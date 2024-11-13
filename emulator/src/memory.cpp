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
    ReadSideEffects(addr);
    return memory[addr];
}

void Memory::Write(uint16_t addr, uint8_t data)
{
    LOG(TRACE) << fmt::format("{:#06x} <- {:#04x}", addr, data);
    WriteSideEffects(addr, data);
    memory[addr] = data;
}


void Memory::ReadSideEffects(uint16_t addr)
{
    /*
     * Apple 1 side effects:
     *
     * Keyboard input register. This register holds valid data when b7 of KBDCR is "1". 
     * Reading KBD will automatically clear b7 of KBDCR. Bit b7 of KBD is permanently set.
     * The monitor expects only upper case characters.
     * 
     * The only bit which we are interested in in the KBDCR register is bit b7. It will be 
     * set whenever a key is pressed on the keyboard. It is cleared again when the KBD location
     * is read as a side effect
    */

    if( addr == KBD )
    {
        LOG(TRACE) << "Clearing 7th bit of KBDCR";
        memory[KBDCR] = bit_clear(memory[KBDCR], 7);
    }
}

void Memory::WriteSideEffects(uint16_t addr, uint8_t data)
{
    // None yet
}
