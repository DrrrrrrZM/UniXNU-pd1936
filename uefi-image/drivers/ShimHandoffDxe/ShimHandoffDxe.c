/** @file
  Read the BootShim handoff record and prove the payload is sitting in RAM where it says.

  BootShim stores, at 0x9F9FF000 (last page of our "FV Region" memory-map entry):
      +0x00  'SHIM' magic
      +0x08  payload physical address (= image base + FD_SIZE)
      +0x10  payload size

  The payload is the tail of the kernel field, i.e. it was loaded by the ABL together with the
  FD - no UFS, no USB, no ramdisk involved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define HANDOFF_BASE  0x9BA00000UL
#define SHIM_MAGIC    0x5348494DULL      /* 'SHIM' */

EFI_STATUS
EFIAPI
ShimHandoffDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64  Magic;
  UINT64  Addr;
  UINT64  Size;
  UINT8   Head[16];
  UINTN   I;

  Magic = MmioRead64 (HANDOFF_BASE + 0x00);
  Addr  = MmioRead64 (HANDOFF_BASE + 0x08);
  Size  = MmioRead64 (HANDOFF_BASE + 0x10);

  DEBUG ((DEBUG_ERROR, "ShimHandoff: magic=%lx addr=0x%lx size=%lu\n", Magic, Addr, Size));

  if (Magic != SHIM_MAGIC) {
    DEBUG ((DEBUG_ERROR, "ShimHandoff: no handoff record (magic mismatch)\n"));
    return EFI_SUCCESS;
  }

  if ((Addr == 0) || (Size == 0)) {
    DEBUG ((DEBUG_ERROR, "ShimHandoff: record present but payload empty\n"));
    return EFI_SUCCESS;
  }

  ZeroMem (Head, sizeof (Head));
  CopyMem (Head, (VOID *)(UINTN)Addr, sizeof (Head));
  DEBUG ((DEBUG_ERROR, "ShimHandoff: payload head:"));
  for (I = 0; I < sizeof (Head); ++I) {
    DEBUG ((DEBUG_ERROR, " %02x", Head[I]));
  }
  DEBUG ((DEBUG_ERROR, "\n"));
  DEBUG ((DEBUG_ERROR, "ShimHandoff: payload text: %a\n", (CHAR8 *)(UINTN)Addr));

  return EFI_SUCCESS;
}
