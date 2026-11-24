/** @file
  Report what the USB stack actually produced: how many UsbIo handles and how many BlockIo
  handles exist, and the geometry of each block device. Placed last in the DXE list so the
  XHCI / UsbBus / mass-storage drivers have had a chance to bind.

  With [Depex] TRUE it always runs and always prints a line, so "no USB" and "USB missing from
  the build" cannot be confused.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/IoLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/UsbIo.h>
#include <Protocol/DevicePath.h>

EFI_STATUS
EFIAPI
UsbProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       I;
  EFI_BLOCK_IO_PROTOCOL  *Blk;

  /* give the USB stack a moment: XHCI port reset + enumeration takes time */
  gBS->Stall (2000000);

  /* --- direct XHCI register view: is the port powered, and does anything connect? --- */
  {
    UINT32  Base = 0x0A600000UL;
    UINT32  Cap  = MmioRead32 (Base);
    UINT32  Op   = Cap & 0xFF;
    UINT32  Hcs1 = MmioRead32 (Base + 0x04);
    UINT32  Ppc  = (MmioRead32 (Base + 0x10) >> 3) & 1;   /* HCCPARAMS1.PPC */
    UINT32  Ports = (Hcs1 >> 24) & 0xFF;
    UINT32  I;
    UINT32  Sc;

    DEBUG ((DEBUG_ERROR, "UsbProbe: XHCI caplen=%u ver=%04x ports=%u PPC=%u\n",
            Op, (Cap >> 16) & 0xFFFF, Ports, Ppc));
    /* watch the port for 20 s: plug the stick in after boot and the state change shows up */
    {
      UINT32  T;
      for (T = 0; T < 10; ++T) {
        Sc = MmioRead32 (Base + Op + 0x400);
        DEBUG ((DEBUG_ERROR, "UsbProbe: t=%us PORTSC1=%08x CCS=%u PED=%u PP=%u PLS=%u\n",
                T * 2u, Sc, Sc & 1u, (Sc >> 1) & 1u, (Sc >> 9) & 1u, (Sc >> 5) & 0xFu));
        gBS->Stall (2000000);
      }
    }

    for (I = 1; I <= Ports && I <= 4; ++I) {
      Sc = MmioRead32 (Base + Op + 0x400 + (I - 1) * 0x10);
      DEBUG ((DEBUG_ERROR,
              "UsbProbe: PORTSC%u=%08x CCS=%u PED=%u PP=%u PLS=%u speed=%u\n",
              I, Sc, Sc & 1u, (Sc >> 1) & 1u, (Sc >> 9) & 1u, (Sc >> 5) & 0xFu,
              (Sc >> 10) & 0xFu));
    }
    DEBUG ((DEBUG_ERROR, "UsbProbe: dwc3 GCTL=%08x GSTS=%08x DSTS=%08x capdir=%u\n",
            MmioRead32 (Base + 0xC110), MmioRead32 (Base + 0xC118),
            MmioRead32 (Base + 0xC70C), (MmioRead32 (Base + 0xC110) >> 12) & 3u));
  }


  /* --- UsbIo handles: one per enumerated USB interface --- */
  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL,
                                     &Count, &Handles);
  DEBUG ((DEBUG_ERROR, "UsbProbe: UsbIo handles = %u (%r)\n",
          (UINT32)Count, Status));
  if (!EFI_ERROR (Status)) {
    gBS->FreePool (Handles);
  }

  /* --- BlockIo handles: what a mass-storage stick would produce --- */
  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL,
                                     &Count, &Handles);
  DEBUG ((DEBUG_ERROR, "UsbProbe: BlockIo handles = %u (%r)\n",
          (UINT32)Count, Status));
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  for (I = 0; I < Count; ++I) {
    Blk    = NULL;
    Status = gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Blk);
    if (EFI_ERROR (Status) || (Blk == NULL) || (Blk->Media == NULL)) {
      continue;
    }

    DEBUG ((DEBUG_ERROR,
            "UsbProbe: blk%u last=%u bs=%u present=%u mediaId=%u removable=%u\n",
            (UINT32)I,
            (UINT32)Blk->Media->LastBlock,
            (UINT32)Blk->Media->BlockSize,
            (UINT32)Blk->Media->MediaPresent,
            (UINT32)Blk->Media->MediaId,
            (UINT32)Blk->Media->RemovableMedia));
  }

  gBS->FreePool (Handles);
  return EFI_SUCCESS;
}
