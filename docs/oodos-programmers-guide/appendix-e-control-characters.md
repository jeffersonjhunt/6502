# Appendix E: Control characters

The console's control characters: line editing, ^C, ^S, ^Q, ^P, and the VT52 subset.

The keyboard and the serial line both give a control key as its ASCII code: ^C is `$03`, ^S `$13`. The design
record, for the reasons: [hw/oodos.md, "The BDOS, phase 3"](../../hw/oodos.md#the-bdos-phase-3) (3b2) and
["BIOS"](../../hw/oodos.md#bios).

## Line editing

[Function 10](07-function-reference.md#bdos-10), and so the `A>` prompt, edits its line with these keys. The cursor
may be anywhere in the line, and a key typed goes in at the cursor.

| Key | Code | What |
|---|---|---|
| ^A | `$01` | the cursor left one character |
| ^B | `$02` | the cursor to the line's start; at the start, to its end |
| ^C | `$03` | in an empty line, the program ends (below); otherwise a character of the line |
| ^E | `$05` | the rest of the line, from the cursor, shown on a new row |
| ^F | `$06` | the cursor right one character |
| ^G | `$07` | the character at the cursor taken out |
| ^H | `$08` | backspace: the character before the cursor taken out (with the SCB's `$2E` `$FF`, as rub/del's echo) |
| ^J | `$0A` | the line ends (LF) |
| ^K | `$0B` | everything from the cursor to the end taken out |
| ^M | `$0D` | the line ends (CR) |
| ^P | `$10` | printer echo on or off |
| ^R | `$12` | the line typed again on a new row |
| ^U | `$15` | the line up to the cursor kept as the previous line; the line emptied, on a new row |
| ^W | `$17` | in an empty line, the previous line back; otherwise the cursor to the end |
| ^X | `$18` | everything before the cursor taken out |
| rub/del | `$7F` | as ^H (with the SCB's `$2F` 0: at the line's end, the character taken out is echoed instead of erased) |

- The line ends at CR or LF wherever the cursor is, the whole line taken, and becomes the previous line.
- Any other control character is part of the line, shown as `^` and its letter, two columns.
- A key past the buffer's size is dropped with a bell.
- The line is shown on rows of the console's width, never in its last column. Typing at a full row's end starts a
  new row; a move before the row's start types the line again on a new row.

## ^C

- **In function 10,** a ^C while the line is empty (nothing typed, or all of it taken out) ends the program by the
  warm boot, with the return code `$FFFE`. In a line that isn't empty it is a character.
- **While the output is stopped** by ^S, a ^C ends the program the same way.
- **At the `A>` prompt,** an empty line's ^C resets the disk system (function 13) as the CCP comes back: every
  drive logged out and read-write again. A ^C that ends a program also drops the rest of its command line.
- **Anywhere else** a ^C is a key like any other: functions 1, 6 and 11 return it, and the output functions keep it
  for the next read. A program that wants ^C to stop it sets bit 0 of the console mode and asks function 11.
- **Bit 3 of the console mode** turns the ending off: a ^C in an empty line is a character, and after a ^S it
  rings the bell.

## ^S, ^Q and ^P

Before each character functions 2, 9 and 111 write, the BDOS looks at the keyboard, and functions 1 and 11 handle
the keys they read the same way:

| Key | Code | What |
|---|---|---|
| ^S | `$13` | stops the output, until ^Q |
| ^Q | `$11` | goes on after a ^S; on its own, dropped |
| ^P | `$10` | printer echo on (with a bell) or off; on its own, dropped |

While the output is stopped, ^Q goes on, ^C ends the program, ^P turns printer echo over and stays stopped, and any
other key rings the bell and is dropped. **Printer echo** sends what functions 1, 2, 9, 10 and 111 put on the
console to LST: as well, but not raw output (console mode bit 2), nor function 6's. Bit 1 of the console mode turns
the handling of all three off ([chapter 4](04-console-devices-scb.md#console-modes)).

## The VT52 subset

The screen takes these, written by any console function:

| Sequence | What |
|---|---|
| CR (`$0D`) | the cursor to column 0 |
| LF (`$0A`) | down a line, the screen scrolling at the bottom |
| BS (`$08`) | back a column |
| BEL (`$07`) | nothing yet: no bell |
| ESC A, ESC B, ESC C, ESC D | the cursor up, down, right, left, stopping at the edge |
| ESC H | the cursor home, the top left |
| ESC J | erase to the end of the screen |
| ESC K | erase to the end of the line |
| ESC E | clear the screen, the cursor home |
| ESC Y row column | the cursor to a place: the row and the column each + 32 (a space is 0); past the edge, the last |

The screen is 80 columns and 30 rows. A character in the last column wraps to the next line. Other control
characters and DEL do nothing; every other byte is shown as it is, lower case as lower case.

**The serial line** gets the same sequences as ANSI, which a PC's terminal program reads: ESC A-D, H, J and K as
ESC `[` and the same letter, ESC E as ESC `[H` ESC `[2J`, and ESC Y as ESC `[`row`;`column`H`, counted from 1.
Other ESC pairs are dropped there, as on the screen.
