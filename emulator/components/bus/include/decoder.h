#pragma once

/**
 * @file decoder.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Address decoding: CPU address -> north bridge -> device.
 * @version 0.3.0
 * @date 2026-10-18
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

        /// The mapping a CPU address decodes to, or nullptr.
        const Mapping *decode(uint16_t addr) const;
        /// Module ID a CPU address selects (for the console and bus traces).
        uint8_t module_at(uint16_t addr) const;
        /// True if any device with a wired IRQ is asserting it.
        bool irq() const;

        const std::vector<Mapping> &mappings() const { return mappings_; }

    private:
        static const NorthBridge DEFAULT_BRIDGE;

        const NorthBridge &bridge_;
        std::vector<Mapping> mappings_;
    };
} // namespace ooe::emu
