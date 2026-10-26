/**
 * @file test_bios_sd.cpp
 * @brief The BIOS's SD card commands (backplane/bios/sd.s): C809R lists the card, C80CR boots /BOOT.BIN.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The BIOS on backplane.machine in a Session, typed at over serial as the
 * WebUI does, with the SD test images (sd/, made by mkfs.fat and mtools) in
 * the card's slot. fat12's BOOT.BIN is a one-sector program that prints
 * through $C80F and returns. Break during C80CR stops the read and its DMA.
 */

#include "harness.h"
#include "sd_images.h"

#include "session.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60
    const std::string MACHINE = EMU_MACHINE_DIR "/backplane.machine";

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

    /// Booted to the monitor, with `image` (if any) in the slot.
    std::unique_ptr<Session> booted(const std::string &image, const std::string &machine = MACHINE)
    {
        std::string error;
        auto s = Session::create({machine, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!s)
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        if (!image.empty() && !s->insert_sd(sd_image(image), image, error))
        {
            std::printf("  %s\n", error.c_str());
            return nullptr;
        }
        std::string out;
        if (!run_until(*s, out, "BACKPLANE BIOS 0.9\r\\\r"))
            return nullptr;
        return s;
    }
} // namespace

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
    CHECK_EQ(s->peek(0xC440) & 0x01, 0x01);       // channel 0 reached terminal count
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
    const std::string path = "test_bios_sd_nocard.machine";
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
    REQUIRE(s->peek(0xC407) & 1);                // channel 0 armed
    s->usb()->key(UsbCard::PAUSE, true);         // Pause/Break
    s->run(300);                                 // Break's own work, before the card's first byte
    s->usb()->key(UsbCard::PAUSE, false);
    CHECK_EQ(s->peek(0xC407) & 1, 0);            // channel 0 stopped
    CHECK_EQ(s->sd()->peek(SdCard::CTRL), 0);    // and the card's DRQ0 off
    std::string out;
    REQUIRE(run_until(*s, out, "\\\r"));
    CHECK(out.find("BOOTED FROM SD") == std::string::npos);
    s->send("C80CR\r");                          // the card takes commands again
    CHECK(run_until(*s, out, "\rBOOTED FROM SD\r"));
}

int main()
{
    return run_all();
}
