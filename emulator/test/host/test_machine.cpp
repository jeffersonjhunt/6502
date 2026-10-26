/**
 * @file test_machine.cpp
 * @brief Machine descriptions: parsing, errors, and the Apple-1 map they build.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The Apple-1 cases are Phase 1's memory-map rules (formerly test_memory),
 * now checked on what a description actually builds rather than on a
 * hand-coded map.
 */

#include "harness.h"

#include "acia6551.h"
#include "backplane_bridge.h"
#include "console_card.h"
#include "dma_controller.h"
#include "apple1.h"
#include "console.h"
#include "hd44780.h"
#include "machine.h"
#include "memory_devices.h"
#include "pia6821.h"
#include "ps2_keyboard.h"
#include "via6522.h"

#include <cstdio>
#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    /// A `size`-byte image of 0x01, 0x02, ... in the working directory.
    std::string image(const char *name, size_t size)
    {
        std::FILE *f = std::fopen(name, "wb");
        for (size_t i = 0; i < size; ++i)
            std::fputc(static_cast<int>((i % 255) + 1), f);
        std::fclose(f);
        return name;
    }

    std::unique_ptr<Machine> build(const std::string &text, std::string &error, MachineOptions opt = {})
    {
        return Machine::build(text, "test.machine", opt, error);
    }

    /// The error a description produces (empty if it built).
    std::string error_of(const std::string &text, MachineOptions opt = {})
    {
        std::string error;
        build(text, error, opt);
        return error;
    }

    bool starts_with(const std::string &s, const char *prefix)
    {
        if (s.rfind(prefix, 0) == 0)
            return true;
        std::printf("  got: %s\n", s.c_str());
        return false;
    }
} // namespace

TEST(builds_the_apple1_map)
{
    const std::string rom = image("rom_32k.bin", 0x8000);
    std::string error;
    auto m = build("ram 0000-7FFF\n"
                   "rom 8000-FFFF " + rom + "\n"
                   "pia6821 D010-D013 name=terminal\n",
                   error);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->bus().mappings().size(), size_t{3});
    CHECK(m->find_as<Pia6821>("terminal") != nullptr);

    Decoder &bus = m->bus();
    bus.write(0x7FFF, 0x11);
    CHECK_EQ(bus.read(0x7FFF), 0x11);   // RAM
    const uint8_t before = bus.read(0xE000);
    bus.write(0xE000, static_cast<uint8_t>(~before));
    CHECK_EQ(bus.read(0xE000), before); // ROM is read-only to the CPU
    CHECK_EQ(bus.read(0x8000), 0x01);   // and holds the image from offset 0
    bus.write(0xD013, 0x04);
    CHECK_EQ(bus.read(0xD013), 0x04);   // the PIA is live inside the ROM's range
}

TEST(comments_blank_lines_and_dollar_addresses)
{
    std::string error;
    auto m = build("# a comment\n\n   ram $0000-$00FF   # trailing\n", error);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->bus().mappings()[0].last, 0xFFu);
}

TEST(module_and_irq_options)
{
    std::string error;
    auto m = build("ram 0000-7FFF module=0\npia6821 D000-D003 name=io module=6 irq\n", error);
    REQUIRE(m != nullptr);
    const auto &maps = m->bus().mappings();
    CHECK_EQ(maps[0].module, 0x0);
    CHECK(!maps[0].irq_wired);
    CHECK_EQ(maps[1].module, 0x6);
    CHECK(maps[1].irq_wired);
}

TEST(physical_addresses_above_64k)
{
    std::string error;
    auto m = build("ram 10000-1FFFF\n", error);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->bus().mappings()[0].first, 0x10000u);
    CHECK(starts_with(error_of("ram 0-100000\n"), "test.machine:1: ram: want a range"));
}

TEST(dma_line_and_drq_option)
{
    std::string error;
    auto m = build("ram 0000-7FFF\ndma C400-C47F name=dma irq\npia6821 D000-D003 drq=2\n", error);
    REQUIRE(m != nullptr);
    CHECK(m->find_as<DmaController>("dma") != nullptr);
    CHECK_EQ(m->bus().mappings()[0].drq, NO_DRQ);
    CHECK_EQ(m->bus().mappings()[2].drq, 2);
    CHECK_EQ(m->bus().read(0xC47F), 0x81);
    CHECK(starts_with(error_of("dma C400-C47F\ndma C500-C57F\n"), "test.machine:2: dma: only one"));
    CHECK(starts_with(error_of("ram 0-F drq=4\n"), "test.machine:1: drq:"));
    CHECK(starts_with(error_of("ram 0-F drq=\n"), "test.machine:1: drq:"));
    CHECK(starts_with(error_of("dma C400-C47F x.bin\n"), "test.machine:1: dma takes no image"));
}

TEST(console_line)
{
    std::string error;
    auto m = build("ram 0000-7FFF\nconsole C700-C77F name=console irq\n", error);
    REQUIRE(m != nullptr);
    CHECK(m->console_card() != nullptr);
    CHECK(m->console_card() == m->find_as<ConsoleCard>("console"));
    CHECK_EQ(m->bus().read(0xC77F), 0xE1);
    CHECK(build("ram 0000-7FFF\n", error)->console_card() == nullptr);
    CHECK(starts_with(error_of("console C700-C77F\nconsole C780-C7FF\n"), "test.machine:2: console: only one"));
    CHECK(starts_with(error_of("console C700-C77F x.bin\n"), "test.machine:1: console takes no image"));
}

TEST(wait_option)
{
    std::string error;
    auto m = build("ram 0000-7FFF\nram 8000-80FF wait=3\n", error);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->bus().mappings()[0].wait, 0);
    CHECK_EQ(m->bus().mappings()[1].wait, 3);
    m->bus().read(0x8000);
    CHECK_EQ(m->bus().wait_states(), 3u);
    CHECK(starts_with(error_of("ram 0-F wait=16\n"), "test.machine:1: wait:"));
    CHECK(starts_with(error_of("ram 0-F wait=x\n"), "test.machine:1: wait:"));
    CHECK(starts_with(error_of("ram 0-F wait=\n"), "test.machine:1: wait:"));
}

TEST(spaces_and_the_northbridge_line)
{
    const std::string flash = image("flash_16.bin", 16);
    std::string error;
    auto m = build("northbridge backplane\n"
                   "ram 00000-FFFFF space=memory\n"
                   "rom 7FFF0-7FFFF space=flash " + flash + "\n"
                   "pia6821 300-303 space=io name=card\n"
                   "pia6821 500-503 space=io module=9\n",
                   error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    const auto &maps = m->bus().mappings();
    REQUIRE(maps.size() == 6u);
    CHECK_EQ(maps[0].first, bus_address(Space::Io, 0x680)); // the bridge's own registers
    CHECK_EQ(maps[0].module, 0xD);
    CHECK_EQ(maps[1].first, bus_address(Space::Io, APPLE_IO)); // and Apple II mode's $C000-$CFFF
    CHECK_EQ(maps[3].first, bus_address(Space::Flash, 0x7FFF0));
    CHECK_EQ(maps[4].module, 0x6);                          // from its I/O block
    CHECK_EQ(maps[5].module, 0x9);                          // module= wins
    REQUIRE(m->find_as<BackplaneBridge>("northbridge") != nullptr);
    CHECK_EQ(m->bus().read(0xFFF0), 0x01);                  // W3 at reset: top of ROM A
    CHECK(m->bus().decode(0xC300)->device == m->find("card"));

    auto boot = build("northbridge backplane bootsel\nram 00000-FFFFF\n", error);
    REQUIRE(boot != nullptr);
    CHECK_EQ(boot->find_as<BackplaneBridge>("northbridge")->window(3), 0x3F);

    CHECK(starts_with(error_of("northbridge\n"), "test.machine:1: northbridge: want"));
    CHECK(starts_with(error_of("northbridge chipset\n"), "test.machine:1: northbridge: want"));
    CHECK(starts_with(error_of("northbridge backplane fast\n"), "test.machine:1: northbridge: want"));
    CHECK(starts_with(error_of("northbridge backplane\nnorthbridge backplane\n"),
                      "test.machine:2: northbridge: only one"));
    CHECK(starts_with(error_of("ram 0-F name=northbridge\nnorthbridge backplane\n"),
                      "test.machine:2: name 'northbridge' is already used"));
    CHECK(starts_with(error_of("ram 0-F space=disk\n"), "test.machine:1: space:"));
    CHECK(starts_with(error_of("ram 700-800 space=io\n"), "test.machine:1: ram: the I/O hole is 000-7FF"));
}

TEST(rom_override_replaces_only_the_first_rom)
{
    const std::string a = image("rom_a.bin", 16), b = image("rom_b.bin", 16);
    MachineOptions opt;
    opt.rom_override = b;
    std::string error;
    auto m = build("rom F000-F00F no_such.rom name=first\nrom F010-F01F " + a + " name=second\n", error, opt);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->find("first")->read(0), 0x01);  // came from rom_b, not no_such.rom
    CHECK(starts_with(error_of("ram 0000-00FF\n", opt), "test.machine: has no rom line"));
}

TEST(errors_name_the_line)
{
    const std::string big = image("rom_17.bin", 17);
    CHECK(starts_with(error_of("ram 0000-00FF\nflux 0000-0001\n"), "test.machine:2: unknown type 'flux'"));
    CHECK(starts_with(error_of("ram 00FF-0000\n"), "test.machine:1: ram: want a range"));
    CHECK(starts_with(error_of("ram 0000-00FF module=10\n"), "test.machine:1: module:"));
    CHECK(starts_with(error_of("ram 0-F name=x\nram 10-1F name=x\n"), "test.machine:2: name 'x' is already used"));
    CHECK(starts_with(error_of("rom F000-FFFF\n"), "test.machine:1: rom: needs an image"));
    CHECK(starts_with(error_of("rom F000-FFFF no_such.rom\n"), "test.machine:1: rom: cannot load no_such.rom"));
    CHECK(starts_with(error_of("rom F000-F00F " + big + "\n"), "test.machine:1: rom: cannot load"));
    CHECK(starts_with(error_of("ram 0-F speed=fast\n"), "test.machine:1: unknown option 'speed'"));
    CHECK(starts_with(error_of("ram 0-F extra.bin\n"), "test.machine:1: ram takes no image"));
    CHECK(starts_with(error_of("cpu z80\n"), "test.machine:1: cpu:"));
    CHECK(starts_with(error_of("# nothing\n"), "test.machine: describes no devices"));
}

TEST(via_acia_and_peripherals)
{
    std::string error;
    auto m = build("via6522  6000-7FFF name=via irq\n"
                   "acia6551 5000-5FFF name=terminal\n"
                   "hd44780  name=lcd via=via data=PB0-3 rs=PB4 rw=PB5 e=PB6 size=20x4\n"
                   "ps2kbd   name=keyboard via=via data=PA0-7\n",
                   error);
    CHECK(m != nullptr);
    if (!m)
        return;
    auto *via = m->find_as<Via6522>("via");
    CHECK(via != nullptr);
    m->bus().write(0x7FF2, 0xAB); // DDRB through the last mirror of 16
    CHECK_EQ(via->ddr(Via6522::Port::B), 0xAB);
    CHECK(m->find_as<Acia6551>("terminal") != nullptr);
    const auto *lcd = m->find_peripheral<ViaLcd>("lcd");
    CHECK(lcd && lcd->lcd().columns() == 20 && lcd->lcd().rows() == 4);
    CHECK(m->find_peripheral<Ps2Keyboard>("keyboard") != nullptr);
    CHECK(m->find_peripheral<Ps2Keyboard>("lcd") == nullptr); // wrong type
    CHECK(make_console(*m, error) != nullptr);
}

TEST(peripheral_errors_name_the_line)
{
    const std::string via = "via6522 6000-600F name=via\n";
    CHECK(starts_with(error_of("hd44780 name=lcd via=nope data=PB0-7 rs=PA5 rw=PA6 e=PA7\n"),
                      "test.machine:1: hd44780: via=NAME must name a via6522"));
    CHECK(starts_with(error_of("ram 0-F name=r\nhd44780 via=r data=PB0-7 rs=PA5 rw=PA6 e=PA7\n"),
                      "test.machine:2: hd44780: via=NAME must name a via6522"));
    CHECK(starts_with(error_of(via + "hd44780 via=via data=PB0-5 rs=PA5 rw=PA6 e=PA7\n"),
                      "test.machine:2: hd44780: data= wants 4 or 8 pins"));
    CHECK(starts_with(error_of(via + "hd44780 via=via data=PB0-7 rs=PA5 rw=PA6\n"),
                      "test.machine:2: hd44780: rs=, rw= and e="));
    CHECK(starts_with(error_of(via + "hd44780 via=via data=PB0-7 rs=PC5 rw=PA6 e=PA7\n"),
                      "test.machine:2: hd44780: rs=, rw= and e="));
    CHECK(starts_with(error_of(via + "hd44780 via=via data=PB0-7 rs=PA5 rw=PA6 e=PA7 size=99x9\n"),
                      "test.machine:2: hd44780: size="));
    CHECK(starts_with(error_of(via + "hd44780 via=via data=PB0-7 rs=PA5 rw=PA6 e=PA7 colour=blue\n"),
                      "test.machine:2: hd44780: unknown option 'colour'"));
    CHECK(starts_with(error_of(via + "ps2kbd via=via data=PA0-3\n"), "test.machine:2: ps2kbd: data= wants 8 pins"));
    CHECK(starts_with(error_of(via + "ps2kbd via=via PA0-7\n"), "test.machine:2: ps2kbd: options are key=value"));
    CHECK(starts_with(error_of(via + "ps2kbd name=k via=via data=PA0-7\nps2kbd name=k via=via data=PA0-7\n"),
                      "test.machine:3: name 'k' is already used"));
    CHECK(starts_with(error_of("via6522 6000-600F x.rom\n"), "test.machine:1: via6522 takes no image"));
}

TEST(a_console_needs_something_to_talk_through)
{
    std::string error;
    auto bare = build("ram 0000-7FFF\n", error);
    CHECK(bare && make_console(*bare, error) == nullptr);
    CHECK(error.find("no 'terminal'") != std::string::npos);
    auto wrong = build("ram 0000-7FFF name=terminal\n", error);
    CHECK(wrong && make_console(*wrong, error) == nullptr);
    CHECK(error.find("must be a pia6821 or an acia6551") != std::string::npos);
}

TEST(a_full_keyboard_refuses_keys_so_a_paste_waits)
{
    // A PS/2 keyboard whose program never reads it fills its queue; a key it
    // cannot take must be refused (and not given to the serial line either),
    // so the host offers it again rather than losing it.
    std::string error;
    auto m = build("ram 0000-3FFF\nacia6551 5000-5FFF name=terminal\nvia6522 6000-7FFF name=via\n"
                   "ps2kbd name=keyboard via=via data=PA0-7\n",
                   error);
    REQUIRE(m != nullptr);
    auto console = make_console(*m, error);
    REQUIRE(console != nullptr);
    m->bus().write(0x600E, 0x80 | Via6522::IRQ_CA1); // the program listens for keys
    m->bus().write(0x5002, Acia6551::CMD_DTR);       // and on the serial line

    unsigned taken = 0;
    while (taken < 2000 && console->key('a'))
    {
        ++taken;
        m->bus().read(0x5000); // the serial line's program keeps up; the keyboard's does not
    }
    CHECK(taken > 0 && taken < 2000);
    CHECK(!console->key('b'));
    CHECK(!(m->bus().peek(0x5001) & Acia6551::STATUS_RDRF)); // RDRF clear: the refused key did not reach the ACIA either
    CHECK(console->key(0x01));              // no scan code: the keyboard is no reason to wait
}

TEST(builtin_default_is_the_apple1)
{
    // The compiled-in text is machines/apple1.machine. Its image name is
    // relative to the working directory, so point it at a scratch ROM.
    MachineOptions opt;
    opt.rom_override = image("rom_32k.bin", 0x8000);
    std::string error;
    auto m = Machine::build(apple1::default_machine(), "apple1.machine", opt, error);
    REQUIRE(m != nullptr);
    CHECK(m->find_as<Pia6821>(apple1::TERMINAL_PIA) != nullptr);
    CHECK_EQ(m->bus().mappings().size(), size_t{3});
}

TEST(cpu_line_picks_the_model)
{
    std::string error;
    auto m = build("cpu w65c02s\nram 0000-00FF\n", error);
    REQUIRE(m != nullptr);
    CHECK(m->cpu() == "w65c02s");
    auto d = build("ram 0000-00FF\n", error);
    REQUIRE(d != nullptr);
    CHECK(d->cpu() == "nmos6502"); // the default
}

TEST(load_reports_a_missing_file)
{
    std::string error;
    CHECK(Machine::load("no_such.machine", {}, error) == nullptr);
    CHECK(starts_with(error, "no_such.machine: cannot open"));
}

int main()
{
    return run_all();
}
