"""GWBASIC.COM and GWBASIC.OVL from the links (hw/oodos.md, "The plan, phase 7").

    python3 -I mkgw.py --com BUILD/gwbasic.res --block 1:BUILD/p1.bin ... -o BUILD

GWBASIC.COM is the resident link's output as it is: OODOS's header and the image from $0300.
GWBASIC.OVL holds the blocks' images, each on a 128-byte record, after a header record:

  +0   "GWB", $1A           (so TYPE stops)
  +4   the format, 1
  +5   the number of blocks, 1-8
  +6   reserved, 0 (2 bytes)
  +8   8 bytes a block: its page (1-8), reserved (0), its first record (2), its records (2), its
       length in bytes (2), each little-endian

The loader (res/loader.s) reads it. Each image is the block's link from $C800, at most 12,288 bytes
(to $F7FF: common memory is the loader's to copy in), and starts with the block's header
(gwbasic.inc's BLK_FIX), checked here: its FARFIX list inside the image.
"""
import argparse
import os
import sys

MAGIC = b"GWB\x1a"
BASE, END = 0xC800, 0xF800
REC = 128


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--com", required=True)
    ap.add_argument("--block", action="append", default=[], help="PAGE:FILE")
    ap.add_argument("-o", required=True, help="the output directory")
    a = ap.parse_args()
    com = open(a.com, "rb").read()
    if com[:4] != b"OOD\x1a":
        sys.exit("mkgw.py: %s has no OODOS header" % a.com)
    blocks = []
    for spec in a.block:
        page, path = spec.split(":", 1)
        page = int(page)
        img = open(path, "rb").read()
        if not 1 <= page <= 8:
            sys.exit("mkgw.py: page %d isn't 1-8" % page)
        if len(img) > END - BASE:
            sys.exit("mkgw.py: P%d is %d bytes, %d over its 12,288" % (page, len(img), len(img) - (END - BASE)))
        fix, nfix = int.from_bytes(img[0:2], "little"), int.from_bytes(img[2:4], "little")
        if nfix and not (BASE + 4 <= fix and fix + 2 * nfix <= BASE + len(img)):
            sys.exit("mkgw.py: P%d's FARFIX list ($%04X, %d) isn't in its image" % (page, fix, nfix))
        blocks.append((page, img))
    if not 1 <= len(blocks) <= 8 or len({p for p, _ in blocks}) != len(blocks):
        sys.exit("mkgw.py: 1-8 blocks, each page once")
    header = bytearray(REC)
    header[0:4] = MAGIC
    header[4] = 1
    header[5] = len(blocks)
    body = bytearray()
    rec = 1
    for i, (page, img) in enumerate(blocks):
        n = (len(img) + REC - 1) // REC
        e = 8 + 8 * i
        header[e] = page
        header[e + 2:e + 4] = rec.to_bytes(2, "little")
        header[e + 4:e + 6] = n.to_bytes(2, "little")
        header[e + 6:e + 8] = len(img).to_bytes(2, "little")
        body += img + bytes(n * REC - len(img))
        rec += n
    open(os.path.join(a.o, "GWBASIC.COM"), "wb").write(com)
    open(os.path.join(a.o, "GWBASIC.OVL"), "wb").write(bytes(header) + bytes(body))


if __name__ == "__main__":
    main()
