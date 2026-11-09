/**
 * @file test_oodos_pb.cpp
 * @brief OOLIB's L5 (hw/oolib.md, "The plan"): the linked sound library (oodos/lib/pb*.s).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * PBTEST (oodos/test/pb/pbtest.s), run by GO, calls a library routine for each command: a letter at $3900,
 * A, X and Y after it, a block at $3A00. L5b's EQ and vis page: what the library leaves in the card's
 * registers, read back through EQIDX and EQVAL as a program would; refusals that write nothing; OO-Amp's
 * test of the EQ (a sine at each band's centre, +12 dB from the library against the stream, 12 dB ± 1
 * louder at the card's test tap); the vis page copied byte for byte as the card holds it; and a revision-1
 * card answered PE_NOCARD. The card's own EQ and vis page are test_sound_eq's.
 *
 * L5c's SID reads: SID_VOICES against the registers as written; SID_NOTE at both sides of every note's threshold
 * for both clocks, the thresholds worked out here (the geometric mean of a note's register value and the one
 * below's, rounded up); SID_ENV's six levels frame by frame against the same rules worked out here, and the
 * times to full, to the sustain and to quiet within a frame of the data sheet's.
 *
 * L5d's playback layer, on real sources: a 6510 program sending a mailbox byte every 20 ms or so (then stopping),
 * or none; a 44.1 kHz stream given by function 82; the ES5503's RUN. PB_STATUS's states, the stall rule's two
 * times and its start after a resume, the milliseconds with paused time left out (a stream's within the card's
 * frames either side of the call); PB_VOLUME's law at its ends and between; PB_LEVELS for a SID's voices, worked
 * out here, and for a stream from the vis page.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"
#include "sound_dsp.h"

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
    constexpr uint32_t MBOX = 0x3900, BUF = 0x3A00, LOG = 0x3C00; ///< pbtest.s's
    constexpr uint8_t PE_NOCARD = 1, PE_RANGE = 2;  ///< oolib.inc's
    constexpr double PI = 3.14159265358979323846;
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    SoundCard &card(Rig &r) { return *r.machine->sound_card(); }
    uint8_t reg(Rig &r, uint8_t offset) { return card(r).peek(offset); }

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

    /// The EQ as the card holds it: the enable, then the preamp's and the bands' gains (EQIDX left moved).
    std::vector<int> eq_state(Rig &r)
    {
        std::vector<int> out;
        card(r).write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
        out.push_back(card(r).read(SoundCard::EQVAL));
        card(r).write(SoundCard::EQIDX, 0);
        for (unsigned i = 0; i <= SoundCard::EQ_BANDS; ++i)
            out.push_back(static_cast<int8_t>(card(r).read(SoundCard::EQVAL)));
        return out;
    }
    std::string show(const std::vector<int> &st)
    {
        std::string s = st[0] ? "on:" : "off:";
        for (size_t i = 1; i < st.size(); ++i)
            s += " " + std::to_string(st[i]);
        return s;
    }

    /// The vis page as the card holds it, without a snapshot: VISDATA read round its 256 indexes to 0, then 98.
    std::vector<uint8_t> held_page(Rig &r)
    {
        SoundCard &c = card(r);
        for (unsigned i = c.peek(SoundCard::VISIDX); i != 0; i = (i + 1) & 0xFF)
            c.read(SoundCard::VISDATA);
        std::vector<uint8_t> p(SoundCard::VIS_BYTES);
        for (uint8_t &b : p)
            b = c.read(SoundCard::VISDATA);
        return p;
    }

    /// A sine at `hz`, `amp` of full scale, `n` frames at 48 kHz, mono 16-bit.
    std::vector<int16_t> sine(size_t n, double hz, double amp)
    {
        std::vector<int16_t> out(n);
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<int16_t>(std::floor(amp * 32'767.0 * std::sin(2 * PI * hz * static_cast<double>(i) / 48'000) + 0.5));
        return out;
    }
    /// `samples` given to the card's stream by the test itself (the program claims nothing), played to the end
    /// while the machine runs: what the tap had, stereo frames.
    std::vector<int16_t> play(CcpRig &r, const std::vector<int16_t> &samples, bool to_end = true)
    {
        SoundCard &c = card(r);
        c.take_tap();
        c.set_tap(true);
        c.write(SoundCard::RATE, 48'000 & 0xFF);
        c.write(SoundCard::RATE + 1, 48'000 >> 8);
        c.write(SoundCard::PCMFMT, SoundCard::PCMFMT_16BIT);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
        for (const int16_t s : samples)
        {
            c.write(SoundCard::PCMDATA, static_cast<uint8_t>(s));
            c.write(SoundCard::PCMDATA, static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
        }
        if (!to_end)
            return {};
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        r.run(ms(samples.size() / 48 + 40));
        CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_ENDED);
        c.write(SoundCard::PCMCTL, 0);
        return c.take_tap();
    }
    /// The RMS of the left side of stereo `frames`, from frame `from`.
    double rms(const std::vector<int16_t> &frames, size_t from)
    {
        double sum = 0;
        size_t n = 0;
        for (size_t f = from; 2 * f < frames.size(); ++f, ++n)
            sum += static_cast<double>(frames[2 * f]) * frames[2 * f];
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0;
    }
    double rms_mono(const std::vector<int16_t> &s, size_t from)
    {
        double sum = 0;
        for (size_t i = from; i < s.size(); ++i)
            sum += static_cast<double>(s[i]) * s[i];
        return s.size() > from ? std::sqrt(sum / static_cast<double>(s.size() - from)) : 0;
    }
} // namespace

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
    // The ends of the range.
    CHECK_EQ(ac(pb(r, 'E', 3, 24)), OK);
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
    // A block with one gain out of range: none of it written.
    std::vector<uint8_t> blk(11);
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
    // A sine at each band's centre at 48 kHz, the band +12 dB (24 half-dB) by PB_EQALL and the EQ on by
    // PB_EQON: its level at the tap, the last quarter second, against the stream's own (the EQ off is bit
    // exact: test_sound_eq), within 1 dB of 12.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    double worst = 0;
    for (unsigned b = 0; b < BusEq::BANDS; ++b)
    {
        std::vector<uint8_t> blk(11, 0);
        blk[b + 1] = 24;
        r.poke(BUF, blk);
        CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), OK);
        CHECK_EQ(ac(pb(r, 'N', 1)), OK);
        const std::vector<int16_t> in = sine(24'000, BusEq::CENTRE_HZ[b], 0.1);
        const std::vector<int16_t> out = play(r, in);
        const double db = 20 * std::log10(rms(out, 12'000) / rms_mono(in, 12'000));
        std::printf("  %5u Hz: %+6.2f dB\n", BusEq::CENTRE_HZ[b], db);
        worst = std::max(worst, std::fabs(db - 12));
        CHECK(std::fabs(db - 12) < 1.0);
        CHECK_EQ(ac(pb(r, 'N', 0)), OK);
    }
    std::printf("  worst: %.3f dB off\n", worst);
}

TEST(pb_vis_copies_a_fresh_snapshot_byte_for_byte)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    card(r).write(SoundCard::VISIDX, 0); // a snapshot of silence first: PB_VIS's must be a new one
    // A 3 kHz sine playing (two seconds of it in the FIFO, more than the test takes).
    play(r, sine(60'000, 3'000, 0.5), false);
    r.run(ms(100));
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 0)), OK);
    std::vector<uint8_t> got(SoundCard::VIS_BYTES);
    for (unsigned i = 0; i < got.size(); ++i)
        got[i] = r.phys(BUF + i);
    CHECK(got == held_page(r)); // the card's page, as PB_VIS left it
    CHECK_EQ(r.phys(BUF + 98), 0xEE); // and nothing past it
    unsigned loudest = 0;
    for (unsigned i = 1; i < 32; ++i)
        if (got[i] > got[loudest])
            loudest = i;
    std::printf("  3 kHz: band %u at %u, VU %u %u\n", loudest, got[loudest], got[32], got[33]);
    CHECK(got[loudest] > 200); // the sine's band: not the silent snapshot
    CHECK(got[32] > 200 && got[33] > 200);
    // A length: 34 bytes, the bands and the VU, and not the 35th.
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 34)), OK);
    const std::vector<uint8_t> page34 = held_page(r);
    bool same = true;
    for (unsigned i = 0; i < 34; ++i)
        same = same && r.phys(BUF + i) == page34[i];
    CHECK(same);
    CHECK_EQ(r.phys(BUF + 34), 0xEE);
    // 99 or more: refused, nothing copied and no snapshot taken (the index left where 34 left it).
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    for (const uint8_t y : {uint8_t{99}, uint8_t{200}, uint8_t{255}})
        CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, y)), RANGE);
    CHECK_EQ(r.phys(BUF), 0xEE);
    CHECK_EQ(card(r).peek(SoundCard::VISIDX), 98); // where held_page left it
    card(r).write(SoundCard::PCMCTL, 0);
}

TEST(on_a_revision_1_card_every_routine_answers_pe_nocard_and_pb_vis_leaves_its_buffer)
{
    CcpRig r;
    REQUIRE(r.ok);
    card(r).set_revision(1);
    start_pb(r);
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'E', 1, 4)), NOCARD);
    CHECK_EQ(ac(pb(r, 'N', 1)), NOCARD);
    CHECK_EQ(ac(pb(r, 'A', BUF & 0xFF, BUF >> 8)), NOCARD); // $EE is -18, in range: then the card is asked
    r.poke(BUF, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(ac(pb(r, 'V', BUF & 0xFF, BUF >> 8, 0)), NOCARD);
    CHECK_EQ(r.phys(BUF), 0xEE);
}

TEST(the_warm_boot_after_the_library_leaves_the_eq_flat)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'E', 5, 20)), OK);
    CHECK_EQ(ac(pb(r, 'N', 1)), OK);
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
            card(r).write(static_cast<uint8_t>((sid ? SoundCard::SID_B : SoundCard::SID_A) + i), v);
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
        const uint8_t base = static_cast<uint8_t>((v < 3 ? SoundCard::SID_A : SoundCard::SID_B) + (v % 3) * 7);
        card(r).write(base + 5, ad);
        card(r).write(base + 6, sr);
        card(r).write(base + 4, ctl);
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
    constexpr uint32_t AREA = 0x3800, SBUF = 0x3A80;
    constexpr uint8_t SN_CLAIM = 1, SN_RELEASE = 2, SN_LOAD = 3, SN_START = 4, SN_STOP = 5, SN_STREAM = 7,
                      SN_BUFFER = 8, SN_END = 12;
    constexpr uint8_t SC_6510 = 1, SC_STREAM = 2, SC_DOC = 4;
    constexpr uint8_t PS_STOPPED = 0, PS_STARTING = 1, PS_PLAYING = 2, PS_PAUSED = 3, PS_STALLED = 4, PS_ENDED = 5;
    constexpr uint8_t PE_STATE = 3, PE_REFUSED = 4;
    const std::string STATE = hex2(PE_STATE) + " 01", REFUSED = hex2(PE_REFUSED) + " 01";

    uint32_t ms_count(Rig &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("MS");
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    /// Function 82 with `block` at BUF (K): A as "aa".
    std::string f82(CcpRig &r, const std::vector<uint8_t> &block)
    {
        r.poke(BUF, block);
        return pb(r, 'K', BUF & 0xFF, BUF >> 8).substr(0, 2);
    }
    std::string f82op(CcpRig &r, uint8_t op) { return pb(r, 'k', op).substr(0, 2); }
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
    /// The 6510's program at $1000: `n` mailbox bytes (n, n - 1 ... 1) some 20 ms apart, then it spins. n 0: none.
    std::vector<uint8_t> ticker(uint8_t n)
    {
        if (n == 0)
            return {0x4C, 0x00, 0x10}; // JMP $1000
        return {0xA2, n,                // LDX #n
                0x8E, 0x00, 0xDE,       // $1002: STX $DE00
                0xA0, 0x10,             // LDY #16
                0x84, 0xF0,             // STY $F0
                0xA0, 0x00,             // $1009: LDY #0
                0x88, 0xD0, 0xFD,       // $100B: DEY, BNE $100B
                0xC6, 0xF0, 0xD0, 0xF7, // DEC $F0, BNE $1009
                0xCA, 0xD0, 0xED,       // DEX, BNE $1002
                0x4C, 0x15, 0x10};      // $1015: JMP $1015
    }
    /// The 6510 claimed, `code` loaded at $1000 and started; PB_BEGIN(PB_SID).
    void sid_begun(CcpRig &r, const std::vector<uint8_t> &code)
    {
        CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("00"));
        r.poke(0x3000, code);
        CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x10, static_cast<uint8_t>(code.size()), 0}), std::string("00"));
        CHECK_EQ(f82(r, {SN_START, 0x00, 0x10, 0}), std::string("00"));
        CHECK_EQ(ac(pb(r, 'B', 0)), OK);
    }
    void with_area(CcpRig &r)
    {
        CHECK_EQ(ac(pb(r, 'O')), OK);
        CHECK_EQ(ac(pb(r, 'I', AREA & 0xFF, AREA >> 8)), OK);
    }
} // namespace

TEST(a_sid_s_status_starting_playing_paused_resumed_stalled_and_stopped)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    with_area(r);
    CHECK_EQ(status(r).state, PS_STOPPED); // nothing begun
    CHECK_EQ(ac(pb(r, 'P')), STATE);
    CHECK_EQ(ac(pb(r, 'R')), STATE);
    const uint32_t m0 = ms_count(r);
    sid_begun(r, ticker(60)); // some 1.2 s of bytes
    r.run(ms(300));
    Status st = status(r);
    CHECK_EQ(st.state, PS_PLAYING);
    const uint32_t played = ms_count(r) - m0;
    std::printf("  playing: %u ms played, %u since before the start\n", st.ms, played);
    CHECK(st.ms <= played && st.ms + 60 >= played);
    // Held 3 s: paused, the time still; then let go: playing at once (the stall rule starts again), the 3 s left out.
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK_EQ(ac(pb(r, 'P')), STATE); // paused already
    CHECK_EQ(reg(r, SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
    st = status(r);
    CHECK_EQ(st.state, PS_PAUSED);
    const unsigned at_pause = st.ms;
    r.run(ms(3000));
    st = status(r);
    CHECK_EQ(st.state, PS_PAUSED);
    CHECK_EQ(st.ms, at_pause);
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(ac(pb(r, 'R')), STATE); // not paused
    st = status(r);
    CHECK_EQ(st.state, PS_PLAYING);
    CHECK(st.ms >= at_pause && st.ms <= at_pause + 60);
    // The ticker's bytes run out; 2 s after its last: stalled.
    r.run(ms(1500));
    CHECK_EQ(status(r).state, PS_PLAYING);
    r.run(ms(2600));
    CHECK_EQ(status(r).state, PS_STALLED);
    CHECK_EQ(r.phys(AREA + 113) | r.phys(AREA + 114) << 8, 60u); // EV_SMCNT
    // Stopped by the program: stopped.
    CHECK_EQ(f82op(r, SN_STOP), std::string("00"));
    CHECK_EQ(status(r).state, PS_STOPPED);
}

TEST(after_a_resume_the_stall_rule_starts_again)
{
    // The ticker's 5 bytes are over before the hold, so only the resume's new start keeps it playing after.
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    with_area(r);
    sid_begun(r, ticker(5));
    r.run(ms(400));
    CHECK_EQ(status(r).state, PS_PLAYING);
    CHECK_EQ(ac(pb(r, 'P')), OK);
    r.run(ms(3000));
    CHECK_EQ(ac(pb(r, 'R')), OK);
    CHECK_EQ(status(r).state, PS_PLAYING); // 3 s since the last byte, but not since the resume
    r.run(ms(1500));
    CHECK_EQ(status(r).state, PS_PLAYING);
    r.run(ms(700));
    CHECK_EQ(status(r).state, PS_STALLED);
}

TEST(a_sid_that_says_nothing_is_starting_for_10_s_then_stalled_and_without_an_area_playing)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    with_area(r);
    sid_begun(r, ticker(0));
    r.run(ms(9000));
    CHECK_EQ(status(r).state, PS_STARTING);
    r.run(ms(1500));
    CHECK_EQ(status(r).state, PS_STALLED);
    // Without an area the 6510 running is all there is to go by.
    CHECK_EQ(ac(pb(r, 'I', 0, 0)), OK);
    CHECK_EQ(status(r).state, PS_PLAYING);
}

TEST(a_stream_s_status_from_the_card_its_milliseconds_from_its_frames)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00"));
    CHECK_EQ(f82(r, {SN_STREAM, 0, 44'100 & 0xFF, 44'100 >> 8, SoundCard::PCMFMT_16BIT | SoundCard::PCMFMT_STEREO}), std::string("00"));
    std::vector<uint8_t> data(0x10000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7 + (i >> 9));
    r.poke(0x10000, data);
    CHECK_EQ(f82(r, {SN_BUFFER, 0x00, 0x00, 0x01, 0, 0}), std::string("00"));
    CHECK_EQ(f82(r, {SN_BUFFER, 0x00, 0x00, 0x01, 0, 0}), std::string("01"));
    CHECK_EQ(ac(pb(r, 'B', 1)), OK);
    r.run(ms(200));
    const auto pos = [&] {
        SoundCard &c = card(r);
        const uint32_t lo = c.read(SoundCard::POS);
        return lo | c.peek(SoundCard::POS + 1) << 8 | c.peek(SoundCard::POS + 2) << 16 |
               static_cast<uint32_t>(c.peek(SoundCard::POS + 3)) << 24;
    };
    const uint32_t p0 = pos();
    Status st = status(r);
    const uint32_t p1 = pos();
    std::printf("  playing: %u ms, frames %u to %u (%.1f to %.1f ms)\n", st.ms, p0, p1, p0 * 1000.0 / 44'100,
                p1 * 1000.0 / 44'100);
    CHECK_EQ(st.state, PS_PLAYING);
    CHECK(st.ms >= p0 * 1000ull / 44'100 && st.ms <= p1 * 1000ull / 44'100);
    CHECK_EQ(ac(pb(r, 'P')), OK);
    CHECK(reg(r, SoundCard::PCMCTL) & SoundCard::PCMCTL_PAUSE);
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

TEST(the_es5503_plays_while_its_run_is_on_and_its_pause_is_refused)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(f82(r, {SN_CLAIM, SC_DOC}), std::string("00"));
    card(r).write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    CHECK_EQ(ac(pb(r, 'B', 2)), OK);
    CHECK_EQ(status(r).state, PS_PLAYING);
    CHECK_EQ(ac(pb(r, 'P')), REFUSED);
    CHECK_EQ(status(r).state, PS_PLAYING);
    card(r).write(SoundCard::DOCCTL, 0);
    CHECK_EQ(status(r).state, PS_STOPPED);
    CHECK_EQ(ac(pb(r, 'B', 3)), RANGE); // no such source
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
            wrong += reg(r, SoundCard::VOLL) != l || reg(r, SoundCard::VOLR) != rr;
        }
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(ac(pb(r, 'v', 255, 0)), OK); // the ends: the far side 0
    CHECK_EQ(reg(r, SoundCard::VOLL), 255);
    CHECK_EQ(reg(r, SoundCard::VOLR), 0);
    CHECK_EQ(ac(pb(r, 'v', 255, 255)), OK);
    CHECK_EQ(reg(r, SoundCard::VOLL), 0);
    CHECK_EQ(reg(r, SoundCard::VOLR), 255);
    CHECK_EQ(ac(pb(r, 'v', 255, 128)), OK);
    // On revision 1, OODOS (booted with it) refuses SN_VOLUME.
    CcpRig r1;
    REQUIRE(r1.ok);
    card(r1).set_revision(1);
    REQUIRE(r1.boot());
    start_pb(r1);
    CHECK_EQ(ac(pb(r1, 'v', 100, 128)), NOCARD);
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
    void sid_reg(Rig &r, unsigned sid, unsigned off, uint8_t v)
    {
        card(r).write(static_cast<uint8_t>((sid ? SoundCard::SID_B : SoundCard::SID_A) + off), v);
    }
} // namespace

TEST(pb_levels_for_a_sid_each_voice_in_its_octave_s_band_scaled_by_its_volume)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    std::vector<uint8_t> log = levels(r, 1);
    CHECK(std::all_of(log.begin(), log.end(), [](uint8_t b) { return b == 0; })); // nothing begun
    CHECK_EQ(ac(pb(r, 'B', 0)), OK);
    // SID A: voice 0 A-4 (440 Hz: $1D45 on PAL), pulse; voice 2 noise. SID B: voice 4 (its second) C-1 at 32.7 Hz
    // ($022D), saw; voice 3 a gate with no waveform. Every attack 0 (full in a frame), sustain 15. A's volume 15,
    // B's 7.
    const auto voice = [&](unsigned sid, unsigned v, unsigned f, uint8_t ctl) {
        sid_reg(r, sid, v * 7 + 0, static_cast<uint8_t>(f));
        sid_reg(r, sid, v * 7 + 1, static_cast<uint8_t>(f >> 8));
        sid_reg(r, sid, v * 7 + 5, 0x00);
        sid_reg(r, sid, v * 7 + 6, 0xF0);
        sid_reg(r, sid, v * 7 + 4, ctl);
    };
    voice(0, 0, 0x1D45, 0x41);
    voice(0, 2, 0x1000, 0x81);
    voice(1, 1, 0x022D, 0x21);
    voice(1, 0, 0x1D45, 0x01);
    sid_reg(r, 0, 24, 0x0F);
    sid_reg(r, 1, 24, 0x07);
    log = levels(r, 2);
    const std::vector<uint8_t> want = {0, 119, 0, 0, 255, 0, 0, 255, 255, 119}; // 255 x 7 / 15 is 119
    const std::vector<uint8_t> got(log.begin() + 10, log.end());
    CHECK(got == want);
    if (got != want)
        for (unsigned i = 0; i < 10; ++i)
            std::printf("  [%u] %u, want %u\n", i, got[i], want[i]);
    // The gates off, release 0: quiet within a frame or two; a new PB_BEGIN starts from quiet.
    for (unsigned sid = 0; sid < 2; ++sid)
        for (unsigned v = 0; v < 3; ++v)
            sid_reg(r, sid, v * 7 + 4, 0x40);
    log = levels(r, 3);
    CHECK(std::all_of(log.begin() + 20, log.end(), [](uint8_t b) { return b == 0; }));
}

TEST(pb_levels_for_a_stream_the_vis_page_s_bands_four_at_a_time)
{
    CcpRig r;
    REQUIRE(r.ok);
    start_pb(r);
    CHECK_EQ(ac(pb(r, 'B', 1)), OK);
    play(r, sine(60'000, 3'000, 0.5), false);
    r.run(ms(100));
    const std::vector<uint8_t> log = levels(r, 1);
    const std::vector<uint8_t> page = held_page(r); // the snapshot PB_LEVELS took
    std::vector<uint8_t> want;
    for (unsigned b = 0; b < 8; ++b)
        want.push_back(static_cast<uint8_t>((page[4 * b] + page[4 * b + 1] + page[4 * b + 2] + page[4 * b + 3]) / 4));
    want.push_back(page[32]);
    want.push_back(page[33]);
    CHECK(log == want);
    std::printf("  3 kHz: bands %u %u %u %u %u %u %u %u, VU %u %u\n", log[0], log[1], log[2], log[3], log[4], log[5],
                log[6], log[7], log[8], log[9]);
    CHECK(log[0] > 100); // 3 kHz is in the first four of 32 bands
    card(r).write(SoundCard::PCMCTL, 0);
    card(r).set_revision(1);
    CHECK_EQ(ac(pb(r, 'L', 1)), NOCARD);
}

int main() { return run_all(); }
