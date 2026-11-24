# Boot splash

The One Off Endeavors logo, built in 5 s at power-on by the video card from a ROM
of its own: four MotoGP bikes scream past as its parts come in, then VINTAGE FAST
is rubber-stamped on in red ink with a thunk. The sound is sound card revision 3's:
its firmware plays it from the card's flash, begun by one record (BOOTSOUND) at
the vertical blank the splash starts on; this folder makes its samples and its
sequence, as revision 2's ES5503 player had them.
hw/design.md ("Boot splash") is the spec; this folder makes its data.

| File | What it is |
|---|---|
| `OOE_Logo_WHT_1920-1080.png` | The logo, white on transparent. Everything else comes from it |
| `splashgen.py` | The video card's splash ROM from the logo, and the reference model of the card's splash engine (`Rom.render_frame()`) |
| `stamp.py` | The VINTAGE FAST stamp's imprint: a bordered box, the words in Phillip Cavette's freeware stamp face "4990810", tilted, unevenly inked. `--monoline` sets them in a face drawn to match the wordmark instead |
| `fonts/4990810_.TTF` | That face. Not in git (freeware, but not ours to pass on): fontsc.com has it, and the build stops and says so if it's missing |
| `splash.bin`, `splash.mem` | The ROM (32 KB), and as a `$readmemh` file for the FPGA's block RAM |
| `../../../emulator/components/devices/splash_rom.h` | The emulator's copy of the ROM |
| `soundgen.py` | The sound's timing and the bikes' physics (Doppler, rpm, distance), which `docgen.py` takes. (Its `tables.inc` was revision 1's SID player's, `bootsnd.s`, which left the ROM with revision 3) |
| `docsamples.py` | The ES5503's samples, all synthesised: the engine (from measurements of a MotoGP recording, kept as numbers), the wind, the crack, the push and the stereo thunk |
| `../../../tools/sndflash/wave.bin` | Them as an image of wave RAM's first 80 KB, for the sound card's flash |
| `docgen.py` | The sequence's tables (`../../../tools/sndflash/doctables.inc`): each bike's pitch for the engine sample and each ear's volume, which the card's firmware plays on 24 oscillators as revision 2's 6510 player (`docsnd.s`, gone from the ROM with revision 3) did |
| `../../../tools/mksndflash.py` | The emulator's copy of the card's flash (`sound_flash.h`) from those two |
| `render.py` | The preview: `preview/splash.mp4`, the picture, silent |

Everything made here is committed, so the BIOS and the emulator build with cc65
alone. After changing the logo, the stamp (`stamp.py`; its ink colour is
`INK_RGB` in `splashgen.py`), the motion (`START`, `scream_curve()`,
`press_curve()`, `shake_curve()` in `splashgen.py`) or the
sound (`soundgen.py`, `docsamples.py`, `docgen.py`):

    make            # ROM, header, samples, tables, sound_flash.h  (python3, numpy, Pillow)
    make preview    # also ffmpeg

then `make` in `..` for the BIOS. `test_video` checks the emulator's splash
against hashes of frames from `Rom.render_frame()`, so a new ROM needs those
hashes remade (the snippet is in the test's comment).

The preview is exact for the picture: it draws the ROM with the model the card
and the emulator follow. The sound is the card's: the emulator plays both.
