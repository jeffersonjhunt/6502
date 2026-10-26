"""Checks OODOS's build tools (README.md): the header, mkprl.py's .PRL and relocator, and the rules gate.

    make check                       # from oodos/, after make
    python3 -I test/check_tools.py

Needs ca65, ld65 and make. Every negative prints what refused it. Ends PASS n, or FAIL n.
"""

import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
PRL_CFG = os.path.join(ROOT, "cfg", "prl.cfg")
COM_CFG = os.path.join(ROOT, "cfg", "com.cfg")

sys.dont_write_bytecode = True  # no __pycache__ in tools/
spec = importlib.util.spec_from_file_location("mkprl", os.path.join(ROOT, "tools", "mkprl.py"))
mkprl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkprl)

passed = failed = 0


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


def assemble(tmp, name, source):
    path = os.path.join(tmp, name + ".s")
    with open(path, "w") as f:
        f.write(source)
    r = run("ca65", "-g", "-I", os.path.join(ROOT, "include"), path, "-o", os.path.join(tmp, name + ".o"))
    if r.returncode:
        raise SystemExit("ca65 failed: %s" % r.stderr)
    return os.path.join(tmp, name + ".o")


def program(body, data=""):
    return ('        .include "oodos.inc"\n        OODOS_HEADER\n        .zeropage\nzp:     .res 2\n'
            "        .code\n%s\n        jmp WBOOT\n        .rodata\n%s\n" % (body, data))


def header_checks():
    com, prl = read(os.path.join(BUILD, "hello.com")), read(os.path.join(BUILD, "hello.prl"))
    hc, hp = mkprl.header(com), mkprl.header(prl)
    ok(com[:4] == b"OOD\x1a" and len(com) == 16 + hc["size"], "hello.com: the magic, then 16 bytes of header and the image")
    ok(hc["flags"] == 0 and hc["page"] == 3, "hello.com: not relocatable, linked at page $03")
    ok(hc["bss"] == 3 and hc["zpbase"] == 0 and hc["zpsize"] == 3, "hello.com: BSS 3 bytes, zero page $00, 3 bytes")
    ok(hc["rsxcount"] == 0 and hc["rsxrec"] == 0, "hello.com: no attached RSXs")
    n = mkprl.bitmap_len(hp["size"])
    ok(hp["flags"] == mkprl.F_RELOC and len(prl) == 16 + hp["size"] + 2 * n, "hello.prl: relocatable, header, image and two bitmaps")
    ok(prl[16:16 + hp["size"]] == com[16:], "hello.prl's image at page $03, zero page $00, is hello.com's")
    pages = sum(bin(b).count("1") for b in prl[16 + hp["size"]:16 + hp["size"] + n])
    zps = sum(bin(b).count("1") for b in prl[16 + hp["size"] + n:])
    ok(pages > 0 and zps > 0, "hello.prl: %d page relocations, %d zero page" % (pages, zps))


def relocation_checks(tmp):
    prl = read(os.path.join(BUILD, "hello.prl"))
    obj = os.path.join(BUILD, "hello.o")
    for page, zp in ((0x03, 0x00), (0x04, 0x02), (0x05, 0x01), (0x10, 0xF0), (0x5A, 0x37), (0x80, 0xF4),
                     (0xA7, 0xFD), (0xBF, 0x00)):
        direct = link(obj, PRL_CFG, os.path.join(tmp, "direct"), page, zp)
        got = mkprl.load(prl, page, zp)
        ok(got == direct[16:], "hello.prl loaded at page $%02X, zero page $%02X, is a direct link there" % (page, zp))
    # The CLI writes the same.
    out = os.path.join(tmp, "img")
    r = run(sys.executable, "-I", "tools/mkprl.py", "--load", "build/hello.prl", "--page", "0x80", "--zp", "0xF4", "-o", out)
    ok(r.returncode == 0 and read(out) == mkprl.load(prl, 0x80, 0xF4), "mkprl.py --load writes the relocated image")
    refused(lambda: mkprl.load(prl, 0x80, 0xFE), "hello.prl with its 3 bytes of zero page at $FE", "zero page")
    refused(lambda: mkprl.load(prl, 0xC0, 0), "hello.prl at page $C0", "don't fit")
    com = read(os.path.join(BUILD, "hello.com"))
    ok(mkprl.load(com) == com[16:], "hello.com loads at $0300 as it is")
    refused(lambda: mkprl.load(com, 0x04), "hello.com at page $04", "only at its link page")


def mkprl_checks(tmp):
    obj = os.path.join(BUILD, "hello.o")
    a = link(obj, PRL_CFG, os.path.join(tmp, "a"))
    b = link(obj, PRL_CFG, os.path.join(tmp, "b"), 4, 2)
    ok(mkprl.make(a, b) == read(os.path.join(BUILD, "hello.prl")), "mkprl: the pair makes the build's hello.prl")
    ok(mkprl.load(mkprl.make(a, link(obj, PRL_CFG, os.path.join(tmp, "c"), 4, 0x10)), 0x22, 0x40)
       == link(obj, PRL_CFG, os.path.join(tmp, "d"), 0x22, 0x40)[16:], "mkprl: a pair $10 of zero page apart works too")
    refused(lambda: mkprl.make(a, a), "mkprl: the same link twice", "one page apart")
    refused(lambda: mkprl.make(a, link(obj, PRL_CFG, os.path.join(tmp, "c"), 5, 2)), "mkprl: two pages apart", "one page apart")
    refused(lambda: mkprl.make(a, link(obj, PRL_CFG, os.path.join(tmp, "c"), 4, 1)), "mkprl: zero page 1 apart", "2 or more apart")
    refused(lambda: mkprl.make(a, link(obj, PRL_CFG, os.path.join(tmp, "c"), 4, 0)), "mkprl: zero page 0 apart", "2 or more apart")
    com = link(obj, COM_CFG, os.path.join(tmp, "com"))
    refused(lambda: mkprl.make(com, b), "mkprl: a .COM link as a", "relocatable flag")
    bad = bytearray(b)
    bad[16 + 0x21] = (bad[16 + 0x21] + 5) & 0xFF
    refused(lambda: mkprl.make(a, bytes(bad)), "mkprl: b with a byte 5 more", "image offset $0021")
    refused(lambda: mkprl.make(a, b[:-1]), "mkprl: b cut short", "too short")
    # A real expression that isn't a relocation: three times an address.
    o = assemble(tmp, "times3", program("        lda #>(msg*3)", "msg: .byte 0"))
    ta, tb = link(o, PRL_CFG, os.path.join(tmp, "ta")), link(o, PRL_CFG, os.path.join(tmp, "tb"), 4, 2)
    refused(lambda: mkprl.make(ta, tb), "mkprl: LDA #>(msg*3)", "image offset $0001 ($0301")
    # A different program as b.
    o2 = assemble(tmp, "other", program("        nop"))
    refused(lambda: mkprl.make(a, link(o2, PRL_CFG, os.path.join(tmp, "ob"), 4, 2)), "mkprl: b another program", "differ")
    r = run(sys.executable, "-I", "tools/mkprl.py", os.path.join(tmp, "ta"), os.path.join(tmp, "tb"), "-o", os.path.join(tmp, "t.prl"))
    ok(r.returncode == 1 and "mkprl: image offset" in r.stderr and not os.path.exists(os.path.join(tmp, "t.prl")),
       "mkprl.py exits 1 and writes nothing: %s" % r.stderr.strip())


def magic_checks():
    com = read(os.path.join(BUILD, "hello.com"))
    prl = read(os.path.join(BUILD, "hello.prl"))
    refused(lambda: mkprl.load(b"\x00" + com[1:]), "a .COM with its magic's first byte cleared", "magic")
    refused(lambda: mkprl.load(prl[:3] + b"\x00" + prl[4:], 0x40, 0), "a .PRL with its magic's ^Z cleared", "magic")
    refused(lambda: mkprl.load(bytes([0xC3, 0x00, 0x01]) + bytes(125)), "an 8080 .COM (JMP $0100)", "magic")
    refused(lambda: mkprl.load(b""), "an empty file", "magic")
    refused(lambda: mkprl.make(com[16:], prl), "mkprl: links without the header", "magic")
    tampered = os.path.join(BUILD, "nomagic.com")
    with open(tampered, "wb") as f:
        f.write(b"XX" + com[2:])
    r = run(sys.executable, "-I", "tools/mkprl.py", "--load", tampered, "-o", tampered + ".img")
    os.remove(tampered)
    ok(r.returncode == 1 and "magic" in r.stderr, "mkprl.py --load exits 1: %s" % r.stderr.strip())


BREAKS = [
    ("rule1", "        .byte $07, zp            ; RMB0 zp", "RMB/SMB/BBR/BBS (rule 1)"),
    ("rule2", "        lda a:zp", "absolute zero page operand (rule 2)"),
    ("rule3", "        lda $0103,x", "absolute stack page operand (rule 3)"),
    ("rule4", "        jsr $F800", "JSR outside the program, not the BDOS (rule 4)"),
    ("data", "        .byte $02", "data in code?"),
]


def rules_checks(tmp):
    r = run(sys.executable, "-I", "tools/rules.py", "build/hello.com", "--map", "build/hello.map")
    ok(r.returncode == 0 and "0 problems" in r.stdout, "rules: hello.com passes: %s" % r.stdout.strip())
    for name, body, expect in BREAKS:
        o = assemble(tmp, name, program("        jsr BDOS\n" + body))
        link(o, COM_CFG, os.path.join(tmp, name + ".com"))
        r = run(sys.executable, "-I", "tools/rules.py", os.path.join(tmp, name + ".com"), "--map", os.path.join(tmp, name + ".com.map"))
        line = r.stdout.splitlines()[0] if r.stdout else ""
        ok(r.returncode == 1 and expect in line and "$0303" in line, "rules: %s refused: %s" % (name, line.replace(tmp + os.sep, "")))
    # The build itself stops: no .COM, no .PRL.
    for kind in ("com", "prl"):
        out = os.path.join(tmp, "build", "rule2." + kind)
        r = run("make", "--no-print-directory", "SRC=" + tmp, "BUILD=" + os.path.join(tmp, "build"), "PROGRAMS=rule2", out)
        ok(r.returncode != 0 and "rule 2" in r.stdout and not os.path.exists(out),
           "make %s with a planted LDA a:zp fails" % os.path.basename(out))
    out = os.path.join(tmp, "build", "rule1.com")
    r = run("make", "--no-print-directory", "SRC=" + tmp, "BUILD=" + os.path.join(tmp, "build"), "PROGRAMS=rule1", out)
    ok(r.returncode != 0 and "rule 1" in r.stdout and not os.path.exists(out), "make rule1.com with a planted RMB0 fails")


def main():
    if not os.path.exists(os.path.join(BUILD, "hello.prl")):
        raise SystemExit("run make first")
    with tempfile.TemporaryDirectory() as tmp:
        print("The header")
        header_checks()
        print("Relocation")
        relocation_checks(tmp)
        print("mkprl")
        mkprl_checks(tmp)
        print("The magic number")
        magic_checks()
        print("The 65C816 rules")
        rules_checks(tmp)
    print("FAIL %d (PASS %d)" % (failed, passed) if failed else "PASS %d" % passed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
