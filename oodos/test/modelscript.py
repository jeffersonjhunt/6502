"""The model test's scripts and results (hw/oodos.md, "The BDOS, phase 3"): their format, the reference
driver that runs a script on the model as DRIVER.COM runs it on the BDOS, the seeded generator, and the
comparison of what DRIVER.COM wrote with what the model predicts.

The script, SCRIPT.BIN on drive A:, user 0 (all numbers little-endian):

    +0   "OOMS"
    +4   version, 1
    +5   the drive under test, 0-15 (A-P): for messages only, each operation names its own
    +6   the number of operations (2 bytes)
    +8   their length in bytes (2)
    +10  the seed (4), for messages
    +14  reserved, 0 (2)
    +16  the operations, each an opcode and its arguments

DRIVER.COM keeps 8 FCBs (36 bytes each, slots 0-7) and its own DMA buffer. The operations, with what each
appends to the result (every result starts with the opcode; A and X are what the BDOS returned; "FCB" is
the slot's EX, CR, R0, R1, R2 after the call):

    01 FCB   slot, bytes 0-15, CR, R0, R1, R2: set an FCB, bytes 16-31 zero      -> op
    02 NAME2 slot, bytes 16-31 (rename's new name)                              -> op
    03 RAND  slot, R0, R1, R2                                                   -> op
    10 FCALL fn, slot: the function with A/X = the FCB (15-17, 19, 22, 23, 30, 35, 36, 99)
                                                    -> op A X FCB, and for 17 with A < 4 the 32-byte entry
    11 NEXT  function 18                            -> op A X, and with A < 4 the entry
    12 READ  fn (20, 33), slot: the DMA buffer filled with $5A first, then the call
                                                    -> op A X FCB S1 S2 (the record's checksum)
    13 WRITE fn (21, 34, 40), slot, tag (2): the DMA buffer filled with the tag's pattern -> op A X FCB
    14 WRITEN slot, n (2), tag (2): n sequential writes (21), record k with tag + k, to the first error
                                                    -> op A X done (2) FCB
    15 READN slot, n (2): n sequential reads (20), to the first error -> op A X done (2) S1 S2 FCB
    16 SALL  slot: 17, then 18 to $FF               -> op A X found (2) S1 S2 (A, bytes 0-15 of each entry)
                                                       T1 T2 (bytes 16-31)
    17 MAKEN slot, n (2): n times, FCB bytes 6-8 the number in three digits, bytes 12-35 zero, 22 then
             16, to the first make that fails       -> op A X made (2): A and X the last make's
    20 CALL  fn, A, X: any other function           -> op A X
    21 FREE  drive: function 46                      -> op A X and the DMA buffer's first 3 bytes
    22 DPB   function 31: the 17 bytes at the address it returns -> op A X DPB
    23 TIME  days (2), hour, minute (BCD): function 104 -> op A X

The pattern of tag t: byte i is ((t & $FF) + i * ((t >> 8) | 1)) XOR i. A checksum is Fletcher's over
256: S1 += byte, S2 += S1. After CALL 13 the driver sets its DMA address again (26).

DRIVER.COM first sets error mode $FF (45), resets (13), sets its DMA, user 0, and reads A:SCRIPT.BIN a
record at a time (26 to each, 20) to its end or 16 KB, and closes it. After the operations it does 48, 13,
user 0, deletes and makes A:RESULT.BIN, writes the results (26 to each record, 21), closes it and prints
"DRIVER OK". RESULT.BIN: "OOMR", version 1, a status (0 run, 1 no or bad script, 2 results full), the
operations run (2), the results' length (2), 6 reserved, then the results, zeros to the record's end.
"""

import random
import struct

import cpm3model as M

MAGIC, RMAGIC, VERSION = b"OOMS", b"OOMR", 1
SCRIPT_MAX = 16384                      # DRIVER.COM's buffers
RESULT_MAX = 24576
NSLOTS = 8
FILL = 0x5A

OP_FCB, OP_NAME2, OP_RAND = 0x01, 0x02, 0x03
OP_FCALL, OP_NEXT, OP_READ, OP_WRITE, OP_WRITEN, OP_READN, OP_SALL, OP_MAKEN = range(0x10, 0x18)
OP_CALL, OP_FREE, OP_DPB, OP_TIME = 0x20, 0x21, 0x22, 0x23

ARGS = {OP_FCB: 21, OP_NAME2: 17, OP_RAND: 4, OP_FCALL: 2, OP_NEXT: 0, OP_READ: 2, OP_WRITE: 4,
        OP_WRITEN: 5, OP_READN: 3, OP_SALL: 1, OP_MAKEN: 3, OP_CALL: 3, OP_FREE: 1, OP_DPB: 0, OP_TIME: 4}
NAMES = {OP_FCB: "FCB", OP_NAME2: "NAME2", OP_RAND: "RAND", OP_FCALL: "FCALL", OP_NEXT: "NEXT",
         OP_READ: "READ", OP_WRITE: "WRITE", OP_WRITEN: "WRITEN", OP_READN: "READN", OP_SALL: "SALL",
         OP_MAKEN: "MAKEN", OP_CALL: "CALL", OP_FREE: "FREE", OP_DPB: "DPB", OP_TIME: "TIME"}
FCALLS = (15, 16, 17, 19, 22, 23, 30, 35, 36, 99)


def pattern(tag):
    lo, step = tag & 0xFF, (tag >> 8) | 1
    return bytes(((lo + i * step) & 0xFF) ^ i for i in range(128))


def fletcher(data, s=(0, 0)):
    a, b = s
    for c in data:
        a = (a + c) & 0xFF
        b = (b + a) & 0xFF
    return a, b


# ---------------------------------------------------------------------------- the script

class Script:
    def __init__(self, drive=0, seed=0, ops=None):
        self.drive, self.seed = drive, seed
        self.ops = ops or []                  # (opcode, bytes of arguments)

    def encode(self):
        body = b"".join(bytes([op]) + args for op, args in self.ops)
        head = MAGIC + bytes([VERSION, self.drive]) + struct.pack("<HHI", len(self.ops), len(body), self.seed)
        return head + b"\0\0" + body

    @staticmethod
    def decode(data):
        if data[:4] != MAGIC or data[4] != VERSION:
            raise ValueError("not a version 1 script")
        count, length, seed = struct.unpack("<HHI", data[6:14])
        s = Script(data[5], seed)
        p, end = 16, 16 + length
        for _ in range(count):
            op = data[p]
            if op not in ARGS:
                raise ValueError("op %d: no opcode $%02X" % (len(s.ops), op))
            n = ARGS[op]
            s.ops.append((op, bytes(data[p + 1:p + 1 + n])))
            p += 1 + n
        if p != end:
            raise ValueError("the operations' length is %d, not %d" % (p - 16, length))
        return s


def describe(op, args):
    n = NAMES[op]
    if op == OP_FCB:
        b = args[1:17]
        dr = "?" if b[0] == 0x3F else "@" if b[0] == 0 else chr(64 + b[0])
        name = "".join(chr(c & 0x7F) + ("'" if c & 0x80 else "") for c in b[1:12])
        return "FCB %d = %s:%s ex %d s2 %d cr %d r %d" % (args[0], dr, name, b[12], b[14], args[17],
                                                          args[18] | args[19] << 8 | args[20] << 16)
    if op == OP_RAND:
        return "RAND %d = %d" % (args[0], args[1] | args[2] << 8 | args[3] << 16)
    if op in (OP_FCALL, OP_READ):
        return "%s %d slot %d" % (n, args[0], args[1])
    if op == OP_WRITE:
        return "WRITE %d slot %d tag $%04X" % (args[0], args[1], args[2] | args[3] << 8)
    if op == OP_WRITEN:
        return "WRITEN slot %d n %d tag $%04X" % (args[0], args[1] | args[2] << 8, args[3] | args[4] << 8)
    if op in (OP_READN, OP_MAKEN):
        return "%s slot %d n %d" % (n, args[0], args[1] | args[2] << 8)
    if op == OP_CALL:
        return "CALL %d A=$%02X X=$%02X" % tuple(args)
    if op == OP_TIME:
        return "TIME day %d %02X:%02X" % (args[0] | args[1] << 8, args[2], args[3])
    return "%s %s" % (n, args.hex())


# ---------------------------------------------------------------------------- results

class Rec:
    """One operation's result: fields (name, value, size, rule); rule "eq", "any" (not compared) or
    "code" (A: any of 0-3 when the model's is under 4)."""

    def __init__(self, op):
        self.op = op
        self.fields = [("op", op, 1, "eq")]

    def add(self, name, value, size=1, rule="eq"):
        self.fields.append((name, value, size, rule))

    def encode(self):
        out = bytearray()
        for _, v, size, _ in self.fields:
            out += v if isinstance(v, (bytes, bytearray)) else v.to_bytes(size, "little")
        return bytes(out)

    def size(self):
        return sum(f[2] for f in self.fields)


def result_size(op, args, a=None):
    """What DRIVER.COM appends for this operation; for FCALL 17 and NEXT, A decides."""
    base = {OP_FCB: 1, OP_NAME2: 1, OP_RAND: 1, OP_FCALL: 8, OP_NEXT: 3, OP_READ: 10, OP_WRITE: 8,
            OP_WRITEN: 10, OP_READN: 12, OP_SALL: 9, OP_MAKEN: 5, OP_CALL: 3, OP_FREE: 6, OP_DPB: 20,
            OP_TIME: 3}[op]
    if (op == OP_NEXT or op == OP_FCALL and args[0] == 17) and (a is None or a < 4):
        base += 32
    return base


def split_results(script, data):
    """RESULT.BIN's header and each operation's bytes, by the script."""
    if data[:4] != RMAGIC or data[4] != VERSION:
        raise ValueError("RESULT.BIN: no OOMR header")
    status, count, length = data[5], data[6] | data[7] << 8, data[8] | data[9] << 8
    body = data[16:16 + length]
    out, p = [], 0
    for op, args in script.ops[:count]:
        if p >= len(body) or body[p] != op:
            raise ValueError("result %d: opcode $%02X, not $%02X" % (len(out), body[p] if p < len(body) else -1, op))
        n = result_size(op, args, body[p + 1] if op in (OP_NEXT, OP_FCALL) else None)
        out.append(bytes(body[p:p + n]))
        p += n
    if p != len(body):
        raise ValueError("RESULT.BIN: %d bytes after the last result" % (len(body) - p))
    return status, count, out


# ---------------------------------------------------------------------------- the reference driver

class Driver:
    """DRIVER.COM's steps, on the model: the same BDOS calls in the same order. Its predictions are Recs."""

    FCB0, DMA, DAT, SCRIPT, RESULT, IOFCB = 0x1000, 0x0F00, 0x0EF0, 0x2000, 0x6000, 0x0E00

    def __init__(self, bdos, mem=None):
        self.b = bdos
        self.mem = mem or M.Memory()
        self.recs = []
        self.log = []                          # each operation's FCB before its call, for messages

    def slot(self, k):
        return self.FCB0 + 36 * k

    def call(self, fn, a=0, x=0):
        return self.b.call(fn, a, x, self.mem)

    def addr_call(self, fn, addr):
        return self.call(fn, addr & 0xFF, addr >> 8)

    def io_fcb(self, name):
        f = bytearray(36)
        f[0] = 1
        f[1:12] = M.fcb_name(name)
        self.mem.write(self.IOFCB, bytes(f))

    # --- before and after the operations ---
    def prologue(self, script_len=None):
        self.call(45, 0xFF)
        self.call(13)
        self.addr_call(26, self.DMA)
        self.call(32, 0)
        self.io_fcb("SCRIPT.BIN")
        a, _ = self.addr_call(15, self.IOFCB)
        if a == 0xFF:
            return None
        k = 0
        while k < SCRIPT_MAX // 128:
            self.addr_call(26, self.SCRIPT + 128 * k)
            a, _ = self.addr_call(20, self.IOFCB)
            if a:
                break
            k += 1
        self.addr_call(16, self.IOFCB)
        self.addr_call(26, self.DMA)
        return self.mem.read(self.SCRIPT, 128 * k)

    def epilogue(self, status, ran):
        body = b"".join(r.encode() for r in self.recs)
        data = RMAGIC + bytes([VERSION, status]) + struct.pack("<HH", ran, len(body)) + bytes(6) + body
        data += bytes(-len(data) % 128)
        self.mem.write(self.RESULT, data)
        self.call(48, 0)
        self.call(13)
        self.call(32, 0)
        self.io_fcb("RESULT.BIN")
        self.addr_call(19, self.IOFCB)
        self.io_fcb("RESULT.BIN")
        self.addr_call(22, self.IOFCB)
        for k in range(len(data) // 128):
            self.addr_call(26, self.RESULT + 128 * k)
            self.addr_call(21, self.IOFCB)
        self.addr_call(16, self.IOFCB)
        return data

    def run(self, script):
        """The whole of DRIVER.COM on the model. @return RESULT.BIN's bytes"""
        raw = self.prologue()
        try:
            s = Script.decode(raw) if raw is not None else None
        except ValueError:
            s = None
        if s is None:
            return self.epilogue(1, 0)
        for op, args in s.ops:
            self.apply(op, args)
        return self.epilogue(0, len(s.ops))

    # --- one operation ---
    def fcb5(self, rec, k, sure=True):
        b = self.mem.read(self.slot(k), 36)
        st = self.b.fcbs.get(self.slot(k))
        pos = "any" if not sure or st is not None and st.uncertain else "eq"
        rec.add("ex", b[12], 1, pos)
        rec.add("cr", b[32], 1, pos)
        rec.add("r", bytes(b[33:36]), 3, "eq")

    def ax(self, rec, a, x, loose=None):
        loose = self.b.loose if loose is None else loose
        rec.add("A", a, 1, "any" if "a" in loose else "code" if "code" in loose and a < 4 else "eq")
        rec.add("X", x, 1, "any" if "x" in loose else "eq")

    def apply(self, op, args):
        """Run one operation. Unspecified (from the model, before anything changed) passes up."""
        rec = Rec(op)
        mem = self.mem
        k = args[1] if op in (OP_FCALL, OP_READ, OP_WRITE) else args[0] if op in (
            OP_WRITEN, OP_READN, OP_SALL, OP_MAKEN) else None
        self.log.append(mem.read(self.slot(k), 36) if k is not None else None)
        if op == OP_FCB:
            mem.write(self.slot(args[0]), args[1:17] + bytes(16) + args[17:21])
        elif op == OP_NAME2:
            mem.write(self.slot(args[0]) + 16, args[1:17])
        elif op == OP_RAND:
            mem.write(self.slot(args[0]) + 33, args[1:4])
        elif op == OP_FCALL:
            fn, k = args
            a, x = self.addr_call(fn, self.slot(k))
            self.ax(rec, a, x)
            self.fcb5(rec, k)
            if fn == 17 and a < 4:
                rec.add("entry", mem.read(self.DMA + 32 * a, 32), 32)
        elif op == OP_NEXT:
            a, x = self.call(18)
            self.ax(rec, a, x)
            if a < 4:
                rec.add("entry", mem.read(self.DMA + 32 * a, 32), 32)
        elif op == OP_READ:
            fn, k = args
            mem.write(self.DMA, bytes([FILL]) * 128)
            a, x = self.addr_call(fn, self.slot(k))
            self.ax(rec, a, x)
            self.fcb5(rec, k)
            s = fletcher(mem.read(self.DMA, 128))
            rule = "any" if a or self.b.undefined else "eq"
            rec.add("sum", bytes(s), 2, rule)
        elif op == OP_WRITE:
            fn, k = args[0], args[1]
            mem.write(self.DMA, pattern(args[2] | args[3] << 8))
            a, x = self.addr_call(fn, self.slot(k))
            self.ax(rec, a, x)
            self.fcb5(rec, k)
        elif op == OP_WRITEN:
            k, n, tag = args[0], args[1] | args[2] << 8, args[3] | args[4] << 8
            a = x = done = 0
            loose = set()
            while done < n:
                mem.write(self.DMA, pattern((tag + done) & 0xFFFF))
                try:
                    a, x = self.addr_call(21, self.slot(k))
                except M.Unspecified:
                    if done:
                        raise RuntimeError("WRITEN: Unspecified after %d writes" % done)
                    raise
                loose = self.b.loose
                if a:
                    break
                done += 1
            self.ax(rec, a, x, loose)
            rec.add("done", done, 2)
            self.fcb5(rec, k)
        elif op == OP_READN:
            k, n = args[0], args[1] | args[2] << 8
            a = x = done = 0
            s, sure = (0, 0), True
            while done < n:
                mem.write(self.DMA, bytes([FILL]) * 128)
                try:
                    a, x = self.addr_call(20, self.slot(k))
                except M.Unspecified:
                    if done:
                        raise RuntimeError("READN: Unspecified after %d reads" % done)
                    raise
                if a:
                    break
                sure = sure and not self.b.undefined
                s = fletcher(mem.read(self.DMA, 128), s)
                done += 1
            self.ax(rec, a, x, set())
            rec.add("done", done, 2)
            rec.add("sum", bytes(s), 2, "eq" if sure else "any")
            self.fcb5(rec, k)
        elif op == OP_SALL:
            k = args[0]
            a, x = self.addr_call(17, self.slot(k))
            n, s1, s2 = 0, (0, 0), (0, 0)
            while a < 4:
                e = mem.read(self.DMA + 32 * a, 32)
                s1 = fletcher(bytes([a]) + e[:16], s1)
                s2 = fletcher(e[16:], s2)
                n += 1
                a, x = self.call(18)
            self.ax(rec, a, x, set())
            rec.add("found", n, 2)
            rec.add("sum", bytes(s1), 2)
            rec.add("sum2", bytes(s2), 2)
        elif op == OP_MAKEN:
            k, n = args[0], args[1] | args[2] << 8
            a = x = done = 0
            while done < n:
                f = bytearray(mem.read(self.slot(k), 36))
                f[6:9] = b"%03d" % done
                f[12:36] = bytes(24)
                mem.write(self.slot(k), bytes(f))
                try:
                    a, x = self.addr_call(22, self.slot(k))
                except M.Unspecified:
                    if done:
                        raise RuntimeError("MAKEN: Unspecified after %d makes" % done)
                    raise
                if a >= 4:
                    break
                self.addr_call(16, self.slot(k))
                done += 1
            self.ax(rec, a, x, set())
            rec.add("made", done, 2)
        elif op == OP_CALL:
            fn, a0, x0 = args
            a, x = self.call(fn, a0, x0)
            self.ax(rec, a, x)
            if fn == 13:
                self.addr_call(26, self.DMA)
        elif op == OP_FREE:
            a, x = self.call(46, args[0])
            self.ax(rec, a, x)
            rec.add("free", mem.read(self.DMA, 3) if a == 0 else bytes(3), 3, "eq" if a == 0 else "any")
        elif op == OP_DPB:
            a, x = self.call(31)
            self.ax(rec, a, x)
            rec.add("dpb", mem.read(a | x << 8, 17), 17)
        elif op == OP_TIME:
            mem.write(self.DAT, args[:4])
            a, x = self.addr_call(104, self.DAT)
            self.ax(rec, a, x)
        self.recs.append(rec)
        return rec


# ---------------------------------------------------------------------------- the generator

NAMES_POOL = ["DATA.BIN", "LOG.TXT", "A", "B.C", "FILE1.TMP", "FILE2.TMP", "BIG.DAT", "X.Y", "SPARSE.DAT",
              "ZZZZZZZZ.ZZZ", "Q1", "Q2.$$$"]
USERS = [0, 0, 0, 1, 3, 15]
PROFILES = ("mixed", "mixed", "sparse", "users", "dirfill", "diskfill")
INVALID_DRIVE = 7                      # H:: never a drive on the OO-1


def bcd(n):
    return n // 10 << 4 | n % 10


class Generator:
    """Seeded scripts, made by running each candidate operation on the model and keeping the ones whose
    outcome CP/M 3 fixes. The same seed and format make the same script."""

    def __init__(self, seed, fmt, drive, nops=None, profile=None, a_fmt="oodos-8m"):
        self.rng = random.Random(seed)
        self.rare = random.Random(seed + 0x10000)  # the rarer choices, so they leave rng's stream as it was
        self.seed, self.fmt, self.drive = seed, M.FORMATS[fmt], drive
        self.profile = profile or self.rng.choice(PROFILES)
        if self.profile == "diskfill" and self.fmt.size > 1 << 20:
            self.profile = "mixed"               # filling 8 MB a record a call is too slow on the emulator
        self.nops = nops or self.rng.randint(220, 420)
        start = M.stamp(2026, 10, 27, 12, 0)
        a = M.Drive(a_fmt, M.mkfs(M.FORMATS[a_fmt], start))
        put_file(a, 0, "SCRIPT.BIN", bytes(128), start)
        self.bdos = M.Bdos({0: a, drive: M.Drive(self.fmt, M.mkfs(self.fmt, start))}, now=start)
        self.drv = Driver(self.bdos)
        self.drv.prologue()
        self.script = Script(drive, seed)
        self.result_bytes = 16
        self.day, self.minute = start[0] | start[1] << 8, 12 * 60   # the clock: days and minutes
        self.since_time = 99
        self.slots = {}                            # slot -> (user, name) last set
        self.cur = 0
        self.tag = self.rng.randrange(65536)
        self.ro_pending = False

    # --- emitting ---
    def room(self, op, args):
        s = len(self.script.encode()) + 1 + len(args)
        # DRIVER.COM stops a page and 64 bytes short of its buffers: room to spare
        return s <= SCRIPT_MAX - 64 and self.result_bytes + result_size(op, args) <= RESULT_MAX - 512

    def emit(self, op, args):
        """Try an operation on the model; keep it if its outcome is fixed. @return its Rec or None"""
        args = bytes(args)
        if not self.room(op, args):
            raise StopIteration
        try:
            rec = self.drv.apply(op, args)
        except M.Unspecified:
            self.drv.log.pop()
            return None
        self.script.ops.append((op, args))
        self.result_bytes += rec.size()
        if op == OP_CALL and args[0] == 14 and rec.fields[1][1] == 0:
            self.cur = args[1]
        if op == OP_CALL and args[0] == 13:
            self.cur = 0
        return rec

    def time(self, force=False):
        if not force and self.since_time < 6:
            return
        r = self.rng.random()
        self.minute += 0 if r < 0.3 else self.rng.randint(1, 30) if r < 0.9 else 0
        if r > 0.95:
            self.day += self.rng.randint(1, 400)
        self.day += self.minute // 1440
        self.minute %= 1440
        self.since_time = 0
        self.emit(OP_TIME, [self.day & 0xFF, self.day >> 8, bcd(self.minute // 60), bcd(self.minute % 60)])

    # --- choices ---
    def dr(self):
        return 0 if self.cur == self.drive and self.rng.random() < 0.2 else self.drive + 1

    def user(self):
        return self.bdos.user

    def name(self, existing=0.7):
        """An 11-byte name: often one on the drive in the current user."""
        dv = self.bdos.drives[self.drive]
        if dv.logged and self.rng.random() < existing:
            names = sorted({dv.entry(i)[1:12] for i in dv.entries() if dv.entry(i)[0] == self.user()})
            if names:
                return bytes(c & 0x7F for c in self.rng.choice(names))
        return M.fcb_name(self.rng.choice(NAMES_POOL))

    def wild(self, name):
        n = bytearray(name)
        for _ in range(self.rng.randint(1, 4)):
            n[self.rng.randrange(11)] = 0x3F
        return bytes(n)

    def set_fcb(self, k, name, dr=None, ex=0, cr=0, r=0, s2=0):
        b = bytearray(16)
        b[0] = self.dr() if dr is None else dr
        b[1:12] = name
        b[12], b[14] = ex, s2
        self.emit(OP_FCB, [k] + list(b) + [cr, r & 0xFF, r >> 8 & 0xFF, r >> 16 & 0xFF])

    def open_slots(self, any_user=False):
        """The slots an FCB operation can use now: open, unchanged, and (unless any_user) in this user."""
        out = []
        for k in range(6):
            a = self.drv.slot(k)
            s = self.bdos.fcbs.get(a)
            if s is None or s.stale or s.closed or self.drv.mem.read(a, 33) != s.image:
                continue
            if (any_user or s.opener == self.bdos.user) and (s.dr or self.bdos.cur == s.drive):
                out.append(k)
        return out

    def usable(self):
        """An open slot, switching back to its user now and then. @return a slot or None"""
        ks = self.open_slots()
        if ks:
            return self.rng.choice(ks)
        others = self.open_slots(any_user=True)
        if others and self.rng.random() < 0.6:
            k = self.rng.choice(others)
            self.emit(OP_CALL, [32, self.bdos.fcbs[self.drv.slot(k)].opener, 0])
            return k if k in self.open_slots() else None
        return None

    def spare_slot(self):
        busy = set(self.open_slots(any_user=True))
        free = [k for k in range(6) if k not in busy]
        return self.rng.choice(free) if free and self.rng.random() < 0.9 else self.rng.randrange(6)

    def files(self):
        dv = self.bdos.drives[self.drive]
        return sum(1 for i in dv.entries() if dv.entry(i)[0] == self.bdos.user) if dv.logged else 0

    def record_edge(self, size=None):
        f = self.fmt
        epe = (f.exm + 1) * 128
        r = self.rng.random()
        if size and r < 0.35:
            return max(0, min(262143, size + self.rng.randint(-2, 2)))
        picks = [0, 1, 127, 128, 129, epe - 1, epe, epe + 1, 2 * epe, 65535, 65536, 262143, 262144,
                 f.rpb - 1, f.rpb, 16383, 16384, self.rng.randrange(4 * epe)]
        if self.profile == "diskfill" or f.size <= 1 << 17:
            picks += [self.rng.randrange(1024)] * 4
        return self.rng.choice(picks)

    # --- the steps ---
    def step(self):
        rng = self.rng
        p = self.profile
        w = {"open": 8, "make": 8, "close": 6, "write": 14, "read": 10, "rand": 12, "size": 4, "setrand": 3,
             "search": 6, "sall": 2, "delete": 4, "rename": 4, "attr": 4, "user": 3, "select": 3,
             "reset": 1, "ro": 2, "free": 3, "misc": 3, "truncate": 3, "flush": 1, "free98": 1}
        if p == "users":
            w["user"] = 12
        if p == "sparse":
            w["rand"] = 30
        if p == "dirfill":
            w["makeN"] = 6
        if p == "diskfill":
            w["fill"] = 6
        if self.files() < 3:
            w["make"] += 20
            w["write"] += 10
        if self.ro_pending and rng.random() < 0.4:
            self.ro_pending = False
            v = 1 << self.drive
            self.emit(OP_CALL, [37, v & 0xFF, v >> 8])
        kinds = list(w)
        kind = rng.choices(kinds, weights=[w[k] for k in kinds])[0]
        getattr(self, "g_" + kind)()
        self.since_time += 1

    def s2(self):
        """S2 as a program might leave it: the Guide has open, make and search take bytes 0-12 only."""
        return self.rare.randrange(1, 256) if self.rare.random() < 0.08 else 0

    def g_open(self):
        k = self.spare_slot()
        name = self.name(0.92)
        ex = self.rng.choice([0, 0, 0, 0, 0, 0, 1, 2, 3])
        cr = 0xFF if self.rare.random() < 0.12 else 0   # $FF: open gives the byte count in CR
        self.set_fcb(k, name, ex=ex, cr=cr, s2=self.s2())
        self.time()
        self.emit(OP_FCALL, [15, k])

    def g_make(self):
        k = self.spare_slot()
        r = self.rng.random()
        name = self.name(0.2) if r < 0.85 else self.wild(self.name(0.2)) if r < 0.92 else self.name(1)
        if self.rare.random() < 0.15:                   # attributes, which make clears
            name = bytearray(name)
            for i in self.rare.sample(range(11), self.rare.randint(1, 4)):
                name[i] |= 0x80
            name = bytes(name)
        ex = self.rare.choice([1, 2, 3, 8]) if self.rare.random() < 0.06 else 0   # File Exists: any extent
        self.set_fcb(k, name, ex=ex, s2=self.s2())
        self.time(force=True)
        self.emit(OP_FCALL, [22, k])

    def g_close(self):
        k = self.usable()
        self.time()
        self.emit(OP_FCALL, [16, self.rng.randrange(6) if k is None else k])

    def g_write(self):
        k = self.usable()
        if k is None:
            return self.g_make() if self.rng.random() < 0.6 else self.g_open()
        self.time()
        self.tag = (self.tag + 7) & 0xFFFF
        if self.rng.random() < 0.5:
            self.emit(OP_WRITE, [21, k, self.tag & 0xFF, self.tag >> 8])
        else:
            s = self.bdos.fcbs[self.drv.slot(k)]
            epe = (self.fmt.exm + 1) * 128
            pos = s.ext * 128 + s.cr
            to = self.rng.choice([128, epe, 2 * epe, self.fmt.rpb])
            n = (to - pos % to) + self.rng.randint(-1, 1) if self.rng.random() < 0.7 else self.rng.randint(1, 300)
            n = max(1, min(n, 2000, M.MAX_EXT * 128 + 127 - pos))
            self.emit(OP_WRITEN, [k, n & 0xFF, n >> 8, self.tag & 0xFF, self.tag >> 8])
            self.tag = (self.tag + n) & 0xFFFF

    def g_read(self):
        k = self.usable()
        if k is None:
            return self.g_open()
        if self.rng.random() < 0.5:
            self.emit(OP_READ, [20, k])
        else:
            n = self.rng.choice([1, 2, 127, 128, 129, 255, 256, 257, self.rng.randint(1, 600)])
            self.emit(OP_READN, [k, n & 0xFF, n >> 8])

    def g_rand(self):
        k = self.usable()
        if k is None:
            return self.g_open()
        dv = self.bdos.drives[self.drive]
        s = self.bdos.fcbs[self.drv.slot(k)]
        size = dv.file_size(s.user, s.name)
        r = self.record_edge(size)
        self.emit(OP_RAND, [k, r & 0xFF, r >> 8 & 0xFF, r >> 16 & 0xFF])
        self.time()
        c = self.rng.random()
        if c < 0.45:
            self.emit(OP_READ, [33, k])
        else:
            self.tag = (self.tag + 3) & 0xFFFF
            self.emit(OP_WRITE, [34 if c < 0.85 else 40, k, self.tag & 0xFF, self.tag >> 8])

    def g_size(self):
        n = self.name(0.8)
        self.set_fcb(6, n if self.rng.random() < 0.9 else self.wild(n), r=self.rng.randrange(1 << 24))
        self.emit(OP_FCALL, [35, 6])

    def g_setrand(self):
        k = self.usable()
        if k is not None:
            self.emit(OP_FCALL, [36, k])

    def search_fcb(self):
        r = self.rng.random()
        if r < 0.15:
            self.set_fcb(6, bytes([0x3F]) * 11, dr=0x3F, ex=0x3F)
        else:
            n = self.name(0.7)
            if r < 0.6:
                n = self.wild(n)
            if r > 0.9:
                n = b"???????????"
            ex = 0x3F if self.rng.random() < 0.5 else 0
            self.set_fcb(6, n, ex=ex, s2=self.s2())

    def g_search(self):
        self.search_fcb()
        rec = self.emit(OP_FCALL, [17, 6])
        if rec is None:
            return
        for _ in range(self.rng.randint(0, 4)):
            if self.emit(OP_NEXT, []) is None or self.drv.recs[-1].fields[1][1] >= 4:
                break

    def g_sall(self):
        self.search_fcb()
        self.emit(OP_SALL, [6])

    def g_delete(self):
        n = self.name(0.8)
        self.set_fcb(6, n if self.rng.random() < 0.75 else self.wild(n))
        self.emit(OP_FCALL, [19, 6])

    def g_rename(self):
        old = self.name(0.85)
        new = self.name(0.15)
        r = self.rng.random()
        if r < 0.05:
            new = self.wild(new)
        self.set_fcb(6, old)
        self.emit(OP_NAME2, [6, self.drive + 1] + list(new) + [0, 0, 0, 0])
        self.emit(OP_FCALL, [23, 6])

    def g_attr(self):
        n = bytearray(self.name(0.9))
        for i in (0, 1, 2, 3, 8, 9, 10):
            if self.rng.random() < (0.25 if i < 8 else 0.35):
                n[i] |= 0x80
        cr = 0
        if self.rare.random() < 0.3:                    # f6': CR is the byte count, into S1
            n[5] |= 0x80
            cr = self.rare.randrange(129)
        self.set_fcb(6, bytes(n), cr=cr)
        self.emit(OP_FCALL, [30, 6])

    def g_user(self):
        if self.rng.random() < 0.2:
            self.emit(OP_CALL, [32, 0xFF, 0])
        else:
            self.emit(OP_CALL, [32, self.rng.choice(USERS), 0])

    def g_select(self):
        r = self.rng.random()
        d = self.drive if r < 0.8 else INVALID_DRIVE if r < 0.9 else 0
        self.emit(OP_CALL, [14, d, 0])
        if self.rng.random() < 0.3:
            self.emit(OP_CALL, [25, 0, 0])

    def g_reset(self):
        if self.rng.random() < 0.3:
            self.emit(OP_CALL, [13, 0, 0])
        else:
            v = 1 << self.drive | (1 if self.rng.random() < 0.3 else 0)
            self.emit(OP_CALL, [37, v & 0xFF, v >> 8])

    def g_ro(self):
        r = self.rng.random()
        if r < 0.4 and self.cur == self.drive:
            self.emit(OP_CALL, [28, 0, 0])
            self.ro_pending = True
        elif r < 0.7:
            self.emit(OP_CALL, [29, 0, 0])
        else:
            self.emit(OP_CALL, [24, 0, 0])

    def g_free(self):
        self.emit(OP_FREE, [self.drive if self.rng.random() < 0.9 else INVALID_DRIVE])

    def g_misc(self):
        r = self.rng.random()
        if r < 0.4:
            self.emit(OP_DPB, [])
        elif r < 0.6:
            self.emit(OP_CALL, [12, 0, 0])
        elif r < 0.8:
            self.emit(OP_CALL, [27, 0, 0])
        else:
            self.emit(OP_CALL, [25, 0, 0])

    def g_truncate(self):
        n = self.name(0.95)
        dv = self.bdos.drives[self.drive]
        size = dv.file_size(self.user(), n) if dv.logged else None
        r = self.rng.randrange(size + 2) if size else self.record_edge()
        self.set_fcb(6, n, r=r)
        self.emit(OP_FCALL, [99, 6])

    def g_flush(self):
        self.emit(OP_CALL, [48, 0, 0])

    def g_free98(self):
        self.emit(OP_CALL, [98, 0, 0])

    def g_makeN(self):
        dv = self.bdos.drives[self.drive]
        free = sum(1 for i in dv.entries() if dv.entry(i)[0] == M.E5)
        n = max(1, free + self.rng.randint(-3, 2))
        n = min(n, 999)
        base = bytearray(M.fcb_name(self.rng.choice(["MKALL", "MANYF", "DFULL"])))  # no space before the digits
        base[5:8] = b"000"
        self.set_fcb(7, bytes(base))
        self.time(force=True)
        self.emit(OP_MAKEN, [7, n & 0xFF, n >> 8])

    def g_fill(self):
        k = self.usable()
        if k is None:
            return self.g_make()
        dv = self.bdos.drives[self.drive]
        if not dv.logged:
            return self.emit(OP_FREE, [self.drive])
        n = max(1, len(dv.free_blocks()) * self.fmt.rpb + self.rng.randint(-3, 3))
        self.time(force=True)
        self.emit(OP_WRITEN, [k, n & 0xFF, n >> 8 & 0xFF, self.tag & 0xFF, self.tag >> 8])
        self.tag = (self.tag + n) & 0xFFFF

    def generate(self):
        self.emit(OP_CALL, [14, self.drive, 0])
        self.cur = self.drive
        self.time(force=True)
        try:
            while len(self.script.ops) < self.nops:
                self.step()
        except StopIteration:
            pass
        # Close what's open, so the directory is the files' last word.
        self.script_closing()
        return self.script

    def script_closing(self):
        for k in range(6):
            s = self.bdos.fcbs.get(self.drv.slot(k))
            if s is None or s.stale or s.closed or not s.modified:
                continue
            try:
                if s.opener != self.bdos.user:
                    self.emit(OP_CALL, [32, s.opener, 0])
                self.time(force=True)
                self.emit(OP_FCALL, [16, k])
            except StopIteration:
                break


def generate(seed, fmt, drive, **kw):
    return Generator(seed, fmt, drive, **kw).generate()


# ---------------------------------------------------------------------------- files, by the model

def put_file(dv, user, name, data, now):
    """Write a file onto a drive through the model's BDOS (make, write, close)."""
    b = M.Bdos({0: dv}, now=now)
    mem = M.Memory()
    f = bytearray(36)
    f[0] = 1
    f[1:12] = M.fcb_name(name)
    b.call(32, user, 0, mem)
    mem.write(0x1000, bytes(f))
    b.call(19, 0x00, 0x10, mem)
    mem.write(0x1000, bytes(f))
    if b.call(22, 0x00, 0x10, mem)[0] >= 4:
        raise ValueError("can't make %s" % name)
    data = data + bytes(-len(data) % 128)
    for i in range(0, len(data), 128):
        mem.write(0x2000, data[i:i + 128])
        b.call(26, 0x00, 0x20, mem)
        if b.call(21, 0x00, 0x10, mem)[0]:
            raise ValueError("can't write %s" % name)
    b.call(16, 0x00, 0x10, mem)


# ---------------------------------------------------------------------------- the comparison

def compare_results(script, predicted, actual_data, out):
    """Each operation's actual result against the prediction. @return the number of differences"""
    try:
        status, count, actual = split_results(script, actual_data)
    except ValueError as e:
        out.append("RESULT.BIN: %s" % e)
        return 1
    if status or count != len(script.ops):
        out.append("RESULT.BIN: status %d, %d of %d operations run" % (status, count, len(script.ops)))
        return 1
    bad = 0
    for i, (rec, got) in enumerate(zip(predicted, actual)):
        diffs = []
        p = 0
        for name, value, size, rule in rec.fields:
            g = got[p:p + size]
            p += size
            want = value if isinstance(value, (bytes, bytearray)) else value.to_bytes(size, "little")
            if rule == "any":
                continue
            if rule == "code" and g[0] < 4:
                continue
            if bytes(g) != bytes(want):
                diffs.append("%s: model %s, BDOS %s" % (name, bytes(want).hex(), bytes(g).hex()))
        if p != len(got):
            diffs.append("the result is %d bytes, the model's %d" % (len(got), p))
        if diffs:
            bad += 1
            if bad <= 10:
                op, args = script.ops[i]
                out.append("op %d (%s): %s" % (i, describe(op, args), "; ".join(diffs)))
    return bad


def compare_images(model_dv, actual, mode, out, label="drive"):
    """The drive's directory and files: exactly, or up to the blocks' numbering. @return differences"""
    f = model_dv.fmt
    act = M.Drive(f, actual)
    bad = 0
    bmap = {}

    def note(msg):
        nonlocal bad
        bad += 1
        if bad <= 10:
            out.append("%s: %s" % (label, msg))

    for i in model_dv.entries():
        m, a = model_dv.entry(i), act.entry(i)
        if m[0] <= 15:
            if mode == "exact":
                if m != a:
                    note("entry %d: model %s, BDOS %s" % (i, m.hex(), a.hex()))
                continue
            if m[:16] != a[:16]:
                note("entry %d: model %s, BDOS %s" % (i, m[:16].hex(), a[:16].hex()))
                continue
            for mb, ab in zip(model_dv.blocks(m), act.blocks(a)):
                if (mb == 0) != (ab == 0) or bmap.setdefault(mb, ab) != ab:
                    note("entry %d: block %d is the BDOS's %d (blocks %s, %s)" % (i, mb, ab, model_dv.blocks(m),
                                                                                    act.blocks(a)))
                    break
        elif m[0] == M.E5:
            if (m if mode == "exact" else m[:1]) != (a if mode == "exact" else a[:1]):
                note("entry %d: model %s, BDOS %s" % (i, m.hex(), a.hex()))
        elif m != a:
            note("entry %d: model %s, BDOS %s" % (i, m.hex(), a.hex()))
    if mode == "exact":
        bmap = {b: b for i in model_dv.entries() if model_dv.entry(i)[0] <= 15
                for b in model_dv.blocks(model_dv.entry(i)) if b}
    bmap.pop(0, None)
    if len(set(bmap.values())) != len(bmap):
        note("two of the model's blocks are one of the BDOS's")
    for mb, ab in sorted(bmap.items()):
        for n in range(f.rpb):
            mdata, und = model_dv.read_rec(mb, n)
            if und:
                continue
            adata = act.read_rec(ab, n)[0]
            if mdata != adata:
                note("block %d record %d (the BDOS's block %d): model %s..., BDOS %s..." % (
                    mb, n, ab, mdata[:8].hex(), adata[:8].hex()))
    return bad
