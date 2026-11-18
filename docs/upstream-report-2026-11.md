# Upstream report — delivering a payload on vivo iQOO Neo 855 (PD1936, SM8150 v2)

Posting this as a summary of a full bring-up attempt, because every "obvious" payload route on
this device is blocked and the blockers are all measurable. Everything below was measured on the
device; the raw logs are referenced at the end.

## Device

| | |
|---|---|
SoC | Qualcomm **SM8150 v2** (`ro.boot.dtb_idx=7` selects the "SM8150 v2 SoC" DTB) |
Device | vivo iQOO Neo 855, `PD1936` / `V1936A`, Android 11, single-slot A-only |
Boot state | `unlocked:yes`, `verifiedbootstate=orange` |
Storage | Samsung `KLUEG8UHDB-C2D1`, UFS 3.x, controller `0x1D84000`, PHY `0x1D87000` |
Panel | OLED 1080x2340 @62 Hz |
Firmware | `abl.elf` is signed end to end; `boot` is the only bootable partition |

Boot chain that works today: `ABL -> boot -> BootShim -> UEFI FD (0x9FC00000, 0x300000) -> SEC/PrePi
-> DXE -> BDS -> UniXNU menu`, with a framebuffer `DEBUG` console (no UART is exposed:
`console=null androidboot.uart=uart2`).

## 1. The boot image cannot carry a payload

Measured limits on this ABL:

| Variant | Result |
|---|---|
`ramdisk_size > 0` — 4 KiB / 426 KiB CPIO / 1 MiB / 8 MiB / 75 MiB, `ramdisk_addr` 0 / `0x1000000` / `0xB9800000` | warning page, then fastboot (6 combinations) |
kernel field 3.15 MB (shim + 3 MiB FD) | boots |
kernel field 7.3 MB | warning page, then fastboot |
kernel field 11.5 MB, 81.8 MB | rejected before the warning page |
DTB inside the kernel field | fastboot |
gzip kernel | fastboot |
modified `vbmeta` (`flags 0 -> 2`) | endless reboot — the ABL checks the vbmeta signature |

Accepted geometry (from the retail image): `page 4096`, `header_version 2`,
`kernel_addr 0x8000`, `tags_addr 0x100`, uncompressed arm64 Image, DTB after the kernel with
`dtb_size = 0`.

**The DTB we ship is ignored.** With a 1 MiB padded DTB in the boot image the firmware still
receives a **679525-byte** tree at `0xC75FE000`, i.e. the ABL merges its own sources (`dtbo` +
appended blobs) and passes that. Padding the DTB to 4 MiB makes the device fall back to fastboot.

So the payload (a 75 MiB FAT volume holding a 49.5 MB `kernel.macho`) cannot travel through the
boot image at all, and gate "payload delivery" becomes "read a partition from firmware".

## 2. UFS from firmware: the controller is alive, its DMA is fenced

The controller is left enabled by the ABL (`CAP=0x1587031F`, `VER=0x00000300`, `HCS=0x0000010F`,
`HCE=1`, `RUN_STOP=1`, UIC error codes all 0), and it stays healthy under our own driver:

```
UIC_CMD_DME_GET completes on the first poll after a working W1C clear of IS
GCC 0x77010/14/18/1c/58/5c/90/ac/c0/8c000: all bit31 = 0 (not gated), core_clk = 0x4221
UFS_PHY_BCR = 0, GDSCR low byte 0 -> released and powered
```

The one step that fails is the descriptor fetch:

```
ring the door bell with the transfer list STOPPED -> harmless
start the list, ring again                        -> the device hangs (watchdog resets it)
```

Tried, all identical: buffers in the DXE pool; in `0x9FFA8000/9000/A000`; an inline UTRD inside
the ABL's own UTRL page `0x9FFAE000` using UFSHCI 3.0 semantics (`utrd = base + slot*32`,
offsets in 32-bit words, per QEMU's model); UTRL base moved to `0x9FFAF000` with a `0x5C`
list-clear; `S2CR[0x300]` forced to bypass.

Two facts that move the diagnosis away from SMMU:

- the live device tree shows `ufshc@1d84000` has **no `iommus` property** (full property list read
  from `/proc/device-tree`), so this master is not behind `apps-smmu@0x15000000`;
- `QCOM_SCM_MP_RESTORE_SEC_CFG` **is implemented** (`IS_CALL_AVAIL` returns `x1 = 1`) but rejects
  every device id 1..20 with `-2`.

## 3. Vendor binaries are not a shortcut

`Project-Aloha/UEFIFirmwareBackup/iqoo-pd1936` publishes this device's own signed driver set:
`xbl.elf`, the vendor's `APRIORI.inc` / `DXE.inc`, and 53 `QcomPkg/Drivers` binaries including
`UFSDxe.efi` (118 784 B). Note the vendor list contains **no `XhciDxe`** — only `UsbfnDwc3Dxe`
(device mode) — so ports that "read storage" are reusing `UFSDxe`, not a USB stick.

We recovered the **real DEPEX** of all 76 drivers from the FV inside `xbl.elf`
(`uefi_firmware` based parser; output committed as `vendor/iqoo-pd1936/vendor-depex.txt`), which
is more than the standalone `.efi` files carry. Even with correct DEPEX:

- `UFSDxe` alone loads and returns `ERROR: C90000002:V03000007 IO <guid>`;
- with 17 infrastructure drivers it binds nothing;
- ordered exactly as the vendor's APRIORI (`ScmDxeLA` first), `ScmDxeLA` **hangs** before it can
  even log.

These are XBL userspace binaries: they assume XBL's SMEM layout, shm-bridge pool and TZ channels.

## 4. USB host works; VBUS does not

Android moves adb/MTP traffic through dwc3 every day, so its DMA into HLOS memory is permitted.
Our driver confirms the host path:

```
GCTL[13:12] forced to 1 (host), read back capdir = 1
XHCI CAP = 0x01100020 -> caplen 0x20, HCIVERSION 0x0110 (XHCI 1.1)
PORTSC1: PP=1, PLS=5 (RxDetect)      <- host port actively scanning
XhciDxe + UsbBusDxe + UsbMassStorageDxe all bind
```

`CCS` stays 0 and disks stay dark because VBUS is absent. From the device tree, VBUS is on SPMI:
`pm8150_gpios 9` (`usb2_vbus_boost`) and the PM8150B SMB5 charger's OTG output.

SPMI from firmware (arbiter v5):

```
read : works  (APID map entry holds the PPID at bits[19:8]; obsrvr + 0x10000*ee + 0x80*apid;
               cmd = (opc<<27) | ((addr&0xFF)<<4) | byte_count)
write: dropped for every APID mapped to the GPIO9 page, for every write opcode
```

which is the `apid_data[apid].write_ee != our_ee` rule Linux enforces. So software VBUS is closed
by the same class of protection as UFS DMA.

## 5. SCM surface: 120 calls, no usable lever

`IS_CALL_AVAIL` sweep: **120 available**, including 18 under `SVC_MP` (`0x0C`) — `RESTORE_SEC_CFG`
0x02, `IOMMU_SECURE_PTBL_SIZE` 0x03, `MP_ASSIGN` 0x16, `CP_SMMU_APERTURE_ID` 0x1B, shm-bridge
0x1C/0x1D/0x1E — and `SVC_SMMU_PROGRAM` (`0x15`) 0x02/0x03.

Argument sweeps:

```
SMMU_PROGRAM 0x02: 12 vectors (arg counts 0..6, SIDs 0 / 0x300) -> no x0 = 0
MP_ASSIGN / CP_SMMU_APERTURE_ID / 0x0E / 0x0F / 0x23            -> rejected
IOMMU_SECURE_PTBL_SIZE                                          -> x0 = 0, but it is a sizing query
```

Software note for anyone doing the same: hand-written `smc #0` inside a driver that logs
afterwards asserts in DebugLib (`Format != ((void *) 0)`) because the TZ return path is not in the
asm clobber list. Use `MdePkg`'s `ArmSmcLib.ArmCallSmc()`.

## What we are asking

1. **Has anyone delivered a payload on a device whose ABL requires `ramdisk_size = 0` and whose
   kernel field is ~3 MiB?** Every SM8150 port we can find (cepheus, nabu, guacamole, alphaplus)
   either boots from a ramdisk or is a Windows-on-ARM target, so we would like to compare notes.
2. **Is a USB-mass-storage payload path supported in UniXNU?** If yes, on which Qualcomm devices,
   and how is VBUS handled there (PMIC GPIO / SMB5 OTG / external power)?
3. **Is there a known way to get `BlockIo` for Qualcomm UFS without the XBL runtime** — a minimal
   `UFSDxe`-compatible shim, or a documented SCM call that a non-secure caller may use?
4. **Known-good VBUS enable from UEFI on PM8150-class PMICs** (`usb2_vbus_boost` GPIO9 vs SMB5
   OTG), including whether the write is permitted from a non-secure EE at all on your device.

## Material we can share

```
docs/ufs-bringup.md            UFSHCI/UIC/SMMU/SCM measurements, section by section
docs/session-2026-11-17.md     the whole attempt as a dated record
docs/bring-up-log.md           boot image rules and every negative result
porting.toml                   structured findings/evidence/pending per area
tools/fv_depex_dump.py         recover real DEPEX from any vendor FV
vendor/iqoo-pd1936/vendor-depex.txt   76 drivers x their dependency expressions
```

Repository: `DrrrrrrZM/UniXNU-pd1936` (device) — the platform package is generated into a
Mu-Silicium tree by `scripts/prepare-pd1936-uefi.py`.
