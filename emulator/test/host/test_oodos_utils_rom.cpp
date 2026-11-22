/**
 * @file test_oodos_utils_rom.cpp
 * @brief Leaving OODOS for the ROM BIOS: function 75 (O_ROM), by the CCP's WOZMON, APPLE2 and DIAG (hw/oodos.md, "OODOS's own"; hw/diag.md, plan 2).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), and the
 * CCP's two built-ins of ours that call function 75 typed at its prompt:
 *
 *   - WOZMON: the screen cleared, then the ROM's monitor, with W3 and VEC back
 *     on the ROM's block (63) and windows 0-2 on RAM blocks 0-2, and it answers;
 *   - APPLE2 with no IIe ROM in ROM A's images 0-3: it says so, the return
 *     code $FF00, and OODOS goes on;
 *   - APPLE2 with one: the machine restarts in Apple II mode, at the ROM's
 *     reset vector. The ROM here is a stand-in made by the test, not Apple's:
 *     the byte the ROM BIOS checks ($FBB3 = $06) and a reset vector to a loop;
 *     in image 0, or only in image 2 (B5: function 75 looks where the running
 *     ROM BIOS's $C806 does, ROM A's images from 0.16 on, the cartridge before:
 *     its version gate, by the major then the minor);
 *   - APPLE2 n (B5: function 75 with A = 3, X = n, interface 11, through the
 *     ROM's dispatching entry $C818): Apple II mode with image n even with
 *     another good image before it; image n not a IIe ROM, it says so (or,
 *     under a ROM BIOS before 0.16, that it needs 0.16), the return code a
 *     failure, and OODOS goes on; any other tail is the command and '?';
 *     a program's 75 with A = 3 gives $FF $FF for no IIe ROM or X of 4 or
 *     more, $FF $FB under an older ROM BIOS, $FE and $FD as A = 1 does;
 *   - with the IIe ROM but no DMA controller (backplane.machine less its
 *     `dma` line): APPLE2 says so and OODOS goes on, and a program's
 *     function 75 comes back with A = $FF, X = $FE, where with no IIe ROM
 *     it's X = $FF; the ROM's APPLE would have gone to its monitor;
 *   - with the IIe ROM and the DMA controller but a revision 1 video card, or
 *     none (backplane.machine less its `video` and `aperture` lines): APPLE2
 *     says the revision 2 card is needed and OODOS goes on, function 75 comes
 *     back with A = $FF, X = $FD (review R1: the ROM's APPLE refuses there too,
 *     into its monitor). OODOS boots on both;
 *   - DIAG (function 75 with A = 2, interface 10; hw/diag.md's plan 2, D3c): N
 *     is the prompt again with the card's image as it was; Y runs the ROM's
 *     diagnostics, quick, from their first lines on the serial line to their
 *     end, and a key boots OODOS from the card again; [FULL] and [ASK] (or a
 *     start of either) are the run's options, any other option or a file is
 *     the command and '?'; a
 *     program's record written and left in a changed buffer just before 75
 *     (DIAGWB.COM, oodos/sys/test/diagwb.s) is on the card after the boot;
 *     with flash block 23 erased (a machine whose second flash line blanks it)
 *     DIAG says there are no diagnostics, the return code $FF00, and a
 *     program's 75 with A = 2 comes back with A = $FF, X = $FC;
 *   - B7: function 75's Apple II mode from a program that left WIN1H $0E and
 *     MAP's AP8 clear (W1HTEST, oodos/sys/test/w1htest.s) reaches its
 *     trampoline with WIN1H 0 and AP8 set again.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <unistd.h>

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
                        unsigned video_revision = 2, const std::array<std::string, 4> &images = {})
            : CcpRig(std::move(apple_rom), machine_text, {}, video_revision, images)
        {
            // Without the DMA controller the card's sectors don't move and each one waits out SDIO's time
            // limit, so the boot takes longer than CcpRig's 3 s: its prompt, given longer.
            if (!ok && !machine_text.empty() && out.find("OODOS 0.15") != std::string::npos)
                ok = until("\r\nA>", ms(10000), out.size());
        }
        uint8_t nb(unsigned reg) { return machine->bus().read(static_cast<uint16_t>(0xC680 + reg)); }
    };

    const std::string CLEARED = "\x1b[H\x1b[2J"; ///< ESC E on the serial line

    constexpr uint16_t LOOP = 0xFA00; ///< the stand-in ROM's reset vector: JMP to itself

    /// A file of the test's own in the temporary directory, named for this process, removed when it goes out of
    /// scope (on a REQUIRE's early return too), so no case leaves a file behind.
    struct TempFile
    {
        std::string path;
        TempFile(const std::string &name, const void *data, size_t size)
            : path((std::filesystem::temp_directory_path() /
                    ("test_oodos_utils_rom_" + std::to_string(::getpid()) + "_" + name)).string())
        {
            std::ofstream(path, std::ios::binary).write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        }
        TempFile(const TempFile &) = delete;
        TempFile &operator=(const TempFile &) = delete;
        ~TempFile() { std::remove(path.c_str()); }
    };

    /// A stand-in IIe ROM (16 KB, $C000-$FFFF), a TempFile: the IIe's ID byte and a loop at reset, at `loop`
    /// (LOOP unless two images must be told apart).
    std::unique_ptr<TempFile> stand_in_rom(uint16_t loop = LOOP)
    {
        std::vector<char> rom(0x4000, static_cast<char>(0xEA));
        rom[0x3BB3] = 0x06;                                     // $FBB3: a IIe, to the ROM BIOS
        rom[loop - 0xC000 + 0] = static_cast<char>(0x4C);       // JMP loop
        rom[loop - 0xC000 + 1] = static_cast<char>(loop & 0xFF);
        rom[loop - 0xC000 + 2] = static_cast<char>(loop >> 8);
        rom[0x3FFC] = static_cast<char>(loop & 0xFF);            // RESET
        rom[0x3FFD] = static_cast<char>(loop >> 8);
        return std::make_unique<TempFile>("stand_in_iie_" + std::to_string(loop) + ".rom", rom.data(), rom.size());
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

    /// backplane.machine with flash block 23 erased: a second flash line over its 16 KB, which answers
    /// there (the later mapping wins), so the ROM has no diagnostics.
    std::string no_diag_machine()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const std::string after = "name=rom_b";
        const size_t at = text.find(after);
        if (at == std::string::npos)
            return {};
        text.insert(text.find('\n', at) + 1, "rom   5C000-5FFFF  space=flash  module=4  name=blank23  -\n");
        return text;
    }

    /// A .COM by hand (the header, then $0300 on), for GO: function 75 with A = `a` (1, Apple II mode, by
    /// default; 2 the diagnostics, 3 Apple II mode with image X) and X = `x` (the diagnostics' options, or the
    /// image; critic 15: set, not as GO leaves it); if it comes back, A and X printed in hex by function 2 (so
    /// OODOS still answers), then the warm boot.
    std::vector<uint8_t> romtest(uint8_t a = 1, uint8_t x = 0)
    {
        std::vector<uint8_t> b = {
            0x4F, 0x4F, 0x44, 0x1A, 0x00, 0x03, 0x2E, 0x00, // magic, flags, page $03, size $2E
            0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, // no BSS, zero page $00-$10
            0xA9, a,                                        // 0300 LDA #a      1 Apple II mode, 2 the diagnostics, 3 image X
            0xA2, x,                                        // 0302 LDX #x      the options, or the image
            0xA0, 0x4B,                                     // 0304 LDY #75     O_ROM
            0x20, 0x03, 0x02,                               // 0306 JSR BDOS
            0x86, 0x10,                                     // 0309 STX $10
            0x20, 0x16, 0x03,                               // 030B JSR HEX     A
            0xA5, 0x10,                                     // 030E LDA $10
            0x20, 0x16, 0x03,                               // 0310 JSR HEX     X
            0x4C, 0x00, 0x02,                               // 0313 JMP WBOOT
            0x48,                                           // 0316 HEX: PHA
            0x4A, 0x4A, 0x4A, 0x4A,                         // 0317 LSR x4
            0x20, 0x21, 0x03,                               // 031B JSR NIB
            0x68,                                           // 031E PLA
            0x29, 0x0F,                                     // 031F AND #$0F
            0xC9, 0x0A,                                     // 0321 NIB: CMP #10
            0x90, 0x02,                                     // 0323 BCC +2
            0x69, 0x06,                                     // 0325 ADC #6      (C set: +7)
            0x69, 0x30,                                     // 0327 ADC #'0'
            0xA0, 0x02,                                     // 0329 LDY #2      C_WRITE
            0x4C, 0x03, 0x02,                               // 032B JMP BDOS
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
    CHECK(o.find("\r\nNo IIe ROM in the flash (the emulator: Settings, ROMs)\r\nA>") != std::string::npos);
    CHECK(r.cmd(":APPLE2").find("No IIe ROM") == std::string::npos); // the return code: a failure
    CHECK(!r.bridge().apple());
    CHECK_EQ(r.nb(2), 61);                                 // the BIOS's block back in W2 after the look
    CHECK(r.cmd("APPLE2").find("No IIe ROM") != std::string::npos); // and again: OODOS goes on
}

TEST(apple2_with_a_iie_rom_restarts_in_apple_ii_mode)
{
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
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
    const auto iie = stand_in_rom();
    RomRig r({iie->path}, text);
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC47F) != 0x82);         // DMA_ID: no controller answers
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
    const auto iie = stand_in_rom();
    RomRig r({iie->path}, text);
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
    const auto iie = stand_in_rom();
    RomRig r({iie->path}, {}, 1);
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
    const auto iie = stand_in_rom();
    RomRig r({iie->path}, text);
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC4FF) != 0x92);          // VIDEO_ID: no revision 2 card answers
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nApple II mode needs the revision 2 video card\r\nA>") != std::string::npos);
    CHECK(!r.bridge().apple());
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFD\r\nA>"));
    CHECK(r.cmd("APPLE2").find("revision 2 video card") != std::string::npos); // and again: OODOS goes on
}

// --- B5: the IIe ROM in ROM A's images, behind the ROM BIOS's version --------------------------------------

namespace
{
    /// Where cold.s keeps the running ROM BIOS's version (major, minor), in RAM.
    uint32_t romver_at() { return 31u * 0x4000u + sym("ROMVER"); }

    /// Typed `line` (APPLE2 by default) and waited for Apple II mode: true if the machine went there, at the
    /// stand-in's loop at `loop`.
    bool apple2_reaches_the_loop(RomRig &r, const std::string &line = "APPLE2", uint16_t loop = LOOP)
    {
        r.type(line + "\r");
        for (int i = 0; i < 40 && !r.bridge().apple(); ++i)
            r.run(ms(50));
        if (!r.bridge().apple())
            return false;
        r.run(ms(50));
        const uint16_t pc = r.cpu->regs().pc;
        return pc >= loop && pc < loop + 3;
    }

    constexpr uint16_t LOOP2 = 0xFA10; ///< a second stand-in's loop, to tell image 2 from image 0

    /// A byte programmed into ROM A at `offset` by the chip's own command sequence (the even chip's at
    /// $AAAA/$5554, the odd chip's at $AAAB/$5555), as the BIOS's flash update writes; true if it reads back.
    bool program_rom_a(RomRig &r, uint32_t offset, uint8_t value)
    {
        Device *a = r.machine->find("rom_a");
        if (!a)
            return false;
        const uint32_t odd = offset & 1u;
        a->write(0xAAAA + odd, 0xAA);
        a->write(0x5554 + odd, 0x55);
        a->write(0xAAAA + odd, 0xA0);
        a->write(offset, value);
        r.run(ms(1));                                       // past its program time
        return a->peek(offset) == value;
    }
} // namespace

TEST(apple2_finds_a_iie_rom_in_image_2_alone)
{
    const auto iie = stand_in_rom();
    RomRig r({}, {}, 2, {"", "", iie->path, ""});
    REQUIRE(r.ok);
    REQUIRE(r.phys(romver_at()) == 0 && r.phys(romver_at() + 1) == 19);
    CHECK(apple2_reaches_the_loop(r));
    CHECK(r.out.find("No IIe ROM") == std::string::npos);
}

TEST(function_75_under_a_rom_bios_before_0_16_looks_in_the_cartridge_not_rom_a)
{
    // Such a ROM's $C806 reads the cartridge: a IIe ROM in ROM A's image 0 isn't one it would take, so 75 says
    // no IIe ROM rather than jump to a $C806 that refuses with OODOS gone.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    r.poke(romver_at() + 1, 15);                           // the ROM BIOS 0.15, to OODOS
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
    CHECK(!r.bridge().apple());
    CHECK_EQ(r.nb(2), 61);                                 // the BIOS's block back in W2 after the look
    r.poke(romver_at() + 1, 0xFF);                         // $FF $FF, not found: old too
    r.poke(romver_at(), 0xFF);
    r.poke_com(romtest());
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
}

TEST(function_75_under_a_rom_bios_1_0_looks_in_rom_a)
{
    // The gate compares the major first: 1.0 is past 0.16 though its minor is below 16.
    const auto iie = stand_in_rom();
    RomRig r({}, {}, 2, {"", "", iie->path, ""});
    REQUIRE(r.ok);
    r.poke(romver_at(), 1);
    r.poke(romver_at() + 1, 0);
    CHECK(apple2_reaches_the_loop(r));
}

// --- B5: APPLE2 n, function 75 with A = 3 and X the image (interface 11), through the ROM's $C818 ---------

TEST(apple2_n_takes_image_n_though_image_0_is_good)
{
    // Case 25: two stand-ins told apart by their loops. Image 2 asked for, image 2's loop, not image 0's (the
    // first good one, which A = 1's path, $C806, would take).
    const auto iie0 = stand_in_rom();
    const auto iie2 = stand_in_rom(LOOP2);
    RomRig r({}, {}, 2, {iie0->path, "", iie2->path, ""});
    REQUIRE(r.ok);
    CHECK(apple2_reaches_the_loop(r, "APPLE2 2", LOOP2));
    CHECK(r.out.find("No IIe ROM") == std::string::npos);
}

TEST(function_75_with_a_3_takes_image_x)
{
    const auto iie0 = stand_in_rom();
    const auto iie2 = stand_in_rom(LOOP2);
    RomRig r({}, {}, 2, {iie0->path, "", iie2->path, ""});
    REQUIRE(r.ok);
    r.poke_com(romtest(3, 2));
    r.type("GO\r");
    for (int i = 0; i < 40 && !r.bridge().apple(); ++i)
        r.run(ms(50));
    REQUIRE(r.bridge().apple());
    r.run(ms(50));
    const uint16_t pc = r.cpu->regs().pc;
    CHECK(pc >= LOOP2 && pc < LOOP2 + 3);
}

TEST(apple2_n_with_image_n_erased_says_so_and_oodos_goes_on)
{
    // Case 27: image 0 good, image 1 not: APPLE2 1 says image 1 has none, and 75 with A = 3, X = 1 gives $FF $FF.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    const std::string o = r.cmd("APPLE2 1");
    CHECK(o.find("\r\nNo IIe ROM in flash image 1 (the emulator: Settings, ROMs)\r\nA>") != std::string::npos);
    CHECK(r.cmd(":APPLE2 1").find("No IIe ROM") == std::string::npos); // the return code: a failure
    CHECK(!r.bridge().apple());
    CHECK_EQ(r.nb(2), 61);                                 // the BIOS's block back in W2 after the look
    r.poke_com(romtest(3, 1));
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
    CHECK(r.cmd("APPLE2 3").find("No IIe ROM in flash image 3") != std::string::npos); // and again: OODOS goes on
    CHECK(!r.bridge().apple());
}

TEST(apple2_with_another_tail_is_the_command_and_a_question_mark)
{
    // Case 28: only one digit 0-3 is an image; anything else is refused before 75, with nothing done.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    CHECK_EQ(r.cmd("APPLE2 4"), std::string("APPLE2 4\r\nAPPLE2 4?\r\nA>"));
    CHECK_EQ(r.cmd("APPLE2 X"), std::string("APPLE2 X\r\nAPPLE2 X?\r\nA>"));
    CHECK_EQ(r.cmd("APPLE2 1 2"), std::string("APPLE2 1 2\r\nAPPLE2 1 2?\r\nA>"));
    CHECK_EQ(r.cmd("APPLE2 12"), std::string("APPLE2 12\r\nAPPLE2 12?\r\nA>"));
    CHECK_EQ(r.cmd("APPLE2 /"), std::string("APPLE2 /\r\nAPPLE2 /?\r\nA>")); // the character before '0'
    CHECK(!r.bridge().apple());
    CHECK(r.out.find("No IIe ROM") == std::string::npos);
}

TEST(function_75_with_a_3_and_x_4_or_more_gives_ff_ff)
{
    // Case 28's program half: X of 4 is no image. Block 4 holds the character sets: a $06 put where an image's
    // ID byte would be in it ($13BB3, set 1's) must not be taken for a IIe ROM, which would leave for $C818 with
    // A = 4 (refused there: the machine restarts).
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    REQUIRE(program_rom_a(r, 0x10000 + 0x3BB3, 0x06));
    for (uint8_t x : {uint8_t{4}, uint8_t{5}, uint8_t{0x80}, uint8_t{0xFF}})
    {
        r.poke_com(romtest(3, x));
        CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
    }
    CHECK(!r.bridge().apple());
    CHECK(r.cmd("DIR").find("No File") == std::string::npos); // OODOS still answers
}

TEST(function_75_with_a_3_under_a_rom_bios_before_0_16_gives_ff_fb)
{
    // Case 29: such a ROM has no $C818 (it is its START there): image 2 good, but 75 refuses before anything,
    // and APPLE2 2 says what it needs.
    const auto iie = stand_in_rom();
    RomRig r({}, {}, 2, {"", "", iie->path, ""});
    REQUIRE(r.ok);
    r.poke(romver_at() + 1, 15);                           // the ROM BIOS 0.15, to OODOS
    r.poke_com(romtest(3, 2));
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFB\r\nA>"));
    CHECK(!r.bridge().apple());
    const std::string o = r.cmd("APPLE2 2");
    CHECK(o.find("\r\nAPPLE2 2 needs ROM BIOS 0.16 or later\r\nA>") != std::string::npos);
    CHECK(r.cmd(":APPLE2 2").find("needs") == std::string::npos); // the return code: a failure
    CHECK(!r.bridge().apple());
    r.poke(romver_at(), 0xFF);                             // $FF $FF, not found: old too
    r.poke(romver_at() + 1, 0xFF);
    r.poke_com(romtest(3, 2));
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFB\r\nA>"));
}

TEST(apple2_n_under_a_rom_bios_1_0_takes_image_n)
{
    // Case 29a: the gate compares the major first, for A = 3 too: 1.0 is past 0.16.
    const auto iie0 = stand_in_rom();
    const auto iie2 = stand_in_rom(LOOP2);
    RomRig r({}, {}, 2, {iie0->path, "", iie2->path, ""});
    REQUIRE(r.ok);
    r.poke(romver_at(), 1);
    r.poke(romver_at() + 1, 0);
    CHECK(apple2_reaches_the_loop(r, "APPLE2 2", LOOP2));
}

TEST(apple2_under_a_rom_bios_before_0_16_says_it_looks_in_the_cartridge)
{
    // Case 29b (Q17): A = 1 under an older ROM looks in the cartridge, where there is none (the stand-in is in
    // ROM A's image 0): the message says where that ROM looks, and what to do.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    r.poke(romver_at() + 1, 15);
    const std::string o = r.cmd("APPLE2");
    CHECK(o.find("\r\nNo IIe ROM: this ROM BIOS looks in the cartridge (Settings: Use built-in)\r\nA>") !=
          std::string::npos);
    CHECK(!r.bridge().apple());
    r.poke(romver_at() + 1, 16);                           // and 0.16 again: the flash's message
    CHECK(r.cmd("APPLE2 1").find("No IIe ROM in flash image 1") != std::string::npos);
}

TEST(function_75_with_a_3_without_dma_or_the_revision_2_card_gives_fe_or_fd)
{
    // Case 30: A = 3 has A = 1's checks, in the ROM's order.
    const auto iie = stand_in_rom();
    {
        const std::string text = no_dma_machine();
        REQUIRE(!text.empty());
        RomRig r({iie->path}, text);
        REQUIRE(r.ok);
        r.poke_com(romtest(3, 0));
        CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFE\r\nA>"));
        CHECK(r.cmd("APPLE2 0").find("\r\nApple II mode needs the DMA controller\r\nA>") != std::string::npos);
        CHECK(!r.bridge().apple());
    }
    {
        RomRig r({iie->path}, {}, 1);
        REQUIRE(r.ok);
        r.poke_com(romtest(3, 0));
        CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFD\r\nA>"));
        CHECK(r.cmd("APPLE2 0").find("\r\nApple II mode needs the revision 2 video card\r\nA>") != std::string::npos);
        CHECK(!r.bridge().apple());
    }
}

TEST(function_75_with_a_4_still_gives_ff_ff)
{
    // Case 31: the table's bound is 4 (A = 0-3); with a good image 0 a bound of 5 would send A = 4 to Apple II mode.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    for (uint8_t a : {uint8_t{4}, uint8_t{5}, uint8_t{0xFF}})
    {
        r.poke_com(romtest(a, 0));
        CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFF\r\nA>"));
    }
    CHECK(!r.bridge().apple());
}

// --- DIAG: function 75 with A = 2 (hw/diag.md's plan 2, D3c) -------------------------------------------------

namespace
{
    const std::string DIAGQ = "The ROM's diagnostics test all of memory and overwrite it: OODOS stops,\r\n"
                              "M:'s files are lost, and the machine boots again at the end. Go on (Y/N)?";
    const std::string TITLE = "OO-1 ROM DIAGNOSTICS";
    const std::string KEYS = "EXAM OR ESC: MONITOR\r"; ///< the end's keys line, after RESULT

    /// The diagnostics' form on the serial line, from `from`: the header's line with the BIOS's version and
    /// the pass, which ends in the run's options (QUICK or FULL, then ASK). Empty if it didn't come within 3 s.
    std::string form_line(RomRig &r, size_t from)
    {
        if (!r.until(TITLE, ms(2000), from) || !r.until("PASS 1  ", ms(3000), from) ||
            !r.until("\r", ms(500), r.out.find("PASS 1  ", from)))
            return {};
        const size_t at = r.out.rfind('\r', r.out.find("PASS 1  ", from)) + 1;
        return r.out.substr(at, r.out.find('\r', at) - at);
    }
} // namespace

TEST(diag_and_n_is_the_prompt_again_with_the_card_unchanged)
{
    RomRig r;
    REQUIRE(r.ok);
    r.cmd("DIR");
    const std::vector<uint8_t> was = r.media->bytes();
    const std::string q = r.to("DIAG", "(Y/N)?");
    CHECK(q.find("DIAG\r\n" + DIAGQ) != std::string::npos);
    CHECK_EQ(r.cmd("N"), std::string("N\r\r\nA>"));            // as ERA's N
    CHECK(r.out.find(TITLE) == std::string::npos);
    CHECK(r.cmd(":CLEAR").find(CLEARED) != std::string::npos); // N is done, not a failure: the return code 0
    CHECK_EQ(r.nb(0), 31);                                 // OODOS's windows, as before
    CHECK_EQ(r.nb(3), 62);
    CHECK(r.media->bytes() == was);
}

TEST(diag_with_a_file_or_another_option_is_the_command_and_a_question_mark)
{
    RomRig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.cmd("DIAG [LOOP]"), std::string("DIAG [LOOP]\r\nDIAG [LOOP]?\r\nA>"));
    CHECK_EQ(r.cmd("DIAG A:X"), std::string("DIAG A:X\r\nDIAG A:X?\r\nA>"));
    CHECK_EQ(r.cmd("DIAG [FULLER]"), std::string("DIAG [FULLER]\r\nDIAG [FULLER]?\r\nA>"));
    CHECK_EQ(r.cmd("DIAG [FULL] [LOOP]"), std::string("DIAG [FULL] [LOOP]\r\nDIAG [FULL] [LOOP]?\r\nA>")); // every list
    CHECK(r.out.find("(Y/N)?") == std::string::npos);
}

TEST(diag_and_y_runs_the_roms_diagnostics_and_a_key_boots_oodos_again)
{
    RomRig r;
    REQUIRE(r.ok);
    r.to("DIAG", "(Y/N)?");
    const size_t from = r.out.size();
    r.type("Y\r");
    const std::string title = form_line(r, from);
    std::printf("  %s\n", title.c_str());
    CHECK(title.find("QUICK") != std::string::npos);
    CHECK(title.find(" ASK") == std::string::npos);
    CHECK(r.until(" 1  FLASH", ms(2000), from));
    const uint64_t t0 = r.cpu->cycles();
    REQUIRE(r.until(KEYS, ms(120'000), from));
    std::printf("  the run: %.1f s at 7.16 MHz\n", static_cast<double>(r.cpu->cycles() - t0) / HZ);
    const size_t result = r.out.find("RESULT: ", from);
    REQUIRE(result != std::string::npos);
    std::printf("  %s\n", r.out.substr(result, r.out.find('\r', result) - result).c_str());
    CHECK(r.out.find("RESULT: ALL PASSED", from) != std::string::npos);
    const size_t end = r.out.size();
    r.type("\r");                                         // a key: the boot, as after a power-on
    REQUIRE(r.until("BACKPLANE BIOS 0.19", ms(3000), end));
    REQUIRE(r.until("\r\nA>", ms(20'000), end));
    CHECK(r.cmd("DIR").find("No File") == std::string::npos); // OODOS again, A: there
    CHECK_EQ(r.nb(3), 62);
}

TEST(a_record_left_in_a_changed_buffer_just_before_diag_is_on_the_card_after_the_boot)
{
    // DIAGWB writes 0:DIAGWB.TXT's record 0 again and calls 75 with A = 2 without closing the file: the new
    // record is only in a changed buffer, which 75 writes back before it leaves (as 48), or it is lost.
    RomRig r;
    REQUIRE(r.ok);
    r.poke_com(read_bytes(SYS + "/diagwb.com"));
    const size_t from = r.out.size();
    r.type("GO\r");
    REQUIRE(r.until("LEAVING\r\n", ms(3000), from));
    REQUIRE(!form_line(r, from).empty());
    REQUIRE(r.until(KEYS, ms(120'000), from));
    const size_t end = r.out.size();
    r.type("\r");
    REQUIRE(r.until("\r\nA>", ms(20'000), end));
    const std::string o = r.cmd("TYPE DIAGWB.TXT");
    CHECK(o.find("AFTER\r\n") != std::string::npos);
    CHECK(o.find("BEFORE") == std::string::npos);
    if (o.find("AFTER") == std::string::npos)
        std::printf("  TYPE: '%s'\n", vis(o).c_str());
}

TEST(diag_full_and_ask_are_the_forms_options)
{
    for (const auto &[tail, full, ask] : {std::tuple<std::string, bool, bool>{" [FULL]", true, false},
                                          {" [ASK]", false, true},
                                          {" [FULL,ASK]", true, true},
                                          {" [ ASK FULL ]", true, true},
                                          {" [F,A]", true, true},  // shortened, as the utilities' options may be
                                          {" [FULL\tASK]", true, true}}) // a tab between them, as a blank
    {
        RomRig r;
        REQUIRE(r.ok);
        r.to("DIAG" + tail, "(Y/N)?");
        const size_t from = r.out.size();
        r.type("Y\r");
        const std::string title = form_line(r, from);
        std::printf("  DIAG%s: %s\n", tail.c_str(), title.c_str());
        CHECK_EQ(title.find("FULL") != std::string::npos, full);
        CHECK_EQ(title.find("QUICK") != std::string::npos, !full);
        CHECK_EQ(title.find(" ASK") != std::string::npos, ask);
        CHECK(title.find(" LOOP") == std::string::npos);
    }
}

TEST(diag_without_diagnostics_in_the_flash_says_so_and_75_gives_ff_fc)
{
    const std::string text = no_diag_machine();
    REQUIRE(!text.empty());
    RomRig r({}, text);
    REQUIRE(r.ok);
    r.to("DIAG", "(Y/N)?");
    const std::string o = r.cmd("Y");
    CHECK(o.find("Y\r\nNo diagnostics for this BIOS in the flash (BIOS 0.14 or later)\r\nA>") != std::string::npos);
    CHECK(r.out.find(TITLE) == std::string::npos);
    CHECK(r.cmd(":CLEAR").find(CLEARED) == std::string::npos); // the return code: a failure
    r.poke_com(romtest(2));
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFC\r\nA>"));
    r.to("DIAG", "(Y/N)?");                                // and again: OODOS goes on
    CHECK(r.cmd("Y").find("No diagnostics for this BIOS in the flash") != std::string::npos);
}

TEST(diag_under_a_rom_bios_version_other_than_block_23s_linked_one_says_so_and_75_gives_ff_fc)
{
    // Review T3: OODOS's own record of the ROM BIOS's version (cold.s's ROMVER, from the greeting) made 0.14, so
    // block 23's linked version, 0.19, isn't the running BIOS's: DIAGIN must refuse, as $C815 would.
    RomRig r;
    REQUIRE(r.ok);
    const uint32_t romver = 31u * 0x4000u + sym("ROMVER");
    REQUIRE(r.phys(romver) == 0 && r.phys(romver + 1) == 19);
    r.poke(romver + 1, 14);
    r.to("DIAG", "(Y/N)?");
    const size_t from = r.out.size();
    const std::string o = r.cmd("Y");
    CHECK(o.find("Y\r\nNo diagnostics for this BIOS in the flash (BIOS 0.14 or later)\r\nA>") != std::string::npos);
    CHECK(r.out.find(TITLE, from) == std::string::npos);
    CHECK(r.cmd(":CLEAR").find(CLEARED) == std::string::npos); // the return code: a failure
    r.poke_com(romtest(2));
    CHECK_EQ(r.cmd("GO"), std::string("GO\r\nFFFC\r\nA>"));
}

TEST(a_refusal_by_c815_after_oodos_has_left_restarts_the_machine)
{
    // Review T1: block 23 with the right signature and linked BIOS version but another DG_VERSION ($8009): OODOS
    // can't know the running BIOS's DG_VERSION, so it jumps; $C815's own check refuses and returns, and the
    // return address ROMENTRY (ROMDIAG before B5) left on the stack is $C800's: the machine restarts, and the card boots OODOS again.
    std::vector<uint8_t> rom = read_bytes(EMU_BIOS_ROM);
    REQUIRE(rom.size() >= 24u * 0x4000u);
    std::vector<uint8_t> b23(rom.begin() + 23 * 0x4000, rom.begin() + 24 * 0x4000);
    b23[9] = static_cast<uint8_t>(b23[9] + 1);
    const TempFile b23file("block23_other_version.bin", b23.data(), b23.size());
    const std::string &path = b23file.path;
    std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
    const size_t at = text.find("name=rom_b");
    REQUIRE(at != std::string::npos);
    text.insert(text.find('\n', at) + 1, "rom   5C000-5FFFF  space=flash  module=4  name=other23  " + path + "\n");
    RomRig r({}, text);
    REQUIRE(r.ok);
    r.to("DIAG", "(Y/N)?");
    const size_t from = r.out.size();
    r.type("Y\r");
    CHECK(r.until("BACKPLANE BIOS 0.19", ms(3000), from));
    CHECK(r.until("\r\nA>", ms(20'000), from));
    CHECK(r.out.find(TITLE, from) == std::string::npos);
    CHECK(r.cmd("DIR").find("No File") == std::string::npos); // OODOS again
}

TEST(romapple_leaves_win1h_0_for_the_roms_apple)
{
    // O6's ROMAPPLE part (B7, b7-plan.md §4): W1HTEST (oodos/sys/test/w1htest.s) puts WIN1H on $0E, clears MAP's
    // AP8 and calls function 75 with A = 1. ROMAPPLE clears WIN0H-WIN3H and VECH and sets MAP by the card (common.s's
    // WINH0: AP8 set, the revision 2 video card) before its trampoline at TRAMP ($0A00) goes to the ROM's APPLE;
    // Apple II mode starts.
    const auto iie = stand_in_rom();
    RomRig r({iie->path});
    REQUIRE(r.ok);
    constexpr uint16_t TRAMP = 0x0A00;
    bool at_tramp = false, on = false;
    uint8_t winh1 = 0xEE, map = 0xEE;
    r.watch = [&](uint16_t pc) {
        if (!at_tramp && pc == TRAMP && r.nb(0) == 0)
        {
            at_tramp = true;
            winh1 = r.bridge().winh(1);
            map = r.bridge().map();
        }
        if (!at_tramp && pc >= 0x0300 && pc < 0x0A00 && r.bridge().winh(1) == 0x0E)
            on = true; // W1HTEST's W1HON ran
    };
    start(r, "w1htest.com", " A");
    for (int i = 0; i < 40 && !r.bridge().apple(); ++i)
        r.run(ms(50));
    r.watch = nullptr;
    CHECK(on);
    CHECK(at_tramp);
    CHECK_EQ(winh1, 0);
    CHECK_EQ(map, 1); // set again: the revision 2 video card
    REQUIRE(r.bridge().apple());
    r.run(ms(50));
    const uint16_t pc = r.cpu->regs().pc;
    CHECK(pc >= LOOP && pc < LOOP + 3);
}

int main()
{
    return run_all();
}
