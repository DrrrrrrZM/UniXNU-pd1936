/** @file
  Put the SM8150 dwc3 controller into host mode and start xHCI.

  Measured before this driver existed: the port trains fine (a reset takes it to U0 running) but the
  controller stays halted - USBCMD.RS=0, USBSTS.HCH=1, CRCR=0 - so nothing is ever enumerated.

  This module runs early in DXE, before any console exists, so it must not call Print(): that asserts
  on a NULL ConOut. All diagnostics go through DEBUG, and the console-friendly summary of the
  controller state lives in ExtStorageProbeDxe, which runs once the console drivers are up.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define DWC3_BASE          0x0A600000UL
#define DWC3_REG_LEN       0x10000UL

#define DWC3_GCTL          0x0C110
#define DWC3_GSTS          0x0C118
#define DWC3_GUSB2PHYCFG   0x0C200
#define DWC3_DCTL          0x0C704
#define DWC3_DSTS          0x0C70C

#define GCTL_PRTCAP_MASK      (3u << 12)
#define GCTL_PRTCAP_HOST      (1u << 12)
#define GUSB2PHYCFG_SUSPHY    (1u << 6)
#define GUSB2PHYCFG_PHYSOFTRST (1u << 31)
#define DCTL_CSFTRST          (1u << 30)

/* xHCI operational offsets, relative to caplen */
#define OP_USBCMD   0x00
#define OP_USBSTS   0x04
#define OP_CRCR     0x18
#define OP_DCBAAP   0x20
#define OP_CONFIG   0x28

/* runtime interrupter 0, relative to rtsoff */
#define IR_ERSTSZ   0x28
#define IR_ERSTBA   0x30
#define IR_ERDP     0x38

#define USBCMD_RS    (1u << 0)
#define USBCMD_HCRST (1u << 1)
#define USBCMD_INTE  (1u << 2)
#define USBSTS_HCH   (1u << 0)
#define USBSTS_CNR   (1u << 11)

STATIC
EFI_STATUS
AllocLowPage (
  OUT VOID   **Buffer,
  OUT UINT64 *Phys
  )
{
  EFI_PHYSICAL_ADDRESS  Addr = 0xFFFFFFFFULL;    /* keep DMA addresses under 4 GiB */
  EFI_STATUS            Status;

  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, 1, &Addr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  ZeroMem ((VOID *)(UINTN)Addr, EFI_PAGE_SIZE);
  *Buffer = (VOID *)(UINTN)Addr;
  *Phys   = (UINT64)Addr;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
XhciStart (
  IN UINT32  OpBase
  )
{
  UINT32        Hcs1;
  UINT32        Hcc1;
  UINT32        RtOff;
  UINT32        MaxSlots;
  UINT32        MaxPorts;
  UINT32        Val;
  UINT32        N;
  EFI_STATUS    Status;
  VOID          *Dcbaa;
  VOID          *CmdRing;
  VOID          *Erst;
  VOID          *EvtRing;
  UINT64        DcbaaPhys;
  UINT64        CmdRingPhys;
  UINT64        ErstPhys;
  UINT64        EvtRingPhys;

  Hcs1     = MmioRead32 (DWC3_BASE + 0x04);
  MaxSlots = Hcs1 & 0xFF;
  MaxPorts = (Hcs1 >> 24) & 0xFF;
  Hcc1     = MmioRead32 (DWC3_BASE + 0x10);
  RtOff    = (Hcc1 & 0x1F) << 5;
  DEBUG ((DEBUG_ERROR, "UsbHost: hcs1=%08x slots=%u ports=%u rtsoff=%x\n",
          Hcs1, MaxSlots, MaxPorts, RtOff));

  for (N = 0; N < 100; ++N) {
    if ((MmioRead32 (DWC3_BASE + OpBase + OP_USBSTS) & USBSTS_CNR) == 0) {
      break;
    }
    gBS->Stall (1000);
  }
  DEBUG ((DEBUG_ERROR, "UsbHost: CNR clear after %u ms\n", N));

  MmioWrite32 (DWC3_BASE + OpBase + OP_USBCMD, USBCMD_HCRST);
  for (N = 0; N < 200; ++N) {
    Val = MmioRead32 (DWC3_BASE + OpBase + OP_USBCMD);
    if (((Val & USBCMD_HCRST) == 0) &&
        ((MmioRead32 (DWC3_BASE + OpBase + OP_USBSTS) & USBSTS_CNR) == 0)) {
      break;
    }
    gBS->Stall (1000);
  }
  DEBUG ((DEBUG_ERROR, "UsbHost: HCRST done after %u ms usbcmd=%08x usbsts=%08x\n", N,
          MmioRead32 (DWC3_BASE + OpBase + OP_USBCMD),
          MmioRead32 (DWC3_BASE + OpBase + OP_USBSTS)));

  Status = AllocLowPage (&Dcbaa, &DcbaaPhys);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHost: dcbaa alloc failed %r\n", Status));
    return Status;
  }
  Status = AllocLowPage (&CmdRing, &CmdRingPhys);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHost: command ring alloc failed %r\n", Status));
    return Status;
  }
  Status = AllocLowPage (&Erst, &ErstPhys);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHost: erst alloc failed %r\n", Status));
    return Status;
  }
  Status = AllocLowPage (&EvtRing, &EvtRingPhys);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHost: event ring alloc failed %r\n", Status));
    return Status;
  }

  *(UINT64 *)(UINTN)Erst        = EvtRingPhys;
  *(UINT32 *)((UINTN)Erst + 8)  = 256;
  *(UINT32 *)((UINTN)Erst + 12) = 0;
  MmioWrite32 (DWC3_BASE + OpBase + IR_ERSTSZ, 1);
  MmioWrite64 (DWC3_BASE + OpBase + IR_ERSTBA, ErstPhys);
  MmioWrite64 (DWC3_BASE + OpBase + IR_ERDP, EvtRingPhys);
  MmioWrite64 (DWC3_BASE + OpBase + OP_DCBAAP, DcbaaPhys);
  MmioWrite64 (DWC3_BASE + OpBase + OP_CRCR, (CmdRingPhys & ~0x3FULL) | 1ULL);
  MmioWrite32 (DWC3_BASE + OpBase + OP_CONFIG, (MaxSlots < 32) ? MaxSlots : 32);

  MmioWrite32 (DWC3_BASE + OpBase + OP_USBCMD, USBCMD_RS | USBCMD_INTE);
  for (N = 0; N < 200; ++N) {
    if ((MmioRead32 (DWC3_BASE + OpBase + OP_USBSTS) & USBSTS_HCH) == 0) {
      break;
    }
    gBS->Stall (1000);
  }
  DEBUG ((DEBUG_ERROR, "UsbHost: RS=1 waited %u ms usbcmd=%08x usbsts=%08x crcr=%08x\n", N,
          MmioRead32 (DWC3_BASE + OpBase + OP_USBCMD),
          MmioRead32 (DWC3_BASE + OpBase + OP_USBSTS),
          MmioRead32 (DWC3_BASE + OpBase + OP_CRCR)));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UsbHostDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Cap;
  UINT32      Gctl;
  UINT32      Phy;
  UINT32      N;
  UINT32      OpBase;

  Cap    = MmioRead32 (DWC3_BASE);
  OpBase = Cap & 0xFF;
  Gctl   = MmioRead32 (DWC3_BASE + DWC3_GCTL);
  Phy    = MmioRead32 (DWC3_BASE + DWC3_GUSB2PHYCFG);
  DEBUG ((DEBUG_ERROR, "UsbHost: CAP=%08x GCTL=%08x GUSB2PHYCFG=%08x caplen=%u\n",
          Cap, Gctl, Phy, OpBase));

  /* USB2 PHY out of suspend */
  Phy &= ~(GUSB2PHYCFG_SUSPHY | GUSB2PHYCFG_PHYSOFTRST);
  MmioWrite32 (DWC3_BASE + DWC3_GUSB2PHYCFG, Phy);
  gBS->Stall (100);

  /* host role */
  Gctl = (Gctl & ~GCTL_PRTCAP_MASK) | GCTL_PRTCAP_HOST;
  MmioWrite32 (DWC3_BASE + DWC3_GCTL, Gctl);
  gBS->Stall (1000);

  /* core soft reset, then re-apply the role */
  MmioWrite32 (DWC3_BASE + DWC3_DCTL, MmioRead32 (DWC3_BASE + DWC3_DCTL) | DCTL_CSFTRST);
  for (N = 0; N < 200; ++N) {
    if ((MmioRead32 (DWC3_BASE + DWC3_DCTL) & DCTL_CSFTRST) == 0) {
      break;
    }
    gBS->Stall (1000);
  }
  Gctl = MmioRead32 (DWC3_BASE + DWC3_GCTL);
  MmioWrite32 (DWC3_BASE + DWC3_GCTL, (Gctl & ~GCTL_PRTCAP_MASK) | GCTL_PRTCAP_HOST);
  gBS->Stall (1000);
  DEBUG ((DEBUG_ERROR, "UsbHost: role=%u DCTL=%08x GSTS=%08x DSTS=%08x\n",
          (MmioRead32 (DWC3_BASE + DWC3_GCTL) >> 12) & 3u,
          MmioRead32 (DWC3_BASE + DWC3_DCTL),
          MmioRead32 (DWC3_BASE + DWC3_GSTS),
          MmioRead32 (DWC3_BASE + DWC3_DSTS)));

  Status = XhciStart (OpBase);
  DEBUG ((DEBUG_ERROR, "UsbHost: xhci start -> %r\n", Status));

  /* publish the window; coherent DMA avoids a mapping failure inside XhciDxe */
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeXhci,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,
             NULL,
             1,
             (UINTN)DWC3_BASE,
             (UINTN)DWC3_REG_LEN);
  DEBUG ((DEBUG_ERROR, "UsbHost: register window -> %r\n", Status));

  return EFI_SUCCESS;
}
