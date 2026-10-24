/**
 * @file test_iie.cpp
 * @brief Apple II mode with the owner's real Apple IIe ROMs (fixtures/apple2e, gitignored).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane through a Session, as the WebUI drives it, with the enhanced
 * IIe ROM in the cartridge: C806R, then the IIe's own boot, Applesoft, the
 * USB keyboard as the IIe's, and its resets. The IIe's screen is read from
 * text page 1 in memory. Apple's ROMs are never in the repo: without
 * fixtures/apple2e (see its index.txt) the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "session.h"
#include "usb_card.h"

#include <fstream>
#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    const std::string ROMS = EMU_FIXTURE_DIR "/apple2e/";
    const std::string IIE = ROMS + "iie-enhanced-c000-ffff.bin";
    constexpr uint64_t FRAME = 17'030; // an Apple frame of cycles

    bool have(const std::string &path) { return std::ifstream(path).good(); }

    struct IIe
    {
        explicit IIe(const std::string &rom = IIE)
        {
            std::string error;
            Session::Config c{EMU_MACHINE_DIR "/backplane.machine", EMU_BIOS_ROM, ""};
            c.apple_rom = {rom};
            s = Session::create(c, error);
            if (!s)
            {
                std::printf("  %s\n", error.c_str());
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

int main()
{
    if (!have(IIE))
    {
        std::printf("SKIP: no %s (the owner's Apple IIe ROMs; see fixtures/apple2e/index.txt)\n", IIE.c_str());
        return SKIP;
    }
    return run_all();
}
