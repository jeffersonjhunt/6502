#pragma once

/**
 * @file test_support.h
 * @brief A flat 64 KB bus for CPU tests.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Deliberately not apple1::Memory: that clears the PIA's key-ready flag on
 * any read of $D010 and ignores stores to ROM, and a CPU conformance suite
 * that walks all 64 KB must not be steered by the Apple-1's memory map.
 *
 * Tests reach a starting state with Cpu6502::set_regs(). Until Phase 2 the
 * core had no way to set registers, and Phase 0 went through reset() and
 * the reset vector instead; the results are identical (EMULATOR_PLAN.md §4).
 */

#include <array>
#include <cstdint>

#include "bus.h"
#include "cpu6502.h"

namespace ooe::test
{
    /// Skip code registered with CTest (SKIP_RETURN_CODE) for absent fixtures.
    constexpr int SKIP = 77;

    class FlatBus : public ooe::emu::Bus
    {
    public:
        uint8_t read(uint16_t addr) override { return mem_[addr]; }
        void write(uint16_t addr, uint8_t data) override { mem_[addr] = data; }

        uint8_t &at(uint16_t addr) { return mem_[addr]; }

    private:
        std::array<uint8_t, 65536> mem_{};
    };
} // namespace ooe::test
