/**
 * @file test_oodos_c.cpp
 * @brief The C target (Plan C1): a C program, compiled by cc65 and linked with oodosc.lib, run from OODOS's CCP.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * CTEST (oodos/test/c/ctest.c, `make -C oodos CPROGRAMS=ctest`) on M:, its first argument a case: main
 * with no arguments; argc and argv from the command tail, NULL after the last, and at most 16; printf's
 * conversions (cc65's _printf.s, altered for rule 2, the one linked); malloc, realloc and free; lines
 * from stdin through function 10 to a ^Z, each byte by its own read(); exit(n)'s status as function
 * 108's code, seen by a `:` command; bdos(); write() refusing a descriptor that isn't the console's. The
 * build's side (the rules gate over the C library, the stock _printf.o refused, cc65's warnings, oodos.h
 * against oodos.inc) is oodos/test/check_tools.py's.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    /// What a command printed: after its echo and CR LF, to the CR LF before the next prompt; CR LF as \n.
    std::string printed(const std::string &line, const std::string &got)
    {
        const std::string echo = line + "\r\n", prompt = "\r\nA>";
        if (got.compare(0, echo.size(), echo) != 0 || got.size() < echo.size() + prompt.size() ||
            got.compare(got.size() - prompt.size(), prompt.size(), prompt) != 0)
            return "<not a command's output: " + vis(got) + ">";
        std::string body = got.substr(echo.size(), got.size() - echo.size() - prompt.size()), out;
        for (size_t i = 0; i < body.size(); ++i)
            if (!(body[i] == '\r' && i + 1 < body.size() && body[i + 1] == '\n'))
                out += body[i];
        return out;
    }

    /// A rig with CTEST.COM on M: and M: on the search chain.
    struct CRig : CcpRig
    {
        CRig()
        {
            put_util("ctest", UTILS);
        }
        /// Run `line` and return what it printed, showing it on a mismatch with `want`.
        bool says(const std::string &line, const std::string &want)
        {
            const std::string got = printed(line, cmd(line));
            if (got != want)
                std::printf("  %s:\n    got  '%s'\n    want '%s'\n", line.c_str(), vis(got).c_str(), vis(want).c_str());
            return got == want;
        }
    };
} // namespace

TEST(main_with_no_arguments_prints_and_returns)
{
    CRig r;
    REQUIRE(r.ok);
    CHECK(r.says("m:ctest", "hello from C\n"));
}

TEST(argc_and_argv_come_from_the_command_tail)
{
    CRig r;
    REQUIRE(r.ok);
    // The CCP gives the tail in capitals; runs of spaces part words; argv[0] is "".
    CHECK(r.says("m:ctest args one  two 3", "argc=5 [] [ARGS] [ONE] [TWO] [3] null=1\n"));
    CHECK(r.says("m:ctest args", "argc=2 [] [ARGS] null=1\n"));
    // 16 at most: the words past the 15th after argv[0] are dropped, and argv[16] is NULL.
    std::string want = "argc=16 [] [ARGS]", line = "m:ctest args";
    for (char c = 'a'; c <= 'q'; ++c)
    {
        line += std::string(" ") + c;
        if (c < 'a' + 14)
            want += std::string(" [") + static_cast<char>(c - 32) + "]";
    }
    CHECK(r.says(line, want + " null=1\n"));
}

TEST(printf_s_conversions_through_stdout_and_sprintf)
{
    CRig r;
    REQUIRE(r.ok);
    CHECK(r.says("m:ctest printf", "-12345 54321 beef BEEF 10\n"
                                   "-1234567 4000000000 12345678\n"
                                   "str Q|   42|42   |00042|abc|%\n"
                                   "sprintf=7\n"));
}

TEST(the_heap_keeps_its_blocks_and_has_the_tpa_s_room)
{
    CRig r;
    REQUIRE(r.ok);
    CHECK(r.says("m:ctest heap", "heap: bad=0 big=1\n"));
}

TEST(stdin_reads_edited_lines_to_a_ctrl_z)
{
    CRig r;
    REQUIRE(r.ok);
    const size_t from = r.out.size();
    r.type("m:ctest read\r");
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));
    r.type("hello world\r");
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));
    r.type("SECOND\r");
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));
    r.type("\x1a\r");
    REQUIRE(r.until("\r\nA>", ms(3000), from + 14));
    const std::string got = r.out.substr(from);
    // Function 10 echoes the line and its CR; read() writes the LF, and gives the line and a '\n'.
    const std::string want = "m:ctest read\r\nhello world\r\ngot[hello world]\r\nSECOND\r\ngot[SECOND]\r\n^Z\r\n"
                             "lines=2 eof=1\r\n\r\nA>";
    CHECK(got == want);
    if (got != want)
        std::printf("  got  '%s'\n  want '%s'\n", vis(got).c_str(), vis(want).c_str());
}

TEST(exit_s_status_is_function_108_s_code)
{
    CRig r;
    REQUIRE(r.ok);
    // A `:` command runs only after a success: exit(3) is a failure, exit(0) and main's return 0 aren't.
    CHECK(r.says("m:ctest exit 3", ""));
    const std::string skipped = r.cmd(":m:ctest"); // the CCP's echo of a skipped line, as test_oodos_ccp has it
    CHECK(skipped == ":m:ctest\r\r\nA>");
    if (skipped != ":m:ctest\r\r\nA>")
        std::printf("  :m:ctest after exit(3): '%s'\n", vis(skipped).c_str());
    CHECK(r.says("m:ctest exit 0", ""));
    CHECK(r.says(":m:ctest", "hello from C\n"));
    CHECK(r.says(":m:ctest", "hello from C\n"));
}

TEST(bdos_calls_any_function)
{
    CRig r;
    REQUIRE(r.ok);
    CHECK(r.says("m:ctest bdos", "by function 9\nversion=31\n"));
}

TEST(write_refuses_a_descriptor_that_isn_t_the_console_s)
{
    CRig r;
    REQUIRE(r.ok);
    CHECK(r.says("m:ctest badfd", "write(5)=-1 ebadf=1\n"));
}

int main()
{
    return run_all();
}
