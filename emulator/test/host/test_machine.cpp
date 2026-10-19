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

#include "apple1.h"
#include "machine.h"
#include "memory_devices.h"
#include "pia6821.h"

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
