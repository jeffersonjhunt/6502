# 4 Console, devices and the SCB

The console and the other character devices, the System Control Block, direct BIOS calls, and the date and time.

The design record, for the reasons: [hw/oodos.md, "BIOS"](../../hw/oodos.md#bios), ["Real-time
clock"](../../hw/oodos.md#real-time-clock).

## Console modes

The console is CON:, the screen and keyboard and the serial line together by default. A program reads and writes
it through the BDOS's console functions:

| Function | What |
|---|---|
| [1](07-function-reference.md#bdos-1) | read a key, echoed |
| [2](07-function-reference.md#bdos-2) | write a character |
| [6](07-function-reference.md#bdos-6) | read or write with none of the BDOS's handling |
| [9](07-function-reference.md#bdos-9) | write a string, to the delimiter |
| [10](07-function-reference.md#bdos-10) | read an edited line |
| [11](07-function-reference.md#bdos-11) | is a key waiting? |
| [111](07-function-reference.md#bdos-111) | write a block of characters |

Between them the BDOS handles a few keys for the program ([appendix E](appendix-e-control-characters.md)): ^S stops
the output until ^Q, ^P turns printer echo on and off while the output is stopped or in a line function 10 reads
(anywhere else it is dropped), and ^C ends the program at a line's start or while the output is stopped. Before
each character 2, 9 and 111 write, the BDOS looks at the keyboard, unless a key is kept already; a key that isn't
one of these is kept, one, for the next read. While one is kept the BDOS doesn't look again, so a ^S typed after it
isn't seen until a read takes the kept key. The warm boot doesn't drop it, so a key typed during a program's output
reaches the next program or the prompt, typed ahead.

**The console mode** (function [109](07-function-reference.md#bdos-109), the SCB's word at `$33`) changes that. Its
bits:

- **Bit 0, ^C only:** function 11 reports a ^C and nothing else. It reads the waiting keys, drops the others and
  keeps the ^C for the next read. A program that writes for a long time and wants ^C to stop it (DIR and TYPE do)
  sets bit 0 and calls 11 now and then.
- **Bit 1, no ^S, ^Q or ^P:** functions 1, 2, 9, 11 and 111 don't look for them. 1 returns them as keys; 11 is the
  BIOS's status and reads nothing; the output functions don't look at the keyboard at all. For a program that
  takes every key itself, an editor or a game.
- **Bit 2, raw output:** 2, 9 and 111 don't expand tabs and don't echo to the printer.
- **Bit 3, no ^C:** a ^C doesn't end the program. At a line's start (function 10) it is a character of the line;
  after a ^S it rings the bell, as any key but ^Q and ^P does there.

The other bits are kept and not looked at (8 and 9 are for a redirecting RSX, phase 5). The CCP sets the mode to 0
at each warm boot, so a program starts with all of the handling and needn't put the mode back.

Function 6 is the way past all of it: `$FF`, `$FE` and `$FD` read with no handling (taking a kept key first), and a
character written by 6 goes out as it is, its column not counted.

**The column.** The SCB keeps the console's column (`$1B`): each character from `$20` up (DEL and `$80`-`$FF`
too) that 1, 2, 9, 10 and 111 send counts one, BS takes it back one, CR sets it to 0, and the other control
characters leave it. A tab goes to the next column that is a multiple of 8. Function 10 lays its
line out by it and by the width (`$1A`, the width less one: 79 for 80 columns).

## The output delimiter

[Function 9](07-function-reference.md#bdos-9) writes a string to its delimiter, `$` unless function
[110](07-function-reference.md#bdos-110) has changed it. A program that has to print a `$` sets another delimiter
(a 0, say) or writes with function 111, whose block has a length and no delimiter. The CCP sets the delimiter back
to `$` at each warm boot.

## Character devices and DEVTBL

The BIOS has three character devices, in its device table (DEVTBL):

| Device | What | Input | Output |
|---|---|---|---|
| 0 CRT | the video card's 80 × 30 text screen and the USB keyboard | yes | yes |
| 1 TTY | the serial card, 19,200 baud | yes | yes |
| 2 LPT | the printer port: there is none yet, so it takes characters and drops them | no | yes |

The logical devices are the SCB's five vectors, a word each, one bit a device: bit 15 CRT, bit 14 TTY, bit 13 LPT.

| SCB | Vector | Cold boot |
|---|---|---|
| `$22` | CON: input | CRT and TTY |
| `$24` | CON: output | CRT and TTY |
| `$26` | AUX: input | TTY |
| `$28` | AUX: output | TTY |
| `$2A` | LST: output | LPT |

Output goes to every device of the vector. Input comes from the first of its devices that has a character, so
CON: reads the keyboard and the serial line both; a vector with no device that has input (none, or LPT alone)
reads ^Z at once. TTY is in CON: and AUX: alike: function 3 and the console read the same line, and a character goes
to whichever asks first. Status is "a character waits" for input from any of the devices, and "ready" for output
when all of them are; every device is always ready for output today.

The user moves the devices with DEVICE ([User's Guide](../oodos-users-guide/05-utilities.md)). A program can set
the vectors with function 49, and read DEVTBL by function 50: the BIOS's DEVTBL returns the table's address in the
system's memory, where the program copies it from with MOVE (XMOVE from bank 0 to bank 1, below). Each entry is 8
bytes, CP/M 3's: the name (6 characters, blank-padded), the mode (bit 0 input, bit 1 output, bit 3 serial) and the
baud rate's code (15 for 19,200; 0 for none); a 0 byte ends the table.

## Direct BIOS calls: function 50

A program calls the BIOS only through [function 50](07-function-reference.md#bdos-50): the BIOS's jump table, at
`$F800` in common memory, is the BDOS's, and works only with the system's windows. Function 50 takes a parameter
block: the BIOS function, its A, X and Y, and two words.

The BIOS's entries are CP/M 3's, in CP/M 3's order, with the registers mapped as the BDOS's are: a byte in A, a word
in A and X, MOVE's other two words in the block's +4-5 and +6-7.

| No. | Entry | Takes | Returns |
|---|---|---|---|
| 0 | BOOT | refused by function 50: A = X = `$FF` | |
| 1 | WBOOT | | the warm boot: doesn't return |
| 2 | CONST | | A = `$FF` a key waits, 0 none |
| 3 | CONIN | | A = the key, waited for |
| 4 | CONOUT | A = the character | |
| 5 | LIST | A | |
| 6 | AUXOUT | A | |
| 7 | AUXIN | | A |
| 8 | HOME | | track 0 |
| 9 | SELDSK | A = the drive (0-15); X bit 0: 1 if selected before, 0 reads the card's units again | A/X = its DPH, 0 for no such drive |
| 10 | SETTRK | A/X = the track | |
| 11 | SETSEC | A/X = the sector, of 512 bytes | |
| 12 | SETDMA | A/X = the buffer | |
| 13 | READ | | A = 0, or 1 an error |
| 14 | WRITE | A = CP/M's deblocking code | A = 0, 1 an error, 2 read-only |
| 15 | LISTST | | A = `$FF` ready, 0 not |
| 16 | SECTRN | A/X = the sector, +4-5 the table | A/X = the sector: there is no translation |
| 17 | CONOST | | A = `$FF` ready, 0 not |
| 18 | AUXIST | | A |
| 19 | AUXOST | | A |
| 20 | DEVTBL | | A/X = the character device table |
| 21 | DEVINI | A = the device | |
| 22 | DRVTBL | | A/X = the drive table, 16 DPHs |
| 23 | MULTIO | A = the count | A = 0: READ and WRITE still take one sector |
| 24 | FLUSH | | A = 0: nothing is held back |
| 25 | MOVE | A/X = the count, +4-5 the source, +6-7 the destination | A/X = the destination + the count; A = X = `$FF` if nothing moved |
| 26 | TIME | A = 0 the clock into the SCB, `$FF` the SCB into the clock | |
| 27 | SELMEM | A = the bank MOVE copies within | |
| 28 | SETBNK | A = the bank of SETDMA's buffer | |
| 29 | XMOVE | A = the source's bank, X = the destination's | for the next MOVE only |

**Banks.** Bank 0 is the system's view, bank 1 the program's own memory, as the BDOS's stub saw its windows. Any
other bank is taken as 0, but XMOVE's may also be `$80` + the top four bits of a physical address, for a MOVE to
or from physical memory. The warm boot sets SETBNK's and SELMEM's banks back to 0 and drops a pending XMOVE, so
what one program sets doesn't reach the next. A program that reads a sector into its own buffer says SETBNK 1
first; one that copies a table out of the BIOS says XMOVE 0, 1, then MOVE.

**What isn't memory:** an address in the I/O hole, in a window on flash, or a destination in a read-only window
has no physical address. READ and WRITE return 1 and MOVE moves nothing and returns `$FFFF`. A MOVE can also end
at `$FFFF`; function 50 doesn't pass MOVE's carry on, so a program tells the two apart by its destination plus its
count.

**The drives.** After a function 50, the BDOS selects its own drive again before its next sector, so a program's
SELDSK can't send the BDOS's reads elsewhere. A program that reads and writes sectors itself (INITDIR does)
bypasses the BDOS's buffers: it resets the drive with function 37 before, which writes its buffers back and
drops them, and again after, so the BDOS reads what the program wrote.

## The System Control Block

The SCB is 100 bytes of the system's state at CP/M 3's offsets: the console's width and column, the vectors, the
console mode, the delimiter, the DMA address, the current drive and user, the error mode, the date and time, and
the rest. [Appendix C](appendix-c-scb.md) has every field.

A program reads and sets a field with [function 49](07-function-reference.md#bdos-49). The SCB is in the system's
memory, not the program's, so 49 is the only way to it. It is where the BDOS keeps that state, with no second
copy, so a set takes effect at once: setting the DMA address is function 26, the drive 14 (unchecked), the user 32,
the error mode 45, and so on. Every byte may be set; setting one the BDOS uses for itself is the program's
responsibility.

Two fields are ours, in bytes CP/M 3 reserves: `$17`, function 47's request to the CCP, and `$18`, whose bit 7
is SETDEF's display mode. The current drive and user at `$3E` and `$44` are the BDOS's; the CCP keeps its own and
makes them current again at each warm boot, so a program's change of drive or user lasts until it ends (unless
it chains with function 47's `$FF`).

## Date and time

The time is kept by the DS3231 on the CPU card, through the north bridge, with a battery to keep it running. OODOS
reads it for every date stamp and for function 105, into the SCB.

A date and time is a DAT, CP/M 3's, 4 bytes:

| Offset | What |
|---|---|
| +0-1 | the day: 1 January 1978 is day 1 |
| +2 | the hour, BCD (`$00`-`$23`) |
| +3 | the minute, BCD |

- **[Function 105](07-function-reference.md#bdos-105)** fills a DAT and returns the seconds, BCD, in A. A clock past
  2155 reads as no time: A = X = `$FF`.
- **[Function 104](07-function-reference.md#bdos-104)** sets the clock from a DAT, the seconds 0.
- **The SCB** holds the date and time as last read (`$58`-`$5C`: the day, the hour, the minute, the second). A
  program may read them by function 49, but they are only as fresh as the last read; 105 reads the clock.

The clock keeps 2000-2199, and OODOS's day numbers reach 2155: a date outside 2000-2155 doesn't set it. Leap years
are every fourth, 2100 among them, in both the clock and OODOS, so the two always agree. The user sets the time with
DATE ([User's Guide](../oodos-users-guide/05-utilities.md)).
