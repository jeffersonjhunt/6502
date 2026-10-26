/**
 * @file test_oodos_card.cpp
 * @brief OODOS's card and cpmtools, both ways (hw/oodos.md, "Testing").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * sd/oodos.sparse is made by cpmtools, mkfs.fat and mtools
 * (oodos/tools/make_card.sh): /BOOT.BIN, /OODOS/DRIVES.TXT, and /OODOS/A.IMG,
 * an oodos-8m drive. Here the emulated card mounts A.IMG as unit 1 with its
 * own MOUNT, as OODOS will, and the sectors at LBA = track x SPT + sector
 * (the BIOS's arithmetic) hold each directory entry, SFCB stamp and record
 * where cpmtools put them. Drive is a test oracle for that addressing,
 * following the DPB worked out from oodos/diskdefs, not a file system.
 *
 * The other way, a stand-in writer makes a file on A: through the card's
 * WRITE, and with OODOS_CARD_OUT set the card is saved there for
 * oodos/test/interop.sh to check with cpmtools. OODOS_CARD_FAULT=alloc or
 * =stamp makes the stand-in wrong on purpose, to see interop.sh fail.
 * Phase 3's BDOS replaces the stand-in: write_on_the_oo1() is the seam.
 *
 * A binary file's byte i is ((i >> 7) * 31 + i * 7 + seed) & 0xFF, as
 * make_card.sh and interop.sh have it.
 */

#include "harness.h"
#include "sd_images.h"

#include "sd_card.h"
#include "sd_media.h"
#include "session.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t HZ = SdCard::NATIVE_HZ;
    constexpr uint64_t cycles_us(uint64_t n) { return HZ * n / 1'000'000; }
    constexpr uint64_t INIT = cycles_us(uint64_t{SdCard::INIT_MS} * 1000);
    constexpr uint64_t ACCESS = cycles_us(SdCard::ACCESS_US), SECTOR_TIME = cycles_us(SdCard::SECTOR_US);
    constexpr size_t SECTOR = SdCard::SECTOR;
    constexpr unsigned RECORD = 128, ENTRY = 32;
    const std::string CARD = "oodos.sparse";

    uint8_t pattern(unsigned seed, size_t i) { return static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF); }
    std::vector<uint8_t> pattern_file(unsigned seed, size_t size)
    {
        std::vector<uint8_t> b(size);
        for (size_t i = 0; i < size; ++i)
            b[i] = pattern(seed, i);
        return b;
    }
    /// A CP/M text file as make_card.sh writes one: CR LF lines, ^Z to the end of the last record.
    std::vector<uint8_t> text_file(const std::vector<std::string> &lines)
    {
        std::vector<uint8_t> b;
        for (const std::string &l : lines)
        {
            b.insert(b.end(), l.begin(), l.end());
            b.push_back('\r');
            b.push_back('\n');
        }
        b.resize(b.size() + RECORD - b.size() % RECORD, 0x1A);
        return b;
    }

    /// CP/M 3's date stamp: days, 1 January 1978 being day 1, then the hour and minute in BCD.
    std::array<uint8_t, 4> stamp(unsigned y, unsigned m, unsigned d, unsigned hour, unsigned minute)
    {
        static const unsigned DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        auto leap = [](unsigned year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); };
        unsigned days = d;
        for (unsigned year = 1978; year < y; ++year)
            days += leap(year) ? 366 : 365;
        for (unsigned month = 1; month < m; ++month)
            days += DAYS[month - 1] + (month == 2 && leap(y));
        auto bcd = [](unsigned v) { return static_cast<uint8_t>(v / 10 << 4 | v % 10); };
        return {static_cast<uint8_t>(days), static_cast<uint8_t>(days >> 8), bcd(hour), bcd(minute)};
    }
    const std::array<uint8_t, 4> MADE = stamp(2026, 10, 26, 12, 0); // make_card.sh's faketime

    /// A diskdef's DPB, worked out as CP/M 3's is from oodos/diskdefs.
    struct Dpb
    {
        unsigned seclen{0}, spt{0}, tracks{0}, off{0}, bls{0}, drm{0}; // drm: entries, not CP/M's entries - 1
        unsigned dsm{0}, exm{0}, dir_blocks{0};

        static Dpb from_diskdefs(const std::string &name)
        {
            Dpb p;
            std::ifstream in(EMU_OODOS_DISKDEFS);
            std::string line;
            bool on = false;
            while (std::getline(in, line))
            {
                std::istringstream words(line);
                std::string key;
                unsigned value = 0;
                words >> key;
                if (key == "diskdef")
                {
                    std::string n;
                    words >> n;
                    on = n == name;
                }
                else if (on && key == "end")
                    break;
                else if (on && words >> value)
                {
                    if (key == "seclen") p.seclen = value;
                    else if (key == "sectrk") p.spt = value;
                    else if (key == "tracks") p.tracks = value;
                    else if (key == "boottrk") p.off = value;
                    else if (key == "blocksize") p.bls = value;
                    else if (key == "maxdir") p.drm = value;
                }
            }
            if (p.bls)
            {
                p.dsm = (p.tracks - p.off) * p.spt * p.seclen / p.bls - 1;
                p.exm = p.dsm < 256 ? p.bls / 1024 - 1 : p.bls / 2048 - 1;
                p.dir_blocks = (p.drm * ENTRY + p.bls - 1) / p.bls;
            }
            return p;
        }
        unsigned pointers() const { return dsm < 256 ? 16 : 8; } ///< block pointers in an entry
        unsigned entry_records() const { return (exm + 1) * 128; }
    };

    /// The card with the OODOS image in, started up, talked to through its registers.
    struct Rig
    {
        explicit Rig(std::vector<uint8_t> bytes) : media(std::make_shared<MemoryMedia>(std::move(bytes), CARD))
        {
            card.insert(media, false);
            card.tick(INIT);
        }
        Rig() : Rig(sd_image(CARD)) {}
        uint8_t reg(uint8_t r) { return card.read(r); }
        void set(uint8_t r, uint8_t v) { card.write(r, v); }
        uint8_t status() { return reg(SdCard::STATUS); }
        /// MOUNT `path` as `unit`, as software does. @return ERROR (0: mounted)
        uint8_t mount(unsigned unit, const std::string &path)
        {
            set(SdCard::UNIT, 0);
            set(SdCard::CMD, SdCard::MOUNT);
            std::vector<uint8_t> p = {static_cast<uint8_t>(unit), 0};
            p.insert(p.end(), path.begin(), path.end());
            for (size_t i = 0; i < SECTOR; ++i)
                set(SdCard::DATA, i < p.size() ? p[i] : 0);
            card.tick(ACCESS);
            return status() & SdCard::BUSY ? 0xFF : reg(SdCard::ERROR);
        }
        void transfer(uint8_t cmd, unsigned unit, uint32_t lba)
        {
            set(SdCard::UNIT, static_cast<uint8_t>(unit));
            for (unsigned i = 0; i < 4; ++i)
                set(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            set(SdCard::COUNT, 1);
            set(SdCard::COUNT + 1, 0);
            set(SdCard::CMD, cmd);
        }
        /// One sector of `unit` by READ. @return false if the card failed it
        bool read(unsigned unit, uint32_t lba, uint8_t *out)
        {
            transfer(SdCard::READ, unit, lba);
            card.tick(ACCESS);
            if (!(status() & SdCard::DATA_READY))
                return false;
            for (size_t i = 0; i < SECTOR; ++i)
                out[i] = reg(SdCard::DATA);
            return status() == (SdCard::PRESENT | SdCard::READY);
        }
        /// One sector of `unit` by WRITE.
        bool write(unsigned unit, uint32_t lba, const uint8_t *in)
        {
            transfer(SdCard::WRITE, unit, lba);
            if (!(status() & SdCard::DATA_READY))
                return false;
            for (size_t i = 0; i < SECTOR; ++i)
                set(SdCard::DATA, in[i]);
            card.tick(SECTOR_TIME);
            return status() == (SdCard::PRESENT | SdCard::READY);
        }

        std::shared_ptr<MemoryMedia> media;
        SdCard card;
    };

    /// An OODOS drive on a unit, by the DPB: sectors by track and sector, blocks, the directory.
    struct Drive
    {
        Rig &rig;
        unsigned unit;
        Dpb dpb;

        uint32_t lba(unsigned track, unsigned sector) const { return track * dpb.spt + sector; }
        /// The `n`th sector of block `b`: the data area starts at track OFF.
        uint32_t block_lba(unsigned b, unsigned n) const
        {
            const unsigned at = dpb.off * dpb.spt + b * (dpb.bls / dpb.seclen) + n;
            return lba(at / dpb.spt, at % dpb.spt);
        }
        std::vector<uint8_t> block(unsigned b)
        {
            std::vector<uint8_t> out(dpb.bls);
            for (unsigned n = 0; n < dpb.bls / SECTOR; ++n)
                CHECK(rig.read(unit, block_lba(b, n), &out[n * SECTOR]));
            return out;
        }
        /// The whole directory: DRM entries of 32 bytes, from block 0.
        std::vector<uint8_t> directory()
        {
            std::vector<uint8_t> out;
            for (unsigned b = 0; b < dpb.dir_blocks; ++b)
            {
                std::vector<uint8_t> blk = block(b);
                out.insert(out.end(), blk.begin(), blk.end());
            }
            out.resize(size_t{dpb.drm} * ENTRY);
            return out;
        }
        unsigned pointer(const uint8_t *e, unsigned i) const
        {
            return dpb.dsm < 256 ? e[16 + i] : unsigned{e[16 + 2 * i]} | unsigned{e[17 + 2 * i]} << 8;
        }
    };

    /// A file's directory entries, in extent order: their indices in the directory.
    std::vector<unsigned> entries_of(const std::vector<uint8_t> &dir, unsigned user, const std::string &name83)
    {
        std::vector<std::pair<unsigned, unsigned>> found; // (extent, index)
        for (unsigned i = 0; i < dir.size() / ENTRY; ++i)
        {
            const uint8_t *e = &dir[i * ENTRY];
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

    /// The file's records, through its entries: each covers EXM + 1 logical extents, the
    /// last holding (EX & EXM) x 128 + RC records. @return empty if an entry is out of place
    std::vector<uint8_t> records_of(Drive &d, const std::vector<uint8_t> &dir, const std::vector<unsigned> &entries)
    {
        std::vector<uint8_t> out;
        for (size_t k = 0; k < entries.size(); ++k)
        {
            const uint8_t *e = &dir[entries[k] * ENTRY];
            const unsigned extent = unsigned{e[14]} * 32 + e[12];
            CHECK_EQ(extent & ~d.dpb.exm, static_cast<unsigned>(k * (d.dpb.exm + 1))); // the k-th entry
            const unsigned records = (extent & d.dpb.exm) * 128 + e[15];
            if (k + 1 < entries.size())
                CHECK_EQ(records, d.dpb.entry_records()); // only the last is part full
            const unsigned per_block = d.dpb.bls / RECORD;
            for (unsigned b = 0; b * per_block < records; ++b)
            {
                const unsigned at = d.pointer(e, b);
                if (at < d.dpb.dir_blocks || at > d.dpb.dsm)
                    return {};
                std::vector<uint8_t> blk = d.block(at);
                const unsigned n = std::min(per_block, records - b * per_block);
                out.insert(out.end(), blk.data(), blk.data() + n * RECORD);
            }
        }
        return out;
    }

    /// The two stamps of directory entry `i`: its SFCB is entry i | 3, 10 bytes an entry from +1.
    std::pair<std::array<uint8_t, 4>, std::array<uint8_t, 4>> stamps_of(const std::vector<uint8_t> &dir, unsigned i)
    {
        const uint8_t *sfcb = &dir[(i | 3) * ENTRY];
        CHECK_EQ(sfcb[0], 0x21);
        const uint8_t *s = sfcb + 1 + (i & 3) * 10;
        return {{s[0], s[1], s[2], s[3]}, {s[4], s[5], s[6], s[7]}};
    }

    /// Every block a directory entry has is in range and nobody else's. @return the blocks in use
    std::set<unsigned> allocation(const Drive &d, const std::vector<uint8_t> &dir)
    {
        std::set<unsigned> used;
        for (unsigned b = 0; b < d.dpb.dir_blocks; ++b)
            used.insert(b);
        for (unsigned i = 0; i < dir.size() / ENTRY; ++i)
        {
            const uint8_t *e = &dir[i * ENTRY];
            if (e[0] > 15)
                continue; // free, the label, an SFCB, an XFCB
            for (unsigned p = 0; p < d.dpb.pointers(); ++p)
            {
                const unsigned b = d.pointer(e, p);
                if (!b)
                    continue;
                CHECK(b <= d.dpb.dsm);
                if (!used.insert(b).second)
                {
                    std::printf("  block %u used twice (entry %u)\n", b, i);
                    CHECK(false);
                }
            }
        }
        return used;
    }

    /// DRIVES.TXT's path for a drive, as the loader will read it: "A: /PATH [RO]", # comments.
    std::string drive_path(const std::string &text, char drive, bool &read_only)
    {
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            line = line.substr(0, line.find('#'));
            std::istringstream words(line);
            std::string name, path, flag;
            if (words >> name >> path && name.size() == 2 && name[1] == ':' && name[0] == drive)
            {
                read_only = words >> flag && flag == "RO";
                return path;
            }
        }
        return "";
    }

    /// The card mounted as the loader will: DRIVES.TXT read through unit 1, then A: as unit 1.
    bool mount_a(Rig &r)
    {
        if (r.mount(1, "/OODOS/DRIVES.TXT") != 0)
            return false;
        std::string text;
        uint8_t s[SECTOR];
        for (uint32_t n = 0; n < r.card.unit(1).sectors && r.read(1, n, s); ++n)
            text.append(reinterpret_cast<const char *>(s), strnlen(reinterpret_cast<const char *>(s), SECTOR));
        bool read_only = true;
        const std::string a = drive_path(text, 'A', read_only);
        return a == "/OODOS/A.IMG" && !read_only && r.mount(1, a) == 0;
    }

    // ---- the OO-1's side ----------------------------------------------------

    /// The file the OO-1 writes, and what interop.sh expects of it: 0:OO1.BIN, 260 records
    /// (two entries), seed 5, made 26 October 2026 13:45 and updated 15:30. The same day:
    /// fsck.cpm 2.23 mixes up the days of an SFCB's first stamps when it compares them.
    const std::string NEW_NAME = "OO1     BIN";
    constexpr unsigned NEW_USER = 0, NEW_SEED = 5;
    constexpr size_t NEW_SIZE = 260 * RECORD;
    const std::array<uint8_t, 4> NEW_MADE = stamp(2026, 10, 26, 13, 45), NEW_UPDATED = stamp(2026, 10, 26, 15, 30);

    /// A stand-in for OODOS's BDOS, until phase 3: makes the file on a drive in CP/M 3's layout,
    /// sector by sector through the card (read, change, write back): entries in the first
    /// free slots, blocks the first free ones, and both stamps in each entry's SFCB slot (as
    /// cpmtools does; fsck.cpm checks them all). `fault`: "alloc" gives it a block another
    /// file has, "stamp" puts each entry's stamps in the next entry's slot.
    void stand_in_bdos(Drive &d, unsigned user, const std::string &name83, const std::vector<uint8_t> &bytes,
                       const std::array<uint8_t, 4> &made, const std::array<uint8_t, 4> &updated,
                       const std::string &fault)
    {
        std::vector<uint8_t> dir = d.directory();
        std::set<unsigned> used = allocation(d, dir);
        auto take_block = [&]() {
            if (fault == "alloc")
                return *used.lower_bound(d.dpb.dir_blocks); // README.TXT's
            unsigned b = d.dpb.dir_blocks;
            while (used.count(b))
                ++b;
            used.insert(b);
            return b;
        };
        auto change = [&](uint32_t lba, size_t offset, const uint8_t *data, size_t n) {
            uint8_t s[SECTOR];
            CHECK(d.rig.read(d.unit, lba, s));
            std::memcpy(s + offset, data, n);
            CHECK(d.rig.write(d.unit, lba, s));
        };
        // `n` bytes at `at` in the directory: an entry, or a part of one, never across sectors.
        auto put = [&](size_t at, const uint8_t *data, size_t n) {
            change(d.block_lba(static_cast<unsigned>(at / d.dpb.bls), static_cast<unsigned>(at % d.dpb.bls / SECTOR)),
                   at % SECTOR, data, n);
        };
        uint8_t stamps[10] = {}; // creation, update, then the password mode and a reserved byte
        std::memcpy(stamps, made.data(), 4);
        std::memcpy(stamps + 4, updated.data(), 4);

        const unsigned records = static_cast<unsigned>(bytes.size() / RECORD);
        const unsigned per_block = d.dpb.bls / RECORD;
        unsigned slot = 0;
        for (unsigned k = 0; k * d.dpb.entry_records() < records; ++k)
        {
            while (dir[slot * ENTRY] != 0xE5 || (slot & 3) == 3) // free, and not an SFCB's place
                ++slot;
            const unsigned n = std::min(d.dpb.entry_records(), records - k * d.dpb.entry_records());
            const unsigned extent = k * (d.dpb.exm + 1) + (n - 1) / 128;
            uint8_t e[ENTRY] = {static_cast<uint8_t>(user)};
            std::memcpy(e + 1, name83.data(), 11);
            e[12] = extent & 31;
            e[14] = static_cast<uint8_t>(extent >> 5);
            e[15] = static_cast<uint8_t>(n - (n - 1) / 128 * 128);
            for (unsigned b = 0; b * per_block < n; ++b)
            {
                const unsigned at = take_block();
                if (d.dpb.dsm < 256)
                    e[16 + b] = static_cast<uint8_t>(at);
                else
                {
                    e[16 + 2 * b] = static_cast<uint8_t>(at);
                    e[17 + 2 * b] = static_cast<uint8_t>(at >> 8);
                }
                const size_t from = (size_t{k} * d.dpb.entry_records() + b * per_block) * RECORD;
                const size_t len = std::min<size_t>(d.dpb.bls, bytes.size() - from);
                for (size_t s = 0; s * SECTOR < len; ++s)
                    change(d.block_lba(at, static_cast<unsigned>(s)), 0, &bytes[from + s * SECTOR],
                           std::min(SECTOR, len - s * SECTOR));
            }
            put(size_t{slot} * ENTRY, e, ENTRY);
            dir[slot * ENTRY] = static_cast<uint8_t>(user);
            const unsigned in_sfcb = fault == "stamp" ? ((slot & 3) + 1) % 3 : slot & 3;
            put(size_t{slot | 3} * ENTRY + 1 + in_sfcb * 10, stamps, sizeof stamps);
        }
    }

    /// The seam: the OO-1 writes 0:OO1.BIN onto A: of the card in `r`, mounted by mount_a().
    /// Phase 3 runs OODOS's BDOS here instead, in a Session with the card in its slot.
    void write_on_the_oo1(Rig &r, const std::string &fault)
    {
        Drive a{r, 1, Dpb::from_diskdefs("oodos-8m")};
        stand_in_bdos(a, NEW_USER, NEW_NAME, pattern_file(NEW_SEED, NEW_SIZE), NEW_MADE, NEW_UPDATED, fault);
    }

    std::string env(const char *name)
    {
        const char *v = std::getenv(name);
        return v ? v : "";
    }
} // namespace

TEST(the_dpb_is_oodos_8m_as_check_diskdefs_works_it_out)
{
    const Dpb p = Dpb::from_diskdefs("oodos-8m");
    CHECK_EQ(p.seclen, 512u);
    CHECK_EQ(p.spt, 16u);
    CHECK_EQ(p.off, 0u);
    CHECK_EQ(p.bls, 4096u);
    CHECK_EQ(p.dsm, 2047u);
    CHECK_EQ(p.exm, 1u);
    CHECK_EQ(p.drm, 1024u);
    CHECK_EQ(p.dir_blocks, 8u);
}

TEST(the_card_boots_its_placeholder_and_returns_to_the_monitor)
{
    std::string error;
    auto s = Session::create({EMU_MACHINE_DIR "/backplane.machine", EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
    REQUIRE(s != nullptr);
    REQUIRE(s->insert_sd(sd_image(CARD), CARD, error));
    std::string out;
    auto until = [&](const std::string &want) {
        for (int i = 0; i < 120 && out.find(want) == std::string::npos; ++i)
        {
            s->run(119'318);
            out += s->take_output();
        }
        return out.find(want) != std::string::npos;
    };
    REQUIRE(until("BACKPLANE BIOS 0.9\r\\\r"));
    s->send("C80CR\r");
    out.clear();
    CHECK(until("\rTHIS IS AN OODOS CARD. NO LOADER YET: BACK TO THE MONITOR\r"));
    CHECK(until("\\\r"));
}

TEST(drives_txt_names_a_and_the_card_mounts_it_as_unit_1)
{
    Rig r;
    REQUIRE(r.media->sectors() == 16u * 2048);
    REQUIRE(mount_a(r));
    CHECK_EQ(r.card.unit(1).sectors, uint64_t{8 * 2048}); // all 8 MB: 1024 tracks of 16
    CHECK(!r.card.unit(1).read_only);
    bool ro = false;
    CHECK_EQ(drive_path("# none\r\nB: /X.IMG RO\r\n  A:   /Y.IMG # the system\r\n", 'B', ro), std::string("/X.IMG"));
    CHECK(ro);
    CHECK_EQ(drive_path("# none\r\nB: /X.IMG RO\r\n  A:   /Y.IMG # the system\r\n", 'A', ro), std::string("/Y.IMG"));
    CHECK(!ro);
    CHECK_EQ(drive_path("# A: /Z.IMG\r\n", 'A', ro), std::string(""));
}

TEST(the_label_and_an_sfcb_every_fourth_entry_are_where_cpmtools_put_them)
{
    Rig r;
    REQUIRE(mount_a(r));
    Drive a{r, 1, Dpb::from_diskdefs("oodos-8m")};
    // The directory's first sector is track 0, sector 0: no boot tracks.
    uint8_t s[SECTOR];
    REQUIRE(r.read(1, a.lba(0, 0), s));
    CHECK_EQ(s[0], 0x20);
    CHECK_EQ(std::string(reinterpret_cast<const char *>(s + 1), 11), std::string("OODOS      "));
    CHECK_EQ(s[12], 0x31); // creation and update stamps
    const std::vector<uint8_t> dir = a.directory();
    REQUIRE(dir.size() == 1024u * ENTRY);
    unsigned sfcbs = 0, free = 0;
    for (unsigned i = 0; i < 1024; ++i)
    {
        sfcbs += (i & 3) == 3 && dir[i * ENTRY] == 0x21;
        free += dir[i * ENTRY] == 0xE5;
    }
    CHECK_EQ(sfcbs, 256u);
    CHECK_EQ(free, 1024u - 256 - 1 - 5); // the label, and five files' entries
    // The label's own stamps, in its bytes 24-31: made at make_card.sh's moment.
    CHECK(std::equal(MADE.begin(), MADE.end(), dir.begin() + 24));
    allocation(a, dir);
}

TEST(each_file_is_where_its_entries_say_with_its_stamps)
{
    Rig r;
    REQUIRE(mount_a(r));
    Drive a{r, 1, Dpb::from_diskdefs("oodos-8m")};
    const std::vector<uint8_t> dir = a.directory();
    struct File
    {
        unsigned user;
        std::string name;
        std::vector<uint8_t> bytes;
        size_t entries;
    };
    const File files[] = {
        {0, "README  TXT",
         text_file({"THIS IS AN OODOS DRIVE, MADE ON LINUX BY CPMTOOLS.",
                    "OODOS READS IT AS DRIVE A:, SD UNIT 1, /OODOS/A.IMG ON THE CARD."}),
         1},
        {0, "PATTERN BIN", pattern_file(1, 36864), 2}, // 288 records: 256 in one entry, 32 in the next
        {3, "USER3   TXT", text_file({"A FILE IN USER 3."}), 1},
        {3, "SMALL   BIN", pattern_file(3, 384), 1},
    };
    for (const File &f : files)
    {
        std::printf("  %u:%s\n", f.user, f.name.c_str());
        const std::vector<unsigned> e = entries_of(dir, f.user, f.name);
        CHECK_EQ(e.size(), f.entries);
        if (e.empty())
            continue;
        CHECK(records_of(a, dir, e) == f.bytes);
        for (unsigned i : e) // cpmtools stamps every entry of a file, not just the first
        {
            const auto st = stamps_of(dir, i);
            CHECK(st.first == MADE);
            CHECK(st.second == MADE);
        }
    }
    CHECK(entries_of(dir, 3, "README  TXT").empty()); // users are apart
}

TEST(the_oo1_writes_a_file_and_the_card_is_saved_for_interop_sh)
{
    Rig r;
    REQUIRE(mount_a(r));
    const std::vector<uint8_t> before = r.media->bytes();
    write_on_the_oo1(r, env("OODOS_CARD_FAULT"));
    const std::string out = env("OODOS_CARD_OUT");
    if (!out.empty())
    {
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char *>(r.media->bytes().data()),
                static_cast<std::streamsize>(r.media->bytes().size()));
        CHECK(f.good());
        std::printf("  the card saved to %s\n", out.c_str());
    }

    // Read back from a fresh card, mounted again, as cpmtools will.
    Rig again(r.media->bytes());
    REQUIRE(mount_a(again));
    Drive a{again, 1, Dpb::from_diskdefs("oodos-8m")};
    const std::vector<uint8_t> dir = a.directory();
    allocation(a, dir);
    const std::vector<unsigned> e = entries_of(dir, NEW_USER, NEW_NAME);
    REQUIRE(e.size() == 2u);
    CHECK_EQ(e[0], 8u); // the first free entries, not an SFCB's
    CHECK_EQ(e[1], 9u);
    CHECK(records_of(a, dir, e) == pattern_file(NEW_SEED, NEW_SIZE));
    for (unsigned i : e)
    {
        const auto st = stamps_of(dir, i);
        CHECK(st.first == NEW_MADE);
        CHECK(st.second == NEW_UPDATED);
    }
    CHECK(records_of(a, dir, entries_of(dir, 0, "PATTERN BIN")) == pattern_file(1, 36864));
    // Nothing else changed: the directory's first sector (entries 8 and 9, the SFCB in 11) and 65 of records.
    size_t sectors = 0;
    for (size_t n = 0; n < before.size() / SECTOR; ++n)
        sectors += std::memcmp(before.data() + n * SECTOR, r.media->bytes().data() + n * SECTOR, SECTOR) != 0;
    CHECK_EQ(sectors, size_t{1 + 65});
}

int main()
{
    return run_all();
}
