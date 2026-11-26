/**
 * @file test_sound_dsp.cpp
 * @brief The sound card's EQ and vis page (sound_dsp.h), driven directly.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * BusEq and BusVis on their own, as the firmware (sound_fw.h) runs them on its bus: each band's gain at its
 * centre within 1 dB, both ways, its width the cookbook's curve, and the preamp; every band ringing down to exact
 * silence within a second, so a dry spell takes the bulk path; a band at or above 0.45 of the bus rate bypassed,
 * just either side of the line; and a sine in the spectrum band its frequency falls in, with the VU and the last
 * 64 samples.
 */

#include "harness.h"

#include "sound_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr double PI = 3.14159265358979323846;
    using Gains = std::array<int8_t, BusEq::BANDS + 1>;

    /// `n` frames of a sine at `hz`, `amp` of full scale, at `rate`: the same bits on every target.
    std::vector<int16_t> sine(size_t n, double hz, double amp, uint32_t rate, double phase = 0)
    {
        std::vector<int16_t> out(n);
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<int16_t>(
                std::floor(amp * 32'767.0 * dsp::sin(2 * PI * hz * static_cast<double>(i) / rate + phase) + 0.5));
        return out;
    }

    /// `n` stereo frames of noise, the same for a seed every run.
    std::vector<int16_t> noise(size_t n, uint32_t seed)
    {
        std::vector<int16_t> out(2 * n);
        for (int16_t &s : out)
        {
            seed = seed * 1'103'515'245u + 12'345u;
            s = static_cast<int16_t>(seed >> 16);
        }
        return out;
    }

    Gains band(unsigned b, int8_t g)
    {
        Gains gains{};
        gains[b + 1] = g;
        return gains;
    }

    /// Stereo `frames` through an EQ of `gains`, enabled, at `rate`, from silence.
    std::vector<int16_t> eq_run(const std::vector<int16_t> &frames, const Gains &gains, uint32_t rate)
    {
        BusEq eq;
        eq.set(gains, true, rate);
        std::vector<int16_t> out(frames);
        for (size_t f = 0; f + 1 < out.size(); f += 2)
            eq.run(out[f], out[f + 1]);
        return out;
    }

    /// The stereo frames the bus carries for mono `samples`.
    std::vector<int16_t> both(const std::vector<int16_t> &samples)
    {
        std::vector<int16_t> out;
        for (const int16_t s : samples)
            out.insert(out.end(), {s, s});
        return out;
    }

    /// The RMS of one side of stereo `frames`, from frame `from`.
    double rms(const std::vector<int16_t> &frames, unsigned side, size_t from)
    {
        double sum = 0;
        size_t n = 0;
        for (size_t f = from; 2 * f + side < frames.size(); ++f, ++n)
            sum += static_cast<double>(frames[2 * f + side]) * frames[2 * f + side];
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0;
    }

    /// The cookbook's peaking filter at `f0`, `db` and Q 1.41, its gain in dB at `f` and `rate`: worked here with
    /// libm, apart from the card's.
    double peaking_db(double f0, double db, double f, double rate)
    {
        const double a = std::pow(10.0, db / 40), w0 = 2 * PI * f0 / rate, alpha = std::sin(w0) / (2 * 1.41);
        const double b[3] = {1 + alpha * a, -2 * std::cos(w0), 1 - alpha * a};
        const double d[3] = {1 + alpha / a, -2 * std::cos(w0), 1 - alpha / a};
        const double w = 2 * PI * f / rate;
        const auto mag = [&](const double *c) {
            const double re = c[0] + c[1] * std::cos(w) + c[2] * std::cos(2 * w);
            const double im = -c[1] * std::sin(w) - c[2] * std::sin(2 * w);
            return std::sqrt(re * re + im * im);
        };
        return 20 * std::log10(mag(b) / mag(d));
    }

    uint8_t level_db(double db)
    {
        return static_cast<uint8_t>(std::clamp(std::floor((db + 60) * 255 / 60 + 0.5), 0.0, 255.0));
    }
} // namespace

TEST(each_band_at_its_centre_gains_within_1_db)
{
    // A sine at each band's centre, the band at +12, +6, -6 and -12 dB and the rest flat, at 48 kHz: the level
    // out against in, both sides, once the filter has settled (the last half of a second).
    constexpr uint32_t RATE = 48'000;
    double worst = 0;
    for (unsigned b = 0; b < BusEq::BANDS; ++b)
    {
        const std::vector<int16_t> in = sine(RATE, BusEq::CENTRE_HZ[b], 0.2, RATE);
        for (const int8_t g : {int8_t{24}, int8_t{12}, int8_t{-12}, int8_t{-24}})
        {
            const std::vector<int16_t> out = eq_run(both(in), band(b, g), RATE);
            const double ref = rms(both(in), 0, RATE / 2);
            for (unsigned side = 0; side < 2; ++side)
            {
                const double db = 20 * std::log10(rms(out, side, RATE / 2) / ref);
                worst = std::max(worst, std::fabs(db - g / 2.0));
                CHECK(std::fabs(db - g / 2.0) < 1.0);
                if (side == 0 && (g == 24 || g == -24))
                    std::printf("  %5u Hz at %+3d dB: %+6.2f dB\n", BusEq::CENTRE_HZ[b], g / 2, db);
            }
        }
    }
    std::printf("  worst: %.3f dB off\n", worst);
    // Half an octave either side of the centre, the cookbook's curve at Q 1.41 (within 0.1 dB): the width is right.
    for (unsigned b = 0; b < 8; ++b)
        for (const double f : {BusEq::CENTRE_HZ[b] / std::sqrt(2.0), BusEq::CENTRE_HZ[b] * std::sqrt(2.0)})
        {
            const std::vector<int16_t> in = sine(RATE, f, 0.2, RATE);
            const double db =
                20 * std::log10(rms(eq_run(both(in), band(b, 24), RATE), 0, RATE / 2) / rms(both(in), 0, RATE / 2));
            const double want = peaking_db(BusEq::CENTRE_HZ[b], 12, f, RATE);
            if (b % 3 == 0)
                std::printf("  %5u Hz band +12 dB, at %7.1f Hz: %+6.2f dB (the curve %+6.2f)\n", BusEq::CENTRE_HZ[b],
                            f, db, want);
            CHECK(std::fabs(db - want) < 0.1);
        }
    // The preamp: the whole stream by its gain.
    const std::vector<int16_t> in = sine(RATE, 440, 0.2, RATE);
    for (const int8_t g : {int8_t{24}, int8_t{-24}, int8_t{3}})
    {
        Gains gains{};
        gains[0] = g;
        const double db = 20 * std::log10(rms(eq_run(both(in), gains, RATE), 0, 0) / rms(both(in), 0, 0));
        std::printf("  the preamp at %+.1f dB: %+6.3f dB\n", g / 2.0, db);
        CHECK(std::fabs(db - g / 2.0) < 0.05);
    }
}

TEST(every_band_rings_down_to_exact_silence_within_a_second)
{
    // A burst of noise through each band alone, at a small and the biggest gain either way, at four rates, then
    // silence: within a second of frames the filter's state is exactly 0, so a dry spell takes the bulk path.
    unsigned stuck = 0, runs = 0;
    for (const uint32_t rate : {8'000u, 22'050u, 44'100u, 48'000u})
        for (unsigned b = 0; b < BusEq::BANDS; ++b)
            for (const int8_t g : {int8_t{1}, int8_t{-1}, int8_t{24}, int8_t{-24}})
            {
                if (!BusEq::band_runs(b, rate))
                    continue;
                BusEq eq;
                eq.set(band(b, g), true, rate);
                const std::vector<int16_t> burst = noise(2'000, b * 7 + static_cast<uint32_t>(g + 30));
                for (size_t f = 0; f < burst.size(); f += 2)
                {
                    int16_t l = burst[f], r = burst[f + 1];
                    eq.run(l, r);
                }
                CHECK(eq.ringing());
                for (uint32_t f = 0; f < rate && eq.ringing(); ++f)
                {
                    int16_t l = 0, r = 0;
                    eq.run(l, r);
                }
                ++runs;
                if (eq.ringing())
                {
                    ++stuck;
                    std::printf("  still ringing: %u Hz at %+.1f dB, %u Hz\n", BusEq::CENTRE_HZ[b], g / 2.0, rate);
                }
            }
    std::printf("  %u of %u still ringing after a second\n", stuck, runs);
    CHECK_EQ(stuck, 0u);
}

TEST(a_band_at_or_above_045_of_the_bus_rate_is_bypassed)
{
    // Each band at +12 dB alone, at rates either side of where it reaches 0.45 of the rate: at or above, the
    // stream bit for bit; below, not.
    const std::vector<int16_t> in = noise(4'000, 9);
    struct Edge
    {
        unsigned band;
        uint32_t below; ///< the band runs: under 0.45 of this rate
        uint32_t at;    ///< bypassed
    };
    // 0.45 x 26,667 = 12,000.15 and x 26,666 = 11,999.7; 14 kHz: 31,112 and 31,111; 16 kHz: 35,556 and 35,555;
    // 6 kHz: 13,334 and 13,333 (at exactly 0.45: 6 kHz at 13,333.3, below; a rate the band sits on exactly:
    // 3 kHz at 6,666.6 is no rate, so 12 kHz at 26,667 is the nearest). And 3 kHz under 8 kHz: bypassed.
    for (const Edge e : {Edge{7, 26'667, 26'666}, Edge{8, 31'112, 31'111}, Edge{9, 35'556, 35'555},
                         Edge{6, 13'334, 13'333}})
    {
        CHECK(BusEq::band_runs(e.band, e.below));
        CHECK(!BusEq::band_runs(e.band, e.at));
        for (const uint32_t rate : {e.below, e.at})
        {
            const bool same = eq_run(in, band(e.band, 24), rate) == in;
            std::printf("  %5u Hz at %u Hz (0.45 of it %.2f): %s\n", BusEq::CENTRE_HZ[e.band], rate, 0.45 * rate,
                        same ? "bypassed" : "runs");
            CHECK_EQ(same, rate == e.at);
        }
    }
    // Every band at 48 kHz runs; at 8 kHz only 60 Hz to 3 kHz (the 3 kHz band: 3,000 < 3,600).
    for (unsigned b = 0; b < BusEq::BANDS; ++b)
    {
        CHECK(BusEq::band_runs(b, 48'000));
        CHECK_EQ(BusEq::band_runs(b, 8'000), BusEq::CENTRE_HZ[b] <= 3'000);
    }
}

TEST(a_sine_lands_in_its_spectrum_band)
{
    // A sine on bin k of the 64-point FFT (k x rate / 64), half of full scale (-6 dB): band k - 1 is the page's
    // highest, at -6 dBFS (within a step); the bins two or more away below -60 dBFS (the window's leakage). The
    // VU near -6 dBFS; the samples the last 64 fed, mono, high bytes. The page as the last frame is fed. Bin 32
    // (Nyquist, band 31) is real: a cosine there.
    for (const uint32_t rate : {48'000u, 22'050u, 8'000u})
        for (const unsigned k : {1u, 4u, 9u, 16u, 31u, 32u})
        {
            const std::vector<int16_t> in =
                sine(4'000, static_cast<double>(k) * rate / 64, 0.5, rate, k == 32 ? PI / 2 : 0);
            BusVis vis;
            for (const int16_t s : in)
                vis.feed(s, s); // mono on both sides
            std::array<uint8_t, BusVis::PAGE> p{};
            vis.snapshot(p);
            const auto top = std::max_element(p.begin(), p.begin() + 32);
            bool quiet = true;
            for (size_t i = 0; i < 32; ++i)
                if ((i + 2 < k - 1 || i > k + 1) && p[i] != 0)
                    quiet = false;
            std::printf("  %5u Hz at %u Hz: band %u at %u, highest %u (want %u), far bands %s, VU %u %u\n",
                        static_cast<unsigned>(k * rate / 64), rate, k - 1, p[k - 1], *top, level_db(-6.0206),
                        quiet ? "silent" : "not silent", p[32], p[33]);
            CHECK_EQ(p[k - 1], *top); // the highest (at Nyquist, band 30 as high: the window's leakage from both sides)
            CHECK(std::abs(static_cast<int>(*top) - level_db(-6.0206)) <= 1);
            CHECK(quiet);
            CHECK(std::abs(static_cast<int>(p[32]) - level_db(-6.0206)) <= 3);
            CHECK_EQ(p[32], p[33]);
            for (size_t i = 0; i < 64; ++i)
            {
                const int mono = in[in.size() - 64 + i];
                CHECK_EQ(p[34 + i], static_cast<uint8_t>((mono + 32'768) >> 8));
            }
            // Then silence (the stream ended): since that snapshot nothing has played, so the VU starts again at
            // silence, and the page is silence.
            vis.silence(rate / 100 + 2);
            std::array<uint8_t, BusVis::PAGE> again{};
            vis.snapshot(again);
            CHECK(again[32] == 0 && again[33] == 0);
            CHECK(std::all_of(again.begin(), again.begin() + 34, [](uint8_t b) { return b == 0; }));
            CHECK(std::all_of(again.begin() + 34, again.end(), [](uint8_t b) { return b == 0x80; }));
        }
}

int main() { return run_all(); }
