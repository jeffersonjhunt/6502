"""The spike's sizes: the 6502 translation against Microsoft's 8086 code.

    python3 -I sizes.py spike.map GWSRC [--routines]

GWSRC is a clone of microsoft/GW-BASIC (commit edf82c2); it is read as text
only. There is no 8086 assembler here, so each instruction's length comes
from the 8086's encoding rules (modrm, displacement and immediate sizes,
the accumulator and sign-extended short forms, short jumps where MASM
would make them), with INS86's raw bytes, the code macros expanded, and
conditional assembly evaluated. DB and DW in the code segment are counted
as data, apart from code. Data-segment (RAM) declarations are summed too:
they are what GW-BASIC needs in the TPA besides its code.

MEASURED lists the 8086 source the spike translated, routine by routine;
the 6502 side is the map's segment sizes. The ratio of the two, per module
and overall, scales each whole module's 8086 size to a 6502 estimate.
"""

import os
import re
import sys

# --- What the spike translated: (file, first label, label after the last line),
# and lines inside that weren't (the double paths, strings, VARPTR...).
MEASURED = [
    ("MATH1", "$FOTCI", "$FOUTO", []),
    ("MATH1", "$FOTZS", "$FOTZ", []),
    ("MATH1", "MDPTEN", "$OVFLS", []),
    ("MATH1", "$OVFLS", "$FOFMT", []),
    ("MATH1", "$FOFMT", "STOULP", []),
    ("MATH1", "STOULP", "$DADDH", []),
    ("MATH1", "$FADDH", "$RDL", []),
    ("MATH1", "$MOVAC", "$MOVAF", []),
    ("MATH1", "$MOVFR", "$VCOMP", []),
    ("MATH1", "$FCOMP", "$DCMPA", []),
    ("MATH1", "$RSUBM", "$VMVAM", []),
    ("MATH1", "$VMOVM", "$POPA", []),
    ("MATH1", "$VPOPF", "FIN", []),
    ("MATH1", "$CHRGT", "DSUB", []),
    ("MATH1", "FADD", "FLOAT", []),
    ("MATH1", "FMULT", "FPWR", []),
    ("MATH1", "ISUB", "MOVFM", []),
    ("MATH1", "MOVRF", "MOVMF", []),
    ("MATH1", "PUSHF", "RNDMON", []),
    ("MATH1", "VMOVFM", "VMOVMF", []),
    ("MATH1", "VMVVFM", "BLTU", []),
    ("MATH1", "VSIGN", "ISIGN", []),
    ("MATH1", "CONIA", "FPWRQ", []),
    ("MATH1", "LINPRT", "SIGNC", []),
    ("MATH1", "NEGR", "LOPTOP", []),
    ("MATH1", "$AEXPS", "$CHKEL", []),
    ("MATH1", "EXIT1", "$FDIVD", []),
    ("MATH1", "$DDIV", "SDIV", []),
    ("MATH1", "FEXIT1", "DDIV", []),
    ("MATH1", "$FDIVS", None, []),
    ("MATH2", "FRCSNG", "$FLT", []),
    ("MATH2", "$FLT", "$FMULD", []),
    ("MATH2", "DMULT", "$FMULS", []),
    ("MATH2", "$FMULS", "$FOTAN", []),
    ("MATH2", "$FOTAN", "$FOTCV", []),
    ("MATH2", "$FOTCV", "DSUBI", [("PUSH\tCX\t\t;SAVE DIGIT AND COMMA COUNT", "FCV40")]),
    ("MATH2", "$FOTNV", "HIDBL", [("MOV\tBX,OFFSET $DP09", "FNV40")]),
    ("MATH2", "S", "$INT", []),
    ("MATH2", "INT", "$LOG", []),
    ("MATH2", "$NORMS", "$POLY", []),
    ("MATH2", "$ROUND", "$ROUNS", []),
    ("MATH2", "$ROUNS", "$SIGD", []),
    ("MATH2", "$SIGD", None, []),
    ("GWMAIN", "GOPRGD", "CHRGTR", []),
    ("GWMAIN", "CHRGTR", "DEFSTR SUBTTL", []),
    ("GWMAIN", "FNDLIN", "PRE FAST CRUNCH SUBTTL", []),
    ("GWMAIN", "LINGET", "RUN SUBTTL", []),
    ("GWMAIN", "GOTO", "RETURN", []),
    ("GWMAIN", "DATA", "LET SUBTTL", []),
    ("GWMAIN", "LET", "ON..GOTO SUBTTL", [("MOV\tBX,FACLO\t;GET POINTER TO THE DESCRIPTOR", "COPNUM")]),
    ("GWMAIN", "IFS", "PRINT SUBTTL", []),
    ("GWMAIN", "CONCH2", "DEPTR", []),
    ("GWEVAL", "FRMEQL", "EVAL SUBTTL", []),
    ("GWEVAL", "EVAL", "MAKUPL", [("CALL\tCHRGTR\t\t;EAT CHAR AFTER", "NTVARP"),
                                  ("CMP\tAL,LOW OFFSET $SCREEN", "PARCHK")]),
    ("GWEVAL", "ISFUN", "MINPLS", []),
    ("GWEVAL", "DOCMP", "GIVDBL", []),
    ("GWEVAL", "SNGFLT", "USER DEFINED SUBTTL", []),
    ("GWEVAL", "DOCNVF", "BCTRAL", []),
    ("BIMISC", "GETSTK", "OMERR", []),
    ("BIMISC", "SYNCHR", "SUBTTL TRAP", []),
    ("BIMISC", "ISLET2", "SUBTTL CLEAR", []),
]

# The resident candidates: what runs for every statement, operator and
# variable (README.md, "Layout"). Whole sections, so the cold parts that
# share them (RUN, ON ERROR's parts of the storage code) are counted too.
HOT = [
    ("GWMAIN", "GENERAL STORAGE MANAGEMENT SUBTTL", "ERROR HANDLING SUBTTL"),
    ("GWMAIN", "FNDLIN", "PRE FAST CRUNCH SUBTTL"),
    ("GWMAIN", "\"FOR\" STATEMENT SUBTTL", "DEFSTR SUBTTL"),
    ("GWMAIN", "LINSPC SUBTTL", "ON..GOTO SUBTTL"),
    ("GWMAIN", "IF ... THEN SUBTTL", "PRINT CODE SUBTTL"),
    ("GWEVAL", "FORMULA EVALUATION CODE SUBTTL", "USER DEFINED SUBTTL"),
    ("BIPTRG", "DIMENSION & VARIABLE SUBTTL", None),
    ("NEXT86", "NEXT:", None),
    ("BISTRS", "STRING FUNCTIONS SUBTTL", "STRING FUNCTIONS - LEN SUBTTL"),
    ("BIMISC", "GETSTK", "NODSKS SUBTTL"),
    ("BIMISC", "DCOMPR SUBTTL", "TRAP ROUTINES SUBTTL"),
    ("BIMISC", "ISLET", "CLEAR SUBTTL"),
]
# The math core among what the spike translated: everything but output,
# doubles and the powers of ten.
HOT_MATH = [
    ("MATH1", "$FADDH", "$DIV10"),
    ("MATH1", "STROUT", "$DADDH"),
    ("MATH1", "$MOVFR", "$VCOMP"),
    ("MATH1", "$FCOMP", "$DCMPA"),
    ("MATH1", "ABSFN", "$SETDB"),
    ("MATH1", "$VMOVM", "$POPA"),
    ("MATH1", "$VPOPF", "FIN"),
    ("MATH1", "FADD", "FLOAT"),
    ("MATH1", "FMULT", "FPWR"),
    ("MATH1", "ISUB", "MOVFM"),
    ("MATH1", "MOVRF", "MOVMF"),
    ("MATH1", "PUSHF", "RNDMON"),
    ("MATH1", "VMOVFM", "VMOVMF"),
    ("MATH1", "VMVVFM", "BLTU"),
    ("MATH1", "VSIGN", "ISIGN"),
    ("MATH1", "CONIA", "FPWRQ"),
    ("MATH1", "NEGR", "LOPTOP"),
    ("MATH1", "$AEXPS", "$CHKEL"),
    ("MATH1", "FEXIT1", "DDIV"),
    ("MATH1", "$FDIVS", None),
    ("MATH1", "LOPTOP", "$AEXPS SUBTTL"),
    ("MATH2", "FRCSNG", "$FMULD"),
    ("MATH2", "$FMULS", "$FOTAN"),
    ("MATH2", "INT", "$LOG"),
    ("MATH2", "$NORMS", "$POLY"),
    ("MATH2", "$ROUNS", "$SIGD"),
]

# The 6502 segments for each module (the map's names), code and data.
SEGMENTS = {
    "MATH1": (["MATH1"], ["MATH1D"]),
    "MATH2": (["MATH2"], []),
    "GWMAIN": (["GWMAIN"], []),
    "GWEVAL": (["GWEVAL"], []),
    "BIMISC": (["BIMISC"], []),
}

FILES = ["ADVGRP", "BIBOOT", "BIMISC", "BIPRTU", "BIPTRG", "BISTRS", "CALL86", "DSKCOM", "FIVEO", "GENGRP",
         "GIO86", "GIOCAS", "GIOCOM", "GIOCON", "GIODSK", "GIOKYB", "GIOLPT", "GIOSCN", "GIOTBL", "GWDATA",
         "GWEVAL", "GWINIT", "GWLIST", "GWMAIN", "GWRAM", "GWSTS", "IBMRES", "ITSA86", "KANJ86", "MACLNG",
         "MATH1", "MATH2", "NEXT86", "SCNDRV", "SCNEDT"]

R8 = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"}
R16 = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"}
SREG = {"ES", "CS", "SS", "DS"}
JCC = {"JZ", "JNZ", "JE", "JNE", "JB", "JNB", "JAE", "JNAE", "JC", "JNC", "JA", "JNA", "JBE", "JNBE", "JS", "JNS",
       "JO", "JNO", "JP", "JNP", "JPE", "JPO", "JL", "JNL", "JGE", "JNGE", "JLE", "JNLE", "JG", "JNG", "JCXZ",
       "LOOP", "LOOPZ", "LOOPE", "LOOPNZ", "LOOPNE"}
ONE = {"RET", "PUSHF", "POPF", "SAHF", "LAHF", "CLD", "STD", "CLC", "STC", "CMC", "CLI", "STI", "CBW", "CWD",
       "STOSB", "STOSW", "LODSB", "LODSW", "MOVSB", "MOVSW", "CMPSB", "CMPSW", "SCASB", "SCASW", "XLAT", "IRET",
       "NOP", "HLT", "INTO", "AAA", "AAS", "DAA", "DAS", "REP", "REPE", "REPZ", "REPNE", "REPNZ", "LOCK", "WAIT",
       "RETF", "PUSHA", "POPA"}
ALU = {"ADD", "ADC", "SUB", "SBB", "AND", "OR", "XOR", "CMP"}
UNARY = {"INC", "DEC", "NEG", "NOT", "MUL", "IMUL", "DIV", "IDIV"}
SHIFT = {"SHL", "SAL", "SHR", "SAR", "ROL", "ROR", "RCL", "RCR"}
# SCNDRV's byte and immediate forms, as plain instructions.
ALIAS = {"MOVB": "MOV", "INCB": "INC", "DECB": "DEC", "ADDB": "ADD", "SUBB": "SUB", "XORB": "XOR", "CMPB": "CMP",
         "MOVBI": "MOV", "MOVI": "MOV", "CMPI": "CMP"}
# Code macros: their expansions' bytes.
MACRO_BYTES = {"DERMAK": 3, "MOVRI": 6, "HLFDE": 2, "HLFHL": 2, "NEGDE": 2, "NEGHL": 2, "DOSIO": 4, "POPR": 2,
               "PUSHR": 2, "SYNCHK": 4, "FSIGN": 3, "CHRGET": 3, "OUTCHR": 3, "GETYPE": 3, "COMPAR": 2}
DATA_MACROS = {"ADR": 2, "R": 2, "R2": 2, "ACRLF": 2}
SKIP = {"PUBLIC", "EXTRN", "ASSUME", "SUBTTL", "TITLE", "PAGE", "INCLUDE", ".XLIST", ".LIST", ".SALL", ".RADIX",
        "END", "ORG", "T", "ADRP", "DCE", "EVEN", "%OUT", "NAME", "LABEL", "ENDM", "MACRO", "UN_DEF", "ADD_EXT",
        "DO_EXT", "EXPAND_MAC", "RINIT", "R1", "PDIRAM", "PDURAM", "NAMES", "NDEV", "IF1", "ENDIF", "IFN"}


def number(tok, radix):
    t = tok.upper()
    try:
        if re.fullmatch(r"[0-9][0-9A-F]*H", t):
            return int(t[:-1], 16)
        if re.fullmatch(r"[0-7]+[OQ]", t):
            return int(t[:-1], 8)
        if re.fullmatch(r"[0-9]+D", t) and radix != 16:
            return int(t[:-1], 10)
        if re.fullmatch(r"[01]+B", t) and radix != 16:
            return int(t[:-1], 2)
        if re.fullmatch(r"[0-9A-F]+", t) and t[0].isdigit():
            return int(t, radix)
    except ValueError:
        return None
    return None


class Source:
    """One file's lines, comments and COMMENT blocks removed, CR stripped."""

    def __init__(self, path):
        raw = open(path, encoding="latin-1").read().replace("\r", "").split("\n")
        self.lines = []
        in_comment = None
        for l in raw:
            if in_comment:
                if in_comment in l:
                    in_comment = None
                self.lines.append("")
                continue
            m = re.match(r"\s*COMMENT\s+(\S)", l, re.I)
            if m:
                rest = l[m.end():]
                if m.group(1) not in rest:
                    in_comment = m.group(1)
                self.lines.append("")
                continue
            self.lines.append(strip_comment(l))
        self.raw = raw


def strip_comment(l):
    out, q = "", None
    for c in l:
        if q:
            out += c
            if c == q:
                q = None
        elif c in "'\"":
            q = c
            out += c
        elif c == ";":
            break
        else:
            out += c
    return out.rstrip()


def split_ops(s):
    ops, cur, q, depth = [], "", None, 0
    for c in s:
        if q:
            cur += c
            if c == q:
                q = None
            continue
        if c in "'\"":
            q = c
        elif c in "([<":
            depth += 1
        elif c in ")]>":
            depth -= 1
        if c == "," and depth == 0:
            ops.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        ops.append(cur.strip())
    return ops


class Estimator:
    def __init__(self, src_dir):
        self.dir = src_dir
        self.consts = {"CPM86": 0, "IBMTOK": 1, "IBMLIK": 1, "DBLCHR": 0, "GW": 1, "MELCO": 0, "TETRA": 0,
                       "MCI": 0, "SIRIUS": 0}
        self.consts["PWR2PX"] = -1
        self.data_syms = set()
        self.code_syms = set()

    # -- operands
    def kind(self, op):
        u = op.upper().strip()
        if u in R8:
            return "r8"
        if u in R16:
            return "r16"
        if u in SREG:
            return "sreg"
        if "[" in u or "PTR" in u:
            return "mem"
        if u.startswith("OFFSET") or u.startswith("LOW") or u.startswith("HIGH") or u.startswith("SHORT"):
            return "imm"
        if re.match(r"^[-+(]*[0-9]", u) or u.startswith("'") or u.startswith('"'):
            return "imm"
        name = re.match(r"[A-Z0-9_$?@.&]+", u)
        if name:
            n = name.group(0)
            if n in self.consts:
                return "imm"
            if n in self.code_syms and n not in self.data_syms:
                return "imm"
        return "mem"

    def mem_bytes(self, op):
        """modrm's displacement bytes, and a segment override prefix."""
        u = op.upper()
        pre = 1 if re.search(r"\b(CS|ES|SS|DS):", u) else 0
        u = re.sub(r"\b(BYTE|WORD|DWORD)\s+PTR\b", "", u)
        u = re.sub(r"\b(CS|ES|SS|DS):", "", u).strip()
        regs = re.findall(r"\b(BX|BP|SI|DI)\b", u)
        if not regs:
            return 2, pre
        disp = re.sub(r"\[[^\]]*\]", "", u).strip()
        inner = re.findall(r"\[([^\]]*)\]", u)
        extra = [x for x in re.split(r"[+]", "".join(inner)) if x.strip() and x.strip() not in ("BX", "BP", "SI", "DI")]
        dtext = (disp + "+" + "+".join(extra)).strip("+ ")
        if not dtext:
            return (1 if regs == ["BP"] else 0), pre
        v = self.value(dtext)
        if v is None:
            return 2, pre
        if v == 0:
            return (1 if regs == ["BP"] else 0), pre
        return (1 if -128 <= v <= 127 else 2), pre

    def value(self, expr):
        e = expr.upper().replace("OFFSET", "").replace("LOW", "").strip()
        toks = re.findall(r"[A-Z0-9_$?@]+|[-+*/()]", e)
        out = ""
        for t in toks:
            if t in "+-*/()":
                out += t
                continue
            n = number(t, self.radix)
            if n is None:
                n = self.consts.get(t)
            if n is None:
                return None
            out += str(n)
        try:
            return int(eval(out, {"__builtins__": {}}))  # noqa: S307 - our own digits and operators
        except Exception:
            return None

    def width(self, ops):
        for o in ops:
            k = self.kind(o)
            if k == "r8":
                return 8
            if k == "r16" or k == "sreg":
                return 16
        for o in ops:
            if "BYTE PTR" in o.upper():
                return 8
            if "WORD PTR" in o.upper():
                return 16
        return 16

    def imm_bytes(self, op, w, sext):
        if w == 8:
            return 1
        v = self.value(op)
        if sext and v is not None and -128 <= v <= 127:
            return 1
        return 2

    # -- an instruction's length
    def insn(self, mn, ops, here, labels):
        mn = ALIAS.get(mn, mn)
        k = [self.kind(o) for o in ops]
        if mn in ONE:
            return 1
        if mn in ("MOVS", "LODS", "STOS", "CMPS", "SCAS"):
            return 2 if any("?CSLAB" in o.upper() or "CS:" in o.upper() for o in ops) and mn != "STOS" else 1
        if mn == "XLAT":
            return 2
        if mn in JCC:
            return 2
        if mn == "JMP":
            o = ops[0].upper()
            if o.startswith("SHORT"):
                return 2
            if k[0] in ("r16",):
                return 2
            if "PTR" in o or "[" in o:
                d, p = self.mem_bytes(ops[0])
                return 2 + d + p
            t = labels.get(o.strip())
            if t is not None and t <= here and here - t < 126:
                return 2
            return 3
        if mn == "CALL":
            if k[0] == "r16":
                return 2
            if "PTR" in ops[0].upper() or "[" in ops[0]:
                d, p = self.mem_bytes(ops[0])
                return 2 + d + p
            return 3
        if mn == "RET":
            return 1 if not ops else 3
        if mn in ("PUSH", "POP"):
            if k[0] in ("r16", "sreg"):
                return 1
            d, p = self.mem_bytes(ops[0])
            return 2 + d + p
        if mn == "INT":
            return 1 if ops[0].strip() == "3" else 2
        if mn in ("IN", "OUT"):
            return 1 if "DX" in [o.upper() for o in ops] else 2
        if mn in ("AAM", "AAD"):
            return 2
        if mn in ("LEA", "LES", "LDS"):
            d, p = self.mem_bytes(ops[1])
            return 2 + d + p
        if mn == "XCHG":
            if set(k) == {"r16"} and "AX" in [o.upper() for o in ops]:
                return 1
            if k[0] == k[1] and k[0] in ("r8", "r16"):
                return 2
            m = ops[0] if k[0] == "mem" else ops[1]
            d, p = self.mem_bytes(m)
            return 2 + d + p
        if mn in UNARY:
            if k[0] == "r16" and mn in ("INC", "DEC"):
                return 1
            if k[0] in ("r8", "r16"):
                return 2
            d, p = self.mem_bytes(ops[0])
            return 2 + d + p
        if mn in SHIFT:
            if k[0] in ("r8", "r16"):
                return 2
            d, p = self.mem_bytes(ops[0])
            return 2 + d + p
        if mn == "MOV":
            a, b = k
            w = self.width(ops)
            if a in ("r8", "r16") and b in ("r8", "r16", "sreg"):
                return 2
            if a == "sreg" or b == "sreg":
                m = ops[1] if a == "sreg" else ops[0]
                if self.kind(m) in ("r16",):
                    return 2
                d, p = self.mem_bytes(m)
                return 2 + d + p
            if a in ("r8", "r16") and b == "imm":
                return 2 if a == "r8" else 3
            if a in ("r8", "r16") and b == "mem":
                d, p = self.mem_bytes(ops[1])
                if ops[0].upper() in ("AL", "AX") and d == 2 and not re.search(r"\b(BX|BP|SI|DI)\b", ops[1].upper()):
                    return 3 + p
                return 2 + d + p
            if a == "mem" and b in ("r8", "r16"):
                d, p = self.mem_bytes(ops[0])
                if ops[1].upper() in ("AL", "AX") and d == 2 and not re.search(r"\b(BX|BP|SI|DI)\b", ops[0].upper()):
                    return 3 + p
                return 2 + d + p
            if a == "mem" and b == "imm":
                d, p = self.mem_bytes(ops[0])
                return 2 + d + p + (1 if w == 8 else 2)
            if a == "mem" and b == "mem":  # a constant MASM took for memory
                d, p = self.mem_bytes(ops[0])
                return 2 + d + p + (1 if w == 8 else 2)
            return 3
        if mn in ALU or mn == "TEST":
            a, b = k[0], k[1] if len(k) > 1 else "imm"
            w = self.width(ops)
            if b == "mem" and a in ("r8", "r16") and self.value(ops[1]) is not None:
                b = "imm"
            if a in ("r8", "r16") and b in ("r8", "r16"):
                return 2
            if b == "imm":
                if ops[0].upper() in ("AL", "AX"):
                    return 1 + (1 if w == 8 else 2)
                if a in ("r8", "r16"):
                    return 2 + self.imm_bytes(ops[1], w, mn != "TEST")
                d, p = self.mem_bytes(ops[0])
                return 2 + d + p + self.imm_bytes(ops[1], w, mn != "TEST")
            m = ops[1] if b == "mem" else ops[0]
            d, p = self.mem_bytes(m)
            return 2 + d + p
        return None

    def data_bytes(self, mn, rest):
        if mn in ("DB", "DW", "DD", "DT"):
            size = {"DB": 1, "DW": 2, "DD": 4, "DT": 10}[mn]
            total = 0
            for o in split_ops(rest):
                m = re.match(r"(.+?)\s+DUP\s*\(", o, re.I)
                if m:
                    n = self.value(m.group(1))
                    total += (n if n is not None else 1) * size
                elif (o.startswith('"') or o.startswith("'")) and mn == "DB":
                    total += max(1, len(o) - 2)
                else:
                    total += size
            return total
        return None

    # -- a file
    def scan(self, name, ranges=None):
        """(code, cdata, dsdata, insns, per-range code) for a file, two passes."""
        src = Source(os.path.join(self.dir, name + ".ASM"))
        self.collect(src)
        labels = {}
        for _ in range(2):
            res = self.pass_(src, labels, ranges)
        return res

    def collect(self, src):
        self.radix = 8
        for l in src.lines:
            m = re.match(r"\s*\.RADIX\s+(\S+)", l, re.I)
            if m:
                self.radix = int(m.group(1))
            m = re.match(r"\s*EXTRN\s+(.*)", l, re.I)
            if m:
                for d in m.group(1).split(","):
                    if ":" in d:
                        n, t = d.split(":", 1)
                        (self.code_syms if t.strip().upper() in ("NEAR", "FAR") else self.data_syms).add(
                            n.strip().upper())
            m = re.match(r"\s*([A-Za-z0-9_$?@]+)\s*(=|EQU\s)\s*(.+)$", l, re.I)
            if m:
                v = self.value(m.group(3))
                if v is not None:
                    self.consts[m.group(1).upper()] = v
            m = re.match(r"\s*([A-Za-z0-9_$?@]+)\s+(LABEL\s+(WORD|BYTE)|DB|DW)\b", l, re.I)
            if m:
                self.data_syms.add(m.group(1).upper())
            m = re.match(r"([A-Za-z0-9_$?@]+):", l)
            if m:
                self.code_syms.add(m.group(1).upper())

    def pass_(self, src, labels, ranges):
        self.radix = 8
        seg = None
        pos = 0
        code = cdata = dsdata = insns = 0
        cond = []
        per = {}
        unknown = {}
        for i, l in enumerate(src.lines):
            if not l.strip():
                continue
            u = l.upper()
            m = re.match(r"\s*\.RADIX\s+(\S+)", u)
            if m:
                self.radix = int(m.group(1))
                continue
            m = re.match(r"\s*(\w+)\s+SEGMENT\b", u)
            if m:
                seg = m.group(1)
                continue
            m = re.match(r"\s*(\w+)\s+ENDS\b", u)
            if m:
                seg = "CSEG" if m.group(1) != "CSEG" else None
                continue
            m = re.match(r"\s*(IF|IFE|IFDEF|IFNDEF|IFB|IFNB)\s+(.*)", u)
            if m:
                v = self.value(m.group(2).replace(" OR ", "|").replace(" AND ", "&"))
                if m.group(1) == "IF":
                    cond.append(bool(v) if v is not None else True)
                elif m.group(1) == "IFE":
                    cond.append(not v if v is not None else True)
                else:
                    cond.append(True)
                continue
            if re.match(r"\s*ENDIF\b", u):
                if cond:
                    cond.pop()
                continue
            if re.match(r"\s*ELSE\b", u) and cond:
                cond[-1] = not cond[-1]
                continue
            if not all(cond):
                continue
            body = l
            m = re.match(r"([A-Za-z0-9_$?@.]+):\s*", body)
            if m:
                labels[m.group(1).upper()] = pos
                body = body[m.end():]
            if not body.strip():
                continue
            parts = body.split(None, 1)
            mn = parts[0].upper()
            rest = parts[1] if len(parts) > 1 else ""
            # NAME DB ... / NAME LABEL ...
            if len(parts) > 1 and rest.split(None, 1)[0].upper() in ("DB", "DW", "DD", "LABEL", "EQU", "=", "MACRO"):
                labels[mn] = pos
                p2 = rest.split(None, 1)
                mn = p2[0].upper()
                rest = p2[1] if len(p2) > 1 else ""
            if "=" in mn or mn in SKIP or mn.startswith("$") and "=" in body:
                continue
            if re.match(r"\s*[A-Za-z0-9_$?@]+\s*=", body):
                continue
            size = None
            kind = "code"
            if mn == "INS86":
                size = sum(1 if number(o.strip(), self.radix) is not None else 2 for o in split_ops(rest))
            elif mn in MACRO_BYTES:
                size = MACRO_BYTES[mn]
                if mn == "DOSIO" and not rest:
                    size = 2
            elif mn == "CALLOS":
                size = 4 if rest.strip() else 2
            elif mn in DATA_MACROS:
                size, kind = DATA_MACROS[mn], "data"
            elif mn in ("DCL", "DC", "DCUL"):
                size, kind = max(1, len(rest.strip()) - 2) + (1 if mn == "DCL" else 0), "data"
            elif mn in ("Q", "QF"):
                size, kind = len(rest.strip()), "data"
            elif mn == "DEV":
                size, kind = 2, "data"
            elif mn in ("REPT", "IRP", "IRPC", "MACRO"):
                continue
            else:
                d = self.data_bytes(mn, rest)
                if d is not None:
                    size, kind = d, "data"
                else:
                    pre = 0
                    if mn in ("REP", "REPE", "REPZ", "REPNE", "REPNZ", "LOCK") and rest:
                        pre = 1
                        p2 = rest.split(None, 1)
                        mn = p2[0].upper()
                        rest = p2[1] if len(p2) > 1 else ""
                    ops = split_ops(rest)
                    try:
                        s = self.insn(mn, ops, pos, labels)
                    except (IndexError, ValueError):
                        s = None
                    if s is None:
                        unknown[mn] = unknown.get(mn, 0) + 1
                        continue
                    size = s + pre
            if seg == "CSEG" or seg is None:
                if kind == "data":
                    cdata += size
                else:
                    code += size
                    insns += 1
                if ranges is not None:
                    r = ranges(i)
                    if r is not None:
                        per.setdefault(r, [0, 0, 0])
                        per[r][0 if kind == "code" else 1] += size
                        if kind == "code":
                            per[r][2] += 1
                pos += size
            else:
                dsdata += size
        self.unknown = unknown
        return code, cdata, dsdata, insns, per


def find_line(src, marker):
    """The line a marker starts: a label (NAME:), a 'TEXT SUBTTL', or text."""
    if marker.endswith(" SUBTTL"):
        want = marker[:-7].upper()
        for i, l in enumerate(src.raw):
            if re.match(r"\s*SUBTTL\s+\"?" + re.escape(want), l.upper()):
                return i
    if marker.startswith("SUBTTL "):
        want = marker[7:].upper()
        for i, l in enumerate(src.raw):
            if re.match(r"\s*SUBTTL\s+\"?" + re.escape(want), l.upper()):
                return i
    for i, l in enumerate(src.raw):
        if re.match(re.escape(marker) + r"\s*:", l) or re.match(re.escape(marker) + r"\s*$", l):
            return i
    for i, l in enumerate(src.raw):
        if marker in l.replace("    ", "\t"):
            return i
    raise ValueError("no line for %s" % marker)


def declared_ram(gwsrc):
    """GW-BASIC's fixed RAM as declared: GWRAM's R, R1 and RINIT variables and
    the data segments' DB/DW/DD (with DUP counts), constants resolved from
    every file and header."""
    names = [f for f in os.listdir(gwsrc) if f.endswith((".ASM", ".H")) or f in ("GIO86U", "MSDOSU")]
    consts = {}

    def val(e, radix):
        out = ""
        for t in re.findall(r"[A-Za-z0-9_$?@]+|[-+*/()]", e):
            if t in "+-*/()":
                out += t
                continue
            n = number(t, radix)
            if n is None:
                n = consts.get(t.upper())
            if n is None:
                return None
            out += str(n)
        try:
            return int(eval(out, {"__builtins__": {}}))  # noqa: S307 - our own digits and operators
        except Exception:
            return None

    for _ in range(4):
        for f in names:
            radix = 8
            for l in open(os.path.join(gwsrc, f), encoding="latin-1"):
                l = l.replace("\r", "").split(";")[0]
                m = re.match(r"\s*\.RADIX\s+(\d+)", l, re.I)
                if m:
                    radix = int(m.group(1))
                m = re.match(r"\s*([A-Za-z0-9_$?@]+)\s*=\s*(.+)$", l)
                if m and m.group(1).upper() not in consts:
                    v = val(m.group(2), radix)
                    if v is not None:
                        consts[m.group(1).upper()] = v
    macro_vars = 0
    radix = 8
    for l in open(os.path.join(gwsrc, "GWRAM.ASM"), encoding="latin-1"):
        l = l.replace("\r", "").split(";")[0]
        m = re.match(r"\s*\.RADIX\s+(\d+)", l, re.I)
        if m:
            radix = int(m.group(1))
        m = re.match(r"\s*(R|R1|RINIT)\s+[A-Za-z0-9_$?@]+\s*,\s*(.+)$", l)
        if m:
            macro_vars += val(m.group(2), radix) or 0
    dseg = 0
    for f in names:
        if not f.endswith(".ASM"):
            continue
        seg, radix = None, 8
        for l in open(os.path.join(gwsrc, f), encoding="latin-1"):
            l = l.replace("\r", "").split(";")[0]
            m = re.match(r"\s*\.RADIX\s+(\d+)", l, re.I)
            if m:
                radix = int(m.group(1))
            m = re.match(r"\s*(\w+)\s+(SEGMENT|ENDS)", l, re.I)
            if m:
                seg = m.group(1).upper() if m.group(2).upper() == "SEGMENT" else None
                continue
            m = re.search(r"\b(DB|DW|DD)\s+(.+)$", l, re.I) if seg == "DSEG" else None
            if not m:
                continue
            unit = {"DB": 1, "DW": 2, "DD": 4}[m.group(1).upper()]
            for item in m.group(2).split(","):
                d = re.match(r"(.+?)\s+DUP\s*\(", item.strip(), re.I)
                if d:
                    dseg += (val(d.group(1), radix) or 0) * unit
                elif item.strip():
                    dseg += unit
    return macro_vars, dseg


def map_sizes(mapfile):
    segs = {}
    for l in open(mapfile):
        m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", l)
        if m:
            segs[m.group(1)] = int(m.group(4), 16)
    return segs


def main():
    mapfile, gwsrc = sys.argv[1], sys.argv[2]
    est = Estimator(gwsrc)
    # The measured ranges, as line sets per file.
    want = {}
    for f, a, b, excl in MEASURED:
        src = Source(os.path.join(gwsrc, f + ".ASM"))
        lo = find_line(src, a)
        hi = find_line(src, b) if b else len(src.raw)
        if hi <= lo:
            raise ValueError("%s: %s..%s is empty" % (f, a, b))
        lines = set(range(lo, hi))
        for x, y in excl:
            xl, yl = find_line(src, x), find_line(src, y)
            lines -= set(range(xl, yl))
        want.setdefault(f, set()).update(lines)

    hot = {}
    for f, a, b in HOT + HOT_MATH:
        src = Source(os.path.join(gwsrc, f + ".ASM"))
        lo = find_line(src, a)
        hi = find_line(src, b) if b else len(src.raw)
        hot.setdefault(f, set()).update(range(lo, hi))

    totals = {}
    measured = {}
    hotsize = {}
    for f in FILES:
        lines = want.get(f)
        H = hot.get(f, set())
        code, cdata, dsdata, insns, per = est.scan(
            f, (lambda i, L=lines, H=H: "m" if L and i in L else ("h" if i in H else None)))
        totals[f] = (code, cdata, dsdata, insns)
        hotsize[f] = per.get("h", [0, 0, 0])[0] + (per.get("m", [0, 0, 0])[0] if f in hot else 0)
        if f in hot and lines:  # the measured lines that are hot too
            code2 = est.scan(f, (lambda i, L=lines, H=H: "x" if i in H else None))[4]
            hotsize[f] = code2.get("x", [0, 0, 0])[0]
        if lines:
            measured[f] = per.get("m", [0, 0, 0])
    six = map_sizes(mapfile)
    rows = []
    t86 = t65 = 0
    for f in SEGMENTS:
        c86, d86, n86 = measured[f]
        code_segs, data_segs = SEGMENTS[f]
        c65 = sum(six[s] for s in code_segs)
        rows.append((f, n86, c86, c65, c65 / c86))
        t86 += c86
        t65 += c65
    runtime = six.get("RUNTIME", 0)
    print("Measured: the 8086 code the spike translated, and the 6502 code it became")
    print("  %-8s %6s %8s %8s %6s" % ("module", "insns", "8086 B", "6502 B", "ratio"))
    for f, n, a, b, r in rows:
        print("  %-8s %6d %8d %8d %6.2f" % (f, n, a, b, r))
    print("  %-8s %6s %8d %8d %6.2f" % ("all", "", t86, t65, t65 / t86))
    print("  plus the BASIC stack's runtime (no 8086 counterpart): %d B; with it %.2f" % (
        runtime, (t65 + runtime) / t86))
    print()
    print("Whole program, 8086 (estimated encodings): code, data in the code segment, RAM declared")
    tc = td = tr = 0
    for f in FILES:
        c, d, r, n = totals[f]
        tc += c
        td += d
        tr += r
        print("  %-8s %6d %6d %6d" % (f, c, d, r))
    print("  %-8s %6d %6d %6d" % ("total", tc, td, tr))
    print()
    math_r = sum(r[3] for r in rows if r[0].startswith("MATH")) / sum(r[2] for r in rows if r[0].startswith("MATH"))
    int_r = sum(r[3] for r in rows if not r[0].startswith("MATH")) / sum(r[2] for r in rows if not r[0].startswith("MATH"))
    worst = max(r[4] for r in rows)
    mc = totals["MATH1"][0] + totals["MATH2"][0]
    rest = tc - mc
    est65 = mc * math_r + rest * int_r
    hi65 = mc * math_r + rest * worst
    print("Extrapolation: math modules at %.2f, the rest at %.2f (%.2f, the worst module, for a high bound)" % (
        math_r, int_r, worst))
    print("  code: 8086 %d B -> 6502 %d B (high %d B); data in the code segment %d B, as is" % (
        tc, est65, hi65, td))
    hm = hotsize.get("MATH1", 0) + hotsize.get("MATH2", 0)
    hr = sum(v for k, v in hotsize.items() if not k.startswith("MATH"))
    print("Resident candidates: 8086 %d B (math %d, interpreter %d) -> 6502 %d B (high %d B)" % (
        hm + hr, hm, hr, hm * math_r + hr * int_r, hm * math_r + hr * worst))
    for k, v in sorted(hotsize.items()):
        if v:
            print("  %-8s %6d" % (k, v))
    mv, ds = declared_ram(gwsrc)
    print("RAM GW-BASIC declares: %d B of GWRAM variables, %d B in data segments: %d B" % (mv, ds, mv + ds))


if __name__ == "__main__":
    main()
