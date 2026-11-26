#!/usr/bin/env python3
"""Build ShengUfs with block-device publication kept and the Verified Root path compiled out.

Upstream's build-arm-driver.py passes ``-DNATIVE_VERIFIED_BLOCK=1`` unconditionally for NativeUfs, so the
verification path is always compiled. Turning that macro off alone is not enough, because
``Operations::read()`` is compiled unconditionally while the symbols it uses (``timerTicks`` and
``native_ufs_read_ticks``) are defined inside the ``#if NATIVE_VERIFIED_BLOCK`` block:

    IOReturn read (uint64_t lba, uint32_t blocks, void *out) {
      uint64_t t0 = timerTicks ();                                   <- unconditional
      IOReturn r = hostRead4k (0, lba, blocks, out);
      __atomic_fetch_add (&native_ufs_read_ticks, timerTicks () - t0, ...);
      return r;
    }

so this script wraps that body and keeps a plain read in the ``#else`` branch.

The sources come from the fetched feed directory rather than from a single file, because NativeUfs.cpp
includes RootCommands.h / RootQueue.h / RootLayout.h / RootWindowRead.h / RootPublication.h in the middle
of the file (line ~914-1225), and ``hostRead4k`` is declared in RootCommands.h. ``tests/`` is skipped: its
own RootLayout.cpp defines a second ``static hostRead4k`` that would collide.

Disabled by build policy, not stubbed: nothing here returns a fake success for a verification call.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"

READ_OLD = """    IOReturn read (uint64_t lba, uint32_t blocks, void *out) {
      uint64_t t0 = timerTicks ();
      IOReturn r = hostRead4k (0, lba, blocks, out);
      __atomic_fetch_add (&native_ufs_read_ticks, timerTicks () - t0, __ATOMIC_RELAXED);
      return r;
    }"""

READ_NEW = """    IOReturn read (uint64_t lba, uint32_t blocks, void *out) {
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
      uint64_t t0 = timerTicks ();
      IOReturn r = hostRead4k (0, lba, blocks, out);
      __atomic_fetch_add (&native_ufs_read_ticks, timerTicks () - t0, __ATOMIC_RELAXED);
      return r;
#else
      return hostRead4k (0, lba, blocks, out);
#endif
    }"""

SMMU_PANIC_OLD = '''  if (failure) {
    gRootDiagnostic.stage = "SMMU readback";
    gRootDiagnostic.result = static_cast<uint32_t> (kIOReturnNotReady);
    rootDiagnosticFailure (failure);
    panic ("NATIVE_UFS smmu: %s idx=%u bank=%u cba2r=%x", failure, index, bank,
           gRootDiagnostic.cba2r);
  }'''

SMMU_PANIC_NEW = '''  if (failure) {
    // SM8150's UFS host is stream 0x300 (HANDOFF.md:144); the ported driver carried
    // SM8550's 0x60, so the SMMU never had an entry to reuse and the SMR write cannot be
    // installed from non-secure code either. Panic rather than continue: a faulting
    // transfer hangs the bus and the watchdog wipes the screen, while this prints whether
    // the SMMU is translating at all (GFSR) and what the firmware left in the first
    // stream-match groups.
    panic ("NATIVE_UFS g86 smmu: %s idx=%u bank=%u pages=%u cba2r=%x "
           "acr=%x scr0=%x gbpa=%x gfsr=%x "
           "smr=%x want_smr=%x smr0=%x smr1=%x smr2=%x smr3=%x "
           "s2cr=%x want_s2cr=%x reuse=%u",
           failure, index, bank, plan.pages, gRootDiagnostic.cba2r,
           read32 (smmu, 0x30), read32 (smmu, 0x0), read32 (smmu, 0x4), read32 (smmu, 0x48),
           read32 (smmu, 0x800 + 4 * index), 0x80000000U | stream,
           read32 (smmu, 0x800), read32 (smmu, 0x804), read32 (smmu, 0x808), read32 (smmu, 0x80c),
           read32 (smmu, 0xc00 + 4 * index), bank,
           plan.reuse ? 1U : 0U);
  }'''




SMMU_H_OLDBANK = "    bool ourBank = ((s2cr >> 16) & 3) == 0 && (s2cr & 255) == p.bank;"
SMMU_H_NEWBANK = ("    // SMMU-500 S2CR carries CB in bits 9:4 (6 bits: NUMCB can reach 128);\n"
                  "    // the driver used to write the bank into the low byte, which sets TYPE=3\n"
                  "    // (reserved) and CB=bank&3, and the SMMU then drops the SMR write.\n"
                  "    bool ourBank = ((s2cr >> 16) & 3) == 0 && ((s2cr >> 4) & 0x3f) == p.bank;")

SMMU_H_OLDWRITE = '''    write(0xc00 + 4 * p.slot, p.bank);
    if (read(0xc00 + 4 * p.slot) != p.bank) return "SMMU stream context readback";
    write(0x800 + 4 * p.slot, 0x80000000U | stream);
  }
  if (read(0x800 + 4 * p.slot) != (0x80000000U | stream) ||
      read(0xc00 + 4 * p.slot) != p.bank)'''
SMMU_H_NEWWRITE = '''    write(0xc00 + 4 * p.slot, p.bank);
    if (read(0xc00 + 4 * p.slot) != p.bank) return "SMMU stream context readback";
    write(0x800 + 4 * p.slot, 0x80000000U | stream);
    // New stream mappings only take effect after SMMU_SYNC; without it the SMR
    // readback can return the pre-write value.
    write(0x80, 1);
    for (unsigned spin = 0; (read(0x80) & 1) && spin < 100000; ++spin) { }
  }
  if (read(0x800 + 4 * p.slot) != (0x80000000U | stream) ||
      read(0xc00 + 4 * p.slot) != p.bank)'''

SMMU_H_OLDSLOT = "      if (!p.reuse) p.slot = i;"
SMMU_H_NEWSLOT = ("      // Keep the first free group. The loop used to overwrite p.slot on every free\n"
                  "      // entry, so it settled on the last one; on this SMMU the tail of the array\n"
                  "      // reads 0 and rejects writes (92/93), while groups 0 and 1 are live.\n"
                  "      if (!p.reuse && p.slot == 0xffffffffU) p.slot = i;")

SMMU_STREAM_OLD = "smmuBypassStream (0x60,"
SMMU_STREAM_NEW = "smmuBypassStream (0x300,"   # SM8150 UFS stream (HANDOFF.md:144)

# NativeRamBlock declares the board store hooks with __attribute__((weak_import)).
# That is a dyld mechanism; the kernel links statically, so ld64 keeps them as strong
# references and the link fails with
#     Undefined symbols: "_native_ufs_store_write_cached", referenced from: ...
# mode() returning 0 makes RamBlock skip the persistent overlay entirely
# (NativeRamBlock.cpp:219 if (mode && ...)), so defining the hooks to report
# "no store" keeps behaviour identical while satisfying the linker.
NATIVE_UFS_STORE_STUBS = '''

// --- PD1936: the one store hook ShengUfs does not define ---
// NativeUfs.cpp defines native_ufs_store_{mode,open,read,write,sync,gpt_check}, but not
// write_cached. NativeRamBlock declares it weak_import; a static kernel link keeps that
// reference strong, so the link fails with
//     Undefined symbols: "_native_ufs_store_write_cached", referenced from: ...
// mode() (storeMode) decides whether RamBlock touches the overlay at all
// (NativeRamBlock.cpp:219 if (mode && ...)), so this stub is only a link-time
// placeholder and is never called on a board without a store window.
extern "C" int native_ufs_store_write_cached (uint64_t, uint32_t, const void *) { return -1; }
'''


def fail(message: str) -> "None":
    raise SystemExit("build-pd1936-ufs-block: " + message)

def default_source() -> Path:
    """The fetched feed holds the whole source set; fall back to the tree copy."""
    feeds = ROOT.parents[3] / "feeds"
    candidates = sorted(feeds.glob("ShengUfs-*"))
    if candidates:
        return candidates[-1]
    return ROOT / "drivers/ShengUfs"


def base_command(record: dict) -> list[str]:
    """Strip the recorded input/output, warning flags and any verification macro definition."""
    command: list[str] = []
    arguments = record["arguments"]
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == record["file"]:
            index += 1
        elif value in ("-o", "-MF", "-MJ"):
            index += 2
        elif value in ("-MD", "-MP", "-Xlinker", "-Xclang"):
            index += 2 if value.startswith("-X") else 1
        elif value.startswith(("-DNATIVE_VERIFIED_BLOCK", "-W", "-mlinker-version=", "-Wl,", "-fuse-ld=")):
            index += 1
        else:
            command.append(value)
            index += 1
    return command


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, default=None)
    ap.add_argument("--out", type=Path, default=ROOT / "build/sheng-ufs")
    args = ap.parse_args()

    source = args.source or default_source()
    if not (source / "NativeUfs.cpp").is_file():
        fail(str(source) + " has no NativeUfs.cpp")
    if not RECIPE.is_file():
        fail(str(RECIPE) + " missing; run the kernel bootstrap pass once")

    record = json.loads(RECIPE.read_text().strip().rstrip(","))
    staged = args.out / "source"
    if staged.exists():
        shutil.rmtree(staged)
    staged.mkdir(parents=True)

    headers_copied = 0
    for item in source.iterdir():
        if item.name == "tests":
            continue
        if item.is_file():
            shutil.copy2(item, staged / item.name)
            if item.suffix == ".h":
                headers_copied += 1
        elif item.is_dir():
            shutil.copytree(item, staged / item.name)

    target = staged / "NativeUfs.cpp"
    text = target.read_text(encoding="utf-8")
    if READ_OLD not in text:
        fail("read() anchor not found; the pinned source changed")
    text = text.replace(READ_OLD, READ_NEW, 1)
    if SMMU_STREAM_OLD not in text:
        fail("smmuBypassStream (0x60, anchor not found")
    text = text.replace(SMMU_STREAM_OLD, SMMU_STREAM_NEW)
    # The driver panics with "SMMU stream mapping readback" when the SMR/S2CR readback
    # differs, but it only prints idx/bank/cba2r - not the two registers that actually
    # differed, although it has just read them into gRootDiagnostic. Print both against
    # the expected values so one boot identifies which field the hardware rejects.
    if SMMU_PANIC_OLD not in text:
        fail("SMMU readback panic anchor not found; the pinned source changed")
    text = text.replace(SMMU_PANIC_OLD, SMMU_PANIC_NEW, 1)
    text += NATIVE_UFS_STORE_STUBS
    target.write_text(text, encoding="utf-8")

    header = staged / "SmmuTakeover.h"
    if not header.is_file():
        fail("SmmuTakeover.h missing; the pinned source changed")
    htext = header.read_text(encoding="utf-8")
    if SMMU_H_OLDWRITE not in htext or SMMU_H_OLDBANK not in htext:
        fail("SmmuTakeover.h S2CR anchors not found; the pinned source changed")
    if SMMU_H_OLDSLOT not in htext:
        fail("SmmuTakeover.h slot anchor not found; the pinned source changed")
    htext = htext.replace(SMMU_H_OLDSLOT, SMMU_H_NEWSLOT, 1)
    htext = htext.replace(SMMU_H_OLDWRITE, SMMU_H_NEWWRITE, 1)
    header.write_text(htext, encoding="utf-8")
    print("  slot: first free SMR group instead of the last; SMR write followed by SMMU_SYNC")
    print("  sources: %s (%d headers)" % (source.name, headers_copied))
    print("  read() guarded; verification path excluded")
    print("  smmu readback panic now reports smr/s2cr against the wanted values")

    include = ["-I", str(staged),
               "-I", str(ROOT / "build/storage-abi/include"),
               "-I", str(ROOT / "drivers/VirtioMmioBlock"),
               "-I", str(ROOT / ".cache/xnu-work/osfmk")]
    defines = ["-DNATIVE_UFS_SOURCE_API=1",
               "-DNATIVE_VERIFIED_BLOCK=0",
               "-DPD1936_UFS_NO_VERIFY=1"]

    objects = []
    for src in sorted(staged.glob("*.cpp")):
        obj = args.out / (src.stem + ".o")
        command = base_command(record) + include + defines + ["-o", str(obj), str(src)]
        (args.out / (src.stem + ".compile.json")).write_text(json.dumps(command, indent=2) + "\n")
        result = subprocess.run(command, cwd=record["directory"], capture_output=True, text=True)
        output = (result.stdout or "") + (result.stderr or "")
        (args.out / (src.stem + ".log")).write_text(output)
        if result.returncode:
            errors = [ln for ln in output.splitlines() if "error:" in ln or "fatal error:" in ln]
            sys.stderr.write("\n".join(errors[:20]) + "\n")
            if not errors:
                sys.stderr.write(output[-2000:] + "\n")
            fail(src.name + " did not compile")
        objects.append(obj)

    # Fold the absolute bindings for IOBlockStorageDevice into this archive: it is the one guaranteed to
    # be on the link line, whereas a separate archive is not referenced by anything and stays unreferenced.
    bindings_dir = ROOT / "build/io-storage-family"
    bindings_obj = bindings_dir / "original-bindings.o"
    if not bindings_obj.is_file():
        subprocess.run([sys.executable, str(ROOT / "scripts/build-storage-bindings.py")],
                       cwd=ROOT, capture_output=True, text=True)
    if bindings_obj.is_file():
        objects.append(bindings_obj)
        print("  including storage bindings object")
    else:
        print("  warning: storage bindings object not available")

    archive = args.out / "libShengUfs.a"
    archive.unlink(missing_ok=True)
    subprocess.run(["xcrun", "ar", "rcs", str(archive)] + [str(o) for o in objects], check=True)
    print("  compiled %d object(s) -> %s (%d bytes)" % (len(objects), archive, archive.stat().st_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
