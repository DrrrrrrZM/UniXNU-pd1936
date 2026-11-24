/** @file
  Dump printable strings from the RAM regions the ABL leaves behind, looking for its own log.

  logfs is zero at runtime on this device (the retail logfs image only contains factory logs), so
  the ABL's record of our failed ramdisk boot has to come from RAM:

    0x85CF0000 + 0x00010000   "Boot Info"      (uefiplat.cfg MemLabel)
    0x86000000 + 0x00200000   "SMEM"
    0x85F20000 + 0x00020000   "AOP CMD DB"

  Keywords of interest: Ramdisk / KernelSize / Boot Image / Load Address / DTB / avb / Failed.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

typedef struct {
  UINT64      Base;
  UINT64      Size;
  CONST CHAR8 *Name;
} SCAN_REGION;

STATIC
VOID
Scan (
  IN CONST SCAN_REGION  *R
  )
{
  CONST UINT8  *P   = (CONST UINT8 *)(UINTN)R->Base;
  UINTN         Off = 0;
  UINTN         Emitted = 0;
  CHAR8         Buf[161];

  DEBUG ((DEBUG_ERROR, "BootInfo: --- %a @0x%lx size 0x%lx\n", R->Name, R->Base, R->Size));

  while (Off < R->Size && Emitted < 24) {
    /* find the start of a printable run */
    while (Off < R->Size && (P[Off] < 0x20 || P[Off] > 0x7E)) {
      ++Off;
    }
    UINTN Start = Off;
    while (Off < R->Size && P[Off] >= 0x20 && P[Off] <= 0x7E) {
      ++Off;
    }
    UINTN Len = Off - Start;
    if (Len >= 10) {
      UINTN Copy = (Len > 159) ? 159 : Len;
      CopyMem (Buf, P + Start, Copy);
      Buf[Copy] = 0;
      DEBUG ((DEBUG_ERROR, "BootInfo: %a+0x%05x %a\n", R->Name, Start, Buf));
      ++Emitted;
    }
  }
  if (Emitted == 0) {
    DEBUG ((DEBUG_ERROR, "BootInfo: %a (no printable runs)\n", R->Name));
  }
}

EFI_STATUS
EFIAPI
BootInfoDumpDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST SCAN_REGION  Regions[] = {
    { 0x85CF0000ULL, 0x00010000ULL, "BootInfo" },
    { 0x85F20000ULL, 0x00020000ULL, "AopCmdDb" },
    { 0x86000000ULL, 0x00200000ULL, "SMEM"     }
  };
  UINT32  I;

  DEBUG ((DEBUG_ERROR, "BootInfo: scanning ABL RAM for its log\n"));
  for (I = 0; I < ARRAY_SIZE (Regions); ++I) {
    Scan (&Regions[I]);
  }
  DEBUG ((DEBUG_ERROR, "BootInfo: done\n"));
  return EFI_SUCCESS;
}
