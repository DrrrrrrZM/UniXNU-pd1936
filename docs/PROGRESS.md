# UniXNU on vivo iQOO Neo 855 (PD1936) — progress and experience

One document with everything learned so far: what works, what was measured, what was ruled
out, and how to continue. Written 2026-11-17 after the UFS probing session.

Companion documents: `docs/bring-up-log.md` (boot image rules and negative results),
`docs/ufs-bringup.md` (raw UFS register table), `HANDOFF.md` (resume block).

---

## 1. Where this stands in one paragraph

The boot chain works on hardware: a self-built 3.8 MB image flashed to `boot` reaches the
UniXNU boot manager, whose menu is drawn on the panel. The XNU kernel and the payload volume
build in CI. Everything after that is blocked by one hardware fact: **this ABL only accepts
a boot image with `ramdisk_size = 0`, and its kernel field only tolerates ~3–4 MiB**, so the
75 MiB payload cannot be delivered through the boot image at all. The remaining path is to
read the payload from a UFS partition, and the UFS controller turns out to be alive and
already running, left that way by the ABL.

---

## 2. Device facts (all measured, nothing taken from documentation)

| Item | Value | Where it came from |
|---|---|---|
Model / codename | vivo iQOO Neo 855 (V1936A) / `PD1936` | `getprop` |
SoC | Qualcomm **SM8150 v2**, not SM8150P | `ro.boot.dtb_idx=7` selects appended DTB #7 ("SM8150 v2 SoC") |
Boot state | `unlocked:yes`, `secure:yes`, `verifiedbootstate=orange` | `fastboot getvar all`, `ro.boot.*` |
Storage | Samsung `KLUEG8UHDB-C2D1`, UFS **3.x**; host `0x1D84000`, PHY `0x1D87000`, ICE `0x1D90000`, GIC SPI 265 | stock DTB + `/proc/iomem` |
Partitions | 74 entries; `boot`/`recovery`/`survival` 96 MiB each, `cache` 256 MiB, `system` 5.5 GiB, `userdata` 228 GiB | `getvar all` |
userdata | first_lba `2202632`, last_lba `62056442`, 59,853,811 sectors | `/proc/partitions` **and** `partition-size:userdata = 0x3914BF3000` |
RAM | 7.3 GiB usable; hole-free 6 GiB at `0x100000000` | `/proc/iomem` |
Firmware windows | FD `0x9FC00000 + 0x300000`; Display `0x9C000000 + 0x02400000`; Kernel `0xC0000000 + 0x07800000`; MLVM `0xA0000000 / 0xA8800000 / 0xB0400000` | 77-entry map inside `xbl.elf` |
Panel | OLED 1080x2340, single 62 Hz mode, `/sys/lcm/lcm_id = 0x22`, P3, HDR[2,3,4] | `dumpsys display`, `/sys/lcm/*` |
Touch | Goodix **GT9886**, GENI I2C `0xC80000`, addr `0x5D`, IRQ GPIO 122, reset GPIO 54 | `/sys/bus/i2c/devices/3-005d` |
Buttons | power = PON `KPDPWR_N`; volume down = PON `RESIN_N`; volume up = PM8150 SPMI-GPIO6; AI key = TLMM GPIO 26 | live FDT `gpio_keys` node, phandles resolved |
Console | `console=null androidboot.uart=uart2` — **no accessible UART** | `ro.boot.uart`, ABL cmdline in `logfs` |

---

## 3. What the bootloader actually allows

Four hard constraints, each verified on hardware:

1. **`fastboot boot` does not exist.** `downloading... OKAY` then
   `FAILED (remote: unknown command)`.
2. **The `recovery` partition is never booted.** `adb reboot recovery` lands in fastboot even
   when `recovery` holds an image that does boot from `boot`; `misc` stays
   `bootonce-bootloader` (19 non-zero bytes, verified twice byte-identical).
3. **`abl.elf` cannot be patched.** Payload is a single 4 MiB `PT_LOAD` at `0x9FA00000` with
   a hash/signature segment (`image_size 0x1990`, sig `0x100`, cert `0x1800`) covering every
   byte — no unverified hole.
4. **Only `boot` accepts a host-built image** (proven by the Magisk flash precedent).

Also: a failed attempt leaves `bootonce-bootloader` in `misc`, which forces the *next* boot
into fastboot. Always boot the retail image successfully once before testing another image.

---

## 4. Boot image: the one shape that works

Accepted:

```
page 4096 · header v2 · kernel_addr 0x8000 · tags_addr 0x100 · ramdisk_size 0
kernel: uncompressed ARM64 image, legal header (text_offset 0x80000, flags 0xA,
        magic "ARM\x64" at +0x38)
DTB:    after the kernel, page aligned, header dtb_size = 0
size:   boot partition is 0x6000000 (96 MiB), but the kernel field is effectively ~3-4 MiB
```

Rejected, each seen on the panel:

| Variant | Result |
|---|---|
`ramdisk_size > 0` — 4 KiB / 426 KiB Magisk CPIO / 1 MiB / 8 MiB / 75 MiB, with `ramdisk_addr` `0x1000000`, `0xB9800000`, `0` | warning page, then fastboot |
DTB inside the kernel field (Mu `append_dtb`) | fastboot |
DTB in a v2 `dtb_size` section | fastboot |
gzip kernel | fastboot |
kernel field 7.3 MiB (plain shim, no payload logic) | warning page, then fastboot |
kernel field 11.5 MiB, 81.8 MiB | no warning page at all (rejected before boot) |
modified `vbmeta` (flags `0` -> `2`) | endless reboot: the ABL checks the vbmeta RSA signature |

AVB is **not** a blocker: `logfs` records `avb_slot_verify.c:246: ERROR: boot: Hash of data
does not match digest in descriptor.` and the ABL boots anyway (`unlocked:yes`).

---

## 5. The experiments, condensed

| # | Image | kernel field | ramdisk | Outcome | What it proved |
|---|---|---|---|---|---|
1 | recovery slot probe (31 MiB raw) | 31 MiB | 0 | fastboot | recovery is not an entry point |
2 | probe (paint + PSCI reset) | 3.1 MiB | 0 | **panel turns white, self-reset** | BootShim runs, framebuffer is `0x9C000000`, PSCI works |
3 | `uefi-boot-v2` | 3.1 MiB | 0 | **menu** | FD relocation + PrePi + DXE + BDS all work |
4 | Mu-style (gzip kernel, DTB inside) | 1.2 MiB | 4 KiB | nothing | that layout is wrong |
5 | 1 MiB ramdisk, addr `0x1000000` | 3.8 MiB | 1 MiB | warning → fastboot | ramdisk not loaded |
6 | 1 MiB ramdisk, addr `0xB9800000` | 3.8 MiB | 1 MiB | warning → fastboot | not an address problem |
7 | 426 KiB Magisk CPIO, addr 0 | 3.8 MiB | 426 KiB | warning → fastboot | not a content problem |
8 | 75 MiB payload as ramdisk | 3.8 MiB | 75 MiB | warning → fastboot | not a size problem (single var confirmed) |
9 | payload embedded in kernel field | 81.8 MiB | 0 | **no warning page** | kernel field has a ceiling |
10 | same, payload copied to `0x100000000` | 11.5 MiB | 0 | warning → fastboot | not a clobber of the kernel |
11 | plain shim + 4 MiB zeros in kernel field | 7.3 MiB | 0 | warning → fastboot | ceiling is between 3 and 7.3 MiB |
12 | corrected ARM64 header (text_offset/flags) | 3.1 MiB | 0 | menu | header fix is safe, but not the ramdisk issue |
13 | key diagnostics (framebuffer squares) | 3.1 MiB | 0 | red square then menu | `sk_open` rc=1: arbiter version mismatch |
14 | v5 refit + APID map `0x0800` | 3.1 MiB | 0 | white square, `pon_apid=65` | arbiter found, PON handle located |
15 | observer at `0xC600000` | 3.1 MiB | 0 | **hang** | that window is `chnls`, not `obsrvr` |
16 | observer at `0xE600000` | 3.1 MiB | 0 | all 8 words zero + watchdog reset | channel mapping unknown, and unbounded polling starves boot |

---

## 6. UEFI firmware as it stands

- Platform package generated by `scripts/prepare-pd1936-uefi.py` into the Mu-Silicium tree:
  `.dec`, `.dsc`, `.fdf`, `Include/APRIORI.inc`, `Include/DXE.inc`, `MemoryMapLib`.
- FD is exactly `0x300000` bytes; FV header (`_FVH` at 0x28) declares `FvLength == file size`,
  PrePi code at `+0x1000`; ~662 KiB free inside the volume.
- Memory map: 32 contiguous regions, no overlap (overlap makes `ValidateMemoryMap()` halt).
  `Display Reserved = 0x9C000000 + 0x02400000`, `PIL` ends at `0x99E00000`, `DXE Heap`
  between them, `Payload 0xC0000000 + 0x04B00000` reserved for an embedded payload attempt.
- **Framebuffer console**: `SerialPortLib` -> `SiliciumPkg/Library/FrameBufferSerialPortLib`
  with `DebugLib` -> `BaseDebugLibSerialPort` and `PcdDebugPrintErrorLevel = 0x80000002`
  (ERROR|WARN). This is the single most valuable change made: without a UART, it is the only
  way to see inside the firmware. It printed the dram carve-outs, the drivers as they load,
  and every register value quoted in this document.
- DXE inventory already includes `DiskIoDxe`, `PartitionDxe`, `Fat`, `BdsDxe`,
  `UniXNURamDiskDxe`, `DarwinBoot`, `ConSplitterDxe`, `GraphicsConsoleDxe`, `UsbBusDxe`,
  `UsbKbDxe`, `ShengKeyInputDxe`.

---

## 7. Kernel build (CI)

`.github/workflows/kernel.yml` builds `kernel.macho` (51,945,472 B) and `payload.img`
(78,643,200 B) plus a boot image on a macOS runner. Seven fixes were needed, each worth
remembering:

1. `patches/xnu/series` must be **empty** — the parser treats every line as a patch path, and
   a UTF-8 BOM corrupts the first one.
2. Upstream `zzz-boot-progress.patch` does not apply to `xnu-10063.141.1` (its `bsd_init.c`
   hunks 4/6 and `kern_exec.c` hunk 4 target code that no longer exists; `bsd_init.c` is
   byte-identical across 141.1 / 121.3 / 101.15). The workflow strips that line from every
   profile series before building.
3. `CONFIG_XNU_NATIVE_ADRENO` and `CONFIG_XNU_NATIVE_LEGACY_DISPLAY` must be off, otherwise
   the link step asserts `$ADRENO_ARCHIVE` / `$LEGACY_DISPLAY_ARCHIVE`. The **defconfig beats
   the fragments**, so edit the defconfig, not only the fragment.
4. Pass `GITHUB_TOKEN`: `prepare-xnu-source.py` calls the GitHub API and 403s otherwise.
5. Put `/opt/homebrew/opt/llvm/bin` and `/opt/homebrew/opt/lld/bin` on `$GITHUB_PATH`
   (keg-only: `llvm-objcopy` was not resolvable).
6. macOS `sed -i` needs a suffix — filter with `grep -v` instead.
7. Pin UniXNU to a fetchable revision and keep submodule init tolerant.

---

## 8. Payload delivery: three dead ends and the live one

| Route | Verdict |
|---|---|
ramdisk in the boot image | dead: 6 combinations tested, `ramdisk_size` must be 0 |
kernel field | dead: 75 MiB payload vs a ~3–4 MiB field |
`recovery` partition | dead: the ABL never boots it |
**UFS partition read from UEFI** | **live, in progress** |

### UFS state (measured)

The controller was left **enabled and running** by the ABL:

```
CAP=0x1587031F  VER=0x00000300 (UFSHCI 3.0)  HCS=0x0000010F
CONTROLLER_ENABLE(0x34)=1        TRANSFER_REQ_LIST_RUN_STOP(0x60)=1
UIC error codes 0x38..0x48 = 0   TRANSFER_REQ_LIST_BASE_L(0x50)=0x9FFAE000
TASK_REQ_LIST_BASE_L(0x70)=0x9FFAF000
```

So no PHY init, no clock enable and no HCI reset is required — only a queued command.

`UTRL` is CPU-readable at `0x9FFAE000`; its slot 0 points at `0x05000000`, which is **not**
CPU-reachable (unmapped it takes a translation fault, `ESR 0x96000006`, `FAR 0x05000000`;
mapped as normal memory the read hangs).

SMMU is active: `IDR0=0x00250406` (SMMU-500), `GBPA=0x00005555`, `GFSR=0x00000039`
(faults recorded). Reading `0x15009800` (a guessed S2CR slot) hangs — the register window
ends at `0x15000FFF`.

---

## 9. Input: why the menu cannot be driven yet

All three real buttons are on SPMI, and this SoC's arbiter is **v5**:

```
core 0x0C440000: version=0x50000000, features=0x80200200 (512 APIDs)
APID map at core+0x0800: PPID 0x008 (PM8150 PON) = APID 65   <- found by scanning
sk_open() therefore succeeds (white diagnostic square)
but the observer window reads 0 both at 0x0C600000 (that is "chnls", hangs on read)
and at 0x0E600000 ("obsrvr"), and TLMM (0x03000000 + gpio*0x1000) reads 0 too
```

Facts that help whoever retries: the DT `reg-names` order is
`core / chnls / obsrvr / intr / cnfg` = `0xC440000 / 0xC600000 / 0xE600000 / 0xE700000 /
0xC40A000`; the v5 driver must accept `>= 0x50000000` and use the APID map at `+0x0800`;
`ShengKeyInputDxe` is from the SM8550 (v7) port and cannot be used unchanged.

Unbounded polling is dangerous: a failing read burns the CPU in MMIO timeouts and the
watchdog resets the boot after ~1 minute. Keep `MAX_FAILURES` small.

---

## 10. Reference ports: what is actually reusable

| Port | Approach | Reusable here? |
|---|---|---|
**nabu** (Pad 5, SM8150) | payload as boot ramdisk; UniXNU pin `245aca1a`; components pinned individually | ramdisk route **no**; its pin list and profile packaging **yes** |
**pclm10** (Reno Ace, SM8150) | `fastboot boot` + ramdisk | **no** (no `fastboot boot` here) |
**cepheus / andromeda** (Mi 9, SM8150) | touch is an **XNU-side patch** (`0082-cepheus-touch.patch`); payload planned from a `cust` partition, but its UEFI is "still being built" | touch driver **later**; `cust` loading **not implemented upstream** |
**alphaplus** (LG G8) | probe colour bars + runtime `*_reset=<step>` bisect, same `fastboot boot` limitation | diagnostics method **yes** |
**guacamole** (7 Pro) | external ios-sweet kernel + ADT | method only |

Nothing in these trees provides a Qualcomm UEFI block driver: `QcomPkg/Drivers` in Mu-Silicium
is a stub (two directories), and the only `BlockIo` producers are Samsung's `UfsDxe` and
`MshcDxe`.

---

## 11. Tooling built along the way

`pd1936/tools/` (~35 scripts): boot image packers (`make_stock_shaped_image.py`,
`build_bootimg_with_ramdisk.py`, `build_bootimg_aosp.py`, `build_bootimg_embed_payload.py`),
decoders (`decode_bootimg.py`, `parse_vbmeta.py`, `patch_vbmeta_flags.py`,
`inspect_fd.py`), device dumps (`scan_abl_dumps.py`, `dump_log_sequence.py`,
`partition_table.py`, `find_xnu_tag.py`), BootShim builders
(`build_bootshim_arm64hdr.sh`, `build_bootshim_payload.sh`), UEFI patch steps
(`patch_keyinput_sm8150.sh`, `patch_keydiag*.sh`, `patch_ufs*.sh`, `patch_fbdebug2.sh`), and
the CI helpers (`gh_dispatch.py`, `gh_tail.py`, `gh_before_error.py`,
`gh_grep_log.py`, `gh_put_file.py`, `gh_artifacts.py`).

---

## 12. Method: what made progress possible

1. **Get a console before chasing details.** Twenty speculative flashes produced less
   information than one framebuffer `DEBUG` line.
2. **Change one variable per flash, and reset the dirty state first** (retail boot, boot it
   once). Half of the earlier "conclusions" were contaminated by the previous failure.
3. **Prefer a value over a guess.** The arbiter generation came from reading
   `core[0] = 0x50000000`; the UTRD address came from `UTRL[0]`; the kernel-field ceiling
   came from a 7.3 MiB probe. Each read collapsed a whole branch of speculation.
4. **Believe the negative results.** `ramdisk_size > 0` was tested six ways; it is not a
   tuning problem.
5. **Announce before a risky read** (print the address, then read it) so a hang still leaves
   a pointer to the culprit — that is how the `chnls` window was identified.
6. **Record immediately.** `docs/ufs-bringup.md` exists because the numbers were written down
   while they were fresh.

---

## 13. Next actions, in order

1. **BlockIo over UFS** (see `HANDOFF.md` §9.2): allocate UTRL/UTRD in CPU-writable DRAM,
   queue one SCSI READ(10), read three ways — `OCS == 0` (write the driver), `OCS` error with
   `GFSR` growth (patch `S2CR` for stream `0x300` to bypass), or never completes (re-check the
   3.0 queue sequence).
2. Format `cache` as FAT16 with `\EFI\UniXNU\Payloads\{kernel.macho,*.adt,*.trustcache}` and
   let `DiskIo` + `Partition` + `Fat` (already in the FV) serve DarwinBoot unchanged.
3. Expect DarwinBoot to then load the kernel; its log lines (`DARWINBOOT: ARM64 va=... pa=...`,
   `inputs validated; exiting boot services`) are the next checkpoint.
4. Input stays frozen until the kernel runs; retry it only with the SMMU/SPMI register map of
   this specific SoC in hand.
5. Display (no 62 Hz panel preset exists in the firmware XMLs) and GPU (no Adreno component
   selected) are later work.
