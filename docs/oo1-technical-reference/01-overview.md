# 1 Overview

The machine as a whole: the backplane, its slots and the module IDs that decode them, and the clocks. No card is
built yet, so what this book describes is **emulated**, in the emulator ([emulator/](../../emulator/)), or
**proposed**, designed only ([docs/README.md](../README.md#status-tags)). The emulator is the reference: where it
and hw/design.md differ, this book says what the emulator does and notes the difference.

The design record, for the reasons: [hw/design.md, "Overview"](../../hw/design.md#overview),
[hw/modules.md](../../hw/modules.md).

## The backplane

Status: **emulated**

The OO-1 is a W65C02S on a backplane of cards. The CPU card carries the CPU, the north bridge, the clock and a DS3231
real-time clock. The 1 MB of RAM is on the backplane. Everything else is a card in a slot: the flash, the DMA
controller, video, sound, storage, the Disk II, serial, USB and the front panel.

The bus has 20 address lines and 8 data lines. The north bridge drives A14–A19 from the windows, and the CPU drives
A0–A13 ([chapter 2](02-memory-and-banking.md)). Besides those lines and the CPU's own (PHI2, RWB, /RESET, /NMI,
/IRQ, SYNC, VPB), the backplane carries:

| Signal | What it does |
|---|---|
| PS0–PS3 | the module ID of the access, from the north bridge: which card answers |
| /OE | the north bridge's read strobe |
| RDY | wait states, and the CPU held for DMA |
| BE | low floats the CPU off the bus, for the DMA controller (and the console card and the bus spy) |
| /IACK | the interrupt poll ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)) |
| DRQ0–3, /DACK0–3 | a card's request for DMA, and the controller's answer ([chapter 5](05-dma.md)) |
| TC | terminal count, with a transfer's last byte |
| AMODE | Apple II mode is on ([chapter 13](13-apple-ii-mode.md)) |
| /BOOTSEL | boot from RAM, not the flash ([chapter 3](03-boot.md#booting-from-ram-bootsel)) |
| OSC | the 14.31818 MHz master clock |

The emulator models what these signals do: module decode, wait states, DMA taking the bus, the poll, terminal count,
the mode and /BOOTSEL. It doesn't model them as pins.

### The physical bus

Status: **proposed**

The slots are 62-pin card-edge connectors, an ISA-style slot with 31 pins a side. The backplane carries +24 V, and
each card regulates it down to what it needs. The pull-ups and pull-downs are on the backplane, so an empty slot
leaves every line in a known state. The pin assignment is in
[hw/design.md, "Backplane Signals"](../../hw/design.md#backplane-signals) and [hw/modules.md](../../hw/modules.md).

## Slots and module IDs

Status: **emulated**

A card isn't chosen by its slot but by its module ID, the 4-bit number the north bridge puts on PS0–PS3 for each
access. Any card can go in any slot. A card decodes its ID and A0–A6 only. Its registers are the 128 bytes at
`$C000 + ID × $80`, in the I/O hole ([chapter 2](02-memory-and-banking.md#the-io-hole)). Every block with
registers ends in an **ID byte** at `+$7F`: the module ID in bits 7–4 and the card's revision in bits 3–0. The BIOS
reads the ID bytes to find which cards are fitted, and leaves alone a card that doesn't answer. An empty block reads
`$FF`.

| ID | Module | Registers | ID byte | In the emulator |
|---|---|---|---|---|
| 0–3 | RAM A–D, 256 KB each | none | | yes |
| 4 | ROM A, the flash, 512 KB | none | | yes |
| 5 | ROM B, the cartridge, 512 KB | none | | yes, erased unless a IIe ROM is given |
| 6 | serial | `$C300` | none yet | a 6551 stand-in, and the VIA ([chapter 9](09-serial-and-via.md)) |
| 7 | mass storage | `$C380` | `$71` | yes ([chapter 10](10-storage.md)) |
| 8 | DMA controller | `$C400` | `$81` | yes ([chapter 5](05-dma.md)) |
| 9 | video | `$C480` | `$91` | yes ([chapter 6](06-video.md)) |
| A | sound | `$C500` | `$A2` (`$A1` before revision 2: take either by the high nibble) | yes ([chapter 7](07-sound.md)) |
| B | USB | `$C580` | `$B1` | yes ([chapter 8](08-usb.md)) |
| C | Disk II | `$C600` | `$C1` | yes ([chapter 10](10-storage.md#the-disk-ii)) |
| D | the north bridge, on the CPU card | `$C680` | `$D1` | yes ([chapter 11](11-north-bridge-and-rtc.md)) |
| E | console, the front panel | `$C700` | `$E1` | yes ([chapter 12](12-front-panel.md)) |
| F | free | | | |

The serial card's 6551 stand-in is mirrored through its whole block, so its `$C37F` is the 6551's control register,
not an ID. The TL16C552A card that the design has is to have its ID there ([chapter 9](09-serial-and-via.md)).
backplane.inc names each ID byte's value: `SD_ID_V1`, `DMA_ID_V1`, and so on.

## Clocks

Status: **emulated**

One 14.31818 MHz crystal, the Apple II's master clock, sits on the CPU card. Every CPU speed is divided down from it,
and the north bridge picks the divider:

| Divider | PHI2 | For |
|---|---|---|
| ÷2 | 7.15909 MHz, a 139.7 ns cycle | native mode |
| ÷14, with every 65th cycle ÷16 | 1.0205 MHz on average | Apple II mode: 65 cycles a scan line, as the Apple has |

The other cards keep their own clocks: the video card's 25.175 MHz pixel clock, the sound card's SID clock (NTSC or
PAL, as its CTRL says), and the RP2350s on the storage, USB and sound cards. None of them needs to be in step with the CPU.

**In the emulator** the CPU runs at the clock the app sets, and the cards are told that clock, so their timing stays
right: a second of the clock, a frame of video, a sector's time.

- The WebUI offers 1.02, 1.79, 3.58 and 7.16 MHz and Max, and starts at 7.16 MHz.
- The terminal app's `--clock` is 1 MHz unless set. `--clock 0` runs as fast as the host can, with the cards keeping
  7.16 MHz time.
- Apple II mode fixes the clock at 1,020,484 Hz, an even rate. The emulator doesn't make every 65th cycle long; it
  keeps the average.

### ÷1

Status: **proposed**

A turbo mode at ÷1, 14.32 MHz, is kept as a possible later mode only: its 69.8 ns cycle is just under the W65C02S's
rated minimum, and it would leave about 30 ns for each read. MODE's TURBO bit is reserved for it
([chapter 11](11-north-bridge-and-rtc.md#turbo)).
