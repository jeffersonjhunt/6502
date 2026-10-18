#pragma once

#include <string>

#include "memory.h"

namespace ooe
{
    constexpr uint16_t APPLE1_ROM_BASE = 0x8000;

    /**
     * @brief Load an Apple-1 ROM image and set up the memory map around it
     *
     * The image (apple1/src/apple1.cfg: 32 KB, $8000-$FFFF) is loaded at
     * $8000 and that range made read-only, except the PIA at $D010-$D013.
     * Below $8000 is RAM. Read-only ROM is what lets Applesoft find the top
     * of RAM at cold start.
     *
     * @return false if the ROM cannot be loaded
     */
    bool LoadApple1(Memory &memory, const std::string &rom);
} // namespace ooe
