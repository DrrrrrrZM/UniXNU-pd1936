# PD1936 bring-up log — vivo iQOO Neo 855 (SM8150 v2)

Status at the time of writing: **the boot chain is proven on hardware up to the UniXNU
boot manager**. The kernel/userland stage is blocked by an upstream XNU patch-series
mismatch (see §7), not by anything on this board.

---

## 1. Device facts (all measured, no vendor documentation used)

| Item | Value | Source |
|---|---|---|
Model / codename | vivo iQOO Neo 855 (V1936A) / `PD1936` | `getprop` |
SoC | Qualcomm **SM8150 v2** (`msmnile`), not SM8150P | `ro.boot.dtb_idx=7` → appended DTB blob #7 "SM8150 v2 SoC" |
Boot chain security | `secureboot_enable=1`, `securebootkeyhash=2c0a52ff…`, `verifiedbootstate=orange`, vbmeta `unlocked` | `ro.boot.*` |
Storage | UFS host `0x1d84000` (`+0x2500`), ICE `0x1d90000` (`+0x8000`), PHY `0x1d87000` (`+0xda8`), Samsung `KLUEG8UHDB-C2D1` (UFS 3.x), GIC SPI 265 | stock DTB + `/proc/iomem` |
Partitions | GPT sector 4096; `sda16 system`, `sda17 vendor`, `sda19 userdata`; single slot, no A/B | `/proc/partitions` |
userdata | first_lba `2202632`, last_lba `62056442`, 59,853,811 sectors | `/proc/partitions` **and** `fastboot getvar partition-size:userdata` (`0x3914BF3000`), cross-checked |
RAM | 7.3 GiB usable; System RAM in 9 ranges; the hole-free 6 GiB window is `0x100000000–0x27FFFFFFF` | `/proc/iomem` |
Firmware windows | `UEFI FD 0x9FC00000 + 0x300000`; `Display Reserved 0x9C000000 + 0x02400000`; `Kernel 0xC0000000 + 0x07800000`; `MLVM_APSS/1/MLVM 0xA0000000 / 0xA8800000 / 0xB0400000` | 77-entry map embedded in `xbl.elf` |
Panel | OLED, `/sys/lcm/lcm_id = 0x22`, single mode 1080×2340 @ **62 Hz**, P3, HDR[2,3,4], 420 nit; brightness = DSI DBV (1023) + DC dimming (4095) | `dumpsys display`, `/sys/lcm/*` |
Touch | **Goodix GT9886 (V2)**, GENI I2C `0xc80000`, addr `0x5d`, IRQ GPIO 122 (edge falling), reset GPIO 54, matrix 1080×2340 | `/sys/bus/i2c/devices/3-005d`, stock `of_node` |
Audio | Tavil sound card; AK4377A (secondary MI2S), TAS2560 (primary MI2S), AW87319 PA, IA6xx voice | input nodes + vendor DAI names |
Console | `ro.boot.uart=uart2` → QUP UART `0xa90000` (not physically accessible) | `ro.boot.uart` |

## 2. Bootloader reality — four measured constraints

1. **`fastboot boot` does not exist** on this ABL: `downloading… OKAY`, `booting…`,
   `FAILED (remote: unknown command)`. The download path works; the command does not.
2. **The `recovery` partition is never booted.** `adb reboot recovery` lands in fastboot and
   `misc`'s BCB is unchanged (`bootonce-bootloader`, 19 non-zero bytes, verified twice with
   identical MD5). There is no recovery boot path to abuse.
3. **`abl.elf` cannot be patched.** ELF32, 3 program headers, payload = one 4 MiB
   `PT_LOAD` at `0x9FA00000`, a QcomPkg UEFI volume with 14 PE images; the hash/signature
   segment (`0x1000`, `0x1a38`, image_size `0x1990`, cert `0x1800`) covers it. Payload content
   runs to the last byte (`+0x3FFFFF`), so there is no unverified hole — unlike the MediaTek
   LK trick, which relied on one.
4. **Only the `boot` partition accepts a host-built image** (proven by a Magisk flash).

## 3. Boot image layout — the single most important finding

The loader accepts an image only when it matches the retail geometry:

```
page 4096 · header v2 · kernel_addr 0x8000 · tags_addr 0x100 · ramdisk 0
kernel = UNCOMPRESSED ARM64 image (BootShim carries the ARM\x64 header at +0x38)
DTB    = appended AFTER the kernel, page aligned, header dtb_size = 0
```

Three variants that fail, each verified on hardware:

| Variant | Result |
|---|---|
DTB inside the kernel field (Mu-Silicium `append_dtb = true`) | boots to fastboot, no execution |
DTB in a v2 `dtb_size`/`dtb_addr` section | boots to fastboot |
gzip-compressed kernel | boots to fastboot |

## 4. What runs today

```
ABL → boot partition → kernel field @0x8000 → BootShim (192 B)
    → copies gzip-free 3 MiB FD to 0x9FC00000 → dc cvac / ic iallu → br 0x9FC00000
    → FD offset 0 holds `b +0x1000` → PrePi/SEC
    → memory map: 31 contiguous regions, ValidateMemoryMap passes
    → DXE dispatch → BDS → UniXNU boot manager
    → panel shows "Project Silicium for vivo iQOO Neo 855 / Firmware Version 4.0"
    → boot menu is displayed
    → selecting an entry: "Failed to Find an Operating System" (no payload yet)
```

A paint probe (`BootShimProbe.S`, `PROBE_RESET=1`) turned the panel white and self-reset
after the delay, which is how the framebuffer base and PSCI were confirmed rather than
assumed.

## 5. Corrections made during bring-up (do not regress these)

| Wrong assumption | Measured reality |
|---|---|
The recovery slot is a safe experiment target | the ABL never boots it |
DTB belongs inside the kernel field | it must follow the kernel, `dtb_size = 0` |
kernel may be gzipped | it must be a raw image |
UFS host layout is "the same as SM8550" | same base slot only; PHY (`0x1d87000` vs `0x1d80000`), host length (`0x2500` vs `0x3000`), clock tables and stream differ |
UFS 2.1 (marketing spec) | UFS 3.x class (`KLUEG8UHDB`, HPB + write booster nodes) |
Synaptics TCM touch (from `/proc/config.gz`) | bound client is `gt9886_V2` (Goodix); the kernel image carries drivers for several modules |
vendor partition could remove bootloader limits | the "modded" `vendor.img` was **554,385,408 bytes of zeros**; vendor is Android user space and cannot gate the bootloader |
memory map from the reference port | rebuilt from `/proc/iomem`; PIL must end at `0x99E00000` or it swallows Display Reserved |

## 6. Artifacts

| Path | Size | Note |
|---|---|---|
`out/SILICIUM_UEFI.fd` | 3,145,728 B (= `0x300000`) | exactly the FD window from `xbl.elf` |
`out/uefi-boot-v2.img` | 3,833,856 B | the image that reaches the boot menu |
`out/probe-boot-dtb.img` | 3,838,528 B | paint + self-reset probe (proved execution) |
`out/boot-recovery.img`, `probe-*` | 0.6–31 MiB | recovery experiments; the slot is useless, kept for the record |
`dts/vivo-pd1936.dtb` | 676,013 B | retail DTB #7 + dtbo overlay #5 merged |
`tools/` | — | 20 read-only analysis and build tools (see each file header) |

Repository: <https://github.com/DrrrrrrZM/UniXNU-pd1936> (public, 18 files).

## 7. Current blocker (upstream, not this board)

`unixnu build kernel` stops at:

```
unixnu: XNU patch patches/xnu/zzz-boot-progress.patch: git failed (1):
error: patch failed: bsd/kern/bsd_init.c
error: bsd/kern/bsd_init.c: patch does not apply
```

`feeds.toml` pins `xnu-10063.141.1`; the patch series (37 hunks, 11 files) expects an
earlier `bsd_init()` prologue. The same failure occurs at UniXNU `95ad781` (the reference
port's tested commit), so it affects every device right now.

Work in progress: `tools/find_xnu_tag.py` downloads only the files a patch touches, per
candidate tag, and runs `patch --dry-run -p1` to identify the tag the series was written
against.

## 8. Next steps

1. Pin `xnu` to the tag the series matches (result of the tool above), or carry a
   device-level context fix.
2. Build `kernel.macho` + `payload.img` (macOS runner, `.github/workflows/kernel.yml`).
3. Flash the image with the payload ramdisk; expect DarwinBoot → kernel entry.
4. UFS read-only archive (`scripts/build-pd1936-ufs-readonly.py`) is already selected in
   `board.toml`; it must compile inside the kernel archive stage.
5. Then, and only then, storage-backed root, display (no 62 Hz panel preset exists in the
   firmware XMLs) and input (no button mapping for this board yet).

## 9. Restore Android

```sh
fastboot flash boot <retail boot.img of the same firmware build>
```
