# Writing code for the OO-1

Read this before writing a program, a driver or a test for the OO-1. It is short on purpose: the books in
[docs/](docs/README.md) are the reference, and [hw/](hw/) is the design record behind them.

## Which book

- **A program under OODOS** (a `.COM` or `.PRL` on the card): the
  [OODOS Programmer's Guide](docs/oodos-programmers-guide/README.md). Each BDOS function is an entry,
  `07-function-reference.md#bdos-N`; [docs/index/functions.json](docs/index/functions.json) lists them all with
  their anchors.
- **The BIOS, a driver, OOLIB, or code on the bare machine or in ROM:** the
  [OO-1 Technical Reference](docs/oo1-technical-reference/README.md); every register that an equate in
  [`backplane.inc`](backplane/bios/backplane.inc) names is in [docs/index/registers.json](docs/index/registers.json).
- **What a user types at `A>`:** the [OODOS User's Guide](docs/oodos-users-guide/README.md).
- **Why something is the way it is:** [hw/oodos.md](hw/oodos.md), [hw/design.md](hw/design.md),
  [hw/oolib.md](hw/oolib.md).

The books are being written (phases G1-G3, [docs/README.md](docs/README.md#phases)). Where an entry isn't written
yet, [docs/tools/unwritten.txt](docs/tools/unwritten.txt) says so; go to hw/ and the code, and trust the code.

## The rules that matter most

- **Call the BDOS as [`oodos.inc`](oodos/include/oodos.inc) says:** `JSR BDOS` (`$0203`), the function in Y, a byte
  in A, a word in A (low) and X (high); the result in A, or A and X. Y isn't kept; the flags come back as they were
  before the call, with D clear, so test A, not the flags. Use oodos.inc's names (`C_WRITESTR`), not bare numbers.
- **A program is a `.COM` (or `.PRL`) file on the card, not ROM.** It starts with `OODOS_HEADER`, runs at `$0300`,
  and ends with `JMP WBOOT`. Its zero page is `$00-$EF`; `$F0-$FF` is the RSXs'.
- **Don't take over what OODOS owns:** interrupts and the vectors, the DMA channels, the north bridge's windows,
  its common hole (HOLE), its timebase (TBCTL, MS0-MS3, TBSUB, MSS) and its random number registers (RNGST, RNGCNT,
  RNGDATA, `$19-$1B`: a program asks function 78), the sound card (revision 3, the only one OODOS drives: drive it
  through function 82 only), the VIA's IER and IFR, and its ACR but for
  timer 1's mode bits (7-6), and the serial line's receive (the 6551's CMD and its interrupt, its STATUS and DATA;
  later the TL16C552A's IER, MCR and FIFO control): a program reads AUX: by functions 3 and 7, and asks function 85
  for the rest (the owner, 2026-11-21). **Timer 1 is the program's, polled** (the owner, 2026-11-21; OODOS 0.16,
  which needs north bridge revision 3): its counter, its latches, ACR's bits 7-6 (the rest of ACR kept as read) and
  its IFR flag, never its IER bit; a program that may run on an older OODOS checks function 80's `FT_INFO` for
  `FC_T1` first. **`$F800-$FFFF` is common memory whatever W3 holds** (OODOS 0.16's common hole, the owner,
  2026-11-22): a program never writes there, and a far-called block needs no copy of it. The one exception to the windows is OOLIB's: its linked library maps W1 itself (a video block
  or one the program allocated), under the W1 contract (OOLIB interface 4,
  [the Programmer's Guide, chapter 5](docs/oodos-programmers-guide/05-memory-and-banking.md#the-w1-contract)); a
  program draws through OOLIB, not by writing `NB_WIN1` or `NB_WIN1H` itself, and keeps its code and data out of
  `$4000-$7FFF` meanwhile. The console's video memory, units 0-12, is OODOS's. Go through OOLIB
  ([hw/oolib.md](hw/oolib.md), phase G3) for the screen, sound, timing and input; for what it doesn't cover yet,
  ask before a program touches that hardware.
- **The system tool's exception** (the owner's, 2026-11-13: [hw/diag.md, answer 1](hw/diag.md#the-owners-answers);
  narrowed to SYSINFO by the owner, 2026-11-14: [plan 2, P2-1](hw/diag.md#plan2-questions), since the diagnostics
  are all in the ROM and `DIAG` at `A>` is OODOS's own, function 75). One program, and no other, may touch the
  hardware below directly under OODOS: `SYSINFO.COM`, built from `oodos/utils/sysinfo.s` with `backplane/oohw/`. No
  other program, RSX or library may claim it, by name or by copying its code; anything else still asks first, as
  above. The list, cut to what SYSINFO reads and writes:
  - **Reads:** each card's ID byte (twice, CC5); `NB_STATUS`; `NB_RTCCTL`; `NB_MAP` (its AP8, read only after
    `NB_ID` reads `$D2` or more, to say what bank 8 is); `VIDEO_MEMSIZE`; `USB_DEVICES`; the SD card's ID and `SD_STATUS`;
    the console card's SWR and PANEL; `NB_SEC` polled for the clock (how that meets the bridge's own I²C reads on
    real hardware is unknown, as `hwclock.s` says).
  - **The VIA's timer 2** (EE5): `T2CL` and `T2CH` written and read, for `HW_CLOCK`'s count. That changes only
    IFR's timer 2 flag (a read of `T2CL` clears it), which OODOS doesn't use; never ACR, IER or timer 1.
  - **Writes:** RTCREG, to read the DS3231's temperature through RTCDATA (an I²C read; BUSY waited for); and the SD
    card's `IDENTIFY` (UNIT 0, CTRL cleared for it and put back, CMD, then 512 bytes of DATA, its ERROR read on a
    failure).
  - **Why IDENTIFY can't collide with OODOS's driver:** OODOS drives the SD card only inside BDOS and BIOS calls,
    each command waited for until BUSY clears (`oodos/sys/bios.s`), and never from an interrupt: its SD interrupt
    stays off (`SD_IEN` 0, `irq.s`'s DEAFEN) and the dispatcher doesn't serve the card. A program runs only between
    those calls, so when SYSINFO writes CMD no command of OODOS's is under way. IDENTIFY changes nothing on the card
    (LBA and COUNT stay), SYSINFO reads DATA by the CPU with CTRL's DMA bit clear and puts CTRL back as it found it,
    and OODOS's next command writes UNIT, LBA, COUNT and CTRL afresh.
- **Keep to the 65C816 rules:** the build checks them (`oodos/tools/rules.py`) and fails a program that breaks them
  ([hw/oodos.md](hw/oodos.md#rules-for-programs-the-65c816)).
- **Interfaces programs use: free before 1.0, then they only grow** (the owner's rule, 2026-11-13; reworded
  2026-11-24). The BDOS functions, OOLIB's interface (function 80's `FT_INFO`), FARCALL and common memory's table,
  the BIOS's jump table and OOUI's interface (`UI_IFACE`). Before OODOS 1.0 (after GW-BASIC, phase 7) they change
  freely: there is no older OODOS, only pre-1.0, and programs and OODOS ship together, so a change needs no
  version gate and no caller refuses an earlier build. Each change is named in its commit (what changed, and its
  interface version raised), and the books follow it. Version numbers stay reported (`FT_INFO`, SYSINFO), never
  checked. From 1.0 they only grow: a new function, operation, flag or field is added with its interface version
  raised, every existing call keeps its arguments, results, record layouts and meaning, and a change that would
  break an existing caller, or remove anything, goes to the owner first, named, with why.
  So, before 1.0 (the owner, 2026-11-23), function 82's operations were laid out afresh as one table grouped by
  job, for the sound card as a device (OODOS 0.17, OOLIB interface 18)
  ([the build plan's decisions](hw/future/sound-device-build.md#decisions)).
- **The sound card is revision 3 (`$A3`) only** (the owner, 2026-11-24: until 1.0, no support or tests for a
  revision that has been replaced). The ROM, OODOS, OOLIB and the emulator drive no other; a program reaches the card
  through function 82 alone, and writes none of its registers.
- **No Digital Research text or code,** in programs, tests or books. Write from the meaning, in our own words.
  The CP/M 3 Programmer's Guide and User's Guide are on the owner's machine, outside the repo and read-only, in
  `~/workspace/references/cpm3/` (PDF and text): read them for what CP/M 3 does, and never copy or closely
  paraphrase their text into the repo or the site.

## Building and testing a program

- **Includes:** [`oodos/include/oodos.inc`](oodos/include/oodos.inc) (the header, the base page, the function
  numbers), [`oodos/include/cmd.inc`](oodos/include/cmd.inc) (console helpers for commands). Hardware:
  [`backplane/bios/backplane.inc`](backplane/bios/backplane.inc).
- **Link configs:** [`oodos/cfg/com.cfg`](oodos/cfg/com.cfg) for a `.COM`, [`oodos/cfg/prl.cfg`](oodos/cfg/prl.cfg)
  for a `.PRL`.
- **Build** with oodos's Makefile, which runs the rules gate and fails on any ld65 warning:
  `make -C oodos SRC=$PWD/myprog PROGRAMS=name BUILD=$PWD/build $PWD/build/name.com`. Name the target: without it,
  make builds `all`, which is every utility and a `.PRL` too. Don't give a program a utility's name (one of
  `oodos/utils/*.s`): the Makefile builds the utility instead. [docs/examples/](docs/examples/) does the same for
  the books' examples ([its Makefile](docs/examples/Makefile)).
- **Test** on the emulator with the rig in
  [`emulator/test/host/oodos_rig.h`](emulator/test/host/oodos_rig.h): `CcpRig` boots OODOS.SYS, `put_m()` puts a
  `.COM` on `M:`, and `cmd("m:name")` types it at the prompt and returns what it printed.
  [`test_docs_examples.cpp`](emulator/test/host/test_docs_examples.cpp) is the smallest suite that does this.
- **A program in a book** goes in [docs/examples/](docs/examples/) with its expected output, and the book shows it
  with a listing marker; `make -C docs check` holds the two together ([docs/README.md](docs/README.md#examples)).
