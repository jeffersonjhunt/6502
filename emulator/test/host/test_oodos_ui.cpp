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
    unsigned free_blocks(Rig &r) { return static_cast<unsigned>(std::stoul(ui(r, 'B').substr(0, 2), nullptr, 16)); }
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
    CHECK_EQ(std::stoul(ui(r, 'E', 2).substr(0, 2), nullptr, 16), all - 2);
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
