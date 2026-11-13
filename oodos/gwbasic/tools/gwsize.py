"""GW-BASIC's size gates (hw/oodos.md, "The plan, phase 7": the memory layout).

    python3 -I gwsize.py BUILD/gwbasic.map [--block BUILD/p1.map ...] [--min 32768] [--warn 33792]
                         [--free FILE]

From the resident link's map: the resident code (CODE, RODATA, DATA), fixed RAM (LOWRAM, RAM), and the
bytes free for BASIC, as GWINIT's banner computes them (p2/gwinit.s): MEMSIZ, the TPA's top ($BFFD, the
BDOS's JMP, with no RSX) less 2, less TXTTAB, the loader's first byte plus 1, less 2 (FRE(0) with no
program). --free writes that figure to FILE, for test_gwbasic_core to hold the banner to. Fails if fewer
than --min are free (32,768, decided 2026-10-26) and warns under --warn; fails if LOWRAM, what a primitive
or a hook may touch, has a byte in $4000-$7FFF (the W1 contract, V4); fails if any segment with bytes but
LOADER, LOADDAT and FARFIX (the loader's, which the workspace takes over) starts at or after LOADER: BSS
or a ZEROPAGE segment there, or fixed RAM, would be under BASIC's program. Each --block map's link is
listed against its 12,288 bytes.
"""
import argparse
import re
import sys

TOP = 0xBFFD  # the BDOS's JMP: the TPA is $0300-$BFFC
TRANSIENT = ("LOADER", "LOADDAT", "FARFIX")  # the loader's: the workspace from LOADER on


def segments(path):
    segs = {}
    for l in open(path):
        m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", l)
        if m:
            segs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
    return segs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--block", action="append", default=[])
    ap.add_argument("--min", type=int, default=32768)
    ap.add_argument("--warn", type=int, default=33792)
    ap.add_argument("--free", help="write the bytes free to this file")
    a = ap.parse_args()
    s = segments(a.map)
    size = lambda *names: sum(s[n][2] for n in names if n in s)
    code, ram = size("CODE", "RODATA", "DATA"), size("LOWRAM", "RAM")
    ws = s["LOADER"][0]
    free = (TOP - 2) - (ws + 1) - 2  # GWINIT's MEMSIZ - TXTTAB - 2
    bad = 0
    print("GW-BASIC's resident core: %d bytes of code and tables, %d of fixed RAM (LOWRAM %d), "
          "the loader %d" % (code, ram, size("LOWRAM"), size("LOADER")))
    print("  the workspace from $%04X: %d bytes free for BASIC (the gate %d, the warning %d)"
          % (ws, free, a.min, a.warn))
    if free < a.min:
        print("gwsize.py: %d bytes free, %d under the gate's %d" % (free, a.min - free, a.min))
        bad = 1
    elif free < a.warn:
        print("gwsize.py: warning: %d bytes free, under %d" % (free, a.warn))
    if "LOWRAM" in s and s["LOWRAM"][2] and s["LOWRAM"][1] >= 0x4000:
        print("gwsize.py: LOWRAM ends at $%04X, in $4000-$7FFF (the W1 contract)" % s["LOWRAM"][1])
        bad = 1
    for name, (start, _, n) in sorted(s.items(), key=lambda e: e[1][0]):
        if n and name not in TRANSIENT and start >= ws:
            print("gwsize.py: %s starts at $%04X, at or after LOADER ($%04X): in BASIC's workspace" % (name, start, ws))
            bad = 1
    if a.free:
        open(a.free, "w").write("%d\n" % free)
    for b in a.block:
        bs = segments(b)
        used = sum(v[2] for k, v in bs.items() if k in ("BLKHDR", "CODE", "RODATA", "DATA", "BRAM", "FARFIX"))
        print("  %s: %d bytes of 12,288, %d free" % (b.rsplit("/", 1)[-1], used, 12288 - used))
        if used > 12288:
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
