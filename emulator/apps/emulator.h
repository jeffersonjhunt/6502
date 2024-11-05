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

#define MEM_SIZE 65536
#define THROTTLE 1              // 1ms


#define IN      0x0200          //  Input buffer to $027F
#define KBD     0xD010          //  PIA.A keyboard input
#define KBDCR   0xD011          //  PIA.A keyboard control register
#define DSP     0xD012          //  PIA.B display output register
#define DSPCR   0xD013          //  PIA.B display control register

namespace ooe
{
    class Emulator
    {
    public:
        Emulator(Memory *memory, bool shouldPause=false);
        ~Emulator();
        
        void Run(cpu *mos6502);

        void UpdateDisplay();
        void ReadKeyboard();

    private:
        Memory *memory;
        Keyboard *keyboard;
        bool shouldPause;
		uint64_t cycleCount;
        void WozMon(uint16_t address);
    };

} // namespace ooe