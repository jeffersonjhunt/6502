/**
 * @file backplane_bridge.cpp
 * @brief The backplane's north bridge.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "backplane_bridge.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "northbridge";

        constexpr uint32_t WINDOW_SHIFT = 14; ///< 16 KB windows and blocks
        constexpr uint32_t WINDOW_MASK = (1u << WINDOW_SHIFT) - 1;
    } // namespace

    BackplaneBridge::BackplaneBridge(bool bootsel, std::shared_ptr<Ds3231> rtc, uint8_t revision)
        : revision_(revision == WIDE || revision == 3 ? revision : uint8_t{1}), bootsel_(bootsel),
          rtc_(rtc ? std::move(rtc) : std::make_shared<Ds3231>())
    {
        apply_reset();
        retime();
    }

    void BackplaneBridge::apply_reset()
    {
        const uint8_t top = bootsel_ ? BOOTSEL_TOP : RESET_TOP;
        win_ = {0, 1, 2, top};
        vec_ = top;
        winh_ = {};
        vech_ = 0;
        map_ = MAP_AP8; // whatever is fitted: old software's bank 8 is the aperture
        mode_ = 0;
        wait_ = {};
    }

    void BackplaneBridge::reset()
    {
        apply_reset(); // a hard reset clears APPLE too: the one way back to native mode
        apple_io_.reset();
        reset_wanted_ = false;
        // The machine's first reset is the power-on one; STATUS says so until the next.
        power_on_ = !reset_seen_;
        if (!reset_seen_)
        {
            // Power-on, while /RESET is held: the square wave to 1 Hz, and the time read.
            const uint8_t control = rtc_->read(Ds3231::CONTROL);
            rtc_->write(Ds3231::CONTROL, control & ~SQW_BITS);
            rtc_->take_edge();
            read_time();
            read_status();
            latch_ = time_;
            // The timebase from 0, R at the release (the power-on read's end), not in step until an edge's read.
            osc_ = frac_ = 0;
            r_osc_ = 0;
            edge_osc_ = 0;
            synced_ = false;
        }
        reset_seen_ = true;
        // A hard reset: the counter and the divider held at 0, starting at the release.
        tb_base_ = osc_;
        ticks_ = 0;
        ms_ = ms_latch_ = 0;
        tbsub_latch_ = 0;
        rtc_reset();
        retime();
        apple_io_.clock_changed(false); // native mode again (the card caught up at the old clock in its reset)
    }

    void BackplaneBridge::rtc_reset()
    {
        tick_ = false;
        tie_ = false;
        rtc_reg_ = 0;
        msf_ = ovr_ = msie_ = false; // any reset: no tick interrupts until software turns MSIE on
    }

    void BackplaneBridge::reset_pulse()
    {
        const uint8_t apple = mode_ & MODE_APPLE;
        apply_reset();
        mode_ = apple; // /RESET alone never clears it
        apple_io_.reset();
        rtc_reset();
        retime();
    }

    bool BackplaneBridge::take_reset()
    {
        const bool r = reset_wanted_;
        reset_wanted_ = false;
        return r;
    }

    Route BackplaneBridge::route(uint16_t cpu_addr, bool vector_pull, bool write) const
    {
        if (apple())
        {
            const AppleSwitches &s = apple_io_.switches();
            const uint32_t a = cpu_addr;
            if (a >= 0xC000 && a < 0xD000)
                return Route{bus_address(Space::Io, APPLE_IO + a - 0xC000), true};
            uint32_t bank = 0;
            if (a < 0x0200)
                bank = s.altzp;
            else if (a < 0xC000)
            {
                bank = write ? s.ramwrt : s.ramrd;
                if (s.store80 && ((a >= 0x0400 && a < 0x0800) || (s.hires && a >= 0x2000 && a < 0x4000)))
                    bank = s.page2;
            }
            else if (!(write ? s.lc_write : s.lc_read))
                return Route{bus_address(Space::Memory, AppleIo::ROM_BASE | a), false}; // the ROM image
            else
                bank = (a < 0xE000 && !s.lc_bank2 ? 4u : 2u) + s.altzp;
            return Route{bus_address(Space::Memory, bank << 16 | a), true};
        }
        if (cpu_addr >= IO_HOLE && cpu_addr < IO_HOLE + IO_SIZE)
            return Route{bus_address(Space::Io, cpu_addr - IO_HOLE), true};
        const unsigned n = cpu_addr >> WINDOW_SHIFT;
        const bool vec = n == 3 && vector_pull;
        const uint8_t reg = vec ? vec_ : win_[n];
        uint32_t offset = static_cast<uint32_t>(reg & BLOCK) << WINDOW_SHIFT | (cpu_addr & WINDOW_MASK);
        if (reg & ROM)
            return Route{bus_address(Space::Flash, offset), (reg & RO) == 0}; // $F00000 + block x $4000: WINnH ignored
        offset |= static_cast<uint32_t>(vec ? vech_ : winh_[n]) << 20; // A20-A23 (always 0 on revision 1)
        return Route{bus_address(Space::Memory, redirect(offset)), (reg & RO) == 0};
    }

    uint8_t BackplaneBridge::poll(bool high) const
    {
        uint8_t v = 0xFF;
        if (!decoder_)
            return v;
        for (const Mapping &m : decoder_->mappings())
        {
            if (m.irq_wired && m.module != NO_MODULE && (m.module >= 8) == high && m.device->irq())
                v = static_cast<uint8_t>(v & ~(1u << (m.module & 7)));
        }
        return v;
    }

    uint8_t BackplaneBridge::read(uint32_t offset)
    {
        if ((offset == IRQL || offset == IRQH) && decoder_)
            decoder_->sync(); // the poll sees every card as of this cycle
        if (offset == SEC)
        {
            latch_ = time_;
            mss_latch_ = static_cast<uint16_t>(mss());
        }
        if (offset == MS0 && timebase())
        {
            ms_latch_ = ms_;
            tbsub_latch_ = static_cast<uint16_t>((osc_ - tick_at(ticks_)) / 2);
        }
        return peek(offset);
    }

    uint8_t BackplaneBridge::peek(uint32_t offset) const
    {
        switch (offset)
        {
        case WIN0:
        case WIN0 + 1:
        case WIN0 + 2:
        case WIN0 + 3:
            return win_[offset - WIN0];
        case VEC:
            return vec_;
        case MODE:
            return mode_;
        case STATUS:
            return static_cast<uint8_t>(revision_ << 4 | (power_on_ ? STATUS_POWER_ON : 0) |
                                        (bootsel_ ? STATUS_BOOTSEL : 0));
        case RTCCTL:
            return rtcctl();
        case IRQL:
            return poll(false);
        case IRQH:
            return poll(true);
        case WAIT0:
        case WAIT0 + 1:
        case WAIT0 + 2:
        case WAIT0 + 3:
            return wait_[offset - WAIT0];
        case SEC:
            return time_[0];
        case SEC + 1:
        case SEC + 2:
        case SEC + 3:
        case SEC + 4:
        case SEC + 5:
        case SEC + 6:
            return latch_[offset - SEC];
        case RTCREG:
            return rtc_reg_;
        case RTCDATA:
            return rtc_data_;
        case WIN0H:
        case WIN0H + 1:
        case WIN0H + 2:
        case WIN0H + 3:
            return wide() ? winh_[offset - WIN0H] : 0xFF;
        case VECH:
            return wide() ? vech_ : 0xFF;
        case MAP:
            return wide() ? map_ : 0xFF;
        case MSSL:
            return timebase() ? static_cast<uint8_t>(mss_latch_) : uint8_t{0xFF};
        case MSSH:
            return timebase() ? static_cast<uint8_t>(mss_latch_ >> 8) : uint8_t{0xFF};
        case TBCTL:
            return timebase() ? tbctl() : uint8_t{0xFF};
        case MS0:
            return timebase() ? static_cast<uint8_t>(ms_) : uint8_t{0xFF}; // live
        case MS0 + 1:
        case MS0 + 2:
        case MS0 + 3:
            return timebase() ? static_cast<uint8_t>(ms_latch_ >> 8 * (offset - MS0)) : uint8_t{0xFF};
        case TBSUBL:
            return timebase() ? static_cast<uint8_t>(tbsub_latch_) : uint8_t{0xFF};
        case TBSUBH:
            return timebase() ? static_cast<uint8_t>(tbsub_latch_ >> 8) : uint8_t{0xFF};
        case ID_REG:
            return id();
        default:
            return 0xFF;
        }
    }

    void BackplaneBridge::write(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
        case WIN0:
        case WIN0 + 1:
        case WIN0 + 2:
        case WIN0 + 3:
            win_[offset - WIN0] = data;
            break;
        case VEC:
            vec_ = data;
            break;
        case MODE:
            if ((data & MODE_APPLE) && !apple())
            {
                EMU_LOGI(TAG, "MODE: Apple II mode");
                reset_wanted_ = true; // the switch pulses /RESET
            }
            mode_ = static_cast<uint8_t>((data & (MODE_APPLE | MODE_TURBO)) | (mode_ & MODE_APPLE)); // APPLE only sets
            retime();
            apple_io_.clock_changed(apple()); // the Disk II caught up at the old clock, then counting by the new
            break;
        case RTCCTL:
            if (data & RTC_TICK)
                tick_ = false;
            tie_ = !timebase() && (data & RTC_TIE) != 0; // revision 3: TIE reads 0, the write ignored
            if (data & RTC_SET)
            {
                Transaction t{Op::Set};
                t.time = staged_;
                queue(t);
            }
            break;
        case SEC:
        case SEC + 1:
        case SEC + 2:
        case SEC + 3:
        case SEC + 4:
        case SEC + 5:
        case SEC + 6:
            staged_[offset - SEC] = data;
            break;
        case RTCREG:
            rtc_reg_ = data;
            queue(Transaction{Op::Read, data});
            break;
        case RTCDATA:
            if (rtc_reg_ == Ds3231::CONTROL)
                data &= static_cast<uint8_t>(~SQW_BITS); // SQW is the bridge's
            queue(Transaction{Op::Write, rtc_reg_, data});
            break;
        case WAIT0:
        case WAIT0 + 1:
        case WAIT0 + 2:
        case WAIT0 + 3:
            wait_[offset - WAIT0] = data;
            break;
        case WIN0H:
        case WIN0H + 1:
        case WIN0H + 2:
        case WIN0H + 3:
            if (wide())
                winh_[offset - WIN0H] = static_cast<uint8_t>(data & HIGH);
            break;
        case VECH:
            if (wide())
                vech_ = static_cast<uint8_t>(data & HIGH);
            break;
        case MAP:
            if (wide())
                map_ = static_cast<uint8_t>(data & MAP_AP8);
            break;
        case TBCTL:
            if (timebase())
            {
                if (data & TB_MSF)
                    msf_ = false;
                if (data & TB_OVR)
                    ovr_ = false;
                msie_ = (data & TB_MSIE) != 0;
            }
            break;
        default:
            break; // read-only or unused
        }
    }

    // The real-time clock

    void BackplaneBridge::set_cpu_hz(uint32_t hz)
    {
        cpu_hz_ = hz ? hz : NATIVE_HZ;
        retime();
    }

    void BackplaneBridge::retime()
    {
        // What's left of the transaction under way takes the same time at the new clock, as a DS3231 conversion does.
        const uint32_t hz = apple() ? APPLE_HZ : cpu_hz_;
        if (!queue_.empty())
            left_ = (left_ * hz + hz_ - 1) / hz_;
        hz_ = hz;
        rtc_->set_cpu_hz(hz);
        osc_rate();
    }

    void BackplaneBridge::osc_rate()
    {
        // OSC is 2 x NATIVE_HZ by definition; Apple II mode's cycle is 14 OSC clocks, every 65th 16: 912 in 65.
        uint64_t num = 2, den = 1;
        if (apple())
            num = 912, den = 65;
        else if (hz_ != NATIVE_HZ)
            num = 2ull * NATIVE_HZ, den = hz_;
        const uint64_t g = std::gcd(num, den);
        num /= g;
        den /= g;
        frac_ = frac_ * den / den_; // the same fraction of an OSC clock
        num_ = num;
        den_ = den;
    }

    uint64_t BackplaneBridge::until_tick() const
    {
        // A tick at OSC clock T lands in the cycle holding T and is seen from the cycle after: osc_ > T.
        if (osc_ > tick_at(ticks_ + 1))
            return 1; // (count_osc() takes every tick it reaches: never so)
        const uint64_t need = (tick_at(ticks_ + 1) + 1 - osc_) * den_ - frac_; // den_ths of OSC to go (> 0)
        return (need + num_ - 1) / num_;
    }

    void BackplaneBridge::count_osc(uint64_t cycles)
    {
        const uint64_t total = frac_ + cycles * num_;
        osc_ += total / den_;
        frac_ = total % den_;
        if (!timebase())
            return;
        while (osc_ > tick_at(ticks_ + 1))
        {
            ++ticks_;
            ++ms_;
            if (msf_)
                ovr_ = true; // one interrupt for two ticks: the counter has both
            msf_ = true;
        }
    }

    unsigned BackplaneBridge::mss() const
    {
        const int64_t d = static_cast<int64_t>(osc_) - r_osc_;
        if (d <= 0)
            return 0;
        // The largest j with floor(157,500 j / 11) <= d: MSS's own divider, from R.
        const uint64_t j = (11 * static_cast<uint64_t>(d) + 10) / OSC_11MS;
        return static_cast<unsigned>(std::min<uint64_t>(j, MSS_MAX));
    }

    uint8_t BackplaneBridge::tbctl() const
    {
        const bool sync = synced_ && osc_ - edge_osc_ < SYNC_OSC;
        return static_cast<uint8_t>((msf_ ? TB_MSF : 0) | (ovr_ ? TB_OVR : 0) | (msie_ ? TB_MSIE : 0) |
                                    (sync ? TB_SYNC : 0));
    }

    uint64_t BackplaneBridge::cost(Op op) const
    {
        unsigned bits = 0;
        switch (op)
        {
        case Op::Refresh:
            bits = REFRESH_BITS;
            break;
        case Op::Set:
            bits = SET_BITS;
            break;
        case Op::Read:
            bits = READ_BITS + 9;
            break;
        case Op::Write:
            bits = WRITE_BITS + 9 + READ_BITS + 9;
            break;
        }
        return (uint64_t{bits} * hz_ + I2C_HZ - 1) / I2C_HZ;
    }

    void BackplaneBridge::queue(const Transaction &t)
    {
        if (t.op == Op::Refresh)
            for (const Transaction &q : queue_)
                if (q.op == Op::Refresh)
                    return; // that edge is missed
        if (queue_.empty())
            left_ = cost(t.op);
        queue_.push_back(t);
    }

    void BackplaneBridge::tick(uint64_t cycles)
    {
        while (cycles)
        {
            uint64_t step = std::min(cycles, rtc_->until_event());
            if (!queue_.empty())
                step = std::min(step, left_);
            if (timebase())
                step = std::min(step, until_tick()); // each tick on its own cycle
            rtc_->advance(step);
            count_osc(step);
            cycles -= step;
            if (!queue_.empty() && (left_ -= step) == 0)
            {
                const Transaction t = queue_.front();
                queue_.pop_front();
                if (!queue_.empty())
                    left_ = cost(queue_.front().op);
                finish(t);
            }
            if (rtc_->take_edge())
            {
                edge_osc_ = osc_;
                Transaction t{Op::Refresh};
                t.osc = osc_;
                queue(t);
            }
        }
    }

    void BackplaneBridge::read_time()
    {
        for (unsigned i = 0; i < TIME_REGS; ++i)
            time_[i] = rtc_->read(static_cast<uint8_t>(i));
    }

    void BackplaneBridge::read_status() { status_ = rtc_->read(Ds3231::STATUS); }

    void BackplaneBridge::finish(const Transaction &t)
    {
        switch (t.op)
        {
        case Op::Refresh:
            read_time();
            read_status();
            tick_ = true;
            r_osc_ = static_cast<int64_t>(t.osc); // SEC-YEAR and MSS change together: MSS from the new edge
            synced_ = true;
            break;
        case Op::Set:
            for (unsigned i = 0; i < TIME_REGS; ++i)
                rtc_->write(static_cast<uint8_t>(i), t.time[i]);
            // OSF to 0; a 1 leaves the alarm flags; EN32kHz as last read.
            rtc_->write(Ds3231::STATUS, (status_ & Ds3231::EN32KHZ) | Ds3231::A2F | Ds3231::A1F);
            time_ = t.time;
            status_ = static_cast<uint8_t>(status_ & ~Ds3231::OSF);
            r_osc_ = static_cast<int64_t>(osc_); // the chip's second restarted: MSS from 0 with the time sent
            break;
        case Op::Read:
            rtc_data_ = rtc_->read(t.reg);
            if (t.reg == Ds3231::STATUS)
                status_ = rtc_data_;
            break;
        case Op::Write:
            rtc_->write(t.reg, t.data);
            rtc_data_ = rtc_->read(t.reg);
            if (t.reg == Ds3231::STATUS)
                status_ = rtc_data_;
            break;
        }
    }

    void BackplaneBridge::set_rtc_time(const Ds3231::Time &t, unsigned ms)
    {
        rtc_->set_time(t, ms);
        read_time();
        r_osc_ = static_cast<int64_t>(osc_) - static_cast<int64_t>(uint64_t{std::min(ms, MSS_MAX)} * OSC_11MS / 11);
    }

    uint8_t BackplaneBridge::rtcctl() const
    {
        // SET is a strobe: a pending one shows as BUSY, so writing back what was read can't resend it.
        return static_cast<uint8_t>((tick_ ? RTC_TICK : 0) | (tie_ ? RTC_TIE : 0) |
                                    (status_ & (Ds3231::A1F | Ds3231::A2F) ? RTC_ALARM : 0) |
                                    (status_ & Ds3231::OSF ? RTC_LOST : 0) | (queue_.empty() ? 0 : RTC_BUSY));
    }

    bool BackplaneBridge::set_pending() const
    {
        return std::any_of(queue_.begin(), queue_.end(), [](const Transaction &t) { return t.op == Op::Set; });
    }

    std::string BackplaneBridge::describe_clock() const
    {
        const auto bcd = [](uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); };
        const std::array<uint8_t, TIME_REGS> &t = time_;
        const bool h12 = (t[2] & Ds3231::HOUR_12) != 0;
        const unsigned hour = h12 ? bcd(t[2] & 0x1F) % 12 + ((t[2] & Ds3231::HOUR_PM) ? 12u : 0u) : bcd(t[2] & 0x3F);
        const unsigned year = 2000 + ((t[5] & Ds3231::CENTURY) ? 100u : 0u) + bcd(t[6]);
        char buf[512];
        std::string out;
        std::snprintf(buf, sizeof buf, "time     %04u-%02u-%02u %02u:%02u:%02u  day %u%s\n", year, bcd(t[5] & 0x1F),
                      bcd(t[4] & 0x3F), hour, bcd(t[1] & 0x7F), bcd(t[0] & 0x7F), t[3] & 7u, h12 ? "  (12 h)" : "");
        out += buf;
        const uint8_t ctl = rtcctl();
        std::string flags;
        for (const auto &[bit, name] : {std::pair<uint8_t, const char *>{RTC_TICK, " TICK"}, {RTC_TIE, " TIE"},
                                        {RTC_ALARM, " ALARM"}, {RTC_LOST, " LOST"}, {RTC_BUSY, " BUSY"}})
            if (ctl & bit)
                flags += name;
        if (set_pending())
            flags += " (SET)"; // SET reads 0: the queue says it's what BUSY waits on
        std::snprintf(buf, sizeof buf, "RTCCTL   $%02X%s\n", ctl, flags.c_str());
        out += buf;
        const Ds3231 &r = *rtc_;
        const auto reg = [&r](unsigned n) { return static_cast<unsigned>(r.read(static_cast<uint8_t>(n))); };
        std::snprintf(buf, sizeof buf,
                      "control  $%02X  status $%02X  aging $%02X  temperature %u.%02u C\n"
                      "alarm 1  $%02X $%02X $%02X $%02X  alarm 2  $%02X $%02X $%02X\n",
                      reg(Ds3231::CONTROL), reg(Ds3231::STATUS), reg(Ds3231::AGING), reg(Ds3231::TEMP_MSB),
                      (reg(Ds3231::TEMP_LSB) >> 6) * 25, reg(7), reg(8), reg(9), reg(10), reg(11), reg(12), reg(13));
        out += buf;
        if (timebase())
            out += describe_timebase();
        return out;
    }

    std::string BackplaneBridge::describe_timebase() const
    {
        if (!timebase())
            return "no timebase: the bridge is revision " + std::to_string(revision_) + "\n";
        const uint8_t ctl = tbctl();
        std::string flags;
        for (const auto &[bit, name] : {std::pair<uint8_t, const char *>{TB_MSF, " MSF"}, {TB_OVR, " OVR"},
                                        {TB_MSIE, " MSIE"}, {TB_SYNC, " SYNC"}})
            if (ctl & bit)
                flags += name;
        char buf[256];
        std::snprintf(buf, sizeof buf, "MS       %u ($%08X)  %u.%03u s\nTBCTL    $%02X%s\nMSS      %u%s\n", ms_, ms_,
                      ms_ / 1000, ms_ % 1000, ctl, flags.c_str(), mss(),
                      ctl & TB_SYNC ? "" : "  (not in step with the DS3231)");
        return buf;
    }
} // namespace ooe::emu
