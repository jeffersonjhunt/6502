#include "memory.h"

Memory::Memory()
{
    Reset();
}

Memory::~Memory()
{
}

void Memory::Reset()
{
    LOG(DEBUG) << "Memory reset";
    for (int i = 0; i < MEM_SIZE; i++)
    {
        memory[i] = 0;
    }
}

uint8_t Memory::Read(uint16_t addr)
{
    LOG(TRACE) << fmt::format("{:#06x} -> {:#04x}", addr, memory[addr]);
    return memory[addr];
}

void Memory::Write(uint16_t addr, uint8_t data)
{
    LOG(TRACE) << fmt::format("{:#06x} <- {:#04x}", addr, data);
    memory[addr] = data;
}
