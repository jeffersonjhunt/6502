# OO-1 Video Card Rev 2 — Private Memory

Nov 10, 2026 · @Jefferson

**Status: proposed (2026-11-10).** Decided in discussion: 2 MB of card memory, a 64 KB aperture in four 16 KB
slots, and no native-mode snooping. The register details below are **proposed**; mark them that way in
[design.md](design.md) when it is updated. The UI toolkit that rides on this card is [ooui.md](ooui.md).

## Summary

In native mode the video card draws from 2 MB of its own memory instead of a snooped copy of banks 8–B. The CPU
reaches that memory through a 64 KB aperture and a DMA-fed data port. Apple II mode keeps snooping exactly as it
does today.

| Topic | Decision |
| --- | --- |
| Card memory | 2 MB asynchronous SRAM; a size register reports what's fitted |
| Addresses | 24 bits throughout, so a later card can grow to 16 MB without software changes |
| CPU access | 64 KB aperture as four 16 KB slots, each with a two-byte page register |
| Bulk access | Data port with an auto-incrementing 24-bit address, fed by DMA on a DRQ channel |
| Native snooping | Dropped. Banks 8–B stop being video memory and return to the system |
| Apple II mode | Unchanged: the card snoops banks 0–1 and the soft switches |
| Status | Proposed. Reverses design.md's "screen is ordinary memory" decision for native mode |

## Why

The snooped model ties native video to a quarter of system RAM; private memory removes that limit and opens the
way to card-side graphics features.

**What the snooped model costs**

- Banks 8–B, 256 KB of the machine's 1 MB, are reserved for video whether a program uses them or not.
- Everything shares that 256 KB: fonts, palettes, maps, tiles, sprites, bitmaps and the console. A full 640×480
  8-bit bitmap (300 KB) can't fit at all.
- Base registers are one byte in 1 KB units, so nothing can ever address more than 256 KB.
- The card can't safely draw anything itself: its mirror would diverge from system RAM.

**What private memory buys**

- 256 KB of system RAM returned to programs and OODOS.
- Room for double-buffered full-screen bitmaps, large tile and sprite sets, and many fonts.
- A coherent place for future card-side features (a blitter, glyph expansion, a GPU), because the card's memory is
  the only copy.
- The same model as the sound card: its own RAM, a port, and DMA.

Very little native-mode software exists yet (the BIOS, its demo and OO-Amp; GW-BASIC isn't started), so the
change is cheap now and expensive later.

## Architecture

```
  6502 + north bridge ──maps W0-W3──▶ Aperture, 64 KB ──┐
  (W0-W3 windows)                     4 x 16 KB slots    │
                                                         ├──▶ 2 MB SRAM ──▶ Scanout: layers, sprites,
  System RAM ──▶ DMA controller ──DRQ 1──▶ Data port ────┤     on the card     palette, 640x480 VGA
  (1 MB)         2 cycles a byte          PADDR/PDATA    │
     │                                                   │
     └──banks 0-1, soft switches──snoop──▶ Apple snoop ──┘
                 (Apple II mode only)
```

The CPU maps aperture slots into its windows for random access. The DMA controller streams from system RAM into
the data port on channel 1. In Apple II mode the card mirrors banks 0–1 into its own memory, as today. Scanout
reads only card memory.

## Card memory

The card carries 2 MB of fast asynchronous SRAM (1M × 16, about 10 ns), shared by scanout, the aperture, the port
and the Apple-mode mirror.

- **Why SRAM.** No refresh, no wait states on the aperture, and simple arbitration, so scanout can never be
  starved. 2 MB is already more than the 6502 can usefully fill. 8 MB would mean SDRAM or HyperRAM: refresh, read
  latency, RDY wait states on the aperture and harder FPGA timing, for space the OO-1 can't fill.
- **Size register.** MEMSIZE reports the fitted memory in 64 KB units (2 MB reads 32), so OODOS and OOLIB allocate
  from what's actually there.
- **Address widths.** Card addresses are 24 bits (up to 16 MB). Base registers become two bytes in 256-byte units.
  Sprite image addresses widen to reach all of card memory.
- **Growth.** A later revision with 8 MB of SDRAM changes only MEMSIZE, plus wait states on the aperture.

An example layout of the 2 MB, which OODOS would own and document:

| Range | Size | Use |
| --- | --- | --- |
| `$000000`–`$03FFFF` | 256 KB | OODOS console, fonts, palettes, system sprites |
| `$040000`–`$1BFFFF` | 1.5 MB | Program allocations: screens, bitmaps, tiles, sprite images |
| `$1C0000`–`$1FFFFF` | 256 KB | Apple II mode: the banks 0–1 mirror (128 KB), the IIe character ROM, spare |

## Aperture

The aperture is a 64 KB physical range that the north bridge decodes to the video card, split into four 16 KB
slots that each show a page of card memory.

- **Slots and pages.** Slot *n* shows card memory at its page number × 16 KB. Each page register is two bytes
  (2 MB needs 7 bits; 16 bits reaches 1 GB), so software never changes as memory grows.
- **Matching the windows.** The CPU sees memory through four 16 KB windows (W0–W3), so any window can be pointed at
  any slot. A program can map a sprite table through W1 and part of a bitmap through W2 at once.
- **Reads and writes.** The card answers aperture cycles as a memory module. With 10 ns SRAM it meets the ~100 ns
  read budget at native speed with no wait states.
- **Location.** A 16 KB-aligned physical bank that Apple mode never uses; bank 8 is the natural choice, leaving
  banks 9–B as RAM again. The north bridge and RAM cards must agree that the card answers there.
- **Apple mode.** The card ignores aperture cycles while APPLE is set; nothing in the IIe map reaches them anyway.
- **At reset.** Slot 0 points at page 0 (the console's memory), so the BIOS can reach the text screen without
  setting anything up first.

## Data port and DMA

Bulk transfers go through a data port, the sound card's RAMADDR/RAMDATA model, so loads never need the aperture.

- **The port.** A 24-bit address (PADDR) and a data register (PDATA). Each read or write of PDATA moves the address
  on by a programmable step (PSTEP: 1, 2, 64, 256 and so on), so a column or a rectangle's rows can be written as a
  stream.
- **DMA.** The card raises DRQ while the port can take or give a byte, so the DMA controller streams from system
  RAM into card memory at 2 cycles a byte, or back. A 4 KB font loads in about 8,200 cycles; a 300 KB bitmap in
  about 0.09 s.
- **Channel.** Channel 1 is the natural one: under OODOS it carries the vertical-blank copies, which become port
  transfers in this model anyway. Channels 0 (SD), 2 (sound) and 3 (general copies) stay as they are.
- **Straight from disk.** A loader reads a file into a RAM buffer by DMA, then streams that buffer into the port;
  OOLIB's asset calls hide both steps.
- **Card to card.** Copies within card memory (scrolling, moving a sprite image) go through the port in both
  directions for now; a card-side copy engine is a later feature.

## Register changes

The block stays at `$C480`–`$C4FF`; base registers widen to two bytes, and the aperture and port take the unused
space from `$C4B0`. The ID becomes `$92`. Every address here is a draft.

| Address | Name | Change |
| --- | --- | --- |
| `$C480`, `$C481` | CTRL, BORDER | Unchanged |
| `$C482`, `$C483` | (reserved) | PALBASE and SPRBASE move to two-byte registers below |
| `$C484`–`$C489` | IEN, ISR, LINE, STATUS, SPLASH | Unchanged |
| `$C48A`–`$C48B` | PALBASE | Card address in 256-byte units, latched at line 480 |
| `$C48C`–`$C48D` | SPRBASE | Same |
| Layer +0 | CONFIG | Unchanged |
| Layer +1–+2 | MAPBASE | Two bytes, 256-byte units |
| Layer +3–+4 | TILEBASE | Two bytes, 256-byte units |
| Layer +5–+6, +7–+8 | HSCROLL, VSCROLL | Moved up one byte |
| Layer +9 | PALOFS | Moved |
| `$C4B0`–`$C4B7` | SLOT0–SLOT3 | Aperture page registers, two bytes each, 16 KB pages |
| `$C4B8`–`$C4BA` | PADDR | Port address, 24 bits |
| `$C4BB` | PDATA | Port data; each access moves PADDR on by PSTEP |
| `$C4BC` | PSTEP | Address step after each PDATA access |
| `$C4BD` | PCTL | DRQ enable, direction (to card or from card) |
| `$C4BE` | MEMSIZE | Fitted memory in 64 KB units; 32 for 2 MB |
| `$C4FF` | ID | `$92` |

Sprite table entries keep VERA's 8-byte layout, with the image address widened into byte 1's spare bits; its exact
encoding is an open question.

## Apple II mode

Apple II mode works exactly as the Technical Reference's chapter 13 describes; the only change is where the card
keeps its copy and the character ROM.

1. **Snooping stays.** The card watches writes to banks 0–1 and the soft-switch accesses (`$C050`–`$C05F`,
   `$C00C`–`$C00F`) as today. This is now the card's only snoop path.
2. **The mirror moves into card memory,** in the reserved Apple region. Nothing outside the card can see the
   difference.
3. **The character ROM moves too.** `C806R` copies the IIe's character ROM into card memory through the port
   before entering the mode, instead of to `$BE000`.
4. **Card memory survives /RESET.** The pulse that enters the mode, and every Ctrl-Reset, resets registers but
   never clears memory, so the character ROM stays.
5. **The aperture is inert.** The card ignores aperture cycles while APPLE is set, and its registers can't be
   reached, as now.
6. **DMA is idle.** The DMA controller goes idle on entering the mode, so the port is never used there.
7. **Timing is unchanged.** 640×480 output, 560×384 centred, and `$C019` in Apple timing.

## Software migration

Little native-mode software exists yet, so everything moves at once; GW-BASIC and OOUI start on the new model.

| Where | What changes |
| --- | --- |
| BIOS `video.s` | VINIT loads the font and palette through the port; VSCREEN sets two-byte bases; VPUTC writes cells through aperture slot 0 and scrolls by port |
| BIOS `demo.s` | Tiles, map, caption and sprite table loaded through the port; per-frame sprite table by port DMA |
| BIOS `apple.s` | Character ROM copied into card memory through the port |
| OODOS CRTINIT, console | Console memory at card page 0; warm boot restores registers as now |
| OODOS function 83 | Video copies become port transfers; unit-based checks become card-address ranges |
| OODOS frame handler | Vertical-blank copies and the pointer sprite go through the port on channel 1 |
| OOLIB `gx` | Allocation from card memory; plot, fill and copy through the aperture or port |
| OO-Amp | Whatever it writes directly; OOUI will absorb most of it |
| W1 contract | Revisited: video is reached through the aperture's slots, and banks 8–B become ordinary RAM |

## Emulator changes

The changes stay inside `VideoCard`, the machine description and the ROM builds, following patterns the sound card
already uses.

- **Own memory.** `VideoCard` owns a 2 MB buffer instead of `attach()`ing to the RAM's banks 8–B. The line renderer
  reads from it with 24-bit base addresses.
- **Aperture.** A memory device for the aperture's 64 KB, decoded to the video card, with the four slot registers.
  The `backplane.machine` line for RAM bank 8 changes to match.
- **Port and DMA.** PADDR, PDATA, PSTEP and PCTL, with DRQ wired to channel 1 (`drq=1` on the machine line), as the
  sound card's RAMDATA is on channel 2.
- **Apple mode.** `attach_apple()` keeps reading banks 0–1 as now; the character ROM is read from card memory.
  /RESET leaves memory intact.
- **Registers.** The new map, with the ID at `$92`; a `set_revision` switch keeps the old card for comparison while
  the BIOS moves.
- **Tools.** A `vram` debugger command to peek and poke card memory; `--screen` unchanged.
- **Tests.** The video and Apple suites updated to the new model; golden frames for the BIOS screen and demo
  regenerated once and compared after the migration.

## Open questions

1. Aperture location: bank 8 as proposed? How do the north bridge and the RAM cards hand that bank to the video
   card?
2. DMA channel for the port: channel 1 as proposed, moving OODOS's vertical-blank copies onto it?
3. Sprite image address encoding: 15 bits in 128-byte units (4 MB), or in 256-byte units (8 MB)?
4. Base register units: 256 bytes as proposed, or 1 KB to keep alignment simple?
5. Does the palette stay in card memory (read per line, as emulated) or get copied into the card's own RAM on a
   PALBASE write (as design.md says)?
6. Card memory layout: does OODOS own the whole map, or only its region with programs allocating the rest?
7. Which FPGA part, given the SRAM bus and the bus-target timing?

## Docs to update when accepted

- hw/design.md: "Video" (reverses "screen is ordinary memory" for native mode), "Video registers", "Memory"
  (banks 8–B), "DMA" (channel 1) and "Apple II Mode".
- Technical Reference: chapters 2 (memory and banking), 5 (DMA), 6 (video), 13 (Apple II mode) and 14 (emulator
  notes); docs/index/registers.json.
- OODOS Programmer's Guide chapter 5 (the W1 contract and video units) and chapter 8 (game services);
  hw/oolib.md.
