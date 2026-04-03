#!/usr/bin/env python3
"""
mkimg.py - Create a 1.44MB floppy image for QEMU
Layout:
  Sector 0    : Bootloader (512 bytes)
  Sectors 1.. : Kernel binary
  Rest        : Zeros
"""
import sys
import os

SECTOR_SIZE   = 512
FLOPPY_SIZE   = 1474560   # 1.44 MB = 2880 * 512
FLOPPY_SECTORS = FLOPPY_SIZE // SECTOR_SIZE

def main():
    if len(sys.argv) < 4:
        print("Usage: mkimg.py <boot.bin> <kernel.bin> <output.img>")
        sys.exit(1)

    boot_path   = sys.argv[1]
    kernel_path = sys.argv[2]
    img_path    = sys.argv[3]

    with open(boot_path, 'rb') as f:
        boot = f.read()
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    # Validate bootloader
    if len(boot) != 512:
        print(f"ERROR: boot.bin must be 512 bytes (got {len(boot)})")
        sys.exit(1)
    if boot[510] != 0x55 or boot[511] != 0xAA:
        print(f"WARNING: Missing boot signature (0x55AA) in boot.bin")

    # Kernel must fit in remaining sectors
    max_kernel = (FLOPPY_SECTORS - 1) * SECTOR_SIZE
    if len(kernel) > max_kernel:
        print(f"ERROR: Kernel too large ({len(kernel)} > {max_kernel} bytes)")
        sys.exit(1)

    kernel_sectors = (len(kernel) + SECTOR_SIZE - 1) // SECTOR_SIZE
    print(f"Boot:   {len(boot)} bytes (sector 0)")
    print(f"Kernel: {len(kernel)} bytes ({kernel_sectors} sectors, starting sector 1)")

    # Build image
    img = bytearray(FLOPPY_SIZE)
    img[0:512] = boot
    img[512:512+len(kernel)] = kernel

    with open(img_path, 'wb') as f:
        f.write(img)

    print(f"Image:  {img_path} ({FLOPPY_SIZE} bytes = 1.44 MB)")
    print()
    print("Run with QEMU:")
    print(f"  qemu-system-i386 -fda {img_path} -boot a -m 4M")

if __name__ == '__main__':
    main()
