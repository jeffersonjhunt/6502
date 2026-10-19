#pragma once

/**
 * @file bus.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief What the CPU sees: a 64 KB address space of reads and writes.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * peek() is a read without side effects, for the debugger (Phase 5).
 *
 * Two write paths, because the CPU is not the only thing that writes:
 *
 * - write() is a CPU store and obeys the memory map. ROM ignores it, and a
 *   6821's status flags cannot be stored to.
 * - poke() is the device or loader side. A ROM has to get its contents and
 *   a keyboard has to raise key-ready, both of which a CPU store must not do.
 *
 * Phase 3 (docs/EMULATOR_PLAN.md) puts an address decoder and devices
 * behind this interface; the CPU will not change.
 */

#include <cstdint>

namespace ooe::emu
{
    class Bus
    {
    public:
        virtual ~Bus() = default;

        virtual uint8_t read(uint16_t addr) = 0;
        virtual void write(uint16_t addr, uint8_t data) = 0;
        /// Device/loader-side write; defaults to write() for a plain bus.
        virtual void poke(uint16_t addr, uint8_t data) { write(addr, data); }
        /// What a read would return, without its side effects: the debugger
        /// and disassembler look at memory this way, so dumping a 6821 does
        /// not eat a keystroke.
        virtual uint8_t peek(uint16_t addr) const = 0;
    };
} // namespace ooe::emu
