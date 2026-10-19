#pragma once

/**
 * @file emulator.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The run loop: keyboard, a slice of cycles, devices, IRQ, screen.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Time is CPU cycles, counted exactly by the core since Phase 4. The loop
 * runs whole instructions until a slice of SLICE_CYCLES has passed, ticks
 * the devices by what actually ran, then services the host. With a clock
 * rate it sleeps to keep emulated time level with wall time; it only sleeps
 * once ahead by more than a millisecond, so the host's sleep granularity
 * does not set the speed.
 *
 * The debugger can stop it between any two instructions: a breakpoint
 * (checked before every instruction), Ctrl-], STP, or --halt. While halted
 * the loop reads debugger commands instead of running, and on resuming the
 * clock starts afresh, so time spent at the prompt is not raced back.
 */

#include <csignal>
#include <cstdint>

#include "cpu6502.h"
#include "debugger.h"
#include "decoder.h"
#include "terminal.h"

namespace ooe::emu
{
    class Emulator
    {
    public:
        /// Cycles between keyboard/screen polls: 64 us at 1 MHz.
        static constexpr unsigned SLICE_CYCLES = 64;

        /// Borrows everything; main() owns it and outlives run().
        /// `clock_hz` of 0 runs flat out.
        Emulator(Cpu6502 &cpu, Decoder &bus, Terminal &terminal, Debugger &debugger, uint32_t clock_hz)
            : cpu_(cpu), bus_(bus), terminal_(terminal), debugger_(debugger), clock_hz_(clock_hz)
        {
        }

        /// Run until Ctrl-C, the debugger's quit, end of input at its
        /// prompt, or until `quit` becomes true.
        void run(const volatile std::sig_atomic_t &quit);

    private:
        /// @return false when the key asks to quit
        bool handle_key(uint8_t key);
        /// One slice of SLICE_CYCLES, or less if a breakpoint stops it.
        void run_slice();

        Cpu6502 &cpu_;
        Decoder &bus_;
        Terminal &terminal_;
        Debugger &debugger_;
        uint32_t clock_hz_;
        bool stop_reported_{false};
    };
} // namespace ooe::emu
