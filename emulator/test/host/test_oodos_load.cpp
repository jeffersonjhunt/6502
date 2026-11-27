/**
 * @file test_oodos_load.cpp
 * @brief OOLIB's L4 (hw/oolib.md, "The plan"): function 83's DM_LOAD, a file's records loaded into
 * physical memory by DMA: whole sectors straight from the SD card, the ends and the apple-do drives
 * through the BDOS's buffers, M: by DMAGO. And functions 20 and 33 with a multi-sector count, whose
 * whole sectors go straight to the program the same way (bdosfile.s's DIRECT, sharing DM_LOAD's run).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * L4TEST (oodos/sys/test/l4test.s) as test_oodos_video runs it. The files are put on B: behind OODOS's
 * back (oodos_rig.h's put_b), on M: (put_m), or are the committed card's /DISKS/CPM.DSK's (its
 * PATTERN.BIN, make_card.sh's), mounted as C:.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
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
    /// A BDOS call by L4TEST's G command: "aa xx", the cycles from its JSR BDOS to the return, and the program's
    /// W1 block then.
    struct Call
    {
        std::string ret;
        uint64_t cycles{0};
        unsigned w1{0};
    };
    Call timed(CcpRig &r, uint8_t fn, unsigned ax)
    {
        static const std::map<std::string, uint16_t> lbl = labels(SYS + "/l4test.lbl");
        const uint16_t call = static_cast<uint16_t>(lbl.at("c_g") + 9), back = static_cast<uint16_t>(call + 3);
        CHECK_EQ(r.phys(call), 0x20); // c_g's JSR BDOS, after its LDA, LDX and LDY
        Call c;
        uint64_t c0 = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == call && r.nb(0) == 0)
                c0 = r.cpu->cycles();
            else if (pc == back && r.nb(0) == 0 && c0 && !c.cycles)
            {
                c.cycles = r.cpu->cycles() - c0;
                c.w1 = r.nb(1);
            }
        };
        c.ret = gcall(r, fn, ax);
        r.watch = nullptr;
        return c;
    }
    /// The cycles a record of `calls` calls of `fn` with a count of `count` (44) on the open FCB, from the third
    /// call on (every buffer in use, as a long read finds them); 33 from record 0, a call's records on each time.
    double per_record(CcpRig &r, uint8_t fn, unsigned count, unsigned calls)
    {
        CHECK_EQ(gcall(r, 44, count), std::string("00 00"));
        uint64_t sum = 0;
        for (unsigned c = 0; c < calls; ++c)
        {
            if (fn == 33)
            {
                const unsigned rec = c * count;
                r.poke(FCB + 33, std::vector<uint8_t>{static_cast<uint8_t>(rec), static_cast<uint8_t>(rec >> 8), 0});
            }
            const Call k = timed(r, fn, FCB);
            CHECK_EQ(k.ret, std::string("00 00"));
            if (c >= 2)
                sum += k.cycles;
        }
        return static_cast<double>(sum) / ((calls - 2) * count);
    }
    /// The program's W1 block, as a BDOS call (12, the version) returns to it.
    unsigned prog_w1(CcpRig &r) { return timed(r, 12, 0).w1; }
    /// `n` bytes of the program's memory from `at` in its W1 (block `w1`).
    std::vector<uint8_t> w1_mem(CcpRig &r, unsigned w1, uint16_t at, size_t n)
    {
        return mem(r, w1 * 16384u + (at - 0x4000u), n);
    }
    void w1_fill(CcpRig &r, unsigned w1, uint16_t at, size_t n, uint8_t b)
    {
        r.poke(w1 * 16384u + (at - 0x4000u), std::vector<uint8_t>(n, b));
    }
    /// The FCB at FCB for `name83` on `drive`, not opened.
    void name_fcb(CcpRig &r, uint32_t at, unsigned drive, const std::string &name83)
    {
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = static_cast<uint8_t>(drive);
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        r.poke(at, fcb);
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
    // The same 64 KB by 51 and 33 with a count of 128 (44), four calls, into blocks 40-43. scall is the loop's
    // first instruction, so the four calls are timed from the first time through it, not the last.
    static const std::map<std::string, uint16_t> lbl = labels(SYS + "/l4test.lbl");
    const uint16_t sc = lbl.at("scall"), sr = lbl.at("sret");
    uint64_t s0 = 0, scycles = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == sc && r.nb(0) == 0 && !s0)
            s0 = r.cpu->cycles();
        else if (pc == sr && r.nb(0) == 0 && s0)
            scycles = r.cpu->cycles() - s0;
    };
    r.poke(FCB + 33, std::vector<uint8_t>{0, 0, 0});
    CHECK_EQ(command(r, 'S', "S", {128, 0x00, 0xA0, 4}), std::string("00 00")); // segment $A000: $A0000
    r.watch = nullptr;
    CHECK(same(mem(r, 0xA0000, 65536), slice(data, 0, 65536), "64 KB by 33"));
    std::printf("  64 KB: DM_LOAD %llu cycles (%.1f ms), four 33 calls of 128 %llu cycles (%.1f ms); %u ms counted, %.1f in the cycles\n",
                static_cast<unsigned long long>(l.cycles), static_cast<double>(l.cycles) / HZ * 1000,
                static_cast<unsigned long long>(scycles), static_cast<double>(scycles) / HZ * 1000, counted, secs * 1000);
    CHECK(l.cycles > 0 && scycles > l.cycles); // 33's whole sectors go straight too, but DM_LOAD is still the faster
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
    r.poke(0x3000, std::vector<uint8_t>{12, 0, 0x44, 0xAC, 3});                          // SN_STREAM
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{13, 0x00, 0x00, 0x01, 0x00, 0x80});              // SN_BUFFER $10000
    CHECK_EQ(gcall(r, O_SOUND_FN, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{13, 0x00, 0x80, 0x01, 0x00, 0x80});              // and $18000
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
    std::printf("  the load %llu cycles with the stream; its state %u, underruns %u\n",
                static_cast<unsigned long long>(l.cycles), r.phys(0x2000 + 96), r.phys(0x2000 + 106));
    CHECK_EQ(r.phys(0x2000 + 96), 2);  // EV_SSTATE: playing
    CHECK_EQ(r.phys(0x2000 + 106), 0); // EV_SUNDER
}

// For the record, not gated (the figures in the Programmer's Guide's entries for 20, 33 and 44): the cycles a record
// of 20 and 33 with a count, and of DM_LOAD, the DMA address $4000 (the program's W1) or a segment.
TEST(for_the_record_the_cycles_a_record_of_20_33_and_dm_load)
{
    CcpRig r;
    REQUIRE(r.ok);
    put_b(r, "BIG     BIN", noise(96 * 1024, 3));
    r.put_m({{0, "BIG     BIN", noise(64 * 1024, 3)}});
    open(r, "X");
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    const auto figure = [&](const char *what, unsigned drive, uint8_t fn, unsigned count, unsigned calls) {
        open_file(r, drive, "BIG     BIN");
        std::printf("  %-34s %6.0f cycles a record\n", what, per_record(r, fn, count, calls));
    };
    figure("B: 20 x 1", 2, 20, 1, 40);
    figure("B: 20 x 96", 2, 20, 96, 8);
    figure("B: 20 x 128", 2, 20, 128, 6);
    figure("M: 20 x 96", 13, 20, 96, 5);
    figure("M: 20 x 128", 13, 20, 128, 4);
    CHECK_EQ(gcall(r, 51, 0xA000), std::string("00 00")); // a segment: $A0000
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0), std::string("00 00"));
    figure("B: 33 x 128, segment $A000", 2, 33, 128, 5);
    CHECK_EQ(gcall(r, 51, 0xFFFF), std::string("00 00"));
    open_file(r, 2, "BIG     BIN");
    const Load l = load(r, 0, 512, 0x90000);
    CHECK_EQ(l.ret, std::string("00 00"));
    std::printf("  %-34s %6.0f cycles a record\n", "B: DM_LOAD, 64 KB", static_cast<double>(l.cycles) / 512);
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

// Functions 20 and 33 with a count: whole sectors straight to the program (bdosfile.s's DIRECT). Each case is
// a way a run could go wrong; every one holds for the record way as well (a record a buffer), as before.

TEST(a_multi_record_read_takes_a_changed_buffer_s_record_not_the_card_s)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(96 * 128, 9);
    put_b(r, "BIG     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 2, "BIG     BIN");
    // Record 9 written (34), not closed: its sector is in a buffer, changed; the card has the old one.
    const std::vector<uint8_t> rec = noise(128, 99);
    r.poke(0x3000, rec);
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x3000), std::string("00 00"));
    r.poke(FCB + 33, std::vector<uint8_t>{9, 0, 0});
    CHECK_EQ(gcall(r, F_WRITERAND_FN, FCB), std::string("00 00"));
    open_file(r, 2, "BIG     BIN");
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    w1_fill(r, w1, 0x4000, 96 * 128, 0xEE);
    CHECK_EQ(gcall(r, 44, 96), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    std::vector<uint8_t> want = data;
    std::copy(rec.begin(), rec.end(), want.begin() + 9 * 128);
    CHECK(same(w1_mem(r, w1, 0x4000, 96 * 128), want, "96 records, record 9 the written one"));
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_multi_record_read_ends_at_the_file_s_end_inside_a_sector)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(99 * 128, 11); // the last sector holds 3 records
    put_b(r, "ODD     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 2, "ODD     BIN");
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    w1_fill(r, w1, 0x4000, 128 * 128, 0xEE);
    CHECK_EQ(gcall(r, 44, 128), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("01 63")); // the end, 99 records read
    const std::vector<uint8_t> got = w1_mem(r, w1, 0x4000, 128 * 128);
    CHECK(same(std::vector<uint8_t>(got.begin(), got.begin() + 99 * 128), data, "the 99 records"));
    CHECK(std::all_of(got.begin() + 99 * 128, got.end(), [](uint8_t b) { return b == 0xEE; })); // nothing past
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_multi_record_read_follows_blocks_that_are_not_next_to_each_other)
{
    // Two files on M: (2 KB blocks) written in turn, a block at a time: each file's blocks are every other one.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 16), std::string("00 00"));
    constexpr uint32_t FCB2 = 0x2170;
    name_fcb(r, FCB, 13, "AAAA    DAT");
    name_fcb(r, FCB2, 13, "BBBB    DAT");
    CHECK_EQ(gcall(r, 22, FCB), std::string("00 00"));
    CHECK_EQ(gcall(r, 22, FCB2), std::string("00 00"));
    const std::vector<uint8_t> da = noise(160 * 128, 21), db = noise(160 * 128, 22);
    for (unsigned k = 0; k < 10; ++k)
    {
        r.poke(w1 * 16384u, std::vector<uint8_t>(da.begin() + k * 2048, da.begin() + (k + 1) * 2048));
        CHECK_EQ(gcall(r, 21, FCB), std::string("00 00"));
        r.poke(w1 * 16384u, std::vector<uint8_t>(db.begin() + k * 2048, db.begin() + (k + 1) * 2048));
        CHECK_EQ(gcall(r, 21, FCB2), std::string("00 00"));
    }
    CHECK_EQ(gcall(r, 16, FCB), std::string("00 00"));
    CHECK_EQ(gcall(r, 16, FCB2), std::string("00 00"));
    open_file(r, 13, "AAAA    DAT");
    // A sector first, then 96 records from record 4: the first run starts inside a block.
    CHECK_EQ(gcall(r, 44, 4), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 96), std::string("00 00"));
    w1_fill(r, w1, 0x4000, 96 * 128, 0xEE);
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 96 * 128), slice(da, 4 * 128, 96 * 128), "records 4-99 of AAAA.DAT"));
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_random_multi_record_read_from_inside_a_sector_starts_its_runs_at_the_next)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(200 * 128, 17);
    put_b(r, "RND     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 2, "RND     BIN"); // no buffer holds a sector of the file
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 48), std::string("00 00"));
    r.poke(FCB + 33, std::vector<uint8_t>{101, 0, 0});
    CHECK_EQ(gcall(r, 33, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 48 * 128), slice(data, 101 * 128, 48 * 128), "records 101-148"));
    CHECK_EQ(r.phys(FCB + 32), 20); // record 148's place in extent 1
    CHECK_EQ(r.phys(FCB + 12), 1);
    CHECK_EQ(r.phys(FCB + 33), 101);
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_random_multi_record_read_leaves_cr_at_its_last_record_as_one_at_a_time_does)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(200 * 128, 17);
    put_b(r, "RND     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 2, "RND     BIN");
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 48), std::string("00 00"));
    // From 104, 48 records: 104-127, then 128-151, the last a run's.
    r.poke(FCB + 33, std::vector<uint8_t>{104, 0, 0});
    CHECK_EQ(gcall(r, 33, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 48 * 128), slice(data, 104 * 128, 48 * 128), "records 104-151"));
    CHECK_EQ(r.phys(FCB + 32), 23); // record 151's place in extent 1
    CHECK_EQ(r.phys(FCB + 12), 1);
    CHECK_EQ(r.phys(FCB + 33), 104); // R0-R2 the first's
    // So 20 next reads record 151 again.
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 128), slice(data, 151 * 128, 128), "function 20 after: record 151"));
}

TEST(a_multi_record_read_into_two_windows_that_are_not_next_to_each_other)
{
    // W1 on block 20 (L4TEST's M), W0 on block 0: the program's $3FFF and $4000 are $3FFF and $50000. A DMA
    // address near W0's end: the records reach W1 where the program sees it, and block 1 ($4000-) gets none.
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(64 * 128, 13);
    put_b(r, "WIN     BIN", data);
    open(r, "X");
    CHECK_EQ(command(r, 'M', "M", {20}).substr(0, 2), std::string("14"));
    // (review 1, T1: $3C80 and $3E80, not on a page, hold DIRECT's round-up of a part page to the window's end)
    for (const uint16_t at : {uint16_t(0x3C80), uint16_t(0x3E00), uint16_t(0x3E80), uint16_t(0x3F00), uint16_t(0x3F80)})
    {
        open_file(r, 2, "WIN     BIN");
        CHECK_EQ(gcall(r, F_DMAOFF_FN, at), std::string("00 00"));
        CHECK_EQ(gcall(r, 44, 32), std::string("00 00"));
        r.poke(20 * 16384u, std::vector<uint8_t>(4096, 0xEE));
        r.poke(1 * 16384u, std::vector<uint8_t>(4096, 0xEE));
        CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
        std::vector<uint8_t> got = mem(r, at, 0x4000u - at);
        const std::vector<uint8_t> rest = mem(r, 20 * 16384u, 32 * 128 - got.size());
        got.insert(got.end(), rest.begin(), rest.end());
        char what[16];
        std::snprintf(what, sizeof what, "from $%04X", at);
        CHECK(same(got, slice(data, 0, 32 * 128), what));
        CHECK(mem(r, 1 * 16384u, 4096) == std::vector<uint8_t>(4096, 0xEE));
    }
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_multi_record_read_on_an_apple_do_unit_goes_through_its_track_buffer)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(r.mount(3, "/DISKS/CPM.DSK") == 0);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 3, "PATTERN BIN");
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 96), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 96 * 128), gen(96 * 128, 5), "C:'s PATTERN.BIN, 96 records"));
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

TEST(a_multi_record_read_through_a_segment_into_oodos_s_card_memory_stops_where_it_did)
{
    // A segment at $87800: records 0-15 fill the rest of slot 1's window (the program's card bank); record 16
    // would be slot 2's, OODOS's card memory. The call is refused there, A = X = $FF, as a record at a time
    // always refused it, the first 16 records written and nothing of slot 2's.
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(64 * 128, 23);
    put_b(r, "SEG     BIN", data);
    open(r, "X");
    open_file(r, 2, "SEG     BIN");
    const std::vector<uint8_t> slot2 = mem(r, 0x88000, 4096);
    CHECK_EQ(gcall(r, 51, 0x8780), std::string("00 00"));
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 32), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("FF FF"));
    CHECK(same(mem(r, 0x87800, 16 * 128), slice(data, 0, 16 * 128), "records 0-15 in slot 1's window"));
    CHECK(mem(r, 0x88000, 4096) == slot2);
    CHECK_EQ(gcall(r, 51, 0xFFFF), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

namespace
{
    /// The card, failing every read once `left` more have been served (while `armed`).
    struct CountFaultMedia : MemoryMedia
    {
        using MemoryMedia::MemoryMedia;
        bool armed{false};
        int left{0};
        bool read(uint64_t lba, uint8_t *out) override
        {
            if (armed && left-- <= 0)
                return false;
            return MemoryMedia::read(lba, out);
        }
    };
} // namespace

TEST(a_physical_error_in_a_multi_record_read_keeps_the_records_read_before_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    auto fm = std::make_shared<CountFaultMedia>(r.media->bytes(), "card");
    r.media = fm;
    r.sd().insert(fm, false);
    r.run(ms(SdCard::INIT_MS + 10));
    REQUIRE(r.mount(1, "/OODOS/A.IMG") == 0);
    REQUIRE(r.mount(2, "/OODOS/B.IMG") == 0);
    const std::vector<uint8_t> data = noise(96 * 128, 19);
    put_b(r, "ERR     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    open_file(r, 2, "ERR     BIN");
    CHECK_EQ(gcall(r, 45, 0xFF), std::string("00 00")); // errors returned
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    CHECK_EQ(gcall(r, 44, 96), std::string("00 00"));
    w1_fill(r, w1, 0x4000, 96 * 128, 0xEE);
    // The card serves 8 sectors more (records 0-31), then fails: Disk I/O, CR at record 32.
    fm->left = 8;
    fm->armed = true;
    const std::string ret = gcall(r, 20, FCB);
    fm->armed = false;
    CHECK_EQ(ret, std::string("FF 01"));
    CHECK_EQ(r.phys(FCB + 32), 32);
    CHECK(same(w1_mem(r, w1, 0x4000, 32 * 128), slice(data, 0, 32 * 128), "records 0-31"));
    // The next call goes on from record 32.
    CHECK_EQ(gcall(r, 44, 32), std::string("00 00"));
    CHECK_EQ(gcall(r, 20, FCB), std::string("00 00"));
    CHECK(same(w1_mem(r, w1, 0x4000, 32 * 128), slice(data, 32 * 128, 32 * 128), "records 32-63"));
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
}

// The one guard of the run's speed (the owner's answer 7, 2026-11-26): every case above passes whether DIRECT
// takes a run or not. Function 20 with a count of 96 on B:, the buffers full: 319 cycles a record when its runs
// are taken, 4,586 when every record goes through a buffer. A ceiling, with a margin. Each call's records are
// compared with the file too, its 8 calls across 6 extents (review 1, T3: 20's runs up to and past an extent's end).
TEST(function_20_with_a_count_of_96_reads_under_400_cycles_a_record)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(96 * 1024, 3);
    put_b(r, "BIG     BIN", data);
    open(r, "X");
    const unsigned w1 = prog_w1(r);
    CHECK_EQ(gcall(r, F_DMAOFF_FN, 0x4000), std::string("00 00"));
    open_file(r, 2, "BIG     BIN");
    CHECK_EQ(gcall(r, 44, 96), std::string("00 00"));
    uint64_t sum = 0;
    for (unsigned k = 0; k < 8; ++k)
    {
        w1_fill(r, w1, 0x4000, 96 * 128, 0xEE);
        const Call call = timed(r, 20, FCB);
        CHECK_EQ(call.ret, std::string("00 00"));
        char what[24];
        std::snprintf(what, sizeof what, "call %u's records", k);
        CHECK(same(w1_mem(r, w1, 0x4000, 96 * 128), slice(data, k * 96 * 128, 96 * 128), what));
        if (k >= 2)
            sum += call.cycles;
    }
    const double c = static_cast<double>(sum) / (6 * 96);
    if (c >= 400)
        std::printf("  B: 20 x 96: %.0f cycles a record\n", c);
    CHECK(c < 400);
    CHECK_EQ(gcall(r, 44, 1), std::string("00 00"));
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

