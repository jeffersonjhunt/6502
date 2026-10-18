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
    if (read_only[addr])
    {
        LOG(TRACE) << fmt::format("{:#06x} <- {:#04x} (read-only, ignored)", addr, data);
        return;
    }
    LOG(TRACE) << fmt::format("{:#06x} <- {:#04x}", addr, data);
    memory[addr] = WriteSideEffects(addr, data);
}

void Memory::Poke(uint16_t addr, uint8_t data)
{
    memory[addr] = data;
}

void Memory::SetReadOnly(uint16_t first, uint16_t last, bool protect)
{
    for (uint32_t a = first; a <= last; a++)
    {
        read_only[a] = protect;
    }
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

uint8_t Memory::WriteSideEffects(uint16_t addr, uint8_t data)
{
    /*
     * 6821 PIA: bits 6 and 7 of each control register are interrupt flags.
     * The CPU can read them but not store to them; they are set by the
     * peripheral (Poke) and cleared by reading the data register. Woz Monitor
     * writes $A7 to KBDCR at reset, which used to fake a keypress.
     */
    if (addr == KBDCR || addr == DSPCR)
    {
        return (uint8_t)((data & 0x3F) | (memory[addr] & 0xC0));
    }

    /*
     * DSP bit 7 is the display's busy line (PB7, an input), not part of the
     * character: any store outputs the low 7 bits and the display reads busy
     * until it has taken it (Display clears the bit). While CRB bit 2 is 0,
     * $D012 is the data direction register instead, as at reset when Woz
     * Monitor stores $7F there; that is not output.
     */
    if (addr == DSP)
    {
        if (!(memory[DSPCR] & 0x04))
        {
            return memory[addr];
        }
        return (uint8_t)(data | 0x80);
    }
    return data;
}
