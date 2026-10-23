/**
 * @file test_session.cpp
 * @brief Session (session.h): the WebUI's core, driven as the browser will.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane machine with the BIOS, run in budgets of one 60 Hz frame
 * at 7.16 MHz, talked to only through Session: serial in and out, the
 * panel's switches and keys, the card's SPI registers, the lamps and the
 * trace ring. Without cc65 the BIOS is not built and the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "session.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60
    const std::string GREETING = "\rBACKPLANE BIOS 0.1\r\\\r";
    const std::string MACHINE = EMU_MACHINE_DIR "/backplane.machine";

    std::unique_ptr<Session> open(const std::string &machine = MACHINE)
    {
        std::string error;
        auto s = Session::create({machine, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!s)
            std::printf("  %s\n", error.c_str());
        return s;
    }

    /// Frames until the output (accumulated in `out`) contains `want`.
    bool run_until(Session &s, std::string &out, const std::string &want, int frames = 60)
    {
        for (int i = 0; i < frames; ++i)
        {
            s.run(FRAME);
            out += s.take_output();
            if (out.find(want) != std::string::npos)
                return true;
        }
        std::printf("  never saw '%s' in '%s'\n", want.c_str(), out.c_str());
        return false;
    }

    /// Booted to the monitor's prompt, output drained.
    std::unique_ptr<Session> booted()
    {
        auto s = open();
        std::string out;
        if (s && !run_until(*s, out, GREETING))
            return nullptr;
        return s;
    }

    std::string write_machine(const std::string &name, const std::string &text)
    {
        std::ofstream(name) << text;
        return name;
    }
} // namespace

TEST(boots_the_bios_in_frame_budgets)
{
    auto s = open();
    REQUIRE(s != nullptr);
    CHECK(s->has_card());
    std::string out;
    int frames = 0;
    for (; frames < 30 && out.find(GREETING) == std::string::npos; ++frames)
    {
        const Session::RunResult r = s->run(FRAME);
        CHECK(r.why == Session::Stop::Budget);
        CHECK(r.cycles >= FRAME && r.cycles < FRAME + 16); // at most one instruction over
        out += s->take_output();
    }
    CHECK_EQ(out, GREETING);
    CHECK(frames < 15);                                     // ~1 M cycles: well under a quarter second
    CHECK_EQ(s->take_output(), std::string());              // taken means gone
}

TEST(serial_round_trip_through_the_monitor)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    s->send("C680.C684\r");
    std::string out;
    CHECK(run_until(*s, out, "C680: 00 01 02 7F 7F"));
}

TEST(halt_switch_cont_and_run_through_the_panel)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    s->set_panel(true, false);
    Session::RunResult r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK_EQ(r.cycles, 0u);
    CHECK(s->halted());
    CHECK_EQ(s->card_read(ConsoleCard::STATUS) & ConsoleCard::HALTED, ConsoleCard::HALTED);

    const uint64_t before = s->cycles();
    s->press(ConsoleCard::CONT);                            // S INST: one instruction
    r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK(r.cycles >= 2 && r.cycles <= 7);
    CHECK_EQ(s->cycles(), before + r.cycles);

    s->set_panel(false, false);                             // ENABLE, then CONT: runs on
    s->press(ConsoleCard::CONT);
    CHECK(s->run(FRAME).why == Session::Stop::Budget);
    CHECK(!s->halted());
}

TEST(a_breakpoint_on_a_label_through_the_spi_registers)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    uint16_t echo = 0;
    REQUIRE(s->resolve("ECHO", echo));
    uint16_t hex = 0;
    CHECK(s->resolve("$ECHO", hex) == false);               // `$` means hex, and ECHO is not
    CHECK(s->resolve("$C8AB", hex) && hex == 0xC8AB);
    CHECK(!s->resolve("NOWHERE", hex));

    const uint32_t phys = s->physical(echo);
    CHECK_EQ(phys >> 12, 0xFCu);                            // W3: RAM block 63
    for (uint8_t i = 0; i < 3; ++i)
        s->card_write(static_cast<uint8_t>(ConsoleCard::BPADDR + i), static_cast<uint8_t>(phys >> (8 * i)));
    s->card_write(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    CHECK(s->run(FRAME).why == Session::Stop::Budget);      // nothing typed: no echo

    s->send("A");
    Session::RunResult r;
    for (int i = 0; i < 10 && r.why != Session::Stop::Halted; ++i)
        r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK_EQ(s->regs().pc, echo);
    CHECK_EQ(s->card_read(ConsoleCard::STATUS), ConsoleCard::HALTED | ConsoleCard::HIT);
    CHECK_EQ(s->regs().a, 'A');                             // the key it is about to echo
}

TEST(output_reaches_the_screen_while_halted)
{
    // Halt just after ECHO stores to the 6551: the character must still
    // arrive, though the CPU will not run again until CONT.
    auto s = booted();
    REQUIRE(s != nullptr);
    uint16_t txdelay = 0;
    REQUIRE(s->resolve("TXDELAY", txdelay));
    const uint32_t phys = s->physical(txdelay);
    for (uint8_t i = 0; i < 3; ++i)
        s->card_write(static_cast<uint8_t>(ConsoleCard::BPADDR + i), static_cast<uint8_t>(phys >> (8 * i)));
    s->card_write(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    s->send("Z");
    std::string out;
    for (int i = 0; i < 10 && !s->halted(); ++i)
    {
        s->run(FRAME);
        out += s->take_output();
    }
    REQUIRE(s->halted());
    s->run(FRAME);                                          // halted: no cycles, but the screen catches up
    out += s->take_output();
    CHECK_EQ(out, std::string("Z"));
}

TEST(the_card_interrupt_ends_a_wai)
{
    // The BIOS runs with I set, so an IRQ only ends WAI; then STP.
    auto s = booted();
    REQUIRE(s != nullptr);
    s->card_write(ConsoleCard::IEN, 0x01);
    s->send("300: CB DB\r300R\r");                          // WAI; STP
    for (int i = 0; i < 5; ++i)
        CHECK(s->run(FRAME).why == Session::Stop::Budget); // waiting
    CHECK(!s->irq());
    s->press(ConsoleCard::LOAD_ADRS);                       // a key press: ISR, IRQH bit 6
    CHECK(s->irq());                                        // the IRQ lamp
    CHECK(s->run(FRAME).why == Session::Stop::Stopped);
    CHECK(s->irq());                                        // nothing acknowledged it
}

TEST(lamps_glow_while_running_and_freeze_when_halted)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    CHECK_EQ(s->lights().addr[19], 0.0f);                   // off by default
    s->set_lights(true);
    s->run(FRAME);
    ConsoleCard::Lights l = s->lights();
    // The monitor polls the 6551 ($0C301: A16-A19 low) from code at $FC8xx (high).
    CHECK(l.addr[19] > 0.5f && l.addr[19] < 1.0f);
    CHECK_EQ(l.addr[16], l.addr[19]);
    CHECK(l.sync > 0.1f && l.sync < 0.5f);

    s->set_panel(true, false);
    s->run(FRAME);
    l = s->lights();
    for (float a : l.addr)
        CHECK(a == 0.0f || a == 1.0f);                      // one cycle, latched
}

TEST(trace_ring_keeps_the_newest_cycles)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::vector<Session::TraceRow> rows(100);
    CHECK_EQ(s->trace(rows.data(), rows.size()), 0u);       // off: nothing
    s->set_trace(true, 64);
    s->run(1000);
    CHECK_EQ(s->trace(rows.data(), rows.size()), 64u);
    CHECK(rows[63].cycle <= s->cycles());
    CHECK(rows[63].cycle > s->cycles() - 16);               // the newest is the last
    unsigned fetches = 0;
    for (size_t i = 0; i < 64; ++i)
    {
        if (i > 0)
            CHECK(rows[i].cycle > rows[i - 1].cycle);       // oldest first
        if (rows[i].flags & Session::TRACE_SYNC)
        {
            ++fetches;
            CHECK_EQ(rows[i].physical, 0xF0000u | rows[i].cpu); // W3 = block 63: $FC000 + (cpu - $C000)
            CHECK_EQ(rows[i].module, 3);                         // RAM D
        }
    }
    CHECK(fetches > 5);
    CHECK_EQ(s->trace(rows.data(), 3), 3u);                 // fewer than kept: the newest three
    CHECK(rows[2].cycle > s->cycles() - 16);
}

TEST(start_and_power_cycle)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::string out;
    s->send("300: 42\r");
    REQUIRE(run_until(*s, out, "00: "));                    // the monitor answers with the old byte
    s->run(FRAME);
    CHECK_EQ(s->peek(0x0300), 0x42);
    s->set_switches(0x12345);

    s->start();                                             // the hard reset: RAM stays
    out.clear();
    CHECK(run_until(*s, out, GREETING));
    CHECK_EQ(s->peek(0x0300), 0x42);

    std::string error;
    CHECK(s->power_cycle(error));                           // RAM forgets; switches do not
    CHECK_EQ(s->peek(0x0300), 0x00);
    CHECK_EQ(s->card_read(ConsoleCard::SWR + 2), 0x01);
    out.clear();
    CHECK(run_until(*s, out, GREETING));
}

TEST(stp_ends_the_run_and_start_gets_out)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::string out;
    s->send("300: DB\r300R\r");                             // STP, and run it
    Session::RunResult r;
    for (int i = 0; i < 20 && r.why != Session::Stop::Stopped; ++i)
        r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Stopped);
    CHECK(s->stopped());
    CHECK_EQ(s->run(FRAME).cycles, 0u);                     // and stays stopped
    s->start();
    out.clear();
    CHECK(run_until(*s, out, GREETING));
    CHECK(!s->stopped());
}

TEST(a_machine_without_a_panel)
{
    std::string text;
    {
        std::ifstream in(MACHINE);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const size_t at = text.find("\nconsole ");
    REQUIRE(at != std::string::npos);
    text.erase(at + 1, text.find('\n', at + 1) - at);       // drop the console line
    auto s = open(write_machine("no_panel.machine", text));
    REQUIRE(s != nullptr);
    CHECK(!s->has_card());
    CHECK_EQ(s->card_read(ConsoleCard::ID_REG), 0xFF);
    s->set_panel(true, false);                              // nothing to halt
    CHECK(s->run(FRAME).why == Session::Stop::Budget);
    std::string out = s->take_output();
    CHECK(run_until(*s, out, GREETING));
    CHECK(s->lights().addr[19] == 0.0f);
}

TEST(errors_name_what_failed)
{
    std::string error;
    CHECK(Session::create({"no_such.machine", "", ""}, error) == nullptr);
    CHECK(error.find("no_such.machine") != std::string::npos);
    error.clear();
    CHECK(Session::create({MACHINE, EMU_BIOS_ROM, "no_such.lbl"}, error) == nullptr);
    CHECK(error.find("no_such.lbl") != std::string::npos);
    error.clear();
    CHECK(Session::create({MACHINE, "no_such.rom", ""}, error) == nullptr);
    CHECK(error.find("no_such.rom") != std::string::npos);
}

int main()
{
    if (!std::ifstream(EMU_BIOS_ROM))
    {
        std::printf("SKIP: %s not built (needs ca65/ld65)\n", EMU_BIOS_ROM);
        return SKIP;
    }
    return run_all();
}
