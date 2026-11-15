# Agent notes — UniXNU-pd1936

Device repository for **vivo iQOO Neo 855 (PD1936 / V1936A)**, Qualcomm **SM8150 v2**,
single-slot A-only, `ro.boot.dtb_idx=7` (`SM8150 v2 SoC`, not the SM8150P variant).

## Repository boundaries

- **Here**: `board.toml`, `feeds.toml`, `configs/`, `dts/`, `patches/xnu/series`,
  `scripts/` (prepare + boot image assembly + `package.sh`), `porting.toml`,
  `proprietary-files.txt`.
- **UniXNU-sm8150-common**: shared SM8150 resources (`soc.toml`, `dts/sm8150.dtsi`,
  `configs/sm8150.config`, SoC patch series).
- **UniXNU/UniXNU**: build orchestration, components, OS profiles.
- **UniXNU/uefi-sheng**: the Mu-Silicium tree the platform package is injected into.

## Hardware invariants — do not "fix" these back to defaults

- **Only `boot` is bootable.** `fastboot boot` returns `unknown command`; the
  `recovery` partition is never entered (`adb reboot recovery` ends in fastboot and
  `misc`/BCB stays `bootonce-bootloader`).
- **Boot image geometry** (measured from the retail `boot.img`): `page 4096`,
  `header_version 2`, `kernel_addr 0x8000`, `tags_addr 0x100`, `ramdisk 0`.
- **Kernel must be uncompressed** and **the DTB must follow the kernel**, page
  aligned, with `dtb_size = 0`. Putting the DTB inside the kernel field (the Mu
  `append_dtb` convention) makes this loader fall back to fastboot.
- **FD window**: `0x9FC00000`, size `0x300000` (from `xbl.elf`). Same value the
  upstream SM8150 configs (cepheus, nabu) use.
- **Memory map** must be contiguous and non-overlapping; `ValidateMemoryMap()` in
  `MemoryInitPeiLib` halts on overlap. `Display Reserved = 0x9C000000 + 0x02400000`
  and `PIL` ends at `0x99E00000` (DXE Heap sits between them).
- **Framebuffer** really is `0x9C000000` — a BootShimProbe that paints it turns the
  panel white, so the value is confirmed, not assumed.
- **Touch**: Goodix GT9886, GENI I2C `0xc80000`, addr `0x5d`, IRQ GPIO 122
  (edge falling), reset GPIO 54. Matrix 1080x2340.
- **Panel**: OLED, `/sys/lcm/lcm_id = 0x22`, single mode 1080x2340 @ 62 Hz, P3,
  HDR[2,3,4], brightness = DSI DBV (1023) + DC dimming (4095). No matching panel
  preset exists in the firmware XMLs; do not claim display support without evidence.

## Verification

```sh
python3 ../UniXNU/unixnu status --json -D .
python3 ../UniXNU/scripts/porting-record.py check --device .
```

Record host results and device observations separately in `porting.toml`; a build
succeeding is not a boot result, and the panel turning white is not a working OS.
