## 6502 emulator

A 6502 core, the Apple-1 memory map, and a terminal front end that runs the
Apple-1 ROMs in `../apple1/src`. It is being refactored to stand in for the
backplane 6502 computer (`../hw/`) during bring-up; see
[`docs/EMULATOR_PLAN.md`](docs/EMULATOR_PLAN.md).

```
cmake --preset release && cmake --build --preset release
./build/release/apps/emu/emulator ../apple1/src/ibasic.rom
./build/release/apps/emu/emulator --machine machines/apple1.machine ../apple1/src/applesoft.rom
./build/release/apps/emu/emulator --machine machines/apple1-65c02.machine --clock 0 ../apple1/src/ibasic.rom
```

`--clock HZ` sets the emulated clock (default 1 MHz, real time for an
Apple-1); `--clock 0` runs as fast as the host allows. Ctrl-Z suspends it
with the terminal put back, as any program. Input can be a pipe or a file
instead of a terminal: keys, Ctrl-] and debugger lines in one stream.

## Bring-up tools

Press **Ctrl-]** to stop the machine at a `debug>` prompt (`h` lists the
commands). They follow the backplane's console panel (`../hw/console.md`):
`r` run, `s [N]` step, `f ADDR [N]` fetch, `st ADDR BYTE...` store, plus
`x` registers, `l` disassemble, `b`/`d` breakpoints, `t` trace on/off and
`q` quit. Addresses take labels. A step does what running does after each
instruction (interrupts, resets), and Ctrl-C cuts a long one short.

```
make -C ../apple1/src roms            # writes ibasic.rom.lbl beside the ROM
./build/release/apps/emu/emulator --symbols ../apple1/src/ibasic.rom.lbl \
    --break ECHO --halt ../apple1/src/ibasic.rom
./build/release/apps/emu/emulator --load prog.hex --load data.bin@0280 --trace run.trace ...
python3 tools/trace_diff.py --symbols ../apple1/src/ibasic.rom.lbl board.trace run.trace
```

- `--halt` starts at the prompt; `--break ADDR` sets a breakpoint (hex or
  label). STP also stops into the debugger.
- `--symbols FILE` reads ld65 `-Ln` labels. The Apple-1 ROM builds write
  `<rom>.lbl` and assemble with `ca65 -g`, so every label is in it.
- `--load FILE` loads Intel HEX (`.hex`/`.ihx`) and `--load FILE@ADDR` a raw
  image, before reset and past ROM's write protection.
- `--trace FILE` writes every bus cycle, SYNC and VPB included, in the
  format the ESP32 bus spy is to use (`docs/BUS_TRACE.md`);
  `tools/trace_diff.py` finds the first cycle where two traces differ.
- `--screen FILE` writes the video card's last frame, 640x480, as a PPM when
  the emulator exits (a machine with a `video` line; see below).

## Ben Eater's board

`machines/eater.machine` is the breadboard computer as it ends his series:
6522, 6551, a 16x2 LCD and a PS/2 keyboard. The programs in `../eater/src`
build with vasm (`make -C ../eater`), as for the real board:

```
./build/release/apps/emu/emulator --machine machines/eater.machine ../eater/src/wozmon.rom    # serial
./build/release/apps/emu/emulator --machine machines/eater.machine ../eater/src/keyboard.rom  # LCD + keyboard
./build/release/apps/emu/emulator --machine machines/eater-lcd8.machine ../eater/src/hello-world-final.rom
```

The LCD is drawn as a box under the text. Keys go to whatever the program
is listening on: the serial port once it turns DTR on, the keyboard once
it enables CA1 interrupts.

## The backplane computer

`machines/backplane.machine` is the real machine's design (`../hw/design.md`)
as far as it is modelled: the W65C02S and north bridge, 1 MB of RAM in four
modules, the 512 KB flash, the DMA controller, the video card ($C480: VGA
640x480 from banks 8-B: text, bitmaps, tiles and sprites), the USB card
($C580: keyboard, mouse, game port; the WebUI types on it), the mass storage
card ($C380: an SD card behind an RP2350; `--sd FILE` puts a card image in its
slot, written through) and the Console card (the front
panel at $C700: its breakpoint and HALT stop at the debugger as "panel"), with a
6551 at $C300 standing in for the serial card. At reset the north bridge maps the top 16 KB of the flash at
$C000-$FFFF, read-only, so the ROM image is the whole flash with the BIOS in
its last 16 KB ($C800-$FFFF is visible; $C000-$C7FF is the I/O hole):

```
make -C ../backplane/bios
./build/release/apps/emu/emulator --machine machines/backplane.machine ../backplane/bios/bios.rom
```

The BIOS (`../backplane/bios`) copies itself into RAM block 63, switches W3
to the copy, turns on the video card's 80x30 text screen (IBM VGA font,
CGA colours) and starts the Woz Monitor on the 6551; everything the monitor
prints goes to the screen as well (`--screen FILE` to see it). Its font is
CC BY-SA 4.0, not MIT: `../backplane/bios/font/README.md`.
`--apple-rom FILE` plugs your own Apple IIe ROM into the cartridge (ROM B): one
16 KB file of `$C000-$FFFF`, or the two 8 KB halves as `CD,EF`; `--apple-chars
FILE` adds its 4 or 8 KB character ROM. `C806R` at the monitor copies the ROM
into bank E and enters Apple II mode (in progress: docs/EMULATOR_PLAN.md).
Apple's ROMs are copyrighted: nothing here ships them.

`--sd FILE` puts a card image in the mass storage card's slot, written through
to the file. `C809R` at the monitor lists the card (a FAT card's root directory),
and `C80CR` loads its `/BOOT.BIN` at `$1000` by DMA and calls it. A program
booted this way prints through `$C80F` (ECHO) and can return to the monitor with
RTS. Make a card with `mkfs.fat -C card.img 32768` and `mcopy`.

`C803R` at the monitor runs the video demo from flash (`../backplane/bios/demo.s`):
a scrolling tile landscape, twelve bouncing sprites whose table is copied by DMA
every vertical blank, a caption layer and a palette fade. Any key goes back to
the monitor, with its screen as it was. `C680.C684` shows the
north bridge's windows (`00 01 02 7F 7F` after boot). Add
`--symbols ../backplane/bios/bios.rom.lbl` for its labels in the debugger.

A machine is one line per chip; see `machines/apple1.machine` and
`components/machine/include/machine.h` for the format. ROM image paths in a
description are relative to the working directory, and a ROM on the command
line replaces the first one.

## `.sid` files

`sidcheck` plays C64 `.sid` tunes (PSID and RSID) on the sound card, headless,
the way hw/design.md (".sid playback") has a host program do it: the tune
loaded through RAMDATA, a stub standing in for the C64's KERNAL, the clock
from the file, then CTRL RUN. There is no sound yet, so it reports what each
tune did to the SIDs: OK, PARTIAL (it wants a third SID, or a second one
where the card has none), SILENT, STALLED or JAM, with the frames played, the
voices that sounded, and the C64 I/O the tune reached that the card lacks:

```
./build/release/apps/sidcheck/sidcheck [--seconds N] [--song N] [--dump] tune.sid...
./build/release/apps/sidcheck/sidcheck --wav out.wav [--model 6581|8580] [--mute 23] tune.sid
```

`--dump` prints the SIDs' registers after each play call, for comparing with
a reference player. `--wav` writes the card's sound (sid.h's output stage:
the waveform DACs, envelopes, filter and mixer), 16-bit stereo at 48 kHz;
`--model` overrides the chip the file asks for, and `--mute` leaves voices out. Tunes are their composers' copyright: keep them in the
gitignored `test/host/fixtures/SIDs/`, where `test_sid_corpus` plays them all.

## WebAssembly

The same core, as an ES module for the WebUI (`apps/web`): `Session`
(`components/session`) behind embind, with the backplane machine and BIOS
built in. It builds in a pinned emsdk image, so only Docker is needed:

```
tools/web_build.sh          # build/web/apps/web/oo1.mjs + oo1.wasm, then ctest --preset web
```

The WebUI that runs it (CRT terminal now; front panel and debugger next)
is `apps/web/ui`: `npm install && npm run dev` there, after the build above.
It reaches the machine only through `MachinePort` (`docs/MACHINE_PORT.md`).

`ctest --preset web` runs the host suites that need no fixtures or Python
under node, plus `apps/web/test/boot.mjs`, which boots the BIOS through the
module as the browser will. It runs ~21 MHz under node: real time at 7.16 MHz
takes about a third of a core.

## Layout

```
components/
  emu_log/     TAG-scoped printf logging (EMU_LOGE/W/I/D; EMU_LOGV with EMU_TRACE)
  bus/         Bus (read/write for the CPU, poke for devices); Device; Decoder + NorthBridge
  cpu6502/     Cpu6502: NMOS 6502 / W65C02S, bus-cycle accurate
  devices/     Ram, Rom, Pia6821, Via6522, Acia6551 (W65C51N), BackplaneBridge; Hd44780 and Ps2Keyboard on a 6522's pins
  machine/     Machine (built from a description), Console, apple1::Io, load_image, load_hex
  debug/       Debugger, disassembler, ld65 symbols, BusTrace
  session/     Session: a machine run a budget of cycles at a time (the WebUI's core)
machines/      plain-text machine descriptions (apple1.machine is the built-in default; eater*.machine; backplane.machine)
apps/emu/      terminal keyboard/display, run loop, main
apps/web/      the WebAssembly module (embind over Session), its Dockerfile and node test
apps/web/ui/   the WebUI: TypeScript + Vite, CRT terminal, MachinePort (its own README)
cmake/         roms.cmake: the backplane BIOS, assembled with cc65
test/host/     tests (test/host/README.md)
tools/         fetch_fixtures.py, trace_diff.py, web_build.sh
docs/          plan, results, BUS_TRACE.md
```

Presets: `dev` (Debug, ASan/UBSan, tests), `release`, `trace` (`dev` with
`EMU_LOGV` compiled in), and `web` (Emscripten; run through `tools/web_build.sh`).

## The core

`ooe::emu::Cpu6502` works through a `Bus`:

```cpp
class Bus
{
public:
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t data) = 0; // CPU stores
    virtual void poke(uint16_t addr, uint8_t data);      // devices, loaders
    virtual uint8_t peek(uint16_t addr) const = 0;       // debugger: no side effects
};

Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);   // or Model::Nmos6502
cpu.reset();          // the 7-cycle reset sequence; PC from $FFFC
unsigned n = cpu.step();  // one instruction; returns its cycles
cpu.irq();            // taken unless I is set; also ends WAI
cpu.nmi();
cpu.regs();           // pc, s, a, x, y, p
cpu.cycles();         // since construction
cpu.state();          // Running, Waiting (WAI), Stopped (STP)
cpu.set_observer(&o); // every access as a BusCycle, with SYNC and VPB
```

Every cycle is the one bus access the chip makes, dummy reads and NMOS
write-backs included, so a cycle count is just the number of accesses and
the traffic can be compared with a logic analyser. Both models pass Harte's
per-cycle tests and Dormann's functional tests (`test/host`). The NMOS
model runs the undocumented opcodes as the die does (LAX, SAX, DCP, the
unstable ANE and SHA with Harte's constants), for the sound card's 6510;
its twelve jams stop the CPU until reset. The W65C02S's undefined opcodes
are WDC's NOPs.

Derived from [gianlucag/mos6502](https://github.com/gianlucag/mos6502)
(MIT, `licenses/mos6502_LICENSE`).

## Links

http://en.wikipedia.org/wiki/MOS_Technology_6502

http://www.6502.org/documents/datasheets/mos/

http://www.mdawson.net/vic20chrome/cpu/mos_6500_mpu_preliminary_may_1976.pdf

http://rubbermallet.org/fake6502.c
