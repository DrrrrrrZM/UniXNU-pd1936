#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rebuild RamBlock with one extra property so APFS accepts md0 as a container.

Evidence chain:
  * AppleAPFSContainer::probe (0xfffffff00566AD64) begins with
        result = safeMetaCast(anObject, IOMedia);
        if (result->isFormatted() == 0 || sub_FFFFFFF00566B968(1) != 0) return nullptr;
    and `isFormatted()` reads the IOMedia Content property.
  * NativeRamBlock.cpp:424-433 sets only kIOPropertyDeviceCharacteristicsKey before
    registerService(); it never publishes Content, so isFormatted() is false and the
    APFS personality never matches md0 - the container is never created for it, which
    is what made container_rootmount answer 79.

Fix: publish `Content = "Apple_APFS"` right before registerService(). The key's value
is the literal string "Content" (kIOMediaContentKey), so no extra header is needed.

Output goes to build/ram-block/libRamBlock.a, the same path component:RamBlock writes,
so listing this script after that component in [kernel] archives replaces it.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"

OLD = """  online = true;
  native_ram_block_ready = 1;
  registerService();"""

NEW = """  // AppleAPFSContainer::probe gates on IOMedia::isFormatted(), which reads the
  // Content property; a medium without it never matches the APFS personality and no
  // container is created for it. Publishing Content here lets APFS treat this disk as
  // an APFS container. The key kIOMediaContentKey is the literal "Content".
  setProperty("Content", "Apple_APFS");
  online = true;
  native_ram_block_ready = 1;
  registerService();"""


def fail(message: str) -> "None":
    raise SystemExit("build-pd1936-ramblock: " + message)


def default_source() -> Path:
    feeds = ROOT.parents[3] / "feeds"
    candidates = sorted(feeds.glob("RamBlock-*"))
    if candidates:
        return candidates[-1]
    return ROOT / "drivers/RamBlock"


def base_command(record: dict) -> list[str]:
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
    ap.add_argument("--out", type=Path, default=ROOT / "build/ram-block")
    args = ap.parse_args()

    source = args.source or default_source()
    if not (source / "NativeRamBlock.cpp").is_file():
        fail(str(source) + " has no NativeRamBlock.cpp")
    if not RECIPE.is_file():
        fail(str(RECIPE) + " missing; run the kernel bootstrap pass once")

    record = json.loads(RECIPE.read_text().strip().rstrip(","))
    staged = args.out / "source"
    if staged.exists():
        shutil.rmtree(staged)
    staged.mkdir(parents=True)

    for item in source.iterdir():
        if item.name == "tests":
            continue
        if item.is_file():
            shutil.copy2(item, staged / item.name)
        elif item.is_dir():
            shutil.copytree(item, staged / item.name)

    target = staged / "NativeRamBlock.cpp"
    text = target.read_text(encoding="utf-8")
    if OLD not in text:
        fail("registerService() anchor not found; the pinned source changed")
    text = text.replace(OLD, NEW, 1)
    target.write_text(text, encoding="utf-8")
    print("  sources: %s" % source.name)
    print("  md0 now publishes Content = Apple_APFS before registerService()")

    include = ["-I", str(staged),
               "-I", str(ROOT / "build/storage-abi/include"),
               "-I", str(ROOT / ".cache/xnu-work/osfmk")]

    objects = []
    for src in sorted(staged.glob("*.cpp")):
        obj = args.out / (src.stem + ".o")
        command = base_command(record) + include + ["-o", str(obj), str(src)]
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

    archive = args.out / "libRamBlock.a"
    archive.unlink(missing_ok=True)
    subprocess.run(["xcrun", "ar", "rcs", str(archive)] + [str(o) for o in objects], check=True)
    print("  compiled %d object(s) -> %s (%d bytes)"
          % (len(objects), archive, archive.stat().st_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
