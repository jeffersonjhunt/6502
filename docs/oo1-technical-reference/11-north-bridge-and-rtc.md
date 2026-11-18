# 11 The north bridge and the clock

The north bridge's registers (module D, `$C680`): the windows, VEC, MODE, STATUS, the interrupt summary, the wait
states, and on revision 2 the windows' high bits and MAP; and the DS3231 real-time clock behind it.

The design record, for the reasons: [hw/design.md, "North bridge
registers"](../../hw/design.md#north-bridge-registers), ["Real-time clock"](../../hw/design.md#real-time-clock).

## The window registers

Status: **emulated**

The north bridge sits on the CPU card. It turns each CPU address into a 24-bit physical one, and each physical
address into a module ID. The bridge isn't a card, so it takes the spare ID `$D` for its own registers,
`$C680–$C6FF`. While the CPU is in that block the bridge puts `$D` on PS0–PS3, so no card answers.

WIN0–WIN3 ([`$C680`–`$C683`](#reg-c680)) map the CPU's four 16 KB windows. VEC ([`$C684`](#reg-c684)) maps
`$C000–$FFFF` while the CPU reads a vector. All five have the same layout: bit 7 ROM, bit 6 RO, bits 5–0 the
block. [Chapter 2](02-memory-and-banking.md#the-four-windows) says how they make the memory map, and
[chapter 4](04-interrupts.md#vec-and-the-vectors) how VEC is used.

A window register takes effect on the next bus cycle. The BIOS relies on that. It copies itself into RAM block 63
and points W3 at the copy, and the next fetch comes from the copy, at the same address
([chapter 3](03-boot.md#reset-and-the-flash)).

## The high window registers and MAP

Status: **proposed**

Emulated as step B7. A revision 2 bridge (ID `$D2`) has six more registers; revision 1 (`$D1`) has none of them,
and there the six offsets read `$FF` and drop what is written.

- **WIN0H–WIN3H** ([`$C6A0`–`$C6A3`](#reg-c6a0)) give A20–A23 for their window's block while its ROM bit is clear,
  so the block is ten bits, WIN*n*H:BLOCK, any 16 KB of the 16 MB. **VECH** ([`$C6A4`](#reg-c6a4)) does the same
  for VEC on a vector pull. With the ROM bit set the flash is chosen and the high register is ignored. All five
  reset to 0, which is the 1 MB the windows had before.
- **MAP** ([`$C6A5`](#reg-c6a5)) holds AP8 in bit 0. While AP8 is set, as it is from every reset, the CPU card sends
  every memory cycle for bank 8 (`$80000–$8FFFF`: a window with ROM clear, WIN*n*H 0 and BLOCK 32–35, and the DMA
  controller's memory side) to the video card's aperture at `$E00000–$E0FFFF`, so software that knows the aperture
  only in bank 8 still finds it there. With AP8 clear, bank 8 is the RAM card's
  ([chapter 2](02-memory-and-banking.md#revision-2-bank-8-the-aperture)). The bridge reads AP8 on every cycle.
- **Software writes them only after reading NB_ID as `$D2`.** The BIOS's boot and Break set WIN0H–WIN3H and VECH to
  0 and write MAP whole: 1 when a revision 2 video card is fitted (`VIDEO_ID` `$92`), else 0, so a machine with no
  such card has bank 8 as RAM ([chapter 3](03-boot.md)). OODOS does the same at its warm boot, keeps AP8 set, and
  clears it only for a program that asks (function 83's `DM_BANK8`). Under OODOS only OOLIB gives a window a high
  register other than 0: WIN1H `$0E`, for slot 1 at `$E04000`.
- **The Console card and the bus spy** see the true map: driving the bus themselves, they find bank 8 the RAM
  card's whatever AP8 holds ([chapter 12](12-front-panel.md)).

## MODE, STATUS and ID

Status: **emulated**

- **MODE** ([`$C685`](#reg-c685)) has one working bit, APPLE. Writing 1 to it enters Apple II mode: the bridge
  switches to the IIe's memory map and clock and pulses /RESET. A /RESET pulse leaves APPLE set; only a hard reset
  clears it. While it is set, the bridge's registers are hidden behind the IIe's I/O
  ([chapter 13](13-apple-ii-mode.md)).
- **STATUS** ([`$C686`](#reg-c686)) says how the machine started: from RAM (/BOOTSEL) or not, and whether the last
  hard reset was the power-on one. Its top four bits are the bridge's revision.
- **WAIT0–WAIT3** ([`$C68A`–`$C68D`](#reg-c68a)) are read/write storage, reserved for a turbo mode: since B2
  (*proposed*) they slow nothing. A slow card asks for wait states by /WAIT, which the bridge turns into RDY for the
  CPU ([chapter 5](05-dma.md#the-controller)). The ROM's diagnostics keep their options in WAIT3.
- **ID** ([`$C6FF`](#reg-c6ff)) reads `$D2`: module D, revision 2, the bridge with WIN0H–WIN3H, VECH and MAP
  (*proposed*, emulated as step B7). A revision 1 bridge reads `$D1` and has none of them; the emulator keeps it as
  a machine-file option. Software writes the new registers only after reading `$D2`. Every block with registers has
  its ID byte at `+$7F` ([chapter 1](01-overview.md#slots-and-module-ids)).

### Turbo

Status: **proposed**

MODE's bit 1, TURBO, is reserved for running the CPU at ÷1, 14.32 MHz, which the design keeps only as a possible
later mode ([hw/design.md, "CPU and clocks"](../../hw/design.md#cpu-and-clocks)). The emulator keeps the bit as
written and does nothing with it.

## The interrupt summary

Status: **emulated**

IRQL ([`$C688`](#reg-c688)) and IRQH ([`$C689`](#reg-c689)) give one bit to each module ID: IRQL to IDs 0–7, IRQH
to IDs 8–F. A bit reads 0 while the card with that ID is pulling /IRQ. Reading either byte is the /IACK poll: the
bridge drives nothing, and each card that is interrupting pulls its own data line low, line ID mod 8 of D0–D7.
[Chapter 4](04-interrupts.md) says which card has which bit, and how a handler uses them.

## The real-time clock

Status: **emulated**

A DS3231 on the CPU card keeps the time, with a CR2032 cell to keep it running with the power off. The bridge owns
it and talks I²C to it, so software never does. Software sees the time in seven registers, a control register, and
a window onto the chip's other registers.

### What the bridge does by itself

Status: **emulated**

- **At power-on**, while /RESET is still held, it sets the DS3231's square wave to 1 Hz and reads the time. The
  first instruction finds SEC–YEAR valid and BUSY clear.
- **Once a second**, on each falling edge of the square wave, it reads the seven time registers and the chip's
  status into SEC–YEAR, LOST and ALARM, and sets TICK. With TIE set, TICK interrupts on IRQH bit 5.
- **One transaction at a time.** BUSY is set from the request until the transaction ends, and its effect lands at
  the end. A transaction takes its bits' time at 400 kHz: 9 bits a byte, and one for each START, repeated START and
  STOP.

  | Transaction | Bits | At 7.16 MHz |
  |---|---|---|
  | the second's read: the time, then the status | 132 | 330 µs, 2,363 cycles |
  | SET: the time, then the status, written | 112 | 280 µs |
  | an RTCDATA read | 39 | 98 µs |
  | an RTCDATA write and its read-back | 68 | 170 µs |

  A square-wave edge that comes while the last second's read is still waiting is missed.

### Reading the time

Status: **emulated**

Read SEC ([`$C690`](#reg-c690)) first, then MIN to YEAR. Reading SEC copies all seven into a latch, and MIN to YEAR
read the latch, so the seven always belong to the same second. The values are the DS3231's own:

- BCD;
- the hour in 24-hour form, unless software has set the chip to 12-hour;
- the day of the week 1–7, with 1 for Sunday;
- the century in bit 7 of MONTH: set means 21xx.

The bridge holds 2000–2199.

### Setting the time

Status: **emulated**

Write SEC to YEAR, then write RTCCTL with SET (bit 1) set and TIE as it should stay. Writes to SEC–YEAR go to a
holding copy, which SET sends; until then the registers still read the running time. BUSY is set from the SET write
until the send is done, and then SEC–YEAR read what was sent. The send restarts the chip's second and clears its
oscillator-stopped flag, so LOST goes off. A second's read, with TICK, follows soon after.

Don't read-modify-write RTCCTL. A TICK that reads 1 and is written back clears TICK. SET reads 0, so writing back
what was read can't send again.

### The chip's other registers

Status: **emulated**

RTCREG ([`$C697`](#reg-c697)) and RTCDATA ([`$C698`](#reg-c698)) reach the DS3231's registers `$00–$12` one at a
time: the alarms, the control and status registers, the aging offset and the temperature. Writing RTCREG reads that
register into RTCDATA. Writing RTCDATA writes the register and reads it back. Either way, wait for BUSY to clear
before reading RTCDATA. There is no auto-increment.

- **The control register** (`$0E`) is written with INTCN and both RS bits cleared, because the square wave belongs
  to the bridge.
- **Alarms** can't use the chip's interrupt pin, because that pin is the square wave's. The bridge reads the status
  register each second, and ALARM in RTCCTL shows its A1F or A2F. Software clears the flag through RTCDATA, and
  ALARM follows when that write ends, as BUSY clears.

### The emulated chip

Status: **emulated**

The emulator counts the DS3231's time in emulated cycles. The chip takes the host's local time once, when the
emulator starts, and keeps it from then on:

- The chip is on its battery through a power cycle: the WebUI's session keeps the same chip when the machine is
  switched off and on, and its time runs on. The battery never runs flat.
- The oscillator-stopped flag starts clear. With the control register's EOSC set, a power-off stops the oscillator
  and sets the flag, so LOST is on at the next power-on, as on the real chip.
- The leap-year rule is the chip's, every fourth year, so 2100 has a 29 February.
- The temperature is always 25.00 °C. The aging register is kept but changes nothing.
- The chip starts in 24-hour form.
- The chip's 250 ms start-up after power-on isn't modelled: it answers at once.
- In Apple II mode the transactions are timed at the Apple clock.
- The WebUI steps the count back into line with the host's clock, by whole seconds, once it is 2 s out.

## Registers

Status: **emulated**

<a id="reg-c680"></a>
### $C680 NB_WIN0: window 0, CPU $0000–$3FFF

#### Equates

`NB`, `NB_WIN0`

#### Module

D, the north bridge

#### Access

R/W. The new mapping applies from the next bus cycle.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | ROM | 1: the flash. Blocks 0–31 are ROM A, 32–63 ROM B, the game cartridge, each reached by its address, with no module ID ([chapter 2](02-memory-and-banking.md#the-24-bit-bus)). 0: RAM |
| 6 | RO | 1: writes through this window are ignored |
| 5–0 | BLOCK | the 16 KB block: physical A14–A19 for RAM, or the 16 KB offset into the flash. With ROM clear, A20–A23 come from [WIN0H](#reg-c6a0) on a revision 2 bridge (*proposed*, B7), 0 on revision 1 |

The CPU's A0–A13 pass through, so CPU `$0000–$3FFF` is physical `WIN0H × $100000 + BLOCK × $4000` on.

#### Reset

`$00`: RAM block 0.

#### Status

**emulated**

#### Notes

`NB_ROM` (`$80`) and `NB_RO` (`$40`) are the two flag bits. See
[chapter 2](02-memory-and-banking.md#the-four-windows).

<a id="reg-c681"></a>
### $C681 NB_WIN1: window 1, CPU $4000–$7FFF

#### Equates

`NB_WIN1`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0](#reg-c680).

#### Bits

As [WIN0](#reg-c680): bit 7 ROM, bit 6 RO, bits 5–0 the block, for CPU `$4000–$7FFF`.

#### Reset

`$01`: RAM block 1.

#### Status

**emulated**

<a id="reg-c682"></a>
### $C682 NB_WIN2: window 2, CPU $8000–$BFFF

#### Equates

`NB_WIN2`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0](#reg-c680).

#### Bits

As [WIN0](#reg-c680): bit 7 ROM, bit 6 RO, bits 5–0 the block, for CPU `$8000–$BFFF`.

#### Reset

`$02`: RAM block 2.

#### Status

**emulated**

#### Notes

The BIOS borrows W2 to copy itself into RAM block 63 at boot, then sets it to block 2. VINIT, setting up the text
screen, borrows W1 for the font (ROM A block 30), which it writes to the video card's port. VPUTC borrows W2 for each
character that ECHO puts on the screen, on the card's aperture slot 0 (*proposed*, revision 2:
[chapter 6](06-video.md#the-bioss-text-screen)): on a `$D2` bridge with WIN2H `$0E` and block 0, `$E00000`; on
`$D1`, block 32. Both put back whatever the windows held before, WIN2H included.

<a id="reg-c683"></a>
### $C683 NB_WIN3: window 3, CPU $C000–$FFFF

#### Equates

`NB_WIN3`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0](#reg-c680).

#### Bits

As [WIN0](#reg-c680): bit 7 ROM, bit 6 RO, bits 5–0 the block. The I/O hole covers `$C000–$C7FF`, so the CPU
reaches the block through W3 at `$C800–$FFFF` only.

#### Reset

`$DF`: ROM A block 31, read-only, the top 16 KB of the flash. With /BOOTSEL held at reset, `$3F`: RAM block 63.

#### Status

**emulated**

#### Notes

The BIOS leaves W3 at `$7F`, RAM block 63, read-only, which holds its own copy. While the CPU reads a vector, VEC
([`$C684`](#reg-c684)) is used instead of W3.

<a id="reg-c684"></a>
### $C684 NB_VEC: the vectors' window

#### Equates

`NB_VEC`

#### Module

D, the north bridge

#### Access

R/W.

#### Bits

As [WIN0](#reg-c680): bit 7 ROM, bit 6 RO, bits 5–0 the block. It maps CPU `$C000–$FFFF` in place of W3 while
the CPU's VPB is low, that is, while it reads the NMI, reset or IRQ vector.

#### Reset

The same as WIN3's: `$DF`, or `$3F` with /BOOTSEL.

#### Status

**emulated**

#### Notes

Only the vector's two bytes come through VEC. The handler is fetched through W3. The BIOS sets VEC to `$7F`, the
same as W3. Ignored in Apple II mode. See [chapter 4](04-interrupts.md#vec-and-the-vectors). On a revision 2
bridge (*proposed*, B7) A20–A23 of a vector pull come from [VECH](#reg-c6a4), not WIN3H, while VEC's ROM bit is
clear; the BIOS and OODOS keep it 0.

<a id="reg-c685"></a>
### $C685 NB_MODE: the machine's mode

#### Equates

`NB_MODE`

#### Module

D, the north bridge

#### Access

R/W. Writing APPLE as 1 pulses /RESET into Apple II mode.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | 0 |
| 1 | TURBO | kept as written; does nothing (proposed: ÷1) |
| 0 | APPLE | write 1: enter Apple II mode. Writing 0 doesn't clear it |

#### Reset

`$00` at a hard reset. A /RESET pulse keeps APPLE.

#### Status

**emulated**

#### Notes

`NB_MODE_APPLE` (`$01`) is the bit. hw/design.md has MODE write-only; the emulator reads it back, though in Apple
II mode the register is hidden. The BIOS's `$C806` sets up the IIe's memory first
([chapter 13](13-apple-ii-mode.md#entering-apple-ii-mode)). Without that set-up, the CPU starts from whatever bank E
holds as the IIe's ROM.

<a id="reg-c686"></a>
### $C686 NB_STATUS: how the machine started

#### Equates

`NB_STATUS`

#### Module

D, the north bridge

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | REV | the bridge's revision: 2 (*proposed*, B7), as NB_ID's low digit; 1 on a revision 1 bridge, the emulator's option |
| 3–2 | | 0 |
| 1 | POWER_ON | 1: the last hard reset was the power-on one |
| 0 | BOOTSEL | 1: /BOOTSEL was held, so the machine booted from RAM |

#### Reset

`$22` after power-on, `$20` after a later hard reset (`$12` and `$10` on a revision 1 bridge). Bit 0 is also set
with /BOOTSEL.

#### Status

**emulated**

#### Notes

`NB_STATUS_BOOTSEL` (`$01`) is bit 0. The BIOS's `$C800` reads it: with /BOOTSEL there is no flash image to go back
to, so it starts the RAM copy afresh instead.

<a id="reg-c687"></a>
### $C687 NB_RTCCTL: the real-time clock's control

#### Equates

`NB_RTCCTL`

#### Module

D, the north bridge

#### Access

R/W. Writing TICK as 1 clears it, and writing SET as 1 sends SEC–YEAR to the chip. TIE takes the value written.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | TICK | 1: a second's read has happened. Write 1 to clear |
| 6 | TIE | 1: TICK interrupts, on IRQH bit 5 |
| 5 | ALARM | 1: the chip's status had A1F or A2F set when the bridge last read it (read-only) |
| 4–3 | | 0 |
| 2 | LOST | 1: the chip's oscillator stopped (OSF), so the time is invalid until set (read-only) |
| 1 | SET | write 1: send the holding copy of SEC–YEAR. A strobe: reads 0 |
| 0 | BUSY | 1: the bridge is talking to the chip (read-only) |

#### Reset

Any reset clears TICK and TIE. ALARM and LOST follow the chip's status. After power-on BUSY is 0.

#### Status

**emulated**

#### Notes

backplane.inc has the bits: `NB_RTC_TICK`, `NB_RTC_TIE`, `NB_RTC_ALARM`, `NB_RTC_LOST`, `NB_RTC_SET` and
`NB_RTC_BUSY`. Write the bits you mean, not what you read ([above](#setting-the-time)). TICK comes every second,
alarm or not; an alarm shows only as ALARM. The BIOS's Break writes `$80`, which clears TICK and turns TIE off.

<a id="reg-c688"></a>
### $C688 NB_IRQL: the interrupt poll, IDs 0–7

#### Equates

`NB_IRQL`

#### Module

D, the north bridge

#### Access

R. Reading brings every card up to date first, so the poll is as of this cycle.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | ID 7 | 0: the mass storage card (the SD card) is interrupting |
| 6 | ID 6 | 0: the serial card, the 6551 or the VIA, is interrupting |
| 5–0 | IDs 5–0 | free (no card: memory answers by its address, [chapter 2](02-memory-and-banking.md#the-24-bit-bus)); read 1 |

#### Reset

`$FF` while nothing interrupts.

#### Status

**emulated**

#### Notes

0 means interrupting: the data lines' pull-ups leave every other bit 1. See
[chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh).

<a id="reg-c689"></a>
### $C689 NB_IRQH: the interrupt poll, IDs 8–F

#### Equates

`NB_IRQH`

#### Module

D, the north bridge

#### Access

R, as [IRQL](#reg-c688).

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | ID F | no module: reads 1 |
| 6 | ID E | 0: the console card |
| 5 | ID D | 0: the bridge's own: RTCCTL's TICK, with TIE |
| 4 | ID C | the Disk II has no interrupt: reads 1 |
| 3 | ID B | 0: the USB card |
| 2 | ID A | 0: the sound card |
| 1 | ID 9 | 0: the video card |
| 0 | ID 8 | 0: the DMA controller: a channel's terminal count, with its MODE's IRQ |

#### Reset

`$FF` while nothing interrupts.

#### Status

**emulated**

<a id="reg-c68a"></a>
### $C68A NB_WAIT0: reserved for a turbo, IDs 0–3

#### Equates

`NB_WAIT0`

#### Module

D, the north bridge

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–6 | ID 3 | reserved |
| 5–4 | ID 2 | reserved |
| 3–2 | ID 1 | reserved |
| 1–0 | ID 0 | reserved |

Each pair is 0–3, kept for a turbo mode's wait states for that module ID. Since B2 (*proposed*) they are storage
only: a write is kept and read back, and slows nothing. Until B1 IDs 0–3 were RAM A–D; on the new bus memory answers
by address and has no ID ([chapter 2](02-memory-and-banking.md#the-24-bit-bus)), so these four IDs have no card.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Before B2 the emulator applied these wait states to the CPU's accesses and the DMA controller's alike, whatever the
clock. Since B2 every wait state comes from a card's /WAIT (in the emulator, a mapping's `wait=`:
[chapter 14](14-emulator-notes.md#backplanemachine)), seen by the CPU and stretching a DMA grant, and WAIT0–3 slow
nothing; hw/design.md keeps them for a turbo mode. Neither the BIOS nor OODOS sets them for wait states. The ROM's
diagnostics keep their options and a stage 1 failure in [WAIT3](#reg-c68d), and stage 7's NB7 writes all four and
reads them back ([chapter 15](15-diagnostics.md)); every /RESET clears them.

<a id="reg-c68b"></a>
### $C68B NB_WAIT1: reserved for a turbo, IDs 4–7

#### Equates

`NB_WAIT1`

#### Module

D, the north bridge

#### Access

R/W, as [WAIT0](#reg-c68a).

#### Bits

Bits 1–0 ID 4, 3–2 ID 5 (both without a card since B1), 5–4 ID 6 (serial), 7–6 ID 7 (storage). Storage only since
B2, as [WAIT0](#reg-c68a).

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c68c"></a>
### $C68C NB_WAIT2: reserved for a turbo, IDs 8–B

#### Equates

`NB_WAIT2`

#### Module

D, the north bridge

#### Access

R/W, as [WAIT0](#reg-c68a).

#### Bits

Bits 1–0 ID 8 (DMA), 3–2 ID 9 (video), 5–4 ID A (sound), 7–6 ID B (USB). Storage only since B2, as
[WAIT0](#reg-c68a).

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c68d"></a>
### $C68D NB_WAIT3: reserved for a turbo, IDs C–F

#### Equates

`NB_WAIT3`

#### Module

D, the north bridge

#### Access

R/W, as [WAIT0](#reg-c68a).

#### Bits

Bits 1–0 ID C (Disk II), 3–2 ID D (the bridge's own registers), 5–4 ID E (console), 7–6 ID F (no module). Storage
only since B2, as [WAIT0](#reg-c68a): the ROM's diagnostics keep their options here
([chapter 15](15-diagnostics.md)).

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c690"></a>
### $C690 NB_SEC: the seconds

#### Equates

`NB_SEC`

#### Module

D, the north bridge

#### Access

R/W. Reading it latches MIN to YEAR. A write goes to the holding copy that SET sends; reads still give the time.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | 0 |
| 6–4 | TENS | 0–5 |
| 3–0 | UNITS | 0–9 |

#### Reset

The time, as the bridge read it at power-on or at the last second.

#### Status

**emulated**

#### Notes

SEC itself reads the time as the bridge last read it from the chip; only MIN to YEAR read the latch. See
[Reading the time](#reading-the-time).

<a id="reg-c691"></a>
### $C691 NB_MIN: the minutes

#### Equates

`NB_MIN`

#### Module

D, the north bridge

#### Access

R/W. Reads give the latch that SEC's read made; a write goes to the holding copy.

#### Bits

As [SEC](#reg-c690): bits 6–4 the tens (0–5), bits 3–0 the units, BCD.

#### Reset

The time.

#### Status

**emulated**

<a id="reg-c692"></a>
### $C692 NB_HOUR: the hour

#### Equates

`NB_HOUR`

#### Module

D, the north bridge

#### Access

R/W, as [MIN](#reg-c691).

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | 0 |
| 6 | 12H | 0: 24-hour; 1: 12-hour |
| 5 | PM / 20 | 12-hour: 1 for PM. 24-hour: the hour's tens are 2 |
| 4 | 10 | the hour's tens are 1 |
| 3–0 | UNITS | 0–9, BCD |

#### Reset

The time.

#### Status

**emulated**

<a id="reg-c693"></a>
### $C693 NB_DAY: the day of the week

#### Equates

`NB_DAY`

#### Module

D, the north bridge

#### Access

R/W, as [MIN](#reg-c691).

#### Bits

All: 1–7, with 1 for Sunday.

#### Reset

The time.

#### Status

**emulated**

#### Notes

The chip counts the day on with the date and doesn't check it against the date. OODOS works the day out from the
date when it sets the time, so the two agree.

<a id="reg-c694"></a>
### $C694 NB_DATE: the day of the month

#### Equates

`NB_DATE`

#### Module

D, the north bridge

#### Access

R/W, as [MIN](#reg-c691).

#### Bits

Bits 5–4 the tens (0–3), bits 3–0 the units, BCD: 1–31.

#### Reset

The time.

#### Status

**emulated**

<a id="reg-c695"></a>
### $C695 NB_MONTH: the month and the century

#### Equates

`NB_MONTH`

#### Module

D, the north bridge

#### Access

R/W, as [MIN](#reg-c691).

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | CENTURY | 0: 20xx; 1: 21xx |
| 6–5 | | 0 |
| 4 | 10 | the month's tens: 1 |
| 3–0 | UNITS | 0–9, BCD: months 1–12 |

#### Reset

The time.

#### Status

**emulated**

<a id="reg-c696"></a>
### $C696 NB_YEAR: the year in the century

#### Equates

`NB_YEAR`

#### Module

D, the north bridge

#### Access

R/W, as [MIN](#reg-c691).

#### Bits

Bits 7–4 the tens, bits 3–0 the units, BCD: 00–99. With MONTH's CENTURY, 2000–2199.

#### Reset

The time.

#### Status

**emulated**

<a id="reg-c697"></a>
### $C697 NB_RTCREG: a DS3231 register's number

#### Equates

`NB_RTCREG`

#### Module

D, the north bridge

#### Access

R/W. Writing it starts a read of that register into RTCDATA, with BUSY set until it is done. Reads give the number
written.

#### Bits

All: the DS3231 register, `$00–$12`. A register past `$12` reads `$FF`.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c698"></a>
### $C698 NB_RTCDATA: a DS3231 register's value

#### Equates

`NB_RTCDATA`

#### Module

D, the north bridge

#### Access

R/W. Reading gives the value the last transaction read. Writing writes RTCREG's register and reads it back, with
BUSY set until it is done.

#### Bits

All: the register's value, as the DS3231 has it. A write to the control register (`$0E`) has INTCN, RS2 and RS1
cleared on the way.

#### Reset

`$00` at power-on. A later reset leaves it as the last read gave it.

#### Status

**emulated**

#### Notes

Wait for BUSY ([RTCCTL](#reg-c687) bit 0) to clear before reading. A write to the status register (`$0F`) updates
LOST and ALARM when it ends.

<a id="reg-c6a0"></a>
### $C6A0 NB_WIN0H: window 0's A20–A23

#### Equates

`NB_WIN0H`

#### Module

D, the north bridge

#### Access

R/W. The new mapping applies from the next bus cycle.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3–0 | HIGH | physical A20–A23 for [WIN0](#reg-c680)'s block, while WIN0's ROM bit is clear |

With ROM clear, CPU `$0000–$3FFF` is physical `HIGH × $100000 + BLOCK × $4000` on, so the window reaches any 16 KB
of the 16 MB. With ROM set the flash is chosen and HIGH is ignored.

#### Reset

`$00` at every reset, hard or a /RESET pulse: the 1 MB of RAM, as on revision 1.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

`$0E` with BLOCK 0–3 reaches the video card's aperture slots at `$E00000–$E0FFFF` (`VIDEO_APER_H`, with
`VIDEO_APERTURE2`, in [`backplane.inc`](../../backplane/bios/backplane.inc)). `$01–$0D` would be RAM past 1 MB: the
machine has none, so reads give `$FF` and writes are lost. A bank 8 address reached with HIGH 0 is sent to the
aperture while [MAP](#reg-c6a5)'s AP8 is set. The BIOS sets WIN0H–WIN3H to 0 at boot and at Break, and APPLE clears
WIN0H–WIN2H before it enters Apple II mode; Apple II mode's map never reads them.

<a id="reg-c6a1"></a>
### $C6A1 NB_WIN1H: window 1's A20–A23

#### Equates

`NB_WIN1H`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0H](#reg-c6a0).

#### Bits

As [WIN0H](#reg-c6a0): bits 3–0 A20–A23 for [WIN1](#reg-c681)'s block, CPU `$4000–$7FFF`, while its ROM bit is clear; bits 7–4 read 0.

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

OOLIB's window (*proposed*, interface 12). After a program's `GX_BANK8`, OOLIB maps the aperture's slot 1 with
WIN1H `$0E` and WIN1 1 (`$E04000`), and puts WIN1H back to 0 with block 1. OODOS saves, clears and restores WIN1H
on the way into the BDOS, the interrupt dispatcher and the hooks, so its own code finds it 0
([the Programmer's Guide, chapter 5](../oodos-programmers-guide/05-memory-and-banking.md#the-w1-contract)).
OODOS's console maps W1 with WIN1H `$0E`, block 0, for each character.

<a id="reg-c6a2"></a>
### $C6A2 NB_WIN2H: window 2's A20–A23

#### Equates

`NB_WIN2H`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0H](#reg-c6a0).

#### Bits

As [WIN0H](#reg-c6a0): bits 3–0 A20–A23 for [WIN2](#reg-c682)'s block, CPU `$8000–$BFFF`, while its ROM bit is clear; bits 7–4 read 0.

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

The BIOS's VPUTC writes WIN2H `$0E` with W2 0 (slot 0) for each character on a `$D2` bridge, and puts both back
([chapter 6](06-video.md#the-bioss-text-screen)).

<a id="reg-c6a3"></a>
### $C6A3 NB_WIN3H: window 3's A20–A23

#### Equates

`NB_WIN3H`

#### Module

D, the north bridge

#### Access

R/W, as [WIN0H](#reg-c6a0).

#### Bits

As [WIN0H](#reg-c6a0): bits 3–0 A20–A23 for [WIN3](#reg-c683)'s block, CPU `$C000–$FFFF`, while its ROM bit is clear; bits 7–4 read 0.

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

The BIOS and OODOS keep it 0: FARCALL and the loader move W3 only.

<a id="reg-c6a4"></a>
### $C6A4 NB_VECH: the vectors' A20–A23

#### Equates

`NB_VECH`

#### Module

D, the north bridge

#### Access

R/W.

#### Bits

As [WIN0H](#reg-c6a0): bits 3–0 A20–A23 for [VEC](#reg-c684)'s block while the CPU reads a vector and VEC's ROM
bit is clear; bits 7–4 read 0. WIN3H is not used for a vector pull.

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

The BIOS and OODOS set it to 0 with WIN0H–WIN3H, and nothing else writes it.

<a id="reg-c6a5"></a>
### $C6A5 NB_MAP: where bank 8 goes

#### Equates

`NB_MAP`

#### Module

D, the north bridge

#### Access

R/W. A change applies from the next bus cycle.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0 |
| 0 | AP8 | 1: every memory cycle the CPU card makes for bank 8, `$80000–$8FFFF` (a window with ROM clear, WIN*n*H 0 and BLOCK 32–35, and the DMA controller's memory side), goes to the video card's aperture at `$E00000–$E0FFFF` instead. 0: bank 8 is the RAM card's |

#### Reset

`$01` at every reset, hard or a /RESET pulse, whatever cards are fitted.

#### Status

**proposed**

Emulated as step B7. Revision 2 only: on a revision 1 bridge the offset reads `$FF` and a write is lost.

#### Notes

`NB_MAP_AP8` (`$01`) is the bit. With AP8 set and no revision 2 video card, bank 8 reads `$FF`: the BIOS writes
MAP whole at boot and at Break, 1 with `VIDEO_ID` `$92`, else 0, and the diagnostics write 0 for their run, so they
test all 64 RAM blocks ([chapter 15](15-diagnostics.md)). OODOS writes MAP the same way at its warm boot and clears
AP8 only for a program that asks, by function 83's `DM_BANK8`
([the Programmer's Guide, chapter 7](../oodos-programmers-guide/07-function-reference.md#bdos-83)). The Console card
and the bus spy, driving the bus themselves, see bank 8 as the RAM card's whatever AP8 holds. Apple II mode's map
ignores it.

<a id="reg-c6ff"></a>
### $C6FF NB_ID: the north bridge's ID

#### Equates

`NB_ID`

#### Module

D, the north bridge

#### Access

R.

#### Bits

All: the type, module D, in bits 7–4, and the revision in bits 3–0: 2 (*proposed*, B7), the bridge with
[WIN0H–WIN3H, VECH and MAP](#the-high-window-registers-and-map); 1 on a revision 1 bridge, which has none of them.

#### Reset

`$D2` (`$D1` on revision 1).

#### Status

**emulated**

#### Notes

`NB_ID_V1` (`$D1`) and `NB_ID_V2` (`$D2`) are the two values. Software writes [WIN0H–WIN3H](#reg-c6a0),
[VECH](#reg-c6a4) and [MAP](#reg-c6a5) only after reading `$D2`: on `$D1` it reaches the aperture by block 32 as
before. NB_ID is read as an ID byte, so a program reads it under SYSINFO's exception only (AGENTS.md).
