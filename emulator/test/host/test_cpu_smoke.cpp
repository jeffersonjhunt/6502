/**
 * @file test_cpu_smoke.cpp
 * @brief Hand-checked 6502 behaviour that needs no downloaded fixtures.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The Dormann and Harte suites are far more thorough, but they are fetched on
 * demand and skip when absent. This file is what every checkout runs, so a
 * machine without fixtures still examines the core rather than reporting a
 * green run over nothing. Each case pins one datasheet fact, chosen for the
 * places emulators most often get wrong: decimal mode, overflow, the NMOS
 * JMP ($xxFF) page wrap, and what BRK and IRQ push.
 *
 * The later cases cover what Harte cannot, because each of his cases is one
 * instruction: the reset and interrupt sequences, and the W65C02S's WAI and
 * STP, which wait on something outside the instruction.
 */

#include "harness.h"
#include "test_support.h"

#include <initializer_list>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t ORG = 0x0200;

    /// Load `code` at ORG, boot there with `r`, and execute `steps` instructions.
    void run(Cpu6502 &c, FlatBus &bus, std::initializer_list<uint8_t> code, int steps, Registers r = {})
    {
        uint16_t addr = ORG;
        for (const uint8_t b : code)
            bus.at(addr++) = b;
        r.pc = ORG;
        c.set_regs(r);
        for (int i = 0; i < steps; ++i)
            c.step();
    }

    struct Recorder : CycleObserver
    {
        void on_cycle(const BusCycle &c) override { seen.push_back(c); }
        std::vector<BusCycle> seen;
    };
} // namespace

TEST(reset_loads_pc_from_vector)
{
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0xFFFC) = 0x34;
    bus.at(0xFFFD) = 0x12;
    c.reset();
    CHECK_EQ(c.regs().pc, 0x1234);
}

TEST(lda_immediate_sets_n_and_z)
{
    FlatBus bus;
    Cpu6502 c(bus);
    run(c, bus, {0xA9, 0x00}, 1); // LDA #$00
    CHECK_EQ(c.regs().a, 0x00);
    CHECK(c.regs().p & Cpu6502::FLAG_Z);
    CHECK(!(c.regs().p & Cpu6502::FLAG_N));

    run(c, bus, {0xA9, 0x80}, 1); // LDA #$80
    CHECK(!(c.regs().p & Cpu6502::FLAG_Z));
    CHECK(c.regs().p & Cpu6502::FLAG_N);
}

TEST(adc_binary_signed_overflow)
{
    FlatBus bus;
    Cpu6502 c(bus);
    run(c, bus, {0x18, 0xA9, 0x50, 0x69, 0x50}, 3); // CLC; LDA #$50; ADC #$50
    CHECK_EQ(c.regs().a, 0xA0);
    CHECK(c.regs().p & Cpu6502::FLAG_V);
    CHECK(c.regs().p & Cpu6502::FLAG_N);
    CHECK(!(c.regs().p & Cpu6502::FLAG_C));
}

TEST(adc_decimal_carries_between_digits)
{
    FlatBus bus;
    Cpu6502 c(bus);
    run(c, bus, {0xF8, 0x18, 0xA9, 0x09, 0x69, 0x01}, 4); // SED; CLC; LDA #$09; ADC #$01
    CHECK_EQ(c.regs().a, 0x10);
    CHECK(!(c.regs().p & Cpu6502::FLAG_C));

    run(c, bus, {0xF8, 0x18, 0xA9, 0x99, 0x69, 0x01}, 4); // 99 + 01 = 00, carry out
    CHECK_EQ(c.regs().a, 0x00);
    CHECK(c.regs().p & Cpu6502::FLAG_C);
}

TEST(sbc_decimal_borrows)
{
    FlatBus bus;
    Cpu6502 c(bus);
    run(c, bus, {0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x01}, 4); // SED; SEC; LDA #$10; SBC #$01
    CHECK_EQ(c.regs().a, 0x09);
    CHECK(c.regs().p & Cpu6502::FLAG_C); // no borrow
}

TEST(jmp_indirect_wraps_within_page)
{
    // NMOS bug: the high byte of the target is fetched from $1000, not $1100.
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0x10FF) = 0x34;
    bus.at(0x1000) = 0x12;
    bus.at(0x1100) = 0x56;
    run(c, bus, {0x6C, 0xFF, 0x10}, 1); // JMP ($10FF)
    CHECK_EQ(c.regs().pc, 0x1234);
}

TEST(jsr_rts_round_trip)
{
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0x2000) = 0x60;                // RTS
    run(c, bus, {0x20, 0x00, 0x20}, 1);   // JSR $2000
    CHECK_EQ(c.regs().pc, 0x2000);
    CHECK_EQ(c.regs().s, 0xFB);
    CHECK_EQ(bus.at(0x01FD), 0x02);       // return address - 1 = $0202
    CHECK_EQ(bus.at(0x01FC), 0x02);
    c.step();
    CHECK_EQ(c.regs().pc, ORG + 3);
    CHECK_EQ(c.regs().s, 0xFD);
}

TEST(brk_pushes_pc_plus_two_with_b_set)
{
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0xFFFE) = 0x00;
    bus.at(0xFFFF) = 0x30;
    run(c, bus, {0x00, 0xEA}, 1); // BRK; (padding)
    CHECK_EQ(c.regs().pc, 0x3000);
    CHECK_EQ(bus.at(0x01FD), 0x02); // PCH
    CHECK_EQ(bus.at(0x01FC), 0x02); // PCL: BRK skips its padding byte
    CHECK(bus.at(0x01FB) & Cpu6502::FLAG_B);
    CHECK(c.regs().p & Cpu6502::FLAG_I);
}

TEST(irq_is_masked_by_i_and_pushes_b_clear)
{
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0xFFFE) = 0x00;
    bus.at(0xFFFF) = 0x40;

    run(c, bus, {0xEA}, 0, Registers{0, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});
    c.irq();
    CHECK_EQ(c.regs().pc, ORG); // masked: nothing happens

    run(c, bus, {0xEA}, 0, Registers{0, 0xFD, 0, 0, 0, Cpu6502::FLAG_U});
    c.irq();
    CHECK_EQ(c.regs().pc, 0x4000);
    CHECK(!(bus.at(0x01FB) & Cpu6502::FLAG_B));
    CHECK(c.regs().p & Cpu6502::FLAG_I);
}

TEST(reset_takes_7_cycles_sets_i_and_drops_s_by_3)
{
    FlatBus bus;
    Cpu6502 c(bus);
    bus.at(0xFFFC) = 0x00;
    bus.at(0xFFFD) = 0xE0;
    c.reset();
    CHECK_EQ(c.cycles(), uint64_t{7});
    CHECK_EQ(c.regs().pc, 0xE000);
    CHECK_EQ(c.regs().s, 0xFD); // from S = 0 at power-on
    CHECK(c.regs().p & Cpu6502::FLAG_I);
    CHECK(!(c.regs().p & Cpu6502::FLAG_B));
}

TEST(w65c02s_reset_and_interrupts_clear_d)
{
    FlatBus bus;
    Cpu6502 c(bus, Cpu6502::Model::W65C02S);
    bus.at(0xFFFE) = 0x00;
    bus.at(0xFFFF) = 0x40;
    c.set_regs(Registers{0x0200, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_D});
    c.reset();
    CHECK(!(c.regs().p & Cpu6502::FLAG_D));

    c.set_regs(Registers{0x0200, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_D});
    c.irq();
    CHECK(!(c.regs().p & Cpu6502::FLAG_D));
    CHECK(bus.at(0x01FB) & Cpu6502::FLAG_D); // the pushed copy keeps it

    Cpu6502 nmos(bus);
    nmos.set_regs(Registers{0x0200, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_D});
    nmos.irq();
    CHECK(nmos.regs().p & Cpu6502::FLAG_D); // NMOS leaves D alone
}

TEST(irq_and_nmi_take_7_cycles)
{
    FlatBus bus;
    Cpu6502 c(bus);
    c.set_regs(Registers{0x0200, 0xFD, 0, 0, 0, Cpu6502::FLAG_U});
    c.irq();
    CHECK_EQ(c.cycles(), uint64_t{7});
    c.nmi(); // not masked by the I that the IRQ just set
    CHECK_EQ(c.cycles(), uint64_t{14});
}

TEST(wai_waits_for_an_interrupt)
{
    FlatBus bus;
    Cpu6502 c(bus, Cpu6502::Model::W65C02S);
    bus.at(0xFFFE) = 0x00;
    bus.at(0xFFFF) = 0x40;
    run(c, bus, {0xCB, 0xEA}, 1); // WAI; NOP
    CHECK(c.state() == Cpu6502::State::Waiting);
    CHECK_EQ(c.regs().pc, ORG + 1);
    CHECK_EQ(c.step(), 1u); // the clock runs, nothing executes
    CHECK_EQ(c.regs().pc, ORG + 1);
    c.irq();
    CHECK(c.state() == Cpu6502::State::Running);
    CHECK_EQ(c.regs().pc, 0x4000);
}

TEST(wai_with_i_set_resumes_without_servicing)
{
    FlatBus bus;
    Cpu6502 c(bus, Cpu6502::Model::W65C02S);
    run(c, bus, {0xCB, 0xEA}, 1, Registers{0, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});
    c.irq();
    CHECK(c.state() == Cpu6502::State::Running);
    CHECK_EQ(c.regs().pc, ORG + 1); // carries on after the WAI
}

TEST(stp_stops_until_reset)
{
    FlatBus bus;
    Cpu6502 c(bus, Cpu6502::Model::W65C02S);
    bus.at(0xFFFC) = 0x00;
    bus.at(0xFFFD) = 0x02;
    run(c, bus, {0xDB}, 1);
    CHECK(c.state() == Cpu6502::State::Stopped);
    c.irq();
    c.nmi();
    CHECK(c.state() == Cpu6502::State::Stopped);
    c.reset();
    CHECK(c.state() == Cpu6502::State::Running);
    CHECK_EQ(c.regs().pc, 0x0200);
}

TEST(wai_and_stp_are_nops_on_nmos)
{
    FlatBus bus;
    Cpu6502 c(bus);
    run(c, bus, {0xCB, 0xDB}, 2);
    CHECK(c.state() == Cpu6502::State::Running);
}

TEST(observer_sees_every_access_with_sync_and_vector_pull)
{
    FlatBus bus;
    Cpu6502 c(bus, Cpu6502::Model::W65C02S);
    Recorder rec;
    c.set_observer(&rec);
    bus.at(0xFFFC) = 0x00;
    bus.at(0xFFFD) = 0x02;
    bus.at(0xFFFE) = 0x00;
    bus.at(0xFFFF) = 0x03;
    c.reset();
    CHECK_EQ(rec.seen.size(), 7u);
    for (size_t i = 0; i < rec.seen.size(); ++i)
    {
        CHECK_EQ(rec.seen[i].cycle, i + 1);
        CHECK(!rec.seen[i].sync);
        CHECK_EQ(rec.seen[i].vector_pull, i >= 5); // only the two $FFFC/D reads
    }
    CHECK_EQ(rec.seen[5].addr, 0xFFFC);

    rec.seen.clear();
    run(c, bus, {0x8D, 0x34, 0x12, 0x00}, 2, c.regs()); // STA $1234; BRK
    CHECK_EQ(rec.seen.size(), 4u + 7u);
    CHECK(rec.seen[0].sync);
    CHECK_EQ(rec.seen[0].data, 0x8D);
    CHECK(!rec.seen[1].sync);
    CHECK(rec.seen[3].write);
    CHECK_EQ(rec.seen[3].addr, 0x1234);
    CHECK(rec.seen[4].sync); // BRK's fetch
    int pulls = 0;
    for (const BusCycle &b : rec.seen)
        pulls += b.vector_pull;
    CHECK_EQ(pulls, 2);
    CHECK(rec.seen[9].vector_pull && rec.seen[9].addr == 0xFFFE);
    CHECK_EQ(rec.seen.back().cycle, c.cycles());

    c.set_observer(nullptr);
    c.step();
    CHECK_EQ(rec.seen.size(), 11u); // detached
}

int main()
{
    return run_all();
}
