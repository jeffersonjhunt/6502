/**
 * @file test_debugger.cpp
 * @brief The debugger's commands and the bus trace's lines.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A small decoded machine: RAM at $0000 (module 0), a 6821 at $D010
 * (module 6) and ROM at $E000 (module 4), with a program poked into ROM.
 * The trace lines are checked character for character against
 * docs/BUS_TRACE.md, including the reset sequence's dummy stack reads and
 * vector pulls, since a board capture will be diffed against exactly these.
 */

#include "harness.h"

#include "bus_trace.h"
#include "debugger.h"
#include "memory_devices.h"
#include "pia6821.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct Rig
    {
        explicit Rig(NorthBridge *bridge = nullptr) : bus(bridge)
        {
            bus.map({0x0000, 0x7FFF, &ram, 0x0});
            bus.map({0xE000, 0xFFFF, &rom, 0x4});
            bus.map({0xD010, 0xD013, &pia, 0x6});
            load(0xFFFC, {0x00, 0xE0}); // reset -> $E000
        }

        void load(uint16_t addr, std::initializer_list<uint8_t> bytes)
        {
            for (const uint8_t b : bytes)
                bus.poke(addr++, b);
        }

        Ram ram{0x8000};
        Rom rom{0x2000};
        Pia6821 pia;
        Decoder bus;
        Symbols symbols;
        Cpu6502 cpu{bus, Cpu6502::Model::W65C02S};
    };

    // $E000: LDA #$41; STA $0200; INC A; BRA $E003 (loops STA/INC)
    constexpr std::initializer_list<uint8_t> PROGRAM = {0xA9, 0x41, 0x8D, 0x00, 0x02, 0x1A, 0x80, 0xFA};

    bool contains(const std::string &haystack, const std::string &needle)
    {
        if (haystack.find(needle) != std::string::npos)
            return true;
        std::printf("  missing '%s' in:\n%s", needle.c_str(), haystack.c_str());
        return false;
    }

    std::vector<std::string> read_lines(std::FILE *f)
    {
        std::rewind(f);
        std::vector<std::string> lines;
        char buf[256];
        while (std::fgets(buf, sizeof buf, f))
        {
            std::string s = buf;
            s.pop_back(); // '\n'
            lines.push_back(s);
        }
        return lines;
    }
} // namespace

TEST(status_shows_registers_flags_and_next_instruction)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    const std::string s = d.status();
    CHECK(contains(s, "PC=E000 A=00 X=00 Y=00 S=FD P=24 nv-bdIzc cycles=7\n"));
    CHECK(contains(s, "E000  A9 41     lda #$41\n"));
}

TEST(step_runs_instructions_and_shows_each)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    const std::string out = d.command("s 3");
    CHECK(contains(out, "E000  A9 41     lda #$41\nE002  8D 00 02  sta $0200\nE005  1A        inc a\n"));
    CHECK(contains(out, "PC=E006 A=42"));
    CHECK_EQ(m.ram.peek(0x0200), 0x41);
    const std::string one = d.command("step");
    CHECK(contains(one, "E006  80 FA     bra $E002\nPC=E002 A=42"));
}

TEST(breakpoints_halt_the_run_loop_and_resume_past_themselves)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    CHECK(contains(d.command("b E005"), "break at E005"));
    CHECK(d.any_breakpoints());

    // What the run loop does: check() before each instruction.
    int steps = 0;
    while (!d.check(m.cpu.regs().pc) && steps < 100)
    {
        m.cpu.step();
        ++steps;
    }
    CHECK(d.halted());
    CHECK_EQ(m.cpu.regs().pc, 0xE005);
    CHECK_EQ(steps, 2);

    d.command("r");
    CHECK(!d.halted());
    CHECK(!d.check(m.cpu.regs().pc)); // not straight back into the same breakpoint
    m.cpu.step();
    m.cpu.step(); // BRA back to $E002
    CHECK(!d.check(m.cpu.regs().pc));
    m.cpu.step();
    CHECK(d.check(m.cpu.regs().pc)); // round the loop to it again

    // step N stops at a breakpoint too, but not on the one it starts at.
    CHECK(contains(d.command("s 100"), "break at E005 after 3 instruction(s)"));

    CHECK(contains(d.command("b"), "E005\n"));
    CHECK(contains(d.command("d E005"), "deleted E005"));
    CHECK(contains(d.command("d E005"), "no breakpoint at E005"));
    CHECK(!d.any_breakpoints());
    d.command("b E000");
    d.command("b E002");
    CHECK(contains(d.command("d all"), "no breakpoints"));
    CHECK(!d.any_breakpoints());
}

TEST(a_breakpoint_after_wai_fires_once_when_it_ends)
{
    Rig m;
    m.load(0xE000, {0xCB, 0xEA, 0xEA}); // WAI; NOP; NOP
    m.cpu.reset();                      // I set: an IRQ ends the WAI and runs on
    Debugger d(m.cpu, m.bus, m.symbols);
    d.command("b E001");
    CHECK(!d.check(m.cpu.regs().pc));
    m.cpu.step(); // WAI
    for (int i = 0; i < 10; ++i)
    {
        CHECK(!d.check(m.cpu.regs().pc)); // idling, not about to run E001
        m.cpu.step();
    }
    CHECK(contains(d.command("s 5"), "(waiting: WAI)")); // nor does a step stop there
    CHECK(!d.halted());
    m.cpu.irq();
    CHECK(d.check(m.cpu.regs().pc));
    CHECK_EQ(m.cpu.regs().pc, 0xE001);
}

TEST(run_skips_only_the_breakpoint_it_halted_at)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    d.command("b E005");
    d.halt("key"); // at E000, not a breakpoint
    d.command("x pc=E005");
    d.command("r");
    CHECK(d.check(m.cpu.regs().pc)); // moved onto a breakpoint: it is the first one hit

    d.command("s"); // E005 -> E006, then back round to E005 by stepping
    d.command("s");
    d.command("s");
    CHECK_EQ(m.cpu.regs().pc, 0xE005);
    d.command("r");
    CHECK(!d.check(m.cpu.regs().pc)); // stepped onto it: run goes past, as from a halt there
}

TEST(steps_do_the_run_loops_work_and_can_be_interrupted)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    unsigned after = 0;
    d.on_step([&] { ++after; });
    d.command("s 3");
    CHECK_EQ(after, 3u);

    bool stop = false;
    unsigned polls = 0;
    d.on_interrupt([&] { ++polls; return stop; });
    d.command("s 10000");
    CHECK_EQ(after, 10003u);
    CHECK(polls > 0);
    stop = true;
    CHECK(contains(d.command("s 1000000"), "interrupted after 4096 instruction(s)"));
    CHECK_EQ(after, 10003u + 4096u);
}

TEST(long_labels_are_not_cut_short)
{
    Rig m;
    m.load(0xE000, {0xEA});
    const std::string name(200, 'L');
    m.symbols.add(name, 0xE000);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    CHECK(contains(d.command("l E000 1"), "E000  EA        " + name + ": nop\n"));
}

TEST(fetch_has_no_side_effects_and_store_obeys_the_map)
{
    Rig m;
    Debugger d(m.cpu, m.bus, m.symbols);
    m.bus.write(0xD011, Pia6821::CR_SELECT_OR);
    m.pia.set_input_a(0xC1); // 'A' with bit 7, as the Apple-1 keyboard sends it
    m.pia.strobe_ca1();
    CHECK(contains(d.command("f D010 4"), "D010  C1 84 00 00"));
    CHECK(contains(d.command("f D010 4"), "A..."));     // bit 7 stripped for the text column
    CHECK(m.pia.cra() & Pia6821::CR_IRQ1);              // key still waiting

    CHECK(contains(d.command("st 0300 DE AD"), "0300  DE AD"));
    CHECK_EQ(m.ram.peek(0x0301), 0xAD);
    d.command("st E000 55"); // ROM: a CPU store is ignored
    CHECK_EQ(m.rom.peek(0x0000), 0x00);

    const std::string rows = d.command("fetch 0000 20");
    CHECK(contains(rows, "0000  00 00"));
    CHECK(contains(rows, "\n0010  00 00 00 00  ")); // 20 is decimal: one full row and four
}

TEST(registers_can_be_set)
{
    Rig m;
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    CHECK(contains(d.command("x a=12 X=34 pc=0300 p=FF"), "PC=0300 A=12 X=34"));
    CHECK_EQ(m.cpu.regs().p, 0xEF); // B is never live
    CHECK(contains(d.command("regs a=123"), "bad register setting 'a=123'"));
    CHECK(contains(d.command("regs q=1"), "bad register setting 'q=1'"));
    CHECK_EQ(m.cpu.regs().a, 0x12); // a bad setting changes nothing
}

TEST(labels_in_commands_and_listings)
{
    Rig m;
    m.load(0xE000, {0x20, 0x10, 0xE0}); // JSR ECHO
    m.load(0xE010, {0x60});
    m.symbols.add("RESET", 0xE000);
    m.symbols.add("ECHO", 0xE010);
    m.symbols.add("ADD", 0x0300);
    m.cpu.reset();
    Debugger d(m.cpu, m.bus, m.symbols);
    CHECK(contains(d.status(), "E000  20 10 E0  RESET: jsr ECHO\n"));
    CHECK(contains(d.command("b ECHO"), "break at E010"));
    CHECK(contains(d.command("b"), "E010  ECHO"));
    CHECK(contains(d.command("l ECHO 1"), "E010  60        ECHO: rts"));
    uint16_t a = 0;
    CHECK(d.parse_address("ADD", a) && a == 0x0300);  // a label wins...
    CHECK(d.parse_address("$ADD", a) && a == 0x0ADD); // ...unless $ forces hex
    CHECK(!d.parse_address("NOPE", a));
    CHECK(!d.parse_address("12345", a));
}

TEST(errors_and_quit)
{
    Rig m;
    Debugger d(m.cpu, m.bus, m.symbols);
    CHECK(contains(d.command("frob"), "unknown command 'frob'"));
    CHECK(contains(d.command("s 0"), "bad arguments"));
    CHECK(contains(d.command("s x"), "bad arguments"));
    CHECK(contains(d.command("f"), "bad arguments"));
    CHECK(contains(d.command("st 0300 1FF"), "bad byte '1FF'"));
    CHECK(contains(d.command("t on"), "no bus trace"));
    CHECK(contains(d.command("h"), "s  step [N]"));
    CHECK_EQ(d.command("   "), std::string());
    CHECK(!d.quit());
    d.command("q");
    CHECK(d.quit());
}

TEST(bus_trace_lines)
{
    Rig m;
    m.load(0xE000, PROGRAM);
    std::FILE *f = std::tmpfile();
    BusTrace trace(f, m.bus, "cpu w65c02s\nmachine test");
    m.cpu.set_observer(&trace);
    m.cpu.reset();
    m.cpu.step(); // LDA #$41
    m.cpu.step(); // STA $0200
    Debugger d(m.cpu, m.bus, m.symbols, &trace);
    CHECK(contains(d.command("t off"), "trace off, 13 cycles written"));
    m.cpu.step(); // INC A, not traced
    CHECK(contains(d.command("t on"), "trace on"));
    m.cpu.step(); // BRA $E002
    std::fflush(f);

    const std::vector<std::string> want = {
        "# ooe-bus-trace 1",
        "# cpu w65c02s",
        "# machine test",
        "1 00000 00 R -- 0", // reset: two reads at PC (0 at power-up)...
        "2 00000 00 R -- 0",
        "3 00100 00 R -- 0", // ...three stack reads in place of pushes...
        "4 001FF 00 R -- 0",
        "5 001FE 00 R -- 0",
        "6 0FFFC 00 R -V 4", // ...and the vector pull
        "7 0FFFD E0 R -V 4",
        "8 0E000 A9 R S- 4", // LDA #$41
        "9 0E001 41 R -- 4",
        "10 0E002 8D R S- 4", // STA $0200
        "11 0E003 00 R -- 4",
        "12 0E004 02 R -- 4",
        "13 00200 41 W -- 0",
        "16 0E006 80 R S- 4", // INC A (cycles 14-15) not traced; BRA $E002
        "17 0E007 FA R -- 4",
        "18 0E008 00 R -- 4", // taken: a dummy read of the next opcode address
    };
    const std::vector<std::string> got = read_lines(f);
    CHECK_EQ(got.size(), want.size());
    for (size_t i = 0; i < want.size() && i < got.size(); ++i)
    {
        if (got[i] != want[i])
            std::printf("  line %zu: got '%s' want '%s'\n", i + 1, got[i].c_str(), want[i].c_str());
        CHECK_EQ(got[i], want[i]);
    }
    std::fclose(f);
}

TEST(bus_trace_uses_the_north_bridge_address)
{
    struct Bank3 : NorthBridge
    {
        Route route(uint16_t a, bool, bool) const override { return Route{0x30000u | a, true}; }
    };
    Bank3 bridge;
    Ram ram(0x10000);
    Decoder bus(&bridge);
    bus.map({0x30000, 0x3FFFF, &ram, 0x2});
    bus.poke(0xFFFC, 0x00);
    bus.poke(0xFFFD, 0x02);
    bus.poke(0x0200, 0xEA); // NOP: two cycles
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    std::FILE *f = std::tmpfile();
    BusTrace trace(f, bus, "");
    cpu.set_observer(&trace);
    cpu.reset();
    cpu.step();
    std::fflush(f);
    const std::vector<std::string> got = read_lines(f);
    CHECK_EQ(got.size(), 1u + 7u + 2u);
    CHECK_EQ(got[6], std::string("6 3FFFC 00 R -V 2"));
    CHECK_EQ(got[8], std::string("8 30200 EA R S- 2"));
    std::fclose(f);
}

int main()
{
    return run_all();
}
