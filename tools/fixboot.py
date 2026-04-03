#!/usr/bin/env python3
"""
fixboot.py - Verify and pad bootloader to exactly 512 bytes.
The bootloader already contains 0x55AA via the .word directive.
"""
import sys

def fix_boot(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    if len(data) < 512:
        while len(data) < 510:
            data.append(0)
        if len(data) == 510:
            data.append(0x55)
            data.append(0xAA)
    elif len(data) > 512:
        data = data[:512]

    # Verify signature
    if data[510] != 0x55 or data[511] != 0xAA:
        print(f"WARNING: Fixing boot signature")
        data[510] = 0x55
        data[511] = 0xAA

    with open(path, 'wb') as f:
        f.write(bytes(data))

    print(f"Bootloader: {len(data)} bytes, sig={hex(data[510])}{hex(data[511])}")

if __name__ == '__main__':
    fix_boot(sys.argv[1])
