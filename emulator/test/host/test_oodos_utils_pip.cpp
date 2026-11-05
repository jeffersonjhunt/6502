/**
 * @file test_oodos_utils_pip.cpp
 * @brief OODOS's PIP.COM (hw/oodos.md, "PIP, phase 4b2").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), PIP
 * (oodos/utils/pip.s, built by `make -C oodos utils`) put on M: as user 0's
 * SYS file, and run from the CCP as `M:PIP`. Its output is compared line for
 * line, and what it leaves on M: is read back behind OODOS's back: each
 * file's bytes from its entries' blocks, its attributes, its user.
 *
 *   - copies: one file, renamed on the way, the two short forms, [Gn] on
 *     either side, a wildcard (COPYING - and each name), a drive's files;
 *     joined files (a text's last record cut at its ^Z, [O] not); onto
 *     itself refused; FILE NOT FOUND; a read-only destination asked about
 *     (Y, N, [W]); SYS files only with [R]; attributes carried; [V]; [A]
 *     and the archive bit, in every entry of a file; [C]'s question;
 *     passwords both sides;
 *   - the '*' mode: the banner, a command a line, the empty line;
 *   - its errors; the return codes, by a conditional command after each.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string HELLO = HELLO_SAYS + " (none)\r\n";

    /// M:'s files: text in user 0 (one of three records, one with a ^Z inside), a binary one
    /// with ^Zs, a SYS one, a read-only one, one in user 3; then PIP.
    void files(CcpRig &r)
    {
        std::string three;
        for (int i = 0; three.size() < 300; ++i)
            three += "LINE " + std::to_string(i) + "\r\n";
        std::vector<uint8_t> bin;
        for (unsigned i = 0; i < 300; ++i)
            bin.push_back(static_cast<uint8_t>(i * 7 + (i % 5 == 0 ? 0x1A - (i * 7 & 0xFF) : 0)));
        r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")},
                 {0, "TWO     TXT", bytes("TWO\r\n")},
                 {0, "THREE   TXT", bytes(three)},
                 {0, "BIN     DAT", bin},
                 {0, "SYST    TXT", bytes("SYSTEM\r\n"), true},
                 {0, "RO      TXT", bytes("READ ONLY\r\n"), false, true},
                 {3, "OTHER   TXT", bytes("IN USER 3\r\n")}});
        r.put_util("pip");
    }

    /// M:'s entry bytes 9-11's bit 7: RO, SYS, archived; -1 if the file isn't there.
    int attrs(CcpRig &r, unsigned user, const std::string &name83)
    {
        const int e = r.m_entry(user, name83);
        if (e < 0)
            return -1;
        const uint32_t a = MDISK + static_cast<uint32_t>(e) * 32;
        return (r.phys(a + 9) >> 7) | (r.phys(a + 10) >> 7) << 1 | (r.phys(a + 11) >> 7) << 2;
    }

    /// What the command wrote: the output less its echo, an LF the CCP owed, and the prompt.
    std::string wrote(const std::string &o, const std::string &line, const std::string &prompt = "A>")
    {
        const std::string head = line + "\r", end = "\r\n" + prompt;
        if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
            o.compare(o.size() - end.size(), end.size(), end) != 0)
            return "<" + vis(o) + ">";
        std::string s = o.substr(head.size(), o.size() - head.size() - end.size());
        if (!s.empty() && s[0] == '\n')
            s.erase(0, 1);
        return s;
    }

    /// PIP's line, and what it wrote. The CCP echoes a ^Z typed in it as "^Z".
    std::string pip(CcpRig &r, const std::string &tail, const std::string &prompt = "A>")
    {
        const std::string line = "m:pip " + tail;
        std::string echo;
        for (const char c : line)
            echo += c == '\x1A' ? std::string("^Z") : std::string(1, c);
        return wrote(r.cmd(line, prompt), echo, prompt);
    }

    void ok(CcpRig &r, const std::string &prompt = "A>") { same(r.cmd(":hello", prompt), ":hello\r\n" + HELLO + "\r\n" + prompt); }
    void failed(CcpRig &r, const std::string &prompt = "A>") { same(r.cmd(":hello", prompt), ":hello\r\r\n" + prompt); }
} // namespace

TEST(pip_copies_a_file_renamed_or_not_and_by_both_short_forms)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    same(pip(r, "m:copy.txt=m:one.txt"), "");
    ok(r);
    same(text(m_data(r, 0, "COPY    TXT")), "ONE\r\n");
    same(text(m_data(r, 0, "ONE     TXT")), "ONE\r\n"); // the source as it was
    CHECK(r.m_entry(0, "COPY    $$$") < 0);           // no temporary file left
    // The short forms: to another user's same name, both ways round
    same(pip(r, "m:[g2]=m:three.txt"), "");
    CHECK(m_data(r, 2, "THREE   TXT") == m_data(r, 0, "THREE   TXT"));
    same(pip(r, "m:two.txt[g4]=m:"), "");
    same(text(m_data(r, 4, "TWO     TXT")), "TWO\r\n");
    // A binary file's records as they are, ^Zs and all
    same(pip(r, "m:bin2.dat=m:bin.dat"), "");
    CHECK(m_data(r, 0, "BIN2    DAT") == m_data(r, 0, "BIN     DAT"));
    // From another user, by [G] on the source
    same(pip(r, "m:mine.txt=m:other.txt[g3]"), "");
    same(text(m_data(r, 0, "MINE    TXT")), "IN USER 3\r\n");
}

TEST(pip_copies_a_wildcards_files_each_under_its_name_and_joins_files)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // Each match to user 1, its name shown; SYS files passed over without [R]
    same(pip(r, "m:[g1]=m:*.txt"), "COPYING -\r\nONE.TXT\r\nTWO.TXT\r\nTHREE.TXT\r\nRO.TXT");
    ok(r);
    same(text(m_data(r, 1, "ONE     TXT")), "ONE\r\n");
    CHECK(r.m_entry(1, "SYST    TXT") < 0);
    CHECK_EQ(attrs(r, 1, "RO      TXT"), 1); // its RO carried, archive clear
    same(pip(r, "m:[g1]=m:syst.txt[r]"), "");
    CHECK_EQ(attrs(r, 1, "SYST    TXT"), 2);
    // Joined: a text's last record cut at its ^Z, so the next follows on
    same(pip(r, "m:all.txt=m:one.txt,m:two.txt,m:one.txt"), "");
    same(text(m_data(r, 0, "ALL     TXT")), "ONE\r\nTWO\r\nONE\r\n");
    CHECK_EQ(m_data(r, 0, "ALL     TXT").size(), size_t{128});
    std::vector<uint8_t> two = m_data(r, 0, "BIN     DAT"), got = m_data(r, 0, "BIN     DAT");
    two.insert(two.end(), got.begin(), got.end());
    same(pip(r, "m:bins.dat=m:bin.dat[o],m:bin.dat[o]"), ""); // [O]: binary, records whole
    CHECK(m_data(r, 0, "BINS    DAT") == two);
    // Errors
    same(pip(r, "m:one.txt=m:one.txt"), "INVALID FORMAT"); // onto itself
    failed(r);
    same(pip(r, "m:=m:one.txt"), "INVALID FORMAT");
    same(pip(r, "m:x.txt=m:nosuch.txt"), "FILE NOT FOUND - NOSUCH.TXT");
    failed(r);
    CHECK(r.m_entry(0, "X       $$$") < 0); // the temporary file deleted
    same(pip(r, "x.txt"), "INVALID FORMAT");
    same(pip(r, "x.txt="), "INVALID FORMAT");
    same(pip(r, "m:x.txt=m:one.txt[q]"), "BAD PARAMETER");
    same(pip(r, "m:x.txt=m:one.txt[g16]"), "INVALID USER NUMBER");
    same(pip(r, "m:x.txt[v]=m:one.txt"), "BAD PARAMETER"); // the destination takes G alone
    same(pip(r, "m:x.txt=m:one.txt two.txt"), "INVALID SEPARATOR");
    same(pip(r, "m:*.txt=m:one.txt"), "INVALID FORMAT");
    same(pip(r, "m:x.txt=m:*.txt"), "INVALID FORMAT");
    same(pip(r, "lst:=nul:,m:one.txt,eof:"), ""); // devices: the next stage's
}

TEST(pip_asks_before_a_read_only_destination_unless_w_and_verifies_with_v)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // N: not deleted, the copy dropped, the old file as it was
    std::string o = r.to("m:pip m:ro.txt=m:one.txt", "DELETE (Y/N)?");
    same(wrote(o + r.cmd("n"), "m:pip m:ro.txt=m:one.txt"), "DESTINATION IS R/O, DELETE (Y/N)?n\r\n**NOT DELETED**");
    failed(r);
    same(text(m_data(r, 0, "RO      TXT")), "READ ONLY\r\n");
    CHECK(r.m_entry(0, "RO      $$$") < 0);
    // Y: deleted, the copy in its place (its source's attributes, none)
    o = r.to("m:pip m:ro.txt=m:one.txt", "DELETE (Y/N)?");
    same(wrote(o + r.cmd("y"), "m:pip m:ro.txt=m:one.txt"), "DESTINATION IS R/O, DELETE (Y/N)?y\r"); // the answer's CR, then the prompt's
    ok(r);
    same(text(m_data(r, 0, "RO      TXT")), "ONE\r\n");
    CHECK_EQ(attrs(r, 0, "RO      TXT"), 0);
    // A read-only copy over it, then [W] writes over it without asking
    same(pip(r, "m:[g5]=m:ro.txt"), "");
    same(pip(r, "m:ro.txt[g5]=m:two.txt[w]"), "");
    same(text(m_data(r, 5, "RO      TXT")), "TWO\r\n");
    // [V]: read back and compared
    same(pip(r, "m:v.txt=m:three.txt[v]"), "");
    ok(r);
    CHECK(m_data(r, 0, "V       TXT") == m_data(r, 0, "THREE   TXT"));
}

TEST(pip_asks_with_c_and_copies_only_unarchived_files_with_a)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // [C]: each asked about, Y copied, N not
    std::string line = "m:pip m:[g6]=m:t*.txt[c]";
    std::string o = r.to(line, "COPY TWO.TXT (Y/N)?"); // in the directory's order
    o += r.to("n", "COPY THREE.TXT (Y/N)?");
    o += r.cmd("y");
    same(wrote(o, line), "COPY TWO.TXT (Y/N)?n\r\nCOPY THREE.TXT (Y/N)?y\r");
    CHECK(r.m_entry(6, "TWO     TXT") < 0);
    CHECK(m_data(r, 6, "THREE   TXT") == m_data(r, 0, "THREE   TXT"));
    // [A]: the files not archived copied, each source then archived; again, none
    same(pip(r, "m:[g7]=m:*.txt[a]"), "COPYING -\r\nONE.TXT\r\nTWO.TXT\r\nTHREE.TXT\r\nRO.TXT");
    CHECK_EQ(attrs(r, 0, "ONE     TXT"), 4);
    CHECK_EQ(attrs(r, 0, "RO      TXT"), 5);
    CHECK_EQ(attrs(r, 7, "ONE     TXT"), 0); // the copy not archived
    same(pip(r, "m:[g8]=m:*.txt[a]"), "FILE NOT FOUND - ????????.TXT");
}

TEST(pip_with_a_copies_a_file_any_of_whose_entries_isnt_archived)
{
    // The BDOS clears t3' only in the entry of the extent a write wrote (the Guide, 2.3.4: a file
    // is archived if all its entries are), so [A] looks at every entry, and names a file once.
    // CHG.DAT and BOTH.DAT have two entries each (256 records, then extent 2's): CHG.DAT's first
    // archived, its second not, as a write to its record 256 leaves it; neither of BOTH.DAT's.
    // KEPT.DAT is archived, NEW.TXT not. Copied to B:, as M: hasn't room for the copies too.
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<uint8_t> big(256 * RECORD + 5);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 13 + i / RECORD);
    auto archived = [&](unsigned e, bool on) {
        if (on)
            r.poke(MDISK + e * 32 + 11, static_cast<uint8_t>(r.phys(MDISK + e * 32 + 11) | 0x80));
    };
    auto two_entries = [&](const std::string &name83, bool first_archived, bool second_archived) {
        r.put_m({{0, name83, std::vector<uint8_t>(big.begin(), big.begin() + 256 * RECORD)}});
        const unsigned first = r.entry;
        const unsigned at = r.free_entry();
        std::vector<uint8_t> e(32, 0);
        for (unsigned i = 0; i < 12; ++i)
            e[i] = r.phys(MDISK + first * 32 + i);
        e[12] = 2; // extent 2: record 256 on
        e[15] = 1;
        e[16] = static_cast<uint8_t>(r.block);
        for (uint32_t i = 0; i < M_BLS; ++i)
            r.poke(MDISK + r.block * M_BLS + i, i < 5 ? big[256 * RECORD + i] : uint8_t{0x1A});
        ++r.block;
        r.poke(MDISK + at * 32, e);
        archived(first, first_archived);
        archived(at, second_archived);
        return std::pair<unsigned, unsigned>{first, at};
    };
    r.put_m({{0, "KEPT    DAT", bytes("KEPT\r\n")}});
    archived(r.entry, true);
    const auto chg = two_entries("CHG     DAT", true, false), both = two_entries("BOTH    DAT", false, false);
    r.put_m({{0, "NEW     TXT", bytes("NEW\r\n")}});
    r.put_util("pip");
    same(pip(r, "b:=m:*.*[a]"), "COPYING -\r\nCHG.DAT\r\nBOTH.DAT\r\nNEW.TXT"); // each once
    for (const auto &[a, b] : {chg, both}) // archived after: both entries (30)
    {
        CHECK_EQ(r.phys(MDISK + a * 32 + 11) & 0x80, 0x80);
        CHECK_EQ(r.phys(MDISK + b * 32 + 11) & 0x80, 0x80);
    }
    same(pip(r, "b:=m:*.*[a]"), "FILE NOT FOUND - ????????.???");
    same(pip(r, "m:chk.dat=b:chg.dat"), ""); // B:'s copy, back on M: (KEPT.DAT's room): all 257 records
    std::vector<uint8_t> want(big.begin(), big.end());
    want.resize(257 * RECORD, 0x1A);
    CHECK(m_data(r, 0, "CHK     DAT") == want);
}

TEST(pip_takes_a_sources_password_and_gives_a_destination_one)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "OPEN    TXT", bytes("OPEN\r\n")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  ");
    r.put_util("pip");
    std::string o = r.cmd("m:pip m:x.txt=m:secret.txt");
    CHECK(o.find("OODOS Error On M: Password Error") != std::string::npos);
    CHECK(r.m_entry(0, "X       TXT") < 0);
    CHECK(r.m_entry(0, "X       $$$") < 0);
    failed(r);
    same(pip(r, "m:x.txt=m:secret.txt;sesame"), "");
    same(text(m_data(r, 0, "X       TXT")), "HIDDEN\r\n");
    // A destination's: its XFCB, READ mode
    same(pip(r, "m:locked.txt;key=m:open.txt"), "");
    const int x = r.m_entry(16, "LOCKED  TXT");
    REQUIRE(x >= 0);
    CHECK_EQ(r.phys(MDISK + static_cast<uint32_t>(x) * 32 + 12) & 0xE0, 0x80);
    o = r.cmd("m:pip m:y.txt=m:locked.txt");
    CHECK(o.find("Password Error") != std::string::npos);
    same(pip(r, "m:y.txt=m:locked.txt;key"), "");
    same(text(m_data(r, 0, "Y       TXT")), "OPEN\r\n");
    // A named destination without one: none, though its source has one (the Guide)
    same(pip(r, "m:named.txt=m:secret.txt;sesame"), "");
    CHECK(r.m_entry(16, "NAMED   TXT") < 0);
    // To another user by d: alone: the source's password and mode
    same(pip(r, "m:[g2]=m:secret.txt;sesame"), "");
    const int y = r.m_entry(18, "SECRET  TXT");
    REQUIRE(y >= 0);
    CHECK_EQ(r.phys(MDISK + static_cast<uint32_t>(y) * 32 + 12) & 0xE0, 0x80);
}

TEST(pip_alone_asks_for_a_command_a_line_until_an_empty_one)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    std::string o = r.to("m:pip", "\r\n*");
    o += r.to("m:a.txt=m:one.txt", "\r\n*");
    o += r.to("m:b.txt=m:nosuch.txt", "\r\n*");
    o += r.cmd("");
    same(o, "m:pip\r\nOODOS PIP 0.3\r\n*m:a.txt=m:one.txt\r\n*m:b.txt=m:nosuch.txt\r\nFILE NOT FOUND - NOSUCH.TXT\r\n*\r\r\nA>");
    same(text(m_data(r, 0, "A       TXT")), "ONE\r\n");
    failed(r); // an error among them: the return code
    // A ^C at '*' (function 10's warm boot) after a copy to user 5: the CCP's user again (the CCP
    // makes its own drive and user current after any program)
    o = r.to("m:pip", "\r\n*");
    o += r.to("m:[g5]=m:two.txt", "\r\n*");
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), r.out.size() - 2));
    same(r.cmd("dir m:two.txt"), "dir m:two.txt\r\nM: TWO      TXT\r\nA>"); // still user 0
    CHECK(r.m_entry(5, "TWO     TXT") >= 0);
}

namespace
{
    /// Text files for the character options.
    void texts(CcpRig &r)
    {
        r.put_m({{0, "MIXED   TXT", bytes("Hello, World\r\nSecond line\r\n")},
                 {0, "TABS    TXT", bytes("A\tB\tC\r\nxy\tz\r\n")},
                 {0, "LONG    TXT", bytes("0123456789ABCDEF\r\nshort\r\n")},
                 {0, "FEEDS   TXT", bytes("PAGE1\r\n\x0CPAGE2\r\n")},
                 {0, "HIGH    TXT", bytes("\xC1\xC2\xC3\r\n")},
                 {0, "LETTER  TXT", bytes("To whom.\r\nDEAR SIR, it is so.\r\nSINCERELY, me\r\nP.S.\r\n")},
                 {0, "GOOD    HEX", bytes(":03000000010203F7\r\n:00000001FF\r\n")},
                 {0, "BADSUM  HEX", bytes(":03000000010203F8\r\n:00000001FF\r\n")},
                 {0, "BADDIG  HEX", bytes(":03000000010G03F7\r\n")},
                 {0, "NOEND   HEX", bytes(":03000000010203F7\r\n")}});
        r.put_util("pip");
    }

    std::string m_text(CcpRig &r, const std::string &name83) { return text(m_data(r, 0, name83)); }
} // namespace

TEST(pip_copies_to_and_from_the_devices)
{
    CcpRig r;
    REQUIRE(r.ok);
    texts(r);
    same(pip(r, "con:=m:mixed.txt"), "Hello, World\r\nSecond line\r\n");
    ok(r);
    same(pip(r, "lst:=m:mixed.txt"), ""); // LST: is LPT, which takes and drops
    same(pip(r, "prn:=m:mixed.txt"), "");
    // NUL: 64 zeros, EOF: a ^Z: a file's end
    same(pip(r, "m:z.dat=nul:,eof:"), "");
    std::vector<uint8_t> z = m_data(r, 0, "Z       DAT");
    REQUIRE(z.size() == 128);
    CHECK(std::all_of(z.begin(), z.begin() + 64, [](uint8_t b) { return b == 0; }));
    CHECK_EQ(z[64], 0x1A);
    // CON: typed, to a ^Z (OODOS's console takes a typed LF as a CR)
    const size_t from = r.out.size();
    r.type("m:pip m:typed.txt=con:\r");
    REQUIRE(r.until("con:\r\n", ms(3000), from));
    r.type("hi there\rbye\x1A");
    CHECK(r.until("\r\nA>", ms(8000), from + 24));
    same(text(m_data(r, 0, "TYPED   TXT")), "hi there\rbye");
    // Errors: NUL: and EOF: are sources, LST: and PRN: destinations
    same(pip(r, "nul:=m:mixed.txt"), "INVALID DESTINATION");
    failed(r);
    same(pip(r, "m:x.txt=lst:"), "INVALID SOURCE");
    same(pip(r, "m:x.txt=xyz:"), "INVALID SOURCE");
    same(pip(r, "xyz:=m:mixed.txt"), "INVALID DESTINATION");
}

TEST(pip_changes_the_characters_as_its_options_say)
{
    CcpRig r;
    REQUIRE(r.ok);
    texts(r);
    same(pip(r, "m:o.txt=m:mixed.txt[u]"), "");
    same(m_text(r, "O       TXT"), "HELLO, WORLD\r\nSECOND LINE\r\n");
    same(pip(r, "m:o.txt=m:mixed.txt[l]"), "");
    same(m_text(r, "O       TXT"), "hello, world\r\nsecond line\r\n");
    same(pip(r, "m:o.txt=m:tabs.txt[t4]"), "");
    same(m_text(r, "O       TXT"), "A   B   C\r\nxy  z\r\n");
    same(pip(r, "m:o.txt=m:long.txt[d10]"), "");
    same(m_text(r, "O       TXT"), "0123456789\r\nshort\r\n");
    same(pip(r, "m:o.txt=m:feeds.txt[f]"), "");
    same(m_text(r, "O       TXT"), "PAGE1\r\nPAGE2\r\n");
    same(pip(r, "m:o.txt=m:high.txt[z]"), "");
    same(m_text(r, "O       TXT"), "ABC\r\n");
    same(pip(r, "m:o.txt=m:mixed.txt[n]"), "");
    same(m_text(r, "O       TXT"), "     1: Hello, World\r\n     2: Second line\r\n");
    same(pip(r, "m:o.txt=m:mixed.txt[n2]"), "");
    same(m_text(r, "O       TXT"), "000001\tHello, World\r\n000002\tSecond line\r\n");
    same(pip(r, "m:o.txt=m:mixed.txt[n2t8]"), ""); // N2's tab expanded by T
    same(m_text(r, "O       TXT"), "000001  Hello, World\r\n000002  Second line\r\n");
    same(pip(r, "m:o.txt=m:mixed.txt[p1]"), ""); // a form feed first, and every 60 lines
    same(m_text(r, "O       TXT"), "\x0CHello, World\r\nSecond line\r\n");
    same(pip(r, "m:o.txt=m:feeds.txt[fp2]"), "");
    same(m_text(r, "O       TXT"), "\x0CPAGE1\r\nPAGE2\r\n\x0C");
    // S and Q: from the start string to the quit string, both kept (the tail in capitals)
    same(pip(r, "m:o.txt=m:letter.txt[sdear sir\x1Aqsincerely\x1A]"), "");
    same(m_text(r, "O       TXT"), "DEAR SIR, it is so.\r\nSINCERELY");
    same(pip(r, "m:o.txt=m:letter.txt[snothing\x1A]"), "START NOT FOUND");
    failed(r);
    same(pip(r, "m:o.txt=m:letter.txt[qnothing\x1A]"), "QUIT NOT FOUND");
    same(pip(r, "m:o.txt=m:letter.txt[s\x1A]"), "BAD PARAMETER"); // an empty string (ours)
    same(pip(r, "m:o.txt=m:letter.txt[snothing]"), "BAD PARAMETER"); // no ^Z
    // E: echoed as it is written
    same(pip(r, "m:o.txt=m:mixed.txt[eu]"), "HELLO, WORLD\r\nSECOND LINE\r\n");
    // Joined by characters: each to its ^Z, one ^Z at the end
    same(pip(r, "m:o.txt=m:mixed.txt[u],m:long.txt[d4]"), "");
    same(m_text(r, "O       TXT"), "HELLO, WORLD\r\nSECOND LINE\r\n0123\r\nshor\r\n");
}

TEST(pip_checks_hex_records_with_h_and_drops_00_records_with_i)
{
    CcpRig r;
    REQUIRE(r.ok);
    texts(r);
    same(pip(r, "m:o.hex=m:good.hex[h]"), "");
    same(m_text(r, "O       HEX"), ":03000000010203F7\r\n:00000001FF\r\n");
    ok(r);
    same(pip(r, "m:o.hex=m:badsum.hex[h]"), "CHECKSUM ERROR - BADSUM.HEX");
    failed(r);
    same(pip(r, "m:o.hex=m:baddig.hex[h]"), "INVALID DIGIT - BADDIG.HEX");
    same(pip(r, "m:o.hex=m:noend.hex[h]"), "UNEXPECTED END OF HEX FILE - NOEND.HEX");
    same(pip(r, "m:o.hex=m:good.hex[i],m:good.hex[h]"), ""); // I: the first one's :00 record dropped
    same(m_text(r, "O       HEX"), ":03000000010203F7\r\n:03000000010203F7\r\n:00000001FF\r\n");
}

TEST(pip_copies_files_past_an_extent_and_says_a_full_disk_a_read_only_temporary_file_and_a_ctrl_c)
{
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<uint8_t> big(256 * RECORD);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 13 + (i >> 7));
    r.put_m({{0, "BIG     DAT", big},
             {0, "LOCK    $$$", bytes("x"), false, true},
             {0, "LINE    HEX", bytes(":" + std::string(90, '0') + "\r\n")},
             {0, "LETTERS TXT", std::vector<uint8_t>(64 * RECORD, 'a')}});
    r.put_util("pip");
    // 32 KB, two logical extents in one entry; joined twice, 64 KB in two entries
    same(pip(r, "m:big2.dat=m:big.dat"), "");
    CHECK(m_data(r, 0, "BIG2    DAT") == big);
    same(r.cmd("era m:big2.dat"), "era m:big2.dat\r\r\nA>"); // room for 64 KB more
    same(pip(r, "m:big3.dat=m:big.dat[o],m:big.dat[o]"), "");
    std::vector<uint8_t> two = big;
    two.insert(two.end(), big.begin(), big.end());
    CHECK(m_data(r, 0, "BIG3    DAT") == two);
    // M: has no room for 96 KB more: the temporary file gone, nothing left behind
    same(pip(r, "m:full.dat=m:big.dat[o],m:big.dat[o],m:big.dat[o]"), "DISK WRITE ERROR - FULL.DAT");
    failed(r);
    CHECK(r.m_entry(0, "FULL    $$$") < 0);
    CHECK(r.m_entry(0, "FULL    DAT") < 0);
    // A read-only NAME.$$$ in the way
    same(pip(r, "m:lock.txt=m:big.dat"), "CAN'T DELETE TEMP FILE");
    // H: a line over 80 characters
    same(pip(r, "m:o.hex=m:line.hex[h]"), "RECORD TOO LONG - LINE.HEX");
    // ^C during a copy (a second into it, PIP running: one typed while the CCP loads it waits for
    // the CCP, as in CP/M): USER ABORTED, the temporary file deleted
    same(r.cmd("era m:big3.dat"), "era m:big3.dat\r\r\nA>");
    const size_t from = r.out.size();
    r.type("m:pip m:c.dat=m:letters.txt[e]\r"); // E: each character echoed, so it takes seconds
    r.run(ms(1000));
    r.type("\x03");
    CHECK(r.until("USER ABORTED\r\nA>", ms(20000), from));
    CHECK(r.m_entry(0, "C       $$$") < 0);
    CHECK(r.m_entry(0, "C       DAT") < 0);
    // AUX: is the serial line
    same(pip(r, "aux:=m:lock.$$$"), "x");
}

TEST(pip_skips_a_file_it_cant_open_in_a_copy_to_a_drive)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  ");
    r.put_util("pip");
    // The Guide: a file whose password fails is skipped and noted, the copy goes on
    const std::string o = pip(r, "m:[g2]=m:*.txt");
    CHECK(o.find("COPYING -\r\nONE.TXT\r\nSECRET.TXT\r\nOODOS Error On M: Password Error\r\n") == 0);
    CHECK(o.size() >= 7 && o.compare(o.size() - 7, 7, "TWO.TXT") == 0);
    failed(r);
    same(text(m_data(r, 2, "ONE     TXT")), "ONE\r\n");
    same(text(m_data(r, 2, "TWO     TXT")), "TWO\r\n");
    CHECK(r.m_entry(2, "SECRET  TXT") < 0);
    CHECK(r.m_entry(2, "SECRET  $$$") < 0);
}

TEST(pip_says_a_full_directory_as_a_write_error)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}});
    r.put_util("pip");
    // Every entry M: has left taken by an empty file, before M: is logged in (OODOS hashes its
    // directory then): no room for NAME.$$$
    for (unsigned i = 0, n = 0; i < 256; ++i)
        if (r.phys(MDISK + i * 32) == 0xE5)
        {
            std::vector<uint8_t> e(32, 0);
            char name[12];
            std::snprintf(name, sizeof name, "FILL%04uDAT", n++);
            for (unsigned c = 0; c < 11; ++c)
                e[1 + c] = static_cast<uint8_t>(name[c]);
            r.poke(MDISK + i * 32, e);
        }
    same(pip(r, "m:x.txt=m:one.txt"), "DISK WRITE ERROR - X.TXT");
    failed(r);
}

int main() { return run_all(); }
