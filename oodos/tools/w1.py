"""A program linked with OOLIB or OOUI kept out of W1 (the W1 contract, interface 8: hw/oolib.md, "Video card
revision 2"; hw/ooui.md): while the library has block 33 in W1, the program's own $4000-$7FFF is out of its
reach, so its code, its data and its BSS must all be in $0300-$3FFF or $8000-$BFFC.

    python3 -I w1.py prog.map

Reads ld65's map (-m) and fails, naming each, on any segment of the image (every one but HEADER and
ZEROPAGE) that overlaps $4000-$7FFF, and on a map with no segments in it (nothing checked). The Makefile
runs it on every program in OOLIB_USERS and OOUI_USERS (the OOUI U1 review, O8).

What it can't see: absolute equates (an event area or a buffer at a fixed address, `AREA = $8000`) aren't
segments, so the program keeps those out of $4000-$7FFF itself; and a .PRL overlay linked with the library
(OVL_LIB) has no load address until function 59 relocates it, so it isn't checked here: the program loads it
outside $4000-$7FFF (hw/oolib.md; the Programmer's Guide, chapter 8).
"""
import re
import sys

W1_FIRST, W1_LAST = 0x4000, 0x7FFF


def segments(mapfile):
    """[(name, start, end, size)] from the map's segment list."""
    text = open(mapfile).read()
    m = re.search(r'^Segment list:\n-+\n.*?\n-+\n(.*?)(?:\n\n|\Z)', text, re.M | re.S)
    if not m:
        return None
    return [(n, int(a, 16), int(b, 16), int(z, 16))
            for n, a, b, z in re.findall(r'^(\S+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})', m.group(1), re.M)]


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[3].strip(), file=sys.stderr)
        return 2
    segs = segments(argv[1])
    if not segs:  # no list, or a list with no rows: nothing was checked, which must not pass (round 2's W6)
        print('w1: %s: no segments in its segment list' % argv[1], file=sys.stderr)
        return 1
    bad = [(n, a, b) for n, a, b, z in segs if n not in ('HEADER', 'ZEROPAGE') and z
           and a <= W1_LAST and b >= W1_FIRST]
    for n, a, b in bad:
        print('w1: %s: %s ($%04X-$%04X) overlaps W1, $4000-$7FFF' % (argv[1], n, a, b), file=sys.stderr)
    if bad:
        return 1
    top = max((b for n, a, b, z in segs if n not in ('HEADER', 'ZEROPAGE') and z and b < W1_FIRST), default=0)
    print('w1: out of $4000-$7FFF (below it to $%04X)' % top)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
