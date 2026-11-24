/** @file
  Find the payload that rides behind the UEFI FD inside the kernel field.

  Layout of the kernel field:  [ shim ][ FD 0x300000 ][ payload ]
  The ABL loads the whole field somewhere in RAM, so the payload sits at

      image_base + shim_size + 0x300000

  and the image base is either the ARM64 text_offset convention (0x80080000) or the QCOM
  "Kernel" region (0xC0000000). Both are inside memory-map entries we declare, so the reads
  cannot fault. Only three 16-byte reads are performed.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UefiLib.h>

#define SHIM_SIZE   0x000000F0ULL          /* 240-byte shim */
#define FD_SIZE     0x00300000ULL
#define PAY_OFF     (SHIM_SIZE + FD_SIZE)
#define MAGIC_TXT   "PD1936PAYLOAD"

EFI_STATUS
EFIAPI
ShimFindDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST UINT64  Bases[] = {
    0xC0000000ULL,      /* "Payload" / QCOM "Kernel" region */
    0x80080000ULL,      /* ARM64 text_offset = 0x80000 */
    0x80000000ULL       /* plain DDR base */
  };
  UINT32  I;

  {
    UINT64  Magic = (UINT64)MmioRead32 (0x9BA00000ULL) |
                    ((UINT64)MmioRead32 (0x9BA00004ULL) << 32);
    UINT64  Addr  = (UINT64)MmioRead32 (0x9BA00008ULL) |
                    ((UINT64)MmioRead32 (0x9BA0000CULL) << 32);
    UINT64  Size  = (UINT64)MmioRead32 (0x9BA00010ULL) |
                    ((UINT64)MmioRead32 (0x9BA00014ULL) << 32);
    DEBUG ((DEBUG_ERROR, "ShimFind: handoff magic=%lx addr=0x%lx size=%lu\n",
            Magic, Addr, Size));
  }

  {
    UINT64  HAddr = (UINT64)MmioRead32 (0x9BA00008ULL) |
                    ((UINT64)MmioRead32 (0x9BA0000CULL) << 32);
    UINT64  HSize = (UINT64)MmioRead32 (0x9BA00010ULL) |
                    ((UINT64)MmioRead32 (0x9BA00014ULL) << 32);
    if (HAddr != 0 && HSize != 0) {
      CHAR8  B2[17];
      CopyMem (B2, (VOID *)(UINTN)HAddr, 16);
      B2[16] = 0;
      DEBUG ((DEBUG_ERROR, "ShimFind: handoff addr=0x%lx size=%lu head='%a' %a\n",
              HAddr, HSize, B2,
              (CompareMem (B2, MAGIC_TXT, sizeof (MAGIC_TXT) - 1) == 0) ? "MATCH" : "no"));
      DEBUG ((DEBUG_ERROR, "ShimFind: handoff addr+0x10: %02x %02x %02x %02x\n",
              ((UINT8 *)(UINTN)HAddr)[16], ((UINT8 *)(UINTN)HAddr)[17],
              ((UINT8 *)(UINTN)HAddr)[18], ((UINT8 *)(UINTN)HAddr)[19]));
    }
  }

  for (I = 0; I < ARRAY_SIZE (Bases); ++I) {
    CONST CHAR8  *P = (CONST CHAR8 *)(UINTN)(Bases[I] + PAY_OFF);
    CHAR8        Buf[17];
    CopyMem (Buf, P, 16);
    Buf[16] = 0;
    DEBUG ((DEBUG_ERROR, "ShimFind: base 0x%lx -> head '%a' %a\n",
            Bases[I], Buf,
            (CompareMem (Buf, MAGIC_TXT, sizeof (MAGIC_TXT) - 1) == 0)
              ? "MATCH" : "no"));
  }

  return EFI_SUCCESS;
}
