/**
 * @file test_sid_audio.cpp
 * @brief The SID's output stage (sid.h) and the sound card's audio (sound_card.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The chip: a voice's level is its waveform less the DAC's zero, times its
 * envelope; the mixer's DC and the volume; the 6581's DC against the
 * 8580's; 3OFF; the filter's three outputs against tones above and below
 * the cutoff, the cutoff curves' ends, resonance's gain; muting; and that
 * the audio path leaves the chip in the state clock() would. The card: a
 * second of SID clocks makes a second of samples, at either crystal; SID A
 * is left and SID B right; the DC goes; samples wait at most half a
 * second; a $D418 sample (a "digi") is loud on the 6581 and faint on the
 * 8580.
 */

#include "harness.h"

#include "sid.h"
#include "sound_card.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr double PAL = 985'248;
    constexpr uint64_t SECOND = 985'248; ///< PAL SID clocks
    constexpr uint8_t CTRL1 = 0x04, AD1 = 0x05, SR1 = 0x06, CTRL3 = 0x12, AD3 = 0x13, SR3 = 0x14;
    constexpr uint8_t FC_LO = 0x15, FC_HI = 0x16, RES_FILT = 0x17, MODE_VOL = 0x18;

    void set_freq(Sid &s, unsigned voice, double hz)
    {
        const auto f = static_cast<unsigned>(std::lround(hz * 16'777'216.0 / PAL));
        s.write(voice * 7, static_cast<uint8_t>(f));
        s.write(voice * 7 + 1, static_cast<uint8_t>(f >> 8));
    }

    void set_cutoff(Sid &s, unsigned fc)
    {
        s.write(FC_LO, static_cast<uint8_t>(fc & 7));
        s.write(FC_HI, static_cast<uint8_t>(fc >> 3));
    }

    /// A voice playing `wave` at full envelope (attack 0, sustain F), settled.
    Sid playing(Sid::Model model, uint8_t wave, double hz, unsigned voice = 0, uint8_t sustain = 0xF0)
    {
        Sid s;
        s.set_model(model);
        set_freq(s, voice, hz);
        s.write(voice * 7 + 5, 0x00);
        s.write(voice * 7 + 6, sustain);
        s.write(MODE_VOL, 0x0F);
        s.write(voice * 7 + 4, static_cast<uint8_t>(wave | Sid::GATE));
        s.clock_audio(40'000); // past the ADSR delay, the attack, and decay to the sustain level
        return s;
    }

    struct Range
    {
        float lo{1e9f}, hi{-1e9f};
        double sum{0}, sq{0};
        unsigned n{0};
        float mean() const { return static_cast<float>(sum / n); }
        /// AC RMS: about the mean.
        float rms() const { return static_cast<float>(std::sqrt(sq / n - (sum / n) * (sum / n))); }
    };

    /// The output, clock by clock, for `clocks`.
    Range watch(Sid &s, unsigned clocks)
    {
        Range r;
        for (unsigned i = 0; i < clocks; ++i)
        {
            s.clock_audio(1);
            const float v = s.output();
            r.lo = std::fmin(r.lo, v);
            r.hi = std::fmax(r.hi, v);
            r.sum += v;
            r.sq += static_cast<double>(v) * v;
            ++r.n;
        }
        return r;
    }

    bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
    double db(double ratio) { return 20 * std::log10(ratio); }
} // namespace

// --- the chip ----------------------------------------------------------------

TEST(a_silent_chip_puts_out_the_mixers_dc_times_the_volume)
{
    Sid old6581, new8580;
    new8580.set_model(Sid::Model::Mos8580);
    for (Sid *s : {&old6581, &new8580})
    {
        CHECK_EQ(s->clock_audio(1000), 0.0); // volume 0: nothing
        s->write(MODE_VOL, 0x0F);
    }
    CHECK(near(static_cast<float>(old6581.clock_audio(1000) / 1000), -0.6f, 1e-5f));
    CHECK(near(static_cast<float>(new8580.clock_audio(1000) / 1000), -0.03f, 1e-5f));
    old6581.write(MODE_VOL, 0x05);
    CHECK(near(static_cast<float>(old6581.clock_audio(10) / 10), -0.2f, 1e-5f));
}

TEST(a_sawtooth_spans_the_dac_less_its_zero)
{
    // 8580: the zero mid-range, so -1 to +1 (less the mixer's faint DC).
    Sid s = playing(Sid::Model::Mos8580, Sid::SAW, 240.5); // a 4096-clock period: every step
    CHECK_EQ(s.voice(0).envelope, 0xFF);
    Range r = watch(s, 8192);
    CHECK(near(r.lo, -1.0f - 0.03f, 2e-3f));
    CHECK(near(r.hi, 0.9995f - 0.03f, 2e-3f));
    // 6581: the zero at $380, so the swing sits high, -0.44 to +1.56, plus the mixer's -0.6.
    s = playing(Sid::Model::Mos6581, Sid::SAW, 240.5);
    r = watch(s, 8192);
    CHECK(near(r.lo, -0.4375f - 0.6f, 2e-3f));
    CHECK(near(r.hi, 1.5615f - 0.6f, 2e-3f));
}

TEST(the_envelope_and_the_volume_scale_a_voice)
{
    Sid loud = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 500);
    const float full = watch(loud, 20'000).rms();
    Sid half = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 500, 0, 0x80); // sustain 8: envelope $88
    CHECK_EQ(half.voice(0).envelope, 0x88);
    CHECK(near(watch(half, 20'000).rms() / full, 0x88 / 255.0f, 0.01f));
    Sid quiet = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 500);
    quiet.write(MODE_VOL, 0x07);
    CHECK(near(watch(quiet, 20'000).rms() / full, 7 / 15.0f, 0.01f));
}

TEST(voice_3_off_unless_it_is_filtered)
{
    Sid s = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 500, 2);
    CHECK(watch(s, 20'000).rms() > 0.3f);
    s.write(MODE_VOL, 0x8F); // 3OFF
    CHECK(watch(s, 20'000).rms() < 1e-6f);
    s.write(RES_FILT, 0x04); // through the filter: 3OFF doesn't reach it
    s.write(MODE_VOL, 0x9F); // and low-pass out
    set_cutoff(s, 2047);
    CHECK(watch(s, 20'000).rms() > 0.3f);
}

TEST(the_cutoff_curves_ends)
{
    Sid s;
    s.set_model(Sid::Model::Mos8580);
    CHECK(near(s.cutoff_hz(), 30, 0.5f));
    set_cutoff(s, 2047);
    CHECK(near(s.cutoff_hz(), 12'500, 1));
    set_cutoff(s, 1024);
    CHECK(near(s.cutoff_hz(), 6'268, 5)); // linear
    s.set_model(Sid::Model::Mos6581);
    CHECK(s.cutoff_hz() > 3'000 && s.cutoff_hz() < 4'500); // the 6581's sag in the middle
    set_cutoff(s, 0);
    CHECK(near(s.cutoff_hz(), 220, 0.5f));
    set_cutoff(s, 2047);
    CHECK(near(s.cutoff_hz(), 18'000, 1));
}

namespace
{
    /// Voice 1's triangle at `hz` through the 8580's filter, cutoff 250 Hz, in `mode` ($10 LP, $20 BP, $40 HP).
    float filtered(double hz, uint8_t mode, uint8_t res = 0)
    {
        Sid s = playing(Sid::Model::Mos8580, Sid::TRIANGLE, hz);
        set_cutoff(s, 36); // 30 + 12470 x 36/2047: 249 Hz
        s.write(RES_FILT, static_cast<uint8_t>(res << 4 | 0x01));
        s.write(MODE_VOL, static_cast<uint8_t>(mode | 0x0F));
        watch(s, 20'000); // settle
        return watch(s, 40'000).rms();
    }
} // namespace

TEST(low_pass_passes_below_the_cutoff_and_cuts_12_db_an_octave_above)
{
    Sid unfiltered = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 62.5);
    const float open = watch(unfiltered, 40'000).rms();
    CHECK(near(static_cast<float>(db(filtered(62.5, 0x10) / open)), 0, 1.0f)); // two octaves under
    // Near four octaves up (the SID tops out at 3.85 kHz): two poles' 46 dB, less the triangle's harmonics
    const double up = db(filtered(3'800, 0x10) / filtered(62.5, 0x10));
    CHECK(up < -38 && up > -50);
    const double octave = db(filtered(1'000, 0x10) / filtered(500, 0x10));
    CHECK(octave < -10 && octave > -14); // 12 dB an octave, well above the cutoff
}

TEST(high_pass_band_pass_and_the_notch)
{
    CHECK(db(filtered(62.5, 0x40) / filtered(3'800, 0x40)) < -20); // HP: two octaves under, 24 dB
    const double centre = filtered(250, 0x20);
    CHECK(db(filtered(62.5, 0x20) / centre) < -7); // BP at Q 0.707: 6 dB an octave each side, softly
    CHECK(db(filtered(1'000, 0x20) / centre) < -7);
    // LP + HP is a notch: at the cutoff they cancel; well away, one of them passes
    CHECK(db(filtered(250, 0x50) / filtered(250, 0x10)) < -15);
    CHECK(near(static_cast<float>(db(filtered(3'800, 0x50) / filtered(3'800, 0x40))), 0, 1));
    // LP + BP at the cutoff: a quarter-turn apart, so sqrt(2) of either
    CHECK(near(static_cast<float>(db(filtered(250, 0x30) / filtered(250, 0x10))), 3, 1.5f));
}

TEST(resonance_raises_the_band_pass_peak_by_q)
{
    // The 8580's Q: 2^((res - 4)/8), 0.707 to 2.59: band-pass at the cutoff by that ratio, 11.3 dB.
    const double gain = db(filtered(250, 0x20, 15) / filtered(250, 0x20, 0));
    CHECK(gain > 9.5 && gain < 13);
    // The 6581's: 0.707 + res/15, up to 1.71: 7.7 dB.
    auto bp6581 = [&](uint8_t res) {
        Sid v = playing(Sid::Model::Mos6581, Sid::TRIANGLE, 1'000);
        set_cutoff(v, 556); // 220 + 17780 x (556/2047)^2.4: 1 kHz on the 6581's curve
        v.write(RES_FILT, static_cast<uint8_t>(res << 4 | 0x01));
        v.write(MODE_VOL, 0x2F);
        watch(v, 20'000);
        return watch(v, 40'000).rms();
    };
    const double g6581 = db(bp6581(15) / bp6581(0));
    CHECK(g6581 > 6 && g6581 < 9.5);
}

TEST(a_muted_voice_leaves_the_output_and_the_chip_runs_on)
{
    Sid s = playing(Sid::Model::Mos8580, Sid::TRIANGLE, 500);
    s.set_muted(0x01);
    CHECK(watch(s, 20'000).rms() < 1e-6f);
    CHECK_EQ(s.voice(0).envelope, 0xFF);
    const uint32_t was = s.voice(0).accumulator;
    s.clock_audio(100);
    CHECK_NE(s.voice(0).accumulator, was);
    s.set_muted(0);
    CHECK(watch(s, 20'000).rms() > 0.3f);
}

TEST(an_attack_from_zero_is_not_silence)
{
    // Gate on: the envelope is 0 but climbing, so the output must follow it even in one long call.
    // A 6581 sawtooth sits high (its zero at $380), so its mean shows the envelope: about +0.56 at full.
    Sid s;
    set_freq(s, 0, 2'000);
    s.write(AD1, 0x00), s.write(SR1, 0xF0), s.write(MODE_VOL, 0x0F);
    s.write(CTRL1, Sid::SAW | Sid::GATE);
    const double mean = s.clock_audio(40'000) / 40'000;
    CHECK(mean > -0.6 + 0.4 && mean < -0.6 + 0.6);
}

TEST(the_filter_settles_to_zero_not_to_denormals)
{
    // Voice 1 rings the filter, then goes straight to the mixer: the filter,
    // fed nothing, decays, while the voice keeps the chip from resting.
    Sid s = playing(Sid::Model::Mos6581, Sid::PULSE, 300);
    s.write(0x03, 0x08);
    set_cutoff(s, 2000);
    s.write(RES_FILT, 0x01);
    s.write(MODE_VOL, 0x1F);
    s.clock_audio(5'000);
    s.write(RES_FILT, 0x00);
    for (int i = 0; i < 20; ++i)
    {
        s.clock_audio(50'000);
        for (const float v : s.filter_state())
            CHECK(std::fpclassify(v) != FP_SUBNORMAL);
    }
    for (const float v : s.filter_state())
        CHECK_EQ(v, 0.0f);
}

TEST(reset_stills_the_filter)
{
    Sid s = playing(Sid::Model::Mos8580, Sid::PULSE, 300);
    s.write(0x03, 0x08); // pulse width $800
    set_cutoff(s, 200);
    s.write(RES_FILT, 0xF1);
    s.write(MODE_VOL, 0x1F);
    s.clock_audio(5'000); // ringing
    s.reset();
    s.write(MODE_VOL, 0x1F);
    const Range r = watch(s, 200);
    CHECK_EQ(r.lo, r.hi); // nothing but the mixer's DC
    CHECK(near(r.lo, -0.03f, 1e-6f));
}

TEST(the_audio_path_leaves_the_chip_as_clock_would)
{
    for (const bool sync : {false, true})
    {
        Sid a, b;
        for (Sid *s : {&a, &b})
        {
            set_freq(*s, 0, 440), set_freq(*s, 2, 97);
            s->write(AD1, 0x2A), s->write(SR1, 0x84), s->write(AD3, 0x11), s->write(SR3, 0xF3);
            s->write(MODE_VOL, 0x1F), s->write(RES_FILT, 0xF1);
            s->write(CTRL1, static_cast<uint8_t>(Sid::SAW | Sid::GATE | (sync ? Sid::SYNC : 0)));
            s->write(CTRL3, Sid::NOISE | Sid::GATE);
        }
        a.clock(123'457);
        for (unsigned i = 0; i < 123'457; i += 1'000)
            b.clock_audio(std::min(1'000u, 123'457 - i));
        for (unsigned n = 0; n < 3; ++n)
        {
            CHECK_EQ(a.voice(n).accumulator, b.voice(n).accumulator);
            CHECK_EQ(a.voice(n).noise, b.voice(n).noise);
            CHECK_EQ(a.voice(n).envelope, b.voice(n).envelope);
            CHECK_EQ(a.voice(n).rate_count, b.voice(n).rate_count);
        }
        CHECK_EQ(a.read(Sid::OSC3), b.read(Sid::OSC3));
    }
}

// --- the card --------------------------------------------------------------

namespace
{
    std::vector<float> take(SoundCard &card)
    {
        std::vector<float> out(2 * 50'000);
        out.resize(2 * card.take_audio(out.data(), 50'000));
        return out;
    }

    /// SID A (or B) on the card playing a triangle, written by the host with the 6510 stopped.
    void tone(SoundCard &card, unsigned chip, double hz)
    {
        const unsigned base = chip ? SoundCard::SID_B : SoundCard::SID_A;
        const auto f = static_cast<unsigned>(std::lround(hz * 16'777'216.0 / PAL));
        card.write(base + 0, static_cast<uint8_t>(f));
        card.write(base + 1, static_cast<uint8_t>(f >> 8));
        card.write(base + AD1, 0x00);
        card.write(base + SR1, 0xF0);
        card.write(base + MODE_VOL, 0x0F);
        card.write(base + CTRL1, Sid::TRIANGLE | Sid::GATE);
    }

    float rms(const std::vector<float> &s, unsigned ch, size_t from = 0)
    {
        double sq = 0;
        size_t n = 0;
        for (size_t i = 2 * from + ch; i < s.size(); i += 2, ++n)
            sq += static_cast<double>(s[i]) * s[i];
        return n ? static_cast<float>(std::sqrt(sq / static_cast<double>(n))) : 0;
    }
} // namespace

TEST(a_second_of_sid_clocks_is_a_second_of_samples)
{
    for (const bool ntsc : {false, true})
    {
        SoundCard card;
        card.set_cpu_hz(ntsc ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ); // a host cycle is a SID clock
        card.write(SoundCard::CTRL, ntsc ? SoundCard::CTRL_NTSC : 0);
        card.set_audio_rate(48'000);
        size_t frames = 0;
        for (int i = 0; i < 4; ++i) // in quarters: half a second waits at most
        {
            card.tick(card.sid_hz() / 4);
            frames += take(card).size() / 2;
        }
        CHECK(frames >= 47'999 && frames <= 48'000); // a quarter's remainder may still be owed
    }
}

TEST(sid_a_is_left_and_sid_b_right)
{
    SoundCard card;
    card.set_cpu_hz(SoundCard::PAL_HZ);
    card.set_audio_rate(48'000);
    tone(card, 0, 440);
    card.tick(SECOND / 10);
    auto s = take(card);
    CHECK(rms(s, 0, 2400) > 0.03f);
    CHECK(rms(s, 1, 2400) < 1e-6f);
    card.write(SoundCard::CTRL, SoundCard::CTRL_BOTH);
    tone(card, 0, 440); // BOTH: these writes reach B too
    card.tick(SECOND / 10);
    s = take(card);
    CHECK(near(rms(s, 1, 2400), rms(s, 0, 2400), 1e-6f));
}

TEST(the_dc_drains_away_and_the_level_has_headroom)
{
    SoundCard card;
    card.set_cpu_hz(SoundCard::PAL_HZ);
    card.set_audio_rate(48'000);
    card.write(SoundCard::SID_A + MODE_VOL, 0x0F); // the 6581's mixer DC: a step
    card.tick(SECOND / 1000);
    auto s = take(card);
    const auto left = [](const std::vector<float> &v) { return v[v.size() - 2]; };
    CHECK(std::fabs(left(s)) > 0.05f); // -0.6 / 8 at first
    card.tick(SECOND / 5);
    s = take(card);
    CHECK(std::fabs(left(s)) < 0.001f); // 16 Hz: gone in 200 ms
    tone(card, 0, 440); // one voice, full envelope: a triangle 2.0 peak to peak, so +-1/8 once its DC is gone
    card.tick(SECOND / 2);
    s = take(card);
    float peak = 0;
    for (size_t i = 2 * 4800; i < s.size(); i += 2)
        peak = std::fmax(peak, std::fabs(s[i]));
    CHECK(peak > 0.115f && peak < 0.135f);
}

TEST(the_model_is_both_chips_and_outlasts_a_reset)
{
    SoundCard card;
    CHECK(card.chip(0).model() == Sid::Model::Mos6581);
    card.set_model(Sid::Model::Mos8580);
    CHECK(card.chip(0).model() == Sid::Model::Mos8580);
    CHECK(card.chip(1).model() == Sid::Model::Mos8580);
    card.reset();
    CHECK(card.chip(1).model() == Sid::Model::Mos8580); // a jumper
}

TEST(samples_wait_half_a_second_at_most_and_none_without_a_rate)
{
    SoundCard card;
    card.set_cpu_hz(SoundCard::PAL_HZ);
    card.tick(SECOND);
    CHECK_EQ(take(card).size(), 0u); // off: none made
    card.set_audio_rate(48'000);
    card.tick(2 * SECOND);
    CHECK_EQ(card.audio_frames(), 24'000u);
    card.set_audio_rate(0);
    CHECK_EQ(card.audio_frames(), 0u);
}

TEST(a_full_buffer_costs_a_sid_write_little)
{
    // Nobody taking samples: each write's few new ones push the oldest out,
    // at the front of half a second's, without moving the rest.
    SoundCard card;
    card.set_cpu_hz(SoundCard::PAL_HZ);
    card.set_audio_rate(48'000);
    card.tick(SECOND);
    const auto t0 = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 50'000; ++i)
    {
        card.write(SoundCard::SID_A + MODE_VOL, i & 1 ? 0x00 : 0x0F);
        card.tick(21);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 500);
    CHECK_EQ(card.audio_frames(), 24'000u);
}

TEST(a_d418_sample_is_loud_on_the_6581_and_faint_on_the_8580)
{
    float swing[2]{};
    for (const auto model : {Sid::Model::Mos6581, Sid::Model::Mos8580})
    {
        SoundCard card;
        card.set_cpu_hz(SoundCard::PAL_HZ);
        card.set_model(model);
        card.set_audio_rate(48'000);
        // A 2 kHz square from the volume register alone: 15, 0, 15, 0, ...
        for (unsigned i = 0; i < 2'000; ++i)
        {
            card.write(SoundCard::SID_A + MODE_VOL, i & 1 ? 0x00 : 0x0F);
            card.tick(246);
        }
        swing[model == Sid::Model::Mos8580] = rms(take(card), 0, 4800);
    }
    CHECK(swing[0] > 0.03f); // 0.6 / 8 peak to peak
    CHECK(db(swing[1] / swing[0]) < -20); // 0.03 against 0.6: 26 dB down
}

int main() { return run_all(); }
