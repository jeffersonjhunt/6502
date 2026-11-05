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
  (as function 98): what a program wrote to a file it didn't close is lost.
- The CCP sets the multi-sector count to 1, the console mode to 0 and the delimiter to `$`, and makes its own drive
  and user current again. The return code (function 108) stays as the program left it.
- A command left by function 47 runs next.

#### Cycles

Not measured.

#### Since

OODOS 0.2

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
- Every changed buffer is written back, then every buffer dropped.
- Every drive is logged out and made read-write, including those made read-only by [function 28](#bdos-28).
- A: becomes the current drive (the SCB's `$3E`). The user number stays.
- The DMA address is set to `$0280` (the SCB's `$3C`). The DMA segment ([function 51](#bdos-51)) is kept.
- A search that [function 18](#bdos-18) would go on with is ended.

The CCP runs function 13 when ^C is typed on an empty prompt line, and only then; an ordinary warm boot doesn't.

#### Cycles

Not measured.

#### Since

OODOS 0.2

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
while an image is mounted on it) and M: (the RAM disk). 1 Disk I/O if the directory can't be read. Each as the
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

OODOS 0.2

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
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

The FCB gets the extent's block pointers and RC (128 if the file goes past this extent, 0 if it doesn't reach it),
and the attributes f1'–f4' and t1'–t3' from the directory. f7' and f8' are the BDOS's: f8' is set when the file
was opened from user 0 (below), f7' when it is write-protected and its password wasn't given. With CR `$FF` on
entry, CR comes back as the byte count of the file's last record (S1, as [function 30](#bdos-30) sets it), and the
program sets CR to 0 itself before it reads. Otherwise CR is left as it was.

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

#### Since

OODOS 0.2

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

- A = 0, X = 0: done. A = `$FF`, X = 0: the extent couldn't be put in the directory (below).
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

OODOS 0.2

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

OODOS 0.2

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

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 1 Disk I/O; 2 Read Only,
when the read moves on from an extent that was written, on a drive that is read-only, and so can't close it;
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

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment. With a multi-sector count, the whole buffer is checked before
the first record moves, and a call whose buffer isn't all memory reads nothing (ours). OODOS never returns CP/M 3's
code 9 (an FCB a failed close made invalid) or 10 (the disk changed since the open): a swapped card image is
handled when the units are read again ([function 13](#bdos-13)).

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

As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)), before anything is written:
4 Invalid Drive; 2 Read Only, the drive is read-only; 3 File R/O, the file is read-only (t1'), was opened from
user 0, or is write-protected and was opened without its password. Then 1 Disk I/O. The DMA buffer is
checked after those, as [function 20](#bdos-20) checks it (a read-only window may be written from).

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
  extent 0, which stamped it, no write of that FCB does.

#### Cycles

Measured by `test_oodos_sys`'s FSTEST on an `oodos-8m` drive, BDOSENT to its return: 7,818 cycles to a sector in a
buffer, 78,451 for a record that takes a new block, 161,986 for the cheapest that must write buffers back first; with
a count of 128 on M:, 786,294 cycles, 6,142 a record
([hw/oodos.md](../../hw/oodos.md#the-archive-bit-and-the-update-stamp-item-10), measured again 2026-11-04). The
first write after an open writes the stamp's SFCB, and a write to an archived extent its entry, besides. The BDOS
stub adds 220.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment, and choices of ours:

- a new block is the lowest free one, and is zeroed, where CP/M 3 leaves the records of it not yet written as they
  happen to be;
- a multi-sector buffer is checked whole before the first record;
- a written extent it can't close as it moves on is A = 1, as no directory space (CP/M 3 lists no 3 for 21);
- codes 9 and 10 aren't returned, as for [function 20](#bdos-20).

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

OODOS 0.2

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

OODOS 0.2

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

OODOS 0.2

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

The DPB is 17 bytes, CP/M 3's layout: SPT (a word, 64), BSH, BLM, EXM, DSM (a word), DRM (a word), AL0, AL1, CKS
(a word, `$8000`: fixed media), OFF (a word, 0), PSH (2) and PHM (3): 512-byte sectors.

| Drive | BSH | Block | DSM | DRM | Size |
|---|---|---|---|---|---|
| `oodos-8m` | 5 | 4 KB | 2,047 | 1,023 | 8 MB |
| `oodos-32m` | 6 | 8 KB | 4,095 | 2,047 | 32 MB |
| M: | 4 | 2 KB | 63 | 255 | 128 KB |

#### Errors

4 Invalid Drive; 1 Disk I/O. As the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).

#### Side effects

The current drive is logged in if it isn't.

#### Cycles

Not measured.

#### Since

OODOS 0.2

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

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment, and the whole buffer checked first, as 20. Code 10 isn't
returned, as for [function 20](#bdos-20).

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

As [function 21](#bdos-21): 4 Invalid Drive, 2 Read Only, 3 File R/O before anything is written, then 1 Disk I/O;
as the error mode says ([function 45](#bdos-45), [appendix A](appendix-a-errors.md)).

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

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers and the DMA segment; a new block is zeroed and the lowest free one, and code 10
isn't returned, all as for [function 21](#bdos-21). Its update stamp and archive attribute are CP/M 3's, as 21's.

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
  read-write. Other drives, the current drive and the DMA address are as they were.
- A search that [function 18](#bdos-18) would go on with is ended.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the vector goes in A and X, where CP/M 3 takes DE. The units are read again
first, as for 13 (ours).

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

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3 zeroes a new block only for 40. OODOS zeroes every block a write takes, so 40 is 34.

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

OODOS 0.2

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
([function 45](#bdos-45), [appendix A](appendix-a-errors.md)): 1 Disk I/O, or 2 Read Only for a unit found
read-only (that buffer is dropped, and the drive made read-only).

#### Side effects

- A drive whose unit has changed since its login has its buffers dropped unwritten first.
- Every changed buffer is written back. One that fails is kept, marked, and tried again by the next 48 or close;
  [function 13](#bdos-13) or 37 drops it. With A = `$FF` all are dropped all the same.

The CCP calls 48, then [98](#bdos-98), at every warm boot, and says a failure there as the BDOS says its errors.

#### Cycles

Not measured.

#### Since

OODOS 0.2

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
33, 34 and 40, and when the record moves for 17 and 18.

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

OODOS 0.2

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

OODOS 0.2

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
  | +0, +1 | OODOS's version: major, minor (0, 2) |
  | +2, +3 | the BIOS's version: major, minor (0, 2) |
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

OODOS 0.2

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
- `$12`: the image isn't 8 MB or 32 MB (16,384 or 65,536 sectors).
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

OODOS 0.2

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

- A = 0, and A = 1 with a IIe ROM in the cartridge: it doesn't return.
- A = 1 with no IIe ROM in the cartridge (`$FBB3` isn't `$06`), or any other A: A = X = `$FF`, and OODOS goes on.
- Y is not kept. The flags come back as they were before the call, the decimal flag clear.

#### Errors

As in Exit.

#### Side effects

- For A = 0 or 1, the changed buffers are written back first, as function 48 does, even when there is no IIe ROM
  to go to. A file left open isn't closed: a program closes its files first.
- OODOS doesn't come back: the ROM's monitor, or a reset, is what follows.

#### Cycles

Not measured.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no function 75. A program can't leave by itself, as W3 is OODOS's and a program may only call
the BDOS. The CCP's WOZMON and APPLE2 use it.

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

OODOS 0.2

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

OODOS 0.2

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

OODOS 0.2

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
