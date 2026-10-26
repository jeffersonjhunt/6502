/**
 * @file emulator.cpp
 * @brief Run loop implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "emulator.h"

#include <chrono>
#include <thread>

#include "emu_log.h"
#include "pacing.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "emulator";

        constexpr uint8_t CTRL_C = 0x03;
    } // namespace

    void Emulator::run(const volatile std::sig_atomic_t &quit)
    {
        using clock = std::chrono::steady_clock;
        debugger_.on_step([this] { after_step(); });
        debugger_.on_interrupt([&quit] { return quit != 0; });

        // Paced from (start, start_cycles), re-based whenever the rate changes
        // or running resumes; the totals are only for the log.
        auto start = clock::now();
        uint64_t start_cycles = cpu_.cycles();
        double run_secs = 0; // excluding time halted
        uint64_t run_cycles = 0;
        auto add_stretch = [&] {
            run_secs += std::chrono::duration<double>(clock::now() - start).count();
            run_cycles += cpu_.cycles() - start_cycles;
        };
        auto rebase = [&] {
            start = clock::now();
            start_cycles = cpu_.cycles();
        };

        while (!quit && !debugger_.quit())
        {
            if (debugger_.halted())
            {
                std::string line;
                if (!terminal_.read_line("debug> ", line))
                    break; // end of input, or a signal: either way, done
                terminal_.write(debugger_.command(line));
                terminal_.update_screen();
                update_clock(); // a step can enter Apple II mode, a reset leave it
                if (!debugger_.halted())
                {
                    if (card_)
                        card_->resume(); // if the card stopped it, it lets go too
                    rebase(); // the clock restarts on resume
                }
                continue;
            }

            if (!handle_key(terminal_.poll_key()))
                break;
            if (!debugger_.halted())
                run_slice();
            terminal_.update_screen();
            const bool new_rate = update_clock();
            if (Terminal::resumed() || new_rate)
            {
                add_stretch();
                rebase(); // pace from here, at the new rate, or not racing back a stop
            }

            if (debugger_.halted())
            {
                add_stretch();
                continue;
            }

            if (clock_hz_ > 0)
            {
                const auto due = start + cycles_to_time(cpu_.cycles() - start_cycles, clock_hz_);
                if (due - clock::now() > std::chrono::milliseconds(1))
                    std::this_thread::sleep_until(due);
            }
        }
        if (!debugger_.halted())
            add_stretch();
        debugger_.on_step(nullptr); // it outlives this loop
        debugger_.on_interrupt(nullptr);
        EMU_LOGI(TAG, "%llu cycles in %.2f s running (%.3f MHz)", static_cast<unsigned long long>(run_cycles),
                 run_secs, run_secs > 0 ? static_cast<double>(run_cycles) / run_secs / 1e6 : 0.0);
    }

    bool Emulator::update_clock()
    {
        const uint32_t fixed = bus_.clock_hz(); // Apple II mode: the clock card's
        const uint32_t hz = fixed ? fixed : base_hz_;
        if (hz == clock_hz_)
            return false;
        EMU_LOGI(TAG, "clock %u Hz", hz);
        clock_hz_ = hz;
        if (clock_changed_)
            clock_changed_(hz);
        return true;
    }

    void Emulator::run_slice()
    {
        unsigned ran = 0;
        while (ran < SLICE_CYCLES)
        {
            if (debugger_.check(cpu_.regs().pc))
            {
                terminal_.write("\n" + debugger_.halt("break"));
                break;
            }
            if (card_ && !card_->may_run(cpu_.regs().pc))
            {
                terminal_.write("\n" + debugger_.halt("panel"));
                break;
            }
            ran += cpu_.step();
            after_step();
        }

        // STP stops the clock for good; say so once rather than idle silently.
        if (cpu_.state() == Cpu6502::State::Stopped && !stop_reported_ && !debugger_.halted())
        {
            stop_reported_ = true;
            terminal_.write("\n" + debugger_.halt("STP"));
        }
        else if (cpu_.state() != Cpu6502::State::Stopped)
            stop_reported_ = false;
    }

    void Emulator::after_step()
    {
        // Level-sensitive, sampled after every instruction (devices keep
        // time in lockstep, so a timer fires at the right instruction).
        // irq() does nothing while I is set, except end a WAI.
        if (bus_.irq())
            cpu_.irq();
        if (bus_.take_reset()) // the north bridge entering Apple II mode
        {
            bus_.pulse_reset();
            cpu_.reset();
        }
        if (card_ && card_->take_nmi())
            cpu_.nmi();
        if (card_ && card_->take_start())
        {
            bus_.reset(); // START: the hard reset, through the north bridge
            cpu_.reset();
        }
    }

    bool Emulator::handle_key(uint8_t key)
    {
        if (key == 0)
            return true;
        EMU_LOGD(TAG, "key %02X", key);
        if (key == Terminal::DEBUG_KEY)
        {
            terminal_.write("\n" + debugger_.halt("Ctrl-]"));
            return true;
        }
        // Only reached with ISIG off; normally Ctrl-C arrives as SIGINT.
        return key != CTRL_C;
    }
} // namespace ooe::emu
