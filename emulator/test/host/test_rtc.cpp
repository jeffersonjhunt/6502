/**
 * @file test_rtc.cpp
 * @brief The north bridge's real-time clock: RTCCTL, SEC-YEAR, RTCREG/RTCDATA.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Real-time clock") is the spec; backplane_bridge.h says how
 * the bridge's I²C work is timed. Most cases run the CPU clock at 400 kHz,
 * so an I²C bit is one cycle and a transaction's BUSY lasts its bit count.
 * Then 6502 code on machines/backplane.machine reads the time through
 * $C690-$C696 and takes a TICK interrupt, and a session keeps the clock
 * through a power cycle.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "cpu6502.h"
#include "debugger.h"
#include "ds3231.h"
#include "machine.h"
#include "session.h"
#include "symbols.h"

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
    constexpr uint16_t RTCREG = NB + BackplaneBridge::RTCREG;
    constexpr uint16_t RTCDATA = NB + BackplaneBridge::RTCDATA;
    constexpr uint32_t HZ = BackplaneBridge::I2C_HZ; ///< a bit a cycle
    const Ds3231::Time START{2026, 10, 26, 13, 45, 7, 2};

    /// The bridge on 1 MB of RAM, the clock at START, reset (powered on).
    struct Rig
    {
        explicit Rig(const Ds3231::Time &t = START, bool lost = false)
        {
            MachineOptions opt;
            opt.rtc = std::make_shared<Ds3231>(t, lost);
            std::string error;
            m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n", "rtc.machine",
                               opt, error);
            if (!m)
                std::printf("  %s\n", error.c_str());
            bridge = m->backplane_bridge();
            rtc = bridge->rtc().get();
            bridge->set_cpu_hz(HZ);
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
        /// Idle until BUSY clears. @return the cycles it took
        uint64_t settle()
        {
            uint64_t n = 0;
            for (bus().sync(); bridge->rtcctl() & BackplaneBridge::RTC_BUSY; bus().sync(), ++n)
                bus().idle();
            return n;
        }
        /// Run to just after the next second's refresh has finished.
        void next_tick()
        {
            const uint64_t was = rtc_seconds();
            while (rtc_seconds() == was)
                idle(1000);
            settle();
        }
        uint64_t rtc_seconds() const { return rtc->time().second; }
        std::vector<uint8_t> time()
        {
            std::vector<uint8_t> t;
            for (uint16_t i = 0; i < 7; ++i)
                t.push_back(bus().read(static_cast<uint16_t>(SEC + i)));
            return t;
        }

        std::unique_ptr<Machine> m;
        BackplaneBridge *bridge{nullptr};
        Ds3231 *rtc{nullptr};
    };

    const std::vector<uint8_t> START_REGS = {0x07, 0x45, 0x13, 0x02, 0x26, 0x10, 0x26};
} // namespace

TEST(power_on_sets_1_hz_and_reads_the_time_with_busy_clear)
{
    Rig r;
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    CHECK(r.time() == START_REGS);
    CHECK_EQ(r.rtc->read(Ds3231::CONTROL), 0x00); // INTCN and RS cleared from the chip's $1C
    CHECK(!r.bus().irq());
    Rig lost(START, true);
    CHECK_EQ(lost.bus().read(RTCCTL), BackplaneBridge::RTC_LOST); // OSF: the time is invalid until set
}

TEST(each_second_the_bridge_reads_the_time_and_sets_tick)
{
    Rig r;
    r.idle(HZ - 1); // the chip's second ends with cycle HZ
    CHECK_EQ(r.bridge->rtcctl(), 0x00);
    r.idle(1);
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_BUSY); // SQW fell: reading 7 + 1 registers
    CHECK_EQ(r.bridge->rtc_time()[0], 0x07);
    CHECK_EQ(r.settle(), uint64_t{BackplaneBridge::REFRESH_BITS});
    CHECK_EQ(BackplaneBridge::REFRESH_BITS, 132u);
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_TICK);
    CHECK_EQ(r.bridge->rtc_time()[0], 0x08);
    r.bus().write(RTCCTL, 0x00); // a 0 leaves TICK
    CHECK(r.bus().read(RTCCTL) & BackplaneBridge::RTC_TICK);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TICK); // write 1 to clear
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    r.next_tick();
    CHECK_EQ(r.bus().read(SEC), 0x09);
}

TEST(reading_sec_latches_all_seven_across_a_second)
{
    Rig r(Ds3231::Time{2026, 12, 31, 23, 59, 59, 5});
    CHECK_EQ(r.bus().read(SEC), 0x59); // latched here...
    r.next_tick();                      // ...and the year turns over
    CHECK_EQ(r.bus().peek(SEC), 0x00);  // SEC itself is live
    CHECK_EQ(r.bus().read(SEC + 1), 0x59);
    CHECK_EQ(r.bus().read(SEC + 2), 0x23);
    CHECK_EQ(r.bus().read(SEC + 4), 0x31);
    CHECK_EQ(r.bus().read(SEC + 5), 0x12);
    CHECK_EQ(r.bus().read(SEC + 6), 0x26);
    CHECK(r.time() == (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x06, 0x01, 0x01, 0x27}));
}

TEST(tick_with_tie_interrupts_as_irqh_bit_5)
{
    Rig r;
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    CHECK_EQ(r.bus().read(RTCCTL), BackplaneBridge::RTC_TIE);
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::IRQH), 0xFF);
    r.next_tick();
    CHECK(r.bus().irq());
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::IRQH), 0xDF); // ID 0xD
    CHECK_EQ(r.bus().read(NB + BackplaneBridge::IRQL), 0xFF);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TICK | BackplaneBridge::RTC_TIE);
    CHECK(!r.bus().irq());
    r.next_tick();
    CHECK(r.bus().irq());
    r.bus().write(RTCCTL, 0x00); // TIE off: TICK stays, the interrupt goes
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bus().read(RTCCTL), BackplaneBridge::RTC_TICK);
}

TEST(set_sends_sec_to_year_and_clears_osf)
{
    Rig r(START, true);
    r.idle(HZ / 2);
    const std::vector<uint8_t> t = {0x30, 0x15, 0x09, 0x07, 0x01, 0x81, 0x05}; // Saturday 2105-01-01 09:15:30
    for (uint16_t i = 0; i < 7; ++i)
        r.bus().write(static_cast<uint16_t>(SEC + i), t[i]);
    CHECK(r.time() == START_REGS); // written to the holding copy; reads give the time
    r.bus().write(RTCCTL, BackplaneBridge::RTC_SET);
    r.bus().sync(); // the write's own cycle is the first of SET's 112
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_LOST | BackplaneBridge::RTC_SET | BackplaneBridge::RTC_BUSY);
    r.idle(BackplaneBridge::SET_BITS - 2);
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_LOST | BackplaneBridge::RTC_SET | BackplaneBridge::RTC_BUSY);
    r.idle(1);
    CHECK_EQ(BackplaneBridge::SET_BITS, 112u);
    CHECK(r.rtc->time() == (Ds3231::Time{2105, 1, 1, 9, 15, 30, 7}));
    CHECK_EQ(r.rtc->read(Ds3231::STATUS) & Ds3231::OSF, 0);
    // Writing the seconds restarted the second, and SQW, high half a second in, fell with it:
    // the bridge reads the new time back, and TICKs.
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_BUSY); // LOST and SET off
    CHECK_EQ(r.settle(), uint64_t{BackplaneBridge::REFRESH_BITS});
    CHECK_EQ(r.bridge->rtcctl(), BackplaneBridge::RTC_TICK);
    CHECK(r.time() == t);
    // The next second is a whole second after the write, not START's.
    r.idle(HZ - BackplaneBridge::REFRESH_BITS - 7 - 1); // time()'s 7 reads are cycles too
    CHECK_EQ(r.rtc->time().second, 30u);
    r.idle(1);
    CHECK_EQ(r.rtc->time().second, 31u);
}

TEST(rtcreg_and_rtcdata_reach_the_other_registers)
{
    Rig r;
    r.bus().write(RTCREG, Ds3231::TEMP_MSB);
    r.bus().sync();
    CHECK(r.bridge->rtcctl() & BackplaneBridge::RTC_BUSY);
    CHECK_EQ(r.settle(), uint64_t{BackplaneBridge::READ_BITS + 9} - 1);
    CHECK_EQ(r.bus().read(RTCREG), Ds3231::TEMP_MSB);
    CHECK_EQ(r.bus().read(RTCDATA), 0x19); // 25 °C
    r.bus().write(RTCREG, Ds3231::AGING);
    r.settle();
    r.bus().write(RTCDATA, 0x85);
    r.bus().sync();
    CHECK(r.bridge->rtcctl() & BackplaneBridge::RTC_BUSY);
    CHECK_EQ(r.settle(), uint64_t{BackplaneBridge::WRITE_BITS + 9 + BackplaneBridge::READ_BITS + 9} - 1);
    CHECK_EQ(r.rtc->read(Ds3231::AGING), 0x85);
    CHECK_EQ(r.bus().read(RTCDATA), 0x85); // read back
    // Queued: four alarm writes and a read behind them, BUSY until the last
    for (uint8_t i = 0; i < 4; ++i)
    {
        r.bus().write(RTCREG, static_cast<uint8_t>(Ds3231::ALARM1 + i));
        r.bus().write(RTCDATA, 0x80); // every second
    }
    r.settle();
    CHECK_EQ(r.rtc->read(Ds3231::ALARM1 + 3), 0x80);
    CHECK_EQ(r.bus().read(RTCDATA), 0x80);
}

TEST(an_alarm_sets_alarm_and_tick_until_its_flag_is_cleared)
{
    Rig r;
    r.rtc->write(Ds3231::ALARM1, 0x10); // :10, minutes, hours and date masked
    for (uint8_t i = 1; i < 4; ++i)
        r.rtc->write(static_cast<uint8_t>(Ds3231::ALARM1 + i), 0x80);
    r.next_tick(); // :08
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TICK);
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    r.next_tick(); // :09
    r.next_tick(); // :10
    CHECK_EQ(r.bus().read(RTCCTL), BackplaneBridge::RTC_TICK | BackplaneBridge::RTC_ALARM);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TICK);
    CHECK_EQ(r.bus().read(RTCCTL), BackplaneBridge::RTC_ALARM); // the flag is the DS3231's
    r.bus().write(RTCREG, Ds3231::STATUS);
    r.settle();
    CHECK_EQ(r.bus().read(RTCDATA), Ds3231::EN32KHZ | Ds3231::A1F);
    r.bus().write(RTCDATA, static_cast<uint8_t>(~Ds3231::A1F)); // A1F to 0; a 1 leaves the rest
    r.settle();
    CHECK_EQ(r.bus().read(RTCDATA), Ds3231::EN32KHZ);
    CHECK_EQ(r.bus().read(RTCCTL), 0x00); // a status write updates ALARM at once
}

TEST(with_intcn_only_an_enabled_alarm_refreshes_the_time)
{
    Rig r;
    r.rtc->write(Ds3231::ALARM1, 0x12); // :12
    for (uint8_t i = 1; i < 4; ++i)
        r.rtc->write(static_cast<uint8_t>(Ds3231::ALARM1 + i), 0x80);
    r.bus().write(RTCREG, Ds3231::CONTROL);
    r.bus().write(RTCDATA, Ds3231::INTCN | Ds3231::A1IE); // SQW becomes INT
    r.settle();
    r.idle(4 * HZ); // :11: no square wave, no reads
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    CHECK_EQ(r.bus().read(SEC), 0x07);
    r.idle(HZ);
    r.settle(); // :12: INT fell
    CHECK_EQ(r.bus().read(RTCCTL), BackplaneBridge::RTC_TICK | BackplaneBridge::RTC_ALARM);
    CHECK_EQ(r.bus().read(SEC), 0x12);
}

TEST(busy_takes_its_bits_at_the_cpu_clock)
{
    Rig r;
    r.bridge->set_cpu_hz(0); // 7.16 MHz: 132 bits at 400 kHz are 2,363 cycles
    r.bus().write(RTCREG, Ds3231::STATUS);
    CHECK_EQ(r.settle(), uint64_t{(39 * 7'159'090 + 399'999) / 400'000} - 1);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_SET);
    CHECK_EQ(r.settle(), uint64_t{(112 * 7'159'090 + 399'999) / 400'000} - 1);
}

TEST(apple_ii_mode_hides_the_registers_and_the_clock_runs_on)
{
    Rig r;
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    r.next_tick();
    REQUIRE(r.bus().irq());
    r.bus().write(NB + BackplaneBridge::MODE, BackplaneBridge::MODE_APPLE);
    REQUIRE(r.bus().take_reset());
    r.bus().pulse_reset(); // entering: /RESET clears TIE and TICK
    REQUIRE(r.bridge->apple());
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bridge->rtcctl(), 0x00);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE); // the IIe's $C687 now: slot 6's ROM
    CHECK_EQ(r.bridge->rtcctl(), 0x00);
    const unsigned was = r.rtc->time().second;
    // The CPU runs at the Apple clock, and the second is still a second.
    for (uint32_t i = 0; i < BackplaneBridge::APPLE_HZ; ++i)
        r.bus().idle();
    r.bus().sync();
    CHECK_EQ(r.rtc->time().second, was + 1);
    r.bus().reset(); // a hard reset: back to native, the clock as it was
    CHECK_EQ(r.bus().read(RTCCTL) & ~BackplaneBridge::RTC_BUSY, 0x00);
    CHECK_EQ(r.bus().read(SEC), r.bridge->rtc_time()[0]);
}

TEST(any_reset_clears_tick_and_tie_but_not_the_time)
{
    Rig r;
    r.bus().write(RTCREG, Ds3231::CONTROL);
    r.bus().write(RTCDATA, Ds3231::A2IE); // a control bit the power-on set-up would keep
    r.bus().write(RTCREG, Ds3231::AGING);
    r.settle();
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    r.next_tick();
    r.next_tick();
    REQUIRE(r.bus().irq());
    r.bus().pulse_reset(); // /RESET
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    CHECK_EQ(r.bus().read(RTCREG), 0x00);
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    r.next_tick();
    REQUIRE(r.bus().irq());
    r.bus().reset(); // the reset button: a hard reset, not a power-on
    CHECK(!r.bus().irq());
    CHECK_EQ(r.bus().read(RTCCTL), 0x00);
    CHECK_EQ(r.rtc->read(Ds3231::CONTROL), Ds3231::A2IE); // no power-on set-up again
    CHECK_EQ(r.bus().read(SEC), 0x10);                     // 13:45:10: the time ran on
    r.next_tick();
    CHECK_EQ(r.bus().read(SEC), 0x11);
}

TEST(the_debugger_shows_the_clock)
{
    Rig r;
    r.bus().write(RTCCTL, BackplaneBridge::RTC_TIE);
    Cpu6502 cpu(r.bus(), Cpu6502::Model::W65C02S);
    Symbols symbols;
    Debugger d(cpu, r.bus(), symbols);
    d.add_view("rtc", "   rtc   the clock\n", [&r] { return r.bridge->describe_clock(); });
    const std::string out = d.command("rtc");
    CHECK(out.find("time     2026-10-26 13:45:07  day 2\n") == 0);
    CHECK(out.find("RTCCTL   $40 TIE\n") != std::string::npos);
    CHECK(out.find("control  $00  status $08  aging $00  temperature 25.00 C") != std::string::npos);
    CHECK(d.command("h").find("   rtc   the clock\nq  quit") != std::string::npos);
    CHECK_EQ(d.command("rtc now"), std::string("bad arguments; 'h' for help\n"));
    r.rtc->write(Ds3231::HOURS, Ds3231::HOUR_12 | Ds3231::HOUR_PM | 0x01);
    r.next_tick();
    CHECK(r.bridge->describe_clock().find("time     2026-10-26 13:45:08  day 2  (12 h)") == 0);
}

namespace
{
    /// The whole 512 KB of ROM A with `code` at $C800 in its top block and the vectors set.
    std::string flash_image(const std::vector<uint8_t> &code, uint16_t irq)
    {
        std::vector<uint8_t> flash(0x80000, 0xFF);
        const size_t block31 = 0x7C000;
        for (size_t i = 0; i < code.size(); ++i)
            flash[block31 + 0x0800 + i] = code[i];
        flash[block31 + 0x3FFC] = 0x00; // reset: $C800
        flash[block31 + 0x3FFD] = 0xC8;
        flash[block31 + 0x3FFE] = static_cast<uint8_t>(irq);
        flash[block31 + 0x3FFF] = static_cast<uint8_t>(irq >> 8);
        const char *image = "rtc_flash.rom";
        std::FILE *f = std::fopen(image, "wb");
        if (f)
        {
            std::fwrite(flash.data(), 1, flash.size(), f);
            std::fclose(f);
        }
        return image;
    }
} // namespace

TEST(a_program_reads_the_time_and_takes_the_tick_interrupt)
{
    std::vector<uint8_t> code = {0x78}; // C800 SEI
    for (uint8_t i = 0; i < 7; ++i)      // LDA SEC+i; STA $0200+i: SEC first, latching
    {
        code.insert(code.end(), {0xAD, static_cast<uint8_t>(0x90 + i), 0xC6});
        code.insert(code.end(), {0x8D, i, 0x02});
    }
    code.insert(code.end(), {
                                0xA9, 0xC0, 0x8D, 0x87, 0xC6, // LDA #TICK|TIE; STA RTCCTL
                                0x58,                         // CLI
                                0xCB,                         // WAI
                                0x80, 0xFD,                   // BRA WAI
                            });
    code.resize(0x100, 0xEA);
    code.insert(code.end(), {
                                // C900: the IRQ
                                0xAD, 0x89, 0xC6, 0x8D, 0x10, 0x02, // LDA IRQH; STA $0210
                                0xAD, 0x87, 0xC6, 0x8D, 0x11, 0x02, // LDA RTCCTL; STA $0211
                                0xA9, 0xC0, 0x8D, 0x87, 0xC6,       // TICK cleared, TIE kept
                                0xAD, 0x87, 0xC6, 0x8D, 0x12, 0x02, // LDA RTCCTL; STA $0212
                                0xAD, 0x90, 0xC6, 0x8D, 0x08, 0x02, // LDA SEC; STA $0208
                                0xDB,                               // STP
                            });
    MachineOptions opt;
    opt.rom_override = flash_image(code, 0xC900);
    opt.rtc_time = START;
    std::string error;
    auto m = Machine::load(EMU_MACHINE_DIR "/backplane.machine", opt, error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    BackplaneBridge *bridge = m->backplane_bridge();
    REQUIRE(bridge != nullptr);
    constexpr uint32_t CPU_HZ = 100'000; // a slow CPU, so a second is quick to run
    bridge->set_cpu_hz(CPU_HZ);
    Decoder &bus = m->bus();
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    bus.reset();
    cpu.reset();
    uint64_t woke = 0;
    for (int i = 0; i < 2'000'000 && cpu.state() != Cpu6502::State::Stopped; ++i)
    {
        cpu.step();
        if (bus.irq())
        {
            if (!woke)
                woke = cpu.cycles();
            cpu.irq();
        }
    }
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);
    for (uint16_t i = 0; i < 7; ++i)
        CHECK_EQ(bus.peek(static_cast<uint16_t>(0x0200 + i)), START_REGS[i]);
    CHECK_EQ(bus.peek(0x0210), 0xDF); // IRQH: the north bridge, ID 0xD
    CHECK_EQ(bus.peek(0x0211), BackplaneBridge::RTC_TICK | BackplaneBridge::RTC_TIE);
    CHECK_EQ(bus.peek(0x0212), BackplaneBridge::RTC_TIE);
    CHECK_EQ(bus.peek(0x0208), 0x08);
    // The second ends at CPU_HZ; then 132 bits at 400 kHz, 33 cycles, before TICK.
    CHECK(woke > CPU_HZ + 32 && woke < CPU_HZ + 40);
    std::remove(opt.rom_override.c_str());
}

TEST(a_session_keeps_the_clock_through_a_power_cycle)
{
    const std::string image = flash_image({0xDB}, 0xC800); // STP
    std::string error;
    auto s = Session::create({EMU_MACHINE_DIR "/backplane.machine", image, ""}, error);
    if (!s)
        std::printf("  %s\n", error.c_str());
    REQUIRE(s != nullptr);
    REQUIRE(s->bridge() != nullptr);
    s->set_rtc_time(Ds3231::Time{2031, 3, 4, 5, 6, 7, 3});
    CHECK_EQ(s->bridge()->rtc_time()[6], 0x31); // SEC-YEAR read at once
    CHECK_EQ(s->peek_io(0x690), 0x07);
    const Ds3231 *chip = s->bridge()->rtc().get();
    REQUIRE(s->power_cycle(error));
    CHECK(s->bridge()->rtc().get() == chip); // the same chip, on its battery
    CHECK(s->bridge()->rtc()->time() == (Ds3231::Time{2031, 3, 4, 5, 6, 7, 3}));
    CHECK_EQ(s->bridge()->rtcctl(), 0x00);
    CHECK_EQ(s->peek_io(0x696), 0x31); // the new bridge read it at power-on
    // With EOSC set the oscillator stops on the battery: the next power-on sees LOST.
    s->bridge()->rtc()->write(Ds3231::CONTROL, Ds3231::EOSC);
    REQUIRE(s->power_cycle(error));
    CHECK_EQ(s->bridge()->rtcctl(), BackplaneBridge::RTC_LOST);
    std::remove(image.c_str());
}

int main()
{
    return run_all();
}
