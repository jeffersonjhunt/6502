/**
 * @file test_video.cpp
 * @brief The video card (hw/design.md, "Video registers"): timing, interrupts, text and mono.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Most cases drive the card alone, with its "CPU clock" set to the pixel
 * clock so one tick is one pixel: the timing checks are then exact. Video
 * memory is a plain array standing in for banks 8-B. The last cases put the
 * card on a backplane machine and run 6502 code against it.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "cpu6502.h"
#include "machine.h"
#include "video_card.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t LINE = VideoCard::LINE_CLOCKS;
    constexpr uint64_t FRAME = LINE * VideoCard::LINES;
    constexpr uint64_t VBLANK_AT = LINE * VideoCard::HEIGHT;
    constexpr uint8_t L0 = VideoCard::LAYER0, L1 = VideoCard::LAYER1;

    constexpr uint16_t WHITE = 0xFFF, RED = 0xF00, GREEN = 0x0F0, BLUE = 0x00F, GREY = 0x888;

    struct Rig
    {
        Rig()
        {
            card.attach(vram.data());
            card.set_cpu_hz(VideoCard::PIXEL_HZ); // one cycle, one pixel clock
        }

        void colour(unsigned index, uint16_t rgb12, unsigned palbase = 0)
        {
            vram[palbase * 1024 + 2 * index] = static_cast<uint8_t>(rgb12);
            vram[palbase * 1024 + 2 * index + 1] = static_cast<uint8_t>(rgb12 >> 8);
        }
        void reg(uint8_t r, uint8_t v) { card.write(r, v); }
        /// A whole frame from wherever the beam is.
        void frames(unsigned n = 1) { card.tick(n * FRAME); }
        /// Registers written now take effect at the next vertical blank; the frame after it shows them.
        void show() { frames(2); }
        uint32_t px(unsigned x, unsigned y) const { return card.frame()[y * VideoCard::WIDTH + x]; }

        /// A text layer: the font at 1 KB unit `font`, the cells at `map`.
        void text(uint8_t layer, uint8_t config, uint8_t map = 8, uint8_t font = 4)
        {
            reg(layer + VideoCard::CONFIG, config);
            reg(layer + VideoCard::MAPBASE, map);
            reg(layer + VideoCard::TILEBASE, font);
        }
        void glyph(unsigned ch, unsigned row, uint8_t bits, uint8_t font = 4) { vram[font * 1024 + ch * 16 + row] = bits; }
        void cell(unsigned col, unsigned row, uint8_t ch, uint8_t attr, uint8_t map = 8, unsigned cols = 80)
        {
            vram[map * 1024 + (row * cols + col) * 2] = ch;
            vram[map * 1024 + (row * cols + col) * 2 + 1] = attr;
        }

        std::vector<uint8_t> vram = std::vector<uint8_t>(VideoCard::VRAM_SIZE, 0);
        VideoCard card;
    };

    uint32_t rgba(uint16_t c) { return VideoCard::rgba(c); }
    const uint32_t BLACK = rgba(0x000);
} // namespace

TEST(registers_read_back_and_reset)
{
    Rig r;
    r.reg(VideoCard::CTRL, 0xFF);
    CHECK_EQ(r.card.peek(VideoCard::CTRL), 0x0F); // four bits
    r.reg(VideoCard::BORDER, 0x12);
    r.reg(VideoCard::PALBASE, 0x34);
    r.reg(VideoCard::SPRBASE, 0x56);
    r.reg(VideoCard::IEN, 0xFF);
    CHECK_EQ(r.card.peek(VideoCard::BORDER), 0x12);
    CHECK_EQ(r.card.peek(VideoCard::PALBASE), 0x34);
    CHECK_EQ(r.card.peek(VideoCard::SPRBASE), 0x56);
    CHECK_EQ(r.card.peek(VideoCard::IEN), 0x07);
    r.reg(L1 + VideoCard::HSCROLL, 0xAB);
    r.reg(L1 + VideoCard::HSCROLL + 1, 0xFF);
    CHECK_EQ(r.card.peek(L1 + VideoCard::HSCROLL), 0xAB);
    CHECK_EQ(r.card.peek(L1 + VideoCard::HSCROLL + 1), 0x03); // 10 bits
    r.reg(L0 + VideoCard::PALOFS, 0x5A);
    CHECK_EQ(r.card.peek(L0 + VideoCard::PALOFS), 0x0A);
    CHECK_EQ(r.card.peek(L1 + VideoCard::PALOFS), 0x00);      // layers are separate
    CHECK_EQ(r.card.peek(VideoCard::ID_REG), 0x91);
    CHECK_EQ(r.card.peek(0x40), 0xFF);
    r.card.reset();
    CHECK_EQ(r.card.peek(VideoCard::CTRL), 0x00);
    CHECK_EQ(r.card.peek(L1 + VideoCard::HSCROLL), 0x00);
}

TEST(the_beam_runs_525_lines_of_800_clocks)
{
    Rig r;
    r.card.tick(10 * LINE + 5);
    CHECK_EQ(r.card.peek(VideoCard::LINE), 10);
    CHECK_EQ(r.card.peek(VideoCard::STATUS), 0x00);
    r.card.tick(VBLANK_AT - 10 * LINE - 6);       // one clock before line 480
    CHECK_EQ(r.card.peek(VideoCard::STATUS), 0x00);
    CHECK_EQ(r.card.frames(), uint64_t{0});
    r.card.tick(1);
    CHECK_EQ(r.card.peek(VideoCard::STATUS), VideoCard::IN_VBLANK);
    CHECK_EQ(r.card.frames(), uint64_t{1});
    r.card.tick(44 * LINE);
    CHECK_EQ(r.card.peek(VideoCard::LINE), 524 & 0xFF);
    CHECK_EQ(r.card.peek(VideoCard::LINE + 1), 524 >> 8);
    r.card.tick(LINE);                            // round to line 0
    CHECK_EQ(r.card.peek(VideoCard::LINE), 0);
    CHECK_EQ(r.card.peek(VideoCard::STATUS), 0x00);
    r.card.tick(FRAME);
    CHECK_EQ(r.card.frames(), uint64_t{2});
}

TEST(at_the_native_clock_a_second_is_60_frames_however_it_is_ticked)
{
    VideoCard whole, pieces;
    whole.tick(VideoCard::NATIVE_HZ);             // 7.16 MHz: one second
    for (uint32_t done = 0; done < VideoCard::NATIVE_HZ; done += 7)
        pieces.tick(std::min<uint32_t>(7, VideoCard::NATIVE_HZ - done)); // one instruction at a time
    CHECK_EQ(whole.frames(), uint64_t{60});       // 59.94 Hz: the 60th vertical blank is inside the second
    CHECK_EQ(pieces.frames(), whole.frames());
    CHECK_EQ(pieces.beam_line(), whole.beam_line());
    VideoCard big;                                // one tick past where cycles x step overflows 64 bits
    const uint64_t huge = 1'300'000'000;          // 3 minutes at 7.16 MHz
    big.tick(huge);
    const double want = (double(huge) * VideoCard::PIXEL_HZ / VideoCard::NATIVE_HZ - 480.0 * 800) / (525 * 800) + 1;
    CHECK(big.frames() + 1 >= uint64_t(want) && big.frames() <= uint64_t(want) + 1);
    VideoCard slow;
    slow.set_cpu_hz(1'020'484);                   // the panel's slowest clock
    slow.tick(1'020'484);
    CHECK_EQ(slow.frames(), uint64_t{60});        // still 60 a second
    VideoCard apple;                              // the Apple clock: a bigger step, so a smaller safe chunk
    apple.set_cpu_hz(1'020'484);
    const uint64_t ages = (uint64_t{1} << 28) + 1; // 4 minutes in one tick
    apple.tick(ages);
    const double frames = (double(ages) * VideoCard::PIXEL_HZ / 1'020'484 - 480.0 * 800) / (525 * 800) + 1;
    CHECK(apple.frames() + 1 >= uint64_t(frames) && apple.frames() <= uint64_t(frames) + 1);
}

TEST(bus_reset_clears_the_registers_and_the_beam_runs_on)
{
    Rig r;
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY);
    r.reg(VideoCard::BORDER, 3);
    r.reg(VideoCard::IEN, VideoCard::VBLANK);
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.card.tick(VBLANK_AT + 5 * LINE);
    REQUIRE(r.card.irq());
    r.card.bus_reset();                           // START, Ctrl-Reset, entering Apple II mode
    CHECK(!r.card.irq());
    CHECK_EQ(r.card.peek(VideoCard::IEN), 0);
    CHECK_EQ(r.card.peek(VideoCard::ISR), 0);
    CHECK_EQ(r.card.peek(VideoCard::CTRL), 0);
    CHECK_EQ(r.card.peek(VideoCard::BORDER), 0);
    CHECK_EQ(r.card.peek(L0 + VideoCard::CONFIG), 0);
    CHECK_EQ(r.card.frames(), uint64_t{1});       // the monitor keeps its sync
    CHECK_EQ(r.card.beam_line(), 485u);
}

TEST(vertical_blank_and_line_interrupts_pend_whatever_ien_says)
{
    Rig r;
    r.card.tick(VBLANK_AT);
    CHECK_EQ(r.card.peek(VideoCard::ISR), VideoCard::VBLANK);
    CHECK(!r.card.irq());                         // pending, not enabled
    r.reg(VideoCard::IEN, VideoCard::VBLANK);
    CHECK(r.card.irq());
    r.reg(VideoCard::ISR, VideoCard::VBLANK);     // write 1 to clear
    CHECK(!r.card.irq());
    CHECK_EQ(r.card.peek(VideoCard::ISR), 0x00);

    r.reg(VideoCard::LINE, 100 & 0xFF);
    r.reg(VideoCard::LINE + 1, 0xFC);             // only bits 0-1 count: line 100
    r.reg(VideoCard::IEN, VideoCard::LINE_MATCH);
    r.card.tick(FRAME - VBLANK_AT + 100 * LINE - 1); // one clock before line 100 starts
    CHECK(!r.card.irq());
    r.card.tick(1);
    CHECK(r.card.irq());
    CHECK_EQ(r.card.peek(VideoCard::ISR), VideoCard::LINE_MATCH);
    CHECK_EQ(r.card.peek(VideoCard::LINE), 100);  // a read is the beam, not the match
}

TEST(display_off_is_black_on_with_no_layer_is_border_a_layer_shows_entry_0)
{
    Rig r;
    r.colour(0, BLUE);
    r.colour(3, GREY);
    r.reg(VideoCard::BORDER, 3);
    r.show();
    CHECK_EQ(r.px(320, 240), BLACK);              // CTRL 0: display off
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(GREY));
    CHECK_EQ(r.px(639, 479), rgba(GREY));
    r.text(L0, VideoCard::TEXT80);                // nothing in it: all transparent
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(BLUE));
    CHECK_EQ(r.px(639, 479), rgba(BLUE));
}

TEST(text_80_columns_glyph_bits_attribute_colours_and_palofs)
{
    Rig r;
    r.colour(0x0F, WHITE);
    r.colour(0x02, GREEN);
    r.colour(0x1F, RED);
    r.colour(0x12, BLUE);
    r.glyph('A', 3, 0b1010'0000);
    r.cell(2, 1, 'A', 0x2F);                      // fg F, bg 2
    r.text(L0, VideoCard::TEXT80);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    const unsigned x = 2 * 8, y = 16 + 3;
    CHECK_EQ(r.px(x, y), rgba(WHITE));
    CHECK_EQ(r.px(x + 1, y), rgba(GREEN));
    CHECK_EQ(r.px(x + 2, y), rgba(WHITE));
    CHECK_EQ(r.px(x + 3, y), rgba(GREEN));
    CHECK_EQ(r.px(x, y + 1), rgba(GREEN));        // row 4 of the glyph is empty
    CHECK_EQ(r.px(x - 1, y), BLACK);              // the cell before: char 0, attr 0, transparent
    r.reg(L0 + VideoCard::PALOFS, 1);             // a 16-colour slice: $1x
    r.frames();                                   // PALOFS is not latched
    CHECK_EQ(r.px(x, y), rgba(RED));
    CHECK_EQ(r.px(x + 1, y), rgba(BLUE));
}

TEST(text_40_columns_doubles_across)
{
    Rig r;
    r.colour(1, WHITE);
    r.glyph(1, 0, 0b0100'0000);
    r.cell(1, 0, 1, 0x01, 8, 40);
    r.text(L0, VideoCard::TEXT40);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(16 + 1, 0), BLACK);             // column 1 starts at x 16; its bit 7 is off
    CHECK_EQ(r.px(16 + 2, 0), rgba(WHITE));       // bit 6: x 18 and 19
    CHECK_EQ(r.px(16 + 3, 0), rgba(WHITE));
    CHECK_EQ(r.px(16 + 4, 0), BLACK);
}

TEST(blink_hides_the_character_16_frames_in_32_and_leaves_8_backgrounds)
{
    Rig r;
    r.colour(0x01, GREEN);
    r.colour(0x09, BLUE);
    r.colour(0x0F, WHITE);
    for (unsigned row = 0; row < 16; ++row)
        r.glyph(0xDB, row, 0xFF);                 // a full block
    r.cell(0, 0, 0xDB, 0x9F);                     // bg 9 (bit 7 set), fg F
    r.text(L0, VideoCard::TEXT80 | VideoCard::BLINK);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));            // frames 0-15: on
    while (r.card.frames() < 17)                  // the frame drawn as the 16th finishes
        r.frames();
    CHECK_EQ(r.px(0, 0), rgba(GREEN));            // off: the background, bit 7 dropped (9 -> 1)
    while (r.card.frames() < 33)
        r.frames();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));            // on again
    r.text(L0, VideoCard::TEXT80);                // no blink: bit 7 is a bright background
    r.cell(0, 0, 0x00, 0x9F);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(BLUE));
}

TEST(layer_1_draws_over_layer_0_where_it_is_not_transparent)
{
    Rig r;
    r.colour(1, RED);
    r.colour(2, GREEN);
    for (uint32_t i = 0; i < 80 * 480; ++i)
        r.vram[16 * 1024 + i] = 0xFF;             // layer 0: mono, every pixel colour 1
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.reg(L0 + VideoCard::MAPBASE, 16);
    r.glyph('X', 0, 0x80);
    r.cell(0, 0, 'X', 0x02);                      // layer 1: one green pixel, background 0
    r.text(L1, VideoCard::TEXT80);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::LAYER1_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(GREEN));
    CHECK_EQ(r.px(1, 0), rgba(RED));              // layer 1 transparent: layer 0 shows
    CHECK_EQ(r.px(400, 300), rgba(RED));
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER1_ON);
    r.frames();                                   // CTRL is not latched
    CHECK_EQ(r.px(1, 0), BLACK);                  // only layer 1: entry 0 behind it
}

TEST(mono_pixels_and_scroll_wrap_within_the_layer)
{
    Rig r;
    r.colour(1, WHITE);
    r.colour(0x31, RED);
    r.vram[16 * 1024] = 0x80;                     // (0, 0)
    r.vram[16 * 1024 + 479 * 80 + 79] = 0x01;     // (639, 479)
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.reg(L0 + VideoCard::MAPBASE, 16);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));
    CHECK_EQ(r.px(1, 0), BLACK);
    CHECK_EQ(r.px(639, 479), rgba(WHITE));
    r.reg(L0 + VideoCard::HSCROLL, 1);
    r.reg(L0 + VideoCard::VSCROLL, 1);
    r.reg(L0 + VideoCard::PALOFS, 3);
    r.show();
    CHECK_EQ(r.px(639, 479), rgba(RED));          // (0, 0) wrapped to the far corner, colour $31
    CHECK_EQ(r.px(638, 478), rgba(RED));          // and (639, 479) to one in from it
    CHECK_EQ(r.px(0, 0), BLACK);
}

TEST(bitmap_256_is_8_bits_doubled_and_centred_with_border_either_side)
{
    Rig r;
    r.colour(3, GREY);                            // the border
    r.colour(0x21, RED);
    r.colour(0xF0, GREEN);
    r.colour(0x01, BLUE);
    const uint32_t img = 64 * 1024;               // unit 64: 256 bytes a row, y the high byte
    r.vram[img + 0] = 0x21;                       // (0, 0)
    r.vram[img + 255] = 0xF0;                     // (255, 0)
    r.vram[img + 239 * 256 + 128] = 0x01;         // (128, 239)
    r.reg(VideoCard::BORDER, 3);
    r.reg(L0 + VideoCard::CONFIG, VideoCard::BITMAP256);
    r.reg(L0 + VideoCard::MAPBASE, 64);
    r.reg(L0 + VideoCard::PALOFS, 5);             // not for 8-bit pixels
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(63, 0), rgba(GREY));            // left of the image: border
    CHECK_EQ(r.px(64, 0), rgba(RED));             // (0, 0) is 2x2 screen pixels
    CHECK_EQ(r.px(65, 1), rgba(RED));
    CHECK_EQ(r.px(66, 0), BLACK);                 // (1, 0): 0, transparent, entry 0
    CHECK_EQ(r.px(574, 0), rgba(GREEN));          // (255, 0)
    CHECK_EQ(r.px(575, 1), rgba(GREEN));
    CHECK_EQ(r.px(576, 0), rgba(GREY));           // right of the image: border
    CHECK_EQ(r.px(64 + 256, 478), rgba(BLUE));    // (128, 239), the last row
    CHECK_EQ(r.px(64 + 257, 479), rgba(BLUE));
    r.reg(L0 + VideoCard::HSCROLL, 255);          // one pixel left of 0: wraps
    r.reg(L0 + VideoCard::VSCROLL, 239);
    r.show();
    CHECK_EQ(r.px(64 + 2, 2), rgba(RED));         // (0, 0) is now at (1, 1)
    CHECK_EQ(r.px(64 + 258, 0), rgba(BLUE));      // (128, 239) at (129, 0)
}

TEST(bitmap_512_is_4_bits_left_pixel_high_with_palofs)
{
    Rig r;
    r.colour(3, GREY);
    r.colour(0x0A, RED);
    r.colour(0x0B, GREEN);
    r.colour(0x2A, BLUE);
    r.colour(0x2B, WHITE);
    const uint32_t img = 64 * 1024;               // 120 KB, still inside banks 8-B
    r.vram[img + 0] = 0xAB;                       // (0, 0) = A, (1, 0) = B
    r.vram[img + 479 * 256 + 255] = 0x0A;         // (511, 479) = A, (510, 479) = 0
    r.reg(VideoCard::BORDER, 3);
    r.reg(L0 + VideoCard::CONFIG, VideoCard::BITMAP512);
    r.reg(L0 + VideoCard::MAPBASE, 64);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(63, 0), rgba(GREY));
    CHECK_EQ(r.px(64, 0), rgba(RED));
    CHECK_EQ(r.px(65, 0), rgba(GREEN));
    CHECK_EQ(r.px(64, 1), BLACK);                 // not doubled
    CHECK_EQ(r.px(575, 479), rgba(RED));
    CHECK_EQ(r.px(574, 479), BLACK);
    CHECK_EQ(r.px(576, 479), rgba(GREY));
    r.reg(L0 + VideoCard::PALOFS, 2);
    r.frames();
    CHECK_EQ(r.px(64, 0), rgba(BLUE));            // $2A
    r.reg(L0 + VideoCard::HSCROLL, 1);
    r.show();
    CHECK_EQ(r.px(64, 0), rgba(WHITE));           // (1, 0), B + PALOFS: $2B, shows first...
    CHECK_EQ(r.px(575, 0), rgba(BLUE));           // ...and (0, 0) wrapped to the far right
}

namespace
{
    constexpr uint8_t MAP = 32, TILES = 48;       // units: map at $88000, tiles at $8C000

    /// A 4-bit pixel of tile `n` (size x size), left pixel high.
    void tile4(Rig &r, unsigned n, unsigned size, unsigned tx, unsigned ty, uint8_t v)
    {
        const uint32_t at = TILES * 1024 + n * size * size / 2 + (ty * size + tx) / 2;
        r.vram[at] = static_cast<uint8_t>(tx & 1 ? (r.vram[at] & 0xF0) | v : (r.vram[at] & 0x0F) | v << 4);
    }
    void entry(Rig &r, unsigned mx, unsigned my, unsigned map_w, unsigned tile, uint8_t flags)
    {
        const uint32_t at = MAP * 1024 + (my * map_w + mx) * 2;
        r.vram[at] = static_cast<uint8_t>(tile);
        r.vram[at + 1] = static_cast<uint8_t>(flags | tile >> 8);
    }
    void tiles(Rig &r, uint8_t config)
    {
        r.reg(L0 + VideoCard::CONFIG, static_cast<uint8_t>(VideoCard::TILE | config));
        r.reg(L0 + VideoCard::MAPBASE, MAP);
        r.reg(L0 + VideoCard::TILEBASE, TILES);
        r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    }
} // namespace

TEST(tiles_8x8_4_bits_doubled_with_the_entry_palette_and_palofs)
{
    Rig r;
    r.colour(0x05, RED);
    r.colour(0x35, GREEN);
    r.colour(0x45, BLUE);
    tile4(r, 1, 8, 0, 0, 5);                      // tile 1: one pixel at (0, 0), colour 5
    tile4(r, 1, 8, 7, 7, 5);                      // and (7, 7)
    entry(r, 0, 0, 32, 1, 0x00);                  // map (0, 0): tile 1, palette 0
    entry(r, 2, 1, 32, 1, 0x30);                  // map (2, 1): tile 1, palette 3
    tiles(r, 0);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(RED));              // a tile pixel is 2x2 on screen
    CHECK_EQ(r.px(1, 1), rgba(RED));
    CHECK_EQ(r.px(2, 0), BLACK);                  // the rest of the tile: 0, transparent
    CHECK_EQ(r.px(14, 14), rgba(RED));            // (7, 7)
    CHECK_EQ(r.px(32, 16), rgba(GREEN));          // map (2, 1) is at 320-space (16, 8): palette 3 -> $35
    r.reg(L0 + VideoCard::PALOFS, 1);
    r.frames();
    CHECK_EQ(r.px(32, 16), rgba(BLUE));           // 3 + 1 -> $45
    r.reg(L0 + VideoCard::PALOFS, 13);
    r.frames();
    CHECK_EQ(r.px(32, 16), rgba(RED));            // 3 + 13 = 16: wraps to 0 -> $05
}

TEST(tiles_flip_and_reach_tile_1023)
{
    Rig r;
    r.colour(0x07, WHITE);
    tile4(r, 1023, 8, 1, 2, 7);                   // the last tile number, one pixel at (1, 2)
    entry(r, 0, 0, 32, 1023, 0x00);
    entry(r, 1, 0, 32, 1023, 0x04);               // H flip: (6, 2)
    entry(r, 2, 0, 32, 1023, 0x08);               // V flip: (1, 5)
    entry(r, 3, 0, 32, 1023, 0x0C);               // both: (6, 5)
    tiles(r, 0);
    r.show();
    auto lit = [&](unsigned tx, unsigned ty) { return r.px(tx * 2, ty * 2) == rgba(WHITE); };
    CHECK(lit(1, 2));
    CHECK(!lit(6, 2));
    CHECK(lit(8 + 6, 2));
    CHECK(!lit(8 + 1, 2));
    CHECK(lit(16 + 1, 5));
    CHECK(!lit(16 + 1, 2));
    CHECK(lit(24 + 6, 5));
}

TEST(tiles_16x16_8_bits_and_a_64_wide_map_scroll_and_wrap)
{
    Rig r;
    r.colour(0x9C, RED);
    r.colour(0x2D, GREEN);
    const uint32_t tile2 = TILES * 1024 + 2 * 256;  // 16x16 at 8 bits: 256 bytes
    r.vram[tile2 + 0] = 0x9C;                       // (0, 0)
    r.vram[tile2 + 15 * 16 + 15] = 0x2D;            // (15, 15)
    entry(r, 63, 0, 64, 2, 0xF0);                   // the map's last column; palette ignored at 8 bits
    tiles(r, VideoCard::TILE16 | VideoCard::TILE8BPP | VideoCard::MAP_W64);
    r.show();
    CHECK_EQ(r.px(0, 0), BLACK);                    // column 63 is at x 1008 in 320-space: off screen
    r.reg(L0 + VideoCard::HSCROLL, (63 * 16) & 0xFF);
    r.reg(L0 + VideoCard::HSCROLL + 1, (63 * 16) >> 8);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(RED));                // scrolled to it
    CHECK_EQ(r.px(30, 30), rgba(GREEN));            // (15, 15), doubled
    CHECK_EQ(r.px(32, 0), BLACK);                   // then column 0, wrapped round (64 x 16 = 1024)
    r.reg(L0 + VideoCard::HSCROLL, (63 * 16 + 4) & 0xFF); // fine scroll: 4 pixels on
    r.reg(L0 + VideoCard::VSCROLL, (32 * 16 - 1) & 0xFF); // 32 high: 512, and one row back from 0
    r.reg(L0 + VideoCard::VSCROLL + 1, (32 * 16 - 1) >> 8);
    r.show();
    CHECK_EQ(r.px(22, 32), rgba(GREEN));            // (15, 15) now at 320-space (11, 16)
}

TEST(tiles_over_text_transparent_where_0)
{
    Rig r;
    r.colour(0x01, RED);
    r.colour(0x0F, WHITE);
    for (unsigned row = 0; row < 16; ++row)
        r.glyph(0xDB, row, 0xFF);
    r.cell(0, 0, 0xDB, 0x0F);                     // layer 0: a white block at text cell (0, 0)
    r.text(L0, VideoCard::TEXT80);
    tile4(r, 1, 8, 0, 0, 1);
    entry(r, 0, 0, 32, 1, 0x00);
    r.reg(L1 + VideoCard::CONFIG, VideoCard::TILE);
    r.reg(L1 + VideoCard::MAPBASE, MAP);
    r.reg(L1 + VideoCard::TILEBASE, TILES);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::LAYER1_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(RED));              // the tile's pixel over the text
    CHECK_EQ(r.px(2, 0), rgba(WHITE));            // transparent: the text shows
    CHECK_EQ(r.px(7, 15), rgba(WHITE));
}

namespace
{
    constexpr uint8_t SPR = 40;                   // the sprite table at unit 40, $8A000

    /// Sprite `n`: image at 64-byte unit `image`, X, Y, flags byte 6, byte 7 (size and palette).
    void sprite(Rig &r, unsigned n, unsigned image, bool bpp8, unsigned x, unsigned y, uint8_t b6, uint8_t b7)
    {
        uint8_t *e = &r.vram[SPR * 1024 + n * 8];
        e[0] = static_cast<uint8_t>(image);
        e[1] = static_cast<uint8_t>(image >> 8 | (bpp8 ? 0x80 : 0));
        e[2] = static_cast<uint8_t>(x);
        e[3] = static_cast<uint8_t>(x >> 8);
        e[4] = static_cast<uint8_t>(y);
        e[5] = static_cast<uint8_t>(y >> 8);
        e[6] = b6;
        e[7] = b7;
    }
    /// An 8x8 4-bit image at 64-byte unit `image`, every pixel `v`.
    void solid8(Rig &r, unsigned image, uint8_t v) { std::fill_n(&r.vram[image * 64], 32, static_cast<uint8_t>(v << 4 | v)); }
    constexpr uint8_t FRONT = 3 << 2, BETWEEN = 2 << 2, BEHIND = 1 << 2;

    void sprites_on(Rig &r, uint8_t more = 0)
    {
        r.reg(VideoCard::SPRBASE, SPR);
        r.reg(VideoCard::CTRL, static_cast<uint8_t>(VideoCard::DISPLAY | VideoCard::SPRITES_ON | more));
    }
} // namespace

TEST(a_sprite_is_drawn_doubled_at_its_place_with_its_palette)
{
    Rig r;
    r.colour(0x35, RED);
    r.colour(0x07, GREEN);
    solid8(r, 100, 5);
    r.vram[100 * 64] = 0x05;                      // its (0, 0) transparent, (1, 0) colour 5
    sprite(r, 0, 100, false, 10, 20, FRONT, 0x03); // 8x8, palette 3
    sprites_on(r);
    r.show();
    CHECK_EQ(r.px(20, 40), BLACK);                // (0, 0): transparent
    CHECK_EQ(r.px(22, 40), rgba(RED));            // (1, 0): $35, 2x2
    CHECK_EQ(r.px(23, 41), rgba(RED));
    CHECK_EQ(r.px(20 + 15, 40 + 15), rgba(RED));  // the far corner
    CHECK_EQ(r.px(20 + 16, 40), BLACK);           // just past it
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY);   // sprites off
    r.frames();
    CHECK_EQ(r.px(22, 40), BLACK);
    sprite(r, 0, 100, false, 10, 20, 0, 0x03);    // depth 0: off
    sprites_on(r);
    r.frames();
    CHECK_EQ(r.px(22, 40), BLACK);
    // 8 bits, 16 wide by 32 high, both flips: the image's (0, 0) lands at the far corner.
    r.vram[200 * 64] = 0x07;
    sprite(r, 1, 200, true, 50, 60, FRONT | 0x03, 0x10 | 0x80 | 0x0F); // 16 wide (1), 32 high (2); palette ignored at 8 bits
    r.frames();
    CHECK_EQ(r.px(2 * (50 + 15), 2 * (60 + 31)), rgba(GREEN));
    CHECK_EQ(r.px(2 * 50, 2 * 60), BLACK);
}

TEST(sprites_wrap_at_1024_so_they_can_start_off_the_left)
{
    Rig r;
    r.colour(0x01, WHITE);
    solid8(r, 100, 1);
    sprite(r, 0, 100, false, 1020, 1020, FRONT, 0x00); // 4 pixels off the left and the top
    sprites_on(r);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));
    CHECK_EQ(r.px(7, 7), rgba(WHITE));            // its (7, 7) at (3, 3)
    CHECK_EQ(r.px(8, 0), BLACK);
}

TEST(depth_puts_a_sprite_behind_between_or_in_front_of_the_layers)
{
    Rig r;
    r.colour(0x01, RED);                          // layer 0: mono, all lit
    r.colour(0x0F, WHITE);                        // layer 1: a text block at cell (0, 0)
    r.colour(0x02, GREEN);                        // the sprite
    for (uint32_t i = 0; i < 80 * 480; ++i)
        r.vram[16 * 1024 + i] = 0xFF;
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.reg(L0 + VideoCard::MAPBASE, 16);
    for (unsigned row = 0; row < 16; ++row)
        r.glyph(0xDB, row, 0xFF);
    r.cell(0, 0, 0xDB, 0x0F);
    r.text(L1, VideoCard::TEXT80);
    solid8(r, 100, 2);
    const uint8_t layers = VideoCard::LAYER0_ON | VideoCard::LAYER1_ON;
    sprite(r, 0, 100, false, 0, 0, BEHIND, 0x00);
    sprites_on(r, layers);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));            // behind both: layer 1's block...
    CHECK_EQ(r.px(10, 10), rgba(RED));            // ...and layer 0 elsewhere
    sprite(r, 0, 100, false, 0, 0, BETWEEN, 0x00);
    r.frames();
    CHECK_EQ(r.px(0, 0), rgba(WHITE));            // between: under layer 1's block
    CHECK_EQ(r.px(10, 10), rgba(GREEN));          // over layer 0
    sprite(r, 0, 100, false, 0, 0, FRONT, 0x00);
    r.frames();
    CHECK_EQ(r.px(0, 0), rgba(GREEN));            // in front of everything
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::SPRITES_ON); // no layers: the border is behind it
    r.colour(0x09, GREY);
    r.reg(VideoCard::BORDER, 9);
    sprite(r, 0, 100, false, 0, 0, BEHIND, 0x00);
    r.frames();
    CHECK_EQ(r.px(0, 0), rgba(GREEN));
    CHECK_EQ(r.px(100, 100), rgba(GREY));
}

TEST(lower_numbers_are_in_front_and_32_a_line_at_most)
{
    Rig r;
    r.colour(0x01, RED);
    r.colour(0x02, GREEN);
    solid8(r, 100, 1);
    solid8(r, 101, 2);
    sprite(r, 0, 101, false, 4, 0, FRONT, 0x00);  // green, overlapping red by 4
    sprite(r, 1, 100, false, 0, 0, FRONT, 0x00);  // red
    for (unsigned n = 2; n < 40; ++n)             // 38 more on line 100: only 32 of all those on it draw
        sprite(r, n, 100, false, 8 * n, 100, FRONT, 0x00);
    sprites_on(r);
    r.show();
    CHECK_EQ(r.px(2 * 4, 0), rgba(GREEN));        // where they overlap, sprite 0 wins
    CHECK_EQ(r.px(2 * 3, 0), rgba(RED));
    CHECK_EQ(r.px(2 * 8 * 33, 200), rgba(RED));   // sprite 33: the 32nd on line 100 (0 and 1 are not on it)
    CHECK_EQ(r.px(2 * 8 * 34, 200), BLACK);       // sprite 34: the 33rd, dropped
}

TEST(collisions_need_overlapping_pixels_and_a_shared_group)
{
    Rig r;
    r.colour(0x01, RED);
    solid8(r, 100, 1);
    sprite(r, 0, 100, false, 0, 0, FRONT | 0x30, 0x00);   // groups 1 and 2 (bits 4-5)
    sprite(r, 1, 100, false, 100, 0, FRONT | 0x60, 0x00); // groups 2 and 4, far off: no overlap
    sprites_on(r);
    r.reg(VideoCard::IEN, VideoCard::COLLISION);
    r.show();
    CHECK_EQ(r.card.peek(VideoCard::ISR) & VideoCard::COLLISION, 0);
    CHECK(!r.card.irq());
    sprite(r, 1, 100, false, 4, 4, FRONT | 0xC0, 0x00);   // overlapping, groups 4 and 8: none shared
    r.frames();
    CHECK_EQ(r.card.peek(VideoCard::ISR) & VideoCard::COLLISION, 0);
    sprite(r, 1, 100, false, 4, 4, FRONT | 0x60, 0x00);   // overlapping, shares group 2
    r.frames(2);
    CHECK_EQ(r.card.peek(VideoCard::ISR) & VideoCard::COLLISION, VideoCard::COLLISION);
    CHECK_EQ(r.card.peek(VideoCard::STATUS) & 0x0F, 0x02); // the shared group
    CHECK(r.card.irq());
    r.reg(VideoCard::ISR, VideoCard::COLLISION);
    CHECK(!r.card.irq());
}

TEST(the_sprite_table_moves_at_vertical_blank)
{
    Rig r;
    r.colour(0x01, RED);
    solid8(r, 100, 1);
    sprite(r, 0, 100, false, 0, 200, FRONT, 0x00); // low on the screen, in table 40
    sprites_on(r);
    r.show();
    CHECK_EQ(r.px(0, 400), rgba(RED));
    r.card.tick(100 * LINE);
    r.reg(VideoCard::SPRBASE, SPR + 1);           // an empty table, mid-frame
    r.card.tick(FRAME - 100 * LINE);
    CHECK_EQ(r.px(0, 400), rgba(RED));            // the rest of that frame: still the old table
    r.frames();
    CHECK_EQ(r.px(0, 400), BLACK);
}

TEST(bases_and_scroll_wait_for_vertical_blank_memory_does_not)
{
    Rig r;
    r.colour(1, WHITE);
    r.colour(1, RED, 2);                          // a second palette at unit 2
    for (uint32_t i = 0; i < 80 * 480; ++i)
        r.vram[16 * 1024 + i] = 0xFF;             // image A: all lit; image B (unit 64) blank
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.reg(L0 + VideoCard::MAPBASE, 16);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    r.card.tick(100 * LINE);                      // the beam is at line 100
    r.reg(L0 + VideoCard::MAPBASE, 64);
    r.reg(VideoCard::PALBASE, 2);
    CHECK_EQ(r.card.peek(L0 + VideoCard::MAPBASE), 64); // reads back at once
    r.card.tick(FRAME - 100 * LINE);              // finish this frame...
    CHECK_EQ(r.px(0, 300), rgba(WHITE));          // ...still image A, palette 0, below the write
    r.frames();
    CHECK_EQ(r.px(0, 300), BLACK);                // B, after the vertical blank

    r.reg(L0 + VideoCard::MAPBASE, 16);
    r.reg(VideoCard::PALBASE, 0);
    r.show();
    r.card.tick(240 * LINE);                      // the beam starts line 240...
    r.colour(1, GREEN);                           // ...and the palette (memory) changes
    r.card.tick(FRAME - 240 * LINE);
    CHECK_EQ(r.px(0, 239), rgba(WHITE));
    CHECK_EQ(r.px(0, 240), rgba(GREEN));          // from this line on
}

TEST(frame_is_the_last_finished_frame_never_one_being_drawn)
{
    Rig r;
    r.colour(1, WHITE);
    for (uint32_t i = 0; i < 80 * 480; ++i)
        r.vram[16 * 1024 + i] = 0xFF;
    r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
    r.reg(L0 + VideoCard::MAPBASE, 16);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    r.colour(1, RED);
    r.card.tick(300 * LINE);                      // 300 lines drawn red, not finished
    CHECK_EQ(r.px(0, 0), rgba(WHITE));
    CHECK_EQ(r.px(0, 479), rgba(WHITE));
    r.card.tick(FRAME);
    CHECK_EQ(r.px(0, 0), rgba(RED));
}

TEST(ppm_holds_the_frame)
{
    Rig r;
    r.colour(3, 0x123);
    r.reg(VideoCard::BORDER, 3);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY);
    r.show();
    const std::string path = "test_video_frame.ppm";
    REQUIRE(write_ppm(r.card, path));
    std::FILE *f = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    char magic[3] = {};
    unsigned w = 0, h = 0, max = 0;
    CHECK_EQ(std::fscanf(f, "%2s %u %u %u", magic, &w, &h, &max), 4);
    std::fgetc(f);                                // the one whitespace after 255
    CHECK_EQ(std::string(magic), std::string("P6"));
    CHECK_EQ(w, 640u);
    CHECK_EQ(h, 480u);
    uint8_t rgb[3] = {};
    CHECK_EQ(std::fread(rgb, 1, 3, f), size_t{3});
    CHECK_EQ(rgb[0], 0x11);
    CHECK_EQ(rgb[1], 0x22);
    CHECK_EQ(rgb[2], 0x33);
    std::fclose(f);
    std::remove(path.c_str());
    CHECK(!write_ppm(r.card, "no/such/dir/x.ppm"));
}

namespace
{
    std::unique_ptr<Machine> backplane(const std::string &rams, std::string &error)
    {
        return Machine::build("cpu w65c02s\n"
                              "northbridge backplane bootsel\n" +
                                  rams + "video 480-4FF space=io name=video irq\n",
                              "video.machine", {}, error);
    }
} // namespace

TEST(the_machine_needs_banks_8_to_b_in_one_ram_line)
{
    std::string error;
    CHECK(backplane("ram 00000-FFFFF module=0\n", error) != nullptr);
    CHECK(backplane("ram 00000-7FFFF\n", error) == nullptr);
    CHECK(error.find("banks 8-B") != std::string::npos);
    error.clear();
    CHECK(backplane("ram 00000-9FFFF\nram A0000-FFFFF\n", error) == nullptr);
    CHECK(error.find("banks 8-B") != std::string::npos);
    error.clear();
    CHECK(backplane("ram 00000-FFFFF\nrom 90000-90FFF\n", error) == nullptr); // not RAM where it matters...
}

TEST(a_6502_program_puts_a_character_on_the_screen_and_takes_the_vblank_irq)
{
    std::string error;
    auto m = backplane("ram 00000-FFFFF module=0\n", error);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    REQUIRE(card != nullptr);
    Decoder &bus = m->bus();
    const std::vector<uint8_t> prog = {
        0xA9, 0x20, 0x8D, 0x81, 0xC6,       // LDA #$20; STA WIN1     $4000 = RAM block 32 = $80000
        0xA9, 0xFF, 0x8D, 0x02, 0x40,       // palette entry 1: $FFF, white
        0xA9, 0x0F, 0x8D, 0x03, 0x40,
        0xA2, 0x0F, 0xA9, 0xFF,             // LDX #15; LDA #$FF
        0x9D, 0x10, 0x44, 0xCA, 0x10, 0xFA, // font (unit 1) char 1: a full block
        0xA9, 0x01, 0x8D, 0x00, 0x48,       // text (unit 2) cell 0: char 1...
        0x8D, 0x01, 0x48,                   // ...attribute $01
        0xA9, 0x02, 0x8D, 0x91, 0xC4,       // MAPBASE 2
        0xA9, 0x01, 0x8D, 0x92, 0xC4,       // TILEBASE 1
        0xA9, 0x03, 0x8D, 0x80, 0xC4,       // CTRL: display, layer 0
        0xA9, 0x01, 0x8D, 0x84, 0xC4,       // IEN: vertical blank
        0xCB,                               // WAI: the bases latch
        0x8D, 0x85, 0xC4,                   // STA ISR (A = 1): clear it
        0xCB,                               // WAI: a whole frame drawn with them
        0xAD, 0x89, 0xC6, 0x85, 0x10,       // LDA IRQH; STA $10
        0xDB,                               // STP
    };
    for (size_t i = 0; i < prog.size(); ++i)
        bus.poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
    bus.reset();
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    Registers regs;
    regs.pc = 0x0200;
    regs.p = 0x24;                          // I set: the IRQ only ends the WAI
    cpu.set_regs(regs);
    for (int i = 0; i < 2'000'000 && cpu.state() != Cpu6502::State::Stopped; ++i)
    {
        cpu.step();
        if (bus.irq())
            cpu.irq();
    }
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);
    CHECK_EQ(card->frames(), uint64_t{2});
    CHECK_EQ(bus.peek(0x0010), 0xFD);       // IRQH: bit 1 low, the video card (ID 9)
    CHECK_EQ(card->frame()[0], rgba(WHITE));
    CHECK_EQ(card->frame()[7 + 15 * 640], rgba(WHITE)); // the cell's far corner
    CHECK_EQ(card->frame()[8], BLACK);      // the next cell: nothing
    CHECK_EQ(bus.peek_physical(0x80800), 0x01); // drawn from the RAM, as written through the window
}

namespace
{
    /// A backplane in Apple II mode: the IIe's display drawn by the video card.
    struct AppleScreen
    {
        AppleScreen()
        {
            std::string error;
            m = Machine::build("cpu w65c02s\nnorthbridge backplane\nram 00000-FFFFF module=0\n"
                               "video 480-4FF space=io name=video irq\n",
                               "apple.machine", {}, error);
            if (!m)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            card = m->video_card();
            // A test character set: code n's row r is n ^ r (so every glyph differs), lit where 0.
            for (unsigned n = 0; n < 256; ++n)
                for (unsigned r = 0; r < 8; ++r)
                    bus().write_physical(0x80000 + VideoCard::APPLE_CHARS + n * 8 + r, static_cast<uint8_t>(n ^ r));
            bus().write(0xC685, BackplaneBridge::MODE_APPLE);
            if (bus().take_reset())
                bus().pulse_reset();
        }
        bool ok() const { return card != nullptr; }
        Decoder &bus() { return m->bus(); }
        void show() { card->tick(2 * 119'437); } // two frames at the card's 7.16 MHz
        /// The screen pixel of Apple dot (x, y): its top left (40-column text and graphics are 2x2).
        uint32_t dot(unsigned x, unsigned y) const { return card->frame()[(48 + y * 2) * 640 + 40 + x * 2]; }
        uint32_t px(unsigned x, unsigned y) const { return card->frame()[y * 640 + x]; }
        /// Glyph `code`'s row r as dots, lit where its bit is 0.
        static bool lit(unsigned code, unsigned r, unsigned b) { return !(((code ^ r) >> b) & 1); }

        std::unique_ptr<Machine> m;
        VideoCard *card{nullptr};
    };

    const uint32_t A_WHITE = VideoCard::rgba(0xFFF), A_BLACK = VideoCard::rgba(0x000);
    const uint32_t A_VIOLET = VideoCard::rgba(0xF4F), A_GREEN = VideoCard::rgba(0x1F4);
    const uint32_t A_BLUE = VideoCard::rgba(0x1CF), A_ORANGE = VideoCard::rgba(0xF64), A_MAGENTA = VideoCard::rgba(0xE26);
    const uint32_t A_DKGREEN = VideoCard::rgba(0x0A6);
} // namespace

TEST(apple_text_40_columns_from_the_character_rom_centred_on_black)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write(0x0400, 0xC1);                 // row 0, column 0
    a.bus().write(0x0400 + 0x28 * 2 + 0x80 * 7 + 39, 0x2A); // row 23, column 39 (inverse)
    a.show();
    size_t wrong = 0;
    for (unsigned r = 0; r < 8; ++r)
        for (unsigned b = 0; b < 7; ++b)
        {
            wrong += a.dot(b, r) != (AppleScreen::lit(0xC1, r, b) ? A_WHITE : A_BLACK);
            wrong += a.dot(273 + b, 184 + r) != (AppleScreen::lit(0x2A, r, b) ? A_WHITE : A_BLACK);
        }
    CHECK_EQ(wrong, size_t{0});
    CHECK_EQ(a.px(40 + 1, 48 + 1), a.dot(0, 0)); // a dot is 2x2
    CHECK_EQ(a.px(39, 48), A_BLACK);             // the border: black, not the VGA layers
    CHECK_EQ(a.px(600, 431), A_BLACK);
    CHECK_EQ(a.px(320, 47), A_BLACK);
}

TEST(apple_flashing_and_the_alternate_set)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write(0x0400, 0x41);                 // a flashing A (ALTCHARSET off)
    a.show();
    const bool first = a.dot(0, 0) == (AppleScreen::lit(0x01, 0, 0) ? A_WHITE : A_BLACK) &&
                       a.dot(1, 0) == (AppleScreen::lit(0x01, 0, 1) ? A_WHITE : A_BLACK);
    CHECK(first);                                // inverse first: code & $3F
    while (a.card->frames() < 18)
        a.card->tick(119'437);
    size_t as_normal = 0;
    for (unsigned b = 0; b < 7; ++b)
        as_normal += a.dot(b, 3) == (AppleScreen::lit(0x81, 3, b) ? A_WHITE : A_BLACK);
    CHECK_EQ(as_normal, size_t{7});              // then normal: (code & $3F) | $80
    a.bus().write(0xC00F, 0);                    // ALTCHARSET on: $41 as it is (MouseText)
    a.show();
    size_t as_is = 0;
    for (unsigned b = 0; b < 7; ++b)
        as_is += a.dot(b, 5) == (AppleScreen::lit(0x41, 5, b) ? A_WHITE : A_BLACK);
    CHECK_EQ(as_is, size_t{7});
}

TEST(apple_text_80_columns_aux_then_main_and_page_2)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write_physical(0x10400, 0xC1);       // aux: column 0
    a.bus().write_physical(0x00400, 0xC2);       // main: column 1
    a.bus().write(0xC00D, 0);                    // 80COL on
    a.show();
    size_t wrong = 0;
    for (unsigned b = 0; b < 7; ++b)
    {
        wrong += a.px(40 + b, 48 + 2 * 2) != (AppleScreen::lit(0xC1, 2, b) ? A_WHITE : A_BLACK);
        wrong += a.px(47 + b, 48 + 2 * 2) != (AppleScreen::lit(0xC2, 2, b) ? A_WHITE : A_BLACK); // 7 pixels a column
    }
    CHECK_EQ(wrong, size_t{0});
    a.bus().write(0xC00C, 0);                    // 40 columns, page 2
    a.bus().write_physical(0x00800, 0xC3);
    a.bus().read(0xC055);
    a.show();
    CHECK_EQ(a.dot(0, 1), AppleScreen::lit(0xC3, 1, 0) ? A_WHITE : A_BLACK);
    CHECK_EQ(a.dot(1, 1), AppleScreen::lit(0xC3, 1, 1) ? A_WHITE : A_BLACK);
    a.bus().write(0xC001, 0);                    // 80STORE: PAGE2 picks memory, the display stays on page 1
    a.show();
    size_t page1 = 0;
    for (unsigned b = 0; b < 7; ++b)             // the whole row: $C2 and $C3 differ in a dot or two
        page1 += a.dot(b, 1) == (AppleScreen::lit(0xC2, 1, b) ? A_WHITE : A_BLACK);
    CHECK_EQ(page1, size_t{7});
}

TEST(apple_lores_blocks_low_nibble_on_top)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write(0x0400, 0x41);                 // top: magenta (1), bottom: dark green (4)
    a.bus().read(0xC050);                        // graphics, HIRES off: lo-res
    a.show();
    CHECK_EQ(a.dot(0, 0), A_MAGENTA);
    CHECK_EQ(a.dot(6, 3), A_MAGENTA);            // a block is 7 dots by 4 lines
    CHECK_EQ(a.dot(0, 4), A_DKGREEN);
    CHECK_EQ(a.dot(6, 7), A_DKGREEN);
    CHECK_EQ(a.dot(7, 0), A_BLACK);              // the next block: 0
}

TEST(apple_hires_artifact_colours_white_pairs_and_the_half_dot_shift)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().read(0xC050);
    a.bus().read(0xC057);                        // hi-res, page 1 at $2000
    a.bus().write(0x2000, 0x01);                 // line 0: dot 0 alone, an even dot: violet
    a.bus().write(0x2001, 0x01);                 // dot 7 (the second byte's bit 0): odd: green
    a.bus().write(0x2400, 0x81);                 // line 1: dot 0, shifted half a dot: blue
    a.bus().write(0x2401, 0x81);                 // dot 7, shifted: orange
    a.bus().write(0x2800, 0x03);                 // line 2: dots 0 and 1 together: white
    a.bus().write(0x2000 + 4 * 0x400 + 4 * 0x80 + 1 * 0x28 + 5, 0x01); // line 100 (4 + 4 x 8 + 64), byte 5
    a.bus().write(0x2000 + 7 * 0x400 + 7 * 0x80 + 2 * 0x28 + 39, 0x40); // line 191, the last dot
    a.show();
    CHECK_EQ(a.dot(35, 100), A_GREEN);           // dot 35: odd
    CHECK_EQ(a.dot(279, 191), A_GREEN);
    CHECK_EQ(a.dot(0, 0), A_VIOLET);
    CHECK_EQ(a.dot(1, 0), A_BLACK);
    CHECK_EQ(a.dot(7, 0), A_GREEN);
    CHECK_EQ(a.px(40, 48 + 2), A_BLACK);         // shifted: the first pixel is still dark...
    CHECK_EQ(a.px(41, 48 + 2), A_BLUE);          // ...from the next one
    CHECK_EQ(a.px(42, 48 + 2), A_BLUE);
    CHECK_EQ(a.px(40 + 7 * 2 + 1, 48 + 2), A_ORANGE);
    CHECK_EQ(a.px(40 + 7 * 2, 48 + 2), A_BLACK);
    CHECK_EQ(a.dot(0, 2), A_WHITE);
    CHECK_EQ(a.dot(1, 2), A_WHITE);
    a.bus().write_physical(0x04000, 0x01);       // page 2, with PAGE2
    a.bus().write_physical(0x02000, 0x00);
    a.bus().read(0xC055);
    a.show();
    CHECK_EQ(a.dot(0, 0), A_VIOLET);
}

TEST(apple_mixed_mode_four_text_rows_under_graphics)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write(0x0400 + 0x28 * 2 + 0x80 * 4, 0xC1); // row 20, column 0
    a.bus().write(0x0400 + 0x28 * 2 + 0x80 * 3, 0x11); // row 19: a lo-res byte
    a.bus().read(0xC050);
    a.bus().read(0xC053);                        // MIXED
    a.show();
    CHECK_EQ(a.dot(0, 152), A_MAGENTA);          // row 19: graphics
    CHECK_EQ(a.dot(0, 160), AppleScreen::lit(0xC1, 0, 0) ? A_WHITE : A_BLACK); // row 20: text
    CHECK_EQ(a.dot(1, 161), AppleScreen::lit(0xC1, 1, 1) ? A_WHITE : A_BLACK);
    a.bus().read(0xC052);                        // not mixed: row 20 is graphics too
    a.show();
    CHECK_EQ(a.dot(0, 160), A_MAGENTA);          // row 20's byte $C1 as lo-res: 1 on top
}

TEST(apple_double_lores_80_blocks_aux_rotated)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write_physical(0x10400, 0x02);       // aux: nibble 2, shown rotated right a bit: 1, magenta
    a.bus().write_physical(0x00400, 0x01);       // main: 1, magenta as it is
    a.bus().write_physical(0x10401, 0x01);       // aux 1 -> 8, brown
    a.bus().read(0xC050);
    a.bus().write(0xC00D, 0);                    // 80COL
    a.bus().read(0xC05E);                        // AN3 off: double
    a.show();
    CHECK_EQ(a.px(40, 48), A_MAGENTA);           // block 0 (aux), 7 dots
    CHECK_EQ(a.px(46, 48), A_MAGENTA);
    CHECK_EQ(a.px(47, 48), A_MAGENTA);           // block 1 (main)
    CHECK_EQ(a.px(54, 48), VideoCard::rgba(0x670)); // block 2 (aux, $10401): brown
    a.bus().read(0xC05F);                        // AN3 on: plain lo-res, 40 blocks of 14 dots
    a.show();
    CHECK_EQ(a.px(40, 48), A_MAGENTA);           // main's $01: magenta
    CHECK_EQ(a.px(53, 48), A_MAGENTA);           // a 14-dot block now
    CHECK_EQ(a.px(54, 48), A_BLACK);             // and the next one main's $0401: 0 (double showed aux's brown)
}

TEST(apple_double_hires_560_dots_four_to_a_colour)
{
    // Column c's aux byte is dots 14c to 14c+6, its main byte 14c+7 to 14c+13 (bit 0 first).
    // Each aligned 4 dots make a pattern, bit n the dot at 4k+n: that pattern's lo-res colour.
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().write_physical(0x12000, 0x01);       // aux 0: dot 0
    a.bus().write_physical(0x02000, 0x0F);       // main 0: dots 7-10
    a.bus().write_physical(0x12001, 0x70);       // aux 1: dots 18-20
    a.bus().write_physical(0x02001, 0x01);       // main 1: dot 21
    a.bus().read(0xC050);
    a.bus().read(0xC057);
    a.bus().write(0xC00D, 0);
    a.bus().read(0xC05E);
    a.show();
    CHECK_EQ(a.px(40 + 0, 48), A_MAGENTA);       // dots 0-3:   0001 = 1
    CHECK_EQ(a.px(40 + 3, 48), A_MAGENTA);
    CHECK_EQ(a.px(40 + 4, 48), VideoCard::rgba(0x670)); // 4-7:  1000 = 8, brown
    CHECK_EQ(a.px(40 + 8, 48), VideoCard::rgba(0xDCF)); // 8-11: 0111 = 7, light blue
    CHECK_EQ(a.px(40 + 12, 48), A_BLACK);        // 12-15: nothing
    CHECK_EQ(a.px(40 + 16, 48), A_GREEN);        // 16-19: 1100 = 12
    CHECK_EQ(a.px(40 + 20, 48), A_VIOLET);       // 20-23: 0011 = 3
    CHECK_EQ(a.px(40 + 23, 48), A_VIOLET);
}

TEST(apple_hires_a_gap_between_two_dots_of_one_colour_takes_it)
{
    AppleScreen a;
    REQUIRE(a.ok());
    a.bus().read(0xC050);
    a.bus().read(0xC057);
    a.bus().write(0x2000, 0x05);                 // dots 0 and 2: both even, violet; 1 off between
    a.bus().write(0x2400, 0x11);                 // line 1: dots 0 and 4: a gap of 3 stays black
    a.bus().write(0x2800, 0x40);                 // line 2: dot 6, violet...
    a.bus().write(0x2801, 0x82);                 // ...and dot 8, shifted: blue. Two colours: no fill
    a.show();
    CHECK_EQ(a.px(40 + 7 * 2 + 1, 48 + 2 * 2), A_BLACK); // dot 7 is in the shifted byte: half a dot right
    CHECK_EQ(a.px(40 + 8 * 2 + 1, 48 + 2 * 2), A_BLUE);
    CHECK_EQ(a.dot(0, 0), A_VIOLET);
    CHECK_EQ(a.dot(1, 0), A_VIOLET);             // filled
    CHECK_EQ(a.dot(2, 0), A_VIOLET);
    CHECK_EQ(a.dot(3, 0), A_BLACK);
    CHECK_EQ(a.dot(2, 1), A_BLACK);
}

int main()
{
    return run_all();
}
