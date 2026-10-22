/**
 * @file test_bios.cpp
 * @brief The backplane BIOS (backplane/bios) on machines/backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The BIOS shadows itself from flash into RAM block 63, points W3 and VEC
 * at the copy and enters the Woz Monitor on the stand-in 6551. These run it
 * headless through the console the app uses, and check the boot from the
 * outside: the banner and prompt, the north bridge's registers as the
 * monitor reads them, a program run with R, the read-only copy, NMI back
 * into the monitor, and the /BOOTSEL boot from an image already in RAM.
 *
 * The ROM is assembled by this build with ca65/ld65, as backplane/bios's
 * Makefile does; without cc65 the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "machine.h"

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
    std::string read_file(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// backplane.machine with the BIOS, its console and CPU, reset and ready.
    struct Board
    {
        explicit Board(bool bootsel = false)
        {
            std::string text = read_file(EMU_MACHINE_DIR "/backplane.machine");
            if (bootsel)
            {
                const std::string line = "northbridge  backplane";
                const size_t at = text.find(line);
                if (at == std::string::npos)
                {
                    std::printf("  backplane.machine has no '%s' line\n", line.c_str());
                    return;
                }
                text.insert(at + line.size(), " bootsel");
            }
            MachineOptions opt;
            opt.rom_override = EMU_BIOS_ROM;
            std::string error;
            machine = Machine::build(text, "backplane.machine", opt, error);
            if (machine)
                console = make_console(*machine, error);
            if (!console)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            cpu = std::make_unique<Cpu6502>(machine->bus(), Cpu6502::Model::W65C02S);
        }

        bool ok() const { return cpu != nullptr; }

        void reset()
        {
            machine->bus().reset();
            cpu->reset();
        }

        /// Run `cycles`, offering `keys` one at a time (each until taken),
        /// spaced so the monitor has echoed one before the next arrives.
        /// @return what the console printed
        std::string run(uint64_t cycles, const std::string &keys = "", uint64_t key_gap = 20'000)
        {
            std::string out;
            size_t next = 0;
            uint64_t last_key = 0;
            const uint64_t end = cpu->cycles() + cycles;
            while (cpu->cycles() < end)
            {
                cpu->step();
                if (machine->bus().irq())
                    cpu->irq();
                if (cpu->cycles() % 64 > 7)
                    continue; // service the console about once every 64 cycles
                console->service();
                uint8_t ch = 0;
                while (console->output(ch))
                    out += static_cast<char>(ch);
                if (next < keys.size() && cpu->cycles() - last_key > key_gap &&
                    console->key(static_cast<uint8_t>(keys[next])))
                {
                    ++next;
                    last_key = cpu->cycles();
                }
            }
            if (next < keys.size())
                std::printf("  only %zu of %zu keys were taken\n", next, keys.size());
            return out;
        }

        const BackplaneBridge &bridge() const { return *machine->find_as<BackplaneBridge>("northbridge"); }

        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
    };

    constexpr uint64_t BOOT = 1'000'000; // cycles: the copy, then the banner at ~3,840 a character
    const std::string GREETING = "\rBACKPLANE BIOS 0.1\r\\\r";

    /// A label's address from the ld65 -Ln file ("al 00C862 .WOZMON"), or 0.
    unsigned label(const std::string &lbl, const std::string &name)
    {
        const size_t at = lbl.find(" ." + name + "\n");
        if (at == std::string::npos || at < 6)
            return 0;
        return static_cast<unsigned>(std::stoul(lbl.substr(at - 6, 6), nullptr, 16));
    }

    std::string hex(unsigned v)
    {
        char buf[3];
        std::snprintf(buf, sizeof buf, "%02X", v & 0xFF);
        return buf;
    }

    bool has(const std::string &out, const char *want)
    {
        if (out.find(want) != std::string::npos)
            return true;
        std::printf("  wanted '%s' in '%s'\n", want, out.c_str());
        return false;
    }
} // namespace

TEST(boots_from_flash_into_the_monitor)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    CHECK_EQ(b.cpu->regs().pc, 0xC800);
    CHECK_EQ(b.run(BOOT), GREETING);
    CHECK_EQ(b.bridge().window(2), 0x02);  // W2 put back
    CHECK_EQ(b.bridge().window(3), 0x7F);  // RAM block 63, read-only
    CHECK_EQ(b.bridge().vec(), 0x7F);
    // The copy in RAM D matches the flash it came from.
    const std::string flash = read_file(EMU_BIOS_ROM);
    REQUIRE(flash.size() == 0x80000u);
    Device *ram_d = b.machine->find("ram_d");
    REQUIRE(ram_d != nullptr);
    size_t differ = 0;
    for (uint32_t a = 0x0800; a < 0x4000; ++a)
        differ += ram_d->peek(0x3C000 + a) != static_cast<uint8_t>(flash[0x7C000 + a]);
    CHECK_EQ(differ, size_t{0});
}

TEST(monitor_reads_the_north_bridge_and_runs_programs)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    std::string out = b.run(BOOT, "C680.C684\r");
    CHECK(has(out, "C680: 00 01 02 7F 7F"));
    out = b.run(BOOT, "FFFC.FFFD\r");
    CHECK(has(out, "FFFC: 00 C8"));
    // LDA #'A'; JSR ECHO; JMP WOZMON: the BIOS's own routines, from RAM.
    const std::string lbl = read_file(EMU_BIOS_ROM ".lbl");
    const unsigned echo = label(lbl, "ECHO"), wozmon = label(lbl, "WOZMON");
    REQUIRE(echo >= 0xC800 && wozmon >= 0xC800);
    b.run(BOOT, "300: A9 41 20 " + hex(echo) + " " + hex(echo >> 8) + " 4C " + hex(wozmon) + " " +
                    hex(wozmon >> 8) + "\r");
    out = b.run(BOOT, "300R\r");
    CHECK(has(out, "0300: A9A\\\r"));
}

TEST(the_shadow_copy_is_read_only)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const uint8_t before = b.machine->bus().peek(0xD000);
    b.run(BOOT, "D000: 55\r");
    CHECK_EQ(b.machine->bus().peek(0xD000), before);
    b.run(BOOT, "C686: 00\r");             // STATUS is read-only too
    CHECK(b.machine->bus().peek(0xC686) & BackplaneBridge::STATUS_POWER_ON);
}

TEST(nmi_enters_the_monitor)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);                           // keys before DTR is on are dropped, as on a serial line
    b.run(BOOT, "300: 80 FE\r");           // BRA * : a program that never returns
    b.run(BOOT, "300R\r");
    CHECK_EQ(b.cpu->regs().pc & 0xFFF0, 0x0300);
    b.cpu->nmi();
    CHECK(has(b.run(BOOT), "\\\r"));
    CHECK(has(b.run(BOOT, "C683\r"), "C683: 7F"));
}

TEST(bootsel_boots_the_same_image_from_ram)
{
    // As the bus spy would: the image in RAM block 63, /BOOTSEL held, flash erased.
    Board b(true);
    REQUIRE(b.ok());
    const std::string flash = read_file(EMU_BIOS_ROM);
    Device *ram_d = b.machine->find("ram_d");
    Device *rom_a = b.machine->find("rom_a");
    REQUIRE(ram_d != nullptr && rom_a != nullptr && flash.size() == 0x80000u);
    for (uint32_t a = 0; a < 0x4000; ++a)
    {
        ram_d->poke(0x3C000 + a, static_cast<uint8_t>(flash[0x7C000 + a]));
        rom_a->poke(0x7C000 + a, 0xFF);
    }
    b.reset();
    CHECK_EQ(b.bridge().window(3), 0x3F);
    CHECK_EQ(b.run(BOOT), GREETING);
    CHECK_EQ(b.bridge().window(3), 0x7F);
    CHECK(has(b.run(BOOT, "C686\r"), "C686: 13")); // revision 1, power-on, BOOTSEL
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
