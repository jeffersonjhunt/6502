/**
 * @file test_oodos_rsx.cpp
 * @brief OODOS's RSXs and loader, phase 5a (hw/oodos.md, "The RSXs, phase 5"), on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), and
 * oodos/sys's test programs put on M: behind OODOS's back: RSXQ
 * (oodos/sys/test/rsxq.s), which does what its tail says, plain and with the
 * test RSXs (test/trsx.inc) attached by tools/gencom.py; OVLT, which calls
 * function 59 for an overlay; the overlays. What the loader leaves in
 * memory is compared with mkprl.py's load of the same file (the Makefile's
 * NAME.PAGE.img), byte for byte, but for the prefix's fields the loader
 * writes and the RSX's own data.
 *
 *   - one, two and stacked RSXs: their pages, $0204, offsets 9 and 12, the
 *     serial number, the loader flag, the remove flag as the file has it;
 *     calls down the chain in order; the CCP's calls through them;
 *   - zero page slices, given, relocated and freed, and a 17th byte refused;
 *     the 15 the table holds and a 16th refused; a program that fits alone
 *     but not with its RSXs; RSXs that a refusal had linked taken out again;
 *   - non-banked RSXs passed over (the table's flag, and the prefix's);
 *   - removal at the warm boot: the lowest, the highest, the middle; the TPA's
 *     top rising only from the bottom; an RSX-only .COM's RSXs kept for the
 *     next program, then removed; 59 removing flagged RSXs, and with 0;
 *   - function 0 once at every end of a program; a $0203 a program
 *     overwrote mended; the SCB's $19 (the CCP running, its command count);
 *   - SCB entries; an empty .COM; GO checked against the lowest RSX;
 *   - 59 by a program: overlays at two pages, every refusal and its codes,
 *     the caller's DMA address and segment, error mode and multi-sector
 *     count kept, R0-R2 the address again; 60 with and without an RSX; 47;
 *   - a BDOS call's cost through no, one and three RSXs.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;

namespace
{
    using Ccp = oodos_rig::ccp::CcpRig;
    using namespace oodos_rig::ccp;

    constexpr uint32_t RES_PHYS = 62u * 0x4000u; ///< block 62, the CCP's and the loader's

    std::vector<uint8_t> built(const std::string &name)
    {
        std::vector<uint8_t> b = read_bytes(SYS + "/" + name);
        CHECK(!b.empty());
        if (b.empty())
            std::printf("  %s/%s: not built\n", SYS.c_str(), name.c_str());
        return b;
    }
    /// A file of the build on M:, in user 0.
    MFile on_m(const std::string &name83, const std::string &file) { return {0, name83, built(file)}; }

    /// The loader's state, in block 62.
    uint8_t res(const Ccp &r, const std::string &label, unsigned i = 0)
    {
        return r.phys(RES_PHYS + sym(label) - 0xC000u + i);
    }
    unsigned nrsx(const Ccp &r) { return res(r, "NRSX"); }
    unsigned word(const Ccp &r, uint16_t a) { return r.phys(a) | r.phys(a + 1u) << 8; }
    unsigned top(const Ccp &r) { return word(r, 0x0204); }

    /// `got` holds `want`, both shown if not.
    void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }
    void hasnt(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) == std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  don't want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }

    /// The RSX at `page` against mkprl.py's load of its file there (`img`): every byte but the prefix's
    /// serial number, offset 9's operand, offset 12 and the loader flag, which the loader writes, and its
    /// last three, its data (trsx.inc's). Then those the loader writes: 107's serial number, the loader
    /// flag 0, `next` and `prev`.
    void rsx_at(const Ccp &r, unsigned page, const std::string &img, unsigned next, unsigned prev)
    {
        const std::vector<uint8_t> want = built(img);
        const uint32_t base = page << 8;
        std::vector<bool> skip(want.size(), false); // trsx.inc's LDA $FFFF: its operand the RSX's own
        for (unsigned i = 0; i + 2 < want.size(); ++i)
            if (want[i] == 0xAD && want[i + 1] == 0xFF && want[i + 2] == 0xFF)
                skip[i + 1] = skip[i + 2] = true;
        unsigned differ = 0;
        for (unsigned i = 0; i + 3 < want.size(); ++i)
        {
            if (i < 6 || i == 10 || i == 11 || i == 12 || i == 13 || i == 24 || skip[i])
                continue;
            if (r.phys(base + i) != want[i] && differ++ < 4)
                std::printf("  $%04X: %02X, mkprl.py %02X\n", base + i, r.phys(base + i), want[i]);
        }
        CHECK_EQ(differ, 0u);
        const uint8_t serial[] = {'O', 'O', 'D', 0, 0, 1};
        for (unsigned i = 0; i < 6; ++i)
            CHECK_EQ(r.phys(base + i), serial[i]);
        CHECK_EQ(r.phys(base + 24), 0);
        CHECK_EQ(r.phys(base + 9), 0x4C);
        CHECK_EQ(word(r, static_cast<uint16_t>(base + 10)), next);
        CHECK_EQ(word(r, static_cast<uint16_t>(base + 12)), prev);
    }

    /// The first "?L=" answer's function 0 count (rsxq prints X, then A: the count).
    unsigned fn0(const std::string &o, char letter)
    {
        const std::string key = std::string("?") + letter + "=";
        const size_t at = o.find(key);
        CHECK(at != std::string::npos);
        return at == std::string::npos ? 0 : hexval(o.substr(at + key.size() + 2, 2));
    }

    /// `line`, whose program chains to `chained` by 47: what the chained command printed, to its prompt.
    std::string chained(Ccp &r, const std::string &line, const std::string &command)
    {
        const size_t from = r.out.size();
        r.type(line + "\r");
        const std::string shown = "A>" + command + "\r\n";
        if (!r.until(shown, ms(4000), from))
        {
            std::printf("  never saw '%s' in '%s'\n", vis(shown).c_str(), vis(r.out.substr(from)).c_str());
            CHECK(false);
            return {};
        }
        const size_t at = r.out.find(shown, from) + shown.size();
        CHECK(r.until("\r\nA>", ms(4000), at));
        return r.out.substr(at);
    }

    /// RSXQ's header with its BSS made to end the image and BSS at `end`.
    std::vector<uint8_t> bss_to(std::vector<uint8_t> com, unsigned end)
    {
        const unsigned size = com[6] | com[7] << 8;
        const unsigned bss = end - 0x0300 - size;
        com[8] = static_cast<uint8_t>(bss);
        com[9] = static_cast<uint8_t>(bss >> 8);
        return com;
    }
} // namespace

// ---- loading and the chain ---------------------------------------------------------------------

TEST(one_rsx_loads_at_the_top_of_the_tpa_and_0203_goes_to_it)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    CHECK_EQ(top(r), 0xBFFDu);
    std::string o = r.cmd("m:rqa # T ?A");
    has(o, "A# T=BF06 ?A=0000 ");                  // its letter before the '#', $0204 its entry
    CHECK_EQ(nrsx(r), 1u);
    CHECK_EQ(res(r, "TBASE"), 0xBF);
    CHECK_EQ(r.phys(0x0203), 0x4C);
    CHECK_EQ(top(r), 0xBF06u);
    rsx_at(r, 0xBF, "trsxa.bf.img", 0xBFFD, 0x0203); // the BDOS next, $0203 before it
    CHECK_EQ(r.phys(0xBF00 + 14), 0);               // its remove flag as the file has it: 0, it stays
    // It stays for the next program, and the CCP's own calls go through it too
    o = r.cmd("m:rsxq # T");
    has(o, "A# T=BF06 ");
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("dir m:rsxq.com");
    has(o, "M: RSXQ     COM");
}

TEST(the_loader_writes_its_flag_0_and_leaves_the_remove_flag_as_the_file_has_it)
{
    Ccp r;
    REQUIRE(r.ok);
    std::vector<uint8_t> f = built("rqa.com");
    const unsigned table = (f[14] | f[15] << 8) * 128u;
    const unsigned rsx = (f[table] | f[table + 1] << 8) * 128u + 16; // A's image in the file
    f[rsx + 24] = 0xFF;                                              // a loader flag of $FF
    f[rsx + 14] = 0xFF;                                              // and a remove flag of $FF
    r.put_m({{0, "RQAF    COM", f}});
    has(r.cmd("m:rqaf # T"), "A# T=BF06 ");
    CHECK_EQ(r.phys(0xBF00 + 24), 0);
    CHECK_EQ(r.phys(0xBF00 + 14), 0xFF);
    CHECK_EQ(nrsx(r), 0u);                                           // so it went after its program
}

TEST(two_rsxs_chain_the_newest_first_and_the_same_one_again_stacks)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQAB    COM", "rqab.com"), on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    std::string o = r.cmd("m:rqab # T ?B");
    has(o, "BA# T=BE06 ");                          // B, loaded second, below A and first in the chain
    CHECK_EQ(nrsx(r), 2u);
    CHECK_EQ(res(r, "TBASE", 0), 0xBF);
    CHECK_EQ(res(r, "TBASE", 1), 0xBE);
    rsx_at(r, 0xBF, "trsxa.bf.img", 0xBFFD, 0xBE09); // A: to the BDOS; led to by B's offset 9
    rsx_at(r, 0xBE, "trsxb.be.img", 0xBF06, 0x0203); // B: to A; led to by $0203
    CHECK_EQ(res(r, "TZP", 1), 0xF0);               // B's 4 bytes of zero page from $F0
    CHECK_EQ(res(r, "TZN", 1), 4);
    CHECK(r.phys(0xF0) != 0);                       // and it counts there
    // The same RSX again: stacked below, the newest first (the Guide's 1.6.5)
    o = r.cmd("m:rqa # T");
    has(o, "ABA# T=BD06 ");
    CHECK_EQ(nrsx(r), 3u);
    CHECK_EQ(res(r, "TBASE", 2), 0xBD);
    CHECK_EQ(word(r, 0xBD0A), 0xBE06u);
    CHECK_EQ(word(r, 0xBE0C), 0xBD09u);
}

TEST(fifteen_rsxs_fill_the_table_and_a_sixteenth_is_refused)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQ15    COM", "rq15.com"), on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    std::string o = r.cmd("m:rq15 # T");
    has(o, std::string(15, 'A') + "# T=B106 ");
    CHECK_EQ(nrsx(r), 15u);
    o = r.cmd("m:rqa #");
    has(o, "Insufficient Memory");
    hasnt(o, "A#");
    CHECK_EQ(nrsx(r), 15u);
    CHECK_EQ(top(r), 0xB106u);
}

TEST(zero_page_slices_are_given_relocated_and_a_seventeenth_byte_is_refused)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQBE    COM", "rqbe.com"), on_m("RQAB    COM", "rqab.com"), on_m("RSXQ    COM", "rsxq.com")});
    // B's 4 and E's 16: E doesn't fit; B, linked first, taken out again
    std::string o = r.cmd("m:rqbe #");
    has(o, "BAD PROGRAM HEADER");
    CHECK_EQ(nrsx(r), 0u);
    CHECK_EQ(top(r), 0xBFFDu);
    o = r.cmd("m:rsxq # T");
    has(o, "# T=BFFD ");
    hasnt(o, "B#");
    // B again, then freed when it goes: its slice from $F0, counted in
    o = r.cmd("m:rqab ?B");
    CHECK_EQ(res(r, "TZP", 1), 0xF0);
    const unsigned z = r.phys(0xF0);
    CHECK(z != 0);
    o = r.cmd("m:rsxq ?B");
    CHECK(r.phys(0xF0) != z);
    has(o, "?B=");
}

TEST(a_program_that_fits_alone_but_not_with_its_rsxs_is_refused_and_they_go_again)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "BIGA    COM", bss_to(built("rqa.com"), 0xBFFD)},
             {0, "BIG     COM", bss_to(built("rsxq.com"), 0xBFFD)},
             {0, "BIGPAST COM", bss_to(built("rsxq.com"), 0xBFFE)}});
    std::string o = r.cmd("m:big T");
    has(o, "T=BFFD ");                              // to $BFFD exactly: it runs
    o = r.cmd("m:bigpast T");
    has(o, "Insufficient Memory");
    o = r.cmd("m:biga T");
    has(o, "Insufficient Memory");                   // to $BFFD with A at $BF00: refused
    CHECK_EQ(nrsx(r), 0u);
    CHECK_EQ(top(r), 0xBFFDu);
}

TEST(non_banked_rsxs_are_passed_over_by_the_tables_flag_and_by_the_prefixs)
{
    Ccp r;
    REQUIRE(r.ok);
    std::vector<uint8_t> prefix_only = built("rqd.com");
    const unsigned table = (prefix_only[14] | prefix_only[15] << 8) * 128u;
    CHECK_EQ(prefix_only[table + 16 + 2], 0x01);     // D's entry: RT_NONBANK, from its prefix
    prefix_only[table + 16 + 2] = 0;                 // left to the prefix's flag alone
    std::vector<uint8_t> table_only = built("rqd.com");
    const unsigned d_rsx = (table_only[table + 16] | table_only[table + 17] << 8) * 128u + 16;
    CHECK_EQ(table_only[d_rsx + 15], 0xFF);
    table_only[d_rsx + 15] = 0;                      // left to the table's flag alone
    r.put_m({on_m("RQD     COM", "rqd.com"), {0, "RQDP    COM", prefix_only}, {0, "RQDT    COM", table_only},
             on_m("RSXQ    COM", "rsxq.com")});
    std::string o = r.cmd("m:rqd # T");
    has(o, "A# T=BF06 ");
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("m:rqdt # T");
    has(o, "AA# T=BE06 ");
    CHECK_EQ(nrsx(r), 2u);
    r.cmd("m:rsxq -A");                             // the second A out again
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("m:rqdp # T");
    has(o, "AA# T=BE06 ");
    CHECK_EQ(nrsx(r), 2u);
}

// ---- removal ---------------------------------------------------------------------------------

TEST(rsxs_whose_remove_flag_is_set_go_at_the_warm_boot_the_top_rising_only_from_the_bottom)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQAB    COM", "rqab.com"), on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com"),
             on_m("RQC     COM", "rqc.com")});
    r.cmd("m:rqab");
    r.cmd("m:rqa");                                 // A $BF, B $BE, A $BD
    CHECK_EQ(nrsx(r), 3u);
    // The middle one, B ($BE): -B passes the newest A ($BD) and B takes it
    std::string o = r.cmd("m:rsxq -B");
    has(o, "-B=0000 ");
    CHECK_EQ(nrsx(r), 2u);
    CHECK_EQ(res(r, "TBASE", 0), 0xBF);
    CHECK_EQ(res(r, "TBASE", 1), 0xBD);
    CHECK_EQ(top(r), 0xBD06u);                      // the hole at $BE stays
    CHECK_EQ(word(r, 0xBD0A), 0xBF06u);             // relinked past it
    CHECK_EQ(word(r, 0xBF0C), 0xBD09u);
    o = r.cmd("m:rsxq #");
    has(o, "AA# ");
    // The lowest: the top rises to the next
    o = r.cmd("m:rsxq -A");
    CHECK_EQ(nrsx(r), 1u);
    CHECK_EQ(top(r), 0xBF06u);
    o = r.cmd("m:rsxq -A");
    CHECK_EQ(nrsx(r), 0u);
    CHECK_EQ(top(r), 0xBFFDu);
    CHECK_EQ(r.phys(0x0203), 0x4C);
    // An RSX whose file has $FF serves its own program, then goes
    o = r.cmd("m:rqc # T");
    has(o, "C# T=BF06 ");
    CHECK_EQ(r.phys(0xBF00 + 14), 0xFF);
    CHECK_EQ(nrsx(r), 0u);
    o = r.cmd("m:rsxq #");
    has(o, "# ");
    hasnt(o, "C#");
}

TEST(an_rsx_only_com_keeps_its_rsxs_for_the_next_program_then_they_go)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQN     COM", "rqn.com"), on_m("RSXQ    COM", "rsxq.com")});
    std::string o = r.cmd("m:rqn");
    same(o, "m:rqn\r\r\nA>");                       // nothing run: the prompt again
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("dir m:rqn.com");                     // a built-in between doesn't end it
    CHECK_EQ(nrsx(r), 1u);
    r.type("\x03");                                 // nor a warm boot (a ^C at the prompt)
    r.run(ms(300));
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("m:rsxq # T");
    has(o, "C# T=BF06 ");                           // its remove flag $FF, still there
    o = r.cmd("m:rsxq # T");
    has(o, "# T=BFFD ");                            // gone after that program
    CHECK_EQ(nrsx(r), 0u);
}

TEST(an_rsx_only_coms_rsxs_are_kept_but_a_programs_59_removes_the_other_flagged_ones)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQAB    COM", "rqab.com"), on_m("RQN     COM", "rqn.com"), on_m("RSXQ    COM", "rsxq.com")});
    r.cmd("m:rqab");                                // A $BF, B $BE: they stay
    r.cmd("m:rqn");                                 // C $BD, its flag $FF: kept for the next program
    std::string o = r.cmd("m:rsxq -B 0 #");
    has(o, "-B=0000 L=00 CA# ");                    // B out at 59, C kept
    CHECK_EQ(nrsx(r), 1u);                          // and C gone after that program
    o = r.cmd("m:rsxq #");
    has(o, "\r\nA# ");
}

TEST(an_rsx_a_program_wrote_over_is_dropped_at_the_warm_boot)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQAB    COM", "rqab.com"), on_m("RSXQ    COM", "rsxq.com")});
    r.cmd("m:rqab");
    std::string o = r.cmd("m:rsxq X");              // B's offset 6 made 0: the warm boot doesn't jump there
    CHECK_EQ(nrsx(r), 1u);
    CHECK_EQ(res(r, "TBASE"), 0xBF);
    o = r.cmd("m:rsxq # T");
    has(o, "A# T=BF06 ");
}

TEST(function_59_removes_flagged_rsxs_and_with_0_does_only_that)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQAB    COM", "rqab.com"), on_m("RSXQ    COM", "rsxq.com")});
    r.cmd("m:rqab");
    std::string o = r.cmd("m:rsxq -B T 0 T #");
    has(o, "-B=0000 T=BE06 L=00 T=BF06 A# ");       // B out at once, the top risen
    CHECK_EQ(nrsx(r), 1u);
}

// ---- the warm boot ---------------------------------------------------------------------------

TEST(every_end_of_a_program_reaches_the_rsxs_as_function_0_once)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    unsigned n = fn0(r.cmd("m:rqa ?A"), 'A');
    CHECK_EQ(n, 0u);
    // RTS, JMP $0200, function 0 (which the RSXs see on its way: the warm boot sends no second), an
    // error's end: each one, and the query's own
    for (const std::string end : {"", "J", "Z", "Q"})
    {
        r.cmd("m:rsxq " + end);
        const unsigned m = fn0(r.cmd("m:rsxq ?A"), 'A');
        CHECK_EQ(m, n + 2);
        if (m != n + 2)
            std::printf("  after '%s'\n", end.c_str());
        n = m;
    }
    // ^C in function 10
    r.type("m:rsxq K\r");
    r.run(ms(300));
    const size_t from = r.out.size();
    r.type("\x03");
    CHECK(r.until("A>", ms(3000), from));
    unsigned m = fn0(r.cmd("m:rsxq ?A"), 'A');
    CHECK_EQ(m, n + 2);
    n = m;
    // 47: the chaining program's end, then the chained one asks
    m = fn0(chained(r, "m:rsxq Cm:rsxq ?A", "M:RSXQ ?A"), 'A');
    CHECK_EQ(m, n + 2);
    n = m;
    // A ^C at the prompt is a warm boot too
    r.type("\x03");
    r.run(ms(300));
    m = fn0(r.cmd("m:rsxq ?A"), 'A');
    CHECK_EQ(m, n + 2);
}

TEST(a_0203_a_program_overwrote_is_mended_at_the_warm_boot)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    r.cmd("m:rqa");
    std::string o = r.cmd("m:rsxq W # T");
    has(o, "\r\n# T=BFFD ");                        // past the RSX for the rest of this program
    o = r.cmd("m:rsxq # T");
    has(o, "A# T=BF06 ");
}

TEST(the_scb_19_says_the_ccp_runs_and_counts_its_commands)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RSXQ    COM", "rsxq.com")});
    CHECK_EQ(r.phys(SCB_PHYS + 0x19) & 0xC0, 0x80); // at the prompt: the CCP runs, no command does
    std::string o = r.cmd("m:rsxq S19");
    const size_t at = o.find("S19=");
    REQUIRE(at != std::string::npos);
    const unsigned v = hexval(o.substr(at + 4, 2));
    CHECK_EQ(v & 0xC0, 0x40);                       // a program runs: a command, not the CCP
    r.cmd("dir m:");                                // a built-in counts
    o = r.cmd("m:rsxq S19");
    has(o, "S19=" + hex(0x40 | ((v + 2) & 0x3F), 2));
    o = r.to("m:rsxq S19!m:rsxq S19", "A>m:rsxq S19\r\n");
    r.run(ms(800));
    has(r.out, "S19=" + hex(0x40 | ((v + 4) & 0x3F), 2)); // each of a ! line's
    CHECK_EQ(r.phys(SCB_PHYS + 0x19) & 0xC0, 0x80);
    // A built-in runs with bit 6 set and the CCP's bit 7: DIR's output is a command's (5b's PUT); at the next
    // prompt both are as before
    bool seen = false;
    const uint16_t sayprompt = sym("SAYPROMPT"), dir = sym("B_DIR");
    r.watch = [&](uint16_t pc) {
        if (pc == dir)
            seen = true;
        if (seen && pc == sayprompt)
        {
            CHECK_EQ(r.phys(SCB_PHYS + 0x19) & 0xC0, 0xC0); // still the command's, before the prompt clears it
            seen = false;
        }
    };
    r.cmd("dir m:");
    r.watch = nullptr;
    CHECK_EQ(r.phys(SCB_PHYS + 0x19) & 0xC0, 0x80);
    // The program's own bytes, $06-$09, are left alone through programs, built-ins and a ! line
    for (unsigned i = 0; i < 4; ++i)
        r.scb(0x06 + i, static_cast<uint8_t>(0x11 * (i + 1)));
    o = r.cmd("m:rsxq S06 S07 S08 S09");
    has(o, "S06=11 S07=22 S08=33 S09=44 ");
    r.cmd("dir m:");
    r.to("m:rsxq!m:rsxq", "A>m:rsxq\r\n");
    r.run(ms(800));
    for (unsigned i = 0; i < 4; ++i)
        CHECK_EQ(r.phys(SCB_PHYS + 0x06 + i), 0x11 * (i + 1));
}

// ---- the header ------------------------------------------------------------------------------

TEST(scb_entries_are_set_as_the_program_starts)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQS     COM", "rqs.com"), on_m("RSXQ    COM", "rsxq.com")});
    std::string o = r.cmd("m:rqs S34 S06");
    has(o, "S34=02 S06=5A ");
    o = r.cmd("m:rsxq S34");
    has(o, "S34=00 ");                              // the warm boot's console mode 0 again
}

TEST(an_empty_com_is_no_program_and_go_is_checked_against_the_lowest_rsx)
{
    Ccp r;
    REQUIRE(r.ok);
    std::vector<uint8_t> empty = built("rsxq.com");
    empty[6] = empty[7] = 0;
    r.put_m({{0, "EMPTY   COM", empty}, on_m("RQA     COM", "rqa.com")});
    std::string o = r.cmd("m:empty");
    has(o, "NO PROGRAM");
    r.cmd("m:rqa");
    r.poke_com(bss_to(built("rsxq.com"), 0xBFFD));
    o = r.cmd("go T");
    has(o, "Insufficient Memory");
    r.poke_com(bss_to(built("rsxq.com"), 0xBF00));
    o = r.cmd("go T");
    has(o, "T=BF06 ");
}

// ---- 59 and 60 by a program ------------------------------------------------------------------

TEST(function_59_loads_an_overlay_relocated_and_puts_the_callers_state_back)
{
    Ccp r;
    REQUIRE(r.ok);
    std::vector<uint8_t> shortf = built("ovl.prl");
    shortf.resize(256);
    r.put_m({on_m("OVLT    COM", "ovlt.com"), on_m("OVL     PRL", "ovl.prl"), on_m("OVLZP   PRL", "ovlzp.prl"),
             on_m("RSXQ    COM", "rsxq.com"), on_m("TRSXA   RSX", "trsxa.rsx"), on_m("RQA     COM", "rqa.com"),
             {0, "SHORT   PRL", shortf}, {0, "TEXT    TXT", bytes("not a program\r\n")}});
    for (const auto &[addr, img] : {std::pair<std::string, std::string>{"6000", "ovl.6000.img"}, {"7100", "ovl.7100.img"}})
    {
        const std::string o = r.cmd("m:ovlt m:ovl.prl " + addr);
        has(o, "L=0000 D=9000 G=0123 E=FE M=03 R=" + addr.substr(2, 2) + addr.substr(0, 2) + "00");
        const std::vector<uint8_t> want = built(img);
        const uint32_t at = hexval(addr);
        unsigned differ = 0;
        for (unsigned i = 0; i < want.size(); ++i)
            differ += r.phys(at + i) != want[i];
        CHECK_EQ(differ, 0u);
    }
    // With the decimal flag set by the caller: the loader entered with it clear (the door's CLD), the same bytes
    {
        const uint16_t loader = sym("LOADER");
        int d_at_entry = -1;
        r.watch = [&](uint16_t pc) {
            if (pc == loader) // the last: OVLT's 59, after the CCP's own load of OVLT
                d_at_entry = (r.cpu->regs().p & Cpu6502::FLAG_D) ? 1 : 0;
        };
        const std::string o = r.cmd("m:ovlt m:ovl.prl 6000D");
        r.watch = nullptr;
        CHECK_EQ(d_at_entry, 0);
        has(o, "L=0000 D=9000 G=0123 E=FE M=03 R=006000");
        const std::vector<uint8_t> want = built("ovl.6000.img");
        unsigned differ = 0;
        for (unsigned i = 0; i < want.size(); ++i)
            differ += r.phys(0x6000 + i) != want[i];
        CHECK_EQ(differ, 0u);
    }
    // A .COM with RSXs attached: the CCP's loads only, so refused, nothing loaded
    has(r.cmd("m:ovlt m:rqa.com 0300"), "L=FE04");
    CHECK_EQ(nrsx(r), 0u);
    // The refusals: A and X, and the state kept all the same
    const std::pair<std::string, std::string> refusals[] = {
        {"m:ovl.prl 6010", "L=FE00"},  // not a page
        {"m:ovl.prl 0200", "L=FE00"},  // below $0300
        {"m:ovl.prl BC00", "L=FE00"},  // past $BFFD
        {"m:ovlzp.prl 6000", "L=FE02"}, // zero page of its own
        {"m:rsxq.com 6000", "L=FE00"}, // a .COM not at $0300
        {"m:trsxa.rsx 6000", "L=FE01"}, // an RSX
        {"m:text.txt 6000", "L=FE03"}, // no magic number
        {"m:short.prl 6000", "L=01"},  // the file ends inside its bitmap
    };
    for (const auto &[tail, want] : refusals)
    {
        const std::string o = r.cmd("m:ovlt " + tail);
        has(o, want);
        has(o, "D=9000 G=0123 E=FE M=03 R=" + tail.substr(tail.size() - 2) + tail.substr(tail.size() - 4, 2) + "00");
    }
    // Over an RSX: fits below $BFFD, not below A at $BF00
    has(r.cmd("m:ovlt m:ovl.prl BB00"), "L=0000");
    r.cmd("m:rqa");
    has(r.cmd("m:ovlt m:ovl.prl BB00"), "L=FE00");
    CHECK_EQ(nrsx(r), 1u);
}

TEST(function_60_with_no_rsx_to_take_it_returns_ff_and_47_loads_rsxs)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RQA     COM", "rqa.com"), on_m("RSXQ    COM", "rsxq.com")});
    has(r.cmd("m:rsxq ?A"), "?A=FFFF ");
    has(chained(r, "m:rsxq Cm:rqa # T", "M:RQA # T"), "A# T=BF06 ");
    CHECK_EQ(nrsx(r), 1u);
    has(r.cmd("m:rsxq ?A"), "?A=00");
}

// ---- cost ------------------------------------------------------------------------------------

TEST(loading_a_gencomd_program_against_a_plain_one)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RSXQ    COM", "rsxq.com"), on_m("RQA     COM", "rqa.com"), on_m("RQAB    COM", "rqab.com"),
             on_m("SYSTEST COM", "systest.com"), on_m("RSXQ2   COM", "rsxq.com")});
    const uint16_t found = sym("FOUNDIT");
    auto load = [&](const std::string &line) {
        uint64_t t0 = 0, t = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == found)
                t0 = r.cpu->cycles();
            else if (t0 && !t && pc == 0x0300)
                t = r.cpu->cycles() - t0;
        };
        r.cmd(line);
        r.watch = nullptr;
        CHECK(t != 0);
        return t;
    };
    load("m:rsxq2");                                // M: logged in, its directory hashed
    const uint64_t plain = load("m:rsxq"), one = load("m:rqa"), two = load("m:rqab");
    const uint64_t big = load("m:systest");
    const std::vector<uint8_t> q = built("rsxq.com"), s = built("systest.com");
    std::printf("  the CCP's load, its search's Open to $0300: RSXQ (%zu bytes) %llu cycles; with one RSX %llu;"
                " with two %llu; SYSTEST (%zu bytes) %llu, %llu a record\n",
                q.size(), static_cast<unsigned long long>(plain), static_cast<unsigned long long>(one),
                static_cast<unsigned long long>(two), s.size(), static_cast<unsigned long long>(big),
                static_cast<unsigned long long>(big / ((s.size() + 127) / 128)));
    CHECK(plain < one && one < two);
}

TEST(a_bdos_call_through_rsxs_costs_their_filters)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({on_m("RSXQ    COM", "rsxq.com"), on_m("RQAB    COM", "rqab.com"), on_m("RQA     COM", "rqa.com")});
    const uint16_t bdosent = sym("BDOSENT");
    auto cheapest = [&](const std::string &line) {
        uint64_t t0 = 0, best = ~uint64_t{0};
        bool in_program = false;
        r.watch = [&](uint16_t pc) {
            if (pc == 0x0300)
                in_program = true;
            if (!in_program)
                return;
            if (pc == 0x0203)
                t0 = r.cpu->cycles();
            else if (t0 && pc == bdosent)
            {
                best = std::min(best, r.cpu->cycles() - t0);
                t0 = 0;
            }
        };
        r.cmd(line);
        r.watch = nullptr;
        return best;
    };
    const uint64_t none = cheapest("m:rsxq ?Z");
    r.cmd("m:rqa");
    const uint64_t one = cheapest("m:rsxq ?Z");
    r.cmd("m:rqab");
    const uint64_t three = cheapest("m:rsxq ?Z");
    std::printf("  JSR $0203 to the BDOS: %llu cycles with no RSX, %llu through one, %llu through three\n",
                static_cast<unsigned long long>(none), static_cast<unsigned long long>(one),
                static_cast<unsigned long long>(three));
    CHECK(none < one && one < three);
    CHECK(three - none < 3 * 60);
}

int main()
{
    return run_all();
}
