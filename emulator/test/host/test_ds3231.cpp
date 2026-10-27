/**
 * @file test_ds3231.cpp
 * @brief The DS3231 real-time clock's registers, on their own.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Maxim's datasheet is the spec (ds3231.h): BCD counting and its rollovers
 * to the century, 12 h mode, both alarms in each mask mode with their
 * flags and the INT pin, the square wave's edges, the control and status
 * bits, aging and the fixed temperature. Time is CPU cycles; a small clock
 * keeps the cases quick.
 */

#include "harness.h"

#include "ds3231.h"

#include <cstdint>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t HZ = 1000; ///< a CPU at 1 kHz: a second is 1000 cycles

    using Time = Ds3231::Time;

    Ds3231 chip(const Time &t, bool lost = false)
    {
        Ds3231 c(t, lost);
        c.set_cpu_hz(HZ);
        return c;
    }

    /// Run `cycles`, an event at a time as the bridge does; @return falling edges seen.
    unsigned run(Ds3231 &c, uint64_t cycles)
    {
        unsigned edges = 0;
        while (cycles)
        {
            const uint64_t step = cycles < c.until_event() ? cycles : c.until_event();
            c.advance(step);
            cycles -= step;
            edges += c.take_edge() ? 1u : 0u;
        }
        return edges;
    }

    void seconds(Ds3231 &c, unsigned n) { run(c, uint64_t{n} * HZ); }
} // namespace

TEST(from_seconds_is_the_civil_date_and_weekday)
{
    CHECK(Ds3231::from_seconds(0) == (Time{1970, 1, 1, 0, 0, 0, 5})); // a Thursday
    // 2026-10-26 13:45:07, a Monday: 20,752 days and 49,507 s
    CHECK(Ds3231::from_seconds(int64_t{20752} * 86400 + 49507) == (Time{2026, 10, 26, 13, 45, 7, 2}));
    CHECK(Ds3231::from_seconds(int64_t{11016} * 86400) == (Time{2000, 2, 29, 0, 0, 0, 3})); // a Tuesday
    CHECK(Ds3231::from_seconds(-1) == (Time{1969, 12, 31, 23, 59, 59, 4}));
}

TEST(the_registers_are_the_time_in_bcd)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x07);
    CHECK_EQ(c.read(Ds3231::MINUTES), 0x45);
    CHECK_EQ(c.read(Ds3231::HOURS), 0x13);
    CHECK_EQ(c.read(Ds3231::DAY), 0x02);
    CHECK_EQ(c.read(Ds3231::DATE), 0x26);
    CHECK_EQ(c.read(Ds3231::MONTH), 0x10);
    CHECK_EQ(c.read(Ds3231::YEAR), 0x26);
    CHECK_EQ(c.read(Ds3231::CONTROL), Ds3231::CONTROL_POR); // $1C, as it powers up
    CHECK_EQ(c.read(Ds3231::STATUS), Ds3231::EN32KHZ);      // OSF clear: the emulator's battery is good
    CHECK_EQ(c.read(0x13), 0xFF);                            // past $12
    Ds3231 lost = chip(Time{}, true);
    CHECK_EQ(lost.read(Ds3231::STATUS), Ds3231::OSF | Ds3231::EN32KHZ);
}

TEST(a_second_is_cpu_hz_cycles)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    CHECK_EQ(c.until_event(), uint64_t{HZ});
    run(c, HZ - 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x07);
    run(c, 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x08);
    c.set_cpu_hz(0); // 7.16 MHz
    CHECK_EQ(c.until_event(), uint64_t{Ds3231::NATIVE_HZ});
}

TEST(seconds_roll_over_to_the_century)
{
    Ds3231 c = chip(Time{2099, 12, 31, 23, 59, 59, 5});
    seconds(c, 1);
    CHECK(c.time() == (Time{2100, 1, 1, 0, 0, 0, 6}));
    CHECK_EQ(c.read(Ds3231::MONTH), Ds3231::CENTURY | 0x01); // the century bit toggled
    CHECK_EQ(c.read(Ds3231::YEAR), 0x00);
    Ds3231 d = chip(Time{2199, 12, 31, 23, 59, 59, 3});
    seconds(d, 1);
    CHECK_EQ(d.read(Ds3231::MONTH), 0x01); // and back: 2200 reads as 2000
    CHECK_EQ(d.read(Ds3231::DAY), 4);
}

TEST(months_end_on_their_own_days_and_the_day_of_week_wraps)
{
    Ds3231 c = chip(Time{2026, 4, 30, 23, 59, 59, 7});
    seconds(c, 1);
    CHECK(c.time() == (Time{2026, 5, 1, 0, 0, 0, 1})); // Saturday 7 to Sunday 1
    Ds3231 d = chip(Time{2026, 1, 31, 23, 59, 59, 7});
    seconds(d, 1);
    CHECK_EQ(d.read(Ds3231::DATE), 0x01);
    CHECK_EQ(d.read(Ds3231::MONTH), 0x02);
}

TEST(leap_years_2000_and_2100_alike)
{
    Ds3231 a = chip(Time{2000, 2, 28, 23, 59, 59, 2});
    seconds(a, 1);
    CHECK(a.time() == (Time{2000, 2, 29, 0, 0, 0, 3}));
    seconds(a, 86400);
    CHECK(a.time() == (Time{2000, 3, 1, 0, 0, 0, 4}));
    Ds3231 b = chip(Time{2027, 2, 28, 23, 59, 59, 1});
    seconds(b, 1);
    CHECK(b.time() == (Time{2027, 3, 1, 0, 0, 0, 2}));
    // The chip's leap rule is every fourth year, so 2100 (not a leap year) gets a 29 February.
    Ds3231 c = chip(Time{2100, 2, 28, 23, 59, 59, 1});
    seconds(c, 1);
    CHECK_EQ(c.read(Ds3231::DATE), 0x29);
    CHECK_EQ(c.read(Ds3231::MONTH), Ds3231::CENTURY | 0x02);
}

TEST(twelve_hour_mode_counts_through_noon_and_midnight)
{
    Ds3231 c = chip(Time{2026, 10, 26, 11, 59, 59, 2});
    c.write(Ds3231::HOURS, Ds3231::HOUR_12 | 0x11); // 11 AM, 12 h mode
    CHECK_EQ(c.read(Ds3231::HOURS), Ds3231::HOUR_12 | 0x11);
    seconds(c, 1);
    CHECK_EQ(c.read(Ds3231::HOURS), Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x12); // 12 PM
    CHECK_EQ(c.time().hour, 12u);
    seconds(c, 3600);
    CHECK_EQ(c.read(Ds3231::HOURS), Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x01);
    c.write(Ds3231::HOURS, Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x11); // 11 PM
    c.write(Ds3231::MINUTES, 0x59);
    c.write(Ds3231::SECONDS, 0x59);
    seconds(c, 1);
    CHECK_EQ(c.read(Ds3231::HOURS), Ds3231::HOUR_12 | 0x12); // 12 AM
    CHECK_EQ(c.read(Ds3231::DATE), 0x27);
    c.write(Ds3231::HOURS, 0x23); // back to 24 h
    CHECK_EQ(c.read(Ds3231::HOURS), 0x23);
}

TEST(writing_the_seconds_restarts_the_second)
{
    Ds3231 c = chip(Time{2026, 10, 26, 0, 0, 0, 2});
    run(c, 600);
    c.write(Ds3231::SECONDS, 0x30);
    run(c, 999);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x30);
    run(c, 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x31);
    c.write(Ds3231::MONTH, 0x13); // out of range: rolls over at the next count
    c.write(Ds3231::DATE, 0x31);
    c.write(Ds3231::HOURS, 0x23);
    c.write(Ds3231::MINUTES, 0x59);
    c.write(Ds3231::SECONDS, 0x59);
    seconds(c, 1);
    CHECK_EQ(c.read(Ds3231::MONTH), 0x01);
    CHECK_EQ(c.read(Ds3231::YEAR), 0x27);
}

TEST(alarm_1_in_each_mask_mode)
{
    struct Mode
    {
        uint8_t regs[4];
        unsigned fires_in; ///< seconds from 2026-10-26 (Monday) 13:45:07 to the first match
    } modes[] = {
        {{0x80, 0x80, 0x80, 0x80}, 1},                       // once a second
        {{0x30, 0x80, 0x80, 0x80}, 23},                      // seconds match: :30
        {{0x30, 0x46, 0x80, 0x80}, 83},                      // minutes and seconds: 13:46:30
        {{0x30, 0x46, 0x14, 0x80}, 3600 + 83},               // hours too: 14:46:30
        {{0x30, 0x46, 0x13, 0x27}, 86400 + 83},              // date: the 27th, 13:46:30
        {{0x30, 0x46, 0x13, Ds3231::ALARM_DY | 4}, 2 * 86400 + 83}, // day: Wednesday (4)
        {{0x30, 0x46, Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x01, 0x80}, 83}, // 1 PM in 12 h form
    };
    for (const Mode &m : modes)
    {
        Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
        for (uint8_t i = 0; i < 4; ++i)
            c.write(static_cast<uint8_t>(Ds3231::ALARM1 + i), m.regs[i]);
        CHECK_EQ(c.read(Ds3231::ALARM1 + 3), m.regs[3]); // read back as written
        seconds(c, m.fires_in - 1);
        CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::A1F, 0);
        seconds(c, 1);
        CHECK_EQ(c.read(Ds3231::STATUS) & (Ds3231::A1F | Ds3231::A2F), Ds3231::A1F);
    }
}

TEST(alarm_2_matches_at_the_minute)
{
    struct Mode
    {
        uint8_t regs[3];
        unsigned fires_in; ///< from 13:45:07
    } modes[] = {
        {{0x80, 0x80, 0x80}, 53},                     // once a minute, at :00
        {{0x47, 0x80, 0x80}, 53 + 60},                // minutes: 13:47:00
        {{0x47, 0x14, 0x80}, 3600 + 53 + 60},         // hours and minutes
        {{0x47, 0x13, 0x28}, 2 * 86400 + 53 + 60},    // date: the 28th
        {{0x47, 0x13, Ds3231::ALARM_DY | 3}, 86400 + 53 + 60}, // day: Tuesday (3)
    };
    for (const Mode &m : modes)
    {
        Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
        for (uint8_t i = 0; i < 3; ++i)
            c.write(static_cast<uint8_t>(Ds3231::ALARM2 + i), m.regs[i]);
        seconds(c, m.fires_in - 1);
        CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::A2F, 0);
        seconds(c, 1);
        CHECK_EQ(c.read(Ds3231::STATUS) & (Ds3231::A1F | Ds3231::A2F), Ds3231::A2F);
    }
}

TEST(alarm_flags_clear_by_writing_0_and_int_follows_them)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    for (uint8_t i = 0; i < 4; ++i)
        c.write(static_cast<uint8_t>(Ds3231::ALARM1 + i), 0x80); // every second
    c.write(Ds3231::CONTROL, Ds3231::INTCN);                    // INT, but A1IE off
    seconds(c, 1);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::A1F); // flagged whatever A1IE says
    CHECK(!c.pin_low());
    CHECK(!c.take_edge());
    c.write(Ds3231::CONTROL, Ds3231::INTCN | Ds3231::A1IE);
    CHECK(c.pin_low()); // enabled with the flag set: INT falls at once
    CHECK(c.take_edge());
    c.write(Ds3231::STATUS, 0xFF & ~Ds3231::A1F); // a 1 leaves the others
    CHECK_EQ(c.read(Ds3231::STATUS), Ds3231::EN32KHZ);
    CHECK(!c.pin_low());
    CHECK_EQ(run(c, HZ), 1u); // and the next match pulls it low again
    CHECK(c.pin_low());
    CHECK_EQ(run(c, 5 * HZ), 0u); // held low: no more edges until it's cleared
}

TEST(the_square_wave_falls_as_each_second_starts)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    CHECK_EQ(run(c, 10 * HZ), 0u); // INTCN at power-up: no square wave
    c.write(Ds3231::CONTROL, 0x00); // 1 Hz, at :17 exactly
    CHECK(c.take_edge());           // INT was high; the wave starts low
    run(c, 250);
    CHECK(c.pin_low()); // low for the first half of the second...
    run(c, 250);
    CHECK(!c.pin_low()); // ...high for the rest
    CHECK_EQ(run(c, 3 * HZ), 3u);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x20);
    CHECK_EQ(run(c, 499), 0u);
    CHECK_EQ(run(c, 1), 1u); // falling as :21 starts
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x21);
    CHECK(c.pin_low());
    // 1.024 kHz: an edge every 32 crystal ticks, 1,024 a second
    Ds3231 f(Time{}, false);
    f.set_cpu_hz(Ds3231::OSC_HZ * 4);
    f.write(Ds3231::CONTROL, Ds3231::RS1);
    f.take_edge();
    CHECK_EQ(run(f, Ds3231::OSC_HZ * 4), 1024u);
    f.write(Ds3231::CONTROL, Ds3231::RS2 | Ds3231::RS1); // 8.192 kHz
    f.take_edge();
    CHECK_EQ(run(f, Ds3231::OSC_HZ * 4), 8192u);
}

TEST(control_and_status_bits_conv_and_bsy)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    c.write(Ds3231::CONTROL, Ds3231::EOSC | Ds3231::BBSQW | Ds3231::A2IE);
    CHECK_EQ(c.read(Ds3231::CONTROL), Ds3231::EOSC | Ds3231::BBSQW | Ds3231::A2IE);
    c.write(Ds3231::CONTROL, Ds3231::CONV);
    CHECK_EQ(c.read(Ds3231::CONTROL), Ds3231::CONV);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::BSY);
    run(c, HZ * Ds3231::CONVERSION_MS / 1000);
    CHECK_EQ(c.read(Ds3231::CONTROL), 0);
    CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::BSY, 0);
    c.write(Ds3231::STATUS, Ds3231::BSY); // read-only; EN32kHz written off
    CHECK_EQ(c.read(Ds3231::STATUS), 0);
    c.write(Ds3231::STATUS, Ds3231::EN32KHZ);
    CHECK_EQ(c.read(Ds3231::STATUS), Ds3231::EN32KHZ);
    // Its own conversion every 64 s: BSY, but not CONV
    seconds(c, 63); // 125 cycles into the 64th second since the chip was made
    run(c, HZ - 125 - 1);
    CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::BSY, 0);
    run(c, 1);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::BSY);
    CHECK_EQ(c.read(Ds3231::CONTROL) & Ds3231::CONV, 0);
}

TEST(osf_is_set_by_eosc_on_the_battery_and_cleared_by_writing_0)
{
    Ds3231 lost = chip(Time{}, true);
    lost.write(Ds3231::STATUS, Ds3231::OSF | Ds3231::EN32KHZ); // a 1 leaves it
    CHECK(lost.read(Ds3231::STATUS) & Ds3231::OSF);
    lost.write(Ds3231::STATUS, Ds3231::EN32KHZ);
    CHECK_EQ(lost.read(Ds3231::STATUS), Ds3231::EN32KHZ);
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    c.power_off();
    CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::OSF, 0); // runs on the battery
    c.write(Ds3231::CONTROL, Ds3231::EOSC);
    c.power_off();
    CHECK(c.read(Ds3231::STATUS) & Ds3231::OSF); // stopped on it
}

TEST(aging_is_kept_and_the_temperature_is_25_degrees)
{
    Ds3231 c = chip(Time{});
    CHECK_EQ(c.read(Ds3231::AGING), 0x00);
    c.write(Ds3231::AGING, 0x85); // -123: kept, though the emulated crystal needs none
    CHECK_EQ(c.read(Ds3231::AGING), 0x85);
    CHECK_EQ(c.read(Ds3231::TEMP_MSB), 0x19);
    CHECK_EQ(c.read(Ds3231::TEMP_LSB), 0x00);
    c.write(Ds3231::TEMP_MSB, 0x00); // read-only
    CHECK_EQ(c.read(Ds3231::TEMP_MSB), 0x19);
    seconds(c, 2);
    CHECK_EQ(c.read(Ds3231::AGING), 0x85);
}

TEST(set_time_restarts_the_second_and_leaves_osf)
{
    Ds3231 c = chip(Time{}, true);
    run(c, 700);
    c.set_time(Time{2030, 6, 15, 8, 0, 0, 7});
    run(c, 999);
    CHECK(c.time() == (Time{2030, 6, 15, 8, 0, 0, 7}));
    run(c, 1);
    CHECK_EQ(c.time().second, 1u);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::OSF);
}

TEST(a_clock_change_keeps_the_second_and_a_conversion_in_real_time)
{
    Ds3231 c = chip(Time{2026, 10, 26, 13, 45, 7, 2});
    run(c, HZ / 2);
    c.set_cpu_hz(4 * HZ); // half a second in: 2000 cycles of 4 kHz to go
    run(c, 2 * HZ - 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x07);
    run(c, 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x08);
    run(c, 3 * HZ);
    c.set_cpu_hz(HZ); // three quarters in: 250 cycles of 1 kHz, not the last one
    run(c, HZ / 4 - 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x08);
    run(c, 1);
    CHECK_EQ(c.read(Ds3231::SECONDS), 0x09);
    // A conversion's 125 ms too: 50 ms in, the other 75 ms at 4 kHz are 300 cycles
    c.write(Ds3231::CONTROL, Ds3231::CONV);
    run(c, 50);
    c.set_cpu_hz(4 * HZ);
    run(c, 299);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::BSY);
    run(c, 1);
    CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::BSY, 0);
}

TEST(set_time_takes_the_milliseconds_into_the_second)
{
    Ds3231 c = chip(Time{});
    c.set_time(Time{2030, 6, 15, 8, 0, 0, 7}, 600);
    run(c, 399);
    CHECK_EQ(c.time().second, 0u);
    run(c, 1);
    CHECK_EQ(c.time().second, 1u);
    // A new chip likewise, made at 7.16 MHz and then set to the CPU's clock
    Ds3231 d(Time{2030, 6, 15, 8, 0, 0, 7}, false, 900);
    d.set_cpu_hz(HZ);
    run(d, 99);
    CHECK_EQ(d.time().second, 0u);
    run(d, 1);
    CHECK_EQ(d.time().second, 1u);
}

TEST(seconds_is_from_seconds_backwards)
{
    // 2000-01-01, a leap day, the chip's 2100-02-29 being no leap day to from_seconds, 2199's last second
    for (const int64_t at : {int64_t{946684800}, int64_t{951782400}, int64_t{1793012345}, int64_t{4102444800},
                             int64_t{4107542400}, int64_t{7258118399}})
    {
        const Ds3231 c = chip(Ds3231::from_seconds(at));
        CHECK_EQ(c.seconds(), at);
    }
}

TEST(step_moves_the_count_and_keeps_the_phase_mode_and_day_numbering)
{
    Ds3231 c = chip(Time{2026, 10, 26, 23, 59, 58, 2});
    c.write(Ds3231::HOURS, Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x11); // 11 PM, in 12 h mode
    c.write(Ds3231::DAY, 5);                                          // the owner's numbering, not ours
    for (uint8_t i = 0; i < 4; ++i)
        c.write(static_cast<uint8_t>(Ds3231::ALARM1 + i), Ds3231::ALARM_MASK); // alarm 1 every second
    c.write(Ds3231::STATUS, 0x00);
    run(c, 300);
    CHECK_EQ(c.ms(), 300u);
    const uint32_t sets = c.sets();

    c.step(3); // over midnight
    CHECK(c.time() == (Time{2026, 10, 27, 0, 0, 1, 6}));
    CHECK_EQ(c.read(Ds3231::HOURS), Ds3231::HOUR_12 | 0x12); // 12 AM
    CHECK_EQ(c.ms(), 300u);
    CHECK_EQ(c.sets(), sets);                          // not a write
    CHECK_EQ(c.read(Ds3231::STATUS) & Ds3231::A1F, 0); // no second counted
    run(c, 699);
    CHECK_EQ(c.time().second, 1u);
    run(c, 1);
    CHECK_EQ(c.time().second, 2u);
    CHECK(c.read(Ds3231::STATUS) & Ds3231::A1F); // a second counted is

    c.step(-2 * 86400); // and back
    CHECK(c.time() == (Time{2026, 10, 25, 0, 0, 2, 4}));
    c.step(-3 * 86400); // the week wraps
    CHECK_EQ(c.time().day, 1u);
    c.step(-86400);
    CHECK_EQ(c.time().day, 7u);
}

TEST(sets_counts_the_times_writes)
{
    Ds3231 c = chip(Time{});
    const uint32_t made = c.sets();
    for (uint8_t r = Ds3231::SECONDS; r <= Ds3231::YEAR; ++r)
        c.write(r, c.read(r));
    CHECK_EQ(c.sets(), made + 7);
    c.write(Ds3231::ALARM1, 0);
    c.write(Ds3231::CONTROL, 0);
    c.write(Ds3231::AGING, 1);
    CHECK_EQ(c.sets(), made + 7);
    c.set_time(Time{2030, 6, 15, 8, 0, 0, 7});
    CHECK_EQ(c.sets(), made + 8);
    seconds(c, 3); // counting isn't setting
    CHECK_EQ(c.sets(), made + 8);
}

int main()
{
    return run_all();
}
