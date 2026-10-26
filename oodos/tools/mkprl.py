"""Makes an OODOS .PRL from two links, and loads one as OODOS will (hw/oodos.md, "RSXs and programs").

    python3 -I mkprl.py prog.a prog.b -o PROG.PRL
    python3 -I mkprl.py --load PROG.PRL --page 0x80 --zp 0xF4 -o prog.img

prog.a and prog.b are the same objects linked with cfg/prl.cfg twice: b a page above a, and with its
zero page segment a few bytes above a's (not 0 or 1 apart, or the two bitmaps couldn't be told apart).
Every byte of the images must be the same, one more (mod 256: the high byte of an address in the
image) or the zero page offset more (a zero page address in the image); anything else is refused, with
where. The .PRL is a's header and image, then the page bitmap, then the zero page bitmap: a bit for
each byte of the image, the first byte in bit 7 of the first bitmap byte.

--load relocates a .PRL to the page and zero page base given, by the same steps as OODOS's loader, and
writes the image as it would be in memory there; given a .COM, it checks the page is the link page.
A file without the header's magic number is refused, by both.
"""

import argparse
import sys

MAGIC = b"OOD\x1a"
HEADER = 16
F_RELOC, F_RSX, F_NOT816 = 0x01, 0x02, 0x80
TOP = 0xC000  # the TPA's limit; the loader uses $0204's value


class Refused(Exception):
    pass


def header(data, name="the file"):
    """The header's fields, or Refused."""
    if len(data) < HEADER or data[:4] != MAGIC:
        raise Refused("%s: no OODOS header (the magic number is missing): not an OODOS program" % name)
    h = {
        "flags": data[4], "page": data[5],
        "size": int.from_bytes(data[6:8], "little"), "bss": int.from_bytes(data[8:10], "little"),
        "zpbase": data[10], "zpsize": data[11],
        "rsxcount": data[12], "reserved": data[13], "rsxrec": int.from_bytes(data[14:16], "little"),
    }
    if len(data) < HEADER + h["size"]:
        raise Refused("%s: %d bytes, too short for its %d-byte image" % (name, len(data), h["size"]))
    return h


def bitmap_len(size):
    return (size + 7) // 8


def make(a, b, name_a="a", name_b="b"):
    """The .PRL from the two links, or Refused naming the first difference that isn't a relocation."""
    ha, hb = header(a, name_a), header(b, name_b)
    if not ha["flags"] & F_RELOC:
        raise Refused("%s: not linked for a .PRL (the relocatable flag is clear): use cfg/prl.cfg" % name_a)
    if (hb["page"] - ha["page"]) & 0xFF != 1:
        raise Refused("%s is linked at page $%02X, %s at $%02X: they must be one page apart"
                      % (name_a, ha["page"], name_b, hb["page"]))
    zp = (hb["zpbase"] - ha["zpbase"]) & 0xFF
    if ha["zpsize"] and zp in (0, 1):
        raise Refused("the zero page bases are $%02X and $%02X: they must be 2 or more apart, or zero page"
                      " and page relocations look alike" % (ha["zpbase"], hb["zpbase"]))
    for f in ("flags", "size", "bss", "zpsize", "rsxcount", "reserved", "rsxrec"):
        if ha[f] != hb[f]:
            raise Refused("the headers' %s differ: %d in %s, %d in %s" % (f, ha[f], name_a, hb[f], name_b))
    size = ha["size"]
    ia, ib = a[HEADER:HEADER + size], b[HEADER:HEADER + size]
    pages = bytearray(bitmap_len(size))
    zps = bytearray(bitmap_len(size))
    base = ha["page"] << 8
    for i, (x, y) in enumerate(zip(ia, ib)):
        d = (y - x) & 0xFF
        if d == 0:
            continue
        if d == 1:
            pages[i >> 3] |= 0x80 >> (i & 7)
        elif d == zp and ha["zpsize"]:
            zps[i >> 3] |= 0x80 >> (i & 7)
        else:
            raise Refused("image offset $%04X ($%04X in %s): $%02X in %s, $%02X in %s, %d apart: neither a"
                          " page (1) nor the zero page (%d)" % (i, base + i, name_a, x, name_a, y, name_b, d, zp))
    return bytes(a[:HEADER]) + bytes(ia) + bytes(pages) + bytes(zps)


def load(data, page=None, zp=None, top=TOP, name="the file"):
    """The image as it is in memory once loaded at page (and zero page base zp), or Refused."""
    h = header(data, name)
    size = h["size"]
    if page is None:
        page = h["page"]
    if zp is None:
        zp = h["zpbase"]
    if (page << 8) + size + h["bss"] > top:
        raise Refused("%s: %d bytes and %d of BSS don't fit at page $%02X below $%04X"
                      % (name, size, h["bss"], page, top))
    image = bytearray(data[HEADER:HEADER + size])
    if not h["flags"] & F_RELOC:
        if page != h["page"]:
            raise Refused("%s: a .COM loads only at its link page, $%02X" % (name, h["page"]))
        return bytes(image)
    if zp + h["zpsize"] > 0x100:
        raise Refused("%s: %d bytes of zero page don't fit from $%02X" % (name, h["zpsize"], zp))
    n = bitmap_len(size)
    maps = data[HEADER + size:HEADER + size + 2 * n]
    if len(maps) < 2 * n:
        raise Refused("%s: the bitmaps are cut short" % name)
    dp, dz = (page - h["page"]) & 0xFF, (zp - h["zpbase"]) & 0xFF
    for i in range(size):
        bit = 0x80 >> (i & 7)
        if maps[i >> 3] & bit:
            image[i] = (image[i] + dp) & 0xFF
        elif maps[n + (i >> 3)] & bit:
            image[i] = (image[i] + dz) & 0xFF
    return bytes(image)


def main():
    ap = argparse.ArgumentParser(description="Make an OODOS .PRL from two links, or load one.")
    ap.add_argument("files", nargs="*", help="the two links, a then b")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--load", metavar="FILE", help="relocate FILE instead")
    ap.add_argument("--page", type=lambda s: int(s, 0), help="--load's page")
    ap.add_argument("--zp", type=lambda s: int(s, 0), help="--load's zero page base")
    a = ap.parse_args()
    try:
        if a.load:
            out = load(open(a.load, "rb").read(), a.page, a.zp, name=a.load)
        elif len(a.files) == 2:
            out = make(open(a.files[0], "rb").read(), open(a.files[1], "rb").read(), *a.files)
        else:
            ap.error("give the two links, or --load")
    except Refused as e:
        print("mkprl: %s" % e, file=sys.stderr)
        return 1
    with open(a.output, "wb") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
