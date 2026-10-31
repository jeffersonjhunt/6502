/**
 * @file test_joyports.cpp
 * @brief The serial card's W65C22S VIA and joystick ports on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md, "The VIA and the joystick ports", is the spec: the VIA at
 * $C340-$C34F in module 6's block, its IRQ on IRQL bit 6, joystick 1 on
 * PA0-PA4 and joystick 2 on PB0-PB4, 0 while pressed, the rest pulled up.
 * The stand-in 6551 keeps the rest of the block ($C300-$C303 and its
 * mirrors), which the BIOS and OODOS use. Then 6502 code on the real
 * machine file reads both sticks and takes T1's interrupts at the 1 kHz
 * tick's rate through the north bridge's IRQL; the session holds the
 * sticks through a power cycle; and the terminal debugger's `joy` sets them.
 */

#include "harness.h"

#include "acia6551.h"
#include "backplane_bridge.h"
#include "cpu6502.h"
#include "debugger.h"
#include "joystick_ports.h"
#include "machine.h"
#include "session.h"
#include "symbols.h"
#include "via6522.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t VIA = 0xC340;
    constexpr uint16_t ORB = VIA + 0x0, ORA = VIA + 0x1, DDRB = VIA + 0x2, DDRA = VIA + 0x3, T1CL = VIA + 0x4, IER = VIA + 0xE;
    constexpr uint16_t IRQL = 0xC680 + BackplaneBridge::IRQL;
    using J = JoystickPorts;

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
        const char *image = "joyports_flash.rom";
        std::FILE *f = std::fopen(image, "wb");
        if (f)
        {
            std::fwrite(flash.data(), 1, flash.size(), f);
            std::fclose(f);
        }
        return image;
    }

    /// backplane.machine, its flash `opt`'s or one that stops at once.
    std::unique_ptr<Machine> backplane(MachineOptions opt = {})
    {
        if (opt.rom_override.empty())
            opt.rom_override = flash_image({0xDB}, 0xC800); // STP
        std::string error;
        auto m = Machine::load(EMU_MACHINE_DIR "/backplane.machine", opt, error);
        if (!m)
            std::printf("  %s\n", error.c_str());
        return m;
    }
} // namespace

TEST(the_via_answers_c340_and_the_6551_keeps_the_rest_of_the_block)
{
    auto m = backplane();
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    bus.reset();
    auto *via = m->find_as<Via6522>("via");
    auto *acia = m->find_as<Acia6551>("terminal");
    REQUIRE(via != nullptr);
    REQUIRE(acia != nullptr);
    // The 6551 where the BIOS and OODOS have it, and in its mirrors either side of the VIA.
    bus.write(0xC302, 0x0B); // command: DTR, no receive IRQ, transmitter on
    CHECK_EQ(acia->command(), 0x0B);
    CHECK_EQ(bus.peek(0xC322), 0x0B);
    CHECK_EQ(bus.peek(0xC33E), 0x0B);
    CHECK_EQ(bus.peek(0xC352), 0x0B);
    CHECK_EQ(bus.peek(0xC37E), 0x0B);
    bus.write(0xC303, 0x1F);
    CHECK_EQ(acia->control(), 0x1F);
    // The VIA in $C340-$C34F: 16 registers, WDC's order.
    bus.write(DDRB, 0x5A);
    CHECK_EQ(via->ddr(Via6522::Port::B), 0x5A);
    CHECK_EQ(acia->command(), 0x0B); // $C342 is DDRB, not the 6551's command
    bus.write(0xC34E, 0x80 | Via6522::IRQ_T2);
    CHECK_EQ(bus.peek(IER), 0x80 | Via6522::IRQ_T2);
    CHECK_EQ(acia->command(), 0x0B); // $C34E is IER, not the 6551's command mirror
    CHECK_EQ(acia->control(), 0x1F);
    // Module 6, wired: the VIA is in the I/O hole's block for ID 6.
    const Mapping *map = nullptr; // the later mapping answers, as the decoder looks
    for (const Mapping &each : bus.mappings())
        if (bus_address(Space::Io, 0x340) >= each.first && bus_address(Space::Io, 0x34F) <= each.last)
            map = &each;
    REQUIRE(map != nullptr);
    CHECK(map->device == via);
    CHECK_EQ(map->module, 6);
    CHECK(map->irq_wired);
    CHECK_EQ(map->wait, 0); // the W65C22S needs no wait state at 7.16 MHz
}

TEST(sticks_read_0_when_pressed_and_everything_else_is_pulled_up)
{
    auto m = backplane();
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    bus.reset();
    JoystickPorts *joy = m->joysticks();
    REQUIRE(joy != nullptr);
    CHECK(m->find_peripheral<JoystickPorts>("joysticks") == joy);
    CHECK_EQ(bus.read(ORA), 0xFF); // nothing pressed, nothing on the user port
    CHECK_EQ(bus.read(ORB), 0xFF);
    joy->set(0, J::UP | J::FIRE);
    CHECK_EQ(bus.read(ORA), 0xEE);
    CHECK_EQ(bus.read(VIA + 0xF), 0xEE); // ORA without handshake reads the same pins
    CHECK_EQ(bus.read(ORB), 0xFF);       // joystick 2 is the other port
    joy->set(1, J::LEFT | J::DOWN);
    CHECK_EQ(bus.read(ORB), 0xF9);
    joy->set(1, J::RIGHT);
    CHECK_EQ(bus.read(ORB), 0xF7);
    joy->set(0, 0xFF); // only the five switches exist
    CHECK_EQ(joy->pressed(0), J::ALL);
    CHECK_EQ(bus.read(ORA), 0xE0);
    joy->set(0, 0);
    CHECK_EQ(bus.read(ORA) & 0x1F, 0x1F);
    // A user port line made an output reads the pin, low as driven; the stick's lines stay inputs.
    bus.write(DDRA, 0x80);
    bus.write(ORA, 0x00);
    joy->set(0, J::DOWN);
    CHECK_EQ(bus.read(ORA), 0x7D);
    // A stick line driven high reads 0 on port A while its switch is pressed (the pin)...
    bus.write(DDRA, 0x01);
    bus.write(ORA, 0x01);
    joy->set(0, J::UP);
    CHECK_EQ(bus.read(ORA), 0xFE);
    // ...and 1 on port B, which reads ORB for an output.
    bus.write(DDRB, 0x01);
    bus.write(ORB, 0x01);
    joy->set(1, J::UP);
    CHECK_EQ(bus.read(ORB), 0xFF);
    bus.write(DDRB, 0x00);
    CHECK_EQ(bus.read(ORB), 0xFE);
    joy->set(0, J::DOWN);
    joy->set(1, J::RIGHT);
    // /RESET: every line an input again, and the pull-ups still there.
    bus.reset();
    CHECK_EQ(bus.peek(DDRA), 0x00);
    CHECK_EQ(bus.read(ORA), 0xFD);
    CHECK_EQ(bus.read(ORB), 0xF7);
}

TEST(the_vias_interrupt_is_irql_bit_6)
{
    auto m = backplane();
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    bus.reset();
    CHECK_EQ(bus.read(IRQL), 0xFF);
    CHECK(!bus.irq());
    bus.write(IER, 0x80 | Via6522::IRQ_T1);
    bus.write(T1CL, 10);
    bus.write(T1CL + 1, 0); // one-shot, 10
    for (int i = 0; i < 12; ++i)
        bus.idle();
    CHECK_EQ(bus.read(IRQL), 0xBF); // bit 6 low: module 6
    CHECK(bus.irq());
    bus.read(T1CL); // the flag clears
    CHECK_EQ(bus.read(IRQL), 0xFF);
    CHECK(!bus.irq());
}

TEST(machine_file_errors_name_the_joyports_line)
{
    std::string error;
    MachineOptions opt;
    CHECK(!Machine::build("ram 0000-FFFF\njoyports via=nope\n", "j.machine", opt, error));
    CHECK(error.find("j.machine:2") == 0);
    CHECK(error.find("via=NAME") != std::string::npos);
    CHECK(!Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\nvia6522 8010-801F name=w\njoyports via=v\n"
                          "joyports via=w\n",
                          "j.machine", opt, error));
    CHECK(error.find("j.machine:5: joyports: only one per machine") == 0);
    // Two on one VIA: still "only one", not "a VIA of its own".
    CHECK(!Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\njoyports via=v\njoyports via=v\n", "j.machine",
                          opt, error));
    CHECK(error.find("j.machine:4: joyports: only one per machine") == 0);
    CHECK(!Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\njoyports via=v port=A\n", "j.machine", opt, error));
    CHECK(error.find("unknown option 'port'") != std::string::npos);
    // The joysticks take both ports: no LCD or keyboard on the same VIA, before or after.
    CHECK(!Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\nhd44780 via=v data=PB0-3 rs=PB4 rw=PB5 e=PB6\n"
                          "joyports via=v\n",
                          "j.machine", opt, error));
    CHECK(error.find("j.machine:4: joyports: via 'v' already has hd44780 on it") == 0);
    CHECK(!Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\njoyports via=v\nps2kbd via=v data=PA0-7\n",
                          "j.machine", opt, error));
    CHECK(error.find("j.machine:4: ps2kbd: via 'v' already has joyports on it") == 0);
    // Another VIA is fine.
    CHECK(Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\nvia6522 8010-801F name=w\njoyports via=v\n"
                         "ps2kbd via=w data=PA0-7\n",
                         "j.machine", opt, error) != nullptr);
    auto m = Machine::build("ram 0000-7FFF\nvia6522 8000-800F name=v\njoyports via=v name=sticks\n", "j.machine", opt,
                            error);
    REQUIRE(m != nullptr);
    CHECK(m->find_peripheral<JoystickPorts>("sticks") == m->joysticks());
}

TEST(a_program_reads_the_sticks_and_takes_t1_at_the_tick_rate_through_irql)
{
    // T1 free-running with the 1 kHz latch, 7,157: an interrupt every 7,159 cycles.
    // The handler reads IRQL, both sticks and T1C-L (clearing the flag), and stops after 5.
    const std::vector<uint8_t> main = {
        0x78,                         // C800 SEI
        0xA9, 0x40, 0x8D, 0x4B, 0xC3, // LDA #$40; STA ACR: T1 free-running
        0xA9, 0xC0, 0x8D, 0x4E, 0xC3, // LDA #$C0; STA IER: T1 on
        0xA9, 0xF5, 0x8D, 0x44, 0xC3, // LDA #<7157; STA T1C-L
        0xA9, 0x1B, 0x8D, 0x45, 0xC3, // LDA #>7157; STA T1C-H: starts it
        0x58,                         // CLI
        0xCB,                         // WAI
        0x80, 0xFD,                   // BRA WAI
    };
    std::vector<uint8_t> code = main;
    code.resize(0x100, 0xEA);
    code.insert(code.end(), {
                                // C900: the IRQ
                                0xAE, 0x20, 0x02,       // LDX $0220: interrupts so far
                                0xAD, 0x88, 0xC6,       // LDA IRQL
                                0x9D, 0x00, 0x02,       // STA $0200,X
                                0xAD, 0x41, 0xC3,       // LDA ORA: joystick 1
                                0x9D, 0x08, 0x02,       // STA $0208,X
                                0xAD, 0x40, 0xC3,       // LDA ORB: joystick 2
                                0x9D, 0x10, 0x02,       // STA $0210,X
                                0xAD, 0x44, 0xC3,       // LDA T1C-L: clears T1
                                0xEE, 0x20, 0x02,       // INC $0220
                                0xE0, 0x04,             // CPX #4
                                0xD0, 0x01,             // BNE +1
                                0xDB,                   // STP: the fifth
                                0x40,                   // RTI
                            });
    MachineOptions opt;
    opt.rom_override = flash_image(code, 0xC900);
    auto m = backplane(opt);
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    bus.reset();
    cpu.reset();
    JoystickPorts *joy = m->joysticks();
    REQUIRE(joy != nullptr);
    // Each interrupt sees the sticks as the host held them before it.
    const uint8_t held[5][2] = {{0, 0}, {J::UP, 0}, {J::FIRE | J::RIGHT, J::DOWN}, {0, J::ALL}, {J::LEFT, J::UP}};
    std::vector<uint64_t> taken;
    while (cpu.cycles() < 100'000 && cpu.state() != Cpu6502::State::Stopped)
    {
        cpu.step();
        if (bus.irq())
            cpu.irq();
        if (cpu.regs().pc == 0xC900) // into the handler: the sticks as the host holds them now
        {
            const size_t n = taken.size();
            if (n < 5)
            {
                joy->set(0, held[n][0]);
                joy->set(1, held[n][1]);
            }
            taken.push_back(cpu.cycles());
        }
    }
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);
    CHECK_EQ(bus.peek(0x0220), 5);
    REQUIRE(taken.size() >= 5);
    for (size_t i = 1; i < 5; ++i)
        CHECK_EQ(taken[i] - taken[i - 1], uint64_t{7159}); // the latch + 2, exactly
    for (uint16_t i = 0; i < 5; ++i)
    {
        CHECK_EQ(bus.peek(static_cast<uint16_t>(0x0200 + i)), 0xBF); // IRQL: module 6 alone
        CHECK_EQ(bus.peek(static_cast<uint16_t>(0x0208 + i)), static_cast<uint8_t>(~held[i][0]));
        CHECK_EQ(bus.peek(static_cast<uint16_t>(0x0210 + i)), static_cast<uint8_t>(~held[i][1]));
    }
    std::remove(opt.rom_override.c_str());
}

TEST(the_session_holds_the_sticks_through_a_power_cycle)
{
    std::string error;
    const std::string image = flash_image({0xCB, 0x80, 0xFD}, 0xC800); // WAI; BRA
    auto s = Session::create({EMU_MACHINE_DIR "/backplane.machine", image, ""}, error);
    if (!s)
        std::printf("  %s\n", error.c_str());
    REQUIRE(s != nullptr);
    REQUIRE(s->joysticks() != nullptr);
    s->set_joystick(0, J::UP);
    s->set_joystick(1, J::FIRE);
    CHECK_EQ(s->peek_io(0x341), 0xFE);
    CHECK_EQ(s->peek_io(0x340), 0xEF);
    REQUIRE(s->power_cycle(error));
    CHECK_EQ(s->joysticks()->pressed(0), J::UP);
    CHECK_EQ(s->peek_io(0x341), 0xFE);
    CHECK_EQ(s->peek_io(0x340), 0xEF);
    s->set_joystick(1, 0);
    CHECK_EQ(s->peek_io(0x340), 0xFF);
    std::remove(image.c_str());
}

TEST(the_debuggers_joy_shows_and_holds_the_sticks)
{
    auto m = backplane();
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    bus.reset();
    JoystickPorts *joy = m->joysticks();
    REQUIRE(joy != nullptr);
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    Symbols symbols;
    Debugger d(cpu, bus, symbols, nullptr);
    d.add_command("joy", "   joy\n", [joy](const std::vector<std::string> &args) { return joy->command(args); });
    CHECK_EQ(d.command("joy"), std::string("joystick 1: -\njoystick 2: -\n"));
    CHECK_EQ(d.command("joy 1 UF"), std::string("joystick 1: up fire\njoystick 2: -\n"));
    CHECK_EQ(bus.read(ORA), 0xEE);
    CHECK_EQ(d.command("joy 2 dlr"), std::string("joystick 1: up fire\njoystick 2: down left right\n"));
    CHECK_EQ(bus.read(ORB), 0xF1);
    CHECK_EQ(d.command("joy 1 -"), std::string("joystick 1: -\njoystick 2: down left right\n"));
    CHECK(d.command("joy 3 u").find("joy [1|2 STATE]") == 0);
    CHECK(d.command("joy 1 x").find("joy [1|2 STATE]") == 0);
    CHECK(d.command("joy 1").find("joy [1|2 STATE]") == 0);
    CHECK_EQ(joy->pressed(1), J::DOWN | J::LEFT | J::RIGHT); // a bad one changes nothing
    CHECK(d.command("help").find("   joy\n") != std::string::npos);
}

int main()
{
    return run_all();
}
