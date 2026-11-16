#pragma once

/**
 * @file decoder.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Address decoding: CPU address -> north bridge -> device.
 * @version 0.14.0
 * @date 2026-11-16
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The W65C02S drives only A0-A15; the north bridge decides what an access
 * reaches (hw/design.md). So it is a pluggable NorthBridge: CPU address in,
 * a Route out. The default sends everything to memory with A16-A23 low,
 * which is exactly a machine with no banking; the backplane's bridge
 * (backplane_bridge.h) adds the windows, ROM space and the I/O hole.
 *
 * A route names one of two address spaces, because the backplane's north
 * bridge reaches more than memory (hw/design.md, "The new bus"):
 *
 * - Memory: the 24-bit physical address space, A0-A23, which memory answers
 *   by address with /MEM low: on the backplane RAM at 000000-0FFFFF, the
 *   video card's aperture at E00000-E0FFFF and the flash, ROM A at
 *   F00000-F7FFFF and ROM B, the cartridge, at F80000-FFFFFF.
 * - Io: the 2 KB I/O hole, CPU $C000-$C7FF, decoded ahead of the windows;
 *   module n's 128-byte block is at n x $80. /MEM is high on these cycles,
 *   so no memory answers them, whatever the address lines hold.
 *
 * Space::Flash is a name for the flash's megabyte of memory space:
 * bus_address(Space::Flash, o) is memory F00000 + o, so a window's ROM
 * block b is physical F00000 + b x $4000.
 *
 * Mappings and routes use bus addresses: the space in bit 24 above a
 * 24-bit offset (bus_address()). A route also says whether the access may
 * write (a window's RO bit), so a write through a read-only window is
 * dropped while poke() still loads it.
 *
 * Mappings in one space may overlap; the later one wins, which is how a
 * 4-byte PIA sits inside a 32 KB ROM image on the Apple-1 without splitting
 * the ROM. Reads of unmapped space return $FF (a floating bus is not
 * modelled); writes there are dropped. A mapping larger than its device
 * mirrors it: the device sees the offset modulo its size(), as a chip
 * decodes only its own low address lines.
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
 * Wait states (/WAIT, hw/design.md "Wait states and the read budget"): a
 * card that needs them pulls the backplane's /WAIT line, and a mapping's
 * `wait` is how many cycles it holds it on every access. On a CPU cycle the
 * north bridge turns /WAIT into RDY, so the access lasts 1 + N cycles; the
 * device is accessed once, at the end, when the CPU takes the data, so a
 * timed device has been ticked through the held cycles by then. The DMA
 * controller obeys /WAIT in its grants alike: a memory cycle lasts 1 + the
 * memory's N, and a fly-by, where memory and the card named on PS0-PS3 both
 * answer the one cycle on the one open-drain line, 1 + the longer of the
 * two. The takeover and the poll (below) are the controller's own cycles
 * and take no /WAIT. /WAIT is the only source of wait states: the north
 * bridge's WAIT0-3 are storage that slows nothing (backplane_bridge.h).
 * A word cycle (below) is one cycle too, so one stretch: 1 + the longer
 * of the memory's and the card's /WAIT, once a word.
 *
 * Bus masters (DMA): before every CPU cycle, and every idle one, the
 * decoder offers the bus to its BusMaster, as the board's DMA controller
 * pulls RDY low before any cycle and then takes the bus with BE. The cycles
 * it uses (dma_*()) are real bus cycles, devices and timers included, and
 * the CPU's pending access is held for them like a wait state. They reach
 * memory space only, by physical address, never through the windows.
 *
 * Byte and word cycles (hw/design.md, "16-bit transfers", step B4): a byte
 * cycle carries D0-D7 whatever A0 is, memory steering an odd address's
 * byte onto the low lane, which a byte device reached by offset already
 * does. A word cycle (dma_*_word(), the controller's WIDE) is at an even
 * address and carries the even byte on D0-D7 and the odd on D8-D15. The
 * mapping that decodes the even address answers both lanes, as one chip
 * select does on the board, even where a later mapping overlays the odd
 * address; an odd byte past that mapping's end floats ($FF) and its write
 * is dropped. Unmapped, a word reads $FFFF (the backplane's pull-ups) and a
 * write is dropped. Memory (the RAM card, ROM A and ROM B, the video
 * aperture) answers words; a card says in its own hooks
 * (Device::dack_read_word()) whether its data register does. The line
 * that tells byte cycles from word cycles is modelled as the call, and as
 * Access::word and BusCycle::word.
 *
 * DMA requests (hw/design.md, "The DMA controller, revision 2"): a card
 * whose mapping has `drq` (Dreq::Wired) pulls /DREQ, one shared open-drain
 * line, and dreq() is its OR, the timed cards caught up first. In a poll
 * cycle (dma_poll(), /DMA and /IACK low) each requesting card pulls the data
 * line of its module ID: dreq_from(). A grant names the card by its module
 * on PS0-PS3, and dreq_card() is the wired mapping that answers it, its
 * device and its /WAIT. The table from module to mapping is kept as
 * mappings are added, so a take with nothing to do costs no scan.
 *
 * The CPU card may send a memory cycle somewhere other than the address its
 * master asked for: the backplane's bridge sends bank 8 to the video
 * aperture while a revision 2 card is fitted (NorthBridge::redirect()).
 * The CPU's windows and the DMA controller's cycles go through it, as both
 * are on the CPU card. peek_physical() and write_physical() are that same
 * reach with no cycle counted (the CPU card's view, for tests and tools);
 * peek_bus() and write_bus() are the backplane as a card driving it sees
 * it, with no redirection: the Console card's EXAM and DEP, while the CPU
 * is halted.
 *
 * PS0-PS3 on a memory cycle (Access::module): memory is chosen by address,
 * so the module lines carry the master's own ID, which the bridge gives
 * (NorthBridge::memory_select()): on the backplane D for the CPU, 8 for the
 * DMA controller's memory to memory cycles, and on a fly-by the module the
 * channel's CARD names (the card the grant is for). A
 * bus with no /MEM (no north bridge, as the Apple-1's) shows the mapping's
 * module= instead. Behind a north bridge a memory mapping's module is not
 * used.
 *
 * The CPU reports its own cycles to its observer; the bus master's go to
 * the decoder's (set_dma_observer()), each read or write as it happens (a
 * word cycle once, at its even address, with BusCycle::word set),
 * with last_access() and wait_states() describing it then, so the Console
 * card sees every bus cycle. The cycle taking the bus carries nothing and
 * is not reported: RDY is low but BE is not yet, so the CPU's held address
 * is still on the bus, and granted() lets the card light it as the CPU's.
 * The poll cycle is treated the same: its address lines carry nothing
 * defined, it is not reported to the observer or the bus trace (whose DMA
 * gap it joins), and granted() counts it, so it is lit as the CPU's too.
 */

#include <array>
#include <cstdint>
#include <vector>

#include "bus.h"
#include "bus_cycle.h"
#include "device.h"

namespace ooe::emu
{
    constexpr uint32_t PHYSICAL_SPACE = 1u << 24; ///< A0-A23: the size of each space
    /// A0-A19: what the registers still 20 bits wide hold (the Console card's PADDR, SWR and BPADDR).
    constexpr uint32_t ADDRESS_20 = (1u << 20) - 1;
    constexpr uint32_t FLASH_BASE = 0xF00000; ///< ROM A; ROM B, the cartridge, at F80000
    constexpr uint8_t NO_MODULE = 0xFF;
    constexpr unsigned MODULE_IDS = 16; ///< PS0-PS3

    /// A card's DMA request: wired to the backplane's /DREQ or not. A type of its own, so a bare
    /// 0 or 1 (revision 1's channel numbers) can't initialise it.
    enum class Dreq : uint8_t
    {
        None,  ///< not wired
        Wired, ///< the card pulls /DREQ, and answers the poll and a grant by its module ID
    };

    enum class Space : uint8_t
    {
        Memory = 0, ///< the 16 MB physical address space
        Io = 1,     ///< the I/O hole, CPU $C000-$C7FF
        Flash = 2,  ///< not a space of its own: memory F00000-FFFFFF, ROM A then ROM B
    };
    constexpr uint32_t SPACE_COUNT = 2; ///< the spaces a bus address can be in: Memory and Io
    constexpr uint32_t IO_HOLE = 0xC000; ///< CPU address of the I/O hole
    constexpr uint32_t IO_SIZE = 0x800;
    /// Apple II mode's $C000-$CFFF, in Io space past the native hole (backplane_bridge.h);
    /// the address lines still show $0C000-$0CFFF.
    constexpr uint32_t APPLE_IO = 0x1000;

    /// A bus address: `offset` (A0-A23) within `space`; in Space::Flash, the offset into the flash (A0-A19).
    constexpr uint32_t bus_address(Space space, uint32_t offset)
    {
        if (space == Space::Flash)
            return FLASH_BASE | (offset & ADDRESS_20);
        return static_cast<uint32_t>(space) << 24 | (offset & (PHYSICAL_SPACE - 1));
    }
    constexpr Space space_of(uint32_t bus_addr) { return static_cast<Space>(bus_addr >> 24); }
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
        /// Route a CPU access. `vector_pull` is the VPB pin; `write` is RWB low (a
        /// bridge may send reads and writes to different places, as Apple II
        /// mode's RAMRD and RAMWRT do). Default: memory, A16-A23 low.
        virtual Route route(uint16_t cpu_addr, bool vector_pull, bool write) const
        {
            (void)vector_pull;
            (void)write;
            return Route{cpu_addr, true};
        }
        /// A hard reset (power-on, the reset button): everything back to its reset value.
        virtual void reset() {}
        /// A /RESET pulse that is not a hard reset (Apple II mode's own, Ctrl-Reset):
        /// by default the same as reset().
        virtual void reset_pulse() { reset(); }
        /// The bridge wants a /RESET pulse (entering Apple II mode): the run loop
        /// asks after each instruction, and resets the CPU. Taken once.
        virtual bool take_reset() { return false; }
        /// The CPU clock the bridge's mode fixes (the clock card follows it, as
        /// Apple II mode's ÷14 does), in Hz; 0: the caller's choice.
        virtual uint32_t clock_hz() const { return 0; }
        /// Who drives a memory cycle, for memory_select().
        enum class Master : uint8_t
        {
            Cpu, ///< through a window
            Dma, ///< the DMA controller's memory to memory cycle
        };
        /// PS0-PS3 on a memory cycle `master` drives, or NO_MODULE for a bus with no /MEM,
        /// where the mapping's own module shows (the default).
        virtual uint8_t memory_select(Master master) const
        {
            (void)master;
            return NO_MODULE;
        }
        /// Where the CPU card sends a memory cycle for `physical` (A0-A23): the CPU's windows and
        /// the DMA controller's cycles alike. Default: where it was asked.
        virtual uint32_t redirect(uint32_t physical) const { return physical; }
    };

    struct Mapping
    {
        uint32_t first{0};          ///< bus address, inclusive
        uint32_t last{0};           ///< bus address, inclusive, in the same space
        Device *device{nullptr};    ///< not owned
        uint8_t module{NO_MODULE};  ///< backplane module ID 0x0-0xF, or NO_MODULE
        bool irq_wired{false};      ///< device's IRQ output reaches the CPU
        uint8_t wait{0};            ///< cycles the card holds /WAIT for on every access, the CPU's and DMA's
        Dreq drq{Dreq::None};       ///< the device's DMA request reaches /DREQ (by `module`)
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
        uint32_t physical{0};       ///< A0-A23
        uint8_t module{NO_MODULE};  ///< PS0-PS3, or NO_MODULE
        bool word{false};           ///< a bus master's word cycle (D0-D15, `physical` even)
    };

    class Decoder : public Bus
    {
    public:
        static constexpr uint8_t UNMAPPED_READ = 0xFF;
        static constexpr uint16_t UNMAPPED_WORD = 0xFFFF; ///< a word cycle no mapping answers: pull-ups

        /// `bridge` is not owned and must outlive the decoder; nullptr = default.
        explicit Decoder(NorthBridge *bridge = nullptr);

        /// Replace the north bridge (nullptr = default). Not owned.
        void set_bridge(NorthBridge *bridge);
        /// The bus master offered every cycle (nullptr = none). Not owned.
        void set_master(BusMaster *master) { master_ = master; }
        /// What watches the bus master's cycles (BusCycle::dma), nullptr = nothing. Not owned.
        void set_dma_observer(CycleObserver *observer) { dma_observer_ = observer; }

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
        /// Of the latest CPU access's wait_states(), the cycles a bus master took first.
        unsigned stolen() const { return last_stolen_; }
        /// Of stolen(), the cycles spent taking the bus (dma_idle()), which no observer saw.
        unsigned granted() const { return last_granted_; }

        /// /RESET: the north bridge and the bus master listen, and every
        /// device hears it (Device::bus_reset()).
        void reset();
        /// A /RESET pulse that is not a hard reset (NorthBridge::reset_pulse()).
        void pulse_reset();
        /// Something wants /RESET pulsed (the bridge entering Apple II mode).
        bool take_reset() { return bridge_->take_reset(); }
        /// The clock the bridge's mode fixes, or 0 (NorthBridge::clock_hz()).
        uint32_t clock_hz() const { return bridge_->clock_hz(); }

        /// A bus master's cycle with nothing transferred (taking the bus).
        unsigned dma_idle();
        /// The DMA controller's poll cycle (/DMA and /IACK low, PS 8): one cycle, counted in
        /// granted() as the takeover is, and not reported to the observer. The cards wired to
        /// /DREQ are caught up, so dreq_from() reads them as of the poll. @return cycles used
        unsigned dma_poll();
        /// /DREQ is low: a card wired to it (Mapping::drq) is requesting. The timed ones are caught
        /// up to now first.
        bool dreq();
        /// In a poll: module `module`'s card pulls its data line (it is wired to /DREQ and
        /// requesting). As of the last catch-up: call it after dma_poll().
        bool dreq_from(uint8_t module) const;
        /// The mapping wired to /DREQ that answers module `module` on PS0-PS3, or nullptr.
        const Mapping *dreq_card(uint8_t module) const;
        /// A bus master's memory read at a physical address. @return cycles used
        unsigned dma_read(uint32_t physical, uint8_t &data);
        /// A bus master's memory write at a physical address. @return cycles used
        unsigned dma_write(uint32_t physical, uint8_t data);
        /// One fly-by cycle between `card` and memory at `physical`:
        /// `to_memory` card -> memory, else memory -> card. Both hold /WAIT as their mappings
        /// say, so the cycle is stretched by the longer of the two. @return cycles used
        unsigned dma_fly_by(uint32_t physical, Device &card, bool to_memory, bool tc);
        /// The same for the DMA controller's grant: `card` is the mapping wired to /DREQ
        /// (dreq_card()), whose `wait` is the card's side of /WAIT, and `module` the channel's
        /// MODULE, which PS0-PS3 carry. @return cycles used
        unsigned dma_fly_by(uint32_t physical, const Mapping &card, uint8_t module, bool to_memory, bool tc);
        /// A bus master's word cycle (WIDE), reading memory at an even physical address: the even byte in
        /// bits 0-7 (D0-D7), the odd in 8-15. One cycle, stretched by the memory's /WAIT once. @return cycles used
        unsigned dma_read_word(uint32_t physical, uint16_t &data);
        /// A bus master's word cycle writing memory at an even physical address. @return cycles used
        unsigned dma_write_word(uint32_t physical, uint16_t data);
        /// One fly-by word cycle between `card` and memory at an even `physical` (Device::dack_read_word(),
        /// dack_write_word()), one /WAIT stretch for both sides. @return cycles used
        unsigned dma_fly_by_word(uint32_t physical, Device &card, bool to_memory, bool tc);
        /// The same for the DMA controller's grant, as dma_fly_by()'s Mapping overload. @return cycles used
        unsigned dma_fly_by_word(uint32_t physical, const Mapping &card, uint8_t module, bool to_memory, bool tc);
        /// Memory at a physical address (A0-A23) with no bus cycle counted, as
        /// the CPU card reaches it (NorthBridge::redirect()): with a revision 2
        /// video card on the backplane, bank 8 is the aperture.
        uint8_t peek_physical(uint32_t physical) const;
        /// Memory at a physical address as the backplane carries it, with no
        /// redirection and no cycle counted: the Console card's EXAM, while the
        /// CPU is halted and emulated time stands still. Bank 8 is the RAM's.
        uint8_t peek_bus(uint32_t physical) const;
        /// A store the same way (DEP). A device write, as the card's cycle is: ROM ignores it.
        void write_bus(uint32_t physical, uint8_t data);
        /// I/O space at `offset` ($000-$FFF the cards, as the native map puts them at $C000)
        /// with no bus cycle, whatever the map is now: a card's registers in Apple II mode too.
        uint8_t peek_io(uint16_t offset) const;
        /// A store at a physical address with no bus cycle counted, as the CPU
        /// card reaches it (peek_physical()). A device write: ROM ignores it.
        void write_physical(uint32_t physical, uint8_t data);

        /// Where a CPU address goes now (with VPB as given).
        Route route(uint16_t addr, bool vector_pull = false, bool write = false) const
        {
            return bridge_->route(addr, vector_pull, write);
        }
        /// Where the CPU card sends a memory cycle for `physical` (NorthBridge::redirect()): with a
        /// revision 2 video card on the backplane, bank 8 goes to the aperture at $E00000.
        uint32_t redirect(uint32_t physical) const { return bridge_->redirect(physical & (PHYSICAL_SPACE - 1)); }
        /// What A0-A23 carry for a CPU address: the physical address in
        /// memory (the flash at F00000); in the I/O hole, the CPU address itself.
        uint32_t physical(uint16_t addr, bool vector_pull = false) const;

        /// The mapping a CPU address decodes to, or nullptr.
        const Mapping *decode(uint16_t addr, bool vector_pull = false) const;
        /// What PS0-PS3 carry for a CPU address (for the console and bus traces): a card's
        /// module ID in the I/O hole; on a memory cycle the bridge's memory_select(), or the
        /// mapping's module on a bus with no /MEM.
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

        void reset_cards();
        const Mapping *find(uint32_t where) const;
        /// The offset a bus address is at in mapping `m`'s device, mirrored into its size().
        uint32_t device_offset(const Mapping &m, uint32_t where) const;
        /// Bring a timed mapping's device up to the cycle before this access.
        void catch_up(const Mapping &m);
        /// Route a CPU access, count its cycles (wait states and any bus
        /// master's cycles too) and record it in last_.
        const Mapping *access(uint16_t addr, Route &r, bool write);
        /// Count one bus master cycle to memory at `where` (a bus address, redirected), stretched by
        /// the memory's /WAIT or `card_wait`, whichever is longer; @return the mapping (or nullptr)
        /// and its cycles in `cycles`.
        const Mapping *dma_cycle(uint32_t where, unsigned &cycles, unsigned card_wait = 0);
        /// The bus address a CPU card cycle for `physical` reaches (NorthBridge::redirect()).
        uint32_t memory(uint32_t physical) const
        {
            return bus_address(Space::Memory, bridge_->redirect(physical & (PHYSICAL_SPACE - 1)));
        }
        /// PS0-PS3 on a memory cycle by `master` to mapping `m` (or nothing).
        uint8_t select(NorthBridge::Master master, const Mapping *m) const;
        /// Bring the slot of `device` up to the cycle before now.
        void catch_up_device(const Device &device);
        /// Tell the DMA observer about a bus master's cycle of `cycles` at bus address `where`, to mapping `m`
        /// (or nothing), with `is_fly_by` true on a fly-by, whose grant puts `module` on PS0-PS3; PS0-PS3 are
        /// worked out only if it watches.
        void dma_seen(uint32_t where, const Mapping *m, bool is_fly_by, uint8_t module, unsigned cycles,
                      uint8_t data, bool write, bool word = false, uint8_t data_high = 0);
        /// Both lanes of a word cycle at bus address `where` (even), mapping `m` answering: the odd byte
        /// past its end floats.
        uint16_t read_word(const Mapping &m, uint32_t where);
        void write_word(const Mapping &m, uint32_t where, uint16_t data);
        /// A word fly-by, as fly_by().
        unsigned word_fly_by(uint32_t physical, Device &card, unsigned card_wait, uint8_t module, bool to_memory,
                             bool tc);
        /// A fly-by of `card` with its side of /WAIT `card_wait` and `module` on PS0-PS3.
        unsigned fly_by(uint32_t physical, Device &card, unsigned card_wait, uint8_t module, bool to_memory,
                        bool tc);
        /// Catch the devices wired to /DREQ up to the cycle before now.
        void catch_up_dreq();
        /// `card`'s mapping in the I/O hole, or nullptr.
        const Mapping *io_mapping_of(const Device &card) const;

        NorthBridge *bridge_;
        BusMaster *master_{nullptr};
        CycleObserver *dma_observer_{nullptr};
        unsigned last_stolen_{0};
        unsigned last_granted_{0};
        unsigned granting_{0}; ///< dma_idle() cycles of the take_bus() under way
        bool vector_pull_{false};
        unsigned last_wait_{0};
        Access last_;
        std::vector<Mapping> mappings_;
        std::vector<size_t> slot_of_;     ///< per mapping: its device's slot
        std::vector<uint32_t> mirror_;    ///< per mapping: the device's size() if the mapping is larger, else 0
        std::vector<Slot> slots_;         ///< distinct devices, in mapping order
        std::vector<size_t> timed_;       ///< slots of timed devices
        std::vector<Device *> irq_wired_; ///< distinct devices with an IRQ wire
        std::vector<size_t> dreq_wired_;  ///< per distinct device wired to /DREQ: its first such mapping
        std::array<int, MODULE_IDS> dreq_module_; ///< per module ID: the wired mapping answering it, or -1
        uint64_t now_{0};
    };
} // namespace ooe::emu
