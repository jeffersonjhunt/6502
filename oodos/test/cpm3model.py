"""A model of CP/M 3's file system, for OODOS's randomized BDOS test (hw/oodos.md, "The BDOS, phase 3").

Ours, written from the CP/M 3 Programmer's Guide's descriptions of BDOS functions 13-37, 40, 46, 48, 98
and 99, at the level a program sees them: what each returns (A, and X as CP/M 3's H) and what it leaves in
the FCB, the DMA buffer and on the disk. Its state is the drives' images, laid out as CP/M 3 lays them out:
directory entries, SFCBs, the label and the blocks, so an image can be compared byte for byte with one the
BDOS wrote, and checked by cpmtools.

    bdos = Bdos({0: Drive(fmt, image), ...}, now=stamp(2026, 10, 27, 12, 0))
    a, x = bdos.call(fn, a, x, mem)          # mem: read(addr, n) -> bytes, write(addr, data)

What is modelled, and the choices where the manual leaves a behaviour open (each also in hw/oodos.md,
Testing, "phase 3a: the model", Proposed):

- Drives: CP/M 3's DPB from oodos/diskdefs (Format); 8-bit block numbers under 256 blocks, else 16-bit;
  EXM from the block size; the directory in the first blocks, every fourth entry an SFCB when the drive
  has them, the label in entry 0. A drive logs in at its first use after a reset (14, a file function, 46):
  the allocation vector is built from the directory and the label's mode read. 13 logs nothing in.
- The allocation vector counts the directory's blocks and the blocks written into FCBs but not yet
  closed (CP/M 3's temporary allocation), which 98, a reset and a login give back.
- Allocation policy, a parameter: "lowest" (the lowest free block) or "nearest" (the free block nearest
  after, then before, the block before it in the FCB, as CP/M 2.2's BDOS chose). A new directory entry is
  the first free one ($E5) in directory order.
- Extents: an FCB's position is its logical extent (S2 x 32 + EX) and CR. A directory entry covers EXM + 1
  logical extents: EX holds the highest written, RC the records in it. Moving to another logical extent
  (a sequential read or write past CR 128, a random read or write elsewhere) writes the current entry back
  if the FCB was written (merging the block maps and keeping the higher EX and RC, as close does), then
  takes the new extent's entry from the directory; a write makes one there if there is none.
- Sequential read and write: past the last record, read returns 1; a hole (no block) also returns 1.
  A write past CR 128 that needs a new entry and finds none returns 1; no block, 2. Random read: R2 over 3
  is 6; no entry for the extent, 4; past RC or a hole, 1. Random write: 6, then no entry space 5, no block
  2. After a random read or write CR is the record's, not the next (the manual's rule).
- Open (15) clears S2 and opens the extent EX names; a file not in the current user but in user 0 with
  the SYS attribute (t2') opens from user 0, read only. Writes to it, or to a file whose t1' is set,
  return A = $FF, X = 3 (File R/O).
- Errors return (the driver sets 45 to $FF): A = $FF and X = CP/M 3's H: 2 R/O disk, 3 R/O file, 4 select
  error, 8 file exists, 9 '?' in the name. "Not found", "no directory space" and the like are A = $FF,
  X = 0. Checks go in this order: the drive, '?' (9), a read-only drive (2), not found, a read-only file
  (3), exists (8); but rename (23) checks the new name first: the drive, '?' in either name, a read-only
  drive, the new name exists (8), the old one not found, a read-only file (3), as OODOS's BDOS does (the
  manual lists the errors in no order).
- Directory codes: 15, 16 (a written FCB), 17, 18 and 22 return the entry's index mod 4; 19, 23 and 30
  return one of 0-3 (which isn't fixed: Result.loose). A close of an FCB not written returns 0 and
  writes nothing. Success puts 0 in X.
- Delete (19) takes '?' and all extents; if any match is R/O nothing is deleted. Rename (23) keeps the
  entries' attributes; '?' in either name is 9, a new name that exists is 8. Set attributes (30) copies
  f1'-f4' and t1'-t3' from the FCB to every extent, no '?'. File size (35) and truncate (99): the
  current user only, '?' is 9. File size puts 0 in R0-R2 when there's no such file (as CP/M 2.2 did).
- Truncate (99) keeps records 0 to R: entries past R's extent are deleted, the one holding it gets EX and
  RC for R and loses the blocks after R's. R at or past the end is A = $FF, X = 0. It stamps the
  update, as a close after a write does (the manual doesn't say; OODOS's BDOS does).
- Date stamps, by the label's mode (byte 12: $10 create, $20 update, $40 access), only ever in the
  SFCB slot of a file's extent-zero entry (the manual: a slot is valid only for an extent-zero entry).
  A new entry's slot is cleared (its password mode and reserved byte 0 too); make (of extent 0) sets
  its create (or access) and update stamps to now, an entry made by a write gets none. Writing a written
  FCB's entry back (close, or moving to another extent) and truncate set the update stamp in the
  file's extent-zero entry's slot, whichever extent they changed. Open of extent 0 sets the access
  stamp with $40 (over the creation stamp, the field they share), not on a read-only drive. Delete,
  rename and 30 leave the SFCB as it is, and so does make on a drive whose mode keeps no stamps. Now is
  what 104 last set (the driver sets the time before what stamps).
- 13 resets every drive (R/O cleared, logged out), drive A current, the DMA $0280; 37 the drives in its
  vector. 28 sets the current drive R/O. 46 puts the free records, 3 bytes, in the DMA buffer. 31 returns
  the address of a copy of the current drive's DPB (17 bytes). 27 returns $FFFF. 12 returns $0031.
- What a record holds: a block allocated by a write (21, 34) has every record undefined until written
  (the BDOS's deblocking may write whatever its buffer held); 40 fills the new block with zeros. Reading
  an undefined record succeeds and its data can't be predicted (Result.undefined).

Where CP/M 3 leaves behaviour open in a way a test can't settle, the model raises Unspecified before it
changes anything, and the generator chooses something else: an FCB used after its file was deleted,
renamed, changed by 30 or 99, or its drive reset (13, 37), or after 98 while it had blocks not yet
closed; two FCBs on one file with either writing; sequential I/O or 36 after an error left the position
uncertain; 18 not straight after 17 or 18; an FCB changed by the program (but for R0-R2); the current
user or drive changed under an open FCB; truncating into a hole; make with EX, S2 or attributes set.
"""

import os

RECORD = 128
E5 = 0xE5
SFCB = 0x21
LABEL = 0x20
TPA_DMA = 0x0280
MAX_EXT = 2047                          # S2 63, EX 31: 32 MB files
MODE_CREATE, MODE_UPDATE, MODE_ACCESS = 0x10, 0x20, 0x40


class Unspecified(Exception):
    """An operation whose outcome CP/M 3 leaves open; nothing has been changed."""


class Format:
    """A diskdef and the CP/M 3 DPB worked out from it."""

    def __init__(self, name, seclen, tracks, sectrk, blocksize, maxdir, boottrk=0):
        self.name, self.seclen, self.tracks, self.sectrk = name, seclen, tracks, sectrk
        self.bls, self.maxdir, self.boottrk = blocksize, maxdir, boottrk
        self.size = tracks * sectrk * seclen
        self.spt = sectrk * seclen // RECORD
        self.bsh = (blocksize // RECORD).bit_length() - 1
        self.blm = (1 << self.bsh) - 1
        self.dsm = (tracks - boottrk) * sectrk * seclen // blocksize - 1
        self.drm = maxdir - 1
        self.big = self.dsm >= 256                       # 16-bit block numbers, 8 to an entry
        self.exm = blocksize // (2048 if self.big else 1024) - 1
        self.slots = 8 if self.big else 16
        self.rpb = blocksize // RECORD                    # records a block
        self.dir_blocks = (maxdir * 32 + blocksize - 1) // blocksize
        self.dir_off = boottrk * sectrk * seclen
        self.psh = (seclen // RECORD).bit_length() - 1

    def dpb(self):
        """The 17 bytes: SPT, BSH, BLM, EXM, DSM, DRM, AL0, AL1, CKS ($8000: fixed), OFF, PSH, PHM."""
        al = (0xFFFF << (16 - self.dir_blocks)) & 0xFFFF
        return bytes([self.spt & 0xFF, self.spt >> 8, self.bsh, self.blm, self.exm,
                      self.dsm & 0xFF, self.dsm >> 8, self.drm & 0xFF, self.drm >> 8, al >> 8, al & 0xFF,
                      0x00, 0x80, self.boottrk & 0xFF, self.boottrk >> 8, self.psh, (1 << self.psh) - 1])

    def block_off(self, b):
        return self.dir_off + b * self.bls


def load_diskdefs(path=None):
    """oodos/diskdefs's formats, by name."""
    path = path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "diskdefs")
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            w = line.split("#")[0].split()
            if not w:
                continue
            if w[0] == "diskdef":
                cur = {"name": w[1]}
            elif w[0] == "end" and cur:
                out[cur["name"]] = Format(cur["name"], cur["seclen"], cur["tracks"], cur["sectrk"],
                                          cur["blocksize"], cur["maxdir"], cur.get("boottrk", 0))
                cur = None
            elif cur is not None and len(w) > 1 and w[1].isdigit():
                cur[w[0]] = int(w[1])
    return out


FORMATS = load_diskdefs()


def stamp(year, month, day, hour, minute):
    """CP/M 3's 4 stamp bytes: days from 1 January 1978 (day 1), hour and minute in BCD."""
    import datetime
    days = (datetime.date(year, month, day) - datetime.date(1977, 12, 31)).days
    return bytes([days & 0xFF, days >> 8, hour // 10 << 4 | hour % 10, minute // 10 << 4 | minute % 10])


def mkfs(fmt, now, label=b"UNLABELED  ", mode=0x31, fill=0):
    """A drive as `mkfs.cpm -t` makes it, the label's mode then set (OODOS's 0x31): the label in entry 0,
    an SFCB ($21, then $E5s) in every fourth, $E5 elsewhere; the label's create and update stamps `now`.
    The data area is `fill` (cpmtools leaves a file's unused blocks as they were: zeros after truncate)."""
    img = bytearray([fill]) * fmt.size
    d = fmt.dir_off
    img[d:d + fmt.maxdir * 32] = bytes([E5]) * (fmt.maxdir * 32)
    for i in range(3, fmt.maxdir, 4):
        img[d + i * 32] = SFCB
    e = bytearray(32)
    e[0] = LABEL
    e[1:12] = label
    e[12] = mode
    e[24:28] = now
    e[28:32] = now
    img[d:d + 32] = e
    return img


def has_wild(name):
    return any((c & 0x7F) == 0x3F for c in name)


def same_name(a, b):
    return all((x & 0x7F) == (y & 0x7F) for x, y in zip(a, b))


class Drive:
    """One drive: its format and image, and the BDOS's state of it."""

    def __init__(self, fmt, image):
        if isinstance(fmt, str):
            fmt = FORMATS[fmt]
        self.fmt = fmt
        self.img = bytearray(image)
        if len(self.img) != fmt.size:
            raise ValueError("%s: the image is %d bytes, not %d" % (fmt.name, len(self.img), fmt.size))
        self.undef = bytearray(fmt.size // RECORD)       # 1: a record whose bytes can't be predicted
        self.logged = False
        self.ro = False
        self.alv = set()
        self.mode = 0

    # --- the directory ---
    def entry(self, i):
        o = self.fmt.dir_off + i * 32
        return bytes(self.img[o:o + 32])

    def set_entry(self, i, e):
        o = self.fmt.dir_off + i * 32
        self.img[o:o + 32] = e

    def entries(self):
        return range(self.fmt.maxdir)

    def is_file(self, e):
        return e[0] <= 15

    def blocks(self, e):
        if self.fmt.big:
            return [e[16 + 2 * k] | e[17 + 2 * k] << 8 for k in range(8)]
        return list(e[16:32])

    def put_blocks(self, e, bl):
        e = bytearray(e)
        if self.fmt.big:
            for k, b in enumerate(bl):
                e[16 + 2 * k], e[17 + 2 * k] = b & 0xFF, b >> 8
        else:
            e[16:32] = bytes(bl)
        return e

    def slot(self, i):
        """Entry i's 10-byte SFCB slot's offset in the image, or None without an SFCB."""
        s = i | 3
        if i & 3 == 3 or s > self.fmt.drm or self.entry(s)[0] != SFCB:
            return None
        return self.fmt.dir_off + s * 32 + 1 + (i & 3) * 10

    def file_entries(self, user, name):
        return [i for i in self.entries() if self.entry(i)[0] == user and same_name(self.entry(i)[1:12], name)]

    def login(self):
        self.logged = True
        self.alv = set(range(self.fmt.dir_blocks))
        self.mode = 0
        for i in self.entries():
            e = self.entry(i)
            if self.is_file(e):
                self.alv.update(b for b in self.blocks(e) if b)
            elif e[0] == LABEL and not self.mode:
                self.mode = e[12]

    def free_blocks(self):
        return [b for b in range(self.fmt.dir_blocks, self.fmt.dsm + 1) if b not in self.alv]

    # --- records ---
    def rec_off(self, block, n):
        return self.fmt.block_off(block) + n * RECORD

    def read_rec(self, block, n):
        o = self.rec_off(block, n)
        return bytes(self.img[o:o + RECORD]), bool(self.undef[o // RECORD])

    def write_rec(self, block, n, data):
        o = self.rec_off(block, n)
        self.img[o:o + RECORD] = data
        self.undef[o // RECORD] = 0

    def new_block(self, block, zero):
        o = self.rec_off(block, 0)
        n = self.fmt.rpb
        if zero:
            self.img[o:o + self.fmt.bls] = bytes(self.fmt.bls)
        self.undef[o // RECORD:o // RECORD + n] = bytes([0 if zero else 1]) * n

    # --- reading a file, for the tests and the harness ---
    def file_size(self, user, name):
        n = None
        for i in self.file_entries(user, name):
            e = self.entry(i)
            r = ((e[14] & 0x3F) << 5 | e[12] & 0x1F) * RECORD + e[15]
            n = r if n is None or r > n else n
        return n

    def read_file(self, user, name, holes=b"\0" * RECORD):
        """The file's records, to its size; a record in no block is `holes`."""
        if isinstance(name, str):
            name = fcb_name(name)
        size = self.file_size(user, name)
        if size is None:
            return None
        f = self.fmt
        recs = {}
        for i in self.file_entries(user, name):
            e = self.entry(i)
            base = ((e[14] & 0x3F) << 5 | e[12] & 0x1F & ~f.exm) * RECORD
            for k, b in enumerate(self.blocks(e)):
                if b:
                    for n in range(f.rpb):
                        recs[base + k * f.rpb + n] = self.read_rec(b, n)[0]
        return b"".join(recs.get(r, holes) for r in range(size))


def fcb_name(text):
    """'NAME.EXT' as the FCB's 11 bytes."""
    n, _, t = text.upper().partition(".")
    return (n.ljust(8) + t.ljust(3)).encode("ascii")


class Fcb:
    """The BDOS's knowledge of an open FCB, at its address (CP/M keeps it in the FCB; the model keeps it
    beside, and checks the program left the FCB alone)."""

    def __init__(self, drive, user, name, ro, dr):
        self.drive, self.user, self.name, self.ro, self.dr = drive, user, name, ro, dr
        self.opener = None
        self.ext, self.cr = 0, 0
        self.entry = None                    # the directory entry of the current extent
        self.map, self.rc, self.s1 = [], 0, 0
        self.modified = False
        self.stale = False                   # its file or drive changed under it
        self.closed = False
        self.uncertain = False               # an error left the position open
        self.image = None                    # bytes 0-32 as last written


class Bdos:
    def __init__(self, drives, now=None, alloc="lowest", dpb_addr=0xF900):
        self.drives = drives
        self.now = now or stamp(2026, 10, 27, 12, 0)
        self.alloc = alloc
        self.dpb_addr = dpb_addr
        self.cur, self.user, self.dma, self.errmode = 0, 0, TPA_DMA, 0
        self.search = None
        self.fcbs = {}
        self.out = bytearray()               # console output
        self.loose = set()                   # this call: "code" A is any of 0-3; "a", "x": not fixed
        self.undefined = False               # this call read an undefined record

    # ------------------------------------------------------------------ the entry
    def call(self, fn, a, x, mem):
        self.loose = set()
        self.undefined = False
        f = getattr(self, "f%d" % fn, None)
        if f is None:
            raise Unspecified("function %d is not modelled" % fn)
        keep = self.search
        r = f(a, x, mem)
        if fn not in (17, 18) and self.search is keep and fn not in (2, 9, 12, 25, 26, 45, 104, 105):
            self.search = None
        return r

    # ------------------------------------------------------------------ helpers
    def drive_of(self, dr):
        d = self.cur if dr == 0 else dr - 1
        return d if d in self.drives else None

    def use(self, d):
        dv = self.drives[d]
        if not dv.logged:
            dv.login()
        return dv

    def code(self, i):
        return i & 3

    def stamp_slot(self, dv, i, made=False, first=False):
        """A made entry's slot: 0, and with `first` (make's extent 0) the create (or access) and update
        stamps as the mode asks. Without `made`, the update stamp in the slot of user:name's extent-zero
        entry, entry i's file (CP/M 3 keeps a file's stamps there only)."""
        if not made:
            e = dv.entry(i)
            i = self.find_ext(dv, e[0], bytes(c & 0x7F for c in e[1:12]), 0)
            if i is None:
                return
        o = dv.slot(i)
        if o is None:
            return
        if made and dv.mode & (MODE_CREATE | MODE_ACCESS | MODE_UPDATE):
            cre = self.now if first and dv.mode & (MODE_CREATE | MODE_ACCESS) else bytes(4)
            upd = self.now if first and dv.mode & MODE_UPDATE else bytes(4)
            dv.img[o:o + 10] = cre + upd + b"\0\0"
        elif not made and dv.mode & MODE_UPDATE:
            dv.img[o + 4:o + 8] = self.now

    def find_ext(self, dv, user, name, ext):
        """The entry holding logical extent `ext` of user:name."""
        f = dv.fmt
        for i in dv.entries():
            e = dv.entry(i)
            if e[0] == user and same_name(e[1:12], name) and (e[14] & 0x3F) == ext >> 5 \
                    and (e[12] & 0x1F & ~f.exm) == (ext & 0x1F & ~f.exm):
                return i
        return None

    def free_entry(self, dv):
        for i in dv.entries():
            if dv.entry(i)[0] == E5:
                return i
        return None

    def allocate(self, dv, hint):
        free = dv.free_blocks()
        if not free:
            return None
        if self.alloc == "nearest" and hint:
            after = [b for b in free if b > hint]
            before = [b for b in free if b < hint]
            cands = []
            if after:
                cands.append(after[0])
            if before:
                cands.append(before[-1])
            return min(cands, key=lambda b: (abs(b - hint), b < hint))
        return free[0]

    def live(self):
        return [s for s in self.fcbs.values() if not s.stale and not s.closed]

    def kill(self, pred):
        for s in self.fcbs.values():
            if not s.stale and pred(s):
                s.stale = True

    def fcb_state(self, addr, mem, op):
        """The open FCB at addr, checked: Unspecified for what CP/M 3 leaves open."""
        s = self.fcbs.get(addr)
        if s is None or s.stale:
            raise Unspecified("%s: FCB $%04X isn't open" % (op, addr))
        b = mem.read(addr, 36)
        if bytes(b[:33]) != s.image:
            raise Unspecified("%s: FCB $%04X changed since the BDOS wrote it" % (op, addr))
        if s.closed and op != "close":
            raise Unspecified("%s: FCB $%04X is closed" % (op, addr))
        if self.user != s.opener:
            raise Unspecified("%s: the user changed since FCB $%04X was opened" % (op, addr))
        if s.dr == 0 and self.cur != s.drive:
            raise Unspecified("%s: the default drive changed under FCB $%04X" % (op, addr))
        return s, b

    def sharers(self, s):
        return [o for o in self.live() if o is not s and o.drive == s.drive and o.user == s.user
                and same_name(o.name, s.name)]

    def put_fcb(self, addr, s, b, mem):
        """Write the FCB as CP/M would: the entry's name, EX, S1, S2 (the module), RC, the map, CR."""
        dv = self.drives[s.drive]
        b = bytearray(b)
        b[1:12] = s.name
        b[12] = s.ext & 0x1F
        b[13] = s.s1
        # S2: the module, and as OODOS's BDOS keeps them there, $80 not written and $40 no entry yet;
        # so a program's own FCB of the same name and place isn't taken for the open one
        b[14] = s.ext >> 5 | (0 if s.modified else 0x80) | (0x40 if s.entry is None else 0)
        b[15] = s.rc
        e = dv.put_blocks(bytes(32), s.map if s.map else [0] * dv.fmt.slots)
        b[16:32] = e[16:32]
        b[32] = s.cr
        mem.write(addr, bytes(b[:33]))
        s.image = bytes(b[:33])

    def put_r(self, addr, r, mem):
        mem.write(addr + 33, bytes([r & 0xFF, r >> 8 & 0xFF, r >> 16 & 0xFF]))

    def load_ext(self, s, ext):
        """Take logical extent `ext`'s entry into the FCB: RC by CP/M's rule (full below the entry's EX,
        its RC at it, 0 above). @return whether there is one."""
        dv = self.drives[s.drive]
        s.ext = ext
        i = self.find_ext(dv, s.user, s.name, ext)
        s.entry = i
        if i is None:
            s.map, s.rc, s.s1 = [0] * dv.fmt.slots, 0, 0
            return False
        e = dv.entry(i)
        s.map, s.s1 = dv.blocks(e), e[13]
        lo = ext & 0x1F
        s.rc = 128 if lo < (e[12] & 0x1F) else e[15] if lo == (e[12] & 0x1F) else 0
        return True

    def writeback(self, s):
        """Close's work: merge the FCB's map, EX and RC into its entry, stamp the update.
        @return the entry, or None if it's gone."""
        dv = self.drives[s.drive]
        if not s.modified:
            return s.entry
        i = s.entry if s.entry is not None and self.find_ext(dv, s.user, s.name, s.ext) == s.entry else \
            self.find_ext(dv, s.user, s.name, s.ext)
        if i is None:
            return None
        e = bytearray(dv.entry(i))
        old = dv.blocks(e)
        merged = []
        for a, b in zip(old, s.map):
            if a and b and a != b:
                raise Unspecified("close: the FCB's map and the directory's disagree")
            merged.append(a or b)
        e = dv.put_blocks(e, merged)
        lo, dex = s.ext & 0x1F, e[12] & 0x1F
        if lo > dex:
            e[12], e[15] = (e[12] & 0xE0) | lo, s.rc
        elif lo == dex:
            e[15] = max(e[15], s.rc)
        dv.set_entry(i, bytes(e))
        s.map = merged
        s.entry = i
        s.modified = False
        self.stamp_slot(dv, i)
        self.kill(lambda o: o in self.sharers(s))       # their copies of the entry are old now
        return i

    def new_entry(self, dv, s, ext, first=False):
        i = self.free_entry(dv)
        if i is None:
            return None
        e = bytearray(32)
        e[0] = s.user
        e[1:12] = s.name
        e[12] = ext & 0x1F
        e[14] = ext >> 5
        dv.set_entry(i, bytes(e))
        self.stamp_slot(dv, i, made=True, first=first)
        self.kill(lambda o: o in self.sharers(s))
        s.entry, s.ext = i, ext
        s.map, s.rc, s.s1 = [0] * dv.fmt.slots, 0, 0
        return i

    def move(self, s, ext, write):
        """To logical extent `ext`: the current entry written back if written, then the new one taken,
        or (writing) made. @return 0, or the error: 4 no extent (read), 5 no directory space (write)."""
        if ext == s.ext and s.entry is not None:
            return 0
        dv = self.drives[s.drive]
        if s.modified and self.writeback(s) is None:
            raise Unspecified("the FCB's entry is gone")
        if self.load_ext(s, ext):
            return 0
        if not write:
            return 4
        if self.new_entry(dv, s, ext) is None:
            return 5
        return 0

    def check_name(self, b):
        return has_wild(b[1:12])

    # ------------------------------------------------------------------ functions
    def f0(self, a, x, mem):
        raise Unspecified("warm boot")

    def f2(self, a, x, mem):
        self.out.append(a)
        self.loose = {"a", "x"}
        return 0, 0

    def f9(self, a, x, mem):
        p = a | x << 8
        while True:
            c = mem.read(p, 1)[0]
            if c == 0x24:
                break
            self.out.append(c)
            p += 1
        self.loose = {"a", "x"}
        return 0, 0

    def f12(self, a, x, mem):
        return 0x31, 0

    def f13(self, a, x, mem):
        for dv in self.drives.values():
            dv.logged = False
            dv.ro = False
        self.kill(lambda s: True)
        self.cur = 0
        self.dma = TPA_DMA
        return 0, 0

    def f14(self, a, x, mem):
        d = a & 0x0F
        if d not in self.drives:
            return 0xFF, 4
        self.cur = d
        self.use(d)
        return 0, 0

    def f15(self, a, x, mem):
        addr = a | x << 8
        b = mem.read(addr, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        if b[32] == 0xFF:
            raise Unspecified("open with CR $FF (the last record's byte count)")
        dv = self.use(d)
        name = bytes(c & 0x7F for c in b[1:12])
        ext = b[12] & 0x1F
        user, ro = self.user, False
        i = self.find_ext(dv, user, name, ext)
        if i is None and user != 0:
            j = self.find_ext(dv, 0, name, ext)
            if j is not None and dv.entry(j)[10] & 0x80:
                i, user, ro = j, 0, True
        if i is None:
            return 0xFF, 0
        e = dv.entry(i)
        s = Fcb(d, user, bytes(e[1:12]), ro or bool(e[9] & 0x80), b[0])
        s.opener = self.user
        s.cr = b[32]
        self.load_ext(s, ext)
        self.fcbs[addr] = s
        self.put_fcb(addr, s, b, mem)
        if dv.mode & MODE_ACCESS and ext == 0 and not dv.ro:  # access overwrites a creation stamp
            o = dv.slot(i)
            if o is not None:
                dv.img[o:o + 4] = self.now
        return self.code(i), 0

    def f16(self, a, x, mem):
        addr = a | x << 8
        s, b = self.fcb_state(addr, mem, "close")
        if not s.modified:
            s.closed = True
            return 0, 0
        dv = self.drives[s.drive]
        if dv.ro:
            return 0xFF, 2
        i = self.writeback(s)
        s.closed = True
        if i is None:
            return 0xFF, 0
        self.put_fcb(addr, s, b, mem)
        return self.code(i), 0

    def match(self, dv, e, pat, user):
        if e[0] != user:
            return False
        for k in range(1, 12):
            p = pat[k] & 0x7F
            if p != 0x3F and (e[k] & 0x7F) != p:
                return False
        if pat[12] == 0x3F:
            return True
        m = ~dv.fmt.exm
        return (e[14] & 0x3F) == (pat[14] & 0x3F) and (e[12] & 0x1F & m) == (pat[12] & 0x1F & m)

    def f17(self, a, x, mem):
        b = mem.read(a | x << 8, 36)
        every = b[0] == 0x3F
        d = self.cur if every else self.drive_of(b[0])
        if d is None:
            self.search = None
            return 0xFF, 4
        self.use(d)
        self.search = {"drive": d, "pat": bytes(b[:15]), "all": every, "user": self.user, "next": 0}
        return self._next(mem)

    def f18(self, a, x, mem):
        if self.search is None:
            raise Unspecified("18 not after 17 or 18")
        return self._next(mem)

    def _next(self, mem):
        q = self.search
        dv = self.drives[q["drive"]]
        while q["next"] <= dv.fmt.drm:
            i = q["next"]
            q["next"] += 1
            e = dv.entry(i)
            if q["all"] or self.match(dv, e, q["pat"], q["user"]):
                o = dv.fmt.dir_off + (i & ~3) * 32
                mem.write(self.dma, bytes(dv.img[o:o + RECORD]))
                return self.code(i), 0
        return 0xFF, 0

    def f19(self, a, x, mem):
        b = mem.read(a | x << 8, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        pat = bytearray(b[:15])
        pat[12] = 0x3F
        hits = [i for i in dv.entries() if self.match(dv, dv.entry(i), pat, self.user)]
        if not hits:
            return 0xFF, 0
        if any(dv.entry(i)[9] & 0x80 for i in hits):
            return 0xFF, 3
        for i in hits:
            e = bytearray(dv.entry(i))
            dv.alv.difference_update(dv.blocks(e))
            dv.alv.update(range(dv.fmt.dir_blocks))
            e[0] = E5
            dv.set_entry(i, bytes(e))
        names = {dv.entry(i)[1:12] for i in hits}
        self.kill(lambda s: s.drive == d and s.user == self.user and any(same_name(s.name, n) for n in names))
        self.loose = {"code"}
        return self.code(hits[0]), 0

    def _fcb_io(self, a, x, mem, op):
        addr = a | x << 8
        s, b = self.fcb_state(addr, mem, op)
        others = self.sharers(s)
        writing = op in ("write", "random write", "write zero fill")
        if others and (writing or any(o.modified for o in others) or s.modified):
            raise Unspecified("%s: two FCBs on one file, one writing" % op)
        return addr, s, b

    def f20(self, a, x, mem):
        addr, s, b = self._fcb_io(a, x, mem, "read")
        if s.uncertain:
            raise Unspecified("read: the position is uncertain after an error")
        dv = self.drives[s.drive]
        moved = s.cr >= 128
        if moved and s.ext < MAX_EXT and s.modified and dv.ro:
            return 0xFF, 2                               # moving on closes the written extent: R/O disk
        if moved:
            if s.ext >= MAX_EXT or self.move(s, s.ext + 1, False):
                s.cr = 0
                s.uncertain = True
                self.put_fcb(addr, s, b, mem)
                return 1, 0
            s.cr = 0
        c = s.cr
        if c >= s.rc:
            s.uncertain = moved              # at the end of an extent CP/M's FCB may be either side
            self.put_fcb(addr, s, b, mem)
            return 1, 0
        off = (s.ext & dv.fmt.exm) * 128 + c
        blk = s.map[off // dv.fmt.rpb]
        if not blk:
            s.uncertain = True
            self.put_fcb(addr, s, b, mem)
            return 1, 0
        data, und = dv.read_rec(blk, off % dv.fmt.rpb)
        mem.write(self.dma, data)
        self.undefined = und
        s.cr = c + 1
        self.put_fcb(addr, s, b, mem)
        return 0, 0

    def _write_rec(self, s, dv, c, data, zero):
        """Record c of the current extent: a block allocated if it has none. @return 0 or 2."""
        off = (s.ext & dv.fmt.exm) * 128 + c
        k = off // dv.fmt.rpb
        blk = s.map[k]
        if not blk:
            hint = next((b for b in reversed(s.map[:k]) if b), 0)
            blk = self.allocate(dv, hint)
            if blk is None:
                return 2
            dv.alv.add(blk)
            dv.new_block(blk, zero)
            s.map[k] = blk
        dv.write_rec(blk, off % dv.fmt.rpb, data)
        s.rc = max(s.rc, c + 1)
        s.modified = True
        return 0

    def f21(self, a, x, mem):
        addr, s, b = self._fcb_io(a, x, mem, "write")
        if s.uncertain:
            raise Unspecified("write: the position is uncertain after an error")
        dv = self.drives[s.drive]
        if dv.ro:
            return 0xFF, 2
        if s.ro:
            return 0xFF, 3
        if s.cr >= 128:
            if s.ext >= MAX_EXT:
                raise Unspecified("write past the largest file")
            if self.move(s, s.ext + 1, True):
                s.uncertain = True
                self.put_fcb(addr, s, b, mem)
                return 1, 0
            s.cr = 0
        data = mem.read(self.dma, RECORD)
        r = self._write_rec(s, dv, s.cr, data, False)
        if r:
            s.uncertain = True
        else:
            s.cr += 1
        self.put_fcb(addr, s, b, mem)
        return r, 0

    def f22(self, a, x, mem):
        addr = a | x << 8
        b = mem.read(addr, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        if b[12] or b[14] or any(c & 0x80 for c in b[1:12]):
            raise Unspecified("make with EX, S2 or attributes set")
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        name = bytes(b[1:12])
        if dv.file_entries(self.user, name):
            return 0xFF, 8
        s = Fcb(d, self.user, name, False, b[0])
        s.opener = self.user
        if self.new_entry(dv, s, 0, first=True) is None:
            return 0xFF, 0
        self.kill(lambda o: o.drive == d and o.user == self.user and same_name(o.name, name))
        s.cr = b[32]
        self.fcbs[addr] = s
        self.put_fcb(addr, s, b, mem)
        return self.code(s.entry), 0

    def f23(self, a, x, mem):
        b = mem.read(a | x << 8, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        old, new = bytes(c & 0x7F for c in b[1:12]), bytes(c & 0x7F for c in b[17:28])
        if has_wild(old) or has_wild(new):
            return 0xFF, 9
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        if dv.file_entries(self.user, new):              # checked first, as OODOS's BDOS does
            return 0xFF, 8
        hits = dv.file_entries(self.user, old)
        if not hits:
            return 0xFF, 0
        if any(dv.entry(i)[9] & 0x80 for i in hits):
            return 0xFF, 3
        for i in hits:
            e = bytearray(dv.entry(i))
            for k in range(11):
                e[1 + k] = (e[1 + k] & 0x80) | new[k]
            dv.set_entry(i, bytes(e))
        self.kill(lambda s: s.drive == d and s.user == self.user and same_name(s.name, old))
        self.loose = {"code"}
        return self.code(hits[0]), 0

    def f24(self, a, x, mem):
        v = sum(1 << d for d, dv in self.drives.items() if dv.logged)
        return v & 0xFF, v >> 8

    def f25(self, a, x, mem):
        return self.cur, 0

    def f26(self, a, x, mem):
        self.dma = a | x << 8
        self.loose = {"a", "x"}
        return 0, 0

    def f27(self, a, x, mem):
        return 0xFF, 0xFF

    def f28(self, a, x, mem):
        self.drives[self.cur].ro = True
        return 0, 0

    def f29(self, a, x, mem):
        v = sum(1 << d for d, dv in self.drives.items() if dv.ro)
        return v & 0xFF, v >> 8

    def f30(self, a, x, mem):
        b = mem.read(a | x << 8, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        name = bytes(c & 0x7F for c in b[1:12])
        hits = dv.file_entries(self.user, name)
        if not hits:
            return 0xFF, 0
        for i in hits:
            e = bytearray(dv.entry(i))
            for k in (1, 2, 3, 4, 9, 10, 11):
                e[k] = (e[k] & 0x7F) | (b[k] & 0x80)
            dv.set_entry(i, bytes(e))
        self.kill(lambda s: s.drive == d and s.user == self.user and same_name(s.name, name))
        self.loose = {"code"}
        return self.code(hits[0]), 0

    def f31(self, a, x, mem):
        self.use(self.cur)                               # it selects: the manual has it return $FFFF on an I/O error
        mem.write(self.dpb_addr, self.drives[self.cur].fmt.dpb())
        self.loose = {"a", "x"}
        return self.dpb_addr & 0xFF, self.dpb_addr >> 8

    def f32(self, a, x, mem):
        if a == 0xFF:
            return self.user, 0
        self.user = a & 0x0F
        self.loose = {"a", "x"}
        return 0, 0

    def _random(self, a, x, mem, op):
        addr, s, b = self._fcb_io(a, x, mem, op)
        r = b[33] | b[34] << 8 | b[35] << 16
        return addr, s, b, r

    def f33(self, a, x, mem):
        addr, s, b, r = self._random(a, x, mem, "random read")
        dv = self.drives[s.drive]
        if b[35] > 3:
            s.uncertain = True
            return 6, 0
        ext, c = r >> 7, r & 127
        if (ext != s.ext or s.entry is None) and s.modified and dv.ro:
            return 0xFF, 2                               # moving closes the written extent: R/O disk
        if self.move(s, ext, False):
            s.cr = c
            s.uncertain = True
            self.put_fcb(addr, s, b, mem)
            return 4, 0
        s.cr = c
        off = (s.ext & dv.fmt.exm) * 128 + c
        blk = s.map[off // dv.fmt.rpb]
        if c >= s.rc or not blk:
            s.uncertain = True
            self.put_fcb(addr, s, b, mem)
            return 1, 0
        data, und = dv.read_rec(blk, off % dv.fmt.rpb)
        mem.write(self.dma, data)
        self.undefined = und
        s.uncertain = False
        self.put_fcb(addr, s, b, mem)
        return 0, 0

    def _wrand(self, a, x, mem, zero):
        addr, s, b, r = self._random(a, x, mem, "write zero fill" if zero else "random write")
        dv = self.drives[s.drive]
        if dv.ro:
            return 0xFF, 2
        if s.ro:
            return 0xFF, 3
        if b[35] > 3:
            s.uncertain = True
            return 6, 0
        ext, c = r >> 7, r & 127
        if self.move(s, ext, True):
            s.cr = c
            s.uncertain = True
            self.put_fcb(addr, s, b, mem)
            return 5, 0
        s.cr = c
        data = mem.read(self.dma, RECORD)
        e = self._write_rec(s, dv, c, data, zero)
        s.uncertain = bool(e)
        self.put_fcb(addr, s, b, mem)
        return e, 0

    def f34(self, a, x, mem):
        return self._wrand(a, x, mem, False)

    def f40(self, a, x, mem):
        return self._wrand(a, x, mem, True)

    def f35(self, a, x, mem):
        addr = a | x << 8
        b = mem.read(addr, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        dv = self.use(d)
        n = dv.file_size(self.user, bytes(c & 0x7F for c in b[1:12]))
        self.put_r(addr, n or 0, mem)
        return (0xFF, 0) if n is None else (0, 0)

    def f36(self, a, x, mem):
        addr = a | x << 8
        s, b = self.fcb_state(addr, mem, "set random record")
        if s.uncertain:
            raise Unspecified("36: the position is uncertain after an error")
        self.put_r(addr, s.ext * 128 + s.cr, mem)
        self.loose = {"a", "x"}
        return 0, 0

    def f37(self, a, x, mem):
        v = a | x << 8
        for d, dv in self.drives.items():
            if v >> d & 1:
                dv.logged = False
                dv.ro = False
        self.kill(lambda s: v >> s.drive & 1)
        return 0, 0

    def f45(self, a, x, mem):
        self.errmode = a
        self.loose = {"a", "x"}
        return 0, 0

    def f46(self, a, x, mem):
        d = a & 0x0F
        if d not in self.drives:
            return 0xFF, 4
        dv = self.use(d)
        n = len(dv.free_blocks()) * dv.fmt.rpb
        mem.write(self.dma, bytes([n & 0xFF, n >> 8 & 0xFF, n >> 16 & 0xFF]))
        return 0, 0

    def f48(self, a, x, mem):
        return 0, 0

    def f98(self, a, x, mem):
        for d, dv in self.drives.items():
            if dv.logged:
                dv.login()
        self.kill(lambda s: s.modified)
        return 0, 0

    def f99(self, a, x, mem):
        addr = a | x << 8
        b = mem.read(addr, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        if b[35] > 3:                                    # R past 32 MB, before the directory is read
            return 0xFF, 0
        name = bytes(c & 0x7F for c in b[1:12])
        hits = dv.file_entries(self.user, name)
        if not hits:
            return 0xFF, 0
        if any(dv.entry(i)[9] & 0x80 for i in hits):
            return 0xFF, 3
        r = b[33] | b[34] << 8 | b[35] << 16
        if r >= dv.file_size(self.user, name):
            return 0xFF, 0
        f = dv.fmt
        ext, c = r >> 7, r & 127
        keep = self.find_ext(dv, self.user, name, ext)
        if keep is None:
            raise Unspecified("truncate into a hole")
        ke = dv.entry(keep)
        lo, dex = ext & 0x1F, ke[12] & 0x1F
        rc = 128 if lo < dex else ke[15] if lo == dex else 0
        if c >= rc or not dv.blocks(ke)[((ext & f.exm) * 128 + c) // f.rpb]:
            raise Unspecified("truncate into a hole")
        for i in hits:
            e = bytearray(dv.entry(i))
            first = (e[14] & 0x3F) << 5 | e[12] & 0x1F & ~f.exm
            if i == keep:
                k = ((ext & f.exm) * 128 + c) // f.rpb
                bl = dv.blocks(e)
                dv.alv.difference_update(b for b in bl[k + 1:] if b)
                bl = bl[:k + 1] + [0] * (f.slots - k - 1)
                e = dv.put_blocks(e, bl)
                e[12] = (e[12] & 0xE0) | ext & 0x1F
                e[15] = c + 1
                dv.set_entry(i, bytes(e))
            elif first > ext:
                dv.alv.difference_update(b for b in dv.blocks(e) if b)
                e[0] = E5
                dv.set_entry(i, bytes(e))
        dv.alv.update(range(f.dir_blocks))
        self.stamp_slot(dv, keep)
        self.kill(lambda s: s.drive == d and s.user == self.user and same_name(s.name, name))
        return 0, 0

    def f104(self, a, x, mem):
        t = mem.read(a | x << 8, 4)
        self.now = bytes(t)
        self.loose = {"a", "x"}
        return 0, 0

    def f105(self, a, x, mem):
        mem.write(a | x << 8, self.now)
        return 0, 0


class Memory:
    """A program's 64 KB, for the model run without a CPU."""

    def __init__(self):
        self.b = bytearray(65536)

    def read(self, addr, n):
        return bytes(self.b[addr:addr + n])

    def write(self, addr, data):
        self.b[addr:addr + len(data)] = data
