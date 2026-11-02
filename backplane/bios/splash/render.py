#!/usr/bin/env python3
"""
render.py - the boot splash as the OO-1 would show and play it.

  * bootsnd.sid: the sound card's tune as a stereo PSID v3 (SID B at $D420),
    playable in any SID player (sidplayfp, VICE's vsid) and on the card.
  * splash.wav: the tune run on a 6502 (py65) with its SID writes clocked
    into two reSIDfp 8580s at the card's NTSC clock, a frame at a time.
  * splash.mp4: the splash ROM drawn by the reference model (splashgen.Rom),
    with that sound.

usage: render.py splash.bin bootsnd.bin outdir  (make preview)
"""
import struct
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image
from py65.devices.mpu6502 import MPU
from py65.memory import ObservableMemory
from pyresidfp import SoundInterfaceDevice
from pyresidfp._pyresidfp import ChipModel

sys.path.insert(0, str(Path(__file__).parent))
import splashgen  # noqa: E402

LOAD, INIT, PLAY = 0x1000, 0x1000, 0x1003
CLOCK = 1_022_727
FPS = 60
RATE_HZ = 240     # play calls a second (the tune sets CIA 1's timer)
RATE = 48000


def make_psid(binary):
    h = bytearray(0x7C)
    h[0:4] = b"PSID"
    struct.pack_into(">HHHHHHHI", h, 4, 3, 0x7C, LOAD, INIT, PLAY, 1, 1, 1)  # speed bit: CIA
    h[0x16:0x16 + 32] = b"OO-1 Boot Splash".ljust(32, b"\0")
    h[0x36:0x36 + 32] = b"One Off Endeavors".ljust(32, b"\0")
    h[0x56:0x56 + 32] = b"2026 One Off Endeavors".ljust(32, b"\0")
    # flags: built-in player 0, C64 compatible, NTSC (bits 2-3 = 2),
    # 8580 (bits 4-5 = 2), second SID 8580 (bits 6-7 = 2)
    struct.pack_into(">H", h, 0x76, (2 << 2) | (2 << 4) | (2 << 6))
    h[0x7A] = 0x42  # second SID at $D420
    h[0x7B] = 0x00
    return bytes(h) + binary


def run_tune(binary, ticks):
    """Per play call, the (chip, register, value) writes it makes."""
    mem = ObservableMemory()
    for i, b in enumerate(binary):
        mem[LOAD + i] = b
    log = []
    sid = {}

    def on_write(addr, value):
        log.append((0 if addr < 0xD420 else 1, (addr - 0xD400) % 0x20, value))
        sid[addr] = value

    mem.subscribe_to_write(range(0xD400, 0xD43D), on_write)
    cpu = MPU(memory=mem)

    def call(addr):
        # JSR from a stub at $0200 that ends in BRK-free spin: return to $0203
        mem[0x0200] = 0x20
        mem[0x0201] = addr & 255
        mem[0x0202] = addr >> 8
        mem[0x0203] = 0x4C  # JMP $0203
        mem[0x0204] = 0x03
        mem[0x0205] = 0x02
        cpu.pc = 0x0200
        cpu.sp = 0xFF
        cycles = 0
        while cpu.pc != 0x0203:
            cpu.step()
            cycles += 1
            assert cycles < 100000, "runaway"
        return cpu.processorCycles

    call(INIT)
    out = [list(log)]
    worst = 0
    for f in range(ticks):
        log.clear()
        before = cpu.processorCycles
        call(PLAY)
        worst = max(worst, cpu.processorCycles - before)
        out.append(list(log))
    return out, worst


def render_audio(writes, seconds):
    chips = [
        SoundInterfaceDevice(model=ChipModel.MOS8580, clock_frequency=CLOCK, sampling_frequency=RATE)
        for _ in range(2)
    ]
    chans = [[], []]
    frame_cycles = CLOCK / RATE_HZ  # a play call's worth
    for c in chips:  # power-on settle, not recorded
        c._sid.clock(CLOCK // 5)
    total = int(seconds * RATE_HZ)
    for f in range(total + 1):
        for chip, reg, val in writes[f] if f < len(writes) else []:
            chips[chip]._sid.write(reg, val)
        for c in range(2):
            chans[c] += chips[c]._sid.clock(int(round(frame_cycles * (f + 1))) - int(round(frame_cycles * f)))
    n = min(len(chans[0]), len(chans[1]))
    st = np.stack([np.array(chans[0][:n]), np.array(chans[1][:n])], axis=1).astype(np.float32)
    # The card's outputs have no DC (they are AC-coupled): high-pass at 20 Hz.
    a = np.exp(-2 * np.pi * 20 / RATE)
    y = np.zeros_like(st)
    prev_x = st[0].copy()  # the card has been on a while: no power-on thump
    prev_y = np.zeros(2, np.float32)
    for i in range(n):
        prev_y = a * (prev_y + st[i] - prev_x)
        prev_x = st[i]
        y[i] = prev_y
    peak = np.abs(y).max()
    y = y / peak * 0.89 * 32767
    return y.astype(np.int16)


def main():
    rom_path, bin_path, outdir = sys.argv[1:4]
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    binary = Path(bin_path).read_bytes()
    (outdir / "bootsnd.sid").write_bytes(make_psid(binary))

    rom = splashgen.Rom(Path(rom_path).read_bytes())
    seconds = rom.frames / FPS + 0.5  # a little tail
    writes, worst = run_tune(binary, int(seconds * RATE_HZ) + 1)
    print(f"tune: {len(binary)} bytes, worst play call {worst} cycles ({worst / (CLOCK / RATE_HZ):.1%} of a tick)")
    pcm = render_audio(writes, seconds)
    with wave.open(str(outdir / "splash.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())

    frames_dir = outdir / "frames"
    frames_dir.mkdir(exist_ok=True)
    for f in range(int(seconds * FPS)):
        idx = rom.render_frame(min(f, rom.frames - 1))
        Image.fromarray(rom.rgb(idx)).save(frames_dir / f"f{f:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", str(frames_dir / "f%03d.png"),
         "-i", str(outdir / "splash.wav"), "-c:v", "libx264", "-preset", "veryfast", "-crf", "16",
         "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-shortest", str(outdir / "splash.mp4")],
        check=True,
    )
    print("wrote", outdir / "splash.mp4")


if __name__ == "__main__":
    main()
