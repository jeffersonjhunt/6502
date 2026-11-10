# 7 Function reference

One entry for each BDOS function, in number order: what goes in, what comes out, the errors, what else changes,
and how it differs from CP/M 3's function of the same number. Every entry has the same headings
([docs/templates/function.md](../templates/function.md)) and an anchor, `bdos-` and the number, so
`07-function-reference.md#bdos-9` is function 9's. [docs/index/functions.json](../index/functions.json) lists every
function number with its anchor once its entry is written.

How to call a function, and how registers and numbers are written here, is in
[chapter 2](02-system-interface.md#calling-the-bdos) and [docs/README.md](../README.md#conventions). In short:
`JSR BDOS` with the function in Y, a byte in A, a word in A (low) and X (high); the result comes back in A, or A and X.
Y is not kept; the flags come back as they were before the call, with the decimal flag clear.

RSXs ([chapter 6](06-program-formats.md#rsxs)) see every call before the BDOS does, and may answer some
themselves; each entry here is what the BDOS does when the call reaches it.

<a id="bdos-0"></a>
### 0 P_TERMCPM: end the program

#### Entry

- Y = 0 (`P_TERMCPM`). A and X are not read.

#### Exit

It doesn't return: the warm boot starts the CCP.

#### Errors

None.

#### Side effects

The warm boot, as `JMP WBOOT` (`$0200`) and a program's last RTS give it:

- The BIOS's banks go back to 0 and a pending XMOVE is dropped; the DMA segment (function 51) is none again and
  every block the program allocated (53-56) is freed.
- The program's windows come back (blocks 0-2, and 62 in W3), with a fresh stack.
- The CCP writes `$0203` and the RSXs' links again from the loader's table, and the RSXs see function 0, once:
  this call, on its way here, or one the warm boot sends for any other end. Then it takes out the RSXs whose remove
  flag is `$FF` ([chapter 6](06-program-formats.md#rsxs)).
- The CCP writes back every changed buffer (as function 48) and frees the blocks of files written but not closed
  (as function 98): what a program wrote to a file it didn't close is lost. On O: and P:
  the disk is checked first, and a changed disk gets none of it
  ([removable drives](03-file-system.md#removable-drives-o-and-p)).
- The CCP sets the multi-sector count to 1, the console mode to 0 and the delimiter to `$`, and makes its own drive
  and user current again. The return code (function 108) stays as the program left it.
- A command left by function 47 runs next.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same. Freeing the program's memory blocks and its DMA segment at the warm boot is ours, as the memory
functions are CP/M-86's; so is the RSXs seeing function 0 for an end by `JMP WBOOT`, an RTS, an error or a ^C.

<a id="bdos-1"></a>
### 1 C_READ: read a key from the console

#### Entry

- Y = 1 (`C_READ`). A and X are not read.

#### Exit

- A = the key; X = 0. The call waits for one.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- A key kept by an earlier look at the console (functions 2, 9, 11, 111) is taken first.
- Unless bit 1 of the console mode (function 109) is set, a ^S stops until ^Q (as in
  [function 9](#bdos-9)), and a ^Q or ^P on its own is dropped; none of the three is returned, and the call waits
  on for another key.
- The key is echoed if it is graphic (`$20`-`$7E`), CR, LF, BS or a tab (expanded to the next eighth column). Any
  other comes back unechoed and takes no column: control characters, ^C among them, DEL and bytes over `$7F`.
- An echo goes to LST: too while printer echo is on, and moves the SCB's console column.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the key comes back in A with X 0. As in CP/M 3, a ^C is an ordinary key here
unless it follows a ^S.

<a id="bdos-2"></a>
### 2 C_WRITE: write a character to the console

#### Entry

- Y = 2 (`C_WRITE`).
- A = the character.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

As for each character of [function 9](#bdos-9):

- A tab (`$09`) moves to the next column that is a multiple of 8, unless bit 2 of the console mode asks for raw
  output.
- While printer echo is on (^P), the character goes to LST: as well, unless the output is raw.
- First the BDOS looks at the keyboard, unless bit 1 of the console mode is set or a key is kept already: ^S stops
  the output until ^Q, a ^Q or ^P alone is dropped, and any other key is kept, one, for the next console input.
- The SCB's console column counts each character from `$20` up (DEL and `$80`-`$FF` too): CR sets it to 0, BS takes
  it back one, and the other control characters (LF, BEL, ESC, ...) leave it.

#### Cycles

5,706 cycles a character to the screen and the serial line, from the BDOS's entry to its return, as
`test_oodos_sys` measured it in phase 3b2; the look at the keyboard is 265 of them, and the serial card's wait for
the character, 3,840, most of the rest. The dispatch changes since have added a few cycles to every call
(function 11's went from 323 to 329). The stub in common memory adds 216 (126 in, 90 out).

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the character goes in A, where CP/M 3 takes it in E.

<a id="bdos-3"></a>
### 3 A_READ: read a character from AUX:

#### Entry

- Y = 3 (`A_READ`). A and X are not read.

#### Exit

- A = the character; X = 0. The call waits for one.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- AUX: is the devices the SCB's AUX: input vector names: TTY, the serial line, unless DEVICE has moved it. TTY is
  part of CON: too, so this function and the console read the same line, and a character goes to whichever asks
  first.
- A vector with no device that has input (none, or LPT alone) returns ^Z (`$1A`) at once.
- Nothing is echoed; ^S, ^Q, ^P and ^C are characters like any other.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the character comes back in A with X 0.

<a id="bdos-4"></a>
### 4 A_WRITE: write a character to AUX:

#### Entry

- Y = 4 (`A_WRITE`).
- A = the character.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- The character goes, as it is, to every device the SCB's AUX: output vector names: TTY, the serial line, unless
  DEVICE has moved it. The serial line turns the screen's VT52 sequences into ANSI
  ([appendix E](appendix-e-control-characters.md#the-vt52-subset)).
- The console column doesn't move; there is no tab expansion and no look at the keyboard.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the character goes in A, where CP/M 3 takes it in E.

<a id="bdos-5"></a>
### 5 L_WRITE: write a character to LST:

#### Entry

- Y = 5 (`L_WRITE`).
- A = the character.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- The character goes, as it is, to every device the SCB's LST: vector names: LPT unless DEVICE has moved it. LPT
  takes the character and drops it: the OO-1 has no parallel port yet.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the character goes in A, where CP/M 3 takes it in E.

<a id="bdos-6"></a>
### 6 C_RAWIO: the console without the BDOS's handling

#### Entry

- Y = 6 (`C_RAWIO`).
- A = `$FF`: return a key if one waits; `$FE`: return the console's status; `$FD`: wait for a key and return it;
  any other value: write A to the console.

#### Exit

- `$FF`: A = the key, or 0 if none waits. `$FD`: A = the key. X = 0.
- `$FE`: A = `$FF` if a key waits, 0 if not; X = 0.
- A character written: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- A key kept by an earlier look at the console is taken first by `$FF` and `$FD`, and `$FE` reports it.
- No key is handled: ^S, ^Q, ^P and ^C come back as keys, and nothing is echoed.
- A character written goes to the console's devices as it is: no tab expansion, no printer echo, no look at the
  keyboard, and the SCB's console column doesn't move.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers. That `$FF`, `$FD` and `$FE` see a key the BDOS kept is ours: CP/M 3's function
6 passes the BDOS by, but a key the BDOS kept and then never gave out would sit there unread.

<a id="bdos-7"></a>
### 7 A_STATIN: is an AUX: character waiting?

#### Entry

- Y = 7 (`A_STATIN`). A and X are not read.

#### Exit

- A = `$FF` if a device of the SCB's AUX: input vector has a character, 0 if not; X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers.

<a id="bdos-8"></a>
### 8 A_STATOUT: is AUX: ready for a character?

#### Entry

- Y = 8 (`A_STATOUT`). A and X are not read.

#### Exit

- A = `$FF` if every device of the SCB's AUX: output vector is ready, 0 if not; X = 0. Each of OODOS's devices
  says it is always ready (TTY waits out each character as it sends it), so A is `$FF` today.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers.

<a id="bdos-9"></a>
### 9 C_WRITESTR: print a string

#### Entry

- Y = 9 (`C_WRITESTR`).
- A/X = the string's address. The address is the program's own, anywhere in `$0000`–`$BFFF`.
- The string ends at the output delimiter, `$` (`$24`) unless function 110 has changed it. The delimiter is not
  printed.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

Each character goes to the console as function 2 sends it:

- A tab (`$09`) moves to the next column that is a multiple of 8, unless bit 2 of the console mode (function 109)
  asks for raw output.
- While printer echo is on (^P), each character goes to LST: as well, unless the output is raw.
- Before each character the BDOS looks at the keyboard, unless bit 1 of the console mode turns that off or a key is kept
  already (then a ^S isn't seen until a read takes it). A ^Q or ^P on its own is dropped; any other key but ^S is kept,
  one, for the next console input. ^S stops the output: then ^Q goes on, ^P turns printer echo over and stays stopped,
  ^C ends the program by the warm boot (unless bit 3 of the console mode), and any other key rings the bell.
- The SCB's console column follows what was printed.

The CCP sets the delimiter back to `$` and the console mode to 0 at each warm boot, so a program starts with both.

#### Cycles

Not measured. `test_oodos_sys` measures function 2's cost a character, not function 9's.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the string's address goes in A and X, where CP/M 3 takes it in DE, and A and X
come back 0.

#### Example

[`docs/examples/writestr.s`](../examples/writestr.s), built and run from the CCP by `test_docs_examples`:

<!-- listing: ../examples/writestr.s -->
```
; WRITESTR: prints a line through BDOS function 9, C_WRITESTR, then ends by
; the warm boot. The Programmer's Guide's example for function 9.

        .include "oodos.inc"

        OODOS_HEADER                    ; once per program

        .code
start:  lda     #<msg                   ; the string's address: low in A,
        ldx     #>msg                   ; high in X
        ldy     #C_WRITESTR             ; the function in Y
        jsr     BDOS
        jmp     WBOOT                   ; back to the CCP

        .rodata
msg:    .byte   "Written by function 9", 13, 10, '$'
```

At the prompt, with WRITESTR.COM on M:, it prints ([`writestr.out`](../examples/writestr.out)):

<!-- listing: ../examples/writestr.out -->
```
Written by function 9
```

<a id="bdos-10"></a>
### 10 C_READSTR: read an edited line from the console

#### Entry

- Y = 10 (`C_READSTR`).
- A/X = the buffer's address, the program's own. The buffer's byte 0 is its size, the most characters the line
  may hold (1-255; 0 is taken as 1).
- A/X = 0: the buffer is at the DMA address, and the characters already in it from byte 2, up to a 0 byte or the
  size, are taken first, as if typed. A CR among them ends the line at once.

#### Exit

- A = 0, X = 0.
- Byte 1 of the buffer = the line's length; the characters follow from byte 2. The CR or LF that ended the line
  isn't stored, and the bytes past the line are left as they were.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

An address in the I/O hole (`$C000`-`$C7FF`): the call stops there, A = X = `$FF`.

#### Side effects

- The line is edited, the cursor anywhere in it, a key typed inserted at the cursor. The editing keys are in
  [appendix E](appendix-e-control-characters.md#line-editing). Any other control character is part of the line,
  shown as `^` and its letter. A key past the buffer's size is dropped, with a bell.
- A ^C while the line is empty ends the program by the warm boot, with the return code `$FFFE`, unless bit 3 of
  the console mode is set. In a line that isn't empty it is a character, shown `^C`.
- The line is shown on rows of the console's width (the SCB's `$1A` holds the width less one: 79, for 80 columns), never
  in the last column. A row starts where the cursor was; typing at the end of a full row starts a new one; a move or a
  deletion before the row's start types the line again on a new row.
- The line ended becomes the previous line, for ^W. Only the CR that ends it is echoed, not an LF.
- What is echoed goes to LST: too while printer echo is on; ^P in the line turns printer echo on or off.
- A key kept by an earlier look at the console is taken first.

#### Cycles

5,994 cycles a key on average from typing it into the line to reading the next (`test_oodos_sys`, over SYSTEST's
154 keys, in phase 3b2): most of it the echo's BIOS CONOUT.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The editing is the banked CP/M 3's, but for the registers: the buffer's address goes in A and X, where CP/M 3
takes it in DE. Ours, where CP/M 3 doesn't say: how the line is laid out on the screen's rows. Ours, where it
differs: ^U and ^R print no mark, where CP/M 3 puts a `#` before the new row; and ^C ends the program only in an
empty line, where CP/M 3 takes one at the line's start. A line holds at most 255 characters.

<a id="bdos-11"></a>
### 11 C_STAT: is a console key waiting?

#### Entry

- Y = 11 (`C_STAT`). A and X are not read.

#### Exit

- A = 1 if a key waits, 0 if not; X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- A key found waiting is read and kept for the next console input (1, 6, 10). Before that, unless bit 1 of the
  console mode is set, a ^S stops until ^Q and then the call returns 0, and a ^Q or ^P alone is dropped (0).
- With bit 1 of the console mode set, A is the BIOS's status: nothing is read.
- With bit 0 of the console mode set, it reports ^C only: it reads the waiting keys until a ^C, which it keeps and
  returns 1 for; the others are dropped. The ^C stays kept, so the call stays 1 until a function 1 or 6 reads it.

#### Cycles

329 cycles with no key waiting, from the BDOS's entry to its return, as `test_oodos_sys` measured it after phase
3c's dispatch (323 in phase 3b2). The stub in common memory adds 216.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers. In ^C-only mode the keys that aren't ^C are dropped, and the ^C is kept for the
next input (ours: so the next ^C can be seen).

<a id="bdos-12"></a>
### 12 S_BDOSVER: the version number

#### Entry

- Y = 12 (`S_BDOSVER`). A and X are not read.

#### Exit

- A = `$31`, X = 0: the version of CP/M 3 whose functions OODOS keeps.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same value. OODOS's own version, and the BIOS's, are [function 72's](#bdos-72).

<a id="bdos-13"></a>
### 13 DRV_ALLRESET: reset every drive

#### Entry

- Y = 13 (`DRV_ALLRESET`).

#### Exit

- A = 0, X = 0; or an error (below).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A buffer that can't be written back is dropped all the same, and the reset is finished first. Then the first such
failure is raised, as the error mode says ([function 45](#bdos-45)): 1 Disk I/O, or 2 Read Only for a unit found
read-only. In return mode A = `$FF` and X = the code. [Appendix A](appendix-a-errors.md) has the codes and the
modes.

#### Side effects

- The BIOS reads the card's units again first. A drive whose image was swapped since its login has its buffers
  dropped unwritten, so nothing meant for the old image is written to the new one.
- Every changed buffer is written back, then every buffer dropped. On O: and P: the disk is checked before anything is
  written back, and a changed disk gets none of it ([removable drives](03-file-system.md#removable-drives-o-and-p)).
- Every drive is logged out and made read-write, including those made read-only by [function 28](#bdos-28).
- A: becomes the current drive (the SCB's `$3E`). The user number stays.
- The DMA address is set to `$0280` (the SCB's `$3C`). The DMA segment ([function 51](#bdos-51)) is kept.
- A search that [function 18](#bdos-18) would go on with is ended.

The CCP runs function 13 when ^C is typed on an empty prompt line, and only then; an ordinary warm boot doesn't.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for two things. The DMA address goes back to `$0280`, OODOS's default buffer, where CP/M 3's is
`$0080`. The units are read again first, so a card image swapped while a drive was logged in loses its unwritten
buffers rather than taking them (ours).

<a id="bdos-14"></a>
### 14 DRV_SET: select the current drive

#### Entry

- Y = 14 (`DRV_SET`).
- A = the drive: 0 for A:, 1 for B:, up to 15 for P:.

#### Exit

- A = 0, X = 0; or an error (below).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

4 Invalid Drive: A is 16 or more, or the drive isn't one OODOS has. Today that is A:–G: (the SD units, each only
while an image is mounted on it), M: (the RAM disk), and O: and P: (the Disk II's drives) on a machine with the
card. 1 Disk I/O if the directory can't be read. Each as the
error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).

#### Side effects

- The drive is logged in if it isn't: its directory is read once, to build the allocation vector and the hash,
  and its label's mode is kept for the date stamps and passwords. A drive on a read-only unit is read-only from
  its login.
- The SCB's current drive (`$3E`) is set, only once the drive is selected.

The warm boot puts the CCP's own drive back, so a program's 14 lasts only until it ends.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the register: the drive goes in A, where CP/M 3 takes it in E.

<a id="bdos-15"></a>
### 15 F_OPEN: open a file

#### Entry

- Y = 15 (`F_OPEN`).
- A/X = the FCB's address. The BDOS reads its bytes 0–32 and writes them back.
- The program sets bytes 0–12: the drive (0 the current one, 1–16 A:–P:), the name and type (no `?`) and EX, the
  extent to open (normally 0). S2 is taken as 0. CR (byte 32) set to `$FF` asks for the file's byte count.
- On a drive with passwords on, the DMA buffer's first 8 bytes are the password, if the file has one.

#### Exit

- A = 0, X = 0: the file is open. A = `$FF`, X = 0: there is no such file.
- A = 10, X = 0: on O: or P:, with access stamps (below), another disk was found in the drive as the stamp was
  to be written; the FCB isn't opened, whatever the error mode
  ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that tests only for `$FF` takes
  this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

The FCB gets the extent's block pointers and RC (128 if the file goes past this extent, 0 if it doesn't reach it),
and the attributes f1'–f4' and t1'–t3' from the directory. f7' and f8' are the BDOS's: f8' is set when the file
was opened from user 0 (below), f7' when it is write-protected and its password wasn't given. With CR `$FF` on
entry, CR comes back as the byte count of the file's last record, whichever extent is opened: the S1 of the file's
last directory entry, the one with the highest extent, as [function 30](#bdos-30) sets it and as cpmtools keeps it
(in that entry only). 0 means a full last record, or no count kept. From OODOS 0.6; before it, open gave the opened
extent's S1, which for a file of more than one entry made by cpmtools is 0. The program sets CR to 0 itself before
it reads. Otherwise CR is left as it was.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)):

- 4 Invalid Drive; 1 Disk I/O.
- 9 ? in Filename: a `?` in the name or type.
- 7 Password Error: the file is read-protected and neither the DMA buffer's 8 bytes nor the default password
  ([function 106](#bdos-106)) is its password.

An FCB in the I/O hole (`$C000`–`$C7FF`) returns A = X = `$FF`, with nothing done.

#### Side effects

- The drive is logged in if it isn't.
- **From user 0:** a file not in the current user is looked for in user 0 with the SYS attribute (t2'). Found
  there, it opens read-only: f8' is set, and its later extents are user 0's too.
- **Write-protected:** opened without its password, f7' is set and kept as the FCB moves to other extents; a write
  is then File R/O.
- **Access stamp:** opening extent 0 stamps the access in the file's SFCB slot, if the drive's label asks for
  access stamps and the drive isn't read-only. OODOS's own drives ask for creation and update stamps only.

#### Cycles

6,694 cycles on an `oodos-8m` drive, BDOSENT to its return, measured by `test_oodos_sys`'s FSTEST
([hw/oodos.md](../../hw/oodos.md#the-archive-bit-and-the-update-stamp-item-10), 2026-11-04). The BDOS stub adds 220.
CR `$FF` costs a second look through the directory, for the file's last entry, as [function 35](#bdos-35) looks: on
M:, about 2,400 cycles more for a file of one entry and 3,900 for one of three (`test_oodos_load`, 2026-11-08),
more on a drive with more entries in use. On O: and P: the look may read directory sectors from the Disk II, each a
real read of milliseconds, when the file's later entries are in sectors not yet in the buffers. The look is made
before the access stamp, so an open that fails in it has stamped nothing. Without CR `$FF` there is no such look.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers: the FCB's address goes in A and X, where CP/M 3 takes it in DE, and success is A
= 0 with X = 0.

<a id="bdos-16"></a>
### 16 F_CLOSE: close a file

#### Entry

- Y = 16 (`F_CLOSE`).
- A/X = the FCB's address, as [function 15](#bdos-15), 22 or a read or write left it. The BDOS reads its bytes
  0–32 and writes them back.

#### Exit

- A = 0, X = 0: done. A = `$FF`, X = 0: the extent couldn't be put in the directory (below). A = 10, X = 0: on
  O: or P:, the disk was changed since the open; nothing written
  ([removable drives](03-file-system.md#removable-drives-o-and-p)).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- An extent that hasn't been written since it was opened or last closed needs nothing in the directory: the
  drive's changed buffers are written back, any failure there is ignored, and the result is 0.
- For a written extent, as the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)):
  2 Read Only, the drive is read-only (or its unit was found so as the buffers were written); 1 Disk I/O, a sector
  of one of the file's blocks couldn't be written. Both are found before the directory is touched.
- A = `$FF`: the directory is full and the extent has no entry, or the FCB and the entry name two different
  blocks for one place (two FCBs writing one file). The FCB and the entry stay as they were.
- 4 Invalid Drive. An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

- The drive's changed data buffers are written back before the entry that points at them, so the disk never has
  an entry pointing at a sector that wasn't written.
- The extent's block pointers are merged with its entry's, EX and RC the later, the blocks made permanent (a later
  [function 98](#bdos-98) won't free them).
- No stamp: the update is stamped by the first write after the open ([function 21](#bdos-21)), not by the close.
- The FCB is marked not written (S2's bit 7), and stays open: the program may go on reading and writing it.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers, and f5' (a partial close) is ignored: every close leaves the FCB open, as a partial
close does (ours).

<a id="bdos-17"></a>
### 17 F_SFIRST: search for the first match

#### Entry

- Y = 17 (`F_SFIRST`).
- A/X = the FCB's address. The BDOS reads bytes 0–32; it doesn't write them back.
- Byte 0, the drive: 0 the current one, 1–16 A:–P:, or `?` (`$3F`) for every entry of the current drive.
- Bytes 1–11, the name and type: a `?` matches any character there.
- Byte 12, EX: `?` matches every extent; otherwise only that extent (with S2 taken as 0).

#### Exit

- A = 0–3, X = 0: found. The directory record (128 bytes, four entries) that holds it is at the DMA address, and
  A is the entry's place in it: the entry starts at the DMA address + 32 × A.
- A = `$FF`, X = 0: no match.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

4 Invalid Drive; 1 Disk I/O. As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).
An FCB in the I/O hole, or a DMA address (with a DMA segment, a physical address) that isn't the program's memory,
returns A = X = `$FF`.

#### Side effects

- The drive is logged in if it isn't.
- Only the current user's files match, up to the last entry in use. With `?` as the drive, every entry matches,
  to the directory's end: files of every user, XFCBs, the label, SFCBs and free entries.
- The pattern and the place are kept for [function 18](#bdos-18) until a search finds nothing more, or 13 or 37.
- The record goes to the DMA segment's physical address when one is set ([function 51](#bdos-51)).

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment, which CP/M 3 doesn't have.

<a id="bdos-18"></a>
### 18 F_SNEXT: search for the next match

#### Entry

- Y = 18 (`F_SNEXT`). Nothing else: the search is [function 17](#bdos-17)'s, as it was asked.

#### Exit

- A = 0–3, X = 0: the next match, its record at the DMA address, as 17 returns it.
- A = `$FF`, X = 0: no more, or no search to go on with.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

1 Disk I/O, as the error mode says ([function 45](#bdos-45)). A DMA address that isn't the program's memory
returns A = X = `$FF`; the next 18 goes on after that entry.

#### Side effects

- 17's pattern is kept by the BDOS, not read again from the FCB, so the program may change the FCB between calls,
  and other functions between them don't disturb it.
- A search ends at its last match, at 13 and 37, and when [function 74](#bdos-74) changes its drive.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the DMA segment, which CP/M 3 doesn't have.

<a id="bdos-19"></a>
### 19 F_DELETE: delete files

#### Entry

- Y = 19 (`F_DELETE`).
- A/X = the FCB's address: the drive, the name and type (`?` matches any character). The BDOS reads bytes 0–32;
  it doesn't change them.
- f5' set: delete only the files' XFCBs (their passwords), not the files.
- On a drive with passwords on, the DMA buffer's first 8 bytes are the password of a protected file.

#### Exit

- A = 0, X = 0: deleted. A = `$FF`, X = 0: nothing matched.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), in this order:

- 4 Invalid Drive; 2 Read Only, the drive is read-only.
- 3 File R/O: a match has the read-only attribute (t1'). Nothing is deleted.
- 7 Password Error: a match is protected and its password wasn't given. Nothing is deleted.
- 1 Disk I/O.

An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

- Every extent of every match in the current user is deleted, and the files' XFCBs with them. Each entry is gone
  from the disk before its blocks are freed.
- With f5', the XFCBs only, and the files' SFCB slots get a password mode of 0.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers. Password Error is checked last, after every other reason to refuse (ours).

<a id="bdos-20"></a>
### 20 F_READ: read the next record

#### Entry

- Y = 20 (`F_READ`).
- A/X = the address of an FCB opened by [function 15](#bdos-15). The BDOS reads bytes 0–32 and writes them back.
- CR (byte 32) is the record of the extent to read.

#### Exit

- A = 0, X = 0: read. The record, 128 bytes, is at the DMA address; CR has moved on by one.
- A = 1: the end of the file, or a record never written. X = the records read before it, with a multi-sector
  count (below); 0 with a count of 1.
- A = X = `$FF`: the DMA buffer isn't the program's memory (below). Nothing is read.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = 10, whatever the error mode: on O: or P:, the disk was changed since the open; nothing read
(with a multi-sector count, no more after the change). X isn't the records done then: open the file again. Else,
as the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 1 Disk I/O; 2 Read Only,
when the read moves on from an extent that was written, on a drive that is read-only, and so can't close it, or
for O: or P: when a buffer the read takes held that drive's data and its disk is write-protected (said after the
read, once: [appendix A](appendix-a-errors.md#physical-errors));
4 Invalid Drive. In return mode, the FCB is written back as the records read before the error left it. A written
extent the read can't close as it moves on (the FCB and the entry name two blocks for one place) is A = 1, the FCB
left at its extent: [function 33](#bdos-33) says 3.

The DMA buffer is checked first, all of it for a multi-sector count: past `$FFFF`, in the I/O hole, on flash or in
a read-only window, or with a DMA segment memory OODOS keeps for itself, is A = X = `$FF`.

#### Side effects

- After record 127 of an extent the next is opened (a written one is closed first).
- **Multi-sector:** with a count N set by [function 44](#bdos-44), one call reads N records, record k to the DMA
  address + 128 × k. The DMA address itself doesn't change. It stops at the first that isn't read.
- With a DMA segment ([function 51](#bdos-51)), the records go to physical memory, segment × 16 + the DMA address.

#### Cycles

Measured by `test_oodos_sys`'s FSTEST on an `oodos-8m` drive, BDOSENT to its return: 4,914 cycles for a record in a
sector already in a buffer, 13,069 when the sector must be read; with a count of 128 on M:, 562,907 cycles, 4,397 a
record ([hw/oodos.md](../../hw/oodos.md#the-archive-bit-and-the-update-stamp-item-10), measured again 2026-11-04).
The BDOS stub adds 220.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment. With a multi-sector count, the whole buffer is checked before
the first record moves, and a call whose buffer isn't all memory reads nothing (ours). OODOS never returns CP/M 3's
code 9 (an FCB a failed close made invalid). It returns 10 (the disk changed since the open) on O: and P:
only, nothing read ([removable drives](03-file-system.md#removable-drives-o-and-p)); a swapped card image is
handled when the units are read again ([function 13](#bdos-13)). The count of the drive's changes 10 is judged
by is kept in the FCB's S1, bits 0-6 (ours).

<a id="bdos-21"></a>
### 21 F_WRITE: write the next record

#### Entry

- Y = 21 (`F_WRITE`).
- A/X = the address of an FCB opened by [function 15](#bdos-15) or made by [function 22](#bdos-22). The BDOS reads
  bytes 0–32 and writes them back.
- CR is the record of the extent to write; the 128 bytes are at the DMA address.

#### Exit

- A = 0, X = 0: written; CR has moved on by one.
- A = 1: no directory space for the next extent. A = 2: the disk is full, or the file would pass 32 MB. X = the
  records written before it, with a multi-sector count; 0 with a count of 1.
- A = X = `$FF`: the DMA buffer isn't the program's memory. Nothing is written.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = 10, whatever the error mode: on O: or P:, the disk was changed since the open; nothing written
(with a multi-sector count, no more after the change). X isn't the records done then: open the file again. Else,
as the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), before anything is written:
4 Invalid Drive; 2 Read Only, the drive is read-only; 3 File R/O, the file is read-only (t1'), was opened from
user 0, or is write-protected and was opened without its password. Then 1 Disk I/O. The DMA buffer is
checked after those, as [function 20](#bdos-20) checks it (a read-only window may be written from).

A write writes the directory itself when it clears an entry's t3' or makes the update stamp (below), and that
write can fail (1 Disk I/O, or 2 Read Only for a unit found read-only) after the record is already in a buffer. In
return mode the call then says the error with CR not moved on: write the record again, or close the file, as for
any physical error.

One Read Only is different: when the SCB's error drive (`$51`, [function 49](#bdos-49)) is O: or P: and not the
FCB's drive, the write took a buffer that held that other floppy's waiting data, and its disk is write-protected.
That error is said after the write is done: the record is written and CR has moved on, so don't write it again
([appendix A](appendix-a-errors.md#physical-errors)). 34 and 40 are the same.

#### Side effects

- A record in a block the file doesn't have yet takes the lowest free block, which is zeroed: a record of it never
  written reads as zeros.
- After record 127 of an extent the extent is closed and the next is opened, or made.
- Multi-sector and the DMA segment as [function 20](#bdos-20).
- The data stays in the BDOS's buffers until a buffer is needed, a close, [function 48](#bdos-48), a drive reset
  or the warm boot writes it back.
- **The archive attribute:** a write clears t3' in the directory entry of the extent it writes, at once, and in the
  FCB ([chapter 3](03-file-system.md#file-attributes)). The other extents' entries keep theirs.
- **The update stamp:** the first write since the file was opened stamps the update in the file's extent-zero SFCB
  slot, if the label asks for update stamps, at once; the writes after it don't, nor does the close. After a make of
  extent 0, which stamped it, no write of that FCB does. The FCB's S1 (byte 13) remembers it: bit 7 set once the
  stamp is made; Open clears it and Make of extent 0 sets it. An FCB put together by hand from a directory entry,
  not opened, carries whatever byte 13 it was given (an entry's byte count of 128 reads as stamped): open a file
  with [function 15](#bdos-15) or make it with [function 22](#bdos-22) before writing it.

#### Cycles

Measured by `test_oodos_sys`'s FSTEST on an `oodos-8m` drive, BDOSENT to its return: 7,818 cycles to a sector in a
buffer, 78,451 for a record that takes a new block, 161,986 for the cheapest that must write buffers back first; with
a count of 128 on M:, 786,294 cycles, 6,142 a record
([hw/oodos.md](../../hw/oodos.md#the-archive-bit-and-the-update-stamp-item-10), measured again 2026-11-04). The
first write after an open writes the stamp's SFCB, and a write to an archived extent its entry, besides. The BDOS
stub adds 220.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment, and choices of ours:

- a new block is the lowest free one, and is zeroed, where CP/M 3 leaves the records of it not yet written as they
  happen to be;
- a multi-sector buffer is checked whole before the first record;
- a written extent it can't close as it moves on is A = 1, as no directory space (CP/M 3 lists no 3 for 21);
- code 9 isn't returned, and 10 only on O: and P:, nothing written, as for [function 20](#bdos-20).

The update stamp and the archive attribute are CP/M 3's: the first write's, and cleared by a write.

<a id="bdos-22"></a>
### 22 F_MAKE: make a file

#### Entry

- Y = 22 (`F_MAKE`).
- A/X = the FCB's address. The BDOS reads bytes 0–32 and writes them back.
- Bytes 0–12: the drive, the name and type (no `?`), EX (normally 0). S2 is taken as 0.
- f6' set, on a drive with passwords on: give the file a password too. The DMA buffer's bytes 0–7 are the
  password and its byte 8 the mode (bit 7 read, bit 6 write, bit 5 delete).

#### Exit

- A = 0, X = 0: made, and open. A = `$FF`, X = 0: the directory is full.
- The FCB's attributes are all clear, its block pointers 0, RC 0, and it is marked not written. CR is left as it
  was: set it to 0 before writing.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 4 Invalid Drive; 9 ? in
Filename; 2 Read Only; 8 File Exists, the current user has the name in any extent; 1 Disk I/O. An FCB in the I/O
hole returns A = X = `$FF`.

#### Side effects

- A new directory entry for extent EX, every attribute clear.
- Extent 0's entry is stamped with the creation and the update, as the label asks; this FCB's writes then make no
  update stamp ([function 21](#bdos-21)). A make of another extent stamps nothing, and the FCB's first write the
  update.
- With f6' and passwords on, the file's XFCB too, in a free entry after the file's. It needs two free entries (or
  an XFCB of the name to use again), else `$FF` and nothing is made. Without passwords on, f6' is ignored.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers. Ours where CP/M 3 is silent: only bits 7–5 of the mode byte (the DMA buffer's
byte 8, where CP/M 3 has it too) count, and a make with f6' that can't have both entries makes neither.

<a id="bdos-23"></a>
### 23 F_RENAME: rename a file

#### Entry

- Y = 23 (`F_RENAME`).
- A/X = the FCB's address. Bytes 0–11 are the drive and the old name; bytes 17–27 the new name and type (byte 16
  isn't read). Neither may have a `?`. The BDOS reads bytes 0–32; it doesn't change them.
- On a drive with passwords on, the DMA buffer's first 8 bytes are a protected file's password.

#### Exit

- A = 0, X = 0: renamed. A = `$FF`, X = 0: no file of the old name.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), in this order:
4 Invalid Drive; 9 ? in Filename; 2 Read Only; 8 File Exists, the new name is the current user's already; then, once
the old name is found, 3 File R/O (t1') and 7 Password Error. 1 Disk I/O. An FCB in the I/O hole returns A = X =
`$FF`.

#### Side effects

Every extent of the file, and its XFCB, gets the new name. Each entry keeps its own attributes.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers. File Exists is checked before the old name is looked for (ours: the order isn't
given).

<a id="bdos-24"></a>
### 24 DRV_LOGINVEC: the logged-in drives

#### Entry

- Y = 24 (`DRV_LOGINVEC`).

#### Exit

- A/X = a bit for each drive logged in: bit 0 of A is A:, bit 7 of A H:, bit 0 of X I:, bit 4 of X M:.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the vector comes back in A and X, where CP/M 3 returns it in HL.

<a id="bdos-25"></a>
### 25 DRV_GET: the current drive

#### Entry

- Y = 25 (`DRV_GET`).

#### Exit

- A = the current drive, 0 for A: to 15 for P:; X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

None.

<a id="bdos-26"></a>
### 26 F_DMAOFF: set the DMA address

#### Entry

- Y = 26 (`F_DMAOFF`).
- A/X = the DMA address: where the records of 17, 18, 20, 21, 33, 34 and 40 go to and come from, and where the
  password functions and [function 46](#bdos-46) look.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None. The address isn't checked until a record moves.

#### Side effects

The SCB's DMA address (`$3C`) is set. The DMA segment ([function 51](#bdos-51)) is kept. The warm boot and
[function 13](#bdos-13) set the address back to `$0280`.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers. The default is `$0280`, OODOS's buffer in the base page.

<a id="bdos-27"></a>
### 27 DRV_ALLOCVEC: the allocation vector's address

#### Entry

- Y = 27 (`DRV_ALLOCVEC`).

#### Exit

- A = `$FF`, X = `$FF`.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The vectors are in OODOS's system block, where a program can't read them, so 27 returns `$FFFF` rather than an
address. [Function 46](#bdos-46) gives a drive's free space.

<a id="bdos-28"></a>
### 28 DRV_SETRO: make the current drive read-only

#### Entry

- Y = 28 (`DRV_SETRO`).

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

The current drive is read-only until [function 13](#bdos-13) or [37](#bdos-37) resets it: a write, make, delete or
other change to it is then 2 Read Only. A rescan that finds its image swapped makes it read-write again.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

None.

<a id="bdos-29"></a>
### 29 DRV_ROVEC: the read-only drives

#### Entry

- Y = 29 (`DRV_ROVEC`).

#### Exit

- A/X = a bit for each read-only drive, laid out as [function 24](#bdos-24)'s.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

A drive is read-only after [function 28](#bdos-28), from its login when its unit is read-only, and once a write to
its unit has been refused.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: A and X, where CP/M 3 returns HL.

<a id="bdos-30"></a>
### 30 F_ATTRIB: set a file's attributes

#### Entry

- Y = 30 (`F_ATTRIB`).
- A/X = the FCB's address: the drive and the name and type (no `?`), with bit 7 of f1'–f4' (the program's
  attributes) and t1'–t3' (read-only, SYS, archive) as they are to be. The BDOS reads bytes 0–32 and doesn't
  change them.
- f6' set: CR (byte 32) is the byte count of the file's last record, to be kept with it.
- On a drive with passwords on, the DMA buffer's first 8 bytes are a protected file's password.

#### Exit

- A = 0, X = 0: done. A = `$FF`, X = 0: no such file.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 4 Invalid Drive; 9 ? in
Filename; 2 Read Only; 7 Password Error, the file is protected and the password wasn't given; 1 Disk I/O. A read-only
file isn't an error: 30 is how its t1' is cleared. An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

Every extent's entry gets f1'–f4' and t1'–t3' from the FCB; f5'–f8' are cleared on the disk. With f6', every
entry's S1 is set to CR, which [function 15](#bdos-15) gives back.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers.

<a id="bdos-31"></a>
### 31 DRV_DPB: the current drive's parameters

#### Entry

- Y = 31 (`DRV_DPB`).

#### Exit

- A/X = the address of the current drive's DPB, in common memory (`$F800`–`$FFFF`), which every program can read.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

The DPB is 17 bytes, CP/M 3's layout: SPT (a word: 64, or 32 on an Apple disk), BSH, BLM, EXM, DSM (a word), DRM
(a word), AL0, AL1, CKS (a word: `$8000`, fixed media; 16 on O: and P:, a checksum for each directory record),
OFF (a word: 0, or 3 boot tracks on an Apple disk), PSH (2) and PHM (3): 512-byte sectors.

| Drive | BSH | Block | DSM | DRM | Size |
|---|---|---|---|---|---|
| `oodos-8m` | 5 | 4 KB | 2,047 | 1,023 | 8 MB |
| `oodos-32m` | 6 | 8 KB | 4,095 | 2,047 | 32 MB |
| M: | 4 | 2 KB | 63 | 255 | 128 KB |
| `apple-do` (B:–G:, O:, P:) | 3 | 1 KB | 127 | 63 | 128 KB |

#### Errors

4 Invalid Drive; 1 Disk I/O. As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).

#### Side effects

The current drive is logged in if it isn't.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers. The DPB is in common memory, so the address works from any bank. A program reads
it and doesn't change it.

<a id="bdos-32"></a>
### 32 F_USERNUM: get or set the user number

#### Entry

- Y = 32 (`F_USERNUM`).
- A = `$FF` to get the user number; anything else sets it to A's low four bits (0–15).

#### Exit

- Getting: A = the user number, X = 0. Setting: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

Setting changes the SCB's current user (`$44`). The warm boot puts the CCP's user back.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the register: A, where CP/M 3 takes E.

<a id="bdos-33"></a>
### 33 F_READRAND: read a random record

#### Entry

- Y = 33 (`F_READRAND`).
- A/X = the address of an open FCB of 36 bytes. The BDOS reads bytes 0–35 and writes them back.
- R0–R2 (bytes 33–35, low first) is the record's number in the file, 0 to 262,143.

#### Exit

- A = 0, X = 0: read, the record at the DMA address.
- A = 1: a record never written. A = 3: the extent the FCB is at can't be closed, to move to the record's. A = 4:
  the record's extent was never written. A = 6: R2 is over 3, past 32 MB. X = the records read before it, with a
  multi-sector count; 0 with a count of 1.
- A = X = `$FF`: the DMA buffer isn't the program's memory.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

R0–R2 isn't moved on. The FCB is left at the record's extent with CR the record's place there, so a
[function 20](#bdos-20) next reads the same record again, and 21 writes it.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 1 Disk I/O; 2 Read Only, to
move off a written extent on a read-only drive; 4 Invalid Drive. The buffer is checked first, as 20 checks it.

#### Side effects

- Multi-sector as [function 20](#bdos-20): N records from R0–R2 on. At the end R0–R2 is the first record's again;
  EX and CR are the last record tried's.
- The DMA segment as 20.

#### Cycles

7,938 cycles for a record in a buffered sector on M:, BDOSENT to its return, and 8,176 through a DMA segment,
measured by `test_oodos_sys` ([hw/oodos.md](../../hw/oodos.md#the-bdos-phase-3), phase 3c). The BDOS stub adds
216.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment, and the whole buffer checked first, as 20. Code 10 is
returned on O: and P: only, as for [function 20](#bdos-20).

<a id="bdos-34"></a>
### 34 F_WRITERAND: write a random record

#### Entry

- Y = 34 (`F_WRITERAND`).
- A/X = the address of an open FCB of 36 bytes; R0–R2 the record's number. The BDOS reads bytes 0–35 and writes
  them back. The 128 bytes are at the DMA address.

#### Exit

- A = 0, X = 0: written.
- A = 2: the disk is full. A = 3: the extent the FCB is at can't be closed. A = 5: no directory space for the
  record's extent. A = 6: past 32 MB. X = the records written before it, with a multi-sector count.
- A = X = `$FF`: the DMA buffer isn't the program's memory.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

R0–R2 and the FCB are left as [function 33](#bdos-33) leaves them.

#### Errors

As [function 21](#bdos-21): 4 Invalid Drive, 2 Read Only, 3 File R/O before anything is written, then 1 Disk I/O,
also from the directory write a write may make (t3', the update stamp); as the error mode says
([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).

#### Side effects

- The record's extent is made if the file hasn't one. Records skipped over aren't written. Read back, one in a
  block a write took, below the extent's RC, is zeros, since every new block is zeroed; one in a block no write
  took, or past RC, returns 1; one in an extent with no entry, 4; and 3 if the extent the FCB is at can't be closed
  to move there ([chapter 3](03-file-system.md#random-records)).
- Multi-sector and the DMA segment as [function 33](#bdos-33).
- The archive attribute and the update stamp as [function 21](#bdos-21): t3' cleared in the extent's entry, the
  update stamped by the first write since the open.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment; a new block is zeroed and the lowest free one, and code 10
is returned on O: and P: only, all as for [function 21](#bdos-21). Its update stamp and archive attribute are CP/M 3's, as 21's.

<a id="bdos-35"></a>
### 35 F_SIZE: a file's size

#### Entry

- Y = 35 (`F_SIZE`).
- A/X = the address of a 36-byte FCB: the drive and the name and type (no `?`). It needn't be open. The BDOS
  reads bytes 0–35 and writes them back.

#### Exit

- A = 0, X = 0: R0–R2 = the file's size in records: the number of the record after its last.
- A = `$FF`, X = 0: no such file in the current user. R0–R2 = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

The size is the end of the furthest extent, so a file written at random with gaps counts them.

#### Errors

4 Invalid Drive; 9 ? in Filename; 1 Disk I/O. As the error mode says ([function 45](#bdos-45),
[appendix A](appendix-a-errors.md)). An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers. R0–R2 is 0 for a file that isn't there (ours, as CP/M 2.2 did).

<a id="bdos-36"></a>
### 36 F_RANDREC: set the random record from the file's place

#### Entry

- Y = 36 (`F_RANDREC`).
- A/X = the address of a 36-byte FCB, at the place a sequential read or write has reached. The BDOS reads bytes
  0–35 and writes them back.

#### Exit

- A = 0, X = 0. R0–R2 = the number of the record the FCB is at: its extent (EX and S2) × 128 + CR.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None. The disk isn't read. An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

None.

<a id="bdos-37"></a>
### 37 DRV_RESET: reset drives

#### Entry

- Y = 37 (`DRV_RESET`).
- A/X = a bit for each drive to reset, laid out as [function 24](#bdos-24)'s.

#### Exit

- A = 0, X = 0; or an error (below).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As [function 13](#bdos-13): a buffer that can't be written back is dropped, the reset finished, then the first
failure raised as the error mode says (1 Disk I/O, 2 Read Only).

#### Side effects

- The units are read again first, as 13 does.
- Each drive named has its changed buffers written back, its buffers dropped, and is logged out and made
  read-write. Other drives, the current drive and the DMA address are as they were. On O: and P: the disk is
  checked before anything is written back, and a changed disk gets none of it
  ([removable drives](03-file-system.md#removable-drives-o-and-p)).
- A search that [function 18](#bdos-18) would go on with is ended.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers: the vector goes in A and X, where CP/M 3 takes DE. The units are read again
first, as for 13 (ours).

<a id="bdos-38"></a>
### 38 DRV_ACCESS: claim drives

#### Entry

- Y = 38 (`DRV_ACCESS`). Anything in A and X is ignored.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None: one program runs at a time, so there is no drive to claim.

#### Cycles

Not measured.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same: an MP/M function CP/M 3 doesn't support, answered with success. A program written for MP/M may call it
before using a drive; on OODOS, as on CP/M 3, it does nothing. (Until 2026-11-04 OODOS answered it with A = X = `$FF`.)

<a id="bdos-39"></a>
### 39 DRV_FREEDRV: release drives

#### Entry

- Y = 39 (`DRV_FREEDRV`). Anything in A and X is ignored.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None: one program runs at a time, so there is no drive to release.

#### Cycles

Not measured.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same: an MP/M function CP/M 3 doesn't support, answered with success. A program written for MP/M may call it
when it is done with a drive; on OODOS, as on CP/M 3, it does nothing. (Until 2026-11-04 OODOS answered it with A = X = `$FF`.)

<a id="bdos-40"></a>
### 40 F_WRITEZF: write a random record, zero-filled

#### Entry

As [function 34](#bdos-34), with Y = 40 (`F_WRITEZF`).

#### Exit

As [function 34](#bdos-34).

#### Errors

As [function 34](#bdos-34).

#### Side effects

As [function 34](#bdos-34). A new block's other records are zeros, as they are for every write.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

CP/M 3 zeroes a new block only for 40. OODOS zeroes every block a write takes, so 40 is 34, Media Change (10) on
O: and P: included.

<a id="bdos-42"></a>
### 42 F_LOCK: lock records

#### Entry

- Y = 42 (`F_LOCK`). Anything in A and X is ignored.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None: one program runs at a time, so there is nothing to lock.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

None: it succeeds and does nothing, as CP/M 3's does.

<a id="bdos-43"></a>
### 43 F_UNLOCK: unlock records

#### Entry

- Y = 43 (`F_UNLOCK`). Anything in A and X is ignored.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

None: it succeeds and does nothing, as CP/M 3's does.

<a id="bdos-44"></a>
### 44 F_MULTISEC: set the multi-sector count

#### Entry

- Y = 44 (`F_MULTISEC`).
- A = the count: how many 128-byte records each call of functions 20, 21, 33, 34 and 40 moves, 1 to 128.

#### Exit

- A = 0, X = 0: the count is set.
- A = `$FF`, X = 0: the count was 0 or over 128, and the old one is kept.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None but the `$FF` above.

#### Side effects

The count is the SCB's byte at `$4A` ([appendix C](appendix-c-scb.md)), its only home: function 49 reads it there,
and can set it too, to any byte (a count over 128 set that way is used as it is, 0 as 1). The CCP sets it back to 1
at each warm boot, so every program starts with 1.

With a count of N, a call of 20, 21, 33, 34 or 40 does what N calls would, with the FCB read once and written back
once:

- Record k goes to or from the DMA address + 128 × k. The DMA address itself doesn't move.
- The call stops at the first record that doesn't succeed. If that is not a physical error (A isn't `$FF`), X is the
  number of records done before it.
- 33, 34 and 40 put R0–R2 back to the first record's. EX and CR are the last record tried's.
- Before the first record, the whole span, the DMA address to + 128 × N, must be the program's memory; if any of
  it isn't, A = X = `$FF` and nothing is done ([chapter 3](03-file-system.md#multi-sector-io)).

#### Cycles

Not measured. What a count saves a record is in [function 20](#bdos-20)'s Cycles.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but X comes back 0 with A.

<a id="bdos-45"></a>
### 45 F_ERRMODE: set the error mode

#### Entry

- Y = 45 (`F_ERRMODE`).
- A = the mode: `$FF` return errors; `$FE` say them, then return them; any other value the default, which says
  them and ends the program.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- The mode is the SCB's byte at `$4B`. It decides what a physical error (Disk I/O, Read Only, File R/O, Invalid
  Drive, Password Error, File Exists, ? in Filename) does in every later call:
  - **Default:** the BDOS prints the message, as [chapter 2](02-system-interface.md#error-modes) shows, and ends the
    program by the warm boot with the return code `$FFFD`.
  - **`$FE`:** it prints the message, then returns A = `$FF` and X = the error's code.
  - **`$FF`:** it returns A = `$FF` and X = the code, and prints nothing.
- In either return mode, a multi-sector call keeps the records it did in the FCB before it returns.
- The CCP sets the default mode before each program it runs.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same modes, but for the registers: the code comes back in X, where CP/M 3 returns it in H. The message names
OODOS, and is always the long form, with the function and the file.

<a id="bdos-46"></a>
### 46 DRV_SPACE: a drive's free space

#### Entry

- Y = 46 (`DRV_SPACE`).
- A = the drive: 0 for A: to 15 for P:.

#### Exit

- A = 0, X = 0. The drive's free records, 128 bytes each, are the 3 bytes at the DMA address, low first.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

4 Invalid Drive; 1 Disk I/O. As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).
A DMA address in the I/O hole returns A = X = `$FF`.

#### Side effects

The drive is logged in if it isn't. The 3 bytes go to the DMA address as a CPU address: a DMA segment isn't used.

The count is the blocks not in use × the records a block. A file being written and not yet closed has its new
blocks counted as in use.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the register: the drive goes in A, where CP/M 3 takes E.

<a id="bdos-47"></a>
### 47 P_CHAIN: end the program and run a command

#### Entry

- Y = 47 (`P_CHAIN`).
- A = `$FF`: the CCP takes the program's current drive and user as its own. Any other value: the CCP's drive and
  user stay as they were.
- The command at `$0280`, the base page's buffer, ended by a 0 byte: what would be typed at the prompt. The DMA
  address doesn't matter; the CCP reads `$0280`. At most 126 characters are taken; the rest are cut off.

#### Exit

It doesn't return: the program ends by the warm boot, and the CCP runs the command.

#### Errors

None from the call. The command's own errors are the CCP's, as if it had been typed
([the User's Guide, chapter 6](../oodos-users-guide/06-errors.md)).

#### Side effects

- The SCB's `$17` gets the request: bit 7 a command waits, bit 6 with the program's drive and user. The CCP reads
  it and clears it at the warm boot.
- The warm boot does all it does after any program: the program's memory is freed, and so on
  ([function 0](#bdos-0)).
- The CCP shows its prompt and the command, as if typed, then runs it. The command takes the place of whatever was
  left of a line of several commands, and may hold `!` itself.
- The return code ([function 108](#bdos-108)) is kept for the program the command runs, which doesn't start with
  0. So a program can pass it a word.
- The RSXs see the program's end as function 0, once, as for any end ([function 0](#bdos-0)); the chained
  command's program brings its own attached RSXs when it loads.

#### Cycles

Not measured.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same, but for the registers: the flag goes in A, where CP/M 3 takes it in E, and the command is at `$0280`,
OODOS's default buffer. Ours, where CP/M 3 is silent: the command is shown after the prompt; it replaces the rest
of a line; and more than 126 characters are cut off. With A = `$FF` the program's drive and user become the
CCP's own, so the prompt keeps them after the chained command ends, where CP/M 3 sets them up only for the program
chained to.

<a id="bdos-48"></a>
### 48 DRV_FLUSH: write the buffers back

#### Entry

- Y = 48 (`DRV_FLUSH`).
- A = `$FF` to drop every buffer as well, once written; anything else keeps them.

#### Exit

- A = 0, X = 0; or an error (below).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

Every changed buffer is tried, past a failure. Then the first failure is raised, as the error mode says
([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 1 Disk I/O, or 2 Read Only for a unit (or a
floppy) found read-only: the drive is made read-only and all its changed buffers are dropped, as none can be
written.

#### Side effects

- A drive whose unit has changed since its login has its buffers dropped unwritten first.
- Every changed buffer is written back. One that fails is kept, marked, and tried again by the next 48 or close;
  [function 13](#bdos-13) or 37 drops it. With A = `$FF` all are dropped all the same. On O: and P: the disk
  is checked before anything is written back, and a changed disk gets none of it
  ([removable drives](03-file-system.md#removable-drives-o-and-p)).

The CCP calls 48, then [98](#bdos-98), at every warm boot, and says a failure there as the BDOS says its errors.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the register: A, where CP/M 3 takes E. Buffers that can't be written are kept for a later try,
and a swapped image's are dropped, not written to it (ours).

<a id="bdos-49"></a>
### 49 S_SCB: get or set a field of the SCB

#### Entry

- Y = 49 (`S_SCB`).
- A/X = the address of a 4-byte parameter block:
  - +0 the field's offset in the SCB, `$00`-`$63` ([appendix C](appendix-c-scb.md));
  - +1 the operation: 0 get, `$FF` set a byte, `$FE` set a word; any other is taken as a get;
  - +2-3 the value to set, low byte first.

#### Exit

- A get: A = the byte at the offset, X = the byte after it (0 at `$63`).
- A set: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- An offset past `$63`: nothing is done, A = 0, X = 0.
- An address in the I/O hole: A = X = `$FF`.

#### Side effects

- The SCB is where the BDOS keeps this state, with no other copy, so a set takes effect at once: the DMA address
  is function 26's, the drive and user 14's and 32's, the error mode 45's, the console mode 109's, the delimiter
  110's, the multi-sector count 44's, the vectors the BIOS's devices.
- A drive set so isn't checked: one that isn't there fails Invalid Drive when a function uses it. The user is kept
  to 0-15, its low four bits, after every set.
- A word set at `$63`, the SCB's last byte, sets only that byte.
- The date and time are the clock as last read: a set lasts until the BDOS next reads the clock, for a date stamp
  or function 105. Function 104 sets the clock.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3's parameter block and offsets, but for the registers: a get returns the word in A and X, where CP/M 3 gives the
byte in A and L and the next in H. Ours: the operations CP/M 3 reserves are a get, and a word set at `$63` sets one
byte. A set of the DMA address, the drive or the user takes effect at once, as the SCB is the BDOS's only copy, but the
drive isn't checked or logged in as function 14 would, and the user is kept to 0-15. The SCB is in the system's memory,
not the program's: a program reaches it only through this function.

<a id="bdos-50"></a>
### 50 S_BIOS: call the BIOS

#### Entry

- Y = 50 (`S_BIOS`).
- A/X = the address of an 8-byte parameter block:
  - +0 the BIOS function: 1 WBOOT, 2 CONST, 3 CONIN, 4 CONOUT, 5 LIST, 6 AUXOUT, 7 AUXIN, 8 HOME, 9 SELDSK,
    10 SETTRK, 11 SETSEC, 12 SETDMA, 13 READ, 14 WRITE, 15 LISTST, 16 SECTRN, 17 CONOST, 18 AUXIST, 19 AUXOST,
    20 DEVTBL, 21 DEVINI, 22 DRVTBL, 23 MULTIO, 24 FLUSH, 25 MOVE, 26 TIME, 27 SELMEM, 28 SETBNK, 29 XMOVE;
  - +1 the BIOS's A, +2 its X, +3 its Y;
  - +4-5 and +6-7 its two words: MOVE's source and destination, SECTRN's table in the first.
- What each BIOS function takes and returns is [chapter 4's](04-console-devices-scb.md#direct-bios-calls-function-50).

#### Exit

- A and X as the BIOS function left them. The words it changed (MOVE's) aren't copied back to the block.
- MOVE's and TIME's carry don't reach the program. A MOVE that returns `$FFFF` was refused, or ended at `$FFFF`;
  the program tells which by whether its destination plus the count is `$FFFF`.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- Function 0, BOOT, which would run the cold boot again and wipe M:, and any function past 29: A = X = `$FF`.
- An address in the I/O hole: A = X = `$FF`.

#### Side effects

- The BIOS's addresses are in bank 0, the system's view, unless the program names bank 1, its own: SETBNK for
  SETDMA's buffer, SELMEM for MOVE's, XMOVE for the next MOVE's two sides. The warm boot sets them back to 0.
- The BDOS forgets which drive the BIOS has selected, and selects its own again before its next sector, so a
  program's SELDSK can't send the BDOS's reads to another drive.

#### Cycles

Not measured. `test_oodos_sys` measures a BIOS call through the jump table, 51 cycles more than a JMP, not
function 50's cost.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The parameter block is OODOS's: a byte each for A, X and Y and two words, where CP/M 3's holds the 8080's
registers. BOOT is refused. The program gets its own flags back.

<a id="bdos-51"></a>
### 51 F_DMASEG: set the DMA segment

#### Entry

- Y = 51 (`F_DMASEG`).
- A/X = the segment. The segment is a paragraph, a 16-byte step of the 1 MB. `$FFFF` is none.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None when it is set: any value is taken. It is checked when a record moves (below).

#### Side effects

With a segment, the records of functions 17, 18, 20, 21, 33, 34 and 40 go to and from physical memory at the
segment × 16 + the DMA address (function 26's), not the program's CPU address. Nothing else uses it: FCBs, console
strings and buffers, function 46's free space and the passwords read from the DMA buffer stay CPU addresses.

A segment may reach any RAM but OODOS's own: banks 0–5, 8–B (the video memory) and E. Refused are banks 6–7 (the
hash, the buffers, the system block), C–D (M:), blocks 60–63 (the BDOS, the BIOS, OODOS's resident block, the ROM
BIOS's copy) and anything past `$FFFFF`. A DMA address whose record would run past `$FFFF` is refused, as it is
without a segment. A refused record is A = X = `$FF` with nothing done: checked before the first record for 20, 21,
33, 34 and 40, and when the record moves for 17 and 18. From OODOS 0.5 a record read (17, 18, 20, 33) into the
console's video memory, units 0–12 (`$80000–$833FF`), is refused too, when it moves: that memory is OODOS's
([function 83](#bdos-83), [chapter 8](08-game-services.md#video-memory)).

The segment lasts until the program sets another or ends: each warm boot sets it back to `$FFFF`. Function 13 sets
the DMA address to `$0280` and keeps the segment.

#### Cycles

Not measured. What a segment adds to a record is in [function 33](#bdos-33)'s Cycles: 238 cycles.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no function 51; this is CP/M-86's, with the segment in A/X. CP/M-86's segment reaches all of an 8086's
memory; OODOS's refuses its own blocks.

<a id="bdos-52"></a>
### 52 F_DMAGET: get the DMA address and segment

#### Entry

- Y = 52 (`F_DMAGET`).
- A/X = the address of 4 bytes for the answer.

#### Exit

- The 4 bytes: +0–1 the DMA address (function 26's), +2–3 the segment (function 51's; `$FFFF` none).
- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = X = `$FF`: the 4 bytes are in the I/O hole, `$C000`–`$C7FF`. Nothing is written.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no function 52; this is CP/M-86's, which returns the two in registers. OODOS writes them to 4 bytes the
program names (ours).

<a id="bdos-53"></a>
### 53 MC_MAX: allocate the largest free run

#### Entry

- Y = 53 (`MC_MAX`).
- A/X = the MCB's address. The MCB is 5 bytes ([chapter 5](05-memory-and-banking.md#allocating-memory)):
  +0–1 M_BASE, +2–3 M_LENGTH, the most wanted, in paragraphs; +4 M_EXT.

#### Exit

- The MCB: M_BASE the run's first paragraph, M_LENGTH its length in paragraphs (a multiple of 1,024), M_EXT 1 if a
  free block is left outside it, else 0.
- A = 0, X = 0: the run is allocated.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FF`, X = 0: no free block, or M_LENGTH is 0. The MCB is as it was.
- A = X = `$FF`: the MCB is in the I/O hole. Nothing is done.

#### Side effects

Allocates the longest run of free blocks, the lowest of runs of equal length, cut to M_LENGTH rounded up to whole
16 KB blocks. The blocks are the program's until it frees them (57, 58) or ends: the warm boot frees them all.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 53, the MCB at A/X. Memory is handed out in 16 KB blocks from
banks 1–5, not by the paragraph.

<a id="bdos-54"></a>
### 54 MC_ABSMAX: allocate the largest run at a base

#### Entry

- Y = 54 (`MC_ABSMAX`).
- A/X = the MCB's address: +0–1 M_BASE, a block's first paragraph in banks 1–5 (`$1000`,
  `$1400`, ... `$5C00`); +2–3 M_LENGTH, the most wanted; +4 M_EXT.

#### Exit

- The MCB: M_BASE as given, M_LENGTH the run's length in paragraphs, M_EXT 1 if a free block is left outside it,
  else 0.
- A = 0, X = 0: the run is allocated.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FF`, X = 0: M_BASE isn't a block's first paragraph in banks 1–5, its block is taken, or M_LENGTH is 0.
- A = X = `$FF`: the MCB is in the I/O hole. Nothing is done.

#### Side effects

Allocates the free blocks from M_BASE up to the first one taken, at most M_LENGTH rounded up to whole blocks. The
run is shorter than asked if a block in it is taken (ours).

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 54, in 16 KB blocks.

<a id="bdos-55"></a>
### 55 MC_ALLOC: allocate memory

#### Entry

- Y = 55 (`MC_ALLOC`).
- A/X = the MCB's address: +2–3 M_LENGTH, the paragraphs wanted.

#### Exit

- The MCB: M_BASE the first paragraph of the lowest free run long enough, M_LENGTH the length rounded up to whole
  blocks (a multiple of 1,024). M_EXT is as it was.
- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FF`, X = 0: no free run that long, or M_LENGTH is 0. Nothing is allocated.
- A = X = `$FF`: the MCB is in the I/O hole. Nothing is done.

#### Side effects

The blocks are the program's until it frees them (57, 58) or ends.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 55, in 16 KB blocks.

<a id="bdos-56"></a>
### 56 MC_ABSALLOC: allocate memory at a base

#### Entry

- Y = 56 (`MC_ABSALLOC`).
- A/X = the MCB's address: +0–1 M_BASE, a block's first paragraph in banks 1–5; +2–3
  M_LENGTH, the paragraphs wanted.

#### Exit

- The MCB: M_LENGTH rounded up to whole blocks. M_BASE and M_EXT are as they were.
- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FF`, X = 0: M_BASE isn't a block's first paragraph in banks 1–5, M_LENGTH is 0, or a block of the length
  from M_BASE is taken. Nothing is allocated: the run isn't shortened (ours).
- A = X = `$FF`: the MCB is in the I/O hole. Nothing is done.

#### Side effects

The blocks are the program's until it frees them (57, 58) or ends.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 56, in 16 KB blocks.

<a id="bdos-57"></a>
### 57 MC_FREE: free memory

#### Entry

- Y = 57 (`MC_FREE`).
- A/X = the MCB's address:
  - M_EXT (+4) = `$FF`: everything is freed, as function 58.
  - Otherwise M_BASE (+0–1) is an allocation's first paragraph, and the whole allocation is freed, whatever
    M_LENGTH says.
  - Or M_BASE is the first paragraph of a later block of an allocation, and M_LENGTH (+2–3) reaches that
    allocation's end or past it: the blocks from M_BASE to the end are freed, and its head stays allocated.

#### Exit

- A = 0, X = 0. The MCB is not written.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FF`, X = 0: M_BASE isn't a block's first paragraph in banks 1–5, its block is free, or (for a later block)
  M_LENGTH is 0 or stops short of the allocation's end. Nothing is freed.
- A = X = `$FF`: the MCB is in the I/O hole. Nothing is done.

#### Side effects

The blocks freed may be allocated again by 53–56.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 57. Where CP/M-86 doesn't say, the rules are ours: a length past
an allocation's end frees only to it, and a shorter one frees nothing.

<a id="bdos-58"></a>
### 58 MC_ALLFREE: free all memory

#### Entry

- Y = 58 (`MC_ALLFREE`).

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

Every block the program allocated is free. The warm boot does the same, so a program that ends without 58 leaks
nothing.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 has no memory functions; this is CP/M-86's 58.

<a id="bdos-59"></a>
### 59 P_LOAD: load an overlay

#### Entry

- Y = 59 (`P_LOAD`).
- A/X = an FCB the program has opened (function 15), its random record R0-R1 the address to load at, low byte
  first. A `.PRL` loads at any page start from `$0300`, relocated there; a `.COM` only at `$0300`.
- A/X = 0: nothing is loaded; only the RSXs whose remove flag is `$FF` are taken out.

#### Exit

- A = 0, X = 0: loaded. Nothing runs: the program calls or jumps into what it loaded.
- R0-R2 hold the load address again (R2 0).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = `$FE`, refused before anything past the image changes, X why: 0 the address (not a page start for a `.PRL`,
  below `$0300`, a `.COM` anywhere but `$0300`) or no room below the lowest RSX (or `$BFFD`); 1 the header (flags
  other than a `.COM`'s or a `.PRL`'s, an RSX file, a `.COM` not linked at `$0300`); 2 a `.PRL` with zero page of
  its own; 3 not a program (no magic number, an empty file, an image of size 0 with nothing attached); 4 RSXs
  attached: only the CCP's loads attach them, so a program's 59 of such a file is refused.
- A = 1: the file ends inside its image or its bitmaps.
- A = `$FF`, X the physical error, from reading the file.

Errors are returned in every error mode: the loader reads with errors returned, and says nothing.

#### Side effects

- The image, at the address, and a `.PRL`'s bytes its bitmaps mark moved to it; BSS above it isn't cleared.
- Taken out first: the RSXs whose remove flag is `$FF` (but those an RSX-only `.COM` left for this program), the
  program's own attached RSXs among them, as in CP/M 3. An RSX meant to outlive its program's overlay loads starts
  with its remove flag 0 and sets it to `$FF` when it is done, as CP/M 3's RSXs do.
- A file's SCB entries are set before 59 returns.
- The DMA address and segment, the error mode and the multi-sector count are as they were before the call.

#### Cycles

Not measured.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

CP/M 3 answers 59 from its LOADER RSX, there only while a program with an RSX header runs; OODOS's loader is always
there, so any program may call it, and the CCP loads programs through it. A `.COM` loads only at `$0300` (ours:
CP/M 3 loads an absolute file anywhere from `0100H`). A `.PRL` needs room only for its image and BSS (CP/M 3 wants
room for the whole file). X's reasons with A = `$FE`, A/X = 0's removal alone, and refusing a file with RSXs
attached (CP/M 3's LOADER loads them for a program too), are ours.

<a id="bdos-60"></a>
### 60 P_RSX: call an RSX

#### Entry

- Y = 60 (`P_RSX`).
- A/X = an RSX parameter block: a function byte, a count of words, then the words.

#### Exit

- An RSX that takes the block's function byte answers it, as it documents.
- None does: A = `$FF`, X = `$FF`.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None but A = X = `$FF` when no RSX takes it.

#### Side effects

None from the BDOS.

#### Cycles

Not measured.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same. Function bytes 0-127 are for programs' RSXs, 128-255 the system's; OODOS's own RSXs use 128-255 (GET's
`$80` and `$81`, PUT's `$90`-`$92`: [chapter 6](06-program-formats.md#rsxs)).

<a id="bdos-72"></a>
### 72 O_VERSION: OODOS's version and FARCALL

#### Entry

- Y = 72 (`O_VERSION`).
- A/X = the address of a 10-byte block to fill.

#### Exit

- The block:

  | Offset | What |
  |---|---|
  | +0, +1 | OODOS's version: major, minor (0, 4) |
  | +2, +3 | the BIOS's version: major, minor (0, 2): OODOS's own BIOS, in OODOS.SYS, not the ROM's (the boot status's `BIOS 0.12`) |
  | +4-5 | the memory fitted, in KB: 1,024 |
  | +6-7 | the memory the memory functions can give programs, in KB: 320, banks 1-5. The total, fixed: not what is free |
  | +8-9 | FARCALL's address in common memory |

- A/X = FARCALL's address.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

An address in the I/O hole: A = X = `$FF`.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 72 (hw/oodos.md, ["OODOS's own"](../../hw/oodos.md#oodoss-own)). FARCALL is in
[chapter 5](05-memory-and-banking.md#far-calls-farcall-and-the-far-call-table).

<a id="bdos-73"></a>
### 73 O_PHYSADDR: a CPU address's physical address

#### Entry

- Y = 73 (`O_PHYSADDR`).
- A/X = the address of 4 bytes. +0–1 hold a CPU address, the program's.

#### Exit

- The 4 bytes: +0 the offset (0–15), +1 0, +2–3 the paragraph. The physical address is the paragraph × 16 + the
  offset.
- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = X = `$FF`: the address is in the I/O hole (`$C000`–`$C7FF`), or its window is on flash, which has no physical
  address; or the 4 bytes are in the I/O hole. The 4 bytes are as they were.

#### Side effects

None. The address is translated through the windows the program had when it called: W3 is a far call's block if
one is mapped. A read-only window translates (it is RAM).

The two halves fit the DMA functions: function 51 with the paragraph and function 26 with the offset name the same
byte.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 73.

<a id="bdos-74"></a>
### 74 O_MOUNT: mount or unmount an SD drive

#### Entry

- Y = 74 (`O_MOUNT`).
- A/X = the address of a block:
  - +0 the drive, numbered as function 14's: 1–6 are B:–G:.
  - +1 the flags: bit 0 set mounts the image read-only.
  - +2 the image's path on the card, ended by a 0 byte, as `DRIVES.TXT` writes it: from the root, so it starts
    with `/`; names separated by `/`, none empty, `.` or `..`; no space, tab, `#`, control character or DEL; at most
    509 bytes. An empty path (a 0 at +2) unmounts the drive.

#### Exit

- A = 0, X = 0: done. Unmounting a drive with nothing mounted is done too.
- A = `$FF` and X the reason (below).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

74 returns its errors whatever the error mode (function 45), and prints nothing. A = `$FF` and X:

- 1: the card failed (no card, no FAT, a timeout), or the drive's changed buffers couldn't be written back.
- 4: not a drive 74 changes: A: (the system drive), M:, or H: on.
- `$10`: the path is refused.
- `$11`: no such file (or it is a directory).
- `$12`: the image isn't 8 MB, 32 MB or a 140 KB Apple CP/M disk (16,384, 65,536 or 280 sectors).
- `$13`: another drive has a file of that name mounted (the card's name for it, as `DRIVES.TXT`'s rule has it).

A = X = `$FF`: the block is in the I/O hole.

#### Side effects

In order, after the drive and the path are checked: the drive's changed buffers are written back (one that fails
refuses the call, nothing changed); the buffers are dropped and the drive logged out and made read-write; a search
by 17 and 18 on it ends; the card mounts or unmounts the image; the card's units are read again; the new image is
checked. An image that fails the checks is unmounted again, so the drive has none.

A drive with no image is no drive: using it is Invalid Drive, as for a drive `DRIVES.TXT` doesn't name. The next
use of a newly mounted drive logs it in.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 74. It does what the loader does for each line of `DRIVES.TXT`
([hw/oodos.md, "Drives"](../../hw/oodos.md#drives)).

<a id="bdos-75"></a>
### 75 O_ROM: leave OODOS for the ROM

#### Entry

- Y = 75 (`O_ROM`).
- A = 0: the ROM BIOS's Woz Monitor, as Break goes there. A = 1: Apple II mode, through the ROM's `$C806`.

#### Exit

- A = 0, and A = 1 with a IIe ROM in the cartridge and the DMA controller fitted: it doesn't return.
- A = 1 without the DMA controller (its ID register, `DMA_ID` at `$C47F`, doesn't read `$81`): A = `$FF`, X = `$FE`,
  and OODOS goes on. Apple II mode clears the Apple's memory by DMA, so the ROM won't start it without one.
- A = 1 with no IIe ROM in the cartridge (`$FBB3` isn't `$06`), or any other A: A = X = `$FF`, and OODOS goes on.
- Every return is a failure, A = `$FF`; X tells the two cases of A = 1 apart. The DMA controller is looked at
  first.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As in Exit.

#### Side effects

- For A = 0 or 1, the changed buffers are written back first, as function 48 does, even when there is no IIe ROM
  to go to. Without the DMA controller nothing is written back: OODOS looks for it before anything else, and
  OODOS's own card transfers need it too. A file left open isn't closed: a program closes its files first. On O: and P: the disk is checked
  before anything is written back, and a changed disk gets none of it
  ([removable drives](03-file-system.md#removable-drives-o-and-p)).
- OODOS doesn't come back: the ROM's monitor, or a reset, is what follows.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 75. A program can't leave by itself, as W3 is OODOS's and a program may only call
the BDOS. The CCP's WOZMON and APPLE2 use it.

<a id="bdos-76"></a>
### 76 O_SECTOR: a DOS 3.3 sector of an Apple disk

#### Entry

- Y = 76 (`O_SECTOR`).
- A/X = the address of a 6-byte block in the program's memory: the drive (0 A: to 15 P:), the track (0–34), the
  sector in DOS 3.3's numbering (0–15), 0 to read or 1 to write, and the address of the sector's 256 bytes (a
  word) in the program's memory, the CPU's address: the DMA segment isn't used.

#### Exit

- A = 0, X = 0: done; a read's 256 bytes are at the address.
- A = 1, X = 0: the sector couldn't be read or written (no disk, a bad sector, the card's error), or the track,
  sector or operation is past what there is.
- A = 2, X = 0: a write to a write-protected disk (the floppy's notch, or a unit `RO` in `DRIVES.TXT`), or to a
  drive set read-only ([function 28](#bdos-28), SET `[RO]`). Nothing is written; the drive stays read-only.
- A = `$FF`, X = 0: the drive isn't an Apple disk. O: and P: are, when the machine has the Disk II card; an SD
  drive is when its unit is a 140 KB image.
- A = X = `$FF`: the block, or the 256 bytes, are in memory that isn't the program's (the I/O hole, flash).
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None raised, whatever the error mode: each is a result in A.

#### Side effects

- If the drive is logged in, its changed buffers are written back, then all its buffers are dropped and it is
  logged out, as [function 37](#bdos-37) does: its next CP/M use reads the disk again, so what 76 wrote is what
  CP/M sees. If a buffer can't be written, A = 1 and nothing more is done.
- Unlike 37, 76 leaves a read-only drive read-only, logged out or not: the bit outlives the logout until 13 or 37
  clears it, as [function 28](#bdos-28) says.
- On an SD drive, the units are read again first (as at a reset) at the program's first 76 call on it, for every
  write, and for a read when the drive isn't yet known as a 140 KB image. The BIOS counts each unit's mount
  changes, however they're seen (a select, 13, 37, 74, 76), and nothing clears the count: 76 keeps it at the
  program's first call on the drive (the warm boot starts that again), and refuses a write, A = `$FF`, when it has
  moved since, so a unit swapped on the card while the program runs takes none of its writes. A read after such
  a swap reads the new image, unchecked. O: and P: aren't read again: a changed floppy is the program's to see.
- The sector is the disk's own: on an SD unit at (track × 16 + sector) × 256 in its image file, on a floppy at its
  physical place by DOS 3.3's interleave. CP/M's skew isn't used, and nothing of DOS 3.3's file system is read or
  kept: that is the program's. DOS33 ([the User's Guide](../oodos-users-guide/05-utilities.md#util-dos33)) is one.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 76 (OODOS's range, 72 on). CP/M 3 would leave this to the BIOS's own
functions, called through 50; OODOS's BDOS owns the buffers, so it is the one to drop them.

<a id="bdos-80"></a>
### 80 O_FRAME: frame and timing

#### Entry

- Y = 80 (`O_FRAME`).
- X = 0: A is the operation, and it takes no parameters.
- X not 0: A/X is the address of a parameter block in the program's memory. Its +0 is the operation, and the
  operation's parameters follow it. The operations are oodos.inc's `FT_` names:

  | A or +0 | Name | The block, after +0 | What it does |
  |---|---|---|---|
  | 0 | `FT_INFO` | 4 bytes to fill (optional) | says which services OODOS has |
  | 1 | `FT_COUNT` | 8 bytes to fill (optional) | reads the millisecond and frame counters |
  | 2 | `FT_OPEN` | +1–2 the event area's address, +3 its size | opens the event area |
  | 3 | `FT_CLOSE` | none | closes it |
  | 4 | `FT_WAIT` | +1 a count of frames (optional) | waits for the next frame, or for that many |
  | 5 | `FT_DELAY` | +1–2 a count of milliseconds | waits that long |
  | 6 | `FT_FRAMEHOOK` | +1–2 the hook's address, 0 none | sets the frame hook, or takes it off |
  | 7 | `FT_TICKHOOK` | +1–2 the hook's address, 0 none; +3–4 every n ms | sets the tick hook, or takes it off |
  | 8 | `FT_COSTS` | 6 bytes to fill | the hooks' costs, as a debug build counts them |

#### Exit

- `FT_INFO`: A = OOLIB's interface version (7: 1 was L1's, 2 L2's, 3 L3's, 4 L4's; 5 L4b's hold, go and
  volume; 6 L5a's mailbox stamp; 7 OODOS 0.8's `EV_SFLAGS`, [chapter 8](08-game-services.md#the-6510)), X = a bit for each service area OODOS
  has (`OA_FRAME`, `$01`: this function; `OA_INPUT`, `$02`: [function 81](#bdos-81); `OA_SOUND`, `$04`: [function
  82](#bdos-82); `OA_DMA`, `$08`: [function 83](#bdos-83)). With a block: +1 and +2 the same, +3 the
  clocks that run (`FC_FRAME` `$01` the frame counter, a video card; `FC_MS` `$02` the millisecond counter, the
  serial card's VIA), +4 the highest event area layout it writes (2).
- `FT_COUNT`: A/X = the millisecond counter's low word. With a block: +1–4 the millisecond counter, +5–8 the
  frame counter, each low byte first, read at one moment.
- `FT_OPEN`, `FT_CLOSE`, `FT_DELAY`, `FT_FRAMEHOOK`, `FT_TICKHOOK`: A = 0, X = 0.
- `FT_WAIT`: returns at the frame wanted. A/X = the frame counter's low word.
- `FT_COSTS`: +1–2 the most cycles a frame hook took, +3–4 a tick hook, +5–6 the frames whose hook wasn't called
  because the last was still running, each since the warm boot. A = 1 if this OODOS counts the cycles (a debug
  build, on a machine with the VIA, whose timer 1 it counts by), 0 if not, and then +1–4 are 0. X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = X = `$FF`, and nothing done, for an operation past 8, one that needs a block given none, a block or a field in
memory that isn't the program's (the I/O hole, flash), and:

- `FT_OPEN`: a size under 16 (`EV_LEN`; 0 is 256); an area that crosses a 16 KB window, is in W3 (`$C000` up), or is in a
  block that isn't the program's RAM in banks 0–5 (flash, a read-only window, OODOS's own memory, M:, video memory).
- `FT_WAIT` with no video card, `FT_DELAY` with no VIA: their clock doesn't run.
- `FT_DELAY` when the millisecond counter stands still for 4 frames: timer 1 has stopped (a program changed it,
  against [chapter 8's rules](08-game-services.md#hooks)), so the wait would never end. It returns early, A = X =
  `$FF`. The frame counter is the witness, so without a running frame counter (no video card, or its vertical
  blank turned off) it waits on.
- `FT_FRAMEHOOK`, `FT_TICKHOOK`: an address in the I/O hole or common memory (`$C000–$C7FF`, `$F800` up), or a
  call made with W0 on the system's block; a tick hook every 0 ms. The old hook is off either way.

An OODOS before L1 answers 80 as a function it doesn't have: A = X = `$FF`. So `FT_INFO`'s A of `$FF` means no
OOLIB services at all.

#### Side effects

- `FT_OPEN` writes the whole area first: +0 the layout, +1 the clocks (`FC_*`), +2 the size, +4–7 and +8–11
  the counters, and 0 everywhere else. From then on OODOS's interrupt handlers write the counters there, at each
  tick and each frame, through the block the area's window had at the call: a later change of the program's
  windows doesn't move it. An area already open is closed first, its input with it. [Chapter
  8](08-game-services.md#the-event-area) has the layout.
- An area of 144 bytes (`EV_LEN2`) or more, 0 for 256, is **layout 2**: +18 its queue's slots, and the input as it is
  now (the modifiers, the devices, the mouse, the controllers and the paddles; no keys held, the queue empty).
  The USB card's event FIFO is emptied, its interrupt turned on, and from then on the input is kept there
  ([chapter 8](08-game-services.md#input)). Less is layout 1, and the USB card's interrupt stays off.
- A hook is called with the windows the program had at the `FT_FRAMEHOOK` or `FT_TICKHOOK` call. The rules it
  must keep are [chapter 8's](08-game-services.md#hooks).
- `FT_WAIT` and `FT_DELAY` wait in the BDOS with interrupts on, so the hooks run meanwhile.
- The warm boot takes every hook off, closes the event area, and sets timer 1 up again as the cold boot did
  (free-running, its latch, its interrupt enable) should a program have changed it. The counters run on, but a
  program that changed timer 1's latch may cost the millisecond counter 1 ms: writing the latch back clears timer
  1's flag, and with it a tick already due.

#### Cycles

Not measured. What the clocks cost every program, the call or not, is [chapter 8's](08-game-services.md#the-clocks).

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 80 (OODOS's range, 72 on). It is the first of OOLIB's service areas, one
function each from 80 ([hw/oolib.md](../../hw/oolib.md#the-plan)).

<a id="bdos-81"></a>
### 81 O_INPUT: input

#### Entry

- Y = 81 (`O_INPUT`).
- X = 0: A is the operation, and it takes no parameters.
- X not 0: A/X is the address of a parameter block in the program's memory, its +0 the operation and the
  operation's parameters after it, as [function 80's](#bdos-80). The operations are oodos.inc's `IN_` names:

  | A or +0 | Name | The block, after +0 | What it does |
  |---|---|---|---|
  | 0 | `IN_INFO` | 2 bytes to fill (optional) | says what is plugged in |
  | 1 | `IN_FLUSH` | none | empties the event queue |
  | 2 | `IN_MAP` | 4 bytes to fill | says which hardware each controller is |
  | 3 | `IN_SWAP` | +1 and +2 two controllers, 1–4 | swaps their hardware |
  | 4 | `IN_MOUSE` | +1 the mode (0 relative, 1 absolute), +2–3 X's limit, +4–5 Y's | sets the mouse's mode |
  | 5 | `IN_LEDS` | +1 bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock | sets the keyboard's lights |
  | 6 | `IN_REPEAT` | +1 the repeat: bits 0–3 the rate, 4–5 the delay | sets the console's key repeat |
  | 7 | `IN_NEXT` | 8 bytes to fill | takes the next event from the queue |

#### Exit

- `IN_INFO`: A = what is plugged in (the USB card's DEVICES: `ED_KEYBOARD` `$01`, `ED_MOUSE` `$02`, `ED_GAMEPAD`
  `$04`; 0 with no USB card), X = the event area layout L2 writes (2). With a block: +1 and +2 the same.
- `IN_MAP`: +1–4 controllers 1–4's hardware: `CH_STICK1` 1 (the VIA's port A), `CH_STICK2` 2 (port B),
  `CH_GAMEPORT` 3 (the USB card's game port), `CH_GAMEPAD` 4 (a USB gamepad: not yet). A = 0, X = 0.
- `IN_NEXT`: +1–8 the event ([chapter 8](08-game-services.md#the-queue)), taken: A = its kind (`EK_*`, 1–6), X = 0.
  A = 0, X = 0 if none waits, and the block untouched.
- The rest: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = X = `$FF`, and nothing done, for an operation past 7, one that needs a block given none, a block in memory that
isn't the program's (the I/O hole, flash), and:

- `IN_SWAP`: a controller that isn't 1–4.
- `IN_MOUSE`: a mode past 1. `IN_MOUSE`, `IN_LEDS`, `IN_REPEAT`: no USB card.
- `IN_NEXT`: no event area of layout 2 open.

`IN_FLUSH` with no layout-2 area open does nothing, A = 0. An OODOS before L2 answers 81 as a function it doesn't
have, A = X = `$FF`: [function 80's](#bdos-80) `FT_INFO` says whether there is input (`OA_INPUT`).

#### Side effects

- `IN_FLUSH` puts the area's tail at its head and its lost count to 0.
- `IN_SWAP` writes the two controllers in the area again at once; the next events from each hardware carry its new
  controller's number.
- `IN_MOUSE` writes the card's MCTRL and its limits, and starts the area's X and Y afresh: absolute mode's position
  (within the new limits), or relative mode's 0.
- `IN_NEXT` moves the area's tail on.
- `IN_LEDS` and `IN_REPEAT` write the card's LEDS and REPEAT. Raw events never repeat; the repeat is the cooked
  keys', the console's. The card has no repeat off: its slowest is `$3F`, 1 s and then two a second.
- The warm boot puts it all back as the cold boot set it: the controllers 1 to 4 in order, the mouse relative with
  limits 639 and 479, the LEDs off, the repeat `$10`; it turns the USB card's interrupt off and closes the area.

#### Cycles

Not measured. What input costs while an area of layout 2 is open, the call or not, is
[chapter 8's](08-game-services.md#what-input-costs).

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 81 (OODOS's range, 72 on). It is OOLIB's second service area
([hw/oolib.md](../../hw/oolib.md#the-plan)).

<a id="bdos-82"></a>
### 82 O_SOUND: sound

#### Entry

- Y = 82 (`O_SOUND`).
- X = 0: A is the operation, and it takes no parameters.
- X not 0: A/X is the address of a parameter block in the program's memory, its +0 the operation and the
  operation's parameters after it, as [function 80's](#bdos-80). The operations are oodos.inc's `SN_` names; the
  part in the last column must be the program's, claimed by `SN_CLAIM`:

  | A or +0 | Name | The block, after +0 | What it does | Part |
  |---|---|---|---|---|
  | 0 | `SN_INFO` | 3 bytes to fill (optional) | says the card's revision, codecs, and the parts free | |
  | 1 | `SN_CLAIM` | +1 the parts: `SC_6510` `$01`, `SC_STREAM` `$02`, `SC_DOC` `$04` | claims them, all or none | |
  | 2 | `SN_RELEASE` | +1 the parts | stops each that is held and gives it back | |
  | 3 | `SN_LOAD` | +1–3 a physical address, +4–5 the card's RAM address, +6–7 the length (0: 65,536) | loads the card's RAM by DMA | 6510 |
  | 4 | `SN_START` | +1–2 the entry, +3 CTRL's bits 0 (NTSC), 3 (SID B at `$D500`), 4 (BOTH) | starts the 6510 there | 6510 |
  | 5 | `SN_STOP` | none | stops the 6510, silences the SIDs | 6510 |
  | 6 | `SN_SEND` | +1 a byte | puts it in the 6510's mailbox | 6510 |
  | 7 | `SN_STREAM` | +1 the codec (0 raw PCM, 1 MP3), +2–3 the rate (Hz), +4 the format (bit 0 stereo, bit 1 16-bit) | starts a stream afresh | stream |
  | 8 | `SN_BUFFER` | +1–3 a physical address, +4–5 the length (0: 65,536) | gives the stream a buffer | stream |
  | 9 | `SN_PAUSE` | none | holds the stream | stream |
  | 10 | `SN_RESUME` | none | lets it go on | stream |
  | 11 | `SN_SEEK` | none | empties the FIFO, drops both buffers | stream |
  | 12 | `SN_END` | none | no more data: what is in plays out | stream |
  | 13 | `SN_STATUS` | 12 bytes to fill (optional) | says the stream's state | stream |
  | 14 | `SN_WAVE` | +1–3 a physical address, +4–6 the wave RAM address (17 bits: +6's bit 0 is A16), +7–8 the length (0: 65,536) | loads the ES5503's wave RAM by DMA | ES5503 |
  | 15 | `SN_DOCHOOK` | +1–2 the hook, 0 none; +3 a byte of its zero page (`$00–$EF`); no block: none | sets the ES5503 hook | ES5503 |
  | 16 | `SN_BEEP` | +1–2 the pitch, 20–20,000 Hz; +3–4 the length, ms | sounds the USB card's speaker | |
  | 17 | `SN_HOLD` | none | holds the 6510 where it is, each SID's volume nibble 0 | 6510 |
  | 18 | `SN_GO` | none | lets a held 6510 go on, the volumes back | 6510 |
  | 19 | `SN_VOLUME` | +1 VOLL, +2 VOLR (0–255, 255 unity) | sets the card's master volumes (revision 2) | |

  Operations 17–19 (`SN_HOLD`, `SN_GO`, `SN_VOLUME`) are since OODOS 0.6 (OOLIB's interface version 5, [function 80's](#bdos-80) `FT_INFO`); an OODOS before them
  refuses them, A = X = `$FF`.

#### Exit

- `SN_INFO`: A = the card's revision (0 with no sound card), X = its CODECS (`$83` on revision 2: raw PCM, MP3, the
  ES5503; 0 on revision 1). With a block: +1 and +2 the same, +3 the parts no one has claimed that the machine has.
- `SN_BUFFER`: A = the buffer that took it, 0 or 1, X = 0.
- `SN_STATUS`: A = the stream's state (the card's STATE: 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5
  ended, `$80` up an error), X = the buffers free (bit 0 buffer 0, bit 1 buffer 1). With a block, of one moment: +1
  and +2 the same, +3–4 the FIFO's room in 256-byte units, +5–8 the frames played, +9–10 an MP3's kbit/s, +11–12 the
  underruns seen since `SN_STREAM`.
- The rest: A = 0, X = 0. `SN_LOAD`, `SN_WAVE` and `SN_BEEP` return when they are done.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = X = `$FF`, and nothing done, for an operation past 19, one that needs a block given none, a block in memory that
isn't the program's (the I/O hole, flash), an operation whose part isn't claimed, and:

- `SN_CLAIM`: no parts asked for, a part the machine hasn't (the stream needs revision 2 and the DMA controller, the
  ES5503 revision 2 and CODECS bit 7), or a part held already: none is claimed then.
- `SN_LOAD`, `SN_WAVE`, `SN_BUFFER`: memory a DMA segment may not reach ([function 51](#bdos-51)): OODOS's own
  blocks, or past `$FFFFF`. `SN_LOAD`, `SN_WAVE`: the card's address and the length running past the end of its
  RAM (`$FFFF` the card's, `$1FFFF` wave RAM: nothing wraps), or a piece of the transfer that never ended (no DMA
  controller).
- `SN_DOCHOOK`: an address in the I/O hole or common memory, the program's W0 the system's, or a zero page byte past
  `$EF`. `SN_WAVE`: a wave RAM address's +6 past 1 (17 bits: `$20000` up isn't there). A refused `SN_DOCHOOK` leaves no hook set (the old one is taken off first), as `FT_FRAMEHOOK` and
  `FT_TICKHOOK` do.
- `SN_BEEP`: a pitch outside 20–20,000, no USB card, or no VIA (no millisecond counter).
- `SN_HOLD`: the 6510 not running (CTRL's RUN off): never started, stopped, or held already. A second hold so
  keeps what the first kept. `SN_GO`: the 6510 not held.
- `SN_VOLUME`: a card before revision 2, which has no master volume.

A = `$FE` (`SN_BUSY`), X = `$FF`, and nothing done, when what it needs is busy: `SN_LOAD` and `SN_WAVE` while a
stream buffer is still going to the card (DMA channel 2), `SN_BUFFER` while the buffer whose turn it is is still in
use, and `SN_SEND` while the 6510 hasn't taken the byte sent before (STATUS bit 1). Try again later. An OODOS before
L3 answers 82 as a function it doesn't have, A = X = `$FF`: [function 80's](#bdos-80) `FT_INFO` says whether there is
sound (`OA_SOUND`).

#### Side effects

- `SN_CLAIM` of the 6510 empties the mailbox from the card. The one to the card it can't: only a /RESET empties it,
  so a byte the last program left there, not taken, survives the warm boot and the claim; a new 6510 driver reads
  `$DE00` once as it starts, to drop it. Then with a layout-2 event area open, a byte the 6510 sends
  reaches the queue as an `EK_SOUND` event, its value the byte, and since OODOS 0.7 is stamped in the area's
  `EV_SMBOX`, `EV_SMMS` and `EV_SMCNT` ([chapter 8](08-game-services.md#the-6510)); since OODOS 0.8, with the
  area's `EV_SFLAGS` bit `ES_NOQUEUE` set, it is stamped but not queued. Of the
  stream: channel 2 stopped and both buffers free.
- `SN_START`, with a layout-2 area open, drops a byte the stopped run left in the mailbox from the card, and sets
  the area's `EV_SMMS` to the millisecond counter and `EV_SMCNT` to 0, before the 6510 runs (since OODOS 0.7).
- `SN_LOAD` and `SN_WAVE` move channel 2 4 KB a piece, each waited for, interrupts on between; the card holds the
  CPU 4,096 cycles a piece. `SN_LOAD` sets RAMADDR, past the last byte after; `SN_WAVE` sets DOCCTL's RAM and AUTOINC
  for it and puts DOCCTL back as it was after, leaving DOCADDR and DOCBANK past the last byte; it holds the ES5503's
  interrupt off meanwhile (IEN bit 3), and the hook waits for the load's end. Both leave DRQSEL 0.
- `SN_START` writes the card's `$FFFC–$FFFD` and CTRL (RUN and RESET with the bits given).
- `SN_HOLD` turns CTRL's RUN off and doesn't pulse RESET, so the 6510's registers and the card's RAM stay as they
  are. Then it reads SID A's and SID B's `$D418` (the card gives back what was last written, by the 6510 or the
  program), keeps both, and writes each back with its low nibble, the volume, 0 and its high nibble (bits 4–6 the filter's outputs, bit 7
  voice 3 off) as it was.
  VOLL and VOLR aren't touched, so a stream or the ES5503 playing alongside goes on as loud as before. `SN_GO`
  turns RUN off (if the program set it while held), writes the two kept bytes back, A's first (with CTRL's BOTH a write to A reaches B too), then sets RUN, again
  without RESET: the 6510 carries on from the instruction it was held at. `SN_STOP`, `SN_START`, `SN_RELEASE` of
  the 6510 and the warm boot forget a hold. While held, the SIDs are the program's to write, as when stopped.
- `SN_VOLUME` writes VOLL and VOLR. They scale every source on the card: the stream, the ES5503 and each SID's
  `$D418` volume nibble as the chip gets it (the register still reads back as written). It needs no claim.
- `SN_STREAM` writes PCMCTL (FLUSH, then RUN), CODEC, RATE, PCMFMT, POS (0) and DRQSEL (1), and the DMA controller's
  master enable on. `SN_BUFFER` takes the buffers in turn, 0, 1, 0, 1, from `SN_STREAM` or `SN_SEEK`; the first goes
  to the card at once, by channel 2 (demand mode, its terminal count's interrupt on), the next when it has all gone.
  `SN_SEEK` writes FLUSH with RUN and PAUSE as they were, and EOF off. `SN_PAUSE`, `SN_RESUME` and `SN_END` set and
  clear PCMCTL's PAUSE and EOF.
- With the stream claimed and a layout-2 area open, OODOS writes the area's +96–107 once a frame and at each
  buffer's end ([chapter 8](08-game-services.md#the-stream)).
- `SN_DOCHOOK` sets the card's IEN bit 3 while a hook is set, and holds it off while it changes the hook, so an
  interrupt meanwhile waits in the chip for the new one. With the ES5503's interrupt routed to the card's 6510
  (DOCCTL's IRQ_CARD), OODOS never reads `$E0` and the hook isn't called: the interrupt is the 6510's. The hook is called as the tick hook is, interrupts off
  ([chapter 8](08-game-services.md#the-es5503)), the interrupt register first written to its zero page byte, the
  port on the registers (DOCCTL's RAM and AUTOINC off, DOCADDR `$E0`); DOCCTL, DOCADDR and DOCBANK as the
  interrupt found them are put back after the hook returns.
- `SN_RELEASE` and the warm boot stop what they give back: the 6510 (CTRL's RUN off, every SID register 0), the
  stream (channel 2 aborted, PCMCTL FLUSH, DRQSEL 0), the ES5503 (DOCCTL's RUN off, the hook off). The warm boot does
  all of it whatever was claimed, and turns the card's IEN off; on revision 2 it sets VOLL and VOLR back to 255, and
  turns the EQ off with the preamp and every band at 0, as a /RESET leaves it (from OODOS 0.7; before it, the EQ was
  left as the program set it).

#### Cycles

`SN_BEEP` takes its length (test_oodos_sound: 1,000 Hz for 100 ms toggles the speaker 193 times, the call 106 ms
with its line printed). What sound costs between calls is [chapter 8's](08-game-services.md#what-sound-costs). The
rest: not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 82 (OODOS's range, 72 on). It is OOLIB's third service area
([hw/oolib.md](../../hw/oolib.md#the-plan)).

<a id="bdos-83"></a>
### 83 O_DMA: DMA and loading

#### Entry

- Y = 83 (`O_DMA`).
- X = 0: A is the operation, and it takes no parameters.
- X not 0: A/X is the address of a parameter block in the program's memory, its +0 the operation and the
  operation's parameters after it, as [function 80's](#bdos-80). The operations are oodos.inc's `DM_` names.
  Addresses are physical, 3 bytes ([function 73](#bdos-73) makes one of a CPU address):

  | A or +0 | Name | The block, after +0 | What it does |
  |---|---|---|---|
  | 0 | `DM_INFO` | 12 bytes to fill (optional) | says the units, the slots and the W1 map |
  | 1 | `DM_LOAD` | +1–2 an open FCB, +3–5 its first record, +6–7 a count of records (1–512), +8–10 the destination | loads the records into physical memory by DMA |
  | 2 | `DM_COPY` | +1–3 the source, +4–6 the destination, +7–8 the length (0: 65,536) | copies by DMA |
  | 3 | `DM_FILL` | +1 the byte, +2–4 the destination, +5–6 the length (0: 65,536) | fills by DMA |
  | 4 | `DM_VBSET` | +1 a slot (0–3), +2–4 the source, +5–7 the destination, +8–9 the length (1–1,024; 0 frees the slot) | sets a vertical-blank copy |
  | 5 | `DM_POINTER` | +1 a sprite (0–63; `$FF` off), +2 and +3 the hotspot's X and Y | moves the sprite with the mouse |
  | 6 | `DM_CONSOLE` | none | shows the console's screen again |

#### Exit

- `DM_INFO`: A = the first video memory unit a program may use (13: `DM_UNIT0`), X = the vertical-blank slots (4:
  `DM_SLOTS`). With a block: +1 and +2 the same, +3 the slots' budget in 256-byte pages (4: `DM_BUDGET`, 1,024
  bytes), +4 the pointer's sprite (`$FF` off), +5–12 the **W1 map**: bit n & 7 of byte n / 8 set for each block n
  that OOLIB may put in W1 now (block 1, the blocks the program has allocated by functions 53–56, and video memory's
  blocks 33–47; [chapter 5](05-memory-and-banking.md#the-w1-contract)).
- `DM_LOAD`: A = 0, X = 0, every record loaded; A = 1 if the file ended first (a record past the extent's count, a
  block never written, or an extent never written), its records before that loaded. The block's +6–7 get the records
  loaded, either way.
- The rest: A = 0, X = 0. `DM_LOAD`, `DM_COPY` and `DM_FILL` return when they are done.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

A = X = `$FF`, and nothing done, for an operation past 6, one that needs a block given none, a block in memory that
isn't the program's (the I/O hole, flash), and a destination that isn't memory a DMA segment may reach
([function 51](#bdos-51): OODOS's own blocks, or past `$FFFFF`) or that touches the console's video memory, units
0–12 (`$80000–$833FF`). A source may be anything a segment may reach, the console's font (units 1–4) included. And:

- `DM_LOAD`: a count of 0 or past 512. Function 33's errors otherwise, as its error mode says ([function
  45](#bdos-45)): 3 (an extent that can't be closed) and 6 (past 32 MB) returned in A, a physical error said. An FCB
  that was never opened has no blocks: A = 1, no records.
- `DM_VBSET`: no video card or no DMA controller; a slot past 3; a length past 1,024, or that takes the four slots'
  past 1,024.
- `DM_POINTER`: a sprite past 63; no layout-2 event area open ([chapter 8](08-game-services.md#the-mouse)); no video
  card or no DMA controller. `$FF` (off) is always taken.

An OODOS before L4 answers 83 as a function it doesn't have, A = X = `$FF`: [function 80's](#bdos-80) `FT_INFO` says
whether there is DMA and loading (`OA_DMA`, interface version 4).

#### Side effects

- `DM_LOAD` writes the drive's changed buffers back first, so it loads what was written, closed or not. On an SD
  drive of OODOS's own formats and on M:, the whole sectors of a CP/M block that the count wants go straight to the
  destination: one READ of up to 8 sectors (4 KB) on DMA channel 0, the CPU held a cycle a byte, interrupts taken
  between (M: by channel 3). A first or last record that doesn't fill its sector goes through a BDOS buffer, as
  function 33 moves a record with a DMA segment. An apple-do drive (O:, P:, an apple-do image on the card) goes
  through its track buffer whole. The FCB is left as a random read of the last record loaded leaves it (R0–R2 and CR
  that record's), or of the first if none was; the DMA segment and address aren't changed. No vertical-blank wait:
  load into a buffer that isn't shown and flip ([chapter 8](08-game-services.md#loading)).
- `DM_COPY` and `DM_FILL` go by channel 3, 2 KB a block, interrupts taken between. A copy to a lower address is
  exact over itself; to a higher one it runs forward, a byte at a time (so a copy one byte up repeats the first). A
  fill writes the byte to the first and copies it forward over the rest.
- `DM_VBSET`'s slot is copied by channel 1 at each vertical blank after the program sets its bit in the event
  area's +12 (`EV_VBGO`, bit n slot n), and the bit cleared then; nothing is copied with no event area open. The
  slots run in order, then the pointer ([chapter 8](08-game-services.md#the-vertical-blank-slots)).
- `DM_POINTER` sets bytes 2–5 (X and Y) of the sprite's entry in the table at SPRBASE at each vertical blank, from the
  area's mouse X and Y less the hotspot, modulo 1,024; not while SPRBASE is a console unit (below 13). The program
  draws the image, sets the entry's other bytes, and puts the mouse in absolute mode with limits 319 and 239
  ([function 81's](#bdos-81) `IN_MOUSE`): the sprites' space is 320 × 240.
- `DM_CONSOLE` sets the video card's registers as the cold boot set them for the console (CTRL, BORDER, PALBASE,
  SPRBASE, the layers): its text, font and palette are where they were, so what was printed meanwhile shows.
- The warm boot frees the slots, turns the pointer off, and shows the console's screen as `DM_CONSOLE` does.

#### Cycles

`DM_LOAD` of 64 KB from an `oodos-8m` drive: 114,442 cycles (16.0 ms), against 620,419 (86.7 ms) by functions 51 and
33 with a multi-sector count of 128 (test_oodos_load). `DM_COPY` and `DM_FILL`: 2 cycles a byte, and the call. What
the slots and the pointer cost a frame is [chapter 8's](08-game-services.md#what-graphics-costs).

#### Since

OODOS 0.5

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 83 (OODOS's range, 72 on). It is OOLIB's fourth service area
([hw/oolib.md](../../hw/oolib.md#the-plan)).

<a id="bdos-98"></a>
### 98 DRV_FREE: free blocks not closed

#### Entry

- Y = 98 (`DRV_FREE`).

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- On every logged-in drive, the blocks taken by files written and not closed are freed: the allocation vector goes
  back to what the directory holds.
- A buffer kept after a failed write (see [function 48](#bdos-48)) is dropped if its block is one 98 frees, since
  the file it belonged to was never closed. One in a block the directory has is kept.

The CCP calls 98 at every warm boot, after 48. A program with a file open for writing doesn't call it: the file's
new blocks would be freed under it.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same. Dropping the failed buffers of the blocks it frees is ours.

<a id="bdos-99"></a>
### 99 F_TRUNCATE: truncate a file

#### Entry

- Y = 99 (`F_TRUNCATE`).
- A/X = the address of a 36-byte FCB: the drive, the name and type (no `?`), and R0–R2 the record that is to be
  the file's last. The BDOS reads bytes 0–35; it doesn't write them back.
- On a drive with passwords on, the DMA buffer's first 8 bytes are a protected file's password.

#### Exit

- A = 0, X = 0: done. A = `$FF`, X = 0: no such file, or R0–R2 isn't a record the file has (below).
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

R0–R2 must be before the file's end and where it has data: in an extent the file has, below RC if that extent is
the last, in a block it has. R2 over 3 is `$FF` before the directory is read.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), in this order:
4 Invalid Drive; 9 ? in Filename; 2 Read Only; 3 File R/O, an extent has t1'; then `$FF` for a record the file hasn't;
then 7 Password Error. 1 Disk I/O. An FCB in the I/O hole returns A = X = `$FF`.

#### Side effects

- The extents after R0–R2's are deleted; the one with it ends there (RC is R mod 128, plus 1), and its pointers
  after R's block are cleared, and its archive attribute (t3'). Each entry is written before its blocks are freed.
- The update is stamped in the file's extent-zero SFCB slot, if the label asks for update stamps.

An FCB open on the file is out of date after: close it without writing, or open it again.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, but for the registers. Stamping the update, clearing t3', and the order of the errors, are ours.

<a id="bdos-100"></a>
### 100 DRV_SETLABEL: make or change a directory label

#### Entry

- Y = 100 (`DRV_SETLABEL`).
- A/X = an FCB's address:
  - byte 0 the drive (0 the current one, 1–16 A:–P:);
  - bytes 1–11 the label's name and type, taken as they are;
  - byte 12 the label's mode: bit 7 passwords on, bit 6 access stamps, bit 5 update stamps, bit 4 creation stamps;
    bit 0 set to give the label a new password.
- The DMA buffer (function 26's address; not the segment): bytes 0–7 the label's present password, if it has one;
  bytes 8–15 the new password, if byte 12's bit 0 is set.

#### Exit

- A = 0, X = 0: the label is made or changed.
- A = `$FF`, X = 0: stamps asked for (bits 6–4) on a directory without SFCBs, or no free entry for a new label.
  Nothing is done.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), in this order:

- 4 Invalid Drive: the drive isn't there; 1 Disk I/O, logging it in.
- 2 Read Only: the drive is read-only.
- 7 Password Error: the label has a password, and the DMA buffer's bytes 0–7 and the default password (106) both
  differ from it. Bit 7 doesn't matter here: a label's own password is always checked.

#### Side effects

- The label is the drive's first label entry, or, without one, a new entry in the first free one. Its mode byte is
  byte 12's bits 7–4 with bit 0 set (ours: bit 0 says a label is there).
- Without bit 0, an old label keeps its password; a new one has none.
- The label's update stamp is set, and its creation stamp when it is made, whatever the mode asks (ours).
- The drive's stamps and password checks follow the new mode from this call on
  ([chapter 3](03-file-system.md#directory-labels)).
- A free entry used moves the drive's high-water mark, so later searches look that far.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, with the FCB at A/X. Where CP/M 3 is silent the choices are ours: bit 0 set in the mode byte, the label
stamped whatever the mode, and the `$FF` for stamps on a directory without SFCBs.

<a id="bdos-101"></a>
### 101 DRV_GETLABEL: a drive's label mode

#### Entry

- Y = 101 (`DRV_GETLABEL`).
- A = the drive, 0–15 (A:–P:).

#### Exit

- A = the label's mode byte (byte 12) as the disk has it, 0 without a label: bit 7 passwords on, 6 access stamps,
  5 update stamps, 4 creation stamps, 0 a label there. X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 4 Invalid Drive, the drive is over
15 or isn't there; 1 Disk I/O, logging it in.

#### Side effects

The drive is logged in if it wasn't. The current drive doesn't change.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same. An OODOS drive's label is usually `$31`: creation and update stamps, no passwords
([chapter 3](03-file-system.md#date-stamps)).

<a id="bdos-102"></a>
### 102 F_TIMEDATE: a file's stamps and password mode

#### Entry

- Y = 102 (`F_TIMEDATE`).
- A/X = an FCB's address: the drive and the file's name, no `?`.

#### Exit

- The FCB, written back (33 bytes): byte 12 the file's password mode (its XFCB's: bit 7 read, 6 write, 5 delete),
  0 without an XFCB; bytes 24–27 the creation or access stamp and 28–31 the update stamp, from the SFCB slot of the
  file's extent-zero entry, zeros without one.
- A = 0, X = 0: the file is there.
- A = `$FF`, X = 0: no such file in the current user. The FCB is as it was.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 4 Invalid Drive; 1 Disk I/O;
9 ? in Filename.

#### Side effects

The drive is logged in if it wasn't. Only the current user's file is looked at: a SYS file of user 0's, which Open
finds from another user, isn't. The XFCB's mode is returned whether or not the label has passwords on; a password
is never needed.

Each stamp is 4 bytes: the day (a word, 1 January 1978 day 1), then the hour and the minute in BCD.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, with the FCB at A/X. CP/M 3's description of this function puts the delete mode at bit 4, and its
function 103's at bit 5; OODOS uses bit 5 for both, as the XFCB holds it.

<a id="bdos-103"></a>
### 103 F_WRITEXFCB: set a file's password and mode

#### Entry

- Y = 103 (`F_WRITEXFCB`).
- A/X = an FCB's address: the drive and the file's name, no `?`; byte 12 the mode: bit 7
  a password to read, bit 6 to write, bit 5 to delete; bit 0 set to give a new password.
- The DMA buffer (function 26's address): bytes 0–7 the file's present password, if it is protected; bytes 8–15 the
  new password, if byte 12's bit 0 is set.

#### Exit

- A = 0, X = 0: the XFCB is made or changed.
- A = `$FF`, X = 0: the drive has no label with passwords on, there is no such file in the current user, or there is
  no free entry for a new XFCB. Nothing is done.
- A = 10, X = 0: on O: or P:, another disk was found in the drive as the directory was to be written; nothing is
  done, whatever the error mode ([removable drives](03-file-system.md#removable-drives-o-and-p)). A program that
  tests only for `$FF` takes this for success: test for 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), in this order: 4 Invalid Drive
(1 Disk I/O, logging it in), 9 ? in Filename, 2 Read Only, then the `$FF`s for passwords off and no file, then
7 Password Error: the file is protected (its mode has a bit of 7–5) and neither the DMA buffer's bytes 0–7 nor the
default password (106) is its password. Then the `$FF` for no room.

#### Side effects

- The XFCB is a directory entry of user + 16 with the file's name: its mode, and its password kept as CP/M 3 keeps
  one ([chapter 3](03-file-system.md#passwords)).
- Without bit 0, a new XFCB has no password and an old one keeps its own (ours).
- The mode is copied into the SFCB slot of the file's extent-zero entry too, as CP/M 3 keeps it.

#### Cycles

Not measured.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

The same, with the FCB at A/X. Ours where CP/M 3 is silent: a new XFCB without bit 0 has no password, and the order
of the errors.

<a id="bdos-104"></a>
### 104 T_SET: set the date and time

#### Entry

- Y = 104 (`T_SET`).
- A/X = the address of a 4-byte date and time (a DAT):
  - +0-1 the day, counted from 1 January 1978, which is day 1;
  - +2 the hour, BCD;
  - +3 the minute, BCD.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A date before 2000 or past 2155 doesn't set the clock, and nothing says so: the SCB has it until the BDOS next
  reads the clock (a stamp, [function 105](#bdos-105)). The hour and the minute aren't checked.
- An address in the I/O hole: A = X = `$FF`.

#### Side effects

- The SCB's date, hour and minute are set from the DAT and its seconds to 0, then the clock (the DS3231, through
  the north bridge) from the SCB, the day of the week worked out.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same DAT, but for the registers: its address goes in A and X, where CP/M 3 takes it in DE. The clock holds
2000-2155.

<a id="bdos-105"></a>
### 105 T_GET: get the date and time

#### Entry

- Y = 105 (`T_GET`).
- A/X = the address of a 4-byte DAT to fill, laid out as [function 104's](#bdos-104).

#### Exit

- The DAT: the day (1 January 1978 is day 1), the hour and the minute, BCD.
- A = the seconds, BCD; X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A clock past 2155, which reads as no time: A = X = `$FF`, the DAT left as it was.
- An address in the I/O hole: A = X = `$FF`.

#### Side effects

- The clock is read into the SCB's date and time.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the DAT's address goes in A and X, where CP/M 3 takes it in DE, and the seconds
come back in A with X 0. The `$FF` for a clock past 2155 is ours.

<a id="bdos-106"></a>
### 106 F_PASSWD: set the default password

#### Entry

- Y = 106 (`F_PASSWD`).
- A/X = the address of 8 bytes, the password.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

- A = X = `$FF`: the 8 bytes are in the I/O hole. The default password is as it was.

#### Side effects

The default password is tried after the DMA buffer's, by every function that checks one. It is none from the cold
boot, and the warm boot keeps it: it lasts until the next call of 106. A password of 8 blanks or 8 zero bytes is
none.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, with the address in A/X.

<a id="bdos-107"></a>
### 107 S_SERIAL: get the serial number

#### Entry

- Y = 107 (`S_SERIAL`).
- A/X = the address of 6 bytes to fill.

#### Exit

- The 6 bytes: `$4F $4F $44 $00 $00 $01`, "OOD" (the program header's magic) and serial 1.
- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

An address in the I/O hole: A = X = `$FF`.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers. The number is the same in every copy of OODOS (ours).

<a id="bdos-108"></a>
### 108 P_CODE: get or set the program return code

#### Entry

- Y = 108 (`P_CODE`).
- A/X = `$FFFF`: get the code. Any other value: set the code to it.

#### Exit

- A get: A/X = the code.
- A set: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- The code is the SCB's word at `$10`. The CCP sets it to 0 as it starts a program that wasn't chained to
  (function 47); the warm boot keeps it for the next command, where a `:` conditional reads it.
- The BDOS sets `$FFFD` when it ends a program for an error (the default error mode), and `$FFFE` when it ends one
  for a ^C.
- The CCP's conditional commands (a command that starts with `:`) read it: `$FF00`-`$FFFF` is a failure, anything
  else a success.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the code goes in and comes back in A and X, where CP/M 3 uses DE and HL.

<a id="bdos-109"></a>
### 109 C_MODE: get or set the console mode

#### Entry

- Y = 109 (`C_MODE`).
- A/X = `$FFFF`: get the mode. Any other value: set the mode to it.

#### Exit

- A get: A/X = the mode.
- A set: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

The mode is the SCB's word at `$33`. Its bits:

| Bit | Set |
|---|---|
| 0 | function 11 reports only ^C |
| 1 | no ^S, ^Q or ^P handling in functions 1, 2, 9, 11 and 111 |
| 2 | raw output for 2, 9 and 111: no tab expansion, no printer echo |
| 3 | ^C doesn't end the program |

The other bits (8 and 9 among them, for a redirecting RSX) are kept and not looked at. The CCP sets the mode to 0
at each warm boot. [Chapter 4](04-console-devices-scb.md#console-modes) has what each bit is for.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same bits, but for the registers: the mode goes in and comes back in A and X, where CP/M 3 uses DE and HL.

<a id="bdos-110"></a>
### 110 C_DELIMIT: get or set the output delimiter

#### Entry

- Y = 110 (`C_DELIMIT`).
- A/X = `$FFFF`: get the delimiter. Any other value: A is the new delimiter (X isn't looked at).

#### Exit

- A get: A = the delimiter, X = 0.
- A set: A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

None.

#### Side effects

- The delimiter is the SCB's byte at `$37`: where [function 9's](#bdos-9) string ends. The CCP sets it back to `$`
  at each warm boot.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the delimiter goes in A, where CP/M 3 takes it in E, and the get is A/X =
`$FFFF`, where CP/M 3's is DE.

<a id="bdos-111"></a>
### 111 C_WRITEBLK: write a block of characters to the console

#### Entry

- Y = 111 (`C_WRITEBLK`).
- A/X = the address of a 4-byte character control block (CCB): +0-1 the characters' address, +2-3 how many.

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

An address in the I/O hole, the CCB's or a character's: the call stops there, A = X = `$FF`.

#### Side effects

- Each character goes to the console as [function 2](#bdos-2) sends it: the look at the keyboard before it, tab
  expansion and printer echo unless the output is raw, the SCB's column. No character ends the block; a count of 0
  writes nothing.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the CCB's address goes in A and X, where CP/M 3 takes it in DE.

<a id="bdos-112"></a>
### 112 L_WRITEBLK: write a block of characters to LST:

#### Entry

- Y = 112 (`L_WRITEBLK`).
- A/X = the address of a 4-byte CCB, as [function 111's](#bdos-111).

#### Exit

- A = 0, X = 0.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

An address in the I/O hole, the CCB's or a character's: the call stops there, A = X = `$FF`.

#### Side effects

- Each character goes to LST: as it is, as [function 5](#bdos-5) sends it: no tab expansion, no look at the
  keyboard, and the console column doesn't move.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the CCB's address goes in A and X, where CP/M 3 takes it in DE.

<a id="bdos-152"></a>
### 152 F_PARSE: parse a file name into an FCB

#### Entry

- Y = 152 (`F_PARSE`).
- A/X = the address of a 4-byte parse block: +0–1 the string's address, +2–3 the FCB's address.
- The string is `{d:}filename{.typ}{;password}`, after any blanks and tabs, and read to 128 bytes at most.

#### Exit

- A/X = 0: nothing but blanks and tabs, then a NUL or CR, follows the name.
- A/X = the address of the delimiter that follows the name (and any blanks after it), so a program can parse the
  next name from there.
- A/X = the address of the first blank after the name, when what follows the blanks isn't a delimiter.
- A/X = `$FFFF`: the string isn't a file name (Errors). The FCB is as it was.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

On success the FCB's bytes 0–23 are written:

- Byte 0: the drive, 1–16 for `A:`–`P:` (either case), 0 for none.
- Bytes 1–11: the name and type in capitals, padded with blanks. A `*` fills the rest of its field with `?`.
- Bytes 12–15: 0.
- Bytes 16–23: the password in capitals, padded with blanks (blanks if none). A `*` is a character of a password.

Bytes 24–35 aren't touched. The delimiters are a blank, tab, CR, NUL, `;`, `=`, `<`, `>`, `.`, `:`, `,`, `|`,
`[` and `]`; `.`, `:` and `;` only where they don't separate the type, the drive and the password. A string of
blanks returns 0 with the FCB blank and drive 0. An empty name (`=X`, `.TXT`) isn't an error: the fields are
blank.

#### Errors

A/X = `$FFFF`, and the FCB isn't changed, for: a name over 8 characters, a type over 3, a password over 8; a
character after a `*` in its field; a control character that isn't a delimiter, DEL or a byte over `$7F`; a colon
second after a character that isn't a drive letter `A`–`P`. These come back whatever the error mode: there is no
disk error. A parse block in the I/O hole returns the same.

#### Side effects

None.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the parse block's address goes in A and X, where CP/M 3 takes DE, and the result
comes back in A and X, where CP/M 3 returns HL. Ours where CP/M 3 is silent: bytes 24–35 are left alone, and a
failed parse leaves the FCB unchanged. As in CP/M 3, a user number isn't parsed: `5A:X` is the name `5A`.
