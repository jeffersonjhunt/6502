/**
 * @file test_via.cpp
 * @brief The 6522 VIA's register, interrupt and timer rules.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Each case is a datasheet fact. Timer cases drive the chip directly with
 * tick() so every cycle is explicit. As through the decoder, the first
 * tick after a write is the write's own cycle, which a load does not
 * count: a load of N reaches 0 after N+1 ticks and times out on the next,
 * so a read in the cycle after that (N+2 after the write) is the first to
 * see the flag, the datasheet's N+1.5. Free-running mode repeats every
 * N+2. The last case runs a timer
 * interrupt through the CPU and decoder, which is what the lockstep timing
 * in the decoder is for.
 */

#include "harness.h"
#include "test_support.h"

#include "decoder.h"
#include "memory_devices.h"
#include "via6522.h"

#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t ORB = 0x0, ORA = 0x1, DDRB = 0x2, DDRA = 0x3, T1CL = 0x4, T1CH = 0x5, T1LL = 0x6, T1LH = 0x7,
                       T2CL = 0x8, T2CH = 0x9, ACR = 0xB, PCR = 0xC, IFR = 0xD, IER = 0xE, ORA_NH = 0xF;
    using P = Via6522::Port;

    void load_t1(Via6522 &v, uint16_t n)
    {
        v.write(T1CL, static_cast<uint8_t>(n));
        v.write(T1CH, static_cast<uint8_t>(n >> 8));
    }

    bool t1_flag(const Via6522 &v) { return v.peek(IFR) & Via6522::IRQ_T1; }
} // namespace

TEST(ports_mix_outputs_and_inputs_and_tell_listeners)
{
    Via6522 v;
    std::vector<std::pair<uint8_t, uint8_t>> seen_b;
    v.listen([&](P port, uint8_t out, uint8_t ddr) {
        if (port == P::B)
            seen_b.emplace_back(out, ddr);
    });
    v.write(DDRB, 0xF0);
    v.write(ORB, 0xA5);
    v.drive(P::B, 0x0F, 0x03);
    v.drive(P::B, 0x01, 0x00); // a second peripheral on another pin
    CHECK_EQ(v.read(ORB), 0xA2); // A from the outputs, 2 from the inputs
    CHECK_EQ(seen_b.size(), 2u);
    CHECK(seen_b[1] == std::make_pair(uint8_t{0xA5}, uint8_t{0xF0}));
    v.write(DDRA, 0xFF);
    v.write(ORA, 0x42);
    CHECK_EQ(v.read(ORA_NH), 0x42);
    CHECK_EQ(v.output(P::A), 0x42);
}

TEST(control_line_edges_follow_the_pcr)
{
    Via6522 v;
    v.set_ca1(true);  // rising, but PCR bit 0 = 0 wants falling
    CHECK(!(v.ifr() & Via6522::IRQ_CA1));
    v.set_ca1(false); // falling
    CHECK(v.ifr() & Via6522::IRQ_CA1);
    v.read(ORA);      // reading port A clears it
    CHECK(!(v.ifr() & Via6522::IRQ_CA1));

    v.write(PCR, 0x11); // CA1 and CB1 on rising edges
    v.set_ca1(true);
    v.set_cb1(true);
    CHECK_EQ(v.ifr() & (Via6522::IRQ_CA1 | Via6522::IRQ_CB1), Via6522::IRQ_CA1 | Via6522::IRQ_CB1);
    v.read(ORA_NH); // no handshake: flags stay
    CHECK(v.ifr() & Via6522::IRQ_CA1);
    v.write(ORB, 0); // writing port B clears CB1
    CHECK(!(v.ifr() & Via6522::IRQ_CB1));
}

TEST(ca2_independent_mode_survives_port_access)
{
    Via6522 v;
    v.write(PCR, 0x02); // CA2 input, negative edge, independent
    v.set_ca2(true);
    v.set_ca2(false);
    CHECK(v.ifr() & Via6522::IRQ_CA2);
    v.read(ORA);
    CHECK(v.ifr() & Via6522::IRQ_CA2); // independent: not cleared by the port
    v.write(IFR, Via6522::IRQ_CA2);   // only by writing IFR
    CHECK(!(v.ifr() & Via6522::IRQ_CA2));

    v.write(PCR, 0x00); // ordinary input mode: the port read clears it
    v.set_ca2(true);
    v.set_ca2(false);
    v.read(ORA);
    CHECK(!(v.ifr() & Via6522::IRQ_CA2));

    v.write(PCR, 0x0C); // manual output, low
    CHECK(!v.ca2_out());
    v.write(PCR, 0x0E);
    CHECK(v.ca2_out());
}

TEST(ifr_ier_and_the_irq_line)
{
    Via6522 v;
    CHECK_EQ(v.read(IER), 0x80); // bit 7 reads as 1
    v.set_ca1(true);
    v.set_ca1(false);
    CHECK_EQ(v.read(IFR), Via6522::IRQ_CA1); // flagged, not enabled: bit 7 clear
    CHECK(!v.irq());
    v.write(IER, 0x80 | Via6522::IRQ_CA1 | Via6522::IRQ_T1); // set bits
    CHECK_EQ(v.read(IER), 0x80 | Via6522::IRQ_CA1 | Via6522::IRQ_T1);
    CHECK_EQ(v.read(IFR), 0x80 | Via6522::IRQ_CA1);
    CHECK(v.irq());
    v.write(IER, Via6522::IRQ_CA1); // bit 7 clear: clear bits
    CHECK_EQ(v.read(IER), 0x80 | Via6522::IRQ_T1);
    CHECK(!v.irq());
}

TEST(t1_one_shot_times_out_once_after_n_plus_1_cycles)
{
    Via6522 v;
    load_t1(v, 10);
    CHECK_EQ(v.t1_counter(), 10);
    v.tick(1); // the write's own cycle: no decrement
    CHECK_EQ(v.t1_counter(), 10);
    v.tick(1);
    CHECK_EQ(v.t1_counter(), 9);
    v.tick(9); // at 0, not yet timed out
    CHECK_EQ(v.t1_counter(), 0);
    CHECK(!t1_flag(v));
    v.tick(1); // 0 -> $FFFF: timeout, N+2 ticks after the write
    CHECK_EQ(v.t1_counter(), 0xFFFF);
    CHECK(t1_flag(v));
    v.read(T1CL); // reading T1C-L clears the flag
    CHECK(!t1_flag(v));
    v.tick(0x10000); // wraps again, but one-shot means one interrupt
    CHECK(!t1_flag(v));
    CHECK_EQ(v.t1_counter(), 0xFFFF);

    load_t1(v, 3);
    v.tick(5);
    CHECK(t1_flag(v));
    v.write(T1LH, 0); // writing the high latch clears it too, without reloading
    CHECK(!t1_flag(v));
    CHECK_EQ(v.t1_counter(), 0xFFFF);
}

TEST(t1_free_run_period_is_n_plus_2_and_drives_pb7)
{
    Via6522 v;
    std::vector<uint8_t> pb7;
    v.listen([&](P port, uint8_t out, uint8_t) {
        if (port == P::B)
            pb7.push_back(out & 0x80);
    });
    v.write(ACR, Via6522::ACR_T1_FREE_RUN | Via6522::ACR_T1_PB7);
    load_t1(v, 10);
    CHECK_EQ(pb7.back(), 0x00); // the load drives PB7 low, in either mode
    v.tick(12);
    CHECK(t1_flag(v));
    CHECK_EQ(pb7.back(), 0x80); // then each timeout inverts it
    CHECK_EQ(v.t1_counter(), 0xFFFF);
    v.tick(1); // the reload cycle
    CHECK_EQ(v.t1_counter(), 10);
    v.write(IFR, Via6522::IRQ_T1);
    v.tick(10); // 11 cycles since the timeout; the period N+2 is 12
    CHECK(!t1_flag(v));
    v.tick(1);
    CHECK(t1_flag(v));
    CHECK_EQ(pb7.back(), 0x00);
    v.tick(12);
    CHECK_EQ(pb7.back(), 0x80);
    CHECK_EQ(v.read(ORB) & 0x80, 0x80); // PB7 reads T1's level, DDRB notwithstanding

    // Many periods at once: the count stays exact.
    v.write(IFR, Via6522::IRQ_T1);
    const uint16_t before = v.t1_counter();
    v.tick(12 * 1000);
    CHECK_EQ(v.t1_counter(), before);

    Via6522 one;
    one.write(ACR, Via6522::ACR_T1_PB7); // one-shot: low from load to timeout
    load_t1(one, 5);
    CHECK_EQ(one.output(P::B) & 0x80, 0);
    one.tick(7);
    CHECK_EQ(one.output(P::B) & 0x80, 0x80);
}

TEST(t2_one_shot_and_pulse_counting)
{
    Via6522 v;
    v.write(T2CL, 5);
    v.write(T2CH, 0);
    v.tick(6);
    CHECK(!(v.ifr() & Via6522::IRQ_T2));
    v.tick(1);
    CHECK(v.ifr() & Via6522::IRQ_T2);
    v.read(T2CL);
    CHECK(!(v.ifr() & Via6522::IRQ_T2));
    v.tick(0x10000);
    CHECK(!(v.ifr() & Via6522::IRQ_T2)); // one-shot

    Via6522 c;
    c.write(ACR, Via6522::ACR_T2_COUNT_PB6);
    c.write(T2CL, 3);
    c.write(T2CH, 0);
    c.tick(100); // cycles do not count in this mode
    CHECK_EQ(c.t2_counter(), 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK(!(c.ifr() & Via6522::IRQ_T2));
        c.drive(P::B, 0x40, 0x40);
        c.drive(P::B, 0x40, 0x00); // a falling edge on PB6
    }
    CHECK(c.ifr() & Via6522::IRQ_T2);
}

TEST(peek_has_no_side_effects)
{
    Via6522 v;
    v.set_ca1(true);
    v.set_ca1(false);
    load_t1(v, 1);
    v.tick(3);
    for (uint32_t r = 0; r < 16; ++r)
        v.peek(r);
    CHECK(v.ifr() & Via6522::IRQ_CA1);
    CHECK(t1_flag(v));
}

TEST(timer_interrupts_through_the_cpu_in_lockstep)
{
    // RAM $0000-$3FFF, VIA $6000-$7FFF (mirrored every 16), ROM-ish RAM at $8000.
    Ram low(0x4000), high(0x8000);
    Via6522 via;
    Decoder bus;
    bus.map({0x0000, 0x3FFF, &low});
    bus.map({0x6000, 0x7FFF, &via, NO_MODULE, true});
    bus.map({0x8000, 0xFFFF, &high});
    const uint8_t program[] = {
        0xA9, 0x40, 0x8D, 0x0B, 0x60, // LDA #$40; STA ACR   free-running T1
        0xA9, 0xC0, 0x8D, 0x0E, 0x60, // LDA #$C0; STA IER   enable T1
        0xA9, 0xF6, 0x8D, 0x04, 0x60, // LDA #$F6; STA T1CL
        0xA9, 0x03, 0x8D, 0x05, 0x70, // LDA #$03; STA $7005 (a mirror of T1CH): N = $3F6 = 1014
        0x58,                         // CLI
        0x4C, 0x15, 0x80,             // JMP *
        // $8018: IRQ handler
        0xE6, 0x10,                   // INC $10
        0xAD, 0x04, 0x60,             // LDA T1CL (clears T1)
        0x40,                         // RTI
    };
    for (size_t i = 0; i < sizeof program; ++i)
        bus.poke(static_cast<uint16_t>(0x8000 + i), program[i]);
    bus.poke(0xFFFC, 0x00);
    bus.poke(0xFFFD, 0x80);
    bus.poke(0xFFFE, 0x18);
    bus.poke(0xFFFF, 0x80);

    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    cpu.reset();
    while (cpu.cycles() < 200'000)
    {
        cpu.step();
        if (bus.irq())
            cpu.irq();
    }
    CHECK_EQ(bus.now(), cpu.cycles()); // the decoder's clock is the CPU's
    // Reset is 7 cycles and each LDA #/STA abs pair 6, so the T1C-H store
    // writes in cycle 31. Timeouts fall at 31 + N + 1 = 1046, then every
    // N + 2 = 1016: 1046 + 195 * 1016 = 199166 is the last before 200000,
    // and its handler has run by then. So exactly 196.
    CHECK_EQ(bus.peek(0x0010), 196);
}

TEST(wai_sleeps_until_the_timer_and_idle_cycles_count)
{
    // WAI makes no bus accesses, so this only works if its idle cycles
    // still reach the decoder's clock (Bus::idle()).
    Ram low(0x4000), high(0x8000);
    Via6522 via;
    Decoder bus;
    bus.map({0x0000, 0x3FFF, &low});
    bus.map({0x6000, 0x7FFF, &via, NO_MODULE, true});
    bus.map({0x8000, 0xFFFF, &high});
    const uint8_t program[] = {
        0xA9, 0xC0, 0x8D, 0x0E, 0x60, // LDA #$C0; STA IER    enable T1
        0xA9, 0xE8, 0x8D, 0x04, 0x60, // LDA #$E8; STA T1CL
        0xA9, 0x03, 0x8D, 0x05, 0x60, // LDA #$03; STA T1CH   one-shot, N = 1000
        0xCB,                         // WAI (I is set: wakes without servicing)
        0xDB,                         // STP
    };
    for (size_t i = 0; i < sizeof program; ++i)
        bus.poke(static_cast<uint16_t>(0x8000 + i), program[i]);
    bus.poke(0xFFFC, 0x00);
    bus.poke(0xFFFD, 0x80);

    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    cpu.reset();
    uint64_t woke = 0;
    while (cpu.cycles() < 5000 && cpu.state() != Cpu6502::State::Stopped)
    {
        const bool waiting = cpu.state() == Cpu6502::State::Waiting;
        cpu.step();
        if (bus.irq())
            cpu.irq();
        if (waiting && cpu.state() == Cpu6502::State::Running && woke == 0)
            woke = cpu.cycles();
    }
    // The T1C-H store writes in cycle 7 + 6 + 6 + 6 = 25, the timeout is
    // 1001 cycles later at 1026, and the idle cycle that sees it ends the WAI.
    CHECK_EQ(woke, 1026u);
    CHECK_EQ(bus.now(), cpu.cycles());
    CHECK(cpu.state() == Cpu6502::State::Stopped);
}

int main()
{
    return run_all();
}
