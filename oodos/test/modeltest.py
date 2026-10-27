"""The model test's commands (hw/oodos.md, "The BDOS, phase 3"; oodos/README.md has them in use).

    python3 -I modeltest.py prepare --seed N --format F --dir DIR --driver DRIVER.COM [--profile P] [--ops N]
        DIR/SCRIPT.BIN, the seed's script; DIR/A.IMG, an oodos-8m drive holding it and DRIVER.COM;
        DIR/T.IMG, the drive under test, empty (made as mkfs.cpm -t, mode 0x31); DIR/setup, what was asked
    python3 -I modeltest.py serve --dir DIR [--fault F]
        the model as the BDOS for test_oodos_model's CPU-only run (the protocol is below); at Q it writes
        DIR/A.out.IMG and DIR/T.out.IMG
    python3 -I modeltest.py predict --dir DIR
        DIR/A.model.IMG, DIR/T.model.IMG and DIR/RESULT.model.BIN: the model's run of the script
    python3 -I modeltest.py check --dir DIR [--compare renumber|exact] [--alloc lowest|nearest] [--loose-x]
        the model's run against what DRIVER.COM left in DIR/A.out.IMG (RESULT.BIN) and DIR/T.out.IMG:
        every result, then the drive's directory and files; prints the seed, each operation that differs
        and how, and PASS or FAIL; exits 0 only on PASS
    python3 -I modeltest.py show --dir DIR       the script, an operation a line
    python3 -I modeltest.py files --image IMG --format F --out DIR
        each file on a drive image to DIR/USER/NAME.EXT (a hole as zeros), and a line for each: the user,
        the name, the type, its first entry's update and create stamps as cpmls -F shows them
    python3 -I modeltest.py mkfs --format F -o IMG
        an empty drive, as mkfs.cpm -t at 2026-10-27 12:00 with the label's mode set to 0x31
    python3 -I modeltest.py sparse --image IMG --format F
        fsck.cpm's "Bad record count" line for each entry whose last logical extent has a hole: fsck.cpm
        2.23 takes RC to need exactly that many blocks, so a sparse file (a random write past a gap,
        as CP/M allows) draws it. model_cpmtools.sh lets those lines through, and only those

--compare exact compares the images byte for byte (the BDOS allocating as --alloc); renumber, the
default, the same but for the blocks' numbers, which need only be one to one. --loose-x doesn't compare
X where the model's is 0 (the BDOS's H on success).

The serve protocol, a line each way, numbers decimal, bytes hex: the host sends "C fn a x"; the model
answers with any of "R addr n" (the host replies with the n bytes), "W addr bytes", "O bytes" (console
output), then "E a x" (return these); or "X why" for an operation CP/M 3 leaves open. "Q" ends it.
"""

import argparse
import os
import sys

sys.dont_write_bytecode = True                       # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cpm3model as M          # noqa: E402
import modelscript as S        # noqa: E402

START = M.stamp(2026, 10, 27, 12, 0)
A_FMT = "oodos-8m"
FAULTS = ("update", "rc", "code", "data")


def drive_for(fmt):
    return 12 if fmt == "oodos-ram" else 1           # M:, or B: (SD unit 2)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def setup(d):
    s = dict(l.split(" ", 1) for l in read(os.path.join(d, "setup")).decode().splitlines() if " " in l)
    return int(s["seed"]), s["format"], int(s["drive"])


def model(d, a_img, t_img, alloc="lowest"):
    seed, fmt, drive = setup(d)
    return M.Bdos({0: M.Drive(A_FMT, a_img), drive: M.Drive(fmt, t_img)}, now=START, alloc=alloc), drive


def cmd_prepare(o):
    os.makedirs(o.dir, exist_ok=True)
    drive = drive_for(o.format)
    g = S.Generator(o.seed, o.format, drive, nops=o.ops, profile=o.profile)
    script = g.generate()
    data = script.encode()
    a = M.Drive(A_FMT, M.mkfs(M.FORMATS[A_FMT], START))
    S.put_file(a, 0, "SCRIPT.BIN", data, START)
    if o.driver:
        S.put_file(a, 0, "DRIVER.COM", read(o.driver), START)
    write(os.path.join(o.dir, "SCRIPT.BIN"), data)
    write(os.path.join(o.dir, "A.IMG"), a.img)
    write(os.path.join(o.dir, "T.IMG"), M.mkfs(M.FORMATS[o.format], START))
    write(os.path.join(o.dir, "setup"), ("seed %d\nformat %s\ndrive %d\nprofile %s\nops %d\n" % (
        o.seed, o.format, drive, g.profile, len(script.ops))).encode())
    print("seed %d: %s on %s:, %d operations, %d bytes" % (o.seed, o.format, chr(65 + drive), len(script.ops),
                                                          len(data)))


class Faulty(M.Bdos):
    """The model made wrong on purpose, for the test that the comparison can fail: the update stamp
    never written, an entry's RC one short, open's directory code 0, a byte of record 5 of each extent
    (the last two on the drive under test, not A:)."""

    fault = None

    def stamp_slot(self, dv, i, made=False, updated=False):
        if self.fault == "update" and updated:
            return
        super().stamp_slot(dv, i, made, updated)

    def writeback(self, s):
        i = super().writeback(s)
        if self.fault == "rc" and i is not None and s.drive:
            dv = self.drives[s.drive]
            e = bytearray(dv.entry(i))
            if e[15] > 1:
                e[15] -= 1
                dv.set_entry(i, bytes(e))
        return i

    def f15(self, a, x, mem):
        r = super().f15(a, x, mem)
        return (0, r[1]) if self.fault == "code" and r[0] < 4 else r

    def _write_rec(self, s, dv, c, data, zero):
        if self.fault == "data" and c == 5 and s.drive:
            data = bytes([data[0] ^ 1]) + data[1:]
        return super()._write_rec(s, dv, c, data, zero)


def cmd_serve(o):
    seed, fmt, drive = setup(o.dir)
    Faulty.fault = o.fault
    a = M.Drive(A_FMT, read(os.path.join(o.dir, "A.IMG")))
    t = M.Drive(fmt, read(os.path.join(o.dir, "T.IMG")))
    b = Faulty({0: a, drive: t}, now=START)
    inp, out = sys.stdin.buffer, sys.stdout.buffer

    def send(line):
        out.write(line.encode() + b"\n")
        out.flush()

    class Remote:
        def read(self, addr, n):
            send("R %d %d" % (addr, n))
            return bytes.fromhex(inp.readline().decode().strip())

        def write(self, addr, data):
            send("W %d %s" % (addr, bytes(data).hex()))

    mem = Remote()
    while True:
        line = inp.readline().decode().split()
        if not line or line[0] == "Q":
            break
        fn, ra, rx = (int(v) for v in line[1:4])
        try:
            n = len(b.out)
            ax = b.call(fn, ra, rx, mem)
            if len(b.out) > n:
                send("O %s" % bytes(b.out[n:]).hex())
            send("E %d %d" % ax)
        except M.Unspecified as e:
            send("X %s" % e)
    write(os.path.join(o.dir, "A.out.IMG"), a.img)
    write(os.path.join(o.dir, "T.out.IMG"), t.img)
    send("Q")


def predict(d, alloc):
    b, drive = model(d, read(os.path.join(d, "A.IMG")), read(os.path.join(d, "T.IMG")), alloc)
    drv = S.Driver(b)
    drv.run(None)
    return b, drv, drive


def cmd_predict(o):
    b, drv, drive = predict(o.dir, o.alloc)
    write(os.path.join(o.dir, "A.model.IMG"), b.drives[0].img)
    write(os.path.join(o.dir, "T.model.IMG"), b.drives[drive].img)
    write(os.path.join(o.dir, "RESULT.model.BIN"), b.drives[0].read_file(0, "RESULT.BIN"))
    print("predicted: %d operations" % len(drv.recs))


def cmd_check(o):
    seed, fmt, drive = setup(o.dir)
    script = S.Script.decode(read(os.path.join(o.dir, "SCRIPT.BIN")))
    out = []
    try:
        b, drv, _ = predict(o.dir, o.alloc)
    except M.Unspecified as e:
        print("seed %d: the script isn't one the model can predict from this drive: %s" % (seed, e))
        return 1
    a_out = M.Drive(A_FMT, read(os.path.join(o.dir, "A.out.IMG")))
    t_out = read(os.path.join(o.dir, "T.out.IMG"))
    res = a_out.read_file(0, "RESULT.BIN")
    if res is None:
        print("seed %d: no RESULT.BIN on A: (did DRIVER run?)" % seed)
        return 1
    preds = drv.recs
    if o.loose_x:
        for r in preds:
            r.fields = [(n, v, s, "any" if n == "X" and v == 0 else rule) for n, v, s, rule in r.fields]
    bad = S.compare_results(script, preds, res, out)
    bad += S.compare_images(b.drives[drive], t_out, o.compare, out, "%s: (%s)" % (chr(65 + drive), fmt))
    for line in out:
        print("seed %d: %s" % (seed, line))
    print("seed %d: %s, %d operations, %s %s" % (seed, "PASS" if not bad else "FAIL %d" % bad, len(script.ops),
                                                fmt, o.compare))
    return 1 if bad else 0


def cmd_show(o):
    script = S.Script.decode(read(os.path.join(o.dir, "SCRIPT.BIN")))
    for i, (op, args) in enumerate(script.ops):
        print("%4d %s" % (i, S.describe(op, args)))


def cpmls_stamp(s):
    """A stamp as cpmls -F shows it: MM/DD/YY HH:MM."""
    import datetime
    d = datetime.date(1977, 12, 31) + datetime.timedelta(days=s[0] | s[1] << 8)
    return "%02d/%02d/%02d %02x:%02x" % (d.month, d.day, d.year % 100, s[2], s[3])


def cmd_files(o):
    dv = M.Drive(o.format, read(o.image))
    seen = set()
    for i in dv.entries():
        e = dv.entry(i)
        if e[0] > 15 or (e[0], bytes(c & 0x7F for c in e[1:12])) in seen:
            continue
        user, nm = e[0], bytes(c & 0x7F for c in e[1:12])
        seen.add((user, nm))
        first = min(dv.file_entries(user, nm), key=lambda j: ((dv.entry(j)[14] & 0x3F) << 5 | dv.entry(j)[12] & 0x1F, j))
        n, t = nm[:8].decode().rstrip(), nm[8:].decode().rstrip()
        os.makedirs(os.path.join(o.out, str(user)), exist_ok=True)
        write(os.path.join(o.out, str(user), n + ("." + t if t else "")), dv.read_file(user, nm))
        sl = dv.slot(first)
        st = dv.img[sl:sl + 8] if sl is not None else bytes(8)
        print("%d %s %s %s %s" % (user, n, t or "-", cpmls_stamp(st[4:8]), cpmls_stamp(st[:4])))


def cmd_sparse(o):
    dv = M.Drive(o.format, read(o.image))
    f = dv.fmt
    for i in dv.entries():
        e = dv.entry(i)
        if e[0] > 15:
            continue
        lo = e[12] & f.exm
        first, last = lo * 128 // f.rpb, (lo * 128 + 127) // f.rpb
        bl = dv.blocks(e)[first:last + 1]
        need = (e[15] + f.rpb - 1) // f.rpb
        if any(b == 0 for b in bl[:need]) or any(bl[need:]):
            nm = bytes(c & 0x7F for c in e[1:12]).decode()
            print('Error: Bad record count (extent=%d, name="%s.%s", record count=%d)' % (i, nm[:8], nm[8:], e[15]))


def cmd_mkfs(o):
    write(o.o, M.mkfs(M.FORMATS[o.format], START))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("prepare")
    q.add_argument("--seed", type=int, required=True)
    q.add_argument("--format", default="oodos-ram", choices=sorted(M.FORMATS))
    q.add_argument("--dir", required=True)
    q.add_argument("--driver")
    q.add_argument("--profile", choices=sorted(set(S.PROFILES)))
    q.add_argument("--ops", type=int)
    for name in ("serve", "predict", "check", "show"):
        q = sub.add_parser(name)
        q.add_argument("--dir", required=True)
        q.add_argument("--alloc", default="lowest", choices=("lowest", "nearest"))
        if name == "serve":
            q.add_argument("--fault", choices=FAULTS)
        if name == "check":
            q.add_argument("--compare", default="renumber", choices=("renumber", "exact"))
            q.add_argument("--loose-x", action="store_true")
    q = sub.add_parser("files")
    q.add_argument("--image", required=True)
    q.add_argument("--format", required=True, choices=sorted(M.FORMATS))
    q.add_argument("--out", required=True)
    q = sub.add_parser("sparse")
    q.add_argument("--image", required=True)
    q.add_argument("--format", required=True, choices=sorted(M.FORMATS))
    q = sub.add_parser("mkfs")
    q.add_argument("--format", required=True, choices=sorted(M.FORMATS))
    q.add_argument("-o", required=True)
    o = p.parse_args()
    return globals()["cmd_" + o.cmd](o) or 0


if __name__ == "__main__":
    sys.exit(main())
