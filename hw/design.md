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
     (1 MB)     (flash)    (4 ch)     (VGA)     (2x SID)  (Floppy Emu) storage     LPT          port      (ESP32)
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

**Slow cards** hold the CPU with RDY wait states. At ÷2 that's mainly the RP2350 cards and the serial card.

**Real-time clock (decided 2026-10-26): a DS3231 on the CPU card,** with a CR2032 to keep time with the power off.
OODOS's date stamps need it ([oodos.md](oodos.md#real-time-clock)). *(Proposed.)* Its I²C lines go to the north
bridge, on the same card: the RTC register at `$C687`. Its SQW output, at 1 Hz, can interrupt as the north bridge's
ID `0xD`.


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
| `$07` | RTC | read/write | *(proposed)* the DS3231's I²C, driven by software: bit 0 SCL, bit 1 SDA, both open drain (1 = released); reading bit 1 gives SDA | `$03` |
| `$08` | IRQL | read | interrupt poll for IDs 0–7, using /IACK. 0 = interrupting | |
| `$09` | IRQH | read | interrupt poll for IDs 8–F | |
| `$0A`–`$0D` | WAIT0–WAIT3 | read/write | wait states the north bridge inserts for each module ID, 2 bits per ID: WAITn holds IDs 4n to 4n+3, the lowest ID in bits 1–0. Reserved for turbo; unused at ÷2 | `$00` |
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
  video ROM's first 2 KB: 8 bytes a code, bit 0 the leftmost of 7 dots, a dot lit where its bit is 0.
- **Apple colours:** the 16 lo-res colours (and hi-res's violet, green, blue, orange and white among them) are the
  usual NTSC approximations, rounded to the card's 12 bits. Hi-res colour is the simple artifact rule: a lone dot
  takes its column's colour, two together are white, and a gap between two dots of one colour takes it; no NTSC
  filter. The double modes follow the same phase rule (dot x of 560 is bit x mod 4 of a lo-res colour): double hi-res
  colours each aligned 4 dots by their pattern; double lo-res shows aux nibbles rotated right a bit.

This is too much for a 74HC670 register file, which would also leave no room for a turbo mode. The north bridge
is a CPLD, for example an ATF1508 (5 V), holding:

- the native window registers;
- the Apple switch logic;
- the module decode;
- the mode register.

### Module decode ###

The north bridge works in two stages:

1. CPU address to physical address, using the windows or the Apple rules, with the I/O hole decoded first. Only
   the CPU goes through this.
2. Physical address to module ID (PS0–PS3). Everything goes through this, including DMA.

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
7. *(Emulator BIOS 0.3.)* The BIOS turns on an 80x30 text screen: an 8x16 font from flash block 30, a 16-colour
   palette, and layer 0 in video memory, so the monitor shows on VGA as well as the serial line. It starts with
   fixed entry points: `$C800` reset, `$C803` a video demo in flash block 29. *(BIOS 0.4.)* The monitor takes keys
   from the serial line or the USB keyboard, and Pause/Break breaks into it. *(BIOS 0.5.)* `$C806` enters Apple II
   mode, with the IIe ROM image taken from the cartridge (ROM B). *(BIOS 0.7, proposed.)* The SD card: `$C809`
   lists the card's root directory, and `$C80C` boots from it. The boot mounts `/BOOT.BIN` as unit 1, reads it to
   `$1000` by DMA channel 0 (at most 88 sectors, `$1000–$BFFF`) and calls it, so a program that returns comes back
   to the monitor. `$C80F` is ECHO, for programs to print with. *(BIOS 0.8.)* The boot puts windows 0–2 back on
   RAM blocks 0–2 first, so the CPU's `$1000` is where the DMA wrote, and a failed command ends with its error
   rather than a hang. *(BIOS 0.9.)* Break (NMI) and BRK enter the monitor with a fresh stack, windows 0–2 on RAM
   blocks 0–2 and the video card's registers back on the text screen, whatever was running (the demo, a program,
   the screen code itself); other IRQs still return at once. `$C800` goes back to the flash and starts from its
   reset vector, as a hard reset does, so it copies the flash again (unless the machine booted from RAM through
   /BOOTSEL). The BIOS's memory-to-memory copies are block transfers that it waits for, so they're whole whatever
   BURST a program has set. Apple II mode also clears the language card's second `$D000` bank (banks 4–5). The
   zero page the BIOS and the monitor keep is listed in `backplane/bios/backplane.inc`: ECHO (`$C80F`) uses
   `$E0–$E7`.

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
  | `$DE00` | the mailbox: read takes the host's byte, write sends one to the host; `$DE01` is its status (bit 0: a byte from the host is waiting; bit 1: the card's last byte hasn't been taken) |

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

**Details settled in the emulator (2026-10-25, proposed for the card):**

- **SID reads.** The card answers the 25 write-only registers itself, with what was last written to each, so
  "reads always work" holds for every register; `$19–$1C` come from the chip.
- **The mailbox** is one byte each way. A second MBOUT before the card has taken the first replaces it, and the
  same for the card's byte; STATUS says when a box is full.
- **RAMDATA** is always ready, so DRQ stays asserted; `backplane.machine` wires it to DMA channel 2.
- **/RESET** stops the card's 6510 (CTRL is cleared), pulls the SIDs' /RES, and empties the mailbox. The card's
  RAM keeps its contents.

**`.sid` playback.**

1. The host stops the card (RUN = 0) and loads the tune's data at its load address through RAMDATA.
2. It writes a small stub that calls init with the song number, then calls play on each CIA timer interrupt, or
   at 50 or 60 Hz for tunes that use the vertical blank.
3. It sets the clock from the file's PAL or NTSC flag and sets RUN.
4. The main CPU is free; the mailbox tells it when to change song or stop.

**Details settled in the emulator (2026-10-25, proposed for the host's player):** `sid_file.h`.

- **The stub** goes on a free page: the one the header names (from `$0400` up), else `$FF00`, `$FE00`, `$0400`, `$C000`
  or `$0200`, the first the tune leaves alone. It sets `$00`/`$01` to `$2F`/`$37`, programs CIA 1's
  timer A (19,656 clocks for a PAL frame, 17,095 for NTSC; the KERNAL's `$4025`/`$4295` for a CIA
  song or an RSID) with its interrupt on, banks `$01` for init's address, calls init with the song less one in
  A, and waits in a `JMP` loop.
- **`$01` for a routine** follows PSID players: `$37` below `$A000`, `$36` below `$D000`, `$34` in
  `$D000-$DFFF`, `$35` above. On the card only `$34` differs (there are no ROMs), so I/O comes back
  after an init under it.
- **The KERNAL's interrupt path** is imitated: `$FFFE` pushes A, X and Y and jumps through `$0314`,
  and `$EA31`, `$EA7E`, `$EA81` and `$FEBC` acknowledge the CIA and return, wherever the tune
  leaves them free, so tunes that install their own interrupt at `$0314` or `$FFFE` run. A tune with
  a play routine that loads over `$0314` goes straight to the stub's handler instead.
- **A sync point:** after each play call the stub writes the call's number (mod 256) to the
  mailbox, so the host can count frames.
- **A mono tune** plays on both SIDs (CTRL BOTH); a second SID at `$D420` or `$D500` sets CTRL bit 3
  to match; a third SID, or a second elsewhere, is beyond the card, and its writes land on SID A's
  mirror.


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
    - The emulator doesn't do this yet: in native mode it still answers only the ID byte
      ([EMULATOR_PLAN.md](../emulator/docs/EMULATOR_PLAN.md#backlog)).

**Decided (2026-10-24), as the emulator models it** (`emulator/components/devices/disk2_card.h`):

- **ID byte** `$C1` at `$C67F` (type C, revision 1).
- **The sequencer** steps at 2 MHz, two steps a CPU cycle (the Apple's long cycle every 65th is left out, 0.2%,
  well inside a drive's speed tolerance). The PROM is read as the 341-0028 is wired, found by running the owner's
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
| `$00` | CTRL | read/write | bit 0 DMA: assert DRQ0 when DATA has a byte, or room for one. Bit 1 RESET: start the SD card up again |
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
  sector, so demand mode pauses and resumes. The DMA terminal count or the command-done interrupt ends it. The
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
  With CTRL.DMA set, DRQ0 follows it. When DATA is clear, reading DATA returns `$FF` and a write to it is dropped.
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

1. The north bridge switches to the Apple mapping, and the clock card switches to the Apple clock.
2. The DMA controller goes idle and the video card switches to Apple modes.
3. The north bridge pulses /RESET.
4. The CPU starts from the IIe ROM image's reset vector in bank E, as a IIe does at power-on.

Before the switch, the BIOS has loaded the ROM image into bank E and cleared banks 0–3 and the second `$D000` bank
(`$D000–$DFFF` of banks 4 and 5).

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
| Speaker (`$C030`) | USB card (proposed), or the sound card |
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

**Proposed: the USB card**

- **USB host:** an RP2350 running TinyUSB. Its PIO answers the bus, holding RDY when it needs time, the same pattern
  as the sound and SD cards.
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
| `$04` | EVENT | read | raw events, two bytes each: the USB HID usage code, then flags (bit 7 = release). Modifiers arrive as keys too (`$E0–$E7`). Asserts DRQ while not empty |
| `$05` | LEDS | write | bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock |
| `$06` | REPEAT | read/write | for KEY: bits 0–3 rate, bits 4–5 delay |
| `$07` | KBCFG | read/write | bits 0–3 layout (0 = US). Bit 7: Pause/Break pulls /NMI, to break into the Monitor |

Raw codes are USB HID usages rather than PC scancodes: one byte, what the keyboard actually sends, no prefixes.

**Decided (2026-10-24), filling in what the tables leave open,** as the emulator does it:

- KEY holds one character, as on the Apple: a new one replaces it, read or not. It follows the Apple keyboard's
  conventions in native mode too (arrows `$08` `$15` `$0A` `$0B`, Delete `$7F`, Ctrl with `$40-$5F` or a letter
  giving `$00-$1F`). Caps Lock acts on letters only, Shift undoes it, and it is **on at power-up**, as a IIe is
  usually left.
- REPEAT: the last character key pressed repeats while held, after (bits 4-5 + 1) x 250 ms, every (bits 0-3 + 1) x
  33 ms (30 to about 2 a second). It resets to `$10`: 500 ms, 30 a second.
- The raw event FIFO holds 128 bytes (64 events); when full, a new event is dropped whole, so pairs never split.
- Each paddle's GAME bit stays high for about 11 us per count after PTRIG, in real time, as on the Apple.
- An event sets its ISR bit whether or not IEN enables it; the card's IRQ is ISR AND IEN.
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

**Proposed: the serial and parallel card** (module ID 6, "Serial A/B & Parallel")

- TI's TL16C552A: two 16550-compatible UARTs with 16-byte FIFOs and an IEEE 1284 bidirectional parallel port, in
  one chip. It is the classic PC combination for COM1, COM2 and LPT1.
- **DMA:** the UARTs' TXRDY/RXRDY outputs drive DRQ lines; the parallel port gets DMA from the card's AUTOSTROBE.
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
| `$C37F` | ID: card type and revision |

| Offset | Name | Access | Purpose |
|---|---|---|---|
| `$20` | DMASEL1 | read/write | the source on DRQ1: 0 none, 1 COM1 receive, 2 COM1 transmit, 3 COM2 receive, 4 COM2 transmit, 5 LPT output |
| `$21` | DMASEL3 | read/write | the same for DRQ3, so a second stream can use the general-purpose channel |
| `$22` | INTSTAT | read | bit 0 COM1, bit 1 COM2, bit 2 LPT: which part is interrupting. Any of them pulls bit 6 of IRQL (ID 6) low. The enables stay in the chip's IER and LPT control registers |
| `$23` | LPTCTL | read/write | bit 0 AUTOSTROBE: in DMA mode the card generates STROBE itself |

- **DMA routing.** On /DACK1 or /DACK3 the CPLD selects the chosen source's data register (chip select, A0–A2 = 0,
  /IOR or /IOW by direction), so the UART needs no address, as fly-by requires. With FCR bit 3 set (DMA mode 1),
  receive requests at the FIFO trigger level or timeout and transmit requests while the FIFO has room; both suit
  demand mode. The classic receive arrangement is channel 1 in AUTO mode as a ring buffer, the OS reading the
  channel's current address to see how much has arrived.
- **AUTOSTROBE.** The 552A's parallel port has no DMA request and only strobes when software writes its control
  register. With AUTOSTROBE set, the card asserts DRQ while the printer isn't BUSY, and after each /DACK write holds
  the data, pulses STROBE and waits for ACK, keeping the datasheet's 1 µs setup, pulse and hold.
- **Bus timing** (TI datasheet SLLS189D). /IOR and /IOW must be low for at least 80 ns, longer than PHI2's ~70 ns
  high time at ÷2, so **every access takes one RDY wait state**. The chip also needs 80 ns between strobes, so the
  CPLD adds a second wait state when accesses come back to back (DMA bursts, read-modify-write instructions). In
  FIFO mode the chip needs 425 ns between reading the receive FIFO and reading IIR or LSR; drivers must allow for it,
  and at serial speeds it costs nothing.
- **Apple mode:** the block is hidden. A stock IIe has no serial card; a Super Serial Card emulation in slot 2 would
  be a later extra.

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
- **EXAM and DEP** take the bus with BE and run one bus cycle at a 20-bit physical address, so they don't depend on
  the window registers.
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
| `$08–$0A` | BPADDR | read/write | breakpoint: 20-bit physical address |
| `$0B` | BPCTRL | read/write | bit 0 enable; bit 1 match an opcode fetch (SYNC); bit 2 a read that is not a fetch; bit 3 a write; bit 4 action, 0 = halt with RDY, 1 = pull /NMI |
| `$0C` | STATUS | read | bit 0 halted; bit 1 breakpoint hit (write 1 to clear); bit 2 panel key pressed |
| `$10–$12` | LADDR | read | address of the cycle where the CPU stopped (halt or breakpoint) |
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
3. **The DMA controller,** as a timed device with DRQ inputs from other devices.
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

Each step can be checked against real software, the same way the Ben Eater programs checked the 6522 and 6551.


## Open Questions ##

1. **Bus masters and I/O.** The I/O hole is decoded from the CPU's address, before the windows, so a bus master
   driving physical addresses (the ESP32) has no way to reach card registers. The DMA controller doesn't need to
   (cards answer /DACK) and the ESP32 reaches the Console card over SPI. If a master ever needs I/O, the simplest
   rule is a physical range the north bridge treats as the I/O hole while BE is low.
