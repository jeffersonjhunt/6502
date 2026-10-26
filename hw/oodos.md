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
- **Directory hashing** is CP/M 3's, kept in memory only (the DPH's HASH table, 4 bytes an entry). It never
  touches the disk, so it costs interoperability nothing. With 1 MB it's on for every drive.
- **cpmtools.** The repo carries [`oodos/diskdefs`](../oodos/diskdefs) with OODOS's formats, so a card is made on
  Linux with, for example ([oodos/README.md](../oodos/README.md) has more):

  ```
  mkfs.cpm -f oodos-8m -t -L OODOS A.IMG
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
  writes a directory label and an SFCB in every fourth entry, `cpmcp` stamps what it writes, `cpmls -F` reads the
  stamps back, and `fsck.cpm` passes, for all three formats. The other direction, OODOS writing and cpmtools
  checking, waits for the BDOS (phase 1's interoperability tests).


## Drives ##

**Proposed**

| Drive | What |
|---|---|
| A–G | SD units 1–7: image files on the FAT card, mounted with MOUNT. A is the system drive |
| M | the RAM disk, in banks C–D (128 KB) |
| O, P | the Apple drives 1 and 2, through the Disk II card |

Which image each SD drive holds is set by a file on the card, `/OODOS/DRIVES.TXT`, read by the loader, and can be
changed while running by a utility that does MOUNT and resets the drive.


## Memory ##

### What a program sees ###

**Proposed**

| CPU range | Window | What |
|---|---|---|
| `$0000–$00FF` | W0 | zero page (below) |
| `$0100–$01FF` | W0 | the program's stack |
| `$0200–$02FF` | W0 | the **base page**: CP/M's page 0, moved up a page with the same offsets |
| `$0300`–top | W0–W2 | the TPA, about 47 KB, less any RSXs at its top |
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
| A block in bank 7 | the system's zero page, stack and data |
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
- **59** loads a `.PRL` at the FCB's random record address, relocating it; a `.COM` loads only where it was linked.
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
- **Apple drives:** below.


## Real-time clock ##

**Decided: a DS3231 on the CPU card.** Date stamps need the time.

**Proposed**

- The CPU card already holds the north bridge, so the DS3231's I²C lines go to it: a north bridge register,
  **RTC at `$C687`**, bit 0 SCL, bit 1 SDA, both open drain; reading bit 1 gives SDA. Software drives I²C by hand,
  which is plenty for reading a clock.
- A CR2032 keeps it running with the power off.
- Its SQW output, set to 1 Hz, can be an interrupt (bit 5 of IRQH, ID `0xD`) for a seconds tick.
- The emulator gives the host's time.


## Loading ##

**Proposed**

1. The BIOS's `C80CR` boots `/BOOT.BIN` from the FAT card, as now. For OODOS that's **OOLDR**, a loader in the
   spirit of CPMLDR.
2. OOLDR mounts the drives from `/OODOS/DRIVES.TXT` and reads **`OODOS.SYS`** from drive A's own CP/M file system,
   with a small read-only BDOS of its own.
3. It puts the banked BDOS, the BIOS and common memory in their blocks, sets the windows and VEC, and starts the
   BDOS's cold boot, which runs the CCP.

A machine with no OODOS card still boots to the monitor as now.


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

**Size.** GW-BASIC is about 60 KB of 8086 code, and 6502 code will be larger. It won't fit in the TPA beside a
useful workspace, so the cold parts (graphics, PLAY, RENUM, the editor) live in allocated blocks and are called
through common memory (FARCALL), 12 KB at a time in W3's code window. The program and its variables have the
TPA, and as much of the interpreter as the 32 KB leaves room for.
**Decided (2026-10-26): at least 32 KB free for BASIC.** That's a minimum, not a target. A spike settles it before the port starts: translate MATH1 and the main interpreter loop,
and measure.

**Testing.** PC-BASIC, an independent reimplementation that aims to match GW-BASIC, is the oracle for what
programs print and how numbers are formatted. It runs only in a throwaway container. The port keeps Microsoft's
copyright and MIT notice.


## Testing ##

**Proposed**

- **The build:** ca65 and ld65, as the BIOS.
- **Card images** made with cpmtools in a throwaway container, from our own files, committed as sparse images like
  `emulator/test/host/sd/`.
- **Interoperability, both ways:** cpmtools writes and OODOS reads; OODOS writes and `fsck.cpm` and `cpmls -l`
  check the result, date stamps included.
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

None at the moment.
