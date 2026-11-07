/**
 * @file es5503.cpp
 * @brief The Ensoniq ES5503 (the DOC): its registers and its oscillators.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "es5503.h"

namespace ooe::emu
{
    void Es5503::reset()
    {
        for (Oscillator &o : osc_)
        {
            o.control |= CONTROL_HALT;
            o.data = 0x80;
            o.pending = false;
            o.acc = 0;
        }
        enabled_ = 1;
        last_irq_ = 0xFF;
        irq_ = false;
        out_.fill(0);
    }

    uint8_t Es5503::read(uint8_t reg)
    {
        if (reg != INTERRUPT)
            return peek(reg);
        uint8_t value = last_irq_;
        irq_ = false;
        for (unsigned i = 0; i < enabled_; ++i) // the lowest pending first
        {
            if (osc_[i].pending)
            {
                osc_[i].pending = false;
                value = static_cast<uint8_t>(i << 1);
                last_irq_ = static_cast<uint8_t>(value | 0x80);
                break;
            }
        }
        for (unsigned i = 0; i < enabled_; ++i) // more to take: the line goes up again at once
            irq_ = irq_ || osc_[i].pending;
        return static_cast<uint8_t>(value | 0x41);
    }

    uint8_t Es5503::peek(uint8_t reg) const
    {
        if (reg < INTERRUPT)
        {
            const Oscillator &o = osc_[reg & 0x1F];
            switch (reg & 0xE0)
            {
            case FREQ_LO: return static_cast<uint8_t>(o.freq);
            case FREQ_HI: return static_cast<uint8_t>(o.freq >> 8);
            case VOLUME: return o.volume;
            case DATA: return o.data;
            case POINTER: return o.pointer;
            case CONTROL: return o.control;
            default: return o.table;
            }
        }
        switch (reg)
        {
        case INTERRUPT:
            for (unsigned i = 0; i < enabled_; ++i)
                if (osc_[i].pending)
                    return static_cast<uint8_t>(i << 1 | 0x41);
            return static_cast<uint8_t>(last_irq_ | 0x41);
        case ENABLE: return static_cast<uint8_t>((enabled_ - 1) << 1);
        case ADC: return ADC_NONE;
        default: return 0;
        }
    }

    void Es5503::write(uint8_t reg, uint8_t data)
    {
        if (reg < INTERRUPT)
        {
            Oscillator &o = osc_[reg & 0x1F];
            switch (reg & 0xE0)
            {
            case FREQ_LO: o.freq = static_cast<uint16_t>((o.freq & 0xFF00) | data); break;
            case FREQ_HI: o.freq = static_cast<uint16_t>((o.freq & 0x00FF) | data << 8); break;
            case VOLUME: o.volume = data; break;
            case DATA: break; // read only
            case POINTER: o.pointer = data; break;
            case CONTROL:
            {
                const unsigned n = reg & 0x1Fu;
                if ((o.control & CONTROL_HALT) && !(data & CONTROL_HALT))
                    o.acc = 0; // started: from the top
                // Halted from here in one-shot or swap (the new value's mode bit 1): as the end of the table would,
                // by the mode it had, an interrupt only if the new value asks for one.
                if (!(o.control & CONTROL_HALT) && (data & CONTROL_HALT) && (data & 0x02))
                    halt(n, false, o.acc, data);
                o.control = data;
                break;
            }
            default: o.table = static_cast<uint8_t>(data & 0x7F); break;
            }
            return;
        }
        if (reg == ENABLE)
            enabled_ = ((data >> 1) & 0x1F) + 1u;
    }

    void Es5503::halt(unsigned n, bool zero, uint32_t &acc, uint8_t ctrl)
    {
        Oscillator &o = osc_[n];
        Oscillator &partner = osc_[n ^ 1u];
        unsigned m = mode(o);
        const uint32_t back = (uint32_t{1} << (8 + size(o))) - 1; // the table's size less one, in index steps
        if (m == MODE_SYNC_AM)
        {
            // An even one restarts the odd one below it, if that runs (oscillator 0 has none); both loop.
            if (!(n & 1) && n > 0 && !(osc_[n - 1].control & CONTROL_HALT))
                osc_[n - 1].acc = 0;
            m = MODE_FREE;
        }
        if (m != MODE_FREE || zero)
            o.control |= CONTROL_HALT;
        else
            acc -= back << shift(o); // free run loops, its phase kept
        if (m == MODE_SWAP)
        {
            partner.control &= static_cast<uint8_t>(~CONTROL_HALT); // the partner from the top
            partner.acc = 0;
        }
        else if (mode(partner) == MODE_SWAP && !(n & 1))
        {
            o.control &= static_cast<uint8_t>(~CONTROL_HALT); // an even one, its partner swapping: again
            acc -= back << shift(o);
        }
        if (!(o.control & CONTROL_IE))
            return;
        if (!zero && !(ctrl & CONTROL_IE))
            return; // a halt written: an interrupt only if the new value asks for one
        o.pending = true;
        irq_ = true;
    }

    void Es5503::step()
    {
        out_.fill(0);
        const std::array<uint8_t, RAM_BYTES> &ram = *ram_;
        for (unsigned n = 0; n < enabled_; ++n)
        {
            Oscillator &o = osc_[n];
            if (o.control & CONTROL_HALT)
                continue;
            const unsigned sz = size(o);
            const uint32_t index = o.acc >> shift(o);
            const uint32_t base = (uint32_t{(o.table & TABLE_BANK) != 0} << 16 | uint32_t{o.pointer} << 8) &
                                  (0x1FF00u & ~((uint32_t{1} << (8 + sz)) - 1));
            const uint8_t byte = ram[(base + (index & ((uint32_t{1} << (8 + sz)) - 1))) % RAM_BYTES];
            uint32_t acc = o.acc + o.freq; // a carry out of 24 bits kept: the next step's end check takes it
            o.data = byte;
            if (byte == 0)
                halt(n, true, acc, o.control);
            else
            {
                const int32_t sample = int32_t{byte} - 128;
                const unsigned ch = o.control >> 4;
                if (mode(o) != MODE_SYNC_AM || !(n & 1))
                    out_[ch] += sample * o.volume;
                else if (n + 1 < OSCILLATORS && !(osc_[n + 1].control & CONTROL_HALT))
                    osc_[n + 1].volume = byte; // AM: the odd one's byte is the volume of the one above
                if (index >= (uint32_t{1} << (8 + sz)) - 1)
                    halt(n, false, acc, o.control);
            }
            o.acc = acc; // below 2^24 + 2^16: an index past the table is always its end, and taken off
        }
    }

    std::array<int32_t, 2> Es5503::stereo() const
    {
        std::array<int32_t, 2> s{};
        for (unsigned ch = 0; ch < CHANNELS; ++ch)
            s[ch & 1] += out_[ch];
        return s;
    }
} // namespace ooe::emu
