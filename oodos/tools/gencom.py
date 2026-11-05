"""Attaches RSXs to an OODOS .COM, as GENCOM does (hw/oodos.md, "The RSXs, phase 5"), for the build.

    python3 -I gencom.py PROG.COM RSX1.RSX RSX2.RSX -o OUT.COM          # attach, or add and replace
    python3 -I gencom.py PROG.COM --scb 0x34,0x02 -o OUT.COM            # SCB entries, no RSXs
    python3 -I gencom.py --null RSX1.RSX RSX2.RSX -o RSX1.COM           # an RSX-only .COM
    python3 -I gencom.py --strip PROG.COM -o OUT.COM                    # back as it was

The .COM keeps its header and image; the attached RSX table follows its image's last record: 16-byte
entries (include/oodos.inc's RT_*), the record each RSX's file starts at, a flags byte (RT_NONBANK
when the RSX's prefix says non-banked; RT_SCB for an SCB entry, its offset and value in place of the
record), the name (the RSX file's, 8 characters, space padded), 5 reserved. Each RSX follows, whole,
on a record. The header's bytes 12 and 14 give the count and the table's record. At most 15 entries,
RSXs and SCB entries together; the file at most 64 KB. An RSX named again replaces the one attached
(by its name); one named twice in the same command is taken once. --loader is accepted and does
nothing (OODOS's loader is always there). GENCOM.COM (phase 5b) makes the same bytes.
"""

import argparse
import importlib.util
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ in tools/
_spec = importlib.util.spec_from_file_location("mkprl", os.path.join(os.path.dirname(os.path.abspath(__file__)), "mkprl.py"))
mkprl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mkprl)

Refused = mkprl.Refused
HEADER, RECORD = mkprl.HEADER, mkprl.RECORD
ENTRY = 16
MAX_ENTRIES = 15
RT_NONBANK, RT_SCB = 0x01, 0x02
SCB_LAST = 0x63  # OODOS's SCB ends here (the Guide's GENCOM allows to $64)


def rsx_length(data, name):
    """An RSX file's length in whole records, from its header: the image, then its two bitmaps."""
    h = mkprl.header(data, name)
    if h["flags"] != mkprl.F_RELOC | mkprl.F_RSX:
        raise Refused("%s: not an RSX (flags $%02X)" % (name, h["flags"]))
    mkprl.rsx_prefix(data[HEADER:HEADER + h["size"]], h["page"] << 8, name)
    n = (mkprl.records(HEADER + h["size"]) + 2 * mkprl.records(mkprl.bitmap_len(h["size"]))) * RECORD
    if len(data) < n:
        raise Refused("%s: %d bytes, too short for its image and bitmaps (%d)" % (name, len(data), n))
    return n


def rsx_name(path):
    base = os.path.basename(path).split(".")[0].upper()
    if not 1 <= len(base) <= 8:
        raise Refused("%s: an RSX's name is 1-8 characters" % path)
    return base


def split(data, name):
    """A .COM's header and image (bytes 12-15 cleared), and what it has attached: [(name, flags, rsx)
    or ('', RT_SCB, (offset, value))]."""
    h = mkprl.header(data, name)
    if h["flags"] != 0 or h["page"] != mkprl.TPA_PAGE:
        raise Refused("%s: not a .COM (flags $%02X, page $%02X)" % (name, h["flags"], h["page"]))
    image = bytearray(mkprl.pad(data[:HEADER + h["size"]]))
    image[12:16] = bytes(4)
    attached = []
    for i in range(h["rsxcount"]):
        at = h["rsxrec"] * RECORD + i * ENTRY
        e = data[at:at + ENTRY]
        if len(e) < ENTRY:
            raise Refused("%s: its attached RSX table is cut short" % name)
        if e[2] & RT_SCB:
            attached.append(("", RT_SCB, (e[0], e[1])))
            continue
        rec = e[0] | e[1] << 8
        rsx = data[rec * RECORD:]
        rsx = rsx[:rsx_length(rsx, "%s's RSX %d" % (name, i))]
        attached.append((e[3:11].decode("ascii").rstrip(), e[2], rsx))
    return bytes(image), attached


def null_com():
    """The header of an RSX-only .COM: an image of size 0."""
    return mkprl.pad(mkprl.MAGIC + bytes([0, mkprl.TPA_PAGE]) + bytes(10))


def build(image, attached):
    if len(attached) > MAX_ENTRIES:
        raise Refused("%d entries: the header has room for %d, RSXs and SCB entries together" % (len(attached), MAX_ENTRIES))
    out = bytearray(image)
    if not attached:
        return bytes(out)
    table_rec = len(out) // RECORD
    table = bytearray()
    rec = table_rec + mkprl.records(len(attached) * ENTRY)
    blobs = []
    for name, flags, what in attached:
        if flags & RT_SCB:
            table += bytes([what[0], what[1], RT_SCB]) + b" " * 8 + bytes(5)
            continue
        table += bytes([rec & 0xFF, rec >> 8, flags]) + name.encode("ascii").ljust(8) + bytes(5)
        blobs.append(mkprl.pad(what))
        rec += len(blobs[-1]) // RECORD
    out += mkprl.pad(table)
    for b in blobs:
        out += b
    if len(out) > 0x10000:
        raise Refused("%d bytes: the file may be at most 64K" % len(out))
    out[12] = len(attached)
    out[14:16] = table_rec.to_bytes(2, "little")
    return bytes(out)


def gencom(com=None, rsxs=(), scb=(), null=False, strip=False, com_name="the .COM"):
    """The new file's bytes. com: the .COM's bytes (None with null); rsxs: [(path, bytes)]; scb:
    [(offset, value)]."""
    if null:
        if com is not None:
            raise Refused("a .COM given with the NULL option")
        image, attached = null_com(), []
        if not rsxs:
            raise Refused("the NULL option needs an RSX")
    else:
        image, attached = split(com, com_name)
    if strip:
        return build(image, [])
    seen = set()
    for path, data in rsxs:
        name = rsx_name(path)
        if name in seen:
            print("gencom: Duplicate input RSX: %s, taken once" % name, file=sys.stderr)
            continue
        seen.add(name)
        length = rsx_length(data, path)
        flags = RT_NONBANK if data[HEADER + mkprl.RSX_NONBANK] == 0xFF else 0
        new = (name, flags, data[:length])
        for i, (n, f, _) in enumerate(attached):
            if n == name and not f & RT_SCB:
                print("gencom: Duplicate RSX in header: %s replaced" % name, file=sys.stderr)
                attached[i] = new
                break
        else:
            attached.append(new)
    for off, val in scb:
        if not 0 <= off <= SCB_LAST or not 0 <= val <= 0xFF:
            raise Refused("SCB=(%X,%X): the offset is $00-$%02X, the value a byte" % (off, val, SCB_LAST))
        for i, (n, f, w) in enumerate(attached):
            if f & RT_SCB and w[0] == off:
                attached[i] = ("", RT_SCB, (off, val))
                break
        else:
            attached.append(("", RT_SCB, (off, val)))
    return build(image, attached)


def main():
    ap = argparse.ArgumentParser(description="Attach RSXs to an OODOS .COM, as GENCOM does.")
    ap.add_argument("files", nargs="+", help="the .COM, then the RSXs (with --null, the RSXs only)")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--null", action="store_true", help="an RSX-only .COM")
    ap.add_argument("--strip", action="store_true", help="what GENCOM added taken off")
    ap.add_argument("--loader", action="store_true", help="accepted, and does nothing")
    ap.add_argument("--scb", action="append", default=[], metavar="OFFSET,VALUE", help="an SCB entry (hex)")
    a = ap.parse_args()
    try:
        scb = []
        for s in a.scb:
            off, _, val = s.partition(",")
            scb.append((int(off, 16), int(val, 16)))
        rd = lambda p: open(p, "rb").read()
        if a.null:
            out = gencom(None, [(p, rd(p)) for p in a.files], scb, null=True)
        else:
            out = gencom(rd(a.files[0]), [(p, rd(p)) for p in a.files[1:]], scb, strip=a.strip, com_name=a.files[0])
    except (Refused, ValueError) as e:
        print("gencom: %s" % e, file=sys.stderr)
        return 1
    with open(a.output, "wb") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
