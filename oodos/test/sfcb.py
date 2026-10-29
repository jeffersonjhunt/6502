"""Reads an OODOS drive's SFCB date stamps, for interop.sh: on its own, not trusting fsck.cpm's check.

    python3 -I sfcb.py IMAGE ENTRIES check ALLOWED   # every file entry's stamps, in order
    python3 -I sfcb.py IMAGE ENTRIES slots U NAME.EXT  # each entry of U:NAME.EXT: its slot's 8 stamp bytes
    python3 -I sfcb.py stamp 2026-10-26 15:30         # a stamp's 4 bytes, as hex

The directory is ENTRIES entries of 32 bytes from the image's start (no boot tracks). A file entry's
stamps are in the SFCB in entry i | 3, slot i & 3: 10 bytes from 1 + slot x 10, creation (or access)
then update, each 2 bytes of days (1 January 1978 is day 1), then hour and minute in BCD.

CP/M 3 keeps a file's stamps in its extent-zero entry's slot only (the entry of its lowest extent here);
OODOS's BDOS leaves a later extent's slot 0, cpmtools stamps it as the first. check fails if a file's
first entry has no SFCB, or an entry's update is before its creation (a later extent's slot may be
all 0). fsck.cpm 2.23 compares an SFCB's first slot backwards, so it warns when the update is on a
later day than the creation; check writes that warning, for exactly those entries, to ALLOWED, the
lines fsck_clean lets through.
"""

import datetime
import sys


def stamp(day, hhmm):
    days = (datetime.date.fromisoformat(day) - datetime.date(1977, 12, 31)).days
    h, m = (int(x) for x in hhmm.split(":"))
    return bytes([days & 0xFF, days >> 8, h // 10 << 4 | h % 10, m // 10 << 4 | m % 10])


def when(s):
    """A stamp, as (days, hour, minute), comparable."""
    return (s[0] | s[1] << 8, s[2], s[3])


def entries(image, n):
    with open(image, "rb") as f:
        d = f.read(n * 32)
    return [d[i * 32:(i + 1) * 32] for i in range(n)]


def slot(dir, i):
    sfcb = dir[i | 3]
    if sfcb[0] != 0x21:
        return None
    return sfcb[1 + (i & 3) * 10:1 + (i & 3) * 10 + 8]


def main():
    a = sys.argv[1:]
    if a[0] == "stamp":
        print(stamp(a[1], a[2]).hex())
        return 0
    image, n, cmd = a[0], int(a[1]), a[2]
    dir = entries(image, n)
    files = [i for i in range(n) if dir[i][0] <= 15]
    if cmd == "check":
        allowed, bad, zero = [], 0, 0
        first = {}
        for i in files:
            key, ext = dir[i][:12], (dir[i][14] & 0x3F) << 5 | dir[i][12] & 0x1F
            if key not in first or ext < first[key][0]:
                first[key] = (ext, i)
        firsts = {i for _, i in first.values()}
        for i in files:
            s = slot(dir, i)
            if s is None:
                if i in firsts:
                    print("entry %d: no SFCB in entry %d" % (i, i | 3))
                    bad += 1
                continue
            if i not in firsts and s == bytes(8):
                zero += 1
                continue
            made, updated = s[:4], s[4:]
            if when(updated) < when(made):
                print("entry %d: updated %s before it was made %s" % (i, updated.hex(), made.hex()))
                bad += 1
            if i & 3 == 0 and when(updated)[0] > when(made)[0]:
                allowed.append("Warning: Modification date earlier than creation date (extent=%d/%d)" % (i | 3, i))
        with open(a[3], "w") as f:
            f.write("".join(l + "\n" for l in allowed))
        print("%d file entries, their stamps in order (%d later extents' slots 0); %d first-slot entries a day "
              "or more apart" % (len(files), zero, len(allowed)))
        return 1 if bad else 0
    if cmd == "slots":
        user, name = int(a[3]), a[4].split(".")
        want = (name[0].ljust(8) + name[1].ljust(3)).encode("ascii")
        for i in files:
            e = dir[i]
            if e[0] == user and bytes(c & 0x7F for c in e[1:12]) == want:
                s = slot(dir, i)
                print("%d %s" % (i, s.hex() if s is not None else "none"))
        return 0
    raise SystemExit("sfcb.py: no command %s" % cmd)


if __name__ == "__main__":
    sys.exit(main())
