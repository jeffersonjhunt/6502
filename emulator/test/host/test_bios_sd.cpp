/**
 * @file test_bios_sd.cpp
 * @brief The BIOS's SD card commands (backplane/bios/sd.s): C809R lists the card, C80CR boots /BOOT.BIN.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The BIOS on backplane.machine in a Session, typed at over serial as the
 * WebUI does, with the SD test images (sd/, made by mkfs.fat and mtools) in
 * the card's slot. fat12's BOOT.BIN is a one-sector program that prints
 * through $C80F and returns. Break during C80CR stops the read and its DMA.
 *
 * And the flash update (flash.s, C818R then F, step B5), from cards made here
 * (sd_images.h's make_card) with stand-ins for a IIe ROM, never Apple's: the
 * image and set written byte for byte, nothing outside them, the commit byte
 * last, the image not good from before its first erase (a Break there, a
 * chip that doesn't answer), a verify that erases again, the files checked
 * before any erase, the run-time refusal of a block outside ROM A 0-5, a chip
 * left half-way put back to reading, QUIET and the windows set however it is
 * entered. Those use the Flash's test-only hooks, so the WebAssembly build
 * leaves them out.
 */

#include "harness.h"
#include "scratch.h"
#include "sd_images.h"

#include "flash.h"
#include "session.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60

    /// @return the path of `name` in the suite's scratch directory (scratch.h)
    std::string tmp(const std::string &name)
    {
        static const ScratchDir dir("test_bios_sd");
        return dir.file(name);
    }

    /// backplane.machine with its video line at revision 2, written as `name` in the scratch directory. The BIOS
    /// is built for revision 2 (hw/video-rev2.md, proposed), and the file still says rev=1 until the BIOS, OODOS
    /// and the demo move together; once it says rev=2 this copies it as it is.
    std::string rev2_machine(const std::string &name)
    {
        std::ifstream in(EMU_MACHINE_DIR "/backplane.machine");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t line = text.find("\nvideo ");
        const size_t rev = line == std::string::npos ? line : text.find(" rev=1", line);
        if (rev != std::string::npos && rev < text.find('\n', line + 1))
            text.replace(rev, 6, " rev=2");
        const std::string path = tmp(name);
        std::ofstream(path) << text;
        return path;
    }
    const std::string MACHINE = rev2_machine("test_bios_sd_rev2.machine");

    bool run_until(Session &s, std::string &out, const std::string &want, int frames = 120)
    {
        for (int i = 0; i < frames; ++i)
        {
            s.run(FRAME);
            out += s.take_output();
            if (out.find(want) != std::string::npos)
                return true;
        }
        std::printf("  never saw '%s' in '%s'\n", want.c_str(), out.c_str());
        return false;
    }

    /// fat12's image with BOOT.BIN's size in its directory entry changed to `size`: its one cluster stays.
    std::vector<uint8_t> boot_bin_cut_to(uint16_t size)
    {
        std::vector<uint8_t> b = sd_image("fat12.sparse");
        CHECK(!b.empty());
        if (b.empty())
            return b;
        const size_t root = (1 + 2 * size_t{b[22]}) * 512;
        size_t at = 0;
        for (size_t i = root; i < root + 224 * 32 && !at; i += 32)
            if (std::string(reinterpret_cast<const char *>(&b[i]), 11) == "BOOT    BIN")
                at = i;
        CHECK(at != 0);
        b[at + 28] = static_cast<uint8_t>(size);
        b[at + 29] = static_cast<uint8_t>(size >> 8);
        return b;
    }

    /// Booted to the monitor, then `image` (if any) put in the slot: a card there at power-on is booted (AUTOBOOT).
    std::unique_ptr<Session> booted(const std::string &image, const std::string &machine = MACHINE)
    {
        std::string error;
        auto s = Session::create({machine, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!s)
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        std::string out;
        if (!run_until(*s, out, "BACKPLANE BIOS 0.20\r\\\r"))
            return nullptr;
        if (!image.empty() && !s->insert_sd(sd_image(image), image, error))
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        return s;
    }

    /// Powered on with `card` already in the slot, as AUTOBOOT finds it; `out` gets what it says.
    std::unique_ptr<Session> powered_on_with(const std::vector<uint8_t> &card, std::string &out, const std::string &machine = MACHINE)
    {
        std::string error;
        auto s = Session::create({machine, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!s || !s->insert_sd(card, "card", error))
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        if (!run_until(*s, out, "BACKPLANE BIOS 0.20\r"))
            return nullptr;
        return s;
    }
    std::unique_ptr<Session> powered_on_with(const std::string &image, std::string &out)
    {
        return powered_on_with(sd_image(image), out);
    }

    /// backplane.machine without the line that starts `card` ("\nvideo "): written as `path`, for `booted`.
    std::string machine_without(const std::string &card, const std::string &path)
    {
        std::ifstream in(MACHINE);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t at = text.find("\n" + card + " ");
        CHECK(at != std::string::npos);
        if (at != std::string::npos)
            text.erase(at + 1, text.find('\n', at + 1) - at);
        std::ofstream(path) << text;
        return path;
    }

    constexpr int SPLASH_FRAMES = 360; // the splash's 306 and some
    const std::string BOOTING = "\rBOOTING THE SD CARD: A KEY FOR THE MONITOR\r";
} // namespace

TEST(at_power_on_a_card_with_boot_bin_is_booted_once_the_splash_is_over)
{
    std::string out;
    auto s = powered_on_with("fat12.sparse", out);
    REQUIRE(s != nullptr);
    REQUIRE(run_until(*s, out, BOOTING));
    CHECK_EQ(s->peek(0xC489) & 0x01, 0x01); // the splash still on: not yet
    for (int i = 0; i < 60; ++i)
        s->run(FRAME), out += s->take_output();
    CHECK(out.find("BOOTED FROM SD") == std::string::npos); // a second on: still waiting
    REQUIRE(run_until(*s, out, "\rBOOTED FROM SD\r", SPLASH_FRAMES));
    CHECK_EQ(s->peek(0xC489) & 0x01, 0x00); // the splash over first
    REQUIRE(run_until(*s, out, "\\\r"));  // it returned: the monitor
    CHECK_EQ(s->peek_physical(0x01000), 0xA2);
    CHECK_EQ(out.find("\\\r"), out.size() - 2); // no monitor before it: the boot came first
}

TEST(a_key_during_the_splash_is_the_monitor_instead)
{
    std::string out;
    auto s = powered_on_with("fat12.sparse", out);
    REQUIRE(s != nullptr);
    REQUIRE(run_until(*s, out, BOOTING));
    s->send("X");                                       // taken: not the monitor's
    REQUIRE(run_until(*s, out, BOOTING + "\\\r"));
    for (int i = 0; i < SPLASH_FRAMES; ++i)
        s->run(FRAME), out += s->take_output();
    CHECK(out.find("BOOTED FROM SD") == std::string::npos);
    s->send("C80CR\r");                                // and the card boots as ever from there
    REQUIRE(run_until(*s, out, "\rBOOTED FROM SD\r"));
}

TEST(at_power_on_a_card_without_boot_bin_is_the_monitor_saying_nothing)
{
    std::string out;
    auto s = powered_on_with("fat16.sparse", out);
    REQUIRE(s != nullptr);
    REQUIRE(run_until(*s, out, "\\\r"));
    CHECK_EQ(out.substr(out.find("BACKPLANE")), std::string("BACKPLANE BIOS 0.20\r\\\r")); // no error, no wait
    s->send("C809R\r");                                // and C809R still says what it finds
    REQUIRE(run_until(*s, out, "\\\r"));
}

TEST(at_power_on_a_boot_bin_that_cant_be_booted_says_why)
{
    std::string out;
    auto s = powered_on_with(boot_bin_cut_to(0), out); // there, but empty: not "nothing to boot"
    REQUIRE(s != nullptr);
    REQUIRE(run_until(*s, out, "\rBOOT.BIN IS EMPTY\r\\\r"));
    CHECK(out.find(BOOTING) == std::string::npos);
}

TEST(with_no_video_card_a_key_has_two_seconds_before_the_boot)
{
    const std::string path = machine_without("video", tmp("test_bios_sd_novideo.machine"));
    {
        std::string out;
        auto s = powered_on_with(sd_image("fat12.sparse"), out, path);
        REQUIRE(s != nullptr);
        REQUIRE(run_until(*s, out, BOOTING));
        for (int i = 0; i < 60; ++i) // a second: still waiting
            s->run(FRAME), out += s->take_output();
        CHECK(out.find("BOOTED FROM SD") == std::string::npos);
        REQUIRE(run_until(*s, out, "\rBOOTED FROM SD\r", 240)); // and then it boots, with no key
    }
    {
        std::string out;
        auto s = powered_on_with(sd_image("fat12.sparse"), out, path);
        REQUIRE(s != nullptr);
        REQUIRE(run_until(*s, out, BOOTING));
        s->send("X");
        REQUIRE(run_until(*s, out, BOOTING + "\\\r"));
        for (int i = 0; i < 240; ++i)
            s->run(FRAME), out += s->take_output();
        CHECK(out.find("BOOTED FROM SD") == std::string::npos);
    }
    std::remove(path.c_str());
}

TEST(c809_lists_the_card_and_its_root)
{
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    s->send("C809R\r");
    std::string out;
    REQUIRE(run_until(*s, out, "BOOT.BIN 00000200\r\\"));
    CHECK(out.find("\rCARD fat12.sparse 00000B40 SECTORS\r") != std::string::npos); // 1,440 KB
    CHECK(out.find("\rHELLO.TXT 00000015\r") != std::string::npos);
    CHECK(out.find("\rA long file name.dsk 00004E20\r") != std::string::npos);
    CHECK(out.find("\rDISKS/\r") != std::string::npos);
    // The 71-character name, as the listing cut it: 56.
    CHECK(out.find("\rThis name is longer than the fifty-six bytes a listing k 00000015\r") != std::string::npos);
}

TEST(c809_on_a_fat32_card_in_a_partition)
{
    auto s = booted("fat32.sparse");
    REQUIRE(s != nullptr);
    s->send("C809R\r");
    std::string out;
    REQUIRE(run_until(*s, out, "MANY/\r\\"));
    CHECK(out.find("\rIMAGES/\r") != std::string::npos);
    CHECK(out.find("00014000 SECTORS") != std::string::npos); // 40 MB
}

TEST(c80c_boots_boot_bin_from_the_card_by_dma)
{
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    s->send("C80CR\r");
    std::string out;
    REQUIRE(run_until(*s, out, "\rBOOTED FROM SD\r"));
    REQUIRE(run_until(*s, out, "\\\r")); // it returned to the monitor
    CHECK_EQ(s->peek_physical(0x01000), 0xA2);   // LDX #0, where it was loaded
    CHECK_EQ(s->peek(0xC410) & 0x01, 0x01);       // channel 0 reached terminal count
    CHECK(s->sd()->unit(1).mounted);
    CHECK_EQ(s->sd()->unit(1).name, std::string("BOOT.BIN"));
}

TEST(c80c_says_why_it_cannot_boot)
{
    {
        auto s = booted("fat16.sparse"); // no BOOT.BIN
        REQUIRE(s != nullptr);
        s->send("C80CR\r");
        std::string out;
        CHECK(run_until(*s, out, "\rNOT ON THE CARD: /BOOT.BIN\r\\"));
    }
    {
        auto s = booted("");
        REQUIRE(s != nullptr);
        std::string error;
        REQUIRE(s->insert_sd(boot_bin_cut_to(0), "EMPTY", error));
        s->send("C80CR\r");
        std::string out;
        CHECK(run_until(*s, out, "\rBOOT.BIN IS EMPTY\r\\"));
    }
    {
        auto s = booted(""); // the slot empty
        REQUIRE(s != nullptr);
        s->send("C80CR\r");
        std::string out;
        CHECK(run_until(*s, out, "\rNO CARD IN THE SLOT\r\\"));
        s->send("C809R\r");
        CHECK(run_until(*s, out, "\rNO CARD IN THE SLOT\r\\\r"));
    }
    {
        std::vector<uint8_t> blank(64 * 512); // a card with no FAT
        auto s = booted("");
        REQUIRE(s != nullptr);
        std::string error;
        REQUIRE(s->insert_sd(blank, "BLANK", error));
        s->send("C80CR\r");
        std::string out;
        CHECK(run_until(*s, out, "\rSD ERROR 09\r\\"));
    }
}

TEST(c80c_boots_a_boot_bin_shorter_than_a_sector)
{
    // 100 bytes: its sector runs on into the cluster's slack, so the program (a few dozen bytes) loads whole.
    auto s = booted("");
    REQUIRE(s != nullptr);
    std::string error;
    REQUIRE(s->insert_sd(boot_bin_cut_to(100), "SHORT", error));
    s->send("C80CR\r");
    std::string out;
    CHECK(run_until(*s, out, "\rBOOTED FROM SD\r"));
    CHECK_EQ(s->sd()->unit(1).sectors, uint64_t{1});
}

TEST(c80c_puts_the_windows_back_before_it_loads)
{
    // A program left W1 and W2 elsewhere; C80CR's DMA writes physical $01000 on and calls CPU $1000.
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    s->send("C681: 07 09\r");
    std::string out;
    REQUIRE(run_until(*s, out, "C681: 07 09\r"));
    s->run(FRAME);
    CHECK_EQ(s->peek(0xC682), 0x09);
    s->send("C80CR\r");
    out.clear();
    REQUIRE(run_until(*s, out, "\rBOOTED FROM SD\r"));
    CHECK_EQ(s->peek(0xC680), 0x00);
    CHECK_EQ(s->peek(0xC681), 0x01);
    CHECK_EQ(s->peek(0xC682), 0x02);
}

TEST(c80c_says_so_when_the_card_goes_while_it_sends_the_path)
{
    // Taken out once MOUNT waits for its sector: PUTBUF must see the command end, not wait for DATA forever.
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    s->send("C80CR\r");
    const uint8_t waiting = SdCard::BUSY | SdCard::DATA_READY;
    bool caught = false;
    for (int i = 0; i < 200'000 && !caught; ++i)
    {
        s->run(4);
        caught = s->sd()->commands() == 1 && (s->sd()->peek(SdCard::STATUS) & waiting) == waiting;
    }
    REQUIRE(caught);
    s->eject_sd();
    std::string out;
    CHECK(run_until(*s, out, "\rSD ERROR 01\r\\"));
}

TEST(c809_waits_for_a_card_to_start_up)
{
    auto s = booted("");
    REQUIRE(s != nullptr);
    std::string error;
    REQUIRE(s->insert_sd(sd_image("fat12.sparse"), "LATE", error)); // starting up: 50 ms
    s->send("C809R\r");
    std::string out;
    CHECK(run_until(*s, out, "\rCARD LATE 00000B40 SECTORS\r"));
}

TEST(a_machine_without_the_card_says_so)
{
    std::ifstream in(MACHINE);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t sd = text.find("\nsd ");
    REQUIRE(sd != std::string::npos);
    text.erase(sd + 1, text.find('\n', sd + 1) - sd);
    const std::string path = tmp("test_bios_sd_nocard.machine");
    std::ofstream(path) << text;
    auto s = booted("", path);
    REQUIRE(s != nullptr);
    s->send("C809R\r");
    std::string out;
    CHECK(run_until(*s, out, "\rNO SD CARD\r\\"));
    std::remove(path.c_str());
}

TEST(break_during_c80c_stops_the_read_and_its_dma)
{
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    s->send("C80CR\r");
    bool reading = false;                        // the READ written: the 3rd command, after MOUNT and IDENTIFY
    for (int i = 0; i < 200'000 && !reading; ++i)
    {
        s->run(4);
        reading = s->sd()->commands() == 3;
    }
    REQUIRE(reading);
    REQUIRE(s->dma()->peek_channel(0, DmaController::CTRL) & 1);     // channel 0 armed
    s->usb()->key(UsbCard::PAUSE, true);         // Pause/Break
    s->run(300);                                 // Break's own work, before the card's first byte
    s->usb()->key(UsbCard::PAUSE, false);
    CHECK_EQ(s->dma()->peek_channel(0, DmaController::CTRL) & 1, 0); // channel 0 stopped
    CHECK_EQ(s->sd()->peek(SdCard::CTRL), 0);    // and the card's DRQ0 off
    std::string out;
    REQUIRE(run_until(*s, out, "\\\r"));
    CHECK(out.find("BOOTED FROM SD") == std::string::npos);
    s->send("C80CR\r");                          // the card takes commands again
    CHECK(run_until(*s, out, "\rBOOTED FROM SD\r"));
}

TEST(break_puts_back_a_card_the_monitor_moved_so_c80c_still_boots)
{
    // b2-plan.md §4 item 11: channel 0's CARD poked to module 8 (none) at the monitor, then Break: QUIET puts
    // every CARD back to its reset module, so C80C's channel 0 is granted to the SD card again. Must fail if
    // QUIET leaves CARD alone: LOADRUN's wait has no timeout, so the boot hangs.
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    std::string out;
    s->send("C47E: 71\r");                       // SELECT 0, the master on
    REQUIRE(run_until(*s, out, "\rC47E: 71"));   // (the monitor says what the byte reads)
    s->send("C40C: 08\r");                       // channel 0's CARD: no module
    REQUIRE(run_until(*s, out, "\rC40C: 08"));
    REQUIRE(s->dma()->peek_channel(0, DmaController::CARD) == 0x08);
    s->usb()->key(UsbCard::PAUSE, true);         // Pause/Break
    s->run(FRAME);
    s->usb()->key(UsbCard::PAUSE, false);
    REQUIRE(run_until(*s, out, "\\\r"));
    CHECK_EQ(s->dma()->peek_channel(0, DmaController::CARD), 0x07);
    CHECK_EQ(s->peek(0xC47E), 0x71);             // CONTROL as the monitor's poke left it
    s->send("C80CR\r");
    CHECK(run_until(*s, out, "\rBOOTED FROM SD\r"));
    CHECK_EQ(s->peek_physical(0x01000), 0xA2);   // loaded where it belongs
}

TEST(a_wide_left_on_channel_0_by_the_monitor_is_cleared_by_break)
{
    // b4-plan.md, the owner's Q11: since B4 WIDE is live, and the SD card's DATA takes bytes, so a WIDE the monitor
    // (or WOZMON under OODOS) leaves on channel 0 would turn each of C80C's word grants into one byte. An accepted
    // gap that Break recovers: QUIET writes every CARD whole, WIDE 0. Must fail if QUIET leaves CARD alone.
    auto s = booted("fat12.sparse");
    REQUIRE(s != nullptr);
    std::string out;
    s->send("C47E: 71\r");                       // SELECT 0, the master on
    REQUIRE(run_until(*s, out, "\rC47E: 71"));
    s->send("C40C: 17\r");                       // channel 0's CARD: mass storage, WIDE
    REQUIRE(run_until(*s, out, "\rC40C: 17"));
    REQUIRE(s->dma()->peek_channel(0, DmaController::CARD) == 0x17);
    s->usb()->key(UsbCard::PAUSE, true);         // Pause/Break
    s->run(FRAME);
    s->usb()->key(UsbCard::PAUSE, false);
    REQUIRE(run_until(*s, out, "\\\r"));
    CHECK_EQ(s->dma()->peek_channel(0, DmaController::CARD), 0x07); // WIDE cleared
    s->send("C80CR\r");
    CHECK(run_until(*s, out, "\rBOOTED FROM SD\r"));
    CHECK_EQ(s->peek_physical(0x01000), 0xA2);   // loaded where it belongs, and it ran: every byte came
}

#ifdef EMU_FAULT_HOOKS // the flash's test-only busy times and fault (native test builds)
// ---- the flash update (flash.s, step B5): C818R, F -----------------------------------------------------------

namespace
{
    using fat_card::File;
    using fat_card::make_card;
    using Bytes = std::vector<uint8_t>;

    constexpr uint32_t IMAGE = 0x4000, SETS = 0x10000, SET = 0x2000;
    constexpr uint32_t ID_AT = 0x3BB3; // $FBB3 in an image: $06 for a IIe's
    constexpr int UPDATE_FRAMES = 900;  // the longest update here, with a slow program time, and some

    /// A stand-in for a IIe ROM, never Apple's: 16 KB of a pattern seeded by `seed`, $06 at $FBB3 (or `id`),
    /// and a reset vector to $FA62, whose code stores `mark` at $0300 and loops.
    Bytes fake_rom(uint8_t seed, uint8_t mark, uint8_t id = 0x06, size_t size = 0x4000)
    {
        Bytes b(size);
        for (size_t i = 0; i < size; ++i)
            b[i] = static_cast<uint8_t>(i * 13 + seed);
        if (size >= 0x4000)
        {
            b[ID_AT] = id;
            b[0x3FFC] = 0x62;
            b[0x3FFD] = 0xFA;
            const uint8_t reset[] = {0xA9, mark, 0x8D, 0x00, 0x03, 0x4C, 0x67, 0xFA}; // LDA #mark; STA $0300; JMP *
            std::copy(std::begin(reset), std::end(reset), b.begin() + 0x3A62);
        }
        return b;
    }
    Bytes chars(size_t size, uint8_t mul, uint8_t add)
    {
        Bytes b(size);
        for (size_t i = 0; i < size; ++i)
            b[i] = static_cast<uint8_t>(i * mul + add);
        return b;
    }

    Flash *rom_a(Session &s) { return dynamic_cast<Flash *>(s.device("rom_a")); }
    Flash *rom_b(Session &s) { return dynamic_cast<Flash *>(s.device("rom_b")); }
    Bytes all_of(const Flash &f)
    {
        Bytes b(f.size());
        for (uint32_t i = 0; i < f.size(); ++i)
            b[i] = f.peek(i);
        return b;
    }
    /// Offsets in [from, to) of `f` that differ from `want` (want[0] at `from`), counted.
    size_t differ(const Flash &f, uint32_t from, const Bytes &want)
    {
        size_t n = 0;
        for (size_t i = 0; i < want.size(); ++i)
            n += f.peek(from + static_cast<uint32_t>(i)) != want[i];
        return n;
    }
    /// Offsets outside [lo1, hi1) and [lo2, hi2) where `now` differs from `before`.
    size_t outside(const Bytes &before, const Bytes &now, uint32_t lo1, uint32_t hi1, uint32_t lo2 = 0, uint32_t hi2 = 0)
    {
        size_t n = 0;
        for (uint32_t i = 0; i < before.size(); ++i)
            if (!(i >= lo1 && i < hi1) && !(i >= lo2 && i < hi2))
                n += before[i] != now[i];
        return n;
    }

    /// Booted to the monitor, then a card of `files` in the slot.
    std::unique_ptr<Session> with_files(const std::vector<File> &files)
    {
        auto s = booted("");
        std::string error;
        if (s && !s->insert_sd(make_card(files), "card", error))
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        return s;
    }

    /// C818R, F, the image's digit: up to the question before the writing, which `out` then holds.
    bool ask(Session &s, std::string &out, unsigned image)
    {
        s.send("C818R\r");
        if (!run_until(s, out, "ANY OTHER KEY MONITOR "))
            return false;
        s.send("F");
        if (!run_until(s, out, "WHICH IIE IMAGE, 0-3? "))
            return false;
        s.send(std::string(1, static_cast<char>('0' + image)));
        return run_until(s, out, "(Y/N)? ", UPDATE_FRAMES);
    }
    /// The whole update of `image`, Y to the question; `out` gets what it said, to the monitor's prompt after.
    bool update(Session &s, std::string &out, unsigned image)
    {
        if (!ask(s, out, image))
            return false;
        out.clear();
        s.send("Y");
        return run_until(s, out, "\r\\", UPDATE_FRAMES);
    }

    /// $C818's $A1 with A = `image`, from a program at $0300: Apple II mode at the stand-in's loop, its mark at $0300.
    void apple_with(Session &s, uint8_t image)
    {
        const uint8_t code[] = {0xA9, image, 0xA2, 0xA1, 0xA0, 0x5A, 0x20, 0x18, 0xC8, 0x00};
        for (size_t i = 0; i < sizeof code; ++i)
            s.write_physical(static_cast<uint32_t>(0x300 + i), code[i]);
        std::string out;
        s.send("300R\r");
        for (int i = 0; i < 120 && !s.apple_mode(); ++i)
            s.run(FRAME);
        s.run(10 * FRAME);
    }

    const Bytes NEW_ROM = fake_rom(5, 0xC1), OLD_ROM = fake_rom(77, 0xD1), NEW_SET = chars(0x2000, 7, 3);
} // namespace

TEST(the_flash_update_writes_image_1_and_set_1_from_the_card_byte_for_byte_at_both_parities)
{
    // b5-plan.md case 18. The program time made slower than the time an unpolled pair takes to send (critic 5),
    // so an update that didn't poll each chip after its byte would write into a busy chip and lose bytes.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}, {"/APPLE2E1.CHR", NEW_SET}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    a->set_busy_cycles(1000, 0, 0);
    std::string out;
    REQUIRE(ask(*s, out, 1));
    CHECK(out.find("\r/APPLE2E1.ROM 16 KB\r/APPLE2E1.CHR 8 KB\rWRITE FLASH IMAGE 1 (Y/N)? ") != std::string::npos);
    out.clear();
    s->send("Y");
    REQUIRE(run_until(*s, out, "\r\\", UPDATE_FRAMES));
    CHECK(out.find("Y\rFLASH IMAGE 1 WRITTEN\r\\") != std::string::npos);
    CHECK_EQ(differ(*a, 1 * IMAGE, NEW_ROM), size_t{0});            // the image, every byte, even and odd
    CHECK_EQ(differ(*a, SETS + 1 * SET, NEW_SET), size_t{0});       // and its set
    CHECK_EQ(s->peek(0xC681), 0x01);                                 // W1 and W2 as Break leaves them
    CHECK_EQ(s->peek(0xC682), 0x02);
    a->set_busy_cycles(0, 0, 0);
    apple_with(*s, 1);                                               // and $C818's $A1 runs it
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x00300), 0xC1);
}

TEST(the_flash_update_over_an_image_already_there_leaves_the_new_file_exactly_and_nothing_else_changed)
{
    // Cases 19 and 23: both chips' sectors erased (four for the image, two for the set), each erase polled, and
    // nothing written outside image 1 and set 1: the other images and sets, blocks 6-31 and ROM B as they were.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}, {"/APPLE2E1.CHR", NEW_SET}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s), *b = rom_b(*s);
    REQUIRE(a != nullptr && b != nullptr);
    for (unsigned n = 0; n < 4; ++n)                                // something in every image and set
    {
        const Bytes img = fake_rom(static_cast<uint8_t>(40 + n), 0xD0), set = chars(SET, 3, static_cast<uint8_t>(n));
        for (uint32_t i = 0; i < IMAGE; ++i)
            a->poke(n * IMAGE + i, n == 1 ? OLD_ROM[i] : img[i]);
        for (uint32_t i = 0; i < SET; ++i)
            a->poke(SETS + n * SET + i, set[i]);
    }
    for (uint32_t i = 0; i < b->size(); i += 7)
        b->poke(i, static_cast<uint8_t>(i));
    const Bytes before_a = all_of(*a), before_b = all_of(*b);
    std::string out;
    REQUIRE(ask(*s, out, 1));
    out.clear();
    const uint64_t from = s->cycles();
    s->send("Y");
    REQUIRE(run_until(*s, out, "WRITTEN\r", UPDATE_FRAMES));
    std::printf("  the update's writing, Y to WRITTEN: %.2f s at 7.16 MHz\n",
                static_cast<double>(s->cycles() - from) / 7'159'090.0);
    REQUIRE(run_until(*s, out, "\r\\", UPDATE_FRAMES));
    CHECK(out.find("FLASH IMAGE 1 WRITTEN") != std::string::npos);
    CHECK_EQ(differ(*a, 1 * IMAGE, NEW_ROM), size_t{0});
    CHECK_EQ(differ(*a, SETS + 1 * SET, NEW_SET), size_t{0});
    CHECK_EQ(outside(before_a, all_of(*a), IMAGE, 2 * IMAGE, SETS + SET, SETS + 2 * SET), size_t{0});
    CHECK(all_of(*b) == before_b);
}

TEST(the_flash_update_stopped_part_way_leaves_the_image_not_good)
{
    // Case 20: every byte but the commit bytes first, so with the image's bytes all programmed and the set's
    // begun, its $FBB3 is still erased: an update stopped there (a Break, the power) leaves image 2 not good.
    auto s = with_files({{"/APPLE2E2.ROM", NEW_ROM}, {"/APPLE2E2.CHR", NEW_SET}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    std::string out;
    REQUIRE(ask(*s, out, 2));
    s->send("Y");
    bool begun = false;
    for (int i = 0; i < UPDATE_FRAMES * 20 && !begun; ++i)
    {
        s->run(FRAME / 20);
        begun = a->peek(SETS + 2 * SET + 8) == NEW_SET[8] && a->peek(SETS + 2 * SET + 9) == NEW_SET[9];
    }
    REQUIRE(begun);                                                  // the set's programming under way
    for (int i = 0; i < 10'000 && (a->busy(0) || a->busy(1)); ++i) // a busy chip reads its status, not its array
        s->run(4);
    REQUIRE(!a->busy(0) && !a->busy(1));
    CHECK_EQ(a->peek(2 * IMAGE + ID_AT), 0xFF);                      // the commit byte: not yet
    size_t written = 0;
    for (uint32_t i = 0; i < IMAGE; ++i)
        written += i != ID_AT && a->peek(2 * IMAGE + i) == NEW_ROM[i];
    CHECK_EQ(written, size_t{IMAGE - 1});                            // though every other byte is
    s->usb()->key(UsbCard::PAUSE, true);                             // Break, there
    s->run(FRAME);
    s->usb()->key(UsbCard::PAUSE, false);
    s->run(10 * FRAME);
    CHECK_EQ(a->peek(2 * IMAGE + ID_AT), 0xFF);
}

namespace
{
    /// Image 1 good (OLD_ROM, mark $D1) and image 3 good (mark $B3), the others erased: C806R takes the first good.
    void two_good_images(Flash &a)
    {
        const Bytes three = fake_rom(9, 0xB3);
        for (uint32_t i = 0; i < IMAGE; ++i)
        {
            a.poke(1 * IMAGE + i, OLD_ROM[i]);
            a.poke(3 * IMAGE + i, three[i]);
        }
    }
    /// C806R: Apple II mode from the first good image, its stand-in's mark at $0300.
    void c806r(Session &s)
    {
        s.send("C806R\r");
        for (int i = 0; i < 240 && !s.apple_mode(); ++i)
            s.run(FRAME);
        s.run(10 * FRAME);
    }
} // namespace

TEST(the_flash_update_broken_off_in_its_first_erase_leaves_the_image_not_good)
{
    // R1 (B5 review round 1): image 1 good before the update, and Break while the chips erase its first 8 KB
    // unit, which doesn't hold $FBB3. The sector erase lengthened (a slower part's, still under the poll's time
    // out) so the Break lands in it. The update made $FBB3 $00 before that erase, so image 1 is not good and
    // C806R takes image 3; erasing first would leave $06 there, over a half-erased image.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    two_good_images(*a);
    REQUIRE(OLD_ROM[0] != 0xFF && OLD_ROM[0x2000] != 0xFF);
    a->set_busy_cycles(0, 1'000'000, 0);                             // 140 ms a sector, the poll's 180
    std::string out;
    REQUIRE(ask(*s, out, 1));
    out.clear();
    s->send("Y");
    bool erasing = false;                                            // unit 0 erased (the array) and unit 1 not yet
    for (int i = 0; i < UPDATE_FRAMES * 20 && !erasing; ++i)
    {
        s->run(FRAME / 20);
        erasing = a->busy(0) && a->Rom::peek(1 * IMAGE) == 0xFF && a->Rom::peek(1 * IMAGE + 0x2000) == OLD_ROM[0x2000];
    }
    REQUIRE(erasing);
    s->usb()->key(UsbCard::PAUSE, true);                             // Break, there
    s->run(FRAME);
    s->usb()->key(UsbCard::PAUSE, false);
    REQUIRE(run_until(*s, out, "\\\r"));
    s->run(30 * FRAME);                                              // the erase over
    REQUIRE(!a->busy(0) && !a->busy(1));
    CHECK_NE(a->peek(1 * IMAGE + ID_AT), 0x06);                      // not good
    a->set_busy_cycles(0, 0, 0);
    c806r(*s);
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x00300), 0xB3);                       // image 3's
}

TEST(the_flash_update_with_a_chip_that_doesnt_answer_its_first_erase_leaves_the_image_not_good)
{
    // R1: image 1 good, and the chips' first sector erase far longer than the poll waits (0.84 s against
    // 180 ms), so the first unit's poll times out, and so does the erase again (the chips still busy, taking
    // no commands). It says so, and image 1's $FBB3 was made $00 before any erase: C806R takes image 3.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    two_good_images(*a);
    a->set_busy_cycles(0, 6'000'000, 0);
    std::string out;
    REQUIRE(update(*s, out, 1));
    CHECK(out.find("Y\rTHE FLASH DOESN'T ANSWER\r\\") != std::string::npos);
    s->run(60 * FRAME);                                              // the slow erase over
    REQUIRE(!a->busy(0) && !a->busy(1));
    CHECK_EQ(a->peek(1 * IMAGE + 0x2000), OLD_ROM[0x2000]);         // the second unit never erased
    CHECK_NE(a->peek(1 * IMAGE + ID_AT), 0x06);                      // not good
    a->set_busy_cycles(0, 0, 0);
    c806r(*s);
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x00300), 0xB3);                       // image 3's
}

TEST(the_flash_update_broken_off_in_its_erase_again_after_the_commit_leaves_the_image_not_good)
{
    // F1 (B5 review round 2): the erase again reached from the last verify, after the commit, with image 1 good
    // by then. The set's first byte won't program, so the verify after $FBB3's $06 finds it, and the update
    // erases again from ERASE, which makes $FBB3 $00 first there too. Break while the chips erase image 1's
    // first unit in that erase again (the sector erase lengthened, as above): image 1 is not good and C806R
    // takes image 3. Skipping that $00 on the erase again would leave $06 over a half-erased image.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}, {"/APPLE2E1.CHR", NEW_SET}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    const Bytes three = fake_rom(9, 0xB3);
    for (uint32_t i = 0; i < IMAGE; ++i)
        a->poke(3 * IMAGE + i, three[i]);
    REQUIRE(NEW_ROM[0] != 0xFF && NEW_SET[0] != 0xFF);
    a->set_program_fault(SETS + 1 * SET + 0);
    a->set_busy_cycles(0, 1'000'000, 0);
    std::string out;
    REQUIRE(ask(*s, out, 1));
    out.clear();
    s->send("Y");
    bool good = false, again = false;                                // $FBB3 seen $06, then the erase again's unit 0
    for (int i = 0; i < UPDATE_FRAMES * 20 && !again; ++i)
    {
        s->run(FRAME / 20);
        good = good || a->Rom::peek(1 * IMAGE + ID_AT) == 0x06;
        again = good && a->busy(0) && a->Rom::peek(1 * IMAGE) == 0xFF;
    }
    REQUIRE(good);
    REQUIRE(again);
    s->usb()->key(UsbCard::PAUSE, true);                             // Break, there
    s->run(FRAME);
    s->usb()->key(UsbCard::PAUSE, false);
    REQUIRE(run_until(*s, out, "\\\r"));
    s->run(30 * FRAME);                                              // the erase over
    REQUIRE(!a->busy(0) && !a->busy(1));
    CHECK_NE(a->peek(1 * IMAGE + ID_AT), 0x06);                      // not good
    a->clear_program_fault();
    a->set_busy_cycles(0, 0, 0);
    c806r(*s);
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x00300), 0xB3);                       // image 3's
}

TEST(the_flash_update_verifies_and_erases_again_a_byte_that_wont_program)
{
    // Case 21: a cell that won't take: said with its physical address, image 1 erased again (not good), and C806R
    // then takes image 3, the next good one.
    auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    const Bytes three = fake_rom(9, 0xB3);
    for (uint32_t i = 0; i < IMAGE; ++i)
        a->poke(3 * IMAGE + i, three[i]);
    REQUIRE(NEW_ROM[0x1234] != 0xFF);
    a->set_program_fault(1 * IMAGE + 0x1234);
    std::string out;
    REQUIRE(update(*s, out, 1));
    CHECK(out.find("\rFLASH IMAGE 1 NOT WRITTEN AT $F05234\r\\") != std::string::npos);
    size_t erased = 0;
    for (uint32_t i = 0; i < IMAGE; ++i)
        erased += a->peek(1 * IMAGE + i) == 0xFF;
    CHECK_EQ(erased, size_t{IMAGE});                                 // erased again: not good
    a->clear_program_fault();
    out.clear();
    s->send("C806R\r");
    for (int i = 0; i < 240 && !s->apple_mode(); ++i)
        s->run(FRAME);
    s->run(10 * FRAME);
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x00300), 0xB3);                       // image 3's
}

TEST(the_flash_update_checks_the_files_before_it_erases_anything)
{
    // Case 22. A missing .CHR erases set n (the VGA font's then); a .ROM of 15 KB, of 16,000 bytes (32 sectors,
    // so only the listing's size tells), not a IIe's, or a .CHR of 2 KB, changes nothing in the flash.
    auto s = with_files({{"/APPLE2E0.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    for (uint32_t i = 0; i < SET; ++i)
        a->poke(SETS + i, static_cast<uint8_t>(i));
    std::string out;
    REQUIRE(ask(*s, out, 0));
    CHECK(out.find("\rNO /APPLE2E0.CHR: SET 0 ERASED, ITS CHARACTERS THE VGA FONT'S\rWRITE FLASH IMAGE 0 (Y/N)? ") !=
          std::string::npos);
    out.clear();
    s->send("Y");
    REQUIRE(run_until(*s, out, "\r\\", UPDATE_FRAMES));
    CHECK(out.find("FLASH IMAGE 0 WRITTEN") != std::string::npos);
    CHECK_EQ(differ(*a, 0, NEW_ROM), size_t{0});
    CHECK_EQ(differ(*a, SETS, Bytes(SET, 0xFF)), size_t{0});         // set 0 erased

    const Bytes before = all_of(*a);
    const std::vector<std::pair<std::vector<File>, std::string>> bad = {
        {{{"/APPLE2E2.ROM", Bytes(NEW_ROM.begin(), NEW_ROM.begin() + 15 * 1024)}}, "\r/APPLE2E2.ROM ISN'T A IIE ROM\r\\"},
        {{{"/APPLE2E2.ROM", Bytes(NEW_ROM.begin(), NEW_ROM.begin() + 16000)}}, "\r/APPLE2E2.ROM ISN'T A IIE ROM\r\\"},
        {{{"/APPLE2E2.ROM", fake_rom(5, 0xC1, 0xEA)}}, "\r/APPLE2E2.ROM ISN'T A IIE ROM\r\\"},
        {{{"/APPLE2E2.ROM", NEW_ROM}, {"/APPLE2E2.CHR", chars(0x800, 1, 0)}}, "\r/APPLE2E2.CHR ISN'T 4 OR 8 KB\r\\"},
        {{}, "\rNOT ON THE CARD: /APPLE2E2.ROM\r\\"},
    };
    for (const auto &[files, said] : bad)
    {
        std::string error;
        s->eject_sd();
        REQUIRE(s->insert_sd(make_card(files), "bad", error));
        out.clear();
        s->send("C818R\r");
        REQUIRE(run_until(*s, out, "ANY OTHER KEY MONITOR "));
        s->send("F");
        REQUIRE(run_until(*s, out, "0-3? "));
        s->send("2");
        CHECK(run_until(*s, out, said.c_str(), UPDATE_FRAMES));
        CHECK(out.find("(Y/N)") == std::string::npos);
        CHECK(all_of(*a) == before);                                 // nothing erased
    }
}

TEST(the_flash_update_refuses_a_block_outside_rom_a_0_to_5_whatever_computed_it)
{
    // Case 23a (critic 4): the image number forced to 6, then 31, after its check (a test-only poke of flash.s's
    // FIMG, zero page $10, at the question): the one write routine refuses the block, and ROM A and ROM B are
    // unchanged.
    for (const auto &[image, said] : {std::pair<uint8_t, const char *>{6, "\rTHE FLASH UPDATE REFUSED BLOCK $06\r\\"},
                                      {31, "\rTHE FLASH UPDATE REFUSED BLOCK $1F\r\\"}})
    {
        auto s = with_files({{"/APPLE2E1.ROM", NEW_ROM}, {"/APPLE2E1.CHR", NEW_SET}});
        REQUIRE(s != nullptr);
        Flash *a = rom_a(*s), *b = rom_b(*s);
        REQUIRE(a != nullptr && b != nullptr);
        const Bytes before_a = all_of(*a), before_b = all_of(*b);
        std::string out;
        REQUIRE(ask(*s, out, 1));
        s->write_physical(0x00010, image);                           // FIMG
        out.clear();
        s->send("Y");
        CHECK(run_until(*s, out, said, UPDATE_FRAMES));
        CHECK(all_of(*a) == before_a);
        CHECK(all_of(*b) == before_b);
        CHECK(!a->busy(0) && !a->busy(1));
    }
}

TEST(the_flash_update_takes_images_0_to_3_alone)
{
    // Case 23's other half: 4 (or 5) at the image's question would be the character sets' block, which the
    // run-time check allows; the question takes only 0-3, and any other key is the monitor, nothing changed.
    auto s = with_files({{"/APPLE2E4.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    const Bytes before = all_of(*a);
    for (const char *key : {"4", "5", "X"})
    {
        std::string out;
        s->send("C818R\r");
        REQUIRE(run_until(*s, out, "ANY OTHER KEY MONITOR "));
        s->send("F");
        REQUIRE(run_until(*s, out, "0-3? "));
        s->send(key);
        CHECK(run_until(*s, out, (std::string("0-3? ") + key + "\r\\").c_str()));
        CHECK(out.find("(Y/N)") == std::string::npos);
    }
    s->run(60 * FRAME);
    CHECK(all_of(*a) == before);
}

TEST(the_flash_update_puts_a_chip_left_half_way_through_a_program_back_to_reading_first)
{
    // Case 23b (critic 3): the even chip left after $AA, $55, $A0 by someone's writes, so it takes the next write
    // to it, anywhere, as data to program. The update's first even write must not be the erase's $AA at $AAAA
    // (image 2's byte $2AAA): image 2 unchanged, image 0 right.
    auto s = with_files({{"/APPLE2E0.ROM", NEW_ROM}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    const Bytes before = all_of(*a);
    s->write_physical(0xF0AAAA, 0xAA);
    s->write_physical(0xF05554, 0x55);
    s->write_physical(0xF0AAAA, 0xA0);
    std::string out;
    REQUIRE(update(*s, out, 0));
    CHECK(out.find("FLASH IMAGE 0 WRITTEN") != std::string::npos);
    CHECK_EQ(differ(*a, 2 * IMAGE, Bytes(before.begin() + 2 * IMAGE, before.begin() + 3 * IMAGE)), size_t{0});
    CHECK_EQ(differ(*a, 0, NEW_ROM), size_t{0});
}

TEST(the_flash_update_runs_quiet_and_sets_its_windows_however_it_was_entered)
{
    // Case 23c (Q18, critic 12): entered with W2 on ROM A block 31, read-only, and the sound card's 6510 running,
    // the update stages in RAM blocks 58-59 all the same, writes image 0 right, and has stopped the 6510.
    auto s = with_files({{"/APPLE2E0.ROM", NEW_ROM}, {"/APPLE2E0.CHR", chars(0x1000, 5, 1)}});
    REQUIRE(s != nullptr);
    Flash *a = rom_a(*s);
    REQUIRE(a != nullptr);
    std::string out;
    s->send("C682: DF\r");
    REQUIRE(run_until(*s, out, "C682: "));
    s->send("C540: 02\r");                                           // SOUND_CTRL: RUN
    REQUIRE(run_until(*s, out, "C540: "));
    REQUIRE((s->peek(0xC540) & 0x02) != 0);
    REQUIRE(s->peek(0xC682) == 0xDF);
    REQUIRE(update(*s, out, 0));
    CHECK(out.find("FLASH IMAGE 0 WRITTEN") != std::string::npos);
    CHECK_EQ(differ(*a, 0, NEW_ROM), size_t{0});
    Bytes set = chars(0x1000, 5, 1);
    set.resize(SET, 0xFF);                                           // a 4 KB set, the rest erased
    CHECK_EQ(differ(*a, SETS, set), size_t{0});
    CHECK_EQ(s->peek(0xC540) & 0x02, 0x00);                          // QUIET stopped the 6510
}

#endif

int main()
{
    return run_all();
}
