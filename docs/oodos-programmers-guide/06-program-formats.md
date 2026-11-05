# 6 Program formats

The files OODOS runs: the .COM, linked at `$0300`, and the relocatable .PRL; the rules that keep a program fit for a
65C816; RSXs and chaining.

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
| 12 | 1 | the number of attached RSX table entries (gencom.py, GENCOM): 0 for none ([RSXs](#rsxs)) |
| 13 | 1 | reserved, 0 |
| 14 | 2 | the record of the attached RSX table: 0 for none |

**Loading.** The CCP finds `NAME.COM` (the User's Guide,
[chapter 2](../oodos-users-guide/02-command-line.md#where-programs-are-found)) and loads it by
[function 59](07-function-reference.md#bdos-59) at `$0300`, as a program loads an overlay. The loader checks the
header before it writes anything to the TPA:

- bytes 0-3 must be the magic number, or the CCP says `NO PROGRAM`; an empty file, or one whose image is empty
  with nothing attached, says the same;
- the flags must be 0 and the link page `$03`, and `$0300` + the size + BSS must end at or below the TPA's top +
  1: the lowest RSX's first byte, or `$BFFD`, the BDOS's entry, with none; otherwise `BAD PROGRAM HEADER`, or for
  no room (too big, or the RSXs' table full) `Insufficient Memory` (ours).

Any RSXs attached to it are loaded first ([RSXs](#rsxs)), so the program must fit below them. Then the loader reads
exactly the image's size: the rest of the first record after the header, whole records straight to their place,
and a last part-record through a buffer, so nothing above the image changes. BSS is not cleared, and the padding at
the end of the file's last record never reaches memory. A file that ends inside what it says it holds, or a read
that fails, says `READ ERROR`. Each of the three ends the command with the return code `$FF00`, and RSXs the load
had linked are taken out again.

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
- The TPA runs from `$0300` to `$BFFC` with no RSXs. The operand of the `JMP` at `$0203`, `$0204-$0205`, is its
  top + 1: `$BFFD`, or with RSXs the lowest one's first byte + 6 ([RSXs](#rsxs)).

A program ends with `JMP WBOOT` (`$0200`), function 0, or an RTS from its first level. The warm boot frees what the
program allocated, writes back what the BDOS still holds, and goes back to the CCP.

**The CCP runs `.COM`s only.** It looks for no other type, and a `.PRL` renamed to `.COM` is refused as
`BAD PROGRAM HEADER`: its flags aren't 0. `.PRL`s are for RSXs and overlays (function 59).

## .PRL files

A `.PRL` is the same header with flag bit 0 set, the image linked at the header's link page and zero page base,
then two bitmaps that say which bytes change when it runs elsewhere: the page bitmap, for the high byte of each
address in the image, and the zero page bitmap, for each address in its zero page. A loader that puts the image at
page P with its zero page at Z adds P minus the link page to each byte the first marks, and Z minus the zero page
base to each byte the second marks. The file byte by byte is [appendix B](appendix-b-prl-format.md).

Function 59 loads a `.PRL` as an overlay, and the loader loads RSXs, which are `.PRL`s too ([RSXs](#rsxs)).
[`oodos/tools/mkprl.py`](../../oodos/tools/mkprl.py) `--load` loads one as the loader does, for testing: the same
bytes, which `test_oodos_rsx` compares.

A `.PRL` is made from three links of the same objects with [`cfg/prl.cfg`](../../oodos/cfg/prl.cfg): at page 3 with
the zero page at `$00`, page 4 with it at `$02`, and page 5 with it at `$05`. mkprl.py compares them. Every byte
must stay the same in all three, move as a page does (1, then 2), or move as the zero page does (2, then 5);
anything else is refused, with its offset. So a byte that is some other function of an address, such as
`>(label*2)`, fails the build instead of relocating wrongly.

## Overlays: function 59

[Function 59](07-function-reference.md#bdos-59) loads a file the program has opened at the address in its FCB's
random record, R0-R1: a `.PRL` at any page from `$0300`, relocated there, or a `.COM` at `$0300` only (OODOS's:
CP/M 3 loads an absolute file anywhere; on OODOS a file that loads elsewhere is a `.PRL`). It doesn't run what it
loads: the program jumps or calls into it. An overlay has no zero page of its own (its header's zero page size must
be 0): it uses its caller's, by importing the caller's zero page symbols when it is linked. Any program may call
59; the loader is always there. A file with RSXs attached is refused: only the CCP's loads attach them.

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

A resident system extension sits between programs and the BDOS: every call through `$0203` reaches it first, and it
handles the functions it is written for and passes the rest on. The design record:
[hw/oodos.md, "The RSXs, phase 5"](../../hw/oodos.md#the-rsxs-phase-5).

**The file.** An RSX is a `.PRL` with header flag bit 1 set too (flags `$03`), file type `.RSX`, linked with
[`cfg/rsx.cfg`](../../oodos/cfg/rsx.cfg): three links, as for a `.PRL`, the `RSXHDR` segment first in the image.
Its zero page, if any, is at most 16 bytes; the loader moves it into what is free of `$F0-$FF`.

**The prefix.** The image starts with 27 bytes that `RSX_PREFIX name, entry, remove{, nonbank}` in
[`oodos.inc`](../../oodos/include/oodos.inc) writes. The offsets are CP/M 3's:

| Offset | Size | What | Who sets it |
|---|---|---|---|
| 0 | 6 | the serial number (function 107's) | the loader |
| 6 | 3 | `JMP` to the RSX's code: where every call arrives | the RSX |
| 9 | 3 | `JMP` to the next RSX, or the BDOS | the loader |
| 12 | 2 | the address of the `JMP` that leads here: `$0203` for the first in the chain, else the offset 9 of the RSX before it | the loader |
| 14 | 1 | the remove flag: `$FF`, take it out at the next warm boot | the RSX |
| 15 | 1 | the non-banked flag: `$FF`, load it only in a non-banked system, so never on OODOS | the RSX |
| 16 | 8 | the name, space padded | the RSX |
| 24 | 1 | the loader flag: the loader writes 0 | the loader |
| 25 | 2 | reserved | |

mkprl.py refuses an RSX without the prefix. **The remove flag is the RSX's own:** one that serves a single program
has `$FF` in its file and goes at the warm boot after that program; one that must stay has 0, and sets `$FF` itself
when it is done.

**The code.** The RSX runs in its caller's context: the caller's stack (a few bytes of it) and windows; zero page
only its slice. It has Y the function and A/X the parameter, as the BDOS would. It passes a call on by `JMP` to its
own offset 9 with them as they came, and makes its own BDOS calls by `JSR` to offset 9, never through `$0203`,
which would reach it again. An RSX that opens a file of its own must put back what its caller had: the DMA address
and segment, the user, the error mode, the multi-sector count.

**Attaching.** GENCOM ([the User's Guide](../oodos-users-guide/05-utilities.md#util-gencom)), and
[`oodos/tools/gencom.py`](../../oodos/tools/gencom.py) for the build, which writes the same bytes, add RSXs to a
`.COM`: after the image's last record comes a table of 16-byte entries, then each RSX file whole, on its records;
header bytes 12 and 14 give the count and the table's record. An entry:

| Offset | Size | What |
|---|---|---|
| 0 | 2 | the record the RSX's file starts at; for an SCB entry, the SCB offset, then the value |
| 2 | 1 | flags: bit 0 load only in a non-banked system (passed over); bit 1 an SCB entry, not an RSX (ours) |
| 3 | 8 | the name, space padded (the RSX file's) |
| 11 | 5 | reserved, 0 |

At most 15 entries, RSXs and SCB entries together. A `.COM` whose image is empty, with RSXs attached, loads them
and runs nothing: they stay for the next program, which can call them, even if their remove flag is `$FF`, and go
at the warm boot after it. An SCB entry sets that byte of the SCB as the program starts, after the CCP's own
settings: a program can be given console mode bits 8 and 9, say.

**In memory.** The loader puts each RSX at the highest page where its image and BSS end at or below the lowest RSX
there, or `$BFFD`, in the table's order, so the last loaded is the lowest and the first a call reaches. An RSX of a
name already loaded is loaded again, below. `$0203` jumps to the lowest RSX's offset 6, so `$0204` is its first byte
+ 6, and a program's TPA ends below its first byte. The loader keeps a table of up to 15 RSXs in its own memory, and
at each warm boot writes `$0203` and every link again from it.

**Removal.** At each warm boot the RSXs whose remove flag is `$FF` are taken out, and a program's
[function 59](07-function-reference.md#bdos-59) does the same first (with A/X = 0, only that), its own attached
RSXs among them: an RSX that must outlive its program's overlay loads starts with its remove flag 0 and sets it
when it is done. Memory comes back from the bottom: a removed RSX above one that stays keeps its pages until that
one goes too. An RSX whose offset 6 or 9 is no longer a `JMP` (a program wrote over it) is dropped at the warm
boot, so it can't be jumped into. A cold boot starts with none.

**The end of a program.** However a program ends, by `JMP WBOOT`, an RTS, function 0, a ^C, an error, or function
47, the RSXs see function 0 once, through the chain, before the CCP's warm boot work (ours). So an RSX can tidy up
after the program it served.

**Calling one.** [Function 60](07-function-reference.md#bdos-60) passes a parameter block down the chain: a function
byte, a count of words, the words. An RSX takes the blocks whose function byte is its own. 0-127 are programs'
RSXs', 128-255 OODOS's: GET's RSX takes `$80` (GET.COM's file) and `$81` (GET CONSOLE), PUT's `$90` (PUT.COM's
file), `$91` (PUT ... TO CONSOLE or PRINTER) and `$92` (is a PUT of the kind writing to a file?);
[`oodos.inc`](../../oodos/include/oodos.inc) names them `RXF_*`.

**What the CCP is doing.** The SCB's `$19` (ours, [appendix C](appendix-c-scb.md)): bit 7 set while the CCP runs,
clear while a program does; bit 6 set while a command runs, from its start to its end (a built-in's too), clear at
the prompt; bits 0-5 count the commands the CCP starts. The CCP writes it by function 49 through `$0203`, so an
RSX that redirects the console sees each change on its way down the chain and knows where the next command starts
and ends, as GET's and PUT's do. [`oodos/include/rsx.inc`](../../oodos/include/rsx.inc) has what OODOS's RSXs share:
their own BDOS calls in the caller's state put back, and reading the CCP's writes to `$19`.

## Chaining

A program can end by naming the command to run next: [function 47](07-function-reference.md#bdos-47) puts it at
`$0280`, ended by a 0, and the CCP runs it after the warm boot, as if typed, with the program's drive and user if
asked. The return code ([function 108](07-function-reference.md#bdos-108)) is kept for the program the command
runs, so the first can pass the second a word. The command loads as if typed, so a program with RSXs attached
brings them; the chaining program's end reaches the RSXs as function 0, as any end does ([RSXs](#rsxs)).
