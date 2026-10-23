#pragma once

/**
 * @file decoder.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Address decoding: CPU address -> north bridge -> device.
 * @version 0.9.0
 * @date 2026-10-22
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The W65C02S drives only A0-A15; the north bridge decides what an access
 * reaches (hw/design.md). So it is a pluggable NorthBridge: CPU address in,
 * a Route out. The default sends everything to memory with A16-A19 low,
 * which is exactly a machine with no banking; the backplane's bridge
 * (backplane_bridge.h) adds the windows, ROM space and the I/O hole.
 *
 * A route names one of three address spaces, because the backplane's north
 * bridge reaches more than the 1 MB of RAM:
 *
 * - Memory: the 20-bit physical address space (RAM A-D).
 * - Flash: ROM A at 00000-7FFFF and ROM B, the cartridge, at 80000-FFFFF,
 *   reached through a window's ROM bit.
 * - Io: the 2 KB I/O hole, CPU $C000-$C7FF, decoded ahead of the windows;
 *   module n's 128-byte block is at n x $80.
 *
 * Mappings and routes use bus addresses: the space in bits 20-21 above a
 * 20-bit offset (bus_address()). A route also says whether the access may
 * write (a window's RO bit), so a write through a read-only window is
 * dropped while poke() still loads it.
 *
 * Mappings in one space may overlap; the later one wins, which is how a
 * 4-byte PIA sits inside a 32 KB ROM image on the Apple-1 without splitting
 * the ROM. Reads of unmapped space return $FF (a floating bus is not
 * modelled); writes there are dropped.
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
 *
 * Wait states (RDY): an access to a mapping with `wait` set, or to a module
 * the north bridge is told to slow down, lasts 1 + N cycles. The device is
 * accessed once, at the end, when the CPU takes the data, so a timed device
 * has been ticked through the held cycles by then.
 *
 * Bus masters (DMA): before every CPU cycle, and every idle one, the
 * decoder offers the bus to its BusMaster, as the board's DMA controller
 * pulls RDY low before any cycle and then takes the bus with BE. The cycles
 * it uses (dma_*()) are real bus cycles, devices and timers included, and
 * the CPU's pending access is held for them like a wait state. They reach
 * memory space only, by physical address, never through the windows.
 */

#include <cstdint>
#include <vector>

#include "bus.h"
#include "device.h"

namespace ooe::emu
{
    constexpr uint32_t PHYSICAL_SPACE = 1u << 20; ///< A0-A19: the size of each space
    constexpr uint8_t NO_MODULE = 0xFF;
    constexpr uint8_t NO_DRQ = 0xFF;

    enum class Space : uint8_t
    {
        Memory = 0, ///< the 1 MB physical address space
        Flash = 1,  ///< ROM A, then ROM B
        Io = 2,     ///< the I/O hole, CPU $C000-$C7FF
    };
    constexpr uint32_t SPACE_COUNT = 3;
    constexpr uint32_t IO_HOLE = 0xC000; ///< CPU address of the I/O hole
    constexpr uint32_t IO_SIZE = 0x800;

    /// A bus address: `offset` (A0-A19) within `space`.
    constexpr uint32_t bus_address(Space space, uint32_t offset)
    {
        return static_cast<uint32_t>(space) << 20 | (offset & (PHYSICAL_SPACE - 1));
    }
    constexpr Space space_of(uint32_t bus_addr) { return static_cast<Space>(bus_addr >> 20); }
    constexpr uint32_t offset_of(uint32_t bus_addr) { return bus_addr & (PHYSICAL_SPACE - 1); }

    /// Where one CPU access goes.
    struct Route
    {
        uint32_t where{0};    ///< bus address
        bool writable{true};  ///< false: a write is dropped (a read-only window)
    };

    class NorthBridge
    {
    public:
        virtual ~NorthBridge() = default;
        /// Route a CPU access. `vector_pull` is the VPB pin. Default: memory, A16-A19 low.
        virtual Route route(uint16_t cpu_addr, bool vector_pull) const
        {
            (void)vector_pull;
            return Route{cpu_addr, true};
        }
        /// /RESET: put the bridge's registers back to their reset values.
        virtual void reset() {}
        /// Wait states the bridge inserts on an access to `module`.
        virtual unsigned wait_states(uint8_t module) const
        {
            (void)module;
            return 0;
        }
    };

    struct Mapping
    {
        uint32_t first{0};          ///< bus address, inclusive
        uint32_t last{0};           ///< bus address, inclusive, in the same space
        Device *device{nullptr};    ///< not owned
        uint8_t module{NO_MODULE};  ///< backplane module ID 0x0-0xF, or NO_MODULE
        bool irq_wired{false};      ///< device's IRQ output reaches the CPU
        uint8_t wait{0};            ///< wait states the card holds RDY for on every access
        uint8_t drq{NO_DRQ};        ///< DMA channel the device's DRQ line goes to, or NO_DRQ
    };

    class Decoder;

    /// Something that can take the bus from the CPU (RDY low, then BE low).
    class BusMaster
    {
    public:
        virtual ~BusMaster() = default;
        /// The CPU is about to make a bus cycle: use the bus first if wanted,
        /// through the decoder's dma_*() cycles.
        /// @return the cycles used, which the CPU waits out
        virtual unsigned take_bus(Decoder &bus) = 0;
        /// /RESET.
        virtual void reset() {}
    };

    /// What the backplane carried on the most recent CPU access, for the bus trace.
    struct Access
    {
        uint32_t physical{0};       ///< A0-A19
        uint8_t module{NO_MODULE};  ///< PS0-PS3, or NO_MODULE
    };

    class Decoder : public Bus
    {
    public:
        static constexpr uint8_t UNMAPPED_READ = 0xFF;

        /// `bridge` is not owned and must outlive the decoder; nullptr = default.
        explicit Decoder(NorthBridge *bridge = nullptr);

        /// Replace the north bridge (nullptr = default). Not owned.
        void set_bridge(NorthBridge *bridge);
        /// The bus master offered every cycle (nullptr = none). Not owned.
        void set_master(BusMaster *master) { master_ = master; }

        /// Add a mapping; later mappings win where ranges overlap.
        /// @return false if the range is empty, crosses or lies outside a space, or has no device
        bool map(const Mapping &m);

        uint8_t read(uint16_t addr) override;
        void write(uint16_t addr, uint8_t data) override;
        void poke(uint16_t addr, uint8_t data) override;
        uint8_t peek(uint16_t addr) const override;
        void idle() override;
        void vector_pull(bool active) override { vector_pull_ = active; }
        unsigned wait_states() const override { return last_wait_; }

        /// /RESET: the north bridge and the bus master listen.
        void reset();

        /// A bus master's cycle with nothing transferred (taking the bus).
        unsigned dma_idle();
        /// A bus master's memory read at a physical address. @return cycles used
        unsigned dma_read(uint32_t physical, uint8_t &data);
        /// A bus master's memory write at a physical address. @return cycles used
        unsigned dma_write(uint32_t physical, uint8_t data);
        /// One fly-by cycle between `card` and memory at `physical`:
        /// `to_memory` card -> memory, else memory -> card. @return cycles used
        unsigned dma_fly_by(uint32_t physical, Device &card, bool to_memory, bool tc);

        /// Where a CPU address goes now (with VPB as given).
        Route route(uint16_t addr, bool vector_pull = false) const { return bridge_->route(addr, vector_pull); }
        /// What A0-A19 carry for a CPU address: the offset in memory or
        /// flash; in the I/O hole, the CPU address itself.
        uint32_t physical(uint16_t addr, bool vector_pull = false) const;

        /// The mapping a CPU address decodes to, or nullptr.
        const Mapping *decode(uint16_t addr, bool vector_pull = false) const;
        /// Module ID a CPU address selects (for the console and bus traces).
        uint8_t module_at(uint16_t addr, bool vector_pull = false) const;
        /// The backplane's lines on the latest read() or write().
        const Access &last_access() const { return last_; }

        /// Catch timed devices up, then: is any device with a wired IRQ asserting it?
        bool irq();
        /// Catch every timed device up to now().
        void sync();
        /// Bus cycles so far: accesses plus idle cycles.
        uint64_t now() const { return now_; }

        const std::vector<Mapping> &mappings() const { return mappings_; }

    private:
        static NorthBridge default_bridge_;

        struct Slot
        {
            Device *device;
            uint64_t synced;  ///< cycles the device has been ticked through
            bool timed;
        };

        const Mapping *find(uint32_t where) const;
        /// Bring a timed mapping's device up to the cycle before this access.
        void catch_up(const Mapping &m);
        /// Route a CPU access, count its cycles (wait states and any bus
        /// master's cycles too) and record it in last_.
        const Mapping *access(uint16_t addr, Route &r);
        /// Count one bus master cycle to memory; @return the mapping (or nullptr) and its cycles in `cycles`.
        const Mapping *dma_cycle(uint32_t physical, unsigned &cycles);
        /// Bring the slot of `device` up to the cycle before now.
        void catch_up_device(const Device &device);

        NorthBridge *bridge_;
        BusMaster *master_{nullptr};
        bool vector_pull_{false};
        unsigned last_wait_{0};
        Access last_;
        std::vector<Mapping> mappings_;
        std::vector<size_t> slot_of_;     ///< per mapping: its device's slot
        std::vector<Slot> slots_;         ///< distinct devices, in mapping order
        std::vector<size_t> timed_;       ///< slots of timed devices
        std::vector<Device *> irq_wired_; ///< distinct devices with an IRQ wire
        uint64_t now_{0};
    };
} // namespace ooe::emu
