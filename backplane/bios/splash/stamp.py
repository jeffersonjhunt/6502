"""
stamp.py - the VINTAGE FAST rubber stamp for the boot splash: the imprint a
rubber stamp leaves, a box with a heavy border and a hairline inside it, the
words in Phillip Cavette's "4990810" (a distressed stamp face, freeware,
fonts/4990810_.TTF: not in git, as it isn't ours to pass on; fontsc.com has
it), tilted a few degrees as a hand stamps it, and inked a little unevenly:
thin patches and grain where the rubber didn't quite meet the page.

Without the font file, `--monoline` (or make_stamp(face=None)) sets the words
in a monoline face drawn here to match DIGITAL MANUFACTURING's instead (the
logo has no V, E or S to borrow, so every letter is drawn from the same few
strokes). That changes the ROM: commit what you build.

make_stamp() returns coverage 0-1 at screen size (float array), drawn 8x
larger and averaged down.
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = Path(__file__).parent / "fonts" / "4990810_.TTF"
FONT_CAP = 0.56     # the font's cap height, of the stamp's height
TRACKING = 0.08     # extra space between its letters, of the cap height

SUPER = 8           # drawn at 8x, then averaged
CAP = 0.40          # cap height, of the stamp's height
STROKE = 0.19       # stroke width, of the cap height (the wordmark's is about 0.14: ink is bolder)
ADVANCE = 0.30      # gap between letters, of the cap height
SPACE = 0.95        # a word space, of the cap height

# Letters on a box w x 1 (y down), as polylines; each corner is rounded with
# the radius given (of the cap height). w is the letter's width.
GLYPHS = {
    "V": (0.80, [([(0, 0), (0.40, 1), (0.80, 0)], 0.06)]),
    "I": (0.00, [([(0, 0), (0, 1)], 0)]),
    "N": (0.80, [([(0, 1), (0, 0), (0.80, 1), (0.80, 0)], 0.06)]),
    "T": (0.82, [([(0, 0), (0.82, 0)], 0), ([(0.41, 0), (0.41, 1)], 0)]),
    "A": (0.80, [([(0, 1), (0, 0), (0.80, 0), (0.80, 1)], 0.30), ([(0, 0.58), (0.80, 0.58)], 0)]),
    "G": (0.80, [([(0.80, 0), (0, 0), (0, 1), (0.80, 1), (0.80, 0.52), (0.44, 0.52)], 0.24)]),
    "E": (0.74, [([(0.74, 0), (0, 0), (0, 1), (0.74, 1)], 0.24), ([(0, 0.5), (0.62, 0.5)], 0)]),
    "F": (0.72, [([(0.72, 0), (0, 0), (0, 1)], 0.24), ([(0, 0.5), (0.60, 0.5)], 0)]),
    "S": (0.78, [([(0.78, 0), (0, 0), (0, 0.5), (0.78, 0.5), (0.78, 1), (0, 1)], 0.22)]),
}


def fillet(points, r):
    """The polyline with each inner corner replaced by an arc of radius r."""
    if r <= 0 or len(points) < 3:
        return points
    out = [points[0]]
    for a, b, c in zip(points, points[1:], points[2:]):
        ax, ay = a[0] - b[0], a[1] - b[1]
        cx, cy = c[0] - b[0], c[1] - b[1]
        la, lc = math.hypot(ax, ay), math.hypot(cx, cy)
        ux, uy, vx, vy = ax / la, ay / la, cx / lc, cy / lc
        cos_t = max(-1.0, min(1.0, ux * vx + uy * vy))
        theta = math.acos(cos_t)                 # the corner's angle
        t = min(r / math.tan(theta / 2), la / 2, lc / 2)  # tangent length
        rr = t * math.tan(theta / 2)
        p0 = (b[0] + ux * t, b[1] + uy * t)
        p1 = (b[0] + vx * t, b[1] + vy * t)
        bx, by = ux + vx, uy + vy
        bl = math.hypot(bx, by)
        d = rr / math.sin(theta / 2)
        centre = (b[0] + bx / bl * d, b[1] + by / bl * d)
        a0 = math.atan2(p0[1] - centre[1], p0[0] - centre[0])
        a1 = math.atan2(p1[1] - centre[1], p1[0] - centre[0])
        da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        for k in range(17):
            ang = a0 + da * k / 16
            out.append((centre[0] + rr * math.cos(ang), centre[1] + rr * math.sin(ang)))
    out.append(points[-1])
    return out


def stroke(draw, pts, width, colour=0):
    """A round-capped, round-joined stroke: a disc stamped every half pixel."""
    rad = width / 2
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) * 2))
        for k in range(n + 1):
            x, y = x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n
            draw.ellipse((x - rad, y - rad, x + rad, y + rad), fill=colour)


def text_width(text, cap):
    """The text's width at cap height `cap`, stroke ends included."""
    w = 0.0
    for i, ch in enumerate(text):
        w += SPACE * cap if ch == " " else GLYPHS[ch][0] * cap
        if i + 1 < len(text) and ch != " " and text[i + 1] != " ":
            w += ADVANCE * cap
    return w + STROKE * cap  # the strokes' half-widths at both ends


def draw_text(d, text, x, y0, cap, colour):
    """The text in the monoline face, its left edge at x and its caps from y0."""
    sw = STROKE * cap
    x += sw / 2
    for i, ch in enumerate(text):
        if ch == " ":
            x += SPACE * cap
            continue
        gw, strokes = GLYPHS[ch]
        for pts, r in strokes:
            pts = [(x + px * cap, y0 + py * cap) for px, py in pts]
            stroke(d, fillet(pts, r * cap), sw, colour)
        x += gw * cap
        if i + 1 < len(text) and text[i + 1] != " ":
            x += ADVANCE * cap


def draw_font_text(d, text, cx, cy, cap, colour, face):
    """The text in a TrueType face, centred on (cx, cy), caps cap high."""
    probe = ImageFont.truetype(str(face), 1000)
    l, t, r, b = probe.getbbox("H")
    font = ImageFont.truetype(str(face), round(1000 * cap / (b - t)))
    track = TRACKING * cap
    widths = [font.getlength(ch) for ch in text]
    total = sum(widths) + track * (len(text) - 1)
    l, t, r, b = font.getbbox("H")
    x, y = cx - total / 2, cy - (t + b) / 2
    for ch, w in zip(text, widths):
        d.text((x, y), ch, font=font, fill=colour)
        x += w + track


def make_stamp(width=None, height=84, tilt=4.0, text="VINTAGE FAST", seed=1960, face=FONT):
    """The imprint: width x height before the tilt; returns the tilted coverage, trimmed.

    face: a TrueType file for the words, or None for the monoline face."""
    if face is not None and not Path(face).exists():
        sys.exit(f"stamp.py: no {face}. Download Phillip Cavette's 4990810 from fontsc.com "
                 "and put its .TTF there, or build with the monoline face (stamp.py --monoline).")
    if width is None:
        width = 468 if face is None else 424  # the font is condensed: a tighter box
    W, H = width * SUPER, height * SUPER
    pad = int(0.08 * W)  # room for the tilt
    im = Image.new("L", (W + 2 * pad, H + 2 * pad), 0)
    d = ImageDraw.Draw(im)
    border = 0.075 * H                          # the heavy border ...
    r = 0.16 * H
    d.rounded_rectangle((pad, pad, pad + W - 1, pad + H - 1), radius=r, fill=255)
    d.rounded_rectangle((pad + border, pad + border, pad + W - 1 - border, pad + H - 1 - border),
                        radius=max(1, r - border), fill=0)
    gap, hair = 0.07 * H, 0.03 * H              # ... and a hairline inside it
    o = border + gap
    d.rounded_rectangle((pad + o, pad + o, pad + W - 1 - o, pad + H - 1 - o), radius=max(1, r - o), fill=255)
    d.rounded_rectangle((pad + o + hair, pad + o + hair, pad + W - 1 - o - hair, pad + H - 1 - o - hair),
                        radius=max(1, r - o - hair), fill=0)
    if face is None:
        cap = CAP * H
        draw_text(d, text, pad + (W - text_width(text, cap)) / 2, pad + (H - cap) / 2, cap, 255)
    else:
        draw_font_text(d, text, pad + W / 2, pad + H / 2, FONT_CAP * H, 255, face)
    # Uneven ink: broad thin patches and fine grain, the same every build.
    rng = np.random.default_rng(seed)
    a = np.asarray(im, np.float32) / 255
    h, w = a.shape
    def noise(cell):
        n = rng.random((h // cell + 2, w // cell + 2)).astype(np.float32)
        img = Image.fromarray((n * 255).astype(np.uint8)).resize((w + 2 * cell, h + 2 * cell), Image.BICUBIC)
        return np.asarray(img, np.float32)[cell : cell + h, cell : cell + w] / 255
    patches = noise(40 * SUPER // 8)            # blotches about 5 px across on the screen
    grain = noise(2 * SUPER // 2)               # specks about 1 px
    # (the font is worn already: with it, a lighter hand)
    thin, holes = (0.9, 0.12) if face is None else (0.5, 0.05)
    ink = np.clip(1.25 - thin * np.clip(patches - 0.45, 0, 1) * 2.2, 0, 1)  # thin where patches run high
    ink *= grain > holes                        # and pinholes
    a = a * ink
    img = Image.fromarray((a * 255).astype(np.uint8)).rotate(tilt, resample=Image.BICUBIC, expand=False)
    a = np.asarray(img, np.float32) / 255
    # trim to the ink, then to whole screen pixels
    ys, xs = np.nonzero(a > 0.02)
    a = a[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]
    hh, ww = -(-a.shape[0] // SUPER) * SUPER, -(-a.shape[1] // SUPER) * SUPER
    full = np.zeros((hh, ww), np.float32)
    full[: a.shape[0], : a.shape[1]] = a
    return full.reshape(hh // SUPER, SUPER, ww // SUPER, SUPER).mean(axis=(1, 3))


if __name__ == "__main__":
    s = make_stamp(face=None if "--monoline" in sys.argv else FONT)
    q = np.clip(np.rint(s * 3), 0, 3) * 85
    Image.fromarray(q.astype(np.uint8)).resize((s.shape[1] * 2, s.shape[0] * 2), Image.NEAREST).save("stamp_preview.png")
    print(s.shape)
