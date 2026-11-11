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
        P_OPTS, C_SHAD, C_MOUS, L_RIGHT, L_DIS, B_OK, B_CAN, W_SIDE, B_TOG, L_SIDE, W_OVER, L_OVER, B_HIDE
    };
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
    CHECK_EQ(ui(r, 'd', 27).substr(0, 2) + ui(r, 'd', 27).substr(6), std::string("0501")); // UX_HANDLE
    CHECK_EQ(ui(r, 'f', L_HEAD).substr(6), std::string("01"));                             // a label takes no focus
    CHECK_EQ(ui(r, 'f', B_HIDE).substr(6), std::string("01"));                             // nor a hidden button
    CHECK_EQ(ui(r, 'f', B_CAN).substr(6), std::string("00"));
    CHECK_EQ(ui(r, 'g').substr(0, 2), std::string("14"));
}


int main() { return run_all(); }
