# 10 Storage

The mass storage card (module 7, `$C380`): an SD card behind an RP2350, its commands, FAT mounts, DMA; and the Disk
II.

The design record, for the reasons: [hw/design.md, "Storage"](../../hw/design.md#storage).

## The SD card

Status: **emulated**

The mass storage card is the native machine's own storage. An RP2350 on the card speaks the SD protocol, starts the
card up and buffers its sectors. Software sees a small set of registers in the style of an IDE disk: write a unit,
a sector number (LBA) and a count, then a command, and the sectors stream through one port, DATA. A CPU loop or DMA
channel 0 moves them.

The card has eight units:

- **Unit 0** is the whole card, sector for sector.
- **Units 1–7** are image files on the card's FAT file system, attached by the MOUNT command. Sector 0 of a unit
  is the file's first ([below](#mounting-fat-images)).

The registers are at `$C380–$C3FF`:

| Offset | Address | Name | Access | What it is |
|---|---|---|---|---|
| `$00` | [`$C380`](#reg-c380) | CTRL | R/W | DMA on (bit 0); RESET (bit 1, a strobe) |
| `$01` | [`$C381`](#reg-c381) | STATUS | R | BUSY, READY, PRESENT, WP, DATA, ERR |
| `$02` | [`$C382`](#reg-c382) | ERROR | R | why the last command failed |
| `$03` | [`$C383`](#reg-c383) | CMD | W | starts a command |
| `$04` | [`$C384`](#reg-c384) | UNIT | R/W | 0–7 |
| `$06–$09` | [`$C386`](#reg-c386) | LBA | R/W | the sector, 32 bits, low byte first |
| `$0A–$0B` | [`$C38A`](#reg-c38a) | COUNT | R/W | sectors, 0 for 65,536; counts down |
| `$0C` | [`$C38C`](#reg-c38c) | DATA | R/W | the transfer's bytes |
| `$0D–$0E` | [`$C38D`](#reg-c38d) | BUFPTR | R/W | the place in the current sector, 0–511 |
| `$10` | [`$C390`](#reg-c390) | IEN | R/W | the interrupts on |
| `$11` | [`$C391`](#reg-c391) | ISR | R; write 1 to clear | the interrupts pending |
| `$7F` | [`$C3FF`](#reg-c3ff) | ID | R | `$71` |

The other offsets (`$05`, `$0F`, `$12–$7E`) read `$FF` and ignore writes.

**Two buffers.** The card holds two 512-byte buffers. While the bus empties one (or fills it, writing), the card
fetches the next sector into the other (or programs the last one). STATUS's DATA bit is set while a whole sector is
there to read, or a whole buffer is free to write. So a loop waits for DATA once a sector, then moves 512 bytes
without looking again. While DATA is clear, a read of DATA gives `$FF` and a write to it is dropped.

**LBA and COUNT move.** As each sector passes through DATA, LBA goes up by one and COUNT down by one. During a
transfer COUNT is the sectors left; after it, LBA names the next sector, so the next sectors need only a new COUNT
and CMD.

### Starting up and reset

Status: **emulated**

- **Starting up** takes 50 ms: after power-up, after a card goes in, and after CTRL's RESET. STATUS's READY is set
  once it has. A command before then fails with error 1. PRESENT says whether there is a card in the slot at all.
- **/RESET** abandons the command in progress and clears the registers: CTRL, ERROR, UNIT, LBA, COUNT, IEN and ISR
  to 0, and ERR with them. Sectors already programmed stay written. The card stays started up, and the units stay
  mounted, so a boot menu can mount an image and reset into it. Entering Apple II mode and Ctrl-Reset pulse
  /RESET.
- **A hard reset** (the reset button, the console's START) resets the card as /RESET does, in the emulator: the
  units stay mounted here too. Only a power cycle starts the RP2350 afresh, with no units mounted and the card
  starting up again.
- **CTRL's RESET** abandons the command and starts the card up again, with READY clear for the 50 ms. It leaves the
  registers alone, ERR and ERROR included, and the units stay mounted. The abandoned command doesn't set ISR's
  bit 0.
- **Taking the card out** fails a command in progress with error 1 and unmounts every unit. Putting a card in
  unmounts every unit too, and the new card starts up.

**In Apple II mode** the card's block is hidden behind the IIe's I/O. The /RESET that enters the mode has abandoned
any command and turned the card's interrupts off. The units stay mounted for when a hard reset brings the native
machine back.

### Interrupts

Status: **emulated**

ISR ([`$C391`](#reg-c391)) has two bits. Bit 0 is set when a command ends, with or without an error, but not when
/RESET or CTRL's RESET abandons it. Bit 1 is set when a card goes in or comes out (not for a card already in the
slot at power-up). They are set whether IEN enables them or not. While a bit is set in both ISR and IEN, the card
pulls /IRQ, and bit 7 of IRQL reads 0 ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)). Writing 1s
to ISR clears those bits.

The BIOS doesn't use the card's interrupts: it polls STATUS. Its IRQ handler only returns, so a program that turns
IEN on under the monitor needs a handler of its own.

### What the BIOS leaves

Status: **emulated**

The BIOS uses the card at power-on, for `C809R` and for `C80CR` ([chapter 3](03-boot.md#booting-from-the-sd-card)).
Under the monitor the card is left this way:

- **At power-on**, the BIOS looks for `/BOOT.BIN`: it waits for READY, MOUNTs `/BOOT.BIN` as unit 1 and reads
  IDENTIFY's units table for its size. That happens before the wait for a key, so a key that keeps the monitor
  still leaves `/BOOT.BIN` mounted as unit 1. With no `/BOOT.BIN` the MOUNT has failed, so STATUS's ERR is set and
  ERROR reads 8 under the monitor. With no FAT volume it reads 9, and the BIOS says `SD ERROR 09`.
- **After an SD boot**, CTRL and UNIT are 0, COUNT is 0, LBA is the number of sectors read, and unit 1 is still
  `/BOOT.BIN`. The loaded program finds the card that way.
- **After `C809R`**, CTRL and UNIT are as they were, and COUNT is 0, the listing's sectors all taken.
- **ISR's bit 0** is set after any of these, since every command that ends sets it. The BIOS never clears it. IEN
  is 0, so it interrupts nothing.
- **Break and BRK** turn IEN off. If the card is BUSY, they write CTRL's RESET, which abandons the command without
  setting ISR's bit 0 (the card then starts up again, 50 ms); otherwise they write CTRL as 0, turning DRQ0 off.
  Either way CTRL reads 0. ERROR, ISR, UNIT, LBA, COUNT and the units are left as they were
  ([chapter 4](04-interrupts.md#pausebreak)).

OODOS's BIOS keeps the same habits: it polls, sets CTRL's DMA bit only around a transfer, puts UNIT back to 0, and
uses CTRL's RESET on a command that hasn't ended in about a second ([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)).

## Commands

Status: **emulated**

Write UNIT, LBA and COUNT as the command needs them, then the command's number to CMD ([`$C383`](#reg-c383)).
STATUS's BUSY is set from then until the command ends, when ISR's bit 0 is set. A command that fails sets ERR and
ERROR and ends at once. A command written while BUSY is dropped, with no error and no ISR bit: wait for BUSY to
clear first, or abandon the command in progress with CTRL's RESET, which sets no ISR bit either. A new command
clears ERR and ERROR.

| CMD | Name | What it does | Through DATA |
|---|---|---|---|
| `$20` | READ | reads COUNT sectors of UNIT from LBA | the sectors, out |
| `$30` | WRITE | writes COUNT sectors of UNIT from LBA | the sectors, in |
| `$A0` | MOUNT | makes a file on the card a unit, 1–7 | one parameter sector, in |
| `$A1` | UNMOUNT | detaches UNIT, 1–7 | nothing |
| `$A2` | LIST | lists a directory on the card | one parameter sector in, then the entries out |
| `$E7` | FLUSH | pushes out anything the media holds back; for before the card is taken out | nothing |
| `$EC` | IDENTIFY | describes the card and its units | one sector, out |

The numbers are ATA's where ATA has one. Any other number fails with error 7.

- **READ and WRITE** check UNIT first: a unit 1–7 that isn't mounted fails with error 6. Then the range: LBA plus
  COUNT past the end of the unit, or of the card for unit 0, fails with error 2 before a byte moves. Then, for a
  WRITE, the protection: a card whose switch is on, or a unit mounted read-only, fails with error 5.
- **A WRITE** has nothing to do until a sector arrives. Each sector is programmed once its 512 bytes are in, and
  the command ends when the last one is. The sectors go to the media as each is programmed.
- **IDENTIFY's and LIST's sectors** come from the firmware, not the card: they don't move LBA, and IDENTIFY's
  doesn't move COUNT.
- **FLUSH** in the emulator pushes a host file's writes out to its disk.

### Errors

Status: **emulated**

ERROR ([`$C382`](#reg-c382)) holds the code while STATUS's ERR is set. Both stay until the next command, or a
/RESET.

| Code | Meaning |
|---|---|
| 1 | no card in the slot, or not started up yet; or the card was taken out during the command |
| 2 | out of range: past the last sector of the card or the unit |
| 3 | the card's transfer failed its CRC. The emulator never gives it |
| 4 | the card stopped answering. In the emulator: a host error reading, writing or flushing the image |
| 5 | write-protected: the card's switch, or a unit mounted read-only |
| 6 | no image mounted as that unit, or not a unit 1–7 (UNMOUNT or MOUNT of unit 0) |
| 7 | not a command the firmware knows |
| 8 | not found: MOUNT's file (or a directory where the file should be), LIST's directory, or a path with no NUL in its sector |
| 9 | no FAT volume on the card, or a damaged one |

An unknown command fails with 7 even with no card in the slot; every other command checks for the card first.

### IDENTIFY's sector

Status: **emulated**

IDENTIFY sends one sector out through DATA. Everything not listed is 0.

| Offset | What it holds |
|---|---|
| `$000–$003` | the card's size in sectors, low byte first |
| `$004` | its type: 1 SDSC (up to 2 GB), 2 SDHC (up to 32 GB), 3 SDXC. There is no 0: without a card, IDENTIFY fails with error 1 |
| `$005` | flags: bit 0 the card is write-protected (STATUS's WP), bit 1 it holds a FAT volume |
| `$006–$007` | the firmware's version, major then minor: 0.2 in the emulator |
| `$010–$02F` | the card's name, ASCII, NUL padded. In the emulator, the image file's name |
| `$040–$11F` | units 1–7, 32 bytes each: the size in sectors (4 bytes), flags (bit 0 mounted, bit 1 read-only, from MOUNT's flag or the file's attribute), and from +8 the file's name (24 bytes, NUL padded) |

Unit n's entry is at `$040 + (n − 1) × 32`. The BIOS takes `/BOOT.BIN`'s size from `$040`; OODOS's BIOS reads the
whole table to learn its drives.

### Waiting for the card

Status: **emulated**

The BIOS polls STATUS. To take a sector out, it waits for DATA, then reads 512 bytes. To finish a command, it waits
for BUSY to clear, then looks at ERR. A command that fails before its sector is ready is caught by the first loop
too, because BUSY clears.

<!-- listing: ../../backplane/bios/sd.s:282-309 -->
```
; Wait for a sector out of DATA, into BUF. Carry set if the command ended
; first: with an error, said.
GETSECT:        LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @read
                BIT     #SD_BUSY
                BNE     GETSECT
                BRA     FAILED
@read:          LDY     #0
@lo:            LDA     SD_DATA
                STA     BUF,Y
                INY
                BNE     @lo
@hi:            LDA     SD_DATA
                STA     BUF + $100,Y
                INY
                BNE     @hi
                CLC
                RTS

; Wait for the command to finish. Carry set if it failed, said.
WAITDONE:       LDA     SD_STATUS
                BIT     #SD_BUSY
                BNE     WAITDONE
                BIT     #SD_ERR
                BNE     FAILED
                CLC
                RTS
```

### Timing

Status: **emulated**

The emulator's figures are estimates for an RP2350 reading the SD card 4 bits at a time:

| What | Time | At 7.16 MHz |
|---|---|---|
| starting up | 50 ms | 357,954 cycles |
| a READ's first sector, after CMD | 100 µs | 715 cycles |
| each further sector read, or a written sector programmed | 25 µs | 178 cycles |
| FLUSH, UNMOUNT, IDENTIFY, and MOUNT or LIST once their sector is in | 100 µs | 715 cycles |

A LIST's first sector of entries is ready as its 100 µs end, the others 25 µs apart. A DMA transfer at a byte a
cycle empties a sector in 512 cycles, 72 µs, so after the first sector it never waits for the card. The BIOS's CPU
loop above takes 14 cycles a byte.

## Mounting FAT images

Status: **emulated**

On a card formatted by a PC, MOUNT makes a file a unit. The firmware reads FAT12, FAT16 and FAT32 with long names,
on the whole card (a "superfloppy") or in the first FAT partition of an MBR. It reads the FAT afresh at each MOUNT
and LIST, so a change made through unit 0 is seen at the next one.

- **Paths** are `/`-separated from the root: `/BOOT.BIN`, `/OODOS/DRIVES.TXT`. Each part matches a long name or
  the 8.3 name, ignoring case.
- **MOUNT** takes one sector through DATA, written as for a WRITE:

  | Offset | What it holds |
  |---|---|
  | +0 | the unit, 1–7 |
  | +1 | flags: bit 0 read-only |
  | +2 | the file's path, ending in a NUL |

  The unit is in the sector, not in UNIT. Mounting a unit that is mounted replaces it. The unit's size is the
  file's, rounded up to whole sectors: a last part-sector reads on into its cluster's slack, so a program shorter
  than a sector still loads. The unit is read-only if the flag or the file's read-only attribute says so, and
  that is what IDENTIFY's bit 1 shows. The card's switch is STATUS's WP, checked when a WRITE starts: a WRITE fails
  with error 5 under any of the three.
- **UNMOUNT** detaches UNIT. Unit 0 can't be unmounted.
- **MOUNT and LIST set COUNT to 1** for their sector, whatever was written there, and it counts down to 0 as the
  sector passes. LBA doesn't move.
- **Writes to a unit** go into the file's own clusters, found when it was mounted. A unit never grows and the FAT
  never changes, so the firmware needs no FAT writer.
- **A damaged chain,** one that ends early, runs on, loops or leaves the volume, fails with error 9 instead of
  hanging.
- **Taking the card out** unmounts every unit. A /RESET doesn't.

### LIST's entries

Status: **emulated**

LIST takes one sector through DATA, the directory's path from +0 ending in a NUL (`/` for the root). Then it sends
the entries out through DATA, with COUNT set to the sectors they fill. Each sector holds 8 entries of 64 bytes:

| Offset | What it holds |
|---|---|
| +0 | the size in bytes, 4 bytes, low first |
| +4 | the FAT attribute byte, whole: bit 0 read-only, bit 4 a directory. Other bits may be set, so test the bits, not the byte |
| +8 | the long name (or the 8.3 one), 56 bytes, NUL padded; a longer name is cut |

An entry of zeros follows the last, so a directory whose entries fill their sectors exactly gets one more sector.
`.`, `..` and volume labels are left out. `C809R` prints the root this way
([chapter 3](03-boot.md#booting-from-the-sd-card)).

## Reading and writing by DMA

Status: **emulated**

The card asks for DMA channel 0 ([chapter 5](05-dma.md#transfers-for-a-card)) while CTRL's DMA bit is set and
STATUS's DATA is: DRQ0 follows DATA. The transfer is fly-by, a byte a cycle.

1. Program channel 0 ([`$C400`](05-dma.md#reg-c400) on): ADDR the physical address, COUNT the bytes (sectors ×
   512), MODE demand (XFER `%01`) with DIR `%00`, card to memory, for a READ, or `%01`, memory to card, for a
   WRITE. Turn on the master enable and set ENABLE.
2. Write UNIT, LBA and COUNT, and CTRL as `SD_CTRL_DMA` (`$01`).
3. Write CMD, and wait for BUSY to clear. Check ERR.
4. Write CTRL as 0, so DRQ0 stays off.

For a READ, DRQ0 is low until the first sector is ready, 100 µs after CMD, and demand mode waits for it. After that
the card keeps a sector ahead in its second buffer, so DRQ0 stays high and demand mode runs on. A WRITE has both
buffers free at once, and the card programs a sector faster than the channel fills one. So with CONTROL's BURST at
no limit the CPU is held for almost the whole transfer. A lower BURST gives it a cycle back every 2^BURST bytes.

The card takes no notice of terminal count: the command ends when its last sector has passed through DATA. So the
two counts must agree:

- **A DMA count short of the sectors** leaves the card BUSY with a sector waiting. Abandon the command with CTRL's
  RESET.
- **A DMA count over** leaves channel 0 armed when the command ends. Abort it before the card is used again.

A channel counts 65,536 bytes at most, 128 sectors. A longer transfer needs the channel programmed again, or its
AUTO mode.

The BIOS's boot reads `/BOOT.BIN` this way, 88 sectors at most, to physical `$01000`:

<!-- listing: ../../backplane/bios/sd.s:217-252 -->
```
; Read SECTORS of unit 1 to $1000 by DMA and call it; back to the monitor if
; it returns, or the read fails (said).
LOADRUN:        STZ     DMA0_ADDR      ; channel 0: to $01000, a sector a 512
                LDA     #>LOAD
                STA     DMA0_ADDR + 1
                STZ     DMA0_ADDR + 2
                STZ     DMA0_COUNT
                LDA     SECTORS
                ASL
                STA     DMA0_COUNT + 1
                LDA     #DMA_DEMAND_IN
                STA     DMA0_MODE
                LDA     #DMA_ON
                STA     DMA_CONTROL
                LDA     #DMA_ENABLE
                STA     DMA0_CTRL

                LDA     #1             ; READ unit 1, from 0, SECTORS of it
                STA     SD_UNIT
                STZ     SD_LBA
                STZ     SD_LBA + 1
                STZ     SD_LBA + 2
                STZ     SD_LBA + 3
                LDA     SECTORS
                STA     SD_COUNT
                STZ     SD_COUNT + 1
                LDA     #SD_CTRL_DMA
                STA     SD_CTRL
                LDA     #SD_READ
                STA     SD_CMD
                JSR     WAITDONE
                STZ     SD_CTRL
                STZ     SD_UNIT
                BCS     @quit
                JSR     LOAD
@quit:          JMP     WOZMON
```

OODOS's BIOS moves one sector at a time the same way, both ways, and checks channel 0's terminal count after the
command, to be sure all 512 bytes moved ([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)).

## The Disk II

Status: **emulated**

The Disk II card is module C. In Apple II mode it is the IIe's disk controller in slot 6, as Apple's card was: the
owner's two Apple PROMs, the boot ROM and the sequencer, in its sockets, and an Apple drive or a Floppy Emu on its
20-pin connector. In native mode the same sixteen switches are in the card's own block, at `$C600–$C60F`
([below](#in-native-mode)), and OODOS drives a floppy through them as its drives O: and P:
([How OODOS drives it](#how-oodos-drives-it)).

The emulated card is the real one's logic, not a model of what disk software expects. The sequencer's PROM runs as
the chip is wired, so software timed to the cycle meets the disk as it would on a real Disk II.

### In Apple II mode: slot 6

Status: **emulated**

In Apple II mode ([chapter 13](13-apple-ii-mode.md#entering-apple-ii-mode)) the card has two places in the IIe's map:

- **Its switches,** sixteen addresses at `$C0E0–$C0EF`, slot 6's I/O.
- **Its boot ROM,** 256 bytes at `$C600–$C6FF`, while INTCXROM is off. With INTCXROM on, the IIe's internal ROM has
  that page instead. Without a boot ROM fitted the page reads `$FF`, and the IIe finds no disk card and starts
  BASIC.

Any access to a switch, a read or a write, works it:

| Address | Switch |
|---|---|
| `$C0E0`, `$C0E1` | phase 0 off, on |
| `$C0E2`, `$C0E3` | phase 1 off, on |
| `$C0E4`, `$C0E5` | phase 2 off, on |
| `$C0E6`, `$C0E7` | phase 3 off, on |
| `$C0E8`, `$C0E9` | the motor off, on |
| `$C0EA`, `$C0EB` | drive 1, drive 2 |
| `$C0EC`, `$C0ED` | Q6 off, on |
| `$C0EE`, `$C0EF` | Q7 off, on |

A read of an even address returns the card's data register; a read of an odd one returns `$FF`. A write puts its
byte on the card's data bus, where the sequencer can load it. Q6 and Q7 together choose what the sequencer does, as
its PROM says: on the 16-sector card, read the disk, sense the write protection, load a byte to write, or shift one
out to the head.

### The sequencer and the drives

Status: **emulated**

- **The sequencer** runs at 2 MHz, two steps for each CPU cycle at the Apple's 1.02 MHz. The IIe's long cycle, one
  in 65, is left out: 0.2%, well inside a drive's speed tolerance. Each step it looks up its PROM with its state,
  the read pulse, Q6, Q7 and the data register's top bit, and does what the PROM says to the data register: clear
  it, shift a bit in, shift it right bringing in the write protection, load it from the bus, or nothing. Without a
  sequencer PROM fitted, nothing reads.
- **A bit cell** is 4 µs, 8 steps, or what a WOZ image's own bit timing says. A cell holding a 1 gives a read pulse
  on its first step. After three 0 bits in a row the read amplifier finds noise, random bits about 3 in 10 of them
  ones, as on a real drive over blank disk. Writing, a change of the sequencer's top state bit within a cell writes
  a 1. (hw/design.md gives every cell 4 µs; the emulator also follows a WOZ disk's own timing, 3.5 µs for some,
  with cells a step apart in turn.)
- **The head** goes, on each phase change and only while the motor turns, to where the stepper magnets that are on
  pull it: onto a single magnet up to a half track away, or halfway between two adjacent ones, a quarter track from
  each. There are 160 quarter tracks.
- **The motor** runs on for 1 s (1,020,484 cycles) after it is switched off. The two drives share it; the one not
  selected doesn't turn.
- **Write protection** is the disk's. An empty drive reads as write-protected, and nothing is written to a
  write-protected disk.
- **/RESET** clears the card's latch: the four phases, Q6 and Q7 off, drive 1 selected, and the motor switched off,
  though it runs on for its second. The disks stay in their drives and the heads where they are. Entering Apple II
  mode, Ctrl-Reset and a hard reset all do this. In the emulator the north bridge resets the card with the IIe's
  switches, not through the card's own reset.

### Disk images

Status: **emulated**

The emulator turns every image into a stream of bits for each track when it loads it:

- **`.woz`**, WOZ 1 and 2: the bits as captured, which is what copy-protected originals need. An unmapped quarter
  track reads as blank.
- **`.dsk`, `.do` and `.po`**: 140 KB, 35 tracks of 16 sectors, written out as DOS 3.3 formats a disk, volume 254.
  Whether the file holds its sectors in DOS or ProDOS order is decided from its contents (a DOS catalog, or a
  ProDOS volume directory, where one order puts it), and only then from the name.

A disk can be saved back as `.woz`, or as sectors while every track still reads as 16 ordinary ones. The terminal
app's `--disk` puts a disk in drive 1 and keeps what is written in memory; the file isn't changed. The disks stay
in their drives through a power cycle. `.nib`, `.d13` and `.2mg` are in the emulator's backlog
([EMULATOR_PLAN.md](../../emulator/docs/EMULATOR_PLAN.md#backlog)).

### In native mode

Status: **emulated**

In native mode the card's block is `$C600–$C67F`, module C's place in the I/O hole:

| Offset | Address | What it is |
|---|---|---|
| `$00–$0F` | [`$C600–$C60F`](#native-modes-switches) | the sixteen switches, slot 6's, in the same order |
| `$10–$7E` | `$C610–$C67E` | read `$FF`; writes are ignored |
| `$7F` | [`$C67F`](#reg-c67f) | the ID, `$C1` |

Neither PROM is visible in native mode. The boot ROM is only slot 6's page in Apple II mode, and the sequencer's
PROM is never on the bus in either mode: the card reads it for itself at each step. Native software sees the
sequencer only through what it does to the data register.

OODOS finds the card by its ID at cold boot and lists its drives, O: and P:, among the drives it found
([below](#how-oodos-drives-it)).

### Native mode's switches

Status: **emulated**

The switches are the ones Apple II mode has at `$C0E0–$C0EF` ([above](#in-apple-ii-mode-slot-6)), at the bottom of
the card's own block:

| Address | Equate | Switch |
|---|---|---|
| [`$C600`](#reg-c600), [`$C601`](#reg-c601) | `DISK2_PH0OFF`, `DISK2_PH0ON` | phase 0 off, on |
| [`$C602`](#reg-c602), [`$C603`](#reg-c603) | `DISK2_PH1OFF`, `DISK2_PH1ON` | phase 1 off, on |
| [`$C604`](#reg-c604), [`$C605`](#reg-c605) | `DISK2_PH2OFF`, `DISK2_PH2ON` | phase 2 off, on |
| [`$C606`](#reg-c606), [`$C607`](#reg-c607) | `DISK2_PH3OFF`, `DISK2_PH3ON` | phase 3 off, on |
| [`$C608`](#reg-c608), [`$C609`](#reg-c609) | `DISK2_MOTOROFF`, `DISK2_MOTORON` | the motor off, on |
| [`$C60A`](#reg-c60a), [`$C60B`](#reg-c60b) | `DISK2_DRIVE1`, `DISK2_DRIVE2` | drive 1, drive 2 |
| [`$C60C`](#reg-c60c), [`$C60D`](#reg-c60d) | `DISK2_Q6OFF`, `DISK2_Q6ON` | Q6 off, on |
| [`$C60E`](#reg-c60e), [`$C60F`](#reg-c60f) | `DISK2_Q7OFF`, `DISK2_Q7ON` | Q7 off, on |

- **Any access works a switch,** a read or a write. A read of an even address returns the data register; a read
  of an odd one returns `$FF`. A write also puts its byte on the card's data bus, where the sequencer loads it in
  write mode.
- **Q7 and Q6 choose what the sequencer does:**

  | Q7 | Q6 | The sequencer |
  |---|---|---|
  | off | off | reads the disk: a whole byte lands in the data register with bit 7 set |
  | off | on | senses the write protection: bit 7 of the data register is 1 if the disk is protected |
  | on | on | loads the byte on its bus into the data register, to write |
  | on | off | shifts the data register out to the head, a bit a cell |

- **One access, one instruction.** Use plain absolute loads and stores, or an absolute,X load whose address stays
  in the card's page, which the W65C02S makes once. An indexed access that crosses a page reads another address
  first, and a read-modify-write instruction touches its address twice; either can work a switch it didn't mean
  to.
- **/RESET** (a hard reset) clears the latch as in Apple II mode: the phases, Q6 and Q7 off, drive 1 selected, the
  motor switched off and running on ([above](#the-sequencer-and-the-drives)). The emulator's debugger and the
  WebUI's memory view read the block without working the switches.

### Timing in native mode

Status: **emulated**

The sequencer's clock is the backplane's oscillator divided by 7, 2.045 MHz, in either mode and whatever the CPU's
speed. Native mode's CPU runs at the oscillator divided by 2, 7.16 MHz, so a sequencer step is exactly 3.5 CPU
cycles:

| | Steps | CPU cycles at 7.16 MHz |
|---|---|---|
| a step | 1 | 3.5 |
| a bit cell | 8 | 28 |
| a disk byte, 8 bits | 64 | **224** |
| a sync byte, 10 bits | 80 | 280 |

The count comes out whole every two steps, so a loop counted in cycles stays in step with the disk with no drift.
The emulator run at a slower clock keeps the sequencer at its own speed, so there a disk byte is fewer cycles
([chapter 14](14-emulator-notes.md#what-differs)).

- **Reading is polled,** and works at any CPU speed. The sequencer puts each byte together itself; software reads
  an even switch (`DISK2_Q6OFF` is the one OODOS uses) until bit 7 is set, then takes the byte. To take each byte
  once, wait for bit 7 to go clear between them, as OODOS's driver does. Bound every wait: with no disk turning,
  or no sequencer PROM, bit 7 may never come.
- **Sensing the write protection:** Q6 on with Q7 off, a whole loop of the sequencer's eight steps (OODOS waits 30
  cycles), then read an even switch: bit 7 is 1 for a protected disk. An empty drive reads as protected.
- **Writing is timed by the CPU.** With Q6 on, a write to `DISK2_Q7ON` starts write mode and puts its byte on the
  bus. For each byte after it, write the byte to `DISK2_Q6ON` (load) and hold Q6 on for at least a whole loop of
  eight steps, 28 cycles, before a read of `DISK2_Q6OFF` (shift); the load is missed, and nothing written, if Q6 is
  on for less. Each load comes 224 cycles after the last, or 280 after a sync byte. A load a cycle late still reads
  back: now and then the byte gains a ninth bit, a 0, which a reader passes over as it does in sync bytes. A load
  a cycle early loses bits. A read of `DISK2_Q7OFF` ends write mode.
- **Nothing may stretch a write loop:** no interrupt, no DMA transfer (a channel holds the CPU off the bus) and no
  access to a card or to memory with wait states (WAIT0–WAIT3, `$C68A–$C68D`, are 0 from reset, and OODOS leaves
  them so; [chapter 11](11-north-bridge-and-rtc.md#reg-c68a)).
- **Stepping is timed by the CPU** too. A phase switched on pulls the head toward its magnet, a half track at most;
  two phases on next to each other hold it between them, a quarter track. The head moves only while the motor
  turns. A driver steps a half track at a time: the next phase on, a step's time, the last one off, and lets the
  head settle after the last step. The emulator moves the head at once, so it needs no step or settling time; a
  real drive does.
- **The motor** of the selected drive starts at `DISK2_MOTORON` and, from `DISK2_MOTOROFF`, runs on for 2,040,968
  of the sequencer's steps: 0.998 s in native mode at any CPU speed, a second in Apple II mode. The card has one
  motor line, to the selected drive: the other drive's disk doesn't turn.
- **No DMA,** and no interrupt. The Disk II is the one card that can't be a DMA channel's partner
  ([chapter 5](05-dma.md#transfers-for-a-card)), and its IRQH bit, 4, always reads 1
  ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)). A driver polls.

The design record has the reasons ([hw/design.md, "Storage"](../../hw/design.md#storage)).

### Without the sequencer PROM

Status: **emulated**

The sequencer's PROM is Apple's and comes from the owner, as the IIe's ROM does; the emulator ships none, and the
website has none unless it is given one (Settings, ROMs). Without it the switches still work: the motor turns, the
phases move the head, Q6 and Q7 change. But every step of the sequencer clears the data register, so an even switch
reads `$00`, bit 7 is never set and nothing is read from a disk. A native driver's waits must end on their own.
OODOS's do: on such a card O: and P: are there, and using one gives Disk I/O after a fraction of a second, as an
empty drive does ([the User's Guide, "Drives"](../oodos-users-guide/03-drives-and-files.md#drives)).

### How OODOS drives it

Status: **emulated**

OODOS's driver is [`oodos/sys/disk2.s`](../../oodos/sys/disk2.s), in the BIOS's block (61), under the BIOS's layer
for Apple CP/M disks. The drives it gives programs, O: and P:, are in the Programmer's Guide
([Removable drives](../oodos-programmers-guide/03-file-system.md#removable-drives-o-and-p)) and the User's Guide
([Drives](../oodos-users-guide/03-drives-and-files.md#drives)); this is how it uses the card.

- **The format** is the 16-sector format DOS 3.3 writes, which the emulator also writes a `.dsk` out as: each
  sector an address field (`D5 AA 96`, the volume, track and sector and their checksum, each in two disk bytes,
  `DE AA`) and a data field (`D5 AA AD`, 342 six-bit values in the 6-and-2 code and a checksum, `DE AA`; the driver
  writes `DE AA EB`). The volume number isn't checked. The image's sector f of a track is physical sector
  `FILE2PHYS[f]` (`$0 $D $B $9 $7 $5 $3 $1 $E $C $A $8 $6 $4 $2 $F`); above it the BIOS puts CP/M's 256-byte
  sector n of a track at the image's sector `SKEW[n]` (`0 6 12 3 9 15 14 5 11 2 8 7 13 4 10 1`), cpmtools's
  `apple-do` layout, the SoftCard's.
- **A whole track at a time.** A read seeks and reads the whole track into a 4 KB track buffer, decoding each data
  field as it passes and checking its checksum. Each drive has two buffers, one for the directory's track (3) and
  one for the rest, in blocks 29 and 30, mapped into window 1 while the driver works. A field that stays bad for
  three tries leaves its sector out and the rest of the track is kept; a read of that sector has the track read
  again once, then is Disk I/O.
- **One sector at a time, written through.** A write finds the sector's address field, then writes its data field
  from the buffer; the buffer and the disk always agree.
- **Write protection** is sensed before each sector is written; a protected disk is Read Only.
- **Seeking:** the head's place is remembered for each drive; when it isn't known (at cold boot, after a field
  names the wrong track, or after a track fails) the driver steps 80 half tracks outward onto the stop first. For
  the real drive it waits 3 ms a half track and 15 ms to settle (proposed: the emulator's head moves at once).
- **Every wait is bounded:** a disk byte 256 polls, an address field 13,000 disk bytes (two turns and more), a track
  three tries. A disk that doesn't turn, or a card with no sequencer PROM, ends in Disk I/O, not a hang.
- **The motor** is switched on for each read or write and off at its end; the card's run-on keeps the disk turning
  for the next access within the second. There is no spin-up wait: a read just starts, and a write starts once its
  sector's address field has been read.
- **A changed floppy.** Whether a disk is turning is seen from the data register changing over about 2,000 cycles.
  A disk that had stopped, or the other drive's (the card has one motor line), may have been changed: the BIOS
  empties its buffers, and the BDOS reads the directory again and compares its checksums. Another disk is logged in
  afresh, and a file open on the old one gets error 10, Media Change. A drive accessed within the same second of
  the clock is taken as unchanged. The rules are the Programmer's Guide's
  ([Removable drives](../oodos-programmers-guide/03-file-system.md#removable-drives-o-and-p)).
- **Interrupts and DMA during a sector.** OODOS calls the driver with interrupts off: the BDOS's entry masks them
  for the whole call, and a sector write masks them itself as well, for the whole write, its seek and the search for
  its address field included, and puts the caller's flag back after. The write also turns the DMA controller's
  master enable off ([`$C441`](05-dma.md#reg-c441)) and puts it back after; a read leaves DMA alone, since OODOS
  runs no transfer while the driver does. Break (NMI) can't be masked: the NMI handler turns the card's write mode
  and motor off and the master enable back on before the monitor, so a write it cuts short loses that sector and
  the rest of the track is kept.

The design record is [hw/oodos.md, "The plan, phase 6"](../../hw/oodos.md#the-plan-phase-6).

## Registers

Status: **emulated**

<a id="reg-c380"></a>
### $C380 SD_CTRL: the card's control

#### Equates

`SD`, `SD_CTRL`

#### Module

7, the mass storage card

#### Access

R/W. Bit 0 reads back as written; bit 1 is a strobe and reads 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | ignored; read 0 |
| 1 | RESET | 1: abandon the command in progress and start the card up again (50 ms, READY clear meanwhile). The units stay mounted, and the other registers are left as they are |
| 0 | DMA | 1: DRQ0 follows STATUS's DATA, so DMA channel 0 moves the bytes ([above](#reading-and-writing-by-dma)) |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Under the monitor CTRL reads `$00`: the BIOS's boot sets DMA only around its read, and Break and BRK write RESET or
0. A command written within 50 ms of a RESET fails with error 1; wait for READY first.

<a id="reg-c381"></a>
### $C381 SD_STATUS: the card's status

#### Equates

`SD_STATUS`

#### Module

7, the mass storage card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | BUSY | 1: a command is in progress. A command written now is ignored |
| 6 | READY | 1: a card is in the slot and started up |
| 5 | PRESENT | 1: a card is in the slot |
| 4 | WP | 1: the card's write-protect switch is on |
| 3 | DATA | 1: a whole sector is waiting at DATA, or a whole buffer is free for one |
| 2–1 | | 0 |
| 0 | ERR | 1: the last command failed, and ERROR says why. The next command clears it |

#### Reset

BUSY, DATA and ERR 0. READY, PRESENT and WP follow the card. A /RESET leaves a card started up, so with one in the
slot STATUS reads `$60` (`$70` write-protected). At power-up READY is 0 for the first 50 ms.

#### Status

**emulated**

#### Notes

After a power-on with a card that has no `/BOOT.BIN`, ERR is still set under the monitor, from the BIOS's MOUNT.
CTRL's RESET doesn't clear ERR.

DATA is set for a whole sector, or a whole free buffer, not for each byte. hw/design.md's register table says "a
byte, or room for one"; its notes on the emulator, and the code, say a whole sector, and this is what DRQ0 follows.

<a id="reg-c382"></a>
### $C382 SD_ERROR: why the last command failed

#### Equates

`SD_ERROR`

#### Module

7, the mass storage card

#### Access

R. Writes are ignored.

#### Bits

All: the error code, 1–9, while STATUS's ERR is set; 0 after a command that succeeded. The codes are in
[Errors](#errors).

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

A new command clears it as it starts. The BIOS prints it in hex, as `SD ERROR 09`, for any code but 8, which it
says as `NOT ON THE CARD:` and the path.

<a id="reg-c383"></a>
### $C383 SD_CMD: start a command

#### Equates

`SD_CMD`

#### Module

7, the mass storage card

#### Access

W. A write starts the command, unless the card is BUSY, when it is ignored. Reads give `$FF`.

#### Bits

All: the command: `$20` READ, `$30` WRITE, `$A0` MOUNT, `$A1` UNMOUNT, `$A2` LIST, `$E7` FLUSH, `$EC` IDENTIFY.
Any other value fails with error 7. The commands are in [Commands](#commands).

#### Reset

None: nothing is stored.

#### Status

**emulated**

#### Notes

Write UNIT, LBA, COUNT and CTRL first. A READ or WRITE takes UNIT, LBA and COUNT as they are when CMD is written.

<a id="reg-c384"></a>
### $C384 SD_UNIT: the unit

#### Equates

`SD_UNIT`

#### Module

7, the mass storage card

#### Access

R/W. Only bits 0–2 are kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | ignored; read 0 |
| 2–0 | UNIT | 0: the whole card. 1–7: a mounted image file |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

READ, WRITE and UNMOUNT use it. MOUNT doesn't: its unit is in its parameter sector. The BIOS and OODOS's BIOS put it
back to 0 after a transfer.

<a id="reg-c386"></a>
### $C386 SD_LBA: the sector

#### Equates

`SD_LBA`

#### Module

7, the mass storage card

#### Access

R/W, 4 bytes, `$C386–$C389`, low byte first. Each byte can be written alone.

#### Bits

All: a 32-bit sector number within UNIT, up to 2 TB of 512-byte sectors. For a unit 1–7, sector 0 is the file's
first.

#### Reset

`$00 $00 $00 $00`.

#### Status

**emulated**

#### Notes

It goes up by one as each sector of a READ or WRITE passes through DATA, so after a transfer it names the next
sector. IDENTIFY, MOUNT and LIST don't move it. After the BIOS's SD boot it holds the number of sectors read.

<a id="reg-c38a"></a>
### $C38A SD_COUNT: the sectors

#### Equates

`SD_COUNT`

#### Module

7, the mass storage card

#### Access

R/W, 2 bytes, `$C38A–$C38B`, low byte first. A read gives the sectors left.

#### Bits

All: the sectors for a READ or WRITE, 1–65,535, or 0 for 65,536.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

It goes down by one as each sector passes through DATA, and reads 0 after a whole transfer. MOUNT and LIST set it
to 1 for their parameter sector, whatever was written; LIST then sets it to its sectors of entries. IDENTIFY leaves
it alone. Under the monitor it is 0 after any of the BIOS's commands.

<a id="reg-c38c"></a>
### $C38C SD_DATA: the transfer's bytes

#### Equates

`SD_DATA`

#### Module

7, the mass storage card

#### Access

R/W. A read takes the next byte of the sector being read out; a write puts the next byte of the sector being
written in. The 512th byte passes the sector on: LBA and COUNT move, and the card fetches or programs the next.

#### Bits

All: a byte of the sector, at BUFPTR.

#### Reset

Reads `$FF`: no transfer is in progress.

#### Status

**emulated**

#### Notes

While STATUS's DATA is clear, a read gives `$FF` and takes nothing, and a write is dropped. DMA channel 0 reads and
writes the same port by /DACK when CTRL's DMA bit is set.

<a id="reg-c38d"></a>
### $C38D SD_BUFPTR: the place in the sector

#### Equates

`SD_BUFPTR`

#### Module

7, the mass storage card

#### Access

R/W, 2 bytes, `$C38D–$C38E`, low byte first. A write moves the place in the current sector.

#### Bits

All: 9 bits, 0–511, the byte of the current sector that DATA reads or writes next. `$C38E` holds bit 8 in its bit
0; its other bits are ignored and read 0.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

It counts up as DATA moves bytes, and goes back to 0 as each sector passes. A program can set it to jump into a
sector without reading what comes before; the sector passes on when the place reaches 512.

<a id="reg-c390"></a>
### $C390 SD_IEN: the interrupts on

#### Equates

`SD_IEN`

#### Module

7, the mass storage card

#### Access

R/W. Only bits 0 and 1 are kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | ignored; read 0 |
| 1 | CARD | 1: interrupt when a card goes in or comes out |
| 0 | DONE | 1: interrupt when a command ends |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The card pulls /IRQ, with IRQL bit 7 reading 0, while any bit is set in both IEN and ISR. Break and BRK turn IEN
off, and the BIOS never turns it on.

<a id="reg-c391"></a>
### $C391 SD_ISR: the interrupts pending

#### Equates

`SD_ISR`

#### Module

7, the mass storage card

#### Access

R; write 1s to clear those bits. Writing 0s changes nothing.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | 0 |
| 1 | CARD | 1: a card went in or came out |
| 0 | DONE | 1: a command ended, with or without an error. A command abandoned by /RESET or CTRL's RESET doesn't set it |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Both bits are set whether IEN has them on or not. The BIOS doesn't clear them, so after any SD command DONE reads 1
under the monitor. An abandoned command sets no DONE, so a program that abandons one with CTRL's RESET mustn't
wait for it. Clear ISR before turning IEN on.

<a id="reg-c3ff"></a>
### $C3FF SD_ID: the card's ID

#### Equates

`SD_ID`

#### Module

7, the mass storage card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | TYPE | 7: the module ID |
| 3–0 | REV | 1: revision 1 |

#### Reset

`$71`.

#### Status

**emulated**

#### Notes

The BIOS checks for `$71` (`SD_ID_V1`) before it touches the card. With the slot empty the ID still reads `$71`: it
is the mass storage card's, and STATUS's PRESENT says whether an SD card is in it.

<a id="reg-c600"></a>
### $C600 DISK2_PH0OFF: stepper phase 0 off

#### Equates

`DISK2`, `DISK2_PH0OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns every phase off.

#### Status

**emulated**

#### Notes

`DISK2` names the card's block, `$C600–$C67F`; `DISK2_PH0OFF` is its first switch. OODOS's driver reaches the phases
as `DISK2_PH0OFF` + 2 × n by an absolute,X load inside the page. In Apple II mode `$C600` is a byte of slot 6's boot
ROM, and this switch is `$C0E0`.

<a id="reg-c601"></a>
### $C601 DISK2_PH0ON: stepper phase 0 on

#### Equates

`DISK2_PH0ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

With the motor turning, the head moves toward phase 0's magnet: onto it from up to a half track away, or halfway to
it with a neighbouring phase on as well ([above](#timing-in-native-mode)). In Apple II mode `$C601` is a byte of
slot 6's boot ROM, and this switch is `$C0E1`.

<a id="reg-c602"></a>
### $C602 DISK2_PH1OFF: stepper phase 1 off

#### Equates

`DISK2_PH1OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

Stepping off phase 1, OODOS turns the next phase on first and this one off a step's time later; at the end of a seek
the last phase goes off once the head has settled. In Apple II mode `$C602` is a byte of slot 6's boot ROM, and this
switch is `$C0E2`.

<a id="reg-c603"></a>
### $C603 DISK2_PH1ON: stepper phase 1 on

#### Equates

`DISK2_PH1ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

With the motor turning, the head moves toward phase 1's magnet: onto it from up to a half track away, or halfway to
it with a neighbouring phase on as well ([above](#timing-in-native-mode)). In Apple II mode `$C603` is a byte of
slot 6's boot ROM, and this switch is `$C0E3`.

<a id="reg-c604"></a>
### $C604 DISK2_PH2OFF: stepper phase 2 off

#### Equates

`DISK2_PH2OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

Stepping off phase 2, OODOS turns the next phase on first and this one off a step's time later; at the end of a seek
the last phase goes off once the head has settled. In Apple II mode `$C604` is a byte of slot 6's boot ROM, and this
switch is `$C0E4`.

<a id="reg-c605"></a>
### $C605 DISK2_PH2ON: stepper phase 2 on

#### Equates

`DISK2_PH2ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

With the motor turning, the head moves toward phase 2's magnet: onto it from up to a half track away, or halfway to
it with a neighbouring phase on as well ([above](#timing-in-native-mode)). In Apple II mode `$C605` is a byte of
slot 6's boot ROM, and this switch is `$C0E5`.

<a id="reg-c606"></a>
### $C606 DISK2_PH3OFF: stepper phase 3 off

#### Equates

`DISK2_PH3OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

Stepping off phase 3, OODOS turns the next phase on first and this one off a step's time later; at the end of a seek
the last phase goes off once the head has settled. In Apple II mode `$C606` is a byte of slot 6's boot ROM, and this
switch is `$C0E6`.

<a id="reg-c607"></a>
### $C607 DISK2_PH3ON: stepper phase 3 on

#### Equates

`DISK2_PH3ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns the phase off.

#### Status

**emulated**

#### Notes

With the motor turning, the head moves toward phase 3's magnet: onto it from up to a half track away, or halfway to
it with a neighbouring phase on as well ([above](#timing-in-native-mode)). In Apple II mode `$C607` is a byte of
slot 6's boot ROM, and this switch is `$C0E7`.

<a id="reg-c608"></a>
### $C608 DISK2_MOTOROFF: the motor off

#### Equates

`DISK2_MOTOROFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET switches the motor off,
and it runs on.

#### Status

**emulated**

#### Notes

The disk keeps turning for 2,040,968 of the sequencer's steps, 0.998 s in native mode, then stops. OODOS switches
the motor off at the end of every read and write and lets the run-on keep the disk at speed for the next access. In
Apple II mode `$C608` is a byte of slot 6's boot ROM, and this switch is `$C0E8`.

<a id="reg-c609"></a>
### $C609 DISK2_MOTORON: the motor on

#### Equates

`DISK2_MOTORON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET switches the motor off.

#### Status

**emulated**

#### Notes

The selected drive's motor: the card has one motor line, and the drive not selected doesn't turn. The emulated disk
is at speed at once. In Apple II mode `$C609` is a byte of slot 6's boot ROM, and this switch is `$C0E9`.

<a id="reg-c60a"></a>
### $C60A DISK2_DRIVE1: select drive 1

#### Equates

`DISK2_DRIVE1`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET selects drive 1.

#### Status

**emulated**

#### Notes

OODOS's O:. Selecting the other drive moves the motor line to it. In Apple II mode `$C60A` is a byte of slot 6's
boot ROM, and this switch is `$C0EA`.

<a id="reg-c60b"></a>
### $C60B DISK2_DRIVE2: select drive 2

#### Equates

`DISK2_DRIVE2`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET selects drive 1.

#### Status

**emulated**

#### Notes

OODOS's P:. In Apple II mode `$C60B` is a byte of slot 6's boot ROM, and this switch is `$C0EB`.

<a id="reg-c60c"></a>
### $C60C DISK2_Q6OFF: Q6 off: read, or shift

#### Equates

`DISK2_Q6OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns Q6 off.

#### Status

**emulated**

#### Notes

The read port: with Q7 off, the sequencer reads the disk and a read here gives each byte, bit 7 set once it is
whole; with Q7 on, it shifts the loaded byte out to the head. OODOS polls here for every disk byte, and reads here
28 cycles after each load to start the shift ([above](#timing-in-native-mode)). In Apple II mode `$C60C` is a byte
of slot 6's boot ROM, and this switch is `$C0EC`.

<a id="reg-c60d"></a>
### $C60D DISK2_Q6ON: Q6 on: sense write protection, or load

#### Equates

`DISK2_Q6ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns Q6 off.

#### Status

**emulated**

#### Notes

With Q7 off, the sequencer brings the write protection into the data register's bit 7 (1: protected; an empty drive
reads protected). With Q7 on, it loads the byte on its bus: a store here is how a write loop gives it each byte. In
Apple II mode `$C60D` is a byte of slot 6's boot ROM, and this switch is `$C0ED`.

<a id="reg-c60e"></a>
### $C60E DISK2_Q7OFF: Q7 off: read mode

#### Equates

`DISK2_Q7OFF`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns the data register.

#### Bits

All: the data register, as the sequencer has it at this cycle ([above](#native-modes-switches)); `$00` on a card
with no sequencer PROM.

#### Reset

The data register isn't cleared: `$00` at power-on, then as the sequencer leaves it. /RESET turns Q7 off.

#### Status

**emulated**

#### Notes

Ends write mode. OODOS reads here for the write-protect sense's result, a loop of the sequencer after Q6 on, and at
the end of every write. Its NMI handler reads here too, so a Break never leaves the card writing. In Apple II mode
`$C60E` is a byte of slot 6's boot ROM, and this switch is `$C0EE`.

<a id="reg-c60f"></a>
### $C60F DISK2_Q7ON: Q7 on: write mode

#### Equates

`DISK2_Q7ON`

#### Module

C, the Disk II card

#### Access

R/W, in native mode. Any access, a read or a write, works the switch; a write also puts its byte on the card's data
bus. A read returns `$FF`.

#### Bits

All: `$FF`.

#### Reset

`$FF`. /RESET turns Q7 off.

#### Status

**emulated**

#### Notes

Write mode: the head writes from the next cell on, and a write-protected disk is left as it is. With Q6 already on,
a store here starts write mode and loads its byte, the first of the field's sync bytes in OODOS's loop. In Apple II
mode `$C60F` is a byte of slot 6's boot ROM, and this switch is `$C0EF`.

<a id="reg-c67f"></a>
### $C67F DISK2_ID: the Disk II card's ID

#### Equates

`DISK2_ID`

#### Module

C, the Disk II card

#### Access

R, in native mode. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | TYPE | `$C`: the module ID |
| 3–0 | REV | 1: revision 1 |

#### Reset

`$C1`.

#### Status

**emulated**

#### Notes

The native machine finds the card by it: OODOS's cold boot checks for `$C1` (`DISK2_ID_V1`). In Apple II mode
`$C67F` is a byte of slot 6's boot ROM instead.
