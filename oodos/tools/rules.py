"""Checks a program's code against OODOS's rules for programs: the 65C816 (hw/oodos.md).

    python3 -I rules.py hello.com --map hello.map
    python3 -I rules.py spike.bin --load 0x0300 --map spike.map --code GWMAIN,GWEVAL \\
        --lbl spike.lbl --inline SYNCHR

A file that starts with OODOS's header (a .COM, or a .PRL's link) is read from it: the image after the
16 bytes, at its link page. Anything else is a bare image at --load. Disassembles each code segment
(--code, from the map; CODE by default) and fails on:

  rule 1   RMB, SMB, BBR or BBS (opcodes $x7 and $xF);
  rule 2   an absolute operand in zero page ($0000-$00FF): zero page only
           through zero page operands;
  rule 3   an absolute operand in the stack page ($0100-$01FF);
  rule 4   a JSR or JMP outside the image but to $0203 (the BDOS) or $0200
           (warm boot);
  and      an opcode the code shouldn't have (an undocumented NOP), which
           would mean data in a code segment.

Rule 2's other half, no 16-bit pointers to zero page, can't be seen in the code: keep zero page
addresses to zero page operands and immediates (pass them in X). Indirect JMPs aren't followed. --inline names
a routine whose JSR is followed by a byte of data (GW-BASIC's SYNCHR); it needs --lbl, ld65's -Ln.
"""

import argparse
import re
import sys

MAGIC = b"OOD\x1a"
HEADER = 16

# The W65C02S: mode per opcode. imp acc imm zp zpx zpy abs abx aby ind aix
# inx iny izp rel zpr (BBR/BBS), and "bad" for an undocumented NOP.
ROWS = """
imp inx imm bad zp  zp  zp  zp  imp imm acc bad abs abs abs zpr
rel iny izp bad zp  zpx zpx zp  imp aby acc bad abs abx abx zpr
abs inx imm bad zp  zp  zp  zp  imp imm acc bad abs abs abs zpr
rel iny izp bad zpx zpx zpx zp  imp aby acc bad abx abx abx zpr
imp inx imm bad zp  zp  zp  zp  imp imm acc bad abs abs abs zpr
rel iny izp bad zpx zpx zpx zp  imp aby imp bad abs abx abx zpr
imp inx imm bad zp  zp  zp  zp  imp imm acc bad ind abs abs zpr
rel iny izp bad zpx zpx zpx zp  imp aby imp bad aix abx abx zpr
rel inx imm bad zp  zp  zp  zp  imp imm imp bad abs abs abs zpr
rel iny izp bad zpx zpx zpy zp  imp aby imp bad abs abx abx zpr
imm inx imm bad zp  zp  zp  zp  imp imm imp bad abs abs abs zpr
rel iny izp bad zpx zpx zpy zp  imp aby imp bad abx abx aby zpr
imm inx imm bad zp  zp  zp  zp  imp imm imp imp abs abs abs zpr
rel iny izp bad zpx zpx zpx zp  imp aby imp imp abs abx abx zpr
imm inx imm bad zp  zp  zp  zp  imp imm imp bad abs abs abs zpr
rel iny izp bad zpx zpx zpx zp  imp aby imp bad abs abx abx zpr
"""
MODES = [m for row in ROWS.split("\n") if row for m in row.split()]
LEN = {"imp": 1, "acc": 1, "bad": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "inx": 2, "iny": 2, "izp": 2,
       "rel": 2, "abs": 3, "abx": 3, "aby": 3, "ind": 3, "aix": 3, "zpr": 3}
ABSOLUTE = ("abs", "abx", "aby", "ind", "aix")
OS = (0x0200, 0x0203)  # warm boot, the BDOS
NOPS = {0x44, 0x54, 0x5C, 0xD4, 0xDC, 0xF4, 0xFC, 0x02, 0x22, 0x42, 0x62, 0x82, 0xC2, 0xE2}


def segments(mapfile):
    segs = {}
    for l in open(mapfile):
        m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", l)
        if m:
            segs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
    return segs


def labels(lblfile):
    out = {}
    for l in open(lblfile):
        m = re.match(r"al ([0-9A-F]+) \.(\w+)", l)
        if m:
            out[m.group(2)] = int(m.group(1), 16)
    return out


def check(data, load, segs, code, inline=()):
    """Returns (instructions, problems): each problem a line to print."""
    problems = []
    count = 0
    for name in code:
        if name not in segs:
            problems.append("%s: no such segment in the map" % name)
            continue
        start, end, size = segs[name]
        if size == 0:
            continue
        if start < load or end >= load + len(data):
            problems.append("%s: $%04X-$%04X is outside the image" % (name, start, end))
            continue
        pc = start
        while pc <= end:
            op = data[pc - load]
            mode = MODES[op]
            n = LEN[mode]
            operand = int.from_bytes(data[pc - load + 1:pc - load + n], "little") if n > 1 else None
            why = None
            if (op & 0x07) == 0x07:
                why = "RMB/SMB/BBR/BBS (rule 1)"
            elif mode == "bad" or op in NOPS:
                why = "not an instruction the code uses: data in code?"
            elif mode in ABSOLUTE and operand < 0x100:
                why = "absolute zero page operand (rule 2)"
            elif mode in ABSOLUTE and operand < 0x200:
                why = "absolute stack page operand (rule 3)"
            elif op in (0x20, 0x4C) and not (load <= operand < load + len(data)) and operand not in OS:
                why = "%s outside the program, not the BDOS (rule 4)" % ("JSR" if op == 0x20 else "JMP")
            if why:
                problems.append("%s $%04X: opcode $%02X %s" % (name, pc, op, why))
            count += 1
            pc += n
            if op == 0x20 and operand in inline:
                pc += 1
        if pc != end + 1:
            problems.append("%s: disassembly ran past the segment's end" % name)
    return count, problems


def main():
    ap = argparse.ArgumentParser(description="Check a program against OODOS's 65C816 rules.")
    ap.add_argument("image")
    ap.add_argument("--map", required=True, help="ld65's -m map, for the segments")
    ap.add_argument("--code", default="CODE", help="the code segments, comma separated (CODE)")
    ap.add_argument("--load", type=lambda s: int(s, 0), help="a bare image's load address")
    ap.add_argument("--lbl", help="ld65's -Ln labels, for --inline")
    ap.add_argument("--inline", default="", help="routines whose JSR is followed by a data byte")
    a = ap.parse_args()
    data = open(a.image, "rb").read()
    if data[:4] == MAGIC:
        load = data[5] << 8
        data = data[HEADER:HEADER + int.from_bytes(data[6:8], "little")]
    elif a.load is None:
        ap.error("%s has no OODOS header: give --load" % a.image)
    else:
        load = a.load
    inline = ()
    if a.inline:
        if not a.lbl:
            ap.error("--inline needs --lbl")
        lbl = labels(a.lbl)
        inline = tuple(lbl[n] for n in a.inline.split(","))
    code = a.code.split(",")
    count, problems = check(data, load, segments(a.map), code, inline)
    for p in problems:
        print("%s: %s" % (a.image, p))
    print("OODOS rules: %d instructions in %s, %d problems" % (count, ", ".join(code), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
