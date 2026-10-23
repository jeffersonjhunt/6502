/**
 * @file console_card.cpp
 * @brief The backplane's Console card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "console_card.h"

namespace ooe::emu
{
    namespace
    {
        constexpr uint32_t ADDR_MASK = PHYSICAL_SPACE - 1;

        uint8_t byte_of(uint32_t v, unsigned i) { return static_cast<uint8_t>(v >> (8 * i)); }

        void set_byte(uint32_t &v, unsigned i, uint8_t b)
        {
            v = ((v & ~(0xFFu << (8 * i))) | static_cast<uint32_t>(b) << (8 * i)) & ADDR_MASK;
        }
    } // namespace

    bool ConsoleCard::halted() const
    {
        return ((panel_ & HALT_SWITCH) && !locked()) || (ctrl_ & CTRL_HALT) || bp_halt_;
    }

    bool ConsoleCard::may_run(uint16_t pc)
    {
        const bool skip = skip_fetch_;
        skip_fetch_ = false;
        if (halted())
        {
            if (step_)
            {
                step_ = false;
                frozen_ = false; // its last cycle becomes the new stop cycle
                return true;     // CONT: this one instruction, then halted again
            }
            latch(last_);
            return false;
        }
        if (!skip && (bpctrl_ & (BP_ENABLE | BP_FETCH | BP_NMI)) == (BP_ENABLE | BP_FETCH) && decoder_ &&
            decoder_->physical(pc) == bpaddr_)
        {
            // RDY low at the SYNC cycle: the fetch is held, PC is on it.
            hit(Cycle{bpaddr_, decoder_->peek(pc), static_cast<uint8_t>(L_SYNC | L_RWB)});
            bp_halt_ = true;
            return false;
        }
        return true;
    }

    void ConsoleCard::on_cycle(const BusCycle &c)
    {
        const uint32_t phys = decoder_ ? decoder_->last_access().physical : c.addr;
        last_ = Cycle{phys, c.data,
                      static_cast<uint8_t>((c.sync ? L_SYNC : 0) | (c.write ? 0 : L_RWB) |
                                           (c.vector_pull ? L_VPB : 0))};

        if (lights_on_)
        {
            const uint64_t w = 1 + (decoder_ ? decoder_->wait_states() : 0); // a held access glows longer
            for (unsigned i = 0; i < ADDRESS_LIGHTS; ++i)
                addr_high_[i] += (phys >> i & 1) * w;
            for (unsigned i = 0; i < DATA_LIGHTS; ++i)
                data_high_[i] += (c.data >> i & 1) * w;
            sync_high_ += c.sync * w;
            rwb_high_ += !c.write * w;
            vpb_high_ += c.vector_pull * w;
            rdy_held_ += w - 1;
            lit_cycles_ += w;
        }

        if (!(bpctrl_ & BP_ENABLE) || phys != bpaddr_)
            return;
        const bool fetch = c.sync && (bpctrl_ & BP_FETCH);
        const bool data = !c.sync && (c.write ? (bpctrl_ & BP_WRITE) : (bpctrl_ & BP_READ));
        if (bpctrl_ & BP_NMI)
        {
            if (fetch || data)
            {
                status_hit_ = HIT;
                latched_ = last_; // LADDR etc. for the debugger on the machine; nothing halts
                nmi_ = true;
            }
        }
        else if (data) // a halting fetch match was caught before the instruction
        {
            hit(last_);
            bp_halt_ = true;
        }
    }

    void ConsoleCard::hit(const Cycle &c)
    {
        status_hit_ = HIT;
        latch(c);
    }

    void ConsoleCard::latch(const Cycle &c)
    {
        if (frozen_)
            return;
        frozen_ = true;
        latched_ = c;
        shown_ = c;
    }

    bool ConsoleCard::take_nmi()
    {
        const bool n = nmi_;
        nmi_ = false;
        return n;
    }

    bool ConsoleCard::take_start()
    {
        const bool s = start_;
        start_ = false;
        return s;
    }

    void ConsoleCard::resume()
    {
        bp_halt_ = false;
        ctrl_ = static_cast<uint8_t>(ctrl_ & ~CTRL_HALT);
        frozen_ = false;
        skip_fetch_ = true; // or it would stop again on the breakpoint it is leaving
    }

    void ConsoleCard::press(uint8_t keys)
    {
        if (!locked())
            apply(keys);
    }

    void ConsoleCard::press_start()
    {
        if (locked())
            return;
        reset_card();
        start_ = true;
    }

    void ConsoleCard::reset_card()
    {
        ctrl_ = 0;
        ien_ = 0;
        isr_ = 0;
        keys_ = 0;
        bp_halt_ = false;
        frozen_ = false;
        step_ = false;
        nmi_ = false;
        last_key_ = 0;
    }

    void ConsoleCard::apply(uint8_t keys)
    {
        keys &= LOAD_ADRS | EXAM | DEP | CONT;
        if (!keys)
            return;
        keys_ |= keys;
        if (ien_ & 1)
            isr_ = 1;

        if (keys & LOAD_ADRS)
        {
            paddr_ = swr_;
            last_key_ = 0;
        }
        for (const uint8_t key : {EXAM, DEP})
        {
            if (!(keys & key) || !halted() || !decoder_)
                continue; // only while halted, as on the PDP-11
            if (last_key_ == key)
                paddr_ = (paddr_ + 1) & ADDR_MASK; // repeated presses step through memory
            if (key == DEP)
                decoder_->write_physical(paddr_, static_cast<uint8_t>(swr_));
            const uint8_t v = decoder_->peek_physical(paddr_);
            shown_ = Cycle{paddr_, v, static_cast<uint8_t>(key == DEP ? 0 : L_RWB)};
            last_key_ = key;
        }
        if ((keys & CONT) && halted())
        {
            const bool switch_halt = (panel_ & HALT_SWITCH) && !locked();
            resume();
            step_ = switch_halt; // S INST: one instruction (S BUS CYCLE too: see the header)
        }
    }

    ConsoleCard::Lights ConsoleCard::lights()
    {
        Lights l;
        if (halted())
        {
            for (unsigned i = 0; i < ADDRESS_LIGHTS; ++i)
                l.addr[i] = static_cast<float>(shown_.addr >> i & 1);
            for (unsigned i = 0; i < DATA_LIGHTS; ++i)
                l.data[i] = static_cast<float>(shown_.data >> i & 1);
            l.sync = (shown_.stat & L_SYNC) ? 1.0f : 0.0f;
            l.rwb = (shown_.stat & L_RWB) ? 1.0f : 0.0f;
            l.vpb = (shown_.stat & L_VPB) ? 1.0f : 0.0f;
            l.rdy = 1.0f; // the card holds RDY low
        }
        else if (lit_cycles_)
        {
            const float n = static_cast<float>(lit_cycles_);
            for (unsigned i = 0; i < ADDRESS_LIGHTS; ++i)
                l.addr[i] = static_cast<float>(addr_high_[i]) / n;
            for (unsigned i = 0; i < DATA_LIGHTS; ++i)
                l.data[i] = static_cast<float>(data_high_[i]) / n;
            l.sync = static_cast<float>(sync_high_) / n;
            l.rwb = static_cast<float>(rwb_high_) / n;
            l.vpb = static_cast<float>(vpb_high_) / n;
            l.rdy = static_cast<float>(rdy_held_) / n;
        }
        if (ctrl_ & SHOW_DISPLAY)
            for (unsigned i = 0; i < DATA_LIGHTS; ++i)
                l.data[i] = static_cast<float>(display_ >> i & 1);

        addr_high_ = {};
        data_high_ = {};
        sync_high_ = rwb_high_ = vpb_high_ = rdy_held_ = lit_cycles_ = 0;
        return l;
    }

    uint8_t ConsoleCard::read(uint32_t offset)
    {
        const uint8_t v = peek(offset);
        if (offset == KEYS)
            keys_ = 0; // keys pressed since the last read
        return v;
    }

    uint8_t ConsoleCard::peek(uint32_t offset) const
    {
        switch (offset)
        {
        case SWR:
        case SWR + 1:
        case SWR + 2:
            return byte_of(swr_, offset - SWR);
        case PANEL:
            return panel_;
        case KEYS:
            return keys_;
        case CTRL:
            return ctrl_;
        case BPADDR:
        case BPADDR + 1:
        case BPADDR + 2:
            return byte_of(bpaddr_, offset - BPADDR);
        case BPCTRL:
            return bpctrl_;
        case STATUS:
            return static_cast<uint8_t>((halted() ? HALTED : 0) | status_hit_ | (keys_ ? KEY_PRESSED : 0));
        case LADDR:
        case LADDR + 1:
        case LADDR + 2:
            return byte_of(latched_.addr, offset - LADDR);
        case LDATA:
            return latched_.data;
        case LSTAT:
            return latched_.stat;
        case PADDR:
        case PADDR + 1:
        case PADDR + 2:
            return byte_of(paddr_, offset - PADDR);
        case IEN:
            return ien_;
        case ISR:
            return isr_;
        case ID_REG:
            return ID;
        default:
            return 0xFF; // DISPLAY is write-only; the rest is unused
        }
    }

    void ConsoleCard::write(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
        case KEYS:
            apply(data); // the remote panel: LOCK does not stop it
            break;
        case DISPLAY:
            display_ = data;
            break;
        case CTRL:
            ctrl_ = static_cast<uint8_t>(data & (CTRL_HALT | LOCK | SHOW_DISPLAY));
            break;
        case BPADDR:
        case BPADDR + 1:
        case BPADDR + 2:
            set_byte(bpaddr_, offset - BPADDR, data);
            break;
        case BPCTRL:
            bpctrl_ = static_cast<uint8_t>(data & 0x1F);
            break;
        case STATUS:
            if (data & HIT)
                status_hit_ = 0;
            break;
        case PADDR:
        case PADDR + 1:
        case PADDR + 2:
            set_byte(paddr_, offset - PADDR, data);
            last_key_ = 0;
            break;
        case IEN:
            ien_ = data & 1;
            break;
        case ISR:
            isr_ = static_cast<uint8_t>(isr_ & ~(data & 1));
            break;
        default:
            break; // SWR, PANEL and the latches are read-only
        }
    }
} // namespace ooe::emu
