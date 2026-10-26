/**
 * @file test_bios_sd.cpp
 * @brief The BIOS's SD card commands (backplane/bios/sd.s): C809R lists the card, C80CR boots /BOOT.BIN.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The BIOS on backplane.machine in a Session, typed at over serial as the
 * WebUI does, with the SD test images (sd/, made by mkfs.fat and mtools) in
 * the card's slot. fat12's BOOT.BIN is a one-sector program that prints
 * through $C80F and returns.
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
        if (!run_until(*s, out, "BACKPLANE BIOS 0.7\r\\\r"))
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
        // BOOT.BIN cut to 100 bytes: still one cluster, but no whole sector.
        std::vector<uint8_t> b = sd_image("fat12.sparse");
        REQUIRE(!b.empty());
        const size_t root = (1 + 2 * size_t{b[22]}) * 512;
        size_t at = 0;
        for (size_t i = root; i < root + 224 * 32 && !at; i += 32)
            if (std::string(reinterpret_cast<const char *>(&b[i]), 11) == "BOOT    BIN")
                at = i;
        REQUIRE(at != 0);
        b[at + 28] = 100;
        b[at + 29] = 0;
        auto s = booted("");
        REQUIRE(s != nullptr);
        std::string error;
        REQUIRE(s->insert_sd(b, "SHORT", error));
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

int main()
{
    return run_all();
}
