#pragma once

/**
 * @file session.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief One running machine, driven a budget of cycles at a time: the WebUI's core.
 * @version 0.1.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A Session owns a machine built from a description, its CPU and its
 * console (the serial terminal), and runs it for a budget of cycles per
 * call. It never sleeps and never touches a terminal or a clock: the
 * caller paces it (the browser runs one budget per animation frame), which
 * is what makes the whole thing testable on the host and portable to
 * WebAssembly (apps/web) unchanged.
 *
 * The run loop is the app's (apps/emu/emulator.cpp) without the debugger:
 * before each instruction the Console card may stop it (a HALT, a
 * breakpoint), IRQ is sampled after each, the card's /NMI and START are
 * taken after each, and the console is serviced about every 64 cycles,
 * offering the next queued key until the machine takes it. A run stops at
 * the budget (overshooting by at most one instruction), at a halt, or at
 * STP, which ends the run at once rather than idling through the budget.
 *
 * The panel goes through the card exactly as hw/design.md describes it:
 * the switches and keys are the card's inputs, and card_read()/card_write()
 * are its SPI header, which is what the ESP32 bridge will reach. So the
 * WebUI's Run, Halt and Step are the panel's HALT switch and CONT. A
 * machine without a `console` line has no panel: those calls do nothing,
 * card_read() returns $FF, and only STP and the budget stop a run.
 *
 * Inspection never takes a bus cycle: registers, peek() (CPU address, with
 * the windows) and peek_physical() (A0-A19), the lamps, and a ring of the
 * latest bus cycles in the bus trace's terms (docs/BUS_TRACE.md), recorded
 * only while turned on.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bus_cycle.h"
#include "console.h"
#include "console_card.h"
#include "cpu6502.h"
#include "machine.h"
#include "symbols.h"

namespace ooe::emu
{
    class Session
    {
    public:
        struct Config
        {
            std::string machine;  ///< machine description file
            std::string rom;      ///< replaces the first rom line's image; empty keeps it
            std::string symbols;  ///< an ld65 -Ln label file, or empty
        };

        enum class Stop : uint8_t
        {
            Budget,  ///< ran the whole budget
            Halted,  ///< the Console card holds the CPU (STATUS says whether a breakpoint did it)
            Stopped, ///< STP: only START, a reset or a power cycle gets out
        };

        struct RunResult
        {
            uint64_t cycles{0}; ///< cycles actually run
            Stop why{Stop::Budget};
        };

        /// One bus cycle, as the bus trace writes it.
        struct TraceRow
        {
            uint64_t cycle{0};
            uint32_t physical{0}; ///< A0-A19
            uint16_t cpu{0};      ///< the CPU's address
            uint8_t data{0};
            uint8_t module{NO_MODULE};
            uint8_t flags{0};     ///< TRACE_WRITE, TRACE_SYNC, TRACE_VPB
        };
        static constexpr uint8_t TRACE_WRITE = 0x01, TRACE_SYNC = 0x02, TRACE_VPB = 0x04;

        /// Build the machine and reset it. @return nullptr with `error` on failure
        static std::unique_ptr<Session> create(const Config &config, std::string &error);

        ~Session();
        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        /// Run up to `budget` cycles.
        RunResult run(uint64_t budget);

        /// The keyswitch from OFF to RUN: a new machine from the same files
        /// (RAM forgets), the panel's switches as they were, a cold start.
        /// @return false, with `error`, if the files no longer build; the old machine stays
        bool power_cycle(std::string &error);

        // The panel: the card's inputs (nothing without a card)
        bool has_card() const { return card_ != nullptr; }
        void set_switches(uint32_t swr);
        void set_panel(bool halt, bool bus_cycle);
        void set_lock(bool locked);
        /// KEYS bits: ConsoleCard::LOAD_ADRS, EXAM, DEP, CONT.
        void press(uint8_t keys);
        /// START: the hard reset (without a card, a plain reset).
        void start();
        // The card's SPI header
        uint8_t card_read(uint8_t offset) const;
        void card_write(uint8_t offset, uint8_t data);

        // The serial terminal
        /// Queue keys for the machine; each is offered until taken.
        void send(const std::string &bytes);
        /// Everything the machine printed since the last call.
        std::string take_output();

        // Inspection
        const Registers &regs() const { return cpu_->regs(); }
        uint64_t cycles() const { return cpu_->cycles(); }
        bool halted() const { return card_ && card_->halted(); }
        bool stopped() const { return cpu_->state() == Cpu6502::State::Stopped; }
        uint8_t peek(uint16_t addr) const { return machine_->bus().peek(addr); }
        uint8_t peek_physical(uint32_t physical) const { return machine_->bus().peek_physical(physical); }
        /// What A0-A19 carry for a CPU address now (the windows as they are).
        uint32_t physical(uint16_t addr) const { return machine_->bus().physical(addr); }
        /// "C800", "$C800" or a label (a label wins over hex; `$` forces hex).
        bool resolve(const std::string &text, uint16_t &addr) const;
        const Symbols &symbols() const { return symbols_; }
        const std::string &machine_name() const { return config_.machine; }

        /// Lamp duty since the last call (zeros without a card).
        ConsoleCard::Lights lights();
        void set_lights(bool on);

        /// Record the latest `depth` bus cycles (off: none, and no cost).
        void set_trace(bool on, size_t depth = 4096);
        /// Copy up to `max` of the newest rows, oldest first. @return rows copied
        size_t trace(TraceRow *out, size_t max) const;

    private:
        class Recorder;

        Session() = default;
        /// Build everything from config_. @return false with `error`
        bool build(std::string &error);
        void reset();
        /// Move console bytes both ways.
        void service();

        Config config_;
        Symbols symbols_;
        std::unique_ptr<Machine> machine_;
        std::unique_ptr<Console> console_;
        std::unique_ptr<Cpu6502> cpu_;
        ConsoleCard *card_{nullptr};
        std::unique_ptr<Recorder> recorder_;
        CycleTee observers_;

        // The panel's physical state, kept across power cycles
        uint32_t switches_{0};
        bool halt_switch_{false}, bus_cycle_switch_{false}, lock_{false}, lights_on_{false};

        std::string input_;   ///< keys not yet taken
        size_t input_next_{0};
        std::string output_;
        uint64_t next_service_{0};
    };
} // namespace ooe::emu
