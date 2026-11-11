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
 * the video demo (demo.s, C803R) on a revision 2 card (hw/video-rev2.md,
 * proposed): tiles, sprites, scroll, fade and the ripple, loaded through the
 * card's port, and back to the monitor's screen as it was on a key, the
 * BIOS's and Apple II mode's card memory untouched; on revision 1 it goes
 * straight back. And the USB keyboard (input.s): the monitor takes keys
 * from it, Pause/Break breaks in, and a USB key ends the demo. And Apple
 * II mode's way in (apple.s, C806R), with a stand-in for the owner's IIe
 * ROM: these tests never need Apple's. And the BIOS's recovery: Break (in
 * the demo, or inside the screen code) and BRK back into the monitor with the windows and the screen as the
 * BIOS keeps them, C800R from the flash again (or the RAM image after a
 * /BOOTSEL boot), the cursor kept on the screen whatever its zero page says,
 * and scrolling whole under a DMA burst limit. Break also stops the DMA (a
 * scroll mid-copy too), quiets the cards' interrupts (the VIA's, and a front-panel key
 * already pending, too) and finds the screen
 * from the ID bytes; the BIOS's copies leave CONTROL as they found it, give
 * up with no controller, and run with the master enable off (the demo's
 * too); C806R stops a program's DMA before its fills. And the boot splash
 * (splash.s): the card's logo and the sound card's tune start on one vertical
 * blank, the boot carries on behind them, and after 5 s the screen and the
 * SIDs are back; Break and the demo stop it early. On a revision 2 sound
 * card Break and the demo's end stop a PCM stream (its FIFO emptied, paused
 * or not) and the ES5503 (DOCCTL's other bits kept, an interrupt left pending
 * on the 6510's route) too, a $82 clears, and the audio goes silent; on
 * revision 1 Break writes nothing at $48-$63.
 *
 * The ROM is assembled by this build with ca65/ld65, as backplane/bios's
 * Makefile does; without cc65 the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "console_card.h"
#include "machine.h"
#include "bus_cycle.h"
#include "es5503.h"
#include "sound_card.h"
#include "video_card.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
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

    /// The video card these run on: the BIOS is built for revision 2 (hw/video-rev2.md), and backplane.machine
    /// still says rev=1 until the BIOS, OODOS and the demo move together.
    constexpr unsigned VIDEO_REVISION = 2;

    /// backplane.machine with the BIOS, its console and CPU, reset and ready.
    struct Board
    {
        /// Board(Board::Rev2{}): the video card at revision 2 (hw/video-rev2.md), as the demo needs it.
        struct Rev2
        {
        };

        explicit Board(bool bootsel = false, bool video = true, std::vector<std::string> apple_rom = {},
                       std::string apple_chars = "", std::vector<std::string> without = {},
                       unsigned video_revision = 0)
        {
            std::string text = read_file(EMU_MACHINE_DIR "/backplane.machine");
            if (!video)
                without.push_back("video");
            for (const std::string &card : without) // the machine without these cards
            {
                const size_t at = text.find("\n" + card + " ");
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
            // The clock starts on a second, not at the host's time: its ticks then fall at the same cycles
            // every run (a whole second of cycles apart), so a test that watches for one, or for none, can't
            // be caught by a tick landing at a moment of the host's choosing.
            opt.rtc_time = Ds3231::Time{2026, 10, 26, 12, 0, 0, 2}; // a Monday
            opt.apple_rom = std::move(apple_rom);
            opt.apple_chars = std::move(apple_chars);
            opt.video_revision = video_revision ? video_revision : VIDEO_REVISION; // 1 only where a test asks
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

        explicit Board(Rev2) : Board(false, true, {}, "", {}, 2) {}

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
                if (on_step)
                    on_step();
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

        /// Called by run() before each instruction, if set.
        std::function<void()> on_step;
        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
    };

    constexpr uint64_t BOOT = 2'000'000; // cycles: the copy, the splash's samples (revision 2: ~0.9M), then the banner at ~3,840 a character
    constexpr uint64_t SPLASH_TIME = 36'500'000; // cycles: the boot splash's 5 s (300 frames), and a little
    const std::string GREETING = "\rBACKPLANE BIOS 0.13\r\\\r";

    /// A label's address from the ld65 -Ln file ("al 00C862 .WOZMON"), or 0.
    unsigned label(const std::string &lbl, const std::string &name)
    {
        const size_t at = lbl.find(" ." + name + "\n");
        if (at == std::string::npos || at < 6)
            return 0;
        return static_cast<unsigned>(std::stoul(lbl.substr(at - 6, 6), nullptr, 16));
    }

    /// A revision 2 card's two-byte register at `offset`, low byte first.
    unsigned reg16(const VideoCard &card, unsigned offset)
    {
        return card.peek(offset) | unsigned{card.peek(offset + 1)} << 8;
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
    CHECK_EQ(reg16(*card, l0 + VideoCard::Rev2::MAPBASE), 0x0020); // card $2000, the old unit 8
    CHECK_EQ(reg16(*card, l0 + VideoCard::Rev2::TILEBASE), 0x0004); // card $0400, the old unit 1
    CHECK_EQ(b.row(0), std::string(""));
    CHECK_EQ(b.row(1), std::string("BACKPLANE BIOS 0.13"));
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
    // The boot splash (splash.s) covers it for 5 s, then the card shows its layers by itself.
    CHECK(card->splash_frame() >= 0);
    b.run(SPLASH_TIME);
    CHECK_EQ(card->splash_frame(), -1);
    CHECK_EQ(card->peek(VideoCard::SPLASH), 0x00);
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
    CHECK_EQ(b.find_row("BACKPLANE BIOS 0.13"), -1); // scrolled off the top
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

    /// The loudest sample the card's mix has given since the last call (it keeps up to a second).
    double peak(SoundCard &sound)
    {
        float buf[2 * 4'096];
        double most = 0;
        for (size_t n; (n = sound.take_audio(buf, 4'096)) != 0;)
            for (size_t i = 0; i < 2 * n; ++i)
                most = std::max(most, static_cast<double>(std::fabs(buf[i])));
        return most;
    }

    /// What the card shows now, as distinct colours.
    size_t colours(const VideoCard &card)
    {
        std::vector<uint32_t> seen;
        for (size_t i = 0; i < size_t{640} * 480; i += 7)
            if (std::find(seen.begin(), seen.end(), card.frame()[i]) == seen.end())
                seen.push_back(card.frame()[i]);
        return seen.size();
    }

    // The demo (demo.s) on a revision 2 card: where it puts things in card memory, the programs' region.
    constexpr uint32_t DEMO_PAL = 0x040000, DEMO_FONT = 0x040400, DEMO_TEXT1 = 0x041400, DEMO_TEXT2 = 0x041E00;
    constexpr uint32_t DEMO_TILES = 0x043000, DEMO_MAP = 0x044000, DEMO_BALL = 0x045000, DEMO_SPR = 0x045100;
    constexpr uint32_t DEMO_RIPPLE = 0x050000, RIPPLE_SIZE = 0xF000; // 16 bitmaps of 256 x 240
    constexpr uint8_t CONFIG_TILES = 0x22, CONFIG_RIPPLE = 0x03;     // a 64-wide tile map; bitmap 256

    const std::string &bios_labels()
    {
        static const std::string lbl = read_file(EMU_BIOS_ROM ".lbl");
        return lbl;
    }

    /// Flash bytes of the demo's block (29) from CPU address `at` (as W1 shows it), `n` of them.
    std::string demo_flash(unsigned at, size_t n)
    {
        static const std::string flash = read_file(EMU_BIOS_ROM);
        return flash.substr(29 * 0x4000 + (at - 0x4000), n);
    }

    std::string card_bytes(const VideoCard &card, uint32_t at, size_t n)
    {
        std::string out;
        for (size_t i = 0; i < n; ++i)
            out += static_cast<char>(card.card_peek(static_cast<uint32_t>(at + i)));
        return out;
    }

    /// Start the demo by C803R and wait for its first frame. `at_entry` runs as the CPU reaches DEMO_MAIN. Typed
    /// on the serial line, or the USB keyboard if `usb`.
    void start_demo(Board &b, bool usb = false, const std::function<void()> &at_entry = {})
    {
        const unsigned main = label(bios_labels(), "DEMO_MAIN");
        bool entered = false;
        b.on_step = [&] {
            if (!entered && b.cpu->regs().pc == main && b.bridge().window(1) == 0xDD)
            {
                entered = true;
                if (at_entry)
                    at_entry();
            }
        };
        auto type = [&](const std::string &keys) { return usb ? b.type_usb(keys) : b.run(4 * FRAME, keys); };
        CHECK(type("C803R\r").find("C803: 4C\\") == std::string::npos); // C803 took the card
        const VideoCard &card = *b.machine->video_card();
        for (int f = 0; f < 60 && card.peek(VideoCard::CTRL) != 0x0F; ++f)
            b.run(FRAME);
        b.on_step = nullptr;
        CHECK(entered);
    }

    /// The BIOS's text screen's registers on revision 2, as Break and the demo's end leave them (VSCREEN: the
    /// card memory layout of hw/video-rev2.md, the font at $000400 and the text at $002000).
    void check_text_screen_registers(const VideoCard &card)
    {
        const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
        using R2 = VideoCard::Rev2;
        CHECK_EQ(card.peek(VideoCard::CTRL), 0x03);
        CHECK_EQ(reg16(card, R2::PALBASE), 0u);
        CHECK_EQ(reg16(card, R2::SPRBASE), 0u);
        CHECK_EQ(card.peek(l0 + VideoCard::CONFIG), 0x80);
        CHECK_EQ(reg16(card, l0 + R2::MAPBASE), 0x0020u);
        CHECK_EQ(reg16(card, l0 + R2::TILEBASE), 0x0004u);
        CHECK_EQ(reg16(card, l0 + R2::HSCROLL), 0u);
        CHECK_EQ(card.peek(l1 + VideoCard::CONFIG), 0);
    }
} // namespace

TEST(the_boot_splash_plays_with_its_sound_then_hands_back_the_screen_and_the_sids)
{
    for (const unsigned rev : {2u, 1u}) // the sound card at either revision: the BIOS takes it by its type
    {
        std::printf("  sound card revision %u\n", rev);
        Board b;
        REQUIRE(b.ok());
        const VideoCard *card = b.machine->video_card();
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(card != nullptr);
        REQUIRE(sound != nullptr);
        sound->set_revision(rev);
        sound->set_audio_rate(48'000); // the mix, to hear the ES5503 in it
        REQUIRE(sound->peek(SoundCard::ID_REG) == (0xA0 | rev));
        b.reset();
        // The card's 6510 starts on the vertical blank the splash starts on: tune frame n, picture frame n.
        bool started_together = false;
        for (unsigned i = 0; i < 2'000 && card->splash_frame() < 0; ++i)
            b.run(1'000);
        started_together = card->splash_frame() == 0 && (sound->ctrl() & SoundCard::CTRL_RUN);
        CHECK(started_together);
        CHECK_EQ(sound->ctrl() & SoundCard::CTRL_NTSC, SoundCard::CTRL_NTSC);
        // The player at $1000, the reset vector at its boot entry: the ES5503's (splash/docsnd.bin) on
        // revision 2, with the samples (splash/wave.bin, from flash blocks 24-28) in wave RAM and the chip
        // running for the 6510; the SIDs' (splash/bootsnd.bin) on revision 1.
        const std::string tune = read_file(rev == 2 ? EMU_BIOS_DIR_SPLASH "/docsnd.bin" : EMU_BIOS_DIR_SPLASH "/bootsnd.bin");
        REQUIRE(!tune.empty());
        size_t differ = 0;
        for (size_t i = 0; i < tune.size(); ++i)
            differ += sound->ram()[0x1000 + i] != static_cast<uint8_t>(tune[i]);
        CHECK_EQ(differ, size_t{0});
        CHECK_EQ(sound->ram()[0xFFFC] | sound->ram()[0xFFFD] << 8, 0x1006);
        if (rev == 2)
        {
            const std::string wave = read_file(EMU_BIOS_DIR_SPLASH "/wave.bin");
            REQUIRE(wave.size() == 0x14000);
            size_t wrong = 0;
            for (size_t i = 0; i < wave.size(); ++i)
                wrong += sound->doc().ram()[i] != static_cast<uint8_t>(wave[i]);
            CHECK_EQ(wrong, size_t{0});
            CHECK(sound->peek(SoundCard::DOCCTL) & SoundCard::DOCCTL_RUN);
        }
        CHECK_EQ(b.run(BOOT), GREETING); // the boot doesn't wait for it
        b.run(FRAME * 60);
        CHECK(card->splash_frame() > 60);
        CHECK(sound->ctrl() & SoundCard::CTRL_RUN);
        if (rev == 2)
        {
            CHECK_EQ(sound->doc().peek(0xE1), 23 << 1); // 24 oscillators
            CHECK_EQ(sound->doc().peek(0xA0) & 0x01, 0); // O1's engine plays, left ...
            CHECK_EQ(sound->doc().peek(0xA1) & 0x01, 0); // ... and right
            CHECK(sound->doc().peek(0x40) || sound->doc().peek(0x41));
            const double engine = peak(*sound); // the last second's mix: the engine, heard
            std::printf("  the engine's peak %.3f\n", engine);
            CHECK(engine > 0.01);
            // The landing (docsnd.s's IMPACT, frame 118): oscillators 0-19 on the thunk's tables, heard.
            while (card->splash_frame() >= 0 && card->splash_frame() < 118)
                b.run(FRAME);
            peak(*sound); // what came before it, dropped
            b.run(FRAME * 6);
            CHECK_EQ(sound->doc().peek(0xC0), 0x37); // T_THUNK_L, left ...
            CHECK_EQ(sound->doc().peek(0xC1), 0x77); // ... T_THUNK_R, right
            CHECK_EQ(sound->doc().peek(0x80), 0xC0); // P_THUNK_L
            const double thunk = peak(*sound);
            std::printf("  the landing's peak %.3f\n", thunk);
            CHECK(thunk > 0.01);
        }
        else
            CHECK(sound->sid(0)[0x18] & 0x0F); // engines: SID A's volume up...
        if (rev == 1)
            continue; // revision 1 took the card and plays the tune; how the splash ends doesn't depend on the
                      // revision, so it is watched once, on revision 2 (5 s of machine time, ~45 s under ASan)
        b.run(SPLASH_TIME);
        CHECK_EQ(card->splash_frame(), -1); // ...and both over: the monitor's screen
        CHECK_EQ(sound->ctrl() & SoundCard::CTRL_RUN, 0); // the player stopped the card ($DE02) ...
        CHECK_EQ(sound->peek(SoundCard::DOCCTL) & SoundCard::DOCCTL_RUN, 0); // ... and the chip (its bit 0)
        unsigned running = 0;
        for (unsigned n = 0; n < 32; ++n)
            running += !(sound->doc().peek(static_cast<uint8_t>(0xA0 + n)) & 0x01);
        CHECK_EQ(running, 0u); // every oscillator halted
        CHECK_EQ(sound->sid(0)[0x18] & 0x0F, 0);
        CHECK_EQ(sound->sid(1)[0x18] & 0x0F, 0);
        CHECK_EQ(b.row(1), std::string("BACKPLANE BIOS 0.13"));
    }
}

TEST(c800_after_an_es5503_program_halts_every_oscillator_before_the_splash_plays)
{
    // A program left all 32 of the ES5503's oscillators playing and was broken out of: QUIET only froze the chip,
    // so its registers are as it left them. C800R's splash must halt them all before the 6510 has the chip, the
    // 8 its player never touches (24-31) included, or they would play on over it, reading the fresh wave RAM.
    Board b;
    REQUIRE(b.ok());
    SoundCard *sound = b.machine->sound_card();
    REQUIRE(sound != nullptr);
    sound->set_revision(2);
    b.reset();
    b.run(BOOT);
    b.run(SPLASH_TIME); // the boot's own splash over
    REQUIRE((sound->peek(SoundCard::DOCCTL) & SoundCard::DOCCTL_RUN) == 0);
    Es5503 &doc = sound->doc();
    doc.write(0xE1, 31 << 1); // 32 oscillators
    for (uint8_t n = 0; n < 32; ++n)
    {
        doc.write(static_cast<uint8_t>(0x00 + n), 0x40); // a pitch
        doc.write(static_cast<uint8_t>(0x40 + n), 255);  // loud
        doc.write(static_cast<uint8_t>(0xA0 + n), 0x00); // free run, not halted
    }
    CHECK(has(b.run(BOOT, "C800R\r"), GREETING.c_str()));
    unsigned running = 0;
    for (uint8_t n = 24; n < 32; ++n)
        running += !(doc.peek(static_cast<uint8_t>(0xA0 + n)) & 0x01);
    CHECK_EQ(running, 0u); // the ones the player leaves alone: halted by the BIOS
}

TEST(break_during_the_boot_splash_stops_it_and_its_sound)
{
    for (const unsigned rev : {2u, 1u}) // QUIET takes the sound card at either revision
    {
        std::printf("  sound card revision %u\n", rev);
        Board b;
        REQUIRE(b.ok());
        const VideoCard *card = b.machine->video_card();
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(card != nullptr);
        REQUIRE(sound != nullptr);
        sound->set_revision(rev);
        b.reset();
        b.run(BOOT);
        CHECK(card->splash_frame() >= 0);
        CHECK(sound->ctrl() & SoundCard::CTRL_RUN); // the splash took the card
        b.cpu->nmi();
        b.run(FRAME * 2);
        CHECK_EQ(card->splash_frame(), -1);
        CHECK_EQ(sound->ctrl() & SoundCard::CTRL_RUN, 0);
        CHECK_EQ(sound->sid(0)[0x18], 0); // and QUIET silenced the SIDs
        if (rev == 2)
            CHECK_EQ(sound->peek(SoundCard::DOCCTL) & SoundCard::DOCCTL_RUN, 0); // and froze the ES5503
    }
}

namespace
{
    /// What revision 2's QUIET has to stop: the PCM stream or the ES5503 (one at a time: each refuses RUN while the
    /// other runs).
    enum class Noise
    {
        pcm,
        doc,
    };

    const char *name(Noise n) { return n == Noise::pcm ? "the PCM stream" : "the ES5503"; }

    /// DOCCTL's bits besides RUN, which QUIET keeps.
    constexpr uint8_t DOC_KEPT = SoundCard::DOCCTL_IRQ_CARD | SoundCard::DOCCTL_AUTOINC | SoundCard::DOCCTL_RAM;

    /// Boot, then end the splash and its tune with a first Break rather than waiting out its 5 s (under ASan about
    /// 50 s of host time a boot): QUIET takes the splash off and stops the card's 6510, so the SIDs are the host's
    /// and the monitor is at its prompt. The Break a case tests comes after, so it is QUIET's second run.
    bool boot_past_splash(Board &b)
    {
        b.reset();
        b.run(BOOT);
        b.cpu->nmi();
        const bool prompt = has(b.run(BOOT), "\\");
        return prompt && b.machine->video_card()->splash_frame() == -1 &&
               !(b.machine->sound_card()->ctrl() & SoundCard::CTRL_RUN);
    }

    /// Start `n` on the card from the host's side, loud: 8-bit mono PCM at 8 kHz, 64 KB of a full-scale square (8
    /// seconds), or the ES5503's oscillator 0 looping a full-scale square in wave RAM, with IE (each loop makes it
    /// pending) and DOCCTL's other bits set: the interrupt routed to the 6510, AUTOINC and RAM. The audio at 48 kHz
    /// and the test tap on, both emptied.
    void start_noise(SoundCard &sound, Noise n)
    {
        sound.set_audio_rate(48'000);
        if (n == Noise::pcm)
        {
            sound.write(SoundCard::RATE, static_cast<uint8_t>(8'000));
            sound.write(SoundCard::RATE + 1, static_cast<uint8_t>(8'000 >> 8));
            sound.write(SoundCard::PCMFMT, 0);
            for (unsigned i = 0; i < 0x10000; ++i)
                sound.write(SoundCard::PCMDATA, i & 8 ? 0xFF : 0x00);
            sound.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
        }
        else
        {
            sound.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC);
            sound.write(SoundCard::DOCADDR, 0);
            sound.write(SoundCard::DOCADDR + 1, 0);
            sound.write(SoundCard::DOCBANK, 0);
            for (unsigned i = 0; i < 256; ++i)
                sound.write(SoundCard::DOCDATA, i < 128 ? 0xFF : 0x01);
            sound.write(SoundCard::DOCCTL, 0); // the registers, one at a time
            const auto reg = [&](uint8_t r, uint8_t v) {
                sound.write(SoundCard::DOCADDR, r);
                sound.write(SoundCard::DOCDATA, v);
            };
            reg(Es5503::FREQ_LO, 0x00);
            reg(Es5503::FREQ_HI, 0x02);
            reg(Es5503::VOLUME, 0xFF);
            reg(Es5503::POINTER, 0);
            reg(Es5503::TABLE, 0); // 256 bytes at 0
            reg(Es5503::CONTROL, Es5503::MODE_FREE << 1 | Es5503::CONTROL_IE); // running, looping, channel 0
            sound.write(SoundCard::DOCCTL, DOC_KEPT | SoundCard::DOCCTL_RUN);
        }
        sound.set_tap(true);
    }

    /// The loudest sample the card has made since the last call, left or right.
    float loudest(SoundCard &sound)
    {
        std::vector<float> buf(2 * 4'096);
        float peak = 0;
        for (size_t got; (got = sound.take_audio(buf.data(), 4'096)) != 0;)
            for (size_t i = 0; i < 2 * got; ++i)
                peak = std::max(peak, std::fabs(buf[i]));
        return peak;
    }

    /// Whether `n` runs: its RUN bit, and (PCM) the stream's STATE or (the ES5503) its oscillator 0 moving.
    bool runs(SoundCard &sound, Noise n)
    {
        if (n == Noise::pcm)
            return (sound.peek(SoundCard::PCMCTL) & SoundCard::PCMCTL_RUN) ||
                   sound.peek(SoundCard::STATE) != SoundCard::STATE_IDLE;
        return sound.peek(SoundCard::DOCCTL) & SoundCard::DOCCTL_RUN;
    }

    /// After a QUIET: `n` stopped, the FIFO emptied (FLUSH), DOCCTL's RUN alone cleared, nothing more on the card's
    /// bus (the test tap), and the audio silent from a frame on (the last of what was made before it, and the SIDs'
    /// DC filter settling, given that frame). The ES5503's interrupt, pending on the 6510's route, stays pending,
    /// and the host's /IRQ stays high (IEN is 0). The 6510's own /IRQ isn't checked here (the 6510 is stopped):
    /// test_sound_doc's the_6510_isnt_held_by_a_paused_chips_interrupt tests that route.
    void check_silent(Board &b, SoundCard &sound, Noise n)
    {
        CHECK(!runs(sound, n));
        CHECK_EQ(sound.fifo_bytes(), size_t{0});
        CHECK_EQ(sound.peek(SoundCard::PCMCTL), 0);
        if (n == Noise::doc)
        {
            CHECK_EQ(sound.peek(SoundCard::DOCCTL), DOC_KEPT);
            CHECK(sound.doc().irq());
            CHECK(!b.machine->bus().irq());
        }
        b.run(FRAME);
        sound.take_tap();
        loudest(sound);
        const uint32_t acc = sound.doc().accumulator(0);
        b.run(10 * FRAME);
        const std::vector<int16_t> tap = sound.take_tap();
        const float peak = loudest(sound);
        std::printf("    after: %zu bus frames, the loudest sample %.6f\n", tap.size() / 2, static_cast<double>(peak));
        CHECK(tap.empty());
        CHECK(peak < 1e-3f);
        CHECK_EQ(sound.doc().accumulator(0), acc); // the chip frozen where it stopped
        CHECK_EQ(sound.peek(SoundCard::IEN), 0);
    }
} // namespace

TEST(break_stops_the_pcm_stream_and_the_es5503_on_revision_2)
{
    // QUIET on a revision 2 card ($A2) also clears PCMCTL RUN and DOCCTL RUN: Break silences everything, a stream
    // or the ES5503 a program left playing as well as the SIDs.
    for (const Noise n : {Noise::pcm, Noise::doc})
    {
        std::printf("  %s\n", name(n));
        Board b;
        REQUIRE(b.ok());
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(sound != nullptr);
        REQUIRE(sound->peek(SoundCard::ID_REG) == SoundCard::ID);
        REQUIRE(boot_past_splash(b)); // the splash and its tune over: the SIDs the host's
        start_noise(*sound, n);
        b.run(10 * FRAME);
        const float before = loudest(*sound);
        std::printf("    before: the loudest sample %.6f\n", static_cast<double>(before));
        CHECK(runs(*sound, n));
        CHECK(n != Noise::pcm || sound->peek(SoundCard::STATE) == SoundCard::STATE_PLAYING);
        CHECK(before > 0.03f); // loud: the check below can tell
        b.cpu->nmi();
        b.run(FRAME);
        check_silent(b, *sound, n);
        CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F")); // and the monitor works
    }
}

TEST(the_demos_end_stops_the_pcm_stream_and_the_es5503)
{
    // The demo ends through BREAK, so through QUIET too.
    for (const Noise n : {Noise::pcm, Noise::doc})
    {
        std::printf("  %s\n", name(n));
        Board b{Board::Rev2{}};
        REQUIRE(b.ok());
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(sound != nullptr);
        REQUIRE(boot_past_splash(b));
        start_demo(b);
        REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
        start_noise(*sound, n);
        b.run(5 * FRAME);
        CHECK(runs(*sound, n));
        CHECK(loudest(*sound) > 0.03f);
        CHECK(has(b.run(10 * FRAME, "X"), "\\")); // the monitor's prompt again
        check_silent(b, *sound, n);
    }
}

TEST(break_stops_a_paused_stream_and_clears_a_refused_run)
{
    // A stream paused stops and empties as a playing one does; a RUN refused ($82 in STATE) reads 0 after Break.
    for (const bool paused : {true, false})
    {
        std::printf("  %s\n", paused ? "a stream paused" : "PCMCTL RUN refused while the ES5503 plays");
        Board b;
        REQUIRE(b.ok());
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(sound != nullptr);
        REQUIRE(boot_past_splash(b));
        start_noise(*sound, paused ? Noise::pcm : Noise::doc);
        b.run(5 * FRAME);
        if (paused)
            sound->write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
        else
            sound->write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
        b.run(FRAME);
        CHECK_EQ(sound->peek(SoundCard::STATE), paused ? SoundCard::STATE_PAUSED : SoundCard::STATE_BOTH);
        b.cpu->nmi();
        b.run(FRAME);
        CHECK_EQ(sound->peek(SoundCard::STATE), SoundCard::STATE_IDLE);
        check_silent(b, *sound, paused ? Noise::pcm : Noise::doc);
    }
}

namespace
{
    /// Every host write to the sound card's revision 2 registers ($C548-$C563), and to its SIDs ($C500-$C53C).
    struct SoundWrites : CycleObserver
    {
        unsigned rev2 = 0, sids = 0;
        void on_cycle(const BusCycle &c) override
        {
            if (!c.write || c.dma)
                return;
            if (c.addr >= 0xC500 + SoundCard::PCMCTL && c.addr <= 0xC500 + SoundCard::DRQSEL)
                ++rev2;
            else if (c.addr >= 0xC500 && c.addr < 0xC500 + 0x3D)
                ++sids;
        }
    };
} // namespace

TEST(break_leaves_a_revision_1_cards_other_offsets_alone)
{
    // Revision 1 ($A1) has nothing at $48-$63: QUIET takes its CTRL, IEN and SIDs as before, and writes there
    // nothing. Revision 2 for comparison: the same Break writes PCMCTL and DOCCTL, once each. (The emulator models
    // revisions 1 and 2 only: set_revision() takes no 3.)
    for (const unsigned rev : {1u, 2u})
    {
        std::printf("  sound card revision %u\n", rev);
        Board b;
        REQUIRE(b.ok());
        SoundCard *sound = b.machine->sound_card();
        REQUIRE(sound != nullptr);
        sound->set_revision(rev);
        REQUIRE(sound->peek(SoundCard::ID_REG) == (0xA0 | rev));
        REQUIRE(boot_past_splash(b));
        SoundWrites seen;
        b.cpu->set_observer(&seen);
        b.cpu->nmi();
        b.run(FRAME);
        b.cpu->set_observer(nullptr);
        std::printf("    Break wrote the SIDs %u times, $C548-$C563 %u times\n", seen.sids, seen.rev2);
        CHECK_EQ(seen.sids, 0x3Du);              // QUIET ran, and took the card
        CHECK_EQ(seen.rev2, rev == 1 ? 0u : 2u);
        CHECK_EQ(sound->ctrl() & SoundCard::CTRL_RUN, 0);
    }
}

TEST(the_demo_runs_from_c803_tiles_sprites_scroll_and_fade)
{
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card != nullptr);
    start_demo(b);
    b.run(20 * FRAME);                           // past the fade
    using R2 = VideoCard::Rev2;
    const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
    CHECK_EQ(card->peek(VideoCard::CTRL), 0x0F); // display, both layers, sprites
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), CONFIG_TILES);
    CHECK_EQ(card->peek(l1 + VideoCard::CONFIG), 0x01); // the caption, text 40x30
    CHECK_EQ(reg16(*card, R2::PALBASE), DEMO_PAL / 256);
    CHECK_EQ(reg16(*card, R2::SPRBASE), DEMO_SPR / 256);
    CHECK_EQ(reg16(*card, l0 + R2::MAPBASE), DEMO_MAP / 256);
    CHECK_EQ(reg16(*card, l0 + R2::TILEBASE), DEMO_TILES / 256);
    CHECK_EQ(reg16(*card, l1 + R2::MAPBASE), DEMO_TEXT1 / 256);
    CHECK_EQ(reg16(*card, l1 + R2::TILEBASE), DEMO_FONT / 256);
    CHECK_EQ(b.bridge().window(1), 0xDD);        // flash block 29, read-only, at $4000
    // Loaded through the port: the graphics as flash has them, and the font from block 30.
    const std::string &lbl = bios_labels();
    CHECK(card_bytes(*card, DEMO_TILES, 512) == demo_flash(label(lbl, "TILES"), 512));
    CHECK(card_bytes(*card, DEMO_MAP, 4096) == demo_flash(label(lbl, "MAP"), 4096));
    CHECK(card_bytes(*card, DEMO_BALL, 128) == demo_flash(label(lbl, "BALL"), 128));
    CHECK(card_bytes(*card, DEMO_FONT, 4096) == read_file(EMU_BIOS_ROM).substr(30 * 0x4000, 4096));
    std::string caption;
    for (unsigned c = 1; c < 39; ++c)
        caption += static_cast<char>(card->card_peek(DEMO_TEXT1 + (1 * 40 + c) * 2));
    CHECK_EQ(caption, std::string("OO-1 VIDEO CARD 2: TILES, SPRITES, DMA"));
    // The palette faded all the way in: the caption's white is $FFF.
    CHECK_EQ(card->card_peek(DEMO_PAL + 2 * 15), 0xFF);
    CHECK_EQ(card->card_peek(DEMO_PAL + 2 * 15 + 1), 0x0F);
    CHECK(colours(*card) >= 12);                 // sky, hills, bricks, stars, balls, caption
    // The table the port's DMA keeps: 12 balls between the layers, the ball's image at unit $0450, and they move.
    auto ball0 = [&]() -> unsigned { return unsigned{card->card_peek(DEMO_SPR + 2)} | unsigned{card->card_peek(DEMO_SPR + 4)} << 16; };
    CHECK_EQ(card->card_peek(DEMO_SPR + 11 * 8 + 0), 0x50);
    CHECK_EQ(card->card_peek(DEMO_SPR + 11 * 8 + 1), 0x04);
    CHECK_EQ(card->card_peek(DEMO_SPR + 11 * 8 + 6), 0x18); // the 12th: depth 2, group 1
    CHECK_EQ(card->card_peek(DEMO_SPR + 12 * 8 + 6), 0x00); // no 13th
    CHECK_EQ(card->peek(R2::PCTL), 0);           // DRQ off between the copies
    const unsigned at = ball0(), scroll = card->peek(l0 + R2::HSCROLL);
    b.run(10 * FRAME);
    CHECK_NE(ball0(), at);
    CHECK_EQ(static_cast<uint8_t>(card->peek(l0 + R2::HSCROLL) - scroll), 10); // a pixel a frame
}

TEST(the_demos_ripple_is_sixteen_bitmaps_in_card_memory_flipped_by_mapbase)
{
    // The new effect: 960 KB of frames, more than revision 1's whole 256 KB. Each row of frame f is a window on
    // the stripes (colours 80-95, a ramp up and down every 32 pixels) at (y / 2 + 2f + sine[4y + 16f]) mod 32.
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    VideoCard *card = b.machine->video_card();
    start_demo(b);
    const std::string sine = demo_flash(label(bios_labels(), "SINE"), 256);
    auto stripe = [](unsigned x) { return static_cast<uint8_t>(80 + (x % 32 < 16 ? x % 32 : 31 - x % 32)); };
    unsigned wrong = 0, still = 0;
    for (unsigned f = 0; f < 16; ++f)
    {
        for (unsigned y = 0; y < 240; ++y)
        {
            const unsigned o = (y / 2 + 2 * f + static_cast<uint8_t>(sine[(4 * y + 16 * f) & 255])) % 32;
            for (unsigned x = 0; x < 256; ++x)
                wrong += card->card_peek(DEMO_RIPPLE + f * RIPPLE_SIZE + y * 256 + x) != stripe(x + o);
        }
        const uint32_t next = DEMO_RIPPLE + (f + 1) % 16 * RIPPLE_SIZE;
        still += card_bytes(*card, DEMO_RIPPLE + f * RIPPLE_SIZE, RIPPLE_SIZE) == card_bytes(*card, next, RIPPLE_SIZE);
    }
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(still, 0u);                         // every frame differs from the next
    CHECK_EQ(card->card_peek(DEMO_RIPPLE + 16 * RIPPLE_SIZE), 0x00); // and there are 16

    // After 256 frames of tiles, the ripple. CONFIG changes a frame after MAPBASE, as the bases latch at line
    // 480 and CONFIG doesn't: the first frame drawn as a bitmap is the ripple's (not the tile map read as pixels).
    using R2 = VideoCard::Rev2;
    const uint8_t l0 = VideoCard::LAYER0, l1 = VideoCard::LAYER1;
    const uint64_t first = card->frames();
    for (int i = 0; i < 300 * 64 && card->peek(l0 + VideoCard::CONFIG) != CONFIG_RIPPLE; ++i)
        b.run(FRAME / 64);                       // still in the vertical blank that wrote it, at the end
    REQUIRE(card->peek(l0 + VideoCard::CONFIG) == CONFIG_RIPPLE);
    std::printf("  the ripple after %llu frames of tiles\n", static_cast<unsigned long long>(card->frames() - first));
    std::vector<uint32_t> ramp;
    for (unsigned i = 80; i < 96; ++i)
        ramp.push_back(VideoCard::rgba(static_cast<uint16_t>(card->card_peek(DEMO_PAL + 2 * i) |
                                                             card->card_peek(DEMO_PAL + 2 * i + 1) << 8)));
    auto ramp_share = [&] {
        unsigned in = 0, n = 0;
        for (unsigned y = 80; y < 400; y += 4)       // clear of the caption's rows
            for (unsigned x = 64; x < 576; x += 4, ++n)
                in += std::find(ramp.begin(), ramp.end(), card->frame()[y * VideoCard::WIDTH + x]) != ramp.end();
        return 100 * in / n;
    };
    b.run(FRAME);                                // the first frame drawn with it
    std::printf("  the ripple's first frame: %u%% of the samples are its colours\n", ramp_share());
    CHECK(ramp_share() >= 85);                   // (the balls cover the rest)
    CHECK(colours(*card) >= 12);
    CHECK_EQ(reg16(*card, l1 + R2::MAPBASE), DEMO_TEXT2 / 256); // the caption's second page
    // Then a new bitmap every frame, in order.
    unsigned last = reg16(*card, l0 + R2::MAPBASE), steps = 0;
    for (int f = 0; f < 20; ++f)
    {
        b.run(FRAME);
        const unsigned base = reg16(*card, l0 + R2::MAPBASE);
        CHECK(base >= DEMO_RIPPLE / 256 && base < (DEMO_RIPPLE + 16 * RIPPLE_SIZE) / 256);
        steps += base == (last - DEMO_RIPPLE / 256 + RIPPLE_SIZE / 256) % (16 * RIPPLE_SIZE / 256) + DEMO_RIPPLE / 256;
        last = base;
    }
    CHECK_EQ(steps, 20u);
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), CONFIG_RIPPLE);
    // And after 256 frames of it, the tiles again.
    b.run(240 * FRAME);
    CHECK_EQ(card->peek(l0 + VideoCard::CONFIG), CONFIG_TILES);
    CHECK_EQ(reg16(*card, l0 + R2::MAPBASE), DEMO_MAP / 256);
    CHECK_EQ(reg16(*card, l1 + R2::MAPBASE), DEMO_TEXT1 / 256);
}

TEST(a_key_ends_the_demo_with_the_monitors_screen_as_it_was)
{
    // On revision 2 the monitor's screen is in card memory, in the BIOS's and OODOS's region below $040000;
    // Apple II mode's is from $1C0000. The demo leaves both as they were: each is filled with a pattern as the
    // demo starts and must still hold it when BREAK takes over (whose VSCREEN writes only registers). Then what
    // was there goes back, for the monitor's checks after.
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C680.C684\r");
    const std::string before = b.row(5);
    VideoCard *card = b.machine->video_card();
    const std::vector<std::pair<uint32_t, uint32_t>> kept_regions = {{0x000000, 0x040000}, {0x1C0000, 0x200000}};
    auto pattern = [](uint32_t a) { return static_cast<uint8_t>(a * 7 + (a >> 9) + 1); };
    std::vector<uint8_t> was;
    start_demo(b, false, [&] {
        for (const auto &[from, to] : kept_regions)
            for (uint32_t a = from; a < to; ++a)
            {
                was.push_back(card->card_peek(a));
                card->card_poke(a, pattern(a));
            }
    });
    REQUIRE(card->peek(VideoCard::CTRL) == 0x0F);
    b.run(20 * FRAME);
    const unsigned brk = label(bios_labels(), "BREAK");
    unsigned changed = ~0u;
    b.on_step = [&] {
        if (b.cpu->regs().pc != brk || changed != ~0u)
            return;
        changed = 0;
        size_t i = 0;
        for (const auto &[from, to] : kept_regions)
            for (uint32_t a = from; a < to; ++a)
            {
                changed += card->card_peek(a) != pattern(a);
                card->card_poke(a, was[i++]);
            }
    };
    const std::string out = b.run(10 * FRAME, "X");
    b.on_step = nullptr;
    REQUIRE(changed != ~0u);                     // BREAK was reached
    CHECK_EQ(changed, 0u);                       // the BIOS's, OODOS's and Apple II mode's card memory untouched
    CHECK(has(out, "\\"));                     // the monitor's prompt again
    for (unsigned n = 0; n < 4; ++n)             // the aperture's slots as /RESET left them
        CHECK_EQ(reg16(*card, static_cast<uint8_t>(VideoCard::Rev2::SLOT0 + 2 * n)), n);
    check_text_screen_registers(*card);
    CHECK_EQ(b.bridge().window(1), 0x01);        // the windows back
    CHECK_EQ(b.bridge().window(2), 0x02);
    CHECK_EQ(b.row(5), before);                  // the text screen kept
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F")); // and the monitor works
}

TEST(on_a_revision_1_card_the_demo_goes_straight_back_to_the_monitor)
{
    // The demo is revision 2's: on revision 1 it changes nothing on the card (whether the BIOS's C803 lets it start
    // or not), and the monitor carries on.
    Board b(false, true, {}, "", {}, 1);
    REQUIRE(b.ok());
    REQUIRE(b.machine->video_card()->revision() == 1);
    REQUIRE(boot_past_splash(b));                // so Break's end of the splash isn't taken for the demo's doing
    const VideoCard *card = b.machine->video_card();
    auto registers = [&] {
        std::string r; // all but what the beam changes
        for (uint8_t o = VideoCard::CTRL; o < 0x30; ++o)
            if (o != VideoCard::ISR && o != VideoCard::LINE && o != VideoCard::LINE + 1 && o != VideoCard::STATUS)
                r += static_cast<char>(card->peek(o));
        return r;
    };
    const std::string before = registers();
    const std::string out = b.run(BOOT, "C803R\r");
    CHECK(has(out, "C803: 4C\\"));
    CHECK(registers() == before);
    CHECK_EQ(b.bridge().window(1), 0x01);
    CHECK_EQ(b.bridge().window(2), 0x02);
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F"));
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

TEST(printing_through_a_scroll_puts_back_slot_0_and_the_ports_settings)
{
    // VPUTC borrows aperture slot 0 and puts it back, and a clear or a scroll (PORTJOBS) puts back the port's
    // PADDR, PSTEP and PCTL (video.s; the Technical Reference, chapter 6), so a program that prints keeps its own.
    // A program's settings, none of them the BIOS's: slot 0 on page 7, PADDR mid-card, a step of 160, stepping down,
    // no DRQ. Then a CR on the bottom row, which scrolls.
    Board b;
    REQUIRE(b.ok());
    REQUIRE(boot_past_splash(b));
    VideoCard *card = b.machine->video_card();
    using R2 = VideoCard::Rev2;
    constexpr uint32_t TEXT = 0x002000;
    const std::string mark = "MARK";
    for (size_t c = 0; c < mark.size(); ++c)
        card->card_poke(static_cast<uint32_t>(TEXT + 10 * 160 + c * 2), static_cast<uint8_t>(mark[c]));
    b.machine->bus().write_physical(0x000E2, 0);               // VCOL, VROW (BIOS_ZP + 2, + 3): the bottom row
    b.machine->bus().write_physical(0x000E3, 29);
    card->write(R2::SLOT0, 0x07);
    card->write(R2::SLOT0 + 1, 0x00);
    card->write(R2::PADDR, 0x56);
    card->write(R2::PADDR + 1, 0x34);
    card->write(R2::PADDR + 2, 0x12);
    card->write(R2::PSTEP, 0xA0);
    card->write(R2::PSTEP + 1, 0x00);
    card->write(R2::PCTL, R2::PCTL_DOWN);
    REQUIRE(reg16(*card, R2::SLOT0) == 0x0007);
    const std::string out = b.run(BOOT, "\r");                // the monitor: its echo and GETLINE's, two CRs
    CHECK(has(out, "\r\r"));
    auto card_row = [&](unsigned r) {
        std::string t;
        for (unsigned c = 0; c < mark.size(); ++c)
            t += static_cast<char>(card->card_peek(TEXT + r * 160 + c * 2));
        return t;
    };
    CHECK_EQ(card_row(10), std::string("    "));               // scrolled: the mark is two rows up
    CHECK_EQ(card_row(8), mark);
    CHECK_EQ(reg16(*card, R2::SLOT0), 0x0007u);                // and all four as the program left them
    CHECK_EQ(card->peek(R2::PADDR) | unsigned{card->peek(R2::PADDR + 1)} << 8 |
                 unsigned{card->peek(R2::PADDR + 2)} << 16,
             0x123456u);
    CHECK_EQ(reg16(*card, R2::PSTEP), 0x00A0u);
    CHECK_EQ(card->peek(R2::PCTL), R2::PCTL_DOWN);
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
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    start_demo(b, true);
    b.run(20 * FRAME);
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    const std::string out = b.type_usb(" ");
    CHECK(has(out, "\\"));
    check_text_screen_registers(*b.machine->video_card());
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

TEST(c806_on_a_revision_1_card_says_so_and_stays_in_the_native_map)
{
    // Apple II mode is revision 2's (its character ROM goes in through revision 2's port): with a IIe ROM in the
    // cartridge but a revision 1 card, C806R refuses before it touches anything, and the monitor carries on.
    const std::string rom = fake_iie_rom("test_rev1_iie.rom");
    Board b(false, true, {rom}, "", {}, 1);
    std::remove(rom.c_str());
    REQUIRE(b.ok());
    REQUIRE(b.machine->video_card()->revision() == 1);
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    bus.write_physical(0x2ABCD, 0x5A);                        // what the fills would clear
    const VideoCard *card = b.machine->video_card();
    std::string regs;
    for (uint8_t o = VideoCard::CTRL; o < 0x30; ++o)
        if (o != VideoCard::ISR && o != VideoCard::LINE && o != VideoCard::LINE + 1 && o != VideoCard::STATUS)
            regs += static_cast<char>(card->peek(o));
    const std::string out = b.run(2 * BOOT, "C806R\r");
    CHECK(has(out, "APPLE II MODE NEEDS THE REVISION 2 VIDEO CARD\\")); // then the monitor's prompt
    CHECK(!b.bridge().apple());                                // still the native map
    CHECK_EQ(bus.peek_physical(0x2ABCD), 0x5A);                // nothing cleared
    CHECK(bus.peek_physical(0xEFFFC) != 0x62 || bus.peek_physical(0xEFFFD) != 0xFA); // no IIe ROM copied to bank E
    std::string after;
    for (uint8_t o = VideoCard::CTRL; o < 0x30; ++o)
        if (o != VideoCard::ISR && o != VideoCard::LINE && o != VideoCard::LINE + 1 && o != VideoCard::STATUS)
            after += static_cast<char>(card->peek(o));
    CHECK(after == regs);                                      // the card as it was
    CHECK_EQ(b.bridge().window(1), 0x01);
    CHECK_EQ(b.bridge().window(2), 0x02);
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F")); // the monitor carries on
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
        opt.video_revision = VIDEO_REVISION;
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

TEST(c806_puts_the_character_rom_in_card_memory_or_makes_one_from_the_vga_font)
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
    // In the card's memory at $1E0000 (hw/video-rev2.md's layout), where it draws Apple text from.
    constexpr uint32_t CHARS = VideoCard::APPLE_CHARS_V2;
    const VideoCard &with_card = *with.machine->video_card();
    size_t differ = 0;
    for (uint32_t i = 0; i < 0x800; ++i)
        differ += with_card.card_peek(CHARS + i) != static_cast<uint8_t>(i * 3 + 1);
    CHECK_EQ(differ, size_t{0});                                // the cartridge's, its first 2 KB
    // Without one: each glyph from the VGA font (flash block 30), 16 rows folded to 8,
    // 7 dots reversed, normal codes inverted. Check A both ways: $C1 normal, $01 inverse.
    const VideoCard &card = *without.machine->video_card();
    const std::string flash = read_file(EMU_BIOS_ROM);
    REQUIRE(flash.size() == 0x80000u);
    size_t wrong = 0;
    for (unsigned r = 0; r < 8; ++r)
    {
        const uint8_t vga = static_cast<uint8_t>(flash[0x78000 + 'A' * 16 + 2 * r] | flash[0x78000 + 'A' * 16 + 2 * r + 1]);
        uint8_t dots = 0;
        for (unsigned b = 0; b < 7; ++b)
            dots = static_cast<uint8_t>(dots | ((vga >> (7 - b)) & 1) << b);
        wrong += card.card_peek(CHARS + 0xC1 * 8 + r) != static_cast<uint8_t>(~dots);
        wrong += card.card_peek(CHARS + 0x01 * 8 + r) != dots;
    }
    CHECK_EQ(wrong, size_t{0});
    size_t blank = 0;
    for (unsigned r = 0; r < 8; ++r)
        blank += card.card_peek(CHARS + 0xC1 * 8 + r) == 0xFF;
    CHECK(blank < 8);                                           // something to see: A is not empty
}

TEST(break_during_the_demo_puts_the_windows_and_the_screen_back)
{
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    start_demo(b);
    b.run(20 * FRAME);
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card->peek(VideoCard::CTRL) == 0x0F);
    b.cpu->nmi();                                // Pause/Break, mid-frame
    CHECK(has(b.run(BOOT), "\\"));
    check_text_screen_registers(*card);
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
    CHECK_EQ(reg16(*b.machine->video_card(), VideoCard::LAYER0 + VideoCard::Rev2::HSCROLL), 0);
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
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    start_demo(b);
    b.run(20 * FRAME);
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    const VideoCard *card = b.machine->video_card();
    auto y = [&](unsigned n) { return int{card->card_peek(DEMO_SPR + n * 8 + 4)}; };
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

namespace
{
    /// Step `b` until `done`, the console serviced and `key` offered until taken. @return whether it came
    template <typename F> bool step_until(Board &b, F done, uint64_t cycles, char key = 0)
    {
        const uint64_t end = b.cpu->cycles() + cycles;
        while (b.cpu->cycles() < end)
        {
            if (done())
                return true;
            b.cpu->step();
            b.console->service();
            uint8_t ch = 0;
            while (b.console->output(ch))
                ;
            if (key && b.console->key(static_cast<uint8_t>(key)))
                key = 0;
        }
        return done();
    }
} // namespace

TEST(c800_after_a_bootsel_boot_starts_the_image_in_ram_again)
{
    Board b(true);
    REQUIRE(b.ok());
    const std::string flash = read_file(EMU_BIOS_ROM);
    Device *ram_d = b.machine->find("ram_d");
    Device *rom_a = b.machine->find("rom_a");
    REQUIRE(ram_d != nullptr && rom_a != nullptr && flash.size() == 0x80000u);
    for (uint32_t a = 0; a < 0x4000; ++a)
    {
        ram_d->poke(0x3C000 + a, static_cast<uint8_t>(flash[0x7C000 + a]));
        rom_a->poke(0x7C000 + a, 0xFF);        // no flash to go back to
    }
    b.reset();
    CHECK_EQ(b.run(BOOT), GREETING);
    CHECK(has(b.run(BOOT, "C800R\r"), GREETING.c_str()));
    CHECK_EQ(b.bridge().window(3), 0x7F);
    CHECK(has(b.run(BOOT, "C680.C684\r"), "C680: 00 01 02 7F 7F"));
}

TEST(break_stops_the_dma_and_quiets_what_would_interrupt)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    b.machine->usb_card()->key(0xE1, true);      // Shift: a USB key event, but no character
    b.machine->usb_card()->key(0xE1, false);
    const std::string keys = "C418: 00 10 00\r"  // channel 1: a byte to $01000...
                             "C410: 00 00 00 00 01 00 8A 03\r" // ...interrupting at terminal count
                             "C406: 01 01\r"     // channel 0 armed, as for the SD card
                             "C5B0: 01\r"        // USB: the key event interrupts
                             "C540: 02 00 04\r"; // the sound card's 6510 running, its mailbox interrupting
    b.run(keys.size() * 25'000 + BOOT / 2, keys);
    REQUIRE(bus.irq());
    REQUIRE(bus.peek(0xC407) & 1);
    b.cpu->nmi();
    CHECK(has(b.run(BOOT), "\\"));
    CHECK(!bus.irq());
    CHECK_EQ(bus.peek(0xC440) & 0x0F, 0);        // no terminal count left
    CHECK_EQ(bus.peek(0xC407) & 1, 0);           // channel 0 stopped
    CHECK_EQ(bus.peek(0xC416) & 0x80, 0);        // and its interrupt off
    CHECK_EQ(bus.peek(0xC5B0), 0);
    CHECK_EQ(bus.peek(0xC540), 0);               // the 6510 stopped
    CHECK_EQ(bus.peek(0xC542), 0);
    // CLI; LDA #'Z'; JSR ECHO; SEI; JMP WOZMON: runs, rather than living in IRQ and RTI.
    const std::string lbl = read_file(EMU_BIOS_ROM ".lbl");
    const unsigned echo = label(lbl, "ECHO"), wozmon = label(lbl, "WOZMON");
    REQUIRE(echo >= 0xC800 && wozmon >= 0xC800);
    b.run(BOOT, "300: 58 A9 5A 20 " + hex(echo) + " " + hex(echo >> 8) + " 78 4C " + hex(wozmon) + " " +
                    hex(wozmon >> 8) + "\r");
    CHECK(has(b.run(BOOT, "300R\r"), "0300: 58Z"));
}

namespace
{
    /// CLI; LDA #'Z'; JSR ECHO; SEI; JMP WOZMON at $0300, run with R: it prints, rather than living in IRQ
    /// and RTI, only if nothing is left interrupting.
    bool cli_runs(Board &b)
    {
        const std::string lbl = read_file(EMU_BIOS_ROM ".lbl");
        const unsigned echo = label(lbl, "ECHO"), wozmon = label(lbl, "WOZMON");
        if (echo < 0xC800 || wozmon < 0xC800)
            return false;
        b.run(BOOT, "300: 58 A9 5A 20 " + hex(echo) + " " + hex(echo >> 8) + " 78 4C " + hex(wozmon) + " " +
                        hex(wozmon >> 8) + "\r");
        return has(b.run(BOOT, "300R\r"), "0300: 58Z");
    }
} // namespace

TEST(break_turns_the_vias_interrupts_off)
{
    // A program's VIA timer left running and enabled: Break must leave it unable to interrupt (todo item 13).
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    const std::string keys = "C34B: 40\r"     // ACR: timer 1 free-running
                             "C34E: C0\r"     // IER: set timer 1's
                             "C344: 00 10\r"; // T1 $1000: writing the high byte starts it
    b.run(keys.size() * 25'000 + BOOT / 2, keys);
    REQUIRE(bus.irq());
    REQUIRE(bus.peek(0xC34E) == 0xC0);
    b.cpu->nmi();
    CHECK(has(b.run(BOOT), "\\"));
    CHECK(!bus.irq());
    CHECK_EQ(bus.peek(0xC34E), 0x80); // IER: none enabled (bit 7 reads 1)
    CHECK(bus.peek(0xC34D) & 0x40);   // the timer runs on: its flag is set, but can't interrupt
    CHECK(cli_runs(b));
}

TEST(break_clears_a_front_panel_key_already_pending)
{
    // The console card pulls /IRQ while ISR is set, whatever IEN says: Break clears it (todo item 13).
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    ConsoleCard *panel = b.machine->console_card();
    REQUIRE(panel != nullptr);
    b.run(BOOT, "C718: 01\r");          // IEN: a key interrupts
    panel->press(ConsoleCard::LOAD_ADRS);
    REQUIRE(bus.irq());
    REQUIRE(bus.peek(0xC719) == 1);
    b.cpu->nmi();
    CHECK(has(b.run(BOOT), "\\"));
    CHECK(!bus.irq());
    CHECK_EQ(bus.peek(0xC719), 0);
    CHECK_EQ(bus.peek(0xC718), 0);
    CHECK(cli_runs(b));
}

TEST(break_turns_the_clocks_tick_off_and_the_time_runs_on)
{
    Board b;
    REQUIRE(b.ok());
    BackplaneBridge *bridge = b.machine->backplane_bridge();
    REQUIRE(bridge != nullptr);
    bridge->set_cpu_hz(1'000'000); // a second in a million cycles
    b.reset();
    b.run(BOOT);
    Decoder &bus = b.machine->bus();
    b.run(BOOT, "C687: 40\r");       // RTCCTL: TIE
    b.run(1'100'000);                 // a second: TICK
    REQUIRE(bus.irq());
    REQUIRE((bus.peek(0xC687) & 0xC0) == 0xC0);
    const uint8_t sec = bus.peek(0xC690);
    b.cpu->nmi();
    std::string out = b.run(20'000);
    CHECK(!bus.irq());
    CHECK_EQ(bus.peek(0xC687) & 0xC0, 0); // TICK and TIE off
    CHECK(has(out + b.run(BOOT), "\\"));
    b.run(1'100'000);
    CHECK_NE(bus.peek(0xC690), sec);      // still counting
    CHECK_EQ(bus.peek(0xC687) & 0x40, 0);
}

TEST(break_finds_the_screen_whatever_its_zero_page_says)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "E4: 00\r");                     // VOK: a program said there's no screen
    b.cpu->nmi();
    b.run(BOOT);
    CHECK_EQ(b.machine->bus().peek(0xE4), 0xFF);
    b.run(BOOT, "C680\r");
    CHECK(b.find_row("C680: 00") >= 0);
}

TEST(a_scroll_with_no_dma_controller_gives_up_rather_than_hangs)
{
    Board b(false, true, {}, "", {"dma"});
    REQUIRE(b.ok());
    b.reset();
    CHECK_EQ(b.run(BOOT), GREETING);
    b.run(BOOT, "E3: 1D FF\r");                  // the bottom row, and VOK set by a program
    CHECK(has(b.run(8 * BOOT, "C680\r"), "C680: 00"));
}

TEST(break_during_a_burst_limited_scroll_stops_it)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C441: 01\r");                   // a cycle back to the CPU every byte
    b.run(BOOT, "E3: 1D\r");                     // the cursor on the bottom row
    Decoder &bus = b.machine->bus();
    REQUIRE(step_until(b, [&] { return (bus.peek(0xC417) & 1) != 0; }, BOOT, '\r')); // a scroll under way (the port's channel 1)
    b.cpu->nmi();
    const unsigned wozmon = label(read_file(EMU_BIOS_ROM ".lbl"), "WOZMON");
    REQUIRE(step_until(b, [&] { return b.cpu->regs().pc == wozmon; }, BOOT));
    CHECK_EQ(bus.peek(0xC417) & 1, 0);           // stopped by Break, not left to run
    CHECK_EQ(bus.peek(0xC4BE), 0);               // and the port's DRQ off
    CHECK(has(b.run(BOOT, "C680\r"), "C680: 00"));
    CHECK_EQ(b.row(29), std::string("_"));
    CHECK_EQ(b.row(28), std::string("C680: 00"));
}

TEST(a_scroll_leaves_the_dma_master_enable_as_it_was)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C441: 70\r");                   // DMA off, as a program may leave it
    b.run(BOOT, "E3: 1D\r");
    b.run(BOOT, "C680\r");
    CHECK_EQ(b.machine->bus().peek(0xC441), 0x70);
    CHECK_EQ(b.row(29), std::string("_"));
    CHECK_EQ(b.row(28), std::string("C680: 00"));
}

TEST(the_demo_runs_with_the_dma_master_enable_off)
{
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C441: 70\r");
    start_demo(b);
    b.run(20 * FRAME);
    REQUIRE(b.machine->video_card()->peek(VideoCard::CTRL) == 0x0F);
    CHECK_EQ(b.machine->video_card()->card_peek(DEMO_SPR + 11 * 8 + 6), 0x18); // the sprite table, by port DMA
    CHECK_EQ(b.machine->video_card()->card_peek(DEMO_RIPPLE + 239 * 256 + 255) & 0xF0, 0x50); // the ripple, too
    CHECK(has(b.run(10 * FRAME, "X"), "\\"));
    CHECK_EQ(b.machine->bus().peek(0xC441), 0x70);
}

TEST(c806_stops_a_programs_dma_before_its_fills)
{
    const std::string rom = fake_iie_rom("test_dma_iie.rom");
    Board b(false, true, {rom});
    std::remove(rom.c_str());
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    b.run(BOOT, "C441: 01\r");                   // a burst limit, so the monitor still runs...
    b.run(BOOT, "C420: 00 00 02 00 00 00 45 01\r"); // ...beside channel 2 feeding the sound card for ever
    REQUIRE(b.machine->bus().peek(0xC427) & 1);
    b.run(4 * BOOT, "C806R\r");
    CHECK(b.bridge().apple());
    CHECK_EQ(b.cpu->regs().pc, 0xFA67);          // the IIe ROM's reset code ran
}

// --- golden frames (hw/video-rev2.md, "Emulator changes": the BIOS screen and the demo) -----------------
//
// Whole frames, hashed (FNV-1a over the pixels, as test_video's splash frames): any change to what the card
// draws for them, the BIOS's screen or the renderer, shows here. The hashes come from this emulator, never from a
// fixture. Each is taken at a fixed frame number, not a cycle count, so a BIOS that merely runs faster or slower
// keeps them; the cursor's and the caption's blink depend on that number too. A change that should change the
// picture (a new BIOS version in the banner, a new demo) fails them: look at the PPM the failure writes beside
// the test, and if it is right, put the hash it printed here.

namespace
{
    uint32_t frame_hash(const VideoCard &card)
    {
        uint32_t h = 2166136261u;
        for (unsigned i = 0; i < VideoCard::WIDTH * VideoCard::HEIGHT; ++i)
            h = (h ^ card.frame()[i]) * 16777619u;
        return h;
    }

    /// Run until the card has finished frame `n` (and not the next): frame() is that one.
    void run_to_frame(Board &b, uint64_t n)
    {
        const VideoCard &card = *b.machine->video_card();
        while (card.frames() < n)
            b.run(FRAME / 64);
    }

    /// The frame's hash is `want`; if not, say the new one and write the frame as golden-`name`.ppm to look at.
    bool golden(const VideoCard &card, const char *name, uint32_t want)
    {
        const uint32_t got = frame_hash(card);
        if (got == want)
            return true;
        const std::string ppm = std::string("golden-") + name + ".ppm";
        std::printf("  golden frame %s: hash 0x%08X, want 0x%08X; the frame is in %s\n", name, got, want,
                    write_ppm(card, ppm) ? ppm.c_str() : "(not written)");
        return false;
    }

    constexpr uint64_t MONITOR_FRAME = 360;   // after the splash's 300, the cursor lit (frame / 16 even)
    constexpr uint32_t MONITOR_HASH = 0xECF44BE7;
    constexpr uint32_t DEMO_FIRST_HASH = 0xA6F79AEF;   // the first frame the demo draws whole: the fade's start
    constexpr uint32_t DEMO_SETTLED_HASH = 0x0D630073; // 30 frames on: faded in, the balls under way
} // namespace

TEST(golden_frame_the_monitors_text_screen)
{
    Board b;
    REQUIRE(b.ok());
    b.reset();
    CHECK_EQ(b.run(BOOT), GREETING);
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card != nullptr);
    b.run(SPLASH_TIME);
    REQUIRE(card->splash_frame() == -1);
    REQUIRE(card->frames() < MONITOR_FRAME);
    run_to_frame(b, MONITOR_FRAME);
    CHECK_EQ(b.row(1), std::string("BACKPLANE BIOS 0.13"));
    CHECK(golden(*card, "monitor", MONITOR_HASH));
}

TEST(golden_frames_the_demos_first_and_a_settled_one)
{
    Board b{Board::Rev2{}};
    REQUIRE(b.ok());
    b.reset();
    b.run(BOOT);
    const VideoCard *card = b.machine->video_card();
    REQUIRE(card != nullptr);
    // CTRL turning $0F (display, both layers, sprites) starts the demo's screen. A frame the beam was drawing
    // then is torn (above that line the monitor's, below it the new layers on bases that latch only at line
    // 480): the first the demo draws whole is the next.
    bool on = false;
    uint64_t at = 0;
    unsigned line = 0;
    b.on_step = [&] {
        if (!on && card->peek(VideoCard::CTRL) == 0x0F)
        {
            on = true;
            at = card->frames();
            line = card->beam_line();
        }
    };
    CHECK(b.run(4 * FRAME, "C803R\r").find("C803: 4C\\") == std::string::npos); // C803 took the card
    for (int f = 0; f < 60 && !on; ++f)
        b.run(FRAME);
    b.on_step = nullptr;
    REQUIRE(on);
    const uint64_t first = at + (line < VideoCard::HEIGHT ? 2 : 1);
    std::printf("  CTRL $0F at line %u of frame %llu: the demo's first whole frame is %llu\n", line,
                static_cast<unsigned long long>(at + 1), static_cast<unsigned long long>(first));
    run_to_frame(b, first);
    CHECK(golden(*card, "demo-first", DEMO_FIRST_HASH));
    run_to_frame(b, first + 30);
    CHECK(colours(*card) >= 12);
    CHECK(golden(*card, "demo-settled", DEMO_SETTLED_HASH));
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
