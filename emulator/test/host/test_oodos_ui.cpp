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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
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
    constexpr uint32_t MBOX = 0x8100, TEXTBUF = 0x8200; ///< uitest.s's

    // uitest.s's handles (its table's order) and blocks.
    enum : uint8_t
    {
        W_DESK, W_BAR, M_FILE, I_OPEN, I_SAVE, I_SEP, I_EXIT, M_EDIT, I_CUT, I_COPY, M_HELP, I_ABOUT, W_MAIN, L_HEAD,
        P_OPTS, C_SHAD, C_MOUS, L_RIGHT, L_DIS, B_OK, B_CAN, W_SIDE, B_TOG, L_SIDE, W_OVER, L_OVER, B_HIDE, W_TALL,
        L_EDGE, MAIN_COUNT
    };
    // ooui.inc's record fields and flags
    constexpr unsigned UW_TYPE = 0, UW_PARENT = 1, UW_FLAGS = 6, UW_TEXT = 8, UW_VALUE = 10;
    constexpr uint8_t UF_DISABLED = 0x02, UF_NOFOCUS = 0x08, UF_MODAL = 0x20, UF_OVERLAY = 0x40, UF_NOFRAME = 0x80;
    constexpr uint8_t UT_NONE = 0, UT_PANEL = 3;
    enum : uint8_t { BLK_MAIN, BLK_ONE, BLK_DESK };
    // ooui.inc's events
    enum : uint8_t { UE_NONE, UE_CLICK, UE_CHANGE, UE_SELECT, UE_MARK, UE_MENU, UE_CLOSE, UE_FOCUS, UE_KEY, UE_WHEEL };
    // HID usages
    constexpr uint8_t K_ENTER = 0x28, K_ESC = 0x29, K_TAB = 0x2B, K_SPACE = 0x2C, K_RIGHT = 0x4F, K_DOWN = 0x51,
                      K_F1 = 0x3A, K_C = 0x06, K_F = 0x09, K_Q = 0x14, K_T = 0x17, K_Z = 0x1D;
    constexpr uint8_t L_CTRL = 0xE0, L_SHIFT = 0xE1, L_ALT = 0xE2;

    VideoCard &vid(Rig &r) { return *r.machine->video_card(); }
    UsbCard &usb(Rig &r) { return *r.machine->usb_card(); }

    /// The ROM's boot splash stopped (the card shows it for its first 300 frames, whatever CTRL says).
    void no_splash(Rig &r)
    {
        r.machine->bus().write(0xC489, 0);
        r.run(ms(40));
    }

    void start_ui(CcpRig &r)
    {
        no_splash(r);
        r.poke(MBOX, 0);
        r.poke_com(read_bytes(UTILS + "/uitest.com"));
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("\nO ", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("\nO ", from)));
        CHECK_EQ(field(r.out.substr(from), "O").substr(6), std::string("00")); // GX_INIT, GX_PALINIT, GX_SPRINIT
    }
    /// Command `c` with A, X, Y: "aa xx cc", and the text it printed before (ui_poll's events).
    std::string ui(CcpRig &r, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0, std::string *before = nullptr)
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
        bool operator==(const Event &o) const { return code == o.code && handle == o.handle && y == o.y && value == o.value; }
    };
    /// ui_poll until UE_NONE: the events it returned, "E cc xx yy vvvv" each; then two frames for what it drew
    /// (the palette and the sprites are shown at a vertical blank).
    std::vector<Event> poll(CcpRig &r)
    {
        std::string text;
        ui(r, 'p', 0, 0, 0, &text);
        std::vector<Event> ev;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            unsigned c = 0, h = 0, y = 0, v = 0;
            if (std::sscanf(line.c_str(), "E %x %x %x %x", &c, &h, &y, &v) == 4)
                ev.push_back({c, h, y, v});
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
    void key(CcpRig &r, uint8_t usage, uint8_t mod = 0)
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
    void point(CcpRig &r, unsigned col, unsigned row)
    {
        usb(r).mouse_to(static_cast<int>(col * 4 + 2), static_cast<int>(row * 8 + 4));
        r.run(ms(30));
    }
    void button(CcpRig &r, bool down)
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
    void golden(CcpRig &r, const std::string &name)
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
    Place rect(CcpRig &r, uint8_t h)
    {
        const std::string a = ui(r, 'r', h);
        Place p{};
        if (a[0] == '?')
            return p;
        p.col = static_cast<unsigned>(std::stoul(a.substr(0, 2), nullptr, 16));
        p.row = static_cast<unsigned>(std::stoul(a.substr(3, 2), nullptr, 16));
        p.hidden = a.substr(6, 2) == "01";
        const std::map<std::string, uint16_t> l = labels(UTILS + "/uitest.lbl");
        p.w = r.phys(l.at("UI_RECT") + 2u);
        p.h = r.phys(l.at("UI_RECT") + 3u);
        return p;
    }
    void expect_place(CcpRig &r, uint8_t h, Place want)
    {
        const Place p = rect(r, h);
        if (p.col != want.col || p.row != want.row || p.w != want.w || p.h != want.h || p.hidden != want.hidden)
            std::printf("  record %u: %u,%u %ux%u%s, want %u,%u %ux%u%s\n", h, p.col, p.row, p.w, p.h, p.hidden ? " hidden" : "",
                        want.col, want.row, want.w, want.h, want.hidden ? " hidden" : "");
        CHECK(p.col == want.col && p.row == want.row && p.w == want.w && p.h == want.h && p.hidden == want.hidden);
    }

    /// uitest.lbl's address for `name`.
    uint16_t lab(const char *name)
    {
        static const std::map<std::string, uint16_t> l = labels(UTILS + "/uitest.lbl");
        return l.at(name);
    }
    /// Record h's address in uitest.s's table, `main`.
    uint16_t rec(uint8_t h) { return static_cast<uint16_t>(lab("main") + h * 16u); }
    void poke_flags(Rig &r, uint8_t h, uint8_t set) { r.poke(rec(h) + UW_FLAGS, static_cast<uint8_t>(r.phys(rec(h) + UW_FLAGS) | set)); }
    /// ui_flags: Y the handle, A to set, X to clear.
    std::string flags(CcpRig &r, uint8_t h, uint8_t set, uint8_t clear = 0) { return ui(r, 'F', set, clear, h); }
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
    const std::map<std::string, uint16_t> l = labels(UTILS + "/uitest.lbl");
    const uint16_t main = l.at("main");
    const uint8_t at = r.phys(main + B_OK * 16u + 1);
    r.poke(main + B_OK * 16u + 1, static_cast<uint8_t>(B_CAN)); // a parent after its child
    const auto refused = [&r] { const std::string a = ui(r, 'o', BLK_MAIN); return a.substr(0, 2) + a.substr(6); };
    CHECK_EQ(refused(), std::string("0401")); // UX_TABLE
    r.poke(main + B_OK * 16u + 1, at);
    r.poke(main + I_CUT * 16u + 1, static_cast<uint8_t>(W_DESK)); // an item outside a menu
    CHECK_EQ(refused(), std::string("0401"));
    r.poke(main + I_CUT * 16u + 1, static_cast<uint8_t>(M_EDIT));
    r.poke(main + L_SIDE * 16u, 15); // a list: U2's
    CHECK_EQ(refused(), std::string("0401"));
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
    expect(poll(r), {}, "F1 for a hidden item");
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
    expect(poll(r), {}, "a modal window shown");
    CHECK_EQ(vid(r).peek(0x19), 4); // the base dimmed (TF_DIM)
    golden(r, "dim-modal");
    CHECK_EQ(ui(r, 'h', W_OVER).substr(6), std::string("00"));
    expect(poll(r), {}, "hidden");
    CHECK_EQ(vid(r).peek(0x19), 2);
    CHECK_EQ(frame_hash(r), house);
}

int main()
{
    const int failed = run_all();
    if (updating())
    {
        std::printf("\n*** OOUI_GOLDEN=update: golden/ooui.txt was WRITTEN, nothing was compared. ***\n"
                    "*** Look at the frames (OOUI_DUMP=dir), then run again without it.         ***\n\n");
        return 77; // CTest's SKIP_RETURN_CODE: an update run never reads as a pass (the review's O18)
    }
    return failed;
}
