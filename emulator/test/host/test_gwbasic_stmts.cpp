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
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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

int main()
{
    return run_all();
}
