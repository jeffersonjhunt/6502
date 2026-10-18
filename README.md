6502 Playground
===========================================================

This is just a bunch of random sudo projects for working on the MOS 6502 CPU.

## External Refs ##

https://www.applefritter.com/replica/chapter7

## Emulator ##

To run the emulator (needs `libfmt-dev`; the ROMs need `cc65`)

```
make -C apple1/src roms
mkdir build
cd build
cmake ../emulator
make
ln -s ../emulator/logging.cfg
./apps/emulator ../apple1/src/ibasic.rom      # or applesoft.rom
```

At the `\` prompt, `E000R` starts BASIC. Ctrl-C quits. With no argument the
emulator looks for `ibasic.rom` in the current directory, and it exits with an
error if the ROM can't be loaded.

### Tests ###

CPU conformance tests (smoke, Klaus Dormann, Tom Harte) build without the
emulator front end. See `emulator/test/host/README.md`.

```
cd emulator
python3 tools/fetch_fixtures.py
cmake -S test/host -B build-host && cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The refactor plan is `emulator/docs/EMULATOR_PLAN.md`; the baseline it starts
from is tagged `beta`.
