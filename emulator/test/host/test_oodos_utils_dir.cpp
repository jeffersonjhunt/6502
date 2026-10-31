/**
 * @file test_oodos_utils_dir.cpp
 * @brief OODOS's DIR.COM, phase 4b1 (hw/oodos.md, "The utilities, phase 4b": DIR).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the
 * built DIR.COM (oodos/utils/dir.s) put on M: as user 0's SYS file and the
 * search chain the default drive, then M:, so the CCP finds it as it would
 * on A:. Files on M: behind OODOS's back, in several users, with SYS, RO,
 * archive and F1-F4, a file of two entries, XFCBs for the Prot column, and
 * SFCB stamps poked into M:'s directory. Each command typed at the CCP, as
 * a user would, its output compared line for line:
 *
 *   - the short form (no option list): the built-in's lines, sorted, each
 *     drive in turn, No File, SYSTEM FILE(S) EXIST; the CCP's hand-offs
 *     (`dir a b`, `dir [full]`, `dirs [size]` with SYS added; `DIR.COM
 *     required` without it);
 *   - FULL (its headings, columns, attributes, ATT, Prot, stamps, Create or
 *     Access by the label, totals), SIZE, DATE (and Date and Time Stamping
 *     Inactive);
 *   - USER=, DRIVE= (ALL: the logged-in drives), DISK=, EXCLUDE, DIR, SYS,
 *     RO, RW, NOSORT, MESSAGE, LENGTH=, FF (LST: put on the serial line),
 *     NOPAGE, paging and ^C, several specifications and drives;
 *   - every error message, the return code after each (`:hello` runs only
 *     after a success), Not Enough Memory for Sort (DIR.COM's DIRROOM made
 *     small, by dir.lbl), a drive that isn't;
 *   - the card's A: against its directory read behind OODOS's back, and
 *     its empty B:.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    /// oodos/utils's build: the build tree's, or OODOS_UTILS_DIR's (a fault put in on purpose).
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    const std::string FH = "    Name     Bytes   Recs   Attributes   Prot      Update          Create";
    const std::string FHA = "    Name     Bytes   Recs   Attributes   Prot      Update          Access";
    const std::string FD = "------------ ------ ------ ------------ ------ --------------  --------------";
    const std::string DH = "    Name         Update          Create";
    const std::string DD = "------------ --------------  --------------";
    const std::string NOSTAMP(14, ' ');

    /// Lines joined as a utility writes them: separated by CR LF, the last not ended.
    std::string lines(const std::vector<std::string> &ls)
    {
        std::string s;
        for (size_t i = 0; i < ls.size(); ++i)
            s += (i ? "\r\n" : "") + ls[i];
        return s;
    }
    std::vector<std::string> operator+(std::vector<std::string> a, const std::vector<std::string> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    /// A stamp as DIR writes it, MM/DD/YY HH:MM.
    std::string when(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi)
    {
        char b[32];
        std::snprintf(b, sizeof b, "%02u/%02u/%02u %02u:%02u", mo, d, y % 100, h, mi);
        return b;
    }
    /// The same as an SFCB keeps it: the days, the hour and minute in BCD.
    std::vector<uint8_t> stamp(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi)
    {
        const unsigned n = days(y, mo, d);
        return {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(h / 10 * 16 + h % 10),
                static_cast<uint8_t>(mi / 10 * 16 + mi % 10)};
    }

    /// An 8 + 3 name as a listing shows it: "NAME     TYP".
    std::string nm(const std::string &name83) { return name83.substr(0, 8) + " " + name83.substr(8, 3); }

    std::string full(const std::string &name83, unsigned kb, unsigned recs, const std::string &attr,
                     const std::string &prot, const std::string &upd = NOSTAMP, const std::string &other = NOSTAMP)
    {
        char b[160];
        std::snprintf(b, sizeof b, "%s %5uk %6u %-12s %-6s %-14s  %-14s", nm(name83).c_str(), kb, recs, attr.c_str(),
                      prot.c_str(), upd.c_str(), other.c_str());
        return b;
    }
    std::string dated(const std::string &name83, const std::string &upd = NOSTAMP, const std::string &other = NOSTAMP)
    {
        return nm(name83) + " " + upd + "  " + other;
    }
    std::string sized(const std::string &name83, unsigned kb)
    {
        char b[16];
        std::snprintf(b, sizeof b, "%5uk", kb);
        return nm(name83) + b;
    }
    std::vector<std::string> totals(unsigned kb, unsigned recs, unsigned files, unsigned blk, char drive,
                                    unsigned used, unsigned max)
    {
        char a[96], b[96];
        std::snprintf(a, sizeof a, "Total Bytes     = %6uk  Total Records = %7u  Files Found = %4u", kb, recs, files);
        std::snprintf(b, sizeof b, "Total 1k Blocks = %6u   Used/Max Dir Entries For Drive %c:%5u/%4u", blk, drive,
                      used, max);
        return {"", a, b};
    }
    std::vector<std::string> heading(char drive, unsigned user)
    {
        char b[64];
        std::snprintf(b, sizeof b, "Directory For Drive %c:  User %2u", drive, user);
        return {b, ""};
    }
    std::vector<std::string> fhead(const std::string &h = FH) { return {h, FD, ""}; }

    /// The rig: DIR.COM on M: (or `com`, a changed copy), the chain the default drive then M:.
    struct DirRig : CcpRig
    {
        std::vector<uint8_t> com;
        explicit DirRig(bool files = true, std::vector<uint8_t> patched = {})
        {
            if (!ok)
                return;
            com = patched.empty() ? read_bytes(UTILS + "/dir.com") : patched;
            REQUIRE(!com.empty());
            put_m({{0, "DIR     COM", com, true}});
            chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
            if (files)
                put_files();
        }
        unsigned com_recs() const { return static_cast<unsigned>((com.size() + 127) / 128); }
        unsigned com_kb() const { return static_cast<unsigned>((com.size() + 2047) / 2048 * 2); }

        /// A directory entry made by hand: `blocks` its pointers.
        unsigned raw(unsigned user, const std::string &name83, unsigned ex, unsigned rc,
                     const std::vector<unsigned> &blocks)
        {
            const unsigned at = free_entry();
            std::vector<uint8_t> e(32, 0);
            e[0] = static_cast<uint8_t>(user);
            for (unsigned i = 0; i < 11; ++i)
                e[1 + i] = static_cast<uint8_t>(name83[i]);
            e[12] = static_cast<uint8_t>(ex);
            e[15] = static_cast<uint8_t>(rc);
            for (size_t k = 0; k < blocks.size(); ++k)
                e[16 + k] = static_cast<uint8_t>(blocks[k]);
            poke(MDISK + at * 32, e);
            return at;
        }
        /// Entry `e`'s SFCB slot: its creation (or access) and update stamps.
        void slot(unsigned e, const std::vector<uint8_t> &first, const std::vector<uint8_t> &upd)
        {
            const uint32_t at = MDISK + (e | 3) * 32 + 1 + (e & 3) * 10;
            REQUIRE(phys(MDISK + (e | 3) * 32) == 0x21);
            poke(at, first);
            poke(at + 4, upd);
        }
        unsigned at(unsigned user, const std::string &name83)
        {
            const int e = m_entry(user, name83);
            CHECK(e >= 0);
            return e >= 0 ? static_cast<unsigned>(e) : 0;
        }
        /// An attribute bit (bit 7 of name byte `i`, 1-11) set in a file's entry.
        void attr(unsigned user, const std::string &name83, unsigned i)
        {
            const uint32_t a = MDISK + at(user, name83) * 32 + i;
            poke(a, static_cast<uint8_t>(phys(a) | 0x80));
        }
        /// M:'s entries that aren't free or SFCBs: DIR's Used.
        unsigned used()
        {
            unsigned n = 0;
            for (unsigned i = 0; i < 256; ++i)
                n += phys(MDISK + i * 32) != 0xE5 && phys(MDISK + i * 32) != 0x21;
            return n;
        }

        /// User 0: ALPHA.TXT (stamped), ZED (an XFCB, Read), BIG.DAT (two entries, its later
        /// extent first), BETA.DAT (RO, archived, F1 and F3, an XFCB, Write), GAMMA.COM (SYS, RO),
        /// DIR.COM (SYS); THREE.TXT in user 3, TWELVE.TXT in 12; an XFCB with no file.
        void put_files()
        {
            put_m({{0, "ALPHA   TXT", std::vector<uint8_t>(300, 'a')}, {0, "ZED        ", bytes("Z")}});
            const unsigned big2 = raw(0, "BIG     DAT", 2, 10, {40});
            put_m({{0, "BETA    DAT", bytes("B"), false, true}, {0, "GAMMA   COM", bytes("G"), true, true}});
            std::vector<unsigned> blocks;
            for (unsigned b = 41; b <= 56; ++b)
                blocks.push_back(b);
            const unsigned big0 = raw(0, "BIG     DAT", 1, 128, blocks);
            put_m({{3, "THREE   TXT", bytes("3")}, {12, "TWELVE  TXT", bytes("12")}});
            xfcb_m(0, "ZED        ", 0x80, "SECRET  ");
            xfcb_m(0, "BETA    DAT", 0x40, "PW      ");
            xfcb_m(0, "GHOST   TXT", 0x20, "X       ");
            attr(0, "BETA    DAT", 11);
            attr(0, "BETA    DAT", 1);
            attr(0, "BETA    DAT", 3);
            for (unsigned i = 3; i < 256; i += 4) // the slots MFORMAT left $E5, cleared as INITDIR would
                for (unsigned j = 1; j < 32; ++j)
                    poke(MDISK + i * 32 + j, 0);
            slot(at(0, "ALPHA   TXT"), stamp(2026, 10, 1, 9, 30), stamp(2026, 10, 2, 10, 45));
            slot(big0, stamp(2026, 10, 5, 8, 0), stamp(2026, 10, 30, 23, 59));
            slot(big2, stamp(2025, 1, 1, 1, 1), stamp(2025, 1, 1, 1, 1)); // a later extent's: not shown
        }

        /// The command's output: between its echo's CR LF and the prompt's.
        std::string said(const std::string &line, const std::string &prompt = "A>")
        {
            const std::string o = cmd(line, prompt);
            const std::string head = line + "\r\n", end = "\r\n" + prompt;
            if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
                o.compare(o.size() - end.size(), end.size(), end) != 0)
                return "<" + o + ">";
            return o.substr(head.size(), o.size() - head.size() - end.size());
        }
        /// After a command: did it succeed? (`:hello` runs only if the return code isn't $FF00-$FFFF.)
        bool succeeded(const std::string &prompt = "A>")
        {
            const std::string o = cmd(":hello", prompt);
            return o.find("Hello from OODOS") != std::string::npos;
        }
    };

    // User 0's files, as put_files() made them, in FULL's sorted order.
    std::vector<std::string> user0_full(DirRig &r, bool att = false)
    {
        return {full("ALPHA   TXT", 2, 3, "Dir RW", "None", when(2026, 10, 2, 10, 45), when(2026, 10, 1, 9, 30)),
                full("BETA    DAT", 2, 1, att ? "Dir RO A1 3" : "Dir RO A", "Write"),
                full("BIG     DAT", 34, 266, "Dir RW", "None", when(2026, 10, 30, 23, 59), when(2026, 10, 5, 8, 0)),
                full("DIR     COM", r.com_kb(), r.com_recs(), "Sys RW", "None"),
                full("GAMMA   COM", 2, 1, "Sys RO", "None"),
                full("ZED        ", 2, 1, "Dir RW", "Read")};
    }
    std::vector<std::string> user0_totals(DirRig &r)
    {
        const unsigned kb = 2 + 2 + 34 + r.com_kb() + 2 + 2, recs = 3 + 1 + 266 + r.com_recs() + 1 + 1;
        const unsigned blk = 1 + 1 + 34 + (r.com_recs() + 7) / 8 + 1 + 1;
        return totals(kb, recs, 6, blk, 'M', r.used(), 192);
    }
} // namespace

// ---- the short form and the CCP's hand-offs --------------------------------------------------

TEST(without_options_dir_com_lists_as_the_built_in_does_sorted_and_each_drive_in_turn)
{
    // In the directory's order already: DIR.COM's lines are the built-in's bytes
    {
        DirRig r(false);
        REQUIRE(r.ok);
        r.put_m({{0, "AAA     TXT", bytes("1")}, {0, "BBB     TXT", bytes("2")}, {0, "CCC        ", bytes("3")}});
        const std::string want =
            "M: " + nm("AAA     TXT") + " : " + nm("BBB     TXT") + " : " + nm("CCC        ") + "\r\nSYSTEM FILE(S) EXIST";
        same(r.said("dir m:"), want);
        same(r.said("m:dir m:"), want);
        CHECK(r.succeeded());
    }
    DirRig r;
    REQUIRE(r.ok);
    // Sorted (the built-in's are in the directory's order); SYS files said, not listed
    const std::string zed = nm("ZED        ");
    same(r.said("m:dir m:"), "M: ALPHA    TXT : BETA     DAT : BIG      DAT : " + zed + "\r\nSYSTEM FILE(S) EXIST");
    CHECK(r.succeeded());
    same(r.said("m:dir m:*.com"), "SYSTEM FILE(S) EXIST");
    // Several specifications: the CCP hands them over, found along the chain; files matching any
    same(r.said("dir m:b*.* m:a*.*"), "M: ALPHA    TXT : BETA     DAT : BIG      DAT");
    same(r.said("dir m:*.com m:z*"), "M: " + zed + "\r\nSYSTEM FILE(S) EXIST");
    // Each drive in turn, in the order first given; one with nothing: No File, and the return code
    same(r.said("dir m:*.txt b: m:zed"), "M: ALPHA    TXT : " + zed + "\r\nNo File");
    CHECK(!r.succeeded());
    same(r.said("dir m:nosuch m:nothing"), "No File");
    CHECK(!r.succeeded());
    // The width: the built-in's rule, 15 a name
    r.scb(0x1A, 39);
    same(r.said("m:dir m:"), "M: ALPHA    TXT : BETA     DAT\r\nM: BIG      DAT : " + zed + "\r\nSYSTEM FILE(S) EXIST");
    r.scb(0x1A, 79);
    // The current drive and user
    same(r.cmd("3m:", "3M>"), "3m:\r\r\n3M>");
    same(r.said("m:dir", "3M>"), "M: THREE    TXT");
    same(r.said("dir three.txt four", "3M>"), "M: THREE    TXT");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

TEST(the_ccps_hand_offs_run_dir_com_by_the_search_chain_with_the_tail_as_typed)
{
    DirRig r;
    REQUIRE(r.ok);
    r.chain({2}); // not on the chain (B:, the card's empty drive; A: has the card's): the built-in says so
    same(r.said("dir [full]"), "DIR.COM required");
    same(r.said("dirs [size]"), "DIR.COM required");
    r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
    same(r.said("dir a b"), "No File"); // A:'s user 0 has neither
    same(r.said("dirs m: [size]"), lines(heading('M', 0) + std::vector<std::string>{
                                            "M: " + sized("DIR     COM", r.com_kb()) + " : " + sized("GAMMA   COM", 2)} +
                                        totals(r.com_kb() + 2, r.com_recs() + 1, 2, (r.com_recs() + 7) / 8 + 1, 'M',
                                               r.used(), 192)));
    same(r.said("dirs m:"), "M: DIR      COM : GAMMA    COM\r\nNON-SYSTEM FILE(S) EXIST"); // the built-in's own
    same(r.cmd("m:", "M>"), "m:\r\r\nM>");
    same(r.said("dir [full]", "M>"), lines(heading('M', 0) + fhead() + user0_full(r) + user0_totals(r)));
    same(r.cmd("a:"), "a:\r\r\nA>");
}

// ---- FULL, SIZE, DATE ------------------------------------------------------------------------

TEST(full_shows_sizes_attributes_protection_and_stamps_with_totals)
{
    DirRig r;
    REQUIRE(r.ok);
    const std::vector<std::string> want = heading('M', 0) + fhead() + user0_full(r) + user0_totals(r);
    same(r.said("dir m: [full]"), lines(want));
    CHECK(r.succeeded());
    same(r.said("dir m: [f]"), "Illegal Option or Modifier."); // FF or FULL
    same(r.said("dir [fu] m:"), lines(want));                  // a prefix; the list anywhere
    same(r.said("dir m: [user=0]"), lines(want));              // FULL is the default with options
    // ATT: F1-F4 in their places
    same(r.said("dir m: [att]"), lines(heading('M', 0) + fhead() + user0_full(r, true) + user0_totals(r)));
}

TEST(full_heads_its_last_column_access_or_create_as_the_label_asks_and_prot_needs_a_label)
{
    {
        DirRig r(false);
        REQUIRE(r.ok);
        r.put_m({{0, "ONE     TXT", bytes("1")}});
        r.xfcb_m(0, "ONE     TXT", 0x20, "PW      ");
        r.slot(r.at(0, "ONE     TXT"), stamp(2026, 3, 4, 5, 6), stamp(2026, 7, 8, 9, 10));
        r.poke(MDISK + 12, 0xD1); // passwords, access and update stamps
        same(r.said("dir m:one.txt"), "M: ONE      TXT");
        same(r.said("dir m:one.txt [full]"),
             lines(heading('M', 0) + fhead(FHA) +
                   std::vector<std::string>{full("ONE     TXT", 2, 1, "Dir RW", "Delete", when(2026, 7, 8, 9, 10),
                                                 when(2026, 3, 4, 5, 6))} +
                   totals(2, 1, 1, 1, 'M', r.used(), 192)));
        same(r.said("dir m:one.txt [date]"),
             lines(heading('M', 0) +
                   std::vector<std::string>{"    Name         Update          Access", DD, "",
                                            dated("ONE     TXT", when(2026, 7, 8, 9, 10), when(2026, 3, 4, 5, 6))} +
                   totals(2, 1, 1, 1, 'M', r.used(), 192)));
    }
    {
        // No label: Prot blank; the last column Create
        DirRig r(false);
        REQUIRE(r.ok);
        r.put_m({{0, "ONE     TXT", bytes("1")}});
        r.xfcb_m(0, "ONE     TXT", 0x80, "PW      ");
        r.slot(r.at(0, "ONE     TXT"), stamp(2026, 3, 4, 5, 6), stamp(2026, 7, 8, 9, 10));
        r.poke(MDISK, 0xE5);
        same(r.said("dir m:one.txt [full]"),
             lines(heading('M', 0) + fhead() +
                   std::vector<std::string>{full("ONE     TXT", 2, 1, "Dir RW", "", when(2026, 7, 8, 9, 10),
                                                 when(2026, 3, 4, 5, 6))} +
                   totals(2, 1, 1, 1, 'M', r.used(), 192)));
    }
}

TEST(size_lists_columns_with_kilobytes_and_date_the_stamps)
{
    DirRig r;
    REQUIRE(r.ok);
    same(r.said("dir m: [size]"),
         lines(heading('M', 0) +
               std::vector<std::string>{
                   "M: " + sized("ALPHA   TXT", 2) + " : " + sized("BETA    DAT", 2) + " : " + sized("BIG     DAT", 34),
                   "M: " + sized("DIR     COM", r.com_kb()) + " : " + sized("GAMMA   COM", 2) + " : " +
                       sized("ZED        ", 2)} +
               user0_totals(r)));
    r.scb(0x1A, 42); // two a line (21 each)
    same(r.said("dir m:*.dat [s]"), "Illegal Option or Modifier."); // SIZE or SYS
    same(r.said("dir m:*.dat [si]"),
         lines(heading('M', 0) +
               std::vector<std::string>{"M: " + sized("BETA    DAT", 2) + " : " + sized("BIG     DAT", 34)} +
               totals(36, 267, 2, 35, 'M', r.used(), 192)));
    r.scb(0x1A, 20); // under one: one
    const std::string narrow = r.cmd("dir m:a*.* m:z* [size]"); // (its echo wraps at 21 columns)
    same(narrow.substr(narrow.find("]\r\n") + 3, narrow.size() - narrow.find("]\r\n") - 7),
         lines(heading('M', 0) +
               std::vector<std::string>{"M: " + sized("ALPHA   TXT", 2), "M: " + sized("ZED        ", 2)} +
               totals(4, 4, 2, 2, 'M', r.used(), 192)));
    r.scb(0x1A, 79);
    same(r.said("dir m: [date]"),
         lines(heading('M', 0) +
               std::vector<std::string>{DH, DD, "",
                                        dated("ALPHA   TXT", when(2026, 10, 2, 10, 45), when(2026, 10, 1, 9, 30)),
                                        dated("BETA    DAT"),
                                        dated("BIG     DAT", when(2026, 10, 30, 23, 59), when(2026, 10, 5, 8, 0)),
                                        dated("DIR     COM"), dated("GAMMA   COM"), dated("ZED        ")} +
               user0_totals(r)));
    CHECK(r.succeeded());
}

TEST(date_on_a_drive_without_sfcbs_says_stamping_is_inactive)
{
    DirRig r(false);
    REQUIRE(r.ok);
    for (unsigned i = 3; i < 256; i += 4) // M: without SFCBs, before it is logged in
        r.poke(MDISK + i * 32, 0xE5);
    r.put_m({{0, "ONE     TXT", bytes("1")}});
    same(r.said("dir m: [date]"), "Date and Time Stamping Inactive");
    CHECK(!r.succeeded());
    // FULL there: no stamps; every entry counts for Max
    same(r.said("dir m:one.txt [full]"),
         lines(heading('M', 0) + fhead() + std::vector<std::string>{full("ONE     TXT", 2, 1, "Dir RW", "None")} +
               totals(2, 1, 1, 1, 'M', r.used(), 256)));
    // The drives after it are searched
    same(r.said("dir [date,drive=(m,b),user=3,message]"),
         lines(std::vector<std::string>{"Date and Time Stamping Inactive", ""} + heading('B', 3) +
               std::vector<std::string>{"No File"}));
}

// ---- users and drives ------------------------------------------------------------------------

TEST(user_and_drive_choose_where_dir_looks)
{
    DirRig r;
    REQUIRE(r.ok);
    const std::vector<std::string> three = heading('M', 3) + std::vector<std::string>{"M: " + sized("THREE   TXT", 2)} +
                                           totals(2, 1, 1, 1, 'M', r.used(), 192);
    const std::vector<std::string> twelve = heading('M', 12) +
                                            std::vector<std::string>{"M: " + sized("TWELVE  TXT", 2)} +
                                            totals(2, 1, 1, 1, 'M', r.used(), 192);
    same(r.said("dir m: [size,user=3]"), lines(three));
    same(r.said("dir m: [size,user=(12,3)]"), lines(three + std::vector<std::string>{""} + twelve)); // 0-15 in order
    same(r.said("dir m:t*.* [size,user=all]"), lines(three + std::vector<std::string>{""} + twelve));
    same(r.said("dir m:t* [size,user=all]"), "No File"); // no type: a blank type
    same(r.said("dir m:t*.txt [user=all,size]"), lines(three + std::vector<std::string>{""} + twelve));
    // DRIVE=: M:, and DISK= too; a list; (d) with a drive in a specification is refused
    same(r.said("dir [size,user=12,drive=m]"), lines(twelve));
    same(r.said("dir [size,user=12,disk=m:]"), lines(twelve));
    same(r.said("dir m:t*.* [size,user=12,drive=m]"), "Illegal Global/Local Drive Spec Mixing.");
    CHECK(!r.succeeded());
    // DRIVE=ALL: the logged-in drives, A to P (A: the CCP's, M: where DIR.COM came from)
    same(r.said("dir [size,user=12,drive=all,message]"),
         lines(heading('A', 12) + std::vector<std::string>{"No File", ""} + twelve));
    same(r.said("dir b:"), "No File"); // the built-in logs B: in
    same(r.said("dir [size,user=12,drive=all,message]"),
         lines(heading('A', 12) + std::vector<std::string>{"No File", ""} + heading('B', 12) +
               std::vector<std::string>{"No File", ""} + twelve));
    same(r.said("dir [size,user=12,drive=all]"), lines(twelve));
    same(r.said("dir t*.* [size,user=12,drive=(b,m),message]"),
         lines(heading('B', 12) + std::vector<std::string>{"No File", ""} + twelve));
    same(r.said("dir t*.* [size,user=12,drive=(m,b,m),message]"),
         lines(twelve + std::vector<std::string>{""} + heading('B', 12) + std::vector<std::string>{"No File"}));
    // DRIVE=*: the current drive
    same(r.cmd("12m:", "12M>"), "12m:\r\r\n12M>");
    same(r.said("dir [size,drive=*]", "12M>"), lines(twelve));
    same(r.said("dir [size]", "12M>"), lines(twelve)); // the current user and drive
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

TEST(exclude_dir_sys_ro_rw_and_nosort_choose_and_order_the_files)
{
    DirRig r;
    REQUIRE(r.ok);
    auto one = [&](const std::vector<std::string> &names) {
        // SIZE's lines for these files of user 0, three a line
        std::vector<std::string> ls = heading('M', 0);
        static const std::map<std::string, unsigned> kb = {{"ALPHA   TXT", 2}, {"BETA    DAT", 2},
                                                           {"BIG     DAT", 34}, {"GAMMA   COM", 2},
                                                           {"ZED        ", 2}};
        unsigned tk = 0, tr = 0, tb = 0;
        std::string line;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const unsigned k = names[i] == "DIR     COM" ? r.com_kb() : kb.at(names[i]);
            const unsigned recs = names[i] == "DIR     COM"   ? r.com_recs()
                                  : names[i] == "BIG     DAT" ? 266
                                  : names[i] == "ALPHA   TXT" ? 3
                                                              : 1;
            tk += k, tr += recs, tb += (recs + 7) / 8;
            line += (i % 3 ? " : " : "M: ") + sized(names[i], k);
            if (i % 3 == 2 || i + 1 == names.size())
                ls.push_back(line), line.clear();
        }
        return lines(ls + totals(tk, tr, static_cast<unsigned>(names.size()), tb, 'M', r.used(), 192));
    };
    same(r.said("dir m:*.txt [exclude,size]"), one({"BETA    DAT", "BIG     DAT", "DIR     COM", "GAMMA   COM", "ZED        "}));
    same(r.said("dir m:*.txt m:*.com [ex,size]"), one({"BETA    DAT", "BIG     DAT", "ZED        "}));
    same(r.said("dir m: [exclude,size]"), "No File"); // a drive alone is every file
    CHECK(!r.succeeded());
    same(r.said("dir m: [sys,size]"), one({"DIR     COM", "GAMMA   COM"}));
    same(r.said("dir m: [dir,size]"), one({"ALPHA   TXT", "BETA    DAT", "BIG     DAT", "ZED        "}));
    same(r.said("dir m: [dir,sys,size]"), one({"ALPHA   TXT", "BETA    DAT", "BIG     DAT", "DIR     COM", "GAMMA   COM", "ZED        "}));
    same(r.said("dir m: [ro,size]"), one({"BETA    DAT", "GAMMA   COM"}));
    same(r.said("dir m: [rw,size]"), one({"ALPHA   TXT", "BIG     DAT", "DIR     COM", "ZED        "}));
    same(r.said("dir m: [rw,sys,size]"), one({"DIR     COM"}));
    same(r.said("dir m: [ro,rw,size]"), one({"ALPHA   TXT", "BETA    DAT", "BIG     DAT", "DIR     COM", "GAMMA   COM", "ZED        "}));
    // NOSORT: the directory's order of the extent-zero entries (BIG's later extent is before BETA's)
    same(r.said("dir m: [nosort,size]"), one({"DIR     COM", "ALPHA   TXT", "ZED        ", "BETA    DAT", "GAMMA   COM", "BIG     DAT"}));
    same(r.said("dir m:nosuch [size]"), "No File");
    same(r.said("dir m:nosuch [size,message]"), lines(heading('M', 0) + std::vector<std::string>{"No File"}));
    CHECK(!r.succeeded());
}

// ---- headings, paging, FF ----------------------------------------------------------------------

TEST(length_repeats_the_heading_and_paging_starts_each_page_with_it)
{
    DirRig r;
    REQUIRE(r.ok);
    const std::vector<std::string> f = user0_full(r);
    // LENGTH=6: the heading again before the file line after each 6 lines
    same(r.said("dir m: [length=6]"),
         lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0]} + fhead() +
               std::vector<std::string>{f[1], f[2], f[3]} + fhead() + std::vector<std::string>{f[4], f[5]} +
               user0_totals(r)));
    // A page of 8: a stop every 7 lines; the heading starts the next page
    r.scb(0x1C, 8);
    size_t from = r.out.size();
    r.type("dir m: [full]\r");
    REQUIRE(r.until("Press a key for more", ms(4000), from));
    r.run(ms(200));
    same(r.out.substr(from), "dir m: [full]\r\n" + lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0], f[1]}) +
                                 "\r\nPress a key for more");
    const size_t first = r.out.find("Press a key for more", from);
    r.type(" ");
    REQUIRE(r.until("Press a key for more", ms(4000), first + 1));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(4000), from));
    const std::string rub = "\r\nPress a key for more\r                    \r";
    same(r.out.substr(from), "dir m: [full]\r\n" + lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0], f[1]}) +
                                 rub + lines(fhead() + std::vector<std::string>{f[2], f[3], f[4], f[5]}) + rub +
                                 lines(user0_totals(r)) + "\r\nA>");
    CHECK(r.succeeded());
    // ^C at the stop: stopped, the return code $FFFE
    from = r.out.size();
    r.type("dir m: [full]\r");
    REQUIRE(r.until("Press a key for more", ms(4000), from));
    r.type("\x03");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "dir m: [full]\r\n" + lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0], f[1]}) +
                                 "\r\nPress a key for more\r\nA>");
    CHECK(!r.succeeded());
    // NOPAGE: no stops and, without LENGTH=, no heading again
    same(r.said("dir m: [nopage]"), lines(heading('M', 0) + fhead() + f + user0_totals(r)));
    same(r.said("dir m: [no page]"), lines(heading('M', 0) + fhead() + f + user0_totals(r)));
    // The page mode off: the same
    r.scb(0x2C, 1);
    same(r.said("dir m: [full]"), lines(heading('M', 0) + fhead() + f + user0_totals(r)));
    r.scb(0x2C, 0);
    r.scb(0x1C, 24);
}

TEST(a_ctrl_c_typed_while_dir_writes_stops_it)
{
    DirRig r(false);
    REQUIRE(r.ok);
    std::vector<MFile> files;
    for (unsigned i = 0; i < 50; ++i)
    {
        char n[12];
        std::snprintf(n, sizeof n, "F%02u     DAT", i);
        files.push_back({7, n, bytes("x")});
    }
    r.put_m(files);
    r.scb(0x1A, 14); // one a line
    r.chain({13, 0, 1}); // M:'s DIR.COM first; HELLO.COM from A:, for succeeded()
    same(r.cmd("7m:", "7M>"), "7m:\r\r\n7M>");
    const size_t from = r.out.size();
    r.type("dir [nopage,size]\r");
    REQUIRE(r.until("F05", ms(4000), from));
    r.type("\x03");
    REQUIRE(r.until("\r\n7M>", ms(3000), from));
    CHECK(r.out.find("F49", from) == std::string::npos);
    CHECK(!r.succeeded("7M>"));
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

TEST(ff_sends_a_form_feed_to_lst_when_the_printer_echo_is_on_and_turns_paging_off)
{
    DirRig r;
    REQUIRE(r.ok);
    const std::vector<std::string> f = user0_full(r);
    r.scb(0x1C, 8); // FF turns paging off: no stops on a page of 8
    // The printer echo off: no form feed
    same(r.said("dir m: [ff]"), lines(heading('M', 0) + fhead() + f + user0_totals(r)));
    // CONOUT: the screen only, LST: the serial line and the echo on: the serial line has what the
    // console was given once, and the form feeds: one first, one each heading LENGTH= writes again
    r.scb(0x25, 0x80);
    r.scb(0x2B, 0x40);
    r.scb(0x38, 1);
    r.cmd(";"); // (a comment: the prompt waiting as the echo went on has a '>' of its own to settle)
    const std::string ff = r.said("dir m: [ff,length=6]");
    same(ff,
         "\f" + lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0]}) + "\r\n\f" +
             lines(fhead() + std::vector<std::string>{f[1], f[2], f[3]}) + "\r\n\f" +
             lines(fhead() + std::vector<std::string>{f[4], f[5]} + user0_totals(r)));
    same(r.said("dir m: [length=6,nopage]"), // without FF: no form feeds
         lines(heading('M', 0) + fhead() + std::vector<std::string>{f[0]} + fhead() +
               std::vector<std::string>{f[1], f[2], f[3]} + fhead() + std::vector<std::string>{f[4], f[5]} +
               user0_totals(r)));
    r.scb(0x38, 0);
    r.scb(0x2B, 0x20);
    r.scb(0x25, 0xC0);
    r.scb(0x1C, 24);
}

// ---- errors ----------------------------------------------------------------------------------

TEST(each_error_has_its_message_and_the_return_code_ff00)
{
    DirRig r;
    REQUIRE(r.ok);
    auto err = [&](const std::string &line, const std::string &msg) {
        same(r.said(line), msg);
        CHECK(!r.succeeded());
    };
    err("dir [full] m: [size]", "Options not grouped together.");
    err("dir m: [full", "Illegal command tail.");
    err("m:dir m:toolongname [full]", "Illegal command tail."); // (the CCP refuses it before DIR.COM:)
    err("dir m:toolongname [full]", "DIR M:TOOLONGNAME [FULL]?");
    err("m:dir m:x m:toolongname", "Illegal command tail.");
    for (const char *o : {"[bogus]", "[d]", "[n]", "[length=4]", "[length=70000]", "[length]", "[length=x]",
                          "[length=(5,6)]", "[user=16]", "[user]", "[user=x]", "[user=(1,x)]", "[user=(1,2]",
                          "[drive=q]", "[drive]", "[drive=(a,q)]", "[full=1]", "[nosort=on]", "[disk=]"})
        err(std::string("dir m: ") + o, "Illegal Option or Modifier.");
    err("dir m: [full,size]", "Illegal Format Value.");
    err("dir m: [date,full]", "Illegal Format Value.");
    err("dir m: [size,date]", "Illegal Format Value.");
    err("dir m:x [drive=m]", "Illegal Global/Local Drive Spec Mixing.");
    err("dir m:nosuch [full]", "No File");
    // A drive that isn't: the BDOS's error (mode $FE), then the next drive
    const std::string o = r.said("dir [size,user=12,drive=(h,m)]");
    same(o, "\r\nOODOS Error On H: Invalid Drive\r\nBDOS Function = 14\r\n\r\n" +
                lines(heading('M', 12) + std::vector<std::string>{"M: " + sized("TWELVE  TXT", 2)} +
                      totals(2, 1, 1, 1, 'M', r.used(), 192)));
    CHECK(!r.succeeded());
    // The short form too
    same(r.said("dir h:x m:zed"), "\r\nOODOS Error On H: Invalid Drive\r\nBDOS Function = 14\r\nM: " + nm("ZED        "));
    CHECK(!r.succeeded());
    // And all well: 0
    same(r.said("dir m:zed [size]"), lines(heading('M', 0) + std::vector<std::string>{"M: " + sized("ZED        ", 2)} +
                                          totals(2, 1, 1, 1, 'M', r.used(), 192)));
    CHECK(r.succeeded());
}

TEST(a_drive_with_more_files_than_fit_is_not_enough_memory_for_sort)
{
    // DIR.COM with DIRROOM (dir.lbl) three records' room: 3 x (32 + 2)
    std::vector<uint8_t> com = read_bytes(UTILS + "/dir.com");
    const std::map<std::string, uint16_t> lbl = labels(UTILS + "/dir.lbl");
    REQUIRE(lbl.count("DIRROOM") == 1);
    const size_t at = lbl.at("DIRROOM") - 0x0300 + 16;
    REQUIRE(at + 1 < com.size() && com[at] == 0xFF && com[at + 1] == 0xFF);
    com[at] = 3 * 34, com[at + 1] = 0;
    DirRig r(true, com);
    REQUIRE(r.ok);
    same(r.said("dir m: [size]"), "Not Enough Memory for Sort");
    CHECK(!r.succeeded());
    same(r.said("m:dir m:"), "Not Enough Memory for Sort");
    same(r.said("dir m: [nosort,size]"), "Not Enough Memory for Sort");
    // Three fit (the names not asked for take no room; an XFCB's file does)
    same(r.said("dir m:*.dat m:zed m:alpha.txt [size]"), "Not Enough Memory for Sort");
    same(r.said("dir m:*.dat m:alpha.txt [size]"),
         lines(heading('M', 0) +
               std::vector<std::string>{"M: " + sized("ALPHA   TXT", 2) + " : " + sized("BETA    DAT", 2) + " : " +
                                        sized("BIG     DAT", 34)} +
               totals(38, 270, 3, 36, 'M', r.used(), 192)));
    // The drive after one that didn't fit is listed
    same(r.said("dir [size,drive=(m,b),message]"),
         lines(std::vector<std::string>{"Not Enough Memory for Sort", ""} + heading('B', 0) +
               std::vector<std::string>{"No File"}));
}

// ---- the card's drives -----------------------------------------------------------------------

TEST(the_cards_a_is_listed_as_its_directory_says_and_its_empty_b_is_no_file)
{
    DirRig r(false);
    REQUIRE(r.ok);
    // A:'s directory read behind OODOS's back: the oracle (oodos-8m: 4 KB blocks, 16-bit pointers)
    CpmDisk a{r, 1};
    const std::vector<uint8_t> d = a.directory();
    struct F
    {
        std::string name83, attr;
        unsigned blocks = 0, recs = 0;
        bool ext0 = false;
        std::string upd = NOSTAMP, cre = NOSTAMP;
    };
    std::map<std::string, F> files;
    unsigned used = 0, sfcbs = 0;
    for (unsigned i = 0; i < a.entries(); ++i)
    {
        const uint8_t *e = &d[i * 32];
        used += e[0] != 0xE5 && e[0] != 0x21;
        sfcbs += e[0] == 0x21;
        if (e[0] != 0)
            continue;
        std::string n;
        for (unsigned c = 1; c < 12; ++c)
            n += static_cast<char>(e[c] & 0x7F);
        F &f = files[n];
        f.name83 = n;
        for (unsigned k = 0; k < 8; ++k)
            f.blocks += a.pointer(e, k) != 0;
        f.recs = std::max(f.recs, (unsigned{e[14]} * 32 + e[12]) * 128 + e[15]);
        if (e[14] == 0 && (e[12] & ~1u) == 0)
        {
            f.ext0 = true;
            f.attr = std::string(e[10] & 0x80 ? "Sys" : "Dir") + (e[9] & 0x80 ? " RO" : " RW") + (e[11] & 0x80 ? " A" : "");
            const uint8_t *s = &d[(i | 3) * 32];
            REQUIRE(s[0] == 0x21);
            auto st = [](const uint8_t *p) {
                const unsigned dn = p[0] | p[1] << 8;
                if (!dn)
                    return NOSTAMP;
                unsigned y = 1978;
                while (days(y + 1, 1, 1) <= dn)
                    ++y;
                unsigned m = 1;
                while (m < 12 && days(y, m + 1, 1) <= dn)
                    ++m;
                const unsigned dd = dn - days(y, m, 1) + 1;
                return when(y, m, dd, (p[2] >> 4) * 10 + (p[2] & 15), (p[3] >> 4) * 10 + (p[3] & 15));
            };
            const uint8_t *sl = s + 1 + (i & 3) * 10;
            f.cre = st(sl), f.upd = st(sl + 4);
        }
    }
    std::vector<std::string> want = heading('A', 0) + fhead();
    unsigned tk = 0, tr = 0, tb = 0, n = 0;
    std::vector<std::string> size_items;
    for (const auto &[name, f] : files) // std::map: sorted by name, as DIR sorts
    {
        if (!f.ext0)
            continue;
        want.push_back(full(name, f.blocks * 4, f.recs, f.attr, "None", f.upd, f.cre));
        tk += f.blocks * 4, tr += f.recs, tb += (f.recs + 7) / 8, ++n;
    }
    want = want + totals(tk, tr, n, tb, 'A', used, a.entries() - sfcbs);
    CHECK(n >= 17); // README, PATTERN, OODOS.SYS, TESTSYS, HELLO.COM, HELLO.PRL, WRITEOO1, the ten utilities
    CHECK(files.count("PATTERN BIN") == 1 && files["PATTERN BIN"].recs == 288); // two entries
    CHECK(files.count("DIR     COM") == 1 && files["DIR     COM"].attr == "Sys RW"); // make_card.sh's
    same(r.said("dir a: [full]"), lines(want));
    CHECK(r.succeeded());
    // The card's own DIR.COM, from A:, says the same
    same(r.said("a:dir a: [full]"), lines(want));
    CHECK(r.succeeded());
    // The empty B: (a label, SFCBs, no files)
    same(r.said("dir b: [full]"), "No File");
    same(r.said("dir b: [full,message]"), lines(heading('B', 0) + std::vector<std::string>{"No File"}));
    same(r.said("m:dir b:"), "No File");
    same(r.said("dir [date,drive=b]"), "No File");
}

int main()
{
    // DIR_CASE=text: only the cases whose names hold it (a convenience while debugging)
    if (const char *only = std::getenv("DIR_CASE"))
    {
        auto &cases = registry();
        cases.erase(std::remove_if(cases.begin(), cases.end(),
                                   [&](const Case &c) { return std::strstr(c.name, only) == nullptr; }),
                    cases.end());
    }
    return run_all();
}
