#pragma once

/**
 * @file emulator.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The run loop: keyboard, a burst of instructions, IRQ, screen.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include <csignal>
#include <cstdint>

#include "cpu6502.h"
#include "decoder.h"
#include "terminal.h"

namespace ooe::emu
{
    class Emulator
    {
    public:
        /// Instructions per tick, between keyboard and screen polls.
        static constexpr int STEPS_PER_TICK = 16;
        /// Sleep per tick in microseconds; 0 runs flat out.
        static constexpr int TICK_SLEEP_US = 0;

        /// Borrows everything; main() owns it and outlives run().
        Emulator(Cpu6502 &cpu, Decoder &bus, Terminal &terminal) : cpu_(cpu), bus_(bus), terminal_(terminal) {}

        /// Run until Ctrl-C (as a key) or until `quit` becomes true.
        void run(const volatile std::sig_atomic_t &quit);

    private:
        /// @return false when the key asks to quit
        bool handle_key(uint8_t key);

        Cpu6502 &cpu_;
        Decoder &bus_;
        Terminal &terminal_;
        uint64_t ticks_{0};
    };
} // namespace ooe::emu
