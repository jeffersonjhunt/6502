/**
 * @file test_gwbasic_stmts.cpp
 * @brief GW-BASIC's other statements, phase 7b (hw/oodos.md, "The plan, phase 7": block P3), run from OODOS's CCP
 *        on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_core does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M:, M:GWBASIC is typed at the prompt, and then each program (emulator/test/host/gwbasic/NAME.bas) is typed
 * line by line and RUN, its output against PC-BASIC's for the same program (NAME.out, by
 * oodos/gwbasic/tools/oracle.sh; or NAME.differ where the 1983 source and PC-BASIC differ).
 *
 * - Step a1: ON ... GOTO and ON ... GOSUB (each index, past the last, a negative or too big one, rounding, a
 *   missing line, a list ending in a comma); ON ERROR GOTO with RESUME, RESUME NEXT, RESUME line and RESUME 0;
 *   ERR and ERL; an error in the handler; ON ERROR GOTO 0 in the handler and out of it; FOR and GOSUB entries
 *   kept across a trap; ERROR n; RESUME without error; No RESUME. And, by hand from the source (PC-BASIC's run of
 *   a file can't type at the prompt): errors in direct mode with ON ERROR set.
 * - Step a2: SWAP, ERASE (with the string space collected after it), CLEAR in each form, OPTION BASE, RANDOMIZE
 *   (with the seed typed: NAME.in), WRITE, STRING$, MID$ as a statement and VARPTR$; and, by hand, what can't come
 *   from PC-BASIC's run: WRITE #, RANDOMIZE TIMER, and CLEAR's n and m on the OO-1's memory map.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string BUILD = std::getenv("GWBASIC_BUILD") ? std::getenv("GWBASIC_BUILD") : EMU_GWBASIC_BUILD;

    std::vector<uint8_t> file(const std::string &name)
    {
        std::ifstream f(BUILD + "/" + name, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    /// The resident link's labels (gwbasic.lbl, ld65's -Ln): name to address.
    std::map<std::string, uint16_t> labels()
    {
        std::map<std::string, uint16_t> out;
        std::ifstream f(BUILD + "/gwbasic.lbl");
        std::string al, addr, name;
        while (f >> al >> addr >> name)
            if (al == "al" && name.size() > 1 && name[0] == '.')
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(addr, nullptr, 16));
        return out;
    }

    /// A CcpRig with GW-BASIC's two files on M:, and GW-BASIC started: what it printed up to its first Ok.
    std::string start(CcpRig &r)
    {
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        CHECK(!com.empty() && !ovl.empty());
        if (com.empty() || ovl.empty())
            return {};
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}});
        return r.to("m:gwbasic", "Ok\r\n");
    }

    /// Program lines typed, as a user types them: a numbered line prints nothing back but its echo.
    void enter(CcpRig &r, const std::vector<std::string> &lines)
    {
        for (const std::string &l : lines)
            r.type(l + "\r");
    }

    /// A command typed: what it printed before the next Ok, its echo (the line, CR, LF) and the Ok left out.
    std::string command(CcpRig &r, const std::string &line)
    {
        const std::string o = r.to(line, "Ok\r\n");
        const size_t ok = o.find("Ok\r\n", line.size());
        if (o.size() < line.size() + 2 + 4 || ok == std::string::npos)
            return "<" + o + ">";
        return o.substr(line.size() + 2, ok - line.size() - 2);
    }

    /// emulator/test/host/gwbasic/NAME.bas's lines.
    std::vector<std::string> bas(const std::string &name)
    {
        std::vector<std::string> out;
        std::string l;
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".bas"))
            if (c == '\n')
            {
                out.push_back(l);
                l.clear();
            }
            else if (c != '\r')
                l += c;
        if (!l.empty())
            out.push_back(l);
        return out;
    }

    /// PC-BASIC's output for NAME.bas (NAME.out), its "not typed" marks ($FF) left out; or NAME.differ's text
    /// (its first line "# why"), where the port keeps the 1983 source's behaviour.
    std::string oracle(const std::string &name)
    {
        std::ifstream differ(std::string(EMU_GWBASIC_PROGRAMS) + "/" + name + ".differ");
        if (differ)
        {
            std::string line, out;
            std::getline(differ, line);
            CHECK(line.rfind("# ", 0) == 0);
            while (std::getline(differ, line))
                out += line + "\r\n";
            return out;
        }
        std::string out;
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".out"))
            if (static_cast<unsigned char>(c) != 0xFF)
                out += c;
        return out;
    }

    /// NAME.bas typed (after NEW) and RUN, each of NAME.in's lines (each ended by a CR) typed when GW-BASIC waits
    /// for a line (its SINLIN at INLIN0, function 10's call), as test_gwbasic_core's run_typed: what it printed
    /// from RUN's echo to READY's Ok, both left out.
    std::string run_typed(CcpRig &r, const std::string &name, uint16_t inlin0)
    {
        std::vector<std::string> in;
        std::string l;
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".in"))
            if (c == '\r')
            {
                in.push_back(l);
                l.clear();
            }
            else
                l += c;
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, bas(name));
        bool waiting = false;
        r.watch = [&](uint16_t pc) {
            if (pc == inlin0)
                waiting = true;
        };
        const size_t from = r.out.size();
        r.type("RUN\r");
        for (size_t next = 0;;)
        {
            const uint64_t end = r.cpu->cycles() + ms(20000);
            while (!waiting && r.cpu->cycles() < end)
                r.run(10'000);
            if (!waiting || next == in.size())
                break;
            waiting = false;
            r.type(in[next++] + "\r");
        }
        r.watch = nullptr;
        const std::string o = r.out.substr(from);
        if (o.size() < 5 + 4 || o.compare(o.size() - 4, 4, "Ok\r\n") != 0)
            return "<" + o + ">";
        return o.substr(5, o.size() - 5 - 4);
    }

    /// Each program typed (after NEW) and RUN, against the oracle.
    void programs(const std::vector<const char *> &names)
    {
        CcpRig r;
        REQUIRE(r.ok);
        start(r);
        for (const char *name : names)
        {
            CHECK_EQ(command(r, "NEW"), std::string());
            enter(r, bas(name));
            const std::string got = command(r, "RUN"), want = oracle(name);
            if (got != want)
                std::printf("  %s:\n    got  '%s'\n    want '%s'\n", name, vis(got).c_str(), vis(want).c_str());
            CHECK_EQ(got, want);
        }
    }
} // namespace

// ON ... GOTO and ON ... GOSUB (GWMAIN.ASM ONGOTO, NTONGS, NTONG1, ISGOSU, LOOPON): index 0 and past the last
// fall through; 1 to the last; a non-integer index rounded; GOSUB's RETURN to the statement after; spaces in the
// list; a list ending in a comma, the index past it; index 255. Then the errors: a negative index and one over 255
// (GETBYT's Illegal function call), a missing line, the index at a trailing comma, neither GOTO nor GOSUB. (A
// Syntax error leaves its line offered to edit, so each of on_sn and on_sn2 is the last of its run.)
TEST(on_goto_and_on_gosub_print_what_pc_basic_prints)
{
    programs({"ongoto", "on_fcn", "on_fcb", "on_ul", "on_sn"});
    programs({"on_sn2"});
}

// ON ERROR GOTO, its trap (NTMDCN) and RESUME (GWMAIN.ASM RESUME, RESNXT, RES0, RESTXT, NOTBGL): RESUME runs the
// statement again (an error inside a P1 statement, PRINT, among them); RESUME NEXT mid-line, after ERROR, after a
// line's first statement, after one in IF's THEN; RESUME line; RESUME 0; ON ERROR GOTO another line inside the
// handler; an unknown code trapped; ERR and ERL in the handler, after RESUME and before any error; an error inside
// the handler printed; ON ERROR GOTO 0 inside the handler (the error printed: RESTRP's ERRESM) and outside it;
// RESUME NEXT three GOSUBs deep inside FOR loops, then the loops and RETURNs still right; END in the handler.
TEST(error_trapping_prints_what_pc_basic_prints)
{
    programs({"trapres", "trapnext", "trapresn", "trapinh", "trapoff", "trapoff2", "trapdeep", "traperl"});
}

// ERROR n (GWMAIN.ASM ERRORS): a known code and an unknown one (Unprintable error), 0 and 256 (Illegal function
// call); RESUME without error; the handler's end without RESUME (PRGEND's No RESUME); ON ERROR GOTO a missing line.
TEST(error_resume_and_their_errors_print_what_pc_basic_prints)
{
    programs({"errkn", "errun", "err0", "err256", "resnoerr", "noresume", "trapul"});
}

// By hand, from the 1983 source (a direct line can't come from PC-BASIC's run of a file). ERRMOR leaves AL zero for
// a direct line, so NTMDCN traps a direct line's error whenever ONELIN is set, whatever ONEFLG says; in a program
// line AL is the line number's two bytes ANDed, plus one, ANDed with ONEFLG:
// - ON ERROR GOTO typed with an error after it on the same line: the handler runs with ERL 65535, and RESUME NEXT
//   goes on in the direct line;
// - ON ERROR left set after a program's END: a direct error is trapped (Division by zero too, which $DIV0S traps
//   when ONELIN is set);
// - the handler STOPped (ONEFLG still set): a direct error is trapped all the same, ONEFLG decremented again ($FE);
//   then an error in line 20 is trapped too, its AL (20's bytes ANDed, plus one: 1) ANDed with $FE being zero.
TEST(errors_in_direct_mode_with_on_error_set_are_trapped)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 PRINT \"trapped\";ERR;ERL", "20 RESUME NEXT"});
    CHECK_EQ(command(r, "ON ERROR GOTO 10:ERROR 5:PRINT \"after\""), std::string("trapped 5  65535 \r\nafter\r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 ON ERROR GOTO 100", "20 END", "100 PRINT \"h\";ERR;ERL:RESUME NEXT"});
    CHECK_EQ(command(r, "RUN"), std::string());
    CHECK_EQ(command(r, "ERROR 9"), std::string("h 9  65535 \r\n"));
    CHECK_EQ(command(r, "X=1/0:PRINT \"x\""), std::string("h 11  65535 \r\nx\r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 ON ERROR GOTO 100", "20 ERROR 5", "30 END", "100 PRINT \"h\";ERR;ERL:STOP"});
    CHECK_EQ(command(r, "RUN"), std::string("h 5  20 \r\nBreak in 100\r\n"));
    CHECK_EQ(command(r, "ERROR 7"), std::string("h 7  65535 \r\nBreak in 100\r\n"));
    CHECK_EQ(command(r, "GOTO 20"), std::string("h 5  20 \r\nBreak in 100\r\n"));
    // NEW clears ON ERROR (CLEARC): a direct error is printed again.
    CHECK_EQ(command(r, "NEW"), std::string());
    CHECK_EQ(command(r, "ERROR 7"), std::string("Out of memory\r\n"));
}

// SWAP (BIMISC.ASM SWAP): integers, singles, doubles, strings (a null one too), array elements with simple
// variables, a variable with itself, subscripts that are formulas (one reaching P3's STRING$), a first variable
// made by SWAP, long names. Then the errors: two types (Type mismatch), a second variable SWAP makes (it moves the
// arrays: Illegal function call), no comma (Syntax error, the last of its run).
TEST(swap_prints_what_pc_basic_prints)
{
    programs({"swap", "swaptm", "swaptm2", "swapfc", "swapsn"});
}

// ERASE (BIMISC.ASM ERASE, LPBKNM, ERSLOP; BIPTRG.ASM ERSFIN, ARYEXT, PTRRNZ): arrays of each type removed, the first,
// a middle one, the last, two at once, one made by use; the arrays above moved down and still right; DIM again after
// ERASE; long names. gcerase: string arrays erased, then enough strings made to collect the string space several
// times, the kept arrays' strings checked (a wrong walk of the arrays after ERASE breaks them). Then the errors: an
// array not there, a simple variable (Illegal function call), a subscript after the name (Syntax error).
TEST(erase_prints_what_pc_basic_prints)
{
    programs({"erase", "gcerase", "erasefc", "erasefc2", "erasesn"});
}

// CLEAR (BIMISC.ASM CLEAR, CSKPCM, CLEARS, CLEART, GETMPM, CDFSTK): alone, with x, with m, with x and m; the
// variables, arrays and ON ERROR gone; the rest of the line run; a GOSUB and a FOR dropped (RETURN without GOSUB,
// NEXT without FOR); n or m 0 and x negative (Illegal function call); too much after m (Syntax error). Where PC-BASIC
// doesn't keep the source's checks (.differ): "CLEAR ," and "CLEAR ,," are the source's (no Syntax error); m 240 or
// less, m more than n, n under the program and its variables (Out of memory); and n past the TPA's top (Illegal
// function call: the OO-1's MAXMEM, where a PC's data segment would take 65000).
TEST(clear_prints_what_pc_basic_prints_or_the_source_says)
{
    programs({"clear", "clearcm", "clearcm2", "clearrg", "clearnf", "clearom", "clearom2", "clearom3", "clearfc",
              "clearfc2", "clearfc3", "clearfc4", "clearsn"});
}

// OPTION BASE (GWMAIN.ASM OPTION, OLDOPT): base 1 for DIM and for an array made by use (its 0 out of range); the
// same base again, even with arrays; CLEAR lets it be set again. Base 0 when it is 0 already doesn't count as set
// (the source's OLDOPT; PC-BASIC counts it: opt01.differ). The errors: another base once one was set, or once there
// is an array (Duplicate Definition); 2, "BAS", nothing (Syntax error, each the last of its run).
TEST(option_base_prints_what_pc_basic_prints)
{
    programs({"option", "opt01", "optbs", "optdd", "optdd2", "optsn"});
    programs({"optsn2"});
    programs({"optsn3"});
}

// RANDOMIZE (GWMAIN.ASM RANDOM, STRNDS): the seed's two bytes set and RND stepped once (so the same seed twice gives
// different numbers, the low byte left as it was), negative and fractional seeds, RND(0) after it. The source's FRCINT
// gives Overflow past 32767 and Type mismatch for a string (PC-BASIC takes the first and gives Illegal function call
// for the second: .differ). And RANDOMIZE alone (INPRG, INPRAG): the prompt, a seed typed, a line not a number
// asked for again (randin.in).
TEST(randomize_prints_what_pc_basic_prints)
{
    programs({"random", "randomov", "randomtm"});
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("INLIN0") == 1);
    const std::string got = run_typed(r, "randin", lab.at("INLIN0")), want = oracle("randin");
    if (got != want)
        std::printf("  randin:\n    got  '%s'\n    want '%s'\n", vis(got).c_str(), vis(want).c_str());
    CHECK_EQ(got, want);
}

// WRITE (FIVEO.ASM WRITE, WRTMLP, NXTWRV, WASEMI, WRTSTR, WRTFIN): numbers of each type without the positive sign's
// space, strings in quotes, null strings, "," and ";" alike, WRITE alone; a ";" at the end (then Missing operand, as
// the source's WASEMI evaluates on). writesn: no separator, Syntax error, after the value is out (PC-BASIC puts the
// comma out first: .differ).
TEST(write_prints_what_pc_basic_prints)
{
    programs({"write", "writemo", "writesn"});
}

// STRING$ (BISTRS.ASM STRNG$, STRSTR, CALSPA): of a code and of a string's first character, 0 and 255 long, rounded
// arguments, nested. The errors: 256 or -1 long, a code of 256 (Illegal function call), a null string (the source's
// ASC2 gives Illegal function call; PC-BASIC a null string: .differ), a string for the length (Type mismatch).
TEST(string_function_prints_what_pc_basic_prints)
{
    programs({"strng", "strfc", "strfc2", "strfc3", "strfc4", "strtm"});
}

// MID$ as a statement (BISTRS.ASM LHSMID, NCP1, NCPMID, BIGLEN, MID$LP): from the n-th character, m of them or as
// many as b$ has, never past a$'s end; m 0 and a null b$ change nothing; a literal in the program (and a DATA string,
// and a second variable naming the same literal) copied first, so the program's text stays as it was; an array
// element; a string in the string space; rounded n and m. The errors: n 0, n past the end, an undefined a$
// (Illegal function call); a number on either side (Type mismatch); no "=" and a space before "(" (Syntax error).
TEST(mid_statement_prints_what_pc_basic_prints)
{
    programs({"lhsmid", "lhsfc0", "lhsfc", "lhsfc2", "lhstm", "lhstm2", "lhssn"});
    programs({"lhssn2"});
}

// VARPTR$ (GWSTS.ASM VARPT2, VARRT2): its type byte for each type, of an array element too, its length 3, and its
// address bytes equal to VARPTR's (only what doesn't depend on where the variable is). An undefined variable is
// Illegal function call.
TEST(varptr_string_prints_what_pc_basic_prints)
{
    programs({"varptr", "varpfc"});
}

// By hand, from the source and 7a's state (none of it can come from PC-BASIC's run of a file):
// - WRITE # is a file's (FILGET, 7c): Advanced Feature, as PRINT # gives in 7a;
// - RANDOMIZE TIMER: TIMER isn't a reserved word in the 1983 source's crunch table (IBMRES.ASM's TTABX has TIME$
//   alone; TIMER is 7f's), so it is a variable, 0, and RANDOMIZE TIMER is RANDOMIZE 0 (each after CLEAR, which
//   starts RND's state again);
// - CLEAR ,n is the workspace's top (OO-1): FRE(0) with nothing in the workspace is n - TXTTAB - 4 (the program's
//   two zero bytes, VAL's zero at n - 1, MEMSIZ at n - 2), so CLEAR at the TPA's top (start-up's: its "Bytes free"
//   + TXTTAB + 4) gives the start-up's bytes free again; one past it is Illegal function call (MAXMEM);
// - CLEAR ,,m is the BASIC stack's size within its 512 bytes (OO-1): a recursive GOSUB runs out of memory sooner at
//   m 300 than at 512, and m 4,000 is the whole 512; and RUN (CLEARC) keeps the size.
TEST(write_file_randomize_timer_and_clear_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string banner = start(r);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("LOAD") == 1);
    CHECK_EQ(command(r, "WRITE #1,2"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "PRINT TIMER"), std::string(" 0 \r\n"));
    const std::string seeded = command(r, "CLEAR:RANDOMIZE 0:PRINT RND;RND");
    CHECK_EQ(command(r, "CLEAR:RANDOMIZE TIMER:PRINT RND;RND"), seeded);
    CHECK(seeded != command(r, "CLEAR:RANDOMIZE 1:PRINT RND;RND"));

    const size_t at = banner.find(" Bytes free");
    REQUIRE(at != std::string::npos);
    const size_t from = banner.find_last_not_of("0123456789", at - 1) + 1;
    const long freed = std::stol(banner.substr(from, at - from));
    const long txttab = lab.at("LOAD") + 1, top = freed + txttab + 4;
    auto fre = [](long n) { return " " + std::to_string(n) + " \r\n"; };
    CHECK_EQ(command(r, "CLEAR ,40000:PRINT FRE(0)"), fre(40000 - txttab - 4));
    CHECK_EQ(command(r, "CLEAR ,39000:PRINT FRE(0)"), fre(39000 - txttab - 4));
    CHECK_EQ(command(r, "CLEAR ," + std::to_string(top + 1)), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "CLEAR ," + std::to_string(top) + ":PRINT FRE(0)"), fre(freed));

    enter(r, {"10 D=D+1:GOSUB 10"});
    auto depth = [&](const std::string &clear) {
        CHECK_EQ(command(r, clear), std::string());
        CHECK_EQ(command(r, "RUN"), std::string("Out of memory in 10\r\n"));
        const std::string d = command(r, "PRINT D");
        return std::stol(d);
    };
    const long d512 = depth("CLEAR ,,512"), d300 = depth("CLEAR ,,300"), d4000 = depth("CLEAR ,,4000");
    std::printf("  GOSUB depth: m 512 %ld, m 300 %ld, m 4000 %ld\n", d512, d300, d4000);
    CHECK(d300 > 0 && d300 < d512);
    CHECK_EQ(d4000, d512);
}

int main()
{
    return run_all();
}
