/**
 * @file via6522.cpp
 * @brief 6522 VIA implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "via6522.h"

namespace ooe::emu
{
    namespace
    {
        constexpr uint8_t lo8(unsigned v) { return static_cast<uint8_t>(v & 0xFF); }

        // PCR: CA2/CB2 is an input in "independent interrupt" mode (x01 / x11),
        // where reading or writing the port leaves its flag alone.
        bool ca2_independent(uint8_t pcr) { return (pcr & 0x0A) == 0x02; }
        bool cb2_independent(uint8_t pcr) { return (pcr & 0xA0) == 0x20; }
    } // namespace

    uint8_t Via6522::output(Port port) const
    {
        if (port == Port::A)
            return ora_;
        if (acr_ & ACR_T1_PB7)
            return static_cast<uint8_t>((orb_ & 0x7F) | (pb7_ ? 0x80 : 0));
        return orb_;
    }

    uint8_t Via6522::port_value(Port port) const
    {
        uint8_t dir = ddr(port);
        if (port == Port::B && (acr_ & ACR_T1_PB7))
            dir = static_cast<uint8_t>(dir | 0x80); // PB7 is T1's, whatever DDRB says
        const uint8_t in = port == Port::A ? in_a_ : in_b_;
        return static_cast<uint8_t>((output(port) & dir) | (in & ~dir));
    }

    void Via6522::notify(Port port)
    {
        const uint8_t dir = port == Port::B && (acr_ & ACR_T1_PB7) ? static_cast<uint8_t>(ddrb_ | 0x80) : ddr(port);
        for (const Listener &l : listeners_)
            l(port, output(port), dir);
    }

    void Via6522::clear_port_flags(Port port)
    {
        if (port == Port::A)
            ifr_ = static_cast<uint8_t>(ifr_ & ~(IRQ_CA1 | (ca2_independent(pcr_) ? 0 : IRQ_CA2)));
        else
            ifr_ = static_cast<uint8_t>(ifr_ & ~(IRQ_CB1 | (cb2_independent(pcr_) ? 0 : IRQ_CB2)));
    }

    uint8_t Via6522::peek(uint32_t offset) const
    {
        switch (offset & 0xF)
        {
        case 0x0: return port_value(Port::B);
        case 0x1:
        case 0xF: return port_value(Port::A);
        case 0x2: return ddrb_;
        case 0x3: return ddra_;
        case 0x4: return lo8(t1_);
        case 0x5: return lo8(t1_ >> 8);
        case 0x6: return lo8(t1_latch_);
        case 0x7: return lo8(t1_latch_ >> 8);
        case 0x8: return lo8(t2_);
        case 0x9: return lo8(t2_ >> 8);
        case 0xA: return sr_;
        case 0xB: return acr_;
        case 0xC: return pcr_;
        case 0xD: return static_cast<uint8_t>(ifr_ | (irq() ? IRQ_ANY : 0));
        default: return static_cast<uint8_t>(ier_ | 0x80);
        }
    }

    uint8_t Via6522::read(uint32_t offset)
    {
        const uint8_t v = peek(offset);
        switch (offset & 0xF)
        {
        case 0x0: clear_port_flags(Port::B); break;
        case 0x1: clear_port_flags(Port::A); break;
        case 0x4: ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_T1); break;
        case 0x8: ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_T2); break;
        case 0xA: ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_SR); break;
        default: break;
        }
        return v;
    }

    void Via6522::write(uint32_t offset, uint8_t data)
    {
        switch (offset & 0xF)
        {
        case 0x0:
            orb_ = data;
            clear_port_flags(Port::B);
            notify(Port::B);
            break;
        case 0x1:
            ora_ = data;
            clear_port_flags(Port::A);
            notify(Port::A);
            break;
        case 0xF:
            ora_ = data;
            notify(Port::A);
            break;
        case 0x2:
            ddrb_ = data;
            notify(Port::B);
            break;
        case 0x3:
            ddra_ = data;
            notify(Port::A);
            break;
        case 0x4:
        case 0x6:
            t1_latch_ = static_cast<uint16_t>((t1_latch_ & 0xFF00) | data);
            break;
        case 0x5:
            t1_latch_ = static_cast<uint16_t>((t1_latch_ & 0x00FF) | (data << 8));
            t1_ = t1_latch_;
            t1_reload_ = false;
            t1_hold_ = true;
            t1_armed_ = true;
            ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_T1);
            if (acr_ & ACR_T1_PB7)
            {
                pb7_ = false;
                notify(Port::B);
            }
            break;
        case 0x7:
            t1_latch_ = static_cast<uint16_t>((t1_latch_ & 0x00FF) | (data << 8));
            ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_T1);
            break;
        case 0x8:
            t2_latch_lo_ = data;
            break;
        case 0x9:
            t2_ = static_cast<uint16_t>(t2_latch_lo_ | (data << 8));
            t2_hold_ = true;
            t2_armed_ = true;
            ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_T2);
            break;
        case 0xA:
            sr_ = data;
            ifr_ = static_cast<uint8_t>(ifr_ & ~IRQ_SR);
            break;
        case 0xB:
        {
            const bool pb7_was = acr_ & ACR_T1_PB7;
            acr_ = data;
            if (pb7_was != static_cast<bool>(acr_ & ACR_T1_PB7))
                notify(Port::B);
            break;
        }
        case 0xC:
            pcr_ = data;
            break;
        case 0xD:
            ifr_ = static_cast<uint8_t>(ifr_ & ~(data & 0x7F));
            break;
        default: // 0xE
            if (data & 0x80)
                ier_ = static_cast<uint8_t>(ier_ | (data & 0x7F));
            else
                ier_ = static_cast<uint8_t>(ier_ & ~(data & 0x7F));
            break;
        }
    }

    // --- timers ------------------------------------------------------------
    //
    // A counter decrements once per cycle, but not in the cycle that loaded
    // it; the timeout is the cycle it goes from 0 to $FFFF, N+1 cycles
    // after that. In free-running mode the cycle after a timeout reloads
    // the latch instead of decrementing, which makes the period N+2.

    void Via6522::t1_timeout()
    {
        if (acr_ & ACR_T1_FREE_RUN)
        {
            ifr_ = static_cast<uint8_t>(ifr_ | IRQ_T1);
            t1_reload_ = true;
            if (acr_ & ACR_T1_PB7)
            {
                pb7_ = !pb7_;
                notify(Port::B);
            }
        }
        else if (t1_armed_)
        {
            ifr_ = static_cast<uint8_t>(ifr_ | IRQ_T1);
            t1_armed_ = false;
            if (acr_ & ACR_T1_PB7)
            {
                pb7_ = true;
                notify(Port::B);
            }
        }
    }

    void Via6522::advance_t1(uint64_t n)
    {
        while (n > 0)
        {
            if (t1_hold_ || t1_reload_)
            {
                if (t1_reload_)
                    t1_ = t1_latch_;
                t1_hold_ = t1_reload_ = false;
                --n;
                continue;
            }
            if (t1_ >= n)
            {
                t1_ = static_cast<uint16_t>(t1_ - n);
                return;
            }
            n -= static_cast<uint64_t>(t1_) + 1; // down to 0, then the timeout cycle
            t1_ = 0xFFFF;
            t1_timeout();
        }
    }

    void Via6522::advance_t2(uint64_t n)
    {
        if (acr_ & ACR_T2_COUNT_PB6)
            return; // counts PB6 pulses instead (drive())
        if (n > 0 && t2_hold_)
        {
            t2_hold_ = false;
            --n;
        }
        while (n > 0)
        {
            if (t2_ >= n)
            {
                t2_ = static_cast<uint16_t>(t2_ - n);
                return;
            }
            n -= static_cast<uint64_t>(t2_) + 1;
            t2_ = 0xFFFF;
            if (t2_armed_)
            {
                ifr_ = static_cast<uint8_t>(ifr_ | IRQ_T2);
                t2_armed_ = false;
            }
        }
    }

    void Via6522::tick(uint64_t cycles)
    {
        advance_t1(cycles);
        advance_t2(cycles);
    }

    // --- peripheral side -----------------------------------------------------

    void Via6522::drive(Port port, uint8_t mask, uint8_t value)
    {
        uint8_t &in = port == Port::A ? in_a_ : in_b_;
        const uint8_t before = in;
        in = static_cast<uint8_t>((in & ~mask) | (value & mask));
        // T2 in pulse-counting mode: one count per PB6 falling edge.
        if (port == Port::B && (acr_ & ACR_T2_COUNT_PB6) && (before & 0x40) && !(in & 0x40))
        {
            t2_ = static_cast<uint16_t>(t2_ - 1);
            if (t2_ == 0 && t2_armed_) // after N pulses
            {
                ifr_ = static_cast<uint8_t>(ifr_ | IRQ_T2);
                t2_armed_ = false;
            }
        }
    }

    void Via6522::edge(bool &line, bool level, bool rising_active, uint8_t flag)
    {
        if (level != line && level == rising_active)
            ifr_ = static_cast<uint8_t>(ifr_ | flag);
        line = level;
    }

    void Via6522::set_ca1(bool level) { edge(ca1_, level, pcr_ & 0x01, IRQ_CA1); }
    void Via6522::set_cb1(bool level) { edge(cb1_, level, pcr_ & 0x10, IRQ_CB1); }

    void Via6522::set_ca2(bool level)
    {
        if (pcr_ & 0x08) // output mode: the pin is ours, not an input
        {
            ca2_ = level;
            return;
        }
        edge(ca2_, level, pcr_ & 0x04, IRQ_CA2);
    }

    void Via6522::set_cb2(bool level)
    {
        if (pcr_ & 0x80)
        {
            cb2_ = level;
            return;
        }
        edge(cb2_, level, pcr_ & 0x40, IRQ_CB2);
    }
} // namespace ooe::emu
