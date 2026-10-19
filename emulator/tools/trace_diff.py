#!/usr/bin/env python3
"""Diff two bus traces (docs/BUS_TRACE.md) and report the first difference.

  python3 tools/trace_diff.py board.trace emu.trace
  python3 tools/trace_diff.py --from FF00 --symbols ibasic.lbl board.trace emu.trace

Cycles are compared relative to each trace's first line (after --from), so a
capture and an emulator run need not start counting at the same number, but
a gap (WAI, STP) must be the same length in both. A field of `?` is "not
captured" and matches anything.

Exit status: 0 if the traces agree for the length of the shorter one, 1 at
the first difference, 2 if a trace is malformed or --from is never reached.
"""

import argparse
import sys

HEADER = "# ooe-bus-trace 1"
FIELDS = ("cycle", "address", "data", "R/W", "signals", "module")


class TraceError(Exception):
    pass


def read_trace(path):
    """[(line number, [six fields])]; raises TraceError on a malformed line."""
    records = []
    with open(path, encoding="ascii", errors="replace") as f:
        first = f.readline().rstrip("\r\n")
        if first != HEADER:
            raise TraceError(f"{path}:1: expected '{HEADER}', got '{first}'")
        for n, line in enumerate(f, start=2):
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split(" ")
            if len(fields) != 6 or not fields[0].isdigit() or not well_formed(fields):
                raise TraceError(f"{path}:{n}: not a trace line: '{line}'")
            records.append((n, fields))
    return records


def well_formed(fields):
    _, addr, data, rw, sig, mod = fields
    def hexfield(v, width):
        return v == "?" or (len(v) == width and all(c in "0123456789ABCDEF" for c in v))
    return (hexfield(addr, 5) and hexfield(data, 2) and rw in ("R", "W", "?")
            and (sig == "?" or (len(sig) == 2 and sig[0] in "S-?" and sig[1] in "V-?"))
            and (mod == "-" or hexfield(mod, 1)))


def read_symbols(path):
    names = {}
    with open(path, encoding="ascii", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3 and parts[0] == "al" and parts[2].startswith("."):
                names.setdefault(int(parts[1], 16), parts[2][1:])
    return names


def label(addr_field, names):
    if not names or addr_field == "?":
        return ""
    addr = int(addr_field, 16) & 0xFFFF
    for off in range(0, 256):
        name = names.get(addr - off)
        if name:
            return f"  {name}" if off == 0 else f"  {name}+{off}"
    return ""


def start_at(records, addr, path):
    for i, (_, f) in enumerate(records):
        if f[4][0] == "S" and f[1] != "?" and int(f[1], 16) == addr:
            return records[i:]
    raise TraceError(f"{path}: no opcode fetch at {addr:05X} (--from)")


def field_matches(k, a, b):
    if a == "?" or b == "?":
        return True
    if FIELDS[k] == "signals":  # SYNC and VPB can each be uncaptured
        return all(x == y or "?" in (x, y) for x, y in zip(a, b))
    return a == b


def show(path, records, i, names, width):
    if i >= len(records):
        return f"{path}: (ended)"
    n, f = records[i]
    return f"{path}:{n}: {' '.join(f):<{width}}{label(f[1], names)}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--from", dest="start", type=lambda v: int(v, 16), metavar="ADDR",
                    help="start each trace at its first opcode fetch at this hex address")
    ap.add_argument("--symbols", metavar="LBL", help="ld65 -Ln label file for the report")
    ap.add_argument("--context", type=int, default=4, help="lines of context before the difference")
    args = ap.parse_args()

    try:
        a, b = read_trace(args.a), read_trace(args.b)
        if args.start is not None:
            a, b = start_at(a, args.start, args.a), start_at(b, args.start, args.b)
        names = read_symbols(args.symbols) if args.symbols else {}
    except (OSError, TraceError) as e:
        print(f"trace_diff: {e}", file=sys.stderr)
        return 2
    if not a or not b:
        print(f"trace_diff: {args.a if not a else args.b} has no cycles", file=sys.stderr)
        return 2

    a0, b0 = int(a[0][1][0]), int(b[0][1][0])
    for i in range(min(len(a), len(b))):
        fa, fb = a[i][1], b[i][1]
        diffs = []
        if int(fa[0]) - a0 != int(fb[0]) - b0:
            diffs.append(f"cycle +{int(fa[0]) - a0} vs +{int(fb[0]) - b0}")
        for k in range(1, 6):
            if not field_matches(k, fa[k], fb[k]):
                diffs.append(f"{FIELDS[k]} {fa[k]} vs {fb[k]}")
        if not diffs:
            continue
        print(f"first difference at cycle {i + 1} of the comparison: {', '.join(diffs)}")
        width = max(len(" ".join(r[1])) for r in a[max(0, i - args.context):i + 1] + b[max(0, i - args.context):i + 1])
        for j in range(max(0, i - args.context), i + 1):
            mark = ">" if j == i else " "
            print(f"{mark} {show(args.a, a, j, names, width)}")
            print(f"{mark} {show(args.b, b, j, names, width)}")
        return 1

    compared = min(len(a), len(b))
    if len(a) != len(b):
        longer, extra = (args.a, len(a) - compared) if len(a) > len(b) else (args.b, len(b) - compared)
        print(f"traces agree for {compared} cycles; {longer} has {extra} more")
    else:
        print(f"traces agree: {compared} cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
