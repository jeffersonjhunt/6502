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
real-time clock, the DMA controller (*proposed*, [chapter 5](05-dma.md): in the emulator it is one more device on
the bus, module 8, wherever it sits) and ROM A, the flash with the BIOS. The 1 MB of RAM is one card in a slot.
Everything else is a card in a slot too: video, sound, storage, the Disk II, serial, USB, the front panel and the
game cartridge (ROM B).

The bus has 24 address lines, A0–A23, and 16 data lines, D0–D15 (*proposed*: emulated as steps B1, the 24-bit
addresses, and B4, the word cycles on D0–D15; [hw/design.md](../../hw/design.md#what-steps-b1-b7-change)). A byte
travels on D0–D7; in the emulator only a DMA transfer with WIDE makes a word cycle
([chapter 5](05-dma.md#16-bit-transfers)). The north bridge drives A14–A23 from the windows (the ROM bit puts out
the flash's address at `$F00000`), and the CPU drives A0–A13 ([chapter 2](02-memory-and-banking.md#the-24-bit-bus)).
The connector's pins are proposed ([below](#the-physical-bus)). Besides those lines and the CPU's own (PHI2, RWB,
/RESET, /NMI, /IRQ, SYNC, VPB), the backplane carries:

| Signal | What it does |
|---|---|
| PS0–PS3 | the module ID of the access, from the north bridge: which card answers |
| /MEM | low on a memory cycle: the RAM and the flash answer their addresses only then, and no memory answers an I/O cycle (*proposed*, emulated as step B1: [chapter 2](02-memory-and-banking.md#the-24-bit-bus)) |
| /WORD | low on a word cycle, both bytes at an even address; high, as it is pulled up, on a byte cycle (*proposed*; the emulator models it as its word cycles, B4) |
| /OE | the read strobe, from the north bridge, or from whichever master holds the bus |
| RDY | the CPU held: by the DMA controller while it has the bus, by the console card to halt and step it, and by the north bridge for a card's /WAIT |
| BE | low floats the CPU off the bus, for the DMA controller (and the console card and the bus spy) |
| /IACK | the interrupt poll ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)) |
| /DREQ | a card's request for DMA, one open-drain line all cards share; the controller polls to find who asks (*proposed*, [chapter 5](05-dma.md#transfers-for-a-card)) |
| /DMA | low in the DMA controller's poll and grant cycles; in a grant, the card named on PS0–PS3 moves its data register's byte, or a word with WIDE (*proposed*, B4: [chapter 5](05-dma.md#16-bit-transfers)) |
| /WAIT | a slow card's wait states, open drain: the north bridge turns it into RDY for the CPU, and the DMA controller stretches its grant by it (*proposed*) |
| TC | terminal count, with a transfer's last byte (or its last word, with WIDE: *proposed*, B4) |
| AMODE | Apple II mode is on ([chapter 13](13-apple-ii-mode.md)) |
| /BOOTSEL | boot from RAM, not the flash ([chapter 3](03-boot.md#booting-from-ram-bootsel)) |
| OSC | the 14.31818 MHz master clock |
| BR0–BR2 | reserved for a bridged bus later: routed to every slot, no card connects them (*proposed*) |
| +5 V and ground | power: 5 V only |

DRQ0–DRQ3 and /DACK0–/DACK3, a request and acknowledge pair for each of four channels on the first design's 62-pin
bus, are gone: /DREQ and /DMA replace them, with the controller's poll.

The emulator models what these signals do: module decode, memory by address, wait states by /WAIT, DMA taking the
bus, the interrupt poll, the DMA controller's poll and grant, word cycles, terminal count, the mode and /BOOTSEL. It
doesn't model them as pins.

### The physical bus

Status: **proposed**

The slots take the 98-contact card edge that PCIe x8 uses, 49 contacts a side, but the bus is the OO-1's own:
each slot is shrouded and keyed so that no other card fits. The backplane carries 5 V only, and a card that needs
another voltage makes it on the card. There are about 15 slots, one for each card that can exist and the CPU card,
on a passive bus at 7.16 MHz. The pull-ups are on the backplane, so an empty slot leaves its lines in a known
state, and a cycle that no master drives, as one master hands the bus to another with BE, is a read that selects
no memory, no card's registers (PS0–PS3 are pulled up to `0xF`, the idle bus's ID, which no card is given) and no
poll. A pull-up alone is slow to raise a line on a loaded bus, so the Console card and the bus spy leave none of
the lines they drive to it. In the cycle it pulls BE low, such a master enables its own drivers, with /MEM and /OE
high, RWB a read and PS0–PS3 `0xE`, its ID. Before it releases BE, it drives /MEM and /OE inactive (high), RWB to
read, PS0–PS3 to `0xF` and /WORD high if it drives /WORD, for at least one cycle; the pull-ups then only hold lines
that are already high. /IACK and /DMA stay the CPU card's. The DMA controller, on the CPU card, needs no such cycle
when it releases BE: the CPU card drives the lines again at once.

The signals are decided; where each sits is not. [hw/modules.md](../../hw/modules.md) has a draft pin-out (67 lines,
6 +5 V contacts, 23 grounds and 2 spares), every position in it open; the positions, the socket and the shroud's
key are the owner's. The signals are in
[hw/design.md, "Backplane Signals"](../../hw/design.md#backplane-signals). The first design's 62-pin ISA-style
slot, with +24 V, is kept for the record in
[hw/design.md](../../hw/design.md#the-62-pin-bus-the-first-designs).

## Slots and module IDs

Status: **emulated**

A card isn't chosen by its slot but by its module ID, the 4-bit number the north bridge puts on PS0–PS3 for each
access. Any card can go in any slot. A card decodes its ID and A0–A6, with /MEM, /DMA and /IACK high for a
register access ([hw/design.md](../../hw/design.md#the-physical-map-on-the-new-bus)); nothing more. Its registers
are the 128 bytes at `$C000 + ID × $80`, in the I/O hole ([chapter 2](02-memory-and-banking.md#the-io-hole)). Every block with
registers ends in an **ID byte** at `+$7F`: the module ID in bits 7–4 and the card's revision in bits 3–0. The BIOS
reads the ID bytes to find which cards are fitted, and leaves alone a card that doesn't answer. An empty block reads
`$FF`.

| ID | Module | Registers | ID byte | In the emulator |
|---|---|---|---|---|
| 0–5 | free (once RAM A–D, ROM A and ROM B: memory answers by address, [chapter 2](02-memory-and-banking.md#the-24-bit-bus)) | none | | their blocks read `$FF` |
| 6 | serial | `$C300` | none yet | a 6551 stand-in, and the VIA ([chapter 9](09-serial-and-via.md)) |
| 7 | mass storage | `$C380` | `$71` | yes ([chapter 10](10-storage.md)) |
| 8 | DMA controller, on the CPU card | `$C400` | `$82` (revision 2, *proposed*) | yes ([chapter 5](05-dma.md)) |
| 9 | video | `$C480` | `$92` | yes ([chapter 6](06-video.md)) |
| A | sound | `$C500` | `$A3` | yes ([chapter 7](07-sound.md)) |
| B | USB | `$C580` | `$B1` | yes ([chapter 8](08-usb.md)) |
| C | Disk II | `$C600` | `$C1` | yes ([chapter 10](10-storage.md#the-disk-ii)) |
| D | the north bridge, on the CPU card | `$C680` | `$D3` (revision 3, *proposed*: WINnH, VECH and MAP, the timebase, the random number chip and the common hole) | yes ([chapter 11](11-north-bridge-and-rtc.md)) |
| E | console, the front panel | `$C700` | `$E1` | yes ([chapter 12](12-front-panel.md)) |
| F | reserved: the idle bus's ID, what PS0–PS3's pull-ups give a cycle no master drives (*proposed*); never a card's | none | | its block reads `$FF` |

Memory has no module ID: the RAM card, 1 MB at `$000000`, whose four 256 KB quarters keep the names RAM A–D;
ROM A, the flash on the CPU card, 512 KB at `$F00000`, holding the BIOS and, since B5 (*proposed*), the IIe images
in blocks 0–5, two chips that can be written (emulated as step B5); and ROM B, the game cartridge, 512 KB at
`$F80000`, erased. All three are in the emulator.

The serial card's 6551 stand-in is mirrored through its whole block, so its `$C37F` is the 6551's control register,
not an ID. The TL16C552A card that the design has is to have its ID there ([chapter 9](09-serial-and-via.md)).
backplane.inc names each ID byte's value: `SD_ID_V1`, `DMA_ID_V2`, and so on.

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
