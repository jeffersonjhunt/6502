/**
 * @file test_pacing.cpp
 * @brief The terminal app's pacing arithmetic (apps/emu/pacing.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "pacing.h"

using namespace ooe::test;
using namespace ooe::emu;

TEST(short_runs_are_exact)
{
    CHECK_EQ(cycles_to_time(0, 1'000'000).count(), int64_t{0});
    CHECK_EQ(cycles_to_time(1, 1'000'000).count(), int64_t{1'000});
    CHECK_EQ(cycles_to_time(1'500'000, 1'000'000).count(), int64_t{1'500'000'000});
    CHECK_EQ(cycles_to_time(7'159'090, 7'159'090).count(), int64_t{1'000'000'000});
}

TEST(hours_at_the_fastest_clock_do_not_overflow)
{
    // 2e10 cycles is 46.5 minutes at 7.16 MHz: x 1e9 is past 2^64.
    constexpr uint64_t cycles = 20'000'000'000ull;
    constexpr uint32_t hz = 7'159'090;
    __extension__ typedef unsigned __int128 u128;
    const auto want = static_cast<int64_t>(static_cast<u128>(cycles) * 1'000'000'000u / hz);
    CHECK_EQ(cycles_to_time(cycles, hz).count(), want);
    // A day at 100 MHz, the most --clock takes.
    const uint64_t day = 86'400ull * 100'000'000;
    CHECK_EQ(cycles_to_time(day, 100'000'000).count(), int64_t{86'400'000'000'000});
}

int main()
{
    return run_all();
}
