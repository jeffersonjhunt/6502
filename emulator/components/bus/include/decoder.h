#pragma once

/**
 * @file decoder.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Address decoding: CPU address -> north bridge -> device.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane (hw/modules.md) has a 20-bit address bus and a 4-bit module
 * ID, but the W65C02S drives only A0-A15. Something on the north bridge
 * makes the other bits, and what that is has not been decided yet. So it is
 * a pluggable NorthBridge: CPU address in, 20-bit physical address out. The
 * default drives A16-A19 low, which is exactly a machine with no banking.
 * When the real rule is chosen (a bank latch, fixed windows), it is one
 * subclass, and nothing else here changes.
 *
 * Mappings live in physical space. Where they overlap the later one wins,
 * which is how a 4-byte PIA sits inside a 32 KB ROM image on the Apple-1
 * without splitting the ROM. Reads of unmapped space return $FF (a floating
 * bus is not modelled); writes there are dropped.
 *
 * Lookup is a reverse linear scan: a machine has a handful of mappings, so
 * anything cleverer waits for a measurement that says it is needed.
 *
 * Time is bus cycles. Every CPU cycle is exactly one read() or write(), or
 * an idle() while the CPU waits, so the decoder counts them itself and needs
 * no wiring to the CPU. Timed devices (Device::timed()) are caught up to the
 * cycle before an access to them, and all of them by irq(), which the run
 * loop calls after every instruction. poke() and peek() are not CPU cycles
 * and do not count; a debugger store through write() does, as a Store from
 * the console takes a bus cycle on the board.
 */

#include <cstdint>
#include <vector>

#include "bus.h"
#include "device.h"

namespace ooe::emu
{
    constexpr uint32_t PHYSICAL_SPACE = 1u << 20; ///< A0-A19
    constexpr uint8_t NO_MODULE = 0xFF;

    class NorthBridge
    {
    public:
        virtual ~NorthBridge() = default;
        /// The 20-bit physical address for a CPU access. Default: A16-A19 low.
        virtual uint32_t physical(uint16_t cpu_addr) const { return cpu_addr; }
    };

    struct Mapping
    {
        uint32_t first{0};          ///< physical, inclusive
        uint32_t last{0};           ///< physical, inclusive
        Device *device{nullptr};    ///< not owned
        uint8_t module{NO_MODULE};  ///< backplane module ID 0x0-0xF, or NO_MODULE
        bool irq_wired{false};      ///< device's IRQ output reaches the CPU
    };

    class Decoder : public Bus
    {
    public:
        static constexpr uint8_t UNMAPPED_READ = 0xFF;

        /// `bridge` is not owned and must outlive the decoder; nullptr = default.
        explicit Decoder(const NorthBridge *bridge = nullptr);

        /// Add a mapping; later mappings win where ranges overlap.
        /// @return false if the range is empty, outside A0-A19, or has no device
        bool map(const Mapping &m);

        uint8_t read(uint16_t addr) override;
        void write(uint16_t addr, uint8_t data) override;
        void poke(uint16_t addr, uint8_t data) override;
        uint8_t peek(uint16_t addr) const override;
        void idle() override { ++now_; }

        /// The 20-bit physical address the north bridge makes of a CPU address.
        uint32_t physical(uint16_t addr) const { return bridge_.physical(addr); }

        /// The mapping a CPU address decodes to, or nullptr.
        const Mapping *decode(uint16_t addr) const;
        /// Module ID a CPU address selects (for the console and bus traces).
        uint8_t module_at(uint16_t addr) const;
        /// Catch timed devices up, then: is any device with a wired IRQ asserting it?
        bool irq();
        /// Catch every timed device up to now().
        void sync();
        /// Bus cycles so far: accesses plus idle cycles.
        uint64_t now() const { return now_; }

        const std::vector<Mapping> &mappings() const { return mappings_; }

    private:
        static const NorthBridge DEFAULT_BRIDGE;

        struct Slot
        {
            Device *device;
            uint64_t synced;  ///< cycles the device has been ticked through
            bool timed;
        };

        /// Bring a timed mapping's device up to the cycle before this access.
        void catch_up(const Mapping &m);

        const NorthBridge &bridge_;
        std::vector<Mapping> mappings_;
        std::vector<size_t> slot_of_;     ///< per mapping: its device's slot
        std::vector<Slot> slots_;         ///< distinct devices, in mapping order
        std::vector<size_t> timed_;       ///< slots of timed devices
        std::vector<Device *> irq_wired_; ///< distinct devices with an IRQ wire
        uint64_t now_{0};
    };
} // namespace ooe::emu
