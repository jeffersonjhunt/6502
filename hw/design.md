# System Design #

Draft, 2026-10-21. The architecture of the backplane computer as agreed so far. Each section separates what is
**decided** from what is **proposed** (a starting point, open to change); open questions are collected at the end.
Card pin-outs and the module ID list are in [modules.md](modules.md), the front panel in [console.md](console.md).


## Goals ##

- A W65C02S at **14.31818 MHz**, just over its 14 MHz rating, or 7.15909 MHz while the hardware is being brought
  up. It has **1 MB of RAM**, with the system software (BIOS, Monitor, BASIC, DOS) living at the top of RAM.
- Banking with as few bits as possible.
- Storage and I/O that **support DMA**: every card can be driven by the CPU through its registers, and can also
  move data by DMA.
- VGA video with text, bitmap, tile and sprite modes; stereo sound from two 6581 SIDs.
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
     RAM A-D     ROM A     DMA ctl    video      sound      Disk II     SD/CF      serial/par    bus spy
     (1 MB)     (flash)    (4 ch)     (VGA)     (2x SID)    (GoTek)    storage                  (ESP32)
```


## CPU and clocks ##

**Decided: one oscillator for the CPU,** a 14.31818 MHz crystal (the Apple II's master clock). Every CPU speed is
divided down from it:

| Divider | PHI2 | Cycle | Used for |
|---|---|---|---|
| ÷1 | 14.31818 MHz | 69.8 ns | native mode |
| ÷2 | 7.15909 MHz | 139.7 ns | native mode during bring-up, or as the fallback if ÷1 isn't stable |
| ÷14, every 65th cycle ÷16 | about 1.0205 MHz average | 978 ns (1,117 ns long cycle) | Apple II mode: 65 cycles per scan line, 17,030 per frame, exactly as the Apple |

- **The ÷1 speed is just over the rating.** Its 69.8 ns cycle is 0.2 ns, about 0.3%, under the datasheet's 70 ns
  minimum. That's well inside what parts typically manage, but it isn't guaranteed. ÷2 is the safety net.
- **÷2 suits early development.** At 140 ns per cycle the read budget is about 100 ns. Flash, backplane RAM, logic
  analysers and long wires all work with no wait states, so timing problems can be dealt with one at a time when
  moving up to ÷1.

**Proposed**

- **A synchronous divider** on the clock card, selected by the north bridge's mode register and a speed bit (or a
  jumper during bring-up). Every speed comes from the same crystal, so switching is just changing the divider at a
  cycle boundary while PHI2 is low. The divider never produces a short pulse, so it needs no clock multiplexer.
- **Clock lines on the backplane.** The OSC line (B30) carries the 14.31818 MHz master at all times. In Apple mode
  the Disk II card divides it by 7 to get its 2 MHz state-machine clock, locked to PHI2 as on the real Apple.
- **Other cards keep their own clocks.** The video card's 25.175 MHz VGA pixel clock and the sound card's SID clock
  are separate oscillators on those cards. Neither needs to be in step with the CPU.

**Timing at ÷1.** These are WDC's 5 V figures:

| Parameter | Value |
|---|---|
| tADS (address valid after PHI2 falls) | 30 ns max |
| tDSR (read data setup) | 10 ns min |
| tMDS (write data valid after PHI2 rises) | 25 ns max |
| tCYC (cycle time) | 70 ns min |

From the address becoming valid to read data being needed is 69.8 − 30 − 10 = **about 30 ns**. That covers the
north bridge's window lookup and module decode, about 7.5 ns in a fast CPLD, plus 10 ns SRAM, with margin for one
transceiver hop each way. At ÷2 the same budget is about 100 ns.

- **Main RAM:** it goes on the CPU board, or on a short, well-grounded backplane segment beside it.
- **Slow cards:** anything slower holds the CPU with RDY wait states. Examples are the flash ROM, the SID card,
  and any slow I/O.


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
| 4–7 | `$40000–$7FFFF` | user memory, RAM disk | unused |
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

**Proposed**

- **Reset state:**

  | Window | CPU range | Block | Physical |
  |---|---|---|---|
  | W0 | `$0000–$3FFF` | 0 | `$00000` |
  | W1 | `$4000–$7FFF` | 1 | `$04000` |
  | W2 | `$8000–$BFFF` | 2 | `$08000` |
  | W3 | `$C000–$FFFF` | 63 | `$FC000`, the top of bank F |

  The machine wakes up looking like an ordinary 64 KB computer with the system software at the top.
- **Vectors.** While VPB is low (the CPU reading an interrupt or reset vector), the north bridge forces W3 to
  block 63, wherever W3 points. Vectors therefore always come from the system software. The first instructions of
  each handler must also be in a fixed place: either in W3 with W3 left alone, or in a small common area the
  handler switches through.
- **Write protection.** One north bridge bit write-protects the system software in bank F; a second bit does the
  same for the Apple ROM image in bank E.
- **I/O.** The north bridge decodes a fixed I/O area ahead of the windows, so it appears whatever the windows
  hold. Proposal: `$C000–$C7FF`, the same place as the Apple's I/O, so there is one I/O hole in both modes. The
  region size and the register map are open.

### Apple II mode ###

In Apple mode A0–A15 pass straight through and the north bridge supplies only A16–A19, following the IIe's own
rules. This is the IIe's design: auxiliary memory is the same addresses in bank 1.

- **RAMRD, RAMWRT** (`$C002–$C005`): bank 0 or 1 for `$0200–$BFFF`, separately for reads and writes.
- **ALTZP** (`$C008/$C009`): bank 0 or 1 for `$0000–$01FF`, and main or auxiliary language card for
  `$D000–$FFFF`.
- **80STORE with PAGE2** (`$C000/$C001`, `$C054/$C055`), plus **HIRES** (`$C056/$C057`): bank 0 or 1 for text
  page 1 and, with HIRES on, hi-res page 1.
- **Language card** (`$C080–$C08F`): RAM in bank 2 (main) or bank 3 (auxiliary), or ROM from bank E; read and
  write selection with the two-read rule for write enable.
  - Proposed: the second 4 KB `$D000` bank is kept at `$C000–$CFFF` of the same bank, which is otherwise unused
    because the CPU never sees RAM there. The north bridge clears A12 for it.
- **I/O:** `$C000–$C0FF` is the IIe's soft switches, keyboard, speaker and game port.
- **Slot ROMs:** `$C100–$CFFF` comes from the ROM image in bank E, as the IIe's internal ROM (INTCXROM, SLOTC3ROM),
  or from a slot card's ROM.
  - Only slot 6 has a card, the Disk II controller. Its boot ROM appears at `$C600`.
- **Status:** the switch states read back at `$C011–$C01F`. `$C019`, the vertical blank flag, is driven by the
  video card in step with the Apple timing.

This is too much for a 74HC670 register file, which is also too slow at 14 MHz. The north bridge is a CPLD, for
example an ATF1508 (5 V), holding:

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

1. At reset the north bridge overlays the flash ROM (module ID 4, ROM A) on W3, for reads only. The CPU runs from
   it with RDY wait states: flash takes 55–70 ns, so each access needs one or two wait states at ÷1 and none at ÷2.
2. The boot code copies the system image from flash into bank F. A DMA memory-to-memory copy does 64 KB in about
   10 ms.
3. It copies the Apple IIe ROM image into bank E.
4. It clears the overlay bit and sets the write-protect bits.
5. From then on everything runs from RAM with no wait states.
6. The flash can be reprogrammed from software, so the BIOS can be updated in place (for example, an SST39SF040).

**From the ESP32 (override)**

1. The bus spy holds /RESET low; a held W65C02S stays off the bus.
2. It writes an image into RAM over the backplane. The image can come from its flash, an SD card, Wi-Fi or USB.
3. It drives BOOTSEL (a new backplane line) to tell the north bridge to start with the overlay off.
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
- **Placement:** on its own card, using one of the free module IDs `0x8`–`0xD`.
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

  That's one bus cycle per byte, up to 14 MB/s.
- **Memory to memory:** two cycles per byte, about 7 MB/s.
- **End of transfer:**
  - an interrupt on /IRQ when a count runs out (terminal count);
  - a status bit for each channel;
  - an optional TC backplane line, so the card knows its transfer is done.
- **Apple mode:** the controller is held idle. Unaltered software never programs it, and stolen cycles would break
  code timed to the cycle.

The front panel's Store, Fetch and Step/Halt switches ([console.md](console.md)) are a bus master too. Step uses RDY
together with SYNC to stop at each instruction; Store and Fetch take the bus the same way the DMA controller does.


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

- a 256-entry palette of 12-bit RGB;
- a display base register (one write to flip between buffers);
- fine scroll on each layer;
- a border colour;
- interrupts at vertical blank and at a chosen scan line.

**Where the data lives.** All video data (maps, tiles, sprite tables, bitmaps) is in banks 8–B, 256 KB, which the
card mirrors. A game keeps its sprite table anywhere in RAM and copies it into the video banks with one 512-byte
DMA during vertical blank, about 37 µs, as the NES does. Storage can load tiles and pictures straight into the video
banks.

**Hardware.** The card needs an FPGA: at 14 MHz it must capture an address and data every 70 ns, and draw layers
and sprites at the VGA pixel clock (25.175 MHz). The Commander X16's VERA is a useful reference. The difference is
that VERA's memory is reached through I/O ports, while here the screen is ordinary memory.

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


## Sound ##

**Decided**

- Two 6581 SIDs, using modern drop-in replacements (ARMSID, FPGASID, SwinSID or similar).
- Playing C64 `.sid` files is a goal for later.

**Proposed**

- **Clock.** The card has its own SID clock, because pitch depends on it. It is switchable between PAL (985,248 Hz,
  17.734475 MHz ÷ 18) and NTSC (1,022,727 Hz), since `.sid` files assume one or the other; most assume PAL.
- **Bus timing.** The SIDs' bus interface expects a 1 MHz PHI2.
  - The card latches each CPU write and passes it to the SID on the SID's own clock.
  - A second write before the first is done is held off with RDY.
  - Reads (OSC3, ENV3, the paddles) take wait states too.
- **Model.** Most replacement chips can be set to behave as a 6581 or an 8580. The filters differ, and tunes are
  written for one or the other.
- **`.sid` playback.**
  - A tune is C64 code with load, init and play addresses, and expects the SID at `$D400`. Stereo tunes put the
    second SID at `$D420` or `$D500`.
  - The player maps a 64 KB "C64 sandbox" through the windows, with the SID card decoded at `$D400` in that
    mapping.
  - A timer interrupt calls play 50 or 60 times a second, running at 14 MHz.
  - Tunes that play samples by timing writes to the volume register need a 1 MHz CPU. The Apple-mode clock could
    be borrowed for those later.


## Storage ##

**Decided:** a GoTek for disks, not original Apple drives.

**Proposed**

- **Disk II controller clone, in slot 6.**
  - In Apple mode it is exactly a Disk II: the switches at `$C0E0–$C0EF`, the boot ROM at `$C600`, the
    ROM-driven state machine and shift register, on the 2 MHz clock derived from OSC.
  - It fits in a CPLD with the boot ROM beside it.
  - In native mode, a DMA channel can move a whole track of raw disk bytes at once.
- **GoTek.**
  - It must run the **HxC firmware**. FlashFloppy, which most GoTeks ship with, has no documented Apple II
    support.
  - HxC reads `.DSK`, `.DO` and `.PO` images directly; `.NIB` and `.WOZ` images are converted to HFE first.
  - It connects through an adapter to the Apple's 20-pin drive cable; the HxC docs give the pinout.
  - Copy-protected disks survive the conversion from `.WOZ` to HFE case by case.
- **Native storage:** an SD or CompactFlash card controller, with DMA for block transfers and registers for simple
  drivers.
  - A floppy runs at about 30 KB/s, too slow to be native storage at 14 MHz.
  - Optionally, a ProDOS block driver in a slot ROM could show the same card as a hard disk in Apple mode. That goes
    beyond a stock IIe, so it's an extra, not a requirement.


## Apple II Mode ##

**Entering.** Native software writes the mode register.

1. The north bridge switches to the Apple mapping, and the clock card switches to the Apple clock.
2. The DMA controller goes idle and the video card switches to Apple modes.
3. The north bridge pulses /RESET.
4. The CPU starts from the IIe ROM image's reset vector in bank E, as a IIe does at power-on.

Before the switch, the BIOS has loaded the ROM image into bank E and cleared banks 0–3.

**Leaving:** see the open questions. Unaltered Apple software must not be able to leave by accident.

**What a stock enhanced IIe needs, and where it comes from**

| IIe feature | Provided by |
|---|---|
| 65C02 | the W65C02S. Its extra opcodes (BBR, BBS, RMB, SMB, WAI, STP) do nothing on the IIe's 65C02 and aren't used by Apple software |
| 128 KB with auxiliary memory, language card, and all soft switches | north bridge |
| Keyboard (`$C000`, `$C010`), Open- and Closed-Apple keys (`$C061/$C062`) | keyboard interface (card to be decided) |
| Speaker (`$C030`) | sound card, or a small speaker driver on the keyboard/game card |
| Game port: paddles and buttons (`$C064–$C070`) | keyboard/game card. Paddle timing depends on the 1 MHz clock |
| Video: all IIe modes, `$C019` vertical blank | video card, by snooping |
| Disk (slot 6) | Disk II clone with a GoTek |
| ROMs | the IIe ROM image (Applesoft, Monitor, 80-column firmware) in bank E. Apple's IIe ROMs are copyrighted and have to be supplied by the owner; the `applesoft-lite` in this repo is not full Applesoft and has no graphics commands |


## Backplane Signals ##

The current pin-out ([modules.md](modules.md)) leaves 20 pins unassigned: A10, A11, B9, B11–B19 and B21–B28.

**Signals this design needs**

| Signal | Dir | Purpose |
|---|---|---|
| RWB | CPU, DMA controller, or bus spy | read/write |
| RDY | open drain, any card | wait states; stops the CPU for DMA |
| BE | DMA controller, bus spy | floats the CPU off the bus |
| SYNC | CPU | opcode fetch, for single-step and the bus spy |
| VPB | CPU | vector pull, for the north bridge's vector rule and the bus spy |
| OE | north bridge | read strobe (already listed in modules.md) |
| DRQ0–DRQ3 | cards | DMA requests |
| DACK0–DACK3 | DMA controller | DMA acknowledges |
| TC | DMA controller | terminal count (optional) |
| BOOTSEL | bus spy | boot from RAM, not ROM |
| AMODE | north bridge | Apple II mode is active (video, Disk II, DMA controller, keyboard card) |

That's 17 signals, leaving 3 pins. At 14 MHz those 3 are best spent on extra ground pins: the slot has only 4
grounds for more than 40 fast signals.


## Emulator ##

The emulator ([emulator/](../emulator/)) can run this design before any hardware exists. Its device model
(machine files, decoder, timed devices, bus trace) carries over. The additions, roughly in order:

1. **The north bridge:** the native windows, the VPB rule, write protection, the boot overlay, and module decode.
   This is `backplane.machine`, unblocked by this document.
2. **RDY:** wait states per module, and stopping the CPU for DMA. The core is bus-cycle accurate already; this
   adds the stall cycle.
3. **The DMA controller,** as a timed device with DRQ inputs from other devices.
4. **Video:** a snooping video device, starting with the text layer, drawn in a window or the terminal.
5. **Apple II mode:** the IIe switch logic, the mode register and the clock switch. Check it by running real IIe
   software.
6. **The Disk II controller,** with `.dsk` and `.woz` images.
7. **The SID card:** register-level first, with sound later.

Each step can be checked against real software, the same way the Ben Eater programs checked the 6522 and 6551.


## Open Questions ##

1. **Leaving Apple II mode.** Options:
   - a hard reset, where power-on and the reset button always start in native mode;
   - a front-panel switch;
   - a register at an address no Apple software uses.
2. **The native I/O area:** where it is, how big, and the register map of each card. Proposed: `$C000–$C7FF`.
3. **Palette depth:** 12-bit RGB is proposed; VGA used 18-bit.
4. **The keyboard and game-port card** for Apple mode, and what native mode uses for a keyboard.
5. **Backplane pin assignment** for the signals above, and whether the RAM can sit on the backplane at ÷1 or must be
   on the CPU board. At ÷2 it can sit on the backplane.
6. **The native interrupt scheme.** With many interrupting cards on one /IRQ line, an interrupt status register in
   the north bridge or DMA controller would save polling every card.
