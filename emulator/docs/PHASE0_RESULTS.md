# Phase 0 results: the core as tagged `beta`

Measured 2026-10-18 on the core at `beta` (9203823), unchanged.
GCC 14.2, Debug, ASan/UBSan on. Commands are in `test/host/README.md`.

## Summary

| Suite | Result |
|---|---|
| `test_cpu_smoke` | **9/9 pass** |
| `test_dormann` (`6502_functional_test`) | **pass**: success trap $3469 after 30,646,176 instructions, 16 s |
| `test_harte` (100 cases/opcode) | 135/151 opcodes clean; 16 fail, 85/15,100 cases |
| `test_harte` (1,000 cases/opcode) | 135/151 opcodes clean; 16 fail, 860/151,000 cases |

No sanitizer reports in any run.

## The one failure class: decimal mode with invalid BCD

All 16 failing opcodes are ADC and SBC in every addressing mode:
`61 65 69 6D 71 75 79 7D` (ADC) and `E1 E5 E9 ED F1 F5 F9 FD` (SBC).

**Every one of the 860 failing cases has D set and at least one operand that
is not valid BCD** (a nibble above 9). A script resolved each failing case's
effective operand in all eight modes to check this. Decimal arithmetic on
valid BCD passes everywhere, and so does all binary-mode ADC/SBC.

The failures show up in A: e.g. `ADC #$4D` with A=$2D, D=1, C=0 gives $80,
where an NMOS 6502 gives $70. Harte compares A before P, so it is not yet
known whether N/V/Z are also wrong in these cases.

**Does it matter?** Not for code that only does BCD on BCD. That includes
Woz Monitor, both BASICs and the Dormann suite. It matters for code that
relies on the undocumented NMOS behaviour, and for the real board, which is
a W65C02S: its invalid-BCD results differ from the NMOS ones, and its N/Z
flags are valid in decimal mode. Fix it as part of the W65C02S work in
Phase 4, against Harte's `wdc65c02` set, not before.

## What this means for Phase 1

The core is in better shape than "works for the most part" suggested: every
documented opcode, in every mode, on 1,000 random states. The emulator's
known problems are most likely **outside `cpu.cpp`**: the main loop and its
I/O, the memory map, and ROM loading. Phase 1 should start with the items in
`EMULATOR_PLAN.md` §1, with the Applesoft ROM failure first.

## Not yet measured

- **Cycle counts.** The core has none. Harte's expected counts are stored in
  the fixtures, ready for Phase 4.
- **P bit 4 (B) in the live status byte.** Masked on purpose: `Reset()` forces
  it on and no program can observe it. Pushed copies (PHP, BRK, IRQ) are
  checked through RAM and pass.
- **Interrupt timing.** That is, when IRQ/NMI are sampled relative to
  instruction boundaries, and the I-flag delay after CLI/SEI/PLP. This needs
  the Phase 3 bus.
- **Undocumented opcodes.** These run as NOP by design.

## Verifying the checks

Each check was made to fail on purpose before its pass was trusted:

- **Mutated core** (INX takes N from bit 6, and `JMP ($xxFF)` without the page
  wrap): `test_harte` flagged both (E8: 484/1000, 6C: 4/1000), and
  `test_cpu_smoke` flagged the JMP. `test_dormann` **still passed**; see the
  blind spot in `test/host/README.md`.
- **The first version of the smoke suite also passed against the mutated
  core.** easylogging++'s `CHECK_EQ` had replaced the harness's and was
  compiled to a no-op. Fixed with `ELPP_NO_CHECK_MACROS` plus an `#error`
  guard; the 9/9 above is from after the fix.
- **Fixtures absent:** `test_dormann` and `test_harte` report Skipped. An
  empty Harte directory fails with "no opcode files".
