"""Keeps include/oodos.h's names the same as include/oodos.inc's (Plan C1).

    python3 -I oodosh.py check include/oodos.inc include/oodos.h
    python3 -I oodosh.py write include/oodos.inc include/oodos.h

oodos.h's block between its BEGIN and END lines is written from oodos.inc: the base page's
addresses (BP_*, WBOOT, BDOS) and every BDOS function's name (P_TERMCPM to the end of that list),
each as a #define with the same value. `check` fails if the block isn't what `write` would make,
or if either list comes out empty; `write` puts it in.
"""

import re
import sys

BEGIN = "/* BEGIN: written from oodos.inc by tools/oodosh.py; don't edit by hand. */"
END = "/* END: written from oodos.inc. */"
NAME = re.compile(r"^([A-Z][A-Z0-9_]*)\s*=\s*(\$[0-9A-Fa-f]+|[0-9]+)\b")


def names(inc):
    """The base page's names and the functions', each a list of (name, value), in oodos.inc's order."""
    page, fns, part = [], [], None
    for line in open(inc):
        if line.startswith("; The base page"):
            part = page
            continue
        if line.startswith("; BDOS functions, in Y"):
            part = fns
            continue
        m = NAME.match(line)
        if part is not None and m:
            v = m.group(2)
            part.append((m.group(1), int(v[1:], 16) if v.startswith("$") else int(v)))
        elif part is not None and not line.strip():
            part = None  # a list ends at a blank line
    return page, fns


def block(inc):
    page, fns = names(inc)
    if not page or not fns:
        raise SystemExit("oodosh.py: %s: no %s found" % (inc, "base page" if not page else "BDOS functions"))
    out = [BEGIN, "", "/* The base page (CP/M's page 0, at $0200). */"]
    out += ["#define %-16s 0x%04X" % (n, v) for n, v in page]
    out += ["", "/* The BDOS functions, bdos()'s fn. */"]
    out += ["#define %-16s %d" % (n, v) for n, v in fns]
    out += ["", END]
    return "\n".join(out)


def split(h):
    text = open(h).read()
    a, b = text.find(BEGIN), text.find(END)
    if a < 0 or b < a:
        raise SystemExit("oodosh.py: %s: no BEGIN and END lines" % h)
    return text[:a], text[a:b + len(END)], text[b + len(END):]


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("check", "write"):
        raise SystemExit(__doc__)
    op, inc, h = sys.argv[1:]
    want = block(inc)
    before, have, after = split(h)
    if op == "write":
        open(h, "w").write(before + want + after)
        return 0
    if have != want:
        print("oodosh.py: %s's names differ from %s's: run `python3 -I tools/oodosh.py write %s %s`"
              % (h, inc, inc, h), file=sys.stderr)
        return 1
    page, fns = names(inc)
    print("oodosh.py: %s: %d base page names, %d functions, as %s" % (h, len(page), len(fns), inc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
