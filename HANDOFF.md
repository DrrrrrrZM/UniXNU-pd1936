# HANDOFF — PD1936 (vivo iQOO Neo 855) UniXNU bring-up

Session summary for resuming after a break. Device: SM8150 v2, bootloader unlocked,
single slot, `ro.boot.dtb_idx=7` (SM8150 v2, not SM8150P).

## 1. What works today (verified on hardware)

```
fastboot flash boot <3.83 MiB image>
  → ABL loads it
  → kernel @0x8000: BootShim (192/208 B) + UEFI FD (3 MiB)
  → BootShim copies FD to 0x9FC00000, dc cvac / ic iallu, br
  → PrePi/SEC: 32 MMU regions, ValidateMemoryMap passes
  → DXE dispatch → BDS → UniXNU boot manager
  → PANEL SHOWS: "Project Silicium for vivo iQOO Neo 855 / Firmware Version 4.0"
  → boot menu appears; selecting an entry prints "Failed to Find an Operating System"
```

A paint-probe image (`BootShimProbe.S`) turned the panel white and self-reset, which is
how the framebuffer base (`0x9C000000`) and PSCI reset were confirmed.

## 2. Boot image rules (measured, 20+ on-device attempts)

Accept:

```
page 4096 · header v2 · kernel_addr 0x8000 · tags_addr 0x100 · ramdisk_size 0
kernel: uncompressed ARM64 image, legal header (text_offset 0x80000, flags 0xA,
        magic "ARM\x64" at 0x38)
DTB: appended AFTER the kernel, page aligned, header dtb_size = 0
size: ≤ 96 MiB (boot partition is 0x6000000)
```

Reject (each verified on the panel):

| Variant | Result |
|---|---|
ramdisk_size > 0 (any content/size/addr: 4 KiB, 426 KiB Magisk CPIO, 1 MiB, 8 MiB, 75 MiB; addr 0x1000000 / 0xB9800000 / 0) | fastboot |
DTB inside the kernel field, or in a v2 dtb_size section | fastboot |
gzip kernel | fastboot |
kernel field 81.8 MB (embed_payload with the full 75 MiB payload) | **no warning page at all** — rejected before boot |
`recovery` partition, even with a boot image that works from `boot` | fastboot |
modified `vbmeta` (flags 0→2, signature broken) | endless reboot |

## 3. What is NOT the blocker

- **AVB**: `logfs` shows `avb_slot_verify.c:246: ERROR: boot: Hash of data does not match
  digest in descriptor.` and then boots anyway (`unlocked:yes`). Non-fatal. Do not touch
  vbmeta — changing flags breaks its signature and loops the device.
- **Memory map**: 32 contiguous regions, no overlap (this was a real bug, now fixed).
- **FD size/结构**: `_FVH` at 0x28, FvLength 0x300000 == file size, PrePi code at +0x1000.
- **XNU build**: CI produces `kernel.macho` (51.9 MB) + `payload.img` (75 MiB).

## 4. Artifacts

Device-flashable (Windows):

```
D:\123Download\pd1936\boot-hdr-rd0.img           3,833,856 B  ← THE WORKING IMAGE (UEFI menu)
D:\123Download\pd1936\boot-embed-8m.img         12,222,464 B  ← untested: 8 MiB fake payload
D:\123Download\pd1936\boot-embed-payload.img    82,477,056 B  ← rejected (kernel field too big)
D:\123Download\pd1936\boot-addr0-payload.img    82,472,960 B  ← ramdisk variant, fails
D:\123Download\pd1936\misc-zero.img              1,048,576 B  ← clean BCB
D:\123Download\pd1936\vbmeta-flags2.img              8,192 B  ← DO NOT FLASH (loops the device)
D:\123Download\pd1936\payload\...payload.img    78,643,200 B  ← the payload volume
D:\123Download\pd1936\...\kernel.macho          51,945,472 B
```

Host tooling (`pd1936/tools/`, ~30 scripts): `make_stock_shaped_image.py`,
`build_bootimg_with_ramdisk.py`, `build_bootimg_aosp.py`,
`build_bootimg_embed_payload.py`, `decode_bootimg.py`, `patch_vbmeta_flags.py`,
`parse_vbmeta.py`, `build_bootshim_payload.sh`, `scan_abl_dumps.py`,
`check_memmap.py`, `gh_*.py` (dispatch / logs / artifacts / put-file).

Dumps: `D:\123Download\pd1936\log\{logfs.img, rawdump.img (all zero), misc.img}`.
`logfs` is the only source of ABL messages — `rawdump`, `logdump`, `apanic` are all zero.

## 5. Repository

<https://github.com/DrrrrrrZM/UniXNU-pd1936> (public, 21 files)

```
board.toml          — SM8150, ipados-17.7.11-21H461, userdata geometry, [kernel] archives
feeds.toml          — pins zzweiqing/UniXNU-sm8150-common @ f363dcd
configs/            — pd1936_defconfig + pd1936-minimal.config (ADRENO/LEGACY_DISPLAY off)
dts/                — pd1936.dts + vivo-pd1936.dtb (stock DTB #7 + dtbo #5 merged)
patches/xnu/series  — empty (comments break the parser, BOM breaks the path)
scripts/            — prepare-pd1936-uefi.py, make-bootimg.py, package.sh,
                      build-pd1936-ufs-readonly.py (463 lines, read-only SM8150 UFS)
docs/bring-up-log.md— 214 lines: rules, negative results, reference ports, plan
.github/workflows/  — firmware.yml (green), kernel.yml (green after 7 fixes)
```

CI history / fixes worth remembering:

1. `patches/xnu/series` must be empty (comments + BOM break it)
2. upstream `zzz-boot-progress.patch` does not apply to `xnu-10063.141.1` — the workflow
   strips that line from every profile series before building
3. `CONFIG_XNU_NATIVE_ADRENO` / `CONFIG_XNU_NATIVE_LEGACY_DISPLAY` off, otherwise the link
   step demands a `$ADRENO_ARCHIVE` / `$LEGACY_DISPLAY_ARCHIVE` this board has not built
   (the defconfig overrides fragments — edit the defconfig, not just the fragment)
4. pass `GITHUB_TOKEN` (prepare-xnu-source.py hits the API and would 403)
5. put `/opt/homebrew/opt/llvm/bin` and `.../lld/bin` on `$GITHUB_PATH`
6. macOS `sed -i` needs a suffix — filter with `grep -v` instead
7. pin UniXNU to a fetchable revision and keep the submodule init tolerant

## 6. Where it stopped

`embed_payload` (payload inside the kernel field, so `ramdisk_size` stays 0):

- BootShim extended: copies FD → 0x9FC00000, then payload → **0xC0000000**
  (`extract/bootshim-payload.bin`, 208 B, legal ARM64 header)
- memory map: `Payload 0xC0000000 + 0x04B00000` reserved (32 regions, 0 overlap)
- packer: `build_bootimg_embed_payload.py`
- **blocker**: an 81.8 MB kernel field is rejected with no warning page. An 8 MiB variant
  (`boot-embed-8m.img`) has not been flashed yet.

Next action: flash `boot-embed-8m.img`.

- menu → the kernel-field size is the limit; binary search (24 / 48 / 64 MB), then either
  gzip the payload (~50% for kernelcache) or trim it.
- still no warning page → the "payload in the kernel field" shape is rejected; go to the
  storage path (UEFI-side UFS + FAT DXE reading `cache` or a dedicated partition,
  following the K20 Pro `darwin` partition precedent).

Also still open: firmware side must read the payload from `0xC0000000` instead of the
ABL-provided ramdisk pointer (`UniXNURamDiskDxe`), otherwise the menu will keep saying
"Failed to Find an Operating System".

## 7. Restore Android (always available)

```sh
fastboot flash boot   "<ROM>\boot.img"
fastboot flash vbmeta "<ROM>\vbmeta.img"      # only if vbmeta was touched
fastboot flash misc   misc-zero.img           # clears bootonce-bootloader
fastboot reboot
```

A failed attempt leaves `bootonce-bootloader` in `misc`; boot the retail image once
successfully before testing the next image, or the results are contaminated.

## 8. Device facts (measured)

- UFS host `0x1d84000`, ICE `0x1d90000`, PHY `0x1d87000`, SMMU `0x15000000` stream `0x300`
- FD window `0x9FC00000/0x300000`; Display Reserved `0x9C000000/0x02400000`
- DRAM `0x80000000-0xFFD05FFF` + `0x100000000-0x27FFFFFFF`; hole-free 6 GiB at 0x100000000
- partitions: `boot 0x6000000`, `recovery 0x6000000`, `cache 0x10000000`, `system 0x160000000`,
  `userdata 0x3914BF3000`, `logfs 0x800000`, `rawdump 0x8000000`, `misc 0x100000`
- panel: OLED 1080x2340 @62 Hz, no matching preset in the firmware XMLs
- touch: Goodix GT9886, I2C `0xc80000` addr `0x5d`, IRQ GPIO 122, reset GPIO 54
- `unlocked:yes`, `secure:yes`, `product:msmnile`, `max-download-size:805306368`

## 9. Resume block (updated after the UFS probing session)

### 9.1 Current state

UFS, measured through the framebuffer console (`docs/ufs-bringup.md` has the raw numbers):

| Item | State |
|---|---|
UFS controller | **alive**: `HCE=1`, `RUN_STOP=1`, `HCS=0x10F`, all UIC error codes 0 |
Register semantics | **known**: UFSHCI **3.0** (`VER=0x00000300`), offsets from Linux `include/ufs/ufshci.h` |
UTRL | **CPU-readable** at `0x9FFAE000` (`UTRL[0]=0x05000000`) |
ABL's UTRD area | **not CPU-accessible**: `0x05000000` faults unmapped, and *hangs* once mapped as normal memory |
SMMU | **active**: `IDR0=0x00250406` (SMMU-500), `GBPA=0x00005555`, `GFSR=0x00000039` (faults recorded), S2CR location **unknown** |
Boot image | works only with `ramdisk_size=0`; kernel field tolerates ~3-4 MiB (3.0 boots / 7.3 falls back / 11.5 and 81.8 rejected) |
Payload | cannot ride the kernel field (75 MiB) -> storage access required, hence this work |
UEFI input | frozen: buttons are SPMI (arbiter **v5**, PON = APID 65, `sk_open` OK) but observer and TLMM both read 0 |

Not the problem, do not re-investigate: PHY, clocks, HCI reset, boot image layout, AVB,
FD size, XNU kernel build (CI green, `kernel.macho` 51.9 MB).

### 9.2 Next action

Implement UTRL/UTRD + one SCSI **READ(10)** (~250 lines), then read the payload volume:

1. allocate UTRL (512 B) + UTRD (4 KiB) + data buffer, all 4 KiB aligned, in CPU-writable DRAM;
2. `UTRL[0] = {utrd_pa_low, utrd_pa_high}`; program `0x50`/`0x54`; set `0x60 = 1`;
3. build an `UTP_UPIU_TRANSACTION_SPECIFIC` UPIU with a READ(10) CDB (LUN 0, LBA 0, 4 KiB);
4. ring the door bell at `0x58`; poll the UTRD's `OCS` field;
5. read three ways:
   - `OCS == 0` -> read path works, write the BlockIo driver, format `cache` as FAT16, let the
     existing DiskIo/Partition/Fat stack serve `\EFI\UniXNU\Payloads\kernel.macho`;
   - `OCS` error **and** `GFSR` grows -> SMMU is translating; patch `S2CR` for stream `0x300`
     to bypass (base `0x15000000`, S2CR array offset still to be determined);
   - never completes -> the 3.0 queue/door-bell sequence is wrong; re-check against `ufshci.h`.

### 9.3 Do not do

- **Do not map `0x05000000`.** It is not CPU-reachable: unmapped it aborts (`ESR 0x96000006`,
  `FAR 0x05000000`), mapped it hangs the bus.
- **Do not guess another S2CR window.** Reads past `0x15000FFF` hang; only revisit SMMU with
  the SMMU-500 register map in hand, or empirically through OCS/`GFSR` after a transfer.
- **Do not re-examine PHY / clocks / HCI reset.** `HCE=1` with zero UIC errors proves ABL left
  the controller fully operational.
- Do not touch `vbmeta` (signature check loops the device) and do not expect
  `ramdisk_size > 0` or the `recovery` partition to boot.
