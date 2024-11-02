#pragma once

#include <algorithm>
#include <iostream>

#include <stdint.h>

#include "fmt/core.h"

#include "mos6502.h"

#define MEM_SIZE 65536

namespace ooe
{
    class Emulator
    {
    public:
        Emulator();
        ~Emulator();
        void WriteMemory(uint16_t address, uint8_t data, bool debug=false);
        uint8_t ReadMemory(uint16_t address, bool debug=false);

        void Run(mos6502 *cpu);
    private:
		uint64_t cycleCount;
        uint64_t lastCycleCount;
        uint8_t memory[MEM_SIZE];
        void WozMon(uint16_t address);
    };

} // namespace ooe