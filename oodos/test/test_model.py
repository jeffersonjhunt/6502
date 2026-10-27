"""The model's own tests (cpm3model.py, modelscript.py): ctest's test_oodos_model_py, and make check in
oodos/test/driver.

    python3 -I test_model.py

Hand-worked CP/M 3 cases from the Programmer's Guide's rules, each a small scenario and the directory
bytes it must leave (worked out here by hand: the DPBs, where each extent's entry goes, its EX, RC and
blocks, the SFCB slots), then the script format, the generator, the reference driver's results and the
comparison, which must find what's wrong. Ends PASS n, or FAIL n.
"""

import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cpm3model as M          # noqa: E402
import modelscript as S        # noqa: E402

passed = failed = 0
T0 = M.stamp(2026, 10, 27, 12, 0)
T1 = M.stamp(2026, 10, 28, 9, 30)
FCB = 0x1000
FCB2 = 0x1100


def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
        print("ok    %s" % what)
    else:
        failed += 1
        print("FAIL  %s" % what)


def show(v):
    return v.hex() if isinstance(v, (bytes, bytearray)) else repr(v)


def eq(got, want, what):
    ok(got == want, what if got == want else "%s: got %s, want %s" % (what, show(got), show(want)))


def h(text):
    return bytes.fromhex(text.replace(" ", ""))


def name(text):
    return M.fcb_name(text).hex()


class Rig:
    """One drive (A:, the drive under test) and the model's BDOS, with a program's memory."""

    def __init__(self, fmt="oodos-ram", alloc="lowest", mode=0x31):
        self.dv = M.Drive(fmt, M.mkfs(M.FORMATS[fmt], T0, mode=mode))
        self.b = M.Bdos({0: self.dv}, now=T0, alloc=alloc)
        self.m = M.Memory()
        self.call(26, 0x00, 0x08)               # DMA at $0800

    def call(self, fn, a=0, x=0):
        return self.b.call(fn, a, x, self.m)

    def fcb(self, text, addr=FCB, ex=0, r=0, dr=1, raw=None):
        f = bytearray(36)
        f[0] = dr
        f[1:12] = raw if raw is not None else M.fcb_name(text)
        f[12] = ex
        f[33:36] = bytes([r & 0xFF, r >> 8 & 0xFF, r >> 16])
        self.m.write(addr, bytes(f))

    def f(self, fn, addr=FCB):
        return self.call(fn, addr & 0xFF, addr >> 8)

    def rand(self, r, addr=FCB):
        self.m.write(addr + 33, bytes([r & 0xFF, r >> 8 & 0xFF, r >> 16]))

    def write(self, n, addr=FCB, tag=0x100, fn=21):
        for k in range(n):
            self.m.write(0x0800, S.pattern(tag + k))
            r = self.f(fn, addr)
            if r != (0, 0):
                return r, k
        return (0, 0), n

    def new(self, text, n=0, addr=FCB, when=None):
        self.fcb(text, addr)
        r = self.f(22, addr)
        if when:
            self.b.now = when
        self.write(n, addr)
        return r

    def e(self, i):
        return self.dv.entry(i)

    def slot(self, i):
        o = self.dv.slot(i)
        return bytes(self.dv.img[o:o + 10])

    def dma(self, n=128):
        return self.m.read(0x0800, n)

    def r(self, addr=FCB):
        b = self.m.read(addr, 36)
        return b[33] | b[34] << 8 | b[35] << 16

    def free(self):
        self.call(46)
        return int.from_bytes(self.dma(3), "little")


def entry(user, text, ex=0, s2=0, rc=0, blocks=(), big=False, s1=0):
    """A file entry's 32 bytes, from the fields worked out by hand."""
    e = bytes([user]) + M.fcb_name(text) + bytes([ex, s1, s2, rc])
    blocks = list(blocks)
    if big:
        mp = b"".join(bytes([b & 0xFF, b >> 8]) for b in blocks) + bytes(2 * (8 - len(blocks)))
    else:
        mp = bytes(blocks) + bytes(16 - len(blocks))
    return e + mp


def formats():
    print("The formats: DPBs worked out from oodos/diskdefs by CP/M 3's rules")
    # SPT, BSH, BLM, EXM, DSM, DRM, AL0, AL1, CKS, OFF, PSH, PHM
    eq(M.FORMATS["oodos-8m"].dpb(), h("4000 05 1f 01 ff07 ff03 ff 00 0080 0000 02 03"),
       "oodos-8m: 64 records a track, 4 KB blocks (BSH 5), EXM 1 (16-bit blocks), 2048 blocks, 1024 entries in 8 blocks")
    eq(M.FORMATS["oodos-32m"].dpb(), h("4000 06 3f 03 ff0f ff07 ff 00 0080 0000 02 03"),
       "oodos-32m: 8 KB blocks (BSH 6), EXM 3, 4096 blocks, 2048 entries in 8 blocks")
    eq(M.FORMATS["oodos-ram"].dpb(), h("4000 04 0f 01 3f00 ff00 f0 00 0080 0000 02 03"),
       "oodos-ram: 2 KB blocks (BSH 4), EXM 1 (8-bit blocks under 256), 64 blocks, 256 entries in 4 blocks")
    f = M.FORMATS["oodos-ram"]
    eq((f.big, f.slots, f.rpb, f.dir_blocks), (False, 16, 16, 4), "oodos-ram: 16 one-byte block numbers, 16 records a block")
    f = M.FORMATS["oodos-8m"]
    eq((f.big, f.slots, f.rpb, f.dir_blocks), (True, 8, 32, 8), "oodos-8m: 8 two-byte block numbers, 32 records a block")


def mkfs():
    print("mkfs: as mkfs.cpm -t, the label's mode 0x31")
    # sd/oodos-ram.sparse is cpmtools's own, made at 2026-10-27 12:00 (oodos/sys/test/make_mdisk.sh).
    path = os.path.join(HERE, "..", "..", "emulator", "test", "host", "sd", "oodos-ram.sparse")
    lines = open(path).read().split("\n")
    img = bytearray(int(lines[1].split()[1]) * 512)
    for line in lines[2:]:
        if line:
            n, x = line.split()
            img[int(n) * 512:(int(n) + 1) * 512] = bytes.fromhex(x)
    mine = M.mkfs(M.FORMATS["oodos-ram"], T0)
    eq(bytes(mine[:8192]), bytes(img), "oodos-ram's directory is mkfs.cpm -t's byte for byte (sd/oodos-ram.sparse)")
    eq(bytes(mine[:32]), h("20 554e4c4142454c4544 2020 31 000000 0000000000000000") + T0 + T0,
       "entry 0: the label, UNLABELED, mode 0x31, created and updated at 12:00")
    eq(bytes(mine[96:128]), h("21") + h("e5") * 31, "entry 3: an SFCB of $E5s")


def make_write_close():
    print("Make, write, close: entries, blocks and stamps")
    t = Rig()
    eq(t.new("A.TXT"), (1, 0), "make A.TXT: the first free entry, 1; directory code 1")
    eq(t.e(1), h("00" + name("A.TXT") + "00 00 00 00" + "00" * 16), "entry 1: user 0, A.TXT, EX 0, RC 0, no blocks")
    eq(t.slot(1), T0 + T0 + h("0000"), "its SFCB slot (entry 3, slot 1): created and updated now, mode 0")
    eq(t.e(3)[1:11] + t.e(3)[21:], h("e5") * 21, "the label's and entry 2's slots untouched")
    t.b.now = T1
    eq(t.write(1)[0], (0, 0), "write a record")
    eq(t.e(1)[15], 0, "the entry's RC is still 0: the directory changes at close")
    eq(t.f(16), (1, 0), "close: directory code 1")
    eq(t.e(1), h("00" + name("A.TXT") + "00 00 00 01 04" + "00" * 15), "entry 1: RC 1, block 4 (the first after the directory's 0-3)")
    eq(t.slot(1), T0 + T1 + h("0000"), "close stamped the update, kept the creation")
    eq(t.f(16), (0, 0), "a second close, nothing written since: 0, and nothing stamped")

    t = Rig()
    t.new("B.DAT", 129)
    t.f(16)
    eq(t.e(1), entry(0, "B.DAT", ex=1, rc=1, blocks=range(4, 13)),
       "129 records (EXM 1): one entry, EX 1 (its second logical extent), RC 1, blocks 4-12")
    t.fcb("B.DAT", FCB2)
    t.f(15, FCB2)
    b = t.m.read(FCB2, 36)
    eq((b[12], b[15]), (0, 128), "open: the FCB at EX 0, its RC 128 (below the entry's EX)")
    t = Rig()
    t.new("B.DAT", 257)
    t.f(16)
    eq(t.e(1), entry(0, "B.DAT", ex=1, rc=128, blocks=range(4, 20)), "257 records: entry 1 full, EX 1, RC 128, 16 blocks")
    eq(t.e(2), entry(0, "B.DAT", ex=2, rc=1, blocks=[20]), "entry 2: EX 2, RC 1, block 20")
    eq(t.slot(2)[:4], T0, "entry 2's slot stamped when the write made it")

    t = Rig("oodos-8m")
    t.new("C.BIN", 1)
    t.f(16)
    eq(t.e(1), h("00" + name("C.BIN") + "00 00 00 01 0800" + "00" * 14), "oodos-8m, 1 record: block 8 as two bytes, 08 00")
    t = Rig("oodos-8m")
    t.new("C.BIN", 257)
    t.f(16)
    eq(t.e(1), entry(0, "C.BIN", ex=1, rc=128, blocks=range(8, 16), big=True), "oodos-8m, 257 records: 8 blocks of 32 records, EX 1")
    eq(t.e(2), entry(0, "C.BIN", ex=2, rc=1, blocks=[16], big=True), "and EX 2, RC 1, block 16")
    t = Rig("oodos-32m")
    t.new("D.BIN", 513)
    t.f(16)
    eq(t.e(1), entry(0, "D.BIN", ex=3, rc=128, blocks=range(8, 16), big=True), "oodos-32m, 513 records: EXM 3, an entry is 512, EX 3")
    eq(t.e(2), entry(0, "D.BIN", ex=4, rc=1, blocks=[16], big=True), "and EX 4, RC 1")


def random_records():
    print("Random records and sparse files")
    t = Rig("oodos-8m")
    t.new("R.DAT")
    t.rand(65535)
    t.m.write(0x0800, S.pattern(7))
    eq(t.f(34), (0, 0), "write record 65535 of a new file")
    b = t.m.read(FCB, 36)
    eq((b[12], b[14], b[32]), (31, 15, 127), "the FCB: EX 31, S2 15, CR 127 (the record's, not the next)")
    t.f(16)
    eq(t.e(1), entry(0, "R.DAT"), "entry 1, make's, still extent 0 and empty")
    eq(t.e(2), entry(0, "R.DAT", ex=31, s2=15, rc=128, blocks=[0] * 7 + [8], big=True),
       "entry 2: extent 511 (S2 15, EX 31), RC 128, block 8 in its last slot")
    t.fcb("R.DAT", FCB2)
    eq(t.f(35, FCB2), (0, 0), "35: found")
    eq(t.r(FCB2), 65536, "file size 65,536 records")
    t.fcb("R.DAT", FCB2)
    t.f(15, FCB2)
    t.rand(65535, FCB2)
    eq((t.f(33, FCB2), t.dma()), ((0, 0), S.pattern(7)), "read record 65535 back")

    t = Rig()
    t.new("S.DAT")
    for r, tag in ((0, 1), (300, 2)):
        t.rand(r)
        t.m.write(0x0800, S.pattern(tag))
        t.f(34)
    t.f(16)
    eq(t.e(1), entry(0, "S.DAT", rc=1, blocks=[4]), "records 0 and 300: entry 1, extent 0, RC 1, block 4")
    eq(t.e(2), entry(0, "S.DAT", ex=2, rc=45, blocks=[0, 0, 5]),
       "entry 2: extents 2-3, EX 2, RC 45 (300 is 2 x 128 + 44), block 5 in slot 2 (record 44, 16 a block)")
    t.fcb("S.DAT", FCB2)
    t.f(35, FCB2)
    eq(t.r(FCB2), 301, "size 301")
    t.fcb("S.DAT")
    t.f(15)
    for r, want, what in ((150, 1, "extent 1, past entry 1's EX: unwritten (1)"),
                          (600, 4, "extent 4: no entry (4)"),
                          (260, 1, "a record in the hole of entry 2's slot 0 (1)"),
                          (290, 0, "290 in block 5, written or not (0)"),
                          (262144, 6, "262,144: out of range (6)"),
                          (300, 0, "300: there")):
        t.rand(r)
        eq(t.f(33)[0], want, "random read %d: %s" % (r, what))
    eq(t.dma(), S.pattern(2), "record 300's bytes")

    t = Rig()
    t.new("U.DAT")
    t.rand(10)
    t.m.write(0x0800, S.pattern(3))
    t.f(34)
    t.rand(3)
    eq(t.f(33), (0, 0), "34 at 10, read 3: the block is there")
    ok(t.b.undefined, "but record 3 is undefined (34 doesn't fill the block)")
    t = Rig()
    t.new("Z.DAT")
    t.rand(10)
    t.m.write(0x0800, S.pattern(3))
    t.f(40)
    t.rand(3)
    t.f(33)
    ok(not t.b.undefined and t.dma() == bytes(128), "40 at 10, read 3: zeros, the new block filled")

    t = Rig()
    t.new("Q.DAT", 3)
    t.f(36)
    eq(t.r(), 3, "36 after three writes: record 3")
    t.rand(1)
    t.f(33)
    t.f(36)
    eq(t.r(), 1, "after a random read of 1, 36 gives 1: the record read, not the next")
    t.f(16)
    t.fcb("Q.DAT")
    t.f(15)
    eq([t.f(20)[0] for _ in range(4)], [0, 0, 0, 1], "read 3 records, the fourth is end of file (1)")


def errors():
    print("Errors (error mode $FF: A = $FF, X = CP/M 3's H)")
    t = Rig()
    t.new("A.TXT")
    t.f(16)
    t.fcb("A.TXT", FCB2)
    eq(t.f(22, FCB2), (0xFF, 8), "make A.TXT again: file exists (8)")
    t.fcb("A?.TXT", FCB2)
    eq(t.f(22, FCB2), (0xFF, 9), "make A?.TXT: '?' in the name (9)")
    eq(t.f(15, FCB2), (0xFF, 9), "open A?.TXT: 9")
    t.fcb("NONE.X", FCB2)
    eq(t.f(15, FCB2), (0xFF, 0), "open a file that isn't there: $FF, 0")
    eq(t.call(14, 7), (0xFF, 4), "select H:: select error (4)")
    t.fcb("A.TXT", FCB2, dr=8)
    eq(t.f(15, FCB2), (0xFF, 4), "open on H:: 4")
    eq(t.call(46, 7), (0xFF, 4), "46 of H:: 4")
    t.call(14, 0)
    eq(t.call(28), (0, 0), "28: A: read only")
    eq(t.call(29), (1, 0), "29: the vector, bit 0")
    t.fcb("NEW.TXT", FCB2)
    eq(t.f(22, FCB2), (0xFF, 2), "make on a read-only drive: 2")
    t.fcb("A.TXT", FCB2)
    eq(t.f(19, FCB2), (0xFF, 2), "delete: 2")
    t.call(37, 1)
    eq(t.call(29), (0, 0), "37 of A: makes it read-write")
    t.fcb("NEW.TXT", FCB2)
    eq(t.f(22, FCB2), (2, 0), "and make works (entry 2)")
    eq((t.call(12), t.call(27), t.call(25)), ((0x31, 0), (0xFF, 0xFF), (0, 0)), "12: $0031; 27: $FFFF; 25: A:")
    t.call(13)
    eq(t.call(24), (0, 0), "13 logs every drive out")
    t.call(14, 0)
    eq(t.call(24), (1, 0), "and 14 logs A: in")


def users():
    print("User numbers, SYS files in user 0, attributes")
    t = Rig()
    t.call(32, 3)
    t.new("X.DAT", 1)
    t.f(16)
    eq(t.e(1), entry(3, "X.DAT", rc=1, blocks=[4]), "made in user 3: byte 0 is 3")
    eq(t.call(32, 0xFF), (3, 0), "32 with $FF: user 3")
    t.call(32, 0)
    t.fcb("X.DAT")
    eq(t.f(17), (0xFF, 0), "user 0's search doesn't see it")
    eq(t.f(15), (0xFF, 0), "nor open")
    t.call(32, 3)
    t.fcb("X.DAT")
    eq(t.f(17), (1, 0), "user 3's search: entry 1")
    eq(t.dma(64)[32:64], entry(3, "X.DAT", rc=1, blocks=[4]), "the DMA buffer holds the directory record")

    t = Rig()
    t.new("S.COM", 1)
    t.f(16)
    t.call(32, 5)
    t.fcb("S.COM")
    eq(t.f(15), (0xFF, 0), "user 5 can't open user 0's S.COM without SYS")
    t.call(32, 0)
    t.fcb("", raw=M.fcb_name("S.COM")[:9] + bytes([ord("O") | 0x80, ord("M")]))
    ok(t.f(30)[0] < 4, "30: set t2' (SYS) on S.COM")
    eq(t.e(1)[10], 0xCF, "the entry's t2' is set (O | $80)")
    t.call(32, 5)
    t.fcb("S.COM")
    eq(t.f(15), (1, 0), "user 5 opens user 0's SYS file")
    eq(t.f(20), (0, 0), "and reads it")
    t.rand(0)
    eq(t.f(34), (0xFF, 3), "but can't write it: File R/O (3)")
    t.call(32, 0)
    t.fcb("", raw=M.fcb_name("S.COM")[:8] + bytes([ord("C") | 0x80, ord("O"), ord("M")]))
    t.f(30)
    eq(t.e(1)[9:12], bytes([0xC3, 0x4F, 0x4D]), "30 again: t1' (R/O) set and t2' cleared, as the FCB says")
    t.fcb("S.COM")
    t.f(15)
    t.rand(0)
    eq(t.f(34), (0xFF, 3), "a R/O file: write is 3")
    t.fcb("S.COM", FCB2)
    eq(t.f(19, FCB2), (0xFF, 3), "delete is 3")
    t.fcb("S.COM", FCB2)
    t.m.write(FCB2 + 16, bytes([1]) + M.fcb_name("T.COM") + bytes(4))
    eq(t.f(23, FCB2), (0xFF, 3), "rename is 3")


def rename_delete():
    print("Rename and delete across extents, '?' searches")
    t = Rig()
    t.new("A.TXT", 257)
    t.f(16)
    t.new("B.TXT", 1, FCB2)
    t.f(16, FCB2)
    eq(t.free(), (60 - 17 - 1) * 16, "free: 60 blocks less A.TXT's 17 and B.TXT's 1, in records")
    t.fcb("A.TXT", FCB2)
    t.m.write(FCB2 + 16, bytes([1]) + M.fcb_name("B.TXT") + bytes(4))
    eq(t.f(23, FCB2), (0xFF, 8), "rename A.TXT to B.TXT: B.TXT exists (8)")
    t.m.write(FCB2 + 16, bytes([1]) + M.fcb_name("C.DAT") + bytes(4))
    ok(t.f(23, FCB2)[0] < 4, "rename A.TXT to C.DAT")
    eq((t.e(1)[:12], t.e(2)[:12]), (h("00" + name("C.DAT")),) * 2, "both of its entries")
    t.fcb("????????.???", FCB2, ex=0x3F)
    codes = [t.f(17, FCB2)[0]] + [t.call(18)[0] for _ in range(3)]
    eq(codes, [1, 2, 0, 0xFF], "search ????????.??? any extent: entries 1, 2 (C.DAT), 4 (B.TXT), then $FF")
    t.fcb("????????.???", FCB2, ex=0)
    codes = [t.f(17, FCB2)[0]] + [t.call(18)[0] for _ in range(2)]
    eq(codes, [1, 0, 0xFF], "with EX 0: each file's first entry only, 1 and 4")
    t.fcb("C.DAT", FCB2, ex=2)
    eq(t.f(17, FCB2), (2, 0), "C.DAT, EX 2: entry 2")
    t.fcb("C.DAT", FCB2, ex=1)
    eq(t.f(17, FCB2), (1, 0), "EX 1 is in entry 1 (EX 1 and 0 differ only under EXM)")
    t.fcb("?.DAT", FCB2)
    ok(t.f(19, FCB2)[0] < 4, "delete ?.DAT")
    eq((t.e(1)[0], t.e(1)[1:], t.e(2)[0]), (0xE5, entry(0, "C.DAT", ex=1, rc=128, blocks=range(4, 20))[1:], 0xE5),
       "both entries $E5, the rest of each as it was")
    eq(t.free(), 59 * 16, "free: 59 blocks again (B.TXT has one)")
    t.fcb("", FCB2, dr=0x3F, raw=b"???????????")
    first = (t.f(17, FCB2), t.dma(32)[:1])
    n = 1 + sum(1 for _ in iter(lambda: t.call(18)[0], 0xFF))
    eq((first, n), (((0, 0), b"\x20"), 256), "search with drive '?': every entry, the label first")


def full():
    print("A full directory, a full disk, temporary blocks")
    t = Rig()
    made = 0
    while True:
        t.fcb("F%03d.X" % made)
        if t.f(22)[0] == 0xFF:
            break
        made += 1
    eq(made, 191, "191 files: 256 entries less 64 SFCBs and the label")
    eq(t.f(22), (0xFF, 0), "the 192nd: $FF, 0 (no directory space)")

    t = Rig()
    t.new("BIG.DAT")
    r, n = t.write(961)
    eq((r, n), ((2, 0), 960), "960 records fill M: (60 blocks of 16); the 961st: 2 (no block)")
    t.f(16)
    eq([t.e(i)[12:16] for i in (1, 2, 4, 5)], [h("01000080"), h("03000080"), h("05000080"), h("07000040")],
       "four entries (3 is an SFCB): EX 1, 3, 5 with RC 128, then EX 7 with RC 64")
    eq(t.dv.blocks(t.e(5)), list(range(52, 64)) + [0] * 4, "the last entry: blocks 52-63")

    t = Rig()
    t.new("T.DAT", 20)
    eq(t.free(), 58 * 16, "20 records written, not closed: 2 blocks taken")
    t.call(98)
    eq(t.free(), 60 * 16, "98 gives them back: the directory has none")
    eq(t.e(1)[12:17], h("0000000000"), "the entry as make left it")


def truncate():
    print("Truncate")
    t = Rig()
    t.new("T.DAT", 300)
    t.f(16)
    t.fcb("T.DAT", FCB2, r=99)
    eq(t.f(99, FCB2), (0, 0), "truncate to record 99")
    eq(t.e(1), entry(0, "T.DAT", ex=0, rc=100, blocks=range(4, 11)), "entry 1: EX 0, RC 100, blocks 4-10 (records 0-111)")
    eq(t.e(2)[0], 0xE5, "entry 2 deleted")
    eq(t.free(), (60 - 7) * 16, "the blocks after given back")
    t.fcb("T.DAT", FCB2, r=100)
    eq(t.f(99, FCB2), (0xFF, 0), "record 100, past the end now: $FF")
    t.fcb("T.DAT", FCB2)
    t.f(35, FCB2)
    eq(t.r(FCB2), 100, "size 100")


def stamps_and_modes():
    print("Stamps by the label's mode")
    for mode, want, what in ((0x31, T0 + T1, "0x31: creation and update"), (0x11, T0 + bytes(4), "0x11: creation only"),
                             (0x21, bytes(4) + T1, "0x21: update only"), (0x01, None, "0x01: none")):
        t = Rig(mode=mode)
        t.new("A.TXT", 1, when=T1)
        t.f(16)
        got = t.slot(1)
        eq(got[:8] if want else got, want or h("e5") * 10, "label mode %s" % what)


def policy():
    print("The allocation policy")
    for alloc, want in (("lowest", 4), ("nearest", 8)):
        t = Rig(alloc=alloc)
        t.new("A.DAT", 16)
        t.f(16)
        t.new("B.DAT", 48, FCB2)
        t.f(16, FCB2)
        t.fcb("A.DAT")
        t.f(19)
        t.fcb("B.DAT", FCB2)
        t.f(15, FCB2)
        t.rand(48, FCB2)
        t.m.write(0x0800, S.pattern(9))
        t.f(34, FCB2)
        t.f(16, FCB2)
        eq(t.dv.blocks(t.e(2))[3], want, "%s: B.DAT's fourth block (after 5-7, with 4 free): %d" % (alloc, want))


def unspecified():
    print("What CP/M 3 leaves open is refused before anything changes")
    t = Rig()
    t.new("A.DAT", 1)
    t.fcb("A.DAT", FCB2)
    t.f(15, FCB2)
    before = bytes(t.dv.img)
    for what, fn in (("a second FCB writing a file being written", lambda: t.write(1, FCB2)),
                     ("the user changed under an open FCB", lambda: (t.call(32, 4), t.write(1))),
                     ("18 not after 17", lambda: t.call(18))):
        try:
            fn()
            ok(False, "%s: not refused" % what)
        except M.Unspecified as e:
            ok(bytes(t.dv.img) == before, "%s: %s; nothing changed" % (what, e))


def scripts():
    print("Scripts: the format, the generator, the results, the comparison")
    s = S.Script(12, 77, [(S.OP_CALL, bytes([14, 12, 0])), (S.OP_FCB, bytes(21)), (S.OP_NEXT, b"")])
    data = s.encode()
    eq(data[:16], h("4f4f4d53 01 0c 0300 1b00 4d000000 0000"), "the header: OOMS, 1, M:, 3 operations, 27 bytes, seed 77")
    eq(S.Script.decode(data).encode(), data, "decode then encode: the same bytes")
    for bad, what in ((b"XXMS" + data[4:], "the magic"), (data[:6] + b"\x04" + data[7:], "a count past the end"),
                      (data[:8] + b"\x1c" + data[9:], "a length that isn't the operations'")):
        try:
            S.Script.decode(bad)
            ok(False, "a bad script (%s): not refused" % what)
        except (ValueError, IndexError):
            ok(True, "a bad script (%s): refused" % what)
    eq(S.pattern(0x0102)[:4], bytes([2 ^ 0, 3 ^ 1, 4 ^ 2, 5 ^ 3]), "pattern of $0102: step 1, from 2, XOR i")
    eq(S.pattern(0x0405)[:3], bytes([5, (5 + 5) ^ 1, (5 + 10) ^ 2]), "pattern of $0405: step 5 (4 | 1)")
    eq(S.fletcher(b"\x01\x02\x03"), (6, 10), "Fletcher's: 1, 3, 6, summed again: 10")

    seen = {}
    for seed in (1, 2, 3):
        for fmt, drive in (("oodos-ram", 12), ("oodos-8m", 1)):
            a = S.Generator(seed, fmt, drive).generate().encode()
            b = S.Generator(seed, fmt, drive).generate().encode()
            ok(a == b, "seed %d %s: the generator makes the same script twice (%d bytes)" % (seed, fmt, len(a)))
            ok(len(a) <= S.SCRIPT_MAX, "and it fits DRIVER.COM's buffer")
            seen[seed, fmt] = a
    ok(len(set(seen.values())) == len(seen), "different seeds and formats make different scripts")

    # The reference driver's run, RESULT.BIN split back by the script, and compared with itself.
    g = S.Generator(4, "oodos-ram", 12)
    sc = g.generate()
    a = M.Drive("oodos-8m", M.mkfs(M.FORMATS["oodos-8m"], T0))
    S.put_file(a, 0, "SCRIPT.BIN", sc.encode(), T0)
    b = M.Bdos({0: a, 12: M.Drive("oodos-ram", M.mkfs(M.FORMATS["oodos-ram"], T0))}, now=T0)
    d = S.Driver(b)
    d.run(None)
    res = a.read_file(0, "RESULT.BIN")
    status, count, parts = S.split_results(sc, res)
    ok(status == 0 and count == len(sc.ops), "RESULT.BIN: status 0, every operation (%d)" % count)
    ok(parts == [r.encode() for r in d.recs], "each result as the driver encoded it")
    ok(len(res) <= S.RESULT_MAX, "the results fit DRIVER.COM's buffer (%d bytes)" % len(res))
    kinds = {op for op, _ in sc.ops}
    ok(kinds >= {S.OP_FCB, S.OP_FCALL, S.OP_READ, S.OP_WRITE, S.OP_CALL, S.OP_TIME}, "the script uses most operations")
    out = []
    ok(S.compare_results(sc, d.recs, res, out) == 0, "the results compare equal with themselves")
    i = next(k for k, (op, _) in enumerate(sc.ops) if op == S.OP_WRITE)
    off = 16 + sum(len(p) for p in parts[:i]) + 1
    bad = bytearray(res)
    bad[off] ^= 0x40
    out = []
    ok(S.compare_results(sc, d.recs, bytes(bad), out) == 1 and ("op %d " % i) in out[0],
       "a write's A changed: found, by its operation: %s" % (out[0] if out else ""))
    dv = b.drives[12]
    out = []
    ok(S.compare_images(dv, bytes(dv.img), "exact", out) == 0, "the drive compares equal with itself, exactly")
    files = [i for i in dv.entries() if dv.entry(i)[0] <= 15 and any(dv.blocks(dv.entry(i)))]
    ok(len(files) > 0, "the script left files with blocks")
    # The same drive with one of its blocks moved, the entries to match: equal but for the numbering.
    sw = M.Drive(dv.fmt, bytes(dv.img))
    used = sorted({x for i in files for x in dv.blocks(dv.entry(i)) if x})
    free = [x for x in range(dv.fmt.dir_blocks, dv.fmt.dsm + 1) if x not in used]
    ok(bool(free), "a free block to move one to")
    src, dst = used[0], free[-1]
    bs = dv.fmt.bls
    sw.img[dst * bs:(dst + 1) * bs] = dv.img[src * bs:(src + 1) * bs]
    for i in files:
        sw.set_entry(i, sw.put_blocks(sw.entry(i), [dst if x == src else x for x in sw.blocks(sw.entry(i))]))
    out = []
    ok(S.compare_images(dv, bytes(sw.img), "renumber", out) == 0, "a block moved: the same up to renumbering")
    out = []
    ok(S.compare_images(dv, bytes(sw.img), "exact", out) > 0, "but not exactly: %s" % (out[0] if out else ""))
    n = next(k for k in range(dv.fmt.rpb) if not dv.read_rec(src, k)[1])
    sw.img[dst * bs + n * 128] ^= 1
    out = []
    ok(S.compare_images(dv, bytes(sw.img), "renumber", out) > 0, "a byte changed in it: found: %s" % (out[0] if out else ""))
    e = files[0]
    wr = bytearray(dv.img)
    wr[e * 32 + 15] ^= 1
    out = []
    ok(S.compare_images(dv, bytes(wr), "renumber", out) > 0, "an RC changed: found: %s" % (out[0] if out else ""))
    o = dv.slot(e)
    wr = bytearray(dv.img)
    wr[o + 7] ^= 1
    out = []
    ok(S.compare_images(dv, bytes(wr), "renumber", out) > 0, "a stamp changed: found: %s" % (out[0] if out else ""))


def main():
    formats()
    mkfs()
    make_write_close()
    random_records()
    errors()
    users()
    rename_delete()
    full()
    truncate()
    stamps_and_modes()
    policy()
    unspecified()
    scripts()
    print("FAIL %d (PASS %d)" % (failed, passed) if failed else "PASS %d" % passed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
