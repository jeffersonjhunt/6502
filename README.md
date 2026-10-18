6502 Playground
===========================================================

This is just a bunch of random sudo projects for working on the MOS 6502 CPU.

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
error if the ROM can't be loaded. Nothing is logged unless you ask:
`--log FILE` writes the run to FILE, and `-v` adds per-key detail.

### Tests ###

Tests for the CPU core (smoke, Klaus Dormann, Tom Harte), the Apple-1 machine
and the app itself. See `emulator/test/host/README.md`.

```
cd emulator
python3 tools/fetch_fixtures.py
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

The refactor plan is `emulator/docs/EMULATOR_PLAN.md`; the baseline it starts
from is tagged `beta`.
