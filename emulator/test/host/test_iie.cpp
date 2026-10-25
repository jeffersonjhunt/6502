/**
 * @file test_iie.cpp
 * @brief Apple II mode with the owner's real Apple IIe ROMs (fixtures/apple2e, gitignored).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane through a Session, as the WebUI drives it, with the enhanced
 * IIe ROM in the cartridge: C806R, then the IIe's own boot, Applesoft, the
 * USB keyboard as the IIe's, and its resets. Then the Disk II with the
 * owner's PROMs and disks (fixtures/disks): DOS 3.3 booting, saving and
 * meeting write protect, and The Oregon Trail's WOZ booting to its menu.
 * The IIe's screen is read from text page 1 in memory. Apple's ROMs and the
 * disks are never in the repo: without fixtures/apple2e (see its index.txt)
 * the suite skips; without a disk, its tests do nothing.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "session.h"
#include "usb_card.h"
#include "video_card.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    const std::string ROMS = EMU_FIXTURE_DIR "/apple2e/";
    const std::string IIE = ROMS + "iie-enhanced-c000-ffff.bin";
    const std::string CHARS = ROMS + "iie-video-enhanced-342-0265-a.bin";
    constexpr uint64_t FRAME = 17'030; // an Apple frame of cycles
    const std::string P5 = ROMS + "disk2-16-p5-boot-341-0027-a.bin";
    const std::string P6 = ROMS + "disk2-16-p6-sequencer-341-0028-a.bin";
    const std::string DISKS = EMU_FIXTURE_DIR "/disks/";
    const std::string TRAIL_DSK = DISKS + "The Oregon Trail.dsk";
    const std::string TRAIL_WOZ = DISKS + "The Oregon Trail - Disk 1, Side A.woz";

    std::vector<uint8_t> bytes_of(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    bool have(const std::string &path) { return std::ifstream(path).good(); }

    struct IIe
    {
        /// `disk`: the Disk II's PROMs too, and that disk in drive 1.
        explicit IIe(const std::string &rom = IIE, const std::string &disk = "")
        {
            std::string error;
            Session::Config c{EMU_MACHINE_DIR "/backplane.machine", EMU_BIOS_ROM, ""};
            c.apple_rom = {rom};
            c.apple_chars = CHARS;
            if (!disk.empty())
            {
                c.disk2_boot = P5;
                c.disk2_sequencer = P6;
            }
            s = Session::create(c, error);
            if (!s || (!disk.empty() && !s->insert_disk(0, bytes_of(disk), disk, error)))
            {
                std::printf("  %s\n", error.c_str());
                s.reset();
                return;
            }
            run(1'500'000); // the BIOS's boot, at 7.16 MHz: 0.2 s
            s->take_output();
            s->send("C806R\r");
            for (int i = 0; i < 400 && !s->fixed_hz(); ++i)
                s->run(FRAME);
            run(60 * FRAME); // a second of the IIe's boot
        }
        bool ok() const { return s && s->fixed_hz() != 0; }
        void run(uint64_t cycles) { s->run(cycles); }

        /// Text page 1, row r, as characters (the IIe's screen codes, high bit set when normal).
        std::string row(unsigned r) const
        {
            std::string t;
            const uint32_t base = 0x0400 + (r & 7) * 0x80 + (r >> 3) * 0x28;
            for (unsigned c = 0; c < 40; ++c)
            {
                uint8_t b = s->peek_physical(base + c) & 0x7F;
                if (b < 0x20)
                    b = static_cast<uint8_t>(b | 0x40); // inverse capitals
                t += static_cast<char>(b);
            }
            return t.substr(0, t.find_last_not_of(' ') + 1);
        }
        bool screen_has(const std::string &text) const
        {
            for (unsigned r = 0; r < 24; ++r)
                if (row(r).find(text) != std::string::npos)
                    return true;
            return false;
        }
        void dump() const
        {
            for (unsigned r = 0; r < 24; ++r)
                std::printf("  %2u |%s|\n", r, row(r).c_str());
        }

        /// The video card's pixel for Apple dot (x, y) (2x2 each, from 40, 48).
        uint32_t dot(unsigned x, unsigned y) const { return s->video()->frame()[(48 + y * 2) * 640 + 40 + x * 2]; }

        /// Type on the USB keyboard, at a person's pace.
        void type(const std::string &text)
        {
            UsbCard *usb = s->usb();
            for (const char ch : text)
            {
                uint8_t usage = 0;
                bool shift = false;
                if (ch >= 'A' && ch <= 'Z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'A');
                else if (ch >= '1' && ch <= '9')
                    usage = static_cast<uint8_t>(0x1E + ch - '1');
                else if (ch == '0')
                    usage = 0x27;
                else if (ch == ' ')
                    usage = 0x2C;
                else if (ch == '\r')
                    usage = 0x28;
                else if (ch == '+')
                    usage = 0x2E, shift = true;
                else if (ch == '=')
                    usage = 0x2E;
                else if (ch == ',')
                    usage = 0x36;
                else if (ch == '"')
                    usage = 0x34, shift = true;
                else if (ch == ':')
                    usage = 0x33, shift = true;
                if (shift)
                    usb->queue_key(0xE1, true);
                usb->queue_key(usage, true);
                run(3 * FRAME);
                usb->queue_key(usage, false);
                if (shift)
                    usb->queue_key(0xE1, false);
                run(3 * FRAME);
            }
            run(30 * FRAME);
        }
        /// A key chord: modifiers held, the key pressed and let go, the modifiers let go.
        void chord(std::initializer_list<uint8_t> mods, uint8_t key)
        {
            UsbCard *usb = s->usb();
            for (uint8_t m : mods)
                usb->queue_key(m, true);
            usb->queue_key(key, true);
            run(3 * FRAME);
            usb->queue_key(key, false);
            run(3 * FRAME); // the IIe's reset code reads Open/Closed Apple: still held here
            for (uint8_t m : mods)
                usb->queue_key(m, false);
            run(3 * FRAME);
        }

        std::unique_ptr<Session> s;
    };

    constexpr uint8_t L_CTRL = 0xE0, L_ALT = 0xE2, R_ALT = 0xE6, DEL = 0x4C;
} // namespace

TEST(the_enhanced_iie_boots_to_applesoft)
{
    IIe e;
    REQUIRE(e.ok());
    CHECK_EQ(e.s->fixed_hz(), BackplaneBridge::APPLE_HZ);
    CHECK_EQ(e.row(0), std::string("               Apple //e"));
    CHECK_EQ(e.row(2).substr(0, 1), std::string("]")); // no disk card: Applesoft's prompt
    if (!e.screen_has("Apple //e"))
        e.dump();
}

TEST(applesoft_answers_on_the_usb_keyboard)
{
    IIe e;
    REQUIRE(e.ok());
    e.type("PRINT 2+2\r");
    CHECK(e.screen_has("]PRINT 2+2"));
    CHECK_EQ(e.row(3), std::string("4"));               // Applesoft: no leading space
    e.type("PRINT \"OO\",1\r");
    CHECK(e.screen_has("OO              1")); // Applesoft's comma: the next 16-column field
    if (e.row(3) != "4")
        e.dump();
}

TEST(ctrl_reset_stays_in_apple_ii_mode)
{
    IIe e;
    REQUIRE(e.ok());
    e.type("10 PRINT 1\r");
    e.chord({L_CTRL}, DEL);                         // Ctrl-Reset: Applesoft keeps its program
    CHECK_EQ(e.s->fixed_hz(), BackplaneBridge::APPLE_HZ);
    e.type("LIST\r");
    CHECK(e.screen_has("10  PRINT 1"));
    if (!e.screen_has("10  PRINT 1"))
        e.dump();
}

TEST(ctrl_open_apple_reset_reboots)
{
    IIe e;
    REQUIRE(e.ok());
    e.type("HOME\r");
    REQUIRE(!e.screen_has("Apple //e"));
    e.chord({L_CTRL, L_ALT}, DEL);                  // Open Apple: a cold start of the IIe
    e.run(60 * FRAME);
    CHECK(e.screen_has("Apple //e"));
    CHECK_EQ(e.s->fixed_hz(), BackplaneBridge::APPLE_HZ); // still the IIe: only a hard reset leaves
    if (!e.screen_has("Apple //e"))
        e.dump();
}

TEST(ctrl_closed_apple_reset_runs_the_self_test)
{
    IIe e;
    REQUIRE(e.ok());
    e.chord({L_CTRL, R_ALT}, DEL);                  // Closed Apple: the IIe's built-in diagnostics
    bool done = false;
    for (int i = 0; i < 60 && !done; ++i)           // up to ~10 s of Apple time
    {
        e.run(10 * FRAME * 6);
        done = e.screen_has("System OK") || e.screen_has("RAM") || e.screen_has("MMU") || e.screen_has("IOU");
    }
    CHECK(e.screen_has("System OK"));               // every chip it checks, passed: RAM, MMU, IOU, ROM
    if (!e.screen_has("System OK"))
        e.dump();
}

TEST(the_boot_screen_drawn_from_the_real_character_rom)
{
    IIe e;
    REQUIRE(e.ok());
    std::ifstream in(CHARS, std::ios::binary);
    const std::string rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(rom.size() == 0x1000);
    const uint32_t white = VideoCard::rgba(0xFFF), black = VideoCard::rgba(0x000);
    size_t wrong = 0, lit = 0;
    const std::string banner = "Apple //e";
    for (size_t i = 0; i < banner.size(); ++i)        // row 0, from column 15: normal characters ($80 up)
    {
        const unsigned code = 0x80u | static_cast<unsigned char>(banner[i]);
        for (unsigned r = 0; r < 8; ++r)
            for (unsigned b = 0; b < 7; ++b)
            {
                const bool on = !((static_cast<uint8_t>(rom[code * 8 + r]) >> b) & 1);
                lit += on;
                wrong += e.dot(static_cast<unsigned>((15 + i) * 7 + b), r) != (on ? white : black);
            }
    }
    CHECK(lit > 50);
    CHECK_EQ(wrong, size_t{0});
}

TEST(gr_and_plot_on_the_lores_screen)
{
    IIe e;
    REQUIRE(e.ok());
    e.type("GR\r");
    e.type("COLOR=1\r");                             // magenta
    e.type("PLOT 0,0\r");
    e.type("COLOR=12\r");                            // green
    e.type("PLOT 39,39\r");
    e.run(4 * FRAME);
    CHECK_EQ(e.dot(0, 0), VideoCard::rgba(0xE26));
    CHECK_EQ(e.dot(6, 3), VideoCard::rgba(0xE26));    // the whole 7x4 block
    CHECK_EQ(e.dot(7, 0), VideoCard::rgba(0x000));
    CHECK_EQ(e.dot(39 * 7, 39 * 4), VideoCard::rgba(0x1F4));
    CHECK(e.screen_has("]PLOT 39,39"));               // GR is mixed: the text window under it
}

TEST(hgr_and_hplot_on_the_hires_screen)
{
    IIe e;
    REQUIRE(e.ok());
    e.type("HGR\r");
    e.type("HCOLOR=3\r");                            // white
    e.type("HPLOT 0,0 TO 279,0\r");
    e.run(4 * FRAME);
    size_t white = 0;
    for (unsigned x = 0; x < 280; ++x)
        white += e.dot(x, 0) == VideoCard::rgba(0xFFF);
    CHECK(white > 270);                               // a white line across the top (the ends can fringe)
    CHECK_EQ(e.dot(140, 1), VideoCard::rgba(0x000));
}

TEST(a_dos_3_3_dsk_boots_and_runs_its_hello_program)
{
    // The owner's .dsk fixture is the DOS 3.3 System Master: DOS boots from it, then its HELLO
    // program reads on across the disk to load Integer BASIC.
    if (!have(TRAIL_DSK) || !have(P5))
        return; // no disk among the fixtures
    IIe e(IIE, TRAIL_DSK);
    REQUIRE(e.ok());
    int seconds = 0;
    for (; seconds < 20 && !e.screen_has("LOADING INTEGER BASIC"); ++seconds)
        e.run(60 * FRAME);
    CHECK(e.screen_has("DOS VERSION 3.3"));
    CHECK(e.screen_has("LOADING INTEGER BASIC"));
    CHECK(e.s->disk2()->status().spinning);
    if (!e.screen_has("DOS VERSION 3.3"))
        e.dump();
}

TEST(a_woz_boots_oregon_trail_to_its_menu)
{
    // The owner's WOZ of The Oregon Trail, disk 1 side A: about 14 s of loading across the
    // disk (its own loader, not DOS's), then its menu, drawn on hi-res page 2.
    if (!have(TRAIL_WOZ) || !have(P5))
        return;
    IIe e(IIE, TRAIL_WOZ);
    REQUIRE(e.ok());
    unsigned furthest = 0;
    int seconds = 0;
    for (; seconds < 30 && (seconds < 5 || e.s->disk2()->status().spinning); ++seconds)
    {
        e.run(60 * FRAME);
        furthest = std::max<unsigned>(furthest, e.s->disk2()->status().quarter_track[0]);
    }
    CHECK(seconds < 30);                   // it loaded, and the drive stopped
    CHECK(furthest > 100);                 // from all over the disk (track 25 and on)
    const AppleSwitches &sw = *e.s->apple_switches();
    CHECK(!sw.text && sw.hires && sw.page2 && !sw.mixed);
    e.run(30 * FRAME);
    // The menu's title, "The Oregon Trail", in white across the top: lit dots on hi-res rows 18-28.
    size_t lit = 0;
    for (unsigned y = 18; y < 29; ++y)
        for (unsigned x = 0; x < 280; ++x)
            lit += e.dot(x, y) != VideoCard::rgba(0x000);
    CHECK(lit > 800);
    CHECK(!e.s->disk(0)->changed());       // it only read
}

namespace
{
    /// The System Master's HELLO goes on to run a menu: let it load, then Ctrl-Reset, which DOS
    /// takes back to the ] prompt.
    void to_dos_prompt(IIe &e)
    {
        for (int i = 0; i < 40 && (i < 8 || e.s->disk2()->status().spinning); ++i)
            e.run(60 * FRAME);
        e.chord({L_CTRL}, DEL);
        e.run(60 * FRAME);
    }
} // namespace

TEST(dos_saves_a_program_and_the_disk_reads_back_as_sectors)
{
    if (!have(TRAIL_DSK) || !have(P5))
        return;
    IIe e(IIE, TRAIL_DSK);
    REQUIRE(e.ok());
    to_dos_prompt(e);
    const auto settle = [&] {
        for (int i = 0; i < 30 && (i < 2 || e.s->disk2()->status().spinning); ++i)
            e.run(60 * FRAME);
    };
    e.type("UNLOCK AIRPLANE\r");                 // the disk is full: room first, the DOS way
    settle();
    e.type("DELETE AIRPLANE\r");
    settle();
    e.type("NEW\r");                             // the menu's program is still in memory
    e.type("10 PRINT 4\r");
    e.type("SAVE OOONE\r");
    settle();
    CHECK(!e.screen_has("DISK FULL"));
    CHECK(e.s->disk(0)->changed());
    e.type("CATALOG\r");                        // DOS reads back what it wrote: a key at each full screen
    for (int page = 0; page < 4 && !e.screen_has(" A 002 OOONE"); ++page)
    {
        e.run(4 * 60 * FRAME);                     // a screenful, then DOS waits for a key
        if (!e.screen_has(" A 002 OOONE"))
            e.type(" ");
    }
    CHECK(e.screen_has(" A 002 OOONE"));      // an Applesoft program of 2 sectors, not locked
    if (!e.screen_has(" A 002 OOONE"))
        e.dump();

    std::vector<uint8_t> sectors;
    std::string error;
    CHECK(e.s->disk(0)->to_sectors(DiskImage::Format::Dos, sectors, error)); // still plain sectors
    std::string names;
    for (unsigned s = 15; s >= 1; --s)          // the catalog: track 17, its entries' names at +$03 + 35n
        for (unsigned n = 0; n < 7; ++n)
            for (unsigned k = 0; k < 30; ++k)
                names += static_cast<char>(sectors[(17 * 16 + s) * 256 + 0x0B + 35 * n + 3 + k] & 0x7F);
    CHECK(names.find("OOONE") != std::string::npos);
}

TEST(a_write_protected_disk_says_so_to_dos)
{
    if (!have(TRAIL_DSK) || !have(P5))
        return;
    IIe e(IIE, TRAIL_DSK);
    REQUIRE(e.ok());
    e.s->disk(0)->set_write_protected(true);
    to_dos_prompt(e);
    e.type("SAVE OOONE\r");
    for (int i = 0; i < 30 && (i < 2 || e.s->disk2()->status().spinning); ++i)
        e.run(60 * FRAME);
    CHECK(e.screen_has("WRITE PROTECTED"));
    CHECK(!e.s->disk(0)->changed());
}

int main()
{
    if (!have(IIE))
    {
        std::printf("SKIP: no %s (the owner's Apple IIe ROMs; see fixtures/apple2e/index.txt)\n", IIE.c_str());
        return SKIP;
    }
    return run_all();
}
