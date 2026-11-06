/**
 * @file test_oodos_disk2.cpp
 * @brief OODOS's Disk II driver, O: and P:, phase 6c (hw/oodos.md, "The plan, phase 6").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_apple boots it (oodos_rig.h's CcpRig), on a backplane whose Disk II has
 * the owner's sequencer PROM, loaded from fixtures/apple2e at run time: the suite exits 77 without it
 * (nothing of it is in the repo, and nothing of it is printed). The disks are ours: the committed card's
 * /DISKS/CPM.DSK and BLANK.DSK (cpmtools's apple-do, make_card.sh), read off the card and written out by
 * DiskImage::from_sectors, as a .dsk is in the WebUI's drive. The oracle is the same image as an SD unit
 * (C:), and DiskImage::to_sectors() of the drive's disk after: the two must be the same bytes.
 */

#include "harness.h"
#include "oodos_rig.h"

#include "bus_cycle.h"
#include "disk2_card.h"
#include "disk_image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    const std::string P6 = EMU_FIXTURE_DIR "/apple2e/disk2-16-p6-sequencer-341-0028-a.bin";
    constexpr unsigned UNIT = 3;
    constexpr size_t IMAGE = DiskImage::SECTOR_IMAGE;
    constexpr uint16_t DISK2 = 0xC600;

    std::vector<uint8_t> gen(size_t n, unsigned seed)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF);
        return b;
    }
    std::vector<uint8_t> records(std::vector<uint8_t> d, uint8_t pad = 0)
    {
        d.resize((d.size() + RECORD - 1) / RECORD * RECORD, pad);
        return d;
    }
    void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }
    [[maybe_unused]] void hasnt(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) == std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  don't want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }

    /// The rig: PIP on M:, the search chain M: first; the card's apple-do images read off it (by mounting
    /// them as unit 3 in turn), CPM.DSK left mounted there as C:, and `o` and `p` in drives 1 and 2.
    struct Dr : CcpRig
    {
        std::vector<uint8_t> cpm, blank; ///< the card's images, 140 KB each
        explicit Dr(bool with_cpm = true, bool with_blank = true, std::vector<MFile> files = {})
            : CcpRig({}, {}, P6)
        {
            if (!ok)
                return;
            put_m(files);
            put_util("pip", UTILS);
            chain({13, 0});
            ok = mount(UNIT, "/DISKS/BLANK.DSK") == 0;
            blank = unit_image();
            ok = ok && mount(UNIT, "/DISKS/CPM.DSK") == 0;
            cpm = unit_image();
            if (with_cpm)
                insert(0, cpm);
            if (with_blank)
                insert(1, blank);
        }
        std::vector<uint8_t> unit_image()
        {
            std::vector<uint8_t> d;
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
            {
                const std::vector<uint8_t> b = read_unit(UNIT, s);
                d.insert(d.end(), b.begin(), b.end());
            }
            return d;
        }
        Disk2Card &card() { return *machine->disk2_card(); }
        std::shared_ptr<DiskImage> insert(unsigned drive, const std::vector<uint8_t> &image)
        {
            auto d = std::shared_ptr<DiskImage>(DiskImage::from_sectors(image, DiskImage::Format::Dos));
            card().insert(drive, d);
            return d;
        }
        /// The disk in `drive` as 140 KB of the file's sectors, or empty if it doesn't read as one.
        std::vector<uint8_t> drive_image(unsigned drive)
        {
            std::vector<uint8_t> img;
            std::string error;
            if (!card().disk(drive) || !card().disk(drive)->to_sectors(DiskImage::Format::Dos, img, error))
                return {};
            return img;
        }
        /// A command line to its prompt, a minute of machine time at most (the drive is slow).
        std::string c(const std::string &line) { return to(line, "\r\nA>", ms(60000)); }
        /// Run on (the motor's second, say).
        void wait(uint64_t cycles) { run(cycles); }
    };
} // namespace

TEST(o_reads_cpmtools_files_as_c_does)
{
    Dr r;
    REQUIRE(r.ok);
    std::string o = r.c("dir o:");
    has(o, "O: README   TXT : PATTERN  BIN");
    o = r.c("type o:readme.txt");
    has(o, "THIS IS AN APPLE CP/M DISK, MADE ON LINUX BY CPMTOOLS.\r\nOODOS READS IT AS AN SD DRIVE, /DISKS/CPM.DSK ON THE CARD.\r\n");
    const uint64_t t0 = r.cpu->cycles();
    r.c("pip m:=o:pattern.bin");
    const uint64_t took = r.cpu->cycles() - t0;
    std::printf("  PIP of 20,000 bytes from O: to M:: %.2f s\n", static_cast<double>(took) / HZ);
    CHECK(m_data(r, 0, "PATTERN BIN") == records(gen(20000, 5)));
    r.c("pip m:small.bin=o:small.bin[g2]");
    CHECK(m_data(r, 0, "SMALL   BIN") == records(gen(300, 7)));
}

TEST(the_same_pips_on_o_and_on_c_give_the_same_bytes)
{
    // Done means this: the image in the drive and the same image as an SD unit, the same PIPs onto each,
    // and the disk read back (DiskImage::to_sectors) is the SD unit's file byte for byte.
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}, {0, "Y       BIN", gen(19200, 13)}});
    REQUIRE(r.ok);
    for (const char *d : {"c", "o"})
    {
        r.c(std::string("pip ") + d + ":=m:x.bin");
        r.c(std::string("pip ") + d + ":=m:y.bin");
        r.c(std::string("era ") + d + ":small.bin[g2]");
        r.c(std::string("ren ") + d + ":z.bin=" + d + ":y.bin");
    }
    const std::vector<uint8_t> c = r.unit_image(), o = r.drive_image(0);
    REQUIRE(o.size() == IMAGE);
    unsigned differ = 0;
    for (size_t s = 0; s < IMAGE / 256; ++s)
        differ += !std::equal(c.begin() + s * 256, c.begin() + (s + 1) * 256, o.begin() + s * 256);
    std::printf("  %u of 560 sectors differ between C: and O:\n", differ);
    CHECK(differ == 0);
    CHECK(c != r.cpm); // and they did change
    // and OODOS reads them back off the disk: a fresh look (^C) at O:
    r.wait(ms(1200));
    r.type("\x03");
    REQUIRE(r.reprompt());
    r.c("pip m:z2.bin=o:z.bin");
    CHECK(m_data(r, 0, "Z2      BIN") == gen(19200, 13));
}

TEST(o_to_p_and_back)
{
    Dr r;
    REQUIRE(r.ok);
    r.c("pip p:=o:pattern.bin");
    r.c("pip o:p2.bin=p:pattern.bin");
    r.c("pip m:=o:p2.bin");
    CHECK(m_data(r, 0, "P2      BIN") == records(gen(20000, 5)));
    has(r.c("dir p:"), "P: PATTERN  BIN");
}

TEST(zz_debug)
{
    if (!std::getenv("D2DBG"))
        return;
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}});
    REQUIRE(r.ok);
    unsigned addr = 0, track = 0, rbyte_fail = 0;
    const uint16_t a_addr = sym("ADDR"), a_track = sym("D2TRACK"), a_rdata = sym("RDATA"), a_seek = sym("SEEK");
    unsigned rdata = 0, seeks = 0;
    r.watch = [&](uint16_t pc) {
        if (r.nb(2) != 61) return;
        if (pc == a_addr) ++addr;
        if (pc == a_track) ++track;
        if (pc == a_rdata) ++rdata;
        if (pc == a_seek) ++seeks;
    };
    std::string o = r.cmd("dir o:");
    if (std::getenv("D2W"))
    {
        unsigned wsec = 0, d2start = 0, d2track = 0, reads = 0;
        const uint16_t a_w = sym("D2WSEC"), a_s = sym("D2START"), a_r = sym("READ");
        r.watch = [&](uint16_t pc) {
            if (r.nb(2) != 61) return;
            if (pc == a_addr) ++addr;
            if (pc == a_w) ++wsec;
            if (pc == a_s) ++d2start;
            if (pc == a_track) ++d2track;
            if (pc == a_r) ++reads;
        };
        addr = 0;
        const uint64_t t0 = r.cpu->cycles();
        r.to("pip o:=m:x.bin", "\r\nA>", ms(60000));
        std::printf("  pip o:=m:x.bin (5 KB): %.2f s, D2WSEC %u, D2START %u, D2TRACK %u, ADDR loops %u, READs %u\n",
                    double(r.cpu->cycles() - t0) / HZ, wsec, d2start, d2track, addr, reads);
    }
    r.watch = nullptr;
    auto sysb = [&](const char *n, unsigned k = 0) { return r.phys(SYS_BLOCK_PHYS + sym(n) + k); };
    std::printf("  %s\n  ADDR %u D2TRACK %u RDATA %u SEEK %u\n", vis(o).c_str(), addr, track, rdata, seeks);
    std::printf("  D2HT %02X %02X D2TRK %02X D2SEC %02X D2GOT %02X %02X D2TGT %02X D2CNT %02X%02X\n", sysb("D2HT"), sysb("D2HT", 1), sysb("D2TRK"),
                sysb("D2SEC"), sysb("D2GOT"), sysb("D2GOT", 1), sysb("D2TGT"), sysb("D2CNT", 1), sysb("D2CNT"));
    const Disk2Card::Status st = r.card().status();
    std::printf("  card: motor %d spinning %d drive %u q6 %d q7 %d quarter %u phases %X data %02X\n", st.motor, st.spinning, st.drive, st.q6, st.q7,
                st.quarter_track[0], st.phases, st.data);
    (void)rbyte_fail;
}

int main()
{
    if (!std::ifstream(P6).good())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
