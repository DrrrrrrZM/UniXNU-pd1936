# UniXNU — vivo iQOO Neo 855 (PD1936 / V1936A)

Format 2 device repository for running XNU-based operating systems on the vivo
iQOO Neo 855 (SM8150 v2, 1080x2340, UFS 3.x, single-slot A-only).

**Status**: the boot chain is proven on hardware up to the UniXNU boot manager.

| Stage | State |
|---|---|
| `fastboot flash boot` of a self-built image | works |
| BootShim → UEFI FD relocation → SEC/PrePi | works (Silicium banner on the panel) |
| DXE dispatch, memory map validation | works (31 regions, no overlap) |
| UniXNU boot menu | **displayed on hardware** |
| Keyboard / button input in the menu | not yet (no input driver for this board) |
| `kernel.macho` + payload volume | not built yet |
| iOS / macOS userland | out of scope for now |

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
