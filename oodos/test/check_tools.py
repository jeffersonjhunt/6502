"""Checks OODOS's build tools (README.md): the header, mkprl.py's .PRL and loader, and the rules gate.

    make check                       # from oodos/, after make
    python3 -I test/check_tools.py [BUILD]

Every program make built in BUILD (build by default) is checked: its .PRL loaded at other pages
against direct links there, and by the loader under memory it mustn't touch. The rest uses
examples/hello.s, built here, and programs written here. Needs ca65, ld65, make and a C++
compiler (spike/gwbasic's run6502, to run hello). Every negative prints what refused it. Ends
PASS n, or FAIL n.
"""

import glob
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PRL_CFG = os.path.join(ROOT, "cfg", "prl.cfg")
COM_CFG = os.path.join(ROOT, "cfg", "com.cfg")
SPIKE = os.path.join(ROOT, "spike", "gwbasic")
LINKS = ((3, 0), (4, 2), (5, 5))  # the Makefile's: (page, zero page base) of a, b and c

sys.dont_write_bytecode = True  # no __pycache__ in tools/
spec = importlib.util.spec_from_file_location("mkprl", os.path.join(ROOT, "tools", "mkprl.py"))
mkprl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkprl)

passed = failed = 0
SENTINEL = 0xA5


def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
        print("ok    %s" % what)
    else:
        failed += 1
        print("FAIL  %s" % what)


def refused(fn, what, expect=""):
    """fn must raise Refused, its message containing expect."""
    try:
        fn()
    except mkprl.Refused as e:
        ok(expect in str(e), "%s: refused: %s" % (what, e))
        return
    ok(False, "%s: not refused" % what)


def run(*cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def link(obj, cfg, out, page=None, zp=None):
    cmd = ["ld65", "-C", cfg]
    if page is not None:
        cmd += ["-D", "__OODOS_PAGE__=%d" % page]
    if zp is not None:
        cmd += ["-D", "__OODOS_ZP__=%d" % zp]
    r = run(*cmd, obj, "-o", out, "-m", out + ".map")
    if r.returncode:
        raise SystemExit("ld65 failed: %s" % r.stderr)
    return read(out)


def links(obj, out, at=LINKS):
    """The three links mkprl takes (out.0, out.1, out.2), and CODE in the first: make()'s arguments."""
    three = [link(obj, PRL_CFG, "%s.%d" % (out, k), page, zp) for k, (page, zp) in enumerate(at)]
    return three + [mkprl.code_range(out + ".0.map")]


def make_prl(obj, out, at=LINKS):
    return mkprl.make(*links(obj, out, at))


def assemble(tmp, name, source):
    path = os.path.join(tmp, name + ".s")
    with open(path, "w") as f:
        f.write(source)
    r = run("ca65", "-g", "-I", os.path.join(ROOT, "include"), path, "-o", os.path.join(tmp, name + ".o"))
    if r.returncode:
        raise SystemExit("ca65 failed: %s" % r.stderr)
    return os.path.join(tmp, name + ".o")


def program(body, data="", zp="zp:     .res 2"):
    return ('        .include "oodos.inc"\n        OODOS_HEADER\n        .zeropage\n%s\n'
            "        .code\n%s\n        jmp WBOOT\n        .rodata\n%s\n" % (zp, body, data))


def patched(data, **fields):
    """data with header fields changed: flags, page, zpsize, rsxcount, rsxrec."""
    b = bytearray(data)
    at = {"flags": (4, 1), "page": (5, 1), "zpsize": (11, 1), "rsxcount": (12, 1), "rsxrec": (14, 2)}
    for f, v in fields.items():
        o, n = at[f]
        b[o:o + n] = v.to_bytes(n, "little")
    return bytes(b)


def under_sentinels(data, page, kind="program", zp=None, overlay=False):
    """Load data at page with the TPA's top just past its image and BSS, in memory full of sentinels.
    @return (the image, how many bytes outside it changed)"""
    h = mkprl.header(data)
    mem = bytearray([SENTINEL]) * 0x10000
    base = page << 8
    top = base + h["size"] + h["bss"]
    if overlay:
        got = mkprl.overlay(data, base, top, mem=mem)
    else:
        got = mkprl.load(data, page, zp, top, kind=kind, mem=mem)
    return got, sum(1 for i in range(0x10000) if mem[i] != SENTINEL and not base <= i < base + h["size"])


def programs_in(build):
    return sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(build, "*.prl"))
                  if os.path.exists(p[:-4] + ".com") and os.path.exists(p[:-4] + ".o"))


def build_checks(build, tmp):
    """Every program in the build: its .COM and .PRL agree, and the .PRL loads where a direct link would."""
    names = programs_in(build)
    ok(names, "the build in %s has programs: %s" % (build, ", ".join(names)))
    for name in names:
        com, prl = read(os.path.join(build, name + ".com")), read(os.path.join(build, name + ".prl"))
        hc, hp = mkprl.header(com), mkprl.header(prl)
        ok(hc["flags"] == 0 and hc["page"] == 3 and len(com) == 16 + hc["size"],
           "%s.com: not relocatable, page $03, 16 bytes of header and the image" % name)
        ok(hp["flags"] == mkprl.F_RELOC and prl[16:16 + hp["size"]] == com[16:] and len(prl) % 128 == 0,
           "%s.prl: relocatable, whole records, its image at page $03 is %s.com's" % (name, name))
        obj = os.path.join(build, name + ".o")
        for page, zp in ((0x03, 0x00), (0x05, 0x01), (0x10, 0xF0 - hp["zpsize"]), (0x5A, 0x37), (0x80, 0x10)):
            if (page << 8) + hp["size"] + hp["bss"] > mkprl.TOP:
                continue
            direct = link(obj, PRL_CFG, os.path.join(tmp, "direct"), page, zp)
            ok(mkprl.load(prl, page, zp) == direct[16:],
               "%s.prl loaded at page $%02X, zero page $%02X, is a direct link there" % (name, page, zp))
        got, changed = under_sentinels(prl, 0x80)
        ok(got == link(obj, PRL_CFG, os.path.join(tmp, "direct"), 0x80, 0)[16:] and not changed,
           "%s.prl at $8000, right under the TPA's top: nothing but its image changed (%d bytes did)" % (name, changed))


def header_checks(hello):
    com, prl = read(hello + ".com"), read(hello + ".prl")
    hc, hp = mkprl.header(com), mkprl.header(prl)
    ok(com[:4] == b"OOD\x1a" and len(com) == 16 + hc["size"], "hello.com: the magic, then 16 bytes of header and the image")
    ok(hc["bss"] == 3 and hc["zpbase"] == 0 and hc["zpsize"] == 3, "hello.com: BSS 3 bytes, zero page $00, 3 bytes")
    ok(hc["rsxcount"] == 0 and hc["rsxrec"] == 0, "hello.com: no attached RSXs")
    n = mkprl.bitmap_len(hp["size"])
    pr, zr = mkprl.bitmap_records(hp["size"])
    ok((pr, zr) == (2, 3) and len(prl) == 4 * 128 and not any(prl[16 + hp["size"]:256]),
       "hello.prl: %d bytes of image, zeros to record 2, the page bitmap there, the zero page's at 3" % hp["size"])
    pages = sum(bin(b).count("1") for b in prl[pr * 128:pr * 128 + n])
    zps = sum(bin(b).count("1") for b in prl[zr * 128:zr * 128 + n])
    ok(pages > 0 and zps > 0 and not any(prl[pr * 128 + n:zr * 128]) and not any(prl[zr * 128 + n:]),
       "hello.prl: %d page relocations, %d zero page, zeros after each bitmap" % (pages, zps))


def zp_marked(prl):
    """The image offsets the zero page bitmap marks."""
    h = mkprl.header(prl)
    at = mkprl.bitmap_records(h["size"])[1] * 128
    return [i for i in range(h["size"]) if prl[at + (i >> 3)] & (0x80 >> (i & 7))]


def loader_checks(hello, tmp):
    prl, com = read(hello + ".prl"), read(hello + ".com")
    out = os.path.join(tmp, "img")
    r = run(sys.executable, "-I", "tools/mkprl.py", "--load", hello + ".prl", "--page", "0x80", "--zp", "0x40", "-o", out)
    ok(r.returncode == 0 and read(out) == mkprl.load(prl, 0x80, 0x40), "mkprl.py --load writes the loaded image")
    ok(mkprl.load(com) == com[16:], "hello.com loads at $0300 as it is")
    got, changed = under_sentinels(com, 3)
    ok(got == com[16:] and not changed, "hello.com right under the TPA's top: nothing but its image changed")
    # Smaller than the first record's 112 bytes: only its own bytes are copied.
    o = assemble(tmp, "tiny", program("        lda zp"))
    tiny = make_prl(o, os.path.join(tmp, "tiny"))
    size = mkprl.header(tiny)["size"]
    got, changed = under_sentinels(tiny, 0x40)
    ok(size < 112 and got == link(o, PRL_CFG, os.path.join(tmp, "d"), 0x40, 0)[16:] and not changed,
       "a %d-byte .PRL under sentinels: only its image changed (%d other bytes did)" % (size, changed))
    # Over 1,024 bytes: several records of each bitmap.
    o = assemble(tmp, "big", program("        .repeat 400\n        lda zp\n        ldx #zp+1\n        sta msg\n"
                                     "        .endrep", "msg:    .repeat 300\n        .word msg\n        .endrep"))
    big = make_prl(o, os.path.join(tmp, "big"))
    hb = mkprl.header(big)
    nrec = mkprl.records(mkprl.bitmap_len(hb["size"]))
    ok(nrec == 4 and len(big) == 128 * (mkprl.bitmap_records(hb["size"])[1] + nrec),
       "a %d-byte .PRL: 4 records to each bitmap" % hb["size"])
    for page, zp in ((0x07, 0x20), (0x91, 0x00)):
        got, changed = under_sentinels(big, page, zp=zp)
        ok(got == link(o, PRL_CFG, os.path.join(tmp, "d"), page, zp)[16:] and not changed,
           "it loads at page $%02X, zero page $%02X, as a direct link, touching nothing else" % (page, zp))
    # An attached RSX table and an RSX after the program: never read into memory.
    table = bytearray(128)
    table[0:2] = (len(prl) // 128 + 1).to_bytes(2, "little")
    table[3:11] = b"FAKERSX "
    attached = patched(prl, rsxcount=1, rsxrec=len(prl) // 128) + bytes(table) + bytes([0xEE]) * 256
    got, changed = under_sentinels(attached, 0x80)
    ok(got == mkprl.load(prl, 0x80) and not changed, "hello.prl with an RSX table and an RSX attached: only the image changed")
    # Set bits in the bitmaps' padding, past the image's bytes, are ignored.
    n = mkprl.bitmap_len(mkprl.header(prl)["size"])
    dirty = bytearray(prl)
    for r in mkprl.bitmap_records(mkprl.header(prl)["size"]):
        dirty[r * 128 + n:(r + 1) * 128] = bytes([0xFF]) * (128 - n)
    got, changed = under_sentinels(bytes(dirty), 0x80)
    ok(got == mkprl.load(prl, 0x80) and not changed, "hello.prl with its bitmaps' padding all ones: the padding is ignored")
    refused(lambda: mkprl.load(prl[:3 * 128], 0x40), "hello.prl without its zero page bitmap's record", "ends at record 3")
    refused(lambda: mkprl.load(prl[:2 * 128], 0x40), "hello.prl without its bitmaps", "before its bitmaps")
    # The refusals.
    refused(lambda: mkprl.load(prl, 0x80, 0xEE), "hello.prl with its 3 bytes of zero page at $EE", "below $F0")
    ok(mkprl.load(prl, 0x80, 0xED) == link(hello + ".o", PRL_CFG, os.path.join(tmp, "d"), 0x80, 0xED)[16:],
       "hello.prl with its 3 bytes of zero page at $ED, up to $EF")
    refused(lambda: mkprl.load(prl, 0xC0, 0), "hello.prl at page $C0", "don't fit")
    refused(lambda: mkprl.load(prl, 0x02, 0), "hello.prl at page $02", "below the TPA")
    refused(lambda: mkprl.load(com, 0x04), "hello.com at page $04", "only at its link page")
    refused(lambda: mkprl.load(patched(com, page=4), 4), "a .COM linked at page $04", "a .COM is linked at $03")
    for flag in (0x04, 0x40):
        refused(lambda: mkprl.load(patched(prl, flags=1 | flag), 0x40), "hello.prl with flag bit $%02X" % flag,
                "unknown flag bits $%02X" % flag)
        refused(lambda: mkprl.load(patched(com, flags=flag)), "hello.com with flag bit $%02X" % flag, "unknown flag bits")
    ok(mkprl.load(patched(prl, flags=mkprl.F_RELOC | mkprl.F_NOT816), 0x40) == mkprl.load(prl, 0x40),
       "hello.prl flagged as breaking the 65C816 rules (bit 7) still loads")
    # RSXs: a slice of $F0-$FF.
    refused(lambda: mkprl.load(prl, 0x40, kind="rsx"), "hello.prl loaded as an RSX", "not an RSX")
    rsx = patched(prl, flags=mkprl.F_RELOC | mkprl.F_RSX)
    refused(lambda: mkprl.load(rsx, 0x40), "an RSX loaded as a program", "an RSX, loaded as a program")
    want = bytearray(mkprl.load(prl, 0xA0, 0))
    for i in zp_marked(prl):
        want[i] = (want[i] + 0xFD) & 0xFF
    ok(mkprl.load(rsx, 0xA0, 0xFD, kind="rsx") == want, "an RSX's 3 bytes of zero page at $FD, the last free")
    refused(lambda: mkprl.load(rsx, 0xA0, 0xFE, kind="rsx"), "an RSX's 3 bytes of zero page at $FE", "free of $F0-$FF")
    refused(lambda: mkprl.load(rsx, 0xA0, 0xE0, kind="rsx"), "an RSX's zero page at $E0, the program's", "free of $F0-$FF")
    refused(lambda: mkprl.load(patched(com, flags=mkprl.F_RSX), kind="rsx"), "a .COM as an RSX", "must be a .PRL")
    # Overlays, function 59: on a page, and no zero page of their own.
    o = assemble(tmp, "ovl", program("        lda #>msg\n        sta msg", "msg: .byte 0", zp=""))
    ovl = make_prl(o, os.path.join(tmp, "ovl"))
    got, changed = under_sentinels(ovl, 0x60, overlay=True)
    ok(mkprl.header(ovl)["zpsize"] == 0 and got == link(o, PRL_CFG, os.path.join(tmp, "d"), 0x60, 0)[16:] and not changed,
       "an overlay at $6000 loads, touching nothing else")
    refused(lambda: mkprl.overlay(ovl, 0x6080), "an overlay at $6080", "isn't a page's start")
    refused(lambda: mkprl.overlay(prl, 0x6000), "hello.prl, with zero page, as an overlay", "an overlay has none")
    with open(os.path.join(tmp, "ovl.prl"), "wb") as f:
        f.write(ovl)
    r = run(sys.executable, "-I", "tools/mkprl.py", "--load", os.path.join(tmp, "ovl.prl"), "--overlay", "0x6001", "-o", out)
    ok(r.returncode == 1 and "page's start" in r.stderr, "mkprl.py --load --overlay 0x6001 exits 1: %s" % r.stderr.strip())


def mkprl_checks(hello, tmp):
    obj = hello + ".o"
    a, b, c, code = links(obj, os.path.join(tmp, "l"))
    ok(mkprl.make(a, b, c, code) == read(hello + ".prl"), "mkprl: the three links make the build's hello.prl")
    o = links(obj, os.path.join(tmp, "m"), ((3, 0), (4, 0x10), (5, 0x30)))
    ok(mkprl.load(mkprl.make(*o), 0x22, 0x40) == link(obj, PRL_CFG, os.path.join(tmp, "d"), 0x22, 0x40)[16:],
       "mkprl: links with the zero page $10 and $30 above work too")
    o = links(obj, os.path.join(tmp, "z"), ((3, 0), (4, 1), (5, 5)))
    ok(mkprl.load(mkprl.make(*o), 0x30, 0x50) == link(obj, PRL_CFG, os.path.join(tmp, "d"), 0x30, 0x50)[16:],
       "mkprl: zero page bases 1 and 5 above: the third link tells them from a page")
    refused(lambda: mkprl.make(a, a, a, code), "mkprl: the same link three times", "one and two pages above")
    refused(lambda: mkprl.make(a, b, b, code), "mkprl: b as c", "one and two pages above")
    for zps in ((0, 1, 2), (0, 0, 0)):
        o = links(obj, os.path.join(tmp, "z"), tuple(zip((3, 4, 5), zps)))
        refused(lambda: mkprl.make(*o), "mkprl: zero page bases $%02X, $%02X, $%02X" % zps, "can't be told apart")
    com = link(obj, COM_CFG, os.path.join(tmp, "com"))
    refused(lambda: mkprl.make(com, b, c, code), "mkprl: a .COM link as a", "relocatable flag")
    bad = bytearray(b)
    bad[16 + 0x21] = (bad[16 + 0x21] + 5) & 0xFF
    refused(lambda: mkprl.make(a, bytes(bad), c, code), "mkprl: b with a byte 5 more", "image offset $0021")
    bad = bytearray(c)
    bad[16 + 0x21] = (bad[16 + 0x21] + 1) & 0xFF
    refused(lambda: mkprl.make(a, b, bytes(bad), code), "mkprl: c with a byte 1 more", "image offset $0021")
    refused(lambda: mkprl.make(a, b, c[:-1], code), "mkprl: c cut short", "too short")
    for f, v in (("rsxcount", 1), ("zpsize", 4)):
        refused(lambda: mkprl.make(a, b, patched(c, **{f: v}), code), "mkprl: c's %s different" % f, "headers' %s differ" % f)
    big = [patched(x, zpsize=0xF1) for x in (a, b, c)]
    refused(lambda: mkprl.make(*big, code), "mkprl: a program with $F1 bytes of zero page", "at most $F0")
    rsx = [patched(x, flags=mkprl.F_RELOC | mkprl.F_RSX, zpsize=17) for x in (a, b, c)]
    refused(lambda: mkprl.make(*rsx, code), "mkprl: an RSX with 17 bytes of zero page", "at most 16")
    rsx = [patched(x, flags=mkprl.F_RELOC | mkprl.F_RSX) for x in (a, b, c)]
    ok(mkprl.header(mkprl.make(*rsx, code))["flags"] == 3, "mkprl: an RSX with 3 bytes of zero page")
    # Expressions that aren't relocations: three times an address, and twice, which moves by 2 then 4
    # (two links took it for the zero page).
    for name, expr in (("times3", ">(msg*3)"), ("times2", ">(msg*2)")):
        o = assemble(tmp, name, program("        lda #%s" % expr, "msg: .byte 0"))
        t = links(o, os.path.join(tmp, name))
        refused(lambda: mkprl.make(*t), "mkprl: LDA #%s" % expr, "image offset $0001 ($0301")
    # A 16-bit pointer to zero page, a zero page address as data, an absolute operand: rule 2.
    for name, body, data in (("zpword", "        lda #<msg", "msg: .word zp"), ("zpbyte", "        lda #<msg", "msg: .byte zp"),
                             ("zpabs", "        lda a:zp", "")):
        o = assemble(tmp, name, program(body, data))
        t = links(o, os.path.join(tmp, name))
        refused(lambda: mkprl.make(*t), "mkprl: %s" % (data.split(": ")[-1] + " in RODATA" if data else body.strip()),
                "outside a CODE zero page/immediate operand (rule 2")
    o = assemble(tmp, "zpok", program("        .pc02\n        lda zp\n        sta (zp),y\n        ldx #zp\n        lda zp+1,x\n"
                                      "        lda (zp,x)\n        lda (zp)\n        ldx #<(zp+1)"))
    ok(mkprl.make(*links(o, os.path.join(tmp, "zpok"))) is not None,
       "mkprl: zero page, (zp),Y, immediate, zp,X, (zp,X) and (zp) operands pass")
    # A different program as b.
    o2 = assemble(tmp, "other", program("        nop"))
    refused(lambda: mkprl.make(a, link(o2, PRL_CFG, os.path.join(tmp, "ob"), 4, 2), c, code), "mkprl: b another program", "differ")
    t = os.path.join(tmp, "times3")
    r = run(sys.executable, "-I", "tools/mkprl.py", t + ".0", t + ".1", t + ".2", "--map", t + ".0.map", "-o", t + ".prl")
    ok(r.returncode == 1 and "mkprl: image offset" in r.stderr and not os.path.exists(t + ".prl"),
       "mkprl.py exits 1 and writes nothing: %s" % r.stderr.strip())


def magic_checks(hello):
    com, prl = read(hello + ".com"), read(hello + ".prl")
    refused(lambda: mkprl.load(b"\x00" + com[1:]), "a .COM with its magic's first byte cleared", "magic")
    refused(lambda: mkprl.load(prl[:3] + b"\x00" + prl[4:], 0x40, 0), "a .PRL with its magic's ^Z cleared", "magic")
    refused(lambda: mkprl.load(bytes([0xC3, 0x00, 0x01]) + bytes(125)), "an 8080 .COM (JMP $0100)", "magic")
    refused(lambda: mkprl.load(b""), "an empty file", "magic")
    refused(lambda: mkprl.make(com[16:], prl, prl, None), "mkprl: links without the header", "magic")
    tampered = hello + ".nomagic"
    with open(tampered, "wb") as f:
        f.write(b"XX" + com[2:])
    r = run(sys.executable, "-I", "tools/mkprl.py", "--load", tampered, "-o", tampered + ".img")
    ok(r.returncode == 1 and "magic" in r.stderr, "mkprl.py --load exits 1: %s" % r.stderr.strip())


BREAKS = [
    ("rule1", "        .byte $07, zp            ; RMB0 zp", "RMB/SMB/BBR/BBS (rule 1)"),
    ("rule2", "        lda a:zp", "absolute zero page operand (rule 2)"),
    ("rule3", "        lda $0103,x", "absolute stack page operand (rule 3)"),
    ("rule4", "        jsr $F800", "JSR outside the program, not the BDOS (rule 4)"),
    ("data", "        .byte $02", "data in code?"),
]


def build(tmp, name, kind, source=None):
    """make SRC=tmp BUILD=tmp/build name.kind, from source if given. @return (status, output, made)"""
    if source is not None:
        with open(os.path.join(tmp, name + ".s"), "w") as f:
            f.write(source)
    out = os.path.join(tmp, "build", name + "." + kind)
    r = run("make", "--no-print-directory", "SRC=" + tmp, "BUILD=" + os.path.join(tmp, "build"), "PROGRAMS=" + name, out)
    return r.returncode, r.stdout + r.stderr, os.path.exists(out)


def tpa_checks(tmp):
    """A .COM's image and BSS end by $BFFC: $BFFD-$BFFF are the BDOS's JMP (hw/oodos.md)."""
    big = lambda n: ('        .include "oodos.inc"\n        OODOS_HEADER\n        .code\n        jmp WBOOT\n'
                     "        .bss\n        .res %d\n" % n)
    status, out, made = build(tmp, "tpatop", "com", big(0xBFFD - 0x0300 - 3))
    ok(status == 0 and made and mkprl.header(read(os.path.join(tmp, "build", "tpatop.com")))["bss"] == 0xBFFD - 0x0303,
       "a .COM to $BFFC links")
    status, out, made = build(tmp, "tpaover", "com", big(0xBFFD - 0x0300 - 2))
    ok(status != 0 and not made, "a .COM to $BFFD doesn't: ld65 refuses it")


def rules_checks(hello, tmp):
    r = run(sys.executable, "-I", "tools/rules.py", hello + ".com", "--map", hello + ".map")
    ok(r.returncode == 0 and "0 problems" in r.stdout, "rules: hello.com passes: %s" % r.stdout.strip())
    for name, body, expect in BREAKS:
        o = assemble(tmp, name, program("        jsr BDOS\n" + body))
        link(o, COM_CFG, os.path.join(tmp, name + ".com"))
        r = run(sys.executable, "-I", "tools/rules.py", os.path.join(tmp, name + ".com"), "--map", os.path.join(tmp, name + ".com.map"))
        line = r.stdout.splitlines()[0] if r.stdout else ""
        ok(r.returncode == 1 and expect in line and "$0303" in line, "rules: %s refused: %s" % (name, line.replace(tmp + os.sep, "")))
    # The zero page segment ends at $EF, in both configs.
    o = assemble(tmp, "zpfull", program("        lda zp", zp="zp:     .res $F0"))
    for cfg, zp in ((COM_CFG, None), (PRL_CFG, 0), (PRL_CFG, 1)):
        cmd = ["ld65", "-C", cfg] + (["-D", "__OODOS_ZP__=%d" % zp] if zp is not None else [])
        r = run(*cmd, o, "-o", os.path.join(tmp, "zpfull"))
        ok((r.returncode == 0) == (zp != 1), "%s: $F0 bytes of zero page from $%02X %s" % (
            os.path.basename(cfg), zp or 0, "link" if zp != 1 else "don't: %s" % (r.stderr.strip().splitlines() or ["linked"])[-1]))
    # The build itself stops: no .COM, no .PRL.
    for kind in ("com", "prl"):
        status, out, made = build(tmp, "rule2", kind)
        ok(status and "rule 2" in out and not made, "make rule2.%s with a planted LDA a:zp fails" % kind)
    status, out, made = build(tmp, "rule1", "com")
    ok(status and "rule 1" in out and not made, "make rule1.com with a planted RMB0 fails")
    # A 16-bit pointer to zero page passes the rules, which see CODE only, and stops at mkprl.
    status, out, made = build(tmp, "zpptr", "prl", program("        lda #<msg", "msg: .word zp"))
    ok(status and "outside a CODE zero page/immediate operand" in out and not made, "make zpptr.prl with a planted .word zp fails")
    # An initialized zero page byte: ld65 drops it with a warning, and the build fails, for both.
    for kind in ("com", "prl"):
        status, out, made = build(tmp, "zpinit", kind, program("        lda zp", zp="zp:     .byte 5"))
        ok(status and "contains initialized data" in out and not made, "make zpinit.%s with .byte 5 in ZEROPAGE fails" % kind)


def run_checks(hello, tmp):
    """hello on the emulator's W65C02S (spike/gwbasic's run6502), with command tails."""
    r = run("make", "--no-print-directory", "-C", SPIKE, "run6502")
    ok(r.returncode == 0, "run6502 built")
    if r.returncode:
        print(r.stdout + r.stderr)
        return
    image = read(hello + ".com")[16:]
    with open(os.path.join(tmp, "hello.bin"), "wb") as f:
        f.write(image)
    line = b"Hello from OODOS\r\n"
    chars = bytes(0x41 + i % 26 for i in range(127))
    for count in (0, 5, 126, 127, 255):
        with open(os.path.join(tmp, "dma"), "wb") as f:
            f.write(bytes([count]) + chars)
        p = subprocess.run([os.path.join(SPIKE, "run6502"), os.path.join(tmp, "hello.bin"),
                            "--load", "0280:" + os.path.join(tmp, "dma"), "--dump", "0280:400:" + os.path.join(tmp, "after")],
                           capture_output=True)
        after = read(os.path.join(tmp, "after"))
        n = min(count, 126)
        ok(p.returncode == 0 and p.stdout == line + line + chars[:n] + b"\r\n" and after[1 + n] == ord("$")
           and after[0x80:0x80 + len(image)] == image,
           "hello with a %d-character tail prints %d, its '$' at $%04X, the image unchanged" % (count, n, 0x281 + n))


def main():
    built = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build"))
    with tempfile.TemporaryDirectory() as tmp:
        r = run("make", "--no-print-directory", "SRC=examples", "BUILD=" + os.path.join(tmp, "hello"), "PROGRAMS=hello")
        if r.returncode:
            raise SystemExit("make hello failed:\n" + r.stdout + r.stderr)
        hello = os.path.join(tmp, "hello", "hello")
        print("The build")
        build_checks(built, tmp)
        print("The header")
        header_checks(hello)
        print("The loader")
        loader_checks(hello, tmp)
        print("mkprl")
        mkprl_checks(hello, tmp)
        print("The magic number")
        magic_checks(hello)
        print("The TPA's top")
        tpa_checks(tmp)
        print("The 65C816 rules")
        rules_checks(hello, tmp)
        print("Running hello")
        run_checks(hello, tmp)
    print("FAIL %d (PASS %d)" % (failed, passed) if failed else "PASS %d" % passed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
