/**
 * @file test_oodos_utils_submit.cpp
 * @brief OODOS's SUBMIT, phase 5c (hw/oodos.md, "The RSXs, phase 5", SUBMIT and 5c's decisions).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_oodos_utils_rsx: OODOS.SYS booted with the card's A: and B:, SUBMIT.COM (its RSX
 * attached by gencom.py) and SETDEF on M: as user 0's SYS files, IOTEST
 * (oodos/sys/test/iotest.s) and SUB files on M:, the search chain M: first and the temporary
 * drive M:, and command lines typed at the CCP. What SUBMIT leaves on M: is read back.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    /// oodos/utils's build: the build tree's, or OODOS_UTILS_DIR's (a fault put in on purpose).
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    unsigned nrsx(const CcpRig &r) { return r.phys(RES_PHYS + sym("NRSX") - 0xC000u); }
    void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }
    void hasnt(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) == std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  don't want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }

    /// A rig with SUBMIT, SETDEF and IOTEST on M: and `files`; the chain M: first, the temporary drive M:.
    struct Sx : CcpRig
    {
        explicit Sx(std::vector<MFile> files = {}, bool submit = true)
        {
            if (!ok)
                return;
            std::vector<uint8_t> io = read_bytes(SYS + "/iotest.com");
            CHECK(!io.empty());
            files.push_back({0, "IOTEST  COM", io, true}); // SYS: found from any user
            put_m(files);
            if (submit)
                put_util("submit", UTILS);
            put_util("setdef", UTILS);
            chain({13, 0});
            scb(0x50, 13);
        }
        /// Type `line` and wait for `want` in what follows it; then for the prompt the keyboard is asked at.
        std::string run(const std::string &line, const std::string &want, uint64_t budget = ms(15000))
        {
            const size_t from = out.size();
            type(line + "\r");
            const bool seen = until(want, budget, from);
            if (!seen)
                std::printf("  '%s': never saw '%s' in '%s'\n", line.c_str(), vis(want).c_str(), vis(out.substr(from)).c_str());
            CHECK(seen);
            CHECK(calls(C_READSTR_FN, budget));   // the keyboard again
            return out.substr(from);
        }
    };
    MFile sub(const std::string &name83, const std::string &text) { return {0, name83, bytes(text)}; }
} // namespace

TEST(submit_runs_a_files_commands_then_the_keyboard_and_its_temporary_file_goes)
{
    Sx r({sub("FOO     SUB", "iotest 2A\r\niotest 2B\r\n")});
    REQUIRE(r.ok);
    const std::string o = r.run("submit m:foo", "A>iotest 2B\r\nB");
    has(o, "A>iotest 2A\r\nA");                  // each shown after the prompt as typed, then run
    CHECK(o.find("iotest 2A") < o.find("iotest 2B"));
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);
    has(r.cmd("iotest 2K"), "K");
    CHECK_EQ(nrsx(r), 0u);
}

TEST(submit_puts_in_its_arguments_dollars_and_control_characters)
{
    // The first line waits for a key, so the temporary file can be read while it serves
    Sx r({sub("ARGS    SUB", "iotest L\r\nX $1 $2 $3 $0 $$9 ^A^b^^^1^ \r\n")});
    REQUIRE(r.ok);
    r.type("submit m:args one two\r");
    CHECK(r.calls(r.C_READSTR_FN, ms(15000)));
    while (r.out.find("iotest L") == std::string::npos && r.calls(r.C_READSTR_FN, ms(2000)))
        ;
    const std::string t = text(m_data(r, 0, "SUB00   $$$"));
    CHECK_EQ(t, std::string("iotest L\r\nX ONE TWO  M:ARGS $9 \x01\x02^^1^ \r\n"));
    r.type("k\r");
    CHECK(r.until("X?", ms(6000), r.out.rfind("iotest L")));
}

TEST(submit_gives_less_than_lines_to_the_program_and_warns_of_those_left)
{
    Sx r({sub("IN      SUB", "iotest L L\r\n<ONE\r\n<\r\niotest 2Z\r\n"),
          sub("LEFT    SUB", "iotest L\r\n<A\r\n<B\r\n<C\r\niotest 2Z\r\n"),
          sub("MORE    SUB", "iotest L L\r\n<A\r\niotest 2Z\r\n"),
          sub("CHARS   SUB", "iotest C C C Q\r\n<ab\r\n<x\r\niotest 2Y\r\n"),
          sub("ASK     SUB", "user\r\n<5\r\niotest 2W\r\n"),
          sub("DMA     SUB", "iotest D\r\n<CD\r\niotest 2V\r\n")});
    REQUIRE(r.ok);
    std::string o = r.run("submit m:in", "\r\nZ");
    has(o, "ONE\rL[ONE] \rL[] ");               // shown as typed; < alone a CR
    o = r.run("submit m:left", "\r\nZ");
    has(o, "A\rL[A] \r\nWARNING: Program input ignored\r\nA>iotest 2Z\r\nZ"); // once for B and C, before
                                                                           // the prompt, the command after it
    CHECK_EQ(o.find("WARNING"), o.rfind("WARNING"));
    hasnt(o, "L[B]");
    // A program that wants more than the < lines: the keyboard, the next command waiting
    const size_t at = r.out.size();
    r.type("submit m:more\r");
    CHECK(r.until("L[A] ", ms(15000), at));
    CHECK(r.calls(r.C_READSTR_FN, ms(3000)));      // IOTEST's second L, at the keyboard
    const size_t from = r.out.size();
    r.type("KB\r");
    CHECK(r.until("\r\nZ", ms(6000), from));
    has(r.out.substr(from), "L[KB] ");
    CHECK(r.calls(r.C_READSTR_FN, ms(6000)));
    // 1 a byte at a time, then CR; status as GET's (conditional: true straight after another)
    o = r.run("submit m:chars", "\r\nY");
    has(o, "aC61 bC62 \rC0D Q0001 ");
    // 10 with A/X 0: the DMA buffer's characters first, as GET (and the BDOS's 10)
    o = r.run("submit m:dma", "\r\nV");
    has(o, "D[ABCD] ");
    // A built-in's question takes a < line
    o = r.run("submit m:ask", "\r\nW");
    has(o, "Enter User #: 5\r");
    has(o, "5A>iotest 2W");
}

TEST(a_submit_inside_a_submit_runs_first_then_the_outer_goes_on)
{
    Sx r({sub("OUT     SUB", "iotest 2A\r\nsubmit m:in\r\niotest 2C\r\n"), sub("IN      SUB", "iotest 2B\r\n")});
    REQUIRE(r.ok);
    const std::string o = r.run("submit m:out", "A>iotest 2C\r\nC");
    CHECK(o.find("A>iotest 2A") < o.find("A>iotest 2B") && o.find("A>iotest 2B") < o.find("A>iotest 2C"));
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);
    CHECK_EQ(r.m_entry(0, "SUB01   $$$"), -1);
}

TEST(a_ctrl_c_ending_a_program_ends_every_submit_level)
{
    Sx r({sub("OUT     SUB", "submit m:in\r\niotest 2C\r\n"), sub("IN      SUB", "iotest L\r\niotest 2B\r\n")});
    REQUIRE(r.ok);
    r.type("submit m:out\r");
    CHECK(r.until("A>iotest L\r", ms(15000), 0));
    CHECK(r.calls(r.C_READSTR_FN, ms(3000)));
    const size_t from = r.out.size();
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(6000), from));
    CHECK(r.calls(r.C_READSTR_FN, ms(6000)));
    hasnt(r.out.substr(from), "iotest 2");          // neither level's next line
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);
    CHECK_EQ(r.m_entry(0, "SUB01   $$$"), -1);
    has(r.cmd("iotest 2K"), "K");
}

TEST(submit_says_what_is_wrong_and_runs_nothing)
{
    Sx r({sub("PARAM   SUB", "iotest 2A\r\niotest $A\r\n"),
          {0, "ASCII   SUB", {'i', 'o', '\r', '\n', 0xFF, '\r', '\n'}},
          sub("LONG    SUB", "iotest 2A\r\n" + std::string(129, 'x') + "\r\n"),
          {0, "SUB00   $$$", bytes("STALE\r\n")}});
    REQUIRE(r.ok);
    has(r.cmd("submit m:param"), "Error on line 2: Parameter Error.\r\nA>");
    has(r.cmd("submit m:ascii"), "Error on line 2: Invalid ASCII character\r\nA>");
    has(r.cmd("submit m:long"), "Error on line 2: Line too long\r\nA>");
    has(r.cmd("submit m:none"), "No 'SUB' file found\r\nA>");
    hasnt(r.out, "\r\nA\r\n");                      // (nothing of the files ran: no IOTEST 2A's A)
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);     // the stale one gone, and no new one left
    CHECK_EQ(nrsx(r), 0u);
    // SUBMIT alone asks
    r.type("submit\r");
    CHECK(r.until("Enter File to Submit: ", ms(6000), r.out.size() - 10));
    r.type("m:none\r");
    CHECK(r.until("No 'SUB' file found", ms(6000), r.out.size() - 10));
}

TEST(the_ccp_runs_a_sub_file_typed_or_found_by_the_search_order)
{
    Sx r({sub("FOO     SUB", "iotest 2$1\r\n"), sub("BOTH    SUB", "iotest 2S\r\n"),
          {0, "BOTH    COM", read_bytes(SYS + "/iotest.com")}});
    REQUIRE(r.ok);
    std::string o = r.run("m:foo.sub q", "A>iotest 2Q\r\nQ");  // typed: SUBMIT M:FOO.SUB Q
    has(r.cmd("foo r"), "FOO?");                    // ORDER (COM), the default: no SUB looked for
    has(r.cmd("setdef [order=(com,sub)]"), "Search Order         - COM, SUB");
    o = r.run("foo r", "A>iotest 2R\r\nR");
    has(r.cmd("both 2X"), "X");                     // COM first: BOTH.COM (IOTEST)
    r.cmd("setdef [order=(sub,com)]");
    o = r.run("both", "A>iotest 2S\r\nS");          // SUB first
    has(r.cmd("setdef [order=(sub)]"), "Search Order         - SUB");
    has(r.cmd("iotest 2T"), "IOTEST?");             // SUB alone: no .COM
    has(r.cmd("m:setdef.com [order=(com,com)]"), "Invalid type for ORDER option."); // (typed: COM)
    has(r.cmd("m:setdef.com [order=(com)]"), "Search Order         - COM");
    // DISPLAY: the .SUB file's line and SUBMIT.COM's own
    r.cmd("setdef [display]");
    o = r.run("m:foo.sub d", "A>iotest 2D\r\nM:IOTEST   COM\r\nD");
    has(o, "M:FOO      SUB\r\n");
    has(o, "M:SUBMIT   COM");
}

TEST(the_ccp_says_submit_when_there_is_no_submit_com)
{
    Sx r({sub("FOO     SUB", "iotest 2Q\r\n")}, false);
    REQUIRE(r.ok);
    r.chain({13});                                  // M: alone: the card's A: has SUBMIT.COM
    has(r.cmd("m:foo.sub"), "SUBMIT?\r\nA>");
}

TEST(a_sub_run_by_name_takes_its_password)
{
    Sx r({sub("PROT    SUB", "iotest 2P\r\nx $0\r\n")});
    REQUIRE(r.ok);
    r.xfcb_m(0, "PROT    SUB", 0x80, "SESAME  "); // read protected
    std::string o = r.run("m:prot.sub;sesame", "X?");
    has(o, "A>iotest 2P\r\nP");
    hasnt(o, "Password Error");
    has(o, "A>x M:PROT.SUB\r\nX?");                 // $0 without its password
    hasnt(o, "SESAME");
    o = r.run("submit m:prot;sesame", "X?");          // typed: the same
    has(o, "A>x M:PROT\r\nX?");
    hasnt(o, "SESAME");
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);
    same(r.cmd("m:prot.sub"), "m:prot.sub\r\nPassword Error\r\nA>"); // without it: SUBMIT can't read it
}

TEST(a_less_than_line_to_a_buffer_of_size_0_gives_one_character_as_typed_input_does)
{
    Sx r({sub("ZERO    SUB", "iotest Z\r\n<AB\r\niotest 2Q\r\n")});
    REQUIRE(r.ok);
    // Typed: the BDOS's 10 takes a size of 0 as 1
    size_t from = r.out.size();
    r.type("iotest Z\r");
    CHECK(r.calls(r.C_READSTR_FN, ms(6000)));
    CHECK(r.calls(r.C_READSTR_FN, ms(6000)));
    r.type("AB\r");
    CHECK(r.until("] ", ms(6000), from));
    const std::string typed = r.out.substr(from);
    has(typed, "Z[A] ");                            // one character taken, B refused
    CHECK(r.calls(r.C_READSTR_FN, ms(6000)));
    // From a < line: the same, the rest of the line dropped
    const std::string o = r.run("submit m:zero", "\r\nQ");
    has(o, "Z[A] ");
    hasnt(o, "Z[AB]");
    hasnt(o, "WARNING");
}

TEST(submit_says_so_and_attaches_nothing_when_its_temporary_file_wont_open_again)
{
    // A fault put in as SUBMIT.COM opens its temporary file again (reopen, after the FCB is cleared):
    // its extent byte made 5, so the open finds no such extent, as for a file gone, while the name is
    // kept for the cleanup to delete the real file. (Erasing M:'s directory entry doesn't do it: the
    // BDOS's directory buffer still has it.)
    Sx r({sub("FOO     SUB", "iotest 2A\r\n")});
    REQUIRE(r.ok);
    const auto lbl = labels(UTILS + "/submit.lbl");
    REQUIRE(lbl.count("reopen") && lbl.count("tfcb"));
    const uint16_t reopen = lbl.at("reopen"), tfcb = lbl.at("tfcb");
    bool faulted = false;
    r.watch = [&](uint16_t pc) {
        if (!faulted && pc == reopen)
        {
            CHECK(r.m_entry(0, "SUB00   $$$") >= 0);  // made, written and closed
            r.machine->bus().write(static_cast<uint16_t>(tfcb + 12), 5);
            faulted = true;
        }
    };
    const std::string o = r.cmd("submit m:foo");
    r.watch = nullptr;
    CHECK(faulted);
    same(o, "submit m:foo\r\nDisk Read Error\r\nA>");
    CHECK_EQ(r.m_entry(0, "SUB00   $$$"), -1);      // the real temporary file deleted
    CHECK(r.calls(r.C_READSTR_FN, ms(3000)));      // the keyboard: nothing from the file
    CHECK_EQ(nrsx(r), 0u);
    same(r.cmd("iotest 2K"), "iotest 2K\r\nK\r\nA>");
    CHECK_EQ(nrsx(r), 0u);
}

TEST(a_less_than_line_goes_to_the_second_program_of_a_bang_line)
{
    Sx r({sub("BANG    SUB", "iotest 2A ! iotest L\r\n<ONE\r\niotest 2Z\r\n"),
          sub("LEFT2   SUB", "iotest 2A ! iotest 2B\r\n<X\r\niotest 2Z\r\n")});
    REQUIRE(r.ok);
    // The first program's end isn't the prompt: the < line is the second's
    std::string o = r.run("submit m:bang", "\r\nZ");
    has(o, "ONE\rL[ONE] ");
    hasnt(o, "WARNING");
    // Left unread by the line's last program: skipped, the warning once, before the prompt
    o = r.run("submit m:left2", "\r\nZ");
    has(o, "B\r\nWARNING: Program input ignored\r\nA>iotest 2Z\r\nZ");
    CHECK_EQ(o.find("WARNING"), o.rfind("WARNING"));
    hasnt(o, "L[X]");
}

TEST(a_built_in_in_a_bang_line_leaves_bit_6_clear_and_the_next_program_gets_its_less_than_line)
{
    // In the middle: USER's end goes by NEXTCMD and SAYPROMPT, which clears bit 6 (set to clear: not the
    // way to the prompt, so nothing skipped); the next command starts from it clear
    Sx r({sub("MID     SUB", "iotest 2A ! user 0 ! iotest L\r\n<ONE\r\niotest 2Z\r\n"),
          sub("FIRST   SUB", "user 5 ! iotest L\r\n<TWO\r\niotest 2Z\r\n")});
    REQUIRE(r.ok);
    const uint16_t user = sym("B_USER"), keyword = sym("KEYWORD");
    bool after_user = false;
    int bit6 = -1;                                  // $19's bit 6 as the command after USER starts
    r.watch = [&](uint16_t pc) {
        if (pc == user)
            after_user = true;
        else if (after_user && pc == keyword)
        {
            bit6 = r.phys(SCB_PHYS + 0x19) & 0x40;
            after_user = false;
        }
    };
    std::string o = r.run("submit m:mid", "\r\nZ");
    CHECK_EQ(bit6, 0);
    has(o, "ONE\rL[ONE] ");
    hasnt(o, "WARNING");
    // First on the line: the same, the program after it gets its < line (USER 5: IOTEST is SYS, found)
    bit6 = -1;
    o = r.run("submit m:first", "\r\nZ");
    r.watch = nullptr;
    CHECK_EQ(bit6, 0);
    has(o, "TWO\rL[TWO] ");
    hasnt(o, "WARNING");
}

int main()
{
    return run_all();
}
