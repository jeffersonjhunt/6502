/**
 * @file test_oodos_utils_set.cpp
 * @brief OODOS's SET and INITDIR (hw/oodos.md, "The utilities, phase 4b"), run from the CCP.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the
 * built SET.COM and INITDIR.COM put on M: as user 0's SYS files with the
 * files a case needs, behind OODOS's back before M: is logged in, and the
 * search chain the default drive, then M:. Each command line is typed at
 * the CCP and what the utility writes compared line for line with the
 * plan; what it leaves on M: is read back behind OODOS's back, byte for
 * byte, and then seen through the BDOS (DIR, TYPE, ERA, a password then
 * needed, a stamp then made).
 *
 *   - SET's file attributes (30): every extent, a wildcard, its lines;
 *   - SET's errors, each its message, the return code $FF00, nothing
 *     changed;
 *   - SET d: [RO] and [RW], another drive and the current;
 *   - SET's label options (100): the name, its password asked for and
 *     checked, protection, the stamp modes; a label made where none was;
 *   - SET's file passwords and modes (103), passwords off, a mode without
 *     a password, the default password (106);
 *   - INITDIR on a directory without SFCBs (entries in SFCB places moved,
 *     the XFCBs' modes in the slots), one nearly full and one too full,
 *     the answer N; one with SFCBs: recovered, stamps cleared, left; its
 *     errors: no drive, a read-only drive, too little memory (the TPA
 *     made small, as an RSX below the BDOS would), the BIOS's read and
 *     write failing on a card's drive.
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
    constexpr unsigned M_ENTRIES = 256;

    /// Lines joined as a utility writes them: separated by CR LF, the last not ended.
    std::string lines(const std::vector<std::string> &ls)
    {
        std::string s;
        for (size_t i = 0; i < ls.size(); ++i)
            s += (i ? "\r\n" : "") + ls[i];
        return s;
    }

    /// `line` typed at the CCP: what the program wrote between its line and the prompt.
    std::string run(CcpRig &r, const std::string &line, const std::string &prompt = "A>")
    {
        const std::string o = r.cmd(line, prompt);
        const std::string head = line + "\r\n", end = "\r\n" + prompt;
        if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
            o.compare(o.size() - end.size(), end.size(), end) != 0)
            return "<" + vis(o) + ">";
        return o.substr(head.size(), o.size() - head.size() - end.size());
    }

    /// The last command's return code a success? (`:` runs HELLO.COM, from A:, only then.)
    bool succeeded(CcpRig &r, const std::string &prompt = "A>")
    {
        return r.cmd(":hello", prompt).find("Hello from OODOS") != std::string::npos;
    }

    /// SET.COM and INITDIR.COM on M:, the search chain the default drive and M:.
    void tools(CcpRig &r)
    {
        r.put_util("set");
        r.put_util("initdir");
        r.chain({0, 13});
    }

    /// M:'s directory, behind OODOS's back.
    std::vector<uint8_t> mdir(CcpRig &r)
    {
        std::vector<uint8_t> d(M_ENTRIES * 32);
        for (uint32_t i = 0; i < d.size(); ++i)
            d[i] = r.phys(MDISK + i);
        return d;
    }
    uint8_t mbyte(CcpRig &r, int entry, unsigned byte)
    {
        CHECK(entry >= 0);
        if (entry < 0)
            return 0;
        return r.phys(MDISK + static_cast<uint32_t>(entry) * 32 + byte);
    }
    std::vector<uint8_t> mbytes(CcpRig &r, int entry, unsigned from, unsigned n)
    {
        std::vector<uint8_t> b;
        for (unsigned i = 0; i < n; ++i)
            b.push_back(mbyte(r, entry, from + i));
        return b;
    }
    /// The SFCB slot of M:'s entry `e`: its 10 bytes (the two stamps, the mode, a reserved byte).
    std::vector<uint8_t> slot(CcpRig &r, int e) { return mbytes(r, e | 3, 1 + 10 * (e & 3), 10); }

    /// M:'s XFCB of a name (user + 16), -1 if none.
    int m_xfcb(CcpRig &r, unsigned user, const std::string &name83) { return r.m_entry(user + 16, name83); }

    /// A directory entry, 32 bytes: the user, the name, EX, RC, no blocks.
    std::vector<uint8_t> entry(unsigned user, const std::string &name83, uint8_t ex = 0, uint8_t rc = 0)
    {
        std::vector<uint8_t> e(32, 0);
        e[0] = static_cast<uint8_t>(user);
        std::copy(name83.begin(), name83.end(), e.begin() + 1);
        e[12] = ex;
        e[15] = rc;
        return e;
    }
    /// A label entry: its name, data byte and password (kept as the BDOS keeps it), no stamps.
    std::vector<uint8_t> label(const std::string &name83, uint8_t mode, const std::string &pw8 = "        ")
    {
        std::vector<uint8_t> e = entry(0x20, name83);
        e[12] = mode;
        const std::vector<uint8_t> k = kept_pw(pw8);
        std::copy(k.begin(), k.end(), e.begin() + 13);
        return e;
    }

    /// M:'s directory made a CP/M 2.2-style one, behind OODOS's back before M: is logged in:
    /// every entry free (no SFCBs), then `lbl` at entry `at` (none if empty); r.entry 0, so put_m()
    /// and put_util() take entries 1, 2, ... from there.
    void plain_m(CcpRig &r, const std::vector<uint8_t> &lbl = {}, unsigned at = 0)
    {
        for (uint32_t i = 0; i < M_ENTRIES * 32; ++i)
            r.poke(MDISK + i, 0xE5);
        if (!lbl.empty())
            r.poke(MDISK + at * 32, lbl);
        r.entry = 0;
    }

    /// What INITDIR should make of a directory without SFCBs: the entries in SFCB places (3, 7,
    /// ...) moved, lowest first, to the lowest free entries outside them; then each fourth entry
    /// an SFCB, its stamps 0, each slot's mode the XFCB's (the first of the name, user + 16) of the
    /// file whose extent-zero entry (EX's bits above EXM 1 clear, S2 0) is the slot's.
    std::vector<uint8_t> reformatted(std::vector<uint8_t> d)
    {
        const unsigned n = static_cast<unsigned>(d.size() / 32);
        std::vector<unsigned> free;
        for (unsigned i = 0; i < n; ++i)
            if (i % 4 != 3 && d[i * 32] == 0xE5)
                free.push_back(i);
        size_t f = 0;
        for (unsigned i = 3; i < n; i += 4)
            if (d[i * 32] != 0xE5 && d[i * 32] != 0x21)
            {
                CHECK(f < free.size());
                if (f >= free.size())
                    return d;
                std::copy(d.begin() + i * 32, d.begin() + i * 32 + 32, d.begin() + free[f++] * 32);
            }
        const std::vector<uint8_t> before = d;
        for (unsigned g = 0; g < n / 4; ++g)
        {
            uint8_t *s = &d[(g * 4 + 3) * 32];
            std::fill(s, s + 32, 0);
            s[0] = 0x21;
            for (unsigned k = 0; k < 3; ++k)
            {
                const uint8_t *e = &d[(g * 4 + k) * 32];
                if (e[0] >= 16 || (e[12] & 0x1E) != 0 || (e[14] & 0x3F) != 0)
                    continue;
                for (unsigned x = 0; x < n; ++x)
                {
                    const uint8_t *xe = &before[x * 32];
                    if (x % 4 == 3 || xe[0] != e[0] + 16)
                        continue;
                    bool same = true;
                    for (unsigned c = 1; c < 12 && same; ++c)
                        same = (xe[c] & 0x7F) == (e[c] & 0x7F);
                    if (same)
                    {
                        s[1 + 10 * k + 8] = xe[12] & 0xE0;
                        break;
                    }
                }
            }
        }
        return d;
    }

    /// The directory's entries but SFCBs and free ones, in order: what no move may lose.
    std::vector<std::vector<uint8_t>> kept(const std::vector<uint8_t> &d)
    {
        std::vector<std::vector<uint8_t>> out;
        for (size_t i = 0; i < d.size(); i += 32)
            if (d[i] != 0xE5 && d[i] != 0x21)
                out.emplace_back(d.begin() + static_cast<long>(i), d.begin() + static_cast<long>(i) + 32);
        std::sort(out.begin(), out.end());
        return out;
    }

    /// A card whose reads or writes fail while `rfail` or `wfail` is set, as a card that has gone
    /// bad would (test_oodos_sys's FaultyMedia, with reads).
    struct FaultyMedia : MemoryMedia
    {
        using MemoryMedia::MemoryMedia;
        bool rfail{false}, wfail{false};
        bool read(uint64_t lba, uint8_t *out) override { return !rfail && MemoryMedia::read(lba, out); }
        bool write(uint64_t lba, const uint8_t *in) override { return !wfail && MemoryMedia::write(lba, in); }
    };
} // namespace

// ---- SET: a file's attributes ----------------------------------------------------------------

TEST(set_gives_files_their_attributes_in_every_extent_and_says_each_file_done)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")},
             {0, "TWO     TXT", bytes("TWO\r\n")},
             {0, "THREE   DAT", bytes("3")},
             {3, "ONE     TXT", bytes("USER 3\r\n")},
             {0, "BIG     DAT", std::vector<uint8_t>(256 * RECORD, 'B')}}); // EX 1, RC 128: extents 0-1
    // BIG.DAT's third extent, a second entry (EX 2), its own block
    const unsigned second = r.free_entry();
    std::vector<uint8_t> e = entry(0, "BIG     DAT", 2, 1);
    e[16] = static_cast<uint8_t>(r.block++);
    r.poke(MDISK + second * 32, e);
    const int one = r.m_entry(0, "ONE     TXT"), two = r.m_entry(0, "TWO     TXT");
    const int one3 = r.m_entry(3, "ONE     TXT"), big = r.m_entry(0, "BIG     DAT");

    same(run(r, "set m:one.txt [ro sys]"), "M:ONE.TXT set to System (SYS), Read Only (RO)");
    CHECK_EQ(mbyte(r, one, 9) & 0x80, 0x80);  // t1': RO
    CHECK_EQ(mbyte(r, one, 10) & 0x80, 0x80); // t2': SYS
    CHECK_EQ(mbyte(r, one3, 9) & 0x80, 0);    // another user's
    CHECK(succeeded(r));
    // The BDOS sees them: DIR lists it no more, ERA is refused
    same(r.cmd("dir m:"), "dir m:\r\nM: TWO      TXT : THREE    DAT : BIG      DAT\r\nSYSTEM FILE(S) EXIST\r\nA>");
    CHECK(r.cmd("era m:one.txt").find("OODOS Error On M: File R/O") != std::string::npos);
    CHECK(r.m_entry(0, "ONE     TXT") >= 0);
    // The table's order, whatever the list's; by M:SET too
    same(run(r, "m:set m:one.txt [rw,dir]"), "M:ONE.TXT set to Directory (DIR), Read Write (RW)");
    CHECK_EQ(mbyte(r, one, 9) & 0x80, 0);
    CHECK_EQ(mbyte(r, one, 10) & 0x80, 0);
    // A wildcard: each of the current user's files, in the directory's order
    same(run(r, "set m:*.txt [f4=on archive=on,f2=off f1=on]"),
         lines({"M:ONE.TXT set to Archive=ON, F1=ON, F2=OFF, F4=ON", "M:TWO.TXT set to Archive=ON, F1=ON, F2=OFF, F4=ON"}));
    for (const int f : {one, two})
    {
        CHECK_EQ(mbyte(r, f, 11) & 0x80, 0x80); // t3': archived
        CHECK_EQ(mbyte(r, f, 1) & 0x80, 0x80);  // f1'
        CHECK_EQ(mbyte(r, f, 2) & 0x80, 0);
        CHECK_EQ(mbyte(r, f, 4) & 0x80, 0x80); // f4'
        CHECK_EQ(mbyte(r, f, 3) & 0x80, 0);
    }
    CHECK_EQ(mbyte(r, one3, 11) & 0x80, 0);
    same(run(r, "set m:one.txt [ar=off,f3=on]"), "M:ONE.TXT set to Archive=OFF, F3=ON");
    CHECK_EQ(mbyte(r, one, 11) & 0x80, 0);
    CHECK_EQ(mbyte(r, one, 3) & 0x80, 0x80);
    CHECK_EQ(mbyte(r, one, 1) & 0x80, 0x80); // what wasn't asked for, as it was
    // Every extent of a file, which is said once
    same(run(r, "set m:big.dat [ro]"), "M:BIG.DAT set to Read Only (RO)");
    CHECK_EQ(mbyte(r, big, 9) & 0x80, 0x80);
    CHECK_EQ(mbyte(r, static_cast<int>(second), 9) & 0x80, 0x80);
    // Another user's files, from that user
    same(r.cmd("3a:", "3A>"), "3a:\r\r\n3A>");
    same(run(r, "set m:*.txt [sys]", "3A>"), "M:ONE.TXT set to System (SYS)");
    CHECK_EQ(mbyte(r, one3, 10) & 0x80, 0x80);
    CHECK_EQ(mbyte(r, two, 10) & 0x80, 0);
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

TEST(set_says_each_error_and_changes_nothing)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}});
    REQUIRE(succeeded(r)); // M: logged in after this, by nothing but the search
    const std::vector<uint8_t> before = mdir(r);
    struct Case
    {
        const char *line, *says;
    };
    const Case cases[] = {
        {"set", "No options specified"},
        {"set m:one.txt", "No options specified"},
        {"set m:one.txt []", "No options specified"},
        {"set m: [ro rw]", "Cannot set both RO and RW."},
        {"set m:one.txt [sys,dir]", "Cannot set both SYS and DIR."},
        {"set m: [create=on,access=on]", "Cannot have both create and access time stamps"},
        {"set m:one.txt [create=on]", "Option only for drives."},
        {"set m:one.txt [protect=on]", "Option only for drives."},
        {"set m:one.txt [default=pw]", "Option only for drives."},
        {"set m: [sys]", "Option requires a file reference."},
        {"set [f1=on]", "Option requires a file reference."},
        {"set m: [protect=read]", "Option requires a file reference."},
        {"set m:one.txt [name=disk]", "Cannot label a drive with a file referenced"},
        {"set m:none.txt [ro]", "File not found."},
        {"set m:*.dat [ro]", "File not found."},
        {"set m:toolongname.txt [ro]", "Invalid file specification."},
        {"set m:one.txt m:two.txt [ro]", "Invalid file specification."},
        {"set m:one.txt=two.txt [ro]", "Invalid file specification."},
        {"set m:one.txt [bogus]", "Invalid option"},
        {"set m:one.txt [ro=on]", "Invalid option"},
        {"set m:one.txt [archive]", "Invalid option"},
        {"set m:one.txt [archive=maybe]", "Invalid option"},
        {"set m:one.txt [f1=(on)]", "Invalid option"},
        {"set m:one.txt [p=x]", "Invalid option"},            // PASSWORD or PROTECT
        {"set m:one.txt [a=on]", "Invalid option"},           // ARCHIVE or ACCESS
        {"set m:one.txt [password=toolongpw]", "Invalid option"}, // 9 characters
        {"set m:one.txt [protect=maybe]", "Invalid option"},
        {"set m: [name=*.x]", "Invalid option"},
        {"set m: [name=b:x]", "Invalid option"},
        {"set m: [name=]", "Invalid option"},
        {"set m:one.txt [ro] [sys]", "Invalid option"}, // a second list
        {"set m:one.txt [ro", "Invalid option"},        // not closed
    };
    for (const Case &c : cases)
    {
        same(run(r, c.line), c.says);
        CHECK(!succeeded(r)); // $FF00
    }
    CHECK(mdir(r) == before);
}

// ---- SET: a drive ----------------------------------------------------------------------------

TEST(set_makes_a_drive_read_only_and_read_write_and_keeps_the_current_drive)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}});
    same(run(r, "set m: [ro]"), "M: set to Read Only (RO)");
    CHECK(succeeded(r));
    std::string o = r.cmd("era m:one.txt");
    CHECK(o.find("OODOS Error On M: Read Only") != std::string::npos);
    CHECK(r.m_entry(0, "ONE     TXT") >= 0);
    same(run(r, "set m: [rw]"), "M: set to Read Write (RW)");
    same(r.cmd("era m:one.txt"), "era m:one.txt\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "ONE     TXT"), -1);
    // The current drive's, with no drive given
    same(r.cmd("m:", "M>"), "m:\r\r\nM>");
    same(run(r, "set [r]", "M>"), "Invalid option"); // RO or RW
    same(run(r, "set [ro]", "M>"), "M: set to Read Only (RO)");
    CHECK(r.cmd("era two.txt", "M>").find("OODOS Error On M: Read Only") != std::string::npos);
    same(run(r, "set [rw]", "M>"), "M: set to Read Write (RW)");
    same(r.cmd("era two.txt", "M>"), "era two.txt\r\r\nM>");
    same(r.cmd("a:"), "a:\r\r\nA>");
    // A drive that isn't: the BDOS says so (error mode $FE)
    o = run(r, "set h: [ro]");
    CHECK(o.find("OODOS Error On H: Invalid Drive") != std::string::npos);
    CHECK(!succeeded(r));
}

// ---- SET: the label --------------------------------------------------------------------------

TEST(set_names_the_label_and_changes_its_password_protection_and_stamps)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    REQUIRE(mbyte(r, 0, 0) == 0x20 && mbyte(r, 0, 12) == 0x31); // cold boot's: UNLABELED, 0x31
    auto lname = [&] { const auto b = mbytes(r, 0, 1, 11); return std::string(b.begin(), b.end()); };
    same(run(r, "set m: [name=disk100.typ]"), "M: Label = DISK100.TYP");
    CHECK_EQ(lname(), std::string("DISK100 TYP"));
    CHECK_EQ(mbyte(r, 0, 12), 0x31); // its other bits kept
    CHECK(succeeded(r));
    same(run(r, "set m: [n=work]"), "M: Label = WORK");
    CHECK_EQ(lname(), std::string("WORK       "));
    // A password: kept as the BDOS keeps it; asked for from then on
    same(run(r, "set m: [password=lblpw]"), "M: Label Password set");
    CHECK(mbytes(r, 0, 13, 11) == kept_pw("LBLPW   "));
    CHECK_EQ(mbyte(r, 0, 12), 0x31);
    same(r.to("set m: [update=off]", "Password: "), "set m: [update=off]\r\nPassword: ");
    same(r.cmd("wrong"), "wrong\r\nWrong Password\r\nA>");
    CHECK_EQ(mbyte(r, 0, 12), 0x31);
    CHECK(!succeeded(r));
    same(r.to("set m: [update=off]", "Password: "), "set m: [update=off]\r\nPassword: ");
    same(r.cmd("lblpw"), "lblpw\r\nM: Update = OFF\r\nA>"); // capitals, as the Guide has it
    CHECK_EQ(mbyte(r, 0, 12), 0x11);
    CHECK(succeeded(r));
    REQUIRE(!r.to("set m: [protect=on]", "Password: ").empty());
    same(r.cmd("LBLPW"), "LBLPW\r\nM: Password Protection = ON\r\nA>");
    CHECK_EQ(mbyte(r, 0, 12), 0x91);
    // ACCESS=ON clears CREATE (the Guide), CREATE=ON ACCESS; one 100 and one line an option
    REQUIRE(!r.to("set m: [access=on]", "Password: ").empty());
    same(r.cmd("lblpw"), "lblpw\r\nM: Access = ON\r\nA>");
    CHECK_EQ(mbyte(r, 0, 12), 0xC1);
    REQUIRE(!r.to("set m: [create=on, update=on, pr=off]", "Password: ").empty());
    same(r.cmd("lblpw"), "lblpw\r\nM: Create = ON\r\nM: Update = ON\r\nM: Password Protection = OFF\r\nA>");
    CHECK_EQ(mbyte(r, 0, 12), 0x31);
    // A new password, then the next option with it, in one command; then none
    REQUIRE(!r.to("set m: [password=new,name=renamed]", "Password: ").empty());
    same(r.cmd("lblpw"), "lblpw\r\nM: Label Password set\r\nM: Label = RENAMED\r\nA>");
    CHECK(mbytes(r, 0, 13, 11) == kept_pw("NEW     "));
    REQUIRE(!r.to("set m: [password=]", "Password: ").empty());
    same(r.cmd("new"), "new\r\nM: Label Password removed\r\nA>");
    CHECK(mbytes(r, 0, 13, 11) == kept_pw("        "));
    same(run(r, "set m: [create=off]"), "M: Create = OFF"); // no password: not asked
    CHECK_EQ(mbyte(r, 0, 12), 0x21);
    same(run(r, "set m: [access=on]"), "M: Access = ON");
    CHECK_EQ(mbyte(r, 0, 12), 0x61);
}

TEST(set_makes_a_label_where_there_is_none_and_its_stamps_are_then_made)
{
    CcpRig r;
    REQUIRE(r.ok);
    for (unsigned i = 0; i < 32; ++i) // M:'s label gone: entry 0 free, the SFCBs kept
        r.poke(MDISK + i, 0xE5);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}});
    const int one = r.m_entry(0, "ONE     TXT");
    REQUIRE(one > 0);
    // No label, no stamps: TYPE's open stamps nothing
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>");
    const std::vector<uint8_t> unstamped = slot(r, one);
    // A label, named LABEL (the Guide's default), in the first free entry: entry 0
    same(run(r, "set m: [access=on]"), "M: Access = ON");
    CHECK_EQ(mbyte(r, 0, 0), 0x20);
    CHECK(mbytes(r, 0, 1, 11) == bytes("LABEL      "));
    CHECK_EQ(mbyte(r, 0, 12), 0x41);
    CHECK(slot(r, one) == unstamped);
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>");
    const std::vector<uint8_t> s = slot(r, one);
    const unsigned today = days(2026, 10, 30);
    CHECK_EQ(s[0], today & 0xFF); // the access, in the creation's field
    CHECK_EQ(s[1], today >> 8);
    CHECK_EQ(s[2], 0x12);
    // The label's own stamps (100's): made and updated today
    CHECK_EQ(mbyte(r, 0, 24), today & 0xFF);
    CHECK_EQ(mbyte(r, 0, 28), today & 0xFF);
}

// ---- SET: a file's password and mode ---------------------------------------------------------

TEST(set_gives_files_passwords_and_modes_the_bdos_then_asks_for)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}});
    const int one = r.m_entry(0, "ONE     TXT"), two = r.m_entry(0, "TWO     TXT");
    // Passwords off: nothing done
    same(run(r, "set m:one.txt [password=secret]"), "Password protection is off: SET [PROTECT=ON]");
    CHECK(!succeeded(r));
    CHECK_EQ(m_xfcb(r, 0, "ONE     TXT"), -1);
    same(run(r, "set m: [protect=on]"), "M: Password Protection = ON");
    CHECK_EQ(mbyte(r, 0, 12), 0xB1);
    // A password and no mode: READ (no XFCB before); the XFCB and the slot's mode
    same(run(r, "set m:one.txt [password=secret]"), "M:ONE.TXT Protection = Read");
    CHECK(succeeded(r));
    const int x1 = m_xfcb(r, 0, "ONE     TXT");
    REQUIRE(x1 >= 0);
    CHECK_EQ(mbyte(r, x1, 12), 0x80);
    CHECK(mbytes(r, x1, 13, 11) == kept_pw("SECRET  "));
    CHECK_EQ(slot(r, one)[8], 0x80);
    CHECK(r.cmd("type m:one.txt").find("OODOS Error On M: Password Error") != std::string::npos);
    same(r.cmd("type m:one.txt;secret"), "type m:one.txt;secret\r\nONE\r\n\r\nA>");
    // A mode: the password given after the name, kept
    same(run(r, "set m:one.txt;secret [protect=write]"), "M:ONE.TXT Protection = Write");
    CHECK_EQ(mbyte(r, x1, 12), 0x40);
    CHECK(mbytes(r, x1, 13, 11) == kept_pw("SECRET  "));
    CHECK_EQ(slot(r, one)[8], 0x40);
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>");
    CHECK(r.cmd("era m:one.txt").find("Password Error") != std::string::npos);
    // Without it, the BDOS's Password Error (103), nothing changed
    std::string o = run(r, "set m:one.txt [protect=none]");
    CHECK(o.find("OODOS Error On M: Password Error") != std::string::npos);
    CHECK(o.find("BDOS Function = 103") != std::string::npos);
    CHECK(!succeeded(r));
    CHECK_EQ(mbyte(r, x1, 12), 0x40);
    o = run(r, "set m:one.txt;wrong [ro]"); // and 30's
    CHECK(o.find("OODOS Error On M: Password Error") != std::string::npos);
    CHECK(o.find("BDOS Function = 30") != std::string::npos);
    CHECK_EQ(mbyte(r, one, 9) & 0x80, 0);
    same(run(r, "set m:one.txt;secret [prot=delete]"), "M:ONE.TXT Protection = Delete");
    CHECK_EQ(mbyte(r, x1, 12), 0x20);
    // A mode with no password, none given: nothing done, for any file of a wildcard
    same(run(r, "set m:two.txt [protect=write]"), "Assign a Password to this file.");
    same(run(r, "set m:two.txt [password=]"), "Assign a Password to this file."); // READ, and none
    same(run(r, "set m:*.txt [protect=read]"), "Assign a Password to this file.");
    CHECK(!succeeded(r));
    CHECK_EQ(m_xfcb(r, 0, "TWO     TXT"), -1);
    CHECK_EQ(mbyte(r, x1, 12), 0x20);
    // A wildcard: each file its attributes, then its mode, the password given for the old ones
    same(run(r, "set m:*.txt;secret [password=new, protect=read, ro]"),
         lines({"M:ONE.TXT set to Read Only (RO)", "M:ONE.TXT Protection = Read", "M:TWO.TXT set to Read Only (RO)",
                "M:TWO.TXT Protection = Read"}));
    const int x2 = m_xfcb(r, 0, "TWO     TXT");
    REQUIRE(x2 >= 0);
    for (const int x : {x1, x2})
    {
        CHECK_EQ(mbyte(r, x, 12), 0x80);
        CHECK(mbytes(r, x, 13, 11) == kept_pw("NEW     "));
    }
    CHECK_EQ(slot(r, two)[8], 0x80);
    CHECK_EQ(mbyte(r, two, 9) & 0x80, 0x80);
    // The default password (106): no line; the BDOS takes it for a file's
    same(run(r, "set [default=new]"), "");
    CHECK(succeeded(r));
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nTWO\r\n\r\nA>");
    same(run(r, "set [default=]"), "");
    CHECK(r.cmd("type m:two.txt").find("Password Error") != std::string::npos);
    // NONE: mode 0, the file unprotected; the password kept
    same(run(r, "set m:two.txt;new [protect=none]"), "M:TWO.TXT Protection = None");
    CHECK_EQ(mbyte(r, x2, 12), 0x00);
    CHECK_EQ(slot(r, two)[8], 0x00);
    CHECK(mbytes(r, x2, 13, 11) == kept_pw("NEW     "));
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nTWO\r\n\r\nA>");
    // A password for a file with an XFCB keeps its mode
    same(run(r, "set m:two.txt [pa=other]"), "M:TWO.TXT Protection = None");
    CHECK(mbytes(r, x2, 13, 11) == kept_pw("OTHER   "));
}

// ---- INITDIR: a directory without SFCBs ------------------------------------------------------

TEST(initdir_gives_a_directory_sfcbs_moving_what_is_in_their_places)
{
    CcpRig r;
    REQUIRE(r.ok);
    // A directory without SFCBs: entry 0 free, the label in an SFCB's place (3) with passwords
    // on; files, XFCBs and another user's file in the others (7, 11, 15)
    plain_m(r, label("TESTDISK   ", 0x81), 3);
    r.put_util("set");     // 1
    r.put_util("initdir"); // 2
    r.chain({0, 13});
    r.entry = 3;
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}}); // 4, 5
    r.xfcb_m(0, "ONE     TXT", 0x80, "SECRET  ");                                     // 6
    r.xfcb_m(0, "TWO     TXT", 0x40, "OPEN    ");                                     // 7
    REQUIRE(r.phys(MDISK) == 0xE5); // (xfcb_m's bit 7 for the label's byte 12 left entry 0 free)
    r.put_m({{0, "THREE   TXT", bytes("THREE\r\n")}}); // 8
    r.entry = 10;
    r.put_m({{0, "BIG     DAT", std::vector<uint8_t>(140 * RECORD, 'B')}}); // 11: EX 1, extent zero
    r.xfcb_m(0, "BIG     DAT", 0x20, "BIGPW   ");                         // 12
    r.entry = 14;
    r.put_m({{3, "USER3   TXT", bytes("U3\r\n")}}); // 15
    const std::vector<uint8_t> before = mdir(r);
    REQUIRE(before[3 * 32] == 0x20 && before[7 * 32] == 16 && before[11 * 32] == 0 && before[15 * 32] == 3);

    // Stamps can't be asked for yet; M: logged in by it, without SFCBs
    same(run(r, "set m: [create=on]"), "Directory needs to be reformatted for date/time stamps.");
    same(r.to("initdir m:", "(Y/N)? "),
         "initdir m:\r\nINITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.\r\n"
         "Do you really want to re-format the directory: M (Y/N)? ");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(succeeded(r));
    const std::vector<uint8_t> after = mdir(r);
    CHECK(after == reformatted(before));
    CHECK(kept(after) == kept(before)); // nothing lost
    // Where they went: the lowest free entries outside the SFCBs' places
    CHECK_EQ(after[0 * 32], 0x20);  // the label
    CHECK_EQ(after[9 * 32], 16);    // TWO.TXT's XFCB
    CHECK_EQ(after[10 * 32], 0);    // BIG.DAT
    CHECK_EQ(after[13 * 32], 3);    // USER3.TXT
    for (unsigned i = 3; i < M_ENTRIES; i += 4)
        CHECK_EQ(after[i * 32], 0x21);
    // The slots: the XFCBs' modes for ONE (4), TWO (5) and BIG (10); stamps 0
    CHECK(slot(r, 4) == std::vector<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0}));
    CHECK_EQ(slot(r, 5)[8], 0x40);
    CHECK_EQ(slot(r, 10)[8], 0x20);
    CHECK_EQ(slot(r, 8)[8], 0x00);
    // The BDOS sees the new directory: the files, a password asked for, stamps now possible
    same(r.cmd("dir m:"), "dir m:\r\nM: ONE      TXT : TWO      TXT : THREE    TXT : BIG      DAT\r\nSYSTEM FILE(S) EXIST\r\nA>");
    CHECK(r.cmd("type m:one.txt").find("Password Error") != std::string::npos);
    same(r.cmd("type m:one.txt;secret"), "type m:one.txt;secret\r\nONE\r\n\r\nA>");
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nTWO\r\n\r\nA>");
    CHECK(r.cmd("era m:big.dat").find("Password Error") != std::string::npos);
    same(r.cmd("3a:", "3A>"), "3a:\r\r\n3A>");
    same(r.cmd("type m:user3.txt", "3A>"), "type m:user3.txt\r\nU3\r\n\r\n3A>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    same(run(r, "set m: [access=on]"), "M: Access = ON");
    CHECK_EQ(mbyte(r, 0, 12), 0xC1);
    same(r.cmd("type m:three.txt"), "type m:three.txt\r\nTHREE\r\n\r\nA>");
    const unsigned today = days(2026, 10, 30);
    CHECK_EQ(slot(r, 8)[0], today & 0xFF);
    CHECK_EQ(slot(r, 8)[1], today >> 8);
    CHECK_EQ(slot(r, 8)[2], 0x12);
}

TEST(initdir_asks_first_and_needs_room_for_the_sfcbs)
{
    // A directory without SFCBs, 192 entries in use (three quarters of 256), every place among
    // them: N leaves it; Y moves 48 entries and fills every other entry. One more is too many.
    for (const unsigned used : {192u, 193u})
    {
        CcpRig r;
        REQUIRE(r.ok);
        plain_m(r, label("FULL       ", 0x01), 0);
        r.put_util("initdir"); // 1
        r.chain({0, 13});
        for (unsigned i = 2; i < used; ++i)
        {
            char n[24];
            std::snprintf(n, sizeof n, "F%03u    DAT", i);
            r.poke(MDISK + i * 32, entry(i % 5 == 0 ? 2 : 0, n));
        }
        const std::vector<uint8_t> before = mdir(r);
        if (used == 193)
        {
            same(run(r, "initdir m:"), "Not enough room in directory.");
            CHECK(!succeeded(r));
            CHECK(mdir(r) == before);
            continue;
        }
        REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
        same(r.cmd("n"), "n\r\r\nA>");
        CHECK(mdir(r) == before);
        REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
        same(r.cmd("Y"), "Y\r\r\nA>");
        const std::vector<uint8_t> after = mdir(r);
        CHECK(after == reformatted(before));
        CHECK(kept(after) == kept(before));
        unsigned free = 0;
        for (unsigned i = 0; i < M_ENTRIES; ++i)
            free += after[i * 32] == 0xE5;
        CHECK_EQ(free, 0u);
        same(r.cmd("dir m:f19*.*").substr(0, 20), "dir m:f19*.*\r\nM: F19");
    }
}

// ---- INITDIR: a directory with SFCBs ---------------------------------------------------------

TEST(initdir_recovers_the_sfcbs_room_or_clears_their_stamps)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r); // cold boot's M:: a label (0x31) and SFCBs
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}});
    const int one = r.m_entry(0, "ONE     TXT");
    // Stamps and a mode in ONE.TXT's slot, behind OODOS's back
    const std::vector<uint8_t> stamped = {0x11, 0x22, 0x12, 0x34, 0x33, 0x44, 0x13, 0x45, 0x80, 0x00};
    r.poke(MDISK + (one | 3) * 32 + 1 + 10 * (one & 3), stamped);
    const std::vector<uint8_t> before = mdir(r);
    // N, N: nothing
    same(r.to("initdir m:", "(Y/N)? "),
         "initdir m:\r\nDirectory already re-formatted.\r\nDo you wish to recover date/time directory space (Y/N)? ");
    same(r.to("n", "(Y/N)? "), "n\r\nDo you want the existing date/time stamps cleared (Y/N)? ");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(succeeded(r));
    CHECK(mdir(r) == before);
    // N, Y: every slot's stamps 0, the rest kept
    REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
    REQUIRE(!r.to("n", "(Y/N)? ").empty());
    same(r.cmd("y"), "y\r\r\nA>");
    std::vector<uint8_t> want = before;
    for (unsigned i = 3; i < M_ENTRIES; i += 4)
        for (unsigned k = 0; k < 3; ++k)
            std::fill(want.begin() + i * 32 + 1 + 10 * k, want.begin() + i * 32 + 1 + 10 * k + 8, 0);
    CHECK(mdir(r) == want);
    CHECK(slot(r, one) == std::vector<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0}));
    // Y: the SFCBs free, the label asking for no stamps
    REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
    same(r.cmd("y"), "y\r\r\nA>");
    std::vector<uint8_t> free = before;
    for (unsigned i = 3; i < M_ENTRIES; i += 4)
        std::fill(free.begin() + i * 32, free.begin() + i * 32 + 32, 0xE5);
    free[12] = 0x01;
    CHECK(mdir(r) == free);
    // The BDOS sees no SFCBs: stamps refused, the files there
    same(run(r, "set m: [update=on]"), "Directory needs to be reformatted for date/time stamps.");
    same(r.cmd("dir m:"), "dir m:\r\nM: ONE      TXT : TWO      TXT\r\nSYSTEM FILE(S) EXIST\r\nA>");
    // And INITDIR now offers to make them
    CHECK(r.to("initdir m:", "(Y/N)? ").find("WILL ACTIVATE") != std::string::npos);
    same(r.cmd("n"), "n\r\r\nA>");
}

// ---- INITDIR: its errors ---------------------------------------------------------------------

TEST(initdir_needs_a_drive_that_is_writable_and_memory_for_what_it_moves)
{
    CcpRig r;
    REQUIRE(r.ok);
    plain_m(r, label("SMALL      ", 0x01), 0);
    r.put_util("set");     // 1
    r.put_util("initdir"); // 2
    r.chain({0, 13});
    r.poke(MDISK + 3 * 32, entry(0, "AT3     DAT"));
    r.poke(MDISK + 7 * 32, entry(0, "AT7     DAT"));
    r.poke(MDISK + 8 * 32, entry(16, "AT7     DAT")); // an XFCB, to keep too
    const std::vector<uint8_t> before = mdir(r);
    for (const char *line : {"initdir", "initdir m:x.txt", "initdir x.txt", "initdir m: b:", "initdir m: [x]", "initdir q:"})
    {
        same(run(r, line), "Invalid drive.");
        CHECK(!succeeded(r));
    }
    std::string o = run(r, "initdir h:");
    CHECK(o.find("OODOS Error On H: Invalid Drive") != std::string::npos);
    CHECK(!succeeded(r));
    // Read-only (SET's, 28)
    same(run(r, "set m: [ro]"), "M: set to Read Only (RO)");
    same(run(r, "initdir m:"), "Drive Read Only");
    CHECK(!succeeded(r));
    same(run(r, "set m: [rw]"), "M: set to Read Write (RW)");
    CHECK(mdir(r) == before);
    // Memory: the TPA's top made BSS's end + 40, as an RSX below the BDOS would make it (its
    // entry a JMP to the BDOS's): room for AT3's copy (32), not AT7's
    const std::map<std::string, uint16_t> l = labels(EMU_OODOS_UTILS_DIR "/initdir.lbl");
    REQUIRE(l.count("BUF"));
    const uint16_t top = static_cast<uint16_t>(l.at("BUF") + 512 + 40);
    bool lowered = false;
    r.watch = [&](uint16_t pc) {
        if (pc == 0x0300 && !lowered)
        {
            lowered = true;
            r.poke(0x0204, static_cast<uint8_t>(top)); // (bank 0's $0204: program memory is blocks 0-2)
            r.poke(0x0205, static_cast<uint8_t>(top >> 8));
            r.poke(top, {0x4C, 0xFD, 0xBF});
        }
    };
    same(run(r, "initdir m:"), "Not Enough Memory");
    r.watch = nullptr;
    CHECK(lowered);
    CHECK(!succeeded(r));
    CHECK(mdir(r) == before);
    // With the TPA whole again, it works
    REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(mdir(r) == reformatted(before));
}

TEST(initdir_says_the_bioss_read_and_write_errors_and_leaves_the_card_as_it_was)
{
    CcpRig r;
    REQUIRE(r.ok);
    auto fm = std::make_shared<FaultyMedia>(r.media->bytes(), "card");
    r.media = fm;
    r.sd().insert(fm, false);
    r.run(ms(SdCard::INIT_MS + 10));
    REQUIRE(r.mount(1, "/OODOS/A.IMG") == 0);
    REQUIRE(r.mount(2, "/OODOS/B.IMG") == 0);
    REQUIRE(r.boot()); // cold boot formats M: again
    r.entry = 0;
    r.block = M_DIRBLOCKS;
    tools(r);
    CpmDisk b{r, 2};
    const std::vector<uint8_t> card = fm->bytes(), dir = b.directory();
    REQUIRE(dir[3 * 32] == 0x21); // mkfs.cpm -t's: SFCBs
    // Its reads failing once the question is answered
    REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
    fm->rfail = true;
    same(r.cmd("y"), "y\r\nDisk Read Error\r\nA>");
    fm->rfail = false;
    CHECK(!succeeded(r));
    CHECK(fm->bytes() == card);
    // Its writes
    REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
    fm->wfail = true;
    same(r.cmd("y"), "y\r\nDisk Write Error\r\nA>");
    fm->wfail = false;
    CHECK(!succeeded(r));
    CHECK(fm->bytes() == card);
    // Then it works: B:'s 1,024 entries, their SFCBs free, the label's stamps off
    REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
    same(r.cmd("y"), "y\r\r\nA>");
    std::vector<uint8_t> want = dir;
    for (unsigned i = 3; i < b.entries(); i += 4)
        std::fill(want.begin() + i * 32, want.begin() + i * 32 + 32, 0xE5);
    REQUIRE(want[0] == 0x20);
    want[12] &= 0x8F;
    CHECK(b.directory() == want);
    same(run(r, "set b: [create=on]"), "Directory needs to be reformatted for date/time stamps.");
}

int main()
{
    return run_all();
}
