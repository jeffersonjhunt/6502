/**
 * @file test_gwbasic_files.cpp
 * @brief GW-BASIC's sequential files, phase 7c (hw/oodos.md, "The plan, phase 7": block P4, the file layer), run from
 *        OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_stmts does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M:, M:GWBASIC is typed at the prompt (the CCP's current drive, A:, is where GW-BASIC's files go: the rig
 * mounts the card's /OODOS/A.IMG there), and then each program (emulator/test/host/gwbasic/NAME.bas) is typed line by line and
 * RUN, its output against PC-BASIC's for the same program (NAME.out, by oodos/gwbasic/tools/oracle.sh; or
 * NAME.differ where the 1983 source, or the OO-1's CP/M files, and PC-BASIC differ). Each program writes its own
 * files and reads them back, so it runs alike in PC-BASIC's /work and on A:.
 *
 * - The statements and functions of the file layer: OPEN in both forms and every mode, CLOSE, PRINT # (with USING),
 *   WRITE #, INPUT #, LINE INPUT #, INPUT$, EOF, LOC, LOF, RESET, and the files closed by END, RUN, NEW, CLEAR and
 *   SYSTEM; their errors.
 * - By hand, from the source: END, NEW and SYSTEM closing the files (SYSTEM's read back by the CCP's TYPE), the
 *   drives and device names, a random OPEN, INPUT$ from the keyboard.
 * - V1, route (c): the FCB and the record's buffer are in P4's own RAM, the BDOS's DMA address the buffer's CPU
 *   address in W3, while P4 is there; and PRINT #'s bytes go to P4 as runs, timed.
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

    /// A link's labels (ld65's -Ln): name to address (p4.lbl, block P4's).
    std::map<std::string, uint16_t> link_labels(const std::string &lbl)
    {
        std::map<std::string, uint16_t> out;
        std::ifstream f(BUILD + "/" + lbl);
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
    /// (its first line "# why"), where the port keeps the 1983 source's behaviour, or the OO-1's.
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

    /// Each program typed (after NEW) and RUN, against the oracle, in one GW-BASIC (so on one card: a program's
    /// files are its own, made anew).
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

// OPEN (GIO86.ASM OPEN, HAVMOD, NOTNSO, GOTMOD, OPEN1; GIODSK.ASM DSKOPN, OPNSET, APPFIN) in both forms and every
// sequential mode: "O", "I" and "A" in either case, a mode word ("Output": its first letter), FOR OUTPUT, INPUT and
// APPEND; the number with and without "#"; OUTPUT over a file (the old one deleted), APPEND at a file's end, to a
// file that isn't there (made), and to an empty one; then the file read back (PRINT # and CLOSE, LINE INPUT #, INPUT
// # and EOF, f_seq). CLOSE (CLOSE, CLSFIL, CLSALL): one, a list with and without "#", all; one not open, or twice,
// passed over; a closed file used again (Bad file number). RESET (GIODSK.ASM RESET) closes every file.
TEST(open_close_and_reset_print_what_pc_basic_prints)
{
    programs({"f_seq", "f_modes", "f_close", "f_reset"});
}

// PRINT # (GIO86.ASM PRINT's FILGET, PTRWDC for a file: width 255, so no line is ever broken) with ",", ";", TAB,
// SPC, USING, an item ending in ";" before the next PRINT #, a PRINT # alone, numbers of each type; WRITE # (FIVEO.ASM
// WRITE): strings quoted, numbers without the positive sign's space, a quote inside a string as it is; and what LINE
// INPUT # and INPUT # read of them (where PC-BASIC reads a double, NAME.differ: the 1983 source reads it as one).
TEST(print_and_write_to_a_file_print_what_pc_basic_prints)
{
    programs({"f_print", "f_write"});
}

// INPUT # (DSKCOM.ASM FILIND, INPDOR, STRCHR): numbers ended by spaces, commas or a line's end, &H; unquoted strings
// (trailing spaces dropped), quoted ones with commas, CRs and LFs inside, the spaces and comma after a quote; LFs in
// an unquoted item (dropped: the 1983 source's, NAME.differ), a quote that never closes, Input past end. LINE INPUT
// # (DLINE) of a line of 255 characters and longer ones (255 end an item, the rest is the next). INPUT$ (FIXINP)
// from a file, with "#" and without, its CR and LF as bytes, PRINT #2 of INPUT$(n, #1) (PTRFIL kept round it), and
// past the end. The ^Z end of a text file (DSKSIN): LINE INPUT # stops at it, EOF is true there. EOF (EOF, DSKEOF)
// before the end and at it.
TEST(input_line_input_and_input_s_print_what_pc_basic_prints)
{
    programs({"f_input", "f_255", "f_inps", "f_ctlz", "f_eof"});
}

// Files longer than one record (30 lines) and than several (400 numbers on a line of 2 KB), read back whole and
// interleaved with another file; LOC (DSKLOC: the records read or written) and LOF (DSKLOF: OO-1, CP/M's size in
// records, times 128: NAME.differ).
TEST(long_files_loc_and_lof_print_what_pc_basic_prints)
{
    programs({"f_long", "f_locof"});
}

// The files closed, each with its last record written, by CLEAR (BIMISC.ASM CLEAR's CLSALL; PC-BASIC's CLEAR closes
// none, NAME.differ) and by RUN (CLEARC's), each in a GW-BASIC of its own: f_run2 reads back, from another number, a
// file RUN closed without a CLOSE (with the close left out, its record would be missing: Input past end).
TEST(clear_and_run_close_every_file)
{
    programs({"f_clear"});
    programs({"f_run"});
    programs({"f_run2"});
}

// The errors, trapped by ON ERROR (ERR and ERL printed), and then each one's message: Bad file number (#0, a file not
// open: the owner's, as PC-BASIC, where the 1983 source maps File not open to Bad file mode; #4, past NMFILE), File not
// found, Bad file mode (a mode letter not O, I, A or R, a null one, PRINT # to an input file, INPUT # and LINE INPUT
// # from an output one, EOF of one with something written, INPUT$ of one), Input past end, File already open (the
// number open, or the name open for output), Bad file name (wild cards, a type alone, a second "."), Illegal function
// call (INPUT$'s 0, CLOSE #300), Type mismatch (a number for the mode), Syntax error. Where PC-BASIC departs from the
// 1983 source, NAME.differ.
TEST(the_file_errors_print_what_pc_basic_prints)
{
    programs({"f_errs", "f_errs2"});
    programs({"f_e52", "f_e53", "f_e54", "f_e55", "f_e62", "f_e64"});
}

// The files closed by END, NEW and SYSTEM, typed at the prompt (by hand, from the 1983 source: PC-BASIC's run of a
// file can't type them). Each file is written and not closed, its last record still in its buffer (ENDS1's,
// CLEARC's (SCRTCH's) and SYSTEM's CLSALL write it); then read back from another file number (so a file left open
// isn't File already open: its record would be missing). After SYSTEM the CCP types the file: GW-BASIC's files are
// on the CCP's current drive, A:, when no drive is named, and A: named is the same drive. Then, by hand, what the
// source gives that PC-BASIC's drives and devices can't: Q: (past CP/M's P:) and a device name (LPT1:, which the
// OO-1 has no device table for yet) are Bad file name; a random OPEN is Advanced Feature until X1; PRINT # with no
// file open is Bad file number (the owner's, 2026-11-14: the source's File not open maps to Bad file mode).
TEST(files_typed_at_the_prompt_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"FEND.TXT\":PRINT #1,\"by end\";:END"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#2,\"FEND.TXT\":LINE INPUT #2,A$:PRINT A$:CLOSE"), std::string("by end\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#3,\"FNEW.TXT\":PRINT #3,\"by new\":NEW"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"FNEW.TXT\":LINE INPUT #1,A$:PRINT A$:CLOSE"), std::string("by new\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"A:FDRV.TXT\":PRINT #1,\"on a\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"FDRV.TXT\":LINE INPUT #1,A$:PRINT A$:CLOSE"), std::string("on a\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"Q:FX.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"LPT1:\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"FX.DAT\""), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "OPEN \"FX.DAT\" AS #1 LEN=20"), std::string("Advanced Feature\r\n"));
    CHECK_EQ(command(r, "PRINT #1,2"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "PRINT EOF(1)"), std::string("Bad file number\r\n"));
    // INPUT$ with no file: the keys typed, not echoed, CR among them as any byte.
    const size_t from = r.out.size();
    r.type("A$=INPUT$(4):PRINT \"<\";A$;\">\";LEN(A$)\r");
    r.type("ab\rc");
    CHECK(r.until("Ok\r\n", ms(4000), from + 10));
    CHECK(r.out.find("<ab\rc> 4 \r\n", from) != std::string::npos);
    r.type("OPEN \"O\",#3,\"FSYS.TXT\":PRINT #3,\"by system\":SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string typed = r.cmd("TYPE A:FSYS.TXT");
    CHECK(typed.find("by system\r\n") != std::string::npos);
    if (typed.find("by system\r\n") == std::string::npos)
        std::printf("  TYPE: '%s'\n", vis(typed).c_str());
}

// V1, route (c) (hw/oodos.md, "The plan, phase 7": 7c): every record a file reads (function 33) or writes (34)
// goes straight to or from its FDB's buffer in P4's own RAM: at each such call the DMA address (function 26's) is
// a buffer of P4's FDB table (p4.lbl's FDB0, FDBLEN apart, DATOFS in), in $C800-$F7FF, and W3 holds block P4
// (its jump table is P4's image's). Then the file written and read back (f_seq), as the oracle has it.
TEST(each_record_goes_to_and_from_p4_s_own_ram)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> p4 = link_labels("p4.lbl");
    REQUIRE(p4.count("FDB0") == 1);
    const uint16_t fdb0 = p4.at("FDB0");
    constexpr uint16_t FDBLEN = 184, DATOFS = 48, FILES = 4; // gwbasic.inc
    const std::vector<uint8_t> image = file("p4.bin");
    REQUIRE(image.size() > 4 + 3 * 8);
    uint16_t dma = 0;
    int records = 0, outside = 0, not_p4 = 0;
    r.watch = [&](uint16_t pc) {
        if (pc != 0x0203) // the BDOS's entry
            return;
        const Registers g = r.cpu->regs();
        if (g.y == 26)
            dma = static_cast<uint16_t>(g.a | g.x << 8);
        if (g.y != 33 && g.y != 34)
            return;
        ++records;
        bool mine = false;
        for (uint16_t i = 0; i < FILES; ++i)
            mine = mine || dma == fdb0 + i * FDBLEN + DATOFS;
        if (!mine || dma < 0xC800 || dma + 128 > 0xF800)
            ++outside;
        for (uint16_t a = 4; a < 4 + 3 * 8; ++a) // BLK_ENTRY's jump table, its first 8 entries: nothing patched
            if (r.machine->bus().peek(static_cast<uint16_t>(0xC800 + a)) != image[a])
            {
                ++not_p4;
                break;
            }
    };
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, bas("f_seq"));
    CHECK_EQ(command(r, "RUN"), oracle("f_seq"));
    enter(r, bas("f_long"));
    CHECK_EQ(command(r, "RUN"), oracle("f_long"));
    r.watch = nullptr;
    CHECK(records > 10);
    CHECK_EQ(outside, 0);
    CHECK_EQ(not_p4, 0);
}

// OUTDO's run (V1: the device interface takes runs): PRINT #1,A$; with A$ 80 characters against A$ empty, 20 times,
// timed from RUN to the end's ENDCON with the far calls counted (each a JSR to FARCALL, the address the loader put
// in at READY's FTSITE), as test_gwbasic_core times PRINT. A statement makes one far call for FILGET; its 80 bytes
// go to P4 as runs of RUNSIZ (32): two when RUNBUF fills and the last 16 at FINPRT, so 3 far calls more a statement,
// where OUTDO sending each byte to P4 would make 80. The cycles a byte include each record's write.
TEST(print_to_a_file_goes_to_p4_in_runs)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("RUN") == 1 && lab.count("ENDCON") == 1 && lab.count("FTSITE") == 1);
    auto timed = [&](const std::string &a, uint64_t &cycles, unsigned &farcalls) {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, {"10 OPEN \"O\",#1,\"FTIME.TXT\":A$=\"" + a + "\"",
                  "20 PRINT #1,A$; : I%=I%+1 : IF I%<20 THEN 20", "30 CLOSE"});
        const uint16_t runc = lab.at("RUN"), endcon = lab.at("ENDCON"), site = lab.at("FTSITE");
        uint16_t farcall = 0;
        uint64_t from = 0;
        bool on = false;
        cycles = 0;
        farcalls = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == runc && !on) // (the program's windows in: the site is read here)
            {
                farcall = static_cast<uint16_t>(r.machine->bus().peek(static_cast<uint16_t>(site + 1)) |
                                                r.machine->bus().peek(static_cast<uint16_t>(site + 2)) << 8);
                from = r.cpu->cycles();
                on = true;
            }
            else if (on && pc == endcon)
            {
                cycles = r.cpu->cycles() - from;
                on = false;
            }
            else if (on && pc == farcall)
                ++farcalls;
        };
        CHECK_EQ(command(r, "RUN"), std::string());
        r.watch = nullptr;
    };
    uint64_t c0 = 0, c1 = 0;
    unsigned f0 = 0, f1 = 0;
    timed("", c0, f0);
    timed(std::string(80, 'x'), c1, f1);
    const double byte = static_cast<double>(c1 - c0) / 1600;
    std::printf("  PRINT #1,A$; : %.0f cycles a byte, its record's write included; far calls %u and %u\n", byte, f0,
                f1);
    CHECK_EQ(f1 - f0, 60u);
    CHECK(byte > 0 && byte < 2000);
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"FTIME.TXT\":PRINT LOF(1):CLOSE"), std::string(" 1664 \r\n"));
}

int main()
{
    return run_all();
}
