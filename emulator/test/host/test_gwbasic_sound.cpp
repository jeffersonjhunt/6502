/**
 * @file test_gwbasic_sound.cpp
 * @brief GW-BASIC's sound, input and events, phase 7f (hw/oodos.md, "The plan, phase 7": block P7): SOUND, BEEP,
 *        PLAY and its traps on OOLIB's music driver, TIMER and ON TIMER, DATE$ and TIME$, STICK, STRIG and their
 *        traps, ON KEY(n) and KEY 15-20, ON COM(n), PEN; run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_screen does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M:, and GW-BASIC starts on its own screen (CON:'s output keeps the CRT), its keys pressed on the USB card.
 *
 * PLAY, SOUND, STICK and the traps print nothing PC-BASIC's oracle could compare, so every case reads the machine
 * (7f's plan, "New tests"): the music driver's commands as P7 hands them to function 82 (SN_SEND's bytes, watched
 * at the BDOS's entry and decoded here: a voice's entry is its command byte, then the frequency word, the length
 * and the gate time in ms, each low byte first), the SID's registers and the card's CTRL, the beeper's block
 * (SN_BEEP), the RTC set by the rig, the joystick ports and the USB keyboard driven by it. Each expected figure is
 * worked out here from the manual (the IBM BASIC Reference 3.0, for meaning), the source (GWSTS.ASM's PLAY and
 * SOUND) and the owner's answers to 7f's plan; each case names the failure on real hardware it guards and the
 * broken build it was seen failing on.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
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

    /// A link's labels (ld65's -Ln): the resident core's (gwbasic.lbl) or a block's (p7.lbl).
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

    constexpr uint16_t L0_CONFIG = 0xC490, L0_MAP = 0xC491;
    uint8_t reg(CcpRig &r, uint16_t a) { return r.machine->bus().peek(a); }
    uint32_t mapbase(CcpRig &r) { return (reg(r, L0_MAP) | reg(r, L0_MAP + 1) << 8) * 256u; }
    unsigned columns(CcpRig &r) { return (reg(r, L0_CONFIG) & 7) == 1 ? 40 : 80; }
    uint8_t vcard(CcpRig &r, uint32_t a) { return r.machine->video_card()->card_peek(a); }

    /// Row `row` of GW-BASIC's text, its trailing blanks dropped (a $FF as a blank).
    std::string line(CcpRig &r, unsigned row)
    {
        std::string s;
        const uint32_t base = mapbase(r);
        const unsigned cols = columns(r);
        for (unsigned c = 0; c < cols; ++c)
        {
            const char ch = static_cast<char>(vcard(r, base + row * cols * 2 + c * 2));
            s += static_cast<unsigned char>(ch) == 0xFF ? ' ' : ch;
        }
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    }
    std::string screen(CcpRig &r, unsigned rows = 25)
    {
        std::string s;
        for (unsigned i = 0; i < rows; ++i)
            s += line(r, i) + "|";
        return s;
    }
    /// Run until some row holds `text`. @return its row, or -1
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
    /// GW-BASIC's two files on M: and GW-BASIC started on its own screen, until its first Ok. @return its row
    int start(CcpRig &r)
    {
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        CHECK(!com.empty() && !ovl.empty());
        if (com.empty() || ovl.empty())
            return -1;
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}});
        const size_t from = r.out.size();
        r.type("m:gwbasic\r");
        CHECK(r.until("m:gwbasic\r\n", ms(2000), from));
        const int ok = until_row(r, "Ok");
        r.run(ms(100));
        return ok;
    }
    /// The screen cleared (so the next line's output is easy to find) and a direct line typed: the row after the
    /// line's echo, or "" if that is Ok (a statement that prints nothing).
    std::string said(CcpRig &r, const std::string &text, uint64_t settle = ms(600))
    {
        enter(r, "CLS");
        enter(r, text, settle);
        for (unsigned i = 0; i + 1 < 25; ++i)
            if (line(r, i) == text)
            {
                const std::string next = line(r, i + 1);
                return next == "Ok" ? std::string() : next;
            }
        std::printf("  '%s' not echoed: %s\n", text.c_str(), screen(r).c_str());
        return "?";
    }

    SoundCard &card(CcpRig &r) { return *r.machine->sound_card(); }
    bool running(CcpRig &r) { return (card(r).peek(SoundCard::CTRL) & SoundCard::CTRL_RUN) != 0; }

    constexpr uint8_t O_SOUND = 82, SN_RELEASE = 2, SN_LOAD = 3, SN_SEND = 6, SN_BEEP = 16;

    /// A voice's entry as P7 sent it to the driver (lib/mudrv/mudrv.s's protocol, lib/mu.s's MUQ).
    struct Entry
    {
        uint64_t at;   ///< the cycle its first byte went
        unsigned voice;
        unsigned freq; ///< the SID's frequency word
        unsigned len, gate; ///< ms
    };
    /// Function 82 as P7 calls it, watched at the BDOS's entry ($0203, Y 82, A/X the block): SN_SEND's bytes
    /// decoded into entries (a voice's tone or note: $0v and 6 bytes), stops ($2v), and the other calls counted.
    struct Sound
    {
        std::vector<Entry> entries;
        std::vector<std::pair<uint64_t, unsigned>> stops; ///< (cycle, voice)
        std::vector<std::vector<uint8_t>> beeps;          ///< SN_BEEP's blocks: +1-2 Hz, +3-4 ms
        std::vector<std::vector<uint8_t>> loads;          ///< SN_LOAD's blocks
        unsigned releases = 0;
        std::vector<uint8_t> pend;
        unsigned need = 0;
        uint64_t pend_at = 0;
        // The call under way: its return address and S after it, its op and block; taken only if it returns A 0
        // (SN_SEND is called again while the card is busy).
        bool in = false;
        uint16_t ret = 0;
        uint8_t ret_s = 0;
        uint64_t call_at = 0;
        std::vector<uint8_t> blk;
        unsigned w(size_t i) const { return static_cast<unsigned>(pend[i] | pend[i + 1] << 8); }
        void byte(uint64_t at, uint8_t b)
        {
            if (need == 0)
            {
                pend.assign(1, b);
                pend_at = at;
                const unsigned hi = b >> 4;
                need = hi == 0 ? 6 : hi == 1 ? 5 : hi == 3 || hi == 6 ? 1 : 0;
                if (hi == 2)
                    stops.push_back({at, b & 15u});
                return;
            }
            pend.push_back(b);
            if (--need == 0 && (pend[0] >> 4) == 0)
                entries.push_back({pend_at, pend[0] & 15u, w(1), w(3), w(5)});
        }
        void done(const std::vector<uint8_t> &b, uint64_t at)
        {
            if (b[0] == SN_SEND)
                byte(at, b[1]);
            else if (b[0] == SN_BEEP)
                beeps.push_back(b);
            else if (b[0] == SN_LOAD)
                loads.push_back(b);
            else if (b[0] == SN_RELEASE)
                ++releases;
        }
        void watch(CcpRig &r)
        {
            r.watch = [this, &r](uint16_t pc) {
                const auto &g = r.cpu->regs();
                Bus &bus = r.machine->bus();
                if (in && pc == ret && g.s == ret_s)
                {
                    in = false;
                    if (g.a == 0)
                        done(blk, call_at);
                    return;
                }
                if (pc != 0x0203 || g.y != O_SOUND || g.x == 0) // (X 0: an operation with no block)
                    return;
                const uint16_t at = static_cast<uint16_t>(g.a | g.x << 8);
                blk.clear();
                for (uint16_t i = 0; i < 8; ++i)
                    blk.push_back(bus.peek(static_cast<uint16_t>(at + i)));
                const uint16_t sp = static_cast<uint16_t>(0x100 + g.s);
                ret = static_cast<uint16_t>((bus.peek(static_cast<uint16_t>(sp + 1)) | bus.peek(static_cast<uint16_t>(sp + 2)) << 8) + 1);
                ret_s = static_cast<uint8_t>(g.s + 2);
                call_at = r.cpu->cycles();
                in = true;
            };
        }
    };
    /// MU_TONE's register value on PAL's clock: Hz x 4,359 / 256 (lib/mutone.s), held at $FFFF.
    unsigned pal(unsigned hz) { return std::min(65535u, hz * 4359u / 256u); }
    /// GW-BASIC's 2.5 ms units as ms: x 5 / 2, rounded (P7's UNITMS).
    unsigned units_ms(unsigned u) { return (u * 5 + 1) / 2; }
    double as_ms(uint64_t cycles) { return static_cast<double>(cycles) * 1000.0 / static_cast<double>(HZ); }

    /// A program typed after NEW and RUN, the sound watched; run for `budget` and then until Ok.
    void run_prog(CcpRig &r, const std::vector<std::string> &lines, uint64_t budget = ms(2000))
    {
        enter(r, "CLS");
        enter(r, "NEW");
        for (const std::string &l : lines)
            enter(r, l, ms(150));
        enter(r, "CLS");
        keys(r, "RUN\r");
        r.run(budget);
    }
} // namespace

// N9 (7f's plan): SOUND 440,18.2 plays one entry on voice 0: the frequency word 440 x 4,359 / 256 (PAL, MU_TONE's
// arithmetic), 18.2 ticks x 22 = 400 units = 1,000 ms, the gate the whole note (the source's SOUND is legato). In
// MF (the default: the owner's answer 6, a queue one note deep) the statement after the first SOUND runs at once,
// and a second SOUND waits for the first to end. Guards SOUND silent, a wrong pitch, or a length in the wrong units.
// Seen failing with (a) the length taken as ms (the units' x 5 / 2 left out), (b) MF's wait after the note, not
// before the next.
TEST(sound_plays_its_tone_on_the_sid)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const std::map<std::string, uint16_t> lab = lbl_map();
    REQUIRE(lab.count("OUTDO") == 1);
    Sound s;
    std::vector<uint64_t> prints; // PRINT's "A" and "B" at OUTDO
    s.watch(r);
    const auto snd = r.watch;
    r.watch = [&](uint16_t pc) {
        snd(pc);
        if (pc == lab.at("OUTDO") && (r.cpu->regs().a == 'A' || r.cpu->regs().a == 'B'))
            prints.push_back(r.cpu->cycles());
    };
    run_prog(r, {"10 SOUND 440,18.2:PRINT \"A\":SOUND 880,18.2:PRINT \"B\""}, ms(2600));
    r.watch = nullptr;
    REQUIRE(s.entries.size() == 2u);
    CHECK_EQ(s.entries[0].voice, 0u);
    CHECK_EQ(s.entries[0].freq, pal(440));
    CHECK_EQ(s.entries[0].len, 1000u);
    CHECK_EQ(s.entries[0].gate, 1000u);
    CHECK_EQ(s.entries[1].freq, pal(880));
    REQUIRE(prints.size() == 2u);
    const double a = as_ms(prints[0] - s.entries[0].at), second = as_ms(s.entries[1].at - s.entries[0].at),
                 b = as_ms(prints[1] - s.entries[0].at);
    std::printf("  from the first entry: PRINT \"A\" %.1f ms, the second entry %.1f ms, PRINT \"B\" %.1f ms\n", a, second,
                b);
    CHECK(a < 50);
    CHECK(second > 950 && second < 1150);
    CHECK(b > second && b - second < 50);
    CHECK(running(r));
}

// N10 (the manual's p. 323): SOUND 500,100 then SOUND 500,0 stops it at once (MU_STOP of voice 0, the gate
// dropped); SOUND 500,0 with nothing sounding, in a run that hasn't played, starts no driver: the 6510 left
// unclaimed (CTRL's RUN off, no SN_LOAD). Guards a stuck note; a 6510 claimed for nothing, refusing the next
// program's .sid. Seen failing with SOUND x,0 queued as a 1 ms note.
TEST(sound_x_0_stops_and_does_nothing_when_silent)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    run_prog(r, {"10 SOUND 500,0"}, ms(500));
    CHECK(s.loads.empty());
    CHECK(s.entries.empty());
    CHECK(!running(r));
    run_prog(r, {"10 SOUND 500,100:SOUND 500,0"}, ms(500));
    r.watch = nullptr;
    CHECK_EQ(s.loads.size(), 1u);
    CHECK_EQ(s.entries.size(), 1u);
    REQUIRE(!s.stops.empty());
    CHECK_EQ(s.stops.back().second, 0u);
    CHECK(!s.entries.empty() && s.stops.back().first > s.entries[0].at);
    CHECK_EQ(card(r).sid(0)[4] & 1, 0); // voice 0's gate off
}

// N11 (the manual's p. 323: 37 to 32,767 Hz, 0 to 65,535 ticks; the owner's answer 19: a negative duration Illegal
// function call): 36 Illegal function call, 37 and 32,767 taken; 65,535 ticks taken (three entries back to back,
// the owner's answer 18: 65,535 x 22 units held at 65,535, 163,838 ms, past the driver's 16-bit length), 65,536
// and -1 Illegal function call. Guards a frequency below the range sent to the SID. Seen failing with the 37 check
// one low.
TEST(sound_s_ranges_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "SOUND 36,1"), std::string("Illegal function call"));
    CHECK(s.entries.empty());
    CHECK_EQ(said(r, "SOUND 37,1"), std::string());
    CHECK_EQ(said(r, "SOUND 32767,1"), std::string());
    CHECK_EQ(said(r, "SOUND 440,65536"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "SOUND 440,-1"), std::string("Illegal function call"));
    REQUIRE(s.entries.size() == 2u);
    CHECK_EQ(s.entries[0].freq, pal(37));
    CHECK_EQ(s.entries[1].freq, pal(32767)); // (held at $FFFF)
    CHECK_EQ(s.entries[0].len, units_ms(22));
    r.run(ms(200));
    CHECK_EQ(said(r, "SOUND 440,65535"), std::string());
    r.watch = nullptr;
    REQUIRE(s.entries.size() == 5u);
    CHECK_EQ(s.entries[2].len, 65535u);
    CHECK_EQ(s.entries[3].len, 65535u);
    CHECK_EQ(s.entries[4].len, units_ms(65535 - 2 * 26214));
    CHECK(s.entries[4].at - s.entries[2].at < ms(50)); // back to back, the first waited for alone
}

// N18 (the owner's answer 6): PLAY's and SOUND's MF wait. SOUND 440,9.1 (500 ms) three times: each after the first
// is sent as the one before ends; ^C during the third: Ok (the break), MU_STOP of voice 0 (SNDRST's) and the gate
// off; a note's end lost (the EK_SOUND events dropped by the watch's own hand: the queue's tail moved past them)
// still lets the next go, by the clock (the queue's end + 100 ms, then MU_STOP). Guards ^C leaving a tone on; a
// program hung for ever on a lost event. Seen failing with (a) SNDRST left out of the break, (b) the clock's end of
// the wait left out.
TEST(play_mf_waits_and_ctrl_c_breaks_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    run_prog(r, {"10 SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1"}, ms(1200));
    press(r, 0x06, CTRL); // ^C, as the third plays
    CHECK(until_row(r, "Break") >= 0);
    const size_t n = s.entries.size();
    r.run(ms(800));
    CHECK_EQ(s.entries.size(), n);
    CHECK(n >= 3);
    REQUIRE(!s.stops.empty());
    CHECK(s.stops.back().first > s.entries[n - 1].at);
    CHECK_EQ(card(r).sid(0)[4] & 1, 0);
    for (size_t i = 1; i < n; ++i)
    {
        const double gap = as_ms(s.entries[i].at - s.entries[i - 1].at);
        CHECK(gap > 480 && gap < 560);
    }
    // The lost end: every EK_SOUND event taken off the queue as it comes, before GW-BASIC sees it.
    const std::map<std::string, uint16_t> lab = lbl_map();
    REQUIRE(lab.count("EVAREA") == 1);
    const uint16_t ev = lab.at("EVAREA");
    Sound t;
    t.watch(r);
    const auto snd = r.watch;
    bool lose = true;
    r.watch = [&](uint16_t pc) {
        snd(pc);
        Bus &bus = r.machine->bus();
        const uint8_t tail = bus.peek(static_cast<uint16_t>(ev + 17)), head = bus.peek(static_cast<uint16_t>(ev + 16));
        if (lose && tail != head && bus.peek(static_cast<uint16_t>(ev + 128 + tail * 8)) == 6) // EK_SOUND
            bus.poke(static_cast<uint16_t>(ev + 17), static_cast<uint8_t>((tail + 1) % bus.peek(static_cast<uint16_t>(ev + 18))));
    };
    run_prog(r, {"10 SOUND 440,9.1:SOUND 440,9.1"}, ms(1500));
    r.watch = nullptr;
    REQUIRE(t.entries.size() == 2u);
    const double gap = as_ms(t.entries[1].at - t.entries[0].at);
    std::printf("  with the note's end lost, the next sent after %.1f ms (500 + 100 by the clock)\n", gap);
    CHECK(gap > 590 && gap < 660);
}

// N19 (the owner's answer 5): after RUN, NEW, CLEAR and SYSTEM the 6510 is given back (SN_RELEASE; CTRL's RUN off),
// and PLAY's state is the defaults again (SNDINI's: here MB's left by a run gone, so the next run's SOUND waits as
// MF). Guards a .sid player refused after GW-BASIC; a new run inheriting the last one's mode. Seen failing with
// CLEARC's SNDINI not called (INITRP's far call left out).
TEST(run_and_system_free_the_6510)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    for (const char *what : {"RUN", "NEW", "CLEAR"})
    {
        CHECK_EQ(said(r, "SOUND 440,2"), std::string());
        CHECK(running(r));
        const unsigned before = s.releases;
        if (std::string(what) == "RUN")
            run_prog(r, {"10 REM"}, ms(300));
        else
            enter(r, what);
        CHECK_EQ(s.releases, before + 1);
        CHECK(!running(r));
    }
    CHECK_EQ(said(r, "SOUND 440,2"), std::string());
    CHECK(running(r));
    const size_t from = r.out.size();
    enter(r, "SYSTEM");
    r.watch = nullptr;
    CHECK(r.until("A>", ms(3000), from));
    CHECK(!running(r));
}

// N20 (the owner's answer 3): BEEP and PRINT CHR$(7) in screen mode, and BEEP in console mode (test_gwbasic_core's
// G11 case), sound the bell by function 82's SN_BEEP, 800 Hz for 250 ms, and claim nothing (no SN_LOAD). Guards
// BEEP silent. Seen failing with ^G's route left out of P6 (CBEEP's far call).
TEST(beep_and_ctrl_g_sound_the_bell)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "BEEP"), std::string());
    CHECK_EQ(said(r, "PRINT CHR$(7);"), std::string());
    r.watch = nullptr;
    REQUIRE(s.beeps.size() == 2u);
    for (const auto &b : s.beeps)
    {
        CHECK_EQ(b[1] | b[2] << 8, 800);
        CHECK_EQ(b[3] | b[4] << 8, 250);
    }
    CHECK(s.loads.empty());
    CHECK(!running(r));
}

// N38 (7f's plan): the first SOUND loads the driver by SN_LOAD from P7's own block: its source is the physical
// address of mu.s's image (P7's DATA, in W3 while P7 runs: function 73's translation, PHYSOF), and the card's RAM
// at $1000 holds mudrv.bin but for its latch (bytes 3-4, set for the clock) and the driver's own status (5-9). Guards the driver loaded from the wrong
// block: a crashed 6510. Seen failing with PHYSOF given the image's address as a W1 one.
TEST(p7_s_driver_image_loads_from_its_block)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "SOUND 440,2"), std::string());
    r.watch = nullptr;
    REQUIRE(s.loads.size() == 1u);
    const std::vector<uint8_t> drv = file("p7/mudrv.bin");
    REQUIRE(drv.size() > 16);
    const auto &b = s.loads[0];
    CHECK_EQ(b[4] | b[5] << 8, 0x1000);
    CHECK_EQ(static_cast<size_t>(b[6] | b[7] << 8), drv.size());
    unsigned differ = 0;
    for (size_t i = 0; i < drv.size(); ++i)
        if ((i < 3 || i > 9) && card(r).ram()[0x1000 + i] != drv[i]) // (the latch, and the driver's own LOST, TICKS)
            ++differ;
    CHECK_EQ(differ, 0u);
    // The source: P7's block (the loader's PAGBLK), at the image's place in it.
    const std::map<std::string, uint16_t> lab = lbl_map(), p7 = lbl_map("p7.lbl");
    REQUIRE(lab.count("PAGBLK") == 1 && p7.count("image") == 1);
    const uint32_t src = b[1] | b[2] << 8 | static_cast<uint32_t>(b[3]) << 16;
    const uint32_t want = static_cast<uint32_t>(r.phys(lab.at("PAGBLK") + 7u)) * 0x4000u + 0x800u + (p7.at("image") - 0xC800u);
    CHECK_EQ(src, want);
}


int main()
{
    return run_all();
}
