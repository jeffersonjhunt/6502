"""Makes OOLIB's asset files (format 3) from PNG images, and checks them (hw/oolib.md, L4; question 8).

    python3 -I mkasset.py bitmap256 picture.png PICTURE.OGX
    python3 -I mkasset.py bitmap512 picture.png PICTURE.OGX
    python3 -I mkasset.py tiles level.png LEVEL.OGX [--tile 8|16] [--bpp 4|8] [--map 32|64]
    python3 -I mkasset.py sprites ship.png SHIP.OGX --size 16x16 [--bpp 4|8] [--table]
    python3 -I mkasset.py palette colours.png COLOURS.OGX
    python3 -I mkasset.py check PICTURE.OGX

The standard library alone: a PNG is read with zlib (8 bits a channel: greyscale, RGB, RGBA, or indexed at 1,
2, 4 or 8 bits, with its tRNS; not interlaced). Colours become the card's 12 bits (4 a channel, rounded); a
picture with more of them than its bitmap holds (256, or 16 at 4 bits) is refused, unless --quantize keeps the
most used and gives each other pixel the nearest of them. A pixel with alpha below 128 is transparent: colour 0
(the card shows what is under it), and the palette's entry 0 is then kept for it.

The file (oodos/include/oolib.inc's AH_* and AP_*): record 0 a 16-byte header ('OGX', the format 3, the parts,
11 bytes 0) and up to 7 parts of 16 bytes: the type (AT_*), the layer CONFIG it was made for, its unit ($FF:
any, the library allocates), a byte 0, its first record (2 bytes), its records (2), its length in bytes (3),
its width and height as the type counts them, 3 bytes 0. Each part starts on a 1 KB boundary of the file and
is padded to one, so its sectors go to video memory straight from the card (function 83's DM_LOAD) and its last
record stays in its own units. The rest of record 0, and of its 1 KB, is 0.

The card's layouts (Technical Reference chapter 6): bitmap 256 is 256 x 240 at a byte a pixel; bitmap 512 is
512 x 480 at 4 bits, the left pixel high, 256 bytes a row; tiles are 8 x 8 or 16 x 16 at 4 or 8 bits, row by
row; a map is 2-byte entries (the tile's number, bits 0-9; H and V flip, 10 and 11; the palette offset, 12-15);
a sprite image is its pixels row by row, a sprite's entry 8 bytes (its image's address / 64 with bit 15 for 8
bits a pixel, X, Y, the flags, the size); a palette 256 entries of GGGGBBBB then 0000RRRR.
"""

import argparse
import struct
import sys
import zlib

MAGIC = b"OGX"
FORMAT = 3
HEADER = 16
PART = 16
MAX_PARTS = 7
RECORD = 128
UNIT = 1024
AT_MAP, AT_TILES, AT_PALETTE, AT_SPRITES, AT_SPRTABLE, AT_BITMAP, AT_FONT, AT_RAW = range(1, 9)
TYPES = {AT_MAP: "map", AT_TILES: "tiles", AT_PALETTE: "palette", AT_SPRITES: "sprites", AT_SPRTABLE: "sprite table",
         AT_BITMAP: "bitmap", AT_FONT: "font", AT_RAW: "raw"}
# CONFIG's layer types and bits (Technical Reference chapter 6)
TILE, BITMAP256, BITMAP512 = 2, 3, 4
TILE16, TILE8BPP, MAP_W64, MAP_H64 = 0x08, 0x10, 0x20, 0x40
MAX_BYTES = 1 << 17                    # a part's length: 17 bits


class AssetError(Exception):
    pass


# ---- PNG ----------------------------------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(data):
    """(width, height, pixels): pixels a list of rows of (r, g, b, a), 8 bits each."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssetError("not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = None
    while pos + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, comp, filt, lace = struct.unpack(">IIBBBBB", body)
            if comp or filt:
                raise AssetError("an unknown PNG compression or filter method")
            if lace:
                raise AssetError("an interlaced PNG")
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if w is None:
        raise AssetError("a PNG without IHDR")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None:
        raise AssetError("PNG colour type %d" % ctype)
    if ctype != 3 and depth != 8 or ctype == 3 and depth not in (1, 2, 4, 8):
        raise AssetError("PNG bit depth %d (8 bits a channel, or indexed)" % depth)
    if ctype == 3 and plte is None:
        raise AssetError("an indexed PNG without PLTE")
    bits = channels * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 0xFF
            elif f != 0:
                raise AssetError("PNG filter %d" % f)
        prev = line
        row = []
        for x in range(w):
            if ctype == 3:
                at = x * depth
                v = (line[at // 8] >> (8 - depth - at % 8)) & ((1 << depth) - 1)
                r, g, b = plte[v]
                alpha = trns[v] if trns is not None and v < len(trns) else 255
                row.append((r, g, b, alpha))
            elif ctype == 0:
                v = line[x]
                row.append((v, v, v, 255))
            elif ctype == 4:
                v, alpha = line[2 * x], line[2 * x + 1]
                row.append((v, v, v, alpha))
            elif ctype == 2:
                row.append((line[3 * x], line[3 * x + 1], line[3 * x + 2], 255))
            else:
                row.append(tuple(line[4 * x:4 * x + 4]))
        rows.append(row)
    return w, h, rows


def write_png(w, h, rows):
    """An RGBA PNG of `rows` (lists of (r, g, b, a)): for tests and the build's samples."""
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# ---- colours ------------------------------------------------------------------------------------------------

def to12(p):
    """An (r, g, b, a) pixel as the card's 12 bits, 0x0RGB; None if it is transparent."""
    r, g, b, a = p
    if a < 128:
        return None
    return ((r * 15 + 127) // 255) << 8 | ((g * 15 + 127) // 255) << 4 | ((b * 15 + 127) // 255)


def entry_bytes(rgb):
    """A palette entry: GGGGBBBB, then 0000RRRR."""
    return bytes([(rgb >> 4 & 0xF) << 4 | (rgb & 0xF), rgb >> 8 & 0xF])


def palette_of(pixels, limit, quantize):
    """(palette, index): the 12-bit colours the pixels use, at most `limit` (entry 0 kept for transparency if
    any pixel is transparent), and a function from a pixel to its index."""
    counts = {}
    transparent = False
    for p in pixels:
        c = to12(p)
        if c is None:
            transparent = True
        else:
            counts[c] = counts.get(c, 0) + 1
    room = limit - (1 if transparent else 0)
    colours = sorted(counts, key=lambda c: (-counts[c], c))
    if len(colours) > room:
        if not quantize:
            raise AssetError("%d colours, more than the %d it holds (--quantize keeps the most used)" %
                             (len(colours), room))
        colours = colours[:room]
    colours.sort()
    palette = ([0] if transparent else []) + colours
    lookup = {c: i for i, c in enumerate(palette) if not (transparent and i == 0)}
    near = {}

    def index(p):
        c = to12(p)
        if c is None:
            return 0
        if c in lookup:
            return lookup[c]
        if c not in near:
            def dist(k):
                return sum((((c >> s) & 15) - ((k >> s) & 15)) ** 2 for s in (0, 4, 8))
            near[c] = min(lookup.items(), key=lambda kv: (dist(kv[0]), kv[1]))[1]
        return near[c]

    return palette, index


def palette_bytes(palette):
    out = b"".join(entry_bytes(c) for c in palette)
    return out + bytes(512 - len(out))


# ---- the file -----------------------------------------------------------------------------------------------

class Part:
    def __init__(self, kind, data, config=0, unit=0xFF, w=0, h=0):
        if not data:
            raise AssetError("an empty part")
        if len(data) > MAX_BYTES:
            raise AssetError("a part of %d bytes: 131,072 at most" % len(data))
        self.kind, self.data, self.config, self.unit, self.w, self.h = kind, bytes(data), config, unit, w, h


def pad(b, to):
    return b + bytes(-len(b) % to)


def build(parts):
    """The file's bytes: the header and its parts, each on 1 KB of the file."""
    if not 1 <= len(parts) <= MAX_PARTS:
        raise AssetError("%d parts: 1-%d" % (len(parts), MAX_PARTS))
    body, entries = b"", b""
    at = UNIT                                # record 0's 1 KB first
    for p in parts:
        records = (len(p.data) + RECORD - 1) // RECORD
        entries += struct.pack("<BBBBHH", p.kind, p.config, p.unit, 0, at // RECORD, records)
        entries += struct.pack("<I", len(p.data))[:3] + bytes([p.w & 0xFF, p.h & 0xFF, 0, 0, 0])
        chunk = pad(p.data, UNIT)
        body += chunk
        at += len(chunk)
    head = MAGIC + bytes([FORMAT, len(parts)]) + bytes(HEADER - 5) + entries
    return pad(head, UNIT) + body


def parse(data):
    """[(type, config, unit, first record, records, bytes, w, h)], checking the header's limits."""
    if len(data) < RECORD:
        raise AssetError("shorter than a record")
    if data[:3] != MAGIC:
        raise AssetError("no 'OGX'")
    if data[3] != FORMAT:
        raise AssetError("format %d, not 3" % data[3])
    n = data[4]
    if not 1 <= n <= MAX_PARTS:
        raise AssetError("%d parts" % n)
    if any(data[5:HEADER]):
        raise AssetError("the header's reserved bytes aren't 0")
    out = []
    for i in range(n):
        e = data[HEADER + i * PART:HEADER + (i + 1) * PART]
        kind, config, unit, zero, first, records = struct.unpack("<BBBBHH", e[:8])
        size = e[8] | e[9] << 8 | e[10] << 16
        w, h = e[11], e[12]
        if kind not in TYPES:
            raise AssetError("part %d: type %d" % (i + 1, kind))
        if zero or any(e[13:]):
            raise AssetError("part %d: its reserved bytes aren't 0" % (i + 1))
        if first % (UNIT // RECORD) or first == 0:
            raise AssetError("part %d: record %d isn't on 1 KB of the file" % (i + 1, first))
        if not 0 < size <= MAX_BYTES or records != (size + RECORD - 1) // RECORD:
            raise AssetError("part %d: %d bytes in %d records" % (i + 1, size, records))
        if unit != 0xFF and (unit < 13 or unit + (size + UNIT - 1) // UNIT > 256):
            raise AssetError("part %d: unit %d (13-255, and its units with it)" % (i + 1, unit))
        if (first + records) * RECORD > len(data):
            raise AssetError("part %d: past the file's end" % (i + 1))
        out.append((kind, config, unit, first, records, size, w, h))
    starts = sorted(p[3] for p in out)
    ends = {p[3]: p[3] + (p[5] + UNIT - 1) // UNIT * (UNIT // RECORD) for p in out}
    for a, b in zip(starts, starts[1:]):
        if ends[a] > b:
            raise AssetError("parts overlap at record %d" % b)
    return out


def part_bytes(data, part):
    return data[part[3] * RECORD:part[3] * RECORD + part[5]]


# ---- the kinds ----------------------------------------------------------------------------------------------

def bitmap256(png, quantize=False):
    w, h, rows = read_png(png)
    if w > 256 or h > 240:
        raise AssetError("%d x %d: a bitmap 256 is 256 x 240 at most" % (w, h))
    pal, index = palette_of([p for r in rows for p in r], 256, quantize)
    out = bytearray(256 * 240)
    for y in range(h):
        for x in range(w):
            out[y * 256 + x] = index(rows[y][x])
    return [Part(AT_PALETTE, palette_bytes(pal)), Part(AT_BITMAP, out[:256 * h], BITMAP256, w=w // 8, h=h // 8)]


def bitmap512(png, quantize=False):
    w, h, rows = read_png(png)
    if w > 512 or h > 480:
        raise AssetError("%d x %d: a bitmap 512 is 512 x 480 at most" % (w, h))
    pal, index = palette_of([p for r in rows for p in r], 16, quantize)
    out = bytearray(256 * h)
    for y in range(h):
        for x in range(w):
            out[y * 256 + x // 2] |= index(rows[y][x]) << (0 if x & 1 else 4)
    return [Part(AT_PALETTE, palette_bytes(pal)), Part(AT_BITMAP, out, BITMAP512, w=w // 8, h=h // 8)]


def tiles(png, size=8, bpp=4, map_w=32, quantize=False):
    """Tiles (each distinct one once, tile 0 first) and a map of them, and the palette."""
    w, h, rows = read_png(png)
    if w % size or h % size:
        raise AssetError("%d x %d isn't whole %d x %d tiles" % (w, h, size, size))
    cols, lines = w // size, h // size
    map_h = 64 if lines > 32 else 32
    if cols > map_w or lines > 64:
        raise AssetError("%d x %d tiles: the map is %d x %d at most" % (cols, lines, map_w, 64))
    pal, index = palette_of([p for r in rows for p in r], 16 if bpp == 4 else 256, quantize)
    images, seen, entries = [], {}, bytearray(map_w * map_h * 2)
    for ty in range(lines):
        for tx in range(cols):
            px = [index(rows[ty * size + y][tx * size + x]) for y in range(size) for x in range(size)]
            if bpp == 4:
                img = bytes(px[i] << 4 | px[i + 1] for i in range(0, len(px), 2))
            else:
                img = bytes(px)
            if img not in seen:
                seen[img] = len(images)
                images.append(img)
            n = seen[img]
            if n > 1023:
                raise AssetError("more than 1,024 different tiles")
            struct.pack_into("<H", entries, (ty * map_w + tx) * 2, n)
    config = TILE | (TILE16 if size == 16 else 0) | (TILE8BPP if bpp == 8 else 0) | \
        (MAP_W64 if map_w == 64 else 0) | (MAP_H64 if map_h == 64 else 0)
    return [Part(AT_PALETTE, palette_bytes(pal)), Part(AT_TILES, b"".join(images), config, w=len(images) & 0xFF),
            Part(AT_MAP, bytes(entries), config, w=map_w, h=map_h)]


SIZES = {8: 0, 16: 1, 32: 2, 64: 3}


def sprites(png, sw, sh, bpp=4, table=False, quantize=False):
    """The frames of a sheet, left to right and down, each sw x sh, and the palette; with `table`, a sprite
    table whose first entries show them in a row (depth 3, at x = 8 + 4 * sw * n... wrapped, y = 8)."""
    if sw not in SIZES or sh not in SIZES:
        raise AssetError("a sprite is 8, 16, 32 or 64 pixels a side")
    w, h, rows = read_png(png)
    if w % sw or h % sh:
        raise AssetError("%d x %d isn't whole %d x %d sprites" % (w, h, sw, sh))
    pal, index = palette_of([p for r in rows for p in r], 16 if bpp == 4 else 256, quantize)
    frames = []
    for fy in range(h // sh):
        for fx in range(w // sw):
            px = [index(rows[fy * sh + y][fx * sw + x]) for y in range(sh) for x in range(sw)]
            img = bytes(px[i] << 4 | px[i + 1] for i in range(0, len(px), 2)) if bpp == 4 else bytes(px)
            frames.append(pad(img, 64))
    if len(frames) > 64:
        raise AssetError("%d sprites: 64 at most" % len(frames))
    parts = [Part(AT_PALETTE, palette_bytes(pal)), Part(AT_SPRITES, b"".join(frames), w=sw, h=sh)]
    if table:
        t = bytearray(512)
        per = len(frames[0]) // 64
        for n in range(len(frames)):
            image = n * per | (0x8000 if bpp == 8 else 0)
            x, y = 8 + (n * (sw + 4)) % 300, 8 + (n * (sw + 4)) // 300 * (sh + 4)
            struct.pack_into("<HHHBB", t, n * 8, image, x, y, 3 << 2, SIZES[sh] << 6 | SIZES[sw] << 4)
        parts.append(Part(AT_SPRTABLE, bytes(t), w=len(frames)))
    return parts


def palette(png, quantize=False):
    w, h, rows = read_png(png)
    pal, _ = palette_of([p for r in rows for p in r], 256, quantize)
    return [Part(AT_PALETTE, palette_bytes(pal))]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="kind", required=True)
    for k in ("bitmap256", "bitmap512", "tiles", "sprites", "palette"):
        s = sub.add_parser(k)
        s.add_argument("png")
        s.add_argument("out")
        s.add_argument("--quantize", action="store_true")
        if k == "tiles":
            s.add_argument("--tile", type=int, choices=(8, 16), default=8)
            s.add_argument("--bpp", type=int, choices=(4, 8), default=4)
            s.add_argument("--map", type=int, choices=(32, 64), default=32)
        if k == "sprites":
            s.add_argument("--size", required=True)
            s.add_argument("--bpp", type=int, choices=(4, 8), default=4)
            s.add_argument("--table", action="store_true")
    c = sub.add_parser("check")
    c.add_argument("file")
    a = ap.parse_args(argv)
    try:
        if a.kind == "check":
            data = open(a.file, "rb").read()
            for i, p in enumerate(parse(data)):
                unit = "any" if p[2] == 0xFF else str(p[2])
                print("part %d: %s, CONFIG $%02X, unit %s, records %d-%d, %d bytes" %
                      (i + 1, TYPES[p[0]], p[1], unit, p[3], p[3] + p[4] - 1, p[5]))
            return 0
        png = open(a.png, "rb").read()
        if a.kind == "bitmap256":
            parts = bitmap256(png, a.quantize)
        elif a.kind == "bitmap512":
            parts = bitmap512(png, a.quantize)
        elif a.kind == "tiles":
            parts = tiles(png, a.tile, a.bpp, a.map, a.quantize)
        elif a.kind == "sprites":
            sw, _, sh = a.size.partition("x")
            parts = sprites(png, int(sw), int(sh), a.bpp, a.table, a.quantize)
        else:
            parts = palette(png, a.quantize)
        out = build(parts)
        parse(out)
        open(a.out, "wb").write(out)
    except (AssetError, OSError, ValueError, zlib.error) as e:
        print("mkasset: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
