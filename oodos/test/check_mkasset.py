"""mkasset.py's own check (hw/oolib.md, L4), run by `make check` as check_tools.py is: PNGs it writes itself
(never the owner's), made into assets and taken apart again, and the header's limits.

    python3 -I check_mkasset.py                # the checks
    python3 -I check_mkasset.py --write DIR    # the samples test_oodos_gfx shows with VIEW: their PNGs and assets
"""

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import mkasset as m  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


def raises(f, what):
    try:
        f()
    except m.AssetError:
        return
    check(False, what + ": not refused")


def rgb12(c):
    """A 12-bit colour as 8-bit (r, g, b, 255), as the card shows it (x 17)."""
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17, 255)


def picture(w, h, colours, seed=1):
    """Rows of pixels from `colours` (12-bit), a pattern that varies by x and y."""
    rows = []
    for y in range(h):
        rows.append([rgb12(colours[(x * 7 + y * 13 + (x * y >> 3) + seed) % len(colours)]) for x in range(w)])
    return rows


def pal_of(data, part):
    b = m.part_bytes(data, part)
    return [(b[2 * i + 1] & 15) << 8 | b[2 * i] for i in range(len(b) // 2)]


def find(parts, kind):
    return [p for p in parts if p[0] == kind][0]


def samples():
    """name: (png bytes, the mkasset kind and its arguments)."""
    c16 = [0x000, 0xF00, 0x0F0, 0x00F, 0xFF0, 0x0FF, 0xF0F, 0xFFF, 0x888, 0x800, 0x080, 0x008, 0x880, 0x088,
           0x808, 0x444]
    c200 = [(i * 37 + 5) & 0xFFF for i in range(200)]
    tile_rows = []
    for y in range(64):                    # 8 x 8 tiles, a few kinds, repeated
        tile_rows.append([rgb12(c16[((x // 8 + y // 8) % 3) * 4 + (x + y) % 4]) for x in range(128)])
    sheet = []
    for y in range(16):
        row = []
        for x in range(64):                # four 16 x 16 frames, transparent about a square
            f = x // 16
            inside = 3 <= x % 16 < 13 and 3 <= y < 13
            row.append(rgb12(c16[1 + f]) if inside else (0, 0, 0, 0))
        sheet.append(row)
    return {
        "VIEW8": (m.write_png(256, 240, picture(256, 240, c200)), ("bitmap256",)),
        "VIEW4": (m.write_png(512, 480, picture(512, 480, c16, 3)), ("bitmap512",)),
        "TILES": (m.write_png(128, 64, tile_rows), ("tiles",)),
        "SHIP": (m.write_png(64, 16, sheet), ("sprites", 16, 16)),
    }


def make(png, args):
    kind = args[0]
    if kind == "bitmap256":
        return m.build(m.bitmap256(png))
    if kind == "bitmap512":
        return m.build(m.bitmap512(png))
    if kind == "tiles":
        return m.build(m.tiles(png))
    return m.build(m.sprites(png, args[1], args[2], table=True))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--write":
        os.makedirs(sys.argv[2], exist_ok=True)
        for name, (png, args) in samples().items():
            open(os.path.join(sys.argv[2], name.lower() + ".png"), "wb").write(png)
            open(os.path.join(sys.argv[2], name + ".OGX"), "wb").write(make(png, args))
        return 0

    s = samples()
    # PNG: what write_png writes, read_png reads.
    w, h, rows = m.read_png(s["VIEW8"][0])
    check((w, h) == (256, 240) and rows == picture(256, 240, [(i * 37 + 5) & 0xFFF for i in range(200)]),
          "a PNG read back")

    # A bitmap 256: each pixel's index names its colour.
    data = make(*s["VIEW8"])
    parts = m.parse(data)
    check(len(data) % 1024 == 0, "the file is whole KB")
    check([p[0] for p in parts] == [m.AT_PALETTE, m.AT_BITMAP], "bitmap 256's parts")
    pal = pal_of(data, find(parts, m.AT_PALETTE))
    bmp = m.part_bytes(data, find(parts, m.AT_BITMAP))
    check(find(parts, m.AT_BITMAP)[1] == m.BITMAP256 and len(bmp) == 256 * 240, "bitmap 256's CONFIG and size")
    ok = all(rgb12(pal[bmp[y * 256 + x]]) == rows[y][x] for y in range(240) for x in range(256))
    check(ok, "bitmap 256 round trip")

    # A bitmap 512 at 4 bits, the left pixel high.
    data = make(*s["VIEW4"])
    parts = m.parse(data)
    pal = pal_of(data, find(parts, m.AT_PALETTE))
    bmp = m.part_bytes(data, find(parts, m.AT_BITMAP))
    _, _, rows4 = m.read_png(s["VIEW4"][0])
    ok = all(rgb12(pal[bmp[y * 256 + x // 2] >> (0 if x & 1 else 4) & 15]) == rows4[y][x]
             for y in range(0, 480, 7) for x in range(512))
    check(ok and len(bmp) == 256 * 480, "bitmap 512 round trip")
    check(find(parts, m.AT_BITMAP)[5] == 122880 and find(parts, m.AT_BITMAP)[4] == 960, "a part past 64 KB")

    # Tiles and a map: the picture made again from them.
    data = make(*s["TILES"])
    parts = m.parse(data)
    pal = pal_of(data, find(parts, m.AT_PALETTE))
    tl = m.part_bytes(data, find(parts, m.AT_TILES))
    mp = m.part_bytes(data, find(parts, m.AT_MAP))
    _, _, trows = m.read_png(s["TILES"][0])
    ok = True
    for y in range(64):
        for x in range(128):
            n = struct.unpack_from("<H", mp, ((y // 8) * 32 + x // 8) * 2)[0] & 1023
            px = (y % 8) * 8 + x % 8
            v = tl[n * 32 + px // 2] >> (0 if px & 1 else 4) & 15
            ok = ok and rgb12(pal[v]) == trows[y][x]
    check(ok, "tiles and map round trip")
    check(len(tl) // 32 < 16, "repeated tiles kept once")

    # Sprites: transparent pixels 0, entry 0 kept; the table's entries name the frames.
    data = make(*s["SHIP"])
    parts = m.parse(data)
    pal = pal_of(data, find(parts, m.AT_PALETTE))
    img = m.part_bytes(data, find(parts, m.AT_SPRITES))
    tab = m.part_bytes(data, find(parts, m.AT_SPRTABLE))
    check(img[0] == 0 and len(img) == 4 * 128, "a sprite's corner transparent, 4 frames of 128 bytes")
    check(rgb12(pal[img[128 + 4 * 8 + 3] >> 4]) == rgb12(0x0F0), "frame 1's inside")
    check([struct.unpack_from("<H", tab, n * 8)[0] for n in range(4)] == [0, 2, 4, 6], "the table's images")
    check(tab[6] == 3 << 2 and tab[7] == (1 << 6 | 1 << 4), "the table's depth and size")

    # The limits.
    big = m.write_png(257, 10, picture(257, 10, [0x111]))
    raises(lambda: m.bitmap256(big), "a bitmap 256 wider than 256")
    many = m.write_png(32, 32, picture(32, 32, [i * 3 for i in range(300)]))
    raises(lambda: m.bitmap256(many), "300 colours in 256")
    check(len(m.palette_of([rgb12(i * 3) for i in range(300)], 256, True)[0]) == 256, "--quantize keeps 256")
    raises(lambda: m.bitmap512(m.write_png(8, 8, picture(8, 8, list(range(1, 40))))), "39 colours at 4 bits")
    raises(lambda: m.build([m.Part(m.AT_RAW, b"x")] * 8), "eight parts")
    raises(lambda: m.Part(m.AT_RAW, bytes(131073)), "a part past 17 bits")
    good = m.build([m.Part(m.AT_RAW, b"abc")])
    raises(lambda: m.parse(b"OGY" + good[3:]), "no magic")
    raises(lambda: m.parse(good[:3] + b"\x02" + good[4:]), "format 2")
    bad = bytearray(good)
    bad[HEADER_FIRST] = 3                  # the part's record: not on 1 KB
    raises(lambda: m.parse(bytes(bad)), "a part off 1 KB")
    bad = bytearray(good)
    bad[m.HEADER + 2] = 12                 # unit 12: the console's
    raises(lambda: m.parse(bytes(bad)), "unit 12")
    raises(lambda: m.read_png(b"not a png"), "not a PNG")
    check(m.parse(good)[0][2] == 0xFF and m.part_bytes(good, m.parse(good)[0]) == b"abc", "a raw part")

    print("mkasset: %d failure(s)" % failures)
    return 1 if failures else 0


HEADER_FIRST = m.HEADER + 4

if __name__ == "__main__":
    sys.exit(main())
