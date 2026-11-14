#!/usr/bin/env python3
"""The flash's sums for the ROM's diagnostics (hw/diag.md, plan 2, stage 1).

Writes, into bios.rom after ld65, the 16-bit sum of the bytes of each flash
block 21-31, block 23's without its last page (which holds this table and its
vectors): the low bytes at block 23's $BF00 + (block - 21), the high bytes at
$BF10 + (block - 21), where diag23.s's SUMLO and SUMHI are. Stage 1 sums each
block through W1 and compares, so a build never ships a table that is wrong.

    python3 romsum.py bios.rom
"""

import sys

BLOCK = 0x4000
FIRST, LAST = 21, 31
TABLE = 23 * BLOCK + 0x3F00


def main(path):
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) != 32 * BLOCK:
        sys.exit(f"romsum.py: {path} is {len(rom)} bytes, not 512 KB")
    for b in range(FIRST, LAST + 1):
        data = rom[b * BLOCK:(b + 1) * BLOCK]
        if b == 23:
            data = data[:0x3F00]
        s = sum(data) & 0xFFFF
        rom[TABLE + b - FIRST] = s & 0xFF
        rom[TABLE + 0x10 + b - FIRST] = s >> 8
    with open(path, "wb") as f:
        f.write(rom)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: romsum.py bios.rom")
    main(sys.argv[1])
