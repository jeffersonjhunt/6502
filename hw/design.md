# System Design #

Draft, 2026-10-21. The architecture of the backplane computer as agreed so far. Each section separates what is
**decided** from what is **proposed** (a starting point, open to change); open questions are collected at the end.
Card pin-outs and the module ID list are in [modules.md](modules.md), the front panel in [console.md](console.md).
**Since BIOS 0.14 and OODOS 0.10 (planned 2026-11-13):** the diagnostics (all in the ROM since plan 2, reached from
`A>` by `DIAG`) and SYSINFO, with their shared library, are in [diag.md](diag.md).


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
  +-------------------------------- CPU card ---------------------------------+
  |                +-----------+      +-----------+                           |
  |  14.31818 MHz->|  clock    | PHI2 |  W65C02S  |  A0-A15, D0-D7, RWB,      |
  |     crystal    |  divider  |----->|           |  SYNC, VPB                |
  |                | /1 /2 /14 |      +-----------+                           |
  |                +-----------+            |                                 |
  |                      ^ speed            v                                 |
  |                      |        +------------------+                        |
  |                      +--------|  north bridge    |  CPU address -> 20-bit |
  |  DS3231 RTC --- I2C --------->|  (CPLD)          |  address -> module ID  |
  |                               +------------------+  (PS0-PS3)             |
  +-----------------------------------------|---------------------------------+
                                            |
  ======================== backplane: A0-A19, D0-D7, PS0-PS3, RWB, RDY, BE, DRQ/DACK ... ========================
       |           |          |          |          |           |           |           |            |
     RAM A-D     ROM A     DMA ctl    video      sound      Disk II      SD       COM1/COM2/   USB/game    bus spy
     (1 MB)     (flash)    (4 ch)     (VGA)     (2x SID)  (Floppy Emu) storage   LPT, VIA,      port      (ESP32)
                                                                                 joysticks
```

*The diagram is the first design's 62-pin bus. On the new bus ([below](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane))
the DMA controller is on the CPU card with eight channels, revision 2 ([the DMA controller, revision
2](#the-dma-controller-revision-2), built in B2), and DRQ/DACK become /DREQ and /DMA, with /WAIT for slow cards; its
draft pin-out is in [modules.md](modules.md) (proposed, B6: the signals decided, the positions the owner's).*


## CPU and clocks ##

**Decided: one oscillator for the CPU,** a 14.31818 MHz crystal (the Apple II's master clock). Every CPU speed is
divided down from it:

| Divider | PHI2 | Cycle | Used for |
|---|---|---|---|
| ÷2 | 7.15909 MHz | 139.7 ns | **native mode** |
| ÷1 | 14.31818 MHz | 69.8 ns | a possible later "turbo" mode, not planned for |
| ÷14, every 65th cycle ÷16 | about 1.0205 MHz average | 978 ns (1,117 ns long cycle) | Apple II mode: 65 cycles per scan line, 17,030 per frame, exactly as the Apple |

- **Decided: the design targets ÷2.** At 140 ns per cycle the read budget is about 100 ns. Flash, RAM on the
  backplane, logic analysers and long wires all work with no wait states. **On the new bus (B0, review round 1)**
  that takes 55 ns flash and RAM ([the read budget](#wait-states-and-the-read-budget)).
- **÷1 is a "future maybe".** Its 69.8 ns cycle is 0.2 ns, about 0.3%, under the datasheet's 70 ns minimum, which
  parts typically manage but WDC doesn't guarantee. It also leaves only about 30 ns for each read (below), which RAM
  on the backplane probably can't meet. Nothing in this design depends on it.

**Proposed**

- **The clock is on the CPU card (decided 2026-10-30).** The crystal and the divider sit beside the CPU and the
  north bridge, which selects the speed: PHI2 never crosses the backplane to reach the CPU, and there is no
  separate clock card. Module ID `0xF`, once the clock's, is the idle bus's since B6: PS0–PS3's pull-ups give it, so
  no card is given it.
- **A synchronous divider** on the CPU card, selected by the north bridge's mode register (and, if turbo is ever
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
  directions, a backplane crossing, and 55–70 ns RAM or flash, with margin. *(On the new bus, with a card's decode
  and a memory card's lane steering added, 55 ns parts: [the read budget](#wait-states-and-the-read-budget).)*
- **At ÷1: about 30 ns.** Only a fast CPLD and 10 ns SRAM close to the CPU would fit.

**Decided: RAM lives on the backplane.**

**Slow cards** hold the CPU with RDY wait states. At ÷2 that's mainly the RP2350 cards and the serial card.
**On the new bus (B0, decided 2026-11-14)** they ask for wait states by **/WAIT** instead, so that the CPU (through
the north bridge) and a DMA grant both see them; RDY stays for the DMA controller's and the Console card's own use
([wait states](#wait-states-and-the-read-budget)).

**Real-time clock (decided 2026-10-26): a DS3231 on the CPU card,** with a CR2032 to keep time with the power off.
OODOS's date stamps need it ([oodos.md](oodos.md#real-time-clock)). **Decided (2026-10-26): the north bridge owns
it.** The bridge talks I²C to it and keeps the time in its own registers (RTCCTL and SEC–YEAR, below), so software
never drives I²C; the DS3231's SQW, at 1 Hz, tells the bridge when to read the time again and can interrupt as the
north bridge's ID `0xD`.


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
| E | `$E0000–$EFFFF` | user memory; the Apple IIe ROM image once Apple mode starts | IIe ROM (`$C100–$FFFF`), write-protected |
| F | `$F0000–$FFFFF` | BIOS, Monitor, BASIC, DOS, write-protected | unused |

**Proposed (2026-11-10): video card revision 2** ([below](#video-card-revision-2), [video-rev2.md](video-rev2.md))
gives the card memory of its own, so banks 8–B stop being video memory:

| Bank | Physical | Native mode | Apple II mode |
|---|---|---|---|
| 8 | `$80000–$8FFFF` | the video card's aperture: four 16 KB slots onto card memory. No RAM is fitted here; the north bridge decodes the bank to the video card | unused (the card ignores it) |
| 9–B | `$90000–$BFFFF` | user memory | unused |

So the machine has 960 KB of RAM and the 64 KB aperture. With a revision 1 card the table above stands, but BIOS
0.13 and OODOS drive revision 2 alone. The emulator keeps RAM C's line over bank 8 and maps the aperture over it,
so those 64 KB of RAM can't be reached while a revision 2 card is fitted ([Emulator](#emulator), step 10).

**Proposed (B0, 2026-11-14): on the new bus** the map is 24 bits, the flash at `$F00000`, and the aperture moves to
`$E00000`, so bank 8 is RAM again once software turns the old place off
([the physical map on the new bus](#the-physical-map-on-the-new-bus)).

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
  | W3 | `$C000–$FFFF` | ROM A block 31, read-only: the top 16 KB of the flash | `$F7C000`, by its address (module ID 4 on the first design's bus) |

  The machine wakes up looking like an ordinary 64 KB computer with the BIOS at the top. When /BOOTSEL is held at
  reset, W3 starts at RAM block 63 (`$FC000`, the top of bank F) instead. See [Boot](#boot).
- **Vectors.** While VPB is low (the CPU reading an interrupt or reset vector), the north bridge maps
  `$C000–$FFFF` through the VEC register instead of W3. VEC resets to the same value as W3, so vectors come from
  the flash until the BIOS moves them to RAM, and an OS can point VEC at its own block. The first instructions of
  each handler must still be in a fixed place: either in W3 with W3 left alone, or in a small common area the
  handler switches through.
- **ROM space.** A window can map flash instead of RAM: blocks 0–31 of ROM A (512 KB) or of ROM B, the game
  cartridge. *(The first design gave them module IDs 4 and 5; on the new bus they answer by their address and have
  no ID: [the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane), B6.)* A cartridge appears in
  the CPU's view without being copied.
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

**Proposed (B0): on the new bus** the north bridge adds WIN0H–WIN3H and VECH (A20–A23 for a window's block) and MAP
(AP8, the aperture's old place in bank 8) at `$20–$25`
([the video aperture moved out of RAM](#the-video-aperture-moved-out-of-ram)).

**Register map** (offset from `$C680`):

| Offset | Name | Access | Purpose | Reset value |
|---|---|---|---|---|
| `$00`–`$03` | WIN0–WIN3 | read/write | the four windows | `$00`, `$01`, `$02`, `$DF` (ROM, RO, block 31) |
| `$04` | VEC | read/write | the mapping of `$C000–$FFFF` while VPB is low | `$DF` |
| `$05` | MODE | write | bit 0 APPLE: writing 1 enters Apple II mode (Apple map, clock ÷14, pulse /RESET); only a hard reset clears it. Bit 1 TURBO: reserved for ÷1 | `$00` |
| `$06` | STATUS | read | bit 0: booted from RAM (/BOOTSEL); bit 1: the last reset was power-on; bits 4–7: north bridge revision | |
| `$07` | RTCCTL | read/write | the real-time clock ([below](#real-time-clock)). Bit 7 TICK: a second passed or an alarm fired; write 1 to clear. Bit 6 TIE: TICK interrupts (bit 5 of IRQH, ID `0xD`). Bit 5 ALARM: the DS3231 flagged an alarm. Bit 2 LOST: the DS3231's oscillator stopped (OSF), so the time is invalid until set. Bit 1 SET: write 1 to send SEC–YEAR to the DS3231; a strobe, it reads 0, and BUSY shows the send pending. Bit 0 BUSY: the bridge is talking to the DS3231. Don't read-modify-write it: a TICK read as 1 and written back clears it | TICK, TIE and SET 0; LOST and ALARM follow the DS3231's status |
| `$08` | IRQL | read | interrupt poll for IDs 0–7, using /IACK. 0 = interrupting | |
| `$09` | IRQH | read | interrupt poll for IDs 8–F | |
| `$0A`–`$0D` | WAIT0–WAIT3 | read/write | wait states the north bridge inserts for each module ID, 2 bits per ID: WAITn holds IDs 4n to 4n+3, the lowest ID in bits 1–0. Reserved for turbo; unused at ÷2. **BIOS 0.14 ([diag.md](diag.md)):** the ROM diagnostics own WAIT3 while they run (it carries a program's options and a stage's flag through their no-RAM stages) and write every wait state 0 on the way out | `$00` |
| `$10`–`$16` | SEC–YEAR | read/write | the time, in BCD as the DS3231's registers `$00–$06` (seconds, minutes, hours, day of week, date, month with the century in bit 7, year). Reading SEC latches all seven, so a read never straddles a second | |
| `$17` | RTCREG | read/write | a DS3231 register number, for RTCDATA | `$00` |
| `$18` | RTCDATA | read/write | that DS3231 register (the alarms, control, status, aging, temperature), read or written through the bridge; wait for BUSY clear. A write to the control register (`$0E`) has INTCN, RS2 and RS1 cleared: SQW is the bridge's | |
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
- **Status:** the switch states read back in bit 7 of `$C011–$C01F`. `$C019`, the vertical blank flag, is driven
  by the video card in step with the Apple timing. As on a real IIe, bits 0–6 of these reads are the keyboard
  latch, which a few programs rely on: the north bridge drives bit 7 and the USB card bits 0–6.

**Decided (2026-10-24), as the emulator models it,** where the IIe's own manuals leave room:

- The language card enables writing on two *reads* in a row of an odd `$C08x` address; an even address, or a write to
  an odd one, clears it. /RESET leaves it reading ROM and writing RAM bank 2, as a IIe's MMU does.
- /RESET also turns off 80STORE, RAMRD, RAMWRT, INTCXROM, ALTZP, SLOTC3ROM, INTC8ROM, 80COL and ALTCHARSET, and
  sets text mode. APPLE survives it.
- `$C019` reads bit 7 = 0 during vertical blank (lines 192-261 of 262, 65 cycles each, 17,030 a frame), counted in
  CPU cycles.
- Empty slots, the cassette and other unused reads return `$FF` (no floating-bus video data yet).
- **The video card's character ROM** for Apple text is 2 KB in video memory at `$BE000`, which the BIOS copies from
  the cartridge (`$84000`) before entering the mode, or builds from the VGA font when there is none. It is the IIe
  video ROM's first 2 KB: 8 bytes a code, bit 0 the leftmost of 7 dots, a dot lit where its bit is 0. **Proposed**
  ([video-rev2.md](video-rev2.md), [below](#video-card-revision-2)): on video card revision 2 it is in the card's own memory at `$1E0000`, which
  BIOS 0.13's `C806R` writes through the card's data port. *As built (B5, BIOS 0.16, [below](#b5-as-built)):* from
  the image's own set *n* in ROM A (`$10000 + $2000 × n`), not the cartridge's `$84000`, or the VGA font with that
  set erased.
- **Apple colours:** the 16 lo-res colours (and hi-res's violet, green, blue, orange and white among them) are the
  usual NTSC approximations, rounded to the card's 12 bits. Hi-res colour is the simple artifact rule: a lone dot
  takes its column's colour, two together are white, and a gap between two dots of one colour takes it; no NTSC
  filter. The double modes follow the same phase rule (dot x of 560 is bit x mod 4 of a lo-res colour): double hi-res
  colours each aligned 4 dots by their pattern; double lo-res shows aux nibbles rotated right a bit.

This is too much for a 74HC670 register file, which would also leave no room for a turbo mode. **Decided
(2026-10-26): the north bridge is two CPLDs or an FPGA with level shifting;** the part is open. It holds:

- the native window registers;
- the Apple switch logic;
- the module decode;
- the mode register;
- the real-time clock's I²C controller and time registers.

### Real-time clock ###

**Decided (2026-10-26).** The DS3231 on the CPU card belongs to the north bridge:

- **At power-on** the bridge sets the DS3231's square wave to 1 Hz (INTCN 0, RS 00) and reads the time. On every
  falling edge of SQW it reads the seven time registers again into SEC–YEAR and sets TICK. The DS3231 holds its
  RST low for about 250 ms after VCC rises and ignores I²C meanwhile, so its RST is part of the board's reset
  chain: /RESET stays held until it goes high, and the bridge sets the chip up and reads the time after that.
- **Alarms:** INT and SQW share a pin and the bridge needs SQW, so alarms can't use it. The bridge reads the
  DS3231's status each second instead and sets ALARM and TICK when A1F or A2F is set; software clears the flag
  through RTCREG/RTCDATA. The bridge keeps INTCN, RS2 and RS1 at 0 on RTCDATA writes to the control register, so
  software can't take SQW from it.
- **Setting the time:** write SEC–YEAR, then SET. The bridge writes them to the DS3231 (BUSY meanwhile), which
  restarts its second from the write, and clears OSF, so LOST goes off.
- **Anything else** (the alarms, the control and status registers, the aging offset, the temperature) goes through
  RTCREG and RTCDATA, one register at a time.
- **The emulator** counts the DS3231's time in emulated cycles from the host's clock at power-on, so tests are
  repeatable and the tick keeps the CPU's time. It keeps the time across /RESET, as the battery does. Where the
  emulator can fall behind real time (the WebUI: a slow browser, a background tab, a pause, the power off), it steps
  the count by whole seconds, to the nearest, once it is 2 s out from the host's clock, keeping the second's phase;
  a time someone set is kept as its difference from the host's clock. Not at Max, which runs ahead by design
  (2026-10-27).

**Decided (2026-10-26), as the emulator models it,** where the above leaves room:

- **Timing.** The bridge does one I²C transaction at a time, at 400 kHz: 9 bits a byte and one for each START,
  repeated START and STOP. BUSY is set from the request to the end, and the effect lands at the end. The second's read
  (the seven time registers, then the status) is 132 bits, 330 µs, 2,363 cycles at 7.16 MHz; SET (the seven, then the
  status) 112; an RTCDATA read 39, a write and its read-back 68. An SQW edge while that second's read is still waiting
  is missed.
- **At power-on** the bridge sets the DS3231 up and reads the time while /RESET is still held, so the first
  instruction finds a valid time and BUSY clear. The emulated chip is ready at once: RST's 250 ms isn't modelled. It
  clears only INTCN and RS2–RS1, keeping the rest of the control register. A hard reset that is not a power-on doesn't
  set it up again; any reset clears TICK, TIE and RTCREG, and leaves LOST and ALARM to the DS3231's status.
- **SEC–YEAR writes** go to a holding copy, which SET sends; reads always give the time. SET is a strobe that reads 0;
  BUSY is on from the SET write until the send is done, and SEC–YEAR then hold what it sent. A TICK written back as
  read would still clear it, so software writes RTCCTL with the bits it means. Writing the seconds restarts the
  DS3231's second, so SQW falls if it was high, and a second's read (and TICK) follows the SET.
- **RTCREG and RTCDATA:** writing RTCREG reads that register into RTCDATA; writing RTCDATA writes the register and
  reads it back. No auto-increment. Registers past `$12` read `$FF`. A write to `$0E` goes with INTCN, RS2 and RS1
  cleared, and reads back so; the rest of the control register is software's.
- **ALARM and LOST** are the status register as the bridge last read it (A1F or A2F; OSF), so they clear when software
  clears the flags through RTCDATA, at once.
- **INT and the faster square waves** are in the emulated chip (`ds3231.h`), but on this board only the DS3231's own
  power-on value, before the bridge's set-up, ever selects them.
- **Day of the week** counts 1–7 with 1 for Sunday; the century bit set means 21xx.
- **The chip** in the emulator starts with the host's local time, as far into its second as the host's clock is, and
  OSF clear; its battery never runs flat, and it keeps the time through a power cycle, unless EOSC is set, which stops it there and sets OSF. Its leap-year rule is
  the chip's, every fourth year, so 2100 gets a 29 February. The temperature is always 25.00 °C, the aging register is
  kept but changes nothing, and BSY follows CONV and the chip's own conversion every 64 seconds (125 ms).

### Module decode ###

The north bridge works in two stages:

1. CPU address to physical address, using the windows or the Apple rules, with the I/O hole decoded first. Only
   the CPU goes through this.
2. Physical address to module ID (PS0–PS3). Everything goes through this, including DMA.

The DMA controller supplies its own addresses, so DMA always sees physical memory and never depends on the window
registers: 20 bits on revision 1, 24 (A0–A23) on revision 2, emulated as B2 ([B2: as built](#b2-as-built)).
**Proposed (B0):** on the new bus, 24-bit addresses, with memory chosen by its address and not by a module ID
([the physical map on the new bus](#the-physical-map-on-the-new-bus)).

### Future: a 65C816 and 16 MB ###

**A note, not a decision (the owner, 2026-11-14).** It records why the module IDs suit a later 65C816 machine
with up to 16 MB, so the reasoning isn't lost.

- **The RAM cards' IDs already fit.** RAM A–D are IDs 0–3, and today the north bridge chooses them by A19–A18,
  256 KB each. With 24-bit addresses it would choose them by A23–A22, 4 MB each, and the IDs wouldn't change. A
  RAM card's ID names the card; how much memory sits behind it is the north bridge's decode. *(2026-11-17, B6:
  on the new bus memory has no ID. The RAM is one card of 1 MB, selected by its address on A0–A23 while /MEM is
  low ([the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)); this bullet is the first
  design's reasoning.)*
- **The CPU's own multiplexing stays on the CPU card.** The W65C816S puts its bank byte (A16–A23) on D0–D7 while
  PHI2 is low. The north bridge's CPLD would latch it, as it already turns the CPU's 16-bit addresses into 20-bit
  physical ones. The backplane carries what the north bridge drives, not the CPU's pins.
- **Two ways to get A20–A23 to the cards.** Every pin of the first design's 62-pin connector is in use (its table is in
  [the 62-pin bus](#the-62-pin-bus-the-first-designs)). **Decided (the owner, 2026-11-14): the wider bus,** on the new connector
  ([Backplane Signals](#backplane-signals)).
  - *Wider bus:* four more address lines, which needs a bigger connector (as ISA's extra row did). The cards are
    unchanged.
  - *Multiplexed bus, the 65C816's own way:* the north bridge drives the upper address bits on D0–D7 while PHI2
    is low, when the data lines are idle, and holds them in a latch. This needs no new pins. Only cards that decode
    above 1 MB (the RAM cards) need a latch, and I/O cards ignore the phase. Every bus master (the DMA controller,
    the bus spy) must drive or latch that phase too. `/IACK`'s poll rides a normal read cycle, so it stays clear
    of the address phase. PHI2 low is about 70 ns at 7.16 MHz (about 35 ns at a 14.3 MHz turbo), which leaves
    room for a fast latch.
- **Room for ROM and I/O.** Four 4 MB RAM cards would fill all 16 MB. The north bridge would set aside a region
  for ROM, the I/O blocks and the video aperture, and leave the RAM behind it unused, as the aperture already hides
  64 KB of RAM C.
- **The windows.** Each window's register names one of 64 blocks of 16 KB. 16 MB is 1,024 blocks, so the
  registers widen. A 65C816 can also reach memory directly by its 24-bit addresses, so much code would need no
  window at all.
- **Software is partly ready.**
  - Programs keep the 65C816 rules ([hw/oodos.md](oodos.md#rules-for-programs-the-65c816)), so they run unchanged
    on it.
  - Some interfaces already carry three-byte addresses: `DM_COPY`, and card addresses.
  - What doesn't fit 16 MB: block numbers are 6 bits throughout (the allocator, function 55, FARCALL, OOLIB's
    `GX_BLOCK`), and physical addresses are checked against 1 MB. Under AGENTS.md's "interfaces only grow" rule,
    these would come as additions (new functions or wider forms, the old ones kept), in an OODOS phase after 1.0.


## Boot ##

**Decided: both paths.** The ROM is the normal way to boot. When it's fitted, the ESP32 can override it.

**From ROM (normal)**

1. At reset W3 and VEC map the top block of the flash (ROM A, by its address; module ID 4 on the first design's bus), read-only. At ÷2, 55–70 ns flash runs
   with no wait states (on the new bus, 55 ns: [the read budget](#wait-states-and-the-read-budget)). A turbo mode
   would need one or two.
2. The BIOS points W2 at RAM block 63 and copies itself from W3 into W2.
3. It writes `$7F` to WIN3 (RAM block 63, read-only). The code it is running is now the RAM copy at the same
   addresses, so execution carries on without a jump. It then sets VEC to match.
4. It copies the rest of the system software into bank F through a window set to ROM space. The CPU does the
   copying, because the DMA controller only reaches RAM; 64 KB takes about 0.1 s, once per boot. The Apple IIe ROM
   image isn't copied at boot: `$C806` (Apple II mode, below) copies it from the cartridge into bank E (`$EC000`)
   each time Apple mode starts, so native mode may use bank E as RAM.
   **Proposed (B0):** on the new bus it copies from ROM A's blocks 0–3 instead, and ROM B is for games only
   ([the flash: ROM A and ROM B](#the-flash-rom-a-and-rom-b)). *As built (B5, BIOS 0.16, [below](#b5-as-built)):*
   `$C806` takes the first good image of the four, and the dispatching entry `$C818`'s function `$A1` image A.
5. From then on the system software runs from RAM. That's the "top of RAM" requirement. It also leaves the flash
   free to be reprogrammed while the machine runs, and keeps a turbo mode possible.
6. The flash can be reprogrammed from software, so the BIOS can be updated in place (for example, an SST39SF040).
   *As built (B5, [below](#b5-as-built)):* ROM A is two SST39SF020A parts, even and odd bytes
   ([the flash: ROM A and ROM B](#the-flash-rom-a-and-rom-b)), emulated with their command sequences; the BIOS's
   flash update writes only the IIe images and sets, blocks 0–5, never the BIOS itself, which a programmer writes
   from the build's `bios-even.rom` and `bios-odd.rom`.
7. *(Emulator BIOS 0.3.)* The BIOS turns on an 80x30 text screen: an 8x16 font from flash block 30, a 16-colour
   palette, and layer 0 in video memory, so the monitor shows on VGA as well as the serial line. It starts with
   fixed entry points: `$C800` reset, `$C803` a video demo in flash block 29. *(Proposed 2026-10-31.)* Before the
   text screen, it starts the [boot splash](#boot-splash) and its sound, which run for 5 s on their own; the
   serial line has the monitor at once. *(BIOS 0.4.)* The monitor takes keys
   from the serial line or the USB keyboard, and Pause/Break breaks into it. *(BIOS 0.5.)* `$C806` enters Apple II
   mode, with the IIe ROM image taken from the cartridge (ROM B; from ROM A's images since BIOS 0.16, B5).
   *(BIOS 0.7, proposed.)* The SD card: `$C809`
   lists the card's root directory, and `$C80C` boots from it. The boot mounts `/BOOT.BIN` as unit 1, reads it to
   `$1000` by DMA channel 0 (at most 88 sectors, `$1000–$BFFF`) and calls it, so a program that returns comes back
   to the monitor. `$C80F` is ECHO, for programs to print with. *(BIOS 0.8.)* The boot puts windows 0–2 back on
   RAM blocks 0–2 first, so the CPU's `$1000` is where the DMA wrote, and a failed command ends with its error
   rather than a hang. *(BIOS 0.9.)* Break (NMI) and BRK enter the monitor with a fresh stack, windows 0–2 on RAM
   blocks 0–2, every DMA channel stopped and its terminal counts cleared, an SD command abandoned (units stay
   mounted), the sound card's 6510 stopped and its SIDs silenced, the interrupt enables of the DMA controller and
   the SD, video, sound, USB and Console cards off (so a program run with R may CLI), and the video card's
   registers back on the text screen, which the BIOS finds again from the cards' ID bytes. That brings the
   monitor back from the demo, a program, or the screen code itself. It doesn't put back W3 or VEC (Break works
   while they're the BIOS's: a program that moves VEC takes the NMI itself), memory (zero page, video memory's
   font, palette and text), DMA CONTROL, or the rest of any card's state (the monitor's entry, WOZMON, programs the
   serial card afresh). It turns the real-time clock's tick off (RTCCTL's TICK and TIE cleared); the time runs on.
   Other IRQs still return at once. `$C800` goes back to the flash and starts from its reset vector, as a hard reset does, so it
   copies the flash again (unless the machine booted from RAM through /BOOTSEL). *(BIOS 0.10, 2026-11-02.)* After
   the greeting, the BIOS boots the SD card by itself, as `$C80C` does, if the card holds a `/BOOT.BIN`: it says
   "BOOTING THE SD CARD: A KEY FOR THE MONITOR" and waits for the splash to end (with no video card, about 2 s at
   7.16 MHz); a key pressed meanwhile is taken, and the machine goes to the monitor instead. With no SD card, an
   empty slot, or no `/BOOT.BIN`, it says nothing more and enters the monitor, as before; a `/BOOT.BIN` that can't
   be booted (empty, too big, no DMA controller, an SD error) is said, as `$C80C` says it, then the monitor. A card
   that doesn't start up is given up on at a key. An OODOS card so boots OODOS at power-on. *(BIOS 0.11,
   2026-11-05.)* Break also turns the VIA's interrupts off (`$7F` to its IER) and clears the Console card's ISR,
   since a front-panel key already pending holds /IRQ low whatever IEN says; and the BIOS takes the sound card at
   any revision (the ID's high nibble, `$A`), for [revision 2](#sound-card-revision-2). The BIOS's memory-to-memory
   copies are block transfers on channel 3: it stops the channel before programming it, turns the master enable
   on for the copy and puts CONTROL back after, and waits for ENABLE to clear, so they're whole whatever BURST a
   program has set. The master enable is the whole controller's, so any channel a program has left armed runs during
   the copy (the text screen's scroll, say): a program that wants a channel not to run aborts it, rather than only
   turning the master enable off. A copy still running after about 0.1 s (no controller, or a busier channel taking the bus) is
   stopped. Apple II mode's fills run inline (they clear the stack), so it stops every channel first and sets no
   burst limit, and each fill is done before the next instruction. Apple II mode also clears the language card's
   second `$D000` bank (banks 4–5). The
   zero page the BIOS and the monitor keep is listed in `backplane/bios/backplane.inc`: ECHO (`$C80F`) uses
   `$E0–$E7`.

   *(BIOS 0.12, 2026-11-07.)* On a sound card of revision 2 or later (ID `$A2` and up), Break also stops the PCM
   stream, emptying its FIFO, and freezes the ES5503, so nothing on the card plays on behind the monitor
   ([The BIOS](#the-bios-step-8)); on revision 1 it writes nothing at `$48–$63`.

   *(BIOS 0.14, 2026-11-14: [diag.md](diag.md), plan 2.)* The ROM's diagnostics are in flash blocks
   21–23 (blocks 0–20 stay erased; since B5, 0–5 hold the IIe images and sets), entered three ways. Before its copy into block 63, with the Console card
   fitted (its ID read twice), the reset turns the breakpoint off if BPADDR holds the diagnostics' own P3 address
   (`$BD1A5`), and only then, so a breakpoint set before START still catches the boot anywhere else; and with
   switch 19 up, and block 23 holding the diagnostics linked with this BIOS (their signature, their version, and
   the BIOS version they were linked with), it shows the diagnostics' prompt, which enters them at CONT or `Y`
   within 5 s and otherwise goes on to the boot, having written nothing. With switch 19 up and no diagnostics (or
   another build's), the boot says so after the greeting. `$C812` is the diagnostics from the monitor, after a `Y`;
   `$C815` is the same for a program that has asked already (A = `$A5` and Y = `$5A`, X the options: bit 0 full,
   bit 1 interactive; anything else is `$C812`), the jump table's last spare, and returns, carry set, if there are
   no diagnostics. OODOS's `DIAG` at `A>` reaches `$C815` through function 75 with A = 2. The diagnostics overwrite
   all of RAM and end in the boot (`BOOT`, past the reset's switch check: the copy, the splash, the SD card's
   `/BOOT.BIN`) or the monitor (`MONBOOT`: the copy and the greeting, no splash, no SD boot), through two fixed
   jumps at `$FFF4` and `$FFF7`. The switches 16–18 (interactive, loop, full) count only at the reset's and
   `$C812`'s entries; the diagnostics own WAIT3 while they run and leave every wait state 0. The greeting is
   `BACKPLANE BIOS 0.14`. The [Technical Reference's chapter 15](../docs/oo1-technical-reference/15-diagnostics.md)
   is the diagnostics' reference.

**From the ESP32 (override)**

1. The bus spy holds /RESET low and pulls BE low, so the CPU and the north bridge float, as they do for the Console
   card. In that cycle it enables its own drivers, with /MEM and /OE high, RWB a read and PS0–PS3 `0xE`
   ([taking and giving back the bus](#the-physical-map-on-the-new-bus)).
2. It writes an image into RAM over the backplane. The image can come from its flash, an SD card, Wi-Fi or USB.
3. It drives /BOOTSEL, so the north bridge starts with W3 and VEC on RAM block 63 instead of the flash.
4. It drives one idle cycle (/MEM and /OE high, RWB a read, PS0–PS3 `0xF`), releases BE, then /RESET, and the CPU
   starts from the image in RAM.

**What the override is for:**

- Firmware development in seconds, with no flash programming.
- Recovery from a bad flash.
- Booting test images and diagnostics.
- Loading programs straight into RAM.
- Automated hardware tests. The ESP32 loads a test, runs it, and captures the bus trace. The trace is compared
  with the emulator's using `tools/trace_diff.py`, in the format defined by
  [emulator/docs/BUS_TRACE.md](../emulator/docs/BUS_TRACE.md).

**Hardware it needs.** The bus spy card becomes able to drive the bus:

- drivers for what a master holding the bus drives: /MEM, PS0–PS3 (`0xE`), A0–A23 and RWB
  ([/MEM in each kind of cycle](#the-physical-map-on-the-new-bus)), and BE and /OE (B6); D0–D7 suffice, since it
  makes byte cycles (/WORD is pulled up);
- level shifting between the ESP32's 3.3 V and the bus's 5 V;
- control of /RESET.

The machine works without it, so it can come later.


## DMA ##

**Decided.** A central DMA controller in the style of the IBM PC's 8237.

*Revision 2 (B2, 2026-11-15) replaces what this section and
[the registers below](#dma-registers-revision-1-dropped-in-b2) describe: eight channels behind one register window,
a CARD byte a channel naming the module it serves, 24-bit addresses, and requests by /DREQ and a poll instead of a
DRQ and DACK pair a channel ([the DMA controller, revision 2](#the-dma-controller-revision-2);
[B2: as built](#b2-as-built)). The first design is kept below for the record.*

- **What a card needs:** only a request line (DRQ), an acknowledge line (DACK), and a data register. That keeps
  DMA cheap enough for every card to have it. *(Revision 2: the shared /DREQ, its answer to the poll on its own data
  line, and its PS match with /DMA low in a grant; still no address decode of its own.)*
- **Placement:** on its own card, module ID `0x8`.
  - **Decided (the owner, 2026-11-14): the DMA controller moves onto the CPU card,** beside the north bridge, with
    the new connector ([Backplane Signals](#backplane-signals)). It already works at the CPU's level: it pulls RDY
    and BE and drives the address lines, which the CPU card does anyway. On one card its address path doesn't cross
    the backplane twice, the reason the CPU and the north bridge share a card, and something a future turbo needs.
    It also frees a slot. Its registers keep module ID `0x8`, so software doesn't change. It may be a second CPLD on
    the CPU card, to keep the north bridge's apart. The owner is designing it.
- **Channels:** four, each with its own DRQ and DACK line. Priority is fixed, with channel 0 highest; it goes to
  storage. **Decided (the owner, 2026-11-14): eight channels** with the new connector, granted by module ID over
  /DREQ and /DMA instead of a DRQ and DACK pair each
  ([the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)). Until that card is built, the
  registers below, the emulator and the software keep four; what channels 4–7 add to the registers is designed
  with the card. *(Since B3 all eight are live behind SELECT, so the emulator and the software no longer keep four:
  that sentence now holds only for revision 1's registers below; B2 had 0–3 live. The BIOS's and OODOS's loops
  cover all eight since B2.)*
- **Registers per channel:** *(revision 1's; revision 2's addresses are 24 bits and it adds CARD)*
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

### DMA registers, revision 1 (dropped in B2) ###

**Revision 1, dropped in B2.** The layout below, ID `$81`, was the emulator's, BIOS 0.14's and OODOS's until B2
(2026-11-15). Revision 2's replaces it ([the DMA controller, revision 2](#the-dma-controller-revision-2)): the BIOS
from 0.15, OODOS from B2 and the emulator know only revision 2, and take an ID of `$81` as no controller. MODE's
bits, CTRL's, the transfer modes and the behaviour below carry over unchanged; the registers' places, the 20-bit
addresses, STATUS's DRQ bits and the wiring by channel do not.

**Decided (then).** The controller's block is `$C400–$C47F` (module ID `0x8`). Each channel has 16 bytes at
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
| 1–0 | XFER | 00 single, 01 demand, 10 block, 11 reserved. Memory to memory always runs as block |
| 3–2 | DIR | 00 card to memory, 01 memory to card, 10 memory to memory, 11 reserved |
| 4 | DEC | 1 = the address counts down |
| 5 | HOLD | 1 = the address stays fixed. On a memory-to-memory source, this fills memory with one byte |
| 6 | AUTO | 1 = at terminal count, reload ADDR and COUNT and stay armed, for ring buffers and sample streams |
| 7 | IRQ | 1 = interrupt at terminal count |

**Shared** (`$C440`):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$40` | STATUS | read; write 1 to clear | bits 0–3: terminal count reached on channel 0–3, also the interrupt source. Bits 4–7: DRQ0–DRQ3 asserted now (read-only) |
| `$41` | CONTROL | read/write | bit 0: master enable. Bits 4–6 BURST: demand and block transfers give the CPU one cycle back every 2^BURST bytes; 7 means no limit, and the OS chooses. Resets to `$70`: no limit, and DMA off until software sets the master enable |
| `$42` | ABORT | write | bit n stops channel n at once; ADDR and COUNT keep the values they stopped at |
| `$7F` | ID | read | card type and revision |

**Behaviour:**

- **Fly-by.** While /DACKn is low the card needs no address decode. For card to memory it drives its data register
  onto the bus; for memory to card it latches the byte at the falling edge of PHI2. The north bridge decodes the
  controller's address to a RAM module as usual.
- **Taking the bus** costs one cycle before the first byte (RDY low, then BE low), each time the controller takes it.
- **Single mode** gives the CPU one cycle back after every byte, as the 8237 does.
- **Memory to memory** reads each byte into a latch and then writes it: two cycles per byte, 64 KB in about 18 ms at
  7.16 MHz. Any channel can do it with its own DEST, so storage on channel 0 is never tied up (the 8237 pairs
  channels 0 and 1 instead). It always runs as a block transfer, whatever XFER says (software writes 10): once REQ
  starts it, the CPU is held until the count runs out, or gets a cycle back every 2^BURST bytes if CONTROL sets a
  limit. Software that needs the copy finished before it goes on waits for ENABLE to clear.
- **Terminal count.** TC is asserted during the last byte's cycle together with its /DACK, the channel's STATUS bit
  is set, and if its IRQ bit is set, bit 0 of IRQH (ID 8) reads 0.
- **Programming.** Registers are written a byte at a time with the channel disabled, so a half-written address
  never matters. Setting ENABLE commits.
- **Channels (proposed):** 0 mass storage (SD), 1 serial and parallel, 2 USB, 3 general
  use (memory to memory and blits). *(Superseded: the wiring is 0 storage, 1 video, 2 sound, 3 none, which revision 2
  keeps as CARD's reset values: [the DMA controller, revision 2](#the-dma-controller-revision-2).)*
- **Channel 1 with video card revision 2 (proposed, 2026-11-10):** the video card's data port, its DRQ wired to
  channel 1 ([below](#video-card-revision-2)). Under OODOS channel 1 already carries the vertical-blank copies,
  which become port transfers. The serial card's DMASEL1 (DRQ1) then needs moving or sharing; its DMASEL3 still
  reaches channel 3. *(Closed by the new bus: the serial card asks by its own module, 6, and its DMASTREAM picks
  which of its two streams the request follows ([below](#the-serial-cards-stream-select)); channel 1 serves the
  video card by CARD 9, and whichever channel software points at module 6 serves the serial card.)*

The front panel's Console card is a bus master too: EXAM and DEP take the bus the same way the DMA controller does.
See [Front Panel](#front-panel).

**Decided (B0): revision 2,** ID `$82`, for the new bus, with a new register layout: one channel window chosen by
CONTROL's SELECT, a module field per channel, eight channels, the request poll, 16-bit transfers
([the DMA controller, revision 2](#the-dma-controller-revision-2)). It replaces the layout above, and the BIOS and
OODOS move to it at once in B2 ([the B2 migration](#the-b2-migration-revision-1-dropped)).


## Video ##

**Decided**

- **Output:** a single VGA timing, 640×480 at 60 Hz, for every mode. 60 Hz matches the Apple II's frame rate.
- **Snooping:** the card refreshes the screen from its own memory, which it fills by snooping the backplane.
  - Each write to a display region is copied into the card's memory as it happens.
  - The CPU, and DMA, write the screen as ordinary system RAM, and programs can read it back.
  - No CPU cycles are taken, so Apple II timing is undisturbed.
  - The PC never refreshed video by DMA either; a 640×480 refresh would need more than twice what the bus can
    carry.
  - **Proposed (2026-11-10): revision 2 reverses this for native mode.** The card draws from 2 MB of its own
    memory, which the CPU reaches through an aperture and a data port, and it snoops nothing in native mode; banks
    8–B go back to the system. Apple II mode keeps snooping as above.
    [Video card revision 2](#video-card-revision-2) has it, and [video-rev2.md](video-rev2.md) the reasons.
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
banks. **Proposed, revision 2:** all of it is in card memory instead, 2 MB, and the sprite table goes to the card
through the data port by DMA on channel 1.

**Hardware.** The card needs an FPGA: it must capture an address and data every 140 ns (every 70 ns in turbo),
and draw layers and sprites at the VGA pixel clock (25.175 MHz). The Commander X16's VERA is a useful reference.
The difference is that VERA's memory is reached through I/O ports, while here the screen is ordinary memory.
**Proposed, revision 2:** the card no longer captures native writes; it has an SRAM bus of its own, which scanout,
the aperture, the port and the Apple mirror share.

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
display data (text, maps, tiles, bitmaps, sprite table, palette) is in the snooped video banks 8–B. This is revision
1, ID `$91`; revision 2 (proposed) moves the bases and the layers' registers ([below](#video-card-revision-2)).

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
| `$09` | SPLASH | read/write | bit 0: play the boot splash from the card's own ROM ([Boot splash](#boot-splash)); clears itself when it ends |
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
- **Text:** each cell is a character byte then an attribute byte (foreground in the low 4 bits, background in the
  high 4), 80 or 40 cells a row. The font is 256 characters of 16 bytes at TILEBASE, the leftmost pixel in bit 7.
  40 columns are shown doubled across.

**Decided (2026-10-24), filling in what the tables leave open,** as the emulator draws it:

- **Transparency:** a layer or sprite pixel whose value is 0 *before* PALOFS is added is transparent.
- **What shows through:** where every layer is transparent, the screen shows palette entry 0. Outside the area a
  layer covers (the bitmap modes don't fill 640x480), it shows the BORDER colour.
- **Display off** (CTRL bit 0 clear) is black. The timing, and its interrupts, keep running.
- **Blink:** with CONFIG bit 7, a cell whose attribute has bit 7 set shows only its background for 16 frames in
  every 32 (about 1.9 Hz, the PC's rate), and the background is the low 3 bits.
- **Interrupt flags:** an event sets its ISR bit whether or not IEN enables it; the card's IRQ is ISR AND IEN.
- **Where in the line:** the vertical blank interrupt and the latching are at the start of line 480; the line
  interrupt is at the start of line LINE. Changes to memory and the palette show from the next line drawn.
- **Bitmaps** are centred: bitmap 256 (256x240, doubled both ways) and bitmap 512 both fill x 64-575, with the
  BORDER colour either side. In a 4-bit byte the left pixel is the high 4 bits, as in VERA.
- **Tile layers are 320x240, shown doubled** (as the mockup's native picture), and so are sprites: the games
  resolution. Text, mono and the bitmaps keep their own sizes.
- **Tile CONFIG:** bit 3 16x16 tiles, bit 4 8 bits a pixel, bit 5 a map 64 entries wide (else 32), bit 6 64 high.
- **Map entries** are VERA's: tile number bits 0-7, then a byte of tile bits 8-9 (bits 0-1), H flip (bit 2), V flip
  (bit 3) and a palette offset (bits 4-7). Tile *n* is at TILEBASE + *n* x its size (32, 64, 128 or 256 bytes), row
  by row. A 4-bit tile's colour is ((entry offset + PALOFS) mod 16) x 16 + pixel; an 8-bit tile's is the pixel.
- **Sprites:** X and Y wrap at 1024, so a sprite can start off the left or top. On each 320x240 line the
  lowest-numbered 32 that touch it are drawn, and a lower number is in front of a higher one. Back to front:
  the border or entry 0, depth-1 sprites, layer 0, depth-2 sprites, layer 1, depth-3 sprites. A 4-bit sprite's
  colour is its palette offset x 16 + pixel; an 8-bit sprite's is the pixel. Images are row by row, left pixel high.
- **Collisions:** opaque pixels of two drawn sprites on the same spot whose groups share a bit. At the vertical
  blank after the frame, STATUS COLL holds the shared groups and ISR bit 2 is set.

### Video card revision 2 ###

**Proposed (2026-11-10), owner to confirm:** everything in this section, from [video-rev2.md](video-rev2.md),
whose register map and card memory layout are frozen for the work. The ID becomes `$92`, so software tells the
revisions apart; what isn't said here is as revision 1 has it, above.

- **Card memory.** 2 MB of asynchronous SRAM (1M × 16, about 10 ns) on the card, shared by scanout, the aperture,
  the port and the Apple-mode mirror. Scanout reads only card memory. MEMSIZE reports what is fitted, so OODOS and
  OOLIB allocate from what is there; a later card with more changes only MEMSIZE (and, with SDRAM, the aperture's
  wait states).
- **Addresses** in card memory are 24 bits. Base registers are two bytes in **256-byte units** (card address
  `base × 256`); sprite images are 15 bits in 256-byte units, reaching 8 MB.
- **No native snooping.** Writes to banks 8–B are no longer copied; bank 8 is the aperture and banks 9–B are RAM
  ([Memory](#physical-memory-1-mb-in-16-banks)).
- **The palette** stays in card memory at PALBASE and is read each line, as the emulator does today, rather than
  copied into the card on a PALBASE write.
- **The aperture** is physical bank 8, `$80000–$8FFFF`, four 16 KB slots. Slot *n*, blocks 32 + *n*, shows card
  memory from SLOT*n* × `$4000`, so any window can be pointed at any slot. The card answers there as a memory
  module, with no wait states at ÷2. The machine fits no RAM at bank 8: the north bridge decodes it to the video
  card. *(On the new bus the aperture moves to `$E00000` and bank 8 is RAM again:
  [the video aperture moved out of RAM](#the-video-aperture-moved-out-of-ram).)*
- **The data port:** PADDR, a 24-bit card address, and PDATA. Each access to PDATA moves PADDR by PSTEP, up or
  down, so a column or a rectangle's rows go as one stream. While PCTL's DRQ enable is set the card raises DRQ on
  channel 1 whenever the port can take or give a byte, so the DMA controller streams to or from system RAM. *(DMA
  revision 2, B2: the card pulls /DREQ by its module, 9, and channel 1 serves it by its reset CARD `$09`.)* Copies
  inside card memory go out and back through the port for now; a copy engine on the card is a later feature.
- **Latching:** the two-byte bases are latched at line 480, as the one-byte ones were.

**Registers** (offset from `$C480`; frozen 2026-11-10):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00`, `$01` | CTRL, BORDER | read/write | as revision 1 |
| `$02`, `$03` | | read | reserved, read 0: PALBASE and SPRBASE moved to `$0A` and `$0C` |
| `$04`–`$09` | IEN, ISR, LINE, STATUS, SPLASH | | as revision 1 |
| `$0A`–`$0B` | PALBASE | read/write | the palette's card address in 256-byte units, low byte first |
| `$0C`–`$0D` | SPRBASE | read/write | the sprite table's, the same |
| `$0E`–`$0F` | | read | reserved, read 0 |
| `$10`, `$20` | layers 0 and 1 | | below |
| `$30`–`$37` | SLOT0–SLOT3 | read/write | the aperture's page registers, two bytes each, low byte first, in 16 KB pages: slot *n* shows card memory from SLOT*n* × `$4000`. /RESET sets SLOT*n* to *n*, so the aperture shows card memory `$000000–$00FFFF` in order |
| `$38`–`$3A` | PADDR | read/write | the port's card address, 24 bits, low byte first |
| `$3B` | PDATA | read/write | a write stores the byte at PADDR, a read returns it; either then moves PADDR by PSTEP |
| `$3C`–`$3D` | PSTEP | read/write | the step after each PDATA access, 16 bits, low byte first; 0 holds the address. /RESET sets 1 |
| `$3E` | PCTL | read/write | bit 0 DRQ enable. Bit 1 direction: 0 to the card (DMA writes PDATA), 1 from the card (DMA reads it). Bit 2: PADDR steps down. The other bits read 0 |
| `$3F` | MEMSIZE | read | the fitted memory in 64 KB units: 32 for 2 MB |
| `$40`–`$7E` | | read | reserved, read 0 |
| `$7F` | ID | read | `$92` |

**Layers** (layer 0 at `$10`, layer 1 at `$20`):

| Offset | Name | Purpose |
|---|---|---|
| `+$0` | CONFIG | as revision 1 |
| `+$1`–`$2` | MAPBASE | two bytes, 256-byte units, low byte first |
| `+$3`–`$4` | TILEBASE | the same |
| `+$5`–`$6`, `+$7`–`$8` | HSCROLL, VSCROLL | as revision 1, one byte higher |
| `+$9` | PALOFS | as revision 1 |
| `+$A`–`$F` | | reserved, read 0 |

- **Past the end.** Inside the fitted memory PADDR steps modulo its size, up or down. Written at or past the fitted
  end, it reads back as written and steps as a plain 24-bit counter, each PDATA access reading `$FF` or dropping
  its write, until it wraps from `$FFFFFF` to `$000000` (settled by the owner, 2026-11-11). Aperture addresses past
  MEMSIZE read `$FF` and ignore writes.
- **A DMA cycle against PCTL's direction** (/DACK reading the port while bit 1 says to the card (on DMA revision 2, a
  grant naming module 9), or writing it
  while bit 1 says from the card) moves nothing: a read gives `$FF`, a write is dropped, and PADDR doesn't step
  (settled by the owner, 2026-11-11).
- **/RESET** resets the registers and never touches card memory.
- **Sprite entries** keep the 8-byte layout, but the image address is 15 bits in 256-byte units: byte 0 its bits
  7–0, byte 1 bits 6–0 its bits 14–8, byte 1 bit 7 the colour depth as before. Bytes 2–7 are unchanged.
- **Apple II mode:** the card ignores the aperture and its registers can't be reached; DMA is idle, so the port is
  never used there ([Apple II Mode](#apple-ii-mode-1)).

**Card memory layout** (frozen 2026-11-10, owner to confirm). OODOS's region keeps today's layout at the same
offsets: what the BIOS and OODOS call unit *u* (1 KB of banks 8–B) is card address *u* × 1 KB, the base-register
value *u* × 4.

| Range | Owner | Use |
|---|---|---|
| `$000000–$03FFFF` | OODOS (the BIOS before it) | as banks 8–B today: the palette at `$000000`, the font at `$000400`, the console's text at `$002000`, OODOS's video units 0–12; the pointer's sprite table is a program's, in its part, as OODOS's `DM_POINTER` writes it |
| `$040000–$1BFFFF` | programs, through OOLIB | screens, bitmaps, tiles, sprite images |
| `$1C0000–$1DFFFF` | Apple II mode | the banks 0–1 mirror, 128 KB |
| `$1E0000–$1E0FFF` | Apple II mode | the IIe's character ROM, which `C806R` copies there |
| `$1E1000–$1FFFFF` | Apple II mode | spare |

**Open:** the FPGA part, given the SRAM bus and the bus-target timing ([video-rev2.md](video-rev2.md#open-questions),
question 7).

### Boot splash ###

**Proposed (2026-10-31).** The One Off Endeavors logo builds itself in 5 s at power-on. The card draws it from a
ROM of its own (block RAM in the FPGA), not from the video banks, so it runs before the BIOS has put anything in
video memory, costs the CPU one register write, and keeps going while the boot carries on behind it. The sound
card plays its sound alongside ([below](#the-splashs-sound)).

**What it shows.**

- The two Os and the ≡ of the icon scream in from the left as a pack, 8 frames apart, then DIGITAL MANUFACTURING
  from the right. Each comes in at up to 160 pixels a frame, motion-blurred, overshoots home by 9 pixels, holds a
  frame, snaps back with a short blur and flashes white as it lands.
- The logo is whole at 1.4 s. Half a second later VINTAGE FAST is rubber-stamped under it: a bordered imprint set
  in Phillip Cavette's distressed stamp face "4990810", tilted 4°, in red ink with thin patches. It starts as a faint ghost 2.2 times
  its size, as if held above the page towards you, and comes down in 4 frames, darkening. It lands at 1.97 s at
  full ink, squashes 4 %, springs back, and the page jolts.
- A glint crosses the logo (not the ink) from 2.8 s, and the last 8 frames fade it out; the card then shows its
  layers again.

**The register.** SPLASH (`$C489`) bit 0:

- Written 1, the splash starts at the next vertical blank, latched like the base registers, and covers the whole
  screen, whatever CTRL says, for the ROM's 300 frames. Then the card goes back to its layers by itself and bit 0
  reads 0. A program that wants the screen sooner writes 0 (the demo does).
- Written 0, it stops at the next vertical blank. /RESET stops it.
- The vertical blank and line interrupts carry on as usual underneath.

**The ROM** (32 KB, made from the logo by `backplane/bios/splash/splashgen.py`, which also writes the FPGA's
`$readmemh` file and the emulator's copy), version 3:

| Offset | Contents |
|---|---|
| `$0000` | Header, 32 bytes: `OOES`, version, part and curve counts, glint band width, frame count (300), glint start frame and speed, fade length (log2), shake start frame and curve |
| `$0020` | Palette: 48 entries in the card's 12-bit format. 0–15 the logo's ramp (0 the background), 16–31 the bright ramp for the flash and the glint, 32–47 the ink |
| `$0080` | Parts, up to 8 of 12 bytes: image offset, width, height, home X and Y, start frame, flags (mirrored: from the right; zoom: the curve scales it; ink: the ink ramp; which curve) |
| `$00E0` | Curves, up to 4: offset and length |
| `$00F0` | Curve data, 2 bytes a frame since a part's start. A moving part's: an offset from home in x (11 bits, signed), blur (0–6), flash, visible. A zooming part's: 1/scale in 256ths, ink level (0–15), visible. After its end, a part sits at home, sharp and whole. The shake curve moves every part in y |
| then | Images at 2 bits a pixel (coverage 0–3), rows padded to a byte, leftmost pixel in the high bits. The two Os share one image |

**The pixel rule**, per part on each pixel, after the shake:

- **A part moving across:** the image's coverage summed over a box of 2^blur pixels centred on the part's source
  column, times 5, shifted right by blur. In the FPGA the box is a running sum: each part streams its row through
  a 64-pixel delay line, adding the pixel coming in and subtracting the one leaving, so it costs no extra memory
  reads. At most three such parts touch one line.
- **The zooming part (the stamp):** scaled about its centre, the source column and row stepping 1/scale a pixel
  (in the FPGA, two accumulators, one stepped each pixel and one each line: no multiplies, one read a pixel). Its
  coverage times 5 times (level + 1) / 16. The tilt is drawn into its image, so there's no rotation.
- Either gives an intensity of 0–15. With the flash, the part uses the bright ramp; with ink, the ink ramp. The
  brightest part wins (a later one on a tie), so the stamp's faint ghost passes behind the logo.
- Then the glint, a diagonal band 40 pixels wide moving 16 pixels a frame from frame 170, moves the logo's covered
  pixels to the bright ramp. Over the last 8 frames the intensity scales down to 0.

`splashgen.py`'s `Rom.render_frame()` is the reference, and the emulator matches it pixel for pixel
(`test_video`).

#### The splash's sound ####

The sound card's 6510 plays it, on whatever the card has: on [revision 2](#sound-card-revision-2) the ES5503, from
samples ([below](#on-the-es5503-revision-2)); on revision 1 the SIDs, as follows. Either way the same bikes pass at
the same moments and the stamp lands on the same frame.

**On the SIDs (revision 1).** A 2 KB program for the 6510 (`backplane/bios/splash/bootsnd.s`; also a stereo PSID,
`bootsnd.sid`, for any SID player). SID A is left and SID B right. Play runs 240 times a second, four ticks to a
video frame, so pitches glide rather than step.

**Four MotoGP bikes** scream past, one for each part of the logo, each a voice on both SIDs:

- **Pitch:** Doppler from a bike at 85 m/s passing 7 m away, on an inline-4 near 17,000 rpm, glided between frames.
  There's a random wobble each tick for the engine's roughness, and at the upshift just after the pass, two ticks
  of noise: the quickshifter's crack.
- **Panning:** the chips' master volumes, slewed so they never click. Each chip also routes the bikes on its far
  side through a low-pass filter, so every bike pans on its own: the far ear hears it dull.
- **Distance:** the voices' sustain levels, which a SID can lower mid-note.
- The fourth bike passes alone, so it has the other voices too: a sawtooth an octave down for body, and a noise
  roar whose brightness follows its pitch.

**The stamp:**

- A push of air as it comes down: noise rising through a low-pass that opens, over its 4 frames.
- The thunk as it lands: a thump (a pulse for the punch, then a triangle) falling from 120 Hz to 35 Hz.
- A rubbery smack: noise through a resonant low-pass snapping shut.
- The handle's wooden knock: a triangle at 196 Hz left and 203 Hz right.
- All of it gone by 2.4 s; the rest of the splash is quiet.

The tune is voiced for 8580 mode, and works on a 6581.

The BIOS (`splash.s`) loads it at `$1000` through RAMDATA, points the card's reset vector at its boot entry
(`$1006`), writes SPLASH, waits for the vertical blank the splash starts on, and sets CTRL to RUN, RESET and NTSC.
The tune calls play once at once and then from CIA 1's timer at 240 Hz, so its frame *n* sounds with the picture's
frame *n*. When it's over it zeroes the SIDs' oscillators (TEST) and registers and writes `$DE02`, which stops the
card's 6510 and hands the SIDs back to the host, as after a reset. Break (QUIET) stops both early. A program that
takes the card within the first 5 s finds the tune's voices still in the SIDs, so it silences them first, as QUIET
does.

##### On the ES5503 (revision 2) #####

*(Proposed 2026-11-10.)* The same scene from samples: `backplane/bios/splash/docsnd.s`, a 1.5 KB player for the
6510, and 80 KB of wave RAM made by `docsamples.py`, all synthesised, the same bytes every build; the engine from
measurements of the real thing (a trackside recording of the 2024 MotoGP tests at Mugello), kept as numbers in
`docsamples.py`, not the recording. The chip runs 24 oscillators, so 34,419 samples a second; even oscillators go
left and odd right.

| Table | Wave RAM | Contents |
|---|---|---|
| Engine | `$00000`, 32 KB loop | a bike flat out near 16,100 rpm: each 720° cycle from the harmonics measured on ten full-throttle pulls (order-tracked and averaged), their levels and phases and how much of each repeats cycle to cycle, the rest a new phase each cycle (the rasp); softly clipped for level, band-limited to 9.5 kHz; oversampled 4 times, 32 cycles in 0.24 s |
| Wind | `$08000`, 8 KB loop | a bike's air and tyre roar, at half rate |
| Crack | `$0A000`, 2 KB one-shot | the quickshifter's crack: two quick bangs and a crackle |
| Push | `$0A800`, 2 KB one-shot | the air a stamp shoves ahead of it, at half rate |
| Thunk | `$0C000` left, `$10000` right, 16 KB one-shots | the stamp landing: the contact's click, a sub kick falling from 171 Hz to 46, the rubber's smack, the handle's wooden knock (each ear's a touch apart), the desk, a short room, driven hard so the body stays up behind the hit; at half rate |

- **Each bike** is the engine on two oscillators in each ear, for 6 dB more: a voice peaks at an eighth of the bus,
  which mixes the chip at a half. Every tick the player glides each bike's pitch (the SIDs' Doppler, with the
  recording's rpm: climbing about 9 % a second, 11 % down at each upshift; from `docgen.py`) and each ear's volume
  (its distance and an equal-power pan), so every bike pans on its own. At the first upshift, just after the pass,
  the crack plays and the engine dips for three frames: the ignition cut; the second, going away, only dips. The
  lone fourth bike also has the engine an octave down, and the wind, on one oscillator to an ear (half and a
  quarter level).
- **The stamp:** the push starts as the stamp does and ends as it lands; then twenty oscillators, ten to an ear,
  play the thunk, and the bikes stop.
- **Why oversampled:** the ES5503 doesn't interpolate; it plays the sample its accumulator's whole part points at.
  At a step that isn't whole, which a gliding pitch never is, every sample is up to one out, and on this engine at
  one sample a step that error was only 19 dB under the sound: hiss and grit all through it. At about 4 samples a
  step it's 31 dB under, the 8 bits' own floor. The price is a short loop, so the cycles vary in phase only, not
  level, and don't beat at its 4 Hz.
- **Level:** the bikes peak about −12 dBFS on the card's bus, about 5 dB under the SIDs' version; the landing about
  −4 dBFS, some 14 dB over the bikes for its first 150 ms.
- **Time:** the player's busiest tick takes about 82 % of the 4,261 SID clocks a tick has.

The BIOS (`splash.s`), when the card's ID is `$A2` or more, stops the card's 6510, copies wave RAM's image from flash
blocks 24–28 (through W1, by DOCDATA with auto-increment, about 0.13 s), loads the player at `$1000`, sets DOCCTL
RUN so the 6510 has the chip at `$DF00`, and starts it as for the SIDs, on the splash's first vertical blank. When
it's over the player halts every oscillator and writes 1 to `$DE02`: the 6510 stops and so does the chip (DOCCTL
RUN clears), so PCM isn't refused afterwards. Break's QUIET stops both early, as it does any ES5503 program.


## Sound ##

**Decided**

- Two 6581 SIDs, using modern drop-in replacements (ARMSID, FPGASID, SwinSID or similar).
- Playing C64 `.sid` files is a goal for later.
- **A co-processor on the card:** an RP2350 running a cycle-exact 6510, with its own 64 KB, so tunes play
  independently of the main CPU.
- **Revision 2 (decided 2026-11-05):** an I2S DAC for a PCM stream, and an emulated ES5503 (the Apple IIgs's DOC),
  both on the RP2350's second core ([below](#sound-card-revision-2); the spec is
  [sound-rev2.md](sound-rev2.md)).
- **The ES5503's channels fold to stereo by their low bit (decided 2026-11-05):** even channels left, odd right.

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
  | `$D011`/`$D012` | with FRAMEEN bit 1 (below), the VIC-II's raster line, read only, from the card's frame: `$D012` its low 8 bits, `$D011` bit 7 its ninth; 312 lines of 63 SID clocks PAL, 263 of 65 NTSC. `$FF` without |
  | `$D400` | SID A |
  | `$D420` or `$D500` | SID B (CTRL bit 3) |
  | `$DC00` | CIA 1, imitated: timer A and its interrupt, for the play routine |
  | `$DE03`/`$DE04` | FRAME and FRAMEEN: the card's frame interrupt, for the host's player stub, and the raster line's switch (below) |
  | `$DE00` | the mailbox: read takes the host's byte, write sends one to the host; `$DE01` is its status (bit 0: a byte from the host is waiting; bit 1: the card's last byte hasn't been taken) |
  | `$DE02` | STOP *(proposed 2026-10-31)*: any write clears CTRL RUN, so a one-shot such as the [boot splash's sound](#the-splashs-sound) hands the SIDs back when it's done. On revision 2 a write with bit 0 set also clears DOCCTL RUN, as the host's write does *(proposed 2026-11-10)*, so an ES5503 one-shot hands back the chip too and PCM may run; 0 leaves the ES5503 be |

  RSID tunes that need the VIC raster interrupt or a second CIA won't play yet; more of the C64 can be imitated
  later.
- **Bus timing.** The RP2350 answers the backplane through its PIO. Writes are latched; reads and back-to-back
  writes hold the CPU with RDY if the card isn't ready. *(On the new bus, by /WAIT: [wait states](#wait-states-and-the-read-budget).)*
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
| `$7F` | ID | read | card type and revision: `$A1`; `$A2` for [revision 2](#sound-card-revision-2) |

**Details settled in the emulator (2026-10-25, proposed for the card):**

- **SID reads.** The card answers the 25 write-only registers itself, with what was last written to each, so
  "reads always work" holds for every register; `$19–$1C` come from the chip.
- **The mailbox** is one byte each way. A second MBOUT before the card has taken the first replaces it, and the
  same for the card's byte; STATUS says when a box is full.
- **RAMDATA** is always ready, so DRQ stays asserted; `backplane.machine` wires it to DMA channel 2. *(DMA revision 2,
  B2: the card's request reaches the shared /DREQ (`drq` on its line) and channel 2 serves it by its reset CARD
  `$0A`; since it asks always at DRQSEL 0 and 2, /DREQ is almost never high with the card fitted: [the standing
  request](#the-dma-controller-revision-2).)*
- **The frame.** The card counts a C64's frame, 19,656 SID clocks PAL or 17,095 NTSC, while RUN is set, from
  the 6510's reset.
  Its interrupt is the card's own, for the host's player stub, at addresses the C64 leaves free so a tune can't
  turn it on by accident: `$DE03` FRAME (bit 0 latches at each frame's start and a 1 written there clears it;
  bit 7 reads 1 while it pulls /IRQ; the other bits read 0) and `$DE04` FRAMEEN (bit 0 lets it interrupt;
  bit 1 shows the raster line at `$D011`/`$D012`). The host's stub sets bit 1 for a PSID and not for an RSID:
  an RSID expects a whole C64, and one let past a raster wait can go on to CIA 2 or timer B and jam.
  /RESET and CTRL RESET turn it off and start the frame over.
- **/RESET** stops the card's 6510 (CTRL is cleared), pulls the SIDs' /RES, and empties the mailbox. The card's
  RAM keeps its contents.

**`.sid` playback.**

1. The host stops the card (RUN = 0) and loads the tune's data at its load address through RAMDATA.
2. It writes a small stub that calls init with the song number, then calls play on each CIA timer interrupt, or
   at 50 or 60 Hz for tunes that use the vertical blank.
3. It sets the clock from the file's PAL or NTSC flag and sets RUN.
4. The main CPU is free; the mailbox tells it when to change song or stop.

Break, BRK and the demo's end stop the card at any revision (the BIOS's QUIET clears RUN and silences the SIDs), and with it any
tune; the WebUI's tune bar then says the machine stopped it. On revision 2 QUIET also stops the PCM stream and the
ES5503 (BIOS 0.12, [below](#the-bios-step-8)).

**Details settled in the emulator (2026-10-25, proposed for the host's player):** `sid_file.h`.

- **The stub** goes on a free page: the one the header names (not `$00` or `$01`, the port and the stack), else `$FF00`, `$FE00`, `$0400`, `$C000`
  or `$0200`, the first the tune leaves alone and that misses `$0314` when the stub sets it. It sets `$00`/`$01` to `$2F`/`$37`, programs CIA 1's
  timer A (the KERNAL's `$4025`/`$4295` for a CIA song or an RSID, with its interrupt on; a frame,
  with it off, for a VBI song, whose play comes from the card's frame interrupt at `$DE03` instead, so init
  retiming the CIA doesn't change its rate; the handler plays only on a frame and acknowledges any
  other interrupt at `$DC0D`, and the interrupt entry sends a frame to it even past a `$0314` that init
  has repointed), banks `$01` for init's address, calls init with the song less one in
  A, and waits in a `JMP` loop.
- **`$01` for a routine** follows PSID players: `$37` below `$A000`, `$36` below `$D000`, `$34` in
  `$D000-$DFFF`, `$35` above. On the card only `$34` differs (there are no ROMs), so I/O comes back
  after an init under it.
- **The KERNAL's interrupt path** is imitated: `$FFFE` pushes A, X and Y and jumps through `$0314`,
  and `$EA31`, `$EA7E`, `$EA81` and `$FEBC` acknowledge the CIA and return, wherever the tune
  leaves them free, so tunes that install their own interrupt at `$0314` or `$FFFE` run. A tune with
  a play routine that loads over `$0314` goes straight to the stub's handler instead. For a VBI song,
  whose frame interrupt only the stub can acknowledge, a frame reaches the stub's handler even past a
  repointed `$0314`. A tune's own `$FFFE` entry that passes on to `$0314`, KERNAL-style, reaches the
  stub's handler and plays; one whose handler never passes on to `$0314` nor clears `$DE03` is
  stormed by the pending frame, a limit of the card (the corpus test's storming case stands for it).
- **A sync point:** after each play call the stub writes the call's number (mod 256) to the
  mailbox, so the host can count frames.
- **A mono tune** plays on both SIDs (CTRL BOTH); a second SID at `$D420` or `$D500` sets CTRL bit 3
  to match; a third SID, or a second elsewhere, is beyond the card, and its writes land on SID A's
  mirror.


### Sound card revision 2 ###

**Decided (2026-11-05):** the DAC and the ES5503, above. **Proposed (2026-11-05):** everything below, the
registers from [sound-rev2.md](sound-rev2.md), which has the behaviour in full and the order it's built in. The
ID becomes `$A2`, so software can tell the revisions apart; what revision 1 has is unchanged, and software that
knows only revision 1 takes the card by the ID's high nibble. **Later revisions (decided by the owner,
2026-11-07):** `$A3` and up keep what `$48–$63` mean. They may add registers, never move or redefine these, so
software that tests for `$A2` or more (the BIOS's QUIET does) works on them; a card that broke that would take a
new type nibble.

**Registers** (offsets from `$C500`; the rest of `$64–$7E` reads `$FF`):

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$48` | PCMCTL | read/write | bit 0 RUN, bit 1 PAUSE, bit 2 FLUSH (a strobe: it empties the FIFO and reads 0), bit 3 EOF. Bit 7 is reserved |
| `$49` | CODEC | read/write | 0 raw PCM, 1 MP3, 2 reserved for MOD; ignored while RUN is set |
| `$4A–$4B` | RATE | read/write | raw PCM's rate in Hz, 8,000 to 48,000; reads back the rate playing |
| `$4C` | PCMFMT | read/write | bit 0 stereo, bit 1 16-bit |
| `$4D–$4E` | FREE | read | the 128 KB FIFO's room in 256-byte units; reading `$4D` latches both |
| `$4F` | PCMDATA | write | the stream; a byte with the FIFO full is dropped |
| `$50` | STATE | read | 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5 ended; `$80` bad RATE or PCMFMT, `$81` no MP3 sync, `$82` PCM and the ES5503 both asked to run |
| `$51–$54` | POS | read/write | frames played; reading `$51` latches all four, a write to `$51` zeroes it |
| `$55–$56` | KBPS | read | the MP3's bitrate |
| `$57`, `$58` | VOLL, VOLR | read/write | master volume, 255 unity; it scales the SIDs' volume too |
| `$59` | EQIDX | read/write | 0 the preamp, 1–10 the bands; bit 7 the enable instead. EQVAL moves it on |
| `$5A` | EQVAL | read/write | the gain at EQIDX in signed half-dB, clamped to ±24; the enable's bit 0 |
| `$5B` | VISIDX | read/write | the index into the 98-byte vis page; writing 0 takes a snapshot |
| `$5C` | VISDATA | read | the page's byte at VISIDX, which moves on |
| `$5D` | CODECS | read | bit *n*: codec *n*; bit 7: an ES5503. `$83` |
| `$5E` | DOCCTL | read/write | the ES5503's port: bit 0 RUN, bit 2 its interrupt to the card's 6510, not the host; bit 5 auto-increment; bit 6 DOCDATA reaches wave RAM, not the registers |
| `$5F` | DOCDATA | read/write | the register or wave RAM byte at DOCADDR |
| `$60–$61` | DOCADDR | read/write | the low byte is the register; both, the RAM address |
| `$62` | DOCBANK | read/write | bit 0: the RAM address's bit 16; auto-increment carries into it, and wraps at 128 KB |
| `$63` | DRQSEL | read/write | the card's DRQ and fly-by register: 0 RAMDATA, 1 PCMDATA, 2 DOCDATA |

- **No dummy read.** The IIgs's Sound GLU needs a read before each DOC read, because the DOC is slow; here a
  DOCDATA read returns its value at once, so code ported from the IIgs drops that read.
- **The ES5503** has 32 oscillators and 128 KB of wave RAM of its own (the IIgs has 64 KB; the chip's bank bit
  allows 128), clocked at 7,159,090 Hz as in the IIgs. Its registers are the real chip's; only the port is ours.
  Wave RAM and its registers are reachable whether RUN is set or not.
- **/RESET** clears PCMCTL, empties the FIFO, zeroes POS, puts the volumes at 255, the EQ off with its gains 0,
  DRQSEL 0 and DOCCTL 0; the ES5503's oscillators halted, no interrupt pending and `$E1` 0; wave RAM keeps its
  contents. CTRL RESET leaves all of it alone.

**Settled in the emulator so far (2026-11-06, proposed for the card):** steps 1 to 5 of the spec, the registers,
the raw PCM stream, the mix, MP3, and the EQ, vis page and volume. Each register reads back what's written, as
above. The stream runs as the spec's behaviour says, on the host's clock and whatever the audio rate, through the
EQ, the vis tap and VOLL/VOLR, and is mixed with the SIDs: resampled by linear interpolation, scaled by
`MIX_DIGITAL` (0.5) and clipped. The spec's start, at 4 KB or EOF, reads for MP3 (step 4) as 4 KB, or EOF, and a decoded
first frame: an MP3 stays buffering past 4 KB until its first frame decodes (its sync run of ten has decided),
which also gives the rate. Nothing has played, so it's no underrun, and only an empty FIFO without EOF is one, as
the spec says; a stream under DMA has none. For raw PCM 4 KB always holds a whole frame, so the two readings
agree there. A real-time feeder pays for it: fed at 1.02 times the stream's rate, playing starts about 135 ms after
the 4 KB start at 320 kbps (235 ms from RUN), 5 ms at 128 kbps and 97 ms for VBR -V0. Where the spec is silent:

- /RESET also puts CODEC, RATE, PCMFMT, EQIDX, VISIDX, DOCADDR and DOCBANK back at 0, as it does RAMADDR.
- EQIDX keeps what's written; past 10 (bit 7 clear), EQVAL reads 0 and takes no write. It moves on in bits 0–6,
  so bit 7 stays.
- DRQSEL keeps bits 0–1; 3 is no source: no DRQ, a fly-by read `$FF` and a write dropped.
- A frame is due every host-clock period of the rate, the first a whole period after playing starts. Running dry
  is found when a frame falls due with none in the FIFO, so a FIFO emptied and refilled between two frames hasn't
  underrun; data again plays from the next frame due, with no new buffering. One counted underrun is one time dry.
- RUN takes RATE and PCMFMT; while it runs they read back what it took. PCMFMT's two bits are all valid, so `$80`
  is a RATE outside 8,000–48,000, or a CODEC of 2 or more (MOD is reserved).
- Releasing PAUSE goes back to the state it paused (buffering, playing or underrun); an underrun paused is the
  same dry spell, not counted again. Ended stays ended until RUN is cleared.
- With EOF, a partial frame left over is dropped when the stream ends, never played, so the next stream starts on
  a frame and FREE reads 512 again.
- FLUSH with RUN set is a seek: the stream buffers again, to 4 KB or EOF, and no underrun is counted for it.
- A new host clock (`set_cpu_hz()`) rescales the part of a frame owed, so a clock change loses no frames.
- The mix: the stream counts as playing while it plays or runs dry (silence); buffering, paused, ended or idle it
  adds nothing, and the SIDs' samples are bit for bit what they are without revision 2. Each run of the stream lines
  up with the SIDs' samples made at its start, a lead of silence ahead: (audio rate / stream rate, rounded up) + 1
  samples, so within a run the bursts a stream frame's samples come in never leave a SID sample without its part of
  the stream. A run starts when the stream starts playing, after a pause, at a FLUSH seek or a new RUN (within one
  write too), and after a tick of over two seconds; each costs one lead of silence. Within a run the delay is
  constant: a frame's level comes out about the lead after it falls due (2 samples for equal rates, 7 for 8 kHz into
  48 kHz); from the start, counting the first frame's period, 3 and 13. The backlog rules are the SIDs': of a gap
  over two seconds only the last second is made, and the stream's samples for the part skipped go with it. With
  nobody collecting, the SIDs are brought up to date once a second and a half of the stream waits. A pause released
  before its lead has played out loses up to a lead of the stream (250 µs at most) to the new run's lead: accepted.
- The EQ (step 5): the preamp, then the ten peaking biquads, each side its own, in doubles (transposed direct form
  II), rounded to the nearest sample and clipped at the end. A band at 0 dB is bypassed as one at or above 0.45 of
  the rate is, and so is the preamp at 0, so on and flat is bit for bit as off. The filters start from silence at
  RUN and FLUSH, and when a band starts running; a gain or the enable written, or an MP3's rate changing, gives new
  coefficients from the next frame, the state kept. A band's state, both halves below 10^-9 of a sample's step
  together, is let go to 0 as one, so a dry spell after a stream ends in exact silence (every band, at every gain
  and rate, within a second) and takes the bulk path; zeroing a half alone could leave a band ringing at that level
  for good. The coefficients' sines, cosines and powers of ten are by
  series in the card's own code (sound_dsp.cpp), not libm, so every target makes the same bits.
- The vis page (step 5): "the left and right mix" is read as their mono mix, (L + R) / 2, the same that indexes
  34–97 hold. The spectrum's 32 bytes are bins 1 to 32 of the 64-point FFT (DC left out, Nyquist in), with a
  periodic Hann window, scaled so a full-scale sine on a bin reads 0 dBFS (|X| over 16; the Nyquist bin, real,
  over 32, so a full-scale cosine there reads 0 dBFS). The scale is
  round((dB + 60) × 255 / 60), clamped to 0–255, for the spectrum and for the VU, whose dB is the side's peak
  sample against full scale. Indexes 34–97 are each frame's high byte, offset to unsigned. Every frame of the bus
  feeds the tap, those the mix doesn't need too (no audio rate, a bulk catch-up's, a dry spell's silence); while
  RUN is set and the stream doesn't play (paused, buffering, ended, an error), the tap has silence at the stream's
  rate on its own frame times, from the frame that found the stop, the EQ's state kept (an MP3 sought has no rate
  until its next first frame: the bus's last one). So the page goes quiet as the sound does, and doesn't depend
  on the audio rate or the ticks. Clearing RUN empties the tap's frames, so the next snapshot's spectrum and
  samples are silence (its VU the peaks since the last, as after a pause); the page itself changes only when
  VISIDX is written 0, as the spec says.
- Volume (step 5): VOLL and VOLR scale the bus after the vis tap: 255 exact, otherwise x × vol / 255 rounded half
  away from zero. SID A takes VOLL and SID B VOLR, a CTRL BOTH write to A reaching B by VOLR; the 6510's reads, the
  host's and the emulator's `sid()` give `$D418` as written.
- MP3 (step 4): CODEC 1 decodes the FIFO with minimp3 (CC0, vendored): from the second frame on as each falls
  due, the first when buffering ends (above). In step, a
  frame is decoded only with all of it and the next header in the FIFO; out of step, a frame is taken by
  minimp3's own sync rule (ten frames in step after it, or the data's end after one), and the card waits until
  that decides (an ID3v2 tag ends a run as the data's end does). So a stream fed while it plays, in any pieces and
  with EOF written whenever, plays what the same bytes at once do, and that is minimp3 on the whole data, but for
  three cases where the card does better: tags, a last frame with something after it (both below), and a lone
  last frame after junk, which the card plays and minimp3 drops. Without EOF fewer than eleven frames don't play,
  so a program sets EOF after a short clip. Only
  layer III frames whose length their header gives are taken: free format, layer I and II frames, and frames
  that decode to nothing are junk; 64 KB of junk since the last frame that made samples is `$81`. ID3v2 tags
  (versions 2.2 to 2.4, flags checked, up to 8 MB declared, footer included) are skipped by their size wherever
  met, not counted as junk (8 MB of tags with no frame is `$81`); the decoder stays in step across one. A frame
  with nothing frame-like and no tag after it, to EOF or for 32 KB, plays by its own header's length, whatever
  follows ignored (CP/M's `$1A` padding, an ID3v1 or APE trailer, anything; `$1A`s aren't stripped, as a frame
  can end in them); without EOF and less than 32 KB after it, it waits. RATE reads 0 until the first frame
  decodes. The rate and channels follow each frame (a change re-anchors the mix); above 48 kHz is `$80`, which no
  MPEG-1, -2 or -2.5 frame can be. FLUSH empties the decoder too. A search that finds nothing costs a few header
  checks at most: the look past a frame resumes where it stopped, junk is dropped as it's found, and a run is at
  most ten headers. minimp3 is built with its stack scratch zeroed
  (`-ftrivial-auto-var-init=zero`), so a bad frame decodes the same whatever the stack held.
- The ES5503's registers read back as MAME's model keeps them: the table register's bit 7 reads 0, the data
  registers, `$E0` and `$E2` take no writes, `$E3–$FF` read 0, and `$E0` with nothing pending reads `$FF` at reset.
  The data registers read `$80` after /RESET.

**The ES5503's oscillators (step 6, decided by the owner 2026-11-06).** The chip alone (`es5503.{h,cpp}`, with
no knowledge of the card), reimplemented from the spec with MAME's ES5503 (mamedev/mame
`e1c41b59c0027df7290229f6008393e5ab72013c`, `src/devices/sound/es5503.cpp`) read for its edge cases; no MAME code
is copied, so no MAME licence is needed.

- **The bank is a latch of its own.** The table register's bit 6 keeps the bank; a pointer write leaves it, and
  `$C0` reads back as written, bit 7 aside. MAME packs the pointer and the bank into one value, so its pointer
  write clears the bank; that comes of its packing, and we don't copy it.
- **One sample at a time.** Each output sample (8 × (enabled + 2) of the chip's 7,159,090 Hz), every enabled
  oscillator not halted steps once, in number order. MAME renders each oscillator over a whole buffer before the
  next, so its AM volume and sync restart take effect once a buffer; that is its buffering, not copied.
- **The accumulator** is 24 bits. Each step the index (accumulator >> (9 + resolution − size), the size masked)
  picks the byte, then the frequency is added; an index at the table's last byte or past is its end, and a loop
  takes (size − 1) << shift off. A sum that carries past 2^24 is kept with its carry until the next step's end
  check takes it, as MAME's sum does: so at resolution 7 a fast frequency that steps over the table's top still
  ends it, where a bare 24-bit wrap would loop a one-shot for ever.
- **Sync and AM pairs:** an even oscillator in sync/AM restarts the odd one below it (if that runs); an odd one
  plays nothing and its byte becomes the volume of the one above it (if that runs), which its volume register
  then reads. Oscillator 0 has nothing below and 31 nothing above: both do nothing there.
- **The halt rules are MAME's `halt_osc`, reimplemented:** a `$00` byte halts in every mode and plays nothing;
  free run loops, its phase kept; one-shot halts; swap halts and starts its partner (number XOR 1) from 0; an even
  oscillator whose partner swaps, itself not swapping, starts again; sync/AM loops. A halt with IE makes the
  oscillator pending. Halting one-shot or swap from a control write does the same by the mode it had, with an
  interrupt only if both the old and the new value have IE; halting free run so does nothing more; clearing the
  halt bit starts the oscillator from 0. One exception (owner, 2026-11-06): a loop never takes the accumulator
  below 0. Where MAME's subtraction would go below 0 (an even oscillator in free run whose partner swaps loops
  twice at one end; one-shot or free run with a swapping partner starts again from wherever a `$00` halted it),
  its 32-bit sum wraps round and the oscillator ends its table every step after; the real chip's 24-bit
  accumulator can't wrap like that. Here it goes to 0 instead and plays the table through, one end a pass.
- **The interrupt line is MAME's:** up when an oscillator becomes pending; a `$E0` read drops it and raises it
  again at once while an enabled oscillator is still pending.
- **No triple mix and no swap click.** MAME mixes the highest enabled oscillator three times (a quirk of the
  real chip, it says); the spec leaves it out, and so do we. MAME doesn't model the swap's one-sample centre
  value either, and we don't.
- **Integer output.** The chip gives each of the 16 channels' sum of (byte − 128) × volume, and a stereo fold
  (even channels left, odd right). The float scale (÷ 128 ÷ 255 ÷ 8) and the resampling to the bus are the
  card's (step 7), so the core has no floating point, and its golden hash is of integers.
- **/RESET** sets every halt bit, clears pending interrupts and drops the line, sets `$E1` to 0 and the
  accumulators to 0, and keeps the other registers and the wave RAM, as the spec says. (MAME's reset zeroes every
  register and leaves the oscillators running; the spec's wins.) A table register never written is size 0, 256
  bytes, whose end the oscillator finds (MAME's table length is 0 until written).

**The ES5503 on the card (step 7).** The port (DOCCTL, DOCDATA, DOCADDR, DOCBANK) reaches the registers and wave
RAM whatever RUN is, auto-increment carrying into the bank and wrapping at 128 KB, and DMA reaches them through
DRQSEL 2. A DOCDATA read returns its value at once: there is no dummy read, and code ported from the IIgs drops
it. While DOCCTL RUN is set and I/O is banked in, the card's 6510 sees the registers at `$DF00–$DFFF` (`$DFE0`
acknowledging as DOCDATA's `$E0` does); with RUN clear the page reads `$FF` and takes no write, as before. The
chip's interrupt goes to the host through IEN bit 3 (STATUS bit 7, IRQH bit 2), or with DOCCTL bit 2 to the 6510's
/IRQ instead. /RESET clears DOCCTL and resets the chip as step 6 says; CTRL RESET leaves it.

- **Its clock is the card's.** The chip plays inside the SIDs' catch-up, over the same SID clocks, so the 6510's
  accesses to `$DF00`, its interrupt, the host's DOCDATA and the bus all see it as of the same moment, whatever
  the host's speed, the ticks or the audio rate. Its steps (every 8 × (enabled + 2) of 7,159,090 Hz) and the
  bus's 48 kHz frames fall on their exact times, counted in integers (a step at a frame's very end is that
  frame's). While it plays, every host write to the card's registers first brings it up to that moment, so a
  write to VOLL, VOLR, the EQ or anything else changes only the frames after it, however late the card was last
  caught up (and so does a change of revision). A switch of the SIDs' crystal (CTRL NTSC) rescales the chip's
  next step and frame end to the new clock, so they stay on time. `mix_dry()` counts SID samples left without a
  bus frame for the chip's bus too (none so far: its frames are made before the SIDs' samples that take them).
- **PCM's register leaves the chip's bus alone.** While the ES5503 has the bus, a PCMCTL write (a refused RUN, a
  0, a FLUSH) doesn't clear the vis tap's frames or the EQ's filters: those are the chip's then. With PCM on the
  bus they behave as before.
- **The 6510's /IRQ only while RUN is set** (owner, 2026-11-06). With DOCCTL bit 2 and RUN clear the 6510 can't
  reach `$DFE0` to take an interrupt, so the chip doesn't pull its /IRQ then; the interrupt stays pending in the
  chip and pulls /IRQ again when RUN is set. The host's route (IEN bit 3) isn't gated by RUN: the host can always
  take the interrupt through DOCDATA, whose registers are reachable whatever RUN is.

Decided by the owner, 2026-11-06:

1. **A refused RUN isn't stored.** PCMCTL's RUN written while the ES5503 runs, or DOCCTL's while PCM runs, isn't
   kept: the register reads it 0, and the source already running plays on.
2. **`$82`** shows in STATE from a refused write until PCMCTL or DOCCTL is next written; that write clears it, and
   sets it again only if it is refused too.
3. **The bus frames are integers:** each the average of the steps it spans (or the last frame held when it spans
   none), round(sum × 32,768 / (steps × 261,120)) of the chip's stereo sums of (byte − 128) × volume, clipped to
   16 bits: the spec's ((byte − 128) / 128) × (volume / 255) × 1/8 per voice, exactly.
4. **Clearing DOCCTL RUN freezes the chip** (no steps) and stops its bus; the vis tap's frames are dropped as when
   PCM's RUN is cleared (the next snapshot silence, its VU the peaks since the last). RUN again plays the
   oscillators on from where they were, the EQ from silence and the mix anchored afresh.
5. **The bus is 48 kHz while the ES5503 runs:** the EQ's coefficients and the mix's resampling are for 48 kHz.

#### The BIOS (step 8) ####

QUIET (Break, BRK and the demo's end) takes the card by the ID's high nibble, as before, and clears CTRL RUN, IEN
and the SIDs at any revision. When the ID is `$A2` or more it then writes `$04` (FLUSH alone) to PCMCTL and clears
RUN in DOCCTL by reading it and writing it back *(BIOS 0.12, 2026-11-07)*, so nothing on the card plays on behind
the monitor; on revision 1 (`$A1`) it writes nothing at `$48–$63`. What the two writes do:

- **PCMCTL `$04`** clears RUN, PAUSE and EOF and empties the FIFO and the decoder: STATE goes to 0 (idle), a `$82`
  is cleared, FREE reads the whole FIFO, and with PCM on the bus the vis tap's frames are dropped and the EQ's
  filters start from silence. A program starts its next stream clean. CODEC, RATE, PCMFMT and POS are left.
- **DOCCTL, RUN cleared,** freezes the chip where it is and stops its bus (decision 4 above) and closes the 6510's
  `$DF00` window. Bits 2, 5 and 6 (the interrupt's route, auto-increment, DOCDATA on wave RAM) are kept, and so are
  the oscillators' registers and wave RAM.
- **An ES5503 interrupt pending at Break stays pending, on its route.** With bit 2 it is the 6510's, which the chip
  pulls only while RUN is set, so it is silent; without, the host's, which IEN (cleared by QUIET) holds off. A
  program acknowledges it before it sets IEN bit 3 again: with DOCCTL bit 6 clear (QUIET keeps it, and with it
  set DOCDATA reads wave RAM) and DOCADDR `$E0`, it reads DOCDATA.
- The volumes, the EQ's gains, the vis page, DOCADDR, DOCBANK and DRQSEL are left as they were; QUIET has already
  aborted every DMA channel (all eight since BIOS 0.15), so DRQSEL's DRQ moves nothing.

The audio is then silent: no bus frames, and the SIDs at volume 0.


## Storage ##

**Decided:** a Floppy Emu for disks, not original Apple drives. *(2026-10-25, replacing the GoTek: see "The
drive" below.)*

**Proposed**

- **Disk II controller clone, in slot 6.**
  - In Apple mode it is exactly a Disk II: the switches at `$C0E0–$C0EF`, the boot ROM at `$C600`, the
    ROM-driven state machine and shift register, on the 2 MHz clock derived from OSC.
  - It fits in a CPLD, with sockets for the Disk II's two 256-byte PROMs, the boot ROM (341-0027) and the sequencer
    (341-0028), which are Apple's: the owner fits their own, as with the IIe ROM. **(Decided 2026-10-24,** replacing
    the idea of keeping the boot ROM in the IIe image in bank E: the IIe's internal ROM has its own `$C600` page,
    which INTCXROM needs.)
  - **Both modes (decided 2026-10-25,** replacing "Apple mode only"). In native mode the card's switches appear
    in its own block: `$C600–$C60F` are `$C0E0–$C0EF`, the same sixteen switches in the same order, and the ID
    byte stays at `$C67F`; the rest of the block reads `$FF`. The boot ROM isn't visible in native mode.
    - **Why it works at 7.16 MHz.** The sequencer runs from OSC ÷ 7 (2.045 MHz) whatever the CPU's speed, and
      it assembles each disk byte itself, holding it in the data register for about 32 µs, so reading is the
      same polling loop at any speed (a faster CPU just polls more often). Writing and stepping are timed by
      the CPU: the Apple's code counts 32 cycles a byte and times the stepper with delay loops, so it can't
      run natively, but a native driver can. Native mode's clock is OSC ÷ 2, exactly 3.5 CPU cycles a
      sequencer step, so a byte (64 steps) is exactly **224 cycles**: a native write loop holds the disk's
      timing with no drift. While it writes, the driver keeps interrupts and DMA off and touches no card with
      wait states.
    - **What it's for.** Native software reading and writing the Apple disk in the drive, without moving the
      image to the SD card: a copier between the two, or a native utility for DOS 3.3 and ProDOS disks. The SD
      card is still the native machine's own storage (about 7 MB/s against the floppy's 30 KB/s), and mounts
      the same images as files.
    - It remains the exception to the rule that I/O supports DMA.
    - The emulator does it (2026-11-05, OODOS phase 6a, [oodos.md](oodos.md#the-plan-phase-6)): the
      switches in native mode, the sequencer two steps every seven CPU cycles. Found there: a write loads
      its byte with Q6 on for a whole loop of the sequencer's eight steps (28 cycles) before Q6 off, as the
      Apple's STA then ORA do in their 4 cycles; one step's worth misses the load. A byte a cycle late (225)
      still reads back, a ninth 0 bit now and then; a cycle early (223) loses bits.

**Decided (2026-10-24), as the emulator models it** (`emulator/components/devices/disk2_card.h`):

- **ID byte** `$C1` at `$C67F` (type C, revision 1).
- **The sequencer** steps at 2 MHz: in Apple II mode two steps a CPU cycle (the Apple's long cycle every 65th is
  left out, 0.2%, well inside a drive's speed tolerance); in native mode (phase 6a) two every seven cycles at
  7.16 MHz, and cycles × (OSC / 7) / the CPU's speed when the emulator runs native mode slower, so the disk keeps
  its own time. The PROM is read as the 341-0028 is wired, found by running the owner's
  dump against a known bit stream: address A7, A6, A0, A5 the state, fed back from data D7–D4; A4 the read pulse,
  low while there is one; A3 Q7, A2 Q6, A1 the data register's bit 7. D3–D0 are the command: 0–7 clear, 8 and C
  nothing, 9 shift in a 0, D shift in a 1, A and E shift right bringing in write protect, B and F load the data bus.
  Writing, the head follows state bit D7: a change within a 4 µs cell is a flux transition.
- **The drive:** a bit cell every 4 µs; a read pulse on the first step of a cell holding a 1; past three 0 bits the
  read amplifier finds noise (3 in 10 ones), as an MC3470 does on unformatted disk. The head moves a quarter track
  at a time towards the magnets that are on, only while the motor runs. The motor runs on for 1 s after it is
  switched off. Reading an odd address returns `$FF`; only even ones drive the data register onto the bus.
- **/RESET** clears the card's latch: phases, Q6 and Q7 off, drive 1, the motor off (running on).
- **Images:** `.woz` (WOZ 1 and 2) bit for bit; `.dsk`/`.do` and `.po` (140 KB) written out as DOS 3.3 formats a
  disk, volume 254, the sector order decided from the contents (a DOS catalog chain, or a ProDOS volume directory,
  where each order puts it) before the name. `.nib`, `.d13` and `.2mg` come later.
- **The drive: a Floppy Emu (decided 2026-10-25,** replacing the GoTek).
  - BMOW's Floppy Emu, built for Apple II drives: it connects to the Disk II's 20-pin drive connector by a ribbon
    cable, and its own SD card holds the images.
  - It reads `.woz`, `.dsk`/`.do`, `.po` and `.nib` directly. WOZ is what the emulator uses, so the same files
    boot the emulator and the machine, most copy protection included.
  - Its SmartPort hard-disk mode isn't usable here: a stock IIe's Disk II controller doesn't speak SmartPort.
  - Check the current firmware's features before buying.
  - **Why not the GoTek:** it is cheaper and widely sold, but it is a PC floppy emulator. It needs the HxC
    firmware flashed on (FlashFloppy, which most ship with, has no documented Apple II support), an adapter to
    the 20-pin cable, and `.woz` and `.nib` converted to HFE first, where copy protection survives case by case.
    It stays a fallback: the card is a Disk II, so any drive that behaves like one works.
- **Native storage (decided): SD, behind an RP2350** (module ID 7, "Mass Storage"). A floppy runs at about
  30 KB/s, too slow to be native storage.
  - **Kept as a module apart from the Disk II (the owner, 2026-11-14).** The two share almost nothing:
    - the Disk II is a clone of Apple's slot 6 controller, at Apple's addresses, with the owner's PROMs, a 2 MHz
      sequencer and no DMA;
    - the SD card is the native machine's own storage, behind an RP2350, with DMA.

    A module ID is logical, though (modules.md: one physical card may serve more than one ID), so one physical
    storage card answering both IDs 7 and `0xC` stays an option.
  - The same pattern as the USB card: one microcontroller family and one toolchain.
  - The RP2350 handles the SD protocol, start-up and buffering, and presents IDE-style registers (below).
  - It can also read FAT well enough to mount image files from a PC-formatted card as units.
  - The RP2350 is 3.3 V: its outputs need HCT/ACT receivers or level shifters on the bus side. Check whether its
    inputs are 5 V tolerant before relying on it.
  - CompactFlash was the alternative: bus-ready with a little glue and no firmware, but a fading medium, with
    8-bit mode and DMA support varying between cards.
  - Optionally, a ProDOS block driver in a slot ROM could show the same card, or a mounted `.po` image, as a hard
    disk in Apple mode. That goes beyond a stock IIe, so it's an extra, not a requirement. Otherwise the block is
    hidden in Apple mode.

### Mass storage registers ###

**Decided.** Module ID `0x7`, `$C380–$C3FF`. The model is an IDE disk: write a sector number and a count, then a
command, and the data streams through one port, fed by DMA channel 0 or a CPU loop.

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00` | CTRL | read/write | bit 0 DMA: assert DRQ0 when DATA has a byte, or room for one (DMA revision 2: pull /DREQ by module 7, which channel 0 serves by its reset CARD `$07`). Bit 1 RESET: start the SD card up again |
| `$01` | STATUS | read | bit 7 BUSY; bit 6 READY (card started up); bit 5 PRESENT (card detect); bit 4 WP (write-protect switch); bit 3 DATA (a byte, or room for one, at DATA); bit 0 ERR |
| `$02` | ERROR | read | error code when ERR is set: no card, out of range, CRC, timeout, write-protected, unit not mounted |
| `$03` | CMD | write | starts a command |
| `$04` | UNIT | read/write | 0 = the whole card; 1–7 = a mounted image file |
| `$06–$09` | LBA | read/write | 32-bit sector number, low byte first: up to 2 TB |
| `$0A–$0B` | COUNT | read/write | sectors; 0 means 65,536. Reading returns how many are left |
| `$0C` | DATA | read/write | the byte stream for the transfer |
| `$0D–$0E` | BUFPTR | read/write | position within the current 512-byte sector, for jumping into a sector without DMA |
| `$10` | IEN | read/write | bit 0 command done; bit 1 card inserted or removed |
| `$11` | ISR | read; write 1 to clear | the same bits, pending; any of them pulls bit 7 of IRQL (ID 7) low |
| `$7F` | ID | read | card type and revision |

**Commands** (numbers from ATA where one exists):

| CMD | Name | Action |
|---|---|---|
| `$20` | READ | read COUNT sectors from LBA through DATA |
| `$30` | WRITE | write COUNT sectors to LBA from DATA |
| `$E7` | FLUSH | write cached data out to the SD card; use before removing it |
| `$EC` | IDENTIFY | one sector through DATA: capacity, card type, name, and the units table |
| `$A0` | MOUNT | DATA takes a sector holding a unit number and a file path; the file becomes that unit |
| `$A1` | UNMOUNT | detach UNIT |
| `$A2` | LIST | the directory at the path in DATA, as fixed-size entries, for a boot or image menu |

**Transfers.**

- **With DMA**, for example 16 sectors to `$20000`: program channel 0 (ADDR `$20000`, COUNT 8192, demand mode, card
  to memory), set CTRL.DMA, write LBA and COUNT, then CMD = `$20`. DRQ0 drops while the card fetches the next
  sector, so demand mode pauses and resumes. *(On revision 2: SELECT 0 with CONTROL saved first, and each take of
  the bus begins with a poll, one cycle; [the DMA controller, revision 2](#the-dma-controller-revision-2).)* The DMA terminal count or the command-done interrupt ends it. The
  RP2350 reads SD in 4-bit mode faster than the bus takes bytes, so this runs near full bus speed, about 7 MB/s.
  Transfers over 128 sectors need a new DMA program or AUTO mode, since the DMA count stops at 64 KB.
- **Without DMA**, an `LDA DATA` / `STA (ptr),Y` loop takes about 11 cycles per byte, roughly 650 KB/s: plenty for
  the BIOS and the Monitor.
- **Writes** run the same way in the other direction. The card may cache them, hence FLUSH.

**Units.** Unit 0 is the raw card. On a FAT-formatted card, MOUNT attaches an image file as units 1–7, where LBA 0
is the file's first sector. Disk images copied from a PC become drives, a boot menu can LIST and start them, and the
OS still sees plain sectors with any file system it likes. The RP2350 only needs enough FAT to find a file's
sectors. The command numbers are fixed now; the raw card works before the mounting firmware exists.

**Proposed, as the emulator models it (2026-10-25)** (`emulator/components/devices/sd_card.h`). These fill in what
the tables leave open:

- **Buffers and DATA.** The card holds two 512-byte buffers, so it fetches (or programs) one sector while the bus
  empties (or fills) the other. STATUS.DATA is set while the whole current sector is there to read, or a whole
  buffer is free to write. A loop can wait for it once a sector and then move 512 bytes without checking again.
  With CTRL.DMA set, DRQ0 follows it (on DMA revision 2, the card's /DREQ). When DATA is clear, reading DATA returns `$FF` and a write to it is dropped.
- **LBA and COUNT advance** as each sector passes through DATA. During a transfer COUNT is the number of sectors
  left; afterwards LBA names the next sector, so consecutive reads need only a new COUNT and CMD.
  MOUNT and LIST set COUNT to 1 for their parameter sector, whatever the program wrote, and don't move LBA.
  IDENTIFY's sector comes from the firmware and moves neither.
- **Errors.** A failed command sets ERR and ERROR, stops at once and sets ISR bit 0 like a finished one; ERR
  stays set until the next command. ERROR codes:
  - 1: no card, or not started up yet;
  - 2: out of range (past the last sector of the card or unit);
  - 3: CRC;
  - 4: timeout, or a host error in the emulator;
  - 5: write-protected;
  - 6: unit not mounted, or not a unit 1–7;
  - 7: unknown command;
  - 8: not found (MOUNT's file or LIST's directory);
  - 9: no FAT volume on the card, or a damaged one.

  A command written while BUSY is ignored. CTRL bit 1 (RESET) is a strobe: it abandons the command and starts the
  card up again.
- **Starting up** takes about 50 ms, after power-up, a card going in, or CTRL.RESET. READY is set once it has.
- **/RESET** abandons a command (sectors already programmed stay written) and clears the registers. The card stays
  started up and units stay mounted, so a boot menu can mount an image and reset into it.
- **IDENTIFY's sector.** Everything not listed is 0.

  | Offset | Contents |
  |---|---|
  | `$000–$003` | the card's sectors |
  | `$004` | its type: 0 none, 1 SDSC (up to 2 GB), 2 SDHC (up to 32 GB), 3 SDXC |
  | `$005` | flags: bit 0 write-protected, bit 1 a FAT volume found |
  | `$006–$007` | the firmware's version, major then minor |
  | `$010–$02F` | the card's name, ASCII, NUL padded |
  | `$040` | a 32-byte entry for each of units 1–7: sectors (4 bytes), flags (bit 0 mounted, bit 1 read-only), then from +8 the file's name (24 bytes) |
- **MOUNT, UNMOUNT and LIST.**
  - MOUNT's sector holds the unit (1–7) at +0, flags at +1 (bit 0 read-only) and the path from +2,
    NUL-terminated. The unit is the file's sectors, rounded up: a last part-sector reads on into the slack of
    its cluster, so a program shorter than a sector still loads. It is read-only if the flag, the file's read-only
    attribute or the card's switch says so. Mounting a unit that is already mounted replaces it.
  - UNMOUNT detaches UNIT.
  - LIST's sector holds the directory's path from +0. The card then sends the entries out through DATA, with
    COUNT set to the number of sectors they fill. Each sector holds 8 entries of 64 bytes:

    | Offset | Contents |
    |---|---|
    | +0 | the size, 4 bytes |
    | +4 | the FAT attributes: bit 0 read-only, bit 4 a directory |
    | +8 | the long name, 56 bytes, NUL padded; a longer name is cut |

    An entry of zeros follows the last one. `.`, `..` and volume labels are left out.
  - **Paths** are `/`-separated from the root. A name matches its long or its 8.3 form, ignoring case.
  - **Where the FAT is:** on the whole card (a "superfloppy") or in the MBR's first FAT partition, as FAT12, FAT16
    or FAT32 with long names.
  - **When it's read:** the firmware reads the FAT again at each MOUNT and LIST. A unit's sectors are found
    when it is mounted. Its writes go into the file's own clusters, so the FAT never changes and the RP2350 needs
    no FAT writer.
  - **A damaged chain** (one that is short, runs on, or loops) fails with error 9 rather than hanging.
  - **Taking the card out** unmounts everything.
- **Timing, the emulator's figures:** 100 µs to a read's first sector, then 25 µs for each further sector (SD in
  4-bit mode, about 20 MB/s), and 25 µs to program a written sector. A DMA at a byte a cycle takes 72 µs a sector,
  so after the first sector the bus never waits. FLUSH takes 100 µs.


## Apple II Mode ##

**Entering.** Native software writes the mode register.

1. The north bridge switches to the Apple mapping, and switches the CPU card's divider to the Apple clock.
2. The DMA controller goes idle and the video card switches to Apple modes.
3. The north bridge pulses /RESET.
4. The CPU starts from the IIe ROM image's reset vector in bank E, as a IIe does at power-on.

Before the switch, the BIOS has loaded the ROM image into bank E and cleared banks 0–3 and the second `$D000` bank
(`$D000–$DFFF` of banks 4 and 5).

**Proposed (2026-11-10), video card revision 2** ([above](#video-card-revision-2)). The mode works as it does
today; only where the card keeps its copy and the character ROM changes:

- The card still snoops writes to banks 0–1 and the display switches (`$C050–$C05F`, `$C00C–$C00F`); it is now
  the card's only snoop path. Its copy of banks 0–1 is in card memory, `$1C0000–$1DFFFF`, which nothing outside
  the card can see.
- `C806R` copies the character ROM into card memory at `$1E0000` through the port before it writes MODE, instead
  of to `$BE000`.
- Card memory survives /RESET: the pulse that enters the mode, and every Ctrl-Reset, reset the card's registers but
  never its memory, so the character ROM stays.
- The aperture is inert while APPLE is set, the registers can't be reached, and DMA is idle, so the port is never
  used. The timing is unchanged: 640×480 out, 560×384 centred, `$C019` in Apple timing.

**Proposed (B0): on the new bus** the mode works as it does today; `$C806` takes the IIe image from ROM A's blocks
0–3, and the steps are checked one by one in [the flash: ROM A and ROM B](#the-flash-rom-a-and-rom-b). *As built
(B5, BIOS 0.16, [below](#b5-as-built)):* so it does; the copy, the fills, MODE and /RESET are unchanged, and only the
image's and the set's sources moved.

**Leaving (decided):** only by hard reset. Power-on and the reset button always start in native mode, so nothing
an Apple program does can leave by accident.

**Two kinds of reset (decided).** IIe software relies on Ctrl-Reset resetting the CPU *without* leaving Apple mode.

- **Ctrl-Reset** (or Ctrl-Delete) on the USB keyboard makes the USB card pulse the bus's /RESET. The APPLE bit
  survives it. The north bridge's own pulse on entering Apple mode is the same kind.
- **Hard reset:** power-on and the reset button go into the north bridge, which clears APPLE and then pulses
  /RESET. /RESET alone never clears the bit.

**What a stock enhanced IIe needs, and where it comes from**

| IIe feature | Provided by |
|---|---|
| 65C02 | the W65C02S. Its extra opcodes (BBR, BBS, RMB, SMB, WAI, STP) do nothing on the IIe's 65C02 and aren't used by Apple software |
| 128 KB with auxiliary memory, language card, and all soft switches | north bridge |
| Keyboard (`$C000`, `$C010`), Open- and Closed-Apple keys (`$C061/$C062`) | USB card, from the USB keyboard |
| Speaker (`$C030`) | USB card (emulated), or the sound card |
| Game port: paddles and buttons (`$C064–$C070`) | USB card's game port. Paddle timing depends on the 1 MHz clock |
| Video: all IIe modes, `$C019` vertical blank | video card, by snooping |
| Disk (slot 6) | Disk II clone with a Floppy Emu |
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
- **Proposed (2026-10-30):** a W65C22S VIA on the serial card, for timers, two digital joystick ports and a user port
  ([below](#the-via-and-the-joystick-ports)).

**Proposed: the USB card**

- **USB host:** an RP2350 running TinyUSB. Its PIO answers the bus, holding RDY when it needs time, the same pattern
  as the sound and SD cards. *(On the new bus, by /WAIT: [wait states](#wait-states-and-the-read-budget).)*
- **Game port:** a DE-9 Apple-style joystick port. The paddles use a real 558 timer, so Apple-mode readings stay
  correct whatever the CPU speed. A USB gamepad mapped onto the paddles is an option for later.
- **Speaker:** a flip-flop that changes on each access, with a small amplifier.

**USB card registers (decided)** (module ID `0xB`, `$C580–$C5FF`, offsets from `$C580`):

*Keyboard.* Two interfaces: cooked ASCII for simple software, raw events for games and an OS.

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00` | KEY | read | cooked: ASCII in the current layout, with key repeat. Bit 7 is the strobe, as on the Apple |
| `$01` | KEYCLR | read/write | any access clears the strobe; reading returns bit 7 = a key is held down |
| `$02` | MODS | read | the modifiers held now, in USB HID bit order (L-Ctrl, L-Shift, L-Alt, L-GUI, then right) |
| `$03` | EVCOUNT | read | bytes waiting in the raw event FIFO (stops at 255) |
| `$04` | EVENT | read | raw events, two bytes each: the USB HID usage code, then flags (bit 7 = release). Modifiers arrive as keys too (`$E0–$E7`). Asserts DRQ while not empty (DMA revision 2: by module B, served by a channel whose CARD software sets to `$0B`; `backplane.machine` leaves it off /DREQ) |
| `$05` | LEDS | write | bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock |
| `$06` | REPEAT | read/write | for KEY: bits 0–3 rate, bits 4–5 delay |
| `$07` | KBCFG | read/write | bits 0–3 layout (0 = US). Bit 7: Pause/Break pulls /NMI, to break into the Monitor |

Raw codes are USB HID usages rather than PC scancodes: one byte, what the keyboard actually sends, no prefixes.

**Decided (2026-10-24), filling in what the tables leave open,** as the emulator does it:

- KEY holds one character, as on the Apple: a new one replaces it, read or not. It follows the Apple keyboard's
  conventions in native mode too (arrows `$08` `$15` `$0A` `$0B`, Delete `$7F`, Ctrl with `$40-$5F` or a letter
  giving `$00-$1F`; with `` ` `` `{` `|` `}` `~` too, as a terminal cooks them: Ctrl-`` ` `` is NUL, Ctrl-`{` ESC). Caps Lock acts on letters only, Shift undoes it, and it is **on at power-up**, as a IIe is
  usually left.
- REPEAT: the last character key pressed repeats while held, after (bits 4-5 + 1) x 250 ms, every (bits 0-3 + 1) x
  33 ms (30 to about 2 a second). It resets to `$10`: 500 ms, 30 a second.
- The raw event FIFO holds 128 bytes (64 events); when full, a new event is dropped whole, so pairs never split.
- Each paddle's GAME bit stays high for about 11 us per count after PTRIG, in real time, as on the Apple.
- An event sets its ISR bit whether or not IEN enables it; the card's IRQ is ISR AND IEN. A key press or release
  sets the key bit even when the raw FIFO has no room for it, and so does a repeat (a new character in KEY).
- /RESET puts back what the CPU programs (LEDS, REPEAT, KBCFG, MCTRL, MAXX/MAXY, IEN, ISR) and empties the raw
  FIFO. Keys held, Caps Lock, the mouse's position and the game port are the host's and carry on.
- Key changes reach the card at the keyboard's USB polling interval, one per 8 ms, so a burst of keys arrives as
  separate presses rather than one overwriting the last in KEY.

*Mouse.*

| Offset | Name | Purpose |
|---|---|---|
| `$10` | MCTRL | bit 0: absolute mode |
| `$11` | MBTN | buttons, bits 0–4 |
| `$12–$13` | MX | reading `$12` latches MX, MY and MWHEEL together. Relative mode: signed movement since the last latch. Absolute mode: the position, so an OS pointer goes straight into a sprite |
| `$14–$15` | MY | |
| `$16` | MWHEEL | signed wheel movement |
| `$17–$18` | MAXX | absolute-mode limit, for example 639 |
| `$19–$1A` | MAXY | absolute-mode limit, for example 479 |

*Game port and speaker.*

| Offset | Name | Purpose |
|---|---|---|
| `$20` | GAME | bits 0–3: the four paddle timer outputs, raw. Bits 4–6: buttons 0–2, 1 = pressed |
| `$21` | PTRIG | writing starts the 558, like `$C070` |
| `$22–$25` | PADDLE0–3 | 0–255, measured by the RP2350 timing the 558 itself: no counting loop or calibration |
| `$26` | SPKR | any access toggles the speaker, like `$C030` |

*Interrupts and status.*

| Offset | Name | Purpose |
|---|---|---|
| `$30` | IEN | bit 0 key event, bit 1 mouse change, bit 2 game button change, bit 3 a device connected or disconnected |
| `$31` | ISR | the same bits, pending; write 1 to clear. Any of them pulls bit 3 of IRQH (ID B) low |
| `$32` | DEVICES | bit 0 keyboard, bit 1 mouse, bit 2 gamepad (reserved) |
| `$7F` | ID | card type and revision |

**In Apple II mode** the north bridge routes these IIe addresses to ID `0xB`, and the native block is hidden:

| Address | Behaviour |
|---|---|
| `$C000–$C00F` (read) | keyboard: Apple ASCII with the strobe. Writes here are the north bridge's soft switches |
| `$C010` | clears the strobe; bit 7 = any key down |
| `$C011–$C01F` (read) | bits 0–6: the keyboard latch (the north bridge drives bit 7) |
| `$C030` | speaker |
| `$C061–$C063` | buttons 0–2. Open Apple is left GUI or Alt, Closed Apple right GUI or Alt, wired with buttons 0 and 1 as on a IIe |
| `$C064–$C067` | paddle timer outputs in bit 7 |
| `$C070` | trigger the 558 |

The card follows the Apple keyboard's conventions: arrows give `$08`, `$15`, `$0A`, `$0B`; Delete gives `$7F`; Caps
Lock behaves as on the IIe; Ctrl-Reset pulses /RESET (see [Apple II Mode](#apple-ii-mode)).

**Proposed: the serial and parallel card** (module ID 6, "Serial A/B, Parallel & VIA"; the VIA and the joystick
ports added 2026-10-30, [below](#the-via-and-the-joystick-ports))

- TI's TL16C552A: two 16550-compatible UARTs with 16-byte FIFOs and an IEEE 1284 bidirectional parallel port, in
  one chip. It is the classic PC combination for COM1, COM2 and LPT1.
- **DMA:** the UARTs' TXRDY/RXRDY outputs drive DRQ lines; the parallel port gets DMA from the card's AUTOSTROBE.
  *(On the new bus one request, by module 6, for the stream DMASTREAM picks: [the serial card's stream
  select](#the-serial-cards-stream-select).)*
- **Clock:** a 1.8432 MHz crystal, so the PC's divisors apply: 1 is 115,200 baud, 12 is 9,600.
- **Glue logic:** a small CPLD turns PHI2 and RWB into the chip's Intel-style /IOR and /IOW, and adds the DMA
  routing and interrupt summary below.
- The W65C51N was considered and rejected: its stuck transmit flag makes DMA and interrupt-driven transmit
  unreliable.

**Serial card registers (decided)** (`$C300–$C37F`). The 552A's own registers are mapped directly in PC order, so
16550 and LPT driver code needs only its base addresses changed.

| Range | Contents |
|---|---|
| `$C300–$C307` | COM1: RBR/THR/DLL, IER/DLM, IIR/FCR, LCR, MCR, LSR, MSR, SCR |
| `$C308–$C30F` | COM2: the same |
| `$C310–$C312` | LPT: data, status, control, as at a PC's LPT base + 0–2 |
| `$C320–$C323` | card registers |
| `$C340–$C34F` | the W65C22S VIA, its 16 registers in WDC's order ([below](#the-via-and-the-joystick-ports)) |
| `$C37F` | ID: card type and revision. **Decided (2026-11-13, the owner, [diag.md](diag.md#the-owners-answers), answer 6): `$61`**, module 6 at revision 1. The emulator's stand-in 6551 has no ID register: its four registers are mirrored through the block, so `$C37F` reads its control register |

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$20` | DMASEL1 | read/write | the source on DRQ1: 0 none, 1 COM1 receive, 2 COM1 transmit, 3 COM2 receive, 4 COM2 transmit, 5 LPT output |
| `$21` | DMASEL3 | read/write | the same for DRQ3, so a second stream can use the general-purpose channel. *On the new bus, stream 1, chosen by DMASTREAM at `$24`: [the serial card's stream select](#the-serial-cards-stream-select)* |
| `$22` | INTSTAT | read | bit 0 COM1, bit 1 COM2, bit 2 LPT, bit 3 the VIA: which part is interrupting. Any of them pulls bit 6 of IRQL (ID 6) low. The enables stay in the chip's IER and LPT control registers |
| `$23` | LPTCTL | read/write | bit 0 AUTOSTROBE: in DMA mode the card generates STROBE itself |

- **DMA routing.** On /DACK1 or /DACK3 (on the new bus, a grant naming module 6, with DMASTREAM choosing the
  stream) the CPLD selects the chosen source's data register (chip select, A0–A2 = 0,
  /IOR or /IOW by direction), so the UART needs no address, as fly-by requires. With FCR bit 3 set (DMA mode 1),
  receive requests at the FIFO trigger level or timeout and transmit requests while the FIFO has room; both suit
  demand mode. The classic receive arrangement is channel 1 in AUTO mode as a ring buffer, the OS reading the
  channel's current address to see how much has arrived.
- **AUTOSTROBE.** The 552A's parallel port has no DMA request and only strobes when software writes its control
  register. With AUTOSTROBE set, the card asserts DRQ while the printer isn't BUSY, and after each /DACK write (a grant, on the
  new bus) holds
  the data, pulses STROBE and waits for ACK, keeping the datasheet's 1 µs setup, pulse and hold.
- **Bus timing** (TI datasheet SLLS189D). /IOR and /IOW must be low for at least 80 ns, longer than PHI2's ~70 ns
  high time at ÷2, so **every access takes one RDY wait state** (on the new bus, a /WAIT one). The chip also needs 80 ns between strobes, so the
  CPLD adds a second wait state when accesses come back to back (DMA bursts, read-modify-write instructions). In
  FIFO mode the chip needs 425 ns between reading the receive FIFO and reading IIR or LSR; drivers must allow for it,
  and at serial speeds it costs nothing.
- **Apple mode:** the block is hidden. A stock IIe has no serial card; a Super Serial Card emulation in slot 2 would
  be a later extra.

#### The VIA and the joystick ports ####

**Proposed (2026-10-30).** A WDC W65C22S on the serial card gives the board what it otherwise lacks, timers that
interrupt at any rate, and two digital joystick ports for native games.

**Why.** Without it, the only time sources are the video card's vertical blank (60 Hz) and scan-line interrupts and
the DS3231's 1 Hz tick ([oolib.md](oolib.md), open question 7). The Apple-style game port stays on the USB card: IIe
software reads it a bit an address, merged with the Open and Closed Apple keys, and the RP2350 times its 558
paddles; the VIA's ports are native only.

**The part.** The W65C22S, still made by WDC, is rated to 14 MHz at 5 V, so it runs at ÷2 with **no wait states**:
the CPLD selects it with PS0–PS3 = 6 and A6–A4 = `100`, A0–A3 go to RS0–RS3, and it takes PHI2 from the backplane.
Its IRQB goes to the CPLD, which shows it as INTSTAT bit 3 and pulls bit 6 of IRQL. /RESET leaves every port
line an input and every interrupt off, the timers' included: their counters run on (the chip doesn't clear them), but
neither interrupts until a program loads it.

**Registers** (`$C340` + RS, WDC's order):

| Offset | Name | Purpose |
|---|---|---|
| `$0` | ORB/IRB | port B: joystick 2 and the user port |
| `$1` | ORA/IRA | port A: joystick 1 and the user port, with handshake |
| `$2`–`$3` | DDRB, DDRA | data direction, 1 = output |
| `$4`–`$7` | T1C-L, T1C-H, T1L-L, T1L-H | timer 1: counter and latches |
| `$8`–`$9` | T2C-L, T2C-H | timer 2 |
| `$A` | SR | the shift register (CB1 clock, CB2 data) |
| `$B` | ACR | timer modes, shift register mode, latching |
| `$C` | PCR | CA1, CA2, CB1, CB2 control |
| `$D`–`$E` | IFR, IER | interrupt flags and enables |
| `$F` | ORA/IRA | port A without handshake |

**The ports:**

| Lines | Use |
|---|---|
| PA0–PA4 | joystick 1: up, down, left, right, fire |
| PB0–PB4 | joystick 2: the same |
| PA5–PA7, PB5 | the user port: general I/O |
| PB6 | the user port; also timer 2's pulse-counting input |
| PB7 | the user port; also timer 1's square-wave output |
| CA1, CA2, CB1, CB2 | the user port's handshake and shift register lines |

**Joystick ports.** Two DE-9 males in the Atari and Commodore pinout, so the digital joysticks made for the 2600,
the C64 and the Amiga plug straight in: pin 1 up, 2 down, 3 left, 4 right, 6 fire, 8 ground, 7 +5 V (for
joysticks with autofire or LEDs; a polyfuse limits it). Each switch closes to ground, so a line reads **0 when
pressed**; 10 kΩ pull-ups to +5 V keep the rest at 1, since the VIA's inputs have none. Pins 5 and 9 (the paddles
and second buttons of the C64 and Amiga) aren't connected. Sega pads, which need a select line on pin 7, aren't
supported. A game reads a stick with one load: `LDA $C341`, then `AND #$1F`.

**The user port.** The other ten lines and the four control lines go to a 2×10 pin header with +5 V and ground, for
an LCD, a sensor, a second keyboard or anything a hobbyist builds, as on the C64's and BBC Micro's user ports.

**Timers.** Both count PHI2, 7.16 MHz.
- **Timer 1** in free-running mode is OODOS's system tick (proposed: 1 kHz, a latch of 7,157, since a free-running period is the latch + 2 cycles: 7,159 cycles, 0.002% fast; [oolib.md](oolib.md)).
  It could also drive PB7 as a square wave.
- **Timer 2** is the program's: a one-shot delay, or a count of pulses on PB6.

**In Apple mode** the VIA is hidden with the rest of the card, its timers' interrupts are never seen (IRQL is
native-only), and the joystick ports are unused: the IIe's game port is the USB card's.

**Module IDs (decided)** for the new cards, using the free IDs `0x8`–`0xD`:

| ID | Module |
|---|---|
| 0x8 | DMA controller |
| 0x9 | Video |
| 0xA | Sound (2× SID) |
| 0xB | USB, game port and speaker |
| 0xC | Disk II controller |
| 0xD | north bridge registers (no card) |

Native storage (SD) is "Mass Storage", ID 7. The front panel is "Console", ID 0xE. On the new bus memory is selected by address and IDs 0–5 are free ([the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)). Since B1 the RAM is one card of 1 MB,
selected by its address; RAM A–D name its four 256 KB quarters (on the first design's bus they were IDs 0–3, four
modules chosen by A18–A19).


## Backplane Signals ##

**Decided (the owner, 2026-11-14): the new connector.** The 62-pin ISA-style connector is full. The bus moves to
the 98-pin card-edge connector that PCIe x8 slots use, which is cheap and easy to find.
*(2026-11-17, B6: the owner gave no map of their own, so B6 recorded the signal list and a draft pin-out in
[modules.md](modules.md), every position open and the owner's: [B6: as built](#b6-as-built).)* It carries:
- **every signal documented below,** the 62-pin bus's and the assignment's;
- **a 16-bit data bus,** D0–D15;
- **24 address lines,** A0–A23 (see [Future: a 65C816 and 16 MB](#future-a-65c816-and-16-mb): the wider bus,
  not the multiplexed one);
- **eight DMA channels** ([DMA](#dma)), the controller moving onto the CPU card, granted by module ID rather than
  by a DRQ and DACK pair each ([the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane));
- **/MEM, /DMA, /DREQ and /WAIT** (the same section; /SEL went with the expansion chassis).

The decisions with it:
- **The slots are keyed with a shroud,** so a normal PCI or PCIe card can't be plugged in. The power pins don't
  follow PCIe's.
- **Power is 5 V only.** The backplane no longer carries +24 V; this replaces the 62-pin bus's +24 V (Power, below).
- **An expansion chassis is more slots, nothing else** (the owner, 2026-11-14, replacing the expansion port at
  module ID `0xF` and its fifth module-ID bit, neither of which comes back). *Replaced the same day: no expansion chassis; one backplane of about
  15 slots, with three pins reserved for a bridged bus ([the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)).*

**The 16-bit data bus (the owner's direction, 2026-11-14; not designed yet):**
- **DMA transfers 16 bits.** A card with DMA moves a word on D0–D15 in each transfer to or from main memory, so a
  transfer takes half the bus cycles it does now. *Refined in B4: each card's documentation says whether its data
  register takes words; WIDE is set only for those ([16-bit transfers](#16-bit-transfers)).*
- **The CPU card may make the bus 16 bits throughout.** The CPU card would read or write all 16 bits of memory on
  each bus cycle and buffer them, shifting each byte into or out of the 8-bit CPU. The bus would then carry only
  16-bit cycles.
- **What it needs:**
  - the RAM cards 16 bits wide;
  - a way to write one byte of a word (byte enables, or A0 kept for that), since the CPU and many cards write
    bytes;
  - a rule for when the buffered byte is still good: a write by DMA or another master to that word, or an I/O
    read, must not be answered from it.

Not designed yet: how the 16-bit bus works (above: the CPU card's word buffer); the pin positions (B6's draft in
[modules.md](modules.md) gives the signals, decided, and positions that are the owner's to choose); the socket's
part; and the shroud and its key (the owner's). The emulator models the bus's signals, not its pins, so the connector doesn't
change it; memory by address, DMA by module ID, eight channels and the wider bus do, when they are built.

### The new bus: memory by address, DMA by module ID, one backplane ###

**Decided (the owner, 2026-11-14), to be detailed with the pin map.** Three changes that go together. The module ID
lines (PS0–PS3) come to select only cards with registers; memory is selected by its address; a DMA transfer is
granted by naming the card's module ID; and one backplane holds every card (the expansion chassis first decided
with them is dropped, below).

**Memory by address.**
- RAM and flash cards decode their own range from A0–A23 (ROM A is on the CPU card, decided later the same day:
  [one backplane](#one-backplane-the-slots-and-the-reserved-pins)) and answer only when **/MEM** is low. The north bridge
  drives /MEM and the physical address on a memory cycle (the CPU through a window, or the memory side of a DMA
  transfer), and holds /MEM high on an I/O cycle at `$C000–$C7FF`, so no memory card answers there whatever the
  address lines hold.
- RAM keeps its place, RAM A–D at `$000000–$0FFFFF`, 256 KB each (decided later the same day: one RAM card of 1 MB, [one backplane](#one-backplane-the-slots-and-the-reserved-pins)). The flash gets a fixed place in the 24-bit map
  (proposed: ROM A at `$F00000–$F7FFFF`, ROM B, the cartridge, at `$F80000–$FFFFFF`). A window's ROM bit keeps
  its meaning: the north bridge puts out a flash address instead of a RAM one, so software doesn't see the change.
- **Module IDs `0x0`–`0x5` are free** (they were RAM A–D, ROM A and ROM B, which never had registers: their blocks
  at `$C000–$C2FF` are unused today). `0xF` is the idle bus's ID, the value PS0–PS3's pull-ups give a cycle no
  master drives, and is never given to a card (B6, review round 2). Six IDs for new cards; the interrupt poll already
  gives each ID its own bit, in IRQL or IRQH.
- With flash on the memory side, the DMA controller can read it too (today it reaches only RAM), for instance to
  copy the Apple IIe ROM at boot.

**DMA by module ID,** in place of eight DRQ and DACK pairs (16 pins become 2):
- **Request:** a card wanting a transfer pulls **/DREQ**, one shared open-drain line. The controller then runs a
  request poll, as the north bridge's /IACK poll does for interrupts: in a cycle with /DMA and /IACK both low,
  each card with a request pulls its own data line, D(ID). With D0–D15 every ID has one.
- **Grant:** a channel's registers gain a module-ID field. The controller serves the enabled channel, highest
  priority first (channel 0 highest, as now), whose module is requesting. In the transfer cycle it drives the
  memory address with /MEM low, puts the card's ID on PS0–PS3 and pulls **/DMA** low. The memory card answers the
  address; the card named by PS0–PS3 puts or takes its data register (fly-by, one cycle a transfer, as now). A
  card's logic is: PS = mine and /DMA high, a register chosen by A0–A6; PS = mine and /DMA low, my data register.
- **Release:** the card lets /DREQ go, or the count runs out; TC is asserted with the last transfer, as now.
- **A card with two streams** (the serial card's two sources on /DACK1 and /DACK3 today) chooses between them by
  a register of its own, as the sound card's DRQSEL already does, or takes two module IDs.
- **Software:** the existing registers keep their places and meaning. At reset each channel's module field names
  the card today's wiring gives it (channel 0 storage, 2 the sound card; channel 1 and 3 in the plan), so code
  that never writes the field works as before. Channels 4–7, and where their registers go in the controller's
  128-byte block (STATUS, CONTROL, ABORT and the ID byte sit at `$40`–`$42` and `$7F`), are designed with the
  card. *(Superseded by B0: revision 2 has a new layout, one channel window chosen by CONTROL's SELECT, and the BIOS
  and OODOS move to it in B2: [the DMA controller, revision 2](#the-dma-controller-revision-2).)*

**One backplane (decided, the owner, 2026-11-14), replacing the plain expansion chassis.** B0's expansion chassis
(more slots in a second box on a buffered link, with /SEL and /CDRV, "ALL pins on the bus carry over to the
chassis", and memory and bus masters kept in the main chassis) is dropped. Memory is one card, selected by address,
and DMA is on the CPU card, so every card fits the one backplane; the module IDs (16), not the slots, are the limit.
The timing that remains is in [wait states and the read budget](#wait-states-and-the-read-budget).

**New lines:** /MEM, /DMA, /DREQ and **/WAIT** (slow cards' wait states, [wait states](#wait-states-and-the-read-budget)), and the reserved BR0–BR2 ([one backplane](#one-backplane-the-slots-and-the-reserved-pins)). /SEL and /CDRV went with the expansion chassis. **Dropped on the new bus:** DRQ0–DRQ3 and /DACK0–/DACK3. The 62-pin
assignment ([the 62-pin bus](#the-62-pin-bus-the-first-designs)) is the first design's; the new bus's draft
pin-out is in [modules.md](modules.md) (B6).

**Proposed (B0, 2026-11-14): the details,** below, for the emulator's steps B1–B5 and B7 and the owner's pin map
(B6). **The owner's decisions** (2026-11-14) are written in as decided:
- ROM A at `$F00000` and ROM B at `$F80000`.
- ROM A is 512 KB, two 256 KB chips side by side; ROM B does the same.
- The Apple IIe ROM images are in ROM A's first 64 KB, chosen by an argument, with their character sets in blocks
  4–5; ROM B is for game cartridges only.
- The video aperture moves to `$E00000`, reached by wider windows, with MAP's AP8 and `DM_BANK8`, so the machine
  has the whole 1 MB.
- DMA revision 2 has the owner's register layout: one channel window chosen by CONTROL's SELECT. Channel 1's
  default module is video and channel 3's none. The serial card selects its stream with DMASTREAM.
- The review's sixteen findings are fixed as recommended (round 1).
- The IIe ROM's copy by DMA is left for later.

#### The physical map on the new bus ####

**Decided.** Addresses are 24 bits, A0–A23. Memory cards decode their own ranges; the module IDs choose only cards
with registers.

```
  $000000 +--------------------------+  the RAM card, 1 MB, in four quarters
          | banks 0-3, "RAM A"       |
  $040000 | banks 4-7, "RAM B"       |
  $080000 | banks 8-B, "RAM C"       |  bank 8 sent to the aperture while AP8 is set
  $0C0000 | banks C-F, "RAM D"       |
  $100000 +--------------------------+
          |                          |
          |  unused: 13 MB, room for |  nothing answers: a read gives $FF, a write is lost
          |  more RAM later          |
          |                          |
  $E00000 +--------------------------+
          | video aperture   64 KB   |  slots 0-3 at +n x $4000
  $E10000 | unused, kept for cards'  |
          | memory (960 KB)          |
  $F00000 +--------------------------+
          | ROM A  512 KB  blocks 0-31  (two 256 KB chips: even and odd bytes)
  $F80000 | ROM B  512 KB  blocks 32-63, the game cartridge (the same)
  $FFFFFF +--------------------------+
```

| Physical | Size | What answers with /MEM low | Window block (ROM bit, BLOCK) |
|---|---|---|---|
| `$000000–$03FFFF` | 256 KB | the RAM card: its first quarter, "RAM A" | 0–15 |
| `$040000–$07FFFF` | 256 KB | the RAM card: its second quarter, "RAM B" | 16–31 |
| `$080000–$0BFFFF` | 256 KB | the RAM card: its third quarter, "RAM C". While AP8 is set the CPU card sends no cycle to `$080000–$08FFFF`: it sends it to the aperture ([below](#the-video-aperture-moved-out-of-ram)) | 32–47 |
| `$0C0000–$0FFFFF` | 256 KB | the RAM card: its fourth quarter, "RAM D" | 48–63 |
| `$100000–$DFFFFF` | 13 MB | nothing: room for RAM cards later | (wide windows, below) |
| `$E00000–$E0FFFF` | 64 KB | the video card's aperture, revision 2 | WINnH `$0E`, BLOCK 0–3 |
| `$E10000–$EFFFFF` | 960 KB | nothing: kept for other cards' memory | |
| `$F00000–$F7FFFF` | 512 KB | ROM A | ROM bit, 0–31 |
| `$F80000–$FFFFFF` | 512 KB | ROM B, the cartridge | ROM bit, 32–63 |

A window's ROM bit keeps its meaning: ROM block *b* is physical `$F00000 + b × $4000`, and the north bridge puts
out that address instead of choosing modules 4 and 5. DMA revision 2 drives all 24 bits (ADDR and DEST
are 24 bits, [below](#the-dma-controller-revision-2)), so DMA reaches the flash and the aperture by their addresses.
The RAM card needs no input to keep quiet in bank 8: the redirection is on the CPU card, so no cycle for `$08xxxx` reaches
the backplane while AP8 is set.

**/MEM in each kind of cycle.** The master holding the bus drives /MEM, PS0–PS3, A0–A23 and RWB. That is normally
the CPU card (the north bridge, or the DMA controller beside it); the Console card and the bus spy drive them when
they take the bus with BE. A card with registers answers when PS0–PS3 is its ID with /MEM and /DMA high; a memory
card answers its range with /MEM low. In a BE handover, while no master drives, the backplane's pull-ups hold /MEM,
/DMA, /IACK, /WORD and /OE high, PS0–PS3 at `0xF` (the idle bus's ID, never a card's) and RWB high, so the cycle is
a read that no memory, no card's registers and no poll answer; A0–A23 float, unused (B6, review round 1:
[modules.md](modules.md)). **Taking and giving back the bus** (the Console card and the bus spy): in the cycle it
pulls BE low, the master enables its own drivers, with /MEM and /OE high, RWB a read and PS0–PS3 `0xE`, its ID.
Before it releases BE, it drives /MEM and /OE inactive (high), RWB to read, PS0–PS3 to `0xF` and /WORD high if it
drives /WORD, for at least one cycle, so the pull-ups only hold lines that are already high: a released line on a
loaded passive bus takes many cycles to rise through a pull-up alone. /IACK and /DMA stay the CPU card's. The DMA
controller, on the CPU card, needs no idle cycle when it releases BE, since the CPU card drives the lines again at
once, so its costs ([below](#the-dma-controller-revision-2)) are as they were (B6, review rounds 2 and 3).

| Cycle | /MEM | /DMA, /IACK | PS0–PS3 | Address | Who answers |
|---|---|---|---|---|---|
| CPU, through a window (RAM, flash, the aperture) | low | high, high | `0xD` (the north bridge's own: no card's registers answer) | the physical address the window gives | the memory card whose range it is |
| CPU, the I/O hole `$C000–$C7FF` | high | high, high | the block's ID, from A7–A10 | A0–A6 the register (the rest the CPU's) | the card with that ID; `0xD` is the bridge itself, no card |
| North bridge interrupt poll (IRQL, IRQH) | high | high, low | `0xD` | | each interrupting card pulls D(ID mod 8), on D0–D7; A0 chooses the half |
| DMA: the takeover (RDY, then BE) | high | high, high | `0x8` | | nobody |
| DMA: the request poll ([below](#the-dma-controller-revision-2)) | high | low, low | `0x8` | | each requesting card pulls D(ID) |
| DMA: a fly-by transfer (the grant) | low | low, high | the channel's module | the memory address | the memory by address, and the named card's data register |
| DMA: memory to memory, the read and the write | low | high, high | `0x8` | the source, then the destination | the memory by address |
| Console card EXAM and DEP, the bus spy | low (it drives it) | high, high | `0xE` (its own) | its physical address (PADDR, 20 bits today: A20–A23 are 0 until it widens) | the memory by address. Bank 8 is the RAM card's to them: AP8 is the CPU card's, and they reach the aperture at `$E00000` only with a wider PADDR |
| Apple II mode, memory (`$0000–$BFFF`, `$D000–$FFFF`, the ROM space from bank E) | low | high, high | `0xD` | A0–A15 as they are, A16–A19 from the IIe's switches, A20–A23 0 | the RAM card |
| Apple II mode, `$C000–$C0FF` and a slot's ROM (`$C600`, the Disk II's) | high | high, high | the answering card's: B the USB card's switches, C the Disk II's; `0xD` for the bridge's own switches | A0–A7 | that card |

The video card's Apple II snoop watches writes with /MEM low to `$000000–$01FFFF` (banks 0–1), A20–A23 0, and the
display switches' I/O cycles, as it watches banks 0–1 and the switches now.

**Byte and word cycles (decided, review round 1).**
- **A byte cycle** carries its byte on D0–D7, whatever A0 is. The CPU's accesses, DMA without WIDE and a card's
  byte fly-by are all byte cycles. A 16-bit memory card (RAM or flash) steers: it moves its odd half's byte to or
  from D0–D7 when A0 is 1. So a card that has only D0–D7 can feed every address, odd and even.
- **A word cycle** carries the even byte on D0–D7 and the odd on D8–D15, at an even address: DMA with WIDE, and the
  CPU card filling its word buffer.
- **/WORD** tells the two apart (the owner's, B6): one line, low on a word cycle and high on a byte cycle, pulled
  up on the backplane, so a master that never drives it makes byte cycles ([modules.md](modules.md),
  [Backplane Signals](#backplane-signals)).

**The Console card on the 24-bit bus (decided, review round 1).** Its registers stay 20 bits for now:
- The breakpoint (BPADDR) matches a cycle only when A20–A23 are 0 and A0–A19 equal BPADDR. So it never fires on a
  flash or aperture cycle that shares the low 20 bits.
- LADDR and the lights show A0–A19. A halt on a cycle above 1 MB (the CPU running in the flash, say) shows those
  bits alone, until the registers widen.

#### The flash: ROM A and ROM B ####

**Decided (the owner, 2026-11-14): ROM A is 512 KB, two 256 KB chips side by side,** one holding the even bytes
and one the odd. The SST39SF020A, a 256 KB part, is the same family as today's SST39SF040 and is programmed the
same way.

- **Byte reads by the 8-bit CPU.** The CPU reads the flash in byte cycles (above). The CPU card puts out A0–A23,
  and ROM A's logic (on the CPU card; the cartridge's own for ROM B) enables the chip A0 chooses and steers its byte onto D0–D7. At ÷2 a 55 ns part needs no wait
  state ([the read budget](#wait-states-and-the-read-budget)). A word cycle reads both chips at once, which halves a DMA
  read of the flash ([B4](#what-steps-b1-b7-change)).
- **The CPU card's word buffer.** It may fill a buffer by a word cycle and give the CPU the other byte on the next
  read of that word, without a bus cycle, which a turbo would want.
  - **The rule for the flash: the buffer serves flash reads only through a window with RO set.** While a program
    or erase is in progress, a chip answers reads with its status, which changes from read to read, so a buffered
    status would look finished.
  - The flash update maps the flash with RO clear, as it must to write it, so every read it makes is a bus cycle.
  - Any write to the flash's range empties the buffer.
- **Programming.** A byte write is a byte cycle on D0–D7. The flash's logic writes it to the chip A0 chooses, whose
  write enable is the only one pulsed. Each chip has its own command sequence, at its own addresses, and its own
  status:
  - a chip's command address *c* (`$5555`, `$2AAA` in the chip) is the flash offset 2*c* for the even chip and
    2*c* + 1 for the odd one: `$AAAA` and `$5554`, or `$AAAB` and `$5555`, from ROM A's start;
  - a chip's 4 KB sector holds the even or the odd bytes of 8 KB of the flash. **The smallest erase is 8 KB:** one
    sector in each chip, at the same chip address. A 16 KB block is two sectors in each chip, so erasing it is four
    sector erases;
  - each chip is polled by its own status (data polling on bit 7, or the toggle bit 6), read at an address of its
    own parity. The two chips can work at once: the update starts a byte in the even chip, one in the odd, and
    polls each.
- **What the BIOS's flash update does differently.** The update is new ([B5](#what-steps-b1-b7-change)): today the
  flash can't be written
  ([the Technical Reference, chapter 2](../docs/oo1-technical-reference/02-memory-and-banking.md#reprogramming-the-flash)).
  It does everything above per chip, where one SST39SF040 would take one sequence for every byte. Writing a byte at
  offset *o* means the command sequence at the chip of *o*'s parity, then the byte, then that chip's poll. *As built
  (B5, BIOS 0.16, [below](#b5-as-built)):* the flash is written in the emulator, and the update, from the monitor's
  `C818R` menu, writes an image and its set a pair of bytes at a time, both chips started and then each polled.
- **Unchanged:**
  - the block numbers, 0–31 of 16 KB, and every window value;
  - the reset block, 31, at `$DF`;
  - the diagnostics' blocks 21–23 and their sums (`backplane/bios/romsum.py` sums blocks 21–31 a byte at a time,
    which doesn't care which chip holds a byte);
  - the BIOS's build of the whole 512 KB image.

  What's new is for the programmer: the build also splits the image into its even and odd halves, one file for
  each chip.

**ROM A's layout** (the owner's direction and answers, 2026-11-14):

| Blocks | Offset in ROM A | Physical | What | Written by |
|---|---|---|---|---|
| 0–3 | `$00000–$0FFFF` | `$F00000–$F0FFFF` | Apple IIe ROM images 0–3, 16 KB each, as the ROM sits at `$C000–$FFFF` | the user, by the BIOS's flash update, from a file on the SD card: the IIe ROMs are Apple's, so we can't ship them |
| 4–5 | `$10000–$17FFF` | `$F10000–$F17FFF` | the IIe character sets, **8 KB each** (one erase unit, so writing one never touches another): set *n* at `$10000 + $2000 × n` for image *n*. The BIOS uses its first 2 KB, as it uses the cartridge's today. A set that is erased (its first 8 bytes `$FF`) is made from the VGA font, as now | the same, from the SD card |
| 6–20 | `$18000–$53FFF` | `$F18000–$F53FFF` | free, erased | |
| 21–23 | `$54000–$5FFFF` | `$F54000–$F5FFFF` | the ROM's diagnostics ([diag.md](diag.md)); block 23's last page holds their sums and vectors | the BIOS's build |
| 24–28 | `$60000–$73FFF` | `$F60000–$F73FFF` | the boot splash's samples ([Boot splash](#boot-splash)), as now | the BIOS's build |
| 29 | `$74000–$77FFF` | `$F74000–$F77FFF` | the video demo | the BIOS's build |
| 30 | `$78000–$7BFFF` | `$F78000–$F7BFFF` | the font | the BIOS's build |
| 31 | `$7C000–$7FFFF` | `$F7C000–$F7FFFF` | the BIOS, the reset block | the BIOS's build |

The owner's direction lists blocks 21–23, 29, 30 and 31. Blocks 24–28 hold the splash's samples today
(`backplane/bios/bios.cfg`), so they stay. The flash update writes only blocks 0–5, so the BIOS and the
diagnostics' sums are never at risk from it.

**ROM B (decided)** is the game cartridge only: the BIOS takes nothing from it any more. Like ROM A, it is two
256 KB chips, even and odd, with the same parts, the same flash code and word reads for a game's assets by 16-bit
DMA.

**`$C806` on the new bus (decided: the image by an argument).**
- `$C806` takes the first good image, 0 to 3, so today's callers (`C806R`, OODOS's `ROMAPPLE`) get the one there is.
- **Apple II mode with image A** is a function of the ROM's **dispatching entry**, not an entry of its own. The jump
  table has no spare after `$C815` ([diag.md](diag.md), P2-2), so any later entry goes through one dispatching
  entry, a function number in a register, as review LL5 foresaw ([diag.md](diag.md)). **B5 designs it:** its
  address (the next after `$C815`), its register and its numbering, with these two functions first: the flash
  update, and Apple II mode with A the image.
- It copies image *n*, ROM A block *n*, to RAM block 59 (`$0EC000–$0EFFFF`, bank E's `$C000–$FFFF`) by the CPU,
  through W1 (`NB_ROM | NB_RO | n`, where it maps the cartridge's block 32 today).
- It takes the character set from `$10000 + $2000 × n` (blocks 4–5), where it takes the cartridge's block 33 today.
- **Function 75 gains A = 3, X = the image.** A = 2 is the DIAG launcher's (`DIAG` at `A>`, [diag.md](diag.md),
  P2-1, OODOS interface 10, being built in D3c). A = 3 is refused today ("any other A"), so this is growth. It comes
  with the interface version after D3c's 10. The CCP's APPLE2 takes an optional image number (APPLE2 has been a
  built-in since 2026-11-03, not a `.COM`: [B5: as built](#b5-as-built)).
- **Function 75's own check moves** (review round 1). Before it jumps to the ROM, `F_ROM` in `oodos/sys/bdos.s`
  checks the cartridge itself: it maps `NB_ROM | NB_RO | CART_BLOCK` in W2 and tests `$FBB3`. In B5 it checks ROM
  A's image instead: the first good one for A = 1, image X for A = 3. Its X = `$FF` result keeps its meaning, no
  IIe ROM.
- `NO APPLE IIE ROM IN THE CARTRIDGE` becomes a message that names the flash and the image. *As built (B5):*
  `NO APPLE IIE ROM IN FLASH IMAGES 0-3` from `$C806`, `NO APPLE IIE ROM IN FLASH IMAGE n` from `$C818`
  ([B5: as built](#b5-as-built)).
- **The emulator:** `--apple-rom` loads ROM A block 0 (or image *n*, with a number before the file), and
  `--apple-chars` loads set 0 at `$10000` (or set *n*). Both load over the BIOS's image, whose blocks 0–5 are
  erased. The WebUI's Settings row writes the same blocks, as the flash update would, and still power-cycles the
  machine.

**Apple II mode holds as [the Technical Reference, chapter 13](../docs/oo1-technical-reference/13-apple-ii-mode.md)
describes,** step by step:

| Chapter 13 | On the new bus |
|---|---|
| MODE's APPLE bit, written only by `$C806`, from `C806R` or function 75 | unchanged (and the dispatching entry's Apple II function, function 75's A = 3) |
| The checks: video card revision 2 (`$92`) | unchanged |
| The checks: the DMA controller | `apple.s` and function 75 test `DMA_ID` for revision 2's `$82` ([the B2 migration](#the-b2-migration-revision-1-dropped)) |
| The checks: a IIe ROM, `$06` at its `$FBB3` | the same test, on ROM A's image instead of the cartridge, in the ROM and in function 75 |
| 1. The image copied by the CPU to `$EC000–$EFFFF` | the same RAM, the RAM card's at `$0EC000`; only the source moves |
| 2. The character ROM into card memory at `$1E0000`, through the port | the same; only the source moves (blocks 4–5) |
| 3. SEI, the zero byte at `$EBFFF` | unchanged |
| 4. The clear by DMA on channel 3: ABORT, STATUS, CONTROL, six fills | the same fills. Every channel is stopped (ABORT `$FF`, STATUS cleared), and CONTROL is written whole as `DMA_ON` with SELECT 3, so the window is channel 3's. Channel 3's module is none. ADDR's and DEST's third bytes are 0–5 and `$0E`. The code never returns, so it doesn't put CONTROL back: the /RESET pulse does |
| 5. MODE, and the /RESET pulse | unchanged. /RESET also puts back the DMA controller's CARD fields and CONTROL (`$70`, SELECT 0), WINnH and VECH (0) and AP8 (1) |
| The memory map: banks 0–5 and E, A0–A15 through, A16–A19 from the switches | unchanged; A20–A23 are 0. No Apple address reaches bank 8, `$E00000` or the flash, so AP8 and the aperture's move don't touch the mode |
| The video card's snoop of banks 0–1 and the switches | unchanged (the table above) |
| The aperture inert while APPLE is set | unchanged, at its new address |
| The clock, Ctrl-Reset, the hard reset, the Disk II, DMA idle | unchanged |

#### The video aperture moved out of RAM ####

**Decided (the owner, 2026-11-14).** The aperture leaves bank 8 for `$E00000–$E0FFFF`, so bank 8 is RAM again and
the machine has the whole 1 MB. `$E00000` starts a 1 MB region below the flash, kept for cards' memory; the
aperture's 64 KB is its first.

**Who uses bank 8 as the aperture today.** Every one reaches it by the block numbers 32–35 in a window or by the
physical addresses `$80000–$8FFFF` in DMA:

| Where | What |
|---|---|
| BIOS, `backplane/bios/video.s` | `VBLOCK = 32` through W2 for VPUTC (slot 0) |
| BIOS, `diag23.s`, `diag.inc` | `DG_APER`: bank 8 is the aperture on a revision 2 card, so not RAM; the RAM stages pass over blocks 32–35 (`MODFIRSTA`) |
| `backplane.inc` | `VIDEO_APERTURE = $80000` |
| OODOS, `oodos/sys/crt.s` | the console's text through block 32 in W1 (`VBLOCK`) |
| OODOS, `oodos/sys/video.s` | `DM_VCOPY`'s DMA through slots 2 and 3 at `$88000` and `$8C000`; the W1 map (block 33) |
| OODOS, `oodos/sys/bdosmem.s` | `OSWIN`: refuses physical destinations `$80000–$83FFF` and `$88000–$8FFFF` |
| `oodos/include/vmem.inc` | `VM_APERTURE = $80000`, `VM_SLOT_BLOCK = 33` |
| OOLIB, `oodos/lib/gxmem.s` (`GX_VMAP`, `GX_BLOCK`), and through it `gxbmp.s`, `gxtile.s` | writes `NB_WIN1` with block 33. **The library is linked into each program,** so programs built today carry block 33 in their own code |
| OOUI, `oodos/ui/uidraw.s` | through `GX_VMAP` (block 33) |
| SYSINFO, `oodos/utils/sysinfo.s` | `DM_INFO` for the card, and its line "960 KB of RAM and the video card's 64 KB aperture" |
| The emulator | `backplane.machine`'s `aperture 80000-8FFFF`; `video_card.{h,cpp}`, `machine.{h,cpp}`, `apps/emu/main.cpp`, the WebUI's `debugger/devices.ts` and `vram.ts`; `test_video_rev2`, `test_bios`, `test_bios_diag`, `test_oodos_gfx`, `test_oodos_load`, `test_oodos_utils_rom`, `test_oodos_video`, `test_app_debug.py` |
| The books and hw/ | the Technical Reference's chapters 2, 5, 6, 11, 13, 14; the Programmer's Guide's chapters 5, 7, 8; hw/oodos.md, oolib.md, ooui.md, diag.md, and [video-rev2.md](video-rev2.md) (frozen: its note comes with B7, by the owner's answer) |

**How a window reaches it (decided): wider windows, and the old bank 8 kept until software says otherwise.** The
window registers have no free bit (bit 7 ROM, bit 6 RO, bits 5–0 BLOCK), and the 6-bit block already covers the
whole 1 MB, so a new place needs more bits. The north bridge gets new registers at offsets that are free today
(`$19–$7E` are unused; `backplane.inc` and `backplane_bridge.h` name nothing there), and its ID goes up:

| Offset | Name | Access | Purpose | Reset |
|---|---|---|---|---|
| `$20`–`$23` | WIN0H–WIN3H | read/write | bits 3–0: A20–A23 for the window's block when its ROM bit is clear. The block is then 10 bits, WINnH:BLOCK, any 16 KB of the 16 MB. With the ROM bit set the flash is chosen and WINnH is ignored. Bits 7–4 0 | `$00` |
| `$24` | VECH | read/write | the same for VEC | `$00` |
| `$25` | MAP | read/write | bit 0 **AP8**: 1, the CPU card sends every memory cycle for `$080000–$08FFFF` (a window with ROM clear, WINnH 0 and BLOCK 32–35, and the DMA controller's memory side) to the aperture at `$E00000–$E0FFFF` instead, as bank 8 was; 0, bank 8 is the RAM card's. Bits 7–1 0 | `$01` |
| `$7F` | ID | read | **`$D2`**, revision 2: a north bridge with the registers above. `$D1` (today's, `HW_NB_ID` in `oohw.inc`, `BackplaneBridge::REVISION` 1) has none of them | |

- **Telling the bridges apart (review round 1).** Software writes WINnH and MAP only after reading NB_ID as `$D2`.
  On `$D1` those writes would be lost, so it falls back:
  - the BIOS and OODOS reach the aperture by block 32 as now;
  - `DM_BANK8` is refused;
  - SYSINFO's `hwname.s` names both IDs.

  NB_ID is already read as an ID byte under SYSINFO's exception (AGENTS.md). The diagnostics' stage 7 checks the
  bridge's revision through `HW_REV` (`diag22.s` 182–184, `CARDIS` with revision 1), and takes `$D2` with B7.
- **Reset and old software:** WINnH 0 and AP8 1, so block 33 in W1 and `$84000` in a DMA address still mean the
  aperture's slot 1. Today's OODOS and every program built with today's OOLIB run as they do now, on 960 KB.
- **No revision 2 video card (review round 1).** There is no aperture to keep, so the BIOS clears AP8 at boot
  (after reading NB_ID `$D2`), and bank 8 is RAM as it is on such a machine today. The diagnostics' `DG_APER`
  follows AP8: with AP8 clear their RAM stages test blocks 32–35.
- **New software reaches the aperture by its own address:** WINnH `$0E` with BLOCK 0–3 for slots 0–3, and
  `$E0xxxx` in DMA. That works whatever AP8 holds, so the new BIOS and OODOS always use it.
- **The recovered 64 KB** is there once AP8 is 0. Programs carry block 33 in their own linked OOLIB, so AP8 may be
  cleared only for a program that has said it knows. **OODOS keeps AP8 set, clears it for the running program at
  its request** (below), and sets it again at the warm boot. A machine with no revision 2 card already has AP8
  clear.
- **The alias is on the CPU card** (the north bridge, and the DMA controller beside it). So the Console card and
  the bus spy, driving the bus themselves, see the true map: bank 8 is the RAM card's to them.
- **Why wider windows.** They are what [Future: a 65C816 and 16 MB](#future-a-65c816-and-16-mb) needs anyway (its
  "the registers widen"), and RAM past 1 MB (`$100000–$DFFFFF`) would be reached the same way.

**WINnH under OODOS (review round 1).** Only OOLIB ever gives a window a WINnH other than 0, and only W1 (WIN1H
`$0E`, slot 1). Code that maps W1 with a single `STA NB_WIN1` has to find WIN1H 0. So the three ways into OODOS
save it, clear it and put it back:
- **The dispatcher,** `irq.s`'s `DISPATCH`: it saves and clears WIN1H on entry and restores it on the way out. The
  handlers it calls map W1 with one write, and they then find WIN1H 0: `TICK`, `EVALL`, `CALL61` and `CALL62`,
  `input.s`'s `USBSRV` and `L2TICK`, `sound.s`'s `DOCIRQ`, `MBOXIN` and `SMDROP`, and `video.s`'s `VBWORK`.
- **The BDOS entry,** `common.s`'s `STUB` and `STUBRET`: the same around every call, so the BDOS-side maps (`crt.s`'s
  `CRTOUT` and `CRTINIT`, `bdosmem.s`'s `CARD`, `input.s`'s `L2OPEN`, `QFLUSH`, `QTAKE`, `CTLALL`, `MRESET` and
  `AREAOUT`, `sound.s`'s `SFIELDS`, `disk2.s`'s `MAPW1` and `UNMAPW1`) find WIN1H 0.
- **The hooks,** `irq.s`'s `CALLHOOK` and `HKBACK`: a hook runs with the program's windows, WIN1H included, and
  OODOS's are put back after.
- **Others:** `common.s`'s `WARM` (the warm boot), `NMI` (Break) and `ROMAPPLE` set WIN0H–WIN3H to 0 before going
  on, and set AP8 back to 1 **only when `VIDEO_ID` reads `$92`** (review round 2). Without a revision 2 card bank 8
  is RAM, and AP8 stays clear: set, bank 8 would read `$FF`. FARCALL and the loader door move W3 only, whose WIN3H is always 0.

**What OODOS and OOLIB add (B7, [below](#what-steps-b1-b7-change)).** Each is an addition, and OOLIB's interface
version (function 80's `FT_INFO`) goes up with them. Every existing call keeps its meaning:

- **Function 83, a new operation `DM_BANK8`** (the next free number, 11; `DM_OPS` 12). Its block is +0 the
  operation, +1 and +2 filled in by OODOS: slot 1's window as WIN1H and WIN1 (`$0E`, 1), or 0 and 0 with no
  revision 2 card.
  - For the running program it clears AP8. Banks 8–B (blocks 32–47) are then its ordinary RAM, as banks 9–B are now:
    in the W1 map, reachable by DMA segments and function 83, not handed out by 53–58.
  - **It succeeds without a revision 2 card** (review round 1): AP8 is already clear there, and the block says
    there's no slot.
  - It's refused (A = `$FF`) on a north bridge `$D1`, or with a ROM BIOS older than the one that knows AP8 (function
    77, `O_ROMVER`): Break into an older ROM's monitor would write its screen into bank 8's RAM.
  - The warm boot sets AP8 again where there is a revision 2 card.
- **`DM_INFO` stays as it is,** 16 bytes (review round 1): callers' blocks (SYSINFO's `.res 16`, OOLIB's) are that
  size. The new values come back in `DM_BANK8`'s own block.
- **The W1 contract** grows: from the new interface version, OOLIB may write WIN1H (`$0E` or 0) as it writes WIN1.
  A map is then two writes, which an interrupt can split, but the dispatcher puts back both as it found them, so a
  split does no harm.
- **Function 73 (`O_PHYSADDR`)** refuses an address in W1 while W1 shows the aperture at `$E00000`, as it refuses
  the flash: its 16-bit paragraph can't hold the address.
- **`PHYSOF` and `PMAP` do the same (review round 2).** `oodos/sys/bios.s`'s `PHYSOF` (MOVE, XMOVE, DMA segments,
  function 83) and `oodos/sys/bdos.s`'s `PMAP` turn a CPU address into a physical one from the windows' blocks
  alone. While WIN1H isn't 0, a W1 address is refused (A = `$FF`) rather than taken for RAM block 1.
- **OOLIB:** `GX_INIT` calls `DM_BANK8` when the version allows, and `GX_BLOCK` writes WIN1H. A program linked with
  an older library never calls it, keeps AP8 set and runs as now. **Decided otherwise (the owner, 2026-11-17, B7's
  question 5):** `GX_INIT` is unchanged and never calls `DM_BANK8`; a program asks for the new mode itself with a new
  entry, `GX_BANK8`, after `GX_INIT`. A relink alone would otherwise change what physical `$84000` means to a program
  that loads card memory by DMA through it (OO-Amp does), with no error ([B7: as built](#b7-as-built)).
- **OOUI** (`UI_IFACE` 1) is unchanged: it maps through `GX_VMAP`.
- **SYSINFO** reads MAP to say what the RAM is, as the ROM's diagnostics do (they own the machine and need no
  exception). The read is added to AGENTS.md's exception, which is SYSINFO's alone since D3c
  ([diag.md](diag.md#plan2-questions), P2-1), with B7 (the owner's answer), together with the note in
  [video-rev2.md](video-rev2.md).

#### The DMA controller, revision 2 ####

**Decided (the owner, 2026-11-14): the register layout.** Revision 2's ID byte is `$82`. Its layout replaces
revision 1's ([DMA registers](#dma-registers-revision-1-dropped-in-b2)): one channel window, which CONTROL's SELECT points at a channel, and
the shared registers beside it. The emulator drops revision 1 when B2 lands, as the BIOS and OODOS dropped the video card's
revision 1 (BIOS 0.13, OODOS 0.9) ([the B2 migration](#the-b2-migration-revision-1-dropped)).

**The block, `$C400–$C47F`:**

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00`–`$02` | ADDR | read/write | the selected channel's address, 24 bits, low byte first. Writing sets the start value; reading returns the current address. Counts wrap within 24 bits |
| `$04`–`$05` | COUNT | read/write | bytes to move, low byte first; 0 means 65,536. Reading returns how many are left |
| `$06` | MODE | read/write | as revision 1's: XFER, DIR, DEC, HOLD, AUTO, IRQ |
| `$07` | CTRL | read/write | as revision 1's: bit 0 ENABLE, bit 1 REQ |
| `$08`–`$0A` | DEST | read/write | the destination, 24 bits, for memory to memory |
| `$0C` | CARD | read/write | bits 3–0 **MODULE**: the module ID the channel serves; `0x8`, the controller's own ID, which never requests, is **none**. Bit 4 **WIDE**: 16-bit transfers ([below](#16-bit-transfers)). Bits 7–5 0 |
| `$03`, `$0B`, `$0D`–`$0F` | | | reserved: read `$FF`, writes ignored |
| `$10` | STATUS | read; write 1 to clear | bit n: terminal count reached on channel n, 0–7, also the interrupt source |
| `$11` | ABORT | write | bit n stops channel n at once; its ADDR and COUNT keep the values they stopped at |
| `$12` | REQ | read | bit n: channel n's MODULE was requesting at the last poll (revision 1's "DRQn asserted now", STATUS bits 4–7) |
| `$13`–`$7D` | | | reserved |
| `$7E` | CONTROL | read/write | bit 0 master enable. **Bits 1–3 SELECT**: the channel the window at `$00–$0F` shows. Bits 4–6 BURST, as revision 1's (2^BURST transfers, a byte or a word, since B4). Bit 7 free (reads 0). Resets to `$70`: no burst limit, DMA off, SELECT 0 |
| `$7F` | ID | read | `$82` |

**CARD's reset values** follow the wiring `emulator/machines/backplane.machine` gave revision 1 (`sd drq=0`,
`video drq=1`, `sound drq=2`; since B2 a bare `drq` on each line, [B2: as built](#b2-as-built)), with WIDE 0:

| Channel | CARD at reset | Module |
|---|---|---|
| 0 | `$07` | mass storage |
| 1 | `$09` | video (its data port, revision 2) |
| 2 | `$0A` | sound |
| 3 | `$08` | none: memory to memory, and the BIOS's copies |
| 4–7 | `$08` | none |

The interrupt stays IRQH bit 0 (ID 8), for any channel. Priority stays fixed, channel 0 highest, then 1–7.

**SELECT is shared state (decided):** **code that sets SELECT saves CONTROL first and restores it after, interrupt
code included.** An interrupt taken between a SELECT and the channel's registers then finds the window wherever
the interrupted code left it, and leaves it there. Main code that writes CONTROL whole (`DMA_ON`) writes the SELECT
it means. STATUS, ABORT, REQ and the ID need no SELECT. The B2 migration below checks every user against the rule.

**The request poll.**
- A card wanting a transfer pulls /DREQ, the shared open-drain line.
- The controller runs a poll when /DREQ is low, the master enable is on, and an armed channel serves a card (its
  MODULE isn't none, and its direction is card to memory or memory to card). It takes the bus (RDY, then BE: the
  takeover cycle, as now) and runs one cycle with /DMA and /IACK low, /MEM high and PS `0x8`. Each card with a
  request pulls its own data line, D(ID), low, as /IACK's poll does on D0–D7 (there line ID mod 8); the backplane's weak pull-ups hold the rest
  high. With D0–D15 every ID has a line. REQ records the result, by channel.
- It then grants the armed channel of highest priority whose MODULE's line was low. A channel started by software
  (REQ, memory to memory) is served in the same order.
- **A poll that grants nothing** (a card requesting with no channel armed for it) gives the bus back. The controller
  polls again only after 64 cycles, or at once if /DREQ goes high and low again or a CTRL is written. So a stray
  request costs at most 2 cycles in 64. A card keeps /DREQ low through the poll cycle itself (below), so the poll
  makes no edge of its own that would restart one.
- **Cost (corrected in B2, the owner's answer 2, 2026-11-15):** the poll is one cycle, after the takeover's. A
  take polls only as the first bullet says: with /DREQ low, an armed channel that serves a card, and no hold-off
  running. A take with no channel armed for a card (only software-started transfers, memory to memory) never
  polls, whatever /DREQ is, so it costs what it did on revision 1. A software-started channel beside an armed card
  channel is served in the same take as the poll, never delayed by a hold-off. *(This text first had every take with /DREQ low
  poll, which the first bullet contradicts; the owner chose the first bullet.)*

| Transfer | Revision 1 | Revision 2 |
|---|---|---|
| single (a byte a request) | 2 cycles a byte: the takeover, the byte | 3: the takeover, the poll, the byte |
| demand, block | 1 + n for n bytes, per take | demand: 2 + n, every take polls (only a card's request starts one). Block: 2 + n for the take a card's request starts; 1 + n for a block REQ starts or one under way, which skips the poll unless another armed card channel waits with /DREQ low and no hold-off running |
| memory to memory | 1 + 2n | 1 + 2n; 2 + 2n only in a take that polls because a card's channel is armed beside it |

With WIDE (since B4, [below](#16-bit-transfers)), n counts transfers, not bytes. A transfer is a word where the rules
allow, so an aligned run of b bytes from an even address is b/2 transfers: a card's demand transfer of b bytes is
2 + b/2 cycles, and memory to memory is 1 + 2t for t transfers, 2 cycles for 2 bytes.

**The sound card's standing request (decided, the owner's answer 1, 2026-11-15).** At DRQSEL 0, its reset value and
OODOS's idle value, and at 2, the sound card asks for DMA all the time (revision 1 sound always does), so with it
fitted /DREQ is almost never high. Accepted as it is: memory-to-memory costs don't move; another card's transfer
pays its poll at each take; a card that becomes ready can wait up to 64 cycles for its grant behind a hold-off; and
while a card's channel is armed with nothing to grant, the stray polls cost 2 cycles in 64. DRQSEL 0 stays the idle
value. With the master enable off (Apple II mode) it costs nothing.

**The grant cycle.**
- The controller drives:
  - the memory address on A0–A23, with /MEM low;
  - RWB for the memory's side (card to memory is a write);
  - the channel's MODULE on PS0–PS3, and /DMA low with /IACK high;
  - TC with the last transfer.
- The memory answers its address, and the card named by PS puts or takes its data register on D0–D7 (a byte cycle;
  memory steers), or with WIDE on D0–D15 (a word cycle, at an even address), fly-by, one cycle a transfer, as now.
- A card's logic: with PS its own and /DMA and /MEM high, a register chosen by A0–A6; with PS its own and /DMA low,
  /IACK high, its data register.

**Demand mode, and when the card lets go (decided, review round 1).** /DREQ is shared, so it can't say which card
let go.
- **The rule applies only in a grant cycle, /DMA low with /IACK high:** only the card named on PS0–PS3 may pull
  /DREQ then, and it pulls it if it wants another transfer after this one.
- The controller samples /DREQ at the end of each grant cycle: low, the demand transfer goes on with no new poll;
  high, it ends.
- In a poll cycle (/DMA and /IACK both low), every card with a request keeps /DREQ low.
- Other cards' requests come back when /DMA goes high.
- A card needs only its PS match, /DMA and /IACK for this, which it decodes anyway.

**TC** is asserted with the last transfer's grant, as now, and the card named by PS sees it.

#### 16-bit transfers ####

**Proposed, emulated (B4).** CARD's WIDE bit, 0 at reset ([as built](#b4-as-built)):
- **A word a transfer.** With WIDE set, each grant is a word cycle: the memory answers the word at the even address,
  both lanes, and the card puts or takes 16 bits of its data register. COUNT stays in bytes and goes down by 2;
  ADDR steps up by 2, and so does DEST on a memory-to-memory copy. A word costs what a byte does: one cycle fly-by,
  two memory to memory.
- **Only counting up (decided, review round 1).** With DEC or HOLD set the channel moves bytes, WIDE or not. Words
  counting down would pair the wrong bytes, and a held word would fill alternate bytes.
- **Alignment.** A transfer that starts at an odd address, or a last odd byte, is moved as a byte cycle, so any
  count and address work. TC comes with the transfer that empties COUNT, a byte or a word.
- **Memory to memory** reads a word into a 16-bit latch and writes it: 2 cycles for 2 bytes. If the source and the
  destination differ in parity, it moves bytes, WIDE or not.
- **BURST counts transfers** (the owner's answer 1, B4): 2^BURST transfers, a byte or a word, before the CPU gets a
  cycle. A word grant costs what a byte grant does, so the CPU's longest wait is the same number of cycles.
- **/WAIT is one line,** so a word grant takes one stretch: 1 + the longer of the memory's and the card's /WAIT, once
  a word.
- **One chip select a word.** The memory or card that decodes the even address answers both lanes. An unmapped word
  floats `$FFFF` (the backplane's pull-ups).
- **A card that takes only bytes** must not have WIDE set on its channel: each card's documentation says its data
  register's width. Given WIDE anyway, the bus does what it would: the card's register moves once a grant, on
  D0–D7; D8–D15 float `$FF` to memory, and the odd byte from memory is lost.

**Each card's data register** (the owner's answers 2–5, B4). The video entries are an addition beside the frozen
[video-rev2.md](video-rev2.md), which says only that PDATA is a byte; its note in B7 names them.

| Card, register | Width | In a word grant |
|---|---|---|
| Memory: the RAM card, ROM A and ROM B | words | the even byte on D0–D7, the odd on D8–D15; a byte cycle at an odd address is steered to D0–D7 |
| The video card's aperture (`$E00000`, and bank 8 redirected there) | words | as memory: both bytes of the word in card memory |
| The video card's port, PDATA (revision 2) | words | two port accesses in order: the even byte at PADDR, a PSTEP step, then the odd byte, another step. Every PSTEP and PCTL meaning holds: a column at PSTEP 160, a step-down copy, the wrap. PSTEP 0 keeps one byte: a word to the card leaves its odd byte, a word from it reads that byte twice. Against PCTL's direction a word reads `$FFFF`, a write is dropped and PADDR doesn't step. A word request means room for two bytes |
| The SD card's DATA | bytes | WIDE stays 0 on channel 0. A word DATA is a later step of its own, with the RP2350 card's word port designed |
| The sound card's RAMDATA, PCMDATA and DOCDATA | bytes | WIDE stays 0 on channel 2, and on any channel naming module A |
| The USB card's event FIFO | bytes | WIDE stays 0 on any channel naming module B |
| The serial card's stream registers (the TL16C552A's) | bytes | WIDE stays 0 on any channel naming module 6 |

#### The serial card's stream select ####

**Decided (the owner's answer, 2026-11-14; B2).** The serial card has two DMA sources and one module ID. The sources
are DMASEL1 and DMASEL3, `$C320–$C321`, on DRQ1 and DRQ3 today. The card gets **DMASTREAM at `$C324`**. Offset `$24`
is free: the card's registers are `$20–$23`, and `$24–$3F` are unused.

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$24` | DMASTREAM | read/write | bit 0: which source the card's /DREQ and its fly-by data register follow: 0 DMASEL1's, 1 DMASEL3's. Bits 7–1 0. Reset 0 |

- DMASEL1 and DMASEL3 keep their values and meaning (0 none, 1–5 the sources), now "stream 0" and "stream 1".
- A channel serves the card by MODULE 6. Neither channel 1 (video) nor 3 (none) names it at reset, so software sets
  CARD on the channel it uses.
- One stream runs at a time.
- Nothing uses the card's DMA today: the emulator has a 6551 standing in for the TL16C552A, mirrored over the
  block, so `$C324` is the 6551's mirror there until the card is modelled.

#### The B2 migration: revision 1 dropped ####

**Decided (the owner, 2026-11-14).** Code written for revision 1 doesn't run on revision 2: the channels' registers
and the shared ones have moved. B2 moves the BIOS and OODOS to the new layout all at once, and the emulator drops
revision 1 with it.

**Why the rule allows it.** AGENTS.md's interfaces that only grow are the BDOS functions, OOLIB's, FARCALL and
common memory's table, the BIOS's jump table and OOUI's. The DMA controller's registers are none of these:
- the DMA channels are OODOS's, and a program doesn't touch them (AGENTS.md: "Don't take over what OODOS owns");
- programs move memory by function 83 and the BIOS's MOVE, whose arguments don't change;
- SYSINFO's exception (AGENTS.md) doesn't include the DMA controller: SYSINFO reads only its ID byte
  (`hwprobe.s`, which reads every card's; `hwname.s` names it); the ROM's diagnostics drive it directly, owning the machine while they run.

So nothing outside the ROM and OODOS.SYS sees the move. The ROM's diagnostics (D3b's stage 8) drive the channels
directly, but they are the ROM's own code and move with it.

**`backplane.inc` for B2.** Revision 1's names go, so any access the migration misses fails to assemble:
- **Removed:** `DMA0_*`–`DMA3_*` (ADDR, COUNT, MODE, CTRL, DEST), and `DMA_ID_V1`.
- **The window:** `DMA_ADDR` `$C400`, `DMA_COUNT` `$C404`, `DMA_MODE` `$C406`, `DMA_CTRL` `$C407`, `DMA_DEST`
  `$C408`, `DMA_CARD` `$C40C`.
- **Shared:** `DMA_STATUS` `$C410`, `DMA_ABORT` `$C411`, `DMA_REQ` `$C412`, `DMA_CONTROL` `$C47E`, `DMA_ID` `$C47F`,
  `DMA_ID_V2` `$82`.
- **Values:**
  - `DMA_SEL` `$0E` (SELECT's mask) and `DMA_SEL0`–`DMA_SEL7` (n × 2);
  - `DMA_CH0`–`DMA_CH7` (bit n) and `DMA_ALL` `$FF`;
  - `DMA_NONE` `$08` and `DMA_WIDE` `$10` (CARD).
- **Kept:** `DMA_ON` `$71` (master, no burst limit, SELECT 0), `DMA_MASTER`, the MODE and CTRL values.

**The ID checks (decided, the owner, 2026-11-14)** take `$82` exactly (`DMA_ID_V2`), not the type nibble that
answer 4 first had: a revision 1 controller (`$81`) has the other layout, so it reads as no controller.
- The BIOS: `bios.s:363`, `sd.s:209`, `video.s:139`, `apple.s:56` and `diag.inc:225`.
- OODOS: `cold.s:390` (`CARD DMA_ID, DMA_ID_V1, $FF`), `bdos.s:297` (function 75's; the line numbers are at
  c4ad400, before B2), `crt.s:301`, `irq.s:611`,
  `sound.s:123`, and `video.s:168`, `440`, `564` and `1125`.
- The diagnostics' revision checks through `HW_REV` (D3b), which take DMA revision 1 today and take 2: `diag22.s`'s
  `HAVEDMA` (105–108) and its users (279, 1624, 1847); stage 8's `CARDIS` (530–532); `diag21.s` 267 and 382; and
  D1's title (551, "ID $81").
- SYSINFO: `backplane/oohw/hwname.s`'s list of known IDs takes `$82`.
- Function 75's text changes: "doesn't read `$81`" becomes "doesn't read `$82`".

**Every access, by grep** (`DMA[0-3]_`, `DMA_CONTROL`, `DMA_STATUS`, `DMA_ABORT` in `backplane/` and `oodos/`),
and what each routine does under the SELECT rule:

| File, routine | Today | On revision 2 |
|---|---|---|
| `backplane/bios/bios.s` QUIET (363–372) | ABORT and STATUS `DMA_ALL`; `STZ DMA0_MODE + $10 × n`, channels 0–3 | ABORT `$FF`, STATUS `$FF`; MODE 0 on channels 0–7 by SELECT, CONTROL saved first and put back (Break leaves CONTROL as it was) |
| `backplane/bios/diag.inc` (225–234, 310) | the diagnostics' QUIET, as the BIOS's; PUTBACK writes CONTROL `$70` | the same as QUIET; `$70` is SELECT 0, the reset value |
| `backplane/bios/diag22.s` (D3b's stage 8: `STAGE8`, `D1`, `D5`, `DMA3`, `DMAWAIT`, `DMA1GO`; `NB3`; `S3`), `diag21.s` (`SD3`, `SD4`, `SDSTUCK`, `DMA0AT`) | each channel's registers by `DMA0_`–`DMA3_`, with interrupts off and every channel the core's | each routine selects its channel, CONTROL saved and put back as the rule says; the tests redone for revision 2 ([below](#the-b2-migration-revision-1-dropped)) |
| `backplane/bios/sd.s` LOADRUN (219–232) | channel 0's registers, then CONTROL `DMA_ON`, never put back | CONTROL saved, `DMA_ON` with SELECT 0, the registers, and CONTROL put back when the load ends |
| `backplane/bios/video.s` VINIT (77) | CONTROL written at start-up | unchanged (SELECT 0) |
| `backplane/bios/video.s` PORTJOB (334–383) | ABORT, channel 1's registers, then CONTROL saved (354) and put back (383) | the save moved first, then SELECT 1, then the registers |
| `backplane/bios/video.s` DMA3RUN (438–457) | exported, but nothing calls it (review round 2): no `JSR DMA3RUN` in the BIOS | dropped in B2 |
| `backplane/bios/demo.s` PORTDMA (577–612) | ABORT, channel 1's registers, CONTROL saved (591) and put back (609) | the save first, then SELECT 1 |
| `backplane/bios/apple.s` (100–135) | ABORT and STATUS `DMA_ALL`, CONTROL `DMA_ON`, channel 3's six fills | ABORT and STATUS `$FF`, CONTROL `DMA_ON` with SELECT 3; no restore (it never returns, and /RESET puts CONTROL back) |
| `oodos/sys/bios.s` SDIO (906–956) | channel 0's registers (911–923), then CONTROL saved (924), restored at SDEND (956); STATUS read (950) | the save first, SELECT 0, the registers; STATUS bit 0 as now |
| `oodos/sys/bios.s` DMAGO, DMA3W (1794–1867) | CONTROL saved (1794) before channel 3's registers, restored (1847); DMA3W polls `DMA3_CTRL` | SELECT 3 after the save; DMA3W runs inside the saved stretch and polls `DMA_CTRL` |
| `oodos/sys/sound.s` C2STOP (252–255) | ABORT channel 2, `STZ DMA2_CTRL`, `STZ DMA2_MODE`, STATUS | save, SELECT 2, CTRL and MODE 0, restore; ABORT and STATUS need none |
| `oodos/sys/sound.s` PIECE (527–561) | channel 2's next piece, **in the interrupt** (the card's or channel 2's terminal count), with no save of CONTROL | **must** save CONTROL, SELECT 2, program, restore: the rule's interrupt case |
| `oodos/sys/sound.s` DMALOAD (653–725) | `TSB` master, channel 2's pieces, polls `DMA2_CTRL`, interrupts on between pieces | save, SELECT 2 and master, the pieces, restore; an interrupt between pieces restores what it found |
| `oodos/sys/sound.s` (294–297, 1152) | STATUS's channel 2 bit; `TSB` master | unchanged (no SELECT) |
| `oodos/sys/video.s` (767–793) | SEI, CONTROL saved (769), channel 1's registers, restored (791) | SELECT 1 after the save |
| `oodos/sys/video.s` PORTGO (838–856) | channel 1's MODE and CTRL, its wait, ABORT and STATUS; called inside the stretches that saved CONTROL | unchanged but for the names: its callers have SELECT 1 |
| `oodos/sys/video.s` VBWORK (987–1103) | the frame handler: CONTROL saved (991) and put back (1101) | SELECT 1 after the save: it already keeps the rule |
| `oodos/sys/video.s` VBQUIET (1125–1129) | ABORT channel 1 | unchanged |
| `oodos/sys/irq.s` DEAFEN (611–633) | each channel's MODE by `DMA0_MODE,X`, X = `$30` down to 0, from the dispatcher and the warm boot | save CONTROL, SELECT 7 down to 0, MODE's IRQ bit off, restore; DMAIRQ grows to 8 bits for STATUS |
| `oodos/sys/disk2.s` (286–295), `common.s` NMI (378) and ROMDIAG (434–438) | CONTROL saved whole into D2DMAS, master off for a write, put back (by Break, and by function 75's way into the ROM's diagnostics) | unchanged: CONTROL is saved and put back whole, SELECT with it |
| `backplane/bios/bios.s`, `apple.s`, `diag.inc`, OODOS | `DMA_ALL` (`$0F`) stops channels 0–3 | `DMA_ALL` `$FF` stops all eight wherever 0–3 are stopped today (review round 1) |

**The emulator** drops revision 1's DMA controller when B2 lands. The machine file's `drq=` becomes the card's
module *(as built, a departure from this wording: a bare `drq` flag, the module from the card's I/O block, and
`drq=N` refused; [B2: as built](#b2-as-built))*, and the tests that program the controller are redone for revision 2: `test_dma`, `test_bios`,
`test_bios_sd`, `test_sd`, `test_console_card`, `test_oodos_sys`, `test_oodos_disk2`, `test_oodos_sound`,
`test_oodos_video`, `test_sound`, `test_sound_mp3`, `test_sound_pcm`, `test_sound_doc`, `test_usb`, `test_machine` and
`test_video_rev2` (review round 2). The WebUI's DMA panel (`emulator/apps/web/ui/src/debugger/devices.ts`) shows the
new layout. *(There is no such panel: the owner's answer 10 leaves one for later, reading through `peek_channel`.)*

**The diagnostics (review round 1).** D3b's DMA tests are redone in B2, with revision 2's bounds:
- **Stage 8** ([diag.md](diag.md)): D1 (the ID, now `$82`; each register read back through SELECT 0–7; CONTROL's
  burst and SELECT bits), D2 and D3 (channel 3's fill and copies), D4 (the fill timed by VIA timer 2) and D5
  (STATUS and ABORT at `$10–$11`). D4 is a memory-to-memory fill, which never polls (the owner's answer 2), so it
  stays 2 cycles a byte, 4,096 for 2 KB, with a sound card's standing request on /DREQ or not. *(Corrected in B2:
  this sentence had "with /DREQ high", which no machine with a sound card has.)*
- **The channel tests in the cards' stages:** V7 (channel 1, the port), S3 (channel 2), SD3 and SD4 (channel 0).
  SD4's timed read gains one poll cycle per take.
- `test_bios_diag` holds each of these to revision 2's numbers.

#### One backplane: the slots and the reserved pins ####

**Decided (the owner, 2026-11-14).**
- **RAM is one card:** 1 MB at `$000000–$0FFFFF`, selected by address. It has no module ID. "RAM A–D" survive
  only as names for its four 256 KB quarters, as the 62-pin machine had four cards.
- **The slots: about 14–15,** one for each card that can exist, plus the CPU card. The exact count is the owner's
  board design.
  - Cards with an ID: at most 13 (IDs `0x0`–`0x7`, `0x9`–`0xC` and `0xE`). IDs `0x8` (DMA) and `0xD` (the north
    bridge) are on the CPU card; `0xF` is the idle bus's, never a card's (B6, review round 2).
  - Cards without one: the RAM card, and the cartridge (ROM B), each in a slot.
  - So 15 slots hold the CPU card, the RAM card, the cartridge and twelve cards with IDs: today's seven and five
    more.
- **Today's cards and their IDs:**

  | ID | Card | Slot |
  |---|---|---|
  | `0x6` | serial: COM1/COM2, LPT, the VIA | a slot |
  | `0x7` | mass storage (SD) | a slot |
  | `0x8` | the DMA controller | on the CPU card |
  | `0x9` | video | a slot |
  | `0xA` | sound | a slot |
  | `0xB` | USB, game port and speaker | a slot |
  | `0xC` | Disk II | a slot |
  | `0xD` | the north bridge | on the CPU card |
  | `0xE` | Console (the front panel) | a slot |
  | — | the RAM card (by address) | a slot |
  | — | the cartridge, ROM B (by address) | a slot |

  **Free:** `0x0`–`0x5` (once RAM A–D, ROM A and ROM B), six IDs for new cards. `0xF` is reserved: it is the idle
  bus's ID, the value PS0–PS3's pull-ups give a cycle no master drives (B6, review round 2).
- **No fifth ID bit (decided, the owner, 2026-11-14: "just drop the 5th bit").** There is no PS4: the module IDs
  stay 16, and cards decode PS0–PS3 only. 32 IDs would need a new set of cards that decode a fifth line; that is
  recorded, not planned. BR0–BR2 (below) are three spare pins, not a fifth bit. The bus spy is a bus
  master with no registers (it reaches the Console card over SPI), so it takes a slot but no ID.
- **The bus is passive, at 7.16 MHz.** Trace length, loading and termination are left to the owner's board design.
- **Reserved pins for a bridged bus.** Three connector pins, **BR0–BR2**, are reserved for a future bridged bus
  with its own ID space (a bridge card that answers for a second set of IDs behind it). They are **unassigned
  until then**: the backplane routes them to every slot, and no card connects to them.
- **ROM A is on the CPU card (decided, the owner, 2026-11-14).**
  - The reset fetch never leaves the CPU card.
  - The BIOS, the flash update and the ROM diagnostics work with the RAM card out or failed. The diagnostics' first
    stages run with no RAM ([diag.md](diag.md)), so they can name a bad RAM card.
  - The RAM card stays plain 16-bit SRAM.
  - Software can't tell where ROM A sits: it answers at `$F00000–$F7FFFF` by address.
- **The CPU card carries** the CPU, the north bridge, the clock, the DS3231, the DMA controller and ROM A.
- **ROM B, the cartridge, stays a card in a slot,** answering at `$F80000–$FFFFFF` by address, with no ID.

#### Wait states and the read budget ####

**Replaced (the owner, 2026-11-14): no expansion chassis.** B0 had an expansion chassis on a cable: the link's
buffers, /SEL and /CDRV, seven timing rules, and a convention that memory and bus masters stay in the main chassis.
They are all dropped. Memory is one card, selected by address, and DMA is on the CPU card, so every card fits
the one backplane; the module IDs (16), not the slots, are the limit
([one backplane](#one-backplane-the-slots-and-the-reserved-pins)). What stays is /WAIT, for slow cards.

**/WAIT (decided, the owner, 2026-11-14).** **/WAIT** is a backplane line of its own, open drain, pulled up on the
backplane. On the new bus it is how any card asks for wait states.
- A slow card (the RP2350 cards, the serial card) pulls /WAIT, not RDY, for as many cycles as it needs.
- **The north bridge turns /WAIT into RDY for the CPU.**
- **The DMA controller obeys /WAIT during its grants** and stretches the grant by the same cycles. It holds RDY low
  itself through a grant, so RDY alone couldn't tell it.
- **Through a stretched cycle** the card keeps driving: its data drivers stay enabled from the first PHI2 rise to
  the final PHI2 fall.
- RDY stays for the DMA controller's and the Console card's own use: stopping the CPU for DMA, and halting and
  stepping it.

**The read budget at 7.16 MHz: an estimate.** It uses B0's assumed delays: tADS 30 ns, tDSR 10 ns; the north bridge
10 ns; each transceiver 5 ns; a card's decode 10 ns; a memory card's lane steering 5 ns.

| Path | Estimate |
|---|---|
| Memory read, 55 ns part | data at the CPU about 10 ns before it is needed; a 70 ns part needs a wait state |
| Register read (a card's CPLD) | about 45 ns to spare |

- **At 7.16 MHz (decided, review round 1): flash and RAM are 55 ns parts.** With them, memory needs no wait states.
  A 70 ns part would need one: the "55–70 ns ... with margin" of [CPU and clocks](#cpu-and-clocks) predates the
  card decode and the lane steering.
- The backplane is a passive bus at 7.16 MHz. Its trace length, loading and termination are the owner's board
  design ([one backplane](#one-backplane-the-slots-and-the-reserved-pins)), and the estimate's margins are to be
  checked against it.
- **A 14.3 MHz turbo** is no longer ruled out by a chassis. What limits it now:
  - the W65C02S's own 70 ns minimum cycle ([CPU and clocks](#cpu-and-clocks));
  - about 30 ns for a read, which the north bridge, a transceiver each way, a card's decode and the lane steering
    already fill: memory would need /WAIT cycles, or 10–15 ns SRAM near the CPU;
  - the passive backplane: about fifteen slots of trace and loading at twice the speed.

#### What steps B1-B7 change ####

**Proposed.** The plan as the owner gave it, with B7 added for the aperture's move. Each step starts after D3b
merges ([decisions](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)).

| Step | The emulator | The BIOS | OODOS, OOLIB | The books | Interfaces |
|---|---|---|---|---|---|
| **B1** memory by address | 24-bit physical addresses (`decoder.h`'s `PHYSICAL_SPACE` and `bus_address`, `space << 20` today); the RAM lines at `000000–0FFFFF`; the flash in the memory space at `F00000` and `F80000`; the north bridge's ROM bit to `$F00000 + b × $4000`; the aperture line at `E00000–E0FFFF`, with bank 8 sent to it **only while a revision 2 video card is fitted**, as the aperture line is inert without one today (review round 1), so a machine with no such card keeps bank 8 as RAM; the bus trace's address column wider; `backplane.machine` | nothing | nothing | the Technical Reference's chapters 2 and 14 (the map), the emulator notes | none: no software-visible change |
| **B2** DMA revision 2 | revision 2's layout, ID `$82`, CARD with its reset values, ADDR and DEST 24 bits; SELECT 0–3 live, 4–7 reading `$FF` until B3; cards request by module (the poll, the grant, the release rule), the machine file's `drq=` becoming the card's module (as built: a bare `drq` flag, [as built](#b2-as-built)); revision 1 dropped; the tests above redone; the serial card's DMASTREAM when the card is modelled; the controller stretching a grant while /WAIT is low; the emulator's wait-state model (`decoder.h`: a mapping's `wait` and the bridge's `wait_states`, RDY wait states today) becomes /WAIT, seen by the CPU's cycles and by a DMA grant alike | [the migration](#the-b2-migration-revision-1-dropped): QUIET, diag.inc's QUIET, LOADRUN, PORTJOB, PORTDMA, APPLE, DMA3RUN dropped; the ID checks; D3b's stage 8 and its card stages, with revision 2's bounds | the migration: SDIO, DMAGO, C2STOP, PIECE, DMALOAD, the port copies, DEAFEN; the ID checks (function 75's included); SYSINFO's `hwname.s` | the Technical Reference's chapters 1, 5 (DMA), 9 (DMASTREAM), 10 and 13, and `docs/index/registers.json`, which quote revision 1's names (review round 2); function 75's text; [DMA registers](#dma-registers-revision-1-dropped-in-b2) here | none to the BDOS, OOLIB or the BIOS's jump tables; the controller's registers change (OODOS's alone); the serial card's grow |
| **B3** eight channels | channels 4–7 live behind SELECT 4–7, their bits in STATUS, ABORT and REQ ([as built](#b3-as-built)) | nothing more: B2's loops already cover 0–7 | nothing more; channels 4–7 are free for later drivers | chapter 5; chapter 15's D1 count, the Technical Reference's contents | none |
| **B4** 16-bit transfers | WIDE; word cycles on the memory and card sides; byte cycles on D0–D7 with memory steering; the cards whose data registers take words say so (the video card's 16-bit memory first; SD and sound each by their own design) ([as built](#b4-as-built)) | may set WIDE for the SD load and the screen's port copies | may set WIDE where a card takes words; no interface change | chapter 5, and each card's chapter | none |
| **B5** the software and the books | `--apple-rom` and `--apple-chars` to ROM A blocks 0–5; the flash written, two chips, each with its command sequences and status (it ignores writes today); the WebUI's IIe ROM row writes ROM A; the Apple tests' fixtures placed there | ROM A's layout; `$C806` from ROM A, the first good image; **the dispatching entry**, designed here (foreseen by LL5), after `$C815`, with two functions: **the flash update** (the IIe images and character sets from the SD card to blocks 0–5) **and Apple II mode with A the image**; the build splits the image into even and odd files | **function 75 gains A = 3, X = the image** (A = 2 is the DIAG launcher's); `F_ROM`'s IIe check moves from the cartridge to ROM A's images (review round 1); the CCP's APPLE2 takes an image (the row said APPLE2.COM, which there isn't: [B5: as built](#b5-as-built)) | chapters 2, 3 and 13; the User's Guide's APPLE2; function 75's entry; [Boot](#boot) and [Apple II Mode](#apple-ii-mode-1) here | the ROM BIOS's new entry, the dispatching entry, with its two functions; function 75 grows (A = 3), with the OODOS interface version after D3c's 10 |
| **B6** the pin-out | none (it models signals, not pins) | none | none | [modules.md](modules.md)'s pin-out, with /WAIT among the new lines (every card's wait states on it, RDY the controller's and the Console card's), BR0–BR2 reserved, about 15 slots, and its ID table (IDs `0x0`–`0x5` no longer RAM and ROM; `hwname.s`'s names for them with it), the Technical Reference's chapter 1 | none |
| **B7** (new) the full 1 MB | WINnH, VECH, MAP and NB_ID `$D2` in the north bridge; AP8 as a register; the WebUI's bank 8 breakpoint check follows AP8, not `VIDEO_ID` (B1's follow-up, [as built](#b1-as-built)) | NB_ID checked; the diagnostics' stage 7 revision check (`diag22.s` 182–184) takes `$D2`; VPUTC through WIN2H `$0E`; AP8 cleared at boot with no revision 2 video card; Break sets WINnH 0, and AP8 only when `VIDEO_ID` is `$92`; the diagnostics' `DG_APER` from AP8, all 64 blocks tested with it clear, the aperture at `$E00000` | OODOS's console, `DM_VCOPY` and OSWIN by the new address; WIN1H saved, cleared and restored by `DISPATCH`, `STUB`/`STUBRET` and `CALLHOOK`/`HKBACK`, set to 0 by `WARM`, `NMI` and `ROMAPPLE`, which set AP8 only when `VIDEO_ID` is `$92` (review round 2); `bios.s`'s `PHYSOF` and `bdos.s`'s `PMAP` refuse a W1 address while WIN1H isn't 0 (review round 2); **`DM_BANK8`**; function 73's refusal; OOLIB's `GX_INIT` and `GX_BLOCK`; SYSINFO's RAM line and `hwname.s`'s `$D2`; `hwname.s`'s names for IDs 0–5 (moved from B6: [B6: as built](#b6-as-built)) | chapters 2, 6, 11; the Programmer's Guide's chapters 5, 7, 8; hw/oodos.md, oolib.md; the note in video-rev2.md (its DRQ 1 and /DACK lines too, revision 1's wiring: B6); diag.md's size of hwname.s; AGENTS.md's exception (MAP) | `DM_BANK8` (function 83, operation 11); the W1 contract's WIN1H; OOLIB's version raised; the north bridge's registers grow |

No step changes the BIOS's jump table in OODOS (`oodos/sys/bios.s`), the BDOS's existing calls, `DM_INFO`'s
record, FARCALL or OOUI's interface.

#### B1: as built ####

**Proposed, emulated (2026-11-14).** Step B1 in the emulator, with the owner's answers to its five items and two
questions (2026-11-14, "recommendations as listed"). No software-visible change: no BIOS, OODOS, OOLIB or GW-BASIC
source changed, and the suites pass with only the thirteen expectations below changed.

- **The decoder** (`decoder.h`): `PHYSICAL_SPACE` is 16 MB, A0–A23. A bus address is the space in bit 24 above a
  24-bit offset, and there are two spaces, Memory and Io. `Space::Flash` is now only a name for memory
  `$F00000–$FFFFFF`: `bus_address(Space::Flash, o)` is `$F00000 + o`. The registers that stay 20 bits use
  `ADDRESS_20`: the DMA controller's revision 1 addresses, and the Console card's PADDR, SWR and BPADDR.
- **The north bridge** (`backplane_bridge.h`):
  - A window's ROM block *b* is `$F00000 + b × $4000`.
  - `memory_select()` gives PS0–PS3 on a memory cycle: D for the CPU, 8 for the DMA controller's memory-to-memory
    cycles. A fly-by shows the card's ID.
  - `redirect()` sends bank 8 to the aperture at `$E00000`, and is applied both to the CPU's windows and to every DMA
    cycle. It is AP8 fixed at build time: on while a revision 2 video card is fitted, off otherwise. MAP and AP8 as a
    register are B7's.
  - A bus with no north bridge (the Apple-1, Eater) still shows each mapping's `module=`.
- **The machine file** (`backplane.machine`):
  - Addresses are up to six digits: `aperture E00000-E0FFFF`. The RAM and flash lines keep their spelling (the
    tests edit the file by its lines). The flash lines stay `space=flash` offsets, which are memory `$F00000` up.
  - An `aperture 80000-8FFFF` line, the old spelling, means the same on the backplane. The aperture is accepted
    only there or at `E00000-E0FFFF`; any other place is an error (review V2). The redirect adds the aperture's base
    to the offset in bank 8.
  - The memory lines keep `module=0`–`5` only as the ID whose WAITn bits slow them, and only in B1. B2's /WAIT
    replaces it. *(Done in B2: the RAM, flash and aperture lines carry no `module=`.)*
  - The I/O hole's IDs 0–5 and F have no line: their blocks read `$FF`, and no ID byte.
- **Physical peeks.** `Decoder::peek_physical()` and `write_physical()` give the CPU card's view (the redirect
  applied), which is what `CcpRig`'s `row()` and `attr()` at `0x82000` use:

  | Machine | `0x08xxxx` | `0xE0xxxx` |
  |---|---|---|
  | revision 2 card, native mode | the aperture's slot *n* (bits 14–15) | the same byte |
  | revision 2 card, Apple II mode | `$FF` (the aperture ignores cycles) | `$FF` |
  | revision 1 card, or none | RAM C's bank 8 | `$FF`: nothing answers |

  `peek_bus()` and `write_bus()` are the bus with no redirection. The Console card's EXAM and DEP use them, so bank
  8 is the RAM card's to the panel (the owner's answer).
- **The Console card** compares BPADDR with A0–A23. A20–A23 must be 0, so a flash or aperture cycle never matches,
  even when its low 20 bits do (the owner's answer). LADDR and the lights show A0–A19.
- **The bus trace** stays at version 1. Its address field is at least five hex digits, so a 24-bit address shows six.
  `tools/trace_diff.py` takes five or six digits and compares addresses as numbers
  ([BUS_TRACE.md](../emulator/docs/BUS_TRACE.md)). The WebUI's trace rows carry A0–A23.
- **The thirteen expectations changed, as the owner approved:**
  - `test_decoder`'s two range checks (past A23, past the I/O space) and its flash address (`0xF00010`);
  - `test_machine`'s refused range, now `ram 0-1000000` (seven digits, past A23): six-digit addresses are valid;
  - `test_backplane`'s ROM B address (`0xF80000`) and its BIOS fetch (`0xF7C822`);
  - the module now shown as D in `test_backplane` (five checks), in `test_session`'s trace rows and in
    `wasm-port.test.ts`.
- **The WebUI's breakpoints (review V1, the owner, 2026-11-15).** BPADDR keeps A0–A19 and matches only with
  A20–A23 0, so the debugger refuses, saying why:
  - a CPU address or label that resolves above `$FFFFF`. A window on the flash gives `$F0xxxx`, and one on the
    aperture (blocks 32–35 with a revision 2 card) gives `$E0xxxx`. Masked to 20 bits, either would have armed an
    unrelated RAM address;
  - a bank 8 address, `$80000–$8FFFF`, typed while `VIDEO_ID` reads `$92`. The CPU card sends the CPU's and DMA's
    bank 8 to the aperture, so no cycle there reaches the bus to match. Without a revision 2 card bank 8 is RAM and
    such a breakpoint is kept.

  A breakpoint saved by an older page above `$FFFFF` isn't restored; the pane names it rather than drop it
  silently.

  The final check (VV1, VV2, the owner, 2026-11-15):
  - **Bank 8 breakpoints from before B1.** A page from before B1 could save one in bank 8. On connecting, the pane
    reads `VIDEO_ID` (`recheck()`), and while it reads `$92` such a breakpoint joins the ones not restored.
  - **The notice has its own line** (`bp-notice`), so a later error doesn't overwrite it.
  - **The entries not restored stay saved** through every later save, and their ids aren't reused. A machine they
    can fire on (bank 8 without a revision 2 card) restores them again.
  - The cases: `breakpoints.test.ts` (a bank 8 breakpoint on revision 2 and on revision 1, kept through a save) and
    `pane.test.ts` (the notice beside a later error, the bank 8 one on connecting). All four were seen failing with
    the round 1 code of `breakpoints.ts`, `breakpoints-view.ts` and `pane.ts` put back.

  **Left for B7:** with AP8 a register, the bank 8 check is to follow AP8 rather than `VIDEO_ID`, since software
  may clear it.
- **New cases:**
  - `test_backplane`: the ROM bit's `$F00000` address, an I/O cycle never reaching memory, IDs 0–5 and F empty;
  - `test_video_rev2`: bank 8 and `$E00000` with a revision 2 card, bank 8 as RAM without one, DMA's PS0–PS3 and its
    bank-8 cycles, EXAM and DEP on the RAM;
  - `test_console_card`: the breakpoint needing A20–A23 0;
  - `test_trace_diff.py`: six-digit addresses.
  - Review round 1:
    - `breakpoints.test.ts`: the three refusals and reports above;
    - `test_backplane`: the redirect adding its base;
    - `test_video_rev2`: the aperture line only at `E00000` or the old spelling, and the old spelling's case
      extended (the RAM at bank 8 on the bus, the card at `$E04123`, a block 33 read on `$E04123`);
    - `debugger.spec.ts`: a typed `84123` refused on the real machine.
- **Must-fail and regression guards (review V6).** Each new case was seen failing once against a deliberate break,
  except two: `an_io_cycle_never_reaches_memory_whatever_the_address_lines_hold` and
  `ids_0_to_5_and_f_are_empty_slots_on_the_machine`. Both pass on the code before B1 too, because the I/O hole was
  already decoded ahead of memory. They guard the /MEM rule against regressions rather than prove B1's change, and
  each was seen failing against a break of that rule (windows decoded ahead of the hole; a stray I/O line on ID 0).

#### B2: as built ####

**Proposed, emulated (2026-11-15).** Step B2, DMA controller revision 2, on `feat/bus-b2` off main `c4ad400`, with the
owner's ten answers to its plan ("recommendations as listed") and the critic's fifteen findings. Revision 1 is gone
from the emulator, the BIOS (0.15) and OODOS (still 0.10, interface 10) in one switch, since the ROM, OODOS.SYS on the
cards, the controller and the books meet in nearly every rig. No interface programs use changed: the BDOS functions,
OOLIB's, FARCALL, the BIOS's jump table and OOUI's are as they were; the controller's registers are OODOS's alone.

- **/WAIT (U1).** `NorthBridge::wait_states()` is gone. A mapping's `wait=` is the card's /WAIT: its cycles hold the
  CPU and stretch a DMA grant it answers; on a fly-by the grant takes the longer of the memory's and the card's (one
  open-drain line). The poll takes none. The bridge's WAIT0–3 are read/write storage, reset 0, reserved for a turbo,
  and slow nothing (answer 4); the diagnostics still keep their options in WAIT3. `backplane.machine`'s RAM, flash
  and aperture lines lose `module=`, which the parser keeps for buses with no north bridge. No machine in the repo
  sets `wait=`, so no CPU cycle count moved.
- **The channel accessor (U2).** `DmaController::peek_channel(n, reg)`, `Machine::dma()` and `Session::dma()` read a
  channel with no effect; the tests that looked at a channel mid-run read it so, never by writing SELECT, which is
  the running software's.
- **The controller (U3).** The layout [above](#the-dma-controller-revision-2), state for eight channels with four
  live until B3 (SELECT 4–7, the window's reserved bytes and `$13`–`$7D` read `$FF` and drop writes; ABORT reads
  `$FF`; CONTROL is stored `& $7F`). CARD keeps bits 4–0; WIDE is stored and read back with no effect until B4
  (answer 6). REQ is latched per channel at each poll: 0 at reset and for MODULE none. `reset()`, which is also
  Apple II mode's /RESET pulse, puts back CONTROL `$70`, every CARD, REQ and the hold-off.
- **The take,** in order: nothing with the master enable off (what Apple II mode relies on: a sound card's standing
  request costs nothing there); the wired timed cards caught up, then /DREQ as the OR of their requests; a poll if
  /DREQ is low, a channel serving a card is armed and no hold-off runs (answer 2); then a grant to the highest-priority
  channel among the card channels whose module asked and the software-started ones (REQ, or a block under way), so
  a memory copy beside a stray request is served in the same take. A poll with nothing to grant gives the bus back
  after its two cycles and holds polls off for 64 cycles from the take's first; /DREQ going high and low again, a CTRL
  write, or a reset ends it early. A demand transfer ends when the granted card lets go, whatever the others ask.
  The poll cycle is like the takeover (answer 5): counted in the stolen cycles, never reported to the cycle
  observers, the trace (still version 1) or a breakpoint, and lit by the Console card with the CPU's held address
  and RDY low. A card is found by its module through a table rebuilt when the mappings change, so a take with
  nothing armed stays cheap.
- **The machine file: a departure from this spec's wording (answer 3, the critic's C12).** The migration and the B2
  row above have "the machine file's `drq=` becomes the card's module". As built, `drq` is a bare flag: the card's
  request reaches /DREQ, its module is its I/O block's, and the channel whose CARD names that module serves it. The
  `sd`, `video` and `sound` lines have it; `usb` doesn't, so the USB card's request (the event FIFO's, on every key)
  pulls nothing. `drq=N` is refused with an error naming CARD, so an old machine file can't load wired wrong. A
  bare `drq` on a line with no module ID (memory, no `module=`) is refused too (review round 1, C1): no CARD could
  name it, yet its request would pull /DREQ.
  `Mapping::drq` is a `Dreq` type that a bare 0 or 1 can't initialise (C1).
- **The BIOS, 0.15.** QUIET and the diagnostics' QUIETM abort and clear all eight channels, then select each in turn,
  7 down to 0, to clear MODE and put CARD back to its reset value, computed (7, 9, A, then none) rather than from a
  table, and put CONTROL back whole; QUIET does it after its SD section. LOADRUN, PORTJOB and PORTDMA save CONTROL,
  select their channel and put it back (LOADRUN on both ways out, so `/BOOT.BIN` starts with the BIOS's own CONTROL).
  APPLE writes `DMA_ON` with SELECT 3, `$77`, and puts nothing back: the /RESET pulse does. DMA3RUN, called by nothing
  since BIOS 0.13, is dropped. Every ID check takes `$82` exactly. The diagnostics: [diag.md, B2](diag.md#b2-dma-revision-2).
- **OODOS** keeps the SELECT rule in SDIO, DMAGO, the port copies (PORTRUN, PORTGO's callers, VBWORK), C2STOP, PIECE
  (in the interrupt), DMALOAD (which now puts the master enable back as it found it) and DEAFEN (all eight channels,
  each selected, CONTROL put back; channels 4–7's `$FF` lands 1s in STATUS bits 4–7, written to clear, which is
  harmless). Every ID check, function 75's included, takes `$82`; `hwname.s` names revision 2 for SYSINFO.
- **Not built in B2:** a WebUI DMA panel (answer 10: a later item, reading through `peek_channel`); the serial card's
  DMASTREAM, which is documented only ([the Technical Reference, chapter 9](../docs/oo1-technical-reference/09-serial-and-via.md#com1-com2-and-lpt)):
  in the emulator `$C324` is the 6551 stand-in's data register, so nothing may touch it.
- **Left for B7: [video-rev2.md](video-rev2.md)'s "DRQ wired to channel 1 (`drq=1` on the machine line)"** is
  frozen, so B2 doesn't edit it (C7). The note B7 adds to video-rev2.md (B0's question 7) is to say the wiring is now
  by module: a bare `drq` on the line, CARD `$09` on channel 1.
- **Old images.** OODOS 0.10's code from before B2 takes `$81` exactly, so on this controller its sound, video, CRT,
  DEAFEN and function 75 paths see no controller. Its SDIO and DMAGO have no ID check. Revision 1's channel 0 sat
  where revision 2's window is (ADDR `$C400`, COUNT `$C404`, MODE `$C406`, CTRL `$C407`), so an old SDIO programs
  revision 2's channel 0 while SELECT is 0 and the master is on, which is how BIOS 0.15's LOADRUN hands over with a
  revision 2 video card (VINIT's CONTROL `$71`, put back after the load), and its sectors reach channel 0. Its
  STATUS, CONTROL and ABORT at `$C440-$C442` are reserved now (reads `$FF`, writes ignored), so its master enable
  and its aborts do nothing, and its terminal-count check, reading `$FF`, always passes. But its channel-3 work
  drives revision 1's places at `$C430-$C43F`, so each copy times out (65,536 polls) and moves nothing, and that
  work is not only M:, `DM_COPY` and `DM_FILL`: the old BDOS moves every FCB and every record between the
  program's bank and the system's by MOVE on channel 3, BOUNCE copies a buffer that crosses a window by it, and the
  Disk II layer copies by it. So an old OODOS 0.10 card reaches `A>`, but no file function works: DIR says
  "No File", TYPE never comes back, and a write saves nothing it should. Don't write with an old card; replace it
  with the built-in one (measured on the emulator, B2's review round 2). Without a revision 2 video card
  CONTROL stays `$70`, the master off, and its sector reads fail too. The cards in the repo are made again. A
  user-loaded BIOS 0.14 kept by the WebUI sees no controller (its SD boot and Apple II mode refuse): the site's release note tells such users to choose "Use built-in", and that a card made before B2 must
  be replaced by the built-in one.
- **The costs measured** ([above](#the-dma-controller-revision-2), the corrected table): single mode 3 cycles a byte;
  memory to memory 1 + 2n, so D4 reads 4,096 and test_video_rev2's 32 cycles hold; SD4 5,829 KB a second (6,214 on
  revision 1); the Console card's RDY duty under single mode 0.7500 (three of each four). Held, before on main
  `c4ad400` and after: `DM_LOAD` of 64 KB 114,568 and 114,761 cycles (the bound 179,000); the load beside a stream
  117,113 and 117,410, no underrun; channel 2's first terminal count 689 and 688 cycles (the bound 1,000); every
  underrun count 0; the tick 234; PIP's 36 KB and a scrolling screen lose no tick. The frame costs:
  [oolib.md](oolib.md#the-plan).
- **Room:** ROM block 22 13,723 bytes (2,661 free), 21 8,236, 23 8,211, block 31's code to `$E407`; OODOS block 61's
  code to `$B74C` (+68), block 62's resident part to `$F5F0` (+19).
- **The expectations changed, as the owner approved them with the plan** (every other expectation held):
  - changed numbers: test_dma's single, demand, block and terminal-count cycle counts (a poll each take while a card
    asks), test_console_card's RDY duty (about 0.75, from two in three), test_bios_diag SD4's comment;
  - in meaning: test_dma's register read-back (the window, 24 bits, ID `$82`, REQ in place of STATUS's request
    bits), test_dma's second card served by CARD, test_machine's `drq` cases, test_backplane's WAIT case (they
    slow nothing), test_usb's DMA case (CARD `$0B`), test_bios's Break and `C806` cases (the new places);
  - mechanical, the numbers unchanged: every suite that programs the controller moved to SELECT and the window, and
    channel reads through `peek_channel`;
  - the version: BIOS 0.15 in every greeting (the tests, the e2e specs, `wasm-port.test.ts`, `boot.mjs`), the six
    SYSINFO goldens' line 6 and their CARDS line (`$82`, revision 2), test_oodos_sysinfo's `ROMV 00 0F`,
    test_oodos_utils_rom's `romver` 15, and `romdiag.txt` (BIOS 0.15, D1 `ID $82; REGISTERS READ BACK ON 4
    CHANNELS`);
  - the docs gate: `registers.json` regenerated (12 DMA records), chapter 5's 11 entries, the listings re-cut.
- **New cases:** in test_dma, the window following SELECT, CARD's reset values, 24-bit addresses, the grant by
  module, a stray request's 2 cycles in 64, the hold-off's two early ends, the release rule, /WAIT on a fly-by and
  none on the poll, and the take order; test_decoder's and test_backplane's /WAIT cases; and the system-level cases
  (the SELECT rule over a stream with SD reads and a stray interrupt, Break leaving SELECT as found, a bad CARD
  under the monitor then Break and `C80CR`, Break during D1, a revision 1 ID refused, Apple II mode with no stolen
  cycle, stage 8's SELECT 3), each with the fault it must fail on named in the plan's section 4. Stage 8's SELECT 3
  after D1, dropped, fails D5's terminal-count check (`NO TERMINAL COUNT IN STATUS`); D2–D4 pass on channel 0, since
  memory to memory works on any channel (corrected 2026-11-15, the owner's answer 4: the plan had named D2).
- **The books:** the Technical Reference's chapters 1–15 where they named revision 1, DRQn and /DACKn, channels by
  number for a card, or the north bridge's wait states; the Programmer's Guide's function 75 and cost figures; the
  User's Guide's SYSINFO sample; [diag.md](diag.md#b2-dma-revision-2), [oolib.md](oolib.md#the-plan),
  [oodos.md](oodos.md) (function 75), and `emulator/docs/` (EMULATOR_PLAN.md, BUS_TRACE.md).

#### B3: as built ####

**Proposed, emulated (2026-11-16).** Step B3, eight channels, on `feat/bus-b3` off main `2fedaa6` (B2's merge), with
the owner's eleven answers to its plan ("recommendations as listed") and the critic's fifteen findings. No interface
programs use changed, and no ROM or OODOS byte: the BIOS stays 0.15, OODOS 0.10 (interface 10).

- **The controller.** `DmaController::LIVE` (4) is gone (answer 1): `CHANNELS` (8) is the one count, at the take's
  armed-channel scan, the poll's REQ latch, the window's read and write, `peek_channel` and `irq()`. So SELECT 4–7
  reach live channels, each with its bit in STATUS, ABORT and REQ, served in priority after 0–3, its terminal count
  with MODE's IRQ bit pulling IRQH bit 0. Everything else was already eight wide. Channels 4–7's CARD resets to `$08`
  (none), so software that never arms one sees no change and no cycle count moved outside D1's own scan.
- **No byte changed.** Three source comments that said 4–7 read `$FF` were corrected, each file keeping its line
  count (answer 5): `backplane.inc`'s `DMA_CH4`, D1's header in `diag22.s`, DEAFEN's in `irq.s`. The ROM (every
  block) and OODOS.SYS, with the programs, built from the branch and from a clean export of `2fedaa6`, compare
  equal. D1 counts the fitted channels at run time, so it prints `ON 8 CHANNELS` with no ROM change, as B2's answer
  7 foresaw. The BIOS's QUIET and QUIETM, D1 and OODOS's DEAFEN already looped SELECT 7 to 0 (B2).
- **The cards.** `make_card.sh` leaves `oodos.sparse` as it was. `demo.sparse` changes in six sectors, date stamps
  only (the FAT entries' create and write times, the CP/M labels' and timestamp entries' time bytes in `A.IMG` and
  `B.IMG`); every file in it is the same as on main. The script stamps the card with the date of the last commit to
  its sources, which is B3's comment commit (`backplane.inc` and `irq.s`), 2026-11-16 14:29:11 UTC. Main's committed
  card had been stale since B2's `2db1504` (2026-11-15 21:47:15), so it would have changed on main too.
- **The expectations changed, the sixteen the owner approved with the plan** (every other held):
  - a changed number (1): `romdiag.txt`'s D1 record, `ON 4 CHANNELS` to `ON 8 CHANNELS`, the dots and width the
    same; it fails with `LIVE` left at 4;
  - in meaning (4, test_dma): `peek_channel`'s "channel 4 reads `$FF`" replaced by reads of channels 4–7; the window
    case renamed `the_window_follows_select_on_all_eight_channels`, its SELECT 4–7 "read `$FF`, dropped" loop and its
    "reached no live channel" check removed for the isolation case below;
  - mechanical (11): the loops over `LIVE` now over `CHANNELS` (test_dma 8, test_bios 2, test_bios_diag 1), and
    test_dma's "SELECT 3 kept" comment corrected to SELECT 7.
- **New cases,** each seen failing under the fault named: in test_dma, channels 5 and 7 copying memory to memory (the
  take's scan left at 4), SELECT reaching one channel of eight (SELECT decoded with two bits; the window's guards
  left at 4), a card served by channel 5 (the REQ latch left at 4), STATUS, ABORT and the interrupt on channels 4–7
  (`irq()`'s loop or ABORT's walk at 4; a TC bit or ABORT aliased to `n & 3`), priority from 0 to 7 (the scan
  reversed), and `peek_channel` over 4–7 (its guard at 4); at system level (answers 2 and 11), Break stopping and
  clearing channels 4–7 after a real terminal count on channel 6 (QUIET's loop from SELECT 3, its ABORT or its STATUS
  write cut to `$0F`), `C806` stopping a program's channel-5 copy before its fills (APPLE's ABORT cut to `$0F`:
  the IIe's memory fills with `$A5`), and OODOS's DEAFEN clearing a stray terminal count on channel 6 (its loop from
  SELECT 3: the system hangs in the interrupt).
- **Measured** (answer 6), the full diagnostics run once on B3 at 7,159,090 Hz: a quick run 56.5 s, a full run
  165.6 s (`a_full_run_never_writes_the_sd_card`'s 164.4 s), as on B2, so [the Technical
  Reference, chapter 15](../docs/oo1-technical-reference/15-diagnostics.md)'s figures stand.
- **Left as gaps, recorded:**
  - D1 writes and reads back each channel alone, so it can't see SELECT's top bit lost (channels 4–7 answering as
    0–3) on real hardware; the emulator's isolation case holds the emulator to it. An alias pass in D1 is a later
    diagnostics item, needing a later BIOS, 0.17 or after (answer 3; 0.16 is B5's alone, B5's answer 11;
    [diag.md](diag.md#b2-dma-revision-2)).
  - D1's not-fitted path can no longer run on the emulator, which fits eight; B2's golden ran it (`ON 4 CHANNELS`)
    and B3 changes none of its bytes (answer 4).
  - A program's channel 4–7 survives the warm boot: DEAFEN clears its MODE's IRQ bit (and its terminal count, if that
    interrupt was on) but doesn't abort it (answer 10). Break recovers it (QUIET aborts all eight); an ABORT of 4–7
    in DEAFEN or the warm boot is for OODOS's next release, an OODOS byte change outside B3. *Done in B5 (its U9,
    [B5: as built](#b5-as-built)):* the warm boot aborts channels 4–7 and zeroes their counts before DEAFEN.
- **Not changed:** the WebUI and the WASM bindings (no DMA panel yet, B2's answer 10); `hw/diag.md`'s plan-era sample
  screen and `oolib.md`'s L-phase inventories of four channels (answers 7 and 8); no site release of B3's own
  (answer 9).
- **The books:** the Technical Reference's chapter 5 (all eight live; STATUS, ABORT and REQ), its contents, chapter
  15's D1 count; [diag.md](diag.md#b2-dma-revision-2); `emulator/docs/EMULATOR_PLAN.md`.

#### B4: as built ####

**Proposed, emulated (2026-11-16).** Step B4, 16-bit transfers, on `feat/bus-b4` off main `cfedc62` (B3's merge),
with the owner's eleven answers to its plan ("recommendations as listed") and the critic's ten findings. CARD's WIDE
is live in the emulator. No interface programs use changed, and no ROM or OODOS byte: the BIOS stays 0.15, OODOS
0.10 (interface 10). `git diff --stat main` names no path under `backplane/`, `oodos/` or `emulator/test/host/sd/`,
so no ROM, OODOS.SYS or card was rebuilt or compared, and nothing in the ROM or OODOS sets WIDE (answer 6).

- **The bus** (`decoder.h` 0.14.0, `device.h` 0.8.0, `bus_cycle.h` 0.2.0). The decoder gains word cycles beside its
  byte ones, which are unchanged: `dma_read_word`, `dma_write_word` and `dma_fly_by_word`, each one bus cycle at an
  even physical address, the even byte on D0–D7 and the odd on D8–D15, stretched once by /WAIT (the longer of the
  memory's and the card's). The mapping that decodes the even address answers both lanes, as one chip select does,
  even where another mapping overlays the odd address (a small choice: today's overlays are 16 KB-aligned, so nothing
  is affected); an odd byte past that mapping's end floats `$FF` and its write is dropped. Unmapped, a word reads
  `$FFFF`. Memory steering, a byte cycle at an odd address carried on D0–D7, needed no code: RAM, ROM and the
  aperture are byte devices reached by offset. The line that tells byte cycles from word cycles (/WORD, B6) is
  modelled as the call, and as a word flag on `Access` and `BusCycle`, which carries D8–D15 too. The observers see
  one cycle a word, at its even address. `Device` gains `dack_read_word` and `dack_write_word`; their defaults are a
  card with D0–D7 only (one register access a grant, D8–D15 high, the odd byte from memory lost).
- **The controller** (`dma_controller.h` 0.4.0). At each take the channel moves words when WIDE is set, neither DEC
  nor HOLD is, and, memory to memory, ADDR and DEST have one parity; it is decided each take, since software may
  rewrite them between. Then a transfer is a word at an even ADDR with two bytes or more left, else a byte (an odd
  head, an odd last byte). COUNT drops by the width, ADDR and DEST step by it (`$FFFFFE` + 2 wraps to 0), TC comes
  with the transfer that empties COUNT, and BURST counts transfers (answer 1). AUTO, STATUS, the interrupt, ENABLE
  and REQ, the take, `reset()` and `pulse_reset()` (CARD's reset values have WIDE 0) are as they were.
- **The cards' widths, as answered** ([above](#16-bit-transfers)): the video card's port takes a word as two PDATA
  accesses, each stepping PSTEP (answer 2, `video_card.h` 0.2.0); the aperture answers both lanes, with no code
  (answer 3); the SD card's DATA (answer 4), the sound and USB cards (answer 5) and the serial card take bytes, and
  their code is unchanged. The Console card's lights and LDATA show D0–D7, the even byte, on a word cycle, and its
  breakpoint matches only the even address, so a breakpoint on a word's odd byte doesn't fire on a WIDE DMA
  (answer 7); `console_card.cpp` is unchanged. The CPU card's word buffer (a "may", not in the B4 row) isn't modelled:
  it is invisible to cycle counts.
- **The expectations changed, the two the owner approved with the plan,** both comments in `test_dma.cpp`, no check
  touched: the file's comment (one cycle a fly-by transfer or two a memory-to-memory one, a transfer being a byte,
  or a word with WIDE) and the read-back line's "it does nothing until B4". Every other expectation held, since
  nothing set WIDE before a transfer: the 64 KB copy's 131,073 cycles, `DM_LOAD`'s 114,761, the frame costs, SD4,
  the goldens and every version string.
- **New cases,** each seen failing under the fault named:
  - test_decoder: a word's lanes (lanes swapped); one /WAIT stretch a word, a fly-by 4 cycles with the memory's 2
    and the card's 3 (a word as two byte cycles); a byte-only card in a word grant (the default reading the card
    twice); an unmapped word and the observer's one cycle (told twice, or D8–D15 left 0); a mapping ending even
    (the odd lane looked up by its own address).
  - test_dma: a 2 KB copy even to even, 1 + 2,048 cycles (WIDE ignored: 1 + 4,096); an odd head and last byte as
    bytes, TC on the last grant whatever its width (a word at an odd address; TC compared with 1); DEC and HOLD
    moving bytes under WIDE (either guard dropped); a demand transfer ending when the card lets go after a word,
    added in review (the release checked after byte grants only); the parity rule (dropped); COUNT, ADDR and DEST
    stepping by 2 and the wrap, with AUTO and the interrupt (`step()` by 1; the carry into A16–A23); a byte-only
    card on a WIDE channel (the default hooks moving two bytes); single mode, 3 cycles a word (the word split in
    two grants); BURST counting transfers (bytes counted); the observer's one cycle a word (each byte reported); a
    channel with no card floating `$FFFF` (the float on D0–D7 only); and the 64 KB copy on the backplane machine
    with CARD `$18` (WIDE ignored).
    `card_resets_to_todays_wiring` is the /RESET guard (the critic's 2): run once with reset keeping WIDE, it fails.
  - test_video_rev2: the port's two accesses at PSTEP 160, stepping down, PSTEP 1 and PSTEP 0 (the word stored at
    PADDR and PADDR + 1 with one step); a word against PCTL's direction (the direction not checked; PADDR stepped);
    word cycles through the aperture at `$E00200` and, by bank 8, `$80400` (the aperture on D0–D7 only); a 4 KB port
    load (the card's word hooks left at the defaults).
  - test_bios_sd (answer 11): `C40C: 17` at the monitor, Break, then `C80C` boots (QUIET's write of CARD made a
    read: CARD stays `$17` and the boot never completes).
- **Measured** at 7,159,090 Hz:
  - a 64 KB copy memory to memory on the backplane machine (channel 3, CARD `$18`, BURST no limit): the DMA holds
    the bus 1 + 2 × 32,768 = 65,537 cycles (9.154 ms), against 131,073 (18.309 ms) without WIDE; the program, with
    its setup before WAI, wakes at cycle 65,606 (9.164 ms) against 131,142 (18.318 ms), the same program under the
    WIDE-ignored mutant;
  - a 2 KB copy, even to even, 1 + 2,048 cycles; 16 bytes even to even 1 + 16, odd to odd 1 + 18 (a byte, 7
    words, a byte), even to odd and odd to even 1 + 32;
  - 256 bytes copied into the aperture at `$E00200`, and at `$80400` through bank 8, 1 + 256 cycles;
  - a 4 KB load into the video port from RAM (channel 1, demand, CARD `$19` against `$09`): 2,050 cycles (0.286 ms)
    with WIDE, 4,098 (0.572 ms) without: the takeover and the poll, then a cycle a word against one a byte;
  - single mode, 3 cycles a word: the takeover, the poll, the word.
- **Apple II mode is unaffected:** the master enable is off in the mode, so the controller never takes the bus;
  the /RESET that enters it puts every CARD back with WIDE 0; APPLE writes no CARD, and its six fills are HOLD fills
  from an odd held source, bytes by two rules; the IIe's character ROM reaches card memory by CPU writes to PDATA;
  the video card's snoop sees only the CPU's byte cycles. No case was added (answer 9): none could fail under a
  plausible fault.
- **Gaps, recorded:**
  - A WIDE left on channel 0 by the monitor (`C40C: 17`) or WOZMON.COM under OODOS: the SD card's DATA takes bytes,
    so each word grant moves one byte; a boot or a sector read puts the sector's first 256 bytes at the even
    addresses, `$FF` in each odd one, and leaves the other 256 in the card, and a sector write hands the card 256 of its 512 bytes before TC (answer 11). Break recovers it, since QUIET writes
    every CARD whole; OODOS's warm boot (DEAFEN) doesn't. OODOS's next release writes CARD back in its warm boot,
    with B3's ABORT of channels 4–7. *Done in B5 (its U9, [B5: as built](#b5-as-built)):* the warm boot puts every
    channel's CARD back to its reset module, WIDE off, with that ABORT, so a WIDE left on channel 0 is cleared at
    the next `A>`.
  - No diagnostic sees D8–D15 (answer 8): the CPU and every byte cycle use D0–D7, so a fault there is harmless while
    no software sets WIDE. A word pass is a later diagnostics item, with a later BIOS (0.17 or after): a WIDE copy of one parity checked
    by the CPU, in D3, and a word reading beside SD4's if the SD card takes words
    ([diag.md](diag.md#b4-16-bit-transfers)).
- **Later** (answer 6): software adopting WIDE where a card takes words, PORTJOB's scroll jobs, PORTDMA, PORTRUN and
  VBWORK through the port, DMAGO's copies of one parity, and LOADRUN and SDIO only once the SD card takes words. It
  ships as a later BIOS (0.17 or after; 0.16 is B5's alone), with B3's D1 alias pass and the word pass, and OODOS's next release (B3's ABORT of 4–7 is in B5's warm boot already),
  writing CARD whole at each setup and re-measuring `DM_LOAD`, the frame costs, `DM_VWRITE` and SD4. The Console
  card's lights and latch widened to 16 bits is later Console card work.
- **Left for B7:** [video-rev2.md](video-rev2.md) is frozen and not touched. The note B7 adds (B0's answer 7) is to
  name the port's word rule and the aperture's two lanes, and that its "2 cycles a byte" (its diagram, and the DMA
  bullet under the port) and "about 8,200 cycles" for a 4 KB font are revision 1's pace: the emulator moves a port
  byte in one cycle, a word in one cycle with WIDE.
- **Not changed:** the bus trace (still version 1: DMA is a gap there, word cycles too), the WebUI, the WASM bindings
  and `Session::TraceRow`; no machine-file token (a card's width is its class's); no site release of B4's own (answer
  10).
- **The books:** the Technical Reference's chapters 5 (a new section,
  [16-bit transfers](../docs/oo1-technical-reference/05-dma.md#16-bit-transfers)), 6, 7, 8, 9, 10, 12 and 14 and its
  contents; [diag.md](diag.md#b4-16-bit-transfers); `emulator/docs/`
  (EMULATOR_PLAN.md, BUS_TRACE.md).

#### B5: as built ####

**Proposed, emulated (2026-11-16).** Step B5, the software and the books, on `feat/bus-b5` off main `c5c86d0` (B4's
merge), with the owner's answers to its plan (Q1–Q15 "recommendations as listed", and Q16–Q19), the critic's twenty
findings, the owner's answers at three build stops (U5, HW_CLOCK, U7), and those to its review round 1 (R1, E1–E3
and W1–W5 fixed, each marked "review round 1" here; W6 accepted) and to its review round 2 (F1–F4 fixed, marked
"review round 2"). The ROM BIOS is 0.16, B5's alone
(answer 11). OODOS's interface goes from 10 to 11 (function 75's A = 3); OODOS stays 0.10 until the site release
that carries B5, which makes it 0.11 (answer 10), so the books' "since OODOS 0.11" is true from that release. Every
interface only grows: the jump table gains its ninth entry, function 75 an argument, and nothing existing changes.

- **The flash, emulated** (answer 2; `flash.h`, `flash.cpp`, new). A `flash` machine-file line is two chips side by
  side, even and odd bytes, each with its own command decoder at its chip addresses `$5555` and `$2AAA` (A0–A14
  compared): program a byte (the new byte the old AND the data), sector erase (4 KB of a chip, 8 KB of the flash),
  chip erase, software ID (`$BF` the maker's, `$B6` the part's, at chip addresses 0 and 1, until `$F0`). A chip keeps
  its place in a sequence until its next write, with no time-out and no reset pin (critic 3), so one left after
  `$A0` programs the next write wherever it lands. Busy times are the SST39SF020A's typical ones as bus cycles at the
  CPU clock (the Session tells each Flash the clock, as it tells the other timed cards, and so does the command-line
  app, at start and on a clock change: review round 1, E1): program 14 µs = 101
  cycles, sector erase 18 ms = 128,864, chip erase 70 ms = 501,137 at 7,159,090 Hz. While busy a chip ignores
  writes and reads give its status (bit 7 the inverse of the byte's, 0 in an erase, bit 6 toggling a read, bits 0–5
  the array's); `peek()` doesn't toggle; the other chip works on. The change lands in the array when the operation
  starts (a simplification: a real part interrupted leaves a half-done byte), and there is no wear and no word
  buffer. Test-only hooks under `EMU_FAULT_HOOKS`, in the Flash itself (critic 14): a byte that won't program and
  longer busy times. `rom_override` takes the first `rom` or `flash` line. `backplane.machine`'s ROM A became a
  `flash` line in U1 and ROM B in U3 (the owner's answer 7 at the U5 stop), both `name=` kept; `Rom` is unchanged,
  so the Apple-1's ROM still ignores writes.
- **The IIe images in ROM A** (the owner's B0 answers 1–2). Image *n* is ROM A block *n* (0–3), set *n* 8 KB at
  `$10000 + $2000 × n` (blocks 4–5); `backplane.inc` gains `IIE_IMAGES` (4) and `IIE_CHARS_BLOCK` (4), and
  `CART_BLOCK` stays as ROM B's first block. `--apple-rom [N=]FILE[,FILE]` and `--apple-chars [N=]FILE` (answer 6,
  critic 9): N one digit and `=`, image 0 without, halves adding up within an image as before, a set given twice
  refused, `dir/x=y.rom` a path. The loader checks every size first, erases each slot, then writes.
  `MachineOptions::apple_rom` and `apple_chars` are image 0's, so every suite places its fixture as before.
- **`$C806`** takes the first good image (`$06` at its `$FBB3`; FINDIMG, the image's number at zero page `$08`,
  critic 7), with that image's own set, or the VGA font with that set erased, never set 0's. With none:
  `NO APPLE IIE ROM IN FLASH IMAGES 0-3` (Q17), W1 back on 1. The copy, the fills, MODE and /RESET are unchanged
  ([Apple II mode holds as](#the-flash-rom-a-and-rom-b)); the Technical Reference's chapter 13 changed only in the
  places answer 15 listed.
- **The dispatching entry, `$C818`** (answer 1; `bios.s`, ROMCALL). The ninth JMP, after which no entry is added;
  START and the code after it moved 3 bytes, which nothing names by address. A program asks with Y = `$5A` and X =
  `$A0` + the function, A its argument. `$A1` is Apple II mode with image A (apple.s's APPLEN, sharing `$C806`'s
  checks and copy): A over 3 returns, carry set; image A not good says `NO APPLE IIE ROM IN FLASH IMAGE n` and goes
  to the monitor. `$A0`, the flash update, is the menu's alone in B5 (Q18): with the token it returns carry set, as
  `$A2–$AF` do, A, X, Y and the windows as they were. Without the token (a typed `C818R` leaves X 0) a menu of one
  line, `ROM: F FLASH UPDATE, 0-3 APPLE II MODE, ANY OTHER KEY MONITOR` (critic 17), which never returns.
  **The row's correction (Q14):** `$C818`, "the next after `$C815`", was START on BIOS 0.15, so a call there
  restarted the machine; hence OODOS's gate, below.
- **The flash update** (answer 4, Q16, Q18, Q19; `flash.s`, new, in block 31's FLASH segment, and `sd.s`'s SDLOAD,
  placed after its messages so the books' listings didn't move). From `C818R`, `F`: the image asked for, 0–3;
  `/APPLE2En.ROM` (16,384 bytes, needed) and `/APPLE2En.CHR` (4 or 8 KB, optional) read by the CPU from the card's
  root into RAM blocks 59 and 58, their sizes the root listing's, not the sectors' (critic 8); size and `$FBB3`
  checked before anything is erased; `WRITE FLASH IMAGE n (Y/N)?`; each chip put back to reading (`$FF` then `$F0`,
  critic 3); the image's `$FBB3` (the odd chip's byte) programmed to `$00`, polled, so the image is not good before
  anything is erased (review round 1, R1: before it, the image's first unit was erased with its `$06` still there, and
  a Break or a chip that didn't answer left a half-erased image `$C806` counted good); the image's four sector erases
  and the set's two; every pair but the commit bytes programmed, a byte
  started in each chip and then each polled at its own parity (Q16, the design's order); a verify through a
  read-only window; the commit bytes (the set's first 8, then the image's `$FBB3` last of all); the verify again. A
  mismatch (`FLASH IMAGE n NOT WRITTEN AT $Fxxxxx`) or a chip that doesn't answer in 65,536 pairs of reads (about
  180 ms; `THE FLASH DOESN'T ANSWER`) erases image and set again, `$FBB3` to `$00` first, unless it is that erase
  again that didn't answer; an odd chip that stops answering before `$FBB3` is made `$00` (in the chips' `$FF`
  and `$F0`) doesn't answer the erase again's `$00` either, so nothing is erased and the image is left as it was,
  good if it was (review round 2, F4: chapter 3 had said the image wasn't good then). It runs QUIET with interrupts off and sets every window itself however
  entered (Q18, critic 12). Every flash write goes through FPUT, which refuses at run time any W1 value but
  `NB_ROM | 0–5` (`THE FLASH UPDATE REFUSED BLOCK $bb`, critic 4), and an `.assert` holds the arithmetic at build;
  no chip erase is ever sent. Zero page `$10–$21` and `$0280–$029A` are its own; sd.s's zero page grew to
  `$F0–$FB`. One image and its 8 KB set take 1.48 s at 7.16 MHz in the emulator. The build writes `bios-even.rom`
  and `bios-odd.rom` (`romsplit.py`, in the Makefile and `emulator/cmake/roms.cmake`, critic 1).
- **OODOS** (answers 3, 7, 8, 12, Q17). Function 75's A = 1 looks where the running ROM BIOS's `$C806` looks: NEWROM
  reads cold.s's ROMVER, the major first (above 0 passes), then the minor (16 or more), `$FF $FF` counting as old
  (critic 18); from 0.16 the first good image of ROM A's four, before it the cartridge exactly as before. **A = 3**,
  X the image, goes through `$C818`/`$A1`, with A = 1's card and DMA checks; X of 4 or more gives A = X = `$FF` and a
  ROM BIOS before 0.16 A = `$FF`, X = `$FB`, both before anything is written back; image X not good A = X = `$FF`
  after the write-back. The CCP's **`APPLE2 [n]`** (in RES2; any other tail the command and `?`) and its messages:
  `No IIe ROM in the flash (the emulator: Settings, ROMs)`, `No IIe ROM in flash image n (the emulator: Settings,
  ROMs)`, `No IIe ROM: this ROM BIOS looks in the cartridge (Settings: Use built-in)` under an older ROM BIOS (by
  function 77, the same gate), and `APPLE2 n needs ROM BIOS 0.16 or later`. `OOLIB_IFACE` is 11. The demo card's
  README says `APPLE2 [n]`.
- **RES didn't move** (§7 gate 2): every RES and COMMON label equal to main's on `oodos.map`, normal and DEBUG builds
  (471 and 479 labels), COMMON byte-identical. The old APPLE2's 39 bytes of code and 59 of message in RES keep their
  size, holding three of the new messages and `$FF` filler, with `.assert`s. **ROMENTRY** (a departure, the owner's
  answer 5 at the U7 stop): RES2's room is set by the DEBUG build (the dispatcher's counters), which a separate
  ROMAPPN overflowed by 44 bytes, so ROMDIAG and A = 3's exit are one routine: A, X and the entry's low byte written
  into the tramp, Y = `$5A`, a refusal's RTS a restart at `$C800` as before.
- **The warm boot (U9, answer 12),** B3's answer 10 and B4's answer 11 carried: WBQUIET's `JSR DEAFEN` became
  `JSR WBDMA`, the same length. WBDMA, a 19-byte stub in RES2, maps W2 as CALL61 does and calls **DMAWARM**, at the
  end of block 61's code (`video.s`, the last of the BIOS segment, so nothing in block 61 moved; no CALL61 entry,
  which would have moved the rest of sound.s and video.s): ABORT of channels 4–7, their counts 0, every CARD back to
  its reset module (7, 9, A, then none) with WIDE off, the master enable and BURST kept while SELECT moves, CONTROL
  put back whole; then DEAFEN. Checked against U6's build: every label in RES, COMMON and block 61 the same, block
  61 byte-identical up to its old end. The gaps "B3: as built" and "B4: as built" recorded are closed.
- **HW_CLOCK** (`backplane/oohw/hwclock.s`; the owner's answers at the U5 and HW_CLOCK stops). U5's layout read
  7,159,342 Hz by the clock chip, outside test_oodos_sysinfo's 0.002%, which the owner kept: OODOS's millisecond
  handler sat at every second's change, and the old pass stamped a change with the pass that saw it. Now a change
  is stamped by the last pass before it, with a gap that brackets it (79 cycles with no interrupt), and of the six
  pairs of its four changes HW_CLOCK keeps the one whose bigger gap over the windows between them is least. Checked
  on U5's ROM padded by 1 to 31 bytes: the clock chip −95 to +58 cycles of 7,159,090, the frame −34 to +13
  ([diag.md](diag.md#d3a-review), the dated B5 paragraph). The diagnostics link it, so **ROM blocks 21–22 and their
  sums changed**; block 22's BSS (D22BSS) has 4 bytes left. SYSINFO.COM changed on both cards.
- **The Session's kept flash (answer 5, U7).** `MachineOptions::flash_user` carries ROM A's blocks 0–5 from the
  running machine's array (not a busy chip's status) into each build, applied after the line's image loads and
  before any IIe slot given, so a power cycle keeps what a program's update or the row wrote, without a separate
  Session copy (the owner's answer 3 at the U7 stop). `set_cartridge` erases image 0 and set 0 in the copy and
  writes the row's files, but only when the IIe ROM's or the characters' bytes differ from the ones the row last
  wrote, so a Disk II PROM alone, or the same bytes again, keeps what a program's flash update wrote (review round 1,
  E2); images 1–3 are kept. The copy is local to the build, so a refused ROM leaves the running
  machine and its flash as they were (critic 10). Not across a page reload, never written back to a file. Blocks
  6–31 and ROM B are the files' again at each power cycle. The WebUI's physical reads take A0–A23 (answer 4 at the
  U7 stop; A0–A19 before), so they reach `$F00000`. `Session::device(name)` gives a test the Flash's hooks (answer
  4 at the U5 stop).
- **The expectations changed, all approved:** §3.1's group A, 59 version lines (BIOS 0.15 to 0.16), and the two U2's
  grep missed, `e2e/boot.spec.ts:48` and `panel.spec.ts:105` (the U7 stop's answer 1); group B, 16 lines (interface
  10 to 11); §3.2's nine sites: test_bios's cartridge case renamed `rom_a_takes_…` (six sites), its two C806R message
  lines to Q17's text, and test_oodos_utils_rom's A = 1 message; and `test_bios_diag.cpp:1251`, V3's frame window
  `119437 + 80` either side (the HW_CLOCK stop's answer 1). No other expectation changed.
- **New cases,** §4's 1–37 with critic 3's 1a, 23a–23c and Q17's 29b, each seen failing under its mutant: test_flash
  (1–7), test_machine (8), test_bios (9–17, 24), test_bios_sd (18–23c), test_oodos_utils_rom (25–31), test_session
  (32), wasm-port.test.ts (33), test_oodos_input (34, 35), test_app_debug.py (36), test_bios_diag (37: ROM A
  byte-equal to `bios.rom` after a full run), and test_oodos_sysinfo's interrupt-after-the-last-poll case.
  test_bios_sd took 452 s alone under ASan with the update cases, so it is split in three (`EMU_SPLIT`, TIMEOUT 300
  kept; the U5 stop's answer 5). `make_card()` moved into `sd_images.h` (critic 2). Review round 1 added, each seen
  failing under its mutant: two test_bios_sd cases for R1 (Break during image 1's first sector erase, lengthened;
  a sector erase past the poll's time-out twice; each `$FBB3` not `$06` and `C806R` taking image 3), test_app_debug's
  sector erase at `--clock 1000000` (E1), test_session's and wasm-port.test.ts's row cases (E2), and test_flash case
  4's wrong second unlock pair and `$10` at a wrong address (E3). Review round 2 added a test_bios_sd case for the
  erase again after the commit (F1: the set's first byte refusing to program, so the last verify fails with `$06`
  already at `$FBB3`; Break during the erase again's first unit erase; `$FBB3` not `$06` and `C806R` taking image
  3), seen failing with the `$00` skipped when FFAIL is set. test_app_debug's E1 check pins the start's
  `set_cpu_hz` only, and says so (F3): the app's one clock change is Apple II mode's, where no flash command can
  be timed. Chapter 14 gained E2's rule for the row (F2), and chapter 3's step 9 the odd chip that stops answering
  before `$FBB3` is made `$00` (F4).
- **Measured,** from U7's build (`b2b64e8`): ROM block 31 has 5,159 bytes free (`$EBCD–$FFF3`; 7,148 on main;
  5,147, `$EBD9–$FFF3`, after review round 1's R1);
  block 21 7,896, block 22 2,409 and block 23 7,917. OODOS's RES region 219 bytes free (`$F725–$F7FF`, RES2 `$316`
  long), BDOS 1,217, COMMON 166 (unchanged). The flash's busy times and the update's 1.48 s above.
- **The cards** were made again after each OODOS unit (U3, U6, U9) and after HW_CLOCK, with a restamp commit after
  each (the U5 stop's answer 8): `oodos.sparse` and `demo.sparse` change in OODOS.SYS and SYSINFO.COM, the demo
  README's line, and date stamps.
- **For the site's release note** (answer 9; no release of B5's own, answer 10): a kept user BIOS older than 0.16
  says `NO APPLE IIE ROM IN THE CARTRIDGE`, since ROM B is erased now; Settings' "Use built-in" fixes it. An uploaded
  OODOS 0.10 card says `No IIe ROM in the cartridge` for the same reason; the built-in card fixes it. Kept IIe ROMs
  in the browser restore into ROM A unchanged. No compatibility copy goes into ROM B.
- **Left for later:** the D8–D15 word pass, B3's D1 alias pass and the WIDE adoption, with a later BIOS (0.17 or
  after) and OODOS's next release (answer 11); an image picker in the WebUI; reading the images in SYSINFO; a flash
  update at `A>` (answer 13: 75 A = 4 with the interface raised, if ever); `$C818`'s `$A0` for programs; the owner's
  check of the command addresses, the ID codes, the times and whether a part abandons a half-sent sequence after a
  time-out, against the SST39SF020A's data sheet (answer 2); `tools/test_coverage.py`'s temp-dir bug, in its own
  commit after B5 (the U5 stop's answer 9; review round 1, W6).
- **The books:** the Technical Reference's chapters 1, 2 (reprogramming the flash, ROM A's blocks, zero page), 3
  (`$C818`, the flash update), 5 (the warm boot's DMAWARM, review round 1), 11, 13 (answer 15's changes), 14 (the
  flash, the options, the kept blocks), 15 and its contents, with the six listings the code moved (chapters 3, 4, 7
  and 13); the User's Guide's APPLE2, its errors and introduction; the Programmer's Guide's functions 75 and 77,
  `FT_INFO`, chapter 8 and appendices A and D; `docs/index/functions.json`;
  [oodos.md](oodos.md#leaving-oodos-wozmon-and-apple2), [diag.md](diag.md) and `emulator/docs/EMULATOR_PLAN.md`,
  annotated.

#### B6: as built ####

**Proposed, docs only (2026-11-17).** Step B6, the pin-out, on `feat/bus-b6` off main `c143b1b` (B1–B5 and the
fixes after B5), with the owner's answers to its plan (Q1–Q14, "recommendations as listed"; Q3 then withdrawn by the
owner) and the critic's fourteen findings. No code, ROM, OODOS, emulator, WebUI, test or card changed: the emulator
models the bus's signals, not its pins.

- **The owner gave no map of their own** (Q1), so B6 published the draft pin-out in [modules.md](modules.md), as a
  draft: the signals are decided, and every position (the pin numbers, the split between the sides, where the
  grounds go) is open, the owner's (Q2).
- **The signals:** 67 lines, the 64 signals and BR0–BR2; with 6 +5V, 23 GND and 2 spares, 98 contacts, 49 a side.
  The lines are the 62-pin bus's (but DRQ0–DRQ3, /DACK0–/DACK3 and +24 V) with D8–D15, A20–A23, /MEM, /DMA,
  /DREQ, /WAIT, /WORD and BR0–BR2.
- **The draft's shape** (Q4): power and the slow lines in the short segment (/RESET, /NMI, /IRQ, AMODE and
  /BOOTSEL on side A; BR0–BR2 and /DREQ on side B); data on side A and address on side B, in order; at most four
  lines between grounds on both sides, the spares and BR0–BR2 counted; a ground on either side of PHI2 and of OSC;
  the rest spares. Choices inside that shape, all positions and so the owner's to change: RDY, BE and /WAIT beside
  PHI2, not in the short segment (they act within a cycle); side A held to four as well, which puts VPB after OSC;
  /BOOTSEL (A11) and TC (A48) on side A, since side B's grounds for its groups of four leave it room for 35 lines, one fewer than the first draft put there, and moving both keeps a spare, RSV1, on side B;
  and two spares, RSV0 and RSV1, which is what the 23 grounds leave.
- **Six +5V contacts** stand on the chosen connector's current rating per contact and the owner's budget for each
  of the 15 or so slots (Q4).
- **The slot is shrouded and keyed** so that no other card fits. The shroud, its key and the socket's part number
  are the owner's (Q5). The owner takes a stray PC card in the slot as impossible, so the draft has no rule about
  what PCIe puts on each contact (Q3, withdrawn).
- **/WORD** is the line that tells a byte cycle from a word cycle: one line, low on a word cycle, pulled up (Q6).
  [Byte and word cycles](#the-physical-map-on-the-new-bus) and B4's "(B6's pin)" now name it.
- **The spares** are RSV0 and RSV1, routed to every slot, unconnected (Q7). **/OE** is kept, driven by the master
  that holds the bus, pulled up (Q8).
- **Pull-ups and directions** (Q9): /WORD, /MEM, /DMA and /IACK are pulled up as well as the 62-pin bus's lines,
  /WAIT, /DREQ, /OE and the weak ones on D0–D15, so a bus handover selects no memory and makes no poll; and, from
  review round 1, PS0–PS3 (to `0xF`, the idle bus's ID since review round 2, so no card's registers answer) and RWB
  (so the cycle is a read, with /OE high). /NMI is IN (the USB card and the Console card pull it), BE and /RESET
  IN/OUT. The bus spy's driver list in [Boot](#boot) (the bus spy) is the decided one (/MEM, PS0–PS3, A0–A23,
  RWB), with BE and /OE.
- **The ID table** (Q13, amended in review round 2): IDs `0x0`–`0x5` are free; `0xF` is reserved, the idle bus's
  ID, never a card's; the RAM card, ROM A (on the CPU card) and ROM B answer by address and have no ID.
- **modules.md carries only the 98-pin bus** (Q11). The 62-pin table stays here alone, in
  [the 62-pin bus, the first design's](#the-62-pin-bus-the-first-designs), retitled from "the current machine's".
- **The stale mentions** (Q12; critic 4, 5, 6, 10, 13): memory's module IDs in [Memory](#memory),
  [Future: a 65C816 and 16 MB](#future-a-65c816-and-16-mb) and the module list; the sentences that said the pin
  map was not designed; the interrupt poll's line (D(ID mod 8), on D0–D7: only the DMA request poll uses D8–D15);
  modules.md's notes (+24 V, the ID lines' pin numbers, the level shifter, the clock and /RESET notes);
  [diag.md](diag.md)'s ID table; the Technical Reference's chapters 1, 2, 11 and 14. Chapter 2's 20-bit sentence
  waits for B7.
- **The row changed** (Q10). B6's row put "`hwname.s`'s names for them" with the ID table. They move to B7's row:
  B7 changes `hwname.s`, SYSINFO and the cards anyway, so the cards change once and B6 stays docs only. B7 also
  corrects [diag.md](diag.md)'s size for `HW_NAME` ("180 B of names"; 222 B of strings today, 158 B once IDs 0–5
  name "(free)"; 169 B as built in B7, with F's "(reserved)": [B7: as built](#b7-as-built)). B6's row is left as it was, the spec.
- **Review round 1** (the owner, 2026-11-17, "recommendations as listed"). The builder's choices were accepted:
  RDY, BE and /WAIT beside PHI2; the four-line rule on side A too, so VPB at A47; /BOOTSEL at A11, TC at A48 and
  /DREQ at B9; two spares. So were its extra fixes: the interrupt poll's wording in
  [the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane), the /MEM table, chapter 11's /OE
  row, and B7's row taking the `HW_NAME` size note and video-rev2.md's DRQ and /DACK lines. Fixed:
  - P1: the pull-up paragraph in modules.md claimed every line known in a handover, but A0–A23, RWB and PS0–PS3 had
    no pull-up, and with /MEM, /DMA and /IACK held high a floating PS0–PS3 is a register cycle. The claim is narrowed
    to what is pulled up, and PS0–PS3 (to `0xF`) and RWB are pulled up, in modules.md's rows and paragraph,
    [/MEM in each kind of cycle](#the-physical-map-on-the-new-bus), chapter 1 and Q9 above. The pull-ups' value has
    to be an ID that no card answers: `0xF`, while it stays free.
  - P2, P3: the BE row says that BE low floats the CPU (address, data, RWB) and the north bridge's address
    drivers, and that the DMA controller, the Console card or the bus spy then drives; the legend says the CPU card
    lets go of /WORD, /OE, /MEM and PS0–PS3 while another master holds BE, and that a byte-only master (the bus spy)
    may leave /WORD to its pull-up.
  - W1: the Technical Reference's chapter 11 (IRQL's bit table) and chapter 4 still called IDs 0–5 RAM and ROM:
    they are free IDs now.
  - W2: [Memory](#memory)'s reset-state table and [Boot](#boot)'s step 1 still gave ROM A module ID 4: they give
    its address, with ID 4 as the first design's.
  - W3: side B has room for 35 lines, not 34 (above).
- **Review round 2** (the owner, 2026-11-17, "recommendations as listed"). Fixed:
  - N2: the bus spy's override held /RESET alone, but reset floats neither the W65C02S nor the north bridge: only
    BE does. [Boot](#boot)'s override now holds /RESET and pulls BE low, and releases BE, then /RESET; the
    Technical Reference's chapter 3 says the same and names BE among the bus spy's needs.
  - F1, N1: the pull-ups make `0xF` the idle bus's ID, so Q13 is amended: `0xF` is reserved, never a card's. Six
    IDs are left for new cards (`0x0`–`0x5`) and at most 13 cards have one, in modules.md's ID table and its
    new-bus bullet, [the new bus](#the-new-bus-memory-by-address-dma-by-module-id-one-backplane),
    [one backplane](#one-backplane-the-slots-and-the-reserved-pins), [CPU and clocks](#cpu-and-clocks), the Technical
    Reference's chapters 1 and 2 and [diag.md](diag.md)'s ID table.
  - N3: pull-ups alone take many cycles to raise a released line on a loaded passive bus, so a new rule, as
    review round 3 left it: before the Console card or the bus spy releases BE, it drives /MEM and /OE inactive
    (high), RWB to read, PS0–PS3 to `0xF` and /WORD high if it drives /WORD, for at least one cycle, and the
    pull-ups then only hold lines already high; /IACK and /DMA stay the CPU card's. It is in modules.md's legend
    and pull-up paragraph, [/MEM in each kind of cycle](#the-physical-map-on-the-new-bus) and chapter 1. The
    emulator, which models the signals and not the pins, charges no cycle for it; B6 is docs only.
  - F2: chapter 2's I/O hole table gave IDs 0–5 to RAM and ROM; they are free IDs, no card.
- **Review round 3, the final check** (the owner, 2026-11-17, "recommendations as listed"). Fixed:
  - G1: round 2's rule had the releasing master drive /IACK and /DMA, which the CPU card drives throughout and the
    other masters have no drivers for, and /WORD, which the bus spy leaves to its pull-up. The rule names only the
    lines such a master drives: /MEM and /OE high, RWB to read, PS0–PS3 to `0xF`, and /WORD high if it drives it;
    /IACK and /DMA stay the CPU card's.
  - G2: the rule is for the masters off the CPU card, the Console card and the bus spy. The DMA controller's
    release needs no idle cycle, since the CPU card drives the lines again at once, so DMA's cycle costs are
    unchanged; the sentence that gave the rule the shape of DMA's takeover cycle is gone.
  - G3: taking the bus had the same gap: a CPU halted by RDY inside a write leaves /MEM and RWB low when BE floats
    it. So a take rule beside the release rule: in the cycle it pulls BE low, the master enables its own drivers,
    with /MEM and /OE high, RWB a read and PS0–PS3 `0xE`, its ID. It is in modules.md's legend and pull-up
    paragraph, [/MEM in each kind of cycle](#the-physical-map-on-the-new-bus), [Boot](#boot)'s override steps and
    chapter 1.
  - G4: EXAM and DEP are one bus cycle, then the idle cycle before BE is released, in
    [Front Panel](#front-panel) and the Technical Reference's chapter 12.
- **video-rev2.md** (frozen, not edited) still says DRQ 1 and /DACK, revision 1's wiring (its lines 21, 56, 113,
  167 and 236); they join B7's note there.
- **No expectation, test, card or version changed.** No site release of B6 (Q14): its books ride the next one.

#### B7: as built ####

**Proposed (2026-11-17).** Step B7, the full 1 MB, on `feat/bus-b7` off main `9cee9bc` (B1–B6 merged), as its plan
gave it with the owner's answers ("recommendations as listed": the critic's C1–C18 folded, questions 1–19 as
recommended). Nine units: the emulator's revision 2 bridge behind an option (U1), BIOS 0.17 (U2), the ROM (U3), the
machine at revision 2 (U4), OODOS's console and WIN1H (U5), `DM_BANK8` and interface 12 (U6), OOLIB's `GX_BANK8`
(U7), the WebUI (U8) and the books (U9).

- **The north bridge, revision 2** ([the video aperture moved out of RAM](#the-video-aperture-moved-out-of-ram)):
  WIN0H–WIN3H (`$20–$23`), VECH (`$24`) and MAP (`$25`, AP8 in bit 0) as the register table has them; WINnH and VECH
  keep bits 3–0 and MAP bit 0, the rest read 0; ID `$D2` and STATUS bits 4–7 2. Every reset, hard or the /RESET
  pulse, puts WINnH and VECH to 0 and MAP to `$01`, whatever is fitted. `route()` takes A20–A23 from WINnH, or VECH
  on a vector pull, while the ROM bit is clear; Apple II mode's branch never reads them. The redirect follows AP8
  live, for the windows, the DMA controller's memory side and `peek_physical()`; the Console card and the bus spy
  see the true map. The static `REVISION` and `ID` mean the default revision, now 2, and each bridge has `id()` and
  `revision()` (C5). `@version` 0.6.0 for the bridge, 0.15.0 for the decoder (C15).
- **A revision 1 bridge stays** as a machine-file option (Q8): `northbridge backplane [bootsel] [revision=1|2]`,
  default 2 from U4 (1 in U1–U3, so every unit stayed green); revision 1 is B1's model exactly (`$20–$25` read `$FF`,
  writes lost, ID `$D1`, the alias fixed at build by `set_aperture()`). Anything else is refused, the token named.
  The WebUI never sets it. It is what the `$D1` fallbacks' tests run on.
- **BIOS 0.17** (Q14). Every new path is gated on NB_ID `$D2`, so a `$D1` machine runs exactly as 0.16 did:
  - `diag.inc`'s inline `NBSET` (A kept) sets WIN0H–WIN3H and VECH to 0 and writes MAP whole, 1 with `VIDEO_ID`
    `$92` and else 0 (Q9), at BOOT2 (BOOT, MONBOOT, C800R and the diagnostics' trampolines come through it) and
    BREAK; not in PUTBACKM, since every way out of a diagnostics run reaches BOOT2 (C6). So AP8 is cleared at boot
    on a machine with no revision 2 card. START, on the RAM copy, clears WIN0H inline on `$D2` before it copies its
    stub into zero page through W0 and runs it there, since C800R reaches START's W0 before BOOT2 (review round 1,
    R4).
  - VPUTC maps W2 through WIN2H `$0E`, block 0 (`$E00000`) on `$D2`, NB_ID read on each call, and puts a program's
    WIN2H back; block 32 on `$D1`.
  - The diagnostics: GO writes MAP 0 on `$D2` whatever the card (Q6), so `DG_APER`, read from AP8, is 0 and stages
    2–6 test all 64 blocks; V4–V7 reach the aperture at `$E00000` (`APW1`, `W1HOME`). Stage 7 takes revision 2,
    `$D2`, exactly (Q7): a `$D1` bridge says `REVISION 1: NOT KNOWN TO THIS BUILD`, SKIP. NB9 is not added (Q16).
  - APPLE clears WIN0H–WIN2H (Q17); chapter 13's APPLE listing changes with it (C1, approved), and Apple II mode
    is otherwise unchanged.
  - `hwname.s` knows `$D2` (`HW_NB_ID2`), names IDs 0–5 "(free)" and F "(reserved)" (C11): `HW_NAME` is 45 B
    (13 B of code, 32 B of pointers) and 169 B of names, 222 B before ([diag.md](diag.md)).
- **OODOS** (stays 0.10, Q15; OOLIB's interface 11 → 12):
  - The console and `DM_VCOPY` by the new address on `$D2` (CRTINIT's CVH/CVB/CVA: `$0E`/0/`$E0`, or 0/32/`$08` on
    `$D1`); `TEXT_LO` checked against `VM_TEXT`.
  - WIN1H across the ways in (Q10): `STUB` saves it in ST_W1H (`$FE`), a `$FF` read (no register) kept as 0, and
    clears it before the stack-page map; `STUBRET` puts back only a value that isn't 0. `DISPATCH` pushes the WIN1H
    it read, clears it unless it is 0 or `$FF`, and puts it back on both exits, the no-card one too (C14). `SETHOOK`
    keeps it at `HK_W1H` (14), `CALLHOOK` restores it, and `HKBACK` clears it by testing the register, not the
    record (C3). Nothing is written to WIN1H on a `$D1` bridge.
  - The new COMMON helper `WINH0` (WIN0H–WIN3H and VECH 0, MAP whole by `VIDEO_ID`, on `$D2` only) from `WARM`,
    `NMI` and `ROMAPPLE`. `WARM` and `NMI` clear WIN0H inline first (WIN0H read as `$FF`, on `$D1`, writes nothing)
    and call `WINH0` only once W0 is on the system block, so its JSR never goes through a WIN0H left set or a
    read-only W0 (review round 1, R1). `WARM` also clears `BANK8`.
  - The refusal of a W1 address while WIN1H isn't 0 in `PHYSOF`, `PMAP`, function 73 (`F_PHYS`), `MRANGE` and
    `FT_OPENX` (Q11).
  - **`DM_BANK8`**, function 83's operation 11 (`DM_OPS` 12), as [above](#the-video-aperture-moved-out-of-ram), in
    block 61: refused with nothing changed on `$D1`, under a ROM BIOS before 0.17 (function 77's version, the major
    first) and with no block; otherwise AP8 cleared with a revision 2 card, `BANK8` set, +1/+2 `$0E`/1 or 0/0 with
    no card. `DM_INFO` stays 16 bytes; its W1 map takes in blocks 32–39 while `BANK8` is set. OSWIN lets bank 8
    through only while `BANK8` is set; `FT_OPENX` still refuses an event area there (C4).
  - SYSINFO reads MAP (under its exception, AGENTS.md) for its RAM line, with no new text (Q13, C18).
- **OOLIB (Q3, Q4, Q5):** `GX_INIT` is unchanged and always takes the old mode (block 33, AP8 left set). The new
  `GX_BANK8`, called after it, checks FT_INFO for 12 and `OA_DMA` and asks for `DM_BANK8`; a `$FF` refusal is
  `GE_VERSION` with the old mode kept, silently. After it GXPHYS maps slot 1 by WIN1H `$0E` and WIN1 1. The W1 cache
  is two bytes, `GXCURH:GXCUR`; `GX_BLOCK` refuses A ≥ 64 first and writes WIN1H 0 only when `GXCURH` isn't 0, as
  `GX_W1BACK` does; WIN1H is never written before `GX_BANK8` gives a window. `GXCURH`, `GXAPH` and `GXAPB` are in
  DATA, since the loader doesn't clear BSS. OO-Amp is unchanged (Q5), and must change its DMA-segment loader before
  it ever calls `GX_BANK8`.
- **The WebUI** (Q12, C12): the bank 8 breakpoint check reads NB_ID first; on `$D2` it refuses a bank 8 breakpoint
  while MAP's AP8 is set, and on any other NB_ID (`$D1`, or nothing answering) it keeps the `VIDEO_ID` rule; VV1's
  `recheck()` follows the same rule (VV1's rule changed, approved). The message's prefix is kept. Its 33 BIOS pins
  moved to 0.17 with the rebuilt WASM core (C10). The Devices tab's north bridge row is later (Q19).
- **The four questions of 2026-11-14** are recorded, dated, in [hw/oodos.md](oodos.md#the-oem-layer) and its phase 7 questions: DEF
  SEG `$8000–$8FFF` keeps the screen-page meaning, never bank 8 RAM (Q1); GW-BASIC refuses any access meeting
  `$80000–$8FFFF` until 7d, in 7c's tail, OODOS unchanged (Q2); `DM_BANK8` after the version and DMA checks, `$FF`
  the old mode silently, no card a success, and the no-card `GX_BLOCK` gap on 7c's tail (Q3); the two-byte cache and
  `GX_BLOCK`'s A < 64 (Q4).
- **The note in [video-rev2.md](video-rev2.md)** is inserted after its Status paragraph, exactly as approved (C9);
  nothing else in that file changed.
- **Room** (`oodos.map`; normal / DEBUG build), against `9cee9bc`'s:

  | Region | Free at `9cee9bc` | Free after B7 | The plan's estimate |
  |---|---|---|---|
  | COMMON | 166 / 156 | **50 / 40** (U5 53 / 43) | ~49 / ~39 (stop limit 32) |
  | RES region (`$C800–$F7FF`) | 219 / 16 | 219 / 16, unchanged | 219 / 16 (limit 16) |
  | BDOS (block 60) | 1,217 / 1,201 | 1,175 / 1,159 | ~1,170 / ~1,155 |
  | BIOS (block 61) | 2,164 | 2,020 | ~1,990 |
  | SYSBSS | 52 / 49 | 47 / 44 | 47 / 44 |
  | ROM block 31 | 5,147 | 5,029 | ~5,000 |
  | ROM block 22 (D22BSS) | 2,409 (4) | 2,389 (4) | ~2,330 (4) |
  | ROM block 23 | 7,917 | 7,843 | ~7,850 |
  | ROM block 21 | 7,896 | 7,896 | 7,896 |

  JUMPS at `$F800`, VECTORS at `$FFFA` and RES's symbols are main's. OOLIB per linked program: gxmem's CODE
  `$3FE` → `$499` (+155 bytes; the plan estimated about 50) and DATA 4 → 7; VIEW.COM 6,150 → 6,308 bytes, its top
  `$2100`, under `$4000`; the OOUI bank unchanged.
- **Cycles, measured** (the fast build, a `$D2` machine, U5 against U4): the dispatcher costs exactly **+17
  cycles** with WIN1H 0 (Q10). The ticks: no area 145 → 162, the MS-carry tick 159 → 176, layout 1 184 → 201,
  layout 2 234 → 249; the paddles started 367 → 382 and read 672 → 687; the USB key interrupt 662 → 679. The
  frames: no area 199 → 215, layout 2 396 → 412, a slot set but not its bit 518 → 534, a 512-byte slot 1,302 →
  1,318, the pointer 736 → 752, the whole budget 2,769 → 2,786. Every figure that moved by 15 or 16 rather than 17
  is page-crossing layout after the move (the owner's answer 1 at the U5 stop). Sound: channel 2's terminal count
  688/858 → 705/874; with the next piece inside it 4,656–4,959 → 4,672–5,405; the rare ones 1,269 (was 1,296); a
  frame with the stream claimed and a layout-2 area 698 → 715. The DEBUG build's hook counts: the tick hook's
  largest 474 (was 489; the bound 380–520 holds), the frame hook's 10,203 (was 10,135). The BDOS stub 142 cycles in
  and 98 out (`< 200` holds); FARCALL 281, unchanged. The latest line a slot ends: 500 with a USB batch, 520 under
  64 KB loads (was 503); the budget case 505 (19 to spare), under loads 502 (was 507). OSWIN gains 6 cycles a call
  (U6) and `GX_BLOCK`, already mapped, 7 → 16 (U7), both outside every timed bound. The books' figures are these
  measured ones (C8), never +17 by hand.
- **Run times:** the diagnostics, with all 64 blocks (Q6), a quick run 58.7 s (was 56.5) and a full run 171.8 s
  (was 165.6). test_bios_diag, then 923 s alone in the fast build, over its 900 s, was split in two ctest entries,
  `test_bios_diag_1of2` and `_2of2`, by `OOE_TEST_SHARD` as test_bios_sd is (the owner's answer 2 at the U4 stop);
  they take 528.6 s and 427.6 s, the TIMEOUT 900 s unchanged. The whole fast ctest about 530–545 s, 125 of 125.
- **Expectation changes:** only the plan's approved ones (§3 as folded, with §3.5's book listings), and the owner's
  answers at the stops: test_bios_sd's stopped-update case waits until neither flash chip is busy (U3);
  `break_inside_vputc_puts_w2_back`'s trigger by W2 0 with WIN2H `$0E` and WIN2H checked put back (U4); the
  measured group D figures, 249, 215 and 412 for the plan's 251, 216 and 413 (U5); test_oodos_pb waits for the CPU
  to be back in the program before its pokes (U5). U8 also dropped test_bios.cpp's unused `NB_VECH`, which the WASM
  build's clang refused.
- **New tests**, each seen failing under its mutant and the source restored: E1–E9 (the bridge, the machine
  option, the video card, Apple II mode, the session), R1–R11 (the ROM), O1–O9 and O11 (OODOS's WIN1H and new
  address), D1–D9 (`DM_BANK8`), L1–L8 (OOLIB, L8 C3's hook case), W1–W3 (the WebUI). `oodos/sys/test/w1htest.s` is
  a test-only program that writes WIN1H itself, with its build rule and its `tools/test-map.txt` rule.
- **Not in B7** (Q16, Q18, Q19 and the plan's list): NB9, the word pass and the D1 alias pass (a later diagnostics
  BIOS); WIDE in the ROM or OODOS; GW-BASIC's DEF SEG, PEEK, POKE, BLOAD, BSAVE and P5's seed (7c's tail); RAM past
  1 MB (WINnH `$01–$0D`, which reads `$FF`); OO-Amp's change; a site release. B7's books ride the next release,
  whose note gains: a kept user BIOS of 0.16 or older, on a machine with no revision 2 video card, leaves bank 8
  reading `$FF`: Use built-in.

#### The owner's answers (B0) ####

**Decided (the owner, 2026-11-14), "recommendations as listed",** and written in above:
1. **The Apple image** is chosen by an argument: `$C806` takes the first good image; the dispatching entry's Apple
   II function takes A (the entry is B5's, as LL5 foresaw: the table has no spare after `$C815`); function 75 gains A = 3 (A = 2 is
   the DIAG launcher's), with the interface version after 10; and the CCP's APPLE2 takes a number (read so in B5:
   there is no APPLE2.COM).
2. **The character sets** are in ROM A, one per image: 8 KB each in blocks 4–5 (review round 1, for the 8 KB erase).
3. **ROM B** is two 256 KB chips, as ROM A is.
4. **DMA revision 2's ID** is `$82`, and the checks take `$82` exactly (decided 2026-11-14, with the new layout),
   so a revision 1 controller reads as none ([the B2 migration](#the-b2-migration-revision-1-dropped)).
5. **The aperture** is reached by WINnH and VECH, with MAP's AP8 and `DM_BANK8`.
6. **The aperture's address** is `$E00000`.
7. **video-rev2.md's note** and the exception's read of MAP both come with B7.
8. **The serial card** selects its stream with DMASTREAM.

Review round 1's sixteen findings are fixed as recommended. They are in the sections above, each marked "review
round 1".

### The 62-pin bus, the first design's ###

The 62-pin bus's assignment, below, is the first design's; the new bus's draft pin-out is in
[modules.md](modules.md) (B6). The first design's pin-out before this assignment left 20 pins unassigned: A10, A11, B9, B11–B19 and B21–B28. This
design needs 18 new signals, which leaves 2 pins. Both go to ground on either side of PHI2.

**Assignment (decided).** New pins are in bold, existing pins are listed for context. This is the
table's only copy: since B6 modules.md carries the new bus's alone.

| Pin | Signal | Driven by | Type | Purpose |
|---|---|---|---|---|
| A2–A9 | D7–D0 | | | data, existing |
| **A10** | RWB | CPU, or the bus master | totem pole | read/write. Beside the data lines it qualifies |
| **A11** | /OE | north bridge | totem pole | read strobe (already in the pin-out before this assignment) |
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

**Power (decided; replaced 2026-11-14 by 5 V only on the new connector, above).** The 62-pin backplane carries +24 V (B29), and every card regulates it down to what it needs at the
point of use. At 24 V the bus carries a fifth of the current that 5 V would need for the same power.


## Front Panel ##

**Decided:** PDP-11 style, on its own **Console card** (module ID `0xE`), with a single switch register and live
lights. It works without the ESP32. This supersedes the switch list in [console.md](console.md).

**Proposed controls**, after the PDP-11/70:

- **Switch register:** 20 switches. LOAD ADRS takes all 20 as a physical address; DEP takes the low 8 as data.
- **LOAD ADRS, EXAM, DEP.** Repeated EXAM or DEP presses advance the address, to step through memory.
- **HALT/ENABLE, CONT,** and **START**: the hard reset, through the north bridge like the reset button, so it
  clears Apple mode.
- **S INST / S BUS CYCLE:** whether CONT, while halted, runs one instruction or one bus cycle.
- **Keyswitch, OFF / RUN / LOCK** (decided): OFF is power off; RUN is normal; LOCK does what CTRL bit 2 does, so the
  panel's keys and switches are ignored.
- **CONT after a halt** (decided): CONT clears a breakpoint halt and CTRL's HALT. With the HALT switch at ENABLE the
  CPU runs on; with it at HALT, CONT runs one step and stays halted. The STATUS hit bit stays set until a 1 is
  written to it, so software or the ESP32 can see that a breakpoint fired.

**Lights:**

- A0–A19: the 20-bit physical address, after the north bridge. It doesn't always match the CPU's address.
- D0–D7: the data bus (decided: 8 lights, not the 16 in [console.md](console.md)).
- Status: Power, Run, Halt, SYNC (fetch), RWB, VPB, RDY (wait), BE (DMA or panel), IRQ, AMODE.

**How the card works:**

- A small CPLD does everything that must be exact to the cycle.
- **Halt and step through RDY.** The W65C02S stops on any cycle when RDY is low, writes included, so single
  bus-cycle step comes free. Releasing RDY until SYNC goes high again gives single-instruction step. What the lights
  show at each step lines up with the emulator's bus trace.
- **EXAM and DEP** take the bus with BE and run one bus cycle at a 20-bit physical address, then the idle cycle
  before BE is released ([taking and giving back the bus](#the-physical-map-on-the-new-bus), B6), so they don't
  depend on the window registers.
- **Live lights.** The CPLD latches the address, data and status on every bus cycle, so the lights glow with real
  bus activity, as a running PDP-11's did, and freeze when halted.
- **Wiring.** Shift-register chains (74HC595 for lights, 74HC165 for switches) or LED drivers keep the card to the
  panel down to one ribbon cable.
- **EXAM and DEP work only while halted,** as on the PDP-11.
- **In Apple mode** the registers are hidden from software, but the panel still works: halting, stepping, EXAM, DEP
  and the lights are all hardware, so Apple software can be single-stepped.
- **The ESP32** plugs into a small SPI header on the card that exposes the same registers. It can halt, step and
  read the latched cycle even when the bus is hung, and mirror the panel over Wi-Fi or script stepping.

### Console registers ###

**Decided.** Module ID `0xE`, `$C700–$C77F`, offsets from `$C700`.

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$00–$02` | SWR | read | the 20-bit switch register, low byte first; programs can read it for boot options, as on the PDP-11 |
| `$03` | PANEL | read | bit 0 HALT/ENABLE switch; bit 1 S INST (0) or S BUS CYCLE (1) |
| `$04` | KEYS | read; write to press | bit 0 LOAD ADRS, bit 1 EXAM, bit 2 DEP, bit 3 CONT. Reading returns keys pressed since the last read and clears them; writing a bit presses that key (the ESP32's remote panel) |
| `$05` | DISPLAY | write | a byte shown on the data lights while CTRL bit 3 is set: the PDP-11's display register, for progress or error codes with no screen |
| `$06` | CTRL | read/write | bit 0 HALT: software asks to halt, like the switch. Bit 2 LOCK: panel keys and switches are ignored. Bit 3: data lights show DISPLAY instead of the bus |
| `$08–$0A` | BPADDR | read/write | breakpoint: 20-bit physical address. *On the 24-bit bus it matches only with A20–A23 0 ([B0](#the-physical-map-on-the-new-bus))* |
| `$0B` | BPCTRL | read/write | bit 0 enable; bit 1 match an opcode fetch (SYNC); bit 2 a read that is not a fetch; bit 3 a write; bit 4 action, 0 = halt with RDY, 1 = pull /NMI |
| `$0C` | STATUS | read | bit 0 halted; bit 1 breakpoint hit (write 1 to clear); bit 2 panel key pressed |
| `$10–$12` | LADDR | read | address of the cycle where the CPU stopped (halt or breakpoint). *On the 24-bit bus, A0–A19 alone* |
| `$13` | LDATA | read | that cycle's data |
| `$14` | LSTAT | read | that cycle's SYNC, RWB, VPB, BE and AMODE |
| `$15–$17` | PADDR | read/write | the panel's address: set by LOAD ADRS, advanced by EXAM and DEP |
| `$18` | IEN | read/write | bit 0: interrupt on a panel key press |
| `$19` | ISR | read; write 1 to clear | pending; pulls bit 6 of IRQH (ID E) low |
| `$7F` | ID | read | card type and revision |

The breakpoint comparator checks every bus cycle at no cost in speed. Halting suits the panel and the ESP32; /NMI
suits a debugger running on the machine itself, which then reads LADDR, LDATA and LSTAT to see the matching cycle.


## Emulator ##

The emulator ([emulator/](../emulator/)) can run this design before any hardware exists. Its device model
(machine files, decoder, timed devices, bus trace) carries over. The additions, roughly in order:

1. **The north bridge:** the registers at `$C680`, the native windows with ROM space and read-only bits, VEC, and
   module decode.
   This is `backplane.machine`, unblocked by this document.
2. **RDY:** wait states per module, and stopping the CPU for DMA. The core is bus-cycle accurate already; this
   adds the stall cycle.
3. **The DMA controller,** as a timed device with DRQ inputs from other devices. *(Revision 2 since B2: the wired
   cards' requests ORed into /DREQ, the poll, and the grant by MODULE.)*
4. **Video:** a snooping video device, starting with the text layer, drawn in a window or the terminal.
5. **Apple II mode:** the IIe switch logic, the mode register and the clock switch. Check it by running real IIe
   software.
6. **The Disk II controller,** with `.woz`, `.dsk`/`.do` and `.po` images (decided 2026-10-24; `.nib`, `.d13` and
   `.2mg` are in the emulator's backlog, [EMULATOR_PLAN.md](../emulator/docs/EMULATOR_PLAN.md#backlog)).
7. **The sound card:** the registers and mailbox first, then the card's own 6510 (with the NMOS undocumented
   opcodes) and the SID, with sound later. *(Emulated 2026-10-25: the SIDs' oscillators and envelopes, which
   OSC3 and ENV3 read back, then the output stage (waveform DACs, the filter, the mixer) and sound in the WebUI,
   which also plays `.sid` files on the card. Combined waveforms' real values are in the emulator's backlog.)*
8. **The mass storage card:** the registers, the raw card through DATA by CPU or DMA, then FAT mounting, the BIOS
   and the WebUI. *(Emulated 2026-10-25: the card, raw sectors and IDENTIFY; FAT, MOUNT, UNMOUNT and LIST; the
   BIOS's listing and boot; the card in the WebUI.)*
9. **The serial card's VIA and joysticks** ([above](#the-via-and-the-joystick-ports)). *(Emulated 2026-10-30: the
   W65C22S at `$C340–$C34F`, its IRQ on IRQL bit 6, and the two joysticks on PA0–PA4 and PB0–PB4, 0 when
   pressed, the rest pulled up. The TL16C552A isn't modelled yet, so a 6551 still stands in for the terminal at
   `$C300–$C303`, mirrored through the rest of the block as before; the VIA answers its 16 bytes over that
   mirror. INTSTAT isn't modelled: `$C322` is still the 6551's mirror, so for now a handler tells the VIA by its
   IFR. In the WebUI gamepads drive the sticks, each keeping its stick until it goes, and keys too once Settings turns them on; the
   terminal app's debugger has `joy`.)*
10. **Video card revision 2** ([above](#video-card-revision-2); proposed, 2026-11-10). *(Built: `VideoCard` owns
    2 MB when it is revision 2 and draws from it with 24-bit bases; the `aperture` line, `E00000-E0FFFF` since B1
    ([as built](#b1-as-built)), is a bare 64 KB memory device that forwards each access to the card (`$FF` and
    writes lost with no card), whose slot registers are the card's own (`$C4B0–$C4B7`); since B7
    ([as built](#b7-as-built)) the north bridge sends bank 8 to it while MAP's AP8 is set, from the CPU's windows and
    DMA alike, and bank 8 is RAM with AP8 clear (a `revision=1` bridge keeps B1's rule: with a revision 2 card only);
    the line is inert with a revision 1 card or none; the port (PADDR, PDATA, PSTEP, PCTL) has
    its DRQ on channel 1 (`drq=1`), as the sound card's RAMDATA is on channel 2 (since B2, a bare `drq` on each line
    and channel 1's CARD `$09`: [B2: as built](#b2-as-built)). The `video` line's `rev=` (1 or 2,
    1 if left out; `backplane.machine` has 2), or `MachineOptions::video_revision` over it, picks the revision,
    fixed once the machine is built. Apple mode still reads banks 0–1 of the RAM, keeping no copy of them, and takes
    the character ROM from card memory; /RESET leaves card memory alone. BIOS 0.13, OODOS (interface 8) and OOLIB
    drive revision 2 alone. The plan's `vram` debugger command, to peek and poke card memory, is its own step.)*

Each step can be checked against real software, the same way the Ben Eater programs checked the 6522 and 6551.


## Open Questions ##

1. **Bus masters and I/O.** The I/O hole is decoded from the CPU's address, before the windows, so a bus master
   driving physical addresses (the ESP32) has no way to reach card registers. The DMA controller doesn't need to
   (cards answer /DACK, or on the new bus their PS match with /DMA low) and the ESP32 reaches the Console card over SPI. If a master ever needs I/O, the simplest
   rule is a physical range the north bridge treats as the I/O hole while BE is low.
