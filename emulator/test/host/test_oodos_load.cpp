/**
 * @file test_oodos_load.cpp
 * @brief OOLIB's L4 (hw/oolib.md, "The plan"): function 83's DM_LOAD, a file's records loaded into
 * physical memory by DMA: whole sectors straight from the SD card, the ends and the apple-do drives
 * through the BDOS's buffers, M: by DMAGO.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * L4TEST (oodos/sys/test/l4test.s) as test_oodos_video runs it. The files are put on B: behind OODOS's
 * back (oodos_rig.h's put_b), on M: (put_m), or are the committed card's /DISKS/CPM.DSK's (its
 * PATTERN.BIN, make_card.sh's), mounted as C:.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"

#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t MBOX = 0x2100, KBLK = 0x2110, FCB = 0x2140; ///< l4test.s's
    constexpr uint8_t DM_LOAD = 1, F_OPEN_FN = 15, F_DMAOFF_FN = 26, F_WRITERAND_FN = 34, O_SOUND_FN = 82;

    uint32_t ms_count(Rig &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("MS");
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    std::vector<uint8_t> noise(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> b(n);
        for (auto &x : b)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            x = static_cast<uint8_t>(seed >> 8);
        }
        return b;
    }
    /// make_card.sh's binary files (test_oodos_apple's gen).
    std::vector<uint8_t> gen(size_t n, unsigned seed)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF);
        return b;
    }
    std::vector<uint8_t> mem(Rig &r, uint32_t at, size_t n)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = r.phys(at + static_cast<uint32_t>(i));
        return b;
    }
    std::vector<uint8_t> slice(const std::vector<uint8_t> &d, size_t from, size_t n)
    {
        std::vector<uint8_t> out(n, 0x1A); // a file's last record is ^Z-padded
        for (size_t i = 0; i < n && from + i < d.size(); ++i)
            out[i] = d[from + i];
        return out;
    }
    bool same(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const char *what)
    {
        if (a == b)
            return true;
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i])
            ++i;
        std::printf("  %s: first different at %zu of %zu\n", what, i, a.size());
        return false;
    }

    size_t open(CcpRig &r, const std::string &tail)
    {
        r.poke(MBOX, 0);
        const size_t from = start(r, "l4test.com", " " + tail);
        if (tail != "X")
            CHECK(r.until("OPEN ", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("OPEN ", from)));
        else
            r.run(ms(200));
        return from;
    }
    std::string command(CcpRig &r, char c, const std::string &label, const std::vector<uint8_t> &args = {},
                        uint64_t budget = ms(4000))
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX + 1, args);
        r.poke(MBOX, static_cast<uint8_t>(c));
        if (!r.until("\n" + label + " ", budget, from))
        {
            std::printf("  no %s line after command %c\n", label.c_str(), c);
            r.tail();
            return {};
        }
        const size_t at = r.out.find("\n" + label + " ", from);
        r.until("\r", ms(500), at + 1);
        return field(r.out.substr(from), label);
    }
    std::string gcall(CcpRig &r, uint8_t fn, unsigned ax) { return command(r, 'G', "G", {fn, static_cast<uint8_t>(ax), static_cast<uint8_t>(ax >> 8)}); }
    /// The FCB at FCB for drive `drive` (1 A:) and `name83`, opened.
    void open_file(CcpRig &r, unsigned drive, const std::string &name83)
    {
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = static_cast<uint8_t>(drive);
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        r.poke(FCB, fcb);
        CHECK_EQ(command(r, 'O', "O"), std::string("00 00"));
    }
    std::vector<uint8_t> load_block(unsigned first, unsigned count, uint32_t dst)
    {
        return {DM_LOAD,
                static_cast<uint8_t>(FCB),
                static_cast<uint8_t>(FCB >> 8),
                static_cast<uint8_t>(first),
                static_cast<uint8_t>(first >> 8),
                static_cast<uint8_t>(first >> 16),
                static_cast<uint8_t>(count),
                static_cast<uint8_t>(count >> 8),
                static_cast<uint8_t>(dst),
                static_cast<uint8_t>(dst >> 8),
                static_cast<uint8_t>(dst >> 16)};
    }
    /// DM_LOAD: "aa xx", and the cycles from the call's JSR to its return, and the records it says.
    struct Load
    {
        std::string ret;
        uint64_t cycles{0};
        unsigned records{0};
    };
    Load load(CcpRig &r, unsigned first, unsigned count, uint32_t dst)
    {
        static const std::map<std::string, uint16_t> lbl = labels(SYS + "/l4test.lbl");
        const uint16_t call = lbl.at("kcall"), ret = lbl.at("kret");
        Load l;
        uint64_t c0 = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == call && r.nb(0) == 0)
                c0 = r.cpu->cycles();
            else if (pc == ret && r.nb(0) == 0 && c0)
                l.cycles = r.cpu->cycles() - c0;
        };
        r.poke(KBLK, load_block(first, count, dst));
        l.ret = command(r, 'K', "K");
        r.watch = nullptr;
        l.records = r.phys(KBLK + 6) | r.phys(KBLK + 7) << 8;
        return l;
    }
} // namespace

TEST(sixty_four_kb_load_into_video_memory_byte_for_byte_and_faster_than_33)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(65536 + 4096, 21);
    put_b(r, "PICTURE BIN", data);
    open(r, "X");
    open_file(r, 2, "PICTURE BIN");
    const uint32_t m0 = ms_count(r);
    const uint64_t c0 = r.cpu->cycles();
    // Into banks 9 (blocks 36-39): 64 KB from $84000 would run through aperture slots 2 and 3's windows,
    // OODOS's card banks 2 and 3, which function 83 refuses (review F2); slot 1's window below.
    Load l = load(r, 0, 512, 0x90000);
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    const unsigned counted = ms_count(r) - m0;
    CHECK_EQ(l.ret, std::string("00 00"));
    CHECK_EQ(l.records, 512u);
    CHECK(same(mem(r, 0x90000, 65536), slice(data, 0, 65536), "64 KB"));
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    // Video memory: 16 KB into slot 1's window, block 33, the program's card bank 16 (the warm boot's); 16 KB
    // and a record is refused, nothing written.
    r.poke(FCB + 33, std::vector<uint8_t>{0, 0, 0});
    CHECK_EQ(load(r, 0, 128, 0x84000).ret, std::string("00 00"));
    CHECK(same(mem(r, 0x84000, 16384), slice(data, 0, 16384), "16 KB to the card"));
    const std::vector<uint8_t> bank2 = mem(r, 0x88000, 128);
    CHECK_EQ(load(r, 0, 129, 0x84000).ret, std::string("FF FF"));
    CHECK(mem(r, 0x88000, 128) == bank2);
    // The same 64 KB by 51 and 33 with a count of 128 (44), four calls, into blocks 40-43.
    static const std::map<std::string, uint16_t> lbl = labels(SYS + "/l4test.lbl");
    const uint16_t sc = lbl.at("scall"), sr = lbl.at("sret");
    uint64_t s0 = 0, scycles = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == sc && r.nb(0) == 0)
            s0 = r.cpu->cycles();
        else if (pc == sr && r.nb(0) == 0 && s0)
            scycles = r.cpu->cycles() - s0;
    };
    r.poke(FCB + 33, std::vector<uint8_t>{0, 0, 0});
    CHECK_EQ(command(r, 'S', "S", {128, 0x00, 0xA0, 4}), std::string("00 00")); // segment $A000: $A0000
    r.watch = nullptr;
    CHECK(same(mem(r, 0xA0000, 65536), slice(data, 0, 65536), "64 KB by 33"));
    std::printf("  64 KB: DM_LOAD %llu cycles (%.1f ms), 51 and 33 x 128 %llu cycles (%.1f ms); %u ms counted, %.1f in the cycles\n",
                static_cast<unsigned long long>(l.cycles), static_cast<double>(l.cycles) / HZ * 1000,
                static_cast<unsigned long long>(scycles), static_cast<double>(scycles) / HZ * 1000, counted, secs * 1000);
    CHECK(l.cycles > 0 && scycles > 4 * l.cycles);
    CHECK(l.cycles < static_cast<uint64_t>(ms(25)));
}

TEST(the_ends_go_through_the_buffer_and_the_file_s_end_stops_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(100 * 128, 5);
    put_b(r, "SHORT   BIN", data);
    open(r, "X");
    open_file(r, 2, "SHORT   BIN");
    // From record 3, 77 records: the first and last partial sectors through the buffer, the middle straight.
    // (Into aperture slot 1's window, the program's card bank 16; slots 2 and 3's are OODOS's: review F2.)
    Load l = load(r, 3, 77, 0x84010);
    CHECK_EQ(l.ret, std::string("00 00"));
    CHECK(same(mem(r, 0x84010, 77 * 128), slice(data, 3 * 128, 77 * 128), "records 3-79"));
    CHECK_EQ(r.phys(0x84010 + 77 * 128), 0); // nothing past
    // The FCB as a random read of the last record leaves it: R0-R2 79, CR 79.
    CHECK_EQ(r.phys(FCB + 33), 79);
    CHECK_EQ(r.phys(FCB + 32), 79);
    // Past the file's end: A = 1, the records it had.
    l = load(r, 90, 30, 0x90000);
    CHECK_EQ(l.ret, std::string("01 00"));
    CHECK_EQ(l.records, 10u);
    CHECK(same(mem(r, 0x90000, 1280), slice(data, 90 * 128, 1280), "records 90-99"));
    // Refusals, nothing written: a count of 0 and of 513, units 0-12, aperture slots 2 and 3's windows (OODOS's
    // card banks 2 and 3), from slot 1's into slot 2's, OODOS's memory.
    r.poke(0x94000, std::vector<uint8_t>(256, 0xEE));
    for (const auto &[count, dst] : std::vector<std::pair<unsigned, uint32_t>>{
             {0, 0x94000}, {513, 0x94000}, {1, 0x83300}, {1, 0x88000}, {1, 0x8C000}, {2, 0x87F80}, {4, 0xF2000}})
    {
        const std::vector<uint8_t> before = mem(r, dst, 512);
        CHECK_EQ(load(r, 0, count, dst).ret, std::string("FF FF"));
        CHECK(mem(r, dst, 512) == before);
    }
    // An FCB never opened: as 33, the file has no records: A = 1, none, nothing written.
    std::vector<uint8_t> fcb(36, 0);
    fcb[0] = 2;
    const std::string n = "SHORT   BIN";
    for (unsigned i = 0; i < 11; ++i)
        fcb[1 + i] = static_cast<uint8_t>(n[i]);
    r.poke(FCB, fcb);
    l = load(r, 0, 4, 0x94000);
    CHECK_EQ(l.ret, std::string("01 00"));
    CHECK_EQ(l.records, 0u);
    CHECK(mem(r, 0x94000, 256) == std::vector<uint8_t>(256, 0xEE));
}

TEST(a_load_past_a_full_extent_s_end_leaves_the_fcb_at_the_last_record)
{
    // A file of exactly 128 records (one full extent), loaded from 0 with a count of 200: the step for record 128
    // finds no extent. The FCB must be left as a random read of record 127 leaves it: extent 0, CR 127, so a
    // sequential read (20) reads record 127 again, as after function 33 (review F4).
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(128 * 128, 77);
    put_b(r, "FULL    BIN", data);
    open(r, "X");
    open_file(r, 2, "FULL    BIN");
    Load l = load(r, 0, 200, 0x90000);
    CHECK_EQ(l.ret, std::string("01 00"));
    CHECK_EQ(l.records, 128u);
    CHECK(same(mem(r, 0x90000, 128 * 128), data, "the extent"));
    std::printf("  the FCB after: EX %u, S2 %02X, CR %u, R %u\n", r.phys(FCB + 12), r.phys(FCB + 14), r.phys(FCB + 32),
                r.phys(FCB + 33) | r.phys(FCB + 34) << 8);
    CHECK_EQ(r.phys(FCB + 12), 0);
    CHECK_EQ(r.phys(FCB + 32), 127);
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>(128, 0xEE));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK(same(mem(r, 0x3000, 128), slice(data, 127 * 128, 128), "function 20 after the load: record 127"));
}

TEST(a_block_never_written_ends_the_load)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(64 * 128, 8); // 8 KB: blocks 0 and 1
    put_b(r, "HOLE    BIN", data);
    // The second block's pointer taken out of its entry, behind OODOS's back: a hole.
    std::vector<uint8_t> dir = r.read_unit(B_UNIT, 0);
    bool done = false;
    for (unsigned e = 0; e < 16 && !done; ++e)
        if (std::string(dir.begin() + e * 32 + 1, dir.begin() + e * 32 + 12) == "HOLE    BIN")
        {
            dir[e * 32 + 18] = dir[e * 32 + 19] = 0;
            done = true;
        }
    REQUIRE(done);
    unit_write(r, B_UNIT, 0, dir);
    open(r, "X");
    open_file(r, 2, "HOLE    BIN");
    Load l = load(r, 0, 64, 0x90000);
    CHECK_EQ(l.ret, std::string("01 00"));
    CHECK_EQ(l.records, 32u);
    CHECK(same(mem(r, 0x90000, 4096), slice(data, 0, 4096), "the first block"));
}

TEST(records_written_and_not_closed_are_loaded_as_written)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(32 * 128, 6);
    put_b(r, "WRITTEN BIN", data);
    open(r, "X");
    open_file(r, 2, "WRITTEN BIN");
    // Record 8 written (34), not closed: it is in a buffer, the card has the old one.
    const std::vector<uint8_t> rec = noise(128, 99);
    r.poke(0x3000, rec);
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x3000), std::string("00 00"));
    r.poke(FCB + 33, std::vector<uint8_t>{8, 0, 0});
    CHECK_EQ(gcall(r, F_WRITERAND_FN, FCB), std::string("00 00"));
    Load l = load(r, 0, 32, 0x90000);
    CHECK_EQ(l.ret, std::string("00 00"));
    std::vector<uint8_t> want = slice(data, 0, 32 * 128);
    std::copy(rec.begin(), rec.end(), want.begin() + 8 * 128);
    CHECK(same(mem(r, 0x90000, 32 * 128), want, "the file with record 8 written"));
}

TEST(m_and_an_apple_do_unit_load_too)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(24 * 1024, 12);
    r.put_m({{0, "TILES   BIN", data}});
    REQUIRE(r.mount(3, "/DISKS/CPM.DSK") == 0);
    open(r, "X");
    open_file(r, 13, "TILES   BIN");
    Load l = load(r, 0, 192, 0x90000);
    CHECK_EQ(l.ret, std::string("00 00"));
    CHECK(same(mem(r, 0x90000, 24 * 1024), slice(data, 0, 24 * 1024), "M:'s 24 KB"));
    std::printf("  24 KB from M:: %llu cycles\n", static_cast<unsigned long long>(l.cycles));
    // C:, an apple-do image on the card: through its track buffer.
    open_file(r, 3, "PATTERN BIN");
    const std::vector<uint8_t> pattern = gen(20000, 5);
    l = load(r, 0, 157, 0xA0000);
    CHECK_EQ(l.ret, std::string("00 00"));
    CHECK(same(mem(r, 0xA0000, 20000), pattern, "C:'s PATTERN.BIN")); // (its last record's tail is the image's)
    std::printf("  20 KB from an apple-do unit: %llu cycles\n", static_cast<unsigned long long>(l.cycles));
}

TEST(a_stream_plays_through_a_64_kb_load_with_no_underrun)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(65536, 33);
    put_b(r, "PICTURE BIN", data);
    open(r, "2");
    open_file(r, 2, "PICTURE BIN");
    // 44.1 kHz 16-bit stereo, two buffers of 32 KB in banks 1 and 2, then the load.
    r.poke(0x10000, noise(65536, 1));
    r.poke(0x3000, std::vector<uint8_t>{1, 2});                                          // SN_CLAIM the stream
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{7, 0, 0x44, 0xAC, 3});                           // SN_STREAM
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{8, 0x00, 0x00, 0x01, 0x00, 0x80});               // SN_BUFFER $10000
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{8, 0x00, 0x80, 0x01, 0x00, 0x80});               // and $18000
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("01 00"));
    r.run(ms(20));
    const uint32_t m0 = ms_count(r);
    const uint64_t c0 = r.cpu->cycles();
    Load l = load(r, 0, 512, 0x90000);
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    CHECK_EQ(l.ret, std::string("00 00"));
    CHECK(same(mem(r, 0x90000, 65536), data, "64 KB while the stream plays"));
    const unsigned counted = ms_count(r) - m0;
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    r.run(ms(100));
    SoundCard &card = *r.machine->sound_card();
    std::printf("  the load %llu cycles with the stream; its state %u, underruns %u\n",
                static_cast<unsigned long long>(l.cycles), card.peek(SoundCard::STATE), r.phys(0x2000 + 106));
    CHECK_EQ(card.peek(SoundCard::STATE), SoundCard::STATE_PLAYING);
    CHECK_EQ(r.phys(0x2000 + 106), 0); // EV_SUNDER
}

// L4a (hw/oolib.md, "L4a: open's byte count"): function 15 with CR $FF gives the byte count of the file's
// last record, which cpmtools keeps in the last extent's entry's S1 only (the others' 0), whichever extent
// is opened. put_m writes such a file as cpmtools does (make_card.sh's cpmcp, oodos-ram: BIG of 70,050
// bytes, EX 1, 3 and 4, S1 0, 0 and 34).
namespace
{
    uint64_t open_cycles = 0; ///< open_cr's last call's, from L4TEST's JSR BDOS to its return
    /// The FCB at FCB opened with `ex` and `cr` on entry: "aa xx", and its 36 bytes after.
    std::pair<std::string, std::vector<uint8_t>> open_cr(CcpRig &r, unsigned drive, const std::string &name83,
                                                         uint8_t ex, uint8_t cr)
    {
        static const std::map<std::string, uint16_t> lbl = labels(SYS + "/l4test.lbl");
        const uint16_t call = static_cast<uint16_t>(lbl.at("c_o") + 6), back = static_cast<uint16_t>(call + 3);
        uint64_t c0 = 0;
        open_cycles = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == call && r.nb(0) == 0)
                c0 = r.cpu->cycles();
            else if (pc == back && r.nb(0) == 0 && c0)
                open_cycles = r.cpu->cycles() - c0;
        };
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = static_cast<uint8_t>(drive);
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        fcb[12] = ex;
        fcb[32] = cr;
        r.poke(FCB, fcb);
        const std::string ret = command(r, 'O', "O");
        r.watch = nullptr;
        return {ret, mem(r, FCB, 36)};
    }
    /// M:'s entries of user `user`'s `name83`, in the directory's order.
    std::vector<unsigned> m_entries(CcpRig &r, unsigned user, const std::string &name83)
    {
        std::vector<unsigned> out;
        for (unsigned i = 0; i < 256; ++i)
        {
            bool match = r.phys(MDISK + i * 32) == user;
            for (unsigned c = 0; c < 11 && match; ++c)
                match = (r.phys(MDISK + i * 32 + 1 + c) & 0x7F) == static_cast<uint8_t>(name83[c]);
            if (match)
                out.push_back(i);
        }
        return out;
    }
    /// Each extent `exs` of M:'s `name83` opened with CR $FF gives `want` in CR, and the FCB is otherwise
    /// byte for byte the one an open with CR 0 gives; an open with CR 5 leaves CR 5.
    void counts(CcpRig &r, const std::string &name83, const std::vector<uint8_t> &exs, uint8_t want)
    {
        for (const uint8_t ex : exs)
        {
            const auto [ret0, plain] = open_cr(r, 13, name83, ex, 0);
            const uint64_t c0 = open_cycles;
            const auto [ret, got] = open_cr(r, 13, name83, ex, 0xFF);
            const uint64_t cff = open_cycles;
            const auto [ret5, five] = open_cr(r, 13, name83, ex, 5);
            std::printf("  %s EX %u: CR $%02X (want $%02X), RC $%02X; %llu cycles with CR 0, %llu with $FF\n",
                        name83.c_str(), ex, got[32], want, got[15], static_cast<unsigned long long>(c0),
                        static_cast<unsigned long long>(cff));
            CHECK(c0 > 0 && cff > 0);
            CHECK_EQ(ret0, std::string("00 00"));
            CHECK_EQ(ret, std::string("00 00"));
            CHECK_EQ(got[32], want);
            CHECK(std::equal(got.begin(), got.begin() + 32, plain.begin()));
            CHECK_EQ(plain[32], 0);
            CHECK_EQ(five[32], 5);
        }
    }
} // namespace

TEST(open_with_cr_ff_gives_the_file_s_last_record_s_count_from_its_last_entry)
{
    {
        // Three entries (EX 1, 3 and 4), the last record 104 bytes: $68 from EX 0, 2 and 4.
        CcpRig r;
        REQUIRE(r.ok);
        r.put_m({{0, "BIG     BIN", noise(65536 + 1000, 3), false, false, true},
                 {0, "ONE     BIN", noise(1024 + 45, 4), false, false, true},
                 {0, "FULL    BIN", noise(4096, 5), false, false, true}});
        const std::vector<unsigned> es = m_entries(r, 0, "BIG     BIN");
        REQUIRE(es.size() == 3u);
        CHECK_EQ(r.phys(MDISK + es[0] * 32 + 13), 0);
        CHECK_EQ(r.phys(MDISK + es[1] * 32 + 13), 0);
        CHECK_EQ(r.phys(MDISK + es[2] * 32 + 13), 0x68);
        open(r, "X");
        counts(r, "BIG     BIN", {0, 1, 2, 3, 4}, 0x68);
        counts(r, "ONE     BIN", {0}, 45);    // one entry: its own, as before
        counts(r, "FULL    BIN", {0}, 0);     // a full last record: cpmtools's 0
    }
    {
        // The last extent's entry first in the directory, the middle one last: the highest extent's,
        // not the last found.
        CcpRig r;
        REQUIRE(r.ok);
        r.put_m({{0, "REV     BIN", noise(65536 + 1000, 6), false, false, true}});
        const std::vector<unsigned> es = m_entries(r, 0, "REV     BIN");
        REQUIRE(es.size() == 3u);
        std::vector<std::vector<uint8_t>> e;
        for (const unsigned i : es)
            e.push_back(mem(r, MDISK + i * 32, 32));
        r.poke(MDISK + es[0] * 32, e[2]);
        r.poke(MDISK + es[1] * 32, e[0]);
        r.poke(MDISK + es[2] * 32, e[1]);
        open(r, "X");
        counts(r, "REV     BIN", {0, 2, 4}, 0x68);
    }
    {
        // A SYS file of user 0's, opened from user 3 (f8'): its extents are looked for in user 0.
        CcpRig r;
        REQUIRE(r.ok);
        r.put_m({{0, "SYS0    BIN", noise(65536 + 1000, 7), true, false, true}});
        open(r, "X");
        r.scb(0x44, 3); // SCB_USER, behind the CCP's back: L4TEST makes no BDOS call as it waits
        counts(r, "SYS0    BIN", {0, 4}, 0x68);
        CHECK(r.phys(FCB + 8) & 0x80); // f8': opened from user 0
        r.scb(0x44, 0);
    }
    {
        // Across a module: entries written by hand (no blocks: open reads none), the one of S2 1, EX 0
        // (extent 32) first in the directory, then extent 0's, then S2 0, EX 31's. S2 1's S1 (9), not
        // EX 31's (7): the extent is S2's and EX's, not EX alone, and not the last entry found.
        CcpRig r;
        REQUIRE(r.ok);
        auto entry = [&](uint8_t ex, uint8_t s1, uint8_t s2, uint8_t rc) {
            std::vector<uint8_t> e(32, 0);
            const std::string name83 = "MOD     BIN";
            for (unsigned i = 0; i < 11; ++i)
                e[1 + i] = static_cast<uint8_t>(name83[i]);
            e[12] = ex, e[13] = s1, e[14] = s2, e[15] = rc;
            r.poke(MDISK + r.free_entry() * 32, e);
        };
        entry(0, 9, 1, 5);
        entry(0, 0, 0, 128);
        entry(31, 7, 0, 128);
        open(r, "X");
        counts(r, "MOD     BIN", {0, 31}, 9);
    }
    {
        // S1 in every entry but the last, as function 30 with f6' leaves it before the file grows (MAKEENT
        // gives a new entry S1 0): 0, the last entry's, not the nonzero or the largest. Then $44, $44 and
        // $33: the last entry's $33, not the largest.
        for (const auto &[s01, s2, want] : {std::tuple<uint8_t, uint8_t, uint8_t>{0x33, 0, 0}, {0x44, 0x33, 0x33}})
        {
            CcpRig r;
            REQUIRE(r.ok);
            r.put_m({{0, "GREW    BIN", noise(65536 + 1000, 9)}});
            const std::vector<unsigned> es = m_entries(r, 0, "GREW    BIN");
            REQUIRE(es.size() == 3u);
            r.poke(MDISK + es[0] * 32 + 13, s01);
            r.poke(MDISK + es[1] * 32 + 13, s01);
            r.poke(MDISK + es[2] * 32 + 13, s2);
            open(r, "X");
            counts(r, "GREW    BIN", {0, 2, 4}, want);
        }
    }
    {
        // With access stamps, the open of extent 0 with CR $FF stamps extent 0's SFCB slot, and nothing
        // else in the directory changes.
        CcpRig r;
        REQUIRE(r.ok);
        r.put_m({{0, "BIG     BIN", noise(65536 + 1000, 8), false, false, true}});
        const std::vector<unsigned> es = m_entries(r, 0, "BIG     BIN");
        REQUIRE(es.size() == 3u);
        r.poke(MDISK + 12, 0x61); // the label: access and update stamps
        r.set_time(2027, 3, 4, 5, 6, 0);
        open(r, "X");
        const std::vector<uint8_t> before = mem(r, MDISK, 256 * 32);
        const auto [ret, got] = open_cr(r, 13, "BIG     BIN", 0, 0xFF);
        CHECK_EQ(ret, std::string("00 00"));
        CHECK_EQ(got[32], 0x68);
        const std::vector<uint8_t> after = mem(r, MDISK, 256 * 32);
        const unsigned slot = (es[0] | 3) * 32 + 1 + (es[0] & 3) * 10; // extent 0's: its access stamp first
        for (unsigned i = 0; i < after.size(); ++i)
            if (after[i] != before[i])
            {
                std::printf("  directory byte %u changed: %02X to %02X\n", i, before[i], after[i]);
                CHECK(i >= slot && i < slot + 4);
            }
        CHECK(!std::equal(after.begin() + slot, after.begin() + slot + 4, before.begin() + slot));
    }
}

int main() { return run_all(); }
