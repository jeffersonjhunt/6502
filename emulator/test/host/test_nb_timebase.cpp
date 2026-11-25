/**
 * @file test_nb_timebase.cpp
 * @brief The north bridge's timebase: the 1 kHz tick, the millisecond counter and MSS.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("The timebase"; its tests B1-B9 in hw/testing.md) is the
 * spec; backplane_bridge.h says how the emulator times it. The CPU runs at
 * NATIVE_HZ, so a cycle is two OSC clocks and tick k comes floor(157,500 k /
 * 11) OSC clocks after the reset's release: in cycle floor(78,750 k / 11),
 * counted from 0.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "cpu6502.h"
#include "dma_controller.h"
#include "ds3231.h"
#include "machine.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t NB = 0xC680;
    constexpr uint16_t RTCCTL = NB + BackplaneBridge::RTCCTL;
    constexpr uint16_t SEC = NB + BackplaneBridge::SEC;
    constexpr uint16_t MSSL = NB + BackplaneBridge::MSSL;
    constexpr uint16_t MSSH = NB + BackplaneBridge::MSSH;
    constexpr uint16_t TBCTL = NB + BackplaneBridge::TBCTL;
    constexpr uint16_t MS0 = NB + BackplaneBridge::MS0;
    constexpr uint16_t TBSUBL = NB + BackplaneBridge::TBSUBL;
    constexpr uint16_t TBSUBH = NB + BackplaneBridge::TBSUBH;
    constexpr uint16_t IRQH = NB + BackplaneBridge::IRQH;
    constexpr uint8_t MSF = BackplaneBridge::TB_MSF, OVR = BackplaneBridge::TB_OVR, MSIE = BackplaneBridge::TB_MSIE,
                      SYNC = BackplaneBridge::TB_SYNC;
    constexpr uint64_t HZ = BackplaneBridge::NATIVE_HZ;
    const Ds3231::Time START{2026, 11, 22, 9, 30, 0, 1};

    /// The cycle (from the release, counted from 0) tick k lands in.
    uint64_t tick_cycle(uint64_t k) { return 78'750 * k / 11; }

    /// The bridge on 1 MB of RAM (and the DMA controller with `dma`), powered on.
    struct Rig
    {
        explicit Rig(bool dma = false)
        {
            MachineOptions opt;
            opt.rtc = std::make_shared<Ds3231>(START);
            std::string error;
            std::string text = "cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF name=ram\n";
            if (dma)
                text += "dma 400-47F space=io name=dma irq\n";
            m = Machine::build(text, "nb3.machine", opt, error);
            if (!m)
                std::printf("  %s\n", error.c_str());
            bridge = m->backplane_bridge();
            rtc = bridge->rtc().get();
            m->bus().reset();
        }
        Decoder &bus() { return m->bus(); }
        /// `n` idle bus cycles, then everything caught up.
        void idle(uint64_t n)
        {
            for (uint64_t i = 0; i < n; ++i)
                bus().idle();
            bus().sync();
        }
        /// Idle to cycle `c` from the release.
        void to(uint64_t c)
        {
            if (bus().now() < c)
                idle(c - bus().now());
        }
        bool line() // IRQH bit 5 low: the bridge interrupting
        {
            return (bus().read(IRQH) & 0x20) == 0;
        }
        void settle()
        {
            for (bus().sync(); bridge->rtcctl() & BackplaneBridge::RTC_BUSY; bus().sync())
                bus().idle();
        }

        std::unique_ptr<Machine> m;
        BackplaneBridge *bridge{nullptr};
        Ds3231 *rtc{nullptr};
    };
} // namespace

TEST(ticks_come_at_the_crystal_s_millisecond)
{
    // B1. Each of ticks 1-1,100 lands in cycle floor(78,750 k / 11): not a cycle before, and MS gains it then. After
    // 1,100 ticks (100 times 11 ms) MS is 1,100. Must fail with the 2-in-11 step removed (7,159 cycles always: 12.7
    // ppm fast), or the long periods on the wrong count.
    Rig r;
    REQUIRE(r.bus().now() == 0);
    unsigned wrong = 0;
    for (uint64_t k = 1; k <= 1100; ++k)
    {
        r.to(tick_cycle(k));
        const uint32_t before = r.bridge->ms();
        r.idle(1);
        const uint32_t after = r.bridge->ms();
        if ((before != k - 1 || after != k) && wrong++ < 5)
            std::printf("  tick %llu: MS %u before its cycle %llu, %u after\n", static_cast<unsigned long long>(k),
                        before, static_cast<unsigned long long>(tick_cycle(k)), after);
    }
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(r.bridge->ms(), 1100u);
    CHECK_EQ(tick_cycle(1100), 7'875'000u); // 1.1 s of the crystal: 78,750 cycles in 11 ms
}

TEST(the_latch_holds_four_bytes_across_a_carry)
{
    // B2. MS just below a carry into MS3: MS0 read, the carry, then MS1-MS3 give the latch's moment, not the
    // count's now; TBSUB comes from the same latch, and a later read of MS0 takes a new one. Must fail with MS1-MS3
    // read live, or TBSUB not latched.
    Rig r;
    r.to(tick_cycle(3) + 1000);
    r.bridge->set_ms(0x00FFFFFE);
    r.to(tick_cycle(4) + 2000); // MS $00FFFFFF, 2,000 cycles into the millisecond
    CHECK_EQ(r.bus().read(MS0), 0xFF);
    const unsigned sub = r.bus().read(TBSUBL) | r.bus().read(TBSUBH) << 8;
    CHECK(sub >= 1999 && sub <= 2001);
    r.to(tick_cycle(5) + 500); // the carry: MS $01000000
    REQUIRE(r.bridge->ms() == 0x01000000);
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::MS0 + 1), 0xFF);
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::MS0 + 2), 0xFF);
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::MS0 + 3), 0x00);
    CHECK_EQ(static_cast<unsigned>(r.bus().read(TBSUBL) | r.bus().read(TBSUBH) << 8), sub);
    CHECK_EQ(r.bus().read(MS0), 0x00); // a new latch, six bus cycles after tick 5's + 500
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::MS0 + 3), 0x01);
    const unsigned now = r.bus().read(TBSUBL) | r.bus().read(TBSUBH) << 8;
    if (now < 504 || now > 507)
        std::printf("  TBSUB %u after the carry, %u before\n", now, sub);
    CHECK(now >= 504 && now <= 507);
}

TEST(msf_interrupts_until_acknowledged)
{
    // B3. MSF with MSIE pulls IRQH bit 5; a read of TBCTL leaves it, a 0 written to MSF leaves it, a 1 lets it go
    // (until the next tick); MSIE off lets it go with MSF still set. Must fail with the acknowledge done by a read
    // (as the VIA's T1CL), or irq() not gated by MSIE.
    Rig r;
    r.bus().write(TBCTL, MSIE);
    r.to(tick_cycle(1) + 1);
    CHECK(r.bus().irq());
    CHECK(r.line());
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | MSIE), MSF | MSIE);
    CHECK(r.bus().irq()); // the read cleared nothing
    r.bus().write(TBCTL, MSIE); // MSF 0: left as it is
    CHECK(r.bus().irq());
    r.bus().write(TBCTL, MSF | OVR | MSIE); // the tick's acknowledge
    CHECK(!r.bus().irq());
    CHECK(!r.line());
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | OVR | MSIE), MSIE);
    r.to(tick_cycle(2) + 1);
    CHECK(r.bus().irq());
    r.bus().write(TBCTL, 0x00); // MSIE off, MSF kept
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | MSIE), MSF);
}

TEST(a_tick_under_dma_is_counted)
{
    // B4. 6502 code starts a 64 KB copy, memory to memory, that holds the CPU 131,073 cycles (18.3 ms): MS has
    // every tick it covered (18, exactly), MSF and OVR are set, and with MSIE it is one interrupt, gone at one
    // acknowledge. Must fail if OVR is never set, or the counter misses ticks the CPU didn't see.
    Rig r(true);
    const uint8_t sel3 = static_cast<uint8_t>(0x71 | 3 << DmaController::SELECT_SHIFT);
    const std::vector<uint8_t> prog = {
        0xA9, sel3, 0x8D, 0x7E, 0xC4, // LDA #$71|SELECT 3; STA CONTROL
        0xA9, 0x00, 0x8D, 0x00, 0xC4, 0x8D, 0x01, 0xC4, 0xA9, 0x02, 0x8D, 0x02, 0xC4, // source $20000
        0xA9, 0x00, 0x8D, 0x08, 0xC4, 0x8D, 0x09, 0xC4, 0xA9, 0x03, 0x8D, 0x0A, 0xC4, // dest $30000
        0xA9, 0x00, 0x8D, 0x04, 0xC4, 0x8D, 0x05, 0xC4,                               // COUNT 0: 64 KB
        0xA9, 0x08, 0x8D, 0x06, 0xC4,                                                 // MODE memory to memory
        0xA9, 0x03, 0x8D, 0x07, 0xC4,                                                 // CTRL ENABLE + REQ
        0xEA,                                                                         // NOP: the copy runs here
        0xDB,                                                                         // STP
    };
    for (size_t i = 0; i < prog.size(); ++i)
        r.bus().poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
    Cpu6502 cpu(r.bus(), Cpu6502::Model::W65C02S);
    Registers regs;
    regs.pc = 0x0200;
    regs.p = 0x24;
    cpu.set_regs(regs);
    for (int i = 0; i < 1000 && cpu.state() != Cpu6502::State::Stopped; ++i)
        cpu.step();
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);
    r.bus().sync();
    const uint64_t now = r.bus().now();
    std::printf("  the program and the copy: %llu cycles\n", static_cast<unsigned long long>(now));
    CHECK(now > 131'073 && now < tick_cycle(19));
    CHECK_EQ(r.bridge->ms(), 18u);
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | OVR), MSF | OVR);
    r.bus().write(TBCTL, MSIE);
    CHECK(r.bus().irq());
    r.bus().write(TBCTL, MSF | OVR | MSIE);
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | OVR), 0);
}

TEST(mss_restarts_with_each_second)
{
    // B5. The DS3231 1,000 ppm fast, then 1,000 ppm slow, for 30 s each: read in order (SEC, then MSSL and MSSH)
    // every 300 cycles, the time never goes backwards, MSS is never past 999, and at each second's change it reads
    // 0 or 1. Across each second's read (SEC read before it lands, MSSL and MSSH after), MSS is still the old
    // second's (990 or more), from SEC's latch: never the old second with the new one's milliseconds, whole or
    // in part. Must fail with MSS the free-running tick's count modulo 1,000, or
    // MSSL and MSSH read live.
    for (const double ppm : {1000.0, -1000.0})
    {
        std::printf("  the chip %+.0f ppm\n", ppm);
        Rig r;
        r.rtc->set_skew_ppm(ppm);
        r.bus().sync();
        unsigned seconds = 0, bad_change = 0, backwards = 0, past = 0, torn = 0;
        uint8_t last_sec = r.bus().read(SEC);
        unsigned last_ms = r.bus().read(MSSL) | r.bus().read(MSSH) << 8;
        bool busy_seen = false;
        const uint64_t end = 30 * HZ;
        while (r.bus().now() < end)
        {
            r.idle(300);
            const bool busy = (r.bridge->rtcctl() & BackplaneBridge::RTC_BUSY) != 0;
            if (busy && !busy_seen && seconds > 0)
            {
                // The edge fell; its read is under way: SEC now, MSS once it has landed.
                const uint8_t sec = r.bus().read(SEC);
                r.settle();
                const unsigned ms = r.bus().read(MSSL) | r.bus().read(MSSH) << 8;
                if (sec == last_sec && ms < 990 && torn++ < 3)
                    std::printf("  SEC %02X with MSS %u across the read\n", sec, ms);
            }
            busy_seen = busy;
            const uint8_t sec = r.bus().read(SEC);
            const unsigned ms = r.bus().read(MSSL) | r.bus().read(MSSH) << 8;
            if (ms > 999)
                ++past;
            if (sec != last_sec)
            {
                ++seconds;
                if (ms > 1 && bad_change++ < 3)
                    std::printf("  second %u: MSS %u at the change\n", seconds, ms);
            }
            else if (ms < last_ms && backwards++ < 3)
                std::printf("  SEC %02X: MSS %u after %u\n", sec, ms, last_ms);
            last_sec = sec;
            last_ms = ms;
        }
        CHECK(seconds >= 29 && seconds <= 31);
        CHECK_EQ(bad_change, 0u);
        CHECK_EQ(backwards, 0u);
        CHECK_EQ(past, 0u);
        CHECK_EQ(torn, 0u);
        CHECK(r.bus().read(TBCTL) & SYNC);
    }
}

TEST(the_tick_needs_no_clock_chip)
{
    // B6. SYNC is clear from power-on until the first second's read lands. Then the chip's square wave stops (its
    // INTCN set behind the bridge's back): SYNC stays 1.1 s after the last edge and then clears, MSS holds at 999,
    // and the tick and MS go on as before. Must fail with the tick taken from the chip's seconds, or SYNC never
    // cleared.
    Rig r;
    CHECK_EQ(r.bus().read(TBCTL) & SYNC, 0);
    while (!(r.bridge->rtcctl() & BackplaneBridge::RTC_BUSY))
        r.idle(100);
    const uint64_t edge = r.bus().now();
    CHECK_EQ(r.bus().read(TBCTL) & SYNC, 0); // the edge, its read not landed yet
    r.settle();
    CHECK(r.bus().read(TBCTL) & SYNC);
    r.rtc->write(Ds3231::CONTROL, Ds3231::INTCN); // SQW stopped: no more edges
    r.to(edge + HZ * 11 / 10 - 200);
    CHECK(r.bus().read(TBCTL) & SYNC);
    r.to(edge + HZ * 11 / 10 + 200);
    CHECK_EQ(r.bus().read(TBCTL) & SYNC, 0);
    r.to(edge + 3 * HZ);
    r.bus().read(SEC);
    CHECK_EQ(r.bus().read(MSSL) | r.bus().read(MSSH) << 8, 999);
    const uint64_t c = r.bus().now();
    uint64_t k = 0;
    while (tick_cycle(k + 1) < c)
        ++k;
    CHECK_EQ(uint64_t{r.bridge->ms()}, k); // every tick since the release
}

TEST(tie_is_gone_on_revision_3)
{
    // B7. TICK is still set by each second's read and cleared by a 1, but TIE written 1 reads 0 and nothing
    // interrupts. Must fail with TIE kept, or TICK interrupting.
    Rig r;
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    CHECK_EQ(r.bus().read(RTCCTL) & BackplaneBridge::RTC_TIE, 0);
    while (!(r.bridge->rtcctl() & BackplaneBridge::RTC_BUSY))
        r.idle(100);
    r.settle();
    CHECK(r.bus().read(RTCCTL) & BackplaneBridge::RTC_TICK);
    CHECK(!r.bus().irq());
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TICK);
    CHECK_EQ(r.bus().read(RTCCTL) & BackplaneBridge::RTC_TICK, 0);
    CHECK(!r.bus().irq());
}

TEST(resets_and_apple_ii_mode)
{
    // B8. A hard reset: MS 0 at the first instruction and MSIE off. A /RESET pulse (Apple II mode's entry): MS runs
    // on, MSIE off. In Apple II mode (912 OSC clocks in 65 cycles) MS gains 1,000 a second of the crystal. Must
    // fail with the divider counting the CPU's cycles (sevenfold slow in Apple II mode), or the pulse clearing MS.
    Rig r;
    r.bus().write(TBCTL, MSIE);
    r.idle(HZ / 10);
    REQUIRE(r.bridge->ms() >= 99);
    r.bus().reset();
    CHECK_EQ(r.bridge->ms(), 0u);
    CHECK_EQ(r.bus().read(TBCTL) & (MSF | OVR | MSIE), 0);
    r.bus().write(TBCTL, MSIE);
    r.idle(HZ / 10);
    const uint32_t before = r.bridge->ms();
    r.bus().write(NB + BackplaneBridge::MODE, BackplaneBridge::MODE_APPLE);
    REQUIRE(r.bridge->take_reset());
    r.bridge->reset_pulse();
    REQUIRE(r.bridge->apple());
    CHECK(r.bridge->ms() >= before);
    CHECK_EQ(r.bridge->tbctl() & MSIE, 0); // (in Apple II mode the CPU's $C6A8 is the IIe's)
    const uint32_t from = r.bridge->ms();
    r.idle(BackplaneBridge::APPLE_HZ); // a second at 1,020,484 Hz
    const uint32_t gained = r.bridge->ms() - from;
    std::printf("  a second in Apple II mode: MS gained %u\n", gained);
    CHECK(gained >= 999 && gained <= 1001);
}

TEST(a_peek_latches_nothing)
{
    // B9. The debugger looks at MS0 and SEC (peek, as the WebUI's memory view does) between a CPU's read of MS0 and
    // its read of MS1, and of SEC and MSSL: the CPU still gets its own latch. Must fail with peek() calling read().
    Rig r;
    r.idle(1000);
    r.bridge->set_ms(0x123456FD);
    CHECK_EQ(r.bus().read(MS0), 0xFD);
    CHECK_EQ(r.bus().read(SEC), 0x00);
    const unsigned mss = r.bus().read(MSSL);
    r.to(tick_cycle(6)); // MS past $12345700, MSS on
    REQUIRE(r.bridge->ms() >= 0x12345700);
    CHECK_EQ(r.bus().peek(MS0), static_cast<uint8_t>(r.bridge->ms()));
    r.bus().peek(SEC);
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::MS0 + 1), 0x56);
    CHECK_EQ(r.bus().read(MSSL), mss);
}

int main()
{
    return run_all();
}
