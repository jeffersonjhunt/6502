/**
 * @file test_oodos_ui.cpp
 * @brief OOUI's phase U1 (hw/ooui.md, "The plan"): the core and the text backend (oodos/ui/), on video card
 * revision 2, driven by UITEST (oodos/test/ui/uitest.s) through the keyboard and the mouse of the USB card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * U1's gate is golden frames: each scene (a desktop, a window with labels and buttons, a menu down, the focus
 * moved by keys, both themes, the pointer on a button, an overlay window) is drawn by OOUI on the emulator and
 * the frame the card shows (VideoCard::frame(), 640 x 480) hashed, FNV-1a over its pixels, and compared with the
 * hash stored in golden/ooui.txt. The hashes are made by this suite from OOUI's own drawing, never from a fixture:
 * `OOUI_GOLDEN=update ctest --preset fast -R test_oodos_ui` writes them again, which a person does on purpose,
 * after looking at the frames (`OOUI_DUMP=dir` writes each as a PPM there); hw/ooui.md, "Emulator and testing",
 * says when. A frame that differs is written as ooui_NAME.ppm in the test's directory.
 *
 * Besides the hashes: the events ui_poll returns for each key and click, the places ui_rect gives, and frames
 * that must equal others (a menu put up again leaves the window as it was; one layer draws what two do).
 */

#include "harness.h"
#include "oodos_rig.h"
#include "usb_card.h"
#include "video_card.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    const std::string GOLDEN = std::string(EMU_GOLDEN_DIR) + "/ooui.txt";
    constexpr uint32_t MBOX = 0x3100, TEXTBUF = 0x3200, WORK = 0x3400, RESULT = 0x3800; ///< uitest.s's

    // uitest.s's handles (its table's order) and blocks.
    enum : uint8_t
    {
        W_DESK, W_BAR, M_FILE, I_OPEN, I_SAVE, I_SEP, I_EXIT, M_EDIT, I_CUT, I_COPY, M_HELP, I_ABOUT, W_MAIN, L_HEAD,
        P_OPTS, C_SHAD, C_MOUS, L_RIGHT, L_DIS, B_OK, B_CAN, W_SIDE, B_TOG, L_SIDE, W_OVER, L_OVER, B_HIDE, W_TALL,
        L_EDGE, W_THIN, L_THIN, MAIN_COUNT
    };
    // ooui.inc's record fields and flags
    constexpr unsigned UW_TYPE = 0, UW_PARENT = 1, UW_FLAGS = 6, UW_TEXT = 8, UW_VALUE = 10;
    constexpr uint8_t UF_DISABLED = 0x02, UF_NOFOCUS = 0x08, UF_MODAL = 0x20, UF_OVERLAY = 0x40, UF_NOFRAME = 0x80;
    constexpr uint8_t UT_NONE = 0, UT_PANEL = 3;
    enum : uint8_t { BLK_MAIN, BLK_ONE, BLK_DESK };
    // ooui.inc's events
    enum : uint8_t
    {
        UE_NONE, UE_CLICK, UE_CHANGE, UE_SELECT, UE_MARK, UE_MENU, UE_CLOSE, UE_FOCUS, UE_KEY, UE_WHEEL, UE_CANVAS, UE_PICK,
        UE_RAW, UE_MODAL
    };
    // HID usages
    constexpr uint8_t K_ENTER = 0x28, K_ESC = 0x29, K_TAB = 0x2B, K_SPACE = 0x2C, K_RIGHT = 0x4F, K_DOWN = 0x51,
                      K_F1 = 0x3A, K_C = 0x06, K_F = 0x09, K_Q = 0x14, K_T = 0x17, K_Z = 0x1D, K_BACK = 0x2A, K_HOME = 0x4A, K_PGUP = 0x4B, K_DEL = 0x4C, K_END = 0x4D, K_PGDN = 0x4E, K_LEFT = 0x50, K_UP = 0x52;
    constexpr uint8_t L_CTRL = 0xE0, L_SHIFT = 0xE1, L_ALT = 0xE2;

    VideoCard &vid(Rig &r) { return *r.machine->video_card(); }
    UsbCard &usb(Rig &r) { return *r.machine->usb_card(); }

    /// The ROM's boot splash stopped (the card shows it for its first 300 frames, whatever CTRL says).
    void no_splash(Rig &r)
    {
        r.machine->bus().write(0xC489, 0);
        r.run(ms(40));
    }

    /// The test program running: uitest (U1's scenes), uitwid, uitbar or uitpick (U2's).
    std::string &prog()
    {
        static std::string p = "uitest";
        return p;
    }
    /// OOUI.BNK's labels (mkbank.py's --lbl, U3a): name to its area (U UICOM, A, B, C the blocks, V the
    /// variables at UISTATE, Z the zero page) and its address in L0's link.
    const std::map<std::string, std::pair<char, uint16_t>> &bank_labels()
    {
        static std::map<std::string, std::pair<char, uint16_t>> l;
        if (l.empty())
        {
            std::istringstream in(read_text(UTILS + "/ooui-bnk.lbl"));
            std::string area, name, hex;
            while (in >> area >> name >> hex)
                l[name] = {area[0], static_cast<uint16_t>(std::stoul(hex, nullptr, 16))};
        }
        return l;
    }
    std::map<std::string, uint16_t> &prog_labels()
    {
        static std::map<std::string, std::map<std::string, uint16_t>> all;
        auto it = all.find(prog());
        if (it == all.end())
            it = all.emplace(prog(), labels(UTILS + "/" + prog() + ".lbl")).first;
        return it->second;
    }
    /// The running program's .lbl's address for `name`; else OOUI.BNK's (U3a): a variable's in UISTATE,
    /// as the program's link put it, a code label's in its block (in W3: bank_block() says which).
    uint16_t lab(const char *name)
    {
        const auto &p = prog_labels();
        if (const auto it = p.find(name); it != p.end())
            return it->second;
        const auto &[area, addr] = bank_labels().at(name);
        if (area == 'V')
            return static_cast<uint16_t>(p.at("UISTATE") + addr - 0x1000);
        return addr;
    }
    /// The block (A, B, C) that holds OOUI.BNK's code label `name`, as ui_load took it (UI_BLOCKS).
    unsigned bank_block(Rig &r, const char *name)
    {
        const char area = bank_labels().at(name).first;
        CHECK(area >= 'A' && area <= 'C');
        return r.phys(prog_labels().at("UI_BLOCKS") + static_cast<unsigned>(area - 'A'));
    }
    /// OOUI.BNK's user on M:, out of the picker's listings of user 0 (uitest.s loads it from there).
    constexpr unsigned BNK_USER = 15;
    /// M: has OOUI.BNK already (a second start_ui on the same rig).
    bool has_bnk(Rig &r)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            bool match = r.phys(MDISK + i * 32) == BNK_USER;
            for (unsigned c = 0; c < 11 && match; ++c)
                match = (r.phys(MDISK + i * 32 + 1 + c) & 0x7F) == static_cast<uint8_t>("OOUI    BNK"[c]);
            if (match)
                return true;
        }
        return false;
    }
    /// OOUI.BNK on M: (U3a: each test program loads it from there), once a rig, before M: is logged in:
    /// a CcpRig's put_m, or a card of the test's own's fresh M:.
    void put_bnk(CcpRig &r)
    {
        if (!has_bnk(r))
            r.put_m({{BNK_USER, "OOUI    BNK", read_bytes(UTILS + "/OOUI.BNK")}});
    }
    void put_bnk(Rig &r)
    {
        if (has_bnk(r))
            return;
        unsigned entry = 0, block = M_DIRBLOCKS;
        for (unsigned i = 0; i < 256; ++i) // past what M:'s files hold already
            if (r.phys(MDISK + i * 32) < 0x10)
                for (unsigned k = 16; k < 32; ++k)
                    block = std::max(block, r.phys(MDISK + i * 32 + k) + 1u);
        put_m_files(r, entry, block, {{BNK_USER, "OOUI    BNK", read_bytes(UTILS + "/OOUI.BNK")}});
    }
    /// `program` run (GO), OOLIB set up, and OOUI.BNK loaded from M: (put there first); with `flag` 'N' not
    /// loaded (the test says 'L'), 'G' not even GX_INIT (U3a).
    template <class R> void start_ui(R &r, const std::string &program = "uitest", char flag = 0)
    {
        prog() = program;
        no_splash(r);
        if (!flag)
            put_bnk(r);
        r.poke(MBOX, 0);
        r.poke(MBOX + 4, static_cast<uint8_t>(flag));
        r.poke_com(read_bytes(UTILS + "/" + program + ".com"));
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("\nO ", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("\nO ", from)));
        CHECK_EQ(field(r.out.substr(from), "O").substr(6), std::string("00")); // GX_INIT, GX_PALINIT, GX_SPRINIT
    }
    /// Command `c` with A, X, Y: "aa xx cc", and the text it printed before (ui_poll's events).
    std::string ui(Rig &r, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0, std::string *before = nullptr)
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX + 1, std::vector<uint8_t>{a, x, y});
        r.poke(MBOX, static_cast<uint8_t>(c));
        const std::string label(1, c);
        if (!r.until("\n" + label + " ", ms(8000), from))
        {
            std::printf("  no %c line\n", c);
            r.tail();
            return std::string(8, '?');
        }
        const size_t p = r.out.find("\n" + label + " ", from);
        r.until("\r", ms(500), p + 1);
        if (before)
            *before = r.out.substr(from, p - from);
        const std::string f = field(r.out.substr(from), label);
        return f.size() >= 8 ? f : std::string(8, '?');
    }

    struct Event
    {
        unsigned code, handle, y, value;
        unsigned kind = 0, x = 0, py = 0, flags = 0; ///< UV_KIND, UV_X, UV_Y, UV_FLAGS (not compared by ==)
        bool operator==(const Event &o) const { return code == o.code && handle == o.handle && y == o.y && value == o.value; }
    };
    /// ui_poll until UE_NONE: the events it returned, "E cc xx yy vvvv" each; then two frames for what it drew
    /// (the palette and the sprites are shown at a vertical blank).
    std::vector<Event> poll(Rig &r)
    {
        std::string text;
        ui(r, 'p', 0, 0, 0, &text);
        std::vector<Event> ev;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            unsigned c = 0, h = 0, y = 0, v = 0, k = 0, x = 0, py = 0, f = 0;
            if (std::sscanf(line.c_str(), "E %x %x %x %x %x %x %x %x", &c, &h, &y, &v, &k, &x, &py, &f) >= 4)
                ev.push_back({c, h, y, v, k, x, py, f});
        }
        r.run(ms(40));
        return ev;
    }
    std::string show(const std::vector<Event> &ev)
    {
        std::string s;
        char b[40];
        for (const Event &e : ev)
        {
            std::snprintf(b, sizeof b, "[%u %u %u %u]", e.code, e.handle, e.y, e.value);
            s += b;
        }
        return s.empty() ? "[]" : s;
    }
    void expect(const std::vector<Event> &got, const std::vector<Event> &want, const char *what)
    {
        if (!(got == want))
            std::printf("  %s: got %s, want %s\n", what, show(got).c_str(), show(want).c_str());
        CHECK(got == want);
    }

    /// A key on the USB keyboard, with a modifier held round it (0 none).
    void key(Rig &r, uint8_t usage, uint8_t mod = 0)
    {
        if (mod)
        {
            usb(r).key(mod, true);
            r.run(ms(20));
        }
        usb(r).key(usage, true);
        r.run(ms(20));
        usb(r).key(usage, false);
        r.run(ms(20));
        if (mod)
        {
            usb(r).key(mod, false);
            r.run(ms(20));
        }
    }
    /// The pointer to the middle of cell (col, row): 4 x 8 pointer units a cell.
    void point(Rig &r, unsigned col, unsigned row)
    {
        usb(r).mouse_to(static_cast<int>(col * 4 + 2), static_cast<int>(row * 8 + 4));
        r.run(ms(30));
    }
    void button(Rig &r, bool down)
    {
        usb(r).mouse_buttons(down ? 1 : 0);
        r.run(ms(30));
    }

    /// The frame the card shows, FNV-1a 64 over its pixels.
    uint64_t frame_hash(Rig &r)
    {
        const uint32_t *f = vid(r).frame();
        uint64_t h = 0xCBF29CE484222325ull;
        for (size_t i = 0; i < size_t{640} * 480; ++i)
            for (unsigned b = 0; b < 4; ++b)
            {
                h ^= (f[i] >> (8 * b)) & 0xFF;
                h *= 0x100000001B3ull;
            }
        return h;
    }
    void write_ppm(Rig &r, const std::string &path)
    {
        const uint32_t *f = vid(r).frame();
        std::ofstream o(path, std::ios::binary);
        o << "P6\n640 480\n255\n";
        for (size_t i = 0; i < size_t{640} * 480; ++i)
        {
            const char px[3] = {static_cast<char>(f[i]), static_cast<char>(f[i] >> 8), static_cast<char>(f[i] >> 16)}; // ABGR
            o.write(px, 3);
        }
    }

    std::map<std::string, uint64_t> read_golden()
    {
        std::map<std::string, uint64_t> g;
        std::ifstream in(GOLDEN);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream l(line);
            std::string name, hex;
            if (l >> name >> hex)
                g[name] = std::stoull(hex, nullptr, 16);
        }
        return g;
    }
    bool updating()
    {
        const char *u = std::getenv("OOUI_GOLDEN");
        return u && std::string(u) == "update";
    }
    /// The frame now against golden frame `name`; with OOUI_GOLDEN=update, stored as it.
    void golden(Rig &r, const std::string &name)
    {
        const uint64_t h = frame_hash(r);
        if (const char *dump = std::getenv("OOUI_DUMP"))
            write_ppm(r, std::string(dump) + "/ooui_" + name + ".ppm");
        std::map<std::string, uint64_t> g = read_golden();
        if (updating())
        {
            g[name] = h;
            std::ofstream o(GOLDEN);
            o << "# OOUI's golden frames (test_oodos_ui.cpp): FNV-1a 64 of each frame's 640 x 480 pixels.\n"
                 "# Made by the emulator from OOUI's drawing: OOUI_GOLDEN=update, after looking (hw/ooui.md).\n";
            for (const auto &[n, v] : g)
            {
                char b[64];
                std::snprintf(b, sizeof b, "%s %016llx\n", n.c_str(), static_cast<unsigned long long>(v));
                o << b;
            }
            std::printf("  golden %s: %016llx (stored)\n", name.c_str(), static_cast<unsigned long long>(h));
            return;
        }
        const auto it = g.find(name);
        if (it == g.end())
        {
            std::printf("  golden %s: none stored (%016llx); OOUI_GOLDEN=update makes it\n", name.c_str(),
                        static_cast<unsigned long long>(h));
            CHECK(false);
            return;
        }
        if (it->second != h)
        {
            write_ppm(r, "ooui_" + name + ".ppm");
            std::printf("  golden %s: %016llx, stored %016llx; the frame is ooui_%s.ppm\n", name.c_str(),
                        static_cast<unsigned long long>(h), static_cast<unsigned long long>(it->second), name.c_str());
        }
        CHECK_EQ(it->second, h);
    }

    /// A widget's place: "cc rr cs" (column, row, carry), then UI_RECT's size read back.
    struct Place
    {
        unsigned col, row, w, h;
        bool hidden;
    };
    Place rect(Rig &r, uint8_t h)
    {
        const std::string a = ui(r, 'r', h);
        Place p{};
        if (a[0] == '?')
            return p;
        p.col = static_cast<unsigned>(std::stoul(a.substr(0, 2), nullptr, 16));
        p.row = static_cast<unsigned>(std::stoul(a.substr(3, 2), nullptr, 16));
        p.hidden = a.substr(6, 2) == "01";
        p.w = r.phys(lab("UI_RECT") + 2u);
        p.h = r.phys(lab("UI_RECT") + 3u);
        return p;
    }
    void expect_place(Rig &r, uint8_t h, Place want)
    {
        const Place p = rect(r, h);
        if (p.col != want.col || p.row != want.row || p.w != want.w || p.h != want.h || p.hidden != want.hidden)
            std::printf("  record %u: %u,%u %ux%u%s, want %u,%u %ux%u%s\n", h, p.col, p.row, p.w, p.h, p.hidden ? " hidden" : "",
                        want.col, want.row, want.w, want.h, want.hidden ? " hidden" : "");
        CHECK(p.col == want.col && p.row == want.row && p.w == want.w && p.h == want.h && p.hidden == want.hidden);
    }

    /// Record h's address in uitest.s's table, `main`.
    uint16_t rec(uint8_t h) { return static_cast<uint16_t>(lab("main") + h * 16u); }
    void poke_flags(Rig &r, uint8_t h, uint8_t set) { r.poke(rec(h) + UW_FLAGS, static_cast<uint8_t>(r.phys(rec(h) + UW_FLAGS) | set)); }
    /// ui_flags: Y the handle, A to set, X to clear.
    std::string flags(Rig &r, uint8_t h, uint8_t set, uint8_t clear = 0) { return ui(r, 'F', set, clear, h); }
    void put_text(Rig &r, const std::string &t)
    {
        std::vector<uint8_t> b(t.begin(), t.end());
        b.push_back(0);
        r.poke(TEXTBUF, b);
    }

    /// The main scene opened and drawn once: no events.
    void open_main(CcpRig &r, uint8_t block = BLK_MAIN)
    {
        start_ui(r);
        CHECK_EQ(ui(r, 'o', block).substr(6), std::string("00"));
        expect(poll(r), {}, "the first poll");
    }
} // namespace

TEST(a_desktop_and_its_menu_bar)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    expect(poll(r), {}, "the first poll");
    CHECK_EQ(ui(r, 'g'), std::string("FF 00 00")); // nothing takes the focus
    CHECK_EQ(ui(r, 'i'), std::string("50 1E 00")); // 80 x 30, the text backend
    golden(r, "desktop");
    // Card memory: one bank, 64 pages aligned, from the program's part; the layers on it.
    CHECK_EQ(vid(r).peek(0x10), 0);                                       // layer 0: text 80
    const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8, font0 = vid(r).peek(0x13) | vid(r).peek(0x14) << 8;
    const unsigned map1 = vid(r).peek(0x21) | vid(r).peek(0x22) << 8;
    CHECK(map0 >= 0x0400 && map0 % 64 == 0);
    CHECK_EQ(font0, map0 + 0x30);
    CHECK_EQ(map1, map0 + 0x14);
    CHECK_EQ(vid(r).peek(0x00) & 0x0F, 0x0F); // the display, both layers, the sprites
    // The font: the console's, with OOUI's glyphs over it (a vertical bar half full: rows 8-15 lit).
    for (unsigned c : {0x41u, 0xB3u, 0xC4u})
        for (unsigned row = 0; row < 16; ++row)
            CHECK_EQ(vid(r).card_peek(font0 * 256u + c * 16 + row), vid(r).card_peek(0x000400 + c * 16 + row));
    for (unsigned row = 0; row < 16; ++row)
        CHECK_EQ(vid(r).card_peek(font0 * 256u + 8 * 16 + row), row >= 8 ? 0xFF : 0x00);
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    r.run(ms(40));
    CHECK_EQ(vid(r).peek(0x10), 0x80); // the console's screen back
}

TEST(a_window_with_labels_buttons_and_a_check_box)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("0F")); // the first that takes the focus: Shadows
    golden(r, "window");
    // The places: from the parent's client area, negative from its far edge, 0 or negative to it.
    expect_place(r, W_MAIN, {6, 3, 46, 15, false});
    expect_place(r, L_HEAD, {8, 5, 42, 1, false});  // x 1 in the client (7, 4, 44, 13), width -1
    expect_place(r, P_OPTS, {8, 7, 20, 6, false});
    expect_place(r, C_SHAD, {10, 9, 17, 1, false}); // width 0: to the panel's client edge
    expect_place(r, B_OK, {27, 15, 10, 1, false});  // x -24, y -2
    expect_place(r, B_CAN, {39, 15, 10, 1, false});
    expect_place(r, W_BAR, {0, 0, 80, 1, false});
    expect_place(r, M_FILE, {1, 1, 19, 6, true});   // its drop-down, hidden until it is down
    expect_place(r, W_OVER, {20, 19, 34, 8, true});
}

TEST(menus_by_the_keyboard)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t window = frame_hash(r);
    key(r, K_F, L_ALT); // Alt+F: File down, Open chosen
    expect(poll(r), {}, "Alt+F");
    golden(r, "menu-file");
    expect_place(r, M_FILE, {1, 1, 19, 6, false});
    key(r, K_DOWN);
    key(r, K_DOWN); // past the line, to Exit
    expect(poll(r), {}, "Down, Down");
    golden(r, "menu-select");
    key(r, K_ESC); // up again: the window as it was
    expect(poll(r), {}, "Escape");
    CHECK_EQ(frame_hash(r), window);
    key(r, K_F, L_ALT);
    key(r, K_RIGHT); // Edit, then Help, then round to File: Enter chooses Open
    key(r, K_RIGHT);
    key(r, K_RIGHT);
    key(r, K_ENTER);
    expect(poll(r), {{UE_MENU, I_OPEN, M_FILE, 0}}, "Alt+F, Right x 3, Enter");
    CHECK_EQ(frame_hash(r), window);
    key(r, K_F, L_ALT);
    key(r, 0x1B); // x: Exit's key letter
    expect(poll(r), {{UE_MENU, I_EXIT, M_FILE, 0}}, "Alt+F, X");
    key(r, K_F1); // the accelerator table's: About, its menu never down
    expect(poll(r), {{UE_MENU, I_ABOUT, M_HELP, 0}}, "F1");
    CHECK_EQ(frame_hash(r), window);
}

TEST(focus_and_buttons_by_the_keyboard)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    key(r, K_TAB);
    key(r, K_TAB);
    expect(poll(r), {{UE_FOCUS, C_MOUS, 0, 0}, {UE_FOCUS, B_OK, 0, 0}}, "Tab, Tab");
    golden(r, "focus-ok");
    key(r, K_TAB, L_SHIFT);
    expect(poll(r), {{UE_FOCUS, C_MOUS, 0, 0}}, "Shift+Tab");
    key(r, K_SPACE); // the check box the focus is on
    expect(poll(r), {{UE_CHANGE, C_MOUS, 0, 1}}, "Space");
    key(r, K_ENTER); // Enter on a check box flips it too
    expect(poll(r), {{UE_CHANGE, C_MOUS, 0, 0}}, "Enter");
    key(r, K_ESC); // the cancel button
    expect(poll(r), {{UE_CLICK, B_CAN, 0, 0}}, "Escape");
    key(r, K_C); // Cancel's key letter: the focus moves there too
    expect(poll(r), {{UE_CLICK, B_CAN, 0, 0}}, "C");
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("14"));
    key(r, K_Q, L_CTRL); // the accelerator table's
    expect(poll(r), {{UE_CLICK, B_CAN, 0, 0}}, "Ctrl+Q");
    key(r, K_T, L_CTRL);
    expect(poll(r), {{UE_FOCUS, B_TOG, 0, 0}}, "Ctrl+T");
    key(r, K_SPACE); // a toggle: on, then off
    key(r, K_SPACE);
    expect(poll(r), {{UE_CLICK, B_TOG, 0, 1}, {UE_CLICK, B_TOG, 0, 0}}, "Space, Space");
    key(r, K_Z); // nothing takes it
    expect(poll(r), {{UE_KEY, B_TOG, K_Z, 0}}, "Z");
    key(r, K_ENTER, L_SHIFT); // the focus takes Enter, Shift or not (Second has no default button)
    expect(poll(r), {{UE_CLICK, B_TOG, 0, 1}}, "Shift+Enter");
}

TEST(both_themes)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t house = frame_hash(r);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    expect(poll(r), {}, "the theme");
    golden(r, "turbo-window");
    key(r, K_F, L_ALT);
    expect(poll(r), {}, "Alt+F");
    golden(r, "turbo-menu");
    key(r, K_ESC);
    CHECK_EQ(ui(r, 't', 0).substr(6), std::string("00"));
    expect(poll(r), {}, "back");
    CHECK_EQ(frame_hash(r), house);
}

TEST(the_pointer_presses_and_clicks)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    // Down on OK: pressed, and the focus there; up on it: clicked.
    point(r, 30, 15);
    button(r, true);
    expect(poll(r), {{UE_FOCUS, B_OK, 0, 0}}, "down on OK");
    golden(r, "pointer-press");
    button(r, false);
    expect(poll(r), {{UE_CLICK, B_OK, 0, 0}}, "up on OK");
    // Toggle, in the other window: the focus moves to it, and it stays down.
    const Place tog = rect(r, B_TOG);
    CHECK(tog.col == 51 && tog.row == 15);
    point(r, tog.col + 3, tog.row);
    button(r, true);
    expect(poll(r), {{UE_FOCUS, B_TOG, 0, 0}}, "down on Toggle");
    button(r, false);
    expect(poll(r), {{UE_CLICK, B_TOG, 0, 1}}, "up on Toggle");
    golden(r, "pointer-hit");
    // Down on OK, up off it: no click. (A click is where the pointer is when ui_poll reads it: the queue's
    // button events carry no position, so each click here is polled before the pointer moves on.)
    point(r, 30, 15);
    button(r, true);
    expect(poll(r), {{UE_FOCUS, B_OK, 0, 0}}, "down on OK again");
    point(r, 30, 16);
    button(r, false);
    expect(poll(r), {}, "up off it");
    // The main window's close box, "[x]" from its third column: closed on the press.
    point(r, 6 + 3, 3);
    button(r, true);
    button(r, false);
    expect(poll(r), {{UE_CLOSE, W_MAIN, 0, 0}}, "the close box");
    // Edit's title on the bar (File's " File " at 1-6, a gap, Edit's at 8-13), then Cut in it.
    point(r, 9, 0);
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "Edit's title");
    expect_place(r, M_EDIT, {8, 1, 8, 4, false});
    point(r, 10, 2);
    button(r, true);
    button(r, false);
    expect(poll(r), {{UE_MENU, I_CUT, M_EDIT, 0}}, "Cut");
    // Copy is disabled: the menu stays down. A click off it puts it up.
    point(r, 9, 0);
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "Edit's title again");
    point(r, 10, 3);
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "Copy");
    expect_place(r, M_EDIT, {8, 1, 8, 4, false});
    point(r, 70, 25);
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "off the menu");
    expect_place(r, M_EDIT, {8, 1, 8, 4, true});
    // The wheel: the widget under the pointer.
    point(r, tog.col, tog.row);
    usb(r).mouse_wheel(-2);
    r.run(ms(30));
    expect(poll(r), {{UE_WHEEL, B_TOG, 0, 0xFFFE}}, "the wheel");
}

TEST(an_overlay_window_and_what_hiding_draws_again)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t window = frame_hash(r);
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {}, "shown");
    golden(r, "overlay");
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("0F")); // not modal: the focus stays
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {}, "hidden");
    CHECK_EQ(frame_hash(r), window);
    // The second window hidden and shown again on the base layer: the first's corner drawn again under it.
    CHECK_EQ(ui(r, 'h', W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {}, "Second hidden");
    CHECK(frame_hash(r) != window);
    CHECK_EQ(ui(r, 's', W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {}, "Second shown");
    CHECK_EQ(frame_hash(r), window);
    // A label's text changed, and back.
    r.poke(TEXTBUF, std::vector<uint8_t>{'C', 'h', 'a', 'n', 'g', 'e', 'd', 0});
    CHECK_EQ(ui(r, 'x', 0, 0, L_SIDE).substr(6), std::string("00"));
    expect(poll(r), {}, "the text");
    CHECK(frame_hash(r) != window);
    r.poke(TEXTBUF, std::vector<uint8_t>{'I', 'n', ' ', 'f', 'r', 'o', 'n', 't', ' ', 'o', 'f', ' ', 't', 'h', 'e', ' ',
                                         'f', 'i', 'r', 's', 't', 0});
    CHECK_EQ(ui(r, 'x', 0, 0, L_SIDE).substr(6), std::string("00"));
    expect(poll(r), {}, "the text back");
    CHECK_EQ(frame_hash(r), window);
}

TEST(one_layer_draws_what_two_do)
{
    // UO_OVER $FF: the menus and the overlay window drawn on the base layer, and what they covered drawn again.
    CcpRig r2;
    REQUIRE(r2.ok);
    open_main(r2);
    const uint64_t window = frame_hash(r2);
    key(r2, K_F, L_ALT);
    poll(r2);
    const uint64_t menu = frame_hash(r2);
    CHECK_EQ(ui(r2, 's', W_OVER).substr(6), std::string("00"));
    poll(r2);
    const uint64_t over = frame_hash(r2);

    CcpRig r;
    REQUIRE(r.ok);
    open_main(r, BLK_ONE);
    CHECK_EQ(frame_hash(r), window);
    key(r, K_F, L_ALT);
    expect(poll(r), {}, "Alt+F");
    CHECK_EQ(frame_hash(r), menu);
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {}, "shown");
    CHECK_EQ(frame_hash(r), over);
    key(r, K_ESC);
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {}, "up and hidden");
    CHECK_EQ(frame_hash(r), window);
    CHECK_EQ(vid(r).peek(0x00) & 0x0F, 0x0B); // the display, layer 0, the sprites: layer 1 the program's
}

TEST(the_table_is_checked_and_handles_too)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    const uint16_t main = lab("main");
    const uint8_t at = r.phys(main + B_OK * 16u + 1);
    r.poke(main + B_OK * 16u + 1, static_cast<uint8_t>(B_CAN)); // a parent after its child
    const auto refused = [&r] { const std::string a = ui(r, 'o', BLK_MAIN); return a.substr(0, 2) + a.substr(6); };
    CHECK_EQ(refused(), std::string("0401")); // UX_TABLE
    r.poke(main + B_OK * 16u + 1, at);
    r.poke(main + I_CUT * 16u + 1, static_cast<uint8_t>(W_DESK)); // an item outside a menu
    CHECK_EQ(refused(), std::string("0401"));
    r.poke(main + I_CUT * 16u + 1, static_cast<uint8_t>(M_EDIT));
    r.poke(main + L_SIDE * 16u, 15); // a list: U2's, no longer refused unregistered (U3a, E4: OOUI.BNK has them all)
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    r.poke(main + L_SIDE * 16u, 4);
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'd', MAIN_COUNT).substr(0, 2) + ui(r, 'd', MAIN_COUNT).substr(6), std::string("0501")); // UX_HANDLE
    CHECK_EQ(ui(r, 'f', L_HEAD).substr(6), std::string("01"));                             // a label takes no focus
    CHECK_EQ(ui(r, 'f', B_HIDE).substr(6), std::string("01"));                             // nor a hidden button
    CHECK_EQ(ui(r, 'f', B_CAN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("14"));
}


// ---- the U1 review's findings (2026-11-11): each case fails without its fix --------------------------------

TEST(o1_a_change_lands_on_the_record_it_names)
{
    // ui_flags, ui_setvalue and ui_settext on a menu's item or a menu: the record named, not its bar; and drawn
    // as the same table opened with the change already in it draws.
    CcpRig a;
    REQUIRE(a.ok);
    open_main(a);
    const uint8_t bar_flags = a.phys(rec(W_BAR) + UW_FLAGS);
    CHECK_EQ(flags(a, I_SAVE, UF_DISABLED).substr(6), std::string("00"));
    CHECK_EQ(ui(a, 'v', 1, 0, I_OPEN).substr(6), std::string("00"));
    put_text(a, "&Fyle");
    CHECK_EQ(ui(a, 'x', 0, 0, M_FILE).substr(6), std::string("00"));
    CHECK(a.phys(rec(I_SAVE) + UW_FLAGS) & UF_DISABLED);
    CHECK_EQ(a.phys(rec(I_OPEN) + UW_VALUE), 1);
    CHECK_EQ(static_cast<uint32_t>(a.phys(rec(M_FILE) + UW_TEXT) | a.phys(rec(M_FILE) + UW_TEXT + 1) << 8), TEXTBUF);
    CHECK_EQ(a.phys(rec(W_BAR) + UW_FLAGS) & ~0x04, bar_flags & ~0x04); // the bar's untouched (its dirty bit aside)
    CHECK_EQ(a.phys(rec(W_BAR) + UW_VALUE), 0);
    CHECK_EQ(a.phys(rec(W_BAR) + UW_TEXT) | a.phys(rec(W_BAR) + UW_TEXT + 1) << 8, 0);
    expect(poll(a), {}, "the changes");
    key(a, K_F, L_ALT);
    expect(poll(a), {}, "Alt+F");
    const uint64_t changed = frame_hash(a);

    CcpRig b;
    REQUIRE(b.ok);
    start_ui(b);
    poke_flags(b, I_SAVE, UF_DISABLED);
    b.poke(rec(I_OPEN) + UW_VALUE, 1);
    put_text(b, "&Fyle");
    b.poke(rec(M_FILE) + UW_TEXT, std::vector<uint8_t>{static_cast<uint8_t>(TEXTBUF), static_cast<uint8_t>(TEXTBUF >> 8)});
    CHECK_EQ(ui(b, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(b);
    key(b, K_F, L_ALT);
    poll(b);
    CHECK_EQ(changed, frame_hash(b));
}

TEST(o2_hiding_the_bar_puts_its_menu_up)
{
    CcpRig a;
    REQUIRE(a.ok);
    open_main(a);
    key(a, K_F, L_ALT);
    poll(a);
    CHECK_EQ(ui(a, 'h', W_BAR).substr(6), std::string("00"));
    expect(poll(a), {}, "the bar hidden");
    expect_place(a, M_FILE, {1, 1, 19, 6, true});
    CcpRig b;
    REQUIRE(b.ok);
    open_main(b);
    CHECK_EQ(ui(b, 'h', W_BAR).substr(6), std::string("00"));
    poll(b);
    CHECK_EQ(frame_hash(a), frame_hash(b));
}

TEST(o3_a_hidden_item_has_no_row)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(ui(r, 'h', I_SAVE).substr(6), std::string("00"));
    key(r, K_F, L_ALT);
    expect(poll(r), {}, "Alt+F");
    expect_place(r, M_FILE, {1, 1, 19, 5, false}); // Open, the line, Exit
    expect_place(r, I_EXIT, {2, 4, 17, 1, false});
    point(r, 5, 4);
    button(r, true);
    button(r, false);
    expect(poll(r), {{UE_MENU, I_EXIT, M_FILE, 0}}, "a click on Exit's row");
}

TEST(o4_a_disabled_menu_stays_up_and_its_items_too)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(flags(r, M_EDIT, UF_DISABLED).substr(6), std::string("00"));
    point(r, 9, 0); // Edit's title
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "a click on Edit");
    expect_place(r, M_EDIT, {8, 1, 8, 4, true});
    key(r, K_F, L_ALT);
    key(r, K_RIGHT); // past Edit, to Help
    key(r, K_ENTER);
    expect(poll(r), {{UE_MENU, I_ABOUT, M_HELP, 0}}, "Alt+F, Right, Enter");
    CHECK_EQ(flags(r, M_HELP, UF_DISABLED).substr(6), std::string("00"));
    key(r, K_F1); // About's accelerator: its menu disabled
    expect(poll(r), {}, "F1");
}

TEST(o5_o12_a_modal_window_keeps_the_keys_and_letters_press_unfocusable_buttons)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    // O12: a button that takes no focus is still pressed by its letter; the focus stays.
    CHECK_EQ(flags(r, B_OK, UF_NOFOCUS).substr(6), std::string("00"));
    key(r, 0x12); // O
    expect(poll(r), {{UE_CLICK, B_OK, 0, 0}}, "O");
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("0F"));
    // O5: under a modal window the accelerator table reaches neither a button nor an item outside it.
    CHECK_EQ(flags(r, W_OVER, UF_MODAL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("1A")); // Hide, the modal window's
    key(r, K_Q, L_CTRL);
    key(r, K_F1);
    expect(poll(r), {}, "Ctrl+Q, F1 under the modal window");
    key(r, 0x0B); // H: Hide's letter, in the modal window
    expect(poll(r), {{UE_CLICK, B_HIDE, 0, 0}}, "H");
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'h', I_ABOUT).substr(6), std::string("00")); // a hidden item: not chosen by its key
    key(r, K_F1);
    expect(poll(r), {{UE_MODAL, W_OVER, 0, 0}}, "F1 for a hidden item"); // (U2: the modal window gone)
    key(r, K_Q, L_CTRL);
    expect(poll(r), {{UE_CLICK, B_CAN, 0, 0}}, "Ctrl+Q with no modal window");
}

TEST(o6_o14_unused_records_and_the_table_s_depth)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    const auto open = [&r] {
        const std::string a = ui(r, 'o', BLK_MAIN);
        if (a.substr(6) != "00")
            return a.substr(0, 2) + a.substr(6); // the error, and the carry
        ui(r, 'c');
        return std::string("ok");
    };
    std::vector<uint8_t> keep(16);
    for (unsigned i = 0; i < 16; ++i)
        keep[i] = r.phys(rec(L_SIDE) + i);
    r.poke(rec(L_SIDE) + UW_TYPE, UT_NONE); // unused, at the top
    r.poke(rec(L_SIDE) + UW_PARENT, 0xFF);
    CHECK_EQ(open(), std::string("ok"));
    r.poke(rec(L_SIDE), std::vector<uint8_t>(16, 0)); // zeroed
    CHECK_EQ(open(), std::string("ok"));
    r.poke(rec(L_SIDE), keep);
    r.poke(rec(W_SIDE) + UW_TYPE, UT_NONE); // Toggle's parent unused: refused
    CHECK_EQ(open(), std::string("0401"));
    r.poke(rec(W_SIDE) + UW_TYPE, 2);
    // O14: a chain of 16 records (the desktop, then 12-26 each the last's child) opens; 17 doesn't.
    for (uint8_t h = 13; h <= 26; ++h)
    {
        r.poke(rec(h) + UW_TYPE, UT_PANEL);
        r.poke(rec(h) + UW_PARENT, static_cast<uint8_t>(h - 1));
    }
    r.poke(rec(W_TALL) + UW_TYPE, UT_PANEL);
    r.poke(rec(L_EDGE) + UW_PARENT, W_DESK);
    CHECK_EQ(open(), std::string("ok"));
    r.poke(rec(I_ABOUT) + UW_TYPE, UT_PANEL); // 11 under the desktop, 12 under 11: 17 deep
    r.poke(rec(I_ABOUT) + UW_PARENT, W_DESK);
    r.poke(rec(W_MAIN) + UW_PARENT, I_ABOUT);
    CHECK_EQ(open(), std::string("0401"));
}

TEST(o7_sizes_that_would_wrap_draw_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t window = frame_hash(r);
    CHECK_EQ(ui(r, 's', L_EDGE).substr(6), std::string("00"));
    expect(poll(r), {}, "Edge shown");
    expect_place(r, L_EDGE, {79, 14, 0, 1, false}); // past Second's client: width 0, not 252
    CHECK_EQ(frame_hash(r), window);
    CHECK_EQ(ui(r, 'h', L_EDGE).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', W_TALL).substr(6), std::string("00"));
    expect(poll(r), {}, "Tall shown");
    expect_place(r, W_TALL, {10, 26, 20, 0, false}); // 4 rows to the bottom, less 6: 0, not 254
    CHECK_EQ(frame_hash(r), window);
    // A size past the client's edge is held to it: Edge at column 20 of Second's 26, 20 wide, is 6 wide, and
    // draws as a label made 6 wide would.
    CHECK_EQ(ui(r, 'h', W_TALL).substr(6), std::string("00"));
    poll(r);
    r.poke(rec(L_EDGE) + 2u, 20); // UW_X
    r.poke(rec(L_EDGE) + 4u, 20); // UW_W
    CHECK_EQ(ui(r, 's', L_EDGE).substr(6), std::string("00"));
    expect(poll(r), {}, "Edge, 20 wide");
    expect_place(r, L_EDGE, {69, 14, 6, 1, false});
    const uint64_t held = frame_hash(r);
    CHECK_EQ(ui(r, 'h', L_EDGE).substr(6), std::string("00"));
    poll(r);
    r.poke(rec(L_EDGE) + 4u, 6);
    CHECK_EQ(ui(r, 's', L_EDGE).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(frame_hash(r), held);
}

TEST(o9_o10_one_session_and_its_layers)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    const auto refused = [&r] { const std::string a = ui(r, 'o', BLK_MAIN); return a.substr(0, 2) + a.substr(6); };
    // O10: the base 0 or 1; the overlay $FF or a layer above the base.
    const uint16_t blk = lab("blk_main");
    for (const auto &[base, over] : std::vector<std::pair<uint8_t, uint8_t>>{{2, 0xFF}, {1, 0}, {1, 1}, {0, 0}, {0, 2}})
    {
        r.poke(blk + 9u, base);
        r.poke(blk + 10u, over);
        CHECK_EQ(refused(), std::string("0701")); // UX_LAYER
    }
    r.poke(blk + 9u, 0);
    r.poke(blk + 10u, 1);
    // O9: open once; a second open refused (UX_BUSY), the session as it was; closed, opened on the same bank.
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(r);
    const uint64_t window = frame_hash(r);
    const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8;
    CHECK_EQ(refused(), std::string("0601"));
    expect(poll(r), {}, "after the refused open");
    CHECK_EQ(frame_hash(r), window);
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00")); // closed already: nothing
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    CHECK_EQ(static_cast<unsigned>(vid(r).peek(0x11) | vid(r).peek(0x12) << 8), map0); // its bank freed and taken again
}

TEST(o11_flags_changed_damage_where_the_widget_was)
{
    for (const uint8_t f : {UF_NOFRAME, UF_OVERLAY})
    {
        CcpRig a;
        REQUIRE(a.ok);
        open_main(a);
        CHECK_EQ(flags(a, W_SIDE, f).substr(6), std::string("00"));
        poll(a);
        CcpRig b;
        REQUIRE(b.ok);
        start_ui(b);
        poke_flags(b, W_SIDE, f);
        CHECK_EQ(ui(b, 'o', BLK_MAIN).substr(6), std::string("00"));
        poll(b);
        if (frame_hash(a) != frame_hash(b))
            std::printf("  flag %02x: set by ui_flags, the frame differs from the table's\n", f);
        CHECK_EQ(frame_hash(a), frame_hash(b));
    }
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(flags(r, C_SHAD, UF_DISABLED).substr(0, 2), std::string("06")); // the flags now: dirty, disabled
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("10")); // the focus moved on to Mouse
}

TEST(o15_the_maps_are_clear_before_the_first_poll)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    // The banks OOUI may take, full of junk: the first is GX_PALINIT's and GX_SPRINIT's, so OOUI's is the next.
    for (uint32_t a = 0x044000; a < 0x04C000; ++a)
        vid(r).card_poke(a, static_cast<uint8_t>(a * 7 + 0x41));
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    CHECK_EQ(vid(r).peek(0x11) | vid(r).peek(0x12) << 8, 0x0440); // the bank filled
    r.run(ms(40));
    const uint32_t *f = vid(r).frame();
    const uint32_t bg = VideoCard::rgba(0x112); // palette entry 0, where both layers are transparent
    size_t other = 0;
    for (size_t i = 0; i < size_t{640} * 480; ++i)
        other += f[i] != bg;
    if (other > 32 * 32)
        std::printf("  %zu pixels not entry 0's\n", other);
    CHECK(other <= 32 * 32); // the pointer at most
}

TEST(o17_the_pointer_off_when_closed_or_suspended)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const auto depth = [&r] {
        r.run(ms(40));
        const unsigned sprbase = vid(r).peek(0x0C) | vid(r).peek(0x0D) << 8;
        return std::make_pair(r.phys(lab("GX_SPR") + 6u) & 0x0C, vid(r).card_peek(sprbase * 256u + 6) & 0x0C);
    };
    CHECK(depth() == std::make_pair(0x0C, 0x0C)); // sprite 0, in front of both layers
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    CHECK(depth() == std::make_pair(0, 0));
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    CHECK(depth() == std::make_pair(0x0C, 0x0C));
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(r.phys(lab("GX_SPR") + 6u) & 0x0C, 0);
}

TEST(p1_a_theme_in_palette_groups_of_its_own)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t house = frame_hash(r);
    CHECK_EQ(ui(r, 't', 2).substr(6), std::string("00")); // the house colours in groups 2, 3 and (dim) 4
    expect(poll(r), {}, "the grouped theme");
    CHECK_EQ(vid(r).peek(0x19), 2); // layer 0's PALOFS
    CHECK_EQ(vid(r).peek(0x29), 3); // layer 1's
    CHECK_EQ(frame_hash(r), house); // the same colours, other entries
    CHECK_EQ(flags(r, W_OVER, UF_MODAL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_OVER, 0, 1}}, "a modal window shown"); // (U2: UE_MODAL)
    CHECK_EQ(vid(r).peek(0x19), 4); // the base dimmed (TF_DIM)
    golden(r, "dim-modal");
    // The dim group: the house colours, each component halved (round 2's W7), in OOLIB's shadow.
    const uint32_t pal = lab("GX_PAL"), colours = r.phys(lab("UI_HOUSE") + 2u) | r.phys(lab("UI_HOUSE") + 3u) << 8;
    for (uint32_t i = 0; i < 16; ++i)
    {
        CHECK_EQ(r.phys(pal + 2 * (64 + i)), (r.phys(colours + 2 * i) >> 1) & 0x77);
        CHECK_EQ(r.phys(pal + 2 * (64 + i) + 1), r.phys(colours + 2 * i + 1) >> 1);
    }
    const uint64_t dim = frame_hash(r);
    CHECK_EQ(ui(r, 't', 3).substr(6), std::string("00")); // the same groups, TF_DIM clear
    expect(poll(r), {}, "no dimming");
    CHECK_EQ(vid(r).peek(0x19), 2);
    CHECK_NE(frame_hash(r), dim);
    CHECK_EQ(ui(r, 't', 2).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_OVER, 0, 0}}, "hidden"); // (U2: UE_MODAL)
    CHECK_EQ(vid(r).peek(0x19), 2);
    CHECK_EQ(frame_hash(r), house);
}

// ---- round 2 of the review (2026-11-11): each case fails without its fix -------------------------------------

namespace
{
    /// The video registers a program sets: CTRL, the layers', PALBASE and SPRBASE.
    std::vector<uint8_t> vregs(Rig &r)
    {
        std::vector<uint8_t> v;
        for (const unsigned a : {0x00u, 0x01u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du})
            v.push_back(vid(r).peek(static_cast<uint8_t>(a)));
        for (uint8_t a = 0x10; a <= 0x29; ++a)
            v.push_back(vid(r).peek(a));
        return v;
    }
} // namespace

TEST(w1_ui_theme_leaves_the_card_alone_while_shut_or_suspended)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r);
    std::vector<uint8_t> before = vregs(r);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00")); // before ui_open: kept, nothing set
    r.run(ms(40));
    CHECK(vregs(r) == before);
    CHECK_EQ(ui(r, 't', 0).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(r);
    const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8;
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    // The layers are the program's now: it sets layer 0 and CTRL its own way.
    r.machine->bus().write(0xC490, 0x02); // tiles
    r.machine->bus().write(0xC491, 0x00);
    r.machine->bus().write(0xC492, 0x05);
    r.machine->bus().write(0xC499, 0x07); // PALOFS
    r.machine->bus().write(0xC480, 0x03);
    r.run(ms(40));
    before = vregs(r);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    r.run(ms(40));
    CHECK(vregs(r) == before);
    // ui_resume sets OOUI's layers again, in the theme chosen meanwhile.
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    expect(poll(r), {}, "resumed");
    CHECK_EQ(vid(r).peek(0x10), 0);
    CHECK_EQ(static_cast<unsigned>(vid(r).peek(0x11) | vid(r).peek(0x12) << 8), map0);
    CHECK_EQ(vid(r).peek(0x00) & 0x0F, 0x0F);
    if (!updating())
        CHECK_EQ(frame_hash(r), read_golden().at("turbo-window"));
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00")); // after ui_close: kept, nothing set
    r.run(ms(40));
    before = vregs(r);
    CHECK_EQ(ui(r, 't', 0).substr(6), std::string("00"));
    r.run(ms(40));
    CHECK(vregs(r) == before);
}

TEST(w2_an_item_hidden_or_shown_while_its_menu_is_down)
{
    CcpRig a;
    REQUIRE(a.ok);
    open_main(a);
    key(a, K_F, L_ALT);
    poll(a);
    const uint64_t down = frame_hash(a);
    CHECK_EQ(ui(a, 'h', I_SAVE).substr(6), std::string("00"));
    expect(poll(a), {}, "Save hidden");
    CcpRig b;
    REQUIRE(b.ok);
    start_ui(b);
    b.poke(rec(I_SAVE) + UW_FLAGS, static_cast<uint8_t>(b.phys(rec(I_SAVE) + UW_FLAGS) | 0x01));
    CHECK_EQ(ui(b, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(b);
    key(b, K_F, L_ALT);
    poll(b);
    CHECK_EQ(frame_hash(a), frame_hash(b)); // as if opened with Save hidden: no row of it left
    CHECK_EQ(ui(a, 's', I_SAVE).substr(6), std::string("00"));
    expect(poll(a), {}, "Save shown");
    CHECK_EQ(frame_hash(a), down);
}

TEST(w3_w8_dimming_only_under_an_overlay_s_modal_window)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(ui(r, 't', 2).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(flags(r, W_SIDE, UF_MODAL).substr(6), std::string("00")); // a modal window on the base layer
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 1}}, "Second modal"); // (U2: UE_MODAL)
    CHECK_EQ(vid(r).peek(0x19), 2); // not dimmed: the dialog is on the base too
    // W8: one layer, no dimming: the dim group's entries left as they were.
    CcpRig o;
    REQUIRE(o.ok);
    start_ui(o);
    CHECK_EQ(ui(o, 'o', BLK_ONE).substr(6), std::string("00"));
    poll(o);
    const uint16_t pal = lab("GX_PAL");
    o.poke(pal + 2u * 64, std::vector<uint8_t>(32, 0x5A));
    CHECK_EQ(ui(o, 't', 2).substr(6), std::string("00"));
    poll(o);
    for (unsigned i = 0; i < 32; ++i)
        CHECK_EQ(o.phys(pal + 2u * 64 + i), 0x5A);
}

TEST(w4_a_framed_window_of_no_width)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint64_t window = frame_hash(r);
    CHECK_EQ(ui(r, 's', W_THIN).substr(6), std::string("00"));
    expect(poll(r), {}, "Thin shown");
    expect_place(r, W_THIN, {0, 27, 0, 3, false});
    expect_place(r, L_THIN, {1, 28, 0, 1, false}); // its client 0 wide, not 254
    CHECK_EQ(frame_hash(r), window);
    point(r, 5, 28);
    usb(r).mouse_wheel(-1);
    r.run(ms(30));
    expect(poll(r), {{UE_WHEEL, W_DESK, 0, 0xFFFF}}, "the wheel over it: the desktop's");
}

TEST(w5_a_menu_down_goes_up_for_a_modal_window_or_when_disabled)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    key(r, K_F, L_ALT);
    poll(r);
    CHECK_EQ(flags(r, W_OVER, UF_MODAL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00")); // ui_show of a modal window
    expect(poll(r), {{UE_MODAL, W_OVER, 0, 1}}, "a modal window shown"); // (U2: UE_MODAL)
    expect_place(r, M_FILE, {1, 1, 19, 6, true});
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    poll(r);
    key(r, K_F, L_ALT);
    poll(r);
    CHECK_EQ(flags(r, W_SIDE, UF_MODAL).substr(6), std::string("00")); // ui_flags making a shown window modal
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 1}}, "Second made modal"); // (U2: UE_MODAL; Over's 0 came at the poll before)
    expect_place(r, M_FILE, {1, 1, 19, 6, true});
    CHECK_EQ(flags(r, W_SIDE, 0, UF_MODAL).substr(6), std::string("00"));
    poll(r);
    key(r, K_F, L_ALT);
    poll(r);
    CHECK_EQ(flags(r, M_FILE, UF_DISABLED).substr(6), std::string("00")); // the menu down disabled
    expect(poll(r), {}, "File disabled");
    expect_place(r, M_FILE, {1, 1, 19, 6, true});
}

TEST(w10_no_item_chosen_from_a_bar_that_doesn_t_show)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    CHECK_EQ(ui(r, 'h', W_BAR).substr(6), std::string("00"));
    key(r, K_F1);
    expect(poll(r), {}, "F1, the bar hidden");
    CHECK_EQ(ui(r, 's', W_BAR).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'h', W_DESK).substr(6), std::string("00")); // the bar's parent hidden
    key(r, K_F1);
    expect(poll(r), {}, "F1, the desktop hidden");
    CHECK_EQ(ui(r, 's', W_DESK).substr(6), std::string("00"));
    key(r, K_F1);
    expect(poll(r), {{UE_MENU, I_ABOUT, M_HELP, 0}}, "F1, shown again");
}

// ---- U2 (proposed): the remaining widgets, the message box, the file picker -----------------------------------------

namespace
{
    // uitwid's scene (uitest.s's u2, UIVAR 1) and uitbar's (UIVAR 2).
    enum : uint8_t { X_DESK, X_WIN, X_GRP, X_R0, X_R1, X_R2, X_CHO, X_FLD, X_LST, X_VSC, X_LS2, X_CNV, X_HSC, X_LBL, X_OUT, X_DONE };
    enum : uint8_t { Y_DESK, Y_WIN, Y_VBR, Y_VSL, Y_HBR, Y_HSL, Y_HSP, Y_LBL, Y_MSG, Y_BAR };
    enum : uint8_t { BLK_U2 = 3, BLK_U2ONE = 4 };
    template <class R> void open_u2(R &r, const std::string &program, uint8_t block = BLK_U2)
    {
        start_ui(r, program);
        CHECK_EQ(ui(r, 'o', block).substr(6), std::string("00"));
    }
    /// ui_setvalue Y, A/X.
    void setv(Rig &r, uint8_t h, unsigned v) { CHECK_EQ(ui(r, 'v', static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), h).substr(6), std::string("00")); }
} // namespace


namespace
{
    /// The record of handle h in the running program's u2 table.
    uint16_t rec2(uint8_t h) { return static_cast<uint16_t>(lab("u2") + h * 16u); }
    unsigned word_at(Rig &r, uint32_t a) { return r.phys(a) | r.phys(a + 1) << 8; }
    unsigned value_of(Rig &r, uint8_t h) { return word_at(r, rec2(h) + UW_VALUE); }
    /// A dialog record's value: its place k (the picker's 0-8).
    unsigned value_of_dlg(Rig &r, unsigned k) { return word_at(r, lab("UDLG") + k * 16 + UW_VALUE); }
    void click(Rig &r, unsigned col, unsigned row)
    {
        point(r, col, row);
        button(r, true);
        button(r, false);
    }
    void type_text(Rig &r, const std::string &t)
    {
        for (const char c : t)
            key(r, static_cast<uint8_t>(c >= 'a' && c <= 'z' ? 0x04 + c - 'a' : c == '.' ? 0x37 : 0x1E + c - '1'));
    }
    constexpr uint8_t UV_LIVE = 0x01, UV_DOUBLE = 0x02;
} // namespace

TEST(u2_widgets_drawn_in_both_themes)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    expect(poll(r), {}, "the first poll");
    golden(r, "u2-lists");
    // O16 and clipping: a label sticking out of its group, shown, is cut at the group's client area.
    const uint64_t before = frame_hash(r);
    CHECK_EQ(ui(r, 's', X_OUT).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(frame_hash(r), before); // all of it outside: nothing drawn
    CHECK_EQ(ui(r, 'h', X_OUT).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    poll(r);
    golden(r, "u2-lists-turbo");
    CcpRig b;
    REQUIRE(b.ok);
    open_u2(b, "uitbar");
    setv(b, Y_VBR, 37);
    setv(b, Y_HBR, 63);
    expect(poll(b), {}, "the first poll");
    golden(b, "u2-levels");
    CHECK_EQ(ui(b, 't', 1).substr(6), std::string("00"));
    poll(b);
    golden(b, "u2-levels-turbo");
}

TEST(u2_radio_buttons_and_a_choice)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("03")); // the first radio button
    key(r, K_DOWN);
    expect(poll(r), {{UE_CHANGE, X_GRP, 0, 1}}, "Down: Medium");
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("04"));
    key(r, K_UP);
    expect(poll(r), {{UE_CHANGE, X_GRP, 0, 0}}, "Up: Low");
    key(r, 0x0B); // H: High's letter
    expect(poll(r), {{UE_CHANGE, X_GRP, 0, 2}}, "H");
    key(r, K_SPACE); // High has the focus now, and is chosen: nothing
    expect(poll(r), {}, "Space on the chosen one");
    click(r, 6, 4); // Medium
    expect(poll(r), {{UE_CHANGE, X_GRP, 0, 1}}, "a click on Medium");
    CHECK_EQ(value_of(r, X_GRP), 1u);
    // The choice: Right, held at the end; Left; Space round; a click on its halves.
    CHECK_EQ(ui(r, 'f', X_CHO).substr(6), std::string("00"));
    key(r, K_RIGHT);
    key(r, K_RIGHT);
    expect(poll(r), {{UE_CHANGE, X_CHO, 0, 2}}, "Right, Right");
    key(r, K_LEFT);
    key(r, K_SPACE);
    key(r, K_SPACE);
    expect(poll(r), {{UE_CHANGE, X_CHO, 0, 1}, {UE_CHANGE, X_CHO, 0, 2}, {UE_CHANGE, X_CHO, 0, 0}}, "Left, Space, Space");
    click(r, 40, 2); // its right half (each click polled: a click is where the pointer is when ui_poll reads it)
    expect(poll(r), {{UE_CHANGE, X_CHO, 0, 1}}, "right half");
    click(r, 30, 2); // its left half
    expect(poll(r), {{UE_CHANGE, X_CHO, 0, 0}}, "left half");
}

TEST(u2_a_text_field)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(ui(r, 'f', X_FLD).substr(6), std::string("00"));
    type_text(r, "abc");
    std::vector<Event> ev = poll(r);
    expect(ev, {{UE_CHANGE, X_FLD, 0, 1}, {UE_CHANGE, X_FLD, 0, 2}, {UE_CHANGE, X_FLD, 0, 3}}, "abc");
    for (const Event &e : ev)
        CHECK_EQ(e.flags & UV_LIVE, UV_LIVE);
    key(r, K_BACK);
    key(r, K_HOME);
    key(r, K_DEL);
    expect(poll(r), {{UE_CHANGE, X_FLD, 0, 2}, {UE_CHANGE, X_FLD, 0, 0}}, "Backspace, Home, Delete");
    const uint16_t buf = lab("fieldbuf");
    CHECK_EQ(r.phys(buf), 'b');
    CHECK_EQ(r.phys(buf + 1u), 0);
    key(r, K_END);
    golden(r, "u2-field"); // (the frame before the poll: as it was drawn last)
    key(r, K_ENTER);
    ev = poll(r);
    expect(ev, {{UE_CHANGE, X_FLD, 0, 1}}, "End, Enter: the last change");
    CHECK(!ev.empty() && !(ev[0].flags & UV_LIVE));
    type_text(r, "x"); // a change, then the focus leaves: its last change follows UE_FOCUS
    key(r, K_TAB);
    ev = poll(r);
    expect(ev, {{UE_CHANGE, X_FLD, 0, 2}, {UE_FOCUS, X_LST, 0, 0}, {UE_CHANGE, X_FLD, 0, 2}}, "x, Tab");
    CHECK(ev.size() == 3 && (ev[0].flags & UV_LIVE) && !(ev[2].flags & UV_LIVE));
    // A click puts the cursor; the field is 16 bytes, 15 characters: the 16th refused.
    CHECK_EQ(ui(r, 'f', X_FLD).substr(6), std::string("00"));
    click(r, 26, 4);
    poll(r);
    CHECK_EQ(value_of(r, X_FLD), 0u);
}

TEST(u2_a_list_by_keys_and_the_mouse)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(ui(r, 'f', X_LST).substr(6), std::string("00"));
    key(r, K_DOWN);
    key(r, K_DOWN);
    key(r, K_END);
    expect(poll(r), {{UE_CHANGE, X_LST, 0, 0}, {UE_CHANGE, X_LST, 0, 1}, {UE_CHANGE, X_LST, 0, 19}}, "Down, Down, End");
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 10u); // the top moved: row 19 the last shown
    golden(r, "u2-list-end");
    key(r, K_PGUP);
    key(r, K_HOME);
    key(r, K_ENTER);
    expect(poll(r), {{UE_CHANGE, X_LST, 0, 10}, {UE_CHANGE, X_LST, 0, 0}, {UE_SELECT, X_LST, 0, 0}}, "PgUp, Home, Enter");
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 0u);
    key(r, K_SPACE); // not a UF_MULTI list: Space isn't its
    expect(poll(r), {}, "Space");
    click(r, 10, 11); // row 3
    expect(poll(r), {{UE_CHANGE, X_LST, 0, 3}}, "a click on row 3");
    click(r, 10, 11);
    std::vector<Event> ev = poll(r);
    expect(ev, {{UE_SELECT, X_LST, 0, 3}}, "a double-click on it");
    CHECK(!ev.empty() && (ev[0].flags & UV_DOUBLE));
    click(r, 33, 16); // its scroll bar, below the thumb: a page down
    expect(poll(r), {}, "a page down by its bar");
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 10u);
    point(r, 10, 12);
    usb(r).mouse_wheel(3); // three up
    r.run(ms(30));
    expect(poll(r), {}, "the wheel");
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 7u);
    // Key repeat: Down held for 0.8 s moves on after the delay (30 frames) every 2 frames.
    CHECK_EQ(ui(r, 'f', X_LST).substr(6), std::string("00"));
    usb(r).key(K_DOWN, true);
    ev.clear();
    for (int i = 0; i < 16; ++i) // polled while it is held, as a program polls each frame
    {
        r.run(ms(50));
        for (const Event &e : poll(r))
            ev.push_back(e);
    }
    usb(r).key(K_DOWN, false);
    r.run(ms(20));
    poll(r);
    if (ev.size() < 6)
        std::printf("  held Down: %zu events\n", ev.size());
    CHECK(ev.size() >= 6);
    // The second list: its routine's rows, marks.
    CHECK_EQ(ui(r, 'f', X_LS2).substr(6), std::string("00"));
    key(r, K_DOWN);
    key(r, K_SPACE);
    expect(poll(r), {{UE_CHANGE, X_LS2, 0, 0}, {UE_MARK, X_LS2, 0, 0}}, "Down, Space on the marked list");
    click(r, 42, 10); // row 2: a mark
    expect(poll(r), {{UE_MARK, X_LS2, 0, 2}}, "a click on the marked list");
}

TEST(u2_scroll_bars)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    click(r, 35, 17); // the down arrow, twice
    click(r, 35, 17);
    expect(poll(r), {{UE_CHANGE, X_VSC, 0, 1}, {UE_CHANGE, X_VSC, 0, 2}}, "the down arrow");
    click(r, 35, 8); // the up arrow
    expect(poll(r), {{UE_CHANGE, X_VSC, 0, 1}}, "the up arrow");
    click(r, 35, 15); // the track below the thumb: a page
    expect(poll(r), {{UE_CHANGE, X_VSC, 0, 11}}, "a page");
    key(r, 0x3E); // F5: UA_UP
    key(r, 0x3F); // F6: UA_DOWN
    key(r, 0x3F);
    expect(poll(r), {{UE_CHANGE, X_VSC, 0, 10}, {UE_CHANGE, X_VSC, 0, 11}, {UE_CHANGE, X_VSC, 0, 12}}, "F5, F6, F6");
    point(r, 35, 12);
    usb(r).mouse_wheel(2);
    r.run(ms(30));
    expect(poll(r), {{UE_CHANGE, X_VSC, 0, 10}}, "the wheel up 2");
    // The thumb dragged: at 10 of 50 its place is 7 x 10 / 50 = 1 (row 10); down to row 16, the end.
    point(r, 35, 10);
    button(r, true);
    expect(poll(r), {}, "the thumb held");
    point(r, 35, 13);
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && (ev[0].flags & UV_LIVE) && ev[0].value > 10);
    point(r, 35, 16);
    poll(r);
    button(r, false);
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && !(ev[0].flags & UV_LIVE) && ev[0].value == 50);
    click(r, 33, 19); // the horizontal bar's right arrow
    expect(poll(r), {{UE_CHANGE, X_HSC, 0, 1}}, "the horizontal bar's right arrow");
}

TEST(u2_sliders_and_bars)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("03")); // the vertical slider
    key(r, K_UP);
    key(r, K_DOWN);
    key(r, K_PGUP);
    key(r, K_HOME);
    key(r, K_END);
    expect(poll(r), {{UE_CHANGE, Y_VSL, 0, 55}, {UE_CHANGE, Y_VSL, 0, 50}, {UE_CHANGE, Y_VSL, 0, 62}, {UE_CHANGE, Y_VSL, 0, 100},
                     {UE_CHANGE, Y_VSL, 0, 0}},
           "Up, Down, PgUp, Home, End");
    // A press at its top row: (79 - 4) x 100 / 79 = 94, live; let go: the last.
    point(r, 6, 2);
    button(r, true);
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].value == 94 && (ev[0].flags & UV_LIVE));
    button(r, false);
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].value == 94 && !(ev[0].flags & UV_LIVE));
    // The horizontal one: a drag from column 24 (62 x 100 / 119 = 52).
    point(r, 24, 4);
    button(r, true);
    ev = poll(r); // the focus moves there silently, and the press's change is the event
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Y_HSL && ev[0].value == 52);
    button(r, false);
    poll(r);
    CHECK_EQ(value_of(r, Y_HSL), 52u);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("05"));
    point(r, 20, 4);
    usb(r).mouse_wheel(1);
    r.run(ms(30));
    expect(poll(r), {{UE_CHANGE, Y_HSL, 0, 57}}, "the wheel: a step up");
    // The sprite thumb: at 75, x = 9 x 4 + 75 x 119 / 100 - 4; hidden with its window, out of sight.
    const uint16_t spr = lab("GX_SPR");
    CHECK_EQ(word_at(r, spr + 8u + 2), 9u * 4 + 75u * 119 / 100 - 4);
    CHECK_EQ(r.phys(spr + 8u + 6) & 0x0C, 0x0C);
    setv(r, Y_HSP, 0);
    poll(r);
    CHECK_EQ(word_at(r, spr + 8u + 2), 9u * 4 - 4);
    CHECK_EQ(ui(r, 'h', Y_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(r.phys(spr + 8u + 6) & 0x0C, 0);
    // A bar's value is held to its top.
    setv(r, Y_VBR, 500);
    CHECK_EQ(value_of(r, Y_VBR), 100u);
    CHECK_EQ(ui(r, 'q', 40, 0, Y_VBR).substr(6), std::string("00")); // ui_setrange: the top 40
    CHECK_EQ(value_of(r, Y_VBR), 40u);
}

TEST(u2_a_canvas_hover_and_a_mouse_plugged_in)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    point(r, 60, 10);
    button(r, true);
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CANVAS && ev[0].handle == X_CNV && ev[0].kind == 1 && ev[0].x == 22 && ev[0].py == 20);
    point(r, 61, 10);
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].kind == 3 && ev[0].x == 26);
    button(r, false);
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].kind == 2);
    usb(r).mouse_wheel(-1);
    r.run(ms(30));
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].kind == 4 && ev[0].value == 0xFFFF);
    // US_HOT: the widget under the pointer.
    point(r, 6, 26);
    poll(r);
    CHECK_EQ(r.phys(rec2(X_DONE) + 7u) & 0x02, 0x02);
    point(r, 60, 10);
    poll(r);
    CHECK_EQ(r.phys(rec2(X_DONE) + 7u) & 0x02, 0);
    // O13: a mouse plugged in after ui_open gets the pointer. Opened with the area saying no mouse, then an
    // EK_DEVICE event saying one came.
    CcpRig m;
    REQUIRE(m.ok);
    start_ui(m, "uitwid");
    const uint32_t area = 0x3000;
    m.poke(area + 25, 0x01); // EV_DEVICES: a keyboard only
    CHECK_EQ(ui(m, 'o', BLK_U2).substr(6), std::string("00"));
    poll(m);
    CHECK_EQ(m.phys(lab("uptr")), 0);
    const uint8_t head = m.phys(area + 16), slots = m.phys(area + 18);
    m.poke(area + 128 + head * 8u, std::vector<uint8_t>{5, 0, 3, 0, 0, 0, 0, 0}); // EK_DEVICE: keyboard and mouse
    m.poke(area + 16, static_cast<uint8_t>((head + 1) % slots));
    m.poke(area + 25, 0x03); // (OODOS writes EV_DEVICES as it queues the event)
    ev = poll(m);
    CHECK(ev.size() == 1 && ev[0].code == UE_RAW && ev[0].y == 5);
    CHECK_EQ(m.phys(lab("uptr")), 1);
}

TEST(u2_the_message_box)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(ui(r, 'm', 0).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, 0xF0, 0, 1}}, "the box up");
    golden(r, "u2-msg");
    CHECK_EQ(ui(r, 'm', 1).substr(0, 2) + ui(r, 'm', 1).substr(6), std::string("0601")); // one up: UX_BUSY
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("F3")); // OK, its first button
    key(r, K_ENTER);
    expect(poll(r), {{UE_CLOSE, 0xF0, 0, 0}, {UE_MODAL, 0xF0, 0, 0}}, "Enter: OK");
    CHECK_EQ(ui(r, 'm', 1).substr(6), std::string("00")); // Yes and No, No the default
    poll(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("F4"));
    key(r, K_ENTER);
    expect(poll(r), {{UE_CLOSE, 0xF0, 0, 1}, {UE_MODAL, 0xF0, 0, 0}}, "Enter: No");
    ui(r, 'm', 1);
    poll(r);
    key(r, 0x1C); // Y
    expect(poll(r), {{UE_CLOSE, 0xF0, 0, 0}, {UE_MODAL, 0xF0, 0, 0}}, "Y");
    ui(r, 'm', 1);
    poll(r);
    key(r, K_ESC);
    expect(poll(r), {{UE_CLOSE, 0xF0, 0, 1}, {UE_MODAL, 0xF0, 0, 0}}, "Escape: No");
    ui(r, 'm', 2); // OK and Cancel: Cancel clicked
    poll(r);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    poll(r);
    golden(r, "u2-msg-turbo");
    const Place cancel = rect(r, 0xF4);
    click(r, cancel.col + 2, cancel.row);
    expect(poll(r), {{UE_CLOSE, 0xF0, 0, 1}, {UE_MODAL, 0xF0, 0, 0}}, "Cancel clicked"); // (its widgets' own events kept from the program)
    ui(r, 'm', 0); // taken away by ui_hide: no answer
    poll(r);
    CHECK_EQ(ui(r, 'h', 0xF0).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, 0xF0, 0, 0}}, "ui_hide(UI_MSG)");
}


namespace
{
    // The picker's public handles.
    constexpr uint8_t P_WIN = 0xF1, P_DRV = 0xF5, P_USR = 0xF6, P_LST = 0xF7, P_FLD = 0xF8, P_OK = 0xF9, P_CAN = 0xFA;
    /// Polls until the listing is done (a few more entries a poll).
    std::vector<Event> settle(Rig &r)
    {
        std::vector<Event> all;
        for (int i = 0; i < 400; ++i)
        {
            for (const Event &e : poll(r))
                all.push_back(e);
            if (r.phys(lab("lst")) == 0)
                break;
        }
        CHECK_EQ(r.phys(lab("lst")), 0);
        return all;
    }
    /// ui_pick with block `blk` and the listing polled to its end: the events on the way.
    std::vector<Event> pick(Rig &r, uint8_t blk)
    {
        CHECK_EQ(ui(r, 'k', blk).substr(6), std::string("00"));
        return settle(r);
    }
    /// The files listed: "NAME.TYP", in the work buffer's order.
    std::vector<std::string> listed(Rig &r)
    {
        std::vector<std::string> names;
        const unsigned n = word_at(r, lab("count"));
        for (unsigned i = 0; i < n; ++i)
        {
            std::string a, b;
            for (unsigned k = 0; k < 8; ++k)
                if (const char c = static_cast<char>(r.phys(WORK + i * 16 + 2 + k)); c != ' ')
                    a += c;
            for (unsigned k = 8; k < 11; ++k)
                if (const char c = static_cast<char>(r.phys(WORK + i * 16 + 2 + k)); c != ' ')
                    b += c;
            names.push_back(a + "." + b);
        }
        return names;
    }
    bool has(const std::vector<std::string> &v, const std::string &n) { return std::find(v.begin(), v.end(), n) != v.end(); }
    /// The status label's text.
    std::string status(Rig &r)
    {
        const uint32_t rec = lab("UDLG") + 8u * 16;
        uint32_t p = word_at(r, rec + UW_TEXT);
        if (p >= 0xC800 && p < 0xF800) // a string of OOUI.BNK's UICOM, the same in each block: block A's (U3a)
            p = r.phys(lab("UI_BLOCKS")) * 0x4000u + p - 0xC000;
        std::string t;
        for (uint8_t c; (c = r.phys(p)) != 0 && t.size() < 40; ++p)
            t += static_cast<char>(c);
        return t;
    }
    /// The result buffer's entry i: drive, user, "NAME    TYP".
    std::string result(Rig &r, unsigned i)
    {
        std::string t = std::to_string(r.phys(RESULT + i * 16)) + " " + std::to_string(r.phys(RESULT + i * 16 + 1)) + " ";
        for (unsigned k = 0; k < 11; ++k)
            t += static_cast<char>(r.phys(RESULT + i * 16 + 2 + k));
        return t;
    }
    unsigned chosen(Rig &r, const char *block) { return r.phys(lab(block) + 10u); }
    void next_drive(Rig &r)
    {
        key(r, K_RIGHT);
        settle(r);
    }
} // namespace

TEST(u2_the_picker_on_the_card_s_drives)
{
    CcpRig r;
    REQUIRE(r.ok);
    // M: forty files, more than a screen, put there before it is logged in; C: the card's apple-do disk; D:
    // nothing mounted.
    std::vector<MFile> m;
    for (unsigned i = 0; i < 40; ++i)
    {
        char n[12];
        std::snprintf(n, sizeof n, "FILE%02u  %s", i, i % 4 == 3 ? "COM" : i % 2 ? "BIN" : "TXT");
        m.push_back({0, n, std::vector<uint8_t>(100, static_cast<uint8_t>(i))});
    }
    r.put_m(m);
    REQUIRE(r.mount(3, "/DISKS/CPM.DSK") == 0);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    // A: (oodos-8m): every file of user 0, once each, SYS ones too; more than its 16 rows.
    std::vector<Event> ev = pick(r, 0);
    CHECK(!ev.empty() && ev[0].code == UE_MODAL && ev[0].handle == P_WIN && ev[0].value == 1);
    std::vector<std::string> a = listed(r);
    CHECK(a.size() > 16);
    CHECK(has(a, "README.TXT") && has(a, "PATTERN.BIN") && has(a, "HELLO.COM") && has(a, "OODOS.SYS"));
    CHECK(!has(a, "USER3.TXT")); // user 3's
    std::vector<std::string> sorted = a;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end()); // each once
    CHECK_EQ(status(r), std::to_string(a.size()) + " files");
    golden(r, "u2-pick");
    // Scrolled to the end: the last name in the field.
    key(r, K_END);
    expect(poll(r), {}, "End (the picker's own)");
    CHECK_EQ(value_of_dlg(r, 3), static_cast<unsigned>(a.size() - 1));
    std::string fld;
    for (uint32_t p = lab("fbuf"); r.phys(p); ++p)
        fld += static_cast<char>(r.phys(p));
    CHECK_EQ(fld, a.back());
    // B: empty; C: apple-do (the card's CPM.DSK: README.TXT and PATTERN.BIN for user 0); D: no drive.
    CHECK_EQ(ui(r, 'f', P_DRV).substr(6), std::string("00"));
    next_drive(r);
    CHECK_EQ(listed(r).size(), 0u);
    CHECK_EQ(status(r), std::string("No files"));
    next_drive(r);
    std::vector<std::string> c = listed(r);
    CHECK(has(c, "README.TXT") && has(c, "PATTERN.BIN") && !has(c, "SMALL.BIN"));
    next_drive(r);
    CHECK_EQ(status(r), std::string("Drive error"));
    for (int i = 0; i < 4; ++i) // E:, F:, G:, then M:
        next_drive(r);
    std::vector<std::string> mm = listed(r);
    CHECK_EQ(mm.size(), 40u);
    // User 3 on A: again.
    CHECK_EQ(ui(r, 'f', P_DRV).substr(6), std::string("00"));
    key(r, K_HOME); // (Home isn't a choice's: nothing)
    for (int i = 0; i < 7; ++i)
        key(r, K_LEFT);
    settle(r);
    CHECK_EQ(ui(r, 'f', P_USR).substr(6), std::string("00"));
    key(r, K_RIGHT);
    key(r, K_RIGHT);
    key(r, K_RIGHT);
    settle(r);
    std::vector<std::string> u3 = listed(r);
    CHECK(has(u3, "USER3.TXT") && has(u3, "SMALL.BIN") && u3.size() == 2);
    // Chosen: Down, Enter on the list: the second file, user 3, drive 1.
    CHECK_EQ(ui(r, 'f', P_LST).substr(6), std::string("00"));
    key(r, K_DOWN);
    key(r, K_DOWN);
    key(r, K_ENTER);
    ev = poll(r);
    expect(ev, {{UE_PICK, P_WIN, 0, 0}, {UE_MODAL, P_WIN, 0, 0}}, "Enter on the list");
    CHECK_EQ(chosen(r, "pickA"), 1u);
    std::string want = u3[1];
    const size_t dot = want.find('.');
    want = want.substr(0, dot) + std::string(8 - dot, ' ') + want.substr(dot + 1) + std::string(3 - (want.size() - dot - 1), ' ');
    CHECK_EQ(result(r, 0), "1 3 " + want);
}

TEST(u2_the_picker_s_marks_types_buttons_and_a_typed_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<MFile> m;
    for (unsigned i = 0; i < 12; ++i)
    {
        char n[12];
        std::snprintf(n, sizeof n, "DATA%02u  %s", i, i % 3 == 0 ? "COM" : i % 3 == 1 ? "TXT" : "BIN");
        m.push_back({0, n, std::vector<uint8_t>(10, 1)});
    }
    r.put_m(m);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    // Block 1: M:'s TXT and BIN files only.
    pick(r, 1);
    std::vector<std::string> t = listed(r);
    CHECK_EQ(t.size(), 8u);
    for (const std::string &n : t)
        CHECK(n.substr(n.size() - 3) != "COM");
    key(r, K_ESC);
    expect(poll(r), {{UE_PICK, P_WIN, 0, 1}, {UE_MODAL, P_WIN, 0, 0}}, "Escape: cancelled");
    CHECK_EQ(chosen(r, "pickB"), 0u);
    // Block 0 on A:: two marked (Space, Space on another), then OK; the extra button by its letter.
    pick(r, 0);
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    poll(r);
    golden(r, "u2-pick-turbo");
    CHECK_EQ(ui(r, 't', 0).substr(6), std::string("00"));
    key(r, K_DOWN);
    key(r, K_SPACE);
    key(r, K_DOWN);
    key(r, K_DOWN);
    key(r, K_SPACE);
    expect(poll(r), {}, "marks (the picker's own)");
    key(r, 0x10); // M: "&Mount", the extra button: UE_PICK 2, still up
    expect(poll(r), {{UE_PICK, P_WIN, 0, 2}}, "the extra button");
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    expect(poll(r), {{UE_PICK, P_WIN, 0, 0}, {UE_MODAL, P_WIN, 0, 0}}, "OK");
    CHECK_EQ(chosen(r, "pickA"), 2u);
    CHECK(result(r, 0) != result(r, 1));
    // Block 2: a name typed on B: (UP_NEW).
    pick(r, 2);
    CHECK_EQ(status(r), std::string("No files"));
    CHECK_EQ(ui(r, 'f', P_FLD).substr(6), std::string("00"));
    type_text(r, "new.txt");
    key(r, K_ENTER);
    std::vector<Event> ev = poll(r);
    CHECK(!ev.empty() && ev[0].code == UE_PICK && ev[0].value == 0);
    CHECK_EQ(chosen(r, "pickC"), 1u);
    CHECK_EQ(result(r, 0), std::string("2 0 NEW     TXT"));
    // Refused: a picker up already.
    pick(r, 1);
    CHECK_EQ(ui(r, 'k', 0).substr(0, 2) + ui(r, 'k', 0).substr(6), std::string("0601"));
    CHECK_EQ(ui(r, 'l').substr(6), std::string("00")); // ui_relist
    settle(r);
    CHECK_EQ(listed(r).size(), 8u);
}

TEST(u2_the_picker_on_an_oodos_32m_drive)
{
    // A card of its own: BIG.IMG (32 MB, oodos-32m) as A:, its directory $E5s but for 24 files' entries.
    std::vector<uint8_t> card = fat16_card({{"BIG.IMG", 65536u * 512}});
    auto word = [&](size_t at) { return size_t{card[at]} | size_t{card[at + 1]} << 8; };
    const size_t root = (word(14) + card[16] * word(22)) * SECTOR;
    const size_t data = root + word(17) * 32;
    std::fill(card.begin() + static_cast<long>(data), card.begin() + static_cast<long>(data + 2048 * 32), 0xE5);
    for (unsigned i = 0; i < 24; ++i)
    {
        uint8_t *e = card.data() + data + i * 32;
        std::memset(e, 0, 32);
        char n[12];
        std::snprintf(n, sizeof n, "BIG%02u   DAT", i);
        std::memcpy(e + 1, n, 11);
        e[15] = 1;                                   // RC: a record
        e[16] = static_cast<uint8_t>(8 + i);         // its block (16-bit pointers: 4,096 blocks)
    }
    Rig r(card);
    REQUIRE(r.ok);
    REQUIRE(r.mount(1, "/BIG.IMG") == 0);
    REQUIRE(r.boot());
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 0);
    std::vector<std::string> b = listed(r);
    CHECK_EQ(b.size(), 24u);
    CHECK(has(b, "BIG00.DAT") && has(b, "BIG23.DAT"));
    CHECK_EQ(status(r), std::string("24 files"));
}

// ---------------------------------------------------------------------------
// U2's review fixes (Y1-Y8, X1-X13): each case fails without its fix.

namespace
{
    /// M: with `n` files FILEnn, TXT and BIN in turn (`big` a 40 KB TXT more: two directory entries).
    void m_files(CcpRig &r, unsigned n, bool big = false)
    {
        std::vector<MFile> m;
        for (unsigned i = 0; i < n; ++i)
        {
            char nm[12];
            std::snprintf(nm, sizeof nm, "FILE%02u  %s", i % 100u, i % 2 ? "BIN" : "TXT");
            m.push_back({0, nm, std::vector<uint8_t>(100, static_cast<uint8_t>(i))});
        }
        if (big)
            m.push_back({0, "BIGFILE TXT", std::vector<uint8_t>(40000, 0x55)});
        r.put_m(m);
    }
    constexpr uint32_t STATE = 0x3900, PDMA = 0x3A00; ///< uitest.s's (UITPICK)
    /// The program's own search of A: to its end (uitest.s's c_search), between two polls: the names it found.
    std::vector<std::string> own_search(Rig &r, unsigned stop_after = 1000)
    {
        std::vector<std::string> names;
        for (unsigned i = 0; i < 200 && names.size() < stop_after; ++i)
        {
            const unsigned a = static_cast<unsigned>(std::stoul(ui(r, 'n', i ? 1 : 0).substr(0, 2), nullptr, 16));
            if (a > 3)
                break;
            std::string n;
            for (unsigned k = 1; k <= 11; ++k)
                n += static_cast<char>(r.phys(PDMA + a * 32 + k) & 0x7F);
            if (r.phys(PDMA + a * 32) == 0) // user 0's
                names.push_back(n);
        }
        return names;
    }
    /// The picker's listing as it stands, sorted (listed() with the order dropped).
    std::vector<std::string> sorted_listing(Rig &r)
    {
        std::vector<std::string> v = listed(r);
        std::sort(v.begin(), v.end());
        return v;
    }
    /// Keys for a name typed in a field: letters, digits, '.', ':', ';', '*', '?' and a space; polled after
    /// each, so the event queue holds them all.
    void type_name(Rig &r, const std::string &t)
    {
        for (const char c : t)
        {
            poll(r);
            if (c >= 'a' && c <= 'z')
                key(r, static_cast<uint8_t>(0x04 + c - 'a'));
            else if (c >= '1' && c <= '9')
                key(r, static_cast<uint8_t>(0x1E + c - '1'));
            else if (c == '0')
                key(r, 0x27);
            else if (c == '.')
                key(r, 0x37);
            else if (c == ';')
                key(r, 0x33);
            else if (c == ':')
                key(r, 0x33, L_SHIFT);
            else if (c == '*')
                key(r, 0x25, L_SHIFT);
            else if (c == '?')
                key(r, 0x38, L_SHIFT);
            else if (c == ' ')
                key(r, K_SPACE);
        }
    }
    /// The program's user, DMA address and segment and error mode, as uitest.s's c_state reads them.
    std::string own_state(Rig &r)
    {
        CHECK_EQ(ui(r, 'w').substr(6), std::string("00"));
        char b[40];
        std::snprintf(b, sizeof b, "user %u dma %04X seg %04X err %02X", r.phys(STATE), word_at(r, STATE + 1), word_at(r, STATE + 3),
                      r.phys(STATE + 5));
        return b;
    }
    /// The base layer's character at (col, row).
    char cell_char(Rig &r, unsigned col, unsigned row)
    {
        const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8;
        return static_cast<char>(vid(r).card_peek(map0 * 256u + row * 160u + col * 2u));
    }
    /// Sprite n's shadow in OOLIB's table: its image word (page and depth bit), X, attribute.
    unsigned spr_image(Rig &r, unsigned n) { return word_at(r, lab("GX_SPR") + n * 8u); }
    unsigned spr_depth(Rig &r, unsigned n) { return r.phys(lab("GX_SPR") + n * 8u + 6) & 0x0C; }
} // namespace

TEST(y1_a_program_s_search_between_the_picker_s_polls)
{
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 40);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'e', 0, 0).substr(6), std::string("00")); // user 0, DMA PDMA, the error mode 0
    const std::vector<std::string> a_files = own_search(r);
    CHECK(a_files.size() > 16);
    // The picker on M:'s TXT and BIN (block 1): a poll lists four; the program searches A: to its end between
    // polls, then half way, then to the end again. Each walk is right, and the picker lists all 40.
    CHECK_EQ(ui(r, 'k', 1).substr(6), std::string("00"));
    poll(r);
    CHECK(r.phys(lab("lst")) != 0);
    CHECK(own_search(r) == a_files);
    poll(r);
    CHECK(own_search(r, 3).size() == 3);
    poll(r);
    CHECK(own_search(r) == a_files);
    settle(r);
    std::vector<std::string> want;
    for (unsigned i = 0; i < 40; ++i)
    {
        char nm[16];
        std::snprintf(nm, sizeof nm, "FILE%02u.%s", i % 100u, i % 2 ? "BIN" : "TXT");
        want.push_back(nm);
    }
    CHECK(sorted_listing(r) == want);
    CHECK_EQ(status(r), std::string("40 files"));
}

TEST(y2_the_picker_hidden_mid_listing_stops_its_walk)
{
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 40);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'k', 1).substr(6), std::string("00"));
    poll(r);
    CHECK(r.phys(lab("lst")) != 0);
    CHECK_EQ(ui(r, 'h', P_WIN).substr(6), std::string("00")); // ui_hide(UI_PICKER): closed, no event
    poll(r);
    const unsigned n = word_at(r, lab("count"));
    CHECK_EQ(r.phys(lab("lst")), 0);
    CHECK_EQ(r.phys(lab("uidle") + 1u), 0);
    for (int i = 0; i < 5; ++i)
        expect(poll(r), {}, "polls after the picker went");
    CHECK_EQ(word_at(r, lab("count")), n);
    // A picker again lists from the start.
    pick(r, 1);
    CHECK_EQ(listed(r).size(), 40u);
}

TEST(y3_a_typed_name_that_isn_t_one_file_s_is_refused)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 2); // UP_NEW on B:
    CHECK_EQ(ui(r, 'f', P_FLD).substr(6), std::string("00"));
    for (const char *bad : {"a*.txt", "a?.txt", "a b.txt", "a;b.txt", "abcdefghi.tx", "a.text", ".txt", "a.b.c", "q:x.txt", "a:b:c"})
    {
        type_name(r, bad);
        poll(r);
        std::string fld;
        for (uint32_t p = lab("fbuf"); r.phys(p); ++p)
            fld += static_cast<char>(std::tolower(r.phys(p)));
        CHECK_EQ(fld, std::string(bad)); // typed whole (the field folds to capitals)
        key(r, K_ENTER);
        const std::vector<Event> ev = poll(r);
        if (!ev.empty())
            std::printf("  \"%s\": %s\n", bad, show(ev).c_str());
        CHECK(ev.empty());
        CHECK_EQ(status(r), std::string("Not a file name"));
        CHECK_EQ(chosen(r, "pickC"), 0u);
        for (int i = 0; i < 16; ++i)
        {
            key(r, K_BACK);
            poll(r);
        }
    }
    // "c:" is the drive: C:, not the B: the block lists.
    type_name(r, "c:new.txt");
    poll(r);
    key(r, K_ENTER);
    const std::vector<Event> ev = poll(r);
    CHECK(!ev.empty() && ev[0].code == UE_PICK && ev[0].value == 0);
    CHECK_EQ(chosen(r, "pickC"), 1u);
    CHECK_EQ(result(r, 0), std::string("3 0 NEW     TXT"));
}

TEST(y4_the_program_s_state_after_each_batch_and_a_drive_error)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'e', 5, 0xFE, 1).substr(6), std::string("00")); // user 5, DMA PDMA, the error mode $FE, segment $1234
    const std::string mine = own_state(r);
    CHECK_EQ(mine, std::string("user 5 dma 3A00 seg 1234 err FE")); // (Y4b: the picker's segment is $FFFF)
    CHECK_EQ(ui(r, 'k', 0).substr(6), std::string("00")); // A:, the drives and users
    for (int i = 0; i < 40 && r.phys(lab("lst")); ++i)
    {
        poll(r);
        CHECK_EQ(own_state(r), mine);
    }
    CHECK_EQ(r.phys(lab("lst")), 0);
    CHECK_EQ(ui(r, 'f', P_USR).substr(6), std::string("00")); // the picker's user 6
    key(r, K_RIGHT);
    poll(r);
    CHECK_EQ(own_state(r), mine);
    settle(r);
    CHECK_EQ(own_state(r), mine);
    CHECK_EQ(ui(r, 'f', P_DRV).substr(6), std::string("00")); // B:, C:, D: (no drive: an error)
    for (int i = 0; i < 3; ++i)
    {
        key(r, K_RIGHT);
        settle(r);
        CHECK_EQ(own_state(r), mine);
    }
    CHECK_EQ(status(r), std::string("Drive error"));
}

TEST(y5_a_file_of_two_directory_entries_listed_once)
{
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 6, true);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 1);
    const std::vector<std::string> v = sorted_listing(r);
    CHECK_EQ(v.size(), 7u);
    CHECK_EQ(std::count(v.begin(), v.end(), std::string("BIGFILE.TXT")), 1);
    CHECK_EQ(status(r), std::string("7 files"));
}

TEST(y6_a_full_work_buffer_says_how_many_it_left_out)
{
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 12);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 3); // every file of M: into a buffer of 8
    CHECK_EQ(listed(r).size(), 8u);
    CHECK_EQ(word_at(r, lab("count")), 8u);
    CHECK_EQ(status(r), std::string("8 of 12 files"));
    CHECK_EQ(value_of_dlg(r, 3) == 0xFFFF || value_of_dlg(r, 3) < 8u, true);
}

TEST(x1_a_double_click_expires)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    auto selected = [](const std::vector<Event> &ev) {
        return std::any_of(ev.begin(), ev.end(), [](const Event &e) { return e.code == UE_SELECT; });
    };
    click(r, 10, 11); // row 3
    expect(poll(r), {{UE_CHANGE, X_LST, 0, 3}}, "a click on row 3");
    // 266 frames later (10 past 256): a first click, not a second.
    r.run(ms(4433));
    click(r, 10, 11);
    std::vector<Event> ev = poll(r);
    if (selected(ev))
        std::printf("  266 frames on: %s\n", show(ev).c_str());
    CHECK(!selected(ev));
    // Within the time but after a press on the label, a key, or a suspend: a first click.
    r.run(ms(600));
    click(r, 10, 11);
    poll(r);
    click(r, 10, 21); // the label
    poll(r);
    click(r, 10, 11);
    CHECK(!selected(poll(r)));
    r.run(ms(600));
    click(r, 10, 11);
    poll(r);
    key(r, K_Z);
    click(r, 10, 11);
    {
        CHECK(!selected(poll(r)));
    }
    r.run(ms(600));
    click(r, 10, 11);
    poll(r);
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    click(r, 10, 11);
    CHECK(!selected(poll(r)));
    // And a double-click still is one.
    r.run(ms(600));
    click(r, 10, 11);
    poll(r);
    click(r, 10, 11);
    CHECK(selected(poll(r)));
}

TEST(x2_a_drag_far_past_either_end_of_a_long_range)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    // The sliders, their tops 60,000: past the right (horizontal) or the top (vertical) is the top, past the
    // other end 0.
    CHECK_EQ(ui(r, 'q', 0x60, 0xEA, Y_HSL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'q', 0x60, 0xEA, Y_VSL).substr(6), std::string("00"));
    poll(r);
    point(r, 9, 4);
    button(r, true);
    poll(r);
    for (const auto &[col, want] : std::vector<std::pair<unsigned, unsigned>>{{79, 60000}, {0, 0}, {79, 60000}})
    {
        point(r, col, 4);
        poll(r);
        CHECK_EQ(value_of(r, Y_HSL), want);
    }
    button(r, false);
    poll(r);
    point(r, 6, 11);
    button(r, true);
    poll(r);
    for (const auto &[row, want] : std::vector<std::pair<unsigned, unsigned>>{{0, 60000}, {29, 0}, {0, 60000}})
    {
        point(r, 6, row);
        poll(r);
        CHECK_EQ(value_of(r, Y_VSL), want);
    }
    button(r, false);
    poll(r);
    // The scroll bars, their last 60,000: the thumb (1 cell, at 0) dragged past each end.
    CcpRig w;
    REQUIRE(w.ok);
    open_u2(w, "uitwid");
    poll(w);
    CHECK_EQ(ui(w, 'q', 0x60, 0xEA, X_VSC).substr(6), std::string("00"));
    CHECK_EQ(ui(w, 'q', 0x60, 0xEA, X_HSC).substr(6), std::string("00"));
    setv(w, X_VSC, 0);
    setv(w, X_HSC, 0);
    poll(w);
    point(w, 35, 9);
    button(w, true);
    poll(w);
    for (const auto &[row, want] : std::vector<std::pair<unsigned, unsigned>>{{29, 60000}, {0, 0}, {29, 60000}})
    {
        point(w, 35, row);
        poll(w);
        CHECK_EQ(value_of(w, X_VSC), want);
    }
    button(w, false);
    poll(w);
    setv(w, X_VSC, 0);
    point(w, 5, 19);
    button(w, true);
    poll(w);
    for (const auto &[col, want] : std::vector<std::pair<unsigned, unsigned>>{{79, 60000}, {0, 0}, {79, 60000}})
    {
        point(w, col, 19);
        poll(w);
        CHECK_EQ(value_of(w, X_HSC), want);
    }
    button(w, false);
    poll(w);
}

TEST(x3_thumbs_out_of_sight_when_suspended_and_let_go_when_closed)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    const unsigned page = r.phys(lab("upage")) | r.phys(lab("upage") + 1u) << 8;
    CHECK_EQ(spr_image(r, 1) & 0x7FFF, page + 0x29); // OOUI's thumb image (UI_OFS_SPR + 1)
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    CHECK_EQ(spr_depth(r, 1), 0);
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(spr_depth(r, 1), 0);
    const unsigned img = spr_image(r, 1) & 0x7FFF;
    CHECK(img < page || img >= page + 64); // not in the card memory ui_close freed
}

TEST(x4_a_thumb_under_a_dialog_or_clipped_is_out_of_sight)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    // The message box up, the thumb moved under it.
    CHECK_EQ(ui(r, 'm', 0).substr(6), std::string("00"));
    poll(r);
    const Place box = rect(r, 0xF0);
    r.poke(rec2(Y_HSP) + 3u, static_cast<uint8_t>(box.row + 1 - 2)); // its row: the box's second
    const unsigned col = box.col + 3;                                 // its column: the box's fourth
    setv(r, Y_HSP, ((col - 9) * 4 + 2) * 100 / 119);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    key(r, K_ESC); // the box gone: the thumb again
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    // The window narrowed and moved right, the slider put to start left of its client area: a thumb at 0 is
    // clipped away, one at the top inside it isn't.
    r.poke(rec2(Y_WIN) + 2u, 30);
    r.poke(rec2(Y_WIN) + 4u, 20);
    r.poke(rec2(Y_HSP) + 2u, 0xE8); // 24 from the client's right: 6 left of its left
    r.poke(rec2(Y_HSP) + 3u, 4);
    setv(r, Y_HSP, 0);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    const Place win = rect(r, Y_WIN), sl = rect(r, Y_HSP);
    REQUIRE(sl.col < win.col + 1 && sl.col + sl.w > win.col + 1);
    CHECK_EQ(spr_depth(r, 1), 0);
    setv(r, Y_HSP, 100);
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
}

TEST(x5_a_clipped_away_part_takes_no_pointer)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    // The label moved to stick out of its group's left (the group's client area 18 wide): it starts 3 left of
    // it, its last columns inside.
    r.poke(rec2(X_OUT) + 2u, static_cast<uint8_t>(256 - 21));
    CHECK_EQ(ui(r, 's', X_OUT).substr(6), std::string("00"));
    poll(r);
    const Place out = rect(r, X_OUT);
    const Place grp = rect(r, X_GRP);
    REQUIRE(out.col < grp.col + 1 && out.col + out.w > grp.col + 1);
    point(r, out.col + out.w - 1, out.row);
    poll(r);
    CHECK_EQ(r.phys(rec2(X_OUT) + 7u) & 0x02, 0x02); // US_HOT: inside, it is there
    point(r, out.col, out.row);
    poll(r);
    CHECK_EQ(r.phys(rec2(X_OUT) + 7u) & 0x02, 0); // outside: not
}

TEST(x6_a_list_that_shrinks_shows_its_rows)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(ui(r, 'f', X_LST).substr(6), std::string("00"));
    key(r, K_END);
    poll(r);
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 10u);
    CHECK_EQ(ui(r, 'q', 12, 0, X_LST).substr(6), std::string("00")); // 12 rows: the top 2, the selection 11
    poll(r);
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 2u);
    CHECK_EQ(value_of(r, X_LST), 11u);
    CHECK_EQ(ui(r, 'q', 4, 0, X_LST).substr(6), std::string("00")); // fewer than it shows: the top 0
    poll(r);
    CHECK_EQ(word_at(r, rec2(X_LST) + 14), 0u);
}

TEST(x7_a_row_whose_right_part_fills_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(cell_char(r, 4, 8), 'T'); // row 0: "Track 00"
    put_text(r, "\t" + std::string(29, 'R'));          // a TAB and 29 characters: as wide as its text
    r.poke(lab("row0") - 2u * 20, std::vector<uint8_t>{0x00, 0x32}); // rows[0] (row0's, less ROWS_N words): TEXTBUF
    CHECK_EQ(ui(r, 'd', X_LST).substr(6), std::string("00"));
    poll(r);
    std::string line;
    for (unsigned c = 4; c < 4 + 29; ++c)
        line += cell_char(r, c, 8);
    CHECK_EQ(line, std::string(29, ' '));
}

TEST(x8_a_click_on_the_desktop_or_a_label_draws_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    const uint16_t clipr = lab("CLIPR");
    const unsigned blk = bank_block(r, "CLIPR"); // (block A's, at $C800-$F7FF only while W3 holds it: U3a)
    unsigned drawn = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == clipr && (r.nb(3) & 0x3F) == blk)
            ++drawn;
    };
    for (const auto &[col, row, what] : std::vector<std::tuple<unsigned, unsigned, const char *>>{
             {79, 29, "the desktop"}, {10, 24, "the label"}, {1, 1, "the desktop again"}})
    {
        point(r, col, row);
        poll(r);
        drawn = 0;
        click(r, col, row);
        expect(poll(r), {}, what);
        if (drawn)
            std::printf("  %s: %u records drawn\n", what, drawn);
        CHECK_EQ(drawn, 0u);
    }
    r.watch = nullptr;
}

TEST(x9_a_held_key_stops_when_the_focus_moves_or_a_modal_box_comes)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    CHECK_EQ(ui(r, 'f', X_LST).substr(6), std::string("00"));
    usb(r).key(K_DOWN, true);
    std::vector<Event> ev;
    for (int i = 0; i < 16; ++i)
    {
        r.run(ms(50));
        for (const Event &e : poll(r))
            ev.push_back(e);
    }
    CHECK(ev.size() >= 6);
    CHECK_EQ(ui(r, 'f', X_LS2).substr(6), std::string("00")); // still held
    ev.clear();
    for (int i = 0; i < 10; ++i)
    {
        r.run(ms(50));
        for (const Event &e : poll(r))
            ev.push_back(e);
    }
    usb(r).key(K_DOWN, false);
    r.run(ms(20));
    poll(r);
    expect(ev, {}, "held on after the focus moved");
    // The message box: Up held on the slider, then the box.
    CcpRig b;
    REQUIRE(b.ok);
    open_u2(b, "uitbar");
    poll(b);
    usb(b).key(K_UP, true);
    for (int i = 0; i < 12; ++i)
    {
        b.run(ms(50));
        poll(b);
    }
    CHECK(value_of(b, Y_VSL) > 60u);
    CHECK_EQ(ui(b, 'm', 0).substr(6), std::string("00"));
    poll(b);
    const unsigned v = value_of(b, Y_VSL);
    ev.clear();
    for (int i = 0; i < 10; ++i)
    {
        b.run(ms(50));
        for (const Event &e : poll(b))
            ev.push_back(e);
    }
    usb(b).key(K_UP, false);
    b.run(ms(20));
    poll(b);
    CHECK_EQ(value_of(b, Y_VSL), v);
    expect(ev, {}, "held on with the box up");
}

TEST(x10_a_window_hidden_mid_drag_ends_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    point(r, 24, 4);
    button(r, true);
    poll(r);
    point(r, 30, 4);
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && (ev[0].flags & UV_LIVE));
    const unsigned v = value_of(r, Y_HSL);
    CHECK_EQ(ui(r, 'h', Y_WIN).substr(6), std::string("00"));
    ev = poll(r); // the drag's last change, as a let-go gives it
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Y_HSL && ev[0].value == v && !(ev[0].flags & UV_LIVE));
    CHECK_EQ(r.phys(lab("upress")), 0xFF);
    CHECK_EQ(ui(r, 's', Y_WIN).substr(6), std::string("00"));
    poll(r);
    point(r, 36, 4); // the button still down: no drag
    expect(poll(r), {}, "moved after");
    button(r, false);
    expect(poll(r), {}, "let go after");
    CHECK_EQ(value_of(r, Y_HSL), v);
}

TEST(x11_a_thumb_s_sprite_must_be_one_of_ooui_s)
{
    for (const uint8_t sprite : {uint8_t{0}, uint8_t{2}, uint8_t{200}, uint8_t{1}})
    {
        CcpRig r;
        REQUIRE(r.ok);
        start_ui(r, "uitbar");
        r.poke(rec2(Y_HSP) + 15u, sprite); // UO_SPRITE 0 (the pointer), UO_SPRITES 2
        const std::string a = ui(r, 'o', BLK_U2);
        if (sprite == 1)
            CHECK_EQ(a.substr(6), std::string("00"));
        else
            CHECK_EQ(a.substr(0, 2) + a.substr(6), std::string("0901")); // UX_SPRITE
    }
}

TEST(x12_values_held_to_their_ranges_at_ui_open)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    r.poke(rec2(Y_VBR) + UW_VALUE, std::vector<uint8_t>{0xF4, 0x01}); // 500 of 100
    r.poke(rec2(Y_VSL) + UW_VALUE, std::vector<uint8_t>{0xC8, 0x00}); // 200 of 100
    CHECK_EQ(ui(r, 'o', BLK_U2).substr(6), std::string("00"));
    CHECK_EQ(value_of(r, Y_VBR), 100u);
    CHECK_EQ(value_of(r, Y_VSL), 100u);
    CcpRig w;
    REQUIRE(w.ok);
    start_ui(w, "uitwid");
    w.poke(rec2(X_LST) + UW_VALUE, std::vector<uint8_t>{40, 0}); // row 40 of 20
    CHECK_EQ(ui(w, 'o', BLK_U2).substr(6), std::string("00"));
    CHECK_EQ(value_of(w, X_LST), 19u);
}

TEST(x13_the_wheel_at_a_slider_s_end_says_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    setv(r, Y_HSL, 100);
    poll(r);
    point(r, 20, 4);
    usb(r).mouse_wheel(1);
    r.run(ms(30));
    expect(poll(r), {}, "the wheel up at the top");
    usb(r).mouse_wheel(-1);
    r.run(ms(30));
    expect(poll(r), {{UE_CHANGE, Y_HSL, 0, 95}}, "the wheel down");
}

// ---------------------------------------------------------------------------
// U2's review, round 2 (Z1, Z2, Z4, Z5; Y4b is y4's segment).

TEST(z1_a_poll_s_walk_costs_little_in_a_large_directory)
{
    // A card of its own: BIG.IMG (oodos-32m) as A:, 200 files SONG0000-SONG0199.SID (names sharing 5 characters).
    std::vector<uint8_t> card = fat16_card({{"BIG.IMG", 65536u * 512}});
    auto word = [&](size_t at) { return size_t{card[at]} | size_t{card[at + 1]} << 8; };
    const size_t root = (word(14) + card[16] * word(22)) * SECTOR;
    const size_t data = root + word(17) * 32;
    std::fill(card.begin() + static_cast<long>(data), card.begin() + static_cast<long>(data + 2048 * 32), 0xE5);
    for (unsigned i = 0; i < 200; ++i)
    {
        uint8_t *e = card.data() + data + i * 32;
        std::memset(e, 0, 32);
        char n[16];
        std::snprintf(n, sizeof n, "SONG%04uSID", i % 10000u);
        std::memcpy(e + 1, n, 11);
        e[15] = 1;
        e[16] = static_cast<uint8_t>(8 + i);
    }
    Rig r(card);
    REQUIRE(r.ok);
    REQUIRE(r.mount(1, "/BIG.IMG") == 0);
    REQUIRE(r.boot());
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    // The instructions a poll runs in the walk's own code (CONSIDER to COUNTED: the compares, not the BDOS).
    const uint16_t from = lab("CONSIDER"), to = lab("COUNTED");
    unsigned long n = 0, most = 0;
    const unsigned blk = bank_block(r, "CONSIDER"); // (block C's: U3a)
    r.watch = [&](uint16_t pc) { n += pc >= from && pc < to && (r.nb(3) & 0x3F) == blk; };
    CHECK_EQ(ui(r, 'k', 4).substr(6), std::string("00"));
    for (int i = 0; i < 100 && r.phys(lab("lst")); ++i)
    {
        n = 0;
        poll(r);
        most = std::max(most, n);
    }
    r.watch = nullptr;
    std::printf("  the most a poll ran in the walk: %lu instructions\n", most);
    CHECK(most < 100000); // (about 54,000 at its last walk, the prune with it: a list's squared /2 is past 600,000)
    CHECK_EQ(r.phys(lab("lst")), 0);
    CHECK_EQ(word_at(r, lab("count")), 200u);
    CHECK_EQ(status(r), std::string("200 files"));
}

TEST(z2_an_overlay_window_hidden_over_nothing_takes_its_thumb)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    r.poke(rec2(Y_DESK) + UW_TYPE, 0);    // no desktop
    r.poke(rec2(Y_WIN) + UW_PARENT, 0xFF); // the window at the top, on the overlay
    r.poke(rec2(Y_WIN) + UW_FLAGS, static_cast<uint8_t>(r.phys(rec2(Y_WIN) + UW_FLAGS) | 0x40));
    CHECK_EQ(ui(r, 'o', BLK_U2).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(ui(r, 'f', Y_VSL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'h', Y_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    CHECK_EQ(ui(r, 's', Y_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
}

TEST(z4_a_drive_and_a_whole_8_3_name_typed)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 2); // UP_NEW on B:
    CHECK_EQ(ui(r, 'f', P_FLD).substr(6), std::string("00"));
    type_name(r, "b:songname.sid");
    poll(r);
    std::string fld;
    for (uint32_t p = lab("fbuf"); r.phys(p); ++p)
        fld += static_cast<char>(r.phys(p));
    CHECK_EQ(fld, std::string("B:SONGNAME.SID"));
    key(r, K_ENTER);
    const std::vector<Event> ev = poll(r);
    CHECK(!ev.empty() && ev[0].code == UE_PICK && ev[0].value == 0);
    CHECK_EQ(result(r, 0), std::string("2 0 SONGNAMESID"));
}

TEST(z5_a_file_deleted_after_it_was_listed_leaves_the_list)
{
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 40);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'k', 1).substr(6), std::string("00"));
    for (int i = 0; i < 3; ++i)
        poll(r);
    CHECK(r.phys(lab("lst")) != 0);
    CHECK(has(listed(r), "FILE02.TXT"));
    std::vector<uint8_t> fcb(36, 0);
    fcb[0] = 13; // M:
    const char *nm = "FILE02  TXT";
    std::memcpy(fcb.data() + 1, nm, 11);
    r.poke(TEXTBUF, fcb);
    CHECK_EQ(ui(r, 'z').substr(0, 2), std::string("00")); // deleted
    settle(r);
    const std::vector<std::string> v = listed(r);
    CHECK(!has(v, "FILE02.TXT"));
    CHECK_EQ(v.size(), 39u);
    CHECK_EQ(status(r), std::string("39 files"));
    CHECK(has(v, "FILE01.BIN") && has(v, "FILE39.BIN"));
}

// U2's review, round 3 (W1).

namespace
{
    /// The picker's name field.
    std::string field_text(Rig &r)
    {
        std::string f;
        for (uint32_t p = lab("fbuf"); r.phys(p) && f.size() < 16; ++p)
            f += static_cast<char>(r.phys(p));
        return f;
    }
    /// M: with 50 files listed part way (block 1), FILE05.BIN selected (the sixth); then the files `gone`
    /// deleted and the listing let run to its end.
    void select_then_delete(CcpRig &r, const std::vector<const char *> &gone)
    {
        m_files(r, 50); // (M: too full for OOUI.BNK as well: it is loaded from B: here, U3a)
        put_b(r, "OOUI    BNK", read_bytes(UTILS + "/OOUI.BNK"));
        start_ui(r, "uitpick", 'N');
        CHECK_EQ(ui(r, 'L', 2).substr(6), std::string("00"));
        CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
        poll(r);
        CHECK_EQ(ui(r, 'k', 1).substr(6), std::string("00"));
        poll(r);
        CHECK_EQ(ui(r, 'f', P_LST).substr(6), std::string("00"));
        for (int i = 0; i < 6; ++i)
            key(r, K_DOWN);
        poll(r);
        CHECK(r.phys(lab("lst")) != 0);
        CHECK_EQ(value_of_dlg(r, 3), 5u);
        CHECK_EQ(field_text(r), std::string("FILE05.BIN"));
        for (const char *g : gone)
        {
            std::vector<uint8_t> fcb(36, 0);
            fcb[0] = 13; // M:
            std::memcpy(fcb.data() + 1, g, 11);
            r.poke(TEXTBUF, fcb);
            CHECK_EQ(ui(r, 'z').substr(0, 2), std::string("00"));
        }
        settle(r);
        CHECK_EQ(listed(r).size(), 50u - gone.size());
    }
} // namespace

TEST(w1_an_entry_before_the_selection_deleted_keeps_the_selection_on_its_file)
{
    CcpRig r;
    REQUIRE(r.ok);
    select_then_delete(r, {"FILE02  TXT"});
    CHECK_EQ(value_of_dlg(r, 3), 4u); // FILE05.BIN, one up
    const std::vector<std::string> v = listed(r);
    CHECK(v.size() > 4 && v[4] == "FILE05.BIN");
    CHECK_EQ(field_text(r), std::string("FILE05.BIN"));
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    const std::vector<Event> ev = poll(r);
    CHECK(!ev.empty() && ev[0].code == UE_PICK && ev[0].value == 0);
    CHECK_EQ(chosen(r, "pickB"), 1u);
    CHECK_EQ(result(r, 0), std::string("13 0 FILE05  BIN"));
}

TEST(w1_the_selected_entry_deleted_leaves_no_selection)
{
    CcpRig r;
    REQUIRE(r.ok);
    select_then_delete(r, {"FILE05  BIN"});
    CHECK(!has(listed(r), "FILE05.BIN"));
    CHECK_EQ(value_of_dlg(r, 3), 0xFFFFu);
    CHECK_EQ(field_text(r), std::string(""));
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    expect(poll(r), {}, "OK with nothing selected"); // nothing chosen: the picker stays
    CHECK_EQ(chosen(r, "pickB"), 0u);
    CHECK_EQ(r.phys(lab("uidle") + 1u) != 0, true); // still up
}

TEST(v1_two_entries_before_the_selection_deleted)
{
    CcpRig r;
    REQUIRE(r.ok);
    select_then_delete(r, {"FILE03  BIN", "FILE04  TXT"});
    CHECK_EQ(value_of_dlg(r, 3), 3u); // FILE05.BIN, two up
    const std::vector<std::string> v = listed(r);
    CHECK(v.size() > 3 && v[3] == "FILE05.BIN");
    CHECK_EQ(field_text(r), std::string("FILE05.BIN"));
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    const std::vector<Event> ev = poll(r);
    CHECK(!ev.empty() && ev[0].code == UE_PICK && ev[0].value == 0);
    CHECK_EQ(result(r, 0), std::string("13 0 FILE05  BIN"));
}

TEST(v1_one_before_the_selection_and_the_selected_one_deleted)
{
    CcpRig r;
    REQUIRE(r.ok);
    select_then_delete(r, {"FILE02  TXT", "FILE05  BIN"});
    CHECK_EQ(value_of_dlg(r, 3), 0xFFFFu);
    CHECK_EQ(field_text(r), std::string(""));
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    expect(poll(r), {}, "OK with nothing selected");
    CHECK_EQ(chosen(r, "pickB"), 0u);
}

int main()
{
    const int failed = run_all();
    if (updating() && failed)
        return failed; // an update run that failed anything else fails (round 2's W7)
    if (updating())
    {
        std::printf("\n*** OOUI_GOLDEN=update: golden/ooui.txt was WRITTEN, nothing was compared. ***\n"
                    "*** Look at the frames (OOUI_DUMP=dir), then run again without it.         ***\n\n");
        return 77; // CTest's SKIP_RETURN_CODE: an update run never reads as a pass (the review's O18)
    }
    return failed;
}

// ---------------------------------------------------------------------------
// U3a (hw/ooui.md, "U3a: OOUI as a banked library"): the items it took over from U2's review, and the banked
// library's own cases.

namespace
{
    enum : uint8_t { BLK_AMP = 5 };       ///< uitest.s's (UITBAR): a screen shaped as OO-Amp's
    constexpr uint8_t A_EQ5 = 16, A_ST0 = 44; ///< one of its 15 sprite sliders, a band; a status line
    /// The cycles from POST (block B: the sprite sliders' thumbs placed after a paint that drew) to its return
    /// in PAINT (PDONE, block A), for a paint after command `c` (A `a`, X `x`, Y `y`).
    unsigned long post_cycles(CcpRig &r, char c, uint8_t a, uint8_t x, uint8_t y)
    {
        const uint16_t post = lab("POST"), done = lab("PDONE");
        const unsigned bb = bank_block(r, "POST"), ba = bank_block(r, "PDONE");
        uint64_t start = 0, took = 0;
        r.watch = [&](uint16_t pc) {
            const unsigned w3 = r.nb(3) & 0x3F;
            if (pc == post && w3 == bb && !start)
                start = r.cpu->cycles();
            else if (pc == done && w3 == ba && start && !took)
                took = r.cpu->cycles() - start;
        };
        CHECK_EQ(ui(r, c, a, x, y).substr(6), std::string("00"));
        poll(r);
        r.watch = nullptr;
        CHECK(took != 0);
        return static_cast<unsigned long>(took);
    }
} // namespace

TEST(z3_the_thumbs_placed_after_a_paint_cost_little)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    CHECK_EQ(ui(r, 'o', BLK_AMP).substr(6), std::string("00"));
    poll(r);
    // A band moved (its value set): its own thumb placed again, the 14 others outside the paint's damage left
    // as they were. Before U3a's fix each of the 15 took a RECT of itself and a FRONT (a RECT of every record):
    // 3,302,244 cycles, 28 frames.
    const unsigned long one = post_cycles(r, 'v', 20, 0, A_EQ5);
    std::printf("  the thumbs placed after a band moved: %lu cycles (a frame is 119,318)\n", one);
    CHECK(one < 100000);
    // A status line's text changed, far from every slider: no thumb placed again.
    r.poke(TEXTBUF, bytes(std::string("Playing", 8)));
    const unsigned long none = post_cycles(r, 'x', 0, 0, A_ST0);
    std::printf("  the thumbs after a status line's text changed: %lu cycles\n", none);
    CHECK(none < 20000);
}

TEST(x4_a_menu_down_over_a_thumb_hides_it)
{
    // The U2 review's X4, left for U3a: the thumb shows only under no menu that is down. UITBAR's scene has a
    // menu bar now (hidden till shown), its menu's drop-down wide and long enough to cover the sprite slider's
    // thumb.
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(ui(r, 's', Y_BAR).substr(6), std::string("00")); // the bar shown (hidden in the scene's frames)
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    key(r, 0x43); // F10: the menu down, over the thumb
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    key(r, K_ESC); // up again: the thumb again
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
}

TEST(the_pointer_s_image_off_ooui_s_memory_after_ui_close)
{
    // Left for U3a by U2's review: ui_close frees OOUI's card memory, so the pointer's sprite (UO_SPRITE, 0
    // here) is pointed at card page 0 (OODOS's, never freed), as the thumbs' are (X3).
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const auto image = [&r] { const uint16_t s = lab("GX_SPR"); return r.phys(s) | (r.phys(s + 1u) & 0x7F) << 8; };
    CHECK_EQ(image(), r.phys(lab("upage")) + (r.phys(lab("upage") + 1u) << 8) + 0x28); // UI_OFS_SPR: the pointer's
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(image(), 0);
}

TEST(w2_a_walk_gets_back_in_step_after_a_file_added_mid_listing)
{
    // Left for U3a by U2's review (W2): once a match was out of place, each new file past the list's end in
    // that walk was looked for through the whole list. Now a walk takes a match past the list's end as new unless
    // it is one of the entries it passed unfound: the place a walk compares at jumps only over a few entries, those it passes unfound kept
    // (8 at most), and an entry it has stamped already is passed. A:, 200 files (as z1's); listed part way,
    // then SONG0003.SID deleted and NEWFILE.SID made in its directory entry, mid-listing: each later poll's walk
    // costs as z1's do (before the fix the most was 823,876 instructions).
    std::vector<uint8_t> card = fat16_card({{"BIG.IMG", 65536u * 512}});
    auto word = [&](size_t at) { return size_t{card[at]} | size_t{card[at + 1]} << 8; };
    const size_t root = (word(14) + card[16] * word(22)) * SECTOR;
    const size_t data = root + word(17) * 32;
    std::fill(card.begin() + static_cast<long>(data), card.begin() + static_cast<long>(data + 2048 * 32), 0xE5);
    for (unsigned i = 0; i < 200; ++i)
    {
        uint8_t *e = card.data() + data + i * 32;
        std::memset(e, 0, 32);
        char n[16];
        std::snprintf(n, sizeof n, "SONG%04uSID", i % 10000u);
        std::memcpy(e + 1, n, 11);
        e[15] = 1;
        e[16] = static_cast<uint8_t>(8 + i);
    }
    Rig r(card);
    REQUIRE(r.ok);
    REQUIRE(r.mount(1, "/BIG.IMG") == 0);
    REQUIRE(r.boot());
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'k', 4).substr(6), std::string("00"));
    for (int i = 0; i < 4; ++i)
        poll(r);
    CHECK(word_at(r, lab("count")) > 20u);
    CHECK(word_at(r, lab("count")) < 150u);
    for (const auto &[c, name] : std::vector<std::pair<char, const char *>>{{'z', "SONG0003SID"}, {'Z', "NEWFILE SID"}})
    {
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = 1; // A:
        std::memcpy(fcb.data() + 1, name, 11);
        r.poke(TEXTBUF, fcb);
        CHECK_EQ(ui(r, c).substr(0, 2), std::string("00"));
    }
    const uint16_t from = lab("CONSIDER"), to = lab("COUNTED");
    const unsigned blk = bank_block(r, "CONSIDER");
    unsigned long n = 0, most = 0;
    r.watch = [&](uint16_t pc) { n += pc >= from && pc < to && (r.nb(3) & 0x3F) == blk; };
    for (int i = 0; i < 100 && r.phys(lab("lst")); ++i)
    {
        n = 0;
        poll(r);
        most = std::max(most, n);
    }
    r.watch = nullptr;
    std::printf("  the most a poll ran in the walk after a file was added mid-listing: %lu instructions\n", most);
    CHECK(most < 100000);
    CHECK_EQ(r.phys(lab("lst")), 0);
    CHECK_EQ(word_at(r, lab("count")), 200u);
    CHECK_EQ(status(r), std::string("200 files"));
}

namespace
{
    enum : uint8_t
    {
        UX_VERSION_ = 1, UX_MEMORY_ = 3, UX_BUSY_ = 6, UX_LOAD_ = 10, UX_GX_ = 11, UX_STATE_ = 12 ///< ooui.inc's
    };
    /// A and the carry of a command, "aacc".
    std::string ac(const std::string &f) { return f.substr(0, 2) + f.substr(6); }
    std::string hex2(unsigned v)
    {
        char b[8];
        std::snprintf(b, sizeof b, "%02X", v & 0xFF);
        return b;
    }
    /// The blocks free (uitest.s's 'B').
    unsigned free_blocks(Rig &r)
    {
        const std::string a = ui(r, 'B').substr(0, 2);
        return std::isxdigit(static_cast<unsigned char>(a[0])) ? static_cast<unsigned>(std::stoul(a, nullptr, 16)) : 999u;
    }
    /// OOUI.BNK, as built.
    std::vector<uint8_t> bnk() { return read_bytes(UTILS + "/OOUI.BNK"); }
} // namespace

TEST(ui_load_s_refusals_leave_nothing_taken)
{
    // Each refusal (uiprog/uiload.s): the error, and the blocks free as before (any it took freed again).
    const auto refused = [](const std::vector<uint8_t> *file, const char *program, char flag, uint8_t err, const char *what)
    {
        CcpRig r;
        REQUIRE(r.ok);
        if (file)
            r.put_m({{BNK_USER, "OOUI    BNK", *file}});
        start_ui(r, program, flag);
        const unsigned before = free_blocks(r);
        const std::string got = ac(ui(r, 'L', 1));
        if (got != hex2(err) + "01")
            std::printf("  %s: got %s\n", what, got.c_str());
        CHECK_EQ(got, hex2(err) + "01");
        CHECK_EQ(free_blocks(r), before);
    };
    refused(nullptr, "uitest", 'N', UX_LOAD_, "no file");
    std::vector<uint8_t> f = bnk();
    f[0] = 'X';
    refused(&f, "uitest", 'N', UX_LOAD_, "not an OOUI.BNK");
    f = bnk();
    f[4] = 0;
    refused(&f, "uitest", 'N', UX_VERSION_, "an older interface");
    f = bnk();
    f[6] = 0x01;
    f[7] = 0x06; // 1,537 bytes of UISTATE: more than the program's 1,536
    refused(&f, "uitest", 'N', UX_VERSION_, "too little UISTATE");
    f = bnk();
    refused(&f, "uitest", 'G', UX_GX_, "no GX_INIT");
    refused(&f, "uitmis", 'N', UX_STATE_, "UISTATE off a page");
    // No block free for the third: the two it took freed again; then, the blocks back, it loads; and again is
    // UX_BUSY.
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{BNK_USER, "OOUI    BNK", bnk()}});
    start_ui(r, "uitest", 'N');
    const unsigned all = free_blocks(r);
    CHECK(all >= 3u);
    CHECK_EQ(ui(r, 'E', 2).substr(0, 2), hex2(all - 2));
    CHECK_EQ(ac(ui(r, 'L', 1)), hex2(UX_MEMORY_) + "01");
    CHECK_EQ(free_blocks(r), 2u);
    CHECK_EQ(ac(ui(r, 'R')).substr(2), std::string("00"));
    CHECK_EQ(free_blocks(r), all);
    CHECK_EQ(ui(r, 'L', 1).substr(6), std::string("00"));
    CHECK_EQ(free_blocks(r), all - 3);
    CHECK_EQ(ac(ui(r, 'L', 1)), hex2(UX_BUSY_) + "01");
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
}

TEST(ui_load_zeroes_uistate_and_the_warm_boot_frees_the_blocks)
{
    // UISTATE full of $FF before ui_load (a program's memory isn't cleared): OOUI opens and draws as ever (D2).
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{BNK_USER, "OOUI    BNK", bnk()}});
    start_ui(r, "uitest", 'N');
    const unsigned all = free_blocks(r);
    r.poke(lab("UISTATE"), std::vector<uint8_t>(1536, 0xFF));
    CHECK_EQ(ui(r, 'L', 1).substr(6), std::string("00"));
    CHECK_EQ(r.phys(lab("UISTATE") + 1535u), 0);
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(frame_hash(r), read_golden().at("window"));
    CHECK_EQ(free_blocks(r), all - 3);
    // The program ended by the warm boot: its blocks are free again.
    ui(r, '!');
    r.run(ms(200));
    start_ui(r, "uitest", 'N');
    CHECK_EQ(free_blocks(r), all);
}

TEST(ui_load_keeps_the_program_s_user_and_dma)
{
    // From B: in the program's user (0), its DMA address, segment and error mode set (UITPICK's 'e'): all as
    // they were after ui_load (D8); and the default name (A:OOUI.BNK, user 0, not on the test card) refused,
    // the program's user 5 put back.
    CcpRig r;
    REQUIRE(r.ok);
    put_b(r, "OOUI    BNK", bnk());
    start_ui(r, "uitpick", 'N');
    CHECK_EQ(ui(r, 'e', 0, 0xFE, 1).substr(6), std::string("00"));
    const std::string mine = own_state(r);
    CHECK_EQ(mine, std::string("user 0 dma 3A00 seg 1234 err FE"));
    CHECK_EQ(ui(r, 'L', 2).substr(6), std::string("00"));
    CHECK_EQ(own_state(r), mine);
    CcpRig s;
    REQUIRE(s.ok);
    start_ui(s, "uitpick", 'N');
    CHECK_EQ(ui(s, 'e', 5, 0xFE, 1).substr(6), std::string("00"));
    const std::string five = own_state(s);
    CHECK_EQ(ac(ui(s, 'L', 0)), hex2(UX_LOAD_) + "01");
    CHECK_EQ(own_state(s), five);
}

namespace
{
    /// What a 'p' (ui_poll till UE_NONE) cost: the cycles of its first ui_poll (from its stub to its return in
    /// c_poll, PRET), the crossings between OOUI's blocks (FARCALLs made with an OOUI block in W3), and what a
    /// crossing adds: from FARCALL's entry to the routine's first instruction (past the dispatcher's JMP), and
    /// from FCRET's entry (the routine's RTS) back to the caller. `crossing` is the least of those timed (U3b: an
    /// interrupt inside one lengthens it by chance), `mean` and `most` their mean and the longest (the U3b
    /// review's G7: a slow path in some crossings shows there).
    struct Costs
    {
        unsigned long poll = 0, crossings = 0, crossing = 0, mean = 0, most = 0;
    };
    Costs poll_costs(Rig &r)
    {
        const uint16_t stub = lab("ui_poll"), back = lab("PRET");
        const uint16_t far = sys_labels().at("FARCALL"), fcret = sys_labels().at("FCRET");
        const uint16_t jumps[] = {static_cast<uint16_t>(lab("AENT") + 23), static_cast<uint16_t>(lab("BENT") + 23),
                                  static_cast<uint16_t>(lab("CENT") + 23)}; // each dispatcher's JMP (xjmp)
        Costs c;
        uint64_t start = 0, xsum = 0, xstart = 0, ostart = 0, xmin = ~uint64_t{0}, xall = 0, xmax = 0;
        unsigned long xdone = 0;
        uint16_t xret = 0, prev = 0;
        uint8_t xsp = 0;
        bool done = false, in = false;
        unsigned long timed = 0;
        r.watch = [&](uint16_t pc) {
            const uint64_t now = r.cpu->cycles();
            const uint16_t was = prev;
            prev = pc;
            if (in && (was == jumps[0] || was == jumps[1] || was == jumps[2]))
            {
                xsum += now - xstart; // in: FARCALL and the dispatcher
                in = false;
            }
            if (xret && pc == fcret && !ostart)
                ostart = now;
            if (pc == stub && !start)
                start = now;
            else if (pc == back && start && !done)
            {
                c.poll = static_cast<unsigned long>(now - start);
                done = true;
            }
            if (xret && pc == xret && r.cpu->regs().s == xsp)
            {
                if (ostart)
                    xsum += now - ostart; // out: FCRET to the caller
                xmin = std::min(xmin, xsum);
                xmax = std::max(xmax, xsum);
                xall += xsum;
                ++xdone;
                xsum = 0;
                xret = 0;
                ostart = 0;
            }
            if (pc == far && (r.nb(3) & 0x3F) != 62)
            {
                if (std::getenv("OOUI_XLOG")) // (which crossing: the stub's return, the selector)
                {
                    const uint8_t sp = r.cpu->regs().s;
                    std::printf("    crossing: from $%04X, xsel %u\n", r.phys(0x100u + static_cast<uint8_t>(sp + 1)) |
                                r.phys(0x100u + static_cast<uint8_t>(sp + 2)) << 8, r.phys(lab("xsel")));
                }
                ++c.crossings;
                if (!xret)
                {
                    const uint8_t sp = r.cpu->regs().s;
                    xret = static_cast<uint16_t>((r.phys(0x100u + static_cast<uint8_t>(sp + 1)) |
                                                  r.phys(0x100u + static_cast<uint8_t>(sp + 2)) << 8) + 4); // past its 3 bytes
                    xsp = static_cast<uint8_t>(sp + 2);
                    xstart = now;
                    in = true;
                    ++timed;
                }
            }
        };
        poll(r);
        r.watch = nullptr;
        c.crossing = timed ? static_cast<unsigned long>(xmin) : 0;
        c.mean = xdone ? static_cast<unsigned long>(xall / xdone) : 0;
        c.most = static_cast<unsigned long>(xmax);
        return c;
    }
} // namespace

TEST(what_a_call_and_a_crossing_cost)
{
    // An event loop's frame with nothing waiting: one ui_poll, through its stub and FARCALL; and a crossing between
    // OOUI's blocks (hw/ooui.md, U3a's cost table). A frame is 119,318 cycles.
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const Costs first = poll_costs(r);
    const Costs idle = poll_costs(r);
    std::printf("  an idle ui_poll: %lu cycles, %lu crossings (the one before: %lu)\n", idle.poll, idle.crossings, first.poll);
    CHECK(idle.poll > 0 && idle.poll < 5000); // (98,858 before U3a skipped placing clean records over no damage;
                                              //  20,728 before U3b's B1)
    CHECK_EQ(idle.crossings, 0u);
    // A U2 widget's key: the list takes Down (a crossing to its handler, and one to draw it).
    CcpRig w;
    REQUIRE(w.ok);
    open_u2(w, "uitwid");
    poll(w);
    CHECK_EQ(ui(w, 'f', 8).substr(6), std::string("00")); // X_LST
    poll(w);
    key(w, K_DOWN);
    const Costs k = poll_costs(w);
    std::printf("  a list's Down: the first ui_poll %lu cycles, %lu crossings; a crossing's overhead %lu cycles at least,"
                " %lu on average, %lu at most\n", k.poll, k.crossings, k.crossing, k.mean, k.most);
    CHECK(k.crossings >= 2u);
    CHECK(k.crossing > 281u && k.crossing < 500u);
    // ui_load from M:, once: its cycles, from its entry to its return.
    CcpRig l;
    REQUIRE(l.ok);
    l.put_m({{BNK_USER, "OOUI    BNK", bnk()}});
    start_ui(l, "uitest", 'N');
    const uint16_t entry = lab("ui_load");
    uint64_t from = 0, took = 0;
    uint16_t ret = 0;
    uint8_t sp = 0;
    l.watch = [&](uint16_t pc) {
        if (pc == entry && !from)
        {
            from = l.cpu->cycles();
            sp = static_cast<uint8_t>(l.cpu->regs().s + 2);
            ret = static_cast<uint16_t>((l.phys(0x100u + static_cast<uint8_t>(sp - 1)) | l.phys(0x100u + sp) << 8) + 1);
        }
        else if (from && !took && pc == ret && l.cpu->regs().s == sp)
            took = l.cpu->cycles() - from;
    };
    CHECK_EQ(ui(l, 'L', 1).substr(6), std::string("00"));
    l.watch = nullptr;
    std::printf("  ui_load from M:: %lu cycles (%.2f s at 7.15909 MHz)\n", static_cast<unsigned long>(took), static_cast<double>(took) / 7159090.0);
    CHECK(took > 0 && took < 7159090u); // under a second
}

TEST(the_picker_s_crossings_a_listing_poll_and_a_selection_move)
{
    // Round 3's F1: a listing poll crosses 3 times (its listing part in C, the list's paint in B, its rows filled
    // in C in one crossing: E3). A selection move, measured, 6: the list's key, the picker's filter of its
    // UE_CHANGE (DFILT), the picker's idle part, the list's paint, its rows, and the name field's paint (it shows
    // the selection); F1 counted 4, without the idle part and the field. S2 (the position cache, hw/ooui.md): each
    // paint's two layers' walks are block C's (2 more), and each record they draw is drawn by block A's ONE (one
    // more each: the listing's one, 6 in all; the move's two, 10 in all, B5's boxes drawing no record that only
    // their bounds met).
    CcpRig r;
    REQUIRE(r.ok);
    m_files(r, 40);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'k', 1).substr(6), std::string("00")); // M:'s TXT and BIN files
    poll(r);
    const Costs listing = poll_costs(r);
    std::printf("  a listing poll: %lu crossings\n", listing.crossings);
    CHECK_EQ(listing.crossings, 6u); // (3 before S2)
    settle(r);
    CHECK_EQ(ui(r, 'f', P_LST).substr(6), std::string("00"));
    poll(r);
    key(r, K_DOWN);
    const Costs move = poll_costs(r);
    std::printf("  a selection move: %lu crossings\n", move.crossings);
    CHECK_EQ(move.crossings, 10u); // (6 before S2, 11 with S2 before B5: a record drawn only for the one box)
}

TEST(ui_load_s_default_name_from_another_user)
{
    // The demo card has OOUI.BNK on A: in user 0, not SYS (make_card.sh): the program in user 3 loads it by its
    // default name, ui_load opening it in user 0 (D8: the switch, not the SYS attribute, finds it), and is in user
    // 3 after.
    Rig r(sd_image("demo.sparse"));
    REQUIRE(r.ok);
    REQUIRE(r.mount(1, "/OODOS/A.IMG") == 0);
    REQUIRE(r.mount(2, "/OODOS/B.IMG") == 0);
    REQUIRE(r.boot());
    start_ui(r, "uitpick", 'N');
    CHECK_EQ(ui(r, 'e', 3, 0xFE, 0).substr(6), std::string("00"));
    const std::string mine = own_state(r);
    CHECK_EQ(mine.substr(0, 7), std::string("user 3 "));
    CHECK_EQ(ui(r, 'L', 0).substr(6), std::string("00"));
    CHECK_EQ(own_state(r), mine);
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
}

TEST(c1_a_thumb_shows_again_when_what_covered_only_it_goes)
{
    // The U3a review's C1: a cover over the thumb alone hid it, and POST kept the slider's first cell as its
    // last, so when the cover went its damage missed that cell and the thumb stayed out of sight. Now the
    // thumb's own cell is kept, shown or not. A small overlay window over the thumb (about column 31, row 6),
    // not over the slider's start (9, 6), then hidden; and the second menu's drop-down, the same. (Only the window
    // fails without the fix: the menu bar's own redraw widens its layer's damage box over the slider's start.)
    {
        CcpRig r;
        REQUIRE(r.ok);
        start_ui(r, "uitbar");
        r.poke(rec2(Y_LBL) + UW_TYPE, 2); // Y_LBL a non-modal overlay window at the top
        r.poke(rec2(Y_LBL) + UW_PARENT, 0xFF);
        r.poke(rec2(Y_LBL) + 2u, 27);
        r.poke(rec2(Y_LBL) + 3u, 5);
        r.poke(rec2(Y_LBL) + 4u, 9);
        r.poke(rec2(Y_LBL) + 5u, 3);
        r.poke(rec2(Y_LBL) + UW_FLAGS, 0x40);
        CHECK_EQ(ui(r, 'o', BLK_U2).substr(6), std::string("00"));
        poll(r);
        const Place w = rect(r, Y_LBL), sl = rect(r, Y_HSP);
        REQUIRE(w.col > sl.col);
        CHECK_EQ(spr_depth(r, 1), 0);
        CHECK_EQ(ui(r, 'h', Y_LBL).substr(6), std::string("00"));
        poll(r);
        CHECK_EQ(spr_depth(r, 1), 0x0C);
    }
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(ui(r, 's', Y_BAR).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    key(r, 0x17, L_ALT); // Alt+T: the second menu down, over the thumb only
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    key(r, K_ESC);
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
}

namespace
{
    /// OOUI.BNK's header fields (mkbank.py's format 1): block b's image and fixups.
    unsigned bnk_word(const std::vector<uint8_t> &f, size_t at) { return f[at] | f[at + 1] << 8u; }
    size_t bnk_image(const std::vector<uint8_t> &f, unsigned b) { return bnk_word(f, 10 + 8 * b) * 128u; }
    size_t bnk_fixups(const std::vector<uint8_t> &f, unsigned b) { return bnk_word(f, 14 + 8 * b) * 128u; }
    /// Block b's first fixup of kind `kind` (0x80 any word): its entry's place in the file and the image's offset.
    std::pair<size_t, unsigned> bnk_fixup(const std::vector<uint8_t> &f, unsigned b, uint8_t kind)
    {
        size_t e = bnk_fixups(f, b);
        unsigned off = 0;
        for (unsigned n = bnk_word(f, 16 + 8 * b); n; --n, e += 2)
        {
            off += f[e];
            if (f[e + 1] == kind || (kind == 0x80 && f[e + 1] >= 0x80 && f[e + 1] != 0xFF))
                return {e, off};
        }
        return {0, 0};
    }
    /// ui_load of `file` from B: (UITPICK, user 0, its DMA state set): refused with `err`, the blocks free as
    /// before and the program's user, DMA address, segment and error mode as they were.
    void refused_from_b(const std::vector<uint8_t> &file, uint8_t err, const char *what)
    {
        CcpRig r;
        REQUIRE(r.ok);
        put_b(r, "OOUI    BNK", file);
        start_ui(r, "uitpick", 'N');
        CHECK_EQ(ui(r, 'e', 0, 0xFE, 1).substr(6), std::string("00"));
        const std::string mine = own_state(r);
        const unsigned before = free_blocks(r);
        const std::string got = ac(ui(r, 'L', 2));
        if (got != hex2(err) + "01")
            std::printf("  %s: got %s\n", what, got.c_str());
        CHECK_EQ(got, hex2(err) + "01");
        CHECK_EQ(free_blocks(r), before);
        CHECK_EQ(own_state(r), mine);
    }
} // namespace

TEST(c2_a_hostile_ooui_bnk_writes_nothing_outside_its_blocks)
{
    // The U3a review's C2: each header and fixup ui_load reads is checked before it is used, and a file that
    // fails one is refused (UX_LOAD), what it took freed and the user put back.
    const std::vector<uint8_t> good = bnk();
    std::vector<uint8_t> f = good;
    f[12] = 0x00; // block A's image $4000 long: past its code window
    f[13] = 0x40;
    refused_from_b(f, UX_LOAD_, "an image too long");
    f = good;
    for (size_t e = bnk_fixups(f, 0), i = 0; i < 60; ++i, e += 2) // block A's fixups: skips, past its image
    {
        f[e] = 0xFF;
        f[e + 1] = 0xFF;
    }
    refused_from_b(f, UX_LOAD_, "a skip run past the image");
    f = good;
    f[bnk_fixups(f, 0) + 1] = 0x05; // a kind mkbank.py doesn't make
    refused_from_b(f, UX_LOAD_, "a bad kind");
    f = good;
    {
        const auto [e, off] = bnk_fixup(f, 0, 0x02);
        REQUIRE(e != 0);
        f[bnk_image(f, 0) + off] = 4; // a block number's byte not 1-3
    }
    refused_from_b(f, UX_LOAD_, "a bad block byte");
    f = good;
    {
        const auto [e, off] = bnk_fixup(f, 0, 0x80);
        REQUIRE(e != 0);
        f[e + 1] = static_cast<uint8_t>(0x80 + f[8]); // an import past the header's count
    }
    refused_from_b(f, UX_LOAD_, "an import index too high");
    f = good;
    f[34] = 3; // ui_open's block: none
    refused_from_b(f, UX_LOAD_, "a bad entry block");
    f = good;
    f[35] = 0x00; // ui_open at $F800: common memory, not a code window
    f[36] = 0xF8;
    refused_from_b(f, UX_LOAD_, "a bad entry address");
}

TEST(c4_a_short_ooui_bnk_is_refused_with_its_blocks_freed)
{
    // The U3a review's C4: a file that ends inside block A's image, or inside its fixups, after the blocks are
    // taken: refused (UX_LOAD), the blocks freed, the user and DMA put back.
    const std::vector<uint8_t> good = bnk();
    refused_from_b(std::vector<uint8_t>(good.begin(), good.begin() + static_cast<long>(bnk_image(good, 0) + 4096)),
                   UX_LOAD_, "short in an image");
    refused_from_b(std::vector<uint8_t>(good.begin(), good.begin() + static_cast<long>(bnk_fixups(good, 0) + 128)),
                   UX_LOAD_, "short in the fixups");
}

// ---------------------------------------------------------------------------
// U3b: the gaps OO-Amp's port found (hw/ooui.md, "U3b"), each a case that fails without its fix.

namespace
{
    enum : uint8_t { Z_DESK, Z_WIN, Z_OWN, Z_STD, Z_VSC, Z_HSC, Z_LBL, Z_BTN, Z_COVER, Z_BAR, Z_CNV = 14 };
    enum : uint8_t { BLK_U3B = 6, BLK_OWN = 7 }; ///< uitest.s's (UITBAR)
    constexpr uint8_t P_ALL = 0xFD;              ///< the picker's Add all (U3b)
    constexpr unsigned TH_COLOURS_ = 34, TC_LABEL_ = 5, TC_BUTTON_ = 6, TC_BUTTONFOCUS_ = 7;
    uint16_t rec3(uint8_t h) { return static_cast<uint16_t>(lab("u3b") + h * 16u); }
    unsigned spr_x(Rig &r, unsigned n) { return word_at(r, lab("GX_SPR") + n * 8u + 2); }
    unsigned spr_y(Rig &r, unsigned n) { return word_at(r, lab("GX_SPR") + n * 8u + 4); }
    unsigned spr_size(Rig &r, unsigned n) { return r.phys(lab("GX_SPR") + n * 8u + 7); }
    /// Sprite n's image word set as the program would (GX_SPRIMG), before ui_open.
    void spr_set_image(Rig &r, unsigned n, unsigned page)
    {
        r.poke(lab("GX_SPR") + n * 8u, static_cast<uint8_t>(page));
        r.poke(lab("GX_SPR") + n * 8u + 1, static_cast<uint8_t>(page >> 8));
    }
    /// A cell's attribute on layer 0 (the base, UO_BASE 0).
    unsigned cell_attr(Rig &r, unsigned col, unsigned row)
    {
        const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8;
        return vid(r).card_peek(map0 * 256u + row * 160u + col * 2u + 1);
    }
    unsigned house(Rig &r, unsigned role) { return r.phys(lab("UI_HOUSE") + TH_COLOURS_ + role); }
    unsigned upage(Rig &r) { return r.phys(lab("upage")) | r.phys(lab("upage") + 1u) << 8; }
    void open_u3b(CcpRig &r, uint8_t block = BLK_U3B)
    {
        start_ui(r, "uitbar");
        spr_set_image(r, 1, 0x0ABC); // the program's own thumbs' images (UF_OWNTHUMB)
        spr_set_image(r, 3, 0x0ABD);
        CHECK_EQ(ui(r, 'o', block).substr(6), std::string("00"));
        poll(r);
    }
    /// A dialog record's text (place k), a UICOM string read from block A.
    std::string dlg_text(Rig &r, unsigned k)
    {
        uint32_t p = word_at(r, lab("UDLG") + k * 16u + UW_TEXT);
        if (p >= 0xC800 && p < 0xF800)
            p = r.phys(lab("UI_BLOCKS")) * 0x4000u + p - 0xC000;
        std::string t;
        for (uint8_t c; (c = r.phys(p)) != 0 && t.size() < 40; ++p)
            t += static_cast<char>(c);
        return t;
    }
} // namespace

TEST(u3b_a_slider_s_own_thumb)
{
    // UF_OWNTHUMB: the thumb's image is the program's (OOUI writes none), its size and group the record's
    // (UW_TEXT, $11: 16 x 8, group 1), and it is centred by its size: x = 9 x 4 + 50 x 119 / 100 - 8 (half of
    // 16), y = 4 x 8 + 4 - 4. Beside it, OOUI's own 8 x 8, as before.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(word_at(r, rec3(Z_OWN) + UW_TEXT), 0x11u); // (the layout macro's thumb)
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    CHECK_EQ(spr_size(r, 1), 0x11u);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(spr_x(r, 1), 9u * 4 + 50u * 119 / 100 - 8);
    CHECK_EQ(spr_y(r, 1), 4u * 8);
    CHECK_EQ(spr_image(r, 2) & 0x7FFF, upage(r) + 0x29);
    CHECK_EQ(spr_size(r, 2) & 0xF0, 0u);
    CHECK_EQ(spr_x(r, 2), 9u * 4 + 50u * 119 / 100 - 4);
    CHECK_EQ(spr_y(r, 2), 6u * 8);
    setv(r, Z_OWN, 100);
    poll(r);
    CHECK_EQ(spr_x(r, 1), 9u * 4 + 119 - 8);
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    CHECK_EQ(ui(r, 'h', Z_WIN).substr(6), std::string("00")); // hidden: out of sight, the image left alone
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    // ui_close (the U3b review's G1): OOUI's own thumbs' images go off the card memory it frees, to page 0; the
    // program's own (sprites 1 and 3) are left as it set them, in its own memory.
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    CHECK_EQ(spr_image(r, 3), 0x0ABDu);
    CHECK_EQ(spr_image(r, 2), 0u);
    CHECK_EQ(spr_image(r, 4), 0u);
}

TEST(u3b_a_scroll_bar_s_sprite_thumb)
{
    // UF_SPRITE on a scroll bar: its thumb a sprite (UW_TEXT's high byte), the bar in units. The vertical one
    // (column 43, rows 4-15, its track rows 5-14: 80 units) has the program's 8 x 32 ($81: UF_OWNTHUMB), so 48
    // units of room: the thumb at (row 5) x 8 + 48 x value / 100, x = 43 x 4 + 2 - 4.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(word_at(r, rec3(Z_VSC) + UW_TEXT), 0x0381u);
    CHECK_EQ(spr_image(r, 3), 0x0ABDu);
    CHECK_EQ(spr_size(r, 3), 0x81u);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_x(r, 3), 43u * 4 + 2 - 4);
    CHECK_EQ(spr_y(r, 3), 5u * 8);
    setv(r, Z_VSC, 50);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 5u * 8 + 24);
    // A press below the thumb pages down, one above it up (the page, 10).
    click(r, 43, 14);
    expect(poll(r), {{UE_CHANGE, Z_VSC, 0, 60}}, "below the thumb: a page down");
    click(r, 43, 5);
    expect(poll(r), {{UE_CHANGE, Z_VSC, 0, 50}}, "above it: a page up");
    // The thumb (units 24-55 of the track) held 16 units in, and dragged to 52: its start 36, 36 x 100 / 48.
    usb(r).mouse_to(43 * 4 + 2, 5 * 8 + 40);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {}, "the thumb held");
    usb(r).mouse_to(43 * 4 + 2, 5 * 8 + 52);
    r.run(ms(30));
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Z_VSC && ev[0].value == 75 && (ev[0].flags & UV_LIVE));
    button(r, false);
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].value == 75 && !(ev[0].flags & UV_LIVE));
    CHECK_EQ(spr_y(r, 3), 5u * 8 + 36);
    // The horizontal one, OOUI's 8 x 8 (column 9, its track columns 10-37: 104 units of room).
    CHECK_EQ(spr_image(r, 4) & 0x7FFF, upage(r) + 0x29);
    CHECK_EQ(spr_x(r, 4), 10u * 4);
    CHECK_EQ(spr_y(r, 4), 10u * 8);
    setv(r, Z_HSC, 50);
    poll(r);
    CHECK_EQ(spr_x(r, 4), 10u * 4 + 52);
    CHECK_EQ(ui(r, 'h', Z_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0);
    CHECK_EQ(spr_depth(r, 4), 0);
}

TEST(u3b_a_tall_thumb_covered_at_one_end_is_hidden)
{
    // The U3b review's G2: the 8 x 32 thumb of the vertical scroll bar (column 43, rows 5-8 at 0) shows only when
    // its first and last cells along the track both show, not its middle's (row 7) alone. A window over its last
    // row (8) hides it, and when the window goes, its damage there (rows 8-10) brings it back, the Z3 filter
    // looking at all its cells; a menu's drop-down over its first row (5), the same.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_y(r, 3), 5u * 8);
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    const Place cover = rect(r, Z_COVER);
    CHECK(cover.col == 42 && cover.row == 8);
    CHECK_EQ(spr_depth(r, 3), 0); // its last row covered: hidden (its middle's row shows)
    CHECK_EQ(spr_depth(r, 4), 0x0C);
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C); // back: the window's damage met its last row
    CHECK_EQ(ui(r, 's', Z_BAR).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    key(r, 0x43); // F10: the drop-down, its bottom row 5, over the thumb's first row only
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0);
    CHECK_EQ(spr_depth(r, 4), 0x0C); // (the horizontal bar's, row 10, below it)
    key(r, K_ESC);
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
}

TEST(u3b_ui_settext_on_a_thumb_is_checked)
{
    // The U3b review's G3: a slider's or a scroll bar's UW_TEXT is its thumb's size byte (UF_OWNTHUMB) and a scroll
    // bar's its sprite, so ui_settext checks the word as ui_open checks the table: refused with UX_SPRITE and the
    // record as it was (the pointer's sprite, one not OOUI's, an own thumb's size 0); taken, the thumb moves to
    // the new sprite and the old goes out of sight.
    constexpr unsigned UX_SPRITE_ = 9;
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const unsigned x = spr_x(r, 3), y = spr_y(r, 3);
    for (const unsigned w : {0x0081u, 0x0681u, 0x0300u})
    {
        CHECK_EQ(ac(ui(r, 'T', static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8), Z_VSC)), hex2(UX_SPRITE_) + "01");
        CHECK_EQ(word_at(r, rec3(Z_VSC) + UW_TEXT), 0x0381u);
    }
    CHECK_EQ(ac(ui(r, 'T', 0, 0, Z_OWN)), hex2(UX_SPRITE_) + "01"); // the slider's own thumb: size 0
    CHECK_EQ(word_at(r, rec3(Z_OWN) + UW_TEXT), 0x11u);
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_size(r, 3), 0x81u);
    CHECK_EQ(spr_y(r, 3), y);
    CHECK_EQ(ui(r, 'T', 0x12, 0, Z_OWN).substr(6), std::string("00")); // its size and group: 16 x 8, group 2
    poll(r);
    CHECK_EQ(spr_size(r, 1), 0x12u);
    // The horizontal bar's thumb to sprite 5, the old (4) out of sight; then the vertical one's to 4, which the
    // horizontal one's last place mustn't keep from being placed (POST's filter: the new sprite has none).
    const unsigned hx = spr_x(r, 4), hy = spr_y(r, 4);
    CHECK_EQ(ui(r, 'T', 0, 5, Z_HSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(word_at(r, rec3(Z_HSC) + UW_TEXT), 0x0500u);
    CHECK_EQ(spr_depth(r, 5), 0x0C);
    CHECK(spr_x(r, 5) == hx && spr_y(r, 5) == hy);
    CHECK_EQ(spr_depth(r, 4), 0);
    CHECK_EQ(ui(r, 'T', 0x81, 4, Z_VSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(word_at(r, rec3(Z_VSC) + UW_TEXT), 0x0481u);
    CHECK_EQ(spr_depth(r, 4), 0x0C);
    CHECK_EQ(spr_size(r, 4), 0x81u);
    CHECK(spr_x(r, 4) == x && spr_y(r, 4) == y);
    CHECK_EQ(spr_depth(r, 3), 0);
}

TEST(u3b_a_canvas_s_cells_from_a_routine)
{
    // The owner's Q1 (U3b): a canvas with UF_ROUTINE draws the cells its row routine gives, a character and an
    // attribute each (OO-Amp's scope and VU, per-cell colours on OOUI's layer). UITBAR's crows: row r's column c
    // is $21 + c + r in ((c & 15) << 4) | ((c + 5r) & 15); the canvas at columns 9-48, rows 20-23. A row given as 0
    // is transparent; without UF_ROUTINE the canvas is transparent, as before.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const auto cells_ok = [&r](unsigned rows) {
        unsigned bad = 0;
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned c = 0; c < 40; ++c)
            {
                const bool drawn = y < rows;
                const unsigned ch = drawn ? 0x21 + c + y : 0, at = drawn ? ((c & 15) << 4) | ((c + y * 5) & 15) : 0;
                bad += static_cast<uint8_t>(cell_char(r, 9 + c, 20 + y)) != ch || cell_attr(r, 9 + c, 20 + y) != at;
            }
        return bad;
    };
    CHECK_EQ(cells_ok(4), 0u);
    CHECK_EQ(cell_char(r, 8, 20), ' '); // beside it, the window's inside, as before
    // The program changes its cells each frame and calls ui_dirty: what one such frame's ui_poll costs, and the
    // canvas's own drawing in it (CNROWS: 4 rows of 40 cells, 4 calls of the routine).
    const uint16_t rows = lab("CNROWS"), done = lab("CNDONE");
    const unsigned bb = bank_block(r, "CNROWS");
    uint64_t from = 0, took = 0;
    r.watch = [&](uint16_t pc) {
        if ((r.nb(3) & 0x3F) != bb)
            return;
        if (pc == rows && !from)
            from = r.cpu->cycles();
        else if (pc == done && from && !took)
            took = r.cpu->cycles() - from;
    };
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    poll(r);
    r.watch = nullptr;
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    const Costs frame = poll_costs(r);
    CHECK(took > 0 && took < 30000);
    CHECK_EQ(cells_ok(4), 0u);
    r.poke(lab("cnone"), 3); // row 3 given as 0: transparent
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(cells_ok(3), 0u);
    CHECK_EQ(ui(r, 'F', 0, 0x40, Z_CNV).substr(6), std::string("00")); // UF_ROUTINE cleared: transparent
    poll(r);
    CHECK_EQ(cells_ok(0), 0u);
    // The same frame for the canvas without UF_ROUTINE (its cells transparent): the rest of that ui_poll's cost,
    // the damage cleared and the window's inside drawn again under it.
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    const Costs plain = poll_costs(r);
    std::printf("  a 40 x 4 routine canvas drawn again: its ui_poll %lu cycles, its cells %lu of them; the same ui_poll"
                " for a plain canvas %lu (a frame is 119,318)\n", frame.poll, static_cast<unsigned long>(took), plain.poll);
    CHECK(frame.poll > plain.poll && frame.poll - plain.poll < 30000);
}

// The U3b review's round 2 (H1-H5): each case fails without its fix.

TEST(u3b_r2_ui_flags_on_a_thumb_is_checked)
{
    // H1: ui_flags on a slider or a scroll bar checks the new flags as ui_open checks the table (SPRCHK): UF_SPRITE
    // onto the pointer's sprite, or UF_OWNTHUMB with a size byte of 0, refused with UX_SPRITE and the flags as they
    // were; and UF_SPRITE cleared puts the thumb's sprite out of sight, which it comes back from when set again.
    constexpr unsigned UX_SPRITE_ = 9;
    constexpr uint8_t UF_SPRITE_ = 0x10, UF_OWNTHUMB_ = 0x20;
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const auto flags = [&r](uint8_t h) { return r.phys(rec3(h) + UW_FLAGS) & ~0x04u; }; // (its dirty bit aside)
    CHECK_EQ(spr_depth(r, 4), 0x0C);
    const unsigned hsc = flags(Z_HSC);
    CHECK_EQ(ui(r, 'F', 0, UF_SPRITE_, Z_HSC).substr(6), std::string("00")); // off: its cells' thumb, the sprite hidden
    poll(r);
    CHECK_EQ(spr_depth(r, 4), 0);
    CHECK_EQ(flags(Z_HSC), hsc & ~0x10u);
    CHECK_EQ(ui(r, 'T', 0, 0, Z_HSC).substr(6), std::string("00")); // its sprite the pointer's: no sprite thumb, taken
    CHECK_EQ(ac(ui(r, 'F', UF_SPRITE_, 0, Z_HSC)), hex2(UX_SPRITE_) + "01");
    CHECK_EQ(flags(Z_HSC), hsc & ~0x10u);
    CHECK_EQ(ui(r, 'T', 0, 4, Z_HSC).substr(6), std::string("00")); // sprite 4 again: taken, and shown
    CHECK_EQ(ui(r, 'F', UF_SPRITE_, 0, Z_HSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(flags(Z_HSC), hsc);
    CHECK_EQ(spr_depth(r, 4), 0x0C);
    CHECK_EQ(ac(ui(r, 'F', UF_OWNTHUMB_, 0, Z_HSC)), hex2(UX_SPRITE_) + "01"); // its size byte 0
    CHECK_EQ(flags(Z_HSC), hsc);
    const unsigned std = flags(Z_STD);
    CHECK_EQ(ac(ui(r, 'F', UF_OWNTHUMB_, 0, Z_STD)), hex2(UX_SPRITE_) + "01"); // a slider's: the same
    CHECK_EQ(flags(Z_STD), std);
    poll(r);
    CHECK_EQ(spr_depth(r, 2), 0x0C);
    CHECK_EQ(spr_image(r, 2) & 0x7FFF, upage(r) + 0x29); // still OOUI's own image
}

TEST(u3b_r2_ui_open_checks_a_scroll_bar_s_and_an_own_thumb_s_sprite)
{
    // H5: ui_open refuses (UX_SPRITE) a scroll bar whose sprite thumb is the pointer's sprite, and a thumb of the
    // program's own (UF_OWNTHUMB) whose size byte is 0, a slider's or a scroll bar's.
    struct Bad
    {
        uint8_t h;
        unsigned at, value;
        const char *what;
    };
    for (const Bad &b : {Bad{Z_VSC, UW_TEXT + 1u, 0, "a scroll bar on the pointer's sprite"},
                         Bad{Z_HSC, UW_TEXT + 1u, 0, "an OOUI-thumbed scroll bar on the pointer's sprite"},
                         Bad{Z_OWN, UW_TEXT, 0, "a slider's own thumb of size 0"},
                         Bad{Z_VSC, UW_TEXT, 0, "a scroll bar's own thumb of size 0"}})
    {
        CcpRig r;
        REQUIRE(r.ok);
        start_ui(r, "uitbar");
        r.poke(rec3(b.h) + b.at, static_cast<uint8_t>(b.value));
        const std::string a = ui(r, 'o', BLK_U3B);
        if (a.substr(0, 2) + a.substr(6) != "0901")
            std::printf("  %s: %s\n", b.what, a.c_str());
        CHECK_EQ(a.substr(0, 2) + a.substr(6), std::string("0901")); // UX_SPRITE
    }
}

TEST(u3b_r2_a_routine_of_0_draws_nothing)
{
    // H2: UF_ROUTINE with a routine of 0 calls nothing: a canvas's rows transparent, a list's rows empty (its
    // text blank in each row's colours, the selection still shown), as a row given as 0 is; no refusal.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(cell_char(r, 9, 20), 0x21);
    CHECK_EQ(ui(r, 'T', 0, 0, Z_CNV).substr(6), std::string("00"));
    poll(r);
    unsigned drawn = 0;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned c = 0; c < 40; ++c)
            drawn += cell_char(r, 9 + c, 20 + y) != 0 || cell_attr(r, 9 + c, 20 + y) != 0;
    CHECK_EQ(drawn, 0u);
    expect(poll(r), {}, "still running");
    CcpRig l;
    REQUIRE(l.ok);
    open_u2(l, "uitwid");
    poll(l);
    CHECK_EQ(cell_char(l, 39, 8), 'I'); // "Item 00"
    const unsigned list_attr = cell_attr(l, 39, 9);
    CHECK_EQ(ui(l, 'T', 0, 0, X_LS2).substr(6), std::string("00"));
    poll(l);
    unsigned text = 0;
    for (unsigned y = 0; y < 10; ++y)
        for (unsigned c = 0; c < 14; ++c)
            text += cell_char(l, 39 + c, 8 + y) != ' ';
    CHECK_EQ(text, 0u);
    CHECK_EQ(cell_attr(l, 39, 9), house(l, 15)); // TC_LIST: a row with no style
    std::printf("  a list's row (style %02X before) with no routine: %02X\n", list_attr, cell_attr(l, 39, 9));
    expect(poll(l), {}, "still running");
    // The selection's row still in its colour (the U3b review's round 3, J3): row 2 chosen, TC_LISTCUR without
    // the focus, TC_LISTSEL with it; its neighbours TC_LIST.
    CHECK(ui(l, 'g').substr(0, 2) != hex2(X_LS2));
    setv(l, X_LS2, 2);
    poll(l);
    CHECK_EQ(cell_attr(l, 39, 10), house(l, 17)); // TC_LISTCUR
    CHECK_EQ(cell_attr(l, 39, 11), house(l, 15));
    CHECK_EQ(ui(l, 'f', X_LS2).substr(6), std::string("00"));
    poll(l);
    CHECK_EQ(cell_attr(l, 39, 10), house(l, 16)); // TC_LISTSEL
    CHECK_EQ(cell_attr(l, 39, 9), house(l, 15));
    CHECK(house(l, 16) != house(l, 15) && house(l, 17) != house(l, 15));
}

TEST(u3b_r2_a_canvas_s_routine_is_called_only_for_the_rows_redrawn)
{
    // H3: the canvas's routine (and its GX_VMAP) is called only for the rows inside what the paint redraws: a
    // window shown and hidden over two of its rows (21-22, with the shadow) calls it for those rows alone. What
    // the canvas's drawing costs in that paint, against the whole canvas's.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    r.poke(rec3(Z_COVER) + 2, 20); // the small window (G2's) moved over the canvas: columns 20-23, row 21
    r.poke(rec3(Z_COVER) + 3, 21);
    r.poke(rec3(Z_COVER) + 5, 1);
    spr_set_image(r, 1, 0x0ABC);
    spr_set_image(r, 3, 0x0ABD);
    CHECK_EQ(ui(r, 'o', BLK_U3B).substr(6), std::string("00"));
    poll(r);
    const uint16_t calls = lab("ccalls"), rows = lab("CNROWS"), done = lab("CNDONE");
    const unsigned bb = bank_block(r, "CNROWS");
    const auto counts = [&]() {
        std::vector<unsigned> c;
        for (unsigned i = 0; i < 4; ++i)
            c.push_back(r.phys(calls + i));
        r.poke(calls, std::vector<uint8_t>{0, 0, 0, 0});
        return c;
    };
    uint64_t from = 0, took = 0;
    const auto time = [&]() {
        from = took = 0;
        r.watch = [&](uint16_t pc) {
            if ((r.nb(3) & 0x3F) != bb)
                return;
            if (pc == rows)
                from = r.cpu->cycles();
            else if (pc == done && from)
                took += r.cpu->cycles() - from, from = 0;
        };
    };
    CHECK(counts() == (std::vector<unsigned>{1, 1, 1, 1}));
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    time();
    poll(r);
    r.watch = nullptr;
    const unsigned long shown = took;
    const std::vector<unsigned> s = counts();
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    time();
    poll(r);
    r.watch = nullptr;
    const unsigned long hidden = took;
    const std::vector<unsigned> h = counts();
    std::printf("  rows called, the window shown: %u %u %u %u; hidden: %u %u %u %u\n", s[0], s[1], s[2], s[3], h[0], h[1],
                h[2], h[3]);
    CHECK(s == (std::vector<unsigned>{0, 1, 1, 0}));
    CHECK(h == (std::vector<unsigned>{0, 1, 1, 0}));
    unsigned bad = 0;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned c = 0; c < 40; ++c)
            bad += static_cast<uint8_t>(cell_char(r, 9 + c, 20 + y)) != 0x21 + c + y;
    CHECK_EQ(bad, 0u);
    time();
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    poll(r);
    r.watch = nullptr;
    CHECK(counts() == (std::vector<unsigned>{1, 1, 1, 1}));
    std::printf("  the canvas's drawing: the strip's paint %lu cycles shown, %lu hidden; the whole canvas %lu\n", shown,
                hidden, static_cast<unsigned long>(took));
}

TEST(u3b_r2_a_routine_returning_with_d_set)
{
    // H4: OOUI clears the decimal flag after a canvas's and a list's routine, so one that returns with it set
    // leaves the drawing as it was.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    r.poke(lab("cdec"), 0x80);
    CHECK_EQ(ui(r, 'd', Z_CNV).substr(6), std::string("00"));
    poll(r);
    unsigned bad = 0;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned c = 0; c < 40; ++c)
            bad += static_cast<uint8_t>(cell_char(r, 9 + c, 20 + y)) != 0x21 + c + y ||
                   cell_attr(r, 9 + c, 20 + y) != (((c & 15) << 4) | ((c + y * 5) & 15));
    CHECK_EQ(bad, 0u);
    CcpRig l;
    REQUIRE(l.ok);
    open_u2(l, "uitwid");
    poll(l);
    const uint64_t before = frame_hash(l);
    l.poke(lab("cdec"), 0x80);
    CHECK_EQ(ui(l, 'd', X_LS2).substr(6), std::string("00"));
    poll(l);
    CHECK_EQ(frame_hash(l), before);
}

// The U3b review's round 3 (J1-J3), and the slider's and scroll bar's margin (U3b, OO-Amp's targets).

TEST(u3b_r3_a_column_routine_returning_with_d_set)
{
    // J1: OOUI clears the decimal flag after the picker's column routine too, so one that returns with it set
    // leaves the picker's rows as they were.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 0); // block A: a column ("c" and the row's number)
    poll(r);
    const uint64_t before = frame_hash(r);
    CHECK_EQ(ui(r, 'd', P_LST).substr(6), std::string("00")); // drawn again as it was
    poll(r);
    CHECK_EQ(frame_hash(r), before);
    r.poke(lab("cdec"), 0x80);
    CHECK_EQ(ui(r, 'd', P_LST).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(frame_hash(r), before);
    expect(poll(r), {}, "still running");
}

TEST(u3b_r3_ui_flags_keeps_an_own_thumb_while_its_sprite_shows)
{
    // J2: ui_flags refuses (UX_SPRITE, carry set, the flags as they were, nothing dirtied) a change to UF_OWNTHUMB
    // while UF_SPRITE is on before and after: the sprite's image would change hands while it shows. UF_SPRITE
    // turned off with it, or on with it, is taken.
    constexpr unsigned UX_SPRITE_ = 9;
    constexpr uint8_t UF_SPRITE_ = 0x10, UF_OWNTHUMB_ = 0x20;
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const auto flags = [&r](uint8_t h) { return static_cast<unsigned>(r.phys(rec3(h) + UW_FLAGS)); };
    struct Bad
    {
        uint8_t h, set, clear;
        const char *what;
    };
    for (const Bad &b : {Bad{Z_OWN, 0, UF_OWNTHUMB_, "a slider's own thumb made OOUI's"},
                         Bad{Z_STD, UF_OWNTHUMB_, 0, "a slider's OOUI thumb made the program's"},
                         Bad{Z_VSC, 0, UF_OWNTHUMB_, "a scroll bar's own thumb made OOUI's"}})
    {
        const unsigned before = flags(b.h);
        CHECK_EQ(before & 0x04u, 0u); // (clean after the paint)
        const std::string a = ac(ui(r, 'F', b.set, b.clear, b.h));
        if (a != hex2(UX_SPRITE_) + "01")
            std::printf("  %s: %s\n", b.what, a.c_str());
        CHECK_EQ(a, hex2(UX_SPRITE_) + "01");
        CHECK_EQ(flags(b.h), before); // the flags as they were, not dirty
    }
    poll(r);
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(spr_image(r, 2) & 0x7FFF, upage(r) + 0x29);
    CHECK_EQ(spr_depth(r, 2), 0x0C);
    // Both off together, then both on: taken; the thumb hidden, then the program's again.
    CHECK_EQ(ui(r, 'F', 0, UF_SPRITE_ | UF_OWNTHUMB_, Z_OWN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0);
    CHECK_EQ(ui(r, 'F', UF_SPRITE_ | UF_OWNTHUMB_, 0, Z_OWN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 1), 0x0C);
    CHECK_EQ(spr_image(r, 1), 0x0ABCu);
    // The scroll bar's: off together, then on as OOUI's own (UF_SPRITE alone): taken, OOUI's image on it.
    CHECK_EQ(ui(r, 'F', 0, UF_SPRITE_ | UF_OWNTHUMB_, Z_VSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0);
    CHECK_EQ(ui(r, 'F', UF_SPRITE_, 0, Z_VSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_image(r, 3) & 0x7FFF, upage(r) + 0x29);
    // UF_OWNTHUMB changed while UF_SPRITE is off: taken (no sprite shows), and still checked when it goes on.
    CHECK_EQ(ui(r, 'F', 0, UF_SPRITE_, Z_STD).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'F', UF_OWNTHUMB_, 0, Z_STD).substr(6), std::string("00"));
    CHECK_EQ(ac(ui(r, 'F', UF_SPRITE_, 0, Z_STD)), hex2(UX_SPRITE_) + "01"); // its size byte 0 (SPRCHK)
}

namespace
{
    constexpr uint8_t UF_MARGIN_ = 0x40;
    /// The first UE_CHANGE in `ev` for `h`: its value, or -1 none.
    int changed(const std::vector<Event> &ev, uint8_t h)
    {
        for (const Event &e : ev)
            if (e.code == UE_CHANGE && e.handle == h)
                return static_cast<int>(e.value);
        return -1;
    }
    /// A press and release at a cell, after the slider's or scroll bar's value is set to `v`: what changed.
    int press_at(Rig &r, uint8_t h, unsigned v, unsigned col, unsigned row)
    {
        setv(r, h, v);
        poll(r);
        click(r, col, row);
        return changed(poll(r), h);
    }
} // namespace

TEST(u3b_a_slider_s_margin_takes_a_press)
{
    // UF_MARGIN (U3b, OO-Amp's targets): a slider's margin byte (UW_TEXT's high byte): across in its low four
    // bits, along in its high four. Z_OWN (row 4, columns 9-38) given $21: rows 3-5, columns 7-40. A press in
    // the margin is the press on the track: the same value as at the track's cell in that column, the ends'
    // values beyond its ends. Without the flag, the margin's cells are the window's: nothing.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const int on = press_at(r, Z_OWN, 0, 20, 4);
    CHECK(on > 0 && on < 100);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), -1); // no flag: nothing
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 3), -1);
    CHECK_EQ(press_at(r, Z_OWN, 50, 7, 4), -1);
    r.poke(rec3(Z_OWN) + UW_TEXT + 1, 0x21);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), -1); // the margin byte alone: nothing
    CHECK_EQ(ui(r, 'F', UF_MARGIN_, 0, Z_OWN).substr(6), std::string("00"));
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), on); // across: below, above
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 3), on);
    CHECK_EQ(press_at(r, Z_OWN, 50, 7, 4), 0); // along: two cells before its start, after its end
    CHECK_EQ(press_at(r, Z_OWN, 50, 40, 5), 100);
    CHECK_EQ(press_at(r, Z_OWN, 50, 6, 4), -1); // past the margin
    CHECK_EQ(press_at(r, Z_OWN, 50, 41, 4), -1);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 2), -1);
    // A drag begun in the margin moves the thumb as one begun on the track.
    setv(r, Z_OWN, 0);
    poll(r);
    point(r, 20, 5);
    button(r, true);
    CHECK_EQ(changed(poll(r), Z_OWN), on);
    point(r, 30, 3);
    const int moved = changed(poll(r), Z_OWN);
    CHECK(moved > on);
    button(r, false);
    poll(r);
    CHECK_EQ(word_at(r, rec3(Z_OWN) + UW_VALUE), static_cast<unsigned>(moved));
    CHECK_EQ(spr_x(r, 1), 9u * 4 + moved * 119u / 100 - 8);
    // Hidden, its margin takes nothing.
    CHECK_EQ(ui(r, 'h', Z_OWN).substr(6), std::string("00"));
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), -1);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 4), -1);
}

TEST(u3b_a_margin_takes_no_press_from_another_widget)
{
    // The margin takes a press only where what is there is one of its parents (the window, the desktop) or
    // nothing: never from a widget drawn in front of it (FRONT's order: a window over it), nor from one behind
    // it (Z_STD, row 6, after Z_OWN, so in front: its margin over Z_OWN's row 4 leaves Z_OWN its own presses);
    // nor under a modal window that isn't its own.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    r.poke(rec3(Z_OWN) + UW_TEXT + 1, 0x01);
    CHECK_EQ(ui(r, 'F', UF_MARGIN_, 0, Z_OWN).substr(6), std::string("00"));
    const int on = press_at(r, Z_OWN, 0, 20, 4);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), on);
    // A window in front, over the margin's row 5 (columns 18-21, rows 5-7): its cells are its own.
    r.poke(rec3(Z_COVER) + 2, 18);
    r.poke(rec3(Z_COVER) + 3, 5);
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), -1);
    CHECK_EQ(press_at(r, Z_OWN, 0, 24, 5), press_at(r, Z_OWN, 0, 24, 4)); // beside it, still the margin
    // Modal, the window over it: the margin outside it takes nothing.
    CHECK_EQ(ui(r, 'F', 0x20, 0, Z_COVER).substr(6), std::string("00")); // UF_MODAL
    poll(r);
    CHECK_EQ(press_at(r, Z_OWN, 0, 24, 5), -1);
    CHECK_EQ(ui(r, 'F', 0, 0x20, Z_COVER).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    // Z_STD (in front of Z_OWN) with a margin of two rows across: rows 4-8. Z_OWN's own row 4 is still Z_OWN's;
    // row 5, in both margins, is Z_STD's (the frontmost); row 7, Z_STD's.
    r.poke(rec3(Z_STD) + UW_TEXT + 1, 0x02);
    CHECK_EQ(ui(r, 'F', UF_MARGIN_, 0, Z_STD).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 4), on);
    setv(r, Z_STD, 0);
    CHECK_EQ(press_at(r, Z_OWN, 0, 20, 5), -1);
    CHECK_EQ(word_at(r, rec3(Z_STD) + UW_VALUE), static_cast<unsigned>(on));
    setv(r, Z_STD, 0);
    CHECK_EQ(press_at(r, Z_STD, 0, 20, 7), on);
}

TEST(u3b_a_scroll_bar_s_margin_takes_a_press)
{
    // A scroll bar's margin byte is UW_B's high byte (its page then 0-255), across only: Z_VSC (column 43, rows
    // 4-15, page 10) given $11: columns 42-44; its along bits do nothing (its ends are its arrows). A press
    // beside the track pages as on it; beside the thumb drags it.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(press_at(r, Z_VSC, 50, 44, 14), -1); // no flag: nothing
    r.poke(rec3(Z_VSC) + 15, 0x11); // UW_B + 1
    CHECK_EQ(ui(r, 'F', UF_MARGIN_, 0, Z_VSC).substr(6), std::string("00"));
    CHECK_EQ(press_at(r, Z_VSC, 50, 43, 14), 60); // on it: a page down (10, UW_B's low byte)
    CHECK_EQ(press_at(r, Z_VSC, 50, 44, 14), 60);
    CHECK_EQ(press_at(r, Z_VSC, 50, 42, 5), 40);
    CHECK_EQ(press_at(r, Z_VSC, 50, 45, 14), -1);
    CHECK_EQ(press_at(r, Z_VSC, 50, 43, 3), -1); // along: none
    CHECK_EQ(press_at(r, Z_VSC, 50, 44, 16), -1);
    // The thumb (units 24-55 at 50: rows 8-11) held from beside it, and dragged.
    setv(r, Z_VSC, 50);
    poll(r);
    usb(r).mouse_to(44 * 4 + 2, 5 * 8 + 40);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {}, "the thumb held from its margin");
    usb(r).mouse_to(44 * 4 + 2, 5 * 8 + 52);
    r.run(ms(30));
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Z_VSC && ev[0].value == 75);
    button(r, false);
    poll(r);
}

TEST(u3b_r4_a_margin_at_the_edges_under_a_hidden_parent_and_a_drop_down)
{
    // The U3b review's round 4, K2: the margin's edge cases. Z_OWN given $11 (a cell across, a cell along) and
    // UF_MARGIN.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    r.poke(rec3(Z_OWN) + UW_TEXT + 1, 0x11);
    CHECK_EQ(ui(r, 'F', UF_MARGIN_, 0, Z_OWN).substr(6), std::string("00"));
    // At a parent's client area's edge: the slider at the window's client (0, 0), columns 3-32 of row 2. The
    // margin's cells on the window's frame (column 2, its left side; row 1, its title) take nothing; those
    // inside do.
    r.poke(rec3(Z_OWN) + 2, 0);
    r.poke(rec3(Z_OWN) + 3, 0);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    const int on = press_at(r, Z_OWN, 50, 10, 2);
    CHECK(on > 0 && on < 100);
    CHECK_EQ(press_at(r, Z_OWN, 50, 10, 3), on); // below, inside the client area
    CHECK_EQ(press_at(r, Z_OWN, 50, 10, 1), -1); // above: the title row
    CHECK_EQ(press_at(r, Z_OWN, 50, 2, 2), -1);  // before its start: the frame
    CHECK_EQ(press_at(r, Z_OWN, 50, 33, 3), 100); // after its end, inside
    // At the screen's left and top edge: the slider on the desktop at (0, 0), columns 0-29 of row 0, the window
    // hidden (else its cells, from row 1, are its own). The margin before its start and above it is off the
    // screen; nothing comes round from the far edges.
    r.poke(rec3(Z_OWN) + 1, Z_DESK);
    CHECK_EQ(ui(r, 'h', Z_WIN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    const int top = press_at(r, Z_OWN, 50, 5, 0);
    CHECK(top > 0 && top < 100);
    CHECK_EQ(press_at(r, Z_OWN, 50, 5, 1), top); // below
    CHECK_EQ(press_at(r, Z_OWN, 50, 0, 1), press_at(r, Z_OWN, 50, 0, 0)); // below its first cell
    CHECK_EQ(press_at(r, Z_OWN, 50, 30, 1), 100); // after its end, below
    CHECK_EQ(press_at(r, Z_OWN, 50, 31, 0), -1);
    CHECK_EQ(press_at(r, Z_OWN, 50, 5, 29), -1);  // the bottom row, the right column: no wrap
    CHECK_EQ(press_at(r, Z_OWN, 50, 79, 0), -1);
    CHECK_EQ(press_at(r, Z_OWN, 50, 79, 29), -1);
    // Back in the window (columns 9-38, row 4), the window hidden: no margin, nor the track.
    r.poke(rec3(Z_OWN) + 1, Z_WIN);
    r.poke(rec3(Z_OWN) + 2, 6);
    r.poke(rec3(Z_OWN) + 3, 2);
    CHECK_EQ(ui(r, 's', Z_WIN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    const int mid = press_at(r, Z_OWN, 50, 20, 4);
    CHECK_EQ(press_at(r, Z_OWN, 50, 20, 5), mid);
    CHECK_EQ(ui(r, 'h', Z_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(press_at(r, Z_OWN, 50, 20, 5), -1);
    CHECK_EQ(press_at(r, Z_OWN, 50, 20, 4), -1);
    CHECK_EQ(ui(r, 's', Z_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(press_at(r, Z_OWN, 50, 20, 3), mid);
    // A drop-down down over the margin (the menu bar's, rows 1-5): a press there is the menu's, never the
    // slider's.
    CHECK_EQ(ui(r, 's', Z_BAR).substr(6), std::string("00"));
    poll(r);
    setv(r, Z_OWN, 50);
    poll(r);
    key(r, 0x43); // F10: the drop-down
    poll(r);
    click(r, 20, 3); // its second item's row, in the margin above the track
    const std::vector<Event> ev = poll(r);
    CHECK_EQ(changed(ev, Z_OWN), -1);
    CHECK(!ev.empty() && ev[0].code == UE_MENU);
    CHECK_EQ(word_at(r, rec3(Z_OWN) + UW_VALUE), 50u);
}

TEST(u3b_a_label_in_a_colour_of_its_own)
{
    // A label's UW_VALUE low byte, not 0: its own attribute ($4E); 0: the theme's TC_LABEL.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(cell_attr(r, 9, 12), 0x4Eu);
    CHECK_EQ(cell_attr(r, 9 + 13, 12), 0x4Eu);
    setv(r, Z_LBL, 0);
    poll(r);
    CHECK_EQ(cell_attr(r, 9, 12), house(r, TC_LABEL_));
}

TEST(u3b_tf_hot_lights_the_button_under_the_pointer)
{
    // US_HOT's colour (U3b's decision): with the theme's TF_HOT, a button under the pointer is drawn in
    // TC_BUTTONFOCUS's colours; without it, as before.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("02")); // the focus on the first slider, not the button
    point(r, 12, 14);
    poll(r);
    CHECK_EQ(cell_attr(r, 12, 14), house(r, TC_BUTTON_)); // the default theme: no TF_HOT
    point(r, 50, 20);
    poll(r);
    CHECK_EQ(ui(r, 't', 4).substr(6), std::string("00")); // the default with TF_HOT
    poll(r);
    CHECK_EQ(cell_attr(r, 12, 14), house(r, TC_BUTTON_));
    point(r, 12, 14);
    poll(r);
    CHECK_EQ(cell_attr(r, 12, 14), house(r, TC_BUTTONFOCUS_));
    point(r, 50, 20);
    poll(r);
    CHECK_EQ(cell_attr(r, 12, 14), house(r, TC_BUTTON_));
}

TEST(u3b_uo_ownmouse_leaves_the_pointer_to_the_program)
{
    // UO_OWNMOUSE (U3b checked it for OO-Amp's own pointer): OOUI sets no pointer (its sprite, sprite 0, as the
    // program left it; no DM_POINTER), and still takes the mouse's clicks from the event area.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    spr_set_image(r, 0, 0x0DEF);
    CHECK_EQ(ui(r, 'o', BLK_OWN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_image(r, 0), 0x0DEFu);
    CHECK_EQ(spr_depth(r, 0), 0);
    CHECK_EQ(r.phys(lab("uptr")), 0);
    click(r, 12, 14);
    expect(poll(r), {{UE_FOCUS, Z_BTN, 0, 0}, {UE_CLICK, Z_BTN, 0, 0}}, "a click on the button: its focus, its click");
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00")); // ui_suspend and ui_resume: still none
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_image(r, 0), 0x0DEFu);
    CHECK_EQ(r.phys(lab("uptr")), 0);
}

TEST(u3b_the_picker_s_add_and_add_all_keep_it_up)
{
    // OO-Amp's Load Files (U3b): UP_KEEP makes OK "&Add", its answer 0 leaving the picker up with its marks;
    // UP_ALL shows "Add a&ll", answer 3, every file listed given (as many as the result holds). Without
    // UP_ALL the button is hidden.
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<MFile> m;
    for (unsigned i = 0; i < 20; ++i)
    {
        char n[12];
        std::snprintf(n, sizeof n, "DATA%02u  BIN", i);
        m.push_back({0, n, std::vector<uint8_t>(10, 1)});
    }
    r.put_m(m);
    start_ui(r, "uitpick");
    CHECK_EQ(ui(r, 'o', BLK_DESK).substr(6), std::string("00"));
    poll(r);
    pick(r, 0);
    CHECK(rect(r, P_ALL).hidden); // block A: no UP_ALL
    CHECK_EQ(dlg_text(r, 5), std::string("&OK"));
    key(r, K_ESC);
    poll(r);
    pick(r, 5);
    CHECK_EQ(listed(r).size(), 20u);
    CHECK_EQ(dlg_text(r, 5), std::string("&Add"));
    CHECK(!rect(r, P_ALL).hidden);
    CHECK_EQ(dlg_text(r, 9), std::string("Add a&ll"));
    key(r, K_DOWN);
    key(r, K_SPACE);
    key(r, K_DOWN);
    key(r, K_DOWN);
    key(r, K_SPACE);
    poll(r);
    const Place ok = rect(r, P_OK);
    click(r, ok.col + 3, ok.row);
    expect(poll(r), {{UE_PICK, P_WIN, 0, 0}}, "Add: up still");
    CHECK_EQ(chosen(r, "pickF"), 2u);
    CHECK(!rect(r, P_WIN).hidden);
    click(r, ok.col + 3, ok.row); // the marks kept: the same two again
    expect(poll(r), {{UE_PICK, P_WIN, 0, 0}}, "Add again");
    CHECK_EQ(chosen(r, "pickF"), 2u);
    key(r, 0x0F); // L: "Add a&ll"
    expect(poll(r), {{UE_PICK, P_WIN, 0, 3}}, "Add all: up still");
    CHECK_EQ(chosen(r, "pickF"), 16u); // the result's room
    const std::vector<std::string> l = listed(r);
    for (unsigned i = 0; i < 16; ++i)
    {
        std::string want = l[i];
        const size_t dot = want.find('.');
        want = "13 0 " + (want.substr(0, dot) + "        ").substr(0, 8) + want.substr(dot + 1);
        CHECK_EQ(result(r, i), want);
    }
    key(r, K_ESC);
    expect(poll(r), {{UE_PICK, P_WIN, 0, 1}, {UE_MODAL, P_WIN, 0, 0}}, "Escape: gone");
}

// ---------------------------------------------------------------------------
// U3b, the repaint trace's B1-B4 (hw/ooui.md, "U3b: the repaint"): what a poll costs among 90 records, as many as
// OO-Amp's screen has.

namespace
{
    enum : uint8_t { BLK_BIG = 8 };                  ///< uitest.s's (UITBAR): block 5's table with 31 records more
    constexpr uint8_t A_TIME = 2, A_EQ5B = 16, A_X4B = 89; ///< its first label; a band (A_EQ5); its last label
    /// A 'p' (ui_poll till UE_NONE): the cycles of all its ui_polls (each from its stub to its return, PRET), how
    /// many, the events, and how often OOUI's REC, RNEXT (a walk's step), FRONT and PASS were entered (U3b, B1-B4),
    /// and (S1) ONE (a record drawn) and PLACE1 (a sprite thumb placed).
    struct Polls
    {
        unsigned long cycles = 0, calls = 0, recs = 0, steps = 0, fronts = 0, passes = 0, ones = 0, places = 0;
        std::vector<Event> ev;
    };
    Polls polls_cost(CcpRig &r)
    {
        const uint16_t stub = lab("ui_poll"), back = lab("PRET"), rec = lab("REC"), front = lab("FRONT"),
                       pass = lab("PASS"), step = lab("RNEXT"), one = lab("ONE"), place = lab("PLACE1");
        const unsigned blocks[3] = {bank_block(r, "PAINT"), bank_block(r, "POST"), bank_block(r, "DLGON")};
        const unsigned passb = bank_block(r, "PASS"); // (block C's from S2)
        Polls p;
        uint64_t start = 0;
        r.watch = [&](uint16_t pc) {
            const unsigned w3 = r.nb(3) & 0x3F;
            const bool ooui = w3 == blocks[0] || w3 == blocks[1] || w3 == blocks[2];
            if (pc == stub && !start)
                start = r.cpu->cycles();
            else if (pc == back && start)
            {
                p.cycles += static_cast<unsigned long>(r.cpu->cycles() - start);
                ++p.calls;
                start = 0;
            }
            else if (ooui && pc == rec)
                ++p.recs;
            else if (ooui && pc == step)
                ++p.steps;
            else if (ooui && pc == front)
                ++p.fronts;
            else if (w3 == passb && pc == pass)
                ++p.passes;
            else if (w3 == blocks[0] && pc == one)
                ++p.ones;
            else if (w3 == blocks[1] && pc == place)
                ++p.places;
        };
        p.ev = poll(r);
        r.watch = nullptr;
        return p;
    }
    void say(const char *what, const Polls &p)
    {
        std::printf("  among 90 records, %s: %lu cycles, %lu ui_poll, %lu REC, %lu RNEXT, %lu FRONT, %lu PASS, %lu ONE, %lu PLACE1\n",
                    what, p.cycles, p.calls, p.recs, p.steps, p.fronts, p.passes, p.ones, p.places);
    }
} // namespace

TEST(u3b_r5_a_poll_s_cost_among_ninety_records)
{
    // OO-Amp's screen has about 90 records; block 8 has 90 (its sliders, bars, canvases, buttons and labels), on
    // one layer as OO-Amp's are, no record on the overlay. A frame is 119,318 cycles. Before B1-B4 (48c0386):
    // idle 56,679; one label dirty 453,345; a drag's step 628,883; a button pressed 1,026,758, released 455,661;
    // 30 widgets changed 2,273,689.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    CHECK_EQ(ui(r, 'o', BLK_BIG).substr(6), std::string("00"));
    poll(r);
    poll(r);
    // An idle poll, nothing dirty and no damage: no paint (B1), the modal window and the overlay kept (B3), so
    // no record looked at.
    const Polls idle = polls_cost(r);
    say("an idle poll", idle);
    CHECK(idle.calls == 1 && idle.cycles > 0 && idle.cycles < 5000);
    CHECK_EQ(idle.passes, 0u);
    CHECK(idle.recs < 5u);
    // One widget dirty, the table's first label: drawn again, the records after it looked at once each, in one
    // pass (B2: no record can be on the overlay), the walks stepping (B4).
    CHECK_EQ(ui(r, 'd', A_TIME).substr(6), std::string("00"));
    const Polls one = polls_cost(r);
    say("one label dirty", one);
    CHECK_EQ(one.passes, 1u);
    CHECK(one.cycles < 200000); // (242,153 with PASS's RECT walking each record's parents: round 5)
    // The table's last label dirty: the records before it stepped over, not found one by one (B4).
    CHECK_EQ(ui(r, 'd', A_X4B).substr(6), std::string("00"));
    const Polls last = polls_cost(r);
    say("the last label dirty", last);
    CHECK(last.recs < 150u);                     // (192 with PASS's or the clearing's steps by REC: B4)
    CHECK(last.recs + last.steps < 330u);        // (374 with OVERS found again each paint: B3)
    // A label hidden, then shown again: the modal window and the overlay found again once (B3), the overlay's
    // walk stepping (B4).
    CHECK_EQ(ui(r, 'h', A_X4B).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 's', A_X4B).substr(6), std::string("00"));
    const Polls shown = polls_cost(r);
    say("the last label shown again", shown);
    CHECK(shown.recs < 150u);                    // (194 with the overlay's walk by REC: B4)
    // A drag: band 5 (column 24, rows 11-18) held on its track and moved a row: no hit test while it is held
    // (B4), the drag still tracked.
    point(r, 24, 15);
    button(r, true);
    const Polls down = polls_cost(r);
    CHECK(changed(down.ev, A_EQ5B) >= 0);
    point(r, 24, 14);
    const Polls drag = polls_cost(r);
    say("a drag's step", drag);
    CHECK_EQ(drag.fronts, 0u);
    CHECK(changed(drag.ev, A_EQ5B) > changed(down.ev, A_EQ5B));
    CHECK(drag.cycles < 290000); // (324,284 without PASS's sibling cache: round 5)
    button(r, false);
    poll(r);
    // A press on a button (A_PLAY, columns 10-15, row 22: drawn pressed), and its release (a click).
    point(r, 11, 22);
    button(r, true);
    const Polls press = polls_cost(r);
    say("a button pressed", press);
    CHECK(press.cycles < 720000); // (747,598 without PASS's sibling cache: round 5)
    button(r, false);
    const Polls release = polls_cost(r);
    say("released", release);
    CHECK(release.cycles < 200000); // (222,613 without PASS's sibling cache: round 5)
    // Many widgets changed at once, as when a track starts: the 14 sliders' values and the 15 status labels.
    for (unsigned h : {6u, 7u, 8u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 33u})
        setv(r, static_cast<uint8_t>(h), 20);
    for (unsigned h = 44; h < 59; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls many = polls_cost(r);
    say("30 widgets changed", many);
    CHECK(many.cycles < 1880000); // (1,921,661 without PASS's sibling cache: round 5)
}

TEST(u3b_r5_the_modal_window_and_the_overlay_kept_are_found_again)
{
    // B3: FINDMODAL's umodal and OVERS's answer are kept between polls, and found again after each change that can
    // move them: ui_show, ui_hide, ui_flags, ui_dirty on a window (its flags the program's own writing),
    // ui_redraw, ui_resume, ui_open (after ui_close), and the dialogs (u2_the_message_box, the picker's cases). A
    // modal window's coming and going is seen (UE_MODAL, umodal); the overlay's window, by a click (FRONT's
    // overlay pass runs only while OVERS says one can be there).
    CcpRig r;
    REQUIRE(r.ok);
    open_main(r);
    const uint16_t umodal = lab("umodal");
    const uint16_t side = static_cast<uint16_t>(lab("main") + W_SIDE * 16u + UW_FLAGS);
    CHECK_EQ(r.phys(umodal), 0xFF);
    // ui_flags
    CHECK_EQ(ui(r, 'F', UF_MODAL, 0, W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 1}}, "ui_flags: modal");
    CHECK_EQ(ui(r, 'F', 0, UF_MODAL, W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 0}}, "ui_flags: not");
    // ui_hide and ui_show of a modal window
    CHECK_EQ(ui(r, 'F', UF_MODAL, 0, W_SIDE).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'h', W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 0}}, "ui_hide");
    CHECK_EQ(ui(r, 's', W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 1}}, "ui_show");
    // ui_dirty on the window after the program wrote its flags itself
    r.poke(side, static_cast<uint8_t>(r.phys(side) & ~UF_MODAL));
    poll(r);
    CHECK_EQ(r.phys(umodal), W_SIDE); // (kept: nothing said it changed)
    CHECK_EQ(ui(r, 'd', W_SIDE).substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 0}}, "ui_dirty on the window");
    // ui_redraw
    r.poke(side, static_cast<uint8_t>(r.phys(side) | UF_MODAL));
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 1}}, "ui_redraw");
    // ui_suspend, the flags written meanwhile, ui_resume
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    r.poke(side, static_cast<uint8_t>(r.phys(side) & ~UF_MODAL));
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    expect(poll(r), {{UE_MODAL, W_SIDE, 0, 0}}, "ui_resume");
    // ui_close, the flags written, ui_open again
    r.poke(side, static_cast<uint8_t>(r.phys(side) | UF_MODAL));
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(r.phys(umodal), W_SIDE);
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    r.poke(side, static_cast<uint8_t>(r.phys(side) & ~UF_MODAL));
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(r.phys(umodal), 0xFF);
    // The overlay: its window shown, a click on its button is its; hidden, the click isn't.
    click(r, 25, 23);
    poll(r);
    CHECK_EQ(ui(r, 's', W_OVER).substr(6), std::string("00"));
    poll(r);
    click(r, 25, 23);
    std::vector<Event> ev = poll(r);
    CHECK(std::any_of(ev.begin(), ev.end(), [](const Event &e) { return e.code == UE_CLICK && e.handle == B_HIDE; }));
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    poll(r);
    click(r, 25, 23);
    ev = poll(r);
    CHECK(std::none_of(ev.begin(), ev.end(), [](const Event &e) { return e.handle == B_HIDE; }));
}

// ---------------------------------------------------------------------------
// U3b round 5: the two gaps OO-Amp's test pass found (hw/ooui.md, "U3b: round 5"): a slider that refuses the
// wheel, and a scroll bar with no arrows.

namespace
{
    constexpr uint8_t UF_NOWHEEL_ = 0x80, UF_NOARROWS_ = 0x80;
    /// A cell's character on layer 0, as a code.
    unsigned ch_at(Rig &r, unsigned col, unsigned row) { return static_cast<uint8_t>(cell_char(r, col, row)); }
    void wheel(Rig &r, int steps)
    {
        usb(r).mouse_wheel(steps);
        r.run(ms(30));
    }
} // namespace

TEST(u3b_r5_a_slider_that_refuses_the_wheel)
{
    // UF_NOWHEEL ($80, a slider's): the wheel over it is the program's, UE_WHEEL with the slider as X and the
    // steps in UV_VALUE, as over a widget that takes no wheel; its value unchanged. Its keys and a press still
    // move it. Without the flag the wheel steps it, as before (u2_sliders_and_bars).
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitbar");
    poll(r);
    CHECK_EQ(ui(r, 'F', UF_NOWHEEL_, 0, Y_HSL).substr(6), std::string("00"));
    poll(r);
    point(r, 20, 4);
    wheel(r, 1);
    expect(poll(r), {{UE_WHEEL, Y_HSL, 0, 1}}, "the wheel up over it: the program's");
    wheel(r, -2);
    expect(poll(r), {{UE_WHEEL, Y_HSL, 0, 0xFFFE}}, "the wheel down over it");
    CHECK_EQ(value_of(r, Y_HSL), 25u);
    CHECK_EQ(ui(r, 'F', UF_NOWHEEL_, 0, Y_VSL).substr(6), std::string("00")); // the vertical one, the same
    poll(r);
    point(r, 6, 5);
    wheel(r, 1);
    expect(poll(r), {{UE_WHEEL, Y_VSL, 0, 1}}, "the wheel over the vertical one");
    CHECK_EQ(value_of(r, Y_VSL), 50u);
    key(r, K_UP); // its keys still step it (the focus is the vertical slider's)
    expect(poll(r), {{UE_CHANGE, Y_VSL, 0, 55}}, "Up still");
    point(r, 24, 4); // and a press still puts it there: 62 x 100 / 119 = 52
    button(r, true);
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Y_HSL && ev[0].value == 52);
    button(r, false);
    poll(r);
    // The flag cleared: the wheel steps it again.
    CHECK_EQ(ui(r, 'F', 0, UF_NOWHEEL_, Y_HSL).substr(6), std::string("00"));
    poll(r);
    point(r, 20, 4);
    wheel(r, 1);
    expect(poll(r), {{UE_CHANGE, Y_HSL, 0, 57}}, "the flag cleared: a step up");
}

TEST(u3b_r5_a_scroll_bar_without_arrows)
{
    // UF_NOARROWS ($80, a scroll bar's): no arrows; the track is the whole bar, a press on either end cell is the
    // track's (a page, or the thumb held), and the thumb travels the whole length. Cells first: UITWID's X_VSC
    // (column 35, rows 8-17, last 50, page 10).
    CcpRig w;
    REQUIRE(w.ok);
    open_u2(w, "uitwid");
    poll(w);
    CHECK_EQ(ch_at(w, 35, 8), 0x1Eu); // with arrows (no flag): the up arrow, the down arrow
    CHECK_EQ(ch_at(w, 35, 17), 0x1Fu);
    click(w, 35, 8);
    expect(poll(w), {}, "with arrows: the up arrow at 0");
    const unsigned below = ch_at(w, 35, 18);
    CHECK_EQ(ui(w, 'F', UF_NOARROWS_, 0, X_VSC).substr(6), std::string("00"));
    poll(w);
    CHECK_EQ(ch_at(w, 35, 18), below); // nothing drawn past its end
    CHECK_EQ(ch_at(w, 35, 8), 0xDBu); // the thumb (10 x 10 / 60: 1 cell) at the bar's first row
    CHECK_EQ(ch_at(w, 35, 9), 0xB0u);
    CHECK_EQ(ch_at(w, 35, 17), 0xB0u); // the last row the track's
    click(w, 35, 17);
    expect(poll(w), {{UE_CHANGE, X_VSC, 0, 10}}, "the last row: a page down, not a step");
    setv(w, X_VSC, 50);
    poll(w);
    CHECK_EQ(ch_at(w, 35, 17), 0xDBu); // at the end, the thumb on the last row
    CHECK_EQ(ch_at(w, 35, 16), 0xB0u);
    click(w, 35, 8);
    expect(poll(w), {{UE_CHANGE, X_VSC, 0, 40}}, "the first row: a page up, not a step");
    // The thumb dragged from the first row to the last: the whole travel, 0 to 50.
    setv(w, X_VSC, 0);
    poll(w);
    point(w, 35, 8);
    button(w, true);
    expect(poll(w), {}, "the thumb held on the first row");
    point(w, 35, 17);
    std::vector<Event> ev = poll(w);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].value == 50 && (ev[0].flags & UV_LIVE));
    button(w, false);
    poll(w);
    CHECK_EQ(ch_at(w, 35, 17), 0xDBu);
    // A sprite thumb: UITBAR's block 6, Z_VSC (column 43, rows 4-15, the program's 8 x 32) and Z_HSC (column 9,
    // columns 9-38, OOUI's 8 x 8). Without arrows the vertical track is rows 4-15, 96 units, 64 of room; the
    // horizontal, 120 units, 112 of room.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(ui(r, 'F', UF_NOARROWS_, 0, Z_VSC).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'F', UF_NOARROWS_, 0, Z_HSC).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8);
    CHECK_EQ(spr_x(r, 4), 9u * 4);
    CHECK_EQ(ch_at(r, 43, 4), 0xB0u); // the track's, not an arrow
    CHECK_EQ(ch_at(r, 43, 15), 0xB0u);
    setv(r, Z_VSC, 50);
    setv(r, Z_HSC, 100);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 32);
    CHECK_EQ(spr_x(r, 4), 9u * 4 + 112);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_depth(r, 4), 0x0C); // (its last cell, column 38, on the bar)
    click(r, 43, 15);
    expect(poll(r), {{UE_CHANGE, Z_VSC, 0, 60}}, "the sprite bar's last row: a page down");
    click(r, 43, 4);
    expect(poll(r), {{UE_CHANGE, Z_VSC, 0, 50}}, "its first row: a page up");
    // The thumb (units 32-63) held 8 units in and moved 12 on: its start 44, 44 x 100 / 64.
    usb(r).mouse_to(43 * 4 + 2, 4 * 8 + 40);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {}, "the thumb held");
    usb(r).mouse_to(43 * 4 + 2, 4 * 8 + 52);
    r.run(ms(30));
    ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Z_VSC && ev[0].value == 44u * 100 / 64);
    button(r, false);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 64u * (44u * 100 / 64) / 100); // (68 placed again: 43 units on)
    // The horizontal one at 4: its thumb units 4-11 (112 x 4 / 100), from the bar's first column. A press 6 units
    // in, on the bar's first cell, is on the thumb: held, no page.
    setv(r, Z_HSC, 4);
    poll(r);
    CHECK_EQ(spr_x(r, 4), 9u * 4 + 4);
    usb(r).mouse_to(9 * 4 + 6, 10 * 8 + 4);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {}, "the horizontal thumb held on the bar's first cell");
    button(r, false);
    expect(poll(r), {{UE_CHANGE, Z_HSC, 0, 4}}, "let go where it was");
    // The flag cleared: the arrows back, the thumb on the track inside them.
    CHECK_EQ(ui(r, 'F', 0, UF_NOARROWS_, Z_VSC).substr(6), std::string("00"));
    setv(r, Z_VSC, 0);
    poll(r);
    CHECK_EQ(ch_at(r, 43, 4), 0x1Eu);
    CHECK_EQ(spr_y(r, 3), 5u * 8);
}

// ---------------------------------------------------------------------------
// OOUI speed, step S1 (hw/ooui.md, "U3b: speed step S1"): (i) a sprite thumb placed only when its record was drawn
// dirty or its value changed since the last POST, or after a geometry event (UQ_GEOM) with the thumb in the
// damage; (v) a press's, a release's and the wheel's hit test reuse uhot while the pointer hasn't moved and no
// geometry event has come since POINTED found it; (viii) a sprite thumb's slider or scroll bar whose value
// changes draws no cells, only its thumb placed again.

TEST(s1_a_poll_s_cost_among_ninety_records)
{
    // Block 8 (UITBAR's), 90 records on one layer, as OO-Amp's screen. A frame is 119,318 cycles.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    CHECK_EQ(ui(r, 'o', BLK_BIG).substr(6), std::string("00"));
    poll(r);
    poll(r);
    constexpr uint8_t A_TITLE = 3, A_SEEK = 6, A_PLS = 33, A_TR0 = 59;
    // A track's start as OO-Amp's: the seek bar's and the playlist's slider's values, the title and the 26
    // track labels (the playlist's rows). Only the two thumbs whose values changed are placed (i), and neither
    // slider's cells are drawn (viii).
    setv(r, A_SEEK, 500);
    setv(r, A_PLS, 40);
    CHECK_EQ(ui(r, 'd', A_TITLE).substr(6), std::string("00"));
    for (unsigned h = A_TR0; h < A_TR0 + 26u; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls start = polls_cost(r);
    say("a track's start (2 values, 27 labels)", start);
    CHECK_EQ(start.places, 2u);
    CHECK(start.cycles < 450000); // (1,679,598 before S1)
    // A drag's step: band 5 (column 24, rows 11-18) held and moved a row: nothing drawn (viii), its thumb alone
    // placed (i), no hit test (B4).
    point(r, 24, 15);
    button(r, true);
    const Polls down = polls_cost(r);
    CHECK(changed(down.ev, A_EQ5B) >= 0);
    point(r, 24, 14);
    const Polls drag = polls_cost(r);
    say("a drag's step", drag);
    CHECK(changed(drag.ev, A_EQ5B) > changed(down.ev, A_EQ5B));
    CHECK_EQ(drag.ones, 0u);
    CHECK_EQ(drag.places, 1u);
    CHECK_EQ(drag.fronts, 0u);
    CHECK(drag.cycles < 170000); // (241,390 before S1)
    button(r, false);
    poll(r);
    // The wheel over the band, the pointer still since POINTED found it there: no hit test (v), nothing drawn
    // (viii). (Right after the drag, uhot isn't the pointer's: the drag's moves looked for nothing.)
    point(r, 24, 13);
    poll(r);
    const Polls whl = [&] {
        usb(r).mouse_wheel(1);
        r.run(ms(30));
        return polls_cost(r);
    }();
    say("the wheel, the pointer still", whl);
    CHECK(changed(whl.ev, A_EQ5B) > changed(drag.ev, A_EQ5B));
    CHECK_EQ(whl.fronts, 0u);
    CHECK_EQ(whl.ones, 0u);
    CHECK(whl.cycles < 170000); // (311,900 before S1)
    // A press on a button (A_PLAY, columns 10-15, row 22) with the pointer already there, and its release: no
    // hit test (v); the button drawn pressed and again, and only the band's thumb placed, the band drawn dirty as
    // the focus left it (i; 4 before S1, every thumb in the damage).
    point(r, 11, 22);
    poll(r);
    button(r, true);
    const Polls press = polls_cost(r);
    say("a button pressed, the pointer still", press);
    CHECK_EQ(press.fronts, 0u);
    CHECK(press.places <= 1u);
    CHECK(press.cycles < 330000); // (608,801 before S1)
    button(r, false);
    const Polls release = polls_cost(r);
    say("released", release);
    CHECK(std::any_of(release.ev.begin(), release.ev.end(), [](const Event &e) { return e.code == UE_CLICK && e.handle == 35; }));
    CHECK_EQ(release.fronts, 0u);
    CHECK(release.cycles < 130000); // (161,069 before S1)
    // 30 widgets at once (the 15 sliders' values, the 15 status labels): the 15 thumbs placed, no slider drawn
    // (viii). Most of what is left is each thumb's SHOWS, a walk of the records (S2's).
    for (unsigned h : {6u, 7u, 8u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 33u})
        setv(r, static_cast<uint8_t>(h), 30);
    for (unsigned h = 44; h < 59; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls many = polls_cost(r);
    say("30 widgets changed", many);
    CHECK_EQ(many.places, 15u);
    CHECK(many.cycles < 1650000); // (1,829,831 before S1)
}

TEST(s1_a_thumb_follows_every_geometry_event)
{
    // (i): with no geometry event, a thumb is placed only when its record was drawn dirty or its value changed; so
    // each event that can cover or uncover a thumb, or move it, must say so (UQ_GEOM). Each is seen here: the thumb
    // hidden when covered, kept hidden while other widgets are drawn and its own value changes, shown (at its new
    // place) when uncovered, and moved when its parent's frame goes.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    // Z_VSC's 8 x 32 thumb, sprite 3: column 43, rows 5-8 at 0; Z_COVER a window at 42-45, rows 8-10, hidden.
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    const auto hidden_while_busy = [&](const char *what) {
        CHECK_EQ(spr_depth(r, 3), 0);
        CHECK_EQ(ui(r, 'd', Z_LBL).substr(6), std::string("00")); // other drawing: no placing
        CHECK_EQ(ui(r, 'd', Z_WIN).substr(6), std::string("00"));
        poll(r);
        if (spr_depth(r, 3) != 0)
            std::printf("  %s: shown while covered\n", what);
        CHECK_EQ(spr_depth(r, 3), 0);
    };
    // ui_show and ui_hide (REFOCUS)
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    hidden_while_busy("ui_show");
    setv(r, Z_VSC, 1); // its value changed while covered: placed, still hidden
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0);
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    setv(r, Z_VSC, 0);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 5u * 8);
    // A drop-down over its first row (MPASS while a menu is down, MCLOSE), by the keys and by the mouse
    CHECK_EQ(ui(r, 's', Z_BAR).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    key(r, 0x43); // F10
    poll(r);
    hidden_while_busy("the drop-down");
    key(r, K_ESC);
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    click(r, 1, 0); // the menu's title, "Ends", on the bar's first columns
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0);
    click(r, 70, 25); // elsewhere: up
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(ui(r, 'h', Z_BAR).substr(6), std::string("00"));
    poll(r);
    // ui_dirty on a window whose UF_HIDDEN the program cleared itself (STALE)
    const uint16_t cf = static_cast<uint16_t>(rec3(Z_COVER) + UW_FLAGS);
    r.poke(cf, static_cast<uint8_t>(r.phys(cf) & ~0x01u));
    CHECK_EQ(ui(r, 'd', Z_COVER).substr(6), std::string("00"));
    poll(r);
    hidden_while_busy("ui_dirty on the window");
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    // ui_redraw after the program moved a window over it by writing its fields (the doc rule), and back
    const uint16_t cx = static_cast<uint16_t>(rec3(Z_COVER) + 2);
    r.poke(cx, 60);
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    r.poke(cx, 42);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    hidden_while_busy("ui_redraw, moved over it");
    r.poke(cx, 60);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    r.poke(cx, 42);
    poll(r);
    // ui_flags: the window's frame gone (UF_NOFRAME), so its client area a cell up and left: every thumb moved,
    // though none of their records is dirty
    const unsigned x2 = spr_x(r, 2), y2 = spr_y(r, 2), x3 = spr_x(r, 3), y3 = spr_y(r, 3);
    CHECK_EQ(ui(r, 'F', UF_NOFRAME, 0, Z_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK(spr_x(r, 2) == x2 - 4 && spr_y(r, 2) == y2 - 8);
    CHECK(spr_x(r, 3) == x3 - 4 && spr_y(r, 3) == y3 - 8);
    CHECK_EQ(ui(r, 'F', 0, UF_NOFRAME, Z_WIN).substr(6), std::string("00"));
    poll(r);
    CHECK(spr_x(r, 2) == x2 && spr_y(r, 2) == y2);
    // The message box over it (the dialogs' records, REFOCUS), at its last value's place: over its last row, 14
    setv(r, Z_VSC, 100);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 5u * 8 + 48);
    CHECK_EQ(ui(r, 'm', 0).substr(6), std::string("00"));
    poll(r);
    const Place box = rect(r, 0xF0);
    CHECK(box.col <= 43 && box.col + box.w > 43 && box.row <= 14 && box.row + box.h > 14);
    hidden_while_busy("the message box");
    key(r, K_ENTER);
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    // ui_suspend and ui_resume: out of sight, then placed again
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    CHECK_EQ(spr_depth(r, 3), 0);
    CHECK_EQ(ui(r, 'U').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    CHECK_EQ(spr_y(r, 3), 5u * 8 + 48);
    // ui_theme (BOXFULL): OOUI's own thumb image (Z_STD's, sprite 2) drawn again in the new theme's colours,
    // though no record is dirty
    const unsigned img = (upage(r) + 0x29u) * 256u;
    const auto image = [&] {
        std::string s;
        for (unsigned i = 0; i < 32; ++i)
            s += static_cast<char>(vid(r).card_peek(img + i));
        return s;
    };
    const std::string before = image();
    CHECK_EQ(ui(r, 't', 1).substr(6), std::string("00"));
    poll(r);
    CHECK(image() != before);
    CHECK_EQ(spr_depth(r, 2), 0x0C);
    CHECK_EQ(ui(r, 't', 0).substr(6), std::string("00"));
    poll(r);
    CHECK(image() == before);
}

TEST(s1_a_thumb_s_own_changes_place_it)
{
    // (i): a thumb's record drawn dirty is placed whatever the damage: ui_setrange (a slider's top: the thumb moves,
    // no geometry event); and (viii) a value change draws none of its cells but places it. A cell thumb's slider
    // or scroll bar is drawn as before.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    setv(r, Z_STD, 50);
    poll(r);
    const unsigned x = spr_x(r, 2);
    CHECK_EQ(ac(ui(r, 'q', 200, 0, Z_STD)), "0000"); // top 100 to 200: value 50 a quarter along
    poll(r);
    CHECK(spr_x(r, 2) < x);
    // The value: the thumb moved, no cell of the track written (a mark left on one is still there)
    const unsigned map0 = vid(r).peek(0x11) | vid(r).peek(0x12) << 8;
    const unsigned cell = map0 * 256u + 6u * 160u + 20u * 2u; // Z_STD's track, column 20, row 6
    const uint8_t was = vid(r).card_peek(cell);
    vid(r).card_poke(cell, '#');
    setv(r, Z_STD, 150);
    poll(r);
    CHECK(spr_x(r, 2) > x);
    CHECK_EQ(vid(r).card_peek(cell), '#');
    vid(r).card_poke(cell, was);
    CHECK_EQ(ui(r, 'd', Z_STD).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(vid(r).card_peek(cell), was);
}

TEST(s1_a_hit_is_found_again_after_each_geometry_event)
{
    // (v): a press, a release and the wheel take uhot while the pointer is where POINTED last found it; each
    // geometry event since (here ui_show and ui_hide, and ui_dirty on a window the program changed itself) and a
    // drag's moves, which look for nothing, make them look again.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const uint16_t cx = static_cast<uint16_t>(rec3(Z_COVER) + 2), cf = static_cast<uint16_t>(rec3(Z_COVER) + UW_FLAGS);
    r.poke(cx, 10); // Z_COVER (hidden) over Z_BTN's columns 10-13, rows 13-15
    r.poke(static_cast<uint16_t>(cx + 1), 13);
    const auto clicked = [](const std::vector<Event> &ev, uint8_t h) {
        return std::any_of(ev.begin(), ev.end(), [h](const Event &e) { return e.code == UE_CLICK && e.handle == h; });
    };
    point(r, 11, 14);
    poll(r);
    // A window shown under the still pointer takes the press and the release; hidden, the button has them.
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    button(r, true);
    poll(r);
    button(r, false);
    CHECK(!clicked(poll(r), Z_BTN));
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    button(r, true);
    poll(r);
    button(r, false);
    CHECK(clicked(poll(r), Z_BTN));
    // Shown while the button is held: the release is the window's, no click.
    button(r, true);
    poll(r);
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    button(r, false);
    CHECK(!clicked(poll(r), Z_BTN));
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    // The wheel: the button's, then the window's (ui_dirty after the program cleared its UF_HIDDEN itself).
    usb(r).mouse_wheel(1);
    r.run(ms(30));
    expect(poll(r), {{UE_WHEEL, Z_BTN, 0, 1}}, "the wheel over the button");
    r.poke(cf, static_cast<uint8_t>(r.phys(cf) & ~0x01u));
    CHECK_EQ(ui(r, 'd', Z_COVER).substr(6), std::string("00"));
    poll(r);
    usb(r).mouse_wheel(1);
    r.run(ms(30));
    expect(poll(r), {{UE_WHEEL, Z_COVER, 0, 1}}, "the wheel over the window shown");
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    poll(r);
    // A drag from Z_STD (row 6) to the button, let go there: the next press, the pointer still, is the button's.
    point(r, 20, 6);
    poll(r);
    button(r, true);
    poll(r);
    point(r, 11, 14);
    poll(r);
    button(r, false);
    poll(r);
    const std::string v = ui(r, 'V', 0, 0, Z_STD);
    button(r, true);
    std::vector<Event> ev = poll(r);
    button(r, false);
    const std::vector<Event> up = poll(r);
    ev.insert(ev.end(), up.begin(), up.end());
    CHECK(clicked(ev, Z_BTN));
    CHECK(changed(ev, Z_STD) < 0);
    CHECK_EQ(ui(r, 'V', 0, 0, Z_STD), v);
    // The pointer moved between POINTED's look and the press's (the mouse's interrupt, here a write to the event
    // area as the press's CELL starts): the press is where the pointer is, the window's inside, not the button's
    // (no focus).
    CHECK_EQ(ui(r, 'f', Z_STD).substr(6), std::string("00"));
    point(r, 12, 14); // (uhot found again: the drag left it the slider's)
    poll(r);
    const uint16_t cell = lab("CELL");
    const unsigned blka = bank_block(r, "CELL");
    bool moved = false;
    r.watch = [&](uint16_t pc) {
        if (!moved && pc == cell && (r.nb(3) & 0x3F) == blka)
        {
            r.poke(0x3000 + 32, 30 * 4 + 2); // EV_MX: column 30, row 14 still, the window's inside
            moved = true;
        }
    };
    button(r, true);
    ev = poll(r);
    r.watch = nullptr;
    CHECK(moved);
    CHECK(std::none_of(ev.begin(), ev.end(), [](const Event &e) { return e.handle == Z_BTN; }));
    button(r, false);
    poll(r);
    // A modal window the program made itself (its UF_HIDDEN cleared, UF_MODAL set, then ui_dirty on it), away
    // from the button: the pointer moved onto the button and still, its press and release are nobody's (POINTED
    // finds the modal window again before it looks).
    r.poke(cx, 60);
    r.poke(cf, static_cast<uint8_t>((r.phys(cf) & ~0x01u) | UF_MODAL));
    CHECK_EQ(ui(r, 'd', Z_COVER).substr(6), std::string("00"));
    point(r, 12, 14);
    poll(r);
    button(r, true);
    ev = poll(r);
    button(r, false);
    const std::vector<Event> up2 = poll(r);
    ev.insert(ev.end(), up2.begin(), up2.end());
    CHECK(!clicked(ev, Z_BTN));
}

TEST(u3b_an_own_thumb_s_length_apart_from_its_sprite)
{
    // (U3b, the owner 2026-11-12) A scroll bar's own thumb (UF_OWNTHUMB) with UF_NOARROWS and UF_MARGIN takes its
    // length from the margin byte's high four bits (x 4 units), not 0, instead of its sprite's: OO-Amp's playlist
    // bar, 11 rows (88 px), its 8 x 32 sprite holding a 20 px thumb at its top, travels 88 - 20 = 68 px, not 56.
    // (With arrows the bits do nothing, as before: u3b_a_scroll_bar_s_margin_takes_a_press.)
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const uint16_t b = rec3(Z_VSC); // column 43, from row 4
    r.poke(static_cast<uint16_t>(b + UW_FLAGS), static_cast<uint8_t>(r.phys(static_cast<uint16_t>(b + UW_FLAGS)) | UF_NOARROWS_ | 0x40));
    r.poke(static_cast<uint16_t>(b + 5), 11); // UW_H: 11 rows
    r.poke(static_cast<uint16_t>(b + 14), std::vector<uint8_t>{10, 0x51}); // the page 10; a 20-unit thumb, a column each side
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8);
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    setv(r, Z_VSC, 100);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 68); // the thumb's end at the bar's (its sprite's 12 more past it)
    CHECK_EQ(spr_depth(r, 3), 0x0C);
    setv(r, Z_VSC, 50);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 34);
    // A press 5 units into the thumb holds it; 34 units on, the value 34 x 100 / 68.
    setv(r, Z_VSC, 0);
    poll(r);
    usb(r).mouse_to(43 * 4 + 2, 4 * 8 + 5);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {}, "the thumb held");
    usb(r).mouse_to(43 * 4 + 2, 4 * 8 + 5 + 34);
    r.run(ms(30));
    std::vector<Event> ev = poll(r);
    CHECK(ev.size() == 1 && ev[0].code == UE_CHANGE && ev[0].handle == Z_VSC && ev[0].value == 50u);
    button(r, false);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 34);
    // At 0, a press 25 units down is past the 20-unit thumb (on its sprite's lower part): the track's, a page.
    setv(r, Z_VSC, 0);
    poll(r);
    usb(r).mouse_to(43 * 4 + 2, 4 * 8 + 25);
    r.run(ms(30));
    button(r, true);
    expect(poll(r), {{UE_CHANGE, Z_VSC, 0, 10}}, "past the thumb: a page down");
    button(r, false);
    poll(r);
    // The high bits 0: the sprite's length again (88 - 32 = 56).
    r.poke(static_cast<uint16_t>(b + 15), 0x01);
    setv(r, Z_VSC, 100);
    poll(r);
    CHECK_EQ(spr_y(r, 3), 4u * 8 + 56);
}

// ---------------------------------------------------------------------------
// S2, the position cache (hw/ooui.md, "U3b: speed step S2"): block C's entry for each record, where it shows, read
// by PASS's, FRONT's, MARGIN's and SHOWS's walks instead of a RECT of every record.

namespace
{
    /// Block C's cache entry h: c0, r0, c1, r1, its flags (uicache.s's arrays, in block C's bank).
    std::array<unsigned, 5> cache_entry(CcpRig &r, unsigned h)
    {
        const uint32_t bank = bank_block(r, "CFILL") * 0x4000u;
        std::array<unsigned, 5> e{};
        const char *names[5] = {"cx0", "cy0", "cx1", "cy1", "cfl"};
        for (unsigned i = 0; i < 5; ++i)
            e[i] = r.phys(bank + (lab(names[i]) - 0xC000u) + h);
        return e;
    }
    /// Every table record's cache entry against ui_rect (its place, and RECT's rclip and rover after it): its cells
    /// inside its parents' client areas, hidden, on the overlay; an unused record or a menu's hidden. The pointer
    /// moved first (to one of two desktop cells, by turns) and a poll, so that HIT finds the cache again if it is
    /// stale (CFILL): it must hold after, and every entry match. The mismatches, said.
    unsigned cache_mismatches(CcpRig &r, const char *what)
    {
        static bool turn = false;
        turn = !turn;
        point(r, turn ? 79 : 78, 29);
        poll(r);
        const bool stale = (r.phys(lab("uany")) & 0x20) != 0; // UQ_CSTALE
        if (stale)
            std::printf("  %s: the cache still stale after a hit test\n", what);
        CHECK(!stale);
        const unsigned n = r.phys(lab("ucount")), tab = word_at(r, lab("utab"));
        unsigned bad = 0;
        for (unsigned h = 0; h < n; ++h)
        {
            const std::array<unsigned, 5> e = cache_entry(r, h);
            const unsigned type = r.phys(tab + h * 16u);
            std::array<unsigned, 5> want{0, 0, 0, 0, 1};
            if (type != 0 && !(type >= 10 && type <= 12))
            {
                const Place p = rect(r, static_cast<uint8_t>(h));
                if (!p.hidden)
                {
                    const unsigned k0 = r.phys(lab("rclip")), k1 = r.phys(lab("rclip") + 1u),
                                   k2 = r.phys(lab("rclip") + 2u), k3 = r.phys(lab("rclip") + 3u);
                    want = {std::max(p.col, k0), std::max(p.row, k1), std::min(std::min(p.col + p.w, 255u), k2),
                            std::min(std::min(p.row + p.h, 255u), k3), r.phys(lab("rover")) * 2u};
                    want[4] |= e[4] & (type == 2 ? 4u : 0u); // a window's shadow bit: the theme's (not checked here)
                }
            }
            if (e != want)
            {
                ++bad;
                std::printf("  %s: record %u's entry %u,%u-%u,%u %02x, want %u,%u-%u,%u %02x\n", what, h, e[0], e[1], e[2],
                            e[3], e[4], want[0], want[1], want[2], want[3], want[4]);
            }
        }
        return bad;
    }
} // namespace

namespace
{
    /// OOUI's base layer's cells (layer `layer`'s map on the card: a character and an attribute each), as drawn,
    /// from column 13 on: on layer 0 the test program's replies to the commands are written to the first columns.
    std::string base_cells(CcpRig &r, unsigned layer = 0)
    {
        const unsigned map0 = vid(r).peek(0x11 + 0x10 * layer) | vid(r).peek(0x12 + 0x10 * layer) << 8;
        std::string c;
        for (unsigned row = 0; row < 30; ++row)
            for (unsigned i = 13u * 2u; i < 80u * 2u; ++i)
                c += static_cast<char>(vid(r).card_peek(map0 * 256u + row * 160u + i));
        return c;
    }
} // namespace

TEST(s2_the_position_cache_holds_every_record_s_place)
{
    // After each step of a session, every entry is the record's place as ui_rect finds it: ui_show and ui_hide (a
    // leaf's own entry, CSET; a window with children, the whole cache again), ui_flags (the same), a drop-down by
    // the keys, the message box and its going, a record the program moved by its fields with ui_redraw (the rule),
    // ui_suspend and ui_resume, ui_theme, and a record the program moved by its fields and only dirtied: PASS
    // places a dirty record at each paint and compares it with its entry, so the move is caught.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    CHECK_EQ(cache_mismatches(r, "opened"), 0u);
    const auto step = [&](const char *what, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0) {
        CHECK_EQ(ui(r, c, a, x, y).substr(6), std::string("00"));
        poll(r);
        CHECK_EQ(cache_mismatches(r, what), 0u);
    };
    step("a window shown", 's', Z_COVER);
    step("and hidden", 'h', Z_COVER);
    step("a label hidden", 'h', Z_LBL);
    step("and shown", 's', Z_LBL);
    step("a window's frame gone", 'F', UF_NOFRAME, 0, Z_WIN);
    step("and back", 'F', 0, UF_NOFRAME, Z_WIN);
    step("a label disabled", 'F', UF_DISABLED, 0, Z_LBL);
    // A theme with no shadows (theme 4, the default lit, with TH_FLAGS' TF_SHADOW taken from its copy): the
    // window's own entry is the same with its frame or without, but its children's places aren't (CSET: a record
    // with children makes the cache stale).
    const uint16_t hflags = static_cast<uint16_t>(lab("UI_HOUSE") + 1u);
    const uint8_t was = r.phys(hflags);
    r.poke(hflags, 0);
    step("a theme with no shadows", 't', 4);
    step("its frame gone", 'F', UF_NOFRAME, 0, Z_WIN);
    step("and back", 'F', 0, UF_NOFRAME, Z_WIN);
    r.poke(hflags, was);
    step("the default theme", 't', 0);
    step("the menu bar shown", 's', Z_BAR);
    key(r, 0x43); // F10: a drop-down
    poll(r);
    CHECK_EQ(cache_mismatches(r, "a drop-down"), 0u);
    key(r, K_ESC);
    poll(r);
    CHECK_EQ(cache_mismatches(r, "the drop-down up"), 0u);
    step("the menu bar hidden", 'h', Z_BAR);
    step("the message box", 'm', 0);
    key(r, K_ENTER);
    poll(r);
    CHECK_EQ(cache_mismatches(r, "the message box gone"), 0u);
    const uint16_t lx = static_cast<uint16_t>(rec3(Z_LBL) + 2), cx = static_cast<uint16_t>(rec3(Z_COVER) + 2);
    r.poke(cx, 20);
    step("a hidden window moved by its fields, ui_redraw", 'D');
    step("and shown", 's', Z_COVER);
    r.poke(lx, 9);
    step("a label moved by its fields, ui_redraw", 'D');
    CHECK_EQ(ui(r, 'S').substr(6), std::string("00"));
    step("ui_suspend, ui_resume", 'U');
    step("ui_theme 1", 't', 1);
    step("ui_theme 0", 't', 0);
    // A window's shadow drawn again where only it meets the box: a label moved under Z_COVER's shadow (its right,
    // columns 46-47, rows 9-10), then dirtied: the cells as they were (CMEETS's shadow).
    r.poke(cx, 42);
    r.poke(lx, 43);
    r.poke(static_cast<uint16_t>(rec3(Z_LBL) + 3), 7);
    step("a label under a window's shadow, ui_redraw", 'D');
    const std::string shade = base_cells(r);
    CHECK_EQ(ui(r, 'd', Z_LBL).substr(6), std::string("00"));
    poll(r);
    CHECK(base_cells(r) == shade);
    CHECK_EQ(cache_mismatches(r, "the label dirty"), 0u);
    r.poke(static_cast<uint16_t>(rec3(Z_LBL) + 3), 10);
    // Moved by its fields and only dirtied: caught at the paint.
    r.poke(lx, 12);
    step("a label moved by its fields, only ui_dirty", 'd', Z_LBL);
    r.poke(static_cast<uint16_t>(rec3(Z_COVER) + 3), 2);
    step("a window moved by its fields, only ui_dirty", 'd', Z_COVER);
}

TEST(s2_a_poll_s_cost_with_the_position_cache)
{
    // Block 8 (UITBAR's), 90 records on one layer, as OO-Amp's screen; a frame is 119,318 cycles. Each limit fails
    // with the cache unread (every record placed by RECT, as before S2). The cycles before S2 (20caf52) in brackets.
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    CHECK_EQ(ui(r, 'o', BLK_BIG).substr(6), std::string("00"));
    poll(r);
    poll(r);
    // One label dirty, the table's first: the records after it not placed unless they meet the box (S2).
    CHECK_EQ(ui(r, 'd', A_TIME).substr(6), std::string("00"));
    const Polls one = polls_cost(r);
    say("one label dirty", one);
    CHECK_EQ(one.ones, 1u);
    CHECK(one.cycles < 80000); // (142,225, 88 records looked at by ONE)
    // A track's start as OO-Amp's: two values, the title and 26 track labels.
    constexpr uint8_t A_TITLE = 3, A_SEEK = 6, A_PLS = 33, A_TR0 = 59;
    setv(r, A_SEEK, 600);
    setv(r, A_PLS, 50);
    CHECK_EQ(ui(r, 'd', A_TITLE).substr(6), std::string("00"));
    for (unsigned h = A_TR0; h < A_TR0 + 26u; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls start = polls_cost(r);
    say("a track's start (2 values, 27 labels)", start);
    CHECK_EQ(start.places, 2u);
    CHECK(start.cycles < 260000); // (378,239)
    // A press on a button with the pointer moved there first (a hit test, FRONT, from the cache), and its release.
    point(r, 11, 22);
    button(r, true);
    const Polls press = polls_cost(r);
    say("a button pressed, the pointer moved there", press);
    CHECK_EQ(press.fronts, 1u);
    CHECK(press.cycles < 260000); // (387,426)
    button(r, false);
    const Polls release = polls_cost(r);
    say("released", release);
    CHECK(std::any_of(release.ev.begin(), release.ev.end(), [](const Event &e) { return e.code == UE_CLICK && e.handle == 35; }));
    CHECK(release.cycles < 80000); // (104,317)
    // A drag's step: band 5 held and moved a row, its thumb placed (SHOWS, from the cache).
    point(r, 24, 15);
    button(r, true);
    polls_cost(r);
    point(r, 24, 14);
    const Polls drag = polls_cost(r);
    say("a drag's step", drag);
    CHECK_EQ(drag.places, 1u);
    CHECK(drag.cycles < 100000); // (141,830)
    button(r, false);
    poll(r);
    // 30 widgets at once: the 15 thumbs placed (each SHOWS from the cache), the 15 status labels.
    for (unsigned h : {6u, 7u, 8u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 33u})
        setv(r, static_cast<uint8_t>(h), 40);
    for (unsigned h = 44; h < 59; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls many = polls_cost(r);
    say("30 widgets changed", many);
    CHECK_EQ(many.places, 15u);
    CHECK(many.cycles < 600000); // (1,555,648)
    // The cache found again whole after a geometry event (ui_redraw: every record drawn), and a leaf shown again
    // (its own entry only, CSET): measured.
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    const Polls redraw = polls_cost(r);
    say("ui_redraw (the cache found again)", redraw); // (2,558,586)
    CHECK_EQ(ui(r, 'h', A_X4B).substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 's', A_X4B).substr(6), std::string("00"));
    const Polls shown = polls_cost(r);
    say("the last label shown again", shown);
    CHECK(shown.cycles < 60000); // (45,655; 158,612 with the whole cache found again, not CSET)
}

// ---------------------------------------------------------------------------
// B5, the damage's boxes (hw/ooui.md, "U3b: B5"): up to UI_BOXES boxes a layer, joined only when they overlap, so
// a record is drawn again only where it meets one.

TEST(b5_two_far_apart_dirty_records_draw_nothing_between_them)
{
    // Block 8 (UITBAR's), 90 records on one layer. The table's first label (A_TIME, top left) and the fourth status
    // label (A_ST3, bottom right, record 47: 42 records after it, the tracks' and the extras' labels among them, in
    // the two's bounds) dirtied together: each drawn, and no other record (with one box, every record their bounds met was drawn
    // again: B5's before). The cells then are the cells ui_redraw draws (OOUI's base layer, 1 in block 8: the
    // frame has a sprite of its own that moves).
    CcpRig r;
    REQUIRE(r.ok);
    start_ui(r, "uitbar");
    CHECK_EQ(ui(r, 'o', BLK_BIG).substr(6), std::string("00"));
    poll(r);
    poll(r);
    constexpr uint8_t A_ST3 = 47;
    const Place a = rect(r, A_TIME), b = rect(r, A_ST3);
    std::printf("  the two: %u,%u %ux%u and %u,%u %ux%u\n", a.col, a.row, a.w, a.h, b.col, b.row, b.w, b.h);
    CHECK(b.row > a.row + 4); // far apart
    CHECK_EQ(ui(r, 'd', A_TIME).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'd', A_ST3).substr(6), std::string("00"));
    const Polls two = polls_cost(r);
    say("two far-apart labels dirty", two);
    CHECK_EQ(two.ones, 2u);
    CHECK(two.cycles < 90000);
    const std::string drawn = base_cells(r, 1);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK(base_cells(r, 1) == drawn);
    // Two that overlap are joined: a label and the window under it (its parent, before it in the table, dirtied
    // after it): one box, every record meeting the window drawn again, the cells unchanged.
    CHECK_EQ(ui(r, 'd', A_X4B).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'd', 1).substr(6), std::string("00"));
    poll(r);
    CHECK(base_cells(r, 1) == drawn);
    // More boxes than UI_BOXES (the 26 track labels, a row each: none overlaps the next): the last joins the
    // layer's last box, and every row is drawn; the cells as ui_redraw's.
    constexpr uint8_t A_TR0 = 59;
    for (unsigned h = A_TR0; h < A_TR0 + 26u; ++h)
        CHECK_EQ(ui(r, 'd', static_cast<uint8_t>(h)).substr(6), std::string("00"));
    const Polls rows = polls_cost(r);
    say("26 track labels dirty", rows);
    CHECK(rows.ones >= 26u);
    CHECK(base_cells(r, 1) == drawn);
}

TEST(b5_each_box_is_drawn_again_whole)
{
    // UITBAR's U3b scene (OOUI's base layer 0). Two far-apart records hidden at once: two boxes, each cleared and
    // every record under it drawn again in it, the window under both in both (ONE once a box it meets). Then two
    // far-apart records dirtied, the window shown over the second (Z_COVER over Z_VSC): the window drawn again in
    // the second box (CMEETS looks at every box used). Two far-apart holes on the overlay's layer, nothing under
    // them. The cells each time as ui_redraw draws them.
    CcpRig r;
    REQUIRE(r.ok);
    open_u3b(r);
    const auto redrawn = [&] {
        CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
        poll(r);
        return base_cells(r);
    };
    CHECK_EQ(ui(r, 'h', Z_LBL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'h', Z_BTN).substr(6), std::string("00"));
    poll(r);
    const std::string holes = base_cells(r);
    CHECK(holes == redrawn());
    CHECK_EQ(ui(r, 's', Z_LBL).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', Z_BTN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 's', Z_COVER).substr(6), std::string("00"));
    poll(r);
    const std::string base = redrawn();
    CHECK_EQ(ui(r, 'd', Z_OWN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'd', Z_VSC).substr(6), std::string("00"));
    const Polls two = polls_cost(r);
    say("U3b: a slider and the scroll bar under a window dirty", two);
    CHECK(base_cells(r) == base);
    // Two far-apart holes on the overlay's layer (1), where no record is under them: the window moved to the top
    // right and put on the overlay, and the message box; both gone in one paint, the layer then as ui_redraw
    // leaves it. (With the last overlay window gone the layer is clear whole, so PASSA's clearing of a box past
    // the first isn't what this sees: no scene here has a hole that no record draws over.)
    r.poke(static_cast<uint16_t>(rec3(Z_COVER) + 2), 70);
    r.poke(static_cast<uint16_t>(rec3(Z_COVER) + 3), 2);
    CHECK_EQ(ui(r, 'F', UF_OVERLAY, 0, Z_COVER).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK_EQ(ui(r, 'm', 0).substr(6), std::string("00"));
    poll(r);
    const std::string up = base_cells(r, 1);
    CHECK_EQ(ui(r, 'h', Z_COVER).substr(6), std::string("00"));
    key(r, K_ENTER);
    poll(r);
    const std::string gone = base_cells(r, 1);
    CHECK(gone != up);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    CHECK(base_cells(r, 1) == gone);
}

TEST(s2_a_new_table_s_menus_and_unused_records_take_no_hit)
{
    // The review's round 8, P1: a table opened after another, its menus' items and its unused records never
    // placed by PASS, must not keep the last table's entries. UITWID: the U2 scene's first radio button (record
    // 3, X_R0) under the pointer; ui_close; the main scene, whose record 3 is a menu's item (I_OPEN, hidden, its
    // menu up), the desktop at that cell; the paint (PASS filling the cache) before any hit test, as the pointer
    // hasn't moved; then a click there: nothing (the desktop), not the item.
    CcpRig r;
    REQUIRE(r.ok);
    open_u2(r, "uitwid");
    poll(r);
    const Place p = rect(r, X_R0);
    point(r, p.col, p.row);
    poll(r);
    CHECK_EQ(ui(r, 'c').substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'o', BLK_MAIN).substr(6), std::string("00"));
    poll(r);
    button(r, true);
    button(r, false);
    const std::vector<Event> ev = poll(r);
    expect(ev, {}, "a click where the last table's radio button was");
    // A record the program made unused itself (its type 0), with ui_redraw as the rule asks, the pointer still:
    // the paint (PASS filling the cache: ui_redraw made it stale) comes before the press's hit test, so the
    // record's entry must say hidden. W_SIDE, the second window, over the main window's Cancel button at its
    // first column: a click there is the window's (nothing), then, the window unused, Cancel's.
    const Place side = rect(r, W_SIDE), can = rect(r, B_CAN);
    CHECK(side.col >= can.col && side.col < can.col + can.w && can.row >= side.row && can.row < side.row + side.h);
    point(r, side.col, can.row);
    poll(r);
    button(r, true);
    button(r, false);
    expect(poll(r), {}, "a click on the second window over Cancel");
    r.poke(rec(W_SIDE), 0);
    CHECK_EQ(ui(r, 'D').substr(6), std::string("00"));
    poll(r);
    button(r, true);
    button(r, false);
    const std::vector<Event> got = poll(r);
    if (got.empty() || got.back().code != UE_CLICK || got.back().handle != B_CAN)
        std::printf("  the window unused: %s, want Cancel's click last\n", show(got).c_str());
    CHECK(!got.empty() && got.back().code == UE_CLICK && got.back().handle == B_CAN);
}

// ---------------------------------------------------------------------------------------------------------------
// UIDEMO (oodos/demo/uidemo.s, proposed): the demo card's OOUI program, as a visitor runs it. The demo card
// booted, UIDEMO typed at A>, and its screens checked by golden frames (the main screen, a menu down, the file
// picker on A:, the Turbo Vision theme); keys and the mouse drive it, and what ui_poll returned is read from its
// EVLOG (4 bytes an event: its code, its widget, UV_VALUE; EVN counts them). Quit gives A> back with the
// console's screen.

namespace
{
    // uidemo.s's handles (its table's order)
    enum : uint8_t
    {
        D_DESK, D_BAR, D_MFILE, D_ABOUT, D_SEP, D_QUIT, D_MVIEW, D_THEME, D_MAIN, D_HEAD, D_OPTS, D_SOUND, D_GRID,
        D_SPEED, D_SLOW, D_NORM, D_FAST, D_PLIST, D_LIST, D_LVOL, D_VOL, D_LLEVEL, D_LEVEL, D_LNAME, D_NAME, D_OPEN,
        D_BABOUT, D_BQUIT, D_STAT, D_COUNT
    };
    constexpr uint8_t K_A = 0x04, K_D = 0x07, K_V = 0x19, UI_MSG_ = 0xF0, UI_PICKER_ = 0xF1;

    uint16_t dlab(const char *name)
    {
        static const std::map<std::string, uint16_t> l = labels(UTILS + "/uidemo.lbl");
        return l.at(name);
    }
    struct DemoEvent
    {
        unsigned code, handle, value;
        bool operator==(const DemoEvent &o) const { return code == o.code && handle == o.handle && value == o.value; }
    };
    /// The events UIDEMO logged since `seen` (EVN's last reading), which moves on.
    std::vector<DemoEvent> demo_events(Rig &r, unsigned &seen)
    {
        std::vector<DemoEvent> ev;
        const unsigned n = r.phys(dlab("EVN"));
        for (; (seen & 0xFF) != n; ++seen)
        {
            const uint32_t at = dlab("EVLOG") + (seen & 63) * 4u;
            ev.push_back({r.phys(at), r.phys(at + 1), static_cast<unsigned>(r.phys(at + 2) | r.phys(at + 3) << 8)});
        }
        return ev;
    }
    std::string show_demo(const std::vector<DemoEvent> &ev)
    {
        std::string s;
        char b[40];
        for (const DemoEvent &e : ev)
        {
            std::snprintf(b, sizeof b, "[%u %u %u]", e.code, e.handle, e.value);
            s += b;
        }
        return s.empty() ? "[]" : s;
    }
    void expect_demo(Rig &r, unsigned &seen, const std::vector<DemoEvent> &want, const char *what)
    {
        r.run(ms(100));
        const std::vector<DemoEvent> got = demo_events(r, seen);
        if (!(got == want))
            std::printf("  %s: got %s, want %s\n", what, show_demo(got).c_str(), show_demo(want).c_str());
        CHECK(got == want);
    }
    /// A widget's value in UIDEMO's table.
    unsigned demo_value(Rig &r, uint8_t h) { return static_cast<unsigned>(r.phys(dlab("tab") + h * 16u + 10) | r.phys(dlab("tab") + h * 16u + 11) << 8); }
    /// UIDEMO's status line, its last event said.
    std::string demo_status(Rig &r)
    {
        std::string s;
        for (uint32_t a = dlab("stat"); r.phys(a) && s.size() < 64; ++a)
            s += static_cast<char>(r.phys(a));
        return s;
    }
    /// The demo card booted, UIDEMO typed at A> and its first paint shown. @return it came up
    /// `console`: the video card's registers at A>, before UIDEMO.
    bool start_demo(Rig &r, std::array<uint8_t, 0x30> &console)
    {
        if (!r.ok || r.mount(1, "/OODOS/A.IMG") != 0 || r.mount(2, "/OODOS/B.IMG") != 0 || !r.boot())
            return false;
        no_splash(r);
        for (unsigned i = 0; i < console.size(); ++i)
            console[i] = vid(r).peek(i);
        r.type("UIDEMO\r");
        for (int i = 0; i < 100 && r.phys(dlab("READY")) != 1; ++i)
            r.run(ms(50));
        r.run(ms(100));
        return r.phys(dlab("READY")) == 1;
    }
} // namespace

TEST(uidemo_on_the_demo_card_by_keys_and_the_mouse)
{
    Rig r(sd_image("demo.sparse"));
    std::array<uint8_t, 0x30> regs{};
    REQUIRE(start_demo(r, regs));
    unsigned seen = 0;
    expect_demo(r, seen, {}, "the start");
    golden(r, "uidemo-main");
    CHECK_EQ(demo_value(r, D_LEVEL), 40u); // the bar where the slider starts

    // The keyboard: a name typed in the field (the focus starts there), Tab on to Open file.
    usb(r).key(L_SHIFT, true);
    key(r, K_A);
    usb(r).key(L_SHIFT, false);
    key(r, K_D);
    key(r, K_A);
    key(r, K_TAB);
    // Each key a live change (UV_VALUE the cursor), then the field's last as the focus leaves it.
    expect_demo(r, seen, {{UE_CHANGE, D_NAME, 1}, {UE_CHANGE, D_NAME, 2}, {UE_CHANGE, D_NAME, 3}, {UE_FOCUS, D_OPEN, 0},
                          {UE_CHANGE, D_NAME, 3}}, "a name and Tab");
    CHECK_EQ(demo_status(r), std::string("Hello, Ada"));
    // Shift+Tab back, twice: the slider; Right moves it a step (5), the bar with it.
    key(r, K_TAB, L_SHIFT);
    key(r, K_TAB, L_SHIFT);
    expect_demo(r, seen, {{UE_FOCUS, D_NAME, 0}, {UE_FOCUS, D_VOL, 0}}, "Shift+Tab");
    key(r, K_RIGHT);
    expect_demo(r, seen, {{UE_CHANGE, D_VOL, 45}}, "Right on the slider");
    CHECK_EQ(demo_value(r, D_LEVEL), 45u);
    CHECK_EQ(demo_status(r), std::string("Volume: 45"));

    // The mouse: Grid's check box (column 8, row 8), Fast (column 27, row 9), the slider pressed at its middle.
    point(r, 8, 8);
    button(r, true);
    button(r, false);
    point(r, 32, 9);
    button(r, true);
    button(r, false);
    expect_demo(r, seen, {{UE_FOCUS, D_GRID, 0}, {UE_CHANGE, D_GRID, 1}, {UE_CHANGE, D_SPEED, 2}}, "Grid and Fast clicked");
    CHECK_EQ(demo_status(r), std::string("Speed: Fast"));
    point(r, 15 + 17, 12);
    button(r, true);
    button(r, false);
    r.run(ms(100));
    const std::vector<DemoEvent> slid = demo_events(r, seen);
    CHECK(!slid.empty() && slid.back().code == UE_CHANGE && slid.back().handle == D_VOL);
    CHECK(!slid.empty() && demo_value(r, D_LEVEL) == slid.back().value && slid.back().value >= 45 && slid.back().value <= 55);

    // A planet chosen with the mouse, its row, then Enter.
    point(r, 56, 9); // the list's third row: Earth
    button(r, true);
    button(r, false);
    key(r, K_ENTER);
    expect_demo(r, seen, {{UE_CHANGE, D_LIST, 2}, {UE_SELECT, D_LIST, 2}}, "Earth");
    CHECK_EQ(demo_status(r), std::string("Chosen: Earth"));

    // The File menu down (Alt+F), then Escape.
    key(r, K_F, L_ALT);
    r.run(ms(100));
    golden(r, "uidemo-menu");
    key(r, K_ESC);
    // View's item: the Turbo Vision theme, its check mark on.
    key(r, K_V, L_ALT);
    key(r, K_ENTER);
    expect_demo(r, seen, {{UE_MENU, D_THEME, 0}}, "the theme's item");
    CHECK_EQ(demo_value(r, D_THEME), 1u);
    r.run(ms(100));
    golden(r, "uidemo-turbo");
    key(r, K_V, L_ALT);
    key(r, K_ENTER);
    expect_demo(r, seen, {{UE_MENU, D_THEME, 1}}, "the theme's item again"); // UV_VALUE its check mark when chosen
    CHECK_EQ(demo_value(r, D_THEME), 0u);

    // Open file... clicked: the picker, on A:; its listing read over a few polls; Escape cancels it.
    point(r, 10, 19);
    button(r, true);
    button(r, false);
    r.run(ms(1500));
    expect_demo(r, seen, {{UE_FOCUS, D_OPEN, 0}, {UE_CLICK, D_OPEN, 0}, {UE_MODAL, UI_PICKER_, 1}}, "Open file...");
    golden(r, "uidemo-picker");
    key(r, K_ESC);
    expect_demo(r, seen, {{UE_PICK, UI_PICKER_, 1}, {UE_MODAL, UI_PICKER_, 0}}, "the picker cancelled");
    CHECK_EQ(demo_status(r), std::string("No file picked"));
    // About (F1): the message box, Enter its OK.
    key(r, K_F1);
    r.run(ms(100));
    key(r, K_ENTER);
    expect_demo(r, seen, {{UE_MENU, D_ABOUT, 0}, {UE_MODAL, UI_MSG_, 1}, {UE_CLOSE, UI_MSG_, 0}, {UE_MODAL, UI_MSG_, 0}}, "About");

    // Escape with nothing open: A> again, the console's screen as it was before UIDEMO.
    const size_t from = r.out.size();
    key(r, K_ESC);
    CHECK(r.until("A>", ms(2000), from));
    r.run(ms(100));
    // (Not LINE, $06-$07, the beam's; nor layer 1's, $20-$2F, which is off, as at A>.)
    CHECK_EQ(regs[0] & 0x04, 0);
    for (unsigned i = 0; i < 0x20; ++i)
    {
        if (i == 6 || i == 7)
            continue;
        if (vid(r).peek(i) != regs[i])
            std::printf("  the video card's register $%02X: $%02X, at A> before $%02X\n", i, vid(r).peek(i), regs[i]);
        CHECK_EQ(vid(r).peek(i), regs[i]);
    }
}
