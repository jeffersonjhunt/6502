/**
 * @file es5503.cpp
 * @brief The Ensoniq ES5503 (the DOC): its registers.
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
        }
        enabled_ = 1;
        last_irq_ = 0xFF;
    }

    uint8_t Es5503::read(uint8_t reg)
    {
        if (reg != INTERRUPT)
            return peek(reg);
        uint8_t value = last_irq_;
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
            case CONTROL: o.control = data; break;
            default: o.table = static_cast<uint8_t>(data & 0x7F); break;
            }
            return;
        }
        if (reg == ENABLE)
            enabled_ = ((data >> 1) & 0x1F) + 1u;
    }
} // namespace ooe::emu
