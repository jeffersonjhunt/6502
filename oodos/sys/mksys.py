"""Makes OODOS.SYS from ld65's outputs, and reads one as OOLDR will (hw/oodos.md, "The system, phase 2").

    python3 -I mksys.py -o OODOS.SYS --entry COLD --labels oodos.lbl \\
        0xF0000:oodos.bdos 0xF4000:oodos.bios 0xF8800:oodos.res 0xFB800:oodos.common
    python3 -I mksys.py --check OODOS.SYS          # its header and segments, listed

Each PHYSICAL:FILE is a segment: FILE's bytes at that physical address. The file is a 128-byte
header, then each segment in the order given, from the start of a record, the last padded with
zeros to a whole record:

  +0   "OOSY"
  +4   OODOS's version, major then minor (--version, system.inc's by default)
  +6   the cold boot's entry, a CPU address in block 62 (--entry: a number, or a label in --labels)
  +8   the number of segments, 1-8
  +9   reserved, 0, to +15
  +16  8 bytes a segment: its physical address (3), length (2), the record it starts at (2), 0

Refused, with why: no segments or more than 8; an empty segment or one over 16 KB; one not wholly
inside block 31 or blocks 60-62; two that overlap; an entry outside block 62's $C800-$FFFF or in no
segment there; a version over 255. --check reads a file by OOLDR's rules (oodos/ldr/ooldr.s), no
more and no fewer: those, and no OOSY, a major version it doesn't know, and a segment that starts in
the header (record 0) or runs past the file's end. The reserved bytes (+9-+15, and each segment's
last) are written 0 and looked at by neither, so a later minor version may use them.
"""

import argparse
import re
import sys

MAGIC = b"OOSY"
RECORD = 128
HEADER = 128
SEGMENT = 8
MAX_SEGMENTS = 8
MAX_LENGTH = 16 * 1024
BLOCK = 16 * 1024
ALLOWED = ((31 * BLOCK, 32 * BLOCK), (60 * BLOCK, 63 * BLOCK))  # block 31; blocks 60-62
RES_BLOCK = 62
ENTRY_LOW, ENTRY_HIGH = 0xC800, 0xFFFF  # block 62 in W3, past the I/O hole
VERSION = (0, 2)  # system.inc's SYS_MAJOR, SYS_MINOR


class Refused(Exception):
    pass


def records(n):
    return (n + RECORD - 1) // RECORD


def check_segments(segments, entry):
    """segments: [(physical, length)]. Refuses what OOLDR refuses."""
    if not 1 <= len(segments) <= MAX_SEGMENTS:
        raise Refused("%d segments: 1 to %d" % (len(segments), MAX_SEGMENTS))
    for i, (phys, length) in enumerate(segments):
        what = "segment %d ($%05X, %d bytes)" % (i + 1, phys, length)
        if length == 0:
            raise Refused("%s is empty" % what)
        if length > MAX_LENGTH:
            raise Refused("%s is over 16 KB" % what)
        if not any(lo <= phys and phys + length <= hi for lo, hi in ALLOWED):
            raise Refused("%s is not inside block 31 or blocks 60-62" % what)
    order = sorted(range(len(segments)), key=lambda i: segments[i][0])
    for a, b in zip(order, order[1:]):
        if segments[a][0] + segments[a][1] > segments[b][0]:
            raise Refused("segments %d and %d overlap" % (a + 1, b + 1))
    if not ENTRY_LOW <= entry <= ENTRY_HIGH:
        raise Refused("the entry $%04X is not in block 62's $C800-$FFFF" % entry)
    target = RES_BLOCK * BLOCK + (entry - 0xC000)
    if not any(phys <= target < phys + length for phys, length in segments):
        raise Refused("the entry $%04X ($%05X) is in no segment" % (entry, target))


def make(parts, entry, version=VERSION):
    """parts: [(physical, bytes)]. Returns OODOS.SYS's bytes."""
    if not all(0 <= v <= 255 for v in version):
        raise Refused("the version %d.%d: each part 0-255" % version)
    check_segments([(phys, len(data)) for phys, data in parts], entry)
    header = bytearray(HEADER)
    header[0:4] = MAGIC
    header[4], header[5] = version
    header[6:8] = entry.to_bytes(2, "little")
    header[8] = len(parts)
    body = bytearray()
    record = HEADER // RECORD
    for i, (phys, data) in enumerate(parts):
        at = 16 + i * SEGMENT
        header[at:at + 3] = phys.to_bytes(3, "little")
        header[at + 3:at + 5] = len(data).to_bytes(2, "little")
        header[at + 5:at + 7] = record.to_bytes(2, "little")
        body += data + bytes(records(len(data)) * RECORD - len(data))
        record += records(len(data))
    return bytes(header + body)


def read(data, major=VERSION[0]):
    """OODOS.SYS's bytes as OOLDR reads them: (version, entry, [(physical, bytes)])."""
    if len(data) < HEADER or data[0:4] != MAGIC:
        raise Refused("not OODOS.SYS: no OOSY")
    if data[4] != major:
        raise Refused("version %d.%d: OOLDR knows major %d" % (data[4], data[5], major))
    count = data[8]
    if not 1 <= count <= MAX_SEGMENTS:
        raise Refused("%d segments: 1 to %d" % (count, MAX_SEGMENTS))
    entry = int.from_bytes(data[6:8], "little")
    parts = []
    for i in range(count):
        at = 16 + i * SEGMENT
        phys = int.from_bytes(data[at:at + 3], "little")
        length = int.from_bytes(data[at + 3:at + 5], "little")
        record = int.from_bytes(data[at + 5:at + 7], "little")
        start = record * RECORD
        if record == 0:
            raise Refused("segment %d starts in the header" % (i + 1))
        if start + length > len(data):
            raise Refused("segment %d's bytes aren't in the file" % (i + 1))
        parts.append((phys, data[start:start + length]))
    check_segments([(phys, len(b)) for phys, b in parts], entry)
    return (data[4], data[5]), entry, parts


def label(lblfile, name):
    """An ld65 -Ln label's value."""
    for line in open(lblfile):
        m = re.match(r"al\s+([0-9A-Fa-f]+)\s+\.(\S+)", line)
        if m and m.group(2) == name:
            return int(m.group(1), 16)
    raise Refused("no label %s in %s" % (name, lblfile))


def main():
    ap = argparse.ArgumentParser(description="Make OODOS.SYS from ld65's outputs, or check one.")
    ap.add_argument("segments", nargs="*", metavar="PHYSICAL:FILE")
    ap.add_argument("-o", "--output")
    ap.add_argument("--entry", help="the cold boot: an address, or a label in --labels")
    ap.add_argument("--labels", help="ld65's -Ln file")
    ap.add_argument("--version", default="%d.%d" % VERSION, help="major.minor (%(default)s)")
    ap.add_argument("--check", metavar="FILE", help="read FILE as OOLDR will, and list it")
    a = ap.parse_args()
    try:
        if a.check:
            version, entry, parts = read(open(a.check, "rb").read())
            print("OODOS.SYS %d.%d, entry $%04X" % (version + (entry,)))
            for phys, data in parts:
                print("  $%05X-$%05X  %5d bytes  block %d" % (phys, phys + len(data) - 1, len(data), phys // BLOCK))
            return 0
        if not a.output or not a.entry or not a.segments:
            ap.error("give -o, --entry and the segments, or --check")
        try:
            entry = int(a.entry, 0)
        except ValueError:
            if not a.labels:
                ap.error("--entry %s needs --labels" % a.entry)
            entry = label(a.labels, a.entry)
        parts = []
        for spec in a.segments:
            phys, _, path = spec.partition(":")
            if not path:
                ap.error("%s: PHYSICAL:FILE" % spec)
            parts.append((int(phys, 0), open(path, "rb").read()))
        major, _, minor = a.version.partition(".")
        out = make(parts, entry, (int(major), int(minor or 0)))
    except Refused as e:
        print("mksys: %s" % e, file=sys.stderr)
        return 1
    with open(a.output, "wb") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
