# Writing code for the OO-1

Read this before writing a program, a driver or a test for the OO-1. It is short on purpose: the books in
[docs/](docs/README.md) are the reference, and [hw/](hw/) is the design record behind them.

## Which book

- **A program under OODOS** (a `.COM` or `.PRL` on the card): the
  [OODOS Programmer's Guide](docs/oodos-programmers-guide/README.md). Each BDOS function is an entry,
  `07-function-reference.md#bdos-N`; [docs/index/functions.json](docs/index/functions.json) lists them all with
  their anchors.
- **The BIOS, a driver, OOLIB, or code on the bare machine or in ROM:** the
  [OO-1 Technical Reference](docs/oo1-technical-reference/README.md); every register is in
  [docs/index/registers.json](docs/index/registers.json).
- **What a user types at `A>`:** the [OODOS User's Guide](docs/oodos-users-guide/README.md).
- **Why something is the way it is:** [hw/oodos.md](hw/oodos.md), [hw/design.md](hw/design.md),
  [hw/oolib.md](hw/oolib.md).

The books are being written (phases G1-G3, [docs/README.md](docs/README.md#phases)). Where an entry isn't written
yet, [docs/tools/unwritten.txt](docs/tools/unwritten.txt) says so; go to hw/ and the code, and trust the code.

## The rules that matter most

- **Call the BDOS as [`oodos.inc`](oodos/include/oodos.inc) says:** `JSR BDOS` (`$0203`), the function in Y, a byte
  in A, a word in A (low) and X (high); the result in A, or A and X. Y and the flags aren't kept. Use oodos.inc's
  names (`C_WRITESTR`), not bare numbers.
- **A program is a `.COM` (or `.PRL`) file on the card, not ROM.** It starts with `OODOS_HEADER`, runs at `$0300`,
  and ends with `JMP WBOOT`. Its zero page is `$00-$EF`; `$F0-$FF` is the RSXs'.
- **Don't take over what OODOS owns:** interrupts and the vectors, the DMA channels, the north bridge's windows,
  the sound card's 6510, and the VIA's ACR, IER and IFR. Once OOLIB exists ([hw/oolib.md](hw/oolib.md), phase G3),
  go through it for the screen, sound, timing and input; until then, ask before a program touches that hardware.
- **Keep to the 65C816 rules:** the build checks them (`oodos/tools/rules.py`) and fails a program that breaks them
  ([hw/oodos.md](hw/oodos.md#rules-for-programs-the-65c816)).
- **No Digital Research text or code,** in programs, tests or books. Write from the meaning, in our own words.

## Building and testing a program

- **Includes:** [`oodos/include/oodos.inc`](oodos/include/oodos.inc) (the header, the base page, the function
  numbers), [`oodos/include/cmd.inc`](oodos/include/cmd.inc) (console helpers for commands). Hardware:
  [`backplane/bios/backplane.inc`](backplane/bios/backplane.inc).
- **Link configs:** [`oodos/cfg/com.cfg`](oodos/cfg/com.cfg) for a `.COM`, [`oodos/cfg/prl.cfg`](oodos/cfg/prl.cfg)
  for a `.PRL`.
- **Build** with oodos's Makefile, which runs the rules gate and fails on any ld65 warning:
  `make -C oodos SRC=$PWD/myprog PROGRAMS=name BUILD=$PWD/build`. [docs/examples/](docs/examples/) does the same for
  the books' examples ([its Makefile](docs/examples/Makefile)).
- **Test** on the emulator with the rig in
  [`emulator/test/host/oodos_rig.h`](emulator/test/host/oodos_rig.h): `CcpRig` boots OODOS.SYS, `put_m()` puts a
  `.COM` on `M:`, and `cmd("m:name")` types it at the prompt and returns what it printed.
  [`test_docs_examples.cpp`](emulator/test/host/test_docs_examples.cpp) is the smallest suite that does this.
- **A program in a book** goes in [docs/examples/](docs/examples/) with its expected output, and the book shows it
  with a listing marker; `make -C docs check` holds the two together ([docs/README.md](docs/README.md#examples)).
