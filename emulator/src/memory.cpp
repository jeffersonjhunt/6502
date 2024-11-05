#include "memory.h"


uint8_t Memory::Read(uint16_t addr)
{
    return memory[addr];
}

void Memory::Write(uint16_t addr, uint8_t data)
{
    memory[addr] = data;
}
