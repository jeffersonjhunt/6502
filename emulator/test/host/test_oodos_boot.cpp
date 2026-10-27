/**
 * @file test_oodos_boot.cpp
 * @brief OOLDR, OODOS's loader (oodos/ldr/ooldr.s): /BOOT.BIN through C80CR, DRIVES.TXT, OODOS.SYS.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The committed card, sd/oodos.sparse (oodos/tools/make_card.sh), boots
 * through the ROM BIOS's C80CR on backplane.machine, as the monitor's user
 * types it: OOLDR reads /OODOS/DRIVES.TXT, mounts A: and B:, finds OODOS.SYS
 * on A: with the CP/M reader (oodos/sys/cpmread.s) and puts its segments in
 * block 31 and blocks 60-62. The card's A: holds OODOS itself as OODOS.SYS
 * and OOLDR's test system (oodos/ldr/testsys.s) as TESTSYS.SYS; here the
 * test system is OODOS.SYS (parts() swaps the names in its copy of A:), so
 * its cold boot records the windows and P it found at $7FF00 and stops.
 * test_oodos_e2e boots OODOS itself.
 *
 * Every refusal (hw/oodos.md, "The system, phase 2" and "Drives") is made
 * here, not committed: cards are built in memory, FAT16 on the whole card,
 * from the committed card's files with DRIVES.TXT or A.IMG's bytes changed.
 * The FAT and CP/M readers here are the test's own; OOLDR's are on the card.
 */

#include "harness.h"
#include "sd_images.h"

#include "fat_volume.h"
#include "sd_card.h"
#include "sd_media.h"
#include "session.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60
    const std::string MACHINE = EMU_MACHINE_DIR "/backplane.machine";
    const std::string CARD = "oodos.sparse";
    constexpr size_t SECTOR = 512, RECORD = 128, ENTRY = 32;
    const std::string SYS_NAME = "OODOS   SYS";
    const std::string TEST_NAME = "TESTSYS SYS"; // on the card; OODOS.SYS in parts()
    const std::string MOVED_NAME = "OODOS   REL"; // the real OODOS.SYS, out of the way in parts()
    constexpr uint32_t TEST_RECORD = 0x7FF00; // testsys.s: what its cold boot found

    using Bytes = std::vector<uint8_t>;

    uint8_t pattern(unsigned seed, size_t i) { return static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF); }
    Bytes text(const std::string &s) { return Bytes(s.begin(), s.end()); }

    const Bytes &committed()
    {
        static const Bytes card = sd_image(CARD);
        return card;
    }

    /// A file's bytes off a card, by the emulator's FAT reader; empty if it isn't there.
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

    // ---- a card made here -----------------------------------------------------

    struct File
    {
        std::string path; ///< "/NAME.EXT" or "/OODOS/NAME.EXT"
        Bytes bytes;
    };

    void put16(Bytes &b, size_t at, unsigned v)
    {
        b[at] = static_cast<uint8_t>(v);
        b[at + 1] = static_cast<uint8_t>(v >> 8);
    }
    void put32(Bytes &b, size_t at, uint32_t v)
    {
        put16(b, at, v & 0xFFFF);
        put16(b, at + 2, v >> 16);
    }

    /// An 8.3 directory entry's name, "NAME.EXT" to "NAME    EXT".
    std::string name83(const std::string &n)
    {
        const size_t dot = n.find('.');
        std::string base = n.substr(0, dot), ext = dot == std::string::npos ? "" : n.substr(dot + 1);
        base.resize(8, ' ');
        ext.resize(3, ' ');
        return base + ext;
    }

    /// FAT16 on the whole card (a superfloppy), 4 KB clusters, each file in one run: what
    /// fat_volume.h reads, with /OODOS a directory. Short names only.
    Bytes make_card(const std::vector<File> &files)
    {
        constexpr unsigned SPC = 8, CLUSTER = SPC * SECTOR, ROOT_ENTRIES = 512;
        auto clusters_of = [](size_t n) { return static_cast<uint32_t>((n + CLUSTER - 1) / CLUSTER); };
        uint32_t used = 1; // /OODOS
        for (const File &f : files)
            used += clusters_of(f.bytes.size());
        const uint32_t clusters = std::max<uint32_t>(used + 16, 4200); // FAT16: at least 4,085
        const uint32_t fat_sectors = static_cast<uint32_t>(((clusters + 2) * 2 + SECTOR - 1) / SECTOR);
        const uint32_t root = 1 + fat_sectors, data = root + ROOT_ENTRIES * ENTRY / SECTOR;
        const uint32_t total = data + clusters * SPC;
        Bytes card(size_t{total} * SECTOR);

        uint8_t *b = card.data();
        b[0] = 0xEB, b[1] = 0x3C, b[2] = 0x90;
        std::memcpy(b + 3, "OOTEST  ", 8);
        put16(card, 11, SECTOR);
        b[13] = SPC;
        put16(card, 14, 1);  // reserved
        b[16] = 1;           // one FAT
        put16(card, 17, ROOT_ENTRIES);
        put32(card, 32, total);
        b[21] = 0xF8;
        put16(card, 22, fat_sectors);
        b[38] = 0x29;
        std::memcpy(b + 43, "NO NAME    FAT16   ", 19);
        b[510] = 0x55, b[511] = 0xAA;

        auto fat = [&](uint32_t c, unsigned v) { put16(card, SECTOR + 2 * size_t{c}, v); };
        fat(0, 0xFFF8);
        fat(1, 0xFFFF);
        uint32_t next = 2;
        auto chain = [&](size_t bytes) -> uint32_t {
            const uint32_t n = clusters_of(bytes), first = n ? next : 0;
            for (uint32_t i = 0; i < n; ++i, ++next)
                fat(next, i + 1 < n ? next + 1 : 0xFFFF);
            return first;
        };
        auto at = [&](uint32_t c) { return (size_t{data} + size_t{c - 2} * SPC) * SECTOR; };
        auto entry = [&](size_t where, const std::string &name, uint8_t attr, uint32_t cluster, size_t size) {
            std::memcpy(&card[where], name.data(), 11);
            card[where + 11] = attr;
            put16(card, where + 26, cluster);
            put32(card, where + 28, static_cast<uint32_t>(size));
        };
        const uint32_t dir = chain(CLUSTER);
        size_t root_at = size_t{root} * SECTOR, dir_at = at(dir);
        entry(root_at, "OODOS      ", FatVolume::DIRECTORY, dir, 0);
        root_at += ENTRY;
        entry(dir_at, ".          ", FatVolume::DIRECTORY, dir, 0);
        entry(dir_at + ENTRY, "..         ", FatVolume::DIRECTORY, 0, 0);
        dir_at += 2 * ENTRY;
        for (const File &f : files)
        {
            const bool in_dir = f.path.rfind("/OODOS/", 0) == 0;
            const uint32_t first = chain(f.bytes.size());
            if (first)
                std::copy(f.bytes.begin(), f.bytes.end(), card.begin() + static_cast<std::ptrdiff_t>(at(first)));
            size_t &where = in_dir ? dir_at : root_at;
            entry(where, name83(f.path.substr(in_dir ? 7 : 1)), 0, first, f.bytes.size());
            where += ENTRY;
        }
        return card;
    }

    // ---- CP/M on an image -----------------------------------------------------

    /// An oodos-8m or oodos-32m image, read and changed by its bytes.
    struct Drive
    {
        Bytes img;
        unsigned bls{4096}, exm{1}, entries{1024};

        static Drive of(Bytes img)
        {
            Drive d{std::move(img)};
            if (d.img.size() == 32u << 20)
                d.bls = 8192, d.exm = 3, d.entries = 2048;
            return d;
        }
        uint8_t *dir(unsigned i) { return &img[size_t{i} * ENTRY]; }
        /// The file's entries in user `user`, in extent order.
        std::vector<unsigned> entries_of(unsigned user, const std::string &name)
        {
            std::vector<std::pair<unsigned, unsigned>> found;
            for (unsigned i = 0; i < entries; ++i)
            {
                const uint8_t *e = dir(i);
                bool same = e[0] == user;
                for (unsigned c = 0; c < 11 && same; ++c)
                    same = (e[1 + c] & 0x7F) == static_cast<uint8_t>(name[c]);
                if (same)
                    found.push_back({unsigned{e[14]} * 32 + e[12], i});
            }
            std::sort(found.begin(), found.end());
            std::vector<unsigned> out;
            for (const auto &f : found)
                out.push_back(f.second);
            return out;
        }
        unsigned pointer(unsigned i, unsigned p) { return unsigned{dir(i)[16 + 2 * p]} | unsigned{dir(i)[17 + 2 * p]} << 8; }
        /// The file's bytes, every record of every entry.
        Bytes file(const std::string &name)
        {
            Bytes out;
            for (unsigned i : entries_of(0, name))
            {
                const uint8_t *e = dir(i);
                const unsigned records = ((unsigned{e[14]} * 32 + e[12]) & exm) * 128 + e[15];
                for (unsigned r = 0; r < records; ++r)
                {
                    const size_t from = size_t{pointer(i, static_cast<unsigned>(r * RECORD / bls))} * bls + r * RECORD % bls;
                    out.insert(out.end(), img.begin() + static_cast<std::ptrdiff_t>(from),
                               img.begin() + static_cast<std::ptrdiff_t>(from + RECORD));
                }
            }
            return out;
        }
        /// Where OODOS.SYS's header record is in the image.
        uint8_t *header() { return &img[size_t{pointer(entries_of(0, SYS_NAME).at(0), 0)} * bls]; }
        /// A free entry, not an SFCB's.
        unsigned free_entry()
        {
            for (unsigned i = 1; i < entries; ++i)
                if ((i & 3) != 3 && dir(i)[0] == 0xE5)
                    return i;
            return 0;
        }
    };

    /// A fresh CP/M 3 drive as mkfs.cpm -t makes one, label mode 0x31, holding `bytes` as
    /// 0:OODOS.SYS: oodos-32m when `big`, its entries in the directory last extent first.
    Drive make_drive(bool big, Bytes bytes)
    {
        Drive d = Drive::of(Bytes(size_t{big ? 32u : 8u} << 20));
        std::fill(d.img.begin(), d.img.begin() + static_cast<std::ptrdiff_t>(d.entries * ENTRY), 0xE5);
        for (unsigned i = 3; i < d.entries; i += 4)
            std::memset(d.dir(i), 0, ENTRY), d.dir(i)[0] = 0x21;
        std::memcpy(d.dir(0), "\x20OODOS      \x31", 13);
        std::memset(d.dir(0) + 13, 0, ENTRY - 13);
        bytes.resize((bytes.size() + RECORD - 1) / RECORD * RECORD);
        const unsigned records = static_cast<unsigned>(bytes.size() / RECORD), per = (d.exm + 1) * 128;
        const unsigned n = (records + per - 1) / per;
        std::vector<unsigned> slots; // the first n free, not an SFCB's: the last extent's first
        for (unsigned i = 1; slots.size() < n; ++i)
            if ((i & 3) != 3)
                slots.insert(slots.begin(), i);
        unsigned block = static_cast<unsigned>(d.entries * ENTRY / d.bls);
        for (unsigned k = 0; k < n; ++k)
        {
            uint8_t *e = d.dir(slots[k]);
            const unsigned here = std::min(per, records - k * per), extent = k * (d.exm + 1) + (here - 1) / 128;
            std::memset(e, 0, ENTRY);
            std::memcpy(e + 1, SYS_NAME.data(), 11);
            e[12] = static_cast<uint8_t>(extent & 31);
            e[14] = static_cast<uint8_t>(extent >> 5);
            e[15] = static_cast<uint8_t>(here - (here - 1) / 128 * 128);
            for (unsigned p = 0; p * d.bls / RECORD < here; ++p, ++block)
            {
                put16(d.img, size_t{slots[k]} * ENTRY + 16 + 2 * p, block);
                const size_t from = (size_t{k} * per + p * d.bls / RECORD) * RECORD;
                const size_t len = std::min<size_t>(d.bls, bytes.size() - from);
                std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(from), len,
                            d.img.begin() + static_cast<std::ptrdiff_t>(size_t{block} * d.bls));
            }
        }
        return d;
    }

    // ---- the committed card's parts ------------------------------------------

    struct Parts
    {
        Bytes boot, drives, a, b;
        std::vector<File> files() const
        {
            return {{"/BOOT.BIN", boot}, {"/OODOS/DRIVES.TXT", drives}, {"/OODOS/A.IMG", a}, {"/OODOS/B.IMG", b}};
        }
        Bytes card() const { return make_card(files()); }
    };
    /// The committed card's files, with the test system as A:'s OODOS.SYS.
    const Parts &parts()
    {
        static const Parts p = [] {
            const Bytes &c = committed();
            Parts out{file_of(c, "/BOOT.BIN"), file_of(c, "/OODOS/DRIVES.TXT"), file_of(c, "/OODOS/A.IMG"),
                      file_of(c, "/OODOS/B.IMG")};
            Drive a = Drive::of(out.a);
            const std::vector<unsigned> real = a.entries_of(0, SYS_NAME), test = a.entries_of(0, TEST_NAME);
            for (unsigned i : real)
                std::memcpy(a.dir(i) + 1, MOVED_NAME.data(), 11);
            for (unsigned i : test)
                std::memcpy(a.dir(i) + 1, SYS_NAME.data(), 11);
            out.a = a.img;
            return out;
        }();
        return p;
    }
    Bytes with_drives(const std::string &drives_txt)
    {
        Parts p = parts();
        p.drives = text(drives_txt);
        return p.card();
    }

    // ---- booting --------------------------------------------------------------

    struct Boot
    {
        std::unique_ptr<Session> s;
        std::string out; ///< from C80CR on
        Bytes before;    ///< blocks 31 and 60-62, before C80CR (block 31 first)
    };

    constexpr uint32_t BLOCK31 = 31u << 14, BLOCK60 = 60u << 14, BLOCK_SIZE = 1u << 14;
    bool saved(uint32_t phys)
    {
        return (phys >= BLOCK31 && phys < BLOCK31 + BLOCK_SIZE) || (phys >= BLOCK60 && phys < BLOCK60 + 3 * BLOCK_SIZE);
    }
    uint8_t peek_saved(const Bytes &before, uint32_t phys)
    {
        return phys < BLOCK60 ? before[phys - BLOCK31] : before[BLOCK_SIZE + phys - BLOCK60];
    }

    /// Run C80CR again on `b`'s session: until the system stops or OOLDR is back at the monitor.
    void c80c(Boot &b)
    {
        b.before.clear();
        for (uint32_t a = BLOCK31; a < BLOCK31 + BLOCK_SIZE; ++a)
            b.before.push_back(b.s->peek_physical(a));
        for (uint32_t a = BLOCK60; a < BLOCK60 + 3 * BLOCK_SIZE; ++a)
            b.before.push_back(b.s->peek_physical(a));
        b.s->send("C80CR\r");
        b.out.clear();
        for (int i = 0; i < 900 && !b.s->stopped(); ++i)
        {
            b.s->run(FRAME);
            b.out += b.s->take_output();
            const size_t ldr = b.out.find("\rOOLDR ");
            if (ldr != std::string::npos && b.out.find("\r\\", ldr) != std::string::npos)
                break;
        }
    }

    /// Booted to the monitor with `media` in the slot, then C80CR.
    Boot boot(std::shared_ptr<SdMedia> media)
    {
        Boot b;
        std::string error;
        b.s = Session::create({MACHINE, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!b.s || !b.s->insert_sd(std::move(media), error))
        {
            std::printf("  %s\n", error.c_str());
            b.s.reset();
            return b;
        }
        std::string out;
        for (int i = 0; i < 120 && out.find("BACKPLANE BIOS 0.9\r\\\r") == std::string::npos; ++i)
        {
            b.s->run(FRAME);
            out += b.s->take_output();
        }
        c80c(b);
        return b;
    }
    Boot boot(const Bytes &card) { return boot(std::make_shared<MemoryMedia>(card, "card")); }

    /// OOLDR said `why` (a line of its own) and went back to the monitor, the windows as C80CR left them.
    void refused(const Bytes &card, const std::string &why)
    {
        Boot b = boot(card);
        REQUIRE(b.s != nullptr);
        const bool said = b.out.find("\r" + why + "\r\\") != std::string::npos;
        std::printf("  %s\n", said ? why.c_str() : ("not \"" + why + "\" but: " + b.out).c_str());
        CHECK(said);
        CHECK(!b.s->stopped());
        CHECK_EQ(b.s->peek(0xC681), 1);
        CHECK_EQ(b.s->peek(0xC682), 2);
        CHECK_EQ(b.s->peek(0xC683) & 0x3F, 63); // still the ROM BIOS
    }

    struct Segment
    {
        uint32_t at;
        unsigned length, record;
    };
    std::vector<Segment> segments_of(const Bytes &sys)
    {
        std::vector<Segment> out;
        for (unsigned i = 0; i < sys[8]; ++i)
        {
            const uint8_t *s = &sys[16 + 8 * i];
            out.push_back({uint32_t{s[0]} | uint32_t{s[1]} << 8 | uint32_t{s[2]} << 16, unsigned{s[3]} | unsigned{s[4]} << 8,
                           unsigned{s[5]} | unsigned{s[6]} << 8});
        }
        return out;
    }

    /// The test system came up from `sys`: every segment byte for byte where it goes, the bytes
    /// around them as they were, and the entry as hw/oodos.md has it.
    void came_up(const Boot &b, const Bytes &sys)
    {
        REQUIRE(b.s != nullptr);
        if (!b.s->stopped())
            std::printf("  it didn't stop: %s\n", b.out.c_str());
        REQUIRE(b.s->stopped());
        CHECK(b.out.find("\rOOLDR 0.2\r") != std::string::npos);
        const std::vector<Segment> segs = segments_of(sys);
        CHECK_EQ(segs.size(), size_t{6});
        auto inside = [&](uint32_t a) {
            return std::any_of(segs.begin(), segs.end(), [&](const Segment &s) { return a >= s.at && a < s.at + s.length; });
        };
        for (const Segment &s : segs)
        {
            size_t wrong = 0;
            for (unsigned i = 0; i < s.length; ++i)
                wrong += b.s->peek_physical(s.at + i) != sys[s.record * RECORD + i];
            std::printf("  $%05X, %u bytes from record %u: %zu wrong\n", s.at, s.length, s.record, wrong);
            CHECK_EQ(wrong, size_t{0});
            for (uint32_t a : {s.at - 1, s.at + s.length}) // the bytes either side, if no one's
                if (!inside(a) && saved(a) && a != TEST_RECORD)
                    CHECK_EQ(b.s->peek_physical(a), peek_saved(b.before, a));
        }
        // The cold boot's record: W0 still block 0, W1-W3 and VEC 60, 61, 62, 62 (writable), I set, D clear.
        std::string sig;
        for (uint32_t i = 0; i < 8; ++i)
            sig += static_cast<char>(b.s->peek_physical(TEST_RECORD + i));
        CHECK_EQ(sig, std::string("TESTSYS!"));
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 8), 0);
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 9), 60);
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 10), 61);
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 11), 62);
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 12), 62);
        const uint8_t p = b.s->peek_physical(TEST_RECORD + 13);
        CHECK_EQ(p & 0x04, 0x04); // I
        CHECK_EQ(p & 0x08, 0x00); // D
        CHECK_EQ(b.s->peek_physical(TEST_RECORD + 15), 0xA5);
    }

    /// Unit `n` is mounted as `name`, its size and flag.
    void mounted(const Boot &b, unsigned n, const std::string &name, bool read_only)
    {
        const SdCard::Unit &u = b.s->sd()->unit(n);
        CHECK(u.mounted);
        CHECK_EQ(u.name, name);
        CHECK_EQ(u.sectors, uint64_t{16384});
        CHECK_EQ(u.read_only, read_only);
    }

    /// The committed card's A.IMG with `change` made to it, as a card.
    template <typename F>
    Bytes with_a(F change)
    {
        Parts p = parts();
        Drive a = Drive::of(p.a);
        change(a);
        p.a = a.img;
        return p.card();
    }
    /// ...or to OODOS.SYS's header record.
    template <typename F>
    Bytes with_header(F change)
    {
        return with_a([&](Drive &a) { change(a.header()); });
    }
} // namespace

TEST(the_committed_card_holds_what_make_card_sh_says)
{
    const Parts p = parts();
    REQUIRE(!p.boot.empty());
    CHECK_EQ(committed().size(), size_t{32} << 20);
    CHECK_EQ(p.a.size(), size_t{8} << 20);
    CHECK_EQ(p.b.size(), size_t{8} << 20);
    CHECK_EQ(std::string(p.drives.begin(), p.drives.end()).find("\nA: /OODOS/A.IMG\nB: /OODOS/B.IMG\n") != std::string::npos,
             true);
    // On the card itself: OODOS, the test system and HELLO.COM, each in user 0
    Drive card_a = Drive::of(file_of(committed(), "/OODOS/A.IMG"));
    const Bytes oodos = card_a.file(SYS_NAME);
    REQUIRE(oodos.size() >= RECORD);
    CHECK(std::equal(oodos.begin(), oodos.begin() + 4, "OOSY"));
    CHECK_EQ(card_a.entries_of(0, TEST_NAME).size(), size_t{2});
    CHECK(card_a.file("HELLO   COM").size() > 0);
    // In parts(), the test system is OODOS.SYS
    Drive a = Drive::of(p.a);
    const std::vector<unsigned> e = a.entries_of(0, SYS_NAME);
    CHECK_EQ(e.size(), size_t{2}); // over a directory entry: 268 records, 256 to an entry
    const Bytes sys = a.file(SYS_NAME);
    REQUIRE(sys.size() == 268 * RECORD);
    CHECK(std::equal(sys.begin(), sys.begin() + 4, "OOSY"));
    // Segment 6 crosses record 256, the second entry's start; segment 5 record 128, a logical extent's.
    const std::vector<Segment> segs = segments_of(sys);
    REQUIRE(segs.size() == 6u);
    CHECK(segs[5].record < 256 && segs[5].record + segs[5].length / RECORD > 256);
    CHECK(segs[4].record < 128 && segs[4].record + segs[4].length / RECORD > 128);
    for (unsigned i = 0; i < 16384; ++i) // and the data segments are testsys.s's patterns
        if (sys[segs[5].record * RECORD + i] != pattern(12, i))
        {
            CHECK(false);
            break;
        }
    Drive b = Drive::of(p.b);
    CHECK_EQ(b.dir(0)[0], 0x20);
    CHECK_EQ(b.dir(0)[12], 0x31);
    CHECK_EQ(b.free_entry(), 1u); // empty
}

TEST(the_committed_card_boots_the_test_system)
{
    const Bytes sys = Drive::of(parts().a).file(SYS_NAME);
    Boot b = boot(parts().card());
    came_up(b, sys);
    mounted(b, 1, "A.IMG", false);
    mounted(b, 2, "B.IMG", false);
    for (unsigned n = 3; n <= 7; ++n)
        CHECK(!b.s->sd()->unit(n).mounted);
    CHECK_EQ(b.s->peek(0xC680), 0);    // W0 as the system left it, still block 0...
    CHECK_EQ(b.s->peek(0xC681), 31);   // ...and W1 where its cold boot put it
}

TEST(drives_txt_in_every_form_the_grammar_allows)
{
    const Bytes sys = Drive::of(parts().a).file(SYS_NAME);
    // A BOM, CR LF, lower case, tabs, comments, RO in any case, and no ending on the last line.
    Boot b = boot(with_drives("\xEF\xBB\xBF# the drives\r\n\r\n  \t\r\na:\t/oodos/a.img  # the system\r\n"
                              "\tB: /OODOS/B.IMG\tRo#work"));
    came_up(b, sys);
    mounted(b, 1, "A.IMG", false);
    mounted(b, 2, "B.IMG", true);
    // RO straight before a comment, and a read-only A:, which the loader only reads.
    Boot c = boot(with_drives("A: /OODOS/A.IMG RO#ro\n"));
    came_up(c, sys);
    mounted(c, 1, "A.IMG", true);
    CHECK(!c.s->sd()->unit(2).mounted);
}

TEST(drives_txt_errors_by_their_line)
{
    const std::string L = "DRIVES.TXT LINE ", WORD = ": ONLY RO AND A COMMENT MAY FOLLOW THE PATH";
    const std::string A = "A: /OODOS/A.IMG\n";
    const std::string long_path = "/" + std::string(508, 'P'); // 509 bytes
    const std::pair<std::string, std::string> bad[] = {
        {"A: /OODOS/A.IMG R0\n", L + "1" + WORD},             // a mistyped RO
        {A + "B: /OODOS/B.IMG RO RO\n", L + "2" + WORD},      // a second RO
        {A + "B: /OODOS/B .IMG\n", L + "2" + WORD},           // a space in the path
        {A + "B: /OODOS/B.IMG ROM\n", L + "2" + WORD},
        {A + "B: /OODOS/B.IMG R\n", L + "2" + WORD},
        {A + "B: /OODOS/B.IMG\n\na: /OODOS/B.IMG\n", L + "4: A: NAMED TWICE"},
        {A + "# b\nB: /oodos/a.img RO\n", L + "3: /oodos/a.img IS DRIVE A: ALREADY"},
        {"A: /oodos/a.img\nB: /OODOS/A.IMG\n", L + "2: /OODOS/A.IMG IS DRIVE A: ALREADY"}, // either way round
        {"A: /OODOS/A.IMG\rB: /OODOS/B.IMG\n", L + "1: A CR NOT BEFORE LF"},
        {A + "# B: one day\rC: /C.IMG\n", L + "2: A CR NOT BEFORE LF"}, // in a comment too
        {"A: /OODOS/A.IMG\r\r\n", L + "1: A CR NOT BEFORE LF"},
        {A + "B: /OODOS/B.IMG\r", L + "2: A CR NOT BEFORE LF"},          // at the end, with no LF
        {A + "H: /OODOS/B.IMG\n", L + "2: NOT A DRIVE A-G AND A COLON"},
        {"A /OODOS/A.IMG\n", L + "1: NOT A DRIVE A-G AND A COLON"},
        {"AB: /OODOS/A.IMG\n", L + "1: NOT A DRIVE A-G AND A COLON"},
        {A + "B", L + "2: NOT A DRIVE A-G AND A COLON"},
        {"\xEF\xBB\xBF\xEF\xBB\xBF" "A: /OODOS/A.IMG\n", L + "1: NOT A DRIVE A-G AND A COLON"}, // one BOM only
        {"A:/OODOS/A.IMG\n", L + "1: NO SPACE OR TAB AFTER THE COLON"},
        {A + "B:\n", L + "2: NO SPACE OR TAB AFTER THE COLON"},
        {"A: OODOS/A.IMG\n", L + "1: THE PATH DOESN'T START WITH /"},
        {"A: #/OODOS/A.IMG\n", L + "1: THE PATH DOESN'T START WITH /"},
        {"A: \t\n", L + "1: THE PATH DOESN'T START WITH /"},
        {"A: /OODOS/A\x01.IMG\n", L + "1: A CONTROL CHARACTER IN THE PATH"},
        {"A: /OODOS/A\x7F.IMG\n", L + "1: A CONTROL CHARACTER IN THE PATH"},
        {A + "B: " + long_path + "P\n", L + "2: THE PATH IS OVER 509 BYTES"},
    };
    for (const auto &b : bad)
        refused(with_drives(b.first), b.second);
    // 509 bytes is a path: it passes the grammar and goes to MOUNT.
    refused(with_drives(A + "B: " + long_path + "\n"), L + "2: " + long_path + " IS NOT ON THE CARD");
}

TEST(drives_it_cannot_mount)
{
    refused(with_drives("B: /OODOS/B.IMG\n"), "DRIVES.TXT NAMES NO A:");
    refused(with_drives(""), "DRIVES.TXT NAMES NO A:");
    refused(with_drives("A: /OODOS/A.IMG\n\nB: /OODOS/NONE.IMG\n"), "DRIVES.TXT LINE 3: /OODOS/NONE.IMG IS NOT ON THE CARD");
    refused(with_drives("A: /OODOS/A.IMG\nB: /BOOT.BIN\n"), "DRIVES.TXT LINE 2: /BOOT.BIN IS NOT 8 MB OR 32 MB");
    refused(with_drives("# A: is DRIVES.TXT\nA: /OODOS/DRIVES.TXT\n"), "DRIVES.TXT LINE 2: /OODOS/DRIVES.TXT IS NOT 8 MB OR 32 MB");
    Parts p = parts();
    refused(make_card({{"/BOOT.BIN", p.boot}, {"/OODOS/A.IMG", p.a}}), "NO /OODOS/DRIVES.TXT ON THE CARD");
}

TEST(units_no_line_names_are_unmounted)
{
    // A first boot leaves unit 3 mounted (C: is refused once mounted); C: then becomes a
    // comment, and the next boot unmounts it.
    Parts p = parts();
    p.drives = text("A: /OODOS/A.IMG\nC: /BOOT.BIN\n");
    auto media = std::make_shared<MemoryMedia>(p.card(), "card");
    Boot b = boot(media);
    REQUIRE(b.s != nullptr);
    CHECK(b.out.find("\rDRIVES.TXT LINE 2: /BOOT.BIN IS NOT 8 MB OR 32 MB\r\\") != std::string::npos);
    CHECK(b.s->sd()->unit(3).mounted);
    std::string error;
    auto v = FatVolume::open(*media, error);
    FatVolume::Entry e;
    std::vector<FatVolume::Extent> ext;
    REQUIRE(v && v->find("/OODOS/DRIVES.TXT", e) == FatVolume::Status::Ok && v->extents(e, ext) == FatVolume::Status::Ok);
    uint8_t s[SECTOR];
    REQUIRE(media->read(ext[0].card, s));
    s[16] = '#'; // "C:" on the second line
    REQUIRE(media->write(ext[0].card, s));
    c80c(b);
    came_up(b, Drive::of(p.a).file(SYS_NAME));
    CHECK(!b.s->sd()->unit(3).mounted);
}

TEST(oodos_sys_headers_it_refuses)
{
    const std::string S = "OODOS.SYS: ";
    auto set16 = [](uint8_t *h, size_t at, unsigned v) {
        h[at] = static_cast<uint8_t>(v);
        h[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    auto seg_at = [](uint8_t *h, unsigned n, uint32_t at) { // segment n, from 1
        h[16 + 8 * (n - 1)] = static_cast<uint8_t>(at);
        h[17 + 8 * (n - 1)] = static_cast<uint8_t>(at >> 8);
        h[18 + 8 * (n - 1)] = static_cast<uint8_t>(at >> 16);
    };
    refused(with_header([](uint8_t *h) { h[3] = 'Z'; }), S + "NOT OODOS.SYS (NO OOSY)");
    refused(with_header([](uint8_t *h) { h[4] = 1; }), S + "MAJOR VERSION 1, NOT 0");
    refused(with_header([](uint8_t *h) { h[4] = 255; }), S + "MAJOR VERSION 255, NOT 0");
    refused(with_header([](uint8_t *h) { h[8] = 0; }), S + "0 SEGMENTS, NOT 1-8");
    refused(with_header([](uint8_t *h) { h[8] = 9; }), S + "9 SEGMENTS, NOT 1-8");
    refused(with_header([&](uint8_t *h) { set16(h, 6, 0x1000); }), S + "ENTRY NOT IN $C800-$FFFF");
    refused(with_header([&](uint8_t *h) { set16(h, 6, 0xC7FF); }), S + "ENTRY NOT IN $C800-$FFFF");
    refused(with_header([&](uint8_t *h) { set16(h, 16 + 8 * 5 + 3, 0x4001); }), S + "SEGMENT 6 IS OVER 16 KB");
    refused(with_header([&](uint8_t *h) { set16(h, 16 + 3, 0xFFFF); }), S + "SEGMENT 1 IS OVER 16 KB");
    const std::string OUT = " IS NOT IN BLOCK 31 OR BLOCKS 60-62";
    refused(with_header([&](uint8_t *h) { seg_at(h, 1, 0x7BF00); }), S + "SEGMENT 1" + OUT);  // starts below 31
    refused(with_header([&](uint8_t *h) { seg_at(h, 1, 0x7FE00); }), S + "SEGMENT 1" + OUT);  // runs past it
    refused(with_header([&](uint8_t *h) { seg_at(h, 1, 0x01000); }), S + "SEGMENT 1" + OUT);  // block 0
    refused(with_header([&](uint8_t *h) { seg_at(h, 1, 0xC0000); }), S + "SEGMENT 1" + OUT);  // between
    refused(with_header([&](uint8_t *h) { seg_at(h, 4, 0xFBFFC); }), S + "SEGMENT 4" + OUT);  // into block 63
    refused(with_header([&](uint8_t *h) { seg_at(h, 4, 0x1F0000); }), S + "SEGMENT 4" + OUT); // past 1 MB
    refused(with_header([&](uint8_t *h) { seg_at(h, 2, 0xF4064); }), S + "SEGMENTS 2 AND 5 OVERLAP");
    refused(with_header([&](uint8_t *h) { seg_at(h, 6, 0xF3FFF); }), S + "SEGMENTS 5 AND 6 OVERLAP"); // by a byte
    refused(with_header([&](uint8_t *h) { seg_at(h, 4, 0xF8800); }), S + "SEGMENTS 3 AND 4 OVERLAP"); // the same start
    refused(with_header([&](uint8_t *h) { set16(h, 16 + 8 * 5 + 5, 141); }), S + "SEGMENT 6 RUNS PAST THE FILE'S END");
}

TEST(oodos_sys_and_the_cp_m_reader)
{
    // Not in user 0: OODOS.SYS in user 1 isn't OODOS.SYS.
    refused(with_a([](Drive &a) {
                for (unsigned i : a.entries_of(0, SYS_NAME))
                    a.dir(i)[0] = 1;
            }),
            "NO OODOS.SYS ON A:");
    const std::string DAMAGED = "A: A DAMAGED DIRECTORY ENTRY FOR OODOS.SYS";
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[0])[0] = 0xE5; }), DAMAGED); // the first entry deleted
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[0])[15] = 0x40; }), DAMAGED); // the first part full
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[1])[15] = 0x81; }), DAMAGED); // RC over 128
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[1])[12] = 32; }), DAMAGED); // EX over 31
    refused(with_a([](Drive &a) { // the first entry twice
                const unsigned i = a.entries_of(0, SYS_NAME)[0];
                std::memcpy(a.dir(a.free_entry()), a.dir(i), ENTRY);
            }),
            DAMAGED);
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[0])[16 + 2] = 3; }), DAMAGED); // a block of the directory
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[0])[16 + 3] = 0x08; }), DAMAGED); // past DSM
    refused(with_a([](Drive &a) { a.dir(a.entries_of(0, SYS_NAME)[1])[14] = 1; }), // extent 34: entry 17
            "A: OODOS.SYS HAS TOO MANY DIRECTORY ENTRIES");
    refused(with_a([](Drive &a) { // no records
                const std::vector<unsigned> e = a.entries_of(0, SYS_NAME);
                a.dir(e[1])[0] = 0xE5;
                a.dir(e[0])[12] = a.dir(e[0])[15] = 0;
            }),
            "OODOS.SYS: EMPTY");
}

TEST(entries_the_reader_skips)
{
    // Copies of OODOS.SYS's first entry, wrong, where the reader must not look: deleted, in user
    // 3, an XFCB (user 16), the label's and an SFCB's places.
    const Bytes sys = Drive::of(parts().a).file(SYS_NAME);
    Boot b = boot(with_a([](Drive &a) {
        const unsigned first = a.entries_of(0, SYS_NAME)[0];
        for (uint8_t user : {uint8_t{3}, uint8_t{16}, uint8_t{0x20}, uint8_t{0x21}, uint8_t{0xE5}}) // $E5 last: it stays free
        {
            uint8_t *e = a.dir(a.free_entry());
            std::memcpy(e, a.dir(first), ENTRY);
            e[0] = user;
            e[16] = 9; // other blocks: read, they'd be wrong
        }
    }));
    came_up(b, sys);
}

TEST(an_oodos_32m_drive_with_its_entries_out_of_order)
{
    // The same OODOS.SYS on oodos-32m: 8 KB blocks, EXM 3. Padded to 600 records it takes two
    // entries (512 records each), written last extent first.
    const Parts p = parts();
    Bytes sys = Drive::of(p.a).file(SYS_NAME);
    sys.resize(600 * RECORD, 0x1A);
    Drive a = make_drive(true, sys);
    REQUIRE(a.entries_of(0, SYS_NAME).size() == 2u);
    CHECK_EQ(a.entries_of(0, SYS_NAME)[0], 2u); // the first extent's entry comes second
    CHECK(a.file(SYS_NAME) == sys);
    Boot b = boot(make_card({{"/BOOT.BIN", p.boot}, {"/OODOS/DRIVES.TXT", text("A: /OODOS/A.IMG\n")}, {"/OODOS/A.IMG", a.img}}));
    came_up(b, sys);
    CHECK_EQ(b.s->sd()->unit(1).sectors, uint64_t{65536});
    // And on oodos-8m made here, the same way.
    Drive c = make_drive(false, sys);
    CHECK_EQ(c.entries_of(0, SYS_NAME).size(), size_t{3});
    came_up(boot(make_card({{"/BOOT.BIN", p.boot}, {"/OODOS/DRIVES.TXT", text("A: /OODOS/A.IMG\n")}, {"/OODOS/A.IMG", c.img}})), sys);
}

int main()
{
    return run_all();
}
