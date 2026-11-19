/**
 * @file test_gwbasic_screen.cpp
 * @brief GW-BASIC's own text screen, phase 7d1 (hw/oodos.md, "The plan, phase 7": block P6), run from OODOS's CCP
 *        on backplane.machine.
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
 * the failure on real hardware it guards and the broken build it was seen failing on (hw/oodos.md, 7d1 as built).
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
// 40, then Ok, then the cursor's blank line at the window's foot, and the 25th line blank. Guards lines lost or
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
    CHECK_EQ(line(r, 24), std::string());
    CHECK_EQ(row_at(r, 24), std::string(80, ' '));
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
    CHECK(until_row(r, " 5") > 0);
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

int main()
{
    return run_all();
}
