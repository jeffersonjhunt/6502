#pragma once

/**
 * @file disasm.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief One instruction to ca65 source text, for either CPU model.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The output is meant to reassemble: ca65 (`.setcpu "6502"` or "65C02")
 * turns each line back into the same bytes, and test_disasm checks exactly
 * that for every opcode of both models. So branches show their target
 * address, an absolute operand below $0100 is written `a:$00xx` (or ca65
 * would pick zero page), and opcodes that are not instructions are `.byte`
 * lists sized as the core executes them: one byte for NMOS undocumented
 * opcodes, WDC's own sizes for its undefined NOPs.
 *
 * Two deliberate departures. BRK is two bytes, as the CPU treats it (it
 * skips the signature byte), but ca65 has no syntax for that byte, so the
 * text is plain `brk`. And with symbols, an operand that is exactly a
 * label's address prints as the label.
 *
 * Bytes are read with Bus::peek(), so disassembling I/O space is harmless.
 */

#include <cstdint>
#include <string>

#include "bus.h"
#include "cpu6502.h"
#include "symbols.h"

namespace ooe::emu
{
    struct Disassembly
    {
        unsigned length{1}; ///< bytes, 1-3
        std::string text;   ///< e.g. "lda ($12),y"
    };

    /// Disassemble the instruction at `pc`. `symbols` may be nullptr.
    Disassembly disassemble(Cpu6502::Model model, const Bus &bus, uint16_t pc, const Symbols *symbols = nullptr);

    /// Instruction length in bytes for `opcode`, as the core executes it.
    unsigned instruction_length(Cpu6502::Model model, uint8_t opcode);

    /// True if `opcode` is a documented instruction on `model` (151 on the
    /// NMOS 6502, 212 on the W65C02S).
    bool is_instruction(Cpu6502::Model model, uint8_t opcode);
} // namespace ooe::emu
