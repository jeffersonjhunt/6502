"""DOS 3.3 disk images: make, list, add, take out, delete and check (phase 6d, hw/oodos.md).

Ours, written from the format's layout (no Apple code or text): a 140 KB image is 35 tracks of 16
sectors of 256 bytes, in DOS 3.3's own sector order, sector s of track t at (t x 16 + s) x 256.

  the VTOC         track 17 sector 0: the first catalog sector (+1, +2), the volume (+6), the
                   track/sector pairs a list holds (+$27, 122), the tracks and sectors (+$34, +$35),
                   the bytes a sector (+$36, 256), and at +$38 four bytes a track: sectors 15-8 in the
                   first, bit 7 sector 15, 7-0 in the second, a bit set when the sector is free
  the catalog      a chain of sectors (+1, +2 the next; 0 ends it), each with 7 entries of 35 bytes
                   from +$0B: the first track/sector list's track (0 never used, $FF deleted, the
                   track then kept in the name's last byte) and sector, the type (bit 7 locked),
                   the name (30 bytes, bit 7 set, blanks after), the sectors it takes (a word)
  a file           a chain of track/sector lists (+1, +2 the next; +5 the file's sector the list
                   starts at; 122 pairs from +$0C, 0/0 a sector never written), and its sectors

Types: T text (bit 7 set, CR ends a line, a 0 ends it), I and A BASIC (a length word first), B binary
(an address and a length word first), S, R. The images made here mark tracks 0-2 used and leave them
empty: no DOS on them, so they don't boot.

    python3 -I dos33.py new IMAGE [VOLUME]
    python3 -I dos33.py ls IMAGE
    python3 -I dos33.py put IMAGE NAME TYPE FILE [ADDRESS]     (B: the address; T: the file as text)
    python3 -I dos33.py get IMAGE NAME FILE                    (the sectors as stored)
    python3 -I dos33.py era IMAGE NAME
    python3 -I dos33.py lock IMAGE NAME
    python3 -I dos33.py check IMAGE
"""

import sys

TRACKS, SECTORS, SECTOR = 35, 16, 256
SIZE = TRACKS * SECTORS * SECTOR
VTOC = (17, 0)
PAIRS = 122                      # track/sector pairs a list holds
ENTRIES = 7                      # catalog entries a sector
ENTRY = 35
TYPES = {0x00: "T", 0x01: "I", 0x02: "A", 0x04: "B", 0x08: "S", 0x10: "R", 0x20: "A", 0x40: "B"}
CODES = {"T": 0x00, "I": 0x01, "A": 0x02, "B": 0x04, "S": 0x08, "R": 0x10}


class Bad(Exception):
    """An image this tool won't take, or a request it refuses."""


class Disk:
    def __init__(self, data):
        if len(data) != SIZE:
            raise Bad("not a 140 KB image: %d bytes" % len(data))
        self.d = bytearray(data)

    # sectors
    def at(self, t, s):
        if not (0 <= t < TRACKS and 0 <= s < SECTORS):
            raise Bad("no sector %d/%d" % (t, s))
        return (t * SECTORS + s) * SECTOR

    def sector(self, t, s):
        o = self.at(t, s)
        return self.d[o:o + SECTOR]

    def put_sector(self, t, s, data):
        assert len(data) == SECTOR
        o = self.at(t, s)
        self.d[o:o + SECTOR] = data

    # the VTOC
    def vtoc(self):
        return self.sector(*VTOC)

    def valid(self):
        v = self.vtoc()
        return (v[0x27] == PAIRS and v[0x34] == TRACKS and v[0x35] == SECTORS and v[0x36] == 0
                and v[0x37] == 1 and v[1] < TRACKS and v[2] < SECTORS)

    def need(self):
        if not self.valid():
            raise Bad("not a DOS 3.3 disk (no VTOC it would take)")

    def free(self, t, s):
        v = self.vtoc()
        b = 0x38 + t * 4 + (0 if s >= 8 else 1)
        return bool(v[b] & (1 << (s & 7)))

    def set_free(self, t, s, free):
        v = self.vtoc()
        b = 0x38 + t * 4 + (0 if s >= 8 else 1)
        bit = 1 << (s & 7)
        v[b] = (v[b] | bit) if free else (v[b] & ~bit)
        self.put_sector(*VTOC, v)

    def free_count(self):
        return sum(self.free(t, s) for t in range(TRACKS) for s in range(SECTORS))

    def alloc(self):
        """A free sector, marked used: outward from the catalog's track, as DOS does, 15 down to 0."""
        for t in list(range(18, TRACKS)) + list(range(16, -1, -1)):
            for s in range(SECTORS - 1, -1, -1):
                if self.free(t, s):
                    self.set_free(t, s, False)
                    return t, s
        raise Bad("disk full")

    # the catalog
    def catalog_sectors(self):
        v = self.vtoc()
        t, s, seen = v[1], v[2], set()
        while t:
            if (t, s) in seen:
                raise Bad("the catalog's chain loops at %d/%d" % (t, s))
            seen.add((t, s))
            yield t, s
            c = self.sector(t, s)
            t, s = c[1], c[2]

    def entries(self):
        """Every entry: (catalog t, s, index, its 35 bytes)."""
        for t, s in self.catalog_sectors():
            c = self.sector(t, s)
            for i in range(ENTRIES):
                o = 0x0B + i * ENTRY
                yield t, s, i, bytes(c[o:o + ENTRY])

    def files(self):
        for t, s, i, e in self.entries():
            if e[0] not in (0, 0xFF):
                yield {"name": name_of(e), "type": TYPES.get(e[2] & 0x7F, "?"), "locked": bool(e[2] & 0x80),
                       "sectors": e[0x21] | e[0x22] << 8, "ts": (e[0], e[1]), "at": (t, s, i)}

    def find(self, name):
        for f in self.files():
            if f["name"] == name:
                return f
        return None

    def put_entry(self, t, s, i, e):
        c = self.sector(t, s)
        o = 0x0B + i * ENTRY
        c[o:o + ENTRY] = e
        self.put_sector(t, s, c)

    # files
    def lists(self, f):
        """The file's track/sector lists, and its data sectors in order (None for one never written)."""
        lists, data, (t, s), seen = [], [], f["ts"], set()
        while t:
            if (t, s) in seen:
                raise Bad("%s: its track/sector lists loop at %d/%d" % (f["name"], t, s))
            seen.add((t, s))
            lists.append((t, s))
            l = self.sector(t, s)
            for p in range(PAIRS):
                pt, ps = l[0x0C + p * 2], l[0x0D + p * 2]
                data.append((pt, ps) if (pt or ps) else None)
            t, s = l[1], l[2]
        while data and data[-1] is None:
            data.pop()
        return lists, data

    def raw(self, f):
        _, data = self.lists(f)
        return b"".join(bytes(self.sector(*p)) if p else bytes(SECTOR) for p in data)


def name_of(e):
    n = bytes(b & 0x7F for b in e[3:3 + 30])
    if e[0] == 0xFF:
        n = n[:-1] + b" "
    return n.decode("latin-1").rstrip(" ")


def new(volume=254):
    """An empty DOS 3.3 image: the VTOC, the catalog's 15 sectors on track 17, tracks 0-2 used and empty."""
    d = Disk(bytes(SIZE))
    v = bytearray(SECTOR)
    v[1], v[2], v[3], v[6] = 17, 15, 3, volume
    v[0x27], v[0x30], v[0x31] = PAIRS, 17, 1
    v[0x34], v[0x35], v[0x36], v[0x37] = TRACKS, SECTORS, 0, 1
    for t in range(TRACKS):
        if t not in (0, 1, 2, 17):
            v[0x38 + t * 4], v[0x39 + t * 4] = 0xFF, 0xFF
    d.put_sector(*VTOC, v)
    for s in range(15, 0, -1):
        c = bytearray(SECTOR)
        if s > 1:
            c[1], c[2] = 17, s - 1
        d.put_sector(17, s, c)
    return d


def add(d, name, typ, stored, locked=False):
    """A file of `stored` bytes (as DOS keeps them: B's address and length first) under `name`."""
    d.need()
    if not 0 < len(name) <= 30:
        raise Bad("a name of 1-30 characters")
    if d.find(name):
        raise Bad("%s is there already" % name)
    slot = next(((t, s, i) for t, s, i, e in d.entries() if e[0] in (0, 0xFF)), None)
    if slot is None:
        raise Bad("catalog full")
    n = max(1, (len(stored) + SECTOR - 1) // SECTOR)
    nlists = (n + PAIRS - 1) // PAIRS
    if d.free_count() < n + nlists:
        raise Bad("disk full")
    lists = [d.alloc() for _ in range(nlists)]
    pairs = [d.alloc() for _ in range(n)]
    for k, (t, s) in enumerate(pairs):
        chunk = bytes(stored[k * SECTOR:(k + 1) * SECTOR])
        d.put_sector(t, s, bytearray(chunk.ljust(SECTOR, b"\0")))
    for k, (t, s) in enumerate(lists):
        l = bytearray(SECTOR)
        if k + 1 < nlists:
            l[1], l[2] = lists[k + 1]
        first = k * PAIRS
        l[5], l[6] = first & 0xFF, first >> 8
        for p, (pt, ps) in enumerate(pairs[first:first + PAIRS]):
            l[0x0C + p * 2], l[0x0D + p * 2] = pt, ps
        d.put_sector(t, s, l)
    e = bytearray(ENTRY)
    e[0], e[1] = lists[0]
    e[2] = CODES[typ] | (0x80 if locked else 0)
    e[3:33] = bytes((ord(c) | 0x80) for c in name.ljust(30))
    e[0x21], e[0x22] = (n + nlists) & 0xFF, (n + nlists) >> 8
    d.put_entry(*slot, e)


def erase(d, name):
    f = d.find(name)
    if not f:
        raise Bad("%s isn't there" % name)
    lists, data = d.lists(f)
    for p in lists + [p for p in data if p]:
        d.set_free(*p, True)
    t, s, i = f["at"]
    c = d.sector(t, s)
    o = 0x0B + i * ENTRY
    c[o + 0x20] = c[o]           # the track kept in the name's last byte
    c[o] = 0xFF
    d.put_sector(t, s, c)


def text(data):
    """CP/M text as DOS keeps it: bit 7 set, CR LF to CR, to the first ^Z, a 0 after."""
    out = bytearray()
    for b in data:
        if b == 0x1A:
            break
        if b == 0x0A:
            continue
        out.append(b | 0x80)
    return bytes(out) + b"\0"


def binary(data, address):
    if len(data) > 0xFFFF:
        raise Bad("a B file of 65,535 bytes at most")
    return bytes([address & 0xFF, address >> 8, len(data) & 0xFF, len(data) >> 8]) + bytes(data)


def check(d):
    """Problems, as strings: every sector used by exactly one file, the catalog or the VTOC, or free in the
    bitmap; the catalog's chain and each file's lists without a loop; each file's sector count right."""
    d.need()
    owner, problems = {}, []

    def use(p, who):
        if p in owner:
            problems.append("%d/%d used by %s and %s" % (p[0], p[1], owner[p], who))
        owner[p] = who

    use(VTOC, "the VTOC")
    for t in (0, 1, 2):
        for s in range(SECTORS):
            use((t, s), "DOS's tracks")
    try:
        for p in d.catalog_sectors():
            use(p, "the catalog")
        for f in list(d.files()):
            lists, data = d.lists(f)
            for p in lists:
                use(p, f["name"])
            for p in data:
                if p:
                    use(p, f["name"])
            if f["sectors"] != len(lists) + sum(1 for p in data if p):
                problems.append("%s: %d sectors in the catalog, %d in its lists"
                                % (f["name"], f["sectors"], len(lists) + sum(1 for p in data if p)))
    except Bad as e:
        problems.append(str(e))
    for t in range(TRACKS):
        for s in range(SECTORS):
            used = (t, s) in owner
            if t == 17 and not used:     # DOS keeps track 17 for the catalog
                used = True
            if used == d.free(t, s):
                problems.append("%d/%d %s but %s in the bitmap"
                                % (t, s, "used" if used else "unused", "free" if used else "not free"))
    return problems


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip())
        return 2
    cmd, path = argv[1], argv[2]
    try:
        if cmd == "new":
            open(path, "wb").write(new(int(argv[3]) if len(argv) > 3 else 254).d)
            return 0
        d = Disk(open(path, "rb").read())
        if cmd == "ls":
            d.need()
            for f in d.files():
                print("%s%s %03d %s" % ("*" if f["locked"] else " ", f["type"], f["sectors"] % 1000, f["name"]))
            print("%d sectors free" % d.free_count())
        elif cmd == "put":
            name, typ, data = argv[3], argv[4], open(argv[5], "rb").read()
            stored = text(data) if typ == "T" else binary(data, int(argv[6], 0) if len(argv) > 6 else 0x2000) \
                if typ == "B" else data
            add(d, name, typ, stored)
            open(path, "wb").write(d.d)
        elif cmd == "get":
            d.need()
            f = d.find(argv[3])
            if not f:
                raise Bad("%s isn't there" % argv[3])
            open(argv[4], "wb").write(d.raw(f))
        elif cmd == "era":
            d.need()
            erase(d, argv[3])
            open(path, "wb").write(d.d)
        elif cmd == "lock":
            d.need()
            f = d.find(argv[3])
            if not f:
                raise Bad("%s isn't there" % argv[3])
            t, s, i = f["at"]
            c = d.sector(t, s)
            c[0x0B + i * ENTRY + 2] |= 0x80
            d.put_sector(t, s, c)
            open(path, "wb").write(d.d)
        elif cmd == "check":
            p = check(d)
            for x in p:
                print(x)
            print("ok" if not p else "%d problems" % len(p))
            return 1 if p else 0
        else:
            raise Bad("no such command: " + cmd)
    except Bad as e:
        print("dos33.py: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
