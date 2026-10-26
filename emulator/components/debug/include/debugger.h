#pragma once

/**
 * @file debugger.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A line-command debugger shaped like the console panel.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane's console (hw/console.md) has a system key (Run, Step/Halt),
 * Step/Next, Store/Fetch against the address switches, and lights for the
 * address and data buses. The commands here are those, plus what a panel
 * cannot do: breakpoints, disassembly and register edits.
 *
 *   r  run                 leave the debugger                    (Run)
 *   s  step [N]            N instructions, default 1             (Step/Next)
 *   f  fetch ADDR [N]      N bytes from ADDR, default 16         (Fetch)
 *   st store ADDR BYTE...  write as the CPU would                (Store)
 *   x  regs [R=V ...]      show, or set A X Y S P PC
 *   l  list [ADDR] [N]     disassemble N (default 8) from ADDR (default PC)
 *   b  break [ADDR]        set a breakpoint, or list them
 *   d  delete ADDR|all     remove breakpoints
 *   t  trace on|off        pause or resume the bus trace
 *      reset               the reset sequence
 *   q  quit                end the emulator
 *
 * A machine adds views of its own devices (add_view()): the backplane's
 * `rtc`, the north bridge's real-time clock.
 *
 * Addresses and values are hex, with or without `$`, or a label from the
 * symbols; counts are decimal. Fetch uses Bus::peek(), so looking at a 6821
 * does not clear its flags. Store goes through the CPU's write path, so ROM
 * stays read-only and a store to an I/O register does what the program's
 * would; use --load to patch ROM.
 *
 * It halts between instructions, not cycles: stepping mid-instruction would
 * need RDY, which the core does not model (cpu6502.h).
 *
 * Nothing here touches a terminal: command() takes a line and returns the
 * text to show, so the whole debugger is testable on the host, and the
 * app's run loop only decides when to prompt.
 */

#include <bitset>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bus_trace.h"
#include "cpu6502.h"
#include "decoder.h"
#include "symbols.h"

namespace ooe::emu
{
    class Debugger
    {
    public:
        /// Borrows everything. `trace` may be nullptr (no --trace).
        Debugger(Cpu6502 &cpu, Decoder &bus, const Symbols &symbols, BusTrace *trace = nullptr)
            : cpu_(cpu), bus_(bus), symbols_(symbols), trace_(trace)
        {
        }

        /// Execute one command line.
        /// @return the text to show, each line ending in '\n'
        std::string command(const std::string &line);

        /// Stop running and say why ("break", "STP", "key").
        /// @return the status to show
        std::string halt(const std::string &why);
        bool halted() const { return halted_; }
        bool quit() const { return quit_; }

        /// For the run loop, before each step: true if the CPU is about to
        /// fetch an opcode at a breakpoint, in which case the debugger is
        /// now halted. Not while it idles in WAI or STP, or a breakpoint
        /// after one would stop every cycle. The first instruction after a
        /// resume does not break where it halted, or `run` from a breakpoint
        /// would stop again where it is; moved elsewhere (PC=, a step), or
        /// halted in WAI (the fetch after it never stopped), it does.
        bool check(uint16_t pc)
        {
            if (cpu_.state() != Cpu6502::State::Running)
                return false;
            if (skip_armed_)
            {
                skip_armed_ = false;
                if (pc == stop_pc_)
                    return false;
            }
            if (!breaks_[pc])
                return false;
            halted_ = true;
            stop_pc_ = pc;
            return true;
        }
        bool any_breakpoints() const { return breaks_.any(); }

        /// What the run loop does after each instruction (IRQ, NMI, resets),
        /// so a step does the same; without it, a step only samples IRQ.
        void on_step(std::function<void()> f) { after_step_ = std::move(f); }
        /// Polled during a long step: true (Ctrl-C) cuts it short.
        void on_interrupt(std::function<bool()> f) { interrupted_ = std::move(f); }
        /// A command `name` showing what `show` returns (a device's state); `help` is its
        /// line in the help, before `q`, ending in '\n'.
        void add_view(std::string name, std::string help, std::function<std::string()> show)
        {
            views_.push_back(View{std::move(name), std::move(help), std::move(show)});
        }

        /// Registers, flags and the next instruction.
        std::string status() const;

        /// Resolve "FF00", "$FF00" or a label.
        bool parse_address(const std::string &s, uint16_t &addr) const;
        bool add_breakpoint(uint16_t addr);

    private:
        std::string step(unsigned count);
        std::string fetch(uint16_t addr, unsigned count) const;
        std::string list(uint16_t addr, unsigned count) const;
        std::string line_at(uint16_t pc, unsigned *length = nullptr) const;
        std::string set_registers(const std::vector<std::string> &settings);
        std::string breakpoints() const;

        Cpu6502 &cpu_;
        Decoder &bus_;
        const Symbols &symbols_;
        BusTrace *trace_;
        std::bitset<65536> breaks_;
        bool halted_{false};
        bool quit_{false};
        bool skip_armed_{false}; ///< resumed: don't break at stop_pc_ first
        uint16_t stop_pc_{0};     ///< where it last halted or stepped to
        std::function<void()> after_step_;
        std::function<bool()> interrupted_;
        struct View
        {
            std::string name, help;
            std::function<std::string()> show;
        };
        std::vector<View> views_;
    };
} // namespace ooe::emu
