/**
 * @file test_oodos_video.cpp
 * @brief OOLIB's L4 (hw/oolib.md, "The plan"): function 83's DM_INFO, copies, fills, vertical-blank slots,
 * pointer and console; the W1 contract; and the warm boot's video part.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_sound boots it (oodos_rig.h). L4TEST (oodos/sys/test/l4test.s), run by
 * GO, opens its area at $2000 and takes commands, a letter at $2100 and its arguments after it; function
 * 83's blocks the test builds at $2110, and the data they name it puts in physical memory itself.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "video_card.h"

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t AREA = 0x2000, MBOX = 0x2100, KBLK = 0x2110, HW1 = 0x2180, HCNT = 0x2181; ///< l4test.s's
    constexpr unsigned EV_VBGO = 12;
    // oodos.inc's
    constexpr uint8_t DM_INFO = 0, DM_COPY = 2, DM_FILL = 3, DM_VBSET = 4, DM_POINTER = 5, DM_CONSOLE = 6, DM_OPS = 7;
    constexpr uint8_t IN_MOUSE = 4, MC_ALLOC = 55, MC_FREE = 57;
    constexpr uint16_t VIDEO = 0xC480;

    uint8_t area(Rig &r, unsigned off) { return r.phys(AREA + off); }
    uint8_t sys(Rig &r, const char *name) { return r.phys(SYS_BLOCK_PHYS + sym(name)); }
    uint32_t ms_count(Rig &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("MS");
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    VideoCard &vid(Rig &r) { return *r.machine->video_card(); }
    uint8_t vreg(Rig &r, uint8_t off) { return vid(r).peek(off); }
    void vset(Rig &r, uint8_t off, uint8_t v) { r.machine->bus().write(static_cast<uint16_t>(VIDEO + off), v); }

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
    std::vector<uint8_t> mem(Rig &r, uint32_t at, size_t n)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = r.phys(at + static_cast<uint32_t>(i));
        return b;
    }
    std::vector<uint8_t> a3(uint32_t a) { return {static_cast<uint8_t>(a), static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(a >> 16)}; }
    std::vector<uint8_t> w2(unsigned v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}; }
    std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> parts)
    {
        std::vector<uint8_t> out;
        for (const auto &p : parts)
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    /// L4TEST with `tail`, run to its OPEN line (or, for X, a little).
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
    /// L4TEST's command `c` (its arguments at MBOX + 1 on): its line's `label` and what follows.
    std::string command(CcpRig &r, char c, const std::string &label, const std::vector<uint8_t> &args = {},
                        uint64_t budget = ms(3000))
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
    std::string f83(CcpRig &r, const std::vector<uint8_t> &block, uint64_t budget = ms(3000))
    {
        r.poke(KBLK, block);
        return command(r, 'K', "K", {}, budget);
    }
    std::string f83op(CcpRig &r, uint8_t op) { return command(r, 'Z', "Z", {op}); }
    std::string f81(CcpRig &r, const std::vector<uint8_t> &block)
    {
        r.poke(KBLK, block);
        return command(r, 'N', "N");
    }
    /// Function `fn` with A/X = `ax`.
    std::string gcall(CcpRig &r, uint8_t fn, unsigned ax) { return command(r, 'G', "G", {fn, static_cast<uint8_t>(ax), static_cast<uint8_t>(ax >> 8)}); }
    void wboot(CcpRig &r)
    {
        const size_t from = r.out.size();
        r.poke(MBOX, 'W');
        CHECK(r.until("\r\nA>", ms(2000), from));
    }

    /// The dispatcher's cost for each interrupt (its first cycle to its RTI's last), by what it was: a
    /// vertical blank (the frame counter moved), a tick alone.
    struct Costs
    {
        std::map<unsigned, unsigned> frame, tick;
        static void print(const char *what, const std::map<unsigned, unsigned> &m)
        {
            std::printf("  %s:", what);
            for (const auto &[c, k] : m)
                std::printf(" %u x%u", c, k);
            std::printf("\n");
        }
        static unsigned mode(const std::map<unsigned, unsigned> &m)
        {
            unsigned best = 0, n = 0;
            for (const auto &[c, k] : m)
                if (k > n)
                    best = c, n = k;
            return best;
        }
    };
    void time_interrupts(Rig &r, Costs &out, uint64_t cycles, const std::function<void()> &each = {})
    {
        const uint16_t irq = sym("IRQ"), end = sym("IRQEND");
        const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
        uint64_t c0 = 0;
        uint8_t l = 0, h = 0, f = 0;
        bool in = false;
        r.watch = [&](uint16_t pc) {
            if (pc == irq)
            {
                c0 = r.cpu->cycles() - 7;
                l = r.nb(8);
                h = r.nb(9);
                f = r.phys(frames);
                in = true;
            }
            else if (pc == end && in)
            {
                const unsigned c = static_cast<unsigned>(r.cpu->cycles() + 6 - c0);
                if (r.phys(frames) != f)
                    ++out.frame[c];
                else if (l == 0xBF && h == 0xFF)
                    ++out.tick[c];
                in = false;
                if (each)
                    each();
            }
        };
        r.run(cycles);
        r.watch = nullptr;
    }

    std::vector<uint8_t> vbset(uint8_t slot, uint32_t src, uint32_t dst, unsigned len)
    {
        return cat({{DM_VBSET, slot}, a3(src), a3(dst), w2(len)});
    }
} // namespace

TEST(dm_info_ft_info_and_the_w1_map)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    CHECK_EQ(command(r, 'F', "F"), std::string("07 0F")); // interface 7, areas frame, input, sound, DMA
    CHECK_EQ(f83op(r, DM_INFO), std::string("0D 04"));
    // +1 13, +2 4 slots, +3 4 pages, +4 no pointer, +5-12 the map: block 1, blocks 33-47
    CHECK_EQ(command(r, 'I', "INFO"), std::string("0D 04 0D0404FF02000000FEFF0000"));
    // An allocation (55): the lowest free block, 4, joins the map; freed (57), it leaves it.
    r.poke(0x2150, std::vector<uint8_t>{0, 0, 0x00, 0x04, 0}); // 1,024 paragraphs
    CHECK_EQ(gcall(r, MC_ALLOC, 0x2150), std::string("00 00"));
    CHECK_EQ(r.phys(0x2151), 0x10); // M_BASE $1000: block 4
    CHECK_EQ(command(r, 'I', "INFO"), std::string("0D 04 0D0404FF12000000FEFF0000"));
    CHECK_EQ(gcall(r, MC_FREE, 0x2150), std::string("00 00"));
    CHECK_EQ(command(r, 'I', "INFO"), std::string("0D 04 0D0404FF02000000FEFF0000"));
    // Refusals: an operation that isn't one; a block needed and not given.
    CHECK_EQ(f83op(r, DM_OPS), std::string("FF FF"));
    CHECK_EQ(f83op(r, DM_COPY), std::string("FF FF"));
    CHECK_EQ(f83op(r, DM_VBSET), std::string("FF FF"));
}

TEST(w1_holds_a_video_or_allocated_block_across_bdos_calls_interrupts_and_hooks)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    // A video block in W1: the CPU's writes land there, and W1 is kept across function 9.
    CHECK_EQ(command(r, 'M', "M", {33}), std::string("21 00"));
    CHECK_EQ(command(r, 'P', "P", {0, 0x5A}), std::string("00 00"));
    CHECK(mem(r, 33 * BLOCK, 256) == std::vector<uint8_t>(256, 0x5A));
    // Kept across ticks, frames and USB interrupts (a layout-2 area: the keys are queued) while it spins.
    UsbCard *usb = r.machine->usb_card();
    const uint32_t f0 = r.phys(SYS_BLOCK_PHYS + sym("FRAMES")), m0 = ms_count(r);
    unsigned seen = 0, other = 0;
    r.watch = [&](uint16_t pc) {
        if (pc >= 0x0300 && pc < 0x2000 && r.nb(0) == 0)
        {
            ++seen;
            other += r.nb(1) != 33;
        }
    };
    for (int k = 0; k < 8; ++k)
    {
        usb->key(0xE1, true);
        r.run(ms(5));
        usb->key(0xE1, false);
        r.run(ms(5));
    }
    r.watch = nullptr;
    const unsigned frames = r.phys(SYS_BLOCK_PHYS + sym("FRAMES")) - f0, msn = ms_count(r) - m0;
    std::printf("  %u instructions of the program over %u ms and %u frames, 16 USB events: W1 not 33 in %u\n", seen,
                msn, frames, other);
    CHECK(frames >= 4 && msn >= 75 && seen > 1000);
    CHECK_EQ(other, 0u);
    CHECK_EQ(r.phys(AREA + 17) != r.phys(AREA + 16), true); // the events were queued meanwhile
    CHECK_EQ(command(r, 'V', "V"), std::string("21 00"));
    // A hook set now keeps W1 33 when it runs, after the program has put block 1 back.
    CHECK_EQ(command(r, 'H', "H"), std::string("00 00"));
    CHECK_EQ(command(r, 'B', "B"), std::string("01 00"));
    r.poke(HW1, 0);
    r.poke(HCNT, 0);
    r.run(ms(60));
    CHECK(r.phys(HCNT) >= 3);
    CHECK_EQ(r.phys(HW1), 33);
    CHECK_EQ(command(r, 'V', "V"), std::string("01 00"));
    // An allocated block (55's, block 4) in W1: written through it.
    r.poke(0x2150, std::vector<uint8_t>{0, 0, 0x00, 0x04, 0});
    CHECK_EQ(gcall(r, MC_ALLOC, 0x2150), std::string("00 00"));
    CHECK_EQ(command(r, 'M', "M", {4}), std::string("04 00"));
    CHECK_EQ(command(r, 'P', "P", {0, 0xC3}), std::string("00 00"));
    CHECK(mem(r, 4 * BLOCK, 256) == std::vector<uint8_t>(256, 0xC3));
    // The warm boot puts block 1 back: the next program finds it there.
    wboot(r);
    open(r, "X");
    CHECK_EQ(command(r, 'V', "V"), std::string("01 00"));
}

TEST(a_function_33_into_4000_lands_in_the_block_w1_holds)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> data = noise(4096, 7);
    put_b(r, "PICTURE BIN", data);
    open(r, "X");
    std::vector<uint8_t> fcb(36, 0);
    fcb[0] = 2;
    const std::string n = "PICTURE BIN";
    for (unsigned i = 0; i < 11; ++i)
        fcb[1 + i] = static_cast<uint8_t>(n[i]);
    fcb[33] = 5; // record 5
    r.poke(0x2140, fcb);
    CHECK_EQ(command(r, 'O', "O"), std::string("00 00"));
    CHECK_EQ(command(r, 'M', "M", {34}), std::string("22 00"));
    CHECK_EQ(command(r, 'R', "R"), std::string("00 00"));
    CHECK(mem(r, 34 * BLOCK, 128) == std::vector<uint8_t>(data.begin() + 5 * 128, data.begin() + 6 * 128));
    CHECK_EQ(command(r, 'V', "V"), std::string("22 00"));
}

TEST(dm_copy_and_dm_fill_move_60_kb_byte_for_byte_and_lose_no_tick)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    const std::vector<uint8_t> src = noise(61440, 3);
    r.poke(0x10000, src);
    uint32_t m0 = ms_count(r);
    uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x90000), w2(61440)})), std::string("00 00"));
    double secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    unsigned counted = ms_count(r) - m0;
    std::printf("  60 KB copied: %u ms counted, %.1f in the cycles (the command's line included)\n", counted, secs * 1000);
    CHECK(mem(r, 0x90000, 61440) == src); // across blocks 36-39
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    // Down over itself: exact.
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x90100), a3(0x90000), w2(4096)})), std::string("00 00"));
    CHECK(mem(r, 0x90000, 4096) == std::vector<uint8_t>(src.begin() + 256, src.begin() + 256 + 4096));
    // A fill of 60 KB, its neighbours untouched; and of one byte.
    r.poke(0xA0000 - 1, 0x11);
    r.poke(0xA0000 + 61440, 0x22);
    m0 = ms_count(r);
    c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_FILL, 0xA5}, a3(0xA0000), w2(61440)})), std::string("00 00"));
    secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    counted = ms_count(r) - m0;
    std::printf("  60 KB filled: %u ms counted, %.1f in the cycles\n", counted, secs * 1000);
    CHECK(mem(r, 0xA0000, 61440) == std::vector<uint8_t>(61440, 0xA5));
    CHECK_EQ(r.phys(0xA0000 - 1), 0x11);
    CHECK_EQ(r.phys(0xA0000 + 61440), 0x22);
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    CHECK_EQ(f83(r, cat({{DM_FILL, 0x3C}, a3(0xB0000), w2(1)})), std::string("00 00"));
    CHECK_EQ(r.phys(0xB0000), 0x3C);
    CHECK_EQ(r.phys(0xB0001), 0x00);
    // The console's font may be a source; units 0-12, OODOS's blocks and past $FFFFF are refused.
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x80400), a3(0xB1000), w2(4096)})), std::string("00 00"));
    CHECK(mem(r, 0xB1000, 4096) == mem(r, 0x80400, 4096));
    const std::vector<uint8_t> unit0 = mem(r, 0x80000, 0x1400); // the palette and the font (the text moves as it prints)
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x80000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x833FF), w2(1)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x7FFF0), w2(32)})), std::string("FF FF")); // OODOS's, and into unit 0
    CHECK_EQ(f83(r, cat({{DM_FILL, 0}, a3(0x82000), w2(1)})), std::string("FF FF"));
    CHECK(mem(r, 0x80000, 0x1400) == unit0);
    CHECK(mem(r, 0x832C0, 0x140) == std::vector<uint8_t>(0x140, 0)); // unit 12 past the text, never written
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x83400), w2(16)})), std::string("00 00")); // unit 13
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x7C000), w2(16)})), std::string("FF FF")); // block 31
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0xF0000), a3(0x90000), w2(16)})), std::string("FF FF")); // from block 60
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0xBFF00), w2(0x100)})), std::string("00 00")); // to bank B's end
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0xBFF00), w2(0x101)})), std::string("FF FF")); // into M:'s bank C
    CHECK_EQ(f83(r, cat({{DM_FILL, 0}, a3(0xFFF00), w2(0x200)})), std::string("FF FF")); // past $FFFFF
}

TEST(a_vertical_blank_slot_is_copied_only_in_the_blank_and_its_bit_cleared)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    const std::vector<uint8_t> src = noise(512, 9);
    r.poke(0x10000, src);
    const std::vector<uint8_t> before = mem(r, 0x90000, 512);
    // Each instruction looked at from before the slot is set: when the bytes change the beam must be in
    // the blank, lines 480-524, after the bit is set (as the program's TSB would), not in the BDOS's time.
    int line = -1;
    unsigned changed_at_frame = 0;
    bool bit = false, early = false;
    const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
    r.watch = [&](uint16_t) {
        if (line < 0 && r.phys(0x90000) == src[0] && r.phys(0x90000 + 511) == src[511])
        {
            line = static_cast<int>(vid(r).beam_line());
            changed_at_frame = r.phys(frames);
            early = !bit;
        }
    };
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 512)), std::string("00 00"));
    r.run(ms(40));
    CHECK(mem(r, 0x90000, 512) == before); // no bit set: nothing copied
    bit = true;
    r.poke(AREA + EV_VBGO, 0x01);
    const unsigned f0 = r.phys(frames);
    r.run(ms(40));
    r.watch = nullptr;
    CHECK(!early);
    std::printf("  the slot's 512 bytes changed at line %d, %u frame(s) after the bit\n", line, (changed_at_frame - f0) & 0xFF);
    CHECK(line >= 480 && line <= 524);
    CHECK(mem(r, 0x90000, 512) == src);
    CHECK_EQ(area(r, EV_VBGO), 0); // cleared when done
    // Four slots, the budget: 4 x 256; a fifth byte refused; a slot that isn't; 1,025; freed and set again.
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(2, 0x10200, 0x92000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 257)), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(4, 0x10300, 0x93000, 16)), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 0)), std::string("00 00")); // freed
    CHECK_EQ(f83(r, vbset(2, 0x10200, 0x92000, 512)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(2, 0x10200, 0x92000, 1025)), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x80000, 16)), std::string("FF FF")); // the console's units
    // A length whose sum with the others would wrap past 16 bits (review F1): refused, slot 0 as it was.
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 0)), std::string("00 00")); // room for slot 1's byte
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 1)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 0xFFFF)), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 0xFC00)), std::string("FF FF"));
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SLLEN")) | r.phys(SYS_BLOCK_PHYS + sym("SLLEN") + 1) << 8, 256u);
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 0)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(0, 0xF0000, 0x90000, 16)), std::string("FF FF")); // from OODOS's block
    r.poke(0x93000, std::vector<uint8_t>(256, 0));
    r.poke(0x92000, std::vector<uint8_t>(512, 0));
    r.poke(AREA + EV_VBGO, 0x0F); // all four bits, slot 1 free
    r.run(ms(40));
    CHECK_EQ(area(r, EV_VBGO), 0);
    CHECK(mem(r, 0x92000, 512) == mem(r, 0x10200, 512));
    CHECK(mem(r, 0x93000, 256) == mem(r, 0x10300, 256));
    // No area: nothing copied, whatever the slots.
    wboot(r);
    open(r, "X");
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x94000, 64)), std::string("00 00"));
    r.poke(0x94000, std::vector<uint8_t>(64, 0));
    r.run(ms(50));
    CHECK(mem(r, 0x94000, 64) == std::vector<uint8_t>(64, 0));
}

TEST(the_frame_costs_and_the_latest_a_slot_ends)
{
    CcpRig r;
    REQUIRE(r.ok);
    put_b(r, "PICTURE BIN", noise(65536, 2));
    // No area, at the prompt: the frame and the tick L3's.
    Costs none;
    time_interrupts(r, none, ms(300));
    Costs::print("no area, a frame", none.frame);
    Costs::print("no area, a tick", none.tick);
    CHECK_EQ(Costs::mode(none.tick), 145u);
    CHECK_EQ(Costs::mode(none.frame), 199u); // L3's to the cycle (main 0497570 measures 199 the same way)
    open(r, "2");
    Costs area2;
    time_interrupts(r, area2, ms(200));
    Costs::print("layout 2, no slot, a frame", area2.frame);
    CHECK_EQ(Costs::mode(area2.frame), 396u); // L3's 389, and the 7 of VBON's test
    const std::vector<uint8_t> src = noise(512, 4);
    r.poke(0x10000, src);
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 512)), std::string("00 00"));
    Costs set;
    time_interrupts(r, set, ms(200));
    Costs::print("layout 2, a slot set, no bit, a frame", set.frame);
    Costs slot;
    r.poke(AREA + EV_VBGO, 1);
    time_interrupts(r, slot, ms(300), [&] {
        if (!(r.phys(AREA + EV_VBGO) & 1))
            r.poke(AREA + EV_VBGO, 1);
    });
    Costs::print("layout 2, a 512-byte slot each frame, a frame", slot.frame);
    CHECK(Costs::mode(slot.frame) > 1024 && Costs::mode(slot.frame) < 1024 + 800);
    // The latest it ends: with a 16-event USB batch under way as the blank begins.
    UsbCard *usb = r.machine->usb_card();
    int latest = -1;
    unsigned landed = 0;
    bool armed = false;
    r.watch = [&](uint16_t) {
        const unsigned line = vid(r).beam_line();
        if (!armed && line == 470)
        {
            for (int k = 0; k < 16; ++k)
                usb->key(static_cast<uint8_t>(0xE0 + (k & 7)), k < 8); // modifiers: no character for the console
            armed = true;
        }
        if (line < 470)
            armed = false;
        if (!(r.phys(AREA + EV_VBGO) & 1))
        {
            ++landed;
            latest = std::max(latest, static_cast<int>(line));
            r.poke(AREA + EV_VBGO, 1);
        }
    };
    r.run(ms(200));
    r.watch = nullptr;
    std::printf("  with a 16-event USB batch at line 470: %u copies, the latest ended at line %d\n", landed, latest);
    CHECK(landed >= 10 && latest >= 480 && latest <= 524);
    // And with DM_LOAD's 4 KB pieces under way: three loads of 64 KB.
    std::vector<uint8_t> fcb(36, 0);
    fcb[0] = 2;
    const std::string n = "PICTURE BIN";
    for (unsigned i = 0; i < 11; ++i)
        fcb[1 + i] = static_cast<uint8_t>(n[i]);
    r.poke(0x2140, fcb);
    CHECK_EQ(command(r, 'O', "O"), std::string("00 00"));
    latest = -1;
    landed = 0;
    r.watch = [&](uint16_t) {
        if (!(r.phys(AREA + EV_VBGO) & 1))
        {
            ++landed;
            latest = std::max(latest, static_cast<int>(vid(r).beam_line()));
            r.poke(AREA + EV_VBGO, 1);
        }
    };
    for (int k = 0; k < 3; ++k)
        CHECK_EQ(f83(r, {1, 0x40, 0x21, 0, 0, 0, 0, 2, 0, 0, 0x0A}), std::string("00 00")); // 512 records to $A0000
    r.watch = nullptr;
    std::printf("  with 64 KB loads under way: %u copies, the latest ended at line %d\n", landed, latest);
    CHECK(landed >= 3 && latest >= 480 && latest <= 524);
}

TEST(the_full_budget_the_pointer_and_a_stream_end_inside_the_blank)
{
    // Review F8: all four slots, 256 bytes each (1,024 in all), the pointer on and a 44.1 kHz stream playing
    // (its fields written each frame), the latest a frame's copies end, with a USB batch or a 64 KB load under way.
    CcpRig r;
    REQUIRE(r.ok);
    put_b(r, "PICTURE BIN", noise(65536, 2));
    open(r, "2");
    r.poke(0x10000, noise(65536, 1));
    r.poke(0x3000, std::vector<uint8_t>{1, 2}); // function 82: claim the stream, start it, two 64 KB buffers (0.74 s)
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{7, 0, 0x44, 0xAC, 3});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{8, 0x00, 0x00, 0x01, 0x00, 0x00});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{8, 0x00, 0x00, 0x02, 0x00, 0x00});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("01 00"));
    for (uint8_t k = 0; k < 4; ++k)
        CHECK_EQ(f83(r, vbset(k, 0x10000 + k * 0x100u, 0x90000 + k * 0x1000u, 256)), std::string("00 00"));
    CHECK_EQ(f81(r, {IN_MOUSE, 1, 0x3F, 0x01, 0xEF, 0x00}), std::string("00 00"));
    vset(r, 0x03, 64);
    CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    Costs all;
    r.poke(AREA + EV_VBGO, 0x0F);
    time_interrupts(r, all, ms(120), [&] {
        if (!(r.phys(AREA + EV_VBGO) & 0x0F))
            r.poke(AREA + EV_VBGO, 0x0F);
    });
    Costs::print("four slots of 256, the pointer, the stream: a frame", all.frame);
    UsbCard *usb = r.machine->usb_card();
    int latest = -1;
    unsigned landed = 0;
    bool armed = false;
    auto track = [&](bool batch) {
        r.watch = [&, batch](uint16_t) {
            const unsigned line = vid(r).beam_line();
            if (batch && !armed && line == 470)
            {
                for (int k = 0; k < 16; ++k)
                    usb->key(static_cast<uint8_t>(0xE0 + (k & 7)), k < 8);
                armed = true;
            }
            if (line < 470)
                armed = false;
            if (!(r.phys(AREA + EV_VBGO) & 0x0F))
            {
                ++landed;
                latest = std::max(latest, static_cast<int>(line));
                r.poke(AREA + EV_VBGO, 0x0F);
            }
        };
    };
    track(true);
    r.run(ms(150));
    r.watch = nullptr;
    std::printf("  1,024 bytes, the pointer, the stream, a USB batch at 470: %u frames, the latest ended at line %d (%d to spare)\n",
                landed, latest, 524 - latest);
    CHECK(landed >= 8 && latest >= 480 && latest <= 524);
    const int usb_latest = latest;
    std::vector<uint8_t> fcb(36, 0);
    fcb[0] = 2;
    const std::string n = "PICTURE BIN";
    for (unsigned i = 0; i < 11; ++i)
        fcb[1 + i] = static_cast<uint8_t>(n[i]);
    r.poke(0x2140, fcb);
    CHECK_EQ(command(r, 'O', "O"), std::string("00 00"));
    latest = -1;
    landed = 0;
    track(false);
    for (int k = 0; k < 3; ++k)
        CHECK_EQ(f83(r, {1, 0x40, 0x21, 0, 0, 0, 0, 2, 0, 0, 0x0A}), std::string("00 00"));
    r.watch = nullptr;
    std::printf("  and under 64 KB loads: %u frames, the latest ended at line %d (%d to spare)\n", landed, latest, 524 - latest);
    CHECK(landed >= 3 && latest >= 480 && latest <= 524);
    CHECK(usb_latest <= 524);
    // The stream still playing at the measurement's end (128 KB of buffers: 0.74 s), and never dry (review R2).
    std::printf("  the stream at the end: state %u, %u frames played, underruns %u\n",
                r.machine->sound_card()->peek(0x50), r.phys(AREA + 100) | r.phys(AREA + 101) << 8 | r.phys(AREA + 102) << 16,
                r.phys(AREA + 106));
    CHECK_EQ(r.machine->sound_card()->peek(0x50), 2); // SOUND_STATE: playing
    CHECK_EQ(r.phys(AREA + 106), 0);                 // the stream's underruns
}

TEST(the_pointer_follows_the_mouse_less_its_hotspot)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "1");
    CHECK_EQ(f83(r, {DM_POINTER, 3, 2, 1}), std::string("FF FF")); // layout 1: no mouse in the area
    wboot(r);
    open(r, "2");
    CHECK_EQ(f81(r, {IN_MOUSE, 1, 0x3F, 0x01, 0xEF, 0x00}), std::string("00 00")); // absolute, 319 x 239
    vset(r, 0x03, 64);                                                              // SPRBASE: unit 64, $90000
    r.poke(0x90000 + 3 * 8, std::vector<uint8_t>(8, 0));
    CHECK_EQ(f83(r, {DM_POINTER, 3, 2, 1}), std::string("00 00"));
    CHECK_EQ(f83(r, {DM_POINTER, 64, 0, 0}), std::string("FF FF"));
    UsbCard *usb = r.machine->usb_card();
    usb->mouse_to(100, 50);
    r.run(ms(50));
    auto at = [&](unsigned b) { return r.phys(0x90000 + 3 * 8 + b); };
    CHECK_EQ(at(2) | (at(3) << 8), 98u);
    CHECK_EQ(at(4) | (at(5) << 8), 49u);
    CHECK_EQ(at(0), 0); // the program's bytes left alone
    usb->mouse_to(1, 0);
    r.run(ms(50));
    CHECK_EQ(at(2) | (at(3) << 8), 1023u); // 1 - 2, modulo 1,024
    CHECK_EQ(at(4) | (at(5) << 8), 1023u);
    Costs ptr;
    time_interrupts(r, ptr, ms(200));
    Costs::print("layout 2, the pointer on, a frame", ptr.frame);
    CHECK(Costs::mode(ptr.frame) > 396 && Costs::mode(ptr.frame) < 396 + 400);
    CHECK_EQ(command(r, 'I', "INFO").substr(0, 14), std::string("0D 04 0D040403")); // the pointer's sprite
    CHECK_EQ(f83(r, {DM_POINTER, 0xFF}), std::string("00 00"));
    usb->mouse_to(200, 100);
    r.run(ms(50));
    CHECK_EQ(at(2) | (at(3) << 8), 1023u); // off: not moved
    // A table at SPRBASE 0, in the console's units: never written there.
    const std::vector<uint8_t> pal = mem(r, 0x80000, 1024);
    vset(r, 0x03, 0);
    CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    r.run(ms(50));
    CHECK(mem(r, 0x80000, 1024) == pal);
}

TEST(the_warm_boot_puts_the_console_back_by_wboot_ctrl_c_and_break)
{
    auto junk = [](CcpRig &r) {
        vset(r, 0x00, 0x0F);       // CTRL: both layers and sprites
        vset(r, 0x01, 0x07);       // BORDER
        vset(r, 0x02, 20);         // PALBASE
        vset(r, 0x03, 64);         // SPRBASE
        vset(r, 0x10, 0x03);       // layer 0: bitmap 256
        vset(r, 0x11, 16);         //   its map
        vset(r, 0x13, 0x10);       //   scrolled
        vset(r, 0x20, 0x02);       // layer 1: tiles
        r.poke(0x10000, noise(64, 1));
        CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 64)), std::string("00 00"));
        CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    };
    auto console = [](CcpRig &r, const std::vector<uint8_t> &units) {
        CHECK_EQ(vreg(r, 0x00), 0x03);
        CHECK_EQ(vreg(r, 0x01), 0);
        CHECK_EQ(vreg(r, 0x02), 0);
        CHECK_EQ(vreg(r, 0x03), 0);
        CHECK_EQ(vreg(r, 0x10), 0x80);
        CHECK_EQ(vreg(r, 0x11), 8);
        CHECK_EQ(vreg(r, 0x12), 1);
        CHECK_EQ(vreg(r, 0x13), 0);
        CHECK_EQ(vreg(r, 0x20), 0);
        CHECK_EQ(vreg(r, 0x04), 0x01);       // IEN: the vertical blank alone
        CHECK_EQ(sys(r, "VBON"), 0);
        CHECK_EQ(sys(r, "PTRON"), 0);
        CHECK(mem(r, 0x80000, 0x1400) == units); // the palette and the font, as before the program
    };
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::vector<uint8_t> units = mem(r, 0x80000, 0x1400);
        open(r, "2");
        junk(r);
        wboot(r);
        console(r, units);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::vector<uint8_t> units = mem(r, 0x80000, 0x1400);
        open(r, "2");
        junk(r);
        const size_t from = r.out.size();
        r.poke(MBOX, 'D');
        CHECK(r.calls(CcpRig::C_READSTR_FN));
        r.type("\x03");
        CHECK(r.until("\r\nA>", ms(2000), from));
        console(r, units);
    }
    {
        // Break: the ROM's VSCREEN puts the card back itself; booting again starts afresh.
        CcpRig r;
        REQUIRE(r.ok);
        open(r, "2");
        junk(r);
        CHECK(sys(r, "VBON") != 0); // a slot and the pointer on: Break must stop them doing anything
        CHECK_EQ(sys(r, "PTRON"), 1);
        r.poke(0x90000, std::vector<uint8_t>(64, 0));
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(vreg(r, 0x00) & 0x08, 0); // sprites off
        CHECK_EQ(vreg(r, 0x04), 0);        // the card's interrupts off (the ROM's QUIET): no frame handler
        r.poke(AREA + EV_VBGO, 1);         // so a slot's bit set under the monitor copies nothing
        r.run(ms(60));
        CHECK(mem(r, 0x90000, 64) == std::vector<uint8_t>(64, 0));
        CHECK_EQ(r.machine->bus().peek(0xC417) & 1, 0); // channel 1 not running
        REQUIRE(r.boot());
        CHECK_EQ(sys(r, "VBON"), 0); // booting again starts afresh
        CHECK_EQ(sys(r, "PTRON"), 0);
    }
    {
        // DM_CONSOLE: the console's screen now, the program going on.
        CcpRig r;
        REQUIRE(r.ok);
        open(r, "X");
        vset(r, 0x00, 0x0F);
        vset(r, 0x10, 0x03);
        CHECK_EQ(f83op(r, DM_CONSOLE), std::string("00 00"));
        CHECK_EQ(vreg(r, 0x00), 0x03);
        CHECK_EQ(vreg(r, 0x10), 0x80);
        CHECK_EQ(command(r, 'F', "F"), std::string("07 0F"));
    }
}

int main() { return run_all(); }
