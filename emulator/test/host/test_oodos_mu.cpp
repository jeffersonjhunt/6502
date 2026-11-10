/**
 * @file test_oodos_mu.cpp
 * @brief OOLIB's L6b (hw/oolib.md, "The plan"): the music library (oodos/lib/mu*.s) over L6a's driver.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * MUTEST (oodos/test/mu/mutest.s), run by GO, calls a library routine for each command: a letter at $3900,
 * A, X and Y after it, a block at $3A00. Through OODOS's own calls: MU_INIT's claim, load and start (the
 * card's RAM against the driver's image, its latch for the clock); notes by number on both clocks against
 * the register values worked out here (Hz x 2^24 / the clock); tones in Hz within 0.02% and held at the
 * register's top; the queue's 32 and PE_FULL; the counts from the events MUTEST hands to MU_EVENT (D);
 * markers; MU_STOP's count kept true while the events from before it are still on their way; instruments,
 * volumes, pause; refusals before MU_INIT; MU_QUIT and the warm boot giving the 6510 back. The driver's own
 * timing and races are test_mudrv's.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t MBOX = 0x3900, BUF = 0x3A00, LOGN = 0x3B00, LOG = 0x3C00; ///< mutest.s's
    constexpr uint8_t PE_RANGE = 2, PE_STATE = 3, PE_FULL = 5;                   ///< oolib.inc's
    constexpr uint8_t MV_NONE = 0, MV_DONE = 1, MV_EMPTY = 2, MV_MARK = 3, MV_HELLO = 5;
    constexpr uint8_t REST = 0xFF, ALL = 0xFF;
    constexpr double PAL = 985'248, NTSC = 1'022'727;
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    SoundCard &card(Rig &r) { return *r.machine->sound_card(); }

    void no_splash(Rig &r)
    {
        r.machine->bus().write(0xC489, 0);
        r.run(ms(40));
    }
    void start_mu(CcpRig &r)
    {
        no_splash(r);
        r.poke(MBOX, 0);
        r.poke_com(read_bytes(UTILS + "/mutest.com"));
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("O 00 00", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("O 00 00", from)));
    }
    /// Command `c` with A, X, Y: "aa xx cc".
    std::string mu(CcpRig &r, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0)
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX + 1, std::vector<uint8_t>{a, x, y});
        r.poke(MBOX, static_cast<uint8_t>(c));
        const std::string label(1, c);
        if (!r.until("\n" + label + " ", ms(2000), from))
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
    std::string ac(const std::string &f) { return f.substr(0, 2) + " " + f.substr(6, 2); }
    std::string hex2(unsigned v)
    {
        char b[3];
        std::snprintf(b, sizeof b, "%02X", v & 0xFF);
        return b;
    }
    const std::string OK = "00 00";
    std::string err(uint8_t e) { return hex2(e) + " 01"; }

    /// A note or tone block at BUF: the voice, the pitch (a note in +1, or Hz in +1-2), the length, the gate.
    void block(CcpRig &r, unsigned voice, unsigned pitch, unsigned len, unsigned gate)
    {
        r.poke(BUF, std::vector<uint8_t>{static_cast<uint8_t>(voice), static_cast<uint8_t>(pitch),
                                          static_cast<uint8_t>(pitch >> 8), static_cast<uint8_t>(len),
                                          static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(gate),
                                          static_cast<uint8_t>(gate >> 8)});
    }
    std::string note(CcpRig &r, unsigned voice, unsigned n, unsigned len, unsigned gate)
    {
        block(r, voice, n, len, gate);
        return ac(mu(r, 'N', BUF & 0xFF, BUF >> 8));
    }
    std::string tone(CcpRig &r, unsigned voice, unsigned hz, unsigned len, unsigned gate)
    {
        block(r, voice, hz, len, gate);
        return ac(mu(r, 'T', BUF & 0xFF, BUF >> 8));
    }
    unsigned count(CcpRig &r, unsigned voice)
    {
        return static_cast<unsigned>(std::stoul(mu(r, 'C', static_cast<uint8_t>(voice)).substr(0, 2), nullptr, 16));
    }

    struct Ev
    {
        uint8_t kind, voice, mark;
        bool operator==(const Ev &o) const { return kind == o.kind && voice == o.voice && mark == o.mark; }
    };
    /// D: the events waiting, each through MU_EVENT, as MUTEST logged them (all of them since the start).
    std::vector<Ev> drain(CcpRig &r)
    {
        mu(r, 'D');
        const unsigned n = r.phys(LOGN) | r.phys(LOGN + 1) << 8;
        std::vector<Ev> out;
        for (unsigned i = 0; i < n; ++i)
            out.push_back({r.phys(LOG + 3 * i), r.phys(LOG + 3 * i + 1), r.phys(LOG + 3 * i + 2)});
        return out;
    }
    /// `total` ms of the machine, the events handed over every 20 ms as a program would (the area's queue holds 15).
    std::vector<Ev> run_draining(CcpRig &r, unsigned total)
    {
        std::vector<Ev> all;
        for (unsigned t = 0; t < total; t += 20)
        {
            r.run(ms(20));
            all = drain(r);
        }
        return all;
    }
    std::vector<Ev> since(const std::vector<Ev> &all, size_t from)
    {
        return std::vector<Ev>(all.begin() + static_cast<long>(std::min(from, all.size())), all.end());
    }

    /// Voice v's frequency register and control as the 6510 wrote them.
    unsigned freq(Rig &r, unsigned v)
    {
        const auto &s = card(r).sid(v / 3);
        const unsigned o = 7 * (v % 3);
        return s[o] | s[o + 1] << 8;
    }
    uint8_t ctrl(Rig &r, unsigned v) { return card(r).sid(v / 3)[7 * (v % 3) + 4]; }
    unsigned want_note(unsigned n, double clock)
    {
        const double v = 440.0 * std::pow(2.0, (static_cast<double>(n) - 57) / 12) * 16'777'216 / clock;
        return static_cast<unsigned>(std::min(65'535.0, std::floor(v + 0.5)));
    }

    /// A rig with MUTEST going, its event area open, and the driver started on `clock` (0 PAL, 1 NTSC).
    void ready(CcpRig &r, uint8_t clock = 0)
    {
        start_mu(r);
        CHECK_EQ(ac(mu(r, 'O')), OK);
        CHECK_EQ(ac(mu(r, 'I', clock)), OK);
        r.run(ms(10)); // the driver's own start
    }
} // namespace

TEST(mu_init_loads_and_starts_the_driver_for_its_clock)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_mu(r);
    CHECK_EQ(ac(mu(r, 'N', BUF & 0xFF, BUF >> 8)), err(PE_STATE)); // nothing before MU_INIT
    CHECK_EQ(ac(mu(r, 'X', 0)), err(PE_STATE));
    CHECK_EQ(ac(mu(r, 'Q')), err(PE_STATE));
    CHECK_EQ(ac(mu(r, 'I', 2)), err(PE_RANGE));
    CHECK_EQ(ac(mu(r, 'I', 0)), OK);
    r.run(ms(10));
    const std::vector<uint8_t> image = read_bytes(UTILS + "/mudrv.bin");
    REQUIRE(image.size() > 6);
    bool same = true;
    for (size_t i = 0; i < image.size(); ++i)
        if (i != 3 && i != 4 && i != 6 && i != 7 && i != 8 && i != 9 && card(r).ram()[0x1000 + i] != image[i])
            same = false; // the latch is set for the clock; the status block moves as it runs
    CHECK(same);
    CHECK_EQ(card(r).ram()[0x1003] | card(r).ram()[0x1004] << 8, 984);
    CHECK_EQ(card(r).timer_latch(), 984);
    CHECK_EQ(card(r).peek(SoundCard::CTRL) & (SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC), SoundCard::CTRL_RUN);
    CHECK_EQ(ac(mu(r, 'I', 0)), err(PE_STATE)); // once
    // Quit: the 6510 stopped and given back; a fresh start on NTSC.
    CHECK_EQ(ac(mu(r, 'Q')), OK);
    CHECK_EQ(card(r).peek(SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
    CHECK_EQ(ac(mu(r, 'I', 1)), OK);
    r.run(ms(10));
    CHECK_EQ(card(r).timer_latch(), 1022);
    CHECK_EQ(card(r).peek(SoundCard::CTRL) & (SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC),
             SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC);
}

TEST(a_note_plays_and_its_events_keep_the_count)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(note(r, 0, 57, 100, 87), OK); // A-4
    CHECK_EQ(count(r, 0), 1u);
    r.run(ms(20));
    CHECK_EQ(freq(r, 0), want_note(57, PAL));
    CHECK_EQ(ctrl(r, 0), 0x41);
    r.run(ms(120));
    CHECK_EQ(ctrl(r, 0), 0x40);
    const std::vector<Ev> ev = drain(r);
    CHECK(ev == (std::vector<Ev>{{MV_DONE, 0, 0}, {MV_EMPTY, 0, 0}}));
    CHECK_EQ(count(r, 0), 0u);
    CHECK_EQ(note(r, 6, 57, 10, 10), err(PE_RANGE));
    CHECK_EQ(note(r, 0, 96, 10, 10), err(PE_RANGE));
}

TEST(every_note_on_both_clocks)
{
    for (uint8_t clock = 0; clock < 2; ++clock)
    {
        CcpRig r;
        REQUIRE(r.ok);
        ready(r, clock);
        unsigned wrong = 0;
        for (unsigned n = 0; n < 96; ++n)
        {
            CHECK_EQ(note(r, 1, n, 2, 2), OK);
            r.run(ms(4));
            if (n % 8 == 7)
                drain(r); // the counts kept down, as a program hands its events over
            if (freq(r, 1) != want_note(n, clock ? NTSC : PAL))
            {
                if (!wrong)
                    std::printf("  note %u on %s: %04X, want %04X\n", n, clock ? "NTSC" : "PAL", freq(r, 1),
                                want_note(n, clock ? NTSC : PAL));
                ++wrong;
            }
        }
        CHECK_EQ(wrong, 0u);
        CHECK_EQ(freq(r, 1), clock ? want_note(95, NTSC) : 0xFFFFu); // PAL's B-7 held at the top
    }
}

TEST(a_tone_in_hz_and_a_rest)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    for (const unsigned hz : {37u, 440u, 1000u, 3800u})
    {
        CHECK_EQ(tone(r, 2, hz, 2, 2), OK);
        r.run(ms(4));
        const double want = hz * 16'777'216.0 / PAL;
        CHECK(std::fabs(freq(r, 2) - want) <= want * 0.0002 + 1);
    }
    CHECK_EQ(tone(r, 2, 32'767, 2, 2), OK); // SOUND's top: the register's
    r.run(ms(4));
    CHECK_EQ(freq(r, 2), 0xFFFFu);
    CHECK_EQ(tone(r, 2, 440, 30, 30), OK);
    CHECK_EQ(tone(r, 2, 0, 30, 30), OK); // a rest: silent, the frequency left
    r.run(ms(45));
    CHECK_EQ(ctrl(r, 2) & 1, 0);
    CHECK(std::fabs(freq(r, 2) - 440 * 16'777'216.0 / PAL) < 2);
    CHECK_EQ(note(r, 2, REST, 10, 10), OK);
}

TEST(the_queue_holds_32_and_says_pe_full)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(ac(mu(r, 'P')), OK);
    CHECK_EQ(card(r).sid(0)[0x18], 0);
    for (unsigned i = 0; i < 32; ++i)
        CHECK_EQ(note(r, 3, 40 + i % 12, 3, 3), OK);
    CHECK_EQ(note(r, 3, 40, 3, 3), err(PE_FULL));
    CHECK_EQ(ac(mu(r, 'M', 3, 1)), err(PE_FULL));
    CHECK_EQ(count(r, 3), 32u);
    CHECK_EQ(ac(mu(r, 'R')), OK);
    CHECK_EQ(card(r).sid(1)[0x18], 15);
    const std::vector<Ev> ev = run_draining(r, 160);
    std::vector<Ev> want(32, Ev{MV_DONE, 3, 0});
    want.push_back({MV_EMPTY, 3, 0});
    CHECK(ev == want);
    CHECK_EQ(count(r, 3), 0u);
}

TEST(a_marker_comes_back_through_mu_event)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(note(r, 4, 60, 30, 30), OK);
    CHECK_EQ(ac(mu(r, 'M', 4, 7)), OK);
    CHECK_EQ(note(r, 4, 62, 30, 30), OK);
    CHECK_EQ(count(r, 4), 3u);
    CHECK_EQ(ac(mu(r, 'M', 4, 15)), err(PE_RANGE)); // the library's own
    r.run(ms(80));
    const std::vector<Ev> ev = drain(r);
    CHECK(ev == (std::vector<Ev>{{MV_DONE, 4, 0}, {MV_MARK, 4, 7}, {MV_DONE, 4, 0}, {MV_EMPTY, 4, 0}}));
    CHECK_EQ(count(r, 4), 0u);
}

TEST(stop_keeps_the_count_true_while_its_leftovers_arrive)
{
    // Five notes on voice 1; two finish, their events not yet handed over; STOP; a new note. The two old
    // "finished" and STOP's own "empty" arrive after the new note is counted: the count must stay 1 until the new
    // note itself finishes. (Without the drain the old events take it below; zeroed at an "empty", to 0.)
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    for (unsigned i = 0; i < 5; ++i)
        CHECK_EQ(note(r, 1, 50, 40, 40), OK);
    r.run(ms(95));
    CHECK_EQ(count(r, 1), 5u); // nothing handed over yet
    CHECK_EQ(ac(mu(r, 'X', 1)), OK);
    CHECK_EQ(count(r, 1), 0u);
    r.run(ms(3));
    CHECK_EQ(ctrl(r, 1) & 1, 0);
    CHECK_EQ(note(r, 1, 52, 40, 40), OK);
    CHECK_EQ(count(r, 1), 1u);
    r.run(ms(10));
    const std::vector<Ev> before = drain(r);
    CHECK_EQ(count(r, 1), 1u);
    // The leftovers let go (those finished, STOP's empty, marker 15), then the driver's empty when it reached
    // marker 15, before the new note came: said, but the count stays the new note's.
    REQUIRE(before.size() >= 5); // at least two finished (sending the five takes time too)
    for (size_t i = 0; i + 1 < before.size(); ++i)
        CHECK_EQ(before[i].kind, MV_NONE);
    CHECK(before.back() == (Ev{MV_EMPTY, 1, 0}));
    r.run(ms(50));
    const std::vector<Ev> after = since(drain(r), before.size());
    CHECK(after == (std::vector<Ev>{{MV_DONE, 1, 0}, {MV_EMPTY, 1, 0}}));
    CHECK_EQ(count(r, 1), 0u);
    // MU_ALL: every voice stopped and counted 0.
    CHECK_EQ(note(r, 0, 50, 500, 500), OK);
    CHECK_EQ(note(r, 5, 50, 500, 500), OK);
    r.run(ms(5));
    CHECK_EQ(ac(mu(r, 'X', ALL)), OK);
    r.run(ms(5));
    CHECK_EQ(count(r, 0), 0u);
    CHECK_EQ(count(r, 5), 0u);
    CHECK_EQ(ctrl(r, 0) & 1, 0);
    CHECK_EQ(ctrl(r, 5) & 1, 0);
    CHECK_EQ(ac(mu(r, 'X', 6)), err(PE_RANGE));
}

TEST(an_instrument_and_the_volumes)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    r.poke(BUF, std::vector<uint8_t>{2, 0x29, 0x11, 0x22, 0x00, 0x04}); // saw (the gate and TEST bits dropped)
    CHECK_EQ(ac(mu(r, 'S', BUF & 0xFF, BUF >> 8)), OK);
    CHECK_EQ(note(r, 2, 57, 20, 20), OK);
    r.run(ms(10));
    const auto &a = card(r).sid(0);
    CHECK_EQ(ctrl(r, 2), 0x21);
    CHECK_EQ(a[14 + 5], 0x11);
    CHECK_EQ(a[14 + 6], 0x22);
    CHECK_EQ(a[14 + 2] | a[14 + 3] << 8, 0x0400);
    CHECK_EQ(ac(mu(r, 'V', 0, 5)), OK);
    r.run(ms(2));
    CHECK_EQ(card(r).sid(0)[0x18], 5);
    CHECK_EQ(card(r).sid(1)[0x18], 15);
    CHECK_EQ(ac(mu(r, 'V', 1, 7)), OK);
    r.run(ms(2));
    CHECK_EQ(card(r).sid(1)[0x18], 7);
    CHECK_EQ(ac(mu(r, 'V', ALL, 9)), OK);
    r.run(ms(2));
    CHECK_EQ(card(r).sid(0)[0x18], 9);
    CHECK_EQ(card(r).sid(1)[0x18], 9);
    CHECK_EQ(ac(mu(r, 'V', 2, 9)), err(PE_RANGE));
    CHECK_EQ(ac(mu(r, 'V', 0, 16)), err(PE_RANGE));
    r.poke(BUF, std::vector<uint8_t>{6, 0x20, 0, 0, 0, 0});
    CHECK_EQ(ac(mu(r, 'S', BUF & 0xFF, BUF >> 8)), err(PE_RANGE));
}

TEST(mu_event_reads_each_kind_of_byte)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(mu(r, 'E', 0x01).substr(0, 5), hex2(MV_HELLO) + " FF");
    CHECK_EQ(mu(r, 'E', 0x37).substr(0, 2), hex2(MV_NONE)); // a command refused: no voice's
    CHECK_EQ(mu(r, 'E', 0x00).substr(0, 2), hex2(MV_NONE));
    CHECK_EQ(mu(r, 'E', 0x25).substr(0, 5), hex2(MV_EMPTY) + " 05");
    CHECK_EQ(mu(r, 'E', 0x80 | 2 << 4 | 9).substr(0, 5), hex2(MV_MARK) + " 02");
    CHECK_EQ(mu(r, 'E', 0xE0).substr(0, 2), hex2(MV_NONE)); // voice 6: none
}

TEST(the_warm_boot_gives_the_6510_back)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(note(r, 0, 57, 2000, 2000), OK);
    r.run(ms(10));
    CHECK_EQ(ctrl(r, 0), 0x41);
    const size_t from = r.out.size();
    r.poke(MBOX, 'W');
    CHECK(r.until("A>", ms(3000), from));
    CHECK_EQ(card(r).peek(SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
    // And a new program may claim it.
    start_mu(r);
    CHECK_EQ(ac(mu(r, 'I', 0)), OK);
}

int main() { return run_all(); }
