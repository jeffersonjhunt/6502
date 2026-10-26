#pragma once

/**
 * @file console_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's Console card (module ID E): the PDP-11 style front panel.
 * @version 0.1.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Front Panel", "Console registers") is the spec. Not to be
 * confused with Console (console.h), the host's keyboard and screen.
 * Registers at $C700:
 *
 *   $00-$02 SWR  $03 PANEL  $04 KEYS  $05 DISPLAY  $06 CTRL
 *   $08-$0A BPADDR  $0B BPCTRL  $0C STATUS
 *   $10-$12 LADDR  $13 LDATA  $14 LSTAT  $15-$17 PADDR  $18 IEN  $19 ISR  $7F ID
 *
 * Three ways in, as on the board:
 *
 * - The CPU bus: read() and write(), for programs.
 * - The SPI header: peek() and write() too, which is what the ESP32 (and the
 *   WebUI) uses. KEYS written here presses keys even when LOCK is set:
 *   that is the remote panel, not the panel.
 * - The panel itself: set_switches(), set_halt_switch(), set_bus_cycle_switch(),
 *   set_lock_switch() and press(). These are switches, not registers, and
 *   LOCK (CTRL bit 2, or the keyswitch) makes the card ignore them.
 *
 * Its CPLD watches every bus cycle (it is a CycleObserver: the CPU's, and
 * the DMA controller's through the decoder, with BE in LSTAT), so the
 * breakpoint comparator and the latched cycle are exact to the cycle, and
 * the lights accumulate how often each line was high (set_lights()).
 *
 * BPCTRL's match bits are disjoint: bit 1 an opcode fetch (SYNC high), bit 2
 * any other read, bit 3 a write.
 *
 * The emulator runs whole instructions, so a halt is between instructions:
 *
 * - A fetch breakpoint that halts is checked before the instruction
 *   (may_run()), so the CPU stops with PC on it, as RDY at the SYNC cycle
 *   does on the board.
 * - A read or write breakpoint latches its exact cycle (LADDR, LDATA,
 *   LSTAT), but the CPU stops after that instruction; on the board RDY
 *   holds the cycle itself.
 * - S BUS CYCLE is readable in PANEL, but CONT still runs one instruction.
 * - Time stands still while halted, so EXAM and DEP take no bus cycle.
 *
 * CONT while halted clears a breakpoint halt and CTRL's HALT; if the HALT
 * switch is still down it runs one instruction, else the CPU runs on. The
 * STATUS hit bit stays until a 1 is written to it. START (press_start())
 * is the hard reset through the north bridge: the run loop does it
 * (take_start()), and the card clears CTRL, IEN, ISR and any halt but its
 * own switch. The breakpoint stays armed, so a boot address can be caught.
 * Any other /RESET (Ctrl-Reset, entering Apple II mode) does the same to
 * the card (bus_reset()).
 *
 * Not modelled yet: Apple mode (AMODE, hiding the registers), and a BE lamp.
 */

#include <array>
#include <cstdint>

#include "bus_cycle.h"
#include "decoder.h"
#include "device.h"

namespace ooe::emu
{
    class ConsoleCard : public Device, public CycleObserver
    {
    public:
        static constexpr uint8_t MODULE = 0xE;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision

        enum Reg : uint8_t
        {
            SWR = 0x00,
            PANEL = 0x03,
            KEYS = 0x04,
            DISPLAY = 0x05,
            CTRL = 0x06,
            BPADDR = 0x08,
            BPCTRL = 0x0B,
            STATUS = 0x0C,
            LADDR = 0x10,
            LDATA = 0x13,
            LSTAT = 0x14,
            PADDR = 0x15,
            IEN = 0x18,
            ISR = 0x19,
            ID_REG = 0x7F,
        };
        // PANEL
        static constexpr uint8_t HALT_SWITCH = 0x01, BUS_CYCLE = 0x02;
        // KEYS
        static constexpr uint8_t LOAD_ADRS = 0x01, EXAM = 0x02, DEP = 0x04, CONT = 0x08;
        // CTRL
        static constexpr uint8_t CTRL_HALT = 0x01, LOCK = 0x04, SHOW_DISPLAY = 0x08;
        // BPCTRL
        static constexpr uint8_t BP_ENABLE = 0x01, BP_FETCH = 0x02, BP_READ = 0x04, BP_WRITE = 0x08,
                                 BP_NMI = 0x10;
        // STATUS
        static constexpr uint8_t HALTED = 0x01, HIT = 0x02, KEY_PRESSED = 0x04;
        // LSTAT
        static constexpr uint8_t L_SYNC = 0x01, L_RWB = 0x02, L_VPB = 0x04, L_BE = 0x08, L_AMODE = 0x10;

        static constexpr unsigned ADDRESS_LIGHTS = 20;
        static constexpr unsigned DATA_LIGHTS = 8;

        /// How bright each lamp is, 0 to 1: the share of bus cycles the
        /// line was high since the last lights() call, or 0/1 while halted.
        struct Lights
        {
            std::array<float, ADDRESS_LIGHTS> addr{};
            std::array<float, DATA_LIGHTS> data{};
            float sync{0}, rwb{0}, vpb{0}, rdy{0};
            uint64_t cycles{0}; ///< the bus cycles they average over (0 while halted)
        };

        /// The decoder it watches (physical addresses, wait states) and
        /// reaches for EXAM and DEP. Not owned.
        void attach(Decoder *decoder) { decoder_ = decoder; }

        // Device: the registers at $C700
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return isr_ != 0; }
        void bus_reset() override { reset_card(); }

        // CycleObserver: the CPLD's view of each bus cycle
        void on_cycle(const BusCycle &c) override;

        // The panel
        void set_switches(uint32_t swr) { swr_ = swr & (PHYSICAL_SPACE - 1); }
        void set_halt_switch(bool halt) { panel_ = static_cast<uint8_t>(halt ? panel_ | HALT_SWITCH : panel_ & ~HALT_SWITCH); }
        void set_bus_cycle_switch(bool bus_cycle)
        {
            panel_ = static_cast<uint8_t>(bus_cycle ? panel_ | BUS_CYCLE : panel_ & ~BUS_CYCLE);
        }
        /// The keyswitch at LOCK: the same as CTRL bit 2.
        void set_lock_switch(bool locked) { lock_switch_ = locked; }
        /// Panel keys (KEYS bits). Ignored while locked.
        void press(uint8_t keys);
        /// The START key. Ignored while locked.
        void press_start();

        // For the run loop, between instructions
        /// The HALT switch, CTRL's HALT, or a breakpoint.
        bool halted() const;
        /// Before each instruction at `pc`: false if the CPU must not run it
        /// (halted, or a fetch breakpoint just hit). While halted, CONT
        /// lets exactly one instruction through.
        bool may_run(uint16_t pc);
        /// A breakpoint asked for /NMI; true once per request.
        bool take_nmi();
        /// START was pressed; true once per press. The run loop resets the
        /// bus and CPU; the card has already reset itself.
        bool take_start();
        /// Run on, as CONT with the HALT switch at ENABLE: for a debugger
        /// that stopped because of the card and is now resuming.
        void resume();

        /// Start or stop accumulating lamp duty (it costs per cycle).
        void set_lights(bool on) { lights_on_ = on; }
        /// The lamps since the last call; starts the next period.
        Lights lights();

    private:
        struct Cycle
        {
            uint32_t addr{0};
            uint8_t data{0};
            uint8_t stat{0}; ///< LSTAT bits
        };

        bool locked() const { return lock_switch_ || (ctrl_ & LOCK); }
        void apply(uint8_t keys);
        /// Freeze LADDR/LDATA/LSTAT (and the lights) at `c`, unless already frozen.
        void latch(const Cycle &c);
        void hit(const Cycle &c);
        void reset_card();

        Decoder *decoder_{nullptr};
        uint32_t swr_{0};
        uint8_t panel_{0};
        bool lock_switch_{false};
        uint8_t keys_{0};
        uint8_t display_{0};
        uint8_t ctrl_{0};
        uint32_t bpaddr_{0};
        uint8_t bpctrl_{0};
        uint8_t status_hit_{0};
        uint32_t paddr_{0};
        uint8_t last_key_{0};   ///< EXAM or DEP: the next of the same kind advances PADDR
        uint8_t ien_{0};
        uint8_t isr_{0};

        bool bp_halt_{false};
        bool frozen_{false};    ///< latched_ holds the stop cycle
        bool step_{false};      ///< CONT while halted: one instruction may run
        bool skip_fetch_{false}; ///< the next instruction ignores a fetch breakpoint
        bool nmi_{false};
        bool start_{false};
        Cycle last_;            ///< the latest bus cycle
        Cycle latched_;         ///< LADDR/LDATA/LSTAT
        Cycle shown_;           ///< what the lights show while halted (the stop cycle, or EXAM/DEP's)

        bool lights_on_{false};
        std::array<uint64_t, ADDRESS_LIGHTS> addr_high_{};
        std::array<uint64_t, DATA_LIGHTS> data_high_{};
        uint64_t sync_high_{0}, rwb_high_{0}, vpb_high_{0}, rdy_held_{0}, lit_cycles_{0};
    };
} // namespace ooe::emu
