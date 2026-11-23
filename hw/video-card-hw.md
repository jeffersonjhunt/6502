# OO-1 Video Card — the Hardware

Nov 20, 2026 · @Jefferson

**Status: draft (2026-11-20).** The owner's answers of 2026-11-20 are decided: a BGA part is fine, no audio, a
video DAC, the same FPGA family for OO-AP, no upscaler, the Si5351A fitted, and **card memory in SDRAM** (which
replaces the two-SRAM plan of the same day). The rest is proposed, owner to confirm. This file is the physical card:
the FPGA, the memory, the bus interface and the display outputs. It answers [video-rev2.md](video-rev2.md)'s open
question 7 (the FPGA part) and adds what that file leaves out. The register map stands. One decision of
video-rev2.md's changes: card memory is SDRAM, not asynchronous SRAM, and MEMSIZE reads 128 (8 MB) instead of 32
([below](#card-memory-sdram)); aperture reads keep their zero wait states.

## Summary

| Topic | Recommendation |
| --- | --- |
| FPGA | Lattice ECP5 **LFE5U-25F-6BG256C**, on a Muse Lab **iCESugar-Pro** module (DDR2 SODIMM-200, 106 user I/O) |
| Card | The module in a socket on an **OO carrier**: the 98-contact edge, level shifters, clocks, DVI and VGA. A chip-down card with the same parts is the alternative |
| Toolchain | Yosys, nextpnr-ecp5 and Project Trellis, all open, so the build runs in the same container as the emulator; Verilator checks the RTL against the emulator's golden frames |
| Card memory | **SDRAM** (decided, the owner, 2026-11-20): the module's IS42S16160B, 32 MB, 16 bits, at 100.7 MHz. Software sees 8 MB (MEMSIZE 128) |
| CPU access | An **Apple II-style interleave**: a window of every CPU cycle is kept for the CPU, so aperture reads have no wait states |
| Bandwidth | About 115 MB/s for scanout, about 227 burst fetches a line: enough for every 640×480 mode |
| Pixel clock | A **Si5351A** clock synthesizer, fitted (decided, the owner, 2026-11-20), beside the 25.175 MHz oscillator |
| On-chip memory | Block RAM shadows of the palette, the sprite table and the font, kept coherent by the card, plus line buffers and the splash ROM |
| Bus interface | 74LVC16T245 dual-supply transceivers for the data lanes, 74LVC245 (5 V-tolerant inputs) for address and control, 74LVC07A for the open-drain lines and the two poll bits |
| Outputs | **VGA** (DE-15) and **DVI over an HDMI connector**, both at 640×480 at 60 Hz, from one 24-bit pixel bus |
| Digital transmitter | TI **TFP410** (DVI, video only; no audio, decided, the owner, 2026-11-20) |
| VGA DAC | The **ADV7123** (10 bits, 330 MHz), decided, the owner, 2026-11-20; Analog Devices no longer recommends the 8-bit ADV7125 for new designs |
| Other timings | None on OO-1, and no upscaler (decided, the owner, 2026-11-20). The card can drive up to 720p-class modes later ([headroom](#headroom-and-future-proofing)) |
| Discrete chips | Not as a replacement. Only a cut-down text and mono card is practical in discrete parts or 5 V CPLDs |

## What the card has to do

These are the demands that size the parts. Each comes from a decision already in the record.

- **Be a bus target with no wait states at ÷2.** Aperture reads (`$E00000–$E0FFFF`) and register reads must
  return data within the read budget of [design.md](design.md#wait-states-and-the-read-budget). Register reads have
  about 45 ns to spare. An aperture read must reach card memory and come back in the same cycle.
- **Snoop in Apple II mode.** It catches every write to banks 0–1 and every access to the soft switches, one bus
  cycle at a time (978 ns in Apple timing, so this is the easy case).
- **Serve the data port by DMA.** It answers a grant naming module 9 with a byte, or a word with WIDE, in one bus
  cycle at 7.16 MHz.
- **Scan out 640×480 at 60 Hz** (25.175 MHz) with two layers, 64 sprites (32 per line) and the palette, all
  read from card memory, plus the Apple modes with NTSC artifact colour.
- **Play the boot splash** from a 32 KB ROM of its own, with the running-sum blur and the zoom.
- **Leave room for draw commands** that OO-AP will feed: Wolfenstein 3D's scaled column first, then DOOM's
  textured columns and spans with colormap lighting ([ooap-design.md](expansion/ooap-design.md)).
- **Answer the interrupt poll on D1 and the DMA request poll on D9,** pulling one data bit and no others (ID 9:
  D(ID mod 8) for /IACK, D(ID) for the request poll).

## Card memory: SDRAM

### The choice

| Kind | Example | For the OO-1 | Against | Verdict |
| --- | --- | --- | --- | --- |
| SDR SDRAM | IS42S16160B, W9825G6KH: 32 MB, 16 bits, TSOP-54 | Already on the iCESugar-Pro, off its 106 edge pins; a few dollars as a chip; 32 MB; bursts suit scanout | Row opens, CAS latency and refresh; a harder controller; random reads take 60 ns or more | **Use** (decided, the owner, 2026-11-20), with the interleave below for zero-wait aperture reads |
| Async SRAM | IS61WV102416 / 204816, 10 ns, TSOP-48 | No refresh, a fixed 20 ns access, a simple arbiter; about 200 MB/s on 32 bits | $15–50 for 2–8 MB, and 58 FPGA pins for a 32-bit bus, more than the module has | Was the plan earlier on 2026-11-20; replaced |
| HyperRAM / OPI PSRAM | 8 MB, x8 DDR, BGA-24 | 12 pins, 333–400 MB/s streamed | Latency like SDRAM's, and BGA; QSPI PSRAM in SOIC-8 is too slow (about 66 MB/s) | No |
| Dual-port SRAM | IDT70V-class | CPU on one port and scanout on the other, no arbiter | Expensive, small (a few Mbit), and the FPGA arbitrates for free | No |
| VRAM (1980s sense) | TMS4461, MT42C4256: DRAM with a serial shift-out port | The classic answer to scanout against CPU access | Out of production since the late 1990s, NOS only. The interleave and the line buffers below do its job | No; the interleave and line buffers *are* the VRAM |
| FPGA block RAM | ECP5-25F: 1,008 Kbit (126 KB) | Single-cycle and dual-ported | Far too small for card memory | Use for shadows, line buffers and the splash |

### The part

- **The module's SDRAM:** ISSI IS42S16160B, 4 banks × 8,192 rows × 512 columns × 16 bits, 32 MB. It is wired to
  the FPGA on the module, so it uses none of the 106 edge pins.
- **Clock:** 100.7 MHz (the pixel clock × 4, from the PLL), CAS latency 2, bursts of 8 words. The module's speed
  grade and its routing are to be checked at 100 MHz on the bench ([the prototype](#the-prototype)); if they only
  hold about 50 MHz, the bandwidth below halves and this section is revisited.
- **Refresh:** 8,192 rows every 64 ms, one AUTO REFRESH every 7.8 µs, about four a scan line, each in the
  scanout part of a CPU cycle.
- **What software sees: 8 MB, MEMSIZE 128.** Card addresses are 24 bits and MEMSIZE is one byte in 64 KB units, so
  16 MB less 64 KB is the most the register map can name, and sprite images reach exactly 8 MB. 8 MB matches that
  reach. The other 24 MB is the card's own, unseen by software, for later features (a copy engine's scratch, an
  OO-2 mode).
- **Card memory layout.** video-rev2.md's frozen layout stands: OODOS owns `$000000–$03FFFF`, Apple II mode
  `$1C0000–$1FFFFF`. Programs get `$040000–$1BFFFF` as before and, new, `$200000–$7FFFFF` (6 MB). OOLIB's allocator
  serves both ranges (proposed).

### The interleave: zero-wait aperture reads

An SDRAM read on an idle bank takes about 60 ns at 100.7 MHz: ACTIVE, two clocks, READ, two clocks of CAS latency
and the capture. If a scanout burst or a refresh were under way, a CPU read could wait 150–200 ns, more than a whole
CPU cycle (139.7 ns). So the card does what the Apple II did with its DRAM: it shares memory by time, in step with
the CPU.

- **The CPU's window.** Each CPU cycle is about 14 card clocks. The controller follows PHI2 (sampled each clock, not
  counted, since 100.7 / 7.16 isn't a whole number) and keeps the SDRAM idle for about 6 clocks from the moment the
  CPU's address is valid, about 30 ns into the cycle.
- **A read in the window** issues ACTIVE and READ at once and has its data about 60 ns later, about 100 ns into the
  cycle; through the transceiver it is on the bus with about 20 ns to spare before the cycle ends. No /WAIT.
- **Writes** (aperture, port, Apple snoop) are posted into a small FIFO and done in the next window, so they never
  wait either.
- **The port** reads ahead: PADDR moves in order, so the card fetches the next bytes into block RAM before DMA asks
  for them.
- **When the cycle isn't the card's,** found once the address decodes (about 40 ns in), the rest of the window goes
  back to scanout.
- **The other ~8 clocks** of each CPU cycle carry one 8-word scanout burst, or a refresh.

The window costs bandwidth: one burst per CPU cycle is 16 bytes every 139.7 ns, **about 115 MB/s**. Without the
interleave (aperture reads with /WAIT instead), the same SDRAM would give about 170 MB/s; a later high-resolution
mode could make that trade ([headroom](#headroom-and-future-proofing)).

### The bandwidth budget

Scanout renders line *n* + 1 into a line buffer while line *n* is shown, so every fetch for a line has the whole
line period, 31.78 µs. That is **227 CPU cycles, so 227 fetches a line**, each one burst of up to 8 words from one
row. Small scattered fetches (a tile row, a sprite row) cost a whole fetch, so the budget counts fetches, not bytes.

| What | Fetches a line | Note |
| --- | --- | --- |
| Text 80×30 | 10 | The cells only; the font comes from its block RAM shadow |
| Tile, 8×8, 4 or 8 bpp | 47 | 41 tile rows and 6 bursts of map entries (320 wide, scrolled) |
| Bitmap 256 or 512 | 16 | One 256-byte row |
| Mono | 5 | |
| Type 7, 640×480 RGB444 | 80 | Two bytes a pixel |
| 32 sprites, 16 px, 4 bpp | 32 | Typical; one fetch a sprite row; the table comes from its shadow |
| 32 sprites, 64 px, 8 bpp | 128 | The worst case |
| CPU and DMA | 0 | They have their own window |

- **Fits:** two type 7 layers (160 of 227); type 7, a tile layer and typical sprites (159); every Apple mode.
- **Doesn't fit:** the worst-case sprites beside type 7 and a tile layer (255). The card drops the pixels of the
  last sprites on such a line, as VERA does when a line runs out of time.
- **Draw commands** get what scanout leaves: about 60–200 fetches a line, depending on the layers shown. A
  scaled-column Wolfenstein view at 60 fps needs about 30.

### Shadows in block RAM

All writes to card memory pass through the card (aperture, port, Apple mirror), so the card can keep exact copies
of small, hot structures in block RAM at no cost in fetches:

- **The palette** (512 bytes): reloaded from PALBASE when PALBASE latches at line 480; any write that lands in its
  range is written to the shadow too. A palette change still shows from the next line, which is what video-rev2.md's
  answer 5 promises ("in card memory, read each line"), without spending 32 fetches on every line.
- **The sprite table** (512 bytes, SPRBASE): the same.
- **The font** (4 KB, the text layer's TILEBASE): the same, reloaded during vertical blank.

This is invisible to software and in the emulator, which reads card memory directly.

## The FPGA

### The requirements

- **I/O on the module's edge (106):**

  | Group | Pins |
  | --- | --- |
  | Backplane, through the shifters: A0–A23, D0–D15, PS0–PS3, RWB, /OE, /WORD, /MEM, PHI2, /RESET, /IACK, /DMA, AMODE, TC | 54 |
  | Open-drain outputs: /IRQ, /DREQ, /WAIT, the D1 and D9 poll bits | 5 |
  | Data transceiver direction and enable | 4 |
  | Pixel bus: 24 bits, DE, HSYNC, VSYNC, pixel clock | 28 |
  | I²C (Si5351A) | 2 |
  | Clock inputs: the 25.175 MHz oscillator, the Si5351A's CLK0 | 2 |
  | LEDs | 2 |
  | **Total** | **97, leaving 9** |

  The SDRAM, the configuration flash and the USB debugger are on the module, off the edge pins.
- **Block RAM:** the splash ROM 32 KB, the font shadow 4 KB, the palette and sprite shadows 1 KB, line buffers and
  the sprite line buffer about 4 KB, the write FIFO and port read-ahead under 1 KB: **about 42 KB** of 126.
- **Logic (an estimate):** VERA fits in an iCE40UP5K's 5,280 LUTs. This card adds the Apple renderer with NTSC
  artifact colour, the splash, the aperture, port and the SDRAM controller with its interleave: about **9–13 k
  LUT4**. Draw commands for Wolfenstein 3D and DOOM add perhaps 3–6 k.
- **Speed:** 100.7 MHz for the memory side and the pixel pipeline, 25.175 MHz for pixels.
- **Open tools,** so the bitstream builds in CI next to the emulator.

### The candidates

| Part | LUT4 | Block RAM | Multipliers | Package, user I/O | Tools | Fit |
| --- | --- | --- | --- | --- | --- | --- |
| iCE40UP5K (VERA's) | 5.3 k | 15 KB + 128 KB SPRAM | 8 | SG48, 39 | Open | Too few pins for external memory; VERA's memory is the 128 KB inside it |
| iCE40HX8K | 7.7 k | 16 KB | 0 | TQ144, about 107 | Open | Too little block RAM (the splash alone is 32 KB) |
| **ECP5 LFE5U-25F** | 24 k | 126 KB | 28 | caBGA-256, 197 (106 on the iCESugar-Pro's edge) | **Open (Trellis)** | **Chosen** |
| ECP5 LFE5U-45F | 44 k | 243 KB | 72 | caBGA-256, 197 | Open | Headroom on a chip-down card; OO-AP's choice if it wants the multipliers |
| Gowin GW2A-18 | 20.7 k | 104 KB | 48 | EQ144 (QFP) or PG256 | Gowin EDA (free licence); Apicula supports the PG256 | The best non-BGA choice; not needed |
| Xilinx Spartan-7 XC7S25 | 14.6 k LUT6 | about 200 KB | 80 | BGA only | Vivado | Strong, but a closed, heavy toolchain |

**Why the ECP5.** It is the only part with plenty of pins, block RAM and speed together with a mature, fully open
toolchain: Yosys, nextpnr-ecp5 and Project Trellis run in the docker container that already builds the emulator.

**BGA (decided, the owner, 2026-11-20): acceptable.** On the module the BGA is already soldered; a chip-down card
needs a 4-layer board and reflow (JLCPCB and PCBWay assemble the caBGA-256 from stock).

**OO-AP (decided, the owner, 2026-11-20): the same family.** One toolchain, one set of level shifters and one
layout style for both cards; OO-AP can take the 45F for its 72 multipliers.

### The RTL and the emulator

The emulator's `VideoCard` (C++) is the reference, and `test_video` already compares it with `splashgen.py` pixel
for pixel. The RTL gets the same treatment: a Verilator build of the FPGA design, driven by bus-cycle traces
recorded from the emulator, must produce the same frames, with an SDRAM model in the testbench. The golden frames
that exist today then cover both. The emulator changes only MEMSIZE (128); aperture timing is unchanged.

## The bus interface (5 V)

The bus is 5 V only ([modules.md](modules.md)); the FPGA's banks are 3.3 V.

| Lines | Direction | Part |
| --- | --- | --- |
| D0–D15 | Both | 1 × 74LVC16T245, dual supply (true 5 V levels out), a direction pin each byte lane, enabled only for the card's own read cycles |
| A0–A23, PS0–PS3, RWB, /OE, /WORD, /MEM, PHI2, /RESET, /IACK, /DMA, AMODE, TC | In | 74LVC245 at 3.3 V: its inputs take 5 V; 38 lines, 5 parts (or 3 × 74LVC16245) |
| /IRQ, /DREQ, /WAIT | Out, open drain | 74LVC07A: open-drain outputs that tolerate the bus's 5 V pull-ups |
| D1 (interrupt poll), D9 (request poll) | Out, open drain | Two more gates of the 74LVC07A, wired straight to the bus lines, beside the transceivers |

- **The polls need their own drivers.** In a poll, other cards pull other bits of the same lanes. If the card's
  data transceivers were turned on, they would drive all 8 or 16 bits and fight them. So during /IACK and request
  polls the transceivers stay off, and only the open-drain gate on D1 or D9 pulls.
- **Through a stretched cycle** (/WAIT, used only in turbo, if ever) the transceivers stay enabled from the first
  PHI2 rise to the final fall, as design.md requires.
- **Sampling.** The FPGA samples PHI2 and the bus with its 100.7 MHz clock, about 14 samples a CPU cycle at
  7.16 MHz. PHI2's edges place the interleave's CPU window.
- **Turbo (14.3 MHz), if ever:** a CPU cycle is about 7 card clocks, too short for a window and a burst both. Aperture
  reads would then use /WAIT, as design.md already expects of memory at that speed.

## Display outputs

### The standards

| Standard | Timing | Pixel clock | On OO-1 |
| --- | --- | --- | --- |
| VGA, 640×480 at 60 Hz | 800 × 525 | 25.175 MHz | **Yes: the only timing**, as decided |
| SVGA, 800×600 at 60 Hz | 1,056 × 628 | 40 MHz | No: 640×480 content doesn't scale to it evenly, and no mode needs it |
| XGA, 1024×768 at 60 Hz | 1,344 × 806 | 65 MHz | No: the parked LVGL GPU's MODE 1, for OO-2 or OO-3 |
| 1280×960 at 60 Hz (2× 640×480) | 1,800 × 1,000 | 108 MHz | No: the upscaler is dropped (the owner, 2026-11-20) |
| 720p, 1080p | | 74.25, 148.5 MHz | No: 480 lines don't double evenly into 720; 1080p is 2× with borders |
| DVI-D | TMDS, 3 data pairs and a clock | as above | **Yes** |
| HDMI | DVI plus data islands (audio, InfoFrames) | as above | No: no audio (the owner, 2026-11-20); DVI over the HDMI connector |

640×480 at 60 Hz is the one format every HDMI display must accept, so a single timing is a safe choice on the
digital output too. DVI over an HDMI connector is plain RGB without the packets, and televisions and monitors take
it. Calling a sold product "HDMI" needs the HDMI adopter agreement; a one-off machine doesn't.

### The outputs

```
              ┌──────────── 24-bit RGB, DE, HSYNC, VSYNC, pixel clock ───────────┐
  ECP5 ──────▶│                                                                   │
  pipeline    ├──▶ TFP410 (DVI) ──▶ TPD12S016 (ESD, DDC, HPD) ──▶ HDMI connector  │
              └──▶ ADV7123 (triple 10-bit DAC) ──▶ 75 Ω ──▶ DE-15 VGA, H/VSYNC    │
```

- **One pixel bus.** The pipeline expands 12-bit colour to 24 bits (each 4-bit value *n* becomes *n* × 17, so
  `$F` is `$FF`) and drives both chips. The parts don't care how many bits are real, so a 24-bit mode on OO-2
  needs no new board.
- **VGA (decided, the owner, 2026-11-20): a video DAC.** The ADV7123 (10 bits a channel, 330 MHz, LQFP-48) is in
  production; the 8-bit ADV7125 is marked not recommended for new designs. The FPGA drives its top 8 bits of each
  channel and ties the bottom 2 low.
- **DVI.** The TFP410 is spec-compliant, in a 64-pin TQFP, 3.3 V, good to 165 MHz, and still in production. It
  keeps TMDS out of the FPGA, so no edge pin needs to carry 250 Mbit/s.
- **The connector side.** A TPD12S016 protects the HDMI lines, shifts DDC and HPD, and supplies the 5 V the
  display's EDID needs. Reading the EDID is not planned; HPD can show in STATUS later if wanted.
- **Apple II mode** is unchanged on both outputs: 560×384 centred in 640×480, at the one timing.

## Headroom and future-proofing

**Proposed, owner to confirm.** How far these parts go before the card needs a different design. OO-1 ships with
640×480 only; this is what a later bitstream could do on the same card, for OO-2 or a later OO-1 mode.

### What each part allows

| Part | Its limit | What that reaches |
| --- | --- | --- |
| TFP410 | 165 MHz pixel clock | 1080p and 1920×1200 at 60 Hz (reduced blanking); nothing above |
| ADV7123 | 330 MHz | Well past 1080p on VGA; never the limit |
| ECP5-25F | About 150 MHz for a pixel pipeline; no SERDES (the LFE5U) | 1080p with a 2-pixel pipeline; not HDMI 2.0 |
| SDRAM, 16 bits | About 115 MB/s with the interleave, about 170 without | **The first limit:** see below |
| SDRAM, size | 32 MB (8 MB visible today) | Room for double-buffered 1080p at 16 bits, if OO-2 widens the address model |
| The CPU and DMA | One bus cycle per CPU cycle: about 7 MB/s, 14 with words | **The practical limit on OO-1:** a full 640×480 16-bit frame takes 0.04–0.09 s to fill |

### Scanout against memory bandwidth

One full-screen layer, fetched a line at a time into the line buffers:

| Timing | Needs, 8 bits a pixel | Needs, 16 bits a pixel | Of 115 MB/s (8 / 16) | Of 170 MB/s (8 / 16) |
| --- | --- | --- | --- | --- |
| 640×480 at 60 Hz | 20 MB/s | 40 MB/s | 17 % / 35 % | 12 % / 24 % |
| 800×600 at 60 Hz | 30 MB/s | 61 MB/s | 26 % / 53 % | 18 % / 36 % |
| 1024×768 at 60 Hz | 50 MB/s | 99 MB/s | 43 % / 86 % | 29 % / 58 % |
| 1280×720 at 60 Hz | 58 MB/s | 115 MB/s | 50 % / 100 % | 34 % / 68 % |
| 1280×1024 at 60 Hz | 82 MB/s | 164 MB/s | 71 % / **143 %** | 48 % / 96 % |
| 1920×1080 at 60 Hz | 130 MB/s | 259 MB/s | **113 %** / **225 %** | 76 % / **152 %** |

### What that means

- **640×480, every mode:** plenty of room, which is all OO-1 needs.
- **With the interleave (the 6502's zero-wait reads):** 8-bit layers up to 1024×768 or 720p; 16-bit up to 800×600.
- **Without it (aperture reads with /WAIT, for a later mode):** 8-bit up to 1280×1024, 1080p tight; 16-bit up to
  720p. The parked LVGL GPU's 1024×768 fits either way at 8 bits.
- **Against the SRAM plan** (about 200 MB/s on 32 bits) this is about one step lower, 720p-class instead of
  1080p-class. The limit is the module's 16-bit width, not SDRAM.
- **More, later:** a chip-down card with two SDRAMs on 32 bits (about 300 MB/s or more) passes the SRAM plan with
  the same controller.
- **4K:** out of reach. It needs 297–594 MHz pixel clocks, TMDS at 3–6 Gbit/s (SERDES the LFE5U doesn't have),
  DDR3 or DDR4 at 0.5–2 GB/s and 16–32 MB a frame. That is a new card.
- **Beyond 640×480 the CPU can't draw the screen.** At about 7–14 MB/s, higher resolutions are useful only with the
  card drawing for itself: the copy engine, OO-AP's draw commands, or the parked GPU.

### What the card has now, for little

- **The Si5351A, fitted** (decided, the owner, 2026-11-20); see [the board](#the-board).
- **The TFP410 wired for 24-bit, single-edge input,** its clock on a clock-capable FPGA output, and the TMDS pairs
  routed as 100 Ω differential pairs, so higher timings need only a bitstream.
- **24 MB of SDRAM** software doesn't see yet.
- **No register for it yet.** A timing-mode register would go in the reserved `$C4C0–$C4FE`; the parked GPU plan's
  GMODE at `$C4B0` is SLOT0 now.

## Discrete chips instead of the FPGA

**Not as a replacement.** Unlike OO-AP, where bit-slice parts did exactly this job in the 1970s, this card's work
grows with the pixel rate and the feature list:

- **The arbiter** shares one memory among scanout, the aperture, the port and the Apple mirror at 100 MHz. In
  discrete logic that's a sequencer in 74AC or 74F parts with edges every 10 ns, on a hand-built board.
- **The features:** two layers of six types, 64 sprites with priority and collision groups, a palette, the Apple
  modes with NTSC artifacts and the splash's blur. The machines of the era did each of these with a custom chip
  (TMS9918, the NES PPU, the IIgs's VGC). Done in TTL it's a few hundred parts.
- **What's still made:** the 5 V ATF1508 CPLD (PLCC-84, socketable), 74AC and 74LVC logic and fast SRAM. The
  1980s video chips (the 6845, the V9958) are NOS only.

**What a discrete card could be:** a cut-down card with 80×30 text and 640×480 mono only. About 25–40 parts: two
or three ATF1508s for timing, address generation and the bus, an SRAM, a font in flash, a shift register and the
DAC, with the same register block for those two modes. OODOS's console and mono graphics would run on it. OOUI's
palette layers, sprites and the games wouldn't. It would be worth building as a stream project, not as the card.

## The board

**Proposed (2026-11-20): the card is an iCESugar-Pro module on an OO carrier.** The module brings the FPGA, the
SDRAM, its 25 MHz oscillator, the 32 MB configuration flash and a USB debugger (iCELink: drag-and-drop programming
and a serial port to the FPGA). The carrier brings everything else:

- **Form:** the 98-contact keyed card edge ([modules.md](modules.md)); a DDR2 SODIMM-200 socket for the module; the
  DE-15 and HDMI connectors on the bracket.
- **Bus side:** the 74LVC16T245, five 74LVC245s and the 74LVC07A.
- **Video:** the TFP410, the TPD12S016 and the ADV7123.
- **Power from the bus's 5 V:** a 3.3 V buck for the carrier's parts, and whatever the module takes at its edge
  (to check). The module makes its own core voltages. An estimate: 1–1.5 W.
- **Clock:** a 25.175 MHz oscillator into a clock-capable edge pin (design.md: the video card keeps its own pixel
  clock); the PLL makes 100.7 MHz from it. If no edge pin reaches a clock-capable ball, the PLL makes 25.2 MHz from
  the module's 25 MHz instead (0.1 % fast, inside monitors' tolerance).
- **The Si5351A (decided, the owner, 2026-11-20: fitted).** The Si5351A-B-GT, MSOP-10, with its own 25 MHz
  crystal (the A variant takes a crystal, not a clock input), on the FPGA's I²C pins.
  - **Why:** the ECP5's PLLs can't make every standard pixel clock (40, 65, 74.25, 108 and 148.5 MHz) exactly;
    the Si5351A's fractional dividers can.
  - **Its outputs:** CLK0 to a clock-capable FPGA input as the alternate pixel clock; CLK1 and CLK2 to test points.
  - **Its boot state:** a stock part comes up with its outputs off until written, so the card never depends on it:
    a later bitstream that wants another timing writes the Si5351A over I²C and switches its pixel clock with the
    ECP5's glitchless clock mux (DCS), during vertical blank.
  - **OO-1's bitstream** leaves it unprogrammed; bring-up checks it answers on I²C and can make 25.175 MHz.
- **Debug:** two LEDs on the carrier and the module's RGB LED; the module's serial port and JTAG.
- **Updating the bitstream:** over the module's USB today; from the OO-1 through a register is a possible later
  feature.

**The alternative: a chip-down card,** with the LFE5U-25F (or 45F), one IS42S16160 or W9825G6KH SDRAM (or two, for
32 bits), a 64 Mbit SPI flash, a JTAG header and the ECP5's 1.1 V and 2.5 V supplies, on a 4-layer board. It is the
answer if the module becomes hard to buy, or if OO-2 wants the 32-bit SDRAM's bandwidth. The RTL is the same.

**Parts cost, an estimate (single quantity, Nov 2026):** the iCESugar-Pro module about $47 (its 2020 launch price);
TFP410 and TPD12S016 about $10; ADV7123 about $8–12; the Si5351A and its crystal about $3; the 25.175 MHz
oscillator about $2; transceivers, buffers and the buck converter about $15; the SODIMM socket, connectors and
edge-card hardware about $10. **About $95–100**, plus a 4-layer carrier board.

## The prototype

**Proposed (2026-11-20).** The prototype is the card itself, built up in phases.

**Design files: [video/](video/README.md)** (2026-11-20): the carrier's KiCad project (complete as a netlist, to be
arranged by hand), the direct board's skeleton, the module's pin map and the carrier's LPF.

**Checked in the module's schematic (Muse Lab v1.3, 2026-11-20):**

- every bank's VCCIO is 3.3 V, so the edge pins all take the carrier's 3.3 V logic;
- the edge has dedicated clock inputs: the 25.175 MHz oscillator goes to SODIMM pin 51 (PT29A, PCLKT0_0) and the
  Si5351A's CLK0 to pin 118 (PT27A, PCLKT0_1);
- the module's 5 V comes in on pins 2-12 (GND on 1-11 and others); the carrier feeds it through a Schottky diode so
  the module's own USB 5 V can't drive the bus;
- the serial port to the iCELink (PT38A, PT42A) and the SDRAM are on balls that aren't on the edge;
- bank 8's five edge pins (PB4A/B, PB6A/B, PB13A) take part in configuration, so they are spares;
- two of the schematic's net labels are misnamed (PT62D is ball C13, PT60B; PT58D is C12, PT56B); the pin map uses
  the balls.

**Still to check on the module** before laying out the carrier:

- the SDRAM's speed grade (the part number's suffix on the chip itself), for 100 MHz;
- the SODIMM socket's part and footprint, matched to the module's pin numbering.

**The phases:**

| Phase | What runs | Hardware |
| --- | --- | --- |
| P1 | The timing generator, the splash, and the SDRAM controller at 100.7 MHz with layers and sprites drawn from SDRAM; a test pattern on both outputs | The module on Muse Lab's breakout carrier, with a DVI PMOD (TFP410) and a 12-bit VGA PMOD |
| P2 | The real outputs and clocks: the ADV7123, TFP410, oscillator and Si5351A | The OO carrier, not yet in a backplane |
| P3 | The real register map, the aperture, the port and the interleave, driven by the emulator over the iCELink serial port; frames compared with the emulator's golden frames | The OO carrier on the bench |
| P4 | The OO-1 bus itself, through the level shifters | The OO carrier in the backplane: the card |

P1 is the bench test that settles the SDRAM question: if the module's SDRAM won't run at 100 MHz with the
interleave's timing, the carrier's layout waits for that answer.

**Parts to order (approximate, single quantity, Nov 2026):**

| Item | Part | Qty | About | For |
| --- | --- | --- | --- | --- |
| FPGA module | Muse Lab iCESugar-Pro with its breakout carrier (LFE5U-25F-6BG256C, 32 MB SDRAM) | 1 | $60 | P1-P4 |
| Spare module | iCESugar-Pro, module only | 1 | $47 | P2-P4 |
| DVI module | 1BitSquared PMOD Digital Video Interface (TFP410, 12-bit), or Black Mesa Labs' 24-bit dual-PMOD DVI board (bare, OSH Park) plus a TFP410 | 1 | $12-20 | P1 |
| VGA module | Digilent Pmod VGA (12-bit resistor ladder) | 1 | $15 | P1 |
| Clock | Adafruit Si5351A breakout (#2045) | 1 | $8 | P1 |
| Video DAC | ADI ADV7123 (KSTZ140 or KSTZ330) | 2 | $8-12 each | P2 |
| DVI transmitter | TI TFP410PAP | 2 | $6-8 each | P2 |
| HDMI protection | TI TPD12S016 | 2 | $2 each | P2 |
| Clock generator | Si5351A-B-GT and a 25 MHz crystal | 2 | $3 each | P2 |
| Pixel oscillator | 25.175 MHz, 3.3 V | 2 | $2 each | P2 |
| SODIMM socket | DDR2 SODIMM-200, standard height | 2 | $3 each | P2 |
| Connectors | DE-15 (VGA), HDMI type A | 2 each | $5 | P2 |
| Bus transceivers | 74LVC16T245 (1), 74LVC245 (5), 74LVC07A (1), per card | 2 sets | $10 a set | P4 |
| OO carrier PCB | 4-layer, with the 98-contact edge and a SODIMM-200 socket | 5 | $40-80 | P2 |

**About $280-340** for everything, with parts for two carriers and a spare module.

## The owner's answers (2026-11-20)

1. **BGA:** acceptable.
2. **Memory:** first two SRAMs, then 2, 4 or 8 MB by the chips fitted, stated by jumpers; all **replaced by 9**.
3. **Digital output:** no audio; DVI through the TFP410.
4. **VGA:** a video DAC (the ADV7123).
5. **OO-AP:** the same FPGA family.
6. **Upscaler:** not needed; dropped.
7. **The Si5351A:** fitted now.
8. **The prototype:** on the iCESugar-Pro module (the owner's find).
9. **Card memory: SDRAM,** the module's 32 MB, with the interleave for zero-wait aperture reads, accepting
   720p-class headroom instead of 1080p-class. The SRAMs, their second footprint and the size jumpers are dropped.

## Open questions

1. **The card's form:** the module on the OO carrier as the card itself (proposed), or the carrier only for the
   prototype and a chip-down card after?
2. **What software sees:** 8 MB (MEMSIZE 128, proposed, matching the sprite reach), or the register map's most,
   16 MB less 64 KB (MEMSIZE 255)?

The interleave's details, OOLIB's two allocation ranges and the prototype plan are proposed, owner to confirm.

## Docs to update when accepted

- [video-rev2.md](video-rev2.md): question 7 answered, and its "2 MB asynchronous SRAM" replaced by SDRAM (its
  note, as for B7; the file stays frozen otherwise).
- [design.md](design.md#video): "Hardware" names the part, the memory and the outputs; revision 2's "Card memory"
  and MEMSIZE; "Open" under revision 2 closed.
- [oolib.md](oolib.md): allocation from `$200000–$7FFFFF` as well.
- [ooap-design.md](expansion/ooap-design.md): its FPGA part question, answered (the ECP5, answer 5).
- The emulator's `VideoCard`: MEMSIZE 128 and 8 MB of card memory.
- The Technical Reference's chapter 6: the outputs, the connectors and card memory's size.
