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
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"
#include "sound_dsp.h"

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

int main() { return run_all(); }
