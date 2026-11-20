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
    c4096 = list(range(4096))
    photo = picture(640, 480, c4096, 5)
    for y in range(150, 250):              # a transparent square
        for x in range(200, 300):
            photo[y][x] = (0, 0, 0, 0)
    return {
        "VIEW8": (m.write_png(256, 240, picture(256, 240, c200)), ("bitmap256",)),
        "VIEW640": (m.write_png(640, 480, picture(640, 480, c200, 2)), ("bitmap640",)),
        "VIEW16": (m.write_png(640, 480, photo), ("direct640",)),
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
    if kind == "bitmap640":
        return m.build(m.bitmap640(png))
    if kind == "direct640":
        return m.build(m.direct640(png))
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

    # A bitmap 640 (revision 2's type 6): rows 1,024 bytes apart, 640 and 1,024 wide, each pixel's entry its colour.
    c200 = [(i * 37 + 5) & 0xFFF for i in range(200)]
    for w, png in ((640, s["VIEW640"][0]), (1024, m.write_png(1024, 480, picture(1024, 480, c200, 4)))):
        data = m.build(m.bitmap640(png))
        parts = m.parse(data)
        _, _, rows6 = m.read_png(png)
        bmp6 = find(parts, m.AT_BITMAP)
        pal = pal_of(data, find(parts, m.AT_PALETTE))
        b = m.part_bytes(data, bmp6)
        check([p[0] for p in parts] == [m.AT_PALETTE, m.AT_BITMAP], "bitmap 640's parts (%d wide)" % w)
        check(bmp6[1] == m.BITMAP640 and bmp6[5] == 491520 and (bmp6[6], bmp6[7]) == (w // 8, 60),
              "bitmap 640's CONFIG 6, 491,520 bytes, AP_W %d, AP_H 60" % (w // 8))
        ok = all(rgb12(pal[b[y * 1024 + x]]) == rows6[y][x] for y in range(480) for x in range(w))
        check(ok, "bitmap 640 round trip, rows 1,024 apart (%d wide)" % w)
        check(not any(b[y * 1024 + x] for y in range(480) for x in range(w, 1024)), "bitmap 640: past the width 0")

    # A direct 640 (type 7): every pixel its 12 bits, low byte first, bits 12-14 0; the square $8000; no palette.
    data = make(*s["VIEW16"])
    parts = m.parse(data)
    _, _, rows7 = m.read_png(s["VIEW16"][0])
    check([p[0] for p in parts] == [m.AT_BITMAP] and parts[0][1] == m.DIRECT640 and parts[0][5] == 614400 and
          (parts[0][6], parts[0][7]) == (80, 60), "direct 640: one part, CONFIG 7, 614,400 bytes, no palette")
    d = m.part_bytes(data, parts[0])

    def want7(p):
        c = m.to12(p)
        return 0x8000 if c is None else c
    ok = all(struct.unpack_from("<H", d, y * 1280 + 2 * x)[0] == want7(rows7[y][x]) for y in range(480)
             for x in range(640))
    check(ok, "direct 640 round trip, rows 1,280 apart")
    check(struct.unpack_from("<H", d, 200 * 1280 + 2 * 250)[0] == 0x8000, "direct 640: the square transparent")
    check(len({m.to12(c) for row in rows7 for c in row if c[3]}) == 4096, "direct 640: all 4,096 colours")
    black = m.build(m.direct640(m.write_png(2, 1, [[(0, 0, 0, 255), (255, 0, 0, 255)]])))
    check(m.part_bytes(black, m.parse(black)[0])[:4] == bytes([0, 0, 0, 0x0F]), "direct 640: black $0000, red $0F00")
    check(m.part_bytes(black, m.parse(black)[0])[4:6] == bytes([0, 0x80]), "direct 640: past the width $8000")

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
    # (revision 2: each 128-byte frame on a card page of its own, the table counting pages)
    check(img[0] == 0 and len(img) == 4 * 256 and not any(img[128:256]),
          "a sprite's corner transparent, 4 frames of 128 bytes, each on 256")
    check(rgb12(pal[img[256 + 4 * 8 + 3] >> 4]) == rgb12(0x0F0), "frame 1's inside")
    check([struct.unpack_from("<H", tab, n * 8)[0] for n in range(4)] == [0, 1, 2, 3], "the table's images, in pages")
    check(tab[6] == 3 << 2 and tab[7] == (1 << 6 | 1 << 4), "the table's depth and size")

    # --full (hw/sprites640.md): each entry's X and Y in 640 x 480 (x wrapped at 600), byte 3 bit 7 set, its bits
    # 6-2 and byte 5's bits 7-2 0, the other bytes as without it. Without --full the table is as it always was:
    # the placement written out again here as mkasset had it before --full, for sheets of 4, 64 and 16 frames.
    def table_of(parts):
        return m.part_bytes(m.build(parts), find(m.parse(m.build(parts)), m.AT_SPRTABLE))

    def doubled_table(frames, sw, sh, bpp):
        t, per = bytearray(512), max(1, (sw * sh * bpp // 8 + 255) // 256)
        for n in range(frames):
            x, y = 8 + (n * (sw + 4)) % 300, 8 + (n * (sw + 4)) // 300 * (sh + 4)
            struct.pack_into("<HHHBB", t, n * 8, n * per | (0x8000 if bpp == 8 else 0), x, y, 3 << 2,
                             m.SIZES[sh] << 6 | m.SIZES[sw] << 4)
        return bytes(t)

    c15 = [(i * 273) & 0xFFF for i in range(1, 16)]
    many = [[rgb12(c15[(x // 8 + y // 8) % 15]) for x in range(64)] for y in range(64)]  # 64 frames of 8 x 8
    big = [[rgb12(c15[(x // 64 + y) % 15]) for x in range(1024)] for y in range(64)]    # 16 of 64 x 64
    for png, sw, sh, bpp, frames in ((s["SHIP"][0], 16, 16, 4, 4), (m.write_png(64, 64, many), 8, 8, 4, 64),
                                     (m.write_png(1024, 64, big), 64, 64, 8, 16)):
        plain = table_of(m.sprites(png, sw, sh, bpp, table=True))
        check(plain == doubled_table(frames, sw, sh, bpp), "%d x %d: without --full, the table as before" % (sw, sh))
        full = table_of(m.sprites(png, sw, sh, bpp, table=True, full=True))
        ok = True
        for n in range(64):
            e, f = plain[n * 8:n * 8 + 8], full[n * 8:n * 8 + 8]
            if n >= frames:
                ok = ok and f == bytes(8)
                continue
            x, y = f[2] | (f[3] & 3) << 8, f[4] | (f[5] & 3) << 8
            ok = ok and f[3] & 0x80 and not f[3] & 0x7C and not f[5] & 0xFC
            ok = ok and x == 8 + (n * (sw + 4)) % 600 and y == 8 + (n * (sw + 4)) // 600 * (sh + 4)
            ok = ok and f[0:2] == e[0:2] and f[6:8] == e[6:8]
        check(ok, "%d x %d: --full's entries at 640 x 480 with byte 3 bit 7" % (sw, sh))
    raises(lambda: m.sprites(s["SHIP"][0], 16, 16, full=True), "--full without --table")

    # combine: the parts of several files in one, in order; a palette that differs kept, a repeated one once.
    tiles_f, ship_f = make(*s["TILES"]), make(*s["SHIP"])
    src = [(tiles_f, p) for p in m.parse(tiles_f)] + [(ship_f, p) for p in m.parse(ship_f)]
    parts, pals = m.combine([tiles_f, ship_f])
    both = m.build(parts)
    got = m.parse(both)
    check([g[0] for g in got] == [p[0] for _, p in src] and pals == 2,
          "combine: every part in order, the two different palettes kept")
    check(all(m.part_bytes(both, g) == m.part_bytes(f, p) and g[1:3] == p[1:3] and g[5:] == p[5:]
              for g, (f, p) in zip(got, src)), "combine: each part's bytes, CONFIG, page, size and width kept")
    parts, pals = m.combine([tiles_f, tiles_f])
    check([p.kind for p in parts] == [m.AT_PALETTE, m.AT_TILES, m.AT_MAP, m.AT_TILES, m.AT_MAP] and pals == 1,
          "combine: a palette equal to one taken left out")
    raises(lambda: m.combine([ship_f, ship_f]), "combine: two sprite images parts")
    table_only = m.build([p for p in m.sprites(s["SHIP"][0], 8, 8, table=True) if p.kind == m.AT_SPRTABLE])
    raises(lambda: m.combine([table_only]), "combine: a sprite table without its images")
    at = m.build([m.Part(m.AT_RAW, bytes(600), unit=0x0400)])
    raises(lambda: m.combine([at, at]), "combine: two parts at the same card page")
    nxt = m.build([m.Part(m.AT_RAW, bytes(600), unit=0x0404)])
    check(len(m.combine([at, nxt])[0]) == 2, "combine: parts at card pages one after the other")
    raises(lambda: m.combine([tiles_f, tiles_f, ship_f]), "combine: eight parts")
    view16 = make(*s["VIEW16"])
    parts, pals = m.combine([view16, ship_f])
    got = m.parse(m.build(parts))
    check([g[0] for g in got] == [m.AT_BITMAP, m.AT_PALETTE, m.AT_SPRITES, m.AT_SPRTABLE] and got[0][5] == 614400 and
          m.part_bytes(m.build(parts), got[0]) == m.part_bytes(view16, m.parse(view16)[0]),
          "combine: a direct 640 and a sprites file, every part in order")

    # The limits.
    big = m.write_png(257, 10, picture(257, 10, [0x111]))
    raises(lambda: m.bitmap256(big), "a bitmap 256 wider than 256")
    many = m.write_png(32, 32, picture(32, 32, [i * 3 for i in range(300)]))
    raises(lambda: m.bitmap256(many), "300 colours in 256")
    raises(lambda: m.bitmap640(m.write_png(1025, 480, picture(1025, 480, [0x111]))), "a bitmap 640 wider than 1,024")
    raises(lambda: m.direct640(m.write_png(641, 480, picture(641, 480, [0x111]))), "a direct 640 wider than 640")
    raises(lambda: m.direct640(m.write_png(8, 481, picture(8, 481, [0x111]))), "a direct 640 taller than 480")
    raises(lambda: m.bitmap640(many), "300 colours in a bitmap 640")
    q = m.bitmap640(many, quantize=True)
    qpal = [(q[0].data[2 * i + 1] & 15) << 8 | q[0].data[2 * i] for i in range(256)]
    _, _, mrows = m.read_png(many)

    def nearest(c):
        return min(range(256), key=lambda i: (sum((((c >> k) & 15) - ((qpal[i] >> k) & 15)) ** 2 for k in (0, 4, 8)), i))
    check(all(q[1].data[y * 1024 + x] == nearest(m.to12(mrows[y][x])) for y in range(32) for x in range(32)),
          "a bitmap 640 with --quantize: each pixel the nearest kept")
    check(len(m.palette_of([rgb12(i * 3) for i in range(300)], 256, True)[0]) == 256, "--quantize keeps 256")
    raises(lambda: m.bitmap512(m.write_png(8, 8, picture(8, 8, list(range(1, 40))))), "39 colours at 4 bits")
    raises(lambda: m.build([m.Part(m.AT_RAW, b"x")] * 8), "eight parts")
    raises(lambda: m.Part(m.AT_RAW, bytes(1572865)), "a part past the program's card memory")
    exact = m.build([m.Part(m.AT_RAW, bytes(1572864))])  # the limit itself (review round 1, F3)
    check(m.parse(exact)[0][5] == 1572864 and m.parse(exact)[0][4] == 12288, "a part of exactly 1,572,864 bytes taken")
    check(len(m.Part(m.AT_RAW, bytes(131073)).data) == 131073, "a part past 17 bits taken")
    check(m.parse(m.build([m.Part(m.AT_RAW, bytes(614400))]))[0][5] == 614400, "a part of 614,400 bytes taken")
    good = m.build([m.Part(m.AT_RAW, b"abc")])
    raises(lambda: m.parse(b"OGY" + good[3:]), "no magic")
    raises(lambda: m.parse(good[:3] + b"\x02" + good[4:]), "format 2")
    raises(lambda: m.parse(good[:3] + b"\x03" + good[4:]), "format 3 (revision 1's units)")
    bad = bytearray(good)
    bad[HEADER_FIRST] = 3                  # the part's record: not on 1 KB
    raises(lambda: m.parse(bytes(bad)), "a part off 1 KB")
    for page, why in ((0x0000, "page $0000: OODOS's"), (0x03FC, "page $03FC: OODOS's"),
                      (0x0401, "page $0401: not on a unit"), (0x1C00, "page $1C00: Apple II mode's")):
        bad = bytearray(good)
        struct.pack_into("<H", bad, m.HEADER + 2, page)
        raises(lambda: m.parse(bytes(bad)), why)
    ok = bytearray(good)
    struct.pack_into("<H", ok, m.HEADER + 2, 0x1BFC)  # the program's last unit: 3 bytes fit
    check(m.parse(bytes(ok))[0][2] == 0x1BFC, "page $1BFC, the program's last unit")
    raises(lambda: m.read_png(b"not a png"), "not a PNG")
    # Malformed PNGs (review F7), each built here from a good one: refused as AssetError, never a traceback.
    import zlib as z

    def chunk(kind, body, crc=None):
        c = z.crc32(kind + body) & 0xFFFFFFFF if crc is None else crc
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", c)

    def png(ihdr, idat, plte=None, extra=b""):
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + (chunk(b"PLTE", plte) if plte else b"") + extra +
                chunk(b"IDAT", idat) + chunk(b"IEND", b""))

    hdr8x8 = struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)
    raises(lambda: m.read_png(png(hdr8x8, z.compress(bytes(4 * 25)))), "an IDAT short of the height")
    idx = struct.pack(">IIBBBBB", 2, 1, 8, 3, 0, 0, 0)
    raises(lambda: m.read_png(png(idx, z.compress(b"\x00\x00\x05"), plte=bytes(6))), "an index past PLTE")
    raises(lambda: m.read_png(png(hdr8x8[:10], z.compress(bytes(8 * 25)))), "an IHDR of 10 bytes")
    good8 = png(hdr8x8, z.compress(bytes(8 * 25)))
    check(m.read_png(good8)[:2] == (8, 8), "the good 8 x 8 reads")
    bad = bytearray(good8)
    bad[8 + 8 + 13] ^= 1                   # IHDR's CRC
    raises(lambda: m.read_png(bytes(bad)), "a wrong CRC")
    raises(lambda: m.read_png(good8[:len(good8) - 20]), "a chunk cut short")
    bomb = png(struct.pack(">IIBBBBB", 2048, 2048, 8, 6, 0, 0, 0), z.compress(bytes(1 << 20)))
    raises(lambda: m.read_png(bomb), "image data short of 2048 x 2048 (no decompression past it)")
    raises(lambda: m.read_png(png(struct.pack(">IIBBBBB", 4096, 4096, 8, 6, 0, 0, 0), b"")), "4096 x 4096")
    rc = m.main(["check", os.devnull])
    check(rc == 1, "check of an empty file: exit 1")
    check(m.parse(good)[0][2] == m.ANY and m.part_bytes(good, m.parse(good)[0]) == b"abc", "a raw part")

    print("mkasset: %d failure(s)" % failures)
    return 1 if failures else 0


HEADER_FIRST = m.HEADER + 4

if __name__ == "__main__":
    sys.exit(main())
