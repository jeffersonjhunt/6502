/**
 * @file test_disasm.cpp
 * @brief The disassembler against two independent oracles: the core and ca65.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Lengths are checked against what the core actually does: step each
 * opcode once and see how far PC moved. Text is checked by round trip:
 * disassemble every opcode of a model, hand the lines to ca65, and require
 * the very same bytes back. A wrong mnemonic or addressing mode assembles
 * to a different opcode, and a wrong length shifts everything after it, so
 * either fails here. The round trip runs twice, once with operands whose
 * absolute form is above $FF and once with $00xx operands and a backward
 * branch, which is what the `a:` prefix is for, and a third time for a
 * backward BBR/BBS.
 *
 * Without cc65 the round trip is reported as skipped rather than passed.
 */

#include "harness.h"
#include "test_support.h"

#include "disasm.h"
#include "symbols.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr Cpu6502::Model MODELS[] = {Cpu6502::Model::Nmos6502, Cpu6502::Model::W65C02S};

    const char *model_name(Cpu6502::Model m) { return m == Cpu6502::Model::W65C02S ? "w65c02s" : "6502"; }

    /// Opcodes that put something other than "next instruction" in PC.
    bool changes_flow(Cpu6502::Model m, uint8_t op)
    {
        switch (op)
        {
        case 0x00: case 0x20: case 0x40: case 0x4C: case 0x60: case 0x6C:
            return true;
        case 0x80: case 0x7C:
            return m == Cpu6502::Model::W65C02S;
        default:
            break;
        }
        if ((op & 0x1F) == 0x10) // the eight conditional branches
            return true;
        return m == Cpu6502::Model::W65C02S && (op & 0x0F) == 0x0F; // BBR/BBS
    }

    int round_trip_skips = 0;

    /// Disassemble every opcode with operand bytes b1 b2, reassemble, compare.
    void round_trip(Cpu6502::Model model, uint8_t b1, uint8_t b2)
    {
        constexpr uint16_t ORG = 0x1000;
        FlatBus bus;
        std::vector<uint8_t> want;
        std::string source = std::string(".setcpu \"") +
                             (model == Cpu6502::Model::W65C02S ? "65C02" : "6502") + "\"\n.org $1000\n";
        uint16_t pc = ORG;
        for (unsigned op = 0; op < 256; ++op)
        {
            const uint8_t bytes[3] = {static_cast<uint8_t>(op), b1, b2};
            for (int i = 0; i < 3; ++i)
                bus.at(static_cast<uint16_t>(pc + i)) = bytes[i];
            const Disassembly d = disassemble(model, bus, pc);
            source += d.text + "\n";
            if (op == 0x00)
                source += ".byte $" + std::string(1, "0123456789ABCDEF"[b1 >> 4]) +
                          "0123456789ABCDEF"[b1 & 15] + "\n"; // BRK's signature byte
            want.insert(want.end(), bytes, bytes + d.length);
            pc = static_cast<uint16_t>(pc + d.length);
        }

        const std::string base = std::string(EMU_TEST_TMP "/disasm_") + model_name(model);
        std::ofstream(base + ".s") << source;
        const std::string cmd = std::string(EMU_CA65 " -o ") + base + ".o " + base + ".s && " EMU_LD65 " -t none -S " +
                                std::to_string(ORG) + " -o " + base + ".bin " + base + ".o";
        CHECK_EQ(std::system(cmd.c_str()), 0);
        std::ifstream in(base + ".bin", std::ios::binary);
        const std::vector<uint8_t> got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK_EQ(got.size(), want.size());

        // Report the first difference by the line that produced it.
        size_t at = 0;
        while (at < got.size() && at < want.size() && got[at] == want[at])
            ++at;
        if (at < want.size() || at < got.size())
        {
            std::printf("  %s, operands %02X %02X: first difference at byte %zu (see %s.s)\n", model_name(model), b1,
                        b2, at, base.c_str());
            CHECK(false);
        }
    }
} // namespace

TEST(instruction_counts)
{
    int nmos = 0, wdc = 0;
    for (unsigned op = 0; op < 256; ++op)
    {
        nmos += is_instruction(Cpu6502::Model::Nmos6502, static_cast<uint8_t>(op));
        wdc += is_instruction(Cpu6502::Model::W65C02S, static_cast<uint8_t>(op));
    }
    CHECK_EQ(nmos, 151);
    CHECK_EQ(wdc, 212);
    CHECK(!is_instruction(Cpu6502::Model::Nmos6502, 0x80)); // BRA is WDC only
    CHECK(is_instruction(Cpu6502::Model::W65C02S, 0x80));
}

TEST(lengths_match_how_far_the_core_moves_pc)
{
    int checked = 0;
    for (const Cpu6502::Model model : MODELS)
    {
        for (unsigned op = 0; op < 256; ++op)
        {
            if (changes_flow(model, static_cast<uint8_t>(op)))
                continue;
            FlatBus bus;
            bus.at(0x0400) = static_cast<uint8_t>(op);
            Cpu6502 c(bus, model);
            c.set_regs(Registers{0x0400, 0xFF, 0, 0, 0, Cpu6502::FLAG_U});
            c.step();
            const unsigned moved = static_cast<uint16_t>(c.regs().pc - 0x0400);
            const unsigned length = instruction_length(model, static_cast<uint8_t>(op));
            if (moved != length)
                std::printf("  %s %02X: core moves PC %u, disassembler says %u\n", model_name(model), op, moved,
                            length);
            CHECK_EQ(moved, length);
            ++checked;
        }
    }
    CHECK(checked > 400); // both models, all but the jumps and branches
}

TEST(formats)
{
    FlatBus bus;
    auto text = [&](std::initializer_list<uint8_t> bytes, Cpu6502::Model m = Cpu6502::Model::W65C02S,
                    const Symbols *sym = nullptr) {
        uint16_t a = 0x0300;
        for (const uint8_t b : bytes)
            bus.at(a++) = b;
        return disassemble(m, bus, 0x0300, sym).text;
    };
    CHECK_EQ(text({0xB1, 0x12}), std::string("lda ($12),y"));
    CHECK_EQ(text({0xAD, 0x12, 0x00}), std::string("lda a:$0012"));
    CHECK_EQ(text({0xD0, 0xFE}), std::string("bne $0300"));
    CHECK_EQ(text({0x8F, 0x34, 0x02}), std::string("bbs0 $34,$0305"));
    CHECK_EQ(text({0x5C, 0x34, 0x12}), std::string(".byte $5C,$34,$12"));
    CHECK_EQ(disassemble(Cpu6502::Model::W65C02S, bus, 0x0300).length, 3u);
    CHECK_EQ(text({0x6C, 0x80, 0x00}), std::string("jmp ($0080)")); // no a: on indirect JMP
    // NMOS undocumented: bytes as the core runs them, and what it does with them.
    CHECK_EQ(text({0x80, 0x00}, Cpu6502::Model::Nmos6502), std::string(".byte $80,$00 ; nop #$00"));
    CHECK_EQ(text({0xA7, 0x12}, Cpu6502::Model::Nmos6502), std::string(".byte $A7,$12 ; lax $12"));
    CHECK_EQ(text({0x9F, 0x00, 0xD4}, Cpu6502::Model::Nmos6502), std::string(".byte $9F,$00,$D4 ; sha $D400,y"));
    CHECK_EQ(text({0x02, 0xEA}, Cpu6502::Model::Nmos6502), std::string(".byte $02")); // a jam
    CHECK_EQ(text({0xA7, 0x12}, Cpu6502::Model::W65C02S), std::string("smb2 $12"));

    Symbols sym;
    sym.add("ECHO", 0xFFEF);
    sym.add("PTR", 0x0012);
    CHECK_EQ(text({0x20, 0xEF, 0xFF}, Cpu6502::Model::W65C02S, &sym), std::string("jsr ECHO"));
    CHECK_EQ(text({0xB1, 0x12}, Cpu6502::Model::W65C02S, &sym), std::string("lda (PTR),y"));
    CHECK_EQ(text({0xAD, 0x12, 0x00}, Cpu6502::Model::W65C02S, &sym), std::string("lda a:PTR"));
}

TEST(reassembles_to_the_same_bytes)
{
#if defined(EMU_CA65) && defined(EMU_LD65)
    for (const Cpu6502::Model model : MODELS)
    {
        round_trip(model, 0x12, 0x34); // absolute operands above $FF, forward branches
        round_trip(model, 0x80, 0x00); // $00xx absolute operands, backward branches
        round_trip(model, 0x12, 0xF0); // backward BBR/BBS
    }
#else
    std::printf("  SKIPPED: no ca65/ld65, round trip not run\n");
    ++round_trip_skips;
#endif
}

int main()
{
    const int rc = run_all();
    return rc == 0 && round_trip_skips ? SKIP : rc;
}
