/**
 * @file pia6821.cpp
 * @brief 6821 PIA implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "pia6821.h"

namespace ooe::emu
{
    namespace
    {
        constexpr uint8_t CR_FLAGS = Pia6821::CR_IRQ1 | Pia6821::CR_IRQ2;
    } // namespace

    Pia6821::Port Pia6821::port(bool b)
    {
        if (b)
            return Port{or_b_, ddr_b_, crb_, in_b_, write_b_};
        return Port{or_a_, ddr_a_, cra_, in_a_, write_a_};
    }

    uint8_t Pia6821::read(uint32_t offset)
    {
        Port p = port(offset & 2);
        if (offset & 1)
            return p.cr;
        if (!(p.cr & CR_SELECT_OR))
            return p.ddr;
        const auto v = static_cast<uint8_t>((p.out & p.ddr) | (p.in & ~p.ddr));
        p.cr = static_cast<uint8_t>(p.cr & ~CR_FLAGS); // reading the data clears the flags
        return v;
    }

    void Pia6821::write(uint32_t offset, uint8_t data)
    {
        Port p = port(offset & 2);
        if (offset & 1)
        {
            // Bits 6-7 are status: readable, not storable. Woz Monitor's
            // reset-time $A7 store to KBDCR used to fake a keypress (Phase 1).
            p.cr = static_cast<uint8_t>((data & ~CR_FLAGS) | (p.cr & CR_FLAGS));
            return;
        }
        if (!(p.cr & CR_SELECT_OR))
        {
            p.ddr = data;
            return;
        }
        p.out = data;
        if (p.on_write)
            p.on_write(static_cast<uint8_t>(p.out & p.ddr));
    }

    bool Pia6821::irq() const
    {
        return ((cra_ & CR_IRQ1) && (cra_ & CR_IRQ1_ENABLE)) || ((crb_ & CR_IRQ1) && (crb_ & CR_IRQ1_ENABLE));
    }
} // namespace ooe::emu
