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
    target.write_text(text.replace(READ_OLD, READ_NEW, 1), encoding="utf-8")
    print("  sources: %s (%d headers)" % (source.name, headers_copied))
    print("  read() guarded; verification path excluded")

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

    archive = args.out / "libShengUfs.a"
    archive.unlink(missing_ok=True)
    subprocess.run(["xcrun", "ar", "rcs", str(archive)] + [str(o) for o in objects], check=True)
    print("  compiled %d object(s) -> %s (%d bytes)" % (len(objects), archive, archive.stat().st_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
