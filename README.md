6502 Playground
===========================================================

This is just a bunch of random sudo projects for working on the MOS 6502 CPU.

Writing code for the OO-1 (the backplane computer and OODOS)? Read [AGENTS.md](AGENTS.md) first; the books are in
[docs/](docs/README.md).

## External Refs ##

https://www.applefritter.com/replica/chapter7

## Emulator ##

To run the emulator (needs CMake and a C++17 compiler; the ROMs need `cc65`)

```
make -C apple1/src roms
cd emulator
cmake --preset release && cmake --build --preset release
./build/release/apps/emu/emulator ../apple1/src/ibasic.rom     # or applesoft.rom
```

At the `\` prompt, `E000R` starts BASIC. Ctrl-C quits. With no argument the
emulator looks for `ibasic.rom` in the current directory, and it exits with an
error if the ROM can't be loaded. `--machine FILE` runs a different machine
description (`emulator/machines/`; `apple1-65c02.machine` is a Replica-1 with
a W65C02S). It runs at 1 MHz; `--clock HZ` changes that and `--clock 0` runs
flat out. Nothing is logged unless you ask:
`--log FILE` writes the run to FILE, and `-v` adds per-key detail.

`emulator/machines/eater.machine` runs Ben Eater's breadboard computer and
the programs in `eater/src` (see `emulator/README.md`).

`emulator/machines/backplane.machine` is the backplane computer being designed
in `hw/design.md`; `backplane/bios` is its BIOS, which boots an SD card's
`/BOOT.BIN` (OODOS, on an OODOS card) or else the Woz Monitor (`make -C backplane/bios`, then see `emulator/README.md`).

Ctrl-] stops it at a debugger prompt (`h` for help). `--halt`, `--break`,
`--symbols`, `--load` and `--trace` are for bring-up; see
`emulator/README.md`.

### Tests ###

Tests for the CPU core (smoke, Klaus Dormann, Tom Harte), the Apple-1 machine
and the app itself. See `emulator/test/host/README.md`.

```
cd emulator
python3 tools/fetch_fixtures.py
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

Two tiers, run by `tools/check.sh` (it says what each step took and which failed):

- **`tools/check.sh quick`**, before a build goes to staging, a few minutes: the oodos checks; every native test,
  built optimised without the sanitizers (`--preset fast`, under a minute); the WebAssembly build and its tests;
  vitest; the e2e smoke set (the tests tagged `@smoke`, `npm run e2e:smoke`). `--url URL` runs the smoke set
  against the deployed page too.
- **`tools/check.sh full`**, only for a production release candidate: the same, but the native tests under
  ASan/UBSan (`--preset dev`, 30 minutes or more), the whole e2e suite (and against `--url URL`), and interop
  (cpmtools reading the cards OODOS writes, in Docker). The sanitizer tests run one a CPU; on a host shared with
  other work, `--jobs 4` keeps them to 4 at once.

The sanitizers are too slow for anything but a release candidate. Between those, a sanitizer build is for an error
found in testing that the logs can't explain: `cmake --preset dev && cmake --build --preset dev`, then that one test.

The refactor plan is `emulator/docs/EMULATOR_PLAN.md`; the baseline it starts
from is tagged `beta`.

### License ###

MIT (`LICENSE`), except the backplane BIOS's font, `backplane/bios/font/vga8x16.bin`: IBM VGA 8x16 from VileR's
[Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/), under CC BY-SA 4.0. It is inside
`bios.rom` and the emulator's web build, which carry it under that license; see `backplane/bios/font/README.md`.
And `oodos/c/_printf.s`: cc65's, altered for OODOS (its changed lines are marked), under cc65's zlib license, which
the file keeps. A C program built for OODOS also links cc65's own library, `none.lib`, under the same license.
