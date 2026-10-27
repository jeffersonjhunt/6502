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
| `test_cpu_smoke` | hand-checked datasheet facts: reset vector, N/Z, binary overflow, valid-BCD ADC/SBC, NMOS `JMP ($xxFF)`, JSR/RTS, what BRK and IRQ push; reset/IRQ/NMI sequences, WAI/STP; SYNC and VPB as an observer sees them | none, always runs |
| `test_dormann_*` | Klaus Dormann's functional tests, three runs: `6502_functional_test` on each model, `65C02_extended_opcodes_test` on the W65C02S. Traps on the first failure | `*.bin` |
| `test_harte_6502`, `test_harte_wdc65c02` | Tom Harte's SingleStepTests: one instruction from a random state; registers, RAM and **every bus cycle** (address, data, direction) compared | `harte/<cpu>/*.txt` |
| `test_pia` | the 6821: DDR select, input/output mixing, read-only flags, clear-on-read, write handler, IRQ enable | none |
| `test_via` | the 6522: ports, PCR edges, IFR/IER, T1 one-shot (N+1.5) and free-run (N+2) with PB7, T2 and pulse counting; a timer interrupt and a WAI wake-up through the CPU to the exact cycle | none |
| `test_acia` | the W65C51N: stuck TDRE, DTR and transmitter gating, receive and its interrupt, programmed reset | none |
| `test_lcd` | the HD44780 through a 6522 in Eater's 8- and 4-bit wirings, busy/AC and data reads, line wrap and shift; PS/2 bytes on CA1 | none |
| `test_decoder` | offsets, unmapped space, later-wins overlap, read-only ROM, module IDs, the north bridge, routes into the flash and I/O spaces with read-only and VPB, IRQ wiring, devices kept in lockstep with bus cycles; RDY wait states: one access held N cycles, counted by the CPU and seen once on the last cycle, a 6522 timer running through them | none |
| `test_backplane` | the backplane's north bridge (hw/design.md): reset and /BOOTSEL maps, any block in any window, the RO and ROM bits and the cartridge, the I/O hole ahead of the windows, VEC on vector pulls, the /IACK poll, WAIT registers slowing their own module only, MODE/STATUS/ID; the BIOS self-copy boot run as 6502 code on `machines/backplane.machine` | none |
| `test_ds3231` | the DS3231 on its own: BCD registers, a second of CPU cycles, rollovers to the century, leap years 2000 and 2100 (the chip's rule), 12 h mode through noon and midnight, writing the seconds, alarm 1 and alarm 2 in each mask mode, flags cleared by writing 0 and the INT pin, the square wave's edges at 1 Hz to 8.192 kHz, CONV and BSY (and its own 64-second conversion), OSF and EOSC on the battery, aging and the temperature, a clock change keeping the second and a conversion in real time, set_time's milliseconds into the second | none |
| `test_rtc` | the north bridge's clock: the power-on set-up, a TICK each second after its 132-bit read, SEC latching all seven across a year's end, TIE and IRQH bit 5, SET's holding copy and timing and OSF cleared, RTCREG/RTCDATA queued, an alarm's ALARM, RTCDATA unable to change INTCN or RS, SET a strobe a read-modify-write cannot resend, BUSY at 7.16 MHz and across a clock change, Apple II mode hiding it while it keeps time, resets; the debugger's `rtc` with a pending SET; 6502 code on `backplane.machine` reading $C690-$C696 and taking the tick's interrupt; a session keeping the chip through a power cycle | none |
| `test_machine` | machine descriptions: the Apple-1 map they build, options, `space=`, `wait=`, `drq=`, the `northbridge`, `dma` and `console` lines, every parse error naming its line, the built-in default; the 6522/6551 and peripheral lines and their errors; consoles | none |
| `test_eater` | Eater's six programs on the Eater machines: hello-world (3 wirings), keyboard.s typing every printable character onto the LCD, Woz Monitor over serial, blink | ROMs, assembled by this build if `vasm6502_oldstyle` is found |
| `test_loader` | `load_image`: short files, missing files, images that don't fit; `load_hex` (Wikipedia's worked example, CRLF, bad checksum, truncation, data past $FFFF, extended addresses); ld65 label files | none |
| `test_disasm` | lengths against how far the core actually moves PC, every opcode of both models; every opcode reassembled by ca65 to the same bytes, three operand patterns | round trip needs `ca65`/`ld65`, else skipped |
| `test_debugger` | each command, breakpoints in the run loop and `step`, fetch without side effects, store obeying ROM; bus trace lines character for character, through a north bridge | none |
| `web_boot` (`--preset web`) | `apps/web/test/boot.mjs`: the WebAssembly module under node — open errors, boot in frame budgets, serial round trip, bytes as Latin-1, HALT/CONT/run, a breakpoint on a label through the SPI registers, lamps and trace as typed arrays, speed (at least real time), power cycle | Docker (`tools/web_build.sh`) |
| `test_session` | the WebUI's core on the backplane with the BIOS, in 60 Hz frame budgets: boot to the monitor, a serial round trip, HALT/CONT/run through the panel, a breakpoint on a label set through the SPI registers, output arriving while halted, the card's IRQ ending a WAI, lamps, the trace ring, START and power cycling, STP, a machine without a panel, errors | the BIOS (`ca65`/`ld65`) |
| `test_console_card` | the Console card on a backplane machine: registers and masks, KEYS clear-on-read, the key interrupt (IRQH bit 6), LOCK from CTRL and the keyswitch with the remote panel still working, LOAD ADRS/EXAM/DEP in physical memory with the address stepping, the HALT switch and CONT single-step, a program halting itself, fetch breakpoints on all 20 address lines halting before the instruction, read/write breakpoints latching their cycle, the /NMI action, START, lamp duty, DISPLAY and frozen lights | none |
| `test_dma` | the DMA controller: registers and reset, the master enable, memory to memory with HOLD and DEC, single/demand/block against a test card with exact stolen cycles, BURST, TC to the card, STATUS and the interrupt, AUTO, priority, ABORT, timers running through DMA cycles; 6502 code doing a 64 KB copy on the backplane in 131,073 cycles while it waits in WAI, woken by the DMA interrupt | none |
| `test_bios` | the backplane BIOS on `backplane.machine`: boot from flash to the Woz Monitor prompt with the copy matching the flash, the north bridge read through the monitor, a program run with R, the read-only copy, NMI into the monitor (Break turning the clock's tick off), and the /BOOTSEL boot of the same image from RAM | `bios.rom`, assembled by this build if `ca65` is on the PATH |
| `test_rom_boot` | Woz Monitor, Integer BASIC and Applesoft booted headless with scripted keys and a captured screen | ROMs, assembled by this build if `ca65` is on the PATH |
| `test_app_tty` | the real app in a pty: Ctrl-C and SIGTERM restore the terminal and exit 0 without leaks (LeakSanitizer); a missing ROM exits with an error; nothing is logged without `--log`, and `--log FILE` gets the run; a bad `--machine` file stops it with file and line | ROMs, `python3` |
| `test_app_debug` | the debugger in the app: Ctrl-] and `r`, `--halt`/`--break`/`--symbols`, Ctrl-C at the prompt, `--load` HEX and raw (over ROM too), STP, a Console card halt as "panel" and `r` releasing it, `--trace` checked and diffed; bad arguments refused before the terminal goes raw | ROMs and labels, `python3` |
| `test_app_eater` | the app on `eater.machine`: Woz Monitor through the serial port, keyboard.s onto the LCD box; a machine with nothing to talk through is refused | Eater ROMs, `python3` |
| `test_sid_audio` | the SID's output stage: the DAC's zero and the mixer's DC on each model, envelope and volume scaling, 3OFF, the filter's LP/BP/HP slopes, notch and LP+BP, the cutoff curves' ends, resonance's gain, muting, an attack from zero, reset stilling the filter, and the audio path leaving the chip as `clock()` would; on the card: samples per second at either crystal, SID A left and B right, the DC draining, the level, the half-second buffer, a `$D418` sample on each model, the model on both chips | none |
| `test_sid_file` | `.sid` files: the PSID/RSID header, the stub's page, vectors and KERNAL exits, CTRL from the clock and the second SID, refusals; hand-assembled tunes played on the card: 50 Hz, the KERNAL's CIA rate and NTSC frames to the clock, the song in A, a tune's own interrupt at `$0314` or `$FFFE`, play and init under the I/O | none |
| `test_sid_corpus` | every tune in `fixtures/SIDs`: a PSID's play routine called on time for 2 s, no jam, a voice sounding within 10 s; RSIDs run without jamming | the owner's `.sid` files |
| `test_sd` | the mass storage card: registers, start-up, READ/WRITE/FLUSH/IDENTIFY on the raw card with LBA and COUNT advancing, the two buffers' timing to the cycle, BUFPTR, every error, a command while BUSY, card in and out, /RESET, DRQ, a host file written through; 6502 code on a backplane reading by CPU loop and by DMA channel 0 both ways, at a byte a cycle; the session keeping the card through a power cycle | the BIOS (`ca65`/`ld65`) for the last case |
| `test_sd_fat` | the SD card's FAT against images mkfs.fat and mtools made (`sd/`, from `tools/make_sd_images.sh`): FAT12 on a whole card, FAT16 and FAT32 in a partition, long names and their checksums, case flags, a fragmented file, a directory over many clusters, clusters past 65,535, damaged chains; MOUNT, UNMOUNT and LIST through the card's registers, unit ranges and read-only rules, writes landing in the file's own clusters | none |
| `test_bios_sd` | the BIOS's SD commands on backplane.machine: C809R lists a FAT12 card and a FAT32 one, C80CR boots fat12's BOOT.BIN by DMA and it prints through $C80F and returns; no BOOT.BIN, an empty one, no card in the slot, a card with no FAT, a card still starting up, a machine without the card | the BIOS (`ca65`/`ld65`) |
| `test_oodos_card` | OODOS's card (`sd/oodos.sparse`, from `oodos/tools/make_card.sh`: cpmtools, mkfs.fat, mtools) and cpmtools, both ways: DRIVES.TXT read through the card, then A.IMG mounted by MOUNT as unit 1; the directory label, an SFCB every fourth entry, each file's entries, stamps and records at LBA = track × 16 + sector, by the DPB from `oodos/diskdefs`; a stand-in BDOS writes a file through WRITE, and with `OODOS_CARD_OUT` the card is saved for `oodos/test/interop.sh` | the BIOS (`ca65`/`ld65`) |
| `test_oodos_boot` | OOLDR (`oodos/ldr/`), the card's `/BOOT.BIN`, booted by C80CR: the test OODOS.SYS's segments byte for byte at their physical addresses, the entry's windows, I and D, units 1 and 2 mounted with their flags; then every refusal with its message (DRIVES.TXT's by line, the drives, OODOS.SYS's header, the CP/M reader's damaged directories), on cards built in memory from the committed one's files, and an `oodos-32m` drive | the BIOS (`ca65`/`ld65`) |
| `test_trace_diff` | `tools/trace_diff.py`: relative cycles and gaps, each field, `?` wildcards, `--from`, malformed traces exit 2 | `python3` |

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

**Apple IIe ROMs** are Apple's copyright and are never fetched or vendored.
To run the Apple II mode tests that need them, put your own in
`fixtures/apple2e/` (gitignored with the rest): the 16 KB main ROM
(`$C000-$FFFF`, or the two 8 KB halves) and, optionally, the 4 or 8 KB video
character ROM. Without them those tests skip; everything else uses a
stand-in ROM the tests write themselves.

**`.sid` tunes** (HVSC's and others) are their composers' copyright and are
never fetched or vendored either. Put your own in `fixtures/SIDs/` for
`test_sid_corpus`; without them it skips. `test_sid_file` builds its own
tunes from a few hand-assembled bytes.

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
