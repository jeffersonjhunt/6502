# OODOS: One Off DOS #

Draft, 2026-10-26. The plan for the OO-1's disk operating system: CP/M Plus (CP/M 3) written again for the
W65C02S and the backplane. As in [design.md](design.md), each section separates what is **decided** from what is
**proposed**; open questions are collected at the end.


## Goals ##

**Decided**

- **The CP/M 3 interface, kept exactly where it can be:**
  - the on-disk format: DPBs, extents, directory labels, SFCB date stamps, password entries, disks up to 512 MB and
    files up to 32 MB;
  - the BDOS function numbers and what they do;
  - the banked structure: the BDOS deblocks, the BIOS reads and writes physical sectors only, and RSXs chain on the
    BDOS entry.
- **cpmtools interoperability with Linux:** an OODOS drive is an image file that `mkfs.cpm`, `cpmls`, `cpmcp` and
  `fsck.cpm` work on with a diskdef from this repo.
- **Apple II disks:** OODOS reads and writes them, in the real drive and as image files.
- **RSXs,** loaded and chained as CP/M 3 does.
- **Banked,** using the north bridge's windows and the 1 MB.
- **The first application is GW-BASIC,** ported from Microsoft's 1983 source (MIT licence, released 2020).
- **The name is OODOS.** It's a new implementation written from Digital Research's CP/M 3 manuals: no DR source
  or binaries go into this repo or the website.

**Decided (2026-10-26): no 8080 or Z80 CP/M programs,** now or by a later card. The CPU is different; OODOS
programs are 6502 programs written to the same interface.


## Disk format ##

**Decided: CP/M 3's, unchanged.** A drive is described by a DPB as CP/M 3 defines it (SPT, BSH, BLM, EXM, DSM, DRM,
AL0/AL1, CKS, OFF, PSH, PHM), and the directory holds CP/M 3's entries: files, XFCBs (passwords), the directory
label, and an SFCB in every fourth entry when the drive has date stamps.

- **Date stamps:** two per file (creation or access, and update), each 2 bytes of days from 1 January 1978, then the
  hour and minute in BCD. The directory label chooses which are kept, as in CP/M 3.
- **Decided (2026-10-26): creation and update stamps.** OODOS drives have the directory label's mode byte (entry
  0, byte 12) at 0x31: a label, creation stamps, update stamps; no access stamps, no passwords. INITDIR's default
  is the same. `mkfs.cpm -t` writes 0x11 (creation only), so a drive made on Linux has the byte set after it.
- **Directory hashing** is CP/M 3's, kept in memory only (the DPH's HASH table, 4 bytes an entry). It never
  touches the disk, so it costs interoperability nothing. With 1 MB it's on for every drive.
- **cpmtools.** The repo carries [`oodos/diskdefs`](../oodos/diskdefs) with OODOS's formats, so a card is made on
  Linux with, for example ([oodos/README.md](../oodos/README.md) has more):

  ```
  mkfs.cpm -f oodos-8m -t -L OODOS A.IMG
  printf '\061' | dd of=A.IMG bs=1 seek=12 conv=notrunc
  cpmcp -f oodos-8m A.IMG HELLO.TXT 0:
  cpmls -f oodos-8m -F A.IMG
  ```

**Decided (phase 0, 2026-10-26): the formats**

| diskdef | Sector | Size | Block | Directory | Boot tracks | For |
|---|---|---|---|---|---|---|
| `oodos-8m` | 512 | 8 MB (1024 tracks of 16) | 4 KB | 1024 | 0 | the usual SD drive |
| `oodos-32m` | 512 | 32 MB | 8 KB | 2048 | 0 | a large SD drive |
| `oodos-ram` | 512 | 128 KB, banks C–D | 2 KB | 256 | 0 | M:, made at boot, never on a card |
| `apple-do` | 256 | 140 KB, 35 × 16 | 1 KB | 64 | 3 | Apple CP/M (SoftCard) disks, cpmtools's own diskdef |

- No boot tracks on SD drives: the loader is `/BOOT.BIN` on the FAT card (below), so a drive image is all file
  system.
- `apple-do` is a CP/M 2.2 format. CP/M 3 reads and writes those; they just have no date stamps.
- **Checked with cpmtools 2.23** ([oodos/test/check_diskdefs.sh](../oodos/test/check_diskdefs.sh)): `mkfs.cpm -t`
  writes a directory label and an SFCB in every fourth entry, the label's mode is set to 0x31 (creation and update
  stamps, as INITDIR sets it) and checked, `cpmcp` stamps what it writes, `cpmls -F` reads the stamps back, and
  `fsck.cpm` passes, for all three formats; and each DPB is legal (at most 16 directory blocks, DSM within 16 bits,
  under 256 for 1 KB blocks). cpmtools writes both stamps whatever the mode, so only OODOS's BDOS (phase 3)
  will honour it. Each reading the other's drives is tested both ways from phase 1 ([Testing](#testing)).


## Drives ##

**Proposed**

| Drive | What |
|---|---|
| A–G | SD units 1–7: image files on the FAT card, mounted with MOUNT. A is the system drive |
| M | the RAM disk, in banks C–D (128 KB) |
| O, P | the Apple drives 1 and 2, through the Disk II card |

Which image each SD drive holds is set by a file on the card, `/OODOS/DRIVES.TXT`, read by the loader, and can be
changed while running by a utility that does MOUNT and resets the drive.

**Decided (2026-10-26): `DRIVES.TXT`** (the test card has one, [oodos/card/DRIVES.TXT](../oodos/card/DRIVES.TXT)):

```
# A is the system drive.
A: /OODOS/A.IMG
B: /OODOS/WORK.IMG
c:	/DISKS/CPM.DSK  ro      # read-only
```

Each line is one of these, and anything else is an error:

| Line | What |
|---|---|
| blank | nothing, or only spaces and tabs |
| a comment | optional spaces and tabs, then `#` and anything to the end of the line |
| a drive | optional spaces and tabs; a drive letter, `A`–`G` in either case (SD units 1–7); `:`; one or more spaces or tabs; the image's path; optionally one or more spaces or tabs and `RO` in any case; optionally spaces and tabs; optionally `#` and a comment |

- **The path** is the image's as MOUNT takes it: from the card's root, so it starts with `/`; `/`-separated, long
  or 8.3 names in any case (fat_volume.h). It has no spaces, tabs, `#` or control characters, and is at most 509
  bytes, what MOUNT's sector holds after the unit and the flags with its NUL.
- **Lines end** in LF or CR LF, so the file can be edited on any PC; the last may have no ending. A CR anywhere
  else, in a comment too, is an error. A UTF-8 byte order mark at the start of the file is skipped. The file is
  read to its size (LIST gives it), not to a NUL.
- **Errors** stop the loader, which says which line, by number, and why, and goes back to the monitor: a line
  that fits none of the forms (a word after the path that isn't `RO`, such as a mistyped `R0`, or a second `RO`;
  a space in the path), a drive named twice (not the later line winning), and the same path on two drives
  (compared in any case, as FAT names are).
- A drive not named isn't mounted. Without an A line, or with a path MOUNT can't find, the loader says which and
  goes back to the monitor.


## Memory ##

### What a program sees ###

**Proposed**

| CPU range | Window | What |
|---|---|---|
| `$0000–$00FF` | W0 | zero page (below) |
| `$0100–$01FF` | W0 | the program's stack |
| `$0200–$02FF` | W0 | the **base page**: CP/M's page 0, moved up a page with the same offsets |
| `$0300`–top | W0–W2 | the TPA, about 47 KB, less any RSXs at its top |
| `$BFFD–$BFFF` | W2 | the BDOS's entry: a `JMP` to the stub in common memory, the TPA's top (phase 2, below) |
| `$C000–$C7FF` | — | the I/O hole, always there |
| `$C800–$F7FF` | W3 | the **code window** (12 KB): OODOS's resident code, or a program's banked code during a far call |
| `$F800–$FFFF` | W3 | **common memory** (2 KB): the BDOS entry stub, the BIOS jump table, the far call table, interrupt entry |

**The base page** keeps CP/M's offsets, so ported code keeps its constants:

| Address | CP/M | What |
|---|---|---|
| `$0200` | `$0000` | `JMP` warm boot |
| `$0203` | `$0005` | `JMP` to the BDOS, or the top RSX. Its operand, `$0204–$0205`, is the top of the TPA + 1, as CP/M's address 6 |
| `$025C` | `$005C` | the default FCB |
| `$026C` | `$006C` | the second FCB |
| `$0280` | `$0080` | the default DMA buffer and the command tail |

**Calling the BDOS:** `JSR $0203` with the function in Y and the parameter in A (low) and X (high). The result comes
back in A (low) and X (high), as CP/M 3 returns it in A and HL.

### Zero page ###

**Decided (2026-10-26): a fixed split.**

- **`$00–$EF` belongs to the program,** at fixed addresses. The BDOS and BIOS never touch it, because they run on
  their own zero page (below).
- **`$F0–$FF` is for RSXs.** The loader gives each RSX its own slice, through the RSX's zero page relocation
  bitmap ([below](#rsxs-and-programs)), so RSXs never share bytes and needn't save them before passing a call down
  the chain. An RSX that doesn't fit in what's left is refused, not overlapped.
- **Interrupt handlers use none of the program's zero page.** They work with absolute and indexed addressing in
  common memory; a driver that needs `(zp),Y` swaps W0 to the system's zero page, as the BDOS and BIOS do.
- **Why not the loader handing out zero page to programs too** (as CP/M-65 does): it costs nothing at run time,
  but needs every program to be relocatable, about 80 ms to load a 24 KB one against 4 ms, and gains nothing on a
  65C816 (below), whose direct page register does the same in hardware.

### Banking ###

**Proposed physical layout**

| Where | What |
|---|---|
| Block 60 | the banked BDOS |
| Block 61 | the OODOS BIOS and drivers |
| Block 62 | W3 while a program runs: OODOS's resident code and common memory; VEC points here too |
| Block 63 | the ROM BIOS and Monitor, as now, so Break still reaches the monitor |
| Block 31 (the top of bank 7) | the system's zero page, stack and data |
| Rest of banks 6–7 | deblocking buffers (BCBs), directory buffers, hash tables |
| Banks C–D | the M: RAM disk |
| Banks 1–5 | blocks programs can ask for (GW-BASIC's banked code, below) |

**Common memory is in every block W3 maps.** Whatever block is in W3, its top 2 KB is the same copy of common
memory, put there by OODOS when the block is allocated for code. So the BDOS entry, the interrupt entry and the far
call table are always at the same addresses, and a far call can swap W3 without losing them. Common memory keeps
no state of its own: that lives in the system's zero page block.

**Entering the BDOS.** The stub in common memory saves the program's stack pointer and window registers, points
W0 at the system's zero page and stack, and W1–W2 at the BDOS and BIOS. On the way out it puts them back and does
RTS. That's a handful of cycles at 7 MHz.

**The program's memory from the BDOS.** FCB and DMA addresses are the program's CPU addresses. The BDOS turns
them into physical ones through the saved window registers and moves data with the DMA controller's memory to
memory copy (CP/M 3's `MOVE` and `XMOVE`), which runs as a block (BIOS 0.9). Sectors come from the SD card by DMA
straight into a buffer's physical address and are never mapped into the CPU's view.

**Allocating blocks.** CP/M 3 has no memory manager, so OODOS adds CP/M-86's (functions 51–58, under
[BDOS](#bdos)), handing out 16 KB blocks. Calling code in a block goes through common memory, not the BDOS (see
[Far calls](#far-calls)). GW-BASIC is the first user.


## BDOS ##

**Decided (2026-10-26): CP/M 3's functions, banked, with CP/M-86's memory functions (51–58) and OODOS's own from
72.** The details below are proposed. Numbers follow
Digital Research's systems wherever one has the function; no number means one thing here and another there.

### Registers ###

- **In:** the function in Y. A byte parameter (CP/M's E) in A; a word parameter (CP/M's DE) in A (low) and X (high).
- **Out:** CP/M's A and L in A, its B and H in X. A word result is A (low) and X (high), as CP/M 3's HL.
- **Errors** follow CP/M 3: A = `$FF` and X = the physical error code (CP/M 3's H) when the error mode
  (function 45) returns errors.
- Y and the flags are not kept. The decimal flag is clear on return.

### CP/M 3's functions ###

All of these do what CP/M 3's do, with the registers above.

| Numbers | What |
|---|---|
| 0–37 | the CP/M 2.2 set, with CP/M 3's changes: 7 and 8 are AUX status, 12 returns `$0031`, 35 returns errors |
| 40 | write random with zero fill |
| 42, 43 | lock and unlock record: succeed and do nothing, as CP/M 3 |
| 44–50 | multi-sector count, error mode, free space, chain, flush, the SCB, direct BIOS calls |
| 59, 60 | load overlay, call an RSX |
| 98–112 | free blocks, truncate, directory labels, date stamps and XFCBs, date and time, passwords, serial number, return code, console mode, output delimiter, print and list block |
| 152 | parse filename |

Not in CP/M 3, and not here: 38, 39 and 41 (MP/M's drive access and test-and-write).

**Where the 6502 changes things:**

- **12** returns `$0031`, as CP/M 3. Programs that need OODOS itself use function 72.
- **27** (allocation vector): in banked CP/M 3 the vector is in the system bank, and here it is too. It returns
  `$FFFF`; function 46 gives the free space.
- **31** returns the address of a copy of the DPB in common memory.
- **49:** the SCB keeps CP/M 3's offsets for every field CP/M 3 documents.
- **50:** the BIOS parameter block is OODOS's: +0 the BIOS function, +1 A, +2 X, +3 Y, +4–5 and +6–7 two words
  (for MOVE and XMOVE's addresses and counts).
- **59** loads an overlay at the FCB's random record address, which must be a page's start, or the call is
  refused. A `.PRL` is relocated there; a `.COM` loads only where it was linked. An overlay has no zero page of
  its own (its header's zero page size must be 0, or it's refused): it uses its caller's, by importing the
  caller's zero page symbols when it's linked ([The .PRL file](#the-prl-file)).
- **104, 105** use CP/M 3's DAT (days from 1 January 1978, hour and minute in BCD); 105 returns the seconds, BCD, in
  A. Both go to the DS3231.

### Memory: CP/M-86's functions ###

CP/M 3 has no memory manager. CP/M-86's suits the OO-1 exactly: it works in paragraphs of 16 bytes, so a 16-bit
paragraph number names any byte boundary of the 1 MB, as an 8086 segment does (and as GW-BASIC's DEF SEG
assumes).

| No. | CP/M-86 name | What |
|---|---|---|
| 51 | F_DMASEG | set the DMA "segment": the paragraph the DMA address counts from, so a read or write reaches any physical memory. `$FFFF`, the default, means the DMA address is the program's CPU address, as in CP/M 3 |
| 52 | F_DMAGET | return the DMA segment and address |
| 53 | MC_MAX | the largest free run of memory, in an MCB |
| 54 | MC_ABSMAX | the same, at a given base |
| 55 | MC_ALLOC | allocate memory: the MCB's length in, its base out |
| 56 | MC_ABSALLOC | allocate memory at a given base |
| 57 | MC_FREE | free one allocation |
| 58 | MC_ALLFREE | free everything the program allocated |

- **The MCB** is CP/M-86's: base (a paragraph, 2 bytes), length (paragraphs, 2 bytes), and an extra byte.
- **Granularity is a 16 KB block,** 1024 paragraphs: lengths round up, bases are block boundaries. Only the blocks
  that are user memory (banks 1–5 and what's left of the others) are handed out.
- **Warm boot frees** everything a program allocated, as 58 does, so a program that stops early leaks nothing.
- MP/M's memory functions (128–130) were the alternative. They describe memory segments of a multi-process
  system, and fit less well.

### OODOS's own ###

From 72, a range no Digital Research system uses (CP/Net has 64–71).

| No. | What |
|---|---|
| 72 | OODOS's version, the BIOS's version, the memory fitted and free, and the address of the far call table |
| 73 | translate a CPU address in the program's map to a physical address (the paragraph and the offset), for programs that program the DMA controller themselves |
| 74 | mount an SD image as a drive, or unmount one: a drive and a path, as MOUNT does |
| 75–97 | reserved for OODOS |

### Far calls ###

**Proposed.** Calling code in an allocated block can't wait for a BDOS call; GW-BASIC makes thousands a second. So
common memory holds a small table, found through function 72, beside the BIOS's jump table:

- **FARCALL:** map a block into W3's code window (the program names the block and an address in `$C800–$F7FF`),
  JSR there, and put W3 back on the way out. The block's code may call FARCALL again; the table keeps a stack of
  the blocks. The program's own memory, `$0000–$BFFF`, stays mapped throughout, so banked code sees all of it.
- A block allocated for code gets common memory copied into its top 2 KB, so 12 KB of each is the program's.
- **FARPEEK, FARPOKE, FARMOVE:** bytes and runs of bytes at physical addresses, for DEF SEG, PEEK, POKE, BLOAD and
  BSAVE. FARMOVE uses the DMA controller.


## BIOS ##

**Proposed.** The 6502 BIOS has CP/M 3's entry points in a jump table in common memory: BOOT, WBOOT, CONST,
CONIN, CONOUT, LIST, AUXOUT, AUXIN, HOME, SELDSK, SETTRK, SETSEC, SETDMA, READ, WRITE, LISTST, SECTRN,
CONOST, AUXIST, AUXOST, DEVTBL, DEVINI, DRVTBL, MULTIO, FLUSH, MOVE, TIME, SELMEM, SETBNK, XMOVE.

- **SD drives:** LBA = track × SPT + sector; a READ programs DMA channel 0 to the buffer's physical address and
  issues the SD card's READ. About a hundred lines.
- **Console:** the video card's 80×30 text screen and the USB keyboard. AUX is COM1 (or COM2), LST the parallel
  port. CP/M 3's device table lets DEVICE move them.
- **TIME:** reads and sets the DS3231.

**Proposed (phase 2)**

- **Drives A–G** are SD units 1–7 as mounted: 16,384 sectors is `oodos-8m`, 65,536 `oodos-32m`. A read-only
  unit's WRITE returns 2, as CP/M 3's write-protect error. SELDSK gives a DPH with the DPB and no translation;
  its buffers (CSV, ALV, BCBs, HASH) come with the BDOS in phase 3.
- **M:** `oodos-ram` in banks C–D (`$C0000–$DFFFF`), READ and WRITE by DMA memory-to-memory copies. Cold boot
  formats it as `mkfs.cpm -t` would, with the label's mode 0x31.
- **SETBNK** names whose memory SETDMA's address is in: 0 the system's view, 1 the program's (through the windows
  the BDOS stub saved). The BIOS turns either into a physical address for the DMA.
- **The devices:** CRT (the text screen and the USB keyboard), TTY (the serial card, the 6551 for now) and LPT
  (nothing yet: no parallel port in the emulator; it takes and drops). CON: is CRT and TTY, in and out, AUX: is
  TTY, LST: is LPT.
- **The terminal** is a VT52 subset: CR, LF, BS, BEL, `ESC A B C D` (cursor), `ESC H` (home), `ESC J` and `ESC K`
  (erase to the end of the screen and of the line), `ESC E` (clear) and `ESC Y` row column (each + 32).
  Lowercase stays lowercase.
- **TIME** with A = 0 reads the bridge into the SCB's date (days from 1 January 1978, day 1 that day), hour,
  minute and second (BCD); with A = `$FF` it sets the bridge from them, the day of the week worked out.
- **Interrupts:** none enabled in phase 2; the console is polled. BRK, and Break (NMI), go to the ROM's monitor
  as BIOS 0.9's Break does (W3 and VEC back to block 63). Any other IRQ returns.
- **Apple drives:** below.


## Real-time clock ##

**Decided: a DS3231 on the CPU card.** Date stamps need the time.

**Decided (2026-10-26): the north bridge owns it** ([design.md](design.md#real-time-clock)).

- The bridge keeps the time in registers, `$C690–$C696`, in the DS3231's BCD; reading the seconds latches them all.
  TIME is a handful of loads, and setting the time is seven stores and SET. No I²C in the BIOS.
- A CR2032 keeps it running with the power off.
- RTCCTL's TICK, once a second, can be an interrupt (bit 5 of IRQH, ID `0xD`) for a seconds tick; alarms set it too.
- The emulator counts it in emulated time, from the host's clock at power-on; the WebUI steps it back into line
  with the host's clock when it falls 2 s out (design.md).


## Loading ##

**Proposed**

1. The BIOS's `C80CR` boots `/BOOT.BIN` from the FAT card, as now. For OODOS that's **OOLDR**, a loader in the
   spirit of CPMLDR.
2. OOLDR mounts the drives from `/OODOS/DRIVES.TXT` and reads **`OODOS.SYS`** from drive A's own CP/M file system,
   with a small read-only BDOS of its own.
3. It puts the banked BDOS, the BIOS and common memory in their blocks, sets the windows and VEC, and starts the
   BDOS's cold boot, which runs the CCP.

A machine with no OODOS card still boots to the monitor as now.


## The system, phase 2 ##

**Decided (2026-10-27)**

- **Until phase 3's BDOS, OODOS.SYS has a stand-in BDOS and CCP:** the console, version, time and direct BIOS
  functions and 72, and an `A>` prompt that runs `NAME.COM` from drive A, user 0. Phase 3 replaces both.
- **Common memory has the full FARCALL** (the spike's), and the BDOS stub saves and restores W3 as well as W0–W2,
  so a home block stays possible ([Open Questions](#open-questions)).
- **M:, the RAM disk, is the BIOS's from phase 2.**

**Proposed (phase 2)**

**The windows.** A program's memory, `$0000–$BFFF`, is blocks 0–2; the system's is block 31.

| | A program running | In the BDOS or the BIOS |
|---|---|---|
| W0 `$0000–$3FFF` | block 0: its zero page, stack, base page, TPA | block 31: the system's zero page, stack, data and the SCB |
| W1 `$4000–$7FFF` | block 1 | block 60: the BDOS |
| W2 `$8000–$BFFF` | block 2 | block 61: the BIOS |
| W3 `$C000–$FFFF` | block 62, or a far call's block | block 62: OODOS's resident code and common memory |
| VEC | block 62 | block 62 |

- **The BDOS's entry** is a `JMP` at `$BFFD`, the last three bytes of the TPA, to the stub in common memory, and
  `$0203` jumps there: so `$0204` is `$BFFD`, the top of the TPA + 1, as CP/M's address 6 is. RSXs load below it.
  The TPA is `$0300–$BFFC`, 48,381 bytes. (A `JMP $F8xx` at `$0203` would make `$0204` claim the I/O hole and
  the code window.)
- **The BDOS stub** saves the program's S, P and WIN0–WIN3, switches to the system's windows and stack, calls the
  BDOS, and puts them back. With interrupts off while it switches; its scratch bytes are in common memory, used
  only between the entry and the switch.
- **The program's memory from the system:** through the saved windows, as physical addresses, by DMA
  (MOVE/XMOVE, or a byte at a time where that's all it is).
- **Programs call the BIOS through function 50 only.** The jump table in common memory is the BDOS's.

**Common memory** (`$F800–$FFFF`, the same in every block W3 maps) holds:

| Address | What |
|---|---|
| `$F800` | the BIOS jump table: CP/M 3's 30 entries, in CP/M 3's order, 3 bytes each |
| after it | the BDOS stub, the warm boot entry, FARCALL, the interrupt and NMI entries, the stub's scratch |
| `$FFFA–$FFFF` | the NMI, reset and IRQ vectors (VEC is block 62) |

The addresses after the jump table aren't fixed: programs find FARCALL through function 72.

**OODOS.SYS** is a file on drive A, user 0. Its first 128-byte record is a header:

| Offset | What |
|---|---|
| +0 | the magic number `4F 4F 53 59` ("OOSY") |
| +4 | OODOS's version, major then minor. OOLDR refuses a major version it doesn't know |
| +6 | the cold boot's entry, a CPU address in block 62 |
| +8 | the number of segments, 1–8 |
| +9–+15 | reserved, 0 |
| +16 | 8 bytes a segment: its physical address (3 bytes), its length in bytes (2, at most 16 KB), the record of the file it starts at (2), reserved (1) |

Each segment's bytes start on a record. A segment must lie inside block 31 or blocks 60–62 and not overlap
another, or OOLDR refuses the file.

**OOLDR** is `/BOOT.BIN`, loaded at `$1000` by the BIOS's `C80CR`:

1. It reads `/OODOS/DRIVES.TXT` (its size from LIST of `/OODOS`, its bytes through a unit mounted for the
   purpose) and checks it all by the grammar above before mounting anything.
2. It mounts each drive line's image as its unit (A is 1, so `/BOOT.BIN` is replaced, being in memory). An
   image must be 16,384 sectors (`oodos-8m`) or 65,536 (`oodos-32m`); any other size, or a path MOUNT can't
   find, stops it with the line's number.
3. It finds OODOS.SYS in drive A's directory, user 0, reads it by its extents, checks the header and puts each
   segment at its physical address.
4. It sets W1, W2, W3 and VEC to blocks 60, 61, 62 and 62 (W0 is still block 0), interrupts off, decimal off,
   and jumps to the cold boot's entry.

Any failure says what, and returns to the monitor. The BIOS learns the drives from the card itself: IDENTIFY's
units table gives each unit's sectors and read-only flag, so nothing is handed over.

**Proposed (phase 2): OOLDR's details,** as [`oodos/ldr/ooldr.s`](../oodos/ldr/ooldr.s) has them:

- **The CP/M reader** is [`oodos/sys/cpmread.s`](../oodos/sys/cpmread.s), linked into OOLDR and, later, the stand-in
  CCP: CR_INIT (a unit; its size picks `oodos-8m` or `oodos-32m`), CR_FIND (an 8 + 3 name, user 0), CR_SEEK (a
  record: OODOS.SYS's segments start where the header says, not in order) and CR_READ (the next record to a CPU
  address). Carry and a code in A on failure, the end of the file told apart. It reads through DATA by CPU loop,
  never DMA, and has no fixed addresses: its zero page and buffers are segments (`CRZP`, 4 bytes; `CRBSS`, about
  1 KB) the linker config places. A file may have 16 directory entries (512 KB on `oodos-8m`), each but the last
  full; an entry missing, twice, part full or naming a block outside the data area is a damaged directory.
- **DRIVES.TXT:** a CR at the very end, with no LF after it, is a CR not before LF. The drives are mounted in order,
  A to G, each with its size checked as it's mounted. Units 2–7 that no line names are unmounted first, so a unit
  left from an earlier boot doesn't become a drive.
- **The header:** the entry must be in `$C800–$FFFF` (block 62 through W3, past the I/O hole); each segment must
  also lie inside the file (its record and length within the file's records); a segment may run from one of blocks
  60–62 into the next; the reserved bytes aren't looked at. Everything is checked before anything is loaded.
- **Loading** maps W1 to a segment's first block and W2 to the next, and copies each record through a buffer, so a
  last part record writes only the segment's bytes. On a failure W1 and W2 go back to blocks 1 and 2.

**The stand-in BDOS** (block 60) has functions 0, 1, 2, 6, 9, 10, 11, 12, 25, 50, 72, 104 and 105, as CP/M 3's
(and 72 as above); any other returns A = X = `$FF`. **The stand-in CCP** prompts `A>`, reads a line (function
10), and runs `NAME.COM` from drive A, user 0, through a read-only reader shared with OOLDR: it checks the
program header's magic, loads the image at `$0300`, sets the base page and jumps there with the program's
windows and a fresh stack. An unknown name prints `NAME?`, as CP/M does. Warm boot comes back to the prompt.


## RSXs and programs ##

**Decided (2026-10-26): CP/M 3's split. `.COM` files load at a fixed address; `.PRL` files are relocatable.**

- **`.COM`:** a short header, then code linked to run at **`$0300`**. That address and the base page's layout are
  permanent. Most programs are these: no relocation step to build, nothing to do at load.
- **`.PRL`:** the same header, then code that relocates by page: a bitmap marks every byte that is the high byte
  of an address, and a second bitmap marks zero page operands. The bitmaps are made by linking twice with ld65, a
  page apart (and a zero page apart), and comparing, as Digital Research's tools did. RSXs are `.PRL`s, and so is
  anything that must load elsewhere: a debugger that moves itself up out of the way, overlays (function 59).
- **The header:** a magic number, a flag for relocatable, the code size, the zero page an RSX needs, any attached
  RSXs. The loader checks the magic number first and refuses a file without it, so an 8080 `.COM` copied over by
  mistake can't run.
- **RSXs** load below the BDOS and chain on `$0203` exactly as CP/M 3's do, each with CP/M 3's RSX prefix (serial
  number, the next entry, the previous entry, the remove flag, the non-banked flag, the name), and a slice of
  `$F0–$FF`.
- **GENCOM** attaches RSXs to a program, as in CP/M 3. GET, PUT and SUBMIT are RSXs.

### The program header ###

**Decided (2026-10-26).** 16 bytes at the start of every `.COM` and `.PRL`, in the file only: the image after it is what
goes into memory. `OODOS_HEADER` in [`oodos/include/oodos.inc`](../oodos/include/oodos.inc) writes it, with the
values from the link.

| Offset | Size | What |
|---|---|---|
| 0 | 4 | the magic number, `$4F $4F $44 $1A`: "OOD", then ^Z, so TYPE shows OOD and stops |
| 4 | 1 | flags: bit 0 relocatable (a `.PRL`, bitmaps follow), bit 1 an RSX (the image starts with the RSX prefix), bit 7 breaks the 65C816 rules (OODOS's tools never set it); the rest 0, and a file with any of them set is refused |
| 5 | 1 | the link page: the image is linked to run at this page × 256. `$03` for a `.COM`, and the only page it loads at |
| 6 | 2 | the image's size, in bytes |
| 8 | 2 | BSS: bytes the program needs above its image, not in the file and not cleared |
| 10 | 1 | the zero page base: where the image's own zero page segment is linked |
| 11 | 1 | the zero page size: that segment's bytes; at most `$F0` for a program, 16 for an RSX (the slice it needs from `$F0–$FF`), 0 for an overlay |
| 12 | 1 | the number of attached RSXs (GENCOM), 0 for none |
| 13 | 1 | reserved, 0 |
| 14 | 2 | the record (128 bytes, from the file's start) of the attached RSX table, 0 for none |

- **The loader** reads the first record into the DMA buffer and refuses the file if bytes 0–3 aren't the magic
  number or a flag bit it doesn't know is set. It refuses a `.COM` whose link page isn't `$03`, a `.PRL` at a page
  below `$03`, an image and BSS that don't fit below the top of the TPA (`$0204`), and a zero page that doesn't
  fit (below). Then it reads **exactly the image's size bytes** of image: the first record's 112 bytes after the
  header, or as many as the image has if it's smaller, are moved to the load address; the following whole records
  can be read straight to their place; and a last part-record goes through the DMA buffer, only the image's bytes
  of it moved. Nothing after the image (a `.PRL`'s bitmaps, the attached RSX table, the attached RSXs) is ever
  read into the image or past it: BSS is not cleared, and memory above the image is left as it was.
- **Sizes are exact.** CP/M files are whole records, so the end of the last one is padding, ignored.
- **The attached RSX table** (GENCOM, phase 5) is a record-aligned run of 16-byte entries after the program: the
  record the RSX's own `.PRL` starts at (2 bytes), a flags byte (bit 0: load only in a non-banked system, as CP/M
  3's), the name (8 bytes, space padded), 5 reserved. Each attached RSX is a whole `.PRL`, header and all, starting
  on a record.
- **The RSX prefix** keeps CP/M 3's offsets, in the RSX's image: 0 the serial number (6 bytes), 6 `JMP` to the
  RSX's entry, 9 `JMP` to the next RSX or the BDOS, 12 the previous entry's address, 14 the remove flag, 15 the
  non-banked flag, 16 the name (8 bytes), 24 the loader's flag, 25–26 reserved. A 6502 `JMP` is 3 bytes as an
  8080's is, so every offset holds.

### The .PRL file ###

**Decided (2026-10-26).** The header (flag bit 0 set), the image as linked at the header's page and zero page
base, then two bitmaps of a bit for each byte of the image, the first byte in bit 7 of the bitmap's first byte.
Each bitmap starts on a record, so a record of it covers 1,024 bytes of the image:

| Record | Size | What |
|---|---|---|
| 0 | 16 | the header, as above |
| 0 | size | the image, from byte 16: linked at the link page, its zero page segment at the zero page base; zeros to the end of its last record |
| p = ⌈(16 + size) / 128⌉ | (size + 7) / 8 | the page bitmap. 1: the byte is the high byte of an address in the image. Zeros to the end of its last record |
| p + ⌈((size + 7) / 8) / 128⌉ | (size + 7) / 8 | the zero page bitmap. 1: the byte is the address of a byte in the image's zero page segment. Zeros to the end of its last record |

The attached RSX table, if any, follows on a later record (its record is in the header).

- **Loading at page P with zero page base Z:** the image is read as above, then the page bitmap is read a
  record at a time through the DMA buffer, and P − the link page added to every byte of the image it marks; then
  the zero page bitmap the same way, adding Z − the zero page base; both modulo 256. Bits past the image's last
  byte are padding, ignored. A byte is in one bitmap or neither.
- **Zero page.** A `.PRL` program (not an RSX) is given Z = `$00`, its own zero page, and the loader refuses one
  with Z + its zero page size past `$F0`. An RSX is given a slice of `$F0–$FF`, and refused if its zero page is
  more than what's left of it. An overlay (function 59) has none of its own: the loader refuses one whose zero
  page size isn't 0. It uses its caller's zero page by importing the caller's symbols, which don't move, as a
  program's zero page is always at its link address. An overlay's load address must be a page's start.
- **The zero page is BSS only:** no initialized data in it (ld65 drops it with only a warning, and OODOS's build
  fails on any ld65 warning). In OODOS's link configs it ends at `$EF` (`$F0` − the zero page base), so a `.PRL`
  linked at zero page base 5, as the third link is, has at most `$EB` bytes of it.
- **Made with [`oodos/tools/mkprl.py`](../oodos/tools/mkprl.py)** from three links of the same objects: the build's
  are page 3 with the zero page at `$00`, page 4 at `$02` and page 5 at `$05`. Every byte must move in the second
  and third links as one of three classes: not at all; 1 and 2 (the high byte of an address in the image); or the
  zero page offsets, 2 and 5 (a zero page address). Anything else is refused, with the offset, so a byte that is
  some other function of an address (`>(label*2)` moves by 2 and 4) is refused rather than taken for a
  relocation. The zero page bases may be any offsets but ones that move as nothing does, or as a page does.
- **No 16-bit pointers to zero page,** rule 2 below: mkprl.py refuses a zero page address that isn't the operand
  of a zero page or immediate instruction in `CODE` (it disassembles `CODE` from the first link's map), so a
  `.word ptr` or `.byte ptr` in data, or an absolute operand, can't relocate quietly. That covers pointers to the
  program's own `ZEROPAGE` segment only, the addresses that move between the links: pointers to an overlay's
  zero page (imported from its caller, so it doesn't move) and to absolute zero page addresses, in `CODE` or the
  data segments, rely on the author. mkprl.py refuses a zero page size over `$F0` for a program, and over 16 for
  an RSX.

### Rules for programs: the 65C816 ###

**Decided (2026-10-26).** A later machine may have a 65C816, whose direct page (zero page) and stack can be put
anywhere. An OODOS program that keeps these rules would run on it unchanged, in emulation mode, with its own
direct page; OODOS's build tools and examples keep them, and the loader may later refuse a program marked as
breaking them.

1. **No `RMB`, `SMB`, `BBR` or `BBS`.** They are the 65C02's only; on the 65C816 the same opcodes are other
   instructions.
2. **Zero page variables only through zero page operands:** no absolute-mode access to them (`LDA $0012`), and no
   16-bit pointers to them. Both break once the direct page moves.
3. **The stack only through the stack instructions:** no reading it in page 1 (`TSX`, then `LDA $0103,X`).
4. **The BDOS only through `$0203`,** with the registers as above.

OODOS's build checks them: `oodos/tools/rules.py` disassembles `CODE` (only `CODE`: data in other segments isn't
looked at) of the `.COM` and of the `.PRL`'s first link, for rules 1, 3, 4 and rule 2's absolute operands, and
mkprl.py finds rule 2's 16-bit pointers to zero page in any segment, by the zero page bitmap ([above](#the-prl-file)),
but only those to the program's own `ZEROPAGE` segment: pointers to an overlay's imported zero page or to absolute
zero page addresses, in `CODE` or the data segments, rely on the author.

## Apple II disks ##

**Decided (2026-10-26): two jobs, in this order.** The details are proposed.

1. **First, Apple CP/M disks as OODOS drives,** in cpmtools's `apple-do` format (the SoftCard's):
   - **as images on the SD card**, `.dsk` files mounted as units: two 256-byte sectors to an SD sector;
   - **in the real drive,** through the Disk II card in native mode: a 6502 driver for the address and data fields
     (6-and-2 GCR), stepping with the phases, and writing with the 224-cycle loop in design.md, with interrupts
     and DMA off while it writes. The motor goes off a second after the last access, timed by the RTC's tick.
2. **Then DOS 3.3 disks:** an exchange utility, like the SoftCard's APDOS, that lists such a disk and copies
   files between it and an OODOS drive. Not a drive format. ProDOS disks could follow in the same utility; that
   isn't decided.

**First, in the emulator:** the Disk II card in native mode, which it doesn't do yet (its backlog). The driver is
tested against it with our own disk images.


## GW-BASIC ##

**Decided: GW-BASIC, from [microsoft/GW-BASIC](https://github.com/microsoft/GW-BASIC)** (MIT).

**What the source is.** 34,474 lines of 8088 assembly in 35 files, dated February 1983, with no build files.
Microsoft machine-translated it from their 8080 source ("translation created 12-Jan-83"), so it still works in 8080
register pairs (HL in BX, DE in DX, BC in CX). That maps onto 6502 zero page pseudo-registers more naturally than
true 8086 code would.

**What fits.**

- **Files:** it uses MS-DOS 1's FCB calls (functions 5, 6, 13–17, 19, 20, 22, 23, 25, 26, 33, 34), which are
  the CP/M BDOS's numbers, because DOS 1 copied them. Only 39 (random block read) and 41 (parse filename) differ;
  they become CP/M 3's multi-sector I/O and function 152.
- **DEF SEG:** an 8086 segment and offset make a 20-bit address, as the OO-1's physical addresses are. PEEK,
  POKE, BLOAD and BSAVE with DEF SEG reach the whole 1 MB, video memory included, through a DMA copy or a spare
  window.

**What we write.** The machine-specific layer isn't in the release: about 217 symbols are used and never defined,
many of them the OEM's routines for the screen, graphics, keyboard, sound, light pen, joystick and serial ports
(SCROUT, SETC, READC, MAPXYC, KEYINP, DONOTE, RDSTIK, INICOM and others). We'd write those for the OO-1 anyway:
the video card's text and bitmap modes for SCREEN, the SIDs for SOUND and PLAY, the USB keyboard, the game port,
COM1/COM2 and the parallel port.

**Size.** The released source is estimated at about 40 KB of 8086 code (40,035 bytes, 39.1 KB) and 4,412 bytes
(4.3 KB) of tables, and 6502 code will be larger. It won't fit in the TPA beside a useful workspace, so the cold
parts (graphics, PLAY, RENUM, the editor) live in allocated blocks and are called through common memory (FARCALL),
12 KB at a time in W3's code window. The program and its variables have the TPA, and as much of the interpreter as
the 32 KB leaves room for.
**Decided (2026-10-26): at least 32 KB free for BASIC.** That's a minimum, not a target.

**Measured (phase 0, 2026-10-26): it fits, just.** The spike ([`oodos/spike/gwbasic/`](../oodos/spike/gwbasic/))
hand-translated about 2,530 of the 8086 instructions (parts of MATH1 and MATH2, the statement loop, CHRGET, GOTO,
IF, LET, the formula evaluator) to 6502, linked as a `.COM` at `$0300`, and checked them on the emulator's W65C02S
core.

- **The ratio:** 6,637 bytes of 6502 for 5,551 of 8086, 1.20. Math is 1.16, interpreter code 1.27, text
  scanning the worst at 1.36.
- **The whole:** 48.9 KB of 6502 (51.9 KB at the worst ratio), plus 4.3 KB of tables, plus the OO-1's OEM layer
  (estimated 12–20 KB): 65–73 KB.
- **The TPA:** a resident core of about 10 KB (the statement loop, LET, IF, GOTO, GOSUB, FOR and NEXT, the
  evaluator, variables, the string core and functions, INKEY$, single and integer math, console output) and 4 KB of
  fixed RAM leave **34,213 bytes free**; 32,599 in the pessimistic case, 169 bytes short, closed by keeping the file
  and device buffers out of the TPA. The rest goes in 7 or 8 banked blocks of 12 KB: math with PRINT and INPUT,
  program entry and LIST, the other statements, files, graphics and devices, the screen editor, the OO-1's drivers.
- **No far calls on the core statement path.** A FARCALL costs 281 cycles over a JSR; one pass of
  `I%=I%+1 : IF I%<1000 THEN …` is about 4,700 and printing a single 16,000–20,000. A loop of LET, IF, GOTO,
  FOR/NEXT, single and integer arithmetic, string functions and INKEY$ stays resident; PRINT and INPUT (once a
  statement), double precision, banked functions (SIN, VAL...), graphics (PSET and LINE once a point), I/O
  statements and typed lines make a far call each time.
- **Correct:** 93,289 math vectors bit for bit against a Python model written from the 8086 source (`mbf.py`),
  and seven BASIC programs; the translated segments pass the 65C816 rules ([`oodos/tools/rules.py`](../oodos/tools/rules.py)). One finding for phase 7 to
  check against PC-BASIC: the 1983 source prints 1/3 as `.3333334`.

For phase 7: keep the BASIC stack in the TPA, zero page pseudo-registers without the 8086's push and swap glue,
dispatch tables that carry the block, and a build that gates the resident size with `sizes.py`.

**Testing.** PC-BASIC, an independent reimplementation that aims to match GW-BASIC, is the oracle for what
programs print and how numbers are formatted. It runs only in a throwaway container. The port keeps Microsoft's
copyright and MIT notice.


## Testing ##

**Done (phase 1, 2026-10-26)** ([oodos/README.md](../oodos/README.md) has the commands)

- **The build:** ca65 and ld65, as the BIOS. [`oodos/Makefile`](../oodos/Makefile) links each program as a `.COM`
  and, three times, a `.PRL`; the `CODE` of the `.COM` and of the `.PRL`'s first link passes the 65C816 rules
  (`oodos/tools/rules.py`), mkprl.py's checks pass, and ld65 gives no warning, or the build fails. `make check`
  tests the tools: relocated `.PRL`s against direct links, the loader's steps (only the image's bytes change in
  memory full of sentinels, with images under 112 bytes, over 1,024, and RSXs attached) and refusals, mkprl's
  refusals, the magic number, and the rules gate on planted violations; and runs hello on the emulator's W65C02S.
  The test card's `/BOOT.BIN` is built with ca65 and ld65 too.
- **Card images:** [`oodos/tools/make_card.sh`](../oodos/tools/make_card.sh) makes the test card with cpmtools,
  mkfs.fat and mtools in a throwaway container, from our own files, under faketime so the same sources make the
  same card. It's committed as a sparse image, `emulator/test/host/sd/oodos.sparse` (184 KB): a 16 MB card,
  FAT16 in an MBR partition, with `/BOOT.BIN`, `/OODOS/DRIVES.TXT` and `/OODOS/A.IMG` (`oodos-8m`, label mode
  0x31, files in users 0 and 3, one over two directory entries).
- **Interoperability, both ways:**
  - **cpmtools writes, the OO-1 reads:** `test_oodos_card`, in ctest, mounts A.IMG through the card's own MOUNT
    (after reading DRIVES.TXT by its grammar, as OOLDR will) and finds every entry, SFCB stamp and record in the
    n-th sector of its block b, at LBA = b × (BLS / 512) + n (the unit's sectors are 512 bytes, the drive has no
    boot tracks), by the DPB worked out from `oodos/diskdefs`.
  - **The OO-1 writes, cpmtools checks:** in the same test a stand-in for the BDOS writes a file through the card's
    WRITE and saves the card; [`oodos/test/interop.sh`](../oodos/test/interop.sh) takes A.IMG off it with mtools and
    checks it with `fsck.cpm -n`, `cpmls -F` (both stamps), the file's raw SFCB slots, and `cpmcp`. It checks the
    committed card the same way. A wrong allocation or misplaced stamps, made on purpose (in an SFCB of mkfs.cpm's
    `$E5`s or of zeros), fail both.
  - **fsck.cpm by its report:** it exits 0 after some errors, a bad stamp for one, so `check_diskdefs.sh` and
    `interop.sh` fail on any Error or Warning line it prints (`oodos/test/cpmtools.sh`). One exception, in
    `interop.sh` only: **cpmtools 2.23's fsck.cpm compares the stamps in an SFCB's first slot backwards.** For a
    file entry E with E mod 4 = 0 it warns "Modification date earlier than creation date (extent=S/E)" when the
    update is on a later day than the creation, and says nothing when it's earlier; the other slots are compared
    right. So `oodos/test/sfcb.py` reads every file entry's stamps itself and fails one updated before it was
    made, and only then is that warning let through, for exactly the first-slot entries it found a day or more
    apart and in order. The OO-1's test file is made a day before it's updated, so the case is always exercised.
  - **Decided (2026-10-26): ctest stays hermetic,** with no skips, on committed images; the checks that need
    Docker are scripts like `check_diskdefs.sh` and `interop.sh`, run by hand and at each phase's verify.
  - Phase 3 puts the BDOS where the stand-in is, running in the emulator; the checks on both sides stay.

**Done (phase 2: the loader, 2026-10-27)** ([oodos/README.md](../oodos/README.md) has the commands)

- **OOLDR and the CP/M reader:** [`oodos/ldr/`](../oodos/ldr/) builds `/BOOT.BIN` from `ooldr.s` and
  `oodos/sys/cpmread.s`, and a test OODOS.SYS from `testsys.s`: six segments in block 31 and blocks 60–62, one of
  them over the first directory entry's end (record 256) and one over a logical extent's (record 128), and two
  touching, the earlier loaded later, so a last part record written whole shows. Its cold boot writes the windows
  and P it found to block 31 and stops. ld65 writes the header's records from the file offsets it gives.
- **The card:** `make_card.sh` builds both in its container and puts them on the card, which is now 32 MB:
  `/BOOT.BIN` is OOLDR, A.IMG holds OODOS.SYS beside phase 1's files, and `/OODOS/B.IMG` is an empty `oodos-8m`
  drive, label mode 0x31; DRIVES.TXT names A: and B:. `SYS=` chooses what goes on A: as OODOS.SYS. The sparse
  image is 351 KB.
- **`test_oodos_boot`, in ctest,** boots the committed card through C80CR in a Session and checks every segment
  byte for byte at its physical address, the bytes either side as they were, the entry (W0 still block 0, W1–W3
  and VEC 60, 61, 62, 62, I set, D clear), and units 1 and 2 mounted as A.IMG and B.IMG with their flags. Then every
  refusal, each with its message, and DRIVES.TXT's with the line's number: each form the grammar refuses (a word
  after the path, `R0`, `ROM` or a second `RO`; a space in the path; a drive named twice; the same path in another
  case, either way round; a CR not before LF, in a comment too and at the end; a drive outside A–G, no blank after
  the colon, a path without `/`, a control character in it, one over 509 bytes), the forms it allows (a BOM, CR LF, blank and comment
  lines, tabs, lower case, `RO` before a comment, a last line with no ending), no A line, a path not on the card,
  an image of the wrong size, no DRIVES.TXT, and a unit left mounted unmounted; OODOS.SYS missing from user 0, a
  bad magic number, an unknown major version, 0 or 9 segments, an entry outside block 62, a segment over 16 KB,
  outside block 31 and blocks 60–62 (below, past and between them, into block 63, past 1 MB), overlapping by a
  byte or wholly, or past the file's end; the reader's damaged directories (an entry missing, twice, part full, an
  RC over 128, an EX over 31, a block of the directory's or past DSM), too many entries and an empty file; and
  copies of OODOS.SYS's entry where the reader mustn't look (deleted, user 3, an XFCB, the label's and an SFCB's
  first bytes). An `oodos-32m` drive, its two entries last extent first, boots too. Those cards are made in the
  test, FAT16 on the whole card, from the committed card's files; nothing more is committed.
- **Each check can fail:** OOLDR built with a deliberate bug and spliced into the card fails the test, one bug at a
  time: every last record written whole, W2 on block 62, CLI, SED, entries of any user read, a CR in a comment
  let by, a CR at the end let by, a drive named twice let by, paths compared in one case only, no overlap check,
  any image size taken, `oodos-32m` with `oodos-8m`'s EXM, block 63 allowed, a segment past the file's end let by,
  the BOM not skipped, nothing unmounted, segments over 16 KB let by, part full entries let by.
- `test_oodos_card` follows the card (its size, the directory's entries, OO1.BIN's two entries now 10 and 12) and
  `interop.sh` passes on it unchanged.

**Proposed**

- **The emulator** runs OODOS in native tests (BDOS functions, the CCP, RSXs, the Apple driver) and in the WebUI's
  e2e tests.
- Nothing of Digital Research's, or any other copyrighted software, goes into the tests or onto the website.


## Phases ##

Each phase is its own branch.

0. **This document,** its open questions settled, the `diskdefs` file, and the GW-BASIC size spike.
1. **Tooling:** the build, card images, the interoperability tests, the DS3231 in the emulator.
2. **OOLDR and the BIOS:** common memory, the banking stub, the SD drives, the console, TIME.
3. **The BDOS:** the file system, deblocking, hashing, multi-sector I/O, the SCB, date stamps, labels and
   passwords.
4. **The CCP and utilities:** the built-in commands; DIR, TYPE, ERA, REN, PIP, SUBMIT, DATE, INITDIR, SET, SHOW,
   DEVICE.
5. **RSXs:** the loader, chaining, GENCOM; GET, PUT and SUBMIT as RSXs.
6. **Apple disks:** native Disk II in the emulator, the driver, `apple-do` drives; then the DOS 3.3
   exchange utility.
7. **GW-BASIC.**
8. **The website:** an OODOS card in the WebUI with OODOS, GW-BASIC and demo programs, all ours or MIT.


## Open Questions ##

- **A home block for GW-BASIC.** Letting a program keep one block mapped in W3 between far calls, and FARCALL
  return to it rather than to nothing, would move 4–6 KB of GW-BASIC out of the TPA. It changes FARCALL's design
  above; for phase 2, not needed for the 32 KB.

  What it would cost (estimates from the spike's measured figures; the home block itself isn't built or timed):
  - **Unchanged:** the core statement path stays resident in the TPA with no far calls (`I%=I%+1 : IF I%<1000
    THEN 10` stays about 4,700 cycles a pass), and FARCALL already saves and restores whatever W3 holds, so it
    stays 281 cycles.
  - **Faster:** PRINT and INPUT in the home block make no far call: 281 cycles a statement, 1.4–1.8% of printing
    a number.
  - **Slower:** with GW-BASIC's block in W3, every BDOS entry and every interrupt handler in OODOS's block must
    map that block in and back out (save, store, restore the window register): about 20–30 cycles each. Console
    output is a BDOS call a character, so printing a number gains roughly 1–2% back, about cancelling the
    PRINT saving. Interrupts at 60 Hz are about 0.02% of the CPU.
  - **The hazard:** while a banked page is in W3 the home block is not, so a call from banked code into home code
    is a far call. Single math in the home block would cost `$FOUT` (banked) 281 cycles per multiply or divide,
    about 15% a number printed at ten of them. So the home block may hold only code the banked pages don't call;
    what they share (the math core, the string core, variables) stays in the TPA. The 4–6 KB must be counted again
    under that rule.

  For phase 2: prototype it in the spike harness and time, with `check.py --timing`, the BDOS map-in stub, a
  PRINT through the home block and `$FOUT` under the placement rule.
- **A lean FARCALL**, keeping only A and the flags across the call, would save an estimated 100–140 of its 281
  cycles (not measured: reading the inline bytes, the return frame and the two window writes stay). Under 1% on
  PRINT, more on a graphics loop's far call a point, nothing on the core statement path. Against it:
  - X and Y no longer reach the target or come back. The GW-BASIC port names numbers in zero page by X, so a
    banked math entry taking one would need X passed through zero page: a second convention at the bank
    boundary, and some of the saving back.
  - FARCALL is OODOS's interface for every program (function 72), not GW-BASIC's. cc65's C passes and returns
    16-bit values in A/X, so a banked C function would lose the high byte. Two variants cost common memory.
  - A routine that works by JSR can break by FARCALL with nothing to say so; the full one's "whatever works by
    JSR works by FARCALL" is what the spike's checks test, and they found real bugs (X lost, D set).

  Leaning (2026-10-27): keep the full FARCALL as the interface; if phase 2's timing shows far calls matter
  somewhere (graphics), a lean variant private to GW-BASIC's dispatchers, whose targets take nothing in X or Y.
  Time a lean prototype with `check.py --timing` first.
