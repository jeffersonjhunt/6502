#!/usr/bin/env python3
"""
render.py - the boot splash as the OO-1 would show and play it.

  * bootsnd.sid: the sound card's tune as a stereo PSID v3 (SID B at $D420),
    playable in any SID player (sidplayfp, VICE's vsid) and on the card.
  * splash.wav: the tune run on a 6502 (py65) with its SID writes clocked
    into two reSIDfp 8580s at the card's NTSC clock, a frame at a time.
  * splash.mp4: the splash ROM drawn by the reference model (splashgen.Rom),
    with that sound: as on sound card revision 1.
  * With --doc docsnd.bin wave.bin, as on revision 2: splash_doc.wav and
    splash_doc.mp4, docsnd.bin run on a 6502 with its writes to the ES5503
    ($DF00) played through a model of the chip (Doc below: the emulator's
    es5503.cpp rules for the modes the splash uses, free run and one-shot)
    from wave.bin, at its 34,419 Hz with the 24 oscillators the player enables.

Both previews are scaled to the same peak, to listen to.

usage: render.py splash.bin bootsnd.bin outdir [--doc docsnd.bin wave.bin]  (make preview)
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


def run_tune(binary, ticks, doc=False):
    """Per play call, the writes it makes: (chip, register, value) to the SIDs,
    or with doc (register, value) to the ES5503 at $DF00."""
    mem = ObservableMemory()
    for i, b in enumerate(binary):
        mem[LOAD + i] = b
    log = []

    def on_sid(addr, value):
        log.append((0 if addr < 0xD420 else 1, (addr - 0xD400) % 0x20, value))

    def on_doc(addr, value):
        log.append((addr - 0xDF00, value))

    if doc:
        mem.subscribe_to_write(range(0xDF00, 0xE000), on_doc)
    else:
        mem.subscribe_to_write(range(0xD400, 0xD43D), on_sid)
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


class Doc:
    """The ES5503 as the splash uses it: free run and one-shot, no interrupts.

    Each step every running oscillator among the enabled reads the byte at its
    index (accumulator >> (9 + resolution - size)), a $00 halting it, adds
    (byte - 128) x volume to its channel, and steps; an index at the table's
    last byte ends it: free run takes (size - 1) << shift off, one-shot halts.
    Clearing the halt bit starts it from 0 (hw/design.md, "The ES5503's
    oscillators")."""

    def __init__(self, ram):
        self.ram = np.frombuffer(ram, np.uint8)
        self.reg = bytearray(256)
        for n in range(32):
            self.reg[0xA0 + n] = 1  # halted
        self.acc = [0] * 32

    def write(self, r, v):
        if 0xA0 <= r < 0xC0:
            n = r - 0xA0
            if (self.reg[r] & 1) and not (v & 1):
                self.acc[n] = 0
        self.reg[r] = v

    def enabled(self):
        return ((self.reg[0xE1] >> 1) & 31) + 1

    def steps(self, count):
        """count steps: an array (count, 2) of the left and right sums."""
        out = np.zeros((count, 2), np.int64)
        reg, ram = self.reg, self.ram
        for n in range(self.enabled()):
            ctl = reg[0xA0 + n]
            if ctl & 1:
                continue
            freq = reg[n] | reg[0x20 + n] << 8
            vol = reg[0x40 + n]
            table = reg[0xC0 + n]
            sz = (table >> 3) & 7
            res = table & 7
            shift = 9 + res - sz
            size = 1 << (8 + sz)
            base = ((table >> 6 & 1) << 16 | reg[0x80 + n] << 8) & (0x1FF00 & ~(size - 1))
            back = (size - 1) << shift
            oneshot = (ctl >> 1) & 3 == 1
            ch = (ctl >> 4) & 1
            acc = self.acc[n]
            col = out[:, ch]
            for k in range(count):
                index = acc >> shift
                byte = ram[base + (index & (size - 1))]
                acc += freq
                if byte == 0:
                    ctl |= 1
                    break
                col[k] += (int(byte) - 128) * vol
                if index >= size - 1:
                    if oneshot:
                        ctl |= 1
                        break
                    acc = acc - back if acc >= back else 0
            self.acc[n] = acc
            reg[0xA0 + n] = ctl
        return out


def render_doc(writes, wave_ram, seconds):
    """The card's DOC bus at its own rate: each step's sums over 261,120 (a voice at full is 1/8)."""
    doc = Doc(wave_ram)
    for r, v in writes[0]:  # init's
        doc.write(r, v)
    step_hz = 7_159_090 / 8 / (doc.enabled() + 2) if writes[0] else 49_716
    out, made = [], 0
    for t in range(1, int(seconds * RATE_HZ) + 1):
        for r, v in writes[t] if t < len(writes) else []:
            doc.write(r, v)
        due = int(round(step_hz * t / RATE_HZ))
        out.append(doc.steps(due - made))
        made = due
    return np.concatenate(out).astype(np.float64) / 261_120, round(step_hz)


def to_pcm(st, peak_to=0.89):
    return (st / np.abs(st).max() * peak_to * 32767).astype(np.int16)


def write_wav(path, pcm, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def mux(frames_dir, wav, mp4):
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", str(frames_dir / "f%03d.png"),
         "-i", str(wav), "-c:v", "libx264", "-preset", "veryfast", "-crf", "16",
         "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-shortest", str(mp4)],
        check=True,
    )
    print("wrote", mp4)


def main():
    rom_path, bin_path, outdir = sys.argv[1:4]
    doc = None
    if "--doc" in sys.argv:
        i = sys.argv.index("--doc")
        doc = (Path(sys.argv[i + 1]).read_bytes(), Path(sys.argv[i + 2]).read_bytes())
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    binary = Path(bin_path).read_bytes()
    (outdir / "bootsnd.sid").write_bytes(make_psid(binary))

    rom = splashgen.Rom(Path(rom_path).read_bytes())
    seconds = rom.frames / FPS + 0.5  # a little tail
    writes, worst = run_tune(binary, int(seconds * RATE_HZ) + 1)
    print(f"tune: {len(binary)} bytes, worst play call {worst} cycles ({worst / (CLOCK / RATE_HZ):.1%} of a tick)")
    write_wav(outdir / "splash.wav", render_audio(writes, seconds), RATE)
    if doc:
        writes, worst = run_tune(doc[0], int(seconds * RATE_HZ) + 1, doc=True)
        print(f"doc tune: {len(doc[0])} bytes, worst play call {worst} cycles ({worst / (CLOCK / RATE_HZ):.1%} of a tick)")
        st, rate = render_doc(writes, doc[1], seconds)
        print(f"doc bus: peak {20 * np.log10(np.abs(st).max()):.1f} dBFS at {rate} Hz")
        write_wav(outdir / "splash_doc.wav", to_pcm(st), rate)

    frames_dir = outdir / "frames"
    frames_dir.mkdir(exist_ok=True)
    for f in range(int(seconds * FPS)):
        idx = rom.render_frame(min(f, rom.frames - 1))
        Image.fromarray(rom.rgb(idx)).save(frames_dir / f"f{f:03d}.png")
    mux(frames_dir, outdir / "splash.wav", outdir / "splash.mp4")
    if doc:
        mux(frames_dir, outdir / "splash_doc.wav", outdir / "splash_doc.mp4")


if __name__ == "__main__":
    main()
