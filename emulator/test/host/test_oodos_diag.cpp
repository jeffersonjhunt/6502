/**
 * @file test_oodos_diag.cpp
 * @brief DIAG.COM and oohw.lib's memory and report routines (hw/diag.md, D2), run from OODOS's CCP on
 *        backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_sysinfo boots it (oodos_rig.h's CcpRig), the build's DIAG.COM put on M:.
 *
 *   - `DIAG` on the whole machine: every test passes or is skipped for its option, the return code is 0, and
 *     the report is golden/diag.txt once what a measurement makes is taken out (each digit of the Hz, the
 *     percentages, the DMA's cycles a byte, the frame's cycles, the stream's rate, the SD card's speed and
 *     M1's second range, which moves with DIAG's size, written '#': the dots stay where they were);
 *   - faults, put in by test-only devices (hw/diag.md, answer 8): FaultyMemory, mapped over physical memory
 *     after the machine is built (later mappings win, for DMA too), forces a bit of one byte: in block 40
 *     (M4, the plan's sample: "block 40 +$1234 ($A1234): read $08, expected $00: bit 3", return code 1), in
 *     block 8 (M2), in aperture slot 1's window, block 33 (V4: the port and slot 1 disagree); and the sound
 *     card's own test-only hook (SoundCard::set_ram_fault) in its RAM (S4);
 *   - no sound card (every S test skipped), the sound card held by another (S2 on skipped "in use");
 *   - /WRITE answered N (the card's image byte for byte as it was) and Y (SD4 passes, and A:'s directory is
 *     as it was);
 *   - P3 (/FULL /I): the breakpoint halts the CPU on DIAG's read, CONT goes on, and it passes; and review
 *     CC3's case: a ^C typed at P3's prompt while the breakpoint is armed, then a program that reads every
 *     byte from $8000 to $BEFF (DIAG's armed byte among them) runs to its end, and CTRL's bit 3 is clear;
 *   - the rules gate on DIAG.COM's two code segments, and on a copy with an RMB put in.
 *
 * DIAG_GOLDEN=update writes golden/diag.txt from what DIAG prints, which a person does on purpose, after
 * reading it. OODOS_UTILS_DIR names another build of DIAG.COM, to see a case fail on a fault put in it on
 * purpose (hw/diag.md, "D2: as built", has each).
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;

    /// A test program for CC3: it reads every byte from $8000 to $BEFF, then ends (JMP WBOOT). Its 16-byte
    /// header as OODOS_HEADER writes one: "OOD", ^Z, no flags, page 3, the image's size, no BSS, two bytes of
    /// zero page from $00.
    std::vector<uint8_t> sweep_com()
    {
        const std::vector<uint8_t> code = {
            0xA9, 0x00, 0x85, 0x00, // LDA #0, STA $00
            0xA9, 0x80, 0x85, 0x01, // LDA #$80, STA $01
            0xA0, 0x00,             // LDY #0
            0xB1, 0x00,             // loop: LDA ($00),Y
            0xC8, 0xD0, 0xFB,       // INY, BNE loop
            0xE6, 0x01, 0xA5, 0x01, // INC $01, LDA $01
            0xC9, 0xBF, 0xD0, 0xF3, // CMP #$BF, BNE loop
            0x4C, 0x00, 0x02};      // JMP $0200
        std::vector<uint8_t> com = {0x4F, 0x4F, 0x44, 0x1A, 0x00, 0x03, static_cast<uint8_t>(code.size()), 0x00,
                                    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00};
        com.insert(com.end(), code.begin(), code.end());
        return com;
    }

    /// A rig with the build's DIAG on M:, and SWEEP.COM.
    struct DiagRig : CcpRig
    {
        explicit DiagRig(const std::string &machine_text = {}) : CcpRig({}, machine_text)
        {
            if (!ok)
                return;
            put_util("diag", UTILS);
            put_m({{0, "SWEEP   COM", sweep_com(), true}});
        }
        /// The return code (function 108: the SCB's word at $10).
        unsigned code() const { return unsigned{phys(SCB_PHYS + 0x10)} | unsigned{phys(SCB_PHYS + 0x11)} << 8; }
    };

    /// `line` at the CCP: what it wrote between the line's echo and the prompt, CR LF as \n.
    std::string run(CcpRig &r, const std::string &line, uint64_t budget = ms(600000))
    {
        const std::string o = r.to(line, "\r\nA>", budget);
        const std::string head = line + "\r\n", end = "\r\nA>";
        if (o.size() < head.size() + end.size() || o.compare(0, head.size(), head) != 0 ||
            o.compare(o.size() - end.size(), end.size(), end) != 0)
            return "<" + vis(o) + ">";
        const std::string body = o.substr(head.size(), o.size() - head.size() - end.size());
        std::string s;
        for (size_t i = 0; i < body.size(); ++i)
            if (!(body[i] == '\r' && i + 1 < body.size() && body[i + 1] == '\n'))
                s += body[i];
        return s;
    }

    std::vector<std::string> split(const std::string &s)
    {
        std::vector<std::string> v;
        std::istringstream in(s);
        std::string l;
        while (std::getline(in, l))
            v.push_back(l.empty() || l.back() != '\r' ? l : l.substr(0, l.size() - 1));
        return v;
    }

    /// The report's line of test `name` ("  NB1  ..."), and the line after it (a failure's detail), or "".
    std::string line_of(const std::string &report, const std::string &name, unsigned after = 0)
    {
        const std::vector<std::string> v = split(report);
        const std::string key = "  " + name + std::string(5 - name.size(), ' ');
        for (size_t i = 0; i + after < v.size(); ++i)
            if (v[i].compare(0, key.size(), key) == 0 && v[i].find(" ...") != std::string::npos)
                return v[i + after];
        return {};
    }
    std::string detail_of(const std::string &report, const std::string &name) { return line_of(report, name, 1); }
    bool ends(const std::string &s, const std::string &tail)
    {
        return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
    }
    size_t count(const std::string &s, const std::string &what)
    {
        size_t n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
            ++n;
        return n;
    }

    /// Each digit (and sign) in the parts of the report a measurement makes, '#', so its length (and its dots)
    /// stay; and M1's second range and its bytes, which move with DIAG's size, the same.
    std::string normal(const std::string &got)
    {
        const std::vector<std::regex> parts = {
            std::regex("CPU ([0-9,]+) Hz by the (?:clock chip|video frame) \\(([-+][0-9.]+)%\\)"),
            std::regex("a DMA fill takes ([0-9.]+) cycles a byte"),
            std::regex("a frame each ([0-9,]+) cycles"),
            std::regex("8,000 Hz: ([0-9,]+) frames a second"),
            std::regex("read by DMA: ([0-9,]+) KB a second"),
            std::regex("TPA \\$4000-\\$7FFF, \\$([0-9A-F]+)-\\$BFFC, ([0-9,]+) bytes")};
        std::string s = got;
        for (const std::regex &re : parts)
        {
            std::string out;
            size_t at = 0;
            for (std::sregex_iterator it(s.begin(), s.end(), re), none; it != none; ++it)
            {
                const std::smatch &m = *it;
                std::string t = m.str();
                for (size_t g = 1; g < m.size(); ++g)
                    for (size_t i = 0; i < static_cast<size_t>(m.length(g)); ++i)
                    {
                        char &c = t[static_cast<size_t>(m.position(g) - m.position()) + i];
                        if (c != ',' && c != '.')
                            c = '#';
                    }
                out += s.substr(at, static_cast<size_t>(m.position()) - at) + t;
                at = static_cast<size_t>(m.position() + m.length());
            }
            s = out + s.substr(at);
        }
        return s;
    }

    /// The output against golden/NAME; with DIAG_GOLDEN=update, stored as it.
    bool golden(const std::string &name, const std::string &got)
    {
        const std::string path = std::string(EMU_GOLDEN_DIR) + "/" + name;
        const char *u = std::getenv("DIAG_GOLDEN");
        if (u && std::string(u) == "update")
        {
            std::ofstream(path, std::ios::binary) << got;
            std::printf("  %s written\n", name.c_str());
            return true;
        }
        const std::string want = read_text(path);
        if (got == want)
            return true;
        std::printf("  %s differs:\n", name.c_str());
        const std::vector<std::string> g = split(got), w = split(want);
        for (size_t i = 0; i < std::max(g.size(), w.size()); ++i)
        {
            const std::string a = i < g.size() ? g[i] : "<none>", b = i < w.size() ? w[i] : "<none>";
            if (a != b)
                std::printf("    line %zu\n      got  '%s'\n      want '%s'\n", i + 1, a.c_str(), b.c_str());
        }
        return false;
    }

    /// backplane.machine with the lines naming `type` (the line's first word) left out.
    std::string without(const std::string &type)
    {
        std::istringstream in(read_text(EMU_MACHINE_DIR "/backplane.machine"));
        std::string l, out;
        while (std::getline(in, l))
        {
            std::istringstream w(l);
            std::string first;
            w >> first;
            if (first != type)
                out += l + "\n";
        }
        return out;
    }

    /// A bad cell, test-only (hw/diag.md, answer 8): mapped over a 16 KB block of physical memory after the
    /// machine is built, it forwards every access to the device that was there, but the byte at `bad` reads as
    /// (byte AND `and_mask`) OR `or_mask`. The CPU's windows and DMA both go through the bus's mappings, so both
    /// see it.
    struct FaultyMemory : Device
    {
        Device *under{nullptr};
        uint32_t first{0}, under_first{0}, bad{0};
        uint8_t and_mask{0xFF}, or_mask{0};
        size_t size() const override { return 0x4000; }
        uint32_t at(uint32_t offset) const
        {
            return static_cast<uint32_t>((first + offset - under_first) % under->size());
        }
        uint8_t read(uint32_t offset) override { return fault(offset, under->read(at(offset))); }
        uint8_t peek(uint32_t offset) const override { return fault(offset, under->peek(at(offset))); }
        void write(uint32_t offset, uint8_t data) override { under->write(at(offset), data); }
        uint8_t fault(uint32_t offset, uint8_t v) const
        {
            return first + offset == bad ? static_cast<uint8_t>((v & and_mask) | or_mask) : v;
        }
    };
    /// FaultyMemory over the 16 KB block holding physical `bad`, on whatever is mapped there now.
    bool fault_at(CcpRig &r, FaultyMemory &f, uint32_t bad, uint8_t and_mask, uint8_t or_mask)
    {
        const uint32_t lo = bad & ~0x3FFFu, hi = lo + 0x3FFF;
        const auto &maps = r.machine->bus().mappings();
        const Mapping *m = nullptr;
        for (auto it = maps.rbegin(); it != maps.rend() && !m; ++it)
            if (it->first <= bus_address(Space::Memory, lo) && it->last >= bus_address(Space::Memory, hi))
                m = &*it;
        if (!m)
            return false;
        f.under = m->device;
        f.under_first = offset_of(m->first);
        f.first = lo;
        f.bad = bad;
        f.and_mask = and_mask;
        f.or_mask = or_mask;
        Mapping n = *m;
        n.first = bus_address(Space::Memory, lo);
        n.last = bus_address(Space::Memory, hi);
        n.device = &f;
        return r.machine->bus().map(n);
    }

    /// Run `cycles` as Rig::run does, but with the console card asked before each instruction, as the
    /// emulator's own run loop does (made the CPU's observer, the card halts it at a read breakpoint).
    /// @return false at once if the CPU is halted
    bool run_halting(CcpRig &r, uint64_t cycles)
    {
        ConsoleCard *card = r.machine->console_card();
        const uint64_t end = r.cpu->cycles() + cycles;
        while (r.cpu->cycles() < end)
        {
            if (card && !card->may_run(r.cpu->regs().pc))
                return false;
            r.cpu->step();
            if (r.machine->bus().irq())
                r.cpu->irq();
            if (r.cpu->cycles() % 64 > 7)
                continue;
            r.console->service();
            uint8_t ch = 0;
            while (r.console->output(ch))
                r.out += static_cast<char>(ch);
        }
        return true;
    }
    /// Run (halts honoured) until the output since `from` has `text`. @return whether it did
    bool until_halting(CcpRig &r, const std::string &text, uint64_t budget, size_t from)
    {
        const uint64_t end = r.cpu->cycles() + budget;
        while (r.out.find(text, from) == std::string::npos && r.cpu->cycles() < end)
            if (!run_halting(r, 20'000))
                return false;
        return r.out.find(text, from) != std::string::npos;
    }
    /// Keys on the serial line, as Rig::type, halts honoured.
    void type_halting(CcpRig &r, const std::string &keys)
    {
        for (const char k : keys)
        {
            for (int i = 0; i < 2000 && !r.console->key(static_cast<uint8_t>(k)); ++i)
                run_halting(r, 1'000);
            run_halting(r, 30'000);
        }
    }

    /// `DIAG ...` typed, its question ("Y or N? ") answered `answer`. @return what it wrote from the line on
    std::string answered(DiagRig &r, const std::string &line, const std::string &answer)
    {
        const size_t from = r.out.size();
        r.type(line + "\r");
        if (!r.until("Y or N? ", ms(60000), from) || !r.calls(CcpRig::C_READSTR_FN))
            return "<no question: " + vis(r.out.substr(from)) + ">";
        r.to(answer, "\r\nA>", ms(120000));
        return r.out.substr(from);
    }
} // namespace

TEST(diag_on_the_whole_machine_passes_and_is_golden)
{
    DiagRig r;
    REQUIRE(r.ok);
    const uint64_t c0 = r.cpu->cycles();
    const std::string got = run(r, "m:diag");
    std::printf("  a quick run: %.1f s of the machine's time\n",
                static_cast<double>(r.cpu->cycles() - c0) / 7'159'090.0);
    CHECK_EQ(count(got, ". FAIL"), size_t{0});
    CHECK_EQ(r.code(), 0u);
    CHECK(ends(line_of(got, "NB5"), "PASS"));
    CHECK(ends(line_of(got, "NB6"), "PASS"));
    CHECK(got.find("SUMMARY  49 tests: 32 passed, 0 failed, 12 skipped, 5 measured") != std::string::npos);
    CHECK(golden("diag.txt", normal(got)));
}

TEST(a_stuck_bit_in_block_40_fails_m4_and_says_where)
{
    DiagRig r;
    REQUIRE(r.ok);
    FaultyMemory f;
    REQUIRE(fault_at(r, f, 0xA1234, 0xFF, 0x08)); // bit 3 stuck at 1
    const std::string got = run(r, "m:diag mem");
    CHECK(ends(line_of(got, "M4"), "FAIL"));
    CHECK_EQ(detail_of(got, "M4"), std::string("       block 40 +$1234 ($A1234): read $08, expected $00: bit 3"));
    CHECK_EQ(count(got, ". FAIL"), size_t{1});
    CHECK_EQ(r.code(), 1u);
    if (count(got, ". FAIL") != 1)
        std::printf("%s\n", got.c_str());
}

TEST(a_stuck_bit_in_block_8_fails_m2)
{
    DiagRig r;
    REQUIRE(r.ok);
    FaultyMemory f;
    REQUIRE(fault_at(r, f, 0x20123, 0xBF, 0x00)); // bit 6 stuck at 0
    const std::string got = run(r, "m:diag mem");
    CHECK(ends(line_of(got, "M2"), "FAIL"));
    CHECK_EQ(detail_of(got, "M2"), std::string("       block 8 +$0123 ($20123): read $2A, expected $6A: bit 6"));
    CHECK(ends(line_of(got, "M3"), "FAIL")); // the march writes $FF there and reads $BF
    CHECK(ends(line_of(got, "M4"), "PASS"));
    CHECK_EQ(r.code(), 2u);
    if (!ends(line_of(got, "M2"), "FAIL"))
        std::printf("%s\n", got.c_str());
}

TEST(an_aperture_fault_fails_v4)
{
    // Slot 1's window, block 33 ($84000-$87FFF): what the CPU reads through it, not what the port reads.
    DiagRig r;
    REQUIRE(r.ok);
    FaultyMemory f;
    REQUIRE(fault_at(r, f, 0x84010, 0xFE, 0x00)); // bit 0 stuck at 0
    const std::string got = run(r, "m:diag video");
    CHECK(ends(line_of(got, "V4"), "FAIL"));
    const std::string d = detail_of(got, "V4");
    CHECK(d.find("the port to slot 1, card +$0010: read $") != std::string::npos);
    CHECK(ends(d, ": bit 0"));
    CHECK(ends(line_of(got, "V2"), "PASS"));
    CHECK_EQ(r.code(), 1u);
    if (!ends(d, ": bit 0"))
        std::printf("%s\n", got.c_str());
}

TEST(no_sound_card_skips_every_sound_test)
{
    DiagRig r(without("sound"));
    REQUIRE(r.ok);
    const std::string got = run(r, "m:diag sound");
    for (const char *t : {"S1", "S2", "S3", "S4", "S5", "S7"})
        CHECK(ends(line_of(got, t), "SKIP (no card)"));
    CHECK(ends(line_of(got, "S6"), "SKIP (/FULL)"));
    CHECK(ends(line_of(got, "S8"), "SKIP (/I)"));
    CHECK(got.find("SOUND\n") != std::string::npos); // no ID after the heading
    CHECK_EQ(count(got, ". FAIL"), size_t{0});
    CHECK_EQ(r.code(), 0u);
    if (!ends(line_of(got, "S2"), "SKIP (no card)"))
        std::printf("%s\n", got.c_str());
}

TEST(a_sound_card_held_by_another_is_skipped_in_use)
{
    DiagRig r;
    REQUIRE(r.ok);
    const uint32_t sclaim = SYS_BLOCK_PHYS + sym("SCLAIM");
    r.poke(sclaim, 0x01); // the 6510 claimed (SC_6510), as a resident program might hold it
    const std::string got = run(r, "m:diag sound");
    CHECK(ends(line_of(got, "S1"), "PASS"));
    for (const char *t : {"S2", "S3", "S4", "S5", "S7"})
        CHECK(ends(line_of(got, t), "SKIP (in use)"));
    CHECK_EQ(count(got, ". FAIL"), size_t{0});
    if (!ends(line_of(got, "S2"), "SKIP (in use)"))
        std::printf("%s\n", got.c_str());
}

TEST(a_stuck_bit_in_the_sound_cards_ram_fails_s4)
{
    DiagRig r;
    REQUIRE(r.ok);
    r.machine->sound_card()->set_ram_fault(0x5678, 0xFB, 0x00); // bit 2 stuck at 0
    const std::string got = run(r, "m:diag sound");
    CHECK(ends(line_of(got, "S4"), "FAIL"));
    CHECK_EQ(detail_of(got, "S4"), std::string("       card RAM $5678: read $2A, expected $2E: bit 2"));
    CHECK(ends(line_of(got, "S3"), "PASS"));
    CHECK_EQ(r.code(), 1u);
    r.machine->sound_card()->clear_ram_fault();
    if (!ends(line_of(got, "S4"), "FAIL"))
        std::printf("%s\n", got.c_str());
}

TEST(write_answered_n_leaves_the_card_as_it_was)
{
    DiagRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> before = r.media->bytes();
    const std::string got = answered(r, "m:diag sd /write", "N");
    CHECK(ends(line_of(got, "SD4"), "SKIP (not confirmed)"));
    CHECK(r.media->bytes() == before);
    if (!ends(line_of(got, "SD4"), "SKIP (not confirmed)"))
        std::printf("%s\n", vis(got).c_str());
}

TEST(write_answered_y_writes_reads_back_and_erases)
{
    DiagRig r;
    REQUIRE(r.ok);
    // A:'s directory (the card's unit 1): the entries in use, and DIAG$$$.TMP's.
    auto entries = [&r] {
        CpmDisk disk{r, 1};
        const std::vector<uint8_t> d = disk.directory();
        unsigned used = 0, tmp = 0;
        for (unsigned i = 0; i < disk.entries(); ++i)
            if (d[i * 32] < 16)
            {
                ++used;
                tmp += std::string(d.begin() + i * 32 + 1, d.begin() + i * 32 + 12) == "DIAG$$$ TMP";
            }
        return std::make_pair(used, tmp);
    };
    const auto before = entries();
    const std::string got = answered(r, "m:diag sd /write", "Y");
    CHECK(ends(line_of(got, "SD4"), "PASS"));
    const auto after = entries();
    CHECK_EQ(after.first, before.first);
    CHECK_EQ(after.second, 0u);
    CHECK_EQ(r.code(), 0u);
    if (!ends(line_of(got, "SD4"), "PASS"))
        std::printf("%s\n", vis(got).c_str());
}

TEST(p3_halts_on_its_read_and_cont_goes_on)
{
    DiagRig r;
    REQUIRE(r.ok);
    ConsoleCard *card = r.machine->console_card();
    REQUIRE(card != nullptr);
    r.cpu->set_observer(card);
    const size_t from = r.out.size();
    type_halting(r, "m:diag panel /full /i\r");
    REQUIRE(until_halting(r, "Y or N? ", ms(30000), from)); // P2's question
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));                 // (nothing is armed yet: Rig::run is enough)
    type_halting(r, "Y\r");
    REQUIRE(until_halting(r, "then CONT on the panel", ms(30000), from));
    REQUIRE(r.calls(CcpRig::C_READSTR_FN)); // in function 10: a key typed sooner, 11 would drop
    type_halting(r, "\r");
    bool halted = false;
    for (int i = 0; i < 200 && !halted; ++i)
        halted = !run_halting(r, 10'000);
    CHECK(halted);
    CHECK(card->halted());
    card->press(ConsoleCard::CONT);
    CHECK(until_halting(r, "\r\nA>", ms(30000), from + 10));
    const std::string got = r.out.substr(from);
    CHECK(ends(line_of(got, "P3"), "PASS"));
    CHECK(ends(line_of(got, "P2"), "PASS"));
    CHECK_EQ(card->peek(ConsoleCard::BPCTRL), 0);
    CHECK_EQ(card->peek(ConsoleCard::CTRL) & ConsoleCard::SHOW_DISPLAY, 0);
    CHECK_EQ(r.code(), 0u);
    if (!ends(line_of(got, "P3"), "PASS") || r.code() != 0)
        std::printf("%s\n", vis(got).c_str());
}

TEST(a_ctrl_c_at_p3_leaves_no_breakpoint_armed)
{
    // Review CC3: the breakpoint is armed and P3 waits for the user; a ^C then (console mode bit 3 set: no warm
    // boot from function 10) ends DIAG with the panel put back. A program that reads the armed byte runs on.
    DiagRig r;
    REQUIRE(r.ok);
    ConsoleCard *card = r.machine->console_card();
    REQUIRE(card != nullptr);
    r.cpu->set_observer(card);
    const size_t from = r.out.size();
    type_halting(r, "m:diag panel /full /i\r");
    REQUIRE(until_halting(r, "Y or N? ", ms(30000), from));
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));
    type_halting(r, "Y\r");
    REQUIRE(until_halting(r, "then CONT on the panel", ms(30000), from));
    REQUIRE(r.calls(CcpRig::C_READSTR_FN));
    std::smatch m;
    const std::string said = r.out.substr(from);
    REQUIRE(std::regex_search(said, m, std::regex("halt at a read of \\$([0-9A-F]{5})")));
    const unsigned armed = static_cast<unsigned>(std::stoul(m[1], nullptr, 16));
    CHECK(armed >= 0x8000 && armed < 0xBF00); // SWEEP reads it
    CHECK_EQ(card->peek(ConsoleCard::BPCTRL), ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ);
    type_halting(r, "\x03\r");
    CHECK(until_halting(r, "\r\nA>", ms(30000), from + 10));
    CHECK(!card->halted());
    CHECK_EQ(card->peek(ConsoleCard::BPCTRL), 0);
    CHECK_EQ(card->peek(ConsoleCard::CTRL) & ConsoleCard::SHOW_DISPLAY, 0);
    CHECK_EQ(r.code(), 0xFFFEu); // ended by the ^C
    const size_t at = r.out.size();
    type_halting(r, "m:sweep\r");
    const bool ended = until_halting(r, "\r\nA>", ms(30000), at + 8);
    CHECK(ended);
    CHECK(!card->halted());
    if (!ended || card->halted())
        std::printf("  SWEEP halted on the armed byte $%05X: %s\n", armed, vis(r.out.substr(from)).c_str());
}

TEST(the_rules_gate_holds_both_code_segments)
{
    const std::string com = UTILS + "/diag.com", map = UTILS + "/diag.map";
    const std::string gate = std::string(EMU_PYTHON) + " -I " + EMU_OODOS_SRC + "/tools/rules.py ";
    CHECK_EQ(std::system((gate + com + " --map " + map + " --code CODE,CODE2 >/dev/null").c_str()), 0);
    // An RMB ($07, rule 1) at CODE2's first byte, $8000 (the file's +16 is $0300): the gate must see it.
    std::vector<uint8_t> b = read_bytes(com);
    const size_t code2 = 0x8000 - 0x0300 + 16;
    REQUIRE(b.size() > code2);
    b[code2] = 0x07;
    const std::string bad = UTILS + "/diag-rmb.com";
    std::ofstream(bad, std::ios::binary).write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    CHECK(std::system((gate + bad + " --map " + map + " --code CODE,CODE2 >/dev/null 2>&1").c_str()) != 0);
    std::remove(bad.c_str());
}

int main()
{
    return run_all();
}
