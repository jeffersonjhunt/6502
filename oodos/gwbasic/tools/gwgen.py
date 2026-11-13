"""GW-BASIC's far call entries (hw/oodos.md, "The plan, phase 7"; gwbasic.inc, "Far calls").

    python3 -I gwgen.py BUILD p1 p2 ...

Each block's sources (pN/*.s) name its entries with a line ";@entry NAME". This writes:
  BUILD/entries.inc   PG_NAME (the block's page, N) and E_NAME (its jump table entry) for every
                      entry, for FJSR in any module, resident or banked;
  BUILD/pN_hdr.s      block N's header and jump table, its first segment (BLKHDR).
Entries are numbered in the order of the files' names, then of the lines. A name given twice, in one
block or two, is an error.
"""
import os
import re
import sys

ENTRY = re.compile(r"^\s*;@entry\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
BLK_ENTRY = 0xC804  # gwbasic.inc's BLK_ENTRY


def entries(block_dir):
    out = []
    for f in sorted(os.listdir(block_dir)):
        if f.endswith(".s"):
            for line in open(os.path.join(block_dir, f), encoding="ascii"):
                m = ENTRY.match(line)
                if m:
                    out.append(m.group(1))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: gwgen.py BUILD pN-directory...")
    build, dirs = sys.argv[1], sys.argv[2:]
    seen = {}
    inc = ["; Made by tools/gwgen.py from the blocks' ;@entry lines: don't edit."]
    for d in dirs:
        m = re.search(r"p([1-8])$", d.rstrip("/"))
        if not m:
            sys.exit("gwgen.py: %s isn't a block's directory (p1-p8)" % d)
        page = int(m.group(1))
        names = entries(d)
        hdr = ["; Made by tools/gwgen.py: block P%d's header and jump table (gwbasic.inc)." % page,
               '.setcpu "65C02"',
               ".import __FARFIX_RUN__, __FARFIX_SIZE__"]
        if names:
            hdr.append(".import " + ", ".join(names))
        hdr += ['.segment "BLKHDR"', "        .word   __FARFIX_RUN__", "        .word   __FARFIX_SIZE__ / 2"]
        for i, n in enumerate(names):
            if n in seen:
                sys.exit("gwgen.py: %s is an entry of P%d and P%d" % (n, seen[n], page))
            seen[n] = page
            inc.append("PG_%s = %d" % (n, page))
            inc.append("E_%s = $%04X" % (n, BLK_ENTRY + 3 * i))
            hdr.append("        JMP     %s" % n)
        hdr += ['.segment "%s"' % seg for seg in ("CODE", "RODATA", "DATA", "BRAM", "FARFIX")]  # each there
        put(os.path.join(build, "p%d_hdr.s" % page), "\n".join(hdr) + "\n")
    put(os.path.join(build, "entries.inc"), "\n".join(inc) + "\n")


def put(path, text):
    """Write the file only if it changed, so make doesn't rebuild what didn't."""
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)


if __name__ == "__main__":
    main()
