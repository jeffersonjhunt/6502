6502 Playground
===========================================================

This is just a bunch of random sudo projects for working on the MOS 6502 CPU.

## External Refs ##

https://www.applefritter.com/replica/chapter7

## Emulator ##

To run the emulator

```
mkdir build
cd build
cmake ../emulator
make
ln -s ../emulator/logging.cfg
./apps/emulator
```

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
