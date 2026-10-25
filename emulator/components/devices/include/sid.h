#pragma once

/**
 * @file sid.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A 6581 SID's oscillators and envelopes, clock by clock.
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
 * Not modelled yet, being sound rather than registers: the waveform DAC,
 * the filter and the mixer.
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

        Sid() { reset(); }
        /// /RES: registers, oscillators and envelopes to 0.
        void reset();
        void write(unsigned reg, uint8_t data);
        /// What the chip drives on a read: the read-only four; the
        /// write-only registers as last written (the card answers those).
        uint8_t read(unsigned reg) const;
        /// `clocks` cycles of the SID's clock.
        void clock(uint64_t clocks);

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

        std::array<uint8_t, REGS> regs_{};
        std::array<Voice, 3> voices_{};
    };
} // namespace ooe::emu
