# Emulator Plan — refactor toward the backplane system

Turns the Apple-1 emulator into one that can stand in for the real 6502
backplane computer (`hw/modules.md`, `hw/console.md`) during bring-up, and
brings it in line with the house C++ style used by `pinled`
(`ooe-site/projects/001/firmware`).

The code as it stood before any of this is tagged **`beta`**.

## 1. Where it is today

A jump-table NMOS 6502 core (derived from gianlucag/mos6502), a flat 64 KB
`Memory` that special-cases the Apple-1 PIA keyboard flag, a raw-tty keyboard,
a stdout display, and a main loop that loads `ibasic.rom` at `$8000` and runs
16 steps per tick. It boots Woz Monitor and Integer BASIC "for the most part".

Known problems at `beta`, and what Phase 1 found behind each (every fix has
a test that was seen failing first):

- **The Applesoft ROM "does not work"**: two emulator bugs.
  - `Memory` let the CPU write over ROM. Applesoft's cold start sizes RAM by
    complementing a byte per page until one doesn't change, so it never found
    the top of RAM and looped at `$FCE8`. ROM is now read-only to the CPU
    (`Memory::SetReadOnly`, set up by `LoadApple1`).
  - A store to `DSP` only printed if the stored byte had bit 7 set. On the
    Apple-1 that bit is the display's busy line, and every store prints.
    Integer BASIC lost the last letter of every error message (`EN ERR`), and
    commit 3612a35 had commented out Applesoft's `and #$7F` in `RDKEY` to keep
    its echo visible. That broke CR detection, because keys then arrived as
    `$8D`. Fixed in `Memory`, with the 6821 rule that `$D012` is the DDR while
    CRB bit 2 is 0, and `io.s` restored to the original.
- **Phantom key at reset.** Woz Monitor stores `$A7` to `KBDCR`, and that set
  key-ready. Bits 6-7 of both PIA control registers are now read-only to the
  CPU; peripherals set them through `BusInterface::Poke`.
- **ROM loading.** It copied a full 128-byte buffer after a short read, logged
  the wrong address, ran 64 KB of zeros when the file was missing, and would
  have wrapped an oversized image into zero page. Replaced by
  `ooe::LoadImage`, which refuses instead.
- **ROM path** was hard-coded relative to the working directory. It is now
  `emulator [rom]`, and failing to load is fatal.
- **Ctrl-C** was SIGINT (raw mode keeps ISIG), which killed the process
  before the terminal was restored. SIGINT/SIGTERM now end `Run()`, and
  everything is owned by `main()`, so destructors run with no leaks. `LoadROM`
  and `shouldPause` are gone.
- Still open, for later phases: no cycle counting, NMOS opcodes only, and
  invalid-BCD decimal results (Phase 4).

## 2. House style (from `pinled`)

| Area | Convention |
|---|---|
| Namespace | `namespace ooe::emu { … }`, body indented 4 spaces |
| Naming | `PascalCase` types, `snake_case()` methods, `member_` with `{}` initialisers, `static constexpr` constants |
| Braces | Allman, spaces not tabs |
| Headers | `#pragma once`, Doxygen `@file/@author/@brief/@version/@date/@copyright (MIT)` and a paragraph on *why* |
| Layout | `components/<name>/{include/<name>.h, <name>.cpp, CMakeLists.txt}` |
| Errors | return codes, `new (std::nothrow)`; no exceptions on the hot path |
| Logging | `TAG`-scoped printf-style macros; nothing on the hot path unless compiled in |
| Build | explicit source lists, C++17, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`, ASan/UBSan on by default, `CMakePresets.json`, `.clangd` |
| Tests | `test/host/harness.h` (`TEST`/`CHECK_EQ`), CTest, hardware-free core |
| Docs | `docs/REQUIREMENTS.md` + plan; requirement IDs cited in code |

## 3. Target layout

```
emulator/
  CMakeLists.txt  CMakePresets.json  .clangd  README.md
  docs/           REQUIREMENTS.md  EMULATOR_PLAN.md  MACHINES.md
  components/
    cpu6502/      6502/65C02 core: registers, decode table, cycle counts
    bus/          Device interface + address decoder (ranges / module IDs)
    devices/      ram, rom, pia6821, via6522, acia6551, console
    machine/      machine descriptions: apple1, eater, ooe-backplane
    emu_log/      TAG-scoped logger (replaces easylogging++)
  apps/emu/       terminal frontend: raw tty, throttle, debugger
  test/host/      harness.h, test_*.cpp, fixtures/
```

Dependency direction, no cycles:

```
apps ─▶ machine ─▶ devices ─▶ bus ─▶ cpu6502
```

`cpu6502` and `bus` include nothing from the terminal or the logger — the same
property that keeps `pinled_schema` testable off-target.

## 4. Phases

### Phase 0 — tests before refactoring

Measure the existing core before changing it.

1. `test/host/harness.h` and a CTest project with the full warning set and
   sanitizers on test code. The core is built without `-Werror` until Phase 2
   brings it under the house flags.
2. Hand-written smoke cases that need no fixtures, so every machine runs
   *something*.
3. Klaus Dormann's `6502_functional_test` against the unchanged core.
4. Tom Harte's SingleStepTests (`65x02/6502/v1`), a pinned subset per legal
   opcode, comparing registers and RAM (cycles recorded, not yet compared).
5. Record what fails (`docs/PHASE0_RESULTS.md`). That list is Phase 1.

### Phase 1 — fix known bugs

Each bug from §1 and each Phase 0 failure gets a failing test first.

### Phase 2 — restyle and restructure

No behaviour change; Phase 0 suites must stay identical. Rename to
`ooe::emu::Cpu6502` (`step/reset/irq/nmi`), move to the component layout, add
Doxygen headers, explicit CMake sources, presets and `.clangd`. Replace
easylogging++ and `fmt` with `emu_log`; bus tracing becomes compile-time so it
costs nothing when off. Bring the core under `-Werror`.

**Done 2026-10-18.** Layout as in §3, except `devices/`, which waits for
Phase 3; the Apple-1 map lives in `components/machine` until then, and the
terminal keyboard/display in `apps/emu`. The opcode table was generated from
the old one. Results are unchanged: Harte's `HARTE_VERBOSE` output at 1,000
cases/opcode is byte-identical to `beta`'s, and Dormann traps at $3469 after
the same 30,646,176 instructions. The core also gained `regs()`/`set_regs()`,
which tests use instead of going through the reset vector, and Phase 5's
debugger will need. Mutations of the restyled core (an unsigned SBC borrow,
`JMP ($xxFF)` without the page wrap) are caught. The app now logs only with
`--log FILE`, and `logging.cfg` is gone.

### Phase 3 — a bus that matches the hardware

- `Device`: `read(addr)`, `write(addr, data)`, `tick(cycles)`, an IRQ line.
- `Bus` owns a decode table; devices stop hard-coding addresses. The PIA's
  clear-on-read moves out of `Memory` into a `Pia6821` device.
- North bridge model: 20-bit address bus and 4-bit module ID (PS0..3), so the
  emulator decodes into module IDs 0x0..0xF exactly as the backplane will.
- Machine descriptions (text/JSON) choose the map, ROMs and devices: one
  binary runs the Apple-1, the Eater board, and the backplane system.

**3a done 2026-10-18.** `Device` and `Decoder` in `components/bus`, with
mappings in 20-bit physical space, later-wins overlap, a module ID per
mapping and an IRQ wire per mapping. A16-A19 come from a pluggable
`NorthBridge`, which for now is the identity (A16-A19 low) until that is
decided (§5). `components/devices` has `Ram`, `Rom` and `Pia6821`, with DDR
select, read-only flags, clear-on-read and a CA1/CB1 strobe. Machines are
plain text (`machines/apple1.machine`, compiled in as the default;
`emulator --machine FILE`). The Apple-1 is now built from that description,
and `apple1::Io` is the one keyboard/screen adapter shared by the app and
the boot tests. `tick()` is deliberately left out until Phase 4 has cycles
to count. The decoder is a reverse linear scan: the release build runs
Applesoft at ~88M instructions/s, about 300x a 1 MHz 6502, so nothing
faster is warranted yet. Harte's known gap (decimal ADC/SBC on invalid BCD)
is now set aside and reported rather than failed (`HARTE_STRICT=1`
restores it), so the suite is green and any new failure stands out.

**3b, still to do:** a `Via6522` (needs Phase 4's cycles for its timers) and
an `Acia6551`, then `eater.machine` and `backplane.machine`. The backplane
file waits on the north bridge's A16-A19 rule and the module address map.

### Phase 4 — timing and CPU variant

- Per-instruction cycle counts with page-cross and branch-taken penalties;
  throttle to a real clock (e.g. 1 MHz). Harte's cycle traces become checked.
- **W65C02S as the primary core** — the real board's CPU (decided
  2026-10-18). Adds BRA, PHX/PLX/PHY/PLY, STZ, TRB/TSB, WAI/STP,
  RMB/SMB/BBR/BBS, `(zp)` addressing, BIT #imm/zp,X/abs,X, INC A/DEC A,
  fixed `JMP ($xxFF)`, D cleared on interrupts, valid N/Z in decimal mode and
  its extra decimal cycle; undefined opcodes as WDC's sized NOPs. Verified
  against Harte's `65x02/wdc65c02` set and Dormann's
  `65C02_extended_opcodes_test`. NMOS stays as a per-machine option for the
  Apple-1 machine.
- RDY, SYNC and VPB as bus signals visible to the console and bus spy.

**Done 2026-10-18,** further than planned: the core is bus-cycle accurate,
not just cycle-counted. Every cycle is the one bus access the silicon makes,
dummy reads and NMOS write-backs included, so cycles are simply accesses and
Phase 5 can diff traffic against the bus spy. Checked per cycle (address,
data, direction) against Harte for both models: 151,000 NMOS and 254,000
WDC cases at 1,000/opcode, all passing. Dormann's functional test passes on
both models and the 65C02 extended test on the W65C02S. NMOS decimal mode
now matches silicon on invalid BCD too (Clark's sequences), which closes the
gap Phase 0 found rather than setting it aside. Also: `cpu w65c02s` in
machine files (`machines/apple1-65c02.machine`, a Replica-1), WAI/STP,
7-cycle reset/IRQ/NMI sequences, `Device::tick(cycles)`, and a cycle-sliced
run loop with `--clock HZ` (default 1 MHz, measured at 1.000 MHz;
`--clock 0` runs ~166 MHz).

Moved to Phase 5: RDY, SYNC and VPB. They only matter to whatever watches
the bus, which is the bus trace and console that Phase 5 builds.

To confirm on hardware: two WDC details come only from Harte. The decimal
extra cycle in immediate mode reads $007F (ADC) or $0000 (SBC), and the
$5C/$DC/$FC NOPs take 4 cycles (WDC's own documentation gives $5C 8).

### Phase 5 — bring-up tooling

- Debugger mirroring the console panel: run/halt/step, store/fetch, break,
  register and memory dump.
- Bus trace in the ESP32 bus spy's format, so emulator and board can be
  diffed cycle by cycle.
- Intel HEX / raw loaders and ca65 `.lbl` symbols for labelled traces.

## 5. Open decisions

1. ~~CPU on the real board~~ — **W65C02S** (decided 2026-10-18). Its
   address bus is 16 bits, so A16..A19 on the backplane must come from the
   north bridge (a bank/segment register?). How those bits are produced is
   now the open question for Phase 3's decoder.
2. ~~Machine description format~~ — **plain text**, one line per chip
   (decided 2026-10-18; `components/machine/include/machine.h`).
3. ~~`fmt`~~ — dropped in Phase 2, with easylogging++; `emu_log` is
   printf-style, like pinled's `ESP_LOGx`.
