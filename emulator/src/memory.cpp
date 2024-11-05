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
    for (int i = 0; i < MEM_SIZE; i++)
    {
        memory[i] = 0;
    }
}

uint8_t Memory::Read(uint16_t addr)
{
    return memory[addr];
}

void Memory::Write(uint16_t addr, uint8_t data)
{
    memory[addr] = data;
}
