#pragma once

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include <stdint.h>

#include "fmt/core.h"

#include "apple1.h"
#include "cpu.h"
#include "display.h"
#include "keyboard.h"
#include "memory.h"
#include "logging.h"

#define TICK_RATE 0            // 0 = run as fast as possible, otherwise sleep for n microseconds (1000000 = 1 second)
#define CYCLES_PER_TICK 16

INITIALIZE_EASYLOGGINGPP

namespace ooe
{
    class Emulator
    {
    public:
        /// Does not take ownership: main() owns every object and outlives Run().
        Emulator(Memory *memory, Keyboard *keyboard, Display *display);
        ~Emulator();
        
        /// Runs until Ctrl-C or SIGINT/SIGTERM, then returns so destructors
        /// (the keyboard's restores the terminal) get to run.
        void Run(cpu *mos6502);

        void UpdateDisplay();
        /// @return false when the user asked to quit
        bool ReadKeyboard();

    private:
        Memory *memory;
        Keyboard *keyboard;
        Display *display;
		uint64_t ticks;
    };

} // namespace ooe