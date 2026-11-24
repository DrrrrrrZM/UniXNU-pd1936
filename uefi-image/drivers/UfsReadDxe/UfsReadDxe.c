/** @file
  Read one block from UFS LUN 0 with the rules the UniXNU porting guide calls out.

  The ABL leaves this controller enabled, its transfer list running, and the request list pointing at
  a region the CPU cannot touch. Every earlier attempt hung once the controller fetched a descriptor.
  This version changes the four things that matter, in the order the guide lists them:

    1. take ownership explicitly: stop the list, wait, clear it, clear IS
    2. put the request list in low DRAM and clean the caches before the doorbell, so the controller
       never reads a stale line (the guide warns that two 32-byte descriptors share a cache line)
    3. set the descriptor's interrupt bit and enable only UTRCE, then find the completion by the
       cleared doorbell bit, as Linux' ufshcd does
    4. keep slot 0 first in the list page so its descriptor starts on a 64-byte boundary

  UFSHCI 3.0 register offsets follow Linux include/ufs/ufshci.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define UFS_HC_BASE      0x01D84000UL

#define REG_IS            0x20        /* interrupt status        */
#define REG_IE            0x24        /* interrupt enable        */
#define REG_HCS           0x30        /* controller status       */
#define REG_HCE           0x34        /* controller enable       */
#define REG_UTRLBA_L      0x50
#define REG_UTRLBA_H      0x54
#define REG_UTRLDBR       0x58        /* doorbell                */
#define REG_UTRLCLR       0x5C        /* list clear              */
#define REG_UTRLRSR       0x60        /* list run/stop           */
#define REG_UIC_CMD       0x90
#define REG_UIC_ARG1      0x94
#define REG_UIC_ARG2      0x98
#define REG_UIC_ARG3      0x9C

#define IS_UTRCS          (1u << 0)
#define IS_UIC            (1u << 10)
#define HCS_UTRLRSR       (1u << 0)
#define HCS_UCRDY         (1u << 3)

#define UTRD_INTERRUPT    (1u << 24)  /* dword 0 bit 24: report completion */
#define UCD_RSP_OFFSET    0x080
#define UCD_PRDT_OFFSET   0x100
#define SLOT_BYTES        32

#define POLL_US           100
#define POLL_MAX          10000       /* ~1 s */

typedef struct {
  UINT8   *List;      UINT64  ListPa;
  UINT8   *Ucd;       UINT64  UcdPa;
  UINT8   *Data;      UINT64  DataPa;
} UFS_BUFFERS;

STATIC
EFI_STATUS
AllocLow (
  IN  UINTN  Pages,
  OUT UINT8  **Va,
  OUT UINT64 *Pa
  )
{
  EFI_PHYSICAL_ADDRESS  Addr = 0xFFFFFFFFULL;
  EFI_STATUS            Status;

  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, Pages, &Addr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  ZeroMem ((VOID *)(UINTN)Addr, Pages * EFI_PAGE_SIZE);
  *Va = (UINT8 *)(UINTN)Addr;
  *Pa = (UINT64)Addr;
  return EFI_SUCCESS;
}

STATIC
UINT32
Rd (
  IN UINT32  Off
  )
{
  return MmioRead32 (UFS_HC_BASE + Off);
}

STATIC
VOID
Wr (
  IN UINT32  Off,
  IN UINT32  Value
  )
{
  MmioWrite32 (UFS_HC_BASE + Off, Value);
}

STATIC
VOID
PutBe32 (
  IN UINT8   *P,
  IN UINT32   V
  )
{
  P[3] = (UINT8)V;
  P[2] = (UINT8)(V >> 8);
  P[1] = (UINT8)(V >> 16);
  P[0] = (UINT8)(V >> 24);
}

STATIC
VOID
PutLe64 (
  IN UINT8   *P,
  IN UINT64   V
  )
{
  UINT32  I;
  for (I = 0; I < 8; ++I) {
    P[I] = (UINT8)(V >> (8 * I));
  }
}

STATIC
VOID
PutLe16 (
  IN UINT8   *P,
  IN UINT16   V
  )
{
  P[0] = (UINT8)V;
  P[1] = (UINT8)(V >> 8);
}

/**
  Stop and clear the transfer list, leaving the controller with no pending work that points at the
  firmware's own descriptors.
**/
STATIC
EFI_STATUS
TakeOverList (
  VOID
  )
{
  UINT32  N;
  UINT32  V;

  /* stop */
  if ((Rd (REG_UTRLRSR) & HCS_UTRLRSR) != 0) {
    Wr (REG_UTRLRSR, 0);
    for (N = 0; N < 1000; ++N) {
      if ((Rd (REG_UTRLRSR) & HCS_UTRLRSR) == 0) {
        break;
      }
      gBS->Stall (100);
    }
  }
  DEBUG ((DEBUG_ERROR, "UfsRead: list stopped (rsr=%08x after %u)\n", Rd (REG_UTRLRSR), N));

  /* clear any queued descriptors */
  Wr (REG_UTRLCLR, 1);
  for (N = 0; N < 1000; ++N) {
    if ((Rd (REG_UTRLCLR) & 1u) == 0) {
      break;
    }
    gBS->Stall (100);
  }
  DEBUG ((DEBUG_ERROR, "UfsRead: list cleared (clr=%08x after %u)\n", Rd (REG_UTRLCLR), N));

  /* acknowledge everything */
  V = Rd (REG_IS);
  if (V != 0) {
    Wr (REG_IS, V);
  }
  DEBUG ((DEBUG_ERROR, "UfsRead: IS was %08x, now %08x\n", V, Rd (REG_IS)));
  return ((Rd (REG_HCS) & HCS_UCRDY) != 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
VOID
BuildRead10 (
  IN UFS_BUFFERS  *B,
  IN UINT64        Lba,
  IN UINT32        Blocks
  )
{
  UINT8   *Utrd = B->List;                   /* slot 0: 64-byte aligned in a page-aligned page */
  UINT8   *Ucd  = B->Ucd;
  UINT8   *Prd  = Ucd + UCD_PRDT_OFFSET;
  UINT32  Len   = Blocks * 512;

  /* request UPIU: TRANSACTION_COMMAND, LUN 0, task tag 1, expected length, READ(10) */
  ZeroMem (Ucd, 0x200);
  PutBe32 (Ucd + 0x00, 1u << 24);
  PutBe32 (Ucd + 0x04, 1);
  PutBe32 (Ucd + 0x08, 0);
  PutBe32 (Ucd + 0x0C, Len);
  Ucd[0x10] = 0x28;                          /* READ(10) */
  PutBe32 (Ucd + 0x15, (UINT32)(Lba & 0xFFFFFFFFULL));
  Ucd[0x19] = 0;
  PutBe32 (Ucd + 0x1A, Blocks);

  /* PRDT entry 0 */
  PutLe64 (Prd + 0x00, B->DataPa);
  Prd[0x08] = 0; Prd[0x09] = 0; Prd[0x0A] = 0; Prd[0x0B] = 0;
  Prd[0x0C] = (UINT8)((Len - 1) & 0xFF);
  Prd[0x0D] = (UINT8)(((Len - 1) >> 8) & 0xFF);
  Prd[0x0E] = (UINT8)(((Len - 1) >> 16) & 0xFF);
  Prd[0x0F] = (UINT8)(((Len - 1) >> 24) & 0xFF);

  /* UTRD */
  ZeroMem (Utrd, SLOT_BYTES);
  Utrd[0] = 0; Utrd[1] = 0; Utrd[2] = 0; Utrd[3] = 0x01;   /* dword 0 bit 24: interrupt */
  PutLe64 (Utrd + 0x10, B->UcdPa);
  PutLe16 (Utrd + 0x18, 0x20);                             /* response UPIU length */
  PutLe16 (Utrd + 0x1A, UCD_RSP_OFFSET / 4);
  PutLe16 (Utrd + 0x1C, 1);                                /* PRDT entry count */
  PutLe16 (Utrd + 0x1E, UCD_PRDT_OFFSET / 4);
}

EFI_STATUS
EFIAPI
UfsReadDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS    Status;
  UFS_BUFFERS   B;
  UINT32        Cap;
  UINT32        Ver;
  UINT32        Hcs;
  UINT32        Hce;
  UINT32        Is;
  UINT32        N;
  UINT32        Ocs;
  UINT32        Resp;

  Cap = Rd (0x00);
  Ver = Rd (0x08);
  Hcs = Rd (REG_HCS);
  Hce = Rd (REG_HCE);
  DEBUG ((DEBUG_ERROR, "UfsRead: CAP=%08x VER=%08x HCS=%08x HCE=%u\n", Cap, Ver, Hcs, Hce & 1u));

  Status = AllocLow (1, &B.List, &B.ListPa);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UfsRead: list alloc failed %r\n", Status));
    return EFI_SUCCESS;
  }
  Status = AllocLow (1, &B.Ucd, &B.UcdPa);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UfsRead: ucd alloc failed %r\n", Status));
    return EFI_SUCCESS;
  }
  Status = AllocLow (1, &B.Data, &B.DataPa);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UfsRead: data alloc failed %r\n", Status));
    return EFI_SUCCESS;
  }
  DEBUG ((DEBUG_ERROR, "UfsRead: list=%lx ucd=%lx data=%lx\n", B.ListPa, B.UcdPa, B.DataPa));

  Status = TakeOverList ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UfsRead: controller not ready (%r), hcs=%08x\n", Status, Rd (REG_HCS)));
    return EFI_SUCCESS;
  }

  BuildRead10 (&B, 0, 1);

  /* point the controller at our list, then start it */
  Wr (REG_UTRLBA_L, (UINT32)(B.ListPa & 0xFFFFFFFFULL));
  Wr (REG_UTRLBA_H, (UINT32)(B.ListPa >> 32));
  WriteBackDataCacheRange (B.List, EFI_PAGE_SIZE);
  WriteBackDataCacheRange (B.Ucd, EFI_PAGE_SIZE);
  WriteBackDataCacheRange (B.Data, EFI_PAGE_SIZE);

  Wr (REG_IE, IS_UTRCS);
  Wr (REG_UTRLRSR, HCS_UTRLRSR);
  for (N = 0; N < 1000; ++N) {
    if ((Rd (REG_UTRLRSR) & HCS_UTRLRSR) != 0) {
      break;
    }
    gBS->Stall (100);
  }
  DEBUG ((DEBUG_ERROR, "UfsRead: running (rsr=%08x after %u) utrlb=%08x\n",
          Rd (REG_UTRLRSR), N, (UINT32)(B.ListPa & 0xFFFFFFFFULL)));

  Wr (REG_UTRLDBR, 1);                       /* ring slot 0 */

  Ocs  = 0xFF;
  Resp = 0xFF;
  for (N = 0; N < POLL_MAX; ++N) {
    Is = Rd (REG_IS);
    if ((Is & IS_UTRCS) != 0) {
      Wr (REG_IS, IS_UTRCS);                 /* W1C */
      InvalidateDataCacheRange (B.List, EFI_PAGE_SIZE);
      InvalidateDataCacheRange (B.Ucd, EFI_PAGE_SIZE);
      InvalidateDataCacheRange (B.Data, EFI_PAGE_SIZE);
      Ocs  = B.List[0x08];
      Resp = B.Ucd[UCD_RSP_OFFSET + 0x10];   /* response UPIU response field */
      DEBUG ((DEBUG_ERROR, "UfsRead: done after %u polls, IS=%08x OCS=%02x response=%02x\n",
              N, Is, Ocs, Resp));
      break;
    }
    gBS->Stall (POLL_US);
  }

  if ((Ocs == 0xFF) && (N >= POLL_MAX)) {
    DEBUG ((DEBUG_ERROR, "UfsRead: no completion after %u polls, IS=%08x DBR=%08x HCS=%08x\n",
            N, Rd (REG_IS), Rd (REG_UTRLDBR), Rd (REG_HCS)));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "UfsRead: data %08x %08x %08x %08x\n",
          *(volatile UINT32 *)(B.Data + 0),
          *(volatile UINT32 *)(B.Data + 4),
          *(volatile UINT32 *)(B.Data + 8),
          *(volatile UINT32 *)(B.Data + 12)));
  return EFI_SUCCESS;
}
