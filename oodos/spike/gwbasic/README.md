# GW-BASIC on the 6502: the size spike #

OODOS phase 0 (hw/oodos.md, "GW-BASIC"): before the port starts, translate part of Microsoft's GW-BASIC (1983,
MIT) into 6502, measure it against the 8086 original, and decide whether at least **32 KB stays free for BASIC
programs** with the interpreter split between the TPA (`$0300–$BFFF`) and banked 12 KB pages in W3 reached by
FARCALL.

**Answer: yes, with a thin margin that the port has to make.** The translation is 1.20 times the 8086's size.
Scaled to the whole released source that is 48.9 KB of 6502 code plus 4.3 KB of tables. The code every
statement and operator runs (the statement loop, the formula evaluator, variables, strings, FOR/NEXT, single
precision and integer arithmetic) comes to about 9,990 bytes resident, which leaves **about 34,300 bytes free
(33.5 KB)**. In the pessimistic case (the worst measured ratio for all non-math code, and GW-BASIC's full PC RAM)
it is 32,683, 85 bytes short, which moving GW-BASIC's file and device buffers out of the TPA puts right.
Everything else (output formatting, input, editing, files, graphics, sound, the screen editor, the OO-1's own
drivers) goes in 7 or 8 banked pages. Loops of ordinary statements make no far calls.

KB here is 1,024 bytes; the TPA, $0300–$BFFF, is 48,384 bytes (47.25 KB), and the minimum is 32,768.

The translation is real code: it assembles with ca65/ld65, keeps GW-BASIC's MBF formats, keeps OODOS's rules
for programs, and matches a Python model of GW-BASIC's arithmetic bit for bit on tens of thousands of vectors,
and matches it in BASIC programs run on the emulator.

## What was translated ##

About 2,500 of the 8086's instructions, from five modules (8086 sizes as estimated below):

| Module | Routines | 8086 instructions |
|---|---|---|
| MATH1 | `$FADDS` `$FSUBS` (add, subtract), `SDIV` `$FDIVS` (divide), `$AEXPS`, `$FCOMP` `$COMPM` `ZCMPCK`, the integer package (`IADD` `ISUB` `IMULT` `IDIV` `IMOD` `ICOMP` `INTDIV`), `$SIGNS` `$GETYP` `$ZERO` `$NEG` `$VNEG` `$VABS` `SGN` `FIXER` `VINT`, the moves (`$MOVFR` `$MOVRF` `$VMOVM` `$VMVMF` `$VMVFM` `VMOVE` `VMOVFM` `VMOVAF` `$VPSHF` `$VPOPF` `PUSHF`), `INEG2` `FLOATR` `NEGR` `CONIA` `MAKINT`, `DDIV` (double divide), `MDPTEN` `MDP10` `$MUL10` `$DIV10` `$FADDH`, and output: `$FOTCI` `$FOTZS` `$FOFMT` `LINPRT`; `$OVFLS` `$DIV0S` `DOINF` | 1,152 |
| MATH2 | `$NORMS` `$ROUNS` `$ROUNM` (normalise, round), `$FMULS` (multiply), `$FLT` `$CSI` `$CSD` `$CDS` `$FS` `$FD` `$FI` `$CINC` (conversions, CINT), `$INT` `$QINT`, `$FMULD` `$ROUND` (double multiply), and output: `$FOUT` `$FOTNV` `$FOTCV` `$FOTAN` `$FOTED` `$SIGD` | 585 |
| GWMAIN | `NEWSTT` and `GONE` (the statement fetcher and dispatch), `CHRGTR` with embedded constants, `CONFC1`, `FNDLIN`, `LINGET`, `GOTO` (with `CONCH2`, which turns the line number into a pointer), `DATA` `REM` `ELSES`, `LET`, `IFS` | 358 |
| GWEVAL | `FRMEVL` `LPOPER` `RETAOP` (operator precedence), `APPLOP` (type matching and dispatch), `EVAL`, `ISFUN`, `PARCHK` `DOMIN` `NOTER` `DOCMP` `DANDOR`, `DOCNVF` | 401 |
| BIMISC | `SYNCHR`, `GETSTK`, `ISLET2` | 36 |

MATH1 and MATH2 are one math package between them: `$FMULS` and `$NORMS` are in MATH2, and single
precision output needs double multiply and divide (it brackets the number with powers of ten in double
precision), so those came too.

**Left out** and stubbed (stubs.s): the double add (`$FADDD`), double compares, double output, `$FIN` (number
input; program text carries numbers as embedded constants, so runs don't need it), transcendental functions,
`^`, RND, PRINT USING, strings, real `PTRGET` (the stub has single-letter variables), PRINT (the stub prints
numbers), FOR/NEXT, GOSUB, errors (the stub prints `?ERR n IN line`), and the OEM layer.
`sizes.py` lists exactly which 8086 lines are counted (`MEASURED`), so both sides cover the same code.

## Sizes ##

```
module    insns   8086 B   6502 B  ratio
MATH1      1152     2593     3077   1.19
MATH2       585     1298     1453   1.12
GWMAIN      358      729      995   1.36
GWEVAL      401      867     1037   1.20
BIMISC       36       64       75   1.17
all        2532     5551     6637   1.20   (1.21 with the BASIC stack's 76 B runtime)
```

The math is 1.16, the interpreter 1.27 (GWMAIN's text scanning, with its 16-bit pointer arithmetic, is the
costliest at 1.36). `$SETDB` is in MATH1 in the 8086 and in math2.s here, which skews those two rows a little
but not the total. The tables `$FOUT` needs (powers of ten, the bracket table, 667 bytes with the messages)
are the same size on both.

**How the 8086 side is measured.** No 8086 assembler is available here (the host is aarch64, and nothing could
be downloaded), so `sizes.py` encodes each instruction's length from the 8086's rules: modrm and displacement
bytes (none for `[BX]`, one for `[BP]` or a small offset, two for a direct address), the accumulator's short
forms (`MOV AX,mem` is 3 bytes), sign-extended 8-bit immediates, segment override prefixes, short jumps where
MASM would make them (conditional jumps always; `JMP` backwards within range), `INS86`'s raw bytes, the code
macros expanded, and conditional assembly evaluated. Checked by hand on stretches of `$FADDS` and `$FCOMP`.
Its total for the released source is 40,035 bytes of code in 18,662 instructions (2.15 bytes each: this is
translated 8080 code, full of one-byte `PUSH`, `POP`, `INC` and `XCHG`), plus 4,412 bytes of tables in the code
segment. An error common to all its estimates cancels in the extrapolation, which multiplies by its own total.

## Extrapolation ##

Each module's 8086 code at its kind's measured ratio: MATH1 and MATH2 at 1.16, the rest at 1.27 (and at 1.36
for a high bound):

| | 8086 | 6502 | high |
|---|---|---|---|
| Code, the released source | 40,035 B (39.1 KB) | 50,039 B (48.9 KB) | 53,161 B (51.9 KB) |
| Tables in the code segment | 4,412 B | 4,412 B | 4,412 B |
| The OO-1's OEM layer (screen, keyboard, sound, ports; not in the release, written new) | — | 12–20 KB | 20 KB |
| **Total** | | **≈ 65–73 KB** | 76 KB |

hw/oodos.md's "about 60 KB of 8086 code" fits: 43 KB released, the rest the OEM layer Microsoft didn't
release.

## Layout ##

**Resident in the TPA**, the code every statement and operator runs:

| | 8086 | 6502 |
|---|---|---|
| Statement loop, CHRGET, LET IF GOTO GOSUB RETURN DATA REM, FOR/NEXT, FNDLIN, FNDFOR, GETSTK, SYNCHR (GWMAIN, NEXT86, BIMISC parts) | 1,634 | 2,075 |
| Formula evaluator, EVAL, operators, function dispatch (GWEVAL's formula section) | 1,165 | 1,480 |
| Variables and arrays, PTRGET (BIPTRG) | 884 | 1,123 |
| String space, temporaries, garbage collection, concatenation (BISTRS) | 737 | 936 |
| Single precision and integer arithmetic, compares, conversions, INT/FIX/ABS/SGN | 2,295 | 2,662 |
| LEN ASC CHR$ LEFT$ RIGHT$ MID$ (a far call would add an estimated 15–25% to them in a loop) | 486 | 617 |
| Statement, function and operator tables, with a bank byte per entry (estimate) | | 400 |
| Console output and Ctrl-C polling (OEM, new; estimate) | | 500 |
| The BASIC stack runtime and the far-call dispatch | | 200 |
| **Resident code** | | **9,993** (high 10,465) |

The first five rows are `sizes.py`'s "resident candidates" (8,276 bytes, high 8,704), whole sections of the
source, so they include some cold code (RUN, for one) that shares them.

**Fixed RAM in the TPA.** GW-BASIC declares 2.0 KB of variables and buffers (GWRAM and the data segments, as
`sizes.py` sums them); with the BASIC stack (0.5 KB), three file blocks (0.6 KB), the OEM layer's RAM and
`$FOUT`'s buffer, plan on **4 KB**. On a PC the whole of it is 5.2 KB (GW-BASIC 3.23's banner says 60300 Bytes
free of its 64 KB data segment), which is the bad case. About 140 bytes of GW-BASIC's variables live in zero
page.

**Free for BASIC:** 48,384 bytes, less 9,993 of code and 4,096 of RAM, leaves **34,295 bytes**, 1,527 over
the minimum. At the high ratio and the PC's 5,236 bytes of RAM it is 32,683, 85 short. To make the margin, in
order of value:

1. **Move the file blocks, device queues and the screen editor's line table out of the TPA** into an allocated
   block, reached with FARMOVE (they are touched once per record or line, not per statement): +1–2 KB.
2. **Let GW-BASIC keep a "home" block in W3** between far calls instead of OODOS's resident code: a second
   12 KB of near-resident code, so half of the resident set could move out of the TPA (+4–6 KB free) and PRINT
   and INPUT would make no far calls. This changes hw/oodos.md's FARCALL (the BDOS and interrupt stubs in
   common memory would map OODOS's block in and out), so it is a decision for the design, not assumed here.
3. Run GW-BASIC without RSXs attached: each one comes off the top of the TPA.

**Banked**, 7 or 8 pages of 12 KB in blocks from banks 1–5 (8 blocks is 128 KB of the 320 KB there), grouped
so one statement stays in one page (6502 estimates from each module's 8086 size):

| Page | What | 6502 |
|---|---|---|
| 1 | The rest of the math package: PRINT USING's formatting, `$FOUT`, `$FIN` (INPUT, READ, VAL), double precision, SIN COS TAN ATN LOG EXP SQR `^` RND, the powers of ten; and the PRINT, LPRINT and INPUT statements | 8.5 KB |
| 2 | Program entry and listing: CRUNCH, LIST, DELETE, the keyword tables, start-up, errors and their messages | 5.8 KB |
| 3 | The other statements and functions: RENUM, AUTO, EDIT, ON, DEF FN, USR, SWAP, ERASE, CLEAR, INSTR, STRING$, events, PLAY/SOUND parsing (GWSTS), CALL | 9.5 KB |
| 4 | Files: OPEN, CLOSE, GET, PUT, the device dispatch (GIO86, GIODSK, DSKCOM, FIVEO) | 8.5 KB |
| 5 | Graphics (GENGRP, ADVGRP, DRAW) and the COM, LPT, keyboard and screen device code | 6.6 KB |
| 6 | The screen editor (SCNDRV, SCNEDT) | 5.0 KB |
| 7, 8 | The OO-1's drivers: video modes, SIDs, game port, ports (with page 6's room) | 12–20 KB |

**Far calls on the hot paths: none.** A loop of LET, IF, GOTO, GOSUB, FOR/NEXT, arithmetic, comparisons,
variables and string assignment stays resident. A far call is made once per PRINT or INPUT statement, per call
of a banked function (SIN, LOG, `^`, RND, VAL, STR$, INSTR...), per file, device, graphics or sound statement,
per line typed (CRUNCH) and per LIST. The dispatch tables carry the page, so the statement and function
dispatchers make the far call themselves; there are no per-routine stubs in the TPA.

What that costs, measured on the emulator (cycles, at 7.16 MHz 1,000 cycles is 140 µs):

| | cycles |
|---|---|
| FARCALL over a JSR/RTS (farcall.s: preserves A, X, Y and the flags both ways, nests) | 263 |
| `$FADDS` / `$FMULS` / `$FDIVS` / `$FCOMP` (single) | 470 / 1,840 / 3,420 / 300 |
| `IADD` / `IMULT` / `IDIV` | 110 / 1,010 / 990 |
| `$FOUT` of a single / of an integer | 18,200 / 2,000 |
| `$FMULD` / `DDIV` (double) | 14,500 / 20,200 |
| `10 I%=I%+1 : IF I%<1000 THEN 10`, per pass | 4,680 |
| `A=A+I%*1.5 : I%=I%+1 : IF I%<1000 THEN ...`, per pass | 10,400 |

So a far call adds 1.4% to printing a number (and, estimated, about as much to SIN, which is a polynomial of
several multiplies), but 8–11% per statement if anything on the statement path were banked: hence the
resident list. A leaner FARCALL (keeping only A and the flags) would roughly halve the 263.

## Correctness ##

`make check` (and `check.py --seed N --count N` for more):

- **Math vectors, bit for bit against mbf.py:** 12,763 by default, 40,263 each for seeds 7 and 42, all equal:
  the FAC's bytes, VALTYP, compare results, errors (CINT's Overflow), infinities after Overflow and Division by
  zero (including GW-BASIC's sign rules: an add or multiply that overflows gives +∞), and `$FOUT`'s text, for
  random operands, operands with close exponents, small decimal numbers and edge cases (zero, ±1, the largest
  and smallest, -32768, 32767).
- **The tables:** math1.s's `$FOTB` and powers of ten must equal mbf.py's, which works out the powers of ten
  as the nearest doubles and copies `$FOTB` from MATH1.ASM. The vectors alone pass with a wrong low byte there.
- **mbf.py itself against exact arithmetic** (Python's fractions): single and double divide and double
  multiply are correctly rounded in every case; add and subtract in 97% (worst 0.75 ulp) and multiply in 94%
  (worst 0.63 ulp). Those are GW-BASIC's own: the add loses its sticky bit when the sum carries, and the
  multiply truncates its partial products below the rounding bits. The port must keep them. check.py fails
  if a divide or the double multiply is ever not correctly rounded, or add, subtract or multiply is out by an
  ulp.
- **Programs**, tokenised by basic.py and run through NEWSTT, FRMEVL and the rest on the emulator, against
  basic.py's reading of the same program: a counted loop, a precedence and type test (integer overflow into
  single, `\`, MOD, AND/OR/XOR/EQV/IMP, NOT, relations, nested IF/ELSE, CINT/FIX/INT, Overflow and Division by
  zero messages mid-line), four programs of 60 lines of random expressions, and outputs GW-BASIC is known to
  print (`2/3` is .6666667, `12345678` is 1.234568E+07, `1E+38*10` prints Overflow then 1.701412E+38).
- **OODOS's rules** (rules.py, over the image): no RMB, SMB, BBR or BBS; zero page only through zero page
  operands; no absolute stack page access; no JSR out of the program but the BDOS's. 3,432 instructions, no
  problems. It found one on the way: an indirect `JMP (PTR2)`, now a pushed return address.

**One finding for phase 7:** by the 1983 source, `PRINT 1/3` prints .3333334, not .3333333: `$FOTNV` brings
1/3 to 3333333.43 in double precision, `$CSD` rounds that to 3333333.5 in single, and `$FOTCV` adds .5. The 6502
and the model agree on it. If shipped GW-BASIC prints .3333333, its `$FOUT` differs from this source; PC-BASIC,
the project's oracle, settles it.

## Risks ##

- **The margin.** 34,295 bytes free is 1.5 KB over the minimum, and the bad case is 85 bytes under it. The 8086
  sizes are estimates (a few percent either way, mostly cancelling), the OEM layer is unknown, and GW-BASIC's RAM
  may be more than counted. Moving buffers out (layout, 1) is cheap insurance; the W3 home block (2) is the big
  lever.
- **The sample is math- and evaluator-heavy.** Text scanning (GWMAIN) came out at 1.36, the worst; the editor,
  CRUNCH and LIST are more of that. The high bound uses 1.36 for all non-math code.
- **A mechanical translation would be bigger.** These routines were translated by hand, idiom by idiom
  (multiplies and divides as bit loops, registers as zero page). Phase 7 should do the same.
- **Speed.** Adds and statements cost hundreds to a few thousand cycles (above), but with no MUL or DIV the
  multiply and divide are bit loops (1,840 and 3,420 cycles single, 14,500 and 20,200 double). How that
  compares with an 8088 isn't measured here. Faster multiplies need tables, which cost TPA.
- **The real PTRGET** wasn't translated: its symbol table search is on every variable reference.
- **Kludges that must survive:** the byte before NUMCON must read as a harmless token (the 8086 has a RET
  there, $C3), because DATA and REM step back through the fake text; GOTO rewrites line numbers in the
  program text into pointers.

## What phase 7 should do differently ##

- **Keep the BASIC stack.** The 8086 keeps the formula evaluator's operands, FOR and GOSUB entries on the CPU
  stack beside return addresses; the 6502's page 1 can't hold that, and OODOS's rule 3 forbids FNDFOR's walk
  through page 1 anyway. Here return addresses stay on the 6502's stack and data go on a stack in the TPA
  (`SPUSH`/`SPOP`), in the same order; do the same for FOR and GOSUB.
- **Registers in zero page, the text pointer apart.** TXTPTR is not BX, so the 8086's `PUSH BX`/`POP BX`
  around math calls, and its `XCHG BX,CX` glue (FADD, FSUB, FMULT, FDIV are aliases here), disappear.
- **Two kinds of pointer.** The 8086 points BX at the FAC or at a variable alike; rule 2 forbids pointers into
  zero page, so numbers in zero page are named by X and numbers in memory by PTR. Every generic move and
  compare becomes two. Budget for it.
- **Keep the algorithms exactly**, including what they drop (the truncated partial product, the add's
  sticky bit, `$CSD`'s half-up rounding, `$FOUT`'s bracketing, which overflows are signed), and test each
  module against mbf.py as it is translated, then against PC-BASIC.
- **Dispatch tables carry the page**, and the dispatchers make the far call: no stubs in the TPA.
- **Keep `sizes.py` in the build**: it gives each segment's size from the map; fail the build when the
  resident set passes its budget, so the 32 KB is checked continuously, not at the end.
- **Decide the W3 home block** (layout, 2) before the split is made, and choose a FARCALL variant: this one
  preserves every register; one preserving A and the flags is enough for GW-BASIC's statements and functions.

## Files ##

| File | What |
|---|---|
| `gwbasic.inc` | zero page, pseudo-registers, tokens, conventions |
| `math1.s` `math2.s` `gwmain.s` `gweval.s` `bimisc.s` `tables.s` | the translation (one segment each; tables.s holds GWDATA's and IBMRES's tables) |
| `stubs.s` | what the translation calls but the spike doesn't translate |
| `harness.s` | runs math vectors or a tokenised program |
| `farcall.s` | a FARCALL, to time it |
| `spike.cfg`, `Makefile` | the build: a `.COM` linked at $0300 |
| `run6502.cpp` | runs the image on the emulator's W65C02S core, answering BDOS calls at $0203 |
| `mbf.py` | the reference model of GW-BASIC's arithmetic |
| `basic.py` | a tokeniser and reference reader for test programs |
| `check.py` | runs everything and compares |
| `rules.py` | checks OODOS's rules on the image |
| `sizes.py` | the 8086 estimates, the comparison and the extrapolation (needs a GW-BASIC clone) |

```
make            # spike.bin and spike.map
make check      # rules.py and check.py
make sizes GWSRC=/path/to/GW-BASIC
```

Derived from Microsoft GW-BASIC (1983), MIT licence: see LICENSE.
