# Boot splash

The One Off Endeavors logo, built in 5 s at power-on by the video card from a ROM
of its own: four MotoGP bikes scream past on the sound card's two SIDs as its
parts come in, then VINTAGE FAST is rubber-stamped on in red ink with a thunk.
hw/design.md ("Boot splash") is the spec; this folder makes its data.

| File | What it is |
|---|---|
| `OOE_Logo_WHT_1920-1080.png` | The logo, white on transparent. Everything else comes from it |
| `splashgen.py` | The video card's splash ROM from the logo, and the reference model of the card's splash engine (`Rom.render_frame()`) |
| `stamp.py` | The VINTAGE FAST stamp's imprint: a bordered box, the words in Phillip Cavette's freeware stamp face "4990810", tilted, unevenly inked. `--monoline` sets them in a face drawn to match the wordmark instead |
| `fonts/4990810_.TTF` | That face. Not in git (freeware, but not ours to pass on): fontsc.com has it, and the build stops and says so if it's missing |
| `splash.bin`, `splash.mem` | The ROM (32 KB), and as a `$readmemh` file for the FPGA's block RAM |
| `../../../emulator/components/devices/splash_rom.h` | The emulator's copy of the ROM |
| `soundgen.py` | The sound's tables and timing (`tables.inc`): Doppler pitch, distance and panning for the bikes, timed to the ROM's parts, and the landing's constants |
| `bootsnd.s`, `bootsnd.cfg` | The sound card's program: a 240 Hz player (the bikes, then the stamp's push and thunk) plus a boot entry for the card (`$1006`) |
| `bootsnd.bin` | It assembled (about 2 KB), which `../splash.s` loads into the card |
| `render.py` | The preview: `preview/bootsnd.sid` (stereo PSID), `preview/splash.wav` and `preview/splash.mp4` |

Everything made here is committed, so the BIOS and the emulator build with cc65
alone. After changing the logo, the stamp (`stamp.py`; its ink colour is
`INK_RGB` in `splashgen.py`), the motion (`START`, `scream_curve()`,
`press_curve()`, `shake_curve()` in `splashgen.py`) or the
sound (`soundgen.py`, `bootsnd.s`):

    make            # ROM, header, tables, bootsnd.bin  (python3, numpy, Pillow, cc65)
    make preview    # also py65, pyresidfp and ffmpeg

then `make` in `..` for the BIOS. `test_video` checks the emulator's splash
against hashes of frames from `Rom.render_frame()`, so a new ROM needs those
hashes remade (the snippet is in the test's comment).

The preview is exact for the picture: it draws the ROM with the model the card
and the emulator follow. The sound is the real `bootsnd.bin` run on a 6502
(py65), its SID writes clocked into two reSIDfp 8580s at the card's NTSC
clock a tick (1/240 s) at a time; the emulator's own SIDs give the same envelope to
within a dB.
