/** @file
  Verify external storage from the firmware side, with an explicit port-reset attempt.

  The first version answered "is a device attached" (PORTSC.CCS) and "did a mass-storage driver
  bind" (BlockIo). On hardware the port reported connected=1 yet enabled=0 with PLS=Inactive, which
  means the link never trained - so this version adds the two measurements that separate the causes:

    A. Is the xHCI controller even running?  USBCMD.RS / USBSTS.HCH / CRCR, plus the dwc3 wrapper
       registers that gate the PHYs (GUSB2PHYCFG, GUSB3PIPECTL, DCTL).
       If HCH is set, no xHCI driver started it and no amount of port poking will enumerate.
    B. Does an explicit port reset move the port to Enabled?  Set PR, poll PRC/PED, report the
       transition, clear the change bits.  A port that resets to enabled can be enumerated; one that
       stays Inactive has a PHY / role-switch problem below xHCI.

  Reads stay bounded and every report is one line so the console remains readable.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PciIo.h>

#define XHCI_BASE        0x0A600000UL
#define XHCI_PORT_BASE   0x400UL
#define XHCI_USBCMD      0x00UL       /* operational: USBCMD   */
#define XHCI_USBSTS      0x04UL       /* operational: USBSTS   */
#define XHCI_CRCR        0x18UL       /* operational: CRCR     */
#define WATCH_ROUNDS     0      /* was 12 x 2 s; the watch loop is disabled for now */
#define WATCH_INTERVAL   2000000ULL
#define RESET_POLL_US    10000ULL
#define RESET_POLL_MAX   40

/* PORTSC bits */
#define PSC_CCS   (1u << 0)
#define PSC_PED   (1u << 1)
#define PSC_PR    (1u << 4)
#define PSC_PLS   (0xFu << 5)
#define PSC_PP    (1u << 9)
#define PSC_PRC   (1u << 21)

STATIC CONST CHAR8 *
PortState (
  IN UINT32  Sc
  )
{
  switch ((Sc >> 5) & 0xFu) {
    case 0:  return "U0 running";
    case 1:  return "U1";
    case 2:  return "U2";
    case 3:  return "U3 suspended";
    case 4:  return "Disabled";
    case 5:  return "RxDetect";
    case 6:  return "Inactive";
    case 7:  return "Polling";
    case 8:  return "Recovery";
    case 9:  return "HotReset";
    case 10: return "Compliance";
    case 15: return "Resume";
    default: return "reserved";
  }
}

STATIC CONST CHAR8 *
ClassifySector0 (
  IN CONST UINT8  *Buf,
  IN UINTN         Size
  )
{
  UINT16  Sig = 0;

  if (Size >= 0x24 && CompareMem (Buf + 0x20, "NXSB", 4) == 0) {
    return "APFS container (bare, NXSB at 0x20)";
  }
  if (Size >= 0x24 && CompareMem (Buf + 0x20, "APSB", 4) == 0) {
    return "APFS volume superblock (APSB)";
  }
  if (Size >= 0x404 && CompareMem (Buf + 0x400, "H+", 2) == 0) {
    return "HFS+ volume";
  }
  if (Size >= 0x0B && CompareMem (Buf + 3, "EXFAT   ", 8) == 0) {
    return "exFAT volume";
  }
  if (Size >= 512) {
    Sig = (UINT16)(Buf[510] | (Buf[511] << 8));
  }
  if (Sig != 0xAA55) {
    return "no boot signature (unformatted?)";
  }
  if (Size >= 0x5A && CompareMem (Buf + 0x52, "FAT32", 5) == 0) {
    return "FAT32 volume";
  }
  if (Size >= 0x3A && CompareMem (Buf + 0x36, "FAT", 3) == 0) {
    return "FAT12/16 volume";
  }
  return "MBR partition table";
}

STATIC VOID
ReportDevice (
  IN UINTN                  Index,
  IN EFI_BLOCK_IO_PROTOCOL  *Blk
  )
{
  EFI_STATUS  Status;
  UINTN       Bs;
  UINT8       *Buf;
  UINTN       BufSize;
  UINT64      Bytes;

  Bs      = Blk->Media->BlockSize ? Blk->Media->BlockSize : 512;
  if (Bs > 4096) {
    Bs = 4096;
  }
  BufSize = Bs * 2;
  Buf     = AllocateZeroPool (BufSize);
  if (Buf == NULL) {
    return;
  }
  Bytes = (UINT64)(Blk->Media->LastBlock + 1) * Blk->Media->BlockSize;

  Print (L"blk%u: %lu MiB bs=%u removable=%u mediaId=%u\n",
         Index, RShiftU64 (Bytes, 20), Blk->Media->BlockSize,
         Blk->Media->RemovableMedia, Blk->Media->MediaId);

  if (!Blk->Media->MediaPresent) {
    Print (L"blk%u: no media present\n", Index);
    FreePool (Buf);
    return;
  }

  Status = Blk->ReadBlocks (Blk, Blk->Media->MediaId, 0, BufSize, Buf);
  if (EFI_ERROR (Status)) {
    Print (L"blk%u: LBA0 read failed (%r)\n", Index, Status);
    FreePool (Buf);
    return;
  }

  Print (L"blk%u: %a\n", Index, ClassifySector0 (Buf, BufSize));
  if (Blk->Media->LastBlock >= 1) {
    UINT8  *Lba1 = AllocateZeroPool (Bs);
    if (Lba1 != NULL) {
      if (!EFI_ERROR (Blk->ReadBlocks (Blk, Blk->Media->MediaId, 1, Bs, Lba1)) &&
          (CompareMem (Lba1, "EFI PART", 8) == 0)) {
        Print (L"blk%u: LBA1 is a GPT header\n", Index);
      }
      FreePool (Lba1);
    }
  }

  FreePool (Buf);
}

/**
  Try to bring one port up: request a reset and report whether it reaches Enabled.
  Returns the final PORTSC value.
**/
STATIC UINT32
TryPortReset (
  IN UINT32  OpBase,
  IN UINT32  PortIndex
  )
{
  UINT32  Addr = XHCI_BASE + OpBase + XHCI_PORT_BASE + (PortIndex - 1) * 0x10;
  UINT32  Sc;
  UINT32  I;

  Sc = MmioRead32 (Addr);
  if ((Sc & PSC_CCS) == 0) {
    return Sc;
  }

  Print (L"ExtStorage: port %u before reset: %08x state=%a PED=%u\n",
         PortIndex, Sc, PortState (Sc), (Sc >> 1) & 1u);

  /* keep power on, request reset, write back the RW bits only */
  MmioWrite32 (Addr, (Sc & (PSC_PP | PSC_PED)) | PSC_PR);

  for (I = 0; I < RESET_POLL_MAX; ++I) {
    gBS->Stall (RESET_POLL_US);
    Sc = MmioRead32 (Addr);
    if ((Sc & PSC_PRC) != 0) {
      break;
    }
  }

  Print (L"ExtStorage: port %u after reset:  %08x state=%a PED=%u PRC=%u\n",
         PortIndex, Sc, PortState (Sc), (Sc >> 1) & 1u, (Sc >> 21) & 1u);

  /* clear the change bits we caused */
  MmioWrite32 (Addr, (Sc & (PSC_PP | PSC_PED)) | PSC_PRC);
  return Sc;
}

STATIC
VOID
ReportXhci (
  VOID
  )
{
  UINT32      Cap;
  UINT32      Hcs1;
  UINT32      Hcc1;
  UINT32      Op;
  UINT32      I;
  UINT32      Sc;
  UINTN       Count;
  EFI_HANDLE  *Handles;
  EFI_STATUS  Status;

  Cap  = MmioRead32 (XHCI_BASE);
  Op   = Cap & 0xFF;
  Hcs1 = MmioRead32 (XHCI_BASE + 0x04);
  Hcc1 = MmioRead32 (XHCI_BASE + 0x10);
  Print (L"ExtStorage: xHCI caplen=%u ver=%04x ports=%u slots=%u rtsoff=%x\n",
         Op, (Cap >> 16) & 0xFFFF, (Hcs1 >> 24) & 0xFF, Hcs1 & 0xFF, (Hcc1 & 0x1F) << 5);
  Print (L"ExtStorage: USBCMD=%08x USBSTS=%08x (HCH=%u) CRCR=%08x\n",
         MmioRead32 (XHCI_BASE + Op + 0x00), MmioRead32 (XHCI_BASE + Op + 0x04),
         MmioRead32 (XHCI_BASE + Op + 0x04) & 1u, MmioRead32 (XHCI_BASE + Op + 0x14));
  Print (L"ExtStorage: DCBAAP=%08x CONFIG=%08x\n",
         MmioRead32 (XHCI_BASE + Op + 0x20), MmioRead32 (XHCI_BASE + Op + 0x28));

  for (I = 1; I <= 4; ++I) {
    Sc = MmioRead32 (XHCI_BASE + Op + XHCI_PORT_BASE + (I - 1) * 0x10);
    Print (L"ExtStorage: port %u PORTSC=%08x ccs=%u ped=%u pls=%u speed=%u\n",
           I, Sc, Sc & 1u, (Sc >> 1) & 1u, (Sc >> 5) & 0xFu, (Sc >> 10) & 0xFu);
  }

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &Count, &Handles);
  Print (L"ExtStorage: PciIo handles=%u (%r)\n", (UINT32)Count, Status);
  if (!EFI_ERROR (Status)) {
    gBS->FreePool (Handles);
  }
}

EFI_STATUS
EFIAPI
ExtStorageProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32      Cap;
  UINT32      OpBase;
  UINT32      Ports;
  UINT32      Cmd;
  UINT32      Sts;
  UINT32      Crcr;
  UINTN       Round;
  UINTN       I;
  UINTN       Count;
  UINTN       Seen;
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  EFI_BLOCK_IO_PROTOCOL  *Blk;

  ReportXhci ();

  Cap    = MmioRead32 (XHCI_BASE);
  OpBase = Cap & 0xFF;
  Ports  = (MmioRead32 (XHCI_BASE + 0x04) >> 24) & 0xFF;

  /* A. controller and PHY gates */
  Cmd  = MmioRead32 (XHCI_BASE + OpBase + XHCI_USBCMD);
  Sts  = MmioRead32 (XHCI_BASE + OpBase + XHCI_USBSTS);
  Crcr = MmioRead32 (XHCI_BASE + OpBase + XHCI_CRCR);
  Print (L"ExtStorage: XHCI caplen=%u ports=%u GCTL=%08x capdir=%u\n",
         OpBase, Ports, MmioRead32 (XHCI_BASE + 0xC110),
         (MmioRead32 (XHCI_BASE + 0xC110) >> 12) & 3u);
  Print (L"ExtStorage: USBCMD=%08x RS=%u HCH=%u USBSTS=%08x CRCR=%08x\n",
         Cmd, Cmd & 1u, Sts & 1u, Sts, Crcr);
  Print (L"ExtStorage: dwc3 GUSB2PHYCFG=%08x GUSB3PIPECTL=%08x DCTL=%08x DSTS=%08x\n",
         MmioRead32 (XHCI_BASE + 0xC200), MmioRead32 (XHCI_BASE + 0xC2C0),
         MmioRead32 (XHCI_BASE + 0xC704), MmioRead32 (XHCI_BASE + 0xC70C));
  if ((Sts & 1u) != 0) {
    Print (L"ExtStorage: controller is HALTED - no xHCI driver started it\n");
  }

  /* B. per-port state and an explicit reset attempt */
  for (I = 1; (I <= Ports) && (I <= 4); ++I) {
    UINT32  Sc = MmioRead32 (XHCI_BASE + OpBase + XHCI_PORT_BASE + (I - 1) * 0x10);
    Print (L"ExtStorage: port %u PORTSC=%08x CCS=%u PED=%u PP=%u state=%a speed=%u\n",
           I, Sc, (Sc >> 0) & 1u, (Sc >> 1) & 1u, (Sc >> 9) & 1u,
           PortState (Sc), (Sc >> 10) & 0xFu);
    if (((Sc & PSC_CCS) != 0) && ((Sc & PSC_PED) == 0)) {
      TryPortReset (OpBase, I);
    }
  }

  /* C. watch for later plug events and for BlockIo handles appearing */
  Seen = 0;
  for (Round = 0; Round < WATCH_ROUNDS; ++Round) {
    UINT32  Sc = MmioRead32 (XHCI_BASE + OpBase + XHCI_PORT_BASE);

    if (((Sc & PSC_CCS) != 0) && ((Sc & PSC_PED) == 0)) {
      static UINT32  LastTried;
      if (LastTried != Sc) {
        LastTried = Sc;
        TryPortReset (OpBase, 1);
      }
    }

    Handles = NULL;
    Count   = 0;
    Status  = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR (Status)) {
      for (I = 0; I < Count; ++I) {
        Blk    = NULL;
        Status = gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Blk);
        if (EFI_ERROR (Status) || (Blk == NULL) || (Blk->Media == NULL)) {
          continue;
        }
        if ((Round == 0) || (I >= Seen)) {
          ReportDevice (I, Blk);
        }
      }
      gBS->FreePool (Handles);
      Seen = Count;
    }
    gBS->Stall (WATCH_INTERVAL);
  }

  /* D. tally */
  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"ExtStorage: no BlockIo handles at all (%r)\n", Status);
    return EFI_SUCCESS;
  }
  {
    UINTN  Removable = 0;
    for (I = 0; I < Count; ++I) {
      Blk = NULL;
      if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Blk))) {
        continue;
      }
      if ((Blk != NULL) && (Blk->Media != NULL) && Blk->Media->RemovableMedia) {
        Removable++;
      }
    }
    Print (L"ExtStorage: %u block device(s), %u removable\n", Count, Removable);
  }
  gBS->FreePool (Handles);
  return EFI_SUCCESS;
}
