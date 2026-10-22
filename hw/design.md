# System Design #

Draft, 2026-10-21. The architecture of the backplane computer as agreed so far. Each section separates what is
**decided** from what is **proposed** (a starting point, open to change); open questions are collected at the end.
Card pin-outs and the module ID list are in [modules.md](modules.md), the front panel in [console.md](console.md).


## Goals ##

- A W65C02S at **7.15909 MHz**, with 14.31818 MHz as a possible later turbo mode. It has **1 MB of RAM** on the
  backplane, with the system software (BIOS, Monitor, BASIC, DOS) living at the top of RAM.
- Banking with as few bits as possible.
- Storage and I/O that **support DMA**: every card can be driven by the CPU through its registers, and can also
  move data by DMA.
- VGA video with text, bitmap, tile and sprite modes; stereo sound from two 6581 SIDs.
- USB keyboard and mouse, a game port, two serial ports (COM1, COM2) and a parallel port.
- An **Apple II mode**, selected by a register, that runs unaltered software for a stock enhanced Apple IIe with
  the extended 80-column card (128 KB). Software that needs any other hardware is out of scope; the one exception is
  the Disk II, needed to load anything at all.


## Overview ##

```
                 +-----------+     +--------------+
14.31818 MHz -->|  clock    |     |              |
    crystal      |  divider  |---->|  W65C02S     |  A0-A15, D0-D7, RWB, SYNC, VPB
                 | /1 /2 /14 | PHI2|  CPU card    |-----------+
                 |           |     +--------------+           |
                 +-----------+                                v
                                                    +------------------+
                                                    |  north bridge    |  CPU address -> 20-bit address
                                                    |  (CPLD)          |  20-bit address -> module ID (PS0-PS3)
                                                    +------------------+
                                                              |
  ======================== backplane: A0-A19, D0-D7, PS0-PS3, RWB, RDY, BE, DRQ/DACK ... ========================
       |           |          |          |          |           |           |           |            |
     RAM A-D     ROM A     DMA ctl    video      sound      Disk II      SD       COM1/COM2/   USB/game    bus spy
     (1 MB)     (flash)    (4 ch)     (VGA)     (2x SID)    (GoTek)    storage     LPT          port      (ESP32)
```


## CPU and clocks ##

**Decided: one oscillator for the CPU,** a 14.31818 MHz crystal (the Apple II's master clock). Every CPU speed is
divided down from it:

| Divider | PHI2 | Cycle | Used for |
|---|---|---|---|
| ÷2 | 7.15909 MHz | 139.7 ns | **native mode** |
| ÷1 | 14.31818 MHz | 69.8 ns | a possible later "turbo" mode, not planned for |
| ÷14, every 65th cycle ÷16 | about 1.0205 MHz average | 978 ns (1,117 ns long cycle) | Apple II mode: 65 cycles per scan line, 17,030 per frame, exactly as the Apple |

- **Decided: the design targets ÷2.** At 140 ns per cycle the read budget is about 100 ns. Flash, RAM on the
  backplane, logic analysers and long wires all work with no wait states.
- **÷1 is a "future maybe".** Its 69.8 ns cycle is 0.2 ns, about 0.3%, under the datasheet's 70 ns minimum, which
  parts typically manage but WDC doesn't guarantee. It also leaves only about 30 ns for each read (below), which RAM
  on the backplane probably can't meet. Nothing in this design depends on it.

**Proposed**

- **A synchronous divider** on the clock card, selected by the north bridge's mode register (and, if turbo is ever
  added, a speed bit). Every speed comes from the same crystal, so switching is just changing the divider at a
  cycle boundary while PHI2 is low. The divider never produces a short pulse, so it needs no clock multiplexer.
- **Clock lines on the backplane.** The OSC line (B30) carries the 14.31818 MHz master at all times. In Apple mode
  the Disk II card divides it by 7 to get its 2 MHz state-machine clock, locked to PHI2 as on the real Apple.
- **Other cards keep their own clocks.** The video card's 25.175 MHz VGA pixel clock and the sound card's SID clock
  are separate oscillators on those cards. Neither needs to be in step with the CPU.

**Timing.** These are WDC's 5 V figures:

| Parameter | Value |
|---|---|
| tADS (address valid after PHI2 falls) | 30 ns max |
| tDSR (read data setup) | 10 ns min |
| tMDS (write data valid after PHI2 rises) | 25 ns max |
| tCYC (cycle time) | 70 ns min |

From the address becoming valid to read data being needed is the cycle time minus 40 ns:

- **At ÷2: about 100 ns.** That covers the north bridge's window lookup and module decode, transceivers in both
  directions, a backplane crossing, and 55–70 ns RAM or flash, with margin.
- **At ÷1: about 30 ns.** Only a fast CPLD and 10 ns SRAM close to the CPU would fit.

**Decided: RAM lives on the backplane.**

**Slow cards** hold the CPU with RDY wait states. At ÷2 that's mainly the sound card.


## Memory ##

### Physical memory: 1 MB in 16 banks ###

The north bridge turns every CPU access into a 20-bit physical address (A0–A19), so the 1 MB is 16 banks of 64 KB.

**Proposed physical layout**

| Bank | Physical | Native mode | Apple II mode |
|---|---|---|---|
| 0 | `$00000–$0FFFF` | main memory | IIe main 64 KB |
| 1 | `$10000–$1FFFF` | user memory | IIe auxiliary 64 KB |
| 2 | `$20000–$2FFFF` | user memory | language card RAM, main |
| 3 | `$30000–$3FFFF` | user memory | language card RAM, auxiliary |
| 4 | `$40000–$4FFFF` | user memory | the main language card's second `$D000` bank (at `$4D000`) |
| 5 | `$50000–$5FFFF` | user memory | the auxiliary language card's second `$D000` bank (at `$5D000`) |
| 6–7 | `$60000–$7FFFF` | user memory, RAM disk | unused |
| 8–B | `$80000–$BFFFF` | video memory (snooped by the video card) | unused |
| C–D | `$C0000–$DFFFF` | user memory, RAM disk | unused |
| E | `$E0000–$EFFFF` | Apple IIe ROM image | IIe ROM (`$C100–$FFFF`), write-protected |
| F | `$F0000–$FFFFF` | BIOS, Monitor, BASIC, DOS, write-protected | unused |

### Native mode: four windows of 16 KB ###

**Decided.** The CPU's 64 KB is four windows of 16 KB. Each window has a **6-bit register** naming any of the 64
16 KB blocks in the 1 MB:

- the register's top 4 bits become A16–A19;
- its bottom 2 bits replace A14–A15;
- A0–A13 pass straight through.

Four bits is the minimum for choosing a bank. The extra two let any 16 KB of the 1 MB appear in any window, so a
program can look at the first 16 KB of the screen without giving up its zero page and stack.

**Decided**

- **Reset state:**

  | Window | CPU range | Maps | Physical |
  |---|---|---|---|
  | W0 | `$0000–$3FFF` | RAM block 0 | `$00000` |
  | W1 | `$4000–$7FFF` | RAM block 1 | `$04000` |
  | W2 | `$8000–$BFFF` | RAM block 2 | `$08000` |
  | W3 | `$C000–$FFFF` | ROM A block 31, read-only: the top 16 KB of the flash | module ID 4 |

  The machine wakes up looking like an ordinary 64 KB computer with the BIOS at the top. When /BOOTSEL is held at
  reset, W3 starts at RAM block 63 (`$FC000`, the top of bank F) instead. See [Boot](#boot).
- **Vectors.** While VPB is low (the CPU reading an interrupt or reset vector), the north bridge maps
  `$C000–$FFFF` through the VEC register instead of W3. VEC resets to the same value as W3, so vectors come from
  the flash until the BIOS moves them to RAM, and an OS can point VEC at its own block. The first instructions of
  each handler must still be in a fixed place: either in W3 with W3 left alone, or in a small common area the
  handler switches through.
- **ROM space.** A window can map flash instead of RAM: blocks 0–31 of ROM A (module ID 4, 512 KB) or of ROM B, the
  game cartridge (module ID 5). A cartridge appears in the CPU's view without being copied.
- **Write protection.** Each window has a read-only bit; writes through it are ignored. It guards against stray
  writes, not deliberate ones: software can clear the bit, or reach the same block through another window.
- **I/O.** The north bridge decodes a fixed I/O area at `$C000–$C7FF` ahead of the windows, so it appears whatever
  the windows hold. It's the same place as the Apple's I/O, so there's one I/O hole in both modes.

### North bridge registers ###

**Decided.** In native mode each module ID owns 128 bytes of the I/O area, at `$C000 + ID × $80`; the 16 IDs
fill `$C000–$C7FF` exactly. A card matches its module ID on PS0–PS3 and decodes only A0–A6. Memory-only modules
(RAM A–D, ROM A and B) leave their blocks unused. Every block with registers ends in an **ID byte at `+$7F`**,
giving the card type and revision, so the BIOS can probe which cards are fitted.

The north bridge isn't a card, so it takes the spare ID `0xD`: its registers are at **`$C680–$C6FF`**. While the
CPU accesses them, the north bridge puts `0xD` on PS0–PS3, so no card answers. In Apple mode the area belongs to
the IIe (`$C680` is slot 6's ROM), and the north bridge registers are hidden until a hard reset.

**Window format.** WIN0–WIN3 and VEC share one layout:

| Bit | Name | Meaning |
|---|---|---|
| 7 | ROM | 1 = flash. Blocks 0–31 are ROM A; 32–63 are ROM B (the cartridge) |
| 6 | RO | 1 = writes through this window are ignored |
| 5–0 | BLOCK | which 16 KB block: A14–A19, or the offset into the flash |

**Register map** (offset from `$C680`):

| Offset | Name | Access | Purpose | Reset value |
|---|---|---|---|---|
| `$00`–`$03` | WIN0–WIN3 | read/write | the four windows | `$00`, `$01`, `$02`, `$DF` (ROM, RO, block 31) |
| `$04` | VEC | read/write | the mapping of `$C000–$FFFF` while VPB is low | `$DF` |
| `$05` | MODE | write | bit 0 APPLE: writing 1 enters Apple II mode (Apple map, clock ÷14, pulse /RESET); only a hard reset clears it. Bit 1 TURBO: reserved for ÷1 | `$00` |
| `$06` | STATUS | read | bit 0: booted from RAM (/BOOTSEL); bit 1: the last reset was power-on; bits 4–7: north bridge revision | |
| `$08` | IRQL | read | interrupt poll for IDs 0–7, using /IACK. 0 = interrupting | |
| `$09` | IRQH | read | interrupt poll for IDs 8–F | |
| `$0A`–`$0D` | WAIT0–WAIT3 | read/write | wait states the north bridge inserts for each module ID, 2 bits per ID. Reserved for turbo; unused at ÷2 | `$00` |
| `$7F` | ID | read | north bridge type and revision | |

With /BOOTSEL held at reset, WIN3 and VEC start at `$3F` (RAM block 63) instead of `$DF`.

### Apple II mode ###

In Apple mode A0–A15 pass straight through and the north bridge supplies only A16–A19, following the IIe's own
rules. This is the IIe's design: auxiliary memory is the same addresses in bank 1.

- **RAMRD, RAMWRT** (`$C002–$C005`): bank 0 or 1 for `$0200–$BFFF`, separately for reads and writes.
- **ALTZP** (`$C008/$C009`): bank 0 or 1 for `$0000–$01FF`, and main or auxiliary language card for
  `$D000–$FFFF`.
- **80STORE with PAGE2** (`$C000/$C001`, `$C054/$C055`), plus **HIRES** (`$C056/$C057`): bank 0 or 1 for text
  page 1 and, with HIRES on, hi-res page 1.
- **Language card** (`$C080–$C08F`): RAM in bank 2 (main) or bank 3 (auxiliary), or ROM from bank E; read and
  write selection with the two-read rule for write enable. The second 4 KB `$D000` bank is at `$D000` of bank 4
  (main) or bank 5 (auxiliary). That keeps every Apple-mode address a pass-through of A0–A15, so the north bridge
  only ever drives A16–A19.
- **I/O:** `$C000–$C0FF` is the IIe's soft switches, keyboard, speaker and game port.
- **Slot ROMs:** `$C100–$CFFF` comes from the ROM image in bank E, as the IIe's internal ROM (INTCXROM, SLOTC3ROM),
  or from a slot card's ROM.
  - Only slot 6 has a card, the Disk II controller. Its boot ROM appears at `$C600`.
- **Status:** the switch states read back at `$C011–$C01F`. `$C019`, the vertical blank flag, is driven by the
  video card in step with the Apple timing.

This is too much for a 74HC670 register file, which would also leave no room for a turbo mode. The north bridge
is a CPLD, for example an ATF1508 (5 V), holding:

- the native window registers;
- the Apple switch logic;
- the module decode;
- the mode register.

### Module decode ###

The north bridge works in two stages:

1. CPU address to physical address, using the windows or the Apple rules. Only the CPU goes through this.
2. Physical address to module ID (PS0–PS3), plus the I/O decode. Everything goes through this, including DMA.

The DMA controller supplies its own 20-bit addresses, so DMA always sees physical memory and never depends on the
window registers.


## Boot ##

**Decided: both paths.** The ROM is the normal way to boot. When it's fitted, the ESP32 can override it.

**From ROM (normal)**

1. At reset W3 and VEC map the top block of the flash (ROM A, module ID 4), read-only. At ÷2, 55–70 ns flash runs
   with no wait states. A turbo mode would need one or two.
2. The BIOS points W2 at RAM block 63 and copies itself from W3 into W2.
3. It writes `$7F` to WIN3 (RAM block 63, read-only). The code it is running is now the RAM copy at the same
   addresses, so execution carries on without a jump. It then sets VEC to match.
4. It copies the rest of the system software into bank F, and the Apple IIe ROM image into bank E, through a window
   set to ROM space. The CPU does the copying, because the DMA controller only reaches RAM; 64 KB takes about
   0.1 s, once per boot.
5. From then on the system software runs from RAM. That's the "top of RAM" requirement. It also leaves the flash
   free to be reprogrammed while the machine runs, and keeps a turbo mode possible.
6. The flash can be reprogrammed from software, so the BIOS can be updated in place (for example, an SST39SF040).

**From the ESP32 (override)**

1. The bus spy holds /RESET low; a held W65C02S stays off the bus.
2. It writes an image into RAM over the backplane. The image can come from its flash, an SD card, Wi-Fi or USB.
3. It drives /BOOTSEL, so the north bridge starts with W3 and VEC on RAM block 63 instead of the flash.
4. It releases /RESET, and the CPU starts from the image in RAM.

**What the override is for:**

- Firmware development in seconds, with no flash programming.
- Recovery from a bad flash.
- Booting test images and diagnostics.
- Loading programs straight into RAM.
- Automated hardware tests. The ESP32 loads a test, runs it, and captures the bus trace. The trace is compared
  with the emulator's using `tools/trace_diff.py`, in the format defined by
  [emulator/docs/BUS_TRACE.md](../emulator/docs/BUS_TRACE.md).

**Hardware it needs.** The bus spy card becomes able to drive the bus:

- drivers for A0–A19, D0–D7 and RWB;
- level shifting between the ESP32's 3.3 V and the bus's 5 V;
- control of /RESET.

The machine works without it, so it can come later.


## DMA ##

**Decided.** A central DMA controller in the style of the IBM PC's 8237.

- **What a card needs:** only a request line (DRQ), an acknowledge line (DACK), and a data register. That keeps
  DMA cheap enough for every card to have it.
- **Placement:** on its own card, module ID `0x8`.
- **Channels:** four, each with its own DRQ and DACK line. Priority is fixed, with channel 0 highest; it goes to
  storage.
- **Registers per channel:**
  - a 20-bit address;
  - a 16-bit count;
  - a mode byte giving the direction, the address step (up, down or fixed) and the transfer mode.
- **Transfer modes:**
  - single: one byte per request, for serial ports;
  - demand: continues while DRQ is held;
  - block: the whole count in one go, for storage and video.
- **Taking the bus:**
  1. The controller pulls RDY low, which stops the W65C02S, including on write cycles.
  2. It pulls BE low, which floats the CPU's address, data and RWB lines.
  3. It drives A0–A19 and RWB itself and asserts the channel's DACK.
  4. The card reads or writes the data bus in the same cycle (a fly-by transfer).

  That's one bus cycle per byte, up to about 7 MB/s.
- **Memory to memory:** two cycles per byte, about 3.5 MB/s.
- **End of transfer:**
  - an interrupt on /IRQ when a count runs out (terminal count);
  - a status bit for each channel;
  - an optional TC backplane line, so the card knows its transfer is done.
- **Apple mode:** the controller is held idle. Unaltered software never programs it, and stolen cycles would break
  code timed to the cycle.

### DMA registers ###

**Decided.** The controller's block is `$C400–$C47F` (module ID `0x8`). Each channel has 16 bytes at
`$C400 + channel × $10`; the registers shared by all channels start at `$C440`, and the rest is reserved.

**Per channel** (offset from the channel's base):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$0`–`$2` | ADDR | read/write | 20-bit physical address, low byte first; only the low 4 bits of `$2` are used. Writing sets the start value; reading returns the current address |
| `$4`–`$5` | COUNT | read/write | bytes to move, low byte first; 0 means 65,536. Reading returns how many are left |
| `$6` | MODE | read/write | see below |
| `$7` | CTRL | read/write | bit 0 ENABLE: writing 1 arms the channel; the hardware clears it at terminal count unless AUTO is set. Bit 1 REQ: writing 1 is a software request, which starts a memory-to-memory transfer or a block transfer with no DRQ |
| `$8`–`$A` | DEST | read/write | 20-bit destination address, for memory to memory only |

**MODE:**

| Bits | Name | Values |
|---|---|---|
| 1–0 | XFER | 00 single, 01 demand, 10 block, 11 reserved |
| 3–2 | DIR | 00 card to memory, 01 memory to card, 10 memory to memory, 11 reserved |
| 4 | DEC | 1 = the address counts down |
| 5 | HOLD | 1 = the address stays fixed. On a memory-to-memory source, this fills memory with one byte |
| 6 | AUTO | 1 = at terminal count, reload ADDR and COUNT and stay armed, for ring buffers and sample streams |
| 7 | IRQ | 1 = interrupt at terminal count |

**Shared** (`$C440`):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$40` | STATUS | read; write 1 to clear | bits 0–3: terminal count reached on channel 0–3, also the interrupt source. Bits 4–7: DRQ0–DRQ3 asserted now (read-only) |
| `$41` | CONTROL | read/write | bit 0: master enable. Bits 4–6 BURST: demand and block transfers give the CPU one cycle back every 2^BURST bytes; 7 (the reset value) means no limit, and the OS chooses |
| `$42` | ABORT | write | bit n stops channel n at once; ADDR and COUNT keep the values they stopped at |
| `$7F` | ID | read | card type and revision |

**Behaviour:**

- **Fly-by.** While /DACKn is low the card needs no address decode. For card to memory it drives its data register
  onto the bus; for memory to card it latches the byte at the falling edge of PHI2. The north bridge decodes the
  controller's address to a RAM module as usual.
- **Taking the bus** costs about one cycle before the first byte (RDY low, then BE low).
- **Single mode** gives the CPU one cycle back after every byte, as the 8237 does.
- **Memory to memory** reads each byte into a latch and then writes it: two cycles per byte, 64 KB in about 18 ms at
  7.16 MHz. Any channel can do it with its own DEST, so storage on channel 0 is never tied up (the 8237 pairs
  channels 0 and 1 instead).
- **Terminal count.** TC is asserted during the last byte's cycle together with its /DACK, the channel's STATUS bit
  is set, and if its IRQ bit is set, bit 0 of IRQH (ID 8) reads 0.
- **Programming.** Registers are written a byte at a time with the channel disabled, so a half-written address
  never matters. Setting ENABLE commits.
- **Channels (proposed):** 0 mass storage (SD), 1 serial and parallel, 2 USB or the Disk II's native side, 3 general
  use (memory to memory and blits).

The front panel's Console card is a bus master too: EXAM and DEP take the bus the same way the DMA controller does.
See [Front Panel](#front-panel).


## Video ##

**Decided**

- **Output:** a single VGA timing, 640×480 at 60 Hz, for every mode. 60 Hz matches the Apple II's frame rate.
- **Snooping:** the card refreshes the screen from its own memory, which it fills by snooping the backplane.
  - Each write to a display region is copied into the card's memory as it happens.
  - The CPU, and DMA, write the screen as ordinary system RAM, and programs can read it back.
  - No CPU cycles are taken, so Apple II timing is undisturbed.
  - The PC never refreshed video by DMA either; a 640×480 refresh would need more than twice what the bus can
    carry.
- **Native use is mostly an OS and games,** so the card has tiles and sprites.
- **Bitmaps are 256 or 512 pixels wide,** so each row is one 6502 page: y is the address's high byte and x the low
  byte.

**Proposed: layers.** The card draws two background layers plus sprites. Each layer is one of these types:

| Type | Format | Memory |
|---|---|---|
| Text | 80×30 or 40×30 cells, 8×16 font, one character byte and one attribute byte each (16 foreground and 16 background colours); the 4 KB font is in RAM and can be redefined | 4.8 KB at 80×30 |
| Tile | 8×8 or 16×16 tiles at 4 or 8 bits per pixel; map up to 64×64 entries of 2 bytes (10-bit tile number, 4-bit palette, H and V flip); fine scroll with wrap-around | map up to 8 KB, plus tiles |
| Bitmap 256 | 256×240, 8 bits per pixel, shown doubled at 512×480 | 60 KB, one bank |
| Bitmap 512 | 512×480, 4 bits per pixel | 120 KB, two banks |
| Mono | 640×480, 1 bit per pixel | 37.5 KB |

**Sprites:**

- 64 of them, each 8, 16, 32 or 64 pixels on a side, at 4 or 8 bits per pixel.
- An 8-byte attribute entry each, 512 bytes in all: position, image, palette, flips, and priority against the two
  layers.
- About 32 per scan line.
- A collision flag and interrupt.

**Common to every mode:**

- a 256-entry palette of 12-bit RGB (decided);
- a display base register (one write to flip between buffers);
- fine scroll on each layer;
- a border colour;
- interrupts at vertical blank and at a chosen scan line.

**Where the data lives.** All video data (maps, tiles, sprite tables, bitmaps) is in banks 8–B, 256 KB, which the
card mirrors. A game keeps its sprite table anywhere in RAM and copies it into the video banks with one 512-byte
DMA during vertical blank, about 72 µs, as the NES does. Storage can load tiles and pictures straight into the video
banks.

**Hardware.** The card needs an FPGA: it must capture an address and data every 140 ns (every 70 ns in turbo),
and draw layers and sprites at the VGA pixel clock (25.175 MHz). The Commander X16's VERA is a useful reference.
The difference is that VERA's memory is reached through I/O ports, while here the screen is ordinary memory.

**Apple II mode.** The card snoops what the IIe's video reads, in banks 0 and 1:

- text and low-res pages 1 and 2 (`$0400–$0BFF`);
- hi-res pages 1 and 2 (`$2000–$5FFF`);
- the display switches `$C050–$C05F` and `$C00C–$C00F`. These change on reads as well as writes, so the card
  watches both.

It draws:

- 40- and 80-column text, low-res, hi-res, double low-res, double hi-res, and mixed mode;
- flashing characters, the alternate character set and MouseText;
- NTSC colour artifacts.

Apple graphics are 280×192, or 560×192 in double modes, and double to 560×384, centred in 640×480. The
Pico-based Apple II VGA cards (AppleII-VGA, A2DVI) work the same way.

**Limit:** the display is not locked to the CPU clock. Effects that switch modes at an exact cycle mid-frame
("vapor lock", raster splits timed by cycle counting) won't display correctly. Ordinary software is unaffected.

### Video registers ###

**Decided.** The card's block is `$C480–$C4FF` (module ID `0x9`). The registers only set up the display; all
display data (text, maps, tiles, bitmaps, sprite table, palette) is in the snooped video banks 8–B.

- **Base registers** are one byte in **1 KB units** of video memory: physical address `$80000 + base × 1024`.
  Sprite images use 12-bit addresses in 64-byte units.
- **Latching.** Base and scroll registers take effect at the next vertical blank, so one write flips buffers without
  tearing.
- In Apple mode the block is hidden; the card follows AMODE and draws the Apple modes from banks 0 and 1.

**Global** (offset from `$C480`):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00` | CTRL | read/write | bit 0 display on; bit 1 layer 0 on; bit 2 layer 1 on; bit 3 sprites on |
| `$01` | BORDER | read/write | border colour (palette index) |
| `$02` | PALBASE | read/write | palette location: 256 entries × 2 bytes |
| `$03` | SPRBASE | read/write | sprite table location: 64 entries × 8 bytes |
| `$04` | IEN | read/write | interrupt enables: bit 0 vertical blank, bit 1 scan line match, bit 2 sprite collision |
| `$05` | ISR | read; write 1 to clear | the same bits, pending; any of them pulls bit 1 of IRQH (ID 9) low |
| `$06`–`$07` | LINE | read/write | write: the 10-bit scan line for the line interrupt. Read: the line being drawn now |
| `$08` | STATUS | read | bit 7 in vertical blank; bits 0–3 COLL, the collision groups in the last collision |
| `$7F` | ID | read | card type and revision |

**Layers** (layer 0 at `$10`, layer 1 at `$20`):

| Offset | Name | Purpose |
|---|---|---|
| `+$0` | CONFIG | bits 2–0 TYPE: 0 text 80×30, 1 text 40×30, 2 tile, 3 bitmap 256, 4 bitmap 512, 5 mono. Bit 3 tile size 8 or 16. Bit 4 tiles at 4 or 8 bpp. Bits 5–6 map size, 32 or 64 entries each way. Bit 7 text: attribute bit 7 blinks (as on the PC) instead of a bright background |
| `+$1` | MAPBASE | text: character and attribute buffer. Tile: the map. Bitmap or mono: the image |
| `+$2` | TILEBASE | tile: tile images. Text: the font |
| `+$3`–`$4` | HSCROLL | 10 bits, wraps |
| `+$5`–`$6` | VSCROLL | 10 bits, wraps |
| `+$7` | PALOFS | added to the high 4 bits of 4 bpp pixels, text colours and mono pixels: any 16-colour slice of the palette |

**Formats in video memory:**

- **Palette entry:** 2 bytes, `GGGGBBBB` then `0000RRRR`. Colour 0 is transparent in layers and sprites. The palette
  is ordinary snooped memory, so a DMA during vertical blank fades it and a PALBASE write swaps it; the FPGA copies
  the 512 bytes into its own RAM when PALBASE changes.
- **Sprite entry** (8 bytes, the X16 VERA layout, so its tools and ideas carry over):

  | Byte | Contents |
  |---|---|
  | 0–1 | bits 0–11: image address in 64-byte units. Bit 15: 8 bpp |
  | 2–3 | X, 10 bits |
  | 4–5 | Y, 10 bits |
  | 6 | bits 0–1 H and V flip; bits 2–3 depth (0 off, 1 behind both layers, 2 between, 3 in front); bits 4–7 collision group mask |
  | 7 | bits 0–3 palette offset; bits 4–5 width (8, 16, 32, 64); bits 6–7 height |

- **Bitmaps:** bitmap 256 (8 bpp) and bitmap 512 (4 bpp) are both 256 bytes per row, one page each, so y is the high
  byte of the address relative to MAPBASE. Mono is 80 bytes per row, packed.


## Sound ##

**Decided**

- Two 6581 SIDs, using modern drop-in replacements (ARMSID, FPGASID, SwinSID or similar).
- Playing C64 `.sid` files is a goal for later.
- **A co-processor on the card:** an RP2350 running a cycle-exact 6510, with its own 64 KB, so tunes play
  independently of the main CPU.

**Why a co-processor.** A `.sid` file is 6502 code (the tune's own player), not data, so playing it independently
needs a CPU on the card. Running tunes on the main CPU instead would hit several walls:

- the I/O hole at `$C000–$C7FF` collides with tunes loaded there;
- the SIDs can't appear at `$D400` inside a window, so tunes couldn't read OSC3 or ENV3;
- sample tunes time their `$D418` writes for a 1 MHz CPU;
- many players, especially demoscene ones, use the NMOS 6510's undocumented opcodes (LAX, SAX, DCP and others),
  which are NOPs on the W65C02S.

The card's 6510 runs from the SID clock at exactly C64 speed, with the SIDs at `$D400` in its own map. Native games
can use it the same way, as arcade boards used sound CPUs: the game loads its music driver onto the card and sends
it commands.

**Proposed: the card**

- **RP2350**, the same chip as the USB and SD cards. Its 520 KB of internal SRAM holds the card's 64 KB, so there is
  no external RAM.
- **SID clock** from crystals, selected by CTRL: PAL 985,248 Hz (17.734475 MHz ÷ 18) or NTSC 1,022,727 Hz
  (14.31818 MHz ÷ 14). `.sid` files assume one or the other; most assume PAL. The 6510 runs one cycle per SID
  clock.
- **Model.** Most replacement chips can behave as a 6581 or an 8580, set by a jumper or a configuration sequence
  written to the chip; there is no card register for it.
- **The card's 6510 memory map**, modelled on the C64:

  | Address | Contents |
  |---|---|
  | `$0000–$FFFF` | 64 KB RAM |
  | `$0001` | the 6510 port, for tunes that bank I/O out of the way |
  | `$D400` | SID A |
  | `$D420` or `$D500` | SID B (CTRL bit 3) |
  | `$DC00` | CIA 1, imitated: timer A and its interrupt, for the play routine |
  | `$DE00` | the mailbox: read takes the host's byte, write sends one to the host; `$DE01` is its status |

  RSID tunes that need the VIC raster interrupt or a second CIA won't play yet; more of the C64 can be imitated
  later.
- **Bus timing.** The RP2350 answers the backplane through its PIO. Writes are latched; reads and back-to-back
  writes hold the CPU with RDY if the card isn't ready.
- **In Apple mode** the card is idle and its block hidden.

**Registers (decided)** (module ID `0xA`, `$C500–$C57F`, offsets from `$C500`):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00–$1C` | SID A | read/write | the SID's 29 registers, for native programs that drive the SIDs themselves. Writes are ignored while the card's 6510 runs; reads always work |
| `$20–$3C` | SID B | read/write | the same for SID B |
| `$40` | CTRL | read/write | bit 0: SID clock PAL (0) or NTSC (1). Bit 1 RUN: the card's 6510 runs. Bit 2 RESET: writing 1 resets it. Bit 3: SID B at `$D500` instead of `$D420` in the card's map. Bit 4 BOTH: SID A writes also go to SID B, so a mono tune plays on both |
| `$41` | STATUS | read | bit 0 BUSY: a SID write is still pending. Bit 1: the host-to-card mailbox is full. Bit 2: the card-to-host mailbox is full. Bit 7: interrupt pending |
| `$42` | IEN | read/write | bit 2: interrupt when the card sends a mailbox byte; it pulls bit 2 of IRQH (ID A) low |
| `$43–$44` | RAMADDR | read/write | address in the card's 64 KB, low byte first; increments on every RAMDATA access |
| `$45` | RAMDATA | read/write | the card's RAM. Asserts DRQ while ready, so DMA loads a whole tune in about 10 ms |
| `$46` | MBOUT | write | a byte to the card ("play song 3", a sound effect, a music cue) |
| `$47` | MBIN | read | a byte from the card ("tune finished", a sync point); reading empties it |
| `$7F` | ID | read | card type and revision |

**`.sid` playback.**

1. The host stops the card (RUN = 0) and loads the tune's data at its load address through RAMDATA.
2. It writes a small stub that calls init with the song number, then calls play on each CIA timer interrupt, or
   at 50 or 60 Hz for tunes that use the vertical blank.
3. It sets the clock from the file's PAL or NTSC flag and sets RUN.
4. The main CPU is free; the mailbox tells it when to change song or stop.


## Storage ##

**Decided:** a GoTek for disks, not original Apple drives.

**Proposed**

- **Disk II controller clone, in slot 6.**
  - In Apple mode it is exactly a Disk II: the switches at `$C0E0–$C0EF`, the boot ROM at `$C600`, the
    ROM-driven state machine and shift register, on the 2 MHz clock derived from OSC.
  - It fits in a CPLD. Its 256-byte boot ROM can be part of the IIe ROM image in bank E, at `$C600`, so the card
    needs no ROM of its own.
  - In native mode, a DMA channel can move a whole track of raw disk bytes at once.
- **GoTek.**
  - It must run the **HxC firmware**. FlashFloppy, which most GoTeks ship with, has no documented Apple II
    support.
  - HxC reads `.DSK`, `.DO` and `.PO` images directly; `.NIB` and `.WOZ` images are converted to HFE first.
  - It connects through an adapter to the Apple's 20-pin drive cable; the HxC docs give the pinout.
  - Copy-protected disks survive the conversion from `.WOZ` to HFE case by case.
- **Native storage (decided): SD, behind an RP2350** (module ID 7, "Mass Storage"). A floppy runs at about
  30 KB/s, too slow to be native storage.
  - The same pattern as the USB card: one microcontroller family and one toolchain.
  - The RP2350 handles the SD protocol and presents ATA-like registers to the 6502: sector number (LBA), count,
    command and status, plus a 512-byte buffer. Demand-mode DMA on channel 0 empties or fills the buffer at full
    bus speed, and the CPU can also read it directly.
  - It could also understand FAT, so files copied onto the card from a PC can be read directly. Optional.
  - The RP2350 is 3.3 V: its outputs need HCT/ACT receivers or level shifters on the bus side. Check whether its
    inputs are 5 V tolerant before relying on it.
  - CompactFlash was the alternative: bus-ready with a little glue and no firmware, but a fading medium, with
    8-bit mode and DMA support varying between cards.
  - Optionally, a ProDOS block driver in a slot ROM could show the same card as a hard disk in Apple mode. That goes
    beyond a stock IIe, so it's an extra, not a requirement.


## Apple II Mode ##

**Entering.** Native software writes the mode register.

1. The north bridge switches to the Apple mapping, and the clock card switches to the Apple clock.
2. The DMA controller goes idle and the video card switches to Apple modes.
3. The north bridge pulses /RESET.
4. The CPU starts from the IIe ROM image's reset vector in bank E, as a IIe does at power-on.

Before the switch, the BIOS has loaded the ROM image into bank E and cleared banks 0–3.

**Leaving (decided):** only by hard reset. Power-on and the reset button always start in native mode, so nothing
an Apple program does can leave by accident.

**What a stock enhanced IIe needs, and where it comes from**

| IIe feature | Provided by |
|---|---|
| 65C02 | the W65C02S. Its extra opcodes (BBR, BBS, RMB, SMB, WAI, STP) do nothing on the IIe's 65C02 and aren't used by Apple software |
| 128 KB with auxiliary memory, language card, and all soft switches | north bridge |
| Keyboard (`$C000`, `$C010`), Open- and Closed-Apple keys (`$C061/$C062`) | USB card, from the USB keyboard |
| Speaker (`$C030`) | USB card (proposed), or the sound card |
| Game port: paddles and buttons (`$C064–$C070`) | USB card's game port. Paddle timing depends on the 1 MHz clock |
| Video: all IIe modes, `$C019` vertical blank | video card, by snooping |
| Disk (slot 6) | Disk II clone with a GoTek |
| ROMs | the IIe ROM image (Applesoft, Monitor, 80-column firmware) in bank E. Apple's IIe ROMs are copyrighted and have to be supplied by the owner; the `applesoft-lite` in this repo is not full Applesoft and has no graphics commands |


## Interrupts ##

**Decided:** one interrupt status register, with a bit per module ID, so a single read finds which cards are
interrupting.

**Proposed mechanism.** Cards are chosen by module ID, not by slot, so the north bridge can't see which card pulled
/IRQ. Instead it asks:

1. The register is two bytes in the north bridge's I/O space, IRQL and IRQH at `$C688/$C689`: module IDs 0–7,
   then IDs 8–F.
2. When the CPU reads either byte, the north bridge asserts **/IACK** for that cycle and drives nothing on the data
   bus.
3. During /IACK, each card with an interrupt pending pulls its own data line low, D(ID mod 8), choosing the half by
   A0. Every other card leaves the data bus alone.
4. Weak pull-ups on the data lines keep every other bit high. The CPU reads a byte where **0 means interrupting**.

The cost is one backplane line, plus open-drain drivers for one data bit on each card that interrupts.

**In Apple II mode** /IACK is never used: Apple software polls its own cards. Native-only cards keep quiet while
AMODE is high.


## Input and Ports ##

**Decided**

- USB keyboard and mouse.
- The game port goes on the same card as the USB ports.
- Two serial ports (COM1, COM2) and a parallel port.

**Proposed: the USB card**

- **USB host:** a microcontroller with USB host support, such as an RP2350 running TinyUSB. It presents simple
  registers to the 6502.
- **Native mode:**
  - a key event FIFO, holding key codes with press and release and the modifier state;
  - mouse movement and buttons;
  - an interrupt on new events.

  The FIFO can be drained by DMA, which meets the "supports DMA" rule, though input rarely needs it.
- **Apple II mode:** the same controller presents the IIe keyboard:
  - ASCII with the strobe bit at `$C000`, cleared by `$C010`;
  - Open- and Closed-Apple on two modifier keys at `$C061/$C062`.
- **Game port:** a DE-9 Apple-style joystick port.
  - The paddles use a real 558 timer, triggered by `$C070` and read in bit 7 of `$C064–$C067`. Because it runs in
    real time, its readings stay correct whatever the CPU speed, though native software has to calibrate.
  - The buttons are read at `$C061–$C063`.
  - A USB gamepad mapped onto the paddles is an option for later.
- **Speaker:** the `$C030` click is a flip-flop that changes on each access, with a small amplifier. It fits on
  this card.

**Proposed: the serial and parallel card** (module ID 6, "Serial A/B & Parallel")

- TI's TL16C552A: two 16550-compatible UARTs with 16-byte FIFOs and an IEEE 1284 bidirectional parallel port, in
  one chip. It is the classic PC combination for COM1, COM2 and LPT1.
- **DMA:** the UARTs' TXRDY/RXRDY outputs drive DRQ lines. The parallel port can use DMA the same way.
- **Clock:** a 1.8432 MHz crystal for the standard baud rates.
- **Glue logic:** the chip has an Intel-style bus, so a few gates turn PHI2 and RWB into /RD and /WR.
- The W65C51N was considered and rejected: its stuck transmit flag makes DMA and interrupt-driven transmit
  unreliable.

**Module IDs (decided)** for the new cards, using the free IDs `0x8`–`0xD`:

| ID | Module |
|---|---|
| 0x8 | DMA controller |
| 0x9 | Video |
| 0xA | Sound (2× SID) |
| 0xB | USB, game port and speaker |
| 0xC | Disk II controller |
| 0xD | north bridge registers (no card) |

Native storage (SD) is "Mass Storage", ID 7. The front panel is "Console", ID 0xE. RAM A–D (IDs 0–3) are four
256 KB modules, chosen by A18–A19.


## Backplane Signals ##

The current pin-out ([modules.md](modules.md)) leaves 20 pins unassigned: A10, A11, B9, B11–B19 and B21–B28. This
design needs 18 new signals, which leaves 2 pins. Both go to ground on either side of PHI2.

**Assignment (decided).** New pins are in bold, existing pins are listed for context. The same table is in
[modules.md](modules.md).

| Pin | Signal | Driven by | Type | Purpose |
|---|---|---|---|---|
| A2–A9 | D7–D0 | | | data, existing |
| **A10** | RWB | CPU, or the bus master | totem pole | read/write. Beside the data lines it qualifies |
| **A11** | /OE | north bridge | totem pole | read strobe (already listed in modules.md) |
| A12–A31 | A19–A0 | | | address, existing |
| B2–B4 | /RESET, /NMI, /IRQ | | | existing |
| B5–B8 | PS3–PS0 | | | module ID, existing |
| **B9** | SYNC | CPU | totem pole | opcode fetch, for single-step and the bus spy |
| B10 | GND | | | existing |
| **B11** | VPB | CPU | totem pole | vector pull, for the north bridge's vector rule |
| **B12** | RDY | any card | open drain, pulled up | wait states; stops the CPU for DMA |
| **B13** | BE | DMA controller, bus spy, Console card | open drain, pulled up | low floats the CPU off the bus |
| **B14** | /IACK | north bridge | totem pole | interrupt status poll |
| **B15** | DRQ0 | storage | pulled down | DMA request, channel 0 (highest priority) |
| **B16** | /DACK0 | DMA controller | totem pole | DMA acknowledge, channel 0 |
| **B17** | DRQ1 | | pulled down | channel 1 |
| **B18** | /DACK1 | DMA controller | totem pole | |
| **B19** | GND | | | ground beside PHI2 |
| B20 | PHI2 | | | existing |
| **B21** | GND | | | ground beside PHI2 |
| **B22** | DRQ2 | | pulled down | channel 2 |
| **B23** | /DACK2 | DMA controller | totem pole | |
| **B24** | DRQ3 | | pulled down | channel 3 |
| **B25** | /DACK3 | DMA controller | totem pole | |
| **B26** | TC | DMA controller | totem pole | terminal count |
| **B27** | AMODE | north bridge | totem pole | Apple II mode is active |
| **B28** | /BOOTSEL | bus spy | open drain, pulled up | boot from RAM, not ROM |
| B29, B30 | +24 VDC, OSC | | | existing |

**Decided: the CPU and the north bridge share a card.**

- With 6-bit windows, the north bridge replaces A14–A15, so the CPU's own A14–A15 must not reach the backplane.
- On one card, the CPU's A14–A15 go only to the north bridge. The north bridge drives backplane A14–A19, and the
  CPU drives A0–A13 directly. The north bridge floats its address lines whenever BE is low.
- Separate cards would have needed two more lines, CA14 and CA15, taking the two ground pins beside PHI2.
- One card also keeps the address path from crossing the backplane twice, which a future turbo mode would need.

**Pull-ups and pull-downs** belong on the backplane itself, so an empty slot leaves every line in a defined state.
That covers RDY, BE, /BOOTSEL, /RESET, /NMI and /IRQ, the weak data-line pull-ups for /IACK, and the DRQ
pull-downs.

**Power (decided).** The backplane carries +24 V (B29), and every card regulates it down to what it needs at the
point of use. At 24 V the bus carries a fifth of the current that 5 V would need for the same power.


## Front Panel ##

**Decided:** PDP-11 style, on its own **Console card** (module ID `0xE`), with a single switch register and live
lights. It works without the ESP32. This supersedes the switch list in [console.md](console.md).

**Proposed controls**, after the PDP-11/70:

- **Switch register:** 20 switches. LOAD ADRS takes all 20 as a physical address; DEP takes the low 8 as data.
- **LOAD ADRS, EXAM, DEP.** Repeated EXAM or DEP presses advance the address, to step through memory.
- **HALT/ENABLE, CONT,** and **START** (reset and run).
- **S INST / S BUS CYCLE:** whether CONT, while halted, runs one instruction or one bus cycle.

**Lights:**

- A0–A19: the 20-bit physical address, after the north bridge. It doesn't always match the CPU's address.
- D0–D7: the data bus (decided: 8 lights, not the 16 in [console.md](console.md)).
- Status: Power, Run, Halt, SYNC (fetch), RWB, VPB, RDY (wait), BE (DMA or panel), IRQ, AMODE.

**How the card works:**

- A small CPLD does everything that must be exact to the cycle.
- **Halt and step through RDY.** The W65C02S stops on any cycle when RDY is low, writes included, so single
  bus-cycle step comes free. Releasing RDY until SYNC goes high again gives single-instruction step. What the lights
  show at each step lines up with the emulator's bus trace.
- **EXAM and DEP** take the bus with BE and run one bus cycle at a 20-bit physical address, so they don't depend on
  the window registers.
- **Live lights.** The CPLD latches the address, data and status on every bus cycle, so the lights glow with real
  bus activity, as a running PDP-11's did, and freeze when halted.
- **Wiring.** Shift-register chains (74HC595 for lights, 74HC165 for switches) or LED drivers keep the card to the
  panel down to one ribbon cable.
- **Registers** at `$C700` (ID `0xE`) give software and the ESP32 bus spy the same view as the panel: switch
  states, halt, step, and a breakpoint address. The ESP32 can mirror the panel over Wi-Fi or script stepping.


## Emulator ##

The emulator ([emulator/](../emulator/)) can run this design before any hardware exists. Its device model
(machine files, decoder, timed devices, bus trace) carries over. The additions, roughly in order:

1. **The north bridge:** the registers at `$C680`, the native windows with ROM space and read-only bits, VEC, and
   module decode.
   This is `backplane.machine`, unblocked by this document.
2. **RDY:** wait states per module, and stopping the CPU for DMA. The core is bus-cycle accurate already; this
   adds the stall cycle.
3. **The DMA controller,** as a timed device with DRQ inputs from other devices.
4. **Video:** a snooping video device, starting with the text layer, drawn in a window or the terminal.
5. **Apple II mode:** the IIe switch logic, the mode register and the clock switch. Check it by running real IIe
   software.
6. **The Disk II controller,** with `.dsk` and `.woz` images.
7. **The sound card:** the registers and mailbox first, then the card's own 6510 (with the NMOS undocumented
   opcodes) and the SID, with sound later.

Each step can be checked against real software, the same way the Ben Eater programs checked the 6522 and 6551.


## Open Questions ##

1. **Card register maps,** each within its 128-byte block of `$C000–$C7FF`: USB, Disk II (native
   side), mass storage, serial, Console.
