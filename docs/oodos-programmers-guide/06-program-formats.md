# 6 Program formats

The files OODOS runs: the .COM, linked at `$0300`, and the relocatable .PRL; the rules that keep a program fit for a
65C816; and, from phase 5, RSXs and chaining.

The design record, for the reasons: [hw/oodos.md, "RSXs and programs"](../../hw/oodos.md#rsxs-and-programs), ["The
.PRL file"](../../hw/oodos.md#the-prl-file).

## .COM files

A `.COM` is a 16-byte header, then the program's image, linked to run at `$0300`. The header is in the file only;
the image after it is what goes into memory. `OODOS_HEADER` in [`oodos.inc`](../../oodos/include/oodos.inc) writes
the header from the values the link gives it, and [`cfg/com.cfg`](../../oodos/cfg/com.cfg) lays the file out, so a
program says `OODOS_HEADER` once, in one module, and never fills in the header by hand.

| Offset | Size | What |
|---|---|---|
| 0 | 4 | the magic number, `$4F $4F $44 $1A`: "OOD" and a ^Z, so TYPE shows `OOD` and stops |
| 4 | 1 | flags: bit 0 a `.PRL`, bit 1 an RSX, bit 7 breaks the 65C816 rules; 0 for a `.COM` |
| 5 | 1 | the link page: `$03` for a `.COM` |
| 6 | 2 | the image's size, in bytes |
| 8 | 2 | BSS: the bytes the program needs above its image, not in the file |
| 10 | 1 | the zero page base: where the program's `ZEROPAGE` segment is linked |
| 11 | 1 | the zero page size: that segment's bytes |
| 12 | 1 | the number of attached RSXs: 0 (GENCOM, phase 5) |
| 13 | 1 | reserved, 0 |
| 14 | 2 | the record of the attached RSX table: 0 |

**Loading.** The CCP finds `NAME.COM` (the User's Guide,
[chapter 2](../oodos-users-guide/02-command-line.md#where-programs-are-found)), reads its first record and checks the
header before it writes anything to the TPA:

- bytes 0-3 must be the magic number, or the CCP says `NO PROGRAM`; an empty file says the same;
- the flags must be 0 and the link page `$03`, and `$0300` + the size + BSS must end at or below `$BFFD`, the
  BDOS's entry; otherwise `BAD PROGRAM HEADER`.

Then it reads exactly the image's size: the rest of the first record after the header, whole records straight to
their place, and a last part-record through a buffer, so nothing above the image changes. BSS is not cleared, and
the padding at the end of the file's last record never reaches memory. A file that ends inside its image, or a read
that fails, says `READ ERROR`. Each of the three ends the command with the return code `$FF00`.

**What a program starts with.** At `$0300`:

- A, X and Y are 0, the decimal flag clear and interrupts off. S is `$FD`: the stack holds the warm boot's address less
  one, so an RTS from the program's first level ends it as `JMP WBOOT` does.
- The base page is filled in ([chapter 2](02-system-interface.md#the-base-page)): the command tail at `$0280`, the
  two FCBs at `$025C` and `$026C`, the drive the program came from at `$0250`, the tail's passwords at
  `$0251-$0256`.
- The DMA address is `$0280`, the error mode 0 (function 45), the return code 0 (function 108, unless the program
  was chained to), the console mode 0 and the delimiter `$` (functions 109 and 110), the multi-sector count 1
  (function 44), and nothing allocated (functions 53-58).
- The zero page `$00-$EF` is the program's, and not cleared. `$F0-$FF` is the RSXs'.
- The TPA runs from `$0300` to `$BFFC`. The operand of the `JMP` at `$0203`, `$0204-$0205`, is its top + 1:
  `$BFFD`, until RSXs come with phase 5.

A program ends with `JMP WBOOT` (`$0200`), function 0, or an RTS from its first level. The warm boot frees what the
program allocated, writes back what the BDOS still holds, and goes back to the CCP.

**The CCP runs `.COM`s only.** It looks for no other type, and a `.PRL` renamed to `.COM` is refused as
`BAD PROGRAM HEADER`: its flags aren't 0. `.PRL`s are for RSXs and overlays, which phase 5's loader brings.

## .PRL files

A `.PRL` is the same header with flag bit 0 set, the image linked at the header's link page and zero page base,
then two bitmaps that say which bytes change when it runs elsewhere: the page bitmap, for the high byte of each
address in the image, and the zero page bitmap, for each address in its zero page. A loader that puts the image at
page P with its zero page at Z adds P minus the link page to each byte the first marks, and Z minus the zero page
base to each byte the second marks. The file byte by byte is [appendix B](appendix-b-prl-format.md).

Nothing in OODOS 0.2 loads a `.PRL` yet: function 59 and the RSX loader come with phase 5
([hw/oodos.md, "Phases"](../../hw/oodos.md#phases)). The build makes them now ([appendix F](appendix-f-building.md)),
and [`oodos/tools/mkprl.py`](../../oodos/tools/mkprl.py) `--load` loads one as the loader will, for testing.

A `.PRL` is made from three links of the same objects with [`cfg/prl.cfg`](../../oodos/cfg/prl.cfg): at page 3 with
the zero page at `$00`, page 4 with it at `$02`, and page 5 with it at `$05`. mkprl.py compares them. Every byte
must stay the same in all three, move as a page does (1, then 2), or move as the zero page does (2, then 5);
anything else is refused, with its offset. So a byte that is some other function of an address, such as
`>(label*2)`, fails the build instead of relocating wrongly.

## Overlays: function 59

Function 59, which loads an overlay, comes with phase 5's loader, and its entry is written then. As designed
([hw/oodos.md, "CP/M 3's functions"](../../hw/oodos.md#cpm-3s-functions)), it loads a `.PRL` at the page the
FCB's random record names, or a `.COM` at `$0300` only. An overlay has no zero page of its own (its header's zero
page size must be 0): it uses its caller's, by importing the caller's zero page symbols when it is linked.

## Rules for programs: the 65C816

A later machine may have a 65C816, whose zero page (its direct page) and stack can be anywhere. A program that
keeps four rules would run on one unchanged, in emulation mode, with a direct page of its own:

1. **No `RMB`, `SMB`, `BBR` or `BBS`.** They are the 65C02's only; the 65C816 has other instructions at those
   opcodes.
2. **Zero page only through zero page operands.** No absolute-mode access to a zero page byte (`LDA $0012`), and
   no 16-bit pointer to one (`.word ptr`). Both break when the direct page moves.
3. **The stack only through the stack instructions.** No reading page 1 by address (`TSX`, then `LDA $0103,X`).
4. **The BDOS only through `$0203`,** with the registers of [chapter 2](02-system-interface.md#calling-the-bdos).

The build enforces them. `oodos/tools/rules.py` disassembles the `CODE` segment of each `.COM` and of each `.PRL`'s
first link, and fails on rules 1, 3 and 4 and on rule 2's absolute operands. mkprl.py finds rule 2's 16-bit
pointers, in any segment, but only those to the program's own `ZEROPAGE` segment, the addresses that move between
its links. What the tools can't see is the author's to keep: code placed outside `CODE`, and pointers to fixed zero
page addresses.

Header flag bit 7 marks a program that breaks the rules. OODOS's tools never set it, and the CCP refuses a program
with it set, as it refuses any flag.

Keeping the rules isn't all: a program also leaves alone what OODOS owns, the interrupts and vectors, the DMA
channels, the north bridge's windows, the sound card's 6510, and the VIA's ACR, IER and IFR
([AGENTS.md](../../AGENTS.md#the-rules-that-matter-most)).

## RSXs

Written with OODOS phase 5, the RSX loader.

## Chaining

A program can end by naming the command to run next: [function 47](07-function-reference.md#bdos-47) puts it at
`$0280`, ended by a 0, and the CCP runs it after the warm boot, as if typed, with the program's drive and user if
asked. The return code ([function 108](07-function-reference.md#bdos-108)) is kept for the program the command
runs, so the first can pass the second a word. What RSXs add to chaining is written with phase 5, the RSX loader.
