#pragma once

/**
 * @file device.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A chip on the bus: addressed by offset within its mapping.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A device sees what a chip sees: its own low address lines, not the
 * system address. The decoder (decoder.h) does the chip select and passes
 * `offset = physical address - mapping base`, so the same Pia6821 works at
 * $D010 on an Apple-1 and wherever the backplane's I/O module puts it.
 *
 * Time: a device that answers timed() gets tick() calls carrying CPU
 * cycles, in lockstep with the CPU. The decoder catches it up just before
 * each access to it, so a 6522 counter read mid-instruction is exact, and
 * before each IRQ check, so a timer interrupt is seen after the very
 * instruction it fired in. Untimed devices (RAM, ROM) are never ticked,
 * which keeps that free for them.
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
        /// read() without the side effects, for the debugger.
        virtual uint8_t peek(uint32_t offset) const = 0;
        /// CPU write. A ROM ignores it.
        virtual void write(uint32_t offset, uint8_t data) = 0;
        /// Loader-side write, past any read-only rule.
        virtual void poke(uint32_t offset, uint8_t data) { write(offset, data); }

        /// Interrupt output, active high. Only reaches the CPU if the mapping wires it.
        virtual bool irq() const { return false; }

        /// True if the device keeps time (tick() does something).
        virtual bool timed() const { return false; }
        /// `cycles` CPU cycles have passed since the last tick. Only called
        /// on timed devices.
        virtual void tick(uint64_t cycles) { (void)cycles; }
    };
} // namespace ooe::emu
