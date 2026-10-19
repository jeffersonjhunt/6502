/**
 * @file emulator.cpp
 * @brief Run loop implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "emulator.h"

#include <chrono>
#include <thread>

#include "emu_log.h"

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
        auto start = clock::now();
        uint64_t start_cycles = cpu_.cycles();
        double run_secs = 0; // excluding time halted
        uint64_t run_cycles = 0;

        while (!quit && !debugger_.quit())
        {
            if (debugger_.halted())
            {
                std::string line;
                if (!terminal_.read_line("debug> ", line))
                    break; // end of input, or a signal: either way, done
                terminal_.write(debugger_.command(line));
                terminal_.update_screen();
                if (!debugger_.halted())
                {
                    start = clock::now(); // the clock restarts on resume
                    start_cycles = cpu_.cycles();
                }
                continue;
            }

            if (!handle_key(terminal_.poll_key()))
                break;
            if (!debugger_.halted())
                run_slice();
            terminal_.update_screen();

            if (debugger_.halted())
            {
                run_secs += std::chrono::duration<double>(clock::now() - start).count();
                run_cycles += cpu_.cycles() - start_cycles;
                continue;
            }

            if (clock_hz_ > 0)
            {
                const auto due = start + std::chrono::nanoseconds((cpu_.cycles() - start_cycles) * 1'000'000'000ull /
                                                                  clock_hz_);
                if (due - clock::now() > std::chrono::milliseconds(1))
                    std::this_thread::sleep_until(due);
            }
        }
        if (!debugger_.halted())
        {
            run_secs += std::chrono::duration<double>(clock::now() - start).count();
            run_cycles += cpu_.cycles() - start_cycles;
        }
        EMU_LOGI(TAG, "%llu cycles in %.2f s running (%.3f MHz)", static_cast<unsigned long long>(run_cycles),
                 run_secs, run_secs > 0 ? static_cast<double>(run_cycles) / run_secs / 1e6 : 0.0);
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
            ran += cpu_.step();
        }
        bus_.tick(ran);

        // Level-sensitive, sampled between slices. irq() does nothing
        // while I is set, except end a WAI.
        if (bus_.irq())
            cpu_.irq();

        // STP stops the clock for good; say so once rather than idle silently.
        if (cpu_.state() == Cpu6502::State::Stopped && !stop_reported_ && !debugger_.halted())
        {
            stop_reported_ = true;
            terminal_.write("\n" + debugger_.halt("STP"));
        }
        else if (cpu_.state() != Cpu6502::State::Stopped)
            stop_reported_ = false;
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
