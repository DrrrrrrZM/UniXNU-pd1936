#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Assemble the PD1936 boot image in the layout this bootloader actually accepts.

Hard-won facts (see porting.toml [boot]):
  * vivo's ABL does not boot the `recovery` partition at all.
  * the kernel must be an UNCOMPRESSED ARM64 image (BootShim carries the header).
  * the device tree must sit AFTER the kernel, page aligned, with header dtb_size 0
    (a DTB inside the kernel field, or a v2 dtb section, is not found -> "boot error").
  * stock geometry: page 4096, header v2, kernel_addr 0x8000, tags_addr 0x100.

No vendor file is needed: the geometry is hardcoded from measurements.
"""
import argparse
import os
import struct
import subprocess
import sys

PAGE = 4096
KERNEL_ADDR = 0x8000
TAGS_ADDR = 0x100
RAMDISK_ADDR = 0x0
BOOT_MAGIC = b"ANDROID!"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"
CMDLINE = b"console=null earlycon=null androidboot.hardware=qcom lpm_levels.sleep_disabled=1 " \
          b"androidboot.memcg=1 msm_rtb.filter=0x237 service_locator.enable=1 swiotlb=2048"
PART = 0x6000000


def align(x, a=PAGE):
    return (x + a - 1) // a * a


def build_shim(source, fd_base, fd_size, out):
    subprocess.run(["clang", "--target=aarch64-none-elf", "-c",
                    f"-DFD_BASE={fd_base}", f"-DFD_SIZE={fd_size}",
                    "-DREQUIRES_KERNEL_HEADER=1", source, "-o", out + ".o"], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", out + ".o", out], check=True)
    return open(out, "rb").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shim-source", help="BootShim.S")
    ap.add_argument("--shim", help="prebuilt BootShim.bin")
    ap.add_argument("--fd", required=True)
    ap.add_argument("--dtb", required=True)
    ap.add_argument("--ramdisk", default="none",
                    help="payload volume image (e.g. payload.img) or 'none'")
    ap.add_argument("--fd-base", default="0x9FC00000")
    ap.add_argument("--fd-size", default="0x300000")
    ap.add_argument("--name", default="UniXNU-PD1936")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    if a.shim:
        shim = open(a.shim, "rb").read()
    elif a.shim_source:
        shim = build_shim(a.shim_source, a.fd_base, a.fd_size, "/tmp/pd1936-bootshim.bin")
    else:
        sys.exit("need --shim or --shim-source")

    fd = open(a.fd, "rb").read()
    dtb = open(a.dtb, "rb").read()
    if not dtb.startswith(FDT_MAGIC):
        sys.exit("DTB does not start with the FDT magic")

    fd_size = int(a.fd_size, 0)
    if len(fd) != fd_size:
        print(f"  WARNING: FD is {len(fd)} B, expected 0x{fd_size:x}")

    kernel = shim + fd                      # uncompressed, BootShim has the header
    ramdisk = b""
    if a.ramdisk and a.ramdisk != "none":
        ramdisk = open(a.ramdisk, "rb").read()
    hdr = bytearray(PAGE)
    struct.pack_into("<8s", hdr, 0, BOOT_MAGIC)
    struct.pack_into("<10I", hdr, 8, len(kernel), KERNEL_ADDR, len(ramdisk),
                     0x1000000 if ramdisk else RAMDISK_ADDR,
                     0, 0, TAGS_ADDR, PAGE, 2, 0)
    struct.pack_into("<16s", hdr, 48, a.name.encode()[:16])
    struct.pack_into("<512s", hdr, 64, CMDLINE[:511])
    struct.pack_into("<I", hdr, 1608, 0)     # recovery_dtbo_size
    struct.pack_into("<Q", hdr, 1612, 0)     # recovery_dtbo_offset
    struct.pack_into("<I", hdr, 1620, PAGE)  # header_size
    struct.pack_into("<I", hdr, 1624, 0)     # dtb_size  <- 0, like the stock image
    struct.pack_into("<Q", hdr, 1628, 0)     # dtb_addr

    img = bytes(hdr) + kernel
    img += b"\x00" * (align(len(img)) - len(img))
    dtb_off = len(img)
    img += dtb
    img += b"\x00" * (align(len(img)) - len(img))
    rd_off = len(img)
    if ramdisk:
        img += ramdisk
        img += b"\x00" * (align(len(img)) - len(img))
    open(a.out, "wb").write(img)

    print(f"  shim    {len(shim)} B")
    print(f"  FD      {len(fd)} B (base {a.fd_base})")
    print(f"  kernel  {len(kernel)} B @0x{KERNEL_ADDR:x}")
    print(f"  DTB     {len(dtb)} B @0x{dtb_off:x} (after the kernel, dtb_size 0)")
    if ramdisk:
        print(f"  ramdisk {len(ramdisk)} B @0x{rd_off:x} (payload volume)")
    print(f"  image   {len(img)} B  fits {PART:#x}: {len(img) <= PART}")
    return 0 if len(img) <= PART else 1


if __name__ == "__main__":
    sys.exit(main())
