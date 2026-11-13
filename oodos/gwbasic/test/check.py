"""Checks GW-BASIC's math (the port's, oodos/gwbasic) against mbf.py, on the emulator's W65C02S core.

The project's own, from the size spike's (oodos/spike/gwbasic/check.py), for the flat test image
(test/flat.cfg, harness.s): `make -C oodos/gwbasic test`, or ctest's test_gwbasic_math.

    python3 -I check.py --runner RUN6502 --image flat.bin [--seed N] [--count N] [-v]

0. Tables: the powers of ten, $FOTB, $FODTB, HIDBL and the polynomials' coefficients in p1/ must equal
   mbf.py's, which come from MATH1.ASM and from exact arithmetic, not from p1/. The vectors alone
   wouldn't catch a wrong low byte there.
1. Math vectors: thousands of operands, random and chosen at the edges, for each translated routine.
   run6502 runs them all (harness.s) and the results must equal mbf.py's byte for byte: the FAC, the
   type, the error, $FOUT's text, where $FIN stopped, RND's seed, and what each printed (Overflow and
   Division by zero, once only where GW-BASIC says so).
2. The model against exact arithmetic: how often each operation is the correctly rounded result, and
   its worst error in units of the last place. Divides and the double multiply must always be; the
   rest within an ulp.

    python3 -I check.py ... --programs

also runs the spike's programs (tokenised BASIC through NEWSTT, FRMEVL and the stub PRINT) against
basic.py: the interpreter's own suites take those over as its parts are translated.

    python3 -I check.py ... --timing

prints cycles instead: each op's on its operand sets, the op less one that only loads the operands, so
the figure is a JSR to the routine; and a BASIC loop's per pass.

Exits non-zero on any mismatch.
"""

import os
import random
import re
import subprocess
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import basic  # noqa: E402
import mbf  # noqa: E402

SRC = os.path.dirname(HERE)  # oodos/gwbasic
RUNNER = None  # --runner
IMAGE = None  # --image
LABELS = None  # beside the image
VLEN = 80
PER_RUN = 300
CTRL, VECTORS, PROGRAM = 0x9FF0, 0xA000, 0x9000
OUTCNT = "9FFE"

OPS = {
    "add": 1, "sub": 2, "mul": 3, "div": 4, "cmp": 5, "flt": 6, "cint": 7,
    "int": 8, "fix": 9, "iadd": 10, "isub": 11, "imul": 12, "idiv": 13,
    "imod": 14, "icmp": 15, "intdiv": 16, "fout": 17, "dmul": 18, "ddiv": 19,
    "csd": 20, "sgn": 21, "neg": 22, "abs": 23, "mul10": 24, "div10": 25,
    "dadd": 26, "dsub": 27, "dcmp": 28, "dint": 29, "fin": 30, "findbl": 31,
    "sqr": 32, "exp": 33, "log": 34, "sin": 35, "cos": 36, "tan": 37, "atn": 38,
    "pwr": 39, "rnd": 40, "fouto": 41, "fouth": 42,
    "ldab": 43, "ldint": 44, "ldfac": 45, "lddbl": 46, "ldcmp": 47, "foutt": 48, "ldtxt": 49,
}
SOFT = ("sqr", "exp", "log", "sin", "cos", "tan", "atn", "pwr", "rnd")  # once-only overflow messages


def check_tables():
    """p1/'s tables must be the model's, which come from MATH1.ASM and MATH2.ASM and from exact
    arithmetic, not from p1/: $FOTB, the powers of ten, $FODTB, HIDBL, $DHALF and each polynomial's
    coefficients. Returns the mismatches."""
    src = []
    for f in ("fout.s", "dmath.s", "trans.s"):
        src += open(os.path.join(SRC, "p1", f)).read().split("\n")

    def rows(label, n):
        i = [k for k, l in enumerate(src) if l.startswith(label + ":")][0]
        out = []
        for l in src[i:i + n]:
            vals = re.search(r"\.byte\s+([^;]*)", l).group(1)
            for v in vals.split(","):
                v = v.strip()
                if v.startswith("<-"):
                    out.append(-int(v[2:]))
                elif v.startswith("$"):
                    out.append(int(v[1:], 16))
                else:
                    out.append(int(v))
        return out

    bad = []
    if rows("FOTB", 32) != mbf.FOTB:
        bad.append("FOTB")
    dp = rows("DPM01", 40)
    if dp[0:8] != mbf.DPM01:
        bad.append("DPM01")
    for n in range(39):
        if dp[8 + 8 * n:16 + 8 * n] != mbf.DP[n]:
            bad.append("DP%02d" % n)
    fodtb = rows("FODTB", 9)
    if [int.from_bytes(bytes(fodtb[7 * k:7 * k + 7]), "little") for k in range(9)] != mbf.FODTB:
        bad.append("FODTB")
    if rows("HIDBL", 1) != mbf.HIDBL:
        bad.append("HIDBL")
    if rows("DHALF", 1) != mbf.DHALF:
        bad.append("DHALF")
    if rows("IN2PI", 1) != mbf.IN2PI:
        bad.append("IN2PI")
    for name in ("LOGP", "LOGQ", "EXPCN", "SINCN", "ATNC1", "ATNC2"):
        coefs = getattr(mbf, name)
        if rows(name, 1 + len(coefs)) != [len(coefs)] + [b for c in coefs for b in c]:
            bad.append(name)
    return bad


# --- Operands --------------------------------------------------------------------


def rsingle(rng, lo=1, hi=255):
    return [rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(lo, hi + 1)]


def single(x):
    return mbf.nearest(Fraction(x))


def rdouble(rng, lo=0x50, hi=0xB0):
    return [rng.randrange(256) for _ in range(7)] + [rng.randrange(lo, hi + 1)]


def double(x):
    return mbf.nearest(Fraction(x), 56)


def pos(b):
    b = list(b)
    b[-2] &= 0x7F
    return b


EDGE_S = [[0, 0, 0, 0], [0, 0, 0, 0x81], [0, 0, 0x80, 0x81], [0xFF, 0xFF, 0x7F, 0xFF], [0xFF, 0xFF, 0xFF, 0xFF],
          [0, 0, 0, 1], [0, 0, 0x80, 1], [0xFF, 0xFF, 0x7F, 0x80], [1, 0, 0, 0x98], [0, 0, 0x80, 0x90]]
EDGE_I = [0, 1, -1, 2, -2, 7, 10, 127, 128, 255, 256, 181, 182, -181, 32767, -32767, -32768, 16384, -16384, 1000]
EDGE_D = [[0] * 8, [0] * 7 + [0x81], [0] * 6 + [0x80, 0x81], [0xFF] * 6 + [0x7F, 0xFF], [0xFF] * 8,
          [0] * 7 + [1], [0xFF] * 6 + [0x7F, 0x80], [1] + [0] * 6 + [0xB8], [0] * 6 + [0x80, 0x90],
          [0x5A] * 7 + [0], [0x5A] * 6 + [0xDA, 0]]  # the last two: zero with a mantissa, and its negative


def small(rng):
    return single(Fraction(rng.randint(-1000, 1000), rng.choice([1, 2, 4, 10, 3])))


def number_text(rng):
    """A number as someone might type it, for $FIN."""
    k = rng.randrange(9)
    sign = rng.choice(["", "", "-", "+"])
    if k == 0:
        return sign + str(rng.randint(0, 99999))
    if k == 1:
        d = "".join(rng.choice("0123456789") for _ in range(rng.randint(1, 20)))
        p = rng.randint(0, len(d))
        return sign + d[:p] + "." + d[p:]
    if k == 2:
        m = "%d.%d" % (rng.randint(0, 999), rng.randint(0, 99999))
        return sign + m + rng.choice("EeDd") + rng.choice(["", "+", "-"]) + str(rng.randint(0, 45))
    if k == 3:
        return sign + str(rng.randint(0, 10 ** rng.randint(1, 18))) + rng.choice("!#%")
    if k == 4:
        return sign + repr(rng.uniform(-1e6, 1e6))[:22]
    if k == 5:
        return sign + "%.*E" % (rng.randint(0, 15), 10 ** rng.uniform(-40, 40))
    if k == 6:
        return " " * rng.randint(0, 3) + sign + str(rng.randint(0, 999)) + " " + str(rng.randint(0, 999))
    if k == 7:
        return sign + "." + "0" * rng.randint(0, 10) + str(rng.randint(1, 99999)) + rng.choice(["", "E-5", "D-30"])
    return sign + str(rng.randint(32760, 32780)) + rng.choice(["", ".", ".0", "!", "#"])


FIN_EDGES = ["0", "1", "-1", "+5", "32767", "32768", "-32768", "-32769", "65535", "3276", "3277", "32770",
             "999999", "1000000", "9999999", "12345678", "123456789012", "1234567890123456789012",
             ".5", "-.5", "1.", "1..2", ".", "-", "+", "", "1E", "1E+", "1E-", "1E5", "1e5", "1E+38", "1E38",
             "1E39", "1.7E38", "1.8E38", "1E-38", "1E-39", "1E-45", "1E-46", "1D38", "1D39", "1D-38", "1D-39",
             "1d5", "2#", "2!", "2%", "2.5%", "2.5#", "-2.5!", "1.5E3#", "1ELSE", "1EQV", "2ELSE", "1eqv",
             "1 2 3", "  42", "1:2", "3.14159265358979323846", "2.718281828459045", ".1", ".01", "1E3276",
             "1E32767", "1E99999", "1.23E-32767", "0.000000000000000000000000000000000000000000001",
             "123456.7", "1234567.8", "1e+0", "7D", "7D-", "9.999999E37", "1E", "12.34.56", "-0", "1 E 3"]


def vectors(rng, count):
    """(op, typeA, A, B) tuples; $FIN's A is its text."""
    v = []
    S, I, D = mbf.SNG, mbf.INT, mbf.DBL
    for op in ("add", "sub", "mul", "div", "cmp"):
        for a in EDGE_S:
            for b in EDGE_S:
                v.append((op, S, a, b))
        for _ in range(count):
            a = rsingle(rng)
            if rng.random() < 0.5:  # close exponents: cancellation and carries
                b = rsingle(rng, max(1, a[3] - 3), min(255, a[3] + 3))
            else:
                b = rsingle(rng)
            v.append((op, S, a, b))
        for _ in range(count // 4):  # small numbers, as programs use
            a = single(rng.randint(-1000, 1000) / rng.choice([1, 2, 4, 10, 3]))
            b = single(rng.randint(-1000, 1000) / rng.choice([1, 2, 8, 10, 7]))
            v.append((op, S, a, b))
    for op in ("flt",):
        for i in EDGE_I + [rng.randint(-32768, 32767) for _ in range(count // 2)]:
            v.append((op, I, ints(i), ints(0)))
    for op in ("cint", "int", "fix", "sgn", "neg", "abs", "mul10", "div10"):
        for a in EDGE_S:
            v.append((op, S, a, None))
        for _ in range(count // 2):
            v.append((op, S, rsingle(rng, 0x60, 0xA0), None))
        for _ in range(count // 4):
            v.append((op, S, single(Fraction(rng.randint(-200000, 200000), rng.choice([1, 2, 4, 10, 1000]))), None))
    for op in ("sgn", "neg", "abs"):
        for i in EDGE_I:
            v.append((op, I, ints(i), None))
    for op in ("iadd", "isub", "imul", "idiv", "imod", "icmp", "intdiv"):
        for a in EDGE_I:
            for b in EDGE_I[:12] + [-32768, 32767]:
                v.append((op, I, ints(a), ints(b)))
        for _ in range(count // 2):
            v.append((op, I, ints(rng.randint(-32768, 32767)), ints(rng.randint(-32768, 32767))))
            v.append((op, I, ints(rng.randint(-300, 300)), ints(rng.randint(-300, 300))))
    for op in ("dmul", "ddiv"):
        for _ in range(count):
            v.append((op, D, rdouble(rng), rdouble(rng)))
        for n in range(39):
            v.append((op, D, rdouble(rng, 0x70, 0x90), mbf.DP[n]))
        v.append((op, D, rdouble(rng), [0] * 8))
        v.append((op, D, [0] * 8, rdouble(rng)))
        v.append((op, D, [0xFF] * 7 + [0xF0], [0xFF] * 7 + [0x90]))
    # The double add, subtract and compare: random, close exponents, 56-58 apart, equal
    # magnitudes, the edges, and overflow.
    for op in ("dadd", "dsub", "dcmp"):
        for a in EDGE_D:
            for b in EDGE_D:
                v.append((op, D, a, b))
        for _ in range(count):
            a = rdouble(rng, 1, 0xFF)
            r = rng.random()
            if r < 0.4:
                b = rdouble(rng, max(1, a[7] - 9), min(255, a[7] + 9))
            elif r < 0.5:
                b = rdouble(rng)
                b[7] = max(1, min(255, a[7] + rng.choice([-58, -57, -56, -55, 55, 56, 57, 58])))
            elif r < 0.6:
                b = list(a)
                b[6] ^= rng.choice([0, 0x80])
                b[rng.randrange(7)] ^= rng.choice([0, 1, 0x80])
            else:
                b = rdouble(rng, 1, 0xFF)
            v.append((op, D, a, b))
        for _ in range(count // 4):
            v.append((op, D, double(Fraction(rng.randint(-10 ** 6, 10 ** 6), rng.choice([1, 3, 10, 7]))),
                      double(Fraction(rng.randint(-10 ** 6, 10 ** 6), rng.choice([1, 3, 10, 9])))))
    for _ in range(count // 2):  # ties: the bits shifted out of the smaller decide its rounding
        for op in ("dadd", "dsub"):
            v.append((op, D) + tie_pair(rng))
    for a in EDGE_D + [rdouble(rng, 1, 0xFF) for _ in range(count)] + \
            [double(Fraction(rng.randint(-10 ** 9, 10 ** 9), rng.choice([1, 2, 4, 10, 1000]))) for _ in range(count // 2)]:
        v.append(("dint", D, a, None))
    for _ in range(count):
        v.append(("csd", D, rdouble(rng, 1, 0xFE), None))
    fo = [[0, 0, 0, 0]] + EDGE_S[1:]
    for x in ("1", "-1", "0.5", "0.1", "1.5", "100", "1e6", "999999", "9999999", "12345678", "0.01", "0.001",
              "1e-5", "123.456", "3.14159265", "1e38", "1.7e38", "1e-38", "2.5e-39", "65535", "-32768",
              "1000000", "10000000", "0.000123", "7", "1/3", "2/3", "10/3", "1/7", "-2.75"):
        fo.append(single(Fraction(x)))
    for _ in range(count):
        fo.append(rsingle(rng))
        fo.append(single(Fraction(rng.randint(-10 ** 7, 10 ** 7), 10 ** rng.randint(0, 9))))
    for a in fo:
        v.append(("fout", S, a, None))
    for i in EDGE_I + [rng.randint(-32768, 32767) for _ in range(count // 4)]:
        v.append(("fout", I, ints(i), None))
    fd = [[0] * 8] + EDGE_D[1:9]
    for x in ("1", "-1", "0.5", "0.1", "1/3", "2/3", "10/3", "1/7", "9999999999999999", "10000000000000000",
              "99999999999999995", "1e16", "1e15", "123456789012345678", "1e38", "1e-38", "3.141592653589793",
              "1e-5", "100", "12345678", "0.000123", "-2.75", "1e17", "65535"):
        fd.append(double(Fraction(x)))
    for _ in range(count // 2):
        fd.append(rdouble(rng, 1, 0xFF))
        fd.append(double(Fraction(rng.randint(-10 ** 16, 10 ** 16), 10 ** rng.randint(0, 20))))
    for a in fd:
        v.append(("fout", D, a, None))
    for i in EDGE_I + [rng.randint(-32768, 32767) for _ in range(count // 4)]:
        v.append(("fouto", I, ints(i), None))
        v.append(("fouth", I, ints(i), None))
    # $FIN: the edges, then numbers as typed.
    texts = FIN_EDGES + [number_text(rng) for _ in range(count * 2)]
    for t in texts:
        v.append(("fin", I, t[:23], None))
    for t in FIN_EDGES[:40] + [number_text(rng) for _ in range(count // 2)]:
        v.append(("findbl", D, t[:23], None))
    # The functions: the edges, random singles, and the ranges each takes.
    for op in ("sqr", "exp", "log", "sin", "cos", "tan", "atn"):
        for a in EDGE_S + [[0, 0, 0x80, 0]]:
            v.append((op, S, a, None))
        for _ in range(count // 2):
            v.append((op, S, rsingle(rng, 0x50, 0xB0), None))
        for _ in range(count // 4):
            v.append((op, S, small(rng), None))
    for _ in range(count // 2):
        v.append(("sqr", S, pos(rsingle(rng)), None))
        v.append(("exp", S, single(Fraction(rng.uniform(-100, 100))), None))
        v.append(("log", S, pos(rsingle(rng)), None))
        for op in ("sin", "cos", "tan"):
            v.append((op, S, single(Fraction(rng.uniform(-10, 10))), None))
            v.append((op, S, single(Fraction(rng.uniform(-1e7, 1e7))), None))
        v.append(("atn", S, single(Fraction(rng.uniform(-5, 5))), None))
    for x in ("88.02969", "88.0297", "88.7", "-88.7", "-89", "-90", "89", "127", "-127", "1e-8", "-1e-8"):
        v.append(("exp", S, single(Fraction(x)), None))
    for k in range(-8, 9):  # multiples of pi/4, and next to them
        x = Fraction(k) * Fraction(314159265358979, 4 * 10 ** 14)
        for op in ("sin", "cos", "tan"):
            v.append((op, S, single(x), None))
            b = single(x)
            b[0] ^= 1
            v.append((op, S, b, None))
    # ^: random, integer powers (XTON), negative bases, zero, and the overflows.
    for a in EDGE_S[:5]:
        for b in EDGE_S[:5]:
            v.append(("pwr", S, a, b))
    for _ in range(count // 2):
        v.append(("pwr", S, pos(rsingle(rng, 0x70, 0x90)), single(Fraction(rng.uniform(-30, 30)))))
        v.append(("pwr", S, small(rng), single(rng.randint(-40, 40))))
        v.append(("pwr", S, single(Fraction(rng.uniform(-3, 3))), single(rng.randint(-300, 300))))
        v.append(("pwr", S, small(rng), single(Fraction(rng.randint(-20, 20), 2))))
        v.append(("pwr", S, rsingle(rng), rsingle(rng, 0x70, 0x98)))
    for x, y in (("2", "10"), ("2", "-10"), ("-2", "3"), ("-2", "-3"), ("-2", "2.5"), ("0", "-1"), ("0", "2"),
                 ("10", "38"), ("10", "39"), ("-10", "39"), ("10", "-39"), ("-10", "-39"), ("2", "127"),
                 ("2", "-128"), ("2", "-129"), ("-2", "-129"), ("-2", "-130"), ("1.0001", "32767"),
                 ("1.0001", "32768"), ("-1", "32768"), ("-1", "32769"), ("-1", "16777215"), ("-1", "16777216"),
                 ("2", "0.5"), ("9", "0.5"), ("10", "0.30103"), ("1e-20", "2"), ("1e-20", "-2"), ("3", "0")):
        v.append(("pwr", S, single(Fraction(x)), single(Fraction(y))))
    # RND: seeds and arguments.
    for _ in range(count // 2):
        seed = [rng.randrange(256) for _ in range(3)] + [0x80]
        arg = rng.choice([single(1), single(0), single(-rng.randint(1, 1000)), rsingle(rng), [0, 0, 0, 0]])
        v.append(("rnd", S, arg, seed))
    v.append(("rnd", S, single(1), list(mbf.RNDCOP)))
    v.append(("rnd", S, single(0), list(mbf.RNDCOP)))
    v.append(("rnd", S, single(-1), list(mbf.RNDCOP)))
    v.append(("rnd", S, single(1), [0, 0, 0, 0]))
    return [x for x in v if x[2] is not None]  # nearest() is None out of range


def tie_pair(rng, exact=None):
    """Two doubles 3-56 bits apart whose smaller, shifted into place, leaves its overflow byte at
    exactly $80, a tie, but for bits shifted out past it, which $SHRA's sticky bit (ST) must keep.
    Half of them have none: an exact tie, rounded to even."""
    a = rdouble(rng, 0x40, 0xC0)
    cl = rng.randint(3, 56)
    m = rng.getrandbits(56) | (1 << 55)
    x = (m << 8) | (1 << (cl + 7))
    x &= ~(((1 << 7) - 1) << cl)  # the overflow byte to be: $80
    x &= ~((1 << cl) - 1)
    if exact is None:
        exact = rng.random() < 0.5
    if not exact:
        x |= 1 << rng.randrange(8, max(9, cl)) if cl > 8 else 0  # one bit to fall out
    m = (x >> 8) & ((1 << 56) - 1)
    b = list(m.to_bytes(7, "little"))
    b[6] = (b[6] & 0x7F) | rng.choice([0, 0x80])
    b.append(a[7] - cl)
    pair = (a, b) if rng.random() < 0.5 else (b, a)
    if exact or sticky_decides(*pair):
        return pair
    return tie_pair(rng, False)


def sticky_decides(a, b):
    """Whether the sum or difference rounds otherwise without $SHRA's sticky bit."""
    out = []
    for sticky in (True, False):
        mbf.STICKY = sticky
        try:
            out.append((mbf.dadd(a, b), mbf.dsub(a, b)))
        except mbf.Overflow:
            out.append(None)
    mbf.STICKY = True
    return out[0] != out[1]


def ints(i):
    i &= 0xFFFF
    return [i & 0xFF, i >> 8, 0, 0]


# --- The model's answer -----------------------------------------------------------

MSGLEN = {"Overflow": 10, "Division by zero": 18}  # with CR LF


def model(op, ta, a, b):
    """A dict: typ, val (bytes or an int), cmp, err, text, end ($FIN), rndx, printed (characters)."""
    S, I, D = mbf.SNG, mbf.INT, mbf.DBL
    r = dict(typ=None, val=None, cmp=0, err=0, text=None, end=None, rndx=None, printed=0)
    if op in ("fin", "findbl"):
        try:
            typ, val, end, msgs = mbf.fin(a, op == "findbl")
        except mbf.Error as e:
            r["err"] = 5  # "&": OCTCNS, the stand-in's FC error
            return r
        r.update(typ=typ, val=val, end=end, printed=sum(MSGLEN[m] for m in msgs[:1]))
        return r
    ia = mbf.i16(a[0] | (a[1] << 8))
    ib = mbf.i16(b[0] | (b[1] << 8)) if b else 0
    typ = {"cint": I, "flt": S, "csd": S, "icmp": I, "cmp": S, "intdiv": S, "dcmp": D}.get(op, ta)
    r["typ"] = typ
    soft = mbf.Soft()
    try:
        if op in SOFT:
            f = {"sqr": lambda: mbf.sqr(a), "exp": lambda: mbf.exp_(a, soft), "log": lambda: mbf.log_(a, soft),
                 "sin": lambda: mbf.sin_(a, soft), "cos": lambda: mbf.cos_(a, soft),
                 "tan": lambda: mbf.tan_(a, soft), "atn": lambda: mbf.atn_(a, soft),
                 "pwr": lambda: mbf.fpwr(a, b, soft)}.get(op)
            if op == "rnd":
                rndx, val = mbf.rnd(b[0:4], a)
                r.update(typ=S, val=val, rndx=rndx)
            else:
                r.update(typ=S, val=f())
            r["printed"] = sum(MSGLEN[m] for m in soft.msgs[:1])
            return r
        if op == "add":
            r["val"] = mbf.fadds(a, b)
        elif op == "sub":
            r["val"] = mbf.fsubs(a, b)
        elif op == "mul":
            r["val"] = mbf.fmuls(a, b)
        elif op == "div":
            r["val"] = mbf.fdivs(a, b)
        elif op == "cmp":
            r["cmp"] = mbf.fcomp(a, b)
        elif op == "flt":
            r["val"] = mbf.flt(ia)
        elif op == "cint":
            r["val"] = mbf.cint(a)
        elif op == "int":
            r["val"] = mbf.int_(a)
        elif op == "fix":
            r["val"] = mbf.fix(a)
        elif op in ("iadd", "isub", "imul", "idiv", "imod"):
            f = {"iadd": mbf.iadd, "isub": mbf.isub, "imul": mbf.imult, "idiv": mbf.idiv, "imod": mbf.imod}[op]
            val, t = f(ia, ib)
            r.update(typ=t, val=val)
        elif op == "icmp":
            r["cmp"] = mbf.icomp(ia, ib)
        elif op == "intdiv":
            r["val"] = mbf.intdiv(ia, ib)
        elif op == "dmul":
            r["val"] = mbf.fmuld(a, b)
        elif op == "ddiv":
            r["val"] = mbf.ddiv(a, b)
        elif op == "dadd":
            r["val"] = mbf.dadd(a, b)
        elif op == "dsub":
            r["val"] = mbf.dsub(a, b)
        elif op == "dcmp":
            r["cmp"] = mbf.dcomp(a, b)
        elif op == "dint":
            r["val"] = mbf.dint(a)
        elif op == "csd":
            r["val"] = mbf.csd(a)
        elif op == "sgn":
            r["typ"] = I
            if ta == I:
                r["val"] = (ia > 0) - (ia < 0)
            else:
                r["val"] = 0 if a[3] == 0 else (-1 if a[2] & 0x80 else 1)
        elif op == "neg":
            if ta == I:
                r.update(typ=S, val=mbf.neg(mbf.flt(ia))) if ia == -32768 else r.update(val=-ia)
            else:
                r["val"] = mbf.neg(a)
        elif op == "abs":
            if ta == I:
                r.update(typ=S, val=mbf.neg(mbf.flt(ia))) if ia == -32768 else r.update(val=abs(ia))
            else:
                r["val"] = a[:2] + [a[2] & 0x7F, a[3]]
        elif op == "mul10":
            r["val"] = mbf.mul10(a)
        elif op == "div10":
            r["val"] = mbf.div10(a)
        elif op == "fout":
            r["text"] = mbf.fout(ia if ta == I else a, ta)
        elif op in ("fouto", "fouth"):
            r["text"] = mbf.foutoh(ia, op == "fouto")
        else:
            raise ValueError(op)
    except mbf.Overflow as o:
        if op in ("idiv", "imod"):
            typ = S
        r.update(typ=o.typ if op in ("dmul", "ddiv", "dadd", "dsub") else typ, val=o.result(), printed=MSGLEN[o.msg])
    except mbf.Error as e:
        r.update(typ=None, val=None, err=e.code)
    return r


# --- Running ----------------------------------------------------------------------


def run_vectors(vs, workdir):
    out = []
    for start in range(0, len(vs), PER_RUN):
        chunk = vs[start:start + PER_RUN]
        data = bytearray()
        for op, ta, a, b in chunk:
            rec = bytearray(VLEN)
            rec[0] = OPS[op]
            rec[1] = ta
            if isinstance(a, str):
                t = a.encode("latin-1")
                rec[56:56 + len(t)] = t
            else:
                rec[4:4 + len(a)] = bytes(a)
            if b:
                rec[12:12 + len(b)] = bytes(b)
            data += rec
        ctrl = bytes([0, 0, len(chunk) & 0xFF, len(chunk) >> 8])
        vec = os.path.join(workdir, "math.vec")
        cfile = os.path.join(workdir, "ctrl.vec")
        res = os.path.join(workdir, "math.out")
        open(vec, "wb").write(data)
        open(cfile, "wb").write(ctrl)
        p = subprocess.run([RUNNER, IMAGE, "--load", "%X:%s" % (CTRL, cfile), "--load", "%X:%s" % (VECTORS, vec),
                            "--outcount", OUTCNT, "--max", "2000000000",
                            "--dump", "%X:%X:%s" % (VECTORS, len(data), res)], capture_output=True, text=True)
        if p.returncode:
            sys.exit("run6502 failed: " + p.stderr)
        got = open(res, "rb").read()
        for k in range(len(chunk)):
            out.append(got[k * VLEN:(k + 1) * VLEN])
    return out


def compare(vs, results, verbose):
    bad = 0
    per = {}
    for (op, ta, a, b), r in zip(vs, results):
        m = model(op, ta, a, b)
        rtyp, rcmp, rerr, rend, rout = r[20], r[21], r[22], r[2], r[3]
        fac = list(r[24:32])
        text = bytes(r[32:56]).split(b"\0")[0].decode("latin-1")
        typ, val = m["typ"], m["val"]
        ok = True
        if m["err"] or rerr:
            ok = m["err"] == rerr
        elif op in ("cmp", "icmp", "dcmp"):
            ok = (rcmp if rcmp < 128 else rcmp - 256) == m["cmp"]
        elif m["text"] is not None:
            ok = text == m["text"]
        else:
            if rtyp != typ:
                ok = False
            elif typ == mbf.INT:
                ok = mbf.i16(fac[4] | (fac[5] << 8)) == mbf.i16(val)
            elif typ == mbf.SNG:
                ok = fac[4:8] == val
            else:
                ok = fac == val
            if m["end"] is not None and rend != m["end"]:
                ok = False
            if m["rndx"] is not None and list(r[32:36]) != m["rndx"]:
                ok = False
        if not m["err"] and rout != m["printed"]:
            ok = False
        per.setdefault(op, [0, 0])[0] += 1
        if not ok:
            per[op][1] += 1
            bad += 1
            if verbose or bad <= 12:
                print("MISMATCH %s %s %s: model %s %s cmp=%s err=%s %r end=%s rndx=%s printed=%d; "
                      "6502 type=%d cmp=%d err=%d fac=%s text=%r end=%d rndx=%s printed=%d" % (
                          op, a if isinstance(a, str) else hx(a), hx(b) if b else "", typ,
                          hx(val) if isinstance(val, list) else val, m["cmp"], m["err"], m["text"], m["end"],
                          hx(m["rndx"]) if m["rndx"] else "-", m["printed"], rtyp, rcmp, rerr, hx(fac), text, rend,
                          hx(r[32:36]), rout))
    return bad, per


def hx(b):
    return " ".join("%02X" % x for x in b) if b is not None else "-"


# --- The model against exact arithmetic ----------------------------------------------


def exactness(rng, n):
    """Divides and the double multiply must be correctly rounded every time;
    add, subtract and single multiply aren't quite (README, Correctness), but
    must stay within an ulp. Returns the lines and the number of failures."""
    lines = []
    failed = 0
    for name, f, g, bits, exact in (("add", mbf.fadds, lambda x, y: x + y, 24, False),
                                    ("sub", mbf.fsubs, lambda x, y: x - y, 24, False),
                                    ("mul", mbf.fmuls, lambda x, y: x * y, 24, False),
                                    ("div", mbf.fdivs, lambda x, y: x / y, 24, True),
                                    ("dmul", mbf.fmuld, lambda x, y: x * y, 56, True),
                                    ("ddiv", mbf.ddiv, lambda x, y: x / y, 56, True)):
        same = total = 0
        worst = Fraction(0)
        for _ in range(n):
            if bits == 24:
                a, b = rsingle(rng, 0x60, 0xA0), rsingle(rng, 0x60, 0xA0)
            else:
                a, b = rdouble(rng), rdouble(rng)
            try:
                r = f(a, b)
            except mbf.Overflow:
                continue
            x = g(mbf.value(a), mbf.value(b))
            want = mbf.nearest(x, bits)
            if want is None or r[-1] == 0:
                continue
            total += 1
            if r == want:
                same += 1
            else:
                e = r[-1]
                ulp = Fraction(2) ** (e - 128 - bits)
                worst = max(worst, abs(mbf.value(r) - x) / ulp)
        bad = (same < total) if exact else (worst >= 1)
        failed += bad
        lines.append("  %-5s %5d/%d correctly rounded; worst %.3f ulp%s" % (
            name, same, total, float(worst), "  BAD" if bad else ""))
    # Step M's: the double add and subtract within an ulp, SQR correctly rounded, and $FIN's singles
    # from 7-digit decimals within an ulp.
    for name, f, g, bits, exact in (("dadd", mbf.dadd, lambda x, y: x + y, 56, False),
                                    ("dsub", mbf.dsub, lambda x, y: x - y, 56, False),
                                    ("sqr", None, None, 24, True), ("fin", None, None, 24, False)):
        same = total = 0
        worst = Fraction(0)
        for _ in range(n):
            try:
                if name == "sqr":
                    a = pos(rsingle(rng, 0x60, 0xA0))
                    r = mbf.sqr(a)
                    want = sqrt_nearest(mbf.value(a))
                    x = mbf.value(want)
                elif name == "fin":
                    t = "%d.%06dE%d" % (rng.randint(1, 9), rng.randint(0, 999999), rng.randint(-30, 30))
                    typ, r, _, _ = mbf.fin(t)
                    x = decimal_value(t)
                    want = mbf.nearest(x)
                else:
                    a, b = rdouble(rng), rdouble(rng)
                    r = f(a, b)
                    x = g(mbf.value(a), mbf.value(b))
                    want = mbf.nearest(x, bits)
            except mbf.Overflow:
                continue
            if want is None or r[-1] == 0:
                continue
            total += 1
            if r == want:
                same += 1
            elif name != "sqr":
                ulp = Fraction(2) ** (r[-1] - 128 - bits)
                worst = max(worst, abs(mbf.value(r) - x) / ulp)
        bad = (same < total) if exact else (worst >= 1)
        failed += bad
        lines.append("  %-5s %5d/%d correctly rounded; worst %.3f ulp%s" % (
            name, same, total, float(worst), "  BAD" if bad else ""))
    return lines, failed


def decimal_value(t):
    m, e = t.split("E")
    return Fraction(m) * Fraction(10) ** int(e)


def sqrt_nearest(x):
    """The single nearest the square root of x (a Fraction), exactly."""
    lo, hi = Fraction(0), max(Fraction(1), x)
    for _ in range(200):
        mid = (lo + hi) / 2
        if mid * mid > x:
            hi = mid
        else:
            lo = mid
    return mbf.nearest(lo)  # a root can't lie within 2^-200 of a single's midpoint


def accuracy(rng, n):
    """The functions against Python's math, in units of the single's last place: GW-BASIC's own
    accuracy, printed, and bounded so a coefficient read wrong or a step out of order shows. EXP
    (and ^, which ends in EXP) loses most: x*LOG2(e) in single precision is out by up to half an ulp
    of a number up to 128, 2^-18, which EXP makes a relative 2^-18.5 (about 45 ulp). LOG near 1, a
    small result from P/Q plus the exponent, is good to an ulp of 1, not of itself: it is
    measured so."""
    import math
    lines, failed = [], 0
    ref = {"sqr": math.sqrt, "exp": math.exp, "log": math.log, "sin": math.sin, "cos": math.cos,
           "tan": math.tan, "atn": math.atan}
    dom = {"sqr": (1e-9, 1e9), "exp": (-80, 80), "log": (1e-9, 1e9), "sin": (-3.2, 3.2), "cos": (-3.2, 3.2),
           "tan": (-1.5, 1.5), "atn": (-30, 30)}
    bound = {"sqr": 0.5, "exp": 128, "log": 4, "sin": 4, "cos": 4, "tan": 8, "atn": 4, "pwr": 160}
    for op in ("sqr", "exp", "log", "sin", "cos", "tan", "atn", "pwr"):
        worst = 0.0
        for _ in range(n):
            soft = mbf.Soft()
            if op == "pwr":
                x, y = rng.uniform(0.1, 10), rng.uniform(-15, 15)
                a, b = single(Fraction(x)), single(Fraction(y))
                r = mbf.fpwr(a, b, soft)
                t = float(mbf.value(a)) ** float(mbf.value(b))
            else:
                lo, hi = dom[op]
                u = rng.uniform(math.log(lo), math.log(hi)) if op in ("sqr", "log") else rng.uniform(lo, hi)
                a = single(Fraction(math.exp(u) if op in ("sqr", "log") else u))
                r = {"sqr": lambda: mbf.sqr(a), "exp": lambda: mbf.exp_(a, soft), "log": lambda: mbf.log_(a, soft),
                     "sin": lambda: mbf.sin_(a, soft), "cos": lambda: mbf.cos_(a, soft),
                     "tan": lambda: mbf.tan_(a, soft), "atn": lambda: mbf.atn_(a, soft)}[op]()
                t = ref[op](float(mbf.value(a)))
            if r[3] == 0 or soft.msgs or t == 0:
                continue
            ulp = 2.0 ** ((max(r[3], 0x81) if op == "log" else r[3]) - 128 - 24)  # LOG: absolute, near 1
            if op in ("sin", "cos", "tan") and abs(t) < 0.01:  # near a zero the absolute error rules
                continue
            worst = max(worst, abs(float(mbf.value(r)) - t) / ulp)
        b = bound[op]
        bad = worst > b
        failed += bad
        lines.append("  %-5s worst %6.2f ulp (bound %g)%s" % (op.upper(), worst, b, "  BAD" if bad else ""))
    return lines, failed


# --- Timing ------------------------------------------------------------------------------


def nonzero(f):
    """f, again until it isn't zero: dividing by zero is another path."""
    def g(rng):
        while True:
            x = f(rng)
            if any(x):
                return x
    return g


def rint(lo, hi):
    return lambda rng: ints(rng.randint(lo, hi))


def ranged(lo, hi):
    return lambda rng: single(Fraction(rng.uniform(lo, hi)))


def label(name):
    for l in open(LABELS):
        m = re.match(r"al ([0-9A-F]+) \.(\w+)$", l.strip())
        if m and m.group(2) == name:
            return int(m.group(1), 16)
    raise KeyError(name)


# The operand sets.
SNG_R = ("random singles, exponents $60-$A0", lambda rng: rsingle(rng, 0x60, 0xA0))
SNG_S = ("small decimals, n/1 2 4 10 3 for |n| <= 1000", small)
SNG_P = ("random positive singles, exponents $60-$A0", lambda rng: pos(rsingle(rng, 0x60, 0xA0)))
SNG_E = ("singles in -80..80", ranged(-80, 80))
SNG_T = ("singles in -10..10", ranged(-10, 10))
SNG_B = ("bases .5..4, exponents -8..8", ranged(0.5, 4))
SNG_N = ("bases .5..4, integer exponents 1..20", ranged(0.5, 4))
INT_S = ("integers, -181..181 (no product overflows)", rint(-181, 181))
INT_F = ("integers, full range (overflows go to single)", rint(-32768, 32767))
DBL_R = ("random doubles, exponents $50-$B0", rdouble)
DBL_S = ("doubles n/1 2 4 10 for |n| <= 10^9", lambda rng: double(Fraction(rng.randint(-10 ** 9, 10 ** 9),
                                                                          rng.choice([1, 2, 4, 10]))))
TXT_N = ("numbers typed: 1-7 digits, a point, sometimes an exponent", None)

# (label, op, the load-only op, VALTYP, operand set, the second operand's set or None, a divisor?)
TIMED = [("$FADDS", "add", "ldab", mbf.SNG, SNG_R, None, False), ("$FADDS", "add", "ldab", mbf.SNG, SNG_S, None, False),
         ("$FMULS", "mul", "ldab", mbf.SNG, SNG_R, None, False), ("$FMULS", "mul", "ldab", mbf.SNG, SNG_S, None, False),
         ("$FDIVS", "div", "ldab", mbf.SNG, SNG_R, None, True), ("$FDIVS", "div", "ldab", mbf.SNG, SNG_S, None, True),
         ("$FCOMP", "cmp", "ldcmp", mbf.SNG, SNG_R, None, False),
         ("IADD", "iadd", "ldint", mbf.INT, INT_S, None, False), ("IADD", "iadd", "ldint", mbf.INT, INT_F, None, False),
         ("IMULT", "imul", "ldint", mbf.INT, INT_S, None, False), ("IMULT", "imul", "ldint", mbf.INT, INT_F, None, False),
         ("IDIV", "idiv", "ldint", mbf.INT, INT_S, None, True), ("IDIV", "idiv", "ldint", mbf.INT, INT_F, None, True),
         ("$FOUT", "foutt", "ldfac", mbf.SNG, SNG_R, None, False), ("$FOUT", "foutt", "ldfac", mbf.SNG, SNG_S, None, False),
         ("$FOUT", "foutt", "ldfac", mbf.INT, INT_S, None, False), ("$FOUT", "foutt", "ldfac", mbf.INT, INT_F, None, False),
         ("$FOUT", "foutt", "ldfac", mbf.DBL, DBL_R, None, False), ("$FOUT", "foutt", "ldfac", mbf.DBL, DBL_S, None, False),
         ("$FMULD", "dmul", "lddbl", mbf.DBL, DBL_R, None, False), ("DDIV", "ddiv", "lddbl", mbf.DBL, DBL_R, None, True),
         ("$FADDD", "dadd", "lddbl", mbf.DBL, DBL_R, None, False), ("$FADDD", "dadd", "lddbl", mbf.DBL, DBL_S, None, False),
         ("DCOMP", "dcmp", "lddbl", mbf.DBL, DBL_R, None, False), ("$DINT", "dint", "ldfac", mbf.DBL, DBL_S, None, False),
         ("$FIN", "fin", "ldtxt", mbf.INT, TXT_N, None, False),
         ("$SQR", "sqr", "ldfac", mbf.SNG, SNG_P, None, False), ("$EXP", "exp", "ldfac", mbf.SNG, SNG_E, None, False),
         ("$LOG", "log", "ldfac", mbf.SNG, SNG_P, None, False), ("$SIN", "sin", "ldfac", mbf.SNG, SNG_T, None, False),
         ("$COS", "cos", "ldfac", mbf.SNG, SNG_T, None, False), ("$TAN", "tan", "ldfac", mbf.SNG, SNG_T, None, False),
         ("$ATAN", "atn", "ldfac", mbf.SNG, SNG_T, None, False),
         ("$FPWR", "pwr", "ldab", mbf.SNG, SNG_B, ranged(-8, 8), False),
         ("XTON", "pwr", "ldab", mbf.SNG, SNG_N, lambda rng: single(rng.randint(1, 20)), False),
         ("$RND", "rnd", "ldfac", mbf.SNG, ("RND(1)", lambda rng: single(1)), None, False)]

LOOPS = [("10 I%=I%+1 : IF I%<{n} THEN 10", "I%=I%+1 : IF I%<1000 THEN 10"),
         ("10 A=A+I%*1.5 : I%=I%+1 : IF I%<{n} THEN 10", "A=A+I%*1.5 : I%=I%+1 : IF I%<1000 THEN 10")]


def run_cycles(recs, workdir):
    """The cycles of one run of these vectors (at most PER_RUN)."""
    vec = os.path.join(workdir, "time.vec")
    cfile = os.path.join(workdir, "ctrl.vec")
    open(vec, "wb").write(b"".join(recs))
    open(cfile, "wb").write(bytes([0, 0, len(recs) & 0xFF, len(recs) >> 8]))
    res = os.path.join(workdir, "time.out")
    p = subprocess.run([RUNNER, IMAGE, "--load", "%X:%s" % (CTRL, cfile), "--load", "%X:%s" % (VECTORS, vec),
                        "--max", "2000000000", "--outcount", OUTCNT,
                        "--dump", "%X:%X:%s" % (VECTORS, VLEN * len(recs), res)], capture_output=True, text=True)
    if p.returncode:
        sys.exit("run6502 failed: " + p.stderr)
    if any(open(res, "rb").read()[22::VLEN]):  # the error path would be timed instead
        sys.exit("timing: an operand set gave an error")
    return int(re.search(r"(\d+) cycles", p.stderr).group(1))


def op_cycles(rng, workdir, n=300):
    """(label, operand set, mean cycles) for each of TIMED."""
    out = []
    for label_, op, load, typ, (what, gen), gen2, divisor in TIMED:
        if op == "fin":
            pairs = [("%d.%d" % (rng.randint(0, 9999), rng.randint(0, 999)) + rng.choice(["", "E5", "E-12"]), None)
                     for _ in range(n)]
        else:
            g2 = gen2 or gen
            pairs = [(gen(rng), (nonzero(g2) if divisor else g2)(rng)) for _ in range(n)]

        def recs(code):
            r = []
            for a, b in pairs:
                rec = bytearray(VLEN)
                rec[0], rec[1] = OPS[code], typ
                if isinstance(a, str):
                    rec[56:56 + len(a)] = a.encode()
                else:
                    rec[4:4 + len(a)] = bytes(a)
                if op == "rnd":
                    rec[12:16] = bytes(mbf.RNDCOP)
                elif b:
                    rec[12:12 + len(b)] = bytes(b)
                r.append(bytes(rec))
            return r
        out.append((label_, what, (run_cycles(recs(op), workdir) - run_cycles(recs(load), workdir)) / n))
    return out


def loop_cycles(workdir):
    """(the loop, cycles a pass)."""
    out = []
    for src, name in LOOPS:
        c = []
        for n in (1000, 2000):
            _, err = run_program(src.format(n=n) + "\n20 END\n", workdir)
            c.append(int(re.search(r"(\d+) cycles", err).group(1)))
        out.append((name, (c[1] - c[0]) / 1000))
    return out


# --- Programs ---------------------------------------------------------------------------


def run_program(src, workdir, counts=()):
    basic.BASE = PROGRAM  # the links in the text are addresses: the flat image's place for it
    prog = basic.tokenise(src)
    pfile = os.path.join(workdir, "prog.vec")
    cfile = os.path.join(workdir, "ctrl.vec")
    open(pfile, "wb").write(prog)
    open(cfile, "wb").write(bytes([1, 0, 0, 0]))
    args = [RUNNER, IMAGE, "--load", "%X:%s" % (PROGRAM, pfile), "--load", "%X:%s" % (CTRL, cfile),
            "--max", "400000000"]
    for c in counts:
        args += ["--count", "%X" % c]
    p = subprocess.run(args, capture_output=True)
    if p.returncode:
        sys.exit("run6502 failed: " + p.stderr.decode())
    return p.stdout.decode("latin-1"), p.stderr.decode()


PROGRAMS = {
    "loop": """
10 REM a counted loop, GOTO and IF
20 I%=0 : S=0
30 I%=I%+1 : S=S+I%/3
40 IF I%<20 THEN 30
50 PRINT I%;S
60 IF S>70 THEN PRINT 1 ELSE PRINT 0
70 END
""",
    "precedence": """
10 PRINT 2+3*4;2*3+4;(2+3)*4;-2*3;10-2-3;100/10/5;7\\2;-7\\2;7 MOD 3;-7 MOD 3
20 PRINT 1<2;2<1;1=1;1<>1;3>=3;3<=2;NOT 0;NOT -1;5 AND 3;5 OR 3;5 XOR 3;5 EQV 3;5 IMP 3
30 PRINT 1+2<4 AND 2*3=6;-3+-2;--5;2*-3;1E+20*1E+20;-1/0;1/3*3;32767+1;-32768-1;181*181;200*200
40 PRINT SGN(-2.5);INT(-2.5);FIX(-2.5);ABS(-7);CINT(2.5);CINT(-2.5);CSNG(3);INT(7.999);SGN(0)
50 A=3 : B%=-4 : PRINT A*B%;A/B%;B%\\3;A+.5;B%*B%*B%*B%
60 IF 1 THEN IF 0 THEN PRINT 11 ELSE PRINT 22 ELSE PRINT 33
70 GOTO 90
80 PRINT 999
90 PRINT 1234567;12345678;.1;.01;1E-10;123456.7;-0
""",
}


# What GW-BASIC is known to print, independent of mbf.py.
GOLDEN = ("""
10 PRINT 2/3;10/3;10000000;1234567;12345678;.1;1E-10;3.14159265
20 PRINT 32767+1;-32768;100000*100;9999999;99999999;2.5;-.5;7\\2;-7 MOD 3;5 AND 3;NOT 0
30 PRINT 1E+38*10
""", " .6666667  3.333333  1E+07  1234567  1.234568E+07  .1  1E-10  3.141593 \r\n"
     " 32768 -32768  1E+07  9999999  1E+08  2.5 -.5  3 -1  1 -1 \r\n"
     "Overflow\r\n 1.701412E+38 \r\n")


def random_program(rng, lines=60):
    out = []
    n = 10
    for _ in range(lines):
        exprs = [basic.random_expr(rng, 3) for _ in range(rng.randint(1, 3))]
        out.append("%d PRINT %s" % (n, ";".join(exprs)))
        n += 10
    return "\n".join(out) + "\n"


# --- The oracle: the model against PC-BASIC -------------------------------------------------

ORACLE = os.path.join(HERE, "oracle")


def oracle_lines():
    """The expressions of oracle/math.bas: a function of a constant, or two constants and an operator,
    each a PRINT. Fixed, so the committed output stays the oracle's."""
    rng = random.Random(1983)
    out = []
    for f, xs in (("SQR", ["0", "1", "2", "3", "4", ".5", "10", "1E10", "1E-10", "123456.7", "2.5E37", "9.999999E-39"]),
                  ("EXP", ["0", "1", "-1", "2", ".5", "10", "-10", "50", "-50", "87", "88", "88.5", "-88", "-89",
                           "1E-5", "1E-8", "3.5", "-3.5", "20.25"]),
                  ("LOG", ["1", "2", "10", ".5", ".1", "100", "1E10", "1E-10", "1.000001", ".999999", "2.718282",
                           "1E38", "1E-38", "7", "3.14159"]),
                  ("SIN", ["0", "1", "-1", ".5", "2", "3", "3.141593", "1.570796", "4.712389", "6.283185", "10",
                           "100", "1000", "1E5", "-1E5", "1E-4", "1E-6", "12.5", "-.25", "7"]),
                  ("COS", ["0", "1", "-1", ".5", "2", "3", "3.141593", "1.570796", "4.712389", "6.283185", "10",
                           "100", "1000", "1E5", "1E-4", "12.5", "-.25", "7"]),
                  ("TAN", ["0", "1", "-1", ".5", "1.5", "2", "3", ".785398", "10", "100", "1E-4", "-.25"]),
                  ("ATN", ["0", "1", "-1", ".5", "2", "10", "100", "1E10", "-1E10", ".2679492", ".3", "1E-4",
                           "-.25", "1.7320508"]),
                  ("CDBL", ["1/3", ".1", "1.5", "123456.7"]), ("CSNG", ["1#/3", ".1#", "2.5#"]),
                  ("INT", ["-2.5#", "2.5#", "123456789.75#", "-1D-20", "1D20"]),
                  ("FIX", ["-2.5#", "2.5#", "-123456789.75#"])):
        for x in xs:
            out.append("%s(%s)" % (f, x))
    for e in ("2^10", "2^.5", "2^-1", "10^-3", "10^38", "(-2)^3", "(-2)^-3", "1.5^2.5", "3^0", "0^2", "9^.5",
              "2^127", "2^-127", "1.0001^10000", "(-1)^32768", ".5^30", "7^-2", "2^24", "2^23.5"):
        out.append(e)
    for e in ("1#/3", "2#/3", "1#/7", "10#/3", "1D16", "1D15", "123456789012345#", "12345678901234567890",
              "1.234567890123456D+20", "1D-20/3", "3.141592653589793#", ".1#", ".1#+.2#", "1#-1D-16",
              "1D16+1", "1D16-1", "99999999999999999#", "1.7D38", "1#/3*3", "2#/3-1#/3", "1D20*1D20",
              "1#+1D-15", "-1#/3", "12345678", "123456789", ".00001#", "1D-10", "65535#", "32768#",
              "1/3", "2/3", "10/3", "1/7", "1E38*10", "-1/3", "100000*100", "1.5E3", "-32768", "32767+1",
              "2.5!", "7%", ".5#", "1E-38/10"):
        out.append(e)
    for _ in range(25):
        f = rng.choice(["SQR", "EXP", "LOG", "SIN", "COS", "TAN", "ATN"])
        x = "%.6g" % (rng.uniform(0.01, 50) if f in ("SQR", "LOG") else rng.uniform(-20, 20))
        out.append("%s(%s)" % (f, x))
    for _ in range(10):
        out.append("%.5g^%.3g" % (rng.uniform(0.1, 5), rng.uniform(-6, 6)))
    for _ in range(10):
        a = "%.12g#" % rng.uniform(-1e6, 1e6)
        b = "%.12g#" % rng.uniform(-1e6, 1e6)
        out.append("%s%s%s" % (a, rng.choice("+-*/"), b))
    out += ["RND", "RND", "RND(1)", "RND(0)", "RND(-1)", "RND", "RND(-5)", "RND", "RND(.5)", "RND(-1E20)", "RND"]
    return out


def write_oracle():
    os.makedirs(ORACLE, exist_ok=True)
    with open(os.path.join(ORACLE, "math.bas"), "w") as f:
        n = 10
        for e in oracle_lines():
            f.write("%d PRINT %s\r\n" % (n, e))
            n += 10
    print("wrote %s: run sh oodos/gwbasic/tools/oracle.sh %s math" % (os.path.join(ORACLE, "math.bas"), ORACLE))


class Expr:
    """oracle/math.bas's expressions, evaluated by the model as GW-BASIC evaluates them: constants as
    $FIN reads them (as CRUNCH does), ^ above unary minus above * and / above + and -, each operator
    on the types APPLOP gives it, and the functions."""

    FUNCS = ("SQR", "EXP", "LOG", "SIN", "COS", "TAN", "ATN", "CDBL", "CSNG", "INT", "FIX", "RND")
    rndx = list(mbf.RNDCOP)  # RND's seed, through the program

    def __init__(self, text, soft):
        self.t, self.i, self.soft = text, 0, soft

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else ""

    def value(self):
        v = self.expr()
        if self.i != len(self.t):
            raise ValueError(self.t)
        return v

    def expr(self):
        v = self.term()
        while self.peek() in ("+", "-") and self.peek():
            op = self.t[self.i]
            self.i += 1
            v = self.apply(op, v, self.term())
        return v

    def term(self):
        v = self.unary()
        while self.peek() in ("*", "/") and self.peek():
            op = self.t[self.i]
            self.i += 1
            v = self.apply(op, v, self.unary())
        return v

    def unary(self):
        if self.peek() == "-":
            self.i += 1
            typ, v = self.unary()
            if typ == mbf.INT:
                return (mbf.SNG, mbf.neg(mbf.flt(v))) if v == -32768 else (mbf.INT, -v)
            v = list(v)
            v[-2] ^= 0x80
            return typ, v
        return self.power()

    def power(self):
        v = self.atom()
        while self.peek() == "^":
            self.i += 1
            w = self.unary()
            v = (mbf.SNG, mbf.fpwr(mbf.to_sng(*v), mbf.to_sng(*w), self.soft))
        return v

    def atom(self):
        S, D = mbf.SNG, mbf.DBL
        if self.peek() == "(":
            self.i += 1
            v = self.expr()
            assert self.peek() == ")"
            self.i += 1
            return v
        if self.t == "RND":
            self.i = 3
            Expr.rndx, v = mbf.rnd(Expr.rndx, mbf.ONE)
            return S, v
        for f in self.FUNCS:
            if self.t.startswith(f + "(", self.i):
                self.i += len(f)
                typ, v = self.atom()
                if f == "CDBL":
                    return D, mbf.to_dbl(typ, v)
                if f == "CSNG":
                    return S, mbf.to_sng(typ, v)
                if f in ("INT", "FIX"):
                    if typ == D:
                        if f == "FIX" and v[6] & 0x80 and v[7]:
                            return D, mbf.dneg(mbf.dint(mbf.dneg(v)))
                        return D, mbf.dint(v)
                    v = mbf.to_sng(typ, v)
                    return S, mbf.int_(v) if f == "INT" else mbf.fix(v)
                v = mbf.to_sng(typ, v)
                if f == "RND":
                    Expr.rndx, v = mbf.rnd(Expr.rndx, v)
                    return S, v
                soft = self.soft
                return S, {"SQR": lambda: mbf.sqr(v), "EXP": lambda: mbf.exp_(v, soft),
                           "LOG": lambda: mbf.log_(v, soft), "SIN": lambda: mbf.sin_(v, soft),
                           "COS": lambda: mbf.cos_(v, soft), "TAN": lambda: mbf.tan_(v, soft),
                           "ATN": lambda: mbf.atn_(v, soft)}[f]()
        typ, v, end, _ = mbf.fin(self.t[self.i:])
        if end <= 0:
            raise ValueError(self.t[self.i:])
        self.i += end
        return typ, v

    def apply(self, op, a, b):
        S, I, D = mbf.SNG, mbf.INT, mbf.DBL
        (ta, x), (tb, y) = a, b
        if ta == D or tb == D:
            f = {"+": mbf.dadd, "-": mbf.dsub, "*": mbf.fmuld, "/": mbf.ddiv}[op]
            return D, self.soft(f, mbf.to_dbl(ta, x), mbf.to_dbl(tb, y))
        if ta == I and tb == I and op != "/":
            r, t = {"+": mbf.iadd, "-": mbf.isub, "*": mbf.imult}[op](x, y)
            return t, r
        f = {"+": mbf.fadds, "-": mbf.fsubs, "*": mbf.fmuls, "/": mbf.fdivs}[op]
        return S, self.soft(f, mbf.to_sng(ta, x), mbf.to_sng(tb, y))


def model_expr(e, soft):
    return Expr(e, soft).value()


def model_print(e):
    """What PRINT e shows: the messages, then the number and its space."""
    soft = mbf.Soft()
    try:
        typ, v = model_expr(e, soft)
    except mbf.Error as err:
        return ["Illegal function call" if err.code == 5 else "?ERR %d" % err.code]
    lines = soft.msgs[:1]  # once only: a function's or ^'s mode, and PRINT has one operation
    return lines + [mbf.fout(v, typ) + " "]


def check_oracle():
    """The model's PRINT of each of oracle/math.bas's lines against PC-BASIC's output (oracle/math.out).
    Where they differ is listed in oracle/math.differ, each with why: a difference that isn't there
    fails, and so does one listed that has gone. Returns the lines and the failures."""
    out_path = os.path.join(ORACLE, "math.out")
    if not os.path.exists(out_path):
        return ["  no oracle/math.out: run check.py --write-oracle, then tools/oracle.sh"], 1
    got = [l.rstrip("\r\n") for l in open(out_path, encoding="latin-1")]
    known = {}
    differ = os.path.join(ORACLE, "math.differ")
    if os.path.exists(differ):
        for l in open(differ):
            l = l.split("#", 1)[0].strip()
            if l:
                known[l.split()[0]] = l
    groups, cur = [], []
    for l in got:  # each PRINT: the messages before it, then its number with its space
        cur.append(l)
        if l.endswith(" ") or l.startswith("Illegal"):
            groups.append(cur)
            cur = []
    exprs = oracle_lines()
    Expr.rndx = list(mbf.RNDCOP)
    lines, bad, seen, same = [], 0, set(), 0
    for e, g in zip(exprs, groups):
        w = model_print(e)
        if g == w:
            same += 1
            continue
        seen.add(e)
        if e not in known:
            bad += 1
            lines.append("  %-30s model %-36s PC-BASIC %s" % (e, " / ".join(w), " / ".join(g)))
    for e in known:
        if e not in seen:
            bad += 1
            lines.append("  %-30s listed in math.differ but the model and PC-BASIC agree" % e)
    want = exprs
    lines.insert(0, "  %d of %d the same, %d different as math.differ lists, %d not listed" % (
        same, len(want), len(seen & set(known)), len(seen - set(known))))
    return lines, bad


def image_end():
    """The flat image's last byte, from its map: it must end below the test data."""
    end = 0
    for l in open(os.path.splitext(IMAGE)[0] + ".map"):
        m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", l)
        if m and m.group(1) != "HEADER" and m.group(1) != "ZEROPAGE":
            end = max(end, int(m.group(3), 16))
    return end


def main():
    import argparse
    import tempfile
    global RUNNER, IMAGE, LABELS
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", help="run6502, built from run6502.cpp")
    ap.add_argument("--image", help="flat.bin (make test), with flat.map and flat.lbl beside it")
    ap.add_argument("--seed", type=int, default=1983)
    ap.add_argument("--count", type=int, default=400)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--programs", action="store_true", help="the spike's programs too, on the stub PRINT")
    ap.add_argument("--timing", action="store_true", help="print the cycles and stop")
    ap.add_argument("--write-oracle", action="store_true", help="write oracle/math.bas and stop")
    args = ap.parse_args()
    if args.write_oracle:
        write_oracle()
        return 0
    RUNNER, IMAGE = os.path.abspath(args.runner), os.path.abspath(args.image)
    LABELS = os.path.splitext(IMAGE)[0] + ".lbl"
    end = image_end()
    if end >= PROGRAM:
        sys.exit("the flat image ends at $%04X, past $%04X where the test data begin (test/flat.cfg)" % (end, PROGRAM))
    if args.timing:
        with tempfile.TemporaryDirectory() as work:
            print("A JSR to each, less loading its operands (300 a set, seed %d):" % args.seed)
            for label_, what, c in op_cycles(random.Random(args.seed), work):
                print("  %-7s %-56s %8.0f" % (label_, what, c))
            print("A BASIC loop, per pass (to 2000 less to 1000, over 1000):")
            for name, c in loop_cycles(work):
                print("  %-54s %7.0f" % (name, c))
        return 0
    rng = random.Random(args.seed)
    failed = 0
    bad = check_tables()
    print("Tables in p1/: %s" % ("ok" if not bad else "MISMATCH " + " ".join(bad)))
    failed += len(bad)
    with tempfile.TemporaryDirectory() as work:
        vs = vectors(rng, args.count)
        res = run_vectors(vs, work)
        bad, per = compare(vs, res, args.v)
        print("Math vectors: %d, %d mismatched" % (len(vs), bad))
        for op in sorted(per, key=lambda o: OPS[o]):
            print("  %-7s %5d  %s" % (op, per[op][0], "ok" if not per[op][1] else "%d BAD" % per[op][1]))
        failed += bad
        print("The model against exact arithmetic (operands 2^-32..2^32):")
        lines, bad = exactness(random.Random(args.seed), 2000)
        for l in lines:
            print(l)
        failed += bad
        print("The model against PC-BASIC (oracle/math.bas, math.out; math.differ lists where they differ):")
        lines, bad = check_oracle()
        for l in lines[:20]:
            print(l)
        failed += bad
        print("The functions against Python's math (2,000 each):")
        lines, bad = accuracy(random.Random(args.seed), 2000)
        for l in lines:
            print(l)
        failed += bad
        if args.programs:
            progs = dict(PROGRAMS)
            for k in range(4):
                progs["random%d" % k] = random_program(rng)
            got, _ = run_program(GOLDEN[0], work)
            ok = got == GOLDEN[1] and basic.run(GOLDEN[0]) == GOLDEN[1]
            print("Program %-10s %s" % ("golden", "ok" if ok else "MISMATCH %r" % got))
            failed += not ok
            for name, src in progs.items():
                got, _ = run_program(src, work)
                want = basic.run(src)
                ok = got == want
                print("Program %-10s %s (%d lines of output)" % (name, "ok" if ok else "MISMATCH", want.count("\n")))
                if not ok:
                    failed += 1
                    gl, wl = got.split("\n"), want.split("\n")
                    for i in range(max(len(gl), len(wl))):
                        g = gl[i] if i < len(gl) else "<none>"
                        w = wl[i] if i < len(wl) else "<none>"
                        if g != w:
                            print("  line %d: 6502 %r" % (i + 1, g))
                            print("  line %d: want %r" % (i + 1, w))
                            if not args.v:
                                break
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
