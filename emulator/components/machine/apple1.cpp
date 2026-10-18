/**
 * @file apple1.cpp
 * @brief Apple-1 memory map implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "apple1.h"

#include "emu_log.h"
#include "loader.h"

namespace ooe::emu::apple1
{
    namespace
    {
        [[maybe_unused]] const char *TAG = "apple1";

        constexpr uint8_t CR_FLAGS = 0xC0;     ///< 6821 control-register bits 6-7
        constexpr uint8_t CRB_SELECT_OR = 0x04; ///< CRB bit 2: $D012 is ORB, not DDRB
    } // namespace

    uint8_t Memory::read(uint16_t addr)
    {
        const uint8_t v = mem_[addr];
        EMU_LOGV(TAG, "%04X -> %02X", addr, v);
        if (addr == KBD)
            mem_[KBDCR] = static_cast<uint8_t>(mem_[KBDCR] & ~KEY_READY);
        return v;
    }

    void Memory::write(uint16_t addr, uint8_t data)
    {
        if (read_only_[addr])
        {
            EMU_LOGV(TAG, "%04X <- %02X (read-only, ignored)", addr, data);
            return;
        }
        EMU_LOGV(TAG, "%04X <- %02X", addr, data);
        mem_[addr] = store_value(addr, data);
    }

    void Memory::poke(uint16_t addr, uint8_t data)
    {
        mem_[addr] = data;
    }

    void Memory::set_read_only(uint16_t first, uint16_t last, bool protect)
    {
        for (uint32_t a = first; a <= last; a++)
            read_only_[a] = protect;
    }

    uint8_t Memory::store_value(uint16_t addr, uint8_t data) const
    {
        if (addr == KBDCR || addr == DSPCR)
            return static_cast<uint8_t>((data & ~CR_FLAGS) | (mem_[addr] & CR_FLAGS));
        if (addr == DSP)
        {
            if (!(mem_[DSPCR] & CRB_SELECT_OR))
                return mem_[addr]; // a DDRB write: not output
            return static_cast<uint8_t>(data | DISPLAY_BUSY);
        }
        return data;
    }

    bool load_rom(Memory &memory, const std::string &path)
    {
        if (load_image(memory, path, ROM_BASE) < 0)
            return false;
        memory.set_read_only(ROM_BASE, 0xFFFF);
        memory.set_read_only(KBD, DSPCR, false);
        return true;
    }
} // namespace ooe::emu::apple1
