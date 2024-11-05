#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include <stdint.h>

#include "fmt/core.h"

#include "mos6502.h"
#include "keyboard.h"

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
        Emulator(bool shouldPause=false);
        ~Emulator();
        
        void WriteMemory(uint16_t address, uint8_t data, bool debug=false);
        uint8_t ReadMemory(uint16_t address, bool debug=false);

        void Run(mos6502 *cpu);

        void UpdateDisplay();
        void ReadKeyboard();

    private:
        Keyboard *keyboard;
        bool shouldPause;
		uint64_t cycleCount;
        uint8_t memory[MEM_SIZE];
        void WozMon(uint16_t address);
    };

} // namespace ooe