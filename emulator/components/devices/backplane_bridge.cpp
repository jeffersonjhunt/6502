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

    namespace
    {
        // The two commands the bridge sends, each a constant group with its CRC.
        const std::vector<uint8_t> RANDOM_GROUP = {0x07, Atecc608::OP_RANDOM, 0x00, 0x00, 0x00, 0x24, 0xCD};
        const std::vector<uint8_t> LOCK_GROUP = {0x07, Atecc608::OP_READ, 0x00, 0x15, 0x00, 0x17, 0x5D};
        const std::vector<uint8_t> WAKE_ANSWER = {0x04, Atecc608::ST_WOKEN, 0x33, 0x43};
    } // namespace

    BackplaneBridge::BackplaneBridge(bool bootsel, std::shared_ptr<Ds3231> rtc)
        : bootsel_(bootsel), rtc_(rtc ? std::move(rtc) : std::make_shared<Ds3231>()),
          chip_(std::make_unique<Atecc608>())
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
        hole_ = 0;      // the common hole off: every reset, and the /RESET pulse
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
            // The random source's set-up, all while /RESET is held.
            if (chip_)
            {
                chip_->set_cpu_hz(hz_);
                setup_ = true;
                rng_start();
                rng_now();
            }
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
        if ((hole_ & HOLE_EN) && cpu_addr >= HOLE_BASE) // ahead of VEC and W3: their ROM, RO and WINnH don't reach it
        {
            const uint32_t at = static_cast<uint32_t>(hole_ & HOLE_BLOCK) << WINDOW_SHIFT | (cpu_addr & WINDOW_MASK);
            return Route{bus_address(Space::Memory, redirect(at)), (hole_ & HOLE_RO) == 0};
        }
        const unsigned n = cpu_addr >> WINDOW_SHIFT;
        const bool vec = n == 3 && vector_pull;
        const uint8_t reg = vec ? vec_ : win_[n];
        uint32_t offset = static_cast<uint32_t>(reg & BLOCK) << WINDOW_SHIFT | (cpu_addr & WINDOW_MASK);
        if (reg & ROM)
            return Route{bus_address(Space::Flash, offset), (reg & RO) == 0}; // $F00000 + block x $4000: WINnH ignored
        offset |= static_cast<uint32_t>(vec ? vech_ : winh_[n]) << 20; // A20-A23
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
        if (offset == MS0)
        {
            ms_latch_ = ms_;
            tbsub_latch_ = static_cast<uint16_t>((osc_ - tick_at(ticks_)) / 2);
        }
        if (offset == RNGDATA)
        {
            if (pool_.empty())
                return 0x00; // nothing taken
            const uint8_t b = pool_.front();
            pool_.pop_front();
            if (pool_.empty() && !rng_fault_ && !drawing_ && rng_locked_)
                rng_start(); // the last byte: refill
            return b;
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
            return static_cast<uint8_t>(revision() << 4 | (power_on_ ? STATUS_POWER_ON : 0) |
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
            return winh_[offset - WIN0H];
        case VECH:
            return vech_;
        case MAP:
            return map_;
        case HOLE:
            return hole_;
        case RNGST:
            return rngst();
        case RNGCNT:
            return static_cast<uint8_t>(pool_.size());
        case RNGDATA:
            return pool_.empty() ? uint8_t{0x00} : pool_.front();
        case MSSL:
            return static_cast<uint8_t>(mss_latch_);
        case MSSH:
            return static_cast<uint8_t>(mss_latch_ >> 8);
        case TBCTL:
            return tbctl();
        case MS0:
            return static_cast<uint8_t>(ms_); // live
        case MS0 + 1:
        case MS0 + 2:
        case MS0 + 3:
            return static_cast<uint8_t>(ms_latch_ >> 8 * (offset - MS0));
        case TBSUBL:
            return static_cast<uint8_t>(tbsub_latch_);
        case TBSUBH:
            return static_cast<uint8_t>(tbsub_latch_ >> 8);
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
            winh_[offset - WIN0H] = static_cast<uint8_t>(data & HIGH);
            break;
        case VECH:
            vech_ = static_cast<uint8_t>(data & HIGH);
            break;
        case MAP:
            map_ = static_cast<uint8_t>(data & MAP_AP8);
            break;
        case HOLE:
            hole_ = data;
            break;
        case RNGST:
            if (data & RNG_DRAW)
            {
                rng_fault_ = false;
                if (!drawing_ && rng_locked_)
                {
                    pool_.clear();
                    rng_start();
                }
            }
            break;
        case TBCTL:
            if (data & TB_MSF)
                msf_ = false;
            if (data & TB_OVR)
                ovr_ = false;
            msie_ = (data & TB_MSIE) != 0;
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
        if (rng_wait_)
            rng_wait_ = std::max<uint64_t>(1, (rng_wait_ * hz + hz_ - 1) / hz_);
        since_command_ = since_command_ * hz / hz_;
        if (chip_)
            chip_->set_cpu_hz(hz);
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

    uint64_t BackplaneBridge::cost(const Transaction &t) const
    {
        unsigned bits = 0;
        switch (t.op)
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
        case Op::Wake:
            return (uint64_t{WAKE_BITS} * hz_ + WAKE_HZ - 1) / WAKE_HZ;
        case Op::Check:
            bits = CHECK_BITS;
            break;
        case Op::Command:
            bits = COMMAND_BITS;
            break;
        case Op::Poll:
            bits = POLL_BITS;
            break;
        case Op::Response:
            bits = 9 * (t.reg ? t.reg - 1u : 0u) + 1; // the group's other bytes, and STOP
            break;
        case Op::Sleep:
            bits = SLEEP_BITS;
            break;
        case Op::SwReset:
            bits = RESET_BITS;
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
            left_ = cost(t);
        queue_.push_back(t);
    }

    void BackplaneBridge::tick(uint64_t cycles)
    {
        while (cycles)
        {
            uint64_t step = std::min(cycles, rtc_->until_event());
            if (!queue_.empty())
                step = std::min(step, left_);
            step = std::min(step, until_tick()); // each tick on its own cycle
            if (rng_wait_)
                step = std::min(step, rng_wait_);
            rtc_->advance(step);
            if (chip_)
                chip_->advance(step);
            count_osc(step);
            since_command_ += step;
            cycles -= step;
            // The random source's wait first: one set by the transaction finishing below starts after this step.
            const bool waited = rng_wait_ && (rng_wait_ -= step) == 0;
            if (!queue_.empty() && (left_ -= step) == 0)
            {
                const Transaction t = queue_.front();
                queue_.pop_front();
                if (!queue_.empty())
                    left_ = cost(queue_.front());
                finish(t);
            }
            if (waited)
                queue(Transaction{rng_next_});
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
        case Op::Wake:
            chip_->wake(uint64_t{WAKE_LOW_US} * hz_ / 1'000'000);
            rng_after(Op::Check, WAKE_DELAY_US);
            break;
        case Op::Check:
        {
            std::vector<uint8_t> w;
            if (chip_->address(true))
                for (unsigned i = 0; i < WAKE_ANSWER.size(); ++i)
                    w.push_back(chip_->read());
            if (w != WAKE_ANSWER)
            {
                if (setup_ && try_ == 1 && w.empty())
                {
                    // No answer at power-on: no chip, never addressed again.
                    setup_ = drawing_ = false;
                    rng_chip_ = false;
                    chip_.reset();
                    break;
                }
                rng_fail();
                break;
            }
            if (setup_)
                rng_chip_ = true;
            Transaction c{Op::Command};
            reading_lock_ = setup_ && !rng_locked_;
            c.reg = reading_lock_;
            queue(c);
            break;
        }
        case Op::Command:
            if (!chip_->address(false))
            {
                rng_fail();
                break;
            }
            chip_->write(Atecc608::WA_COMMAND, t.reg ? LOCK_GROUP : RANDOM_GROUP);
            since_command_ = 0;
            first_poll_ = true;
            rng_after(Op::Poll, POLL_US);
            break;
        case Op::Poll:
            ++counts_.polls;
            if (first_poll_)
            {
                // From the command's end to this poll's start.
                counts_.first_poll = since_command_ - cost(t);
                first_poll_ = false;
            }
            if (chip_->address(true))
            {
                resp_.assign(1, chip_->read());
                Transaction r{Op::Response};
                r.reg = resp_[0];
                queue(r);
            }
            else if (since_command_ >= uint64_t{GIVE_UP_US} * hz_ / 1'000'000)
                rng_fail(); // the give-up
            else
                rng_after(Op::Poll, POLL_US);
            break;
        case Op::Response:
        {
            for (unsigned i = 1; i < t.reg; ++i)
                resp_.push_back(chip_->read());
            const size_t want = reading_lock_ ? 7 : 3 + POOL;
            const bool whole = resp_.size() == want && resp_[0] == want &&
                               Atecc608::crc(resp_.data(), want - 2) == (resp_[want - 2] | resp_[want - 1] << 8);
            if (!whole)
            {
                rng_fail();
                break;
            }
            if (reading_lock_)
            {
                reading_lock_ = false;
                rng_locked_ = resp_[4] != Atecc608::UNLOCKED; // LockConfig, byte 87
                if (!rng_locked_)
                {
                    after_ = After::Done; // never drawn from
                    queue(Transaction{Op::Sleep});
                    break;
                }
                queue(Transaction{Op::Command}); // the first draw, on the same wake
                break;
            }
            pool_.assign(resp_.begin() + 1, resp_.begin() + 1 + POOL);
            after_ = After::Done;
            queue(Transaction{Op::Sleep});
            break;
        }
        case Op::Sleep:
            if (chip_->address(false))
                chip_->write(Atecc608::WA_SLEEP, {});
            if (after_ == After::Retry)
                queue(Transaction{Op::SwReset});
            else
            {
                if (after_ == After::Fault)
                    rng_fault_ = true;
                drawing_ = setup_ = false;
            }
            break;
        case Op::SwReset:
            queue(Transaction{Op::Wake});
            break;
        }
    }

    void BackplaneBridge::rng_start()
    {
        drawing_ = true;
        try_ = 1;
        ++counts_.draws;
        queue(Transaction{Op::Wake});
    }

    void BackplaneBridge::rng_fail()
    {
        ++counts_.failures;
        if (try_ < TRIES)
        {
            ++try_;
            after_ = After::Retry;
        }
        else
            after_ = After::Fault;
        queue(Transaction{Op::Sleep});
    }

    void BackplaneBridge::rng_after(Op op, uint32_t us)
    {
        rng_next_ = op;
        rng_wait_ = std::max<uint64_t>(1, uint64_t{us} * hz_ / 1'000'000);
    }

    void BackplaneBridge::rng_now()
    {
        // Only the set-up's transactions are queued at power-on: run them, and the waits between, at once.
        while (true)
        {
            if (!queue_.empty())
            {
                chip_ ? chip_->advance(left_) : void();
                since_command_ += left_;
                const Transaction t = queue_.front();
                queue_.pop_front();
                if (!queue_.empty())
                    left_ = cost(queue_.front());
                finish(t);
            }
            else if (rng_wait_)
            {
                if (chip_)
                    chip_->advance(rng_wait_);
                since_command_ += rng_wait_;
                rng_wait_ = 0;
                queue(Transaction{rng_next_});
            }
            else
                break;
        }
    }

    uint8_t BackplaneBridge::rngst() const
    {
        return static_cast<uint8_t>((pool_.empty() ? 0 : RNG_READY) | (rng_chip_ ? RNG_CHIP : 0) |
                                    (rng_locked_ ? RNG_LOCKED : 0) | (rng_fault_ ? RNG_FAULT : 0) |
                                    (drawing_ ? RNG_BUSY : 0));
    }

    std::string BackplaneBridge::describe_random() const
    {
        const uint8_t st = rngst();
        std::string flags;
        for (const auto &[bit, name] : {std::pair<uint8_t, const char *>{RNG_READY, " READY"}, {RNG_CHIP, " CHIP"},
                                        {RNG_LOCKED, " LOCKED"}, {RNG_FAULT, " FAULT"}, {RNG_BUSY, " BUSY"}})
            if (st & bit)
                flags += name;
        char buf[256];
        std::snprintf(buf, sizeof buf, "RNGST    $%02X%s\npool     %zu bytes  draws %u, tries failed %u\n", st,
                      flags.c_str(), pool_.size(), counts_.draws, counts_.failures);
        return buf;
    }

    void BackplaneBridge::set_rtc_time(const Ds3231::Time &t, unsigned ms)
    {
        rtc_->set_time(t, ms);
        read_time();
        r_osc_ = static_cast<int64_t>(osc_) - static_cast<int64_t>(uint64_t{std::min(ms, MSS_MAX)} * OSC_11MS / 11);
    }

    uint8_t BackplaneBridge::rtcctl() const
    {
        // SET is a strobe: a pending one shows as BUSY, so writing back what was read can't resend it. BUSY is
        // the clock's transactions alone: the random source's have RNGST's.
        const bool busy = std::any_of(queue_.begin(), queue_.end(), [](const Transaction &t) { return clock_op(t.op); });
        return static_cast<uint8_t>((tick_ ? RTC_TICK : 0) |
                                    (status_ & (Ds3231::A1F | Ds3231::A2F) ? RTC_ALARM : 0) |
                                    (status_ & Ds3231::OSF ? RTC_LOST : 0) | (busy ? RTC_BUSY : 0));
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
        for (const auto &[bit, name] : {std::pair<uint8_t, const char *>{RTC_TICK, " TICK"},
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
        out += describe_timebase() + describe_random();
        return out;
    }

    std::string BackplaneBridge::describe_timebase() const
    {
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
