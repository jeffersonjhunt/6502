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
  devices/     Ram, Rom, Pia6821
  machine/     Machine (built from a description), apple1::Io, load_image
machines/      plain-text machine descriptions (apple1.machine is the built-in default)
apps/emu/      terminal keyboard/display, run loop, main
test/host/     tests (test/host/README.md)
tools/         fetch_fixtures.py
docs/          plan and results
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
};

Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);   // or Model::Nmos6502
cpu.reset();          // the 7-cycle reset sequence; PC from $FFFC
unsigned n = cpu.step();  // one instruction; returns its cycles
cpu.irq();            // taken unless I is set; also ends WAI
cpu.nmi();
cpu.regs();           // pc, s, a, x, y, p
cpu.cycles();         // since construction
cpu.state();          // Running, Waiting (WAI), Stopped (STP)
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
