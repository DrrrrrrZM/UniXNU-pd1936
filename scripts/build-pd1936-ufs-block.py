#!/usr/bin/env python3
"""Build the ShengUfs archive with block-device publication kept and Verified Root disabled.

Upstream's build-arm-driver.py appends ``-DNATIVE_VERIFIED_BLOCK=1`` unconditionally for the NativeUfs
component, which drags in the verification path and therefore the symbols
``_vr_open`` / ``_vr_verify_block`` / ``_vr_xnu_crypto`` / ``_verified_root_*`` /
``_boot_progress_poll``. That flag is not reachable from board.toml, so this script compiles the same
sources itself with the exact compile record XNU emitted, forcing the macro off.

What stays: NativeUfsBlock and its IOKit publication path, so UFS can still be published as a block
device. What goes: the authenticated-root code path, which is disabled by build policy rather than
faked with stubs.
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
def _default_source() -> Path:
    """Prefer the tree copy; fall back to the fetched feed, which survives without the component."""
    component_copy = ROOT / "build/sheng-ufs/source"
    if component_copy.is_dir():
        return component_copy
    tree_copy = ROOT / "drivers/ShengUfs"
    if tree_copy.is_dir():
        return tree_copy
    candidates = sorted((ROOT.parents[3] / "feeds").glob("ShengUfs-*"))
    return candidates[-1] if candidates else tree_copy


SOURCE = _default_source()


def fail(message: str) -> "None":
    raise SystemExit("build-pd1936-ufs-block: " + message)


def base_command(record: dict) -> list[str]:
    """Strip the recorded input, output and warning-related flags from the compile recipe."""
    command: list[str] = []
    arguments = record["arguments"]
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == record["file"]:
            index += 1
        elif value == "-o":
            index += 2
        elif value in ("-MF", "-MJ"):
            index += 2
        elif value in ("-MD", "-MP"):
            index += 1
        elif value.startswith(("-DNATIVE_VERIFIED_BLOCK", "-W", "-mlinker-version=", "-Wl,", "-fuse-ld=")):
            index += 1
        elif value in ("-Xlinker", "-Xclang"):
            index += 2
        else:
            command.append(value)
            index += 1
    return command


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, default=SOURCE)
    ap.add_argument("--out", type=Path, default=ROOT / "build/sheng-ufs-block")
    args = ap.parse_args()

    if not RECIPE.is_file():
        fail(str(RECIPE) + " missing; run the kernel bootstrap pass once")
    if not args.source.is_dir():
        fail(str(args.source) + " missing")

    record = json.loads(RECIPE.read_text().strip().rstrip(","))
    args.out.mkdir(parents=True, exist_ok=True)
    staged = args.out / "source"
    if staged.exists():
        shutil.rmtree(staged)
    shutil.copytree(args.source, staged)

    sources = sorted(staged.glob("*.cpp"))
    if not sources:
        fail("no .cpp sources under " + str(staged))

    include = ["-I", str(staged),
               "-I", str(ROOT / "build/sheng-ufs"),
               "-I", str(ROOT / "build/sheng-ufs/source"),
               "-I", str(ROOT / "build/storage-abi/include"),
               "-I", str(ROOT / "drivers/VirtioMmioBlock"),
               "-I", str(ROOT / ".cache/xnu-work/osfmk")]
    defines = ["-DNATIVE_UFS_SOURCE_API=1",
               "-DNATIVE_VERIFIED_BLOCK=0",          # build policy: no verified root path
               "-DPD1936_UFS_NO_VERIFY=1",
               "-Wno-error"]

    objects = []
    for src in sources:
        obj = args.out / (src.stem + ".o")
        command = base_command(record) + include + defines + ["-o", str(obj), str(src)]
        (args.out / (src.stem + ".compile.json")).write_text(json.dumps(command, indent=2) + "\n")
        result = subprocess.run(command, cwd=record["directory"], capture_output=True, text=True)
        (args.out / (src.stem + ".log")).write_text((result.stdout or "") + (result.stderr or ""))
        if result.returncode:
            text = (result.stdout or "") + (result.stderr or "")
            errors = [ln for ln in text.splitlines() if "error:" in ln or "fatal error:" in ln]
            sys.stderr.write("\n".join(errors[:20]) + "\n")
            if not errors:
                sys.stderr.write(text[-2000:] + "\n")
            fail(src.name + " did not compile")
        objects.append(obj)

    archive = ROOT / "build/sheng-ufs/libShengUfs.a"  # overwrite the component archive
    archive.unlink(missing_ok=True)
    subprocess.run(["xcrun", "ar", "rcs", str(archive)] + [str(o) for o in objects], check=True)
    print("  compiled " + str(len(objects)) + " object(s)")
    print("  archive " + str(archive) + " (" + str(archive.stat().st_size) + " bytes)")
    print("  NATIVE_VERIFIED_BLOCK=0: verification path excluded by build policy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
