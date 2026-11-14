#!/usr/bin/env python3
"""The flash's sums for the ROM's diagnostics (hw/diag.md, plan 2, stage 1).

Writes, into bios.rom after ld65, the 16-bit sum of the bytes of each flash
block 21-31, block 23's without its last page (which holds this table and its
vectors): the low bytes at diag23.s's SUMLO + (block - 21), the high bytes at
its SUMHI + (block - 21), both found in ld65's label file (block 23 is seen at
$8000). Stage 1 sums each block through W1 and compares, so a build never ships
a table that is wrong. The build links to a temporary file and renames it only
after this succeeds.

    python3 romsum.py bios.rom bios.rom.lbl
"""

import sys

BLOCK = 0x4000
FIRST, LAST = 21, 31
DIAG23 = 23 * BLOCK


def label(lbl, name):
    with open(lbl) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3 and parts[2] == "." + name:
                return int(parts[1], 16)
    sys.exit(f"romsum.py: no {name} in {lbl}")


def main(path, lbl):
    lo = DIAG23 + label(lbl, "SUMLO") - 0x8000
    hi = DIAG23 + label(lbl, "SUMHI") - 0x8000
    if not (DIAG23 + 0x3F00 <= lo < DIAG23 + BLOCK and DIAG23 + 0x3F00 <= hi < DIAG23 + BLOCK):
        sys.exit("romsum.py: SUMLO and SUMHI must be in block 23's last page, which isn't summed")
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) != 32 * BLOCK:
        sys.exit(f"romsum.py: {path} is {len(rom)} bytes, not 512 KB")
    for b in range(FIRST, LAST + 1):
        data = rom[b * BLOCK:(b + 1) * BLOCK]
        if b == 23:
            data = data[:0x3F00]
        s = sum(data) & 0xFFFF
        rom[lo + b - FIRST] = s & 0xFF
        rom[hi + b - FIRST] = s >> 8
    with open(path, "wb") as f:
        f.write(rom)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: romsum.py bios.rom bios.rom.lbl")
    main(sys.argv[1], sys.argv[2])
