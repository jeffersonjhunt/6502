/**
 * @file test_disk2_native.cpp
 * @brief The Disk II card in native mode, phase 6a (hw/oodos.md, "The plan, phase 6"): 6502 code on the
 * backplane driving its switches, and the bridge's mode setting the card's clock. No fixtures; the disk
 * cases, which need the owner's sequencer PROM, are test_disk2_prom's.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The W65C02S on backplane.machine, with no sequencer PROM: the switches' latch, the ID and the card's
 * step count are what it reads.
 */

#include "harness.h"

#include "cpu6502.h"
#include "decoder.h"
#include "disk2_card.h"
#include "machine.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

TEST(native_6502_code_drives_every_switch_and_the_bridge_sets_the_clock)
{
    // The backplane machine in native mode, the W65C02S running code at $0400 that touches each of the
    // sixteen switches at $C600-$C60F by LDA or STA, and reads the ID at $C67F. Then MODE's Apple II bit,
    // which the bridge passes on to the card's clock. No PROM needed.
    MachineOptions opt;
    opt.rom_override = EMU_BIOS_ROM;
    std::string error;
    std::ifstream f(EMU_MACHINE_DIR "/backplane.machine");
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto m = Machine::build(text, "backplane.machine", opt, error);
    REQUIRE(m != nullptr);
    Disk2Card *card = m->disk2_card();
    REQUIRE(card != nullptr);
    Cpu6502 cpu(m->bus(), Cpu6502::Model::W65C02S);
    m->bus().reset();
    cpu.reset();
    CHECK(card->clock() == Disk2Card::Clock::Native);
    auto run = [&](uint16_t pc, const std::vector<uint8_t> &code, unsigned instructions) {
        for (size_t i = 0; i < code.size(); ++i)
            m->bus().write_physical(static_cast<uint32_t>(pc + i), code[i]);
        Registers r = cpu.regs();
        r.pc = pc;
        r.p = Cpu6502::FLAG_I;
        cpu.set_regs(r);
        for (unsigned i = 0; i < instructions; ++i)
            cpu.step();
    };
    auto lda = [](uint8_t lo) { return std::vector<uint8_t>{0xAD, lo, 0xC6}; };
    auto sta = [](uint8_t lo) { return std::vector<uint8_t>{0x8D, lo, 0xC6}; };
    std::vector<uint8_t> on;
    for (const auto &i : {sta(0x09), lda(0x0B), lda(0x0D), sta(0x0F), lda(0x01), sta(0x03), lda(0x05), lda(0x07)})
        on.insert(on.end(), i.begin(), i.end());
    run(0x0400, on, 8);
    Disk2Card::Status s = card->status();
    CHECK(s.motor && s.drive == 1 && s.q6 && s.q7 && s.phases == 0x0F);
    std::vector<uint8_t> off;
    for (const auto &i : {lda(0x00), sta(0x02), lda(0x04), lda(0x06), lda(0x0E), sta(0x0C), lda(0x0A), lda(0x08), lda(0x7F)})
        off.insert(off.end(), i.begin(), i.end());
    off.insert(off.end(), {0x8D, 0x80, 0x04}); // STA $0480: the ID
    run(0x0500, off, 10);
    s = card->status();
    CHECK(!s.motor && s.spinning && s.drive == 0 && !s.q6 && !s.q7 && s.phases == 0);
    CHECK_EQ(m->bus().peek_physical(0x0480), 0xC1);
    // 7,000 cycles of native code are 2,000 steps
    const uint64_t s0 = card->status().steps;
    run(0x0600, {0x4C, 0x00, 0x06}, 2000);  // JMP *: 3 cycles each, 6,000; then 1,000 idle bus cycles
    for (int i = 0; i < 1000; ++i)
        m->bus().idle();
    CHECK_EQ(card->status().steps - s0, uint64_t{2000});
    run(0x0700, {0xA9, 0x01, 0x8D, 0x85, 0xC6}, 2); // LDA #1, STA $C685: MODE's Apple II bit
    CHECK(card->clock() == Disk2Card::Clock::Apple);
    m->bus().reset();                        // a hard reset: native mode again
    CHECK(card->clock() == Disk2Card::Clock::Native);
}


int main() { return run_all(); }
