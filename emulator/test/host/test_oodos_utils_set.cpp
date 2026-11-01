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
 *     a password, the default password (106), left as it was when the
 *     label's checks fail; a wildcard going on past a file the BDOS
 *     refuses; NONE deleting the password; a file's first XFCB counting;
 *     a label's stamp bits dropped where there are no SFCBs;
 *   - INITDIR on a directory without SFCBs (entries in SFCB places moved,
 *     the XFCBs' modes in the slots), one nearly full and one too full,
 *     the answer N; one with SFCBs: recovered, stamps cleared, left; its
 *     errors: no drive, a read-only drive, too little memory (the TPA
 *     made small, as an RSX below the BDOS would), the BIOS's read and
 *     write failing on a card's drive; nothing written when the moves
 *     find no place or the drive's reset fails; a re-format a write error
 *     stopped, at five points, and a recover, finished by the next INITDIR;
 *     SET's label options on a read-only drive refused before anything.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
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
        r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
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

    /// The SFCB slot of entry `e` in a directory image.
    std::vector<uint8_t> slot_of(const std::vector<uint8_t> &d, unsigned e)
    {
        const size_t at = (e | 3) * 32 + 1 + 10 * (e & 3);
        return std::vector<uint8_t>(d.begin() + static_cast<long>(at), d.begin() + static_cast<long>(at) + 10);
    }

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
    /// bad would (test_oodos_sys's FaultyMedia, with reads); or whose writes fail once `wleft` more
    /// have been made (-1: never). `writes` counts the writes made.
    struct FaultyMedia : MemoryMedia
    {
        using MemoryMedia::MemoryMedia;
        bool rfail{false}, wfail{false};
        int wleft{-1};
        unsigned writes{0};
        bool read(uint64_t lba, uint8_t *out) override { return !rfail && MemoryMedia::read(lba, out); }
        bool write(uint64_t lba, const uint8_t *in) override
        {
            if (wfail || wleft == 0)
                return false;
            if (wleft > 0)
                --wleft;
            ++writes;
            return MemoryMedia::write(lba, in);
        }
    };

    /// The card made a FaultyMedia, A.IMG and B.IMG mounted again and OODOS booted (cold boot
    /// formats M: again), SET and INITDIR on M:, A: logged in (the CCP's warm boot needs it).
    std::shared_ptr<FaultyMedia> faulty(CcpRig &r)
    {
        auto fm = std::make_shared<FaultyMedia>(r.media->bytes(), "card");
        r.media = fm;
        r.sd().insert(fm, false);
        r.run(ms(SdCard::INIT_MS + 10));
        const bool up = r.mount(1, "/OODOS/A.IMG") == 0 && r.mount(2, "/OODOS/B.IMG") == 0 && r.boot();
        CHECK(up);
        if (!up)
            return nullptr;
        r.entry = 0;
        r.block = M_DIRBLOCKS;
        tools(r);
        same(r.cmd("dir a:x"), "dir a:x\r\nNo File\r\nA>"); // (M: first on the chain: the search doesn't)
        return fm;
    }

    /// A sector of a card's unit written by the card's WRITE, behind OODOS's back.
    void write_unit(CcpRig &r, unsigned unit, uint32_t lba, const std::vector<uint8_t> &s)
    {
        REQUIRE(s.size() == SECTOR);
        r.sd().write(SdCard::UNIT, static_cast<uint8_t>(unit));
        for (unsigned i = 0; i < 4; ++i)
            r.sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
        r.sd().write(SdCard::COUNT, 1);
        r.sd().write(SdCard::COUNT + 1, 0);
        r.sd().write(SdCard::CMD, SdCard::WRITE);
        for (const uint8_t b : s)
            r.sd().write(SdCard::DATA, b);
        r.run(ms(1));
        r.sd().write(SdCard::UNIT, 0);
        CHECK(r.read_unit(unit, lba) == s);
    }

    /// INITDIR's RESET (37), the first time it is called: `at` run, then its return made `a`, `x`
    /// if `fail` (as a 37 that couldn't write a buffer back). @return set when it was called
    std::shared_ptr<bool> on_reset(CcpRig &r, std::function<void()> at, bool fail = false)
    {
        const std::map<std::string, uint16_t> l = labels(EMU_OODOS_UTILS_DIR "/initdir.lbl");
        CHECK(l.count("RESET"));
        const uint16_t reset = l.count("RESET") ? l.at("RESET") : 0;
        auto called = std::make_shared<bool>(false);
        auto back = std::make_shared<int>(-1);
        r.watch = [&r, reset, at, fail, called, back](uint16_t pc) {
            if (!*called && pc == reset)
            {
                *called = true;
                const uint8_t sp = r.cpu->regs().s; // the JSR's return address, less 1
                *back = (r.phys(0x0101u + sp) | r.phys(0x0102u + sp) << 8) + 1;
                at();
            }
            else if (fail && *back >= 0 && pc == *back)
            {
                *back = -1;
                Registers g = r.cpu->regs();
                g.a = 0xFF;
                g.x = 1;
                g.p = static_cast<uint8_t>(g.p & ~Cpu6502::FLAG_Z);
                r.cpu->set_regs(g);
            }
        };
        return called;
    }
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

TEST(set_changes_no_default_password_when_the_labels_checks_fail)
{
    CcpRig r;
    REQUIRE(r.ok);
    // A directory without SFCBs, passwords on, the label's password LBLPW; a protected file
    plain_m(r, label("PLAIN      ", 0x81, "LBLPW   "), 0);
    r.put_util("set");
    r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}});
    r.xfcb_m(0, "ONE     TXT", 0x80, "SECRET  ");
    const std::vector<uint8_t> before = mdir(r);
    // Stamps without SFCBs: refused before the label's password is asked, the default not set
    same(run(r, "set m: [default=secret, create=on]"), "Directory needs to be reformatted for date/time stamps.");
    CHECK(!succeeded(r));
    CHECK(r.cmd("type m:one.txt").find("OODOS Error On M: Password Error") != std::string::npos);
    // The label's password wrong: the same
    same(r.to("set m: [default=secret, name=other]", "Password: "), "set m: [default=secret, name=other]\r\nPassword: ");
    same(r.cmd("wrong"), "wrong\r\nWrong Password\r\nA>");
    CHECK(!succeeded(r));
    CHECK(r.cmd("type m:one.txt").find("OODOS Error On M: Password Error") != std::string::npos);
    // A read-only drive (28): Drive Read Only before the password is asked, the default not set
    same(run(r, "set m: [ro]"), "M: set to Read Only (RO)");
    same(run(r, "set m: [default=secret, name=other]"), "Drive Read Only");
    CHECK(!succeeded(r));
    CHECK(r.cmd("type m:one.txt").find("OODOS Error On M: Password Error") != std::string::npos);
    same(run(r, "set m: [rw]"), "M: set to Read Write (RW)");
    CHECK(mdir(r) == before);
    // Right: the label renamed, then the default set
    REQUIRE(!r.to("set m: [default=secret, name=other]", "Password: ").empty());
    same(r.cmd("lblpw"), "lblpw\r\nM: Label = OTHER\r\nA>");
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>");
}

TEST(set_drops_a_labels_stamp_bits_on_a_directory_without_sfcbs)
{
    // A label asking for create and update stamps on a directory without SFCBs (made elsewhere):
    // any label option writes it without them, as 100 would refuse them; UPDATE=OFF's CREATE too
    struct Case
    {
        const char *line, *says;
        uint8_t mode;
        const char *name;
    };
    const Case cases[] = {{"set m: [update=off]", "M: Update = OFF", 0x01, "STAMPS     "},
                          {"set m: [name=fixed]", "M: Label = FIXED", 0x01, "FIXED      "},
                          {"set m: [protect=on]", "M: Password Protection = ON", 0x81, "STAMPS     "}};
    for (const Case &c : cases)
    {
        CcpRig r;
        REQUIRE(r.ok);
        plain_m(r, label("STAMPS     ", 0x31), 0);
        r.put_util("set");
        r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
        same(run(r, "set m: [create=on]"), "Directory needs to be reformatted for date/time stamps.");
        CHECK_EQ(mbyte(r, 0, 12), 0x31);
        same(run(r, c.line), c.says);
        CHECK(succeeded(r));
        CHECK_EQ(mbyte(r, 0, 12), c.mode);
        CHECK(mbytes(r, 0, 1, 11) == bytes(c.name));
    }
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
    // NONE: mode 0, the file unprotected; its password deleted (the Guide's Table 5-17), so a mode
    // asked for later needs a new one
    same(run(r, "set m:two.txt;new [protect=none]"), "M:TWO.TXT Protection = None");
    CHECK_EQ(mbyte(r, x2, 12), 0x00);
    CHECK_EQ(slot(r, two)[8], 0x00);
    CHECK(mbytes(r, x2, 13, 11) == kept_pw("        "));
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nTWO\r\n\r\nA>");
    same(run(r, "set m:two.txt [protect=read]"), "Assign a Password to this file.");
    CHECK(!succeeded(r));
    CHECK_EQ(mbyte(r, x2, 12), 0x00);
    // A password for a file with an XFCB keeps its mode
    same(run(r, "set m:two.txt [pa=other]"), "M:TWO.TXT Protection = None");
    CHECK(mbytes(r, x2, 13, 11) == kept_pw("OTHER   "));
    // NONE with a new password: the mode 0, the new password kept
    same(run(r, "set m:one.txt;new [protect=none, password=third]"), "M:ONE.TXT Protection = None");
    CHECK_EQ(mbyte(r, x1, 12), 0x00);
    CHECK(mbytes(r, x1, 13, 11) == kept_pw("THIRD   "));
}

TEST(set_goes_on_with_the_next_file_after_one_the_bdos_refuses)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {0, "TWO     TXT", bytes("TWO\r\n")}, {0, "THREE   TXT", bytes("3\r\n")}});
    const int one = r.m_entry(0, "ONE     TXT"), two = r.m_entry(0, "TWO     TXT"), three = r.m_entry(0, "THREE   TXT");
    // Passwords on; TWO.TXT's another than the others'
    r.xfcb_m(0, "ONE     TXT", 0x80, "SECRET  ");
    r.xfcb_m(0, "TWO     TXT", 0x80, "OTHER   ");
    r.xfcb_m(0, "THREE   TXT", 0x80, "SECRET  ");
    const int x2 = m_xfcb(r, 0, "TWO     TXT"), x3 = m_xfcb(r, 0, "THREE   TXT");
    // 30: TWO.TXT refused (the BDOS says so), its 103 not tried; ONE and THREE done; $FF00
    std::string o = run(r, "set m:*.txt;secret [ro, protect=write]");
    const std::string bdos = "\r\nOODOS Error On M: Password Error\r\nBDOS Function = 30";
    const size_t at = o.find(bdos);
    CHECK(at != std::string::npos);
    CHECK_EQ(o.substr(0, at), lines({"M:ONE.TXT set to Read Only (RO)", "M:ONE.TXT Protection = Write"}));
    const size_t after = o.find("M:THREE.TXT", at);
    CHECK(after != std::string::npos && o.find("\r\n", at + bdos.size()) < after); // the BDOS's line ended
    if (after != std::string::npos)
        CHECK_EQ(o.substr(after), lines({"M:THREE.TXT set to Read Only (RO)", "M:THREE.TXT Protection = Write"}));
    CHECK(o.find("M:TWO.TXT") == std::string::npos);
    CHECK(!succeeded(r));
    CHECK_EQ(mbyte(r, one, 9) & 0x80, 0x80);
    CHECK_EQ(mbyte(r, two, 9) & 0x80, 0);
    CHECK_EQ(mbyte(r, three, 9) & 0x80, 0x80);
    CHECK_EQ(mbyte(r, x2, 12), 0x80);
    CHECK_EQ(mbyte(r, x3, 12), 0x40);
    // 103 alone: the same
    o = run(r, "set m:*.txt;secret [protect=delete]");
    CHECK(o.find("\r\nOODOS Error On M: Password Error\r\nBDOS Function = 103") != std::string::npos);
    CHECK(o.find("M:ONE.TXT Protection = Delete") == 0);
    const std::string last = "\r\nM:THREE.TXT Protection = Delete";
    CHECK(o.size() >= last.size() && o.compare(o.size() - last.size(), last.size(), last) == 0);
    CHECK(!succeeded(r));
    CHECK_EQ(mbyte(r, x2, 12), 0x80);
    CHECK_EQ(mbyte(r, x3, 12), 0x20);
}

TEST(set_takes_a_files_first_xfcb_as_the_bdos_does)
{
    CcpRig r;
    REQUIRE(r.ok);
    tools(r);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}});
    // Two XFCBs of the name (as a rename onto an orphan's leaves): the first's mode 0 and a
    // password, the second's READ and none. The BDOS takes the first.
    r.xfcb_m(0, "ONE     TXT", 0x00, "SECRET  ");
    r.xfcb_m(0, "ONE     TXT", 0x80, "        ");
    const int x1 = m_xfcb(r, 0, "ONE     TXT");
    int x2 = -1;
    for (unsigned i = static_cast<unsigned>(x1) + 1; i < M_ENTRIES && x2 < 0; ++i)
        if (mbyte(r, static_cast<int>(i), 0) == 16 && mbytes(r, static_cast<int>(i), 1, 11) == bytes("ONE     TXT"))
            x2 = static_cast<int>(i);
    REQUIRE(x1 >= 0 && x2 > x1);
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>"); // mode 0: not protected
    // The kept mode is the first's (0, no line but None); a mode needs no new password: it has one
    same(run(r, "set m:one.txt [protect=read]"), "M:ONE.TXT Protection = Read");
    CHECK(succeeded(r));
    CHECK_EQ(mbyte(r, x1, 12), 0x80);
    CHECK(mbytes(r, x1, 13, 11) == kept_pw("SECRET  "));
    CHECK_EQ(mbyte(r, x2, 12), 0x80);
    CHECK(r.cmd("type m:one.txt").find("Password Error") != std::string::npos);
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
    r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
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
        r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
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
    r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
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
    const auto fm = faulty(r);
    REQUIRE(fm);
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

TEST(initdir_writes_nothing_unless_every_entry_finds_a_place_and_the_reset_worked)
{
    // A directory without SFCBs, two entries in SFCB places to move
    auto setup = [](CcpRig &r) {
        plain_m(r, label("SMALL      ", 0x01), 0);
        r.put_util("initdir"); // 1
        r.chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
        r.poke(MDISK + 3 * 32, entry(0, "AT3     DAT"));
        r.poke(MDISK + 7 * 32, entry(0, "AT7     DAT"));
    };
    {
        // The free entries taken behind the BDOS's back once INITDIR has counted them (as if its
        // view and the disk's differed): no place for the moves, so no SFCB written over them
        CcpRig r;
        REQUIRE(r.ok);
        setup(r);
        REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
        std::vector<uint8_t> filled;
        const auto called = on_reset(r, [&] {
            for (unsigned i = 0; i < M_ENTRIES; ++i)
                if (i % 4 != 3 && r.phys(MDISK + i * 32) == 0xE5)
                {
                    char n[24];
                    std::snprintf(n, sizeof n, "X%03u    DAT", i);
                    r.poke(MDISK + i * 32, entry(5, n));
                }
            filled = mdir(r);
        });
        same(r.cmd("y"), "y\r\nNot enough room in directory.\r\nA>");
        r.watch = nullptr;
        CHECK(*called);
        CHECK(!succeeded(r));
        CHECK(mdir(r) == filled);
    }
    {
        // 37 failing before the first write (a buffer it couldn't write back: the BDOS says so):
        // nothing written
        CcpRig r;
        REQUIRE(r.ok);
        setup(r);
        const std::vector<uint8_t> before = mdir(r);
        REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
        const auto called = on_reset(r, [] {}, true);
        same(r.cmd("y"), "y\r\r\nA>");
        r.watch = nullptr;
        CHECK(*called);
        CHECK(!succeeded(r));
        CHECK(mdir(r) == before);
        // Then it works
        REQUIRE(!r.to("initdir m:", "(Y/N)? ").empty());
        same(r.cmd("y"), "y\r\r\nA>");
        CHECK(mdir(r) == reformatted(before));
    }
}

TEST(initdir_finishes_a_re_format_a_write_error_stopped)
{
    // B: (a card's: its writes can fail) without SFCBs: sector 0 and 1's entries in use but 5 and
    // 21, so the eight in SFCB places move to 5, 21 and sector 2's 32-38: three writes, then the 64
    // sectors' SFCBs. A write fails after k: 0 (nothing written), 1 (the moves half made), 3 (the
    // moves made, sector 0's SFCBs not), 4 (sector 0's SFCBs made, sector 1's places still the
    // originals, each with its copy outside: both the partly test and the copies decide), 5
    // (sectors 0 and 1's SFCBs made). INITDIR says so, and run again it finishes the job: nothing
    // lost, nothing twice.
    for (const int k : {0, 1, 3, 4, 5})
    {
        std::printf("  a write failing after %d\n", k);
        CcpRig r;
        REQUIRE(r.ok);
        const auto fm = faulty(r);
        REQUIRE(fm);
        CpmDisk b{r, 2};
        // B:'s SFCBs taken away first (INITDIR's own), then its first 32 entries written
        REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
        same(r.cmd("y"), "y\r\r\nA>");
        std::vector<uint8_t> d = b.directory();
        REQUIRE(d[0] == 0x20 && d[3 * 32] == 0xE5);
        for (unsigned i = 1; i < 32; ++i)
        {
            if (i == 5 || i == 21)
                continue;
            char n[24];
            std::snprintf(n, sizeof n, "F%02u     DAT", i);
            std::vector<uint8_t> e = entry(i == 9 ? 2 : 0, n);
            if (i == 7) // F04.DAT's XFCB, in a place: its mode to F04's slot
            {
                e = entry(16, "F04     DAT");
                e[12] = 0x80;
            }
            std::copy(e.begin(), e.end(), d.begin() + i * 32);
        }
        write_unit(r, 2, 0, std::vector<uint8_t>(d.begin(), d.begin() + SECTOR));
        write_unit(r, 2, 1, std::vector<uint8_t>(d.begin() + SECTOR, d.begin() + 2 * SECTOR));
        const std::vector<uint8_t> before = b.directory();
        REQUIRE(before == d);
        const std::vector<uint8_t> want = reformatted(before);
        CHECK_EQ(want[5 * 32 + 1], 'F'); // F03 moved to 5
        CHECK(slot_of(want, 4)[8] == 0x80);

        REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
        fm->writes = 0;
        fm->wleft = k;
        same(r.cmd("y"), k ? "y\r\nDisk Write Error\r\nDirectory partly re-formatted: run INITDIR again.\r\nA>"
                           : "y\r\nDisk Write Error\r\nA>");
        fm->wleft = -1;
        CHECK_EQ(fm->writes, static_cast<unsigned>(k));
        CHECK(!succeeded(r));
        const std::vector<uint8_t> partly = b.directory();
        if (k == 0)
            CHECK(partly == before);
        if (k == 1) // F03 at 5 and in 3 too
            CHECK(std::equal(partly.begin() + 3 * 32, partly.begin() + 4 * 32, partly.begin() + 5 * 32));
        CHECK_EQ(partly[3 * 32], k >= 4 ? 0x21 : before[3 * 32]);
        if (k == 4) // sector 1's places the originals still
            for (unsigned i = 19; i < 32; i += 4)
                CHECK(std::equal(partly.begin() + i * 32, partly.begin() + i * 32 + 32, before.begin() + i * 32));
        // Again: the rest done
        const std::string q = r.to("initdir b:", "(Y/N)? ");
        same(q, std::string("initdir b:\r\n") + (k >= 4 ? "Directory partly re-formatted.\r\n" : "") +
                    "INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.\r\n"
                    "Do you really want to re-format the directory: B (Y/N)? ");
        fm->writes = 0;
        same(r.cmd("y"), "y\r\r\nA>");
        CHECK(succeeded(r));
        CHECK_EQ(fm->writes, k <= 0 ? 67u : k == 1 ? 66u : 64u); // the moves left, then the SFCBs
        const std::vector<uint8_t> after = b.directory();
        CHECK(after == want);
        CHECK(kept(after) == kept(before));
        same(r.cmd("dir b:f1*.*").substr(0, 22), "dir b:f1*.*\r\nB: F10   ");
    }
}

TEST(initdir_finishes_a_recover_a_write_error_stopped)
{
    // B: (a card's: its writes can fail) with mkfs.cpm -t's SFCBs, recovered: its 64 sectors written,
    // sector 0 first. A write fails after k: 0 (nothing written), 1 (sector 0's SFCBs free: entry 3
    // free, the BDOS then seeing no SFCBs), 40. Run again, INITDIR finds SFCBs in places but not
    // entry 3, says so and asks to recover first: N goes on to the re-format's question, Y finishes.
    for (const int k : {0, 1, 40})
    {
        std::printf("  a write failing after %d\n", k);
        CcpRig r;
        REQUIRE(r.ok);
        const auto fm = faulty(r);
        REQUIRE(fm);
        CpmDisk b{r, 2};
        const std::vector<uint8_t> before = b.directory();
        REQUIRE(before[0] == 0x20 && before[3 * 32] == 0x21);
        std::vector<uint8_t> want = before;
        for (unsigned i = 3; i < b.entries(); i += 4)
            std::fill(want.begin() + i * 32, want.begin() + i * 32 + 32, 0xE5);
        want[12] &= 0x8F;
        const std::string have = "Directory already re-formatted.\r\n"
                                 "Do you wish to recover date/time directory space (Y/N)? ";
        if (k == 1)
        {
            // Clearing the stamps isn't a re-format: a write error there says only that
            same(r.to("initdir b:", "(Y/N)? "), "initdir b:\r\n" + have);
            REQUIRE(!r.to("n", "(Y/N)? ").empty());
            fm->wleft = 1;
            same(r.cmd("y"), "y\r\nDisk Write Error\r\nA>");
            fm->wleft = -1;
            CHECK(!succeeded(r));
            std::vector<uint8_t> cleared = before; // sector 0's slots' stamps 0, the rest as it was
            for (unsigned i = 3; i < 16; i += 4)
                for (unsigned k3 = 0; k3 < 3; ++k3)
                    std::fill(cleared.begin() + i * 32 + 1 + 10 * k3, cleared.begin() + i * 32 + 1 + 10 * k3 + 8, 0);
            CHECK(b.directory() == cleared);
        }
        same(r.to("initdir b:", "(Y/N)? "), "initdir b:\r\n" + have);
        fm->writes = 0;
        fm->wleft = k;
        same(r.cmd("y"), k ? "y\r\nDisk Write Error\r\nDirectory partly re-formatted: run INITDIR again.\r\nA>"
                           : "y\r\nDisk Write Error\r\nA>");
        fm->wleft = -1;
        CHECK_EQ(fm->writes, static_cast<unsigned>(k));
        CHECK(!succeeded(r));
        const std::vector<uint8_t> partly = b.directory();
        const size_t done = static_cast<size_t>(k) * SECTOR; // sectors 0 to k-1 recovered, the rest not
        CHECK(std::equal(partly.begin(), partly.begin() + static_cast<long>(done), want.begin()));
        CHECK(std::equal(partly.begin() + static_cast<long>(done), partly.end(), before.begin() + static_cast<long>(done)));
        // Again
        const std::string q = r.to("initdir b:", "(Y/N)? ");
        if (k == 0)
            same(q, "initdir b:\r\n" + have);
        else
            same(q, "initdir b:\r\nDirectory partly re-formatted.\r\n"
                    "Do you wish to recover date/time directory space (Y/N)? ");
        if (k == 40)
        {
            // N: the re-format offered instead (N again: nothing); then the recover asked for again
            same(r.to("n", "(Y/N)? "), "n\r\nINITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.\r\n"
                                        "Do you really want to re-format the directory: B (Y/N)? ");
            same(r.cmd("n"), "n\r\r\nA>");
            CHECK(b.directory() == partly);
            REQUIRE(!r.to("initdir b:", "(Y/N)? ").empty());
        }
        fm->writes = 0;
        same(r.cmd("y"), "y\r\r\nA>");
        CHECK(succeeded(r));
        CHECK_EQ(fm->writes, 64u);
        CHECK(b.directory() == want);
        // The BDOS sees no SFCBs, and INITDIR offers to make them, nothing partly
        same(run(r, "set b: [create=on]"), "Directory needs to be reformatted for date/time stamps.");
        same(r.to("initdir b:", "(Y/N)? "), "initdir b:\r\nINITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.\r\n"
                                             "Do you really want to re-format the directory: B (Y/N)? ");
        same(r.cmd("n"), "n\r\r\nA>");
    }
}


int main()
{
    return run_all();
}
