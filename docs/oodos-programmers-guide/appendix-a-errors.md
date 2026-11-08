# Appendix A: Error codes

Every code the BDOS returns, by function, and the physical errors in X.

Each function's entry in [chapter 7](07-function-reference.md) is the reference; this appendix puts the codes in one
place. A function returns in A, or A and X ([chapter 2](02-system-interface.md#return-and-error-codes)). It can
fail three ways:

- **A result code:** the function did what it could and says how it went, in A (X 0 unless the entry says
  otherwise): `$FF` for "not found" or "no room", or a small number for the record functions. These come back
  whatever the error mode.
- **A physical error:** something a program usually can't go on from: the disk failed, the drive is read-only, a
  password is wrong. What happens is the error mode's ([function 45](07-function-reference.md#bdos-45), below).
- **Memory that isn't the program's:** an FCB, a buffer or a parameter block in the I/O hole (`$C000`–`$C7FF`)
  or on flash; a read into a read-only window; or, with a DMA segment, memory OODOS keeps for itself. The function
  stops and returns A = X = `$FF`, whatever the error mode, and says nothing.

**10, Media Change,** is a result code: on O: or P:, the disk was changed under an FCB opened before, or under a
call about to write the directory, and nothing was read or written
([removable drives](03-file-system.md#removable-drives-o-and-p)). It comes whatever the error mode. A function
that otherwise says a failure as `$FF` (15 with access stamps, 19, 22, 23, 30, 99, 100, 103) gives 10 too: a program that tests only for
`$FF` takes it for success, so test for 0. With 10 from a read or a write (20, 21, 33, 34, 40), X
isn't the records done, multi-sector or not: the program opens the file again and works out where it was.

A function number OODOS doesn't have returns A = X = `$FF`: 61–71, 77–97, 113–151, and 153 on. So does 60 when no
RSX takes it. 41, MP/M's test and write, returns A = `$FF` with X 0, as CP/M 3 answers it.

## Physical errors

| X | Message | What causes it |
|---|---|---|
| 1 | `Disk I/O` | a sector that couldn't be read or written; the close of a file whose data couldn't be written |
| 2 | `Read Only` | a change to a read-only drive: made so by function 28, on a read-only unit, or on a unit that refused a write |
| 3 | `File R/O` | a write to, or a delete or rename of, a read-only file (t1'); a write to a file opened from user 0, or opened without a write-protected file's password; a truncate (99) of a read-only file |
| 4 | `Invalid Drive` | a drive OODOS doesn't have, or one with no image mounted |
| 7 | `Password Error` | a protected file or label, when neither the password given nor the default one is its password |
| 8 | `File Exists` | a make, or a rename's new name, that the user has already |
| 9 | `? in Filename` | a `?` where one name is needed |

Any function that wants a sector buffer, including one that only reads (14, 15, 17, 18, 20, 33, 35, 46, 101, 102),
can get Read Only (2) for O: or P: when the buffer it takes held that drive's waiting data and the drive's disk
turns out to be write-protected; the SCB's error drive names O: or P:, not the function's drive. The function's work is done first and the error said at its end; all of that drive's waiting data is dropped
with it, so it comes once for the disk. This is when the buffer held the *other* floppy drive's data: when it
held the function's own drive's, the Read Only is said at once, part-way, as any write-back's failure on the
drive is. If the function fails itself as well, its own error is the one returned, with its own drive in the
SCB's error drive; in display and default mode the held Read Only is said first, then the function's own, and
the default mode's end of the program comes after both. A write (21, 34, 40) that gets the held Read Only has
its record written and CR moved on: don't write the record again. (A missing disk says nothing there: its data
is kept and marked.)

## Error modes

[Function 45](07-function-reference.md#bdos-45) sets the error mode, the SCB's byte `$4B`:

| Mode | What a physical error does |
|---|---|
| `$FF`, return | A = `$FF`, X = the code. Nothing is printed. |
| `$FE`, display and return | the message is printed (below), then A = `$FF`, X = the code |
| anything else: the default | the message is printed and the program ends by the warm boot, with the return code `$FFFD` ([function 108](07-function-reference.md#bdos-108)) |

The message is two lines; the second names the file for a function with an FCB:

```
OODOS Error On B: Disk I/O
BDOS Function = 15 File = README  .TXT
```

In every mode the SCB's error drive (`$51`) is set to the drive. A program starts in the default mode. In a return
mode, a multi-sector call that fails keeps the records it had done: the FCB is written back as they left it.

## Result codes by function

The functions with result codes, and what each means. A function not listed returns no code: 0, or what its entry
says. The last column is the physical errors the function can raise.

| Function | Result codes | Physical |
|---|---|---|
| 13 `DRV_ALLRESET`, 37 `DRV_RESET` | 0 | 1, 2: the first failure, after the reset |
| 14 `DRV_SET` | 0 | 1, 4 |
| 15 `F_OPEN` | 0 opened; `$FF` not found; 10 the disk changed (O:, P:), with access stamps | 1, 4, 7, 9 |
| 16 `F_CLOSE` | 0; `$FF` no entry to be had, or two blocks for one place; 10 the disk changed (O:, P:) | 1, 2, 4 |
| 17 `F_SFIRST`, 18 `F_SNEXT` | 0–3 the entry's place in the record; `$FF` no more | 1, 4 |
| 19 `F_DELETE` | 0; `$FF` not found; 10 the disk changed (O:, P:) | 1, 2, 3, 4, 7 |
| 20 `F_READ` | 0; 1 the end of the file; 10 the disk changed (O:, P:). X = the records done, multi-sector | 1, 2, 4 |
| 21 `F_WRITE` | 0; 1 no directory space; 2 the disk full; 10 the disk changed (O:, P:). X = the records done | 1, 2, 3, 4 |
| 22 `F_MAKE` | 0; `$FF` the directory full; 10 the disk changed (O:, P:) | 1, 2, 4, 8, 9 |
| 23 `F_RENAME` | 0; `$FF` not found; 10 the disk changed (O:, P:) | 1, 2, 3, 4, 7, 8, 9 |
| 30 `F_ATTRIB` | 0; `$FF` not found; 10 the disk changed (O:, P:) | 1, 2, 4, 7, 9 |
| 31 `DRV_DPB` | the DPB's address | 1, 4 |
| 33 `F_READRAND` | 0; 1 never written; 3 the extent can't be closed; 4 an extent never written; 6 past 32 MB; 10 the disk changed (O:, P:). X = the records done | 1, 2, 4 |
| 34 `F_WRITERAND`, 40 `F_WRITEZF` | 0; 2 the disk full; 3 the extent can't be closed; 5 no directory space; 6 past 32 MB; 10 the disk changed (O:, P:). X = the records done | 1, 2, 3, 4 |
| 35 `F_SIZE` | 0; `$FF` not found | 1, 4, 9 |
| 44 `F_MULTISEC` | 0; `$FF` a count that isn't 1–128 | — |
| 46 `DRV_SPACE` | 0, the free records at the DMA address | 1, 4 |
| 48 `DRV_FLUSH` | 0 | 1, 2: the first failure, after every buffer is tried |
| 49 `S_SCB` | a get: the word; 0 past `$63` | — |
| 50 `S_BIOS` | the BIOS's A and X; `$FFFF` for BOOT or a function past XMOVE | — |
| 53–57 `MC_MAX`–`MC_FREE` | 0; `$FF` with X 0: no memory, or a base or length refused | — |
| 74 `O_MOUNT` | 0; `$FF` with X: 1 the card or a write-back failed, 4 not B:–G:, `$10` the path refused, `$11` not found, `$12` the image's size, `$13` another drive's file. Whatever the error mode | — |
| 75 `O_ROM` | doesn't return; `$FF` with X: `$FF` no Apple II ROM, or A not 0 or 1; `$FE` no DMA controller, for Apple II mode | — |
| 76 `O_SECTOR` | 0; 1 the sector's error, or past the disk; 2 write-protected; `$FF` not an Apple disk. Whatever the error mode | — |
| 99 `F_TRUNCATE` | 0; `$FF` not found, or not a record the file has; 10 the disk changed (O:, P:) | 1, 2, 3, 4, 7, 9 |
| 100 `DRV_SETLABEL` | 0; `$FF` stamps asked for without SFCBs, or no free entry; 10 the disk changed (O:, P:) | 1, 2, 4, 7 |
| 101 `DRV_GETLABEL` | the label's byte 12, 0 none | 1, 4 |
| 102 `F_TIMEDATE` | 0; `$FF` not found | 1, 4, 9 |
| 103 `F_WRITEXFCB` | 0; `$FF` no label or passwords off, not found, or no free entry; 10 the disk changed (O:, P:) | 1, 2, 4, 7, 9 |
| 105 `T_GET` | the seconds; `$FFFF` the clock past 2155 | — |
| 152 `F_PARSE` | 0 the end; the address of what follows; `$FFFF` not a file name | — |

Function 47 doesn't return: it ends the program by the warm boot, and the CCP says what is wrong with its command.
