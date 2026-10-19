#pragma once

/**
 * @file device.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A chip on the bus: addressed by offset within its mapping.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A device sees what a chip sees: its own low address lines, not the
 * system address. The decoder (decoder.h) does the chip select and passes
 * `offset = physical address - mapping base`, so the same Pia6821 works at
 * $D010 on an Apple-1 and wherever the backplane's I/O module puts it.
 *
 * No tick() yet: nothing can use one until the core counts cycles
 * (Phase 4), and a tick measured in instructions would be a wrong API to
 * build a 6522's timers on.
 */

#include <cstddef>
#include <cstdint>

namespace ooe::emu
{
    class Device
    {
    public:
        virtual ~Device() = default;

        /// Bytes of address space the device decodes (its mapping's natural size).
        virtual size_t size() const = 0;

        /// CPU read. May have side effects (a 6821 clears its flags).
        virtual uint8_t read(uint32_t offset) = 0;
        /// CPU write. A ROM ignores it.
        virtual void write(uint32_t offset, uint8_t data) = 0;
        /// Loader-side write, past any read-only rule.
        virtual void poke(uint32_t offset, uint8_t data) { write(offset, data); }

        /// Interrupt output, active high. Only reaches the CPU if the mapping wires it.
        virtual bool irq() const { return false; }
    };
} // namespace ooe::emu
