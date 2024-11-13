#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include <stdint.h>

#include "fmt/core.h"

#include "cpu.h"
#include "display.h"
#include "keyboard.h"
#include "memory.h"
#include "logging.h"

#define TICK_RATE 0            // 0 = run as fast as possible, otherwise sleep for n microseconds (1000000 = 1 second)
#define CYCLES_PER_TICK 4

INITIALIZE_EASYLOGGINGPP

namespace ooe
{
    class Emulator
    {
    public:
        Emulator(Memory *memory, Keyboard *keyboard, Display *display);
        ~Emulator();
        
        void Run(cpu *mos6502);

        void UpdateDisplay();
        void ReadKeyboard();
        void LoadProgram(uint16_t address, uint8_t *program, size_t size);


    private:
        Memory *memory;
        Keyboard *keyboard;
        Display *display;
        bool shouldPause;
		uint64_t ticks;
        void WozMon(uint16_t address);
        void Basic(uint16_t address);
    };

} // namespace ooe