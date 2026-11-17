# Development framework — six pillars

The port is organised as gates with verifiable criteria, and every artefact is placed at the
layer where someone else can reuse it.

## 1. Layered contribution model

Reusable work moves up and out of the device repository; the device repository keeps only
what is genuinely board-specific.

| Layer | Contents | Destination |
|---|---|---|
L0 Qualcomm components | UFS BlockIo driver, SM8150 PHY/clock parameter set | `Silicon/Qualcomm` or a component repository |
L1 SoC shared | SMMU stream mapping, SPMI **v5** arbiter access, memory-map fragment | `UniXNU-sm8150-common` |
L2 device components | GT9886 touch, Adreno 640, ath11k skeleton | component repositories |
L3 device repository | `board.toml`, `dts/`, defconfig, payload layout | **this repository only** |

Rule of thumb: if a second SM8150 board would need the same code, it does not belong here.

## 2. Gates

| Gate | Goal | Verifiable criterion | State |
|---|---|---|---|
**G0** | boot chain | a 3.8 MB image shows the UniXNU menu on the panel | ✅ passed |
**G1** | payload delivery | the firmware reads `\\EFI\\UniXNU\\Payloads\\kernel.macho` from a partition | 🔴 main effort |
**G2** | DarwinBoot hand-off | `DARWINBOOT: ARM64 va=... pa=...` then `inputs validated; exiting boot services` | pending G1 |
**G3** | kernel to platform expert | kernel log lines past `PE_init_platform` | pending G2 |
**G4** | UFS root + launchd | `launchd` starts from the APFS/HFS+ volume | pending G3 |
**G5** | peripherals | panel, touch, GPU, WiFi | pending G4 |

No gate advances on "looks right"; each has an on-panel or on-device observation.

## 3. Triage

- **Main effort**: UFS read path, then SMMU if the transfer is translated.
- **Parallel (pure host work)**: Adreno 640, ath11k and GT9886 component skeletons — they can
  be written and reviewed without the device.
- **Deferred**: menu input (needs the SPMI v5 arbiter map), display panel presets (no 62 Hz
  preset exists in the firmware XMLs).
- **Fallback**: only if the SMMU turns out to be unfixable — a minimal fake-FAT shim inside
  DarwinBoot so it can consume a raw LBA range without BlockIo.

## 4. Evidence-driven iteration (SOP)

1. one variable per flash;
2. reset the dirty state first (retail boot, boot it once) — a failure leaves
   `bootonce-bootloader` which forces the next boot to fastboot;
3. prefer a read number over a guess;
4. announce a risky read before performing it, so a hang still names the culprit;
5. believe negative results (six ramdisk variants is not a tuning problem);
6. record immediately — `porting.toml` area entries carry `findings`, `evidence`, `pending`.

## 5. CI and release governance

- `firmware.yml` and `kernel.yml` are green; both pin UniXNU to a fetchable revision.
- Add: UFS driver build, payload packing, and a `porting-record` gate
  (`scripts/porting-record.py check`) so a record cannot drift from the tree.
- Version the artefacts (menu, firmware, payload) and keep the device seed out of them.

## 6. Reference and upstream strategy

- Configuration reference: **raphael** (K20 Pro). Firmware reference: **guacamole**.
- Copy the *method*, never the constants: three separate errors came from transplanting
  SM8550 assumptions (UFS PHY window, arbiter version, ramdisk behaviour).
- Feed results back: UFS BlockIo, SPMI v5 arbiter access and the SM8150 PHY parameter set are
  all candidates for upstream. One board forcing a shared component out is the healthy loop.

## Near-term actions

1. UTRL/UTRD + one SCSI READ(10), three-way verdict (`OCS == 0` / `GFSR` grows -> patch S2CR
   / hangs -> re-check the 3.0 sequencing).
2. Then the read-only BlockIo driver, placed in `UniXNU-sm8150-common`.
3. Format `cache` as FAT16 with the payload and let the existing DiskIo/Partition/Fat stack
   serve DarwinBoot; `DARWINBOOT:` lines are the next checkpoint.
4. In parallel, the Adreno 640 / ath11k / GT9886 skeleton components.
5. Keep `porting.toml [storage]` and `docs/ufs-bringup.md` current with every measurement.
