#!/usr/bin/env python3
"""
docsamples.py - the boot splash's samples for the sound card's ES5503 (revision 2).

Everything here is synthesised, deterministically (fixed seeds), so `make`
rebuilds the same bytes. The chip plays 8-bit samples (128 is silence; $00 is
its stop marker, so a sample never uses it) at its output rate with 24
oscillators enabled: 7,159,090 / 8 / 26 = 34,419 Hz. The engine and the crack
are made at that rate, to play a sample a step; the wind, the push and the
thunk, whose sound is mostly low, at half of it, to play a sample every other
step, so they take half the room and the BIOS's copy half the time. Every
loop is seamless: the loops are made periodic in the frequency domain, and
the chip's free run (hw/design.md, "The ES5503's oscillators") repeats a
table of N bytes every N - 1 samples, its last byte standing in for its first.

The wave RAM image (wave.bin: its first 80 KB, the rest unused; the BIOS
copies it from flash blocks 24-28):

  $00000  ENGINE   32 KB loop, 4 times oversampled: a MotoGP bike flat out, ~16,100 rpm
  $08000  WIND      8 KB loop, half rate: a bike's air and tyre roar
  $0A000  CRACK     2 KB one-shot: the quickshifter's crack at an upshift
  $0A800  PUSH      2 KB one-shot, half rate: the air a stamp pushes ahead of it
  $0C000  THUNK_L  16 KB one-shot, half rate: the stamp landing, left ...
  $10000  THUNK_R  16 KB one-shot, half rate: ... and right

The engine is built from measurements of the real thing: a trackside
recording of the 2024 MotoGP tests at Mugello (Aprilia, Ducati, Honda, KTM
and Yamaha). Ten clean full-throttle pulls in it were order-tracked (each
resampled to a fixed 256 points per 720-degree cycle, following the engine's
pitch) and their cycles averaged, which gives each harmonic of the cycle
its level, phase and how much of it repeats from cycle to cycle (MAG_DB,
PHASE, COHERENCE). What they show, and what the engine here has:

  * the harmonics of the 720-degree cycle, not just of the firing, all of
    them present: the V4s' uneven firing; 2 to 13 carry the sound, strong
    and weak in turn, and above that they fall about 1.5 dB a harmonic
  * the low harmonics the same every cycle, the high ones (above ~2 kHz)
    mostly new each cycle: the rasp is the cycles' differences, not noise

Each of its cycles is built from those numbers, its varying part a new set
of phases each cycle (engine_cycles), then softly clipped, for level on 8
bits, with the levels aimed off so the clipped loop still has the measured
ones. Nothing is above 9.5 kHz as played, so it doesn't alias when the
Doppler shift plays it faster.

It's oversampled 4 times: a bike plays it about 4 samples a step. The
ES5503 doesn't interpolate, it takes the sample its accumulator's whole part
points at, so a table played at a step that isn't whole is a sample out, at
most, all the time; on this engine, at one sample a step, that error is only
19 dB under the sound itself, a hiss and grit all through it. At 4 samples a
step it's a quarter of a step: 31 dB under, the 8 bits' own floor. The cost
is the loop's length: its 32,767-sample period holds 32 engine cycles
exactly, so it loops without a seam, but repeats each 0.24 s; varying the
cycles' phases only, not their levels, keeps it from beating at that. F0 is
the mean firing frequency at one sample a step; NOMINAL the cycle's
frequency at OVERSAMPLE samples a step, RPM that as rpm.

The deep notches of a single recording (the ground's reflection into the
microphone) aren't the bike's: a harmonic is never more than 6 dB under
the smooth trend of the pulls.
"""
import argparse
import sys
from pathlib import Path

import numpy as np

CLOCK = 7_159_090
OSCILLATORS = 24
FS = CLOCK / 8 / (OSCILLATORS + 2)      # 34,419 Hz
HALF = FS / 2                           # the half-rate tables
IMAGE = 0x14000                         # the part of wave RAM the BIOS loads: 80 KB

ENGINE_AT, ENGINE_SIZE = 0x00000, 0x8000
WIND_AT, WIND_SIZE = 0x08000, 0x2000
CRACK_AT, CRACK_SIZE = 0x0A000, 0x0800
PUSH_AT, PUSH_SIZE = 0x0A800, 0x0800
THUNK_L_AT, THUNK_R_AT, THUNK_SIZE = 0x0C000, 0x10000, 0x4000

OVERSAMPLE = 4                           # the engine is played 4 samples a step (or so)
ENGINE_CYCLES = 32                       # 720-degree cycles in the engine's period
ENGINE_PERIOD = ENGINE_SIZE - 1          # samples: free run's period
CYCLE = ENGINE_PERIOD / ENGINE_CYCLES    # samples per 720 degrees
F0 = 4 * FS / CYCLE                      # mean firing frequency at one sample a step
NOMINAL = OVERSAMPLE * FS / CYCLE        # the cycle's frequency at OVERSAMPLE samples a step
RPM = 120 * NOMINAL                      # ... as rpm

# The engine's cycle, measured (see the module's docstring): harmonic k of the
# 720-degree cycle, from k = 1. MAG_DB is the part that repeats cycle after
# cycle, in dB from the strongest; COHERENCE how much of each harmonic does
# repeat (1: all of it; the rest varies cycle to cycle); PHASE the repeating
# part's phase, in radians, from harmonic 1's. Above the table: -0.6 dB a
# harmonic, the last coherence, random phases.
MAG_DB = (-16.5, -7.4, 1.5, -6.7, -4.6, -12.2, -4.8, -4.2, -14.3, -5.3, -15.2, -9.5,
          -11.6, -18.7, -15.2, -23.8, -20.7, -24.1, -27.9, -26.1, -32.0, -27.9, -31.3, -32.1,
          -31.9, -36.3, -30.5, -35.5, -41.9, -42.6, -39.0, -41.1, -38.9, -41.8, -42.6, -43.3,
          -44.2, -44.7, -45.4, -44.5, -51.3, -46.7, -51.4, -49.5, -50.1, -48.4, -52.3, -53.7)
COHERENCE = (0.94, 0.95, 0.95, 0.91, 0.85, 0.81, 0.82, 0.79, 0.75, 0.70, 0.70, 0.71,
             0.69, 0.63, 0.52, 0.45, 0.45, 0.40, 0.37, 0.27, 0.30, 0.29, 0.31, 0.31,
             0.29, 0.29, 0.24, 0.22, 0.23, 0.25, 0.29, 0.26, 0.24, 0.23, 0.21, 0.19,
             0.20, 0.22, 0.25, 0.22, 0.24, 0.21, 0.23, 0.21, 0.25, 0.24, 0.23, 0.20)
PHASE = (0.00, 6.26, 1.42, 0.21, 0.77, 2.43, 0.08, 2.08, 0.92, 2.95, 4.78, 3.87,
         0.13, 5.62, 1.27, 1.37, 1.21, 3.63, 3.50, 5.10, 5.95, 6.08, 1.46, 2.02)
HARMONICS = 64                           # up to 8.6 kHz at the nominal rate
CLIP = 2.5                               # the soft clip's drive: a crest of 2, for level


def table_code(size):
    """The table register's size bits for a table of `size` bytes (256 << n)."""
    return (size // 256).bit_length() - 1


def to_bytes(x, peak=126):
    """-1..1 to the chip's bytes, 1-255 (never $00, its stop marker)."""
    return np.clip(np.rint(128 + peak * x), 1, 255).astype(np.uint8)


def spectrum_shape(n, fs, points):
    """A magnitude response for an n-point real FFT: (Hz, dB) points, log-interpolated."""
    f = np.fft.rfftfreq(n, 1 / fs)
    hz = np.array([p[0] for p in points], float)
    db = np.array([p[1] for p in points], float)
    g = np.interp(np.log10(np.maximum(f, 1)), np.log10(hz), db, left=db[0], right=db[-1])
    return 10 ** (g / 20)


def normalise(x):
    x = x - x.mean()
    return x / np.max(np.abs(x))


def engine_cycles(seed, level_db):
    """The loop from per-harmonic total levels (dB), cycle by cycle, before the clip.

    Each cycle's harmonic k is the repeating part, COHERENCE of it at PHASE,
    plus a part at a random phase for the rest, new each cycle; a cycle is two periods
    under a Hann window, a period apart, so they sum to a seamless whole, and
    wrap round the loop's end."""
    rng = np.random.default_rng(seed)
    k = np.arange(1, HARMONICS + 1)
    rho = np.array(COHERENCE + (COHERENCE[-1],) * (HARMONICS - len(COHERENCE)))
    phi = np.concatenate([PHASE, rng.uniform(0, 2 * np.pi, HARMONICS - len(PHASE))])
    amp = 10 ** (level_db / 20) * np.clip((9500 - k * NOMINAL) / 2000, 0, 1)
    n, span = ENGINE_PERIOD, int(np.ceil(2 * CYCLE)) + 2
    y = np.zeros(n)
    for c in range(ENGINE_CYCLES):
        centre = c * CYCLE
        z = np.exp(2j * np.pi * rng.random(HARMONICS))   # a new phase, not a new level
        h = amp * (rho * np.exp(1j * phi) + np.sqrt(1 - rho ** 2) * z)
        t = np.arange(int(np.floor(centre - CYCLE)), int(np.floor(centre - CYCLE)) + span)
        u = (t - centre) / CYCLE
        w = np.where(np.abs(u) < 1, 0.5 + 0.5 * np.cos(np.pi * u), 0)
        np.add.at(y, t % n, w * np.real(np.exp(2j * np.pi * np.outer(u, k)) @ h))
    return normalise(y)


def harmonic_levels(y):
    """Each harmonic's total level (dB, RMS over the cycles) in a loop of ENGINE_CYCLES."""
    p = 256
    t = np.arange(ENGINE_CYCLES * p) * len(y) / (ENGINE_CYCLES * p)
    cycles = np.interp(t, np.arange(len(y)), y).reshape(ENGINE_CYCLES, p)
    h = np.fft.rfft(cycles, axis=1) / p
    return 20 * np.log10(np.sqrt((np.abs(h) ** 2).mean(0))[1:HARMONICS + 1] + 1e-12)


def engine(rng):
    # the total level each harmonic should have: its repeating part over (most
    # of) its coherence, since the part that varies carries the rest
    rho = np.array(COHERENCE + (COHERENCE[-1],) * (HARMONICS - len(COHERENCE)))
    db = np.array(MAG_DB + tuple(MAG_DB[-1] - 0.6 * (i + 1) for i in range(HARMONICS - len(MAG_DB))))
    target = db - 0.85 * 20 * np.log10(rho)
    seed = int(rng.integers(1 << 31))
    # the clip adds harmonics and squeezes the strong ones: aim off for it,
    # a few rounds, until the clipped loop's levels are the target's
    adjust, fit = np.zeros(HARMONICS), np.arange(HARMONICS) < len(MAG_DB)
    for _ in range(4):
        y = np.tanh(CLIP * engine_cycles(seed, target + adjust)) / np.tanh(CLIP)
        got = harmonic_levels(y)
        err = got - got[:12].max() - (target - target[:12].max())
        adjust -= 0.8 * np.clip(err, -6, 6) * fit
    # and what the clip adds up high, above what it was aimed for: an EQ over
    # the whole loop, harmonic by harmonic (the loop's bin b is harmonic
    # b / ENGINE_CYCLES of the cycle), to the target, and nothing above it
    Y = np.fft.rfft(y)
    h = np.arange(len(Y)) / ENGINE_CYCLES
    k = np.arange(1, HARMONICS + 1)
    eq = 10 ** (-np.interp(h, k, err) / 20)
    eq *= np.clip((9500 - h * NOMINAL) / 2000, 0, 1) * (h > 0.5)
    return normalise(np.fft.irfft(Y * eq, ENGINE_PERIOD))


def wind(rng):
    n = WIND_SIZE - 1
    W = np.fft.rfft(rng.normal(0, 1, n))
    shape = spectrum_shape(n, HALF, [(20, -50), (150, -12), (500, 0), (2000, -3), (5000, -10), (9000, -24), (13000, -60)])
    return normalise(np.fft.irfft(W * shape, n))


def bandpass_noise(rng, n, points, fs=FS):
    W = np.fft.rfft(rng.normal(0, 1, n))
    return np.fft.irfft(W * spectrum_shape(n, fs, points), n)


def crack(rng):
    """Two quick bangs and a crackle: the ignition cut and its restart."""
    n = int(0.040 * FS)
    t = np.arange(n) / FS
    x = np.zeros(n)
    for at, a in ((0.0, 1.0), (0.0085, 0.75), (0.0175, 0.45)):
        s = t - at
        on = s >= 0
        burst = bandpass_noise(rng, n, [(100, -40), (600, -6), (2500, 0), (7000, -8), (12000, -40)])
        thump = np.sin(2 * np.pi * 150 * s) * np.exp(-s / 0.006)
        x += on * a * (normalise(burst) * np.exp(-s / 0.0025) + 0.6 * thump)
    return normalise(x)


def push(rng):
    """Air shoved ahead of the stamp: noise rising in pitch and level, to the landing."""
    n = int(4 * HALF / 60)                            # the stamp's 4 frames down
    t = np.arange(n) / n
    lo = bandpass_noise(rng, n, [(100, -40), (500, 0), (1500, -8), (6000, -40)], HALF)
    hi = bandpass_noise(rng, n, [(500, -40), (2500, 0), (5000, -6), (10000, -40)], HALF)
    x = (1 - t) * normalise(lo) + t * normalise(hi)
    return normalise(x * t ** 2.2)


def thunk(rng, side):
    """The stamp landing: the contact's click, a sub kick, the rubber's smack, the handle's
    wooden knock, the desk, a short room; driven hard, so it lands with weight."""
    fs = HALF
    n = int(0.62 * fs)
    t = np.arange(n) / fs
    attack = 1 - np.exp(-t / 0.0004)
    f = 46 + 125 * np.exp(-t / 0.028)                 # the kick's pitch falls, 171 Hz to 46
    thump = np.sin(2 * np.pi * np.cumsum(f) / fs) * attack * np.exp(-t / 0.16)
    click = normalise(bandpass_noise(rng, n, [(200, -40), (1200, -6), (3000, 0), (6000, -3), (8500, -30)], fs))
    click *= np.exp(-t / 0.0015)
    smack = normalise(bandpass_noise(rng, n, [(100, -40), (450, -4), (1500, 0), (3500, -6), (9000, -40)], fs))
    smack *= np.exp(-t / 0.014)
    squish = normalise(bandpass_noise(rng, n, [(60, -40), (180, 0), (600, -4), (1500, -40)], fs)) * np.exp(-t / 0.04)
    detune = 1.0 if side == 0 else 1.018             # each ear's knock a touch apart: width
    knock = sum(a * np.sin(2 * np.pi * hz * detune * t + side) * np.exp(-t / tau)
                for hz, a, tau in ((212, 1.0, 0.070), (538, 0.6, 0.045), (1185, 0.35, 0.026), (2360, 0.2, 0.012)))
    desk = np.sin(2 * np.pi * 96 * t) * attack * np.exp(-t / 0.18)
    dry = 1.0 * thump + 0.6 * click + 0.7 * smack + 0.30 * squish + 0.45 * knock + 0.28 * desk
    ir_n = int(0.35 * fs)
    ir = rng.normal(0, 1, ir_n) * np.exp(-np.arange(ir_n) / fs / 0.075)
    wet = np.convolve(dry, ir)[:n]
    x = dry + 0.16 * normalise(wet) * np.max(np.abs(dry))
    x = np.tanh(2.4 * x / np.max(np.abs(x))) / np.tanh(2.4)   # dense: the body stays up behind the hit
    fade = np.clip((n - np.arange(n)) / (0.05 * fs), 0, 1)   # nothing cut off at the end
    return x * fade


def tail_off(x, ms=3.0):
    """A one-shot's last few ms faded to silence, so it doesn't click when it stops."""
    n = int(ms / 1000 * FS)
    x = x.copy()
    x[-n:] *= np.linspace(1, 0, n)
    return x


def place(ram, at, size, data, loop):
    """A table at `at`: a loop fills it (its last byte its first); a one-shot is followed by $00."""
    if loop:
        assert len(data) == size - 1
        data = np.concatenate([data, data[:1]])
    else:
        assert len(data) < size
        data = np.concatenate([data, np.zeros(size - len(data), np.uint8)])
    assert at % size == 0, "a table sits on a multiple of its size"
    ram[at : at + size] = data


def build():
    ram = np.full(IMAGE, 128, np.uint8)  # silence; nothing plays the gaps
    place(ram, ENGINE_AT, ENGINE_SIZE, to_bytes(engine(np.random.default_rng(16568))), True)
    place(ram, WIND_AT, WIND_SIZE, to_bytes(wind(np.random.default_rng(85))), True)
    place(ram, CRACK_AT, CRACK_SIZE, to_bytes(tail_off(crack(np.random.default_rng(270)))), False)
    place(ram, PUSH_AT, PUSH_SIZE, to_bytes(push(np.random.default_rng(4))), False)
    rng = np.random.default_rng(1960)
    l, r = thunk(rng, 0), thunk(rng, 1)
    peak = max(np.max(np.abs(l)), np.max(np.abs(r)))
    place(ram, THUNK_L_AT, THUNK_SIZE, to_bytes(l / peak), False)
    place(ram, THUNK_R_AT, THUNK_SIZE, to_bytes(r / peak), False)
    return ram.tobytes()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("-o", "--out", default="wave.bin")
    ap.add_argument("--wav", help="also write each sample as a WAV here, to listen to")
    a = ap.parse_args()
    ram = build()
    Path(a.out).write_bytes(ram)
    print(f"wave RAM image: {len(ram)} bytes at {FS:.0f} Hz; engine {RPM:.0f} rpm at {OVERSAMPLE} samples a step")
    if a.wav:
        import wave
        d = Path(a.wav)
        d.mkdir(parents=True, exist_ok=True)
        b = np.frombuffer(ram, np.uint8)
        for name, at, size, reps in (("engine", ENGINE_AT, ENGINE_SIZE, 3), ("wind", WIND_AT, WIND_SIZE, 3),
                                     ("crack", CRACK_AT, CRACK_SIZE, 1), ("push", PUSH_AT, PUSH_SIZE, 1),
                                     ("thunk_l", THUNK_L_AT, THUNK_SIZE, 1), ("thunk_r", THUNK_R_AT, THUNK_SIZE, 1)):
            s = b[at : at + size]
            if reps > 1:
                s = np.tile(s[: size - 1], reps)
            pcm = ((s.astype(np.int16) - 128) * 256).astype("<i2")
            rate = FS if name in ("engine", "crack") else HALF
            with wave.open(str(d / f"{name}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(round(rate))
                w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    sys.exit(main())
