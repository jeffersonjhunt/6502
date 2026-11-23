/**
 * @file sound_card3.cpp
 * @brief The sound card, revision 3: the front end (sound_card3.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_card3.h"

#include <algorithm>

namespace ooe::emu
{
    SoundCard3::SoundCard3(unsigned memsize_units) : fw_(*this, memsize_units) {}

    SoundCard3::~SoundCard3() = default;

    void SoundCard3::set_cpu_hz(uint32_t hz)
    {
        const uint32_t to = hz ? hz : NATIVE_HZ;
        phase_ = phase_ * to / host_hz_; // the part of a sample owed, rescaled
        host_hz_ = to;
    }

    void SoundCard3::tick(uint64_t cycles)
    {
        phase_ += cycles * SoundFw::RATE;
        const uint64_t samples = phase_ / host_hz_;
        phase_ %= host_hz_;
        if (samples)
        {
            fw_.advance(samples);
            take_edges();
        }
    }

    void SoundCard3::catch_up() { take_edges(); }

    void SoundCard3::take_edges()
    {
        if (fw_.take_xdone())
            isr_ |= INT_XDONE;
        if (fw_.take_cmdroom())
            isr_ |= INT_CMDROOM;
        if (fw_.take_ready())
            isr_ |= INT_READY;
    }

    uint8_t SoundCard3::status() const
    {
        unsigned s = 0;
        s |= fw_.ready() ? STATUS_READY : 0u;
        s |= fw_.cmd_ready() ? STATUS_CMDRDY : 0u;
        s |= fw_.evt_len() ? STATUS_EVT : 0u;
        s |= fw_.xfer_open() ? STATUS_XFER | STATUS_DATA : 0u;
        s |= fw_.evlost() ? STATUS_EVLOST : 0u;
        s |= fw_.err() ? STATUS_ERR : 0u;
        s |= irq() ? STATUS_IRQ : 0u;
        return static_cast<uint8_t>(s);
    }

    uint8_t SoundCard3::peek(uint32_t offset) const
    {
        switch (offset & 0x7F)
        {
        case CTRL: return ctrl_ & CTRL_DRQ;
        case STATUS: return status();
        case ERROR: return fw_.error();
        case CMDFREE: return static_cast<uint8_t>(std::min<size_t>(fw_.cmd_free(), 255));
        case EVTLEN: return static_cast<uint8_t>(std::min<size_t>(fw_.evt_len(), 255));
        case XLEFT: return static_cast<uint8_t>(fw_.xleft());
        case XLEFT + 1: return static_cast<uint8_t>(xlatch_ >> 8);
        case XLEFT + 2: return static_cast<uint8_t>(xlatch_ >> 16);
        case XTAG: return fw_.xtag();
        case IEN: return ien_;
        case ISR: return isr();
        case MEMSIZE: return fw_.memsize();
        case FEATURES: return fw_.features();
        case FWVER: return fw_.version_major();
        case FWVER + 1: return fw_.version_minor();
        case ID_REG: return ID;
        default: return 0; // EVT's next byte is a read's, which takes it
        }
    }

    uint8_t SoundCard3::read(uint32_t offset)
    {
        catch_up();
        switch (offset & 0x7F)
        {
        case EVT: return fw_.evt_read();
        case XLEFT:
            xlatch_ = fw_.xleft(); // all three, so a read across a carry isn't torn
            return static_cast<uint8_t>(xlatch_);
        default: return peek(offset);
        }
    }

    void SoundCard3::write(uint32_t offset, uint8_t data)
    {
        catch_up();
        switch (offset & 0x7F)
        {
        case CTRL:
            if (data & CTRL_RESET)
            {
                strobe_reset();
                break;
            }
            if (data & CTRL_FWRESTART)
                fw_.restart();
            ctrl_ = data & CTRL_DRQ;
            break;
        case CMD:
            fw_.cmd_write(data);
            break;
        case DATA:
            if (!fw_.data_write(data))
                ++dropped_;
            break;
        case IEN:
            ien_ = data & (INT_EVT | INT_XDONE | INT_CMDROOM | INT_READY);
            break;
        case ISR:
            isr_ = static_cast<uint8_t>(isr_ & ~(data & (INT_XDONE | INT_CMDROOM | INT_READY)));
            break;
        default:
            break;
        }
        take_edges();
    }

    void SoundCard3::dack_write(uint8_t data, bool tc)
    {
        (void)tc;
        catch_up();
        if (!fw_.data_write(data))
            ++dropped_;
        take_edges();
    }

    void SoundCard3::strobe_reset()
    {
        fw_.reset();
        ctrl_ = ien_ = isr_ = 0;
    }

    void SoundCard3::bus_reset() { strobe_reset(); }

    void SoundCard3::silence() {}
} // namespace ooe::emu
