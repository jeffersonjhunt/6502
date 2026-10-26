/**
 * @file backplane_bridge.cpp
 * @brief The backplane's north bridge.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "backplane_bridge.h"

#include <algorithm>
#include <cstdio>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "northbridge";

        constexpr uint32_t WINDOW_SHIFT = 14; ///< 16 KB windows and blocks
        constexpr uint32_t WINDOW_MASK = (1u << WINDOW_SHIFT) - 1;
    } // namespace

    BackplaneBridge::BackplaneBridge(bool bootsel, std::shared_ptr<Ds3231> rtc)
        : bootsel_(bootsel), rtc_(rtc ? std::move(rtc) : std::make_shared<Ds3231>())
    {
        apply_reset();
        retime();
    }

    void BackplaneBridge::apply_reset()
    {
        const uint8_t top = bootsel_ ? BOOTSEL_TOP : RESET_TOP;
        win_ = {0, 1, 2, top};
        vec_ = top;
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
            rtc_->write(Ds3231::CONTROL, control & ~(Ds3231::INTCN | Ds3231::RS2 | Ds3231::RS1));
            rtc_->take_edge();
            read_time();
            read_status();
            latch_ = time_;
        }
        reset_seen_ = true;
        rtc_reset();
        retime();
    }

    void BackplaneBridge::rtc_reset()
    {
        tick_ = false;
        tie_ = false;
        rtc_reg_ = 0;
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
        const uint8_t reg = (n == 3 && vector_pull) ? vec_ : win_[n];
        const uint32_t offset = static_cast<uint32_t>(reg & BLOCK) << WINDOW_SHIFT | (cpu_addr & WINDOW_MASK);
        return Route{bus_address(reg & ROM ? Space::Flash : Space::Memory, offset), (reg & RO) == 0};
    }

    unsigned BackplaneBridge::wait_states(uint8_t module) const
    {
        return (wait_[(module >> 2) & 3] >> ((module & 3) * 2)) & 3u;
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
            latch_ = time_;
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
            return static_cast<uint8_t>(REVISION << 4 | (power_on_ ? STATUS_POWER_ON : 0) |
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
        case ID_REG:
            return ID;
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
            break;
        case RTCCTL:
            if (data & RTC_TICK)
                tick_ = false;
            tie_ = (data & RTC_TIE) != 0;
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
            queue(Transaction{Op::Write, rtc_reg_, data});
            break;
        case WAIT0:
        case WAIT0 + 1:
        case WAIT0 + 2:
        case WAIT0 + 3:
            wait_[offset - WAIT0] = data;
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

    void BackplaneBridge::retime() { rtc_->set_cpu_hz(apple() ? APPLE_HZ : cpu_hz_); }

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
        const uint64_t hz = apple() ? APPLE_HZ : cpu_hz_;
        return (uint64_t{bits} * hz + I2C_HZ - 1) / I2C_HZ;
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
            rtc_->advance(step);
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
                queue(Transaction{Op::Refresh});
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
            break;
        case Op::Set:
            for (unsigned i = 0; i < TIME_REGS; ++i)
                rtc_->write(static_cast<uint8_t>(i), t.time[i]);
            // OSF to 0; a 1 leaves the alarm flags; EN32kHz as last read.
            rtc_->write(Ds3231::STATUS, (status_ & Ds3231::EN32KHZ) | Ds3231::A2F | Ds3231::A1F);
            time_ = t.time;
            status_ = static_cast<uint8_t>(status_ & ~Ds3231::OSF);
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

    void BackplaneBridge::set_rtc_time(const Ds3231::Time &t)
    {
        rtc_->set_time(t);
        read_time();
    }

    uint8_t BackplaneBridge::rtcctl() const
    {
        bool set = false;
        for (const Transaction &q : queue_)
            set = set || q.op == Op::Set;
        return static_cast<uint8_t>((tick_ ? RTC_TICK : 0) | (tie_ ? RTC_TIE : 0) |
                                    (status_ & (Ds3231::A1F | Ds3231::A2F) ? RTC_ALARM : 0) |
                                    (status_ & Ds3231::OSF ? RTC_LOST : 0) | (set ? RTC_SET : 0) |
                                    (queue_.empty() ? 0 : RTC_BUSY));
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
                                        {RTC_ALARM, " ALARM"}, {RTC_LOST, " LOST"}, {RTC_SET, " SET"},
                                        {RTC_BUSY, " BUSY"}})
            if (ctl & bit)
                flags += name;
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
        return out;
    }
} // namespace ooe::emu
