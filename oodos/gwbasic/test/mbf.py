# Derived from Microsoft GW-BASIC (1983).
# Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.

"""GW-BASIC's Microsoft Binary Format arithmetic, modelled in Python.

The reference the port's 6502 math is checked against (check.py), from the
size spike's: single, integer and double arithmetic, number input ($FIN) and
output ($FOUT), and the functions (SQR, ^, EXP, LOG, SIN, COS, TAN, ATN,
RND), each with the source's own polynomial, constants and order. It is
written from the algorithms of MATH1.ASM and MATH2.ASM, not from the 6502
translation: each function says what GW-BASIC computes, in Python's own
integers, including what it keeps and drops (the single multiply's
truncated partial product, the rounding byte's sticky bit, which overflows
give a signed infinity). Where GW-BASIC is exact, the model is too, and
exact_* below check that against Python's fractions.

Numbers are lists of bytes in memory order, as in the FAC: a single is
[low, mid, sign|high, exponent], a double 4 more low bytes in front. An
exponent of 0 means zero; otherwise the value is 0.1mmm... (binary, the 1
implied) times 2^(exponent-128).

Results are (bytes, valtyp, error, printed): error is GW-BASIC's error
number for an error that stops the program (0 for none), printed the
message an overflow or division by zero prints and carries on from.
"""

from fractions import Fraction

INT, SNG, DBL = 2, 4, 8
ERR_OV = 6
INF_S = [0xFF, 0xFF, 0x7F, 0xFF]
INF_D = [0xFF] * 6 + [0x7F, 0xFF]


class Overflow(Exception):
    """A soft overflow: the FAC becomes an infinity and BASIC carries on."""

    def __init__(self, bh, sign, typ=SNG, msg="Overflow"):
        super().__init__(msg)
        self.bh, self.sign, self.typ, self.msg = bh, sign, typ, msg

    def result(self):
        # DOINF: +infinity when BH is zero (zero to a negative power), else
        # the sign's.
        neg = self.bh != 0 and (self.sign & 0x80)
        if self.typ == DBL:
            b = list(INF_D)
        else:
            b = list(INF_S)
        if neg:
            b[-2] |= 0x80
        return b


class Error(Exception):
    """An error that stops the program, such as CINT's Overflow."""

    def __init__(self, code):
        super().__init__(code)
        self.code = code


# --- Singles: packing, normalising, rounding ---------------------------------


def mant24(b):
    return ((b[2] | 0x80) << 16) | (b[1] << 8) | b[0]


def pack24(m, exp, sign):
    return [m & 0xFF, (m >> 8) & 0xFF, ((m >> 16) & 0x7F) | (sign & 0x80), exp & 0xFF]


ZERO4 = [0, 0, 0, 0]


def rounm(m24, ah, exp, sign, bh):
    """$ROUNM: round the 24-bit mantissa by the byte below it; a half to even."""
    if ah + 0x80 < 0x100:
        return pack24(m24, exp, sign)
    half = ((ah + 0x80) & 0xFF) == 0
    dx = (m24 & 0xFFFF) + 1
    if dx != 0x10000:
        m24 = (m24 & 0xFF0000) | dx
        if half:
            m24 &= ~1
        return pack24(m24, exp, sign)
    bl = ((m24 >> 16) + 1) & 0xFF
    if bl:
        return pack24(bl << 16, exp, sign)
    exp += 1
    if exp == 0x100:
        raise Overflow(bh, sign)
    return pack24(0x800000, exp, sign)


def rouns(m32, exp, sign, bh):
    """$ROUNS: the top 3 bits of the byte below the mantissa round it."""
    return rounm(m32 >> 8, m32 & 0xE0, exp, sign, bh)


def norms(m32, exp, sign):
    """$NORMS: shift left until the top bit is set (bytes first), then round."""
    bh, cx = exp, 4
    while not (m32 & 0x80000000):
        if (m32 >> 24) == 0:
            if bh <= 8:
                return ZERO4
            bh -= 8
            m32 = (m32 << 8) & 0xFFFFFFFF
            cx -= 1
            if cx == 0:
                return ZERO4
            continue
        m32 = (m32 << 1) & 0xFFFFFFFF
        bh -= 1
        if bh == 0:
            return ZERO4
    return rouns(m32, bh, sign, bh)


# --- Single add, subtract, multiply, divide, compare ---------------------------


def fadds(a, fac):
    """$FADDS: a + fac (a in BXDX)."""
    if a[3] == 0:
        return list(fac)
    if fac[3] == 0:
        return list(a)
    al, ah, si = fac[2], fac[3], fac[0] | (fac[1] << 8)
    bl, bh, dx = a[2], a[3], a[0] | (a[1] << 8)
    sign, exp = al, ah
    if ah < bh:
        cl = bh - ah
        exp, sign = bh, bl
        al, ah, si, bl, bh, dx = bl, bh, dx, al, ah, si
    else:
        cl = ah - bh
    sub = (al ^ bl) & 0x80
    al |= 0x80
    bl |= 0x80
    ov = 0
    while cl:
        if cl >= 25:
            return [si & 0xFF, si >> 8, (al & 0x7F) | (sign & 0x80), exp]
        if cl >= 8:
            old, ov = ov, dx & 0xFF
            if old:
                ov |= 0x20
            dx = (dx >> 8) | (bl << 8)
            bl = 0
            cl -= 8
            if ov & 0x1F:
                ov |= 0x20
            continue
        while cl:
            v = ((bl << 24) | (dx << 8) | ov) >> 1
            bl, dx, ov = v >> 24, (v >> 8) & 0xFFFF, v & 0xFF
            if ov & 0x10:
                ov |= 0x20
            cl -= 1
    if sub:
        t = ((al << 24) | (si << 8)) - ((bl << 24) | (dx << 8) | ov)
        if t < 0:
            sign ^= 0xFF
            t = -t
        return norms(t, exp, sign)
    v = ((bl << 16) | dx) + ((al << 16) | si)
    if v >> 24:
        exp += 1
        if exp == 0x100:
            raise Overflow(0, sign)
        return rouns(((v << 8) | ov) >> 1, exp, sign, 0)
    return rouns((v << 8) | ov, exp, sign, 0)


def fsubs(a, fac):
    """$FSUBS: a - fac."""
    if fac[3] == 0:
        return list(a)
    f = list(fac)
    f[2] ^= 0x80
    return fadds(a, f)


def aexps(bh, bl, fexp, fhi):
    """$AEXPS: the product's exponent and sign; None for underflow."""
    sign = bl ^ fhi
    ax = bh + fexp - 0x101
    if ax >= 0:
        if ax >= 0x80:
            raise Overflow(bh, sign)
        return ax + 0x80, sign
    ax += 0x80
    if ax < 0:
        return None, sign
    return ax, sign


def fmuls(a, fac):
    """$FMULS: a * fac. The 8086's partial products give floor(A*B / 2^16)."""
    if fac[3] == 0 or a[3] == 0:
        return ZERO4
    exp, sign = aexps(a[3], a[2], fac[3], fac[2])
    if exp is None:
        return ZERO4
    r = (mant24(a) * mant24(fac)) >> 16
    if r & 0x80000000:
        exp += 1
        if exp == 0x100:
            raise Overflow(0, sign)
    else:
        r = (r << 1) & 0xFFFFFFFF
    return rouns(r, exp, sign, 0)


def s8(x):
    return x - 0x100 if x & 0x80 else x


def fdivs(a, fac):
    """$FDIVS: a / fac: a 32-bit quotient, a sticky bit, round to even."""
    if fac[3] == 0:
        raise Overflow(a[3], a[2], msg="Division by zero")
    if a[3] == 0:
        return ZERO4
    sign = a[2] ^ fac[2]
    e = s8(a[3] ^ 0x80) - s8(fac[3] ^ 0x80)
    if e > 127:
        raise Overflow(a[3], sign)
    if e < -128:
        return ZERO4
    ah = (e & 0xFF) ^ 0x80
    q, rem = divmod(mant24(a) << 31, mant24(fac))
    if rem:
        q |= 1
    if q & 0x80000000:
        ah += 1
        if ah == 0x100:
            raise Overflow(q >> 24, sign)
    else:
        q = (q << 1) & 0xFFFFFFFF
        if ah == 0:
            return ZERO4
    dl, m = q & 0xFF, q >> 8
    if dl > 0x80 or (dl == 0x80 and (m & 1)):
        m += 1
        if m == 1 << 24:
            m = 0
            ah += 1
            if ah == 0x100:
                raise Overflow(0, sign)
    return pack24(m, ah, sign)


def fcomp(fac, b):
    """$FCOMP: 1, 0 or -1 as fac >, = or < b."""
    if b[3] == 0:
        return 0 if fac[3] == 0 else (-1 if fac[2] & 0x80 else 1)
    if fac[3] == 0:
        return 1 if b[2] & 0x80 else -1
    if (fac[2] ^ b[2]) & 0x80:
        return -1 if fac[2] & 0x80 else 1
    x = (fac[3], fac[2], fac[1], fac[0])
    y = (b[3], b[2], b[1], b[0])
    if fac[2] & 0x80:
        x, y = y, x
    return (x > y) - (x < y)


# --- Conversions ----------------------------------------------------------------


def flt(i):
    """$FLT: a 16-bit integer to a single."""
    i &= 0xFFFF
    sign = 0
    if i & 0x8000:
        sign, i = 0x80, (-i) & 0xFFFF
    return norms(i << 16, 0x90, sign)


def cint(fac):
    """$CINC: to the nearest integer, halves away from zero; else Overflow."""
    if fac[3] < 0x80:
        return 0
    ch = fac[3] - 0x80
    if ch > 16:
        raise Error(ERR_OV)
    bx = (fac[2] << 8) | fac[1]
    if ch == 16:
        if bx == 0x8000 and not (fac[0] & 0x80):
            return -32768
        raise Error(ERR_OV)
    v = (bx | 0x8000) >> (15 - ch)
    v = (v >> 1) + (v & 1)
    if v == 0x8000:
        if fac[2] & 0x80:
            return -32768
        raise Error(ERR_OV)
    return -v if fac[2] & 0x80 else v


def int_(fac):
    """$INT: the largest integer not above it, as a single."""
    exp = fac[3]
    if exp >= 0x98 or exp == 0:
        return list(fac)
    sign = fac[2]
    m = mant24(fac)
    if sign & 0x80:
        m -= 1
    m >>= 0x98 - exp
    if sign & 0x80:
        m += 1
    return norms(m << 8, 0x98, sign)


def neg(fac):
    f = list(fac)
    f[-2] ^= 0x80
    return f


def fix(fac):
    if fac[2] & 0x80 and fac[3]:
        return neg(int_(neg(fac)))
    return int_(fac)


def qint(fac):
    """$QINT for a positive single: its integer part, truncated."""
    if fac[3] == 0:
        return 0
    return mant24(fac) >> (0x98 - fac[3])


# --- Integers ---------------------------------------------------------------------


def i16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def iadd(d, b):
    s = d + b
    if -32768 <= s <= 32767:
        return s, INT
    return fadds(flt(d), flt(b)), SNG


def isub(d, b):
    if b == -32768:
        r = neg(fsubs([0, 0, 0x80, 0x90], flt(d)))
        return cint(r), INT
    return iadd(d, -b)


def imult(d, b):
    p = d * b
    if -32768 <= p <= 32767:
        return p, INT
    return fmuls(flt(d), flt(b)), SNG


def idiv(d, b):
    """\\: truncated; -32768 \\ -1 is the single 32768."""
    if b == 0:
        raise Overflow(1, (d >> 8) & 0xFF, msg="Division by zero")
    if d == -32768 and b == -1:
        return neg(flt(d)), SNG
    q = abs(d) // abs(b)
    return (q if (d < 0) == (b < 0) else -q), INT


def imod(d, b):
    if b == d:
        return 0, INT
    if d == -32768 and b in (1, -1):
        return 0, INT
    if b == 0:
        raise Overflow(1, (d >> 8) & 0xFF, msg="Division by zero")
    r = abs(d) % abs(b)
    return (-r if d < 0 else r), INT


def icomp(d, b):
    return (d < b) - (d > b)


def intdiv(d, b):
    return fdivs(flt(d), flt(b))


# --- Doubles: multiply, divide, to single ------------------------------------------


def mant56(b):
    return ((b[6] | 0x80) << 48) | int.from_bytes(bytes(b[0:6]), "little")


def fmuld(fac, arg):
    """$FMULD: the exact product, the top 64 bits kept with a sticky bit."""
    if fac[7] == 0:
        return list(fac)
    if arg[7] == 0:
        return [0] * 8
    try:
        exp, sign = aexps(arg[7], arg[6], fac[7], fac[6])
    except Overflow as o:
        o.typ = DBL
        raise
    if exp is None:
        return list(fac[0:4]) + [0, 0, 0, 0]
    p = (mant56(arg) * mant56(fac)) << 8
    x = (p >> 56) & ((1 << 64) - 1)
    if p & ((1 << 56) - 1):
        x |= 0x20
    if x >> 63:
        exp += 1
        if exp == 0x100:
            raise Overflow(1, sign, DBL)
    else:
        x = (x << 1) & ((1 << 64) - 1)
    return dround(x, exp, sign)


def dround(x, exp, sign):
    """$ROUND: x is the 7 mantissa bytes over the overflow byte."""
    x += 0x80
    if x >> 64:
        exp += 1
        if exp == 0x100:
            raise Overflow(1, sign, DBL)
        x = 0x8000 << 48
    if (x & 0xFF) == 0:
        x &= ~0x100
    m = x >> 8
    b = list(m.to_bytes(7, "little"))
    b[6] = (b[6] & 0x7F) | (sign & 0x80)
    return b + [exp]


def ddiv(fac, arg):
    """DDIV: fac / arg: a 64-bit quotient, a sticky bit, round to even."""
    sign = fac[6] ^ arg[6]
    if arg[7] == 0:
        raise Overflow(1, sign, DBL, msg="Division by zero")
    if fac[7] == 0:
        return [0] * 8
    e = s8(fac[7] ^ 0x80) - s8(arg[7] ^ 0x80)
    if e > 127:
        raise Overflow(1, sign, DBL)
    if e < -128:
        return [0] * 8
    ah = (e & 0xFF) ^ 0x80
    q, rem = divmod(mant56(fac) << 63, mant56(arg))
    if rem:
        q |= 1
    if q >> 63:
        ah += 1
        if ah == 0x100:
            raise Overflow((q >> 24) & 0xFF, sign, DBL)
    else:
        q = (q << 1) & ((1 << 64) - 1)
        if ah == 0:
            return [0] * 8
    dl, m = q & 0xFF, q >> 8
    if dl > 0x80 or (dl == 0x80 and (m & 1)):
        m += 1
        if m == 1 << 56:
            m = 0
            ah += 1
            if ah == 0x100:
                raise Overflow(0, sign, DBL)
    b = list(m.to_bytes(7, "little"))
    b[6] = (b[6] & 0x7F) | (sign & 0x80)
    return b + [ah]


def csd(fac8):
    """$CSD: double to single; the next byte rounds it, a half upwards."""
    m = ((fac8[6] | 0x80) << 16) | (fac8[5] << 8) | fac8[4]
    return rounm(m, fac8[3] | 0x40, fac8[7], fac8[6], 1)


def cds(fac4):
    return [0, 0, 0, 0] + list(fac4)


# --- Output: $FOUT -------------------------------------------------------------------

DP = {}  # 10^n, n = 0..38, as $DP00.. hold them: filled below, after nearest()
DPM01 = None  # 10^-1


def mdpten(fac4, n, soft=None):
    """MDPTEN for a single: times 10^n in double precision, back to single.
    With soft, an overflow in the double goes on to $CSD, as GW-BASIC does."""
    call = soft or (lambda f, *a: f(*a))
    d = cds(fac4)
    if n >= 0:
        if d[7] == 0:
            return ZERO4
        if n >= 39:
            raise Overflow(1, d[6])
        return call(csd, call(fmuld, d, DP[n]))
    if n < -38:
        n += 38
        if n < -38:
            return ZERO4
        d = ddiv(d, DP[-n])
        n = -38
    return call(csd, ddiv(d, DP[-n]))


def mdpten_d(d, n):
    """MDPTEN for a double: times 10^n, staying double. Zero is the single
    part zeroed ($ZERO), the low half left as it was."""
    if n >= 0:
        if d[7] == 0:
            return list(d[0:4]) + [0, 0, 0, 0]
        if n >= 39:
            raise Overflow(1, 0, DBL)
        return fmuld(d, DP[n])
    if n < -38:
        n += 38
        if n < -38:
            return list(d[0:4]) + [0, 0, 0, 0]
        d = ddiv(d, DP[-n])
        n = -38
    return ddiv(d, DP[-n])


def mul10(f):
    return fmuls(f, csd(DP[1]))


def div10(f):
    return fmuls(f, csd(DPM01))


# $FOTB, by exponent: the power of ten $FOTNV multiplies by to bring a number
# near 10^6..10^7. Copied from MATH1.ASM (CON86): a hand-made table, which
# floor(7 - (e-128)*log10(2)) misses by one in 15 places.
FOTB = [
    38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38,
    38, 37, 37, 37, 36, 36, 36, 35,
    35, 35, 34, 34, 34, 34, 33, 33,
    33, 32, 32, 32, 31, 31, 31, 31,
    30, 30, 30, 29, 29, 29, 29, 28,
    28, 28, 27, 27, 27, 26, 26, 26,
    25, 25, 25, 25, 24, 24, 24, 23,
    23, 23, 23, 22, 22, 22, 22, 21,
    21, 21, 20, 20, 20, 19, 19, 19,
    19, 18, 18, 18, 17, 17, 17, 16,
    16, 16, 16, 15, 15, 15, 14, 14,
    14, 13, 13, 13, 13, 12, 12, 12,
    11, 11, 11, 10, 10, 10, 10, 9,
    9, 9, 8, 8, 8, 7, 7, 7,
    6, 6, 6, 6, 5, 5, 5, 4,
    4, 4, 3, 3, 3, 3, 2, 2,
    2, 1, 1, 1, 0, 0, 0, 0,
    -1, -1, -1, -2, -2, -2, -3, -3,
    -3, -3, -4, -4, -4, -5, -5, -5,
    -6, -6, -6, -6, -7, -7, -7, -8,
    -8, -8, -9, -9, -9, -9, -10, -10,
    -10, -11, -11, -11, -12, -12, -12, -12,
    -13, -13, -13, -14, -14, -14, -15, -15,
    -15, -15, -16, -16, -16, -17, -17, -17,
    -18, -18, -18, -18, -19, -19, -19, -20,
    -20, -20, -21, -21, -21, -21, -22, -22,
    -22, -23, -23, -23, -24, -24, -24, -25,
    -25, -25, -25, -26, -26, -26, -27, -27,
    -27, -28, -28, -28, -28, -29, -29, -29,
    -30, -30, -30, -31, -31, -31, -31, -32,
]
FOSTB = [1000000, 100000, 10000]
FOITB = [10000, 1000, 100, 10, 1]


def fout(val, typ):
    """$FOUT: the text PRINT shows for a number (no trailing space)."""
    buf = [0] * 40
    bx = 1
    buf[1] = ord(" ")
    if typ == INT:
        s = (val > 0) - (val < 0)
    else:
        s = 0 if val[-1] == 0 else (-1 if val[-2] & 0x80 else 1)
    if s < 0:
        buf[1] = ord("-")
        if typ == INT:
            if val == -32768:
                typ, val = SNG, neg(flt(val))
            else:
                val = -val
        else:
            val = neg(val)
    bx = 2
    buf[2] = ord("0")
    if s == 0:
        return " 0"
    if typ == INT:
        return fout_int(buf, val)
    return fofmt(buf, val, typ)


def fotci(buf, bx, v, ndig, powers, cx):
    for p in powers[len(powers) - ndig:]:
        bx, cx = foted(buf, bx, cx)
        d = 0
        while v >= p:
            v -= p
            d += 1
        buf[bx] = ord("0") + d
        bx += 1
    bx, cx = foted(buf, bx, cx)
    buf[bx] = 0
    return bx, cx


def foted(buf, bx, cx):
    """$FOTED: the point when CH counts down to it, commas by CL."""
    ch, cl = cx
    ch = (ch - 1) & 0xFF
    if ch == 0:
        buf[bx] = ord(".")
        return bx + 1, (0, 0)
    cl = (cl - 1) & 0xFF
    if cl == 0:
        buf[bx] = ord(",")
        return bx + 1, (ch, 3)
    return bx, (ch, cl)


def fotan(buf, bx, cx):
    """$FOTAN: as $FOTED, but a point and leading zeros if CH is 0 or less."""
    ch, cl = cx
    ch = (ch - 1) & 0xFF
    if ch & 0x80:
        buf[bx] = ord(".")
        while True:
            bx += 1
            buf[bx] = ord("0")
            ch = (ch + 1) & 0xFF
            if ch == 0:
                break
        return bx + 1, (0, 0), True
    if ch == 0:
        buf[bx] = ord(".")
        return bx + 1, (0, 0), True
    cl = (cl - 1) & 0xFF
    if cl == 0:
        buf[bx] = ord(",")
        return bx + 1, (ch, 3), True
    return bx, (ch, cl), False


def fout_int(buf, v):
    fotci(buf, 2, v, 5, FOITB, (7, 0))
    # $FOTZS: leading zeros become spaces; the sign moves up to the digits.
    sign = buf[1]
    bx = 1
    while True:
        buf[bx] = ord(" ")
        bx += 1
        while buf[bx] == ord(" "):
            bx += 1
        if buf[bx] == 0:
            bx -= 1
            buf[bx] = ord("0")
            break
        if chr(buf[bx]) not in "0,":
            break
    bx -= 1
    buf[bx] = sign
    return text(buf, bx)


def text(buf, bx):
    s = ""
    while buf[bx]:
        s += chr(buf[bx])
        bx += 1
    return s


def fotnv(f, typ=SNG):
    """$FOTNV: bracket into 10^6..10^7 (a double then to 10^15..10^16); the
    decimal exponent that takes."""
    e = 0
    if typ == DBL:
        while FOTB[f[7]]:
            p = FOTB[f[7]]
            e -= p
            f = mdpten_d(f, p)
        if fcomp(f[4:], DP[6][4:]) < 0:
            f = fmuld(f, DP[1])
            e -= 1
        f = fmuld(f, DP[9])
        e -= 9
        if dcmpm(f, HIDBL) > 0:
            f = fmuld(f, DPM01)
            e += 1
        return f, e
    while FOTB[f[3]]:
        p = FOTB[f[3]]
        e -= p
        f = mdpten(f, p)
    if fcomp(f, DP[6][4:]) < 0:
        f = mul10(f)
        e -= 1
    return f, e


def sigd(f, cl, typ=SNG):
    """$SIGD: CL up by one for each zero digit at the right of the bracket."""
    if typ == DBL:
        if s8((f[7] - 0xB8) & 0xFF) < 0:
            f = dint(faddd(f, DHALF))
        z = f
        while True:
            z1 = z
            g = dint(fmuld(z, DPM01))
            z = g
            g = fmuld(g, DP[1])
            if dcmpm(g, z1) != 0:
                return cl
            cl += 1
    if s8((f[3] - 0x98) & 0xFF) < 0:
        f = int_(fadds([0, 0, 0, 0x80], f))
    z = f
    while True:
        z1 = z
        g = int_(div10(z))
        z = g
        g = mul10(g)
        if fcomp(g, z1) != 0:
            return cl
        cl += 1


def fofmt(buf, f, typ=SNG):
    al = None
    f, al = fotnv(f, typ)
    ch = cl = 17 if typ == DBL else 8
    if al <= 0:
        if al + (16 if typ == DBL else 7) >= 0:
            path = "ffm18"
        else:
            cl = sigd(f, cl, typ)
            ah = al + cl
            if ah <= 0:
                path = "ffm20"
            else:
                ch = ch + al
                path = "ffm19"
    else:
        path = "ffm20"
    if path == "ffm18":
        al = al + ch
        ch += 1
        if ch < al:
            ch = 3
            path = "ffm30"
        else:
            ch = al
            path = "ffm19"
    if path == "ffm19":
        ch += 1
        al = 2
    elif path == "ffm20":
        al = al + ch
        ch = 3
    al -= 2
    bx = 2
    bx, cx, z = fotan(buf, bx, (ch & 0xFF, 0))
    buf[bx] = ord("0")
    if z:
        bx += 1
    bx = fotcv(buf, bx, f, cx, typ)
    while True:
        bx -= 1
        if buf[bx] != ord("0"):
            break
    if buf[bx] != ord("."):
        bx += 1
    if al != 0:
        buf[bx] = ord("D" if typ == DBL else "E")
        bx += 1
        buf[bx] = ord("+")
        if al < 0:
            buf[bx] = ord("-")
            al = -al
        buf[bx + 1] = ord("0") + al // 10
        buf[bx + 2] = ord("0") + al % 10
        bx += 3
    buf[bx] = 0
    return text(buf, 1)


def fotcv(buf, bx, f, cx, typ=SNG):
    """$FOTCV: the 7 digits of a bracketed single, or the 16 of a double: 9
    by 7-byte powers of ten ($FODTB), then as a single's."""
    if typ == DBL:
        g = dint(f)
        if dcmpm(g, f) != 0:
            f = faddd(f, DHALF)
        v = mant56(f) >> ((0xB8 - f[7]) & 0xFF)
        for p in FODTB:
            bx, cx = foted(buf, bx, cx)
            d = 0
            while v >= p:
                v -= p
                d += 1
            buf[bx] = ord("0") + d
            bx += 1
        v &= 0xFFFFFF
    else:
        g = int_(f)
        if fcomp(g, f) != 0:
            f = fadds([0, 0, 0, 0x80], f)
        v = qint(f)
    for p in FOSTB:
        bx, cx = foted(buf, bx, cx)
        d = 0
        while v >= p:
            v -= p
            d += 1
        buf[bx] = ord("0") + d
        bx += 1
    v &= 0xFFFF
    bx, cx = fotci(buf, bx, v, 4, FOITB, cx)
    return bx


FODTB = [10 ** n for n in range(15, 6, -1)]  # $FODTB: 1D15 down to 1D7, 7-byte integers
# HIDBL: 9999999999999999, the largest double .5 can be added to without overflow.
HIDBL = [0o375, 0o377, 0o3, 0o277, 0o311, 0o33, 0o16, 0o266]
DHALF = [0, 0, 0, 0, 0, 0, 0, 0x80]  # $DHALF


# --- Doubles: add, subtract, compare, INT (step M) ---------------------------------

M64 = (1 << 64) - 1
Z8 = [0] * 8
STICKY = True  # check.py turns $SHRA's sticky bit off to find the vectors that need it


def dpack(m56, exp, sign):
    """$ROUNX: the 7 mantissa bytes (the hidden bit's place taking the sign)."""
    b = list((m56 & ((1 << 56) - 1)).to_bytes(7, "little"))
    b[6] = (b[6] & 0x7F) | (sign & 0x80)
    return b + [exp & 0xFF]


def shra(x, cl):
    """$SHRA: the ARG's 7 bytes over its overflow byte (x, 64 bits) right by
    CL bits. A whole byte keeps only the old overflow byte's ST bit ($20); a
    bit passing through bit 4 of the overflow byte sets ST."""
    while True:
        if cl >= 8:
            st = x & 0x20
            x >>= 8
            cl -= 8
            x |= st
            continue
        if cl == 0:
            return x
        x >>= 1
        if x & 0x10 and STICKY:
            x |= 0x20
        cl -= 1


def normd(x, exp, sign):
    """$NORMD: shift the 7 bytes and the overflow byte left until normalised
    (a byte at a time while the top byte is zero), at most 57 bits; then
    $ROUND. Underflow, or 57 bits shifted, is zero ($DZERO)."""
    dl = 57
    while True:
        top = x >> 56
        if top & 0x80:
            return dround(x, exp, sign)
        if top:
            x = (x << 1) & M64
            exp -= 1
            if exp == 0:
                return list(Z8)
            dl -= 1
            if dl == 0:
                return list(Z8)
            continue
        if exp <= 8:
            return list(Z8)
        exp -= 8
        if dl <= 8:
            return list(Z8)
        dl -= 8
        x = (x << 8) & M64


def faddd(fac, arg):
    """$FADDD: FAC = ARG + FAC. The smaller shifts right ($SHRA) under the
    larger; 57 bits apart or more, the larger is the answer."""
    if arg[7] == 0:
        return list(fac)
    if fac[7] == 0:
        return list(arg)
    al, ah, bl, bh = fac[6], fac[7], arg[6], arg[7]
    F, A = mant56(fac), mant56(arg)
    sign, exp = al, ah
    cl = ah - bh
    a64 = A << 8
    if cl != 0:
        if cl < 0:
            al, bl = bl, al
            cl = -cl
            sign, exp = al, bh
            F, A = A, F
            a64 = A << 8
        if cl >= 57:
            return dpack(F, exp, sign)
        a64 = shra(a64, cl)
    f64 = F << 8
    if not ((al ^ bl) & 0x80):
        s = f64 + a64
        if s >> 64:
            exp += 1
            if exp == 0x100:
                raise Overflow(1, sign, DBL)
            s >>= 1
        return dround(s & M64, exp, sign)
    s = f64 - a64
    if s < 0:
        sign ^= 0xFF
        s = (-s) & M64
    return normd(s, exp, sign)


def fsubd(fac, arg):
    """$FSUBD: FAC = ARG - FAC; a zero FAC gives the ARG as it is."""
    if fac[7] == 0:
        return list(arg)
    f = list(fac)
    f[6] ^= 0x80
    return faddd(f, arg)


def dadd(left, right):
    """DADD (the formula evaluator's +): left in the FAC, right in the ARG."""
    return faddd(left, right)


def dsub(left, right):
    """DSUB: both signs flipped, then ARG - FAC."""
    f, a = list(left), list(right)
    f[6] ^= 0x80
    a[6] ^= 0x80
    return fsubd(f, a)


def dcmpm(fac, m):
    """$DCMPM: 1, 0 or -1 as the double FAC is >, = or < m."""
    if m[7] == 0:
        return 0 if fac[7] == 0 else (-1 if fac[6] & 0x80 else 1)
    if fac[7] == 0:
        return 1 if m[6] & 0x80 else -1
    if (fac[6] ^ m[6]) & 0x80:
        return -1 if fac[6] & 0x80 else 1
    x, y = fac[::-1], m[::-1]
    if fac[6] & 0x80:
        x, y = y, x
    return (x > y) - (x < y)


def dcomp(left, right):
    """DCOMP: as ICOMP, 1 if left < right, -1 if greater."""
    return -dcmpm(left, right)


def dint(fac):
    """$DINT: the largest integer not above it. A negative number less one,
    shifted, plus one; then $NORMD. A zero exponent is left alone."""
    e = fac[7]
    if e >= 0xB8 or e == 0:
        return list(fac)
    sign = fac[6]
    m = mant56(fac)
    if sign & 0x80:
        m -= 1
    m >>= 0xB8 - e
    if sign & 0x80:
        m += 1
    return normd(m << 8, 0xB8, sign)


def dneg(d):
    d = list(d)
    d[6] ^= 0x80
    return d


# --- Soft overflow: the FAC becomes an infinity and the code goes on --------------------

class Soft:
    """Runs a routine that may print Overflow or Division by zero and go on,
    as the transcendental functions do, collecting the messages."""

    def __init__(self):
        self.msgs = []

    def __call__(self, f, *args):
        try:
            return f(*args)
        except Overflow as o:
            self.msgs.append(o.msg)
            return o.result()


# --- $FIN: text to a number -----------------------------------------------------------

FINCH = ".eE%#!Dd"
S1E6 = [0x00, 0x24, 0x74, 0x94]  # 1000000 ($FIDIG's limit for singles)
S32KM = [0x00, 0x00, 0x80, 0x90]  # $S32KM: -32768


def chrgtr(t, i):
    """CHRGTR over plain text: the next character past spaces, tabs and line
    feeds; (index, character, a digit?)."""
    while True:
        i += 1
        c = t[i]
        if c not in (0x20, 0x09, 0x0A):
            return i, c, 0x30 <= c <= 0x39


def to_sng(typ, v):
    if typ == INT:
        return flt(v)
    if typ == DBL:
        return csd(v)
    return v


def to_dbl(typ, v):
    if typ == INT:
        return cds(flt(v))
    if typ == SNG:
        return cds(v)
    return v


def mdpten_t(typ, v, n, soft):
    """MDPTEN by type: a double stays double, anything else comes back single."""
    if typ == DBL:
        return DBL, soft(mdpten_d, v, n)
    return SNG, soft(mdpten, to_sng(typ, v), n, soft)


class Fin:
    """$FIN: the number at the start of text, as GW-BASIC reads it."""

    def __init__(self, text, dbl=False):
        self.t = list(text.encode("latin-1")) + [0]
        self.soft = Soft()
        self.dbl = dbl

    def run(self):
        """(VALTYP, value, the index it stopped at, the messages)."""
        t = self.t
        typ, v = (DBL, [0] * 8) if self.dbl else (INT, 0)
        self.di, self.cx = 0, 0xFFFF
        i = 0
        c = t[0]
        if c == 0x26:  # "&": OCTCNS, the interpreter's
            raise Error(-1)
        neg_ = c == 0x2D
        if c not in (0x2B, 0x2D):
            i -= 1
        while True:
            i, c, dig = chrgtr(t, i)
            if dig:
                typ, v = self.fidig(typ, v, c - 0x30)
                continue
            dx = si = 0
            k = FINCH.find(chr(c)) if c else -1
            if k < 0:
                typ, v = self.fine(typ, v, dx, si)
                break
            ch = FINCH[k]
            if ch == ".":
                self.cx = (self.cx + 1) & 0xFFFF
                if self.cx:
                    typ, v = self.fine(typ, v, dx, si)
                    break
                if typ == INT:
                    typ, v = SNG, flt(v)
                continue
            if ch in "eE":
                _, c2, _ = chrgtr(t, i)
                if c2 in b"LlQq":
                    typ, v = self.fine(typ, v, dx, si)
                    break
                typ, v, i, dx, si = self.finex(typ, v, i, False)
                typ, v = self.fine(typ, v, dx, si)
                break
            if ch == "%":
                i += 1
                typ, v = self.fine(typ, v, dx, si)
                break
            if ch == "#":
                typ, v = DBL, to_dbl(typ, v)
                typ, v = self.fine(typ, v, 0, 0)
                i += 1
                break
            if ch == "!":
                typ, v = SNG, to_sng(typ, v)
                typ, v = self.fine(typ, v, 0, 0)
                i += 1
                break
            typ, v, i, dx, si = self.finex(typ, v, i, True)  # D, d
            typ, v = self.fine(typ, v, dx, si)
            break
        if neg_:
            if typ == INT:
                if v == -32768:
                    typ, v = SNG, neg(flt(v))
                else:
                    v = -v
            else:
                v = list(v)
                v[-2] ^= 0x80
            if typ == SNG and v == S32KM:
                typ, v = INT, -32768
        return typ, v, i, self.soft.msgs

    def fidig(self, typ, v, d):
        """$FIDIG: times ten and the digit added, going to single at 32768 and
        to double at 10^6."""
        self.di = (self.di + self.cx + 1) & 0xFFFF
        if typ == INT:
            if v < 3277:
                w = v * 10 + d
                if w < 0x8000:
                    return INT, w
            typ, v = SNG, flt(v)
        elif typ == SNG and fcomp(v, S1E6) >= 0:
            typ, v = DBL, cds(v)
        if typ == SNG:
            v = self.soft(mul10, v)
            return SNG, self.soft(fadds, v, flt(d))
        v = self.soft(fmuld, v, DP[1])
        return DBL, self.soft(faddd, cds(flt(d)), v)

    def finex(self, typ, v, i, dbl):
        """$FINEX: the exponent's digits, after forcing single (E) or double (D)."""
        if typ != DBL:
            typ, v = (DBL, to_dbl(typ, v)) if dbl else (SNG, to_sng(typ, v))
        si = dx = 0
        i, c, dig = chrgtr(self.t, i)
        if not dig:
            if c == 0x2D:
                si = 0xFFFF
            elif c != 0x2B:
                return typ, v, i, dx, si
            i, c, dig = chrgtr(self.t, i)
            if not dig:
                return typ, v, i, dx, si
        while dig:
            dx = dx * 10 + (c - 0x30) if dx < 3276 else 32767
            i, c, dig = chrgtr(self.t, i)
        return typ, v, i, dx, si

    def fine(self, typ, v, dx, si):
        """$FINE: times 10^(exponent - digits after the point)."""
        if si:
            dx = -dx
        dx -= self.di if self.di < 0x8000 else self.di - 0x10000
        if dx < -32768:  # the 8086's JO goes astray here; the port takes it as underflow
            dx = -32768
        if dx == 0:
            return typ, v
        return mdpten_t(typ, v, dx, self.soft)


def fin(text, dbl=False):
    return Fin(text, dbl).run()


# --- SQR, ^, EXP, LOG, SIN, COS, TAN, ATN, RND (step M) --------------------------------

def octs(s):
    """A single from MATH1.ASM's octal bytes."""
    return [int(x, 8) for x in s.split()]


ONE = [0, 0, 0, 0x81]
LG2E = octs("073 252 070 201")
LN2 = octs("030 162 061 200")
LOGP = [octs(s) for s in ("232 367 031 203", "044 143 103 203", "165 315 215 204", "251 177 203 202")]
LOGQ = [octs(s) for s in ("000 000 000 201", "342 260 115 203", "012 162 021 203", "364 004 065 177")]
EXPCN = [octs(s) for s in ("174 210 131 164", "340 227 046 167", "304 035 036 172", "136 120 143 174",
                           "032 376 165 176", "030 162 061 200", "000 000 000 201")]
SINCN = [octs(s) for s in ("373 327 036 206", "145 046 231 207", "130 064 043 207", "341 135 245 206",
                           "333 017 111 203")]
ATNC1 = [octs(s) for s in ("327 263 135 201", "000 000 200 201")]
ATNC2 = [octs(s) for s in ("142 065 203 176", "120 044 114 176", "171 251 252 177", "000 000 000 201")]
IN2PI = octs("013 104 116 156 203 371 042 176")
TWOPI = [0xDB, 0x0F, 0x49, 0x83]  # DX 7733, BX 101511
PI2 = [0xDB, 0x0F, 0x49, 0x81]  # DX 7733, BX 100511
PI6 = [0x92, 0x0A, 0x06, 0x80]  # DX 5222, BX 100006
TANPI12 = [0xA2, 0x30, 0x09, 0x7F]  # DX 30242, BX 77411
SQR3 = [0xD7, 0xB3, 0x5D, 0x81]  # DX 131727, BX 100535
S32K = [0x00, 0x00, 0x00, 0x90]  # BX 110000: 32768
RNDA, RNDC = 214013, 2531011
RNDCOP = [0o122, 0o307, 0o117, 0o200]  # the seed RUN starts from


def signs(f):
    return 0 if f[3] == 0 else (-1 if f[2] & 0x80 else 1)


def qint_s(fac):
    """$QINT: BL:DX, the integer part (towards minus infinity), two's
    complement, 24 bits."""
    if fac[3] == 0:
        return 0
    cl = (0x98 - fac[3]) & 0xFF
    m = mant24(fac)
    n = fac[2] & 0x80
    if n:
        m = (m - 1) & 0xFFFFFF
    m >>= cl
    if n:
        m = (m + 1) & 0xFFFFFF
        m = (-m) & 0xFFFFFF
    return m


def poly(x, coefs, soft):
    """$POLY: Horner's rule, the first coefficient the highest."""
    f = coefs[0]
    for c in coefs[1:]:
        f = soft(fmuls, x, f)
        f = soft(fadds, c, f)
    return f


def polyx(x, coefs, soft):
    """$POLYX: x * P(x^2)."""
    p = poly(soft(fmuls, x, x), coefs, soft)
    return soft(fmuls, x, p)


def sqr(f, soft=None):
    """$SQR: a bit at a time, 25 bits, the last rounding the 24th up."""
    if f[2] & 0x80:
        raise Error(5)
    al = f[3]
    if al == 0:
        return list(f)
    dx = ((f[2] | 0x80) << 8) | f[1]
    bx = f[0] << 8
    odd = al & 1
    al >>= 1
    if odd:
        v = ((dx << 16) | bx) >> 1
        dx, bx = v >> 16, v & 0xFFFF
    exp = (al + 0x40 + odd) & 0xFF
    inp = (dx << 16) | bx  # BP:DI:DX:BX, 64 bits
    r = 0  # AX:SI
    for _ in range(25):
        inp = (inp << 2) & M64
        r = ((r << 1) | 1) & 0xFFFFFFFF
        rem = inp >> 32
        if rem >= r:
            inp = ((rem - r) << 32) | (inp & 0xFFFFFFFF)
            r = (r & 0xFFFF0000) | ((r + 1) & 0xFFFF)
        else:
            r = (r & 0xFFFF0000) | ((r - 1) & 0xFFFF)
    r >>= 1
    c = r & 1
    r = ((r >> 1) + c) & 0xFFFFFFFF
    return [r & 0xFF, (r >> 8) & 0xFF, (r >> 16) & 0x7F, exp]


def log_(f, soft):
    """$LOG: Hart 2524, P(x)/Q(x) on the mantissa, plus the exponent, times LN 2."""
    if signs(f) <= 0:
        raise Error(5)
    if fcomp(f, ONE) == 0:
        return [0, 0, 0, 0]
    e = s8((f[3] - 0x80) & 0xFF)
    x = [f[0], f[1], f[2], 0x80]
    p = poly(x, LOGP, soft)
    q = poly(x, LOGQ, soft)
    r = soft(fdivs, p, q)
    r = soft(fadds, r, flt(e))
    return soft(fmuls, LN2, r)


def exp_(f, soft):
    """$EXP: 2^(x*LOG2(e)), the integer power in the exponent, Hart 1302 on
    the fraction."""
    y = soft(fmuls, LG2E, f)
    if y[3] >= 0x88:
        if y[2] & 0x80:
            return [0, 0, 0, 0]
        return _ovf(soft, 0x81, 0)
    if y[3] < 0x68:
        return list(ONE)
    n = qint_s(y)
    ah = ((n & 0xFF) + 0x81) & 0xFF
    if ah == 0:
        return _ovf(soft, 0x81, 0)
    mag = (-n) & 0xFFFFFF if y[2] & 0x80 else n
    ip = norms(mag << 8, 0x98, y[2])
    fr = soft(fsubs, y, ip)
    p = poly(fr, EXPCN, soft)
    return soft(fmuls, [0, 0, 0, ah], p)


def _ovf(soft, bh, sign, typ=SNG):
    def f():
        raise Overflow(bh, sign, typ)
    return soft(f)


def xton(x, n_fac, soft):
    """XTON: x^n for an integer 0 < n < 32768, Knuth's binary method."""
    n = qint_s(n_fac) & 0xFFFF
    y = list(ONE)
    z = x
    while True:
        bit = n & 1
        n >>= 1
        if bit:
            y = soft(fmuls, z, y)
        if n == 0:
            return y
        z = soft(fmuls, z, z)


def fpwr(x, y, soft):
    """$FPWR: x^y (x in BXDX, y in the FAC)."""
    s = signs(y)
    if s == 0:
        return exp_(y, soft)
    if s < 0:
        if x[3] == 0:
            return _ovf_d0(soft)
    elif x[3] == 0:
        return [0, 0, 0, 0]
    if x[2] & 0x80:
        if y[3] >= 0x99:
            raise Error(5)
        if fcomp(int_(y), y) != 0:
            raise Error(5)
    if y[2] & 0x80:
        ya = [y[0], y[1], y[2] & 0x7F, y[3]]
        r = fp30a(x, ya, soft)
        if r[3]:
            return soft(fdivs, ONE, r)
        dl = qint_s(ya) & 0xFF
        dl = ((dl >> 1) | (dl << 7)) & 0xFF
        return _ovf(soft, 0xFF, x[2] & dl)
    return fp30a(x, y, soft)


def _ovf_d0(soft):
    def f():
        raise Overflow(0, 0, msg="Division by zero")
    return soft(f)


def fp30a(x, y, soft):
    xneg = x[2] & 0x80
    yi = int_(y)
    if fcomp(yi, y) == 0 and fcomp(yi, S32K) < 0:
        return xton(x, yi, soft)
    odd = False
    if xneg:
        odd = bool(qint_s(yi) & 1)
    xa = [x[0], x[1], x[2] & 0x7F, x[3]]
    r = log_(xa, soft)
    r = soft(fmuls, y, r)
    r = exp_(r, soft)
    return neg(r) if odd else r


def rr2(d, i):
    """RR2: FAC = INT(X) - X, then negated: X - INT(X)."""
    return dneg(fsubd(d, i))


def rr1(d):
    return rr2(d, dint(d))


def rr(f, soft):
    """RR: the single to double, times 1/(2 pi), its fraction."""
    d = soft(fmuld, cds(f), IN2PI)
    return rr1(d)


def sin30(d, soft):
    """From SIN30: the quadrant, the angle in it, Hart 3341."""
    d = list(d)
    if d[7]:
        d[7] = (d[7] + 2) & 0xFF
    a = dint(d)
    zneg = a[7] == 0x82
    sub1 = bool(a[7] & 1) or bool(a[6] & 0x40)
    d = rr2(d, a)
    if sub1:
        d = soft(fsubd, d, DP[0])
    if d[7] < 2:
        d = list(Z8)
    else:
        d[7] -= 2
    s = csd(d)
    if s[3] < 0x74:
        s = soft(fmuls, TWOPI, s)
    else:
        s = polyx(s, SINCN, soft)
    return neg(s) if zneg else s


def sin_(f, soft):
    if f[3] < 0x77:
        return list(f)
    n = f[2] & 0x80
    f = [f[0], f[1], f[2] & 0x7F, f[3]]
    s = sin30(rr(f, soft), soft)
    return neg(s) if n else s


def cos_(f, soft):
    f = [f[0], f[1], f[2] & 0x7F, f[3]]
    d = rr(f, soft)
    q = list(DP[0])
    q[7] = 0x7F
    d = soft(faddd, d, q)
    return sin30(rr1(d), soft)


def tan_(f, soft):
    s = sin_(f, soft)
    c = cos_(f, soft)
    return soft(fdivs, s, c)


def atn_(f, soft):
    """$ATAN: Hart 4940, reduced by 1/x past 1 and by pi/6 past tan(pi/12)."""
    n = f[2] & 0x80
    f = [f[0], f[1], f[2] & 0x7F, f[3]]
    post = []
    if f[3] >= 0x81:
        post.append(PI2)
        f = soft(fdivs, ONE, f)
    if fcomp(f, TANPI12) >= 0:
        post.append(PI6)
        a = soft(fadds, SQR3, f)
        p = poly(f, ATNC1, soft)
        f = soft(fdivs, p, a)
    r = polyx(f, ATNC2, soft)
    for k in reversed(post):
        r = soft(fadds, PI6, r) if k is PI6 else soft(fsubs, PI2, r)
    return neg(r) if n else r


def rnd(rndx, f):
    """$RND: (the new RNDX, the result). RNDX is 4 bytes, of which the low 3
    are the seed. 0 repeats the last; negative seeds from its own bytes."""
    rndx = list(rndx)
    s = signs(f)
    if s:
        if s < 0:
            rndx[0:3] = f[0:3]
        x = rndx[0] | (rndx[1] << 8) | (rndx[2] << 16)
        x = (x * RNDA + RNDC) & 0xFFFFFF
        rndx[0:3] = [x & 0xFF, (x >> 8) & 0xFF, x >> 16]
    x = rndx[0] | (rndx[1] << 8) | (rndx[2] << 16)
    return rndx, norms(x << 8, 0x80, 0)


def foutoh(i, octal):
    """$FOUTO, $FOUTH: OCT$ and HEX$'s digits, leading zeros dropped."""
    i &= 0xFFFF
    s = "%06o" % i if octal else "%04X" % i
    return s.lstrip("0") or "0"


# --- Exact values, for checking the model itself -----------------------------------------


def value(b):
    """The exact value of a single or double as a Fraction (None if infinite)."""
    if b[-1] == 0:
        return Fraction(0)
    if len(b) == 4:
        m, bits = mant24(b), 24
    else:
        m, bits = mant56(b), 56
    v = Fraction(m, 1 << bits) * Fraction(2) ** (b[-1] - 128)
    return -v if b[-2] & 0x80 else v


def nearest(x, bits=24):
    """The single (or double) nearest x, ties to even; None out of range."""
    if x == 0:
        return [0] * (4 if bits == 24 else 8)
    sign = 0x80 if x < 0 else 0
    x = abs(x)
    e = 0
    while x >= 1:
        x /= 2
        e += 1
    while x < Fraction(1, 2):
        x *= 2
        e -= 1
    m = x * (1 << bits)
    q = int(m)
    r = m - q
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and q & 1):
        q += 1
    if q == 1 << bits:
        q >>= 1
        e += 1
    exp = e + 128
    if not 0 < exp < 256:
        return None
    b = list(q.to_bytes(bits // 8, "little"))
    b[-1] = (b[-1] & 0x7F) | sign
    return b + [exp]


# The powers of ten. MATH1.ASM's table holds each as the double nearest it
# (check.py checks math1.s's copy against these).
DPM01 = nearest(Fraction(1, 10), 56)
for _n in range(39):
    DP[_n] = nearest(Fraction(10) ** _n, 56)
