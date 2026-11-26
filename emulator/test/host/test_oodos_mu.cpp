/**
 * @file test_oodos_mu.cpp
 * @brief OOLIB's L6b (hw/oolib.md, "The plan"): the music library (oodos/lib/mu*.s) on sound card revision 3: the
 * card's music sequencer, note blocks sent by function 82's SN_MUSIC.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * MUTEST (oodos/test/mu/mutest.s), run by GO, calls a library routine for each command: a letter at $3900, A, X
 * and Y after it, a block at $3A00. Through OODOS's own calls: MU_INIT's claim of music (nothing loaded); notes by
 * number and tones in Hz, each a block of one sent at once, against the SID register the card writes (Hz x 16 x
 * 2^20 / the clock); the card's eight blocks and PE_FULL; the counts from the card (MU_COUNT: SN_MSTATUS); the
 * card's events handed to MU_EVENT (D): a marker, the queue's end, MU_THRESH's count; MU_BLOCK; MU_STOP;
 * instruments and volumes; refusals before MU_INIT; MU_QUIT and the warm boot giving music back. The card's own
 * sequencer is test_sound_music's.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card3.h"
#include "sound_fw.h"

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
    constexpr uint32_t MBOX = 0x3900, BUF = 0x3A00, NBLK = 0x3D00, LOGN = 0x3B00, LOG = 0x3C00; ///< mutest.s's
    constexpr uint8_t PE_RANGE = 2, PE_STATE = 3, PE_REFUSED = 4, PE_FULL = 5;                   ///< oolib.inc's
    constexpr uint8_t MV_NONE = 0, MV_DONE = 1, MV_MARK = 3, MV_COUNT = 6;
    constexpr uint8_t SK_MUSIC_MARK = 0x40, SK_MUSIC_COUNT = 0x41, SK_MUSIC_DONE = 0x42;
    constexpr uint8_t REST = 0xFF, ALL = 0xFF, SC_MUSIC = 4;
    constexpr double PAL = 985'248;
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    SoundCard3 &dev(Rig &r) { return *r.machine->sound_device(); }
    SoundFw &fw(Rig &r) { return dev(r).firmware(); }

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
    /// MU_COUNT: "aa xx": voice's notes waiting, music's state.
    std::string count(CcpRig &r, unsigned voice) { return mu(r, 'C', static_cast<uint8_t>(voice)).substr(0, 5); }

    struct Ev
    {
        uint8_t kind, voice, mark; ///< MU_EVENT's A, X, Y
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
    /// The card's SID voice v (0-5: A's three, B's): its frequency register and control, from the shadows.
    unsigned freq(Rig &r, unsigned v)
    {
        const auto &s = dev(r).chips().sid(v / 3);
        const unsigned o = 7 * (v % 3);
        return s[o] | s[o + 1] << 8;
    }
    uint8_t ctrl(Rig &r, unsigned v) { return dev(r).chips().sid(v / 3)[7 * (v % 3) + 4]; }
    /// The register the card writes for MU_NOTE's note n (the block's n + 12, 69 the A at 440 Hz; Hz x 16 rounded,
    /// then x 2^20 / the clock: PAL's, no tune holding the clock).
    unsigned want_note(unsigned n)
    {
        const double hz16 = std::round(440.0 * 16.0 * std::pow(2.0, (static_cast<double>(n) + 12 - 69) / 12.0));
        return static_cast<unsigned>(std::min(65'535.0, std::floor(hz16 * 1'048'576.0 / PAL)));
    }
    unsigned want_hz(unsigned hz) { return static_cast<unsigned>(std::min(65'535.0, std::floor(hz * 16.0 * 1'048'576.0 / PAL))); }

    /// A rig with MUTEST going, its event area open, and music claimed.
    void ready(CcpRig &r, uint8_t clock = 0)
    {
        start_mu(r);
        CHECK_EQ(ac(mu(r, 'O')), OK);
        CHECK_EQ(ac(mu(r, 'I', clock)), OK);
    }
} // namespace

TEST(mu_init_claims_music_and_loads_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_mu(r);
    CHECK_EQ(ac(mu(r, 'N', BUF & 0xFF, BUF >> 8)), err(PE_STATE)); // nothing before MU_INIT
    CHECK_EQ(ac(mu(r, 'X', 0)), err(PE_STATE));
    CHECK_EQ(ac(mu(r, 'Q')), err(PE_STATE));
    CHECK_EQ(ac(mu(r, 'H', 2)), err(PE_STATE));
    CHECK_EQ(ac(mu(r, 'I', 2)), err(PE_RANGE));
    const size_t free0 = fw(r).store_free();
    CHECK_EQ(ac(mu(r, 'I', 0)), OK);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SCLAIM")), SC_MUSIC);
    CHECK_EQ(fw(r).store_free(), free0); // nothing loaded
    CHECK_EQ(ac(mu(r, 'I', 0)), err(PE_STATE)); // once
    CHECK_EQ(ac(mu(r, 'Q')), OK);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SCLAIM")), 0);
    CHECK_EQ(ac(mu(r, 'I', 1)), OK); // NTSC's: kept, the card's music is in Hz and µs
}

TEST(a_note_plays_as_a_block_of_one_and_the_card_counts_it)
{
    // MU_NOTE sends its note at once (the owner's answer 9): it sounds on a SID voice of the card at its register,
    // gated, and ends; a second queued behind it is counted waiting by the card (MU_COUNT), exactly. Must fail with
    // MU_COUNT reading another voice's count.
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(note(r, 0, 57, 100, 87), OK); // A-4
    CHECK_EQ(note(r, 0, 60, 100, 87), OK); // C-5, queued
    CHECK_EQ(count(r, 0), std::string("01 01")); // one waiting, music playing
    CHECK_EQ(count(r, 1), std::string("00 01"));
    r.run(ms(20));
    CHECK_EQ(freq(r, 0), want_note(57));
    CHECK_EQ(ctrl(r, 0), 0x41); // the default pulse, gated
    r.run(ms(100));
    CHECK_EQ(freq(r, 0), want_note(60));
    CHECK_EQ(count(r, 0), std::string("00 01"));
    r.run(ms(120));
    CHECK_EQ(ctrl(r, 0) & 1, 0);
    CHECK_EQ(count(r, 0), std::string("00 00")); // stopped: the queue's end
    const std::vector<Ev> ev = drain(r);
    REQUIRE(!ev.empty());
    CHECK_EQ(ev.back().kind, MV_DONE);
    CHECK_EQ(note(r, 6, 57, 10, 10), err(PE_RANGE));
    CHECK_EQ(note(r, 0, 96, 10, 10), err(PE_RANGE));
}

TEST(every_note_at_its_register)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    unsigned wrong = 0;
    for (unsigned n = 0; n < 96; ++n)
    {
        CHECK_EQ(note(r, 1, n, 2, 2), OK);
        r.run(ms(1));
        if (freq(r, 0) != want_note(n))
        {
            if (!wrong)
                std::printf("  note %u: %04X, want %04X\n", n, freq(r, 0), want_note(n));
            ++wrong;
        }
        r.run(ms(4));
    }
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(freq(r, 0), 0xFFFFu); // PAL's B-7 held at the top
}

TEST(a_tone_in_hz_and_a_rest)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    for (const unsigned hz : {37u, 440u, 1000u, 3800u})
    {
        CHECK_EQ(tone(r, 2, hz, 2, 2), OK);
        r.run(ms(1));
        CHECK_EQ(freq(r, 0), want_hz(hz));
        r.run(ms(4));
    }
    CHECK_EQ(tone(r, 2, 32'767, 2, 2), OK); // SOUND's top: the register's
    r.run(ms(1));
    CHECK_EQ(freq(r, 0), 0xFFFFu);
    r.run(ms(4));
    CHECK_EQ(tone(r, 2, 440, 30, 30), OK);
    CHECK_EQ(tone(r, 2, 0, 30, 30), OK); // a rest: silent, the frequency left
    r.run(ms(45));
    CHECK_EQ(ctrl(r, 0) & 1, 0);
    CHECK_EQ(freq(r, 0), want_hz(440));
    CHECK_EQ(note(r, 2, REST, 10, 10), OK);
}

TEST(thirty_two_blocks_fill_the_card_and_the_thirty_third_is_pe_full)
{
    // The card holds 32 blocks (review C6: 8 before), the one playing and those queued: a 33rd note is PE_FULL, a
    // marker too, and once the first has ended there is room again. Must fail with SNDBLK's test of the queue against
    // all 32 slots (the card then refuses the load: PE_REFUSED).
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    for (unsigned i = 0; i < 32; ++i)
        CHECK_EQ(note(r, 3, 40 + i, 2000, 2000), OK);
    CHECK_EQ(note(r, 3, 40, 2000, 2000), err(PE_FULL));
    CHECK_EQ(ac(mu(r, 'M', 3, 1)), err(PE_FULL));
    CHECK_EQ(count(r, 3), std::string("1F 01")); // 31 waiting, the first sounding
    r.run(ms(2020));
    CHECK_EQ(note(r, 3, 40, 200, 200), OK);
    CHECK_EQ(ac(mu(r, 'X', ALL)), OK);
    CHECK_EQ(count(r, 3), std::string("00 00"));
}

TEST(a_marker_comes_back_through_mu_event)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(note(r, 4, 60, 30, 30), OK);
    CHECK_EQ(ac(mu(r, 'M', 4, 7)), OK);
    CHECK_EQ(note(r, 4, 62, 30, 30), OK);
    CHECK_EQ(ac(mu(r, 'M', 4, 15)), err(PE_RANGE));
    r.run(ms(100));
    const std::vector<Ev> ev = drain(r);
    CHECK(ev == (std::vector<Ev>{{MV_MARK, 4, 7}, {MV_DONE, ev.empty() ? uint8_t{0} : ev.back().voice, 0}}));
}

TEST(a_whole_block_and_the_threshold_s_count)
{
    // MU_BLOCK: four tones in one note block the program built, queued as one; MU_THRESH 2: a MUSIC_COUNT (MV_COUNT)
    // as the notes waiting fall from 2 to 1, then the queue's end (MV_DONE). Must fail with MU_THRESH sending
    // another of SN_MCTL's operations.
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    std::vector<uint8_t> blk = {1, 1, 0, 0, 4, 0, 0, 0, 0, 0x20, 0x00, 0xF0, 0x00, 0x08, 255, 128};
    for (const unsigned hz : {300u, 400u, 500u, 600u})
    {
        const uint32_t f = hz * 16, us = 40'000;
        blk.insert(blk.end(), {0x10, static_cast<uint8_t>(f), static_cast<uint8_t>(f >> 8), static_cast<uint8_t>(f >> 16)});
        for (const uint32_t v : {us, us})
            blk.insert(blk.end(), {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)});
    }
    r.poke(NBLK, blk);
    r.poke(BUF, std::vector<uint8_t>{NBLK & 0xFF, NBLK >> 8, static_cast<uint8_t>(blk.size()), 0});
    CHECK_EQ(ac(mu(r, 'H', 2)), OK);
    CHECK_EQ(ac(mu(r, 'B', BUF & 0xFF, BUF >> 8)), OK);
    CHECK_EQ(count(r, 0), std::string("03 01"));
    r.run(ms(5));
    CHECK_EQ(freq(r, 0), want_hz(300));
    CHECK_EQ(ctrl(r, 0), 0x21);
    r.run(ms(200));
    const std::vector<Ev> ev = drain(r);
    CHECK(ev == (std::vector<Ev>{{MV_COUNT, 0, 0}, {MV_DONE, ev.empty() ? uint8_t{0} : ev.back().voice, 0}}));
    blk[1] = 2; // a block that doesn't parse (the entries' voice outside the mask): the card's E_DATA
    r.poke(NBLK, blk);
    CHECK_EQ(ac(mu(r, 'B', BUF & 0xFF, BUF >> 8)), err(PE_REFUSED));
}

TEST(stop_empties_the_queue_and_the_count)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    for (unsigned i = 0; i < 5; ++i)
        CHECK_EQ(note(r, 1, 50, 400, 400), OK); // (long enough that none ends while the five are sent)
    r.run(ms(5));
    CHECK_EQ(count(r, 1), std::string("04 01"));
    CHECK_EQ(ac(mu(r, 'X', 1)), OK);
    CHECK_EQ(count(r, 1), std::string("00 00"));
    CHECK_EQ(ctrl(r, 0) & 1, 0);
    CHECK_EQ(note(r, 1, 52, 40, 40), OK); // a new note after it plays, alone
    CHECK_EQ(count(r, 1), std::string("00 01"));
    r.run(ms(60));
    CHECK_EQ(count(r, 1), std::string("00 00"));
    CHECK_EQ(note(r, 0, 50, 500, 500), OK);
    CHECK_EQ(note(r, 5, 50, 500, 500), OK);
    CHECK_EQ(ac(mu(r, 'X', ALL)), OK);
    CHECK_EQ(count(r, 5), std::string("00 00"));
    CHECK_EQ(ac(mu(r, 'X', 6)), err(PE_RANGE));
}

TEST(an_instrument_and_the_volumes)
{
    // MU_INSTR's waveform and ADSR and pulse width on the voice's next note; MU_VOLUME's volume as the chip's while
    // music holds it (the card gives a block the first free SID voice: SID A's voice 0 here).
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    r.poke(BUF, std::vector<uint8_t>{2, 0x21, 0x11, 0x22, 0x00, 0x04}); // saw
    CHECK_EQ(ac(mu(r, 'S', BUF & 0xFF, BUF >> 8)), OK);
    CHECK_EQ(note(r, 2, 57, 20, 20), OK);
    r.run(ms(5));
    const auto &a = dev(r).chips().sid(0);
    CHECK_EQ(ctrl(r, 0), 0x21);
    CHECK_EQ(a[5], 0x11);
    CHECK_EQ(a[6], 0x22);
    CHECK_EQ(a[2] | a[3] << 8, 0x0400);
    CHECK_EQ(a[0x18] & 0x0F, 15); // full at the start
    r.run(ms(30));
    CHECK_EQ(ac(mu(r, 'V', 0, 5)), OK); // SID A's voices (0-2)
    CHECK_EQ(note(r, 2, 57, 20, 20), OK);
    r.run(ms(5));
    CHECK_EQ(dev(r).chips().sid(0)[0x18] & 0x0F, 5);
    r.run(ms(30));
    CHECK_EQ(ac(mu(r, 'V', 1, 7)), OK); // SID B's (3-5)
    CHECK_EQ(note(r, 3, 57, 20, 20), OK);
    r.run(ms(5));
    CHECK_EQ(dev(r).chips().sid(0)[0x18] & 0x0F, 7);
    r.run(ms(30));
    CHECK_EQ(ac(mu(r, 'V', ALL, 9)), OK);
    CHECK_EQ(note(r, 2, 57, 20, 20), OK);
    r.run(ms(5));
    CHECK_EQ(dev(r).chips().sid(0)[0x18] & 0x0F, 9);
    CHECK_EQ(ac(mu(r, 'V', 2, 9)), err(PE_RANGE));
    CHECK_EQ(ac(mu(r, 'V', 0, 16)), err(PE_RANGE));
    r.poke(BUF, std::vector<uint8_t>{6, 0x20, 0, 0, 0, 0});
    CHECK_EQ(ac(mu(r, 'S', BUF & 0xFF, BUF >> 8)), err(PE_RANGE));
}

TEST(mu_event_reads_each_kind_of_event)
{
    CcpRig r;
    REQUIRE(r.ok);
    ready(r);
    CHECK_EQ(mu(r, 'E', SK_MUSIC_DONE, 3).substr(0, 5), hex2(MV_DONE) + " 03");
    CHECK_EQ(mu(r, 'E', SK_MUSIC_MARK, 2 << 4 | 9).substr(0, 5), hex2(MV_MARK) + " 02");
    CHECK_EQ(mu(r, 'E', SK_MUSIC_COUNT, 0).substr(0, 2), hex2(MV_COUNT));
    CHECK_EQ(mu(r, 'E', 0x60, 0).substr(0, 2), hex2(MV_NONE)); // a beep's end: not music's
    CHECK_EQ(mu(r, 'E', 0x23, 1).substr(0, 2), hex2(MV_NONE));
}

TEST(the_warm_boot_gives_music_back)
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
    CHECK_EQ(fw(r).music_state(), SoundFw::M_STOPPED);
    start_mu(r); // and a new program may claim it
    CHECK_EQ(ac(mu(r, 'I', 0)), OK);
}

TEST(mu_init_refuses_music_claimed_already_and_leaves_the_claim)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_mu(r);
    r.poke(BUF, std::vector<uint8_t>{1, SC_MUSIC}); // SN_CLAIM
    CHECK_EQ(mu(r, 'K', BUF & 0xFF, BUF >> 8).substr(0, 2), "00");
    CHECK_EQ(ac(mu(r, 'I', 0)), err(PE_REFUSED));
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SCLAIM")), SC_MUSIC); // still the program's
    r.poke(BUF, std::vector<uint8_t>{2, SC_MUSIC}); // SN_RELEASE
    CHECK_EQ(mu(r, 'K', BUF & 0xFF, BUF >> 8).substr(0, 2), "00");
    CHECK_EQ(ac(mu(r, 'I', 0)), OK);
}

int main() { return run_all(); }
