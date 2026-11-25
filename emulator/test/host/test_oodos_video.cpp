/**
 * @file test_oodos_video.cpp
 * @brief OOLIB's L4 (hw/oolib.md, "The plan"): function 83's DM_INFO, copies, fills, vertical-blank slots,
 * pointer and console; the W1 contract; and the warm boot's video part. On video card revision 2 (hw/video-rev2.md,
 * interface version 8): the slots' destinations and the pointer's table in the card's own memory, written through
 * its port; the card operations DM_VWRITE, DM_VREAD, DM_VFILL and DM_VCOPY; aperture slot 0 the console's. And
 * DM_VFILLW (interface version 13, hw/video640.md): a word's fill, two passes at PSTEP 2.
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
    constexpr uint8_t DM_INFO = 0, DM_COPY = 2, DM_FILL = 3, DM_VBSET = 4, DM_POINTER = 5, DM_CONSOLE = 6, DM_VWRITE = 7,
                      DM_VREAD = 8, DM_VFILL = 9, DM_VCOPY = 10, DM_VFILLW = 12, DM_OPS = 13;
    // backplane.inc's revision 2 registers, as offsets in the card's block
    constexpr uint8_t V_PALBASE = 0x0A, V_SPRBASE = 0x0C, V_L0 = 0x10, V_L1 = 0x20, V_SLOT0 = 0x30, V_PCTL = 0x3E;
    constexpr uint8_t IN_MOUSE = 4, MC_ALLOC = 55, MC_FREE = 57;
    constexpr uint16_t VIDEO = 0xC480;

    uint8_t area(Rig &r, unsigned off) { return r.phys(AREA + off); }
    uint8_t sys(Rig &r, const char *name) { return r.phys(SYS_BLOCK_PHYS + sym(name)); }
    VideoCard &vid(Rig &r) { return *r.machine->video_card(); }
    uint8_t vreg(Rig &r, uint8_t off) { return vid(r).peek(off); }
    void vset(Rig &r, uint8_t off, uint8_t v) { r.machine->bus().write(static_cast<uint16_t>(VIDEO + off), v); }
    void vset2(Rig &r, uint8_t off, unsigned v) { vset(r, off, static_cast<uint8_t>(v)), vset(r, off + 1, static_cast<uint8_t>(v >> 8)); }
    unsigned vreg2(Rig &r, uint8_t off) { return vreg(r, off) | vreg(r, off + 1) << 8; }
    /// The card's own memory (revision 2), as the tests read and write it behind the machine's back.
    std::vector<uint8_t> cmem(Rig &r, uint32_t at, size_t n)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = vid(r).card_peek(at + static_cast<uint32_t>(i));
        return b;
    }
    void cpoke(Rig &r, uint32_t at, const std::vector<uint8_t> &b)
    {
        for (size_t i = 0; i < b.size(); ++i)
            vid(r).card_poke(at + static_cast<uint32_t>(i), b[i]);
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
                else if (l == 0xFF && h == 0xDF) // the bridge's tick alone (N9)
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
    CHECK_EQ(command(r, 'F', "F"), std::string("12 2F")); // interface 18, areas frame, input, sound, DMA, serial
    CHECK_EQ(f83op(r, DM_INFO), std::string("20 04"));       // 2 MB of card memory, 4 slots
    // +1 32, +2 4 slots, +3 4 pages, +4 no pointer, +5-12 the map: block 1, block 33 (aperture slot 1's window,
    // the program's card bank) and 36-47 (banks 9-B), not 32, 34 or 35 (slots 0, 2 and 3's: OODOS's card banks,
    // review F2); +13-16 the program's card pages $0400-$1BFF
    CHECK_EQ(command(r, 'I', "INFO"), std::string("20 04 200404FF02000000F2FF00000004001C"));
    const std::string map = command(r, 'I', "INFO").substr(14, 16);
    const auto in_map = [&](unsigned block) {
        return (std::stoul(map.substr(block / 8 * 2, 2), nullptr, 16) >> (block & 7) & 1) != 0;
    };
    CHECK(in_map(1) && in_map(33) && in_map(36) && in_map(47));
    CHECK(!in_map(32) && !in_map(34) && !in_map(35)); // must fail if slots 2-3's windows are handed out
    CHECK(!in_map(0) && !in_map(2) && !in_map(31) && !in_map(48));
    // An allocation (55): the lowest free block, 4, joins the map; freed (57), it leaves it.
    r.poke(0x2150, std::vector<uint8_t>{0, 0, 0x00, 0x04, 0}); // 1,024 paragraphs
    CHECK_EQ(gcall(r, MC_ALLOC, 0x2150), std::string("00 00"));
    CHECK_EQ(r.phys(0x2151), 0x10); // M_BASE $1000: block 4
    CHECK_EQ(command(r, 'I', "INFO"), std::string("20 04 200404FF12000000F2FF00000004001C"));
    CHECK_EQ(gcall(r, MC_FREE, 0x2150), std::string("00 00"));
    CHECK_EQ(command(r, 'I', "INFO"), std::string("20 04 200404FF02000000F2FF00000004001C"));
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
    // The cold boot (which ends in the warm boot) leaves aperture slot 1 on the program's first card bank, 16
    // (review F2): its window, block 33, in W1, the CPU's writes land in the card's memory at $040000, never in
    // OODOS's part; and W1 is kept across function 9.
    CHECK_EQ(vreg2(r, V_SLOT0 + 2), 16u);
    CHECK_EQ(command(r, 'M', "M", {33}), std::string("21 00"));
    CHECK_EQ(command(r, 'P', "P", {0, 0x5A}), std::string("00 00"));
    CHECK(cmem(r, 0x040000, 256) == std::vector<uint8_t>(256, 0x5A));
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
    CHECK_EQ(vreg2(r, V_SLOT0 + 2), 16u); // the slot kept through it all
    // An allocated block (55's, block 4) in W1: written through it.
    r.poke(0x2150, std::vector<uint8_t>{0, 0, 0x00, 0x04, 0});
    CHECK_EQ(gcall(r, MC_ALLOC, 0x2150), std::string("00 00"));
    CHECK_EQ(command(r, 'M', "M", {4}), std::string("04 00"));
    CHECK_EQ(command(r, 'P', "P", {0, 0xC3}), std::string("00 00"));
    CHECK(mem(r, 4 * BLOCK, 256) == std::vector<uint8_t>(256, 0xC3));
    // The warm boot puts block 1 back, and the slots on banks 0, 16, 2 and 3 (slot 1 on the program's first
    // bank, review F2): the next program finds them there.
    vset2(r, V_SLOT0 + 2, 1); // (as a program that broke the contract would leave it: OODOS's bank 1)
    wboot(r);
    open(r, "X");
    CHECK_EQ(command(r, 'V', "V"), std::string("01 00"));
    for (unsigned n = 0; n < 4; ++n)
        CHECK_EQ(vreg2(r, static_cast<uint8_t>(V_SLOT0 + 2 * n)), n == 1 ? 16u : n);
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
    vset2(r, V_SLOT0 + 2, 20); // slot 1 on the program's card bank 20 ($050000), as GX_VMAP sets it
    CHECK_EQ(command(r, 'M', "M", {33}), std::string("21 00"));
    CHECK_EQ(command(r, 'R', "R"), std::string("00 00"));
    CHECK(cmem(r, 0x050000, 128) == std::vector<uint8_t>(data.begin() + 5 * 128, data.begin() + 6 * 128));
    CHECK_EQ(command(r, 'V', "V"), std::string("21 00"));
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
    // The console's font (through slot 0's window) may be a source; slot 0's window, OODOS's blocks and past
    // $FFFFF are refused.
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x80400), a3(0xB1000), w2(4096)})), std::string("00 00"));
    CHECK(mem(r, 0xB1000, 4096) == mem(r, 0x80400, 4096));
    const std::vector<uint8_t> unit0 = mem(r, 0x80000, 0x1400); // the palette and the font (the text moves as it prints)
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x80000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x833FF), w2(1)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x7FFF0), w2(32)})), std::string("FF FF")); // OODOS's, and into unit 0
    CHECK_EQ(f83(r, cat({{DM_FILL, 0}, a3(0x82000), w2(1)})), std::string("FF FF"));
    CHECK(mem(r, 0x80000, 0x1400) == unit0);
    CHECK(mem(r, 0x832C0, 0x140) == std::vector<uint8_t>(0x140, 0)); // unit 12 past the text, never written
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x83400), w2(16)})), std::string("FF FF")); // unit 13: slot 0's still
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x83FF0), w2(32)})), std::string("FF FF")); // from slot 0 into 1
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x84000), w2(16)})), std::string("00 00")); // slot 1's window
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x84000), w2(0x4000)})), std::string("00 00")); //   all of it
    // Slots 2 and 3's windows, $88000-$8FFFF, show OODOS's card banks 2 and 3 (review F2): refused, nothing
    // written there.
    const std::vector<uint8_t> banks23 = cmem(r, 0x008000, 0x8000);
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x88000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x8C000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x87FF0), w2(32)})), std::string("FF FF")); // from slot 1 into 2
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x84000), w2(0x4001)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x8FFF0), w2(32)})), std::string("FF FF")); // from slot 3 on
    CHECK_EQ(f83(r, cat({{DM_FILL, 0xE7}, a3(0x88000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_FILL, 0xE7}, a3(0x8C000), w2(1)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_FILL, 0xE7}, a3(0x8BFFF), w2(1)})), std::string("FF FF"));
    CHECK(cmem(r, 0x008000, 0x8000) == banks23);
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x90000), w2(16)})), std::string("00 00")); // bank 9: RAM
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x7C000), w2(16)})), std::string("FF FF")); // block 31
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0xF0000), a3(0x90000), w2(16)})), std::string("FF FF")); // from block 60
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0xBFF00), w2(0x100)})), std::string("00 00")); // to bank B's end
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0xBFF00), w2(0x101)})), std::string("FF FF")); // into M:'s bank C
    CHECK_EQ(f83(r, cat({{DM_FILL, 0}, a3(0xFFF00), w2(0x200)})), std::string("FF FF")); // past $FFFFF
}

TEST(the_card_operations_move_bytes_through_the_port_and_the_slots_and_refuse_what_isnt_the_programs)
{
    // Video card revision 2: DM_VWRITE, DM_VREAD and DM_VFILL through the card's port by channel 1, DM_VCOPY card
    // to card through slots 2 and 3; each byte for byte, across banks and pieces, with no tick lost.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    const std::vector<uint8_t> src = noise(61440, 5);
    r.poke(0x10000, src);
    cpoke(r, 0x043FFF, {0x77});
    cpoke(r, 0x043FFF + 1 + 61440, {0x88});
    uint32_t m0 = ms_count(r);
    uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x044000), w2(61440)})), std::string("00 00"));
    double secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    unsigned counted = ms_count(r) - m0;
    std::printf("  60 KB to the card: %u ms counted, %.1f in the cycles\n", counted, secs * 1000);
    CHECK(cmem(r, 0x044000, 61440) == src);
    CHECK_EQ(vid(r).card_peek(0x043FFF), 0x77);
    CHECK_EQ(vid(r).card_peek(0x044000 + 61440), 0x88);
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    CHECK_EQ(vreg(r, V_PCTL), 0); // the port's DRQ off after
    // Back from the card, unaligned, crossing a bank: what went there.
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x047F00), a3(0x30000), w2(5000)})), std::string("00 00"));
    CHECK(mem(r, 0x30000, 5000) == std::vector<uint8_t>(src.begin() + 0x3F00, src.begin() + 0x3F00 + 5000));
    // The console's font may be read: OODOS's card memory is a source.
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x000400), a3(0x31000), w2(4096)})), std::string("00 00"));
    CHECK(mem(r, 0x31000, 4096) == cmem(r, 0x000400, 4096));
    // A fill of 10,000 bytes from an odd address, its neighbours untouched; and one byte.
    cpoke(r, 0x060000, {0x11});
    cpoke(r, 0x060001 + 10000, {0x22});
    CHECK_EQ(f83(r, cat({{DM_VFILL, 0xA5}, a3(0x060001), w2(10000)})), std::string("00 00"));
    CHECK(cmem(r, 0x060001, 10000) == std::vector<uint8_t>(10000, 0xA5));
    CHECK_EQ(vid(r).card_peek(0x060000), 0x11);
    CHECK_EQ(vid(r).card_peek(0x060001 + 10000), 0x22);
    CHECK_EQ(f83(r, cat({{DM_VFILL, 0x3C}, a3(0x070000), w2(1)})), std::string("00 00"));
    CHECK_EQ(vid(r).card_peek(0x070000), 0x3C);
    CHECK_EQ(vid(r).card_peek(0x070001), 0x00);
    // Card to card: 40,000 bytes from an offset that isn't the destination's, so pieces end at either's bank;
    // the slots it borrows put back; then down over itself, exact.
    vset2(r, V_SLOT0 + 4, 50);
    vset2(r, V_SLOT0 + 6, 51);
    m0 = ms_count(r);
    c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x044123), a3(0x0A3F00), w2(40000)})), std::string("00 00"));
    secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    counted = ms_count(r) - m0;
    std::printf("  40,000 bytes card to card: %u ms counted, %.1f in the cycles\n", counted, secs * 1000);
    CHECK(cmem(r, 0x0A3F00, 40000) == std::vector<uint8_t>(src.begin() + 0x123, src.begin() + 0x123 + 40000));
    CHECK(counted + 1 >= static_cast<unsigned>(secs * 1000) && counted <= static_cast<unsigned>(secs * 1000) + 1);
    CHECK_EQ(vreg2(r, V_SLOT0 + 4), 50u);
    CHECK_EQ(vreg2(r, V_SLOT0 + 6), 51u);
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x0A4000), a3(0x0A3F00), w2(8192)})), std::string("00 00"));
    CHECK(cmem(r, 0x0A3F00, 8192) == std::vector<uint8_t>(src.begin() + 0x223, src.begin() + 0x223 + 8192));
    // Up over itself, 5 bytes on, 3,000 bytes across a 2 KB piece (review F10): each byte copied forward, as
    // DM_COPY is, so the first 5 repeat; checked byte for byte against a forward copy here.
    {
        cpoke(r, 0x0B0100, noise(3005, 11));
        std::vector<uint8_t> want = cmem(r, 0x0B0000, 0x1000);
        for (unsigned i = 0; i < 3000; ++i)
            want[0x105 + i] = want[0x100 + i];
        CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x0B0100), a3(0x0B0105), w2(3000)})), std::string("00 00"));
        CHECK(cmem(r, 0x0B0000, 0x1000) == want);
    }
    // The console's font copied into the program's card memory (as OOLIB's text does).
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x000400), a3(0x100000), w2(4096)})), std::string("00 00"));
    CHECK(cmem(r, 0x100000, 4096) == cmem(r, 0x000400, 4096));
    // Refusals, with nothing written: a card destination in OODOS's part, into Apple II mode's, past the fitted
    // memory; a source past it; a physical destination in slot 0's window or OODOS's blocks.
    // (OODOS's part but the text, which moves as the commands print)
    const std::vector<uint8_t> oodos = cat({cmem(r, 0x000000, 0x1400), cmem(r, 0x003400, 0x3CC00)}),
                               apple = cmem(r, 0x1BFF00, 0x200);
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x000000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x03FFF0), w2(32)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x1BFFF0), w2(32)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x1BFFF0), w2(16)})), std::string("00 00")); // its last bytes
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0xFFFFF0), w2(32)})), std::string("FF FF")); // wraps 24 bits
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0xF0000), a3(0x040000), w2(16)})), std::string("FF FF")); // from block 60
    CHECK_EQ(f83(r, cat({{DM_VFILL, 0}, a3(0x002000), w2(1)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VFILL, 0}, a3(0x1C0000), w2(1)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x040000), a3(0x000000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x1FFFF0), a3(0x040000), w2(32)})), std::string("FF FF"));
    // DM_VREAD's refusals: the physical destinations compared too, which is all a read could change (review
    // F10); the card's $040000 set apart from them first, so a read that went ahead would show.
    cpoke(r, 0x040000, std::vector<uint8_t>(32, 0x5D));
    cpoke(r, 0x1FFFF0, std::vector<uint8_t>(16, 0x6E));
    r.poke(0x30000, std::vector<uint8_t>(32, 0x00));
    const std::vector<uint8_t> at30000 = mem(r, 0x30000, 32), at7c000 = mem(r, 0x7C000, 16),
                               at88000 = mem(r, 0x88000, 16);
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x1FFFF0), a3(0x30000), w2(32)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x040000), a3(0x80000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x040000), a3(0x7C000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x040000), a3(0x88000), w2(16)})), std::string("FF FF")); // slot 2's window
    CHECK(mem(r, 0x30000, 32) == at30000);
    CHECK(mem(r, 0x7C000, 16) == at7c000);
    CHECK(mem(r, 0x88000, 16) == at88000);
    CHECK(cat({cmem(r, 0x000000, 0x1400), cmem(r, 0x003400, 0x3CC00)}) == oodos);
    CHECK(cmem(r, 0x1C0000, 0x100) == std::vector<uint8_t>(apple.begin() + 0x100, apple.end()));
    CHECK_EQ(f83op(r, DM_VWRITE), std::string("FF FF")); // a block needed
}

TEST(dm_vfillw_fills_a_word_pattern)
{
    // A direct 640 colour fill: three rows of 1,280 bytes of $0F00 (red) from row 12 of a screen at $040000, which
    // straddles a 16 KB slot; the bytes either side untouched. Then lengths that end on a low byte (7, 1) and
    // 65,536 (0), in many pieces; refused, nothing written, outside the program's part.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    const uint32_t row12 = 0x040000 + 12 * 1280;
    auto words = [](uint16_t w, size_t n) {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(i & 1 ? w >> 8 : w);
        return b;
    };
    cpoke(r, row12 - 1, {0x77});
    cpoke(r, row12 + 3840, {0x88});
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0x0F00), a3(row12), w2(3840)})), std::string("00 00"));
    CHECK(cmem(r, row12, 3840) == words(0x0F00, 3840));
    CHECK(row12 < 0x044000 && row12 + 1280 > 0x044000); // the row crosses the slot's end
    CHECK_EQ(vid(r).card_peek(row12 - 1), 0x77);
    CHECK_EQ(vid(r).card_peek(row12 + 3840), 0x88);
    CHECK(cmem(r, row12 + 3840, 64) == cat({{0x88}, std::vector<uint8_t>(63, 0)})); // nothing past it, odd or even
    cpoke(r, 0x050000, std::vector<uint8_t>(16, 0x99));
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0x1234), a3(0x050001), w2(7)})), std::string("00 00"));
    CHECK(cmem(r, 0x050000, 16) == std::vector<uint8_t>({0x99, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x99, 0x99,
                                                          0x99, 0x99, 0x99, 0x99, 0x99, 0x99}));
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0xABCD), a3(0x050009), w2(1)})), std::string("00 00"));
    CHECK(cmem(r, 0x050008, 3) == std::vector<uint8_t>({0x99, 0xCD, 0x99}));
    cpoke(r, 0x06FFFF, {0x55});
    cpoke(r, 0x080000, {0x66});
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0x8000), a3(0x070000), w2(0)})), std::string("00 00"));
    std::printf("  65,536 bytes by DM_VFILLW, the call and L4TEST's line: %llu cycles\n",
                static_cast<unsigned long long>(r.cpu->cycles() - c0));
    CHECK(cmem(r, 0x070000, 65536) == words(0x8000, 65536));
    CHECK_EQ(vid(r).card_peek(0x06FFFF), 0x55);
    CHECK_EQ(vid(r).card_peek(0x080000), 0x66);
    const std::vector<uint8_t> before = cmem(r, 0x03FF00, 0x200), end = cmem(r, 0x1BFF00, 0x200);
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0xFFFF), a3(0x03FFFE), w2(4)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0xFFFF), a3(0x1BFFFE), w2(4)})), std::string("FF FF"));
    CHECK(cmem(r, 0x03FF00, 0x200) == before && cmem(r, 0x1BFF00, 0x200) == end);
    CHECK_EQ(f83(r, cat({{DM_VFILLW}, w2(0x0F0F), a3(0x1BFFFC), w2(4)})), std::string("00 00")); // its last bytes
    CHECK(cmem(r, 0x1BFFFC, 4) == words(0x0F0F, 4));
    CHECK_EQ(f83op(r, DM_VFILLW), std::string("FF FF")); // a block needed
    CHECK_EQ(vreg(r, V_PCTL), 0);
}

TEST(op_13_is_refused)
{
    // DM_OPS is 13: operation 12 is DM_VFILLW, and 13 the first that isn't one, with a block or without.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    cpoke(r, 0x050000, std::vector<uint8_t>(16, 0));
    CHECK_EQ(f83op(r, 13), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{13}, w2(0x0F00), a3(0x050000), w2(16)})), std::string("FF FF"));
    CHECK(cmem(r, 0x050000, 16) == std::vector<uint8_t>(16, 0));
    CHECK_EQ(f83op(r, 0xFF), std::string("FF FF"));
}

TEST(a_card_transfer_that_doesnt_end_is_refused_and_the_next_goes_on)
{
    // Review F4: PORTGO stops channel 1 after 65,536 polls; the operation then answers $FF, not success. The
    // card's DRQ taken away behind OODOS's back just before the channel starts, so the channel never ends.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    r.poke(0x10000, noise(16, 13));
    cpoke(r, 0x050000, std::vector<uint8_t>(16, 0));
    const uint16_t en = static_cast<uint16_t>(sym("PORTGO") + 6); // its LDA #DMA_ENABLE, after STA MODE, STX PCTL
    bool hit = false;
    r.watch = [&](uint16_t pc) {
        if (!hit && pc == en && r.machine->bus().peek(pc) == 0xA9)
        {
            hit = true;
            vset(r, V_PCTL, 0);
        }
    };
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x050000), w2(16)})), std::string("FF FF"));
    r.watch = nullptr;
    CHECK(hit);
    CHECK(cmem(r, 0x050000, 16) == std::vector<uint8_t>(16, 0));
    CHECK_EQ(vreg(r, V_PCTL), 0);
    CHECK_EQ(r.machine->dma()->peek_channel(1, DmaController::CTRL) & 1, 0); // channel 1 stopped
    // The next goes on.
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x050000), w2(16)})), std::string("00 00"));
    CHECK(cmem(r, 0x050000, 16) == mem(r, 0x10000, 16));
}

TEST(the_card_operations_are_refused_without_the_dma_controller)
{
    // Review F4: as DM_VBSET is. backplane.machine less its DMA controller (test_oodos_utils_rom's way): the
    // card's sectors don't move, so the boot waits out SDIO's limits and L4TEST is put in memory, not loaded.
    std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
    const size_t at = text.find("\ndma ");
    REQUIRE(at != std::string::npos);
    text.erase(at + 1, text.find('\n', at + 1) - at);
    CcpRig r({}, text);
    if (!r.ok && r.out.find("OODOS ") != std::string::npos)
        r.ok = r.until("\r\nA>", ms(10000), r.out.size());
    REQUIRE(r.ok);
    REQUIRE(r.machine->bus().read(0xC47F) != 0x82); // DMA_ID: no controller answers
    open(r, "X");
    CHECK_EQ(f83op(r, DM_INFO), std::string("20 04")); // the card is there
    r.poke(0x10000, noise(16, 17));
    cpoke(r, 0x050000, std::vector<uint8_t>(16, 0));
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f83(r, cat({{DM_VWRITE}, a3(0x10000), a3(0x050000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VREAD}, a3(0x050000), a3(0x30000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VFILL, 0x5A}, a3(0x050000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x060000), a3(0x050000), w2(16)})), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x050000, 16)), std::string("FF FF"));
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / HZ;
    std::printf("  five refusals without the DMA controller: %.1f ms\n", secs * 1000);
    CHECK(secs < 0.1); // refused at once, not after 65,536 polls of a channel that isn't there
    CHECK(cmem(r, 0x050000, 16) == std::vector<uint8_t>(16, 0));
}

TEST(a_vertical_blank_slot_is_copied_only_in_the_blank_and_its_bit_cleared)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    const std::vector<uint8_t> src = noise(512, 9);
    r.poke(0x10000, src);
    const std::vector<uint8_t> before = cmem(r, 0x090000, 512); // the card's memory: the slots' destinations
    // Each instruction looked at from before the slot is set: when the bytes change the beam must be in
    // the blank, lines 480-524, after the bit is set (as the program's TSB would), not in the BDOS's time.
    int line = -1;
    unsigned changed_at_frame = 0;
    bool bit = false, early = false;
    const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
    r.watch = [&](uint16_t) {
        if (line < 0 && vid(r).card_peek(0x090000) == src[0] && vid(r).card_peek(0x090000 + 511) == src[511])
        {
            line = static_cast<int>(vid(r).beam_line());
            changed_at_frame = r.phys(frames);
            early = !bit;
        }
    };
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 512)), std::string("00 00"));
    r.run(ms(40));
    CHECK(cmem(r, 0x090000, 512) == before); // no bit set: nothing copied
    bit = true;
    r.poke(AREA + EV_VBGO, 0x01);
    const unsigned f0 = r.phys(frames);
    r.run(ms(40));
    r.watch = nullptr;
    CHECK(!early);
    std::printf("  the slot's 512 bytes changed at line %d, %u frame(s) after the bit\n", line, (changed_at_frame - f0) & 0xFF);
    CHECK(line >= 480 && line <= 524);
    CHECK(cmem(r, 0x090000, 512) == src);
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
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x000000, 16)), std::string("FF FF")); // OODOS's card memory
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x03FFF0, 32)), std::string("FF FF")); //   into the program's
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x1BFFF0, 32)), std::string("FF FF")); // into Apple II mode's
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x1FFFF0, 32)), std::string("FF FF")); // past the fitted memory
    // A length whose sum with the others would wrap past 16 bits (review F1): refused, slot 0 as it was.
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 0)), std::string("00 00")); // room for slot 1's byte
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 1)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 0xFFFF)), std::string("FF FF"));
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 0xFC00)), std::string("FF FF"));
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SLLEN")) | r.phys(SYS_BLOCK_PHYS + sym("SLLEN") + 1) << 8, 256u);
    CHECK_EQ(f83(r, vbset(1, 0x10100, 0x91000, 0)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(3, 0x10300, 0x93000, 256)), std::string("00 00"));
    CHECK_EQ(f83(r, vbset(0, 0xF0000, 0x90000, 16)), std::string("FF FF")); // from OODOS's block
    cpoke(r, 0x093000, std::vector<uint8_t>(256, 0));
    cpoke(r, 0x092000, std::vector<uint8_t>(512, 0));
    r.poke(AREA + EV_VBGO, 0x0F); // all four bits, slot 1 free
    r.run(ms(40));
    CHECK_EQ(area(r, EV_VBGO), 0);
    CHECK(cmem(r, 0x092000, 512) == mem(r, 0x10200, 512));
    CHECK(cmem(r, 0x093000, 256) == mem(r, 0x10300, 256));
    // No area: nothing copied, whatever the slots.
    wboot(r);
    open(r, "X");
    CHECK_EQ(f83(r, vbset(0, 0x10000, 0x94000, 64)), std::string("00 00"));
    cpoke(r, 0x094000, std::vector<uint8_t>(64, 0));
    r.run(ms(50));
    CHECK(cmem(r, 0x094000, 64) == std::vector<uint8_t>(64, 0));
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
    CHECK_EQ(Costs::mode(none.tick), 142u); // the bridge's tick (N10; 162 with timer 1's)
    CHECK_EQ(Costs::mode(none.frame), 231u); // 215 before N9: the long way's test of IRQH bit 5 (the tick) added
    open(r, "2");
    Costs area2;
    time_interrupts(r, area2, ms(200));
    Costs::print("layout 2, no slot, a frame", area2.frame);
    CHECK_EQ(Costs::mode(area2.frame), 447u); // 412 before N9: the tick's test on the long way and IRQOTH's (N9, N10)
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
    CcpRig r({}, machine_with_sound(3)); // sound card revision 3, the card as a device
    REQUIRE(r.ok);
    put_b(r, "PICTURE BIN", noise(65536, 2));
    open(r, "2");
    r.poke(0x10000, noise(65536, 1));
    r.poke(0x3000, std::vector<uint8_t>{1, 2}); // function 82: claim the stream, start it, two 64 KB buffers (0.74 s)
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{12, 0, 0x44, 0xAC, 3});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{13, 0x00, 0x00, 0x01, 0x00, 0x00});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("00 00"));
    r.poke(0x3000, std::vector<uint8_t>{13, 0x00, 0x00, 0x02, 0x00, 0x00});
    CHECK_EQ(gcall(r, 82, 0x3000), std::string("01 00"));
    for (uint8_t k = 0; k < 4; ++k)
        CHECK_EQ(f83(r, vbset(k, 0x10000 + k * 0x100u, 0x90000 + k * 0x1000u, 256)), std::string("00 00"));
    CHECK_EQ(f81(r, {IN_MOUSE, 1, 0x3F, 0x01, 0xEF, 0x00}), std::string("00 00"));
    vset2(r, V_SPRBASE, 0x0A00); // the table at card $0A0000
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
                r.phys(AREA + 96), r.phys(AREA + 100) | r.phys(AREA + 101) << 8 | r.phys(AREA + 102) << 16,
                r.phys(AREA + 106));
    CHECK_EQ(r.phys(AREA + 96), 2); // EV_SSTATE: playing
    CHECK_EQ(r.phys(AREA + 106), 0);                 // the stream's underruns
}

TEST(dm_pointer_full_sets_byte_3_bit_7)
{
    // DM_POINTER's 64 + the sprite (interface version 14, hw/sprites640.md): each frame the pointer's entry gets
    // its X and Y with byte 3's bit 7 (FULL) set, in the program's own units, here 640 x 480 (the mouse at
    // 639 x 479). DM_POINTER 3 again: the bit clear. DM_INFO's +4 says sprite 3 either way; 128 is refused.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f81(r, {IN_MOUSE, 1, 0x7F, 0x02, 0xDF, 0x01}), std::string("00 00")); // absolute, 639 x 479
    vset2(r, V_SPRBASE, 0x0900);                                                    // SPRBASE: card $090000
    cpoke(r, 0x090000, std::vector<uint8_t>(64 * 8, 0));
    CHECK_EQ(f83(r, {DM_POINTER, 64 + 3, 0, 0}), std::string("00 00"));
    UsbCard *usb = r.machine->usb_card();
    usb->mouse_to(500, 400);
    r.run(ms(50));
    auto entry = [&](unsigned n) { return cmem(r, 0x090000 + n * 8 + 2, 4); };
    const std::vector<uint8_t> full{0xF4, 0x81, 0x90, 0x01}, doubled{0xF4, 0x01, 0x90, 0x01}, none(4, 0);
    CHECK(entry(3) == full);
    for (int f = 0; f < 3; ++f) // each frame
    {
        cpoke(r, 0x090000 + 3 * 8 + 2, none);
        r.run(ms(17));
        CHECK(entry(3) == full);
    }
    CHECK(entry(0) == none); // no other entry written
    CHECK_EQ(command(r, 'I', "INFO").substr(0, 14), std::string("20 04 20040403"));
    CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    r.run(ms(50));
    CHECK(entry(3) == doubled);
    CHECK_EQ(command(r, 'I', "INFO").substr(0, 14), std::string("20 04 20040403"));
    CHECK_EQ(f83(r, {DM_POINTER, 128, 0, 0}), std::string("FF FF"));
    CHECK_EQ(f83(r, {DM_POINTER, 254, 0, 0}), std::string("FF FF"));
    r.run(ms(50));
    CHECK(entry(3) == doubled); // refused: the pointer as it was
    CHECK_EQ(f83(r, {DM_POINTER, 64 + 63, 0, 0}), std::string("00 00")); // the last: sprite 63, full
    r.run(ms(50));
    CHECK(entry(63) == full);
}

TEST(the_warm_boot_and_dm_console_clear_sprctl)
{
    // SPRCTL ($C48E, hw/sprites640.md) set by a program is 0 again after the warm boot, and after DM_CONSOLE, so
    // the next program's sprites (and whatever its table's byte 3 holds) show doubled until it sets it.
    CcpRig r;
    REQUIRE(r.ok);
    CHECK_EQ(vreg(r, 0x0E), 0x00);
    open(r, "X");
    vset(r, 0x0E, 0x01);
    CHECK_EQ(vreg(r, 0x0E), 0x01);
    wboot(r);
    CHECK_EQ(vreg(r, 0x0E), 0x00);
    open(r, "X");
    vset(r, 0x0E, 0x01);
    CHECK_EQ(f83op(r, DM_CONSOLE), std::string("00 00"));
    CHECK_EQ(vreg(r, 0x0E), 0x00);
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
    vset2(r, V_SPRBASE, 0x0900);                                                    // SPRBASE: card $090000
    cpoke(r, 0x090000 + 3 * 8, std::vector<uint8_t>(8, 0));
    CHECK_EQ(f83(r, {DM_POINTER, 3, 2, 1}), std::string("00 00"));
    CHECK_EQ(f83(r, {DM_POINTER, 128, 0, 0}), std::string("FF FF")); // 128-254 refused
    CHECK_EQ(f83(r, {DM_POINTER, 64, 0, 0}), std::string("00 00"));  // taken since interface 14: sprite 0, full resolution
    CHECK_EQ(f83(r, {DM_POINTER, 3, 2, 1}), std::string("00 00"));   // sprite 3 again
    UsbCard *usb = r.machine->usb_card();
    usb->mouse_to(100, 50);
    r.run(ms(50));
    auto at = [&](unsigned b) { return vid(r).card_peek(0x090000 + 3 * 8 + b); };
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
    CHECK_EQ(command(r, 'I', "INFO").substr(0, 14), std::string("20 04 20040403")); // the pointer's sprite
    CHECK_EQ(f83(r, {DM_POINTER, 0xFF}), std::string("00 00"));
    usb->mouse_to(200, 100);
    r.run(ms(50));
    CHECK_EQ(at(2) | (at(3) << 8), 1023u); // off: not moved
    // A table at SPRBASE 0, in OODOS's card memory (the console's palette), or in Apple II mode's: never
    // written there.
    const std::vector<uint8_t> pal = cmem(r, 0x000000, 1024);
    vset2(r, V_SPRBASE, 0);
    CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    r.run(ms(50));
    CHECK(cmem(r, 0x000000, 1024) == pal);
    const std::vector<uint8_t> apple = cmem(r, 0x1C0000, 1024);
    vset2(r, V_SPRBASE, 0x1C00);
    r.run(ms(50));
    CHECK(cmem(r, 0x1C0000, 1024) == apple);
    // SPRBASE on the program's last page, $1BFF (review F3): sprite 31's entry is in that page and written,
    // sprite 63's is at $1C00F8, Apple II mode's, and never written: the entry's own address is checked.
    vset2(r, V_SPRBASE, 0x1BFF);
    cpoke(r, 0x1BFF00 + 31 * 8, std::vector<uint8_t>(8, 0));
    CHECK_EQ(f83(r, {DM_POINTER, 31, 0, 0}), std::string("00 00"));
    r.run(ms(50));
    CHECK_EQ(vid(r).card_peek(0x1BFF00 + 31 * 8 + 2) | vid(r).card_peek(0x1BFF00 + 31 * 8 + 3) << 8, 200u);
    CHECK_EQ(vid(r).card_peek(0x1BFF00 + 31 * 8 + 4) | vid(r).card_peek(0x1BFF00 + 31 * 8 + 5) << 8, 100u);
    CHECK_EQ(f83(r, {DM_POINTER, 63, 0, 0}), std::string("00 00"));
    r.run(ms(50));
    CHECK(cmem(r, 0x1C0000, 1024) == apple); // must fail if only SPRBASE's start is checked
}

TEST(the_warm_boot_puts_the_console_back_by_wboot_ctrl_c_and_break)
{
    auto junk = [](CcpRig &r) {
        vset(r, 0x00, 0x0F);       // CTRL: both layers and sprites
        vset(r, 0x01, 0x07);       // BORDER
        vset2(r, V_PALBASE, 0x0500); // PALBASE
        vset2(r, V_SPRBASE, 0x0900); // SPRBASE
        vset(r, V_L0, 0x03);         // layer 0: bitmap 256
        vset2(r, V_L0 + 1, 0x0600);  //   its map
        vset2(r, V_L0 + 3, 0x0700);  //   its tiles
        vset(r, V_L0 + 5, 0x10);     //   scrolled
        vset(r, V_L0 + 9, 3);        //   PALOFS
        vset(r, V_L1, 0x02);         // layer 1: tiles
        vset2(r, V_SLOT0 + 2, 40);   // aperture slots 1-3 on the program's banks
        vset2(r, V_SLOT0 + 4, 41);
        vset2(r, V_SLOT0 + 6, 42);
        r.poke(0x10000, noise(64, 1));
        CHECK_EQ(f83(r, vbset(0, 0x10000, 0x90000, 64)), std::string("00 00"));
        CHECK_EQ(f83(r, {DM_POINTER, 3, 0, 0}), std::string("00 00"));
    };
    auto console = [](CcpRig &r, const std::vector<uint8_t> &units) {
        CHECK_EQ(vreg(r, 0x00), 0x03);
        CHECK_EQ(vreg(r, 0x01), 0);
        CHECK_EQ(vreg2(r, V_PALBASE), 0u);
        CHECK_EQ(vreg2(r, V_SPRBASE), 0u);
        CHECK_EQ(vreg(r, V_L0), 0x80);
        CHECK_EQ(vreg2(r, V_L0 + 1), 0x20u); // the text at card $002000
        CHECK_EQ(vreg2(r, V_L0 + 3), 0x04u); // the font at card $000400
        CHECK_EQ(vreg2(r, V_L0 + 5), 0u);
        CHECK_EQ(vreg2(r, V_L0 + 7), 0u);
        CHECK_EQ(vreg(r, V_L0 + 9), 0);
        CHECK_EQ(vreg(r, V_L1), 0);
        for (unsigned n = 0; n < 4; ++n) // slot n on card bank n but slot 1, on the program's first (review F2)
            CHECK_EQ(vreg2(r, static_cast<uint8_t>(V_SLOT0 + 2 * n)), n == 1 ? 16u : n);
        CHECK_EQ(vreg(r, V_PCTL), 0);
        CHECK_EQ(vreg(r, 0x04), 0x01);       // IEN: the vertical blank alone
        CHECK_EQ(sys(r, "VBON"), 0);
        CHECK_EQ(sys(r, "PTRON"), 0);
        CHECK(cmem(r, 0x000000, 0x1400) == units); // the palette and the font, as before the program
    };
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::vector<uint8_t> units = cmem(r, 0x000000, 0x1400);
        open(r, "2");
        junk(r);
        wboot(r);
        console(r, units);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::vector<uint8_t> units = cmem(r, 0x000000, 0x1400);
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
        cpoke(r, 0x090000, std::vector<uint8_t>(64, 0));
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(vreg(r, 0x00) & 0x08, 0); // sprites off
        CHECK_EQ(vreg(r, 0x04), 0);        // the card's interrupts off (the ROM's QUIET): no frame handler
        r.poke(AREA + EV_VBGO, 1);         // so a slot's bit set under the monitor copies nothing
        r.run(ms(60));
        CHECK(cmem(r, 0x090000, 64) == std::vector<uint8_t>(64, 0));
        CHECK_EQ(r.machine->dma()->peek_channel(1, DmaController::CTRL) & 1, 0); // channel 1 not running
        REQUIRE(r.boot());
        CHECK_EQ(sys(r, "VBON"), 0); // booting again starts afresh
        CHECK_EQ(sys(r, "PTRON"), 0);
        CHECK_EQ(vreg2(r, V_SLOT0 + 2), 16u); // the cold boot's slot 1: the program's first card bank
    }
    {
        // DM_CONSOLE: the console's screen now, the program going on.
        CcpRig r;
        REQUIRE(r.ok);
        open(r, "X");
        vset(r, 0x00, 0x0F);
        vset(r, V_L0, 0x03);
        vset2(r, V_L0 + 1, 0x0600);
        vset2(r, V_SLOT0 + 2, 40);
        CHECK_EQ(f83op(r, DM_CONSOLE), std::string("00 00"));
        CHECK_EQ(vreg(r, 0x00), 0x03);
        CHECK_EQ(vreg(r, V_L0), 0x80);
        CHECK_EQ(vreg2(r, V_L0 + 1), 0x20u);
        CHECK_EQ(vreg2(r, V_SLOT0 + 2), 40u); // the program's slot left as it is: it goes on
        CHECK_EQ(command(r, 'F', "F"), std::string("12 2F"));
    }
}

TEST(with_map_0_the_console_and_dm_vcopy_use_the_aperture_at_e00000_and_leave_bank_8_ram_alone)
{
    // O5 (B7, b7-plan.md §4): OODOS reaches slot 0's window (the console's text) by WIN1H $0E and block 0, and
    // DM_VCOPY slots 2 and 3's windows at $E08000 and $E0C000 (crt.s's CVA), never bank 8. With MAP forced to 0
    // (AP8 clear: bank 8 is the RAM card's), the console still prints, scrolls and erases on the card, DM_VCOPY
    // copies right, and bank 8's RAM keeps its pattern.
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(r.bridge().id() == 0xD3);
    open(r, "X");
    const std::vector<uint8_t> ram = noise(0x10000, 7);
    r.machine->bus().write(0xC6A5, 0); // MAP: AP8 clear
    REQUIRE(r.bridge().map() == 0);
    for (uint32_t i = 0; i < ram.size(); ++i)
        r.machine->bus().write_bus(0x80000 + i, ram[i]);
    // 64 lines from L4TEST (more than twice the screen's 30 rows, so it scrolls at least 30 times, each scroll
    // erasing the bottom row), each a DM_VCOPY of 1,000 bytes card to card, from a fresh place each time. Every
    // row's right half is junk first, so each row is clean after only if it was erased before its line went in.
    for (unsigned n = 0; n < 30; ++n)
        for (unsigned c = 40; c < 80; ++c)
            vid(r).card_poke(0x002000 + n * 160 + c * 2, '#');
    const std::vector<uint8_t> src = noise(64 * 1000, 9);
    cpoke(r, 0x060000, src);
    for (unsigned i = 0; i < 64; ++i)
        CHECK_EQ(f83(r, cat({{DM_VCOPY}, a3(0x060000 + i * 1000), a3(0x0A0000 + i * 1000), w2(1000)})),
                 std::string("00 00"));
    CHECK(cmem(r, 0x0A0000, src.size()) == src);
    CHECK_EQ(r.bridge().map(), 0); // no warm boot in between
    std::vector<uint8_t> now(ram.size());
    for (uint32_t i = 0; i < ram.size(); ++i)
        now[i] = r.machine->bus().peek_bus(0x80000 + i);
    CHECK(now == ram);
    // The card's text: every row one of L4TEST's K lines ("\r\nK 00 00", the last on the bottom row), the junk
    // scrolled off or erased.
    auto row = [&](unsigned n) {
        std::string s;
        for (unsigned c = 0; c < 80; ++c)
            s += static_cast<char>(vid(r).card_peek(0x002000 + n * 160 + c * 2));
        return s.substr(0, s.find_last_not_of(' ') + 1);
    };
    for (unsigned n = 0; n < 30; ++n)
        CHECK_EQ(row(n), std::string("K 00 00"));
}

// --- B7: function 83's DM_BANK8 (b7-plan.md §2.3, §4 D1-D5; proposed) ------------------------------------------

namespace
{
    constexpr uint8_t DM_BANK8 = 11, DM_NONE = 13; // oodos.inc's DM_BANK8, since interface 12; 13 its DM_OPS since 13
    constexpr uint16_t NB_MAPREG = 0xC6A5;

    std::string bank8(CcpRig &r) { return f83(r, {DM_BANK8, 0xEE, 0xEE}); }
    /// The block's +1 and +2 after a DM_BANK8.
    std::string bank8_block(Rig &r) { return hex(r.phys(KBLK + 1), 2) + hex(r.phys(KBLK + 2), 2); }
    std::vector<uint8_t> bus_bytes(Rig &r, uint32_t at, size_t n)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = r.machine->bus().peek_bus(at + static_cast<uint32_t>(i));
        return b;
    }
    void put_bus(Rig &r, uint32_t at, const std::vector<uint8_t> &b)
    {
        for (size_t i = 0; i < b.size(); ++i)
            r.machine->bus().write_bus(at + static_cast<uint32_t>(i), b[i]);
    }
    void romver(Rig &r, uint8_t major, uint8_t minor)
    {
        r.poke(SYS_BLOCK_PHYS + sym("ROMVER"), major);
        r.poke(SYS_BLOCK_PHYS + sym("ROMVER") + 1, minor);
    }
}

TEST(dm_bank8_makes_bank_8_the_programs_ram_and_gives_slot_1_at_e04000)
{
    // D1. With the revision 2 video card: A = 0, +1/+2 WIN1H $0E and block 1 (slot 1's window at $E04000), MAP
    // 0. Then W1 on block 32 writes bank 8's RAM (read back by the backplane's own view), not the card; DM_INFO's
    // map takes in blocks 32-39 (its byte 4 $FF); and DM_COPY to $84000 lands in RAM, the card's slot 1 bank
    // untouched.
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(r.bridge().id() == 0xD3);
    open(r, "X");
    REQUIRE(r.bridge().map() == 1);
    put_bus(r, 0x80000, std::vector<uint8_t>(0x100, 0x11));
    const std::vector<uint8_t> console = cmem(r, 0x000000, 0x100), prog = cmem(r, 0x040000, 64);
    CHECK_EQ(bank8(r), std::string("00 00"));
    CHECK_EQ(bank8_block(r), std::string("0E01"));
    CHECK_EQ(r.bridge().map(), 0);
    CHECK_EQ(command(r, 'I', "INFO"), std::string("20 04 200404FF02000000FFFF00000004001C"));
    CHECK_EQ(command(r, 'M', "M", {32}).substr(0, 2), std::string("20")); // W1 on block 32
    CHECK_EQ(command(r, 'P', "P", {0, 0xD1}), std::string("00 00"));
    CHECK(bus_bytes(r, 0x80000, 0x100) == std::vector<uint8_t>(0x100, 0xD1));
    CHECK(cmem(r, 0x000000, 0x100) == console);
    command(r, 'B', "B");
    const std::vector<uint8_t> src = noise(64, 21);
    r.poke(0x10000, src);
    CHECK_EQ(f83(r, cat({{DM_COPY}, a3(0x10000), a3(0x84000), w2(64)})), std::string("00 00"));
    CHECK(bus_bytes(r, 0x84000, 64) == src);
    CHECK(cmem(r, 0x040000, 64) == prog);
}

TEST(dm_bank8_with_no_revision_2_video_card_succeeds_with_no_window)
{
    // D2. No revision 2 card (revision 1's): the ROM BIOS left AP8 clear, bank 8 RAM from the boot; DM_BANK8 still
    // succeeds, with +1/+2 0 and 0 (no aperture to map).
    CcpRig r({}, {}, {}, 1);
    REQUIRE(r.ok);
    open(r, "X");
    REQUIRE(r.bridge().map() == 0);
    CHECK_EQ(bank8(r), std::string("00 00"));
    CHECK_EQ(bank8_block(r), std::string("0000"));
    CHECK_EQ(r.bridge().map(), 0);
}

TEST(dm_bank8_is_refused_under_a_rom_before_0_17)
{
    // D3. A ROM BIOS older than 0.17 (OODOS's record of it, cold.s's ROMVER, made 0.16) would draw the monitor's
    // screen through bank 8 on a Break: $FF $FF, MAP still 1. One of 1.0 is newer (the major first), and $FF $FF
    // (not found) is refused.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    romver(r, 0, 16);
    CHECK_EQ(bank8(r), std::string("FF FF"));
    CHECK_EQ(bank8_block(r), std::string("EEEE"));
    CHECK_EQ(r.bridge().map(), 1);
    romver(r, 0xFF, 0xFF);
    CHECK_EQ(bank8(r), std::string("FF FF"));
    CHECK_EQ(r.bridge().map(), 1);
    romver(r, 1, 0);
    CHECK_EQ(bank8(r), std::string("00 00"));
    CHECK_EQ(r.bridge().map(), 0);
}

TEST(the_warm_boot_ends_dm_bank8)
{
    // D4. After DM_BANK8 a DM_COPY to $80000 (slot 0's window before) is RAM and allowed; after the warm boot
    // MAP is 1 again, the same copy is refused, and DM_INFO's map is as before.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    CHECK_EQ(bank8(r), std::string("00 00"));
    const std::vector<uint8_t> src = noise(16, 23);
    r.poke(0x10000, src);
    const std::vector<uint8_t> copy = cat({{DM_COPY}, a3(0x10000), a3(0x80000), w2(16)});
    CHECK_EQ(f83(r, copy), std::string("00 00"));
    CHECK(bus_bytes(r, 0x80000, 16) == src);
    wboot(r);
    CHECK_EQ(r.bridge().map(), 1);
    open(r, "X");
    const std::vector<uint8_t> console = cmem(r, 0x000000, 16);
    CHECK_EQ(f83(r, copy), std::string("FF FF"));
    CHECK(cmem(r, 0x000000, 16) == console);
    CHECK_EQ(command(r, 'I', "INFO"), std::string("20 04 200404FF02000000F2FF00000004001C"));
}

TEST(dm_bank8_needs_a_block_and_is_the_last_operation)
{
    // D5. Operation 11 with X = 0 (no block for +1 and +2): refused, MAP kept; operation 13 isn't one, with a block
    // or without (12 is DM_VFILLW since interface 13, hw/video640.md).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    CHECK_EQ(f83op(r, DM_BANK8), std::string("FF FF"));
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(f83op(r, DM_NONE), std::string("FF FF"));
    CHECK_EQ(f83(r, {DM_NONE, 0xEE, 0xEE}), std::string("FF FF"));
    CHECK_EQ(r.bridge().map(), 1);
    CHECK_EQ(r.machine->bus().peek(NB_MAPREG), 1);
}

int main() { return run_all(); }
