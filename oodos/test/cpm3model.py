"""A model of CP/M 3's file system, for OODOS's randomized BDOS test (hw/oodos.md, "The BDOS, phase 3").

Ours, written from the CP/M 3 Programmer's Guide's descriptions of BDOS functions 13-37, 40, 44, 46, 48,
98-103 and 106 (and phase 3c's 50-52, below), at the level a program sees them: what each returns (A, and
X as CP/M 3's H) and what it leaves in the FCB, the DMA buffer and on the disk. Its state is the drives' images, laid out as CP/M 3 lays them out:
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
  2. After a random read or write CR is the record's, not the next (the manual's rule). A read that must
  write a written extent back (moving to another) on a read-only drive returns A = $FF, X = 2, the FCB
  as it was (as OODOS's BDOS: the implied close is refused). The FCB's S2 has the module and, as
  OODOS's BDOS keeps them, $80 while the extent isn't written and $40 while it has no entry yet, so a
  program's own FCB with the open one's name and place is a changed FCB, not the open one.
- A program gives an FCB's bytes 0-12 (the Guide, 15 and 22): open, make and search (EX not '?') take
  S2 as 0. Open (15) opens the extent EX names; a file not in the current user but in user 0 with the
  SYS attribute (t2') opens from user 0, read only. Writes to it, or to a file whose t1' is set, return
  A = $FF, X = 3 (File R/O). Open with CR $FF puts the last record's byte count (the entry's S1) in CR.
- Make (22) makes extent EX's entry (stamped if EX is 0), every attribute 0 in it and in the FCB
  (2.3.4); a name the user has in any extent is 8, File Exists.
- Errors return (the driver sets 45 to $FF): A = $FF and X = CP/M 3's H: 2 R/O disk, 3 R/O file, 4 select
  error, 8 file exists, 9 '?' in the name. "Not found", "no directory space" and the like are A = $FF,
  X = 0. Checks go in this order: the drive, '?' (9), a read-only drive (2), not found, a read-only file
  (3), exists (8); but rename (23) checks the new name first: the drive, '?' in either name, a read-only
  drive, the new name exists (8), the old one not found, a read-only file (3), as OODOS's BDOS does (the
  manual lists the errors in no order).
- Directory codes: 17 and 18 return the entry's index mod 4; the others return 0 on success (the
  Guide, 2.3, after Table 2-8). A close of an FCB not written returns 0 and writes nothing. Success puts
  0 in X.
- Delete (19) takes '?' and all extents; if any match is R/O nothing is deleted. Rename (23) keeps the
  entries' attributes; '?' in either name is 9, a new name that exists is 8. Set attributes (30) copies
  f1'-f4' and t1'-t3' from the FCB to every extent, no '?', and with f6' CR into every extent's S1, the
  byte count. File size (35) and truncate (99): the
  current user only, '?' is 9. File size puts 0 in R0-R2 when there's no such file (as CP/M 2.2 did).
- Truncate (99) keeps records 0 to R: entries past R's extent are deleted, the one holding it gets EX and
  RC for R and loses the blocks after R's. R at or past the end, or where the file has no data (no
  entry reaching R's extent, past RC there, no block: the Guide's "a region of the file where data
  exists"), is A = $FF, X = 0, and R2 over 3 is A = $FF, X = 0 before the file is looked for (so before
  a read-only file's 3). It stamps the update, as a close after a write does (the manual doesn't say;
  OODOS's BDOS does).
- Date stamps, by the label's mode (byte 12: $10 create, $20 update, $40 access), only ever in the
  SFCB slot of a file's extent-zero entry (the manual: a slot is valid only for an extent-zero entry).
  A new entry's slot is cleared (its password mode, but a protected file's extent 0's, and reserved
  byte 0 too), whatever the mode asks
  (3b1: so 102 never reads another file's); make (of extent 0) sets its create (or access) and update
  stamps to now as the mode asks, an entry made by a write gets none. Writing a written
  FCB's entry back (close, or moving to another extent) and truncate set the update stamp in the
  file's extent-zero entry's slot, whichever extent they changed. Open of extent 0 sets the access
  stamp with $40 (over the creation stamp, the field they share), not on a read-only drive. Delete,
  rename and 30 leave the SFCB as it is. Now is
  what 104 last set (the driver sets the time before what stamps).
- 13 resets every drive (R/O cleared, logged out), drive A current, the DMA $0280; 37 the drives in its
  vector. 28 sets the current drive R/O. 46 puts the free records, 3 bytes, in the DMA buffer. 31 logs
  the current drive in (the manual has it return $FFFF on an I/O error, so it reads the disk) and returns
  the address of a copy of its DPB (17 bytes). 27 returns $FFFF. 12 returns $0031.
- What a record holds: a block allocated by a write (21, 34) has every record undefined until written
  (the BDOS's deblocking may write whatever its buffer held); 40 fills the new block with zeros. Reading
  an undefined record succeeds and its data can't be predicted (Result.undefined).

- Phase 3b1 (hw/oodos.md, "The BDOS, phase 3", Proposed (3b1)): the multi-sector count (44, 1-128, else
  $FF): 20, 21, 33, 34 and 40 with a count N are N calls, record k at the DMA address + 128 k, to the first
  that doesn't return 0: A that, X the records done before it, but a physical error's (A = $FF) X its
  code; 33, 34 and 40 put R0-R2 back to the first record's. N records past $FFFF are A = X = $FF before
  the first, nothing done, after the FCB's checks and a write's R/O drive and file (the BDOS's order);
  the BDOS refuses records in the I/O hole or on flash the same way, which the model's flat memory (its own addresses, MBUF at $C000) doesn't have. Labels (100, 101): the label's data byte (FCB
  byte 12's bits 7-4, bit 0 set), name, its password (bytes 13, 16-23) and stamps (24-31: creation when
  made, update every time); bit 0 of FCB byte 12 a new password from the DMA buffer's bytes 8-15; stamps
  asked for without SFCBs (entry 3 isn't one) $FF; a label's password must be given (X 7). XFCBs (user +
  16): made by 22 with f6' (the password the DMA buffer's 0-7, the mode its byte 8, bits 7-5; two free
  entries needed, or an XFCB of the name) and 103 (the mode FCB byte 12's bits 7-5, bit 0 a new password
  from the DMA's 8-15; a new one's password otherwise blanks), followed by 19 and 23, deleted alone by 19
  with f5'. The mode is also kept in the file's extent-zero SFCB slot (byte 8), also when that entry is
  made after the XFCB (its mode, not 0, with the label's passwords on or off). Passwords are kept as CP/M
  3 keeps them (the key, the sum of the 8 bytes, at 13; the bytes reversed, XORed with it, at 16-23), 8
  blanks or 8 zeros is none (kept as blanks). With the label's bit 7: a protected file (its XFCB has a
  mode bit) needs its password, the DMA buffer's first 8 bytes or the default (106: none from the start),
  for 19, 23, 30 and 99 (X 7, the last check) and for 15 in read mode; 15 in write mode opens it read-only
  (writes X 3). 102 gives the XFCB's mode (0 without) in byte 12 and the extent-zero slot's stamps in
  24-31 (0 without), the current user's file in any extent.

- Phase 3c (hw/oodos.md, "The BDOS, phase 3", Proposed (3c)): the DMA segment, 51 ($FFFF none, the default;
  13 sets the DMA address and keeps it, ours), 52 (the DMA address and the segment, 4 bytes). With one,
  the records of 17, 18, 20, 21, 33, 34 and 40 go to and from physical memory of the model's own (Bdos.phys, 1
  MB) at segment x 16 + the DMA address; 46's result and the passwords stay at the CPU's. A segment's records
  where it may not reach (seg_ok: blocks 24-31, 48-55, 60-63, past $FFFFF) or past an offset of $FFFF are
  A = X = $FF: for 20, 21, 33, 34 and 40 before the first record, as self.past; for 17 and 18 once the search
  has moved on. 50 is modelled only as DRIVER uses it: XMOVE (BIOS 29) of banks 1 (the program's) or $80 +
  A16-A19 (physical), then MOVE (25), which returns the destination + the count.

Where CP/M 3 leaves behaviour open in a way a test can't settle, the model raises Unspecified before it
changes anything, and the generator chooses something else: an FCB used after its file was deleted,
renamed, changed by 30 or 99, or its drive reset (13, 37), or after 98 while it had blocks not yet
closed; two FCBs on one file with either writing; sequential I/O or 36 after an error left the position
uncertain; 18 not straight after 17 or 18; an FCB changed by the program (but for R0-R2); the current
user or drive changed under an open FCB; make with EX past 31.
"""

import os

RECORD = 128
E5 = 0xE5
SFCB = 0x21
LABEL = 0x20
TPA_DMA = 0x0280
MAX_EXT = 2047                          # S2 63, EX 31: 32 MB files
MODE_CREATE, MODE_UPDATE, MODE_ACCESS, MODE_PASSWORD = 0x10, 0x20, 0x40, 0x80
XFCB_MODES = 0xE0
BLANK_PW = b" " * 8


def pw_none(pw):
    """8 blanks or 8 zero bytes: no password, which anything matches."""
    return pw == BLANK_PW or pw == bytes(8)


def pw_decode(e):
    """An entry's password (an XFCB's or the label's), as CP/M 3 keeps it."""
    return bytes(e[23 - j] ^ e[13] for j in range(8))


def pw_encode(e, pw):
    """The entry with password pw kept: the key at 13, the bytes reversed and XORed with it at 16-23."""
    if pw_none(pw):
        pw = BLANK_PW
    e = bytearray(e)
    key = sum(pw) & 0xFF
    e[13] = key
    for j in range(8):
        e[23 - j] = pw[j] ^ key
    return bytes(e)


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


NO_SEG = 0xFFFF
PHYS_SIZE = 1 << 20
BIOS_MOVE, BIOS_XMOVE = 25, 29


def seg_ok(p, n):
    """The n bytes at physical p are memory a DMA segment may reach (hw/oodos.md, phase 3c): any RAM but
    OODOS's own, blocks 24-31 (banks 6-7), 48-55 (C-D, M:) and 60-63, and nothing past $FFFFF."""
    end = p + n - 1
    if end >= PHYS_SIZE:
        return False
    return all(not (24 <= b < 32 or 48 <= b < 56 or b >= 60) for b in (p >> 14, end >> 14))


class Bdos:
    def __init__(self, drives, now=None, alloc="lowest", dpb_addr=0xF900):
        self.drives = drives
        self.now = now or stamp(2026, 10, 27, 12, 0)
        self.alloc = alloc
        self.dpb_addr = dpb_addr
        self.cur, self.user, self.dma, self.errmode = 0, 0, TPA_DMA, 0
        self.multi = 1                       # 44's count
        self.seg = NO_SEG                    # 51's DMA segment (3c)
        self.phys = bytearray(PHYS_SIZE)     # physical memory, where a segment's records go
        self.xmove = None                    # 50's XMOVE banks, for the next MOVE
        self.past = False                    # the call's records go past $FFFF: its first refuses it
        self.defpw = bytes(8)                # 106's: none
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
        if fn not in (17, 18) and self.search is keep and fn not in (2, 9, 12, 25, 26, 45, 50, 51, 52, 104, 105):
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

    # ------------------------------------------------------------------ the DMA segment (3c)
    def phys_dma(self):
        return self.seg * 16 + self.dma

    def seg_refused(self, n):
        """With a segment, n records at the DMA address that a segment may not reach (seg_ok), or past
        $FFFF. Without one False: the CPU's own checks are self.past's."""
        if self.seg == NO_SEG:
            return False
        return self.dma + RECORD * n > 0x10000 or not seg_ok(self.phys_dma(), RECORD * n)

    def dma_put(self, mem, data):
        if self.seg == NO_SEG:
            mem.write(self.dma, data)
        else:
            p = self.phys_dma()
            self.phys[p:p + len(data)] = data

    def dma_get(self, mem):
        if self.seg == NO_SEG:
            return mem.read(self.dma, RECORD)
        p = self.phys_dma()
        return bytes(self.phys[p:p + RECORD])

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
        if made:                                         # cleared whatever the mode (102 reads it)
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
        o = dv.slot(i)
        if o is not None and ext >> 5 == 0 and ext & 0x1F & ~dv.fmt.exm == 0:
            x = self.xfcb_of(dv, s.user, s.name)         # extent 0 made after the XFCB: its mode (2.3.8),
            if x is not None:                            # the label's passwords on or off
                dv.img[o + 8] = dv.entry(x)[12] & XFCB_MODES
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

    # ------------------------------------------------------------------ passwords (3b1)
    def given(self, pw, mem):
        """Whether pw is given: none, the DMA buffer's first 8 bytes, or the default password."""
        return pw_none(pw) or pw == bytes(mem.read(self.dma, 8)) or pw == self.defpw

    def xfcb_of(self, dv, user, name):
        for i in dv.entries():
            e = dv.entry(i)
            if e[0] == user + 16 and same_name(e[1:12], name):
                return i
        return None

    def locked(self, dv, e, mem):
        """An XFCB that protects its file, whose password isn't given (with the label's passwords on)."""
        return bool(dv.mode & MODE_PASSWORD and e[12] & XFCB_MODES and not self.given(pw_decode(e), mem))

    def protected(self, dv, user, name, mem):
        """user:name's XFCB's mode if it is protected and its password isn't given, else 0."""
        x = self.xfcb_of(dv, user, name)
        if x is None:
            return 0
        e = dv.entry(x)
        return e[12] & XFCB_MODES if self.locked(dv, e, mem) else 0

    def slot_mode(self, dv, user, name, mode):
        """The mode into the SFCB slot (its byte 8) of user:name's extent-zero entry, if it has one."""
        i = self.find_ext(dv, user, name, 0)
        if i is None:
            return
        o = dv.slot(i)
        if o is not None:
            dv.img[o + 8] = mode

    def put_xfcb(self, dv, user, name, mode, pw, x=None):
        """user:name's XFCB, in entry x or the first free one: the mode, the password, the rest 0."""
        if x is None:
            x = self.free_entry(dv)
        e = bytearray(32)
        e[0] = user + 16
        e[1:12] = bytes(c & 0x7F for c in name)
        e[12] = mode
        dv.set_entry(x, pw_encode(bytes(e), pw))
        self.slot_mode(dv, user, name, mode)

    def label_of(self, dv):
        for i in dv.entries():
            if dv.entry(i)[0] == LABEL:
                return i
        return None

    # ------------------------------------------------------------------ multi-sector I/O (3b1)
    def _multi(self, one, a, x, mem, random):
        """A record function the multi-sector count's times: N calls, record k at the DMA + 128 k. Records
        past $FFFF are refused (FF FF, nothing done) by the first call, after the checks the BDOS makes
        before MRANGE: the FCB's drive and, for a write, the R/O drive and R/O file."""
        n = self.multi
        addr = a | x << 8
        self.past = self.dma + RECORD * n > 0x10000 or self.seg_refused(n)
        if n > 1 and one == self._f21 and not self.past:  # a run past the largest file: open, refused
            s = self.fcbs.get(addr)
            if s is not None and s.ext * 128 + s.cr + n > MAX_EXT * 128 + 128:
                raise Unspecified("a multi-sector write past the largest file")
        dma, r0 = self.dma, bytes(mem.read(addr + 33, 3))
        undefined, done = False, 0
        try:
            for k in range(n):
                self.dma = dma + RECORD * k
                if random and k:
                    r = (r0[0] | r0[1] << 8 | r0[2] << 16) + k
                    mem.write(addr + 33, bytes([r & 0xFF, r >> 8 & 0xFF, r >> 16 & 0xFF]))
                try:
                    ra, rx = one(a, x, mem)
                except Unspecified:
                    if k:
                        raise RuntimeError("a multi-sector call: Unspecified after %d records" % k)
                    raise
                self.past = False
                undefined = undefined or self.undefined
                if ra:
                    break
                done += 1
        finally:
            self.dma, self.past = dma, False
            if random and (done or n > 1):
                mem.write(addr + 33, r0)
        self.undefined = undefined
        if ra == 0:
            return 0, 0
        return ra, rx if ra == 0xFF else done

    def f20(self, a, x, mem):
        return self._multi(self._f20, a, x, mem, False)

    def f21(self, a, x, mem):
        return self._multi(self._f21, a, x, mem, False)

    def f33(self, a, x, mem):
        return self._multi(self._f33, a, x, mem, True)

    def f34(self, a, x, mem):
        return self._multi(lambda a, x, mem: self._wrand(a, x, mem, False), a, x, mem, True)

    def f40(self, a, x, mem):
        return self._multi(lambda a, x, mem: self._wrand(a, x, mem, True), a, x, mem, True)

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
        self.dma = TPA_DMA                   # the segment kept (3c, ours)
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
        prot = self.protected(dv, user, name, mem)
        if prot & 0x80:
            return 0xFF, 7                               # read mode, no password
        e = dv.entry(i)
        s = Fcb(d, user, bytes(e[1:12]), ro or bool(e[9] & 0x80) or bool(prot & 0x40), b[0])
        s.opener = self.user
        self.load_ext(s, ext)
        s.cr = s.s1 if b[32] == 0xFF else b[32]           # CR $FF: the last record's byte count
        self.fcbs[addr] = s
        self.put_fcb(addr, s, b, mem)
        if dv.mode & MODE_ACCESS and ext == 0 and not dv.ro:  # access overwrites a creation stamp
            o = dv.slot(i)
            if o is not None:
                dv.img[o:o + 4] = self.now
        return 0, 0

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
        return 0, 0

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
        pat = bytearray(b[:15])
        if pat[12] != 0x3F:
            pat[14] = 0                                  # the program gives bytes 0-12: module 0
        self.search = {"drive": d, "pat": bytes(pat), "all": every, "user": self.user, "next": 0}
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
                if self.seg_refused(1):                  # the search has moved on, as the BDOS's
                    return 0xFF, 0xFF
                self.dma_put(mem, bytes(dv.img[o:o + RECORD]))
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
        files = [i for i in dv.entries() if self.match(dv, dv.entry(i), pat, self.user)]
        xfcbs = [i for i in dv.entries() if self.match(dv, dv.entry(i), pat, self.user + 16)]
        if not files and not xfcbs:
            return 0xFF, 0
        if any(dv.entry(i)[9] & 0x80 for i in files):
            return 0xFF, 3
        if any(self.locked(dv, dv.entry(i), mem) for i in xfcbs):
            return 0xFF, 7                               # the password, last
        only = bool(b[5] & 0x80)                         # f5': the XFCBs only
        for i in sorted(xfcbs + ([] if only else files)):
            e = bytearray(dv.entry(i))
            if e[0] >= 16:
                if only:
                    self.slot_mode(dv, e[0] - 16, bytes(c & 0x7F for c in e[1:12]), 0)
            else:
                dv.alv.difference_update(dv.blocks(e))
                dv.alv.update(range(dv.fmt.dir_blocks))
            e[0] = E5
            dv.set_entry(i, bytes(e))
        if not only:
            names = {dv.entry(i)[1:12] for i in files}
            self.kill(lambda s: s.drive == d and s.user == self.user and any(same_name(s.name, n) for n in names))
        return 0, 0

    def _fcb_io(self, a, x, mem, op):
        addr = a | x << 8
        s, b = self.fcb_state(addr, mem, op)
        others = self.sharers(s)
        writing = op in ("write", "random write", "write zero fill")
        if others and (writing or any(o.modified for o in others) or s.modified):
            raise Unspecified("%s: two FCBs on one file, one writing" % op)
        return addr, s, b

    def _f20(self, a, x, mem):
        addr, s, b = self._fcb_io(a, x, mem, "read")
        if self.past:                                    # past $FFFF (MRANGE): refused
            return 0xFF, 0xFF
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
        self.dma_put(mem, data)
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

    def _f21(self, a, x, mem):
        addr, s, b = self._fcb_io(a, x, mem, "write")
        if s.uncertain:
            raise Unspecified("write: the position is uncertain after an error")
        dv = self.drives[s.drive]
        if dv.ro:
            return 0xFF, 2
        if s.ro:
            return 0xFF, 3
        if self.past:                                    # past $FFFF (MRANGE): refused
            return 0xFF, 0xFF
        if s.cr >= 128:
            if s.ext >= MAX_EXT:
                raise Unspecified("write past the largest file")
            if self.move(s, s.ext + 1, True):
                s.uncertain = True
                self.put_fcb(addr, s, b, mem)
                return 1, 0
            s.cr = 0
        data = self.dma_get(mem)
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
        if b[12] > 31:
            raise Unspecified("make with EX past 31")
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        name = bytes(c & 0x7F for c in b[1:12])         # every attribute 0 (S2 isn't the program's)
        if dv.file_entries(self.user, name):
            return 0xFF, 8
        xfcb = bool(b[6] & 0x80 and dv.mode & MODE_PASSWORD)     # f6' with passwords on: an XFCB too
        if xfcb:
            x = self.xfcb_of(dv, self.user, name)
            if x is None and sum(1 for i in dv.entries() if dv.entry(i)[0] == E5) < 2:
                return 0xFF, 0                           # no room for the file and its XFCB
        s = Fcb(d, self.user, name, False, b[0])
        s.opener = self.user
        if self.new_entry(dv, s, b[12], first=b[12] == 0) is None:   # extent 0's stamped
            return 0xFF, 0
        if xfcb:
            pw = mem.read(self.dma, 9)
            self.put_xfcb(dv, self.user, name, pw[8] & XFCB_MODES, pw[:8], x)
        self.kill(lambda o: o.drive == d and o.user == self.user and same_name(o.name, name))
        s.cr = b[32]
        self.fcbs[addr] = s
        self.put_fcb(addr, s, b, mem)
        return 0, 0

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
        xfcbs = dv.file_entries(self.user + 16, old)
        if not hits and not xfcbs:
            return 0xFF, 0
        if any(dv.entry(i)[9] & 0x80 for i in hits):
            return 0xFF, 3
        if any(self.locked(dv, dv.entry(i), mem) for i in xfcbs):
            return 0xFF, 7
        for i in hits + xfcbs:
            e = bytearray(dv.entry(i))
            for k in range(11):
                e[1 + k] = (e[1 + k] & 0x80) | new[k]
            dv.set_entry(i, bytes(e))
        self.kill(lambda s: s.drive == d and s.user == self.user and same_name(s.name, old))
        return 0, 0

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
        if self.protected(dv, self.user, name, mem):
            return 0xFF, 7
        for i in hits:
            e = bytearray(dv.entry(i))
            for k in (1, 2, 3, 4, 9, 10, 11):
                e[k] = (e[k] & 0x7F) | (b[k] & 0x80)
            if b[6] & 0x80:                              # f6': the byte count, CR, into S1
                e[13] = b[32]
            dv.set_entry(i, bytes(e))
        self.kill(lambda s: s.drive == d and s.user == self.user and same_name(s.name, name))
        return 0, 0

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

    def _f33(self, a, x, mem):
        addr, s, b, r = self._random(a, x, mem, "random read")
        dv = self.drives[s.drive]
        if self.past:                                    # past $FFFF (MRANGE): refused
            return 0xFF, 0xFF
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
        self.dma_put(mem, data)
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
        if self.past:                                    # past $FFFF (MRANGE): refused
            return 0xFF, 0xFF
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
        data = self.dma_get(mem)
        e = self._write_rec(s, dv, c, data, zero)
        s.uncertain = bool(e)
        self.put_fcb(addr, s, b, mem)
        return e, 0

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

    def f44(self, a, x, mem):
        if not 1 <= a <= 128:
            return 0xFF, 0
        self.multi = a
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
        if keep is None:                                 # R must be where data is (the Guide)
            return 0xFF, 0
        ke = dv.entry(keep)
        lo, dex = ext & 0x1F, ke[12] & 0x1F
        rc = 128 if lo < dex else ke[15] if lo == dex else 0
        if c >= rc or not dv.blocks(ke)[((ext & f.exm) * 128 + c) // f.rpb]:
            return 0xFF, 0
        if self.protected(dv, self.user, name, mem):
            return 0xFF, 7
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

    def f100(self, a, x, mem):
        addr = a | x << 8
        b = mem.read(addr, 36)
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        dv = self.use(d)
        if dv.ro:
            return 0xFF, 2
        if b[12] & (MODE_CREATE | MODE_UPDATE | MODE_ACCESS) and dv.entry(3)[0] != SFCB:
            return 0xFF, 0                               # stamps need SFCBs (entry 3 one)
        i = self.label_of(dv)
        pw = mem.read(self.dma, 16)
        if i is not None:
            e = bytearray(dv.entry(i))
            old = pw_decode(e)
            if not self.given(old, mem):
                return 0xFF, 7
        else:
            i = self.free_entry(dv)
            if i is None:
                return 0xFF, 0
            e = bytearray(32)
            e[24:28] = self.now                          # made: its creation
            old = BLANK_PW
        e[0] = LABEL
        e[1:12] = b[1:12]
        e[12] = b[12] & 0xF0 | 1
        e[28:32] = self.now
        dv.set_entry(i, pw_encode(bytes(e), pw[8:16] if b[12] & 1 else old))
        dv.mode = e[12]
        return 0, 0

    def f101(self, a, x, mem):
        d = a & 0xFF
        if d > 15:
            return 0xFF, 4
        if d not in self.drives:
            return 0xFF, 4
        return self.use(d).mode, 0

    def f102(self, a, x, mem):
        addr = a | x << 8
        b = bytearray(mem.read(addr, 36))
        d = self.drive_of(b[0])
        if d is None:
            return 0xFF, 4
        if self.check_name(b):
            return 0xFF, 9
        dv = self.use(d)
        name = bytes(c & 0x7F for c in b[1:12])
        if not dv.file_entries(self.user, name):
            return 0xFF, 0
        x = self.xfcb_of(dv, self.user, name)
        b[12] = dv.entry(x)[12] if x is not None else 0
        b[24:32] = bytes(8)
        i = self.find_ext(dv, self.user, name, 0)
        o = dv.slot(i) if i is not None else None
        if o is not None:
            b[24:32] = dv.img[o:o + 8]
        mem.write(addr, bytes(b[:33]))
        return 0, 0

    def f103(self, a, x, mem):
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
        if not dv.mode & MODE_PASSWORD:
            return 0xFF, 0
        name = bytes(c & 0x7F for c in b[1:12])
        if not dv.file_entries(self.user, name):
            return 0xFF, 0
        x = self.xfcb_of(dv, self.user, name)
        if x is not None:
            e = dv.entry(x)
            if self.locked(dv, e, mem):
                return 0xFF, 7
            old = pw_decode(e)
        else:
            if self.free_entry(dv) is None:
                return 0xFF, 0
            old = BLANK_PW
        pw = mem.read(self.dma, 16)
        self.put_xfcb(dv, self.user, name, b[12] & XFCB_MODES, pw[8:16] if b[12] & 1 else old, x)
        return 0, 0

    def f106(self, a, x, mem):
        self.defpw = bytes(mem.read(a | x << 8, 8))
        self.loose = {"a", "x"}
        return 0, 0

    def f50(self, a, x, mem):
        """Direct BIOS calls: only XMOVE and MOVE, as DRIVER moves records to and from physical memory with
        them; a bank is 1 (the program's) or $80 + A16-A19 (phase 3c's physical one)."""
        blk = mem.read(a | x << 8, 8)
        fn, ra, rx = blk[0], blk[1], blk[2]
        bw1, bw2 = blk[4] | blk[5] << 8, blk[6] | blk[7] << 8
        self.loose = {"a", "x"}
        if fn == BIOS_XMOVE:
            self.xmove = (ra, rx)
            return 0, 0
        if fn != BIOS_MOVE or self.xmove is None:
            raise Unspecified("function 50: BIOS %d (only XMOVE, then MOVE, are modelled)" % fn)
        (src, dst), self.xmove = self.xmove, None
        n = ra | rx << 8
        for bank in (src, dst):
            if bank != 1 and not 0x80 <= bank <= 0x8F:
                raise Unspecified("function 50: XMOVE bank $%02X" % bank)
        if src == 1:
            data = mem.read(bw1, n)
        else:
            p = (src & 0x0F) << 16 | bw1
            data = bytes(self.phys[p:p + n])
        if dst == 1:
            mem.write(bw2, data)
        else:
            p = (dst & 0x0F) << 16 | bw2
            self.phys[p:p + n] = data
        e = (bw2 + n) & 0xFFFF
        self.loose = set()
        return e & 0xFF, e >> 8

    def f51(self, a, x, mem):
        self.seg = a | x << 8
        return 0, 0

    def f52(self, a, x, mem):
        mem.write(a | x << 8, bytes([self.dma & 0xFF, self.dma >> 8, self.seg & 0xFF, self.seg >> 8]))
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
