/**
 * @file sound_card.cpp
 * @brief The backplane's sound card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_card.h"

#include <algorithm>
#include <cmath>

namespace ooe::emu
{
    void SoundCard::reset()
    {
        catch_up();
        chips_[0].reset();
        chips_[1].reset();
        sids_at_ = clock_;
        ctrl_ = ien_ = 0;
        ram_addr_ = 0;
        to_card_ = to_host_ = 0;
        to_card_full_ = to_host_full_ = false;
        ddr_ = port_ = 0;
        cia_reset();
        phase_ = 0;
        owed_ = 0;
        tune_clocks();
    }

    void SoundCard::reset_cpu()
    {
        ++cpu_resets_;
        ddr_ = port_ = 0;
        cia_reset();
        cpu_.reset();
        owed_ = 0;
    }

    // --- time ---------------------------------------------------------------

    void SoundCard::tick(uint64_t cycles)
    {
        // SID clocks = host cycles x SID Hz / host Hz, the remainder carried.
        phase_ += cycles * sid_hz();
        const uint64_t due = phase_ / host_hz_;
        phase_ %= host_hz_;
        if (!(ctrl_ & CTRL_RUN))
        {
            clock_ += due; // the SIDs' clock runs on; they catch up when next touched
            return;
        }
        owed_ += static_cast<int64_t>(due);
        while (owed_ > 0)
        {
            step_start_ = cpu_.cycles();
            in_step_ = true;
            cia_tick(cpu_.step());
            if (cia_irq())
            {
                const uint64_t entry = cpu_.cycles();
                cpu_.irq(); // ignored while I is set; level-sensitive, so it comes back
                cia_tick(static_cast<unsigned>(cpu_.cycles() - entry)); // its 7 cycles are SID clocks too
            }
            in_step_ = false;
            const uint64_t n = cpu_.cycles() - step_start_;
            clock_ += n;
            owed_ -= static_cast<int64_t>(n);
        }
    }

    uint64_t SoundCard::now() const
    {
        // Mid-instruction, the 6510's accesses so far: a SID write lands on its own cycle.
        return clock_ + (in_step_ ? cpu_.cycles() - step_start_ : 0);
    }

    void SoundCard::catch_up() const
    {
        const uint64_t t = now();
        if (t <= sids_at_)
            return;
        if (audio_rate_)
            render(t - sids_at_);
        else
        {
            chips_[0].clock(t - sids_at_);
            chips_[1].clock(t - sids_at_);
        }
        sids_at_ = t;
    }

    // --- audio ------------------------------------------------------------------

    void SoundCard::set_audio_rate(uint32_t rate)
    {
        catch_up(); // what went before, at the old rate (or none)
        audio_rate_ = rate;
        audio_.clear();
        sum_l_ = sum_r_ = 0;
        sample_clocks_ = sample_length_ = sample_phase_ = 0;
        hp_in_ = hp_out_ = {};
        hp_pole_ = rate ? std::exp(-2.0f * 3.14159265f * 16.0f / static_cast<float>(rate)) : 0.0f;
    }

    void SoundCard::set_model(Sid::Model m)
    {
        catch_up();
        chips_[0].set_model(m);
        chips_[1].set_model(m);
    }

    void SoundCard::tune_clocks()
    {
        chips_[0].set_clock_hz(sid_hz());
        chips_[1].set_clock_hz(sid_hz());
    }

    void SoundCard::render(uint64_t clocks) const
    {
        while (clocks)
        {
            if (!sample_length_)
            {
                // Each sample spans SID Hz / rate clocks, the remainder carried: 20 or 21 for PAL at 48 kHz.
                sample_phase_ += sid_hz();
                sample_length_ = sample_phase_ / audio_rate_;
                sample_phase_ %= audio_rate_;
                if (!sample_length_)
                    continue; // a rate above the SID clock: this sample spans none
            }
            const uint64_t n = std::min(clocks, sample_length_ - sample_clocks_);
            sum_l_ += chips_[0].clock_audio(n);
            sum_r_ += chips_[1].clock_audio(n);
            sample_clocks_ += n;
            clocks -= n;
            if (sample_clocks_ < sample_length_)
                continue;
            const float in[2] = {static_cast<float>(sum_l_ / static_cast<double>(sample_clocks_)),
                                 static_cast<float>(sum_r_ / static_cast<double>(sample_clocks_))};
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                // the output capacitor: a one-pole high-pass, y = x - x' + p y'
                hp_out_[ch] = in[ch] - hp_in_[ch] + hp_pole_ * hp_out_[ch];
                hp_in_[ch] = in[ch];
                audio_.push_back(std::fmax(-1.0f, std::fmin(1.0f, hp_out_[ch] / 8.0f))); // headroom: of 192 tunes, one clips, 12 samples
            }
            sum_l_ = sum_r_ = 0;
            sample_clocks_ = sample_length_ = 0;
        }
        const size_t most = 2 * static_cast<size_t>(audio_rate_ / 2); // half a second
        if (audio_.size() > most)
            audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(audio_.size() - most));
    }

    size_t SoundCard::take_audio(float *out, size_t max_frames)
    {
        catch_up();
        const size_t frames = std::min(max_frames, audio_.size() / 2);
        std::copy(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(2 * frames), out);
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(2 * frames));
        return frames;
    }

    uint8_t SoundCard::read(uint32_t offset)
    {
        switch (offset & 0x7F)
        {
        case RAMDATA:
            return ram_byte();
        case MBIN:
            to_host_full_ = false;
            return to_host_;
        default:
            return peek(offset);
        }
    }

    uint8_t SoundCard::peek(uint32_t offset) const
    {
        const unsigned o = offset & 0x7F;
        if (o < SID_A + SID_REGS)
            return sid_read(0, o - SID_A);
        if (o >= SID_B && o < SID_B + SID_REGS)
            return sid_read(1, o - SID_B);
        switch (o)
        {
        case CTRL:
            return ctrl_;
        case STATUS:
            return static_cast<uint8_t>((to_card_full_ ? STATUS_TO_CARD : 0) | (to_host_full_ ? STATUS_TO_HOST : 0) |
                                        (irq() ? STATUS_IRQ : 0));
        case IEN:
            return ien_;
        case RAMADDR:
            return static_cast<uint8_t>(ram_addr_);
        case RAMADDR + 1:
            return static_cast<uint8_t>(ram_addr_ >> 8);
        case RAMDATA:
            return ram_[ram_addr_];
        case MBIN:
            return to_host_;
        case ID_REG:
            return ID;
        default:
            return 0xFF;
        }
    }

    void SoundCard::write(uint32_t offset, uint8_t data)
    {
        const unsigned o = offset & 0x7F;
        if (o < SID_A + SID_REGS || (o >= SID_B && o < SID_B + SID_REGS))
        {
            if (!(ctrl_ & CTRL_RUN)) // the card's 6510 owns the SIDs while it runs
                sid_write(o >= SID_B ? 1 : 0, o & 0x1F, data);
            return;
        }
        switch (o)
        {
        case CTRL:
            if ((data ^ ctrl_) & CTRL_NTSC)
            {
                catch_up(); // up to now on the old crystal
                phase_ = 0; // a new crystal: the remainder was in the old one's clocks
            }
            ctrl_ = static_cast<uint8_t>(data & ~CTRL_RESET); // RESET is a strobe
            tune_clocks();
            if (data & CTRL_RESET)
                reset_cpu();
            break;
        case IEN:
            ien_ = static_cast<uint8_t>(data & IEN_MAILBOX);
            break;
        case RAMADDR:
            ram_addr_ = static_cast<uint16_t>((ram_addr_ & 0xFF00) | data);
            break;
        case RAMADDR + 1:
            ram_addr_ = static_cast<uint16_t>((ram_addr_ & 0x00FF) | data << 8);
            break;
        case RAMDATA:
            ram_[ram_addr_++] = data;
            break;
        case MBOUT:
            to_card_ = data;
            to_card_full_ = true;
            break;
        default:
            break;
        }
    }

    uint8_t SoundCard::dack_read(bool) { return ram_byte(); }

    void SoundCard::dack_write(uint8_t data, bool) { ram_[ram_addr_++] = data; }

    int SoundCard::card_receive()
    {
        if (!to_card_full_)
            return -1;
        to_card_full_ = false;
        return to_card_;
    }

    void SoundCard::card_send(uint8_t byte)
    {
        to_host_ = byte;
        to_host_full_ = true;
    }

    uint8_t SoundCard::card_status() const
    {
        return static_cast<uint8_t>((to_card_full_ ? CARD_FROM_HOST : 0) | (to_host_full_ ? CARD_UNTAKEN : 0));
    }

    void SoundCard::sid_write(unsigned chip, unsigned reg, uint8_t data)
    {
        catch_up(); // the write lands on its clock
        chips_[chip & 1].write(reg, data);
        if ((chip & 1) == 0 && (ctrl_ & CTRL_BOTH))
            chips_[1].write(reg, data);
    }

    uint8_t SoundCard::sid_read(unsigned chip, unsigned reg) const
    {
        catch_up();
        return chips_[chip & 1].read(reg);
    }

    // --- the card's 6510 and its map -------------------------------------------

    bool SoundCard::io_in() const
    {
        const unsigned lines = (port_ | ~static_cast<unsigned>(ddr_)) & 0x07u; // inputs are pulled high
        return (lines & 0x04) && (lines & 0x03);
    }

    unsigned SoundCard::sid_at(uint16_t addr) const
    {
        const uint16_t b = ctrl_ & CTRL_SID_B_D500 ? 0xD500 : 0xD420;
        return addr >= b && addr < b + 0x20 ? 1 : 0;
    }

    uint8_t SoundCard::card_peek(uint16_t addr) const
    {
        if (addr == 0x0000)
            return ddr_;
        if (addr == 0x0001) // inputs: bits 0-5 pulled high, 6-7 unconnected, holding what was written
            return static_cast<uint8_t>((port_ & ddr_) | (~ddr_ & (0x3F | (port_ & 0xC0))));
        if (addr < 0xD000 || addr > 0xDFFF || !io_in())
            return ram_[addr];
        if (addr >= 0xD400 && addr < 0xD800)
            return sid_read(sid_at(addr), addr & 0x1F);
        if (addr >= 0xDC00 && addr < 0xDD00)
        {
            switch (addr & 0x0F)
            {
            case CIA_TA_LO: return static_cast<uint8_t>(cia_counter_);
            case CIA_TA_HI: return static_cast<uint8_t>(cia_counter_ >> 8);
            case CIA_ICR: return static_cast<uint8_t>(cia_flags_ | (cia_irq() ? 0x80 : 0));
            case CIA_CRA: return cia_cra_;
            default: return 0xFF;
            }
        }
        if (addr == 0xDE00)
            return to_card_;
        if (addr == 0xDE01)
            return card_status();
        return 0xFF;
    }

    uint8_t SoundCard::card_read(uint16_t addr)
    {
        if (io_in() && addr >= 0xDC00 && addr < 0xDD00)
            return cia_read(addr & 0x0F);
        if (io_in() && addr == 0xDE00)
        {
            const int b = card_receive();
            return b < 0 ? to_card_ : static_cast<uint8_t>(b); // empty: the last byte, again
        }
        return card_peek(addr);
    }

    void SoundCard::card_write(uint16_t addr, uint8_t data)
    {
        if (addr == 0x0000)
            ddr_ = data;
        else if (addr == 0x0001)
            port_ = data;
        if (addr < 0xD000 || addr > 0xDFFF || !io_in())
        {
            ram_[addr] = data; // $00 and $01 too
            return;
        }
        if (addr >= 0xD400 && addr < 0xD800)
            sid_write(sid_at(addr), addr & 0x1F, data);
        else if (addr >= 0xDC00 && addr < 0xDD00)
            cia_write(addr & 0x0F, data);
        else if (addr == 0xDE00)
            card_send(data);
    }

    // --- CIA 1, as far as tunes need it -------------------------------------------

    void SoundCard::cia_reset()
    {
        cia_latch_ = cia_counter_ = 0xFFFF;
        cia_cra_ = cia_flags_ = cia_mask_ = 0;
    }

    void SoundCard::cia_tick(unsigned cycles)
    {
        if (!(cia_cra_ & CRA_START))
            return;
        while (cycles)
        {
            if (cia_counter_ >= cycles)
            {
                cia_counter_ = static_cast<uint16_t>(cia_counter_ - cycles);
                return;
            }
            // 0, then one more clock to underflow: reload and flag it
            cycles -= cia_counter_ + 1u;
            cia_counter_ = cia_latch_;
            cia_flags_ |= 0x01;
            if (cia_cra_ & CRA_ONE_SHOT)
            {
                cia_cra_ = static_cast<uint8_t>(cia_cra_ & ~CRA_START);
                return;
            }
        }
    }

    uint8_t SoundCard::cia_read(unsigned reg)
    {
        if (reg != CIA_ICR)
            return card_peek(static_cast<uint16_t>(0xDC00 + reg));
        const uint8_t v = static_cast<uint8_t>(cia_flags_ | (cia_irq() ? 0x80 : 0));
        cia_flags_ = 0; // reading ICR acknowledges
        return v;
    }

    void SoundCard::cia_write(unsigned reg, uint8_t data)
    {
        switch (reg)
        {
        case CIA_TA_LO:
            cia_latch_ = static_cast<uint16_t>((cia_latch_ & 0xFF00) | data);
            break;
        case CIA_TA_HI:
            cia_latch_ = static_cast<uint16_t>((cia_latch_ & 0x00FF) | data << 8);
            if (!(cia_cra_ & CRA_START))
                cia_counter_ = cia_latch_;
            break;
        case CIA_ICR:
            if (data & 0x80)
                cia_mask_ = static_cast<uint8_t>(cia_mask_ | (data & 0x1F));
            else
                cia_mask_ = static_cast<uint8_t>(cia_mask_ & ~data);
            break;
        case CIA_CRA:
            if (data & CRA_LOAD)
                cia_counter_ = cia_latch_;
            cia_cra_ = static_cast<uint8_t>(data & ~CRA_LOAD); // LOAD is a strobe
            break;
        default:
            break;
        }
    }
} // namespace ooe::emu
