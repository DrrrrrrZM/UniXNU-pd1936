# UFS bring-up on PD1936 — measured facts

Goal: read the payload volume from storage in UEFI, because this board's ABL refuses a
`ramdisk` and caps the boot image kernel field at a few MiB.

Everything below was read from the hardware through the firmware's framebuffer console
(`SerialPortLib` = `SiliciumPkg/Library/FrameBufferSerialPortLib`), not from a spec.

## 1. The controller is alive and already running

Register offsets are from Linux `include/ufs/ufshci.h` (`REG_*` enum), version read from
the chip.

| Offset | Register | Value | Meaning |
|---|---|---|---|
`0x00` | CAP | `0x1587031F` | 32 transfer slots, 64-bit addressing |
`0x04` | MCQCAP | `0` | no MCQ |
`0x08` | UFS_VERSION | `0x00000300` | **UFSHCI 3.0** |
`0x10` | CONTROLLER_PID | `0x01000000` | |
`0x14` | CONTROLLER_MID | `0x00010217` | |
`0x20` | INTERRUPT_STATUS | `0x00000400` | |
`0x24` | INTERRUPT_ENABLE | `0` | |
`0x30` | CONTROLLER_STATUS | `0x0000010F` | device present |
`0x34` | **CONTROLLER_ENABLE** | **`1`** | **ABL left the controller enabled** |
`0x38–0x48` | UIC error codes | all `0` | link clean: PHY and clocks already up |
`0x50` | TRANSFER_REQ_LIST_BASE_L | `0x9FFAE000` | UTRL, CPU-readable |
`0x54` | TRANSFER_REQ_LIST_BASE_H | `0` | below 4 GiB |
`0x58` | TRANSFER_REQ_DOOR_BELL | `0` | |
`0x5C` | TRANSFER_REQ_LIST_CLEAR | `0` | |
`0x60` | **TRANSFER_REQ_LIST_RUN_STOP** | **`1`** | **the transfer list is running** |
`0x70` | TASK_REQ_LIST_BASE_L | `0x9FFAF000` | UTMR list |

Consequence: no PHY init, no clock enable, no HCI reset is needed. Only a command has to be
queued.

## 2. UTRL is readable; the UTRD area that UTRL[0] points at is not

```
Utrl 00 = 05000000   Utmrl 00 = 76821018
Utrl 04 = 00000000   Utmrl 04 = 00000002
Utrl 08 = 00000000   Utmrl 08 = 00000000
Utrl 0c = 00000000   Utmrl 0c = 0003FFFF
```

`UTRL[0]` holds `0x05000000` (UTRD base, upper word 0). That address is **not accessible to
the CPU**:

- unmapped: `Synchronous Exception`, `ESR 0x96000006`, `FAR 0x05000000`
  → *Data abort: Translation fault, second level*
- after adding it to the board memory map as normal memory: the read **hangs** instead
  (no target answers on the interconnect)

So `0x05000000` is either not DRAM or reachable only by DMA. Writing a UTRD there from the
CPU is not an option; a UTRD placed in CPU-writable DRAM is the alternative, subject to the
SMMU.

## 3. SMMU state (probed, then abandoned: reads past 0x15000FFF hang)

```
Smmu IDR0 = 00250406    ARM SMMU-500
Smmu IDR1 = 00000000
Smmu GBPA = 00005555
Smmu GFSR = 00000039    faults have been recorded
```

Reading `0x15009800` (S2CR for stream 0x300) **hangs** — the register window ends at
`0x15000FFF`, so the S2CR array is not where the probe looked. Do not probe further
blindly; either consult the SMMU-500 map or attempt a transfer and read `GFSR`/OCS.

## 4. Other facts collected on the way

- `rawdump`, `logdump`, `apanic` are all zero; `logfs` is the only ABL log with content.
- `vbmeta` must not be modified: changing `flags` invalidates its RSA signature and the
  board then reboots forever. AVB hash mismatches are non-fatal here (`unlocked:yes`).
- `recovery` is never booted by this ABL, even with an image that boots from `boot`.
- Boot image: `ramdisk_size` must be 0; kernel uncompressed with a legal ARM64 header
  (`text_offset 0x80000`, `flags 0xA`); DTB after the kernel with `dtb_size 0`.
- The boot image kernel field tolerates ~3–4 MiB: 3.0 MiB boots, 7.3 MiB falls back to
  fastboot, 11.5 and 81.8 MiB are rejected outright. The payload (75 MiB) can never ride
  there, which is why storage access is required.
- UEFI input is unsolved on this board: the three buttons are on SPMI (PON + PMIC GPIO) and
  the arbiter is **v5** (core `0x50000000`, features `0x80200200` = 512 APIDs, APID map at
  `core+0x0800`, PON PPID `0x008` = APID 65). `sk_open` succeeds, but observer-channel
  reads return 0 and TLMM (`0x03000000 + gpio*0x1000`) also reads 0.
- The framebuffer console is the single most useful bring-up tool on this board: no UART is
  reachable (`console=null androidboot.uart=uart2`).

## 5. Next step

Queue one SCSI READ(10) with our own UTRL/UTRD in CPU-writable DRAM:

1. allocate UTRL (512 B) + UTRD (4 KiB) + a data buffer, 4 KiB aligned;
2. write `UTRL[0] = {utrd_pa_low, utrd_pa_high}`, program `0x50/0x54`, set `0x60 = 1`;
3. fill the UTRD with an `UTP_UPIU_TRANSACTION_SPECIFIC` UPIU carrying a READ(10) CDB;
4. ring `0x58` (door bell) and poll the UTRD's `OCS` field;
5. if DMA is translated by the SMMU and the buffer is not mapped, OCS reports an error and
   `GFSR` grows — in that case patch `S2CR` for stream `0x300` to bypass (window
   `0x15000000`, S2CR array base still to be determined from the SMMU-500 map).

## 6. G1 verdict — the controller's DMA is not usable after hand-off

Six builds narrowed this down; the last one separates the two possible causes cleanly.

### The discriminator that settled it

Ring the door bell with the transfer list **stopped** (`0x60 = 0`), then start it and ring
again. A stopped list fetches nothing, so a hang there would be a register problem.

```
UfsRead: A stop list, ring bell while stopped     -> printed
UfsRead: B bell while stopped survived            -> printed   (register writes are fine)
UfsRead: C start list and ring again              -> HANG
```

Conclusion: writing `0x58` is harmless; the hang happens when the controller actually
**DMA-fetches the UTRD**.

### What was tried before that

| Attempt | Buffer location | Result |
|---|---|---|
1 | DXE pool (`0x9ADD7xxx`) | hang at the bell |
2 | `0x9FFA8000/0x9FFA9000/0x9FFAA000` window | hang at the bell |
3 | everything inside ABL's UTRL page `0x9FFAE000` | hang at the bell |
4 | UTRD made inline per UFSHCI 3.0 (QEMU semantics: `utrd = base + slot*32`, offsets in 32-bit words) | hang at the bell |
5 | SMMU: `S2CR[0x300]` forced to BYPASS (`0x00000001` -> `0x00010001`) | hang at the bell |
6 | UTRL base moved to ABL's task-list page `0x9FFAF000`, plus `0x5C` list-clear | hang at the bell |

SMMU probe values (window readable at `0x15000000`, `S2CR[n] = 0xC00 + n*4`):

```
S2CR[0]   = 00000000     type 0 (fault), cbndx 0
@0x1000   = 000100FF     type 1 (bypass), cbndx 0xFF   <- shows the encoding
S2CR[300] = 00000001     type 0 (fault), cbndx 1
@0x1C00   = 00000000
```

Forcing `S2CR[0x300]` to bypass changed nothing, so either `0x300` is not this controller's
stream or the block is not behind that SMMU at all.

### Reading of the evidence

- CPU access to the controller registers is complete: reads work, writes take effect
  (`0x50`/`0x54`/`0x60` read back our values), and ringing a stopped list is safe.
- No buffer address tried is DMA-reachable: three different regions, page-aligned and
  unaligned, inline UTRD, plus an SMMU bypass write.
- The controller was left `HCE=1`, `RUN_STOP=1` by the ABL, i.e. it had DMA working
  *before* hand-off.

The consistent explanation is that the ABL (or the secure world it hands over to) closed the
controller's DMA path — a common Qualcomm arrangement where storage stays owned by
XBL/TZ and HLOS reaches it through SCM calls. That is not something a UEFI driver can undo.

### What would unblock G1

1. An SCM/TZ interface to the storage (not obtainable from the ABL image we have).
2. Evidence from another `ramdisk_size = 0` board that firmware-side UFS DMA works after
   hand-off — no such port exists yet in the UniXNU device set.
3. An ABL variant that leaves the DMA path open (vivo-internal).

Until one of those exists, G1 stays blocked on the firmware side and the payload question
needs a different answer:
- shrink the payload below the ~3-4 MiB kernel-field limit (not reachable: `kernel.macho`
  is 49.5 MB), or
- keep the whole thing inside a boot ramdisk (this ABL refuses any `ramdisk_size > 0`).

## 7. Secure world: implemented, but closed to us

`docs/ufs-bringup.md` section 6 left one question: is the DMA refusal something the secure
world can be asked to lift? The SCM interface exists for exactly that, so it was called
directly from the firmware with `smc #0`.

Numbers (Linux `drivers/firmware/qcom/qcom_scm.h`, `qcom_scm-smc.c`):

```
SCM_SMC_FNID(s, c) = (s << 8) | c
x0                  = 0x80000000 | (OWNER_SIP << 24) | fnid      (FAST_CALL, SMC32)
QCOM_SCM_SVC_INFO = 0x06 , QCOM_SCM_INFO_IS_CALL_AVAIL = 0x01
QCOM_SCM_SVC_MP   = 0x0C , QCOM_SCM_MP_RESTORE_SEC_CFG  = 0x02
QCOM_SCM_UFS_DEV_ID = 19 , QCOM_SCM_ICE_DEV_ID = 20
```

What the device answered:

| Probe | Result |
|---|---|
`IS_CALL_AVAIL(MP, RESTORE_SEC_CFG)` | `x0 = 0` (call delivered), **`x1 = 1` -> the TZ implements it** |
`RESTORE_SEC_CFG(dev=19, spare=0)` | `x0 = 0xFFFFFFFFFFFFFFFE` (-2), `x1 = -4`, `S2CR[0x300]` unchanged |
`RESTORE_SEC_CFG(dev=19, spare=1)` | same |
`RESTORE_SEC_CFG(dev=20, spare=0)` | same |
`RESTORE_SEC_CFG(dev=19, spare=0x10)` | same |
`RESTORE_SEC_CFG(dev=1 .. 20, spare=0)` | **all 20 rejected with -2**, `S2CR[0x300]` never changed |

### Verdict

- The interface is **present** in this device's TZ (`x1 = 1`), so the earlier guess "maybe the
  TZ does not have it" is wrong.
- Every non-secure invocation is **refused**, including for device ids that have nothing to do
  with storage (1 = MDSS, 18 = GFX). The refusal is not about the UFS id: it is about **who is
  asking**. The call is reserved for the secure world itself (i.e. the ABL / TZ boot flow).
- This matches the SMMU observation: the UFS master's stream configuration lives in a bank a
  non-secure write cannot change (`arm-smmu-qcom.c` documents firmware that rewrites S2CR
  FAULT/BYPASS writes).

Consequence, stated plainly: **a plain MMIO READ(10) needs no secure world, but making the
DMA legal does, and the secure world refuses us.** Firmware-side UFS is therefore closed on
this device, and with it gate G1 in its current form:

| Channel for the payload | State |
|---|---|
kernel field | <= ~3 MiB, already occupied by the 3 MiB firmware volume |
boot ramdisk | this ABL requires `ramdisk_size = 0` |
DTB field | ABL ignores it and passes its own 679525-byte tree; padding to 4 MiB even fails to boot |
recovery partition | never booted by this ABL |
UFS read from firmware | DMA refused by the secure world (this section) |
USB host (mass storage) | possible in principle, but the phone's port is device-only and no XHCI host driver exists yet |

What would still work, none of it available today:

1. an ABL that leaves the DMA path open, or an SCM call the TZ accepts for a non-secure
   caller;
2. a payload small enough for the ~3 MiB kernel field (the kernel is 49.5 MB);
3. a firmware-side USB host stack driving a mass-storage device or a second phone.

## 8. Final isolation: the controller works, its memory access does not

Each row below is a measurement from the device, not an inference.

| Question | Measurement | Verdict |
|---|---|---|
Is the controller alive at all? | `UIC_CMD_DME_GET` with an invalid attribute: IS completion bit cleared with W1C (`00000400 -> 00000000`), then the command **completed on the first poll** (`polls=1`, IS back to `00000400`) | **yes, the command engine works** |
Are the UFS clocks enabled? | GCC `0x77010/0x77014/0x77018/0x7701c/0x77058/0x7705c/0x77090/0x770ac/0x770c0/0x8c000` all report bit31 = 0 (not gated), `core_clk` = `0x00004221` | **all on** |
Is the controller in reset or collapsed? | `0x77000` (UFS_PHY_BCR) = `0`, GDSCR `0x77004` low byte 0 (no SW collapse) | **released, powered** |
Does the UFS master go through the apps SMMU? | live device tree `ufshc@1d84000` has **no `iommus` property** (full property list read from `/proc/device-tree`) | **no** — the earlier `S2CR[0x300]` work was aimed at the wrong block |
Can the secure world be asked to lift a restriction? | `IS_CALL_AVAIL(MP, RESTORE_SEC_CFG)` -> `x1 = 1` (implemented), but `RESTORE_SEC_CFG` returns `0xFFFFFFFFFFFFFFFE` for **every** device id 1..20 | **refused for a non-secure caller** |
Can the controller fetch a transfer descriptor? | ringing the door bell while the list is stopped is harmless; starting the list and ringing it **hangs the device** | **no — this is the only broken step** |

Sequence of the last few probes, for the record:

```
IS 00000400 -> after W1C 00000000 (cleared=yes)          <- completion bit is W1C and works
Uic2: polls=1 IS=00000400 arg1=0000DEAD arg2=0 arg3=0    <- UIC command completed
Gcc 077010 core_clk = 00004221 [on]                      <- clocks are not the problem
Gcc 077000 UFS_PHY_BCR = 00000000                        <- not held in reset
```

### Conclusion

The controller accepts commands and its registers behave, but the moment it has to read
memory on its own it stops the bus. Combined with:

- no IOMMU binding for this master (so no SMMU mapping to fix),
- all clocks and power present,
- the secure world's restore-sec-cfg interface refusing every device id,

the remaining explanation is that the UFS master's access to HLOS memory is fenced by
platform memory protection (Qualcomm XPU / DDR protection configured by the secure world).
That is not something a UEFI driver can undo, and it is consistent with "Linux works": Linux
runs with the votes and the protection state the secure world set up for it after the
hand-off that we never perform.

**Gate G1 is therefore blocked by platform memory protection, not by missing driver code.**
Any further progress on payload delivery would need one of: an ABL that performs the normal
Linux-style hand-off for our image, a secure-world call that succeeds, or a completely
different transport (USB host), which is likely fenced the same way and has not been tested.
