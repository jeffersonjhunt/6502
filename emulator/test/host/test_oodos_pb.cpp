/**
 * @file test_oodos_pb.cpp
 * @brief OOLIB's L5 (hw/oolib.md, "The plan"): the linked sound library (oodos/lib/pb*.s, sid*.s), on sound card
 * revision 3, the card as a device: every routine through function 82.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * PBTEST (oodos/test/pb/pbtest.s), run by GO, calls a library routine for each command: a letter at $3900, A, X
 * and Y after it, a block at $3A00. L5b's EQ and vis page: what the library leaves in the card (the firmware's EQ
 * and volumes, read by the test as a debugger would); refusals that set nothing; OO-Amp's test of the EQ (a sine
 * at each band's centre, +12 dB from the library, 12 dB +- 1 louder at the card's tap than with the EQ off); the
 * vis page copied, its length kept; a card OODOS doesn't drive (revision 2) answered PE_NOCARD.
 *
 * L5c's SID reads: SID_VOICES against the card's shadows as written; SID_NOTE at both sides of every note's
 * threshold for both clocks; SID_ENV's six levels frame by frame against the rules worked out here.
 *
 * L5d's playback layer, on the card's own sources: a .sid tune on the card's player (its stall the card's: no play
 * call for 2 s, 10 s before the first), a 44.1 kHz stream given by function 82, music (PB_MUSIC); PB_DOC refused.
 * PB_STATUS's states and milliseconds (the card's count, paused time left out), PB_VOLUME's law, PB_LEVELS the
 * card's own measure.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card3.h"
#include "sound_dsp.h"
#include "sound_fw.h"

#include <algorithm>
#include <array>
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
    const bool REV3 = (sound_revision() = 3, true); ///< every rig here: revision 3

    constexpr uint32_t MBOX = 0x3900, BUF = 0x3A00, LOG = 0x3C00; ///< pbtest.s's
    constexpr uint32_t AREA = 0x3800, SBUF = 0x3A80;
    constexpr uint8_t PE_NOCARD = 1, PE_RANGE = 2, PE_STATE = 3, PE_REFUSED = 4; ///< oolib.inc's
    constexpr uint8_t SN_CLAIM = 1, SN_RELEASE = 2, SN_TUNE = 5, SN_PLAY = 6, SN_STOP = 7, SN_STREAM = 12, SN_BUFFER = 13,
                      SN_END = 17, SN_MUSIC = 24, SN_MCTL = 25;
    constexpr uint8_t SC_TUNE = 1, SC_STREAM = 2, SC_MUSIC = 4;
    constexpr uint8_t PS_STOPPED = 0, PS_STARTING = 1, PS_PLAYING = 2, PS_PAUSED = 3, PS_STALLED = 4, PS_ENDED = 5;
    constexpr double PI = 3.14159265358979323846;
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    SoundCard3 &dev(Rig &r) { return *r.machine->sound_device(); }
    SoundFw &fw(Rig &r) { return dev(r).firmware(); }
    /// A SID register written straight to the card's chip (the test's, as a tune would): the card's shadow.
    void sid_reg(Rig &r, unsigned sid, unsigned off, uint8_t v) { dev(r).chips().chip_write(sid, off, v); }

    /// The ROM's boot splash stopped, as test_oodos_gfx does.
    void no_splash(Rig &r)
    {
        r.machine->bus().write(0xC489, 0);
        r.run(ms(40));
    }
    void start_pb(CcpRig &r)
    {
        no_splash(r);
        r.poke(MBOX, 0);
        r.poke_com(read_bytes(UTILS + "/pbtest.com"));
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("O 00 00", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("O 00 00", from)));
    }
    /// Command `c` with A, X, Y: "aa xx cc".
    std::string pb(CcpRig &r, char c, uint8_t a = 0, uint8_t x = 0, uint8_t y = 0)
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX + 1, std::vector<uint8_t>{a, x, y});
        r.poke(MBOX, static_cast<uint8_t>(c));
        const std::string label(1, c);
        if (!r.until("\n" + label + " ", ms(3000), from))
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
    /// "aa cc" of a result: A and the carry (X is the routine's to change).
    std::string ac(const std::string &f) { return f.substr(0, 2) + " " + f.substr(6, 2); }
    std::string hex2(unsigned v)
    {
        char b[3];
        std::snprintf(b, sizeof b, "%02X", v & 0xFF);
        return b;
    }
    const std::string OK = "00 00";
    const std::string NOCARD = hex2(PE_NOCARD) + " 01", RANGE = hex2(PE_RANGE) + " 01";
    const std::string STATE = hex2(PE_STATE) + " 01", REFUSED = hex2(PE_REFUSED) + " 01";

    /// Function 82 with `block` at BUF (K): A as "aa".
    std::string f82(CcpRig &r, const std::vector<uint8_t> &block)
    {
        r.poke(BUF, block);
        return pb(r, 'K', BUF & 0xFF, BUF >> 8).substr(0, 2);
    }
    std::string f82op(CcpRig &r, uint8_t op) { return pb(r, 'k', op).substr(0, 2); }
    std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    std::vector<uint8_t> le24(uint32_t v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16)}; }
    std::vector<uint8_t> u32(uint32_t v)
    {
        return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    }

    /// The EQ as the card holds it: the enable, then the preamp's and the bands' gains.
    std::vector<int> eq_state(Rig &r)
    {
        std::vector<int> out{fw(r).eq_enabled() ? 1 : 0};
        for (unsigned i = 0; i <= 10; ++i)
            out.push_back(fw(r).eq_gain(i));
        return out;
    }
    std::string show(const std::vector<int> &st)
    {
        std::string s = st[0] ? "on:" : "off:";
        for (size_t i = 1; i < st.size(); ++i)
            s += " " + std::to_string(st[i]);
        return s;
    }

    /// A sine at `hz`, `amp` of full scale, `n` frames at 48 kHz, mono 16-bit, as bytes.
    std::vector<uint8_t> sine(size_t n, double hz, double amp)
    {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < n; ++i)
        {
            const auto s = static_cast<int16_t>(std::floor(amp * 32'767.0 * std::sin(2 * PI * hz * static_cast<double>(i) / 48'000) + 0.5));
            out.push_back(static_cast<uint8_t>(s));
            out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
        }
        return out;
    }
    /// `pcm` (48 kHz mono 16-bit, at most 64 KB) played through function 82 by the program (the stream claimed,
    /// opened, one buffer, and the end said unless `to_end` is false). With `to_end`, run to its end and released:
    /// what the card's output tap had, stereo frames.
    std::vector<int16_t> play(CcpRig &r, const std::vector<uint8_t> &pcm, bool to_end = true)
    {
        r.poke(0x10000, pcm);
        CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00"));
        CHECK_EQ(f82(r, {SN_STREAM, 0, 48'000 & 0xFF, 48'000 >> 8, 2}), std::string("00"));
        dev(r).set_tap(true);
        CHECK_EQ(f82(r, std::vector<uint8_t>{SN_BUFFER} + le24(0x10000) + std::vector<uint8_t>{static_cast<uint8_t>(pcm.size()), static_cast<uint8_t>(pcm.size() >> 8)}),
                 std::string("00"));
        if (!to_end)
            return {};
        CHECK_EQ(f82op(r, SN_END), std::string("00"));
        r.run(ms(pcm.size() / 96 + 200));
        CHECK_EQ(fw(r).stream_state(0), SoundFw::S_ENDED);
        CHECK_EQ(f82(r, {SN_RELEASE, SC_STREAM}), std::string("00"));
        std::vector<int16_t> t = dev(r).take_tap();
        dev(r).set_tap(false);
        return t;
    }
    /// The RMS of the left side of stereo `frames`, over its loudest stretch: frames `from` to `to` after the first
    /// one not 0 (the stream's start, wherever the prebuffer put it).
    double rms(const std::vector<int16_t> &frames, size_t from, size_t to)
    {
        size_t at = 0;
        while (2 * at < frames.size() && frames[2 * at] == 0)
            ++at;
        double sum = 0;
        size_t n = 0;
        for (size_t f = at + from; f < at + to && 2 * f < frames.size(); ++f, ++n)
            sum += static_cast<double>(frames[2 * f]) * frames[2 * f];
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0;
    }
} // namespace

// ---- L5b: the EQ and the vis page --------------------------------------------------------------------------

TEST(pb_eq_sets_each_band_and_the_preamp_and_keeps_the_enable)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'N', 1)), OK);
    std::vector<int> want(12, 0);
    want[0] = 1;
    for (unsigned b = 0; b <= 10; ++b)
    {
        const int g = b % 2 ? -static_cast<int>(2 * b + 3) : static_cast<int>(2 * b + 4);
        CHECK_EQ(ac(pb(r, 'E', static_cast<uint8_t>(b), static_cast<uint8_t>(g))), OK);
        want[b + 1] = g;
        CHECK_EQ(show(eq_state(r)), show(want)); // that band, the others as they were, the EQ still on
    }
    CHECK_EQ(ac(pb(r, 'E', 3, 24)), OK); // the ends of the range
    CHECK_EQ(ac(pb(r, 'E', 4, static_cast<uint8_t>(-24))), OK);
    want[4] = 24;
    want[5] = -24;
    CHECK_EQ(show(eq_state(r)), show(want));
}

TEST(a_band_or_gain_out_of_range_is_refused_and_writes_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'E', 2, 10)), OK);
    const std::vector<int> before = eq_state(r);
    for (const auto &[band, gain] : std::vector<std::pair<int, int>>{{11, 0}, {255, 0}, {0x80, 4}, {2, 25}, {2, -25}, {2, 127}, {2, -128}})
        CHECK_EQ(ac(pb(r, 'E', static_cast<uint8_t>(band), static_cast<uint8_t>(gain))), RANGE);
    CHECK_EQ(show(eq_state(r)), show(before));
    std::vector<uint8_t> blk(11); // a block with one gain out of range: none of it set
    for (unsigned i = 0; i < 11; ++i)
        blk[i] = static_cast<uint8_t>(i);
    blk[10] = 25;
    r.poke(BUF, blk);
    CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), RANGE);
    blk[10] = 0;
    blk[0] = static_cast<uint8_t>(-25);
    r.poke(BUF, blk);
    CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), RANGE);
    CHECK_EQ(show(eq_state(r)), show(before));
}

TEST(pb_eqon_turns_the_eq_on_and_off_and_keeps_the_gains)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'E', 7, 12)), OK);
    for (const auto &[a, on] : std::vector<std::pair<uint8_t, int>>{{1, 1}, {0, 0}, {0x55, 1}, {0, 0}, {0xFF, 1}})
    {
        CHECK_EQ(ac(pb(r, 'N', a)), OK);
        const std::vector<int> st = eq_state(r);
        CHECK_EQ(st[0], on);
        CHECK_EQ(st[8], 12);
    }
}

TEST(pb_eqall_writes_the_preamp_and_ten_bands)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    const std::vector<int> gains = {-24, 24, -1, 1, 0, 7, -7, 23, -23, 12, -12};
    std::vector<uint8_t> blk;
    for (const int g : gains)
        blk.push_back(static_cast<uint8_t>(g));
    r.poke(BUF, blk);
    CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), OK);
    std::vector<int> want = {0};
    want.insert(want.end(), gains.begin(), gains.end());
    CHECK_EQ(show(eq_state(r)), show(want)); // and the enable left off
}

TEST(oo_amp_s_test_a_band_at_12_db_from_the_library_is_12_db_louder_at_its_centre)
{
    // A sine at each band's centre, streamed through function 82, the band +12 dB (24 half-dB) by PB_EQALL and the
    // EQ on by PB_EQON: its level at the card's output tap, a quarter second of it, against the same with the EQ off,
    // within 1 dB of 12.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    double worst = 0;
    for (unsigned b = 0; b < BusEq::BANDS; ++b)
    {
        const std::vector<uint8_t> in = sine(24'000, BusEq::CENTRE_HZ[b], 0.1);
        CHECK_EQ(ac(pb(r, 'N', 0)), OK);
        const double flat = rms(play(r, in), 6'000, 18'000);
        std::vector<uint8_t> blk(11, 0);
        blk[b + 1] = 24;
        r.poke(BUF, blk);
        CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), OK);
        CHECK_EQ(ac(pb(r, 'N', 1)), OK);
        const double db = 20 * std::log10(rms(play(r, in), 6'000, 18'000) / flat);
        std::printf("  %5u Hz: %+6.2f dB\n", BusEq::CENTRE_HZ[b], db);
        worst = std::max(worst, std::fabs(db - 12));
        CHECK(std::fabs(db - 12) < 1.0);
        r.poke(BUF, std::vector<uint8_t>(11, 0));
        CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), OK);
    }
    std::printf("  worst: %.3f dB off\n", worst);
}

TEST(pb_vis_copies_the_card_s_page_and_keeps_to_its_length)
{
    // A 3 kHz sine playing: PB_VIS's page shows it (its band, the VU), 98 bytes and nothing past; a length of 34 the
    // bands and the VU alone; 99 or more refused, nothing copied.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    play(r, sine(30'000, 3'000, 0.5), false);
    r.run(ms(200));
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 0)), OK);
    std::vector<uint8_t> got(98);
    for (unsigned i = 0; i < got.size(); ++i)
        got[i] = r.phys(BUF + i);
    CHECK_EQ(r.phys(BUF + 98), 0xEE); // and nothing past it
    unsigned loudest = 0;
    for (unsigned i = 1; i < 32; ++i)
        if (got[i] > got[loudest])
            loudest = i;
    std::printf("  3 kHz: band %u at %u, VU %u %u\n", loudest, got[loudest], got[32], got[33]);
    CHECK(got[loudest] > 150);
    CHECK(got[32] > 150 && got[33] > 150);
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 34)), OK);
    CHECK(r.phys(BUF + 32) > 150);
    CHECK_EQ(r.phys(BUF + 34), 0xEE);
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    for (const uint8_t y : {uint8_t{99}, uint8_t{200}, uint8_t{255}})
        CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, y)), RANGE);
    CHECK_EQ(r.phys(BUF), 0xEE);
}

TEST(on_a_card_oodos_doesn_t_drive_every_routine_answers_pe_nocard_and_pb_vis_leaves_its_buffer)
{
    // Revision 2: OODOS 0.17 drives revision 3 alone, so the EQ, the vis page and the volumes are PE_NOCARD.
    CcpRig r({}, machine_with_sound(2));
    REQUIRE(r.ok);
    start_pb(r);
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'E', 1, 4)), NOCARD);
    CHECK_EQ(ac(pb(r, 'N', 1)), NOCARD);
    CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), NOCARD); // $EE is -18, in range: then the card is asked
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 0)), NOCARD);
    CHECK_EQ(r.phys(BUF), 0xEE);
    CHECK_EQ(ac(pb(r, 'v', 100, 128)), NOCARD);
}

TEST(the_warm_boot_after_the_library_leaves_the_eq_flat)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'E', 5, 20)), OK);
    CHECK_EQ(ac(pb(r, 'N', 1)), OK);
    CHECK_EQ(show(eq_state(r)), show(std::vector<int>{1, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0}));
    const size_t from = r.out.size();
    r.poke(MBOX, 'W');
    CHECK(r.until("\r\nA>", ms(2000), from));
    CHECK_EQ(show(eq_state(r)), show(std::vector<int>(12, 0)));
}

// ---- L5c: the SIDs' voices -----------------------------------------------------------------------------------

TEST(sid_voices_reads_both_sids_as_last_written)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    std::vector<uint8_t> want;
    for (unsigned sid = 0; sid < 2; ++sid)
        for (unsigned i = 0; i < 25; ++i)
        {
            const uint8_t v = static_cast<uint8_t>(0x11 * sid + 7 * i + 3);
            sid_reg(r, sid, i, v);
            want.push_back(v);
        }
    r.poke(BUF, std::vector<uint8_t>(64, 0xEE));
    CHECK_EQ(pb(r, 'S', BUF & 0xFF, BUF >> 8).substr(6), std::string("00"));
    std::vector<uint8_t> got(50);
    for (unsigned i = 0; i < 50; ++i)
        got[i] = r.phys(BUF + i);
    CHECK(got == want);
    CHECK_EQ(r.phys(BUF + 50), 0xEE); // and nothing past the block
}

namespace
{
    /// The thresholds, as mksidtab.py's rule says, worked out here: note 0's 0, then each the geometric mean of
    /// its note's register value and the one below's, rounded up, at most $FFFF.
    std::vector<unsigned> thresholds(double clock)
    {
        std::vector<double> reg(96);
        for (int n = 0; n < 96; ++n)
            reg[n] = 440.0 * std::pow(2.0, (n - 57) / 12.0) * 16'777'216.0 / clock;
        std::vector<unsigned> t = {0};
        for (int n = 1; n < 96; ++n)
            t.push_back(static_cast<unsigned>(std::min(65'535.0, std::ceil(std::sqrt(reg[n - 1] * reg[n])))));
        return t;
    }
    unsigned note(CcpRig &r, unsigned f, uint8_t ctrl)
    {
        const std::string out = pb(r, 'T', static_cast<uint8_t>(f), static_cast<uint8_t>(f >> 8), ctrl);
        return static_cast<unsigned>(std::stoul(out.substr(0, 2), nullptr, 16));
    }
} // namespace

TEST(sid_note_at_both_sides_of_every_threshold_for_both_clocks)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    for (const auto &[clock, ctrl] : std::vector<std::pair<double, uint8_t>>{{985'248, 0x00}, {1'022'727, 0x01}, {1'022'727, 0x19}})
    {
        const std::vector<unsigned> t = thresholds(clock);
        unsigned wrong = 0;
        for (unsigned n = 1; n < 96; ++n)
        {
            if (t[n] == t[n - 1]) // capped at $FFFF: the last of them is the note
                continue;
            wrong += note(r, t[n], ctrl) != n;
            wrong += note(r, t[n] - 1, ctrl) != n - 1;
        }
        CHECK_EQ(wrong, 0u);
        CHECK_EQ(note(r, 0, ctrl), 0xFFu); // SID_NONE
        CHECK_EQ(note(r, 1, ctrl), 0u);
        unsigned top = 95;
        while (t[top] > 0xFFFF)
            --top;
        CHECK_EQ(note(r, 0xFFFF, ctrl), top);
        std::printf("  clock %.0f (CTRL %02X): A-4 at %u, top %u\n", clock, ctrl, note(r, 0x1D45, ctrl), note(r, 0xFFFF, ctrl));
    }
    CHECK_EQ(note(r, 0x1D45, 0), 57u); // 440 Hz on PAL is $1D45: A-4
}

namespace
{
    /// The data sheet's times and mksidtab.py's steps, worked out here.
    constexpr double FRAME_MS = 1000 / 59.94;
    constexpr unsigned ATTACK_MS[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
    constexpr unsigned DECAY_MS[16] = {6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000};
    unsigned step(unsigned ms) { return std::min(0xFF00u, static_cast<unsigned>(std::lround(0xFF00 * FRAME_MS / ms))); }

    /// A voice's level as sidenv.s's rules move it, a frame at a time.
    struct Env
    {
        unsigned lvl = 0, phase = 0, gate = 0;
        void frame(uint8_t ctl, uint8_t ad, uint8_t sr)
        {
            if ((ctl & 1) != gate)
            {
                gate = ctl & 1;
                phase = gate ? 1 : 3;
            }
            const unsigned sus = (sr >> 4) * 0x11 << 8;
            if (phase == 1)
            {
                lvl += step(ATTACK_MS[ad >> 4]);
                if (lvl >= 0xFF00)
                {
                    lvl = 0xFF00;
                    phase = 2;
                }
            }
            else if (phase == 2 && lvl > sus)
            {
                const unsigned d = step(DECAY_MS[ad & 15]);
                lvl = lvl < d + sus ? sus : lvl - d;
            }
            else if (phase == 3)
            {
                const unsigned d = step(DECAY_MS[sr & 15]);
                if (lvl < d)
                {
                    lvl = 0;
                    phase = 0;
                }
                else
                    lvl -= d;
            }
        }
    };
    constexpr unsigned VOFF[6] = {0, 7, 14, 25, 32, 39};
    void set_voice(Rig &r, unsigned v, uint8_t ctl, uint8_t ad, uint8_t sr)
    {
        const unsigned chip = v < 3 ? 0 : 1, base = (v % 3) * 7;
        sid_reg(r, chip, base + 5, ad);
        sid_reg(r, chip, base + 6, sr);
        sid_reg(r, chip, base + 4, ctl);
    }
    /// `n` frames by F: the log, voice v's level at frame i at [i * 6 + v].
    std::vector<uint8_t> frames(CcpRig &r, unsigned n)
    {
        CHECK_EQ(pb(r, 'F', static_cast<uint8_t>(n)).substr(6), std::string("00"));
        std::vector<uint8_t> log(6 * n);
        for (unsigned i = 0; i < log.size(); ++i)
            log[i] = r.phys(LOG + i);
        return log;
    }
} // namespace

TEST(sid_env_moves_each_voice_as_the_rules_say_and_near_the_data_sheet_s_times)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(pb(r, 'Z').substr(6), std::string("00"));
    // Six voices, each its own ADSR: A's three, B's three (so the offsets of both SIDs are taken).
    const uint8_t AD[6] = {0x96, 0x00, 0x5A, 0x33, 0x0F, 0xC2};
    const uint8_t SR[6] = {0x83, 0xF0, 0x05, 0x48, 0x00, 0xA1};
    Env model[6];
    for (unsigned v = 0; v < 6; ++v)
        set_voice(r, v, 0x41, AD[v], SR[v]); // pulse, the gate on
    std::vector<uint8_t> on = frames(r, 60);
    for (unsigned v = 0; v < 6; ++v)
        set_voice(r, v, 0x40, AD[v], SR[v]); // the gate off
    std::vector<uint8_t> off = frames(r, 40);
    unsigned wrong = 0;
    for (unsigned v = 0; v < 6; ++v)
    {
        for (unsigned i = 0; i < 60; ++i)
        {
            model[v].frame(0x41, AD[v], SR[v]);
            wrong += on[i * 6 + v] != model[v].lvl >> 8;
        }
        for (unsigned i = 0; i < 40; ++i)
        {
            model[v].frame(0x40, AD[v], SR[v]);
            wrong += off[i * 6 + v] != model[v].lvl >> 8;
        }
    }
    CHECK_EQ(wrong, 0u);
    // Voice 0 against the data sheet: attack 9 (250 ms) to full, decay 6 (204 ms over the whole range) to the
    // sustain 8 ($88), release 3 (72 ms over it) to quiet: each within a frame of its time.
    auto first = [](const std::vector<uint8_t> &log, unsigned v, auto pred, unsigned from = 0) {
        for (unsigned i = from; i < log.size() / 6; ++i)
            if (pred(log[i * 6 + v]))
                return static_cast<int>(i) + 1;
        return -1;
    };
    const int full = first(on, 0, [](uint8_t l) { return l == 0xFF; });
    const int sus = first(on, 0, [](uint8_t l) { return l == 0x88; }, static_cast<unsigned>(full)); // after the attack
    const int quiet = first(off, 0, [](uint8_t l) { return l == 0; });
    const double t_full = 250 / FRAME_MS, t_sus = 204 * (0xFF - 0x88) / 255.0 / FRAME_MS, t_quiet = 72 * 0x88 / 255.0 / FRAME_MS;
    std::printf("  frames: full %d (%.1f), sustain after %d (%.1f), quiet %d (%.1f)\n", full, t_full, sus - full,
                t_sus, quiet, t_quiet);
    CHECK(std::fabs(full - t_full) <= 1);
    CHECK(std::fabs((sus - full) - t_sus) <= 1);
    CHECK(std::fabs(quiet - t_quiet) <= 1);
    // SID_ENV0: all quiet again, and with the gates off they stay so.
    CHECK_EQ(pb(r, 'Z').substr(6), std::string("00"));
    set_voice(r, 1, 0x41, 0x00, 0xF0); // voice 1 was held at full: gated again after the reset, it starts from 0
    const std::vector<uint8_t> again = frames(r, 1);
    CHECK_EQ(again[0], 0);
    CHECK_EQ(again[1], 0xFF); // attack 0: full in one frame
}

// ---- L5d: playback ------------------------------------------------------------------------------------------

namespace
{
    struct Status
    {
        unsigned state, ms, under;
    };
    Status status(CcpRig &r)
    {
        CHECK_EQ(ac(pb(r, 'Q', SBUF & 0xFF, SBUF >> 8)), OK);
        const auto b = [&](unsigned i) { return static_cast<unsigned>(r.phys(SBUF + i)); };
        return {b(0), b(1) | b(2) << 8 | b(3) << 16 | b(4) << 24, b(5) | b(6) << 8};
    }
    /// A PSID: load and init $1000, play $1000 + `play_at` (0: none), PAL, VBI (test_sound_fw's).
    std::vector<uint8_t> psid(std::vector<uint8_t> code, uint16_t play_at)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 3, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10;
        if (play_at)
            b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = 1, b[0x11] = 1, b[0x77] = 0x14;
        return b + code;
    }
    /// init: volume up, voice 1 a gated sawtooth, RTS ($1000-$1014).
    const std::vector<uint8_t> SAW = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4,
                                      0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, 0x21, 0x8D, 0x04, 0xD4, 0x60};
    /// A tune whose play routine ($1015) is called each frame `n` times and then hangs (its interrupt never ends,
    /// so no more play calls: a stall). n 0: it hangs at the first; 255: it plays for ever.
    std::vector<uint8_t> ticker(uint8_t n)
    {
        if (n == 255)
            return psid(SAW + std::vector<uint8_t>{0xEE, 0x01, 0xD4, 0x60}, 21); // INC $D401, RTS
        return psid(SAW + std::vector<uint8_t>{0xAD, 0x00, 0xC0,     // LDA $C000 (a counter: init leaves it 0)
                                               0xC9, n,              // CMP #n
                                               0xF0, 0x04,           // BEQ: hang
                                               0xEE, 0x00, 0xC0,     // INC $C000
                                               0x60,                 // RTS
                                               0x4C, 0x20, 0x10},    // $1020: JMP $1020
                    21);
    }
    /// The tune player claimed, `file` loaded and played (SN_TUNE, SN_PLAY), and PB_BEGIN(PB_SID).
    void tune_begun(CcpRig &r, const std::vector<uint8_t> &file)
    {
        CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00"));
        r.poke(0x10000, file);
        CHECK_EQ(f82(r, std::vector<uint8_t>{SN_TUNE} + le24(0x10000) + le24(static_cast<uint32_t>(file.size())) + std::vector<uint8_t>{0}),
                 std::string("00"));
        CHECK_EQ(f82(r, {SN_PLAY, 0, 0, 0, 0, 0, 0, 0, 0, 0}), std::string("00"));
        CHECK_EQ(ac(pb(r, 'B', 0)), OK);
    }
} // namespace

TEST(a_tune_s_status_starting_playing_paused_resumed_stalled_and_stopped)
{
    // PB_STATUS of a .sid tune from the card's own status (SN_TSTATUS): playing once its play calls come, paused and
    // its time held, playing again with the pause left out, stalled 2 s after its last play call (the card's rule),
    // stopped by the program. Must fail with PB_STATUS's state taken from the card's +0 off by one (the map).
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(status(r).state, PS_STOPPED); // nothing begun
    CHECK_EQ(ac(pb(r, 'P')), STATE);
    CHECK_EQ(ac(pb(r, 'R')), STATE);
    const uint32_t m0 = ms_count(r);
    tune_begun(r, ticker(60)); // about 1.2 s of play calls
    r.run(ms(300));
    Status st = status(r);
    CHECK_EQ(st.state, PS_PLAYING);
    const uint32_t played = ms_count(r) - m0;
    std::printf("  playing: %u ms played, %u since before the start\n", st.ms, played);
    CHECK(st.ms <= played && st.ms + 80 >= played);
    // Held 3 s: paused, the time still; then let go: playing, the 3 s left out.
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK_EQ(ac(pb(r, 'P')), STATE); // paused already
    CHECK_EQ(fw(r).tune_state(), SoundFw::T_PAUSED);
    st = status(r);
    CHECK_EQ(st.state, PS_PAUSED);
    const unsigned at_pause = st.ms;
    r.run(ms(3000));
    st = status(r);
    CHECK_EQ(st.state, PS_PAUSED);
    CHECK(st.ms <= at_pause + 2);
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(ac(pb(r, 'R')), STATE); // not paused
    st = status(r);
    CHECK_EQ(st.state, PS_PLAYING);
    CHECK(st.ms >= at_pause && st.ms <= at_pause + 60);
    // The play calls run out; 2 s after the last: stalled.
    r.run(ms(1500));
    CHECK_EQ(status(r).state, PS_PLAYING);
    r.run(ms(2600));
    CHECK_EQ(status(r).state, PS_STALLED);
    CHECK_EQ(f82op(r, SN_STOP), std::string("00"));
    CHECK_EQ(status(r).state, PS_STOPPED);
}

TEST(after_a_resume_the_stall_rule_starts_again)
{
    // The tune's 5 play calls are over before the hold, so only the card's fresh start of its rule at the pause keeps
    // it playing after the resume.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    tune_begun(r, ticker(5));
    r.run(ms(400));
    CHECK_EQ(status(r).state, PS_PLAYING);
    CHECK_EQ(ac(pb(r, 'P')), OK);
    r.run(ms(3000));
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(status(r).state, PS_PLAYING); // 3 s since the last call, but not counting the pause
    r.run(ms(1000));
    CHECK_EQ(status(r).state, PS_PLAYING);
    r.run(ms(1600));
    CHECK_EQ(status(r).state, PS_STALLED);
}

TEST(a_tune_whose_play_never_comes_is_starting_for_10_s_then_stalled)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    tune_begun(r, ticker(0));
    r.run(ms(9000));
    CHECK_EQ(status(r).state, PS_STARTING);
    r.run(ms(1500));
    CHECK_EQ(status(r).state, PS_STALLED);
}

TEST(a_tune_with_no_play_routine_plays_past_10_s)
{
    // A .sid with no play routine installs its own interrupt and makes no play call: it isn't stalled.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    tune_begun(r, psid(SAW + std::vector<uint8_t>{0x4C, 0x15, 0x10}, 0)); // init: the saw, then spins
    r.run(ms(10'500));
    CHECK_EQ(status(r).state, PS_PLAYING);
    CHECK_EQ(f82op(r, SN_STOP), std::string("00"));
    CHECK_EQ(status(r).state, PS_STOPPED);
}

TEST(a_resume_refused_after_the_part_was_stopped_leaves_it_not_paused)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    tune_begun(r, ticker(60));
    r.run(ms(100));
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK_EQ(f82op(r, SN_STOP), std::string("00"));
    CHECK_EQ(ac(pb(r, 'R')), REFUSED); // the card's E_STATE: nothing paused
    CHECK_EQ(status(r).state, PS_STOPPED);
    CHECK_EQ(ac(pb(r, 'R')), STATE); // not paused now
}

TEST(a_stream_s_status_from_the_card_its_milliseconds_from_its_frames)
{
    // PB_STATUS of the stream from SN_STATUS: playing, its milliseconds the card's frames at the stream's own rate
    // (SN_STATUS's +13-14); paused; run dry: an underrun (stalled); the end said: ended; released: stopped.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00"));
    CHECK_EQ(f82(r, {SN_STREAM, 0, 44'100 & 0xFF, 44'100 >> 8, 3}), std::string("00"));
    std::vector<uint8_t> data(0x10000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7 + (i >> 9));
    r.poke(0x10000, data);
    CHECK_EQ(f82(r, {SN_BUFFER, 0x00, 0x00, 0x01, 0, 0}), std::string("00"));
    CHECK_EQ(f82(r, {SN_BUFFER, 0x00, 0x00, 0x01, 0, 0}), std::string("01"));
    CHECK_EQ(ac(pb(r, 'B', 1)), OK);
    fw(r).set_stream_tap(true);
    r.run(ms(200));
    const size_t f0 = fw(r).take_stream_tap().size() / 2;
    Status st = status(r);
    const size_t f1 = f0 + fw(r).take_stream_tap().size() / 2;
    std::printf("  playing: %u ms; frames played meanwhile %zu to %zu since the tap began\n", st.ms, f0, f1);
    CHECK_EQ(st.state, PS_PLAYING);
    CHECK(st.ms >= 150 && st.ms <= 260);
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK_EQ(status(r).state, PS_PAUSED);
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(status(r).state, PS_PLAYING);
    // Nothing more given: it runs dry, an underrun; then the end said, it ends.
    r.run(ms(1200));
    st = status(r);
    CHECK_EQ(st.state, PS_STALLED);
    CHECK(st.under >= 1);
    CHECK(st.ms >= 2 * 0x10000 * 1000ull / 4 / 44'100 - 1); // both buffers' frames played
    CHECK_EQ(f82op(r, SN_END), std::string("00"));
    r.run(ms(50));
    CHECK_EQ(status(r).state, PS_ENDED);
    CHECK_EQ(f82(r, {SN_RELEASE, SC_STREAM}), std::string("00"));
    CHECK_EQ(status(r).state, PS_STOPPED); // SN_STATUS refused: not claimed
}

TEST(music_s_status_from_the_card_and_pb_doc_refused)
{
    // PB_MUSIC (interface 18): a note block of three 300 ms tones playing, its milliseconds the card's; paused and
    // resumed by SN_MCTL; stopped when it ends. PB_DOC is refused (the ES5503 is the card's); past PB_MUSIC no source.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'B', 2)), REFUSED);
    CHECK_EQ(ac(pb(r, 'B', 4)), RANGE);
    CHECK_EQ(f82(r, {SN_CLAIM, SC_MUSIC}), std::string("00"));
    std::vector<uint8_t> blk = std::vector<uint8_t>{1, 1, 0, 0} + u32(3) + std::vector<uint8_t>{2, 0, 0, 0, 0, 0, 255, 128};
    for (const unsigned hz : {440u, 550u, 660u})
        blk = blk + std::vector<uint8_t>{0x10} + le24(hz * 16) + u32(300'000) + u32(300'000);
    r.poke(0x20000, blk);
    CHECK_EQ(f82(r, std::vector<uint8_t>{SN_MUSIC, 0} + le24(0x20000) + le24(static_cast<uint32_t>(blk.size())) + std::vector<uint8_t>{2}),
             std::string("00"));
    CHECK_EQ(ac(pb(r, 'B', 3)), OK);
    r.run(ms(200));
    Status st = status(r);
    CHECK_EQ(st.state, PS_PLAYING);
    CHECK(st.ms >= 150 && st.ms <= 260);
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK_EQ(fw(r).music_state(), SoundFw::M_PAUSED);
    CHECK_EQ(status(r).state, PS_PAUSED);
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(fw(r).music_state(), SoundFw::M_PLAYING);
    r.run(ms(900));
    CHECK_EQ(status(r).state, PS_STOPPED);
}

TEST(pb_volume_s_law_at_its_ends_its_centre_and_between)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    const auto law = [](unsigned lev, unsigned bal) {
        unsigned l = lev, rr = lev;
        if (bal < 128)
            rr = lev * bal / 128;
        else if (bal > 128)
            l = lev * (255 - bal) / 127;
        return std::pair<unsigned, unsigned>{l, rr};
    };
    unsigned wrong = 0;
    for (const unsigned lev : {255u, 200u, 128u, 1u, 0u})
        for (const unsigned bal : {0u, 1u, 64u, 127u, 128u, 129u, 192u, 254u, 255u})
        {
            CHECK_EQ(ac(pb(r, 'v', static_cast<uint8_t>(lev), static_cast<uint8_t>(bal))), OK);
            const auto [l, rr] = law(lev, bal);
            wrong += fw(r).volume_left() != l || fw(r).volume_right() != rr;
        }
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(ac(pb(r, 'v', 255, 0)), OK); // the ends: the far side 0
    CHECK_EQ(fw(r).volume_left(), 255);
    CHECK_EQ(fw(r).volume_right(), 0);
    CHECK_EQ(ac(pb(r, 'v', 255, 255)), OK);
    CHECK_EQ(fw(r).volume_left(), 0);
    CHECK_EQ(fw(r).volume_right(), 255);
    CHECK_EQ(ac(pb(r, 'v', 255, 128)), OK);
}

namespace
{
    std::vector<uint8_t> levels(CcpRig &r, unsigned frames)
    {
        CHECK_EQ(ac(pb(r, 'L', static_cast<uint8_t>(frames))), OK);
        std::vector<uint8_t> log(10 * frames);
        for (unsigned i = 0; i < log.size(); ++i)
            log[i] = r.phys(LOG + i);
        return log;
    }
} // namespace

TEST(pb_levels_are_the_card_s_own_for_a_tune_and_a_stream)
{
    // PB_LEVELS: nothing begun, all 0; a tune's sawtooth, measured by the card (SN_LEVELS: no longer an estimate from
    // its registers), shows in a band and the VU; a 3 kHz stream's in the lowest band and the VU. Must fail with
    // PB_LEVELS's block left at 0 (SN_LEVELS's results not copied).
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    std::vector<uint8_t> log = levels(r, 1);
    CHECK(std::all_of(log.begin(), log.end(), [](uint8_t b) { return b == 0; })); // nothing begun
    tune_begun(r, ticker(255));
    log = levels(r, 10);
    const std::vector<uint8_t> last(log.end() - 10, log.end());
    std::printf("  a tune: bands %u %u %u %u %u %u %u %u, VU %u %u\n", last[0], last[1], last[2], last[3], last[4],
                last[5], last[6], last[7], last[8], last[9]);
    CHECK(*std::max_element(last.begin(), last.begin() + 8) > 20);
    CHECK(last[8] > 20 && last[9] > 20);
    CHECK_EQ(f82op(r, SN_STOP), std::string("00"));
    CHECK_EQ(f82(r, {SN_RELEASE, SC_TUNE}), std::string("00"));
    CHECK_EQ(ac(pb(r, 'B', 1)), OK);
    play(r, sine(30'000, 3'000, 0.5), false);
    r.run(ms(200));
    log = levels(r, 1);
    std::printf("  3 kHz: bands %u %u %u %u %u %u %u %u, VU %u %u\n", log[0], log[1], log[2], log[3], log[4], log[5],
                log[6], log[7], log[8], log[9]);
    CHECK(log[0] > 50 && log[0] == *std::max_element(log.begin(), log.begin() + 8)); // 3 kHz: the vis page's first four bands
    CHECK(log[8] > 150 && log[9] > 150);
}

int main() { return run_all(); }
