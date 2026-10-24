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
    VideoCard slow;
    slow.set_cpu_hz(1'020'484);                   // the panel's slowest clock
    slow.tick(1'020'484);
    CHECK_EQ(slow.frames(), uint64_t{60});        // still 60 a second
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

int main()
{
    return run_all();
}
