/**
 * @file test_rom_boot.cpp
 * @brief Boot the Apple-1 ROMs headless, type at them, read the screen.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * End to end through the same pieces the app uses (LoadApple1, Memory, the
 * core), with the terminal swapped for a scripted keyboard and a captured
 * display. This is the test for "the Applesoft ROM does not work": it hung
 * at $FCE8 sizing RAM, and printed nothing after E000R.
 *
 * The keyboard and display here repeat what Keyboard::Pull and
 * Display::update do to the PIA registers. Phase 3 moves both behind a
 * Pia6821 device and this copy goes away.
 *
 * ROMs are assembled from apple1/src by this test build when ca65 is on the
 * PATH; without it the cases skip.
 */

#include "harness.h"
#include "test_support.h"

#include "apple1.h"

#include <fstream>
#include <string>

using namespace ooe::test;
using namespace ooe::emu;
using namespace ooe::emu::apple1;

namespace
{
    /// Run `mem` (ROM already loaded) from reset, typing `keys` (CR as '\r');
    /// return the screen text. The display is serviced throughout: without
    /// that the ROM blocks in ECHO on its first character and nothing after
    /// it is exercised.
    ///
    /// Stops after `max_steps` instructions, or once every key is consumed and
    /// `settle` more instructions have run with nothing new printed.
    std::string session(Memory &mem, const std::string &keys, long max_steps = 40'000'000,
                        long settle = 2'000'000)
    {
        Cpu6502 c(mem);
        c.reset();

        std::string screen;
        size_t next_key = 0;
        long quiet = 0;
        for (long steps = 0; steps < max_steps; steps += 16)
        {
            for (int i = 0; i < 16; ++i)
                c.step();

            const uint8_t dsp = mem.read(DSP);
            if (dsp & 0x80)
            {
                const char ch = static_cast<char>(dsp & 0x7F);
                screen += ch == '\r' ? '\n' : ch;
                mem.poke(DSP, static_cast<uint8_t>(dsp & 0x7F));
                quiet = 0;
            }
            else if (next_key == keys.size() && (quiet += 16) > settle)
                break;

            // One key at a time, and only once the last one has been read.
            if (next_key < keys.size() && !(mem.read(KBDCR) & 0x80) && steps % 4096 == 0)
            {
                mem.poke(KBD, static_cast<uint8_t>(keys[next_key++] | 0x80));
                mem.poke(KBDCR, static_cast<uint8_t>(mem.read(KBDCR) | 0x80));
            }
        }
        return screen;
    }

    std::string session(const char *rom, const std::string &keys, long max_steps = 40'000'000,
                        long settle = 2'000'000)
    {
        apple1::Memory mem;
        if (!apple1::load_rom(mem, rom))
            return "<load failed>";
        return session(mem, keys, max_steps, settle);
    }

    bool have(const char *path)
    {
        return static_cast<bool>(std::ifstream(path));
    }

    /// True if `screen` contains `needle`; if not, prints the screen so the
    /// failure shows what the ROM did instead.
    bool contains(const std::string &screen, const char *needle)
    {
        if (screen.find(needle) != std::string::npos)
            return true;
        std::printf("  --- screen (wanted \"%s\") ---\n%s\n  ---\n", needle, screen.c_str());
        return false;
    }

    int skipped = 0;
} // namespace

TEST(woz_monitor_examines_memory)
{
    if (!have(EMU_IBASIC_ROM))
        return void(++skipped);
    // FF00 is the first byte of Woz Monitor itself: CLD.
    const std::string screen = session(EMU_IBASIC_ROM, "FF00\r", 2'000'000, 200'000);
    CHECK(contains(screen, "FF00: D8"));
}

TEST(no_phantom_key_at_reset)
{
    if (!have(EMU_IBASIC_ROM))
        return void(++skipped);
    // Woz Monitor stores $A7 to KBDCR at reset. If that sets key-ready, the
    // monitor reads a $00 "key" into its input buffer before any typing.
    // (The first version of this case never serviced the display, so the
    // monitor sat in ECHO and the case passed with the bug present.)
    apple1::Memory mem;
    REQUIRE(apple1::load_rom(mem, EMU_IBASIC_ROM));
    mem.poke(0x0200, 0xEE);
    const std::string screen = session(mem, "", 400'000, 200'000);
    REQUIRE(contains(screen, "\\\n")); // reached the prompt, so input was polled
    CHECK_EQ(mem.read(0x0200), 0xEE);
}

TEST(integer_basic_runs_a_program)
{
    if (!have(EMU_IBASIC_ROM))
        return void(++skipped);
    const std::string screen = session(EMU_IBASIC_ROM, "E000R\r10 PRINT 2+3\r20 END\rRUN\r");
    CHECK(contains(screen, "RUN\n5\n"));
}

TEST(integer_basic_error_message_is_complete)
{
    if (!have(EMU_IBASIC_ROM))
        return void(++skipped);
    // Each message's last character is stored without bit 7; it used to vanish.
    const std::string screen = session(EMU_IBASIC_ROM, "E000R\r10 PRINT 1\rRUN\r");
    CHECK(contains(screen, "END ERR"));
}

TEST(applesoft_starts_and_evaluates)
{
    if (!have(EMU_APPLESOFT_ROM))
        return void(++skipped);
    // Applesoft prints no leading space for positives (unlike Commodore's).
    const std::string screen = session(EMU_APPLESOFT_ROM, "E000R\rPRINT 2+3\r");
    CHECK(contains(screen, "]PRINT 2+3\n5\n"));
}

TEST(applesoft_runs_floating_point)
{
    if (!have(EMU_APPLESOFT_ROM))
        return void(++skipped);
    const std::string screen = session(EMU_APPLESOFT_ROM, "E000R\r10 PRINT SQR(2)\rRUN\r");
    CHECK(contains(screen, "1.41421356"));
}

int main()
{
    const int rc = run_all();
    if (rc == 0 && skipped == static_cast<int>(ooe::test::registry().size()))
    {
        std::printf("SKIP: ROMs not built (needs ca65/ld65 on PATH at configure time)\n");
        return SKIP;
    }
    return rc;
}
