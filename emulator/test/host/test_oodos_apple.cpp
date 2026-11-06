/**
 * @file test_oodos_apple.cpp
 * @brief apple-do drives from .dsk images on the SD card, phase 6b (hw/oodos.md, "The plan, phase 6").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_utils boots it (oodos_rig.h's CcpRig), with the committed
 * card's /DISKS/CPM.DSK (made by cpmtools's mkfs.cpm and cpmcp with its own apple-do diskdef,
 * oodos/tools/make_card.sh) or /DISKS/BLANK.DSK mounted as unit 3, C:, behind OODOS's back.
 * The oracle is the image read through the card's registers and taken apart here as cpmtools
 * takes it: 35 tracks of 16 sectors of 256 bytes in the file's DOS order, CP/M's sector n of a
 * track the file's SKEW[n], 3 boot tracks, 1 KB blocks, 64 entries, 8-bit block pointers.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    constexpr unsigned UNIT = 3, TRACKS = 35, SPT = 16, SEC = 256, BOOT = 3, BLS = 1024, ENTRIES = 64;
    constexpr size_t IMAGE = size_t{TRACKS} * SPT * SEC;
    constexpr unsigned SKEW[16] = {0, 6, 12, 3, 9, 15, 14, 5, 11, 2, 8, 7, 13, 4, 10, 1}; // cpmtools's apple-do

    /// make_card.sh's binary files: byte i is ((i >> 7) * 31 + i * 7 + seed) & 0xFF.
    std::vector<uint8_t> gen(size_t n, unsigned seed)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF);
        return b;
    }

    /// The image's offset of CP/M's 256-byte sector L of the disk (0 the first of track 0).
    size_t offset(unsigned l) { return (size_t{l / SPT} * SPT + SKEW[l % SPT]) * SEC; }
    /// The 256-byte sectors (file order) the directory takes: CP/M's 0-7 of track 3.
    std::set<size_t> dir_sectors()
    {
        std::set<size_t> s;
        for (unsigned l = 0; l < ENTRIES * 32 / SEC; ++l)
            s.insert(offset(BOOT * SPT + l) / SEC);
        return s;
    }

    /// An apple-do image as cpmtools reads it.
    struct Disk
    {
        std::vector<uint8_t> img;
        const uint8_t *entry(unsigned i) const
        {
            const unsigned l = BOOT * SPT + i * 32 / SEC;
            return &img[offset(l) + i * 32 % SEC];
        }
        /// Byte `at` of block `b` (from the data area's start, track 3).
        size_t block_at(unsigned b, unsigned at) const { return offset(BOOT * SPT + b * (BLS / SEC) + at / SEC) + at % SEC; }
        std::vector<unsigned> entries_of(unsigned user, const std::string &name83) const
        {
            std::vector<std::pair<unsigned, unsigned>> found;
            for (unsigned i = 0; i < ENTRIES; ++i)
            {
                const uint8_t *e = entry(i);
                bool same = e[0] == user;
                for (unsigned c = 0; c < 11 && same; ++c)
                    same = (e[1 + c] & 0x7F) == static_cast<uint8_t>(name83[c]);
                if (same)
                    found.push_back({unsigned{e[14]} * 32 + e[12], i});
            }
            std::sort(found.begin(), found.end());
            std::vector<unsigned> out;
            for (const auto &f : found)
                out.push_back(f.second);
            return out;
        }
        /// A file's records, by its entries (EXM 0: an entry an extent, RC its records).
        std::vector<uint8_t> file(unsigned user, const std::string &name83) const
        {
            std::vector<uint8_t> out;
            for (unsigned i : entries_of(user, name83))
            {
                const uint8_t *e = entry(i);
                for (unsigned r = 0; r < e[15]; ++r)
                {
                    const unsigned b = e[16 + r / 8];
                    for (unsigned j = 0; j < RECORD; ++j)
                        out.push_back(img[block_at(b, r % 8 * unsigned{RECORD} + j)]);
                }
            }
            return out;
        }
        /// The 256-byte sectors (file order) a file's blocks take.
        std::set<size_t> sectors_of(unsigned user, const std::string &name83) const
        {
            std::set<size_t> s;
            for (unsigned i : entries_of(user, name83))
                for (unsigned k = 0; k < 16; ++k)
                    if (const unsigned b = entry(i)[16 + k])
                        for (unsigned q = 0; q < BLS / SEC; ++q)
                            s.insert(block_at(b, q * SEC) / SEC);
            return s;
        }
    };

    /// The data, padded to its records: with zeros, as cpmcp pads a binary file, or as given (PIP's ^Z).
    std::vector<uint8_t> records(std::vector<uint8_t> d, uint8_t pad = 0)
    {
        d.resize((d.size() + RECORD - 1) / RECORD * RECORD, pad);
        return d;
    }
    /// a == b, and where they first differ if not.
    bool same_bytes(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const char *what)
    {
        if (a == b)
            return true;
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i])
            ++i;
        std::printf("  %s: %zu bytes against %zu, first different at %zu ($%02X)\n", what, a.size(), b.size(), i,
                    i < a.size() ? a[i] : 0);
        return false;
    }

    void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }

    /// A rig with PIP, SHOW and INITDIR on M:, the search chain M: first, and `path` as C:.
    struct Ar : CcpRig
    {
        explicit Ar(const std::string &path = "/DISKS/CPM.DSK", bool ro = false, std::vector<MFile> files = {})
        {
            if (!ok)
                return;
            put_m(files);
            put_util("pip", UTILS);
            put_util("show", UTILS);
            put_util("initdir", UTILS);
            chain({13, 0});
            ok = mount(UNIT, path, ro) == 0;
        }
        Disk disk()
        {
            Disk d;
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
            {
                const std::vector<uint8_t> b = read_unit(UNIT, s);
                d.img.insert(d.img.end(), b.begin(), b.end());
            }
            return d;
        }
        /// A unit's sector written through the card's registers, behind OODOS's back.
        void write_unit(unsigned unit, uint32_t lba, const std::vector<uint8_t> &s)
        {
            sd().write(SdCard::UNIT, static_cast<uint8_t>(unit));
            for (unsigned i = 0; i < 4; ++i)
                sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            sd().write(SdCard::COUNT, 1);
            sd().write(SdCard::COUNT + 1, 0);
            sd().write(SdCard::CMD, SdCard::WRITE);
            for (size_t i = 0; i < SECTOR; ++i)
                sd().write(SdCard::DATA, s[i]);
            run(ms(2));
            sd().write(SdCard::UNIT, 0);
        }
        void put_disk(const Disk &d)
        {
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
                write_unit(UNIT, s, std::vector<uint8_t>(d.img.begin() + s * SECTOR, d.img.begin() + (s + 1) * SECTOR));
        }
    };
} // namespace

TEST(a_140_kb_unit_is_an_apple_do_drive_with_cpmtools_dpb)
{
    Ar r;
    REQUIRE(r.ok);
    // The DPB, as apple-do's diskdef works out (seclen 256, sectrk 16, tracks 35, blocksize 1024,
    // maxdir 64, boottrk 3) in OODOS's 512-byte sectors: SPT 32, BSH 3, BLM 7, EXM 0, DSM 127,
    // DRM 63, AL0 $C0, AL1 0, CKS $8000 (an SD unit is never changed under the system), OFF 3, PSH 2, PHM 3.
    const std::string o = r.cmd("dir c:");
    has(o, "README   TXT");
    std::string dpb;
    const uint16_t at = sym("DPBAPL");
    for (unsigned i = 0; i < 17; ++i)
        dpb += hex(r.phys(COMMON_PHYS + at - 0xF800u + i), 2);
    same(dpb, "2000030700" "7F00" "3F00" "C000" "0080" "0300" "0203");
    // and C:'s DPH has it (the BIOS's DRVTAB, in block 61 at $8000)
    const uint32_t bios = 61u * BLOCK - 0x8000u, tab = bios + sym("DRVTAB") + 2 * 2;
    const uint32_t dph = bios + (r.phys(tab) | r.phys(tab + 1) << 8);
    CHECK_EQ(r.phys(dph + 12) | r.phys(dph + 13) << 8, at);
    // SHOW's sums from it: 128 KB, 64 entries
    const std::string s = r.cmd("show c:[drive]");
    has(s, "128: Kilobyte Drive Capacity");
    has(s, "64: 32 Byte Directory Entries");
    std::printf("%s\n", vis(s).c_str());
}

TEST(cpmtools_files_read_back_every_byte)
{
    Ar r;
    REQUIRE(r.ok);
    std::string o = r.cmd("dir c:");
    has(o, "C: README   TXT : PATTERN  BIN");
    o = r.cmd("type c:readme.txt");
    has(o, "THIS IS AN APPLE CP/M DISK, MADE ON LINUX BY CPMTOOLS.\r\nOODOS READS IT AS AN SD DRIVE, /DISKS/CPM.DSK ON THE CARD.\r\n");
    const uint64_t t0 = r.cpu->cycles();
    o = r.cmd("pip m:=c:pattern.bin");
    const uint64_t took = r.cpu->cycles() - t0;
    std::printf("  PIP of 20,096 bytes from C: to M:: %llu cycles (%.2f s)\n", static_cast<unsigned long long>(took),
                static_cast<double>(took) / HZ);
    CHECK(same_bytes(m_data(r, 0, "PATTERN BIN"), records(gen(20000, 5)), "PATTERN.BIN on M:"));
    o = r.cmd("pip m:=c:small.bin[g2]");
    CHECK(m_data(r, 0, "SMALL   BIN") == records(gen(300, 7)));
    // the oracle reads the same as OODOS did
    const Disk d = r.disk();
    CHECK(same_bytes(d.file(0, "PATTERN BIN"), records(gen(20000, 5)), "PATTERN.BIN in the image"));
    CHECK(d.file(2, "SMALL   BIN") == records(gen(300, 7)));
}

TEST(writes_land_where_cpmtools_reads_them_and_nowhere_else)
{
    Ar r("/DISKS/CPM.DSK", false,
         {{0, "X       BIN", gen(5120, 11)}, {0, "Y       BIN", gen(19200, 13)}});
    REQUIRE(r.ok);
    const Disk before = r.disk();
    r.cmd("pip c:=m:x.bin");
    r.cmd("pip c:=m:y.bin");
    const Disk after = r.disk();
    CHECK(after.file(0, "X       BIN") == gen(5120, 11));
    CHECK(after.file(0, "Y       BIN") == gen(19200, 13));
    CHECK_EQ(after.entries_of(0, "Y       BIN").size(), size_t{2}); // 150 records: two extents
    CHECK(after.file(0, "PATTERN BIN") == records(gen(20000, 5)));
    CHECK(after.file(2, "SMALL   BIN") == records(gen(300, 7)));
    // Only the directory's sectors and the new files' changed: not the boot tracks, not the other
    // half of an SD sector a write shared, not another file's.
    std::set<size_t> allowed = dir_sectors();
    for (const char *n : {"X       BIN", "Y       BIN"})
        for (size_t s : after.sectors_of(0, n))
            allowed.insert(s);
    unsigned changed = 0, stray = 0;
    for (size_t s = 0; s < IMAGE / SEC; ++s)
        if (!std::equal(before.img.begin() + s * SEC, before.img.begin() + (s + 1) * SEC, after.img.begin() + s * SEC))
        {
            ++changed;
            if (!allowed.count(s))
            {
                ++stray;
                std::printf("  sector %zu (track %zu) changed\n", s, s / SPT);
            }
        }
    std::printf("  %u sectors changed, %u outside the directory and the new files\n", changed, stray);
    CHECK(changed > 0);
    CHECK_EQ(stray, 0u);
    CHECK(std::equal(before.img.begin(), before.img.begin() + BOOT * SPT * SEC, after.img.begin()));
    // OODOS reads back what it wrote, past its buffers: ^C resets the disks (the BDOS's buffers
    // dropped, the units read again and so the track buffer emptied)
    r.type("\x03");
    REQUIRE(r.reprompt());
    r.cmd("pip m:y2.bin=c:y.bin");
    CHECK(same_bytes(m_data(r, 0, "Y2      BIN"), gen(19200, 13), "Y.BIN back"));
    // ERA and REN, in the image
    r.cmd("era c:x.bin");
    r.cmd("ren c:z.bin=c:y.bin");
    const Disk d = r.disk();
    CHECK(d.entries_of(0, "X       BIN").empty());
    CHECK(d.entries_of(0, "Y       BIN").empty());
    CHECK(d.file(0, "Z       BIN") == gen(19200, 13));
}

TEST(a_full_disk_and_a_full_directory)
{
    // BLANK.DSK: 126 of its 128 blocks free. Three files of 32 KB fit; a fourth doesn't.
    Ar r("/DISKS/BLANK.DSK", false,
         {{0, "A       BIN", gen(32768, 1)}, {0, "B       BIN", gen(32768, 2)}, {0, "C       BIN", gen(32768, 3)}});
    REQUIRE(r.ok);
    r.cmd("pip c:=m:a.bin");
    r.cmd("pip c:=m:b.bin");
    r.cmd("pip c:=m:c.bin");
    const std::string o = r.cmd("pip c:d.bin=m:a.bin");
    has(o, "DISK WRITE ERROR");
    Disk d = r.disk();
    CHECK(d.file(0, "C       BIN") == gen(32768, 3));
    CHECK(d.entries_of(0, "D       BIN").empty());
    // The directory filled behind OODOS's back to 63 entries: one more file fits, then none.
    Ar f("/DISKS/BLANK.DSK", false, {{0, "S       BIN", gen(128, 4)}});
    REQUIRE(f.ok);
    Disk blank = f.disk();
    for (unsigned i = 0; i < ENTRIES - 1; ++i)
    {
        uint8_t *e = const_cast<uint8_t *>(blank.entry(i));
        std::memset(e, 0, 32);
        std::memcpy(e + 1, ("F" + hex(i, 2) + "     DAT").c_str(), 11);
    }
    f.put_disk(blank);
    has(f.cmd("pip c:=m:s.bin"), "A>");
    CHECK(f.disk().file(0, "S       BIN") == records(gen(128, 4)));
    has(f.cmd("pip c:t.bin=m:s.bin"), "DISK WRITE ERROR - T.BIN"); // PIP's word for a full directory too
    CHECK(f.disk().entries_of(0, "T       BIN").empty());
}

TEST(read_only_unit_rescan_and_a_unit_that_is_no_drive)
{
    // RO in DRIVES.TXT (the units table's flag): the BDOS's Read Only, the image as it was.
    Ar r("/DISKS/CPM.DSK", true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(r.ok);
    const Disk before = r.disk();
    has(r.to("pip c:=m:x.bin", "A>"), "Read Only");
    CHECK(r.disk().img == before.img);
    // Another image mounted as the unit, then ^C: the drive logged in again, nothing of the old
    // one's kept (its directory's track was in the track buffer).
    Ar s("/DISKS/CPM.DSK");
    REQUIRE(s.ok);
    has(s.cmd("dir c:"), "README");
    REQUIRE(s.mount(UNIT, "/DISKS/BLANK.DSK") == 0);
    s.type("\x03");
    REQUIRE(s.reprompt());
    has(s.cmd("dir c:"), "No File");
    // 281 sectors: not a drive
    REQUIRE(s.mount(UNIT, "/DISKS/ODD.DSK") == 0);
    s.type("\x03");
    REQUIRE(s.reprompt());
    has(s.to("dir c:", "A>"), "Invalid Drive");
}

TEST(initdir_on_an_apple_disk_its_eight_sectors_a_track)
{
    // INITDIR works through the BIOS by tracks and sectors: 8 a track here.
    Ar r("/DISKS/BLANK.DSK", false, {{0, "S       BIN", gen(2000, 6)}});
    REQUIRE(r.ok);
    r.cmd("pip c:=m:s.bin");
    const std::string q = r.to("initdir c:", "(Y/N)? ");
    has(q, "INITDIR WILL ACTIVATE TIME STAMPS");
    const std::string o = r.cmd("y");
    std::printf("  %s%s\n", vis(q).c_str(), vis(o).c_str());
    const Disk d = r.disk();
    unsigned sfcbs = 0;
    for (unsigned i = 3; i < ENTRIES; i += 4)
        sfcbs += d.entry(i)[0] == 0x21;
    CHECK_EQ(sfcbs, ENTRIES / 4);
    CHECK(d.file(0, "S       BIN") == records(gen(2000, 6), 0x1A));
    std::string dir = r.cmd("dir c:");
    has(dir, "S        BIN");
}

int main() { return run_all(); }
