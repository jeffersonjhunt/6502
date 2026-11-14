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
 * - Step b: DEF FN and FN (each type, 0 to many parameters, a parameter named as a variable, nesting, recursion
 *   to Out of memory, in FOR and GOSUB, redefinition, the string space collected while parameters are bound, and
 *   the errors); and, by hand: DEF in direct mode, FN typed after RUN, DEF SEG (7c's), and USR on 6502 routines
 *   read into an integer array (PC-BASIC's USR runs 8086 code).
 * - Step c: RENUM, typed at the prompt with its program (NAME.typ) against PC-BASIC's transcript of the same lines;
 *   and, by hand from the source (PC-BASIC's transcript of AUTO and EDIT is its own interface's, not GW-BASIC's
 *   screen): AUTO with its numbers and "*", ended by ^C and by a number past 65529, and EDIT.
 * - Step d: PRINT USING, every field and its edge cases, against PC-BASIC (NAME.differ where it departs from the 1983
 *   source, each checked against a model of MATH1.ASM's and MATH2.ASM's routines on mbf.py's arithmetic); the
 *   longest field the source allows, with the program after FBUFF LISTed; and, by hand, LPRINT USING to LST:.
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

    /// A typed session (step c): GW-BASIC started, then each line (a CR added) typed when GW-BASIC waits for a line
    /// (its PINLIN at INLIN0), as oracle.sh gives NAME.typ's lines to PC-BASIC: the whole transcript, from the
    /// start's Ok to what it printed once it waits after the last line, as PC-BASIC writes it.
    std::string session(const std::vector<std::string> &in)
    {
        CcpRig r;
        CHECK(r.ok);
        const std::map<std::string, uint16_t> lab = labels();
        CHECK(lab.count("INLIN0") == 1);
        if (!r.ok || lab.count("INLIN0") != 1)
            return {};
        const uint16_t inlin0 = lab.at("INLIN0");
        bool waiting = false;
        r.watch = [&](uint16_t pc) {
            if (pc == inlin0)
                waiting = true;
        };
        start(r);
        const size_t from = r.out.size();
        for (size_t next = 0;; ++next)
        {
            const uint64_t end = r.cpu->cycles() + ms(20000);
            while (!waiting && r.cpu->cycles() < end)
                r.run(10'000);
            if (!waiting || next == in.size())
                break;
            waiting = false;
            r.type(in[next] + "\r");
        }
        r.watch = nullptr;
        return "Ok\r\n" + r.out.substr(from);
    }

    /// NAME.typ's lines.
    std::vector<std::string> typ(const std::string &name)
    {
        std::vector<std::string> in;
        std::string l;
        for (const char c : read_text(EMU_GWBASIC_PROGRAMS "/" + name + ".typ"))
            if (c == '\r')
            {
                in.push_back(l);
                l.clear();
            }
            else
                l += c;
        return in;
    }

    /// Each session against the oracle.
    void sessions(const std::vector<const char *> &names)
    {
        for (const char *name : names)
        {
            const std::string got = session(typ(name)), want = oracle(name);
            if (got != want)
                std::printf("  %s:\n    got  '%s'\n    want '%s'\n", name, vis(got).c_str(), vis(want).c_str());
            CHECK_EQ(got, want);
        }
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

// DEF FN and FN (GWEVAL.ASM DEF, DEFNC3, SCNLIS, FNDOER, ASGMOR, POPASG, LPSIZL, FINASG, FINVLS, NOCPRS; the resident
// PTRGET's NOARYS, PRMSRC, NOTFNS and GARBA2's ARYVAR): functions of each type, the result made the function's type
// and each argument its parameter's; 0, 1, 2 and 3 parameters; a parameter named as a variable, which keeps its
// value (fnshad: a variable set after the DEF too); nested calls, a function calling another, as an argument of
// another; in FOR and GOSUB; DEF FN again; a string function of a string function. fngc: strings collected while a
// string function's parameters are bound, in PARM1 and in an outer call's block on the BASIC stack, 400 times, each
// result checked. Then the errors: an undefined function, Type mismatch for the result and for an argument of
// either kind, recursion to Out of memory, too few or too many arguments, no "(", a parameter list without its
// comma (Syntax error, each the last of its run). Where PC-BASIC doesn't do what the source does (.differ): the
// body sees the variables, not its caller's parameters (fnscope); two parameters of one name (fndup); a body not
// ending the DEF's statement (fnsn4); PRMSIZ's 99 bytes of parameters (fnfc); a function whose result is its
// string parameter (fnproj: PC-BASIC gives the variable's).
TEST(def_fn_and_fn_print_what_pc_basic_prints_or_the_source_says)
{
    programs({"fn", "fnshad", "fngc", "fnproj", "fnscope", "fndup", "fnuf", "fntm", "fntm2", "fntm3", "fnom", "fnfc",
              "fnsn"});
    programs({"fnsn2"});
    programs({"fnsn3"});
    programs({"fnsn4"});
    programs({"fnsn5"});
}

// By hand, from the source (none of it can come from PC-BASIC's run of a file):
// - DEF FN typed is Illegal direct (ERRDIR); a function a program DEFined can be called from a typed line after it
//   ran, the program's text still there; a function never DEFined is Undefined user function;
// - DEF SEG, with or without a segment, is 7c's: Advanced Feature; DEF with anything else is Syntax error;
// - USR (USRFN, SCNUSR, DEFUSR) on 6502 routines put in an integer array by READ, at VARPTR's address, under the
//   port's convention (A VALTYP, X #FACLO, RDL/RDH a string's descriptor; the result in the FAC and VALTYP): one
//   doubles an integer through X (USR and USR0 alike), one gives a string's length from its descriptor, one gives
//   VALTYP; the argument's variable is left as it was. USR n never DEFined, or DEFined at 65535 or -1 ($FFFF), is
//   Illegal function call. (Every simple variable is made before the DEF USRs: a new one moves the arrays up.)
TEST(def_in_direct_mode_def_seg_and_usr_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "DEF FNA(X)=X*2"), std::string("Illegal direct\r\n"));
    CHECK_EQ(command(r, "PRINT FNA(1)"), std::string("Undefined user function\r\n"));
    enter(r, {"10 DEF FNA(X)=X*2", "20 DEF FNB$(A$)=A$+\"!\""});
    CHECK_EQ(command(r, "RUN"), std::string());
    CHECK_EQ(command(r, "PRINT FNA(21);FNB$(\"hi\")"), std::string(" 42 hi!\r\n"));
    CHECK_EQ(command(r, "DEF SEG"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "DEF SEG=4096"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "DEF X"), std::string("Syntax error\r\n"));

    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 X%=0:DIM A%(15):FOR I=0 TO 13:READ L,H:A%(I)=L+256*H+65536!*(H>127):NEXT",
              "20 DATA 22,0,54,1,96,0",                              // ASL 0,X / ROL 1,X / RTS
              "30 DATA 178,0,133,44,100,45,169,2,133,27,96,0",       // LDA (RDL) / STA FACLO / STZ FACLO+1 /
                                                                     // LDA #2 / STA VALTYP / RTS
              "40 DATA 149,0,116,1,169,2,133,27,96,0",               // STA 0,X / STZ 1,X / LDA #2 / STA VALTYP / RTS
              "50 DEF USR=VARPTR(A%(0)):DEF USR1=VARPTR(A%(3)):DEF USR9=VARPTR(A%(9))",
              "60 PRINT USR(21);USR0(-300);USR1(\"hello\");USR1(\"\");USR9(7);USR9(1.5);USR9(2#);USR9(\"abc\")",
              "70 X%=1000:PRINT USR0(X%);X%"});
    CHECK_EQ(command(r, "RUN"), std::string(" 42 -600  5  0  2  4  8  3 \r\n 2000  1000 \r\n"));
    CHECK_EQ(command(r, "PRINT USR2(1)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF USR3=65535:PRINT USR3(1)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF USR4=-1:PRINT USR4(1)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT USR(5)+USR0(USR(1))"), std::string(" 14 \r\n"));
}

// RENUM (GWMAIN.ASM RESEQ, EATCOM, RESNN, NXTRSL, NXTRSC, RESSD1, RESNX1, SCCALL; SCCLIN's way of SCNEX2, NTERRG,
// CHGPTR), typed sessions against PC-BASIC's transcripts of the same lines (NAME.typ). renum: a program with each
// kind of line reference the source's CRUNCH makes a line number (LINRES: GOTO, GOSUB, THEN, ELSE, ON ... GOTO and
// GOSUB lists, RESTORE, RESUME, RETURN, RUN, LIST, LLIST, DELETE, EDIT, AUTO, RENUM, and ERL's: "ERL=110" and
// "ERL<>110" change, "110=ERL" doesn't), "ON ERROR GOTO 0" left as it is, and a GOTO to a missing line ("Undefined
// line 999 in 160", left unchanged); RUN, then RENUM 1000,30 (the first two lines kept), RENUM, RENUM 100,,5, each
// LISTed and RUN again. renumerl: ON ERROR GOTO with "IF ERL=40 THEN RESUME 50". renumfc: a new number at or below
// a line kept, an increment of 0 (given, or after a comma), the last number past 65529 or past 65535, all Illegal
// function call with nothing changed; the last allowed, 65529; too much, or a letter (Syntax error); "." for new.
TEST(renum_prints_what_pc_basic_prints)
{
    sessions({"renum", "renumerl", "renumfc"});
}

// By hand, from the source (PC-BASIC's transcript of AUTO shows its own interface, not GW-BASIC's screen), AUTO
// (GWMAIN.ASM AUTO, SNGAUT, SNGAU1; MAIN's AUTO part, AUTELN, MAINBX's "*"; EDENT's AUTRES and its empty lines;
// SCNEDT.ASM EDTBRK's reset at ^C; BIMISC.ASM SCRTCH's at NEW). OO-1: GW-BASIC prints the number offered on the
// screen and its editor reads it back with the line; here it is function 10's preload (MAINA), shown at once as the
// line's start, with "*" for a line that exists:
// - AUTO alone: 10, 20, ...; ^C (taken when the line ends, as 7a's) ends it: "^C", a new line, Ok;
// - AUTO 15,5: an empty line at an existing line's "*" keeps that line, a line typed after the "*" replaces it;
// - from 65520 by 5: after 65525 the next would be 65530, so AUTO ends and the prompt is the plain one;
// - AUTO 100, takes the old increment, AUTO . the current line (DOT, the last entered: "*", it exists);
// - an empty line at a new number goes on to the next without an error, and stores nothing;
// - the number rubbed out and NEW typed: a direct line, and NEW ends AUTO (SCRTCH);
// - an increment of 0, and AUTO n, before any AUTO set one (Illegal function call); anything after it (Syntax error).
TEST(auto_offers_numbers_by_hand)
{
    const std::string got = session({"AUTO 100,", "AUTO", "PRINT 1", "PRINT 2", "\x03", "AUTO 15,5", "X=1", "", "\x03",
                                     "LIST", "AUTO 15,5", "", "PRINT 22", "\x03", "AUTO 65520,5", "A=1", "B=2",
                                     "AUTO 100,", "\x03", "AUTO .", "\x03", "AUTO 40", "", "", "\x03", "LIST",
                                     "AUTO 70", "\b\b\bNEW", "LIST", "AUTO 10,0", "AUTO 10,5X"});
    const std::string want = "Ok\r\n"
                             "AUTO 100,\r\nIllegal function call\r\nOk\r\n"
                             "AUTO\r\n10 PRINT 1\r\n20 PRINT 2\r\n30 ^C\r\r\nOk\r\n"
                             "AUTO 15,5\r\n15 X=1\r\n20*\r\n25 ^C\r\r\nOk\r\n"
                             "LIST\r\n10 PRINT 1\r\n15 X=1\r\n20 PRINT 2\r\nOk\r\n"
                             "AUTO 15,5\r\n15*\r\n20*PRINT 22\r\n25 ^C\r\r\nOk\r\n"
                             "AUTO 65520,5\r\n65520 A=1\r\n65525 B=2\r\n"
                             "AUTO 100,\r\n100 ^C\r\r\nOk\r\n"
                             "AUTO .\r\n65525*^C\r\r\nOk\r\n"
                             "AUTO 40\r\n40 \r\n50 \r\n60 ^C\r\r\nOk\r\n"
                             "LIST\r\n10 PRINT 1\r\n15 X=1\r\n20 PRINT 22\r\n65520 A=1\r\n65525 B=2\r\nOk\r\n"
                             "AUTO 70\r\n70 \b \b\b \b\b \bNEW\r\nOk\r\n"
                             "LIST\r\nOk\r\n"
                             "AUTO 10,0\r\nIllegal function call\r\nOk\r\n"
                             "AUTO 10,5X\r\nSyntax error\r\nOk\r\n";
    if (got != want)
        std::printf("  got  '%s'\n  want '%s'\n", vis(got).c_str(), vis(want).c_str());
    CHECK_EQ(got, want);
}

// By hand, from the source (SCNEDT.ASM EDIT, EREDIT; PC-BASIC's EDIT is its own screen's): EDIT n lists the line into
// the next line read, as ERREDT does after a Syntax error (OO-1: function 10's preload, as 7a's), and goes to MAIN
// with no Ok; an Enter keeps it, an edit (here a backspace and a digit) changes it. EDIT . is the current line
// (DOT, which EDIT sets). EDIT of a missing line, and EDIT alone (line 0), are Undefined line number; anything after
// the number is Illegal function call (the source's EFCERR). The source doesn't refuse EDIT in a program: the
// program ends there, and its line is offered.
TEST(edit_offers_its_line_by_hand)
{
    const std::string got = session({"10 PRINT 1", "20 PRINT 2", "EDIT 10", "", "EDIT 20", "\b5", "EDIT .", "",
                                     "EDIT 30", "EDIT 10X", "EDIT", "LIST", "30 EDIT 10", "RUN", "\b9", "LIST"});
    const std::string want = "Ok\r\n"
                             "10 PRINT 1\r\n20 PRINT 2\r\n"
                             "EDIT 10\r\n10 PRINT 1\r\n"
                             "EDIT 20\r\n20 PRINT 2\b \b5\r\n"
                             "EDIT .\r\n20 PRINT 5\r\n"
                             "EDIT 30\r\nUndefined line number\r\nOk\r\n"
                             "EDIT 10X\r\nIllegal function call\r\nOk\r\n"
                             "EDIT\r\nUndefined line number\r\nOk\r\n"
                             "LIST\r\n10 PRINT 1\r\n20 PRINT 5\r\nOk\r\n"
                             "30 EDIT 10\r\n"
                             "RUN\r\n 1 \r\n 5 \r\n10 PRINT 1\b \b9\r\n"
                             "LIST\r\n10 PRINT 9\r\n20 PRINT 5\r\n30 EDIT 10\r\nOk\r\n";
    if (got != want)
        std::printf("  got  '%s'\n  want '%s'\n", vis(got).c_str(), vis(want).c_str());
    CHECK_EQ(got, want);
}

// PRINT USING (BIPRTU.ASM PRINUS, with BISTRS.ASM LEFTUS; MATH2.ASM PUFOUT, $PUI, $PUIZ, $PUF, DPCOM and MATH1.ASM
// $PUFE, $FOTZ, $FOTZC, $PUFXE in P1). The string fields: "!", "&", "\ \" of each width, longer and shorter strings,
// null strings, "\" that is no field, "_" escapes, literal text before, between and after, a list longer than the
// format, ";" and "," between values and at the end, a "+" before a string field. The numeric fields, with
// integers (puint), singles (pusng), E format (puexp) and doubles (pudbl): "#", ".", leading "+", trailing "+" and
// "-", "**", "$$", "**$", ",", "^^^^" (and "^^^", "^^^^^"), each with positive, negative, zero, small and large
// numbers; rounding at the field's last digit (and into a digit more, in E format); "%" for each form too wide;
// 10^16 and more in free format after a "%"; the most digits (24). pumisc: the format reused, values left over or
// fields left over, "+" and "." and "$" and "*" that are no field's, and a format from a string variable and an
// expression. pugc: the format's characters moved by collections in the middle of its list. The errors: a null format, a format with no field (after printing it), 25 digits (Illegal function
// call); a number for a string field, a string for a number's, a number for the format (Type mismatch); no
// delimiter between values, "," for the format's ";" (Syntax error).
TEST(print_using_prints_what_pc_basic_prints)
{
    programs({"pustr", "puint", "pusng", "puexp", "pudbl", "pumisc", "pugc"});
    programs({"pufc0", "pufc1", "pufc2", "pufc3", "pufc4", "pufc5", "pufc6", "pufc7", "putm", "putm2", "putm3",
              "pusn"});
    programs({"pusn2"});
    programs({"pusn3"});
}

// FBUFF holds the longest field the source allows (review DD9): 24 places, two before the point with a comma and
// 22 after it with a trailing sign, of a double just under 10^16, which $PUF writes out to FBUFF+46 before $PUFXE
// finds it too wide ("%"). The program's first line, just past FBUFF in the resident core's RAM, is LISTed after.
TEST(print_using_s_longest_field_fits_fbuff)
{
    programs({"pulong"});
}

// LPRINT USING (GIO86.ASM LPRINT, then PRINUS as PRINT's), by hand (PC-BASIC's printer isn't the oracle's): to LST:,
// with no line's end after the ";", then PTRFIL back to the console (FINPRT) for the PRINT USING after it; READY's
// FINLPT ends LPT1's line (CR LF) at the prompt.
TEST(lprint_using_goes_to_lpt1)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    std::string lst;
    r.watch = [&](uint16_t pc) {
        const Registers g = r.cpu->regs();
        if (pc == 0x0203 && g.y == 5) // the BDOS's entry, L_WRITE
            lst += static_cast<char>(g.a);
    };
    const std::string con = command(r, "LPRINT USING \"##.#-!\";-1.25,\"x\";:PRINT USING \"&\";\"y\"");
    r.watch = nullptr;
    CHECK_EQ(vis(lst), vis(" 1.3-x\r\n"));
    CHECK_EQ(vis(con), vis("y\r\n"));
}

int main()
{
    return run_all();
}
