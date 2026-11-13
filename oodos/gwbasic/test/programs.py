"""GW-BASIC's test programs on the flat image, against PC-BASIC's output (the project's own).

    python3 -I programs.py --runner RUN6502 --image flat.bin [--dir DIR] [NAME...] [-v]

Each DIR/NAME.bas (DIR: programs/ beside this file) is tokenised by basic.py and run in the flat
harness (harness.s: the program at $9000, run through NEWSTT, printed by the stand-in PRINT, which
prints numbers and strings with ";" between them). What it prints must be DIR/NAME.out, PC-BASIC's
output for the same file (tools/oracle.sh), once PC-BASIC's error messages are put as the stand-in's
error printer gives them: "Type mismatch in 20" as "?ERR 13 IN 20".

The stand-in prints an error where the cursor is; GW-BASIC (and PC-BASIC) start a new line first if
the cursor isn't at the start of one, so the 6502's output gets that line break before "?ERR".

Where GW-BASIC's 1983 source and PC-BASIC differ, the port keeps the source's behaviour, and
DIR/NAME.differ is what the 6502 must print instead: its first line, "# why", says why; the rest is
the expected text, line by line. NAME.out stays PC-BASIC's, for the record.

A line "REM @memsize N" in a program puts the top of the string space N bytes past the program's
end (harness.s's MEMSIZ), so the variables and strings have N bytes: a small space, for the garbage
collector to work hard in. PC-BASIC reads it as a remark; such a program prints nothing that
depends on the size. Without it the space ends at $9FEE.

A must-fail can be run against a broken image: the check exits non-zero on any difference.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import basic  # noqa: E402

CTRL, PROGRAM = 0x9FF0, 0x9000

# GW-BASIC's messages for the errors the programs make, and their numbers.
MESSAGES = {
    "Syntax error": 2, "Illegal function call": 5, "Overflow": 6, "Out of memory": 7,
    "Subscript out of range": 9, "Duplicate Definition": 10, "Division by zero": 11,
    "Type mismatch": 13, "Out of string space": 14, "String too long": 15,
    "String formula too complex": 16, "Missing operand": 22, "Advanced Feature": 73,
}


def expected(text):
    """PC-BASIC's output, its error line as the stand-in prints one."""
    out = []
    for line in text.split("\r\n"):
        m = re.match(r"(.*?)([A-Z][A-Za-z ]+) in (\d+)\xff?$", line)
        if m and m.group(2) in MESSAGES:
            line = "%s?ERR %d IN %s" % (m.group(1), MESSAGES[m.group(2)], m.group(3))
        out.append(line)
    return "\r\n".join(out)


def run(runner, image, src, work):
    basic.BASE = PROGRAM
    prog = basic.tokenise(src)
    memsize = 0
    m = re.search(r"REM @memsize (\d+)", src)
    if m:
        memsize = PROGRAM + len(prog) + int(m.group(1))
    pfile = os.path.join(work, "prog.vec")
    cfile = os.path.join(work, "ctrl.vec")
    open(pfile, "wb").write(prog)
    open(cfile, "wb").write(bytes([1, 0, 0, 0, 0, 0, memsize & 0xFF, memsize >> 8]))
    p = subprocess.run([runner, image, "--load", "%X:%s" % (PROGRAM, pfile), "--load", "%X:%s" % (CTRL, cfile),
                        "--max", "400000000"], capture_output=True)
    if p.returncode:
        return None, p.stderr.decode()
    return p.stdout.decode("latin-1"), p.stderr.decode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--dir", default=os.path.join(HERE, "programs"))
    ap.add_argument("-v", action="store_true", help="every differing line")
    ap.add_argument("names", nargs="*")
    a = ap.parse_args()
    names = a.names or sorted(f[:-4] for f in os.listdir(a.dir) if f.endswith(".bas"))
    failed = 0
    with tempfile.TemporaryDirectory() as work:
        for name in names:
            src = open(os.path.join(a.dir, name + ".bas"), encoding="latin-1").read().replace("\r\n", "\n")
            want = expected(open(os.path.join(a.dir, name + ".out"), encoding="latin-1", newline="").read())
            differ = os.path.join(a.dir, name + ".differ")
            if os.path.exists(differ):
                lines = open(differ, encoding="latin-1").read().rstrip("\n").split("\n")
                if not lines[0].startswith("# "):
                    sys.exit("%s: the first line must be '# why'" % differ)
                want = "".join(l + "\r\n" for l in lines[1:])
            got, err = run(os.path.abspath(a.runner), os.path.abspath(a.image), src, work)
            if got is None:
                print("Program %-10s run6502 failed: %s" % (name, err.strip()))
                failed += 1
                continue
            got = re.sub(r"(?<=[^\n])\?ERR ", "\r\n?ERR ", got)
            ok = got == want
            print("Program %-10s %s (%d lines)" % (name, "ok" if ok else "MISMATCH", want.count("\r\n")))
            if not ok:
                failed += 1
                gl, wl = got.split("\r\n"), want.split("\r\n")
                for i in range(max(len(gl), len(wl))):
                    g = gl[i] if i < len(gl) else "<none>"
                    w = wl[i] if i < len(wl) else "<none>"
                    if g != w:
                        print("  line %d: 6502 %r" % (i + 1, g))
                        print("  line %d: want %r" % (i + 1, w))
                        if not a.v:
                            break
    print("%d programs, %d failed: %s" % (len(names), failed, "FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
