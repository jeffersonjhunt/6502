#!/usr/bin/env python3
"""
splashgen.py - build the OO-1 video card's boot-splash ROM from the OOE logo.

The splash is drawn by fixed-function logic in the video card's FPGA from a
small ROM (block RAM), not from the snooped video banks, so it runs before the
BIOS has put anything in video memory. This script makes that ROM and is also
the reference model of the engine: Rom.render_frame() is what the card draws,
pixel for pixel, and the emulator's VideoCard::draw_splash() follows it.

The show, 300 frames (5 s): the O, the O and the E of the icon scream in from
the left as a pack; DIGITAL MANUFACTURING from the right; half a second later
VINTAGE FAST is rubber-stamped under it (stamp.py): a faint, oversized ghost
pressed down onto the page in 4 frames, landing in red ink with a squash and a
jolt. A glint crosses the logo, and the last 8 frames fade out.

ROM layout, version 2 (little-endian), see hw/design.md "Boot splash":

  $0000  HEADER   32 bytes
           +0  'O','O','E','S'  magic       +4  version (2)
           +5  part count (at most 8)       +6  curve count (at most 4)
           +7  glint band width (pixels)    +8  total frames, 16 bits
           +10 glint start frame, 16 bits   +12 glint speed, pixels a frame
           +13 fade: the last 2^fade frames fade out
           +14 shake start frame, 16 bits   +16 shake curve ($FF: none)
           +17 reserved to +31
  $0020  PALETTE  48 entries x 2 bytes, the card's format: GGGGBBBB, 0000RRRR
                  0-15 the logo ramp (0 = background), 16-31 the bright ramp
                  used by the flash and the glint, 32-47 the ink
  $0080  PARTS    8 entries x 12 bytes
           +0  image offset in the ROM, 16 bits
           +2  width, 16 bits     +4  height, 16 bits
           +6  home X, 16 bits    +8  home Y, 16 bits
           +10 start frame
           +11 flags: bit 0 mirror (enters from the right), bit 1 zoom (the
                      curve scales it about its centre), bit 2 ink (the ink
                      ramp), bits 4-5 curve
  $00E0  CURVES   4 entries x 4 bytes: data offset (16 bits), length, reserved
  $00F0  curve data, 2 bytes a frame since the part's start. A moving part:
           bits 0-10  offset from home in x, signed (negated when mirrored)
           bits 11-13 blur 0-6 (see below)
           bit  14    flash: the part uses the bright ramp this frame
           bit  15    visible
         A zooming part:
           bits 0-10  1 / scale, in 1/256ths (256: its own size)
           bits 11-14 ink level: intensity x (level + 1) / 16
           bit  15    visible
                  After its last entry a part sits at home, sharp and whole.
                  The shake curve's offsets move every part in y, from the
                  shake start.
  ...    IMAGES   2 bits a pixel (coverage 0-3), rows padded to a byte,
                  leftmost pixel in the high bits (as VERA and the card's
                  4-bit modes)

Pixel rule (per part on screen pixel x, y; S the shake now, 0 outside it):
  moving part:   sx = x - (homeX + d), sy = y - (homeY + S)
      w = 2^blur, s = cov(sx + j) summed for j in [-w/2, w/2) (j = 0 when
      w = 1), i = (s x 5) >> blur
  zooming part:  cx = homeX + width/2, cy = homeY + height/2 + S,
      sx = width/2 + floor((x - cx) x inv / 256),
      sy = height/2 + floor((y - cy) x inv / 256),
      i = (cov(sx, sy) x 5 x (level + 1)) >> 4
  cov is 0 outside the image; i is 0-15; v = i | flash << 4 | ink << 5 (0
  when i = 0). The screen pixel takes the part with the largest i (a later
  part on a tie); then if 0 < v < 32 and 0 <= (x + y/2) - glintpos < band,
  v |= 16 (glintpos = (f - glintstart) x speed - band - 240, only while it is
  on screen: the ink doesn't shine). In the last 2^fade frames the intensity
  scales down: k = frames - f, i = (i x k) >> fade. The colour is PALETTE[v].
  After the last frame the card shows its layers again.
  In hardware the box is a running sum over a 64-pixel delay line per part
  (no extra reads), and the zoom two accumulators, one stepped by inv each
  pixel and one each line (no multiplies): one read a pixel.
"""
import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from stamp import make_stamp  # noqa: E402

W, H = 640, 480
FRAMES = 300          # 5 s at 60 Hz
SCALE = 3             # the logo PNG is drawn at 3x the screen's size

# Regions of the 1920x984 PNG (measured: alpha > 128), in PNG pixels.
ICON_ROWS = (124, 663)
WORD_ROWS = (789, 876)
O1_COLS = (133, 640)
O2_COLS = (706, 1214)
E_COLS = (1280, 1788)
LEFT, RIGHT = 133, 1800

STAMP_GAP = 18        # the stamp under the wordmark

# Timeline: the frame each part starts moving. DIGITAL MANUFACTURING lands at
# frame 84 (its start + 14); the stamp comes half a second later.
START = {"O1": 8, "O2": 16, "E": 24, "WORD": 70, "STAMP": 114}
PRESS_FRAMES = 4      # the stamp's way down: it lands at START["STAMP"] + 4
IMPACT = START["STAMP"] + PRESS_FRAMES
GLINT_START, GLINT_SPEED, GLINT_BAND = 170, 16, 40
FADE = 3              # the last 8 frames fade out

HEADER, PALETTE, PARTS, CURVES, CURVE_DATA = 0x00, 0x20, 0x80, 0xE0, 0xF0
MAX_PARTS, MAX_CURVES, PART_SIZE = 8, 4, 12
MIRROR, ZOOM, INK = 0x01, 0x02, 0x04
INK_RGB = (12, 2, 2)  # vintage red; (12, 12, 12) stamps it in the logo's white


def quantise(cov):
    """Coverage 0-1 to 2 bits, columns trimmed on the right."""
    q = np.clip(np.rint(cov * 3), 0, 3).astype(np.uint8)
    cols = np.where(q.max(axis=0) > 0)[0]
    return q[:, : cols.max() + 1]


def downsample(alpha, x0, x1, y0, y1):
    """Area-average a region of the alpha channel by SCALE and quantise to 2 bits."""
    w = math.ceil((x1 - x0) / SCALE)
    h = math.ceil((y1 - y0) / SCALE)
    region = np.zeros((h * SCALE, w * SCALE), dtype=np.float32)
    src = alpha[y0:y1, x0:x1].astype(np.float32) / 255.0
    region[: src.shape[0], : src.shape[1]] = src
    return quantise(region.reshape(h, SCALE, w, SCALE).mean(axis=(1, 3)))


def pack2bpp(img):
    h, w = img.shape
    stride = (w + 3) // 4
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            out[y * stride + x // 4] |= int(img[y, x]) << (6 - 2 * (x % 4))
    return bytes(out)


def rgb12(r, g, b):
    """0-15 each -> the card's 2 bytes: GGGGBBBB, 0000RRRR."""
    return bytes([(g << 4) | b, r])


def make_palette():
    pal = bytearray()
    # Logo ramp: black to a light grey, so the white of the flash and glint shows.
    top = (12, 12, 12)
    for i in range(16):
        t = i / 15
        pal += rgb12(*(round(c * t) for c in top))
    # Bright ramp (flash, glint): up to pure white.
    for i in range(16):
        t = min(1.0, i / 15 * 1.15)
        pal += rgb12(*(round(15 * t) for _ in range(3)))
    # Ink ramp: black to the stamp's ink.
    for i in range(16):
        pal += rgb12(*(round(c * i / 15) for c in INK_RGB))
    return bytes(pal)


def blur_for(speed):
    # 180-degree shutter: the blur is about half the distance moved.
    return min(6, int(round(math.log2(max(1.0, speed * 0.5)))))


def scream_curve():
    """A part screaming in from the left: (offset, blur, flash) a frame.

    A cubic ease-out from 640 px off to OVER px past home in T frames (about
    160 px a frame at the start: MotoGP pace); it holds the overshoot for a
    frame, then snaps back in two frames with a short blur, flashing as it lands.
    """
    T, OVER = 11, 9
    pos = [-640 + (640 + OVER) * (1 - (1 - t / T) ** 3) for t in range(T + 1)]
    pos += [OVER, OVER * 0.35, 0, 0, 0]
    out, prev = [], pos[0] - (pos[1] - pos[0])
    for t, p in enumerate(pos):
        out.append((int(round(p)), blur_for(abs(p - prev)), 1 if t == T + 3 else 0))
        prev = p
    return out


def press_curve():
    """The stamp pressed onto the page: (1/scale x 256, ink level) a frame.

    It starts as a faint ghost 2.2 times its size, as if held above the page
    towards you, and comes down in PRESS_FRAMES, darkening; on the frame it
    lands it's full size and full ink. Then it squashes 4 % and springs back.
    """
    steps = [(2.2, 2), (1.75, 4), (1.4, 6), (1.15, 9), (1.0, 15), (0.96, 15), (1.02, 15), (1.0, 15)]
    assert steps[PRESS_FRAMES][0] == 1.0
    return [(round(256 / scale), level) for scale, level in steps]


def shake_curve():
    """The page's jolt as the stamp lands: every part's y, a frame at a time."""
    return [(dy, 0, 0) for dy in (3, -2, 2, -1, 0)]


def encode_press(entries):
    out = bytearray()
    for inv, level in entries:
        out += struct.pack("<H", inv | level << 11 | 0x8000)
    return bytes(out)


def encode_curve(entries):
    """(offset, blur, flash) entries, for a moving part or the shake."""
    out = bytearray()
    for d, blur, flash in entries:
        out += struct.pack("<H", (d & 0x7FF) | (blur << 11) | (flash << 14) | 0x8000)
    return bytes(out)


def build(png_path):
    im = np.array(Image.open(png_path).convert("RGBA"))
    alpha = im[:, :, 3]
    o = downsample(alpha, *O1_COLS, *ICON_ROWS)       # both Os share it
    e = downsample(alpha, *E_COLS, *ICON_ROWS)
    word = downsample(alpha, LEFT, RIGHT, *WORD_ROWS)

    logo_w = math.ceil((RIGHT - LEFT) / SCALE)
    logo_h = math.ceil((WORD_ROWS[1] - ICON_ROWS[0]) / SCALE)
    stamp = quantise(make_stamp())
    x0 = (W - logo_w) // 2
    y0 = (H - (logo_h + STAMP_GAP + stamp.shape[0])) // 2

    def hx(cols):
        return x0 + (cols[0] - LEFT) // SCALE

    word_y = y0 + (WORD_ROWS[0] - ICON_ROWS[0]) // SCALE
    stamp_y = y0 + logo_h + STAMP_GAP

    curves = [encode_curve(scream_curve()), encode_press(press_curve()), encode_curve(shake_curve())]
    SCREAM, PRESS, SHAKE = 0, 1, 2
    images = [("O", o), ("E", e), ("WORD", word), ("STAMP", stamp)]
    parts = [
        # image, home x, home y, start, flags
        ("O", hx(O1_COLS), y0, START["O1"], SCREAM << 4),
        ("O", hx(O2_COLS), y0, START["O2"], SCREAM << 4),
        ("E", hx(E_COLS), y0, START["E"], SCREAM << 4),
        ("WORD", x0, word_y, START["WORD"], SCREAM << 4 | MIRROR),
        ("STAMP", (W - stamp.shape[1]) // 2, stamp_y, START["STAMP"], PRESS << 4 | ZOOM | INK),
    ]
    assert len(parts) <= MAX_PARTS and len(curves) <= MAX_CURVES

    curve_blobs, curve_dir, at = bytearray(), bytearray(), CURVE_DATA
    for blob in curves:
        curve_dir += struct.pack("<HBB", at, len(blob) // 2, 0)
        curve_blobs += blob
        at += len(blob)
    img_off = at
    blobs, offsets, dims = bytearray(), {}, {}
    for name, img in images:
        offsets[name] = img_off + len(blobs)
        dims[name] = img.shape
        blobs += pack2bpp(img)

    rom = bytearray()
    rom += b"OOES" + bytes([3, len(parts), len(curves), GLINT_BAND])
    rom += struct.pack("<HHBBHB", FRAMES, GLINT_START, GLINT_SPEED, FADE, IMPACT, SHAKE)
    rom += bytes(32 - len(rom))
    rom += make_palette()
    assert len(rom) == PARTS
    for img, x, y, start, flags in parts:
        h, w = dims[img]
        rom += struct.pack("<HHHHHBB", offsets[img], w, h, x, y, start, flags)
    rom += bytes(CURVES - len(rom))
    rom += curve_dir + bytes(CURVE_DATA - CURVES - len(curve_dir))
    rom += curve_blobs
    rom += blobs
    return bytes(rom)


# ---------------------------------------------------------------- the model


class Rom:
    def __init__(self, rom):
        self.rom = rom
        assert rom[:4] == b"OOES" and rom[4] == 3
        self.nparts, self.ncurves, self.band = rom[5], rom[6], rom[7]
        self.frames, self.glint_start, self.glint_speed, self.fade = struct.unpack_from("<HHBB", rom, 8)
        self.shake_start, self.shake_curve = struct.unpack_from("<HB", rom, 14)
        self.pal = [struct.unpack_from("<BB", rom, PALETTE + 2 * i) for i in range(48)]
        self.parts = [struct.unpack_from("<HHHHHBB", rom, PARTS + PART_SIZE * i) for i in range(self.nparts)]
        self.curves = []
        for c in range(self.ncurves):
            off, n, _ = struct.unpack_from("<HBB", rom, CURVES + 4 * c)
            self.curves.append([struct.unpack_from("<H", rom, off + 2 * i)[0] for i in range(n)])
        self.images = {}
        for off, w, h, *_ in self.parts:
            if off not in self.images:
                stride = (w + 3) // 4
                raw = np.frombuffer(rom, np.uint8, stride * h, off).reshape(h, stride)
                px = np.zeros((h, stride * 4), np.uint8)
                for k in range(4):
                    px[:, k::4] = (raw >> (6 - 2 * k)) & 3
                self.images[off] = px[:, :w]

    @staticmethod
    def entry(e):
        d = e & 0x7FF
        if d & 0x400:
            d -= 0x800
        return bool(e & 0x8000), d, (e >> 11) & 7, (e >> 14) & 1

    def shake(self, f):
        if self.shake_curve == 0xFF:
            return 0
        t = f - self.shake_start
        c = self.curves[self.shake_curve]
        return self.entry(c[t])[1] if 0 <= t < len(c) else 0

    def state(self, part, f):
        """The part's raw curve entry for frame f: None before it starts, 0 after its curve."""
        off, w, h, x, y, start, flags = self.parts[part]
        t = f - start
        c = self.curves[(flags >> 4) & 3]
        if t < 0:
            return None
        return c[t] if t < len(c) else 0

    def render_frame(self, f):
        """Palette indices for frame f, H x W."""
        out = np.zeros((H, W), np.uint8)   # v
        best = np.zeros((H, W), np.int32)  # its intensity
        shake = self.shake(f)
        yy, xx = np.mgrid[0:H, 0:W]
        for p in range(self.nparts):
            e = self.state(p, f)
            if e is None:
                continue
            off, w, h, hx, hy, start, flags = self.parts[p]
            img = self.images[off].astype(np.int32)
            inten = np.zeros((H, W), np.int32)
            ink = 32 if flags & INK else 0
            if flags & ZOOM:
                inv, level = (e & 0x7FF, (e >> 11) & 15) if e else (256, 15)
                if e and not e & 0x8000:
                    continue
                cx, cy = hx + w // 2, hy + h // 2 + shake
                sx = w // 2 + (((xx - cx) * inv) >> 8)
                sy = h // 2 + (((yy - cy) * inv) >> 8)
                ok = (sx >= 0) & (sx < w) & (sy >= 0) & (sy < h)
                inten[ok] = (img[sy[ok], sx[ok]] * 5 * (level + 1)) >> 4
                flash = 0
            else:
                vis, d, blur, flash = self.entry(e) if e else (True, 0, 0, 0)
                if not vis:
                    continue
                if flags & MIRROR:
                    d = -d
                bw = 1 << blur
                lo, hi = bw // 2, bw - bw // 2  # window: sx-lo .. sx+hi-1
                pad = np.zeros((h, w + 2 * bw), np.int32)
                pad[:, bw : bw + w] = img
                cs = np.concatenate([np.zeros((h, 1), np.int32), np.cumsum(pad, axis=1)], axis=1)
                sx0 = -(hi - 1)
                sxs = np.arange(sx0, w + lo)
                a = sxs - lo + bw
                vals = ((cs[:, a + bw] - cs[:, a]) * 5) >> blur
                left, top = hx + d + sx0, hy + shake
                ys0, ys1 = max(0, top), min(H, top + h)
                xs0, xs1 = max(0, left), min(W, left + vals.shape[1])
                if xs0 >= xs1 or ys0 >= ys1:
                    continue
                inten[ys0:ys1, xs0:xs1] = vals[ys0 - top : ys1 - top, xs0 - left : xs1 - left]
            win = (inten > 0) & (inten >= best)
            best[win] = inten[win]
            out[win] = (inten[win] | flash << 4 | ink).astype(np.uint8)
        if f >= self.glint_start:
            pos = (f - self.glint_start) * self.glint_speed - self.band - 240
            if pos < W:
                d = xx + yy // 2 - pos
                out[(d >= 0) & (d < self.band) & (out > 0) & (out < 32)] |= 16
        k = self.frames - f
        if k <= 1 << self.fade:
            i = ((out & 15).astype(np.int32) * k) >> self.fade
            out = np.where(i > 0, (out & 0x30) | i, 0).astype(np.uint8)
        return out

    def rgb(self, idx):
        lut = np.zeros((48, 3), np.uint8)
        for i, (gb, r) in enumerate(self.pal):
            lut[i] = [(r & 15) * 17, (gb >> 4) * 17, (gb & 15) * 17]
        return lut[idx]


def write_outputs(rom, outdir, header):
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "splash.bin").write_bytes(rom)
    # $readmemh file for the FPGA's block RAM
    (outdir / "splash.mem").write_text("".join(f"{b:02x}\n" for b in rom))
    # C++ header for the emulator
    lines = [
        "// Generated by backplane/bios/splash/splashgen.py from OOE_Logo_WHT_1920-1080.png. Do not edit.",
        "// The video card's boot-splash ROM: hw/design.md \"Boot splash\".",
        "#pragma once",
        "#include <cstdint>",
        "namespace ooe::emu::splash_rom",
        "{",
        f"    inline constexpr unsigned SIZE = {len(rom)};",
        "    inline constexpr uint8_t DATA[SIZE] = {",
    ]
    for i in range(0, len(rom), 16):
        lines.append("        " + ", ".join(f"0x{b:02X}" for b in rom[i : i + 16]) + ",")
    lines += ["    };", "} // namespace ooe::emu::splash_rom", ""]
    Path(header).write_text("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("png")
    ap.add_argument("-o", "--outdir", default=".", help="for splash.bin and splash.mem")
    ap.add_argument("--header", default="../../../emulator/components/devices/splash_rom.h",
                    help="the emulator's copy of the ROM")
    ap.add_argument("--frames", help="also write PNG frames to this directory")
    args = ap.parse_args()
    rom = build(args.png)
    write_outputs(rom, args.outdir, args.header)
    r = Rom(rom)
    print(f"splash ROM: {len(rom)} bytes, {r.nparts} parts, {r.ncurves} curves, {r.frames} frames")
    for i, p in enumerate(r.parts):
        print(f"  part {i}: image @{p[0]:#06x} {p[1]}x{p[2]} home ({p[3]},{p[4]}) start {p[5]} flags {p[6]:#04x}")
    if args.frames:
        d = Path(args.frames)
        d.mkdir(parents=True, exist_ok=True)
        for f in range(r.frames):
            Image.fromarray(r.rgb(r.render_frame(f))).save(d / f"f{f:03d}.png")


if __name__ == "__main__":
    main()
