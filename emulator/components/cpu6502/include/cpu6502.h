#pragma once

/**
 * @file cpu6502.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief NMOS 6502 and WDC W65C02S, bus-cycle accurate.
 * @version 0.4.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT).
 * Originally derived from gianlucag/mos6502, Copyright (c) 2017 Gianluca
 * Ghettini (MIT); see licenses/mos6502_LICENSE.
 *
 * Every cycle of a real 6502 is exactly one bus access, a read or a write,
 * including the "dummy" ones: the re-read of an address before a page carry
 * is fixed, the NMOS write-back in a read-modify-write, the stack read before
 * a pull. This core makes each of those accesses, at the address the silicon
 * uses, in order. So the cycle count is simply the number of accesses, and
 * the bus traffic can be diffed against a logic analyser or the bus spy
 * (Phase 5) cycle by cycle. It matters functionally too: a dummy read of a
 * 6821 clears its flags on hardware, and does so here.
 *
 * Verified per cycle against Tom Harte's SingleStepTests for both models
 * (address, data and direction of every access) and against Klaus Dormann's
 * functional tests (test/host). Execution is still an instruction at a time:
 * interrupts are taken between instructions.
 *
 * Models:
 * - Nmos6502: the 151 documented opcodes, and NMOS decimal mode including
 *   its invalid-BCD results and flags. Undocumented opcodes run as two-cycle,
 *   one-byte NOPs, which is not what NMOS silicon does.
 * - W65C02S: the real board's CPU. Every WDC instruction (BRA, PHX..PLY, STZ,
 *   TRB/TSB, BIT #/zp,X/abs,X, INC A/DEC A, (zp), JMP (abs,X), RMB/SMB,
 *   BBR/BBS, WAI, STP), 65C02 decimal mode (valid N/Z, one extra cycle), D
 *   cleared by interrupts, a fixed JMP ($xxFF), and WDC's undefined opcodes
 *   as the NOPs Harte documents.
 */

#include <cstdint>

#include "bus.h"

namespace ooe::emu
{
    struct Registers
    {
        uint16_t pc{0};
        uint8_t s{0xFD};
        uint8_t a{0};
        uint8_t x{0};
        uint8_t y{0};
        uint8_t p{0x20}; ///< U always reads 1; B is never set in the live register
    };

    class Cpu6502
    {
    public:
        enum class Model
        {
            Nmos6502,
            W65C02S,
        };

        enum class State
        {
            Running,
            Waiting, ///< after WAI, until an interrupt
            Stopped, ///< after STP, until reset
        };

        /// Status-register bits.
        static constexpr uint8_t FLAG_C = 0x01;
        static constexpr uint8_t FLAG_Z = 0x02;
        static constexpr uint8_t FLAG_I = 0x04;
        static constexpr uint8_t FLAG_D = 0x08;
        static constexpr uint8_t FLAG_B = 0x10; ///< only in copies pushed by PHP/BRK
        static constexpr uint8_t FLAG_U = 0x20; ///< always reads 1
        static constexpr uint8_t FLAG_V = 0x40;
        static constexpr uint8_t FLAG_N = 0x80;

        static constexpr uint16_t NMI_VECTOR = 0xFFFA;
        static constexpr uint16_t RESET_VECTOR = 0xFFFC;
        static constexpr uint16_t IRQ_VECTOR = 0xFFFE;

        explicit Cpu6502(Bus &bus, Model model = Model::Nmos6502) : bus_(bus), model_(model)
        {
            regs_.s = 0; // so the reset sequence's three decrements leave $FD
        }

        /// The 7-cycle reset sequence: PC from $FFFC, S -= 3, I set (and D
        /// cleared on the W65C02S). A, X and Y are left alone, as on silicon.
        void reset();
        /// One instruction, or one idle cycle while waiting or stopped.
        /// @return cycles taken
        unsigned step();
        /// Take an IRQ now (7 cycles) unless I is set. Also ends WAI, with or
        /// without servicing it, as the W65C02S does.
        void irq();
        /// Take an NMI now (7 cycles); also ends WAI.
        void nmi();

        const Registers &regs() const { return regs_; }
        /// Overwrite every register: for tests and the debugger, not for programs.
        void set_regs(const Registers &r) { regs_ = r; }
        /// Cycles (bus accesses plus idle cycles) since construction.
        uint64_t cycles() const { return cycles_; }
        State state() const { return state_; }
        Model model() const { return model_; }

    private:
        enum class Mode
        {
            Imm,
            Zp,
            Zpx,
            Zpy,
            Abs,
            Abx,
            Aby,
            Izx, ///< (zp,X)
            Izy, ///< (zp),Y
            Izp, ///< (zp), W65C02S only
        };

        using Rmw = uint8_t (Cpu6502::*)(uint8_t);

        bool cmos() const { return model_ == Model::W65C02S; }

        // One bus access each; with idle cycles, the only things that count.
        uint8_t read(uint16_t addr);
        void write(uint16_t addr, uint8_t data);

        uint8_t fetch() { return read(regs_.pc++); }
        uint16_t fetch16();
        void push(uint8_t v);
        uint8_t pull();

        // Effective addresses, making each mode's address-phase accesses.
        uint16_t ea_read(Mode m);  ///< indexed: the extra cycle only on a page cross
        uint16_t ea_write(Mode m); ///< indexed: always the extra cycle
        uint16_t abs_indexed(uint8_t index, bool always_extra);
        uint16_t izy(bool always_extra);

        bool flag(uint8_t f) const { return (regs_.p & f) != 0; }
        void set_flag(uint8_t f, bool on);
        void set_nz(uint8_t v);

        // Operations.
        void load(uint8_t &reg, Mode m);
        void store(Mode m, uint8_t v);
        void logic(Mode m, char op);
        void adc(Mode m);
        void sbc(Mode m);
        void compare(uint8_t reg, Mode m);
        void bit(Mode m);
        void binary_add(uint8_t m);
        void decimal_extra_cycle(Mode m, uint16_t ea, bool subtract);
        void rmw(Mode m, Rmw op);
        void rmw_acc(Rmw op);
        void rmb_smb(unsigned bitno, bool set);
        void branch(bool taken);
        void bbr_bbs(unsigned bitno, bool set);
        void interrupt(uint16_t vector, uint8_t pushed_p);
        void implied() { read(regs_.pc); }
        void push_reg(uint8_t v);
        uint8_t pull_reg();

        uint8_t asl(uint8_t v);
        uint8_t lsr(uint8_t v);
        uint8_t rol(uint8_t v);
        uint8_t ror(uint8_t v);
        uint8_t inc(uint8_t v);
        uint8_t dec(uint8_t v);
        uint8_t tsb(uint8_t v);
        uint8_t trb(uint8_t v);

        void execute(uint8_t opcode);
        bool execute_cmos(uint8_t opcode);

        Bus &bus_;
        Model model_;
        Registers regs_{};
        State state_{State::Running};
        uint64_t cycles_{0};
    };
} // namespace ooe::emu
