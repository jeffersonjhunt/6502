/**
 * @file cpu6502.cpp
 * @brief NMOS 6502 / W65C02S implementation, one bus access per cycle.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT).
 * Originally derived from gianlucag/mos6502 (MIT); see licenses/mos6502_LICENSE.
 *
 * The access patterns below were read off Harte's per-cycle fixtures, not
 * recalled from datasheets, and every one is checked by test_harte. Where
 * the two models differ it is almost always a dummy access:
 *
 *   indexed page cross   NMOS re-reads the un-carried address; WDC re-reads
 *                        the last operand byte (PC-1)
 *   read-modify-write    NMOS writes the old value back; WDC re-reads it
 *   ASL..ROR abs,X       WDC skips the extra cycle when no page is crossed
 *   JMP ($xxFF)          NMOS wraps within the page; WDC fixes it, but still
 *                        dummy-reads the wrapped address first (+1 cycle)
 *   ADC/SBC decimal      WDC takes one extra cycle, re-reading the operand
 *
 * Two WDC details come only from Harte and are worth confirming against the
 * board with the bus spy: the decimal extra cycle in immediate mode reads
 * $007F (ADC) or $0000 (SBC), and the $5C/$DC/$FC NOPs take 4 cycles.
 *
 * Decimal arithmetic follows Bruce Clark's "Decimal Mode" (6502.org),
 * appendix A: sequences 1-2 for ADC, 3 for NMOS SBC, 4 for 65C02 SBC.
 */

#include "cpu6502.h"

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        [[maybe_unused]] const char *TAG = "cpu";

        constexpr uint8_t lo8(unsigned v) { return static_cast<uint8_t>(v & 0xFF); }
        constexpr uint16_t stack(uint8_t s) { return static_cast<uint16_t>(0x0100 | s); }
        constexpr bool crosses(uint16_t a, uint16_t b) { return ((a ^ b) & 0xFF00) != 0; }
    } // namespace

    // --- bus ---------------------------------------------------------------

    uint8_t Cpu6502::read(uint16_t addr)
    {
        ++cycles_;
        const uint8_t v = bus_.read(addr);
        cycles_ += bus_.wait_states(); // held by RDY; traced once, on its last cycle
        if (observer_)
            observer_->on_cycle(BusCycle{cycles_, addr, v, false, sync_, vector_pull_});
        sync_ = false;
        return v;
    }

    void Cpu6502::write(uint16_t addr, uint8_t data)
    {
        ++cycles_;
        bus_.write(addr, data);
        cycles_ += bus_.wait_states();
        if (observer_)
            observer_->on_cycle(BusCycle{cycles_, addr, data, true, false, vector_pull_});
    }

    uint16_t Cpu6502::fetch16()
    {
        const unsigned lo = fetch();
        const unsigned hi = fetch();
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    void Cpu6502::push(uint8_t v)
    {
        write(stack(regs_.s), v);
        regs_.s--;
    }

    uint8_t Cpu6502::pull()
    {
        regs_.s++;
        return read(stack(regs_.s));
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

    // --- control -------------------------------------------------------------

    void Cpu6502::reset()
    {
        state_ = State::Running;
        read(regs_.pc);
        read(regs_.pc);
        for (int i = 0; i < 3; ++i) // the pushes of an interrupt, as reads
            read(stack(regs_.s--));
        regs_.p = static_cast<uint8_t>((regs_.p | FLAG_I | FLAG_U) & ~FLAG_B);
        if (cmos())
            set_flag(FLAG_D, false);
        vector_pull_ = true;
        bus_.vector_pull(true);
        const unsigned lo = read(RESET_VECTOR);
        const unsigned hi = read(RESET_VECTOR + 1);
        vector_pull_ = false;
        bus_.vector_pull(false);
        regs_.pc = static_cast<uint16_t>(lo | (hi << 8));
    }

    unsigned Cpu6502::step()
    {
        if (state_ != State::Running)
        {
            ++cycles_; // WAI/STP: the clock runs, the bus is idle
            bus_.idle();
            const unsigned held = bus_.wait_states(); // a bus master may use the idle bus
            cycles_ += held;
            return 1 + held;
        }
        const uint64_t start = cycles_;
        sync_ = true;
        const uint8_t opcode = fetch();
        EMU_LOGV(TAG, "%04X: %02X", static_cast<unsigned>(regs_.pc - 1), opcode);
        execute(opcode);
        return static_cast<unsigned>(cycles_ - start);
    }

    void Cpu6502::irq()
    {
        if (state_ == State::Stopped)
            return;
        state_ = State::Running; // a WAI resumes even when the IRQ is masked
        if (flag(FLAG_I))
            return;
        read(regs_.pc);
        read(regs_.pc);
        interrupt(IRQ_VECTOR, static_cast<uint8_t>((regs_.p & ~FLAG_B) | FLAG_U));
    }

    void Cpu6502::nmi()
    {
        if (state_ == State::Stopped)
            return;
        state_ = State::Running;
        read(regs_.pc);
        read(regs_.pc);
        interrupt(NMI_VECTOR, static_cast<uint8_t>((regs_.p & ~FLAG_B) | FLAG_U));
    }

    void Cpu6502::interrupt(uint16_t vector, uint8_t pushed_p)
    {
        push(lo8(regs_.pc >> 8));
        push(lo8(regs_.pc));
        push(pushed_p);
        set_flag(FLAG_I, true);
        if (cmos())
            set_flag(FLAG_D, false);
        vector_pull_ = true;
        bus_.vector_pull(true);
        const unsigned lo = read(vector);
        const unsigned hi = read(static_cast<uint16_t>(vector + 1));
        vector_pull_ = false;
        bus_.vector_pull(false);
        regs_.pc = static_cast<uint16_t>(lo | (hi << 8));
    }

    // --- addressing ----------------------------------------------------------

    uint16_t Cpu6502::abs_indexed(uint8_t index, bool always_extra)
    {
        const uint16_t base = fetch16();
        const auto ea = static_cast<uint16_t>(base + index);
        if (always_extra || crosses(base, ea))
        {
            if (cmos())
                read(static_cast<uint16_t>(regs_.pc - 1));
            else
                read(static_cast<uint16_t>((base & 0xFF00) | (ea & 0xFF)));
        }
        return ea;
    }

    uint16_t Cpu6502::izy(bool always_extra)
    {
        const uint8_t zp = fetch();
        const unsigned lo = read(zp);
        const unsigned hi = read(lo8(zp + 1u));
        const auto base = static_cast<uint16_t>(lo | (hi << 8));
        const auto ea = static_cast<uint16_t>(base + regs_.y);
        if (always_extra || crosses(base, ea))
        {
            if (cmos())
                read(static_cast<uint16_t>(regs_.pc - 1));
            else
                read(static_cast<uint16_t>((base & 0xFF00) | (ea & 0xFF)));
        }
        return ea;
    }

    uint16_t Cpu6502::ea_read(Mode m)
    {
        switch (m)
        {
        case Mode::Imm:
            return regs_.pc++;
        case Mode::Zp:
            return fetch();
        case Mode::Zpx:
        case Mode::Zpy:
        {
            const uint8_t zp = fetch();
            read(zp); // the un-indexed address, on both models
            return lo8(zp + (m == Mode::Zpx ? regs_.x : regs_.y));
        }
        case Mode::Abs:
            return fetch16();
        case Mode::Abx:
            return abs_indexed(regs_.x, false);
        case Mode::Aby:
            return abs_indexed(regs_.y, false);
        case Mode::Izx:
        {
            const uint8_t zp = fetch();
            read(zp);
            const uint8_t ptr = lo8(zp + regs_.x);
            const unsigned lo = read(ptr);
            const unsigned hi = read(lo8(ptr + 1u)); // the pointer wraps in zero page
            return static_cast<uint16_t>(lo | (hi << 8));
        }
        case Mode::Izy:
            return izy(false);
        case Mode::Izp:
        {
            const uint8_t zp = fetch();
            const unsigned lo = read(zp);
            const unsigned hi = read(lo8(zp + 1u));
            return static_cast<uint16_t>(lo | (hi << 8));
        }
        }
        return 0;
    }

    uint16_t Cpu6502::ea_write(Mode m)
    {
        switch (m)
        {
        case Mode::Abx:
            return abs_indexed(regs_.x, true);
        case Mode::Aby:
            return abs_indexed(regs_.y, true);
        case Mode::Izy:
            return izy(true);
        default:
            return ea_read(m);
        }
    }

    // --- loads, stores, logic ------------------------------------------------

    void Cpu6502::load(uint8_t &reg, Mode m)
    {
        reg = read(ea_read(m));
        set_nz(reg);
    }

    void Cpu6502::store(Mode m, uint8_t v)
    {
        write(ea_write(m), v);
    }

    void Cpu6502::logic(Mode m, char op)
    {
        const uint8_t v = read(ea_read(m));
        if (op == '|')
            regs_.a = static_cast<uint8_t>(regs_.a | v);
        else if (op == '&')
            regs_.a = static_cast<uint8_t>(regs_.a & v);
        else
            regs_.a = static_cast<uint8_t>(regs_.a ^ v);
        set_nz(regs_.a);
    }

    void Cpu6502::compare(uint8_t reg, Mode m)
    {
        compare_value(reg, read(ea_read(m)));
    }

    void Cpu6502::compare_value(uint8_t reg, uint8_t v)
    {
        set_flag(FLAG_C, reg >= v);
        set_nz(lo8(reg - v + 0x100u));
    }

    void Cpu6502::bit(Mode m)
    {
        const uint8_t v = read(ea_read(m));
        set_flag(FLAG_Z, (regs_.a & v) == 0);
        if (m != Mode::Imm) // BIT # (W65C02S) affects Z only
        {
            set_flag(FLAG_N, v & 0x80);
            set_flag(FLAG_V, v & 0x40);
        }
    }

    // --- arithmetic ----------------------------------------------------------

    void Cpu6502::binary_add(uint8_t m)
    {
        const unsigned a = regs_.a;
        const unsigned sum = a + m + (flag(FLAG_C) ? 1u : 0u);
        set_flag(FLAG_C, sum > 0xFF);
        set_flag(FLAG_V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);
        regs_.a = lo8(sum);
        set_nz(regs_.a);
    }

    void Cpu6502::decimal_extra_cycle(Mode m, uint16_t ea, bool subtract)
    {
        // W65C02S only. Re-reads the operand; immediate mode reads a fixed
        // address instead (per Harte; see the file comment).
        if (m == Mode::Imm)
            read(subtract ? 0x0000 : 0x007F);
        else
            read(ea);
    }

    void Cpu6502::adc(Mode m)
    {
        const uint16_t ea = ea_read(m);
        if (add(read(ea)) && cmos())
            decimal_extra_cycle(m, ea, false);
    }

    bool Cpu6502::add(uint8_t v)
    {
        if (!flag(FLAG_D))
        {
            binary_add(v);
            return false;
        }
        const int a = regs_.a, b = v, c = flag(FLAG_C) ? 1 : 0;
        int al = (a & 0x0F) + (b & 0x0F) + c; // Clark seq. 1/2
        if (al >= 0x0A)
            al = ((al + 0x06) & 0x0F) + 0x10;
        const int seq2 = static_cast<int8_t>(a & 0xF0) + static_cast<int8_t>(b & 0xF0) + al;
        int sum = (a & 0xF0) + (b & 0xF0) + al;
        if (sum >= 0xA0)
            sum += 0x60;
        set_flag(FLAG_C, sum >= 0x100);
        set_flag(FLAG_V, seq2 < -128 || seq2 > 127);
        regs_.a = lo8(static_cast<unsigned>(sum));
        if (cmos())
        {
            set_nz(regs_.a);
        }
        else
        {
            set_flag(FLAG_N, seq2 & 0x80);
            set_flag(FLAG_Z, ((a + b + c) & 0xFF) == 0); // NMOS: Z from the binary sum
        }
        return true;
    }

    void Cpu6502::sbc(Mode m)
    {
        const uint16_t ea = ea_read(m);
        if (subtract(read(ea)) && cmos())
            decimal_extra_cycle(m, ea, true);
    }

    bool Cpu6502::subtract(uint8_t v)
    {
        const int a = regs_.a, b = v, c = flag(FLAG_C) ? 1 : 0;
        if (!flag(FLAG_D))
        {
            binary_add(lo8(~static_cast<unsigned>(v)));
            return false;
        }
        // Carry and overflow are the binary subtraction's on both models.
        const unsigned bin = static_cast<unsigned>(a - b - 1 + c);
        set_flag(FLAG_C, a - b - 1 + c >= 0);
        set_flag(FLAG_V, ((static_cast<unsigned>(a) ^ bin) & (static_cast<unsigned>(a) ^ static_cast<unsigned>(b)) & 0x80) != 0);
        int al = (a & 0x0F) - (b & 0x0F) + c - 1;
        int r = 0;
        if (cmos())
        {
            r = a - b + c - 1; // Clark seq. 4
            if (r < 0)
                r -= 0x60;
            if (al < 0)
                r -= 0x06;
        }
        else
        {
            if (al < 0) // Clark seq. 3
                al = ((al - 0x06) & 0x0F) - 0x10;
            r = (a & 0xF0) - (b & 0xF0) + al;
            if (r < 0)
                r -= 0x60;
        }
        regs_.a = lo8(static_cast<unsigned>(r));
        set_nz(cmos() ? regs_.a : lo8(bin)); // NMOS: N and Z from the binary difference
        return true;
    }

    // --- read-modify-write ---------------------------------------------------

    uint8_t Cpu6502::asl(uint8_t v)
    {
        set_flag(FLAG_C, v & 0x80);
        const uint8_t r = lo8(static_cast<unsigned>(v) << 1);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::lsr(uint8_t v)
    {
        set_flag(FLAG_C, v & 0x01);
        const auto r = static_cast<uint8_t>(v >> 1);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::rol(uint8_t v)
    {
        const uint8_t r = lo8(static_cast<unsigned>(v) << 1 | (flag(FLAG_C) ? 1u : 0u));
        set_flag(FLAG_C, v & 0x80);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::ror(uint8_t v)
    {
        const uint8_t r = lo8((v >> 1) | (flag(FLAG_C) ? 0x80u : 0u));
        set_flag(FLAG_C, v & 0x01);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::inc(uint8_t v)
    {
        const uint8_t r = lo8(v + 1u);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::dec(uint8_t v)
    {
        const uint8_t r = lo8(v - 1u);
        set_nz(r);
        return r;
    }

    uint8_t Cpu6502::tsb(uint8_t v)
    {
        set_flag(FLAG_Z, (regs_.a & v) == 0);
        return static_cast<uint8_t>(v | regs_.a);
    }

    uint8_t Cpu6502::trb(uint8_t v)
    {
        set_flag(FLAG_Z, (regs_.a & v) == 0);
        return static_cast<uint8_t>(v & ~regs_.a);
    }

    uint8_t Cpu6502::rmw(Mode m, Rmw op)
    {
        uint16_t ea = 0;
        if (m == Mode::Abx)
        {
            // WDC: shifts and rotates skip the extra cycle without a page
            // cross; INC and DEC never do. NMOS always takes it.
            const bool inc_dec = op == &Cpu6502::inc || op == &Cpu6502::dec;
            ea = abs_indexed(regs_.x, !cmos() || inc_dec);
        }
        else
        {
            ea = ea_write(m);
        }
        const uint8_t v = read(ea);
        if (cmos())
            read(ea); // WDC re-reads
        else
            write(ea, v); // NMOS writes the old value back first
        const uint8_t r = (this->*op)(v);
        write(ea, r);
        return r;
    }

    void Cpu6502::rmw_acc(Rmw op)
    {
        implied();
        regs_.a = (this->*op)(regs_.a);
    }

    void Cpu6502::rmb_smb(unsigned bitno, bool set)
    {
        const uint8_t zp = fetch();
        const uint8_t v = read(zp);
        read(zp);
        const auto mask = static_cast<uint8_t>(1u << bitno);
        write(zp, set ? static_cast<uint8_t>(v | mask) : static_cast<uint8_t>(v & ~mask));
    }

    // --- branches and jumps ----------------------------------------------------

    void Cpu6502::branch(bool taken)
    {
        const auto offset = static_cast<int8_t>(fetch());
        if (!taken)
            return;
        read(regs_.pc);
        const auto target = static_cast<uint16_t>(regs_.pc + offset);
        if (crosses(regs_.pc, target))
            read(static_cast<uint16_t>((regs_.pc & 0xFF00) | (target & 0xFF)));
        regs_.pc = target;
    }

    void Cpu6502::bbr_bbs(unsigned bitno, bool set)
    {
        const uint8_t zp = fetch();
        const uint8_t v = read(zp);
        read(zp);
        const auto offset = static_cast<int8_t>(fetch());
        if (((v >> bitno) & 1u) != (set ? 1u : 0u))
            return;
        read(regs_.pc);
        const auto target = static_cast<uint16_t>(regs_.pc + offset);
        if (crosses(regs_.pc, target))
            read(regs_.pc);
        regs_.pc = target;
    }

    // --- stack -----------------------------------------------------------------

    void Cpu6502::push_reg(uint8_t v)
    {
        implied();
        push(v);
    }

    uint8_t Cpu6502::pull_reg()
    {
        implied();
        read(stack(regs_.s));
        return pull();
    }

    // --- decode ----------------------------------------------------------------

    bool Cpu6502::execute_cmos(uint8_t op)
    {
        Registers &r = regs_;
        const unsigned lo = op & 0x0F, hi = op >> 4;

        if (lo == 0x7) // RMB0-7 / SMB0-7
        {
            rmb_smb(hi & 7, hi >= 8);
            return true;
        }
        if (lo == 0xF) // BBR0-7 / BBS0-7
        {
            bbr_bbs(hi & 7, hi >= 8);
            return true;
        }
        if ((lo == 0x3 || lo == 0xB) && op != 0xCB && op != 0xDB)
            return true; // one-byte, one-cycle NOP

        switch (op)
        {
        case 0x04: rmw(Mode::Zp, &Cpu6502::tsb); return true;
        case 0x0C: rmw(Mode::Abs, &Cpu6502::tsb); return true;
        case 0x14: rmw(Mode::Zp, &Cpu6502::trb); return true;
        case 0x1C: rmw(Mode::Abs, &Cpu6502::trb); return true;
        case 0x12: logic(Mode::Izp, '|'); return true;
        case 0x32: logic(Mode::Izp, '&'); return true;
        case 0x52: logic(Mode::Izp, '^'); return true;
        case 0x72: adc(Mode::Izp); return true;
        case 0x92: store(Mode::Izp, r.a); return true;
        case 0xB2: load(r.a, Mode::Izp); return true;
        case 0xD2: compare(r.a, Mode::Izp); return true;
        case 0xF2: sbc(Mode::Izp); return true;
        case 0x1A: rmw_acc(&Cpu6502::inc); return true;
        case 0x3A: rmw_acc(&Cpu6502::dec); return true;
        case 0x34: bit(Mode::Zpx); return true;
        case 0x3C: bit(Mode::Abx); return true;
        case 0x89: bit(Mode::Imm); return true;
        case 0x5A: push_reg(r.y); return true;
        case 0xDA: push_reg(r.x); return true;
        case 0x7A: r.y = pull_reg(); set_nz(r.y); return true;
        case 0xFA: r.x = pull_reg(); set_nz(r.x); return true;
        case 0x64: store(Mode::Zp, 0); return true;
        case 0x74: store(Mode::Zpx, 0); return true;
        case 0x9C: store(Mode::Abs, 0); return true;
        case 0x9E: store(Mode::Abx, 0); return true;
        case 0x80: branch(true); return true; // BRA
        case 0x7C: // JMP (abs,X)
        {
            const uint16_t base = fetch16();
            read(static_cast<uint16_t>(r.pc - 2)); // the operand's first byte
            const auto ptr = static_cast<uint16_t>(base + r.x);
            const unsigned plo = read(ptr);
            const unsigned phi = read(static_cast<uint16_t>(ptr + 1));
            r.pc = static_cast<uint16_t>(plo | (phi << 8));
            return true;
        }
        case 0xCB: // WAI
            implied();
            read(r.pc);
            state_ = State::Waiting;
            return true;
        case 0xDB: // STP
            implied();
            read(r.pc);
            state_ = State::Stopped;
            return true;
        // Undefined: NOPs with the size and bus pattern WDC's die gives them.
        case 0x02: case 0x22: case 0x42: case 0x62: case 0x82: case 0xC2: case 0xE2:
            fetch();
            return true;
        case 0x44:
            read(fetch());
            return true;
        case 0x54: case 0xD4: case 0xF4:
            read(ea_read(Mode::Zpx));
            return true;
        case 0x5C: case 0xDC: case 0xFC:
            fetch16();
            read(static_cast<uint16_t>(r.pc - 1));
            return true;
        default:
            return false;
        }
    }

    void Cpu6502::execute(uint8_t op)
    {
        if (cmos() && execute_cmos(op))
            return;

        Registers &r = regs_;
        switch (op)
        {
        // loads and stores
        case 0xA9: load(r.a, Mode::Imm); break;
        case 0xA5: load(r.a, Mode::Zp); break;
        case 0xB5: load(r.a, Mode::Zpx); break;
        case 0xAD: load(r.a, Mode::Abs); break;
        case 0xBD: load(r.a, Mode::Abx); break;
        case 0xB9: load(r.a, Mode::Aby); break;
        case 0xA1: load(r.a, Mode::Izx); break;
        case 0xB1: load(r.a, Mode::Izy); break;
        case 0xA2: load(r.x, Mode::Imm); break;
        case 0xA6: load(r.x, Mode::Zp); break;
        case 0xB6: load(r.x, Mode::Zpy); break;
        case 0xAE: load(r.x, Mode::Abs); break;
        case 0xBE: load(r.x, Mode::Aby); break;
        case 0xA0: load(r.y, Mode::Imm); break;
        case 0xA4: load(r.y, Mode::Zp); break;
        case 0xB4: load(r.y, Mode::Zpx); break;
        case 0xAC: load(r.y, Mode::Abs); break;
        case 0xBC: load(r.y, Mode::Abx); break;
        case 0x85: store(Mode::Zp, r.a); break;
        case 0x95: store(Mode::Zpx, r.a); break;
        case 0x8D: store(Mode::Abs, r.a); break;
        case 0x9D: store(Mode::Abx, r.a); break;
        case 0x99: store(Mode::Aby, r.a); break;
        case 0x81: store(Mode::Izx, r.a); break;
        case 0x91: store(Mode::Izy, r.a); break;
        case 0x86: store(Mode::Zp, r.x); break;
        case 0x96: store(Mode::Zpy, r.x); break;
        case 0x8E: store(Mode::Abs, r.x); break;
        case 0x84: store(Mode::Zp, r.y); break;
        case 0x94: store(Mode::Zpx, r.y); break;
        case 0x8C: store(Mode::Abs, r.y); break;

        // logic and arithmetic
        case 0x09: logic(Mode::Imm, '|'); break;
        case 0x05: logic(Mode::Zp, '|'); break;
        case 0x15: logic(Mode::Zpx, '|'); break;
        case 0x0D: logic(Mode::Abs, '|'); break;
        case 0x1D: logic(Mode::Abx, '|'); break;
        case 0x19: logic(Mode::Aby, '|'); break;
        case 0x01: logic(Mode::Izx, '|'); break;
        case 0x11: logic(Mode::Izy, '|'); break;
        case 0x29: logic(Mode::Imm, '&'); break;
        case 0x25: logic(Mode::Zp, '&'); break;
        case 0x35: logic(Mode::Zpx, '&'); break;
        case 0x2D: logic(Mode::Abs, '&'); break;
        case 0x3D: logic(Mode::Abx, '&'); break;
        case 0x39: logic(Mode::Aby, '&'); break;
        case 0x21: logic(Mode::Izx, '&'); break;
        case 0x31: logic(Mode::Izy, '&'); break;
        case 0x49: logic(Mode::Imm, '^'); break;
        case 0x45: logic(Mode::Zp, '^'); break;
        case 0x55: logic(Mode::Zpx, '^'); break;
        case 0x4D: logic(Mode::Abs, '^'); break;
        case 0x5D: logic(Mode::Abx, '^'); break;
        case 0x59: logic(Mode::Aby, '^'); break;
        case 0x41: logic(Mode::Izx, '^'); break;
        case 0x51: logic(Mode::Izy, '^'); break;
        case 0x69: adc(Mode::Imm); break;
        case 0x65: adc(Mode::Zp); break;
        case 0x75: adc(Mode::Zpx); break;
        case 0x6D: adc(Mode::Abs); break;
        case 0x7D: adc(Mode::Abx); break;
        case 0x79: adc(Mode::Aby); break;
        case 0x61: adc(Mode::Izx); break;
        case 0x71: adc(Mode::Izy); break;
        case 0xE9: sbc(Mode::Imm); break;
        case 0xE5: sbc(Mode::Zp); break;
        case 0xF5: sbc(Mode::Zpx); break;
        case 0xED: sbc(Mode::Abs); break;
        case 0xFD: sbc(Mode::Abx); break;
        case 0xF9: sbc(Mode::Aby); break;
        case 0xE1: sbc(Mode::Izx); break;
        case 0xF1: sbc(Mode::Izy); break;
        case 0xC9: compare(r.a, Mode::Imm); break;
        case 0xC5: compare(r.a, Mode::Zp); break;
        case 0xD5: compare(r.a, Mode::Zpx); break;
        case 0xCD: compare(r.a, Mode::Abs); break;
        case 0xDD: compare(r.a, Mode::Abx); break;
        case 0xD9: compare(r.a, Mode::Aby); break;
        case 0xC1: compare(r.a, Mode::Izx); break;
        case 0xD1: compare(r.a, Mode::Izy); break;
        case 0xE0: compare(r.x, Mode::Imm); break;
        case 0xE4: compare(r.x, Mode::Zp); break;
        case 0xEC: compare(r.x, Mode::Abs); break;
        case 0xC0: compare(r.y, Mode::Imm); break;
        case 0xC4: compare(r.y, Mode::Zp); break;
        case 0xCC: compare(r.y, Mode::Abs); break;
        case 0x24: bit(Mode::Zp); break;
        case 0x2C: bit(Mode::Abs); break;

        // read-modify-write
        case 0x0A: rmw_acc(&Cpu6502::asl); break;
        case 0x06: rmw(Mode::Zp, &Cpu6502::asl); break;
        case 0x16: rmw(Mode::Zpx, &Cpu6502::asl); break;
        case 0x0E: rmw(Mode::Abs, &Cpu6502::asl); break;
        case 0x1E: rmw(Mode::Abx, &Cpu6502::asl); break;
        case 0x4A: rmw_acc(&Cpu6502::lsr); break;
        case 0x46: rmw(Mode::Zp, &Cpu6502::lsr); break;
        case 0x56: rmw(Mode::Zpx, &Cpu6502::lsr); break;
        case 0x4E: rmw(Mode::Abs, &Cpu6502::lsr); break;
        case 0x5E: rmw(Mode::Abx, &Cpu6502::lsr); break;
        case 0x2A: rmw_acc(&Cpu6502::rol); break;
        case 0x26: rmw(Mode::Zp, &Cpu6502::rol); break;
        case 0x36: rmw(Mode::Zpx, &Cpu6502::rol); break;
        case 0x2E: rmw(Mode::Abs, &Cpu6502::rol); break;
        case 0x3E: rmw(Mode::Abx, &Cpu6502::rol); break;
        case 0x6A: rmw_acc(&Cpu6502::ror); break;
        case 0x66: rmw(Mode::Zp, &Cpu6502::ror); break;
        case 0x76: rmw(Mode::Zpx, &Cpu6502::ror); break;
        case 0x6E: rmw(Mode::Abs, &Cpu6502::ror); break;
        case 0x7E: rmw(Mode::Abx, &Cpu6502::ror); break;
        case 0xE6: rmw(Mode::Zp, &Cpu6502::inc); break;
        case 0xF6: rmw(Mode::Zpx, &Cpu6502::inc); break;
        case 0xEE: rmw(Mode::Abs, &Cpu6502::inc); break;
        case 0xFE: rmw(Mode::Abx, &Cpu6502::inc); break;
        case 0xC6: rmw(Mode::Zp, &Cpu6502::dec); break;
        case 0xD6: rmw(Mode::Zpx, &Cpu6502::dec); break;
        case 0xCE: rmw(Mode::Abs, &Cpu6502::dec); break;
        case 0xDE: rmw(Mode::Abx, &Cpu6502::dec); break;

        // register-only
        case 0xE8: implied(); set_nz(++r.x); break;
        case 0xC8: implied(); set_nz(++r.y); break;
        case 0xCA: implied(); set_nz(--r.x); break;
        case 0x88: implied(); set_nz(--r.y); break;
        case 0xAA: implied(); r.x = r.a; set_nz(r.x); break;
        case 0xA8: implied(); r.y = r.a; set_nz(r.y); break;
        case 0xBA: implied(); r.x = r.s; set_nz(r.x); break;
        case 0x8A: implied(); r.a = r.x; set_nz(r.a); break;
        case 0x98: implied(); r.a = r.y; set_nz(r.a); break;
        case 0x9A: implied(); r.s = r.x; break; // no flags
        case 0x18: implied(); set_flag(FLAG_C, false); break;
        case 0x38: implied(); set_flag(FLAG_C, true); break;
        case 0x58: implied(); set_flag(FLAG_I, false); break;
        case 0x78: implied(); set_flag(FLAG_I, true); break;
        case 0xB8: implied(); set_flag(FLAG_V, false); break;
        case 0xD8: implied(); set_flag(FLAG_D, false); break;
        case 0xF8: implied(); set_flag(FLAG_D, true); break;
        case 0xEA: implied(); break;

        // branches
        case 0x10: branch(!flag(FLAG_N)); break;
        case 0x30: branch(flag(FLAG_N)); break;
        case 0x50: branch(!flag(FLAG_V)); break;
        case 0x70: branch(flag(FLAG_V)); break;
        case 0x90: branch(!flag(FLAG_C)); break;
        case 0xB0: branch(flag(FLAG_C)); break;
        case 0xD0: branch(!flag(FLAG_Z)); break;
        case 0xF0: branch(flag(FLAG_Z)); break;

        // stack
        case 0x48: push_reg(r.a); break;
        case 0x08: push_reg(static_cast<uint8_t>(r.p | FLAG_B | FLAG_U)); break;
        case 0x68: r.a = pull_reg(); set_nz(r.a); break;
        case 0x28: r.p = static_cast<uint8_t>((pull_reg() | FLAG_U) & ~FLAG_B); break;

        // jumps, calls, interrupts
        case 0x4C: r.pc = fetch16(); break;
        case 0x6C:
        {
            const uint16_t ptr = fetch16();
            const unsigned plo = read(ptr);
            unsigned phi = 0;
            const auto wrapped = static_cast<uint16_t>((ptr & 0xFF00) | lo8(ptr + 1u));
            if (cmos())
            {
                // WDC still visits the NMOS address, as a dummy read, then
                // fetches the high byte from the right one.
                read(wrapped);
                phi = read(static_cast<uint16_t>(ptr + 1));
            }
            else
            {
                phi = read(wrapped); // NMOS: ($10FF) takes its high byte from $1000
            }
            r.pc = static_cast<uint16_t>(plo | (phi << 8));
            break;
        }
        case 0x20:
        {
            const unsigned plo = fetch();
            read(stack(r.s));
            push(lo8(r.pc >> 8)); // the address of the operand's last byte
            push(lo8(r.pc));
            const unsigned phi = read(r.pc);
            r.pc = static_cast<uint16_t>(plo | (phi << 8));
            break;
        }
        case 0x60:
        {
            implied();
            read(stack(r.s));
            const unsigned plo = pull();
            const unsigned phi = pull();
            r.pc = static_cast<uint16_t>(plo | (phi << 8));
            read(r.pc);
            r.pc++;
            break;
        }
        case 0x40:
        {
            implied();
            read(stack(r.s));
            r.p = static_cast<uint8_t>((pull() | FLAG_U) & ~FLAG_B);
            const unsigned plo = pull();
            const unsigned phi = pull();
            r.pc = static_cast<uint16_t>(plo | (phi << 8));
            break;
        }
        case 0x00:
            fetch(); // BRK's padding byte
            interrupt(IRQ_VECTOR, static_cast<uint8_t>(r.p | FLAG_B | FLAG_U));
            break;

        default:
            execute_undocumented(op);
            break;
        }
    }

    // --- NMOS undocumented opcodes ---------------------------------------------
    //
    // What the NMOS die does with the other 105 opcodes, as Harte's fixtures
    // record it (and as a 6510's tunes rely on): mostly two documented
    // operations sharing one addressing sequence, an RMW's then an ALU's.
    // ANE and LXA mix in the bus's "magic" constant, $EE on the chips Harte
    // measured; SHA, SHX, SHY and TAS store a register ANDed with the base
    // address's high byte + 1, and on a page cross that value becomes the
    // high byte of the address written.

    void Cpu6502::execute_undocumented(uint8_t op)
    {
        Registers &r = regs_;
        // The RMW + ALU combinations share the documented groups' layout:
        // low bits 3 or 7 or F, the mode in bits 2-4.
        static constexpr Mode RMW_MODES[8] = {Mode::Izx, Mode::Zp, Mode::Imm, Mode::Abs, Mode::Izy, Mode::Zpx, Mode::Aby, Mode::Abx};
        const unsigned group = op >> 5, mode = (op >> 2) & 7;
        if ((op & 3) == 3 && mode != 2 && group != 4 && group != 5)
        {
            const Mode m = RMW_MODES[mode];
            switch (group)
            {
            case 0: r.a = static_cast<uint8_t>(r.a | rmw(m, &Cpu6502::asl)); set_nz(r.a); return; // SLO
            case 1: r.a = static_cast<uint8_t>(r.a & rmw(m, &Cpu6502::rol)); set_nz(r.a); return; // RLA
            case 2: r.a = static_cast<uint8_t>(r.a ^ rmw(m, &Cpu6502::lsr)); set_nz(r.a); return; // SRE
            case 3: add(rmw(m, &Cpu6502::ror)); return;                                            // RRA
            case 6: compare_value(r.a, rmw(m, &Cpu6502::dec)); return;                             // DCP
            case 7: subtract(rmw(m, &Cpu6502::inc)); return;                                       // ISC
            }
        }
        switch (op)
        {
        // SAX, LAX, LAS
        case 0x87: store(Mode::Zp, static_cast<uint8_t>(r.a & r.x)); return;
        case 0x97: store(Mode::Zpy, static_cast<uint8_t>(r.a & r.x)); return;
        case 0x8F: store(Mode::Abs, static_cast<uint8_t>(r.a & r.x)); return;
        case 0x83: store(Mode::Izx, static_cast<uint8_t>(r.a & r.x)); return;
        case 0xA7: load(r.a, Mode::Zp); r.x = r.a; return;
        case 0xB7: load(r.a, Mode::Zpy); r.x = r.a; return;
        case 0xAF: load(r.a, Mode::Abs); r.x = r.a; return;
        case 0xBF: load(r.a, Mode::Aby); r.x = r.a; return;
        case 0xA3: load(r.a, Mode::Izx); r.x = r.a; return;
        case 0xB3: load(r.a, Mode::Izy); r.x = r.a; return;
        case 0xBB: r.a = r.x = r.s = static_cast<uint8_t>(read(ea_read(Mode::Aby)) & r.s); set_nz(r.a); return;

        // immediate ALU combinations
        case 0x0B: case 0x2B: // ANC
            r.a = static_cast<uint8_t>(r.a & fetch());
            set_nz(r.a);
            set_flag(FLAG_C, r.a & 0x80);
            return;
        case 0x4B: r.a = static_cast<uint8_t>(r.a & fetch()); r.a = lsr(r.a); return; // ALR
        case 0x6B: arr(fetch()); return;
        case 0x8B: r.a = static_cast<uint8_t>((r.a | MAGIC) & r.x & fetch()); set_nz(r.a); return; // ANE
        case 0xAB: r.a = r.x = static_cast<uint8_t>((r.a | MAGIC) & fetch()); set_nz(r.a); return; // LXA
        case 0xCB: // SBX
        {
            const unsigned ax = r.a & r.x, v = fetch();
            set_flag(FLAG_C, ax >= v);
            r.x = lo8(ax - v + 0x100u);
            set_nz(r.x);
            return;
        }
        case 0xEB: subtract(fetch()); return; // SBC #, again

        // stores of a register ANDed with the address
        case 0x93: store_high_and(Mode::Izy, static_cast<uint8_t>(r.a & r.x)); return; // SHA
        case 0x9F: store_high_and(Mode::Aby, static_cast<uint8_t>(r.a & r.x)); return; // SHA
        case 0x9E: store_high_and(Mode::Aby, r.x); return;                             // SHX
        case 0x9C: store_high_and(Mode::Abx, r.y); return;                             // SHY
        case 0x9B: r.s = static_cast<uint8_t>(r.a & r.x); store_high_and(Mode::Aby, r.s); return; // TAS

        // NOPs that still address memory
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: read(ea_read(Mode::Imm)); return;
        case 0x04: case 0x44: case 0x64: read(ea_read(Mode::Zp)); return;
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: read(ea_read(Mode::Zpx)); return;
        case 0x0C: read(ea_read(Mode::Abs)); return;
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: read(ea_read(Mode::Abx)); return;
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: implied(); return;

        default: // the jams ($x2): the CPU stops until reset, as STP
            read(r.pc);
            state_ = State::Stopped;
            return;
        }
    }

    void Cpu6502::arr(uint8_t v)
    {
        // AND, then ROR A, with flags from the adder's half of the job: in
        // decimal mode it applies the BCD fix-ups to the rotated value.
        Registers &r = regs_;
        const unsigned t = r.a & v;
        const bool c = flag(FLAG_C);
        unsigned res = (t >> 1) | (c ? 0x80u : 0u);
        if (!flag(FLAG_D))
        {
            set_nz(lo8(res));
            set_flag(FLAG_C, res & 0x40);
            set_flag(FLAG_V, ((res >> 6) ^ (res >> 5)) & 1);
        }
        else
        {
            set_flag(FLAG_N, c);
            set_flag(FLAG_Z, res == 0);
            set_flag(FLAG_V, (t ^ res) & 0x40);
            if ((t & 0x0F) + (t & 0x01) > 5)
                res = (res & 0xF0) | ((res + 6) & 0x0F);
            const bool carry = (t >> 4) + ((t >> 4) & 1) > 5;
            set_flag(FLAG_C, carry);
            if (carry)
                res += 0x60;
        }
        r.a = lo8(res);
    }

    void Cpu6502::store_high_and(Mode m, uint8_t v)
    {
        // The address phase of an indexed store, then v & (base high + 1).
        uint16_t base = 0;
        uint8_t index = m == Mode::Abx ? regs_.x : regs_.y;
        if (m == Mode::Izy)
        {
            const uint8_t zp = fetch();
            const unsigned lo = read(zp);
            const unsigned hi = read(lo8(zp + 1u));
            base = static_cast<uint16_t>(lo | (hi << 8));
        }
        else
        {
            base = fetch16();
        }
        auto ea = static_cast<uint16_t>(base + index);
        read(static_cast<uint16_t>((base & 0xFF00) | (ea & 0xFF)));
        const auto out = static_cast<uint8_t>(v & ((base >> 8) + 1u));
        if (crosses(base, ea))
            ea = static_cast<uint16_t>((out << 8) | (ea & 0xFF));
        write(ea, out);
    }
} // namespace ooe::emu
