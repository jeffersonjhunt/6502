/**
 * @file test_video_rev2.cpp
 * @brief The video card's revision 2 (hw/video-rev2.md, proposed): card memory, the register map,
 * the aperture, the data port and its DMA, and the line renderer drawing from card memory.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The first cases drive the card alone, its "CPU clock" the pixel clock (as test_video's do);
 * the later ones put it on a backplane machine: backplane.machine itself with the revision
 * chosen by MachineOptions, so the aperture, RAM C and DMA channel 1 are wired as its lines say.
 * Revision 1 keeps test_video and the BIOS and OODOS suites.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "dma_controller.h"
#include "machine.h"
#include "video_card.h"

#include <cstdio>
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
            card.set_revision(2);
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
        if (o == 0x02 || o == 0x03 || o == 0x0E || o == 0x0F)
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
    CHECK_EQ(n, 4u + 2 * 6 + 63);
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
    c.set_revision(2);
    CHECK_EQ(c.peek(VideoCard::ID_REG), 0x92);
    c.set_revision(1);
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

TEST(backplane_machine_is_revision_1_by_default)
{
    auto m = backplane(0);
    REQUIRE(m != nullptr);
    REQUIRE(m->video_card() != nullptr);
    CHECK_EQ(m->video_card()->revision(), 1u);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC4FF), 0x91);
    bus.write_physical(0x80800, 0x5A); // bank 8 is RAM C's, and the card draws from it
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
        bus.poke(0xC410, static_cast<uint8_t>(addr));
        bus.poke(0xC411, static_cast<uint8_t>(addr >> 8));
        bus.poke(0xC412, static_cast<uint8_t>(addr >> 16));
        bus.poke(0xC414, static_cast<uint8_t>(count));
        bus.poke(0xC415, static_cast<uint8_t>(count >> 8));
        bus.poke(0xC416, DmaController::BLOCK | dir);
        bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
        bus.poke(0xC417, DmaController::ENABLE);
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
    CHECK(Machine::build(head + "sound 500-57F space=io rev=2\n", "v.machine", {}, error) == nullptr);
    CHECK(error.find("only the video card") != std::string::npos);
    auto none = Machine::build(head + "aperture 80000-8FFFF\n", "v.machine", {}, error); // no card: inert
    REQUIRE(none != nullptr);
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
        const uint16_t c = static_cast<uint16_t>(0xC400 + n * 0x10);
        bus.poke(c + 0u, static_cast<uint8_t>(addr));
        bus.poke(c + 1u, static_cast<uint8_t>(addr >> 8));
        bus.poke(c + 2u, static_cast<uint8_t>(addr >> 16));
        bus.poke(c + 4u, static_cast<uint8_t>(count));
        bus.poke(c + 5u, static_cast<uint8_t>(count >> 8));
        bus.poke(c + 6u, mode);
        bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
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
    bus.poke(0xC410, static_cast<uint8_t>(src));
    bus.poke(0xC411, static_cast<uint8_t>(src >> 8));
    bus.poke(0xC412, static_cast<uint8_t>(src >> 16));
    bus.poke(0xC418, static_cast<uint8_t>(dest));
    bus.poke(0xC419, static_cast<uint8_t>(dest >> 8));
    bus.poke(0xC41A, static_cast<uint8_t>(dest >> 16));
    bus.poke(0xC414, 0x00); // 512
    bus.poke(0xC415, 0x02);
    bus.poke(0xC416, DmaController::BLOCK | DmaController::MEM_TO_MEM);
    bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
    bus.poke(0xC417, DmaController::ENABLE | DmaController::REQ);
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

int main()
{
    return run_all();
}
