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
Apple-1); `--clock 0` runs as fast as the host allows.

## Bring-up tools

Press **Ctrl-]** to stop the machine at a `debug>` prompt (`h` lists the
commands). They follow the backplane's console panel (`../hw/console.md`):
`r` run, `s [N]` step, `f ADDR [N]` fetch, `st ADDR BYTE...` store, plus
`x` registers, `l` disassemble, `b`/`d` breakpoints, `t` trace on/off and
`q` quit. Addresses take labels.

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
modules, and the 512 KB flash, with a 6551 at $C300 standing in for the
serial card. At reset the north bridge maps the top 16 KB of the flash at
$C000-$FFFF, read-only, so the ROM image is the whole flash with the BIOS in
its last 16 KB ($C800-$FFFF is visible; $C000-$C7FF is the I/O hole):

```
./build/release/apps/emu/emulator --machine machines/backplane.machine bios.rom
```

There is no BIOS in the repo yet; `test_backplane` builds a small one that
shadows itself into RAM.

A machine is one line per chip; see `machines/apple1.machine` and
`components/machine/include/machine.h` for the format. ROM image paths in a
description are relative to the working directory, and a ROM on the command
line replaces the first one.

## Layout

```
components/
  emu_log/     TAG-scoped printf logging (EMU_LOGE/W/I/D; EMU_LOGV with EMU_TRACE)
  bus/         Bus (read/write for the CPU, poke for devices); Device; Decoder + NorthBridge
  cpu6502/     Cpu6502: NMOS 6502 / W65C02S, bus-cycle accurate
  devices/     Ram, Rom, Pia6821, Via6522, Acia6551 (W65C51N), BackplaneBridge; Hd44780 and Ps2Keyboard on a 6522's pins
  machine/     Machine (built from a description), Console, apple1::Io, load_image, load_hex
  debug/       Debugger, disassembler, ld65 symbols, BusTrace
machines/      plain-text machine descriptions (apple1.machine is the built-in default; eater*.machine; backplane.machine)
apps/emu/      terminal keyboard/display, run loop, main
test/host/     tests (test/host/README.md)
tools/         fetch_fixtures.py, trace_diff.py
docs/          plan, results, BUS_TRACE.md
```

Presets: `dev` (Debug, ASan/UBSan, tests), `release`, and `trace` (`dev` with
`EMU_LOGV` compiled in).

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
model's undocumented opcodes are two-cycle NOPs, which is not what silicon
does; the W65C02S's undefined opcodes are WDC's NOPs.

Derived from [gianlucag/mos6502](https://github.com/gianlucag/mos6502)
(MIT, `licenses/mos6502_LICENSE`).

## Links

http://en.wikipedia.org/wiki/MOS_Technology_6502

http://www.6502.org/documents/datasheets/mos/

http://www.mdawson.net/vic20chrome/cpu/mos_6500_mpu_preliminary_may_1976.pdf

http://rubbermallet.org/fake6502.c
