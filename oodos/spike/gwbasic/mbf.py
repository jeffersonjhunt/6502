"""GW-BASIC's Microsoft Binary Format arithmetic, modelled in Python.

The reference the spike's 6502 code is checked against (check.py). It is
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

DP = {}  # 10^n as GW-BASIC's double table holds it (filled from math1.s by check.py)
DPM01 = None


def mdpten(fac4, n):
    """MDPTEN for a single: times 10^n in double precision, back to single."""
    d = cds(fac4)
    if n >= 0:
        if d[7] == 0:
            return ZERO4
        if n >= 39:
            raise Overflow(1, d[6])
        return csd(fmuld(d, DP[n]))
    if n < -38:
        n += 38
        if n < -38:
            return ZERO4
        d = ddiv(d, DP[-n])
        n = -38
    return csd(ddiv(d, DP[-n]))


def mul10(f):
    return fmuls(f, csd(DP[1]))


def div10(f):
    return fmuls(f, csd(DPM01))


FOTB = []  # $FOTB, filled by check.py
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
        s = 0 if val[3] == 0 else (-1 if val[2] & 0x80 else 1)
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
    return fofmt(buf, val)


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


def fotnv(f):
    """$FOTNV: bracket into 10^6..10^7; the decimal exponent that takes."""
    e = 0
    while FOTB[f[3]]:
        p = FOTB[f[3]]
        e -= p
        f = mdpten(f, p)
    if fcomp(f, DP[6][4:]) < 0:
        f = mul10(f)
        e -= 1
    return f, e


def sigd(f, cl):
    """$SIGD: CL up by one for each zero digit at the right of the bracket."""
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


def fofmt(buf, f):
    al = None
    f, al = fotnv(f)
    ch = cl = 8
    if al <= 0:
        if al + 7 >= 0:
            path = "ffm18"
        else:
            cl = sigd(f, cl)
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
    bx = fotcv(buf, bx, f, cx)
    while True:
        bx -= 1
        if buf[bx] != ord("0"):
            break
    if buf[bx] != ord("."):
        bx += 1
    if al != 0:
        buf[bx] = ord("E")
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


def fotcv(buf, bx, f, cx):
    """$FOTCV: the 7 digits of a bracketed single."""
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
