/**
 * @file test_oodos_e2e.cpp
 * @brief OODOS from the card: C80CR, OOLDR, OODOS.SYS, the CCP, HELLO.COM.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The committed card, sd/oodos.sparse (oodos/tools/make_card.sh), as the
 * owner would use it: backplane.machine boots to the monitor, C80CR loads
 * OOLDR, OOLDR loads OODOS.SYS from A:, and its stand-in CCP prompts A> and
 * runs NAME.COM from A: through the BDOS (Open, then Read). WRITEOO1 writes a
 * file there through it, and HELLO still runs after. test_oodos_boot covers
 * OOLDR's refusals, test_oodos_sys the system's parts; this is all of them
 * together.
 */

#include "harness.h"
#include "sd_images.h"

#include "fat_volume.h"
#include "sd_media.h"
#include "session.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60
    const std::string MACHINE = EMU_MACHINE_DIR "/backplane.machine";
    constexpr size_t SECTOR = 512, RECORD = 128, ENTRY = 32, HEADER = 16;
    constexpr uint8_t SENTINEL = 0x5A;
    constexpr unsigned GUARD = 32; // bytes past HELLO's image the monitor sets to SENTINEL first

    using Bytes = std::vector<uint8_t>;

    /// A file's bytes off the card, by the emulator's FAT reader; empty if it isn't there.
    Bytes file_of(const Bytes &card, const std::string &path)
    {
        MemoryMedia media(card, "card");
        std::string error;
        auto v = FatVolume::open(media, error);
        FatVolume::Entry e;
        std::vector<FatVolume::Extent> ext;
        if (!v || v->find(path, e) != FatVolume::Status::Ok || v->extents(e, ext) != FatVolume::Status::Ok)
            return {};
        Bytes out;
        for (const auto &x : ext)
            out.insert(out.end(), card.begin() + static_cast<std::ptrdiff_t>(x.card * SECTOR),
                       card.begin() + static_cast<std::ptrdiff_t>((x.card + x.count) * SECTOR));
        out.resize(e.size);
        return out;
    }

    /// A small file's records off an oodos-8m drive (4 KB blocks, 1024 entries, 16-bit
    /// pointers, EXM 1), user 0, its first directory entry only: enough for HELLO.COM.
    Bytes cpm_file(const Bytes &img, const std::string &name83)
    {
        constexpr unsigned BLS = 4096, ENTRIES = 1024;
        for (unsigned i = 0; i < ENTRIES; ++i)
        {
            const uint8_t *e = &img[i * ENTRY];
            std::string name(e + 1, e + 12);
            for (char &c : name)
                c = static_cast<char>(c & 0x7F); // the attributes off: HELLO.COM is SYS
            if (e[0] != 0 || e[12] > 1 || name != name83)
                continue;
            Bytes out;
            const unsigned records = (e[12] & 1) * 128 + e[15];
            for (unsigned r = 0; r < records; ++r)
            {
                const unsigned block = e[16 + 2 * (r / 32)] | e[17 + 2 * (r / 32)] << 8;
                const size_t at = size_t{block} * BLS + (r % 32) * RECORD;
                out.insert(out.end(), img.begin() + static_cast<std::ptrdiff_t>(at),
                           img.begin() + static_cast<std::ptrdiff_t>(at + RECORD));
            }
            return out;
        }
        return {};
    }

    struct Oo1
    {
        std::unique_ptr<Session> s;
        std::shared_ptr<MemoryMedia> card;
        std::string out;

        /// Run until `text` appears in the output after `from`, or `frames` pass.
        bool until(const std::string &text, size_t from, int frames = 600)
        {
            for (int i = 0; i < frames && !s->stopped(); ++i)
            {
                if (out.find(text, from) != std::string::npos)
                    return true;
                s->run(FRAME);
                out += s->take_output();
            }
            const bool found = out.find(text, from) != std::string::npos;
            if (!found)
                std::printf("  never saw '%s' in '%s'\n", text.c_str(), out.substr(from).c_str());
            return found;
        }
        /// Type `line` and wait for the next prompt; @return what came between.
        std::string command(const std::string &line)
        {
            const size_t from = out.size();
            s->send(line + "\r");
            if (!until("\nA>", from))
                return {};
            return out.substr(from);
        }
    };

    /// The committed card in the slot, booted to the monitor; `before` is typed there, then C80CR.
    Oo1 booted(const std::string &before = {})
    {
        Oo1 m;
        std::string error;
        m.s = Session::create({MACHINE, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        m.card = std::make_shared<MemoryMedia>(sd_image("oodos.sparse"), "card");
        if (!m.s || !m.s->insert_sd(m.card, error))
        {
            std::printf("  %s\n", error.c_str());
            m.s.reset();
            return m;
        }
        if (!m.until("BACKPLANE BIOS 0.9\r\\\r", 0, 120))
            return m;
        if (!before.empty())
        {
            m.s->send(before + "\r");
            for (int i = 0; i < 30; ++i) // the monitor takes the line (it prints no prompt after a store)
            {
                m.s->run(FRAME);
                m.out += m.s->take_output();
            }
        }
        const size_t from = m.out.size();
        m.s->send("C80CR\r");
        m.until("\nA>", from, 900);
        return m;
    }

    const Bytes &hello()
    {
        static const Bytes h = cpm_file(file_of(sd_image("oodos.sparse"), "/OODOS/A.IMG"), "HELLO   COM");
        return h;
    }
    unsigned image_size() { return hello()[6] | hello()[7] << 8; } // the header's size word (OH_SIZE)
    unsigned bss_size() { return hello()[8] | hello()[9] << 8; }   // and its BSS (OH_BSS), which HELLO writes
} // namespace

TEST(c80cr_boots_oodos_from_the_card_to_the_prompt)
{
    Oo1 m = booted();
    REQUIRE(m.s != nullptr);
    CHECK(m.out.find("\rOOLDR 0.2\r") != std::string::npos);
    CHECK(m.out.find("OODOS") != std::string::npos);
    CHECK(m.out.find("\nA>") != std::string::npos);
    CHECK(!m.s->stopped());
    // W3 and VEC on block 62 (at the prompt the CPU is in the BDOS, so W0-W2 are the system's)
    CHECK_EQ(m.s->peek(0xC683) & 0x3F, 62);
    CHECK_EQ(m.s->peek(0xC684) & 0x3F, 62);
}

TEST(the_ccp_runs_hello_com_from_a_and_comes_back)
{
    REQUIRE(hello().size() >= RECORD);
    const unsigned size = image_size();
    // The image follows the 16-byte header in the file, so it ends inside a record: some of record
    // 0 and part of the last, each copied through the CCP's buffer
    REQUIRE(size > RECORD - HEADER && (HEADER + size) % RECORD != 0 && HEADER + size <= hello().size());
    // GUARD bytes past the image and its BSS set to SENTINEL by the monitor before C80CR: neither the
    // load nor HELLO may touch them
    const unsigned guard = 0x300 + size + bss_size();
    char at[8];
    std::snprintf(at, sizeof at, "%X:", guard);
    std::string fill = at;
    for (unsigned i = 0; i < GUARD; ++i)
        fill += " 5A";
    Oo1 m = booted(fill);
    REQUIRE(m.s != nullptr);
    for (unsigned i = 0; i < GUARD; ++i)
        REQUIRE(m.s->peek_physical(guard + i) == SENTINEL);

    const std::string said = m.command("hello world");
    CHECK(said.find("Hello from OODOS\r\n") != std::string::npos);
    CHECK(said.find(" WORLD") != std::string::npos); // its command tail, printed back, in capitals (4a)
    // Exactly the image at $0300, nothing past it
    size_t wrong = 0;
    for (unsigned i = 0; i < size; ++i)
        wrong += m.s->peek_physical(static_cast<uint32_t>(0x300 + i)) != hello()[HEADER + i];
    CHECK_EQ(wrong, size_t{0});
    for (unsigned i = 0; i < GUARD; ++i)
        CHECK_EQ(m.s->peek_physical(guard + i), SENTINEL);

    // Again: the warm boot came back to a CCP that can load a second time
    const std::string again = m.command("HELLO");
    CHECK(again.find("Hello from OODOS\r\n") != std::string::npos);
}

TEST(a_name_not_on_a_is_name_question_mark)
{
    Oo1 m = booted();
    REQUIRE(m.s != nullptr);
    const std::string nosuch = m.command("NOSUCH");
    CHECK(nosuch.find("NOSUCH?") != std::string::npos);
    CHECK(m.command("TESTSYS").find("TESTSYS?") != std::string::npos); // TESTSYS.SYS, not .COM
    CHECK(m.command("TOOLONGNAME").find("TOOLONGNAME?") != std::string::npos);
    CHECK(m.command("HELLO").find("Hello from OODOS") != std::string::npos); // and it still runs things
}

TEST(writeoo1_writes_through_the_bdos_and_the_ccp_still_loads)
{
    Oo1 m = booted();
    REQUIRE(m.s != nullptr);
    CHECK(m.command("WRITEOO1").find("OO1.BIN WRITTEN\r\n") != std::string::npos);
    CHECK(m.command("HELLO").find("Hello from OODOS") != std::string::npos);
    CHECK(m.command("WRITEOO1").find("\nMAKE FF 08\r\n") != std::string::npos); // File Exists, returned to it
    // OO1.BIN's first entry, 256 records, on the card the Session wrote: byte i of the file
    // ((i >> 7) * 31 + i * 7 + 5) & 0xFF.
    const Bytes oo1 = cpm_file(file_of(m.card->bytes(), "/OODOS/A.IMG"), "OO1     BIN");
    REQUIRE(oo1.size() == 256 * RECORD);
    size_t wrong = 0;
    for (size_t i = 0; i < oo1.size(); ++i)
        wrong += oo1[i] != static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + 5) & 0xFF);
    CHECK_EQ(wrong, size_t{0});
}

int main()
{
    return run_all();
}
