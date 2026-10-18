# Host tests

CPU conformance tests that build and run on any machine with a C++17 compiler
and `libfmt-dev`. No terminal, no ROMs, no hardware (`docs/EMULATOR_PLAN.md`
§4, Phase 0).

```sh
python3 tools/fetch_fixtures.py          # optional; without it two suites skip
cmake -S test/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Run a suite directly for its full output:

```sh
./build-host/test_cpu_smoke
HARTE_VERBOSE=1 ./build-host/test_harte  # every failing case, not just the first
```

## Suites

| Suite | Covers | Fixtures |
|---|---|---|
| `test_cpu_smoke` | hand-checked datasheet facts: reset vector, N/Z, binary overflow, valid-BCD ADC/SBC, NMOS `JMP ($xxFF)`, JSR/RTS, what BRK and IRQ push | none, always runs |
| `test_dormann` | Klaus Dormann's `6502_functional_test`: every documented opcode and mode, traps on the first failure | `6502_functional_test.bin` |
| `test_harte` | Tom Harte's SingleStepTests: one instruction from a random state, registers and RAM compared, per opcode | `harte/*.txt` |
| `test_memory` | Apple-1 memory map: read-only ROM, PIA control-register flags, `DSP` output and DDR | none |
| `test_loader` | `LoadImage`: short files, missing files, images that don't fit | none |
| `test_rom_boot` | Woz Monitor, Integer BASIC and Applesoft booted headless with scripted keys and a captured screen | ROMs, assembled by this build if `ca65` is on the PATH |
| `test_app_tty` | the real app in a pty: Ctrl-C and SIGTERM restore the terminal and exit 0 without leaks (LeakSanitizer); a missing ROM exits with an error | ROMs, `python3` |

Suites with fixtures exit 77 when their fixtures are absent, which CTest reports
as **Skipped**, not Passed. Check the skipped count: a green run with two
skips has only run the smoke suite. `test_harte` fails, rather than passes,
if its fixture directory exists but holds no cases.

## Fixtures

`tools/fetch_fixtures.py` fetches both from pinned commits into `fixtures/`,
which is gitignored. Neither is vendored: Dormann's tests are GPL-3.0 and the
full Harte set is ~1 GB. Dormann's binary is checked against a sha256, because
`test_dormann` hard-codes its success address. Harte defaults to 100 cases per
opcode (~2 MB on disk); `--cases 1000` is ~23 MB and still quick to fetch.

## Things to know

- **The core is built as tagged `beta`.** The test code gets
  `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`. The core doesn't
  yet, and its headers are `SYSTEM`, until Phase 2.
- **`ELPP_NO_CHECK_MACROS` is required.** easylogging++ otherwise defines its
  own `CHECK`/`CHECK_EQ` over `harness.h`'s, and with logging off they do
  nothing. `test_support.h` `#error`s without it.
- **ASan/UBSan are on by default** (`-DEMU_SANITIZE=OFF` to disable). Dormann
  takes ~16 s with them.
- **Dormann has blind spots.** It only tests INX on $FE→$FF→$00→$01, so an N
  flag taken from bit 6 instead of bit 7 passes it. Harte catches that one.
  Don't read a Dormann pass on its own as a clean core.
