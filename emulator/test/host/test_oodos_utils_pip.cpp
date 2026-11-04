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
 *     and the archive bit; [C]'s question; passwords both sides;
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

    /// M:'s file (user, 8 + 3): its bytes, from its entries in extent order (EXM 1: an entry's
    /// records (EX & 1) * 128 + RC), its blocks' pointers 8-bit. Empty if there is none.
    std::vector<uint8_t> m_data(CcpRig &r, unsigned user, const std::string &name83)
    {
        std::vector<std::pair<unsigned, unsigned>> es; // extent, entry
        for (unsigned i = 0; i < 256; ++i)
        {
            const uint32_t a = MDISK + i * 32;
            bool match = r.phys(a) == user;
            for (unsigned c = 0; c < 11 && match; ++c)
                match = (r.phys(a + 1 + c) & 0x7F) == static_cast<uint8_t>(name83[c]);
            if (match)
                es.push_back({r.phys(a + 12) + 32u * r.phys(a + 14), i});
        }
        std::sort(es.begin(), es.end());
        std::vector<uint8_t> data;
        for (const auto &[ext, i] : es)
        {
            const uint32_t a = MDISK + i * 32;
            const unsigned records = (ext & 1u) * 128 + r.phys(a + 15), per = M_BLS / unsigned{RECORD};
            for (unsigned k = 0; k * per < records; ++k)
            {
                const unsigned block = r.phys(a + 16 + k);
                const unsigned n = std::min(per, records - k * per);
                for (uint32_t b = 0; b < n * unsigned{RECORD}; ++b)
                    data.push_back(r.phys(MDISK + block * M_BLS + b));
            }
        }
        return data;
    }

    /// The bytes as a string, to the first ^Z (a text file's end).
    std::string text(const std::vector<uint8_t> &d)
    {
        std::string s(d.begin(), d.end());
        return s.substr(0, s.find('\x1A'));
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

    /// PIP's line, and what it wrote.
    std::string pip(CcpRig &r, const std::string &tail, const std::string &prompt = "A>")
    {
        const std::string line = "m:pip " + tail;
        return wrote(r.cmd(line, prompt), line, prompt);
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

int main() { return run_all(); }
