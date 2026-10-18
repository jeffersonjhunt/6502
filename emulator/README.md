## 6502 emulator

A 6502 core, the Apple-1 memory map, and a terminal front end that runs the
Apple-1 ROMs in `../apple1/src`. It is being refactored to stand in for the
backplane 6502 computer (`../hw/`) during bring-up; see
[`docs/EMULATOR_PLAN.md`](docs/EMULATOR_PLAN.md).

```
cmake --preset release && cmake --build --preset release
./build/release/apps/emu/emulator ../apple1/src/ibasic.rom
```

## Layout

```
components/
  emu_log/     TAG-scoped printf logging (EMU_LOGE/W/I/D; EMU_LOGV with EMU_TRACE)
  bus/         Bus: read/write for the CPU, poke for devices and loaders
  cpu6502/     Cpu6502: NMOS core, one instruction per step()
  machine/     apple1::Memory (RAM, read-only ROM, 6821 PIA rules), load_image
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

Cpu6502 cpu(bus);
cpu.reset();   // PC from $FFFC
cpu.step();    // one instruction
cpu.irq();     // taken unless I is set
cpu.nmi();
cpu.regs();    // pc, s, a, x, y, p
```

Decoding is a 256-entry table pairing an addressing mode (consumes the
operand, returns the effective address) with an operation. All 151
documented opcodes and decimal mode are implemented; undocumented opcodes
run as one-byte NOPs. There are no cycle counts yet, and decimal ADC/SBC on
invalid BCD differs from silicon (`docs/PHASE0_RESULTS.md`). Both are Phase 4,
with the W65C02S the real board uses.

Derived from [gianlucag/mos6502](https://github.com/gianlucag/mos6502)
(MIT, `licenses/mos6502_LICENSE`).

## Links

http://en.wikipedia.org/wiki/MOS_Technology_6502

http://www.6502.org/documents/datasheets/mos/

http://www.mdawson.net/vic20chrome/cpu/mos_6500_mpu_preliminary_may_1976.pdf

http://rubbermallet.org/fake6502.c
