/**
 * @file test_oodos_gfx.cpp
 * @brief OOLIB's L4 (hw/oolib.md, "The plan"): the linked graphics library (oodos/lib/gx*.s) and VIEW.COM, on
 * video card revision 2 (hw/video-rev2.md, interface version 8): the program's card memory handed out in card
 * pages, drawn through aperture slot 1 in W1, copied and filled on the card by function 83.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * GXTEST (oodos/test/gx/gxtest.s), run by GO, calls a library routine for each command: a letter at $3900,
 * A, X and Y after it, the GX_* variables poked here at gxtest.lbl's addresses. What it draws is read back
 * from the card's memory and compared with what this file draws itself by the same rules (a line by Bresenham's,
 * a circle by the midpoint way, a flood as the 4-connected region); what the card shows (frame()) with what
 * the bytes say. VIEW shows assets that check_mkasset.py made from PNGs it wrote itself (never the owner's).
 */

#include "harness.h"
#include "oodos_rig.h"
#include "video_card.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    constexpr uint32_t AREA = 0x3800, MBOX = 0x3900, TEXT = 0x3980; ///< gxtest.s's
    constexpr unsigned PAGE0 = 0x0400; ///< vmem.inc's VM_PROG_PAGE: the program's first card page

    const std::map<std::string, uint16_t> &lbl()
    {
        static const std::map<std::string, uint16_t> l = labels(UTILS + "/gxtest.lbl");
        return l;
    }
    uint16_t at(const char *name) { return lbl().at(name); }
    void word(Rig &r, const char *name, unsigned v)
    {
        r.poke(at(name), static_cast<uint8_t>(v));
        r.poke(at(name) + 1u, static_cast<uint8_t>(v >> 8));
    }
    /// GX_A or GX_C: a card address, 3 bytes.
    void addr3(Rig &r, const char *name, uint32_t a)
    {
        for (unsigned i = 0; i < 3; ++i)
            r.poke(at(name) + i, static_cast<uint8_t>(a >> (8 * i)));
    }
    uint32_t addr3(Rig &r, const char *name)
    {
        return r.phys(at(name)) | r.phys(at(name) + 1u) << 8 | static_cast<uint32_t>(r.phys(at(name) + 2u)) << 16;
    }
    VideoCard &vid(Rig &r) { return *r.machine->video_card(); }
    uint8_t vreg(Rig &r, uint8_t off) { return vid(r).peek(off); }
    unsigned vreg2(Rig &r, uint8_t off) { return vreg(r, off) | vreg(r, static_cast<uint8_t>(off + 1)) << 8; }
    /// The card's own memory, at card address `a`.
    uint8_t card(Rig &r, uint32_t a) { return vid(r).card_peek(a); }
    std::vector<uint8_t> cmem(Rig &r, uint32_t a, size_t n)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = card(r, a + static_cast<uint32_t>(i));
        return b;
    }
    void cpoke(Rig &r, uint32_t a, const std::vector<uint8_t> &b)
    {
        for (size_t i = 0; i < b.size(); ++i)
            vid(r).card_poke(a + static_cast<uint32_t>(i), b[i]);
    }
    /// Palette entry `i` of the palette at card page `pal`.
    uint16_t entry(Rig &r, unsigned pal, unsigned i)
    {
        return static_cast<uint16_t>((card(r, pal * 256 + 2 * i + 1) & 15) << 8 | card(r, pal * 256 + 2 * i));
    }

    /// The ROM's boot splash stopped (the card shows it for its first 300 frames, whatever CTRL says).
    void no_splash(Rig &r)
    {
        r.machine->bus().write(0xC489, 0);
        r.run(ms(40));
    }

    size_t start_gx(CcpRig &r)
    {
        no_splash(r);
        r.poke(MBOX, 0);
        r.poke_com(read_bytes(UTILS + "/gxtest.com"));
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("O 00 00", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("O 00 00", from)));
        return from;
    }
    /// Command `c` with A, X, Y: "aa xx cc".
    std::string gx(CcpRig &r, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0, uint64_t budget = ms(8000))
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX + 1, std::vector<uint8_t>{a, x, y});
        r.poke(MBOX, static_cast<uint8_t>(c));
        const std::string label(1, c);
        if (!r.until("\n" + label + " ", budget, from))
        {
            std::printf("  no %c line\n", c);
            r.tail();
            return std::string(8, '?');
        }
        const size_t p = r.out.find("\n" + label + " ", from);
        r.until("\r", ms(500), p + 1);
        const std::string f = field(r.out.substr(from), label);
        return f.size() >= 8 ? f : std::string(8, '?');
    }
    unsigned hexa(const std::string &s) { return s[0] == '?' ? 0 : static_cast<unsigned>(std::stoul(s.substr(0, 2), nullptr, 16)); }
    /// A and X of a command's answer, as a word: a card page.
    unsigned hexax(const std::string &s)
    {
        return s[0] == '?' ? 0 : static_cast<unsigned>(std::stoul(s.substr(0, 2), nullptr, 16) | std::stoul(s.substr(3, 2), nullptr, 16) << 8);
    }
    /// GX_VALLOC: `pages` card pages aligned to `align`: "aa xx cc".
    std::string valloc(CcpRig &r, unsigned pages, unsigned align = 1)
    {
        word(r, "GX_V", pages);
        return gx(r, 'A', static_cast<uint8_t>(align));
    }
    unsigned alloc(CcpRig &r, unsigned pages, unsigned align = 1)
    {
        const std::string a = valloc(r, pages, align);
        CHECK_EQ(a.substr(6), std::string("00"));
        return hexax(a);
    }
    void vfree(CcpRig &r, unsigned page, unsigned pages)
    {
        word(r, "GX_V", pages);
        gx(r, 'a', static_cast<uint8_t>(page), static_cast<uint8_t>(page >> 8));
    }
    /// GX_BMP: bitmap `mode` from card page `page`.
    void bmp_at(CcpRig &r, unsigned mode, unsigned page)
    {
        word(r, "GX_V", page);
        gx(r, 'B', static_cast<uint8_t>(mode));
    }

    // ---- the reference: the same rules, on a copy of the bitmap ------------------------------------------

    struct Bmp
    {
        unsigned mode, w, h, row;
        std::vector<uint8_t> b;
        explicit Bmp(unsigned m)
            : mode(m), w(m == 0 ? 256 : m == 1 ? 512 : 640), h(m == 0 ? 240 : 480), row(m == 2 ? 80 : 256),
              b(static_cast<size_t>(row) * h, 0)
        {
        }
        bool off(int x, int y) const { return x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h); }
        void plot(int x, int y, unsigned c)
        {
            if (off(x, y))
                return;
            if (mode == 0)
                b[y * row + x] = static_cast<uint8_t>(c);
            else if (mode == 1)
            {
                uint8_t &v = b[y * row + x / 2];
                v = x & 1 ? static_cast<uint8_t>((v & 0xF0) | (c & 15)) : static_cast<uint8_t>((v & 0x0F) | (c & 15) << 4);
            }
            else
            {
                uint8_t &v = b[y * row + x / 8];
                const uint8_t m = static_cast<uint8_t>(0x80 >> (x & 7));
                v = c & 1 ? static_cast<uint8_t>(v | m) : static_cast<uint8_t>(v & ~m);
            }
        }
        unsigned point(int x, int y) const
        {
            if (off(x, y))
                return 0;
            if (mode == 0)
                return b[y * row + x];
            if (mode == 1)
                return x & 1 ? b[y * row + x / 2] & 15 : b[y * row + x / 2] >> 4;
            return (b[y * row + x / 8] >> (7 - (x & 7))) & 1;
        }
        void line(int x0, int y0, int x1, int y1, unsigned c)
        {
            const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            for (;;)
            {
                plot(x0, y0, c);
                if (x0 == x1 && y0 == y1)
                    break;
                const int e2 = 2 * err;
                if (e2 >= dy)
                    err += dy, x0 += sx;
                if (e2 <= dx)
                    err += dx, y0 += sy;
            }
        }
        void rect(int x0, int y0, int x1, int y1, unsigned c)
        {
            for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
                plot(x, y0, c), plot(x, y1, c);
            for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
                plot(x0, y, c), plot(x1, y, c);
        }
        void frect(int x0, int y0, int x1, int y1, unsigned c)
        {
            for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
                for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
                    plot(x, y, c);
        }
        void circle(int cx, int cy, int r, unsigned c)
        {
            int x = r, y = 0, d = 1 - r;
            while (x >= y)
            {
                for (const auto &[a, b2] : {std::pair{x, y}, std::pair{y, x}})
                    plot(cx + a, cy + b2, c), plot(cx - a, cy + b2, c), plot(cx + a, cy - b2, c), plot(cx - a, cy - b2, c);
                ++y;
                if (d < 0)
                    d += 2 * y + 1;
                else
                {
                    --x;
                    d += 2 * (y - x) + 1;
                }
            }
        }
        void flood(int x, int y, unsigned c)
        {
            const unsigned t = point(x, y);
            if (t == c || off(x, y))
                return;
            std::vector<std::pair<int, int>> s{{x, y}};
            while (!s.empty())
            {
                auto [px, py] = s.back();
                s.pop_back();
                if (off(px, py) || point(px, py) != t)
                    continue;
                plot(px, py, c);
                s.push_back({px + 1, py});
                s.push_back({px - 1, py});
                s.push_back({px, py + 1});
                s.push_back({px, py - 1});
            }
        }
    };

    /// What the library drew from card page `page`, against the reference: where they first differ.
    bool same_bmp(Rig &r, unsigned page, const Bmp &ref, const char *what)
    {
        const std::vector<uint8_t> got = cmem(r, page * 256u, ref.b.size());
        if (got == ref.b)
            return true;
        size_t i = 0;
        while (got[i] == ref.b[i])
            ++i;
        std::printf("  %s: byte %zu (row %zu) $%02X, want $%02X\n", what, i, i / ref.row, got[i], ref.b[i]);
        return false;
    }

    void xy(CcpRig &r, int x, int y, int x2 = 0, int y2 = 0)
    {
        word(r, "GX_X", static_cast<unsigned>(x));
        word(r, "GX_Y", static_cast<unsigned>(y));
        word(r, "GX_X2", static_cast<unsigned>(x2));
        word(r, "GX_Y2", static_cast<unsigned>(y2));
    }

    /// The same drawn by the library and the reference, at mode `mode`, from card page `page`.
    void draw_all(CcpRig &r, unsigned mode, unsigned page, Bmp &ref)
    {
        const unsigned c1 = mode == 2 ? 1 : 5, c2 = mode == 2 ? 1 : 9, c3 = mode == 2 ? 0 : 3;
        const int w = static_cast<int>(ref.w), h = static_cast<int>(ref.h);
        bmp_at(r, mode, page);
        xy(r, 0, 0, w - 1, h - 1); // the whole bitmap cleared
        gx(r, 'F', 0, 0, 0, ms(40000));
        ref.frect(0, 0, w - 1, h - 1, 0);
        for (const auto &[x, y] : std::vector<std::pair<int, int>>{{0, 0}, {7, 3}, {100, 50}, {w - 1, h - 1}, {w, 5}})
        {
            xy(r, x, y);
            gx(r, 'P', static_cast<uint8_t>(c1));
            ref.plot(x, y, c1);
        }
        xy(r, 10, 10, 150, 90); // lines: shallow, steep and backwards, off the edge
        gx(r, 'N', static_cast<uint8_t>(c2));
        ref.line(10, 10, 150, 90, c2);
        xy(r, 20, 200, 30, 12);
        gx(r, 'N', static_cast<uint8_t>(c1));
        ref.line(20, 200, 30, 12, c1);
        xy(r, 200, 100, w + 20, 130);
        gx(r, 'N', static_cast<uint8_t>(c2));
        ref.line(200, 100, w + 20, 130, c2);
        xy(r, 40, 120, 90, 160);
        gx(r, 'R', static_cast<uint8_t>(c2));
        ref.rect(40, 120, 90, 160, c2);
        xy(r, 60, 130, 70, 150); // inside the rectangle, a filled one
        gx(r, 'F', static_cast<uint8_t>(c1));
        ref.frect(60, 130, 70, 150, c1);
        xy(r, 0, 220, w - 1, 225); // a band the full width
        gx(r, 'F', static_cast<uint8_t>(c2));
        ref.frect(0, 220, w - 1, 225, c2);
        xy(r, 180, 60);
        word(r, "GX_R", 30);
        gx(r, 'C', static_cast<uint8_t>(c1));
        ref.circle(180, 60, 30, c1);
        xy(r, 180, 60); // the circle's inside flooded
        gx(r, 'O', static_cast<uint8_t>(c3 ? c3 : 1), 0, 0, ms(30000));
        ref.flood(180, 60, c3 ? c3 : 1);
        xy(r, 50, 125); // the rectangle's inside, round the filled one
        gx(r, 'O', static_cast<uint8_t>(c3 ? 7 : 1), 0, 0, ms(30000));
        ref.flood(50, 125, c3 ? 7 : 1);
        xy(r, 7, 3);
        CHECK_EQ(hexa(gx(r, 'p')), ref.point(7, 3));
    }

    /// VIEW with B:name83 (an asset check_mkasset.py wrote), to its "Press a key" line.
    size_t view(CcpRig &r, const std::string &file, const std::string &name83)
    {
        const std::vector<uint8_t> asset = read_bytes(UTILS + "/assets/" + file);
        CHECK(asset.size() > 1024);
        put_b(r, name83, asset);
        r.put_util("view");
        no_splash(r);
        const size_t from = r.out.size();
        r.type("M:VIEW B:" + name83.substr(0, name83.find(' ')) + "\r");
        const bool said = r.until("Press a key to stop.", ms(8000), from);
        CHECK(said);
        if (!said)
            r.tail();
        r.run(ms(40));
        return from;
    }
    void key_back(CcpRig &r, size_t from)
    {
        r.console->key('x');
        CHECK(r.until("\r\nA>", ms(2000), from));
        CHECK_EQ(vreg(r, 0x10), 0x80); // the console's screen back
        CHECK_EQ(vreg2(r, 0x0A), 0u);  // PALBASE: the console's palette
    }
} // namespace

TEST(view_shows_a_bitmap_256_as_its_png_says)
{
    CcpRig r;
    REQUIRE(r.ok);
    const size_t from = view(r, "VIEW8.OGX", "VIEW8   OGX");
    const std::vector<uint8_t> asset = read_bytes(UTILS + "/assets/VIEW8.OGX");
    const unsigned pal = vreg2(r, 0x0A), map = vreg2(r, 0x11);
    std::printf("  PALBASE $%04X, layer 0's MAPBASE $%04X, CONFIG $%02X, CTRL $%02X\n", pal, map, vreg(r, 0x10), vreg(r, 0x00));
    CHECK(pal >= PAGE0 && map >= PAGE0); // in the program's card memory
    CHECK_EQ(vreg(r, 0x10), 3); // bitmap 256
    CHECK_EQ(vreg(r, 0x00), 0x03);
    const std::vector<uint8_t> pb(asset.begin() + 1024, asset.begin() + 1024 + 512);
    const std::vector<uint8_t> px(asset.begin() + 2048, asset.begin() + 2048 + 256 * 240);
    const uint32_t *f = vid(r).frame();
    unsigned wrong = 0;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 256; ++x)
        {
            const unsigned i = px[y * 256 + x];
            const uint32_t want = VideoCard::rgba(static_cast<uint16_t>((pb[2 * i + 1] & 15) << 8 | pb[2 * i]));
            wrong += f[(2 * y) * 640 + 64 + 2 * x] != want;
        }
    CHECK_EQ(wrong, 0u);
    key_back(r, from);
}

TEST(view_shows_a_bitmap_512_tiles_and_sprites)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        const size_t from = view(r, "VIEW4.OGX", "VIEW4   OGX");
        const std::vector<uint8_t> asset = read_bytes(UTILS + "/assets/VIEW4.OGX");
        CHECK_EQ(vreg(r, 0x10), 4); // bitmap 512
        const unsigned map = vreg2(r, 0x11), pal = vreg2(r, 0x0A);
        const std::vector<uint8_t> shown = cmem(r, map * 256u, 256 * 480);
        CHECK(shown == std::vector<uint8_t>(asset.begin() + 2048, asset.begin() + 2048 + 256 * 480)); // 120 KB: two loads
        const uint32_t *f = vid(r).frame();
        unsigned wrong = 0;
        for (unsigned y = 0; y < 480; y += 5)
            for (unsigned x = 0; x < 512; ++x)
                wrong += f[y * 640 + 64 + x] != VideoCard::rgba(entry(r, pal, (shown[y * 256 + x / 2] >> (x & 1 ? 0 : 4)) & 15));
        CHECK_EQ(wrong, 0u);
        key_back(r, from);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        const size_t from = view(r, "TILES.OGX", "TILES   OGX");
        CHECK_EQ(vreg(r, 0x10) & 7, 2); // tiles
        const unsigned map = vreg2(r, 0x11), tiles = vreg2(r, 0x13), pal = vreg2(r, 0x0A);
        CHECK(map != tiles && map >= PAGE0 && tiles >= PAGE0);
        const uint32_t *f = vid(r).frame();
        unsigned wrong = 0;
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 128; ++x)
            {
                const uint32_t ea = map * 256 + ((y / 8) * 32 + x / 8) * 2;
                const unsigned e = card(r, ea) | (card(r, ea + 1) & 3) << 8, p = (y % 8) * 8 + x % 8;
                const unsigned v = (card(r, tiles * 256 + e * 32 + p / 2) >> (p & 1 ? 0 : 4)) & 15;
                wrong += f[(2 * y) * 640 + 2 * x] != VideoCard::rgba(entry(r, pal, v));
            }
        CHECK_EQ(wrong, 0u);
        key_back(r, from);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        const size_t from = view(r, "SHIP.OGX", "SHIP    OGX");
        CHECK(vreg(r, 0x00) & 0x08); // sprites on
        const unsigned table = vreg2(r, 0x0C);
        // Sprite 1's image moved on by the images' first page (on a unit, 4 pages): a page past a multiple of 4.
        const unsigned img = card(r, table * 256 + 8) | (card(r, table * 256 + 9) & 0x7F) << 8;
        std::printf("  the table at card page $%04X, sprite 1's image at page $%04X\n", table, img);
        CHECK(img >= PAGE0);
        CHECK_EQ(img % 4, 1u);
        const uint32_t *f = vid(r).frame();
        const unsigned sx = 8 + 20 + 8, sy = 8 + 8; // sprite 1 at x 28, y 8: its inside, from 3 to 12, green
        CHECK_EQ(f[(2 * sy) * 640 + 2 * sx], VideoCard::rgba(0x0F0));
        key_back(r, from);
    }
}

TEST(view_s_refusals)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_util("view");
    r.put_m({{0, "TEXT    OGX", bytes(std::string(300, 'x'))}});
    CHECK(r.cmd("M:VIEW").find("VIEW filespec") != std::string::npos);
    CHECK(r.cmd("M:VIEW M:NONE").find("No File") != std::string::npos);
    CHECK(r.cmd("M:VIEW M:TEXT").find("Not an asset file.") != std::string::npos);
    CHECK_EQ(vreg(r, 0x10), 0x80); // the console's screen throughout
}

TEST(plot_line_rectangles_circle_and_flood_at_8_4_and_1_bits)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    for (const unsigned mode : {0u, 1u, 2u})
    {
        const unsigned pages = mode == 0 ? 240 : mode == 1 ? 480 : 150; // 60 KB, 120 KB, 37.5 KB
        const std::string a = valloc(r, pages, 64);                        // on 16 KB
        REQUIRE(a.substr(6) == "00");
        const unsigned page = hexax(a);
        std::printf("  mode %u: %u card pages from $%04X\n", mode, pages, page);
        CHECK(page >= PAGE0 && page % 64 == 0);
        Bmp ref(mode);
        draw_all(r, mode, page, ref);
        CHECK(same_bmp(r, page, ref, mode == 0 ? "bitmap 256" : mode == 1 ? "bitmap 512" : "mono"));
        vfree(r, page, pages);
    }
    // The pages: the first unit for one page (a unit, 4 pages, the grain), eight units aligned to 8 after it,
    // the next single unit back in the gap, and no run of the whole 1.5 MB left; a size of 0 refused.
    CHECK_EQ(hexax(valloc(r, 1)), PAGE0);
    CHECK_EQ(hexax(valloc(r, 32, 32)), PAGE0 + 32);
    CHECK_EQ(hexax(valloc(r, 4)), PAGE0 + 4);
    CHECK_EQ(valloc(r, 6144).substr(6), std::string("01"));
    const std::string none = valloc(r, 0);
    CHECK_EQ(none.substr(0, 2) + none.substr(5), std::string("03 01")); // GE_MEMORY
    // The last unit of the program's part, and past it: not on the card's room.
    CHECK_EQ(valloc(r, 6144 - 64, 1).substr(6), std::string("00")); // all but the first 16 KB
    CHECK_EQ(valloc(r, 4).substr(6), std::string("00"));             // the gap's last units
    // W1: slot 1's block 33, banks 9-B's 36-47 and block 1, not 32 (slot 0's, the console's) or 2.
    CHECK_EQ(gx(r, 'b', 33).substr(6), std::string("00"));
    CHECK_EQ(gx(r, 'b', 36).substr(6), std::string("00"));
    CHECK_EQ(gx(r, 'b', 32).substr(6), std::string("01"));
    CHECK_EQ(gx(r, 'b', 2).substr(6), std::string("01"));
    // GX_VMAP: a page's bank in slot 1 and block 33 in W1, its CPU address back; OODOS's and Apple II mode's
    // pages refused, the slot left as it was.
    CHECK_EQ(gx(r, 'm', 0x41, 0x05), std::string("00 41 00")); // page $0541: card $054100, bank 21, $4100
    CHECK_EQ(vreg2(r, 0x32), 21u);
    CHECK_EQ(r.phys(at("GXCUR")), 33); // the library's W1: slot 1's block
    CHECK_EQ(gx(r, 'm', 0x00, 0x00).substr(6), std::string("01"));
    CHECK_EQ(gx(r, 'm', 0xFF, 0x03).substr(6), std::string("01"));
    CHECK_EQ(gx(r, 'm', 0x00, 0x1C).substr(6), std::string("01"));
    CHECK_EQ(vreg2(r, 0x32), 21u);
    gx(r, 'w');
    CHECK_EQ(r.phys(at("GXCUR")), 1);
}

TEST(the_library_refuses_to_map_below_interface_8)
{
    CcpRig r;
    REQUIRE(r.ok);
    // FT_INFO's answer made 7 in OODOS's block 60, as an OODOS for revision 1's card gives it: its LDA #12
    // (interface 12 since function 83's DM_BANK8, B7), LDX #$0F.
    const uint32_t base = 60u * BLOCK - 0x4000u + sym("FT_INFOX");
    uint32_t p = base;
    while (p < base + 64 && !(r.phys(p) == 0xA9 && r.phys(p + 1) == 12 && r.phys(p + 2) == 0xA2 && r.phys(p + 3) == 0x0F))
        ++p;
    REQUIRE(p < base + 64);
    r.poke(p + 1, 7);
    start_gx(r);
    const std::string i = gx(r, 'I');
    CHECK_EQ(i.substr(0, 2) + i.substr(5), std::string("01 01")); // GE_VERSION, carry set
    CHECK_EQ(gx(r, 'b', 33).substr(6), std::string("01"));
    const unsigned slot1 = vreg2(r, 0x32);
    CHECK_EQ(gx(r, 'm', 0x00, 0x05).substr(6), std::string("01")); // nor the slot
    CHECK_EQ(vreg2(r, 0x32), slot1);
}

TEST(tiles_maps_scrolling_sprites_and_collisions)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    const unsigned map = alloc(r, 16, 4);
    gx(r, 'M', static_cast<uint8_t>(map), static_cast<uint8_t>(map >> 8), 32);
    word(r, "GX_V", 0x1234);
    gx(r, 'W', 0, 32); // 32 rows of $1234
    std::vector<uint8_t> want;
    for (int i = 0; i < 1024; ++i)
        want.push_back(0x34), want.push_back(0x12);
    CHECK(cmem(r, map * 256u, 2048) == want);
    word(r, "GX_V", 0x0ABC);
    gx(r, 'U', 0, 5, 7); // column 5, row 7
    CHECK_EQ(card(r, map * 256 + (7 * 32 + 5) * 2), 0xBC);
    CHECK_EQ(card(r, map * 256 + (7 * 32 + 5) * 2 + 1), 0x0A);
    CHECK_EQ(gx(r, 'u', 0, 5, 7), std::string("BC 0A 00"));
    CHECK_EQ(gx(r, 'u', 0, 6, 7), std::string("34 12 00"));
    xy(r, 300, 17);
    gx(r, 'Q', 1);
    CHECK_EQ(vreg2(r, 0x25), 300u); // layer 1's HSCROLL and VSCROLL
    CHECK_EQ(vreg2(r, 0x27), 17u);
    // Sprites: the shadow on the card only after GX_SPRSHOW and the next vertical blank.
    gx(r, 'Z');
    const unsigned table = vreg2(r, 0x0C);
    CHECK_EQ(table, map + 16); // the next unit after the map's four
    xy(r, 100, 50);
    gx(r, 'X', 5);
    word(r, "GX_V", 0x8012);
    gx(r, 'V', 5);
    gx(r, 'Y', 5, 0x1C, 0x50); // group 1, depth 3; 16 x 16
    r.run(ms(40));
    CHECK_EQ(card(r, table * 256 + 5 * 8 + 2), 0); // not yet
    gx(r, 'z');
    r.run(ms(40));
    CHECK(cmem(r, table * 256u + 40, 8) == (std::vector<uint8_t>{0x12, 0x80, 100, 0, 50, 0, 0x1C, 0x50}));
    CHECK_EQ(r.phys(AREA + 12), 0); // the go bit cleared
    // Two sprites of group 1 on one spot: the card's collision, read and cleared by GX_COLL.
    const unsigned img = alloc(r, 1);
    CHECK_EQ(img, table + 4); // review F1: the table took one unit (512 bytes), not 512 pages
    cpoke(r, img * 256, std::vector<uint8_t>(256, 0x11));
    for (const uint8_t s : {uint8_t{6}, uint8_t{7}})
    {
        xy(r, 200, 100);
        gx(r, 'X', s);
        word(r, "GX_V", img); // the image's card page
        gx(r, 'V', s);
        gx(r, 'Y', s, 0x1C, 0x50);
    }
    gx(r, 'z');
    gx(r, 'S', 0x08);
    r.run(ms(60));
    const std::string c = gx(r, 'x');
    CHECK_EQ(c.substr(0, 2), std::string("01")); // group 1
    CHECK_EQ(c.substr(6), std::string("01"));    // the flag, then cleared
    // While the two still overlap, each frame's vertical blank sets the flag again, so whether it reads clear
    // here depends only on where in the frame GX_COLL ran. Part them, let the last colliding frame's flag
    // arrive and clear it, and then it stays clear.
    xy(r, 400, 300);
    gx(r, 'X', 7);
    gx(r, 'z');
    r.run(ms(40));
    gx(r, 'x');
    r.run(ms(20));
    CHECK_EQ(gx(r, 'x').substr(6), std::string("00")); // no collision since
    CHECK_EQ(vreg(r, 0x05) & 0x04, 0);
}

TEST(a_fade_text_and_double_buffering)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    CHECK_EQ(gx(r, 'K').substr(6), std::string("00"));
    const unsigned pal = vreg2(r, 0x0A);
    CHECK(pal >= PAGE0);
    gx(r, 'E', 1, 0x00, 0x0F); // entry 1 red, then faded to green over 8 frames
    gx(r, 'H');
    r.run(ms(40));
    CHECK_EQ(entry(r, pal, 1), 0xF00);
    std::vector<uint8_t> target(512, 0);
    target[2] = 0xF0; // entry 1: green 15
    r.poke(0x3A00, target);
    word(r, "GX_V", 0x3A00);
    std::vector<unsigned> reds, greens;
    const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
    uint8_t last = r.phys(frames);
    r.watch = [&](uint16_t) {
        if (r.phys(frames) != last)
        {
            last = r.phys(frames);
            reds.push_back(entry(r, pal, 1) >> 8);
            greens.push_back(entry(r, pal, 1) >> 4 & 15);
        }
    };
    gx(r, 'D', 8);
    r.watch = nullptr;
    std::printf("  the fade's red:");
    for (unsigned v : reds)
        std::printf(" %u", v);
    std::printf("; green:");
    for (unsigned v : greens)
        std::printf(" %u", v);
    std::printf("\n");
    CHECK_EQ(entry(r, pal, 1), 0x0F0);
    CHECK(std::is_sorted(reds.rbegin(), reds.rend()) && std::is_sorted(greens.begin(), greens.end()));
    CHECK(std::set<unsigned>(greens.begin(), greens.end()).size() >= 6); // a step a frame
    // Text: the console's font, opaque, on a bitmap 256.
    const unsigned bmp = alloc(r, 240);
    CHECK_EQ(bmp, pal + 4); // review F1: the palette took one unit (512 bytes), not 512 pages
    bmp_at(r, 0, bmp);
    gx(r, 'G', 0, 0);
    r.poke(at("GX_FG"), 15);
    r.poke(at("GX_BG"), 1);
    r.poke(at("GX_OPAQUE"), 1);
    r.poke(TEXT, std::vector<uint8_t>{'O', 'K', 0});
    xy(r, 10, 20);
    gx(r, 'T', static_cast<uint8_t>(TEXT), static_cast<uint8_t>(TEXT >> 8));
    Bmp ref(0);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned row = 0; row < 16; ++row)
        {
            const uint8_t bits = card(r, 0x000400 + (c ? 'K' : 'O') * 16u + row); // the console's font
            for (unsigned b = 0; b < 8; ++b)
                ref.plot(static_cast<int>(10 + c * 8 + b), static_cast<int>(20 + row), bits & (0x80 >> b) ? 15 : 1);
        }
    const std::vector<uint8_t> got = cmem(r, bmp * 256u, 256 * 40);
    CHECK(std::equal(got.begin() + 20 * 256, got.begin() + 36 * 256, ref.b.begin() + 20 * 256));
    CHECK_EQ(r.phys(at("GX_X")), 26); // moved on 8 a character
    // Double buffering: buffer A all 1, B all 2; a flip shows B from a frame's start, never half.
    const unsigned b2 = alloc(r, 240);
    xy(r, 0, 0, 255, 239);
    gx(r, 'F', 1);
    bmp_at(r, 0, b2);
    gx(r, 'F', 2);
    word(r, "GX_V", bmp); // layer 0: bitmap 256 at A
    word(r, "GX_R", 0);
    gx(r, 'L', 0, 3);
    gx(r, 'S', 0x02);
    r.run(ms(40));
    unsigned torn = 0, a_frames = 0, b_frames = 0;
    uint64_t seen = vid(r).frames();
    const uint32_t ca = vid(r).frame()[100 * 640 + 300];
    r.watch = [&](uint16_t) {
        if (vid(r).frames() == seen)
            return;
        seen = vid(r).frames();
        const uint32_t *f = vid(r).frame();
        const uint32_t top = f[2 * 640 + 300], bottom = f[476 * 640 + 300];
        torn += top != bottom;
        (top == ca ? a_frames : b_frames) += 1;
    };
    word(r, "GX_V", b2);
    gx(r, 'J', 0);
    r.run(ms(60));
    r.watch = nullptr;
    std::printf("  the flip: %u frames of A, %u of B, %u torn\n", a_frames, b_frames, torn);
    CHECK_EQ(torn, 0u);
    CHECK(b_frames >= 3);
    CHECK_EQ(vreg2(r, 0x11), b2);
}

TEST(empty_and_out_of_bitmap_copies_and_fills_do_nothing_or_are_refused)
{
    // Review F2: a width or a row count of 0 is nothing, never function 83's 0 (65,536); a copy whose rectangle
    // leaves the bitmap is refused (a copy isn't clipped as a draw is). A sentinel unit after each is untouched.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    const unsigned bmp = alloc(r, 240), sentinel = alloc(r, 256);
    CHECK_EQ(sentinel, bmp + 240); // the next free run
    const std::vector<uint8_t> marks = std::vector<uint8_t>(65536, 0x5A);
    cpoke(r, sentinel * 256u, marks);
    bmp_at(r, 0, bmp);
    xy(r, 0, 0, 0, 10); // 0 wide
    word(r, "GX_DX", 0);
    word(r, "GX_DY", 100);
    CHECK_EQ(gx(r, 'y').substr(6), std::string("00"));
    xy(r, 0, 0, 10, 0); // 0 high
    CHECK_EQ(gx(r, 'y').substr(6), std::string("00"));
    xy(r, 200, 0, 100, 10); // past the row's end
    const std::string past = gx(r, 'y');
    CHECK_EQ(past.substr(0, 2) + past.substr(5), std::string("08 01")); // GE_BOUNDS
    xy(r, 0, 230, 10, 20); // past the last row
    CHECK_EQ(gx(r, 'y').substr(6), std::string("01"));
    xy(r, 0, 0, 10, 10); // to past the last row
    word(r, "GX_DY", 235);
    CHECK_EQ(gx(r, 'y').substr(6), std::string("01"));
    bmp_at(r, 1, bmp); // 4 bits: width 0 is even, and still nothing
    xy(r, 0, 0, 0, 10);
    word(r, "GX_DY", 100);
    CHECK_EQ(gx(r, 'y').substr(6), std::string("00"));
    xy(r, 1, 0, 10, 10); // an odd x at 4 bits: GE_BOUNDS (review R3)
    std::string odd = gx(r, 'y');
    CHECK_EQ(odd.substr(0, 2) + odd.substr(5), std::string("08 01"));
    bmp_at(r, 2, bmp); // mono: no copy, GE_BOUNDS
    xy(r, 0, 0, 8, 8);
    odd = gx(r, 'y');
    CHECK_EQ(odd.substr(0, 2) + odd.substr(5), std::string("08 01"));
    CHECK(cmem(r, sentinel * 256u, 65536) == marks);
    // A map filled with 0 rows: nothing.
    const unsigned map = alloc(r, 16, 4);
    const unsigned after = map + 16;
    cpoke(r, after * 256u, std::vector<uint8_t>(4096, 0xA5));
    gx(r, 'M', static_cast<uint8_t>(map), static_cast<uint8_t>(map >> 8), 32);
    word(r, "GX_V", 0x1234);
    gx(r, 'W', 0, 0);
    CHECK(cmem(r, after * 256u, 4096) == std::vector<uint8_t>(4096, 0xA5));
    CHECK(cmem(r, map * 256u, 64) == std::vector<uint8_t>(64, 0)); // the map itself untouched
}

TEST(no_event_area_refuses_the_shows_and_leaves_zero_page_alone)
{
    // Review F6: with GX_INIT(0) the TSB had no area to hit but the program's zero page byte $0C.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    CHECK_EQ(gx(r, 'i').substr(6), std::string("00"));
    CHECK_EQ(gx(r, 'K').substr(6), std::string("00")); // the palette's slot set: that much needs no area
    gx(r, 'Z');                                         // and the sprites' (both allocate: GXT, now at $0C, moves)
    CHECK_EQ(vreg2(r, 0x0A), PAGE0);                    // a unit each (review F1)
    CHECK_EQ(vreg2(r, 0x0C), PAGE0 + 4);
    CHECK_EQ(hexax(valloc(r, 1)), PAGE0 + 8);
    const uint8_t z = r.phys(0x000C);
    std::string a = gx(r, 'H');
    CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("07 01")); // GE_AREA
    a = gx(r, 'z');
    CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("07 01"));
    word(r, "GX_V", 0x3A00);
    a = gx(r, 'D', 4);
    CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("07 01"));
    CHECK_EQ(r.phys(0x000C), z);
}

namespace
{
    /// A one-part asset file of our own making: its header's part as given, the data after it.
    /// (Format 4: a part's place a card page, 2 bytes, $FFFF any.)
    std::vector<uint8_t> asset1(uint8_t type, unsigned page, unsigned record, unsigned records, uint32_t bytes,
                                const std::vector<std::vector<uint8_t>> &more = {})
    {
        std::vector<uint8_t> f(1024, 0);
        f[0] = 'O', f[1] = 'G', f[2] = 'X', f[3] = 4, f[4] = static_cast<uint8_t>(1 + more.size());
        auto part = [&](unsigned i, const std::vector<uint8_t> &p) { std::copy(p.begin(), p.end(), f.begin() + 16 + 16 * i); };
        part(0, {type, 0, static_cast<uint8_t>(page), static_cast<uint8_t>(page >> 8), static_cast<uint8_t>(record), static_cast<uint8_t>(record >> 8), static_cast<uint8_t>(records),
                 static_cast<uint8_t>(records >> 8), static_cast<uint8_t>(bytes), static_cast<uint8_t>(bytes >> 8),
                 static_cast<uint8_t>(bytes >> 16), 0, 0, 0, 0, 0});
        for (size_t i = 0; i < more.size(); ++i)
            part(static_cast<unsigned>(i + 1), more[i]);
        f.resize(1024 + 9 * 1024, 0x77); // data: 9 KB of $77
        return f;
    }
    std::string asset(CcpRig &r, const std::string &name83)
    {
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = 13; // M:
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        r.poke(0x3A80, fcb);
        return gx(r, '@', 0x80, 0x3A);
    }
} // namespace

TEST(card_memory_put_got_copied_and_filled_through_function_83)
{
    // Review F8: GX_VADDR, GX_VPUT, GX_VGET, GX_VCOPY and GX_VFILL (vmem.inc), unaligned and across a 16 KB
    // bank; GX_A kept; a length of 0 nothing; OODOS's part and Apple II mode's refused with nothing written.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    CHECK_EQ(gx(r, 'd', 0x41, 0x05).substr(0, 5), std::string("41 05")); // A/X kept
    CHECK_EQ(addr3(r, "GX_A"), 0x054100u);
    const unsigned page = alloc(r, 192, 64); // 48 KB on 16 KB: three banks
    CHECK(page >= PAGE0 && page % 64 == 0);
    const uint32_t base = page * 256u, at_a = base + 0x3F00 + 0x83; // 300 bytes from $..3F83: over the bank's end
    cpoke(r, base, std::vector<uint8_t>(0xC000, 0xEE));
    std::vector<uint8_t> src(300);
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 7 + 3);
    constexpr uint16_t SRC = 0x3A01, DST = 0x3C03; // the program's memory, past gxtest's, unaligned
    r.poke(SRC, src);
    // GX_VPUT: the program's bytes to the card, and not a byte either side.
    addr3(r, "GX_A", at_a);
    word(r, "GX_V", 300);
    CHECK_EQ(gx(r, 'v', SRC & 0xFF, SRC >> 8).substr(6), std::string("00"));
    CHECK_EQ(addr3(r, "GX_A"), at_a);
    CHECK(cmem(r, at_a, 300) == src);
    CHECK_EQ(card(r, at_a - 1), 0xEE);
    CHECK_EQ(card(r, at_a + 300), 0xEE);
    // GX_VGET: back to the program's memory.
    r.poke(DST, std::vector<uint8_t>(302, 0x11));
    CHECK_EQ(gx(r, 'g', DST & 0xFF, DST >> 8).substr(6), std::string("00"));
    CHECK_EQ(addr3(r, "GX_A"), at_a);
    std::vector<uint8_t> got(300);
    for (unsigned i = 0; i < 300; ++i)
        got[i] = r.phys(DST + i);
    CHECK(got == src);
    CHECK_EQ(r.phys(DST + 300u), 0x11);
    // GX_VCOPY: card to card, the destination over the third bank's start.
    const uint32_t to = base + 0x7FC1;
    addr3(r, "GX_C", to);
    CHECK_EQ(gx(r, 'k').substr(6), std::string("00"));
    CHECK_EQ(addr3(r, "GX_A"), at_a);
    CHECK(cmem(r, to, 300) == src);
    CHECK_EQ(card(r, to + 300), 0xEE);
    // GX_VFILL: 1,000 bytes of $C3 over the boundary.
    const uint32_t fill = base + 0x3E05;
    addr3(r, "GX_A", fill);
    word(r, "GX_V", 1000);
    CHECK_EQ(gx(r, 'f', 0xC3).substr(6), std::string("00"));
    CHECK_EQ(addr3(r, "GX_A"), fill);
    CHECK(cmem(r, fill, 1000) == std::vector<uint8_t>(1000, 0xC3));
    CHECK_EQ(card(r, fill - 1), 0xEE);
    CHECK_EQ(card(r, fill + 1000), 0xEE);
    // A length of 0: nothing, carry clear (never function 83's 65,536).
    const std::vector<uint8_t> before = cmem(r, base, 0xC000);
    addr3(r, "GX_A", base);
    addr3(r, "GX_C", base + 0x100);
    word(r, "GX_V", 0);
    r.poke(DST, std::vector<uint8_t>(16, 0x22));
    for (const char c : {'v', 'g', 'k', 'f'})
    {
        const uint8_t a = c == 'f' ? uint8_t{0x99} : static_cast<uint8_t>(DST), x = c == 'f' ? uint8_t{0} : static_cast<uint8_t>(DST >> 8);
        CHECK_EQ(gx(r, c, a, x).substr(6), std::string("00"));
        CHECK_EQ(addr3(r, "GX_A"), base);
    }
    CHECK(cmem(r, base, 0xC000) == before);
    CHECK_EQ(r.phys(DST), 0x22);
    // Refused (GE_DMA, carry set), nothing written: OODOS's part, Apple II mode's, and a run into it.
    word(r, "GX_V", 64);
    for (const uint32_t bad : {0x000100u, 0x03FFF0u, 0x1C0000u, 0x1BFFF0u})
    {
        const std::vector<uint8_t> was = cmem(r, bad, 64);
        addr3(r, "GX_A", bad);
        std::string a = gx(r, 'v', SRC & 0xFF, SRC >> 8);
        CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("04 01"));
        CHECK_EQ(addr3(r, "GX_A"), bad);
        a = gx(r, 'f', 0x55);
        CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("04 01"));
        addr3(r, "GX_A", base); // a good source, the bad destination
        addr3(r, "GX_C", bad);
        a = gx(r, 'k');
        CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("04 01"));
        if (cmem(r, bad, 64) != was)
            std::printf("  card $%06X written\n", bad);
        CHECK(cmem(r, bad, 64) == was);
    }
    // But a source anywhere in the fitted memory: OODOS's font back to the program.
    addr3(r, "GX_A", 0x000400);
    word(r, "GX_V", 16);
    CHECK_EQ(gx(r, 'g', DST & 0xFF, DST >> 8).substr(6), std::string("00"));
    for (unsigned i = 0; i < 16; ++i)
        CHECK_EQ(r.phys(DST + i), card(r, 0x000400 + i));
}

TEST(an_asset_s_header_is_checked_and_what_it_took_is_freed)
{
    // Review F5: the file is untrusted. Bytes 1,024 in 64 records (8 KB), a bad type, a record off 1 KB, a named
    // page already taken, OODOS's card memory, a page off a unit: each refused, a sentinel after the pages
    // untouched; a second part refused frees the first's pages.
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "LONG    OGX", asset1(8, 0xFFFF, 8, 64, 1024)},
             {0, "TYPE    OGX", asset1(9, 0xFFFF, 8, 8, 1024)},
             {0, "ODD     OGX", asset1(8, 0xFFFF, 9, 8, 1024)},
             {0, "TAKEN   OGX", asset1(8, 0x0500, 8, 8, 1024)},
             {0, "CONSOLE OGX", asset1(8, 0x0030, 8, 8, 1024)},
             {0, "APPLE   OGX", asset1(8, 0x1C00, 8, 8, 1024)},
             {0, "UNIT    OGX", asset1(8, 0x0601, 8, 8, 1024)},
             {0, "SECOND  OGX", asset1(8, 0xFFFF, 8, 8, 1024, {{9, 0, 0xFF, 0xFF, 16, 0, 8, 0, 0, 4, 0, 0, 0, 0, 0, 0}})},
             {0, "GOOD    OGX", asset1(8, 0xFFFF, 8, 8, 1024)}});
    start_gx(r);
    gx(r, 'I');
    const unsigned taken = alloc(r, 32, 32); // units 0-7
    CHECK_EQ(taken, PAGE0);
    const unsigned held = alloc(r, 4); // unit 8: the first one-unit run
    CHECK_EQ(held, PAGE0 + 32);
    cpoke(r, 0x042400, std::vector<uint8_t>(2048, 0x5A)); // units 9 and 10
    for (const char *name : {"LONG    OGX", "TYPE    OGX", "ODD     OGX", "SECOND  OGX"})
    {
        const std::string a = asset(r, name);
        std::printf("  %s: %s\n", name, a.c_str());
        CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("06 01")); // GE_FORMAT
    }
    for (const char *name : {"CONSOLE OGX", "APPLE   OGX", "UNIT    OGX"})
    {
        const std::string a = asset(r, name);
        std::printf("  %s: %s\n", name, a.c_str());
        CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("03 01")); // GE_MEMORY
    }
    // TAKEN names page $0500, free: taken, its bytes there; again, refused.
    CHECK_EQ(asset(r, "TAKEN   OGX").substr(6), std::string("00"));
    CHECK(cmem(r, 0x050000, 1024) == std::vector<uint8_t>(1024, 0x77));
    std::string a = asset(r, "TAKEN   OGX");
    CHECK_EQ(a.substr(0, 2) + a.substr(5), std::string("03 01"));
    CHECK_EQ(hexax(valloc(r, 4)), PAGE0 + 36u); // unit 9: nothing the refused files took kept
    CHECK(cmem(r, 0x042800, 1024) == std::vector<uint8_t>(1024, 0x5A));
    // A good one loads its 1,024 bytes into unit 10, and no further.
    CHECK_EQ(asset(r, "GOOD    OGX").substr(6), std::string("00"));
    CHECK(cmem(r, 0x042800, 1024) == std::vector<uint8_t>(1024, 0x77));
    CHECK_EQ(card(r, 0x042C00), 0);
}

TEST(a_filled_rectangle_wholly_off_the_bitmap_draws_nothing_at_once)
{
    // Review R1: a clip that leaves nothing (lo past hi) once ran a row's loop round 65,536 pixels. Off each edge,
    // at each depth: nothing drawn, and each call short.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    const unsigned page = alloc(r, 480);
    for (const unsigned mode : {0u, 1u, 2u})
    {
        const int w = mode == 0 ? 256 : mode == 1 ? 512 : 640, h = mode == 0 ? 240 : 480;
        const size_t bytes = mode == 2 ? 80u * 480 : 256u * static_cast<unsigned>(h);
        bmp_at(r, mode, page);
        cpoke(r, page * 256u, std::vector<uint8_t>(bytes, 0));
        for (const auto &[x0, y0, x1, y1] : std::vector<std::array<int, 4>>{{w + 10, 20, w + 30, 40},    // right
                                                                            {-20, 20, -5, 40},           // left
                                                                            {20, h + 10, 40, h + 30},    // below
                                                                            {20, -20, 40, -5},           // above
                                                                            {w + 1, h + 1, w + 9, h + 9}}) // past both
        {
            xy(r, x0, y0, x1, y1);
            const uint64_t c0 = r.cpu->cycles();
            gx(r, 'F', 1, 0, 0, ms(500));
            const double took = static_cast<double>(r.cpu->cycles() - c0) / HZ * 1000;
            if (took > 60)
                std::printf("  mode %u, (%d,%d)-(%d,%d): %.1f ms\n", mode, x0, y0, x1, y1, took);
            CHECK(took < 60); // the command's line included
        }
        CHECK(cmem(r, page * 256u, bytes) == std::vector<uint8_t>(bytes, 0));
    }
}

TEST(a_layer_s_palette_group)
{
    // GX_PALOFS (the OOUI U1 review, P1): A the layer, X the group, held to 4 bits; the other layer's untouched.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    CHECK_EQ(gx(r, 'l', 1, 5).substr(6), std::string("00"));
    CHECK_EQ(vreg(r, 0x29), 5);
    CHECK_EQ(vreg(r, 0x19), 0);
    CHECK_EQ(gx(r, 'l', 0, 0x1C).substr(6), std::string("00")); // 4 bits: 12
    CHECK_EQ(vreg(r, 0x19), 12);
    CHECK_EQ(vreg(r, 0x29), 5);
    CHECK_EQ(gx(r, 'l', 3, 7).substr(6), std::string("00")); // layer 3 is layer 1 (bit 0 only), as GX_LAYER takes it
    CHECK_EQ(vreg(r, 0x29), 7);
}

// --- B7: GX_BANK8 and the two-byte W1 cache (b7-plan.md §2.4, §4 L1-L8; proposed) ---------------------------------

namespace
{
    constexpr uint16_t NB_WINH = 0xC6A0, NB_MAPREG = 0xC6A5; ///< WIN0H to MAP: $C6A0-$C6A5
    constexpr uint8_t B_RAM = 0x5B, B_CARD = 0xC5;           ///< $4000 in RAM block 1, and on the card
    constexpr uint32_t SLOT1_AT = 0x054000; ///< card page $0541's bank (21) at W1's $4000

    /// The CPU's writes to $C6A0-$C6A5 (the DMA's aren't the library's); with `program`, only those made while the
    /// program's code ($0300-$3FFF: GXTEST and the library linked in it) runs, not OODOS's own (the console's
    /// WIN1H on a revision 2 bridge), told by the rig's watch.
    struct NewRegWrites : CycleObserver
    {
        unsigned writes = 0;
        bool program = false, in_program = false;
        void on_cycle(const BusCycle &c) override
        {
            if (!c.dma && c.write && c.addr >= NB_WINH && c.addr <= NB_MAPREG && (!program || in_program))
                ++writes;
        }
        void watch(CcpRig &r)
        {
            r.watch = [this](uint16_t pc) { in_program = pc >= 0x0300 && pc < 0x4000; };
        }
    };
    /// Run until the CPU is back in GXTEST (its wait, after a command's line): W1 and WIN1H are then its own.
    void settle(CcpRig &r)
    {
        for (int i = 0; i < 400; ++i)
        {
            const uint16_t pc = r.cpu->regs().pc;
            if (pc >= 0x0300 && pc < 0x4000)
                return;
            r.run(ms(1) / 8);
        }
        CHECK(!"GXTEST's wait not reached");
    }
    uint8_t w1h(CcpRig &r)
    {
        settle(r);
        return r.bridge().winh(1);
    }
    uint8_t w1(CcpRig &r)
    {
        settle(r);
        return r.nb(1);
    }
    std::string revision_1_bridge()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const std::string line = "northbridge  backplane";
        const size_t at = text.find(line);
        if (at != std::string::npos)
            text.insert(at + line.size(), " revision=1");
        return text;
    }
    /// $4000's two bytes: RAM block 1's, and the card's under card page $0541 (GX_VMAP's bank 21).
    void mark_w1(CcpRig &r)
    {
        r.poke(0x4000, B_RAM);
        vid(r).card_poke(SLOT1_AT, B_CARD);
    }
    /// FT_INFO's answer, in OODOS's block 60, made `v` (its LDA #12, LDX #$0F).
    void ft_info_as(CcpRig &r, uint8_t v)
    {
        const uint32_t base = 60u * BLOCK - 0x4000u + sym("FT_INFOX");
        uint32_t p = base;
        while (p < base + 64 && !(r.phys(p) == 0xA9 && r.phys(p + 1) == 12 && r.phys(p + 2) == 0xA2 && r.phys(p + 3) == 0x0F))
            ++p;
        REQUIRE(p < base + 64);
        r.poke(p + 1, v);
    }
    std::string ac(const std::string &s) { return s.substr(0, 2) + s.substr(5); } ///< A and the carry
} // namespace

TEST(after_gx_bank8_slot_1_is_win1h_0e_and_gx_block_1_is_ram_block_1)
{
    // L1. GX_BANK8: carry clear, MAP's AP8 clear. GX_VMAP then maps slot 1 by WIN1H $0E with block 1 ($E04000) and
    // a plot lands on the card; GX_BLOCK(1) is a move (WIN1H $0E to 0, though the block byte is 1 both times), and a
    // byte stored through W1 lands in RAM block 1, the card's byte kept.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    REQUIRE(r.bridge().map() == 1);
    CHECK_EQ(gx(r, '8').substr(6), std::string("00"));
    CHECK_EQ(r.bridge().map(), 0);
    CHECK_EQ(gx(r, 'b', 32).substr(6), std::string("00")); // bank 8's RAM in the W1 map now
    const unsigned page = alloc(r, 240, 64);
    bmp_at(r, 0, page);
    mark_w1(r);
    CHECK_EQ(gx(r, 'm', 0x41, 0x05), std::string("00 41 00"));
    CHECK_EQ(w1h(r), 0x0E);
    CHECK_EQ(w1(r), 1);
    CHECK_EQ(r.phys(at("GXCURH")), 0x0E);
    CHECK_EQ(r.phys(at("GXCUR")), 1);
    CHECK_EQ(gx(r, 's', 0xA5).substr(6), std::string("00"));
    CHECK_EQ(card(r, SLOT1_AT), 0xA5);
    CHECK_EQ(r.phys(0x4000), B_RAM);
    xy(r, 7, 3);
    gx(r, 'P', 9);
    CHECK_EQ(card(r, page * 256u + 3 * 256 + 7), 9);
    CHECK_EQ(w1h(r), 0x0E);
    CHECK_EQ(gx(r, 'b', 1).substr(6), std::string("00"));
    CHECK_EQ(w1h(r), 0);
    CHECK_EQ(w1(r), 1);
    CHECK_EQ(gx(r, 'h'), std::string("00 5B 00"));
    gx(r, 's', 0x77);
    CHECK_EQ(r.phys(0x4000), 0x77);
    CHECK_EQ(card(r, SLOT1_AT), 0xA5);
}

TEST(gx_w1back_puts_win1h_0_back)
{
    // L2. GX_VMAP after GX_BANK8, then GX_W1BACK: WIN1H 0 and block 1, so W1 is RAM block 1 again.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    CHECK_EQ(gx(r, '8').substr(6), std::string("00"));
    mark_w1(r);
    gx(r, 'm', 0x41, 0x05);
    CHECK_EQ(gx(r, 'h'), std::string("0E C5 00"));
    gx(r, 'w');
    CHECK_EQ(w1h(r), 0);
    CHECK_EQ(w1(r), 1);
    CHECK_EQ(r.phys(at("GXCURH")), 0);
    CHECK_EQ(r.phys(at("GXCUR")), 1);
    CHECK_EQ(gx(r, 'h'), std::string("00 5B 00"));
}

TEST(gx_block_refuses_a_block_past_63)
{
    // L3. GX_BLOCK of 64 or more: GE_MAP, W1 as it was, though GX_W1MAP's next bytes (GX_UNITS) have the bits for
    // $40 and $41 set: units 0 and 1 taken.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    CHECK_EQ(hexax(valloc(r, 8)), PAGE0);
    REQUIRE(r.phys(at("GX_UNITS")) == 0x03);
    const uint8_t was = w1(r);
    for (const uint8_t b : {uint8_t{0x40}, uint8_t{0x41}, uint8_t{0xFF}})
    {
        CHECK_EQ(ac(gx(r, 'b', b)), std::string("02 01")); // GE_MAP
        CHECK_EQ(w1(r), was);
    }
    CHECK_EQ(gx(r, 'b', 0x3F).substr(6), std::string("01")); // not in the map: refused as before
}

TEST(gx_bank8_under_a_rom_before_0_17_keeps_the_old_mode)
{
    // L4. ROMVER (OODOS's record of the ROM BIOS) made 0.16: DM_BANK8 is refused, so GX_BANK8 gives GE_VERSION;
    // the library stays in the old mode: GX_BLOCK(32) refused (the old map), GX_VMAP on block 33, MAP still 1, and
    // no write to WIN1H (nor $C6A0-$C6A5) all the while.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    r.poke(SYS_BLOCK_PHYS + sym("ROMVER"), 0);
    r.poke(SYS_BLOCK_PHYS + sym("ROMVER") + 1, 16);
    NewRegWrites seen;
    seen.program = true;
    seen.watch(r);
    r.cpu->set_observer(&seen);
    CHECK_EQ(ac(gx(r, '8')), std::string("01 01")); // GE_VERSION
    CHECK_EQ(gx(r, 'b', 32).substr(6), std::string("01"));
    CHECK_EQ(gx(r, 'm', 0x41, 0x05), std::string("00 41 00"));
    CHECK_EQ(w1(r), 33);
    gx(r, 'w');
    settle(r);
    r.cpu->set_observer(nullptr);
    r.watch = nullptr;
    CHECK_EQ(seen.writes, 0u);
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(w1h(r), 0);
    CHECK_EQ(r.phys(at("GXAPH")), 0);
    CHECK_EQ(r.phys(at("GXAPB")), 33);
}

TEST(gx_bank8_needs_interface_12)
{
    // L5. FT_INFO's interface made 11 (an OODOS before DM_BANK8): GX_INIT still takes it (8 at least), GX_BANK8
    // gives GE_VERSION without asking function 83, and MAP stays 1.
    CcpRig r;
    REQUIRE(r.ok);
    ft_info_as(r, 11);
    start_gx(r);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    CHECK_EQ(ac(gx(r, '8')), std::string("01 01"));
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(r.phys(at("GXAPH")), 0);
    // And with no revision 2 card (revision 1's), GX_INIT's GE_VERSION as before, while GX_BANK8 succeeds: bank 8 is
    // RAM, with no window to map (b7-plan.md §2.4, step 4).
    CcpRig n({}, {}, {}, 1);
    REQUIRE(n.ok);
    start_gx(n);
    CHECK_EQ(ac(gx(n, 'I')), std::string("01 01"));
    CHECK_EQ(gx(n, '8').substr(6), std::string("00"));
    CHECK_EQ(n.phys(at("GXAPH")), 0);
    CHECK_EQ(n.phys(at("GX_OK")), 0);
}

TEST(a_program_that_calls_only_gx_init_keeps_ap8_and_block_33)
{
    // L6. GX_INIT alone (OO-Amp's way, question 5): MAP's AP8 stays set and GX_VMAP maps block 33, WIN1H 0, exactly
    // as before B7.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(gx(r, 'm', 0x41, 0x05), std::string("00 41 00"));
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(w1(r), 33);
    CHECK_EQ(w1h(r), 0);
    CHECK_EQ(r.phys(at("GXCURH")), 0);
    CHECK_EQ(r.phys(at("GXCUR")), 33);
}

TEST(on_a_revision_1_bridge_the_library_never_writes_c6a0_to_c6a5)
{
    // L7. A $D1 bridge has no WINnH: GX_INIT, GX_BANK8 (refused: GE_VERSION), GX_VMAP, GX_BLOCK and GX_W1BACK
    // never write $C6A0-$C6A5, and W1 moves as before (blocks 33, 36, 1).
    CcpRig r({}, revision_1_bridge());
    REQUIRE(r.ok);
    REQUIRE(r.bridge().id() == 0xD1);
    start_gx(r);
    NewRegWrites seen;
    r.cpu->set_observer(&seen);
    CHECK_EQ(gx(r, 'I').substr(6), std::string("00"));
    CHECK_EQ(ac(gx(r, '8')), std::string("01 01"));
    CHECK_EQ(gx(r, 'm', 0x41, 0x05), std::string("00 41 00"));
    CHECK_EQ(w1(r), 33);
    CHECK_EQ(gx(r, 'b', 36).substr(6), std::string("00"));
    CHECK_EQ(w1(r), 36);
    CHECK_EQ(gx(r, 'b', 1).substr(6), std::string("00"));
    gx(r, 'm', 0x41, 0x05);
    gx(r, 'w');
    CHECK_EQ(w1(r), 1);
    r.cpu->set_observer(nullptr);
    CHECK_EQ(seen.writes, 0u);
}

TEST(a_frame_hook_that_maps_slot_1_leaves_the_interrupted_code_its_own_w1)
{
    // L8 (C3). After GX_BANK8, a frame hook that calls GX_VMAP, so returns with WIN1H $0E and block 1 in W1. It
    // fires while CRTOUT has the console mapped (WIN1H $0E, block 0) and while GXTEST waits with WIN1H 0 and block 1:
    // each time, the interrupted code gets its own WIN1H and W1 back (HKBACK clears WIN1H by the register, whatever
    // the hook's record says, and the dispatcher puts the interrupted one back). The console's text is right, and
    // GXTEST's $4000 is RAM block 1's after.
    CcpRig r;
    REQUIRE(r.ok);
    start_gx(r);
    gx(r, 'I');
    CHECK_EQ(gx(r, '8').substr(6), std::string("00"));
    gx(r, 'w');
    mark_w1(r);
    const uint16_t irq = sym("IRQ"), rti1 = static_cast<uint16_t>(irq + 12), rti2 = sym("IRQEND");
    const uint16_t callhook = sym("CALLHOOK"), hkback = sym("HKBACK"), fhook = at("FHOOK");
    unsigned depth = 0, in_crt = 0, in_prog = 0, other = 0, wrong = 0;
    bool leaving = false, hooked = false, in_hook = false;
    uint8_t h_in = 0, w_in = 0;
    r.watch = [&](uint16_t pc) {
        if (leaving)
        {
            leaving = false;
            if (depth && --depth == 0 && hooked)
            {
                hooked = false;
                if (r.bridge().winh(1) != h_in || r.nb(1) != w_in)
                    ++wrong;
            }
        }
        if (pc == irq)
        {
            if (depth++ == 0)
                h_in = r.bridge().winh(1), w_in = r.nb(1);
        }
        else if (pc == rti1 || pc == rti2)
            leaving = true;
        else if (pc == callhook)
            in_hook = true;
        else if (pc == hkback)
            in_hook = false;
        else if (pc == fhook && in_hook && depth == 1)
        {
            hooked = true;
            if (h_in == 0x0E && w_in == 0)
                ++in_crt;
            else if (h_in == 0 && w_in == 1)
                ++in_prog;
            else
                ++other;
        }
    };
    CHECK_EQ(ac(gx(r, 'j', 1, 0x41, 0x05)), std::string("00 00"));
    for (int i = 0; i < 20 && in_crt < 3; ++i)
        CHECK_EQ(gx(r, 'q', 30, 0, 0, ms(20000)).substr(0, 2), std::string("00"));
    r.run(ms(100)); // GXTEST waiting: frames with WIN1H 0
    const std::string h = gx(r, 'h');
    std::printf("  frame hooks: %u in CRTOUT, %u in the program, %u else; %u with W1 wrong after; %u calls\n", in_crt,
                in_prog, other, wrong, r.phys(at("HOOKN")));
    CHECK(in_crt >= 3);
    CHECK(in_prog >= 3);
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(h, std::string("00 5B 00"));
    // The console: the last 27 rows above the "q" and "h" lines are all the 60 characters, nothing lost or misplaced.
    const std::string want = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwx";
    unsigned bad = 0;
    for (unsigned n = 0; n < 27; ++n)
    {
        std::string s;
        for (unsigned c = 0; c < 80; ++c)
            s += static_cast<char>(card(r, 0x002000 + n * 160 + c * 2));
        bad += s.substr(0, s.find_last_not_of(' ') + 1) != want;
    }
    CHECK_EQ(bad, 0u);
    gx(r, 'j', 0);
    r.watch = nullptr;
}

int main() { return run_all(); }
