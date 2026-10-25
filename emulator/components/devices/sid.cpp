/**
 * @file sid.cpp
 * @brief A 6581 SID's oscillators and envelopes.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Time is spent in bulk where nothing depends on the clock-by-clock order:
 * an accumulator just adds freq x clocks (and the noise register is clocked
 * once for each rise of bit 19 on the way), and an envelope jumps from one
 * rate-counter match to the next. Only hard sync, where one voice resets
 * another the clock its top bit rises, needs every clock.
 */

#include "sid.h"

namespace ooe::emu
{
    namespace
    {
        constexpr uint32_t ACC_MASK = 0xFFFFFF, TOP = 0x800000, NOISE_CLOCK = 0x080000;
        constexpr uint16_t RATE_MASK = 0x7FFF;

        /// Clocks per envelope step, by rate nibble (the chip's, measured).
        constexpr uint16_t RATE_PERIODS[16] = {9,   32,  63,   95,   149,  220,   267,   313,
                                               392, 977, 1954, 3126, 3907, 11720, 19532, 31251};

        uint32_t clock_noise(uint32_t r)
        {
            const uint32_t bit = ((r >> 22) ^ (r >> 17)) & 1u;
            return ((r << 1) | bit) & 0x7FFFFF;
        }

        /// The noise waveform: eight of the register's bits, as the top 8 of 12.
        unsigned noise_output(uint32_t r)
        {
            return ((r >> 20 & 1) << 11) | ((r >> 18 & 1) << 10) | ((r >> 14 & 1) << 9) | ((r >> 11 & 1) << 8) |
                   ((r >> 9 & 1) << 7) | ((r >> 5 & 1) << 6) | ((r >> 2 & 1) << 5) | ((r & 1) << 4);
        }

        /// The voice that syncs and ring-modulates voice n.
        constexpr unsigned source(unsigned n) { return (n + 2) % 3; }
    } // namespace

    void Sid::reset()
    {
        regs_ = {};
        voices_ = {};
    }

    void Sid::write(unsigned reg, uint8_t data)
    {
        if (reg >= POTX)
            return; // read-only
        const unsigned n = reg / 7;
        if (n < 3 && reg % 7 == 4)
        {
            Voice &v = voices_[n];
            const uint8_t was = regs_[reg];
            if (!(was & GATE) && (data & GATE))
            {
                v.phase = Phase::Attack; // from wherever the counter is
                v.frozen = false;
            }
            else if ((was & GATE) && !(data & GATE))
            {
                v.phase = Phase::Release;
            }
            if (data & TEST)
            {
                v.accumulator = 0;
                v.noise = NOISE_SEED;
            }
        }
        regs_[reg] = data;
    }

    uint8_t Sid::read(unsigned reg) const
    {
        switch (reg)
        {
        case POTX:
        case POTY:
            return 0xFF; // no paddles
        case OSC3:
            return static_cast<uint8_t>(waveform(2) >> 4);
        case ENV3:
            return voices_[2].envelope;
        default:
            return reg < REGS ? regs_[reg] : 0xFF;
        }
    }

    unsigned Sid::waveform(unsigned n) const
    {
        const Voice &v = voices_[n % 3];
        const uint8_t ctrl = control(n % 3);
        if (!(ctrl & (TRIANGLE | SAW | PULSE | NOISE)))
            return 0;
        unsigned out = 0xFFF;
        if (ctrl & TRIANGLE)
        {
            uint32_t top = v.accumulator & TOP;
            if (ctrl & RING)
                top ^= voices_[source(n % 3)].accumulator & TOP;
            out &= ((top ? ~v.accumulator : v.accumulator) >> 11) & 0xFFF;
        }
        if (ctrl & SAW)
            out &= v.accumulator >> 12;
        if (ctrl & PULSE)
            out &= (ctrl & TEST) || (v.accumulator >> 12) >= pulse_width(n % 3) ? 0xFFFu : 0u;
        if (ctrl & NOISE)
            out &= noise_output(v.noise);
        return out;
    }

    void Sid::clock(uint64_t clocks)
    {
        if (!clocks)
            return;
        bool syncing = false;
        for (unsigned n = 0; n < 3; ++n)
            syncing = syncing || ((control(n) & SYNC) && freq(source(n)) && !(control(source(n)) & TEST));
        if (syncing)
            for (uint64_t i = 0; i < clocks; ++i)
                clock_oscillators();
        else
            advance_oscillators(clocks);
        for (unsigned n = 0; n < 3; ++n)
            clock_envelope(n, clocks);
    }

    void Sid::clock_oscillators()
    {
        bool rose[3]{};
        for (unsigned n = 0; n < 3; ++n)
        {
            Voice &v = voices_[n];
            if (control(n) & TEST)
                continue; // held at 0
            const uint32_t was = v.accumulator;
            v.accumulator = (was + freq(n)) & ACC_MASK;
            rose[n] = !(was & TOP) && (v.accumulator & TOP);
            if (!(was & NOISE_CLOCK) && (v.accumulator & NOISE_CLOCK))
                v.noise = clock_noise(v.noise);
        }
        for (unsigned n = 0; n < 3; ++n)
            if ((control(n) & SYNC) && rose[source(n)])
                voices_[n].accumulator = 0;
    }

    void Sid::advance_oscillators(uint64_t clocks)
    {
        for (unsigned n = 0; n < 3; ++n)
        {
            Voice &v = voices_[n];
            if (control(n) & TEST)
                continue;
            const uint64_t from = v.accumulator, to = from + clocks * freq(n);
            // bit 19 rises at k x 2^20 + 2^19: count those passed on the way
            const uint64_t half = NOISE_CLOCK;
            const uint64_t rises = (to + half) / (2 * half) - (from + half) / (2 * half);
            for (uint64_t i = 0; i < rises; ++i)
                v.noise = clock_noise(v.noise);
            v.accumulator = static_cast<uint32_t>(to & ACC_MASK);
        }
    }

    uint16_t Sid::rate_period(unsigned n) const
    {
        const Voice &v = voices_[n];
        const uint8_t ad = regs_[n * 7 + 5], sr = regs_[n * 7 + 6];
        switch (v.phase)
        {
        case Phase::Attack: return RATE_PERIODS[ad >> 4];
        case Phase::DecaySustain: return RATE_PERIODS[ad & 0x0F];
        case Phase::Release: return RATE_PERIODS[sr & 0x0F];
        }
        return RATE_PERIODS[0];
    }

    void Sid::clock_envelope(unsigned n, uint64_t clocks)
    {
        Voice &v = voices_[n];
        while (clocks)
        {
            // The count goes up one a clock, mod $8000, and steps on matching
            // the period: a period below the count waits for the wrap.
            const uint16_t period = rate_period(n);
            uint64_t to_match = static_cast<uint16_t>((period - v.rate_count) & RATE_MASK);
            if (to_match == 0)
                to_match = RATE_MASK + 1u;
            if (clocks < to_match)
            {
                v.rate_count = static_cast<uint16_t>((v.rate_count + clocks) & RATE_MASK);
                return;
            }
            clocks -= to_match;
            v.rate_count = 0;
            step_envelope(v, n);
        }
    }

    void Sid::step_envelope(Voice &v, unsigned n)
    {
        // Decay and release step only every exp_period rate periods; attack every one.
        if (v.phase != Phase::Attack && ++v.exp_count != v.exp_period)
            return;
        v.exp_count = 0;
        if (v.frozen)
            return;
        switch (v.phase)
        {
        case Phase::Attack:
            if (++v.envelope == 0xFF)
                v.phase = Phase::DecaySustain;
            break;
        case Phase::DecaySustain:
            if (v.envelope != (regs_[n * 7 + 6] >> 4) * 0x11)
                --v.envelope;
            break;
        case Phase::Release:
            --v.envelope;
            break;
        }
        switch (v.envelope) // the exponential stretch, set as the counter passes each mark
        {
        case 0xFF: v.exp_period = 1; break;
        case 0x5D: v.exp_period = 2; break;
        case 0x36: v.exp_period = 4; break;
        case 0x1A: v.exp_period = 8; break;
        case 0x0E: v.exp_period = 16; break;
        case 0x06: v.exp_period = 30; break;
        case 0x00:
            v.exp_period = 1;
            v.frozen = true; // until the next attack
            break;
        default: break;
        }
    }
} // namespace ooe::emu
