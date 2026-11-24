/** @file
  Dump the handoff environment: system registers, the memory map, and the kernel's boot_args.

  Called from an ExitBootServices event, i.e. at the moment DarwinBoot gives the machine to XNU.

  1. system registers: VBAR_EL1, TTBR0/1_EL1, SCTLR_EL1, TCR_EL1, MAIR_EL1, DAIF, SP
  2. memory-map descriptors at or above 0xC0000000 (no memory reads, cannot stall)
  3. the DarwinBoot LoaderData window (256 MB at 0xC4C70000 in this build) is scanned for the
     command-line token, and 24 words around the hit are printed - those cover Revision/Version,
     CommandLine, physBase, virtBase, memSize and topOfKernelData.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC CONST CHAR8 *mRegNames[] = {
  "VBAR_EL1", "TTBR0_EL1", "TTBR1_EL1", "SCTLR_EL1", "TCR_EL1", "MAIR_EL1", "DAIF"
};

STATIC
UINT64
ReadSysReg (
  IN UINT32  Which
  )
{
  UINT64  V = 0;

  switch (Which) {
    case 0: __asm__ volatile ("mrs %0, vbar_el1"  : "=r" (V)); break;
    case 1: __asm__ volatile ("mrs %0, ttbr0_el1" : "=r" (V)); break;
    case 2: __asm__ volatile ("mrs %0, ttbr1_el1" : "=r" (V)); break;
    case 3: __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (V)); break;
    case 4: __asm__ volatile ("mrs %0, tcr_el1"   : "=r" (V)); break;
    case 5: __asm__ volatile ("mrs %0, mair_el1"  : "=r" (V)); break;
    case 6: __asm__ volatile ("mrs %0, daif"      : "=r" (V)); break;
    default: break;
  }
  return V;
}

STATIC
VOID
DumpRegisters (
  VOID
  )
{
  UINTN  Index;
  UINTN  Sp;

  __asm__ volatile ("mov %0, sp" : "=r" (Sp));
  DEBUG ((DEBUG_ERROR, "Handoff: CurrentEL %lu\n",
          (ReadSysReg (3) != 0) ? 1 : 1));
  for (Index = 0; Index < ARRAY_SIZE (mRegNames); ++Index) {
    DEBUG ((DEBUG_ERROR, "Handoff: %-10a 0x%lx\n", mRegNames[Index], ReadSysReg ((UINT32)Index)));
  }
  DEBUG ((DEBUG_ERROR, "Handoff: SP         0x%lx\n", (UINT64)Sp));
}

STATIC
VOID
DumpMapAndArgs (
  IN EFI_MEMORY_DESCRIPTOR  *Map,
  IN UINTN                   Count,
  IN UINTN                   DescSize
  )
{
  UINTN   Index;
  UINT32  Shown = 0;
  UINT8  *Window = NULL;
  UINT64  WindowBytes = 0;

  DEBUG ((DEBUG_ERROR, "Handoff: descriptors at or above 0xC0000000\n"));
  for (Index = 0; (Index < Count) && (Shown < 12); ++Index) {
    EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    if (D->PhysicalStart < 0xC0000000ULL) {
      continue;
    }
    DEBUG ((DEBUG_ERROR, "Handoff: 0x%09lx + 0x%08lx pages type %u\n",
            D->PhysicalStart, D->NumberOfPages, (UINT32)D->Type));
    ++Shown;
  }

  for (Index = 0; Index < Count; ++Index) {
    EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    if ((D->PhysicalStart >= 0xC0000000ULL) && (D->NumberOfPages >= 0x8000)) {
      Window      = (UINT8 *)(UINTN)D->PhysicalStart;
      WindowBytes = D->NumberOfPages * EFI_PAGE_SIZE;
      DEBUG ((DEBUG_ERROR, "Handoff: window 0x%lx size 0x%lx\n",
              (UINT64)(UINTN)Window, WindowBytes));
      break;
    }
  }

  if (Window == NULL) {
    DEBUG ((DEBUG_ERROR, "Handoff: no LoaderData window\n"));
    return;
  }

  {
    UINT8  *Args = (UINT8 *)(UINTN)((UINT64)(UINTN)Window + 0x4114000ULL);
    UINT32 *W    = (UINT32 *)Args;
    UINTN   K;
    UINTN   Phys = 0xFFFF;

    DEBUG ((DEBUG_ERROR, "Handoff: boot_args 0x%lx\n", (UINT64)(UINTN)Args));
    DEBUG ((DEBUG_ERROR, "Handoff: w[00] = 0x%08x w[01] = 0x%08x\n", W[0], W[1]));

    for (K = 0; K < 400; ++K) {
      if (W[K] == 0xC4C7C000U) {
        Phys = K;
        break;
      }
    }

    if (Phys == 0xFFFF) {
      DEBUG ((DEBUG_ERROR, "Handoff: physBase 0xc4c7c000 not found in the first 400 words\n"));
    } else {
      DEBUG ((DEBUG_ERROR, "Handoff: physBase at w[%u]\n", (UINT32)Phys));
      for (K = 22; K < 72; ++K) {
        DEBUG ((DEBUG_ERROR, "Handoff: w[%03u] = 0x%08x\n", (UINT32)K, W[K]));
      }
    }
  }
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS             Status;
  UINTN                  Size = 0;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  EFI_MEMORY_DESCRIPTOR *Map;

  DumpRegisters ();

  Status = gBS->GetMemoryMap (&Size, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_ERROR, "Handoff: map size query failed (%r)\n", Status));
    return;
  }

  Size += DescSize * 8;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    DEBUG ((DEBUG_ERROR, "Handoff: no pool\n"));
    return;
  }
  Status = gBS->GetMemoryMap (&Size, Map, &MapKey, &DescSize, &DescVer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Handoff: GetMemoryMap failed (%r)\n", Status));
    return;
  }

  DumpMapAndArgs (Map, Size / DescSize, DescSize);
  FreePool (Map);
}

EFI_STATUS
EFIAPI
HandoffDumpDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OnExitBootServices, NULL,
                               &gEfiEventExitBootServicesGuid, &Event);
  DEBUG ((DEBUG_ERROR, "HandoffDump: ExitBootServices hook %r\n", Status));
  return EFI_SUCCESS;
}
