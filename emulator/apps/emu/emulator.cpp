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
        const auto start = clock::now();
        const uint64_t start_cycles = cpu_.cycles();

        while (!quit)
        {
            if (!handle_key(terminal_.poll_key()))
                break;

            unsigned ran = 0;
            while (ran < SLICE_CYCLES)
                ran += cpu_.step();
            bus_.tick(ran);

            // Level-sensitive, sampled between slices. irq() does nothing
            // while I is set, except end a WAI.
            if (bus_.irq())
                cpu_.irq();

            terminal_.update_screen();

            if (clock_hz_ > 0)
            {
                const auto due = start + std::chrono::nanoseconds((cpu_.cycles() - start_cycles) * 1'000'000'000ull /
                                                                  clock_hz_);
                if (due - clock::now() > std::chrono::milliseconds(1))
                    std::this_thread::sleep_until(due);
            }
        }
        const double secs = std::chrono::duration<double>(clock::now() - start).count();
        EMU_LOGI(TAG, "%llu cycles in %.2f s (%.3f MHz)",
                 static_cast<unsigned long long>(cpu_.cycles() - start_cycles), secs,
                 secs > 0 ? static_cast<double>(cpu_.cycles() - start_cycles) / secs / 1e6 : 0.0);
    }

    bool Emulator::handle_key(uint8_t key)
    {
        if (key == 0)
            return true;
        EMU_LOGD(TAG, "key %02X", key);
        // Only reached with ISIG off; normally Ctrl-C arrives as SIGINT.
        return key != CTRL_C;
    }
} // namespace ooe::emu
