# 2 The system interface

How a program calls OODOS, and what OODOS hands it: the BDOS call on the 6502, the program header, the base page,
the command tail, file control blocks, and how errors come back.

The design record, for the reasons: [hw/oodos.md, "Registers"](../../hw/oodos.md#registers), ["The program
header"](../../hw/oodos.md#the-program-header).

## Calling the BDOS

A program calls the BDOS with `JSR BDOS`, the base page's `$0203`
([`oodos.inc`](../../oodos/include/oodos.inc) names it `BDOS`):

- **Y** holds the function's number. Use oodos.inc's names: `LDY #C_WRITESTR`, not `LDY #9`.
- **A** holds a byte parameter (CP/M's E). A word parameter, often an address, goes in **A (low) and X (high)**
  (CP/M's DE).
- **The result** comes back in A, a byte (CP/M's A or L) with X 0, or in A and X, a word (CP/M's HL). An error a
  function returns is A = `$FF`, with the code in X where the error mode says so ([below](#error-modes)).
- **Y and the flags are not kept.** The decimal flag is clear on return.

```
        lda     #<msg                   ; a word: low in A,
        ldx     #>msg                   ; high in X
        ldy     #C_WRITESTR             ; the function in Y
        jsr     BDOS
```

`$0203` jumps to `$BFFD`, the last three bytes of the TPA, which jump to the BDOS's stub in common memory (from
phase 5, `$0203` may jump to an RSX instead: [hw/oodos.md, "RSXs and
programs"](../../hw/oodos.md#rsxs-and-programs)). The stub keeps the program's stack pointer, flags and windows,
switches to the system's windows and stack, calls the BDOS and puts everything back. The program's zero page,
`$00`-`$EF`, is never touched: the BDOS works on a zero page and a stack of its own. Interrupts are off while the
BDOS runs. The stub costs 216 cycles a call, 126 in and 90 out (`test_oodos_sys`).

**Addresses** a program passes (an FCB, a string, a buffer, a parameter block) are its own CPU addresses, in
`$0000`-`$BFFF`; the BDOS reaches them through the windows the stub saved. An address in the I/O hole,
`$C000`-`$C7FF`, isn't memory: a function that meets one stops there and returns A = X = `$FF`.

A function number OODOS doesn't have returns A = X = `$FF`. The numbers keep CP/M 3's meanings, CP/M-86's for the
memory functions 51-58, and OODOS's own from 72 ([chapter 7](07-function-reference.md)).

## The program header

Every `.COM` and `.PRL` file starts with 16 bytes that describe it. They are in the file only: what goes into memory
is the image after them. One module of a program writes them with `OODOS_HEADER`, once; the link config
([`com.cfg`](../../oodos/cfg/com.cfg) or [`prl.cfg`](../../oodos/cfg/prl.cfg)) supplies the values.

| Offset | Size | oodos.inc | What |
|---|---|---|---|
| 0 | 4 | `OODOS_MAGIC` | the magic number, `$4F $4F $44 $1A`: "OOD", then ^Z, so TYPE shows `OOD` and stops |
| 4 | 1 | `OH_FLAGS` | flags: bit 0 (`OF_RELOC`) a `.PRL`, its bitmaps after the image; bit 1 (`OF_RSX`) an RSX; bit 7 (`OF_NOT816`) breaks the 65C816 rules, never set by OODOS's tools. The others are 0 |
| 5 | 1 | `OH_PAGE` | the link page: the image runs at this page × 256, `$03` for a `.COM` |
| 6 | 2 | `OH_SIZE` | the image's size, in bytes |
| 8 | 2 | `OH_BSS` | BSS: the bytes the program needs above its image, not in the file and not cleared |
| 10 | 1 | `OH_ZPBASE` | where the image's zero page segment starts |
| 11 | 1 | `OH_ZPSIZE` | that segment's size, at most `$F0` for a program |
| 12 | 1 | `OH_RSXCOUNT` | the attached RSXs (phase 5), 0 for none |
| 13 | 1 | | reserved, 0 |
| 14 | 2 | `OH_RSXREC` | the record of the attached RSX table (phase 5), 0 for none |

**The CCP loads a `.COM`** only if the magic number is there (else `NO PROGRAM`), no flag is set, the link page is
`$03`, and the image and its BSS end by `$BFFC`, the TPA's top (else `BAD PROGRAM HEADER`). It reads exactly the
image's size to `$0300`: nothing past the image changes, and the BSS isn't cleared. A `.PRL` and an RSX are the
phase 5 loader's ([chapter 6](06-program-formats.md)).

## The base page

The base page is CP/M's page 0, moved up to `$0200` with the same offsets, so a ported program keeps its
constants:

| Address | CP/M | oodos.inc | What |
|---|---|---|---|
| `$0200` | `$0000` | `BP_WBOOT`, `WBOOT` | `JMP` to the warm boot |
| `$0203` | `$0005` | `BP_BDOS`, `BDOS` | `JMP` to the BDOS (from phase 5, the top RSX) |
| `$0204` | `$0006` | `BP_TOP` | that `JMP`'s operand: the top of the TPA + 1, `$BFFD` |
| `$0250` | `$0050` | | the drive the program was loaded from, 1-16 (A: is 1) |
| `$0251` | `$0051` | | the first file specification's password: its address in the tail (2 bytes), then its length (`$0253`) |
| `$0254` | `$0054` | | the second's, the same way |
| `$025C` | `$005C` | `BP_FCB` | the default FCB, from the tail's first file specification |
| `$026C` | `$006C` | `BP_FCB2` | the second FCB, from the second |
| `$0280` | `$0080` | `BP_DMA` | the default DMA buffer, holding the command tail |
| `$0300` | `$0100` | `BP_TPA` | the TPA, where a `.COM`'s image starts |

The TPA runs from `$0300` to `$BFFC`, 48,381 bytes. Below the base page are the program's zero page (`$00`-`$EF`;
`$F0`-`$FF` is for RSXs) and its stack. There is no CP/M 2 drive and user byte at `$0004`: the BDOS's `JMP` is
there. The current drive and user are functions 25's and 32's.

**A program starts** at `$0300` with A, X and Y 0, the decimal flag clear, interrupts off, a fresh stack and the
warm boot's address on it, so its last RTS ends it as `JMP WBOOT` does. The DMA address is `$0280`, the error mode
the default, the console mode 0, the delimiter `$` and the multi-sector count 1; the return code is 0, unless the
program was chained to.

## The command tail

What follows the command's name on the line, its leading blank too, is the tail, at `$0280`:

- `$0280`: the tail's length, 0-126;
- `$0281` on: its characters, in capitals (the CCP takes every letter of the line as a capital), then a 0.

The CCP parses the tail's first two file specifications into the base page's FCBs, as function 152 parses one:
the drive (0 for none), the name and the type in capitals and blank-padded, a `*` filled out with `?`, bytes 12-15
0. `$027C`-`$027F` are 0 too. A delimiter after the first specification (`=`, `,` and the rest of 152's) is passed
over before the second, so `NEW=OLD` and `B:=A:X` fill both FCBs. An option list in brackets, `[...]`, isn't a
specification. A specification 152 refuses leaves its FCB blank and stops the parse: the CCP doesn't check a
program's tail, the program does. A password given with a specification (`NAME.TYP;SECRET`) isn't in the FCB: its
place in the tail and its length are at `$0251`-`$0256`, 0 and 0 for none.

The second FCB at `$026C` lies inside the first's last 20 bytes: a program that opens the first FCB copies the
second out first, as in CP/M.

## File control blocks

A file is named, opened, read and written through a file control block (FCB) in the program's memory: 36 bytes,
CP/M 3's layout.

| Offset | Name | What |
|---|---|---|
| 0 | DR | the drive: 0 the current one, 1-16 A:-P: |
| 1-8 | f1-f8 | the name, capitals, blank-padded; bit 7 of each is an attribute |
| 9-11 | t1-t3 | the type, the same way; bit 7 of t1 is read-only, of t2 SYS, of t3 archive |
| 12 | EX | the extent number |
| 13 | S1 | the last record's byte count, kept in the directory |
| 14 | S2 | the BDOS's: the extent number's high bits, and its own state |
| 15 | RC | the records in this extent |
| 16-31 | D0-D15 | the extent's blocks, the BDOS's; a rename's new name; a label's and an XFCB's password |
| 32 | CR | the current record in the extent, for sequential reads and writes |
| 33-35 | R0-R2 | the random record number, low byte first |

A program sets DR, the name and the type, and sets EX and CR to 0 before it opens or makes a file; bytes 13-31 are
the BDOS's from then on. Most functions read and write the first 33 bytes only; the random record functions
(33-36, 40) and function 99 use all 36. The attributes, and the bits of f5'-f8' that a function reads or sets, are
[chapter 3's](03-file-system.md#file-attributes); a protected file's password goes in the DMA buffer when the file
is opened ([chapter 3](03-file-system.md#passwords)).

An FCB may be anywhere in the program's memory: the BDOS copies it in and out by DMA. Function 152 fills one from a
typed name.

## Return and error codes

Each function says what it returns ([chapter 7](07-function-reference.md)). Three kinds come back:

- **A result:** a byte in A (X 0), or a word in A and X.
- **A return code** that the program is expected to look at: 0 for done, `$FF` for not found or no room, or a
  number a function gives a meaning (a directory code 0-3 from the searches, 17 and 18; 1 for the end of a file
  from 20; 1-6 from the random reads and writes). These come back in every error mode.
- **A physical error:** something the program may not be ready for, said or returned as the error mode decides
  ([below](#error-modes)). Returned, it is A = `$FF` and X = the code:

| X | Message | What |
|---|---|---|
| 1 | Disk I/O | the card couldn't read or write a sector |
| 2 | Read Only | the drive is read-only (function 28, or a read-only unit) |
| 3 | File R/O | the file is read-only: its t1' set, opened from user 0 as a SYS file, or opened without its password |
| 4 | Invalid Drive | no such drive, or one that couldn't be logged in |
| 7 | Password Error | the file is protected and its password wasn't given |
| 8 | File Exists | a make or a rename found the name there already |
| 9 | ? in Filename | a `?` where one name is needed |

The drive of the last error is the SCB's byte at `$51`, in every mode. [Appendix A](appendix-a-errors.md) lists
the codes by function.

**The program's return code** (function 108) is separate: a word a program sets for whatever runs next, which the
CCP's conditional commands read. The BDOS sets `$FFFD` when it ends a program for an error and `$FFFE` when it
ends one for a ^C.

## Error modes

Function 45 sets what a physical error does:

- **The default** (any mode but `$FE` and `$FF`; the one every program starts in): the BDOS says the error, ends
  the program by the warm boot and sets the return code to `$FFFD`.
- **`$FE`:** it says the error, then returns it, A = `$FF` and X = the code.
- **`$FF`:** it returns the error and says nothing.

The message is two lines, the second with the file for a function that has one:

```
OODOS Error On B: Disk I/O
BDOS Function = 15 File = README  .TXT
```

The name's 8 characters and the type's 3 are shown as the FCB has them, attributes stripped. In a return mode, a
multi-sector read or write keeps the records it did in the FCB before it returns, so the program can see how far
it got.

A program that wants to recover (try another drive, ask for a password, say it its own way) sets `$FF` or `$FE`,
and checks A after each call that can fail. The CCP runs in `$FF` and `$FE` itself, and sets the default before
each program.
