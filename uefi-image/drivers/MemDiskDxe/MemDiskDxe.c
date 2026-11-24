/** @file
  Expose the payload (which rides in the kernel field behind the UEFI FD) as a block device.

  The chain is:
      ABL loads [shim][FD][payload] at 0xC0080000
      BootShim copies the FD to 0x9FC00000 and records {magic, payload_addr, payload_size} at
      0x9BA00000
      this driver publishes EFI_BLOCK_IO_PROTOCOL over that memory
      PartitionDxe + Fat bind automatically
      DarwinBoot reads \EFI\UniXNU\Payloads\kernel.macho from it

  No UFS, no USB, no ramdisk, no secure-world call is involved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

#define HANDOFF_BASE  0x9BA00000ULL
#define SHIM_MAGIC    0x5348494DULL        /* 'SHIM' */
#define SECTOR        512ULL

STATIC EFI_BLOCK_IO_MEDIA  mMedia;
STATIC EFI_BLOCK_IO_PROTOCOL  mBlockIo;
STATIC UINT8  *mBase;
STATIC UINT64  mSize;

STATIC
EFI_STATUS
EFIAPI
MemReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                  MediaId,
  IN EFI_LBA                 Lba,
  IN UINTN                   BufferSize,
  OUT VOID                  *Buffer
  )
{
  UINT64  Off;

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if ((Buffer == NULL) || (This->Media->MediaId != MediaId)) {
    return EFI_INVALID_PARAMETER;
  }

  Off = MultU64x32 (Lba, (UINT32)This->Media->BlockSize);
  if (Off + BufferSize > mSize) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Buffer, mBase + Off, BufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MemWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                  MediaId,
  IN EFI_LBA                 Lba,
  IN UINTN                   BufferSize,
  IN VOID                   *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
MemFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MemDiskDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINT64      Magic;
  UINT64      Size;

  Magic = (UINT64)MmioRead32 ((UINTN)HANDOFF_BASE) |
          ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 4) << 32);
  mBase = (UINT8 *)(UINTN)((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 8) |
                           ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 12) << 32));
  Size  = (UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 16) |
          ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 20) << 32);

  DEBUG ((DEBUG_ERROR, "MemDisk: magic=%lx base=%p size=%lu\n", Magic, mBase, Size));

  if ((Magic != SHIM_MAGIC) || (mBase == NULL) || (Size < SECTOR)) {
    DEBUG ((DEBUG_ERROR, "MemDisk: no payload, nothing to publish\n"));
    return EFI_SUCCESS;
  }

  mSize = Size & ~(SECTOR - 1);

  ZeroMem (&mMedia, sizeof (mMedia));
  mMedia.MediaId          = 0x50443939;         /* 'PD99' */
  mMedia.RemovableMedia   = FALSE;
  mMedia.MediaPresent     = TRUE;
  mMedia.LogicalPartition = FALSE;
  mMedia.ReadOnly         = FALSE;
  mMedia.WriteCaching     = FALSE;
  mMedia.BlockSize        = (UINT32)SECTOR;
  mMedia.IoAlign          = 0;
  mMedia.LastBlock        = (mSize / SECTOR) - 1;

  mBlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mBlockIo.Media       = &mMedia;
  mBlockIo.Reset       = NULL;
  mBlockIo.ReadBlocks  = MemReadBlocks;
  mBlockIo.WriteBlocks = MemWriteBlocks;
  mBlockIo.FlushBlocks = MemFlushBlocks;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  NULL);
  DEBUG ((DEBUG_ERROR, "MemDisk: %lu sectors published (%r)\n",
          (UINT32)(mSize / SECTOR), Status));
  return EFI_SUCCESS;
}
