/**
 * @file test_oodos_utils_rsx.cpp
 * @brief OODOS's GENCOM, GET and PUT, phase 5b (hw/oodos.md, "The RSXs, phase 5"), on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_oodos_utils: OODOS.SYS booted with the card's A: and B:, the utilities
 * (oodos/utils, `make -C oodos utils`, GET.COM and PUT.COM with their RSXs attached by
 * gencom.py) put on M: as user 0's SYS files, and command lines typed at the CCP.
 * IOTEST (oodos/sys/test/iotest.s) reads by 1, 6, 10 and 11 and writes by 2, 6, 9, 111, 5
 * and 112 as its tail says. What PUT and GENCOM leave on M: is read back from its directory.
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
    constexpr uint32_t BLOCK62 = 62u * 0x4000u;
    /// oodos/utils's build: the build tree's, or OODOS_UTILS_DIR's (a fault put in on purpose).
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    std::vector<uint8_t> built(const std::string &dir, const std::string &name)
    {
        std::vector<uint8_t> b = read_bytes(dir + "/" + name);
        CHECK(!b.empty());
        if (b.empty())
            std::printf("  %s/%s: not built\n", dir.c_str(), name.c_str());
        return b;
    }
    unsigned nrsx(const CcpRig &r) { return r.phys(BLOCK62 + sym("NRSX") - 0xC000u); }

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

    /// A rig with GET, PUT and GENCOM on M:, IOTEST, and `files`; the search chain M: first.
    struct Rx : CcpRig
    {
        explicit Rx(std::vector<MFile> files = {})
        {
            if (!ok)
                return;
            std::vector<uint8_t> io = built(SYS, "iotest.com"); // zero-padded, as gencom.py pads
            io.resize((io.size() + 127) / 128 * 128, 0);
            files.push_back({0, "IOTEST  COM", io});
            put_m(files);
            put_util("get", UTILS);
            put_util("put", UTILS);
            put_util("gencom", UTILS);
            chain({13, 0});
        }
    };
} // namespace

// ---- GET -------------------------------------------------------------------------------------

TEST(get_gives_the_next_programs_input_from_a_file_then_the_keyboard)
{
    Rx r({{0, "IN      TXT", bytes("ONE\r\nTWO\r\n")}});
    REQUIRE(r.ok);
    std::string o = r.cmd("get file m:in.txt");
    has(o, "Getting console input from file: IN.TXT");
    CHECK_EQ(nrsx(r), 1u);
    o = r.cmd("iotest L L");
    has(o, "ONE\rL[ONE] TWO\rL[TWO] ");           // shown as typed (ECHO), then the program's
    // The file done: the keyboard again, and the RSX gone at the next warm boot
    const size_t from = r.out.size();
    r.type("iotest L\r");
    r.run(ms(500));
    r.type("KEY\r");
    CHECK(r.until("L[KEY] ", ms(3000), from));
    r.run(ms(300));
    CHECK_EQ(nrsx(r), 0u);
    // The long form, NO ECHO; a program that ends first: the keyboard for the next
    o = r.cmd("get console input from file m:in.txt [no echo]");
    has(o, "Getting console input from file: IN.TXT");
    o = r.cmd("iotest L");
    has(o, "\r\nL[ONE] ");
    hasnt(o, "ONE\r");
    const size_t f2 = r.out.size();
    r.type("iotest L\r");
    r.run(ms(500));
    r.type("K2\r");
    CHECK(r.until("L[K2] ", ms(3000), f2));
    r.run(ms(300));
    CHECK_EQ(nrsx(r), 0u);
}

TEST(get_reads_lines_bytes_and_raw_bytes_to_a_ctrl_z)
{
    Rx r({{0, "LINES   TXT", bytes("A\r\nB\nC\x1A" "D\r\n")}, {0, "BYTES   TXT", bytes("xy\r\npq")}});
    REQUIRE(r.ok);
    r.cmd("get file m:lines.txt");
    std::string o = r.cmd("iotest L L L");          // CR LF, a lone LF, then ^Z ends the file
    has(o, "L[A] B\rL[B] C\rL[C] ");
    r.cmd("get file m:bytes.txt");
    o = r.cmd("iotest C C C R W");                  // 1 echoes; the LF after CR dropped; 6 raw
    has(o, "xC78 yC79 \rC0D R70 W71 ");
    CHECK_EQ(nrsx(r), 1u);                          // ended with its program, not its file
    r.cmd("iotest");                                // (gone at the warm boot after the next program)
    CHECK_EQ(nrsx(r), 0u);
}

TEST(get_answers_status_by_the_console_modes_bits_8_and_9)
{
    Rx r({{0, "IN      TXT", bytes("abcdefgh\r\n")}});
    REQUIRE(r.ok);
    r.cmd("get file m:in.txt");
    has(r.cmd("iotest S Q U S"), "S00 Q0001 U00FF S00 "); // conditional: true only straight after another
    r.cmd("get file m:in.txt");
    has(r.cmd("iotest M1 S Q T"), "S01 Q0101 TFF ");         // bit 8: always true
    r.cmd("get file m:in.txt");
    has(r.cmd("iotest M2 S Q T"), "S00 Q0000 T00 ");         // bit 9: always false
    r.cmd("get file m:in.txt");
    const size_t from = r.out.size();                        // both: no redirection
    r.type("iotest M3 L\r");
    r.run(ms(500));
    r.type("KB\r");
    CHECK(r.until("L[KB] ", ms(3000), from));
}

TEST(get_system_takes_the_ccps_lines_until_its_end_or_a_get_console_line)
{
    Rx r({{0, "S1      TXT", bytes("iotest 2X\r\nget console\r\niotest 2Y\r\n")},
          {0, "S2      TXT", bytes("iotest 2Z\r\n")},
          {0, "N1      TXT", bytes("get file m:n2.txt [system]\r\niotest 2P\r\n")},
          {0, "N2      TXT", bytes("iotest 2Q\r\n")}});
    REQUIRE(r.ok);
    size_t from = r.out.size();
    r.type("get file m:s1.txt [system]\r");
    CHECK(r.until("A>get console\r", ms(6000), from));
    r.run(ms(1500));
    has(r.out.substr(from), "A>iotest 2X\r\nX");
    hasnt(r.out.substr(from), "iotest 2Y");          // GET CONSOLE: the keyboard again
    has(r.cmd("iotest 2K"), "K");
    from = r.out.size();
    r.type("get file m:s2.txt [system]\r");
    CHECK(r.until("A>iotest 2Z\r\nZ", ms(6000), from));
    r.run(ms(800));
    has(r.cmd("iotest 2J"), "J");                    // the file's end: the keyboard
    // A GET in a GET file stacks: the newer file first, then the rest of the older
    from = r.out.size();
    r.type("get file m:n1.txt [system]\r");
    CHECK(r.until("A>iotest 2P\r\nP", ms(9000), from));
    const std::string s = r.out.substr(from);
    CHECK(s.find("Q") != std::string::npos && s.find("Q") < s.find("A>iotest 2P"));
    r.run(ms(800));
    has(r.cmd("iotest 2W"), "W");
}

TEST(get_refuses_what_isnt_a_form_and_leaves_no_rsx)
{
    Rx r({{0, "IN      TXT", bytes("x\r\n")}});
    REQUIRE(r.ok);
    const std::pair<std::string, std::string> cases[] = {
        {"get", "Invalid command."},
        {"get console input", "Invalid command."},
        {"get console input from printer", "Invalid command."},
        {"get file", "Invalid command."},
        {"get auxiliary input from file m:in.txt", "Auxiliary device redirection not implemented"},
        {"get console input from auxiliary", "Auxiliary device redirection not implemented"},
        {"get file m:in.txt [echo,system]", "Invalid option modifier."},
        {"get file m:in.txt [echo,no echo]", "Invalid option modifier."},
        {"get file m:in.txt [system=1]", "Invalid option modifier."},
        {"get file m:in.txt [bogus]", "Invalid option."},
        {"get file m:in.txt [echo][system]", "Invalid delimiter."},
        {"get file m:in.txt [echo", "Invalid delimiter."},
        {"get file m:in.txt junk", "End of line expected"},
        {"get file m:in.txt [echo] junk", "End of line expected"},
        {"get console junk", "Invalid command."},
        {"get file m:*.txt", "Invalid file specification."},
        {"get file m:none.txt", "No File"},
    };
    for (const auto &[line, want] : cases)
    {
        has(r.cmd(line), "\r\n" + want + "\r\nA>");
        r.cmd("dir m:in.txt");
        CHECK_EQ(nrsx(r), 0u);
    }
    has(r.cmd("get console"), "get console\r\n\r\nA>"); // nothing to end: nothing said
}

// ---- PUT -------------------------------------------------------------------------------------

TEST(put_sends_the_next_commands_output_to_a_file_a_built_ins_too)
{
    Rx r({{0, "IN      TXT", bytes("x\r\n")}});
    REQUIRE(r.ok);
    std::string o = r.cmd("put console output to file m:out.txt");
    has(o, "Putting console output to file: OUT.TXT");
    o = r.cmd("dir m:in.txt");
    has(o, "M: IN       TXT");                       // ECHO, the console's default
    CHECK_EQ(text(m_data(r, 0, "OUT     TXT")), std::string("\nM: IN       TXT")); // the CCP's owed LF first
    CHECK_EQ(r.m_entry(0, "OUT     $$$"), -1);
    r.cmd("iotest");
    CHECK_EQ(nrsx(r), 0u);
    // A program's: 2, 9, 111 and 6, as sent, a tab as a tab; NO ECHO: nothing on the console
    r.cmd("put console file m:p.txt [no echo]");
    o = r.cmd("iotest 2a9B6zE");
    hasnt(o, "Anine");                               // (the tail in capitals, as the CCP takes it)
    CHECK_EQ(text(m_data(r, 0, "P       TXT")), std::string("\nAnineblockZ\x1B\t\x01"));
    // FILTER: control characters as ^ and a letter, CR and LF kept
    r.cmd("put console file m:f.txt [filter, no echo]");
    r.cmd("iotest E N 2q");
    CHECK_EQ(text(m_data(r, 0, "F       TXT")), std::string("\n^[^I^A\r\nQ"));
}

TEST(put_no_echo_shows_its_prompt_when_a_program_asks_for_input)
{
    Rx r;
    REQUIRE(r.ok);
    r.cmd("put console file m:out.txt [no echo]");
    const size_t from = r.out.size();
    r.type("iotest 2a L\r");
    CHECK(r.until("PUT>", ms(3000), from));
    r.type("in\r");
    CHECK(r.until("\r\nA>", ms(3000), from + 10));
    has(text(m_data(r, 0, "OUT     TXT")), "AL[in] ");
}

TEST(put_printer_output_to_a_file_and_system_until_put_back)
{
    std::string big;
    while (big.size() < 6000)
        big += "BIG FILE LINE\r\n";
    Rx r({{0, "IN      TXT", bytes("x\r\n")}, {0, "BIG     TXT", bytes(big)}});
    REQUIRE(r.ok);
    r.put_util("pip", UTILS);
    std::string o = r.cmd("put printer output to file m:lst.txt");
    has(o, "Putting printer output to file: LST.TXT");
    r.cmd("iotest 5a X 2c");
    CHECK_EQ(text(m_data(r, 0, "LST     TXT")), std::string("Alist"));
    // SYSTEM: every command's, closed at each end so the warm boot keeps its blocks, until put back
    r.cmd("put console file m:sys.txt [system]");
    r.cmd("iotest " + std::string(100, '9'));        // 400 bytes: records written before warm boots,
    r.cmd("pip m:copy.txt=m:big.txt");              // then blocks taken by another file: the PUT file's
    r.cmd("iotest 2x");                             // kept (closed at each command's end, not freed by 98)
    r.cmd("dir m:in.txt");
    r.cmd("iotest 2y");
    CHECK_EQ(nrsx(r), 1u);
    r.cmd("put console output to console");
    r.cmd("dir m:in.txt");
    CHECK_EQ(nrsx(r), 0u);
    const std::string s = text(m_data(r, 0, "SYS     TXT"));
    std::string nines;
    for (int i = 0; i < 100; ++i)
        nines += "nine";
    has(s, nines);
    has(s, "X");
    has(s, "M: IN       TXT");
    has(s, "Y");
    hasnt(s, "PUT CONSOLE OUTPUT TO CONSOLE\r\nA>DIR");
    r.cmd("put printer file m:l2.txt [echo,system]");
    r.cmd("iotest 5p");
    r.cmd("put printer output to printer");
    CHECK_EQ(text(m_data(r, 0, "L2      TXT")), std::string("P"));
    // NO ECHO the printer's default, ECHO on asking: LST: put on the serial line (the SCB's $2A: TTY) to see it
    r.scb(0x2A, 0);
    r.scb(0x2B, 0x40);
    r.cmd("put printer file m:l3.txt");
    hasnt(r.cmd("iotest 5Q"), "\r\nQ");
    r.cmd("put printer file m:l4.txt [echo]");
    has(r.cmd("iotest 5R"), "\r\nR");
    CHECK_EQ(text(m_data(r, 0, "L3      TXT")), std::string("Q"));
    CHECK_EQ(text(m_data(r, 0, "L4      TXT")), std::string("R"));
}

TEST(put_asks_about_a_file_there_already_and_about_another_file)
{
    Rx r({{0, "OUT     TXT", bytes("OLD\r\n")}, {0, "RO      TXT", bytes("R\r\n"), false, true}});
    REQUIRE(r.ok);
    std::string o = r.to("put console file m:out.txt", "(Y/N)");
    has(o, "File already exists Delete it? (Y/N)");
    o = r.cmd("n");
    CHECK_EQ(nrsx(r), 0u);                           // (PUT.COM's own RSX gone with it)
    CHECK_EQ(text(m_data(r, 0, "OUT     TXT")), std::string("OLD\r\n"));
    r.to("put console file m:out.txt", "(Y/N)");
    r.cmd("y");
    CHECK_EQ(text(m_data(r, 0, "OUT     TXT")), std::string("OLD\r\n")); // kept until the new is done
    r.cmd("iotest 2n");
    CHECK_EQ(text(m_data(r, 0, "OUT     TXT")), std::string("\nN"));
    has(r.cmd("put console file m:ro.txt"), "Read only.");
    // Another file: Y, both get the output; N, the old one closed and the new one alone
    r.cmd("put console file m:a.txt [system]");
    o = r.to("put console file m:b.txt", "(Y/N)");
    has(o, "Do you want another file? (Y/N)");
    r.cmd("y");
    r.cmd("iotest 2z");
    r.cmd("put console output to console");
    has(text(m_data(r, 0, "B       TXT")), "Z");
    has(text(m_data(r, 0, "A       TXT")), "Z");
    r.cmd("put console file m:c.txt [system]");
    r.to("put console file m:d.txt", "(Y/N)");
    r.cmd("n");
    r.cmd("iotest 2w");
    hasnt(text(m_data(r, 0, "C       TXT")), "W");
    has(text(m_data(r, 0, "D       TXT")), "W");
}

TEST(the_rsxs_own_file_calls_leave_the_programs_state_as_it_was)
{
    Rx r({{0, "IN      TXT", bytes("A LINE\r\n")}});
    REQUIRE(r.ok);
    r.cmd("put console file m:out.txt [system]");          // PUT writes a record and GET reads one,
    r.cmd("get file m:in.txt [no echo]");                  // inside the program's calls
    r.cmd("iotest V");
    r.cmd("put console output to console");
    const std::string s = text(m_data(r, 0, "OUT     TXT"));
    has(s, std::string(130, '.') + "V9000 0123 05 FE 03 ");
}

TEST(put_says_when_its_file_was_erased_under_it)
{
    Rx r;
    REQUIRE(r.ok);
    r.cmd("put console file m:out.txt [no echo]");
    std::string o = r.cmd("era m:out.$$$");
    has(o, "PUT ERROR: FILE ERASED.");
    CHECK_EQ(r.m_entry(0, "OUT     TXT"), -1);
}

TEST(put_refuses_what_isnt_a_form)
{
    Rx r;
    REQUIRE(r.ok);
    const std::pair<std::string, std::string> cases[] = {
        {"put", "Invalid command."},
        {"put console", "Invalid command."},
        {"put console output to printer", "Invalid command."},
        {"put printer to console", "Invalid command."},
        {"put file m:x.txt", "Invalid command."},
        {"put auxiliary output to file m:x.txt", "Auxiliary device redirection not implemented"},
        {"put console file m:x.txt [echo,no echo]", "Invalid option modifier."},
        {"put console file m:x.txt [filter,nofilter]", "Invalid option modifier."},
        {"put console file m:x.txt [bogus]", "Invalid option."},
        {"put console file m:x.txt [echo] [system]", "Invalid delimiter."},
        {"put console file m:x.txt more", "End of line expected"},
        {"put console file m:x.txt [echo] more", "End of line expected"},
        {"put console file m:x?.txt", "Invalid file specification."},
    };
    for (const auto &[line, want] : cases)
    {
        has(r.cmd(line), "\r\n" + want + "\r\nA>");
        r.cmd("dir m:");
        CHECK_EQ(nrsx(r), 0u);
        CHECK_EQ(r.m_entry(0, "X       $$$"), -1);
    }
}

// ---- GENCOM ----------------------------------------------------------------------------------

namespace
{
    /// GENCOM's rig: IOTEST, two test RSXs and a text file on M:.
    struct Gx : Rx
    {
        explicit Gx(std::vector<MFile> more = {})
            : Rx([&] {
                  more.push_back({0, "TRSXA   RSX", built(SYS, "trsxa.rsx")});
                  more.push_back({0, "TRSXB   RSX", built(SYS, "trsxb.rsx")});
                  more.push_back({0, "IN      TXT", bytes("x\r\n")});
                  return more;
              }())
        {
        }
    };
    /// M:'s file is the build's, to its records.
    void is_built(CcpRig &r, const std::string &name83, const std::string &file)
    {
        const std::vector<uint8_t> got = m_data(r, 0, name83), want = built(SYS, file);
        std::vector<uint8_t> padded = want;
        padded.resize((want.size() + 127) / 128 * 128, 0);
        const bool same_bytes = got == padded || got == want;
        CHECK(same_bytes);
        if (!same_bytes)
        {
            size_t i = 0;
            while (i < got.size() && i < padded.size() && got[i] == padded[i])
                ++i;
            std::printf("  %s: %zu bytes, %s %zu; first difference at %zu\n", name83.c_str(), got.size(), file.c_str(),
                        padded.size(), i);
        }
    }
} // namespace

TEST(gencom_attaches_replaces_strips_and_makes_an_rsx_only_com_as_gencom_py_does)
{
    Gx r;
    REQUIRE(r.ok);
    std::string o = r.cmd("gencom m:iotest m:trsxa");
    hasnt(o, "\r\nA>gencom");
    is_built(r, "IOTEST  COM", "iota.com");
    CHECK_EQ(r.m_entry(0, "IOTEST  $$$"), -1);
    o = r.cmd("gencom m:iotest m:trsxa");            // again: replaced
    has(o, "Duplicate RSX in header. Replacing old by new");
    is_built(r, "IOTEST  COM", "iota.com");
    r.cmd("gencom m:iotest m:trsxb");                // added
    is_built(r, "IOTEST  COM", "iotab.com");
    r.cmd("gencom m:iotest");                        // taken off
    is_built(r, "IOTEST  COM", "iotest.com");
    r.cmd("gencom m:iotest [scb=(34,02),(06,5A)]");  // SCB entries, both forms
    is_built(r, "IOTEST  COM", "iots.com");
    r.cmd("gencom m:iotest");
    r.cmd("gencom m:iotest [scb=(34,2),scb=(6,5a)]");
    is_built(r, "IOTEST  COM", "iots.com");
    r.cmd("gencom m:iotest");
    r.cmd("gencom m:iotest [loader]");               // LOADER alone: as it was
    is_built(r, "IOTEST  COM", "iotest.com");
    r.cmd("gencom m:trsxa [null]");                  // RSXs only, named after the first
    is_built(r, "TRSXA   COM", "iotn.com");
    // The attached program runs with its RSX: A's letter before IOTEST's '#'... (A writes its
    // letter for a '#' by 2)
    r.cmd("gencom m:iotest m:trsxa");
    has(r.cmd("iotest 2#"), "A#");
}

TEST(gencom_says_what_is_wrong)
{
    std::vector<uint8_t> big(31 * 1024, 0x55);       // an RSX's header claiming 30 KB of image
    const std::vector<uint8_t> h = {0x4F, 0x4F, 0x44, 0x1A, 0x03, 0x03, 0x30, 0x75, 0, 0, 0, 0, 0, 0, 0, 0};
    std::copy(h.begin(), h.end(), big.begin());
    Gx r({{0, "BIG1    RSX", big}, {0, "BIG2    RSX", big}, {0, "RO      COM", built(SYS, "iotest.com"), false, true}});
    REQUIRE(r.ok);
    std::string scb16 = "[scb=";
    for (unsigned i = 0; i < 16; ++i)
        scb16 += (i ? ",(" : "(") + hex(i, 1) + ",1)";
    scb16 += "]";
    const std::pair<std::string, std::string> cases[] = {
        {"gencom m:trsxa.rsx m:trsxb", "First submitted file must be a COM file"},
        {"gencom m:in.txt m:trsxb", "First submitted file must be a COM file"},
        {"gencom m:iotest m:trsxa [null]", "IOTEST.RSX No File"}, // with NULL, the first is an RSX
        {"gencom m:iotest.com [null]", "COM file found and NULL option"},
        {"gencom m:iotest m:in.txt", "Invalid RSX type"},
        {"gencom m:iotest m:iotest.com", "Invalid RSX type"},
        {"gencom m:*.com m:trsxa", "Invalid wild card in the FCB name or type field"},
        {"gencom m:iotest m:big1 m:big2", "Total file size exceeds 64K"},
        {"gencom m:iotest " + scb16, "No more space in the header for RSXs or SCB initialization."},
        {"gencom m:iotest [bogus]", "Unrecognized option"},
        {"gencom m:iotest [null=1]", "No modifier for this option"},
        {"gencom m:iotest [scb=34,02]", "Missing left parenthesis."},
        {"gencom m:iotest [scb=(34,02]", "Missing right Parenthesis"},
        {"gencom m:iotest [scb=]", "Missing SCB value."},
        {"gencom m:iotest [scb=(64,01)]", "Invalid SCB offset"},
        {"gencom m:ro m:trsxa", "Read only"},
        {"gencom m:nofile m:trsxa", "NOFILE.COM No File"},
    };
    for (const auto &[line, want] : cases)
    {
        has(r.cmd(line), want + "\r\nA>");
        CHECK_EQ(r.m_entry(0, "IOTEST  $$$"), -1);
        is_built(r, "IOTEST  COM", "iotest.com");
    }
    std::string o = r.cmd("gencom m:iotest m:trsxa m:trsxa");  // named twice: taken once
    has(o, "Duplicate input RSX");
    is_built(r, "IOTEST  COM", "iota.com");
}

int main()
{
    return run_all();
}
