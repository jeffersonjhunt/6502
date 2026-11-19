/**
 * @file test_gwbasic_screen.cpp
 * @brief GW-BASIC's own text screen, phases 7d1 and 7d2 (hw/oodos.md, "The plan, phase 7": block P6; 7d2's function
 *        keys, their line, WIDTH "SCRN:", SCRN: and LCOPY), run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_core does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M:, and M:GWBASIC is typed at the prompt on the serial line. Here CON:'s output keeps the CRT, as the cold boot
 * leaves it, so GW-BASIC takes its own screen (the owner's answer 2 of 2026-11-18): its text is read from the video
 * card's memory at layer 0's MAPBASE, a character and an attribute a cell, and its keys are pressed on the USB card,
 * as a user presses them (the suite's own helper adds Shift for the punctuation, which type_usb lacks).
 *
 * Every case is written by hand from the source's meaning (SCNDRV.ASM, SCNEDT.ASM, GWSTS.ASM, GIOKYB.ASM) and the
 * owner's answers; PC-BASIC's transcripts aren't GW-BASIC's screen, so none is compared with them. Each case says
 * the failure on real hardware it guards and the broken build it was seen failing on (hw/oodos.md, 7d1 and 7d2 as
 * built). From 7d2 the key line is on at start-up (the owner's answer 1), on the last row.
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

    /// A link's labels (ld65's -Ln): the resident core's (gwbasic.lbl) or a block's (p6.lbl).
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

    /// The key line at 80 columns with the PC's ten texts (7d2): each key's number, its first 6 characters (a CR
    /// shown as 27), a blank; the trailing blank dropped, as line() drops it.
    const std::string KEYLINE = "1LIST   2RUN\x1b   3LOAD\"  4SAVE\"  5CONT\x1b  6,\"LPT1 7TRON\x1b  8TROFF\x1b 9KEY    0SCREEN";

    constexpr uint16_t L0_CONFIG = 0xC490, L0_MAP = 0xC491, L0_TILE = 0xC493, L0_VSCROLL = 0xC497;

    uint8_t reg(CcpRig &r, uint16_t a) { return r.machine->bus().peek(a); }
    /// Layer 0's map's card address (MAPBASE x 256).
    uint32_t mapbase(CcpRig &r) { return (reg(r, L0_MAP) | reg(r, L0_MAP + 1) << 8) * 256u; }
    /// The text's columns: 40 for CONFIG's type 1, else 80.
    unsigned columns(CcpRig &r) { return (reg(r, L0_CONFIG) & 7) == 1 ? 40 : 80; }
    uint8_t card(CcpRig &r, uint32_t a) { return r.machine->video_card()->card_peek(a); }

    /// Row `row` (from 0) of the text at card address `base` (the layer's by default), `cols` cells: its characters.
    std::string row_at(CcpRig &r, unsigned row, uint32_t base = 0xFFFFFFFF, unsigned cols = 0)
    {
        if (base == 0xFFFFFFFF)
            base = mapbase(r);
        if (cols == 0)
            cols = columns(r);
        std::string s;
        for (unsigned c = 0; c < cols; ++c)
            s += static_cast<char>(card(r, base + row * cols * 2 + c * 2));
        return s;
    }
    /// The same row with its trailing blanks dropped; the $FF a message leaves (the "not typed" mark, SCNDRV.ASM's
    /// CTLNMK, a blank in the PC's font and the console's) as a blank.
    std::string line(CcpRig &r, unsigned row)
    {
        std::string s = row_at(r, row);
        for (char &c : s)
            if (static_cast<unsigned char>(c) == 0xFF)
                c = ' ';
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    }
    /// The cell's attribute.
    uint8_t attr(CcpRig &r, unsigned row, unsigned col)
    {
        return card(r, mapbase(r) + row * columns(r) * 2 + col * 2 + 1);
    }
    /// The first 25 rows, each its trailing blanks dropped, joined by '|': for a failure's message.
    std::string screen(CcpRig &r, unsigned rows = 25)
    {
        std::string s;
        for (unsigned i = 0; i < rows; ++i)
            s += line(r, i) + "|";
        return s;
    }
    /// Run until some row of the screen holds `text`. @return its row, or -1
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

    constexpr uint8_t SHIFT = 0xE1, CTRL = 0xE0;
    /// A key pressed and let go on the USB keyboard, with a modifier held round it (0 none).
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
    /// Text typed on the USB keyboard, a key at a time, Shift added where the character needs it.
    void keys(CcpRig &r, const std::string &text)
    {
        for (const char ch : text)
        {
            const auto [u, shift] = usage(ch);
            REQUIRE(u != 0);
            press(r, u, shift ? SHIFT : 0);
        }
    }
    /// A line typed and Enter: then run until the screen settles (GW-BASIC waits for the next key).
    void enter(CcpRig &r, const std::string &text, uint64_t settle = ms(300))
    {
        keys(r, text + "\r");
        r.run(settle);
    }

    /// A CcpRig with GW-BASIC's two files on M:, CON: as the cold boot leaves it (CRT and TTY), and GW-BASIC
    /// started on its own screen: until its first Ok. @return the row of that Ok, or -1
    int start(CcpRig &r, const std::function<void()> &echoed = {})
    {
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        CHECK(!com.empty() && !ovl.empty());
        if (com.empty() || ovl.empty())
            return -1;
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}});
        const size_t from = r.out.size();
        r.type("m:gwbasic\r");
        CHECK(r.until("m:gwbasic\r\n", ms(2000), from));
        if (echoed)
            echoed();
        const int ok = until_row(r, "Ok");
        r.run(ms(100));
        return ok;
    }
} // namespace

// The banner and Ok are on GW-BASIC's own layer, not on the serial line and not on the console's screen: layer 0's
// MAPBASE in the program's card memory ($040000 up), CONFIG text 80 x 30 with BLINK, TILEBASE the console's font
// ($0004: the owner's answer 5), 25 rows centred (VSCROLL 440, answer 10). Guards: GW-BASIC's output landing on the
// console's hidden screen. Seen failing with OUTDO's screen branch left out (function 2 in screen mode).
TEST(the_banner_and_ok_are_on_gw_basics_own_layer)
{
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<uint8_t> console(80 * 30 * 2);
    const size_t from = r.out.size();
    const int ok = start(r, [&] {
        for (size_t i = 0; i < console.size(); ++i) // once the CCP has echoed the command
            console[i] = card(r, 0x2000 + static_cast<uint32_t>(i));
    });
    CHECK(ok > 0);
    CHECK(mapbase(r) >= 0x040000);
    CHECK_EQ(reg(r, L0_CONFIG), 0x80);
    CHECK_EQ(reg(r, L0_TILE) | reg(r, L0_TILE + 1) << 8, 0x0004);
    CHECK_EQ(reg(r, L0_VSCROLL) | reg(r, L0_VSCROLL + 1) << 8, 440);
    CHECK_EQ(line(r, 0), std::string("GW-BASIC for the OO-1"));
    CHECK_EQ(line(r, 1), std::string("(C) Copyright Microsoft 1982"));
    CHECK(line(r, 2).find(" Bytes free") != std::string::npos);
    CHECK_EQ(line(r, 3), std::string("Ok"));
    CHECK_EQ(ok, 3);
    CHECK_EQ(attr(r, 0, 0), 0x07);
    // Nothing on the serial line after the command's echo, and the console's text as it was before.
    const std::string tty = r.out.substr(from);
    CHECK(tty.find("Ok") == std::string::npos && tty.find("GW-BASIC") == std::string::npos);
    size_t changed = 0;
    for (size_t i = 0; i < console.size(); ++i)
        changed += card(r, 0x2000 + static_cast<uint32_t>(i)) != console[i];
    CHECK_EQ(changed, 0u);
    if (changed || ok != 3)
        std::printf("  the screen: %s\n", screen(r).c_str());
}

    /// RUN's cycles, from the RUN statement to the end's ENDCON (check.py --timing's method), the program
    /// `lines` typed first (after NEW).
    uint64_t timed(CcpRig &r, const std::vector<std::string> &lines)
    {
        const std::map<std::string, uint16_t> lab = lbl_map();
        enter(r, "PRINT CHR$(12);"); // a clear screen first: the editor reads back what a line is typed over
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
        const uint64_t end = r.cpu->cycles() + ms(20000);
        while (cycles == 0 && r.cpu->cycles() < end)
            r.run(ms(10));
        r.watch = nullptr;
        r.run(ms(200));
        CHECK(cycles > 0);
        return cycles;
    }

// The keys come from the event queue alone, once each and in order (V2's must-fail): "1", "2" and "3" pressed
// within one frame, while a program reads INKEY$, come back as "123"; and a program line typed on the USB keyboard
// runs. Guards doubled or reordered keys. Seen failing with (a) the cooked keys read as well in screen mode
// (POLKEY's console path left on: each key twice) and (b) the queue taken from its head (newest first).
TEST(keys_come_from_the_queue_once_each)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "10 K$=\"\":FOR I=1 TO 400:K$=K$+INKEY$:NEXT:PRINT CHR$(91);K$;CHR$(93)");
    keys(r, "RUN\r");
    r.run(ms(100));
    UsbCard *usb = r.machine->usb_card();
    for (const uint8_t u : {uint8_t{0x1E}, uint8_t{0x1F}, uint8_t{0x20}}) // 1 2 3, all inside 15 ms
    {
        usb->key(u, true);
        r.run(ms(2));
        usb->key(u, false);
        r.run(ms(3));
    }
    const int at = until_row(r, "[", ms(8000));
    CHECK(at > 0);
    if (at > 0)
        CHECK_EQ(line(r, static_cast<unsigned>(at)), std::string("[123]"));
    enter(r, "20 PRINT 6*7");
    enter(r, "RUN 20", ms(600));
    CHECK(until_row(r, " 42") > 0);
}

// Keys typed in BASIC, then SYSTEM: the CCP's next line is empty, and the console's screen is back (W1's
// must-fail). Guards the session's typing replayed into the CCP from the cooked keys. Seen failing with SYSTEM's
// drains and KEYEV's left out.
TEST(system_leaves_the_ccp_line_empty)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "PRINT 12345");
    CHECK(until_row(r, " 12345") > 0);
    const size_t from = r.out.size();
    keys(r, "SYSTEM\r");
    CHECK(r.until("A>", ms(4000), from));
    r.run(ms(800));
    const size_t prompt = r.out.find("A>", from);
    const std::string after = prompt == std::string::npos ? "?" : r.out.substr(prompt + 2);
    CHECK_EQ(vis(after), std::string());
    CHECK_EQ(mapbase(r), 0x002000u);
}

// 40 numbered PRINTs scroll the window (24 lines) and keep every line: the numbers in order, one a row, up to
// 40, then Ok, then the cursor's blank line at the window's foot, and the key line as it was. Guards lines lost or
// doubled at the bottom while output scrolls. Seen failing with the row cache not written back before SCROLL's
// DM_VCOPY.
TEST(a_scroll_keeps_every_line)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "FOR I=1 TO 40:PRINT I:NEXT", ms(1500));
    const int ok = until_row(r, "Ok");
    CHECK_EQ(ok, 22);
    CHECK_EQ(line(r, 21), std::string(" 40"));
    for (int row = 21, n = 40; row >= 0; --row, --n)
        CHECK_EQ(line(r, static_cast<unsigned>(row)), " " + std::to_string(n));
    CHECK_EQ(line(r, 23), std::string());
    CHECK_EQ(line(r, 24), KEYLINE);
    CHECK_EQ(row_at(r, 24), KEYLINE + std::string(80 - KEYLINE.size(), ' '));
    if (ok != 22)
        std::printf("  the screen: %s\n", screen(r).c_str());
}

// Ctrl-C on the USB keyboard breaks a running program: "Break in 10" and Ok on the screen, and GW-BASIC still
// runs (no warm boot). Guards a program that can't be stopped short of Break. Seen failing with KEYEV leaving
// Ctrl-C (usage $06 with Ctrl) to the cooked path.
TEST(ctrl_c_breaks_a_running_program)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const size_t since = r.out.size();
    enter(r, "10 GOTO 10");
    keys(r, "RUN\r");
    r.run(ms(300));
    press(r, 0x06, CTRL);
    const int at = until_row(r, "Break in 10");
    CHECK(at > 0);
    if (at > 0)
        CHECK_EQ(line(r, static_cast<unsigned>(at + 1)), std::string("Ok"));
    enter(r, "PRINT 5");
    if (at > 0)
        CHECK_EQ(line(r, static_cast<unsigned>(at + 3)), std::string(" 5"));
    CHECK(r.out.find("A>", since) == std::string::npos);
}

// A typed line is read back from the cells (SCNRDL): PRINT "AX, a Backspace, B" and Enter runs PRINT "AB" as the
// screen shows it. Guards the line run not being the line shown. Seen failing with SCNRDL starting a column right
// of the logical line's start.
TEST(a_typed_line_is_read_back_from_the_cells)
{
    CcpRig r;
    REQUIRE(r.ok);
    const int ok = start(r);
    REQUIRE(ok > 0);
    keys(r, "PRINT \"AX");
    press(r, 0x2A); // Backspace
    keys(r, "B\"\r");
    r.run(ms(300));
    CHECK_EQ(line(r, static_cast<unsigned>(ok + 1)), std::string("PRINT \"AB\""));
    CHECK_EQ(line(r, static_cast<unsigned>(ok + 2)), std::string("AB"));
    CHECK_EQ(line(r, static_cast<unsigned>(ok + 3)), std::string("Ok"));
}

constexpr uint8_t K_UP = 0x52, K_DOWN = 0x51, K_LEFT = 0x50, K_RIGHT = 0x4F, K_HOME = 0x4A, K_END = 0x4D,
                  K_INS = 0x49, K_DEL = 0x4C, K_F1 = 0x3A;
/// The last row holding `text` (from the bottom of the window up), or -1.
int last_row(CcpRig &r, const std::string &text)
{
    for (int i = 24; i >= 0; --i)
        if (line(r, static_cast<unsigned>(i)).find(text) != std::string::npos)
            return i;
    return -1;
}

// The full-screen editor changes a listed line in place: LIST, Up to line 20, Right to its string, overtyped,
// Insert and more text, Delete, Enter: LIST and RUN show the changed line, line 10 as it was. Guards editing a line
// entering something else. Seen failing with insert's move (MVCHLN) taking its characters from a column right of
// the cursor. (The plan's mutant, the row cache kept across a return from P6, is how this build works, written back
// at every return; a scroll's copy that left the cache on its row isn't caught either, since the cursor's moves
// reload the rows it passes. So this case's mutant is the insert's.)
TEST(the_editor_changes_a_listed_line_in_place)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "10 PRINT \"ONE\"");
    enter(r, "20 PRINT \"TWO\"");
    enter(r, "30 PRINT \"SIX\"");
    for (int i = 0; i < 12; ++i) // enough lines to scroll while listing
        enter(r, "PRINT");
    enter(r, "LIST", ms(600));
    const int r20 = last_row(r, "20 PRINT \"TWO\""), ok = last_row(r, "Ok");
    REQUIRE(r20 > 0 && ok > r20);
    for (int i = 0; i < ok + 1 - r20; ++i)
        press(r, K_UP);
    for (int i = 0; i < 10; ++i)
        press(r, K_RIGHT);
    keys(r, "SE");
    press(r, K_INS);
    keys(r, "VEN");
    press(r, K_DEL);
    keys(r, "\r");
    r.run(ms(300));
    CHECK_EQ(line(r, static_cast<unsigned>(r20)), std::string("20 PRINT \"SEVEN\""));
    press(r, 0x0F, CTRL); // ^L: the screen cleared (Enter left the cursor on line 30's row)
    enter(r, "LIST", ms(600));
    CHECK(last_row(r, "10 PRINT \"ONE\"") > 0);
    CHECK(last_row(r, "20 PRINT \"SEVEN\"") > 0);
    CHECK(last_row(r, "30 PRINT \"SIX\"") > 0);
    enter(r, "RUN", ms(600));
    const int seven = last_row(r, "SEVEN");
    CHECK(seven > 0 && line(r, static_cast<unsigned>(seven)) == "SEVEN" &&
          line(r, static_cast<unsigned>(seven - 1)) == "ONE");
    if (seven < 0 || line(r, static_cast<unsigned>(seven - 1)) != "ONE")
        std::printf("  the screen: %s\n", screen(r).c_str());
}

// A logical line spans two rows: a line of 99 characters typed wraps at the 80th, and Enter with the cursor on
// its second row enters the whole of it. Guards long lines cut at 80. Seen failing with CHWRAP's wrap terminator
// (TRMWRP) written as EOL.
TEST(a_logical_line_spans_two_rows)
{
    CcpRig r;
    REQUIRE(r.ok);
    const int ok = start(r);
    REQUIRE(ok > 0);
    keys(r, "10 A$=\"" + std::string(90, 'B') + "\"");
    CHECK_EQ(line(r, static_cast<unsigned>(ok + 2)), std::string(17, 'B') + "\"");
    keys(r, "\r");
    r.run(ms(300));
    enter(r, "RUN");
    enter(r, "PRINT LEN(A$);RIGHT$(A$,3)");
    CHECK(until_row(r, " 90 BBB") > 0);
}

// INKEY$ gives the PC's two-byte codes for the keys with no character: Up CHR$(0)+"H" (72), F1 CHR$(0)+";" (59),
// Home CHR$(0)+"G" (71), Del CHR$(0)+"S" (83); and a key held repeats, after 500 ms and then 30 a second (the owner's
// answer 8). Guards games that read the arrows not moving. Seen failing with the navigation keys' table one entry
// off.
TEST(inkey_gives_the_pcs_two_byte_codes)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "KEY 1,\"\""); // 7d2: F1's soft key empty, so it gives the PC's code (a PC program does the same)
    enter(r, "10 A$=INKEY$:IF A$=\"\" THEN 10");
    enter(r, "20 PRINT CHR$(91);LEN(A$);ASC(A$);ASC(RIGHT$(A$,1)):GOTO 10");
    keys(r, "RUN\r");
    r.run(ms(200));
    const std::vector<std::pair<uint8_t, std::string>> want = {
        {K_UP, "[ 2  0  72"}, {K_F1, "[ 2  0  59"}, {K_HOME, "[ 2  0  71"}, {K_DEL, "[ 2  0  83"}};
    for (const auto &[k, text] : want)
    {
        press(r, k);
        r.run(ms(150));
        CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "[ "))), text);
    }
    press(r, 0x06, CTRL);
    r.run(ms(200));
    enter(r, "30 K$=\"\":FOR I=1 TO 6000:K$=K$+INKEY$:NEXT:PRINT CHR$(93);LEN(K$)");
    keys(r, "RUN 30\r");
    r.run(ms(50));
    press(r, 0x04, 0, 1000); // A, held a second
    const int at = until_row(r, "] ", ms(20000));
    REQUIRE(at > 0);
    const long n = std::strtol(line(r, static_cast<unsigned>(at)).c_str() + 1, nullptr, 10);
    std::printf("  A held 1 s: %ld characters (1, then one each 33 ms from 500 ms)\n", n);
    CHECK(n >= 10 && n <= 20);
}

// EDIT and AUTO on the screen: EDIT 10 shows the line with the cursor at its start, and Enter keeps it; AUTO offers
// "10 ", takes two lines, offers "30 ", and Ctrl-C ends it. Guards EDIT and AUTO hanging or losing the line. Seen
// failing with the preload sent to function 10 in screen mode.
TEST(auto_and_edit_on_the_screen)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "10 PRINT 1+1");
    enter(r, "EDIT 10");
    const int e = last_row(r, "10 PRINT 1+1");
    REQUIRE(e > 0);
    CHECK_EQ(line(r, static_cast<unsigned>(e - 1)), std::string("EDIT 10"));
    CHECK_EQ(attr(r, static_cast<unsigned>(e), 0) & 0x80, 0x80); // the cursor, at the line's start
    keys(r, "\r");
    r.run(ms(300));
    enter(r, "AUTO");
    CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "10*"))), std::string("10*"));
    keys(r, "\r");
    r.run(ms(300));
    CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "20"))), std::string("20"));
    enter(r, "PRINT 2");
    CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "30"))), std::string("30"));
    press(r, 0x06, CTRL);
    r.run(ms(300));
    enter(r, "LIST", ms(600));
    CHECK(last_row(r, "10 PRINT 1+1") > 0);
    CHECK(last_row(r, "20 PRINT 2") > 0);
    CHECK(last_row(r, "30 ") < last_row(r, "20 PRINT 2"));
}

// LOCATE, CSRLIN and POS: LOCATE 5,10:PRINT "X"; puts X at cell (4,9), and then CSRLIN is 5 and POS(0) 11; after
// PRINT STRING$(80,"A"); CSRLIN is the next line (GTLINE: the next character goes there); LOCATE 0, LOCATE 26 and
// LOCATE ,81 are Illegal function call, LOCATE 25 is allowed (the key line is off). Guards a program's screen layout
// one row off. Seen failing with GTLINE's test of the cursor past the line's end made with BCC alone (the 8086's
// JBE made JB).
TEST(locate_csrlin_and_pos)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:LOCATE 5,10:PRINT \"X\";:A=CSRLIN:B=POS(0):LOCATE 12,1:PRINT A;B", ms(400));
    CHECK_EQ(static_cast<char>(card(r, mapbase(r) + 4 * 160 + 9 * 2)), 'X');
    CHECK_EQ(line(r, 11), std::string(" 5  11"));
    enter(r, "CLS:LOCATE 3,1:PRINT STRING$(80,\"A\");:A=CSRLIN:LOCATE 12,1:PRINT A", ms(400));
    CHECK_EQ(line(r, 11), std::string(" 4"));
    enter(r, "CLS:LOCATE 3,1:PRINT STRING$(79,\"A\");:A=CSRLIN:LOCATE 12,1:PRINT A", ms(400));
    CHECK_EQ(line(r, 11), std::string(" 3"));
    enter(r, "CLS:LOCATE 0", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
    enter(r, "CLS:LOCATE 26", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
    enter(r, "CLS:LOCATE ,81", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
    enter(r, "KEY OFF:CLS:LOCATE 25,1:PRINT \"Z\";:LOCATE 10,1", ms(300));
    CHECK_EQ(line(r, 24), std::string("Z"));
    CHECK(last_row(r, "Illegal") < 0);
}

// COLOR and CLS: COLOR 14,1 writes cells $1E; COLOR 30,1 $9E, blinking; COLOR ,,4 sets the border to 4; COLOR 32
// is Illegal function call; CLS blanks the 24 rows in the current attribute, draws the key line again and homes the
// cursor. Guards
// wrong colours. Seen failing with the background put in bits 3-0.
TEST(color_and_cls)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:COLOR 14,1:PRINT \"Y\";:COLOR 30,1:PRINT \"B\";:COLOR 7,0", ms(300));
    CHECK_EQ(attr(r, 0, 0), 0x1E);
    CHECK_EQ(attr(r, 0, 1), 0x9E);
    enter(r, "COLOR ,,4");
    CHECK_EQ(reg(r, 0xC481), 4);
    enter(r, "COLOR 32", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
    enter(r, "COLOR 7,2:CLS", ms(300));
    bool all = true;
    for (unsigned row = 1; row < 24; ++row)
        for (unsigned c = row == 1 ? 1 : 0; c < 80; ++c) // (the cursor waits at row 1's first cell)
            all = all && attr(r, row, c) == 0x27 && card(r, mapbase(r) + row * 160 + c * 2) == ' ';
    CHECK(all);
    CHECK_EQ(line(r, 24), KEYLINE); // the key line: its numbers in $27, its texts reversed, $72
    for (unsigned c = 0; c < 80; ++c)
        all = all && attr(r, 24, c) == (c % 8 == 0 || c % 8 == 7 ? 0x27 : 0x72);
    CHECK(all);
    CHECK_EQ(line(r, 0), std::string("Ok"));
}

// WIDTH 40 and 80, and 30 rows: WIDTH 40 makes the layer 40 columns (CONFIG's type 1), the line 40 wide and the
// text 80 bytes a row; WIDTH 80,30 has all 30 rows (decision 8's OO-1 extension; VSCROLL 0), so LOCATE 30,1 works;
// WIDTH 50 is Illegal function call. Guards 40-column text scattered across the page. Seen failing with the row
// stride left at 160 after WIDTH 40.
TEST(width_40_80_and_30_rows)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "WIDTH 40", ms(400));
    CHECK_EQ(reg(r, L0_CONFIG), 0x81);
    enter(r, "PRINT \"FORTY\":PRINT POS(0)", ms(300));
    CHECK_EQ(row_at(r, 2, mapbase(r), 40).substr(0, 5), std::string("FORTY"));
    CHECK_EQ(row_at(r, 3, mapbase(r), 40).substr(0, 3), std::string(" 1 "));
    enter(r, "LOCATE 5,1:PRINT STRING$(45,\"W\");", ms(300));
    CHECK_EQ(row_at(r, 4, mapbase(r), 40), std::string(40, 'W'));
    CHECK_EQ(row_at(r, 5, mapbase(r), 40).substr(0, 5), std::string("WWWWW"));
    enter(r, "WIDTH 80,30", ms(400));
    CHECK_EQ(reg(r, L0_CONFIG), 0x80);
    CHECK_EQ(reg(r, L0_VSCROLL) | reg(r, L0_VSCROLL + 1) << 8, 0);
    enter(r, "KEY OFF:LOCATE 30,1:PRINT \"THIRTY\";:LOCATE 10,1", ms(300));
    CHECK_EQ(line(r, 29), std::string("THIRTY"));
    enter(r, "WIDTH 50", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
}

// WIDTH "SCRN:",n is the screen's WIDTH n with the rows kept (GIOSCN.ASM's SCNSWD; 7d2): WIDTH "SCRN:",40 makes the
// layer 40 columns (CONFIG $81) and the line 40 wide; WIDTH "SCRN:",255 (the source's infinite width) changes
// nothing; WIDTH "SCRN:",50 is Illegal function call; WIDTH "SCRN:",80 puts 80 back. Guards WIDTH "SCRN:" a no-op,
// or a garbled screen. Seen failing with SCNSWD returning C set, as the printers' do (the home block then never
// calls SWIDP).
TEST(width_scrn_sets_the_screen)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "WIDTH \"SCRN:\",40", ms(400));
    CHECK_EQ(reg(r, L0_CONFIG), 0x81);
    enter(r, "PRINT STRING$(45,\"S\")", ms(300));
    CHECK_EQ(row_at(r, 2, mapbase(r), 40), std::string(40, 'S'));
    CHECK_EQ(row_at(r, 3, mapbase(r), 40).substr(0, 6), std::string("SSSSS "));
    enter(r, "WIDTH \"SCRN:\",255:PRINT \"SAME\"", ms(400));
    CHECK_EQ(reg(r, L0_CONFIG), 0x81);
    CHECK(last_row(r, "SAME") > 0);
    enter(r, "WIDTH \"SCRN:\",50", ms(300));
    CHECK(last_row(r, "Illegal function call") >= 0);
    CHECK_EQ(reg(r, L0_CONFIG), 0x81);
    enter(r, "WIDTH \"SCRN:\",80", ms(400));
    CHECK_EQ(reg(r, L0_CONFIG), 0x80);
    enter(r, "PRINT \"EIGHTY\"", ms(300));
    CHECK_EQ(line(r, 2), std::string("EIGHTY"));
}

namespace
{
    /// What LST: is sent (BDOS function 5, watched at the BDOS's entry) while `line` is typed and run.
    std::string lst_of(CcpRig &r, const std::string &line, uint64_t settle = ms(1500))
    {
        std::string got;
        r.watch = [&](uint16_t pc) {
            const Registers g = r.cpu->regs();
            if (pc == 0x0203 && g.y == 5)
                got += static_cast<char>(g.a);
        };
        enter(r, line, settle);
        r.watch = nullptr;
        return got;
    }
    /// Rows 0 to `rows` - 1 of the text at `base` as LCOPY prints them: a character below 32 or $FF a blank, the
    /// trailing blanks dropped, a CR and an LF after each.
    std::string as_lcopy(CcpRig &r, uint32_t base, unsigned rows, int blank_row = -1)
    {
        std::string out;
        for (unsigned i = 0; i < rows; ++i)
        {
            std::string s = static_cast<int>(i) == blank_row ? std::string() : row_at(r, i, base);
            for (char &c : s)
                if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 0xFF)
                    c = ' ';
            while (!s.empty() && s.back() == ' ')
                s.pop_back();
            out += s + "\r\n";
        }
        return out;
    }
} // namespace

// LCOPY prints the screen on LST: (GWSTS.ASM's LCPY, the OEM's: the owner's answer 9): every one of the page's 25
// rows, each as the card holds it with its trailing blanks dropped and a CR LF after it, what the statement before it
// printed ("ABC", in the same line) among them; LCOPY's n is taken and not used. With page 1 active and page 0 shown,
// it prints page 0, what is on the screen. In console mode it stays Advanced Feature (test_gwbasic_files). Guards a
// printout missing what was just printed, or the wrong page's. Seen failing with LCPYP reading the active page (and
// not failing with its TXBUF run and flush left out: FINPRT runs TXBUF and every return from P6 flushes the cache,
// so nothing waits when LCOPY starts; both are kept, as at every entry of P6's).
TEST(lcopy_prints_the_screen_on_lst)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const std::string got = lst_of(r, "CLS:LOCATE 5,3:PRINT \"ONE\":LOCATE 12,40:PRINT \"TWO\":LOCATE 20,1:PRINT \"ABC\";:LCOPY 1");
    const int ok = last_row(r, "Ok");
    CHECK_EQ(ok, 20);
    CHECK_EQ(line(r, 19), std::string("ABC"));
    const std::string want = as_lcopy(r, mapbase(r), 25, ok); // the Ok came after the copy
    CHECK_EQ(vis(got), vis(want));
    size_t lines = 0;
    for (size_t p = got.find("\r\n"); p != std::string::npos; p = got.find("\r\n", p + 2))
        ++lines;
    CHECK_EQ(lines, 25u);
    CHECK(got.find("  ONE\r\n") != std::string::npos);
    CHECK(got.find(std::string(39, ' ') + "TWO\r\n") != std::string::npos);
    CHECK(got.find("\r\nABC\r\n") != std::string::npos);
    enter(r, "CLS:PRINT \"VISUAL\"", ms(300));
    const uint32_t page0 = mapbase(r);
    const std::string shown = lst_of(r, "SCREEN 0,,1,0:CLS:PRINT \"ACT\"+\"IVE\":LCOPY"); // ("ACTIVE" typed would be on page 0)
    CHECK_EQ(mapbase(r), page0);
    CHECK_EQ(vis(shown), vis(as_lcopy(r, page0, 25)));
    CHECK(shown.find("VISUAL") != std::string::npos);
    CHECK(shown.find("ACTIVE") == std::string::npos);
    enter(r, "SCREEN 0,,0,0", ms(300));
}

// SCRN: is GW-BASIC's own screen in screen mode (GIOSCN.ASM's SCNSOT, SCNOL1, SCNOL2; 7c's device, 7d1's screen):
// a file OPENed on "SCRN:" prints on the layer, a CR as a new line, a comma to the next zone, and nothing goes to the
// serial line; at 40 columns a 45-character PRINT #1 wraps at 40. Guards SCRN: output on the serial line, or
// unwrapped, under screen mode. Seen failing with P4's SCNOUT sending by function 2, not OUTDO.
TEST(scrn_writes_on_the_screen)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const size_t from = r.out.size();
    enter(r, "CLS:OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,\"HELLO\";CHR$(13);\"AB\":PRINT #1,1,2:CLOSE", ms(600));
    CHECK_EQ(line(r, 0), std::string("HELLO"));
    CHECK_EQ(line(r, 1), std::string("AB"));
    CHECK_EQ(line(r, 2), std::string(" 1             2"));
    CHECK_EQ(line(r, 3), std::string("Ok"));
    CHECK_EQ(vis(r.out.substr(from)), std::string());
    enter(r, "WIDTH 40", ms(400));
    enter(r, "CLS:OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,STRING$(45,\"Z\"):CLOSE", ms(600));
    CHECK_EQ(row_at(r, 0, mapbase(r), 40), std::string(40, 'Z'));
    CHECK_EQ(line(r, 1), std::string("ZZZZZ"));
    CHECK_EQ(line(r, 2), std::string("Ok"));
    CHECK_EQ(vis(r.out.substr(from)), std::string());
}

// The key line at start-up (GWINIT.ASM's PRNTND and SKEYON; 7d2, the owner's answer 1): in screen mode, with no
// program named, the last row is the key line, each key's number in the attribute ($07), its text reversed ($70);
// so LOCATE 25 is Illegal function call (LOCATE's KEYSW). KEY OFF blanks the row and LOCATE 25 then works; KEY ON
// draws it again. With a program named on the command line (m:gwbasic m:p) there is no key line, as the source has
// it. Guards the line missing, unreadable, or over a program's last row. Seen failing with (a) PRNTND's test of BUF
// left out (the line on with a program named) and (b) XFGBG not swapping (the text $07).
TEST(the_key_line_at_start_up)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r) > 0);
        CHECK_EQ(line(r, 24), KEYLINE);
        bool rev = true;
        for (unsigned c = 0; c < 80; ++c)
            rev = rev && attr(r, 24, c) == (c % 8 == 0 || c % 8 == 7 ? 0x07 : 0x70);
        CHECK(rev);
        enter(r, "CLS:LOCATE 25,1", ms(300));
        CHECK(last_row(r, "Illegal function call") >= 0);
        CHECK_EQ(line(r, 24), KEYLINE);
        enter(r, "KEY OFF", ms(300));
        CHECK_EQ(row_at(r, 24), std::string(80, ' '));
        enter(r, "CLS:LOCATE 25,1:PRINT \"LAST\";:LOCATE 5,1", ms(300));
        CHECK_EQ(line(r, 24), std::string("LAST"));
        CHECK(last_row(r, "Illegal") < 0);
        enter(r, "KEY ON", ms(300));
        CHECK_EQ(line(r, 24), KEYLINE);
    }
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
    const std::string prog = "10 PRINT \"NAMED\"\r\n\x1a";
    r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}, {0, "P       BAS", {prog.begin(), prog.end()}}});
    r.type("m:gwbasic m:p\r");
    CHECK(until_row(r, "NAMED") >= 0);
    CHECK(until_row(r, "Ok") >= 0);
    r.run(ms(200));
    CHECK_EQ(row_at(r, 24), std::string(80, ' '));
}

// KEY n,"text" (GWSTS.ASM's KEYS): KEY 1,"FILES"+CHR$(13) shows field 1 as "1FILES" and 27 (the CR), and key 2
// unchanged; KEY 2,STRING$(20,"X") keeps 15 (KEY LIST shows them); KEY 0 and KEY 11 are Illegal function call,
// KEY 1,5 Type mismatch, KEY(1) ON Advanced Feature (7f's, the owner's answer 5). Guards a key's text landing on
// another key. Seen failing with STRTAB's place one key on (KEYSP's DEC A left out).
TEST(key_n_sets_a_key_and_its_field)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "KEY 1,\"FILES\"+CHR$(13)", ms(300));
    CHECK_EQ(line(r, 24).substr(0, 16), std::string("1FILES\x1b 2RUN\x1b   "));
    enter(r, "KEY 2,STRING$(20,\"X\"):CLS:KEY LIST", ms(600));
    CHECK_EQ(line(r, 0), std::string("F1 FILES\x1b"));
    CHECK_EQ(line(r, 1), "F2 " + std::string(15, 'X'));
    CHECK_EQ(line(r, 24).substr(0, 16), std::string("1FILES\x1b 2XXXXXX "));
    for (const char *bad : {"KEY 0,\"A\"", "KEY 11,\"A\""})
    {
        enter(r, std::string("CLS:") + bad, ms(300));
        CHECK_EQ(line(r, 0), std::string("Illegal function call"));
    }
    enter(r, "CLS:KEY 1,5", ms(300));
    CHECK_EQ(line(r, 0), std::string("Type mismatch"));
    enter(r, "CLS:KEY(1) ON", ms(300));
    CHECK_EQ(line(r, 0), std::string("Advanced Feature"));
    CHECK_EQ(line(r, 24).substr(0, 8), std::string("1FILES\x1b "));
}

// KEY LIST (GWSTS.ASM's KEYLST): the ten keys, "F", the number, a blank and the text, a CR shown as 27 (KEYLSP), one
// a line: the PC's defaults (the owner's answer 3), then Ok. Guards KEY LIST scrolling the screen with blank lines (a
// CR printed). Seen failing with KEYLSP's CR substitution left out.
TEST(key_list_lists_the_ten)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:KEY LIST", ms(600));
    const std::vector<std::string> want = {"F1 LIST",       "F2 RUN\x1b",    "F3 LOAD\"",       "F4 SAVE\"",
                                           "F5 CONT\x1b",   "F6 ,\"LPT1:\"\x1b", "F7 TRON\x1b", "F8 TROFF\x1b",
                                           "F9 KEY",        "F10 SCREEN 0,0,0\x1b", "Ok"};
    for (unsigned i = 0; i < want.size(); ++i)
        CHECK_EQ(line(r, i), want[i]);
    CHECK_EQ(line(r, 24), KEYLINE);
}

// KEY LIST with a CR in the middle of a key's text (7d2's review round 1, F3): KEY 1,"A"+CHR$(13)+"B" lists as F1,
// A, 27 (the CR, as the PC shows it), B: the 27 in its own cell and the B after it, not over it; the key line shows
// the same (KEYDCH). Guards a key's text listed short, a character lost under the 27. Seen failing with KEYLSP's 27
// printed by PUTC (DOESC: the cursor doesn't move, so the B covered it: "F1 AB").
TEST(key_list_shows_a_cr_in_the_middle_of_a_text)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "KEY 1,\"A\"+CHR$(13)+\"B\":CLS:KEY LIST", ms(600));
    CHECK_EQ(line(r, 0), std::string("F1 A\x1b" "B"));
    CHECK_EQ(line(r, 1), std::string("F2 RUN\x1b"));
    CHECK_EQ(line(r, 24).substr(0, 8), std::string("1A\x1b" "B    "));
    enter(r, "CLS:PRINT \"P\";CHR$(27);\"Q\"", ms(300));
    CHECK_EQ(line(r, 0), std::string("PQ"));
}

// ^T (SCNDRV.ASM's LABELK, the OEM's FKYADV): at 80 columns it turns the key line off and on; at 40 columns it shows
// F1-F5, then F6-F10, then off, then F1-F5 again. Typed in the middle of a line (here turning it on) it changes
// nothing of the line.
// Guards the 40-column line stuck on one half. Seen failing with FKYADV not going on past F6-F10 to off.
TEST(ctrl_t_pages_the_key_line)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    constexpr uint8_t T = 0x17;
    press(r, T, CTRL);
    r.run(ms(200));
    CHECK_EQ(row_at(r, 24), std::string(80, ' '));
    keys(r, "PRINT 12");
    press(r, T, CTRL);
    enter(r, "3", ms(300));
    CHECK(last_row(r, " 123") > 0);
    CHECK_EQ(line(r, 24), KEYLINE);
    enter(r, "WIDTH 40", ms(400));
    const std::string f1 = "1LIST   2RUN\x1b   3LOAD\"  4SAVE\"  5CONT\x1b", f6 = "6,\"LPT1 7TRON\x1b  8TROFF\x1b 9KEY    0SCREEN";
    CHECK_EQ(line(r, 24), f1);
    const std::vector<std::string> next = {f6, "", f1, f6};
    for (const std::string &want : next)
    {
        press(r, T, CTRL);
        r.run(ms(200));
        CHECK_EQ(line(r, 24), want);
    }
}

// The key line follows the page and the screen's size: SCREEN 0,,1,1 (a new active page) draws it on page 1, and
// WIDTH 80,30 puts it on the 30th row (index 29), the window the 29 above it (LOCATE 30 Illegal function call,
// LOCATE 29 fine), the 25th row plain text. Guards a page or a 30-row screen with no key line, or the line
// mid-screen. Seen failing with SCRNP's change of page not drawing it (KEYDSP).
TEST(the_key_line_follows_pages_and_width)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const uint32_t page0 = mapbase(r);
    enter(r, "SCREEN 0,,1,1", ms(400));
    CHECK(mapbase(r) != page0);
    CHECK_EQ(line(r, 24), KEYLINE);
    enter(r, "SCREEN 0,,0,0", ms(400));
    enter(r, "WIDTH 80,30", ms(400));
    CHECK_EQ(line(r, 29), KEYLINE);
    CHECK_EQ(row_at(r, 24), std::string(80, ' '));
    enter(r, "LOCATE 30,1", ms(300));
    CHECK(until_row(r, "Illegal function call") >= 0);
    enter(r, "CLS:LOCATE 29,1:PRINT \"ROW29\";:LOCATE 5,1", ms(300));
    CHECK_EQ(line(r, 28), std::string("ROW29"));
    CHECK_EQ(line(r, 29), KEYLINE);
}

// LOCATE's row past the window's last is Illegal function call, whatever the row (7d2's review round 1, F1): with
// the key line on, 25 and 255 (24 is the last); with it off, 26 and 255; at 30 rows, 30 and 255 on, 31 and 255 off.
// Each leaves the cursor where it was (the message on row 0 after CLS) and the key line drawn. Guards a program's
// LOCATE taking the cursor off the screen. Seen failing with LOCATE's check made on the row less KEYSW, which wraps
// 255 to 0 with the key line on.
TEST(locate_past_the_last_row_is_illegal)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    auto bad = [&](const std::vector<std::string> &rows, unsigned last) {
        for (const std::string &row : rows)
        {
            enter(r, "CLS:LOCATE " + row + ",1:PRINT \"X\"", ms(300));
            CHECK_EQ(line(r, 0), std::string("Illegal function call"));
        }
        enter(r, "CLS:LOCATE " + std::to_string(last) + ",1:PRINT \"L\";:LOCATE 5,1", ms(300));
        CHECK_EQ(line(r, last - 1), std::string("L"));
        CHECK(last_row(r, "Illegal") < 0);
    };
    bad({"25", "255"}, 24);
    CHECK_EQ(line(r, 24), KEYLINE);
    enter(r, "KEY OFF", ms(300));
    bad({"26", "255"}, 25);
    enter(r, "WIDTH 80,30:KEY ON", ms(400));
    bad({"30", "255"}, 29);
    CHECK_EQ(line(r, 29), KEYLINE);
    enter(r, "KEY OFF", ms(300));
    bad({"31", "255"}, 30);
}

// The soft keys (GIOKYB.ASM's CHKFKY and GETFKY; 7d2, the owner's answers 2 and 3): at Ok, F1 types its text, LIST,
// with no CR, and Enter lists; F2's RUN and its CR run the program; in a program INKEY$ gives F1's text a byte a call.
// A text the queue hasn't room for is dropped whole: with 20 keys waiting, F10's 13 bytes don't fit the 11 left, so
// the program reads the 20 alone. Guards function keys doing nothing, or another key's text. Seen failing with (a) the
// soft key's place one key on (F1 giving F2's text) and (b) the text queued without the room check (the 11 that fit
// queued, the rest lost).
TEST(a_function_key_types_its_text)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "10 PRINT \"RAN\"");
    enter(r, "CLS", ms(300));
    press(r, K_F1);
    r.run(ms(200));
    CHECK_EQ(line(r, 1), std::string("LIST"));
    keys(r, "\r");
    r.run(ms(400));
    CHECK_EQ(line(r, 2), std::string("10 PRINT \"RAN\""));
    press(r, K_F1 + 1);
    r.run(ms(600));
    CHECK(last_row(r, "RAN") > 2);
    CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "RAN")) - 1), std::string("RUN"));
    enter(r, "NEW");
    enter(r, "10 K$=\"\":FOR I=1 TO 300:K$=K$+INKEY$:IF LEN(K$)=4 THEN 20");
    enter(r, "15 NEXT");
    enter(r, "20 PRINT CHR$(91);K$;CHR$(93)");
    enter(r, "CLS:RUN", ms(100));
    press(r, K_F1);
    CHECK(until_row(r, "]") >= 0);
    CHECK_EQ(line(r, 0), std::string("[LIST]"));
    enter(r, "NEW");
    enter(r, "10 FOR I=1 TO 5000:NEXT:PRINT CHR$(91);");
    enter(r, "20 K$=\"\":FOR I=1 TO 40:K$=K$+INKEY$:NEXT:PRINT LEN(K$);K$;CHR$(93)");
    enter(r, "CLS:RUN", ms(50));
    keys(r, "ABCDEFGHIJKLMNOPQRST");
    press(r, K_F1 + 9);
    CHECK_EQ(line(r, 0), std::string()); // the program still waits
    CHECK(until_row(r, "]", ms(20000)) >= 0);
    CHECK_EQ(line(r, 0), std::string("[ 20 ABCDEFGHIJKLMNOPQRST]"));
}

// An empty soft key gives the PC's two-byte code (CHKFKY's: no text, no expansion): after KEY 1,"" INKEY$ reads F1
// as CHR$(0)+";" (59), and KEY 1 set again types its text. Guards games that read F1 by INKEY$ broken. Seen failing
// with the empty text's test left out (nothing queued).
TEST(an_empty_soft_key_gives_the_pc_code)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "KEY 1,\"\"");
    enter(r, "10 A$=INKEY$:IF A$=\"\" THEN 10");
    enter(r, "20 PRINT CHR$(91);LEN(A$);ASC(A$);ASC(RIGHT$(A$,1))");
    enter(r, "CLS:RUN", ms(200));
    press(r, K_F1);
    CHECK(until_row(r, "[") >= 0);
    CHECK_EQ(line(r, 0), std::string("[ 2  0  59"));
    enter(r, "KEY 1,\"OK1\":CLS", ms(300));
    press(r, K_F1);
    r.run(ms(200));
    CHECK_EQ(line(r, 1), std::string("OK1"));
}

// Shifted function keys have no soft text (CHKFKY takes 59-68 alone): Shift-F1 gives CHR$(0)+CHR$(84), Ctrl-F1 94,
// Alt-F1 104, as 7d1 gives them, while F1 types LIST. Guards a shifted key typing the plain key's text. Seen failing
// with the expansion taken on the key's usage, not its scan code's range (every F1 expanded).
TEST(shifted_function_keys_aren_t_expanded)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "10 A$=INKEY$:IF A$=\"\" THEN 10");
    enter(r, "20 PRINT CHR$(91);LEN(A$);ASC(A$);ASC(RIGHT$(A$,1)):GOTO 10");
    keys(r, "RUN\r");
    r.run(ms(200));
    constexpr uint8_t ALT = 0xE2;
    const std::vector<std::pair<uint8_t, std::string>> want = {
        {SHIFT, "[ 2  0  84"}, {CTRL, "[ 2  0  94"}, {ALT, "[ 2  0  104"}};
    for (const auto &[mod, text] : want)
    {
        press(r, K_F1, mod);
        r.run(ms(150));
        CHECK_EQ(line(r, static_cast<unsigned>(last_row(r, "[ "))), text);
    }
    press(r, K_F1); // LIST, a byte an INKEY$
    r.run(ms(300));
    const int last = last_row(r, "[ ");
    REQUIRE(last >= 3);
    CHECK_EQ(line(r, static_cast<unsigned>(last - 3)), std::string("[ 1  76  76"));
    CHECK_EQ(line(r, static_cast<unsigned>(last)), std::string("[ 1  84  84"));
    press(r, 0x06, CTRL);
    r.run(ms(200));
}

// SCREEN( reads a cell: PRINT "Q"; then SCREEN(r,c) is 81 and SCREEN(r,c,1) the attribute, in one statement.
// Guards SCREEN() returning the old character. Seen failing with SCRNFP not running OUTDO's TXBUF first (the "Q"
// still waiting to be drawn).
TEST(screen_reads_a_cell)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:LOCATE 3,3:PRINT \"Q\";SCREEN(3,3);SCREEN(3,3,1)", ms(400));
    CHECK_EQ(line(r, 2), std::string("  Q 81  7"));
}

// SCREEN 0's pages (the owner's answer 6): SCREEN 0,,1,0 sends PRINT to page 1, unseen (MAPBASE still page 0's);
// SCREEN 0,,1,1 shows it, page 0 as it was. Guards pages mixed up. Seen failing with the driver writing the
// visual page, not the active one.
TEST(screen_0_pages)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:PRINT \"PAGE ZERO\"", ms(300));
    const uint32_t page0 = mapbase(r);
    enter(r, "SCREEN 0,,1,0:CLS:PRINT \"PAGE ONE\"", ms(400));
    CHECK_EQ(mapbase(r), page0);
    CHECK_EQ(line(r, 0), std::string("PAGE ZERO"));
    enter(r, "SCREEN 0,,1,1", ms(300));
    CHECK(mapbase(r) != page0);
    CHECK_EQ(line(r, 0), std::string("PAGE ONE"));
    CHECK_EQ(row_at(r, 0, page0).substr(0, 9), std::string("PAGE ZERO"));
    enter(r, "SCREEN 1", ms(300));
    CHECK(last_row(r, "Illegal function call") >= 0);
    enter(r, "SCREEN 0,,4", ms(300));
    CHECK(last_row(r, "Illegal function call") >= 0);
}

// The timing gate (V6), on the running GWBASIC.COM: PRINT A$; with an 80-character A$ and no scroll, by check.py
// --timing's method (the run of a loop with the statement less the run without it, over 200 passes): at most 30,000
// cycles. A whole-window scroll, MKRMNL's SCRLUP (SCRLUPN) from its JSR to its return, the average over the
// scrolls of 40 lines (24 lines: 23 rows copied on the card by DM_VCOPY, the terminators, the last row blank), the
// row last printed already back on the card: at most 14,000, the owner's cap of 2026-11-18 (answer 4, revised: the
// figure measured after SCRLUP's trim, 13,892, rounded up to the next 500). With the row just printed still to
// write back, as under LIST, the scroll writes it first: about 2,000 more, a write the PRINT makes anyway. And, for
// the record, a key's cost: EVPOLL's far call to KEYEV, the cooked twin's drain included. Guards a screen too slow
// to use. Seen failing with the run bypassed (TXRUN far called for every byte).
TEST(print_and_scroll_are_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    auto pass = [&](const std::string &stmt) {
        const uint64_t a = timed(r, {"10 A$=STRING$(80,65)", "20 LOCATE 1,1:" + stmt + "I%=I%+1:IF I%<100 THEN 20"});
        const uint64_t b = timed(r, {"10 A$=STRING$(80,65)", "20 LOCATE 1,1:" + stmt + "I%=I%+1:IF I%<300 THEN 20"});
        return static_cast<double>(b - a) / 200;
    };
    const double with = pass("PRINT A$;:"), without = pass("");
    const double print = with - without;
    std::printf("  PRINT A$; (80 characters, no scroll): %.0f cycles (the gate 30,000)\n", print);
    CHECK(print > 1000 && print <= 30000);
    // The scroll: SCRLUP's calls while 40 lines are printed.
    const auto l6 = lbl_map(std::string("p6.lbl"));
    const uint16_t up = l6.at("SCRLUPN"), kev = l6.at("KEYEV");
    uint64_t from = 0, total = 0, kfrom = 0, ktotal = 0, lo = ~uint64_t{0}, hi = 0;
    unsigned n = 0, kn = 0;
    int depth = -1, kdepth = -1;
    r.watch = [&](uint16_t pc) {
        if (depth < 0 && pc == up)
        {
            from = r.cpu->cycles();
            depth = r.cpu->regs().s;
        }
        else if (depth >= 0 && r.cpu->regs().s == depth + 2)
        {
            const uint64_t c = r.cpu->cycles() - from;
            total += c;
            lo = std::min(lo, c);
            hi = std::max(hi, c);
            ++n;
            depth = -1;
        }
        if (kdepth < 0 && pc == kev)
        {
            kfrom = r.cpu->cycles();
            kdepth = r.cpu->regs().s;
        }
        else if (kdepth >= 0 && r.cpu->regs().s == kdepth + 2)
        {
            ktotal += r.cpu->cycles() - kfrom;
            ++kn;
            kdepth = -1;
        }
    };
    // First PRINT I: the scroll then also writes back the row just printed (DM_VWRITE), a write the PRINT makes,
    // scroll or not; then PRINT alone, the row already back on the card: the scroll's own cost, the cap's figure.
    enter(r, "CLS:FOR I=1 TO 40:PRINT I:NEXT", ms(1500));
    const double with_row = n ? static_cast<double>(total) / n : 0;
    std::printf("  a scroll with the row just printed written back first: %u, %.0f cycles each\n", n, with_row);
    n = 0;
    total = 0;
    lo = ~uint64_t{0};
    hi = 0;
    enter(r, "CLS:FOR I=1 TO 40:PRINT:NEXT", ms(1500));
    std::printf("  (each from %llu to %llu)\n", static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi));
    r.watch = nullptr;
    const double scroll = n ? static_cast<double>(total) / n : 0, key = kn ? static_cast<double>(ktotal) / kn : 0;
    std::printf("  a whole-window scroll (SCRLUPN, MKRMNL's SCRLUP): %u, %.0f cycles each (the cap 14,000)\n", n, scroll);
    std::printf("  a key (KEYEV, its drain included): %u, %.0f cycles each\n", kn, key);
    CHECK(n >= 15);
    CHECK(scroll > 1000 && scroll <= 14000);
}

// For the record (7d2, unit 6), not gated: what the key line and the soft keys cost. CLS with the key line on (the
// line drawn again), KEY 1,"X" with it on (drawn again) and with it off, by timed()'s method over 20 passes; and a
// key's cost, EVPOLL's far call to KEYEV with its drain, for a plain key (A: its cooked twin drained too), for F1 with
// no text (the PC's code queued) and for F1 with its text (LIST: 4 bytes queued), each the average over 8 presses
// while a program reads INKEY$.
TEST(the_key_line_and_soft_keys_cost)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    auto per = [&](const std::string &stmt) {
        const uint64_t a = timed(r, {"10 I%=I%+1:IF I%<20 THEN 10"});
        const uint64_t b = timed(r, {"10 " + stmt + ":I%=I%+1:IF I%<20 THEN 10"});
        return static_cast<double>(b - a) / 20;
    };
    const double cls = per("CLS"), keyon = per("KEY 1,\"X\"");
    enter(r, "KEY OFF", ms(300));
    const double clsoff = per("CLS"), keyoff = per("KEY 1,\"X\"");
    enter(r, "KEY ON", ms(300));
    std::printf("  CLS with the key line: %.0f cycles (%.0f with it off)\n", cls, clsoff);
    std::printf("  KEY 1,\"X\" with the key line on: %.0f cycles (%.0f with it off)\n", keyon, keyoff);
    CHECK(cls > clsoff && keyon > keyoff);
    enter(r, "KEY 1,\"\":NEW");
    enter(r, "10 A$=INKEY$:GOTO 10");
    keys(r, "RUN\r");
    r.run(ms(200));
    const uint16_t kev = lbl_map(std::string("p6.lbl")).at("KEYEV");
    uint64_t kfrom = 0, ktotal = 0;
    unsigned kn = 0;
    int kdepth = -1;
    const std::function<void(uint16_t)> watch = [&](uint16_t pc) {
        if (kdepth < 0 && pc == kev)
        {
            kfrom = r.cpu->cycles();
            kdepth = r.cpu->regs().s;
        }
        else if (kdepth >= 0 && r.cpu->regs().s == kdepth + 2)
        {
            ktotal += r.cpu->cycles() - kfrom;
            ++kn;
            kdepth = -1;
        }
    };
    r.watch = watch;
    auto avg = [&](uint8_t key) {
        kn = 0;
        ktotal = 0;
        for (int i = 0; i < 8; ++i)
        {
            press(r, key);
            r.run(ms(50));
        }
        return kn ? static_cast<double>(ktotal) / kn : 0;
    };
    const double plain = avg(0x04), none = avg(K_F1);
    r.watch = nullptr;
    press(r, 0x06, CTRL);
    r.run(ms(200));
    enter(r, "KEY 1,\"LIST\"");
    keys(r, "RUN\r");
    r.run(ms(200));
    r.watch = watch;
    const double soft = avg(K_F1);
    r.watch = nullptr;
    std::printf("  a key (KEYEV, its drain included): A %.0f cycles, F1 with no text %.0f, F1 with LIST %.0f\n", plain,
                none, soft);
    CHECK(soft > none);
    press(r, 0x06, CTRL);
    r.run(ms(200));
}

// Review round 1 (7d1), F1: insert mode that pushes a line past the row's end wraps what runs off onto the next row,
// and the line entered is the line on the screen: 70 B's in a string, the cursor 20 back, Insert, ten C's (the line
// grows to 88, two rows), Enter: no Syntax error, and A$ is 80 characters with the C's from its 52nd. The same on the
// window's last line, where the wrap scrolls the screen. Guards insert typing garbling a long line (the wrap landing
// two rows down: "Syntax error", the line not entered). Seen failing with MKRMCH keeping CH alone round MVCHLN (the
// insert past the end leaving CL its count), as 7d1 was built.
TEST(insert_past_the_row_s_end_wraps_the_line)
{
    for (const bool bottom : {false, true})
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r) > 0);
        if (bottom)
            enter(r, "FOR I=1 TO 40:PRINT I:NEXT:PRINT", ms(1500)); // the cursor on the window's last line
        else
            enter(r, "CLS");
        keys(r, "10 A$=\"" + std::string(70, 'B') + "\"");
        for (int i = 0; i < 20; ++i)
            press(r, K_LEFT);
        press(r, K_INS);
        keys(r, std::string(10, 'C'));
        keys(r, "\r");
        r.run(ms(400));
        enter(r, "RUN", ms(400));
        enter(r, "PRINT LEN(A$);INSTR(A$,\"C\");MID$(A$,50,14)", ms(400));
        CHECK(last_row(r, "Syntax error") < 0);
        CHECK(last_row(r, " 80  52 BBCCCCCCCCCCBB") >= 0);
        if (last_row(r, " 80  52 BBCCCCCCCCCCBB") < 0)
            std::printf("  (%s) the screen: %s\n", bottom ? "the last line" : "the top", screen(r).c_str());
    }
}

// Review round 1, F2: what a statement printed is on the screen before GW-BASIC waits for a key outside the
// editor: "Division by zero" (an overflow message, the program going on) shown while INPUT$(1) waits; and LIST
// paused by ^S has drawn every byte it gave before the pause (TXBUF empty), then goes on to its end. Guards a
// message or a listing's lines missing from the screen while it waits. Seen failing with POLKEY and KEYEV's pause
// not running TXBUF first, as 7d1 was built.
TEST(output_is_shown_before_a_wait_for_a_key)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r) > 0);
        enter(r, "10 A=1/0:X$=INPUT$(1):PRINT \"GOT\"");
        keys(r, "RUN\r");
        r.run(ms(800));
        CHECK_EQ(line(r, 6), std::string("Division by zero"));
        press(r, 0x04);
        r.run(ms(400));
        CHECK_EQ(line(r, 7), std::string("GOT"));
        if (line(r, 7) != "GOT")
            std::printf("  the screen: %s\n", screen(r).c_str());
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        REQUIRE(start(r) > 0);
        for (int i = 1; i <= 15; ++i)
            enter(r, std::to_string(i * 10) + " REM " + std::string(40, static_cast<char>('A' + i)), ms(250));
        const auto lab = lbl_map();
        keys(r, "LIST\r");
        r.run(ms(35));
        press(r, 0x16, CTRL); // ^S
        r.run(ms(500));
        CHECK_EQ(r.machine->bus().peek(lab.at("TXLEN")), 0);
        const std::string paused = screen(r);
        r.run(ms(500));
        CHECK_EQ(screen(r), paused); // still paused
        press(r, 0x04);
        r.run(ms(800));
        const int ok = last_row(r, "Ok");
        CHECK(ok > 0);
        if (ok > 0)
            CHECK_EQ(line(r, static_cast<unsigned>(ok - 1)), "150 REM " + std::string(40, 'P'));
    }
}

// Review round 1, F3: an error in the command line, in screen mode, is on the console when GW-BASIC goes back to
// OODOS: "m:gwbasic /Q" (no such switch) and "m:gwbasic /M:100" (start-up's Out of memory) print the heading and
// "Error detected in command line" to the console (its screen and the serial line, CON:'s), then A>. Guards the
// error vanishing (it went to TXBUF, never run). Seen failing with CMDERR printing in screen mode, as 7d1 was
// built.
TEST(a_command_line_error_is_on_the_console)
{
    for (const std::string tail : {"/Q", "/M:100"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}});
        const size_t from = r.out.size();
        r.type("m:gwbasic " + tail + "\r");
        CHECK(r.until("A>", ms(6000), from + 5));
        r.run(ms(300));
        const std::string tty = r.out.substr(from);
        CHECK(tty.find("(C) Copyright Microsoft 1982") != std::string::npos);
        CHECK(tty.find("\r\nError detected in command line\r\n") != std::string::npos);
        bool shown = false;
        for (unsigned row = 0; row < 30; ++row)
            shown = shown || row_at(r, row, 0x2000, 80).find("Error detected in command line") != std::string::npos;
        CHECK(shown);
        CHECK_EQ(mapbase(r), 0x2000u); // the console's screen shown again
        if (!shown)
            std::printf("  (%s) the serial line: %s\n", tail.c_str(), vis(tty).c_str());
    }
}

// Review round 1, F4: PRINT's TAB and comma go by where the cursor is, also after the screen's control codes in the
// same PRINT: "ABCDEFGHIJ";CHR$(11);TAB(3);"X" puts X in column 3 of the first row (CHR$(11) homes the cursor), and
// "ABCDEFGHIJ" with five CHR$(29)s (cursor left), "Y" and ,"Z" puts Y over F and Z in the next zone, column 15.
// Guards TAB and commas going by OUTDO's own count (X on a new row, Z five columns early). Seen failing with
// PTRGPS taking TTYPOS before TXBUF is run, as 7d1 was built.
TEST(tab_and_comma_follow_the_cursor)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "CLS:PRINT \"ABCDEFGHIJ\";CHR$(11);TAB(3);\"X\"", ms(400));
    CHECK_EQ(line(r, 0), std::string("  XDEFGHIJ"));
    CHECK_EQ(line(r, 1), std::string("Ok"));
    enter(r, "CLS:PRINT \"ABCDEFGHIJ\";STRING$(5,29);\"Y\",\"Z\"", ms(400));
    CHECK_EQ(line(r, 0), std::string("ABCDEY        Z"));
    enter(r, "CLS:PRINT \"ABCDEFGHIJ\";CHR$(11);POS(0)", ms(400));
    CHECK_EQ(line(r, 0), std::string(" 1 DEFGHIJ"));
}

// Review round 1, F5: with 25 rows the 30-row layer's rows 26-30, the bands above and below the centred text, are
// the border's colour, as a PC's overscan: after COLOR 15,1:WIDTH 40:COLOR 7,0:CLS they are black (attribute 0),
// not WIDTH's blue; COLOR ,,4 makes them red on every page (page 1's too), the text's cells as they were. Guards
// the bands keeping a colour from before (a blue frame round the text) and COLOR's border never showing. Seen
// failing with PGINIT filling the bands in ATTR, and no fill for COLOR's border, as 7d1 was built.
TEST(the_border_shows_in_the_bands)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "COLOR 15,1:WIDTH 40:COLOR 7,0:CLS", ms(600));
    const uint32_t base = mapbase(r);
    CHECK_EQ(card(r, base + 1), 0x07);
    for (const unsigned row : {25u, 27u, 29u})
        CHECK_EQ(card(r, base + row * 80 + 79), 0x00);
    enter(r, "COLOR ,,4", ms(600));
    CHECK_EQ(card(r, base + 1), 0x07);
    CHECK_EQ(card(r, base + 24 * 80 + 1), 0x07);
    for (const unsigned row : {25u, 29u})
    {
        CHECK_EQ(card(r, base + row * 80 + 1), 0x40);
        CHECK_EQ(card(r, base + 12 * 256 + row * 80 + 1), 0x40); // page 1 (12 card pages at 40 columns)
    }
    enter(r, "WIDTH 80,30", ms(600));
    CHECK_EQ(card(r, mapbase(r) + 29 * 160 + 1), 0x07); // 30 rows: no bands, the text's own
}

// Review round 1, F7: the keyboard's lights at start-up are Caps Lock (GW-BASIC's own, on) and Num Lock (the keypad
// gives digits): LEDS 3; Caps Lock pressed leaves Num Lock's on. Guards Num Lock's light off while the keypad types
// digits. Seen failing with KLEDS writing Caps Lock's bit alone, as 7d1 was built.
TEST(the_keyboard_s_lights_are_caps_and_num_lock)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    CHECK_EQ(r.machine->usb_card()->leds(), 0x03);
    press(r, 0x39); // Caps Lock
    r.run(ms(100));
    CHECK_EQ(r.machine->usb_card()->leds(), 0x01);
}

int main()
{
    return run_all();
}
