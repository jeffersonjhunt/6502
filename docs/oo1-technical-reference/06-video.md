# 6 Video

The VGA card (module 9, `$C480`): text, tile and bitmap layers, sprites, the palette, and its memory. Revision 2,
which BIOS 0.13 and OODOS drive (*proposed*), draws from 2 MB of its own ([below](#revision-2-card-memory));
revision 1, the old card, drew from banks 8–B, and the BIOS no longer supports it.

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
([`$C490`](#reg-c490), [`$C4A0`](#reg-c4a0)) gives it one of six types, in bits 2–0, or on revision 2 one of
eight:

| Type | Layer | Its own pixels | MAPBASE holds | TILEBASE holds |
|---|---|---|---|---|
| 0 | text, 80×30 cells | 640×480 | the cells | the font |
| 1 | text, 40×30 cells, each pixel doubled across | 320×480 | the cells | the font |
| 2 | tiles, each pixel shown 2×2 | 320×240 | the map | the tiles |
| 3 | bitmap 256: 8 bits a pixel, shown 2×2 | 256×240 | the image | |
| 4 | bitmap 512: 4 bits a pixel | 512×480 | the image | |
| 5 | mono: 1 bit a pixel | 640×480 | the image | |
| 6 | bitmap 640: 8 bits a pixel, revision 2 (*proposed*) | 1,024×480, 640 shown | the image | |
| 7 | direct 640: 12-bit colour a pixel, no palette, revision 2 (*proposed*) | 640×480 | the image | |

On revision 1, types 6 and 7 are unassigned: a layer of either is drawn as if it were off. On revision 2 they are
the two 640×480 colour layers ([below](#bitmap-640-and-direct-640)).

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
Direct 640 is the exception: its pixel of 0 is opaque black, and a pixel with bit 15 set is the transparent one
([below](#bitmap-640-and-direct-640)). Text, tile and mono layers, bitmap 640 and direct 640 cover the whole
screen. Bitmap 256 and bitmap 512 cover x 64–575 only, and BORDER shows on either side. With both layers off, or
both unassigned, the whole screen is BORDER, and the sprites are drawn over it.

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

Both bitmaps sit at x 64–575, centred, with BORDER either side. Revision 2 adds two that fill the screen,
[bitmap 640 and direct 640](#bitmap-640-and-direct-640).

### Bitmap 640 and direct 640

Status: **proposed**

Revision 2's layer types 6 and 7 ([hw/video640.md](../../hw/video640.md)) are 640×480 colour pictures that cover
the whole screen, x 0–639: BORDER shows nowhere under them. Revision 1 draws neither.

| | Type 6, bitmap 640 | Type 7, direct 640 |
|---|---|---|
| A pixel | a byte, a palette index | a word, low byte first: byte 0 green in bits 7–4 and blue in bits 3–0, byte 1 red in bits 3–0, as a [palette entry](#the-palette) is |
| Colours | 256 of the 4,096, through the palette at PALBASE | all 4,096, with no palette |
| A row | 1,024 bytes, 4 pages | 1,280 bytes, 5 pages, rows packed |
| Pixel (x, y) at | MAPBASE × 256 + y × 1,024 + x | MAPBASE × 256 + y × 1,280 + 2x |
| The layer | 1,024×480, of which 640 across are shown | 640×480 |
| A screen | 491,520 bytes (480 KB, 1,920 pages) | 614,400 bytes (600 KB, 2,400 pages) |
| Transparent | a pixel of 0 | a pixel with bit 15 set (byte 1's bit 7, `$8000`) |
| PALOFS | not used, as bitmap 256 | not used: no palette |

- **Bitmap 640's rows** are 1,024 bytes: pixel *x* of a row is at its offset *x*, and the 384 bytes past x 639 are
  the layer's own pixels, shown when HSCROLL moves them on ([Scrolling](#scrolling)). A row is four pages, so a row
  on a 1 KB boundary never crosses a 16 KB aperture slot: a slot holds 16 rows.
- **Direct 640's word** is `$0RGB`, the palette entry's 12 bits. Bits 12–14 are not used: the card ignores them,
  and software writes them 0 so that a later card may give them a meaning. Bit 15 set makes the pixel transparent,
  so `$0000` is opaque black: a byte fill of `$00` clears a screen to black, and one of `$FF` makes it all
  transparent. A word never crosses a slot (it is at an even address).
- **Direct 640's rows straddle slots.** Rows are 5 pages apart, so a 16 KB slot boundary falls inside 4 rows in
  every 64: from a base on a 16 KB boundary, rows 12, 25, 38 and 51 of each 64, 30 of the 480. Software that
  writes along a row through the aperture maps the next slot at the boundary; OOLIB maps each pixel by its own
  address.
- **What shows where** is as [above](#what-shows-where): either type may be on either layer, and a sprite's colour
  is still a palette index, over or under a direct 640 layer. A direct 640 layer 1 shows layer 0 and depth-2
  sprites wherever its words have bit 15 set; a direct 640 layer 0 shows depth-1 sprites there, and palette entry 0
  where no other layer covers. Both layers may be direct 640 at once.
- **Through the port:** a row or a whole screen is one stream at PSTEP 1 (both are contiguous). A bitmap 640 column
  is PSTEP 1,024. A direct 640 column is two byte streams at PSTEP 1,280, the low bytes from 2*x* and the high bytes
  from 2*x* + 1, and a fill of one colour is two streams at PSTEP 2, the low byte at the even addresses and the high
  byte at the odd, the DMA's source held on one byte each time (OODOS's function 83, `DM_VFILLW`).

**Costs** (measured in the emulator, [hw/video640.md, "As built"](../../hw/video640.md)): a whole screen through the
port, by DMA from a byte held still, is a cycle a byte: 0.092 s for a bitmap 640 (491,520 bytes) and 0.113–0.116 s for
a direct 640 (614,400), filled by OOLIB's `GX_FRECT`. The emulator draws a frame of one direct 640 layer in about
0.7 ms natively and 0.9 ms as WebAssembly with an opaque picture (1.4 and 1.6 ms when half its pixels are
transparent at random, the worst case), of the 16.7 ms a frame lasts.

The emulator draws both on `rev=2` only. The decision, its dates and the owner's assumption for the real board,
that the card's memory keeps up with a direct 640 layer beside the other layer and the sprites, are in
[hw/video640.md](../../hw/video640.md).

### Scrolling

Status: **emulated**

Each layer has HSCROLL and VSCROLL, 10 bits each, in the layer's own pixels: a tile layer and bitmap 256 scroll in
steps of two screen pixels both ways, a 40-column text layer in steps of two across and one down. A scroll moves the
picture left or up, and it wraps within the layer: within the map for tiles (256 to 1,024 pixels each way), within
640×480 for 80-column text, mono and direct 640, 320×480 for 40-column text, 512×480 for bitmap 512, 256×240 for
bitmap 256, and 1,024×480 for bitmap 640, so a picture up to 1,024 wide scrolls across with no seam.
A text layer has no cells past its 80×30 or 40×30, so scrolling one rotates the screen.

The scroll registers, and every base register, take effect at the start of vertical blank, line 480: to show on the
next frame they must be written before it ([below](#when-changes-show)).

### Apple II mode

Status: **emulated**

In Apple II mode the card draws the IIe's display instead of its layers: text, lo-res, hi-res and the double modes,
from banks 0 and 1, as the north bridge's soft switches say. Each Apple dot is 2×2 pixels, the 560×384 picture
centred on black. The card's registers can't be reached, and the /RESET that entered the mode has set them all back
to their reset values. Text is drawn from the IIe's character ROM, which the BIOS's `C806R` puts in card memory at
`$1E0000`, through the port (*proposed*, revision 2). On a revision 1 card `C806R` refuses and stays in the monitor.
[Chapter 13](13-apple-ii-mode.md) has the mode.

## Sprites

Status: **emulated**

Sprites share the tile layers' 320×240, each sprite pixel shown as 2×2. On revision 2 a sprite may instead be
full resolution, a sprite pixel a screen pixel at 640×480 positions ([below](#full-resolution-sprites)). CTRL bit 3
turns them on. The table is 64
entries of 8 bytes, 512 bytes, at SPRBASE ([`$C483`](#reg-c483)), in VERA's layout:

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 7–0 | the image's address bits 7–0, in 64-byte units of video memory |
| 1 | 7 | 1: 8 bits a pixel; 0: 4 bits |
| 1 | 6–4 | not used |
| 1 | 3–0 | the image's address bits 11–8 |
| 2 | 7–0 | X bits 7–0 |
| 3 | 7 | FULL: on revision 2 while SPRCTL's bit 0 is set, the sprite is full resolution ([below](#full-resolution-sprites)); otherwise ignored |
| 3 | 6–2 | not used: write 0 |
| 3 | 1–0 | X bits 9–8 |
| 4 | 7–0 | Y bits 7–0 |
| 5 | 7–2 | not used: write 0 |
| 5 | 1–0 | Y bits 9–8 |
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
  right half at the left edge. The visible part is X 0–319 and Y 0–239. The unused bits of bytes 3 and 5 are
  ignored; software writes them 0, so that a later card may give them a meaning, as byte 3's bit 7 now has one.
- **On each line** the card draws the 32 lowest-numbered sprites whose rows cover it, depth 0 not counted; any more
  aren't drawn on that line. The 32 are counted whether or not they are on the screen across. A lower-numbered
  sprite is in front of a higher-numbered one, whatever their depths.
- **Collisions.** Where opaque pixels of two drawn sprites fall on the same visible spot and their group masks share
  a bit, that is a collision. At the vertical blank after the frame, STATUS's COLL ([`$C488`](#reg-c488)) gets the
  shared bits of every collision in it, and ISR's bit 2 is set. COLL keeps them until a later frame has a collision.
  No sprites are drawn, and so none collide, while the display is off or the splash covers the screen.

A program keeps its table in ordinary RAM and copies it into video memory in the vertical blank, by DMA, as the
demo does: its 96 bytes take about 27 µs, at two cycles a byte ([chapter 5](05-dma.md#memory-to-memory)), or
one cycle a byte with WIDE and the two addresses of one parity (*proposed*, B4: [chapter 5](05-dma.md#16-bit-transfers)), which nothing sets yet. Two
tables and a SPRBASE write to flip between them also work, but SPRBASE is latched at line 480: written before it, the
flip shows on the next frame; written in the vertical blank, a frame later.

hw/design.md, "Video" ([`hw/design.md:556`](../../hw/design.md#video)), puts a copy of the whole 512-byte table at
about 72 µs. The DMA controller takes two cycles a byte for a memory-to-memory copy
(`DmaController::transfer()` in `emulator/components/devices/dma_controller.cpp`), so it is about 143 µs at 7.16 MHz.
With WIDE and both addresses even it takes two cycles a word, 1 + 512 cycles, about 72 µs.

### Full-resolution sprites

Status: **proposed**

On revision 2 a sprite can be drawn at full resolution, one screen pixel a sprite pixel, at a position in the
screen's own 640×480 ([hw/sprites640.md](../../hw/sprites640.md)). Two things make a sprite full resolution:
SPRCTL's FULL bit ([`$C48E`](#reg-c48e)), which a program sets once, and its own entry's byte 3 bit 7
(`VIDEO_SPRX_FULL`). With SPRCTL 0, as at reset and after OODOS's warm boot, byte 3 bit 7 means nothing and every
sprite is drawn as [above](#sprites), whatever the unused bits hold; so a table written for a card without the
register shows as it did. Sprites of the two sizes can be on the screen at once.

| | A doubled sprite (byte 3 bit 7 clear, or SPRCTL 0) | A full-resolution sprite (SPRCTL 1, byte 3 bit 7 set) |
|---|---|---|
| A sprite pixel | 2×2 screen pixels | 1×1 |
| X and Y | 320×240 units, 10 bits | 640×480 units, 10 bits |
| Visible | X 0–319, Y 0–239 | X 0–639, Y 0–479 |
| Wrap | at 1,024 | at 1,024 |
| Width and height (byte 7's codes) | 8, 16, 32 or 64 sprite pixels: 16–128 screen pixels | 8, 16, 32 or 64 screen pixels |

- **Positions.** X and Y still wrap at 1,024, not at 640 or 480: a 64-pixel full-resolution sprite at X 1,000
  shows its right 40 pixels at the left edge, and one at X 600 its left 40 at the right edge, with nothing at the
  left. X 640–1,023 and Y 480–1,023 are off the screen.
- **The image** is the same bytes at either size: row by row at its card page, 4 or 8 bits a pixel. One image can be
  shown both ways, by the flag alone. The palette offset, the depth, the collision groups and the flips mean what
  they do for a doubled sprite; a flip reverses the sprite's own width or height, so at full resolution it reverses
  screen pixels.
- **On each line** the card draws the 32 lowest-numbered sprites whose rows cover it, of either size, depth 0 not
  counted. A doubled sprite's row covers two lines (its row is (line ÷ 2 − Y) mod 1,024), a full-resolution
  sprite's one ((line − Y) mod 1,024). A lower-numbered sprite is in front of a higher-numbered one, whatever their
  sizes and depths.
- **Depth** is decided at each screen pixel: a full-resolution sprite between the layers shows through layer 1's
  transparent pixels one by one.
- **Collisions** are found in screen pixels. A doubled sprite's opaque pixel covers its four screen pixels, a
  full-resolution sprite's one, and two sprites collide where opaque pixels of both cover one screen pixel and their
  groups share a bit. Two doubled sprites collide exactly as they would with SPRCTL 0; two full-resolution sprites
  side by side (pixels at X 1–8 and 9–16) don't.
- **The pointer.** OODOS's pointer sprite is doubled unless a program asks function 83's `DM_POINTER` for a
  full-resolution one ([the Programmer's Guide, chapter 7](../oodos-programmers-guide/07-function-reference.md#bdos-83)).

**Costs** (measured in the emulator, [hw/sprites640.md, "As built"](../../hw/sprites640.md)): with SPRCTL 0 a frame
takes what it did. The dearest case is 64 doubled sprites of 64 × 64 drawn while SPRCTL is 1 (each doubled pixel then
written twice): about 5.1 ms of the frame's 16.7 natively and 3.8 ms as WebAssembly, over two text layers. The
same table at full resolution costs about what it does doubled.

The emulator draws them on `rev=2` only. The decision, its dates and the owner's assumption for the real board, that
the card fetches 32 full-resolution sprites a line as it does 32 doubled ones, are in
[hw/sprites640.md](../../hw/sprites640.md).

## The palette

Status: **emulated**

The palette is 256 entries of two bytes, 512 bytes, in video memory at PALBASE ([`$C482`](#reg-c482)). Byte 0 is
green in bits 7–4 and blue in bits 3–0; byte 1 is red in bits 3–0, its bits 7–4 not used. That is 12 bits a
colour, 4,096 colours. Every colour of the layers and the sprites is a palette index, BORDER's too, but for a
direct 640 layer's, which is the 12 bits themselves ([above](#bitmap-640-and-direct-640)). The splash and Apple II
mode have colours of their own.

The card reads the palette from memory for each line it draws, so a change to it shows from the next line drawn: a
fade done by DMA in the vertical blank shows whole on the next frame. PALBASE itself is latched at vertical blank,
so a write to it swaps the whole palette between frames.

The BIOS's palette is the CGA's 16 colours, which VINIT writes to entries 0–15 at unit 0 (`video.s`):

<!-- listing: ../../backplane/bios/video.s:453-472 -->
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

hw/design.md, "Video registers" ([`hw/design.md:626–627`](../../hw/design.md#video-registers)), has the card copy
the 512 bytes into its own RAM when PALBASE changes. The emulator reads them from video memory for every line
instead (`emulator/components/devices/video_card.cpp:515–519`), which is what lets a DMA fade show with no PALBASE
write.

## Video memory

Status: **emulated**

On revision 2 (*proposed*) everything the card draws but the splash is in its own 2 MB, reached through the aperture
and the data port: [Revision 2: card memory](#revision-2-card-memory). The next three paragraphs are revision 1's,
the old card, which the BIOS no longer drives (its registers are below as `_R1` entries); the table after them is
BIOS 0.13's, on revision 2.

On revision 1, in native mode, everything the card draws but the splash is in banks 8–B, physical
`$80000–$BFFFF`, 256 KB: the cells and fonts, maps and tiles, bitmaps, the sprite table and images, and the
palette. The card keeps its own copy by watching every write to those banks on the backplane, so it takes no bus cycles. To the CPU and the DMA
controller the banks are ordinary RAM: a program writes them through a window on RAM blocks 32–47, or by DMA, and
can read them back ([chapter 2](02-memory-and-banking.md#physical-memory)). The emulator draws from the RAM itself,
so writes by the CPU, by DMA and from the front panel all show.

The base registers (PALBASE, SPRBASE, MAPBASE, TILEBASE) are one byte, in 1 KB units: unit *u* is physical
`$80000` + *u* × 1,024, which is RAM block 32 + *u* / 16, at (*u* mod 16) × 1 KB into it. Sprite images are in
64-byte units, 12 bits. Every address wraps within the 256 KB, so a map or a bitmap that runs past `$BFFFF` goes on
at `$80000`. Revision 2 moves all of it into the card's own memory
([below](#revision-2-card-memory)).

What BIOS 0.13 puts where, on revision 2, in card addresses (*proposed*; hw/video-rev2.md's layout). The old unit
*u* of banks 8–B is card address *u* × 1 KB, so the BIOS's screen keeps its offsets:

| Card address | What |
|---|---|
| `$000000` | the BIOS's palette, entries 0–15 (`video.s`) |
| `$000400` | the BIOS's font, 4 KB, copied from flash block 30 |
| `$002000` | the BIOS's text screen, 80×30 cells, 4,800 bytes |
| `$040000`–`$13FFFF` | the video demo's (`C803R`, `demo.s`), in the programs' region: [its table](#the-video-demo-on-revision-2) |
| `$1E0000` | Apple II mode's character ROM, 2 KB (`apple.s`) |

The demo keeps clear of `$000000`–`$03FFFF` and of Apple II mode's `$1C0000`–`$1FFFFF`, so the text screen is still
there when it ends. BIOS 0.12, on revision 1, kept the same screen at units 0, 1–4 and 8–12 (`$80000`, `$80400`,
`$82000`), the demo at units 13–36 and the character ROM at `$BE000`.

### Under OODOS

Status: **proposed**, on [revision 2](#revision-2-card-memory), which OODOS is built for (`oodos/include/vmem.inc`,
`oodos/sys/video.s`): its console takes the screen only when the ID reads `$92` and the DMA controller is there. On
revision 1, or with no card, OODOS has no console screen, only the serial line.

**Card memory.** OODOS owns card `$000000–$03FFFF`; a program reads it and never writes it. The console is the
BIOS's text screen, set up again by OODOS's own CRTINIT at cold boot: the palette at `$000000`, the font at
`$000400`, the 80×30 text at `$002000`, the old video units 0–12 (`$000000–$0033FF`); the rest is kept for OODOS.
The program owns `$040000–$1BFFFF`, handed out by OOLIB, for as long as it runs; Apple II mode owns
`$1C0000–$1FFFFF`, never a program's. Function 83's card operations refuse a card destination outside the
program's part, and its pointer is written only while SPRBASE is in it.

**The aperture.** Slot 0 is OODOS's and always shows card bank 0, the console's, where the console writes its
text: since B7 (*proposed*, a `$D2` north bridge) at `$E00000–$E03FFF`, through W1 with WIN1H `$0E` and block 0;
on a `$D1` bridge at `$80000–$83FFF` (block 32). Slot 1, at `$84000–$87FFF` (block 33), or after `GX_BANK8` at
`$E04000–$E07FFF` (WIN1H `$0E` with block 1), is the program's, through OOLIB under the W1 contract
([the Programmer's Guide, chapter 5](../oodos-programmers-guide/05-memory-and-banking.md#the-w1-contract)).
Slots 2 and 3 are OODOS's too, at `$E08000–$E0FFFF` (`$88000–$8FFFF` on `$D1`): function 83's card-to-card copy
borrows them for each piece and puts them back. While AP8 is set for the program, as OODOS leaves it, bank 8 is
those slots, so no physical destination of function 83, and no record read through a DMA segment (function 51),
may touch `$80000–$83FFF` or `$88000–$8FFFF`: OODOS refuses it. DM_INFO's W1 map leaves out blocks 32, 34 and 35:
it is block 1, the blocks the program has allocated, 33, and 36–47 (banks 9–B, ordinary RAM on revision 2). After
the program's `DM_BANK8` bank 8 is its RAM: the refusal lifts and the map takes in blocks 32–39.

**Registers.** A program may write the drawing registers, but not IEN (OODOS's: the vertical blank alone) nor
SPLASH, and of ISR only bit 2 (writing `$04` clears the collision flag; OODOS never enables the collision interrupt,
and turns off a line or collision interrupt a program enables). SLOT0, SLOT2 and SLOT3 and the data port's
registers (PADDR, PDATA, PSTEP, PCTL) are OODOS's; OOLIB writes SLOT1 alone. A program may write SPRCTL
([`$C48E`](#reg-c48e)) to show full-resolution sprites; the warm boot clears it. Collisions are read from STATUS and
ISR: OODOS keeps no copy of them.

**The warm boot** (and function 83's `DM_CONSOLE`) sets the card's registers back as CRTINIT sets them: CTRL `$03`
(the display and layer 0 on), BORDER, PALBASE, SPRBASE and SPRCTL 0, layer 0 80×30 text (CONFIG `$80`) with its map at the
text and its tiles at the font, unscrolled, layer 1's CONFIG 0, IEN the vertical blank alone. The warm boot alone
also turns the port's DMA request off and puts SLOT0–SLOT3 on card banks 0, 16, 2 and 3: slot 1 on the program's
first bank, so block 33 never shows OODOS's part. It copies nothing: the console's memory was never the program's,
so what OODOS printed meanwhile shows.

**The frame handler's copies.** OODOS's vertical-blank copies (from system memory to the program's card memory) and
the pointer sprite's position go through the data port, on DMA channel 1 (its CARD `$09`, the video card's
module), from the frame handler
([chapter 4](04-interrupts.md#under-oodos), [chapter 5](05-dma.md#under-oodos)).

### When changes show

Status: **emulated**

- **Each line** is drawn as the beam reaches the end of its 640 visible pixels, from video memory and the registers
  as they are at that moment. A change to memory, the palette, CTRL, BORDER, CONFIG, PALOFS or SPRCTL shows from
  the next line drawn.
- **At the start of vertical blank**, line 480, the card latches PALBASE, SPRBASE and each layer's MAPBASE,
  TILEBASE, HSCROLL and VSCROLL. Written at any other time, they read back at once but take effect at the next line
  480, so one write flips between two buffers without tearing. To show on the next frame, they must be written
  before line 480; written in vertical blank, they show a frame later.
- **In vertical blank**, lines 480–524, nothing is drawn, and STATUS bit 7 is set. Memory, the palette's contents,
  CTRL, BORDER, CONFIG and PALOFS written then show on the next frame. The demo waits for its start so:

<!-- listing: ../../backplane/bios/demo.s:471-477 -->
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
(`backplane/bios/demo.s:237–244`). The sprites show on the next frame, but the scroll, latched already, shows a frame
later, so it runs a frame behind. CONFIG isn't latched, so the demo writes it a frame after the MAPBASE it goes with
(*proposed*, revision 2's demo: [revision 2](#revision-2-card-memory)).

### The BIOS's text screen

Status: **proposed**, on [revision 2](#revision-2-card-memory), which BIOS 0.13 is built for: it takes the screen
only when the ID reads `$92`. On revision 1, or with no card, VOK stays clear and the monitor is on the serial line
alone; the emulator has both.

At boot VINIT copies the font and the palette in, clears the screen, and ends in VSCREEN, which sets the registers.
Break calls VSCREEN again, after QUIET, so the monitor always comes back to this screen, with its memory as it was
left. VSCREEN does nothing unless both the video card's and the DMA controller's ID bytes answer:

<!-- listing: ../../backplane/bios/video.s:148-178 -->
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

It leaves ISR, LINE and SPLASH alone, and of layer 1 it writes only CONFIG. The font, the palette and the text keep
revision 1's old offsets in card memory (`$000400`, `$000000` and `$002000`, so the base
registers read `$0004`, `$0000` and `$0020`). VINIT writes the font and the palette into the port's PDATA, and
clears the screen with two port transfers by DMA on channel 1 (CARD 9, the card's module), held on one byte with PSTEP 2: the characters, then
the attributes. VPUTC, run for each character, writes it with the CPU through aperture slot 0 (W2 on physical
`$E00000`, WIN2H `$0E` and block 0, since BIOS 0.17 on a `$D2` north bridge, *proposed*; on `$D1`, `$80000`,
block 32; it points SLOT0 at page 0 and puts W2, WIN2H and SLOT0 back after), and scrolls by the port on channel 1: the rows out
of the card into a 2 KB buffer at `$FC000` (under the I/O hole, in RAM block 63) and back in a row higher, then a
fill for the last row. It puts the port's PADDR, PSTEP and PCTL back after, and since BIOS 0.15 the DMA controller's
CONTROL too, which it saves before it selects channel 1 (*proposed*), but stops a program's own transfer on
channel 1. BIOS 0.12, on revision 1, wrote it through W2 on RAM block 32 and scrolled by DMA on channel 3
([chapter 5](05-dma.md#memory-to-memory)); BIOS 0.13 leaves a revision 1 card alone.

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
With a sound card, the BIOS sends the card BOOTSOUND on the vertical blank the splash starts on, so the sound keeps
time with the picture ([chapter 7](07-sound.md#the-boot-sound)). With a `/BOOT.BIN` to boot, AUTOBOOT waits for the bit to clear
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

hw/design.md's register table ([`hw/design.md:606`](../../hw/design.md#video-registers)) says any pending ISR bit
pulls IRQH. The card interrupts on ISR AND IEN (`emulator/components/devices/include/video_card.h:252`), as the
same section's later list says ([`hw/design.md:652`](../../hw/design.md#video-registers)).

## Revision 2: card memory

Status: **proposed**

Revision 2 ([hw/video-rev2.md](../../hw/video-rev2.md)) draws from 2 MB of its own memory instead of banks 8–B,
reached through a 64 KB aperture at physical `$E00000` (`VIDEO_APERTURE2`, four 16 KB slots; since B7, *proposed*,
also at bank 8, `$80000`, `VIDEO_APERTURE`, while the north bridge's AP8 is set) and a data port whose DMA request
channel 1 serves (its CARD `$09` from reset). Its ID reads `$92` (`VIDEO_ID_V2`). It moves the base registers to two bytes in 256-byte units and the
layers' registers up a byte; the `_R1` equates are revision 1's names where they moved. Every byte it doesn't name
reads 0. The emulator has it (`rev=2` on the machine's video line, as `backplane.machine` has it), and BIOS 0.13
and OODOS drive it and nothing older: on a revision 1 card the BIOS's text screen stays off, `C806R` refuses, and
OODOS has no console screen. Revision 1 is still documented here, as the old card; the emulator can still build
one (`rev=1`).

Native mode no longer snoops anything, and banks 9–B are ordinary RAM again: bank 8 is the aperture while AP8 is
set, and RAM once it is clear ([chapter 2](02-memory-and-banking.md#revision-2-bank-8-the-aperture)).
Apple II mode still snoops banks 0–1 ([below](#apple-ii-mode-on-revision-2)). The layers, sprites, palette, splash,
interrupts and timing are as revision 1 draws them; what changes is where their data is, how it is reached, and
where some registers sit. The BIOS's use of it is in [Video memory](#video-memory) and [The BIOS's text
screen](#the-bioss-text-screen), OODOS's in [Under OODOS](#under-oodos).

The design record, for the reasons: [hw/design.md, "Video card revision
2"](../../hw/design.md#video-card-revision-2), and [hw/video-rev2.md](../../hw/video-rev2.md), whose register map and
card memory layout are frozen.

### Card memory

Status: **proposed**

Card memory is 2 MB, card addresses `$000000–$1FFFFF`, 24 bits wide so that a larger card needs no new software.
MEMSIZE (`$C4BF`) gives what is fitted in 64 KB units: 32 for 2 MB. An aperture or port address past it reads `$FF`,
and a write to it is lost.

- **Base registers** (PALBASE, SPRBASE, each layer's MAPBASE and TILEBASE) are two bytes, low byte first, in
  256-byte units: a base of *b* is card address *b* × 256, so they reach 16 MB. Both bytes are latched at line 480,
  as revision 1's one byte was: to flip in one frame, write both before it.
- **Sprite images** are 15 bits in 256-byte units ([below](#the-sprite-table-on-revision-2)).
- **The palette** is in card memory at PALBASE, read for each line as revision 1's is in the emulator, so a fade
  streamed through the port in the vertical blank shows whole on the next frame.
- **/RESET** sets the registers back and never touches card memory.

How the 2 MB is shared:

| Card addresses | Whose | What |
|---|---|---|
| `$000000–$03FFFF` | OODOS, and the BIOS before it | what revision 1 keeps in banks 8–B, at the same offsets: the palette at `$000000`, the font at `$000400`, the text screen at `$002000`, OODOS's video units 0–12; the pointer's sprite table is a program's, in its part, as function 83's `DM_POINTER` writes it |
| `$040000–$1BFFFF` | programs, through OOLIB | screens, bitmaps, tiles, sprite images: 1.5 MB |
| `$1C0000–$1DFFFF` | Apple II mode | its copy of banks 0–1, 128 KB |
| `$1E0000–$1E0FFF` | Apple II mode | the IIe's character ROM, which `C806R` puts there |
| `$1E1000–$1FFFFF` | Apple II mode | spare |

A revision 1 unit *u*, 1 KB at `$80000` + *u* × 1,024, is card address *u* × 1,024 on revision 2, and its base
register value is *u* × 4: the text screen at unit 8 is `$002000`, base `$0020`.

### The aperture

Status: **proposed**

The aperture is physical `$E00000–$E0FFFF` (`VIDEO_APERTURE2`). It is four slots of 16 KB, and slot *n* shows card
memory from SLOT*n* × `$4000`. SLOT0–SLOT3 (`$C4B0–$C4B7`) are two bytes each, low byte first, a 16 KB page number.
A window with WIN*x*H `$0E` (`VIDEO_APER_H`) and block *n* shows slot *n*, on a revision 2 north bridge (ID `$D2`,
*proposed*, B7: [chapter 11](11-north-bridge-and-rtc.md#the-high-window-registers-and-map)). While the bridge's AP8
is set, as from reset, bank 8 is the aperture too, `$80000–$8FFFF` (`VIDEO_APERTURE`), with no RAM answering: a
window pointed at block 32 + *n* ([chapter 2](02-memory-and-banking.md#the-four-windows)) shows slot *n*, so a
program can have a sprite table in one window and part of a bitmap in another.

- **Reads and writes** are ordinary memory cycles, with no wait states at 7.16 MHz. The aperture answers word
  cycles too (DMA with WIDE, *proposed*, B4: [chapter 5](05-dma.md#16-bit-transfers)), both bytes of the word, as
  any memory does: a WIDE copy of 256 bytes into it takes 1 + 256 cycles.
- **At /RESET** SLOT*n* is *n*: the aperture shows card memory `$000000–$00FFFF` in order, so slot 0 (`$E00000`,
  or block 32 while AP8 is set) is the palette, the font and the text screen, and the BIOS reaches the screen with
  nothing set up first.
- **Paging** a slot is two register writes; until both are written the slot shows the page the two bytes make, so
  move a slot only while nothing reads through it.
- **In Apple II mode** the card ignores the aperture.

### The data port

Status: **proposed**

The port moves bytes to or from card memory with no window at all, a byte for each PDATA access.

| Register | Address | What |
|---|---|---|
| PADDR | `$C4B8–$C4BA` | the card address, 24 bits, low byte first |
| PDATA | `$C4BB` | a write stores the byte at PADDR; a read returns the byte there. Either then moves PADDR by PSTEP |
| PSTEP | `$C4BC–$C4BD` | the step, 16 bits, low byte first; 0 holds the address. /RESET sets 1 |
| PCTL | `$C4BE` | bit 0: DRQ enable. Bit 1: the direction, 0 to the card (DMA writes PDATA), 1 from the card (DMA reads it). Bit 2: PADDR steps down. The other bits read 0 |

- **The step** lets one stream write a column or a rectangle: 160 for a column of the 80-column text screen (80
  cells of 2 bytes a row), 256 for a column of bitmap 256 or bitmap 512 (256 bytes a row), 80 for one of mono,
  1,024 for one of bitmap 640, and 1,280 for direct 640's, in two passes: the low bytes, then the high bytes.
  PSTEP 0 holds the card address, so every access is the same byte. The BIOS's fills step 2, with the DMA's
  source held on one byte.
- **PADDR wraps** at the end of the fitted memory, up or down. Set at or past that end, it reads back as written and
  steps as a plain 24-bit counter (each PDATA read giving `$FF`, each write lost) until it wraps from `$FFFFFF` to
  `$000000`, inside the memory again.
- **By DMA:** with PCTL's bit 0 set the card pulls /DREQ, by its module 9, whenever the port can take or give a
  byte, and channel 1, whose CARD is `$09` from reset, serves it, so the controller streams system RAM into card
  memory, or back, fly-by (*proposed*: revision 1's controller had a DRQ line wired to channel 1 instead)
  ([chapter 5](05-dma.md#channel-1-the-video-cards-data-port)). PCTL's bit 1 must agree with the channel's DIR: a DMA
  cycle against it moves nothing (a read gives `$FF`, a write is dropped) and leaves PADDR where it was.
- **A word grant** (channel 1's CARD with WIDE, `$19`; *proposed*, B4, [chapter 5](05-dma.md#16-bit-transfers)) is
  two port accesses in order: the even byte (D0–D7) at PADDR, a PSTEP step, then the odd byte (D8–D15), another
  step. So every PSTEP and PCTL meaning holds: PSTEP 160 puts the two bytes in a column, PCTL's bit 2 takes both steps
  down, and PADDR wraps as for a byte. PSTEP 0 keeps one byte: a word to the card leaves its odd byte at PADDR, and
  a word from it reads that byte twice. Against PCTL's direction a word reads `$FFFF`, a write is dropped and PADDR
  doesn't step. A word request means the port has room for two bytes; the emulated port always has. A 4 KB load
  from RAM takes 2,050 cycles this way, against 4,098 a byte at a time. This is an addition beside
  [hw/video-rev2.md](../../hw/video-rev2.md#data-port-and-dma), which has PDATA a byte
  ([hw/design.md](../../hw/design.md#16-bit-transfers)). Neither the BIOS nor OODOS sets WIDE yet.
- **Within card memory,** a scroll or a moved sprite image goes out through the port and back in; the card has no
  copy engine yet.

### Registers that move

Status: **proposed**

Revision 2 keeps the block at `$C480–$C4FF` and CTRL, BORDER, IEN, ISR, LINE, STATUS and SPLASH where they are. The
rest move to make room for two-byte bases:

| Revision 1 | Revision 2 |
|---|---|
| PALBASE `$C482`, one byte | PALBASE `$C48A–$C48B`; `$C482` reads 0 |
| SPRBASE `$C483`, one byte | SPRBASE `$C48C–$C48D`; `$C483` reads 0 |
| layer +1 MAPBASE, +2 TILEBASE | +1–2 MAPBASE, +3–4 TILEBASE |
| layer +3–4 HSCROLL, +5–6 VSCROLL | +5–6 HSCROLL, +7–8 VSCROLL |
| layer +7 PALOFS | +9 PALOFS |
| none | SLOT0–SLOT3 `$C4B0–$C4B7`, PADDR `$C4B8`, PDATA `$C4BB`, PSTEP `$C4BC`, PCTL `$C4BE`, MEMSIZE `$C4BF` |
| none | SPRCTL `$C48E` (*proposed*, [hw/sprites640.md](../../hw/sprites640.md)) |
| ID `$91` | ID `$92` |

`$C48F`, each layer's +A–+F and `$C4C0–$C4FE` read 0. Software that knows only revision 1 tests the ID and
leaves a `$92` card alone.

### The sprite table on revision 2

Status: **proposed**

The entry keeps revision 1's 8 bytes and their layout but for the image address, which takes byte 1's spare bits: 15 bits
in 256-byte units, reaching 8 MB.

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 7–0 | the image's unit number, bits 7–0 |
| 1 | 7 | 1: 8 bits a pixel; 0: 4 bits |
| 1 | 6–0 | the unit number's bits 14–8 |
| 2–7 | | as revision 1 ([Sprites](#sprites)), byte 3's bit 7 the FULL flag ([Full-resolution sprites](#full-resolution-sprites)) |

The image is at the unit number × 256 in card memory.

### The video demo on revision 2

Status: **proposed**

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

### Apple II mode on revision 2

Status: **proposed**

Apple II mode draws as [above](#apple-ii-mode). The card still watches writes to banks 0–1 and the display
switches on the backplane, its one snoop path now, and keeps its copy in card memory at `$1C0000–$1DFFFF`. It draws
text from the character ROM at `$1E0000`, which `C806R` copies there through the port. Its registers can't be
reached, it ignores the aperture, and DMA is idle, so the port isn't used in the mode. Card memory survives the
/RESET that enters it and every Ctrl-Reset ([chapter 13](13-apple-ii-mode.md#revision-2-of-the-video-card)). The
emulator draws the mode from banks 0–1 of its RAM directly, as it does with revision 1, so it keeps no copy at
`$1C0000`; only the character ROM comes from card memory.

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
contents are read for each line, so they need no PALBASE write to show ([The palette](#the-palette)). BIOS 0.12
wrote `$00` at boot and on Break, and its demo used unit 13; BIOS 0.13 doesn't drive revision 1.

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

Latched at line 480, so a write in vertical blank shows a frame later. BIOS 0.12 wrote `$00` at boot and on Break,
and its demo used unit 36; BIOS 0.13 doesn't drive revision 1.

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

Revision 1's is [VIDEO_PALBASE_R1](#reg-c482). BIOS 0.13 writes `$0000` at boot and on Break (card address
`$000000`, its palette).

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

Revision 1's is [VIDEO_SPRBASE_R1](#reg-c483). BIOS 0.13 writes `$0000` at boot and on Break. On revision 2 an entry's image address is 15 bits of 256-byte units: byte 0, then byte 1's bits 6–0; byte 1's bit 7 is still the colour depth.

<a id="reg-c48e"></a>
### $C48E VIDEO_SPRCTL: full-resolution sprites, on revision 2

#### Equates

`VIDEO_SPRCTL`

#### Module

9, the video card

#### Access

R/W, on [revision 2](#revision-2-card-memory) only. Not latched.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0; a write is ignored |
| 0 | FULL | 1: a sprite entry's byte 3 bit 7 makes that sprite full resolution; 0: byte 3 bit 7 is ignored |

#### Reset

`$00`: every sprite doubled, as on a card without the register.

#### Status

**proposed**

#### Notes

SPRCTL isn't latched at line 480, as SPRBASE is: a write shows from the next line drawn, in the same frame, as
CTRL's does, so lines drawn before it keep the sizes they had (written as the beam starts line 200, lines 0–199 of
that frame are drawn as before and lines 200–479 by the new setting). A program sets it once, before its first
table. backplane.inc
names `VIDEO_SPR_FULL` (`$01`, this register's bit) and `VIDEO_SPRX_FULL` (`$80`, the entry's byte 3 bit 7).
OODOS's warm boot and function 83's `DM_CONSOLE` write 0, so one program's setting never reaches the next; the BIOS
leaves it alone, and only /RESET clears it under the monitor. What a full-resolution sprite is: [Full-resolution
sprites](#full-resolution-sprites). Revision 1 has no SPRCTL (`$C48E` reads `$FF` there, as every address it doesn't answer) and draws every
sprite doubled.

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
| 2–0 | TYPE | 0 text 80×30, 1 text 40×30, 2 tiles, 3 bitmap 256, 4 bitmap 512, 5 mono; revision 2, 6 bitmap 640, 7 direct 640 (*proposed*); revision 1, 6–7 nothing |

#### Reset

`$00`: text 80×30.

#### Status

**emulated**

#### Notes

A layer type ignores the bits that aren't its own, and every bit reads back as written. backplane.inc names
`VIDEO_TEXT80B` (`$80`: text 80×30, blinking), `VIDEO_TEXT40` (`$01`), `VIDEO_TILE` (`$02`: 8×8 tiles at 4 bits) and
`VIDEO_MAP_W64` (`$20`), and for revision 2 `VIDEO_BITMAP640` (`$06`), `VIDEO_DIRECT640` (`$07`) and
`VIDEO_DIRECT_T` (`$80`, direct 640's transparent bit, in a pixel's byte 1). The BIOS writes `$80` at boot and on
Break, and never types 6 or 7. [Modes and layers](#modes-and-layers) has the types.

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

Latched at line 480, so one write flips between two pages; a write in vertical blank shows a frame later. BIOS
0.12 wrote 8 (`$82000`) at boot and on Break; BIOS 0.13 writes revision 2's [VIDEO_L0_MAP](#revision-2-card-memory)
here, `$0020` (card address `$002000`).

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

Latched at line 480, so a write in vertical blank shows a frame later. BIOS 0.12 wrote 1 (`$80400`, its font) at
boot and on Break; BIOS 0.13 doesn't drive revision 1 (on revision 2 it writes layer 0's TILEBASE, `$0004`: card
address `$000400`).

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

The BIOS doesn't write it: after Break it is as a program left it (BIOS 0.12's demo left 16).

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

The BIOS doesn't write it: after Break it is as a program left it (BIOS 0.12's demo left 1, the BIOS's font).

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

All: a 16 KB page of card memory: aperture slot 0, physical `$E00000`–`$E03FFF` (bank 8's `$80000`–`$83FFF` while AP8 is set), shows card memory from SLOT0 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

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

All: a 16 KB page of card memory: aperture slot 1, physical `$E04000`–`$E07FFF` (bank 8's `$84000`–`$87FFF` while AP8 is set), shows card memory from SLOT1 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

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

All: a 16 KB page of card memory: aperture slot 2, physical `$E08000`–`$E0BFFF` (bank 8's `$88000`–`$8BFFF` while AP8 is set), shows card memory from SLOT2 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

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

All: a 16 KB page of card memory: aperture slot 3, physical `$E0C000`–`$E0FFFF` (bank 8's `$8C000`–`$8FFFF` while AP8 is set), shows card memory from SLOT3 × `$4000`. A page past MEMSIZE reads `$FF` and ignores writes.

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

#### Notes

A DMA word grant (WIDE, *proposed*, B4) is two port accesses, even byte first, each stepping PADDR by PSTEP
([the data port](#the-data-port)).

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
| 0 | DRQ | `VIDEO_PCTL_DRQ`: ask for DMA by module 9 (/DREQ), which channel 1 serves at reset |

#### Reset

`$00`.

#### Status

**proposed**

#### Notes

A DMA grant cycle (module 9 on PS0–PS3, /DMA low) in the other direction from bit 1's moves nothing.

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
