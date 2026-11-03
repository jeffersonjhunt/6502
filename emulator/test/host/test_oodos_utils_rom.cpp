/**
 * @file test_oodos_utils_rom.cpp
 * @brief Leaving OODOS for the ROM BIOS: function 75 (O_ROM), WOZMON.COM and APPLE2.COM (hw/oodos.md, "OODOS's own").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the two
 * utilities put on M: and run from the CCP:
 *
 *   - WOZMON: the ROM's monitor, with W3 and VEC back on the ROM's block (63)
 *     and windows 0-2 on RAM blocks 0-2, and it answers;
 *   - APPLE2 with no IIe ROM in the cartridge: it says so, the return code
 *     $FF00, and OODOS goes on;
 *   - APPLE2 with one: the machine restarts in Apple II mode, at the ROM's
 *     reset vector. The ROM here is a stand-in made by the test, not Apple's:
 *     the byte the ROM BIOS checks ($FBB3 = $06) and a reset vector to a loop.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    /// A rig with WOZMON and APPLE2 on M:, the search chain the default drive, then M:.
    struct RomRig : CcpRig
    {
        explicit RomRig(std::vector<std::string> apple_rom = {}) : CcpRig(std::move(apple_rom))
        {
            if (!ok)
                return;
            put_util("wozmon", UTILS);
            put_util("apple2", UTILS);
            chain({13, 0});
        }
        uint8_t nb(unsigned reg) { return machine->bus().read(static_cast<uint16_t>(0xC680 + reg)); }
    };

    constexpr uint16_t LOOP = 0xFA00; ///< the stand-in ROM's reset vector: JMP to itself

    /// A stand-in IIe ROM (16 KB, $C000-$FFFF) in the build tree: the IIe's ID byte and a loop at reset.
    std::string stand_in_rom()
    {
        std::vector<char> rom(0x4000, static_cast<char>(0xEA));
        rom[0x3BB3] = 0x06;                                     // $FBB3: a IIe, to the ROM BIOS
        rom[LOOP - 0xC000 + 0] = static_cast<char>(0x4C);       // JMP LOOP
        rom[LOOP - 0xC000 + 1] = static_cast<char>(LOOP & 0xFF);
        rom[LOOP - 0xC000 + 2] = static_cast<char>(LOOP >> 8);
        rom[0x3FFC] = static_cast<char>(LOOP & 0xFF);            // RESET
        rom[0x3FFD] = static_cast<char>(LOOP >> 8);
        const std::string path = "stand_in_iie.rom";
        std::ofstream(path, std::ios::binary).write(rom.data(), static_cast<std::streamsize>(rom.size()));
        return path;
    }
} // namespace

TEST(wozmon_leaves_oodos_for_the_roms_monitor)
{
    RomRig r;
    REQUIRE(r.ok);
    REQUIRE(!r.to("WOZMON", "\r\\\r").empty());          // the ROM's Break: a CR, its prompt
    CHECK_EQ(r.nb(3), 0x40 | 63);                          // W3 and VEC on the ROM's copy, read-only
    CHECK_EQ(r.nb(4), 0x40 | 63);
    CHECK_EQ(r.nb(0), 0);                                  // windows 0-2 as at reset
    CHECK_EQ(r.nb(1), 1);
    CHECK_EQ(r.nb(2), 2);
    CHECK(!r.to("C683", "C683: 7F").empty());            // and the monitor answers
}

TEST(apple2_without_a_iie_rom_says_so_and_oodos_goes_on)
{
    RomRig r;
    REQUIRE(r.ok);
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nNo IIe ROM in the cartridge (the emulator: Settings, ROMs)\r\nA>") != std::string::npos);
    CHECK(r.cmd(":APPLE2").find("No IIe ROM") == std::string::npos); // the return code: a failure
    CHECK(!r.bridge().apple());
    CHECK_EQ(r.nb(2), 61);                                 // the BIOS's block back in W2 after the look
    CHECK(r.cmd("APPLE2").find("No IIe ROM") != std::string::npos); // and again: OODOS goes on
}

TEST(apple2_with_a_iie_rom_restarts_in_apple_ii_mode)
{
    RomRig r({stand_in_rom()});
    REQUIRE(r.ok);
    r.type("APPLE2\r");
    for (int i = 0; i < 40 && !r.bridge().apple(); ++i)
        r.run(ms(50));
    REQUIRE(r.bridge().apple());
    r.run(ms(50));
    const uint16_t pc = r.cpu->regs().pc;
    CHECK(pc >= LOOP && pc < LOOP + 3);                    // at the ROM's reset, as a IIe at power-on
    CHECK(r.out.find("No IIe ROM") == std::string::npos);
}
