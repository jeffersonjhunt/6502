/**
 * @file test_gwbasic_core.cpp
 * @brief GW-BASIC's core, phase 7a (hw/oodos.md, "The plan, phase 7"), run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree (EMU_GWBASIC_BUILD, or
 * GWBASIC_BUILD to try another build). OODOS.SYS boots as test_oodos_ccp boots it (oodos_rig.h's CcpRig), both
 * files go on M: behind OODOS's back, and M:GWBASIC is typed at the prompt. Then lines are typed at GW-BASIC's
 * own prompt, on the serial line, as a user types them.
 *
 * - The loader: the blocks loaded, common memory copied into them, the far call sites patched; the banner, its
 *   bytes free against the plan's gate (32,768), and Ok; SYSTEM back to the CCP.
 * - A far call's crossing (V8): READY's FTSITE, a far call to P1's FARTST, which does nothing, timed from the
 *   JSR to the instruction after it, less a JSR and its RTS: at most 340 cycles.
 * - Programs typed and run, against PC-BASIC's output for the same program (emulator/test/host/gwbasic/NAME.bas
 *   and NAME.out, made by oodos/gwbasic/tools/oracle.sh): statements, TRON, STOP, END, the errors, LIST.
 * - What PC-BASIC's run of a file can't give, written by hand from GW-BASIC's own messages and LIST's format
 *   (each case says so): typing, replacing and deleting lines; LIST's ranges; RUN, RUN n and NEW; errors in
 *   direct mode; a Syntax error's line offered to edit; ^C (Break in n) and CONT; INKEY$; LLIST.
 * - Step I2's statements, the same way (FOR, NEXT, GOSUB, RETURN, WHILE, WEND, PRINT, READ, DATA, RESTORE, with
 *   their errors), the size spike's seven programs, and INPUT with its lines typed as GW-BASIC asks; INPUT;,
 *   LPRINT and the forms of later sub-phases by hand; and the timing figures: the core loop's cycles a pass, a
 *   PRINT statement's with its far call, and a character's through function 2.
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

    /// The resident core's labels (ld65's -Ln).
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

    /// A command typed: what it printed before the next Ok, its echo (the line, CR, LF) and the Ok left out
    /// (and anything after the Ok: a Syntax error's line, offered to edit).
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

    /// PC-BASIC's output for NAME.bas (NAME.out), its "not typed" marks ($FF, which GW-BASIC sends after a
    /// message and the OO-1's console doesn't show) left out. Where the 1983 source and PC-BASIC differ, the
    /// port keeps the source's behaviour and NAME.differ is what it must print instead, as oodos/gwbasic/test's
    /// programs.py has it: its first line "# why", then the text, line by line.
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
    /// for a line (its SINLIN at INLIN0, function 10's call); what it printed from RUN's echo to READY's Ok, both
    /// left out. GW-BASIC waiting with no line left is READY's MAIN: the end.
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

    /// A program typed (after NEW) and RUN, timed from the RUN statement to the end's ENDCON, with the far calls
    /// made between them counted (each a JSR to FARCALL, the address the loader put in at READY's FTSITE).
    struct Timed
    {
        uint64_t cycles = 0;
        unsigned farcalls = 0;
    };
    Timed timed(CcpRig &r, const std::map<std::string, uint16_t> &lab, const std::vector<std::string> &lines)
    {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, lines);
        const uint16_t runc = lab.at("RUN"), endcon = lab.at("ENDCON"), site = lab.at("FTSITE");
        const uint16_t farcall = static_cast<uint16_t>(r.machine->bus().peek(static_cast<uint16_t>(site + 1)) |
                                                       r.machine->bus().peek(static_cast<uint16_t>(site + 2)) << 8);
        Timed t;
        uint64_t from = 0;
        bool on = false;
        r.watch = [&](uint16_t pc) {
            if (pc == runc && !on)
            {
                from = r.cpu->cycles();
                on = true;
            }
            else if (on && pc == endcon)
            {
                t.cycles = r.cpu->cycles() - from;
                on = false;
            }
            else if (on && pc == farcall)
                ++t.farcalls;
        };
        command(r, "RUN");
        r.watch = nullptr;
        return t;
    }

    /// The banner's "N Bytes free".
    long bytes_free(const std::string &banner)
    {
        const size_t at = banner.find(" Bytes free");
        if (at == std::string::npos)
            return -1;
        size_t from = at;
        while (from > 0 && banner[from - 1] >= '0' && banner[from - 1] <= '9')
            --from;
        return std::strtol(banner.substr(from, at - from).c_str(), nullptr, 10);
    }
} // namespace

TEST(the_loader_loads_the_blocks_and_gw_basic_says_ok)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string got = start(r);
    CHECK(got.find("GW-BASIC") != std::string::npos);
    CHECK(got.find("Ok\r\n") != std::string::npos);
    const std::string back = r.cmd("system");
    CHECK(back.find("A>") != std::string::npos);
}

TEST(a_far_call_crosses_in_at_most_340_cycles)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("FTSITE") == 1);
    const uint16_t site = lab.at("FTSITE"), after = static_cast<uint16_t>(site + 6);
    uint64_t from = 0;
    std::vector<uint64_t> took;
    r.watch = [&](uint16_t pc) {
        if (pc == site)
            from = r.cpu->cycles();
        else if (pc == after && from)
        {
            took.push_back(r.cpu->cycles() - from);
            from = 0;
        }
    };
    start(r);
    for (int i = 0; i < 4; ++i) // each REM is READY again: another crossing (a blank line isn't: GW-BASIC
        r.to("REM", "Ok\r\n"); // reads another with no "Ok")
    r.watch = nullptr;
    REQUIRE(took.size() >= 5u);
    // The least of them: a crossing the 1 kHz tick (145 cycles) didn't land in.
    uint64_t least = took[0];
    for (const uint64_t t : took)
        least = t < least ? t : least;
    const uint64_t crossing = least - 12; // less a JSR (6) and its RTS (6): FARTST is an RTS
    std::printf("  a far call into P1 and back: %lu cycles over a JSR (the spike's FARCALL 281; OOUI's crossing 322)\n",
                static_cast<unsigned long>(crossing));
    CHECK(crossing <= 340u);
}

// The heading as GWINIT prints it (by hand: GW-BASIC's HEDING, with the OO-1's line before it), and the bytes
// free: MEMSIZ - TXTTAB - 2, at least the plan's 32,768 (hw/oodos.md, "The plan, phase 7": the gate).
TEST(the_banner_has_the_heading_and_at_least_32768_bytes_free)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string got = start(r);
    CHECK(got.find("\r\nGW-BASIC for the OO-1\r\n(C) Copyright Microsoft 1982         \r\n") != std::string::npos);
    const long n = bytes_free(got);
    std::printf("  the banner: %ld Bytes free (the gate 32,768)\n", n);
    CHECK(n >= 32768);
    CHECK(got.find(std::to_string(n) + " Bytes free\r\nOk\r\n") != std::string::npos);
}

// Each program typed, then RUN: what it prints must be PC-BASIC's output for it. (errsn, last, leaves its
// line offered to edit: a_syntax_error_offers_its_line_to_edit.)
TEST(programs_typed_and_run_print_what_pc_basic_prints)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    for (const char *name : {"run1", "tron", "stop", "end", "errul", "errtm", "list", "listr", "errsn"})
    {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, bas(name));
        const std::string got = command(r, "RUN"), want = oracle(name);
        if (got != want)
            std::printf("  %s:\n    got  '%s'\n    want '%s'\n", name, vis(got).c_str(), vis(want).c_str());
        CHECK_EQ(got, want);
    }
}

// By hand, from GW-BASIC's LIST (GWLIST.ASM): lines typed in any order are kept in order; one typed again
// replaces it; a number alone deletes its line; LIST n, n-m, -m and n- list those; DELETE takes a range out.
TEST(lines_are_put_in_order_replaced_deleted_and_listed)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"30 PRINT \"c\"", "10 print \"a\"", "20 PRINT \"b\"", "40 END", "25 x=1"});
    CHECK_EQ(command(r, "LIST"),
             std::string("10 PRINT \"a\"\r\n20 PRINT \"b\"\r\n25 X=1\r\n30 PRINT \"c\"\r\n40 END\r\n"));
    enter(r, {"20 PRINT \"B\";2", "25"});
    CHECK_EQ(command(r, "LIST"), std::string("10 PRINT \"a\"\r\n20 PRINT \"B\";2\r\n30 PRINT \"c\"\r\n40 END\r\n"));
    CHECK_EQ(command(r, "LIST 20"), std::string("20 PRINT \"B\";2\r\n"));
    CHECK_EQ(command(r, "LIST 20-30"), std::string("20 PRINT \"B\";2\r\n30 PRINT \"c\"\r\n"));
    CHECK_EQ(command(r, "LIST -20"), std::string("10 PRINT \"a\"\r\n20 PRINT \"B\";2\r\n"));
    CHECK_EQ(command(r, "LIST 30-"), std::string("30 PRINT \"c\"\r\n40 END\r\n"));
    CHECK_EQ(command(r, "LIST 15-25"), std::string("20 PRINT \"B\";2\r\n"));
    CHECK_EQ(command(r, "RUN"), std::string("a\r\nB 2 \r\nc\r\n"));
    enter(r, {"10"});
    CHECK_EQ(command(r, "LIST"), std::string("20 PRINT \"B\";2\r\n30 PRINT \"c\"\r\n40 END\r\n"));
    // DELETE (GWLIST.ASM): its "Ok" before the lines go, and none after.
    const std::string del = r.to("DELETE 20-30", "Ok\r\n");
    CHECK(del.find("Ok\r\n") != std::string::npos);
    CHECK_EQ(command(r, "LIST"), std::string("40 END\r\n"));
    CHECK_EQ(command(r, "DELETE 25"), std::string("Illegal function call\r\n"));
}

// By hand: RUN n starts at n with the variables cleared; END keeps its place for CONT; LLIST goes to LST:
// (OODOS's LPT takes and drops), so the console shows only Ok; NEW leaves no program.
TEST(run_n_cont_llist_and_new)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 A=5", "20 PRINT A", "30 END", "40 PRINT A+1"});
    CHECK_EQ(command(r, "RUN"), std::string(" 5 \r\n"));
    CHECK_EQ(command(r, "RUN 20"), std::string(" 0 \r\n"));
    CHECK_EQ(command(r, "CONT"), std::string(" 1 \r\n"));
    CHECK_EQ(command(r, "LLIST"), std::string());
    CHECK_EQ(command(r, "NEW"), std::string());
    CHECK_EQ(command(r, "LIST"), std::string());
    CHECK_EQ(command(r, "RUN"), std::string());
}

// By hand, GW-BASIC's messages (GWDATA.ASM): an error in a direct line has no " in"; a line that can't go in
// the program says why; CONT with nothing to continue; a later sub-phase's word says Advanced Feature.
TEST(errors_in_direct_mode)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "X"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "GOTO 100"), std::string("Undefined line number\r\n"));
    CHECK_EQ(command(r, "CONT"), std::string("Can't continue\r\n"));
    CHECK_EQ(command(r, "A$=1"), std::string("Type mismatch\r\n"));
    CHECK_EQ(command(r, "PRINT 1+"), std::string("Missing operand\r\n"));
    CHECK_EQ(command(r, "15"), std::string("Undefined line number\r\n"));
    CHECK_EQ(command(r, "X=&H12345"), std::string("Overflow\r\n"));
    CHECK_EQ(command(r, "RENUM"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "FILES"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "PRINT CVI(\"ab\")"), std::string("Advanced Feature\r\n"));
}

// By hand, as GW-BASIC's ERREDT gives it: after a Syntax error in a program line, that line is listed for the
// next line read, which may edit it (here a backspace takes the ")" out).
TEST(a_syntax_error_offers_its_line_to_edit)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, bas("errsn"));
    const size_t from = r.out.size();
    r.to("RUN", "Syntax error in 20\r\nOk\r\n");
    const size_t ok = r.out.find("Syntax error in 20\r\nOk\r\n", from);
    REQUIRE(ok != std::string::npos);
    CHECK(r.until("20 PRINT 1)", ms(2000), ok));
    r.type("\b\r");
    CHECK_EQ(command(r, "LIST 20"), std::string("20 PRINT 1\r\n"));
    CHECK_EQ(command(r, "RUN"), std::string("start\r\n 1 \r\nnot here\r\n"));
}

// By hand, as GIOKYB.ASM's ^C: "Break in" the line, and CONT goes on; a ^C in a line typed drops the line;
// INKEY$ gets a key typed while the program runs.
TEST(ctrl_c_breaks_and_cont_goes_on_and_inkey_reads_a_key)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 I=I+1", "20 GOTO 10"});
    size_t from = r.out.size();
    r.type("RUN\r");
    r.run(ms(200));
    r.type("\x03");
    CHECK(r.until("Ok\r\n", ms(2000), from));
    const std::string brk = r.out.substr(from);
    CHECK(brk.find("\r\nBreak in 10\r\nOk\r\n") != std::string::npos ||
          brk.find("\r\nBreak in 20\r\nOk\r\n") != std::string::npos);
    CHECK_EQ(command(r, "PRINT I>100"), std::string("-1 \r\n"));
    from = r.out.size();
    r.type("CONT\r");
    r.run(ms(100));
    r.type("\x03");
    CHECK(r.until("Ok\r\n", ms(2000), from));
    CHECK(r.out.find("Break in ", from) != std::string::npos);
    const std::string cc = r.to("PRINT 7\x03", "Ok\r\n");
    CHECK(cc.find(" 7 \r\n") == std::string::npos);
    enter(r, {"NEW", "10 A$=INKEY$:IF A$=\"\" THEN 10", "20 PRINT A$;ASC(A$)"});
    from = r.out.size();
    r.type("RUN\r");
    r.run(ms(100));
    r.type("x");
    CHECK(r.until("x 120 \r\nOk\r\n", ms(2000), from));
}

// Step I2's statements, each a program typed and RUN against PC-BASIC's output (or NAME.differ, where the
// 1983 source and PC-BASIC differ): FOR and NEXT, GOSUB and RETURN, WHILE and WEND, PRINT, READ, DATA and
// RESTORE, TAB and SPC; each statement's errors; then the size spike's seven programs (oodos/spike/gwbasic/:
// a loop, precedence, outputs GW-BASIC is known to print, and four of basic.py's random expressions), as text
// (oodos/gwbasic/test/check.py --write-programs). (e_dsn, last, leaves its DATA line offered to edit.)
TEST(statements_typed_and_run_print_what_pc_basic_prints)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    for (const char *name : {"for", "gosub", "while", "print", "tabneg", "read", "e_nf", "e_nf2", "e_rg", "e_od",
                             "e_fn", "e_wh", "e_we", "e_ftm", "e_ftm2", "e_ov", "e_litm", "spk_loop", "spk_prec",
                             "spk_gold", "spk_rnd0", "spk_rnd1", "spk_rnd2", "spk_rnd3", "e_dsn"})
    {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, bas(name));
        const std::string got = command(r, "RUN"), want = oracle(name);
        if (got != want)
            std::printf("  %s:\n    got  '%s'\n    want '%s'\n", name, vis(got).c_str(), vis(want).c_str());
        CHECK_EQ(got, want);
    }
}

// INPUT and LINE INPUT, NAME.in's lines typed as GW-BASIC asks for each, against PC-BASIC's output, which shows
// them as typed: prompts, "? ", several values, quoted strings, "?Redo from start" (too many, too few, not a
// number), a subscript in the list.
TEST(input_typed_lines_print_what_pc_basic_prints)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("INLIN0") == 1);
    const std::string got = run_typed(r, "input", lab.at("INLIN0")), want = oracle("input");
    if (got != want)
        std::printf("  input:\n    got  '%s'\n    want '%s'\n", vis(got).c_str(), vis(want).c_str());
    CHECK_EQ(got, want);
}

// By hand, from the 1983 source:
// - INPUT; (SCNSEM): GW-BASIC's screen editor keeps the cursor after the line typed; OODOS's function 10 echoes
//   the CR, so 7a gives no LF after it: the next output starts at the line's start (over the typed text);
// - LPRINT goes to LST: (OODOS's LPT takes and drops): the console shows nothing of it but an arithmetic
//   message, which $DIV0S sends to the console ($CATTY);
// - PRINT # and INPUT # (the files, 7c), PRINT USING (7b), the graphics LINE (7e): Advanced Feature;
// - RESTORE to a line that isn't there: Undefined line number; NEXT in direct mode with no FOR.
TEST(input_semicolon_lprint_and_later_forms)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const size_t from = r.out.size();
    r.type("INPUT;A:PRINT \"a=\";A\r");
    CHECK(r.until("? ", ms(2000), from + 5));
    r.type("7\r");
    CHECK(r.until("Ok\r\n", ms(2000), from + 5));
    CHECK(r.out.find("? 7\ra= 7 \r\nOk\r\n", from) != std::string::npos);
    CHECK_EQ(command(r, "LPRINT \"to the printer\";1,2"), std::string());
    CHECK_EQ(command(r, "PRINT 1:LPRINT 2:PRINT 3"), std::string(" 1 \r\n 3 \r\n"));
    CHECK_EQ(command(r, "LPRINT 1/0"), std::string("Division by zero\r\n")); // $CATTY: the message to the console
    CHECK_EQ(command(r, "PRINT #1,2"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "INPUT #1,A"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "PRINT USING \"##\";1"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "LINE (0,0)-(1,1)"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "RESTORE 100"), std::string("Undefined line number\r\n"));
    CHECK_EQ(command(r, "NEXT"), std::string("NEXT without FOR\r\n"));
    CHECK_EQ(command(r, "FOR I=1 TO 3:PRINT I;:NEXT"), std::string(" 1  2  3 \r\n"));
}

// The timing figures (hw/oodos.md, "The plan, phase 7": 7a's cycles), on the running GWBASIC.COM, each by
// check.py --timing's method: two runs of different lengths, the difference over the passes between them, from
// the RUN statement to the end's ENDCON (the console's tick and CHKINT's look at the keys included, as a program
// meets them). The limits are first limits, with room over the measurement.
// - the core loop, `10 I%=I%+1 : IF I%<n THEN 10`: the plan's 4,700 a pass (the spike's, on its harness),
//   with no far call;
// - a PRINT statement that prints nothing (`PRINT ;`), over the loop: its cost, with its one far call to P1;
// - a character through function 2: `PRINT A$;`, A$ 80 characters against A$ empty, over 80 a pass.
TEST(the_core_loop_a_print_and_a_character_are_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("RUN") == 1 && lab.count("ENDCON") == 1 && lab.count("FTSITE") == 1);
    const Timed l1 = timed(r, lab, {"10 I%=I%+1 : IF I%<1000 THEN 10"});
    const Timed l2 = timed(r, lab, {"10 I%=I%+1 : IF I%<3000 THEN 10"});
    const double loop = static_cast<double>(l2.cycles - l1.cycles) / 2000;
    std::printf("  the core loop: %.0f cycles a pass (the plan's 4,700), far calls %u and %u\n", loop, l1.farcalls,
                l2.farcalls);
    CHECK(loop > 1000 && loop <= 6000);
    CHECK_EQ(l1.farcalls, 0u);
    CHECK_EQ(l2.farcalls, 0u);
    const Timed p1 = timed(r, lab, {"10 PRINT ; : I%=I%+1 : IF I%<1000 THEN 10"});
    const Timed p2 = timed(r, lab, {"10 PRINT ; : I%=I%+1 : IF I%<3000 THEN 10"});
    const double print = static_cast<double>(p2.cycles - p1.cycles) / 2000 - loop;
    std::printf("  PRINT ; : %.0f cycles a statement, its far call included; far calls %u and %u\n", print,
                p1.farcalls, p2.farcalls);
    CHECK(print > 300 && print <= 3000);
    CHECK_EQ(p1.farcalls, 1000u);
    CHECK_EQ(p2.farcalls, 3000u);
    const std::string a80 = "10 A$=\"" + std::string(80, 'x') + "\"";
    const Timed c0 = timed(r, lab, {"10 A$=\"\"", "20 PRINT A$; : I%=I%+1 : IF I%<20 THEN 20"});
    const Timed c1 = timed(r, lab, {a80, "20 PRINT A$; : I%=I%+1 : IF I%<20 THEN 20"});
    const double ch = static_cast<double>(c1.cycles - c0.cycles) / (20 * 80);
    std::printf("  a character through function 2: %.0f cycles (the plan's estimate 1,500-2,500)\n", ch);
    CHECK(ch > 100 && ch <= 8000);
}

int main()
{
    return run_all();
}
