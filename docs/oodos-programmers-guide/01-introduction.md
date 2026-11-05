# 1 Introduction

What OODOS is, where it comes from, and what a program sees when it runs. For anyone writing a .COM or .PRL program
for the OO-1, in ca65 or cc65.

The design record, for the reasons: [hw/oodos.md, "Goals"](../../hw/oodos.md#goals) and ["What a program
sees"](../../hw/oodos.md#what-a-program-sees).

## What OODOS is

OODOS, the One Off DOS, is the OO-1's disk operating system. It loads from the SD card: the ROM BIOS boots the
card's `/BOOT.BIN`, OOLDR, which reads `OODOS.SYS` from drive A: and starts it. Then the prompt, `A>`.

It has three parts, as CP/M 3 has:

- **the BDOS,** the functions a program calls: the console and the other devices, files and drives, the system
  control block, memory. It runs banked, in a 16 KB block of its own, out of the program's way.
- **the BIOS,** under the BDOS: the SD card's drives and the `M:` RAM disk, the console, the clock, the copies
  between memory blocks. A program reaches it through the BDOS, by function 50.
- **the CCP,** which reads the command line, runs its built-in commands, and loads and runs programs.

A program is a `.COM` file on a drive, a 6502 program that calls the BDOS. The machine underneath, without OODOS,
is the [Technical Reference](../oo1-technical-reference/README.md)'s.

## Its CP/M 3 heritage

OODOS is CP/M 3 (CP/M Plus) written again for the W65C02S and the OO-1's backplane, from the meaning of CP/M 3's
interface, not from its code. What is kept is kept exactly:

- **The function numbers and what they do.** Function 15 opens a file here as there, with the same FCB and the
  same return codes. Where a CP/M 3 function has no number here, it doesn't exist here; no number means one thing
  in OODOS and another in a Digital Research system.
- **The disk format:** the directory, extents, directory labels, date stamps and passwords, so `cpmtools` on Linux
  reads and writes an OODOS drive.
- **The base page and the FCB,** with CP/M's offsets, moved up to page 2.
- **The banked structure:** the BDOS deblocks sectors, the BIOS reads and writes them.

A program written for CP/M 3's interface ports by its logic: the same calls, in the 6502's registers. CP/M's
8080 and Z80 programs don't run, now or later; the CPU is different.

## What's ours

Where the 6502 or the OO-1 needs it, OODOS does its own thing, and this guide says "ours" each time. In short
(every case is in [appendix G](appendix-g-differences.md)):

- **The registers:** the function in Y, the parameter in A and X, the result in A and X
  ([chapter 2](02-system-interface.md#calling-the-bdos)).
- **The addresses:** the base page at `$0200`, programs at `$0300`, the BDOS's entry at `$0203`.
- **The program header:** 16 bytes at the start of every `.COM` and `.PRL`, which the CCP checks before it runs a
  program ([chapter 6](06-program-formats.md)).
- **Memory beyond 64 KB:** CP/M-86's memory functions, 51-58, hand out 16 KB blocks of the 1 MB, and FARCALL calls
  code in them ([chapter 5](05-memory-and-banking.md)).
- **OODOS's own functions,** from 72: the versions and FARCALL's address, a CPU address's physical one, mounting
  a drive, leaving for the ROM ([chapter 7](07-function-reference.md#bdos-72)).
- **Rules that keep a program fit for a 65C816,** checked by the build
  ([chapter 6](06-program-formats.md#rules-for-programs-the-65c816)).

## The memory a program sees

| Address | What |
|---|---|
| `$0000-$00EF` | the program's zero page |
| `$00F0-$00FF` | the RSXs' zero page (phase 5) |
| `$0100-$01FF` | the program's stack |
| `$0200-$02FF` | the base page ([chapter 2](02-system-interface.md#the-base-page)) |
| `$0300-$BFFC` | the TPA: the program, its data, and its BSS, 47 KB |
| `$BFFD-$BFFF` | a `JMP` to the BDOS: the TPA's top |
| `$C000-$C7FF` | the I/O registers, always there; OODOS's, not the program's |
| `$C800-$F7FF` | OODOS's resident code: the CCP; during a far call, the program's own banked code |
| `$F800-$FFFF` | common memory: the BDOS's entry, the BIOS's jump table, FARCALL, the interrupt entry |

The BDOS and the BIOS run on their own zero page and stack, in other blocks, so a call never touches the program's
`$00-$EF`, and takes six bytes of its stack: the return address and four the entry saves the registers in. FCB and DMA
addresses are the program's own CPU addresses anywhere in `$0000-$BFFF`; the BDOS finds them in physical memory itself.
The 1 MB beyond these 64 KB is reached through the memory functions ([chapter 5](05-memory-and-banking.md)).

## How to read this guide

Chapters 2 to 6 explain: how to call OODOS, files, the console and the devices, memory, the program formats.
[Chapter 7](07-function-reference.md) has an entry for each function, each with the same headings: Entry, Exit,
Errors, Side effects, Cycles, Since, Status, and how it differs from CP/M 3. Chapter 8, the game services, comes
with OOLIB. The appendices gather what is spread through the chapters: the error codes, the `.PRL` format, the SCB,
every function on one page, the control characters, building a program, and the differences from CP/M 3.

Numbers are written as [docs/README.md](../README.md#conventions) says: hex with `$`, a word in registers as A/X
(A low, X high). Each entry's status is **built** (in OODOS.SYS and tested) or **proposed**. Where this guide and
the code disagree, the code is right and the guide is wrong: report it. An agent reads
[AGENTS.md](../../AGENTS.md) first.

[Appendix F](appendix-f-building.md) builds and runs a first program.
