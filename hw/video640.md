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

These were measured on a prototype before the build; the as-built figures are below ("As built").

#### What it adds beside video-rev2.md ####

- CONFIG's TYPE 6 and 7 on revision 2 (video-rev2.md leaves them as revision 1 has them, nothing).
- backplane.inc's `VIDEO_BITMAP640`, `VIDEO_DIRECT640` and `VIDEO_DIRECT_T`; no register moves, and no register is
  added.
- OODOS's function 83 operation 12, `DM_VFILLW`; OOLIB interface 13 (`GB_640`, `GB_DIRECT`, word colours,
  `GX_FGH` and `GX_BGH`, GX_ASSET's limit); mkasset's `bitmap640` and `direct640`; VIEW relinked.

#### As built (2026-11-20, branch feat/video-640) ####

**Status: built in the emulator; proposed for the board** (OODOS 0.12, OOLIB interface version 13).

- **The units:** U1 the record and the Technical Reference's definition; U2 the emulator's two renderers (revision 2
  only), then drawn without a branch (below); U3 the debugger's names; U4 mkasset's `bitmap640` and `direct640`;
  U5 function 83's `DM_VFILLW` (operation 12, `DM_OPS` 13) and interface 13; U6 OOLIB's `GB_640`, `GB_DIRECT`,
  word colours, `GX_FGH`/`GX_BGH` and GX_ASSET's limit; U7 VIEW relinked (no code change); U8 the books and OODOS
  0.12. The cards (`emulator/test/host/sd`) were rebuilt after U5, U6 and U8 (answer 21).
- **The owner's later answers** (2026-11-20, at U5, answers 20-23): the three tests that patch FT_INFO to an older
  interface search for the byte 13; the cards rebuilt at the end of each unit that changes a file on them;
  `GX_FRECT` on a direct 640 bitmap uses `DM_VFILL` when the colour's two bytes are equal, `DM_VFILLW` otherwise;
  GX_ASSET's record counter carries into its third byte (a part past record 65,535), tested with a sparse file
  (test_oodos_gfx, `an_asset_part_past_record_65535_loads`: record 0 and records 65,528-65,591 on B:, no 8 MB file).
- **Found on the way:** `GX_FRECT`'s test for a whole-width rectangle added the width's high byte with the
  compare's carry, not the add's; it held for widths of 256, 512 and 1,024, whose low byte rolls over to 0, and not
  for 640, so a full-width direct 640 fill went a row at a time. Fixed in U6 (G2 guards it).
- **Byte for byte:** GWBASIC.COM, GWBASIC.OVL and OOUI.BNK are main's (ffa7305f) at every unit; VIEW.COM's own
  object links with main's library into main's VIEW.COM, so it differs only in the library's modules (gxasset).

**Frame time, as built** (µs a frame, the card alone, the plan's benchmark; native g++ -O2, and WebAssembly in the
repo's emsdk image run by node). Random words make half of a direct 640 layer transparent at random, which no
branch predictor learns: the worst case. An opaque picture (every word's bit 15 clear) is what a photograph is.
Runs differ by about 5%.

| Layers on | Native, random | Native, opaque | WASM, random | WASM, opaque |
|---|---|---|---|---|
| text 80 (the console, for scale) | 2,390 | | 1,385 | |
| bitmap 256 | 588 | | 768 | |
| type 6 | 631 | 645 | 740 | 745 |
| type 7 | 1,399 | 679 | 1,617 | 884 |
| text 80 + text 80 + 32 sprites | 4,405 | | 2,292 | |
| type 7 + text 80 + 32 sprites | 3,399 | 2,885 | 1,864 | 1,966 |
| type 6 + type 7 + 32 sprites | 1,710 | 952 | 1,881 | 1,141 |
| type 7 + type 7 + 32 sprites | 2,068 | 971 | 1,673 | 1,287 |

With the branch the first build had (bit 15 tested, then what is below), random words cost 2,037 native and 2,425
WebAssembly for a type 7 layer; the select brought them to 1,313-1,399 and 1,617, the opaque case unchanged.

**The library and OODOS, as built** (test_oodos_gfx and test_oodos_video, the fast build, recorded, not gated but
for G2's 0.15 s):

| What | Measured |
|---|---|
| A whole direct 640 screen by `GX_FRECT`, `$0F00` (`DM_VFILLW`, 10 calls) | 828,508 cycles, 0.116 s |
| The same in black, clear or `$0F0F` (`DM_VFILL`, 10 calls) | 806,824-809,934 cycles, 0.113 s |
| A whole bitmap 640 (1,024 × 480) by `GX_FRECT` (`DM_VFILL`, 8 calls of 64 rows) | 662,137 cycles, 0.092 s |
| A bitmap 640's 640 shown columns (a `DM_VFILL` a row, 480) | 1,525,797 cycles, 0.213 s |
| 65,536 bytes by one `DM_VFILLW`, with L4TEST's command line | 145,322 cycles |
| A 600 KB part by `GX_ASSET` from an `oodos-8m` drive | 0.67 s |
| VIEW of a bitmap 640 (480 KB) and of a direct 640 (600 KB), from the command to "Press a key" | 0.91 s, 1.02 s |
| `GX_PLOT`, its first instruction to its return: bitmap 256, bitmap 640, direct 640 | 406, 443, 498 cycles |
| `GX_PLOT` on direct 640 where it maps the next slot (row 12, x 511 then 512) | 514 cycles |

The loads are slower than the plan's estimate (about 0.28 s for 600 KB): each 4 KB piece is a `DM_LOAD` and a
`DM_VWRITE`, two function calls and their checks; loading straight to the card by DMA (OOLIB's question 13) would
take most of that away.

**Memory:** a bitmap 640 screen is 1,920 card pages and a direct 640 2,400, of the program's 6,144: three bitmap
640s, or two direct 640s (a double buffer, 1,200 KB, with 336 KB left), as planned. **Code:** OODOS.SYS 42,752 →
42,880 bytes (`DM_VFILLW` and the step in the port's loop); `gxbmp` 2,715 → 3,035 bytes of code and 20 → 33 of
tables, its BSS +3; `gxtext` 315 → 326, BSS +2; `gxasset` 877 → 882 (VIEW.COM 6,308 → 6,313); `video_card.cpp` +~60 lines and a 16 KB table.

