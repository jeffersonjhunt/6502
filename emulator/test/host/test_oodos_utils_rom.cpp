/**
 * @file test_oodos_utils_rom.cpp
 * @brief Leaving OODOS for the ROM BIOS: function 75 (O_ROM), by the CCP's WOZMON and APPLE2 (hw/oodos.md, "OODOS's own").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), and the
 * CCP's two built-ins of ours that call function 75 typed at its prompt:
 *
 *   - WOZMON: the screen cleared, then the ROM's monitor, with W3 and VEC back
 *     on the ROM's block (63) and windows 0-2 on RAM blocks 0-2, and it answers;
 *   - APPLE2 with no IIe ROM in the cartridge: it says so, the return code
 *     $FF00, and OODOS goes on;
 *   - APPLE2 with one: the machine restarts in Apple II mode, at the ROM's
 *     reset vector. The ROM here is a stand-in made by the test, not Apple's:
 *     the byte the ROM BIOS checks ($FBB3 = $06) and a reset vector to a loop;
 *   - with the IIe ROM but no DMA controller (backplane.machine less its
 *     `dma` line): APPLE2 says so and OODOS goes on, and a program's
 *     function 75 comes back with A = $FF, X = $FE, where with no IIe ROM
 *     it's X = $FF; the ROM's APPLE would have gone to its monitor;
 *   - with the IIe ROM and the DMA controller but a revision 1 video card, or
 *     none (backplane.machine less its `video` and `aperture` lines): APPLE2
 *     says the revision 2 card is needed and OODOS goes on, function 75 comes
 *     back with A = $FF, X = $FD (review R1: the ROM's APPLE refuses there too,
 *     into its monitor). OODOS boots on both.
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
    /// The CCP's rig, and the north bridge's registers.
    struct RomRig : CcpRig
    {
        explicit RomRig(std::vector<std::string> apple_rom = {}, const std::string &machine_text = {},
                        unsigned video_revision = 2)
            : CcpRig(std::move(apple_rom), machine_text, {}, video_revision)
        {
            // Without the DMA controller the card's sectors don't move and each one waits out SDIO's time
            // limit, so the boot takes longer than CcpRig's 3 s: its prompt, given longer.
            if (!ok && !machine_text.empty() && out.find("OODOS 0.8") != std::string::npos)
                ok = until("\r\nA>", ms(10000), out.size());
        }
        uint8_t nb(unsigned reg) { return machine->bus().read(static_cast<uint16_t>(0xC680 + reg)); }
    };

    const std::string CLEARED = "\x1b[H\x1b[2J"; ///< ESC E on the serial line

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

    /// backplane.machine without its DMA controller.
    std::string no_dma_machine()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const size_t at = text.find("\ndma ");
        if (at == std::string::npos)
            return {};
        text.erase(at + 1, text.find('\n', at + 1) - at);
        return text;
    }

    /// backplane.machine without its video card (the card's line and its aperture's).
    std::string no_video_machine()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        for (const char *line : {"\nvideo ", "\naperture "})
        {
            const size_t at = text.find(line);
            if (at == std::string::npos)
                return {};
            text.erase(at + 1, text.find('\n', at + 1) - at);
        }
        return text;
    }

    /// A .COM by hand (the header, then $0300 on), for GO: function 75 with A = 1; if it comes back, A and X
    /// printed in hex by function 2 (so OODOS still answers), then the warm boot.
    std::vector<uint8_t> romtest()
    {
        std::vector<uint8_t> b = {
            0x4F, 0x4F, 0x44, 0x1A, 0x00, 0x03, 0x2C, 0x00, // magic, flags, page $03, size $2C
            0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, // no BSS, zero page $00-$10
            0xA9, 0x01,                                     // 0300 LDA #1      Apple II mode
            0xA0, 0x4B,                                     // 0302 LDY #75     O_ROM
            0x20, 0x03, 0x02,                               // 0304 JSR BDOS
            0x86, 0x10,                                     // 0307 STX $10
            0x20, 0x14, 0x03,                               // 0309 JSR HEX     A
            0xA5, 0x10,                                     // 030C LDA $10
            0x20, 0x14, 0x03,                               // 030E JSR HEX     X
            0x4C, 0x00, 0x02,                               // 0311 JMP WBOOT
            0x48,                                           // 0314 HEX: PHA
            0x4A, 0x4A, 0x4A, 0x4A,                         // 0315 LSR x4
            0x20, 0x1F, 0x03,                               // 0319 JSR NIB
            0x68,                                           // 031C PLA
            0x29, 0x0F,                                     // 031D AND #$0F
            0xC9, 0x0A,                                     // 031F NIB: CMP #10
            0x90, 0x02,                                     // 0321 BCC +2
            0x69, 0x06,                                     // 0323 ADC #6      (C set: +7)
            0x69, 0x30,                                     // 0325 ADC #'0'
            0xA0, 0x02,                                     // 0327 LDY #2      C_WRITE
            0x4C, 0x03, 0x02,                               // 0329 JMP BDOS
        };
        return b;
    }
} // namespace

TEST(wozmon_leaves_oodos_for_the_roms_monitor)
{
    RomRig r;
    REQUIRE(r.ok);
    r.cmd("DIR");                                         // something on the screen
    // The CCP's CR LF, the clear, then the ROM's Break: its prompt, a CR.
    REQUIRE(!r.to("WOZMON", "\r\n" + CLEARED + "\\\r").empty());
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

TEST(apple2_without_the_dma_controller_says_so_and_oodos_goes_on)
{
    const std::string text = no_dma_machine();
    REQUIRE(!text.empty());
    RomRig r({stand_in_rom()}, text);
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC47F) != 0x81);         // DMA_ID: no controller answers
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nApple II mode needs the DMA controller\r\nA>") != std::string::npos);
    CHECK(o.find("No IIe ROM") == std::string::npos);
    CHECK(!r.bridge().apple());
    CHECK(r.cmd(":APPLE2").find("DMA") == std::string::npos); // the return code: a failure
    CHECK(r.cmd("APPLE2").find("needs the DMA") != std::string::npos); // and again: OODOS goes on
}

TEST(function_75_without_the_dma_controller_returns_ff_fe_and_oodos_goes_on)
{
    const std::string text = no_dma_machine();
    REQUIRE(!text.empty());
    RomRig r({stand_in_rom()}, text);
    REQUIRE(r.ok);
    r.poke_com(romtest());                                 // GO: no drive to load it from, as SDIO needs DMA
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFE\r\nA>"));
    CHECK(!r.bridge().apple());
    r.poke_com(romtest());                                 // (after a warm boot GO has no program)
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFE\r\nA>")); // and again: OODOS goes on
}

TEST(function_75_without_a_iie_rom_returns_ff_ff)
{
    RomRig r;
    REQUIRE(r.ok);
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
    CHECK(!r.bridge().apple());
}

// Review R1: the ROM's APPLE refuses without the revision 2 video card, before its DMA and IIe ROM checks,
// into its monitor; function 75 looks first, so a refusal comes back to OODOS. The IIe ROM and the DMA
// controller are there, so only the card's check can refuse.
TEST(apple2_on_a_revision_1_video_card_says_so_and_oodos_goes_on)
{
    RomRig r({stand_in_rom()}, {}, 1);
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC4FF) == 0x91);         // VIDEO_ID: revision 1
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nApple II mode needs the revision 2 video card\r\nA>") != std::string::npos);
    CHECK(o.find("No IIe ROM") == std::string::npos);
    CHECK(!r.bridge().apple());
    CHECK(r.cmd(":APPLE2").find("video card") == std::string::npos); // the return code: a failure
    CHECK(r.cmd("APPLE2").find("revision 2 video card") != std::string::npos); // and again: OODOS goes on
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFD\r\nA>"));
    CHECK(!r.bridge().apple());
}

TEST(apple2_without_a_video_card_says_so_and_oodos_goes_on)
{
    const std::string text = no_video_machine();
    REQUIRE(!text.empty());
    RomRig r({stand_in_rom()}, text);
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC4FF) != 0x92);          // VIDEO_ID: no revision 2 card answers
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nApple II mode needs the revision 2 video card\r\nA>") != std::string::npos);
    CHECK(!r.bridge().apple());
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFD\r\nA>"));
    CHECK(r.cmd("APPLE2").find("revision 2 video card") != std::string::npos); // and again: OODOS goes on
}

int main()
{
    return run_all();
}
