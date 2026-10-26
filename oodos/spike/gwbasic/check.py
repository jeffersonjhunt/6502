"""Checks the spike's 6502 GW-BASIC code against mbf.py, on the emulator.

    python3 -I check.py [--seed N] [--count N] [-v]

0. Tables: math1.s's $FOTB and powers of ten must equal mbf.py's. The
   vectors alone wouldn't catch a wrong low byte there.
1. Math vectors: thousands of operands, random and chosen at the edges, for
   each translated routine. run6502 runs them all (harness.s) and the
   results must equal mbf.py's byte for byte: the FAC, the type, the error,
   and $FOUT's text.
2. The model against exact arithmetic: how often each operation is the
   correctly rounded result, and its worst error in units of the last place.
3. Programs: tokenised BASIC run through NEWSTT, FRMEVL and the rest, the
   output compared with basic.py's reading of the same program, which uses
   mbf.py for the arithmetic. One program is generated: random expressions.

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

RUNNER = os.path.join(HERE, "run6502")
IMAGE = os.path.join(HERE, "spike.bin")
VLEN = 48
PER_RUN = 300

OPS = {
    "add": 1, "sub": 2, "mul": 3, "div": 4, "cmp": 5, "flt": 6, "cint": 7,
    "int": 8, "fix": 9, "iadd": 10, "isub": 11, "imul": 12, "idiv": 13,
    "imod": 14, "icmp": 15, "intdiv": 16, "fout": 17, "dmul": 18, "ddiv": 19,
    "csd": 20, "sgn": 21, "neg": 22, "abs": 23, "mul10": 24, "div10": 25,
}


def check_tables():
    """math1.s's $FOTB and powers of ten must be the model's, which come from
    MATH1.ASM and from exact arithmetic, not from math1.s. Returns the
    mismatches."""
    src = open(os.path.join(HERE, "math1.s")).read().split("\n")

    def rows(label, n):
        i = [k for k, l in enumerate(src) if l.startswith(label + ":")][0]
        out = []
        for l in src[i:i + n]:
            vals = re.search(r"\.byte\s+(.*)", l).group(1)
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
    return bad


# --- Operands --------------------------------------------------------------------


def rsingle(rng, lo=1, hi=255):
    return [rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(lo, hi + 1)]


def single(x):
    return mbf.nearest(Fraction(x))


def rdouble(rng, lo=0x50, hi=0xB0):
    return [rng.randrange(256) for _ in range(7)] + [rng.randrange(lo, hi + 1)]


EDGE_S = [[0, 0, 0, 0], [0, 0, 0, 0x81], [0, 0, 0x80, 0x81], [0xFF, 0xFF, 0x7F, 0xFF], [0xFF, 0xFF, 0xFF, 0xFF],
          [0, 0, 0, 1], [0, 0, 0x80, 1], [0xFF, 0xFF, 0x7F, 0x80], [1, 0, 0, 0x98], [0, 0, 0x80, 0x90]]
EDGE_I = [0, 1, -1, 2, -2, 7, 10, 127, 128, 255, 256, 181, 182, -181, 32767, -32767, -32768, 16384, -16384, 1000]


def vectors(rng, count):
    """(op, typeA, A, B) tuples."""
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
        if a is not None:
            v.append(("fout", S, a, None))
    for i in EDGE_I + [rng.randint(-32768, 32767) for _ in range(count // 4)]:
        v.append(("fout", I, ints(i), None))
    return v


def ints(i):
    i &= 0xFFFF
    return [i & 0xFF, i >> 8, 0, 0]


# --- The model's answer -----------------------------------------------------------


def model(op, ta, a, b):
    """(valtyp, bytes or int, cmp, error, text)."""
    S, I, D = mbf.SNG, mbf.INT, mbf.DBL
    ia = mbf.i16(a[0] | (a[1] << 8))
    ib = mbf.i16(b[0] | (b[1] << 8)) if b else 0
    typ = {"cint": I, "flt": S, "csd": S, "icmp": I, "cmp": S, "intdiv": S}.get(op, ta)
    try:
        if op == "add":
            return S, mbf.fadds(a, b), 0, 0, None
        if op == "sub":
            return S, mbf.fsubs(a, b), 0, 0, None
        if op == "mul":
            return S, mbf.fmuls(a, b), 0, 0, None
        if op == "div":
            return S, mbf.fdivs(a, b), 0, 0, None
        if op == "cmp":
            return S, None, mbf.fcomp(a, b), 0, None
        if op == "flt":
            return S, mbf.flt(ia), 0, 0, None
        if op == "cint":
            return I, mbf.cint(a), 0, 0, None
        if op == "int":
            return S, mbf.int_(a), 0, 0, None
        if op == "fix":
            return S, mbf.fix(a), 0, 0, None
        if op in ("iadd", "isub", "imul", "idiv", "imod"):
            f = {"iadd": mbf.iadd, "isub": mbf.isub, "imul": mbf.imult, "idiv": mbf.idiv, "imod": mbf.imod}[op]
            r, t = f(ia, ib)
            return t, r, 0, 0, None
        if op == "icmp":
            return I, None, mbf.icomp(ia, ib), 0, None
        if op == "intdiv":
            return S, mbf.intdiv(ia, ib), 0, 0, None
        if op == "dmul":
            return D, mbf.fmuld(a, b), 0, 0, None
        if op == "ddiv":
            return D, mbf.ddiv(a, b), 0, 0, None
        if op == "csd":
            return S, mbf.csd(a), 0, 0, None
        if op == "sgn":
            if ta == I:
                return I, (ia > 0) - (ia < 0), 0, 0, None
            return I, 0 if a[3] == 0 else (-1 if a[2] & 0x80 else 1), 0, 0, None
        if op == "neg":
            if ta == I:
                if ia == -32768:
                    return S, mbf.neg(mbf.flt(ia)), 0, 0, None
                return I, -ia, 0, 0, None
            return S, mbf.neg(a), 0, 0, None
        if op == "abs":
            if ta == I:
                if ia == -32768:
                    return S, mbf.neg(mbf.flt(ia)), 0, 0, None
                return I, abs(ia), 0, 0, None
            return S, a[:2] + [a[2] & 0x7F, a[3]], 0, 0, None
        if op == "mul10":
            return S, mbf.mul10(a), 0, 0, None
        if op == "div10":
            return S, mbf.div10(a), 0, 0, None
        if op == "fout":
            return ta, None, 0, 0, mbf.fout(ia if ta == I else a, ta)
    except mbf.Overflow as o:
        if op in ("idiv", "imod"):
            typ = S
        return o.typ if op in ("dmul", "ddiv") else typ, o.result(), 0, 0, None
    except mbf.Error as e:
        return None, None, 0, e.code, None
    raise ValueError(op)


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
        p = subprocess.run([RUNNER, IMAGE, "--load", "7FF0:" + cfile, "--load", "8000:" + vec,
                            "--dump", "8000:%X:%s" % (len(data), res)], capture_output=True, text=True)
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
        typ, val, cmpr, err, txt = model(op, ta, a, b)
        rtyp, rcmp, rerr = r[20], r[21], r[22]
        fac = list(r[24:32])
        ok = True
        if err or rerr:
            ok = err == rerr
        elif op in ("cmp", "icmp"):
            ok = (rcmp if rcmp < 128 else rcmp - 256) == cmpr
        elif op == "fout":
            got = bytes(r[32:48]).split(b"\0")[0].decode("latin-1")
            ok = got == txt
        else:
            if rtyp != typ:
                ok = False
            elif typ == mbf.INT:
                ok = mbf.i16(fac[4] | (fac[5] << 8)) == mbf.i16(val)
            elif typ == mbf.SNG:
                ok = fac[4:8] == val
            else:
                ok = fac == val
        per.setdefault(op, [0, 0])[0] += 1
        if not ok:
            per[op][1] += 1
            bad += 1
            if verbose or bad <= 12:
                print("MISMATCH %s %s %s: model %s %s cmp=%s err=%s %r; 6502 type=%d cmp=%d err=%d fac=%s text=%r" % (
                    op, hx(a), hx(b) if b else "", typ, hx(val) if isinstance(val, list) else val, cmpr, err, txt,
                    rtyp, rcmp, rerr, hx(fac), bytes(r[32:48]).split(b"\0")[0]))
    return bad, per


def hx(b):
    return " ".join("%02X" % x for x in b) if b is not None else "-"


# --- The model against exact arithmetic ----------------------------------------------


def exactness(rng, n):
    lines = []
    for name, f, g, bits in (("add", mbf.fadds, lambda x, y: x + y, 24),
                             ("sub", mbf.fsubs, lambda x, y: x - y, 24),
                             ("mul", mbf.fmuls, lambda x, y: x * y, 24),
                             ("div", mbf.fdivs, lambda x, y: x / y, 24),
                             ("dmul", mbf.fmuld, lambda x, y: x * y, 56),
                             ("ddiv", mbf.ddiv, lambda x, y: x / y, 56)):
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
        lines.append("  %-5s %5d/%d correctly rounded; worst %.3f ulp" % (name, same, total, float(worst)))
    return lines


# --- Programs ---------------------------------------------------------------------------


def run_program(src, workdir, counts=()):
    prog = basic.tokenise(src)
    pfile = os.path.join(workdir, "prog.vec")
    cfile = os.path.join(workdir, "ctrl.vec")
    open(pfile, "wb").write(prog)
    open(cfile, "wb").write(bytes([1, 0, 0, 0]))
    args = [RUNNER, IMAGE, "--load", "6100:" + pfile, "--load", "7FF0:" + cfile, "--max", "400000000"]
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


def main():
    import argparse
    import tempfile
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1983)
    ap.add_argument("--count", type=int, default=400)
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failed = 0
    bad = check_tables()
    print("Tables in math1.s: %s" % ("ok" if not bad else "MISMATCH " + " ".join(bad)))
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
        for l in exactness(random.Random(args.seed), 2000):
            print(l)
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
