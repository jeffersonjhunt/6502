# Appendix G: How OODOS differs from CP/M 3

Every difference in one place, for someone porting a CP/M 3 program's logic: registers, addresses, functions ours or
refused, the file formats.

Each function's own difference is its entry's **CP/M 3** heading in [chapter 7](07-function-reference.md); this
appendix gathers them. The reasons are in [hw/oodos.md](../../hw/oodos.md#goals), which marks each choice of ours
as "ours".

## What doesn't differ

- The function numbers, and what each function does, for every function OODOS has.
- The FCB, its fields and their offsets; the attributes in the name's bit 7s; the random record R0-R2.
- The disk format: the directory, extents, directory labels, SFCB date stamps, XFCBs and passwords, so cpmtools
  read and write an OODOS drive.
- The error modes and the physical error codes.
- The SCB's offsets, for every field CP/M 3 documents.
- The DAT format of the date and time: days from 1 January 1978, the hour and minute in BCD.

## The CPU and the registers

- **No 8080 or Z80 programs,** now or later. An OODOS program is a 6502 program written to the same interface.
- **The call** is `JSR $0203`, CP/M's `CALL 5`: the function in Y (CP/M's C), a byte in A (E), a word in A low and
  X high (DE). A byte result comes back in A with X 0 (CP/M's A and L); a word in A/X (HL); a physical error's code
  in X (H). Y isn't kept; the flags come back as they were before the call, the decimal flag clear
  ([chapter 2](02-system-interface.md#calling-the-bdos)).
- **Memory that isn't the program's,** an FCB or a buffer in the I/O hole or on flash, returns A = X = `$FF`
  whatever the error mode, and does nothing ([appendix A](appendix-a-errors.md)).

## Addresses

| CP/M 3 | OODOS | What |
|---|---|---|
| `$0000` | `$0200` | the warm boot's `JMP`; the base page is CP/M's page 0 moved up two pages, offsets kept |
| `$0005` | `$0203` | the BDOS's `JMP`; its operand the TPA's top + 1 |
| `$0050`-`$0056` | `$0250`-`$0256` | the drive the program came from; the tail's passwords |
| `$005C`, `$006C` | `$025C`, `$026C` | the two FCBs |
| `$0080` | `$0280` | the default DMA buffer and the command tail; the DMA address after a reset (13) |
| `$0100` | `$0300` | where a `.COM` runs |

The zero page and the stack are the program's own: `$00-$EF` and page 1, which the BDOS never touches. `$F0-$FF`
is for RSXs ([chapter 1](01-introduction.md#the-memory-a-program-sees)).

## Programs

- **A header.** Every `.COM` and `.PRL` starts with 16 bytes of OODOS's, with a magic number the CCP checks, so a
  file that isn't an OODOS program isn't run ([chapter 6](06-program-formats.md#com-files)). Its exact size is in
  the header: the padding of the last record is never loaded.
- **The `.PRL`** is OODOS's own layout: the header, the image, then a page bitmap and a zero page bitmap, each on a
  record ([appendix B](appendix-b-prl-format.md)). It relocates zero page addresses as well as pages.
- **The 65C816 rules,** checked by the build ([chapter 6](06-program-formats.md#rules-for-programs-the-65c816)).
- **The CCP runs `.COM`s only,** and a typed `NAME.PRL` is refused (CP/M 3's CCP loads it as a `.COM`). The CCP is
  resident, not a transient, so there is no LOADER RSX: OODOS's loader is always there, and the CCP loads programs by
  function 59 ([chapter 6](06-program-formats.md#rsxs)). A `.SUB` runs through SUBMIT, as in CP/M 3, and
  `PROFILE.SUB` at the first prompt; SUBMIT's temporary files are `SUBnn.$$$` in user 0 (ours).
- **RSXs** are `.RSX` files in OODOS's `.PRL` layout with CP/M 3's 27-byte prefix. The loader keeps a table of up to
  15, and writes `$0203` and their links again from it at every warm boot. Every end of a program reaches them as
  function 0, once: a `JMP WBOOT`, an RTS or an error's end too. The SCB's `$19` says whether the CCP or a program
  runs.
- **The attached RSX table** follows the `.COM`'s image, not a header page before it, and may hold SCB entries.

## Functions OODOS adds

- **51-58, CP/M-86's memory functions:** the DMA segment, so a record reaches any physical memory, and blocks of
  the 1 MB allocated and freed in an MCB. Memory is handed out in 16 KB blocks of banks 1-5, not by the
  paragraph; the warm boot frees what a program allocated ([chapter 5](05-memory-and-banking.md)).
- **72-75, OODOS's own:** the versions and FARCALL's address (72), a CPU address's physical one (73), mounting an
  SD image as a drive (74), leaving OODOS for the ROM's monitor or Apple II mode (75).

## Functions OODOS hasn't

41, MP/M's test-and-write, returns A = `$FF` with X 0, as CP/M 3 answers it. Every number no Digital Research system
gives a meaning returns A = X = `$FF`. 38 and 39, MP/M's drive access and free drive
([DRV_ACCESS](07-function-reference.md#bdos-38), [DRV_FREEDRV](07-function-reference.md#bdos-39)), return A = X = 0,
the success CP/M 3 answers them with.

## Each function

Beyond the registers, which change for every function as above:

| No. | Name | How it differs |
|---|---|---|
| 0 | `P_TERMCPM` | the warm boot also frees the program's memory blocks and its DMA segment |
| 6 | `C_RAWIO` | `$FF`, `$FD` and `$FE` see a key the BDOS kept back, so none is lost |
| 10 | `C_READSTR` | how a long line lays out over the screen's rows; no `#` for ^U and ^R; ^C ends the program only in an empty line; 255 characters at most |
| 11 | `C_STAT` | in ^C-only mode other keys are dropped and the ^C kept for the next input |
| 13 | `DRV_ALLRESET` | the DMA address goes back to `$0280`; the card's units are read again first, so a swapped image loses its unwritten buffers |
| 16 | `F_CLOSE` | f5' is ignored: every close leaves the FCB open |
| 19 | `F_DELETE` | a password is checked after every other reason to refuse |
| 20, 33 | `F_READ`, `F_READRAND` | the DMA segment; a multi-sector buffer is checked whole first, and a call whose buffer isn't all memory moves nothing; 20 says 1 where an extent can't be closed as it moves on; codes 9 and 10 are never returned |
| 21, 34 | `F_WRITE`, `F_WRITERAND` | the DMA segment; a new block is the lowest free one, and zeroed; 21 says 1 where an extent can't be closed as it moves on; codes 9 and 10 are never returned |
| 22 | `F_MAKE` | the password mode is byte 8's bits 7-5; a make that can't have both its entries makes neither |
| 23 | `F_RENAME` | File Exists is checked before the old name is looked for |
| 27 | `DRV_ALLOCVEC` | returns `$FFFF`: the vectors are in the system's memory. Function 46 gives the free space |
| 31 | `DRV_DPB` | the DPB is in common memory, readable from any bank; a program doesn't change it |
| 35 | `F_SIZE` | R0-R2 is 0 for a file that isn't there |
| 37 | `DRV_RESET` | the units are read again first, as for 13 |
| 40 | `F_WRITEZF` | the same as 34, since every new block is zeroed |
| 45 | `F_ERRMODE` | the message names OODOS, and always gives the function and the file |
| 48 | `DRV_FLUSH` | a buffer that can't be written is kept for a later try; a swapped image's are dropped |
| 49 | `S_SCB` | a get returns the word; the operations CP/M 3 reserves are gets; a set takes effect at once, but the drive isn't checked or logged in as 14 would, and the user is kept to 0-15 |
| 50 | `S_BIOS` | the parameter block is OODOS's (A, X, Y and two words); BOOT is refused |
| 52 | `F_DMAGET` | writes the DMA address and segment to 4 bytes, where CP/M-86 returns them in registers |
| 57 | `MC_FREE` | a length past an allocation's end frees only to its end |
| 59 | `P_LOAD` | any program may call it; a `.COM` loads only at `$0300`; a file with RSXs attached is refused (only the CCP attaches them); X says why a load is refused; A/X = 0 only removes |
| 98 | `DRV_FREE` | drops the failed buffers of the blocks it frees |
| 99 | `F_TRUNCATE` | stamps the update and clears t3' in the entry that ends at R |
| 100 | `DRV_SETLABEL` | the label's stored mode has bit 0 set, saying a label is there; it is stamped whatever its mode; stamps asked for without SFCBs return `$FF` |
| 103 | `F_WRITEXFCB` | a new XFCB without a password given has none |
| 104, 105 | `T_SET`, `T_GET` | the clock holds 2000-2155: 104 of a date outside it sets the SCB's date, not the clock; 105 returns `$FF` past it |
| 107 | `S_SERIAL` | the same number in every copy |
| 152 | `F_PARSE` | bytes 24-35 are left alone; a failed parse leaves the FCB as it was |

## The console

- The screen understands a subset of the VT52's escapes, and the serial line gets their ANSI forms
  ([appendix E](appendix-e-control-characters.md)).
- Line editing is the banked CP/M 3's ([function 10](07-function-reference.md#bdos-10)).

## At the prompt

The User's Guide's entries say how each command differs ([chapter 4](../oodos-users-guide/04-built-in-commands.md),
[chapter 5](../oodos-users-guide/05-utilities.md)). GO, CLEAR, CLS, WOZMON and APPLE2 are ours.
