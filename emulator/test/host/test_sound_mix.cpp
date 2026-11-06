/**
 * @file test_sound_mix.cpp
 * @brief Sound card revision 2's mix (hw/sound-rev2.md, step 3): the digital bus resampled into the SIDs' sound.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * What take_audio() gives with a PCM stream playing: a sine at the level
 * MIX_DIGITAL sets and at its own frequency, measured from the samples (a
 * least-squares fit of the known frequency for the level, zero crossings for
 * the frequency); the SIDs and the stream summed, neither lost; the stream on
 * time in the mix, after a start and after a pause; with the bus stopped, the
 * SIDs' samples exactly as without it; the resampler's frame counts over ten
 * seconds, for four stream rates into two audio rates, with no drift; the
 * backlog bounded with nobody collecting, an hour's tick in bulk, and nothing
 * made at audio rate 0.
 */

#include "harness.h"

#include "sound_card.h"

#include <cmath>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t SECOND = SoundCard::NATIVE_HZ;
    constexpr double PI = 3.14159265358979323846;

    void start(SoundCard &c, uint8_t fmt, uint16_t rate)
    {
        c.write(SoundCard::RATE, static_cast<uint8_t>(rate));
        c.write(SoundCard::RATE + 1, static_cast<uint8_t>(rate >> 8));
        c.write(SoundCard::PCMFMT, fmt);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    }

    void put16(SoundCard &c, int16_t v)
    {
        c.write(SoundCard::PCMDATA, static_cast<uint8_t>(v));
        c.write(SoundCard::PCMDATA, static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    }

    /// `ms` milliseconds of the card's time in 1 ms steps, the FIFO topped up from `frame` (stereo 16-bit, the
    /// frame number in, left and right out) before each, and every sample made collected into `out`.
    void play(SoundCard &c, unsigned ms, std::vector<float> &out,
              const std::function<std::pair<int16_t, int16_t>(uint64_t)> &frame, uint64_t &next)
    {
        std::vector<float> buf(2 * 4'096);
        for (unsigned i = 0; i < ms; ++i)
        {
            while (frame && c.fifo_bytes() + 4 <= SoundCard::FIFO_BYTES && c.fifo_bytes() < 16'384)
            {
                const auto [l, r] = frame(next++);
                put16(c, l);
                put16(c, r);
            }
            for (uint64_t t = SECOND / 1'000; t; t -= std::min<uint64_t>(t, 7))
                c.tick(std::min<uint64_t>(t, 7));
            const size_t n = c.take_audio(buf.data(), 4'096);
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(2 * n));
        }
    }

    /// One side of interleaved stereo, from frame `from` on.
    std::vector<double> side(const std::vector<float> &s, unsigned ch, size_t from)
    {
        std::vector<double> out;
        for (size_t i = 2 * from + ch; i < s.size(); i += 2)
            out.push_back(s[i]);
        return out;
    }

    /// A least-squares fit of a sine at `hz`, plus a constant: the amplitude, and the RMS of what is left.
    struct Fit
    {
        double amplitude, residual;
    };
    Fit fit(const std::vector<double> &x, double hz, double rate)
    {
        double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0, sum = 0;
        const size_t n = x.size();
        for (size_t i = 0; i < n; ++i)
        {
            const double s = std::sin(2 * PI * hz * static_cast<double>(i) / rate),
                         c = std::cos(2 * PI * hz * static_cast<double>(i) / rate);
            ss += s * s, cc += c * c, sc += s * c, xs += x[i] * s, xc += x[i] * c, sum += x[i];
        }
        const double mean = sum / static_cast<double>(n), det = ss * cc - sc * sc;
        const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
        double r = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double t = 2 * PI * hz * static_cast<double>(i) / rate;
            const double e = x[i] - mean - a * std::sin(t) - b * std::cos(t);
            r += e * e;
        }
        return {std::hypot(a, b), std::sqrt(r / static_cast<double>(n))};
    }

    /// The frequency from the upward zero crossings, each placed between its two samples.
    double frequency(const std::vector<double> &x, double rate)
    {
        double first = -1, last = -1;
        unsigned crossings = 0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            if (x[i - 1] < 0 && x[i] >= 0)
            {
                const double at = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0)
                    first = at;
                last = at;
                ++crossings;
            }
        }
        return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
    }

    std::pair<int16_t, int16_t> sines(uint64_t n, double rate)
    {
        const double t = static_cast<double>(n) / rate;
        return {static_cast<int16_t>(std::lround(16'384 * std::sin(2 * PI * 1'000 * t))),
                static_cast<int16_t>(std::lround(8'192 * std::sin(2 * PI * 440 * t)))};
    }
} // namespace

TEST(a_sine_stream_arrives_at_its_level_and_frequency)
{
    // 1 kHz at half of full scale on the left, 440 Hz at a quarter on the right. In the mix they are scaled by
    // MIX_DIGITAL (0.5): amplitudes 0.25 and 0.125, less linear interpolation's own loss: at most sinc^2(f / stream
    // rate), none when every output falls on an input frame (equal rates).
    for (const uint32_t rate : {48'000u, 44'100u})
        for (const uint16_t stream : {uint16_t{48'000}, uint16_t{44'100}, uint16_t{22'050}})
        {
            SoundCard c;
            c.set_audio_rate(rate);
            start(c, SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT, stream);
            std::vector<float> out;
            uint64_t next = 0;
            play(c, 1'100, out, [&](uint64_t n) { return sines(n, stream); }, next);
            const std::vector<double> l = side(out, 0, rate / 10), r = side(out, 1, rate / 10); // after 100 ms
            const Fit fl = fit(l, 1'000, rate), fr = fit(r, 440, rate);
            const double hl = frequency(l, rate), hr = frequency(r, rate);
            std::printf("  %u from %u: left %.5f at %.3f Hz (residual %.6f), right %.5f at %.3f Hz (residual %.6f)\n",
                        rate, stream, fl.amplitude, hl, fl.residual, fr.amplitude, hr, fr.residual);
            const auto lerp = [&](double hz) {
                if (stream == rate)
                    return 1.0;
                const double x = PI * hz / stream;
                return std::pow(std::sin(x) / x, 2);
            };
            // Between sinc^2's loss (outputs spread evenly between input frames) and none (on them).
            CHECK(fl.amplitude <= 0.25 * 1.0005 && fl.amplitude >= 0.25 * lerp(1'000) * 0.9995);
            CHECK(fr.amplitude <= 0.125 * 1.0005 && fr.amplitude >= 0.125 * lerp(440) * 0.9995);
            CHECK(std::fabs(hl - 1'000) < 0.5);
            CHECK(std::fabs(hr - 440) < 0.5);
            CHECK(fl.residual < 0.002); // interpolated: nearest-neighbour leaves about 0.01
            CHECK(fr.residual < 0.002);
        }
}

namespace
{
    /// SID A's voice 1, a pulse near 300 Hz at full volume, written from the host.
    void sid_voice(SoundCard &c)
    {
        c.write(SoundCard::SID_A + 0x00, 0x00); // frequency $13F0: about 300 Hz at PAL
        c.write(SoundCard::SID_A + 0x01, 0x14);
        c.write(SoundCard::SID_A + 0x03, 0x08); // pulse width 50%
        c.write(SoundCard::SID_A + 0x06, 0xF0); // sustain 15
        c.write(SoundCard::SID_A + 0x18, 0x0F); // volume 15
        c.write(SoundCard::SID_A + 0x04, 0x41); // pulse, gate
    }
} // namespace

TEST(a_stream_and_a_sid_voice_sum_without_either_lost)
{
    // Three cards on the same clock: the voice alone, the stream alone, and both. Both is the sum of the other two
    // (less the silent card's own output, which is in each), clipped; and each part is there at its own level.
    const uint32_t rate = 48'000;
    SoundCard none, voice, stream, both;
    std::vector<float> out[4];
    uint64_t next[4] = {};
    SoundCard *cards[4] = {&none, &voice, &stream, &both};
    for (SoundCard *c : cards)
        c->set_audio_rate(rate);
    sid_voice(voice);
    sid_voice(both);
    start(stream, SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT, 48'000);
    start(both, SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT, 48'000);
    const auto gen = [](uint64_t n) { return sines(n, 48'000); };
    play(none, 600, out[0], nullptr, next[0]);
    play(voice, 600, out[1], nullptr, next[1]);
    play(stream, 600, out[2], gen, next[2]);
    play(both, 600, out[3], gen, next[3]);
    REQUIRE(out[3].size() == out[0].size() && out[3].size() == out[1].size() && out[3].size() == out[2].size());
    double worst = 0;
    for (size_t i = 0; i < out[3].size(); ++i)
    {
        const double want = std::fmax(-1.0, std::fmin(1.0, double{out[1][i]} + out[2][i] - out[0][i]));
        worst = std::fmax(worst, std::fabs(out[3][i] - want));
    }
    std::printf("  both against voice + stream: at most %.2g apart\n", worst);
    CHECK(worst < 1e-5);
    const std::vector<double> l = side(out[3], 0, rate / 10), v = side(out[1], 0, rate / 10);
    const Fit sine = fit(l, 1'000, rate);
    CHECK(std::fabs(sine.amplitude - 0.25) < 0.25 * 0.005); // the stream, whole
    double voice_energy = 0, mixed_energy = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        voice_energy += v[i] * v[i];
        mixed_energy += l[i] * l[i];
    }
    // The voice is there too: the mix carries the stream's energy plus the voice's (they're uncorrelated).
    const double sine_energy = 0.25 * 0.25 / 2 * static_cast<double>(v.size());
    std::printf("  energy: voice %.2f, stream %.2f, both %.2f\n", voice_energy, sine_energy, mixed_energy);
    CHECK(voice_energy > 0.001 * static_cast<double>(v.size()));
    CHECK(std::fabs(mixed_energy - (voice_energy + sine_energy)) < 0.02 * (voice_energy + sine_energy));
}

namespace
{
    /// The first frame from `from` whose left side passes `level`.
    size_t first_over(const std::vector<float> &s, double level, size_t from = 0)
    {
        for (size_t i = from; 2 * i < s.size(); ++i)
            if (std::fabs(s[2 * i]) > level)
                return i;
        return SIZE_MAX;
    }

    void run(SoundCard &c, uint64_t cycles)
    {
        for (; cycles >= 7; cycles -= 7)
            c.tick(7);
        if (cycles)
            c.tick(cycles);
    }

    std::vector<float> take_all(SoundCard &c)
    {
        std::vector<float> out(2 * 48'000);
        out.resize(2 * c.take_audio(out.data(), 48'000));
        return out;
    }
} // namespace

TEST(the_stream_is_on_time_in_the_mix_after_a_start_and_a_pause)
{
    // Nobody collects for 50 ms, so the SIDs' samples for it aren't made yet when the stream starts; the stream
    // must still land 50 ms in, not at the first sample waiting. A DC level makes its start plain to see. With
    // the rates equal, each bus frame makes one output, on it: after the SIDs' samples made by the start come the
    // lead (two silent frames, rate / stream + 1), then the interpolation's first output (the silence before
    // the stream), then the stream.
    const uint32_t rate = 48'000;
    const size_t lead = 2;
    SoundCard c;
    c.set_audio_rate(rate);
    run(c, SECOND / 20);
    start(c, SoundCard::PCMFMT_16BIT, 48'000);
    for (unsigned i = 0; i < 4'800; ++i)
        put16(c, 16'384); // 0.5: 0.25 in the mix; 100 ms of it
    const size_t made = c.audio_frames(); // the SIDs' samples up to the start (nothing taken yet)
    run(c, SECOND / 20);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
    run(c, SECOND / 20);    // 50 ms paused
    const size_t made_again = c.audio_frames();
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    run(c, SECOND / 20);
    const std::vector<float> out = take_all(c);
    const size_t on = first_over(out, 0.1);
    std::printf("  the stream starts at sample %zu (2,400 is 50 ms; the SIDs had made %zu)\n", on, made);
    CHECK(made >= 2'399 && made <= 2'400);
    CHECK_EQ(on, made + lead + 1);
    // Paused at 100 ms: silence from about 4,800 to 7,200, then the stream again.
    bool quiet = true;
    for (size_t i = 4'805; i < 7'195; ++i)
        quiet = quiet && std::fabs(out[2 * i]) < 0.01;
    CHECK(quiet);
    const size_t again = first_over(out, 0.1, 4'805);
    std::printf("  and again at %zu after the pause (7,200 is 150 ms; the SIDs had made %zu)\n", again, made_again);
    CHECK(made_again >= 7'199 && made_again <= 7'200);
    CHECK_EQ(again, made_again + lead + 1);
}

TEST(with_the_bus_stopped_the_sids_samples_are_as_without_it)
{
    // A voice on every card. One card never touches the stream; one buffers and pauses it (no bus); one plays a
    // stream to its end. Bit for bit, the first two agree throughout, and the third once its stream is over.
    const uint32_t rate = 44'100;
    SoundCard plain, idle, ended;
    for (SoundCard *c : {&plain, &idle, &ended})
    {
        c->set_audio_rate(rate);
        sid_voice(*c);
    }
    start(idle, SoundCard::PCMFMT_16BIT, 48'000);
    for (unsigned i = 0; i < 1'000; ++i)
        put16(idle, 20'000);                       // under 4 KB: buffering
    start(ended, SoundCard::PCMFMT_16BIT, 48'000);
    for (unsigned i = 0; i < 2'400; ++i)
        put16(ended, 20'000);                      // 50 ms
    ended.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    std::vector<float> out[3];
    for (unsigned ms = 0; ms < 400; ++ms)
    {
        if (ms == 100)
            idle.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
        unsigned k = 0;
        for (SoundCard *c : {&plain, &idle, &ended})
        {
            run(*c, SECOND / 1'000);
            const std::vector<float> got = take_all(*c);
            out[k].insert(out[k].end(), got.begin(), got.end());
            ++k;
        }
    }
    REQUIRE(out[0].size() == out[1].size() && out[0].size() == out[2].size());
    CHECK(out[1] == out[0]);
    CHECK_EQ(ended.peek(SoundCard::STATE), SoundCard::STATE_ENDED);
    const size_t after = 2 * (rate / 10); // 100 ms in: the stream long over
    CHECK(std::vector<float>(out[2].begin() + static_cast<std::ptrdiff_t>(after), out[2].end()) ==
          std::vector<float>(out[0].begin() + static_cast<std::ptrdiff_t>(after), out[0].end()));
    CHECK(std::vector<float>(out[2].begin(), out[2].begin() + static_cast<std::ptrdiff_t>(after)) !=
          std::vector<float>(out[0].begin(), out[0].begin() + static_cast<std::ptrdiff_t>(after)));
}

TEST(the_resampler_makes_the_audio_rate_from_every_stream_rate_with_no_drift)
{
    for (const uint32_t rate : {44'100u, 48'000u})
        for (const uint16_t stream : {uint16_t{8'000}, uint16_t{22'050}, uint16_t{44'100}, uint16_t{48'000}})
        {
            SoundCard c;
            c.set_audio_rate(rate);
            start(c, SoundCard::PCMFMT_16BIT, stream);
            for (unsigned i = 0; i < 2'048; ++i)
                put16(c, 1'000);                    // 4 KB, then dry: the bus runs on, silent
            // At every check, the mix's frames are the audio rate's for the time gone, behind by a constant at most:
            // the first bus frame falls due a stream frame after the start, interpolating waits for the frame
            // after, and a stream frame's outputs come at once (six for 8 kHz into 48 kHz). With no drift, that
            // bound holds over ten seconds as over one.
            const int64_t burst = (rate + stream - 1) / stream, late = 3 * burst + 1;
            int64_t worst_off = 0, least_off = INT64_MAX;
            uint64_t cycles = 0;
            size_t most = 0;
            for (unsigned s = 1; s <= 10; ++s)
            {
                for (unsigned ms = 0; ms < 1'000; ++ms)
                {
                    run(c, SECOND / 1'000);
                    cycles += SECOND / 1'000;
                    take_all(c);
                    most = std::max(most, c.mix_backlog());
                }
                const int64_t want = static_cast<int64_t>(cycles * rate / SECOND);
                const int64_t off = want - static_cast<int64_t>(c.mix_frames());
                worst_off = std::max(worst_off, off);
                least_off = std::min(least_off, off);
            }
            std::printf("  %u from %u: %llu frames in 10 s; behind by %lld to %lld; %zu waiting at most\n", rate,
                        stream, static_cast<unsigned long long>(c.mix_frames()), static_cast<long long>(least_off),
                        static_cast<long long>(worst_off), most);
            CHECK(least_off >= -1 && worst_off <= late); // -1: the floor of the time gone, against a frame made on it
            CHECK(worst_off - least_off <= burst + 1); // the same, give or take a burst: no drift
            // The bus and the SIDs keep step: no backlog builds past one stream frame's outputs and a little.
            CHECK(most <= static_cast<size_t>(burst) + 2);
        }
}

TEST(nobody_collecting_keeps_the_mix_bounded_and_a_huge_tick_is_bulk)
{
    const uint32_t rate = 48'000;
    SoundCard c;
    c.set_audio_rate(rate);
    start(c, SoundCard::PCMFMT_16BIT, 48'000);
    for (unsigned i = 0; i < 2'048; ++i)
        put16(c, 1'000);
    size_t most = 0;
    for (unsigned ms = 0; ms < 10'000; ++ms) // 10 s, nothing taken, no SID touched
    {
        run(c, SECOND / 1'000);
        most = std::max(most, c.mix_backlog());
    }
    std::printf("  at most %zu frames of the bus waiting\n", most);
    CHECK(most <= rate * 9 / 4);              // two seconds and a quarter
    CHECK(c.audio_frames() <= rate / 2);      // the SIDs' half second, which brings the bus's down too
    CHECK(c.mix_backlog() <= 2);
    for (const uint64_t hours : {uint64_t{1}, uint64_t{24}}) // in one tick: the last second made, at most
    {
        // In bulk: a frame at a time, 24 hours of them took 5.6 s; in bulk it is microseconds. Two seconds is
        // room for a slow host or a sanitizer build, and far under the frame-at-a-time cost there.
        const auto t0 = std::chrono::steady_clock::now();
        c.tick(SECOND * 3'600 * hours);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %llu h in one tick: %.6f s\n", static_cast<unsigned long long>(hours), secs);
        CHECK(secs < 2.0);
        CHECK(c.mix_backlog() <= rate + 3);
        std::vector<float> out = take_all(c);
        CHECK(out.size() <= 2 * rate / 2);
        CHECK(c.mix_backlog() <= 4);
    }
}

TEST(nothing_is_mixed_at_audio_rate_0)
{
    SoundCard c;
    start(c, SoundCard::PCMFMT_16BIT, 48'000);
    for (unsigned i = 0; i < 4'000; ++i)
        put16(c, 1'000);
    run(c, SECOND);
    CHECK_EQ(c.mix_frames(), 0u);
    CHECK_EQ(c.mix_backlog(), 0u);
    CHECK_EQ(c.audio_frames(), 0u);
}

namespace
{
    // The golden program: SID A and B voices from the host, the filter, a 6510 program that sweeps a voice, a
    // gap with nobody collecting, at 44.1 and 48 kHz. FNV-1a over the bits of every float take_audio() gives.
    uint64_t golden_hash()
    {
        using ooe::emu::SoundCard;
        uint64_t h = 1469598103934665603ull;
        const auto eat = [&](const float *p, size_t n) {
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t b;
                std::memcpy(&b, &p[i], 4);
                for (int k = 0; k < 4; ++k)
                {
                    h ^= (b >> (8 * k)) & 0xFF;
                    h *= 1099511628211ull;
                }
            }
        };
        for (const uint32_t rate : {44'100u, 48'000u})
        {
            SoundCard c;
            c.set_audio_rate(rate);
            c.write(0x00, 0x25); c.write(0x01, 0x11); c.write(0x05, 0x09); c.write(0x06, 0xF0);
            c.write(0x18, 0x1F); c.write(0x17, 0x01); c.write(0x15, 0x05); c.write(0x16, 0x40); c.write(0x04, 0x41);
            c.write(0x20 + 0x18, 0x0F); c.write(0x20 + 0x01, 0x30); c.write(0x20 + 0x04, 0x21); c.write(0x20 + 0x06, 0xF0);
            // On the 6510: INC $D401 (SID A voice 1's frequency high) in a loop, slowly: LDX #0; DEX; BNE; INC; JMP.
            const uint8_t prog[] = {0xA2, 0x00, 0xCA, 0xD0, 0xFD, 0xEE, 0x01, 0xD4, 0x4C, 0x00, 0x10};
            for (size_t i = 0; i < sizeof prog; ++i)
                c.ram()[0x1000 + i] = prog[i];
            c.ram()[0xFFFC] = 0x00;
            c.ram()[0xFFFD] = 0x10;
            float buf[2 * 4096];
            for (int k = 0; k < 400; ++k)
            {
                if (k == 100)
                    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN | SoundCard::CTRL_RESET); // the 6510 takes over
                uint64_t n = SoundCard::NATIVE_HZ / 100;
                if (k == 250)
                    n += 3ull * SoundCard::NATIVE_HZ; // 3 s nobody collects
                for (; n >= 7; n -= 7)
                    c.tick(7);
                c.tick(n);
                eat(buf, 2 * c.take_audio(buf, 4096));
            }
        }
        return h;
    }
} // namespace

TEST(with_pcm_idle_the_sids_samples_are_main_s_bit_for_bit)
{
    // The hash of the same program built against main before the mix (bbe5f98), by a scratch program linking
    // main's own libraries. A change to the SIDs' samples with the stream idle changes it.
    const uint64_t h = golden_hash();
    std::printf("  %016llx\n", static_cast<unsigned long long>(h));
    CHECK(h == 0xf8b6874cebedd157ull);
}

TEST(collected_at_any_moments_the_mix_has_no_gap_and_no_shift)
{
    // The same stream twice: collected every millisecond, and at random moments from 100 to 30,000 cycles
    // apart. Bit for bit the same samples, with the stream unbroken from its start: the queue never runs dry
    // while the bus runs, so no sample goes without its frame and none takes a later one.
    for (const auto &[rate, stream] : {std::pair<uint32_t, uint16_t>{48'000, 8'000}, {48'000, 22'050},
                                       {44'100, 48'000}, {48'000, 44'100}})
    {
        std::vector<float> got[2];
        for (unsigned way = 0; way < 2; ++way)
        {
            SoundCard c;
            c.set_audio_rate(rate);
            start(c, SoundCard::PCMFMT_16BIT, stream);
            uint32_t seed = 7;
            uint64_t t = 0;
            std::vector<float> buf(2 * 48'000);
            while (t < 3 * SECOND)
            {
                while (c.fifo_bytes() + 2 <= 16'384)
                    put16(c, 16'384);
                seed = seed * 1'103'515'245u + 12'345u;
                const uint64_t n = way ? 100 + (seed >> 8) % 29'901 : SECOND / 1'000;
                run(c, n);
                t += n;
                const size_t k = c.take_audio(buf.data(), 48'000);
                got[way].insert(got[way].end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(2 * k));
            }
        }
        const size_t n = std::min(got[0].size(), got[1].size());
        const size_t on = first_over(got[1], 0.2);
        size_t low = 0;
        for (size_t i = on; 2 * i < n; ++i)
            low += got[1][2 * i] < 0.2f;
        std::printf("  %u from %u: the stream from sample %zu, %zu samples low after it, %s\n", rate, stream, on, low,
                    std::equal(got[0].begin(), got[0].begin() + static_cast<std::ptrdiff_t>(n), got[1].begin())
                        ? "the same both ways"
                        : "different");
        CHECK_EQ(low, 0u);
        CHECK(std::equal(got[0].begin(), got[0].begin() + static_cast<std::ptrdiff_t>(n), got[1].begin()));
    }
}

TEST(a_stream_over_before_a_gap_is_gone_after_it_not_late)
{
    // Ended, or paused, with its frames still queued for samples not made; then 3 s with nobody collecting. The
    // SIDs skip all but the last second of it, and the stream's frames go with the samples they were meant for.
    for (const bool pause : {false, true})
    {
        SoundCard c;
        c.set_audio_rate(48'000);
        start(c, 0, 48'000);
        for (unsigned i = 0; i < 43'200; ++i)
            c.write(SoundCard::PCMDATA, 0xC0); // 8-bit, +0.5: 0.25 in the mix
        if (!pause)
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        run(c, SECOND * 95 / 100);
        if (pause)
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
        CHECK(c.mix_backlog() > 40'000); // most of it waiting
        run(c, 3 * SECOND);
        const std::vector<float> out = take_all(c);
        size_t loud = 0;
        for (size_t i = 0; i < out.size(); i += 2)
            loud += out[i] > 0.1f;
        std::printf("  %s: %zu of %zu samples carry the stream\n", pause ? "paused" : "ended", loud, out.size() / 2);
        CHECK_EQ(loud, 0u);
    }
}

TEST(the_sum_past_full_scale_is_clipped)
{
    // Three resonant pulse voices on SID A, filtered, and a full-scale square stream: the sum passes 1, and comes
    // out clipped to -1..1. Without the stream the voices stay under 1, so the clip is the sum's doing.
    for (const Sid::Model model : {Sid::Model::Mos6581, Sid::Model::Mos8580})
        for (const bool pcm : {false, true})
        {
            SoundCard c;
            c.set_model(model);
            c.set_audio_rate(48'000);
            for (unsigned v = 0; v < 3; ++v)
            {
                const unsigned b = SoundCard::SID_A + 7 * v;
                c.write(b + 0, 0x00);
                c.write(b + 1, static_cast<uint8_t>(0x08 + v));
                c.write(b + 3, 0x08);
                c.write(b + 6, 0xF0);
                c.write(b + 4, 0x41);
            }
            c.write(SoundCard::SID_A + 0x15, 0x00);
            c.write(SoundCard::SID_A + 0x16, 0x10);
            c.write(SoundCard::SID_A + 0x17, 0xF7); // resonance 15, all three filtered
            c.write(SoundCard::SID_A + 0x18, 0x1F); // low-pass, volume 15
            if (pcm)
            {
                start(c, SoundCard::PCMFMT_16BIT, 48'000);
                for (unsigned i = 0; i < 20'000; ++i)
                    put16(c, (i / 40) % 2 ? int16_t{32'767} : int16_t{-32'768});
            }
            double peak = 0;
            size_t at_clip = 0, past = 0;
            std::vector<float> buf(2 * 4'096);
            for (unsigned ms = 0; ms < 400; ++ms)
            {
                run(c, SECOND / 1'000);
                const size_t n = c.take_audio(buf.data(), 4'096);
                for (size_t i = 0; i < 2 * n; ++i)
                {
                    peak = std::fmax(peak, std::fabs(buf[i]));
                    at_clip += std::fabs(buf[i]) == 1.0f;
                    past += std::fabs(buf[i]) > 1.0f;
                }
            }
            std::printf("  %s, %s: peak %.3f, %zu samples at the clip\n", model == Sid::Model::Mos6581 ? "6581" : "8580",
                        pcm ? "with the stream" : "the voices alone", peak, at_clip);
            CHECK_EQ(past, 0u);
            if (pcm)
                CHECK(at_clip > 100);
            else
                CHECK(peak < 0.95);
        }
}

int main()
{
    return run_all();
}
