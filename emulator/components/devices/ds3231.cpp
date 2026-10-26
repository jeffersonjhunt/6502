/**
 * @file ds3231.cpp
 * @brief The DS3231 real-time clock.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "ds3231.h"

#include <algorithm>

namespace ooe::emu
{
    namespace
    {
        unsigned from_bcd(uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); }
        uint8_t to_bcd(unsigned n) { return static_cast<uint8_t>((n / 10 % 10) << 4 | n % 10); }

        /// Hours in 24 h from an hours register (the time's or an alarm's), either mode.
        unsigned hour_of(uint8_t v)
        {
            if (!(v & Ds3231::HOUR_12))
                return from_bcd(v & 0x3F);
            return from_bcd(v & 0x1F) % 12 + ((v & Ds3231::HOUR_PM) ? 12u : 0u);
        }

        /// The chip's month lengths: every fourth year a leap year, 2100 too.
        unsigned days_in(unsigned month, unsigned year)
        {
            switch (month)
            {
            case 2:
                return year % 4 == 0 ? 29 : 28;
            case 4:
            case 6:
            case 9:
            case 11:
                return 30;
            default:
                return 31;
            }
        }
    } // namespace

    Ds3231::Time Ds3231::from_seconds(int64_t seconds)
    {
        // Days to a civil date: Howard Hinnant's days_from_civil, backwards.
        int64_t days = seconds / 86400;
        int64_t rest = seconds % 86400;
        if (rest < 0)
        {
            rest += 86400;
            --days;
        }
        Time t;
        t.hour = static_cast<unsigned>(rest / 3600);
        t.minute = static_cast<unsigned>(rest / 60 % 60);
        t.second = static_cast<unsigned>(rest % 60);
        t.day = static_cast<unsigned>(((days % 7 + 7) % 7 + 4) % 7 + 1); // 1970-01-01 was a Thursday, 5
        const int64_t z = days + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        t.date = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
        t.month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
        t.year = static_cast<unsigned>(yoe + era * 400 + (t.month <= 2 ? 1 : 0));
        return t;
    }

    Ds3231::Ds3231(const Time &t, bool lost)
    {
        set_time(t);
        if (lost)
            status_ |= OSF;
    }

    void Ds3231::set_time(const Time &t)
    {
        sec_ = t.second;
        min_ = t.minute;
        hour_ = t.hour;
        day_ = t.day;
        date_ = t.date;
        month_ = t.month;
        year_ = t.year % 100;
        century_ = t.year >= 2100;
        phase_ = 0;
    }

    Ds3231::Time Ds3231::time() const
    {
        return Time{2000 + (century_ ? 100u : 0u) + year_, month_, date_, hour_, min_, sec_, day_};
    }

    uint8_t Ds3231::read(uint8_t reg) const
    {
        switch (reg)
        {
        case SECONDS:
            return to_bcd(sec_);
        case MINUTES:
            return to_bcd(min_);
        case HOURS:
            if (!mode12_)
                return to_bcd(hour_);
            return static_cast<uint8_t>(HOUR_12 | (hour_ >= 12 ? HOUR_PM : 0) | to_bcd(hour_ % 12 ? hour_ % 12 : 12));
        case DAY:
            return static_cast<uint8_t>(day_ & 7);
        case DATE:
            return to_bcd(date_);
        case MONTH:
            return static_cast<uint8_t>((century_ ? CENTURY : 0) | to_bcd(month_));
        case YEAR:
            return to_bcd(year_);
        case CONTROL:
            return static_cast<uint8_t>((control_ & ~CONV) | (conv_forced_ && converting() ? CONV : 0));
        case STATUS:
            return static_cast<uint8_t>(status_ | (converting() ? BSY : 0));
        case AGING:
            return aging_;
        case TEMP_MSB:
            return TEMPERATURE;
        case TEMP_LSB:
            return 0x00;
        default:
            return reg < CONTROL ? alarm_[reg - ALARM1] : 0xFF;
        }
    }

    void Ds3231::write(uint8_t reg, uint8_t data)
    {
        const bool low = pin_low();
        switch (reg)
        {
        case SECONDS:
            sec_ = from_bcd(data & 0x7F);
            phase_ = 0; // the countdown chain restarts
            break;
        case MINUTES:
            min_ = from_bcd(data & 0x7F);
            break;
        case HOURS:
            mode12_ = (data & HOUR_12) != 0;
            hour_ = hour_of(data);
            break;
        case DAY:
            day_ = data & 7u;
            break;
        case DATE:
            date_ = from_bcd(data & 0x3F);
            break;
        case MONTH:
            month_ = from_bcd(data & 0x1F);
            century_ = (data & CENTURY) != 0;
            break;
        case YEAR:
            year_ = from_bcd(data);
            break;
        case CONTROL:
            control_ = static_cast<uint8_t>(data & ~CONV);
            if (data & CONV)
            {
                start_conversion();
                conv_forced_ = true;
            }
            break;
        case STATUS:
            // OSF, A2F, A1F: a 0 clears, a 1 leaves them. EN32kHz is plain.
            status_ = static_cast<uint8_t>((status_ & (data | ~(OSF | A2F | A1F)) & ~EN32KHZ) | (data & EN32KHZ));
            break;
        case AGING:
            aging_ = data;
            break;
        case TEMP_MSB:
        case TEMP_LSB:
            break; // read-only
        default:
            if (reg < CONTROL)
                alarm_[reg - ALARM1] = data;
            break;
        }
        if (!low && pin_low())
            edge_ = true; // INT fell: an alarm enabled with its flag already set, say
    }

    void Ds3231::set_cpu_hz(uint32_t hz)
    {
        hz_ = hz ? hz : NATIVE_HZ;
        phase_ = std::min<uint64_t>(phase_, hz_ - 1);
    }

    unsigned Ds3231::square_period() const
    {
        static constexpr unsigned PERIOD[4] = {OSC_HZ, 32, 8, 4}; // 1 Hz, 1.024, 4.096, 8.192 kHz
        return PERIOD[(control_ >> 3) & 3];
    }

    uint64_t Ds3231::next_square_edge(uint64_t phase) const
    {
        // Edge k falls at crystal tick k x period: the first cycle at or past it.
        const uint64_t p = square_period();
        uint64_t k = phase * OSC_HZ / (p * hz_) + 1;
        for (;; ++k)
        {
            const uint64_t at = (k * p * hz_ + OSC_HZ - 1) / OSC_HZ;
            if (at > phase)
                return std::min<uint64_t>(at, hz_);
        }
    }

    uint64_t Ds3231::until_event() const
    {
        if (!(control_ & INTCN) && square_period() != OSC_HZ)
            return next_square_edge(phase_) - phase_;
        return hz_ - phase_;
    }

    void Ds3231::advance(uint64_t cycles)
    {
        const bool square = !(control_ & INTCN);
        const uint64_t edge_at = square && square_period() != OSC_HZ ? next_square_edge(phase_) : hz_;
        const bool low = pin_low();
        now_ += cycles;
        phase_ += cycles;
        if (square && phase_ == edge_at)
            edge_ = true; // the second's own edge too, when edge_at is hz_
        if (phase_ >= hz_)
        {
            phase_ -= hz_;
            count_second();
            if (!square && !low && pin_low())
                edge_ = true;
        }
        if (!converting())
            conv_forced_ = false;
    }

    bool Ds3231::take_edge()
    {
        const bool e = edge_;
        edge_ = false;
        return e;
    }

    bool Ds3231::int_active() const
    {
        return ((status_ & A1F) && (control_ & A1IE)) || ((status_ & A2F) && (control_ & A2IE));
    }

    bool Ds3231::pin_low() const
    {
        if (control_ & INTCN)
            return int_active();
        const uint64_t p = square_period();
        return phase_ * OSC_HZ / hz_ % p < p / 2; // low for the first half of each period
    }

    void Ds3231::count_second()
    {
        if (++sec_ >= 60)
        {
            sec_ = 0;
            if (++min_ >= 60)
            {
                min_ = 0;
                if (++hour_ >= 24)
                {
                    hour_ = 0;
                    day_ = day_ >= 7 ? 1 : day_ + 1;
                    if (++date_ > days_in(month_, year_))
                    {
                        date_ = 1;
                        if (++month_ > 12)
                        {
                            month_ = 1;
                            if (++year_ > 99)
                            {
                                year_ = 0;
                                century_ = !century_;
                            }
                        }
                    }
                }
            }
        }
        if (++seconds_ % 64 == 0)
            start_conversion(); // the chip's own, every 64 seconds: BSY but not CONV
        check_alarms();
    }

    void Ds3231::check_alarms()
    {
        const auto field = [](uint8_t reg, unsigned value, uint8_t bits) {
            return (reg & ALARM_MASK) || from_bcd(static_cast<uint8_t>(reg & bits)) == value;
        };
        const auto hours = [this](uint8_t reg) { return (reg & ALARM_MASK) || hour_of(reg) == hour_; };
        const auto day_date = [this](uint8_t reg) {
            if (reg & ALARM_MASK)
                return true;
            return (reg & ALARM_DY) ? (reg & 0x0Fu) == day_ : from_bcd(reg & 0x3F) == date_;
        };
        if (field(alarm_[0], sec_, 0x7F) && field(alarm_[1], min_, 0x7F) && hours(alarm_[2]) && day_date(alarm_[3]))
            status_ |= A1F;
        if (sec_ == 0 && field(alarm_[4], min_, 0x7F) && hours(alarm_[5]) && day_date(alarm_[6]))
            status_ |= A2F;
    }

    void Ds3231::start_conversion()
    {
        conv_until_ = std::max(conv_until_, now_ + uint64_t{hz_} * CONVERSION_MS / 1000);
    }

    void Ds3231::power_off()
    {
        if (control_ & EOSC)
            status_ |= OSF;
    }
} // namespace ooe::emu
