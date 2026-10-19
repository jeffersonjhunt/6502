/**
 * @file disasm.cpp
 * @brief Opcode tables and formatting.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Most of the instruction set is regular: the ALU group and the shifts sit
 * at fixed offsets from a base opcode per mnemonic, so those are generated
 * from the base and the rest are listed. The W65C02S table starts as a copy
 * of the NMOS one and adds WDC's instructions on top.
 */

#include "disasm.h"

#include <array>
#include <cstdio>

namespace ooe::emu
{
    namespace
    {
        enum class Mode : uint8_t
        {
            None, ///< not an instruction
            Imp,
            Acc,
            Imm,
            Zp,
            Zpx,
            Zpy,
            Abs,
            Abx,
            Aby,
            Ind,  ///< (abs)
            Aix,  ///< (abs,x)
            Izx,  ///< (zp,x)
            Izy,  ///< (zp),y
            Izp,  ///< (zp)
            Rel,
            Zpr,  ///< zp,rel (BBR/BBS)
        };

        struct Op
        {
            const char *name{nullptr};
            Mode mode{Mode::None};
        };

        using Table = std::array<Op, 256>;

        unsigned mode_length(Mode m)
        {
            switch (m)
            {
            case Mode::None:
            case Mode::Imp:
            case Mode::Acc:
                return 1;
            case Mode::Abs:
            case Mode::Abx:
            case Mode::Aby:
            case Mode::Ind:
            case Mode::Aix:
            case Mode::Zpr:
                return 3;
            default:
                return 2;
            }
        }

        Table nmos_table()
        {
            Table t{};
            auto set = [&t](unsigned op, const char *name, Mode m) { t[op] = Op{name, m}; };

            // ORA AND EOR ADC STA LDA CMP SBC: base + 09 imm, 05 zp, 15 zp,x,
            // 0D abs, 1D abs,x, 19 abs,y, 01 (zp,x), 11 (zp),y.
            const struct
            {
                const char *name;
                unsigned base;
            } alu[] = {{"ora", 0x00}, {"and", 0x20}, {"eor", 0x40}, {"adc", 0x60},
                       {"sta", 0x80}, {"lda", 0xA0}, {"cmp", 0xC0}, {"sbc", 0xE0}};
            for (const auto &a : alu)
            {
                if (a.base != 0x80) // no STA #
                    set(a.base + 0x09, a.name, Mode::Imm);
                set(a.base + 0x05, a.name, Mode::Zp);
                set(a.base + 0x15, a.name, Mode::Zpx);
                set(a.base + 0x0D, a.name, Mode::Abs);
                set(a.base + 0x1D, a.name, Mode::Abx);
                set(a.base + 0x19, a.name, Mode::Aby);
                set(a.base + 0x01, a.name, Mode::Izx);
                set(a.base + 0x11, a.name, Mode::Izy);
            }
            // ASL ROL LSR ROR: base + 0A a, 06 zp, 16 zp,x, 0E abs, 1E abs,x.
            const struct
            {
                const char *name;
                unsigned base;
            } shift[] = {{"asl", 0x00}, {"rol", 0x20}, {"lsr", 0x40}, {"ror", 0x60}};
            for (const auto &s : shift)
            {
                set(s.base + 0x0A, s.name, Mode::Acc);
                set(s.base + 0x06, s.name, Mode::Zp);
                set(s.base + 0x16, s.name, Mode::Zpx);
                set(s.base + 0x0E, s.name, Mode::Abs);
                set(s.base + 0x1E, s.name, Mode::Abx);
            }
            for (const auto &[name, base] : {std::pair{"dec", 0xC0u}, std::pair{"inc", 0xE0u}})
            {
                set(base + 0x06, name, Mode::Zp);
                set(base + 0x16, name, Mode::Zpx);
                set(base + 0x0E, name, Mode::Abs);
                set(base + 0x1E, name, Mode::Abx);
            }

            const char *branches[] = {"bpl", "bmi", "bvc", "bvs", "bcc", "bcs", "bne", "beq"};
            for (unsigned i = 0; i < 8; ++i)
                set(0x10 + i * 0x20, branches[i], Mode::Rel);

            set(0x24, "bit", Mode::Zp);
            set(0x2C, "bit", Mode::Abs);
            set(0xE0, "cpx", Mode::Imm);
            set(0xE4, "cpx", Mode::Zp);
            set(0xEC, "cpx", Mode::Abs);
            set(0xC0, "cpy", Mode::Imm);
            set(0xC4, "cpy", Mode::Zp);
            set(0xCC, "cpy", Mode::Abs);
            set(0x4C, "jmp", Mode::Abs);
            set(0x6C, "jmp", Mode::Ind);
            set(0x20, "jsr", Mode::Abs);
            set(0xA2, "ldx", Mode::Imm);
            set(0xA6, "ldx", Mode::Zp);
            set(0xB6, "ldx", Mode::Zpy);
            set(0xAE, "ldx", Mode::Abs);
            set(0xBE, "ldx", Mode::Aby);
            set(0xA0, "ldy", Mode::Imm);
            set(0xA4, "ldy", Mode::Zp);
            set(0xB4, "ldy", Mode::Zpx);
            set(0xAC, "ldy", Mode::Abs);
            set(0xBC, "ldy", Mode::Abx);
            set(0x86, "stx", Mode::Zp);
            set(0x96, "stx", Mode::Zpy);
            set(0x8E, "stx", Mode::Abs);
            set(0x84, "sty", Mode::Zp);
            set(0x94, "sty", Mode::Zpx);
            set(0x8C, "sty", Mode::Abs);

            const std::pair<unsigned, const char *> implied[] = {
                {0x00, "brk"}, {0x18, "clc"}, {0xD8, "cld"}, {0x58, "cli"}, {0xB8, "clv"}, {0xCA, "dex"},
                {0x88, "dey"}, {0xE8, "inx"}, {0xC8, "iny"}, {0xEA, "nop"}, {0x48, "pha"}, {0x08, "php"},
                {0x68, "pla"}, {0x28, "plp"}, {0x40, "rti"}, {0x60, "rts"}, {0x38, "sec"}, {0xF8, "sed"},
                {0x78, "sei"}, {0xAA, "tax"}, {0xA8, "tay"}, {0xBA, "tsx"}, {0x8A, "txa"}, {0x9A, "txs"},
                {0x98, "tya"}};
            for (const auto &[op, name] : implied)
                set(op, name, Mode::Imp);
            return t;
        }

        Table wdc_table()
        {
            Table t = nmos_table();
            auto set = [&t](unsigned op, const char *name, Mode m) { t[op] = Op{name, m}; };

            const char *izp[] = {"ora", "and", "eor", "adc", "sta", "lda", "cmp", "sbc"};
            for (unsigned i = 0; i < 8; ++i)
                set(0x12 + i * 0x20, izp[i], Mode::Izp);
            set(0x89, "bit", Mode::Imm);
            set(0x34, "bit", Mode::Zpx);
            set(0x3C, "bit", Mode::Abx);
            set(0x1A, "inc", Mode::Acc);
            set(0x3A, "dec", Mode::Acc);
            set(0x80, "bra", Mode::Rel);
            set(0x5A, "phy", Mode::Imp);
            set(0x7A, "ply", Mode::Imp);
            set(0xDA, "phx", Mode::Imp);
            set(0xFA, "plx", Mode::Imp);
            set(0x64, "stz", Mode::Zp);
            set(0x74, "stz", Mode::Zpx);
            set(0x9C, "stz", Mode::Abs);
            set(0x9E, "stz", Mode::Abx);
            set(0x04, "tsb", Mode::Zp);
            set(0x0C, "tsb", Mode::Abs);
            set(0x14, "trb", Mode::Zp);
            set(0x1C, "trb", Mode::Abs);
            set(0x7C, "jmp", Mode::Aix);
            set(0xCB, "wai", Mode::Imp);
            set(0xDB, "stp", Mode::Imp);

            static const char *const BIT_OPS[4][8] = {
                {"rmb0", "rmb1", "rmb2", "rmb3", "rmb4", "rmb5", "rmb6", "rmb7"},
                {"smb0", "smb1", "smb2", "smb3", "smb4", "smb5", "smb6", "smb7"},
                {"bbr0", "bbr1", "bbr2", "bbr3", "bbr4", "bbr5", "bbr6", "bbr7"},
                {"bbs0", "bbs1", "bbs2", "bbs3", "bbs4", "bbs5", "bbs6", "bbs7"},
            };
            for (unsigned b = 0; b < 8; ++b)
            {
                set(0x07 + b * 0x10, BIT_OPS[0][b], Mode::Zp);
                set(0x87 + b * 0x10, BIT_OPS[1][b], Mode::Zp);
                set(0x0F + b * 0x10, BIT_OPS[2][b], Mode::Zpr);
                set(0x8F + b * 0x10, BIT_OPS[3][b], Mode::Zpr);
            }
            return t;
        }

        const Table &table(Cpu6502::Model model)
        {
            static const Table NMOS = nmos_table();
            static const Table WDC = wdc_table();
            return model == Cpu6502::Model::W65C02S ? WDC : NMOS;
        }

        /// Size of an opcode that is not an instruction, as the core runs it.
        unsigned undefined_length(Cpu6502::Model model, uint8_t op)
        {
            if (model != Cpu6502::Model::W65C02S)
                return 1; // the core's NMOS undocumented opcodes are one-byte NOPs
            if ((op & 0x0F) == 0x02 || op == 0x44 || op == 0x54 || op == 0xD4 || op == 0xF4)
                return 2;
            if (op == 0x5C || op == 0xDC || op == 0xFC)
                return 3;
            return 1; // x3, xB
        }

        std::string hex(unsigned v, int digits)
        {
            char b[8];
            std::snprintf(b, sizeof b, "$%0*X", digits, v);
            return b;
        }

        /// A label if one is exactly at `addr`, else `$xx` or `$xxxx`. With
        /// `force_abs`, an address below $0100 gets ca65's `a:` so that it
        /// does not reassemble as zero page; JMP's indirect forms have no
        /// zero-page variant, and ca65 rejects the prefix there.
        std::string address(unsigned addr, int digits, const Symbols *symbols, bool force_abs = false)
        {
            const std::string prefix = force_abs && addr < 0x100 ? "a:" : "";
            if (symbols)
            {
                if (const std::string *name = symbols->name_at(static_cast<uint16_t>(addr)))
                    return prefix + *name;
            }
            return prefix + hex(addr, digits);
        }
    } // namespace

    bool is_instruction(Cpu6502::Model model, uint8_t opcode)
    {
        return table(model)[opcode].mode != Mode::None;
    }

    unsigned instruction_length(Cpu6502::Model model, uint8_t opcode)
    {
        const Op &op = table(model)[opcode];
        if (op.mode == Mode::None)
            return undefined_length(model, opcode);
        if (opcode == 0x00)
            return 2; // BRK skips its signature byte
        return mode_length(op.mode);
    }

    Disassembly disassemble(Cpu6502::Model model, const Bus &bus, uint16_t pc, const Symbols *symbols)
    {
        const uint8_t opcode = bus.peek(pc);
        const unsigned b1 = bus.peek(static_cast<uint16_t>(pc + 1));
        const unsigned b2 = bus.peek(static_cast<uint16_t>(pc + 2));
        const unsigned word = b1 | (b2 << 8);
        const Op &op = table(model)[opcode];

        Disassembly d;
        d.length = instruction_length(model, opcode);
        if (op.mode == Mode::None)
        {
            d.text = ".byte " + hex(opcode, 2);
            if (d.length > 1)
                d.text += "," + hex(b1, 2);
            if (d.length > 2)
                d.text += "," + hex(b2, 2);
            return d;
        }

        const std::string name = op.name;
        auto rel = [&](unsigned offset, unsigned next) {
            const auto target = static_cast<uint16_t>(pc + next + static_cast<int8_t>(offset));
            return address(target, 4, symbols);
        };
        switch (op.mode)
        {
        case Mode::Imp: d.text = name; break;
        case Mode::Acc: d.text = name + " a"; break;
        case Mode::Imm: d.text = name + " #" + hex(b1, 2); break;
        case Mode::Zp: d.text = name + " " + address(b1, 2, symbols); break;
        case Mode::Zpx: d.text = name + " " + address(b1, 2, symbols) + ",x"; break;
        case Mode::Zpy: d.text = name + " " + address(b1, 2, symbols) + ",y"; break;
        case Mode::Abs: d.text = name + " " + address(word, 4, symbols, true); break;
        case Mode::Abx: d.text = name + " " + address(word, 4, symbols, true) + ",x"; break;
        case Mode::Aby: d.text = name + " " + address(word, 4, symbols, true) + ",y"; break;
        case Mode::Ind: d.text = name + " (" + address(word, 4, symbols) + ")"; break;
        case Mode::Aix: d.text = name + " (" + address(word, 4, symbols) + ",x)"; break;
        case Mode::Izx: d.text = name + " (" + address(b1, 2, symbols) + ",x)"; break;
        case Mode::Izy: d.text = name + " (" + address(b1, 2, symbols) + "),y"; break;
        case Mode::Izp: d.text = name + " (" + address(b1, 2, symbols) + ")"; break;
        case Mode::Rel: d.text = name + " " + rel(b1, 2); break;
        case Mode::Zpr: d.text = name + " " + address(b1, 2, symbols) + "," + rel(b2, 3); break;
        case Mode::None: break;
        }
        return d;
    }
} // namespace ooe::emu
