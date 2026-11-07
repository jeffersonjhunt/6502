/**
 * @file sound_dsp.cpp
 * @brief Sound card revision 2's digital bus: the EQ and the vis page.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_dsp.h"

#include <algorithm>
#include <cmath>

namespace ooe::emu
{
    namespace
    {
        constexpr double PI = 3.14159265358979323846;
        constexpr double TWO_PI = 2 * PI;
        constexpr double LN2 = 0.69314718055994530942;
        constexpr double LN10 = 2.30258509299404568402;

        /// sin of x within [-pi, pi]: its Taylor series, a fixed number of terms (enough at pi).
        double sin_reduced(double x)
        {
            const double x2 = x * x;
            double term = x, sum = x;
            for (unsigned i = 1; i <= 30; ++i)
            {
                term = term * -x2 / static_cast<double>((2 * i) * (2 * i + 1));
                sum += term;
            }
            return sum;
        }

        /// x less the nearest multiple of 2 pi.
        double reduce(double x) { return x - std::floor(x / TWO_PI + 0.5) * TWO_PI; }

        /// The FFT's angles, and its window.
        struct Tables
        {
            std::array<double, BusVis::N> cos{}, sin{}, hann{};
            std::array<double, 255> level{}; ///< the power at which each step of 1 to 255 begins
            Tables()
            {
                for (size_t n = 0; n < BusVis::N; ++n)
                {
                    const double w = TWO_PI * static_cast<double>(n) / static_cast<double>(BusVis::N);
                    cos[n] = dsp::cos(w);
                    sin[n] = dsp::sin(w);
                    hann[n] = 0.5 - 0.5 * cos[n];
                }
                // Step b covers -60 + (b - 0.5) x 60 / 255 dBFS and up: the dB rounded to a step.
                for (unsigned b = 1; b <= 255; ++b)
                    level[b - 1] = dsp::pow10(((static_cast<double>(b) - 0.5) * 60.0 / 255.0 - 60.0) / 10.0);
            }
        };

        const Tables &tables()
        {
            static const Tables t;
            return t;
        }
    } // namespace

    namespace dsp
    {
        double sin(double x) { return sin_reduced(reduce(x)); }

        double cos(double x) { return sin_reduced(reduce(x + PI / 2)); }

        double exp(double x)
        {
            // x = k ln 2 + r, |r| <= ln 2 / 2: e^r by its series, then k doublings (exact).
            const double k = std::floor(x / LN2 + 0.5);
            const double r = x - k * LN2;
            double term = 1, sum = 1;
            for (unsigned i = 1; i <= 25; ++i)
            {
                term = term * r / static_cast<double>(i);
                sum += term;
            }
            return std::ldexp(sum, static_cast<int>(k));
        }

        double pow10(double x) { return exp(x * LN10); }

        uint8_t level(double power)
        {
            const auto &t = tables().level;
            return static_cast<uint8_t>(std::upper_bound(t.begin(), t.end(), power) - t.begin());
        }
    } // namespace dsp

    void BusEq::set(const std::array<int8_t, BANDS + 1> &gains, bool on, uint32_t rate)
    {
        pre_on_ = on && gains[0] != 0;
        pre_ = pre_on_ ? dsp::pow10(static_cast<double>(gains[0]) / 40.0) : 1.0; // half-dB: / 2 / 20
        count_ = 0;
        for (unsigned b = 0; b < BANDS; ++b)
        {
            Biquad &q = band_[b];
            const int g = gains[b + 1];
            if (!on || g == 0 || !rate || !band_runs(b, rate))
            {
                q.s = {}; // when it runs again, from silence
                continue;
            }
            // The cookbook's peaking EQ: A = 10^(dB / 40), w0 = 2 pi f0 / rate, alpha = sin(w0) / 2Q.
            const double a = dsp::pow10(static_cast<double>(g) / 80.0);
            const double w0 = TWO_PI * static_cast<double>(CENTRE_HZ[b]) / static_cast<double>(rate);
            const double alpha = dsp::sin(w0) / (2.0 * static_cast<double>(Q_100) / 100.0);
            const double c = dsp::cos(w0);
            const double a0 = 1.0 + alpha / a;
            q.b0 = (1.0 + alpha * a) / a0;
            q.b1 = -2.0 * c / a0;
            q.b2 = (1.0 - alpha * a) / a0;
            q.a1 = q.b1;
            q.a2 = (1.0 - alpha / a) / a0;
            runs_[count_++] = b;
        }
        active_ = pre_on_ || count_ > 0;
    }

    void BusEq::clear()
    {
        for (Biquad &q : band_)
            q.s = {};
    }

    bool BusEq::ringing() const
    {
        for (unsigned i = 0; i < count_; ++i)
            for (const auto &ch : band_[runs_[i]].s)
                if (ch[0] != 0 || ch[1] != 0)
                    return true;
        return false;
    }

    void BusEq::run(int16_t &l, int16_t &r)
    {
        if (!active_)
            return;
        double x[2] = {static_cast<double>(l), static_cast<double>(r)};
        if (pre_on_)
        {
            x[0] *= pre_;
            x[1] *= pre_;
        }
        for (unsigned i = 0; i < count_; ++i)
        {
            Biquad &q = band_[runs_[i]];
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                // Transposed direct form II. A state far below a sample's step is let go to 0, so silence after a
                // stream ends in exact silence, and no denormal slows the sums: both halves at once, as one step
                // (zeroing either alone feeds the other a jump, and the filter can ring on at that level for good).
                auto &s = q.s[ch];
                const double y = q.b0 * x[ch] + s[0];
                s[0] = q.b1 * x[ch] - q.a1 * y + s[1];
                s[1] = q.b2 * x[ch] - q.a2 * y;
                if (std::fabs(s[0]) < 1e-9 && std::fabs(s[1]) < 1e-9)
                    s = {0, 0};
                x[ch] = y;
            }
        }
        const auto out = [](double v) {
            return static_cast<int16_t>(std::clamp(std::floor(v + 0.5), -32'768.0, 32'767.0));
        };
        l = out(x[0]);
        r = out(x[1]);
    }

    void BusVis::feed(int16_t l, int16_t r)
    {
        ring_[at_] = static_cast<int16_t>((l + r) >> 1);
        at_ = (at_ + 1) % N;
        peak_[0] = std::max(peak_[0], static_cast<unsigned>(std::abs(l)));
        peak_[1] = std::max(peak_[1], static_cast<unsigned>(std::abs(r)));
    }

    void BusVis::silence(uint64_t n)
    {
        for (uint64_t i = 0; i < std::min<uint64_t>(n, N); ++i)
        {
            ring_[at_] = 0;
            at_ = (at_ + 1) % N;
        }
    }

    void BusVis::snapshot(std::array<uint8_t, PAGE> &page)
    {
        const Tables &t = tables();
        std::array<double, N> x{};
        for (size_t n = 0; n < N; ++n)
        {
            const int16_t s = ring_[(at_ + n) % N];
            x[n] = t.hann[n] * static_cast<double>(s) / 32'768.0;
            page[SAMPLES + n] = static_cast<uint8_t>((s + 32'768) >> 8);
        }
        // Bins 1 to 32. A full-scale sine on a bin gives |X| = N / 4 (the window's mean is a half): that is 0 dBFS.
        for (size_t k = 1; k <= BANDS; ++k)
        {
            double re = 0, im = 0;
            for (size_t n = 0; n < N; ++n)
            {
                const size_t a = n * k % N;
                re += x[n] * t.cos[a];
                im -= x[n] * t.sin[a];
            }
            // The Nyquist bin is real, the window's whole sum: a full-scale cosine there gives N / 2.
            const double scale = static_cast<double>(N) / (k == N / 2 ? 2.0 : 4.0);
            page[k - 1] = dsp::level((re * re + im * im) / (scale * scale));
        }
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const double p = static_cast<double>(peak_[ch]) / 32'768.0;
            page[VU + ch] = dsp::level(p * p);
            peak_[ch] = 0;
        }
    }

    void BusVis::clear()
    {
        clear_frames();
        peak_ = {0, 0};
    }

    void BusVis::clear_frames()
    {
        ring_.fill(0);
        at_ = 0;
    }
} // namespace ooe::emu
