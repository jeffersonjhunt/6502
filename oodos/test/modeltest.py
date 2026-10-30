"""The model test's commands (hw/oodos.md, "The BDOS, phase 3"; oodos/README.md has them in use).

    python3 -I modeltest.py prepare --seed N --format F --dir DIR --driver DRIVER.COM [--profile P] [--ops N]
                                    [--cut N]
        DIR/SCRIPT.BIN, the seed's script; DIR/A.IMG, an oodos-8m drive holding it and DRIVER.COM;
        DIR/T.IMG, the drive under test, empty (made as mkfs.cpm -t, mode 0x31); DIR/setup, what was asked
    python3 -I modeltest.py serve --dir DIR [--fault F]
        the model as the BDOS for test_oodos_model's CPU-only run (the protocol is below); at Q it writes
        DIR/A.out.IMG and DIR/T.out.IMG
    python3 -I modeltest.py predict --dir DIR
        DIR/A.model.IMG, DIR/T.model.IMG and DIR/RESULT.model.BIN: the model's run of the script
    python3 -I modeltest.py check --dir DIR [--compare renumber|exact] [--alloc lowest|nearest] [--loose-x]
        the model's run against what DRIVER.COM left in DIR/A.out.IMG (RESULT.BIN) and DIR/T.out.IMG:
        every result, then the drive's directory and files, then A:'s (all but RESULT.BIN's records,
        which are the results); prints the seed, each operation
        that differs and how, and PASS or FAIL (FAIL too for whatever stops the comparison: no
        RESULT.BIN, a script the model can't predict, an image missing); exits 0 only on PASS
    python3 -I modeltest.py show --dir DIR       the script, an operation a line
    python3 -I modeltest.py seeds                the seeds test_oodos_model compares by default, 1,4,...
    python3 -I modeltest.py files --image IMG --format F --out DIR
        each file on a drive image to DIR/USER/NAME.EXT (a hole as zeros; its last record cut to the byte
        count, S1, if its last entry has one and records, as cpmcp does), and a line for each: the user,
        the name, the type, its first entry's update and create stamps as cpmls -F shows them, and its
        password mode as cpmls -F's Prot (None, Read, Write, Delete: the slot's byte 8, the XFCB's mode)
    python3 -I modeltest.py label --image IMG --format F
        fsck.cpm's line for a label asking for both creation and access stamps (cpmtools 2.23 calls that an
        error; the Guide has them share the field, and OODOS's BDOS takes it), for model_cpmtools.sh to let
        through; and on stderr what fsck.cpm checks of the drive's passwords: its XFCBs and the label's
    python3 -I modeltest.py mkfs --format F -o IMG
        an empty drive, as mkfs.cpm -t at 2026-10-27 12:00 with the label's mode set to 0x31
    python3 -I modeltest.py sparse --image IMG --format F
        fsck.cpm's "Bad record count" line for each entry whose last logical extent has a hole: fsck.cpm
        2.23 takes RC to need exactly that many blocks, so a sparse file (a random write past a gap,
        as CP/M allows) draws it. model_cpmtools.sh lets those lines through, and only those. An entry
        with a block past those RC needs is no sparse file but an RC too small: on stderr, and exits 1

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
FAULTS = ("update", "rc", "code", "data", "a-attr")


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
    if o.cut is not None:                    # the first N operations of the seed's script, to find a difference
        script.ops = script.ops[:o.cut]
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
    never written, an entry's RC one short, open's directory code 1 (the Guide's is 0), a byte of record
    5 of each extent (the RC and the byte on the drive under test, not A:), and (a-attr) f2's attribute bit
    set in each entry close writes on A:, which only A:'s directory shows."""

    fault = None

    def stamp_slot(self, dv, i, made=False, first=False):
        if self.fault == "update" and not made:
            return
        super().stamp_slot(dv, i, made, first)

    def writeback(self, s):
        i = super().writeback(s)
        if self.fault == "rc" and i is not None and s.drive:
            dv = self.drives[s.drive]
            e = bytearray(dv.entry(i))
            if e[15] > 1:
                e[15] -= 1
                dv.set_entry(i, bytes(e))
        if self.fault == "a-attr" and i is not None and not s.drive:
            dv = self.drives[0]
            e = bytearray(dv.entry(i))
            e[2] |= 0x80
            dv.set_entry(i, bytes(e))
        return i

    def f15(self, a, x, mem):
        r = super().f15(a, x, mem)
        return (1, r[1]) if self.fault == "code" and r[0] == 0 else r

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


def check(o, seed, fmt, drive):
    script = S.Script.decode(read(os.path.join(o.dir, "SCRIPT.BIN")))
    out = []
    try:
        b, drv, _ = predict(o.dir, o.alloc)
    except M.Unspecified as e:
        print("seed %d: FAIL: the script isn't one the model can predict from this drive: %s" % (seed, e))
        return 1
    a_out = read(os.path.join(o.dir, "A.out.IMG"))
    t_out = read(os.path.join(o.dir, "T.out.IMG"))
    res = M.Drive(A_FMT, a_out).read_file(0, "RESULT.BIN")
    if res is None:
        print("seed %d: FAIL: no RESULT.BIN on A: (did DRIVER run?)" % seed)
        return 1
    preds = drv.recs
    if o.loose_x:
        for r in preds:
            r.fields = [(n, v, s, "any" if n == "X" and v == 0 else rule) for n, v, s, rule in r.fields]
    bad = S.compare_results(script, preds, res, out)
    bad += S.compare_images(b.drives[drive], t_out, o.compare, out, "%s: (%s)" % (chr(65 + drive), fmt))
    # A:'s directory and SFCBs, and SCRIPT.BIN's and DRIVER.COM's records; RESULT.BIN's records are the
    # results, compared above by their rules (what CP/M 3 leaves open isn't the model's to say).
    bad += S.compare_images(b.drives[0], a_out, o.compare, out, "A: (%s)" % A_FMT, skip=[(0, "RESULT.BIN")])
    for line in out:
        print("seed %d: %s" % (seed, line))
    print("seed %d: %s, %d operations, %s %s" % (seed, "PASS" if not bad else "FAIL %d" % bad, len(script.ops),
                                                fmt, o.compare))
    return 1 if bad else 0


def cmd_check(o):
    # Whatever stops the comparison is a FAIL line too, so that a run of many seeds can't miss one.
    try:
        seed, fmt, drive = setup(o.dir)
    except (OSError, KeyError, ValueError) as e:
        print("%s: FAIL: no setup (%s: %s)" % (o.dir, type(e).__name__, e))
        return 1
    try:
        return check(o, seed, fmt, drive)
    except Exception as e:                 # noqa: BLE001: a missing image, a bad script, a model error
        print("seed %d: FAIL: %s: %s" % (seed, type(e).__name__, e))
        return 1


def cmd_seeds(o):
    print(",".join(str(n) for n in S.DEFAULT_SEEDS))


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
        data = dv.read_file(user, nm)
        last = max(dv.file_entries(user, nm), key=lambda j: ((dv.entry(j)[14] & 0x3F) << 5 | dv.entry(j)[12] & 0x1F, j))
        count = dv.entry(last)[13]                      # the last record's byte count, as 30 set it (2.3.12):
        if dv.entry(last)[15] and 0 < count < 128:      # cpmcp copies that much of the last extent's last record
            data = data[:len(data) - 128 + count]
        write(os.path.join(o.out, str(user), n + ("." + t if t else "")), data)
        sl = dv.slot(first)
        st = dv.img[sl:sl + 9] if sl is not None else bytes(9)
        # cpmls -F reads the mode from the lowest extent's slot, byte 8. An extent-zero entry's must be the
        # file's XFCB's (the Guide, 2.3.8): taken from the XFCB, not the slot, so a slot that has drifted
        # from it fails. A later extent's slot is 0, so with no extent zero cpmls shows none, as with no slot
        x = next((j for j in dv.entries() if dv.entry(j)[0] == user + 16
                  and bytes(c & 0x7F for c in dv.entry(j)[1:12]) == nm), None)
        e0 = ((dv.entry(first)[14] & 0x3F) << 5 | dv.entry(first)[12] & 0x1F) & ~dv.fmt.exm == 0
        mode = dv.entry(x)[12] if x is not None and sl is not None and e0 else 0
        prot = "Read" if mode & 0x80 else "Write" if mode & 0x40 else "Delete" if mode & 0x20 else "None"
        print("%d %s %s %s %s %s" % (user, n, t or "-", cpmls_stamp(st[4:8]), cpmls_stamp(st[:4]), prot))


def sparse(dv):
    """Each entry's last logical extent against its RC. @return (fsck.cpm's "Bad record count" lines for
    the entries with a hole in RC's blocks, which a sparse file has; what's wrong: an entry with a block
    past RC's, which no file should have)"""
    f = dv.fmt
    allowed, errors = [], []
    for i in dv.entries():
        e = dv.entry(i)
        if e[0] > 15:
            continue
        lo = e[12] & f.exm
        first, last = lo * 128 // f.rpb, (lo * 128 + 127) // f.rpb
        bl = dv.blocks(e)[first:last + 1]
        need = (e[15] + f.rpb - 1) // f.rpb
        nm = bytes(c & 0x7F for c in e[1:12]).decode()
        if any(bl[need:]):
            errors.append("entry %d (%s.%s): RC %d, but blocks %s past its %d" % (i, nm[:8], nm[8:], e[15],
                                                                              bl[need:], need))
        elif any(b == 0 for b in bl[:need]):
            allowed.append('Error: Bad record count (extent=%d, name="%s.%s", record count=%d)' % (
                i, nm[:8], nm[8:], e[15]))
    return allowed, errors


def cmd_sparse(o):
    allowed, errors = sparse(M.Drive(o.format, read(o.image)))
    for line in allowed:
        print(line)
    for line in errors:
        print("modeltest.py sparse: %s" % line, file=sys.stderr)
    return 1 if errors else 0


def cmd_label(o):
    dv = M.Drive(o.format, read(o.image))
    xfcbs = sum(1 for i in dv.entries() if 16 <= dv.entry(i)[0] < 32 and dv.entry(i)[12] & M.XFCB_MODES)
    for i in dv.entries():
        e = dv.entry(i)
        if e[0] == M.LABEL:
            if e[12] & M.MODE_ACCESS and e[12] & M.MODE_CREATE:
                print("Error: Bit 4 and 6 can only be exclusively be set (extent=%d, label byte=0x%02x)" % (i, e[12]))
            print("modeltest.py label: the label's byte 0x%02X%s, %d XFCBs with a mode" % (
                e[12], ", its password" if e[12] & M.MODE_PASSWORD else "", xfcbs), file=sys.stderr)
            break


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
    q.add_argument("--cut", type=int)
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
    q = sub.add_parser("label")
    q.add_argument("--image", required=True)
    q.add_argument("--format", required=True, choices=sorted(M.FORMATS))
    sub.add_parser("seeds")
    q = sub.add_parser("mkfs")
    q.add_argument("--format", required=True, choices=sorted(M.FORMATS))
    q.add_argument("-o", required=True)
    o = p.parse_args()
    return globals()["cmd_" + o.cmd](o) or 0


if __name__ == "__main__":
    sys.exit(main())
