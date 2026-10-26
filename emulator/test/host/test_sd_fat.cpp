/**
 * @file test_sd_fat.cpp
 * @brief The SD card's FAT: fat_volume.h, and MOUNT, UNMOUNT and LIST on the card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The images in sd/ were made by mkfs.fat, mtools and sfdisk
 * (tools/make_sd_images.sh), not by anything here, so the reader is checked
 * against what a PC writes: FAT12 on a whole card, FAT16 and FAT32 in an
 * MBR's first partition, long names, a fragmented file, a read-only one, a
 * directory spanning clusters. A file's byte i is
 * ((i >> 9) * 31 + i * 7 + seed) & 0xFF, its seed given by the script.
 */

#include "harness.h"
#include "sd_images.h"

#include "fat_volume.h"
#include "sd_card.h"
#include "sd_media.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
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

    /// The FAT12 image's longest name: 71 characters, in six long-name entries.
    const std::string LONGEST = "This name is longer than the fifty-six bytes a listing keeps of it.txt";

    uint8_t content(unsigned seed, size_t i) { return static_cast<uint8_t>(((i >> 9) * 31 + i * 7 + seed) & 0xFF); }

    std::vector<uint8_t> sparse(const std::string &name)
    {
        std::vector<uint8_t> bytes = sd_image(name);
        CHECK(!bytes.empty());
        return bytes;
    }

    std::shared_ptr<MemoryMedia> card_image(const std::string &name)
    {
        return std::make_shared<MemoryMedia>(sparse(name), name);
    }

    /// A file's sectors, read through its extents (a last part-sector whole, the cluster's slack after the end).
    std::vector<uint8_t> file_bytes(SdMedia &media, const FatVolume &v, const FatVolume::Entry &e)
    {
        std::vector<FatVolume::Extent> ext;
        std::vector<uint8_t> out;
        CHECK(v.extents(e, ext) == FatVolume::Status::Ok);
        uint8_t s[SECTOR];
        for (const FatVolume::Extent &x : ext)
        {
            CHECK_EQ(x.first, uint64_t{out.size() / SECTOR}); // in order, no gaps
            for (uint64_t n = 0; n < x.count; ++n)
            {
                CHECK(media.read(x.card + n, s));
                out.insert(out.end(), s, s + SECTOR);
            }
        }
        return out;
    }

    size_t differ(const std::vector<uint8_t> &bytes, unsigned seed)
    {
        size_t n = 0;
        for (size_t i = 0; i < bytes.size(); ++i)
            n += bytes[i] != content(seed, i);
        return n;
    }

    std::vector<std::string> names(const std::vector<FatVolume::Entry> &entries)
    {
        std::vector<std::string> out;
        for (const auto &e : entries)
            out.push_back(e.name);
        return out;
    }

    std::string joined(const std::vector<std::string> &v)
    {
        std::string s;
        for (const auto &x : v)
            s += (s.empty() ? "" : "|") + x;
        return s;
    }

    /// The card with an image in, started up, talked to through its registers.
    struct Rig
    {
        explicit Rig(const std::string &image) : media(card_image(image))
        {
            card.insert(media, false);
            card.tick(INIT);
        }
        uint8_t reg(uint8_t r) { return card.read(r); }
        void set(uint8_t r, uint8_t v) { card.write(r, v); }
        unsigned count() { return unsigned{reg(SdCard::COUNT)} | unsigned{reg(SdCard::COUNT + 1)} << 8; }
        uint8_t status() { return reg(SdCard::STATUS); }
        /// A command taking one sector: `param` into DATA, then the firmware's time.
        void command_with(uint8_t cmd, const std::vector<uint8_t> &param, uint8_t unit = 0)
        {
            set(SdCard::UNIT, unit);
            set(SdCard::CMD, cmd);
            for (size_t i = 0; i < SECTOR; ++i)
                set(SdCard::DATA, i < param.size() ? param[i] : 0);
            card.tick(ACCESS);
        }
        /// MOUNT `path` as `unit`. @return ERROR (0: mounted)
        uint8_t mount(unsigned unit, const std::string &path, uint8_t flags = 0)
        {
            std::vector<uint8_t> p = {static_cast<uint8_t>(unit), flags};
            p.insert(p.end(), path.begin(), path.end());
            command_with(SdCard::MOUNT, p);
            CHECK(!(status() & SdCard::BUSY));
            return reg(SdCard::ERROR);
        }
        void transfer(uint8_t cmd, unsigned unit, uint32_t lba, uint16_t count)
        {
            set(SdCard::UNIT, static_cast<uint8_t>(unit));
            for (unsigned i = 0; i < 4; ++i)
                set(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            set(SdCard::COUNT, static_cast<uint8_t>(count));
            set(SdCard::COUNT + 1, static_cast<uint8_t>(count >> 8));
            set(SdCard::CMD, cmd);
        }
        /// READ through DATA, waiting as needed.
        std::vector<uint8_t> read(unsigned unit, uint32_t lba, uint16_t count)
        {
            transfer(SdCard::READ, unit, lba, count);
            std::vector<uint8_t> out;
            for (uint16_t n = 0; n < count; ++n)
            {
                card.tick(n ? SECTOR_TIME : ACCESS);
                if (!(status() & SdCard::DATA_READY))
                    break;
                for (size_t i = 0; i < SECTOR; ++i)
                    out.push_back(reg(SdCard::DATA));
            }
            return out;
        }

        std::shared_ptr<MemoryMedia> media;
        SdCard card;
    };
} // namespace

TEST(fat12_on_the_whole_card)
{
    auto m = card_image("fat12.sparse");
    std::string error;
    auto v = FatVolume::open(*m, error);
    if (!v)
        std::printf("  %s\n", error.c_str());
    REQUIRE(v != nullptr);
    CHECK_EQ(v->type(), 12u);
    CHECK_EQ(v->first_sector(), uint64_t{0});
    CHECK_EQ(v->label(), std::string("OO1FAT12"));
    std::vector<FatVolume::Entry> root;
    REQUIRE(v->list("/", root) == FatVolume::Status::Ok);
    CHECK_EQ(joined(names(root)), std::string("HELLO.TXT|A long file name.dsk|DISKS|" + LONGEST + "|BOOT.BIN"));
    CHECK_EQ(root[0].size, 21u);
    CHECK(root[2].attributes & FatVolume::DIRECTORY);
    CHECK_EQ(root[1].short_name.substr(0, 6), std::string("ALONGF"));
    std::vector<FatVolume::Entry> disks;
    REQUIRE(v->list("DISKS", disks) == FatVolume::Status::Ok); // no leading slash is the root too
    CHECK_EQ(joined(names(disks)), std::string("GAME.PO|README"));
    CHECK(disks[1].attributes & FatVolume::READ_ONLY);
    CHECK(!(disks[0].attributes & FatVolume::READ_ONLY));

    FatVolume::Entry e;
    REQUIRE(v->find("/disks/game.po", e) == FatVolume::Status::Ok); // any case
    CHECK_EQ(e.size, 16384u);
    CHECK_EQ(differ(file_bytes(*m, *v, e), 2), size_t{0});
    REQUIRE(v->find("/A LONG FILE NAME.DSK", e) == FatVolume::Status::Ok);
    const std::string alias = e.short_name;
    REQUIRE(v->find("/" + alias, e) == FatVolume::Status::Ok); // the 8.3 alias finds it too
    CHECK_EQ(e.name, std::string("A long file name.dsk"));
    std::vector<uint8_t> bytes = file_bytes(*m, *v, e);
    CHECK_EQ(bytes.size(), size_t{40 * 512}); // 20,000 bytes: 39 sectors and part of a 40th
    bytes.resize(20000);
    CHECK_EQ(differ(bytes, 1), size_t{0});
    REQUIRE(v->find("/HELLO.TXT", e) == FatVolume::Status::Ok);
    std::vector<FatVolume::Extent> one;
    CHECK(v->extents(e, one) == FatVolume::Status::Ok);
    CHECK_EQ(one.size(), size_t{1}); // under a sector: still one
    CHECK_EQ(one.empty() ? 0 : one[0].count, uint64_t{1});

    CHECK(v->find("/NOPE", e) == FatVolume::Status::NotFound);
    CHECK(v->find("/HELLO.TXT/X", e) == FatVolume::Status::NotFound);
    CHECK(v->list("/HELLO.TXT", root) == FatVolume::Status::NotFound);
    REQUIRE(v->find("", e) == FatVolume::Status::Ok);
    CHECK(e.attributes & FatVolume::DIRECTORY);
}

TEST(fat16_in_a_partition_with_a_fragmented_file)
{
    auto m = card_image("fat16.sparse");
    std::string error;
    auto v = FatVolume::open(*m, error);
    REQUIRE(v != nullptr);
    CHECK_EQ(v->type(), 16u);
    CHECK_EQ(v->first_sector(), uint64_t{2048});
    CHECK_EQ(v->label(), std::string("OO1FAT16"));
    std::vector<FatVolume::Entry> root;
    REQUIRE(v->list("/", root) == FatVolume::Status::Ok);
    CHECK_EQ(joined(names(root)), std::string("FRAG.BIN|B.BIN|ODD.BIN")); // A.BIN's slot reused
    FatVolume::Entry e;
    REQUIRE(v->find("/FRAG.BIN", e) == FatVolume::Status::Ok);
    std::vector<FatVolume::Extent> ext;
    REQUIRE(v->extents(e, ext) == FatVolume::Status::Ok);
    CHECK_EQ(ext.size(), size_t{2}); // A's 10 KB hole, then after B
    CHECK_EQ(ext[0].count, uint64_t{20});
    CHECK_EQ(ext[1].first, uint64_t{20});
    CHECK_EQ(ext[1].card, ext[0].card + 40); // B's 20 sectors between
    CHECK_EQ(differ(file_bytes(*m, *v, e), 4), size_t{0});
    REQUIRE(v->find("/odd.bin", e) == FatVolume::Status::Ok);
    std::vector<uint8_t> odd = file_bytes(*m, *v, e);
    CHECK_EQ(odd.size(), size_t{1024}); // 1,000 bytes: a sector and most of another
    odd.resize(1000);
    CHECK_EQ(differ(odd, 5), size_t{0});
}

TEST(fat32_in_a_partition_with_directories_over_many_clusters)
{
    auto m = card_image("fat32.sparse");
    std::string error;
    auto v = FatVolume::open(*m, error);
    REQUIRE(v != nullptr);
    CHECK_EQ(v->type(), 32u);
    CHECK_EQ(v->first_sector(), uint64_t{2048});
    CHECK_EQ(v->label(), std::string("OO1FAT32"));
    FatVolume::Entry e;
    REQUIRE(v->find("/Images/Apple/dos33.dsk", e) == FatVolume::Status::Ok);
    CHECK_EQ(differ(file_bytes(*m, *v, e), 6), size_t{0});
    std::vector<FatVolume::Entry> many;
    REQUIRE(v->list("/MANY", many) == FatVolume::Status::Ok);
    REQUIRE(many.size() == size_t{40}); // a long name takes 4 entries: over 100, in 512-byte clusters
    for (unsigned i = 1; i <= 40; ++i)
    {
        char want[64];
        if (i % 2)
            std::snprintf(want, sizeof want, "FILE%02u.TXT", i);
        else
            std::snprintf(want, sizeof want, "Entry number %02u with a long name.txt", i);
        CHECK_EQ(many[i - 1].name, std::string(want));
        CHECK_EQ(many[i - 1].size, 9u);
    }
}

TEST(no_fat_volume_is_found_on_a_blank_or_foreign_card)
{
    std::string error;
    MemoryMedia blank(std::vector<uint8_t>(64 * SECTOR), "BLANK");
    CHECK(FatVolume::open(blank, error) == nullptr);
    CHECK_EQ(error, std::string("no FAT volume or partition table"));
    std::vector<uint8_t> mbr(64 * SECTOR);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    mbr[446 + 4] = 0x83; // a Linux partition
    MemoryMedia linux_card(mbr, "LINUX");
    CHECK(FatVolume::open(linux_card, error) == nullptr);
    CHECK_EQ(error, std::string("no FAT partition"));
    mbr[446 + 4] = 0x0C;
    mbr[446 + 8] = 8; // FAT32 at 8, but nothing there
    MemoryMedia empty_partition(mbr, "EMPTY");
    CHECK(FatVolume::open(empty_partition, error) == nullptr);
    CHECK_EQ(error, std::string("the partition holds no FAT volume"));
}

TEST(a_damaged_chain_is_damaged_not_a_hang)
{
    auto m = card_image("fat12.sparse");
    std::string error;
    auto v = FatVolume::open(*m, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry e;
    REQUIRE(v->find("/DISKS/GAME.PO", e) == FatVolume::Status::Ok);
    /// Set a FAT12 entry in the first FAT (sector 1: one reserved sector).
    auto set12 = [](std::vector<uint8_t> &bytes, uint32_t cluster, unsigned value) {
        const size_t at = SECTOR + cluster + cluster / 2;
        if (cluster & 1)
        {
            bytes[at] = static_cast<uint8_t>((bytes[at] & 0x0F) | (value & 0x0F) << 4);
            bytes[at + 1] = static_cast<uint8_t>(value >> 4);
        }
        else
        {
            bytes[at] = static_cast<uint8_t>(value);
            bytes[at + 1] = static_cast<uint8_t>((bytes[at + 1] & 0xF0) | (value >> 8 & 0x0F));
        }
    };
    std::vector<FatVolume::Extent> ext;
    for (unsigned next : {e.cluster, 0xFFFu, 0u}) // round and round; ends after one cluster; a free one
    {
        std::vector<uint8_t> bytes = m->bytes();
        set12(bytes, e.cluster, next);
        MemoryMedia broken(bytes, "BROKEN");
        auto bv = FatVolume::open(broken, error);
        REQUIRE(bv != nullptr);
        CHECK(bv->extents(e, ext) == FatVolume::Status::Damaged);
    }
    CHECK(v->extents(e, ext) == FatVolume::Status::Ok); // and as it was, it is fine

    // A directory's chain round in a loop: DISKS is one cluster, made to point at itself.
    FatVolume::Entry disks;
    REQUIRE(v->find("/DISKS", disks) == FatVolume::Status::Ok);
    std::vector<uint8_t> bytes = m->bytes();
    set12(bytes, disks.cluster, disks.cluster);
    MemoryMedia looped(bytes, "LOOPED");
    auto lv = FatVolume::open(looped, error);
    REQUIRE(lv != nullptr);
    std::vector<FatVolume::Entry> list;
    CHECK(lv->list("/DISKS", list) == FatVolume::Status::Damaged);
    CHECK(lv->find("/DISKS/GAME.PO", e) == FatVolume::Status::Damaged);
}

TEST(mount_makes_a_file_a_unit)
{
    Rig r("fat12.sparse");
    CHECK_EQ(r.mount(2, "/DISKS/GAME.PO"), 0);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    CHECK_EQ(r.count(), 0u); // the parameter sector, counted down
    CHECK(r.card.unit(2).mounted);
    CHECK_EQ(r.card.unit(2).sectors, uint64_t{32});
    const std::vector<uint8_t> got = r.read(2, 3, 2);
    REQUIRE(got.size() == 2 * SECTOR);
    size_t bad = 0;
    for (size_t i = 0; i < got.size(); ++i)
        bad += got[i] != content(2, 3 * SECTOR + i);
    CHECK_EQ(bad, size_t{0});
    // IDENTIFY: a FAT volume, and the unit in the table.
    r.set(SdCard::CMD, SdCard::IDENTIFY);
    r.card.tick(ACCESS);
    std::vector<uint8_t> id;
    for (size_t i = 0; i < SECTOR; ++i)
        id.push_back(r.reg(SdCard::DATA));
    CHECK_EQ(id[5], SdCard::ID_FAT);
    CHECK_EQ(id[7], SdCard::VERSION_MINOR);
    const uint8_t *u = &id[0x40 + 32];
    CHECK_EQ(u[0] | u[1] << 8, 32);
    CHECK_EQ(u[4], SdCard::UNIT_MOUNTED);
    CHECK_EQ(std::string(reinterpret_cast<const char *>(u + 8)), std::string("GAME.PO"));
    CHECK_EQ(id[0x40 + 4], 0); // unit 1: nothing
}

TEST(mount_takes_its_sector_then_access_us)
{
    Rig r("fat12.sparse");
    r.set(SdCard::CMD, SdCard::MOUNT);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY | SdCard::BUSY | SdCard::DATA_READY);
    CHECK_EQ(r.count(), 1u);
    const std::string p = std::string("\x01\x00", 2) + "HELLO.TXT";
    for (size_t i = 0; i < SECTOR; ++i)
        r.set(SdCard::DATA, i < p.size() ? static_cast<uint8_t>(p[i]) : 0);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY | SdCard::BUSY);
    r.card.tick(ACCESS - 1);
    CHECK(r.status() & SdCard::BUSY);
    r.card.tick(1);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY);
    CHECK(r.card.unit(1).mounted);
    CHECK_EQ(r.card.unit(1).sectors, uint64_t{1}); // 21 bytes: part of one sector, which counts
}

TEST(mount_errors)
{
    Rig r("fat12.sparse");
    CHECK_EQ(r.mount(0, "/HELLO.TXT"), SdCard::ERR_UNIT);
    CHECK_EQ(r.mount(8, "/HELLO.TXT"), SdCard::ERR_UNIT);
    CHECK_EQ(r.mount(1, "/NOPE.PO"), SdCard::ERR_NOT_FOUND);
    CHECK_EQ(r.mount(1, "/DISKS"), SdCard::ERR_NOT_FOUND); // a directory
    CHECK_EQ(r.mount(1, "/HELLO.TXT/GAME.PO"), SdCard::ERR_NOT_FOUND);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::ERR);
    std::vector<uint8_t> unterminated(SECTOR, 'A');
    unterminated[0] = 1;
    unterminated[1] = 0;
    r.command_with(SdCard::MOUNT, unterminated);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NOT_FOUND);
    CHECK(!r.card.unit(1).mounted);
    Rig blank("fat12.sparse");
    blank.transfer(SdCard::WRITE, 0, 0, 1);
    for (size_t i = 0; i < SECTOR; ++i)
        blank.set(SdCard::DATA, 0); // boot sector gone
    blank.card.tick(SECTOR_TIME);
    CHECK_EQ(blank.mount(1, "/HELLO.TXT"), SdCard::ERR_NO_FAT);
}

TEST(a_unit_has_its_own_range_and_read_only_rule)
{
    Rig r("fat12.sparse");
    REQUIRE(r.mount(1, "/DISKS/GAME.PO") == 0);
    r.transfer(SdCard::READ, 1, 31, 2);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_RANGE);
    r.transfer(SdCard::READ, 1, 31, 1);
    CHECK(r.status() & SdCard::BUSY);
    r.card.tick(ACCESS);
    for (size_t i = 0; i < SECTOR; ++i)
        r.reg(SdCard::DATA);
    REQUIRE(r.mount(3, "/DISKS/README") == 0); // read-only attribute
    CHECK(r.card.unit(3).read_only);
    r.transfer(SdCard::WRITE, 3, 0, 1);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_WP);
    REQUIRE(r.mount(4, "/DISKS/GAME.PO", SdCard::MOUNT_READ_ONLY) == 0);
    r.transfer(SdCard::WRITE, 4, 0, 1);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_WP);
    r.transfer(SdCard::WRITE, 1, 0, 1); // the same file, mounted writable
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY | SdCard::BUSY | SdCard::DATA_READY);
    r.media->set_write_protected(true);
    REQUIRE(r.mount(5, "/DISKS/GAME.PO") == 0); // reading a protected card is fine
}

TEST(writes_to_a_unit_land_in_the_files_clusters_across_a_fragment)
{
    Rig r("fat16.sparse");
    REQUIRE(r.mount(1, "/FRAG.BIN") == 0);
    const std::vector<uint8_t> before = r.media->bytes();
    r.transfer(SdCard::WRITE, 1, 18, 4); // sectors 18-21: the first run ends at 20
    for (size_t i = 0; i < 4 * SECTOR; ++i)
    {
        if (!(r.status() & SdCard::DATA_READY))
            r.card.tick(SECTOR_TIME);
        r.set(SdCard::DATA, 0xEE);
    }
    r.card.tick(4 * SECTOR_TIME);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY);
    std::string error;
    auto v = FatVolume::open(*r.media, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry e;
    REQUIRE(v->find("/FRAG.BIN", e) == FatVolume::Status::Ok);
    const std::vector<uint8_t> file = file_bytes(*r.media, *v, e);
    size_t bad = 0;
    for (size_t i = 0; i < file.size(); ++i)
        bad += file[i] != (i >= 18 * SECTOR && i < 22 * SECTOR ? 0xEE : content(4, i));
    CHECK_EQ(bad, size_t{0});
    // Nothing else on the card changed: 4 sectors' worth of bytes, all in the file.
    size_t changed = 0;
    for (size_t i = 0; i < before.size(); ++i)
        changed += before[i] != r.media->bytes()[i];
    CHECK(changed <= 4 * SECTOR);
    REQUIRE(v->find("/B.BIN", e) == FatVolume::Status::Ok); // the file between the runs
    CHECK_EQ(differ(file_bytes(*r.media, *v, e), 9), size_t{0});
}

TEST(unmount_and_taking_the_card_out_detach_units_and_reset_keeps_them)
{
    Rig r("fat12.sparse");
    REQUIRE(r.mount(1, "/DISKS/GAME.PO") == 0);
    REQUIRE(r.mount(2, "/A long file name.dsk") == 0);
    r.card.bus_reset();
    CHECK(r.card.unit(1).mounted); // a boot menu can mount, then reset into it
    r.set(SdCard::UNIT, 1);
    r.set(SdCard::CMD, SdCard::UNMOUNT);
    CHECK(r.status() & SdCard::BUSY);
    r.card.tick(ACCESS);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY);
    CHECK(!r.card.unit(1).mounted);
    CHECK(r.card.unit(2).mounted);
    r.transfer(SdCard::READ, 1, 0, 1);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_UNIT);
    r.card.eject();
    CHECK(!r.card.unit(2).mounted);
    r.card.insert(r.media);
    r.card.tick(INIT);
    r.transfer(SdCard::READ, 2, 0, 1);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_UNIT);
}

TEST(list_returns_a_directory_in_64_byte_entries)
{
    Rig r("fat32.sparse");
    std::string path = "/MANY";
    r.command_with(SdCard::LIST, std::vector<uint8_t>(path.begin(), path.end()));
    // 40 entries, 8 a sector, and the zero entry after them: 6 sectors.
    CHECK_EQ(r.count(), 6u);
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY | SdCard::BUSY | SdCard::DATA_READY);
    std::vector<uint8_t> list;
    for (int n = 0; n < 6; ++n)
    {
        r.card.tick(SECTOR_TIME);
        REQUIRE(r.status() & SdCard::DATA_READY);
        for (size_t i = 0; i < SECTOR; ++i)
            list.push_back(r.reg(SdCard::DATA));
    }
    CHECK_EQ(r.status(), SdCard::PRESENT | SdCard::READY);
    CHECK_EQ(r.count(), 0u);
    CHECK_EQ(r.reg(SdCard::LBA), 0); // the listing isn't the card's sectors
    const uint8_t *e = &list[1 * 64];
    CHECK_EQ(e[0], 9);
    CHECK_EQ(e[4], 0x20); // archive
    CHECK_EQ(std::string(reinterpret_cast<const char *>(e + 8)), std::string("Entry number 02 with a long name.txt"));
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&list[38 * 64 + 8])), std::string("FILE39.TXT"));
    size_t tail = 0;
    for (size_t i = 40 * 64; i < list.size(); ++i)
        tail += list[i] != 0;
    CHECK_EQ(tail, size_t{0});

    path = "/";
    r.command_with(SdCard::LIST, std::vector<uint8_t>(path.begin(), path.end()));
    CHECK_EQ(r.count(), 1u);
    std::vector<uint8_t> root;
    for (size_t i = 0; i < SECTOR; ++i)
        root.push_back(r.reg(SdCard::DATA));
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&root[8])), std::string("IMAGES"));
    CHECK_EQ(root[4] & FatVolume::DIRECTORY, FatVolume::DIRECTORY);
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&root[64 + 8])), std::string("MANY"));
    CHECK_EQ(root[128 + 8], 0);

    path = "/IMAGES/APPLE/DOS33.DSK";
    r.command_with(SdCard::LIST, std::vector<uint8_t>(path.begin(), path.end()));
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NOT_FOUND);
    path = "/NOWHERE";
    r.command_with(SdCard::LIST, std::vector<uint8_t>(path.begin(), path.end()));
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NOT_FOUND);
}

TEST(a_long_name_is_cut_at_56_bytes_in_a_listing)
{
    Rig r("fat12.sparse");
    r.command_with(SdCard::LIST, {'/', 0});
    std::vector<uint8_t> root;
    for (size_t i = 0; i < SECTOR; ++i)
        root.push_back(r.reg(SdCard::DATA));
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&root[64 + 8])), std::string("A long file name.dsk"));
    CHECK_EQ(root[64 + 0] | root[64 + 1] << 8, 20000);
    const char *cut = reinterpret_cast<const char *>(&root[3 * 64 + 8]);
    CHECK_EQ(std::string(cut, SdCard::LIST_NAME), LONGEST.substr(0, SdCard::LIST_NAME)); // no NUL: it fills the field
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&root[4 * 64 + 8])), std::string("BOOT.BIN"));
    CHECK_EQ(root[5 * 64 + 8], 0);
}

namespace
{
    uint16_t get16(const std::vector<uint8_t> &b, size_t at) { return static_cast<uint16_t>(b[at] | b[at + 1] << 8); }
    uint32_t get32(const std::vector<uint8_t> &b, size_t at) { return get16(b, at) | uint32_t{get16(b, at + 2)} << 16; }
    void put32(std::vector<uint8_t> &b, size_t at, uint32_t v)
    {
        for (unsigned i = 0; i < 4; ++i)
            b[at + i] = static_cast<uint8_t>(v >> (8 * i));
    }

    /// The byte offset of the root directory entry whose 8.3 name is `name83` (11 bytes, padded), FAT12.
    size_t fat12_root_entry(const std::vector<uint8_t> &b, const char *name83)
    {
        const size_t root = (1 + 2 * size_t{get16(b, 22)}) * SECTOR; // reserved 1, two FATs
        for (size_t at = root; at < root + 224 * 32; at += 32)
            if (std::memcmp(&b[at], name83, 11) == 0)
                return at;
        return 0;
    }

    FatVolume::Entry entry_named(SdMedia &m, const std::string &dir, const std::string &name, bool &found)
    {
        std::string error;
        found = false;
        auto v = FatVolume::open(m, error);
        std::vector<FatVolume::Entry> list;
        if (!v || v->list(dir, list) != FatVolume::Status::Ok)
            return {};
        for (const auto &e : list)
            if (e.name == name)
            {
                found = true;
                return e;
            }
        return {};
    }
} // namespace

TEST(short_names_take_the_case_byte_and_05_and_volume_details)
{
    std::vector<uint8_t> b = card_image("fat12.sparse")->bytes();
    const size_t hello = fat12_root_entry(b, "HELLO   TXT");
    REQUIRE(hello != 0);
    b[hello + 12] = 0x18; // Windows NT's lower-case base and extension
    MemoryMedia lower(b, "LOWER");
    bool found = false;
    FatVolume::Entry e = entry_named(lower, "/", "hello.txt", found);
    CHECK(found);
    CHECK_EQ(e.short_name, std::string("HELLO.TXT"));
    b[hello + 12] = 0x08; // the base only
    MemoryMedia base(b, "BASE");
    entry_named(base, "/", "hello.TXT", found);
    CHECK(found);
    b[hello + 12] = 0;
    b[hello] = 0x05; // a name that really starts with $E5
    MemoryMedia e5(b, "E5");
    e = entry_named(e5, "/", "\xE5" "ELLO.TXT", found);
    CHECK(found);
    b[hello] = 'H';
    std::memcpy(&b[43], "NO NAME    ", 11); // the BPB's label: none
    MemoryMedia unlabelled(b, "UNLABELLED");
    std::string error;
    auto v = FatVolume::open(unlabelled, error);
    REQUIRE(v != nullptr);
    CHECK_EQ(v->label(), std::string(""));
    MemoryMedia cut(std::vector<uint8_t>(b.begin(), b.begin() + 1000 * SECTOR), "CUT"); // 2,880 sectors in the BPB
    CHECK(FatVolume::open(cut, error) == nullptr);
    CHECK_EQ(error, std::string("a volume bigger than the card"));
}

TEST(long_names_need_their_checksums_and_keep_ascii)
{
    std::vector<uint8_t> b = card_image("fat12.sparse")->bytes();
    const size_t root = (1 + 2 * size_t{get16(b, 22)}) * SECTOR;
    // "A long file name.dsk": two long-name entries ($42, then $01), then its 8.3 one.
    size_t first = 0;
    for (size_t at = root; at < root + 224 * 32; at += 32)
        if (b[at] == 0x42 && b[at + 11] == 0x0F && b[at + 1] == 'a') // "ame.dsk" is part 2
            first = at;
    REQUIRE(first != 0);
    CHECK_EQ(b[first + 32], 0x01);
    std::vector<uint8_t> accent = b;
    accent[first + 32 + 1] = 0xE9; // part 1's 'A' as U+00E9
    MemoryMedia accented(accent, "ACCENT");
    bool found = false;
    entry_named(accented, "/", "? long file name.dsk", found);
    CHECK(found);
    std::vector<uint8_t> bad = b;
    bad[first + 32 + 13] ^= 0x01; // part 1's checksum
    MemoryMedia broken(bad, "BROKEN");
    entry_named(broken, "/", "A long file name.dsk", found);
    CHECK(!found); // the long name is dropped: the 8.3 alias stands
    std::string error;
    auto v = FatVolume::open(broken, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry e;
    REQUIRE(v->find("/" + std::string("ALONGF~1.DSK"), e) == FatVolume::Status::Ok);
    CHECK_EQ(e.name, std::string("ALONGF~1.DSK"));
}

TEST(a_path_through_an_empty_file_is_not_the_root)
{
    std::vector<uint8_t> b = card_image("fat12.sparse")->bytes();
    const size_t hello = fat12_root_entry(b, "HELLO   TXT");
    REQUIRE(hello != 0);
    b[hello + 26] = b[hello + 27] = 0; // no cluster
    put32(b, hello + 28, 0);           // no bytes
    MemoryMedia empty(b, "EMPTY");
    std::string error;
    auto v = FatVolume::open(empty, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry e;
    CHECK(v->find("/HELLO.TXT/DISKS", e) == FatVolume::Status::NotFound);
    std::vector<FatVolume::Extent> ext;
    REQUIRE(v->find("/HELLO.TXT", e) == FatVolume::Status::Ok);
    CHECK(v->extents(e, ext) == FatVolume::Status::Ok);
    CHECK(ext.empty());
}

TEST(fat32_clusters_past_65535_and_the_reserved_top_bits)
{
    // Move DOS33.DSK's 32 clusters up by 65,536, so its entry's high word is 1, and set
    // the top four bits of every FAT entry in its new chain, which FAT32 says to ignore.
    std::vector<uint8_t> b = card_image("fat32.sparse")->bytes();
    const size_t vol = 2048 * SECTOR;
    const size_t fat = vol + size_t{get16(b, vol + 14)} * SECTOR;
    const size_t data = fat + 2 * size_t{get32(b, vol + 36)} * SECTOR;
    MemoryMedia before(b, "BEFORE");
    std::string error;
    auto v = FatVolume::open(before, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry e;
    REQUIRE(v->find("/IMAGES/APPLE/DOS33.DSK", e) == FatVolume::Status::Ok);
    REQUIRE(e.cluster < 0x10000);
    const uint32_t from = e.cluster, to = e.cluster + 0x10000;
    for (uint32_t i = 0; i < 32; ++i)
    {
        std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(data + (from + i - 2) * SECTOR), SECTOR,
                    b.begin() + static_cast<std::ptrdiff_t>(data + (to + i - 2) * SECTOR));
        put32(b, fat + (to + i) * 4, (i == 31 ? 0x0FFFFFFFu : to + i + 1) | 0xF0000000u);
    }
    // The directory entry: find it by its first cluster and size in APPLE's one cluster.
    size_t at = 0;
    for (size_t i = data; i + 32 <= b.size() && !at; i += 32)
        if (get16(b, i + 26) == from && get32(b, i + 28) == 16384 && b[i + 11] == 0x20)
            at = i;
    REQUIRE(at != 0);
    b[at + 20] = 1; // high word
    MemoryMedia moved(b, "MOVED");
    auto mv = FatVolume::open(moved, error);
    REQUIRE(mv != nullptr);
    REQUIRE(mv->find("/IMAGES/APPLE/DOS33.DSK", e) == FatVolume::Status::Ok);
    CHECK_EQ(e.cluster, to);
    CHECK_EQ(differ(file_bytes(moved, *mv, e), 6), size_t{0});
}

TEST(an_mbr_with_fat_in_its_boot_code_is_still_an_mbr)
{
    std::vector<uint8_t> b = card_image("fat16.sparse")->bytes();
    REQUIRE(b[0] != 0xEB && b[0] != 0xE9 && b[0] != 0xE8);
    std::memcpy(&b[54], "FAT", 3); // boot code that happens to hold the letters
    MemoryMedia m(b, "MBR");
    std::string error;
    auto v = FatVolume::open(m, error);
    REQUIRE(v != nullptr);
    CHECK_EQ(v->first_sector(), uint64_t{2048});
}

TEST(the_card_unmounts_on_a_new_card_and_reports_read_only_units)
{
    Rig r("fat12.sparse");
    REQUIRE(r.mount(1, "/DISKS/GAME.PO", SdCard::MOUNT_READ_ONLY) == 0);
    r.set(SdCard::COUNT, 7);
    r.set(SdCard::CMD, SdCard::IDENTIFY);
    r.card.tick(ACCESS);
    std::vector<uint8_t> id;
    for (size_t i = 0; i < SECTOR; ++i)
        id.push_back(r.reg(SdCard::DATA));
    CHECK_EQ(id[0x40 + 4], SdCard::UNIT_MOUNTED | SdCard::UNIT_READ_ONLY);
    CHECK_EQ(r.count(), 7u); // IDENTIFY leaves COUNT
    r.card.insert(card_image("fat16.sparse")); // a new card, without taking the old one out first
    CHECK(!r.card.unit(1).mounted);
}

TEST(a_damaged_directory_fails_mount_with_no_fat_and_list_checks_its_path)
{
    Rig r("fat12.sparse");
    std::vector<uint8_t> b = r.media->bytes();
    std::string error;
    auto v = FatVolume::open(*r.media, error);
    REQUIRE(v != nullptr);
    FatVolume::Entry disks;
    REQUIRE(v->find("/DISKS", disks) == FatVolume::Status::Ok);
    const size_t at = SECTOR + disks.cluster + disks.cluster / 2; // DISKS's FAT12 entry: point it at itself
    if (disks.cluster & 1)
    {
        b[at] = static_cast<uint8_t>((b[at] & 0x0F) | (disks.cluster & 0x0F) << 4);
        b[at + 1] = static_cast<uint8_t>(disks.cluster >> 4);
    }
    else
    {
        b[at] = static_cast<uint8_t>(disks.cluster);
        b[at + 1] = static_cast<uint8_t>((b[at + 1] & 0xF0) | (disks.cluster >> 8 & 0x0F));
    }
    SdCard &card = r.card;
    card.insert(std::make_shared<MemoryMedia>(b, "LOOPED"));
    card.tick(INIT);
    CHECK_EQ(r.mount(1, "/DISKS/GAME.PO"), SdCard::ERR_NO_FAT);
    std::vector<uint8_t> unterminated(SECTOR, 'A');
    r.command_with(SdCard::LIST, unterminated);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NOT_FOUND);
    r.command_with(SdCard::LIST, {'/', 'D', 'I', 'S', 'K', 'S', 0});
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NO_FAT);
}

int main()
{
    return run_all();
}
