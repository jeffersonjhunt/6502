# 6 Video

The VGA card (module 9, `$C480`): text, tile and bitmap layers, sprites, the palette, and its memory in banks 8–B.

The design record, for the reasons: [hw/design.md, "Video"](../../hw/design.md#video), ["Video
registers"](../../hw/design.md#video-registers), ["Boot splash"](../../hw/design.md#boot-splash).

## Modes and layers

Status: **emulated**

The card always sends 640×480 at 60 Hz. It has no registers for the timing: every mode is drawn into that one
picture. The card draws two background layers and the sprites from video memory, and its registers only say where
things are and how to show them.

| | |
|---|---|
| Pixel clock | 25.175 MHz |
| A line | 800 pixel clocks, 640 of them shown: 31.8 µs, 227.5 CPU cycles at 7.16 MHz |
| A frame | 525 lines, 480 of them shown: 16.7 ms, about 119,400 CPU cycles |
| Vertical blank | lines 480–524: 45 lines, 1.43 ms |

The card's timing runs from its own clock. The picture stays at 60 frames a second whatever the CPU's clock is. It
isn't locked to the CPU, so code can't time a mode change to a cycle mid-frame.

CTRL ([`$C480`](#reg-c480)) turns things on: bit 0 the display, bit 1 layer 0, bit 2 layer 1, bit 3 the sprites.
With the display off the screen is black, but the timing and its interrupts run on. Each layer's CONFIG
([`$C490`](#reg-c490), [`$C4A0`](#reg-c4a0)) gives it one of six types, in bits 2–0:

| Type | Layer | Its own pixels | MAPBASE holds | TILEBASE holds |
|---|---|---|---|---|
| 0 | text, 80×30 cells | 640×480 | the cells | the font |
| 1 | text, 40×30 cells, each pixel doubled across | 320×480 | the cells | the font |
| 2 | tiles, each pixel shown 2×2 | 320×240 | the map | the tiles |
| 3 | bitmap 256: 8 bits a pixel, shown 2×2 | 256×240 | the image | |
| 4 | bitmap 512: 4 bits a pixel | 512×480 | the image | |
| 5 | mono: 1 bit a pixel | 640×480 | the image | |
| 6, 7 | unassigned: draws nothing | | | |

A layer of type 6 or 7 is drawn as if it were off.

### What shows where

Status: **emulated**

Each pixel of the screen is built back to front:

1. the BORDER colour where no layer that is on covers the pixel, or else palette entry 0;
2. sprites at depth 1;
3. layer 0;
4. sprites at depth 2;
5. layer 1;
6. sprites at depth 3.

A layer pixel or sprite pixel whose value is 0, before PALOFS is added, is transparent: what is behind it shows.
Text, tile and mono layers cover the whole screen. The two bitmaps cover x 64–575 only, and BORDER shows on either
side. With both layers off, or both unassigned, the whole screen is BORDER, and the sprites are drawn over it.

### Text

Status: **emulated**

A text layer is 30 rows of 80 or 40 cells, from MAPBASE, row by row. Each cell is two bytes: the character, then
its attribute, with the foreground colour in bits 3–0 and the background in bits 7–4. The font is at TILEBASE:
256 characters of 16 bytes, one byte a row of 8 pixels, the leftmost in bit 7. A cell is 8×16 pixels; at 40
columns each is shown 16 pixels wide.

A pixel set in the font takes the foreground colour and a clear one the background, and PALOFS × 16 is added to
either. A colour of 0 is transparent, so a cell with background 0 shows what is behind it.

With CONFIG's bit 7 set, attribute bit 7 blinks the character instead: for 16 frames in every 32, about 1.9 Hz,
the cell shows its background only. The background is then bits 6–4, 8 colours. The BIOS's cursor blinks that way,
with no help from the BIOS.

### Tiles

Status: **emulated**

A tile layer is 320×240, each pixel shown as 2×2. CONFIG's other bits set it up:

- bit 3: tiles 16×16 (else 8×8);
- bit 4: 8 bits a pixel (else 4);
- bit 5: the map is 64 entries wide (else 32);
- bit 6: the map is 64 entries high (else 32).

The map is at MAPBASE, row by row, two bytes an entry: the tile number's bits 7–0, then a byte with the tile
number's bits 9–8 in bits 1–0, H flip in bit 2, V flip in bit 3 and a palette offset in bits 7–4. The map is up to
64×64 entries, 8 KB. Tile *n* is at TILEBASE + *n* × its size: 32, 64, 128 or 256 bytes. A tile is row by row; at 4
bits a pixel, the left pixel of a byte is its high 4 bits.

A 4-bit tile's colour is ((the entry's offset + PALOFS) mod 16) × 16 + the pixel. An 8-bit tile's colour is the
pixel, with no offset.

### Bitmaps and mono

Status: **emulated**

- **Bitmap 256** is 256×240 at 8 bits a pixel, shown 2×2 at 512×480. Each row is 256 bytes, one page, so from
  MAPBASE y is the address's high byte and x its low byte. The colour is the pixel; PALOFS isn't used. 60 KB.
- **Bitmap 512** is 512×480 at 4 bits a pixel, also 256 bytes a row, the left pixel in a byte's high 4 bits. The
  colour is PALOFS × 16 + the pixel. 120 KB.
- **Mono** is 640×480 at 1 bit a pixel, 80 bytes a row, the leftmost pixel in bit 7. A 1 is colour PALOFS × 16 + 1;
  a 0 is transparent. 37.5 KB.

Both bitmaps sit at x 64–575, centred, with BORDER either side.

### Scrolling

Status: **emulated**

Each layer has HSCROLL and VSCROLL, 10 bits each, in the layer's own pixels: a tile layer and bitmap 256 scroll in
steps of two screen pixels both ways, a 40-column text layer in steps of two across and one down. A scroll moves the
picture left or up, and it wraps within the layer: within the map for tiles (256 to 1,024 pixels each way), within
640×480 for 80-column text and mono, 320×480 for 40-column text, 512×480 for bitmap 512, and 256×240 for bitmap 256.
A text layer has no cells past its 80×30 or 40×30, so scrolling one rotates the screen.

The scroll registers, and every base register, take effect at the start of vertical blank, line 480: to show on the
next frame they must be written before it ([below](#when-changes-show)).

### Apple II mode

Status: **emulated**

In Apple II mode the card draws the IIe's display instead of its layers: text, lo-res, hi-res and the double modes,
from banks 0 and 1, as the north bridge's soft switches say. Each Apple dot is 2×2 pixels, the 560×384 picture
centred on black. The card's registers can't be reached, and the /RESET that entered the mode has set them all back
to their reset values. Text is drawn from the IIe's character ROM, which the BIOS's `C806R` puts in video memory at
`$BE000` (on revision 2, proposed, in card memory at `$1E0000`, through the port). [Chapter 13](13-apple-ii-mode.md)
has the mode.

## Sprites

Status: **emulated**

Sprites share the tile layers' 320×240, each sprite pixel shown as 2×2. CTRL bit 3 turns them on. The table is 64
entries of 8 bytes, 512 bytes, at SPRBASE ([`$C483`](#reg-c483)), in VERA's layout:

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 7–0 | the image's address bits 7–0, in 64-byte units of video memory |
| 1 | 7 | 1: 8 bits a pixel; 0: 4 bits |
| 1 | 6–4 | not used |
| 1 | 3–0 | the image's address bits 11–8 |
| 2 | 7–0 | X bits 7–0 |
| 3 | 1–0 | X bits 9–8; bits 7–2 not used |
| 4 | 7–0 | Y bits 7–0 |
| 5 | 1–0 | Y bits 9–8; bits 7–2 not used |
| 6 | 7–4 | collision groups, a mask |
| 6 | 3–2 | depth: 0 off, 1 behind both layers, 2 between them, 3 in front |
| 6 | 1 | V flip |
| 6 | 0 | H flip |
| 7 | 7–6 | height: 8, 16, 32 or 64 pixels |
| 7 | 5–4 | width: 8, 16, 32 or 64 pixels |
| 7 | 3–0 | palette offset |

- **The image** is at its address × 64, row by row, as a tile is: at 4 bits a pixel, the left pixel of a byte is
  its high 4 bits. A pixel of 0 is transparent. A 4-bit sprite's colour is its palette offset × 16 + the pixel; an
  8-bit sprite's is the pixel.
- **X and Y** wrap at 1,024, so a sprite can start off the left or the top: a 16-pixel sprite at X = 1,016 shows its
  right half at the left edge. The visible part is X 0–319 and Y 0–239.
- **On each line** the card draws the 32 lowest-numbered sprites whose rows cover it, depth 0 not counted; any more
  aren't drawn on that line. The 32 are counted whether or not they are on the screen across. A lower-numbered
  sprite is in front of a higher-numbered one, whatever their depths.
- **Collisions.** Where opaque pixels of two drawn sprites fall on the same visible spot and their group masks share
  a bit, that is a collision. At the vertical blank after the frame, STATUS's COLL ([`$C488`](#reg-c488)) gets the
  shared bits of every collision in it, and ISR's bit 2 is set. COLL keeps them until a later frame has a collision.
  No sprites are drawn, and so none collide, while the display is off or the splash covers the screen.

A program keeps its table in ordinary RAM and copies it into video memory in the vertical blank, by DMA, as the
demo does: its 96 bytes take about 27 µs, at two cycles a byte ([chapter 5](05-dma.md#memory-to-memory)). Two
tables and a SPRBASE write to flip between them also work, but SPRBASE is latched at line 480: written before it, the
flip shows on the next frame; written in the vertical blank, a frame later.

hw/design.md, "Video" ([`hw/design.md:527`](../../hw/design.md#video)), puts a copy of the whole 512-byte table at
about 72 µs. The DMA controller takes two cycles a byte for a memory-to-memory copy
(`emulator/components/devices/dma_controller.cpp:109–110`), so it is about 143 µs at 7.16 MHz.

## The palette

Status: **emulated**

The palette is 256 entries of two bytes, 512 bytes, in video memory at PALBASE ([`$C482`](#reg-c482)). Byte 0 is
green in bits 7–4 and blue in bits 3–0; byte 1 is red in bits 3–0, its bits 7–4 not used. That is 12 bits a
colour, 4,096 colours. Every colour of the layers and the sprites is a palette index, BORDER's too. The splash and
Apple II mode have colours of their own.

The card reads the palette from memory for each line it draws, so a change to it shows from the next line drawn: a
fade done by DMA in the vertical blank shows whole on the next frame. PALBASE itself is latched at vertical blank,
so a write to it swaps the whole palette between frames.

The BIOS's palette is the CGA's 16 colours, which VINIT writes to entries 0–15 at unit 0 (`video.s`):

<!-- listing: ../../backplane/bios/video.s:460-479 -->
```
; The CGA's 16 colours, 12-bit: GGGGBBBB, then 0000RRRR.
.macro RGB r, g, b
                .byte   (g << 4) | b, r
.endmacro
CGA:            RGB     $0, $0, $0     ; black
                RGB     $0, $0, $A     ; blue
                RGB     $0, $A, $0     ; green
                RGB     $0, $A, $A     ; cyan
                RGB     $A, $0, $0     ; red
                RGB     $A, $0, $A     ; magenta
                RGB     $A, $5, $0     ; brown
                RGB     $A, $A, $A     ; light grey
                RGB     $5, $5, $5     ; dark grey
                RGB     $5, $5, $F     ; light blue
                RGB     $5, $F, $5     ; light green
                RGB     $5, $F, $F     ; light cyan
                RGB     $F, $5, $5     ; light red
                RGB     $F, $5, $F     ; light magenta
                RGB     $F, $F, $5     ; yellow
                RGB     $F, $F, $F     ; white
```

hw/design.md, "Video registers" ([`hw/design.md:593–594`](../../hw/design.md#video-registers)), has the card copy
the 512 bytes into its own RAM when PALBASE changes. The emulator reads them from video memory for every line
instead (`emulator/components/devices/video_card.cpp:515–519`), which is what lets a DMA fade show with no PALBASE
write.

## Video memory

Status: **emulated**

In native mode, everything the card draws but the splash is in banks 8–B, physical `$80000–$BFFFF`, 256 KB: the
cells and fonts, maps and tiles, bitmaps, the sprite table and images, and the palette. The card keeps its own copy
by watching every write to those banks on the backplane, so it takes no bus cycles. To the CPU and the DMA
controller the banks are ordinary RAM: a program writes them through a window on RAM blocks 32–47, or by DMA, and
can read them back ([chapter 2](02-memory-and-banking.md#physical-memory)). The emulator draws from the RAM itself,
so writes by the CPU, by DMA and from the front panel all show.

The base registers (PALBASE, SPRBASE, MAPBASE, TILEBASE) are one byte, in 1 KB units: unit *u* is physical
`$80000` + *u* × 1,024, which is RAM block 32 + *u* / 16, at (*u* mod 16) × 1 KB into it. Sprite images are in
64-byte units, 12 bits. Every address wraps within the 256 KB, so a map or a bitmap that runs past `$BFFFF` goes on
at `$80000`.

What the BIOS and its video demo (`C803R`, `demo.s`) put where:

| Units | Physical | What |
|---|---|---|
| 0 | `$80000` | the BIOS's palette, entries 0–15 |
| 1–4 | `$80400` | the BIOS's font, 4 KB, copied from flash block 30 |
| 8–12 | `$82000` | the BIOS's text screen, 80×30 cells, 4,800 bytes |
| 13 | `$83400` | the demo's palette |
| 16–18 | `$84000` | the demo's caption, a 40×30 text layer |
| 20 | `$85000` | the demo's tiles |
| 24–27 | `$86000` | the demo's map, 64×32 |
| 32 | `$88000` | the demo's ball, sprite image 512 |
| 36 | `$89000` | the demo's sprite table |
| 248–249 | `$BE000` | Apple II mode's character ROM, 2 KB (`apple.s`) |

The demo keeps clear of units 0–12, so the text screen is still there when it ends. The demo's rows are revision
1's; the demo is moving to revision 2's card memory (*proposed*, [revision 2](#revision-2-card-memory)).

### Under OODOS

Status: **built**

The console is the BIOS's text screen, set up by OODOS's own CRTINIT at cold boot: the palette at unit 0, the font
at units 1–4, the text at units 8–12. Those units, 0–12 (`$80000–$833FF`), are OODOS's (OOLIB L4): function 83
refuses them as a destination, a record read through a DMA segment may not land in them, and block 32 is never in
W1's map. A program has units 13–255. Units 13–15 are in block 32, so only DMA reaches them.

A program may write every video register but IEN (OODOS's: the vertical blank alone) and SPLASH, and of ISR only
bit 2 (writing `$04` clears the collision flag; OODOS never enables the collision interrupt, and turns off a line or
collision interrupt a program enables). It reaches video memory through OOLIB, which maps W1 itself under the W1
contract ([the Programmer's Guide, chapter 5](../oodos-programmers-guide/05-memory-and-banking.md#the-w1-contract)),
or by function 83. Collisions are read from STATUS and ISR: OODOS keeps no copy of them.

The warm boot (and function 83's `DM_CONSOLE`) sets the registers back as CRTINIT sets them: CTRL `$03`, BORDER,
PALBASE and SPRBASE 0, layer 0 text at units 8 and 1, unscrolled, layer 1's CONFIG 0, IEN the vertical blank alone.
It copies nothing: the console's memory was never the program's, so what OODOS printed meanwhile shows. OODOS's
vertical-blank copies and the pointer sprite run from the frame handler on DMA channel 1
([chapter 4](04-interrupts.md#under-oodos), [chapter 5](05-dma.md#under-oodos)).

### When changes show

Status: **emulated**

- **Each line** is drawn as the beam reaches the end of its 640 visible pixels, from video memory and the registers
  as they are at that moment. A change to memory, the palette, CTRL, BORDER, CONFIG or PALOFS shows from the next
  line drawn.
- **At the start of vertical blank**, line 480, the card latches PALBASE, SPRBASE and each layer's MAPBASE,
  TILEBASE, HSCROLL and VSCROLL. Written at any other time, they read back at once but take effect at the next line
  480, so one write flips between two buffers without tearing. To show on the next frame, they must be written
  before line 480; written in vertical blank, they show a frame later.
- **In vertical blank**, lines 480–524, nothing is drawn, and STATUS bit 7 is set. Memory, the palette's contents,
  CTRL, BORDER, CONFIG and PALOFS written then show on the next frame. The demo waits for its start so:

<!-- listing: ../../backplane/bios/demo.s:470-476 -->
```
; Wait for the start of a vertical blank (STATUS bit 7 going on).
WAITVB:
@out:           BIT     VIDEO_STATUS
                BMI     @out
@in:            BIT     VIDEO_STATUS
                BPL     @in
                RTS
```

After WAITVB the demo copies its sprite table in and writes layer 0's CONFIG and HSCROLL
(`backplane/bios/demo.s:236–243`). The sprites show on the next frame, but the scroll, latched already, shows a frame
later, so it runs a frame behind. CONFIG isn't latched, so the demo writes it a frame after the MAPBASE it goes with
(*proposed*, revision 2's demo: [revision 2](#revision-2-card-memory)).

### The BIOS's text screen

Status: **emulated** on revision 1; **proposed** on [revision 2](#revision-2-card-memory), which BIOS 0.13 is built
for (it takes the screen only when the ID reads `$92`).

At boot VINIT copies the font and the palette in, clears the screen, and ends in VSCREEN, which sets the registers.
Break calls VSCREEN again, after QUIET, so the monitor always comes back to this screen, with its memory as it was
left. VSCREEN does nothing unless both the video card's and the DMA controller's ID bytes answer:

<!-- listing: ../../backplane/bios/video.s:146-176 -->
```
; The card's registers for the text screen, if there is one: layer 0 only,
; unscrolled, no interrupts. Its memory, the aperture's slots and the port
; are left as they are.
VSCREEN:
                JSR     PROBE
                BPL     @done
                STZ     VIDEO_IEN
                STZ     VIDEO_PALBASE
                STZ     VIDEO_PALBASE + 1
                STZ     VIDEO_SPRBASE
                STZ     VIDEO_SPRBASE + 1
                STZ     VIDEO_BORDER
                LDA     #VIDEO_TEXT80B
                STA     VIDEO_L0_CONFIG
                LDA     #<(TEXT_CARD >> 8)
                STA     VIDEO_L0_MAP
                LDA     #>(TEXT_CARD >> 8)
                STA     VIDEO_L0_MAP + 1
                LDA     #<(FONT_CARD >> 8)
                STA     VIDEO_L0_TILE
                LDA     #>(FONT_CARD >> 8)
                STA     VIDEO_L0_TILE + 1
                STZ     VIDEO_L0_HSCROLL
                STZ     VIDEO_L0_HSCROLL + 1
                STZ     VIDEO_L0_VSCROLL
                STZ     VIDEO_L0_VSCROLL + 1
                STZ     VIDEO_L0_PALOFS
                STZ     VIDEO_L1_CONFIG
                LDA     #VIDEO_ON_L0
                STA     VIDEO_CTRL
@done:          RTS
```

It leaves ISR, LINE and SPLASH alone, and of layer 1 it writes only CONFIG. Proposed, on revision 2: the font, the
palette and the text keep their old offsets in card memory (`$000400`, `$000000` and `$002000`, so the base
registers read `$0004`, `$0000` and `$0020`). VINIT writes the font and the palette into the port's PDATA, and
clears the screen with two port transfers by DMA on channel 1, held on one byte with PSTEP 2: the characters, then
the attributes. VPUTC, run for each character, writes it with the CPU through aperture slot 0 (W2 on physical
`$80000`; it points SLOT0 at page 0 and puts both back after), and scrolls by the port on channel 1: the rows out
of the card into a 2 KB buffer at `$FC000` (under the I/O hole, in RAM block 63) and back in a row higher, then a
fill for the last row. It puts the port's PADDR, PSTEP and PCTL back after, but stops a program's own transfer on
channel 1. Revision 1's BIOS (0.12) wrote it through W2 on RAM block 32 and scrolled by DMA on channel 3
([chapter 5](05-dma.md#memory-to-memory)).

## The boot splash

Status: **emulated**

The card can draw the One Off Endeavors logo building itself, in 5 s, from a ROM of its own rather than from video
memory. So it runs before anything is in the video banks, and costs the CPU one register write. hw/design.md's
"Boot splash" ([hw/design.md](../../hw/design.md#boot-splash)) has what it shows and the ROM's layout. It still
labels the splash proposed; the emulator has it.

SPLASH ([`$C489`](#reg-c489)) bit 0 runs it:

- **Written 1,** the splash starts at the next vertical blank and covers the whole screen, whatever CTRL says, for
  the ROM's 300 frames. Then the card goes back to its layers by itself and the bit reads 0. Writing 1 while it
  plays doesn't start it again.
- **Written 0,** the bit reads 0 at once, and the splash stops at the next vertical blank.
- **/RESET** stops it at once.
- The timing goes on underneath: vertical blank, the line interrupt and the latching are as usual, and the
  registers can be set up behind it.

The BIOS writes 1 at boot, if the card's ID answers, and then VINIT sets the text screen up behind it (`splash.s`).
With a sound card, the BIOS starts the card's 6510 on the vertical blank the splash starts on, so the tune keeps
time with the picture ([chapter 7](07-sound.md)). With a `/BOOT.BIN` to boot, AUTOBOOT waits for the bit to clear
first ([chapter 3](03-boot.md#booting-from-the-sd-card)). Break's QUIET writes 0, and so does the demo before it
takes the screen.

## Interrupts

Status: **emulated**

The card has three interrupt sources, in the same bits of ISR ([`$C485`](#reg-c485)) and IEN
([`$C484`](#reg-c484)):

| Bit | Source | Set |
|---|---|---|
| 0 | vertical blank | at the start of line 480, once a frame |
| 1 | line | at the start of line LINE ([`$C486`](#reg-c486)), before it is drawn |
| 2 | collision | at the vertical blank after a frame with a sprite collision |

An event sets its ISR bit whether IEN enables it or not, so a program can poll ISR with the interrupts off. The card
pulls /IRQ while ISR AND IEN is nonzero, and reads 0 in IRQH's bit 1 meanwhile
([`$C689`](11-north-bridge-and-rtc.md#reg-c689)). A handler reads ISR, does its work and writes the bits it handled
back to ISR as 1s, which clears them; until it does, /IRQ stays low
([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)).

- The line interrupt compares LINE with the beam's line, 0–524, so a LINE of 525 or more never matches. It comes at
  the line's first pixel, about 180 CPU cycles before the line is drawn: a handler that quick changes that line.
  Mid-frame it can change only what isn't latched, CTRL, BORDER, CONFIG and PALOFS, or the palette in memory; a
  latched register waits for line 480.
- With the display off, or the splash on the screen, the vertical blank and line interrupts still come.
- The BIOS enables none of them. VSCREEN and QUIET write 0 to IEN, but nothing in the BIOS clears ISR. After a
  reset LINE is 0, so ISR soon reads `$03`. After Break, LINE and any collision bits are as the program left them:
  QUIET writes only IEN and SPLASH, VSCREEN writes neither LINE nor ISR, and COLL clears only on /RESET.
- In Apple II mode the registers can't be reached, and the /RESET that entered the mode cleared IEN, so the card
  never interrupts there.

hw/design.md's register table ([`hw/design.md:573`](../../hw/design.md#video-registers)) says any pending ISR bit
pulls IRQH. The card interrupts on ISR AND IEN (`emulator/components/devices/include/video_card.h:252`), as the
same section's later list says ([`hw/design.md:619`](../../hw/design.md#video-registers)).

## Revision 2: card memory

Status: **proposed**

Revision 2 ([hw/video-rev2.md](../../hw/video-rev2.md)) draws from 2 MB of its own memory instead of banks 8–B,
reached through a 64 KB aperture at physical `$80000` (`VIDEO_APERTURE`, four 16 KB slots) and a data port on DMA
channel 1. Its ID reads `$92` (`VIDEO_ID_V2`). It moves the base registers to two bytes in 256-byte units and the
layers' registers up a byte; the `_R1` equates are revision 1's names where they moved. Every byte it doesn't name
reads 0. The emulator has it (`rev=2` on the machine's video line); the BIOS and OODOS still drive revision 1.

The video demo (`C803R`, `demo.s`) is revision 2's (*proposed*); on a revision 1 card it goes straight back to the
monitor. It loads everything through the port and keeps to the programs' region of card memory, so the BIOS's
screen below `$040000` and Apple II mode's memory from `$1C0000` are as they were when it ends:

| Card address | What |
|---|---|
| `$040000` | the palette, 6 slices: the scene, the 4 balls', the ripple's ramp (colours 80–95) |
| `$040400` | a copy of the BIOS's font, from flash block 30 |
| `$041400`, `$041E00` | the caption, a 40×30 text layer: a page for each scene, flipped by layer 1's MAPBASE |
| `$043000`, `$044000` | the tiles, and the 64×32 map |
| `$045000`, `$045100` | the ball (sprite image unit `$0450`), and the sprite table, sent by port DMA each frame |
| `$050000`–`$13FFFF` | the ripple: 16 bitmaps of 256×240 at 8 bits, `$F000` apart, built by port DMA at the start |

Every 256 frames layer 0 changes between the scrolling tiles and the ripple, which flips its MAPBASE to the next
bitmap each frame: 960 KB of frames, more than revision 1's whole video memory.

## Registers

Status: **emulated**

The card answers at `$C480–$C489`, the two layers at `$C490–$C497` and `$C4A0–$C4A7`, and the ID at `$C4FF`. Every
other address in its block reads `$FF`, and a write to it is lost. A /RESET (a reset, Ctrl-Reset, or entering
Apple II mode) puts every register back to its reset value and stops the splash. The beam isn't reset, so the
monitor keeps its sync.

<a id="reg-c480"></a>
### $C480 VIDEO_CTRL: what is on

#### Equates

`VIDEO`, `VIDEO_CTRL`

#### Module

9, the video card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0; a write is ignored |
| 3 | SPRITES | 1: the sprites are drawn |
| 2 | LAYER1 | 1: layer 1 is drawn |
| 1 | LAYER0 | 1: layer 0 is drawn |
| 0 | DISPLAY | 1: the display is on; 0: the screen is black |

#### Reset

`$00`: the screen black.

#### Status

**emulated**

#### Notes

CTRL isn't latched: a write shows from the next line drawn. backplane.inc names `VIDEO_ON_L0` (`$03`: the display
and layer 0) and `VIDEO_ALL` (`$0F`: everything). The BIOS writes `VIDEO_ON_L0` at boot and on Break (VSCREEN), so
under the monitor CTRL reads `$03`. The splash covers the screen whatever CTRL says.

<a id="reg-c481"></a>
### $C481 VIDEO_BORDER: the border colour

#### Equates

`VIDEO_BORDER`

#### Module

9, the video card

#### Access

R/W.

#### Bits

All: a palette index, the colour where no layer covers the screen.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The border shows beside the bitmaps (x 0–63 and 576–639), and over the whole screen with no layer on. It isn't
latched. The BIOS writes `$00` at boot and on Break.

<a id="reg-c482"></a>
### $C482 VIDEO_PALBASE_R1: where the palette is

#### Equates

`VIDEO_PALBASE_R1`

#### Module

9, the video card

#### Access

R/W. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: the palette's unit, at physical `$80000` + PALBASE × 1,024. It is 512 bytes.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Latched at line 480, so a write in vertical blank shows a frame later ([above](#when-changes-show)). The palette's
contents are read for each line, so they need no PALBASE write to show ([The palette](#the-palette)). The BIOS writes
`$00` at boot and on Break; the demo uses unit 13.

<a id="reg-c483"></a>
### $C483 VIDEO_SPRBASE_R1: where the sprite table is

#### Equates

`VIDEO_SPRBASE_R1`

#### Module

9, the video card

#### Access

R/W. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: the sprite table's unit, at physical `$80000` + SPRBASE × 1,024. It is 64 entries of 8 bytes
([Sprites](#sprites)).

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Latched at line 480, so a write in vertical blank shows a frame later. The BIOS writes `$00` at boot and on Break;
the demo uses unit 36.

<a id="reg-c484"></a>
### $C484 VIDEO_IEN: the interrupt enables

#### Equates

`VIDEO_IEN`

#### Module

9, the video card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0; a write is ignored |
| 2 | COLLISION | 1: interrupt on a sprite collision |
| 1 | LINE | 1: interrupt at line LINE |
| 0 | VBLANK | 1: interrupt at vertical blank |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The card pulls /IRQ while ISR AND IEN is nonzero ([Interrupts](#interrupts)). Enabling a source whose ISR bit is
already set interrupts at once, so clear ISR first. The BIOS writes `$00` at boot and on Break (VSCREEN, and
QUIET before it).

<a id="reg-c485"></a>
### $C485 VIDEO_ISR: the interrupts pending

#### Equates

`VIDEO_ISR`

#### Module

9, the video card

#### Access

R; write 1s to clear. A bit written 1 is cleared; a bit written 0 is left as it is.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | COLLISION | 1: a frame has had a sprite collision |
| 1 | LINE | 1: the beam has started line LINE |
| 0 | VBLANK | 1: a vertical blank has started |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

A bit is set by its event whether IEN enables it or not. The BIOS never clears ISR. After a reset LINE is 0, so bits
0 and 1 are set again every frame and ISR soon reads `$03`. After Break, LINE and bit 2 are as the program left them
([Interrupts](#interrupts)).

<a id="reg-c486"></a>
### $C486 VIDEO_LINE: the line to interrupt at, or the beam's line

#### Equates

`VIDEO_LINE`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first. A write sets the line for the line interrupt. A read gives the line the beam is on,
not what was written.

#### Bits

All: a line number, 10 bits. `$C487` holds bits 9–8 in its bits 1–0; its bits 7–2 are dropped on a write and read
0. A read is 0–524: 0–479 drawn, 480–524 the vertical blank.

#### Reset

The line to interrupt at is 0. A read gives the beam's line: a reset doesn't move the beam.

#### Status

**emulated**

#### Notes

Read the low byte first; the beam can move on to the next line between the two reads. A program can't read back the
line it wrote. The BIOS doesn't write LINE.

<a id="reg-c488"></a>
### $C488 VIDEO_STATUS: vertical blank and collisions

#### Equates

`VIDEO_STATUS`

#### Module

9, the video card

#### Access

R. A write is ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | VBLANK | 1: the beam is in vertical blank, lines 480–524 |
| 6–4 | | 0 |
| 3–0 | COLL | the collision groups shared in the last frame that had a collision |

#### Reset

COLL is 0. VBLANK is as the beam is.

#### Status

**emulated**

#### Notes

COLL is set at the vertical blank after a frame with a collision, with ISR's bit 2, and stays until a later frame
has one. `BIT VIDEO_STATUS` puts VBLANK in N: the demo and the splash code wait on it
([above](#when-changes-show)).

<a id="reg-c489"></a>
### $C489 VIDEO_SPLASH: the boot splash

#### Equates

`VIDEO_SPLASH`

#### Module

9, the video card

#### Access

R/W. Writing 1 starts the splash at the next vertical blank; writing 0 stops it at the next one. The card clears
the bit itself when the splash ends.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0; a write is ignored |
| 0 | ON | 1: the splash is playing, or starts at the next vertical blank |

#### Reset

`$00`. A reset stops a splash at once.

#### Status

**emulated**

#### Notes

`VIDEO_SPLASH_ON` (`$01`) is in backplane.inc. The splash lasts 300 frames, 5 s ([The boot
splash](#the-boot-splash)). The BIOS writes 1 at boot; Break (QUIET) and the demo write 0. Under the monitor it
reads 1 for the first 5 s after a reset, then 0.

<a id="reg-c48a"></a>
### $C48A VIDEO_PALBASE: where the palette is, on revision 2

#### Equates

`VIDEO_PALBASE`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only. Latched at line 480, as on revision 1.

#### Bits

All: a card address in 256-byte units, low byte first: the palette (512 bytes) starts at card address value × 256.

#### Reset

`$00 $00`.

#### Status

**proposed**

#### Notes

Revision 1's is [VIDEO_PALBASE_R1](#reg-c482).

<a id="reg-c48c"></a>
### $C48C VIDEO_SPRBASE: where the sprite table is, on revision 2

#### Equates

`VIDEO_SPRBASE`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only. Latched at line 480, as on revision 1.

#### Bits

All: a card address in 256-byte units, low byte first: the sprite table (64 entries of 8 bytes) starts at card address value × 256.

#### Reset

`$00 $00`.

#### Status

**proposed**

#### Notes

Revision 1's is [VIDEO_SPRBASE_R1](#reg-c483). On revision 2 an entry's image address is 15 bits of 256-byte units: byte 0, then byte 1's bits 6–0; byte 1's bit 7 is still the colour depth.

<a id="reg-c490"></a>
### $C490 VIDEO_L0_CONFIG: layer 0's type

#### Equates

`VIDEO_L0`, `VIDEO_L0_CONFIG`

#### Module

9, the video card

#### Access

R/W. Not latched: a write shows from the next line drawn.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | BLINK | text: 1, attribute bit 7 blinks the cell and the background is 3 bits |
| 6 | MAP_H64 | tiles: 1, the map is 64 entries high (else 32) |
| 5 | MAP_W64 | tiles: 1, the map is 64 entries wide (else 32) |
| 4 | TILE8BPP | tiles: 1, 8 bits a pixel (else 4) |
| 3 | TILE16 | tiles: 1, 16×16 tiles (else 8×8) |
| 2–0 | TYPE | 0 text 80×30, 1 text 40×30, 2 tiles, 3 bitmap 256, 4 bitmap 512, 5 mono, 6–7 nothing |

#### Reset

`$00`: text 80×30.

#### Status

**emulated**

#### Notes

A layer type ignores the bits that aren't its own, and every bit reads back as written. backplane.inc names
`VIDEO_TEXT80B` (`$80`: text 80×30, blinking), `VIDEO_TEXT40` (`$01`), `VIDEO_TILE` (`$02`: 8×8 tiles at 4 bits) and
`VIDEO_MAP_W64` (`$20`). The BIOS writes `$80` at boot and on Break. [Modes and layers](#modes-and-layers) has the
types.

<a id="reg-c491"></a>
### $C491 VIDEO_L0_MAP_R1: layer 0's map

#### Equates

`VIDEO_L0_MAP_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L0_MAP`'s

#### Module

9, the video card

#### Access

R/W. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: a unit, at physical `$80000` + MAPBASE × 1,024: the cells of a text layer, the map of a tile layer, or the
image of a bitmap or mono layer.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Latched at line 480, so one write flips between two pages; a write in vertical blank shows a frame later. The BIOS
writes 8 (`$82000`) at boot and on Break.

<a id="reg-c492"></a>
### $C492 VIDEO_L0_TILE_R1: layer 0's tiles or font

#### Equates

`VIDEO_L0_TILE_R1`

#### Module

9, the video card

#### Access

R/W. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: a unit, at physical `$80000` + TILEBASE × 1,024: a text layer's font, 4 KB, or a tile layer's tiles. Bitmap
and mono layers don't use it.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Latched at line 480, so a write in vertical blank shows a frame later. The BIOS writes 1 (`$80400`, its font) at
boot and on Break.

<a id="reg-c493"></a>
### $C493 VIDEO_L0_HSCROLL_R1: layer 0's scroll across

#### Equates

`VIDEO_L0_HSCROLL_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L0_TILE`'s

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: 10 bits, in the layer's own pixels. `$C494` holds bits 9–8 in its bits 1–0; its bits 7–2 are dropped and
read 0.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

The layer moves left by HSCROLL pixels and wraps within its width: the map's for tiles, 640 or 320 for text, 640 for
mono, 512 or 256 for the bitmaps ([Scrolling](#scrolling)). Latched at line 480, so both bytes, written
between two line 480s, take effect together; written in vertical blank, they show a frame later, as the demo's do
([When changes show](#when-changes-show)). The BIOS writes 0 at boot and on Break.

<a id="reg-c495"></a>
### $C495 VIDEO_L0_VSCROLL_R1: layer 0's scroll down

#### Equates

`VIDEO_L0_VSCROLL_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L0_HSCROLL`'s

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first. A read gives what was written last, which takes effect at the next line 480.

#### Bits

All: 10 bits, in the layer's own pixels. `$C496` holds bits 9–8 in its bits 1–0; its bits 7–2 are dropped and
read 0.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

The layer moves up by VSCROLL lines and wraps within its height: the map's for tiles, 480 for text, mono and bitmap
512, 240 for bitmap 256. Latched at line 480, so a write in vertical blank shows a frame later. The BIOS writes 0
at boot and on Break.

<a id="reg-c497"></a>
### $C497 VIDEO_L0_PALOFS_R1: layer 0's palette slice

#### Equates

`VIDEO_L0_PALOFS_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L0_VSCROLL`'s

#### Module

9, the video card

#### Access

R/W. Not latched: a write shows from the next line drawn.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0; a write is ignored |
| 3–0 | PALOFS | a 16-colour slice of the palette, added × 16 to the layer's colours |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

It applies to text colours, mono pixels, bitmap 512 and 4-bit tiles (added to the map entry's offset, mod 16). It
doesn't apply to bitmap 256 or 8-bit tiles. A colour of 0 is transparent before PALOFS is added. The BIOS writes 0
at boot and on Break.

<a id="reg-c499"></a>
### $C499 VIDEO_L0_PALOFS: layer 0's palette slice, on revision 2

#### Equates

`VIDEO_L0_PALOFS`

#### Module

9, the video card

#### Access

R/W, on [revision 2](#revision-2-card-memory) only. Not latched.

#### Bits

As revision 1's [VIDEO_L0_PALOFS_R1](#reg-c497): bits 3–0, a 16-colour slice; bits 7–4 read 0.

#### Reset

`$00`.

#### Status

**proposed**

<a id="reg-c4a0"></a>
### $C4A0 VIDEO_L1_CONFIG: layer 1's type

#### Equates

`VIDEO_L1`, `VIDEO_L1_CONFIG`

#### Module

9, the video card

#### Access

R/W, as [layer 0's](#reg-c490).

#### Bits

As [VIDEO_L0_CONFIG](#reg-c490), for layer 1.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

Layer 1 is drawn over layer 0, with depth-2 sprites between them. CTRL bit 2 turns it on. The BIOS writes `$00` at
boot and on Break, with CTRL's bit 2 off; the demo uses it as a 40×30 text layer for its caption.

<a id="reg-c4a1"></a>
### $C4A1 VIDEO_L1_MAP_R1: layer 1's map

#### Equates

`VIDEO_L1_MAP_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L1_MAP`'s

#### Module

9, the video card

#### Access

R/W, as [layer 0's](#reg-c491).

#### Bits

As [VIDEO_L0_MAP_R1](#reg-c491), for layer 1.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The BIOS doesn't write it: after Break it is as a program left it (the demo leaves 16).

<a id="reg-c4a2"></a>
### $C4A2 VIDEO_L1_TILE_R1: layer 1's tiles or font

#### Equates

`VIDEO_L1_TILE_R1`

#### Module

9, the video card

#### Access

R/W, as [layer 0's](#reg-c492).

#### Bits

As [VIDEO_L0_TILE_R1](#reg-c492), for layer 1.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The BIOS doesn't write it: after Break it is as a program left it (the demo leaves 1, the BIOS's font).

<a id="reg-c4a3"></a>
### $C4A3 VIDEO_L1_HSCROLL_R1: layer 1's scroll across

#### Equates

`VIDEO_L1_HSCROLL_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L1_TILE`'s

#### Module

9, the video card

#### Access

R/W, 2 bytes, as [layer 0's](#reg-c493).

#### Bits

As [VIDEO_L0_HSCROLL_R1](#reg-c493), for layer 1; `$C4A4` is the high byte.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

The BIOS doesn't write it: after Break it is as a program left it.

<a id="reg-c4a5"></a>
### $C4A5 VIDEO_L1_VSCROLL_R1: layer 1's scroll down

#### Equates

`VIDEO_L1_VSCROLL_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L1_HSCROLL`'s

#### Module

9, the video card

#### Access

R/W, 2 bytes, as [layer 0's](#reg-c495).

#### Bits

As [VIDEO_L0_VSCROLL_R1](#reg-c495), for layer 1; `$C4A6` is the high byte.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

The BIOS doesn't write it: after Break it is as a program left it.

<a id="reg-c4a7"></a>
### $C4A7 VIDEO_L1_PALOFS_R1: layer 1's palette slice

#### Equates

`VIDEO_L1_PALOFS_R1`; on [revision 2](#revision-2-card-memory) (proposed) this address is `VIDEO_L1_VSCROLL`'s

#### Module

9, the video card

#### Access

R/W, as [layer 0's](#reg-c497).

#### Bits

As [VIDEO_L0_PALOFS_R1](#reg-c497), for layer 1.

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The BIOS doesn't write it: after Break it is as a program left it.

<a id="reg-c4a9"></a>
### $C4A9 VIDEO_L1_PALOFS: layer 1's palette slice, on revision 2

#### Equates

`VIDEO_L1_PALOFS`

#### Module

9, the video card

#### Access

R/W, on [revision 2](#revision-2-card-memory) only. Not latched.

#### Bits

As revision 1's [VIDEO_L1_PALOFS_R1](#reg-c4a7): bits 3–0, a 16-colour slice; bits 7–4 read 0.

#### Reset

`$00`.

#### Status

**proposed**

<a id="reg-c4b0"></a>
### $C4B0 VIDEO_SLOT0: aperture slot 0's page

#### Equates

`VIDEO_SLOT0`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only.

#### Bits

All: a 16 KB page of card memory: aperture slot 0, physical `$80000`–`$83FFF`, shows card memory from SLOT0 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

#### Reset

`$00 $00`: slot 0 shows page 0, so the aperture shows card memory `$000000`–`$00FFFF` in order.

#### Status

**proposed**

<a id="reg-c4b2"></a>
### $C4B2 VIDEO_SLOT1: aperture slot 1's page

#### Equates

`VIDEO_SLOT1`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only.

#### Bits

All: a 16 KB page of card memory: aperture slot 1, physical `$84000`–`$87FFF`, shows card memory from SLOT1 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

#### Reset

`$01 $00`: slot 1 shows page 1, so the aperture shows card memory `$000000`–`$00FFFF` in order.

#### Status

**proposed**

<a id="reg-c4b4"></a>
### $C4B4 VIDEO_SLOT2: aperture slot 2's page

#### Equates

`VIDEO_SLOT2`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only.

#### Bits

All: a 16 KB page of card memory: aperture slot 2, physical `$88000`–`$8BFFF`, shows card memory from SLOT2 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

#### Reset

`$02 $00`: slot 2 shows page 2, so the aperture shows card memory `$000000`–`$00FFFF` in order.

#### Status

**proposed**

<a id="reg-c4b6"></a>
### $C4B6 VIDEO_SLOT3: aperture slot 3's page

#### Equates

`VIDEO_SLOT3`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only.

#### Bits

All: a 16 KB page of card memory: aperture slot 3, physical `$8C000`–`$8FFFF`, shows card memory from SLOT3 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

#### Reset

`$03 $00`: slot 3 shows page 3, so the aperture shows card memory `$000000`–`$00FFFF` in order.

#### Status

**proposed**

<a id="reg-c4b8"></a>
### $C4B8 VIDEO_PADDR: the port's address

#### Equates

`VIDEO_PADDR`

#### Module

9, the video card

#### Access

R/W, 3 bytes, low byte first, on [revision 2](#revision-2-card-memory) only. Each access to PDATA moves it by PSTEP.

#### Bits

All: a 24-bit card address. It wraps at the fitted memory's end; an address past MEMSIZE reads `$FF` through PDATA and ignores writes.

#### Reset

`$00 $00 $00`.

#### Status

**proposed**

<a id="reg-c4bb"></a>
### $C4BB VIDEO_PDATA: the port's data

#### Equates

`VIDEO_PDATA`

#### Module

9, the video card

#### Access

R/W, on [revision 2](#revision-2-card-memory) only. A write stores the byte at PADDR, a read returns it; either then moves PADDR by PSTEP. DMA on channel 1 does the same while PCTL bit 0 is set.

#### Bits

All: the byte at PADDR.

#### Reset

Whatever card memory holds at PADDR.

#### Status

**proposed**

<a id="reg-c4bc"></a>
### $C4BC VIDEO_PSTEP: the port's step

#### Equates

`VIDEO_PSTEP`

#### Module

9, the video card

#### Access

R/W, 2 bytes, low byte first, on [revision 2](#revision-2-card-memory) only.

#### Bits

All: how far PADDR moves after each PDATA access, up, or down with PCTL bit 2. 0 holds PADDR.

#### Reset

`$01 $00`.

#### Status

**proposed**

#### Notes

A step of 160 writes a column of the 80-column text screen.

<a id="reg-c4be"></a>
### $C4BE VIDEO_PCTL: the port's control

#### Equates

`VIDEO_PCTL`

#### Module

9, the video card

#### Access

R/W, on [revision 2](#revision-2-card-memory) only.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | DOWN | `VIDEO_PCTL_DOWN`: PADDR steps down |
| 1 | FROM | `VIDEO_PCTL_FROM`: DMA reads PDATA (0: DMA writes it) |
| 0 | DRQ | `VIDEO_PCTL_DRQ`: ask for DMA on channel 1 |

#### Reset

`$00`.

#### Status

**proposed**

#### Notes

A /DACK cycle in the other direction from bit 1's moves nothing.

<a id="reg-c4bf"></a>
### $C4BF VIDEO_MEMSIZE: the card memory fitted

#### Equates

`VIDEO_MEMSIZE`

#### Module

9, the video card

#### Access

R, on [revision 2](#revision-2-card-memory) only. A write is ignored.

#### Bits

All: card memory in 64 KB units: 32 (`VIDEO_MEMSIZE_2MB`) for 2 MB.

#### Reset

`$20`.

#### Status

**proposed**

<a id="reg-c4ff"></a>
### $C4FF VIDEO_ID: the card's ID

#### Equates

`VIDEO_ID`

#### Module

9, the video card

#### Access

R. A write is ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | TYPE | 9, the module ID |
| 3–0 | REV | 1, the revision |

#### Reset

`$91`.

#### Status

**emulated**

#### Notes

`VIDEO_ID_V1` (`$91`) is in backplane.inc. The BIOS looks for it before it touches the card: the splash, VINIT,
VSCREEN, QUIET and AUTOBOOT all do. With no card in the slot the byte reads `$FF`.
