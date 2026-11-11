# Boot splash

The One Off Endeavors logo, built in 5 s at power-on by the video card from a ROM
of its own: four MotoGP bikes scream past as its parts come in, then VINTAGE FAST
is rubber-stamped on in red ink with a thunk. The sound plays on the sound card's
ES5503 from samples (revision 2), or on its two SIDs (revision 1).
hw/design.md ("Boot splash") is the spec; this folder makes its data.

| File | What it is |
|---|---|
| `OOE_Logo_WHT_1920-1080.png` | The logo, white on transparent. Everything else comes from it |
| `splashgen.py` | The video card's splash ROM from the logo, and the reference model of the card's splash engine (`Rom.render_frame()`) |
| `stamp.py` | The VINTAGE FAST stamp's imprint: a bordered box, the words in Phillip Cavette's freeware stamp face "4990810", tilted, unevenly inked. `--monoline` sets them in a face drawn to match the wordmark instead |
| `fonts/4990810_.TTF` | That face. Not in git (freeware, but not ours to pass on): fontsc.com has it, and the build stops and says so if it's missing |
| `splash.bin`, `splash.mem` | The ROM (32 KB), and as a `$readmemh` file for the FPGA's block RAM |
| `../../../emulator/components/devices/splash_rom.h` | The emulator's copy of the ROM |
| `soundgen.py` | The sound's timing and the bikes' physics (Doppler, rpm, distance), and the SIDs' tables (`tables.inc`) |
| `bootsnd.s`, `bootsnd.cfg` | The SIDs' program (revision 1): a 240 Hz player (the bikes, then the stamp's push and thunk) plus a boot entry for the card (`$1006`) |
| `bootsnd.bin` | It assembled (about 2 KB), which `../splash.s` loads into the card |
| `docsamples.py` | The ES5503's samples, all synthesised: the engine (from measurements of a MotoGP recording, kept as numbers), the wind, the crack, the push and the stereo thunk |
| `wave.bin` | Them as an image of wave RAM's first 80 KB, which `../splash.s` copies in from flash blocks 24-28 |
| `docgen.py` | The ES5503 player's tables (`doctables.inc`): each bike's pitch for the engine sample and each ear's volume |
| `docsnd.s` | The ES5503's program (revision 2): the same scene from the samples, on 24 oscillators, with the same boot entry; `docsnd.bin` assembled (about 1.5 KB) |
| `render.py` | The preview: `preview/bootsnd.sid` (stereo PSID), `preview/splash.wav` and `preview/splash.mp4` (the SIDs), `preview/splash_doc.wav` and `preview/splash_doc.mp4` (the ES5503) |

Everything made here is committed, so the BIOS and the emulator build with cc65
alone. After changing the logo, the stamp (`stamp.py`; its ink colour is
`INK_RGB` in `splashgen.py`), the motion (`START`, `scream_curve()`,
`press_curve()`, `shake_curve()` in `splashgen.py`) or the
sound (`soundgen.py`, `bootsnd.s`; `docsamples.py`, `docgen.py`, `docsnd.s`):

    make            # ROM, header, tables, both players, wave.bin  (python3, numpy, Pillow, cc65)
    make preview    # also py65, pyresidfp and ffmpeg

then `make` in `..` for the BIOS. `test_video` checks the emulator's splash
against hashes of frames from `Rom.render_frame()`, so a new ROM needs those
hashes remade (the snippet is in the test's comment).

The preview is exact for the picture: it draws the ROM with the model the card
and the emulator follow. The sound is the real players run on a 6502 (py65):
`bootsnd.bin`'s SID writes clocked into two reSIDfp 8580s at the card's NTSC
clock a tick (1/240 s) at a time, and `docsnd.bin`'s ES5503 writes played from
`wave.bin` through a model of the chip (`render.py`'s `Doc`, the emulator's
rules for free run and one-shot). The emulator's own SIDs and ES5503 give the
same envelopes, to within a dB and a third of one.
