/**
 * @file test_gwbasic_gfx.cpp
 * @brief GW-BASIC's graphics, phases 7e1-7e3 (hw/oodos.md, "The plan, phase 7": block P5's SCREEN 1 and 2, PSET,
 *        PRESET, POINT, LINE with STEP, B and BF, COLOR, WIDTH and CLS on a graphics screen; 7e2's CIRCLE, PAINT and
 *        DRAW; 7e3's GET and PUT), run from OODOS's CCP on backplane.machine.
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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <tuple>
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

    /// A link's labels (ld65's -Ln): the resident core's (gwbasic.lbl) or a block's (p5.lbl).
    std::map<std::string, uint16_t> lbl_map(const std::string &lbl = "gwbasic.lbl")
    {
        std::map<std::string, uint16_t> out;
        std::ifstream f(BUILD + "/" + lbl);
        std::string al, addr, name;
        while (f >> al >> addr >> name)
            if (al == "al" && name.size() > 1 && name[0] == '.')
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(addr, nullptr, 16));
        return out;
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

    /// The test's own reference of the 1983 source's drawing (7e2: the owner's answer 3, CIRCLE's ring the source's):
    /// a screen of pixels, LINE's walk (GENGRP.ASM's DOGRPH with GWSTS.ASM's LINLP3: from the smaller y, the sum from
    /// half the major delta, only the pixels on the screen set, the true clipping) and CIRCLE (ADVGRP.ASM's CIRCLE:
    /// the half-unit loop, CPLOT8's octants, SCALE's rounding, CGTCNT's point counts), written from the source's
    /// meaning, as G9's Life is written from LIFEGFX's.
    struct Ref
    {
        int mode, w, ax, ay, atr;
        std::map<std::pair<int, int>, int> px;
        explicit Ref(int m) : mode(m), w(m == 1 ? 320 : 640), ax(w / 2), ay(100), atr(m == 1 ? 3 : 1) {}
        bool on(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < 200; }
        void set(int x, int y, int c)
        {
            if (on(x, y))
                px[{x, y}] = c;
        }
        int at(int x, int y) const
        {
            const auto i = px.find({x, y});
            return i == px.end() ? 0 : i->second;
        }
        /// The rows as dump() gives them.
        std::vector<std::string> rows() const
        {
            const unsigned bits = mode == 1 ? 2 : 1, per = 8 / bits;
            std::vector<std::string> out;
            for (int y = 0; y < 200; ++y)
            {
                std::string s;
                for (unsigned b = 0; b < 80; ++b)
                {
                    int v = 0;
                    for (unsigned i = 0; i < per; ++i)
                        v = v << bits | at(static_cast<int>(b * per + i), y);
                    char h[3];
                    std::snprintf(h, sizeof h, "%02X", v);
                    s += h;
                }
                out.push_back(s);
            }
            return out;
        }
        void line(int x1, int y1, int x2, int y2, int c)
        {
            if (y2 < y1)
                std::swap(x1, x2), std::swap(y1, y2);
            const int dy = y2 - y1, xs = x2 >= x1 ? 1 : -1, dx = std::abs(x2 - x1);
            const bool ymaj = dy >= dx;
            const int maj = ymaj ? dy : dx, mn = ymaj ? dx : dy;
            int sum = maj / 2, x = x1, y = y1;
            for (int i = 0; i <= maj; ++i)
            {
                set(x, y, c);
                sum += mn;
                if (sum >= maj)
                {
                    sum -= maj;
                    if (ymaj)
                        x += xs;
                    else
                        ++y;
                }
                if (ymaj)
                    ++y;
                else
                    x += xs;
            }
        }
        static int cint(float v) { return static_cast<int>(std::lround(v)); }
        static int s16(unsigned v) { v &= 0xFFFF; return v & 0x8000 ? static_cast<int>(v) - 0x10000 : static_cast<int>(v); }
        static unsigned scale(unsigned a, unsigned v)
        {
            if ((a & 0xFF) == 0)
                return a >> 8 ? v : 0;
            return (((a & 0xFF) * (v & 0xFFFF) + 128) >> 8) & 0xFFFF;
        }
        /// CIRCLE (cx,cy),r,c,start,end,aspect: NAN for an argument not given (c -1: the default).
        void circle(int cx, int cy, int r, int c = -1, float start = NAN, float end = NAN, float aspect = NAN)
        {
            if (c < 0)
                c = mode == 1 ? 3 : 1;
            c = std::min(c, mode == 1 ? 3 : 1);
            ax = cx, ay = cy;
            const int npts = cint(static_cast<float>(r) * 0.70710677f);
            unsigned clinef = 0, csclxy = 0;
            auto cnt = [&](float v, unsigned bit) {
                if (v < 0) // (-0 is 0: no line, CGTCNT's exponent test)
                    v = -v, clinef |= bit;
                const float f = v * 0.15915494f;
                return static_cast<unsigned>(cint(f * static_cast<float>(static_cast<int16_t>((npts * 8) & 0xFFFF)))) & 0xFFFF;
            };
            const unsigned sc = std::isnan(start) ? 0 : cnt(start, 1), ec = std::isnan(end) ? 0xFFFF : cnt(end, 128);
            unsigned plotf, cst, cen;
            if (ec >= sc)
                plotf = 0, cst = sc, cen = ec;
            else
            {
                plotf = 0xFF, cst = ec, cen = sc;
                unsigned a = clinef;
                a = ((a << 1) | (a >> 7)) & 0xFF;
                a = ((a << 1) | (a >> 7)) & 0xFF;
                a |= clinef;
                clinef = ((a >> 1) | ((a & 1) << 7)) & 0xFF;
            }
            unsigned asp;
            if (std::isnan(aspect))
                asp = mode == 1 ? 213 : 107;
            else
            {
                float v = aspect;
                if (v > 1)
                    csclxy = 1, v = 1 / v;
                asp = static_cast<unsigned>(cint(v * 256)) & 0xFFFF;
            }
            auto cplscx = [&](unsigned v) { // (DE, HL)
                const unsigned sv = scale(asp, v);
                return csclxy ? std::pair<unsigned, unsigned>{v, sv} : std::pair<unsigned, unsigned>{sv, v};
            };
            auto plot = [&](unsigned hl, unsigned de, unsigned pc) {
                auto pt = [&] { set(cx + s16(hl), cy + s16(de), c); };
                auto ln = [&] { line(cx + s16(hl), cy + s16(de), cx, cy, c); };
                if (cst == pc)
                    return (clinef & 1) ? ln() : pt();
                if (cst > pc)
                    return plotf ? pt() : void();
                if (cen == pc)
                    return (clinef & 128) ? ln() : pt();
                if (cen > pc)
                    return plotf ? void() : pt();
                if (plotf)
                    pt();
            };
            // CPLOT8, as the source has it (the model's order): HL x, DE y on entry.
            auto oct8 = [&](unsigned x, unsigned y) {
                unsigned cpcnt = y, cpcnt8 = 0;
                auto [de1, hl1] = cplscx(y);
                unsigned cxoff = hl1;
                unsigned hl = de1, de = x;
                const unsigned save = hl;
                auto [de2, hl2] = cplscx(de);
                unsigned cyoff = de2;
                de = (0x10000 - save) & 0xFFFF;
                hl = hl2;
                auto cplot4 = [&](unsigned &h, unsigned &d) {
                    for (int k = 0; k < 4; ++k)
                    {
                        const unsigned d8 = cpcnt8;
                        cpcnt8 = (npts * 2 + d8) & 0xFFFF;
                        plot(h, d, (cpcnt + d8) & 0xFFFF);
                        if (k == 3)
                            return;
                        const unsigned nh = (0x10000 - cxoff) & 0xFFFF, nd = (0x10000 - cyoff) & 0xFFFF;
                        cxoff = h, cyoff = d, h = nh, d = nd;
                    }
                };
                cplot4(hl, de);
                cpcnt8 = static_cast<unsigned>(npts) & 0xFFFF;
                cpcnt = (npts - cpcnt) & 0xFFFF;
                cxoff = (0x10000 - cxoff) & 0xFFFF;
                de = (0x10000 - de) & 0xFFFF;
                cplot4(hl, de);
            };
            unsigned y = 0, x = (2u * static_cast<unsigned>(r)) & 0xFFFF;
            int sum = 0;
            for (;;)
            {
                if ((y & 1) == 0)
                {
                    oct8(((x + 1) & 0xFFFF) >> 1, ((y + 1) & 0xFFFF) >> 1);
                    if (y >= x)
                        break;
                }
                sum = static_cast<int16_t>((sum + 2 * static_cast<int>(y) + 1) & 0xFFFF);
                if (sum >= 0)
                {
                    sum = static_cast<int16_t>((sum - (2 * static_cast<int>(x) - 1)) & 0xFFFF);
                    x = (x - 1) & 0xFFFF;
                }
                ++y;
            }
        }
    };

    /// Every lit pixel of `a` within one pixel (either way, diagonals too) of a lit one of `b`: the count that isn't.
    unsigned far_from(const std::vector<std::string> &a, const std::vector<std::string> &b, bool mono)
    {
        auto lit = [&](const std::vector<std::string> &rows, int x, int y) {
            if (x < 0 || y < 0 || y >= 200 || x >= (mono ? 640 : 320))
                return false;
            const unsigned per = mono ? 8 : 4, bits = mono ? 1 : 2;
            const unsigned byte = static_cast<unsigned>(std::stoul(rows[static_cast<size_t>(y)].substr(static_cast<size_t>(x) / per * 2, 2), nullptr, 16));
            return ((byte >> ((per - 1 - static_cast<unsigned>(x) % per) * bits)) & ((1u << bits) - 1)) != 0;
        };
        unsigned n = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < (mono ? 640 : 320); ++x)
                if (lit(a, x, y))
                {
                    bool near = false;
                    for (int dy = -1; dy <= 1 && !near; ++dy)
                        for (int dx = -1; dx <= 1 && !near; ++dx)
                            near = lit(b, x + dx, y + dy);
                    if (!near && ++n <= 8)
                        std::printf("  (%d,%d) has nothing within a pixel\n", x, y);
                }
        return n;
    }
    /// The card's rows against the reference's: each row that differs printed (the first 8), and the count.
    unsigned compare_ref(CcpRig &r, const Ref &ref)
    {
        const std::vector<std::string> want = ref.rows(), got = dump(r);
        unsigned bad = 0;
        for (size_t y = 0; y < 200; ++y)
            if (got[y] != want[y] && ++bad <= 8)
                std::printf("  row %zu:\n    want %s\n    got  %s\n", y, want[y].c_str(), got[y].c_str());
        if (bad)
            std::printf("  %u rows of 200 differ from the reference\n", bad);
        return bad;
    }
} // namespace

// SCREEN 1's pixels against PC-BASIC's (gwbasic/gfx1): PSET and PRESET with and without an attribute (one past the
// mode's last colour is the last, PC-BASIC's: 7 and 6 are 3), STEP; LINE in all eight octants, steep and shallow, from either end;
// -(x,y), -STEP, STEP(x,y)-STEP; B and BF with their corners either way, a 1-pixel box of each, the default and
// colour 0; BF boxes off every edge and across a corner, and wholly past each edge (nothing drawn: review round 1,
// F1), where PC-BASIC's fill is the IBM manual's (p. 158: the B box's rectangle, clipped); PSET off the screen
// (nothing); the frame's bank boundary (tile row 12, column 32) crossed by a line and a box. And the layer as
// PC-BASIC's screen shows it: VSCROLL 236, 20 layer lines above the 200. Lines and B boxes off the screen are
// lines_off_the_screen_are_clipped_on_their_own_slope's (the IBM spec's fix-up, A6: PC-BASIC's clipping is no longer
// the expectation). Guards a picture drawn in the wrong place or colour, or a box clipped wrong. Seen failing with
// (a) RIGHTC's step across a tile column 4, not 32 - 3, (b) the left pixel in a byte's low 4 bits, (c) SCALXL
// bringing an x below 0 to 0, not -1 (7e1, its lines off the screen since moved), (d) DOBOXF without BOXOFF's early
// exit (a box past an edge filling it), and (e) SETATR taking an attribute's low bits, not the last colour.
TEST(screen_1_pixels_match_pc_basic)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx1", {"gfx1"}, ms(8000)) >= 0);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x22);
    CHECK_EQ(reg16(r, L0 + L_VSCROLL), 236);
    CHECK_EQ(compare(r, "gfx1"), 0u);
}

// SCREEN 2's pixels against PC-BASIC's (gwbasic/gfx2): the same set at 640 across, the attributes past 1 made 1
// (PC-BASIC's: 2 and 3 are 1), and the rows past the page's first 16 KB (row 102's pair crosses it; a line along it, one across
// rows 103-104, a box over rows 96-110 and a line down the whole screen); every row shown twice, the mono layer's
// VSCROLL 440; its BF boxes off the screen as G1's (its lines and B boxes off it are now
// lines_off_the_screen_are_clipped_on_their_own_slope's). Guards a row shown once (half-height lines), or the
// picture's bottom half in the wrong card bank. Seen failing with (a) SETC writing row 2y only, (b) DOWNC not mapping
// the next bank (the cached bank kept), and (c) LONSCR taking an x past the last as on the screen (its pixel plotted
// into the next row; 7e1, its lines off the screen since moved).
TEST(screen_2_pixels_match_pc_basic)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx2", {"gfx2"}, ms(8000)) >= 0);
    CHECK_EQ(reg(r, L0 + L_CONFIG), 0x05);
    CHECK_EQ(reg16(r, L0 + L_VSCROLL), 440);
    CHECK_EQ(compare(r, "gfx2"), 0u);
}

namespace
{
    /// A LINE statement's figure: a line, or with `box` the B box on its corners.
    struct Seg
    {
        long x1, y1, x2, y2;
        int c;
        bool box = false;
    };

    /// The test's own reference of a clipped line (the owner's G2: the IBM manual's p. 159, "clipped", taken in its
    /// plain meaning): every point of the given line, walked as GENGRP.ASM's DOGRPH and GWSTS.ASM's LINLP3 walk it
    /// (from the end with the smaller y, or the second end if they're level, along the major axis, the ties y's, the
    /// sum from half the major delta and a minor step whenever it reaches the major delta), set in `pic` where it lies
    /// on the w x 200 screen. So a line keeps its own slope across the edge.
    void ref_line(std::vector<int> &pic, long w, long x1, long y1, long x2, long y2, int c)
    {
        long x = x2, y = y2, ex = x1, ey = y1;
        if (y2 > y1)
        {
            x = x1;
            y = y1;
            ex = x2;
            ey = y2;
        }
        const long dx = std::labs(ex - x), dy = ey - y, xs = ex < x ? -1 : 1;
        const bool ymaj = dy >= dx;
        const long maj = ymaj ? dy : dx, mn = ymaj ? dx : dy;
        long sum = maj / 2;
        for (long n = 0; n <= maj; ++n)
        {
            if (x >= 0 && x < w && y >= 0 && y < 200)
                pic[static_cast<size_t>(y * w + x)] = c;
            sum += mn;
            if (sum >= maj)
            {
                sum -= maj;
                if (ymaj)
                    x += xs;
                else
                    ++y;
            }
            if (ymaj)
                ++y;
            else
                x += xs;
        }
    }
    void ref_seg(std::vector<int> &pic, long w, const Seg &g)
    {
        if (!g.box)
            return ref_line(pic, w, g.x1, g.y1, g.x2, g.y2, g.c);
        ref_line(pic, w, g.x1, g.y2, g.x2, g.y2, g.c);
        ref_line(pic, w, g.x1, g.y1, g.x2, g.y1, g.c);
        ref_line(pic, w, g.x2, g.y1, g.x2, g.y2, g.c);
        ref_line(pic, w, g.x1, g.y1, g.x1, g.y2, g.c);
    }

    /// SCREEN `mode`'s figures run as a program (CLIP.BAS on M:, three statements a line) and the card's whole screen
    /// against the reference: the pixels that differ (the first 8 printed).
    unsigned clipped(int mode, const std::vector<Seg> &segs, uint64_t budget = ms(8000))
    {
        const long w = mode == 1 ? 320 : 640;
        std::string prog = "10 SCREEN " + std::to_string(mode) + "\r\n";
        std::vector<int> pic(static_cast<size_t>(w * 200), 0);
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const Seg &g = segs[i];
            char st[96];
            std::snprintf(st, sizeof st, "LINE (%ld,%ld)-(%ld,%ld),%d%s", g.x1, g.y1, g.x2, g.y2, g.c, g.box ? ",B" : "");
            prog += (i % 3 ? ":" : std::to_string(20 + 10 * i / 3) + " ") + std::string(st);
            if (i % 3 == 2 || i + 1 == segs.size())
                prog += "\r\n";
            ref_seg(pic, w, g);
        }
        CcpRig r;
        CHECK(r.ok);
        if (!r.ok)
            return ~0u;
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}, {0, "CLIP    BAS", bytes(prog)}});
        const size_t from = r.out.size();
        r.type("m:gwbasic m:clip\r");
        CHECK(r.until("m:gwbasic m:clip\r\n", ms(2000), from));
        CHECK(until_row(r, "Ok", budget) >= 0);
        unsigned bad = 0;
        for (long y = 0; y < 200; ++y)
            for (long x = 0; x < w; ++x)
            {
                const int got = pixel(r, static_cast<unsigned>(x), static_cast<unsigned>(y)),
                          want = pic[static_cast<size_t>(y * w + x)];
                if (got != want && ++bad <= 8)
                    std::printf("  SCREEN %d (%ld,%ld): %d, want %d\n", mode, x, y, got, want);
            }
        if (bad)
            std::printf("  SCREEN %d: %u pixels differ\n", mode, bad);
        return bad;
    }
} // namespace

// A line with an end off the screen is clipped on its own slope (the owner's G2, the IBM spec's fix-up, A6: the IBM
// manual's p. 159 has out-of-range coordinates clipped; the source's SCALXY truncation is an OEM placeholder, G1), and
// PC-BASIC, whose clipping moves the far end, is no longer the expectation for these: each picture against the
// test's own reference (ref_line: the whole given line walked as LINLP3 walks it, its points on the screen set). In
// SCREEN 1 and 2: 7e1's lines off every edge, across the corners and wholly off (once gfx1's and gfx2's), B boxes
// past each edge and corner, lines whose far ends lie at +-32,000 (their deltas past 32,767: the sum past 16 bits)
// in all four directions, a line from past the right edge leftwards, one that passes outside a corner, and LINE
// (-32768,-32768)-(32767,32767). And a pixel half a step off the line goes the source's way (LINLP3's sum from half the
// major delta, stepped at it: the owner's ruling 2 of 7e1), so LINE (300,10)-(310,50) in SCREEN 2 has x 300 on rows
// 10 and 11 only. Guards a picture whose lines bend or shift where they meet an edge. Seen failing with (a) LSKIP's
// minor move left out (a line from far off shifted), (b) LINCLP's sum without its 17th bit (the long lines), and (c)
// XDELT's sign by N alone (a delta past 32,767 taken the wrong way).
TEST(lines_off_the_screen_are_clipped_on_their_own_slope)
{
    const std::vector<Seg> one = {
        {161, 100, -1000, 110, 1}, {161, 100, 1000, 95, 2}, {160, 101, 170, -1000, 3}, {160, 101, 150, 1000, 1},
        {21, 60, -30, 30, 2}, {30, 40, -9, 15, 3}, {300, 60, 345, 90, 1}, {310, 30, 339, 47, 2},
        {100, 20, 85, -21, 3}, {120, 15, 150, -17, 1}, {100, 185, 81, 230, 2}, {200, 180, 233, 215, 3},
        {-10, 5, 5, -10, 1}, {-20, 30, 25, -15, 2}, {330, 180, 290, 230, 1}, {10, -10, 20, -20, 3},
        {-5, 50, -15, 60, 3}, {400, 10, 300, 10, 2}, {300, 10, 400, 60, 1, true}, {-30, 150, 20, 180, 3, true},
        {100, -20, 140, 5, 2, true}, {250, 190, 280, 230, 1, true}, {-50, -50, -10, -10, 3, true},
        {-100, 0, 100, 100, 2}, {-32000, 50, 32000, 60, 3}, {100, -32000, 110, 32000, 1},
        {32000, 120, -32000, 130, 2}, {200, 32000, 210, -32000, 3}, {1000, 10, -1000, 190, 1},
        {-100, 300, 400, 250, 2}, {-32768, -32768, 32767, 32767, 3}};
    CHECK_EQ(clipped(1, one), 0u);
    const std::vector<Seg> two = {
        {321, 100, -1000, 110, 1}, {321, 100, 1000, 95, 1}, {320, 101, 330, -1000, 1}, {320, 101, 310, 1000, 1},
        {40, 60, -61, 30, 1}, {60, 40, -19, 15, 1}, {620, 60, 690, 90, 1}, {630, 30, 679, 47, 1},
        {200, 20, 185, -21, 1}, {240, 15, 300, -17, 1}, {200, 185, 181, 230, 1}, {400, 180, 433, 215, 1},
        {-10, 5, 5, -10, 1}, {-40, 30, 50, -15, 1}, {650, 180, 590, 230, 1}, {10, -10, 20, -20, 1},
        {-5, 50, -15, 60, 1}, {800, 10, 600, 10, 1}, {600, 10, 700, 60, 1, true}, {-30, 150, 20, 180, 1, true},
        {100, -20, 140, 5, 1, true}, {550, 190, 580, 230, 1, true}, {-50, -50, -10, -10, 1, true},
        {-32000, 70, 32000, 80, 1}, {450, -32000, 460, 32000, 1}, {32000, 140, -32000, 150, 1},
        {2000, 5, -2000, 195, 1}, {-32768, -32768, 32767, 32767, 1}, {300, 10, 310, 50, 1}};
    CHECK_EQ(clipped(2, two), 0u);
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 2:LINE (300,10)-(310,50)", ms(1000));
    CHECK_EQ(pixel(r, 300, 10), 1);
    CHECK_EQ(pixel(r, 300, 11), 1);
    CHECK_EQ(pixel(r, 300, 12), 0);
    CHECK_EQ(pixel(r, 301, 12), 1);
    CHECK_EQ(pixel(r, 310, 48), 1);
    CHECK_EQ(pixel(r, 310, 50), 1);
}

// POINT and SCREEN( in a graphics mode, as the IBM manual has them (the IBM spec's fix-up, A7 and A8): POINT's
// coordinates are absolute (p. 258), so POINT STEP(1,1) is Syntax error (the source's SCAND takes STEP), as is
// POINT(STEP(1,1)); SCREEN(row,col) of a blank cell with a pixel not 0 among the 8 x 8 under it is 0 (p. 311: graphics
// there), of a blank cell over none 32, and of a character the character (the text is over the graphics here, the
// owner's answer 5); SCREEN's z is Illegal function call in a graphics mode (p. 310: text mode only), and still the
// attribute in SCREEN 0. Guards a program's test of the screen for graphics reading a blank. Seen failing with (a)
// SCRNFP's far call to GCELL left out (32 for a cell over graphics), (b) SCRNFP taking z in a graphics mode, and (c)
// POINTP taking STEP.
TEST(point_and_screen_in_a_graphics_mode_are_the_manual_s)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:PSET (17,9),2:PSET (39,15),1:LOCATE 2,6:PRINT \"A\";:PSET (44,12),3", ms(1500));
    enter(r, "A=SCREEN(2,3):B=SCREEN(2,4):C=SCREEN(2,5):D=SCREEN(2,6):LOCATE 10,1:PRINT A;B;C;D", ms(1500));
    CHECK_EQ(line(r, 9), std::string(" 0  32  0  65"));
    enter(r, "CLS:PRINT POINT STEP(1,1)", ms(800));
    CHECK_EQ(last_row(r, "Syntax error"), 0);
    enter(r, "CLS:PRINT POINT(STEP(1,1))", ms(800));
    CHECK_EQ(last_row(r, "Syntax error"), 0);
    enter(r, "CLS:PRINT POINT(1,1)", ms(800));
    CHECK_EQ(line(r, 0), std::string(" 0"));
    enter(r, "CLS:PRINT SCREEN(1,1,1)", ms(800));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    enter(r, "CLS:PRINT SCREEN(1,1,0)", ms(800));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    enter(r, "SCREEN 2:PSET (300,100):A=SCREEN(13,38):B=SCREEN(13,39):LOCATE 5,1:PRINT A;B", ms(1500));
    CHECK_EQ(line(r, 4), std::string(" 0  32"));
    enter(r, "SCREEN 0:CLS:PRINT \"Q\";:X=SCREEN(1,1,1):LOCATE 5,1:PRINT X", ms(1500));
    CHECK_EQ(line(r, 4), std::string(" 7"));
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
// alone changes nothing (the IBM manual's p. 44: any parameter may be left out, and leaving it out changes nothing)
// and a third parameter is Illegal function call (p. 43's form has two), as is COLOR 1 in SCREEN 2 (p. 43); SCREEN
// 0's COLOR as 7d1's after it. Guards a program's background or palette in the wrong colours. Seen failing with the
// palette's parity taken as zero or not (COLOR ,2 gives group 2), (the IBM spec's fix-up, A3 and A4) with GCOLOR
// refusing no parameters and taking a third, and with GTPRMS leaving the flags past its count (COLOR 1,1,1's third
// taken by the SCREEN 2 after it as an apage: Illegal function call, found by the fix-up).
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
    CHECK_EQ(last_row(r, "Illegal function call"), -1);
    CHECK_EQ(entry(0), 0x0FFFu);
    CHECK_EQ(reg(r, L0 + L_PALOFS), 1);
    enter(r, "CLS:COLOR 1,1,1", ms(400));
    CHECK_EQ(last_row(r, "Illegal function call"), 0);
    CHECK_EQ(entry(0), 0x0FFFu);
    CHECK_EQ(reg(r, L0 + L_PALOFS), 1);
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

// RUN and CLS centre the graphics cursor (GENGRP.ASM's GRPRST at CLEARC, P5's GCLS), against PC-BASIC's pixels
// (gwbasic/gfx8 in SCREEN 1, gfx9 in SCREEN 2): a PSET, then RUN, then LINE -(0,0) starts at (160,100) (320,100);
// a PSET, then CLS, then LINE -(319,0); a PSET, then CLEAR, then LINE -(319,199). And so do CLS 2 and PRINT
// CHR$(12), by hand (the source's SCNCLR calls GRPINI; the IBM manual's p. 36 centres it at CLS and is silent on
// the rest; the IBM spec's fix-up, A1, undoing 7e1's F2): PSET (10,10),1: CLS 2: PSET STEP(0,0),2 sets (160,100),
// (10,10) kept; PSET (20,20),1: PRINT CHR$(12);: PSET STEP(1,1),3 sets (161,101). Guards a program's relative
// drawing starting in the wrong place. Seen failing with CLEARC's GRPRST call left out, and (A1) with SCNCLR's
// GRPINI left out.
TEST(run_and_cls_centre_the_graphics_cursor)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx8", {"gfx8"}, ms(8000)) >= 0);
        CHECK_EQ(compare(r, "gfx8"), 0u);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx9", {"gfx9"}, ms(8000)) >= 0);
        CHECK_EQ(compare(r, "gfx9"), 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:PSET (10,10),1:CLS 2:PSET STEP(0,0),2", ms(1500));
    CHECK_EQ(pixel(r, 10, 10), 1);
    CHECK_EQ(pixel(r, 160, 100), 2);
    enter(r, "PSET (20,20),1:PRINT CHR$(12);:PSET STEP(1,1),3", ms(1500));
    CHECK_EQ(pixel(r, 21, 21), 0);
    CHECK_EQ(pixel(r, 161, 101), 3);
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

// G12 (7e2): CIRCLE's ring is the 1983 source's (ADVGRP.ASM's half-unit loop and octants, the owner's answer 3: the
// IBM manual is silent on the pixels), exact against the test's own reference of it (Ref::circle), and every lit pixel
// within one pixel of one of PC-BASIC's (gwbasic/gfx12, SCREEN 1, and gfx13, SCREEN 2: their .out, by oracle.sh),
// both ways: the default aspect (213 and 107, the manual's 5/6 and 5/12, the owner's G8), aspects 1, 0.3, 0.5, 1.7
// and 2.5 (past 1, x scaled), radii 0-100, circles across each edge and wholly off it, STEP's centre. Guards a circle
// drawn the wrong shape or size, an octant missing or mirrored. Seen failing with (a) SCAL2 without its + 128 (the
// rings a pixel in) and (b) CPLSCX not swapping for an aspect over 1 (the 1.7 and 2.5 circles squashed, not tall).
TEST(circle_pixels_match_the_source)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx12", {"gfx12"}, ms(8000)) >= 0);
        Ref ref(1);
        ref.circle(160, 100, 50, 3), ref.circle(160, 100, 100, 1);
        ref.circle(50, 45, 30, 1, NAN, NAN, 1), ref.circle(270, 45, 30, 2, NAN, NAN, .3f);
        ref.circle(50, 150, 25, 2, NAN, NAN, .5f), ref.circle(270, 150, 25, 3, NAN, NAN, 1.7f);
        ref.circle(160, 100, 20, 2, NAN, NAN, 2.5f);
        for (int R = 0; R <= 12; R += 3)
            ref.circle(100 + R * 9, 190, R, 3);
        ref.circle(230, 190, 1, 1), ref.circle(240, 190, 2, 2);
        ref.circle(0, 100, 40, 1), ref.circle(319, 15, 30, 2), ref.circle(160, -10, 25, 3), ref.circle(300, 199, 35, 2);
        ref.circle(-200, -200, 20, 3), ref.circle(500, 100, 30, 1), ref.circle(100, 40, 12, 3);
        CHECK_EQ(compare_ref(r, ref), 0u);
        const std::vector<std::string> pc = expected("gfx12"), got = dump(r);
        CHECK_EQ(far_from(got, pc, false), 0u);
        CHECK_EQ(far_from(pc, got, false), 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx13", {"gfx13"}, ms(8000)) >= 0);
    Ref ref(2);
    ref.circle(320, 100, 50), ref.circle(320, 100, 100);
    ref.circle(80, 45, 30, 1, NAN, NAN, 1), ref.circle(560, 45, 60, 1, NAN, NAN, .3f);
    ref.circle(100, 150, 40, 1, NAN, NAN, .5f), ref.circle(540, 150, 25, 1, NAN, NAN, 1.7f);
    ref.circle(320, 100, 20, 1, NAN, NAN, 2.5f);
    for (int R = 0; R <= 12; R += 3)
        ref.circle(200 + R * 12, 190, R);
    ref.circle(420, 190, 1), ref.circle(440, 190, 2);
    ref.circle(0, 100, 40), ref.circle(639, 15, 30), ref.circle(320, -15, 60), ref.circle(600, 199, 35);
    ref.circle(-200, -200, 20), ref.circle(900, 100, 30);
    CHECK_EQ(compare_ref(r, ref), 0u);
    const std::vector<std::string> pc = expected("gfx13"), got = dump(r);
    CHECK_EQ(far_from(got, pc, true), 0u);
    CHECK_EQ(far_from(pc, got, true), 0u);
}

// G13 (7e2): CIRCLE's arcs and radius lines, the source's (the owner's G7: by point count, CGTCNT's), against the
// reference and within one pixel of PC-BASIC's (two for the radius lines: the source's ends are 1-4 degrees from
// the true angle; PC-BASIC, the oracle only, compared outside two boxes where it parts from the source by more)
// (gwbasic/gfx14): start below and above the end (the wrap: the arc's complement's ends), negative
// ends (a line to the centre), 0 to 2 x PI, -0, an end omitted, an arc off the screen; and LINE -( ) after a CIRCLE
// starts at its centre (the manual's p. 31: the last point referenced is the centre). Guards a pie chart's slices at
// the wrong angles, a radius line missing, the next relative line starting elsewhere. Seen failing with (a) the
// CLINEF bits not swapped when start > end (the line at the other end) and (b) CNBTWN's polarity inverted (the
// arc's complement drawn).
TEST(circle_arcs_and_radius_lines)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx14", {"gfx14"}, ms(8000)) >= 0);
    Ref ref(1);
    ref.circle(60, 50, 40, 1, 0, 1.5708f), ref.line(60, 50, 10, 10, 2);
    ref.circle(160, 50, 40, 2, 1, 4), ref.line(160, 50, 140, 5, 3);
    ref.circle(260, 50, 40, 3, -.5f, -2), ref.line(260, 50, 310, 5, 1);
    ref.circle(60, 150, 40, 1, -1, 3), ref.line(60, 150, 5, 195, 3);
    ref.circle(160, 150, 30, 2, 4, 1), ref.circle(160, 150, 40, 3, -5, -.7f);
    ref.circle(260, 150, 35, 1, 0, 6.283185f), ref.circle(260, 150, 20, 2, -0.0f, -3);
    ref.circle(300, 190, 40, 3, -.3f, -2.5f), ref.circle(100, 100, 15, 1, NAN, -2);
    CHECK_EQ(compare_ref(r, ref), 0u);
    const std::vector<std::string> pc = expected("gfx14"), got = dump(r);
    // within two pixels for the radius lines: each pixel of either within 2 of the other's, but in two boxes where
    // PC-BASIC parts from the source by more (an oracle disagreement, recorded: the arc from an omitted start to -2,
    // which PC-BASIC draws past 180 degrees, and the -0.3 radius line, the source's count a few degrees off it)
    auto masked = [](int x, int y) { return (x >= 80 && x <= 120 && y >= 80 && y <= 120) || (x >= 300 && y >= 175); };
    auto far2 = [&](const std::vector<std::string> &a, const std::vector<std::string> &b) {
        unsigned n = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
            {
                auto lit = [&](const std::vector<std::string> &rows, int X, int Y) {
                    if (X < 0 || Y < 0 || X >= 320 || Y >= 200)
                        return false;
                    const unsigned byte = static_cast<unsigned>(std::stoul(rows[static_cast<size_t>(Y)].substr(static_cast<size_t>(X) / 4 * 2, 2), nullptr, 16));
                    return ((byte >> ((3 - static_cast<unsigned>(X) % 4) * 2)) & 3) != 0;
                };
                if (masked(x, y) || !lit(a, x, y))
                    continue;
                bool near = false;
                for (int dy = -2; dy <= 2 && !near; ++dy)
                    for (int dx = -2; dx <= 2 && !near; ++dx)
                        near = lit(b, x + dx, y + dy);
                if (!near && ++n <= 8)
                    std::printf("  (%d,%d) has nothing within two pixels\n", x, y);
            }
        return n;
    };
    CHECK_EQ(far2(got, pc), 0u);
    CHECK_EQ(far2(pc, got), 0u);
}

// CIRCLE's two owner's rulings of 2026-11-19 on 7e2's U2, review round 1's F1 and F1's revision of U2b. A negative
// radius takes the source's loop (the manual is silent: no check, G9), about 13 s of points off the screen, and the
// OO-1's poll in it (every 256 steps: block 1 back, POLKEY) lets Ctrl-C break it: Break within a second, and the next
// line typed runs and reads an array element past $4000 (block 1 in W1). The cursor is the centre at once (the
// source's SCAN1, P5's) and the colour the drawing colour once it is parsed (ATRSCN's): after an error in the angles
// (CIRCLE (50,60),10,1,9: an angle past 2 x PI), the radius (a string) or the colour (300), the LINE -(0,0) after it
// starts at the centre; after DRAW "C1":CIRCLE (100,100),10,2,9 a DRAW move is in 2. Guards a program that can't be
// stopped, and drawing after an error starting elsewhere or in another colour. Seen failing with (a) the poll left
// out (Break only after the loop), (b) the centre's SCAN1 deferred until after CIRCA (the line from (160,100); the
// radius's and colour's errors too) and (c) CIRCC's colour left out (the move in 1).
TEST(circle_breaks_and_keeps_its_centre)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "10 SCREEN 1:DIM X%(6000):X%(6000)=777:CIRCLE (160,100),-5:PRINT \"DONE\"");
    keys(r, "RUN\r");
    r.run(ms(1000));
    const uint64_t t0 = r.cpu->cycles();
    press(r, 0x06, 0xE0);
    const int at = until_row(r, "Break in 10", ms(2000));
    CHECK(at >= 0);
    std::printf("  Break %.2f s after Ctrl-C\n", static_cast<double>(r.cpu->cycles() - t0) / 7.16e6);
    CHECK(r.cpu->cycles() - t0 <= ms(1000));
    CHECK_EQ(last_row(r, "DONE"), -1);
    enter(r, "PRINT X%(6000)", ms(500)); // past $4000: block 1 in W1
    CHECK(last_row(r, " 777") >= 0);
    enter(r, "CLS:CIRCLE (50,60),10,1,9", ms(1000));
    CHECK(last_row(r, "Illegal function call") >= 0);
    enter(r, "LINE -(0,0),2", ms(1000));
    CHECK_EQ(pixel(r, 50, 60), 2);
    CHECK_EQ(pixel(r, 25, 30), 2);
    CHECK_EQ(pixel(r, 160, 100), 0);
    // an error in the radius (a string: Type mismatch) or the colour (300: Illegal function call) leaves the cursor
    // at the centre too (the source's SCAN1 has moved it: F1's revision of the U2b ruling)
    enter(r, "CLS:CIRCLE (60,80),\"X\"", ms(1000));
    CHECK(last_row(r, "Type mismatch") >= 0);
    enter(r, "LINE -(0,0),3", ms(1000));
    CHECK_EQ(pixel(r, 60, 80), 3);
    CHECK_EQ(pixel(r, 30, 40), 3);
    enter(r, "CLS:CIRCLE (90,60),10,300", ms(1000));
    CHECK(last_row(r, "Illegal function call") >= 0);
    enter(r, "LINE -(0,0),1", ms(1000));
    CHECK_EQ(pixel(r, 90, 60), 1);
    CHECK_EQ(pixel(r, 45, 30), 1);
    // and the colour is the attribute as soon as it is parsed (review round 1, F1: the source's ATRSCN)
    enter(r, "CLS:DRAW \"C1\":CIRCLE (100,100),10,2,9", ms(1500));
    CHECK(last_row(r, "Illegal function call") >= 0);
    enter(r, "DRAW \"BM20,20 R10\"", ms(1500));
    CHECK_EQ(pixel(r, 25, 20), 2);
}

// G14 (7e2): PAINT's pixels against PC-BASIC's, exact (gwbasic/gfx15 in SCREEN 1, gfx16 and gfx17 in SCREEN 2): the
// source's algorithm (ADVGRP.ASM's scan lines and queue) on the OEM's SCANR, SCANL, TUPC and TDOWNC (the OO-1's,
// their contracts read from PAINT's use). A boundary colour and the default (the paint's), nested boxes, a one-pixel
// leak, a box crossed by a line of the paint colour (the source's pruning: the far side left), triangles of LINEs, a
// start on the border, a paint of 0 where all is 0, the comb of 160 slots, a whole screen and a ring painted inside
// and out. Guards a fill leaking out of its shape or stopping short of its border. Seen failing with (a) TDOWNC
// moving past row 199 and (b) ENTSLR's already-painted test left out (the pruned box painted below its line).
TEST(paint_matches_pc_basic)
{
    for (const char *name : {"gfx15", "gfx16", "gfx17"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, std::string("m:gwbasic m:") + name, {name}, ms(30000)) >= 0);
        std::printf("  %s:\n", name);
        CHECK_EQ(compare(r, name), 0u);
    }
}

// G15 (7e2): PAINT's queue lives in free memory, STREND to FRETOP (the source's), here above $4000 (gwbasic/gfx18: an
// array from below $4000 to past it, so the queue's entries lie in $4000-$7FFF, where the aperture is while PAINT
// draws: each put and taken with block 1 in W1): the comb's pixels PC-BASIC's, the array's 3,001 elements read
// back, FRE(0) the same after. And by hand: with under 12 bytes free, Out of memory at once (INTQUE's floor); with
// about 200 free, the comb's queue fills and Out of memory part way, the strings and an array past $4000 intact;
// Ctrl-C during a whole screen's fill gives Break, and that array element reads back (block 1 in W1). Guards a big PAINT corrupting the program's variables, or one that
// can't be stopped. Seen failing with (a) PUTQ without its block 1 (the entries written to the card) and (b) the
// poll (CKCNTC) left out of PNTLOP.
TEST(paint_queue_lives_in_free_memory)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx18", {"gfx18"}, ms(60000)) >= 0);
        CHECK_EQ(compare(r, "gfx18"), 0u);
        enter(r, "PRINT \"P\";HEX$(P0);\" \";HEX$(P1):PRINT \"B\";B;F0-F1", ms(1000));
        unsigned lo = 0, hi = 0;
        const int p = last_row(r, "P");
        REQUIRE(p >= 0);
        CHECK(std::sscanf(line(r, static_cast<unsigned>(p)).c_str(), "P%x %x", &lo, &hi) == 2);
        CHECK(lo < 0x4000 && hi >= 0x4000);
        CHECK_EQ(line(r, static_cast<unsigned>(p + 1)), std::string("B 0  0"));
    }
    // Ctrl-C during a whole screen's fill: Break, and an array element past $4000 read back (block 1 in W1)
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r) >= 0);
        enter(r, "10 SCREEN 2:DIM X%(6000):X%(6000)=777:PAINT (320,100),1", ms(100));
        keys(r, "RUN\r");
        r.run(ms(1500));
        press(r, 0x06, 0xE0);
        CHECK(until_row(r, "Break in 10", ms(1000)) >= 0);
        unsigned filled = 0;
        for (unsigned y = 0; y < 200; ++y)
            filled += pixel(r, 0, y) == 1;
        CHECK(filled < 200); // stopped before the fill's end
        enter(r, "PRINT \"X\";X%(6000)", ms(1000));
        CHECK(last_row(r, "X 777") >= 0);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    // under 12 bytes free: strings take the space
    enter(r, "SCREEN 2:DIM S$(200):DIM X%(6000):X%(6000)=777:N=0:F=0", ms(1000));
    enter(r, "F=FRE(0)-8:WHILE F>250:S$(N)=SPACE$(250):N=N+1:F=FRE(0)-8:WEND:S$(N)=SPACE$(F):PRINT \"Z\"+\"9\";FRE(0)", ms(100));
    until_row(r, "Z9", ms(60000));
    std::printf("  %s\n", line(r, static_cast<unsigned>(std::max(0, last_row(r, "Z9")))).c_str());
    enter(r, "PAINT (1,1),1", ms(2000));
    CHECK(last_row(r, "Out of memory") >= 0);
    enter(r, "CLS:PRINT \"X\";X%(6000)", ms(1000));
    CHECK(last_row(r, "X 777") >= 0);
    // about 200 bytes free: the comb's queue fills part way; the strings and the array intact
    enter(r, "CLEAR:CLS:DIM S$(200):DIM X%(6000):X%(6000)=777:N=0:F=0", ms(2000));
    enter(r, "FOR X=0 TO 636 STEP 4:LINE (X,0)-(X,90),1:NEXT", ms(6000));
    enter(r, "F=FRE(0)-210:WHILE F>250:S$(N)=SPACE$(250):N=N+1:F=FRE(0)-210:WEND:PRINT \"W\"+\"7\";F;N", ms(100));
    until_row(r, "W7", ms(60000));
    enter(r, "S$(N)=SPACE$(F):PRINT \"Z\"+\"9\";FRE(0)", ms(100));
    until_row(r, "Z9", ms(60000));
    std::printf("  %s\n", line(r, static_cast<unsigned>(std::max(0, last_row(r, "Z9")))).c_str());
    enter(r, "PAINT (1,95),1", ms(10000));
    CHECK(last_row(r, "Out of memory") >= 0);
    enter(r, "CLS:B=0:FOR I=0 TO N-1:B=B-(LEN(S$(I))<>250)-(ASC(S$(I))<>32):NEXT:PRINT \"Q\";B;X%(6000)", ms(8000));
    CHECK(last_row(r, "Q 0  777") >= 0);
}

// G16 (7e2): DRAW's pixels against PC-BASIC's, exact (gwbasic/gfx19 in SCREEN 1, gfx20 in SCREEN 2): U D L R E F G H,
// M both ways, B, N, C, S 1-12 with negative moves (DSCLDE's truncation toward 0), A0-A3 (A1 and A3 by moves
// the aspect makes whole, R and U: the source rounds, PC-BASIC truncates; a turned L and A3's D, where PC-BASIC
// parts from the source, by hand), X with VARPTR$ and by name, =var; single and integer,
// lower case, spaces and ";", U D L R across every edge, LINE -( ) and PSET STEP after; and by hand, moves from far
// off the screen and diagonals across the edges clipped as LINE's (the true clipping, the owner's G11; PC-BASIC's own
// clipping moves the far end, so differs) against the test's reference. Guards a turtle drawing in the wrong place, a scaled shape the wrong size, the cursor wrong after.
// Seen failing with (a) DSCLDE truncating a negative value down (S3 L5 4, not 3) and (b) DRWFLG not cleared after a
// move (N sticks).
TEST(draw_matches_pc_basic)
{
    for (const char *name : {"gfx19", "gfx20"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, std::string("m:gwbasic m:") + name, {name}, ms(20000)) >= 0);
        std::printf("  %s:\n", name);
        CHECK_EQ(compare(r, name), 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:DRAW \"BM-500,-500 C2 M20,190 BM300,-40 C3 M250,250 BM10,150 C1 G20 BM310,150 F30 BM200,10 E30\"",
          ms(2000));
    Ref ref(1); // (BM-500,-500 is relative: from the centre, to (-340,-400))
    ref.line(20, 190, -340, -400, 2), ref.line(250, 250, 300, -40, 3);
    ref.line(-10, 170, 10, 150, 1), ref.line(340, 180, 310, 150, 1), ref.line(230, -20, 200, 10, 1);
    CHECK_EQ(compare_ref(r, ref), 0u);
    // A3 by the source (GOSCAL's rounding, its swap and negation; PC-BASIC parts from it here): D10 is 12 left
    enter(r, "CLS:DRAW \"BM80,80 C3 A3 D10 A0\"", ms(1500));
    CHECK_EQ(pixel(r, 68, 80), 3);
    CHECK_EQ(pixel(r, 74, 80), 3);
    CHECK_EQ(pixel(r, 67, 80), 0);
    CHECK_EQ(pixel(r, 80, 90), 0);
    enter(r, "CLS:DRAW \"BM80,80 C2 A1 L12 A0\"", ms(1500)); // L12 turned: down 10 (12 x 213/256, rounded)
    CHECK_EQ(pixel(r, 80, 90), 2);
    CHECK_EQ(pixel(r, 80, 91), 0);
}

// G17 (7e2): DRAW's TA and P (not in the source; the IBM manual's, pp. 74-75, the owner's answer 9; the turn's
// arithmetic the OO-1's), against PC-BASIC where it agrees (gwbasic/gfx21): TA 30, -45, 45, 90, 120, 200, 270 with S4
// and S8, P inside a box and a diamond, the colour after P. And by hand, where PC-BASIC parts from the manual: P sets
// the drawing colour to its paint even when it paints nothing (the manual's "figure color", the owner's G10:
// DRAW "C1 P3,0 R9" from a pixel of 0, the border, paints nothing and draws the R9 in 3, where PC-BASIC keeps 1);
// A n after TA n turns by A alone (the OO-1's: the one angle). Guards a rotated figure drawn wrong, a filled shape
// left empty. Seen failing with (a) TA's turn's sine term's sign flipped and (b) P's border taken as its paint.
TEST(draw_turns_and_paints)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx21", {"gfx21"}, ms(20000)) >= 0);
        CHECK_EQ(compare(r, "gfx21"), 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:DRAW \"BM20,20 C1 P3,0 R9\"", ms(1500));
    CHECK_EQ(pixel(r, 20, 20), 3);
    CHECK_EQ(pixel(r, 29, 20), 3);
    CHECK_EQ(pixel(r, 30, 20), 0);
    CHECK_EQ(pixel(r, 20, 21), 0);
    enter(r, "DRAW \"BM100,100 C2 TA45 A1 R12 TA0\"", ms(1500)); // A1 alone: up 10 (12 x 5/6)
    CHECK_EQ(pixel(r, 100, 90), 2);
    CHECK_EQ(pixel(r, 100, 95), 2);
    CHECK_EQ(pixel(r, 108, 92), 0);
}

// G18 (7e2): the errors of DRAW, CIRCLE and PAINT (gwbasic/gfx22: 78 statements, each code against PC-BASIC's but
// the six the source or the manual decides: DRAW "R99999" Illegal function call (NUML01's overflow, D6), "R1 ;;; U1"
// Illegal function call (a ";" alone is no command, D7), "R--5" a move of 5 (each "-" negates, D9), CIRCLE with a
// negative radius no error (no check, the owner's G9), PAINT's tile and background Advanced Feature until 7e4 (G12));
// with S0 Illegal function call (the manual's p. 74) and PAINT off the screen no error (p. 236), as PC-BASIC has
// them. And by hand: "U;" a move of 0 (the source's empty number, D8); a recursive X Out of memory (MCLXEQ's GETSTK,
// D15), and the next line runs; DRAW, CIRCLE and PAINT in SCREEN 0 Illegal function call; PAINT off the screen leaves
// the cursor where SCAN1 put it (LINE -(0,0) from (400,100)); after each error an array element past $4000 reads
// back. Guards an error in a drawing statement leaving the aperture in W1, or a runaway X crashing the interpreter.
// Seen failing with MCLXEQ without its stack check (the recursive X overruns the 6502 stack).
TEST(graphics_errors_are_the_source_s)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:gfx22", {"gfx22"}, ms(120000)) >= 0);
        // the codes, 3 columns each, wrapped at 80 (here) or 78 (PC-BASIC's dump): read as words
        auto words = [](const std::string &text) {
            std::vector<int> v;
            size_t i = 0;
            while (i < text.size())
            {
                while (i < text.size() && (text[i] == ' ' || text[i] == '\r' || text[i] == '\n'))
                    ++i;
                size_t j = i;
                while (j < text.size() && text[j] >= '0' && text[j] <= '9')
                    ++j;
                if (j == i)
                    break;
                v.push_back(std::atoi(text.substr(i, j - i).c_str()));
                i = j;
            }
            return v;
        };
        const std::vector<int> pc = words(read_text(EMU_GWBASIC_PROGRAMS "/gfx22.out"));
        std::string shown;
        for (unsigned row = 0; row < 4; ++row)
        {
            std::string l = line(r, row); // (padded: a code may wrap across two rows)
            l.resize(80, ' ');
            if (l.find("Ok") != std::string::npos)
                break;
            shown += l;
        }
        const std::vector<int> ours = words(shown);
        REQUIRE(pc.size() >= 78 && ours.size() >= 78);
        const std::map<int, int> source = {{9, 5}, {25, 5}, {28, 0}, {43, 0}, {69, 73}, {70, 73}};
        unsigned bad = 0;
        for (int i = 0; i < 78; ++i)
        {
            const auto s = source.find(i);
            const int want = s == source.end() ? pc[static_cast<size_t>(i)] : s->second;
            if (ours[static_cast<size_t>(i)] != want && ++bad <= 8)
                std::printf("  statement %d: %d, want %d (PC-BASIC's %d)\n", i, ours[static_cast<size_t>(i)], want,
                            pc[static_cast<size_t>(i)]);
        }
        CHECK_EQ(bad, 0u);
    }
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    enter(r, "SCREEN 1:DIM X%(6000):X%(6000)=777:DRAW \"BM50,50 C1 U; R5\"", ms(1500));
    CHECK_EQ(pixel(r, 50, 50), 1);
    CHECK_EQ(pixel(r, 50, 49), 0);
    CHECK_EQ(pixel(r, 55, 50), 1);
    enter(r, "A$=\"R1XA$;\":DRAW \"XA$;\"", ms(3000));
    CHECK(last_row(r, "Out of memory") >= 0);
    enter(r, "CLS:PRINT \"X\";X%(6000)", ms(1000));
    CHECK(last_row(r, "X 777") >= 0);
    enter(r, "PAINT (400,100),1:LINE -(0,0),2", ms(1500));
    CHECK_EQ(pixel(r, 0, 0), 2);
    CHECK_EQ(pixel(r, 316, 79), 2); // on the line from (400,100): 316 x 100/400 = 79
    CHECK_EQ(pixel(r, 160, 100), 0);
    for (const char *stmt : {"DRAW \"R5\"", "CIRCLE (10,10),5", "PAINT (10,10),1"})
    {
        enter(r, "SCREEN 0:CLS", ms(800));
        enter(r, stmt, ms(800));
        CHECK(last_row(r, "Illegal function call") >= 1);
        enter(r, "PRINT \"X\";X%(6000)", ms(800));
        CHECK(last_row(r, "X 777") >= 0);
    }
}

// G19 (7e2): block 1 is back in W1 after CIRCLE, PAINT and DRAW (G3's method, gwbasic/gfx23: an array across
// $4000-$7FFF, filled, then read back after a circle, an arc with radius lines, a PAINT, a DRAW with X, a DRAW with P
// and one with TA: 6 checks). Guards a program's variables read from the card after a drawing. Seen failing with
// CIRCP's exit without GX_W1BACK.
TEST(block_1_is_back_after_circle_paint_and_draw)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx23", {"gfx23"}, ms(60000)) >= 0);
    const int at = last_row(r, "CHECKS");
    REQUIRE(at >= 0);
    CHECK_EQ(line(r, static_cast<unsigned>(at)), std::string("CHECKS 6 BAD 0"));
}

/// RUN's cycles, from the RUN statement to the end's ENDCON (check.py --timing's method), the program `lines` typed
/// first (after NEW).
uint64_t timed(CcpRig &r, const std::vector<std::string> &lines)
{
    const std::map<std::string, uint16_t> lab = lbl_map();
    enter(r, "NEW");
    for (const std::string &l : lines)
        enter(r, l);
    const uint16_t runc = lab.at("RUN"), endcon = lab.at("ENDCON");
    uint64_t from = 0, cycles = 0;
    bool on = false;
    r.watch = [&](uint16_t pc) {
        if (pc == runc && !on)
        {
            from = r.cpu->cycles();
            on = true;
        }
        else if (on && pc == endcon)
        {
            cycles = r.cpu->cycles() - from;
            on = false;
        }
    };
    keys(r, "RUN\r");
    const uint64_t end = r.cpu->cycles() + ms(60000);
    while (cycles == 0 && r.cpu->cycles() < end)
        r.run(ms(10));
    r.watch = nullptr;
    r.run(ms(200));
    CHECK(cycles > 0);
    return cycles;
}

// The graphics' timing gate (V6), by check.py --timing's method (a loop with the statement less the loop without it,
// over the passes): in SCREEN 2, PSET (X%,Y%),1 with integer variables at most 9,000 cycles a statement, and LINE
// (0,0)-(639,199),1 (640 points, each two rows) at most 200,000; for the record, a 7 x 7 BF in SCREEN 1 (LIFEGFX's
// box), LINE (0,0)-(319,199) there, CLS in SCREEN 1 and 2, and a line clipped from ends 32,000 off the screen.
// Guards graphics too slow to use. Seen failing with
// the aperture mapped by gxmem's GX_VMAP for every pixel (NEWBNK's cache off).
TEST(graphics_are_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    auto per = [&](const std::string &setup, const std::string &stmt, int n) {
        const std::string loop = "I%=I%+1:IF I%<" + std::to_string(n) + " THEN 20";
        const uint64_t a = timed(r, {"10 " + setup + ":X%=100:Y%=50:I%=0", "20 " + loop});
        const uint64_t b = timed(r, {"10 " + setup + ":X%=100:Y%=50:I%=0", "20 " + stmt + ":" + loop});
        return static_cast<double>(b - a) / n;
    };
    const double pset = per("SCREEN 2", "PSET (X%,Y%),1", 100);
    const double line2 = per("SCREEN 2", "LINE (0,0)-(639,199),1", 10);
    const double box = per("SCREEN 1", "LINE (X%,Y%)-(X%+6,Y%+6),3,BF", 50);
    const double line1 = per("SCREEN 1", "LINE (0,0)-(319,199),2", 10);
    const double cls1 = per("SCREEN 1", "CLS", 10), cls2 = per("SCREEN 2", "CLS", 10);
    const double far = per("SCREEN 2", "LINE (-32000,-32000)-(32000,32000),1", 10);
    std::printf("  PSET (X%%,Y%%),1 in SCREEN 2: %.0f cycles (the gate 9,000)\n", pset);
    std::printf("  LINE (0,0)-(639,199),1 in SCREEN 2: %.0f cycles (the gate 200,000)\n", line2);
    std::printf("  for the record: a 7 x 7 BF in SCREEN 1 %.0f, LINE (0,0)-(319,199) %.0f, CLS in SCREEN 1 %.0f, in "
                "SCREEN 2 %.0f, LINE (-32000,-32000)-(32000,32000) in SCREEN 2 (clipped) %.0f\n", box, line1, cls1, cls2,
                far);
    CHECK(pset > 500 && pset <= 9000);
    CHECK(line2 > 10000 && line2 <= 200000);
}


// G20 (7e2): CIRCLE, PAINT and DRAW are timed (timed()'s method, a loop with the statement less the loop without):
// CIRCLE (320,100),50,1 in SCREEN 2 at most 400,000 cycles; a whole SCREEN 2 PAINT (128,000 pixels, from the centre)
// at most 40 M; a 100-move DRAW (A$, 50 x "R3D3", in SCREEN 2) at most 500,000 (the owner's answer 13); for the
// record SCREEN 1's whole PAINT, CIRCLE r=100 in SCREEN 1, and the DRAW with TA30. Guards graphics too slow to use.
// Seen failing with SCANR re-making the cursor by MAPXYC at each pixel (the whole PAINT over 40 M).
TEST(circle_paint_and_draw_are_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) >= 0);
    auto per = [&](const std::string &setup, const std::string &stmt, int n) {
        const std::string loop = "I%=I%+1:IF I%<" + std::to_string(n) + " THEN 20";
        const uint64_t a = timed(r, {"10 " + setup + ":I%=0", "20 " + loop});
        const uint64_t b = timed(r, {"10 " + setup + ":I%=0", "20 " + stmt + ":" + loop});
        return static_cast<double>(b - a) / n;
    };
    const double circ = per("SCREEN 2", "CIRCLE (320,100),50,1", 10);
    const double paint2 = per("SCREEN 2", "CLS:PAINT (320,100),1", 2) - per("SCREEN 2", "CLS", 2);
    const std::string moves = "A$=\"\":FOR J=1 TO 50:A$=A$+\"R3D3\":NEXT:DRAW \"BM10,10\"";
    const double draw = per("SCREEN 2:" + moves, "DRAW \"BM10,10\"+A$", 5) - per("SCREEN 2:" + moves, "DRAW \"BM10,10\"", 5);
    const double circ1 = per("SCREEN 1", "CIRCLE (160,100),100,1", 5);
    const double paint1 = per("SCREEN 1", "CLS:PAINT (160,100),1", 2) - per("SCREEN 1", "CLS", 2);
    const double drawta = per("SCREEN 2:" + moves, "DRAW \"BM10,10 TA30\"+A$", 2) - per("SCREEN 2:" + moves, "DRAW \"BM10,10 TA30\"", 2);
    std::printf("  CIRCLE (320,100),50,1 in SCREEN 2: %.0f cycles (the gate 400,000)\n", circ);
    std::printf("  a whole SCREEN 2 PAINT: %.0f cycles (the gate 40,000,000)\n", paint2);
    std::printf("  a 100-move DRAW in SCREEN 2: %.0f cycles (the gate 500,000)\n", draw);
    std::printf("  for the record: CIRCLE r=100 in SCREEN 1 %.0f, a whole SCREEN 1 PAINT %.0f, the DRAW with TA30 %.0f\n",
                circ1, paint1, drawta);
    CHECK(circ > 10000 && circ <= 400000);
    CHECK(paint2 > 1000000 && paint2 <= 40000000);
    CHECK(draw > 10000 && draw <= 500000);
}


// --- 7e3: GET and PUT of graphics ---------------------------------------------------------------------------------

namespace
{
    /// A program's text output after SCREEN 0 (NAME.out's lines, their trailing blanks dropped).
    std::vector<std::string> out_lines(const std::string &name)
    {
        std::vector<std::string> want;
        std::string l;
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".out"))
            if (c == '\n')
            {
                while (!l.empty() && (l.back() == ' ' || l.back() == '\r'))
                    l.pop_back();
                want.push_back(l);
                l.clear();
            }
            else
                l += c;
        return want;
    }

    /// GET of the whole screen into A%(8001), typed at the prompt: the array's 16,004 bytes as they are in the TPA,
    /// read when the resident NEWSTT runs after the GET (the program's blocks in W0-W2, block 1 back in W1), found by
    /// its first word (the bits, 640). Empty if never seen.
    std::vector<uint8_t> get_whole(CcpRig &r, unsigned last_x)
    {
        enter(r, "DIM A%(8001):PRINT \"V \";HEX$(VARPTR(A%(0)))", ms(1000));
        const int row = last_row(r, "V ");
        unsigned addr = 0;
        if (row < 0 || std::sscanf(line(r, static_cast<unsigned>(row)).c_str(), "V %x", &addr) != 1)
            return {};
        std::vector<uint8_t> got;
        const uint16_t newstt = lbl_map().at("NEWSTT");
        auto pk = [&](unsigned a) { return r.machine->bus().peek(static_cast<uint16_t>(a)); };
        r.watch = [&](uint16_t pc) {
            if (pc != newstt || !got.empty() || (pk(addr) | pk(addr + 1) << 8) != 640)
                return;
            for (unsigned i = 0; i < 16004; ++i)
                got.push_back(pk(addr + i));
        };
        keys(r, "GET (0,0)-(" + std::to_string(last_x) + ",199),A%\r");
        r.run(ms(2000));
        r.watch = nullptr;
        return got;
    }
} // namespace

// G21 (7e3): GET reads what PC-BASIC's GET read: gfx1 and gfx21 (SCREEN 1), gfx2 and gfx16 (SCREEN 2) run with their
// lines from 9000 cut, then a whole-screen GET typed at the prompt; the array, read from the TPA, equals PC-BASIC's
// (NAME.out: the header 640 and 200, then its dump's rows, which are GET's bytes) byte for byte. Guards a saved
// screen or sprite garbled, a PC program's tests of its own arrays failing. Seen failing with (a) SCREEN 1's byte
// path packing a tile byte's right pixel into the higher pair, (b) SCREEN 2's byte path not mapping the next bank
// when a row crosses W1's end (GNXHI's BANKUP left out).
TEST(get_reads_what_pc_basic_s_get_read)
{
    for (const auto &[name, w] : std::vector<std::pair<std::string, unsigned>>{
             {"gfx1", 319}, {"gfx21", 319}, {"gfx2", 639}, {"gfx16", 639}})
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:" + name, {name}, ms(60000)) >= 0);
        const std::vector<uint8_t> got = get_whole(r, w);
        REQUIRE(got.size() == 16004u);
        const std::vector<std::string> want = expected(name);
        REQUIRE(want.size() == 200u);
        CHECK_EQ(got[0] | got[1] << 8, 640);
        CHECK_EQ(got[2] | got[3] << 8, 200);
        unsigned bad = 0;
        for (unsigned y = 0; y < 200; ++y)
        {
            std::string s;
            for (unsigned b = 0; b < 80; ++b)
            {
                char h[3];
                std::snprintf(h, sizeof h, "%02X", got[4 + y * 80 + b]);
                s += h;
            }
            if (s != want[y] && ++bad <= 4)
                std::printf("  %s row %u:\n    want %s\n    got  %s\n", name.c_str(), y, want[y].c_str(), s.c_str());
        }
        if (bad)
            std::printf("  %s: %u rows of 200 differ\n", name.c_str(), bad);
        CHECK_EQ(bad, 0u);
    }
}

// G22 (7e3): GET's array layout matches PC-BASIC's (gwbasic/gfx24, the IBM manual's pp. 116-117): element 0 the bits
// (the pixels x 2 in SCREEN 1, x 1 in SCREEN 2), element 1 the rows, each row padded to a byte with 0 bits, the
// leftmost pixel high; rectangles aligned and not, the corners reversed, 1 x 1, 8, 9, 12 and 13 wide, in both modes;
// the elements printed after SCREEN 0, exactly PC-BASIC's. Guards an image's size or padding wrong, so a PUT (here or
// a PC's) misplaces it. Seen failing with (a) element 0 the pixels, not the bits; (b) a row's last byte's spare bits
// set, not cleared.
TEST(get_array_layout_matches_pc_basic)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx24", {"gfx24"}, ms(60000)) >= 0);
    const std::vector<std::string> want = out_lines("gfx24");
    REQUIRE(want.size() >= 16u);
    for (unsigned i = 0; i < 16; ++i)
        CHECK_EQ(line(r, i), want[i]);
}

// G23 (7e3): PUT's actions match PC-BASIC's (the IBM manual's pp. 282-284), SCREEN 1's frame (gwbasic/gfx25) and
// SCREEN 2's (gfx28) against PC-BASIC's dumps: a 4 x 4 (or 10 x 4) sprite by PSET, PRESET, AND, OR, XOR and the
// default (XOR) on backgrounds of every colour at each x MOD 4 (or 8), wider sprites aligned and not, XOR twice (the
// background back), AND and OR over a pattern, a SCREEN 2 array PUT in SCREEN 1 (4 pixels of 2 bits), 7 bits in
// SCREEN 1 (3 pixels), and in SCREEN 2 rows across the page's first 16 KB (row 102's pair). Guards sprites in the
// wrong colours, an XOR animation leaving trails. Seen failing with (a) PRESET storing the array as PSET (GBPRE's and
// GOPP's complement left out), (b) SCREEN 2's byte path writing row 2y only (the twin unequal).
TEST(put_actions_match_pc_basic)
{
    for (const std::string name : {"gfx25", "gfx28"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r, "m:gwbasic m:" + name, {name}, ms(60000)) >= 0);
        CHECK_EQ(compare(r, name), 0u);
    }
}

// G24 (7e3): GET and PUT's errors are the source's (gwbasic/gfx26: 47 codes, PC-BASIC's where it agrees; and by
// hand where the source or the manual decides): an array exactly big enough and one byte short (Illegal function
// call, nothing written), single, double and 2-D arrays, a string array (Type mismatch: the owner's answer 6, the
// manual's pp. 116, 281), one not dimensioned; an action word after GET, an unknown one, a comma and none (Syntax
// error: the manual's form, p. 281; the source's NEWSTT would take the end), no array, one corner, PUT STEP, the
// edges (310 fits, 311 doesn't; 190 fits, 191 doesn't; -1, 32767). By hand, the source's: an empty array (width or
// height 0: Illegal function call, 40-41), a 3,000-bit row (42), "@" taken (43-44), GET into K%(5) filling from
// element 0 before the Syntax error (45), the cursor at GET's first corner when it is off the screen (46), GET's
// first corner by STEP (the owner's answer 5, 48), SCREEN 0 (49-50). After every error the array P() across $4000
// reads back unchanged (C: none bad) and the next line runs. Guards a refused GET writing past its array, an error
// leaving the aperture in W1. Seen failing with (a) GPUTA's size check an element short (2: B%(15) taken), (b) PUT's
// far corner's CHKRNG left out (four refusals gone, the PUTs past the right and bottom edges among them).
TEST(get_and_put_errors_are_the_source_s)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx26", {"gfx26"}, ms(120000)) >= 0);
    std::string shown;
    for (unsigned row = 0; row < 10; ++row)
    {
        const std::string l = line(r, row);
        if (l == "Ok")
            break;
        shown += l;
    }
    std::string pc;
    for (const std::string &l : out_lines("gfx26"))
        pc += l;
    auto items = [](const std::string &text) {
        std::map<std::string, std::string> m;
        size_t i = 0;
        while ((i = text.find('|', i)) != std::string::npos)
        {
            const size_t c = text.find(':', i), j = text.find('|', i + 1);
            const std::string k = text.substr(i + 1, c - i - 1);
            std::string v = text.substr(c + 1, j == std::string::npos ? std::string::npos : j - c - 1);
            while (!v.empty() && v.back() == ' ')
                v.pop_back();
            if (c != std::string::npos)
                m[k] = v;
            i = j == std::string::npos ? text.size() : j;
        }
        // (the canary's line has no colon: "C n b")
        const size_t ci = text.find("|C ");
        if (ci != std::string::npos)
            m["C"] = text.substr(ci + 2);
        return m;
    };
    std::map<std::string, std::string> ours = items(shown), want = items(pc);
    REQUIRE(want.size() >= 40u);
    // the source's and the manual's, where PC-BASIC differs
    const std::map<std::string, std::string> hand = {{"40", " 5"}, {"41", " 5"}, {"42", " 5"}, {"43", " 0"},
                                                       {"44", " 0"}, {"45", " 2  20  10"}, {"46", " 5  2"},
                                                       {"48", " 0  20  10"}};
    for (const auto &[k, v] : hand)
        want[k] = v;
    unsigned bad = 0;
    for (const auto &[k, v] : want)
    {
        std::string o = ours.count(k) ? ours[k] : "(none)";
        auto squeeze = [](std::string t) {
            std::string u;
            for (const char c : t)
                if (c != ' ')
                    u += c;
                else if (!u.empty() && u.back() != ' ')
                    u += ' ';
            while (!u.empty() && u.back() == ' ')
                u.pop_back();
            return u;
        };
        if (k == "C") // the canary's bad count alone (the errors' count follows the source's codes)
            o = squeeze(o).substr(squeeze(o).rfind(' ') + 1);
        const std::string w = k == "C" ? std::string("0") : squeeze(v);
        if (squeeze(o) != w && ++bad <= 8)
            std::printf("  %s: %s, want %s\n", k.c_str(), o.c_str(), w.c_str());
    }
    CHECK_EQ(bad, 0u);
}

// G25 (7e3): GET and PUT keep block 1 (G3's method, gwbasic/gfx27): the sprite's array S() lies in $4000-$7FFF (its
// rows pass through TXBUF), Q() across $4000 and P() across $8000 as canaries; in both modes a 48 x 48 (96 x 48)
// sprite GET, PUT by PSET unaligned and aligned and by XOR, each GET back equal to the first, and both canaries read
// back after each of the 14 GETs and PUTs. Guards a program's variables taken from or written to the card. Seen
// failing with NWRITE drawing the row straight from the array (not through TXBUF: the rows in $4000-$7FFF read from
// card memory).
TEST(get_and_put_keep_block_1)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r, "m:gwbasic m:gfx27", {"gfx27"}, ms(120000)) >= 0);
    const int at = last_row(r, "CHECKS");
    REQUIRE(at >= 0);
    CHECK_EQ(line(r, static_cast<unsigned>(at)), std::string("CHECKS 14 BAD 0 DIFF 0"));
    const int q = last_row(r, "Q ");
    REQUIRE(q >= 0);
    unsigned q0 = 0, q1 = 0, s0 = 0, s1 = 0, p0 = 0, p1 = 0;
    CHECK(std::sscanf(line(r, static_cast<unsigned>(q)).c_str(), "Q %x %x S %x %x P %x %x", &q0, &q1, &s0, &s1, &p0,
                      &p1) == 6);
    CHECK(q0 < 0x4000 && q1 >= 0x4000);
    CHECK(s0 >= 0x4000 && s1 < 0x8000);
    CHECK(p0 < 0x8000 && p1 >= 0x8000);
}

int main()
{
    return run_all();
}
