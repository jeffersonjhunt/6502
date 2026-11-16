#!/usr/bin/env python3
"""ROM A's two chips, from the whole image (hw/design.md, "The flash: ROM A and ROM B").

ROM A is two 256 KB chips side by side: flash offset o is byte o >> 1 of the
chip o & 1 chooses, the even bytes in one and the odd in the other. A
programmer writes each chip on its own, so the build splits bios.rom into the
file for each: EVEN[i] = ROM[2i], ODD[i] = ROM[2i + 1]. Each is written to a
temporary name and renamed, so a failed run leaves no half a pair.

    python3 romsplit.py bios.rom bios-even.rom bios-odd.rom
"""

import os
import sys


def main(argv):
    if len(argv) != 4:
        sys.exit("usage: romsplit.py ROM EVEN ODD")
    with open(argv[1], "rb") as f:
        rom = f.read()
    if len(rom) % 2:
        sys.exit(f"romsplit.py: {argv[1]} is {len(rom)} bytes: two chips need an even size")
    for path, half in ((argv[2], rom[0::2]), (argv[3], rom[1::2])):
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(half)
        os.replace(tmp, path)


if __name__ == "__main__":
    main(sys.argv)
