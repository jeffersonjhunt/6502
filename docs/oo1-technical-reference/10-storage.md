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
  registers alone, ERR and ERROR included, and the units stay mounted.
- **Taking the card out** fails a command in progress with error 1 and unmounts every unit. Putting a card in
  unmounts every unit too, and the new card starts up.

**In Apple II mode** the card's block is hidden behind the IIe's I/O. The /RESET that enters the mode has abandoned
any command and turned the card's interrupts off. The units stay mounted for when a hard reset brings the native
machine back.

### Interrupts

Status: **emulated**

ISR ([`$C391`](#reg-c391)) has two bits. Bit 0 is set when a command ends, with or without an error. Bit 1 is set
when a card goes in or comes out (not for a card already in the slot at power-up). They are set whether IEN
enables them or not. While a bit is set in both ISR and IEN, the card pulls /IRQ, and bit 7 of IRQL reads 0
([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)). Writing 1s to ISR clears those bits.

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
- **ISR's bit 0** is set after any of these, since every command ends by setting it. The BIOS never clears it. IEN
  is 0, so it interrupts nothing.
- **Break and BRK** turn IEN off. If the card is BUSY, they write CTRL's RESET, which abandons the command (the card
  then starts up again, 50 ms); otherwise they write CTRL as 0, turning DRQ0 off. Either way CTRL reads 0. ERROR,
  ISR, UNIT, LBA, COUNT and the units are left as they were ([chapter 4](04-interrupts.md#pausebreak)).

OODOS's BIOS keeps the same habits: it polls, sets CTRL's DMA bit only around a transfer, puts UNIT back to 0, and
uses CTRL's RESET on a command that hasn't ended in about a second ([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)).

## Commands

Status: **emulated**

Write UNIT, LBA and COUNT as the command needs them, then the command's number to CMD ([`$C383`](#reg-c383)).
STATUS's BUSY is set from then until the command ends, when ISR's bit 0 is set. A command that fails sets ERR and
ERROR and ends at once. A command written while BUSY is ignored: wait for BUSY to clear first, or abandon the
command in progress with CTRL's RESET. A new command clears ERR and ERROR.

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
| `$004` | its type: 0 none, 1 SDSC (up to 2 GB), 2 SDHC (up to 32 GB), 3 SDXC |
| `$005` | flags: bit 0 the card is write-protected, bit 1 it holds a FAT volume |
| `$006–$007` | the firmware's version, major then minor: 0.2 in the emulator |
| `$010–$02F` | the card's name, ASCII, NUL padded. In the emulator, the image file's name |
| `$040–$11F` | units 1–7, 32 bytes each: the size in sectors (4 bytes), flags (bit 0 mounted, bit 1 read-only), and from +8 the file's name (24 bytes, NUL padded) |

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
  than a sector still loads. The unit is read-only if the flag, the file's read-only attribute or the card's switch
  says so.
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
| +4 | the FAT attributes: bit 0 read-only, bit 4 a directory |
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

DRQ0 drops while the card fetches the next sector, so demand mode pauses and starts again by itself. After the
first sector the card stays ahead of the channel, so with CONTROL's BURST at no limit the CPU is held for almost the
whole transfer. A lower BURST gives it a cycle back every 2^BURST bytes.

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
20-pin connector. In native mode the emulated card answers only its ID byte. The design gives it its switches in
native mode too, which is [proposed](#native-modes-switches).

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
- **The head** moves a quarter track at a time towards the stepper magnets that are on, when a phase changes and
  only while the motor turns. There are 160 quarter tracks.
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

In native mode the card's block is `$C600–$C67F`. Only the ID at `$C67F` ([`DISK2_ID`](#reg-c67f)) answers: it
reads `$C1`. The rest of the block reads `$FF` and ignores writes, and the boot ROM isn't visible. OODOS finds the
card by its ID at cold boot, and lists it among the cards it found.

### Native mode's switches

Status: **proposed**

The design puts the card's sixteen switches in its native block as well: `$C600–$C60F` work as `$C0E0–$C0EF` do in
Apple II mode, in the same order, with the ID still at `$C67F` and the rest reading `$FF`. Native software could
then read and write the Apple disk in the drive without moving its image to the SD card: a copier between the two,
or a native utility for DOS 3.3 and ProDOS disks.

- **Reading** is the same polling loop at any CPU speed. The sequencer runs from the backplane's oscillator, not
  the CPU's clock, and assembles each disk byte itself, holding it in the data register for about 32 µs.
- **Writing and stepping** are timed by the CPU. The Apple's own code counts its cycles, so it can't run natively,
  but a native driver can. At 7.16 MHz a sequencer step is exactly 3.5 CPU cycles, so a disk byte, 64 steps, is
  exactly 224 cycles, and a native write loop holds the disk's timing with no drift. While it writes, the driver
  keeps interrupts and DMA off and touches no card with wait states.
- **No DMA.** The Disk II is the one card that doesn't support it.

backplane.inc has no equates for these switches, and the emulator doesn't decode them yet. An OODOS phase now under
way adds native mode, and its register pages follow here later. The design record has the reasons
([hw/design.md, "Storage"](../../hw/design.md#storage)).

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
| 0 | DONE | 1: a command ended, with or without an error |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Both bits are set whether IEN has them on or not. The BIOS doesn't clear them, so after any SD command DONE reads 1
under the monitor. Clear ISR before turning IEN on.

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
### $C600 DISK2: the Disk II card's block

#### Equates

`DISK2`

#### Module

C, the Disk II card

#### Access

In native mode, R: every address of `$C600–$C67E` reads `$FF`, and writes are ignored. In Apple II mode,
`$C600–$C6FF` is slot 6's boot ROM, read-only.

#### Bits

All: `$FF` in native mode. In Apple II mode, with INTCXROM off, the byte of the boot ROM in the card's socket; with
INTCXROM on, the IIe's internal ROM.

#### Reset

`$FF` in native mode, which no reset changes.

#### Status

**emulated**

#### Notes

The base of the card's block: the equate names the block, not a register. The proposed native switches would be at
`$C600–$C60F` ([above](#native-modes-switches)). The card's switches in Apple II mode are at `$C0E0–$C0EF`
([above](#in-apple-ii-mode-slot-6)).

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
