/**
 * @file test_disk_image.cpp
 * @brief DiskImage: 140 KB sector images to tracks of bits and back, and WOZ files.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Everything here is made up in the test, so it runs everywhere. Real disks
 * booting on the real Disk II PROMs are test_iie's, with the owner's files.
 */

#include "harness.h"

#include "disk_image.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    std::vector<uint8_t> random_image(uint32_t seed)
    {
        std::vector<uint8_t> img(DiskImage::SECTOR_IMAGE);
        for (uint8_t &b : img)
        {
            seed = seed * 1103515245u + 12345u;
            b = static_cast<uint8_t>(seed >> 16);
        }
        return img;
    }

    /// The disk bytes along a track, as an ideal reader takes them, twice round.
    std::vector<uint8_t> nibbles(const DiskTrack &t)
    {
        std::vector<uint8_t> out;
        uint8_t reg = 0;
        for (uint32_t n = 0; n < 2 * t.bit_count; ++n)
        {
            reg = static_cast<uint8_t>(reg << 1 | t.bit(n % t.bit_count));
            if (reg & 0x80)
                out.push_back(reg), reg = 0;
        }
        return out;
    }

    /// A DOS 3.3 disk's VTOC and catalog chain (track 17, sectors 15 down to 1), placed as `physical` says.
    std::vector<uint8_t> dos_disk(const uint8_t *file_sector_of_physical)
    {
        static constexpr uint8_t DOS_PHYSICAL[16] = {0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};
        std::vector<uint8_t> img = random_image(7);
        const auto sector = [&](unsigned logical) {
            return img.data() + (17 * 16 + file_sector_of_physical[DOS_PHYSICAL[logical]]) * 256;
        };
        uint8_t *vtoc = sector(0);
        vtoc[1] = 17, vtoc[2] = 15, vtoc[3] = 3, vtoc[0x27] = 122, vtoc[0x34] = 35, vtoc[0x35] = 16;
        for (unsigned s = 15; s >= 1; --s)
        {
            sector(s)[1] = s > 1 ? 17 : 0;
            sector(s)[2] = static_cast<uint8_t>(s - 1);
        }
        return img;
    }

    constexpr uint8_t DOS_ORDER[16] = {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};
    constexpr uint8_t PRODOS_ORDER[16] = {0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15};

    /// A ProDOS volume directory's key block (2) and the next (3), placed as `physical` says.
    std::vector<uint8_t> prodos_disk(const uint8_t *file_sector_of_physical)
    {
        std::vector<uint8_t> img = random_image(9);
        const auto block = [&](unsigned b) { // its first half: physical sectors 0, 4, 8, 12, 1, 5, 9, 13
            static constexpr uint8_t FIRST[8] = {0, 4, 8, 12, 1, 5, 9, 13};
            return img.data() + file_sector_of_physical[FIRST[b % 8]] * 256;
        };
        uint8_t *dir = block(2);
        dir[0] = dir[1] = 0;
        dir[2] = 3, dir[3] = 0;
        dir[4] = 0xF4; // a volume directory header, a 4-letter name
        std::memcpy(dir + 5, "OO-1", 4);
        block(3)[0] = 2, block(3)[1] = 0;
        return img;
    }

    std::vector<uint8_t> bytes(const char *s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }

    /// A WOZ 1 file: track i `bits[i]` bits long, of 0x55 (0: an empty record), quarter tracks per `tmap`.
    std::vector<uint8_t> woz1(const std::vector<uint16_t> &bits, const std::vector<uint8_t> &tmap)
    {
        std::vector<uint8_t> f = bytes("WOZ1\xFF\x0A\x0D\x0A");
        f.resize(12, 0);
        const auto chunk = [&](const char *id, const std::vector<uint8_t> &data) {
            f.insert(f.end(), id, id + 4);
            for (int i = 0; i < 4; ++i)
                f.push_back(static_cast<uint8_t>(data.size() >> (8 * i)));
            f.insert(f.end(), data.begin(), data.end());
        };
        std::vector<uint8_t> info(60, 0);
        info[0] = 1, info[1] = 1;
        chunk("INFO", info);
        chunk("TMAP", tmap);
        std::vector<uint8_t> trks(bits.size() * 6656, 0x55);
        for (size_t i = 0; i < bits.size(); ++i)
            trks[i * 6656 + 6648] = static_cast<uint8_t>(bits[i]), trks[i * 6656 + 6649] = static_cast<uint8_t>(bits[i] >> 8);
        chunk("TRKS", trks);
        return f;
    }
    std::vector<uint8_t> tmap_of(std::initializer_list<std::pair<unsigned, uint8_t>> m)
    {
        std::vector<uint8_t> t(DiskImage::QUARTER_TRACKS, DiskImage::NO_TRACK);
        for (const auto &[q, i] : m)
            t[q] = i;
        return t;
    }
} // namespace

TEST(the_6_and_2_code_is_the_64_disk_bytes)
{
    const auto &c = DiskImage::code62();
    const uint8_t want[64] = {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE,
                              0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE,
                              0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD,
                              0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2,
                              0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
    size_t same = 0;
    for (size_t i = 0; i < 64; ++i)
        same += c[i] == want[i];
    CHECK_EQ(same, size_t{64});
}

TEST(sectors_become_a_dos_formatted_track_and_come_back)
{
    for (const DiskImage::Format order : {DiskImage::Format::Dos, DiskImage::Format::Prodos})
    {
        const std::vector<uint8_t> img = random_image(order == DiskImage::Format::Dos ? 1 : 2);
        const auto d = DiskImage::from_sectors(img, order);
        REQUIRE(d != nullptr);
        CHECK(d->format() == order);
        std::vector<uint8_t> back;
        std::string error;
        CHECK(d->to_sectors(order, back, error));
        CHECK(back == img);
        CHECK(!d->changed());
    }
}

TEST(a_track_holds_16_address_fields_in_physical_order)
{
    const auto d = DiskImage::from_sectors(random_image(3), DiskImage::Format::Dos);
    const DiskTrack *t = d->track(4 * 5);
    REQUIRE(t != nullptr);
    CHECK(t->bit_count <= 51'200);                  // fits a real track: 6,400 bytes of 4 us bits
    CHECK(t->bit_count > 50'000);
    CHECK(d->track(4 * 5 - 1) == t);                // the quarter tracks either side read it too, as WOZ maps
    CHECK(d->track(4 * 5 + 1) == t);
    CHECK(d->track(4 * 5 + 2) == nullptr);          // the half track between: nothing
    CHECK(d->track(4 * 35) == nullptr);
    const std::vector<uint8_t> n = nibbles(*t);
    std::vector<unsigned> sectors;
    for (size_t i = 0; i + 11 < n.size() / 2 + 20; ++i)
        if (n[i] == 0xD5 && n[i + 1] == 0xAA && n[i + 2] == 0x96)
        {
            const auto oe = [&](size_t k) { return ((n[k] << 1) | 1) & n[k + 1]; };
            CHECK_EQ(oe(i + 3), 254);                // volume
            CHECK_EQ(oe(i + 5), 5);                  // track
            sectors.push_back(static_cast<unsigned>(oe(i + 7)));
            CHECK_EQ(oe(i + 9), 254 ^ 5 ^ oe(i + 7)); // the checksum
        }
    REQUIRE(sectors.size() >= 16);
    for (unsigned p = 0; p < 16; ++p)
        CHECK_EQ(sectors[p], p);
}

TEST(a_data_field_is_written_as_dos_3_3_writes_it)
{
    // Byte 0 = $01, the rest 0: its low two bits, swapped, make the first of 86 two-bit values 2; the
    // rest are 0. Each is written XOR the one before, through the code: $9A, $9A, then $96s; the
    // checksum is the last, $96. (The owner's real disks booting on the real ROM, test_iie, are what
    // pin this down; this keeps it pinned everywhere.)
    std::vector<uint8_t> img(DiskImage::SECTOR_IMAGE, 0);
    img[0] = 0x01;
    const auto d = DiskImage::from_sectors(img, DiskImage::Format::Dos);
    const std::vector<uint8_t> n = nibbles(*d->track(0));
    size_t at = 0;
    while (at + 3 < n.size() && !(n[at] == 0xD5 && n[at + 1] == 0xAA && n[at + 2] == 0xAD))
        ++at;
    REQUIRE(at + 3 + 343 + 3 < n.size());
    const uint8_t *f = &n[at + 3];
    CHECK_EQ(f[0], 0x9A);
    CHECK_EQ(f[1], 0x9A);
    size_t rest = 0;
    for (size_t i = 2; i < 343; ++i)
        rest += f[i] == 0x96;
    CHECK_EQ(rest, size_t{341});
    CHECK_EQ(f[343], 0xDE);
    CHECK_EQ(f[344], 0xAA);
    CHECK_EQ(f[345], 0xEB);
    img[0] = 0x04;                        // a high bit: the 87th value (the first of the high six), 1
    const auto e = DiskImage::from_sectors(img, DiskImage::Format::Dos);
    const std::vector<uint8_t> m = nibbles(*e->track(0));
    CHECK_EQ(m[at + 3 + 85], 0x96);
    CHECK_EQ(m[at + 3 + 86], 0x97);       // 1 ^ 0
    CHECK_EQ(m[at + 3 + 87], 0x97);       // 0 ^ 1
}

TEST(sectors_land_where_their_order_puts_them)
{
    std::vector<uint8_t> img(DiskImage::SECTOR_IMAGE, 0);
    for (unsigned s = 0; s < 16; ++s)
        img[(3 * 16 + s) * 256] = static_cast<uint8_t>(0xA0 + s); // track 3: each sector says which it is
    const auto dos = DiskImage::from_sectors(img, DiskImage::Format::Dos);
    std::vector<uint8_t> as_po;
    std::string error;
    REQUIRE(dos->to_sectors(DiskImage::Format::Prodos, as_po, error));
    for (unsigned p = 0; p < 16; ++p) // physical p holds file sector DOS_ORDER[p]; as ProDOS order, it is PRODOS_ORDER[p]
        CHECK_EQ(as_po[(3 * 16 + PRODOS_ORDER[p]) * 256], 0xA0 + DOS_ORDER[p]);
}

TEST(a_sector_image_says_its_order_by_its_contents)
{
    std::string error;
    const auto a = DiskImage::load(dos_disk(DOS_ORDER), "game.po", error);   // a DOS catalog, DOS order, misnamed
    REQUIRE(a != nullptr);
    CHECK(a->format() == DiskImage::Format::Dos);
    const auto b = DiskImage::load(dos_disk(PRODOS_ORDER), "game.dsk", error); // a DOS disk, kept in ProDOS order
    REQUIRE(b != nullptr);
    CHECK(b->format() == DiskImage::Format::Prodos);
    const auto c = DiskImage::load(prodos_disk(PRODOS_ORDER), "vol.dsk", error);
    REQUIRE(c != nullptr);
    CHECK(c->format() == DiskImage::Format::Prodos);
    const auto d = DiskImage::load(prodos_disk(DOS_ORDER), "vol.po", error);
    REQUIRE(d != nullptr);
    CHECK(d->format() == DiskImage::Format::Dos);
    const auto e = DiskImage::load(random_image(5), "noise.PO", error);       // neither: the name decides
    REQUIRE(e != nullptr);
    CHECK(e->format() == DiskImage::Format::Prodos);
    const auto f = DiskImage::load(random_image(5), "noise.do", error);
    REQUIRE(f != nullptr);
    CHECK(f->format() == DiskImage::Format::Dos);
}

TEST(woz_saved_and_loaded_again_is_the_same_disk)
{
    const auto d = DiskImage::from_sectors(random_image(4), DiskImage::Format::Dos);
    d->set_write_protected(true);
    d->track_for_writing(4 * 3).set_bit(600, !d->track(4 * 3)->bit(600)); // in sector 0's data field
    d->track_for_writing(2).set_bit(5, true);                              // a half track that was empty
    const std::vector<uint8_t> woz = d->to_woz();
    CHECK(std::memcmp(woz.data(), "WOZ2\xFF\x0A\x0D\x0A", 8) == 0);
    std::string error;
    const auto e = DiskImage::load(woz, "x.woz", error);
    REQUIRE(e != nullptr);
    CHECK(e->format() == DiskImage::Format::Woz);
    CHECK(e->write_protected());
    for (unsigned q = 0; q < DiskImage::QUARTER_TRACKS; ++q)
    {
        const DiskTrack *a = d->track(q), *b = e->track(q);
        CHECK_EQ(a == nullptr, b == nullptr);
        if (a && b)
        {
            CHECK_EQ(a->bit_count, b->bit_count);
            CHECK(std::memcmp(a->bytes.data(), b->bytes.data(), (a->bit_count + 7) / 8) == 0);
        }
    }
    std::vector<uint8_t> sectors;
    CHECK(!e->to_sectors(DiskImage::Format::Dos, sectors, error)); // track 3 no longer reads as plain sectors
    CHECK(error.find("track 3, sector") == 0);
    CHECK(error.find("save it as .woz") != std::string::npos);
}

TEST(a_damaged_or_foreign_file_is_refused_with_a_reason)
{
    std::string error;
    CHECK(DiskImage::load(std::vector<uint8_t>(1000), "x.nib", error) == nullptr);
    CHECK(error == "x.nib: 1000 bytes is no disk image (want a .woz, or a 143,360-byte .dsk, .do or .po)");
    std::vector<uint8_t> woz = DiskImage::from_sectors(random_image(6), DiskImage::Format::Dos)->to_woz();
    woz[3000] ^= 1;
    CHECK(DiskImage::load(woz, "x.woz", error) == nullptr);
    CHECK(error == "WOZ file damaged: its CRC does not match");
    woz[3000] ^= 1;
    woz[8] = woz[9] = woz[10] = woz[11] = 0; // no CRC: allowed, and it loads
    CHECK(DiskImage::load(woz, "x.woz", error) != nullptr);
    std::vector<uint8_t> text = woz;
    text[6] = 0x0A; // CR LF made LF: sent as text
    CHECK(DiskImage::load(text, "x.woz", error) == nullptr);
    CHECK(error == "not a WOZ file: its header is damaged (sent as text?)");
    std::vector<uint8_t> big = woz;
    big[21] = 2; // INFO disk type: 3.5"
    CHECK(DiskImage::load(big, "x.woz", error) == nullptr);
    CHECK(error == "a 3.5\" WOZ disk: the Disk II takes 5.25\" disks");
}

TEST(a_woz_1_file_loads)
{
    // WOZ 1: INFO, TMAP, then TRKS as 6,656-byte records: the bits, then bytes used, bit count.
    std::vector<uint8_t> f = bytes("WOZ1\xFF\x0A\x0D\x0A");
    f.resize(12, 0);
    const auto chunk = [&](const char *id, const std::vector<uint8_t> &data) {
        f.insert(f.end(), id, id + 4);
        for (int i = 0; i < 4; ++i)
            f.push_back(static_cast<uint8_t>(data.size() >> (8 * i)));
        f.insert(f.end(), data.begin(), data.end());
    };
    std::vector<uint8_t> info(60, 0);
    info[0] = 1, info[1] = 1, info[2] = 0;
    chunk("INFO", info);
    std::vector<uint8_t> tmap(160, 0xFF);
    tmap[0] = tmap[1] = 0;
    chunk("TMAP", tmap);
    std::vector<uint8_t> trk(6656, 0);
    trk[0] = 0xD5, trk[1] = 0xAA, trk[2] = 0x96;
    trk[6648] = static_cast<uint8_t>(50'000 & 0xFF), trk[6649] = static_cast<uint8_t>(50'000 >> 8);
    chunk("TRKS", trk);
    std::string error;
    const auto d = DiskImage::load(f, "old.woz", error);
    REQUIRE(d != nullptr);
    REQUIRE(d->track(1) != nullptr);
    CHECK_EQ(d->track(1)->bit_count, 50'000u);
    CHECK_EQ(d->track(0)->bytes[2], 0x96);
    CHECK(d->track(2) == nullptr);
}

TEST(more_tracks_than_quarter_tracks_save_within_the_woz_header)
{
    // 200 records, three mapped: only those are the disk, and TRKS's 160 entries hold them (an overflow once).
    std::string error;
    const auto d = DiskImage::load(woz1(std::vector<uint16_t>(200, 50'000), tmap_of({{0, 0}, {4, 150}, {8, 199}})),
                                   "many.woz", error);
    REQUIRE(d != nullptr);
    const std::vector<uint8_t> woz = d->to_woz();
    const auto e = DiskImage::load(woz, "x.woz", error);
    REQUIRE(e != nullptr);
    for (const unsigned q : {0u, 4u, 8u})
    {
        REQUIRE(e->track(q) != nullptr);
        CHECK_EQ(e->track(q)->bit_count, 50'000u);
    }
    CHECK(e->track(1) == nullptr);
    CHECK_EQ(woz.size(), size_t{1536 + 3 * 13 * 512}); // the header, and the three tracks' blocks

    std::vector<uint16_t> empty(250, 0);                  // 249 empty records first: written past the header's end
    empty.back() = 50'000;
    const auto f = DiskImage::load(woz1(empty, tmap_of({{0, 249}})), "empty.woz", error);
    REQUIRE(f != nullptr);
    CHECK_EQ(f->to_woz().size(), size_t{1536 + 13 * 512});
}

TEST(a_track_written_where_there_was_none_is_its_own)
{
    // 300 records, one mapped: a new track's index once wrapped onto another's, or became NO_TRACK.
    std::string error;
    const auto d = DiskImage::load(woz1(std::vector<uint16_t>(300, 50'000), tmap_of({{0, 0}})), "many.woz",
                                   error);
    REQUIRE(d != nullptr);
    for (unsigned q = 1; q < DiskImage::QUARTER_TRACKS; ++q)
    {
        DiskTrack &t = d->track_for_writing(q);
        REQUIRE(d->track(q) == &t);
        t.set_bit(0, false);
    }
    CHECK_EQ(d->track(5)->bit_count, DiskImage::BLANK_TRACK_BITS);
    CHECK(d->track(5) != d->track(6));
    CHECK_EQ(d->track(0)->bit_count, 50'000u);
    CHECK(d->track(0)->bit(1)); // 0x55: untouched
}

TEST(an_empty_track_keeps_the_ones_after_it)
{
    // Records 0 and 2 mapped, 1 empty: saved and loaded, quarter track 4 must still be there.
    std::string error;
    const auto d = DiskImage::load(woz1({50'000, 0, 50'000}, tmap_of({{0, 0}, {4, 2}})), "gap.woz", error);
    REQUIRE(d != nullptr);
    REQUIRE(d->track(4) != nullptr);
    const auto e = DiskImage::load(d->to_woz(), "x.woz", error);
    REQUIRE(e != nullptr);
    REQUIRE(e->track(0) != nullptr);
    REQUIRE(e->track(4) != nullptr);
    CHECK_EQ(e->track(4)->bit_count, 50'000u);

    // A WOZ 2 with an empty entry between two tracks (as another tool may leave one) loads them both.
    std::vector<uint8_t> gap = e->to_woz();
    std::copy_n(gap.begin() + 264, 8, gap.begin() + 272); // TRKS entry 1 (quarter track 4's) moved to 2
    std::fill_n(gap.begin() + 264, 8, 0);
    gap[88 + 4] = 2;                                      // TMAP
    gap[8] = gap[9] = gap[10] = gap[11] = 0;              // no CRC
    const auto g = DiskImage::load(gap, "gap2.woz", error);
    REQUIRE(g != nullptr);
    REQUIRE(g->track(4) != nullptr);
    CHECK_EQ(g->track(4)->bit_count, 50'000u);
}

TEST(sectors_of_the_wrong_size_make_no_disk)
{
    CHECK(DiskImage::from_sectors(std::vector<uint8_t>(1000), DiskImage::Format::Dos) == nullptr);
    CHECK(DiskImage::from_sectors(std::vector<uint8_t>(DiskImage::SECTOR_IMAGE + 1), DiskImage::Format::Dos) == nullptr);
}

int main() { return run_all(); }
