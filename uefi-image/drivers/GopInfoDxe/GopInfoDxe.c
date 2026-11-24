/** @file
  Report the GOP mode the firmware actually published, plus sample pixels from the scanout.

  XNU's display path is GopFramebuffer over this scanout, so a wrong PixelFormat/PixelsPerScanLine
  shows up as colour-channel or stride artefacts (green blocks).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

EFI_STATUS
EFIAPI
GopInfoDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                        Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL     *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINT32                           *Pix;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL)) {
    DEBUG ((DEBUG_ERROR, "GopInfo: no GOP (%r)\n", Status));
    return EFI_SUCCESS;
  }

  Info = Gop->Mode->Info;
  DEBUG ((DEBUG_ERROR, "GopInfo: mode %u  %ux%u  format=%u  ppsl=%u  fb=0x%lx size=0x%lx\n",
          Gop->Mode->Mode, Info->HorizontalResolution, Info->VerticalResolution,
          Info->PixelFormat, Info->PixelsPerScanLine,
          (UINT64)Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize));

  /* PixelFormat: 0 = R,G,B reserved ; 1 = B,G,R reserved ; 2 = bit mask ; 3 = blt only */
  Pix = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  if (Pix != NULL) {
    DEBUG ((DEBUG_ERROR, "GopInfo: px[0]=%08x px[1]=%08x px[stride]=%08x px[stride*100]=%08x\n",
            Pix[0], Pix[1], Pix[Info->PixelsPerScanLine], Pix[Info->PixelsPerScanLine * 100]));
  }

  return EFI_SUCCESS;
}
