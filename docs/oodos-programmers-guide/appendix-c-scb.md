# Appendix C: The SCB

The System Control Block's fields by offset, as function 49 reads and sets them.

The SCB is 100 bytes, `$00`-`$63`, at CP/M 3's offsets for every field CP/M 3 documents. It lives in the system's
memory: a program reaches it only through [function 49](07-function-reference.md#bdos-49), and a set takes effect
at once ([chapter 4](04-console-devices-scb.md#the-system-control-block)). A word is low byte first. "Cold boot" is
the value OODOS starts with; the bytes not listed are reserved, 0.

| Offset | Size | What | Cold boot |
|---|---|---|---|
| `$05` | 1 | the version, as function 12 returns it (12 returns `$0031` whatever this holds) | `$31` |
| `$06` | 4 | the user flags: the program's, not looked at by OODOS | 0 |
| `$10` | 2 | the program return code, function 108's | 0 |
| `$17` | 1 | ours: function 47's request to the CCP. Bit 7 a command waits at `$0280`; bit 6 with the program's drive and user | 0 |
| `$18` | 1 | ours: bit 7 SETDEF's display mode, the CCP says where a program came from before it runs | 0 |
| `$19` | 1 | ours, for RSXs that redirect the console: bit 7 set while the CCP runs, clear while a program does; bits 0-6 a count the CCP steps as each command starts | 0, then bit 7 |
| `$1A` | 1 | the console's width less one | 79 |
| `$1B` | 1 | the console's column | 0 |
| `$1C` | 1 | the page length, for the CCP's and the utilities' paging | 24 |
| `$22` | 2 | CON: input's devices: bit 15 CRT, 14 TTY, 13 LPT | CRT, TTY |
| `$24` | 2 | CON: output's devices | CRT, TTY |
| `$26` | 2 | AUX: input's devices | TTY |
| `$28` | 2 | AUX: output's devices | TTY |
| `$2A` | 2 | LST: output's devices | LPT |
| `$2C` | 1 | the page mode: 0 the CCP and the utilities stop after each page, else not | 0 |
| `$2E` | 1 | ^H in function 10: 0 a backspace, `$FF` as rub/del's echo | 0 |
| `$2F` | 1 | rub/del in function 10: `$FF` as a backspace, 0 echoes the character it takes | `$FF` |
| `$33` | 2 | the console mode, function 109's | 0 |
| `$37` | 1 | the output delimiter, function 110's | `$` |
| `$38` | 1 | printer echo (^P): non-zero on | 0 |
| `$3C` | 2 | the DMA address, function 26's | `$0280` |
| `$3E` | 1 | the BDOS's current drive, 0 A: (functions 14 and 25) | 0 |
| `$44` | 1 | the BDOS's current user (function 32), kept to 0-15 | 0 |
| `$4A` | 1 | the multi-sector count, function 44's | 1 |
| `$4B` | 1 | the error mode, function 45's | 0, the default |
| `$4C` | 4 | the drive search chain: 0 the current drive, 1-16 A:-P:, `$FF` ends it | 0, `$FF` |
| `$50` | 1 | the temporary file drive, 0 the current one | 0 |
| `$51` | 1 | the drive of the last physical error | |
| `$57` | 1 | the BDOS's flags: bit 7 the long error messages, which OODOS always gives | `$80` |
| `$58` | 2 | the date, as a DAT's day: 1 January 1978 is day 1 | the clock |
| `$5A` | 1 | the hour, BCD | the clock |
| `$5B` | 1 | the minute, BCD | the clock |
| `$5C` | 1 | the second, BCD | the clock |
| `$5D` | 2 | common memory's base | `$F800` |

**What the warm boot sets.** At each warm boot the CCP sets `$19`'s bit 7 (clear again as a program starts), and the console mode to 0, the delimiter to `$` and the
multi-sector count to 1, clears `$17` once it has read it, and makes its own drive and user current (`$3E`,
`$44`). Before a program it sets the DMA address to `$0280`, the error mode to the default and, unless the program
was chained to, the return code to 0.

**The date and time** are the clock as last read. The BDOS reads the clock into them for each date stamp and for
function 105, so a value set by 49 lasts only until then; function 104 sets the clock itself.

**The page length and mode** (`$1C`, `$2C`) are kept for the CCP and the utilities, which page their output by
them; the BDOS doesn't use them.

[hw/oodos.md, "The BDOS, phase 3"](../../hw/oodos.md#the-bdos-phase-3) (3b2) has the reasons for each value.
