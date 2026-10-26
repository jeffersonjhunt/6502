"""A tiny GW-BASIC for checking the spike: tokeniser and reference reader.

tokenise(src) gives the bytes GW-BASIC keeps a program in (a zero, then
lines of link, line number, tokens and a zero, then a zero link) for the
statements and functions the spike has: PRINT (numbers), LET, IF THEN ELSE,
GOTO, REM, END; SGN INT ABS CINT CSNG FIX; + - * / \\ MOD, the relations,
NOT AND OR XOR EQV IMP. Numbers become GW-BASIC's embedded constants: 0-9 in
the token, then one byte, two bytes, or a single in MBF (Python's nearest
single: the spike has no FIN, so the program carries the bytes).

run(src) is what GW-BASIC prints for the same program, read the way its
interpreter reads it (FRMEVL's precedences and type rules, IF's skipping of
ELSE clauses) with mbf.py doing the arithmetic. It is the expected output
check.py compares the 6502's with.
"""

import re
from fractions import Fraction

import mbf

KEYWORDS = {"END": 0x81, "LET": 0x88, "GOTO": 0x89, "IF": 0x8B, "REM": 0x8F, "PRINT": 0x91, "ELSE": 0xA1,
            "THEN": 0xCD, "NOT": 0xD3, "AND": 0xEE, "OR": 0xEF, "XOR": 0xF0, "EQV": 0xF1, "IMP": 0xF2,
            "MOD": 0xF3}
FUNCS = {"SGN": 0x84, "INT": 0x85, "ABS": 0x86, "CINT": 0x9C, "CSNG": 0x9D, "FIX": 0x9F}
OPS = {">": 0xE6, "=": 0xE7, "<": 0xE8, "+": 0xE9, "-": 0xEA, "*": 0xEB, "/": 0xEC, "^": 0xED, "\\": 0xF4}
PREC = {"+": 121, "-": 121, "*": 124, "/": 124, "^": 127, "AND": 80, "OR": 70, "XOR": 60, "EQV": 50, "IMP": 40,
        "MOD": 122, "\\": 123}
BASE = 0x6100


def lex(line):
    """A line's text to tokens: (kind, value...)."""
    toks = []
    i = 0
    after_jump = False
    while i < len(line):
        c = line[i]
        if c == " ":
            toks.append(("ch", " "))
            i += 1
            continue
        m = re.match(r"[A-Z]+", line[i:])
        if m:
            w = m.group(0)
            for k in sorted(list(KEYWORDS) + list(FUNCS), key=len, reverse=True):
                if w.startswith(k):
                    if k in FUNCS:
                        toks.append(("fn", k))
                    else:
                        toks.append(("kw", k))
                    i += len(k)
                    after_jump = k in ("GOTO", "THEN", "ELSE")
                    if k == "REM":
                        toks.append(("rem", line[i:]))
                        return toks
                    break
            else:
                v = line[i]
                i += 1
                suf = ""
                if i < len(line) and line[i] in "%!":
                    suf = line[i]
                    i += 1
                toks.append(("var", v, suf))
                after_jump = False
            continue
        m = re.match(r"(\d+\.?\d*|\.\d+)(E[+-]?\d+)?", line[i:])
        if m:
            s = m.group(0)
            i += len(s)
            if after_jump:
                toks.append(("line", int(s)))
            elif re.fullmatch(r"\d+", s) and int(s) <= 32767:
                toks.append(("num", mbf.INT, int(s)))
            else:
                toks.append(("num", mbf.SNG, mbf.nearest(Fraction(s))))
            after_jump = False
            continue
        if c in OPS:
            toks.append(("op", c))
        else:
            toks.append(("ch", c))
        after_jump = False
        i += 1
    return toks


def parse(src):
    prog = []
    for l in src.strip("\n").split("\n"):
        m = re.match(r"(\d+)\s*(.*)", l.strip())
        prog.append((int(m.group(1)), lex(m.group(2))))
    return prog


def crunch(toks):
    out = bytearray()
    for t in toks:
        k = t[0]
        if k == "ch":
            out.append(ord(t[1]))
        elif k == "kw":
            if t[1] == "ELSE":
                out.append(ord(":"))
            out.append(KEYWORDS[t[1]])
        elif k == "fn":
            out += bytes([0xFF, FUNCS[t[1]]])
        elif k == "op":
            out.append(OPS[t[1]])
        elif k == "var":
            out += (t[1] + t[2]).encode()
        elif k == "rem":
            out += t[1].encode()
        elif k == "line":
            out += bytes([14, t[1] & 0xFF, t[1] >> 8])
        elif k == "num":
            if t[1] == mbf.INT:
                v = t[2]
                if v < 10:
                    out.append(17 + v)
                elif v < 256:
                    out += bytes([15, v])
                else:
                    out += bytes([28, v & 0xFF, v >> 8])
            else:
                out += bytes([29] + t[2])
    return out


def tokenise(src):
    out = bytearray([0])
    for n, toks in parse(src):
        body = bytes([n & 0xFF, n >> 8]) + crunch(toks) + b"\0"
        link = BASE + len(out) + 2 + len(body)
        out += bytes([link & 0xFF, link >> 8]) + body
    out += b"\0\0"
    return bytes(out)


# --- The reference reader ----------------------------------------------------------


class Stop(Exception):
    pass


class Interp:
    def __init__(self, prog):
        self.prog = prog
        self.out = []
        self.vars = {}
        self.line = None

    # Values are (INT, int) or (SNG, [4 bytes]).
    def to_sng(self, v):
        return v[1] if v[0] == mbf.SNG else mbf.flt(v[1])

    def to_int(self, v):
        return v[1] if v[0] == mbf.INT else self.guard(lambda: mbf.cint(v[1]))

    def guard(self, f):
        try:
            return f()
        except mbf.Overflow as o:
            self.out.append(o.msg + "\r\n")
            return o.result()
        except mbf.Error as e:
            self.error(e.code)

    def error(self, code):
        self.out.append("?ERR %s IN %s\r\n" % (mbf.fout(code, mbf.INT), mbf.fout(self.line, mbf.INT)))
        raise Stop()

    # -- expressions
    def peek(self):
        while self.pos < len(self.toks) and self.toks[self.pos] == ("ch", " "):
            self.pos += 1
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def take(self):
        t = self.peek()
        self.pos += 1
        return t

    def binop(self, t):
        if t is None:
            return None
        if t[0] == "op" and t[1] in "<>=":
            return "REL"
        if t[0] == "op" and t[1] in PREC:
            return t[1]
        if t[0] == "kw" and t[1] in PREC:
            return t[1]
        return None

    def expr(self, prec=0):
        left = self.unary()
        while True:
            t = self.peek()
            op = self.binop(t)
            if op is None:
                return left
            q = 100 if op == "REL" else PREC[op]
            if q <= prec:
                return left
            if op == "REL":
                bits = 0
                while True:
                    t = self.peek()
                    if not (t and t[0] == "op" and t[1] in "<>="):
                        break
                    self.take()
                    b = {">": 1, "=": 2, "<": 4}[t[1]]
                    if (bits ^ b) < bits:
                        self.error(2)
                    bits ^= b
                right = self.expr(100)
                left = self.compare(left, right, bits)
                continue
            self.take()
            if op in ("AND", "OR", "XOR", "EQV", "IMP", "MOD", "\\"):
                l = self.to_int(left)
                right = self.expr(q)
                r = self.to_int(right)
                left = self.logical(op, l, r)
                continue
            if op == "^":
                self.error(5)
            right = self.expr(q)
            left = self.arith(op, left, right)

    def compare(self, a, b, bits):
        if a[0] == mbf.INT and b[0] == mbf.INT:
            c = mbf.icomp(a[1], b[1])
        else:
            c = mbf.fcomp(self.to_sng(b), self.to_sng(a))  # FAC = right, BXDX = left
        c = {1: 4, 0: 2, -1: 1}[c]  # 1: left < right
        return (mbf.INT, -1 if c & bits else 0)

    def arith(self, op, a, b):
        if a[0] == mbf.INT and b[0] == mbf.INT:
            f = {"+": mbf.iadd, "-": mbf.isub, "*": mbf.imult}.get(op)
            if f:
                r = self.guard(lambda: f(a[1], b[1]))
                return (r[1], r[0]) if isinstance(r, tuple) else (mbf.SNG, r)
            return (mbf.SNG, self.guard(lambda: mbf.intdiv(a[1], b[1])))
        x, y = self.to_sng(a), self.to_sng(b)
        f = {"+": mbf.fadds, "-": mbf.fsubs, "*": mbf.fmuls, "/": mbf.fdivs}[op]
        return (mbf.SNG, self.guard(lambda: f(x, y)))

    def logical(self, op, l, r):
        if op == "MOD":
            res = self.guard(lambda: mbf.imod(l, r))
        elif op == "\\":
            res = self.guard(lambda: mbf.idiv(l, r))
        else:
            res = None
        if res is not None:
            if isinstance(res, tuple):
                return (res[1], res[0]) if res[1] == mbf.SNG else (mbf.INT, res[0])
            return (mbf.SNG, res)
        l &= 0xFFFF
        r &= 0xFFFF
        v = {"AND": l & r, "OR": l | r, "XOR": l ^ r, "EQV": ~(l ^ r), "IMP": ~(l & ~r)}[op]
        return (mbf.INT, mbf.i16(v))

    def unary(self):
        t = self.take()
        if t is None:
            self.error(22)
        if t[0] == "num":
            return (t[1], t[2])
        if t[0] == "var":
            return self.getvar(t)
        if t[0] == "op" and t[1] == "+":
            return self.unary()
        if t[0] == "op" and t[1] == "-":
            v = self.expr(125)
            if v[0] == mbf.INT:
                return (mbf.SNG, mbf.neg(mbf.flt(v[1]))) if v[1] == -32768 else (mbf.INT, -v[1])
            return (mbf.SNG, mbf.neg(v[1]))
        if t[0] == "kw" and t[1] == "NOT":
            v = self.to_int(self.expr(90))
            return (mbf.INT, mbf.i16(~v))
        if t[0] == "fn":
            self.expect("(")
            v = self.expr()
            self.expect(")")
            return self.func(t[1], v)
        if t == ("ch", "("):
            v = self.expr()
            self.expect(")")
            return v
        self.error(2)

    def expect(self, c):
        t = self.take()
        if t != ("ch", c):
            self.error(2)

    def func(self, name, v):
        S, I = mbf.SNG, mbf.INT
        if name == "SGN":
            if v[0] == I:
                return (I, (v[1] > 0) - (v[1] < 0))
            return (I, 0 if v[1][3] == 0 else (-1 if v[1][2] & 0x80 else 1))
        if name == "INT":
            return v if v[0] == I else (S, mbf.int_(v[1]))
        if name == "FIX":
            return v if v[0] == I else (S, mbf.fix(v[1]))
        if name == "ABS":
            if v[0] == I:
                return (S, mbf.neg(mbf.flt(v[1]))) if v[1] == -32768 else (I, abs(v[1]))
            return (S, v[1][:2] + [v[1][2] & 0x7F, v[1][3]])
        if name == "CINT":
            return (I, self.to_int(v))
        if name == "CSNG":
            return (S, self.to_sng(v))

    def getvar(self, t):
        typ = mbf.INT if t[2] == "%" else mbf.SNG
        return self.vars.get((t[1], typ), (typ, 0 if typ == mbf.INT else [0, 0, 0, 0]))

    # -- statements
    def run(self):
        lines = {n: k for k, (n, _) in enumerate(self.prog)}
        k = 0
        try:
            while k < len(self.prog):
                self.line, self.toks = self.prog[k]
                self.pos = 0
                nxt = self.statements()
                if nxt is not None:
                    if nxt not in lines:
                        self.error(8)
                    k = lines[nxt]
                else:
                    k += 1
        except Stop:
            pass
        return "".join(self.out)

    def statements(self):
        """Run the line from pos; a GOTO's line number, or None."""
        while True:
            t = self.peek()
            if t is None:
                return None
            if t == ("ch", ":"):
                self.take()
                continue
            r = self.statement()
            if r is not None:
                return r

    def statement(self):
        t = self.take()
        if t[0] == "kw":
            k = t[1]
            if k == "END":
                raise Stop()
            if k == "REM" or k == "ELSE":
                self.pos = len(self.toks)
                return None
            if k == "GOTO":
                return self.take()[1]
            if k == "PRINT":
                return self.print_()
            if k == "LET":
                t = self.take()
            if k == "IF":
                return self.if_()
        if t[0] == "var":
            self.expect_op("=")
            v = self.expr()
            if t[2] == "%":
                v = (mbf.INT, self.to_int(v))
            else:
                v = (mbf.SNG, self.to_sng(v))
            self.vars[(t[1], v[0])] = v
            return None
        self.error(2)

    def expect_op(self, c):
        t = self.take()
        if t != ("op", c):
            self.error(2)

    def print_(self):
        newline = True
        while True:
            t = self.peek()
            if t is None or t == ("ch", ":") or (t[0] == "kw" and t[1] == "ELSE"):
                break
            if t == ("ch", ";"):
                self.take()
                newline = False
                continue
            newline = True
            v = self.expr()
            self.out.append(mbf.fout(v[1], v[0]) + " ")
        if newline:
            self.out.append("\r\n")
        return None

    def if_(self):
        v = self.expr()
        t = self.peek()
        if t == ("ch", ","):
            self.take()
        t = self.take()
        if t != ("kw", "THEN") and t != ("kw", "GOTO"):
            self.error(2)
        if t == ("kw", "GOTO"):
            self.pos -= 1
        true = not (v[1] == 0 if v[0] == mbf.INT else v[1][3] == 0)
        if not true:
            depth = 1
            while True:  # DATA-skip statements, counting IFs, to the right ELSE
                while True:
                    t = self.peek()
                    if t is None:
                        return None
                    if t == ("ch", ":") or (t[0] == "kw" and t[1] == "ELSE"):
                        break
                    if t == ("kw", "IF"):
                        depth += 1
                    self.take()
                if t == ("ch", ":"):
                    self.take()
                    continue
                self.take()
                depth -= 1
                if depth == 0:
                    break
        t = self.peek()
        if t is None:
            return None
        if t[0] == "line":
            self.take()
            return t[1]
        if t == ("kw", "GOTO"):
            self.take()
            return self.take()[1]
        return self.statements()


def run(src):
    return Interp(parse(src)).run()


# --- Random expressions --------------------------------------------------------------


def random_expr(rng, depth):
    """A random expression the reference reader evaluates without an error."""
    while True:
        e = _rexpr(rng, depth)
        it = Interp([(10, lex("PRINT " + e))])
        out = it.run()
        if "ERR" not in out and "Overflow" not in out and "Division" not in out:
            return e


def _rexpr(rng, depth):
    if depth == 0 or rng.random() < 0.25:
        r = rng.random()
        if r < 0.35:
            return str(rng.randint(0, 9))
        if r < 0.6:
            return str(rng.choice([rng.randint(10, 300), rng.randint(300, 32767)]))
        if r < 0.85:
            return rng.choice(["0.5", "1.5", "2.25", "0.1", "3.14159", "123.456", "1E+5", "2.5E-3", "7.75",
                               "100000", "99999.9", "0.333333"])
        return rng.choice(["-", ""]) + str(rng.randint(1, 50))
    r = rng.random()
    if r < 0.12:
        return "-" + _rexpr(rng, depth - 1)
    if r < 0.2:
        return "%s(%s)" % (rng.choice(list(FUNCS)), _rexpr(rng, depth - 1))
    if r < 0.27:
        return "(" + _rexpr(rng, depth - 1) + ")"
    if r < 0.3:
        return "NOT " + _rexpr(rng, depth - 1)
    op = rng.choice(["+", "-", "*", "/", "+", "-", "*", "\\", " MOD ", "=", "<", ">", "<>", "<=", ">=", " AND ",
                     " OR ", " XOR "])
    return _rexpr(rng, depth - 1) + op + _rexpr(rng, depth - 1)
