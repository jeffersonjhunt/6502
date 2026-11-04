/**
 * @file test_oodos_utils.cpp
 * @brief OODOS's utilities, phase 4b (hw/oodos.md, "The utilities, phase 4b"): their shared code.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), and
 * UTTEST (oodos/sys/test/uttest.s) run by GO with a tail that says what to
 * test, each line it writes compared with what util.inc and cmd.inc's
 * CM_EOL should give:
 *
 *   - the option list: none, one, a second, one not closed; prefixes, an
 *     exact name winning, ambiguous and unknown names, NO and a name as one
 *     word; values, lists, numbers, ON and OFF, drives, values passed over;
 *   - file specifications by 152, the option list passed over, the
 *     delimiters, one refused; a line read and taken as the tail;
 *   - numbers in fields, with commas, 16-bit decimal in, the 24-bit helpers;
 *   - dates across leap years and 2099/2100 (against oodos_rig.h's days()),
 *     weekdays, times, stamps;
 *   - the name compare and a password decoded; the directory of M: and a
 *     card's drive read by 17 with '?' (against the bytes behind OODOS's
 *     back), its label, SFCBs and DPB, the drive selected and put back;
 *   - CM_EOL's owed line, paging's stop at it, a ^C there; the return code;
 *   - the rig: put_m's files past 16 KB, put_util and the search chain.
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
    /// Lines joined as a utility writes them: separated by CR LF, the last not ended.
    std::string lines(const std::vector<std::string> &ls)
    {
        std::string s;
        for (size_t i = 0; i < ls.size(); ++i)
            s += (i ? "\r\n" : "") + ls[i];
        return s;
    }

    /// UTTEST by GO with `tail`: what it wrote between GO's line and the prompt.
    std::string ut(CcpRig &r, const std::string &tail, const std::string &prompt = "A>")
    {
        r.poke_com(read_bytes(SYS + "/uttest.com"));
        const std::string line = "GO " + tail;
        const std::string o = r.cmd(line, prompt);
        const std::string head = line + "\r\n", end = "\r\n" + prompt;
        if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
            o.compare(o.size() - end.size(), end.size(), end) != 0)
            return "<" + o + ">";
        return o.substr(head.size(), o.size() - head.size() - end.size());
    }

    /// CP/M 3's days back to a date, by days(): the oracle for UT_TODATE.
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

    /// A directory entry's name as CM_DIRNM writes it.
    std::string dirnm(const uint8_t *e)
    {
        std::string s;
        for (unsigned i = 1; i < 12; ++i)
        {
            if (i == 9)
                s += ' ';
            s += static_cast<char>(e[i] & 0x7F);
        }
        return s;
    }

    /// What UTTEST X writes for a drive whose directory is `d` (`n` entries).
    std::vector<std::string> dir_lines(const std::vector<uint8_t> &d, unsigned n, unsigned drive, const std::string &dpb)
    {
        std::vector<std::string> ls = {"DRV " + std::to_string(drive), "DPB " + dpb,
                                       std::string("SFCBS ") + (d[3 * 32] == 0x21 ? "1" : "0")};
        std::string label = "LABEL NONE";
        for (unsigned i = 0; i < n; ++i)
            if (d[i * 32] == 0x20)
            {
                const uint8_t *e = &d[i * 32];
                label = "LABEL " + std::string(e + 1, e + 12) + " " + hex(e[12], 2) + " [";
                for (unsigned j = 0; j < 8; ++j)
                    label += static_cast<char>(e[23 - j] ^ e[13]);
                label += "]";
                break;
            }
        ls.push_back(label);
        for (unsigned i = 0; i < n; ++i)
        {
            const uint8_t *e = &d[i * 32];
            if (e[0] == 0xE5)
                continue;
            std::string l = "ENT " + std::to_string(i) + " " + hex(e[0], 2) + " ";
            if (e[0] != 0x21)
                l += dirnm(e);
            if (e[0] >= 16 && e[0] < 32)
            {
                l += " [";
                for (unsigned j = 0; j < 8; ++j)
                    l += static_cast<char>(e[23 - j] ^ e[13]);
                l += "]";
            }
            ls.push_back(l);
        }
        ls.push_back("ENTRIES " + std::to_string(n) + " 0");
        return ls;
    }
} // namespace

// ---- the option list -------------------------------------------------------------------------

TEST(the_option_list_is_found_once_and_its_names_matched_by_prefix)
{
    CcpRig r;
    REQUIRE(r.ok);
    same(ut(r, "O"), "LIST 0");
    same(ut(r, "O X Y"), "LIST 0");
    same(ut(r, "O [FULL] [SIZE]"), "LIST 2"); // a second list
    same(ut(r, "O [FULL"), "LIST 3");         // not closed
    same(ut(r, "O X [FULL] Y"), lines({"LIST 1", "5 FULL", "END"}));
    same(ut(r, "O []"), lines({"LIST 1", "END"}));
    // Prefixes; an exact name wins over a longer one it starts (DIR, DIRS)
    same(ut(r, "O [DIR,DIRS DI D DR DA DISK]"),
         lines({"LIST 1", "0 DIR", "1 DIRS", "AMB DI", "AMB D", "3 DR", "4 DA", "2 DISK", "END"}));
    // NO, blanks and a name: one word; NO alone ambiguous (NOPAGE, NOSORT)
    same(ut(r, "o [no page, NO,NOS, NO  SORT,N,NOPAGE,PAGE,P]"),
         lines({"LIST 1", "6 NOPAGE", "AMB NO", "8 NOS", "8 NOSORT", "AMB N", "6 NOPAGE", "7 PAGE", "7 P", "END"}));
    // Unknown: no such name, longer than one, empty before '=', '(' or ')', over 16 characters
    same(ut(r, "O [X,DIRECTORY,FULLER,=5,(A),),LENGTH,ABCDEFGHIJKLMNOPQ]"),
         lines({"LIST 1", "UNK X", "UNK DIRECTORY", "UNK FULLER", "UNK ", "UNK ", "UNK ", "9 LENGTH", "UNK LONG",
                "END"}));
}

TEST(an_options_value_is_a_word_or_a_list_read_as_a_number_on_off_or_a_drive)
{
    CcpRig r;
    REQUIRE(r.ok);
    same(ut(r, "O [LENGTH=20,LENGTH = 70000,USER=(1,2,15),USER=]"),
         lines({"LIST 1", "9 LENGTH = W [20 #20 - -]", "9 LENGTH = W [70000 #OVR - -]",
                "10 USER = L [1 #1 - -] [2 #2 - -] [15 #15 - -]", "10 USER = W [ #- - -]", "END"}));
    same(ut(r, "O [DRIVE=C:,DRIVE=*,DRIVE=(A,B:,*,Q),DRIVE=65535]"),
         lines({"LIST 1", "3 DRIVE = W [C: #- - 3]", "3 DRIVE = W [* #- - 0]",
                "3 DRIVE = L [A #- - 1] [B: #- - 2] [* #- - 0] [Q #- - -]", "3 DRIVE = W [65535 #65535 - -]",
                "END"}));
    same(ut(r, "O [PAGE=ON PAGE=OFF,PAGE=MAYBE PAGE = ( ON OFF )]"),
         lines({"LIST 1", "7 PAGE = W [ON #- ON -]", "7 PAGE = W [OFF #- OFF -]", "7 PAGE = W [MAYBE #- - -]",
                "7 PAGE = L [ON #- ON -] [OFF #- OFF -]", "END"}));
    // A value not read (SIZE's) is passed over, a word or a list
    same(ut(r, "O [SIZE=(A,B),FULL,SIZE=5 DIR,SIZE = ( X , Y ) DATE]"),
         lines({"LIST 1", "11 SIZE", "5 FULL", "11 SIZE", "0 DIR", "11 SIZE", "4 DATE", "END"}));
    // Errors: a list not closed, an item over 16 characters, a character that isn't an item's
    same(ut(r, "O [USER=(1,2]"), lines({"LIST 1", "10 USER = L [1 #1 - -] [2 #2 - -]!", "END"}));
    same(ut(r, "O [LENGTH=12345678901234567,USER=(A=B),FULL]"),
         lines({"LIST 1", "9 LENGTH = W!", "10 USER = L [A #- - 1]!", "5 FULL", "END"}));
    // An unknown option's value is passed over too
    same(ut(r, "O [X=(1,2),Y=3,FULL]"), lines({"LIST 1", "UNK X", "UNK Y", "5 FULL", "END"}));
}

// ---- file specifications and a line read -----------------------------------------------------

TEST(file_specifications_are_parsed_by_152_past_the_option_list)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string t = "S B:ONE.TXT;PW1 [X] TWO.DAT,NEW=OLD";
    const std::string tail = " " + t; // as the CCP leaves it: its blank first
    auto at = [&](const std::string &s) { return std::to_string(tail.find(s)); };
    same(ut(r, t), lines({"S " + at("B:") + " " + at("[X]") + " - 02 ONE     TXT;PW1      {B:ONE.TXT} Z",
                          "S " + at("TWO") + " " + at(",") + " , 00 TWO     DAT;         {TWO.DAT} Z",
                          "S " + at("NEW") + " " + at("=") + " = 00 NEW        ;         {NEW} Z",
                          "S " + at("OLD") + " " + std::to_string(tail.size()) + " - 00 OLD        ;         {OLD} Z",
                          "NONE"}));
    // A wildcard, a drive alone; a password not shown in the specification as typed
    const std::string t2 = "S *.C A: X.Y;SECRET=Z";
    const std::string tail2 = " " + t2;
    auto at2 = [&](const std::string &s) { return std::to_string(tail2.find(s)); };
    same(ut(r, t2), lines({"S " + at2("*") + " " + at2(" A:") + " - 00 ????????C  ;         {*.C} Z",
                           "S " + at2("A:") + " " + at2(" X") + " - 01            ;         {A:} Z",
                           "S " + at2("X.Y") + " " + at2("=") + " = 00 X       Y  ;SECRET   {X.Y} Z",
                           "S " + at2("Z") + " " + std::to_string(tail2.size()) + " - 00 Z          ;         {Z} Z",
                           "NONE"}));
    // One 152 refuses: its index, the parse stopped there
    same(ut(r, "S A TOOLONGNAME.X Y"), lines({"S 3 4 - 00 A          ;         {A} Z", "ERR 5"}));
    same(ut(r, "S"), "NONE");
    same(ut(r, "S [ONLY OPTIONS]"), "NONE");
    // A delimiter with nothing after it but blanks and option lists: refused, at its specification
    same(ut(r, "S A,"), "ERR 3");
    same(ut(r, "S A B= [X] "), lines({"S 3 4 - 00 A          ;         {A} Z", "ERR 5"}));
    same(ut(r, "S A .[X"), "ERR 3");
    same(ut(r, "S A,[X] B"), lines({"S 3 4 , 00 A          ;         {A} Z", "S 9 10 - 00 B          ;         {B} Z", "NONE"}));
}

TEST(a_line_read_is_the_tail_in_capitals)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.poke_com(read_bytes(SYS + "/uttest.com"));
    REQUIRE(!r.to("GO R", "Line? ").empty());
    const std::string typed = "  b:one.txt;pw  two";
    std::string o = r.cmd(typed);
    // Function 10 echoes the CR only: the LF is owed, and written before the next output
    same(o, typed + "\r\nLEN 19 [  B:ONE.TXT;PW  TWO]\r\nS 2 14 - 02 ONE     TXT;PW       {B:ONE.TXT} Z\r\n"
                    "S 16 19 - 00 TWO        ;         {TWO} Z\r\nNONE\r\nA>");
    // An empty line; a 0 ends one, as the CCP's
    r.poke_com(read_bytes(SYS + "/uttest.com"));
    REQUIRE(!r.to("GO R", "Line? ").empty());
    same(r.cmd(""), "\r\nLEN 0 []\r\nNONE\r\nA>");
    r.poke_com(read_bytes(SYS + "/uttest.com"));
    REQUIRE(!r.to("GO R", "Line? ").empty());
    o = r.cmd(std::string("a\0b", 3));
    CHECK(o.find("\r\nLEN 1 [A]\r\nS 0 1 - 00 A          ;         {A} Z\r\nNONE\r\nA>") != std::string::npos);
}

// ---- numbers ---------------------------------------------------------------------------------

TEST(numbers_are_written_in_fields_with_commas_and_read_to_65535)
{
    CcpRig r;
    REQUIRE(r.ok);
    same(ut(r, "N"), lines({"|0|0|", "|     0|0|", "|7|7|", "|999|999|", "|1,000|1000|", "|1000|1000|",
                            "|    9,488|9488|", "|    65,536|65536|", "|123,456|123456|", "|1,234,567|1234567|",
                            "|16,777,215|16777215|", "|    16777215|16777215|", "|16777215|16777215|",
                            "D 0 Y1", "D 65535 Y5", "D OVR Y5", "D 123 Y3", "D - Y0", "D OVR Y8", "D 42 Y5",
                            "ADD 140000", "ADD 0 C1", "SHL 1", "SHL 8388608", "DIV8 0", "DIV8 1", "DIV8 1",
                            "DIV8 2", "DIV8 2", "DIV8 2097151", "DIV8 2097152", "CMP 1 1", "CMP 1 0", "CMP 0 0",
                            "DIV 10000 0", "DIV 551 23", "DROPPEDJOINED"}));
}

// ---- dates and times -------------------------------------------------------------------------

TEST(dates_go_from_days_and_back_across_leap_years_and_2100)
{
    CcpRig r;
    REQUIRE(r.ok);
    static const char *const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::vector<std::string> want;
    for (const unsigned n : {1u, 365u, 366u, days(1980, 2, 29), days(1980, 3, 1), days(2000, 2, 29),
                             days(2026, 10, 31), days(2099, 12, 31), days(2100, 2, 28), days(2100, 2, 29),
                             days(2100, 3, 1), days(2100, 12, 31), 65535u})
    {
        unsigned y, m, d;
        date_of(n, y, m, d);
        want.push_back(std::to_string(n) + " " + two(m) + "/" + two(d) + "/" + two(y) + " " + WD[(n - 1) % 7] + " " +
                       std::to_string(y) + "-" + std::to_string(m) + "-" + std::to_string(d) + " " +
                       std::to_string(n) + " " + (y % 4 == 0 ? "29" : "28"));
    }
    want.push_back("09:05 23:59:58");
    want.push_back("|              |10/30/26 12:34|"); // a stamp of 0 days: 14 blanks
    same(ut(r, "D"), lines(want));
    CHECK(want[0] == "1 01/01/78 Sun 1978-1-1 1 28");                 // day 1, a Sunday (the Guide)
    CHECK(want[9] == "44620 02/29/00 Mon 2100-2-29 44620 29");        // the DS3231's 2100
    CHECK(want[12].substr(0, 18) == "65535 06/04/57 Sun"); // the last day 16 bits hold
}

// ---- names, passwords, the directory ---------------------------------------------------------

TEST(names_are_matched_without_attributes_and_passwords_decoded)
{
    CcpRig r;
    REQUIRE(r.ok);
    same(ut(r, "M"), lines({"11011010", "[SECRET  ]"}));
}

TEST(the_directory_is_every_entry_by_17_with_its_label_sfcbs_and_dpb)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.put_m({{0, "ONE     TXT", bytes("ONE\r\n")}, {3, "TWO     DAT", bytes("2"), true, true}});
    r.xfcb_m(0, "ONE     TXT", 0x80, "SECRET  ");
    const std::vector<uint8_t> k = kept_pw("LABELPW ");
    r.poke(MDISK + 13, k); // the label's password
    const std::string o = ut(r, "X M");
    std::vector<uint8_t> d(256 * 32);
    for (uint32_t i = 0; i < d.size(); ++i)
        d[i] = r.phys(MDISK + i);
    std::vector<std::string> want = dir_lines(d, 256, 12, dpb_hex("oodos-ram"));
    want.push_back("DRV 0"); // the drive put back
    same(o, lines(want));
    CHECK(o.find("LABEL UNLABELED   B1 [LABELPW ]") != std::string::npos);
    CHECK(o.find("ENT 1 00 ONE      TXT\r\n") != std::string::npos);
    CHECK(o.find(" 10 ONE      TXT [SECRET  ]") != std::string::npos);
    // A card's drive, from the current drive; and one that isn't, the BDOS's error said (mode $FE)
    same(r.cmd("b:", "B>"), "b:\r\r\nB>");
    const std::string ob = ut(r, "X", "B>");
    CpmDisk b{r, 2};
    std::vector<std::string> wb = dir_lines(b.directory(), b.entries(), 1, dpb_hex("oodos-8m"));
    wb.push_back("DRV 1");
    same(ob, lines(wb));
    same(r.cmd("a:"), "a:\r\r\nA>");
    const std::string oh = ut(r, "X H");
    CHECK(oh.find("OODOS Error On H: Invalid Drive") != std::string::npos);
    CHECK(oh.find("SELECT FAILED") != std::string::npos);
}

// ---- CM_EOL, paging, the end -----------------------------------------------------------------

TEST(a_line_owed_is_counted_for_paging_and_a_utilitys_last_line_is_the_prompts)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.scb(0x1C, 4); // a page of 4: 3 lines, then the stop, at the next line's start
    r.poke_com(read_bytes(SYS + "/uttest.com"));
    size_t from = r.out.size();
    r.type("GO P 9\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.run(ms(200));
    same(r.out.substr(from), "GO P 9\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\nPress a key for more");
    r.type(" ");
    REQUIRE(r.until("LINE 6\r\nPress a key for more", ms(3000), from));
    r.type(" ");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    // The last line isn't followed by a stop: it is owed, then dropped, and the prompt's CR LF ends it
    same(r.out.substr(from), "GO P 9\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\nPress a key for more\r                    \r"
                             "LINE 4\r\nLINE 5\r\nLINE 6\r\nPress a key for more\r                    \r"
                             "LINE 7\r\nLINE 8\r\nLINE 9\r\nA>");
    same(r.cmd(":hello"), ":hello\r\n" + HELLO_SAYS + "\r\n\r\nA>"); // done: 0
    // ^C at the stop: CMD_BREAK, UT_BREAK's $FFFE (':' skips)
    r.poke_com(read_bytes(SYS + "/uttest.com"));
    from = r.out.size();
    r.type("GO P 9\r");
    REQUIRE(r.until("Press a key for more", ms(3000), from));
    r.type("\x03");
    REQUIRE(r.until("\r\nA>", ms(3000), from));
    same(r.out.substr(from), "GO P 9\r\nLINE 1\r\nLINE 2\r\nLINE 3\r\nPress a key for moreBROKE\r\nA>");
    same(r.cmd(":hello"), ":hello\r\r\nA>");
    // Page mode off: no stops; three lines on a page of 4 end before it would stop
    r.scb(0x2C, 1);
    same(ut(r, "P 5"), lines({"LINE 1", "LINE 2", "LINE 3", "LINE 4", "LINE 5"}));
    r.scb(0x2C, 0);
    same(ut(r, "P 3"), lines({"LINE 1", "LINE 2", "LINE 3"}));
    // UT_FAIL: $FF00
    same(ut(r, "F"), "FAILED");
    same(r.cmd(":hello"), ":hello\r\r\nA>");
}

// ---- the rig ---------------------------------------------------------------------------------

TEST(the_rig_puts_files_past_16_kb_and_utilities_found_by_the_search_chain)
{
    CcpRig r;
    REQUIRE(r.ok);
    // ARGS.COM made 140 records long, its image to the end: two extents in M:'s one entry
    std::vector<uint8_t> big = args("BIG ");
    const size_t image = 140 * RECORD - 16;
    REQUIRE(big.size() < 16 + image);
    const size_t was = big.size();
    big.resize(16 + image);
    for (size_t i = was; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 7 + 3);
    big[6] = static_cast<uint8_t>(image), big[7] = static_cast<uint8_t>(image >> 8);
    r.put_m({{0, "BIG     COM", big}});
    r.put_util("uttest", SYS); // (both before M: is logged in)
    const int e = r.m_entry(0, "BIG     COM");
    REQUIRE(e >= 0);
    CHECK_EQ(unsigned{r.phys(MDISK + static_cast<uint32_t>(e) * 32 + 12)}, 1u);  // EX
    CHECK_EQ(unsigned{r.phys(MDISK + static_cast<uint32_t>(e) * 32 + 15)}, 12u); // RC
    CHECK_EQ(field(r.cmd("m:big"), "ARGS"), std::string("BIG "));
    bool loaded = true; // the last record's bytes where the loader put them
    for (size_t i = big.size() - RECORD; i < big.size(); ++i)
        loaded = loaded && r.phys(static_cast<uint32_t>(0x0300 + i - 16)) == big[i];
    CHECK(loaded);
    // put_util: user 0's SYS file, found from user 5 along the chain (the default drive, then M:)
    same(r.cmd("uttest f"), "uttest f\r\nUTTEST?\r\nA>");
    r.chain({0, 13});
    same(r.cmd("5a:", "5A>"), "5a:\r\r\n5A>");
    same(r.cmd("uttest f", "5A>"), "uttest f\r\nFAILED\r\n5A>");
    same(r.cmd("0a:"), "0a:\r\r\nA>");
    same(r.cmd("dir m:"), "dir m:\r\nM: BIG      COM\r\nSYSTEM FILE(S) EXIST\r\nA>"); // UTTEST is SYS
}

int main()
{
    return run_all();
}
