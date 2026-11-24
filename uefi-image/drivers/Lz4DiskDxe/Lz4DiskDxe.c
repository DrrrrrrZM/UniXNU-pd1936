/** @file
  Decompress the LZ4-packed payload and expose it as a block device.

  Pipeline:
      kernel field = [ shim ][ FD ][ payload.img.lz4 ]   (12.2 MB, fits the ~36 MB ABL budget)
      BootShim records {magic, addr, size} of the compressed blob at 0x9BA00000
      this driver decompresses the legacy-LZ4 stream to 0x80000000 (75 MB, reserved in our map)
      EFI_BLOCK_IO_PROTOCOL over that buffer -> PartitionDxe + Fat -> DarwinBoot

  Legacy LZ4 frame: magic 0x184C2102, then blocks of [le32 compressed_size][lz4 block data],
  each block expanding to at most 8 MiB.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#define HANDOFF_BASE    0x9BA00000ULL
#define SHIM_MAGIC      0x5348494DULL
#define DST_BASE        0x100000000ULL
#define DST_SIZE        (200ULL * 1024 * 1024)     /* payload.img from CI: 78,643,200 B */
#define LZ4_MAGIC       0x184C2102UL
#define SECTOR          512ULL

STATIC EFI_BLOCK_IO_MEDIA     mMedia;
STATIC EFI_BLOCK_IO_PROTOCOL  mBlockIo;
STATIC UINT8                 *mDst;

/* --- classic LZ4 block decoder ------------------------------------------------------------ */
STATIC
EFI_STATUS
Lz4Block (
  IN  CONST UINT8  *Src,
  IN  UINTN         SrcLen,
  OUT UINT8        *Dst,
  OUT UINTN        *DstLen
  )
{
  CONST UINT8  *Ip = Src;
  CONST UINT8  *Iend = Src + SrcLen;
  UINT8        *Op = Dst;
  UINT8        *Oend = Dst + *DstLen;

  while (Ip < Iend) {
    UINTN  Token = *Ip++;
    UINTN  LitLen = Token >> 4;
    UINTN  MatchLen;
    UINTN  Offset;
    UINT8  *Match;

    if (LitLen == 15) {
      UINT8  S;
      do {
        if (Ip >= Iend) {
          return EFI_VOLUME_CORRUPTED;
        }
        S = *Ip++;
        LitLen += S;
      } while (S == 255);
    }
    if ((Ip + LitLen > Iend) || (Op + LitLen > Oend)) {
      return EFI_VOLUME_CORRUPTED;
    }
    CopyMem (Op, Ip, LitLen);
    Ip += LitLen;
    Op += LitLen;

    if (Ip >= Iend) {
      break;                       /* last sequence has literals only */
    }

    Offset = (UINTN)Ip[0] | ((UINTN)Ip[1] << 8);
    Ip    += 2;
    if ((Offset == 0) || ((UINTN)(Op - Dst) < Offset)) {
      return EFI_VOLUME_CORRUPTED;
    }

    MatchLen = Token & 0xF;
    if (MatchLen == 15) {
      UINT8  S;
      do {
        if (Ip >= Iend) {
          return EFI_VOLUME_CORRUPTED;
        }
        S = *Ip++;
        MatchLen += S;
      } while (S == 255);
    }
    MatchLen += 4;

    Match = Op - Offset;
    if (Op + MatchLen > Oend) {
      return EFI_VOLUME_CORRUPTED;
    }
    while (MatchLen-- != 0) {
      *Op++ = *Match++;            /* byte-wise: matches may overlap */
    }
  }

  *DstLen = (UINTN)(Op - Dst);
  return EFI_SUCCESS;
}

/* --- block I/O ----------------------------------------------------------------------------- */
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
  Off = MultU64x32 (Lba, This->Media->BlockSize);
  if (Off + BufferSize > DST_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (Buffer, mDst + Off, BufferSize);
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
MemFlush (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Lz4DiskDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS   Status;
  EFI_HANDLE   Handle;
  UINT64       Magic;
  UINT64       Src;
  UINT64       Size;
  CONST UINT8  *P;
  CONST UINT8  *End;
  UINTN        Out = 0;
  UINT32       Blocks = 0;

  Magic = (UINT64)MmioRead32 ((UINTN)HANDOFF_BASE) |
          ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 4) << 32);
  Src  = (UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 8) |
         ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 12) << 32);
  Size = (UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 16) |
         ((UINT64)MmioRead32 ((UINTN)HANDOFF_BASE + 20) << 32);

  DEBUG ((DEBUG_ERROR, "Lz4Disk: handoff magic=%lx src=0x%lx size=%lu\n", Magic, Src, Size));
  if ((Magic != SHIM_MAGIC) || (Src == 0) || (Size < 8)) {
    DEBUG ((DEBUG_ERROR, "Lz4Disk: no compressed payload\n"));
    return EFI_SUCCESS;
  }

  P = (CONST UINT8 *)(UINTN)Src;
  if (*(UINT32 *)P != LZ4_MAGIC) {
    DEBUG ((DEBUG_ERROR, "Lz4Disk: bad magic %08x\n", *(UINT32 *)P));
    return EFI_SUCCESS;
  }

  mDst = (UINT8 *)(UINTN)DST_BASE;
  ZeroMem (mDst, DST_SIZE);

  P   += 4;
  End  = (CONST UINT8 *)(UINTN)(Src + Size);
  while (P + 4 <= End) {
    UINT32  Clen = *(UINT32 *)P;
    UINTN   Dlen = DST_SIZE - Out;
    if ((Clen == 0) || (P + 4 + Clen > End)) {
      break;
    }
    Status = Lz4Block (P + 4, Clen, mDst + Out, &Dlen);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Lz4Disk: block %u failed (%r) at out=%lu\n", Blocks, Status, Out));
      return EFI_SUCCESS;
    }
    Out += Dlen;
    ++Blocks;
    P   += 4 + Clen;
    if (Out >= DST_SIZE) {
      break;
    }
  }

  DEBUG ((DEBUG_ERROR, "Lz4Disk: %u blocks -> %lu bytes decompressed\n", Blocks, Out));
  if (Out < 1024) {
    DEBUG ((DEBUG_ERROR, "Lz4Disk: decompressed image too small, aborting\n"));
    return EFI_SUCCESS;
  }

  ZeroMem (&mMedia, sizeof (mMedia));
  mMedia.MediaId          = 0x4C5A3444;      /* 'LZ4D' */
  mMedia.MediaPresent     = TRUE;
  mMedia.BlockSize        = (UINT32)SECTOR;
  mMedia.LastBlock        = (Out / SECTOR) - 1;

  mBlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mBlockIo.Media       = &mMedia;
  mBlockIo.ReadBlocks  = MemReadBlocks;
  mBlockIo.WriteBlocks = MemWriteBlocks;
  mBlockIo.FlushBlocks = MemFlush;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gEfiBlockIoProtocolGuid, &mBlockIo, NULL);
  DEBUG ((DEBUG_ERROR, "Lz4Disk: published %lu sectors (%r)\n", (UINT32)(Out / SECTOR), Status));
  return EFI_SUCCESS;
}
