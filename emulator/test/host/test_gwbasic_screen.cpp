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

// 7d1's first light: a line typed on the USB keyboard runs, its output after it.
TEST(a_typed_print_runs)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "PRINT 6*7");
    const int at = until_row(r, " 42");
    CHECK(at > 0);
    std::printf("  the screen: %s\n", screen(r).c_str());
}

// A probe: PRINT A$; with an 80-character A$ on one row, against an empty A$.
TEST(probe_print_cost)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const uint64_t a1 = timed(r, {"10 A$=STRING$(80,65):B$=CHR$(11)", "20 PRINT A$;B$;:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t a2 = timed(r, {"10 A$=STRING$(80,65):B$=CHR$(11)", "20 PRINT A$;B$;:I%=I%+1:IF I%<300 THEN 20"});
    const uint64_t e1 = timed(r, {"10 A$=\"\":B$=CHR$(11)", "20 PRINT A$;B$;:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t e2 = timed(r, {"10 A$=\"\":B$=CHR$(11)", "20 PRINT A$;B$;:I%=I%+1:IF I%<300 THEN 20"});
    const double a = static_cast<double>(a2 - a1) / 200, e = static_cast<double>(e2 - e1) / 200;
    std::printf("  PRINT A$;B$; 80: %.0f a pass; empty: %.0f; the 80 characters: %.0f\n", a, e, a - e);
    const uint64_t s1 = timed(r, {"20 PRINT:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t s2 = timed(r, {"20 PRINT:I%=I%+1:IF I%<300 THEN 20"});
    std::printf("  PRINT (a scroll each): %.0f a pass\n", static_cast<double>(s2 - s1) / 200);
    std::printf("  the screen: %s\n", screen(r).c_str());
}

// A probe: where the cycles go, by label, for one PRINT of 80 characters.
TEST(probe_profile)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    std::map<uint16_t, std::string> names;
    for (const auto &[n, a] : lbl_map(std::string("p6.lbl")))
        if (a >= 0xC800)
            names[a] = "p6:" + n;
    for (const auto &[n, a] : lbl_map(std::string("gwbasic.lbl")))
        names[a] = n;
    enter(r, "NEW");
    enter(r, "10 A$=STRING$(80,65)");
    enter(r, "20 FOR I=1 TO 40:PRINT:NEXT");
    std::map<std::string, uint64_t> hist;
    uint64_t last = r.cpu->cycles();
    std::string where = "?";
    bool on = false;
    const auto lab0 = lbl_map(std::string("gwbasic.lbl"));
    const uint16_t runc = lab0.at("RUN"), endcon = lab0.at("ENDCON");
    r.watch = [&](uint16_t pc) {
        if (pc == runc)
            on = true;
        if (pc == endcon)
            on = false;
        const uint64_t now = r.cpu->cycles();
        if (on)
            hist[where] += now - last;
        last = now;
        auto it = names.upper_bound(pc);
        if (pc >= 0xF800 || (pc >= 0x4000 && pc < 0x8000 && r.nb(1) == 60))
            where = pc >= 0xF800 ? "common" : "bdos";
        else if (it == names.begin())
            where = "?";
        else
            where = std::prev(it)->second;
    };
    const auto l6 = lbl_map(std::string("p6.lbl"));
    const uint16_t dmc = l6.at("DMCALL");
    unsigned ndm = 0;
    std::map<uint8_t, std::pair<unsigned, uint64_t>> per;
    uint64_t dmfrom = 0;
    uint8_t dmop = 0;
    int depth = -1;
    auto inner = r.watch;
    r.watch = [&](uint16_t pc) {
        inner(pc);
        if (on && pc == dmc && r.nb(3) == r.nb(3))
        {
            ++ndm;
            dmfrom = r.cpu->cycles();
            dmop = r.machine->bus().peek(static_cast<uint16_t>(l6.at("DMB")));
            depth = r.cpu->regs().s;
        }
        else if (depth >= 0 && r.cpu->regs().s == depth + 2) // back in DMCALL's caller
        {
            per[dmop].first++;
            per[dmop].second += r.cpu->cycles() - dmfrom;
            depth = -1;
        }
    };
    keys(r, "RUN\r");
    r.run(ms(3000));
    r.watch = nullptr;
    std::printf("  DMCALLs: %u\n", ndm);
    for (const auto &[op, c] : per)
        std::printf("  op %u: %u calls, %.0f cycles each\n", op, c.first, static_cast<double>(c.second) / c.first);
    std::vector<std::pair<uint64_t, std::string>> v;
    for (const auto &[n, c] : hist)
        v.push_back({c, n});
    std::sort(v.rbegin(), v.rend());
    for (size_t i = 0; i < v.size() && i < 25; ++i)
        std::printf("  %8llu %s\n", static_cast<unsigned long long>(v[i].first), v[i].second.c_str());
}

// A probe: a whole-window scroll's cycles, SCRLUP's call (MKRMNL's, at a line feed on the window's last line)
// from its JSR to its return; and a scrolling PRINT against one that doesn't scroll (PRINT CHR$(11); homes).
TEST(probe_scroll_cost)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const auto l6 = lbl_map(std::string("p6.lbl"));
    const uint16_t up = l6.at("SCRLUP");
    uint64_t from = 0, total = 0;
    unsigned n = 0;
    int depth = -1;
    r.watch = [&](uint16_t pc) {
        if (depth < 0 && pc == up && r.nb(3) != 0)
        {
            from = r.cpu->cycles();
            depth = r.cpu->regs().s;
        }
        else if (depth >= 0 && r.cpu->regs().s == depth + 2)
        {
            total += r.cpu->cycles() - from;
            ++n;
            depth = -1;
        }
    };
    enter(r, "FOR I=1 TO 40:PRINT:NEXT");
    r.watch = nullptr;
    std::printf("  SCRLUP: %u calls, %.0f cycles each\n", n, n ? static_cast<double>(total) / n : 0.0);
    const uint64_t a1 = timed(r, {"20 PRINT:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t a2 = timed(r, {"20 PRINT:I%=I%+1:IF I%<300 THEN 20"});
    const uint64_t b1 = timed(r, {"20 PRINT CHR$(11);:PRINT:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t b2 = timed(r, {"20 PRINT CHR$(11);:PRINT:I%=I%+1:IF I%<300 THEN 20"});
    const uint64_t c1 = timed(r, {"20 PRINT CHR$(11);:I%=I%+1:IF I%<100 THEN 20"});
    const uint64_t c2 = timed(r, {"20 PRINT CHR$(11);:I%=I%+1:IF I%<300 THEN 20"});
    const double a = static_cast<double>(a2 - a1) / 200, b = static_cast<double>(b2 - b1) / 200,
                 c = static_cast<double>(c2 - c1) / 200;
    std::printf("  PRINT, scrolling: %.0f a pass; homed then PRINT: %.0f; homed alone: %.0f; the scroll: %.0f\n", a, b, c,
                a - (b - c));
}

int main()
{
    return run_all();
}
