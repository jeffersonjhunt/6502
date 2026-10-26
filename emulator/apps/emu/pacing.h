#pragma once

/**
 * @file pacing.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief When emulated time reaches a cycle count, in wall time.
 * @version 0.1.0
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Split out of the run loop so the arithmetic is testable: cycles x 1e9
 * overflows 64 bits after 1.8e10 cycles, 43 minutes at 7.16 MHz, after
 * which an unpaced machine runs flat out for good.
 */

#include <chrono>
#include <cstdint>

namespace ooe::emu
{
    /// How long `cycles` take at `hz` (not 0): whole seconds and the rest
    /// apart, so nothing overflows short of 292 years of emulated time.
    inline std::chrono::nanoseconds cycles_to_time(uint64_t cycles, uint32_t hz)
    {
        return std::chrono::seconds(cycles / hz) + std::chrono::nanoseconds(cycles % hz * 1'000'000'000ull / hz);
    }
} // namespace ooe::emu
