/**
 * @file sound_card3.cpp
 * @brief The sound card, revision 3: the front end (sound_card3.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_card3.h"

#include "sid_file.h"

#include <algorithm>

namespace ooe::emu
{
    SoundCard3::SoundCard3(unsigned memsize_units) : fw_(*this, memsize_units)
    {
        watch_.card = &chips_;
        chips_.set_cpu_observer(&watch_);
        chips_.set_audio_rate(adc_rate_);
        chips_.set_chip_tap(true);
    }

    SoundCard3::~SoundCard3() = default;

    void SoundCard3::set_cpu_hz(uint32_t hz)
    {
        const uint32_t to = hz ? hz : NATIVE_HZ;
        phase_ = phase_ * to / host_hz_; // the part of a sample owed, rescaled
        host_hz_ = to;
        chips_.set_cpu_hz(hz);
    }

    void SoundCard3::set_adc_rate(uint32_t rate)
    {
        std::vector<float> before;
        adc_take(before); // what the old rate made: the firmware's, as it was
        adc_rate_ = rate;
        chips_.set_audio_rate(rate);
        chips_.set_chip_tap(rate != 0);
    }

    void SoundCard3::adc_take(std::vector<float> &out)
    {
        const std::vector<float> s = chips_.take_chip_tap();
        out.insert(out.end(), s.begin(), s.end());
        if (adc_copy_on_)
            adc_copy_.insert(adc_copy_.end(), s.begin(), s.end());
        while (chips_.audio_frames()) // the chips' own mix, unused: the firmware mixes
        {
            std::vector<float> f(2 * 1024);
            chips_.take_audio(f.data(), 1024);
        }
    }

    void SoundCard3::doc_ram(uint32_t addr, const uint8_t *bytes, size_t n)
    {
        for (size_t i = 0; i < n && addr + i < Es5503::RAM_BYTES; ++i)
            doc_.ram()[addr + i] = bytes[i];
    }

    void SoundCard3::doc_frame(int16_t out[2])
    {
        // The chip steps at its own rate (CLOCK_HZ / its clocks a sample); a frame is the average of the steps in
        // it, as revision 2's bus has the chip (each voice (byte - 128) / 128 x volume / 255 x 1/8 of full scale),
        // or the last when none fell in it.
        doc_acc_ += Es5503::CLOCK_HZ;
        const uint64_t per = uint64_t{doc_.clocks_per_sample()} * SoundFw::RATE;
        while (doc_acc_ >= per)
        {
            doc_acc_ -= per;
            doc_.step();
            const std::array<int32_t, 2> s = doc_.stereo();
            doc_sum_[0] += s[0];
            doc_sum_[1] += s[1];
            ++doc_steps_;
        }
        if (doc_steps_)
        {
            const int64_t den = int64_t{doc_steps_} * (128 * 255 * 8);
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                const int64_t num = doc_sum_[ch] * 32'768;
                const int64_t q = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
                doc_last_[ch] = static_cast<int16_t>(std::clamp<int64_t>(q, -32'768, 32'767));
            }
            doc_sum_[0] = doc_sum_[1] = 0;
            doc_steps_ = 0;
        }
        out[0] = doc_last_[0];
        out[1] = doc_last_[1];
    }

    void SoundCard3::set_audio_rate(uint32_t rate)
    {
        audio_rate_ = rate;
        out48_.clear();
        out_at_ = 0;
        out_phase_ = 0;
    }

    size_t SoundCard3::take_audio(float *out, size_t max_frames)
    {
        fw_.take_output(out48_);
        if (!audio_rate_)
        {
            out48_.clear();
            out_at_ = 0;
            return 0;
        }
        // Linear, from 48 kHz to the audio rate: frame k at k x 48,000 / rate of the card's.
        size_t n = 0;
        while (n < max_frames)
        {
            const size_t i = out_at_ + static_cast<size_t>(out_phase_ / audio_rate_);
            if (2 * (i + 1) + 1 >= out48_.size())
                break;
            const float frac = static_cast<float>(out_phase_ % audio_rate_) / static_cast<float>(audio_rate_);
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                const float a = out48_[2 * i + ch], b = out48_[2 * (i + 1) + ch];
                out[2 * n + ch] = (a + (b - a) * frac) / 32768.0f;
            }
            ++n;
            out_phase_ += SoundFw::RATE;
        }
        const size_t whole = static_cast<size_t>(out_phase_ / audio_rate_);
        out_at_ += whole;
        out_phase_ -= whole * audio_rate_;
        if (out_at_ > 4096) // the frames read past: dropped (the next may be a frame on, not come yet)
        {
            const size_t gone = std::min(out_at_, out48_.size() / 2);
            out48_.erase(out48_.begin(), out48_.begin() + static_cast<std::ptrdiff_t>(2 * gone));
            out_at_ -= gone;
        }
        return n;
    }

    void SoundCard3::run_chips(uint64_t cycles)
    {
        if (!cycles)
            return;
        chips_.tick(cycles);
        if (chips_.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
            chips_.read(SoundCard::MBIN); // the stub's frame number: the play call is counted on the 6510's bus
    }

    void SoundCard3::tick(uint64_t cycles)
    {
        // The chips to each sample the firmware acts at, then the firmware; the samples between in one step.
        while (cycles)
        {
            const uint64_t n = fw_.next_action();
            const uint64_t need = (n * host_hz_ - phase_ + SoundFw::RATE - 1) / SoundFw::RATE;
            const uint64_t step = std::min(need, cycles);
            run_chips(step);
            cycles -= step;
            phase_ += step * SoundFw::RATE;
            const uint64_t samples = phase_ / host_hz_;
            phase_ %= host_hz_;
            if (samples)
                fw_.advance(samples);
        }
        take_edges();
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
            fw_.cmd_write(data, static_cast<uint32_t>(phase_ * 1'000'000 / host_hz_)); // the byte's place in its sample
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

    // --- the port: the card's chips ------------------------------------------------------------------------

    void SoundCard3::silence()
    {
        stop_sid(chips_);
        for (unsigned o = 0; o < 32; ++o) // every oscillator halted, its mode kept
            doc_.write(static_cast<uint8_t>(0xA0 + o), static_cast<uint8_t>(doc_.peek(static_cast<uint8_t>(0xA0 + o)) | 1));
    }

    void SoundCard3::tune_start(const SidPlan &plan, bool ntsc, bool sid_b_d500)
    {
        fw_adc_flush();
        // A tune starts on chips just reset (/RES): the 6510 stopped, CIA 1, its port and both SIDs as at
        // power-on (the SIDs' envelope counters run on otherwise), whatever played before it. Its RAM stays.
        tune_two_ = !(plan.ctrl & SidPlan::CTRL_BOTH); // the plan's BOTH: a one-SID tune
        chips_.reset_for_tune(!tune_two_);
        stop_tune_chips();
        chips_.set_model(to_sid(plan.model));
        for (const auto &block : plan.blocks)
        {
            chips_.write(SoundCard::RAMADDR, static_cast<uint8_t>(block.addr));
            chips_.write(SoundCard::RAMADDR + 1, static_cast<uint8_t>(block.addr >> 8));
            for (const uint8_t byte : block.bytes)
                chips_.write(SoundCard::RAMDATA, byte);
        }
        if (adc_rate_)
            chips_.set_audio_rate(adc_rate_); // the samples afresh from the reset
        // A one-SID tune (the plan's BOTH, which revision 3 doesn't set): its reads of $D420-$D43F answer SID A, its
        // mirror, as a C64's and as revision 2's copy into SID B did; its writes keep today's decode.
        chips_.set_reads_from_a((plan.ctrl & SidPlan::CTRL_BOTH) != 0);
        chips_ctrl_ = static_cast<uint8_t>(SoundCard::CTRL_RUN | (ntsc ? SoundCard::CTRL_NTSC : 0) |
                                           (sid_b_d500 ? SoundCard::CTRL_SID_B_D500 : 0)); // never BOTH
        watch_.calls = 0;
        watch_.last_play = 0;
        chips_.write(SoundCard::CTRL, static_cast<uint8_t>(chips_ctrl_ | SoundCard::CTRL_RESET));
        ++starts_;
    }

    void SoundCard3::stop_tune_chips()
    {
        chips_.write(SoundCard::CTRL, 0); // the 6510 stopped: the SIDs are the card's
        for (unsigned reg = 0; reg < 0x19; ++reg)
        {
            chips_.chip_write(0, reg, 0);
            if (tune_two_)
                chips_.chip_write(1, reg, 0);
        }
    }

    void SoundCard3::tune_stop() { stop_tune_chips(); }

    void SoundCard3::tune_hold(bool held)
    {
        if (held)
        {
            chips_.write(SoundCard::CTRL, static_cast<uint8_t>(chips_ctrl_ & ~SoundCard::CTRL_RUN));
            for (unsigned n = 0; n < (tune_two_ ? 2u : 1u); ++n) // the tune's chips alone
            {
                held_vol_[n] = chips_.sid(n)[SoundCard::SID_MODE_VOL];
                chips_.write(static_cast<uint32_t>((n ? SoundCard::SID_B : SoundCard::SID_A) + SoundCard::SID_MODE_VOL),
                             static_cast<uint8_t>(held_vol_[n] & 0xF0));
            }
            return;
        }
        for (unsigned n = 0; n < (tune_two_ ? 2u : 1u); ++n)
            chips_.write(static_cast<uint32_t>((n ? SoundCard::SID_B : SoundCard::SID_A) + SoundCard::SID_MODE_VOL), held_vol_[n]);
        chips_.write(SoundCard::CTRL, chips_ctrl_);
    }

    bool SoundCard3::tune_jammed(uint16_t &pc)
    {
        if (chips_.cpu().state() != Cpu6502::State::Stopped)
            return false;
        pc = chips_.cpu().regs().pc;
        return true;
    }

    void SoundCard3::sid_write(unsigned chip, unsigned reg, uint8_t data)
    {
        chips_.chip_write(chip, reg, data);
    }

    void SoundCard3::sid_regs(unsigned chip, uint8_t out[25])
    {
        const auto &r = chips_.sid(chip);
        for (unsigned i = 0; i < 25; ++i)
            out[i] = r[i];
    }
} // namespace ooe::emu
