/**
 * @file test_oodos_utils_sys.cpp
 * @brief OODOS's system utilities, phase 4b1 (hw/oodos.md, "The utilities, phase 4b"): SHOW, DATE, DEVICE, SETDEF.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the
 * utilities (`make -C oodos utils`) put on M: as user 0's SYS files and run
 * from the CCP as a user would, by the search chain and by `M:NAME`; what
 * each writes compared line for line with the plan's forms, and what each
 * sets read back behind OODOS's back: the SCB (SYS_BLOCK_PHYS + $200), the
 * DS3231 through the bridge, the directories of M: and the card's A: and B:
 * (the free space, the label, the users, the DPB worked out from
 * oodos/diskdefs). Then the CCP acting on what SETDEF set: the chain finding
 * a program, the display mode's line, paging.
 *
 *   - SHOW: SPACE for a drive and for every logged-in drive (RW and RO),
 *     LABEL (passwords, create or access stamps, none), USERS, DIR, DRIVE
 *     on oodos-8m and oodos-ram, prefixes, every error;
 *   - DATE: the display, CONTINUOUS stopped by a key, a time specification
 *     checked (months, days, leap years, hours, minutes, seconds) and set
 *     at the key with its seconds, DATE SET's prompts with empty lines
 *     (or only blanks) keeping their parts, the errors, no clock;
 *   - DEVICE: the full display and its prompt (a line taken as a command),
 *     NAMES, VALUES, a physical and a logical device and their other
 *     names, assignments and NULL, input and output checked, the baud rate
 *     and XON refused, NOXON taken, CONSOLE [PAGE] and [COLUMNS=,LINES=],
 *     every error;
 *   - SETDEF: the display, the chain with *, TEMPORARY, ORDER (SUB
 *     refused), DISPLAY, PAGE, every error, nothing set after one.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    /// Lines joined as a utility writes them: separated by CR LF, the last not ended.
    std::string lines(const std::vector<std::string> &ls)
    {
        std::string s;
        for (size_t i = 0; i < ls.size(); ++i)
            s += (i ? "\r\n" : "") + ls[i];
        return s;
    }

    /// `line` typed at the CCP: what it wrote between the line's echo and the prompt.
    std::string run(CcpRig &r, const std::string &line, const std::string &prompt = "A>")
    {
        const std::string o = r.cmd(line, prompt);
        const std::string head = line + "\r\n", end = "\r\n" + prompt;
        if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
            o.compare(o.size() - end.size(), end.size(), end) != 0)
            return "<" + o + ">";
        return o.substr(head.size(), o.size() - head.size() - end.size());
    }

    /// The return code after the last command: ":hello" runs HELLO only after a success.
    bool succeeded(CcpRig &r, const std::string &prompt = "A>")
    {
        return r.cmd(":hello", prompt).find("Hello from OODOS") != std::string::npos;
    }

    /// The utilities' build: the build tree's, or OODOS_UTILS_DIR's (a build of a changed copy, to see
    /// a case fail on a fault put in on purpose).
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    /// A rig with these utilities on M: and the search chain the default drive, then M:.
    struct SysRig : CcpRig
    {
        explicit SysRig(const std::vector<std::string> &utils = {"show", "date", "device", "setdef"})
        {
            if (!ok)
                return;
            for (const std::string &u : utils)
                put_util(u, UTILS);
            chain({13, 0}); // M: first: the build's utilities, before the card's on A: (make_card.sh)
        }
        uint8_t scbv(unsigned offset) const { return phys(SCB_PHYS + offset); }
    };

    /// n in decimal, with commas if `commas`, right in `width`.
    std::string num(unsigned long n, size_t width = 0, bool commas = true)
    {
        std::string d = std::to_string(n), s;
        for (size_t i = 0; i < d.size(); ++i)
        {
            if (commas && i && (d.size() - i) % 3 == 0)
                s += ',';
            s += d[i];
        }
        return std::string(s.size() < width ? width - s.size() : 0, ' ') + s;
    }
    std::string pad(const std::string &s, size_t width) { return s + std::string(s.size() < width ? width - s.size() : 0, ' '); }

    /// A diskdef's DPB, the 17 bytes.
    std::vector<uint8_t> dpb(const std::string &name)
    {
        const std::string h = dpb_hex(name);
        std::vector<uint8_t> b;
        for (size_t i = 0; i + 1 < h.size(); i += 2)
            b.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
        return b;
    }
    unsigned word(const std::vector<uint8_t> &b, size_t at) { return unsigned{b[at]} | unsigned{b[at + 1]} << 8; }

    /// CP/M 3's days back to a date, by days().
    void date_of(unsigned n, unsigned &y, unsigned &m, unsigned &d)
    {
        y = 1978;
        while (days(y + 1, 1, 1) <= n)
            ++y;
        m = 1;
        while (m < 12 && days(y, m + 1, 1) <= n)
            ++m;
        d = n - days(y, m, 1) + 1;
    }
    std::string two(unsigned v)
    {
        char b[8];
        std::snprintf(b, sizeof b, "%02u", v % 100);
        return b;
    }
    std::string bcd(uint8_t v) { return two((v >> 4) * 10u + (v & 15u)); }
    /// A stamp (days, hour and minute in BCD) as MM/DD/YY HH:MM, or 14 blanks.
    std::string stamp(const uint8_t *s)
    {
        const unsigned n = unsigned{s[0]} | unsigned{s[1]} << 8;
        if (n == 0)
            return std::string(14, ' ');
        unsigned y, m, d;
        date_of(n, y, m, d);
        return two(m) + "/" + two(d) + "/" + two(y) + " " + bcd(s[2]) + ":" + bcd(s[3]);
    }

    /// A drive's directory read behind OODOS's back, and its numbers worked out as the plan has them.
    struct Dir
    {
        std::vector<uint8_t> d, p;
        unsigned n, bls;
        bool words;
        Dir(CcpRig &r, unsigned unit) // 0 M:, else a card's unit
        {
            CpmDisk disk{r, unit};
            d = disk.directory();
            n = disk.entries();
            bls = disk.bls();
            words = disk.words();
            p = dpb(unit ? "oodos-8m" : "oodos-ram");
        }
        const uint8_t *e(unsigned i) const { return &d[i * 32]; }
        unsigned dsm() const { return word(p, 5); }
        unsigned free_kb() const
        {
            std::vector<bool> used(dsm() + 1, false);
            const unsigned al = unsigned{p[9]} << 8 | p[10];
            for (unsigned b = 0; b < 16; ++b)
                if (al & (0x8000u >> b))
                    used[b] = true;
            for (unsigned i = 0; i < n; ++i)
                if (e(i)[0] < 16)
                    for (unsigned k = 0; k < (words ? 8u : 16u); ++k)
                    {
                        const unsigned b = words ? unsigned{e(i)[16 + 2 * k]} | unsigned{e(i)[17 + 2 * k]} << 8 : e(i)[16 + k];
                        if (b && b <= dsm())
                            used[b] = true;
                    }
            unsigned f = 0;
            for (bool u : used)
                f += !u;
            return f * bls / 1024;
        }
        unsigned free_entries() const
        {
            unsigned f = 0;
            for (unsigned i = 0; i < n; ++i)
                f += e(i)[0] == 0xE5;
            return f;
        }
        /// Each user's files: extent-zero entries (S2 0, EX within EXM).
        std::vector<unsigned> files() const
        {
            std::vector<unsigned> c(16, 0);
            for (unsigned i = 0; i < n; ++i)
                if (e(i)[0] < 16 && e(i)[14] == 0 && (e(i)[12] & ~p[4] & 0x1F) == 0)
                    ++c[e(i)[0]];
            return c;
        }
        int label() const
        {
            for (unsigned i = 0; i < n; ++i)
                if (e(i)[0] == 0x20)
                    return static_cast<int>(i);
            return -1;
        }
    };

    std::string space_line(char drive, bool ro, unsigned kb)
    {
        return std::string(1, drive) + ": " + (ro ? "RO" : "RW") + ", Space: " + num(kb, 9) + "K";
    }
    std::vector<std::string> users_lines(char drive, unsigned user, const Dir &d)
    {
        const std::vector<unsigned> c = d.files();
        std::string u = "Active Files: ", f = std::string(1, drive) + ": # of files:";
        for (unsigned i = 0; i < 16; ++i)
            if (c[i])
                u += num(i, 4), f += num(c[i], 4);
        return {"Active User :" + num(user, 3), u, f,
                std::string(1, drive) + ": Number of free directory entries: " + std::to_string(d.free_entries())};
    }
    std::vector<std::string> drive_lines(char drive, const Dir &d)
    {
        const std::vector<uint8_t> &p = d.p;
        const unsigned long recs = static_cast<unsigned long>(d.dsm() + 1) << p[2];
        return {"    " + std::string(1, drive) + ": Drive Characteristics",
                num(recs, 7) + ": 128 Byte Record Capacity",
                num(recs / 8, 7) + ": Kilobyte Drive Capacity",
                num(word(p, 7) + 1, 7) + ": 32 Byte Directory Entries",
                num((word(p, 11) & 0x7FFF) * 4, 7) + ": Checked Directory Entries", // a CKS byte a record
                num((p[4] + 1u) * 128, 7) + ": Records / Directory Entry",
                num(p[3] + 1u, 7) + ": Records / Block",
                num(word(p, 0), 7) + ": Sectors / Track",
                num(128u << p[15], 7) + ": Bytes / Physical Record"};
    }
    /// The name as CM_FNAME writes it: NAME.TYP, NAME for a blank type.
    std::string fname(const uint8_t *e)
    {
        std::string n, t;
        for (unsigned i = 1; i < 9 && (e[i] & 0x7F) != ' '; ++i)
            n += static_cast<char>(e[i] & 0x7F);
        for (unsigned i = 9; i < 12 && (e[i] & 0x7F) != ' '; ++i)
            t += static_cast<char>(e[i] & 0x7F);
        return t.empty() ? n : n + "." + t;
    }
    std::vector<std::string> label_lines(char drive, const uint8_t *l)
    {
        const uint8_t mode = l[12];
        auto onoff = [](bool on, size_t w) { return pad(on ? "on" : "off", w); };
        return {"Label for drive " + std::string(1, drive) + ":", "",
                "Directory     Passwds  Stamp   Stamp   Label Created   Label Updated",
                std::string("Label         Reqd     ") + (mode & 0x40 ? "Access" : "Create") + "  Update",
                "------------  -------  ------  ------  --------------  --------------",
                pad(fname(l), 14) + onoff(mode & 0x80, 9) + onoff(mode & 0x50, 8) + onoff(mode & 0x20, 8) + stamp(l + 24) +
                    "  " + stamp(l + 28)};
    }

    // ---- DATE's oracle

    /// The clock's time, read behind OODOS's back.
    Ds3231::Time now(CcpRig &r) { return r.bridge().rtc()->time(); }
    /// "Sat 10/31/26 12:00:00" for a date and time.
    std::string date_line(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s)
    {
        static const char *const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        return std::string(WD[(days(y, mo, d) - 1) % 7]) + " " + two(mo) + "/" + two(d) + "/" + two(y) + " " + two(h) +
               ":" + two(mi) + ":" + two(s);
    }
    std::string date_line(const Ds3231::Time &t) { return date_line(t.year, t.month, t.date, t.hour, t.minute, t.second); }
    /// A time a second earlier, within the day.
    Ds3231::Time earlier(Ds3231::Time t)
    {
        if (t.second)
            --t.second;
        else if (t.minute)
            t.second = 59, --t.minute;
        else if (t.hour)
            t.second = 59, t.minute = 59, --t.hour;
        return t;
    }
    /// `got` is the clock's line now, or a second before (the clock may have ticked since).
    void same_clock(CcpRig &r, const std::string &got)
    {
        const Ds3231::Time t = now(r);
        const std::string a = date_line(t), b = date_line(earlier(t));
        CHECK(got == a || got == b);
        if (got != a && got != b)
            std::printf("  got  '%s'\n  want '%s' or '%s'\n", vis(got).c_str(), a.c_str(), b.c_str());
    }

    // ---- DEVICE's oracle

    std::vector<std::string> names_lines()
    {
        return {"Physical Devices:", "I=Input,O=Output,S=Serial,X=Xon-Xoff",
                pad("CRT     NONE IO", 21) + pad("TTY    19200 IOS", 21) + "LPT     NONE O"};
    }
    /// The SCB's vector (0 CONIN: to 4 LST:), as a word.
    unsigned vector(SysRig &r, unsigned v) { return r.scbv(0x22 + 2 * v) | r.scbv(0x23 + 2 * v) << 8; }
    std::string vline(unsigned v, unsigned bits)
    {
        static const char *const N[] = {"CONIN: ", "CONOUT:", "AUXIN: ", "AUXOUT:", "LST:   "};
        static const char *const D[] = {"CRT", "TTY", "LPT"};
        std::string s = std::string(N[v]) + " = ";
        if (!(bits & 0xE000))
            return s + "Null Device";
        bool first = true;
        for (unsigned i = 0; i < 3; ++i)
            if (bits & (0x8000u >> i))
                s += (first ? "" : " ") + std::string(D[i]), first = false;
        return s;
    }
    std::vector<std::string> values_lines(SysRig &r)
    {
        std::vector<std::string> ls = {"Current Assignments:"};
        for (unsigned v = 0; v < 5; ++v)
            ls.push_back(vline(v, vector(r, v)));
        return ls;
    }
} // namespace

// ---- SHOW ------------------------------------------------------------------------------------

TEST(show_space_is_each_logged_in_drives_access_mode_and_free_kilobytes)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    r.put_m({{0, "ONE     TXT", std::vector<uint8_t>(5000, 'x')}, {3, "TWO     DAT", bytes("2")}});
    // A: logged in by the built-in DIR, and M: by the search that found SHOW.COM (M: first on the
    // chain, so the search no longer logs A: in)
    same(r.cmd("dir a:x"), "dir a:x\r\nNo File\r\nA>");
    const std::string all = run(r, "show");
    Dir a(r, 1), m(r, 0);
    same(all, lines({space_line('A', false, a.free_kb()), space_line('M', false, m.free_kb())}));
    CHECK(m.free_kb() <= 120u - 2 * 4); // 60 data blocks of 2 KB less SHOW's, ONE's 3 and TWO's
    CHECK(succeeded(r));
    // One drive: B:, logged in by it; then every drive has it, A to P
    Dir b(r, 2);
    same(run(r, "show b:"), space_line('B', false, b.free_kb()));
    same(run(r, "m:show [space]"), lines({space_line('A', false, a.free_kb()), space_line('B', false, b.free_kb()),
                                          space_line('M', false, m.free_kb())}));
    same(run(r, "show m:[s]"), space_line('M', false, m.free_kb()));
    same(run(r, "show  m: [ SPACE ] "), space_line('M', false, m.free_kb()));
    same(run(r, "show []"), lines({space_line('A', false, a.free_kb()), space_line('B', false, b.free_kb()),
                                   space_line('M', false, m.free_kb())}));
    // A drive that isn't: the BDOS's error, and the return code
    const std::string h = run(r, "show h:");
    CHECK(h.find("OODOS Error On H: Invalid Drive") != std::string::npos);
    CHECK(!succeeded(r));
}

TEST(show_space_says_ro_for_a_read_only_drive)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    REQUIRE(r.mount(2, "/OODOS/B.IMG", true) == 0); // B: read-only before its first select
    Dir b(r, 2);
    same(run(r, "show b:"), space_line('B', true, b.free_kb()));
    same(run(r, "show"), lines({space_line('A', false, Dir(r, 1).free_kb()), space_line('B', true, b.free_kb()),
                                space_line('M', false, Dir(r, 0).free_kb())}));
}

TEST(show_label_shows_the_drives_label_and_its_modes)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    r.poke(MDISK + 12, 0xE1); // M:'s label: passwords, access and update stamps
    Dir m(r, 0);
    REQUIRE(m.label() == 0);
    same(run(r, "show m:[label]"), lines(label_lines('M', m.e(0))));
    CHECK(run(r, "show m:[l]").find("UNLABELE.D    on       on      on      10/30/26 12:00  10/30/26 12:00") !=
          std::string::npos);
    CHECK(succeeded(r));
    Dir a(r, 1), b(r, 2);
    REQUIRE(a.label() >= 0 && b.label() >= 0);
    same(run(r, "show [label]"), lines(label_lines('A', a.e(static_cast<unsigned>(a.label())))));
    CHECK(run(r, "show [label]").find("Label         Reqd     Create  Update") != std::string::npos);
    same(run(r, "show b: [lab]"), lines(label_lines('B', b.e(static_cast<unsigned>(b.label())))));
    CHECK(run(r, "show b: [lab]").find("\r\nWORK          off      on      on      ") != std::string::npos);
}

TEST(show_label_without_one_says_so)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    r.poke(MDISK, 0xE5); // M:'s label gone, before M: is logged in
    same(run(r, "show m:[label]"), "No directory label exists.");
    CHECK(!succeeded(r));
}

TEST(show_users_counts_each_users_files_by_their_extent_zero_entries)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    r.put_m({{0, "ONE     TXT", bytes("1")},
             {0, "BIG     DAT", std::vector<uint8_t>(140 * 128, 'b')}, // one entry, EX 1: extent zero's
             {3, "TWO     DAT", bytes("2")},
             {3, "SYS     DAT", bytes("3"), true},
             {12, "TWELVE  TXT", bytes("12")}});
    // ONE.TXT's second logical extent group (EX 2), an XFCB: neither is a file
    std::vector<uint8_t> e(32, 0);
    e[0] = 0;
    std::memcpy(&e[1], "ONE     TXT", 11);
    e[12] = 2;
    r.poke(MDISK + r.free_entry() * 32, e);
    r.xfcb_m(3, "TWO     DAT", 0x80, "SECRET  ");
    Dir m(r, 0);
    const std::vector<unsigned> c = m.files();
    CHECK(c[0] == 3 && c[3] == 2 && c[12] == 1); // ONE, BIG and SHOW; TWO and SYS; TWELVE
    same(run(r, "show m:[users]"), lines(users_lines('M', 0, m)));
    CHECK(run(r, "show m:[u]").find("Active Files:    0   3  12\r\nM: # of files:   3   2   1\r\n") != std::string::npos);
    same(r.cmd("12a:", "12A>"), "12a:\r\r\n12A>");
    same(run(r, "show m:[users]", "12A>"), lines(users_lines('M', 12, m)));
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    Dir a(r, 1);
    same(run(r, "show [users]"), lines(users_lines('A', 0, a)));
}

TEST(show_dir_counts_the_free_entries_and_drive_the_dpbs_numbers)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    Dir m(r, 0);
    same(run(r, "show m:[dir]"), "M: Number of free directory entries: " + std::to_string(m.free_entries()));
    CHECK_EQ(m.free_entries(), 256u - 64 - 2); // the SFCBs, the label and SHOW.COM aren't free
    Dir a(r, 1), b(r, 2);
    same(run(r, "show [di]"), "A: Number of free directory entries: " + std::to_string(a.free_entries()));
    same(run(r, "show b:[dir]"), "B: Number of free directory entries: " + std::to_string(b.free_entries()));
    CHECK_EQ(b.free_entries(), 1024u - 256 - 1);
    same(run(r, "show [drive]"), lines(drive_lines('A', a)));
    same(run(r, "show b:[dr]"), lines(drive_lines('B', b)));
    same(run(r, "show m:[drive]"), lines(drive_lines('M', m)));
    CHECK(run(r, "show m:[drive]") ==
          lines({"    M: Drive Characteristics", "  1,024: 128 Byte Record Capacity", "    128: Kilobyte Drive Capacity",
                 "    256: 32 Byte Directory Entries", "      0: Checked Directory Entries",
                 "    256: Records / Directory Entry", "     16: Records / Block", "     64: Sectors / Track",
                 "    512: Bytes / Physical Record"}));
    CHECK(succeeded(r));
}

TEST(show_drive_counts_four_checked_entries_a_cks_byte)
{
    // Every OODOS DPB has CKS $8000 (fixed media): M:'s, in common memory, given a check vector's size
    // for the run (the BDOS doesn't read CKS), one byte a directory record of 4 entries.
    SysRig r({"show"});
    REQUIRE(r.ok);
    const uint32_t cks = COMMON_PHYS + (sym("DPBM") - 0xF800u) + 11;
    REQUIRE(r.phys(cks) == 0x00 && r.phys(cks + 1) == 0x80);
    auto checked = [&](uint8_t lo, uint8_t hi) {
        r.poke(cks, std::vector<uint8_t>{lo, hi});
        const std::string o = run(r, "show m:[drive]");
        const size_t at = o.find(": Checked Directory Entries");
        return at == std::string::npos || at < 7 ? o : o.substr(at - 7, 7);
    };
    same(checked(0x40, 0x00), "    256"); // a removable drive's (DRM + 1) / 4
    same(checked(0x10, 0x80), "     64"); // bit 15, a permanent drive's, not counted
    same(checked(0xFF, 0x7F), "131,068");
    same(checked(0x00, 0x80), "      0"); // as it was
    CHECK(succeeded(r));
}

TEST(show_refuses_what_it_doesnt_know)
{
    SysRig r({"show"});
    REQUIRE(r.ok);
    same(run(r, "show q:"), "Unrecognized drive.");
    CHECK(!succeeded(r));
    same(run(r, "show [foo]"), "Unrecognized option.");
    same(run(r, "show [d]"), "Unrecognized option."); // DIR or DRIVE
    same(run(r, "show [space,label]"), "Unrecognized option.");
    same(run(r, "show [label=x]"), "Unrecognized option.");
    same(run(r, "show x"), "Unrecognized input.");
    same(run(r, "show ab:"), "Unrecognized input.");
    same(run(r, "show 1:"), "Unrecognized input.");
    same(run(r, "show [label"), "Unrecognized input.");
    same(run(r, "show [label] x"), "Unrecognized input.");
    same(run(r, "show [label] [dir]"), "Unrecognized input.");
    same(run(r, "show m: b:"), "Unrecognized input.");
    CHECK(!succeeded(r));
    same(run(r, "m:show m:[dir]").substr(0, 2), "M:");
    CHECK(succeeded(r));
}

// ---- DATE ------------------------------------------------------------------------------------

TEST(date_shows_the_clock_and_continuous_rewrites_it_each_second_to_a_key)
{
    SysRig r({"date"});
    REQUIRE(r.ok);
    same_clock(r, run(r, "date"));
    CHECK(run(r, "m:date").substr(0, 13) == "Fri 10/30/26 "); // CcpRig's clock: 30 October 2026, a Friday
    CHECK(succeeded(r));
    r.set_time(2026, 10, 31, 23, 59, 58);
    same_clock(r, run(r, "date"));
    CHECK(run(r, "date").substr(0, 13) == "Sat 10/31/26 "); // the plan's example's day
    // CONTINUOUS (any prefix): the line again after a CR, each second, until a key, which is taken
    for (const std::string c : {"date c", "date continuous", "date CONT"})
    {
        const size_t from = r.out.size();
        r.type(c + "\r");
        REQUIRE(r.until("\r" + std::string("Sun 11/01/26 00:00:0"), ms(9000), from + c.size() + 2));
        REQUIRE(r.until("\r", ms(3000), r.out.size()));
        r.type("x");
        REQUIRE(r.until("\r\nA>", ms(3000), from));
        const std::string o = r.out.substr(from + c.size() + 2, r.out.size() - from - c.size() - 2 - 4);
        // Each part a second after the one before it, on the same line
        std::vector<std::string> parts;
        for (size_t at = 0, cr; at <= o.size(); at = cr + 1)
        {
            cr = o.find('\r', at);
            if (cr == std::string::npos)
                cr = o.size();
            parts.push_back(o.substr(at, cr - at));
        }
        CHECK(parts.size() >= 3);
        CHECK(o.find('\n') == std::string::npos);
        for (size_t i = 1; i < parts.size(); ++i)
        {
            const std::string want_a = parts[i - 1].substr(0, 18), got_a = parts[i].substr(0, 18);
            const unsigned s0 = static_cast<unsigned>(std::stoul(parts[i - 1].substr(19))), s1 = static_cast<unsigned>(std::stoul(parts[i].substr(19)));
            CHECK(parts[i].size() == 21);
            CHECK(s1 == (s0 + 1) % 60);
            (void)want_a, (void)got_a;
        }
        CHECK(succeeded(r)); // the key taken: ":hello" is the line, not "x:hello"
        r.set_time(2026, 10, 31, 23, 59, 58);
    }
}

TEST(date_sets_the_clock_at_a_key_with_its_seconds)
{
    SysRig r({"date"});
    REQUIRE(r.ok);
    size_t from = r.out.size();
    r.type("date 2/29/28 23:59:58\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.run(ms(1500)); // the clock isn't set until the key
    CHECK(now(r).year == 2026);
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    std::string o = r.out.substr(from);
    const std::string head = "date 2/29/28 23:59:58\r\nPress any key to set time\r\n";
    REQUIRE(o.compare(0, head.size(), head) == 0);
    same_clock(r, o.substr(head.size(), o.size() - head.size() - 4));
    CHECK(o.find("Tue 02/29/28 23:59:5") != std::string::npos); // 2028's 29 February, a Tuesday
    const Ds3231::Time t = now(r);
    CHECK(t.year == 2028 && t.month == 2 && t.date == 29 && t.hour == 23 && t.minute == 59 && t.second >= 58);
    CHECK_EQ(unsigned{r.scbv(0x58)} | unsigned{r.scbv(0x59)} << 8, days(2028, 2, 29)); // the SCB's date, by 49
    CHECK(succeeded(r));
    // Two digits each, leading zeros or not; the seconds kept (104 would make them 0)
    from = r.out.size();
    r.type("date 01/05/99 07:08:37\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type("k");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("Mon 01/05/99 07:08:3", from) != std::string::npos);
    CHECK(now(r).year == 2099 && now(r).second >= 37);
}

TEST(date_refuses_a_date_or_time_out_of_range_and_any_other_form)
{
    SysRig r({"date"});
    REQUIRE(r.ok);
    for (const char *bad : {"date 13/01/26 00:00:00", "date 0/01/26 00:00:00", "date 02/29/27 00:00:00",
                            "date 02/30/28 00:00:00", "date 04/31/26 00:00:00", "date 10/0/26 00:00:00",
                            "date 10/31/26 24:00:00", "date 10/31/26 12:60:00", "date 10/31/26 12:00:60",
                            "date 10/31/26", "date 10/31/26 12:00", "date 10/31/2026 12:00:00",
                            "date 010/31/26 12:00:00", "date 10-31-26 12:00:00", "date 10/31/26,12:00:00",
                            "date 10/31/26 12:00:00 x", "date 10/31/26 12:00:00:00", "date foo", "date set x",
                            "date c x", "date continuousx", "date s", "date setx", "date 10/31/26x12:00:00"})
    {
        same(run(r, bad), "Illegal date/time specification.");
        CHECK(!succeeded(r));
    }
    CHECK(now(r).year == 2026 && now(r).month == 10 && now(r).date == 30); // nothing set
    // A clock past 2155: no clock to show; set, it is shown again
    r.set_time(2170, 1, 1, 0, 0, 0);
    same(run(r, "date"), "No clock");
    CHECK(!succeeded(r));
    same(run(r, "date c"), "No clock");
    const size_t from = r.out.size();
    r.type("date 10/31/26 09:00:00\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("\r\nSat 10/31/26 09:00:0", from) != std::string::npos);
    CHECK(now(r).year == 2026);
}

TEST(date_set_asks_for_the_date_and_time_an_empty_line_keeping_each)
{
    SysRig r({"date"});
    REQUIRE(r.ok);
    // A date given, the time kept: the clock's at the key
    size_t from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("Enter today's date (MM/DD/YY): ", ms(3000), from));
    r.type("12/25/30\r");
    REQUIRE(r.until("Enter the time (HH:MM:SS): ", ms(3000), from));
    r.type("\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    std::string o = r.out.substr(from);
    std::string head = "date set\r\nEnter today's date (MM/DD/YY): 12/25/30\r\nEnter the time (HH:MM:SS): \r\n"
                       "Press any key to set time\r\n";
    REQUIRE(o.compare(0, head.size(), head) == 0);
    same_clock(r, o.substr(head.size(), o.size() - head.size() - 4));
    Ds3231::Time t = now(r);
    CHECK(t.year == 2030 && t.month == 12 && t.date == 25 && t.hour == 12 && t.minute == 0);
    // The date kept, a time given
    from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("(MM/DD/YY): ", ms(3000), from));
    r.type("\r");
    REQUIRE(r.until("(HH:MM:SS): ", ms(3000), from));
    r.type("8:15:30\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    o = r.out.substr(from);
    head = "date set\r\nEnter today's date (MM/DD/YY): \r\nEnter the time (HH:MM:SS): 8:15:30\r\n"
           "Press any key to set time\r\n";
    REQUIRE(o.compare(0, head.size(), head) == 0);
    CHECK(o.find("Wed 12/25/30 08:15:3") == head.size());
    t = now(r);
    CHECK(t.year == 2030 && t.month == 12 && t.date == 25 && t.hour == 8 && t.minute == 15 && t.second >= 30);
    CHECK(succeeded(r));
    // Both kept: the clock goes on as it was
    from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("(MM/DD/YY): ", ms(3000), from));
    r.type("\r");
    REQUIRE(r.until("(HH:MM:SS): ", ms(3000), from));
    r.type("\r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    t = now(r);
    CHECK(t.year == 2030 && t.month == 12 && t.date == 25 && t.hour == 8 && t.minute == 15);
    // A line of only blanks and tabs is an empty one: both kept again
    from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("(MM/DD/YY): ", ms(3000), from));
    r.type("   \r");
    REQUIRE(r.until("(HH:MM:SS): ", ms(3000), from));
    r.type(" \t \r");
    REQUIRE(r.until("Press any key to set time", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("Illegal", from) == std::string::npos);
    t = now(r);
    CHECK(t.year == 2030 && t.month == 12 && t.date == 25 && t.hour == 8);
    CHECK(succeeded(r));
    // A bad line: refused there, nothing set
    from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("(MM/DD/YY): ", ms(3000), from));
    r.type("2/30/31\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "date set\r\nEnter today's date (MM/DD/YY): 2/30/31\r\nIllegal date/time specification.\r\nA>");
    from = r.out.size();
    r.type("date set\r");
    REQUIRE(r.until("(MM/DD/YY): ", ms(3000), from));
    r.type("\r");
    REQUIRE(r.until("(HH:MM:SS): ", ms(3000), from));
    r.type("25:00:00\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("25:00:00\r\nIllegal date/time specification.\r\nA>", from) != std::string::npos);
    CHECK(!succeeded(r));
    CHECK(now(r).year == 2030 && now(r).hour == 8);
}

// ---- DEVICE ----------------------------------------------------------------------------------

TEST(device_shows_the_devices_and_assignments_and_takes_a_line)
{
    SysRig r({"device"});
    REQUIRE(r.ok);
    std::vector<std::string> all = names_lines();
    all.push_back("");
    for (const std::string &l : values_lines(r))
        all.push_back(l);
    all.push_back("");
    CHECK(all[5] == "CONIN:  = CRT TTY" && all[7] == "AUXIN:  = TTY" && all[9] == "LST:    = LPT"); // cold boot's
    size_t from = r.out.size();
    r.type("device\r");
    REQUIRE(r.until("Enter new assignment or hit RETURN: ", ms(5000), from));
    r.type("\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "device\r\n" + lines(all) + "\r\nEnter new assignment or hit RETURN: \r\r\nA>");
    CHECK(succeeded(r));
    // The line is a DEVICE command's tail
    from = r.out.size();
    r.type("device\r");
    REQUIRE(r.until("RETURN: ", ms(5000), from));
    r.type("aux:=crt,tty\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK_EQ(vector(r, 2), 0xC000u);
    CHECK_EQ(vector(r, 3), 0xC000u);
    from = r.out.size();
    r.type("m:device\r");
    REQUIRE(r.until("RETURN: ", ms(5000), from));
    r.type("tty\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("RETURN: tty\r\nTTY    19200 IOS\r\nA>", from) != std::string::npos);
    same(run(r, "device names"), lines(names_lines()));
    same(run(r, "device values"), lines(values_lines(r)));
    CHECK(run(r, "device values").find("AUXIN:  = CRT TTY\r\nAUXOUT: = CRT TTY\r\n") != std::string::npos);
}

TEST(device_shows_a_physical_or_logical_device_by_any_of_its_names)
{
    SysRig r({"device"});
    REQUIRE(r.ok);
    same(run(r, "device crt"), "CRT     NONE IO");
    same(run(r, "device tty"), "TTY    19200 IOS");
    same(run(r, "device lpt:"), "LPT     NONE O");
    same(run(r, "device conin:"), vline(0, 0xC000));
    same(run(r, "device conout"), vline(1, 0xC000));
    same(run(r, "device auxin:"), "AUXIN:  = TTY");
    same(run(r, "device auxout:"), "AUXOUT: = TTY");
    same(run(r, "device lst:"), "LST:    = LPT");
    same(run(r, "device con:"), lines({"CONIN:  = CRT TTY", "CONOUT: = CRT TTY"}));
    same(run(r, "device console"), lines({"CONIN:  = CRT TTY", "CONOUT: = CRT TTY"}));
    same(run(r, "device keyboard"), "CONIN:  = CRT TTY");
    same(run(r, "device aux:"), lines({"AUXIN:  = TTY", "AUXOUT: = TTY"}));
    same(run(r, "device auxiliary"), lines({"AUXIN:  = TTY", "AUXOUT: = TTY"}));
    same(run(r, "device printer:"), "LST:    = LPT");
    CHECK(succeeded(r));
}

TEST(device_assigns_a_logical_device_to_physical_ones_or_null)
{
    SysRig r({"device"});
    REQUIRE(r.ok);
    same(run(r, "device auxout:=lpt,tty"), "");
    CHECK_EQ(vector(r, 3), 0x6000u);
    CHECK(succeeded(r));
    same(run(r, "device lst: = null"), "");
    CHECK_EQ(vector(r, 4), 0u);
    same(run(r, "device lst:"), "LST:    = Null Device");
    same(run(r, "device printer=tty , crt"), "");
    CHECK_EQ(vector(r, 4), 0xC000u);
    same(run(r, "device keyboard = tty"), "");
    CHECK_EQ(vector(r, 0), 0x4000u);
    same(run(r, "device auxiliary:=crt"), "");
    CHECK(vector(r, 2) == 0x8000u && vector(r, 3) == 0x8000u);
    same(run(r, "device con:=tty"), ""); // the serial line is still the console
    CHECK(vector(r, 0) == 0x4000u && vector(r, 1) == 0x4000u);
    same(run(r, "device console=crt,tty"), "");
    CHECK(vector(r, 0) == 0xC000u && vector(r, 1) == 0xC000u);
    same(run(r, "device aux:=tty [noxon]"), ""); // NOXON: so it is
    CHECK(vector(r, 2) == 0x4000u && vector(r, 3) == 0x4000u);
    same(run(r, "device aux:=tty [no xon],crt[noxon]"), "");
    CHECK(vector(r, 2) == 0xC000u);
    // Input for an input vector, output for an output one
    same(run(r, "device conin:=lpt"), "Invalid Physical device.");
    same(run(r, "device aux:=crt,lpt"), "Invalid Physical device.");
    CHECK(!succeeded(r));
    CHECK(vector(r, 0) == 0xC000u && vector(r, 2) == 0xC000u); // nothing set
    same(run(r, "device auxout:=lpt"), "");
    CHECK_EQ(vector(r, 3), 0x2000u);
    // The rest of a line ignored, and said
    same(run(r, "device lst:=null x"), "End of line expected");
    CHECK_EQ(vector(r, 4), 0u);
    CHECK(!succeeded(r));
    same(run(r, "device names x"), lines({names_lines()[0], names_lines()[1], names_lines()[2], "End of line expected"}));
    same(run(r, "device tty extra"), "TTY    19200 IOS\r\nEnd of line expected");
}

TEST(device_refuses_baud_rates_and_xon_and_says_each_error)
{
    SysRig r({"device"});
    REQUIRE(r.ok);
    same(run(r, "device tty [9600]"), "Baud rate cannot be set for this device");
    CHECK(!succeeded(r));
    same(run(r, "device tty [134]"), "Baud rate cannot be set for this device");
    same(run(r, "device aux:=tty [xon]"), "Xon-Xoff cannot be set for this device");
    same(run(r, "device lpt [xon,9600]"), "Xon-Xoff cannot be set for this device");
    same(run(r, "device aux:=crt [19200]"), "Baud rate cannot be set for this device");
    CHECK(vector(r, 2) == 0x4000u); // nothing set
    same(run(r, "device tty [noxon]"), "");
    CHECK(succeeded(r));
    same(run(r, "device tty [9601]"), "Invalid number.");
    same(run(r, "device tty [99999]"), "Invalid number.");
    same(run(r, "device tty [fast]"), "Invalid option.");
    same(run(r, "device tty [xon=1]"), "Invalid option.");
    same(run(r, "device tty [noxon"), "Invalid option.");
    same(run(r, "device foo:=crt"), "Bad Logical Device Assignment");
    same(run(r, "device =crt"), "Bad Logical Device Assignment");
    same(run(r, "device con:=foo"), "Invalid Physical device.");
    same(run(r, "device con:="), "Invalid Physical device.");
    same(run(r, "device con:=null,crt"), "Invalid Physical device.");
    same(run(r, "device con:=crt,null"), "Invalid Physical device.");
    same(run(r, "device xyz"), "Invalid Physical device.");
    same(run(r, "device con:=crt;tty"), "Invalid delimiter.");
    same(run(r, "device con: crt"), "Invalid delimiter.");
    same(run(r, "device con:=crt tty"), "Invalid delimiter.");
    same(run(r, "device con: [xon]"), "Invalid option.");
    same(run(r, "device lst: [page]"), "Invalid option.");
    CHECK(!succeeded(r));
    CHECK(vector(r, 0) == 0xC000u && vector(r, 1) == 0xC000u && vector(r, 4) == 0x2000u);
}

TEST(device_console_shows_and_sets_the_width_and_page)
{
    SysRig r({"device"});
    REQUIRE(r.ok);
    same(run(r, "device console [page]"), lines({"Console width set to 80 columns", "Console page set to 24 lines"}));
    CHECK(succeeded(r));
    same(run(r, "device console [columns=40, lines=16]"),
         lines({"Console width set to 40 columns", "Console page set to 16 lines"}));
    CHECK(r.scbv(0x1A) == 39 && r.scbv(0x1C) == 16);
    same(run(r, "device con: [lines=0 col=256]"), lines({"Console width set to 256 columns", "Console page set to 0 lines"}));
    CHECK(r.scbv(0x1A) == 255 && r.scbv(0x1C) == 0);
    same(run(r, "device console [c=1,l=255,p]"), lines({"Console width set to 1 columns", "Console page set to 255 lines"}));
    CHECK(r.scbv(0x1A) == 0 && r.scbv(0x1C) == 255);
    r.scb(0x1A, 79); // (function 10 shows the typed line on rows of the width)
    for (const char *bad : {"device console [columns=0]", "device console [columns=257]", "device console [lines=256]",
                            "device console [columns]", "device console [columns=abc]", "device console [lines=-1]",
                            "device console [columns=(40,50)]", "device console [lines=(16)]"}) // one number, no list
        same(run(r, bad), "Invalid number.");
    same(run(r, "device console [size]"), "Invalid option.");
    same(run(r, "device console [page=1]"), "Invalid option.");
    same(run(r, "device console [page"), "Invalid option.");
    CHECK(!succeeded(r));
    CHECK(r.scbv(0x1A) == 79 && r.scbv(0x1C) == 255); // nothing set
    same(run(r, "device console [columns=80,lines=24]"),
         lines({"Console width set to 80 columns", "Console page set to 24 lines"}));
}

// ---- SETDEF ----------------------------------------------------------------------------------

namespace
{
    std::vector<std::string> setdef_lines(const std::vector<std::string> &chain, const std::string &temp = "Default",
                                          bool page = true, bool display = false)
    {
        static const char *const ORD[] = {"1st", "2nd", "3rd", "4th"};
        std::vector<std::string> ls = {"Drive Search Path:"};
        for (size_t i = 0; i < chain.size(); ++i)
            ls.push_back(std::string(ORD[i]) + " Drive            - " + chain[i]);
        ls.push_back("");
        ls.push_back("Search Order         - COM");
        ls.push_back("Temporary Drive      - " + temp);
        ls.push_back(std::string("Console Page Mode    - ") + (page ? "On" : "Off"));
        ls.push_back(std::string("Program Name Display - ") + (display ? "On" : "Off"));
        return ls;
    }
    std::vector<unsigned> chain_of(SysRig &r)
    {
        std::vector<unsigned> c;
        for (unsigned i = 0; i < 4; ++i)
            c.push_back(r.scbv(0x4C + i));
        return c;
    }
} // namespace

TEST(the_cards_show_date_device_and_setdef_run_from_a_as_the_builds_do)
{
    // The committed card's own copies (make_card.sh put them on A:, user 0, SYS): run by A:NAME,
    // what they say as the build's on M: say it (test_oodos_boot checks they are the same bytes)
    SysRig r;
    REQUIRE(r.ok);
    Dir a(r, 1);
    same(run(r, "a:show a:[dir]"), "A: Number of free directory entries: " + std::to_string(a.free_entries()));
    CHECK_EQ(a.free_entries(), 1024u - 256 - 1 - 23); // the label, and 21 files' 23 entries, ten utilities among them
    same(run(r, "a:show [label]"), lines(label_lines('A', a.e(static_cast<unsigned>(a.label())))));
    same(run(r, "a:show [drive]"), lines(drive_lines('A', a)));
    same(run(r, "a:setdef"), lines(setdef_lines({"M:", "Default"})));
    same(run(r, "a:device names"), run(r, "m:device names"));
    same(run(r, "a:device values"), lines(values_lines(r)));
    CHECK(run(r, "a:date").substr(0, 13) == "Fri 10/30/26 "); // CcpRig's clock
    CHECK(succeeded(r));
    // From the chain without M: (the default drive, A:, alone): found on A:
    r.chain({0});
    same(run(r, "show [dir]"), "A: Number of free directory entries: " + std::to_string(a.free_entries()));
}

TEST(setdef_shows_and_sets_the_drive_search_chain)
{
    SysRig r({"setdef"});
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}}); // (before M: is logged in)
    same(run(r, "setdef"), lines(setdef_lines({"M:", "Default"})));
    CHECK(succeeded(r));
    // The chain decides where a program is found
    same(run(r, "setdef *"), lines(setdef_lines({"Default"})));
    CHECK(chain_of(r) == std::vector<unsigned>({0, 0xFF, 0xFF, 0xFF}));
    same(r.cmd("args"), "args\r\nARGS?\r\nA>");
    same(run(r, "setdef"), lines(setdef_lines({"Default"}))); // M: is out of the chain: A:'s, the card's
    same(run(r, "m:setdef m:,*"), lines(setdef_lines({"M:", "Default"})));
    CHECK(chain_of(r) == std::vector<unsigned>({13, 0, 0xFF, 0xFF}));
    CHECK_EQ(field(r.cmd("args"), "ARGS"), std::string("M0  "));
    same(run(r, "setdef a:, b: ,m:,*"), lines(setdef_lines({"A:", "B:", "M:", "Default"})));
    CHECK(chain_of(r) == std::vector<unsigned>({1, 2, 13, 0}));
    CHECK_EQ(field(r.cmd("args"), "ARGS"), std::string("M0  "));
    same(run(r, "setdef p:,m:"), lines(setdef_lines({"P:", "M:"}))); // P: isn't a drive: passed over
    CHECK_EQ(field(r.cmd("args"), "ARGS"), std::string("M0  "));
    // Errors: nothing set
    same(run(r, "setdef a:,b:,c:,d:,e:"), "More than four drives specified.");
    CHECK(!succeeded(r));
    same(run(r, "setdef a:,m:,a:"), "Drive defined twice in search Path");
    same(run(r, "setdef *,m:,*"), "Drive defined twice in search Path");
    same(run(r, "setdef q:"), "Invalid drive.");
    same(run(r, "setdef 1:"), "Invalid drive.");
    same(run(r, "setdef a"), "Invalid drive.");
    same(run(r, "setdef a:,"), "Invalid drive.");
    same(run(r, "setdef a:;m:"), "Invalid delimiter.");
    same(run(r, "setdef a: m:"), "Invalid delimiter.");
    same(run(r, "setdef a: [foo]"), "Invalid option.");
    CHECK(!succeeded(r));
    CHECK(chain_of(r) == std::vector<unsigned>({16, 13, 0xFF, 0xFF}));
}

TEST(setdef_sets_the_temporary_drive_the_order_display_and_page_modes)
{
    SysRig r({"setdef"});
    REQUIRE(r.ok);
    std::vector<MFile> files = {{0, "ARGS    COM", args("M0  ")}}; // (before M: is logged in)
    for (unsigned i = 0; i < 6; ++i)
        files.push_back({0, "F" + std::to_string(i) + "      TXT", bytes("x")});
    r.put_m(files);
    same(run(r, "setdef [temporary=c:]"), lines(setdef_lines({"M:", "Default"}, "C:")));
    CHECK_EQ(unsigned{r.scbv(0x50)}, 3u);
    same(run(r, "setdef [temp=*]"), lines(setdef_lines({"M:", "Default"})));
    CHECK_EQ(unsigned{r.scbv(0x50)}, 0u);
    same(run(r, "setdef [order=(com)]"), lines(setdef_lines({"M:", "Default"})));
    same(run(r, "setdef [order=com]"), lines(setdef_lines({"M:", "Default"})));
    CHECK(succeeded(r));
    // DISPLAY: $18's bit 7, the rest kept; the CCP says where a program came from
    r.scb(0x18, 0x05);
    same(run(r, "setdef [display]"), lines(setdef_lines({"M:", "Default"}, "Default", true, true)));
    CHECK_EQ(unsigned{r.scbv(0x18)}, 0x85u);
    CHECK(r.cmd("args").find("args\r\nM:ARGS     COM\r\nARGS M0  \r\n") != std::string::npos);
    same(run(r, "setdef [no display]"), "M:SETDEF   COM\r\n" + lines(setdef_lines({"M:", "Default"}))); // still on as it ran
    CHECK_EQ(unsigned{r.scbv(0x18)}, 0x05u);
    CHECK(r.cmd("args").find("args\r\nARGS M0  \r\n") != std::string::npos);
    // PAGE: $2C 0 on, $FF off; the CCP's DIR pages by it
    r.scb(0x1C, 4); // a page of 4: 3 lines, then the stop
    same(run(r, "setdef [nopage]"), lines(setdef_lines({"M:", "Default"}, "Default", false)));
    CHECK_EQ(unsigned{r.scbv(0x2C)}, 0xFFu);
    r.scb(0x1A, 14); // DIR: one name a line
    same(run(r, "dir m:*.txt"), lines({"M: F0       TXT", "M: F1       TXT", "M: F2       TXT", "M: F3       TXT",
                                       "M: F4       TXT", "M: F5       TXT"}));
    r.scb(0x1A, 79);
    // [PAGE]: on, and SETDEF's own lines page by it
    size_t from = r.out.size();
    r.type("setdef [page]\r");
    REQUIRE(r.until("Press a key for more", ms(5000), from));
    r.type(" ");
    REQUIRE(r.until("Press a key for more", ms(5000), r.out.size()));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(5000), from));
    const std::string rub = "Press a key for more\r                    \r";
    same(r.out.substr(from), "setdef [page]\r\nDrive Search Path:\r\n1st Drive            - M:\r\n"
                             "2nd Drive            - Default\r\n" + rub + "\r\nSearch Order         - COM\r\n"
                             "Temporary Drive      - Default\r\n" + rub + "Console Page Mode    - On\r\n"
                             "Program Name Display - Off\r\nA>");
    CHECK_EQ(unsigned{r.scbv(0x2C)}, 0u);
    r.scb(0x1A, 14);
    from = r.out.size();
    r.type("dir m:*.txt\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    r.scb(0x1A, 79);
    r.scb(0x1C, 24);
    // Together, in one list
    same(run(r, "setdef m:,* [temporary=m:, order=(com), display, no page]"),
         lines(setdef_lines({"M:", "Default"}, "M:", false, true)));
    CHECK(chain_of(r) == std::vector<unsigned>({13, 0, 0xFF, 0xFF}));
    CHECK(r.scbv(0x50) == 13 && r.scbv(0x18) == 0x85 && r.scbv(0x2C) == 0xFF);
    CHECK(succeeded(r));
}

TEST(setdef_says_each_error_and_sets_nothing)
{
    SysRig r({"setdef"});
    REQUIRE(r.ok);
    same(run(r, "setdef [order=(sub,com)]"), "ORDER=(SUB): no SUBMIT yet");
    CHECK(!succeeded(r));
    same(run(r, "setdef [order=sub]"), "ORDER=(SUB): no SUBMIT yet");
    same(run(r, "setdef [order=(exe)]"), "Invalid type for ORDER option.");
    same(run(r, "setdef [order=(com,co)]"), "Invalid type for ORDER option.");
    same(run(r, "setdef [order=()]"), "Invalid type for ORDER option.");
    same(run(r, "setdef [order=(com]"), "Invalid type for ORDER option.");
    same(run(r, "setdef [temporary=q:]"), "Invalid drive.");
    same(run(r, "setdef [temporary=cc]"), "Invalid drive.");
    same(run(r, "setdef [temporary=(c:,d:)]"), "Invalid drive."); // one drive, no list
    same(run(r, "setdef [temporary=(m:)]"), "Invalid drive.");
    same(run(r, "setdef [temporary]"), "Invalid option.");
    same(run(r, "setdef [order]"), "Invalid option.");
    same(run(r, "setdef [foo]"), "Invalid option.");
    same(run(r, "setdef [no]"), "Invalid option."); // NODISPLAY or NOPAGE
    same(run(r, "setdef [page=on]"), "Invalid option.");
    same(run(r, "setdef [page"), "Invalid option.");
    same(run(r, "setdef [page] x"), "Invalid delimiter.");
    same(run(r, "setdef [page] [display]"), "Invalid delimiter.");
    same(run(r, "setdef m: [display,temporary=q:]"), "Invalid drive.");
    CHECK(!succeeded(r));
    CHECK(chain_of(r) == std::vector<unsigned>({13, 0, 0xFF, 0xFF}));
    CHECK(r.scbv(0x50) == 0 && (r.scbv(0x18) & 0x80) == 0 && r.scbv(0x2C) == 0);
}

int main()
{
    return run_all();
}
