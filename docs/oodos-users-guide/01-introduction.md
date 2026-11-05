# 1 Introduction

What OODOS is from the prompt: starting it, the A> prompt, and what this guide covers.

The design record, for the reasons: [hw/oodos.md, "Goals"](../../hw/oodos.md#goals).

## What OODOS is

OODOS is the OO-1's disk operating system. It is CP/M 3 (CP/M Plus) written again for the OO-1's 6502 and its
backplane: the same command line, the same file names and drives, the same disk format, and CP/M 3's built-in
commands and its utilities for files and drives, so a CP/M 3 user is at home. Its programming tools (ED, the
assemblers and the debugger, among others) aren't part of it, and SUBMIT, GET and PUT come with a later phase. It
is a new program, written from what CP/M 3 does; no Digital Research code is in it.

OODOS runs programs written for it: 6502 programs in `.COM` files. It can't run the 8080 and Z80 programs of other
CP/M machines, as the CPU is different; it checks each program's header first and refuses a file without OODOS's
(`NO PROGRAM`, [chapter 6](06-errors.md)).

OODOS's drives are image files on the SD card, and a RAM disk. Linux's cpmtools read and write the same images
(`cpmls`, `cpmcp`), so files go between OODOS and a PC on the card.

This guide is for whoever types at the prompt: the command line, drives and file names, the commands the system
runs itself ([chapter 4](04-built-in-commands.md)), the utilities on the card ([chapter 5](05-utilities.md)), and
what the messages mean ([chapter 6](06-errors.md)). Writing programs is the
[Programmer's Guide](../oodos-programmers-guide/README.md).

## Starting OODOS

OODOS starts from the SD card. With the card in, the OO-1 shows its splash at power-on, then the BIOS boots the card
by itself. A key pressed during the splash goes to the ROM's monitor instead; from there, `C80CR` boots the card. A
machine with no OODOS card goes to the monitor, as it does without OODOS.

The card holds a loader, `/BOOT.BIN`, and a folder, `/OODOS`. The file `/OODOS/DRIVES.TXT` says which image file
on the card is each drive ([chapter 3](03-drives-and-files.md#drives)); the loader checks it, puts the images in
place and loads `OODOS.SYS` from drive A:. If it can't, it says why (the line of `DRIVES.TXT` at fault, or what is
missing) and goes back to the monitor.

OODOS then says who it is and what it found, in four lines, on the screen and the serial line alike:

```
OODOS 0.2  2026-11-03 12:00:00
BIOS 0.10   47K TPA
A: 8M   B: 8M   M: 128K
Cards: video, sound, SD, Disk II, USB keyboard, DMA, panel
```

- **The first line:** OODOS's version, and the date and time from the clock.
- **The second:** the ROM BIOS's version (`?` if the ROM doesn't say), and the memory a program gets, the TPA.
- **The third:** each drive and its size.
- **The fourth:** the cards that answered, in that order, or `none`.

Then the prompt. To leave OODOS, see [WOZMON](04-built-in-commands.md#wozmon) and
[APPLE2](04-built-in-commands.md#apple2).

## The prompt

The prompt is the current drive and `>`:

```
A>
```

When the user number isn't 0, the prompt starts with it: `5A>` is user 5 on drive A:, `12M>` user 12 on M:. OODOS
starts on A:, user 0. A command without a drive works on the current drive, and a file is looked for among the
current user's files ([chapter 3](03-drives-and-files.md#user-numbers)).

Type a command after the prompt and press Return. When it is done, the prompt comes again on a new line. Typing and
editing a command is [chapter 2](02-command-line.md).
