#pragma once

/**
 * @file apple1.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The Apple-1 memory map: RAM, read-only ROM, and the 6821 PIA.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * One flat 64 KB array with the Apple-1's rules applied on the CPU path
 * (write); the device path (poke) is unrestricted. The rules, each of which
 * was a bug before Phase 1 (docs/EMULATOR_PLAN.md §1):
 *
 * - ROM is read-only. Applesoft sizes RAM by finding a byte that will not
 *   change, and hung when there wasn't one.
 * - Bits 6-7 of KBDCR and DSPCR are 6821 status flags: readable, not
 *   storable. Woz Monitor's reset-time $A7 store used to fake a keypress.
 * - Reading KBD clears key-ready.
 * - DSP bit 7 is the display's busy line, not part of the character, so any
 *   store outputs; while CRB bit 2 is 0, $D012 is the DDR and nothing does.
 *
 * Phase 3 replaces this with RAM/ROM/Pia6821 devices behind a decoder.
 */

#include <bitset>
#include <cstdint>
#include <string>

#include "bus.h"

namespace ooe::emu::apple1
{
    constexpr uint16_t KBD = 0xD010;   ///< PIA A data: keyboard
    constexpr uint16_t KBDCR = 0xD011; ///< PIA A control
    constexpr uint16_t DSP = 0xD012;   ///< PIA B data: display
    constexpr uint16_t DSPCR = 0xD013; ///< PIA B control
    constexpr uint16_t ROM_BASE = 0x8000;

    constexpr uint8_t KEY_READY = 0x80;    ///< KBDCR bit 7
    constexpr uint8_t DISPLAY_BUSY = 0x80; ///< DSP bit 7

    class Memory : public Bus
    {
    public:
        static constexpr size_t SIZE = 65536;

        uint8_t read(uint16_t addr) override;
        void write(uint16_t addr, uint8_t data) override;
        void poke(uint16_t addr, uint8_t data) override;

        /// Make [first, last] read-only to the CPU (or writable again).
        void set_read_only(uint16_t first, uint16_t last, bool protect = true);

    private:
        uint8_t store_value(uint16_t addr, uint8_t data) const;

        uint8_t mem_[SIZE]{};
        std::bitset<SIZE> read_only_;
    };

    /**
     * @brief Load a ROM image at $8000 and lay out the map around it
     *
     * The image (apple1/src/apple1.cfg: 32 KB, $8000-$FFFF) becomes
     * read-only except the PIA at $D010-$D013. Below $8000 is RAM.
     *
     * @return false if the ROM cannot be loaded
     */
    bool load_rom(Memory &memory, const std::string &path);
} // namespace ooe::emu::apple1
