/**
 * @file test_gwbasic_gfx.cpp
 * @brief GW-BASIC's graphics, phase 7e1 (hw/oodos.md, "The plan, phase 7": block P5's SCREEN 1 and 2, PSET, PRESET,
 *        POINT, LINE with STEP, B and BF, COLOR, WIDTH and CLS on a graphics screen), run from OODOS's CCP on
 *        backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_screen does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M: and GW-BASIC takes its own screen (CON:'s output keeps the CRT). The text is read from the layer that shows
 * it (layer 0 in SCREEN 0; layer 1, over the graphics, in SCREEN 1 and 2: the owner's answer 5 of 2026-11-19), and
 * the pixels from the card through layer 0's registers: SCREEN 1's map entries (MAPBASE) to its tiles (TILEBASE),
 * SCREEN 2's mono rows from MAPBASE, both rows of each pair. So what is shown is checked, not just what was written.
 *
 * The pixel cases (G1, G2, G8) run a program of gwbasic/ (NAME.bas, put on M: with its lines from 9000 cut: those
 * dump the screen in PC-BASIC, by GET, as hex rows) and compare the card's pixels, in the same rows, with PC-BASIC's
 * (NAME.out, by oodos/gwbasic/tools/oracle.sh); the others are written by hand from the source's meaning (GENGRP.ASM)
 * and the owner's answers, the card's registers and palette read where PC-BASIC's screen isn't this machine's. Each
 * case says the failure on real hardware it guards and the broken build it was seen failing on (hw/oodos.md, 7e1 as
 * built).
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string BUILD = std::getenv("GWBASIC_BUILD") ? std::getenv("GWBASIC_BUILD") : EMU_GWBASIC_BUILD;

    std::vector<uint8_t> file(const std::string &name)
    {
        std::ifstream f(BUILD + "/" + name, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    // The video card's registers (revision 2: the Technical Reference, chapter 6; oodos/lib/gx.inc).
    constexpr uint16_t CTRL = 0xC480, PALBASE = 0xC48A, L0 = 0xC490, L1 = 0xC4A0;
    constexpr uint16_t L_CONFIG = 0, L_MAP = 1, L_TILE = 3, L_VSCROLL = 7, L_PALOFS = 9;

    uint8_t reg(CcpRig &r, uint16_t a) { return r.machine->bus().peek(a); }
    uint16_t reg16(CcpRig &r, uint16_t a) { return static_cast<uint16_t>(reg(r, a) | reg(r, a + 1) << 8); }
    uint8_t card(CcpRig &r, uint32_t a) { return r.machine->video_card()->card_peek(a); }

    /// The layer the text shows on: 1 when CTRL shows layer 1 (a graphics mode's overlay), else 0.
    uint16_t text_layer(CcpRig &r) { return (reg(r, CTRL) & 0x04) ? L1 : L0; }
    uint32_t text_base(CcpRig &r) { return reg16(r, text_layer(r) + L_MAP) * 256u; }
    unsigned columns(CcpRig &r) { return (reg(r, text_layer(r) + L_CONFIG) & 7) == 1 ? 40 : 80; }

    /// Row `row` (from 0) of the text, its trailing blanks dropped, the "not typed" mark ($FF) a blank.
    std::string line(CcpRig &r, unsigned row)
    {
        const uint32_t base = text_base(r);
        const unsigned cols = columns(r);
        std::string s;
        for (unsigned c = 0; c < cols; ++c)
        {
            const char ch = static_cast<char>(card(r, base + row * cols * 2 + c * 2));
            s += static_cast<unsigned char>(ch) == 0xFF ? ' ' : ch;
        }
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    }
    /// A text cell's attribute.
    uint8_t attr(CcpRig &r, unsigned row, unsigned col)
    {
        return card(r, text_base(r) + row * columns(r) * 2 + col * 2 + 1);
    }
    std::string screen(CcpRig &r, unsigned rows = 25)
    {
        std::string s;
        for (unsigned i = 0; i < rows; ++i)
            s += line(r, i) + "|";
        return s;
    }
    int last_row(CcpRig &r, const std::string &text)
    {
        for (int i = 24; i >= 0; --i)
            if (line(r, static_cast<unsigned>(i)).find(text) != std::string::npos)
                return i;
        return -1;
    }
    /// Run until some row of the text holds `text`. @return its row, or -1
    int until_row(CcpRig &r, const std::string &text, uint64_t budget = ms(4000))
    {
        const uint64_t end = r.cpu->cycles() + budget;
        while (r.cpu->cycles() < end)
        {
            for (unsigned i = 0; i < 30; ++i)
                if (line(r, i).find(text) != std::string::npos)
                    return static_cast<int>(i);
            r.run(ms(10));
        }
        std::printf("  never saw '%s' on the screen: %s\n", text.c_str(), screen(r).c_str());
        return -1;
    }

    /// A character's key on a US keyboard: its usage and whether it needs Shift. 0 none.
    std::pair<uint8_t, bool> usage(char ch)
    {
        static const std::string pairs = "1!2@3#4$5%6^7&8*9(0)\r\r\x1b\x1b\b\b\t\t  -_=+[{]}\\|~~;:'\"`~,<.>/?";
        if (ch >= 'a' && ch <= 'z')
            return {static_cast<uint8_t>(0x04 + ch - 'a'), false};
        if (ch >= 'A' && ch <= 'Z') // Caps Lock is on (GW-BASIC's own, at start-up): capitals unshifted
            return {static_cast<uint8_t>(0x04 + ch - 'A'), false};
        for (size_t i = 0; i < pairs.size(); ++i)
            if (pairs[i] == ch && !(i / 2 == 0x32 - 0x1E))
                return {static_cast<uint8_t>(0x1E + i / 2), (i & 1) != 0};
        return {0, false};
    }
    constexpr uint8_t SHIFT = 0xE1;
    void press(CcpRig &r, uint8_t use, uint8_t mod = 0, unsigned hold_ms = 20)
    {
        UsbCard *usb = r.machine->usb_card();
        if (mod)
        {
            usb->key(mod, true);
            r.run(ms(10));
        }
        usb->key(use, true);
        r.run(ms(hold_ms));
        usb->key(use, false);
        r.run(ms(20));
        if (mod)
        {
            usb->key(mod, false);
            r.run(ms(10));
        }
    }
    void keys(CcpRig &r, const std::string &text)
    {
        for (const char ch : text)
        {
            const auto [u, shift] = usage(ch);
            REQUIRE(u != 0);
            press(r, u, shift ? SHIFT : 0);
        }
    }
    void enter(CcpRig &r, const std::string &text, uint64_t settle = ms(300))
    {
        keys(r, text + "\r");
        r.run(settle);
    }

    /// gwbasic/NAME.bas's lines, those from 9000 (PC-BASIC's dump) left out unless `all`.
    std::string program(const std::string &name, bool all = false)
    {
        std::string out, l;
        auto take = [&] {
            if (!l.empty() && (all || std::atoi(l.c_str()) < 9000))
                out += l + "\r\n";
            l.clear();
        };
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".bas"))
            if (c == '\n')
                take();
            else if (c != '\r')
                l += c;
        take();
        return out;
    }

    /// A CcpRig with GW-BASIC's two files on M: (and `progs`, gwbasic/NAME.bas each, as NAME.BAS), and `command`
    /// typed at the CCP (M:GWBASIC, a program's name after it to run that): until GW-BASIC's Ok.
    int start(CcpRig &r, const std::string &command = "m:gwbasic", const std::vector<std::string> &progs = {},
              uint64_t budget = ms(4000))
    {
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        CHECK(!com.empty() && !ovl.empty());
        if (com.empty() || ovl.empty())
            return -1;
        std::vector<MFile> files = {{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}};
        for (const std::string &p : progs)
        {
            std::string n = p;
            for (char &c : n)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            n.resize(8, ' ');
            files.push_back({0, n + "BAS", bytes(program(p))});
        }
        r.put_m(files);
        const size_t from = r.out.size();
        r.type(command + "\r");
        CHECK(r.until(command + "\r\n", ms(2000), from));
        const int ok = until_row(r, "Ok", budget);
        r.run(ms(100));
        return ok;
    }

    /// PC-BASIC's dump of gwbasic/NAME.bas's screen (NAME.out): its rows of hex, GET's bytes in order, past the
    /// line that gives the array's size; blank lines (its width's wrap) left out.
    std::vector<std::string> expected(const std::string &name)
    {
        std::vector<std::string> rows;
        std::string l;
        bool on = false;
        auto take = [&] {
            if (on && !l.empty())
                rows.push_back(l);
            if (l.find(" 640  200") != std::string::npos)
                on = true;
            l.clear();
        };
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".out"))
            if (c == '\n')
                take();
            else if (c != '\r')
                l += c;
        take();
        return rows;
    }

    /// The pixel at (x, y) as layer 0 shows it: SCREEN 1's from its map entry's tile (type 2, 4-bit 8 x 8
    /// tiles, the map 64 wide), the entry's palette offset 0; SCREEN 2's from its mono rows (type 5), both rows
    /// of the pair (-1 if they differ).
    int pixel(CcpRig &r, unsigned x, unsigned y)
    {
        const uint8_t config = reg(r, L0 + L_CONFIG);
        const uint32_t map = reg16(r, L0 + L_MAP) * 256u;
        if ((config & 7) == 2)
        {
            const uint32_t e = map + ((y / 8) * 64 + x / 8) * 2;
            const unsigned tile = card(r, e) | (card(r, e + 1) & 3) << 8;
            if (card(r, e + 1) & 0xFC)
                return -1;
            const uint8_t b = card(r, reg16(r, L0 + L_TILE) * 256u + tile * 32 + (y % 8) * 4 + (x % 8) / 2);
            return x % 2 ? b & 0x0F : b >> 4;
        }
        if ((config & 7) == 5)
        {
            const unsigned bit = 7 - x % 8;
            const int a = card(r, map + (2 * y) * 80 + x / 8) >> bit & 1;
            const int b = card(r, map + (2 * y + 1) * 80 + x / 8) >> bit & 1;
            return a == b ? a : -1;
        }
        return -2;
    }
    /// The card's screen, in PC-BASIC's dump's rows: GET's bytes, 80 a row (4 pixels a byte in SCREEN 1, the left
    /// in bits 7-6; 8 in SCREEN 2), in hex. A pixel layer 0 can't show (a wrong map entry, the mono pair unequal)
    /// is "?".
    std::vector<std::string> dump(CcpRig &r)
    {
        const bool mono = (reg(r, L0 + L_CONFIG) & 7) == 5;
        const unsigned per = mono ? 8 : 4, bits = mono ? 1 : 2;
        std::vector<std::string> rows;
        for (unsigned y = 0; y < 200; ++y)
        {
            std::string s;
            for (unsigned b = 0; b < 80; ++b)
            {
                int v = 0;
                bool bad = false;
                for (unsigned i = 0; i < per; ++i)
                {
                    const int p = pixel(r, b * per + i, y);
                    bad = bad || p < 0 || p >= (1 << bits);
                    v = v << bits | (p & ((1 << bits) - 1));
                }
                char h[3];
                std::snprintf(h, sizeof h, "%02X", v);
                s += bad ? "??" : h;
            }
            rows.push_back(s);
        }
        return rows;
    }
    /// The card's rows against NAME.out's: each row that differs printed (the first 8), and the count.
    unsigned compare(CcpRig &r, const std::string &name)
    {
        const std::vector<std::string> want = expected(name), got = dump(r);
        CHECK_EQ(want.size(), size_t{200});
        unsigned bad = 0;
        for (size_t y = 0; y < got.size() && y < want.size(); ++y)
            if (got[y] != want[y] && ++bad <= 8)
                std::printf("  row %zu:\n    want %s\n    got  %s\n", y, want[y].c_str(), got[y].c_str());
        if (bad)
            std::printf("  %u rows of 200 differ\n", bad);
        return bad;
    }
} // namespace

// SCREEN 1's pixels against PC-BASIC's (gwbasic/gfx1): PSET and PRESET with and without an attribute (the colours'
// low 2 bits, PC-BASIC's: 7 is 3, 6 is 2), STEP; LINE in all eight octants, steep and shallow, from either end;
// -(x,y), -STEP, STEP(x,y)-STEP; B and BF with their corners either way, a 1-pixel box of each, the default and
// colour 0; BF boxes off every edge; lines off every edge (shallow off the sides, steep off the top and bottom:
// where PC-BASIC's clipping and the source's truncation agree); PSET off the screen (nothing); the frame's bank
// boundary (tile row 12, column 32) crossed by a line and a box. And the layer as PC-BASIC's screen shows it:
// VSCROLL 236, 20 layer lines above the 200. Guards a picture drawn in the wrong place or colour, or a line or box
// clipped wrong. Seen failing with (a) RIGHTC's step across a tile column 4, not 32 - 3, and (b) the left pixel
// in a byte's low 4 bits.
TEST(screen_1_pixels_match_pc_basic)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx1", {"gfx1"}, ms(8000)) >= 0);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    CHECK_EQ(reg16(r, L0 + L_VSCROLL), 236);
    CHECK_EQ(compare(r, "gfx1"), 0u);
}

// SCREEN 2's pixels against PC-BASIC's (gwbasic/gfx2): the same set at 640 across, the attributes' low bit (2 is
// 0, 3 is 1), and the rows past the page's first 16 KB (row 102's pair crosses it; a line along it, one across
// rows 103-104, a box over rows 96-110 and a line down the whole screen); every row shown twice, the mono layer's
// VSCROLL 440. Guards a row shown once (half-height lines), or the picture's bottom half in the wrong card bank.
// Seen failing with (a) SETC writing row 2y only, (b) DOWNC not mapping the next bank (the cached bank kept).
TEST(screen_2_pixels_match_pc_basic)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx2", {"gfx2"}, ms(8000)) >= 0);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x05);
    CHECK_EQ(reg16(r, L0 + L_VSCROLL), 440);
    CHECK_EQ(compare(r, "gfx2"), 0u);
}

// Where the source and PC-BASIC part (by hand, from GENGRP.ASM; a recorded deviation, 7e1 as built): a line's end off
// the screen is truncated to its edge (SCALXY, DOGRPH's "just truncate"), so LINE (300,10)-(400,60),1,B draws its
// right side on x 319, rows 10-60 (PC-BASIC clips that side away), and LINE (-100,0)-(100,100) runs from (0,0) as a
// 45-degree line; and a pixel half a step off the line goes the source's way (LINLP3's sum from half the major delta,
// stepped at it: PC-BASIC steps past it), so LINE (300,10)-(310,50) in SCREEN 2 has x 300 on rows 10 and 11 only.
// Guards the source's line where an edge or a half step meets it. Seen failing with SCALXY leaving an x past the last
// as it is.
TEST(lines_off_the_screen_and_half_steps_are_the_source_s)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:LINE (300,10)-(400,60),1,B:LINE (-100,0)-(100,100),2", ms(1000));
    unsigned side = 0;
    for (unsigned y = 10; y <= 60; ++y)
        side += pixel(r, 319, y) == 1;
    CHECK_EQ(side, 51u);
    CHECK_EQ(pixel(r, 0, 0), 2);
    CHECK_EQ(pixel(r, 50, 50), 2);
    CHECK_EQ(pixel(r, 100, 100), 2);
    CHECK_EQ(pixel(r, 0, 50), 0);
    enter(r, "SCREEN 2:LINE (300,10)-(310,50)", ms(1000));
    CHECK_EQ(pixel(r, 300, 10), 1);
    CHECK_EQ(pixel(r, 300, 11), 1);
    CHECK_EQ(pixel(r, 300, 12), 0);
    CHECK_EQ(pixel(r, 301, 12), 1);
    CHECK_EQ(pixel(r, 310, 48), 1);
    CHECK_EQ(pixel(r, 310, 50), 1);
}

// Block 1 is back in W1 after every graphics statement (hw/oodos.md's testing list: "GX_W1BACK left out after a
// PSET"): gwbasic/gfx3 fills every 16th element of an array that lies across $4000-$7FFF (VARPTR checked), then
// reads them all back after each of PSET, PRESET, LINE, LINE B and BF, POINT, CLS, COLOR, SCREEN 1, 2 and 0 (16
// checks). Guards a program's variables read from the card's memory after a drawing. Seen failing with GX_W1BACK
// left out of DOLINE (the LINE's exit).
TEST(block_1_is_back_after_every_graphics_statement)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx3", {"gfx3"}, ms(60000)) >= 0);
    const int at = last_row(r, "CHECKS");
    REQUIRE(at >= 0);
    CHECK_EQ(line(r, static_cast<unsigned>(at)), std::string("CHECKS 16 BAD 0"));
    const int p = last_row(r, "P ");
    REQUIRE(p >= 0);
    unsigned lo = 0, hi = 0;
    CHECK(std::sscanf(line(r, static_cast<unsigned>(p)).c_str(), "P %x %x", &lo, &hi) == 2);
    CHECK(lo < 0x4000 && hi >= 0x8000);
}

// The text is over the graphics (the owner's answer 5): SCREEN 1, the whole screen filled in colour 2, LOCATE 10,5
// and PRINT "HI": layer 1 shows the text at 40 columns (CONFIG text type 1, BLINK) over layer 0's tiles, the cells
// H and I in attribute 3 (colour 3 on background 0, transparent), in layer 0's palette group (PALOFS), CTRL showing
// the display and both layers, the bands (rows 26-30) transparent, the pixels under the text still colour 2, and the
// key line on row 25; SCREEN 2 the same at 80 columns, colour 1. Guards text hidden under the picture, or wiping it,
// or in another colour than the PC's. Seen failing with (a) TXMODE without the overlay bit (the text on layer 0, the
// graphics' layer replaced), (b) the text's attribute's background 7.
TEST(text_is_over_the_graphics)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:LINE (0,0)-(319,199),2,BF:LOCATE 10,5:PRINT \"HI\"", ms(2000));
    CHECK_EQ(reg(r, CTRL), 0x07);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    CHECK_EQ(reg(r, L1 + L_CONFIG), 0x81);
    CHECK_EQ(reg16(r, L1 + L_VSCROLL), 440);
    CHECK_EQ(reg(r, L1 + L_PALOFS), reg(r, L0 + L_PALOFS));
    CHECK_EQ(reg(r, L0 + L_PALOFS), 2);
    CHECK_EQ(line(r, 9), std::string("    HI"));
    CHECK_EQ(attr(r, 9, 4), 0x03);
    CHECK_EQ(attr(r, 9, 5), 0x03);
    CHECK_EQ(attr(r, 9, 0), 0x03);
    CHECK_EQ(attr(r, 26, 0), 0x00);
    CHECK_EQ(pixel(r, 36, 75), 2);
    CHECK_EQ(pixel(r, 44, 79), 2);
    CHECK(line(r, 24).rfind("1LIST", 0) == 0);
    enter(r, "SCREEN 2:LINE (0,0)-(639,199),1,BF:LOCATE 10,5:PRINT \"HI\"", ms(2000));
    CHECK_EQ(reg(r, CTRL), 0x07);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x05);
    CHECK_EQ(reg(r, L1 + L_CONFIG), 0x80);
    CHECK_EQ(reg(r, L1 + L_PALOFS), 3);
    CHECK_EQ(reg(r, L0 + L_PALOFS), 3);
    CHECK_EQ(line(r, 9), std::string("    HI"));
    CHECK_EQ(attr(r, 9, 4), 0x01);
    CHECK_EQ(attr(r, 29, 79), 0x00);
    CHECK_EQ(pixel(r, 36, 75), 1);
    CHECK(line(r, 24).rfind("1LIST", 0) == 0);
}

// SCREEN 0 -> 1 -> 2 -> 0, and WIDTH: each change of mode clears the mode's page (a PSET before it is gone) and its
// text, SCREEN 1's at 40 columns and SCREEN 2's at 80; WIDTH 80 in SCREEN 1 is SCREEN 2 and WIDTH 40 in SCREEN 2 is
// SCREEN 1 (PC-BASIC's), the same mode again changes nothing; SCREEN 1,,1 (CGA's one page) and WIDTH 40,30 Illegal
// function call; SCREEN 0 shows the text on layer 0 again (40 columns, as SCREEN 1 left it), the console's palette
// (PALBASE 0) and layer 1 off, and a program typed before it lists as it was. Guards a program that ends in SCREEN 0
// left with graphics over its text, or the console's colours wrong. Seen failing with SCREEN 0 leaving CTRL's layer
// 1 bit set.
TEST(screen_modes_switch_and_come_back)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "10 PRINT \"KEPT\"");
    const uint16_t pal0 = reg16(r, PALBASE);
    CHECK_EQ(pal0, 0);
    enter(r, "SCREEN 1:PSET (5,5),3", ms(600));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    CHECK_EQ(columns(r), 40u);
    CHECK_EQ(pixel(r, 5, 5), 3);
    CHECK(reg16(r, PALBASE) >= 0x0400);
    enter(r, "SCREEN 1", ms(400)); // the same mode: the picture stays
    CHECK_EQ(pixel(r, 5, 5), 3);
    enter(r, "SCREEN 2:PSET (5,5)", ms(600));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x05);
    CHECK_EQ(columns(r), 80u);
    CHECK_EQ(pixel(r, 5, 5), 1);
    enter(r, "SCREEN 1", ms(600));
    CHECK_EQ(pixel(r, 5, 5), 0);
    enter(r, "PSET (6,6),2:WIDTH 40", ms(600));
    CHECK_EQ(pixel(r, 6, 6), 2);
    enter(r, "WIDTH 80", ms(600));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x05);
    CHECK_EQ(columns(r), 80u);
    enter(r, "WIDTH 40", ms(600));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    CHECK_EQ(columns(r), 40u);
    enter(r, "SCREEN 1,,1", ms(400));
    CHECK(last_row(r, "Illegal function call") >= 0);
    enter(r, "CLS:WIDTH 40,30", ms(600));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    enter(r, "SCREEN 0", ms(600));
    CHECK_EQ(reg(r, CTRL), 0x03);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x81);
    CHECK_EQ(reg16(r, PALBASE), 0);
    CHECK_EQ(columns(r), 40u);
    enter(r, "LIST", ms(1000));
    CHECK(last_row(r, "10 PRINT \"KEPT\"") >= 0);
    enter(r, "WIDTH 80", ms(1000));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x80);
}

// COLOR on a graphics screen (the PC's COLOR [background][,palette]): SCREEN 1's palette is GW-BASIC's own (PALBASE
// its card memory: answer 10), its groups the console's CGA colours (1: green, red, brown; 2: cyan, magenta, light
// grey; 3: white); COLOR 1,0 makes entry 0 (the backdrop behind each transparent pixel) the console's blue and both
// layers' PALOFS group 1, COLOR ,1 group 2, COLOR ,2 group 1 (an even palette), COLOR 15 the backdrop white; COLOR
// alone, and COLOR 1 in SCREEN 2, Illegal function call (PC-BASIC's); SCREEN 0's COLOR as 7d1's after it. Guards a
// program's background or palette in the wrong colours. Seen failing with the palette's parity taken as zero or
// not (COLOR ,2 gives group 2).
TEST(color_on_a_graphics_screen)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1", ms(600));
    const uint32_t pal = reg16(r, PALBASE) * 256u;
    CHECK(pal >= 0x040000);
    auto entry = [&](unsigned e) { return static_cast<unsigned>(card(r, pal + 2 * e) | card(r, pal + 2 * e + 1) << 8); };
    CHECK_EQ(entry(0), 0x0000u);
    CHECK_EQ(entry(17), 0x00A0u); // green
    CHECK_EQ(entry(18), 0x0A00u); // red
    CHECK_EQ(entry(19), 0x0A50u); // brown
    CHECK_EQ(entry(33), 0x00AAu); // cyan
    CHECK_EQ(entry(34), 0x0A0Au); // magenta
    CHECK_EQ(entry(35), 0x0AAAu); // light grey
    CHECK_EQ(entry(49), 0x0FFFu); // white
    enter(r, "COLOR 1,0", ms(400));
    CHECK_EQ(entry(0), 0x000Au); // blue
    CHECK_EQ(reg(r, L0 + L_PALOFS), 1);
    CHECK_EQ(reg(r, L1 + L_PALOFS), 1);
    enter(r, "COLOR ,1", ms(400));
    CHECK_EQ(reg(r, L0 + L_PALOFS), 2);
    CHECK_EQ(reg(r, L1 + L_PALOFS), 2);
    CHECK_EQ(entry(0), 0x000Au);
    enter(r, "COLOR ,2", ms(400));
    CHECK_EQ(reg(r, L0 + L_PALOFS), 1);
    CHECK_EQ(reg(r, L1 + L_PALOFS), 1);
    enter(r, "COLOR 15", ms(400));
    CHECK_EQ(entry(0), 0x0FFFu);
    CHECK_EQ(reg(r, L0 + L_PALOFS), 1);
    enter(r, "CLS:COLOR", ms(400));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    enter(r, "SCREEN 2:COLOR 1", ms(600));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    enter(r, "SCREEN 0:COLOR 2,1:PRINT \"Q\"", ms(600));
    CHECK_EQ(reg16(r, PALBASE), 0);
    CHECK_EQ(attr(r, 0, 0), 0x12);
}

// POINT reads what was drawn (gwbasic/gfx7, against PC-BASIC's printed values): both pixels of a SCREEN 1 byte, a
// line's, a blank one, the last; -1 off every edge; the graphics cursor kept across a POINT (a LINE from the
// PSET before it); SCREEN 2's bits 7 and 0 of a byte, the last pixel, a blank one, and -1 past the right edge.
// Guards a game's collision test by POINT reading the wrong pixel. Seen failing with READC's nibble taken from the
// other half.
TEST(point_reads_what_was_drawn)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx7", {"gfx7"}, ms(8000)) >= 0);
    std::vector<std::string> want;
    std::string l;
    for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/gfx7.out"))
        if (c == '\n')
        {
            while (!l.empty() && (l.back() == ' ' || l.back() == '\r'))
                l.pop_back();
            want.push_back(l);
            l.clear();
        }
        else
            l += c;
    REQUIRE(want.size() == 4u);
    for (unsigned i = 0; i < 4; ++i)
        CHECK_EQ(line(r, i), want[i]);
}

// RUN and CLS centre the graphics cursor (GENGRP.ASM's GRPRST at CLEARC, GRPINI at SCNCLR), against PC-BASIC's pixels
// (gwbasic/gfx8 in SCREEN 1, gfx9 in SCREEN 2): a PSET, then RUN, then LINE -(0,0) starts at (160,100) (320,100);
// a PSET, then CLS, then LINE -(319,0); a PSET, then CLEAR, then LINE -(319,199). Guards a program's first relative
// line starting where the last one ended. Seen failing with CLEARC's GRPRST call left out.
TEST(run_and_cls_centre_the_graphics_cursor)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx8", {"gfx8"}, ms(8000)) >= 0);
        CHECK_EQ(compare(r, "gfx8"), 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx9", {"gfx9"}, ms(8000)) >= 0);
    CHECK_EQ(compare(r, "gfx9"), 0u);
}

// LIFEGFX (basic/lifegfx.bas, the owner's: SCREEN 1, COLOR 0,1, a LINE ...,BF a cell, LOCATE and PRINT on the
// graphics screen) runs: pattern 3, the R-pentomino, typed at its INPUT, line 280 made IF GN<2 THEN 210 and 285
// END in the copy put on M:, so it stops after its second generation in SCREEN 1. Its 40 x 24 cells on the card
// match the test's own Life of the same board (wrapped at its edges): each live cell a 7 x 7 box in colour 3 with
// its 8th row and column clear, every other pixel 0, the status row's text "Gen 2   Pop n" over the graphics' row 0.
// A generation's time, from the status row's "Gen 1" to its "Gen 2", is printed (not gated). Guards the program the
// owner named not running. Seen failing with BOXLOP's count one row short (DOBOXF's INC BX left out).
TEST(lifegfx_draws_the_board)
{
    CcpRig r;
    REQUIRE(r.ok);
    std::string prog;
    for (const std::string &l : {std::string(EMU_GWBASIC_PROGRAMS "/../../../../basic/lifegfx.bas")})
    {
        std::string t = read_text(l), cur;
        for (const char c : t)
            if (c == '\n')
            {
                if (cur.rfind("280 ", 0) == 0)
                    cur = "280 IF GN<2 THEN 210\r\n285 END";
                if (!cur.empty())
                    prog += cur + "\r\n";
                cur.clear();
            }
            else if (c != '\r')
                cur += c;
    }
    REQUIRE(prog.find("285 END") != std::string::npos);
    const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
    REQUIRE(!com.empty() && !ovl.empty());
    r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}, {0, "LIFEGFX BAS", bytes(prog)}});
    const size_t from = r.out.size();
    r.type("m:gwbasic m:lifegfx\r");
    CHECK(r.until("m:gwbasic m:lifegfx\r\n", ms(2000), from));
    REQUIRE(until_row(r, "Pattern", ms(6000)) >= 0);
    enter(r, "3", ms(100));
    // The test's own Life: the R-pentomino (lifegfx.bas's DATA 2110), two generations on the 40 x 24 torus.
    bool g[42][26] = {};
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{21, 12}, {22, 12}, {20, 13}, {21, 13}, {21, 14}})
        g[x][y] = true;
    for (int gen = 0; gen < 2; ++gen)
    {
        bool n[42][26] = {};
        for (int y = 1; y <= 24; ++y)
            for (int x = 1; x <= 40; ++x)
            {
                int c = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy)
                            c += g[(x + dx + 39) % 40 + 1][(y + dy + 23) % 24 + 1];
                n[x][y] = c == 3 || (c == 2 && g[x][y]);
            }
        std::copy(&n[0][0], &n[0][0] + 42 * 26, &g[0][0]);
    }
    unsigned pop = 0;
    for (int y = 1; y <= 24; ++y)
        for (int x = 1; x <= 40; ++x)
            pop += g[x][y];
    REQUIRE(until_row(r, "Gen 1", ms(60000)) == 0);
    const uint64_t g1 = r.cpu->cycles();
    REQUIRE(until_row(r, "Gen 2", ms(60000)) == 0);
    const uint64_t g2 = r.cpu->cycles();
    REQUIRE(until_row(r, "Ok", ms(4000)) > 0);
    std::printf("  a generation of LIFEGFX (the R-pentomino): %llu cycles, %.1f s\n",
                static_cast<unsigned long long>(g2 - g1), static_cast<double>(g2 - g1) / 7.16e6);
    CHECK_EQ(line(r, 0), "Gen 2   Pop " + std::to_string(pop));
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    unsigned bad = 0;
    for (unsigned y = 0; y < 200; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const unsigned X = x / 8 + 1, Y = y / 8;
            const int want = Y >= 1 && x % 8 < 7 && y % 8 < 7 && g[X][Y] ? 3 : 0;
            if (pixel(r, x, y) != want && ++bad <= 8)
                std::printf("  (%u,%u): %d, want %d\n", x, y, pixel(r, x, y), want);
        }
    CHECK_EQ(bad, 0u);
}

int main()
{
    return run_all();
}
