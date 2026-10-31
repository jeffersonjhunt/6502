/**
 * @file test_oodos_utils_files.cpp
 * @brief OODOS's TYPE.COM, ERASE.COM and RENAME.COM (hw/oodos.md, "The utilities, phase 4b").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the
 * three utilities (oodos/utils/type.s, erase.s, rename.s, built by `make
 * -C oodos utils`) put on M: as user 0's SYS files, and run from the CCP
 * as a user would: by `M:NAME`, and by the search chain (the default
 * drive, then M:) when a built-in hands them a form it can't do. Each
 * one's output is compared line for line, with the built-in's for the
 * forms the built-in does too, and what it leaves on M: is read back
 * behind OODOS's back:
 *
 *   - TYPE: one file as the built-in (^Z, tabs, a .COM's magic, No File,
 *     user 0's SYS file, a password); [PAGE] and [NOPAGE] over the SCB's
 *     page mode, a ^C at a stop; a wildcard, each file's name on its own
 *     line; the prompt; its errors;
 *   - ERASE: one name and a wildcard as the built-in (asked about, No
 *     File, read-only, a password); [CONFIRM] asking about each file; the
 *     prompt; its errors;
 *   - RENAME: new=old as the built-in (the drive rule, No File, a name
 *     there already asked about, read-only, passwords); wildcards and the
 *     '?' places, Invalid wildcard, each rename shown; the two prompts;
 *     its errors;
 *   - the CCP's hand-offs to them, and "X.COM required" without them;
 *   - the return codes, by a conditional command after each.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string HELLO = "Hello from OODOS\r\nHello from OODOS\r\n\r\n";

    /// M:'s files: text in user 0 (one without a last LF, one with tabs, a SYS one), a .COM, a
    /// read-only one, one in user 3; then the three utilities.
    void files(CcpRig &r)
    {
        r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")},
                 {0, "TWO     TXT", bytes("TWO\r\n")},
                 {0, "NOLF    TXT", bytes("NOLF")},
                 {0, "THREE   DAT", bytes("3")},
                 {0, "TABS    TXT", bytes("A\tB\r\n")},
                 {3, "OTHER   TXT", bytes("IN USER 3\r\n")},
                 {0, "RO      DAT", bytes("R"), false, true},
                 {0, "FOUR    DAT", bytes("4")},
                 {0, "SYST    TXT", bytes("SYSTEM TEXT\r\n"), true},
                 {0, "STOP    TXT", bytes("BEFORE\r\n\x1A" "AFTER\r\n")},
                 {0, "PROG    COM", read_bytes(SYS + "/hello.com")}});
        r.put_util("type");
        r.put_util("erase");
        r.put_util("rename");
    }

    /// What the command wrote: the output less its echo, an LF the CCP owed (written before a
    /// program, or by the prompt's CR LF after a built-in that wrote nothing) and the prompt.
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

    /// The built-in's and the utility's line give what the other writes.
    void as_builtin(CcpRig &r, const std::string &builtin, const std::string &util, const std::string &want)
    {
        same(wrote(r.cmd(builtin), builtin), want);
        same(wrote(r.cmd(util), util), want);
    }

    /// The return code: 0 (the conditional runs) or a failure (it is skipped).
    void ok(CcpRig &r, const std::string &prompt = "A>") { same(r.cmd(":hello", prompt), ":hello\r\n" + HELLO + "\r\n" + prompt); }
    void failed(CcpRig &r, const std::string &prompt = "A>") { same(r.cmd(":hello", prompt), ":hello\r\r\n" + prompt); }

    bool on_m(CcpRig &r, unsigned user, const std::string &name83) { return r.m_entry(user, name83) >= 0; }
} // namespace

// ---- TYPE ------------------------------------------------------------------------------------

TEST(type_com_shows_one_file_as_the_built_in_does)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    as_builtin(r, "type m:one.txt", "m:type m:one.txt", "ONE\r\n");
    ok(r);
    as_builtin(r, "type m:tabs.txt", "m:type m:tabs.txt", "A       B\r\n");
    as_builtin(r, "type m:stop.txt", "m:type m:stop.txt", "BEFORE\r\n");
    as_builtin(r, "type m:prog.com", "m:type m:prog.com", "OOD");
    as_builtin(r, "type m:nolf.txt", "m:type m:nolf.txt", "NOLF");
    as_builtin(r, "type m:nosuch.txt", "m:type m:nosuch.txt", "No File");
    failed(r);
    same(r.cmd("m:type m:one.txt"), "m:type m:one.txt\r\nONE\r\n\r\nA>"); // its bytes as they are
    same(r.cmd("m:type m:nosuch.txt"), "m:type m:nosuch.txt\r\nNo File\r\nA>");
    // User 0's SYS file from another user, as Open finds it; another user's file not
    same(r.cmd("4m:", "4M>"), "4m:\r\r\n4M>");
    same(r.cmd("m:type syst.txt", "4M>"), "m:type syst.txt\r\nSYSTEM TEXT\r\n\r\n4M>");
    same(r.cmd("m:type other.txt", "4M>"), "m:type other.txt\r\nNo File\r\n4M>");
    same(r.cmd("3m:", "3M>"), "3m:\r\r\n3M>");
    same(r.cmd("m:type other.txt", "3M>"), "m:type other.txt\r\nIN USER 3\r\n\r\n3M>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

TEST(type_com_takes_a_password_after_the_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "SECOND  TXT", bytes("ALSO\r\n")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  ");
    r.xfcb_m(0, "SECOND  TXT", 0x80, "SESAME  ");
    r.put_util("type");
    std::string o = r.cmd("m:type m:secret.txt");
    CHECK(o.find("\r\nOODOS Error On M: Password Error\r\nBDOS Function = 15 File = SECRET  .TXT") != std::string::npos);
    CHECK(o.find("HIDDEN") == std::string::npos);
    failed(r);
    same(r.cmd("m:type m:secret.txt;sesame"), "m:type m:secret.txt;sesame\r\nHIDDEN\r\n\r\nA>");
    // A wildcard: the password given for each file
    same(r.cmd("m:type m:se*.txt;sesame"), "m:type m:se*.txt;sesame\r\nM:SECRET.TXT\r\nHIDDEN\r\nM:SECOND.TXT\r\nALSO\r\n\r\nA>");
    ok(r);
    // Without it: each said by the BDOS, the next name on a line of its own, the return code $FF00
    o = r.cmd("m:type m:se*.txt");
    same(o, "m:type m:se*.txt\r\nM:SECRET.TXT\r\n\r\nOODOS Error On M: Password Error\r\nBDOS Function = 15 File = SECRET  .TXT\r\n"
            "M:SECOND.TXT\r\n\r\nOODOS Error On M: Password Error\r\nBDOS Function = 15 File = SECOND  .TXT\r\n\r\nA>");
    failed(r);
}

TEST(type_com_pages_by_its_option_over_the_scbs_page_mode)
{
    CcpRig r;
    REQUIRE(r.ok);
    std::string text;
    for (unsigned i = 1; i <= 9; ++i)
        text += "LINE " + std::to_string(i) + "\r\n";
    r.put_m({{0, "TEXT    TXT", bytes(text)}});
    r.put_util("type");
    r.scb(0x1C, 4); // a page of 4: 3 lines, then the stop
    const std::string stop = "Press a key for more";
    const std::string rub = "\r                    \r";
    // Neither: the SCB's page mode (on), as the built-in
    size_t from = r.out.size();
    r.type("m:type m:text.txt\r");
    REQUIRE(r.until(stop, ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("LINE 6\r\n" + stop, ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("LINE 9\r\n" + stop, ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "m:type m:text.txt\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\n" + stop + rub +
                                 "LINE 4\r\nLINE 5\r\nLINE 6\r\n" + stop + rub + "LINE 7\r\nLINE 8\r\nLINE 9\r\n" + stop +
                                 rub + "\r\nA>");
    // [NOPAGE], [NO PAGE] and any prefix of it: no stops
    for (const std::string opt : {"[nopage]", "[no page]", "[ NO  PAGE ]", "[n]", "[NOP]"})
        same(r.cmd("m:type m:text.txt " + opt), "m:type m:text.txt " + opt + "\r\n" + text + "\r\nA>");
    // The page mode off: no stops; [PAGE] (or P) stops whatever the SCB says
    r.scb(0x2C, 0xFF);
    same(r.cmd("m:type m:text.txt"), "m:type m:text.txt\r\n" + text + "\r\nA>");
    for (const std::string opt : {"[page]", "[p]", "[nopage,page]"})
    {
        from = r.out.size();
        r.type("m:type m:text.txt " + opt + "\r");
        REQUIRE(r.until(stop, ms(3000), from));
        r.run(ms(200));
        same(r.out.substr(from), "m:type m:text.txt " + opt + "\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\n" + stop);
        r.type("\x03"); // ^C at the stop: stopped, the return code $FFFE
        REQUIRE(r.until("\r\nA>", ms(3000), from));
        same(r.out.substr(from), "m:type m:text.txt " + opt + "\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\n" + stop + "\r\nA>");
        failed(r);
    }
    // A page length under 2: no stops, [PAGE] or not (ours, as the built-ins)
    r.scb(0x1C, 1);
    same(r.cmd("m:type m:text.txt [page]"), "m:type m:text.txt [page]\r\n" + text + "\r\nA>");
    ok(r);
}

TEST(type_com_shows_each_file_of_a_wildcard_under_its_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // In the directory's order, the current user's (SYS files too); NOLF's last line ended for the
    // name after it; the ^Z of STOP.TXT ends it
    const std::string all = "M:ONE.TXT\r\nONE\r\nM:TWO.TXT\r\nTWO\r\nM:NOLF.TXT\r\nNOLF\r\nM:TABS.TXT\r\nA       B\r\n"
                            "M:SYST.TXT\r\nSYSTEM TEXT\r\nM:STOP.TXT\r\nBEFORE\r\n";
    same(r.cmd("m:type m:*.txt"), "m:type m:*.txt\r\n" + all + "\r\nA>");
    ok(r);
    same(r.cmd("m:type m:?o*.txt"), "m:type m:?o*.txt\r\nM:NOLF.TXT\r\nNOLF\r\nA>");
    same(r.cmd("m:type m:*.dat"), "m:type m:*.dat\r\nM:THREE.DAT\r\n3\r\nM:RO.DAT\r\nR\r\nM:FOUR.DAT\r\n4\r\nA>");
    same(r.cmd("m:type m:*.xyz"), "m:type m:*.xyz\r\nNo File\r\nA>");
    failed(r);
    // From the current drive: its letter on each name; user 3's alone
    same(r.cmd("3m:", "3M>"), "3m:\r\r\n3M>");
    same(r.cmd("m:type *.*", "3M>"), "m:type *.*\r\nM:OTHER.TXT\r\nIN USER 3\r\n\r\n3M>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    // Paged across the files: a name line counts
    r.scb(0x1C, 4);
    const size_t from = r.out.size();
    r.type("m:type m:*.txt\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.run(ms(200));
    same(r.out.substr(from), "m:type m:*.txt\r\nM:ONE.TXT\r\nONE\r\nM:TWO.TXT\r\nPress a key for more");
    r.type("\x03");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    failed(r);
}

TEST(type_com_asks_for_a_name_and_says_its_errors)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    same(r.to("m:type", "Enter filename: "), "m:type\r\nEnter filename: ");
    same(r.cmd("m:one.txt"), "m:one.txt\r\nONE\r\n\r\nA>");
    ok(r);
    same(r.to("m:type [nopage]", "Enter filename: "), "m:type [nopage]\r\nEnter filename: ");
    same(r.cmd("m:t*.txt"), "m:t*.txt\r\nM:TWO.TXT\r\nTWO\r\nM:TABS.TXT\r\nA       B\r\n\r\nA>");
    same(r.to("m:type", "Enter filename: "), "m:type\r\nEnter filename: ");
    same(r.cmd(""), "\r\r\nA>"); // an empty line ends it
    ok(r);
    same(r.to("m:type", "Enter filename: "), "m:type\r\nEnter filename: ");
    same(r.cmd("m:nosuch"), "m:nosuch\r\nNo File\r\nA>");
    same(r.to("m:type", "Enter filename: "), "m:type\r\nEnter filename: ");
    same(r.cmd("m:one.txt [x]"), "m:one.txt [x]\r\nTry 'PAGE' or 'NO PAGE'\r\nA>");
    // Its errors, before anything is typed
    for (const std::string tail : {" [x]", " m:one.txt [page=3]", " m:one.txt [page] [nopage]", " m:one.txt [page",
                                   " m:one.txt [pagex]"})
        same(r.cmd("m:type" + tail), "m:type" + tail + "\r\nTry 'PAGE' or 'NO PAGE'\r\nA>");
    failed(r);
    for (const std::string tail : {" toolongname.txt", " m:one.txt two.txt", " a=b", " q:x"})
        same(r.cmd("m:type" + tail), "m:type" + tail + "\r\nInvalid file specification.\r\nA>");
    failed(r);
    // A drive that isn't: the BDOS's error, said
    const std::string o = r.cmd("m:type h:x.txt");
    CHECK(o.find("OODOS Error On H: Invalid Drive") != std::string::npos);
    failed(r);
}

// ---- ERASE -----------------------------------------------------------------------------------

TEST(erase_com_deletes_as_the_built_in_does)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // A single name: deleted, nothing written (the CCP's LF before a program, then its prompt)
    same(r.cmd("m:erase m:one.txt"), "m:erase m:one.txt\r\n\r\nA>");
    CHECK(!on_m(r, 0, "ONE     TXT"));
    CHECK(on_m(r, 0, "TWO     TXT"));
    ok(r);
    as_builtin(r, "era m:one.txt", "m:erase m:one.txt", "No File");
    failed(r);
    // A wildcard asked about, as typed: N keeps them (the return code 0), Y erases them
    same(r.to("m:erase m:*.txt", "(Y/N)?"), "m:erase m:*.txt\r\nERASE M:*.TXT (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(on_m(r, 0, "TWO     TXT"));
    ok(r);
    same(r.to("m:erase m:t*.txt", "(Y/N)?"), "m:erase m:t*.txt\r\nERASE M:T*.TXT (Y/N)?");
    same(r.cmd(" y"), " y\r\r\nA>");
    CHECK(!on_m(r, 0, "TWO     TXT"));
    CHECK(!on_m(r, 0, "TABS    TXT"));
    CHECK(on_m(r, 0, "NOLF    TXT"));
    CHECK(on_m(r, 3, "OTHER   TXT")); // another user's
    ok(r);
    same(r.to("m:erase m:t*.txt", "(Y/N)?"), "m:erase m:t*.txt\r\nERASE M:T*.TXT (Y/N)?");
    same(r.cmd("Y"), "Y\r\nNo File\r\nA>");
    failed(r);
    // The password isn't shown in the question
    same(r.to("m:erase m:*.dat;pw", "(Y/N)?"), "m:erase m:*.dat;pw\r\nERASE M:*.DAT (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    // Read-only: the BDOS's File R/O, said; kept
    as_builtin(r, "era m:ro.dat", "m:erase m:ro.dat", "\r\nOODOS Error On M: File R/O\r\nBDOS Function = 19 File = RO      .DAT\r\n");
    CHECK(on_m(r, 0, "RO      DAT"));
    failed(r);
}

TEST(erase_com_takes_a_password_after_the_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "SECOND  TXT", bytes("ALSO\r\n")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  ");
    r.xfcb_m(0, "SECOND  TXT", 0x80, "SESAME  ");
    r.put_util("erase");
    std::string o = r.cmd("m:erase m:secret.txt;wrong");
    CHECK(o.find("OODOS Error On M: Password Error\r\nBDOS Function = 19") != std::string::npos);
    CHECK(on_m(r, 0, "SECRET  TXT"));
    failed(r);
    same(r.cmd("m:erase m:secret.txt;sesame"), "m:erase m:secret.txt;sesame\r\n\r\nA>");
    CHECK(!on_m(r, 0, "SECRET  TXT"));
    CHECK_EQ(r.m_entry(16, "SECRET  TXT"), -1); // its XFCB with it
    // CONFIRM: the password given for each
    same(r.to("m:erase m:se*.txt;sesame [confirm]", "(Y/N)? "),
         "m:erase m:se*.txt;sesame [confirm]\r\nERASE M:SECOND.TXT (Y/N)? ");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(!on_m(r, 0, "SECOND  TXT"));
    ok(r);
}

TEST(erase_com_confirms_each_file_with_its_option)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // Each matching file asked about in turn, in the directory's order; Y deletes it
    same(r.to("m:erase m:*.txt [confirm]", "(Y/N)? "), "m:erase m:*.txt [confirm]\r\nERASE M:ONE.TXT (Y/N)? ");
    same(r.to("y", "(Y/N)? "), "y\r\nERASE M:TWO.TXT (Y/N)? ");
    same(r.to("n", "(Y/N)? "), "n\r\nERASE M:NOLF.TXT (Y/N)? ");
    same(r.to("", "(Y/N)? "), "\r\nERASE M:TABS.TXT (Y/N)? ");
    same(r.to("Y", "(Y/N)? "), "Y\r\nERASE M:SYST.TXT (Y/N)? ");
    same(r.to("no", "(Y/N)? "), "no\r\nERASE M:STOP.TXT (Y/N)? ");
    same(r.cmd(" yes"), " yes\r\r\nA>");
    ok(r);
    CHECK(!on_m(r, 0, "ONE     TXT"));
    CHECK(on_m(r, 0, "TWO     TXT"));
    CHECK(on_m(r, 0, "NOLF    TXT"));
    CHECK(!on_m(r, 0, "TABS    TXT"));
    CHECK(on_m(r, 0, "SYST    TXT"));
    CHECK(!on_m(r, 0, "STOP    TXT"));
    // A single name is asked about too; C is CONFIRM
    same(r.to("m:erase m:two.txt [c]", "(Y/N)? "), "m:erase m:two.txt [c]\r\nERASE M:TWO.TXT (Y/N)? ");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(on_m(r, 0, "TWO     TXT"));
    same(r.cmd("m:erase m:*.xyz [confirm]"), "m:erase m:*.xyz [confirm]\r\nNo File\r\nA>");
    failed(r);
    // A read-only file: the BDOS's error said, the next question on a line of its own, $FF00
    same(r.to("m:erase m:*.dat [confirm]", "(Y/N)? "), "m:erase m:*.dat [confirm]\r\nERASE M:THREE.DAT (Y/N)? ");
    same(r.to("y", "(Y/N)? "), "y\r\nERASE M:RO.DAT (Y/N)? ");
    same(r.to("y", "(Y/N)? "),
         "y\r\r\nOODOS Error On M: File R/O\r\nBDOS Function = 19 File = RO      .DAT\r\nERASE M:FOUR.DAT (Y/N)? ");
    same(r.cmd("y"), "y\r\r\nA>");
    failed(r);
    CHECK(!on_m(r, 0, "THREE   DAT"));
    CHECK(on_m(r, 0, "RO      DAT"));
    CHECK(!on_m(r, 0, "FOUR    DAT"));
    // From the current drive and user: its letter in the question
    same(r.cmd("3m:", "3M>"), "3m:\r\r\n3M>");
    same(r.to("m:erase *.* [confirm]", "(Y/N)? "), "m:erase *.* [confirm]\r\nERASE M:OTHER.TXT (Y/N)? ");
    same(r.cmd("y", "3M>"), "y\r\r\n3M>");
    CHECK(!on_m(r, 3, "OTHER   TXT"));
}

TEST(erase_com_asks_for_a_name_and_says_its_errors)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    same(r.to("m:erase", "Enter filename: "), "m:erase\r\nEnter filename: ");
    same(r.cmd("m:one.txt"), "m:one.txt\r\r\nA>");
    CHECK(!on_m(r, 0, "ONE     TXT"));
    ok(r);
    same(r.to("m:erase [confirm]", "Enter filename: "), "m:erase [confirm]\r\nEnter filename: ");
    same(r.to("m:two.txt", "(Y/N)? "), "m:two.txt\r\nERASE M:TWO.TXT (Y/N)? ");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(!on_m(r, 0, "TWO     TXT"));
    same(r.to("m:erase", "Enter filename: "), "m:erase\r\nEnter filename: ");
    same(r.to("m:*.dat", "(Y/N)?"), "m:*.dat\r\nERASE M:*.DAT (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    same(r.to("m:erase", "Enter filename: "), "m:erase\r\nEnter filename: ");
    same(r.cmd(""), "\r\r\nA>");
    ok(r);
    for (const std::string tail : {" x [y]", " x [confirm=1]", " x [c] [c]", " x [c", " a b", " a=b", " [conf,x]"})
        same(r.cmd("m:erase" + tail), "m:erase" + tail + "\r\nMissing delimiter or Unrecognized Option\r\nA>");
    failed(r);
    same(r.cmd("m:erase toolongname.txt"), "m:erase toolongname.txt\r\nInvalid file specification.\r\nA>");
    failed(r);
    CHECK(on_m(r, 0, "THREE   DAT"));
}

// ---- RENAME ----------------------------------------------------------------------------------

TEST(rename_com_renames_new_equals_old_as_the_built_in_does)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    same(r.cmd("m:rename m:new.txt=one.txt"), "m:rename m:new.txt=one.txt\r\n\r\nA>"); // nothing written
    CHECK(!on_m(r, 0, "ONE     TXT"));
    CHECK(on_m(r, 0, "NEW     TXT"));
    ok(r);
    same(r.cmd("m:rename new2.txt = m:new.txt"), "m:rename new2.txt = m:new.txt\r\n\r\nA>"); // old's drive
    CHECK(on_m(r, 0, "NEW2    TXT"));
    as_builtin(r, "ren m:x.txt=nosuch.txt", "m:rename m:x.txt=nosuch.txt", "No File");
    failed(r);
    // New there already: asked; N renames nothing ($FF00)
    same(r.to("m:rename m:two.txt=new2.txt", "(Y/N)?"),
         "m:rename m:two.txt=new2.txt\r\nNot renamed: TWO.TXT file already exists, delete (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(on_m(r, 0, "NEW2    TXT"));
    failed(r);
    // Y: the old TWO.TXT deleted, NEW2.TXT renamed to it
    const int two = r.m_entry(0, "TWO     TXT"), new2 = r.m_entry(0, "NEW2    TXT");
    REQUIRE(two >= 0 && new2 >= 0);
    same(r.to("m:rename m:two.txt=new2.txt", "(Y/N)?"),
         "m:rename m:two.txt=new2.txt\r\nNot renamed: TWO.TXT file already exists, delete (Y/N)?");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "NEW2    TXT"), -1);
    CHECK_EQ(r.m_entry(0, "TWO     TXT"), new2);
    CHECK_EQ(r.phys(MDISK + static_cast<uint32_t>(two) * 32), 0xE5);
    ok(r);
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nONE\r\n\r\nA>");
    // Read-only: the BDOS's error, said
    as_builtin(r, "ren m:rw.dat=ro.dat", "m:rename m:rw.dat=ro.dat",
               "\r\nOODOS Error On M: File R/O\r\nBDOS Function = 23 File = RO      .DAT\r\n");
    CHECK(on_m(r, 0, "RO      DAT"));
    failed(r);
    // Another user's file isn't there
    same(r.cmd("m:rename m:x.txt=other.txt"), "m:rename m:x.txt=other.txt\r\nNo File\r\nA>");
}

TEST(rename_com_takes_old_and_news_passwords)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "SECOND  TXT", bytes("ALSO\r\n")},
             {0, "KEPT    TXT", bytes("KEPT\r\n")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  ");
    r.xfcb_m(0, "SECOND  TXT", 0x80, "SESAME  ");
    r.xfcb_m(0, "KEPT    TXT", 0x80, "KEPTPW  ");
    r.put_util("rename");
    std::string o = r.cmd("m:rename m:hid.txt=secret.txt");
    CHECK(o.find("OODOS Error On M: Password Error\r\nBDOS Function = 23") != std::string::npos);
    CHECK(on_m(r, 0, "SECRET  TXT"));
    failed(r);
    same(r.cmd("m:rename m:hid.txt=secret.txt;sesame"), "m:rename m:hid.txt=secret.txt;sesame\r\n\r\nA>");
    CHECK(on_m(r, 0, "HID     TXT"));
    CHECK(on_m(r, 16, "HID     TXT")); // its XFCB renamed with it
    // New there already and protected: its own password deletes it
    same(r.to("m:rename m:kept.txt;keptpw=hid.txt;sesame", "(Y/N)?"),
         "m:rename m:kept.txt;keptpw=hid.txt;sesame\r\nNot renamed: KEPT.TXT file already exists, delete (Y/N)?");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(!on_m(r, 0, "HID     TXT"));
    same(r.cmd("type m:kept.txt;sesame"), "type m:kept.txt;sesame\r\nHIDDEN\r\n\r\nA>");
    // A wildcard: old's password for each (KEPT.TXT is HID.TXT's entry, SECRET.TXT's before it)
    same(r.cmd("m:rename m:*.new=*.txt;sesame"), "m:rename m:*.new=*.txt;sesame\r\nKEPT.NEW=KEPT.TXT\r\nSECOND.NEW=SECOND.TXT\r\nA>");
    CHECK(on_m(r, 0, "SECOND  NEW"));
    CHECK(on_m(r, 0, "KEPT    NEW"));
    ok(r);
}

TEST(rename_com_renames_each_file_of_a_wildcard)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    r.put_m({{0, "TWO     NEW", bytes("OLD TWO\r\n")}});
    // The Guide's *.TXI=*.TEX: each matching file, the names read first; each done shown; one there
    // already asked about (N: not renamed, the return code $FF00)
    same(r.to("m:rename m:*.new=*.txt", "(Y/N)?"),
         "m:rename m:*.new=*.txt\r\nONE.NEW=ONE.TXT\r\nNot renamed: TWO.NEW file already exists, delete (Y/N)?");
    same(r.cmd("n"), "n\r\nNOLF.NEW=NOLF.TXT\r\nTABS.NEW=TABS.TXT\r\nSYST.NEW=SYST.TXT\r\nSTOP.NEW=STOP.TXT\r\nA>");
    failed(r);
    CHECK(on_m(r, 0, "ONE     NEW"));
    CHECK(on_m(r, 0, "TWO     TXT"));
    CHECK(on_m(r, 0, "SYST    NEW"));
    CHECK(on_m(r, 3, "OTHER   TXT")); // another user's
    CHECK_EQ((r.phys(MDISK + static_cast<uint32_t>(r.m_entry(0, "SYST    NEW")) * 32 + 10) & 0x80), 0x80); // SYS kept
    // '?'s in the same places: the old name's characters there
    same(r.cmd("m:rename m:a*.t*=n*.n*"), "m:rename m:a*.t*=n*.n*\r\nAOLF.TEW=NOLF.NEW\r\nA>");
    same(r.cmd("m:rename m:o?o.new=o?e.new"), "m:rename m:o?o.new=o?e.new\r\nONO.NEW=ONE.NEW\r\nA>");
    CHECK(on_m(r, 0, "ONO     NEW"));
    // Y to the one there: it is deleted, then the rename
    const int old_two = r.m_entry(0, "TWO     NEW");
    same(r.to("m:rename m:t?o.new=t?o.txt", "(Y/N)?"),
         "m:rename m:t?o.new=t?o.txt\r\nNot renamed: TWO.NEW file already exists, delete (Y/N)?");
    same(r.cmd("y"), "y\r\nTWO.NEW=TWO.TXT\r\nA>");
    ok(r);
    CHECK(!on_m(r, 0, "TWO     TXT"));
    CHECK(old_two >= 0 && r.phys(MDISK + static_cast<uint32_t>(old_two) * 32) == 0xE5);
    same(r.cmd("type m:two.new"), "type m:two.new\r\nTWO\r\n\r\nA>");
    // Nothing matched; the '?'s not in the same places (the Guide's A*.TEX=A.T*)
    same(r.cmd("m:rename m:*.q=*.xyz"), "m:rename m:*.q=*.xyz\r\nNo File\r\nA>");
    failed(r);
    for (const std::string tail : {" a*.tex=a.t*", " *.x=a.y", " a.y=*.x", " ab*.x=a*.x", " a*.x=?b*.x"})
        same(r.cmd("m:rename" + tail), "m:rename" + tail + "\r\nInvalid wildcard.\r\nA>");
    failed(r);
    // Through the CCP: REN with a wildcard hands it over, found by the search chain
    r.chain({0, 13});
    // (a BDOS error's CR LF ends the line owed before it)
    same(r.cmd("ren m:*.dt=*.dat"), "ren m:*.dt=*.dat\r\nTHREE.DT=THREE.DAT\r\nOODOS Error On M: File R/O\r\n"
                                    "BDOS Function = 23 File = RO      .DAT\r\nFOUR.DT=FOUR.DAT\r\nA>");
}

TEST(rename_com_asks_for_the_names_and_says_its_errors)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // The Guide's RENAME with no tail: the two names asked for, the rename shown
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.to("m:x.prn", "Enter Old Name: "), "m:x.prn\r\nEnter Old Name: ");
    same(r.cmd("one.txt"), "one.txt\r\nX.PRN=ONE.TXT\r\nA>");
    CHECK(on_m(r, 0, "X       PRN"));
    ok(r);
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.to("m:*.q", "Enter Old Name: "), "m:*.q\r\nEnter Old Name: ");
    same(r.cmd("*.dat"), "*.dat\r\nTHREE.Q=THREE.DAT\r\nOODOS Error On M: File R/O\r\nBDOS Function = 23 File = RO      .DAT\r\nFOUR.Q=FOUR.DAT\r\nA>");
    failed(r);
    // An empty line ends it
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.cmd(""), "\r\r\nA>");
    ok(r);
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.to("m:y.txt", "Enter Old Name: "), "m:y.txt\r\nEnter Old Name: ");
    same(r.cmd(""), "\r\r\nA>");
    ok(r);
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.cmd("a=b"), "a=b\r\nIncorrect file specification.\r\nA>");
    failed(r);
    same(r.to("m:rename", "Enter New Name: "), "m:rename\r\nEnter New Name: ");
    same(r.to("m:y.txt", "Enter Old Name: "), "m:y.txt\r\nEnter Old Name: ");
    same(r.cmd("a b"), "a b\r\nIncorrect file specification.\r\nA>");
    // Its forms
    for (const std::string tail : {" x", " a b", " =x", " m:x=", " b:x=m:y", " a=b=c", " a,b", " a=b [x]",
                                   " toolongname.x=y", " x=toolongname.y", " m:two.txt=two.txt", " *.txt=m:*.txt"})
        same(r.cmd("m:rename" + tail), "m:rename" + tail + "\r\nIncorrect file specification.\r\nA>");
    failed(r);
    CHECK(on_m(r, 0, "TWO     TXT"));
}

// ---- the CCP's hand-offs ---------------------------------------------------------------------

TEST(the_built_ins_hand_their_other_forms_to_the_utilities_on_the_search_chain)
{
    CcpRig r;
    REQUIRE(r.ok);
    files(r);
    // Not on the search chain (the default drive alone): "X.COM required"
    same(r.cmd("type m:*.txt"), "type m:*.txt\r\nTYPE.COM required\r\nA>");
    same(r.cmd("type"), "type\r\nTYPE.COM required\r\nA>");
    same(r.cmd("era"), "era\r\nERASE.COM required\r\nA>");
    same(r.cmd("era x [confirm]"), "era x [confirm]\r\nERASE.COM required\r\nA>");
    same(r.cmd("ren"), "ren\r\nRENAME.COM required\r\nA>");
    same(r.cmd("ren *.x=*.y"), "ren *.x=*.y\r\nRENAME.COM required\r\nA>");
    failed(r);
    // The default drive, then M:: found there, from any user (user 0's SYS files)
    r.chain({0, 13});
    same(r.cmd("type m:*.dat"), "type m:*.dat\r\nM:THREE.DAT\r\n3\r\nM:RO.DAT\r\nR\r\nM:FOUR.DAT\r\n4\r\nA>");
    ok(r);
    same(r.cmd("type m:one.txt [nopage]"), "type m:one.txt [nopage]\r\nONE\r\n\r\nA>");
    same(r.to("type", "Enter filename: "), "type\r\nEnter filename: ");
    same(r.cmd("m:two.txt"), "m:two.txt\r\nTWO\r\n\r\nA>");
    same(r.to("era", "Enter filename: "), "era\r\nEnter filename: ");
    same(r.cmd(""), "\r\r\nA>");
    same(r.to("era m:t*.txt [confirm]", "(Y/N)? "), "era m:t*.txt [confirm]\r\nERASE M:TWO.TXT (Y/N)? ");
    same(r.to("n", "(Y/N)? "), "n\r\nERASE M:TABS.TXT (Y/N)? ");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK(on_m(r, 0, "TWO     TXT"));
    CHECK(!on_m(r, 0, "TABS    TXT"));
    same(r.to("ren", "Enter New Name: "), "ren\r\nEnter New Name: ");
    same(r.to("m:uno.txt", "Enter Old Name: "), "m:uno.txt\r\nEnter Old Name: ");
    same(r.cmd("m:one.txt"), "m:one.txt\r\nUNO.TXT=ONE.TXT\r\nA>");
    same(r.cmd("ren m:*.x=*.y"), "ren m:*.x=*.y\r\nNo File\r\nA>");
    failed(r);
    same(r.cmd("5a:", "5A>"), "5a:\r\r\n5A>");
    same(r.cmd("type m:*.*", "5A>"), "type m:*.*\r\nNo File\r\n5A>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

int main()
{
    return run_all();
}
