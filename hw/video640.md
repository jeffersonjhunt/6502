### Video card revision 2: bitmap 640 and direct 640 (layer types 6 and 7) ###

**Proposed (2026-11-20).** Two 640×480 colour layer types for the revision 2 video card, in the TYPE values its
CONFIG left spare. This record is an addition beside [video-rev2.md](video-rev2.md), whose register map and card
memory layout stay frozen and which this work does not edit, as [design.md's "16-bit
transfers"](design.md#16-bit-transfers) is for the port's word grants. The definition programs and drivers use is
the Technical Reference's [chapter 6, "Bitmap 640 and direct
640"](../docs/oo1-technical-reference/06-video.md#bitmap-640-and-direct-640); OOLIB's side is in
[oolib.md, "640×480 colour layers, interface version 13"](oolib.md#the-plan).

#### The owner's decisions ####

0. **(2026-11-20) Two new layer types** in the spare TYPE values 6 and 7 of revision 2's CONFIG: type 6, 640×480 at
   8 bits a pixel through the palette (256 of 4,096 colours); type 7, 640×480 at 16 bits a pixel, direct 12-bit
   colour (all 4,096), no palette.
1. **(2026-11-20) Rows:** type 6 a row every 1,024 bytes (pixel at y × 1,024 + x; 480 KB a screen); type 7 rows
   packed, 1,280 bytes (600 KB a screen; a double buffer, 1.2 MB, fits in the program area).
2. **(2026-11-20) When:** its own step, after GW-BASIC 7e4 merged.
3. **(2026-11-20) Type 7's pixel:** RGB444 in a little-endian 16-bit word, `$0RGB` (byte 0 GGGGBBBB, byte 1
   0000RRRR), the palette entry's layout; 4,096 colours, direct, no palette lookup; the output stays 12-bit. RGB565
   with a wider output was offered and declined.
4. **(2026-11-20, "recommendations as listed")** the plan's questions:
   - **Names:** "bitmap 640" (type 6) and "direct 640" (type 7); OOLIB's `GB_640` and `GB_DIRECT`; mkasset's
     `bitmap640` and `direct640`; backplane.inc's `VIDEO_BITMAP640` and `VIDEO_DIRECT640`; the debugger's panel
     shows `bitmap 640` and `direct 640`.
   - **Type 6's width:** the layer is 1,024×480 with 640 shown; HSCROLL wraps at 1,024; `GB_640` is 1,024 wide;
     mkasset takes PNGs up to 1,024 wide.
   - **Type 7's transparency:** bit 15 set (`$8000`) is transparent; bits 12–14 are ignored when read and written
     0; `$0000` is opaque black; mkasset writes a pixel of alpha below 128 as `$8000`.
   - **Type 6's file:** the card's image, 1,024-byte rows (491,520 bytes for 480 rows); GX_ASSET unchanged but for
     its limit.
   - **Word fills:** OODOS's function 83 gains operation 12, `DM_VFILLW` (two port streams at PSTEP 2); OOLIB
     interface 13.
   - **How software knows:** types 6 and 7 are part of revision 2's definition (ID `$92`): no revision 2 board
     exists, and the emulator is the only one. No FEATURES register.
   - **Versions:** OOLIB interface 13, and OODOS 0.12 as the step's last commit.
   - **GX_ASSET's limit:** every part, up to the program's area of card memory, 1,572,864 bytes.
   - **PALOFS** is not used on type 6, as on bitmap 256.
   - **Two type 7 layers at once** are allowed (the assumption below covers it).
   - **mkasset's quantizer:** the existing `--quantize` (the most used colours, the rest to the nearest) for
     type 6; photographs go to type 7.
   - **The record:** this file, the Technical Reference's chapter 6, a pointer in design.md's "Video registers"
     and an entry in oolib.md; video-rev2.md untouched.

5. **(2026-11-20, answers 16-19 at U5)** FT_INFO's interface byte reads 13 wherever a test expects it, SYSINFO's
   reports too; OODOS 0.12's version expectations approved in advance; function 83's "no such operation" case moves
   to operation 13; and **mkasset's direct640 pads a PNG narrower than 640 with the transparent word `$8000`**, as
   bitmap640's 0 bytes are transparent on type 6: what is past a picture shows what is under it, on either type.

#### The owner's assumption for the real board ####

**The video card keeps up** (decision 0, "assume the video card can keep up"): scanout of a direct 640 layer, 1,280
bytes a line (36.9 MB/s), beside the other layer (two direct 640 layers: 2,560 bytes a line, 73.7 MB/s) and the
sprites, on the one 16-bit SRAM that the aperture and the port share, is taken as met. It is an assumption, not a
measurement: no revision 2 board exists. If a board can't, the fallback is to allow type 7 on one layer at a time,
which changes only the definition, not the software.

#### The definition ####

| | Type 6, bitmap 640 | Type 7, direct 640 |
|---|---|---|
| Pixel | a byte, a palette index | a word, `$0RGB` low byte first: byte 0 GGGGBBBB, byte 1 0000RRRR |
| Colours | 256 of 4,096, from PALBASE | all 4,096, no palette |
| Row | 1,024 bytes (4 card pages) | 1,280 bytes (5 card pages), packed |
| Pixel's card address | MAPBASE × 256 + y × 1,024 + x | MAPBASE × 256 + y × 1,280 + 2x |
| The layer's own pixels | 1,024 × 480, 640 shown | 640 × 480 |
| A screen | 491,520 bytes (1,920 pages) | 614,400 bytes (2,400 pages) |
| Where it shows | x 0–639, the whole screen: BORDER nowhere | the same |
| HSCROLL wraps at | 1,024 | 640 |
| VSCROLL wraps at | 480 | 480 |
| PALOFS | not used | not used |
| Transparent | a pixel of 0 | bit 15 set |
| TILEBASE, CONFIG bits 7–3 | not used; read back as written | the same |

- **Why type 7 uses the palette's byte order:** the word a program stores, a palette entry and a pixel are one
  value, `$0RGB`, so the emulator's `rgba()`, mkasset's `entry_bytes()` and OOLIB's palette bytes serve both, and
  the card's colour path after the palette is unchanged (12 bits in).
- **The compositor's order** is unchanged: border or entry 0, depth-1 sprites, layer 0, depth-2 sprites, layer 1,
  depth-3 sprites. A sprite's colour stays a palette index. Collisions are sprites only, as before.
- **The aperture:** a type 6 row on a 1 KB boundary never crosses a 16 KB slot. Type 7's rows are 5 pages apart and
  5 is invertible mod 64, so a slot boundary falls inside 4 rows in every 64 (rows 12, 25, 38 and 51 of each 64 from
  a base on 16 KB), 30 of the 480. A word never straddles.
- **The port:** a row or a screen is one stream at PSTEP 1; a type 6 column PSTEP 1,024; a type 7 column two byte
  streams at PSTEP 1,280; a type 7 colour fill two streams at PSTEP 2, the DMA's source held on one byte each
  (function 83's `DM_VFILLW`).
- **The BIOS and the console** are unchanged: the BIOS never writes CONFIG 6 or 7, and Break, the warm boot and
  `DM_CONSOLE` write layer 0 `$80` and layer 1 0 as before.
- **Revision 1** keeps 6 and 7 as nothing (drawn as if the layer were off).

#### The emulator ####

`VideoCard::draw_layer` draws type 6 by `draw_bitmap640` and type 7 by `draw_direct640`, on revision 2 only. The
line is still built in `int16_t` values: -1 uncovered (BORDER), 0 transparent, 1–255 a palette entry, and
`$1000`–`$1FFF` a direct colour, so the compositor's "covers" tests place it as any layer pixel; its last step looks
a direct colour up in a table of the 4,096 `rgba()` values built once. No save state exists for the card, so there is
nothing to migrate.

**Frame time, measured** (µs a frame, the card alone, a standalone benchmark of `VideoCard` built native at -O2 and
as WebAssembly run by node; a frame is 16,683 µs):

| Layers on | Native | WebAssembly |
|---|---|---|
| text 80 (the console, for scale) | 2,126 | 1,294 |
| bitmap 256 | 540 | 693 |
| type 6 | 552 | 745 |
| type 7 | 670 | 851 |
| text 80 + text 80 + 32 sprites | 3,983 | 2,148 |
| type 7 + text 80 + 32 sprites | 2,470 | 1,787 |
| type 6 + type 7 + 32 sprites | 901 | 1,142 |
| type 7 + type 7 + 32 sprites | 963 | 1,234 |

These were measured on a prototype before the build; the as-built figures are below.

#### What it adds beside video-rev2.md ####

- CONFIG's TYPE 6 and 7 on revision 2 (video-rev2.md leaves them as revision 1 has them, nothing).
- backplane.inc's `VIDEO_BITMAP640`, `VIDEO_DIRECT640` and `VIDEO_DIRECT_T`; no register moves, and no register is
  added.
- OODOS's function 83 operation 12, `DM_VFILLW`; OOLIB interface 13 (`GB_640`, `GB_DIRECT`, word colours,
  `GX_FGH` and `GX_BGH`, GX_ASSET's limit); mkasset's `bitmap640` and `direct640`; VIEW relinked.
