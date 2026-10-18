#pragma once

/**
 * @file cpu6502.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief NMOS 6502 core: one instruction per step(), through a Bus.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT).
 * Derived from gianlucag/mos6502, Copyright (c) 2017 Gianluca Ghettini (MIT);
 * see licenses/mos6502_LICENSE.
 *
 * A jump table indexed by opcode: each entry pairs an addressing-mode
 * function, which consumes the operand bytes and returns the effective
 * address, with an operation that acts on it. The 151 documented opcodes are
 * implemented, decimal mode included; the rest execute as one-byte NOPs.
 *
 * Measured against Dormann's functional test and Harte's SingleStepTests in
 * test/host (docs/PHASE0_RESULTS.md). Known gaps: no cycle counts, and
 * decimal ADC/SBC on invalid BCD differs from silicon. Both are Phase 4,
 * along with the W65C02S variant the real board uses.
 */

#include <array>
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
        uint8_t p{0x20};
    };

    class Cpu6502
    {
    public:
        /// Status-register bits.
        static constexpr uint8_t FLAG_C = 0x01;
        static constexpr uint8_t FLAG_Z = 0x02;
        static constexpr uint8_t FLAG_I = 0x04;
        static constexpr uint8_t FLAG_D = 0x08;
        static constexpr uint8_t FLAG_B = 0x10; ///< only in pushed copies on silicon
        static constexpr uint8_t FLAG_U = 0x20; ///< reads as 1
        static constexpr uint8_t FLAG_V = 0x40;
        static constexpr uint8_t FLAG_N = 0x80;

        static constexpr uint16_t NMI_VECTOR = 0xFFFA;
        static constexpr uint16_t RESET_VECTOR = 0xFFFC;
        static constexpr uint16_t IRQ_VECTOR = 0xFFFE;

        explicit Cpu6502(Bus &bus) : bus_(bus) {}

        /// Load PC from the reset vector; A=X=Y=0, S=$FD, P=U|B.
        void reset();
        /// Fetch, decode and execute one instruction.
        void step();
        /// Take an IRQ now, unless I is set.
        void irq();
        /// Take an NMI now.
        void nmi();

        const Registers &regs() const { return regs_; }
        /// Overwrite every register: for tests and the debugger, not for programs.
        void set_regs(const Registers &r) { regs_ = r; }

    private:
        using AddrFn = uint16_t (Cpu6502::*)();
        using OpFn = void (Cpu6502::*)(uint16_t);

        struct Instr
        {
            AddrFn addr;
            OpFn op;
        };

        using Table = std::array<Instr, 256>;
        static Table build_table();
        static const Table TABLE;

        uint8_t read(uint16_t addr) { return bus_.read(addr); }
        void write(uint16_t addr, uint8_t data) { bus_.write(addr, data); }
        uint16_t read16(uint16_t addr);

        bool flag(uint8_t f) const { return (regs_.p & f) != 0; }
        void set_flag(uint8_t f, bool on);
        /// N and Z from a result, the commonest flag update.
        void set_nz(uint8_t v);

        void push(uint8_t v);
        uint8_t pop();
        void interrupt(uint16_t vector, uint8_t pushed_p);

        // Addressing modes: consume operand bytes, return the effective address.
        uint16_t addr_acc(); ///< accumulator (unused address)
        uint16_t addr_imm(); ///< #imm: the operand's own address
        uint16_t addr_abs();
        uint16_t addr_zp();
        uint16_t addr_zpx();
        uint16_t addr_zpy();
        uint16_t addr_abx();
        uint16_t addr_aby();
        uint16_t addr_imp(); ///< implied (unused address)
        uint16_t addr_rel(); ///< branch target
        uint16_t addr_izx(); ///< (zp,X)
        uint16_t addr_izy(); ///< (zp),Y
        uint16_t addr_ind(); ///< (abs), with the NMOS page-wrap bug

        // Operations, grouped as in the datasheet.
        void op_adc(uint16_t src);
        void op_and(uint16_t src);
        void op_asl(uint16_t src);
        void op_asl_acc(uint16_t src);
        void op_bcc(uint16_t src);
        void op_bcs(uint16_t src);
        void op_beq(uint16_t src);
        void op_bit(uint16_t src);
        void op_bmi(uint16_t src);
        void op_bne(uint16_t src);
        void op_bpl(uint16_t src);
        void op_brk(uint16_t src);
        void op_bvc(uint16_t src);
        void op_bvs(uint16_t src);
        void op_clc(uint16_t src);
        void op_cld(uint16_t src);
        void op_cli(uint16_t src);
        void op_clv(uint16_t src);
        void op_cmp(uint16_t src);
        void op_cpx(uint16_t src);
        void op_cpy(uint16_t src);
        void op_dec(uint16_t src);
        void op_dex(uint16_t src);
        void op_dey(uint16_t src);
        void op_eor(uint16_t src);
        void op_inc(uint16_t src);
        void op_inx(uint16_t src);
        void op_iny(uint16_t src);
        void op_jmp(uint16_t src);
        void op_jsr(uint16_t src);
        void op_lda(uint16_t src);
        void op_ldx(uint16_t src);
        void op_ldy(uint16_t src);
        void op_lsr(uint16_t src);
        void op_lsr_acc(uint16_t src);
        void op_nop(uint16_t src);
        void op_ora(uint16_t src);
        void op_pha(uint16_t src);
        void op_php(uint16_t src);
        void op_pla(uint16_t src);
        void op_plp(uint16_t src);
        void op_rol(uint16_t src);
        void op_rol_acc(uint16_t src);
        void op_ror(uint16_t src);
        void op_ror_acc(uint16_t src);
        void op_rti(uint16_t src);
        void op_rts(uint16_t src);
        void op_sbc(uint16_t src);
        void op_sec(uint16_t src);
        void op_sed(uint16_t src);
        void op_sei(uint16_t src);
        void op_sta(uint16_t src);
        void op_stx(uint16_t src);
        void op_sty(uint16_t src);
        void op_tax(uint16_t src);
        void op_tay(uint16_t src);
        void op_tsx(uint16_t src);
        void op_txa(uint16_t src);
        void op_txs(uint16_t src);
        void op_tya(uint16_t src);

        Bus &bus_;
        Registers regs_{};
    };
} // namespace ooe::emu
