#!/usr/bin/env python3
"""OOLIB's SID tables (lib/sidnote.s, lib/sidenv.s; hw/oolib.md, L5c): writes
lib/sidnote.inc and lib/sidenv.inc, which are committed. Run it again only if
a rule below changes; test_oodos_pb works the same values out itself.

  sid_note_pal, sid_note_ntsc   for each note from C-0 (0) to B-7 (95), the
      lowest frequency register value nearer it than the note below: the
      geometric mean of the two notes' values, rounded up (note 0's is 0). A
      voice's note is the last whose threshold its value reaches. The
      register's value is Hz x 2^24 / the clock (985,248 Hz PAL, 1,022,727
      NTSC); A-4 is 440 Hz, note 57.
  sid_env_attack, sid_env_decay   for each of a SID's 16 attack rates and 16
      decay and release rates (the chip's data sheet: 2 ms to 8 s, 6 ms to
      24 s), the step a frame (1/59.94 s) moves a 16-bit level whose full
      scale is $FF00: the time to cross all of it.

These are OO-Amp's (its tools/mksid.py), so its notes and levels stay as
they were when it moves onto the library. Standard library only.
Usage: mksidtab.py LIBDIR
"""
import math, os, sys

CLOCKS = (('pal', 985248), ('ntsc', 1022727))
A4 = 440.0
ATTACK_MS = (2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000)
DECAY_MS = (6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000)
FRAME_MS = 1000 / 59.94
FULL = 0xFF00


def thresholds(clock):
    reg = [A4 * 2 ** ((n - 57) / 12) * (1 << 24) / clock for n in range(96)]
    return [0] + [min(0xFFFF, math.ceil(math.sqrt(reg[n - 1] * reg[n]))) for n in range(1, 96)]


def words(name, vals):
    out = ['%s:' % name]
    for i in range(0, len(vals), 8):
        out.append('        .word   ' + ', '.join('$%04X' % v for v in vals[i:i + 8]))
    return out


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(['; Made by oodos/tools/mksidtab.py: run it, not an editor.', '        .rodata'] + lines) + '\n')


def main(libdir):
    notes = []
    for name, clock in CLOCKS:
        notes += words('sid_note_' + name, thresholds(clock))
    write(os.path.join(libdir, 'sidnote.inc'), notes)
    env = words('sid_env_attack', [min(FULL, round(FULL * FRAME_MS / t)) for t in ATTACK_MS])
    env += words('sid_env_decay', [min(FULL, round(FULL * FRAME_MS / t)) for t in DECAY_MS])
    write(os.path.join(libdir, 'sidenv.inc'), env)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
