"""Makes an OODOS .PRL from three links, and loads one as OODOS's loader will (hw/oodos.md, "RSXs and programs").

    python3 -I mkprl.py prog.a prog.b prog.c --map prog.a.map -o PROG.PRL
    python3 -I mkprl.py --load PROG.PRL --page 0x80 -o prog.img        # a program at $8000
    python3 -I mkprl.py --load RSX.PRL --rsx --page 0xA0 --zp 0xF8 -o rsx.img
    python3 -I mkprl.py --load OVL.PRL --overlay 0x6000 -o ovl.img     # function 59

prog.a, prog.b and prog.c are the same objects linked with cfg/prl.cfg three times: b a page above a
and its zero page segment 2 above, c two pages above a and its zero page 5 above. Every byte of the
images must move in b and c as one of three classes: not at all; by a page (1, then 2: the high byte
of an address in the image); or by the zero page offsets (2, then 5: a zero page address in the
image). Anything else is refused, with where. So is a zero page address that isn't the operand of a
zero page or immediate instruction in CODE (prog.a.map gives CODE): it would be a 16-bit pointer to
zero page, against rule 2.

The .PRL is a's header and image, then the page bitmap, then the zero page bitmap: a bit for each
byte of the image, the first byte in bit 7 of the first bitmap byte. Each bitmap starts on a record
(128 bytes), so a record of it covers 1,024 bytes of the image; the gaps and the file's end are
zeros, to whole records.

--load loads a .PRL or a .COM by the same steps and refusals as OODOS's loader, and writes the image
as it is in memory then. load() works on a 64 KB memory and touches only the image's bytes in it.
A file without the header's magic number is refused, by both.
"""

import argparse
import importlib.util
import os
import sys

MAGIC = b"OOD\x1a"
HEADER = 16
RECORD = 128
F_RELOC, F_RSX, F_NOT816 = 0x01, 0x02, 0x80
FLAGS = F_RELOC | F_RSX | F_NOT816
TPA_PAGE = 0x03  # a .COM's link page, and the lowest a program loads at
TOP = 0xC000  # the TPA's limit; the loader uses $0204's value
ZP_RSX = 0xF0  # $00-$EF are a program's, $F0-$FF the RSXs'
RSX_ZP_MAX = 16
ZP_MODES = ("imm", "zp", "zpx", "zpy", "inx", "iny", "izp", "zpr")

sys.dont_write_bytecode = True  # no __pycache__ in tools/
_spec = importlib.util.spec_from_file_location("rules", os.path.join(os.path.dirname(os.path.abspath(__file__)), "rules.py"))
rules = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rules)


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


def records(n):
    return (n + RECORD - 1) // RECORD


def bitmap_records(size):
    """The records the page bitmap and the zero page bitmap start at."""
    pages = records(HEADER + size)
    return pages, pages + records(bitmap_len(size))


def pad(b):
    """b, zeros to a whole number of records."""
    return bytes(b) + bytes(records(len(b)) * RECORD - len(b))


def zp_operands(image, base, code):
    """The image offsets that are the operand of a zero page or immediate instruction in CODE
    (code: CODE's first and last address in the link at base, or None)."""
    out = set()
    if code is None:
        return out
    pc, end = code
    while base <= pc <= end and pc - base < len(image):
        mode = rules.MODES[image[pc - base]]
        if mode in ZP_MODES:
            out.add(pc - base + 1)
        pc += rules.LEN[mode]
    return out


def make(a, b, c, code, name_a="a", name_b="b", name_c="c"):
    """The .PRL from the three links, or Refused naming the first byte that isn't a relocation.
    code: CODE's first and last address in a, from its map (code_range), or None for no CODE."""
    ha, hb, hc = header(a, name_a), header(b, name_b), header(c, name_c)
    if not ha["flags"] & F_RELOC:
        raise Refused("%s: not linked for a .PRL (the relocatable flag is clear): use cfg/prl.cfg" % name_a)
    if (hb["page"] - ha["page"]) & 0xFF != 1 or (hc["page"] - ha["page"]) & 0xFF != 2:
        raise Refused("%s, %s and %s are linked at pages $%02X, $%02X and $%02X: they must be one and two pages"
                      " above the first" % (name_a, name_b, name_c, ha["page"], hb["page"], hc["page"]))
    for f in ("flags", "size", "bss", "zpsize", "rsxcount", "reserved", "rsxrec"):
        if not ha[f] == hb[f] == hc[f]:
            raise Refused("the headers' %s differ: %d in %s, %d in %s, %d in %s"
                          % (f, ha[f], name_a, hb[f], name_b, hc[f], name_c))
    if ha["flags"] & F_RSX and ha["zpsize"] > RSX_ZP_MAX:
        raise Refused("%s: an RSX with %d bytes of zero page: an RSX has at most 16, in $F0-$FF" % (name_a, ha["zpsize"]))
    if ha["zpsize"] > ZP_RSX:
        raise Refused("%s: %d bytes of zero page: a program has at most $F0, $00-$EF" % (name_a, ha["zpsize"]))
    zp = ((hb["zpbase"] - ha["zpbase"]) & 0xFF, (hc["zpbase"] - ha["zpbase"]) & 0xFF)
    if ha["zpsize"] and zp in ((0, 0), (1, 2)):
        raise Refused("the zero page bases are $%02X, $%02X and $%02X: they move as %s, so the bitmaps can't be"
                      " told apart" % (ha["zpbase"], hb["zpbase"], hc["zpbase"],
                                       "nothing does" if zp == (0, 0) else "a page does"))
    size = ha["size"]
    ia, ib, ic = (x[HEADER:HEADER + size] for x in (a, b, c))
    base = ha["page"] << 8
    operands = zp_operands(ia, base, code)
    pages, zps = bytearray(bitmap_len(size)), bytearray(bitmap_len(size))
    for i in range(size):
        d = ((ib[i] - ia[i]) & 0xFF, (ic[i] - ia[i]) & 0xFF)
        if d == (0, 0):
            continue
        where = "image offset $%04X ($%04X in %s)" % (i, base + i, name_a)
        if d == (1, 2):
            pages[i >> 3] |= 0x80 >> (i & 7)
        elif d == zp and ha["zpsize"]:
            if i not in operands:
                raise Refused("%s: $%02X, a zero page address that isn't the operand of a zero page or immediate"
                              " instruction in CODE: a 16-bit pointer to zero page (rule 2)" % (where, ia[i]))
            zps[i >> 3] |= 0x80 >> (i & 7)
        else:
            raise Refused("%s: $%02X, $%02X and $%02X in the three links, moving by %d and %d: neither a page"
                          " (1, 2) nor the zero page (%d, %d)" % (where, ia[i], ib[i], ic[i], d[0], d[1], zp[0], zp[1]))
    return pad(a[:HEADER + size]) + pad(pages) + pad(zps)


def read_records(data, first, count, name):
    """count records of the file from record first, as the BDOS reads them: Refused past its end."""
    if count and (first + count - 1) * RECORD >= len(data):
        raise Refused("%s: ends at record %d, before its bitmaps do" % (name, records(len(data))))
    return [pad(data[r * RECORD:(r + 1) * RECORD]) for r in range(first, first + count)]


def load(data, page=None, zp=None, top=TOP, name="the file", kind="program", mem=None):
    """Load a program, an RSX or an overlay (kind) at page, a .PRL's zero page at zp, into mem
    (64 KB; made if None) as OODOS's loader does. @return the image as it is in memory, or Refused"""
    h = header(data, name)
    size, flags = h["size"], h["flags"]
    reloc = bool(flags & F_RELOC)
    if flags & ~FLAGS:
        raise Refused("%s: unknown flag bits $%02X in the header" % (name, flags & ~FLAGS))
    if bool(flags & F_RSX) != (kind == "rsx"):
        raise Refused("%s: %s, loaded as %s" % (name, "an RSX" if flags & F_RSX else "not an RSX",
                                                 "an RSX" if kind == "rsx" else "a " + kind))
    if kind == "rsx" and not reloc:
        raise Refused("%s: an RSX must be a .PRL" % name)
    if page is None:
        page = h["page"]
    if zp is None:
        zp = ZP_RSX if kind == "rsx" else h["zpbase"]
    if not reloc:
        if h["page"] != TPA_PAGE:
            raise Refused("%s: a .COM linked at page $%02X: a .COM is linked at $%02X" % (name, h["page"], TPA_PAGE))
        if page != h["page"] or zp != h["zpbase"]:
            raise Refused("%s: a .COM loads only at its link page, $%02X" % (name, h["page"]))
    if page < TPA_PAGE:
        raise Refused("%s: page $%02X is below the TPA ($%02X)" % (name, page, TPA_PAGE))
    if (page << 8) + size + h["bss"] > top:
        raise Refused("%s: %d bytes and %d of BSS don't fit at page $%02X below $%04X"
                      % (name, size, h["bss"], page, top))
    if kind == "overlay" and h["zpsize"]:
        raise Refused("%s: an overlay with %d bytes of zero page: an overlay has none, it uses its caller's"
                      % (name, h["zpsize"]))
    if kind == "rsx" and (zp < ZP_RSX or zp + h["zpsize"] > 0x100):
        raise Refused("%s: %d bytes of zero page from $%02X: an RSX's must be in what's free of $F0-$FF"
                      % (name, h["zpsize"], zp))
    if kind != "rsx" and zp + h["zpsize"] > ZP_RSX:
        raise Refused("%s: %d bytes of zero page don't fit from $%02X below $F0" % (name, h["zpsize"], zp))

    if mem is None:
        mem = bytearray(0x10000)
    base = page << 8
    # The image: the first record's 112 bytes after the header, then a record at a time, each only
    # as far as the image goes. Nothing past it is read into memory.
    at, r = 0, 0
    while at < size:
        rec = read_records(data, r, 1, name)[0]
        part = rec[HEADER:] if r == 0 else rec
        n = min(len(part), size - at)
        mem[base + at:base + at + n] = part[:n]
        at, r = at + n, r + 1
    if reloc:
        # Each bitmap a record at a time, a record covering 1,024 bytes of the image.
        dp, dz = (page - h["page"]) & 0xFF, (zp - h["zpbase"]) & 0xFF
        for first, d in zip(bitmap_records(size), (dp, dz)):
            for k, rec in enumerate(read_records(data, first, records(bitmap_len(size)), name)):
                for j in range(min(RECORD * 8, size - k * RECORD * 8)):  # bits past the image are padding
                    if rec[j >> 3] & (0x80 >> (j & 7)):
                        i = base + k * RECORD * 8 + j
                        mem[i] = (mem[i] + d) & 0xFF
    return bytes(mem[base:base + size])


def overlay(data, address, top=TOP, name="the file", mem=None):
    """Function 59: load an overlay at address, which must be a page's start. @return as load()"""
    if address & 0xFF:
        raise Refused("%s: load address $%04X isn't a page's start: an overlay loads on a page" % (name, address))
    return load(data, address >> 8, None, top, name, "overlay", mem)


def code_range(mapfile):
    """CODE's first and last address, from ld65's map; None if it's empty."""
    start, end, size = rules.segments(mapfile).get("CODE", (0, 0, 0))
    return (start, end) if size else None


def main():
    ap = argparse.ArgumentParser(description="Make an OODOS .PRL from three links, or load one.")
    ap.add_argument("files", nargs="*", help="the three links, a, b then c")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--map", help="a's ld65 map, for CODE")
    ap.add_argument("--load", metavar="FILE", help="load FILE instead")
    num = lambda s: int(s, 0)
    ap.add_argument("--page", type=num, help="--load's page")
    ap.add_argument("--zp", type=num, help="--load's zero page base")
    ap.add_argument("--rsx", action="store_true", help="--load an RSX")
    ap.add_argument("--overlay", type=num, metavar="ADDRESS", help="--load an overlay at ADDRESS (function 59)")
    a = ap.parse_args()
    try:
        if a.load:
            data = open(a.load, "rb").read()
            if a.overlay is not None:
                out = overlay(data, a.overlay, name=a.load)
            else:
                out = load(data, a.page, a.zp, name=a.load, kind="rsx" if a.rsx else "program")
        elif len(a.files) == 3 and a.map:
            out = make(*(open(f, "rb").read() for f in a.files), code_range(a.map), *a.files)
        else:
            ap.error("give the three links and --map, or --load")
    except Refused as e:
        print("mkprl: %s" % e, file=sys.stderr)
        return 1
    with open(a.output, "wb") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
