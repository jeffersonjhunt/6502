#!/usr/bin/env python3
"""Make the video demo's graphics (demo.s): tiles, map, a ball, palettes.

  make_assets.py OUTDIR

Writes, in the video card's formats (hw/design.md, "Formats in video memory"):

  tiles.bin     16 tiles, 8x8 at 4 bits (32 bytes each, left pixel high)
  map.bin       64 x 32 map entries (2 bytes: tile, then palette offset 0 and no flips)
  ball.bin      a 16x16 ball at 4 bits: 1 dark rim, 2 body, 3 highlight
  palettes.bin  16 fade levels x 5 slices x 16 colours x 2 bytes (GGGGBBBB, 0000RRRR)

The landscape: a night sky in three bands with stars, a moon, rolling hills
whose shape repeats every 64 tiles (so the scroll wraps without a seam), and
a brick ground. Slice 0 is the scene and the caption; slices 1-4 the balls.
Deterministic: run again, get the same bytes.
"""

import math
import os
import random
import sys

# Slice 0 (colour 0 is transparent).
SCENE = {
    1: 0x124, 2: 0x236, 3: 0x358,       # sky, top to horizon
    4: 0xEEF,                           # stars
    5: 0x143, 6: 0x3A5,                 # hill body, hill top
    7: 0xA52, 8: 0x631,                 # brick, mortar
    9: 0xFEC,                           # moon
    14: 0xFE5, 15: 0xFFF,               # caption: yellow, white
}
BALLS = [  # dark, body, highlight
    (0x600, 0xD22, 0xFAA),
    (0x050, 0x3C3, 0xAFA),
    (0x015, 0x36E, 0xAEF),
    (0x640, 0xEB2, 0xFEA),
]

SKY = [1, 2, 3]
T_SKY = [1, 2, 3]                       # solid sky tiles, one per band
T_STAR = [4, 5, 6]                      # a star on each band
T_HILL, T_HILLTOP, T_BRICK, T_MOON_L, T_MOON_R = 7, 8, 9, 10, 11


def tile(pixels):
    """pixels: 8 rows of 8 values -> 32 bytes."""
    out = bytearray()
    for row in pixels:
        for x in range(0, 8, 2):
            out.append(row[x] << 4 | row[x + 1])
    return out


def solid(c):
    return [[c] * 8 for _ in range(8)]


def tiles():
    t = [solid(0) for _ in range(16)]
    for band, c in enumerate(SKY):
        t[T_SKY[band]] = solid(c)
        star = solid(c)
        star[3][4] = 4
        star[2][4] = star[4][4] = star[3][3] = star[3][5] = 4 if band == 0 else c
        t[T_STAR[band]] = star
    t[T_HILL] = solid(5)
    top = solid(5)
    for x in range(8):
        edge = 1 if 2 <= x <= 5 else 2   # rounded: higher in the middle
        for y in range(edge):
            top[y][x] = 3                # the horizon band behind it
        top[edge][x] = 6                 # the lit top
    t[T_HILLTOP] = top
    brick = solid(7)
    for x in range(8):
        brick[3][x] = brick[7][x] = 8
    for y in range(3):
        brick[y][2] = 8
    for y in range(4, 7):
        brick[y][6] = 8
    t[T_BRICK] = brick
    for half, idx in ((0, T_MOON_L), (1, T_MOON_R)):
        m = solid(1)
        for y in range(8):
            for x in range(8):
                dx, dy = x + 8 * half - 7.5, y - 3.5
                if dx * dx + dy * dy * 1.6 < 30:
                    m[y][x] = 9
        t[idx] = m
    return b"".join(tile(p) for p in t)


def heights():
    """Hill top row per map column (0 = top), periodic over 64 columns."""
    return [round(20 + 2.5 * math.sin(2 * math.pi * x / 64) + 1.5 * math.sin(2 * math.pi * 3 * x / 64 + 1)) for x in range(64)]


def map_():
    rnd = random.Random(1)
    out = bytearray()
    h = heights()
    for y in range(32):
        for x in range(64):
            if y >= 28:
                t = T_BRICK
            elif y > h[x]:
                t = T_HILL
            elif y == h[x]:
                t = T_HILLTOP
            elif y == 3 and x in (40, 41):
                t = T_MOON_L if x == 40 else T_MOON_R
            else:
                band = min(2, y // 7)
                t = T_STAR[band] if band == 0 and rnd.random() < 0.12 else T_SKY[band]
            out += bytes([t, 0])
    return out


def ball():
    rows = []
    for y in range(16):
        row = []
        for x in range(16):
            dx, dy = x - 7.5, y - 7.5
            r = dx * dx + dy * dy
            if r > 56:
                row.append(0)
            elif r > 40:
                row.append(1)
            elif (dx + 2.5) ** 2 + (dy + 2.5) ** 2 < 7:
                row.append(3)
            else:
                row.append(2)
        rows.append(row)
    out = bytearray()
    for row in rows:
        for x in range(0, 16, 2):
            out.append(row[x] << 4 | row[x + 1])
    return bytes(out)


def palettes():
    slices = [dict(SCENE)] + [{1: d, 2: b, 3: h} for d, b, h in BALLS]
    out = bytearray()
    for level in range(16):
        for s in slices:
            for i in range(16):
                c = s.get(i, 0)
                r, g, b = (c >> 8 & 15) * level // 15, (c >> 4 & 15) * level // 15, (c & 15) * level // 15
                out += bytes([g << 4 | b, r])
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        print("usage: make_assets.py OUTDIR", file=sys.stderr)
        return 2
    for name, data in (("tiles.bin", tiles()), ("map.bin", map_()), ("ball.bin", ball()), ("palettes.bin", palettes())):
        with open(os.path.join(sys.argv[1], name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
