#pragma once

/**
 * @file sid.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A SID (6581 or 8580): oscillators, envelopes, and the output stage, clock by clock.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The sound card's two chips (sound_card.h), as far as software can see
 * them: the 29 registers, and the four it reads back. POTX and POTY read
 * $FF (no paddles); OSC3 is voice 3's waveform, its top 8 bits; ENV3 is
 * voice 3's envelope. Tunes read those two for vibrato, random numbers and
 * timing, so the parts behind them are modelled clock by clock, from the
 * chip's documented and measured behaviour:
 *
 * Oscillators. Each voice adds its 16-bit frequency to a 24-bit phase
 * accumulator every clock (Hz = freq x clock / 2^24). Its 12-bit waveforms:
 * sawtooth, the accumulator's top 12 bits; triangle, the top bit folding
 * the next 11 (XORed with the ring source's top bit when RING is set);
 * pulse, all ones while the top 12 bits are at or above the pulse width;
 * noise, 8 bits of a 23-bit shift register (taps 22 and 17), clocked when
 * accumulator bit 19 rises. Voice n's sync and ring source is voice n-1
 * (voice 1's is voice 3): SYNC resets the accumulator when the source's top
 * bit rises. TEST holds the accumulator at 0 and the pulse output high,
 * and resets the noise register. Two or more waveforms at once are ANDed:
 * the real chip's mixes are lower and uneven (a property of its die,
 * measured in tables this emulator doesn't use), so OSC3 then reads
 * approximately.
 *
 * Envelopes. An 8-bit counter, stepped by a 15-bit rate counter whose
 * period comes from the rate nibble: 9, 32, 63, 95, 149, 220, 267, 313,
 * 392, 977, 1954, 3126, 3907, 11720, 19532, 31251 clocks (2 ms to 8 s for
 * a full attack at 1 MHz). Attack counts up to $FF; decay and release
 * count down with the chip's exponential stretch (each step taking 1, 2, 4,
 * 8, 16 or 30 rate periods below $FF, $5D, $36, $1A, $0E and $06), decay
 * stopping at the sustain level, $11 x the nibble. GATE on starts attack
 * from wherever the counter is; off starts release. The rate counter only
 * resets when it matches the period, so a period written lower than the
 * count runs on through $7FFF first: the SID's ADSR delay, which tunes
 * work around and some rely on. At 0 the counter stays until the next
 * attack.
 *
 * Output (clock_audio(); clock() skips it, for a card nobody listens to).
 * Each voice is its 12-bit waveform, less the DAC's zero level, times its
 * envelope: on the 6581 the zero sits low ($380 of $FFF), so a voice's
 * envelope moves a DC level even with no waveform, as on the chip; the
 * 8580's sits in the middle. $D417's low bits send voices through the
 * filter, the rest go straight to the mixer, except voice 3 with $D418
 * bit 7 (3OFF) unless it is filtered. The filter is a two-pole state
 * variable filter, run every clock, its low-, band- and high-pass outputs
 * chosen by $D418 bits 4-6 and summed; resonance ($D417's high nibble)
 * sets its Q: on the 6581 0.707 + res/15, on the 8580 2^((res-4)/8), from
 * 0.7 to 1.7 and 2.6. The cutoff's 11 bits map to Hz as the
 * chips are measured to, approximately: the 8580's is nearly linear, 30 Hz
 * to 12.5 kHz; the 6581's (which varies chip to chip) stays low through
 * the bottom quarter and rises steeply, about 220 Hz to 18 kHz. The mixer
 * adds its own DC level and scales everything by the master volume; on
 * the 6581 that DC is large, so writing $D418 alone makes a 4-bit DAC (the
 * sample playback in many tunes), while on the 8580 it is faint, as it is
 * on the chip. Output is about -1 to 1 for one voice at full envelope.
 *
 * Not modelled: the DACs' nonlinearity, the 6581 filter's distortion, the
 * combined waveforms' real values, and EXT IN.
 */

#include <array>
#include <cstdint>

namespace ooe::emu
{
    class Sid
    {
    public:
        static constexpr unsigned REGS = 29;
        static constexpr uint8_t POTX = 0x19, POTY = 0x1A, OSC3 = 0x1B, ENV3 = 0x1C;
        /// A voice's control register ($04, $0B, $12).
        static constexpr uint8_t GATE = 0x01, SYNC = 0x02, RING = 0x04, TEST = 0x08, TRIANGLE = 0x10, SAW = 0x20,
                                 PULSE = 0x40, NOISE = 0x80;
        static constexpr uint32_t NOISE_SEED = 0x7FFFF8;

        enum class Phase : uint8_t
        {
            Attack,
            DecaySustain,
            Release,
        };

        struct Voice
        {
            uint32_t accumulator{0}; ///< 24 bits
            uint32_t noise{NOISE_SEED}; ///< 23 bits
            uint8_t envelope{0};
            Phase phase{Phase::Release};
            uint16_t rate_count{0}; ///< 15 bits
            uint8_t exp_count{0};
            uint8_t exp_period{1}; ///< rate periods a decay or release step takes
            bool frozen{true};     ///< at 0, waiting for an attack
        };

        enum class Model : uint8_t
        {
            Mos6581,
            Mos8580,
        };

        Sid()
        {
            set_model(Model::Mos6581);
            reset();
        }
        /// The chip: the output stage differs (the registers don't).
        void set_model(Model m);
        Model model() const { return model_; }
        /// The clock the chip runs at, for the filter's cutoff in Hz (default PAL's).
        void set_clock_hz(uint32_t hz);
        /// /RES: registers, oscillators and envelopes to 0.
        void reset();
        void write(unsigned reg, uint8_t data);
        /// What the chip drives on a read: the read-only four; the
        /// write-only registers as last written (the card answers those).
        uint8_t read(unsigned reg) const;
        /// `clocks` cycles of the SID's clock.
        void clock(uint64_t clocks);
        /// The same, through the output stage: @return the sum of the
        /// chip's output over those clocks (each about -1 to 1).
        double clock_audio(uint64_t clocks);
        /// Leave voices out of the output (bit n: voice n + 1), as a player's mute does; the chip runs on.
        void set_muted(uint8_t voices) { muted_ = voices & 7; }
        uint8_t muted() const { return muted_; }
        /// The output now (clock_audio()'s last clock).
        float output() const { return out_; }
        /// The filter's cutoff in Hz, as the registers set it.
        float cutoff_hz() const;

        const std::array<uint8_t, REGS> &regs() const { return regs_; }
        const Voice &voice(unsigned n) const { return voices_[n % 3]; }
        /// Voice `n`'s 12-bit waveform output now.
        unsigned waveform(unsigned n) const;

    private:
        uint16_t freq(unsigned n) const { return static_cast<uint16_t>(regs_[n * 7] | regs_[n * 7 + 1] << 8); }
        uint16_t pulse_width(unsigned n) const
        {
            return static_cast<uint16_t>(regs_[n * 7 + 2] | (regs_[n * 7 + 3] & 0x0F) << 8);
        }
        uint8_t control(unsigned n) const { return regs_[n * 7 + 4]; }
        uint16_t rate_period(unsigned n) const;
        void clock_oscillators();                   ///< one clock, sync included
        void advance_oscillators(uint64_t clocks); ///< many, when no voice syncs
        void clock_envelope(unsigned n, uint64_t clocks);
        void step_envelope(Voice &v, unsigned n);
        /// Every envelope at 0 and the filter at rest: the output holds.
        bool quiet() const;
        void tune_filter();
        float mix(); ///< one clock of the output stage

        std::array<uint8_t, REGS> regs_{};
        std::array<Voice, 3> voices_{};
        Model model_{Model::Mos6581};
        uint32_t clock_hz_{985'248};
        float zero_{0x380};  ///< the waveform DAC's zero level
        float mixer_dc_{0}; ///< the mixer's DC, in voice full scales
        float w_{0}, q1_{1.4142f}; ///< the filter's frequency and damping
        float lp_{0}, bp_{0}; ///< the filter's state
        float out_{0};
        uint8_t muted_{0};
    };
} // namespace ooe::emu
