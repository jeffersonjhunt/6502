# Host tests

Tests for the CPU core, the Apple-1 machine and the terminal app. They need a
C++17 compiler; `cc65` (for the ROM suites) and `python3` (for the app suite)
are optional. From `emulator/`:

```sh
python3 tools/fetch_fixtures.py          # optional; without it two suites skip
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run a suite directly for its full output:

```sh
./build/dev/test/host/test_cpu_smoke
HARTE_VERBOSE=1 ./build/dev/test/host/test_harte wdc65c02  # every failing case, not just the first
```

## Suites

| Suite | Covers | Fixtures |
|---|---|---|
| `test_cpu_smoke` | hand-checked datasheet facts: reset vector, N/Z, binary overflow, valid-BCD ADC/SBC, NMOS `JMP ($xxFF)`, JSR/RTS, what BRK and IRQ push | none, always runs |
| `test_dormann_*` | Klaus Dormann's functional tests, three runs: `6502_functional_test` on each model, `65C02_extended_opcodes_test` on the W65C02S. Traps on the first failure | `*.bin` |
| `test_harte_6502`, `test_harte_wdc65c02` | Tom Harte's SingleStepTests: one instruction from a random state; registers, RAM and **every bus cycle** (address, data, direction) compared | `harte/<cpu>/*.txt` |
| `test_pia` | the 6821: DDR select, input/output mixing, read-only flags, clear-on-read, write handler, IRQ enable | none |
| `test_decoder` | offsets, unmapped space, later-wins overlap, read-only ROM, module IDs, the north bridge, IRQ wiring | none |
| `test_machine` | machine descriptions: the Apple-1 map they build, options, every parse error naming its line, the built-in default | none |
| `test_loader` | `load_image`: short files, missing files, images that don't fit | none |
| `test_rom_boot` | Woz Monitor, Integer BASIC and Applesoft booted headless with scripted keys and a captured screen | ROMs, assembled by this build if `ca65` is on the PATH |
| `test_app_tty` | the real app in a pty: Ctrl-C and SIGTERM restore the terminal and exit 0 without leaks (LeakSanitizer); a missing ROM exits with an error; nothing is logged without `--log`, and `--log FILE` gets the run; a bad `--machine` file stops it with file and line | ROMs, `python3` |

Suites with fixtures exit 77 when their fixtures are absent, which CTest reports
as **Skipped**, not Passed. Check the skipped count: a green run with two
skips has only run the smoke suite. `test_harte` fails, rather than passes,
if its fixture directory exists but holds no cases.

## Fixtures

`tools/fetch_fixtures.py` fetches both from pinned commits into `fixtures/`,
which is gitignored. Neither is vendored: Dormann's tests are GPL-3.0 and the
full Harte set is ~1 GB. Dormann's binary is checked against a sha256, because
`test_dormann` hard-codes its success addresses. Harte defaults to 100 cases
per opcode for each CPU (~7 MB on disk); `--cases 1000` is ~69 MB and still
quick to fetch. Fixtures are format 2 (with bus cycles) since Phase 4; older
ones fail with "re-run tools/fetch_fixtures.py".

## Things to know

- **Everything gets the same flags.** Code under test and tests alike build
  with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`. Phase 0
  exempted the core until Phase 2 restyled it.
- **ASan/UBSan come from the `dev` preset** (`EMU_SANITIZE`). `test_app_tty`
  relies on them: LeakSanitizer is what turns a leak at exit into a failure.
  Dormann takes ~1 s with them; it took ~16 s while easylogging++ was on the
  hot path.
- **Phase 2 changed no results.** At 1,000 cases per opcode, the restyled
  core's `HARTE_VERBOSE` output is byte-identical to the `beta` core's (860
  failing cases, all invalid-BCD decimal), and Dormann traps at the same
  address after the same 30,646,176 instructions.
- **Dormann has blind spots.** It only tests INX on $FE→$FF→$00→$01, so an N
  flag taken from bit 6 instead of bit 7 passes it. It cannot see bus
  traffic either: an NMOS read-modify-write that re-reads instead of
  writing back, or a wrong invalid-BCD result, passes Dormann and fails
  Harte (both checked by mutation in Phase 4).
  Don't read a Dormann pass on its own as a clean core.
