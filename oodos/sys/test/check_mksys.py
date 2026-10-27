"""Checks mksys.py (oodos/sys): OODOS.SYS as it builds it, as OOLDR reads it, and every refusal.

    make check                       # from oodos/sys, after make
    python3 -I test/check_mksys.py [BUILD]

The build's OODOS.SYS is read back against ld65's parts it was made of; then files made here, the
edges of what OOLDR takes, and each refusal, by make(), by read() and through the command line.
Every negative prints what refused it. Ends PASS n, or FAIL n.
"""

import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MKSYS = os.path.join(ROOT, "mksys.py")

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("mksys", MKSYS)
mksys = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mksys)

passed = failed = 0
K = 1024
BLOCK = 16 * K


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
    except mksys.Refused as e:
        ok(expect in str(e), "%s: refused: %s" % (what, e))
        return
    ok(False, "%s: not refused" % what)


def mksys_cli(*args):
    return subprocess.run([sys.executable, "-I", MKSYS] + list(args), capture_output=True, text=True)


def built(build):
    """The build's OODOS.SYS against the parts the Makefile gave mksys.py."""
    sysfile = os.path.join(build, "OODOS.SYS")
    data = open(sysfile, "rb").read()
    version, entry, parts = mksys.read(data)
    ok(version == mksys.VERSION, "OODOS.SYS: version %d.%d" % version)
    ok(entry == mksys.label(os.path.join(build, "oodos.lbl"), "COLD"), "OODOS.SYS: the entry is COLD, $%04X" % entry)
    want = [(0xF0000, "bdos"), (0xF4000, "bios"), (0xF8800, "res"), (0xFB800, "common")]
    ok([p for p, _ in parts] == [p for p, _ in want], "OODOS.SYS: four segments, blocks 60, 61, 62 and 62's top")
    for (phys, name), (got_phys, got) in zip(want, parts):
        ok(got == open(os.path.join(build, "oodos." + name), "rb").read(), "OODOS.SYS: $%05X is ld65's %s" % (phys, name))
    ok(len(data) % mksys.RECORD == 0, "OODOS.SYS: whole records, %d" % (len(data) // mksys.RECORD))
    ok(len(parts[3][1]) == 2 * K, "OODOS.SYS: common memory is all 2 KB, to $FFFF")


def layout():
    """make()'s header and records, byte by byte."""
    a, b = bytes(range(1, 130)), b"\xAA" * 3
    data = mksys.make([(0xF8800, a), (0x7C000, b)], 0xC800, (1, 9))
    ok(data[0:4] == b"OOSY" and data[4:6] == b"\x01\x09", "header: magic and version")
    ok(data[6:8] == b"\x00\xC8" and data[8] == 2 and not any(data[9:16]), "header: entry, count, reserved 0")
    ok(data[16:24] == bytes([0x00, 0x88, 0x0F, 129, 0, 1, 0, 0]), "segment 1: address, length, record 1")
    ok(data[24:32] == bytes([0x00, 0xC0, 0x07, 3, 0, 3, 0, 0]), "segment 2: from record 3 (1 + 2 of 129 bytes)")
    ok(not any(data[32:128]), "header: unused entries 0")
    ok(data[128:257] == a and not any(data[257:384]), "segment 1's bytes, then zeros to its record's end")
    ok(data[384:387] == b and len(data) == 512 and not any(data[387:]), "segment 2 on its record, padded")
    version, entry, parts = mksys.read(mksys.make([(0xF8800, a), (0x7C000, b)], 0xC800))
    ok((version, entry, parts) == (mksys.VERSION, 0xC800, [(0xF8800, a), (0x7C000, b)]), "read() gives back what make() took")


def edges():
    """What OOLDR takes at the edges."""
    whole = bytes(BLOCK)
    mksys.make([(31 * BLOCK, whole), (62 * BLOCK + 0x800, b"x")], 0xC800)
    ok(True, "a 16 KB segment filling block 31")
    mksys.make([(60 * BLOCK + 0x3000, bytes(0x2000)), (62 * BLOCK + 0x800, b"x")], 0xC800)
    ok(True, "a segment across blocks 60 and 61")
    mksys.make([(62 * BLOCK + 0x3FFF, b"x")], 0xFFFF)
    ok(True, "the entry at $FFFF, the last byte of block 62")
    mksys.make([(62 * BLOCK + 0x800 + i * 0x100, b"x" * 0x100) for i in range(8)], 0xC800)
    ok(True, "8 segments, end to end")


def refusals():
    seg = (62 * BLOCK + 0x800, b"cold")
    make = lambda parts, entry=0xC800, version=mksys.VERSION: mksys.make(parts, entry, version)
    refused(lambda: make([]), "no segments", "0 segments")
    refused(lambda: make([(62 * BLOCK + 0x800 + i * 16, b"x") for i in range(9)]), "9 segments", "9 segments")
    refused(lambda: make([seg, (60 * BLOCK, b"")]), "an empty segment", "empty")
    refused(lambda: make([seg, (60 * BLOCK, bytes(BLOCK + 1))]), "16 KB and a byte", "over 16 KB")
    refused(lambda: make([seg, (30 * BLOCK, b"x")]), "block 30", "not inside")
    refused(lambda: make([seg, (32 * BLOCK - 1, b"xy")]), "block 31 into 32", "not inside")
    refused(lambda: make([seg, (60 * BLOCK - 1, b"xy")]), "block 59 into 60", "not inside")
    refused(lambda: make([seg, (63 * BLOCK, b"x")]), "block 63, the ROM's", "not inside")
    refused(lambda: make([seg, (62 * BLOCK + 0x3FFF, b"xy")]), "past the end of block 62", "not inside")
    refused(lambda: make([seg, (62 * BLOCK + 0x803, b"x")]), "two overlapping by a byte", "overlap")
    refused(lambda: make([(60 * BLOCK, bytes(100)), seg, (60 * BLOCK + 50, b"x")]), "one inside another", "overlap")
    refused(lambda: make([seg], 0xC7FF), "the entry in the I/O hole", "not in block 62")
    refused(lambda: make([seg], 0x0300), "the entry in the TPA", "not in block 62")
    refused(lambda: make([seg], 0xC900), "the entry in no segment", "in no segment")
    refused(lambda: make([seg], 0xC804), "the entry just past its segment", "in no segment")
    refused(lambda: make([seg], version=(256, 0)), "version 256.0", "0-255")
    good = mksys.make([seg], 0xC800)
    refused(lambda: mksys.read(b"OOSX" + good[4:]), "read: no magic", "no OOSY")
    refused(lambda: mksys.read(good[:100]), "read: shorter than the header", "no OOSY")
    refused(lambda: mksys.read(good[:4] + b"\x01" + good[5:]), "read: a major version it doesn't know", "knows major")
    refused(lambda: mksys.read(good[:12] + b"\x01" + good[13:]), "read: a reserved byte set", "reserved")
    refused(lambda: mksys.read(good[:8] + b"\x00" + good[9:]), "read: 0 segments", "0 segments")
    refused(lambda: mksys.read(good[:8] + b"\x09" + good[9:]), "read: 9 segments", "9 segments")
    refused(lambda: mksys.read(good[:21] + b"\x05\x00" + good[23:]), "read: a segment past the file's end", "aren't in the file")
    refused(lambda: mksys.read(good[:21] + b"\x00\x00" + good[23:]), "read: a segment in the header", "aren't in the file")
    refused(lambda: mksys.read(good[:16] + b"\x00\x00\x08" + good[19:]), "read: a segment in block 32", "not inside")
    refused(lambda: mksys.read(good[:6] + b"\x00\x03" + good[8:]), "read: the entry $0300", "not in block 62")


def cli(build):
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, "part")
        open(part, "wb").write(b"\xEA" * 10)
        out = os.path.join(tmp, "OODOS.SYS")
        lbl = os.path.join(tmp, "x.lbl")
        open(lbl, "w").write("al 00C800 .COLD\nal 00C805 .OTHER\n")
        r = mksys_cli("-o", out, "--entry", "COLD", "--labels", lbl, "0xF8800:" + part)
        ok(r.returncode == 0 and mksys.read(open(out, "rb").read())[1] == 0xC800, "CLI: --entry by label")
        r = mksys_cli("-o", out, "--entry", "0xC800", "0xF8800:" + part)
        ok(r.returncode == 0, "CLI: --entry as a number")
        r = mksys_cli("-o", out, "--entry", "NONE", "--labels", lbl, "0xF8800:" + part)
        ok(r.returncode == 1 and "no label NONE" in r.stderr, "CLI: an unknown label refused: %s" % r.stderr.strip())
        r = mksys_cli("-o", out, "--entry", "0xC800", "0x7B000:" + part)
        ok(r.returncode == 1 and "not inside" in r.stderr, "CLI: block 30 refused: %s" % r.stderr.strip())
        r = mksys_cli("--check", os.path.join(build, "OODOS.SYS"))
        ok(r.returncode == 0 and "block 62" in r.stdout, "CLI: --check lists the build's")
        open(part, "wb").write(b"NOT OODOS.SYS")
        r = mksys_cli("--check", part)
        ok(r.returncode == 1 and "no OOSY" in r.stderr, "CLI: --check refuses what isn't: %s" % r.stderr.strip())


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
    built(build)
    layout()
    edges()
    refusals()
    cli(build)
    print("%s %d" % ("PASS" if not failed else "FAIL", passed if not failed else failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
