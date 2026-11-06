#pragma once

/**
 * @file sound_dsp.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Sound card revision 2's digital bus: the EQ and the vis page.
 * @version 0.1.0
 * @date 2026-11-06
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/sound-rev2.md ("Signal path", "Vis page") is the spec, proposed. Frames
 * are signed 16-bit, left and right, at the bus's rate.
 *
 * BusEq is the EQ: the preamp, then ten peaking biquads (the RBJ cookbook's,
 * Q 1.41) at 60, 170, 310, 600 Hz and 1, 3, 6, 12, 14, 16 kHz, each channel
 * its own. Gains are EQVAL's half-dB. A band at or above 0.45 of the bus rate
 * is bypassed, and so is one at 0 (as is the preamp at 0), so with every gain
 * 0, or disabled, a frame comes out as it went in, bit for bit. The sums are
 * doubles, rounded to the nearest sample and clipped at the end.
 *
 * BusVis is the vis page's source: the last 64 frames' mono mix ((L + R) / 2)
 * and each side's peak since the last snapshot, after the EQ. snapshot() makes
 * the page: 0-31 the spectrum, bins 1 to 32 of a 64-point FFT of those frames
 * with a periodic Hann window, scaled so a full-scale sine on a bin reads
 * 0 dBFS (bin 32, Nyquist, real: a full-scale cosine there); 32-33 the peaks; both 0 to 255 for -60 to 0 dBFS, rounded; 34-97
 * the frames' high bytes, unsigned (oldest first, $80 zero).
 *
 * Every number is made the same on every target: only +, -, x, / and sqrt,
 * which IEEE 754 rounds exactly, built without fused multiply-adds; the
 * coefficients' sines, cosines and powers of ten by series here, not libm.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace ooe::emu
{
    class BusEq
    {
    public:
        static constexpr unsigned BANDS = 10;
        static constexpr std::array<unsigned, BANDS> CENTRE_HZ = {60, 170, 310, 600, 1'000, 3'000, 6'000, 12'000,
                                                                   14'000, 16'000};
        /// Q, the cookbook's, in hundredths.
        static constexpr unsigned Q_100 = 141;

        /// The gains (the preamp, then the bands, in half-dB) and enable, at the bus `rate`: the coefficients
        /// anew, the filters' state kept.
        void set(const std::array<int8_t, BANDS + 1> &gains, bool on, uint32_t rate);
        /// The filters' state to silence (a new stream).
        void clear();
        /// One frame, in place.
        void run(int16_t &l, int16_t &r);
        /// Nothing to do: off or flat, or (`ringing` false) its state silent too, so silence in is silence out.
        bool idle() const { return !active_; }
        bool ringing() const;
        /// Whether band `b` (0-9) runs at `rate`: below 0.45 of it.
        static bool band_runs(unsigned b, uint32_t rate) { return CENTRE_HZ[b] * 100ull < 45ull * rate; }

    private:
        struct Biquad
        {
            double b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
            std::array<std::array<double, 2>, 2> s{}; ///< per channel, the transposed form's two
        };
        std::array<Biquad, BANDS> band_{};
        std::array<unsigned, BANDS> runs_{}; ///< the bands that run, in order
        unsigned count_{0};
        double pre_{1};
        bool pre_on_{false};
        bool active_{false};
    };

    class BusVis
    {
    public:
        static constexpr size_t PAGE = 98;
        static constexpr size_t BANDS = 32, VU = 32, SAMPLES = 34, N = 64;

        /// One frame, after the EQ.
        void feed(int16_t l, int16_t r);
        /// `n` frames of silence.
        void silence(uint64_t n);
        /// The page from what's been fed; the peaks start again.
        void snapshot(std::array<uint8_t, PAGE> &page);
        /// Nothing fed: the page all silence.
        void clear();

    private:
        std::array<int16_t, N> ring_{}; ///< the mono mix, a ring
        size_t at_{0};                  ///< the oldest
        std::array<unsigned, 2> peak_{};
    };

    namespace dsp
    {
        /// Deterministic: the same bits on every IEEE 754 target.
        double sin(double x);
        double cos(double x);
        double exp(double x);
        double pow10(double x);
        /// 0 to 255 for -60 to 0 dBFS (rounded), of a power relative to full scale's (1.0).
        uint8_t level(double power);
    } // namespace dsp
} // namespace ooe::emu
