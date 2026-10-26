#pragma once

/**
 * @file disk2_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's Disk II controller (module ID C), in slot 6 in Apple II mode.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Storage") is the spec. In Apple II mode it is a Disk II:
 * AppleIo (apple_io.h) hands it slot 6, the switches at $C0E0-$C0EF and its
 * boot ROM at $C600. In native mode it answers only its ID byte, at
 * $C67F; the rest of its block reads $FF.
 *
 *   $C0E0-$C0E7  stepper phases 0-3, off then on    $C0E8/9  motor off/on
 *   $C0EA/B      drive 1/2                          $C0EC/D  Q6 off/on
 *   $C0EE/F      Q7 off/on
 *
 * A read of an even address returns the data register; an odd one, $FF. A
 * write puts its byte on the card's data bus, which the sequencer loads
 * in write mode.
 *
 * The card's two PROMs are Apple's and come from the owner, as the IIe ROM
 * does: the 256-byte boot ROM (341-0027, 16-sector) and the sequencer
 * (341-0028). Without a boot ROM slot 6 is empty, so the IIe boots to
 * BASIC; without a sequencer nothing reads. The sequencer is the real
 * state machine, run at 2 MHz (two steps a CPU cycle), so software timed
 * to the cycle meets the disk as on a real Disk II. Its PROM is read as
 * the chip is wired: address A7, A6, A0, A5 the state (fed back from data
 * bits D7-D4), A4 the read pulse (low while there is one), A3 Q7, A2 Q6,
 * A1 the data register's bit 7. Data bits 3-0 are the command: 0-7 clear,
 * 8 and C nothing, 9 shift in a 0, D a 1, A and E shift right bringing in
 * write protect, B and F load the data bus.
 *
 * The drive: a bit cell every 4 us (8 steps), or as a WOZ disk's own bit
 * timing says (3.5 us: 7 steps; between whole steps, cells a step apart
 * in turn), a read pulse on the first step of a cell holding a 1. Past three 0 bits
 * the read amplifier finds noise, as an MC3470 does: random bits, 3 in 10
 * ones. Writing, the head follows state bit D7: a change in a cell writes
 * a 1. The head moves by quarter tracks towards the magnets that are on
 * (only while the motor runs); the motor keeps spinning 1 s after it is
 * switched off. Two drives, each with its own head and disk; the one not
 * selected is stopped.
 *
 * The card keeps its own time against the bus's cycle count (attach()): it
 * catches up whenever it is touched or looked at, so a spinning disk costs
 * nothing between accesses. A long gap in read mode skips to the end,
 * since nothing read in it was taken.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "device.h"
#include "disk_image.h"

namespace ooe::emu
{
    class Decoder;

    class Disk2Card : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0xC;
        static constexpr uint8_t ID = 0xC1; ///< card type C, revision 1
        static constexpr uint8_t FLOATING = 0xFF;
        static constexpr unsigned DRIVES = 2;
        static constexpr uint64_t MOTOR_DELAY = 1'020'484; ///< cycles the motor runs on: 1 s at the Apple's clock

        /// The bus whose cycle count is the card's clock. Not owned.
        void attach(const Decoder *decoder) { decoder_ = decoder; }
        /// The owner's PROMs: the boot ROM, the sequencer (256 bytes each; empty: none). @return false with `error`
        bool load_roms(const std::string &boot, const std::string &sequencer, std::string &error);
        bool has_boot() const { return has_boot_; }
        bool has_sequencer() const { return has_sequencer_; }

        /// A disk into drive 0 or 1 (nullptr: take it out).
        void insert(unsigned drive, std::shared_ptr<DiskImage> disk);
        const std::shared_ptr<DiskImage> &disk(unsigned drive) const { return drives_[drive % DRIVES].disk; }

        // Apple II mode, through AppleIo: slot 6
        /// $C0E0 + n. `effects` false: a peek, which changes nothing.
        uint8_t slot_io(unsigned n, bool write, uint8_t data, bool effects);
        /// $C600 + offset.
        uint8_t slot_rom(uint8_t offset) const { return has_boot_ ? boot_[offset] : FLOATING; }
        /// /RESET clears the card's latch: phases, Q6, Q7 and the motor off (it runs on), drive 1.
        void reset();

        struct Status
        {
            bool motor{false};    ///< switched on
            bool spinning{false}; ///< switched on, or running on
            uint8_t drive{0};
            bool q6{false}, q7{false};
            uint8_t data{0};
            uint8_t phases{0};
            std::array<uint8_t, DRIVES> quarter_track{};
        };
        /// As of now (catches up first).
        Status status();

        // Device: native mode's block at $C600
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override { return offset == 0x7F ? ID : FLOATING; }
        void write(uint32_t, uint8_t) override {}

    private:
        struct Drive
        {
            std::shared_ptr<DiskImage> disk;
            unsigned quarter{0}; ///< 0-159
            uint32_t pos{0};     ///< the bit under the head
        };

        uint64_t now() const;
        void catch_up(uint64_t cycle);
        void run(uint64_t steps);
        void step();
        void move_head();
        uint32_t track_bits(const Drive &d) const;
        unsigned cell_length(const Drive &d) const; ///< 125 ns units
        bool noise();

        const Decoder *decoder_{nullptr};
        uint64_t synced_{0};
        std::array<uint8_t, 256> boot_{}, sequencer_{};
        bool has_boot_{false}, has_sequencer_{false};
        std::array<Drive, DRIVES> drives_{};
        unsigned drive_{0};
        uint8_t phases_{0};
        bool motor_{false}, spinning_{false};
        uint64_t stops_at_{0}; ///< cycle the motor stops, once switched off
        bool q6_{false}, q7_{false};
        uint8_t data_{0}, bus_{0}, state_{0};
        unsigned cell_{0};     ///< 125 ns units into the bit cell
        bool level_{false};    ///< the write head's last level
        unsigned zeros_{0};    ///< 0 bits read in a row
        uint32_t noise_{0x2545F491};
    };
} // namespace ooe::emu
