#!/usr/bin/env python3
"""Turn a Windows bitmap font (.FON) into the video card's font format.

  fon2bin.py FONT.FON OUT.bin

The card's text layer wants 256 characters of 16 bytes, one byte a row, the
leftmost pixel in bit 7 (hw/design.md, "Formats in video memory"): 4 KB.
A .FON is an NE executable holding FNT resources; this takes the first one,
which must be 8 pixels wide and 16 high and cover characters 0-255. FNT
keeps a glyph as byte columns, and an 8-pixel glyph is one column: its 16
bytes are already the rows, top first.

Used once to make vga8x16.bin from VileR's Bm437_IBM_VGA_8x16.FON (see
README.md here); kept so the font can be made again from the original.
"""

import struct
import sys

RT_FONT = 0x8008


def fnt_resource(data):
    if data[:2] != b"MZ":
        raise ValueError("not an MZ executable")
    ne = struct.unpack_from("<I", data, 0x3C)[0]
    if data[ne:ne + 2] != b"NE":
        raise ValueError("not an NE executable")
    table = ne + struct.unpack_from("<H", data, ne + 0x24)[0]
    shift = struct.unpack_from("<H", data, table)[0]
    at = table + 2
    while True:
        type_id, count = struct.unpack_from("<HH", data, at)
        if type_id == 0:
            raise ValueError("no FONT resource")
        at += 8
        for _ in range(count):
            offset, length = struct.unpack_from("<HH", data, at)
            if type_id == RT_FONT:
                return data[offset << shift:(offset << shift) + (length << shift)]
            at += 12


def glyphs(fnt):
    version = struct.unpack_from("<H", fnt, 0)[0]
    height = struct.unpack_from("<H", fnt, 0x58)[0]
    first, last = fnt[0x5F], fnt[0x60]
    if version not in (0x200, 0x300):
        raise ValueError(f"FNT version {version:#x}")
    if height != 16 or first != 0 or last != 255:
        raise ValueError(f"want 16 high, characters 0-255; got {height} high, {first}-{last}")
    table, entry = (0x76, 4) if version == 0x200 else (0x94, 6)
    out = bytearray()
    for ch in range(256):
        width = struct.unpack_from("<H", fnt, table + ch * entry)[0]
        offset = (struct.unpack_from("<H", fnt, table + ch * entry + 2)[0] if version == 0x200
                  else struct.unpack_from("<I", fnt, table + ch * entry + 2)[0])
        if width != 8:
            raise ValueError(f"character {ch} is {width} wide, not 8")
        out += fnt[offset:offset + 16]
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as f:
        font = glyphs(fnt_resource(f.read()))
    with open(sys.argv[2], "wb") as f:
        f.write(font)
    return 0


if __name__ == "__main__":
    sys.exit(main())
