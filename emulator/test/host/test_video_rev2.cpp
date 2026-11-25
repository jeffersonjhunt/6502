/**
 * @file test_video_rev2.cpp
 * @brief The video card's revision 2 (hw/video-rev2.md, proposed): card memory, the register map,
 * the aperture, the data port and its DMA, and the line renderer drawing from card memory.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The first cases drive the card alone, its "CPU clock" the pixel clock (as test_video's do);
 * the later ones put it on a backplane machine: backplane.machine itself with the revision
 * chosen by MachineOptions, so the aperture, RAM C and DMA channel 1 are wired as its lines say.
 * Then layer types 6 and 7, bitmap 640 and direct 640 (hw/video640.md), and full-resolution sprites
 * (SPRCTL, hw/sprites640.md). Revision 1 keeps test_video.
 * Last, the debugger's `vram` (VideoCard::command), on the card and through a Debugger as the app
 * wires it.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "console_card.h"
#include "cpu6502.h"
#include "debugger.h"
#include "decoder.h"
#include "dma_controller.h"
#include "machine.h"
#include "symbols.h"
#include "video_card.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    using R2 = VideoCard::Rev2;
    constexpr uint64_t FRAME = uint64_t{VideoCard::LINE_CLOCKS} * VideoCard::LINES;
    constexpr uint8_t L0 = VideoCard::LAYER0, L1 = VideoCard::LAYER1;

    struct Rig
    {
        Rig()
        {
            if (!card.set_revision(2)) // a card on its own: never locked
                std::abort();
            card.set_cpu_hz(VideoCard::PIXEL_HZ); // one cycle, one pixel clock
        }
        void reg(uint8_t r, uint8_t v) { card.write(r, v); }
        void reg16(uint8_t r, uint16_t v)
        {
            card.write(r, static_cast<uint8_t>(v));
            card.write(r + 1u, static_cast<uint8_t>(v >> 8));
        }
        void paddr(uint32_t a)
        {
            reg(R2::PADDR, static_cast<uint8_t>(a));
            reg(R2::PADDR + 1, static_cast<uint8_t>(a >> 8));
            reg(R2::PADDR + 2, static_cast<uint8_t>(a >> 16));
        }
        uint32_t paddr() const
        {
            return uint32_t{card.peek(R2::PADDR)} | uint32_t{card.peek(R2::PADDR + 1)} << 8 |
                   uint32_t{card.peek(R2::PADDR + 2)} << 16;
        }
        void colour(uint32_t pal, unsigned index, uint16_t rgb12)
        {
            card.card_poke(pal + 2 * index, static_cast<uint8_t>(rgb12));
            card.card_poke(pal + 2 * index + 1, static_cast<uint8_t>(rgb12 >> 8));
        }
        void show() { card.tick(2 * FRAME); }
        uint32_t px(unsigned x, unsigned y) const { return card.frame()[y * VideoCard::WIDTH + x]; }

        VideoCard card;
    };

    uint32_t rgba(uint16_t c) { return VideoCard::rgba(c); }

    /// Every register's reset value on revision 2, $00-$7F (LINE and STATUS: the beam at line 0).
    uint8_t reset_value(unsigned o)
    {
        if (o >= R2::SLOT0 && o < R2::SLOT0 + 8)
            return o & 1 ? 0 : static_cast<uint8_t>((o - R2::SLOT0) / 2); // SLOTn = n
        switch (o)
        {
        case R2::PSTEP: return 1;
        case R2::MEMSIZE: return 32;
        case VideoCard::ID_REG: return 0x92;
        default: return 0;
        }
    }

    /// The bytes revision 2 reserves: they read 0 whatever is written.
    bool reserved(unsigned o)
    {
        if (o == 0x02 || o == 0x03 || o == 0x0F) // $0E is SPRCTL (hw/sprites640.md)
            return true;
        if (o >= 0x10 && o < 0x30)
            return (o & 0xF) >= 0xA;
        return o >= 0x40 && o < 0x7F;
    }

    std::string read_text(const std::string &path)
    {
        std::ifstream in(path);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    /// backplane.machine, flash erased, the video card at `rev` (0: the file's own).
    std::unique_ptr<Machine> backplane(unsigned rev)
    {
        MachineOptions opt;
        opt.rom_override = "-";
        opt.video_revision = rev;
        std::string error;
        auto m = Machine::build(read_text(EMU_MACHINE_DIR "/backplane.machine"), "backplane.machine", opt, error);
        if (!m)
            std::printf("  %s\n", error.c_str());
        return m;
    }
} // namespace

// --- the register map ---------------------------------------------------------

TEST(every_register_resets_to_its_value_and_the_id_is_92)
{
    Rig r;
    for (unsigned o = 0; o < 0x80; ++o)
    {
        if (r.card.peek(o) != reset_value(o))
            std::printf("  $%02X reads $%02X\n", o, r.card.peek(o));
        CHECK_EQ(r.card.peek(o), reset_value(o));
    }
    CHECK_EQ(r.card.revision(), 2u);
}

TEST(reserved_bytes_read_0_whatever_is_written)
{
    Rig r;
    unsigned n = 0;
    for (unsigned o = 0; o < 0x80; ++o)
    {
        if (!reserved(o))
            continue;
        r.reg(static_cast<uint8_t>(o), 0xFF);
        CHECK_EQ(r.card.peek(o), 0x00);
        ++n;
    }
    CHECK_EQ(n, 3u + 2 * 6 + 63);
}

TEST(registers_read_back_at_their_frozen_addresses)
{
    Rig r;
    r.reg(VideoCard::CTRL, 0xFF);
    r.reg(VideoCard::BORDER, 0x12);
    r.reg(VideoCard::IEN, 0xFF);
    r.reg16(R2::PALBASE, 0xA1B2);
    r.reg16(R2::SPRBASE, 0xC3D4);
    CHECK_EQ(0x8A, 0x80 + R2::PALBASE); // $C48A
    CHECK_EQ(0x8C, 0x80 + R2::SPRBASE); // $C48C
    CHECK_EQ(r.card.peek(VideoCard::CTRL), 0x0F);
    CHECK_EQ(r.card.peek(VideoCard::BORDER), 0x12);
    CHECK_EQ(r.card.peek(VideoCard::IEN), 0x07);
    CHECK_EQ(r.card.peek(0x0A), 0xB2);
    CHECK_EQ(r.card.peek(0x0B), 0xA1);
    CHECK_EQ(r.card.peek(0x0C), 0xD4);
    CHECK_EQ(r.card.peek(0x0D), 0xC3);
    for (uint8_t layer : {L0, L1})
    {
        const uint8_t k = layer == L0 ? 0x00 : 0x80;
        r.reg(layer + 0x0, static_cast<uint8_t>(0x15 ^ k));         // CONFIG
        r.reg16(layer + 0x1, static_cast<uint16_t>(0x1234 ^ k));    // MAPBASE
        r.reg16(layer + 0x3, static_cast<uint16_t>(0x5678 ^ k));    // TILEBASE
        r.reg16(layer + 0x5, 0xFFFF);                               // HSCROLL: 10 bits
        r.reg16(layer + 0x7, static_cast<uint16_t>(0x0123 + k));    // VSCROLL
        r.reg(layer + 0x9, 0x5A);                                   // PALOFS: 4 bits
    }
    for (uint8_t layer : {L0, L1})
    {
        const uint8_t k = layer == L0 ? 0x00 : 0x80;
        CHECK_EQ(r.card.peek(layer + 0x0), 0x15 ^ k);
        CHECK_EQ(r.card.peek(layer + 0x1), 0x34 ^ k);
        CHECK_EQ(r.card.peek(layer + 0x2), 0x12);
        CHECK_EQ(r.card.peek(layer + 0x3), 0x78 ^ k);
        CHECK_EQ(r.card.peek(layer + 0x4), 0x56);
        CHECK_EQ(r.card.peek(layer + 0x5), 0xFF);
        CHECK_EQ(r.card.peek(layer + 0x6), 0x03);
        CHECK_EQ(r.card.peek(layer + 0x7), (0x0123 + k) & 0xFF);
        CHECK_EQ(r.card.peek(layer + 0x8), 0x01);
        CHECK_EQ(r.card.peek(layer + 0x9), 0x0A);
    }
    for (unsigned n = 0; n < 4; ++n)
        r.reg16(static_cast<uint8_t>(R2::SLOT0 + 2 * n), static_cast<uint16_t>(0x1100 * (n + 1) + 7));
    for (unsigned n = 0; n < 4; ++n)
    {
        CHECK_EQ(r.card.peek(0x30 + 2 * n), (0x1100 * (n + 1) + 7) & 0xFF);
        CHECK_EQ(r.card.peek(0x31 + 2 * n), (0x1100 * (n + 1)) >> 8);
    }
    r.paddr(0xABCDEF);
    CHECK_EQ(r.card.peek(0x38), 0xEF);
    CHECK_EQ(r.card.peek(0x39), 0xCD);
    CHECK_EQ(r.card.peek(0x3A), 0xAB);
    r.reg16(R2::PSTEP, 0x0280);
    CHECK_EQ(r.card.peek(0x3C), 0x80);
    CHECK_EQ(r.card.peek(0x3D), 0x02);
    r.reg(R2::PCTL, 0xFF);
    CHECK_EQ(r.card.peek(0x3E), 0x07); // other bits read 0
    r.reg(R2::MEMSIZE, 0x01);
    r.reg(VideoCard::ID_REG, 0x01);
    CHECK_EQ(r.card.peek(0x3F), 32);   // read-only
    CHECK_EQ(r.card.peek(0x7F), 0x92);
}

TEST(bus_reset_resets_the_registers_and_never_card_memory)
{
    Rig r;
    for (uint32_t a : {0x000000u, 0x002000u, 0x0FFFFFu, 0x1E0000u, 0x1FFFFFu})
        r.card.card_poke(a, static_cast<uint8_t>(a * 3 + 1));
    r.reg16(R2::PALBASE, 0x1234);
    r.reg16(R2::SLOT0 + 4, 0x0077);
    r.paddr(0x012345);
    r.reg16(R2::PSTEP, 160);
    r.reg(R2::PCTL, 0x07);
    r.reg(L1 + 0x9, 0x03);
    r.card.bus_reset();
    for (unsigned o = 0; o < 0x80; ++o) // PDATA: the byte at PADDR 0, which /RESET left alone
        CHECK_EQ(r.card.peek(o), o == R2::PDATA ? 0x01 : reset_value(o));
    for (uint32_t a : {0x000000u, 0x002000u, 0x0FFFFFu, 0x1E0000u, 0x1FFFFFu})
        CHECK_EQ(r.card.card_peek(a), static_cast<uint8_t>(a * 3 + 1));
    r.card.reset(); // power-on reset too: SRAM keeps what it holds
    CHECK_EQ(r.card.card_peek(0x1E0000), static_cast<uint8_t>(0x1E0000 * 3 + 1));
}

TEST(memsize_is_32_and_memory_past_it_reads_ff)
{
    Rig r;
    CHECK_EQ(r.card.peek(R2::MEMSIZE), 32);
    CHECK_EQ(uint32_t{r.card.peek(R2::MEMSIZE)} << 16, VideoCard::MEMORY_SIZE);
    r.card.card_poke(0x200000, 0x12); // ignored
    CHECK_EQ(r.card.card_peek(0x200000), 0xFF);
    CHECK_EQ(r.card.card_peek(0xFFFFFF), 0xFF);
}

TEST(set_revision_1_is_todays_card)
{
    VideoCard c;
    CHECK_EQ(c.revision(), 1u);       // the default, until the BIOS and OODOS move
    CHECK_EQ(c.peek(VideoCard::ID_REG), 0x91);
    CHECK(c.set_revision(2));
    CHECK_EQ(c.peek(VideoCard::ID_REG), 0x92);
    CHECK(c.set_revision(1));
    CHECK_EQ(c.peek(VideoCard::ID_REG), 0x91);
    CHECK_EQ(c.peek(0x40), 0xFF);       // revision 1's unused bytes read $FF
    c.write(VideoCard::PALBASE, 0x34);  // and PALBASE is at $02
    CHECK_EQ(c.peek(VideoCard::PALBASE), 0x34);
    CHECK(!c.drq());
    c.write(R2::PCTL, 0x01);            // nothing there on revision 1
    CHECK(!c.drq());
}

// --- the port -------------------------------------------------------------------

TEST(pdata_steps_paddr_up_by_pstep_and_peek_holds_it)
{
    Rig r;
    r.paddr(0x002000);
    for (unsigned i = 0; i < 4; ++i)
        r.reg(R2::PDATA, static_cast<uint8_t>(0xA0 + i));
    CHECK_EQ(r.paddr(), 0x002004u);
    for (unsigned i = 0; i < 4; ++i)
        CHECK_EQ(r.card.card_peek(0x002000 + i), 0xA0 + i);
    r.paddr(0x002001);
    CHECK_EQ(r.card.peek(R2::PDATA), 0xA1); // peek: no step
    CHECK_EQ(r.paddr(), 0x002001u);
    CHECK_EQ(r.card.read(R2::PDATA), 0xA1);
    CHECK_EQ(r.card.read(R2::PDATA), 0xA2);
    CHECK_EQ(r.paddr(), 0x002003u);
}

TEST(a_step_of_160_writes_a_column_of_the_80_column_screen)
{
    Rig r;
    r.reg16(R2::PSTEP, 160);
    r.paddr(0x002000 + 2 * 5); // column 5's characters
    for (unsigned row = 0; row < 30; ++row)
        r.reg(R2::PDATA, static_cast<uint8_t>('A' + row));
    CHECK_EQ(r.paddr(), 0x002000u + 10 + 30 * 160);
    bool column = true;
    for (unsigned row = 0; row < 30; ++row)
        column = column && r.card.card_peek(0x002000 + row * 160 + 10) == 'A' + row &&
                 r.card.card_peek(0x002000 + row * 160 + 11) == 0;
    CHECK(column);
}

TEST(pctl_bit_2_steps_down_and_a_step_of_0_holds)
{
    Rig r;
    r.reg(R2::PCTL, R2::PCTL_DOWN);
    r.reg16(R2::PSTEP, 2);
    r.paddr(0x000010);
    r.reg(R2::PDATA, 0x11);
    r.reg(R2::PDATA, 0x22);
    CHECK_EQ(r.paddr(), 0x00000Cu);
    CHECK_EQ(r.card.card_peek(0x10), 0x11);
    CHECK_EQ(r.card.card_peek(0x0E), 0x22);
    r.reg16(R2::PSTEP, 0);
    r.reg(R2::PDATA, 0x33);
    r.reg(R2::PDATA, 0x44);
    CHECK_EQ(r.paddr(), 0x00000Cu);
    CHECK_EQ(r.card.card_peek(0x0C), 0x44);
}

TEST(paddr_wraps_at_the_fitted_memorys_end_both_ways)
{
    Rig r;
    r.paddr(0x1FFFFF);
    r.reg(R2::PDATA, 0x5A);
    CHECK_EQ(r.paddr(), 0x000000u);
    CHECK_EQ(r.card.card_peek(0x1FFFFF), 0x5A);
    r.reg16(R2::PSTEP, 0x100);
    r.paddr(0x1FFF80);
    r.reg(R2::PDATA, 0x01);
    CHECK_EQ(r.paddr(), 0x000080u);
    r.reg(R2::PCTL, R2::PCTL_DOWN);
    r.reg(R2::PDATA, 0x02);
    CHECK_EQ(r.paddr(), 0x1FFF80u);
}

TEST(port_addresses_past_memsize_read_ff_and_ignore_writes)
{
    Rig r;
    r.paddr(0x200000);
    r.reg(R2::PDATA, 0x12);
    CHECK_EQ(r.paddr(), 0x200001u);
    r.paddr(0x200000);
    CHECK_EQ(r.card.read(R2::PDATA), 0xFF);
    CHECK_EQ(r.card.card_peek(0x000000), 0x00); // and nothing landed in range
}

// --- the renderer, from card memory ----------------------------------------------

TEST(a_text_screen_is_drawn_from_card_memory_at_oodos_offsets)
{
    // The console's layout (hw/video-rev2.md, "Card memory layout"): palette $000000,
    // font $000400, text $002000; in 256-byte units 0, 4 and $20.
    Rig r;
    r.colour(0, 1, 0xFFF);
    r.colour(0, 2, 0x00F);
    for (unsigned row = 0; row < 16; ++row)
        r.card.card_poke(0x000400 + 'X' * 16 + row, 0xF0);   // the left half lit
    r.card.card_poke(0x002000 + (2 * 80 + 3) * 2, 'X');       // row 2, column 3
    r.card.card_poke(0x002000 + (2 * 80 + 3) * 2 + 1, 0x21);  // white on blue
    r.reg(L0 + VideoCard::CONFIG, VideoCard::TEXT80);
    r.reg16(L0 + R2::MAPBASE, 0x0020);
    r.reg16(L0 + R2::TILEBASE, 0x0004);
    r.reg16(R2::PALBASE, 0x0000);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(3 * 8, 2 * 16), rgba(0xFFF));
    CHECK_EQ(r.px(3 * 8 + 3, 2 * 16 + 15), rgba(0xFFF));
    CHECK_EQ(r.px(3 * 8 + 4, 2 * 16), rgba(0x00F));
    CHECK_EQ(r.px(4 * 8, 2 * 16), rgba(0x000)); // the next cell: 0 on 0
}

TEST(bases_are_24_bit_card_addresses_in_256_byte_units)
{
    // Everything past the first 256 KB, where revision 1 could never reach, and on odd 256-byte units.
    Rig r;
    const uint32_t pal = 0x1A0100, font = 0x0F4300, text = 0x150500;
    r.colour(pal, 1, 0x0F0);
    r.colour(pal, 5, 0xF00);
    for (unsigned row = 0; row < 16; ++row)
        r.card.card_poke(font + 1 * 16 + row, 0xFF);
    r.card.card_poke(text, 1);
    r.card.card_poke(text + 1, 0x51);
    r.reg(L1 + VideoCard::CONFIG, VideoCard::TEXT40);
    r.reg16(L1 + R2::MAPBASE, static_cast<uint16_t>(text >> 8));
    r.reg16(L1 + R2::TILEBASE, static_cast<uint16_t>(font >> 8));
    r.reg16(R2::PALBASE, static_cast<uint16_t>(pal >> 8));
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER1_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(0x0F0));
    CHECK_EQ(r.px(15, 15), rgba(0x0F0)); // 40 columns: doubled
    r.reg(L1 + R2::HSCROLL, 8);          // scroll by one character: the cell's background, entry 5... of cell 1
    r.card.card_poke(text + 2, 0);
    r.card.card_poke(text + 3, 0x50);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(0xF00));
}

TEST(tiles_and_a_bitmap_come_from_card_memory)
{
    Rig r;
    const uint32_t pal = 0x100000, map = 0x180000, tiles = 0x1B0000;
    r.colour(pal, 0x13, 0xF0F);
    // map entry 0: tile 2, palette offset 1; tile 2 (8x8, 4 bits) all pixel 3
    r.card.card_poke(map, 2);
    r.card.card_poke(map + 1, 0x10);
    for (unsigned i = 0; i < 32; ++i)
        r.card.card_poke(tiles + 2 * 32 + i, 0x33);
    r.reg(L0 + VideoCard::CONFIG, VideoCard::TILE);
    r.reg16(L0 + R2::MAPBASE, static_cast<uint16_t>(map >> 8));
    r.reg16(L0 + R2::TILEBASE, static_cast<uint16_t>(tiles >> 8));
    r.reg16(R2::PALBASE, static_cast<uint16_t>(pal >> 8));
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.px(0, 0), rgba(0xF0F));
    CHECK_EQ(r.px(15, 15), rgba(0xF0F)); // 8x8 doubled
    // bitmap 256 at 8 bits from $0C0000: pixel (0, 0) colour $13
    r.card.card_poke(0x0C0000, 0x13);
    r.reg(L0 + VideoCard::CONFIG, VideoCard::BITMAP256);
    r.reg16(L0 + R2::MAPBASE, 0x0C00);
    r.show();
    CHECK_EQ(r.px(64, 0), rgba(0xF0F));
}

TEST(a_sprite_image_address_is_15_bits_of_256_byte_units)
{
    Rig r;
    const uint32_t pal = 0x000000, table = 0x1C8000, image = 0x123400; // unit $1234
    r.colour(pal, 0x27, 0x0FF);
    // entry 0: image unit $1234, 4 bits; X 10, Y 20; depth 3, palette offset 2, 8x8
    const uint8_t e[8] = {0x34, 0x12, 10, 0, 20, 0, 3 << 2, 0x02};
    for (unsigned i = 0; i < 8; ++i)
        r.card.card_poke(table + i, e[i]);
    for (unsigned i = 0; i < 32; ++i)
        r.card.card_poke(image + i, 0x77);
    r.reg16(R2::SPRBASE, static_cast<uint16_t>(table >> 8));
    r.reg16(R2::PALBASE, 0);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::SPRITES_ON);
    r.show();
    CHECK_EQ(r.px(20, 40), rgba(0x0FF));
    CHECK_EQ(r.px(35, 55), rgba(0x0FF));
    CHECK_EQ(r.px(36, 40), rgba(0x000));
    // byte 1 bit 7 is still the depth: 8 bits, the image's bytes are the colours
    r.card.card_poke(table + 1, 0x92);
    r.colour(pal, 0x77, 0xF80);
    for (unsigned i = 0; i < 64; ++i)
        r.card.card_poke(image + i, 0x77);
    r.show();
    CHECK_EQ(r.px(20, 40), rgba(0xF80));
}

// --- on the machine: the aperture and DMA ------------------------------------------

TEST(backplane_machine_is_revision_2_by_default)
{
    auto m = backplane(0);
    REQUIRE(m != nullptr);
    REQUIRE(m->video_card() != nullptr);
    CHECK_EQ(m->video_card()->revision(), 2u);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC4FF), 0x92);
    bus.write_physical(0x80800, 0x5A); // bank 8 is ordinary RAM again: the card draws from its own memory
    CHECK_EQ(bus.peek_physical(0x80800), 0x5A);
}

TEST(on_revision_2_bank_8_is_the_aperture_and_banks_9_to_b_are_ram)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    REQUIRE(card != nullptr);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC4FF), 0x92);
    CHECK_EQ(bus.peek(0xC4BF), 32);
    // At reset SLOTn = n: the aperture shows card memory $000000-$00FFFF in order.
    for (unsigned n = 0; n < 4; ++n)
        card->card_poke(n * 0x4000 + 0x123, static_cast<uint8_t>(0xC0 + n));
    for (unsigned n = 0; n < 4; ++n)
        CHECK_EQ(bus.peek_physical(0x80000 + n * 0x4000 + 0x123), 0xC0 + n);
    // Banks 9-B: plain RAM, not the card's.
    bus.write_physical(0x90000, 0x11);
    bus.write_physical(0xBFFFF, 0x22);
    CHECK_EQ(bus.peek_physical(0x90000), 0x11);
    CHECK_EQ(bus.peek_physical(0xBFFFF), 0x22);
    CHECK_EQ(card->card_peek(0x10000), 0x00);
    CHECK_EQ(card->card_peek(0x0FFFF), 0x00);
}

TEST(each_slot_maps_its_own_page_for_reads_and_writes)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    const uint16_t pages[4] = {0x0007, 0x0050, 0x007F, 0x0010};
    for (unsigned n = 0; n < 4; ++n)
    {
        bus.write(static_cast<uint16_t>(0xC4B0 + 2 * n), static_cast<uint8_t>(pages[n]));
        bus.write(static_cast<uint16_t>(0xC4B1 + 2 * n), static_cast<uint8_t>(pages[n] >> 8));
    }
    for (unsigned n = 0; n < 4; ++n)
    {
        bus.write_physical(0x80000 + n * 0x4000, static_cast<uint8_t>(0x10 + n));
        bus.write_physical(0x80000 + n * 0x4000 + 0x3FFF, static_cast<uint8_t>(0x20 + n));
    }
    for (unsigned n = 0; n < 4; ++n)
    {
        CHECK_EQ(card->card_peek(uint32_t{pages[n]} * 0x4000), 0x10 + n);
        CHECK_EQ(card->card_peek(uint32_t{pages[n]} * 0x4000 + 0x3FFF), 0x20 + n);
    }
    card->card_poke(0x7F * 0x4000 + 0x42, 0x99);       // the last page of 2 MB
    CHECK_EQ(bus.peek_physical(0x88042), 0x99);
    // Through a CPU window: W1 on block 32 + 2 (bank 8's third 16 KB, slot 2).
    bus.write(0xC681, 34);
    CHECK_EQ(bus.read(0x4042), 0x99);
    bus.write(0x4043, 0x98);
    CHECK_EQ(card->card_peek(0x7F * 0x4000 + 0x43), 0x98);
}

TEST(aperture_pages_past_memsize_read_ff_and_ignore_writes)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    bus.write(0xC4B2, 0x80); // SLOT1 = page $80: card address $200000, past 2 MB
    bus.write(0xC4B3, 0x00);
    bus.write_physical(0x84000, 0x12);
    CHECK_EQ(bus.peek_physical(0x84000), 0xFF);
    bus.write(0xC4B3, 0x01); // $0180: far past
    CHECK_EQ(bus.peek_physical(0x84000), 0xFF);
    CHECK_EQ(m->video_card()->card_peek(0x000000), 0x00);
    CHECK_EQ(m->video_card()->card_peek(0x004000), 0x00); // nothing wrapped in
}

TEST(the_aperture_ignores_cycles_in_apple_ii_mode_and_memory_survives_the_pulse)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    card->card_poke(0x1E0000, 0x3C); // as C806R will put the character ROM there
    bus.write_physical(0x80010, 0x77);
    CHECK_EQ(card->card_peek(0x10), 0x77);
    bus.write(0xC4B0, 0x05); // SLOT0 = 5
    bus.write(0xC685, BackplaneBridge::MODE_APPLE);
    REQUIRE(bus.take_reset());
    bus.pulse_reset();
    REQUIRE(m->backplane_bridge()->apple());
    CHECK_EQ(bus.peek_physical(0x80010), 0xFF); // ignored: a floating bus
    bus.write_physical(0x80010, 0x55);
    CHECK_EQ(card->card_peek(0x10), 0x77);
    CHECK_EQ(card->card_peek(0x1E0000), 0x3C);  // /RESET never touches card memory
    CHECK_EQ(card->peek(R2::SLOT0), 0x00);      // ...but resets the registers
}

namespace
{
    /// DMA channel 1, `count` bytes between system RAM at `addr` and the card, block mode.
    void dma_channel_1(Decoder &bus, uint32_t addr, unsigned count, uint8_t dir)
    {
        // SELECT 1 first: the window at $C400-$C40F then shows channel 1
        bus.poke(0xC47E, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 1 << 1);
        bus.poke(0xC400, static_cast<uint8_t>(addr));
        bus.poke(0xC401, static_cast<uint8_t>(addr >> 8));
        bus.poke(0xC402, static_cast<uint8_t>(addr >> 16));
        bus.poke(0xC404, static_cast<uint8_t>(count));
        bus.poke(0xC405, static_cast<uint8_t>(count >> 8));
        bus.poke(0xC406, DmaController::BLOCK | dir);
        bus.poke(0xC407, DmaController::ENABLE);
        for (unsigned i = 0; i < count + 100; ++i)
            bus.read(0x0000);
    }
} // namespace

TEST(drq_on_channel_1_streams_system_ram_into_card_memory)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    for (unsigned i = 0; i < 4096; ++i) // a 4 KB font
        bus.write_physical(0x20000 + i, static_cast<uint8_t>(i * 13 + 5));
    bus.write(0xC4B8, 0x00); // PADDR $000400
    bus.write(0xC4B9, 0x04);
    bus.write(0xC4BA, 0x00);
    CHECK(!card->drq());
    bus.write(0xC4BE, R2::PCTL_DRQ); // to the card
    CHECK(card->drq());
    dma_channel_1(bus, 0x20000, 4096, DmaController::TO_CARD);
    bool same = true;
    for (unsigned i = 0; i < 4096; ++i)
        same = same && card->card_peek(0x400 + i) == static_cast<uint8_t>(i * 13 + 5);
    CHECK(same);
    CHECK_EQ(card->card_peek(0x400 + 4096), 0x00); // and no further
    CHECK_EQ(bus.peek(0xC4B8), 0x00);              // PADDR $001400
    CHECK_EQ(bus.peek(0xC4B9), 0x14);
}

TEST(drq_on_channel_1_streams_card_memory_back_with_a_step)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    for (unsigned row = 0; row < 30; ++row) // column 7 of the console's text, a byte a row
        card->card_poke(0x002000 + row * 160 + 14, static_cast<uint8_t>('a' + row));
    bus.write(0xC4B8, 14); // PADDR $00200E
    bus.write(0xC4B9, 0x20);
    bus.write(0xC4BC, 160); // PSTEP 160
    bus.write(0xC4BD, 0);
    bus.write(0xC4BE, R2::PCTL_DRQ | R2::PCTL_FROM_CARD);
    dma_channel_1(bus, 0x30000, 30, DmaController::TO_MEMORY);
    bool same = true;
    for (unsigned row = 0; row < 30; ++row)
        same = same && bus.peek_physical(0x30000 + row) == 'a' + row;
    CHECK(same);
    CHECK_EQ(bus.peek_physical(0x30000 + 30), 0x00);
}

TEST(a_dack_cycle_against_pctls_direction_moves_nothing)
{
    Rig r;
    r.reg(R2::PCTL, R2::PCTL_DRQ); // to the card
    CHECK_EQ(r.card.dack_read(false), 0xFF);
    CHECK_EQ(r.paddr(), 0u);
    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_FROM_CARD);
    r.card.dack_write(0x42, false);
    CHECK_EQ(r.paddr(), 0u);
    CHECK_EQ(r.card.card_peek(0), 0x00);
    r.reg(R2::PCTL, 0);
    CHECK(!r.card.drq());
}

TEST(machine_lines_check_the_revision_and_the_aperture)
{
    std::string error;
    const std::string head = "cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n";
    CHECK(Machine::build(head + "video 480-4FF space=io name=video rev=3\n", "v.machine", {}, error) == nullptr);
    CHECK(error.find("rev:") != std::string::npos);
    CHECK(Machine::build(head + "usb 500-57F space=io rev=2\n", "v.machine", {}, error) == nullptr);
    CHECK(error.find("only the video and sound cards") != std::string::npos);
    auto none = Machine::build(head + "aperture 80000-8FFFF\n", "v.machine", {}, error); // no card: inert
    REQUIRE(none != nullptr);
    none->bus().write_physical(0x80000, 0x42);
    CHECK_EQ(none->bus().peek_physical(0x80000), 0xFF); // AP8 set at reset: the write went to the empty aperture
    none->bus().write(0xC6A5, 0);                       // MAP: AP8 clear, as the BIOS does with no card
    none->bus().write_physical(0x80000, 0x42);
    CHECK_EQ(none->bus().peek_physical(0x80000), 0x42); // the RAM answers
    CHECK(Machine::build(head + "video 480-4FF space=io rev=2\naperture 80000-87FFF\n", "v.machine", {}, error) ==
          nullptr);
    CHECK(error.find("64 KB") != std::string::npos);
    // Revision 2 needs no RAM at banks 8-B; revision 1 does.
    const std::string low = "cpu w65c02s\nnorthbridge backplane\nram 00000-7FFFF\n";
    auto m = Machine::build(low + "video 480-4FF space=io rev=2\naperture 80000-8FFFF\n", "v.machine", {}, error);
    CHECK(m != nullptr);
    CHECK(Machine::build(low + "video 480-4FF space=io rev=1\n", "v.machine", {}, error) == nullptr);
    CHECK(error.find("banks 8-B") != std::string::npos);
    // The old spelling, 80000-8FFFF, is the aperture at its own place, E00000, with bank 8 sent there:
    // the bus still has the RAM at bank 8 (B1, review V3).
    auto old = Machine::build(head + "video 480-4FF space=io rev=2\naperture 80000-8FFFF\n", "v.machine", {}, error);
    REQUIRE(old != nullptr);
    Decoder &bus = old->bus();
    old->video_card()->card_poke(0x4123, 0xC3);
    bus.write_bus(0x084123, 0x5A);
    CHECK_EQ(bus.peek_bus(0x084123), 0x5A);       // the RAM, as the bus carries it
    CHECK_EQ(bus.peek_physical(0xE04123), 0xC3);  // the card, at the aperture's own address
    CHECK_EQ(bus.peek_physical(0x084123), 0xC3);  // and the CPU card's bank 8
    bus.write(0xC681, 33);                        // W1 on block 33: slot 1
    CHECK_EQ(bus.read(0x4123), 0xC3);
    CHECK_EQ(bus.last_access().physical, 0xE04123u);
}

TEST(the_aperture_line_is_at_e00000_or_the_old_bank_8_spelling_and_nowhere_else)
{
    std::string error;
    const std::string head = "cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF\nvideo 480-4FF space=io rev=2\n";
    CHECK(Machine::build(head + "aperture E00000-E0FFFF\n", "v.machine", {}, error) != nullptr);
    CHECK(Machine::build(head + "aperture 80000-8FFFF\n", "v.machine", {}, error) != nullptr);
    for (const char *line : {"aperture E04000-E13FFF\n", "aperture 90000-9FFFF\n", "aperture 100000-10FFFF\n"})
    {
        CHECK(Machine::build(head + line, "v.machine", {}, error) == nullptr);
        CHECK(error.find("aperture: want 64 KB at E00000-E0FFFF") != std::string::npos);
    }
}

// --- review fixes: Apple II mode, DMA modes, the aperture by DMA, latching, the build -----

TEST(revision_2_apple_text_reads_the_character_rom_from_card_1e0000)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    // A test character set (code n's row r is n ^ r, lit where 0) where C806R will put it on revision 2;
    // card $03E000, revision 1's place, holds zeros (every dot lit), so reading there shows.
    for (unsigned n = 0; n < 256; ++n)
        for (unsigned r = 0; r < 8; ++r)
            card->card_poke(VideoCard::APPLE_CHARS_V2 + n * 8 + r, static_cast<uint8_t>(n ^ r));
    bus.write(0xC685, BackplaneBridge::MODE_APPLE);
    REQUIRE(bus.take_reset());
    bus.pulse_reset();
    bus.write(0x0400, 0xC1);                                // row 0, column 0
    bus.write(0x0400 + 0x28 * 2 + 0x80 * 7 + 39, 0x2A);     // row 23, column 39 (inverse)
    card->tick(2 * 119'437);                                // two frames at 7.16 MHz
    auto dot = [&](unsigned x, unsigned y) { return card->frame()[(48 + y * 2) * 640 + 40 + x * 2]; };
    auto lit = [](unsigned code, unsigned r, unsigned b) { return !(((code ^ r) >> b) & 1); };
    const uint32_t white = rgba(0xFFF), black = rgba(0x000);
    size_t wrong = 0;
    for (unsigned r = 0; r < 8; ++r)
        for (unsigned b = 0; b < 7; ++b)
        {
            wrong += dot(b, r) != (lit(0xC1, r, b) ? white : black);
            wrong += dot(273 + b, 184 + r) != (lit(0x2A, r, b) ? white : black);
        }
    CHECK_EQ(wrong, size_t{0});
    CHECK_EQ(card->frame()[39 + 48 * 640], black);          // the border: black
}

namespace
{
    /// DMA channel `n`, `count` bytes between system RAM at `addr` and the channel's card, in `mode`.
    void dma_card(Decoder &bus, unsigned n, uint32_t addr, unsigned count, uint8_t mode)
    {
        // SELECT n first: the window at $C400-$C40F then shows channel n
        bus.poke(0xC47E, static_cast<uint8_t>(DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | n << 1));
        const uint16_t c = 0xC400;
        bus.poke(c + 0u, static_cast<uint8_t>(addr));
        bus.poke(c + 1u, static_cast<uint8_t>(addr >> 8));
        bus.poke(c + 2u, static_cast<uint8_t>(addr >> 16));
        bus.poke(c + 4u, static_cast<uint8_t>(count));
        bus.poke(c + 5u, static_cast<uint8_t>(count >> 8));
        bus.poke(c + 6u, mode);
        bus.poke(c + 7u, DmaController::ENABLE);
        for (unsigned i = 0; i < 2 * count + 100; ++i)
            bus.read(0x0000);
    }
} // namespace

TEST(the_port_takes_dma_in_demand_and_single_modes_and_stepping_down)
{
    for (uint8_t xfer : {DmaController::DEMAND, DmaController::SINGLE})
    {
        auto m = backplane(2);
        REQUIRE(m != nullptr);
        VideoCard *card = m->video_card();
        Decoder &bus = m->bus();
        for (unsigned i = 0; i < 200; ++i)
            bus.write_physical(0x20000 + i, static_cast<uint8_t>(i * 5 + xfer));
        bus.write(0xC4B8, 0x00); // PADDR $003000
        bus.write(0xC4B9, 0x30);
        bus.write(0xC4BE, R2::PCTL_DRQ);
        dma_card(bus, 1, 0x20000, 200, static_cast<uint8_t>(xfer | DmaController::TO_CARD));
        bool same = true;
        for (unsigned i = 0; i < 200; ++i)
            same = same && card->card_peek(0x3000 + i) == static_cast<uint8_t>(i * 5 + xfer);
        CHECK(same);
        CHECK_EQ(card->card_peek(0x3000 + 200), 0x00);
        CHECK_EQ(bus.peek(0xC4B8), 200);  // PADDR $0030C8
        CHECK_EQ(bus.peek(0xC4B9), 0x30);
        // Down: the same bytes stored backwards from $004063.
        bus.write(0xC4B8, 0x63);
        bus.write(0xC4B9, 0x40);
        bus.write(0xC4BE, R2::PCTL_DRQ | R2::PCTL_DOWN);
        dma_card(bus, 1, 0x20000, 100, static_cast<uint8_t>(xfer | DmaController::TO_CARD));
        same = true;
        for (unsigned i = 0; i < 100; ++i)
            same = same && card->card_peek(0x4063 - i) == static_cast<uint8_t>(i * 5 + xfer);
        CHECK(same);
        CHECK_EQ(bus.peek(0xC4B8), 0xFF); // PADDR $003FFF
        CHECK_EQ(bus.peek(0xC4B9), 0x3F);
    }
}

TEST(a_memory_to_memory_copy_to_80000_lands_in_card_memory_through_the_aperture)
{
    // OODOS's channel-1 vertical-blank copies write physical $80000 up; on revision 2 that is the aperture.
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    for (unsigned i = 0; i < 512; ++i)
        bus.write_physical(0x21000 + i, static_cast<uint8_t>(i ^ 0xA5));
    bus.write(0xC4B2, 0x40); // SLOT1 = page $40: card $100000
    const uint32_t src = 0x21000, dest = 0x83F00; // across slot 0's end into slot 1
    // SELECT 1 first: the window at $C400-$C40F then shows channel 1
    bus.poke(0xC47E, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 1 << 1);
    bus.poke(0xC400, static_cast<uint8_t>(src));
    bus.poke(0xC401, static_cast<uint8_t>(src >> 8));
    bus.poke(0xC402, static_cast<uint8_t>(src >> 16));
    bus.poke(0xC408, static_cast<uint8_t>(dest));
    bus.poke(0xC409, static_cast<uint8_t>(dest >> 8));
    bus.poke(0xC40A, static_cast<uint8_t>(dest >> 16));
    bus.poke(0xC404, 0x00); // 512
    bus.poke(0xC405, 0x02);
    bus.poke(0xC406, DmaController::BLOCK | DmaController::MEM_TO_MEM);
    bus.poke(0xC407, DmaController::ENABLE | DmaController::REQ);
    for (int i = 0; i < 100; ++i)
        bus.read(0x0000);
    bool same = true;
    for (unsigned i = 0; i < 256; ++i)
        same = same && card->card_peek(0x3F00 + i) == static_cast<uint8_t>(i ^ 0xA5) &&
               card->card_peek(0x100000 + i) == static_cast<uint8_t>((i + 256) ^ 0xA5);
    CHECK(same);
    CHECK_EQ(card->card_peek(0x4000), 0x00);    // slot 1 isn't page 1
    CHECK_EQ(bus.peek_physical(0x84000), static_cast<uint8_t>(256 ^ 0xA5));
}

// --- 16-bit transfers (step B4): the port in a word grant, the aperture in word cycles -------------------------
// The owner's Q2 and Q3 (b4-plan.md), recorded in hw/design.md beside the frozen hw/video-rev2.md.

TEST(the_port_takes_a_word_as_two_accesses_each_stepping_pstep)
{
    // A word grant is two PDATA accesses in order: the even byte at PADDR, a PSTEP step, the odd byte, another
    // step. Must fail with the word stored at PADDR and PADDR+1 then one step (the "16-bit memory" shortcut): a
    // column (PSTEP 160) or a step-down copy corrupted.
    Rig r;
    r.reg16(R2::PSTEP, 160);
    r.reg(R2::PCTL, R2::PCTL_DRQ);                           // to the card
    r.paddr(0x2000);
    r.card.dack_write_word(0xBBAA, false);
    CHECK_EQ(r.card.card_peek(0x2000), 0xAA);
    CHECK_EQ(r.card.card_peek(0x2000 + 160), 0xBB);
    CHECK_EQ(r.card.card_peek(0x2001), 0x00);
    CHECK_EQ(r.paddr(), 0x2000u + 320);
    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_DOWN);           // stepping down
    r.paddr(0x3000);
    r.card.dack_write_word(0xDDCC, true);
    CHECK_EQ(r.card.card_peek(0x3000), 0xCC);
    CHECK_EQ(r.card.card_peek(0x3000 - 160), 0xDD);
    CHECK_EQ(r.paddr(), 0x3000u - 320);

    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_FROM_CARD);      // from the card, the same way
    r.paddr(0x2000);
    CHECK_EQ(r.card.dack_read_word(false), 0xBBAAu);
    CHECK_EQ(r.paddr(), 0x2000u + 320);
    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_FROM_CARD | R2::PCTL_DOWN);
    r.paddr(0x3000);
    CHECK_EQ(r.card.dack_read_word(false), 0xDDCCu);
    CHECK_EQ(r.paddr(), 0x3000u - 320);

    r.reg16(R2::PSTEP, 1);                                   // PSTEP 1 at an even PADDR: a plain little-endian word
    r.reg(R2::PCTL, R2::PCTL_DRQ);
    r.paddr(0x4000);
    r.card.dack_write_word(0x2211, false);
    CHECK_EQ(r.card.card_peek(0x4000), 0x11);
    CHECK_EQ(r.card.card_peek(0x4001), 0x22);
    CHECK_EQ(r.paddr(), 0x4002u);

    r.reg16(R2::PSTEP, 0);                                   // PSTEP 0 holds: the odd byte is the one kept
    r.paddr(0x5000);
    r.card.dack_write_word(0x4433, false);
    CHECK_EQ(r.card.card_peek(0x5000), 0x44);
    CHECK_EQ(r.card.card_peek(0x5001), 0x00);
    CHECK_EQ(r.paddr(), 0x5000u);
    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_FROM_CARD);
    CHECK_EQ(r.card.dack_read_word(false), 0x4444u);         // and a word from it is that byte twice
    CHECK_EQ(r.paddr(), 0x5000u);
}

TEST(a_word_against_the_ports_direction_moves_nothing)
{
    // hw/video-rev2.md:167's rule for a byte, for a word: nothing drives the bus ($FFFF), a write is dropped, and
    // PADDR doesn't step. Must fail with one access done, or PADDR stepped.
    Rig r;
    r.paddr(0x1000);
    r.card.card_poke(0x1000, 0x5A);
    r.reg(R2::PCTL, R2::PCTL_DRQ);                           // to the card: a word from it is against it
    CHECK_EQ(r.card.dack_read_word(false), 0xFFFFu);
    CHECK_EQ(r.paddr(), 0x1000u);
    r.reg(R2::PCTL, R2::PCTL_DRQ | R2::PCTL_FROM_CARD);      // from the card: a word to it is against it
    r.card.dack_write_word(0x1234, false);
    CHECK_EQ(r.paddr(), 0x1000u);
    CHECK_EQ(r.card.card_peek(0x1000), 0x5A);
    CHECK_EQ(r.card.card_peek(0x1001), 0x00);
}

TEST(word_cycles_reach_card_memory_through_the_aperture)
{
    // The owner's Q3: the aperture is memory and answers both lanes of a word cycle, at $E00000 and through bank
    // 8's redirect. A WIDE memory-to-memory copy (channel 3, CARD $18) of 256 bytes is 128 words, 1 + 256 cycles.
    // Must fail with the aperture answering D0-D7 only (the odd bytes left alone).
    for (const uint32_t dest : {0xE00200u, 0x80400u})
    {
        auto m = backplane(2);
        REQUIRE(m != nullptr);
        VideoCard *card = m->video_card();
        Decoder &bus = m->bus();
        for (unsigned i = 0; i < 256; ++i)
            bus.write_physical(0x21000 + i, static_cast<uint8_t>(i * 7 + 3));
        bus.poke(0xC47E, static_cast<uint8_t>(DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 3 << 1));
        const uint32_t src = 0x21000;
        bus.poke(0xC400, static_cast<uint8_t>(src));
        bus.poke(0xC401, static_cast<uint8_t>(src >> 8));
        bus.poke(0xC402, static_cast<uint8_t>(src >> 16));
        bus.poke(0xC408, static_cast<uint8_t>(dest));
        bus.poke(0xC409, static_cast<uint8_t>(dest >> 8));
        bus.poke(0xC40A, static_cast<uint8_t>(dest >> 16));
        bus.poke(0xC404, 0x00); // 256
        bus.poke(0xC405, 0x01);
        bus.poke(0xC40C, DmaController::WIDE | DmaController::CARD_NONE);
        bus.poke(0xC406, DmaController::BLOCK | DmaController::MEM_TO_MEM);
        bus.poke(0xC407, DmaController::ENABLE | DmaController::REQ);
        bus.read(0x0000);
        CHECK_EQ(bus.wait_states(), 1u + 256);
        const uint32_t at = (dest & 0xFFFF); // slot 0 = card page 0
        bool same = true;
        for (unsigned i = 0; i < 256; ++i)
            same = same && card->card_peek(at + i) == static_cast<uint8_t>(i * 7 + 3);
        CHECK(same);
        CHECK_EQ(card->card_peek(at + 256), 0x00);
    }
}

TEST(a_wide_port_load_from_ram_takes_half_the_cycles)
{
    // The owner's Q2 at the controller: channel 1 in demand mode, 4 KB to the port with WIDE (CARD $19): the
    // takeover, the poll and 2,048 word grants, 2 + 2,048 cycles, against 2 + 4,096 in bytes. Must fail with the
    // card's word hooks left at Device's defaults (every other byte lost).
    for (const bool wide : {false, true})
    {
        auto m = backplane(2);
        REQUIRE(m != nullptr);
        VideoCard *card = m->video_card();
        Decoder &bus = m->bus();
        for (unsigned i = 0; i < 4096; ++i)
            bus.write_physical(0x20000 + i, static_cast<uint8_t>(i * 13 + 5));
        bus.write(0xC4B8, 0x00); // PADDR $000400
        bus.write(0xC4B9, 0x04);
        bus.write(0xC4BA, 0x00);
        bus.write(0xC4BE, R2::PCTL_DRQ);
        bus.poke(0xC47E, static_cast<uint8_t>(DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 1 << 1));
        bus.poke(0xC400, 0x00);
        bus.poke(0xC401, 0x00);
        bus.poke(0xC402, 0x02);
        bus.poke(0xC404, 0x00); // 4,096
        bus.poke(0xC405, 0x10);
        bus.poke(0xC40C, static_cast<uint8_t>((wide ? DmaController::WIDE : 0) | 0x9));
        bus.poke(0xC406, DmaController::DEMAND | DmaController::TO_CARD);
        bus.poke(0xC407, DmaController::ENABLE);
        bus.read(0x0000);
        const unsigned cycles = bus.wait_states();
        std::printf("  a 4 KB port load %s WIDE: %u cycles\n", wide ? "with" : "without", cycles);
        CHECK_EQ(cycles, 2u + (wide ? 2048 : 4096));
        bool same = true;
        for (unsigned i = 0; i < 4096; ++i)
            same = same && card->card_peek(0x400 + i) == static_cast<uint8_t>(i * 13 + 5);
        CHECK(same);
        CHECK_EQ(card->card_peek(0x400 + 4096), 0x00);
        CHECK_EQ(bus.peek(0xC4B9), 0x14);                    // PADDR $001400
        CHECK_EQ(bus.peek(0xC40C) & DmaController::WIDE, wide ? DmaController::WIDE : 0);
    }
}

TEST(the_bases_high_bytes_latch_at_line_480)
{
    constexpr uint64_t LINE = VideoCard::LINE_CLOCKS;
    struct Case
    {
        const char *what;
        uint8_t reg;     ///< the high byte written mid-frame
        uint8_t value;
        uint16_t before, after; ///< the 12-bit colour at (x, 300)
        unsigned x;
    };
    const Case cases[] = {
        {"PALBASE", R2::PALBASE + 1, 0x10, 0xFFF, 0xF00, 0},              // palette $000000 -> $100000
        {"MAPBASE", L0 + R2::MAPBASE + 1, 0x11, 0xFFF, 0x0F0, 0},         // image $010000 -> $110000 (blank)
        {"TILEBASE", L1 + R2::TILEBASE + 1, 0x12, 0xFFF, 0x0F0, 100},     // font $020000 -> $120000 (blank)
        {"SPRBASE", R2::SPRBASE + 1, 0x14, 0xFFF, 0x0F0, 400},            // table $040000 -> $140000 (empty)
    };
    for (const Case &c : cases)
    {
        Rig r;
        r.colour(0x000000, 0, 0x0F0);
        r.colour(0x000000, 1, 0xFFF);
        r.colour(0x100000, 1, 0xF00);
        r.colour(0x100000, 0, 0xF00);
        const bool spr = c.reg == R2::SPRBASE + 1, text = c.reg == L1 + R2::TILEBASE + 1;
        if (c.reg == R2::PALBASE + 1 || c.reg == L0 + R2::MAPBASE + 1)
        {
            for (uint32_t i = 0; i < 80 * 480; ++i)
                r.card.card_poke(0x010000 + i, 0xFF);           // image A: all lit
            r.reg(L0 + VideoCard::CONFIG, VideoCard::MONO);
            r.reg16(L0 + R2::MAPBASE, 0x0100);
            r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
        }
        else if (text)
        {
            for (unsigned row = 0; row < 16; ++row)
                r.card.card_poke(0x020000 + 1 * 16 + row, 0xFF); // font A: char 1 a block
            for (unsigned col = 0; col < 80; ++col)
            {
                r.card.card_poke(0x030000 + (18 * 80 + col) * 2, 1);   // row 18 (lines 288-303)
                r.card.card_poke(0x030000 + (18 * 80 + col) * 2 + 1, 0x01);
            }
            r.reg(L1 + VideoCard::CONFIG, VideoCard::TEXT80);
            r.reg16(L1 + R2::MAPBASE, 0x0300);
            r.reg16(L1 + R2::TILEBASE, 0x0200);
            r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER1_ON);
        }
        else if (spr)
        {
            // entry 0: image $050000 (unit $0500), 4 bits, at (200, 150), in front, 8x8; pixels 1
            const uint8_t e[8] = {0x00, 0x05, 200, 0, 150, 0, 3 << 2, 0x00};
            for (unsigned i = 0; i < 8; ++i)
                r.card.card_poke(0x040000 + i, e[i]);
            for (unsigned i = 0; i < 32; ++i)
                r.card.card_poke(0x050000 + i, 0x11);
            r.reg16(R2::SPRBASE, 0x0400);
            r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::SPRITES_ON);
        }
        r.show();
        CHECK_EQ(r.px(c.x, 300), rgba(c.before));
        r.card.tick(100 * LINE);                            // the beam at line 100
        r.reg(c.reg, c.value);
        CHECK_EQ(r.card.peek(c.reg), c.value);              // reads back at once
        r.card.tick(FRAME - 100 * LINE);                    // this frame: still the old base
        if (r.px(c.x, 300) != rgba(c.before))
            std::printf("  %s moved before line 480\n", c.what);
        CHECK_EQ(r.px(c.x, 300), rgba(c.before));
        r.card.tick(FRAME);                                 // after the vertical blank
        if (r.px(c.x, 300) != rgba(c.after))
            std::printf("  %s didn't take effect at line 480\n", c.what);
        CHECK_EQ(r.px(c.x, 300), rgba(c.after));
    }
}

// --- bitmap 640 and direct 640, layer types 6 and 7 (hw/video640.md) ---------------------------

namespace
{
    constexpr unsigned W = VideoCard::WIDTH, H = VideoCard::HEIGHT;
    constexpr uint32_t B640 = VideoCard::BITMAP640_ROW, D640 = VideoCard::DIRECT640_ROW;

    /// 256 different colours, one an entry: the high 8 bits are the index.
    uint16_t entry_colour(unsigned i) { return static_cast<uint16_t>(i << 4 | ((i * 7) & 0xF)); }
    void palette(Rig &r, uint32_t pal)
    {
        for (unsigned i = 0; i < 256; ++i)
            r.colour(pal, i, entry_colour(i));
    }
    void word(Rig &r, uint32_t at, uint16_t w)
    {
        r.card.card_poke(at, static_cast<uint8_t>(w));
        r.card.card_poke(at + 1, static_cast<uint8_t>(w >> 8));
    }
    /// The frame against `want(x, y)`: the pixels that differ, the first of them printed.
    template <typename F> unsigned frame_differs(const Rig &r, F want)
    {
        unsigned bad = 0;
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x)
                if (r.px(x, y) != want(x, y) && !bad++)
                    std::printf("  (%u, %u): %08x, wanted %08x\n", x, y, r.px(x, y), want(x, y));
        return bad;
    }
    /// A layer of `type` at card address `at` (on a 256-byte unit) on layer `l`.
    void layer(Rig &r, uint8_t l, uint8_t type, uint32_t at)
    {
        r.reg(l + VideoCard::CONFIG, type);
        r.reg16(l + R2::MAPBASE, static_cast<uint16_t>(at >> 8));
    }
} // namespace

TEST(type_6_draws_640_by_480_at_a_byte_a_pixel)
{
    // A pattern that differs along every row and from row to row, at the 1,024-byte stride: the whole
    // frame through the palette, x 0-639 covered (BORDER nowhere), PALOFS changing nothing.
    Rig r;
    const uint32_t img = 0x040000, pal = 0x1A0000;
    palette(r, pal);
    auto pix = [](unsigned x, unsigned y) { return static_cast<uint8_t>(x + 3 * y + (x >> 8) * 29); };
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < B640; ++x)
            r.card.card_poke(img + y * B640 + x, pix(x, y));
    layer(r, L0, VideoCard::BITMAP640, img);
    r.reg(L0 + R2::PALOFS, 3);
    r.reg16(R2::PALBASE, static_cast<uint16_t>(pal >> 8));
    r.reg(VideoCard::BORDER, 0x11);
    r.colour(pal, 0x11, 0xABC); // BORDER's own colour, nowhere in the picture
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(r.card.peek(L0 + VideoCard::CONFIG), VideoCard::BITMAP640);
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) {
                 const uint8_t v = pix(x, y);
                 return rgba(v == 0x11 ? 0xABC : entry_colour(v));
             }),
             0u);
}

TEST(type_6_scrolls_and_wraps_at_1024_and_480)
{
    Rig r;
    const uint32_t img = 0x0C0400; // on a 1 KB boundary, not 16 KB
    palette(r, 0);
    auto pix = [](unsigned x, unsigned y) { return static_cast<uint8_t>(5 * x + 3 * y + (x >> 8) * 17); };
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < B640; ++x)
            r.card.card_poke(img + y * B640 + x, pix(x, y));
    layer(r, L1, VideoCard::BITMAP640, img);
    r.reg16(L1 + R2::HSCROLL, 900);
    r.reg16(L1 + R2::VSCROLL, 470);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER1_ON);
    r.show();
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) {
                 return rgba(entry_colour(pix((x + 900) % 1024, (y + 470) % 480)));
             }),
             0u);
}

TEST(type_7_draws_every_12_bit_colour_directly)
{
    // All 4,096 colours, bits 12-14 set on every odd pixel, the palette scrambled; then a flip between
    // two 600 KB buffers, written mid-frame, at line 480.
    Rig r;
    const uint32_t a = 0x040000, b = a + D640 * H; // $040000 and $0D6000
    uint32_t seed = 0x2468ACE1;
    for (unsigned i = 0; i < 256; ++i)
    {
        seed = seed * 1103515245 + 12345;
        r.colour(0, i, static_cast<uint16_t>(seed >> 16));
    }
    auto colour = [](unsigned x, unsigned y, unsigned off) { return static_cast<uint16_t>((y * W + x + off) % 4096); };
    auto spare = [](unsigned x) { return static_cast<uint16_t>(x & 1 ? ((x >> 1) % 7 + 1) << 12 : 0); };
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
        {
            word(r, a + y * D640 + 2 * x, colour(x, y, 0) | spare(x));
            word(r, b + y * D640 + 2 * x, colour(x, y, 1234));
        }
    layer(r, L0, VideoCard::DIRECT640, a);
    r.reg(VideoCard::BORDER, 3);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return rgba(colour(x, y, 0)); }), 0u);
    r.card.tick(100 * VideoCard::LINE_CLOCKS); // the beam at line 100
    r.reg16(L0 + R2::MAPBASE, static_cast<uint16_t>(b >> 8));
    r.card.tick(FRAME - 100 * VideoCard::LINE_CLOCKS); // this frame: still A
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return rgba(colour(x, y, 0)); }), 0u);
    r.card.tick(FRAME); // after the vertical blank: B, whole
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return rgba(colour(x, y, 1234)); }), 0u);
}

TEST(type_7_wraps_at_640_and_480)
{
    Rig r;
    const uint32_t img = 0x100000;
    auto colour = [](unsigned x, unsigned y) { return static_cast<uint16_t>((x * 5 + y * 77) & 0xFFF); };
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            word(r, img + y * D640 + 2 * x, colour(x, y));
    layer(r, L0, VideoCard::DIRECT640, img);
    r.reg16(L0 + R2::HSCROLL, 1000); // 360 across
    r.reg16(L0 + R2::VSCROLL, 479);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON);
    r.show();
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return rgba(colour((x + 1000) % 640, (y + 479) % 480)); }),
             0u);
}

TEST(type_7_s_transparent_bit_shows_the_layer_below)
{
    // Layer 1 direct 640 in four bands across: $8000, $FFFF (both transparent), $0000 (opaque black) and
    // $0F00 (red); under it type 6 (all entry 5), then 80-column text (every pixel lit, entry 6), then
    // nothing (palette entry 0, never BORDER).
    Rig r;
    const uint32_t top = 0x100000, b6 = 0x040000, font = 0x0C0000, text = 0x0C1000;
    r.colour(0, 0, 0x123);
    r.colour(0, 5, 0x0F0);
    r.colour(0, 6, 0x00F);
    r.colour(0, 9, 0x456);
    r.reg(VideoCard::BORDER, 9);
    const uint16_t bands[4] = {0x8000, 0xFFFF, 0x0000, 0x0F00};
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            word(r, top + y * D640 + 2 * x, bands[x / 160]);
    for (uint32_t i = 0; i < B640 * H; ++i)
        r.card.card_poke(b6 + i, 5);
    for (unsigned row = 0; row < 16; ++row)
        r.card.card_poke(font + 'X' * 16 + row, 0xFF);
    for (unsigned c = 0; c < 80 * 30; ++c)
    {
        r.card.card_poke(text + 2 * c, 'X');
        r.card.card_poke(text + 2 * c + 1, 0x06);
    }
    layer(r, L1, VideoCard::DIRECT640, top);
    struct Below
    {
        const char *what;
        uint16_t colour; ///< what the transparent bands show
        bool on;
    };
    for (const Below &below : {Below{"type 6", 0x0F0, true}, Below{"text", 0x00F, true}, Below{"nothing", 0x123, false}})
    {
        if (below.colour == 0x0F0)
            layer(r, L0, VideoCard::BITMAP640, b6);
        else
        {
            layer(r, L0, VideoCard::TEXT80, text);
            r.reg16(L0 + R2::TILEBASE, static_cast<uint16_t>(font >> 8));
        }
        r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER1_ON | (below.on ? VideoCard::LAYER0_ON : 0));
        r.show();
        const unsigned bad = frame_differs(r, [&](unsigned x, unsigned) {
            const uint16_t want[4] = {below.colour, below.colour, 0x000, 0xF00};
            return rgba(want[x / 160]);
        });
        if (bad)
            std::printf("  over %s\n", below.what);
        CHECK_EQ(bad, 0u);
    }
}

TEST(a_layer_over_type_7)
{
    // Type 6 on layer 1 over type 7: its 0s show type 7's colours exactly, its other pixels the palette.
    Rig r;
    const uint32_t d = 0x040000, b6 = 0x100000;
    for (unsigned i = 0; i < 256; ++i)
        r.colour(0, i, static_cast<uint16_t>(0xFFF - i)); // nothing like the direct colours
    auto colour = [](unsigned x, unsigned y) { return static_cast<uint16_t>((y * W + x) % 4096); };
    for (unsigned y = 0; y < H; ++y)
    {
        for (unsigned x = 0; x < W; ++x)
            word(r, d + y * D640 + 2 * x, colour(x, y));
        for (unsigned x = 0; x < B640; ++x)
            r.card.card_poke(b6 + y * B640 + x, x < 64 ? 7 : 0);
    }
    layer(r, L0, VideoCard::DIRECT640, d);
    layer(r, L1, VideoCard::BITMAP640, b6);
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::LAYER1_ON);
    r.show();
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return rgba(x < 64 ? 0xFFF - 7 : colour(x, y)); }), 0u);
}

TEST(sprites_over_and_under_type_7)
{
    // Layer 0 direct 640: x 0-319 opaque red, 320-639 transparent ($8000). Sprite 0 at depth 1 across the
    // edge shows only on the transparent side; sprites at depths 2 and 3 show over both; a sprite's colour
    // is a palette entry (8 bits; 4 bits with its palette offset). Collisions are the sprites' alone.
    Rig r;
    const uint32_t d = 0x040000, table = 0x1A0000, img8 = 0x1A1000, img4 = 0x1A1100;
    r.colour(0, 0, 0x123);
    r.colour(0, 0x21, 0x0F0);
    r.colour(0, 0x43, 0x00F);
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            word(r, d + y * D640 + 2 * x, x < 320 ? 0x0F00 : 0x8000);
    for (unsigned i = 0; i < 256; ++i)
        r.card.card_poke(img8 + i, 0x21); // 16x16 at 8 bits
    for (unsigned i = 0; i < 128; ++i)
        r.card.card_poke(img4 + i, 0x33); // 16x16 at 4 bits
    // entry: image unit (2 bytes, bit 15 8 bits), X, Y (320x240), flags (depth << 2, group << 4), size
    auto sprite = [&](unsigned n, uint32_t image, bool bpp8, unsigned x, unsigned y, unsigned depth, unsigned group,
                      unsigned palofs) {
        const uint32_t e = table + n * 8;
        const unsigned unit = image >> 8;
        const uint8_t bytes[8] = {static_cast<uint8_t>(unit), static_cast<uint8_t>((unit >> 8) | (bpp8 ? 0x80 : 0)),
                                  static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(y),
                                  static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(depth << 2 | group << 4),
                                  static_cast<uint8_t>(0x50 | palofs)}; // 16x16
        for (unsigned i = 0; i < 8; ++i)
            r.card.card_poke(e + i, bytes[i]);
    };
    sprite(0, img8, true, 150, 10, 1, 4, 0);  // screen x 300-331, y 20-51: depth 1
    sprite(1, img8, true, 150, 40, 2, 1, 0);  // y 80-111: depth 2
    sprite(2, img4, false, 150, 70, 3, 2, 4); // y 140-171: depth 3, colour $43
    sprite(3, img8, true, 152, 42, 2, 1, 0);  // behind sprite 1, sharing its group: a collision
    sprite(4, img8, true, 145, 12, 1, 4, 0);  // behind sprite 0, under the red: a collision all the same
    layer(r, L0, VideoCard::DIRECT640, d);
    r.reg16(R2::SPRBASE, static_cast<uint16_t>(table >> 8));
    r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::SPRITES_ON);
    r.show();
    const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
        // a 16x16 sprite at (sx, sy) of the 320x240 space covers 32x32 screen pixels
        auto at = [&](unsigned sx, unsigned sy) { return x >= 2 * sx && x < 2 * sx + 32 && y >= 2 * sy && y < 2 * sy + 32; };
        const bool s0 = at(150, 10) || at(145, 12), s1 = at(150, 40) || at(152, 42), s2 = at(150, 70);
        if (s1)
            return rgba(0x0F0);
        if (s2)
            return rgba(0x00F);
        if (x < 320)
            return rgba(0xF00); // depth 1 hidden by the opaque direct pixels
        return rgba(s0 ? 0x0F0 : 0x123);
    });
    CHECK_EQ(bad, 0u);
    CHECK_EQ(r.card.peek(VideoCard::STATUS) & 0x0F, 0x05); // groups 1 and 4
}

// --- full-resolution sprites (hw/sprites640.md) -------------------------------------------------

namespace
{
    /// Entry n of the sprite table at `table`: the image at card address `image` (on a 256-byte unit), X and Y
    /// (10 bits; `full` sets byte 3 bit 7), byte 6 (groups, depth, flips) and byte 7 (sizes, palette offset).
    void entry(Rig &r, uint32_t table, unsigned n, uint32_t image, bool bpp8, unsigned x, unsigned y, bool full,
               uint8_t b6, uint8_t b7)
    {
        const unsigned unit = image >> 8;
        const uint8_t e[8] = {static_cast<uint8_t>(unit),
                              static_cast<uint8_t>(((unit >> 8) & 0x7F) | (bpp8 ? 0x80 : 0)),
                              static_cast<uint8_t>(x),
                              static_cast<uint8_t>(((x >> 8) & 3) | (full ? R2::SPRX_FULL : 0)),
                              static_cast<uint8_t>(y),
                              static_cast<uint8_t>((y >> 8) & 3),
                              b6,
                              b7};
        for (unsigned i = 0; i < 8; ++i)
            r.card.card_poke(table + n * 8 + i, e[i]);
    }
    /// Byte 6: the collision groups, the depth (0-3) and the flips (bit 0 H, bit 1 V).
    constexpr uint8_t attrs(unsigned depth, unsigned groups = 0, unsigned flips = 0)
    {
        return static_cast<uint8_t>(groups << 4 | depth << 2 | flips);
    }
    /// Byte 7: the width and height codes (0-3: 8, 16, 32, 64) and the palette offset.
    constexpr uint8_t dims(unsigned wcode, unsigned hcode, unsigned palofs = 0)
    {
        return static_cast<uint8_t>(hcode << 6 | wcode << 4 | palofs);
    }
    /// A sprite image's pixel (tx, ty) as the card colours it: a palette index, 0 transparent.
    unsigned image_px(const Rig &r, uint32_t image, bool bpp8, unsigned w, unsigned tx, unsigned ty, unsigned palofs)
    {
        if (bpp8)
            return r.card.card_peek(image + ty * w + tx);
        const uint8_t b = r.card.card_peek(image + ty * (w / 2) + tx / 2);
        const unsigned v = tx & 1 ? b & 0x0F : b >> 4;
        return v ? (palofs << 4 | v) : 0;
    }
    /// A w x h image that differs along every row and down every column, some of its pixels transparent.
    void pattern(Rig &r, uint32_t image, bool bpp8, unsigned w, unsigned h, unsigned seed)
    {
        for (unsigned ty = 0; ty < h; ++ty)
            for (unsigned tx = 0; tx < w; ++tx)
            {
                unsigned v = (tx * 7 + ty * 13 + seed) & (bpp8 ? 0xFF : 0x0F);
                if ((tx + 2 * ty + seed) % 5 == 0)
                    v = 0;
                if (bpp8)
                    r.card.card_poke(image + ty * w + tx, static_cast<uint8_t>(v));
                else
                {
                    const uint32_t at = image + ty * (w / 2) + tx / 2;
                    const uint8_t b = r.card.card_peek(at);
                    r.card.card_poke(at, static_cast<uint8_t>(tx & 1 ? (b & 0xF0) | v : (b & 0x0F) | v << 4));
                }
            }
    }
    /// The sprite pixel (tx, ty) that a sprite at (sx, sy), w x h, puts at screen pixel (x, y), its positions in
    /// screen pixels / `scale` (1 full resolution, 2 doubled) and wrapping at 1,024. False where it puts none.
    bool covers(unsigned x, unsigned y, unsigned sx, unsigned sy, unsigned w, unsigned h, unsigned scale, unsigned &tx,
                unsigned &ty)
    {
        tx = (x / scale - sx) & 0x3FF;
        ty = (y / scale - sy) & 0x3FF;
        return tx < w && ty < h;
    }
    std::vector<uint32_t> grab(const Rig &r) { return std::vector<uint32_t>(r.card.frame(), r.card.frame() + W * H); }
    /// The table at `table`, sprites on and full resolution on or off, the palette at 0.
    void sprites_on(Rig &r, uint32_t table, bool full, uint8_t layers = 0)
    {
        r.reg16(R2::SPRBASE, static_cast<uint16_t>(table >> 8));
        r.reg(R2::SPRCTL, full ? R2::SPR_FULL : 0);
        r.reg(VideoCard::CTRL, static_cast<uint8_t>(VideoCard::DISPLAY | VideoCard::SPRITES_ON | layers));
    }

    /// Today's rules for doubled sprites, written out again from the definition (chapter 6, "Sprites"): each
    /// line of the 320x240 space, the 32 lowest-numbered sprites that cover it, a lower number in front,
    /// collisions where opaque pixels share a spot and a group bit; over two mono layers (PALOFS 1 and 2).
    struct Reference
    {
        std::vector<uint32_t> frame;
        uint8_t coll{0};
    };
    Reference doubled_reference(const Rig &r, uint32_t table, uint32_t mono0, uint32_t mono1)
    {
        auto pal = [&](unsigned i) {
            return rgba(static_cast<uint16_t>(r.card.card_peek(2 * i) | (r.card.card_peek(2 * i + 1) & 0x0F) << 8));
        };
        auto mono = [&](uint32_t at, unsigned x, unsigned y) { return (r.card.card_peek(at + y * 80 + x / 8) >> (7 - x % 8)) & 1; };
        Reference out{std::vector<uint32_t>(W * H), 0};
        for (unsigned ly = 0; ly < H / 2; ++ly)
        {
            uint8_t colour[320] = {}, depth[320] = {}, mask[320] = {};
            unsigned drawn = 0;
            for (unsigned n = 0; n < 64 && drawn < 32; ++n)
            {
                const uint32_t e = table + n * 8;
                const uint8_t b6 = r.card.card_peek(e + 6), b7 = r.card.card_peek(e + 7);
                const unsigned d = (b6 >> 2) & 3;
                if (!d)
                    continue;
                const unsigned w = 8u << ((b7 >> 4) & 3), h = 8u << (b7 >> 6);
                const unsigned sx = r.card.card_peek(e + 2) | (r.card.card_peek(e + 3) & 3u) << 8;
                const unsigned sy = r.card.card_peek(e + 4) | (r.card.card_peek(e + 5) & 3u) << 8;
                const unsigned row = (ly - sy) & 0x3FF;
                if (row >= h)
                    continue;
                ++drawn;
                const uint32_t image = (r.card.card_peek(e) | (r.card.card_peek(e + 1) & 0x7Fu) << 8) * 256u;
                const bool bpp8 = r.card.card_peek(e + 1) & 0x80;
                const unsigned ty = b6 & 2 ? h - 1 - row : row;
                for (unsigned i = 0; i < w; ++i)
                {
                    const unsigned x = (sx + i) & 0x3FF;
                    if (x >= 320)
                        continue;
                    const unsigned v = image_px(r, image, bpp8, w, b6 & 1 ? w - 1 - i : i, ty, b7 & 0x0F);
                    if (!v)
                        continue;
                    out.coll |= mask[x] & (b6 >> 4);
                    mask[x] |= b6 >> 4;
                    if (depth[x])
                        continue;
                    colour[x] = static_cast<uint8_t>(v);
                    depth[x] = static_cast<uint8_t>(d);
                }
            }
            for (unsigned y = 2 * ly; y < 2 * ly + 2; ++y)
                for (unsigned x = 0; x < W; ++x)
                {
                    const unsigned s = x / 2;
                    unsigned c = 0; // both layers cover: entry 0 where all is transparent
                    if (depth[s] == 1)
                        c = colour[s];
                    if (mono(mono0, x, y))
                        c = 0x11;
                    if (depth[s] == 2)
                        c = colour[s];
                    if (mono(mono1, x, y))
                        c = 0x21;
                    if (depth[s] == 3)
                        c = colour[s];
                    out.frame[y * W + x] = pal(c);
                }
        }
        return out;
    }
} // namespace

TEST(a_full_sprite_is_drawn_1_to_1_in_640_units)
{
    // Over a mono layer and over a bitmap 640 layer: 8x8, 16x16 and 64x64 sprites at 4 and 8 bits, full
    // resolution at the odd place (101, 77) of 640x480, in front. Each screen pixel of the box is one image
    // pixel (the layer where it is transparent); the layer is exact everywhere else.
    const uint32_t lay = 0x040000, table = 0x1A0000, image = 0x1A1000;
    for (uint8_t type : {VideoCard::MONO, VideoCard::BITMAP640})
        for (unsigned code : {0u, 1u, 3u})
            for (bool bpp8 : {false, true})
            {
                Rig r;
                palette(r, 0);
                const unsigned n = 8u << code;
                auto layer_px = [&](unsigned x, unsigned y) -> unsigned {
                    if (type == VideoCard::MONO)
                        return (r.card.card_peek(lay + y * 80 + x / 8) >> (7 - x % 8)) & 1;
                    return r.card.card_peek(lay + y * B640 + x);
                };
                for (unsigned y = 0; y < H; ++y)
                    for (unsigned x = 0; x < (type == VideoCard::MONO ? 80u : B640); ++x)
                        r.card.card_poke(lay + y * (type == VideoCard::MONO ? 80u : B640) + x,
                                         static_cast<uint8_t>(x * 37 + y * 11 + (x >> 8) * 29));
                pattern(r, image, bpp8, n, n, code * 3 + bpp8);
                entry(r, table, 0, image, bpp8, 101, 77, true, attrs(3), dims(code, code, 6));
                layer(r, L0, type, lay);
                sprites_on(r, table, true, VideoCard::LAYER0_ON);
                r.show();
                const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
                    unsigned tx, ty;
                    if (covers(x, y, 101, 77, n, n, 1, tx, ty))
                        if (const unsigned v = image_px(r, image, bpp8, n, tx, ty, 6))
                            return rgba(entry_colour(v));
                    return rgba(entry_colour(layer_px(x, y)));
                });
                if (bad)
                    std::printf("  over %s: %ux%u at %u bits\n", type == VideoCard::MONO ? "mono" : "bitmap 640", n, n,
                                bpp8 ? 8 : 4);
                CHECK_EQ(bad, 0u);
            }
}

TEST(the_full_bit_needs_sprctl)
{
    // SPRCTL: bit 0 alone, 0 at reset and after /RESET. Byte 3 bit 7 means nothing while it is 0, and with it
    // set byte 3's bits 6-2 and byte 5's bits 7-2 still mean nothing: an old table shows as it did.
    Rig r;
    CHECK_EQ(r.card.peek(R2::SPRCTL), 0x00);
    r.reg(R2::SPRCTL, 0xFF);
    CHECK_EQ(r.card.peek(R2::SPRCTL), 0x01);
    r.reg(R2::SPRCTL, 0xFE);
    CHECK_EQ(r.card.peek(R2::SPRCTL), 0x00);
    r.reg(R2::SPRCTL, 0x01);
    r.card.bus_reset();
    CHECK_EQ(r.card.peek(R2::SPRCTL), 0x00);

    const uint32_t table = 0x1A0000, image = 0x1A1000;
    palette(r, 0);
    pattern(r, image, true, 16, 16, 1);
    auto table_with = [&](uint8_t b3, uint8_t b5) { // ORed into each entry's bytes 3 and 5
        entry(r, table, 0, image, true, 101, 77, false, attrs(3), dims(1, 1));
        entry(r, table, 1, image, true, 276, 30, false, attrs(3), dims(1, 1)); // X high 1
        for (unsigned n : {0u, 1u})
        {
            r.card.card_poke(table + n * 8 + 3, static_cast<uint8_t>(r.card.card_peek(table + n * 8 + 3) | b3));
            r.card.card_poke(table + n * 8 + 5, static_cast<uint8_t>(r.card.card_peek(table + n * 8 + 5) | b5));
        }
    };
    sprites_on(r, table, false);
    table_with(0, 0);
    r.show();
    const auto plain = grab(r);
    table_with(0x80, 0);
    r.show();
    CHECK(grab(r) == plain); // SPRCTL 0: bit 7 ignored
    table_with(0xFC, 0xFC);
    r.show();
    CHECK(grab(r) == plain); // and every other spare bit
    r.reg(R2::SPRCTL, R2::SPR_FULL);
    table_with(0x7C, 0xFC);
    r.show();
    CHECK(grab(r) == plain); // SPRCTL 1, bit 7 clear: doubled, the junk ignored
    table_with(0x80, 0);
    r.show();
    const auto full = grab(r);
    CHECK(full != plain); // bit 7 counts now
    table_with(0xFC, 0xFC);
    r.show();
    CHECK(grab(r) == full);
}

TEST(doubled_sprites_are_unchanged_with_sprctl_on_or_off)
{
    // 20 seeded random tables of 64 doubled sprites (every size, depth, flip and group, both colour depths,
    // junk in byte 3's bits 6-2 and byte 5's bits 7-2; every fourth table 64 sprites 64 high crowding the top
    // lines, past 32 a line) over two mono layers. With SPRCTL 0 (byte 3 bit 7 random too) and with SPRCTL 1
    // (bit 7 clear) each frame and COLL equal a reference that applies today's rules on its own.
    Rig r;
    uint32_t seed = 2026;
    auto rnd = [&] {
        seed = seed * 1103515245 + 12345;
        return (seed >> 16) & 0xFF;
    };
    for (uint32_t a = 0; a < VideoCard::MEMORY_SIZE; ++a)
        r.card.card_poke(a, static_cast<uint8_t>(rnd() < 100 ? 0 : rnd()));
    const uint32_t mono0 = 0x010000, mono1 = 0x020000, table = 0x1C0000;
    auto setup = [&](bool full) {
        r.card.bus_reset(); // COLL 0
        layer(r, L0, VideoCard::MONO, mono0);
        r.reg(L0 + R2::PALOFS, 1);
        layer(r, L1, VideoCard::MONO, mono1);
        r.reg(L1 + R2::PALOFS, 2);
        sprites_on(r, table, full, VideoCard::LAYER0_ON | VideoCard::LAYER1_ON);
        r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::LAYER1_ON); // latch first
        r.show();
        r.reg(VideoCard::CTRL, VideoCard::DISPLAY | VideoCard::LAYER0_ON | VideoCard::LAYER1_ON | VideoCard::SPRITES_ON);
        r.show();
    };
    unsigned crowded = 0;
    for (unsigned trial = 0; trial < 20; ++trial)
    {
        const bool crowd = trial % 4 == 0;
        for (unsigned n = 0; n < 64; ++n)
        {
            const unsigned x = (1000 + (rnd() | rnd() << 8) % 344) & 0x3FF; // -24 to 319
            const unsigned y = crowd ? rnd() % 40 : (1000 + (rnd() | rnd() << 8) % 264) & 0x3FF;
            const uint8_t e[8] = {static_cast<uint8_t>(rnd()), static_cast<uint8_t>((rnd() & 0x80) | (rnd() & 0x1F)),
                                  static_cast<uint8_t>(x), static_cast<uint8_t>((x >> 8) | (rnd() & 0x7C)),
                                  static_cast<uint8_t>(y), static_cast<uint8_t>((y >> 8) | (rnd() & 0xFC)),
                                  static_cast<uint8_t>(rnd()),
                                  static_cast<uint8_t>(crowd ? 0xC0 | (rnd() & 0x3F) : rnd())};
            for (unsigned i = 0; i < 8; ++i)
                r.card.card_poke(table + n * 8 + i, e[i]);
        }
        const Reference want = doubled_reference(r, table, mono0, mono1);
        crowded += crowd;
        for (bool full : {false, true})
        {
            for (unsigned n = 0; n < 64; ++n)
            {
                const uint32_t b3 = table + n * 8 + 3;
                r.card.card_poke(b3, static_cast<uint8_t>((r.card.card_peek(b3) & 0x7F) | (full ? 0 : rnd() & 0x80)));
            }
            setup(full);
            const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) { return want.frame[y * W + x]; });
            const uint8_t coll = r.card.peek(VideoCard::STATUS) & 0x0F;
            if (bad || coll != want.coll)
                std::printf("  table %u, SPRCTL %u: %u pixels, COLL $%X (wanted $%X)\n", trial, full, bad, coll, want.coll);
            CHECK_EQ(bad, 0u);
            CHECK_EQ(coll, want.coll);
        }
    }
    CHECK_EQ(crowded, 5u);
}

TEST(a_full_and_a_doubled_sprite_collide_in_screen_pixels)
{
    // Sprite 0 groups %0011, sprite 1 %0110: a collision is COLL $2. A doubled 8x8 at X 10 covers screen 20-35;
    // a full one at screen 36 misses it, at 35 hits it. Full ones at X 1 (1-8) and 9 (9-16) miss; at 8 they hit.
    // Then the same down Y.
    const uint32_t table = 0x1A0000, image = 0x1A1000;
    struct Case
    {
        bool full0;
        unsigned x0, y0;
        bool full1;
        unsigned x1, y1;
        uint8_t coll;
    };
    const Case cases[] = {
        {false, 10, 10, true, 36, 20, 0}, {false, 10, 10, true, 35, 20, 2}, {true, 1, 20, true, 9, 20, 0},
        {true, 1, 20, true, 8, 20, 2},    {false, 10, 10, true, 20, 36, 0}, {false, 10, 10, true, 20, 35, 2},
        {true, 20, 1, true, 20, 9, 0},    {true, 20, 1, true, 20, 8, 2},
    };
    for (const Case &c : cases)
    {
        Rig r;
        for (unsigned i = 0; i < 64; ++i)
            r.card.card_poke(image + i, 0x21); // 8x8, every pixel opaque
        entry(r, table, 0, image, true, c.x0, c.y0, c.full0, attrs(3, 0x3), dims(0, 0));
        entry(r, table, 1, image, true, c.x1, c.y1, c.full1, attrs(3, 0x6), dims(0, 0));
        sprites_on(r, table, true);
        r.show();
        const uint8_t coll = r.card.peek(VideoCard::STATUS) & 0x0F;
        if (coll != c.coll)
            std::printf("  %s (%u, %u) and %s (%u, %u): COLL $%X\n", c.full0 ? "full" : "doubled", c.x0, c.y0,
                        c.full1 ? "full" : "doubled", c.x1, c.y1, coll);
        CHECK_EQ(coll, c.coll);
        CHECK_EQ((r.card.peek(VideoCard::ISR) & VideoCard::COLLISION) != 0, c.coll != 0);
    }
}

TEST(full_sprites_wrap_at_1024_and_show_0_to_639)
{
    // A 16x16 full sprite: at (1016, 1016) its last 8x8 at the top left; at X 632 its left 8 columns at the
    // right edge and nothing at the left; at X 640 or Y 480 nothing; at Y 472 its top 8 rows.
    const uint32_t table = 0x1A0000, image = 0x1A1000;
    struct Place
    {
        unsigned x, y, shown;
    };
    for (const Place p : {Place{1016, 1016, 64}, Place{632, 100, 128}, Place{640, 100, 0}, Place{100, 480, 0},
                          Place{100, 472, 128}})
    {
        Rig r;
        palette(r, 0);
        pattern(r, image, true, 16, 16, 9);
        entry(r, table, 0, image, true, p.x, p.y, true, attrs(3), dims(1, 1));
        sprites_on(r, table, true);
        r.show();
        unsigned shown = 0; // the box's screen pixels, opaque or not
        const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
            unsigned tx, ty;
            if (covers(x, y, p.x, p.y, 16, 16, 1, tx, ty))
            {
                ++shown;
                if (const unsigned v = image_px(r, image, true, 16, tx, ty, 0))
                    return rgba(entry_colour(v));
            }
            return rgba(entry_colour(0)); // BORDER 0: no layer is on
        });
        if (bad)
            std::printf("  at (%u, %u)\n", p.x, p.y);
        CHECK_EQ(bad, 0u);
        CHECK_EQ(shown, p.shown);
    }
}

TEST(full_sprites_flip)
{
    // A 16x8 image that is the same no way round, at 4 and 8 bits, flipped H, V and both, full resolution.
    const uint32_t table = 0x1A0000, image = 0x1A1000;
    for (bool bpp8 : {false, true})
        for (unsigned flips : {1u, 2u, 3u})
        {
            Rig r;
            palette(r, 0);
            pattern(r, image, bpp8, 16, 8, 4);
            entry(r, table, 0, image, bpp8, 201, 99, true, attrs(3, 0, flips), dims(1, 0, 3));
            sprites_on(r, table, true);
            r.show();
            const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
                unsigned tx, ty;
                if (covers(x, y, 201, 99, 16, 8, 1, tx, ty))
                    if (const unsigned v = image_px(r, image, bpp8, 16, flips & 1 ? 15 - tx : tx, flips & 2 ? 7 - ty : ty, 3))
                        return rgba(entry_colour(v));
                return rgba(entry_colour(0));
            });
            if (bad)
                std::printf("  %u bits, flips %u\n", bpp8 ? 8 : 4, flips);
            CHECK_EQ(bad, 0u);
        }
}

TEST(full_sprites_order_depth_and_the_32_a_line)
{
    const uint32_t table = 0x1A0000, image = 0x1A1000;
    {
        // Full sprites at depths 1, 2 and 3 over text 80 (layer 1) over bitmap 640 (layer 0): depth 1 shows where
        // both layers are transparent, 2 where layer 1 is, 3 everywhere; each screen pixel on its own.
        Rig r;
        palette(r, 0);
        const uint32_t bmp = 0x040000, font = 0x0C0000, cells = 0x0C1000;
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x)
                r.card.card_poke(bmp + y * B640 + x, static_cast<uint8_t>((x + y) % 3 ? 0 : x * 5 + y));
        for (unsigned row = 0; row < 16; ++row)
            r.card.card_poke(font + 16 + row, row % 4 < 2 ? 0xA5 : 0x5A); // character 1
        for (unsigned c = 0; c < 80 * 30; ++c)
        {
            r.card.card_poke(cells + 2 * c, 1);
            r.card.card_poke(cells + 2 * c + 1, 0x0F); // foreground 15, background 0: transparent
        }
        layer(r, L0, VideoCard::BITMAP640, bmp);
        layer(r, L1, VideoCard::TEXT80, cells);
        r.reg16(L1 + R2::TILEBASE, static_cast<uint16_t>(font >> 8));
        pattern(r, image, true, 32, 32, 2);
        const unsigned xs[3] = {101, 301, 501};
        for (unsigned d = 1; d <= 3; ++d)
            entry(r, table, d - 1, image, true, xs[d - 1], 100, true, attrs(d), dims(2, 2));
        sprites_on(r, table, true, VideoCard::LAYER0_ON | VideoCard::LAYER1_ON);
        r.show();
        const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
            const unsigned l0 = r.card.card_peek(bmp + y * B640 + x);
            const unsigned l1 = (r.card.card_peek(font + 16 + y % 16) >> (7 - x % 8)) & 1 ? 15 : 0;
            unsigned s = 0, d = 0;
            for (unsigned k = 0; k < 3 && !d; ++k)
            {
                unsigned tx, ty;
                if (covers(x, y, xs[k], 100, 32, 32, 1, tx, ty) && (s = image_px(r, image, true, 32, tx, ty, 0)))
                    d = k + 1;
            }
            unsigned c = 0;
            if (d == 1)
                c = s;
            if (l0)
                c = l0;
            if (d == 2)
                c = s;
            if (l1)
                c = l1;
            if (d == 3)
                c = s;
            return rgba(entry_colour(c));
        });
        CHECK_EQ(bad, 0u);
    }
    for (bool full_first : {true, false})
    {
        // A full sprite 0 in front of a doubled sprite 1, and the other way round: a lower number is in front,
        // whatever the sizes. The full one is 16x16 at (400, 300); the doubled at (196, 146), screen 392-423.
        Rig r;
        palette(r, 0);
        for (unsigned i = 0; i < 256; ++i)
        {
            r.card.card_poke(image + i, 0x31);
            r.card.card_poke(image + 0x100 + i, 0x32);
        }
        entry(r, table, full_first ? 0 : 1, image, true, 400, 300, true, attrs(2), dims(1, 1));
        entry(r, table, full_first ? 1 : 0, image + 0x100, true, 196, 146, false, attrs(2), dims(1, 1));
        sprites_on(r, table, true);
        r.show();
        const unsigned bad = frame_differs(r, [&](unsigned x, unsigned y) {
            unsigned tx, ty;
            const bool f = covers(x, y, 400, 300, 16, 16, 1, tx, ty), d = covers(x, y, 196, 146, 16, 16, 2, tx, ty);
            if (f && (full_first || !d))
                return rgba(entry_colour(0x31));
            if (d)
                return rgba(entry_colour(0x32));
            return rgba(entry_colour(0));
        });
        if (bad)
            std::printf("  the %s sprite numbered 0\n", full_first ? "full" : "doubled");
        CHECK_EQ(bad, 0u);
    }
    {
        // 34 sprites on lines 100-107, full and doubled in turn, each at its own place across: the first 32 are
        // drawn, the 33rd (full) and 34th (doubled) aren't.
        Rig r;
        palette(r, 0);
        for (unsigned i = 0; i < 64; ++i)
            r.card.card_poke(image + i, 0x44);
        for (unsigned n = 0; n < 34; ++n)
        {
            const bool full = n % 2 == 0;
            entry(r, table, n, image, true, full ? 18 * n : 9 * n, full ? 100 : 50, full, attrs(3), dims(0, 0));
        }
        sprites_on(r, table, true);
        r.show();
        for (unsigned n = 0; n < 34; ++n)
        {
            const bool drawn = r.px(18 * n + 3, 103) == rgba(entry_colour(0x44));
            if (drawn != (n < 32))
                std::printf("  sprite %u %s\n", n, drawn ? "drawn" : "not drawn");
            CHECK_EQ(drawn, n < 32);
        }
    }
}

TEST(sprctl_is_not_latched_a_write_shows_from_the_next_line)
{
    // SPRCTL isn't latched at line 480, as SPRBASE is: written as the beam starts line 200, it shows from line 200 of
    // that same frame (review round 1, F1). A 64 x 64 sprite with byte 3 bit 7, at (101, 150): full resolution it
    // covers lines 150-213 at x 101-164; doubled, lines 300-427 at x 202-329. In the frame of the write, lines 0-199
    // are drawn doubled (none of it there) and lines 200 on full resolution: rows 50-63 on lines 200-213, and
    // nothing on 300-427.
    Rig r;
    const uint32_t table = 0x1A0000, image = 0x1A1000;
    constexpr uint64_t LINE = VideoCard::LINE_CLOCKS;
    palette(r, 0);
    pattern(r, image, true, 64, 64, 5);
    entry(r, table, 0, image, true, 101, 150, true, attrs(3), dims(3, 3));
    sprites_on(r, table, false);
    r.show(); // the beam back at the start of line 0
    REQUIRE(r.card.beam_line() == 0);
    auto want = [&](unsigned x, unsigned y, bool full) {
        unsigned tx, ty;
        if (covers(x, y, 101, 150, 64, 64, full ? 1 : 2, tx, ty))
            if (const unsigned v = image_px(r, image, true, 64, tx, ty, 0))
                return rgba(entry_colour(v));
        return rgba(entry_colour(0));
    };
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return want(x, y, false); }), 0u); // doubled: 300-427
    r.card.tick(200 * LINE);
    CHECK_EQ(r.card.beam_line(), 200u); // lines 0-199 drawn, 200 not yet
    r.reg(R2::SPRCTL, R2::SPR_FULL);
    r.card.tick(FRAME - 200 * LINE); // the rest of the frame, through its vertical blank
    unsigned shown = 0;
    for (unsigned y = 200; y < 214; ++y)
        for (unsigned x = 101; x < 165; ++x)
            shown += r.px(x, y) != rgba(entry_colour(0));
    CHECK(shown > 0);
    CHECK_EQ(frame_differs(r, [&](unsigned x, unsigned y) { return want(x, y, y >= 200); }), 0u);
}

TEST(video_revision_takes_0_1_or_2_only)
{
    for (unsigned rev : {3u, 9u})
    {
        MachineOptions opt;
        opt.rom_override = "-";
        opt.video_revision = rev;
        std::string error;
        CHECK(Machine::build(read_text(EMU_MACHINE_DIR "/backplane.machine"), "backplane.machine", opt, error) ==
              nullptr);
        CHECK(error.find("video_revision") != std::string::npos);
    }
    auto one = backplane(1);
    REQUIRE(one != nullptr);
    CHECK_EQ(one->video_card()->revision(), 1u);
}

TEST(a_built_machines_video_revision_is_fixed)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    card->card_poke(0x123, 0x77);
    CHECK(!card->set_revision(1));                  // the bus is wired for revision 2
    CHECK_EQ(card->revision(), 2u);
    CHECK_EQ(m->bus().peek(0xC4FF), 0x92);
    CHECK_EQ(m->bus().peek_physical(0x80123), 0x77); // the aperture still answers
    auto r1 = backplane(1);
    REQUIRE(r1 != nullptr);
    CHECK(!r1->video_card()->set_revision(2));
    CHECK_EQ(r1->bus().peek(0xC4FF), 0x91);
    VideoCard alone;                                // a card on its own isn't wired: it may change
    CHECK(alone.set_revision(2));
    CHECK(alone.card_memory() != nullptr);
    CHECK(alone.set_revision(1));
    CHECK(alone.card_memory() == nullptr);          // revision 1 has no card memory
}

TEST(vram_shows_card_memory_at_a_24_bit_address)
{
    Rig r;
    for (unsigned i = 0; i < 20; ++i)
        r.card.card_poke(0x1E0000 + i, static_cast<uint8_t>(0x41 + i)); // "ABC...", across a row's end
    CHECK_EQ(r.card.command({"1E0000", "20"}),
             std::string("1E0000  41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50  ABCDEFGHIJKLMNOP\n"
                         "1E0010  51 52 53 54                                      QRST\n"));
    CHECK_EQ(r.card.command({"$1E0002", "1"}), std::string("1E0002  43                                               C\n"));
    const std::string sixteen = r.card.command({"1E0000"}); // default 16
    CHECK_EQ(sixteen.size(), std::string("1E0000  41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50  ABCDEFGHIJKLMNOP\n").size());
    // Past the fitted 2 MB: $FF, as the card reads it; the address wraps at 24 bits.
    CHECK_EQ(r.card.command({"200000", "2"}), std::string("200000  FF FF                                            ..\n"));
    r.card.card_poke(0, 0x5A);
    CHECK_EQ(r.card.command({"FFFFFF", "2"}).substr(0, 14), std::string("FFFFFF  FF 5A "));
}

TEST(vram_writes_card_memory_without_touching_the_port)
{
    Rig r;
    r.paddr(0x123456);
    r.reg16(R2::PSTEP, 3);
    CHECK_EQ(r.card.command({"040000", "=", "12", "$34", "ab"}),
             std::string("040000  12 34 AB                                         .4.\n"));
    CHECK_EQ(r.card.card_peek(0x040000), 0x12);
    CHECK_EQ(r.card.card_peek(0x040001), 0x34);
    CHECK_EQ(r.card.card_peek(0x040002), 0xAB);
    CHECK_EQ(r.paddr(), 0x123456u);                 // a debugger's poke: no PDATA cycle
    CHECK_EQ(r.card.peek(R2::PSTEP), 3);
    CHECK_EQ(r.card.command({"1FFFFF", "=", "77", "88"}).substr(0, 14), std::string("1FFFFF  77 FF ")); // past it: ignored
    CHECK_EQ(r.card.card_peek(0x1FFFFF), 0x77);
}

TEST(vram_refuses_bad_arguments_and_a_revision_1_card)
{
    Rig r;
    r.card.card_poke(0x10, 0x99);
    const std::string usage = r.card.command({});
    CHECK(usage.find("vram ADDR [N]") != std::string::npos);
    CHECK_EQ(r.card.command({"1000000"}), usage);   // 7 digits
    CHECK_EQ(r.card.command({"XYZ"}), usage);
    CHECK_EQ(r.card.command({"10", "0"}), usage);
    CHECK_EQ(r.card.command({"10", "4097"}), usage);
    CHECK_EQ(r.card.command({"10", "1A"}), usage);  // a count is decimal
    CHECK_EQ(r.card.command({"10", "="}), usage);
    CHECK_EQ(r.card.command({"10", "2", "3"}), usage);
    CHECK_EQ(r.card.command({"10", "=", "100"}), std::string("bad byte '100'\n"));
    CHECK_EQ(r.card.card_peek(0x10), 0x99);         // nothing written by a refused line
    CHECK(r.card.command({"0", "4096"}).size() > 256 * 60);
    VideoCard one; // revision 1: it has no card memory
    CHECK(one.command({"0"}).find("no card memory") != std::string::npos);
}

TEST(vram_at_the_debuggers_prompt_reaches_the_machines_card)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Cpu6502 cpu(m->bus(), Cpu6502::Model::W65C02S);
    Symbols symbols;
    Debugger d(cpu, m->bus(), symbols);
    d.add_command("vram", "   vram ADDR [N]\n", [card](const std::vector<std::string> &args) { return card->command(args); });
    CHECK(d.command("h").find("vram ADDR [N]") != std::string::npos);
    CHECK_EQ(d.command("vram 2000 = 48 49"), std::string("002000  48 49                                            HI\n"));
    CHECK_EQ(m->bus().peek_physical(0x82000), 0x48); // aperture slot 0 shows it
    CHECK_EQ(m->bus().peek_physical(0x82001), 0x49);
    CHECK_EQ(d.command("vram 2000 2"), std::string("002000  48 49                                            HI\n"));
}

// --- the 24-bit bus (step B1): the aperture at $E00000, bank 8 sent there with a revision 2 card ----

namespace
{
    /// backplane.machine with no video line at all, flash erased.
    std::unique_ptr<Machine> backplane_without_video()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const size_t at = text.find("\nvideo ");
        if (at == std::string::npos)
            return nullptr;
        text.erase(at + 1, text.find('\n', at + 1) - at);
        MachineOptions opt;
        opt.rom_override = "-";
        std::string error;
        auto m = Machine::build(text, "backplane.machine", opt, error);
        if (!m)
            std::printf("  %s\n", error.c_str());
        return m;
    }

    /// Every DMA cycle's lines, as the Console card sees them.
    struct DmaLines : CycleObserver
    {
        explicit DmaLines(const Decoder &d) : bus(d) {}
        void on_cycle(const BusCycle &c) override
        {
            if (c.dma)
                seen.push_back(bus.last_access());
        }
        const Decoder &bus;
        std::vector<Access> seen;
    };
} // namespace

TEST(on_revision_2_the_cpu_card_sends_bank_8_to_the_aperture_at_e00000)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    for (unsigned n = 0; n < 4; ++n)
        card->card_poke(n * 0x4000 + 0x123, static_cast<uint8_t>(0xC0 + n));
    for (unsigned n = 0; n < 4; ++n)
    {
        // The CPU card's view (peek_physical, as a DMA cycle): bank 8 and $E0xxxx are both the card.
        CHECK_EQ(bus.peek_physical(0x080000 + n * 0x4000 + 0x123), 0xC0 + n);
        CHECK_EQ(bus.peek_physical(0xE00000 + n * 0x4000 + 0x123), 0xC0 + n);
        // Through a window on block 32 + n: the bus carries $E0xxxx, the bridge's ID.
        bus.write(0xC681, static_cast<uint8_t>(32 + n));
        CHECK_EQ(bus.read(0x4123), 0xC0 + n);
        CHECK_EQ(bus.last_access().physical, 0xE00000 + n * 0x4000 + 0x123);
        CHECK_EQ(bus.last_access().module, BackplaneBridge::MODULE);
    }
    bus.write(0xC681, 33);
    bus.write(0x4200, 0x6B);                         // a CPU store through slot 1
    CHECK_EQ(card->card_peek(0x4200), 0x6B);
    bus.write_physical(0xE08001, 0x6C);              // and one at the aperture's own address
    CHECK_EQ(card->card_peek(0x8001), 0x6C);
    // The bus as a card driving it sees it (the Console card): bank 8 is the RAM card's.
    bus.write_bus(0x080123, 0x5C);
    CHECK_EQ(bus.peek_bus(0x080123), 0x5C);
    CHECK_EQ(card->card_peek(0x0123), 0xC0);         // the card untouched
    CHECK_EQ(bus.peek_physical(0x080123), 0xC0);     // and the CPU card still sees the aperture
    CHECK_EQ(bus.peek_bus(0xE00123), 0xC0);
}

TEST(without_a_revision_2_card_bank_8_is_ram_and_e00000_is_empty)
{
    std::vector<std::unique_ptr<Machine>> machines;
    machines.push_back(backplane(1));
    machines.push_back(backplane_without_video());
    for (auto &m : machines)
    {
        REQUIRE(m != nullptr);
        Decoder &bus = m->bus();
        // MAP's AP8 is set at reset whatever is fitted: bank 8 goes to the empty aperture until it is cleared.
        CHECK_EQ(bus.peek_physical(0x080123), 0xFF);
        bus.write(0xC681, 32);                       // W1 on block 32: the cycle carries $E00123
        bus.read(0x4123);
        CHECK_EQ(bus.last_access().physical, 0xE00123u);
        bus.write(0xC6A5, 0);                        // MAP: AP8 clear, as the BIOS does with no revision 2 card
        bus.write_physical(0x080123, 0x42);
        CHECK_EQ(bus.peek_physical(0x080123), 0x42); // the RAM
        CHECK_EQ(bus.peek_bus(0x080123), 0x42);
        bus.write(0xC681, 32);                       // W1 on block 32: the RAM too
        CHECK_EQ(bus.read(0x4123), 0x42);
        CHECK_EQ(bus.last_access().physical, 0x080123u);
        bus.write_physical(0xE00123, 0x24);          // nothing answers at $E00000
        CHECK_EQ(bus.peek_physical(0xE00123), 0xFF);
        CHECK_EQ(bus.peek_physical(0x080123), 0x42);
    }
}

TEST(dma_memory_cycles_carry_8_a_fly_by_the_cards_id_and_bank_8_goes_to_the_aperture)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    DmaLines lines(bus);
    bus.set_dma_observer(&lines);
    for (unsigned i = 0; i < 16; ++i)
        bus.write_physical(0x21000 + i, static_cast<uint8_t>(0x30 + i));
    // Memory to memory, $21000 to $84000: the reads at $21xxx, the writes at the aperture's slot 1.
    const uint32_t src = 0x21000, dest = 0x84000;
    // SELECT 1 first: the window at $C400-$C40F then shows channel 1
    bus.poke(0xC47E, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 1 << 1);
    bus.poke(0xC400, static_cast<uint8_t>(src));
    bus.poke(0xC401, static_cast<uint8_t>(src >> 8));
    bus.poke(0xC402, static_cast<uint8_t>(src >> 16));
    bus.poke(0xC408, static_cast<uint8_t>(dest));
    bus.poke(0xC409, static_cast<uint8_t>(dest >> 8));
    bus.poke(0xC40A, static_cast<uint8_t>(dest >> 16));
    bus.poke(0xC404, 16);
    bus.poke(0xC405, 0);
    bus.poke(0xC406, DmaController::BLOCK | DmaController::MEM_TO_MEM);
    bus.poke(0xC407, DmaController::ENABLE | DmaController::REQ);
    for (int i = 0; i < 50; ++i)
        bus.read(0x0000);
    CHECK_EQ(card->card_peek(0x4000 + 15), 0x3F);
    REQUIRE(lines.seen.size() == 32u);
    unsigned wrong = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        const Access &rd = lines.seen[2 * i], &wr = lines.seen[2 * i + 1];
        wrong += rd.physical != 0x21000 + i || rd.module != BackplaneBridge::DMA_MODULE;
        wrong += wr.physical != 0xE04000 + i || wr.module != BackplaneBridge::DMA_MODULE;
    }
    CHECK_EQ(wrong, 0u);
    // A fly-by on channel 1: the memory side by address, PS0-PS3 the video card's 9.
    lines.seen.clear();
    bus.write(0xC4B8, 0x00); // PADDR $000400
    bus.write(0xC4B9, 0x04);
    bus.write(0xC4BA, 0x00);
    bus.write(0xC4BE, R2::PCTL_DRQ);
    dma_channel_1(bus, 0x21000, 16, DmaController::TO_CARD);
    CHECK_EQ(card->card_peek(0x400 + 15), 0x3F);
    REQUIRE(lines.seen.size() >= 16u);
    unsigned flyby = 0;
    for (const Access &a : lines.seen)
        flyby += a.module == 0x9 && a.physical >= 0x21000 && a.physical < 0x21010;
    CHECK_EQ(flyby, 16u);
}

TEST(the_console_cards_exam_and_dep_see_bank_8_as_the_ram_cards)
{
    auto m = backplane(2);
    REQUIRE(m != nullptr);
    ConsoleCard *panel = m->console_card();
    REQUIRE(panel != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    card->card_poke(0x0123, 0xC3);
    bus.write_bus(0x080123, 0x3C);
    panel->set_halt_switch(true);
    panel->set_switches(0x80123);
    panel->press(ConsoleCard::LOAD_ADRS);
    panel->press(ConsoleCard::EXAM);
    const ConsoleCard::Lights l = panel->lights();
    uint8_t shown = 0;
    for (unsigned i = 0; i < 8; ++i)
        shown = static_cast<uint8_t>(shown | (l.data[i] > 0.5f ? 1u << i : 0u));
    CHECK_EQ(shown, 0x3C);                            // the RAM's byte, not the card's $C3
    panel->set_switches(0x000A5);
    panel->press(ConsoleCard::DEP);
    CHECK_EQ(bus.peek_bus(0x080123), 0xA5);           // DEP: the RAM
    CHECK_EQ(card->card_peek(0x0123), 0xC3);          // the card memory untouched
    CHECK_EQ(bus.peek_physical(0x080123), 0xC3);      // the CPU's bank 8 is still the aperture
}

// --- the north bridge (step B7): MAP's AP8 and WIN1H on the machine ------------

namespace
{
    /// backplane.machine, flash erased.
    std::unique_ptr<Machine> backplane_erased()
    {
        const std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        MachineOptions opt;
        opt.rom_override = "-";
        std::string error;
        auto m = Machine::build(text, "backplane.machine", opt, error);
        if (!m)
            std::printf("  %s\n", error.c_str());
        return m;
    }

    /// The DMA controller's memory to memory copy of 16 bytes, channel 0, run to its end.
    void copy16(Decoder &bus, uint32_t src, uint32_t dest)
    {
        bus.poke(0xC47E, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE); // SELECT 0
        bus.poke(0xC400, static_cast<uint8_t>(src));
        bus.poke(0xC401, static_cast<uint8_t>(src >> 8));
        bus.poke(0xC402, static_cast<uint8_t>(src >> 16));
        bus.poke(0xC408, static_cast<uint8_t>(dest));
        bus.poke(0xC409, static_cast<uint8_t>(dest >> 8));
        bus.poke(0xC40A, static_cast<uint8_t>(dest >> 16));
        bus.poke(0xC404, 16);
        bus.poke(0xC405, 0);
        bus.poke(0xC406, DmaController::BLOCK | DmaController::MEM_TO_MEM);
        bus.poke(0xC407, DmaController::ENABLE | DmaController::REQ);
        for (int i = 0; i < 50; ++i)
            bus.read(0x0000);
    }

    /// What the Console card's EXAM shows at `physical`.
    uint8_t exam(ConsoleCard &panel, uint32_t physical)
    {
        panel.set_switches(physical);
        panel.press(ConsoleCard::LOAD_ADRS);
        panel.press(ConsoleCard::EXAM);
        const ConsoleCard::Lights l = panel.lights();
        uint8_t shown = 0;
        for (unsigned i = 0; i < 8; ++i)
            shown = static_cast<uint8_t>(shown | (l.data[i] > 0.5f ? 1u << i : 0u));
        return shown;
    }
} // namespace

TEST(with_ap8_clear_bank_8_is_ram_and_the_card_is_at_e00000_and_win1h_0e)
{
    auto m = backplane_erased();
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    REQUIRE(card != nullptr);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC6FF), 0xD3);
    CHECK_EQ(bus.peek(0xC6A5), 0x01);                  // AP8 set at reset: old software's bank 8
    card->card_poke(0x4123, 0xC1);                     // slot 1
    CHECK_EQ(bus.peek_physical(0x084123), 0xC1);
    bus.write(0xC6A5, 0x00);                           // AP8 clear
    bus.write_physical(0x084123, 0x42);                // bank 8: the RAM
    CHECK_EQ(bus.peek_physical(0x084123), 0x42);
    CHECK_EQ(bus.peek_bus(0x084123), 0x42);
    CHECK_EQ(card->card_peek(0x4123), 0xC1);           // the card untouched
    CHECK_EQ(bus.peek_physical(0xE04123), 0xC1);       // and still at its own address
    bus.write(0xC681, 33);                             // W1 on block 33: the RAM
    CHECK_EQ(bus.read(0x4123), 0x42);
    CHECK_EQ(bus.last_access().physical, 0x084123u);
    bus.write(0xC6A1, 0x0E);                           // WIN1H $0E, WIN1 1: slot 1 by its new address
    bus.write(0xC681, 1);
    CHECK_EQ(bus.read(0x4123), 0xC1);
    CHECK_EQ(bus.last_access().physical, 0xE04123u);
    bus.write(0x4124, 0x6E);
    CHECK_EQ(card->card_peek(0x4124), 0x6E);
    bus.write(0xC6A5, 0x01);                           // AP8 set: WIN1H $0E is the card all the same
    CHECK_EQ(bus.read(0x4124), 0x6E);
    bus.write(0xC6A1, 0x00);
    bus.write(0xC681, 33);                             // and block 33 is the aperture again
    CHECK_EQ(bus.read(0x4123), 0xC1);
}

TEST(dma_to_84000_follows_ap8_both_ways)
{
    auto m = backplane_erased();
    REQUIRE(m != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    for (unsigned i = 0; i < 16; ++i)
        bus.write_physical(0x21000 + i, static_cast<uint8_t>(0x30 + i));
    copy16(bus, 0x21000, 0x84000);                     // AP8 set: the card's slot 1
    CHECK_EQ(card->card_peek(0x4000 + 15), 0x3F);
    CHECK_EQ(bus.peek_bus(0x84000 + 15), 0x00);
    card->card_poke(0x4000 + 15, 0x00);
    bus.write(0xC6A5, 0x00);                           // AP8 clear: the RAM
    copy16(bus, 0x21000, 0x84000);
    CHECK_EQ(bus.peek_bus(0x84000 + 15), 0x3F);
    CHECK_EQ(card->card_peek(0x4000 + 15), 0x00);
    copy16(bus, 0x21000, 0xE08000);                    // the card's own address, AP8 or not
    CHECK_EQ(card->card_peek(0x8000 + 15), 0x3F);
    bus.write(0xC6A5, 0x01);                           // and set again: the card once more
    for (unsigned i = 0; i < 16; ++i)
        bus.write_physical(0x21000 + i, static_cast<uint8_t>(0x50 + i));
    copy16(bus, 0x21000, 0x84000);
    CHECK_EQ(card->card_peek(0x4000 + 15), 0x5F);
    CHECK_EQ(bus.peek_bus(0x84000 + 15), 0x3F);        // the RAM keeps the earlier copy
}

TEST(exam_and_dep_see_bank_8s_ram_whatever_ap8_holds)
{
    auto m = backplane_erased();
    REQUIRE(m != nullptr);
    ConsoleCard *panel = m->console_card();
    REQUIRE(panel != nullptr);
    VideoCard *card = m->video_card();
    Decoder &bus = m->bus();
    card->card_poke(0x0123, 0xC3);
    bus.write_bus(0x080123, 0x3C);
    panel->set_halt_switch(true);
    for (const uint8_t map : {uint8_t{0x01}, uint8_t{0x00}})
    {
        bus.write(0xC6A5, map);
        CHECK_EQ(exam(*panel, 0x80123), 0x3C);         // the RAM's byte, never the card's $C3
        CHECK_EQ(bus.peek_physical(0x080123), map ? 0xC3 : 0x3C); // while the CPU card's bank 8 follows MAP
    }
    bus.write(0xC6A5, 0x01);
    panel->set_switches(0x80123);
    panel->press(ConsoleCard::LOAD_ADRS);
    panel->set_switches(0x000A5);
    panel->press(ConsoleCard::DEP);
    CHECK_EQ(bus.peek_bus(0x080123), 0xA5);            // DEP: the RAM
    CHECK_EQ(card->card_peek(0x0123), 0xC3);
}

int main()
{
    return run_all();
}
