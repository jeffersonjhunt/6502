#!/usr/bin/env python3
"""
render.py - the boot splash as the OO-1 would show it.

  * splash.mp4: the splash ROM drawn by the reference model (splashgen.Rom),
    silent. The sound is sound card revision 3's own (its firmware plays it
    from the card's flash, tools/sndflash/); the emulator plays both together.
    The earlier previews of the sound, run from the ROM's 6510 players
    (bootsnd.s, docsnd.s), went with those players (git has them).

usage: render.py splash.bin outdir  (make preview)
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import splashgen  # noqa: E402

FPS = 60


def mux(frames_dir, mp4):
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", str(frames_dir / "f%03d.png"),
         "-c:v", "libx264", "-preset", "veryfast", "-crf", "16", "-pix_fmt", "yuv420p", str(mp4)],
        check=True,
    )
    print("wrote", mp4)


def main():
    rom_path, outdir = sys.argv[1:3]
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    rom = splashgen.Rom(Path(rom_path).read_bytes())
    seconds = rom.frames / FPS + 0.5  # a little tail
    frames_dir = outdir / "frames"
    frames_dir.mkdir(exist_ok=True)
    for f in range(int(seconds * FPS)):
        idx = rom.render_frame(min(f, rom.frames - 1))
        Image.fromarray(rom.rgb(idx)).save(frames_dir / f"f{f:03d}.png")
    mux(frames_dir, outdir / "splash.mp4")


if __name__ == "__main__":
    main()
