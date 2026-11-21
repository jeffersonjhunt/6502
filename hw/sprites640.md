### Video card revision 2: full-resolution sprites ###

**Proposed (2026-11-20).** Sprites drawn one screen pixel a sprite pixel, at 640×480 positions, on the revision 2
video card, as an addition that changes nothing any program writes today. This record is an addition beside
[video-rev2.md](video-rev2.md), whose register map and card memory layout stay frozen and which this work does not
edit, as [video640.md](video640.md) is for layer types 6 and 7. The definition programs and drivers use is the
Technical Reference's [chapter 6, "Full-resolution sprites"](../docs/oo1-technical-reference/06-video.md#full-resolution-sprites)
and its register entry [`$C48E` VIDEO_SPRCTL](../docs/oo1-technical-reference/06-video.md#reg-c48e); OOLIB's side
is in [oolib.md, "Full-resolution sprites, interface version 14"](oolib.md#the-plan).

#### The owner's decisions ####

0. **(2026-11-20) The request:** full-resolution sprites (1×1 pixels, 640×480 positions), as an addition that
   breaks nothing.
1. **(2026-11-20, "recommendations as listed")** the plan's questions:
   1. **The mechanism:** a per-sprite flag, the entry's byte 3 bit 7 (FULL), counted only while SPRCTL (`$C48E`,
      revision 2; reset 0; cleared by OODOS's warm boot and `DM_CONSOLE`) bit 0 is set. A full-resolution sprite's
      pixels are 1×1, its X and Y in 640×480 units (10 bits, wrapping at 1,024; visible X 0–639, Y 0–479). A flag
      alone was declined (a program that put anything in byte 3's unused bits would change), and so was a global
      switch (no mixing; the pointer would need scaling; as CTRL bit 4 every GX_SHOW and warm boot clears it).
   2. **Collisions** in screen pixels (640×480): exact for full-resolution sprites, and identical to today's for
      two doubled sprites. The 320×240 cells were declined.
   3. **The real board:** the owner's assumption for the 640 layers ("assume the video card can keep up") extends to
      this: full-resolution sprites at the same 32 a line, of either size ([below](#the-owners-assumption-for-the-real-board)).
   4. **When:** now, on `feat/sprites640` from main `77cc7cf6`, beside GW-BASIC 7e5; OOLIB interface 14.
   5. **A full-resolution pointer** in this step: function 83's `DM_POINTER` with 64 + the sprite gives a 1×
      pointer (about 20 bytes of OODOS); OOUI keeps its doubled pointer.
   6. **mkasset and VIEW:** `mkasset.py sprites --table --full`; VIEW sets SPRCTL whenever it shows a table.
   7. **OOLIB's calls:** a new module, `gxsprf.s`, with `GX_SPRFULL` (A 1 on, 0 off; `GE_VERSION` below interface
      14) and `GX_SPRFPOS` (a 640-unit position that also makes the sprite full resolution). GX_SPRPOS is unchanged
      (it makes the sprite doubled), so `gxspr.s` and OOUI.BNK stay byte for byte. A `GX_SPRSIZE` beside a
      GX_SPRPOS that keeps the bit was declined: it changes `gxspr.s`, and with it OOUI.BNK.
   8. **Versions:** OOLIB interface 14; OODOS 0.13 as the step's last commit after review.
   9. **The BIOS** unchanged: /RESET clears SPRCTL, nothing the monitor shows uses it, and OODOS's warm boot clears
      it.
   10. **Names:** "full-resolution sprite" in the books; `VIDEO_SPRCTL` (`$C48E`) with bit FULL; the entry's byte 3
       bit 7 FULL (`VIDEO_SPRX_FULL`); `GX_SPRFULL` and `GX_SPRFPOS`; mkasset's `--full`; the debugger panel's
       `full`. The record is this file, the Technical Reference's chapter 6, a pointer in design.md's "Video
       registers" and an entry in oolib.md; video-rev2.md untouched.
2. **(2026-11-20) Expectation changes approved,** the plan's five: the reserved-bytes test (`$0E` no longer
   reserved); FT_INFO's `0D 0F` reads `0E 0F`; the interface-patching helpers search for the byte 14;
   `DM_POINTER` 64 is taken and the refusal moves to 128; and OODOS 0.13's version lines, wherever 0.12 appears as
   an expectation, as the 0.11 → 0.12 release had them (greetings, version words, PIP's and OOLDR's banners,
   SYSINFO's goldens).
3. **(2026-11-20, at U7, answer 11: "fix 1")** OOLIB's archive lists its modules in byte order under any locale
   (oodos/Makefile's `LIBSRC` sorted, as `UISRC` is): the locale's collation had put `gxsprf` before `gxspr` here
   and after it in make_card.sh's container, so VIEW.COM, which links both, was laid out two ways. The mutant gap
   below is accepted.

#### The owner's assumption for the real board ####

**The video card keeps up** (decision 1.3): at the same 32 sprites a line, of either size, a line's sprite fetch
at full resolution is up to 32 × 64 pixels, 2,048 bytes at 8 bits a pixel (64.4 MB/s) or 1,024 at 4 bits (32.2
MB/s); doubled sprites are 1,024 bytes a line if a card builds each 320-pixel sprite line once for its two screen
lines. With the table's scan (up to 512 bytes a line, 16.1 MB/s) and two direct 640 layers (73.7 MB/s), the worst
sum is about 154 MB/s on the one 16-bit SRAM, beside the aperture and the port. These are estimates, and taking
them as met is an assumption, not a measurement: no revision 2 board exists. If a board can't, the fallback is to
count a full-resolution sprite as two of the 32 (at most 16 full-resolution sprites on a line), which changes only
the definition and the emulator's count, not the software.

#### The definition ####

**SPRCTL** (`$C48E`, revision 2 only):

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0; a write is ignored |
| 0 | FULL | 1: an entry's byte 3 bit 7 makes that sprite full resolution; 0: byte 3 bit 7 is ignored |

Reset `$00`. Not latched, as CTRL isn't: a write shows from the next line drawn. OODOS's warm boot and
`DM_CONSOLE` write 0, so one program's setting never reaches the next. `$C48F` stays reserved (reads 0).
Revision 1 has no SPRCTL and never draws a full-resolution sprite.

**The entry** keeps its 8 bytes. Byte 3 (X bits 9–8 in bits 1–0) gains bit 7, FULL; its bits 6–2 and byte 5's bits
7–2 are still unused, and software writes them 0 from now on so that a later card may give them a meaning.

| | A doubled sprite (byte 3 bit 7 clear, or SPRCTL 0) | A full-resolution sprite (SPRCTL 1, byte 3 bit 7 set) |
|---|---|---|
| A sprite pixel on the screen | 2×2 | 1×1 |
| X and Y | 320×240 units, 10 bits, wrapping at 1,024 | 640×480 units, 10 bits, wrapping at 1,024 |
| Visible | X 0–319, Y 0–239 | X 0–639, Y 0–479 |
| Its size, each way | 8, 16, 32 or 64 sprite pixels: 16–128 screen pixels | the same codes: 8–64 screen pixels |
| Its rows | each covers two screen lines | each covers one |

- **Positions.** A full-resolution sprite at X 1,000 shows its right part at the left edge (a 64-pixel sprite: 40
  pixels), and one at X 600 its left 40 pixels at the right edge, nothing wrapped to the left: the wrap is at
  1,024, not 640. X 640–1,023 and Y 480–1,023 are off the screen.
- **The image** is the same bytes either way (row by row, 4 or 8 bits, at its card page), so one image shows at
  either size by the flag alone. Palette offset, depth (0 off, 1 behind both layers, 2 between them, 3 in front) and
  the collision groups mean what they mean now. Flips reverse the sprite's own width and height; at full resolution
  the reversed pixel is one screen pixel.
- **The 32 a line.** On each line the card draws the 32 lowest-numbered sprites whose rows cover it, of either size,
  depth 0 not counted: a doubled sprite's row is (line / 2 − Y) mod 1,024, a full-resolution one's (line − Y) mod
  1,024. A lower number is in front, across both sizes.
- **Collisions** are found in screen pixels, 640×480. A doubled sprite's opaque pixel marks its four screen pixels,
  a full-resolution sprite's one. Two doubled sprites collide on exactly the frames they collide on today (their 2×2
  blocks fall on the same even-aligned spots in both spaces); a full-resolution sprite and a doubled one collide
  where a 1×1 pixel falls on one of the four; two full-resolution sprites side by side don't.
- **Depth against the layers** is per screen pixel at both sizes, so a full-resolution sprite between the layers
  shows through layer 1's transparent pixels at 1×1.

#### Why every byte written today keeps its meaning ####

1. **SPRCTL 0 is today's card.** At reset, after every warm boot and `DM_CONSOLE`, and for every program that never
   writes `$C48E`, the card reads bytes 3 and 5 `& 3` and draws every sprite doubled, by today's own code path in
   the emulator. The plan's check rendered 20 random 64-sprite tables (every size, depth, flip and group, junk in
   byte 3's bits 6–2 and byte 5's bits 7–2) both ways: no frame and no COLL value differed, nor with byte 3 bit 7
   set on every entry while SPRCTL was 0.
2. **SPRCTL 1 with byte 3 bit 7 clear is today's picture too:** the same check through the full-resolution path,
   no difference. A program that opts in sees its old sprites unchanged.
3. **No software in the repo writes `$C48E`** but what this step adds: the BIOS, OOUI, GW-BASIC and the utilities
   write the registers by name, and none names it.
4. **OODOS's and OOUI's pointer stays doubled:** the frame handler rewrites the pointer sprite's bytes 2–5 each
   frame with bits 7–2 0, so even under a program that sets SPRCTL the pointer is doubled, in 320 units, unless the
   program asks `DM_POINTER` for a full-resolution one.
5. **OOLIB's GX_SPRPOS, GX_SPRIMG, GX_SPRATTR, GX_SPRINIT, GX_SPRSHOW, GX_COLL and `gxspr.s` are unchanged byte for
   byte;** the new calls are a new module, so OOUI.BNK and every program that doesn't call them link the same bytes.
   GX_SPRPOS writes byte 3's bits 7–2 0, as it always has.
6. **GX_ASSET** moves a sprite table's bytes 0–1 only: a table file's byte 3 reaches the card as written, which
   mkasset made 0 in bit 7 until `--full`. A file with the bit set shows full resolution only once the program sets
   SPRCTL.
7. **mkasset's output is unchanged** without `--full`.
8. **GW-BASIC** has no sprites and links none of the sprite modules.
9. **Function 83's `DM_POINTER`** keeps every call it takes: sprites 0–63 and `$FF` as before; 64–127, refused
   before, now mean "the same sprite, full resolution" (growth). `DM_INFO`'s +4 still returns 0–63 or `$FF`.

#### The emulator ####

`VideoCard` keeps SPRCTL (`$0E`, bit 0; reset 0). With SPRCTL 0, or on revision 1, `draw_line` builds today's
320-pixel sprite line and reads it at x / 2, unchanged. With SPRCTL 1 it builds a 640-pixel line: a doubled sprite's
pixel is written, masked and tested for collision at 2x and 2x + 1, a full-resolution sprite's at x, one count of
32 and one front order across both. Collisions go to the frame's COLL as before; only the masks' width changes.
The debugger's Devices panel shows ` · full` on revision 2's Sprites row while SPRCTL bit 0 is set. No save state
exists for the card, so there is nothing to migrate.

**Frame time, measured on a prototype before the build** (µs a frame, the card alone; a standalone benchmark of
`VideoCard` built native at -O2 and as WebAssembly run by node, main's card and the prototype run interleaved twice
and the lower kept; a frame is 16,683 µs). 64 sprites of 64×64 are the worst case: two bands of 32, so 32 sprites on
each of 256 lines doubled (128 at full resolution).

| Layers | Sprites | Main, native | Main, WASM | Prototype, native | Prototype, WASM |
|---|---|---|---|---|---|
| text 80 + text 80 | none | 3,988 | 2,136 | 4,009 | 2,158 |
| text 80 + text 80 | 64 × 64×64, 8 bits, doubled, SPRCTL 0 | 4,613 | 2,892 | 4,580 | 2,940 |
| text 80 + text 80 | the same, SPRCTL 1 (the 640 path) | | | 5,156 | 3,765 |
| text 80 + text 80 | 64 × 64×64, 8 bits, full resolution | | | 4,449 | 2,844 |
| text 80 + text 80 | 64 × 64×64, 8 bits, half of each | | | 4,765 | 3,315 |
| direct 640 + direct 640 | 64 × 64×64, 8 bits, doubled, SPRCTL 0 | 2,498 | 2,054 | 2,428 | 2,048 |
| direct 640 + direct 640 | the same, SPRCTL 1 | | | 3,004 | 2,914 |
| direct 640 + direct 640 | 64 × 64×64, 8 bits, full resolution | | | 2,289 | 2,131 |

SPRCTL 0 costs nothing (today's code, within the noise). Full-resolution sprites cost less than doubled ones at the
same table (half the lines, a pixel each). The dearest case is doubled sprites drawn by the 640 path, a program that
sets SPRCTL and keeps big doubled sprites: 31% of a frame native, 23% as WebAssembly.

#### What it adds beside video-rev2.md ####

- SPRCTL at `$C48E` (video-rev2.md has it reserved), and the entry's byte 3 bit 7; `$C48F` stays reserved.
- backplane.inc's `VIDEO_SPRCTL`, `VIDEO_SPR_FULL` and `VIDEO_SPRX_FULL`.
- OODOS: SPRCTL cleared by the warm boot and `DM_CONSOLE`; `DM_POINTER`'s 64 + the sprite; OOLIB interface 14.
- OOLIB's `gxsctl.s` (`GX_SPRFULL`) and `gxsprf.s` (`GX_SPRFPOS`); mkasset's `sprites --table --full`; VIEW sets SPRCTL with a
  table.

#### As built (2026-11-20, branch feat/sprites640) ####

**Status: built in the emulator; proposed for the board** (OODOS 0.13, OOLIB interface version 14).

- **The units:** U1 the record and the Technical Reference's definition; U2 the emulator (SPRCTL, `draw_sprites640`,
  today's path kept as `draw_sprites320`, its body unchanged); U3 the debugger's ` · full`; U4 OODOS (CRTREGS's
  SPRCTL 0, `DM_POINTER`'s 64 + n, interface 14); U5 OOLIB's `gxsprf.s` (`GX_SPRFULL` moved to `gxsctl.s` in review round 1); U6 mkasset's `--full`; U7 VIEW's
  `GX_SPRFULL`, and the Makefile's sorted `LIBSRC` (answer 11); U8 the books and OODOS 0.13. The cards were rebuilt
  after U4, U7 and OODOS 0.13; the sort needed none (the cards were already in byte order).
- **Byte for byte** as main 77cc7cf6's (cmp): GWBASIC.COM, GWBASIC.OVL, OOUI.BNK (the Makefile's build and
  test/ui's), and a probe linking every export of gxspr.s from oolib.lib. OOUI.BNK holds none of gxspr.s's code (its
  sprite calls are linked on the program's side), so the probe is what sees a change there. mkasset without `--full`
  writes the same bytes as before. Of the 32 programs the test tree links (review round 1, F4, each built from main
  77cc7cf6 and from the branch under both locales): under LC_ALL=C, as make_card.sh's container builds the cards,
  GXTEST and VIEW.COM differ in content (the new calls) and PIP.COM in its banner (0.13); MUTEST and PBTEST are byte
  for byte main's. Under en_US.UTF-8, against main built so, MUTEST and PBTEST differ too, only in the order of a
  pair of modules (mu and munote, pb and pbcard: main's unsorted list followed the locale), their sizes the same.
- **The tests** (each seen failing under its mutants, each run on its own case): S1-S7 in test_video_rev2, S8 the
  existing sprite cases unchanged, W1 (vitest), W2 (vga.spec), O1-O3 (test_oodos_video), G1-G2 (test_oodos_gfx), G3
  the byte-for-byte compare, M1 (check_mkasset), X1 (VIEW). **One gap, accepted (answer 11):** S3's mutant (b), a
  doubled sprite's mask set on one pixel of its pair, can't fail S3: with doubled sprites alone the two masks of a
  pair always agree. S4 (a full-resolution sprite against a doubled one's odd pixel) fails under it; both runs are
  recorded.
- **Found on the way:** S3's reference failed the WebAssembly build's `-Wsign-conversion` (clang), which g++ doesn't
  warn of; W2 at first typed the monitor's colon as text, where the USB keyboard takes key codes (Shift+;).

**Frame time, as built** (µs a frame, the card alone, the plan's benchmark; native g++ -O2, and WebAssembly from the
repo's emsdk image run by that image's node; main's card and this one interleaved twice on a quiet host, the lower
kept; a frame is 16,683 µs). "Main" for a full-resolution row is main's card drawing the same table doubled (it has
no SPRCTL).

| Layers | Sprites | Main, native | Built, native | Main, WASM | Built, WASM |
|---|---|---|---|---|---|
| text 80 + text 80 | none | 3,997 | 3,984 | 2,120 | 2,128 |
| text 80 + text 80 | 64 × 64×64, 8 bits, doubled, SPRCTL 0 | 4,559 | 4,554 | 2,895 | 2,920 |
| text 80 + text 80 | **the same, SPRCTL 1 (the 640 path)** | 4,612 | **5,057** | 2,901 | **3,844** |
| text 80 + text 80 | 64 × 64×64, 8 bits, full resolution | 4,381 | 4,375 | 2,686 | 2,846 |
| text 80 + text 80 | 64 × 64×64, 8 bits, half of each | 4,657 | 4,720 | 2,782 | 3,307 |
| direct 640 + direct 640 | 64 × 64×64, 8 bits, doubled, SPRCTL 0 | 2,382 | 2,474 | 2,040 | 2,066 |
| direct 640 + direct 640 | the same, SPRCTL 1 | 2,413 | 2,919 | 2,041 | 2,889 |
| direct 640 + direct 640 | 64 × 64×64, 8 bits, full resolution | 2,240 | 2,252 | 1,838 | 2,132 |

The dearest case, doubled sprites drawn by the 640 path, is 5,057 µs native (30% of a frame) against the prototype's
5,156; an earlier run of it on a busy host gave 5,322. As WebAssembly it is 3,844 µs (23%) against the prototype's
3,765, within the runs' spread; the same binaries under the host's own node (20, not the image's 24) ran about 2.5
times slower for main's card and this one alike, so only figures from one runtime compare. With SPRCTL 0 the card is
within 4% of main's, both ways; full-resolution sprites cost about what the same table doubled does (natively a
little less; as WebAssembly 3% less over text, 3% more over two direct 640 layers).

**Code:** OODOS 13 bytes and 1 of BSS (OODOS.SYS still 42,880 bytes); `gxsctl` 31 bytes (VIEW.COM 6,313 → 6,349
bytes; 6,627 before review round 1's F3, when `GX_SPRFULL` shared `gxsprf` and took in gxspr.s); `gxsprf` 56, in a
program only with gxspr.s's 222 and its 514-byte shadow; `video_card.cpp` +~55 lines.

**Review round 1** (2026-11-20, the owner: "recommendations as listed"; 4 findings, 3 new, 1 a revision of answer
11):

- **F1 (minor):** no test wrote SPRCTL mid-frame, so a card latching it at line 480 would have passed. Test
  `sprctl_is_not_latched_a_write_shows_from_the_next_line` (test_video_rev2): SPRCTL written as the beam starts line
  200, lines 0-199 of that frame drawn doubled and 200 on full resolution; seen failing with SPRCTL latched at the
  vertical blank. Chapter 6's `$C48E` notes say the rule as tested.
- **F2 (nit):** the User's Guide's VIEW: an older OODOS shows a `--full` table's sprites 2×2 at twice their places,
  those in the screen's right or bottom half not at all.
- **F3 (nit):** `GX_SPRFULL` in `gxsctl.s`, apart from `GX_SPRFPOS`: VIEW.COM 6,627 → 6,349 bytes, with no 514-byte
  shadow. GWBASIC.COM, .OVL, OOUI.BNK (both builds) and the gxspr.s probe still byte for byte main 77cc7cf6's.
- **F4 (nit):** the byte-for-byte paragraph above names its baselines.
