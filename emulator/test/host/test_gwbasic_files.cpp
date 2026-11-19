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
 * - The owner's small decisions (2026-11-14), by hand: error mode $FE, so a drive with no image is a BASIC error;
 *   the command line's switches (/F: /S: /C: read and ignored, /M: the workspace's top); FILES leaving out SYS files;
 *   a CR after an LF going on with a loaded line; a name of more than 8 characters and a type, Bad file name.
 * - CHAIN and COMMON (7c's CHAIN step): COMMON's variables, ALL, a line, MERGE with DELETE, the DEF types, a file
 *   open across it, the errors; LIST and LLIST to a file; VARPTR(#n), by hand: the FDB's place in P4's table.
 * - The devices as files (7c's devices step), by hand from the source: LPT1: to LST: as LPRINT goes, and LPOS;
 *   COM1: on AUX:, polled, its options parsed and kept (the speed not set: the line stays 19,200), the bytes sent
 *   and received watched at the BDOS's entry; SCRN:, KYBD: and CONS:; the COM statement with G10's guard (TRPTBL's
 *   range), LCOPY, ERDEV and IOCTL.
 * - The 7c fidelity review's findings (the owner, 2026-11-15), by hand, each failing without its fix: LOC and LOF
 *   inside a PRINT # (W1), CHKFOP and a device's slot (W2) and attribute bits (W3), RUN of a file ending in a bare LF
 *   (W4), FILFCB's names as MS-DOS's parse takes them (W5, W6, W8), KILL and NAME passing over SYS files (W7),
 *   LLIST's CR after an LF (W9), SCRN:'s last character after console output (W10), a break's new line (W12).
 * - 7c's tail, by hand from the source and the design (T1-T12): P5's start-up (GX_INIT, GX_BANK8); DEF SEG, PEEK and
 *   POKE on each route (segment 0's own bytes, GX_BLOCK, DM_COPY, bank 8 refused in console mode, OODOS's memory);
 *   USR under another segment; BLOAD and BSAVE, the run checked whole, the file's bytes, the errors; PEEK's and
 *   BLOAD's cycles.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "video_card.h"

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
        r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
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

    /// FILES typed: what it printed, its heading line ("A:\\": the drive, the IBM manual's directory name) and its
    /// bytes-free line left out (the IBM spec's fix-up: the names alone, as the source prints them).
    std::string files(CcpRig &r, const std::string &line)
    {
        std::string o = command(r, line);
        if (o.size() >= 5 && o[1] == ':' && o.compare(2, 3, "\\\r\n") == 0)
            o = o.substr(5);
        const size_t b = o.rfind(" Bytes free\r\n");
        if (b != std::string::npos && b + 13 == o.size())
            o = o.substr(0, o.rfind("\r\n", b) + 2);
        return o;
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
// interleaved with another file; LOC (DSKLOC: the records read or written) and LOF (DSKLOF: the bytes, the ^Z
// counted, as PC-BASIC's: the IBM manual's p. 175, from the last record's byte count CLOSE keeps with the file and
// OPEN reads back, OODOS's CP/M 3 count; the owner's answer 2 of the IBM spec's fix-up).
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
// file open is Bad file number (the IBM manual's A-4, error 52), and GET and PUT of one Bad file mode (A-3, error 54:
// the IBM spec's fix-up, A2; seen failing with DPUTG's not-open branch on DERFNO).
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
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"LPT1:\":PRINT #1,\"x\":CLOSE"), std::string()); // to LST: (lpt1_files_...)
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"FX.DAT\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"FX.DAT\" AS #1 LEN=20:CLOSE"), std::string());
    CHECK_EQ(command(r, "PRINT #1,2"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "PRINT EOF(1)"), std::string("Bad file number\r\n"));
    // GET and PUT of a file not open: Bad file mode (the IBM manual's A-3, error 54, and the source's; the IBM spec's
    // fix-up, A2), the one place A-4's Bad file number doesn't apply.
    CHECK_EQ(command(r, "GET #1,1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "PUT #2"), std::string("Bad file mode\r\n"));
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
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"FTIME.TXT\":PRINT LOF(1):CLOSE"), std::string(" 1601 \r\n"));
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
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
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

// The command line's switches (GWINIT.ASM SCANSW, SCANS1, FOK, WASC, WASS, WASF, GETVAL; GWEVAL.ASM CNSGET; ITSA86.ASM
// MAPCLC's and MAPINI's checks), by hand from the source (the owner's decision 6, 2026-11-14): /F: and /S: read and
// ignored, /C: too (OO-1), /M:n the workspace's top as CLEAR's n (MEMSIZ n - 2): FRE(0) is less by the TPA's top less
// n, the bytes free 4 less than n less TXTTAB, a hex n (&H) taken. A letter that isn't a switch, a missing colon,
// something after the number that isn't "/", an n past the TPA's top or too near TXTTAB: the heading, "Error detected
// in command line", and back to the CCP (CMDERR, before start-up is done).
TEST(the_command_line_s_switches_are_read_and_m_sets_the_top)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 PRINT \"free\";FRE(0)", "20 SYSTEM"});
    CHECK_EQ(command(r, "SAVE \"PROG\""), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    auto fre = [&](const std::string &line) {
        const std::string o = r.cmd(line);
        const size_t at = o.find("free ");
        if (at == std::string::npos)
        {
            std::printf("  '%s': '%s'\n", line.c_str(), vis(o).c_str());
            return -1L;
        }
        return std::strtol(o.c_str() + at + 5, nullptr, 10);
    };
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    const long all = fre("m:gwbasic prog"), top = r.phys(0x0204) | r.phys(0x0205) << 8;
    CHECK(all > 30000);
    CHECK_EQ(fre("m:gwbasic prog /f:3 /s:256 /m:40000"), all - (top - 40000));
    CHECK_EQ(fre("m:gwbasic prog /M:40000/F:3"), all - (top - 40000));
    CHECK_EQ(fre("m:gwbasic prog /c:256 /m:&H9C40 /s:1024"), all - (top - 40000));
    CHECK_EQ(fre("m:gwbasic prog /f:3 /s:256"), all);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("LOAD") == 1);
    const std::string head = r.to("m:gwbasic /m:40000", "Ok\r\n");
    CHECK(head.find("\r\n" + std::to_string(40000 - 2 - (lab.at("LOAD") + 1) - 2) + " Bytes free\r\n") !=
          std::string::npos);
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string refused = "(C) Copyright Microsoft 1982         \r\n\r\nError detected in command line\r\n";
    for (const std::string &tail : std::vector<std::string>{"prog /x:3", "/m40000", "prog /f:3 x", "/m:" + std::to_string(top + 1),
                                   "/m:" + std::to_string(lab.at("LOAD") + 1 + 32), "prog /s"})
    {
        const std::string o = r.cmd("m:gwbasic " + tail);
        CHECK(o.find(refused) != std::string::npos);
        CHECK(o.find("free") == std::string::npos);
        if (o.find(refused) == std::string::npos)
            std::printf("  '%s': '%s'\n", tail.c_str(), vis(o).c_str());
    }
    const std::string least = r.to("m:gwbasic /m:" + std::to_string(lab.at("LOAD") + 1 + 33), "Ok\r\n");
    CHECK(least.find("\r\n29 Bytes free\r\n") != std::string::npos); // 33 above TXTTAB: the least taken
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
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
// files (its heading and bytes free the IBM manual's, below), as FILES "A:" (ALFILS's "d:*.*"), "*.*" and "a:*.*" do, the same lines; a drive named before a pattern; NAME
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
    // The IBM manual's heading (the drive's root, OODOS having no directories) and bytes-free line (pp. 102-103; the
    // IBM spec's fix-up, the owner's answer 1): the free bytes OODOS's function 46 gives, a multiple of 128, which a
    // 16 KB file written takes at least 16,384 from; B:, empty, has the heading and File not found.
    CHECK(all.rfind("A:\\\r\n", 0) == 0);
    const size_t at = all.rfind("\r\n", all.size() - 3) + 2;
    const std::string freeline = all.substr(at);
    CHECK(freeline.size() > 13 && freeline.substr(freeline.size() - 13) == " Bytes free\r\n");
    const long before = std::atol(freeline.c_str());
    CHECK(before > 0 && before % 128 == 0);
    CHECK_EQ(command(r, "FILES \"A:DH?.*\""), "A:\\\r\nDHA     .TXT DHB     .TXT \r\n" + freeline);
    CHECK_EQ(command(r, "FILES \"B:\""), std::string("B:\\\r\nFile not found\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"DHBIG.TXT\":FOR I=1 TO 128:PRINT #1,STRING$(126,\"x\"):NEXT:CLOSE"),
             std::string());
    const std::string after = command(r, "FILES \"DHBIG.TXT\"");
    CHECK(before - std::atol(after.substr(after.rfind("\r\n", after.size() - 3) + 2).c_str()) >= 16384);
    CHECK_EQ(command(r, "KILL \"DHBIG.TXT\""), std::string());
    CHECK_EQ(command(r, "NAME \"DHA.TXT\" AS \"B:DHC.TXT\""), std::string("Rename across disks\r\n"));
    CHECK_EQ(command(r, "NAME \"A:DHA.TXT\" AS \"A:DHC.TXT\""), std::string());
    CHECK(command(r, "FILES \"DH?.TXT\"").rfind("A:\\\r\nDHC     .TXT DHB     .TXT \r\n ", 0) == 0);
    CHECK_EQ(command(r, "NAME \"DH?.TXT\" AS \"DHD.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "NAME \"DHB.TXT\" AS \"DH*.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "NAME \"DHB.TXT\" AS \"DHB.TXTX\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "KILL \"Q:DHB.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "FILES \"Q:\""), std::string("Q:\\\r\nBad file name\r\n")); // the heading first: its drive named
    CHECK_EQ(command(r, "KILL \"A:DH?.TXT\""), std::string());
    CHECK_EQ(command(r, "FILES \"DH*.*\""), std::string("A:\\\r\nFile not found\r\n"));
    CHECK(command(r, "FILES").find("DH") == std::string::npos);
}

// The owner's decision 8 (2026-11-14): FILES leaves out a file with CP/M's SYS attribute (t2', p4/giodsk.s SKPSYS), as
// MS-DOS's FCB search leaves out system files and CP/M's DIR doesn't list them; by hand. The rig's A: has its
// utilities (SET.COM, PIP.COM and the rest) as SYS files, the others not: FILES lists README.TXT and not SET.COM, and a
// pattern only SYS files match is File not found. A file made SYS by SET (function 30) leaves FILES "pattern", and
// OPEN doesn't find it either (W7, the owner's 2026-11-15).
TEST(files_leaves_out_system_files)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"DSA.TXT\":CLOSE:OPEN \"O\",#1,\"DSB.TXT\":CLOSE"), std::string());
    // The names alone: FILES's heading (A:\) and its bytes-free line (the IBM manual's) left out.
    auto names = [&](const std::string &c) {
        std::string o = command(r, c);
        if (o.rfind("A:\\\r\n", 0) == 0)
            o = o.substr(5);
        const size_t b = o.rfind(" Bytes free\r\n");
        if (b != std::string::npos && b + 13 == o.size())
            o = o.substr(0, o.rfind("\r\n", b) + 2);
        return o;
    };
    CHECK_EQ(names("FILES \"DS?.TXT\""), std::string("DSA     .TXT DSB     .TXT \r\n"));
    const std::string all = command(r, "FILES");
    CHECK(all.find("README  .TXT") != std::string::npos);
    CHECK(all.find("WRITEOO1.COM") != std::string::npos);
    CHECK(all.find("SET     .COM") == std::string::npos);
    CHECK(all.find("PIP     .COM") == std::string::npos);
    CHECK_EQ(names("FILES \"S*.COM\""), std::string("File not found\r\n"));
    CHECK_EQ(names("FILES \"*.COM\""), std::string("WRITEOO1.COM \r\n"));
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    r.cmd("set dsa.txt [sys]");
    CHECK(r.cmd("dirsys ds*.txt").find("DSA      TXT") != std::string::npos);
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    CHECK(r.to("m:gwbasic", "Ok\r\n").find("Bytes free") != std::string::npos);
    CHECK_EQ(names("FILES \"DS?.TXT\""), std::string("DSB     .TXT \r\n"));
    CHECK_EQ(names("FILES \"DSA.TXT\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"DSA.TXT\":PRINT EOF(1):CLOSE"), std::string("File not found\r\n")); // W7
    CHECK_EQ(command(r, "KILL \"DS?.TXT\""), std::string());
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
    CHECK_EQ(command(r, "RUN"), std::string("Path/file access error in 10\r\n"));
    CHECK_EQ(command(r, "CHDIR \"\\\""), std::string());
    CHECK_EQ(command(r, "CHDIR \".\":CHDIR \"\":PRINT \"root\""), std::string("root\r\n"));
    CHECK_EQ(command(r, "CHDIR \"X\""), std::string("Path not found\r\n"));
    CHECK_EQ(command(r, "CHDIR \"\\\\\""), std::string("Path not found\r\n"));
    CHECK_EQ(command(r, "MKDIR \"\\\""), std::string("Path/file access error\r\n"));
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
// closed file. r_eof: EOF of a random file. r_lof: LOF of one (the bytes: the furthest PUT's end, as PC-BASIC's). Where PC-BASIC departs from the 1983 source, or the
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
// and LOF (the bytes). LEN 1,024 (eight records each): three PUT out of order, four 255-byte strings and a
// MKI$ FIELDed in each, GET back; a record never written (zeros, EOF true); a record whose place passes 2^25 bytes
// (Bad record number past CP/M 3's 18-bit random record, the owner's answer 2; the source's Illegal function call
// past 16 bits) and the last before it (128-byte records 262,136 to 262,143); LEN past /S: (1,025: Illegal function
// call). LOF is the bytes (the IBM manual's p. 175): 1,000 for five records of 200.
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
                                            " 5  1000 \r\n"));
    // Records to CP/M 3's whole reach, 18 bits of 128-byte records, 32 MB (the owner's answer 2): of LEN 600, record
    // 55,924 ends in 128-byte record 262,143, and 55,925 would cross past it: Bad record number, not a wrap to record
    // 0; and a record past 8 MB (record 20,000 of LEN 1,024 starts at 128-byte record 159,992, R2 2) is PUT and read
    // back, with the file's size in bytes (LOF: 20,480,000), and again from the disk once it is closed and opened.
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 OPEN \"R\",#1,\"RBIG.DAT\",600:ON ERROR GOTO 90",
              "20 GET #1,55924:PRINT \"ok\":GET #1,55925:PRINT \"on\":CLOSE",
              "30 OPEN \"R\",#2,\"RFAR.DAT\",1024:FIELD #2,4 AS F$:LSET F$=\"far!\":PUT #2,20000:LSET F$=\"\"",
              "40 GET #2,20000:PRINT F$;LOF(2):GET #2,19999:PRINT ASC(F$):CLOSE",
              "50 OPEN \"R\",#2,\"RFAR.DAT\",1024:PRINT LOF(2):CLOSE:KILL \"RFAR.DAT\":END",
              "90 PRINT \"err\";ERR;ERL:RESUME NEXT"});
    CHECK_EQ(command(r, "RUN"), std::string("ok\r\nerr 63  20 \r\non\r\nfar! 20480000 \r\n 0 \r\n 20480000 \r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 OPEN \"R\",#1,\"RKB.DAT\",1024",
              "15 FIELD #1,255 AS A$,255 AS B$,255 AS C$,255 AS D$,4 AS E$",
              "20 FOR K=1 TO 3:READ N:LSET A$=STRING$(255,64+N):LSET B$=A$:LSET C$=A$:LSET D$=STRING$(255,96+N)",
              "30 LSET E$=MKI$(N)+\"ok\":PUT #1,N:NEXT:DATA 3,1,2",
              "40 FOR N=1 TO 3:GET #1,N:X$=STRING$(255,64+N)",
              "45 PRINT N;A$=X$;B$=X$;C$=X$;D$=STRING$(255,96+N);CVI(E$);RIGHT$(E$,2):NEXT",
              "50 GET #1,5:PRINT ASC(A$);ASC(E$);EOF(1);LOC(1);LOF(1)",
              "60 ON ERROR GOTO 90",
              "70 GET #1,32769:GET #1,32768:PRINT ASC(A$);EOF(1)",
              "80 CLOSE:OPEN \"R\",#1,\"RKB2.DAT\",1025:END",
              "90 PRINT \"err\";ERR;ERL:RESUME NEXT"});
    CHECK_EQ(command(r, "RUN"), std::string(" 1 -1 -1 -1 -1  1 ok\r\n 2 -1 -1 -1 -1  2 ok\r\n 3 -1 -1 -1 -1  3 ok\r\n"
                                            " 0  0 -1  5  3072 \r\nerr 63  70 \r\n 0 -1 \r\nerr 5  80 \r\n"));
    // (LOF in bytes: 3 records of 1,024, the furthest PUT's end; 32,769's place passes 32 MB, CP/M 3's reach: Bad
    // record number; 32,768's last byte is 128-byte record 262,143's.)
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

// Random files' statements in a loop are timed, for the record (the IBM spec's fix-up, P4's relief: LSET and RSET
// moved to P3, each still one far call from the home block): a program's RUN to its ENDCON, the loop with the
// statement less the loop without it, over the passes (check.py --timing's method): GET #1,n and PUT #1,n of a
// 128-byte record, LSET and RSET of a FIELDed string, CVI of one, and FIELD. Not gated: printed.
TEST(random_file_statements_in_a_loop_are_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("RUN") == 1 && lab.count("ENDCON") == 1);
    auto timed = [&](const std::vector<std::string> &lines) {
        CHECK_EQ(command(r, "NEW"), std::string());
        enter(r, lines);
        uint64_t from = 0, cycles = 0;
        bool on = false;
        r.watch = [&](uint16_t pc) {
            if (pc == lab.at("RUN") && !on)
            {
                from = r.cpu->cycles();
                on = true;
            }
            else if (on && pc == lab.at("ENDCON"))
            {
                cycles = r.cpu->cycles() - from;
                on = false;
            }
        };
        const std::string out = command(r, "RUN");
        r.watch = nullptr;
        CHECK_EQ(out, std::string());
        CHECK(cycles > 0);
        return cycles;
    };
    const std::string open = "10 OPEN \"R\",#1,\"TIMED.DAT\",128:FIELD #1,2 AS A$,126 AS B$:LSET B$=\"x\":I%=0";
    auto per = [&](const std::string &stmt, int n) {
        const std::string loop = "I%=I%+1:IF I%<" + std::to_string(n) + " THEN 20";
        const uint64_t a = timed({open, "20 " + loop, "30 CLOSE"});
        const uint64_t b = timed({open, "20 " + stmt + ":" + loop, "30 CLOSE"});
        return static_cast<double>(b > a ? b - a : 0) / n;
    };
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"TIMED.DAT\",128:FIELD #1,128 AS A$:FOR I=1 TO 4:PUT #1,I:NEXT:CLOSE"),
             std::string());
    const double put = per("PUT #1,1", 20), get = per("GET #1,2", 20), lset = per("LSET A$=\"ab\"", 50),
                 rset = per("RSET A$=\"c\"", 50), cvi = per("X%=CVI(A$)", 50), field = per("FIELD #1,2 AS A$", 50);
    std::printf("  a loop's statement, cycles each: PUT #1,1 %.0f, GET #1,2 %.0f, LSET %.0f, RSET %.0f, CVI %.0f, "
                "FIELD %.0f\n", put, get, lset, rset, cvi, field);
    CHECK(put > 0 && get > 0 && lset > 0 && rset > 0);
}

int main()
{
    return run_all();
}


// The owner's decision 5 (2026-11-14): error mode $FE at start-up (p2/gwinit.s, function 45), so a physical error is
// the BDOS's two lines and then GW-BASIC's own error (p4/giodsk.s PBDOS, by the 8086's DSKERR: Invalid Drive is Disk not
// Ready, 71), and GW-BASIC goes on; in the default mode the BDOS would end it. By hand: the BDOS's message is
// OODOS's (appendix A), GW-BASIC's from the 1983 table. H: and C: have no image in the rig. OPEN for input (15) and
// output (19, the old file deleted first), LOAD, SAVE, KILL and NAME (17, the search first), FILES; the FDB given back
// (the same number opened next), ON ERROR's ERR 71 and a statement after each error; SYSTEM back to the CCP, which
// needs nothing put back (sys/ccp.s RUNTAIL sets the default mode for each program it runs).
TEST(a_drive_with_no_image_is_a_basic_error_and_gw_basic_goes_on)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(r.phys(SCB_PHYS + 0x4B), 0xFE);
    auto bdos = [](const char *drive, int fn, const char *file) {
        return std::string("\r\nOODOS Error On ") + drive + ": Invalid Drive\r\nBDOS Function = " + std::to_string(fn) +
               " File = " + file + "\r\n";
    };
    auto said = [&](const char *drive, int fn, const char *file) { return bdos(drive, fn, file) + "Disk not ready\r\n"; };
    CHECK_EQ(command(r, "PRINT ERDEV;\"<\";ERDEV$;\">\""), std::string(" 0 <>\r\n"));
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"H:X.TXT\""), said("H", 15, "X       .TXT"));
    // ERDEV and ERDEV$ (the IBM manual's pp. 91-93; the OO-1's, the owner's answer 2): OODOS's physical error, 4
    // Invalid Drive, and the drive it was on.
    CHECK_EQ(command(r, "PRINT ERDEV;\"<\";ERDEV$;\">\""), std::string(" 4 <H:>\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"C:X.TXT\""), said("C", 19, "X       .TXT"));
    CHECK_EQ(command(r, "OPEN \"H:X.TXT\" FOR APPEND AS #2"), said("H", 15, "X       .TXT"));
    CHECK_EQ(command(r, "LOAD \"H:X\""), said("H", 15, "X       .BAS"));
    CHECK_EQ(command(r, "SAVE \"H:X\""), said("H", 19, "X       .BAS"));
    CHECK_EQ(command(r, "KILL \"H:X.TXT\""), said("H", 17, "X       .TXT"));
    CHECK_EQ(files(r, "FILES \"H:\""), said("H", 17, "????????.???"));
    CHECK_EQ(command(r, "NAME \"H:X\" AS \"H:Y\""), said("H", 17, "X       .   "));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"MD.TXT\":PRINT #1,\"kept\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"MD.TXT\":INPUT #1,A$:CLOSE:PRINT A$"), std::string("kept\r\n"));
    enter(r, {"10 ON ERROR GOTO 100", "20 OPEN \"I\",#1,\"H:X.TXT\"", "30 PRINT \"not here\"", "40 END",
              "100 PRINT \"ERR\";ERR;\"in\";ERL:RESUME 110", "110 PRINT \"went on\":ON ERROR GOTO 0", "120 OPEN \"I\",#3,\"H:Y\""});
    CHECK_EQ(command(r, "RUN"), bdos("H", 15, "X       .TXT") + "ERR 71 in 20 \r\nwent on\r\n" +
                                    bdos("H", 15, "Y       .   ") + "Disk not ready in 120\r\n");
    CHECK_EQ(command(r, "PRINT \"after\""), std::string("after\r\n"));
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
}

// The owner's decision 10 (2026-11-14): in a text file LOADed or MERGEd, a CR that follows an LF doesn't end the line
// (SCNEDT.ASM DSKCHI, p4/scnedt.s DSKCHP), so the line goes on past it, the LF kept in it (CHRGTR passes over an LF
// as over a blank). By hand, from the source: PC-BASIC ends the line at that CR, so its answer would be a Direct
// statement in file. The file is written by PRINT # (each line ended by CR LF), line 10 with an LF and a CR in it.
TEST(a_cr_after_an_lf_continues_a_loaded_line)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 OPEN \"O\",#1,\"CRLF.BAS\"", "20 PRINT #1,\"10 PRINT \"+CHR$(34)+\"A\"+CHR$(34)+\";\"+CHR$(10)+CHR$(13)+CHR$(34)+\"B\"+CHR$(34)",
              "30 PRINT #1,\"20 PRINT \"+CHR$(34)+\"C\"+CHR$(34)", "40 CLOSE"});
    CHECK_EQ(command(r, "RUN"), std::string());
    CHECK_EQ(command(r, "LOAD \"CRLF\""), std::string());
    CHECK_EQ(command(r, "RUN"), std::string("AB\r\nC\r\n"));
    const std::string list = command(r, "LIST");
    CHECK_EQ(list.substr(0, 14), std::string("10 PRINT \"A\";\n"));
    CHECK(list.find("20 PRINT \"C\"\r\n") != std::string::npos);
    CHECK(list.find("\r\"B\"") == std::string::npos);
    CHECK_EQ(command(r, "KILL \"CRLF.BAS\""), std::string());
}

// The owner's decision 9 (2026-11-14, narrowed 2026-11-15 to the source's behaviour): only a long name with a type is
// Bad file name: a name of more than 8 characters before its type, for OPEN, LOAD and SAVE (function 152 refuses
// it). A long name with no type runs on into the type, as the source's scan does (ABCDEFGHI is ABCDEFGH.I: KILL,
// NAME and FILES through FILFCB, d_files.differ). By hand: PC-BASIC cuts the name to 8.
TEST(a_name_of_more_than_8_characters_and_a_type_is_bad_file_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"ABCDEFGHI.TXT\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"ABCDEFGHI.TXT\" FOR INPUT AS #1"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "SAVE \"ABCDEFGHI.BAS\""), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "LOAD \"ABCDEFGHI.BAS\""), std::string("Bad file name\r\n"));
}

// CHAIN and COMMON (FIVEO.ASM CHAIN, COMMON, CHNRET; DSKCOM.ASM CHNENT; 7c's CHAIN step), programs that write the
// program they chain to as text by PRINT #, against PC-BASIC: COMMON's variables of each type, arrays of numbers and
// strings, a long name, a string whose text is in the program (copied to the string space first, CDVARS); the rest
// gone (c_common); ALL (c_all); a line to start at, a CHAIN from a chained program (c_line), a line that isn't
// there (c_ul); CHAIN MERGE with ALL and DELETE, an overlay in turn, the DEF types and the files kept (c_merge), and
// without ALL, COMMON's alone kept (c_merge2); the DEF types back to single and OPTION BASE kept by a CHAIN (c_types);
// a file open across it (c_files); a missing file (c_miss); COMMON's and CHAIN's errors (c_e2-c_e7: a Syntax error
// last in its GW-BASIC, whose next line is that line's edit). Then a typed session (c_tok): CHAIN of a tokenised and an
// ASCII SAVE, typed at the prompt, with a line and ALL. Where PC-BASIC departs from the 1983 source, NAME.differ.
TEST(chain_and_common_print_what_pc_basic_prints)
{
    programs({"c_common", "c_all", "c_line", "c_ul", "c_merge", "c_merge2", "c_types", "c_files", "c_miss"});
    programs({"c_e3", "c_e5", "c_e6", "c_e7", "c_e2"});
    programs({"c_e4"});
    sessions({"c_tok"});
}

// LIST and LLIST to a file (GWLIST.ASM LIST's ,"name": NAMSCN and NULOPM, file 0 opened for output; READY closes
// it), a range of the program, read back by LINE INPUT # and LOADed as text again, a missing name (Missing operand)
// and a drive past P: (Bad file name), as a typed session. Where PC-BASIC departs from the source (LLIST's file:
// the source's LLIST goes on into LIST), c_list.differ.
TEST(list_to_a_file_prints_what_pc_basic_prints)
{
    sessions({"c_list"});
}

// By hand, from the source, what PC-BASIC can't give: CHAIN MERGE of a tokenised file is Bad file mode (OKGET2;
// PC-BASIC fails); VARPTR(#n) (GIO86.ASM GETPTR) is the address of file n's FDB's F_MODE (OO-1: P4's FDB table,
// p4.lbl's FDB0, FDBLEN = 184 bytes apart; F_MODE at 1), as an integer, so negative in $C800-$F7FF; Bad file number
// for a file not open or past NMFILE.
TEST(chain_merge_of_a_tokenised_file_and_varptr_of_a_file_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> p4 = link_labels("p4.lbl");
    REQUIRE(p4.count("FDB0") == 1);
    enter(r, {"10 PRINT \"TOKENISED\""});
    CHECK_EQ(command(r, "SAVE \"CMT\""), std::string());
    CHECK_EQ(command(r, "A=1:CHAIN MERGE \"CMT\""), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "CHAIN \"CMT\""), std::string("TOKENISED\r\n"));
    auto varptr = [&](int n) { // PRINT's form of the integer: its sign, or a space
        const int a = p4.at("FDB0") + n * 184 + 1, s = a >= 32768 ? a - 65536 : a;
        return (s < 0 ? "" : " ") + std::to_string(s) + " \r\n";
    };
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"VP1.TXT\":OPEN \"O\",#3,\"VP3.TXT\""), std::string());
    CHECK_EQ(command(r, "PRINT VARPTR(#1)"), varptr(1));
    CHECK_EQ(command(r, "PRINT VARPTR(#3)"), varptr(3));
    CHECK_EQ(command(r, "PRINT VARPTR(#2)"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "PRINT VARPTR(#4)"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "CLOSE:PRINT VARPTR(#1)"), std::string("Bad file number\r\n"));
}

namespace
{
    /// What a BDOS function (5 L_WRITE: LST:; 4 A_WRITE: AUX:) is sent while `line` is typed and run to its Ok,
    /// read at the BDOS's entry as test_gwbasic_core's lst_of() reads LST:.
    std::string sent_by(CcpRig &r, uint8_t function, const std::string &line)
    {
        std::string got;
        r.watch = [&](uint16_t pc) {
            const Registers g = r.cpu->regs();
            if (pc == 0x0203 && g.y == function)
                got += static_cast<char>(g.a);
        };
        command(r, line);
        r.watch = nullptr;
        return got;
    }
} // namespace

// LPT1: as a file (GIOLPT.ASM's LPTOPN, LPTSOT, LPOUT1; the 7c devices step), by hand from the source (PC-BASIC's
// printer isn't the oracle's): to LST: by function 5, through LPT1's own DCB, which LPRINT shares (its column, its
// CR flag), by the rules 7a's LPRINT keeps (T4, T5): a CR followed by an LF, an LF straight after a CR dropped, a
// TAB moving no column, a CR LF before a printable byte at the width (80). So each PRINT #1 below sends what the
// same LPRINT sends. BIN sends the bytes as they are; a random OPEN writes as an output one; LIST to "LPT1:" is
// LLIST's text. LPOS(n) is LPT1's column plus 1 for n 0 and 1, and 1 for 2 and 3, LPT2: and LPT3:, which have no
// driver here; past 3 Illegal function call (the IBM manual's p. 178; the IBM spec's fix-up, C-1 item 17 and its Q8
// default: the source's (n - 1) / 2 read LPT1's flags byte for 2 and LPT2's width past it). The errors: input or
// append (Bad file mode), an option but BIN, LPT9: (Bad file name), LPT2: and LPT3: (Device unavailable), EOF and LOC
// of a printer (Bad file mode), and INPUT # from one (Input past end: read from a file opened for output, A-11).
TEST(lpt1_files_go_to_lst_as_lprint_does_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    auto lst = [&](const std::string &line) { return vis(sent_by(r, 5, line)); };
    const std::string open = "OPEN \"LPT1:\" FOR OUTPUT AS #1:PRINT #1,";
    CHECK_EQ(lst(open + "\"A\";CHR$(13);CHR$(10);\"B\":CLOSE"), vis("A\r\nB\r\n"));
    CHECK_EQ(lst(open + "\"A\";CHR$(13);\"B\":CLOSE"), vis("A\r\nB\r\n"));
    CHECK_EQ(lst(open + "STRING$(85,\"x\"):CLOSE"), vis(std::string(80, 'x') + "\r\n" + std::string(5, 'x') + "\r\n"));
    CHECK_EQ(lst(open + "CHR$(9);\"A\";TAB(20);\"B\":CLOSE"), vis("\tA" + std::string(18, ' ') + "B\r\n"));
    for (const char *items : {"CHR$(9);\"A\";TAB(20);\"B\"", "STRING$(85,\"x\")", "1,2,\"abc\";-3", "SPC(5);\"Q\";CHR$(8);\"R\"",
                              "STRING$(70,\"y\");12345"})
        CHECK_EQ(lst(open + items + ":CLOSE"), lst(std::string("LPRINT ") + items));
    CHECK_EQ(lst("OPEN \"LPT1:BIN\" FOR OUTPUT AS #1:PRINT #1,\"A\";CHR$(13);\"B\";:CLOSE"), vis("A\rB"));
    CHECK_EQ(lst("OPEN \"lpt1:bin\" AS #1:PRINT #1,\"C\";CHR$(10);:CLOSE"), vis("C\n"));
    CHECK_EQ(lst("OPEN \"LPT1:\" AS #1:PRINT #1,\"R\":CLOSE"), vis("R\r\n"));
    CHECK_EQ(lst("OPEN \"O\",#2,\"LPT1:\":OPEN \"O\",#3,\"LPT1:\":PRINT #2,\"2\";:PRINT #3,\"3\":CLOSE"), vis("23\r\n"));
    enter(r, {"10 PRINT 1", "20 REM two"});
    CHECK_EQ(lst("LIST ,\"LPT1:\""), lst("LLIST"));
    CHECK_EQ(lst("LIST ,\"LPT1:\""), vis("10 PRINT 1\r\n20 REM two\r\n"));
    CHECK_EQ(command(r, "LPRINT \"AB\";:PRINT LPOS(0);LPOS(1);LPOS(2);LPOS(3)"), std::string(" 3  3  1  1 \r\n"));
    CHECK_EQ(command(r, open + "\"ABCD\";:PRINT LPOS(1):CLOSE"), std::string(" 5 \r\n"));
    CHECK_EQ(command(r, "LPRINT CHR$(13);:PRINT LPOS(0);LPOS(1)"), std::string(" 1  1 \r\n"));
    CHECK_EQ(command(r, "PRINT LPOS(4)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT LPOS(255)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT1:\" FOR INPUT AS #1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT1:\" FOR APPEND AS #1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT1:X\" FOR OUTPUT AS #1"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT1:BINX\" FOR OUTPUT AS #1"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT9:\" FOR OUTPUT AS #1"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT2:\" FOR OUTPUT AS #1"), std::string("Device unavailable\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT3:\" FOR OUTPUT AS #1"), std::string("Device unavailable\r\n"));
    CHECK_EQ(command(r, "OPEN \"LPT1:\" FOR OUTPUT AS #1:PRINT EOF(1)"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "PRINT LOC(1)"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "INPUT #1,A"), std::string("Input past end\r\n")); // read for output (A-11)
    CHECK_EQ(command(r, "CLOSE:PRINT 1"), std::string(" 1 \r\n"));
}

// WIDTH's file and device forms (GIO86.ASM's WIDTHS; 7d2, the owner's answer 6: in console mode too), by hand from
// the source: WIDTH #f,n sets an open file's width (F_WID): a printer's file breaks its line there (CRIFEL), and a
// disk file's width moves PRINT #'s comma zones (PTRWID: at 20 the last zone is column 0, so a comma ends the line;
// the source breaks no disk file's line at its width, so neither does the port); WIDTH LPRINT n sets
// LPT1's (the resident LINLEN+1, shared with LPRINT), and WIDTH "LPT1:",n the same by the device's set-width function
// (P4's DEVSWD: LPTSWD). The errors, the source's: a file not open (Bad file number), a width of 0 (Illegal function
// call), CONS: (Bad file mode), a disk's name (Bad file name). WIDTH n and WIDTH "SCRN:" are the screen's, Advanced
// Feature in console mode, as 7d1 left them.
TEST(width_of_a_file_lprint_and_a_device)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::string back = "OPEN \"I\",#1,\"W.T\":WHILE NOT EOF(1):LINE INPUT #1,A$:PRINT LEN(A$);:WEND:CLOSE";
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W.T\":PRINT #1,\"A\",\"B\":CLOSE"), std::string());
    CHECK_EQ(command(r, back), std::string(" 15 \r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W.T\":WIDTH #1,20:PRINT #1,\"A\",\"B\":CLOSE"), std::string());
    CHECK_EQ(command(r, back), std::string(" 1  1 \r\n"));
    auto lst = [&](const std::string &line) { return vis(sent_by(r, 5, line)); };
    CHECK_EQ(lst("OPEN \"LPT1:\" FOR OUTPUT AS #1:WIDTH #1,10:PRINT #1,STRING$(25,\"A\"):CLOSE"),
             vis(std::string(10, 'A') + "\r\n" + std::string(10, 'A') + "\r\n" + std::string(5, 'A') + "\r\n"));
    CHECK_EQ(lst("WIDTH LPRINT 20:LPRINT STRING$(30,\"B\")"),
             vis(std::string(20, 'B') + "\r\n" + std::string(10, 'B') + "\r\n"));
    CHECK_EQ(lst("WIDTH \"LPT1:\",15:LPRINT STRING$(20,\"C\")"),
             vis(std::string(15, 'C') + "\r\n" + std::string(5, 'C') + "\r\n"));
    CHECK_EQ(lst("WIDTH \"lpt1:\",80:LPRINT STRING$(20,\"D\")"), vis(std::string(20, 'D') + "\r\n"));
    CHECK_EQ(command(r, "WIDTH #3,10"), std::string("Bad file number\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W.T\":WIDTH #1,0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "CLOSE:WIDTH LPRINT 0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "WIDTH \"CONS:\",40"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "WIDTH \"A:\",40"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "WIDTH \"LPT1:\""), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "WIDTH 80"), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, "WIDTH \"SCRN:\",40"), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, "PRINT 1"), std::string(" 1 \r\n"));
}

// WIDTH "SCRN:",n and WIDTH "KYBD:",n in console mode change nothing (7d2's review round 1, F2): Advanced Feature,
// as before 7d2, and SCRN:'s width stays the console's 80, so a SCRN: file's 60 characters are still one line; the
// parse's errors come first, as the others' (a width of 0 Illegal function call, no width Syntax error); a disk's
// name is still Bad file name. Guards a refused statement leaving a changed width behind. Seen failing with the home
// block's refusal (SCNDEV) left out: P4's SCNSWD set the width to 40 before P6 refused it, and the line broke at 40.
TEST(width_of_the_screen_s_devices_changes_nothing_in_console_mode)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::string sixty = "OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,STRING$(60,\"A\"):CLOSE";
    CHECK_EQ(command(r, sixty), std::string(60, 'A') + "\r\n");
    for (const char *w : {"WIDTH \"SCRN:\",40", "WIDTH \"KYBD:\",40", "WIDTH \"scrn:\",20", "WIDTH \"kybd:XY\",30"})
        CHECK_EQ(command(r, w), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, sixty), std::string(60, 'A') + "\r\n");
    CHECK_EQ(command(r, "WIDTH \"SCRN:\",0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "WIDTH \"KYBD:\""), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "WIDTH \"A:BN:\",40"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "WIDTH \"LPT1:\",80"), std::string());
}

// COM1: as a file (GIOCOM.ASM's COMOPN, PCOMOP, COMSIN, COMSOT, COMEOF, COMLOC, COMLOF, COMCLS), by hand from the
// source (PC-BASIC's port isn't the oracle's), on AUX: (OO-1, decision 12: polled; the OEM's routines, p4/oem.s):
// a byte sent goes by function 4 (watched at the BDOS's entry), one received is read by function 3 when function 7
// says it waits. The test makes TTY AUX:'s alone while the program runs (the SCB's CONIN: vector CRT only, as DEVICE
// CONIN:=CRT does; RUN typed on the USB keyboard), so GW-BASIC's look at the keys (POLKEY, which COMSIN calls while
// it waits) can't take the line's bytes, which come by the console's key(). The options: the speed, parity, data
// and stop bits parsed, checked and kept in the DCB (CM1DCB, read while P4 is in W3) as PCOMOP and INICOM give them,
// and not set (the owner, 2026-11-14: the line stays 19,200); each wrong one Bad file name; COM2: Device
// Unavailable; COM1: one file at a time. A file is binary unless ASC is given: no ^Z at its CLOSE, and EOF true
// while no byte waits; but its CR LF and width rules still apply (COMSOT: BIN not given), with no LF after a CR
// unless LF is given. ASCII: a ^Z at CLOSE, and a ^Z received is its end.
TEST(com1_files_go_to_and_from_aux_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> p4 = link_labels("p4.lbl");
    REQUIRE(p4.count("CMBAUD") == 1 && p4.count("CMSTOPB") == 1);
    for (const char *bad : {"COM1:1234", "COM1:0", "COM1:123456", "COM1:300,X", "COM1:300,N,9", "COM1:300,N,3",
                            "COM1:300,N,8,3", "COM1:300,N,8,1.6", "COM1:300,N,8,1,XY", "COM1:,,,,ASC,BIN", "COM1:,,,,LX",
                            "COM1:300 N", "COM1:,,,,BIX"})
        CHECK_EQ(command(r, std::string("OPEN \"") + bad + "\" AS #1"), std::string("Bad file name\r\n"));
    CHECK_EQ(command(r, "OPEN \"COM2:\" AS #1"), std::string("Device unavailable\r\n"));
    CHECK_EQ(command(r, "OPEN \"COM1:\" AS #1:OPEN \"COM1:\" AS #2"), std::string("File already open\r\n"));
    CHECK_EQ(command(r, "CLOSE"), std::string());
    // The DCB as PCOMOP leaves it, read at the file's first byte out (W3 then P4's).
    struct Dcb
    {
        unsigned baud, data, parity, stop;
    } dcb{};
    auto peek = [&](const char *l, unsigned k = 0) { return r.machine->bus().peek(static_cast<uint16_t>(p4.at(l) + k)); };
    auto aux = [&](const std::string &line) {
        std::string got;
        dcb = {};
        r.watch = [&](uint16_t pc) {
            const Registers g = r.cpu->regs();
            if (pc != 0x0203 || g.y != 4)
                return;
            if (got.empty())
                dcb = {static_cast<unsigned>(peek("CMBAUD") | peek("CMBAUD", 1) << 8), peek("CMBYTSZ"), peek("CMPARIT"),
                       peek("CMSTOPB")};
            got += static_cast<char>(g.a);
        };
        const std::string printed = command(r, line);
        r.watch = nullptr;
        CHECK_EQ(vis(printed), vis(got)); // AUX: is TTY, the serial line the rig reads the console's output on
        return vis(got);
    };
    CHECK_EQ(aux("OPEN \"COM1:\" AS #1:PRINT #1,\"HI\";CHR$(13);CHR$(10);\"X\":CLOSE"), vis("HI\rX\r"));
    CHECK(dcb.baud == 300 && dcb.data == 7 && dcb.parity == 2 && dcb.stop == 0); // the defaults; 300: 1 stop bit
    CHECK_EQ(aux("OPEN \"COM1:9600,N,8,1.5,LF,ASC\" AS #1:PRINT #1,\"A\":CLOSE"), vis("A\r\n\x1a"));
    CHECK(dcb.baud == 9600 && dcb.data == 8 && dcb.parity == 0 && dcb.stop == 1);
    CHECK_EQ(aux("OPEN \"COM1:110,O,5\" FOR OUTPUT AS #1:PRINT #1,\"B\":CLOSE"), vis("B\r"));
    CHECK(dcb.baud == 110 && dcb.data == 5 && dcb.parity == 1 && dcb.stop == 1); // 110 and 5 bits: 1.5
    CHECK_EQ(aux("OPEN \"COM1:75,M,6\" AS #1:PRINT #1,\"C\";:CLOSE"), vis("C"));
    CHECK(dcb.baud == 75 && dcb.data == 6 && dcb.parity == 3 && dcb.stop == 2); // 75 and 6 bits: 2
    CHECK_EQ(aux("OPEN \"COM1:19200,S,8,2,RS,CS,DS2,CD5,BIN\" AS #1:PRINT #1,\"D\";CHR$(13);:CLOSE"), vis("D\r"));
    CHECK(dcb.baud == 19200 && dcb.data == 8 && dcb.parity == 4 && dcb.stop == 2);
    CHECK_EQ(aux("OPEN \"COM1:1 2 0 0,,7\" AS #1:PRINT #1,STRING$(3,\"e\"):CLOSE"), vis("eee\r"));
    CHECK(dcb.baud == 1200 && dcb.data == 7 && dcb.parity == 2);
    // Input: a program run with CONIN: the USB keyboard's alone, the line's bytes fed as the console's keys.
    enter(r, {"NEW", "10 OPEN \"COM1:300,N,8,1\" AS #1", "20 PRINT #1,\"HI\"", "30 PRINT \"a\";:A$=INPUT$(1,#1)",
              "40 PRINT \"<\";A$;\">\";LOC(1);LOF(1)", "50 PRINT \"b\";:LINE INPUT #1,B$:PRINT \"[\";B$;\"]\";LOC(1);INPUT$(1,#1);EOF(1)",
              "55 CLOSE:OPEN \"COM1:,,,,ASC\" AS #2:PRINT \"c\";:PRINT EOF(2);EOF(2):CLOSE"});
    std::string sent;
    r.watch = [&](uint16_t pc) {
        const Registers g = r.cpu->regs();
        if (pc == 0x0203 && g.y == 4)
            sent += static_cast<char>(g.a);
    };
    r.scb(0x22, 0x00); // CONIN: CRT alone
    r.scb(0x23, 0x80);
    const size_t from = r.out.size();
    r.type_usb("RUN\r");
    CHECK(r.until("a", ms(4000), from + 3)); // each byte sent once the program waits for it
    r.type("Q");
    CHECK(r.until("b", ms(4000), from + 3));
    r.type("hello\rx"); // the byte after the CR read and backed up (the console sends no LF: it makes one a CR)
    CHECK(r.until("c", ms(4000), from + 3));
    r.type("\x1a");
    CHECK(r.until("Ok\r\n", ms(8000), from));
    r.watch = nullptr;
    r.scb(0x23, 0xC0); // CONIN: CRT and TTY again, from the next line typed (function 10 reads the vector at its start)
    r.type_usb("\r");
    CHECK_EQ(vis(sent), vis("HI\r\x1a"));
    const std::string ran = r.out.substr(from);
    const std::string want = "a<Q> 0  1 \r\nb[hello] 1 x-1 \r\nc-1 -1 \r\n\x1aOk\r\n"; // (AUX:'s ^Z on the line too)
    CHECK(ran.find(want) != std::string::npos);
    if (ran.find(want) == std::string::npos)
        std::printf("  ran: '%s'\n", vis(ran).c_str());
    CHECK_EQ(command(r, "PRINT 2"), std::string(" 2 \r\n"));
}

// The console's devices as files (GIOSCN.ASM, GIOKYB.ASM, GIOCON.ASM; the devices step), by hand from the source:
// SCRN: through BASIC's own console output, a CR as CR LF and an LF straight after a CR dropped, its width the
// console's (80: a CR LF before the 81st column); CONS: raw, by function 6, no CR LF of its own; KYBD: the keys
// typed (INPUT$, EOF's ^Z), the keyboard's queue for LOC and LOF (32, one kept empty: 31: W11). Each for its one way
// only (Bad file mode).
TEST(scrn_kybd_and_cons_files_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,\"AB\";CHR$(13);CHR$(10);\"C\":CLOSE"),
             std::string("AB\r\nC\r\n"));
    CHECK_EQ(command(r, "OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,\"AB\";CHR$(13);\"C\":CLOSE"), std::string("AB\r\nC\r\n"));
    CHECK_EQ(command(r, "OPEN \"scrn:\" FOR OUTPUT AS #1:PRINT #1,STRING$(81,\"s\"):CLOSE"),
             std::string(80, 's') + "\r\ns\r\n");
    CHECK_EQ(command(r, "OPEN \"SCRN:\" FOR OUTPUT AS #1:PRINT #1,1,2,\"z\":CLOSE"), command(r, "PRINT 1,2,\"z\""));
    CHECK_EQ(command(r, "OPEN \"CONS:\" FOR OUTPUT AS #1:PRINT #1,\"AB\";CHR$(10);\"C\":CLOSE"), std::string("AB\nC\r\n"));
    CHECK_EQ(command(r, "OPEN \"SCRN:\" FOR INPUT AS #1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "OPEN \"CONS:\" AS #1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "OPEN \"KYBD:\" FOR OUTPUT AS #1"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "OPEN \"KYBD:\" AS #1:PRINT LOC(1);LOF(1):CLOSE"), std::string(" 0  31 \r\n"));
    size_t from = r.out.size();
    r.type("OPEN \"KYBD:\" FOR INPUT AS #1:A$=INPUT$(3,#1):PRINT \"<\";A$;\">\":CLOSE\r");
    r.type("xyz");
    CHECK(r.until("Ok\r\n", ms(4000), from + 10));
    CHECK(r.out.find("<xyz>\r\n", from) != std::string::npos);
    from = r.out.size();
    r.type("OPEN \"KYBD:\" FOR INPUT AS #2:PRINT EOF(2);EOF(2):CLOSE\r");
    r.type("\x1a");
    CHECK(r.until("Ok\r\n", ms(4000), from + 10));
    CHECK(r.out.find("-1 -1 \r\n", from) != std::string::npos);
}

// The COM statement (GWSTS.ASM COMS: SEVSTT for COM(1) and COM(2)) and G10 (the owner's, 2026-11-14): an event id
// past TRPTBL's range is Illegal function call, never a write past it: COM(200) passes the source's signed JGE (199
// is negative) and would set the flag of event 214, 642 bytes into P3's RAM; with the guard, no byte past TRPTBL's
// 63 changes (read at COMSP, P3 in W3). ON COM(n) GOSUB takes its line (its trap from the port is 7f's). Against
// PC-BASIC where it can tell (d_com: COM(n)'s errors, COM2: Device Unavailable); by hand, LCOPY, the source's LCOPYS
// parse and then the OEM's LCPY, Advanced Feature until the screen (7d); IOCTL, the source's Advanced Feature
// (GWSTS.ASM's ERDEV and IOCTL are ADVERR); and ERDEV and ERDEV$, which the 1983 table has no words for, are the IBM
// manual's read-only variables (p. 91; the IBM spec's fix-up, C-1 item 18): crunched to $FE $95 and $FE $A2 and
// listed back, 0 and "" (no device error is recorded yet: a default), and ERDEV or ERDEV$ as a statement, an
// assignment to it included, Syntax error. Seen failing with ETABX's words left out (ERDEV a variable) and EVALX's
// ERDEV branch left out (Syntax error in a formula).
TEST(the_com_statement_lcopy_erdev_and_ioctl)
{
    programs({"d_com"});
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> p3 = link_labels("p3.lbl");
    REQUIRE(p3.count("TRPTBL") == 1 && p3.count("COMSP") == 1);
    const uint16_t past = static_cast<uint16_t>(p3.at("TRPTBL") + 3 * 21), end = 0xF800; // NUMTRP 21
    std::vector<std::vector<uint8_t>> seen;
    r.watch = [&](uint16_t pc) {
        if (pc != p3.at("COMSP"))
            return;
        std::vector<uint8_t> b;
        for (uint16_t a = past; a < end; ++a)
            b.push_back(r.machine->bus().peek(a));
        seen.push_back(b);
    };
    CHECK_EQ(command(r, "COM(1) ON"), std::string());
    CHECK_EQ(command(r, "COM(200) ON"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "COM(129) STOP"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "COM(0) OFF"), std::string());
    r.watch = nullptr;
    REQUIRE(seen.size() == 4);
    CHECK(seen[1] == seen[0] && seen[2] == seen[0] && seen[3] == seen[0]);
    CHECK_EQ(command(r, "LCOPY"), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, "LCOPY 1"), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, "LCOPY 256"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "LCOPY 1,2"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "IOCTL #1,\"X\""), std::string("Advanced feature\r\n"));
    CHECK_EQ(command(r, "ERDEV"), std::string("Syntax error\r\n")); // a read-only variable: no statement
    CHECK_EQ(command(r, "ERDEV=1"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "ERDEV$=\"A\""), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "PRINT ERDEV;LEN(ERDEV$);\"<\";ERDEV$;\">\""), std::string(" 0  0 <>\r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    enter(r, {"10 X=ERDEV+1:Y$=ERDEV$"});
    CHECK_EQ(command(r, "LIST"), std::string("10 X=ERDEV+1:Y$=ERDEV$\r\n"));
    CHECK_EQ(command(r, "RUN"), std::string());
    CHECK_EQ(command(r, "PRINT X;LEN(Y$)"), std::string(" 1  0 \r\n"));
    const uint16_t text = static_cast<uint16_t>(r.phys(0x79) | r.phys(0x7A) << 8); // TXTTAB (gwbasic.inc)
    std::vector<uint8_t> tok;
    for (uint16_t i = 4; i < 4 + 16; ++i)
        tok.push_back(r.phys(static_cast<uint16_t>(text + i)));
    auto has = [&](uint8_t b) {
        for (size_t i = 0; i + 1 < tok.size(); ++i)
            if (tok[i] == 0xFE && tok[i + 1] == b)
                return true;
        return false;
    };
    CHECK(has(0x95)); // ERDEV, the source's STMDSX token
    CHECK(has(0xA2)); // ERDEV$, the OO-1's
}

// The 7c fidelity review's findings (the owner, 2026-11-15: W1-W12, "recommendations as listed"), each by hand
// from the source, each failing on a build without its fix.

// W1: LOC and LOF inside a PRINT # to the same file see every byte printed before them (p4/gio86.s FILFNP sends
// OUTDO's run first, FLUSHR), as the 8086's OUTDO writes each byte at once: the 129th byte writes record 0, so LOC(1)
// is 1 and LOF(1) 128. Without the flush, bytes 129-130 still wait in RUNBUF and nothing has been written.
TEST(w1_loc_and_lof_inside_a_print_to_the_file_count_every_byte)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W1.TXT\":PRINT #1,STRING$(130,\"A\");LOC(1);LOF(1):CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"W1.TXT\":LINE INPUT #1,A$:PRINT LEN(A$);RIGHT$(A$,10):CLOSE"),
             std::string(" 138 AA 1  128 \r\n"));
}

// W2 (Major): CHKFOP (p4/giodsk.s) compares only disk files' FDBs. A device's file (LPT1:) in a table slot that once
// held a disk file keeps that file's FCB bytes (the slots are fixed, OO-1); the 8086's device FDB is fresh memory,
// so the name opens again: OUTPUT and APPEND of it are not File already open.
TEST(w2_a_device_s_file_is_never_taken_for_a_disk_file_s_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W2.TXT\":PRINT #1,\"x\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"W2.TXT\":CLOSE #1:OPEN \"LPT1:\" FOR OUTPUT AS #1"), std::string());
    CHECK_EQ(command(r, "OPEN \"O\",#2,\"W2.TXT\":PRINT #2,\"y\":CLOSE #2"), std::string());
    CHECK_EQ(command(r, "OPEN \"A\",#2,\"W2.TXT\":PRINT #2,\"z\":CLOSE #2"), std::string());
    CHECK_EQ(command(r, "CLOSE:OPEN \"I\",#3,\"W2.TXT\":INPUT #3,A$,B$:PRINT A$;B$:CLOSE"), std::string("yz\r\n"));
}

// W3: CHKFOP compares names without CP/M's attribute bits (F_OPEN leaves them in the FCB): a file with ARCHIVE set
// (by SET, at the CCP) open for input is File already open for output, as the 8086's; else the OUTPUT would delete
// it under the open file.
TEST(w3_a_file_with_attributes_open_is_file_already_open)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W3.TXT\":PRINT #1,\"kept\":CLOSE"), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    r.cmd("set w3.txt [archive=on]");
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    CHECK(r.to("m:gwbasic", "Ok\r\n").find("Bytes free") != std::string::npos);
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"W3.TXT\":OPEN \"O\",#2,\"W3.TXT\""), std::string("File already open\r\n"));
    CHECK_EQ(command(r, "LINE INPUT #1,A$:PRINT A$:CLOSE"), std::string("kept\r\n"));
}

// W4 (Major): RUN of a text file whose last line ends in a bare LF runs at the file's end (p4/scnedt.s DSKCHP tests
// LDRUN before the CR/LF rule), as the 8086's NOTCHN jumps to RUNC. Without it the CR that ends the load follows the
// LF, is dropped, and the load waits on the keyboard. The unfinished line goes in first, as LOAD's does (the IBM
// manual's p. 306; the IBM spec's fix-up, C-1 item 15: the source dropped it), so it runs too. CHAIN's end the same,
// but the unfinished line is still dropped (the source's: item 15 is RUN's).
TEST(w4_run_of_a_file_ending_in_a_bare_lf_runs)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"LF.BAS\":PRINT #1,\"10 PRINT 1\""), std::string());
    CHECK_EQ(command(r, "PRINT #1,\"20 PRINT 2\";CHR$(10);:CLOSE"), std::string());
    CHECK_EQ(command(r, "RUN \"LF\""), std::string(" 1 \r\n 2 \r\n"));
    CHECK_EQ(command(r, "CHAIN \"LF\""), std::string(" 1 \r\n"));
    CHECK_EQ(command(r, "LIST"), std::string("10 PRINT 1\r\n"));
}

// W5: KILL, NAME and FILES (FILFCB) cut the type that a typeless name of 12 or more characters runs into to 3, as
// MS-DOS's parse does, so they find the file OPEN made of the same name (NAMSCN: ABCDEFGH.IJK).
TEST(w5_a_long_name_with_no_type_is_cut_as_open_cuts_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"ABCDEFGHIJKL\":CLOSE"), std::string());
    CHECK_EQ(files(r, "FILES \"ABCDEFGHIJKL\""), std::string("ABCDEFGH.IJK \r\n"));
    CHECK_EQ(command(r, "NAME \"ABCDEFGHIJKLMN\" AS \"W5.TXT\""), std::string());
    CHECK_EQ(command(r, "NAME \"W5.TXT\" AS \"ABCDEFGHIJKLM\""), std::string());
    CHECK_EQ(command(r, "KILL \"ABCDEFGHIJKL\""), std::string());
    CHECK_EQ(files(r, "FILES \"ABCDEFGH.*\""), std::string("File not found\r\n"));
}

// W6: in KILL, NAME and FILES, what follows a "*" in its field is passed over, as MS-DOS's parse does ("*1.BAS" is
// "*.BAS", "AB*CD.TXT" "AB*.TXT", ".B*X" ".B*"), where function 152 refuses it.
TEST(w6_characters_after_a_star_are_passed_over)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W6A.BAS\":CLOSE:OPEN \"O\",#1,\"W6B.BAS\":CLOSE"), std::string());
    const std::string both = "W6A     .BAS W6B     .BAS \r\n";
    CHECK_EQ(files(r, "FILES \"W6*.BAS\""), both);
    CHECK_EQ(files(r, "FILES \"W6*1.BAS\""), both);
    CHECK_EQ(files(r, "FILES \"W6?.B*X\""), both);
    CHECK_EQ(command(r, "KILL \"W6*CD.BAS\""), std::string());
    CHECK_EQ(files(r, "FILES \"W6*.*\""), std::string("File not found\r\n"));
}

// W7 (Major): KILL and NAME pass over a SYS file, as FILES does (SKPSYS, the owner's decision 8) and MS-DOS's FCB
// search does: in user 0 on A:, the card's utilities are SYS files. KILL of a pattern deletes the other files it
// matches, each by its own name, and leaves the SYS ones; a SYS file named alone is File not found.
TEST(w7_kill_and_name_pass_over_system_files)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W7A.COM\":CLOSE:OPEN \"O\",#1,\"W7B.COM\":CLOSE"), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    r.cmd("set w7b.com [sys]");
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    CHECK(r.to("m:gwbasic", "Ok\r\n").find("Bytes free") != std::string::npos);
    CHECK_EQ(command(r, "KILL \"S*.COM\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "KILL \"PIP.COM\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "NAME \"PIP.COM\" AS \"W7X.COM\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "NAME \"W7B.COM\" AS \"W7X.COM\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "KILL \"W7?.COM\""), std::string());
    CHECK_EQ(files(r, "FILES \"W7?.COM\""), std::string("File not found\r\n"));
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string dir = r.cmd("dirsys *.com");
    for (const char *kept : {"SET      COM", "PIP      COM", "W7B      COM"})
        CHECK(dir.find(kept) != std::string::npos);
    CHECK(dir.find("W7X") == std::string::npos);
    CHECK(r.cmd("dir w7a.com").find("W7A") == std::string::npos);
}

// W8: "+", "/", '"' and a control character end a name in KILL, NAME and FILES, as in MS-DOS's parse (function 152
// takes the first three as name characters and refuses the last): KILL "A+B.TXT" is KILL "A", which isn't there.
TEST(w8_plus_slash_quote_and_control_characters_end_a_name)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"A+B.TXT\":CLOSE"), std::string());
    CHECK_EQ(command(r, "KILL \"A+B.TXT\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "KILL \"A/B.TXT\""), std::string("File not found\r\n"));
    CHECK_EQ(files(r, "FILES \"A+B.TXT\""), std::string("File not found\r\n"));
    CHECK_EQ(files(r, "FILES \"A?B.TXT\""), std::string("A+B     .TXT \r\n"));
    CHECK_EQ(command(r, "KILL \"X\"+CHR$(1)"), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W8\":CLOSE:KILL \"W8\"+CHR$(34)+\"X\""), std::string());
    CHECK_EQ(files(r, "FILES \"W8\""), std::string("File not found\r\n"));
}

// W9: LLIST (and LIST to a file or device) sends a CR after a line feed inside a program line (GWLIST.ASM LISPRT
// by SCNEDT.ASM's OUTCH1), so the printer starts the rest at its left margin: LF, CR, and the CR's LF.
TEST(w9_llist_sends_a_cr_after_a_line_feed_in_a_line)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",1,\"W9.BAS\":PRINT #1,\"10 PRINT 1:\"+CHR$(10)+\"PRINT 2\":CLOSE"), std::string());
    CHECK_EQ(command(r, "LOAD \"W9\""), std::string());
    CHECK_EQ(vis(sent_by(r, 5, "LLIST")), vis("10 PRINT 1:\n\r\nPRINT 2\r\n"));
    CHECK_EQ(command(r, "LIST"), std::string("10 PRINT 1:\nPRINT 2\r\n")); // the console: none
}

// W10: the console's output passes SCRN:'s LSTCHR (res/oem.s OUTDO), as the 8086's console output passes SCNOL1: an
// LF to SCRN: after console printing isn't the LF straight after a CR that SCNOL1 drops.
TEST(w10_console_output_moves_scrn_s_last_character)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"SCRN:\" FOR OUTPUT AS 1"), std::string());
    CHECK_EQ(command(r, "PRINT #1,\"A\";CHR$(13);:PRINT \"X\";:PRINT #1,CHR$(10);\"B\""),
             std::string("A\r\nX\nB\r\n"));
    CHECK_EQ(command(r, "PRINT #1,\"A\";CHR$(13);CHR$(10);\"B\":CLOSE"), std::string("A\r\nB\r\n"));
}

// W12: a break (^C) sends its new line to the screen (KYBINI's FINPRT, GIOKYB.ASM's, first), not to the file LIST
// writes: the screen shows it before Ok, and the file ends with the last whole line listed.
TEST(w12_a_break_s_new_line_goes_to_the_screen)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    std::vector<std::string> lines;
    for (int n = 1; n <= 300; ++n)
        lines.push_back(std::to_string(n) + " REM a line to list, one of three hundred");
    enter(r, lines);
    CHECK_EQ(command(r, "PRINT 5"), std::string(" 5 \r\n"));
    unsigned records = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == 0x0203 && r.cpu->regs().y == 34)
            ++records;
    };
    const size_t from = r.out.size();
    r.type("LIST ,\"W12.TXT\"\r");
    const uint64_t end = r.cpu->cycles() + ms(8000);
    while (records < 3 && r.cpu->cycles() < end)
        r.run(10'000);
    r.watch = nullptr;
    REQUIRE(records >= 3);
    r.type("\x03");
    CHECK(r.until("Ok\r\n", ms(4000), from));
    CHECK_EQ(vis(r.out.substr(from)), vis("LIST ,\"W12.TXT\"\r\n\r\nOk\r\n"));
    const std::string last =
        command(r, "OPEN \"I\",#1,\"W12.TXT\":WHILE NOT EOF(1):LINE INPUT #1,A$:WEND:PRINT A$:CLOSE");
    CHECK(last.find(" REM a line to list, one of three hundred\r\n") != std::string::npos);
    if (last.find(" REM a line") == std::string::npos)
        std::printf("  the file's last line: '%s'\n", vis(last).c_str());
}

// W7's OPEN part (the owner, 2026-11-15; random too, once the error path below was fixed): OPEN doesn't find a SYS file (p4/giodsk.s SYSCHK), as MS-DOS's FCB open
// doesn't: INPUT is File not found; OUTPUT, APPEND and random, which would delete the file (CP/M's delete takes a SYS
// file) or make a second directory entry of its name (F_MAKE), are Path/File access error. The SYS file is left as it
// was, one entry, its text whole; and SAVE over one is the same error.
TEST(w7_open_passes_over_a_system_file_and_leaves_it_whole)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W7S.TXT\":PRINT #1,\"system text\":CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W7S.BAS\":CLOSE"), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    r.cmd("set w7s.* [sys]");
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    CHECK(r.to("m:gwbasic", "Ok\r\n").find("Bytes free") != std::string::npos);
    CHECK_EQ(command(r, "OPEN \"I\",#1,\"W7S.TXT\""), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W7S.TXT\""), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"A\",#1,\"W7S.TXT\""), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"W7S.TXT\""), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"W7S.TXT\" AS #3 LEN=20"), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"W7S.TXT\" FOR OUTPUT AS #2"), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "LOAD \"W7S\""), std::string("File not found\r\n"));
    enter(r, {"10 REM"});
    CHECK_EQ(command(r, "SAVE \"W7S\""), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"W7T.TXT\":PRINT #1,\"t\":CLOSE"), std::string()); // the FDBs given back
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string dir = r.cmd("dirsys w7s.*");
    size_t n = 0;
    for (size_t at = dir.find("W7S      TXT"); at != std::string::npos; at = dir.find("W7S      TXT", at + 1))
        ++n;
    CHECK_EQ(n, 1u);
    CHECK(dir.find("W7S      BAS") != std::string::npos);
    const std::string typed = r.cmd("type w7s.txt");
    CHECK(typed.find("system text\r\n") != std::string::npos);
    if (n != 1 || typed.find("system text") == std::string::npos)
        std::printf("  dirsys: '%s'\n  type: '%s'\n", vis(dir).c_str(), vis(typed).c_str());
}

// An error in a random OPEN comes back to Ok (the owner, 2026-11-15): the FDB is FREFDB's, so the error's STKERR
// frees it in FINPRT, and FFREE gives its FIELD buffer back through XFRSTR; FFREE keeps AX-DX, as the 8086's PUSHes
// do, since STKERR keeps its return in CX and ERROR its number in DL across FINPRT. A build whose FFREE doesn't keep
// them returns to CX's garbage (into the monitor). A drive with no image (function 15's error, then Disk not
// Ready), twice, the string space as it was (FRE(0)); a SYS file (W7: Path/File access error, the file whole);
// then a random file opens and closes as before.
TEST(an_error_in_a_random_open_comes_back_to_ok)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "A$=STRING$(50,\"s\"):B$=A$+\"t\""), std::string());
    const std::string fre = command(r, "PRINT FRE(0)");
    for (const char *line : {"OPEN \"R\",#1,\"H:X.DAT\"", "OPEN \"H:X.DAT\" AS #2 LEN=40"})
    {
        const std::string got = command(r, line);
        CHECK(got.find("BDOS Function = 15") != std::string::npos);
        CHECK(got.size() > 16 && got.substr(got.size() - 16) == "Disk not ready\r\n");
        if (got.find("Disk not ready") == std::string::npos)
            std::printf("  %s: '%s'\n", line, vis(got).c_str());
    }
    CHECK_EQ(command(r, "PRINT FRE(0)"), fre);
    CHECK_EQ(command(r, "PRINT LEN(B$);RIGHT$(B$,2)"), std::string(" 51 st\r\n"));
    CHECK_EQ(command(r, "OPEN \"O\",#1,\"RS.TXT\":PRINT #1,\"whole\":CLOSE"), std::string());
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    r.cmd("set rs.txt [sys]");
    r.scb(0x25, 0x40); // CON:'s output TTY alone (SCB $24, VEC_TTY): 7a's console mode, not 7d's own screen
    CHECK(r.to("m:gwbasic", "Ok\r\n").find("Bytes free") != std::string::npos);
    CHECK_EQ(command(r, "A$=STRING$(50,\"s\")"), std::string());
    const std::string fre2 = command(r, "PRINT FRE(0)");
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"RS.TXT\""), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "OPEN \"RS.TXT\" AS #2 LEN=40"), std::string("Path/file access error\r\n"));
    CHECK_EQ(command(r, "PRINT FRE(0);LEN(A$)"), fre2.substr(0, fre2.size() - 2) + " 50 \r\n");
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"RN.DAT\",10:FIELD #1,10 AS F$:LSET F$=\"ok\":PUT #1,1:CLOSE"), std::string());
    CHECK_EQ(command(r, "OPEN \"R\",#1,\"RN.DAT\",10:FIELD #1,10 AS F$:GET #1,1:PRINT F$:CLOSE"),
             std::string("ok        \r\n"));
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    const std::string typed = r.cmd("type rs.txt");
    CHECK(typed.find("whole\r\n") != std::string::npos);
}

// --- 7c's tail (hw/oodos.md, "The plan, phase 7": P5's seed, DEF SEG, PEEK, POKE, BLOAD, BSAVE) -------------------

namespace
{
    /// A block's label's physical address: the block GW-BASIC's loader took for page `page` (PAGBLK, in the resident
    /// core's LOWRAM: blocks 0-2 are the CPU's own addresses) and the label's place in it (each block is linked at
    /// $C800 for W3, $C000 its start).
    uint32_t block_phys(CcpRig &r, unsigned page, const std::string &lbl, const std::string &name)
    {
        const std::map<std::string, uint16_t> res = link_labels("gwbasic.lbl"), blk = link_labels(lbl);
        CHECK(res.count("PAGBLK") == 1 && blk.count(name) == 1);
        if (res.count("PAGBLK") != 1 || blk.count(name) != 1)
            return 0;
        return r.phys(res.at("PAGBLK") + page) * BLOCK + (blk.at(name) - 0xC000u);
    }
} // namespace

// T12, B7's question 5 (the owner, 2026-11-17): P5's start-up (p5/seed.s GXSTRT) calls OOLIB's GX_INIT, then
// GX_BANK8, so once GW-BASIC is up on a revision 2 card the north bridge's MAP has AP8 clear (bank 8 is the
// program's RAM), and gxmem's GXAPH and GXAPB (in P5's block, by p5.lbl) say slot 1's window is WIN1H $0E with
// block 1. SYSTEM's warm boot sets AP8 again. A build that calls GX_BANK8 before GX_INIT fails: GX_INIT always
// goes back to the old mode, so GXAPH is 0 (and GXAPB 33).
TEST(start_up_maps_slot_1_by_gx_bank8)
{
    CcpRig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.nb(0x25) & 1, 1); // NB_MAP's AP8, at the prompt
    CHECK(start(r).find("Bytes free") != std::string::npos);
    CHECK_EQ(r.nb(0x25) & 1, 0);
    CHECK_EQ(r.phys(block_phys(r, 5, "p5.lbl", "GXAPH")), 0x0E);
    CHECK_EQ(r.phys(block_phys(r, 5, "p5.lbl", "GXAPB")), 1);
    r.type("SYSTEM\r");
    CHECK(r.until("A>", ms(4000), r.out.size()));
    CHECK_EQ(r.nb(0x25) & 1, 1);
}

namespace
{
    /// The block GW-BASIC's loader took for page `page` (PAGBLK's byte).
    unsigned page_block(CcpRig &r, unsigned page)
    {
        const std::map<std::string, uint16_t> res = link_labels("gwbasic.lbl");
        CHECK(res.count("PAGBLK") == 1);
        return res.count("PAGBLK") == 1 ? r.phys(res.at("PAGBLK") + page) : 0;
    }

    /// The segment, as GW-BASIC's &H, of physical address `a` (a multiple of 16).
    std::string seg_hex(uint32_t a)
    {
        char b[16];
        std::snprintf(b, sizeof b, "&H%X", static_cast<unsigned>(a >> 4));
        return b;
    }

    /// Counts the times W1 isn't block 1 (NB_WIN1, WIN1H 0) when the resident NEWSTT or SNGFLT runs: at each
    /// statement's end, and as each PEEK's value is made. Each far call has returned by then.
    struct W1Watch
    {
        unsigned seen = 0, off = 0;
        void arm(CcpRig &r)
        {
            const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
            CHECK(lab.count("NEWSTT") == 1 && lab.count("SNGFLT") == 1);
            const uint16_t a = lab.count("NEWSTT") ? lab.at("NEWSTT") : 0, b = lab.count("SNGFLT") ? lab.at("SNGFLT") : 0;
            r.watch = [this, &r, a, b](uint16_t pc) {
                if (pc != a && pc != b)
                    return;
                ++seen;
                off += r.nb(1) != 1 || r.nb(0x21) != 0;
            };
        }
    };
} // namespace

// T1 (GWEVAL.ASM DEF, DEFNC2): DEF SEG = n makes n the segment (FRMQNT: &H9000 and -28672 are the same, 65536
// Overflow); DEF SEG alone is segment 0, the port's DS, the program's own memory from physical $00000, so a POKE at
// VARPTR's address changes the variable. Bank 9 is RAM no one else uses. A build whose DEF SEG alone leaves SAVSEG
// as it was (the 8086's MOV DX,DS dropped) POKEs bank 9 instead, and A% stays 0.
TEST(def_seg_sets_the_segment_and_alone_is_segment_0)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "DEF SEG=&H9000:POKE 0,77:POKE 1,78"), std::string());
    CHECK_EQ(r.phys(0x90000), 77);
    CHECK_EQ(r.phys(0x90001), 78);
    CHECK_EQ(command(r, "DEF SEG=-28672:PRINT PEEK(0);PEEK(1)"), std::string(" 77  78 \r\n"));
    CHECK_EQ(command(r, "A%=0:DEF SEG:POKE VARPTR(A%),5:PRINT A%"), std::string(" 5 \r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H9000:POKE 2,79:DEF SEG:PRINT PEEK(VARPTR(A%))"), std::string(" 5 \r\n"));
    CHECK_EQ(r.phys(0x90002), 79);
    CHECK_EQ(command(r, "DEF SEG=65536"), std::string("Overflow\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H9000:DEF SEG:PRINT PEEK(VARPTR(A%))"), std::string(" 5 \r\n"));
    CHECK_EQ(command(r, "DEF SEG 5"), std::string("Syntax error\r\n"));
}

// T2: under segment 0 only $0000-$BFFF is the CPU's own (the program's blocks 0-2); $C000 on is physical block 3,
// which PEEK and POKE reach through P5 (function 83), not the CPU's W3 (GW-BASIC's code) nor the I/O hole at
// $C000-$C7FF. The test puts $5A at physical $0C800. A build whose fast path lacks the $C000 bound reads W3.
TEST(segment_0_above_c000_is_block_3_not_w3)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    r.poke(0x0C800, 0x5A);
    r.poke(0x0C000, 0x33);
    CHECK_EQ(command(r, "PRINT PEEK(&HC800);PEEK(-16384)"), std::string(" 90  51 \r\n"));
    CHECK_EQ(command(r, "POKE &HC800,7:POKE 49153!,8"), std::string());
    CHECK_EQ(r.phys(0x0C800), 7);
    CHECK_EQ(r.phys(0x0C001), 8);
    CHECK_EQ(command(r, "PRINT PEEK(&HC800)"), std::string(" 7 \r\n"));
}

// T3, P5's gate (V3; hw/oodos.md 7c): PEEK and POKE in an allocated block (P8's, past its code at offset $2000 and
// below its copy of common memory; its segment from PAGBLK) and in bank 9 go through GX_BLOCK, then GX_W1BACK
// puts block 1 back. A%(4000) of an 8,001-element array lies in $4000-$7FFF (VARPTR), so in the same statement
// after each it must read as itself, and W1 must be block 1 at every statement's end and every PEEK's value. A
// build whose DSBYTE leaves out GX_W1BACK reads the element from the other block.
TEST(peek_poke_in_an_allocated_block_and_bank_9_put_block_1_back)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(command(r, "DIM A%(8000):A%(4000)=1234:V=VARPTR(A%(4000)):PRINT V>=16384 AND V<32767"),
             std::string("-1 \r\n"));
    const uint32_t p8 = page_block(r, 8) * BLOCK + 0x2000;
    CHECK(p8 >= 4 * BLOCK);
    r.poke(p8 + 1, 0x42);
    W1Watch w;
    w.arm(r);
    CHECK_EQ(command(r, "DEF SEG=&H9000:POKE &H123,99:PRINT A%(4000);PEEK(&H123);A%(4000)"),
             std::string(" 1234  99  1234 \r\n"));
    CHECK_EQ(r.phys(0x90123), 99);
    CHECK_EQ(command(r, "DEF SEG=" + seg_hex(p8) + ":POKE 0,55:PRINT A%(4000);PEEK(0);PEEK(1);A%(4000)"),
             std::string(" 1234  55  66  1234 \r\n"));
    CHECK_EQ(r.phys(p8), 55);
    CHECK_EQ(command(r, "DEF SEG:PRINT A%(4000)"), std::string(" 1234 \r\n"));
    r.watch = nullptr;
    CHECK(w.seen > 6);
    CHECK_EQ(w.off, 0u);
}

// T4's PEEK and POKE half (B7's question 2, the owner, 2026-11-17): any access meeting bank 8, $80000-$8FFFF, is
// Illegal function call, PEEK included, tested on the 20-bit address: &H7FFF:&H100 is $800F0, and &H8400 is bank 8
// too. Bank 8's RAM (the program's after GX_BANK8) is unchanged; bank 9 still works after. A build that tests the
// segment ($8000-$8FFF) instead lets &H7FFF:&H100 through (GX_BLOCK maps bank 8's block after GX_BANK8). This is in
// console mode; 7d3's screen page is test_gwbasic_screen's (and this case is its console-mode must-fail, D5).
TEST(bank_8_is_refused_to_peek_and_poke)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    r.poke(0x800F0, 0xC3);
    r.poke(0x84000, 0x3C);
    r.poke(0x8FFFF, 0x5A);
    CHECK_EQ(command(r, "DEF SEG=&H7FFF:POKE &H100,1"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT PEEK(&H100)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H8400:POKE 0,1"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT PEEK(0)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H8FFF:PRINT PEEK(15)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H7FFF:PRINT PEEK(15)"), std::string(" 0 \r\n")); // $7FFFF: bank 7, OODOS's: 0
    CHECK_EQ(r.phys(0x800F0), 0xC3);
    CHECK_EQ(r.phys(0x84000), 0x3C);
    CHECK_EQ(r.phys(0x8FFFF), 0x5A);
    CHECK_EQ(command(r, "DEF SEG=&H9000:POKE 0,9:PRINT PEEK(0)"), std::string(" 9 \r\n"));
}

// T4's PEEK and POKE half, the screen page's side (7d3 review round 1, F4): in console mode a POKE under DEF
// SEG=&H8000 is Illegal function call and the console's card memory (OODOS's units 0-12, card $000000-$0033FF, which
// CON:'s TTY-only output never writes) is unchanged, with the PEEK of it refused too. Guards a program poking the
// PC's screen in console mode writing OODOS's own console text. Must fail: DSSCR's SCRMOD test left out.
TEST(console_mode_s_screen_page_is_refused)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    VideoCard &vid = *r.machine->video_card();
    std::vector<uint8_t> con(0x3400);
    for (uint32_t i = 0; i < con.size(); ++i)
        con[i] = vid.card_peek(i);
    CHECK_EQ(command(r, "DEF SEG=&H8000:POKE 0,1"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "POKE 1,2"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT PEEK(0)"), std::string("Illegal function call\r\n"));
    unsigned moved = 0;
    for (uint32_t i = 0; i < con.size(); ++i)
        moved += vid.card_peek(i) != con[i];
    CHECK_EQ(moved, 0u);
}

// T9, B7's question 3: with no revision 2 card GX_INIT says GE_VERSION and GX_BLOCK refuses every block, so DEF SEG
// reaches bank 9 and an allocated block (P8's) by the 1-byte DM_COPY route instead: the bytes POKEd are in physical
// memory and PEEK reads them, and W1 stays block 1 throughout. A build that takes GX_BLOCK's refusal as the end
// (Illegal function call) fails it.
TEST(no_revision_2_card_takes_the_dm_copy_route)
{
    CcpRig r({}, {}, {}, 1);
    REQUIRE(r.ok);
    CHECK(start(r).find("Bytes free") != std::string::npos);
    const uint32_t p8 = page_block(r, 8) * BLOCK + 0x2000;
    r.poke(p8 + 1, 0x42);
    W1Watch w;
    w.arm(r);
    CHECK_EQ(command(r, "DEF SEG=&H9000:POKE &H123,99:PRINT PEEK(&H123)"), std::string(" 99 \r\n"));
    CHECK_EQ(r.phys(0x90123), 99);
    CHECK_EQ(command(r, "DEF SEG=" + seg_hex(p8) + ":POKE 0,55:PRINT PEEK(0);PEEK(1)"), std::string(" 55  66 \r\n"));
    CHECK_EQ(r.phys(p8), 55);
    r.watch = nullptr;
    CHECK(w.seen > 4);
    CHECK_EQ(w.off, 0u);
}

// T10: OODOS's own memory (banks 6, 7, C, D, F) is function 83's to refuse: a PEEK there gives 0 (the scratch byte
// cleared first), a POKE is Illegal function call, the memory unchanged; bank E (Apple II mode's) reads and writes
// by DM_COPY. The bank E POKE and PEEK leave 77 in the scratch byte first, so a build that doesn't clear it before a
// refused copy gives 77 for bank F.
TEST(oodos_memory_peeks_0_and_refuses_pokes)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const uint8_t f0 = r.phys(0xF0000), s6 = r.phys(0x60010);
    CHECK_EQ(command(r, "DEF SEG=&HE000:POKE 0,77:PRINT PEEK(0)"), std::string(" 77 \r\n"));
    CHECK_EQ(r.phys(0xE0000), 77);
    CHECK_EQ(command(r, "DEF SEG=&HF000:PRINT PEEK(0)"), std::string(" 0 \r\n"));
    CHECK_EQ(command(r, "POKE 0,1"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H6001:PRINT PEEK(0)"), std::string(" 0 \r\n"));
    CHECK_EQ(command(r, "POKE 0,1"), std::string("Illegal function call\r\n"));
    CHECK_EQ(r.phys(0xF0000), f0);
    CHECK_EQ(r.phys(0x60010), s6);
}

// T11 (the OEM table: CALL and USR, "a 6502 routine in the TPA (segment 0); any other segment refused"): under DEF
// SEG=&H100, USR is Illegal function call and its routine isn't run (it would INC M%); after DEF SEG, it runs as
// before. A build whose USRFNP ignores SAVSEG runs it.
TEST(usr_under_another_segment_is_refused)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 X=0:M%=0:Q=VARPTR(M%):DIM A%(3):P=VARPTR(A%(0))",
              "20 POKE P,&HEE:POKE P+1,Q AND 255:POKE P+2,INT(Q/256):POKE P+3,&H60", // INC M% / RTS
              "30 DEF USR=P"});
    CHECK_EQ(command(r, "RUN"), std::string());
    CHECK_EQ(command(r, "DEF SEG=&H100:X=USR(0)"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "PRINT M%"), std::string(" 0 \r\n"));
    CHECK_EQ(command(r, "DEF SEG:X=USR(0):PRINT M%"), std::string(" 1 \r\n"));
}

// PEEK's cost on each route (7c's tail, measured for hw/oodos.md): the cycles from FUNDSP's F_PEEK to LABBCK (the
// function's return), the argument already evaluated: segment 0 below $C000 (one far call, to P1), bank 9 through
// GX_BLOCK and GX_W1BACK (a second, to P5), and bank E by a 1-byte DM_COPY (a BDOS call besides). Each the least of
// 4. Bounds only against a run gone astray.
TEST(peek_costs_on_each_route)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    const std::map<std::string, uint16_t> lab = link_labels("gwbasic.lbl");
    REQUIRE(lab.count("F_PEEK") == 1 && lab.count("LABBCK") == 1);
    const uint16_t from = lab.at("F_PEEK"), to = lab.at("LABBCK");
    auto cost = [&](const std::string &seg) {
        uint64_t at = 0, best = ~uint64_t{0};
        r.watch = [&](uint16_t pc) {
            if (pc == from)
                at = r.cpu->cycles();
            else if (pc == to && at)
            {
                best = std::min(best, r.cpu->cycles() - at);
                at = 0;
            }
        };
        CHECK_EQ(command(r, seg + ":X=PEEK(256):X=PEEK(256):X=PEEK(256):X=PEEK(256)"), std::string());
        r.watch = nullptr;
        return best;
    };
    const uint64_t s0 = cost("DEF SEG"), b9 = cost("DEF SEG=&H9000"), be = cost("DEF SEG=&HE000");
    std::printf("  PEEK: segment 0 %llu cycles, bank 9 (GX_BLOCK) %llu, bank E (DM_COPY) %llu\n",
                static_cast<unsigned long long>(s0), static_cast<unsigned long long>(b9),
                static_cast<unsigned long long>(be));
    CHECK(s0 > 100 && s0 < b9 && b9 < be && be < 10000);
}

namespace
{
    /// A byte of the test pattern at place i (none of 256 consecutive the same).
    uint8_t pat(uint32_t i) { return static_cast<uint8_t>(i * 7 + 3); }
    void put_pattern(CcpRig &r, uint32_t at, uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i)
            r.poke(at + i, pat(i));
    }
    /// The places in [at, at + n) whose byte isn't the pattern's.
    unsigned off_pattern(CcpRig &r, uint32_t at, uint32_t n)
    {
        unsigned bad = 0;
        for (uint32_t i = 0; i < n; ++i)
            bad += r.phys(at + i) != pat(i);
        return bad;
    }
    void fill(CcpRig &r, uint32_t at, uint32_t n, uint8_t v)
    {
        for (uint32_t i = 0; i < n; ++i)
            r.poke(at + i, v);
    }
    unsigned not_byte(CcpRig &r, uint32_t at, uint32_t n, uint8_t v)
    {
        unsigned bad = 0;
        for (uint32_t i = 0; i < n; ++i)
            bad += r.phys(at + i) != v;
        return bad;
    }
} // namespace

// T4's BLOAD and BSAVE half (B7's question 2), and the owner's answer 2: a run meeting bank 8 is Illegal function
// call before anything moves: BLOAD to &H8400:0 (slot 1's old window, bank 8's RAM since GX_BANK8), a run from bank 7
// into it, and BSAVE from it (no file made). Bank 8's RAM and card memory's slot 1 bank (the program's first, card
// $040000) are unchanged; bank 9 still works after. And a BSAVE that meets OODOS's memory (bank F; bank 5 into bank
// 6) is Illegal function call with no file written. A build whose DSRUN leaves out the bank 8 test writes bank 8 by
// DM_LOAD, which OODOS allows after GX_BANK8. This is in console mode; 7d3's screen page is test_gwbasic_screen's (and
// this case is its console-mode must-fail, D5).
TEST(bank_8_is_refused_whole_to_bload_and_bsave)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    VideoCard &vid = *r.machine->video_card();
    put_pattern(r, 0x90000, 512);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BSAVE \"B\",0,512"), std::string());
    fill(r, 0x84000, 600, 0xC3);
    fill(r, 0x80000, 16, 0xC3);
    std::vector<uint8_t> card(600);
    for (uint32_t i = 0; i < card.size(); ++i)
        card[i] = vid.card_peek(0x040000 + i);
    CHECK_EQ(command(r, "DEF SEG=&H8400:BLOAD \"B\",0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H7FFF:BLOAD \"B\",0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H8400:BSAVE \"C\",0,&H100"), std::string("Illegal function call\r\n"));
    CHECK_EQ(not_byte(r, 0x84000, 600, 0xC3), 0u);
    CHECK_EQ(not_byte(r, 0x80000, 16, 0xC3), 0u);
    unsigned moved = 0;
    for (uint32_t i = 0; i < card.size(); ++i)
        moved += vid.card_peek(0x040000 + i) != card[i];
    CHECK_EQ(moved, 0u);
    CHECK_EQ(command(r, "DEF SEG=&HF000:BSAVE \"D\",0,16"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "DEF SEG=&H5FF0:BSAVE \"D\",0,512"), std::string("Illegal function call\r\n"));
    CHECK_EQ(files(r, "FILES \"C.BAS\""), std::string("File not found\r\n"));
    CHECK_EQ(files(r, "FILES \"D.BAS\""), std::string("File not found\r\n"));
    fill(r, 0x90000, 512, 0);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BLOAD \"B\",0:PRINT PEEK(511)"), std::string(" " + std::to_string(pat(511)) + " \r\n"));
    CHECK_EQ(off_pattern(r, 0x90000, 512), 0u);
}

// T5: the run is checked whole before anything moves. A 512-byte file BLOADed at &H5FF0:0 ($5FF00, block 23: the
// program's banks, unallocated, AMAP's last byte) runs into bank 6, OODOS's: Illegal function call, with
// $5FF00-$5FFFF unchanged. A build whose DSRUN tests only the run's first block puts the head in bank 5, and then
// DM_LOAD is refused partway (or the last part copied).
TEST(a_run_is_checked_whole_before_anything_moves)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("AMAP") + 19), 0); // block 23 free
    put_pattern(r, 0x90000, 512);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BSAVE \"B\",0,512"), std::string());
    fill(r, 0x5FF00, 256, 0xA5);
    const uint8_t six = r.phys(0x60000);
    CHECK_EQ(command(r, "DEF SEG=&H5FF0:BLOAD \"B\",0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(not_byte(r, 0x5FF00, 256, 0xA5), 0u);
    CHECK_EQ(r.phys(0x60000), six);
    CHECK_EQ(command(r, "DEF SEG=&H5FE0:BLOAD \"B\",0"), std::string()); // $5FE00-$5FFFF: bank 5 alone
    CHECK_EQ(off_pattern(r, 0x5FE00, 512), 0u);
}

// T6 (GIO86.ASM BLOAD, BLODP, DEFSAD; GIODSK.ASM DFSTLD): BLOAD puts the bytes and nothing past them. 1,000 patterned
// bytes BSAVEd from &H9000:0, BLOADed to &HA000:0: the rest of record 0 by DM_COPY, whole records by DM_LOAD, the
// last part record's bytes only, so the sentinels at $9FFFF and $A03E8 stay. A 100-byte file (record 0 alone). BLOAD
// with no offset goes to the file's own &H9000:0. DEF SEG: BLOAD "B",VARPTR(A%(0)) fills an integer array (the PC's
// idiom). Must fail: (a) the last record loaded whole by DM_LOAD (the $A03E8 sentinel); (b) the 7 header bytes not
// skipped (the bytes shift).
TEST(bload_puts_the_bytes_and_nothing_past_them)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    put_pattern(r, 0x90000, 1000);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BSAVE \"B\",0,1000:BSAVE \"S\",0,100"), std::string());
    fill(r, 0x9FFF0, 0x500, 0xEE);
    CHECK_EQ(command(r, "DEF SEG=&HA000:BLOAD \"B\",0"), std::string());
    CHECK_EQ(off_pattern(r, 0xA0000, 1000), 0u);
    CHECK_EQ(not_byte(r, 0x9FFF0, 16, 0xEE), 0u);
    CHECK_EQ(not_byte(r, 0xA03E8, 0x100, 0xEE), 0u);
    fill(r, 0xA1000, 0x100, 0xEE);
    CHECK_EQ(command(r, "DEF SEG=&HA100:BLOAD \"S\",0"), std::string());
    CHECK_EQ(off_pattern(r, 0xA1000, 100), 0u);
    CHECK_EQ(not_byte(r, 0xA1064, 0x9C, 0xEE), 0u);
    fill(r, 0x90000, 1000, 0);
    CHECK_EQ(command(r, "DEF SEG=&H1234:BLOAD \"B\""), std::string());
    CHECK_EQ(off_pattern(r, 0x90000, 1000), 0u);
    const int w0 = pat(0) | pat(1) << 8, w499 = pat(998) | pat(999) << 8;
    CHECK_EQ(command(r, "DIM A%(499):DEF SEG:BLOAD \"B\",VARPTR(A%(0)):PRINT A%(0);A%(499)"),
             std::string(" " + std::to_string(w0 > 32767 ? w0 - 65536 : w0) + "  " +
                         std::to_string(w499 > 32767 ? w499 - 65536 : w499) + " \r\n"));
}

// T7 (GIO86.ASM BSAVE, OUT16; GIO86U's BSVFID): BSAVE writes $FD, the segment, the offset and the length (each low
// byte first), then the bytes: read back from M:'s directory and blocks, 1,000 bytes from &H9000:0 are 8 records,
// $FD 00 90 00 00 E8 03, the bytes, and DSKCLS's ^Z. A length of 0 writes the header alone. A build whose piece
// loop doesn't move its source on at a record's end repeats the run's first bytes in every record.
TEST(bsave_writes_the_header_and_the_bytes)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    put_pattern(r, 0x90000, 1000);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BSAVE \"M:B7\",0,1000:BSAVE \"M:Z7\",&H10,0"), std::string());
    const std::vector<uint8_t> d = m_data(r, 0, "B7      BAS");
    REQUIRE(d.size() == 8 * RECORD);
    const std::vector<uint8_t> head = {0xFD, 0x00, 0x90, 0x00, 0x00, 0xE8, 0x03};
    CHECK(std::vector<uint8_t>(d.begin(), d.begin() + 7) == head);
    unsigned bad = 0;
    for (uint32_t i = 0; i < 1000; ++i)
        bad += d[7 + i] != pat(i);
    CHECK_EQ(bad, 0u);
    CHECK_EQ(d[1007], 0x1A);
    const std::vector<uint8_t> z = m_data(r, 0, "Z7      BAS");
    REQUIRE(z.size() == RECORD);
    CHECK(std::vector<uint8_t>(z.begin(), z.begin() + 8) == std::vector<uint8_t>({0xFD, 0x00, 0x90, 0x10, 0x00, 0x00, 0x00, 0x1A}));
}

// T8 (GIO86.ASM BLOAD's ERBFM2, BPARMS's SNERR2 and SNERR3, PRODIR): BLOAD of a SAVE ,A file is Bad file mode,
// nothing written; a missing file File not found; BSAVE with one number and BLOAD with two Syntax error, as is more
// after BSAVE's length; in direct mode under a protected program (SAVE ,P, LOAD), both Illegal function call; a
// device's name Bad file mode (the owner's answer 3: disk files only). A build without the $FD check writes the text
// file's bytes at the destination.
TEST(bload_and_bsave_errors)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    enter(r, {"10 PRINT 1"});
    CHECK_EQ(command(r, "SAVE \"T\",A"), std::string());
    fill(r, 0xA0000, 256, 0xEE);
    CHECK_EQ(command(r, "DEF SEG=&HA000:BLOAD \"T\",0"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "BLOAD \"T.BAS\""), std::string("Bad file mode\r\n"));
    CHECK_EQ(not_byte(r, 0xA0000, 256, 0xEE), 0u);
    CHECK_EQ(command(r, "BLOAD \"NONE\",0"), std::string("File not found\r\n"));
    CHECK_EQ(command(r, "BSAVE \"X\",0"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "BSAVE \"X\""), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "BLOAD \"T\",0,10"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "BSAVE \"X\",0,10,5"), std::string("Syntax error\r\n"));
    CHECK_EQ(command(r, "BSAVE \"SCRN:\",0,10"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "BLOAD \"KYBD:\",0"), std::string("Bad file mode\r\n"));
    CHECK_EQ(command(r, "SAVE \"P\",P"), std::string());
    CHECK_EQ(command(r, "LOAD \"P\""), std::string());
    CHECK_EQ(command(r, "BSAVE \"X\",0,10"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "BLOAD \"T\",0"), std::string("Illegal function call\r\n"));
    CHECK_EQ(command(r, "NEW"), std::string());
    CHECK_EQ(files(r, "FILES \"X.BAS\""), std::string("File not found\r\n"));
}

// BLOAD of 65,535 bytes (7c's tail, measured for hw/oodos.md; the design's estimate about 16 ms, by DM_LOAD): the
// cycles of the BLOAD command from its line typed to Ok, after a BSAVE of bank 9's first 65,535 bytes; the bytes
// checked. A bound only against a run gone astray.
TEST(bload_of_64_kb_by_dm_load)
{
    CcpRig r;
    REQUIRE(r.ok);
    start(r);
    put_pattern(r, 0x90000, 65535);
    CHECK_EQ(command(r, "DEF SEG=&H9000:BSAVE \"BIG\",0,&HFFFF"), std::string());
    const std::map<std::string, uint16_t> p4 = link_labels("p4.lbl");
    REQUIRE(p4.count("BLDRUN") == 1 && p4.count("LODEND") == 1);
    const unsigned blk4 = page_block(r, 4);
    uint64_t run0 = 0, run = 0;
    r.watch = [&](uint16_t pc) {
        if (r.nb(3) != blk4)
            return;
        if (pc == p4.at("BLDRUN"))
            run0 = r.cpu->cycles();
        else if (pc == p4.at("LODEND") && run0)
            run = r.cpu->cycles() - run0;
    };
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(command(r, "DEF SEG=&HA000:BLOAD \"BIG\",0"), std::string());
    const uint64_t c = r.cpu->cycles() - c0;
    r.watch = nullptr;
    auto msf = [](uint64_t n) { return static_cast<double>(n) * 1000.0 / static_cast<double>(HZ); };
    std::printf("  BLOAD of 65,535 bytes: the bytes (BLDRUN: DFSTLD) %llu cycles, %.1f ms; the command from its line "
                "typed to Ok %llu, %.1f ms\n",
                static_cast<unsigned long long>(run), msf(run), static_cast<unsigned long long>(c), msf(c));
    CHECK(run > 0 && run < c);
    CHECK_EQ(off_pattern(r, 0xA0000, 65535), 0u);
    CHECK(c < ms(1000));
}
