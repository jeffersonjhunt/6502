"""Checks the spike's code against OODOS's rules for programs (hw/oodos.md).

    python3 -I rules.py spike.bin spike.map spike.lbl

Disassembles every translated code segment (from the map) and fails on:

  rule 1   RMB, SMB, BBR or BBS (opcodes $x7 and $xF);
  rule 2   an absolute operand in zero page ($0000-$00FF): zero page only
           through zero page operands;
  rule 3   an absolute operand in the stack page ($0100-$01FF);
  and      an opcode the code shouldn't have (an undocumented NOP), which
           would mean data in a code segment.

Rule 2's other half, no 16-bit pointers to zero page, holds by construction:
numbers in zero page are passed by their address in X, and PTR only ever
points into memory (gwbasic.inc). Rule 4 (the BDOS only through $0203) is
checked too: a JSR or JMP outside the image may only go to $0203 (the BDOS)
or $0200 (warm boot). Indirect JMPs aren't followed. The harness, stubs and
FARCALL are test scaffolding and aren't checked: these are the translated
segments only.
"""

import re
import sys

CODE = ["GWMAIN", "GWEVAL", "BIMISC", "MATH1", "MATH2", "RUNTIME"]
LOAD = 0x0300

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


def main():
    image, mapfile, lblfile = sys.argv[1:4]
    data = open(image, "rb").read()
    segs = {}
    for l in open(mapfile):
        m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", l)
        if m:
            segs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    labels = {}
    for l in open(lblfile):
        m = re.match(r"al ([0-9A-F]+) \.(\w+)", l)
        if m:
            labels[m.group(2)] = int(m.group(1), 16)
    synchr = labels["SYNCHR"]
    bad = 0
    count = 0
    for name in CODE:
        start, end = segs[name]
        pc = start
        while pc <= end:
            op = data[pc - LOAD]
            mode = MODES[op]
            n = LEN[mode]
            operand = int.from_bytes(data[pc - LOAD + 1:pc - LOAD + n], "little") if n > 1 else None
            why = None
            if (op & 0x07) == 0x07:
                why = "RMB/SMB/BBR/BBS (rule 1)"
            elif mode == "bad" or op in NOPS:
                why = "not an instruction the code uses: data in code?"
            elif mode in ABSOLUTE and operand < 0x100:
                why = "absolute zero page operand (rule 2)"
            elif mode in ABSOLUTE and operand < 0x200:
                why = "absolute stack page operand (rule 3)"
            elif op in (0x20, 0x4C) and not (LOAD <= operand < LOAD + len(data)) and operand not in OS:
                why = "%s outside the program, not the BDOS (rule 4)" % ("JSR" if op == 0x20 else "JMP")
            if why:
                print("%s $%04X: opcode $%02X %s" % (name, pc, op, why))
                bad += 1
            count += 1
            pc += n
            if op == 0x20 and operand == synchr:
                pc += 1  # SYNCHR's inline byte
        if pc != end + 1:
            print("%s: disassembly ran past the segment's end" % name)
            bad += 1
    print("OODOS rules: %d instructions in %s, %d problems" % (count, ", ".join(CODE), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
