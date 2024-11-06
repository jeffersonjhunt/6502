#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include <stdint.h>

#include "fmt/core.h"

#include "cpu.h"
#include "keyboard.h"
#include "memory.h"
#include "logging.h"

#define MEM_SIZE 65536
#define TICK_RATE 1             // 1ms per tick


#define IN      0x0200          //  Input buffer to $027F
#define KBD     0xD010          //  PIA.A keyboard input
#define KBDCR   0xD011          //  PIA.A keyboard control register
#define DSP     0xD012          //  PIA.B display output register
#define DSPCR   0xD013          //  PIA.B display control register

INITIALIZE_EASYLOGGINGPP

namespace ooe
{
    class Emulator
    {
    public:
        Emulator(Memory *memory, Keyboard *keyboard);
        ~Emulator();
        
        void Run(cpu *mos6502);

        void UpdateDisplay();
        void ReadKeyboard();

    private:
        Memory *memory;
        Keyboard *keyboard;
        bool shouldPause;
		uint64_t ticks;
        void WozMon(uint16_t address);
    };

} // namespace ooe