#pragma once

/**
 * @file test_support.h
 * @brief Flat 64 KB bus and CPU set-up shared by the host tests.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The tests drive the core through its public API exactly as it stood at the
 * `beta` tag — Phase 0 measures that code, it does not change it. The core has
 * no "set PC" or "set A", so a starting state is reached the only way it
 * offers: load the reset-time register values, point the reset vector at the
 * start address, and call Reset(). Memory under the vector is restored
 * afterwards, so a test image that occupies $FFFC is unaffected.
 *
 * FlatBus is deliberately not the emulator's Memory: Memory clears the PIA
 * keyboard flag on any read of $D010, and a CPU conformance suite that walks
 * all 64 KB must not be steered by an Apple-1 side effect.
 */

#include <array>
#include <cstdint>

// cpu.h pulls in easylogging++, whose CHECK/CHECK_EQ would replace harness.h's
// with no-ops while logging is off. See test/host/CMakeLists.txt.
#ifndef ELPP_NO_CHECK_MACROS
#error "ELPP_NO_CHECK_MACROS must be defined, or easylogging++ disables harness.h's CHECK macros"
#endif

#include "bus.h"
#include "cpu.h"

namespace ooe::test
{
    /// 6502 status-register bits.
    namespace flag
    {
        constexpr uint8_t C = 0x01;
        constexpr uint8_t Z = 0x02;
        constexpr uint8_t I = 0x04;
        constexpr uint8_t D = 0x08;
        constexpr uint8_t B = 0x10;
        constexpr uint8_t U = 0x20;
        constexpr uint8_t V = 0x40;
        constexpr uint8_t N = 0x80;
    } // namespace flag

    /// Skip code registered with CTest (SKIP_RETURN_CODE) for absent fixtures.
    constexpr int SKIP = 77;

    class FlatBus : public BusInterface
    {
    public:
        uint8_t Read(uint16_t addr) override { return mem_[addr]; }
        void Write(uint16_t addr, uint8_t data) override { mem_[addr] = data; }

        void clear() { mem_.fill(0); }
        uint8_t &at(uint16_t addr) { return mem_[addr]; }

    private:
        std::array<uint8_t, 65536> mem_{};
    };

    struct Regs
    {
        uint16_t pc{0};
        uint8_t s{0xFD};
        uint8_t a{0};
        uint8_t x{0};
        uint8_t y{0};
        uint8_t p{flag::U};
    };

    /// Put `c` into state `r` via the reset path (see file comment).
    inline void boot(cpu &c, FlatBus &bus, const Regs &r)
    {
        const uint8_t lo = bus.at(0xFFFC);
        const uint8_t hi = bus.at(0xFFFD);
        bus.at(0xFFFC) = static_cast<uint8_t>(r.pc & 0xFF);
        bus.at(0xFFFD) = static_cast<uint8_t>(r.pc >> 8);
        c.SetResetA(r.a);
        c.SetResetX(r.x);
        c.SetResetY(r.y);
        c.SetResetS(r.s);
        c.SetResetP(r.p);
        c.Reset();
        bus.at(0xFFFC) = lo;
        bus.at(0xFFFD) = hi;
    }
} // namespace ooe::test
