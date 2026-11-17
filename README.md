# UniXNU — vivo iQOO Neo 855 (PD1936 / V1936A)

Format 2 device repository for running XNU-based operating systems on the vivo
iQOO Neo 855 (SM8150 v2, 1080x2340, UFS 3.x, single-slot A-only).

**Status**: the boot chain runs on hardware up to the UniXNU boot manager, the XNU kernel
builds in CI, and storage bring-up is in progress because this board refuses every other
way of delivering the payload.

| Stage | State |
|---|---|
| `fastboot flash boot` of a self-built image | works (3.8 MB image) |
| BootShim -> UEFI FD relocation -> SEC/PrePi | works (Silicium banner on the panel) |
| DXE dispatch, memory map validation | works (32 regions, no overlap) |
| Framebuffer DEBUG console | **works** - the only observability this board has (no UART) |
| UniXNU boot menu | **displayed on hardware** |
| `kernel.macho` + `payload.img` | **built by CI** (51.9 MB / 75 MiB) |
| Payload delivery | **blocked**: `ramdisk_size` must be 0, and the kernel field only tolerates ~3-4 MiB (3.0 boots, 7.3 falls back) |
| UFS controller (for a payload partition) | command engine **alive** (UIC commands complete, clocks on, not in reset), but the controller's own **memory access is fenced**: the DMA fetch hangs while UIC works, there is no IOMMU binding to fix, and the secure world refuses `RESTORE_SEC_CFG` for all device ids |
| Menu input (buttons) | frozen: all three keys are SPMI (arbiter v5); observer and TLMM both read 0 |
| iOS / macOS userland | out of scope for now |

What was ruled out, with on-hardware evidence (`docs/bring-up-log.md`,
`docs/ufs-bringup.md`): any `ramdisk_size > 0`, the `recovery` partition as an entry point,
a modified `vbmeta`, DTB inside the kernel field, a gzip kernel, and an illegal ARM64 image
header. A firmware-only image with `ramdisk_size = 0` is the one shape this ABL accepts.


## What this port establishes

Measured on the device and encoded in `board.toml` / `dts/` / `scripts/`:

- `fastboot boot` is **not implemented** by this ABL (`unknown command`), and the
  `recovery` partition is **never booted** (`adb reboot recovery` lands in fastboot,
  the BCB is not written). Only the `boot` partition is a usable target.
- The boot image must be laid out like the stock one: **uncompressed** kernel,
  device tree **after** the kernel with `dtb_size = 0`, `page 4096`, `hv 2`,
  `kernel_addr 0x8000`, `tags_addr 0x100`. A DTB stored inside the kernel field is
  not found by this loader and the device falls back to fastboot.
- Firmware window from `xbl.elf`: `UEFI FD = 0x9FC00000 / 0x300000`,
  `Display Reserved = 0x9C000000 / 0x02400000`, DRAM bank 2 = `0x100000000 + 6 GiB`.
- Touch is Goodix GT9886 on the GENI I2C at `0xc80000` (addr `0x5d`,
  IRQ GPIO 122 falling edge, reset GPIO 54).

## Build

Firmware and the boot image build on Linux:

```sh
python3 scripts/prepare-pd1936-uefi.py --uefi ../uefi-pd1936 --device . --tree ../UniXNU
python3 ../UniXNU/unixnu build firmware -D . --os 17 --uefi ../uefi-pd1936
python3 scripts/make-bootimg.py \
  --shim-source ../uefi-pd1936/BootShim/BootShim.S \
  --fd ../uefi-pd1936/Build/pd1936Pkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd \
  --dtb ../UniXNU/build/pd1936/pd1936.dtb \
  --out boot.img
```

The kernel and payload step needs a macOS host (Xcode); `.github/workflows/kernel.yml`
does that on a GitHub macOS runner.

## Flash

```sh
fastboot flash boot boot.img      # the only usable slot on this device
fastboot reboot
```

Rollback: flash the retail `boot.img` of the same firmware build.

## Layout

```
board.toml              board, IPSW, userdata geometry, kernel inputs, firmware
feeds.toml              pinned shared SoC source (zzweiqing/UniXNU-sm8150-common)
configs/pd1936_defconfig
dts/pd1936.dts          board device tree (+ vivo-pd1936.dtb, stock DTB + dtbo merged)
patches/xnu/series      device patch series (currently empty)
scripts/prepare-pd1936-uefi.py   generates the Mu-Silicium platform package
scripts/make-bootimg.py          assembles a boot image this loader accepts
scripts/package.sh               fixed packaging script (do not edit)
porting.toml            per-area record with evidence and pending work
proprietary-files.txt   firmware inputs from the stock ROM
```
