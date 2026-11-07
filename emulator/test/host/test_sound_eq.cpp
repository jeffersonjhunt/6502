/**
 * @file test_sound_eq.cpp
 * @brief Sound card revision 2's EQ, vis page and volume (hw/sound-rev2.md, step 5).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card alone, at the test tap (the bus after the EQ and volume, before any
 * resampling): the EQ disabled, or flat, is bit for bit the stream; each
 * band's gain at its centre within 1 dB, both ways; a band at or above 0.45
 * of the bus rate bypassed, just either side of the line; a sine in the
 * spectrum band its frequency falls in, the VU and the last 64 samples; VOLL
 * and VOLR at 0 silent and between rounded, on the bus and on the SIDs'
 * $D418, which reads back as written; and the EQ's sound and the vis page the
 * same whatever the audio rate and however the time is ticked, to a golden
 * hash that WebAssembly's build must make too.
 */

#include "harness.h"

#include "minimp3.h"
#include "sound_card.h"
#include "sound_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t HZ = 4'800'000; ///< the host's clock in these tests: a 48 kHz frame is 100 cycles
    constexpr uint8_t MONO16 = SoundCard::PCMFMT_16BIT, STEREO16 = SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT;
    constexpr double PI = 3.14159265358979323846;

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

    void start(SoundCard &c, uint8_t fmt, uint32_t rate)
    {
        c.write(SoundCard::RATE, static_cast<uint8_t>(rate));
        c.write(SoundCard::RATE + 1, static_cast<uint8_t>(rate >> 8));
        c.write(SoundCard::PCMFMT, fmt);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    }

    void put(SoundCard &c, const std::vector<int16_t> &samples)
    {
        for (const int16_t s : samples)
        {
            c.write(SoundCard::PCMDATA, static_cast<uint8_t>(s));
            c.write(SoundCard::PCMDATA, static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
        }
    }

    /// The EQ: the gains (the preamp, then the bands, in half-dB) and the enable.
    void eq(SoundCard &c, const std::array<int8_t, 11> &gains, bool on)
    {
        c.write(SoundCard::EQIDX, 0);
        for (const int8_t g : gains)
            c.write(SoundCard::EQVAL, static_cast<uint8_t>(g));
        c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
        c.write(SoundCard::EQVAL, on ? 1 : 0);
    }

    std::array<int8_t, 11> band(unsigned b, int8_t g)
    {
        std::array<int8_t, 11> gains{};
        gains[b + 1] = g;
        return gains;
    }

    /// A snapshot (VISIDX written 0), then the page read through VISDATA, and the 0s past it.
    std::vector<uint8_t> page(SoundCard &c, size_t n = SoundCard::VIS_BYTES)
    {
        c.write(SoundCard::VISIDX, 0);
        std::vector<uint8_t> p(n);
        for (uint8_t &b : p)
            b = c.read(SoundCard::VISDATA);
        return p;
    }

    uint32_t pos(SoundCard &c)
    {
        const uint32_t lo = c.read(SoundCard::POS); // latches the rest
        const uint32_t b1 = c.read(SoundCard::POS + 1), b2 = c.read(SoundCard::POS + 2);
        const uint32_t b3 = c.read(SoundCard::POS + 3);
        return lo | b1 << 8 | b2 << 16 | b3 << 24;
    }

    /// `samples` (mono, or stereo with `fmt`) played whole at `rate`, as the tap has them: stereo frames. With
    /// `last`, the vis page (100 bytes) as the last frame has played, before the stream ends.
    std::vector<int16_t> play(SoundCard &c, const std::vector<int16_t> &samples, uint8_t fmt, uint32_t rate,
                              uint64_t chunk = 7, std::vector<uint8_t> *last = nullptr)
    {
        c.set_tap(true);
        start(c, fmt, rate);
        put(c, samples);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        const size_t frames = fmt & SoundCard::PCMFMT_STEREO ? samples.size() / 2 : samples.size();
        uint64_t cycles = (uint64_t{frames} + 2) * HZ / rate + HZ / 100; // in 64 bits: size_t is 32 in WebAssembly
        for (; cycles >= chunk; cycles -= chunk)
        {
            c.tick(chunk);
            if (last && last->empty() && pos(c) >= frames)
                *last = page(c, 100);
        }
        c.tick(cycles);
        CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_ENDED);
        return c.take_tap();
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

    std::vector<uint8_t> mp3_file(const std::string &name)
    {
        std::ifstream in(std::string(EMU_MP3_DIR) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// `ticks` of `chunk` host cycles, or (chunk 0) one tick of them all.
    void ticks(SoundCard &c, uint64_t cycles, uint64_t chunk)
    {
        if (!chunk)
            return c.tick(cycles);
        for (; cycles >= chunk; cycles -= chunk)
            c.tick(chunk);
        c.tick(cycles);
    }

    uint64_t fnv(uint64_t h, uint8_t b) { return (h ^ b) * 0x100000001B3ull; }
} // namespace

TEST(the_eq_disabled_or_flat_is_bit_exact_at_the_tap)
{
    // Gains on every band and the preamp, the EQ off; then on with every gain 0: the stream, bit for bit.
    const std::vector<int16_t> in = noise(20'000, 5);
    std::array<int8_t, 11> gains;
    for (size_t i = 0; i < gains.size(); ++i)
        gains[i] = static_cast<int8_t>(i % 2 ? 24 : -17);
    for (const bool flat : {false, true})
        for (const uint32_t rate : {8'000u, 44'100u, 48'000u})
        {
            SoundCard c;
            c.set_cpu_hz(HZ);
            eq(c, flat ? std::array<int8_t, 11>{} : gains, flat);
            const std::vector<int16_t> got = play(c, in, STEREO16, rate);
            std::printf("  %s at %u Hz: %s\n", flat ? "on, flat" : "off, gains set", rate,
                        got == in ? "the stream" : "different");
            CHECK(got == in);
        }
    // And on, with gains: not the stream (so the test above can fail).
    SoundCard c;
    c.set_cpu_hz(HZ);
    eq(c, gains, true);
    CHECK(play(c, in, STEREO16, 48'000) != in);
}

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
            SoundCard c;
            c.set_cpu_hz(HZ);
            eq(c, band(b, g), true);
            const std::vector<int16_t> out = play(c, in, MONO16, RATE);
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
            SoundCard c;
            c.set_cpu_hz(HZ);
            eq(c, band(b, 24), true);
            const double db = 20 * std::log10(rms(play(c, in, MONO16, RATE), 0, RATE / 2) / rms(both(in), 0, RATE / 2));
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
        SoundCard c;
        c.set_cpu_hz(HZ);
        std::array<int8_t, 11> gains{};
        gains[0] = g;
        eq(c, gains, true);
        const double db = 20 * std::log10(rms(play(c, in, MONO16, RATE), 0, 0) / rms(both(in), 0, 0));
        std::printf("  the preamp at %+.1f dB: %+6.3f dB\n", g / 2.0, db);
        CHECK(std::fabs(db - g / 2.0) < 0.05);
    }
}

TEST(a_gain_written_while_it_plays_takes_effect_from_the_next_frame)
{
    // A 1 kHz sine with the EQ on and flat, the 1 kHz band raised 12 dB halfway: the first half the stream, the
    // second (once the filter has settled) 12 dB up. Then the EQ disabled the same way: the stream again, exactly.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> in = sine(RATE, 1'000, 0.2, RATE);
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.set_tap(true);
    eq(c, {}, true);
    start(c, MONO16, RATE);
    put(c, in);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    const auto run_for = [&](uint64_t cycles) {
        for (; cycles >= 7; cycles -= 7)
            c.tick(7);
        c.tick(cycles);
    };
    run_for(HZ / 3);
    c.write(SoundCard::EQIDX, 5); // the 1 kHz band
    c.write(SoundCard::EQVAL, 24);
    run_for(HZ / 3);
    c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
    c.write(SoundCard::EQVAL, 0);
    run_for(HZ / 2);
    const std::vector<int16_t> out = c.take_tap();
    const size_t third = RATE / 3;
    std::vector<int16_t> parts[3];
    for (size_t k = 0; k < 3; ++k)
        parts[k].assign(out.begin() + static_cast<std::ptrdiff_t>(2 * (k * third + 2'000)),
                        out.begin() + static_cast<std::ptrdiff_t>(2 * ((k + 1) * third - 100)));
    const double ref = rms(both(in), 0, 0);
    const double db[3] = {20 * std::log10(rms(parts[0], 0, 0) / ref), 20 * std::log10(rms(parts[1], 0, 0) / ref),
                          20 * std::log10(rms(parts[2], 0, 0) / ref)};
    std::printf("  flat %+.2f dB, raised %+.2f dB, disabled %+.2f dB\n", db[0], db[1], db[2]);
    CHECK(std::fabs(db[0]) < 0.05);
    CHECK(std::fabs(db[1] - 12) < 0.2);
    std::vector<int16_t> tail(out.end() - 2'000, out.end()), want;
    const std::vector<int16_t> b2 = both(in);
    want.assign(b2.begin() + static_cast<std::ptrdiff_t>(out.size() - 2'000),
                b2.begin() + static_cast<std::ptrdiff_t>(out.size()));
    CHECK(tail == want); // disabled: the stream bit for bit from there
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

TEST(the_vis_page_goes_quiet_after_a_pause_the_end_or_run_cleared)
{
    // A 1 kHz tone at 48 kHz, paused 33 cycles past a frame: the page 2,470 cycles in has 25 frames of silence
    // after the tone (on the stream's own frame times, the first 67 cycles in), and 0.1 s in is silence. Released,
    // 50 ms on: the tone. Then RUN cleared: the page already taken unchanged, the next snapshot silence (before any
    // tick), the VU too. RUN again
    // with EOF: what's left plays, and 0.1 s after the end the page is silence. The same at audio rates 0 and 48 kHz,
    // ticked in 7-cycle steps, by milliseconds, or one tick to each event.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> in = sine(RATE / 2, 1'000, 0.5, RATE);
    std::vector<uint8_t> first;
    bool same = true;
    for (const uint32_t audio : {0u, 48'000u})
        for (const uint64_t chunk : {uint64_t{7}, uint64_t{HZ / 1'000}, uint64_t{0}})
        {
            SoundCard c;
            c.set_cpu_hz(HZ);
            c.set_audio_rate(audio);
            start(c, MONO16, RATE);
            put(c, in);
            std::vector<uint8_t> pages;
            const auto snap = [&] {
                const std::vector<uint8_t> p = page(c);
                pages.insert(pages.end(), p.begin(), p.end());
                float buf[2 * 4'096];
                while (c.take_audio(buf, 4'096))
                {
                }
            };
            ticks(c, HZ * 3 / 10 + 33, chunk);
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
            ticks(c, 2'470, chunk);
            snap(); // 0
            ticks(c, HZ / 10, chunk);
            snap(); // 1
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
            ticks(c, HZ / 20, chunk);
            snap(); // 2
            c.write(SoundCard::PCMCTL, 0);
            c.write(SoundCard::VISIDX, 40); // not 0: no new snapshot, the one before read, as it was
            CHECK_EQ(c.read(SoundCard::VISDATA), pages[2 * SoundCard::VIS_BYTES + 40]);
            snap(); // 3
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            ticks(c, HZ * 3 / 10, chunk);
            CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_ENDED);
            snap(); // 4
            if (first.empty())
                first = pages;
            std::printf("  audio %5u Hz, %s: %s\n", audio,
                        chunk == 7 ? "7-cycle ticks" : chunk ? "1 ms ticks" : "one tick to each event",
                        pages == first ? "the same pages" : "different");
            same = same && pages == first;
        }
    CHECK(same);
    const auto silent = [&](size_t n) {
        const auto *p = &first[n * SoundCard::VIS_BYTES];
        return std::all_of(p, p + 32, [](uint8_t b) { return b == 0; }) &&
               std::all_of(p + 34, p + 98, [](uint8_t b) { return b == 0x80; });
    };
    // 2,470 cycles into the pause: 25 frames of silence at the end of the samples, the tone before them.
    const uint8_t *p0 = &first[34];
    const size_t quiet = static_cast<size_t>(std::count(p0 + 39, p0 + 64, 0x80));
    const bool tone = p0[38] != 0x80 && std::count(p0, p0 + 39, 0x80) < 30;
    std::printf("  paused 2,470 cycles: last 25 samples %zu silent, the tone before %s; 0.1 s: %s; playing: %s; "
                "RUN cleared: %s; ended: %s\n",
                quiet, tone ? "there" : "gone", silent(1) ? "silent" : "not", silent(2) ? "silent" : "not",
                silent(3) ? "silent" : "not", silent(4) ? "silent" : "not");
    CHECK_EQ(quiet, size_t{25});
    CHECK(tone);
    CHECK(silent(1));
    CHECK(!silent(2));
    CHECK(silent(3));
    CHECK(silent(4));
    CHECK(first[3 * SoundCard::VIS_BYTES + 32] == 0); // RUN cleared straight after a snapshot: no peak since
}

TEST(after_a_seek_the_vis_page_goes_quiet_and_writes_with_run_clear_keep_it)
{
    // A seek (FLUSH with RUN) and no data for 0.1 s: the page silence. Raw PCM, buffering again at its rate; an
    // MP3, buffering without a rate until its next first frame (the bus's last, then); and an MP3 sought into
    // 64 KB of text, $81. Then RUN cleared, and PCMCTL written with FLUSH and with EOF, RUN still clear: the page
    // taken before reads as it was; the next snapshot is silence.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> tone = sine(RATE / 2, 1'000, 0.5, RATE);
    const std::vector<uint8_t> mp3 = mp3_file("cbr128_joint_44k.mp3");
    const auto silent = [](const std::vector<uint8_t> &p) {
        return std::all_of(p.begin(), p.begin() + 32, [](uint8_t b) { return b == 0; }) &&
               std::all_of(p.begin() + 34, p.begin() + 98, [](uint8_t b) { return b == 0x80; });
    };
    for (const int kind : {0, 1, 2})
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        if (kind == 0)
        {
            start(c, MONO16, RATE);
            put(c, tone);
        }
        else
        {
            c.write(SoundCard::RATE, static_cast<uint8_t>(8'000)); // not the MP3's rate: none of its business
            c.write(SoundCard::RATE + 1, static_cast<uint8_t>(8'000 >> 8));
            c.write(SoundCard::CODEC, 1);
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
            for (const uint8_t b : mp3)
                c.write(SoundCard::PCMDATA, b);
        }
        // T leaves part of a frame owed at the seek (13 x 44,100 / HZ, an eighth), and D ends just short of a frame
        // boundary without it: the count is 10 only if the part owed is carried.
        constexpr uint64_t T = HZ / 5 + 13, D = 1'088;
        ticks(c, T, 7);
        const std::vector<uint8_t> before = page(c);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_FLUSH);
        if (kind == 1)
        {
            // D cycles after the seek, the frames of silence at the MP3's 44.1 kHz on its own frame times (playing
            // from cycle 0, so T cycles in it owes T x 44,100 mod HZ of a frame): as many $80s end the samples.
            ticks(c, D, 7);
            const std::vector<uint8_t> soon = page(c);
            const uint64_t want = (T * 44'100 % HZ + D * 44'100) / HZ;
            size_t quiet = 0;
            while (quiet < 64 && soon[97 - quiet] == 0x80)
                ++quiet;
            std::printf("  MP3, %llu cycles after the seek: %zu frames of silence (want %llu at 44.1 kHz)\n",
                        static_cast<unsigned long long>(D), quiet, static_cast<unsigned long long>(want));
            CHECK_EQ(quiet, static_cast<size_t>(want));
        }
        if (kind == 2)
            for (size_t i = 0; i < 65'600; ++i)
                c.write(SoundCard::PCMDATA, static_cast<uint8_t>('a' + i % 26));
        ticks(c, HZ / 10, 7);
        const uint8_t st = c.peek(SoundCard::STATE);
        const std::vector<uint8_t> after = page(c);
        static const char *const names[] = {"raw PCM", "MP3", "MP3 into text"};
        std::printf("  %s: before the seek %s; 0.1 s after, STATE $%02X, %s\n", names[kind],
                    silent(before) ? "silent" : "the sound", st, silent(after) ? "silent" : "not silent");
        CHECK(!silent(before));
        CHECK_EQ(st, kind == 2 ? SoundCard::STATE_NO_SYNC : SoundCard::STATE_BUFFERING);
        CHECK(silent(after));
    }
    // RUN clear: writes keep the page taken; the next snapshot's spectrum and samples are silence, and its VU what
    // played since the page taken (50 ms more of the tone), as after a pause for the same tail.
    std::vector<uint8_t> taken, next, paused;
    for (const bool pause : {false, true})
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        start(c, MONO16, RATE);
        put(c, tone);
        ticks(c, HZ / 5, 7);
        const std::vector<uint8_t> t = page(c);
        ticks(c, HZ / 20, 7);
        if (pause)
        {
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
            paused = page(c);
            continue;
        }
        taken = t;
        for (const uint8_t ctl : {uint8_t{0}, SoundCard::PCMCTL_FLUSH, SoundCard::PCMCTL_EOF})
            c.write(SoundCard::PCMCTL, ctl);
        c.write(SoundCard::VISIDX, 1); // not 0: the page as it is
        std::vector<uint8_t> kept(SoundCard::VIS_BYTES);
        kept[0] = taken[0];
        for (size_t i = 1; i < kept.size(); ++i)
            kept[i] = c.read(SoundCard::VISDATA);
        CHECK(kept == taken);
        std::printf("  RUN clear, then FLUSH and EOF: the page taken %s\n", kept == taken ? "as it was" : "changed");
        next = page(c);
    }
    std::printf("  the next snapshot: %s, VU %u %u; after a pause for the same tail, VU %u %u\n",
                silent(next) ? "silent" : "not silent", next[32], next[33], paused[32], paused[33]);
    CHECK(!silent(taken));
    CHECK(silent(next));
    CHECK_EQ(next[32], paused[32]);
    CHECK_EQ(next[33], paused[33]);
    CHECK(next[32] > 200);

    // /RESET: the page of silence, the VU 0 too (the peaks gone with the frames), read with no new snapshot.
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        start(c, MONO16, RATE);
        put(c, tone);
        ticks(c, HZ / 5, 7);
        c.reset();
        c.write(SoundCard::VISIDX, 1); // not 0: the page /RESET made
        std::vector<uint8_t> p(SoundCard::VIS_BYTES);
        for (size_t i = 1; i < p.size(); ++i)
            p[i] = c.read(SoundCard::VISDATA);
        std::printf("  after /RESET: %s, VU %u %u\n", silent(p) ? "silent" : "not silent", p[32], p[33]);
        CHECK(silent(p));
        CHECK(p[32] == 0 && p[33] == 0);
    }
}

TEST(a_new_host_clock_rescales_the_vis_taps_silence_owed)
{
    // A tone at 48 kHz on a 4.8 MHz host clock, ticked 0.1 s and 99 cycles (99/100 of a frame owed), then paused
    // and the clock made a tenth (480 kHz), in either order: one cycle more is 1/10 of a frame, so one frame of
    // silence falls due, not ten.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> tone = sine(RATE / 2, 997, 0.5, RATE);
    for (const bool clock_first : {false, true})
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        start(c, MONO16, RATE);
        put(c, tone);
        ticks(c, HZ / 10 + 99, 7);
        if (clock_first)
            c.set_cpu_hz(HZ / 10);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
        if (!clock_first)
            c.set_cpu_hz(HZ / 10);
        c.tick(1);
        const std::vector<uint8_t> p = page(c);
        size_t quiet = 0;
        while (quiet < 64 && p[97 - quiet] == 0x80)
            ++quiet;
        std::printf("  %s: %zu frame(s) of silence\n", clock_first ? "clock, then pause" : "pause, then clock", quiet);
        CHECK_EQ(quiet, size_t{1});
    }
}

TEST(a_new_rate_gives_the_eq_new_coefficients)
{
    // The 3 kHz band +12 dB, a 48 kHz stream played to its end; then on the same card, the EQ untouched, a
    // 22,050 Hz stream: its 3 kHz sine comes out +12 dB, the cookbook's curve at the new rate.
    SoundCard c;
    c.set_cpu_hz(HZ);
    eq(c, band(5, 24), true);
    play(c, noise(4'000, 21), STEREO16, 48'000);
    c.write(SoundCard::PCMCTL, 0);
    const std::vector<int16_t> in = sine(22'050, 3'000, 0.2, 22'050);
    const std::vector<int16_t> out = play(c, in, MONO16, 22'050);
    const double db = 20 * std::log10(rms(out, 0, 11'025) / rms(both(in), 0, 11'025));
    std::printf("  3 kHz at 22,050 Hz after 48 kHz: %+.2f dB (the curve %+.2f)\n", db,
                peaking_db(3'000, 12, 3'000, 22'050));
    CHECK(std::fabs(db - peaking_db(3'000, 12, 3'000, 22'050)) < 0.1);
}

TEST(a_new_run_or_a_seek_starts_the_eq_from_silence)
{
    // The 60 Hz band +12 dB (a long ring) and a stream of noise. Played on a fresh card; then on one that has just
    // played it to its end (the filters left ringing) with RUN cleared and set again; and on one that seeks back
    // (FLUSH with RUN) half way through: the second time, the tap the same as the fresh card's, bit for bit.
    const std::vector<int16_t> in = noise(6'000, 31);
    SoundCard fresh;
    fresh.set_cpu_hz(HZ);
    eq(fresh, band(0, 24), true);
    const std::vector<int16_t> want = play(fresh, in, STEREO16, 48'000);

    SoundCard again;
    again.set_cpu_hz(HZ);
    eq(again, band(0, 24), true);
    play(again, in, STEREO16, 48'000);
    again.write(SoundCard::PCMCTL, 0);
    const bool rerun = play(again, in, STEREO16, 48'000) == want;

    SoundCard seek;
    seek.set_cpu_hz(HZ);
    eq(seek, band(0, 24), true);
    seek.set_tap(true);
    start(seek, STEREO16, 48'000);
    put(seek, in);
    ticks(seek, HZ / 16, 7); // 3,000 frames in
    seek.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_FLUSH);
    seek.take_tap();
    put(seek, in);
    seek.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    ticks(seek, HZ / 4, 7);
    const bool sought = seek.take_tap() == want;
    std::printf("  RUN again: %s; a seek: %s\n", rerun ? "the same" : "different", sought ? "the same" : "different");
    CHECK(rerun);
    CHECK(sought);
}

TEST(an_mp3_through_the_eq_is_buseq_on_minimp3s_frames)
{
    // A 48 kHz MP3 then a 44.1 kHz one, back to back, the EQ on: the tap is BusEq run directly over minimp3's
    // frames, with the coefficients for each frame's rate as it changes, bit for bit.
    std::vector<uint8_t> bytes = mp3_file("cbr320_stereo_48k.mp3");
    const std::vector<uint8_t> b2 = mp3_file("cbr128_joint_44k.mp3");
    bytes.insert(bytes.end(), b2.begin(), b2.end());
    std::array<int8_t, 11> gains{};
    for (size_t i = 0; i < gains.size(); ++i)
        gains[i] = static_cast<int8_t>(i % 2 ? 20 : -14);
    std::vector<int16_t> want;
    {
        BusEq ref;
        int rate = 0;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        std::set<int> rates;
        for (size_t at = 0; at < bytes.size();)
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), bytes.data() + at, static_cast<int>(bytes.size() - at),
                                              pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            at += static_cast<size_t>(info.frame_bytes);
            if (!info.hz || !n)
                continue;
            if (info.hz != rate)
                ref.set(gains, true, static_cast<uint32_t>(rate = info.hz));
            rates.insert(info.hz);
            for (int i = 0; i < n; ++i)
            {
                int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                int16_t r = info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l;
                ref.run(l, r);
                want.insert(want.end(), {l, r});
            }
        }
        CHECK_EQ(rates.size(), size_t{2});
    }
    SoundCard c;
    c.set_cpu_hz(HZ);
    eq(c, gains, true);
    c.set_tap(true);
    c.write(SoundCard::CODEC, 1);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    for (const uint8_t b : bytes)
        c.write(SoundCard::PCMDATA, b);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    for (unsigned ms = 0; ms < 3'000 && c.peek(SoundCard::STATE) != SoundCard::STATE_ENDED; ++ms)
        ticks(c, HZ / 1'000, 7);
    const std::vector<int16_t> got = c.take_tap();
    std::printf("  48 kHz then 44.1 kHz: %zu frames, the card %s\n", want.size() / 2,
                got == want ? "the same" : "different");
    CHECK(!want.empty());
    CHECK(got == want);
}

TEST(a_new_host_clock_rescales_the_sids_clock_owed)
{
    // The SIDs' clock runs on the host's, the part of a SID clock owed carried from tick to tick. Ticked 73,077
    // cycles at 7.16 MHz, then the clock made 1 MHz, then one cycle: the part owed comes over rescaled, so one
    // cycle more pays out a SID clock or two, not the eight the old clock's remainder would make. And the other
    // way, 1 MHz then 7.16 MHz, ticked to almost a whole SID clock owed (10,236 cycles: 0.9985 of one), so one
    // cycle more pays it, where the old clock's remainder would pay none. Each against the sum worked here: the
    // remainder x new / old, then one cycle.
    SoundCard probe;
    const uint64_t at0 = probe.sid_clocks();
    probe.tick(SoundCard::NATIVE_HZ);
    const uint64_t sid = probe.sid_clocks() - at0; // SID clocks a second
    for (const auto &[from, to, cycles] :
         {std::tuple<uint32_t, uint32_t, uint64_t>{SoundCard::NATIVE_HZ, 1'000'000, 73'077},
          std::tuple<uint32_t, uint32_t, uint64_t>{1'000'000, SoundCard::NATIVE_HZ, 10'236}})
    {
        SoundCard c;
        c.set_cpu_hz(from);
        const uint64_t start = c.sid_clocks();
        c.tick(cycles);
        const uint64_t before = c.sid_clocks();
        c.set_cpu_hz(to);
        c.tick(1);
        const uint64_t step = c.sid_clocks() - before;
        const uint64_t owed = cycles * sid % from * to / from; // the remainder, at the new clock
        const uint64_t want = (owed + sid) / to;
        std::printf("  %u Hz to %u Hz after %llu cycles (%llu SID clocks): one cycle more, %llu SID clocks "
                    "(want %llu)\n",
                    from, to, static_cast<unsigned long long>(cycles), static_cast<unsigned long long>(before - start),
                    static_cast<unsigned long long>(step), static_cast<unsigned long long>(want));
        CHECK_EQ(before - start, cycles * sid / from);
        CHECK_EQ(step, want);
    }
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
            SoundCard c;
            c.set_cpu_hz(HZ);
            eq(c, band(e.band, 24), true);
            const bool same = play(c, in, STEREO16, rate) == in;
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
    // VU near -6 dBFS; the samples the tap's last 64, mono, high bytes. The page as the last frame plays. Bin 32
    // (Nyquist, band 31) is real: a cosine there.
    for (const uint32_t rate : {48'000u, 22'050u, 8'000u})
        for (const unsigned k : {1u, 4u, 9u, 16u, 31u, 32u})
        {
            SoundCard c;
            c.set_cpu_hz(HZ);
            const std::vector<int16_t> in =
                sine(4'000, static_cast<double>(k) * rate / 64, 0.5, rate, k == 32 ? PI / 2 : 0);
            std::vector<uint8_t> p;
            const std::vector<int16_t> tap = play(c, in, MONO16, rate, 7, &p);
            CHECK_EQ(p.size(), size_t{100});
            p.resize(100);
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
                const size_t f = tap.size() / 2 - 64 + i;
                const int mono = (tap[2 * f] + tap[2 * f + 1]) >> 1;
                CHECK_EQ(p[34 + i], static_cast<uint8_t>((mono + 32'768) >> 8));
            }
            CHECK(p[98] == 0 && p[99] == 0); // past the page
            // Since that snapshot nothing has played: the VU starts again at silence; and the stream has ended, so
            // the page is silence.
            const std::vector<uint8_t> again = page(c);
            CHECK(again[32] == 0 && again[33] == 0);
            CHECK(std::all_of(again.begin(), again.begin() + 34, [](uint8_t b) { return b == 0; }));
            CHECK(std::all_of(again.begin() + 34, again.end(), [](uint8_t b) { return b == 0x80; }));
        }
}

TEST(the_vis_page_follows_the_eq_and_not_the_volume)
{
    // The tap is after the EQ and before volume: a band cut at 12 kHz takes a 12 kHz sine's band down 12 dB in
    // the page; VOLL and VOLR at 0 leave the page as it was.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> in = sine(8'000, 12'000, 0.5, RATE); // bin 16
    std::vector<std::vector<uint8_t>> pages;
    for (const int mode : {0, 1, 2})
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        if (mode == 1)
            eq(c, band(7, -24), true);
        if (mode == 2)
        {
            c.write(SoundCard::VOLL, 0);
            c.write(SoundCard::VOLR, 0);
        }
        std::vector<uint8_t> p;
        play(c, in, MONO16, RATE, 7, &p);
        p.resize(SoundCard::VIS_BYTES);
        pages.push_back(p);
    }
    std::printf("  band 15: flat %u, the 12 kHz band cut 12 dB %u, volume 0 %u\n", pages[0][15], pages[1][15],
                pages[2][15]);
    CHECK(std::abs(static_cast<int>(pages[0][15]) - static_cast<int>(pages[1][15]) - 51) <= 4); // 12 dB: 51 steps
    CHECK(pages[2] == pages[0]);
}

TEST(volume_0_is_silent_and_between_is_rounded)
{
    const std::vector<int16_t> in = noise(6'000, 11);
    for (const auto &[l, r] : {std::pair<uint8_t, uint8_t>{0, 0}, {128, 255}, {255, 1}, {200, 77}})
    {
        SoundCard c;
        c.set_cpu_hz(HZ);
        c.write(SoundCard::VOLL, l);
        c.write(SoundCard::VOLR, r);
        CHECK_EQ(c.read(SoundCard::VOLL), l);
        CHECK_EQ(c.read(SoundCard::VOLR), r);
        std::vector<int16_t> want = in;
        for (size_t i = 0; i < want.size(); ++i)
        {
            const int v = i % 2 ? r : l, q = want[i] * v;
            want[i] = static_cast<int16_t>(q >= 0 ? (q + 127) / 255 : -((-q + 127) / 255));
        }
        const std::vector<int16_t> got = play(c, in, STEREO16, 48'000);
        std::printf("  VOLL %u VOLR %u: %s\n", l, r, got == want ? "as rounded" : "different");
        CHECK(got == want);
    }
    // At 0, the mix too: the stream adds nothing to the SIDs' silence.
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.set_audio_rate(48'000);
    c.write(SoundCard::VOLL, 0);
    c.write(SoundCard::VOLR, 0);
    start(c, STEREO16, 48'000);
    put(c, in);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    float peak = 0, buf[2 * 512];
    for (int t = 0; t < 100; ++t)
    {
        for (int i = 0; i < 100; ++i)
            c.tick(HZ / 10'000);
        for (size_t n; (n = c.take_audio(buf, 512)) != 0;)
            for (size_t i = 0; i < 2 * n; ++i)
                peak = std::max(peak, std::fabs(buf[i]));
    }
    std::printf("  volume 0 in the mix: peak %g\n", static_cast<double>(peak));
    CHECK(peak < 1e-3f); // the SIDs idle: their DC filtered away, nothing else
}

TEST(the_sids_volume_is_scaled_and_reads_back_as_written)
{
    // $D418 reaches each chip with its low nibble round(nibble x vol / 255): SID A by VOLL, SID B by VOLR. The
    // card's registers read back what was written; a volume change reaches the chips at once.
    SoundCard c;
    const unsigned A = SoundCard::SID_A + SoundCard::SID_MODE_VOL, B = SoundCard::SID_B + SoundCard::SID_MODE_VOL;
    c.write(A, 0x1F);
    c.write(B, 0x2A);
    CHECK_EQ(c.chip(0).regs()[SoundCard::SID_MODE_VOL], 0x1F); // at 255, as written
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x2A);
    c.write(SoundCard::VOLL, 128); // 15 x 128 / 255 = 7.53: 8
    CHECK_EQ(c.chip(0).regs()[SoundCard::SID_MODE_VOL], 0x18);
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x2A);
    c.write(SoundCard::VOLR, 0);
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x20);
    CHECK_EQ(c.read(A), 0x1F);
    CHECK_EQ(c.read(B), 0x2A);
    CHECK_EQ(c.sid(0)[SoundCard::SID_MODE_VOL], 0x1F);
    CHECK_EQ(c.sid(1)[SoundCard::SID_MODE_VOL], 0x2A);
    c.write(B, 0x4F); // a write while the volume is 0: scaled on its way in
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x40);
    CHECK_EQ(c.read(B), 0x4F);
    // Every nibble and volume rounds as the spec says.
    bool all = true;
    for (unsigned v : {1u, 17u, 34u, 127u, 128u, 200u, 254u})
    {
        c.write(SoundCard::VOLL, static_cast<uint8_t>(v));
        for (unsigned n = 0; n < 16; ++n)
        {
            c.write(A, static_cast<uint8_t>(0x30 | n));
            const unsigned want = static_cast<unsigned>(std::floor(n * v / 255.0 + 0.5));
            all = all && c.chip(0).regs()[SoundCard::SID_MODE_VOL] == (0x30 | want);
        }
    }
    CHECK(all);
    // CTRL BOTH: a write to A reaches B too, by B's own volume.
    c.write(SoundCard::VOLL, 255);
    c.write(SoundCard::VOLR, 128);
    c.write(SoundCard::CTRL, SoundCard::CTRL_BOTH);
    c.write(A, 0x0F);
    CHECK_EQ(c.chip(0).regs()[SoundCard::SID_MODE_VOL], 0x0F);
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x08);
    CHECK_EQ(c.read(B), 0x0F);
    // Back to 255: as written again, at once. /RESET: the volumes 255 and the chips cleared.
    c.write(SoundCard::VOLR, 255);
    CHECK_EQ(c.chip(1).regs()[SoundCard::SID_MODE_VOL], 0x0F);
    c.write(SoundCard::VOLL, 9);
    c.reset();
    CHECK_EQ(c.read(SoundCard::VOLL), 255);
    CHECK_EQ(c.chip(0).regs()[SoundCard::SID_MODE_VOL], 0);
    c.write(A, 0x0F);
    CHECK_EQ(c.chip(0).regs()[SoundCard::SID_MODE_VOL], 0x0F);
}

TEST(the_sids_sound_follows_the_volume)
{
    // A SID voice held on, its level made at VOLL 255 and 85 (15 x 85 / 255 = 5: a third): the second a third of
    // the first, as the chip's volume nibble makes it.
    double level[2]{};
    for (const uint8_t vol : {uint8_t{255}, uint8_t{85}})
    {
        SoundCard c;
        c.set_audio_rate(48'000);
        c.write(SoundCard::VOLL, vol);
        c.write(SoundCard::SID_A + 0x18, 0x0F);
        c.write(SoundCard::SID_A + 0x01, 0x20); // voice 1: a pulse at a few hundred Hz, full sustain
        c.write(SoundCard::SID_A + 0x03, 0x08);
        c.write(SoundCard::SID_A + 0x06, 0xF0);
        c.write(SoundCard::SID_A + 0x04, 0x41);
        float buf[2 * 512];
        double sum = 0;
        size_t n = 0;
        for (int t = 0; t < 50; ++t)
        {
            for (int i = 0; i < 100; ++i)
                c.tick(716); // 7.16 MHz's 1/10,000 s
            for (size_t got; (got = c.take_audio(buf, 512)) != 0;)
                for (size_t i = 0; i < got; ++i, ++n)
                    if (t >= 25)
                        sum += static_cast<double>(buf[2 * i]) * buf[2 * i];
        }
        level[vol == 255 ? 0 : 1] = std::sqrt(sum);
    }
    std::printf("  SID A at VOLL 85 against 255: %.4f (want 1/3)\n", level[1] / level[0]);
    CHECK(std::fabs(level[1] / level[0] - 1.0 / 3.0) < 0.02);
}

TEST(the_eq_and_the_vis_page_are_the_same_whatever_the_audio_rate_or_ticks)
{
    // The EQ on, every band and the preamp, an 8 kHz stereo stream of noise and a sine for 4 s (as much as the FIFO
    // holds), the tap off: the vis page at 1 s, 2.5 s and 5.5 s (after the end: silence), with the audio rate 0,
    // 48 kHz and 44.1 kHz, ticked in 7-cycle steps, by milliseconds, and in one tick each (over two seconds: the
    // mix's bulk catch-up). Then the golden hash of the tap and the pages: this build's and WebAssembly's alike.
    constexpr uint32_t RATE = 8'000;
    std::array<int8_t, 11> gains;
    for (size_t i = 0; i < gains.size(); ++i)
        gains[i] = static_cast<int8_t>(i % 3 == 0 ? 18 : i % 3 == 1 ? -24 : 7);
    gains[0] = -6;
    std::vector<int16_t> in = noise(4 * RATE, 3);
    const std::vector<int16_t> tone = sine(4 * RATE, 1'000, 0.4, RATE);
    for (size_t f = 0; f < tone.size(); ++f)
        in[2 * f] = static_cast<int16_t>(in[2 * f] / 4 + tone[f]);
    const uint64_t marks[] = {HZ, HZ * 5 / 2, HZ * 11 / 2};
    std::vector<uint8_t> first;
    bool same = true;
    for (const uint32_t audio : {0u, 48'000u, 44'100u})
        for (const uint64_t chunk : {uint64_t{7}, uint64_t{HZ / 1'000}, uint64_t{0}})
        {
            SoundCard c;
            c.set_cpu_hz(HZ);
            c.set_audio_rate(audio);
            eq(c, gains, true);
            start(c, STEREO16, RATE);
            put(c, in);
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            std::vector<uint8_t> pages;
            uint64_t t = 0;
            for (const uint64_t m : marks)
            {
                if (chunk == 0)
                    c.tick(m - t);
                else
                    for (uint64_t u = t; u < m; u += chunk)
                        c.tick(std::min(chunk, m - u));
                t = m;
                const std::vector<uint8_t> p = page(c);
                pages.insert(pages.end(), p.begin(), p.end());
                float buf[2 * 4'096];
                while (c.take_audio(buf, 4'096))
                {
                }
            }
            if (first.empty())
                first = pages;
            std::printf("  audio %5u Hz, %s: %s\n", audio,
                        chunk == 7 ? "7-cycle ticks" : chunk ? "1 ms ticks" : "one tick to each snapshot",
                        pages == first ? "the same pages" : "different");
            same = same && pages == first;
        }
    CHECK(same);
    CHECK(std::any_of(first.begin(), first.begin() + 32, [](uint8_t b) { return b > 100; })); // not silence
    CHECK(std::all_of(first.begin() + 2 * 98, first.begin() + 2 * 98 + 32, [](uint8_t b) { return b == 0; }));
    CHECK(std::all_of(first.begin() + 2 * 98 + 34, first.end(), [](uint8_t b) { return b == 0x80; }));
    CHECK(first[2 * 98 + 32] > 100); // the VU: the peak since the snapshot before, while it still played

    // The golden hash: the tap (EQ on, VOLL 200) and the three pages.
    SoundCard c;
    c.set_cpu_hz(HZ);
    eq(c, gains, true);
    c.write(SoundCard::VOLL, 200);
    const std::vector<int16_t> tap = play(c, in, STEREO16, RATE);
    uint64_t h = 0xCBF29CE484222325ull;
    for (const int16_t s : tap)
    {
        h = fnv(h, static_cast<uint8_t>(s));
        h = fnv(h, static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
    }
    for (const uint8_t b : first)
        h = fnv(h, b);
    std::printf("  golden: %016llx\n", static_cast<unsigned long long>(h));
    CHECK_EQ(h, 0x0BE0AFA2FD43DEFCull);
}

TEST(the_eq_rings_down_into_an_underrun_the_same_whatever_the_ticks)
{
    // A tone with the EQ boosting it, then the stream dry (no EOF): the bus's silence goes through the EQ, which
    // rings on for a while. The page 2 ms and 1.5 s into the dry spell, at audio rates 0 and 48 kHz and ticked
    // finely or in one tick: the same. The first still has the ring in its samples; the second is silence.
    constexpr uint32_t RATE = 48'000;
    const std::vector<int16_t> in = sine(RATE / 2, 60, 0.3, RATE);
    std::vector<uint8_t> first;
    bool same = true;
    for (const uint32_t audio : {0u, 48'000u})
        for (const uint64_t chunk : {uint64_t{7}, uint64_t{0}})
        {
            SoundCard c;
            c.set_cpu_hz(HZ);
            c.set_audio_rate(audio);
            eq(c, band(0, 24), true);
            start(c, MONO16, RATE);
            put(c, in); // 48 KB: playing from 4 KB on
            std::vector<uint8_t> pages;
            uint64_t t = 0;
            for (const uint64_t m : {HZ / 2 + HZ / 500, 2 * HZ})
            {
                if (chunk == 0)
                    c.tick(m - t);
                else
                    for (uint64_t u = t; u < m; u += chunk)
                        c.tick(std::min(chunk, m - u));
                t = m;
                const std::vector<uint8_t> p = page(c);
                pages.insert(pages.end(), p.begin(), p.end());
                float buf[2 * 4'096];
                while (c.take_audio(buf, 4'096))
                {
                }
            }
            CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_UNDERRUN);
            if (first.empty())
                first = pages;
            same = same && pages == first;
        }
    const auto ringing = std::any_of(first.begin() + 34, first.begin() + 98, [](uint8_t b) { return b != 0x80; });
    const auto quiet = std::all_of(first.begin() + 98 + 34, first.end(), [](uint8_t b) { return b == 0x80; });
    std::printf("  2 ms dry: %s; 1.5 s dry: %s; the same every way: %s\n", ringing ? "ringing" : "silent",
                quiet ? "silent" : "ringing", same ? "yes" : "no");
    CHECK(same);
    CHECK(ringing);
    CHECK(quiet);
}

int main() { return run_all(); }
