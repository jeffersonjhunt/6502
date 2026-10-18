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
        while (!quit)
        {
            if (!handle_key(keyboard_.poll()))
                break;

            for (int i = 0; i < STEPS_PER_TICK; ++i)
                cpu_.step();

            display_.update();

            if (++ticks_ % 1000 == 0)
                EMU_LOGV(TAG, "ticks: %llu", static_cast<unsigned long long>(ticks_));
            if (TICK_SLEEP_US > 0)
                std::this_thread::sleep_for(std::chrono::microseconds(TICK_SLEEP_US));
        }
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
