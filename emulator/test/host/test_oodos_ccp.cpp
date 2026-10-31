/**
 * @file test_oodos_ccp.cpp
 * @brief OODOS's CCP, phase 4a (hw/oodos.md, "The CCP, phase 4"), on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_sys boots it (oodos_rig.h), the committed
 * card's A.IMG as A: and B.IMG as B:, and command lines typed at the CCP
 * over the serial line, each one's output compared line for line with what
 * the CP/M 3 User's Guide has the command show (or, where it is silent,
 * what hw/oodos.md says is ours). Files the cases need are put on M:
 * behind OODOS's back before anything logs M: in: ARGS.COM
 * (oodos/sys/test/args.s, which prints its tail, its default FCBs, the
 * passwords' places, its drive, user and return code) under other names,
 * each copy tagged, and text files.
 *
 *   - the line: capitals, blanks, comments, its length and the tail's;
 *   - several commands with '!', conditional ones with ':', a ^C's end;
 *   - d:, nd:, USER (and its question);
 *   - DIR and DIRSYS: columns by the console width, SYS files, No File,
 *     the messages for the other kind, paging, ^C;
 *   - TYPE: to its ^Z, tabs, paging, ^C, passwords;
 *   - ERA: one name, wildcards asked about, read-only, passwords;
 *   - REN: new=old, No File, a name there already asked about;
 *   - the utilities the built-ins hand their options to, found or not;
 *   - programs: the drive, the user, user 0's SYS files, the search
 *     chain, .COM, .SUB and other types, a password, the display mode;
 *   - the tail and the default FCBs, the passwords' places and the load
 *     drive in the base page; the return code.
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
    constexpr uint32_t SCB_PHYS = SYS_BLOCK_PHYS + 0x200; ///< the SCB, CP/M 3's offsets
    constexpr unsigned M_BLS = 2048, M_DIRBLOCKS = 4;

    /// A file for M:'s directory. Its last record is ^Z-padded.
    struct MFile
    {
        unsigned user;
        std::string name83; ///< 8 + 3, blank-padded
        std::vector<uint8_t> data;
        bool sys = false, ro = false;
    };

    std::vector<uint8_t> bytes(const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); }

    /// ARGS.COM with its tag (the 4 bytes after "TAG:") made `tag`.
    std::vector<uint8_t> args(const std::string &tag)
    {
        std::vector<uint8_t> b = read_bytes(SYS + "/args.com");
        const std::string mark = "TAG:----";
        const auto at = std::search(b.begin(), b.end(), mark.begin(), mark.end());
        CHECK(at != b.end() && tag.size() == 4);
        if (at != b.end() && tag.size() == 4)
            std::copy(tag.begin(), tag.end(), at + 4);
        return b;
    }

    /// The password as the BDOS keeps it in an XFCB (bytes 13-15, 16-23): the key, 0, 0, then
    /// the 8 bytes reversed and XORed with the key, their sum (3b1).
    std::vector<uint8_t> kept_pw(const std::string &pw8)
    {
        uint8_t key = 0;
        for (const char c : pw8)
            key = static_cast<uint8_t>(key + static_cast<uint8_t>(c));
        std::vector<uint8_t> out = {key, 0, 0};
        for (unsigned j = 0; j < 8; ++j)
            out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(pw8[7 - j]) ^ key));
        return out;
    }

    /// The serial line with its CRs and LFs shown, for a failure.
    std::string vis(const std::string &s)
    {
        std::string t;
        for (const char c : s)
            t += c == '\r' ? std::string("\\r") : c == '\n' ? std::string("\\n") : std::string(1, c);
        return t;
    }

    /// OODOS.SYS booted with the card's A.IMG as A: and B.IMG (empty) as B:.
    struct Ccp : Rig
    {
        unsigned entry = 0, block = M_DIRBLOCKS; ///< M:'s last entry and next block put_m() used

        Ccp()
        {
            if (!ok)
                return;
            ok = mount(1, "/OODOS/A.IMG") == 0 && mount(2, "/OODOS/B.IMG") == 0;
            set_time(2026, 10, 30, 12, 0, 0);
            ok = ok && boot();
        }

        unsigned free_entry()
        {
            do
                ++entry;
            while (phys(MDISK + entry * 32) != 0xE5);
            return entry;
        }
        /// Files into M:'s directory and blocks, behind OODOS's back: before M: is logged in.
        void put_m(const std::vector<MFile> &files)
        {
            for (const MFile &f : files)
            {
                const unsigned at = free_entry();
                const unsigned records = static_cast<unsigned>((f.data.size() + RECORD - 1) / RECORD);
                REQUIRE(records >= 1 && records <= 128);
                std::vector<uint8_t> e(32, 0);
                e[0] = static_cast<uint8_t>(f.user);
                for (unsigned i = 0; i < 11; ++i)
                    e[1 + i] = static_cast<uint8_t>(f.name83[i]);
                e[9] = static_cast<uint8_t>(e[9] | (f.ro ? 0x80 : 0));
                e[10] = static_cast<uint8_t>(e[10] | (f.sys ? 0x80 : 0));
                e[15] = static_cast<uint8_t>(records);
                std::vector<uint8_t> data = f.data;
                data.resize(size_t{records} * RECORD, 0x1A);
                for (unsigned k = 0; k * M_BLS < data.size(); ++k)
                {
                    e[16 + k] = static_cast<uint8_t>(block);
                    for (size_t i = 0; i < M_BLS && k * M_BLS + i < data.size(); ++i)
                        poke(MDISK + block * M_BLS + static_cast<uint32_t>(i), data[k * M_BLS + i]);
                    ++block;
                }
                poke(MDISK + at * 32, e);
            }
        }
        /// An XFCB for a file of M:'s, its mode (bit 7 read, 6 write, 5 delete) and password,
        /// and M:'s label's passwords on.
        void xfcb_m(unsigned user, const std::string &name83, uint8_t mode, const std::string &pw8)
        {
            const unsigned at = free_entry();
            std::vector<uint8_t> e(32, 0);
            e[0] = static_cast<uint8_t>(user + 16);
            for (unsigned i = 0; i < 11; ++i)
                e[1 + i] = static_cast<uint8_t>(name83[i]);
            e[12] = mode;
            const std::vector<uint8_t> k = kept_pw(pw8);
            std::copy(k.begin(), k.end(), e.begin() + 13);
            poke(MDISK + at * 32, e);
            poke(MDISK + 12, static_cast<uint8_t>(phys(MDISK + 12) | 0x80));
        }
        /// M:'s entry of a name (user, 8 + 3, attributes ignored): its number, or -1.
        int m_entry(unsigned user, const std::string &name83)
        {
            for (unsigned i = 0; i < 256; ++i)
            {
                bool match = phys(MDISK + i * 32) == user;
                for (unsigned c = 0; c < 11 && match; ++c)
                    match = (phys(MDISK + i * 32 + 1 + c) & 0x7F) == static_cast<uint8_t>(name83[c]);
                if (match)
                    return static_cast<int>(i);
            }
            return -1;
        }
        void scb(unsigned offset, uint8_t v) { poke(SCB_PHYS + offset, v); }

        /// Type `line` and wait for a CR LF and `prompt` after its echo. @return all from the echo on
        std::string cmd(const std::string &line, const std::string &prompt = "A>")
        {
            return to(line, "\r\n" + prompt);
        }
        /// Type `line` and wait for `end` after its echo. @return all from the echo on
        std::string to(const std::string &line, const std::string &end, uint64_t budget = ms(8000))
        {
            const size_t from = out.size();
            type(line + "\r");
            if (!until(end, budget, from + line.size()))
            {
                std::printf("  '%s': never saw '%s' in '%s'\n", line.c_str(), vis(end).c_str(), vis(out.substr(from)).c_str());
                return {};
            }
            return out.substr(from);
        }
    };

    /// `got` is `want`, both shown if not.
    void same(const std::string &got, const std::string &want)
    {
        CHECK(got == want);
        if (got != want)
            std::printf("  got  '%s'\n  want '%s'\n", vis(got).c_str(), vis(want).c_str());
    }

    /// M:'s files for the DIR, TYPE, ERA and REN cases: six without SYS, one with, in user 0;
    /// one in user 3; a read-only one.
    void dir_files(Ccp &r)
    {
        r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")},
                 {0, "TWO     TXT", bytes("TWO\r\n")},
                 {0, "THREE   DAT", bytes("3")},
                 {0, "SYSF    COM", bytes("S"), true},
                 {0, "NOTYPE     ", bytes("N")},
                 {0, "SIX     TXT", bytes("SIX\r\n")},
                 {0, "SEVEN   TXT", bytes("SEVEN\r\n")},
                 {3, "THREE   TXT", bytes("IN USER 3\r\n")},
                 {0, "RO      DAT", bytes("R"), false, true}});
    }
} // namespace

// ---- the line --------------------------------------------------------------------------------

TEST(the_line_is_taken_in_capitals_and_blank_lines_and_comments_do_nothing)
{
    Ccp r;
    REQUIRE(r.ok);
    same(r.cmd(""), "\r\r\nA>"); // the prompt again, on the next line
    same(r.cmd("   "), "   \r\r\nA>");
    same(r.cmd("; a comment"), "; a comment\r\r\nA>");
    // The command and its tail in capitals (the Guide, 1.2): HELLO prints its tail back
    same(r.cmd("hello world"), "hello world\r\nHello from OODOS\r\nHello from OODOS\r\n WORLD\r\n\r\nA>");
    same(r.cmd("   hello"), "   hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>"); // blanks first
    same(r.cmd("nosuch"), "nosuch\r\nNOSUCH?\r\nA>");                                    // the Guide's DJR?
    same(r.cmd("nosuch tail"), "nosuch tail\r\nNOSUCH?\r\nA>");                          // the keyword alone
    same(r.cmd("dircopysys.com"), "dircopysys.com\r\nDIRCOPYSYS.COM?\r\nA>");            // and the Guide's
    // 255 characters a line (function 10's most), the rest a BEL each
    const std::string o = r.cmd(std::string(260, 'z'));
    CHECK(o.find("\r\n" + std::string(255, 'Z') + "?\r\nA>") != std::string::npos);
    CHECK(o.find(std::string(256, 'Z')) == std::string::npos);
    // A tail of 126 runs; of 127 doesn't
    CHECK(r.cmd("hello " + std::string(125, 'x')).find(" " + std::string(125, 'X') + "\r\n") != std::string::npos);
    const std::string tl = r.cmd("hello " + std::string(126, 'x')); // (the echo on two rows of the width)
    CHECK(tl.find("x\r\nCommand tail too long\r\nA>") != std::string::npos);
    CHECK(tl.find("Hello") == std::string::npos);
}

TEST(several_commands_run_in_turn_and_conditional_ones_only_after_a_success)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}});
    // Each after the first shown after a prompt, as it was typed (the Guide, 4.5)
    same(r.to("hello!hello", "\r\nA>hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>"),
         "hello!hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>");
    // NAME? is a failure, so ':' skips; a comment does nothing; then DIR
    same(r.to("nosuch!:hello! ;x!dir *.txt", "A>dir *.txt\r\nA: README   TXT\r\nA>"),
         "nosuch!:hello! ;x!dir *.txt\r\nNOSUCH?\r\nA>:hello\r\r\nA>;x\r\r\nA>dir *.txt\r\nA: README   TXT\r\nA>");
    // A success, so ':' runs
    same(r.to("dir *.txt!:hello", "A>:hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>"),
         "dir *.txt!:hello\r\nA: README   TXT\r\nA>:hello\r\nHello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>");
    // A program's failure ($FF00 by 108) is the next's condition; a skipped one keeps the code
    std::string o = r.to("m:args @f!:hello!:hello!m:args", "A>m:args\r\nARGS M0  \r\n");
    CHECK(o.find("RC 0000\r\n\r\nA>:hello\r\r\nA>:hello\r\r\nA>m:args\r\nARGS M0  \r\n") != std::string::npos);
    CHECK(r.until("RC 0000\r\n\r\nA>", ms(3000), r.out.rfind("ARGS M0")));
    CHECK(o.find("Hello") == std::string::npos);
    // A program ended by ^C ($FFFE): the rest of the line dropped
    o = r.cmd("m:args @c!hello");
    CHECK(o.find("ARGS M0") != std::string::npos);
    CHECK(o.find("A>hello") == std::string::npos);
    CHECK(o.find("Hello") == std::string::npos);
    // A built-in's failure is a failure too (ours), and its success a success
    same(r.to("dir nosuch!:hello", "A>:hello\r\r\nA>"), "dir nosuch!:hello\r\nNo File\r\nA>:hello\r\r\nA>");
    same(r.to("user 99!:user 3", "A>:user 3\r\r\nA>"), "user 99!:user 3\r\nUSER 99?\r\nA>:user 3\r\r\nA>");
    same(r.to("user 0!:user 3", "A>:user 3\r\r\n3A>"), "user 0!:user 3\r\r\nA>:user 3\r\r\n3A>");
}

// ---- d:, nd:, USER ---------------------------------------------------------------------------

TEST(the_drive_and_user_change_with_d_colon_nd_colon_and_user)
{
    Ccp r;
    REQUIRE(r.ok);
    same(r.cmd("b:", "B>"), "b:\r\r\nB>"); // the Guide, 2.4: A>B: then B>
    same(r.cmd("m:", "M>"), "m:\r\r\nM>");
    same(r.cmd("5a:", "5A>"), "5a:\r\r\n5A>"); // 2.5: the user, then the drive
    same(r.cmd("0b:", "B>"), "0b:\r\r\nB>");
    same(r.cmd("15a:", "15A>"), "15a:\r\r\n15A>");
    same(r.cmd("16b:", "15A>"), "16b:\r\n16B:?\r\n15A>"); // no user 16
    same(r.cmd("a: x", "15A>"), "a: x\r\nA: X?\r\n15A>"); // nothing after it
    same(r.cmd("q:", "15A>"), "q:\r\nQ:?\r\n15A>");       // no drive Q:
    // H: isn't mounted: 14's error, said by the BDOS; the drive stays
    same(r.cmd("h:", "15A>"), "h:\r\r\nOODOS Error On H: Invalid Drive\r\nBDOS Function = 14\r\n\r\n15A>");
    same(r.cmd("user 3", "3A>"), "user 3\r\r\n3A>"); // USER (5-116)
    same(r.cmd("use 12", "12A>"), "use 12\r\r\n12A>");
    same(r.cmd("user  0 ", "A>"), "user  0 \r\r\nA>");
    // Without a number it asks
    same(r.to("user", "Enter User #: "), "user\r\nEnter User #: ");
    same(r.cmd("5", "5A>"), "5\r\r\n5A>");
    same(r.to("user", "Enter User #: "), "user\r\nEnter User #: ");
    same(r.cmd("x", "5A>"), "x\r\nUSER?\r\n5A>");
    same(r.cmd("user 16", "5A>"), "user 16\r\nUSER 16?\r\n5A>");
    same(r.cmd("user x", "5A>"), "user x\r\nUSER X?\r\n5A>");
    same(r.cmd("user 1 2", "5A>"), "user 1 2\r\nUSER 1 2?\r\n5A>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
}

// ---- DIR and DIRSYS --------------------------------------------------------------------------

TEST(dir_lists_names_in_columns_and_says_what_else_exists)
{
    Ccp r;
    REQUIRE(r.ok);
    dir_files(r);
    // The Guide's layout: the drive, then NAME     TYP, " : " between, five at 80 columns
    same(r.cmd("dir m:"), "dir m:\r\nM: ONE      TXT : TWO      TXT : THREE    DAT : NOTYPE       : SIX      TXT\r\n"
                          "M: SEVEN    TXT : RO       DAT\r\nSYSTEM FILE(S) EXIST\r\nA>");
    same(r.cmd("dirsys m:"), "dirsys m:\r\nM: SYSF     COM\r\nNON-SYSTEM FILE(S) EXIST\r\nA>");
    same(r.cmd("dirs m:*.com"), "dirs m:*.com\r\nM: SYSF     COM\r\nA>");
    same(r.cmd("dir m:*.txt"), "dir m:*.txt\r\nM: ONE      TXT : TWO      TXT : SIX      TXT : SEVEN    TXT\r\nA>");
    same(r.cmd("dir m:notype"), "dir m:notype\r\nM: NOTYPE      \r\nA>"); // a blank type
    same(r.cmd("dir m:t*.*"), "dir m:t*.*\r\nM: TWO      TXT : THREE    DAT\r\nA>");
    same(r.cmd("dir m:sysf.com"), "dir m:sysf.com\r\nSYSTEM FILE(S) EXIST\r\nA>");
    same(r.cmd("dirs m:*.txt"), "dirs m:*.txt\r\nNON-SYSTEM FILE(S) EXIST\r\nA>");
    same(r.cmd("dir m:nosuch"), "dir m:nosuch\r\nNo File\r\nA>");
    same(r.cmd("dir b:"), "dir b:\r\nNo File\r\nA>");
    same(r.cmd("dirs b:"), "dirs b:\r\nNo File\r\nA>");
    same(r.cmd("m:", "M>"), "m:\r\r\nM>"); // the current drive
    same(r.cmd("dir *.dat", "M>"), "dir *.dat\r\nM: THREE    DAT : RO       DAT\r\nM>");
    same(r.cmd("3m:", "3M>"), "3m:\r\r\n3M>"); // the current user's
    same(r.cmd("dir", "3M>"), "dir\r\nM: THREE    TXT\r\n3M>");
    same(r.cmd("dirs", "3M>"), "dirs\r\nNON-SYSTEM FILE(S) EXIST\r\n3M>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    // The width (SCB $1A, the width less 1): 39 is two names a line, 14 one
    r.scb(0x1A, 39);
    same(r.cmd("dir m:*.txt"), "dir m:*.txt\r\nM: ONE      TXT : TWO      TXT\r\nM: SIX      TXT : SEVEN    TXT\r\nA>");
    r.scb(0x1A, 14);
    same(r.cmd("dir m:*.txt"), "dir m:*.txt\r\nM: ONE      TXT\r\nM: TWO      TXT\r\nM: SIX      TXT\r\nM: SEVEN    TXT\r\nA>");
    r.scb(0x1A, 79);
    // A drive that isn't: the BDOS's error, said
    const std::string o = r.cmd("dir h:");
    CHECK(o.find("OODOS Error On H: Invalid Drive\r\nBDOS Function = 17") != std::string::npos);
    CHECK(o.find("No File") == std::string::npos);
    same(r.cmd("dir a:toolongname"), "dir a:toolongname\r\nDIR A:TOOLONGNAME?\r\nA>"); // one 152 refuses
}

TEST(dir_and_type_stop_at_a_full_page_and_a_ctrl_c_stops_them)
{
    Ccp r;
    REQUIRE(r.ok);
    std::string text;
    for (unsigned i = 1; i <= 9; ++i)
        text += "LINE " + std::to_string(i) + "\r\n";
    std::string big;
    for (unsigned i = 0; i < 60; ++i)
        big += "A LONG LINE OF TEXT, NUMBER " + std::to_string(i) + "\r\n";
    std::vector<MFile> files = {{0, "ONE     TXT", bytes("1")}, {0, "TWO     TXT", bytes("2")},
                                {0, "THREE   TXT", bytes("3")}, {0, "FOUR    TXT", bytes("4")},
                                {0, "TEXT    TXT", bytes(text)}, {0, "BIG     TXT", bytes(big)}};
    for (unsigned i = 0; i < 40; ++i)
    {
        char n[12];
        std::snprintf(n, sizeof n, "F%02u     DAT", i);
        files.push_back({7, n, bytes("x")});
    }
    r.put_m(files);
    r.scb(0x1A, 14); // DIR: one name a line
    r.scb(0x1C, 4);  // a page of 4: 3 lines, then the stop
    size_t from = r.out.size();
    r.type("dir m:*.txt\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.run(ms(200));
    same(r.out.substr(from), "dir m:*.txt\r\nM: ONE      TXT\r\nM: TWO      TXT\r\nM: THREE    TXT\r\nPress a key for more");
    r.type("x");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "dir m:*.txt\r\nM: ONE      TXT\r\nM: TWO      TXT\r\nM: THREE    TXT\r\nPress a key for more"
                             "\r                    \rM: FOUR     TXT\r\nM: TEXT     TXT\r\nM: BIG      TXT\r\nA>");
    // TYPE, by its LFs (the width back: function 10 shows the line on rows of it)
    r.scb(0x1A, 79);
    from = r.out.size();
    r.type("type m:text.txt\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("LINE 6\r\nPress a key for more", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("LINE 9\r\nPress a key for more", ms(3000), from)); // a full page, though the last
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "type m:text.txt\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\nPress a key for more\r                    \r"
                             "LINE 4\r\nLINE 5\r\nLINE 6\r\nPress a key for more\r                    \r"
                             "LINE 7\r\nLINE 8\r\nLINE 9\r\nPress a key for more\r                    \r\r\nA>");
    // ^C at the stop: stopped, the rest of the line dropped, the return code $FFFE (':' skips)
    from = r.out.size();
    r.type("type m:text.txt!hello\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.type("\x03");
    if (!r.until("\r\nA>", ms(3000), from))
        r.tail();
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "type m:text.txt!hello\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\nPress a key for more\r\nA>");
    same(r.cmd(":hello"), ":hello\r\r\nA>");
    // The page mode off (SCB $2C not 0): no stops; nor with a page length under 2
    r.scb(0x2C, 1);
    same(r.cmd("type m:text.txt"), "type m:text.txt\r\n" + text + "\r\nA>");
    r.scb(0x1C, 1);
    r.scb(0x2C, 0);
    same(r.cmd("type m:text.txt"), "type m:text.txt\r\n" + text + "\r\nA>");
    // A ^C typed while TYPE writes: stopped there
    from = r.out.size();
    r.type("type m:big.txt\r");
    REQUIRE(r.until("NUMBER 3\r\n", ms(4000), from));
    r.type("\x03");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("NUMBER 59", from) == std::string::npos);
    same(r.cmd(":hello"), ":hello\r\r\nA>");
    // And while DIR lists
    r.scb(0x1C, 24);
    r.scb(0x1A, 14);
    same(r.cmd("7m:", "7M>"), "7m:\r\r\n7M>");
    from = r.out.size();
    r.type("dir\r");
    REQUIRE(r.until("F03", ms(4000), from));
    r.type("\x03");
    REQUIRE(r.until("\r\n7M>", ms(3000), from));
    CHECK(r.out.find("F39", from) == std::string::npos);
}

// ---- TYPE ------------------------------------------------------------------------------------

TEST(type_shows_a_file_to_its_ctrl_z_with_tabs_expanded)
{
    Ccp r;
    REQUIRE(r.ok);
    dir_files(r);
    r.put_m({{0, "TABS    TXT", bytes("A\tB\r\nAB\tC\r\n")},
             {0, "STOP    TXT", bytes("BEFORE\r\n\x1A" "AFTER\r\n")},
             {0, "PROG    COM", read_bytes(SYS + "/hello.com")},
             {0, "SYST    TXT", bytes("SYSTEM TEXT\r\n"), true}});
    same(r.cmd("type m:one.txt"), "type m:one.txt\r\nONE\r\n\r\nA>");
    same(r.cmd("typ m:tabs.txt"), "typ m:tabs.txt\r\nA       B\r\nAB      C\r\n\r\nA>");
    same(r.cmd("type m:stop.txt"), "type m:stop.txt\r\nBEFORE\r\n\r\nA>");
    same(r.cmd("type m:prog.com"), "type m:prog.com\r\nOOD\r\nA>"); // the magic's ^Z (ours)
    same(r.cmd("type m:nosuch.txt"), "type m:nosuch.txt\r\nNo File\r\nA>");
    same(r.cmd("type m:*.txt"), "type m:*.txt\r\nTYPE M:*.TXT?\r\nA>");
    same(r.cmd("type m:one.txt two.txt"), "type m:one.txt two.txt\r\nTYPE M:ONE.TXT TWO.TXT?\r\nA>");
    // User 0's SYS file from another user, as Open finds it (the Guide, 4.4.1)
    same(r.cmd("4m:", "4M>"), "4m:\r\r\n4M>");
    same(r.cmd("type syst.txt", "4M>"), "type syst.txt\r\nSYSTEM TEXT\r\n\r\n4M>");
    same(r.cmd("type one.txt", "4M>"), "type one.txt\r\nNo File\r\n4M>");
}

// ---- ERA -------------------------------------------------------------------------------------

TEST(era_deletes_a_name_and_asks_before_a_wildcard)
{
    Ccp r;
    REQUIRE(r.ok);
    dir_files(r);
    same(r.cmd("era m:one.txt"), "era m:one.txt\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "ONE     TXT"), -1);
    CHECK(r.m_entry(0, "TWO     TXT") >= 0);
    same(r.cmd("era m:one.txt"), "era m:one.txt\r\nNo File\r\nA>");
    // A wildcard asks (the Guide, 5-38): N keeps them, Y erases them
    same(r.to("era m:*.txt", "(Y/N)?"), "era m:*.txt\r\nERASE M:*.TXT (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(r.m_entry(0, "TWO     TXT") >= 0);
    same(r.to("erase m:*.txt", "(Y/N)?"), "erase m:*.txt\r\nERASE M:*.TXT (Y/N)?");
    same(r.cmd(" y"), " y\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "TWO     TXT"), -1);
    CHECK_EQ(r.m_entry(0, "SIX     TXT"), -1);
    CHECK_EQ(r.m_entry(0, "SEVEN   TXT"), -1);
    CHECK(r.m_entry(3, "THREE   TXT") >= 0); // another user's
    CHECK(r.m_entry(0, "THREE   DAT") >= 0);
    same(r.to("era m:*.txt", "(Y/N)?"), "era m:*.txt\r\nERASE M:*.TXT (Y/N)?");
    same(r.cmd("Y"), "Y\r\nNo File\r\nA>"); // nothing left to erase
    // Read-only: the BDOS's File R/O, said (error mode $FE); kept
    const std::string o = r.cmd("era m:ro.dat");
    CHECK(o.find("OODOS Error On M: File R/O\r\nBDOS Function = 19") != std::string::npos);
    CHECK(r.m_entry(0, "RO      DAT") >= 0);
    // The utility's forms, and a second name
    same(r.cmd("era"), "era\r\nERASE.COM required\r\nA>");
    same(r.cmd("era x [confirm]"), "era x [confirm]\r\nERASE.COM required\r\nA>");
    same(r.cmd("era a b"), "era a b\r\nERA A B?\r\nA>");
    // The password isn't shown in the question
    same(r.to("era m:*.dat;pw", "(Y/N)?"), "era m:*.dat;pw\r\nERASE M:*.DAT (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
}

TEST(era_type_and_a_program_take_a_password_after_the_name)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "SECRET  TXT", bytes("HIDDEN\r\n")}, {0, "SARGS   COM", args("PW  ")}});
    r.xfcb_m(0, "SECRET  TXT", 0x80, "SESAME  "); // read protected (so write and delete too)
    r.xfcb_m(0, "SARGS   COM", 0x80, "OPEN    ");
    std::string o = r.cmd("type m:secret.txt");
    CHECK(o.find("OODOS Error On M: Password Error") != std::string::npos);
    CHECK(o.find("HIDDEN") == std::string::npos);
    same(r.cmd("type m:secret.txt;sesame"), "type m:secret.txt;sesame\r\nHIDDEN\r\n\r\nA>");
    same(r.cmd("m:sargs"), "m:sargs\r\nPassword Error\r\nA>");
    CHECK_EQ(field(r.cmd("m:sargs;open x"), "ARGS"), std::string("PW  "));
    o = r.cmd("era m:secret.txt;wrong");
    CHECK(o.find("Password Error") != std::string::npos);
    CHECK(r.m_entry(0, "SECRET  TXT") >= 0);
    same(r.cmd("era m:secret.txt;sesame"), "era m:secret.txt;sesame\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "SECRET  TXT"), -1);
}

// ---- REN -------------------------------------------------------------------------------------

TEST(ren_renames_new_equals_old_and_asks_before_replacing)
{
    Ccp r;
    REQUIRE(r.ok);
    dir_files(r);
    same(r.cmd("ren m:new.txt=one.txt"), "ren m:new.txt=one.txt\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "ONE     TXT"), -1);
    CHECK(r.m_entry(0, "NEW     TXT") >= 0);
    same(r.cmd("rename new2.txt = m:new.txt"), "rename new2.txt = m:new.txt\r\r\nA>"); // old's drive
    CHECK(r.m_entry(0, "NEW2    TXT") >= 0);
    same(r.cmd("ren m:x.txt=nosuch.txt"), "ren m:x.txt=nosuch.txt\r\nNo File\r\nA>");
    // New there already: asked (the Guide, 5-81); N renames nothing
    same(r.to("ren m:two.txt=new2.txt", "(Y/N)?"),
         "ren m:two.txt=new2.txt\r\nNot renamed: TWO.TXT file already exists, delete (Y/N)?");
    same(r.cmd("n"), "n\r\r\nA>");
    CHECK(r.m_entry(0, "NEW2    TXT") >= 0);
    // Y: the old TWO.TXT deleted, NEW2.TXT renamed to it
    const int two = r.m_entry(0, "TWO     TXT"), new2 = r.m_entry(0, "NEW2    TXT");
    REQUIRE(two >= 0 && new2 >= 0);
    same(r.to("ren m:two.txt=new2.txt", "(Y/N)?"),
         "ren m:two.txt=new2.txt\r\nNot renamed: TWO.TXT file already exists, delete (Y/N)?");
    same(r.cmd("y"), "y\r\r\nA>");
    CHECK_EQ(r.m_entry(0, "NEW2    TXT"), -1);
    CHECK_EQ(r.m_entry(0, "TWO     TXT"), new2);
    CHECK_EQ(r.phys(MDISK + static_cast<uint32_t>(two) * 32), 0xE5);
    same(r.cmd("type m:two.txt"), "type m:two.txt\r\nONE\r\n\r\nA>");
    // Its forms
    same(r.cmd("ren b:x=m:y"), "ren b:x=m:y\r\nREN B:X=M:Y?\r\nA>"); // two drives
    same(r.cmd("ren x"), "ren x\r\nREN X?\r\nA>");
    same(r.cmd("ren a b"), "ren a b\r\nREN A B?\r\nA>");
    same(r.cmd("ren =x"), "ren =x\r\nREN =X?\r\nA>");
    same(r.cmd("ren m:x="), "ren m:x=\r\nREN M:X=?\r\nA>");
    same(r.cmd("ren"), "ren\r\nRENAME.COM required\r\nA>"); // the utility's forms
    same(r.cmd("ren *.x=*.y"), "ren *.x=*.y\r\nRENAME.COM required\r\nA>");
    // Read-only: the BDOS's error, said
    const std::string o = r.cmd("ren m:rw.dat=ro.dat");
    CHECK(o.find("OODOS Error On M: File R/O\r\nBDOS Function = 23") != std::string::npos);
    CHECK(r.m_entry(0, "RO      DAT") >= 0);
}

// ---- the utilities ---------------------------------------------------------------------------

TEST(options_go_to_the_built_ins_utilities_with_the_tail_as_given)
{
    Ccp r;
    REQUIRE(r.ok);
    same(r.cmd("dir [full]"), "dir [full]\r\nDIR.COM required\r\nA>");
    same(r.cmd("dir a b"), "dir a b\r\nDIR.COM required\r\nA>"); // two specifications
    same(r.cmd("dirs [size]"), "dirs [size]\r\nDIR.COM required\r\nA>");
    same(r.cmd("type"), "type\r\nTYPE.COM required\r\nA>");
    same(r.cmd("type x [nopage]"), "type x [nopage]\r\nTYPE.COM required\r\nA>");
    r.put_m({{0, "DIR     COM", args("DIR ")}, {0, "TYPE    COM", args("TYPE")},
             {0, "ERASE   COM", args("ERA ")}, {0, "RENAME  COM", args("REN ")}});
    same(r.cmd("m:", "M>"), "m:\r\r\nM>");
    std::string o = r.cmd("dir x [full]", "M>");
    CHECK_EQ(field(o, "ARGS"), std::string("DIR "));
    CHECK_EQ(field(o, "TAIL"), std::string("09 [ X [FULL]]"));
    o = r.cmd("dirsys x [full]", "M>"); // SYS added (ours)
    CHECK_EQ(field(o, "TAIL"), std::string("0D [ X [SYS,FULL]]"));
    o = r.cmd("dirs a b", "M>");
    CHECK_EQ(field(o, "ARGS"), std::string("DIR "));
    CHECK_EQ(field(o, "TAIL"), std::string("0A [ A B [SYS]]"));
    CHECK_EQ(field(r.cmd("type x [nopage]", "M>"), "ARGS"), std::string("TYPE"));
    CHECK_EQ(field(r.cmd("type", "M>"), "ARGS"), std::string("TYPE"));
    CHECK_EQ(field(r.cmd("era x [c]", "M>"), "ARGS"), std::string("ERA "));
    CHECK_EQ(field(r.cmd("erase", "M>"), "ARGS"), std::string("ERA "));
    CHECK_EQ(field(r.cmd("ren", "M>"), "ARGS"), std::string("REN "));
    o = r.cmd("rename *.new=*.old", "M>");
    CHECK_EQ(field(o, "ARGS"), std::string("REN "));
    CHECK_EQ(field(o, "TAIL"), std::string("0C [ *.NEW=*.OLD]"));
    // A drive before a built-in's name runs the program of that name (ours)
    CHECK_EQ(field(r.cmd("m:dir x", "M>"), "ARGS"), std::string("DIR "));
    same(r.cmd("a:dir", "M>"), "a:dir\r\nA:DIR?\r\nM>");
    same(r.cmd("dir", "M>"), "dir\r\nM: DIR      COM : TYPE     COM : ERASE    COM : RENAME   COM\r\nM>"); // the built-in
}

// ---- programs --------------------------------------------------------------------------------

TEST(a_program_is_found_on_its_drive_in_the_user_then_user_0s_sys_files)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}, {0, "SARGS   COM", args("MS0 "), true}, {5, "ARGS    COM", args("M5  ")}});
    CHECK_EQ(field(r.cmd("m:args"), "ARGS"), std::string("M0  "));     // d:NAME
    CHECK_EQ(field(r.cmd("m:args.com"), "ARGS"), std::string("M0  ")); // .COM given
    same(r.cmd("args"), "args\r\nARGS?\r\nA>");                          // not on A:
    same(r.cmd("m:", "M>"), "m:\r\r\nM>");
    CHECK_EQ(field(r.cmd("args", "M>"), "ARGS"), std::string("M0  ")); // the current drive
    same(r.cmd("5m:", "5M>"), "5m:\r\r\n5M>");
    CHECK_EQ(field(r.cmd("args", "5M>"), "ARGS"), std::string("M5  "));  // the user's own first
    CHECK_EQ(field(r.cmd("sargs", "5M>"), "ARGS"), std::string("MS0 ")); // then user 0's SYS file
    same(r.cmd("6m:", "6M>"), "6m:\r\r\n6M>");
    same(r.cmd("args", "6M>"), "args\r\nARGS?\r\n6M>"); // user 0's, not SYS
    const std::string o = r.cmd("sargs", "6M>");
    CHECK_EQ(field(o, "ARGS"), std::string("MS0 "));
    CHECK_EQ(field(o, "DU"), std::string("0C 06")); // it runs in user 6
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    // Types: .SUB is phase 5's (ours); any other refused
    same(r.cmd("m:args.sub"), "m:args.sub\r\nARGS.SUB: no SUBMIT yet\r\nA>");
    same(r.cmd("m:args.prl"), "m:args.prl\r\nM:ARGS.PRL?\r\nA>");
    same(r.cmd("m:arg?"), "m:arg?\r\nM:ARG??\r\nA>");
    same(r.cmd("m:a*"), "m:a*\r\nM:A*?\r\nA>");
    same(r.cmd("m:.com"), "m:.com\r\nM:.COM?\r\nA>");
    same(r.cmd("h:args"), "h:args\r\nREAD ERROR\r\nA>"); // no drive H: given: an error (ours)
    same(r.cmd("m:verylongname"), "m:verylongname\r\nM:VERYLONGNAME?\r\nA>");
    same(r.cmd("5b:args"), "5b:args\r\n5B:ARGS?\r\nA>"); // a user only with d: alone (ours)
    same(r.cmd("a=b"), "a=b\r\nA=B?\r\nA>");
}

TEST(a_program_without_a_drive_is_looked_for_along_the_search_chain)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}, {0, "HELLO   COM", args("MHEL")}});
    same(r.cmd("args"), "args\r\nARGS?\r\nA>");
    // The chain (SCB $4C-$4F: 0 the default drive, 1-16 A-P, $FF the end), as SETDEF sets it
    r.scb(0x4C, 0), r.scb(0x4D, 13), r.scb(0x4E, 0xFF);
    const std::string o = r.cmd("args");
    CHECK_EQ(field(o, "ARGS"), std::string("M0  "));
    CHECK_EQ(field(o, "BP"), std::string("0D000000000000")); // loaded from M:
    CHECK(r.cmd("hello").find("Hello from OODOS") != std::string::npos); // the default drive first
    r.scb(0x4C, 13), r.scb(0x4D, 0);
    CHECK_EQ(field(r.cmd("hello"), "ARGS"), std::string("MHEL")); // M: first now
    r.scb(0x4C, 8), r.scb(0x4D, 0x1F), r.scb(0x4E, 1), r.scb(0x4F, 13); // H: and 31 aren't drives
    CHECK(r.cmd("hello").find("Hello from OODOS") != std::string::npos);
    CHECK_EQ(field(r.cmd("args"), "ARGS"), std::string("M0  ")); // the fourth
    same(r.cmd("a:args"), "a:args\r\nA:ARGS?\r\nA>");             // a drive given: that one only
    r.scb(0x4C, 0), r.scb(0x4D, 0xFF);
    same(r.cmd("args"), "args\r\nARGS?\r\nA>");
}

TEST(the_display_mode_says_where_a_program_came_from)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}, {0, "SARGS   COM", args("MS0 "), true}});
    r.scb(0x18, 0x80); // SETDEF's [DISPLAY] (ours: SCB $18 bit 7)
    CHECK(r.cmd("m:args").find("m:args\r\nM:ARGS     COM\r\nARGS M0  \r\n") != std::string::npos);
    same(r.cmd("4m:", "4M>"), "4m:\r\r\n4M>");
    CHECK(r.cmd("sargs", "4M>").find("sargs\r\nM:SARGS    COM (User 0)\r\nARGS MS0 \r\n") != std::string::npos);
    same(r.cmd("0m:", "M>"), "0m:\r\r\nM>");
    CHECK(r.cmd("sargs", "M>").find("sargs\r\nM:SARGS    COM\r\nARGS MS0 \r\n") != std::string::npos);
    r.scb(0x18, 0);
    CHECK(r.cmd("args", "M>").find("args\r\nARGS M0  \r\n") != std::string::npos);
}

// ---- the tail and the base page ----------------------------------------------------------------

TEST(the_tail_and_the_default_fcbs_are_parsed_as_cp_m_3s_ccp_does)
{
    Ccp r;
    REQUIRE(r.ok);
    r.put_m({{0, "ARGS    COM", args("M0  ")}, {9, "ARGS    COM", args("M9  ")}});
    auto fcb = [](unsigned drive, const std::string &name83) {
        std::string s = hex(drive, 2);
        for (const char c : name83)
            s += hex(static_cast<unsigned char>(c), 2);
        return s + "00000000";
    };
    const std::string blank = fcb(0, "           ");
    // Two specifications with passwords: the FCBs, the passwords' places in $0280's copy
    std::string o = r.cmd("m:args b:one.txt;pw1 two.dat;xy");
    CHECK_EQ(field(o, "TAIL"), std::string("19 [ B:ONE.TXT;PW1 TWO.DAT;XY]"));
    CHECK_EQ(field(o, "F1"), fcb(2, "ONE     TXT"));
    CHECK_EQ(field(o, "F2"), fcb(0, "TWO     DAT") + "00000000");
    CHECK_EQ(field(o, "BP"), std::string("0D8C0203980202")); // M:; $028C, 3; $0298, 2
    CHECK_EQ(field(o, "PW1"), std::string("[PW1]"));
    CHECK_EQ(field(o, "PW2"), std::string("[XY]"));
    CHECK_EQ(field(o, "DU"), std::string("00 00"));
    CHECK_EQ(field(o, "RC"), std::string("0000"));
    // new=old: both, the delimiter between passed over
    o = r.cmd("m:args new=old");
    CHECK_EQ(field(o, "F1"), fcb(0, "NEW        "));
    CHECK_EQ(field(o, "F2"), fcb(0, "OLD        ") + "00000000");
    CHECK_EQ(field(o, "BP"), std::string("0D000000000000"));
    o = r.cmd("m:args a,b");
    CHECK_EQ(field(o, "F1"), fcb(0, "A          "));
    CHECK_EQ(field(o, "F2"), fcb(0, "B          ") + "00000000");
    o = r.cmd("m:args b:=a:*.c");
    CHECK_EQ(field(o, "F1"), fcb(2, "           "));
    CHECK_EQ(field(o, "F2"), fcb(1, "????????C  ") + "00000000");
    // An option list isn't a specification (ours)
    o = r.cmd("m:args [opt] z.txt");
    CHECK_EQ(field(o, "F1"), fcb(0, "Z       TXT"));
    CHECK_EQ(field(o, "F2"), blank + "00000000");
    // Nothing: blank; one 152 refuses: blank, and the parse stops
    o = r.cmd("m:args");
    CHECK_EQ(field(o, "TAIL"), std::string("00 []"));
    CHECK_EQ(field(o, "F1"), blank);
    CHECK_EQ(field(o, "F2"), blank + "00000000");
    o = r.cmd("m:args toolongname.x two");
    CHECK_EQ(field(o, "F1"), blank);
    CHECK_EQ(field(o, "F2"), blank + "00000000");
    // The return code starts at 0 whatever the last program left
    CHECK_EQ(field(r.cmd("m:args @f"), "RC"), std::string("0000"));
    CHECK_EQ(field(r.cmd("m:args"), "RC"), std::string("0000"));
    // The current drive and user are the CCP's; the load drive the one it came from
    same(r.cmd("9m:", "9M>"), "9m:\r\r\n9M>");
    o = r.cmd("args", "9M>");
    CHECK_EQ(field(o, "ARGS"), std::string("M9  "));
    CHECK_EQ(field(o, "DU"), std::string("0C 09"));
    CHECK_EQ(field(o, "BP"), std::string("0D000000000000"));
}

int main()
{
    return run_all();
}
