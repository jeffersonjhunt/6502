/**
 * @file cpu6502.cpp
 * @brief NMOS 6502 core implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT).
 * Derived from gianlucag/mos6502 (MIT); see licenses/mos6502_LICENSE.
 *
 * Restyled from the `beta` core without changing behaviour: the opcode table
 * was generated from the old one, and every Phase 0 suite gives the same
 * result (docs/PHASE0_RESULTS.md). Where a quirk below looks wrong, it is
 * there because the measured core had it; fixing those is Phase 4's job,
 * against the W65C02S.
 */

#include "cpu6502.h"

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        [[maybe_unused]] const char *TAG = "cpu";

        constexpr uint8_t lo8(unsigned v) { return static_cast<uint8_t>(v & 0xFF); }
    } // namespace

    Cpu6502::Table Cpu6502::build_table()
    {
        Table t{};
        for (auto &e : t)
            e = {&Cpu6502::addr_imp, &Cpu6502::op_nop}; // undocumented: one-byte NOP

        t[0x00] = {&Cpu6502::addr_imp, &Cpu6502::op_brk};
        t[0x01] = {&Cpu6502::addr_izx, &Cpu6502::op_ora};
        t[0x05] = {&Cpu6502::addr_zp, &Cpu6502::op_ora};
        t[0x06] = {&Cpu6502::addr_zp, &Cpu6502::op_asl};
        t[0x08] = {&Cpu6502::addr_imp, &Cpu6502::op_php};
        t[0x09] = {&Cpu6502::addr_imm, &Cpu6502::op_ora};
        t[0x0A] = {&Cpu6502::addr_acc, &Cpu6502::op_asl_acc};
        t[0x0D] = {&Cpu6502::addr_abs, &Cpu6502::op_ora};
        t[0x0E] = {&Cpu6502::addr_abs, &Cpu6502::op_asl};
        t[0x10] = {&Cpu6502::addr_rel, &Cpu6502::op_bpl};
        t[0x11] = {&Cpu6502::addr_izy, &Cpu6502::op_ora};
        t[0x15] = {&Cpu6502::addr_zpx, &Cpu6502::op_ora};
        t[0x16] = {&Cpu6502::addr_zpx, &Cpu6502::op_asl};
        t[0x18] = {&Cpu6502::addr_imp, &Cpu6502::op_clc};
        t[0x19] = {&Cpu6502::addr_aby, &Cpu6502::op_ora};
        t[0x1D] = {&Cpu6502::addr_abx, &Cpu6502::op_ora};
        t[0x1E] = {&Cpu6502::addr_abx, &Cpu6502::op_asl};
        t[0x20] = {&Cpu6502::addr_abs, &Cpu6502::op_jsr};
        t[0x21] = {&Cpu6502::addr_izx, &Cpu6502::op_and};
        t[0x24] = {&Cpu6502::addr_zp, &Cpu6502::op_bit};
        t[0x25] = {&Cpu6502::addr_zp, &Cpu6502::op_and};
        t[0x26] = {&Cpu6502::addr_zp, &Cpu6502::op_rol};
        t[0x28] = {&Cpu6502::addr_imp, &Cpu6502::op_plp};
        t[0x29] = {&Cpu6502::addr_imm, &Cpu6502::op_and};
        t[0x2A] = {&Cpu6502::addr_acc, &Cpu6502::op_rol_acc};
        t[0x2C] = {&Cpu6502::addr_abs, &Cpu6502::op_bit};
        t[0x2D] = {&Cpu6502::addr_abs, &Cpu6502::op_and};
        t[0x2E] = {&Cpu6502::addr_abs, &Cpu6502::op_rol};
        t[0x30] = {&Cpu6502::addr_rel, &Cpu6502::op_bmi};
        t[0x31] = {&Cpu6502::addr_izy, &Cpu6502::op_and};
        t[0x35] = {&Cpu6502::addr_zpx, &Cpu6502::op_and};
        t[0x36] = {&Cpu6502::addr_zpx, &Cpu6502::op_rol};
        t[0x38] = {&Cpu6502::addr_imp, &Cpu6502::op_sec};
        t[0x39] = {&Cpu6502::addr_aby, &Cpu6502::op_and};
        t[0x3D] = {&Cpu6502::addr_abx, &Cpu6502::op_and};
        t[0x3E] = {&Cpu6502::addr_abx, &Cpu6502::op_rol};
        t[0x40] = {&Cpu6502::addr_imp, &Cpu6502::op_rti};
        t[0x41] = {&Cpu6502::addr_izx, &Cpu6502::op_eor};
        t[0x45] = {&Cpu6502::addr_zp, &Cpu6502::op_eor};
        t[0x46] = {&Cpu6502::addr_zp, &Cpu6502::op_lsr};
        t[0x48] = {&Cpu6502::addr_imp, &Cpu6502::op_pha};
        t[0x49] = {&Cpu6502::addr_imm, &Cpu6502::op_eor};
        t[0x4A] = {&Cpu6502::addr_acc, &Cpu6502::op_lsr_acc};
        t[0x4C] = {&Cpu6502::addr_abs, &Cpu6502::op_jmp};
        t[0x4D] = {&Cpu6502::addr_abs, &Cpu6502::op_eor};
        t[0x4E] = {&Cpu6502::addr_abs, &Cpu6502::op_lsr};
        t[0x50] = {&Cpu6502::addr_rel, &Cpu6502::op_bvc};
        t[0x51] = {&Cpu6502::addr_izy, &Cpu6502::op_eor};
        t[0x55] = {&Cpu6502::addr_zpx, &Cpu6502::op_eor};
        t[0x56] = {&Cpu6502::addr_zpx, &Cpu6502::op_lsr};
        t[0x58] = {&Cpu6502::addr_imp, &Cpu6502::op_cli};
        t[0x59] = {&Cpu6502::addr_aby, &Cpu6502::op_eor};
        t[0x5D] = {&Cpu6502::addr_abx, &Cpu6502::op_eor};
        t[0x5E] = {&Cpu6502::addr_abx, &Cpu6502::op_lsr};
        t[0x60] = {&Cpu6502::addr_imp, &Cpu6502::op_rts};
        t[0x61] = {&Cpu6502::addr_izx, &Cpu6502::op_adc};
        t[0x65] = {&Cpu6502::addr_zp, &Cpu6502::op_adc};
        t[0x66] = {&Cpu6502::addr_zp, &Cpu6502::op_ror};
        t[0x68] = {&Cpu6502::addr_imp, &Cpu6502::op_pla};
        t[0x69] = {&Cpu6502::addr_imm, &Cpu6502::op_adc};
        t[0x6A] = {&Cpu6502::addr_acc, &Cpu6502::op_ror_acc};
        t[0x6C] = {&Cpu6502::addr_ind, &Cpu6502::op_jmp};
        t[0x6D] = {&Cpu6502::addr_abs, &Cpu6502::op_adc};
        t[0x6E] = {&Cpu6502::addr_abs, &Cpu6502::op_ror};
        t[0x70] = {&Cpu6502::addr_rel, &Cpu6502::op_bvs};
        t[0x71] = {&Cpu6502::addr_izy, &Cpu6502::op_adc};
        t[0x75] = {&Cpu6502::addr_zpx, &Cpu6502::op_adc};
        t[0x76] = {&Cpu6502::addr_zpx, &Cpu6502::op_ror};
        t[0x78] = {&Cpu6502::addr_imp, &Cpu6502::op_sei};
        t[0x79] = {&Cpu6502::addr_aby, &Cpu6502::op_adc};
        t[0x7D] = {&Cpu6502::addr_abx, &Cpu6502::op_adc};
        t[0x7E] = {&Cpu6502::addr_abx, &Cpu6502::op_ror};
        t[0x81] = {&Cpu6502::addr_izx, &Cpu6502::op_sta};
        t[0x84] = {&Cpu6502::addr_zp, &Cpu6502::op_sty};
        t[0x85] = {&Cpu6502::addr_zp, &Cpu6502::op_sta};
        t[0x86] = {&Cpu6502::addr_zp, &Cpu6502::op_stx};
        t[0x88] = {&Cpu6502::addr_imp, &Cpu6502::op_dey};
        t[0x8A] = {&Cpu6502::addr_imp, &Cpu6502::op_txa};
        t[0x8C] = {&Cpu6502::addr_abs, &Cpu6502::op_sty};
        t[0x8D] = {&Cpu6502::addr_abs, &Cpu6502::op_sta};
        t[0x8E] = {&Cpu6502::addr_abs, &Cpu6502::op_stx};
        t[0x90] = {&Cpu6502::addr_rel, &Cpu6502::op_bcc};
        t[0x91] = {&Cpu6502::addr_izy, &Cpu6502::op_sta};
        t[0x94] = {&Cpu6502::addr_zpx, &Cpu6502::op_sty};
        t[0x95] = {&Cpu6502::addr_zpx, &Cpu6502::op_sta};
        t[0x96] = {&Cpu6502::addr_zpy, &Cpu6502::op_stx};
        t[0x98] = {&Cpu6502::addr_imp, &Cpu6502::op_tya};
        t[0x99] = {&Cpu6502::addr_aby, &Cpu6502::op_sta};
        t[0x9A] = {&Cpu6502::addr_imp, &Cpu6502::op_txs};
        t[0x9D] = {&Cpu6502::addr_abx, &Cpu6502::op_sta};
        t[0xA0] = {&Cpu6502::addr_imm, &Cpu6502::op_ldy};
        t[0xA1] = {&Cpu6502::addr_izx, &Cpu6502::op_lda};
        t[0xA2] = {&Cpu6502::addr_imm, &Cpu6502::op_ldx};
        t[0xA4] = {&Cpu6502::addr_zp, &Cpu6502::op_ldy};
        t[0xA5] = {&Cpu6502::addr_zp, &Cpu6502::op_lda};
        t[0xA6] = {&Cpu6502::addr_zp, &Cpu6502::op_ldx};
        t[0xA8] = {&Cpu6502::addr_imp, &Cpu6502::op_tay};
        t[0xA9] = {&Cpu6502::addr_imm, &Cpu6502::op_lda};
        t[0xAA] = {&Cpu6502::addr_imp, &Cpu6502::op_tax};
        t[0xAC] = {&Cpu6502::addr_abs, &Cpu6502::op_ldy};
        t[0xAD] = {&Cpu6502::addr_abs, &Cpu6502::op_lda};
        t[0xAE] = {&Cpu6502::addr_abs, &Cpu6502::op_ldx};
        t[0xB0] = {&Cpu6502::addr_rel, &Cpu6502::op_bcs};
        t[0xB1] = {&Cpu6502::addr_izy, &Cpu6502::op_lda};
        t[0xB4] = {&Cpu6502::addr_zpx, &Cpu6502::op_ldy};
        t[0xB5] = {&Cpu6502::addr_zpx, &Cpu6502::op_lda};
        t[0xB6] = {&Cpu6502::addr_zpy, &Cpu6502::op_ldx};
        t[0xB8] = {&Cpu6502::addr_imp, &Cpu6502::op_clv};
        t[0xB9] = {&Cpu6502::addr_aby, &Cpu6502::op_lda};
        t[0xBA] = {&Cpu6502::addr_imp, &Cpu6502::op_tsx};
        t[0xBC] = {&Cpu6502::addr_abx, &Cpu6502::op_ldy};
        t[0xBD] = {&Cpu6502::addr_abx, &Cpu6502::op_lda};
        t[0xBE] = {&Cpu6502::addr_aby, &Cpu6502::op_ldx};
        t[0xC0] = {&Cpu6502::addr_imm, &Cpu6502::op_cpy};
        t[0xC1] = {&Cpu6502::addr_izx, &Cpu6502::op_cmp};
        t[0xC4] = {&Cpu6502::addr_zp, &Cpu6502::op_cpy};
        t[0xC5] = {&Cpu6502::addr_zp, &Cpu6502::op_cmp};
        t[0xC6] = {&Cpu6502::addr_zp, &Cpu6502::op_dec};
        t[0xC8] = {&Cpu6502::addr_imp, &Cpu6502::op_iny};
        t[0xC9] = {&Cpu6502::addr_imm, &Cpu6502::op_cmp};
        t[0xCA] = {&Cpu6502::addr_imp, &Cpu6502::op_dex};
        t[0xCC] = {&Cpu6502::addr_abs, &Cpu6502::op_cpy};
        t[0xCD] = {&Cpu6502::addr_abs, &Cpu6502::op_cmp};
        t[0xCE] = {&Cpu6502::addr_abs, &Cpu6502::op_dec};
        t[0xD0] = {&Cpu6502::addr_rel, &Cpu6502::op_bne};
        t[0xD1] = {&Cpu6502::addr_izy, &Cpu6502::op_cmp};
        t[0xD5] = {&Cpu6502::addr_zpx, &Cpu6502::op_cmp};
        t[0xD6] = {&Cpu6502::addr_zpx, &Cpu6502::op_dec};
        t[0xD8] = {&Cpu6502::addr_imp, &Cpu6502::op_cld};
        t[0xD9] = {&Cpu6502::addr_aby, &Cpu6502::op_cmp};
        t[0xDD] = {&Cpu6502::addr_abx, &Cpu6502::op_cmp};
        t[0xDE] = {&Cpu6502::addr_abx, &Cpu6502::op_dec};
        t[0xE0] = {&Cpu6502::addr_imm, &Cpu6502::op_cpx};
        t[0xE1] = {&Cpu6502::addr_izx, &Cpu6502::op_sbc};
        t[0xE4] = {&Cpu6502::addr_zp, &Cpu6502::op_cpx};
        t[0xE5] = {&Cpu6502::addr_zp, &Cpu6502::op_sbc};
        t[0xE6] = {&Cpu6502::addr_zp, &Cpu6502::op_inc};
        t[0xE8] = {&Cpu6502::addr_imp, &Cpu6502::op_inx};
        t[0xE9] = {&Cpu6502::addr_imm, &Cpu6502::op_sbc};
        t[0xEA] = {&Cpu6502::addr_imp, &Cpu6502::op_nop};
        t[0xEC] = {&Cpu6502::addr_abs, &Cpu6502::op_cpx};
        t[0xED] = {&Cpu6502::addr_abs, &Cpu6502::op_sbc};
        t[0xEE] = {&Cpu6502::addr_abs, &Cpu6502::op_inc};
        t[0xF0] = {&Cpu6502::addr_rel, &Cpu6502::op_beq};
        t[0xF1] = {&Cpu6502::addr_izy, &Cpu6502::op_sbc};
        t[0xF5] = {&Cpu6502::addr_zpx, &Cpu6502::op_sbc};
        t[0xF6] = {&Cpu6502::addr_zpx, &Cpu6502::op_inc};
        t[0xF8] = {&Cpu6502::addr_imp, &Cpu6502::op_sed};
        t[0xF9] = {&Cpu6502::addr_aby, &Cpu6502::op_sbc};
        t[0xFD] = {&Cpu6502::addr_abx, &Cpu6502::op_sbc};
        t[0xFE] = {&Cpu6502::addr_abx, &Cpu6502::op_inc};

        return t;
    }

    const Cpu6502::Table Cpu6502::TABLE = Cpu6502::build_table();

    // --- control ---------------------------------------------------------------

    void Cpu6502::reset()
    {
        regs_.a = 0;
        regs_.x = 0;
        regs_.y = 0;
        regs_.s = 0xFD;
        regs_.p = FLAG_U | FLAG_B;
        regs_.pc = read16(RESET_VECTOR);
    }

    void Cpu6502::step()
    {
        const uint8_t opcode = read(regs_.pc++);
        EMU_LOGV(TAG, "%04X: %02X", static_cast<unsigned>(regs_.pc - 1), opcode);
        const Instr &i = TABLE[opcode];
        const uint16_t src = (this->*i.addr)();
        (this->*i.op)(src);
    }

    void Cpu6502::irq()
    {
        if (!flag(FLAG_I))
            interrupt(IRQ_VECTOR, static_cast<uint8_t>((regs_.p & ~FLAG_B) | FLAG_U));
    }

    void Cpu6502::nmi()
    {
        interrupt(NMI_VECTOR, static_cast<uint8_t>((regs_.p & ~FLAG_B) | FLAG_U));
    }

    void Cpu6502::interrupt(uint16_t vector, uint8_t pushed_p)
    {
        push(lo8(regs_.pc >> 8));
        push(lo8(regs_.pc));
        push(pushed_p);
        set_flag(FLAG_I, true);
        regs_.pc = read16(vector);
    }

    // --- helpers ---------------------------------------------------------------

    uint16_t Cpu6502::read16(uint16_t addr)
    {
        const unsigned lo = read(addr);
        const unsigned hi = read(static_cast<uint16_t>(addr + 1));
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    void Cpu6502::set_flag(uint8_t f, bool on)
    {
        regs_.p = on ? static_cast<uint8_t>(regs_.p | f) : static_cast<uint8_t>(regs_.p & ~f);
    }

    void Cpu6502::set_nz(uint8_t v)
    {
        set_flag(FLAG_N, v & 0x80);
        set_flag(FLAG_Z, v == 0);
    }

    void Cpu6502::push(uint8_t v)
    {
        write(static_cast<uint16_t>(0x0100 + regs_.s), v);
        regs_.s--;
    }

    uint8_t Cpu6502::pop()
    {
        regs_.s++;
        return read(static_cast<uint16_t>(0x0100 + regs_.s));
    }

    // --- addressing modes ------------------------------------------------------

    uint16_t Cpu6502::addr_acc()
    {
        return 0;
    }

    uint16_t Cpu6502::addr_imp()
    {
        return 0;
    }

    uint16_t Cpu6502::addr_imm()
    {
        return regs_.pc++;
    }

    uint16_t Cpu6502::addr_abs()
    {
        const unsigned lo = read(regs_.pc++);
        const unsigned hi = read(regs_.pc++);
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    uint16_t Cpu6502::addr_zp()
    {
        return read(regs_.pc++);
    }

    uint16_t Cpu6502::addr_zpx()
    {
        return lo8(read(regs_.pc++) + regs_.x);
    }

    uint16_t Cpu6502::addr_zpy()
    {
        return lo8(read(regs_.pc++) + regs_.y);
    }

    uint16_t Cpu6502::addr_abx()
    {
        return static_cast<uint16_t>(addr_abs() + regs_.x);
    }

    uint16_t Cpu6502::addr_aby()
    {
        return static_cast<uint16_t>(addr_abs() + regs_.y);
    }

    uint16_t Cpu6502::addr_rel()
    {
        const auto offset = static_cast<int8_t>(read(regs_.pc++));
        return static_cast<uint16_t>(regs_.pc + offset);
    }

    uint16_t Cpu6502::addr_izx()
    {
        const uint8_t zp = lo8(read(regs_.pc++) + regs_.x);
        const unsigned lo = read(zp);
        const unsigned hi = read(lo8(zp + 1u)); // the pointer wraps within zero page
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    uint16_t Cpu6502::addr_izy()
    {
        const uint8_t zp = read(regs_.pc++);
        const unsigned lo = read(zp);
        const unsigned hi = read(lo8(zp + 1u));
        return static_cast<uint16_t>((lo | (hi << 8)) + regs_.y);
    }

    uint16_t Cpu6502::addr_ind()
    {
        const uint16_t ptr = addr_abs();
        const unsigned lo = read(ptr);
        // NMOS: the high byte comes from the same page, so ($10FF) reads $1000.
        const unsigned hi = read(static_cast<uint16_t>((ptr & 0xFF00) | lo8(ptr + 1u)));
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    // --- operations ------------------------------------------------------------

    void Cpu6502::op_adc(uint16_t src)
    {
        const uint8_t m = read(src);
        const uint8_t a = regs_.a;
        const unsigned carry = flag(FLAG_C) ? 1 : 0;
        unsigned tmp = m + a + carry;
        set_flag(FLAG_Z, (tmp & 0xFF) == 0);
        if (flag(FLAG_D))
        {
            if (((a & 0xFu) + (m & 0xFu) + carry) > 9)
                tmp += 6;
            set_flag(FLAG_N, tmp & 0x80);
            set_flag(FLAG_V, !((a ^ m) & 0x80) && ((a ^ tmp) & 0x80));
            if (tmp > 0x99)
                tmp += 96;
            set_flag(FLAG_C, tmp > 0x99);
        }
        else
        {
            set_flag(FLAG_N, tmp & 0x80);
            set_flag(FLAG_V, !((a ^ m) & 0x80) && ((a ^ tmp) & 0x80));
            set_flag(FLAG_C, tmp > 0xFF);
        }
        regs_.a = lo8(tmp);
    }

    void Cpu6502::op_sbc(uint16_t src)
    {
        const uint8_t m = read(src);
        const uint8_t a = regs_.a;
        // int, not unsigned: the decimal adjust below compares a signed
        // difference, and (a & 0x0F) - borrow may be -1.
        const int borrow = flag(FLAG_C) ? 0 : 1;
        unsigned tmp = static_cast<unsigned>(a - m - borrow);
        set_flag(FLAG_N, tmp & 0x80);
        set_flag(FLAG_Z, (tmp & 0xFF) == 0);
        set_flag(FLAG_V, ((a ^ tmp) & 0x80) && ((a ^ m) & 0x80));
        if (flag(FLAG_D))
        {
            if (((a & 0x0F) - borrow) < (m & 0x0F))
                tmp -= 6;
            if (tmp > 0x99)
                tmp -= 0x60;
        }
        set_flag(FLAG_C, tmp < 0x100);
        regs_.a = lo8(tmp);
    }

    void Cpu6502::op_and(uint16_t src)
    {
        regs_.a = static_cast<uint8_t>(regs_.a & read(src));
        set_nz(regs_.a);
    }

    void Cpu6502::op_eor(uint16_t src)
    {
        regs_.a = static_cast<uint8_t>(regs_.a ^ read(src));
        set_nz(regs_.a);
    }

    void Cpu6502::op_ora(uint16_t src)
    {
        regs_.a = static_cast<uint8_t>(regs_.a | read(src));
        set_nz(regs_.a);
    }

    void Cpu6502::op_bit(uint16_t src)
    {
        const uint8_t m = read(src);
        // N and V come from the operand; the live P keeps U and B set.
        regs_.p = static_cast<uint8_t>((regs_.p & 0x3F) | (m & 0xC0) | FLAG_U | FLAG_B);
        set_flag(FLAG_Z, (m & regs_.a) == 0);
    }

    void Cpu6502::op_cmp(uint16_t src)
    {
        const auto tmp = static_cast<unsigned>(regs_.a - read(src));
        set_flag(FLAG_C, tmp < 0x100);
        set_flag(FLAG_N, tmp & 0x80);
        set_flag(FLAG_Z, (tmp & 0xFF) == 0);
    }

    void Cpu6502::op_cpx(uint16_t src)
    {
        const auto tmp = static_cast<unsigned>(regs_.x - read(src));
        set_flag(FLAG_C, tmp < 0x100);
        set_flag(FLAG_N, tmp & 0x80);
        set_flag(FLAG_Z, (tmp & 0xFF) == 0);
    }

    void Cpu6502::op_cpy(uint16_t src)
    {
        const auto tmp = static_cast<unsigned>(regs_.y - read(src));
        set_flag(FLAG_C, tmp < 0x100);
        set_flag(FLAG_N, tmp & 0x80);
        set_flag(FLAG_Z, (tmp & 0xFF) == 0);
    }

    // Shifts and rotates write back once (NMOS silicon writes twice; Phase 3/4).

    void Cpu6502::op_asl(uint16_t src)
    {
        const uint8_t m = read(src);
        set_flag(FLAG_C, m & 0x80);
        const uint8_t r = lo8(m << 1u);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_asl_acc(uint16_t)
    {
        set_flag(FLAG_C, regs_.a & 0x80);
        regs_.a = lo8(regs_.a << 1u);
        set_nz(regs_.a);
    }

    void Cpu6502::op_lsr(uint16_t src)
    {
        const uint8_t m = read(src);
        set_flag(FLAG_C, m & 0x01);
        const uint8_t r = static_cast<uint8_t>(m >> 1);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_lsr_acc(uint16_t)
    {
        set_flag(FLAG_C, regs_.a & 0x01);
        regs_.a = static_cast<uint8_t>(regs_.a >> 1);
        set_nz(regs_.a);
    }

    void Cpu6502::op_rol(uint16_t src)
    {
        const unsigned m = (read(src) << 1u) | (flag(FLAG_C) ? 1u : 0u);
        set_flag(FLAG_C, m > 0xFF);
        const uint8_t r = lo8(m);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_rol_acc(uint16_t)
    {
        const unsigned m = (regs_.a << 1u) | (flag(FLAG_C) ? 1u : 0u);
        set_flag(FLAG_C, m > 0xFF);
        regs_.a = lo8(m);
        set_nz(regs_.a);
    }

    void Cpu6502::op_ror(uint16_t src)
    {
        const unsigned m = read(src) | (flag(FLAG_C) ? 0x100u : 0u);
        set_flag(FLAG_C, m & 0x01);
        const uint8_t r = lo8(m >> 1);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_ror_acc(uint16_t)
    {
        const unsigned m = regs_.a | (flag(FLAG_C) ? 0x100u : 0u);
        set_flag(FLAG_C, m & 0x01);
        regs_.a = lo8(m >> 1);
        set_nz(regs_.a);
    }

    void Cpu6502::op_dec(uint16_t src)
    {
        const uint8_t r = lo8(read(src) - 1u);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_inc(uint16_t src)
    {
        const uint8_t r = lo8(read(src) + 1u);
        set_nz(r);
        write(src, r);
    }

    void Cpu6502::op_dex(uint16_t)
    {
        set_nz(--regs_.x);
    }

    void Cpu6502::op_dey(uint16_t)
    {
        set_nz(--regs_.y);
    }

    void Cpu6502::op_inx(uint16_t)
    {
        set_nz(++regs_.x);
    }

    void Cpu6502::op_iny(uint16_t)
    {
        set_nz(++regs_.y);
    }

    void Cpu6502::op_bcc(uint16_t src)
    {
        if (!flag(FLAG_C))
            regs_.pc = src;
    }

    void Cpu6502::op_bcs(uint16_t src)
    {
        if (flag(FLAG_C))
            regs_.pc = src;
    }

    void Cpu6502::op_beq(uint16_t src)
    {
        if (flag(FLAG_Z))
            regs_.pc = src;
    }

    void Cpu6502::op_bne(uint16_t src)
    {
        if (!flag(FLAG_Z))
            regs_.pc = src;
    }

    void Cpu6502::op_bmi(uint16_t src)
    {
        if (flag(FLAG_N))
            regs_.pc = src;
    }

    void Cpu6502::op_bpl(uint16_t src)
    {
        if (!flag(FLAG_N))
            regs_.pc = src;
    }

    void Cpu6502::op_bvc(uint16_t src)
    {
        if (!flag(FLAG_V))
            regs_.pc = src;
    }

    void Cpu6502::op_bvs(uint16_t src)
    {
        if (flag(FLAG_V))
            regs_.pc = src;
    }

    void Cpu6502::op_brk(uint16_t)
    {
        regs_.pc++; // BRK skips its padding byte
        interrupt(IRQ_VECTOR, static_cast<uint8_t>(regs_.p | FLAG_U | FLAG_B));
    }

    void Cpu6502::op_jmp(uint16_t src)
    {
        regs_.pc = src;
    }

    void Cpu6502::op_jsr(uint16_t src)
    {
        regs_.pc--; // pushes the address of its own last byte
        push(lo8(regs_.pc >> 8));
        push(lo8(regs_.pc));
        regs_.pc = src;
    }

    void Cpu6502::op_rts(uint16_t)
    {
        const unsigned lo = pop();
        const unsigned hi = pop();
        regs_.pc = static_cast<uint16_t>((lo | (hi << 8)) + 1);
    }

    void Cpu6502::op_rti(uint16_t)
    {
        regs_.p = static_cast<uint8_t>(pop() | FLAG_U | FLAG_B);
        const unsigned lo = pop();
        const unsigned hi = pop();
        regs_.pc = static_cast<uint16_t>(lo | (hi << 8));
    }

    void Cpu6502::op_clc(uint16_t)
    {
        set_flag(FLAG_C, false);
    }

    void Cpu6502::op_cld(uint16_t)
    {
        set_flag(FLAG_D, false);
    }

    void Cpu6502::op_cli(uint16_t)
    {
        set_flag(FLAG_I, false);
    }

    void Cpu6502::op_clv(uint16_t)
    {
        set_flag(FLAG_V, false);
    }

    void Cpu6502::op_sec(uint16_t)
    {
        set_flag(FLAG_C, true);
    }

    void Cpu6502::op_sed(uint16_t)
    {
        set_flag(FLAG_D, true);
    }

    void Cpu6502::op_sei(uint16_t)
    {
        set_flag(FLAG_I, true);
    }

    void Cpu6502::op_lda(uint16_t src)
    {
        regs_.a = read(src);
        set_nz(regs_.a);
    }

    void Cpu6502::op_ldx(uint16_t src)
    {
        regs_.x = read(src);
        set_nz(regs_.x);
    }

    void Cpu6502::op_ldy(uint16_t src)
    {
        regs_.y = read(src);
        set_nz(regs_.y);
    }

    void Cpu6502::op_sta(uint16_t src)
    {
        write(src, regs_.a);
    }

    void Cpu6502::op_stx(uint16_t src)
    {
        write(src, regs_.x);
    }

    void Cpu6502::op_sty(uint16_t src)
    {
        write(src, regs_.y);
    }

    void Cpu6502::op_tax(uint16_t)
    {
        regs_.x = regs_.a;
        set_nz(regs_.x);
    }

    void Cpu6502::op_tay(uint16_t)
    {
        regs_.y = regs_.a;
        set_nz(regs_.y);
    }

    void Cpu6502::op_tsx(uint16_t)
    {
        regs_.x = regs_.s;
        set_nz(regs_.x);
    }

    void Cpu6502::op_txa(uint16_t)
    {
        regs_.a = regs_.x;
        set_nz(regs_.a);
    }

    void Cpu6502::op_txs(uint16_t)
    {
        regs_.s = regs_.x; // no flags
    }

    void Cpu6502::op_tya(uint16_t)
    {
        regs_.a = regs_.y;
        set_nz(regs_.a);
    }

    void Cpu6502::op_pha(uint16_t)
    {
        push(regs_.a);
    }

    void Cpu6502::op_php(uint16_t)
    {
        push(static_cast<uint8_t>(regs_.p | FLAG_U | FLAG_B));
    }

    void Cpu6502::op_pla(uint16_t)
    {
        regs_.a = pop();
        set_nz(regs_.a);
    }

    void Cpu6502::op_plp(uint16_t)
    {
        regs_.p = static_cast<uint8_t>(pop() | FLAG_U | FLAG_B);
    }

    void Cpu6502::op_nop(uint16_t)
    {
    }
} // namespace ooe::emu
