#pragma once

/**
 * @file emulator.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The run loop: keyboard, a slice of cycles, devices, IRQ, screen.
 * @version 0.6.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Time is CPU cycles, counted exactly by the core since Phase 4; devices
 * keep it in lockstep through the decoder (Phase 3b). The loop runs whole
 * instructions, checking IRQ after each, until a slice of SLICE_CYCLES has
 * passed, then services the host. With a clock
 * rate it sleeps to keep emulated time level with wall time; it only sleeps
 * once ahead by more than a millisecond, so the host's sleep granularity
 * does not set the speed.
 *
 * The debugger can stop it between any two instructions: a breakpoint
 * (checked before every instruction), Ctrl-], STP, or --halt. While halted
 * the loop reads debugger commands instead of running, and on resuming the
 * clock starts afresh, so time spent at the prompt (or stopped by Ctrl-Z)
 * is not raced back. A breakpoint is checked only before an opcode fetch,
 * not on the idle cycles of WAI or STP. The debugger's steps do the same
 * work after each instruction as running does (after_step()).
 *
 * A Console card (console_card.h) can stop it too: before each instruction
 * the loop asks it (may_run()), so its HALT, a halting breakpoint or a
 * program setting CTRL's HALT lands at the debugger as "panel", and
 * resuming the debugger releases the card. Its /NMI requests are taken
 * after the instruction that made them, and START resets the machine.
 */

#include <csignal>
#include <cstdint>
#include <functional>

#include "console_card.h"
#include "cpu6502.h"
#include "debugger.h"
#include "decoder.h"
#include "terminal.h"

namespace ooe::emu
{
    /// Written by main()'s signal handlers. Ctrl-C during a debugger
    /// command (a long step) ends the command, not the app.
    struct RunSignals
    {
        volatile std::sig_atomic_t quit = 0;
        volatile std::sig_atomic_t in_command = 0; ///< the run loop is in Debugger::command()
        volatile std::sig_atomic_t interrupt = 0;  ///< Ctrl-C while in_command
    };

    class Emulator
    {
    public:
        /// Cycles between keyboard/screen polls: 64 us at 1 MHz.
        static constexpr unsigned SLICE_CYCLES = 64;

        /// Borrows everything; main() owns it and outlives run().
        /// `clock_hz` of 0 runs flat out. `card` may be nullptr (no console line).
        Emulator(Cpu6502 &cpu, Decoder &bus, Terminal &terminal, Debugger &debugger, uint32_t clock_hz,
                 ConsoleCard *card = nullptr)
            : cpu_(cpu), bus_(bus), terminal_(terminal), debugger_(debugger), base_hz_(clock_hz), clock_hz_(clock_hz),
              card_(card)
        {
        }

        /// Called when the machine's mode fixes a new clock (Apple II mode),
        /// or gives it back (a hard reset), for the cards.
        void on_clock_change(std::function<void(uint32_t)> f) { clock_changed_ = std::move(f); }
        /// Run until Ctrl-C, the debugger's quit, end of input at its
        /// prompt, or until `signals.quit` becomes true.
        void run(RunSignals &signals);

    private:
        /// @return false when the key asks to quit
        bool handle_key(uint8_t key);
        /// One slice of SLICE_CYCLES, or less if a breakpoint stops it.
        void run_slice();
        /// After every instruction, run or stepped: IRQ, resets, the card's NMI and START.
        void after_step();
        /// The mode's clock if it fixes one, else --clock. @return true if it changed
        bool update_clock();

        Cpu6502 &cpu_;
        Decoder &bus_;
        Terminal &terminal_;
        Debugger &debugger_;
        uint32_t base_hz_;  ///< --clock
        uint32_t clock_hz_; ///< what the loop paces by now
        std::function<void(uint32_t)> clock_changed_;
        ConsoleCard *card_;
        bool stop_reported_{false};
    };
} // namespace ooe::emu
