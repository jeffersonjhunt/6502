#pragma once

/**
 * @file flash.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's flash: two byte-wide chips side by side, even and odd bytes.
 * @version 0.1.0
 * @date 2026-11-16
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * ROM A (and ROM B) is two 256 KB chips (hw/design.md, "The flash: ROM A
 * and ROM B", step B5): flash offset o is chip o & 1's byte o >> 1, so a
 * word cycle reads one byte from each. Each chip is its own part, with its
 * own command decoder, busy time and status; nothing ties the two.
 *
 * A chip reads as memory until it is sent a command: a sequence of writes,
 * each a byte at a chip address, of which the chip compares only A0-A14.
 * Every command begins with the unlock pair, $AA at $5555 then $55 at
 * $2AAA (flash offsets $AAAA and $5554 for the even chip, $AAAB and $5555
 * for the odd), and goes on:
 *
 *   $A0 at $5555, then the data at any address   program one byte: the new
 *                                                byte is the old AND the data
 *   $80 at $5555, the unlock pair, $30 at an     erase the chip's 4 KB sector
 *     address in the sector                      holding it (8 KB of flash)
 *   $80 at $5555, the unlock pair, $10 at $5555  erase the whole chip
 *   $90 at $5555                                 software ID: chip address
 *                                                bit 0 chooses the maker's
 *                                                code ($BF) or the part's ($B6)
 *   $F0 at $5555 (or $F0 alone, anywhere)        back to reading the array
 *
 * A write that doesn't fit the sequence puts the chip back to reading (ID
 * mode lasts until an $F0). A chip keeps its place in a sequence until its
 * next write: reads don't disturb it and nothing times it out, and the part
 * has no reset pin, so bus_reset() leaves it too. One left after $A0 takes
 * the next write, wherever it lands, as data to program (hw/design.md: the
 * update puts each chip back to reading before its first command).
 *
 * While a program or an erase runs the chip is busy: it ignores writes, and
 * a read gives its status instead of the array: bit 7 the inverse of the
 * programmed data's bit 7 (0 during an erase), bit 6 flipping on every read,
 * bits 0-5 the array's. Software polls until two reads agree. peek() gives
 * the status without flipping bit 6. The other chip goes on as normal.
 *
 * The busy times are the part's typical ones (14 us a byte, 18 ms a
 * sector, 70 ms the chip), counted in bus cycles at the CPU clock
 * (set_cpu_hz(), 7.16 MHz by default) from the decoder the Flash is
 * attached to, so nothing is ticked. Simplifications: the change lands in
 * the array when the operation starts (a real part interrupted leaves a
 * half-done byte or sector), and there is no wear.
 *
 * poke() and load() are the loader's (Rom's): they write the array directly,
 * as a programmer would, with no commands.
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "decoder.h"
#include "memory_devices.h"

namespace ooe::emu
{
    class Flash : public Rom
    {
    public:
        static constexpr uint32_t NATIVE_HZ = 7'159'090;
        static constexpr uint8_t MAKER_ID = 0xBF;  ///< software ID, chip address 0
        static constexpr uint8_t PART_ID = 0xB6;   ///< chip address 1
        static constexpr uint32_t CHIP_SECTOR = 0x1000; ///< a chip's smallest erase (8 KB of flash)
        static constexpr uint16_t COMMAND_1 = 0x5555, COMMAND_2 = 0x2AAA; ///< chip addresses, A0-A14
        static constexpr uint32_t PROGRAM_NS = 14'000;      ///< typical times
        static constexpr uint32_t SECTOR_NS = 18'000'000;
        static constexpr uint32_t CHIP_NS = 70'000'000;

        enum class Op : uint8_t
        {
            None,
            Program,
            SectorErase,
            ChipErase,
        };

        /// `size` bytes of flash, even: each chip is half of it. Erased (all $FF) until loaded.
        explicit Flash(size_t size);

        /// The clock busy times are counted on: the decoder's cycle count. nullptr: none, and
        /// every operation is over as soon as it starts. Not owned.
        void attach(const Decoder *clock) { clock_ = clock; }
        /// How fast those cycles are, in Hz (0: 7.16 MHz).
        void set_cpu_hz(uint32_t hz) { hz_ = hz ? hz : NATIVE_HZ; }
        /// Bus cycles an operation keeps a chip busy, at the clock set_cpu_hz() gave.
        uint64_t busy_cycles(Op op) const;

        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;

        /// Chip `chip` (0 even, 1 odd) is programming or erasing.
        bool busy(unsigned chip) const;
        /// Chip `chip` reads its software ID.
        bool id_mode(unsigned chip) const { return chips_[chip & 1].id; }

#ifdef EMU_FAULT_HOOKS
        /// Tests: a program of flash offset `offset` leaves the byte as it was (a cell that won't
        /// take); clear_program_fault() takes it off. Native test builds only (EMU_FAULT_HOOKS).
        void set_program_fault(uint32_t offset)
        {
            fault_on_ = true;
            fault_offset_ = offset;
        }
        void clear_program_fault() { fault_on_ = false; }
        /// Tests: busy times in bus cycles instead of the part's (0: the part's), as a slower part.
        void set_busy_cycles(uint64_t program, uint64_t sector, uint64_t chip)
        {
            busy_override_ = {program, sector, chip};
        }
#endif

    private:
        /// Where a chip is in a command sequence.
        enum class Step : uint8_t
        {
            Read,     ///< reading the array (or the ID)
            Unlock1,  ///< $AA at $5555 seen
            Unlock2,  ///< and $55 at $2AAA
            Data,     ///< after $A0: the next write is programmed
            Erase0,   ///< after $80
            Erase1,   ///< and $AA at $5555
            Erase2,   ///< and $55 at $2AAA: $30 or $10 next
        };

        struct Chip
        {
            Step step{Step::Read};
            bool id{false};
            Op op{Op::None};
            uint64_t until{0}; ///< the cycle the operation ends
            uint8_t poll7{0};  ///< status bit 7 while busy
            bool toggle{false}; ///< status bit 6, flipped on every read while busy
        };

        uint64_t now() const { return clock_ ? clock_->now() : UINT64_MAX; }
        bool busy_now(const Chip &c) const { return c.op != Op::None && now() < c.until; }
        uint8_t status(const Chip &c, uint32_t offset) const;
        void command(unsigned chip, uint32_t address, uint8_t data);
        void start(Chip &c, Op op, uint8_t data);

        std::array<Chip, 2> chips_{};
        const Decoder *clock_{nullptr};
        uint32_t hz_{NATIVE_HZ};
#ifdef EMU_FAULT_HOOKS
        bool fault_on_{false};
        uint32_t fault_offset_{0};
        std::array<uint64_t, 3> busy_override_{};
#endif
    };
} // namespace ooe::emu
