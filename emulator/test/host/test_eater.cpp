/**
 * @file test_eater.cpp
 * @brief Ben Eater's programs on the Eater machines, headless.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The programs in eater/src were written for the real breadboard, so
 * running them is the end-to-end check of the 6522, the 6551, the LCD and
 * the PS/2 keyboard together, through the machine files the app uses:
 *
 * - hello-world.s and hello-world-final.s: the LCD on 8 lines, the second
 *   polling the busy flag through port B with DDRB switched to input.
 * - hello-world-4pin.s: 4-bit mode with E/RW/RS on PB7-5, a wiring no
 *   shipped machine has, so it is described inline.
 * - keyboard.s: PS/2 scan codes through CA1 interrupts into the LCD. Every
 *   printable character is typed and must come back off the LCD, which
 *   checks the scan code table against Eater's own keymap. (That found a
 *   bug in the keymap, not the table: Shift+3 and Shift+4 are swapped.)
 * - wozmon.s over the 6551: examine and store through the serial line.
 * - blink.s: the pattern on port B.
 *
 * The ROMs are assembled by this build with vasm, as eater/Makefile does;
 * without vasm the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "console.h"
#include "hd44780.h"
#include "machine.h"
#include "via6522.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    std::string rom(const char *name) { return std::string(EMU_EATER_ROM_DIR "/") + name + ".rom"; }

    /// A machine, its console and CPU, reset and ready.
    struct Board
    {
        Board(const std::string &description, const std::string &origin, const std::string &image)
        {
            MachineOptions opt;
            opt.rom_override = image;
            std::string error;
            machine = Machine::build(description, origin, opt, error);
            if (machine)
                console = make_console(*machine, error);
            if (!console)
            {
                std::printf("  %s: %s\n", origin.c_str(), error.c_str());
                return;
            }
            cpu = std::make_unique<Cpu6502>(machine->bus(), Cpu6502::Model::W65C02S);
            cpu->reset();
        }

        static Board from_file(const char *machine_file, const char *program)
        {
            const std::string path = std::string(EMU_MACHINE_DIR "/") + machine_file;
            std::ifstream in(path);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return Board(text, path, rom(program));
        }

        bool ok() const { return cpu != nullptr; }

        /// Run `cycles`, offering `keys` one at a time (each until taken),
        /// spaced so the program has handled one before the next arrives.
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

        std::string lcd(unsigned row) const { return machine->find_peripheral<ViaLcd>(LCD)->lcd().line(row); }

        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
    };

    constexpr uint64_t SECOND = 1'000'000; // at 1 MHz
} // namespace

TEST(hello_world_final_8_bit_busy_polling)
{
    Board b = Board::from_file("eater-lcd8.machine", "hello-world-final");
    CHECK(b.ok());
    b.run(SECOND / 10);
    CHECK_EQ(b.lcd(0), std::string("Hello, world!   "));
}

TEST(hello_world_unrolled)
{
    Board b = Board::from_file("eater-lcd8.machine", "hello-world");
    CHECK(b.ok());
    b.run(SECOND / 10);
    CHECK_EQ(b.lcd(0), std::string("Hello, world!   "));
}

TEST(hello_world_4pin_wiring_inline)
{
    const std::string machine = "cpu w65c02s\n"
                                "ram 0000-3FFF\n"
                                "via6522 6000-7FFF name=via\n"
                                "rom 8000-FFFF x.rom\n"
                                "hd44780 name=lcd via=via data=PB0-3 rs=PB5 rw=PB6 e=PB7\n";
    Board b(machine, "4pin.machine", rom("hello-world-4pin"));
    CHECK(b.ok());
    b.run(SECOND / 10);
    CHECK_EQ(b.lcd(0), std::string("Hello, world!   "));
}

TEST(keyboard_types_every_printable_character)
{
    Board b = Board::from_file("eater.machine", "keyboard");
    CHECK(b.ok());
    b.run(SECOND / 10); // LCD set up, CA1 interrupt enabled
    b.run(SECOND, "Hi\nthere");
    CHECK_EQ(b.lcd(0), std::string("Hi              "));
    CHECK_EQ(b.lcd(1), std::string("there           ")); // Enter: DDRAM $28, line 2 on the board

    // Every printable character but '~' (an arrow in the LCD's ROM), 16 at a
    // time between Escapes (which clear the display).
    std::string all;
    for (int c = 0x20; c <= 0x7D; ++c)
        all += static_cast<char>(c);
    for (size_t at = 0; at < all.size(); at += 16)
    {
        const std::string chunk = all.substr(at, 16);
        b.run(SECOND, "\x1b" + chunk);
        // keyboard.s's shifted keymap has # and $ the wrong way round ($25
        // is '4' unshifted but '#' shifted), so the board shows them swapped.
        std::string want = chunk;
        for (char &c : want)
            c = c == '#' ? '$' : c == '$' ? '#' : c;
        const std::string shown = b.lcd(0).substr(0, chunk.size());
        if (shown != want)
            std::printf("  typed '%s', LCD shows '%s'\n", chunk.c_str(), shown.c_str());
        CHECK_EQ(shown, want);
    }
}

TEST(wozmon_over_the_serial_port)
{
    Board b = Board::from_file("eater.machine", "wozmon");
    CHECK(b.ok());
    std::string out = b.run(SECOND / 10);
    CHECK_EQ(out, std::string("\\\r")); // the prompt, then CR
    out = b.run(SECOND, "FF00.FF07\r");
    CHECK(out.find("FF00: A9 1F 8D 03 50 A9 0B 8D") != std::string::npos);
    out = b.run(SECOND, "300: 12 34\r300.301\r");
    CHECK(out.find("0300: 12 34") != std::string::npos);
    CHECK_EQ(b.machine->bus().peek(0x0301), 0x34);
}

TEST(blink_rotates_port_b)
{
    Board b = Board::from_file("eater-lcd8.machine", "blink");
    CHECK(b.ok());
    std::vector<uint8_t> seen;
    b.machine->find_as<Via6522>("via")->listen([&](Via6522::Port port, uint8_t out, uint8_t) {
        if (port == Via6522::Port::B && seen.size() < 8)
            seen.push_back(out);
    });
    b.run(1000);
    // DDRB first (ORB still 0), then $50 rotated right through carry (clear after reset).
    CHECK(seen == (std::vector<uint8_t>{0x00, 0x50, 0x28, 0x14, 0x0A, 0x05, 0x02, 0x81}));
}

int main()
{
    if (!std::ifstream(rom("wozmon")))
    {
        std::printf("SKIP: %s not built (needs vasm6502_oldstyle)\n", rom("wozmon").c_str());
        return SKIP;
    }
    return run_all();
}
