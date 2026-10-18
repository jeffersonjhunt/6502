#include "apple1.h"

#include "loader.h"

namespace ooe
{
    bool LoadApple1(Memory &memory, const std::string &rom)
    {
        if (LoadImage(&memory, rom, APPLE1_ROM_BASE) < 0)
        {
            return false;
        }
        memory.SetReadOnly(APPLE1_ROM_BASE, 0xFFFF);
        memory.SetReadOnly(KBD, DSPCR, false);
        return true;
    }
} // namespace ooe
