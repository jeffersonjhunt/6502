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
 * card, and back to the monitor's screen as it was on a key. And the USB
 * keyboard (input.s): the monitor takes keys from it, Pause/Break breaks
 * in, and a USB key ends the demo. And Apple II mode's way in (apple.s,
 * C806R), with a stand-in for the owner's IIe ROM: these tests never need
 * Apple's. And the BIOS's recovery: Break (in the demo, or inside the screen
 * code) and BRK back into the monitor with the windows and the screen as the
 * BIOS keeps them, C800R from the flash again, the cursor kept on the screen
 * whatever its zero page says, and scrolling whole under a DMA burst limit.
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
#include <cstdlib>
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
        explicit Board(bool bootsel = false, bool video = true, std::vector<std::string> apple_rom = {},
                       std::string apple_chars = "")
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
            opt.apple_rom = std::move(apple_rom);
            opt.apple_chars = std::move(apple_chars);
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
                if (machine->bus().take_reset()) // the run loop's job, as Session's: Apple II mode's /RESET
                {
                    machine->bus().pulse_reset();
                    cpu->reset();
                }
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

        /// Type on the USB keyboard: each character's key down, a gap, up, a gap.
        /// Letters, digits, space, '.', ':' and CR. @return what the console printed
        std::string type_usb(const std::string &text, uint64_t gap = 25'000)
        {
            UsbCard *usb = machine->usb_card();
            std::string out;
            for (const char ch : text)
            {
                uint8_t usage = 0;
                bool shift = false;
                if (ch >= 'A' && ch <= 'Z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'A');
                else if (ch >= 'a' && ch <= 'z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'a');
                else if (ch >= '1' && ch <= '9')
                    usage = static_cast<uint8_t>(0x1E + ch - '1');
                else if (ch == '0')
                    usage = 0x27;
                else if (ch == ' ')
                    usage = 0x2C;
                else if (ch == '.')
                    usage = 0x37;
                else if (ch == ':')
                    usage = 0x33, shift = true;
                else if (ch == '\r')
                    usage = 0x28;
                if (shift)
                    usb->key(0xE1, true);
                usb->key(usage, true);
                out += run(gap);
                usb->key(usage, false);
                if (shift)
                    usb->key(0xE1, false);
                out += run(gap);
            }
            return out + run(250'000);            // the answer, at the serial line's pace
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
    const std::string GREETING = "\rBACKPLANE BIOS 0.9\r\\\r";

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
    CHECK_EQ(b.row(1), std::string("BACKPLANE BIOS 0.9"));
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
    CHECK_EQ(b.find_row("BACKPLANE BIOS 0.9"), -1); // scrolled off the top
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

TEST(the_monitor_takes_keys_from_the_usb_keyboard)
{
    Board b;
    REQUIRE(b.ok());
    REQUIRE(b.machine->usb_card() != nullptr);
    b.reset();
    b.run(BOOT);
    CHECK_EQ(b.machine->usb_card()->peek(UsbCard::KBCFG), 0x80); // KINIT: Pause/Break breaks in
    CHECK(has(b.type_usb("C680.C684\r"), "C680.C684\r\rC680: 00 01 02 7F 7F"));
    UsbCard *usb = b.machine->usb_card();
    usb->key(0x39, true);                        // Caps Lock off: the BIOS makes capitals anyway
    usb->key(0x39, false);
    REQUIRE(!usb->caps_lock());
    CHECK(has(b.type_usb("fffc.fffd\r"), "FFFC: 00 C8"));
    CHECK(has(b.run(BOOT, "C680\r"), "C680: 00"));     // and the serial line still works
}

TEST(pause_break_on_the_usb_keyboard_enters_the_monitor)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.type_usb("C680");                          // half a line...
    UsbCard *usb = b.machine->usb_card();
    usb->key(UsbCard::PAUSE, true);
    std::string out;
    for (int i = 0; i < 1000 && out.find("\\") == std::string::npos; ++i)
    {
        b.cpu->step();
        if (usb->take_nmi())                     // the run loop's job (Session, the app)
            b.cpu->nmi();
        b.console->service();
        uint8_t ch = 0;
        while (b.console->output(ch))
            out += static_cast<char>(ch);
    }
    usb->key(UsbCard::PAUSE, false);
    out += b.run(BOOT);
    CHECK(has(out, "\\"));                     // ...abandoned: a fresh prompt
    CHECK(has(b.type_usb("C680.C680\r"), "C680: 00"));
}

TEST(a_usb_key_ends_the_demo)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.type_usb("C803R\r");
    b.run(20 * FRAME);
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    const std::string out = b.type_usb(" ");
    CHECK(has(out, "\\"));
    CHECK_EQ(b.machine->video_card()->peek(VideoCard::CTRL), 0x03);
    CHECK_EQ(b.bridge().window(1), 0x01);
}

namespace
{
    /// A stand-in for the owner's IIe ROM: 16 KB as at $C000-$FFFF, a IIe's
    /// ID byte at $FBB3, a reset vector, and a pattern to recognise it by.
    std::string fake_iie_rom(const std::string &path, uint8_t id = 0x06)
    {
        std::string image(0x4000, '\0');
        for (size_t i = 0; i < image.size(); ++i)
            image[i] = static_cast<char>(i * 13 + 5);
        image[0xFBB3 - 0xC000] = static_cast<char>(id);
        image[0xFFFC - 0xC000] = 0x62; // $FA62, the IIe's reset
        image[0xFFFD - 0xC000] = static_cast<char>(0xFA);
        // ...which here stores $A5 at $0300 and stops: LDA #$A5; STA $0300; JMP *
        const uint8_t reset[] = {0xA9, 0xA5, 0x8D, 0x00, 0x03, 0x4C, 0x67, 0xFA};
        for (size_t i = 0; i < sizeof reset; ++i)
            image[0xFA62 - 0xC000 + i] = static_cast<char>(reset[i]);
        std::ofstream(path, std::ios::binary) << image;
        return path;
    }
} // namespace

TEST(c806_without_a_iie_rom_says_so_and_goes_back_to_the_monitor)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const std::string out = b.run(BOOT, "C806R\r");
    CHECK(has(out, "NO APPLE IIE ROM IN THE CARTRIDGE\\"));  // then the monitor's prompt
    CHECK_EQ(b.bridge().window(1), 0x01);
    CHECK(has(b.run(BOOT, "C680\r"), "C680: 00"));          // the monitor carries on
    const std::string plus = fake_iie_rom("test_ii_plus.rom", 0xEA); // an Apple II Plus's ROM: not a IIe's
    Board p(false, true, {plus});
    std::remove(plus.c_str());
    REQUIRE(p.ok());
    p.reset();
    p.run(BOOT);
    CHECK(has(p.run(BOOT, "C806R\r"), "NO APPLE IIE ROM IN THE CARTRIDGE\\"));
}

TEST(c806_copies_the_iie_rom_to_bank_e_clears_the_iies_memory_and_writes_mode)
{
    const std::string rom = fake_iie_rom("test_fake_iie.rom");
    Board b(false, true, {rom});
    std::remove(rom.c_str());
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    for (uint32_t a : {0x10000u, 0x2ABCDu, 0x3FFFFu, 0x0C000u, 0x4D000u, 0x5DFFFu, 0x41234u})
        bus.write_physical(a, 0x5A);                          // something to clear (and one to keep)
    REQUIRE(bus.peek_physical(0x2ABCD) == 0x5A);
    const std::string flash = read_file(EMU_BIOS_ROM);
    b.run(2 * BOOT, "C806R\r");
    size_t differ = 0;
    for (uint32_t i = 0; i < 0x4000; ++i)
        differ += bus.peek_physical(0xEC000 + i) != static_cast<uint8_t>(i * 13 + 5) &&
                  i != 0xFBB3 - 0xC000 && i != 0xFFFC - 0xC000 && i != 0xFFFD - 0xC000 &&
                  !(i >= 0xFA62 - 0xC000 && i < 0xFA6A - 0xC000);
    CHECK_EQ(differ, size_t{0});                               // the IIe ROM, in bank E
    CHECK_EQ(bus.peek_physical(0xEFFFC), 0x62);
    size_t left = 0;
    for (uint32_t a = 0; a < 0x40000; a += 0x101)
        left += bus.peek_physical(a) != 0 && a != 0x00300 && (a < 0x00100 || a >= 0x00200); // (not its store, nor the stack)
    CHECK_EQ(left, size_t{0});                                 // banks 0-3: all zero
    CHECK_EQ(bus.peek_physical(0x3FFFF), 0);
    CHECK_EQ(bus.peek_physical(0x4D000), 0);                   // the second $D000 bank, main...
    CHECK_EQ(bus.peek_physical(0x5DFFF), 0);                   // ...and auxiliary
    CHECK_EQ(bus.peek_physical(0x41234), 0x5A);                // the rest of bank 4 untouched
    CHECK_EQ(bus.peek_physical(0xFC800), static_cast<uint8_t>(flash[0x7C800])); // the BIOS's own copy kept
    // MODE: Apple II mode, a /RESET pulse, and the CPU running the IIe ROM's reset code in bank E
    REQUIRE(b.machine->find_as<BackplaneBridge>("northbridge") != nullptr);
    CHECK(b.machine->find_as<BackplaneBridge>("northbridge")->apple());
    CHECK_EQ(b.cpu->regs().pc, 0xFA67);                        // its JMP *
    CHECK_EQ(bus.peek_physical(0x00300), 0xA5);                // its store, to bank 0
}

TEST(the_cartridge_takes_16_kb_or_two_halves_and_a_4_or_8_kb_character_rom)
{
    std::string text = read_file(EMU_MACHINE_DIR "/backplane.machine");
    std::string error;
    const std::string rom = fake_iie_rom("test_cart.rom");
    {
        std::ofstream("test_cd.rom", std::ios::binary) << read_file(rom).substr(0, 0x2000);
        std::ofstream("test_ef.rom", std::ios::binary) << read_file(rom).substr(0x2000);
        std::ofstream("test_chars.rom", std::ios::binary) << std::string(0x1000, '\x11');
        std::ofstream("test_bad.rom", std::ios::binary) << std::string(0x3000, '\0');
    }
    auto build = [&](std::vector<std::string> parts, std::string chars = "") {
        MachineOptions opt;
        opt.rom_override = EMU_BIOS_ROM;
        opt.apple_rom = std::move(parts);
        opt.apple_chars = std::move(chars);
        error.clear();
        return Machine::build(text, "backplane.machine", opt, error);
    };
    auto whole = build({rom});
    REQUIRE(whole != nullptr);
    auto halves = build({"test_cd.rom", "test_ef.rom"}, "test_chars.rom");
    REQUIRE(halves != nullptr);
    size_t differ = 0;
    Device *a = whole->find("rom_b"), *c = halves->find("rom_b");
    REQUIRE(a != nullptr && c != nullptr);
    for (uint32_t i = 0; i < 0x4000; ++i)
        differ += a->peek(i) != c->peek(i);
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(c->peek(0x4000), 0x11);                           // the character ROM at $4000
    CHECK_EQ(a->peek(0x4000), 0xFF);                           // erased without one
    CHECK_EQ(a->peek(0x7FFFF), 0xFF);
    CHECK(build({"test_bad.rom"}) == nullptr);
    CHECK(error.find("want 16 KB") != std::string::npos);
    CHECK(build({rom}, "test_bad.rom") == nullptr);
    CHECK(error.find("4 or 8 KB") != std::string::npos);
    CHECK(build({"no_such.rom"}) == nullptr);
    CHECK(error.find("no_such.rom") != std::string::npos);
    const size_t at = text.find("\nrom   80000");
    REQUIRE(at != std::string::npos);
    text.erase(at + 1, text.find('\n', at + 1) - at);
    CHECK(build({rom}) == nullptr);
    CHECK(error.find("rom_b") != std::string::npos);
    for (const char *f : {"test_cart.rom", "test_cd.rom", "test_ef.rom", "test_chars.rom", "test_bad.rom"})
        std::remove(f);
}

TEST(c806_puts_the_character_rom_in_video_memory_or_makes_one_from_the_vga_font)
{
    const std::string rom = fake_iie_rom("test_chars_iie.rom");
    {
        std::string chars(0x1000, '\0');
        for (size_t i = 0; i < chars.size(); ++i)
            chars[i] = static_cast<char>(i * 3 + 1);
        std::ofstream("test_chars.chr", std::ios::binary) << chars;
    }
    Board with(false, true, {rom}, "test_chars.chr");
    Board without(false, true, {rom});
    std::remove("test_chars.chr");
    std::remove(rom.c_str());
    REQUIRE(with.ok() && without.ok());
    for (Board *b : {&with, &without})
    {
        b->reset();
        b->run(BOOT);
        b->run(2 * BOOT, "C806R\r");
    }
    size_t differ = 0;
    for (uint32_t i = 0; i < 0x800; ++i)
        differ += with.machine->bus().peek_physical(0xBE000 + i) != static_cast<uint8_t>(i * 3 + 1);
    CHECK_EQ(differ, size_t{0});                                // the cartridge's, its first 2 KB
    // Without one: each glyph from the VGA font (video memory unit 1), 16 rows folded to 8,
    // 7 dots reversed, normal codes inverted. Check A both ways: $C1 normal, $01 inverse.
    Decoder &bus = without.machine->bus();
    size_t wrong = 0;
    for (unsigned r = 0; r < 8; ++r)
    {
        const uint8_t vga = bus.peek_physical(0x80400 + 'A' * 16 + 2 * r) | bus.peek_physical(0x80400 + 'A' * 16 + 2 * r + 1);
        uint8_t dots = 0;
        for (unsigned b = 0; b < 7; ++b)
            dots = static_cast<uint8_t>(dots | ((vga >> (7 - b)) & 1) << b);
        wrong += bus.peek_physical(0xBE000 + 0xC1 * 8 + r) != static_cast<uint8_t>(~dots);
        wrong += bus.peek_physical(0xBE000 + 0x01 * 8 + r) != dots;
    }
    CHECK_EQ(wrong, size_t{0});
    size_t blank = 0;
    for (unsigned r = 0; r < 8; ++r)
        blank += bus.peek_physical(0xBE000 + 0xC1 * 8 + r) == 0xFF;
    CHECK(blank < 8);                                           // something to see: A is not empty
}

TEST(break_during_the_demo_puts_the_windows_and_the_screen_back)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(20 * FRAME, "C803R\r");
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card->peek(VideoCard::CTRL) == 0x0F);
    b.cpu->nmi();                                // Pause/Break, mid-frame
    CHECK(has(b.run(BOOT), "\\"));
    const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
    CHECK_EQ(card->peek(VideoCard::CTRL), 0x03);
    CHECK_EQ(card->peek(VideoCard::PALBASE), 0);
    CHECK_EQ(card->peek(VideoCard::SPRBASE), 0);
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), 0x80);
    CHECK_EQ(card->peek(l0 + VideoCard::MAPBASE), 8);
    CHECK_EQ(card->peek(l0 + VideoCard::TILEBASE), 1);
    CHECK_EQ(card->peek(l0 + VideoCard::HSCROLL), 0);
    CHECK_EQ(card->peek(l1 + VideoCard::CONFIG), 0);
    CHECK_EQ(b.bridge().window(0), 0x00);
    CHECK_EQ(b.bridge().window(1), 0x01);
    CHECK_EQ(b.bridge().window(2), 0x02);
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F"));
    CHECK(b.find_row("C680: 00 01 02 7F 7F") >= 0); // on the screen too
}

TEST(break_inside_vputc_puts_w2_back)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    bool caught = false;                         // a key's echo, with W2 on video memory
    bool offered = false;
    for (int i = 0; i < 2'000'000 && !caught; ++i)
    {
        b.cpu->step();
        b.console->service();
        uint8_t ch = 0;
        while (b.console->output(ch))
            ;
        if (!offered)
            offered = b.console->key('A');
        caught = b.bridge().window(2) == 32;
    }
    REQUIRE(caught);
    b.cpu->nmi();
    CHECK(has(b.run(BOOT), "\\"));
    CHECK_EQ(b.bridge().window(2), 0x02);        // the monitor's echo found it put back
    CHECK(has(b.run(BOOT, "C682\r"), "C682: 02"));
}

TEST(brk_enters_the_monitor)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "300: 00 00\r");
    CHECK(has(b.run(BOOT, "300R\r"), "0300: 00\\\r"));
    CHECK(has(b.run(BOOT, "C680.C682\r"), "C680: 00 01 02"));
}

TEST(c800_puts_the_windows_and_the_screen_back_and_copies_the_flash_again)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const std::string lbl = read_file(EMU_BIOS_ROM ".lbl");
    const unsigned greeting = label(lbl, "GREETING");
    REQUIRE(greeting >= 0xC800);
    Device *ram_d = b.machine->find("ram_d");
    REQUIRE(ram_d != nullptr);
    ram_d->poke(0x3C000 + greeting + 1 - 0xC000, 'X'); // the RAM copy spoilt: "XACKPLANE"
    b.run(BOOT, "C681: 05\r");
    b.run(BOOT, "C483: 07\r");                   // layer 0 scrolled
    CHECK(has(b.run(BOOT, "C800R\r"), GREETING.c_str()));
    CHECK_EQ(b.bridge().window(1), 0x01);
    CHECK_EQ(b.bridge().window(3), 0x7F);
    CHECK_EQ(b.machine->video_card()->peek(uint8_t{VideoCard::LAYER0} + VideoCard::HSCROLL), 0);
}

TEST(the_cursor_stays_on_the_screen_whatever_is_in_its_zero_page)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "E3: 28\r");                     // VROW 40, past the bottom
    CHECK(b.machine->bus().peek(0xE3) < 30);
    b.run(BOOT, "C680\r");
    CHECK(b.find_row("C680: 00") >= 0);
}

TEST(the_screen_scrolls_whole_with_a_burst_limit)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C441: 11\r");                   // DMA: a cycle back to the CPU every 2 bytes
    std::string keys;
    for (int i = 0; i < 12; ++i)
        keys += "FFFC.FFFD\r";
    b.run(keys.size() * 25'000 + BOOT, keys);
    CHECK_EQ(b.row(29), std::string("_"));
    CHECK_EQ(b.row(28), std::string("FFFC: 00 C8"));
    CHECK_EQ(b.row(27), std::string(""));
    CHECK_EQ(b.row(26), std::string("FFFC.FFFD"));
    CHECK_EQ(b.row(25), std::string("FFFC: 00 C8"));
    size_t odd = 0;
    for (unsigned r = 0; r < 30; ++r)
        for (unsigned c = 0; c < 80; ++c)
            odd += b.attr(r, c) != (r == 29 && c == 0 ? 0x87 : 0x07);
    CHECK_EQ(odd, size_t{0});
}

TEST(the_demo_balls_bounce_off_the_bottom)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(20 * FRAME, "C803R\r");
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    auto y = [&](unsigned n) { return int{b.machine->bus().peek_physical(0x89000 + n * 8 + 4)}; };
    std::vector<int> last;
    for (unsigned n = 0; n < 12; ++n)
        last.push_back(y(n));
    int lowest = 0, jump = 0;
    for (int f = 0; f < 60; ++f)
    {
        b.run(FRAME);
        for (unsigned n = 0; n < 12; ++n)
        {
            lowest = std::max(lowest, y(n));
            jump = std::max(jump, std::abs(y(n) - last[n]));
            last[n] = y(n);
        }
    }
    CHECK(lowest >= 224);                        // some reached the bottom...
    CHECK(jump <= 2);                            // ...and none jumped to the top
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
