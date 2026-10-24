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
 * Then the screen (video.s): the monitor on the video card's text layer,
 * typing and backspace, scrolling by DMA, and a machine with no card. And
 * the video demo (demo.s, C803R): tiles, sprites, scroll and fade on the
 * card, and back to the monitor's screen as it was on a key.
 *
 * The ROM is assembled by this build with ca65/ld65, as backplane/bios's
 * Makefile does; without cc65 the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "machine.h"

#include <algorithm>
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
        explicit Board(bool bootsel = false, bool video = true)
        {
            std::string text = read_file(EMU_MACHINE_DIR "/backplane.machine");
            if (!video)
            {
                const size_t at = text.find("\nvideo ");
                if (at != std::string::npos)
                    text.erase(at + 1, text.find('\n', at + 1) - at);
            }
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

        /// The BIOS's text screen (video memory unit 8), row by row, trailing blanks dropped.
        std::string row(unsigned r) const
        {
            std::string t;
            for (unsigned c = 0; c < 80; ++c)
                t += static_cast<char>(machine->bus().peek_physical(SCREEN + r * 160 + c * 2));
            return t.substr(0, t.find_last_not_of(' ') + 1);
        }
        uint8_t attr(unsigned r, unsigned c) const { return machine->bus().peek_physical(SCREEN + r * 160 + c * 2 + 1); }
        /// The row holding `text`, or -1.
        int find_row(const std::string &text) const
        {
            for (unsigned r = 0; r < 30; ++r)
                if (row(r) == text)
                    return static_cast<int>(r);
            return -1;
        }

        static constexpr uint32_t SCREEN = 0x82000;

        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
    };

    constexpr uint64_t BOOT = 1'000'000; // cycles: the copy, then the banner at ~3,840 a character
    const std::string GREETING = "\rBACKPLANE BIOS 0.3\r\\\r";

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

TEST(the_monitor_is_on_the_vga_screen_in_the_vga_font)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    CHECK_EQ(b.run(BOOT), GREETING);       // the serial line is unchanged
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card != nullptr);
    CHECK_EQ(card->peek(VideoCard::CTRL), 0x03);
    const uint8_t l0 = VideoCard::LAYER0;
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), 0x80); // text 80x30, blink
    CHECK_EQ(card->peek(l0 + VideoCard::MAPBASE), 8);
    CHECK_EQ(card->peek(l0 + VideoCard::TILEBASE), 1);
    CHECK_EQ(b.row(0), std::string(""));
    CHECK_EQ(b.row(1), std::string("BACKPLANE BIOS 0.3"));
    CHECK_EQ(b.row(2), std::string("\\"));
    CHECK_EQ(b.row(3), std::string("_"));  // the cursor...
    CHECK_EQ(b.attr(3, 0), 0x87);          // ...blinking
    CHECK_EQ(b.attr(1, 0), 0x07);
    CHECK_EQ(b.attr(29, 79), 0x07);        // the whole screen was cleared
    // The font came from flash block 30, the palette is the CGA's.
    const std::string flash = read_file(EMU_BIOS_ROM);
    size_t differ = 0;
    for (uint32_t i = 0; i < 0x1000; ++i)
        differ += b.machine->bus().peek_physical(0x80400 + i) != static_cast<uint8_t>(flash[0x78000 + i]);
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(b.machine->bus().peek_physical(0x8000E), 0xAA); // entry 7: $AAA
    CHECK_EQ(b.machine->bus().peek_physical(0x8000F), 0x0A);
    // And the card draws it: the B of BACKPLANE, row 1, column 0, pixel for pixel.
    const uint32_t fg = VideoCard::rgba(0xAAA), bg = VideoCard::rgba(0x000);
    size_t wrong = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 8; ++x)
        {
            const bool on = (static_cast<uint8_t>(flash[0x78000 + 'B' * 16 + y]) >> (7 - x)) & 1;
            wrong += card->frame()[(16 + y) * 640 + x] != (on ? fg : bg);
        }
    CHECK_EQ(wrong, size_t{0});
}

TEST(typing_shows_on_the_screen_and_backspace_takes_it_back)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C680.C684\r");
    CHECK(b.find_row("C680.C684") >= 0);
    CHECK(b.find_row("C680: 00 01 02 7F 7F") > b.find_row("C680.C684"));
    b.run(BOOT, "12\b3");
    CHECK_EQ(b.row(static_cast<unsigned>(b.find_row("C680: 00 01 02 7F 7F") + 1)), std::string("13_"));
}

TEST(the_screen_scrolls_by_dma_and_keeps_its_colours)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const std::string full(80, '1');       // a whole row: the scroll must copy row 29's last cell too
    std::string keys;
    for (int i = 0; i < 16; ++i)           // three rows each (the line, a blank, the answer): past 30
    {
        keys += "FFFC.FFFD\r";
        if (i == 11)                       // the screen is full: typed on the bottom row, its own wrap
            keys += full + "\r";          // scrolls it, then 4 more commands carry it up 12 rows
    }
    b.run(keys.size() * 25'000 + BOOT, keys);
    CHECK(b.find_row(full) >= 0);          // scrolled up many times, still whole
    CHECK_EQ(b.find_row("BACKPLANE BIOS 0.3"), -1); // scrolled off the top
    CHECK_EQ(b.row(29), std::string("_"));
    CHECK_EQ(b.row(28), std::string("FFFC: 00 C8"));
    CHECK_EQ(b.row(27), std::string(""));
    CHECK_EQ(b.row(26), std::string("FFFC.FFFD"));
    size_t odd = 0;
    for (unsigned r = 0; r < 30; ++r)
        for (unsigned c = 0; c < 80; ++c)
            odd += b.attr(r, c) != (r == 29 && c == 0 ? 0x87 : 0x07);
    CHECK_EQ(odd, size_t{0});
}

TEST(without_a_video_card_the_bios_still_runs_the_monitor)
{
    Board b(false, false);
    REQUIRE(b.ok());
    REQUIRE(b.machine->video_card() == nullptr);
    b.reset();
    CHECK_EQ(b.run(BOOT), GREETING);
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F"));
}

namespace
{
    constexpr uint64_t FRAME = 119'437;          // a video frame at 7.16 MHz

    /// What the card shows now, as distinct colours.
    size_t colours(const VideoCard &card)
    {
        std::vector<uint32_t> seen;
        for (size_t i = 0; i < size_t{640} * 480; i += 7)
            if (std::find(seen.begin(), seen.end(), card.frame()[i]) == seen.end())
                seen.push_back(card.frame()[i]);
        return seen.size();
    }
} // namespace

TEST(the_demo_runs_from_c803_tiles_sprites_scroll_and_fade)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card != nullptr);
    b.run(30 * FRAME, "C803R\r");               // typed, then 20-odd frames: past the fade
    CHECK_EQ(card->peek(VideoCard::CTRL), 0x0F); // display, both layers, sprites
    const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), 0x22); // tiles, a 64-wide map
    CHECK_EQ(card->peek(l1 + VideoCard::CONFIG), 0x01); // the caption, text 40x30
    CHECK_EQ(card->peek(VideoCard::SPRBASE), 36);
    CHECK_EQ(b.bridge().window(1), 0xDD);        // flash block 29, read-only, at $4000
    // The palette faded all the way in: the caption's white is $FFF.
    CHECK_EQ(b.machine->bus().peek_physical(0x83400 + 2 * 15), 0xFF);
    CHECK_EQ(b.machine->bus().peek_physical(0x83400 + 2 * 15 + 1), 0x0F);
    CHECK(colours(*card) >= 12);                 // sky, hills, bricks, stars, balls, caption
    // The table the DMA keeps: 12 balls between the layers, and they move.
    auto ball0 = [&]() -> unsigned { return unsigned{b.machine->bus().peek_physical(0x89000 + 2)} | unsigned{b.machine->bus().peek_physical(0x89004)} << 16; };
    CHECK_EQ(b.machine->bus().peek_physical(0x89000 + 11 * 8 + 6), 0x18); // the 12th: depth 2, group 1
    CHECK_EQ(b.machine->bus().peek_physical(0x89000 + 12 * 8 + 6), 0x00); // no 13th
    const unsigned at = ball0(), scroll = card->peek(l0 + VideoCard::HSCROLL);
    b.run(10 * FRAME);
    CHECK_NE(ball0(), at);
    CHECK_EQ(static_cast<uint8_t>(card->peek(l0 + VideoCard::HSCROLL) - scroll), 10); // a pixel a frame
}

TEST(a_key_ends_the_demo_with_the_monitors_screen_as_it_was)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C680.C684\r");
    const std::string before = b.row(5);
    REQUIRE(before == "C680: 00 01 02 7F 7F");
    b.run(20 * FRAME, "C803R\r");
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    const std::string out = b.run(10 * FRAME, "X");
    CHECK(has(out, "\\"));                     // the monitor's prompt again
    const VideoCard *card = b.machine->video_card();
    const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
    CHECK_EQ(card->peek(VideoCard::CTRL), 0x03);
    CHECK_EQ(card->peek(VideoCard::PALBASE), 0);
    CHECK_EQ(card->peek(VideoCard::SPRBASE), 0);
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), 0x80);
    CHECK_EQ(card->peek(l0 + VideoCard::MAPBASE), 8);
    CHECK_EQ(card->peek(l0 + VideoCard::HSCROLL), 0);
    CHECK_EQ(card->peek(l1 + VideoCard::CONFIG), 0);
    CHECK_EQ(b.bridge().window(1), 0x01);        // the windows back
    CHECK_EQ(b.bridge().window(2), 0x02);
    CHECK_EQ(b.row(5), before);                  // the text screen kept
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F")); // and the monitor works
}

TEST(without_a_video_card_c803_goes_back_to_the_monitor)
{
    Board b(false, false);
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const std::string out = b.run(BOOT, "C803R\r");
    CHECK(has(out, "C803: 4C\\"));             // R shows the byte, then the prompt at once
    CHECK_EQ(b.bridge().window(1), 0x01);
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
