#pragma once

/**
 * @file ds3231.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The DS3231 real-time clock on the CPU card, as its I²C registers.
 * @version 0.1.0
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Real-time clock"): the north bridge owns the chip
 * (backplane_bridge.h), so nothing here is a bus device. These are the
 * chip's registers, $00-$12, as Maxim's datasheet describes them:
 *
 *   $00-$06  seconds, minutes, hours, day (1-7), date, month (bit 7 the
 *            century), year: BCD. Hours bit 6 is 12 h mode, bit 5 then PM
 *   $07-$0A  alarm 1: seconds, minutes, hours, day/date; bit 7 of each the
 *            mask bit A1M1-A1M4, bit 6 of $0A DY/DT (day, not date)
 *   $0B-$0D  alarm 2: minutes, hours, day/date (A2M2-A2M4); it matches at :00
 *   $0E      control: EOSC BBSQW CONV RS2 RS1 INTCN A2IE A1IE
 *   $0F      status: OSF . . . EN32kHz BSY A2F A1F
 *   $10      aging offset (kept; the crystal here is exact)
 *   $11-$12  temperature, always 25.00 °C ($19, $00)
 *
 * Time is CPU cycles at set_cpu_hz() (0: 7.16 MHz), so a second is that
 * many cycles of emulated time and the clock keeps the CPU's time at any
 * speed. advance() never passes an event (until_event()): a second, or an
 * edge of the square wave, so the bridge sees each one as it happens.
 *
 * The SQW/INT pin: with INTCN 0 a square wave at RS (1 Hz, 1.024, 4.096 or
 * 8.192 kHz, counted on the 32,768 Hz crystal), falling as each second
 * starts; with INTCN 1, INT, low while A1F && A1IE or A2F && A2IE.
 * take_edge() says it fell since the last call.
 *
 * As the chip: the time is kept in binary and shown in BCD, so a value
 * written out of range (a 13th month) rolls over at the next count. A year
 * divisible by 4 is a leap year, so 2100 has a 29 February: the datasheet's
 * "valid up to 2100". Writing the seconds restarts the second. Alarm flags
 * are set on a match whatever A1IE/A2IE say; OSF, A1F and A2F clear by
 * writing 0 and ignore a 1. CONV runs a conversion (BSY for CONVERSION_MS,
 * then CONV clears), and the chip runs one itself every 64 seconds.
 *
 * Power: the chip outlives the machine on its CR2032 (the session keeps it
 * across a power cycle). power_off() is the switch to the battery: with
 * EOSC set the oscillator stops there, so OSF is set. A new chip in the
 * emulator has the time it is given and OSF clear (`lost` sets it): the
 * emulator's battery never runs flat.
 */

#include <array>
#include <cstdint>

namespace ooe::emu
{
    /// A wall-clock time: years 2000-2199, day 1-7 (1 = Sunday, the convention here).
    struct RtcTime
    {
        unsigned year{2000}, month{1}, date{1}, hour{0}, minute{0}, second{0}, day{7};
        bool operator==(const RtcTime &o) const
        {
            return year == o.year && month == o.month && date == o.date && hour == o.hour && minute == o.minute &&
                   second == o.second && day == o.day;
        }
    };

    class Ds3231
    {
    public:
        static constexpr uint8_t REGISTERS = 0x13;
        enum Reg : uint8_t
        {
            SECONDS = 0x00,
            MINUTES = 0x01,
            HOURS = 0x02,
            DAY = 0x03,
            DATE = 0x04,
            MONTH = 0x05,
            YEAR = 0x06,
            ALARM1 = 0x07, ///< seconds, minutes, hours, day/date
            ALARM2 = 0x0B, ///< minutes, hours, day/date
            CONTROL = 0x0E,
            STATUS = 0x0F,
            AGING = 0x10,
            TEMP_MSB = 0x11,
            TEMP_LSB = 0x12,
        };
        // Control
        static constexpr uint8_t EOSC = 0x80, BBSQW = 0x40, CONV = 0x20, RS2 = 0x10, RS1 = 0x08, INTCN = 0x04,
                                 A2IE = 0x02, A1IE = 0x01;
        // Status
        static constexpr uint8_t OSF = 0x80, EN32KHZ = 0x08, BSY = 0x04, A2F = 0x02, A1F = 0x01;
        // Time and alarm bits
        static constexpr uint8_t HOUR_12 = 0x40, HOUR_PM = 0x20, CENTURY = 0x80, ALARM_MASK = 0x80, ALARM_DY = 0x40;
        static constexpr uint8_t CONTROL_POR = INTCN | RS2 | RS1; ///< $1C, as the chip powers up
        static constexpr uint8_t TEMPERATURE = 25;                ///< °C, $11; $12 is 0: .00

        static constexpr uint32_t NATIVE_HZ = 7'159'090;
        static constexpr uint32_t OSC_HZ = 32'768;
        static constexpr unsigned CONVERSION_MS = 125; ///< the datasheet's typical tCONV

        using Time = RtcTime;
        /// Seconds since 1970-01-01 00:00 on the wall clock (no time zone), as a Time.
        static Time from_seconds(int64_t seconds);

        explicit Ds3231(const Time &t = Time{}, bool lost = false);

        /// A register over I²C (no side effects: reading is free on the chip). $13 and up read $FF.
        uint8_t read(uint8_t reg) const;
        /// A register written over I²C. $13 and up, BSY and the temperature ignore it.
        void write(uint8_t reg, uint8_t data);

        /// Set the time as a programmer on the bench would: the second restarts; OSF stays.
        void set_time(const Time &t);
        /// The time as the registers count it, in 24 h, 2000-2199.
        Time time() const;

        /// The CPU clock advance()'s cycles run at; 0 = NATIVE_HZ.
        void set_cpu_hz(uint32_t hz);
        /// Cycles to the next second or square-wave edge (at least 1).
        uint64_t until_event() const;
        /// `cycles` (at most until_event()) of emulated time.
        void advance(uint64_t cycles);
        /// The SQW/INT pin fell since the last call.
        bool take_edge();
        /// SQW/INT is low.
        bool pin_low() const;

        /// VCC gone, the battery holding the chip: with EOSC set the oscillator stops (OSF).
        void power_off();

    private:
        void count_second();
        void check_alarms();
        bool converting() const { return now_ < conv_until_; }
        bool int_active() const;
        /// Square-wave period in crystal ticks (RS), and the cycle of its next falling edge after `phase`.
        unsigned square_period() const;
        uint64_t next_square_edge(uint64_t phase) const;
        void start_conversion();

        // The time, binary
        unsigned sec_{0}, min_{0}, hour_{0}, day_{7}, date_{1}, month_{1}, year_{0};
        bool century_{false}, mode12_{false};
        std::array<uint8_t, 7> alarm_{}; ///< $07-$0D as written
        uint8_t control_{CONTROL_POR};
        uint8_t status_{EN32KHZ}; ///< OSF, EN32kHz, A2F, A1F; BSY comes from the conversion
        uint8_t aging_{0};
        bool conv_forced_{false};

        uint32_t hz_{NATIVE_HZ};
        uint64_t phase_{0};       ///< cycles into the current second
        uint64_t now_{0};         ///< cycles since the chip was made
        uint64_t conv_until_{0};  ///< BSY until this cycle
        unsigned seconds_{0};     ///< seconds counted, for the 64-second conversion
        bool edge_{false};
    };
} // namespace ooe::emu
