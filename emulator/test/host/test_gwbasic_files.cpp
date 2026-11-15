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
 * - LOAD, SAVE and MERGE (7c's LOAD step): typed sessions against PC-BASIC's transcripts (NAME.typ); W2, a tokenised
 *   LOAD at the workspace's top, with sentinels past it; and a program named on the command line, from the CCP.
 * - KILL, NAME and FILES (7c's directory step), on the BDOS's search, delete and rename, names parsed by function
 *   152; and the directory hook: CHDIR, MKDIR and RMDIR crunched to their extended tokens and answered by P4's one
 *   hook (p4/oem.s), as OODOS has no directories.
 * - Random files (7c's X1 step): FIELD, LSET, RSET, GET, PUT, CVI to MKD$, PRINT #, INPUT # and WRITE # in a FIELD
 *   buffer; records of any LEN on CP/M's 128-byte records, read and written through the FDB's buffer in P4 (V1),
 *   records past the end as zeros (X1); the FIELD buffers taken from the TPA's string space and given back.
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

    /// A typed session (LOAD's step, as test_gwbasic_stmts' step c): GW-BASIC started, then each line (a CR added)
    /// typed when GW-BASIC waits for a typed line (its PINLIN at INLIN0), as oracle.sh gives NAME.typ's lines to
    /// PC-BASIC: the whole transcript, from the start's Ok to what it printed once it waits after the last line.
    std::string session(const std::vector<std::string> &in)
    {
        CcpRig r;
        CHECK(r.ok);
        const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
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

    /// NAME.typ's lines, each ended by a CR.
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

    /// Each session against the oracle (NAME.out, PC-BASIC's transcript; or NAME.differ).
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
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"FX.DAT\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"FX.DAT\" AS #1 LEN=20:CLOSE"), std::string());
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

// LOAD, SAVE and MERGE (DSKCOM.ASM LOAD, LRUN, NOTRN1, BINLOD, MERGE, OKGETM, MAINGO, SAVE, GOODSV, ENDOFS; GIO86.ASM
// PRGFIL, NAMBAS, FSTLOD, BINSAV, INCHR's FL0EOF and NOTCHN; GIODSK.ASM DFSTLD, PROSAV, PENCOD, PDECOD; SCNEDT.ASM
// DSKCHI), typed sessions against PC-BASIC's transcripts of the same lines (NAME.typ), or NAME.differ where it
// departs from the 1983 source. l_save: a program SAVEd tokenised and LOADed, LISTed and RUN; SAVE ,A and LOAD of the
// text; a name with no type given .BAS; LOAD of a missing file (File not found); LOAD ,R, RUN name and RUN name,R;
// the options' Syntax errors. l_merge: MERGE of a text file into a program (lines replaced and added), of a tokenised
// one (Bad file mode: the source's OKGET2), of a missing one, and with more after the name (Syntax error, the file
// closed: PRGFIN). l_files: a file open across LOAD ,R (kept, NLONLY's $81), LOAD and RUN name (closed). l_text: text
// files written by PRINT #: a direct statement in one (Direct statement in file: DIRDO), its ^Z end, LFs before a
// line and before its CR, a last line with no CR, an empty file. l_prot: SAVE ,P, then LOAD: it runs, and LIST,
// EDIT, a line typed, SAVE and SAVE ,A are Illegal function call (PROCHK), until NEW (PC-BASIC lists a protected
// program: l_prot.differ, from the source).
TEST(load_save_and_merge_print_what_pc_basic_prints)
{
    sessions({"l_save", "l_merge", "l_files", "l_text", "l_prot"});
}

// W2 (hw/oodos.md, "The plan, phase 7": 7c, and its must-fail list): a tokenised LOAD reads whole records by function
// 83's DM_LOAD straight into the workspace only while each fits below its top (FSTLOD's FRETOP - 86), and the last
// part through the file's buffer, copying only the bytes below it. A program of several records whose end lies 20
// bytes into its file's last record is SAVEd; CLEAR ,n then puts the workspace's top (MEMSIZ) 90 bytes past the
// program's end, and sentinels go into the 16 bytes past it. LOAD must leave them as they were, with DM_LOAD used, and
// the program must run. A build that loads the last record whole by DM_LOAD writes it over them.
TEST(a_tokenised_load_writes_nothing_past_the_workspace_s_top)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("MEMSIZ") == 1);
    const uint16_t memsiz = lab.at("MEMSIZ");
    // The program's memory by its physical address: W0-W2 are blocks 0-2 (the CPU's view at the prompt is the
    // BDOS's, waiting in function 10).
    auto word = [&](uint16_t a) { return static_cast<uint16_t>(r.phys(a) | r.phys(a + 1u) << 8); };
    constexpr uint16_t TXTTAB = 0x79, VARTAB = 0xA0; // gwbasic.inc
    const std::string pad(100, 'x');
    enter(r, {"10 REM " + pad, "11 REM " + pad, "12 REM " + pad, "13 REM " + pad, "14 REM " + pad,
              "20 PRINT \"w2 ran\";LEN(\"" + pad + "\")"});
    CHECK_EQ(command(r, "REM"), std::string()); // (the lines taken)
    const unsigned at = static_cast<unsigned>(word(VARTAB) - word(TXTTAB) + 1); // the ^Z's place in the file
    const unsigned more = (128u + 20u - at % 128u) % 128u;
    enter(r, {"14 REM " + pad + std::string(more, 'y')});
    CHECK_EQ(command(r, "REM"), std::string());
    const uint16_t end = word(VARTAB);
    CHECK_EQ((end - word(TXTTAB) + 1) % 128, 20);
    CHECK(end - word(TXTTAB) > 4 * 128);
    CHECK_EQ(command(r, "SAVE \"W2\""), std::string());
    // CLEAR ,n: n's MEMSIZ measured, then the one wanted.
    CHECK_EQ(command(r, "CLEAR ,40000"), std::string());
    const int off = 40000 - word(memsiz), want = end + 90;
    CHECK_EQ(command(r, "CLEAR ," + std::to_string(want + off)), std::string());
    REQUIRE(word(memsiz) == want);
    for (uint16_t i = 1; i <= 16; ++i)
        r.poke(want + i, static_cast<uint8_t>(0xA5 ^ i));
    unsigned dmaloads = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == 0x0203 && r.cpu->regs().y == 83 && r.cpu->regs().x != 0)
            ++dmaloads;
    };
    CHECK_EQ(command(r, "LOAD \"W2\""), std::string());
    r.watch = nullptr;
    CHECK(dmaloads >= 1);
    unsigned changed = 0;
    for (uint16_t i = 1; i <= 16; ++i)
        changed += r.phys(want + i) != static_cast<uint8_t>(0xA5 ^ i);
    CHECK_EQ(changed, 0u);
    CHECK_EQ(word(VARTAB), end);
    CHECK_EQ(command(r, "RUN"), std::string("w2 ran 100 \r\n"));
    // A program that doesn't end below the top is Out of memory, and nothing written past it: the same file, the top
    // 30 bytes lower (its end then past FRETOP - 86).
    CHECK_EQ(command(r, "CLEAR ," + std::to_string(want - 30 + off)), std::string());
    for (uint16_t i = 1; i <= 16; ++i)
        r.poke(want - 30 + i, static_cast<uint8_t>(0x5A ^ i));
    CHECK_EQ(command(r, "LOAD \"W2\""), std::string("Out of memory\r\n"));
    changed = 0;
    for (uint16_t i = 1; i <= 16; ++i)
        changed += r.phys(want - 30 + i) != static_cast<uint8_t>(0x5A ^ i);
    CHECK_EQ(changed, 0u);
    CHECK_EQ(command(r, "LIST"), std::string());
}

// A program named on GW-BASIC's command line (GWINIT.ASM TBFLP, ENDCMD, ISSLH, FNDSLH, NXTBYT; ITSA86.ASM INITSA's
// LRUN), by hand from the source: no heading and no bytes free, the program loaded (".BAS" given) and run, then Ok;
// the name ends at a "/" (the switches are a later step's). A missing one: File not found, and back to the CCP, as an
// error in start-up's line 65534 ends GW-BASIC.
TEST(a_program_named_on_the_command_line_runs)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 PRINT \"from the command line\"", "20 SYSTEM"});
    CHECK_EQ(command(r, "SAVE \"PROG\""), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string got = r.cmd("m:gwbasic prog");
    CHECK(got.find("from the command line\r\n") != std::string::npos);
    CHECK(got.find("Bytes free") == std::string::npos);
    CHECK(got.find("Copyright") == std::string::npos);
    if (got.find("from the command line\r\n") == std::string::npos)
        std::printf("  got '%s'\n", vis(got).c_str());
    const std::string sw = r.cmd("m:gwbasic  prog/f:3");
    CHECK(sw.find("from the command line\r\n") != std::string::npos);
    const std::string none = r.cmd("m:gwbasic nofile");
    CHECK(none.find("File not found\r\n") != std::string::npos);
    if (none.find("File not found\r\n") == std::string::npos)
        std::printf("  got '%s'\n", vis(none).c_str());
}

// KILL (GIODSK.ASM KILL, KILL2), NAME (NAME, SAMDRV, NA_FNF, NA_FAE) and FILES (FILES, NOARG, ALFILS, GOTNAM, FILNXT,
// MORNAM, PRISPA, NOTEXT, NWFILN, NEXTFL), with NAMFIL and FILFCB (STRTNM, NAMORE, LNGNAM, FINNAM, TRMSTR). d_kill: a
// file KILLed, then OPENed for input (File not found), KILL of a missing file, of an open one (File already open), of
// a null name (Bad file name) and a number (Type mismatch), "?" and "*" (each file of the pattern deleted, the others
// kept). d_name: a file renamed and read by its new name, the old one gone; a missing one; to a name there already
// (File already exists); an open one; null names (Bad file name: the source's NAMFIL, NAME.differ) and numbers;
// d_name2 no AS (Syntax error). d_files: FILES with "*" and "?", lower case, a name alone, none matching (File not
// found), one of more than 8 characters (FILFCB's "." after the 8th), six names a line and fewer after PRINT ;, as the
// 1983 source prints them (NAME.differ: PC-BASIC's heading, columns and Bytes free differ). The messages of 58 and 74.
TEST(kill_name_and_files_print_what_the_source_prints)
{
    programs({"d_kill", "d_name", "d_name2", "d_files", "d_e58", "d_e74"});
}

// By hand, from the source, what PC-BASIC's run of a file can't give: FILES with no name lists the current drive's
// files, as FILES "A:" (ALFILS's "d:*.*"), "*.*" and "a:*.*" do, the same lines; a drive named before a pattern; NAME
// to another drive is Rename across disks (SAMDRV's check, before the disk is read); and the OO-1's departures: a "?"
// in either of NAME's names is Bad file name (CP/M's rename takes none), and a name function 152 refuses (Q:, a type
// of more than 3) is Bad file name, for KILL, NAME and FILES alike.
TEST(files_kill_and_name_typed_at_the_prompt_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"DHA.TXT\":CLOSE:OPEN \"O\",#1,\"DHB.TXT\":CLOSE"), std::string());
    const std::string all = command(r, "FILES");
    CHECK(all.find("DHA     .TXT DHB     .TXT ") != std::string::npos);
    CHECK(all.size() > 2 && all.substr(all.size() - 2) == "\r\n");
    CHECK_EQ(command(r, "FILES \"A:\""), all);
    CHECK_EQ(command(r, "FILES \"*.*\""), all);
    CHECK_EQ(command(r, "FILES \"a:*.*\""), all);
    CHECK_EQ(command(r, "FILES \"A:DH?.*\""), std::string("DHA     .TXT DHB     .TXT \r\n"));
    CHECK_EQ(command(r, "NAME \"DHA.TXT\" AS \"B:DHC.TXT\""), std::string("Rename across disks\r\n"));
    CHECK_EQ(command(r, "NAME \"A:DHA.TXT\" AS \"A:DHC.TXT\""), std::string());
    CHECK_EQ(command(r, "FILES \"DH?.TXT\""), std::string("DHC     .TXT DHB     .TXT \r\n"));
    CHECK_EQ(command(r, "NAME \"DH?.TXT\" AS \"DHD.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "NAME \"DHB.TXT\" AS \"DH*.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "NAME \"DHB.TXT\" AS \"DHB.TXTX\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "KILL \"Q:DHB.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "FILES \"Q:\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "KILL \"A:DH?.TXT\""), std::string());
    CHECK_EQ(command(r, "FILES \"DH*.*\""), std::string("File not found\r\n"));
    CHECK(command(r, "FILES").find("DH") == std::string::npos);
}

// The directory hook (the owner's, 2026-11-13 and 2026-11-14): CHDIR, MKDIR and RMDIR are added to P2's keyword table
// (OO-1: the 1983 table has no words for them), each crunched to $FE and its STMDSX token ($97, $98, $99: the program's
// text read at TXTTAB) and LISTed back as the word; and one hook (p4/oem.s DIRHK) answers all three: CHDIR to "\", "."
// or "" is done (the root), any other CHDIR and any RMDIR Path not found (76), MKDIR Path/File access error (75); a
// number is Type mismatch. d_dir against PC-BASIC (NAME.differ: PC-BASIC refuses CHDIR "" and makes the directory),
// the messages of 75 and 76 (added: the 1983 table ends at 74) and 77 (past them: Unprintable error, NAME.differ).
TEST(chdir_mkdir_and_rmdir_crunch_to_their_tokens_and_reach_the_directory_hook)
{
    programs({"d_dir", "d_e75", "d_e76", "d_e77"});
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::string line = "10 CHDIR \"\\\":MKDIR \"X\":RMDIR \"Y\"";
    enter(r, {line});
    CHECK_EQ(command(r, "LIST"), line + "\r\n");
    auto word = [&](uint16_t a) { return static_cast<uint16_t>(r.phys(a) | r.phys(a + 1u) << 8); };
    constexpr uint16_t TXTTAB = 0x79; // gwbasic.inc
    const uint16_t text = word(TXTTAB);
    std::vector<uint8_t> got;
    for (uint16_t i = 4; i < 4 + 17; ++i)
        got.push_back(r.phys(static_cast<uint16_t>(text + i)));
    const std::vector<uint8_t> want = {0xFE, 0x97, ' ', '"', '\\', '"', ':', 0xFE, 0x98,
                                       ' ',  '"',  'X', '"', ':',   0xFE, 0x99, ' '};
    CHECK(got == want);
    CHECK_EQ(word(static_cast<uint16_t>(text + 2)), 10);
    CHECK_EQ(command(r, "RUN"), std::string("Path/File access error in 10\r\n"));
    CHECK_EQ(command(r, "CHDIR \"\\\""), std::string());
    CHECK_EQ(command(r, "CHDIR \".\":CHDIR \"\":PRINT \"root\""), std::string("root\r\n"));
    CHECK_EQ(command(r, "CHDIR \"X\""), std::string("Path not found\r\n"));
    CHECK_EQ(command(r, "CHDIR \"\\\\\""), std::string("Path not found\r\n"));
    CHECK_EQ(command(r, "MKDIR \"\\\""), std::string("Path/File access error\r\n"));
    CHECK_EQ(command(r, "RMDIR \"\\\""), std::string("Path not found\r\n"));
    CHECK_EQ(command(r, "CHDIR"), std::string("Missing operand\r\n"));
    CHECK_EQ(command(r, "RMDIR 1"), std::string("Type mismatch\r\n"));
    CHECK_EQ(command(r, "CHDIR \"\\\" X"), std::string("Syntax error\r\n"));
}

// Random files (7c's X1 step: DSKCOM.ASM FIELD, LSET, RSET, CVI to MKD$; GIO86.ASM DPUTG, FALLOC, FFREE, XFRSTR,
// UPDSTD; GWSTS.ASM PUT, GET; GIODSK.ASM DSKOPN's random part, DSKRND, GETSUB, PUTSUB, SINP50, FOVCHK, DSKSOT's random
// part, CMPFBC; FIVEO.ASM WRTFIN's padding). r_lens: LEN 1 (300 records), 128 (FOR RANDOM, the next record by PUT
// alone) and the default, LSET and RSET into FIELDed strings, a record never written (zeros), LOC. r_order: records of
// LEN 100 PUT out of order and one again, each GET back; GET and PUT with no number (the next). r_cv: MKI$, MKS$,
// MKD$ and CVI, CVS, CVD round trips and their bytes, on edge values; their errors. r_field: FIELD's errors (FIELD
// overflow past the record, Bad file mode, Bad file number, Illegal function call, Type mismatch), GET and PUT of a
// sequential file, a FIELDed string assigned by LET (it leaves the buffer) and LSET then (a copy of the literal),
// Bad record number (0, 32768, -1), PRINT # and INPUT # in the FIELD buffer, PRINT # past it, WRITE #'s padding.
// r_seq: one file used as sequential and random in turn. r_sw: LEN past /S: (128 while the switches aren't read)
// and LEN 0: Illegal function call. r_move: strings, a string array and two FIELD buffers kept right as a random
// file opens and closes (the string space moved, XFRSTR, and the descriptors with it, UPDSTD); a FIELDed string of a
// closed file. r_eof: EOF of a random file. r_lof: LOF of one. Where PC-BASIC departs from the 1983 source, or the
// OO-1 from both, NAME.differ.
TEST(random_files_print_what_pc_basic_prints)
{
    programs({"r_lens", "r_order", "r_cv", "r_field", "r_seq", "r_sw", "r_move", "r_eof", "r_lof", "r_forr"});
}

// X1 (hw/oodos.md, "The plan, phase 7": 7c): LEN 100, records 1-5 PUT to a new file, then each GET back as it was
// written (records 2, 3 and 4 end inside a 128-byte record no earlier PUT reached, which function 33 finds unwritten:
// 1, or 4 past the extent, and the read-modify-write takes as zeros). r_x1b: records never written, past the end and
// in the last 128-byte record's tail, GET as zeros with EOF true. Must-fail: a build that takes 33's 1 or 4 as an
// error, or doesn't zero the buffer before the read.
TEST(x1_records_past_the_file_s_end_read_back_as_written)
{
    programs({"r_x1", "r_x1b"});
}

// V1, route (c), for random records too: every 128-byte record GET and PUT read (function 33) or write (34) goes to or
// from its FDB's buffer in P4's own RAM, with block P4 in W3, as each_record_goes_to_and_from_p4_s_own_ram checks the
// sequential files'; the FIELD buffer, in the TPA, is never the DMA address. r_x1 and r_order, as the oracle has them.
TEST(each_random_record_goes_to_and_from_p4_s_own_ram)
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
        for (uint16_t a = 4; a < 4 + 3 * 8; ++a) // BLK_ENTRY's jump table: P4's image's
            if (r.machine->bus().peek(static_cast<uint16_t>(0xC800 + a)) != image[a])
            {
                ++not_p4;
                break;
            }
    };
    for (const char *name : {"r_x1", "r_order"})
    {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, bas(name));
        CHECK_EQ(command(r, "RUN"), oracle(name));
    }
    r.watch = nullptr;
    CHECK(records > 20);
    CHECK_EQ(outside, 0);
    CHECK_EQ(not_p4, 0);
}

// By hand, from the source (PC-BASIC's /S: can't be given by oracle.sh): records longer than a 128-byte record. /S:
// (MAXREC, res/far4.s: the command line's switches aren't read yet) is set to 1,024, the most W4 allows, as GWBASIC
// /S:1024 would set it. LEN 200: records PUT out of order, each GET back across the 128-byte records it spans; LOC,
// and LOF (OO-1: whole records). LEN 1,024 (eight records each): three PUT out of order, four 255-byte strings and a
// MKI$ FIELDed in each, GET back; a record never written (zeros, EOF true); a record whose place passes 2^23 bytes
// (DSKRND's DH: Illegal function call) and the last before it (records 65,528 to 65,535); LEN past /S: (1,025:
// Illegal function call).
TEST(random_records_of_200_and_1024_bytes_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("MAXREC") == 1);
    const uint16_t maxrec = lab.at("MAXREC");
    CHECK_EQ(r.phys(maxrec) | r.phys(maxrec + 1u) << 8, 128);
    r.poke(maxrec, 1024 & 0xFF);
    r.poke(maxrec + 1u, 1024 >> 8);
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 OPEN \"R\",#1,\"RBIG.DAT\",200:FIELD #1,100 AS A$,100 AS B$",
              "20 FOR K=1 TO 5:READ N:LSET A$=STRING$(100,64+N):RSET B$=STR$(N):PUT #1,N:NEXT",
              "30 DATA 4,2,5,1,3",
              "40 FOR N=1 TO 5:GET #1,N:PRINT N;LEFT$(A$,2);A$=STRING$(100,64+N);RIGHT$(B$,2):NEXT",
              "50 PRINT LOC(1);LOF(1):CLOSE"});
    CHECK_EQ(command(r, "RUN"), std::string(" 1 AA-1  1\r\n 2 BB-1  2\r\n 3 CC-1  3\r\n 4 DD-1  4\r\n 5 EE-1  5\r\n"
                                            " 5  1024 \r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 OPEN \"R\",#1,\"RKB.DAT\",1024",
              "15 FIELD #1,255 AS A$,255 AS B$,255 AS C$,255 AS D$,4 AS E$",
              "20 FOR K=1 TO 3:READ N:LSET A$=STRING$(255,64+N):LSET B$=A$:LSET C$=A$:LSET D$=STRING$(255,96+N)",
              "30 LSET E$=MKI$(N)+\"ok\":PUT #1,N:NEXT:DATA 3,1,2",
              "40 FOR N=1 TO 3:GET #1,N:X$=STRING$(255,64+N)",
              "45 PRINT N;A$=X$;B$=X$;C$=X$;D$=STRING$(255,96+N);CVI(E$);RIGHT$(E$,2):NEXT",
              "50 GET #1,5:PRINT ASC(A$);ASC(E$);EOF(1);LOC(1);LOF(1)",
              "60 ON ERROR GOTO 90",
              "70 GET #1,8193:GET #1,8192:PRINT ASC(A$);EOF(1)",
              "80 CLOSE:OPEN \"R\",#1,\"RKB2.DAT\",1025:END",
              "90 PRINT \"err\";ERR;ERL:RESUME NEXT"});
    CHECK_EQ(command(r, "RUN"), std::string(" 1 -1 -1 -1 -1  1 ok\r\n 2 -1 -1 -1 -1  2 ok\r\n 3 -1 -1 -1 -1  3 ok\r\n"
                                            " 0  0 -1  5  3072 \r\nerr 5  70 \r\n 0 -1 \r\nerr 5  80 \r\n"));
}

// The FIELD buffers are in the TPA (hw/oodos.md, "The plan, phase 7": a FIELDed string's descriptor points into its
// file's buffer, which the string functions read there), taken from the top of the string space when a random file
// is opened, as the 8086's FALLOC takes its FDB, and given back when it is closed (FFREE): by hand, from the source.
// OPEN of LEN 100 lowers FRE(0) and MEMSIZ by 100, and the buffer is the 100 bytes from MEMSIZ + 2 (FILTAB, past
// VAL's zero): a LSET string is there. CLOSE puts both back, and the FIELDed string is null.
TEST(field_buffers_are_taken_from_the_tpa_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("MEMSIZ") == 1);
    const uint16_t memsiz = lab.at("MEMSIZ");
    auto word = [&](uint16_t a) { return static_cast<uint16_t>(r.phys(a) | r.phys(a + 1u) << 8); };
    CHECK_EQ(command(r, "A$=\"\":B$=\"str\"+\"ing\":F=FRE(0)"), std::string());
    const uint16_t top = word(memsiz);
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"RTPA.DAT\",100:FIELD #1,100 AS A$"), std::string());
    CHECK_EQ(command(r, "LSET A$=\"in the tpa\":PRINT F-FRE(0)"), std::string(" 100 \r\n"));
    CHECK_EQ(word(memsiz), top - 100);
    std::string at;
    for (uint16_t i = 0; i < 10; ++i)
        at += static_cast<char>(r.phys(static_cast<uint16_t>(word(memsiz) + 2 + i)));
    CHECK_EQ(at, std::string("in the tpa"));
    CHECK_EQ(command(r, "PRINT LEFT$(A$,10);LEN(A$);B$"), std::string("in the tpa 100 string\r\n"));
    CHECK_EQ(command(r, "CLOSE:PRINT F-FRE(0);LEN(A$);B$"), std::string(" 0  0 string\r\n"));
    CHECK_EQ(word(memsiz), top);
}

// FALLOC's room (GIO86.ASM FALLOC, FAPAS1, FALCOK): with less free than the FIELD buffer wants but enough once the
// strings are collected, OPEN collects them (GARBA2, once) and opens; with too little even then, Out of memory, and
// nothing changed (MEMSIZ as it was, the file not open). By hand, from the source.
TEST(a_random_open_collects_the_strings_or_is_out_of_memory)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("MEMSIZ") == 1 && lab.count("GARBA2") == 1);
    const uint16_t memsiz = lab.at("MEMSIZ"), garba2 = lab.at("GARBA2");
    auto word = [&](uint16_t a) { return static_cast<uint16_t>(r.phys(a) | r.phys(a + 1u) << 8); };
    CHECK_EQ(command(r, "A$=\"\":B$=\"\":N%=0:F=0:N%=FRE(0)/2-75:DIM X%(N%):F=FRE(0)"), std::string());
    CHECK_EQ(command(r, "A$=STRING$(100,\"a\"):A$=\"\":PRINT F-FRE(0)"), std::string(" 100 \r\n"));
    const std::string free_now = command(r, "PRINT FRE(0)");
    const int before = std::atoi(free_now.c_str());
    CHECK(before < 60 && before + 100 >= 60);
    unsigned collections = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == garba2)
            ++collections;
    };
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"RGC.DAT\",60:FIELD #1,60 AS B$:LSET B$=\"room\""), std::string());
    r.watch = nullptr;
    CHECK_EQ(collections, 1u);
    CHECK_EQ(command(r, "PRINT LEFT$(B$,4);F-FRE(0)"), std::string("room 60 \r\n"));
    const uint16_t top = word(memsiz);
    CHECK_EQ(command(r, "OPEN \"R\",#2,\"RGC2.DAT\",128"), std::string("Out of memory\r\n"));
    CHECK_EQ(word(memsiz), top);
    CHECK_EQ(command(r, "PRINT LOC(2)"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "PRINT LEFT$(B$,4):CLOSE:PRINT F-FRE(0)"), std::string("room\r\n 0 \r\n"));
}

int main()
{
    return run_all();
}
