/**
 * @file test_oodos_clock.cpp
 * @brief OOLIB's L1 (hw/oolib.md, "The plan"): OODOS's interrupt dispatcher, its clocks, the event
 * area, the hooks and function 80, on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h), and L1TEST (oodos/sys/test/l1test.s)
 * run by GO. The counters are read behind OODOS's back, in the system block (irq.s's MS and FRAMES)
 * and in the program's event area, against the emulated time: the CPU's cycles, 7,159,090 a second,
 * whatever the host's speed. test_oodos_clock_disk2 has the Disk II's catch-up (it needs the
 * fixtures), test_oodos_clock_debug the debug build's hook costs.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr double TICK = 78750.0 / 11;                    ///< cycles a millisecond: the bridge's tick (N10)
    constexpr double FRAME = 7159090.0 * 800 * 525 / 25175000; ///< cycles a frame: 640x480 at 59.94 Hz
    constexpr uint32_t HOOKS = 5 * BLOCK;                    ///< L1TEST's hook records, block 5
    constexpr uint16_t VIA_IER = 0xC34E, VIDEO_IEN = 0xC484, VIA_T1LL = 0xC346, VIA_T1LH = 0xC347, VIA_ACR = 0xC34B;
    constexpr unsigned EV_LAYOUT = 0, EV_CLOCKS = 1, EV_SIZE = 2, EV_MS = 4, EV_FRAMES = 8; ///< oodos.inc's

    uint32_t word32(Rig &r, uint32_t a)
    {
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    uint32_t frame_count(Rig &r) { return word32(r, SYS_BLOCK_PHYS + sym("FRAMES")); }
    unsigned word16(Rig &r, uint32_t a) { return r.phys(a) | r.phys(a + 1) << 8; }
    uint16_t l1(const std::string &name) { return labels(SYS + "/l1test.lbl").at(name); }

    /// The clocks over a stretch of emulated time: what they counted, and what the cycles say.
    struct Span
    {
        Rig &r;
        uint64_t c0;
        uint32_t m0, f0;
        explicit Span(Rig &rig) : r(rig), c0(rig.cpu->cycles()), m0(ms_count(rig)), f0(frame_count(rig)) {}
        double ms_want() const { return static_cast<double>(r.cpu->cycles() - c0) / TICK; }
        double frames_want() const { return static_cast<double>(r.cpu->cycles() - c0) / FRAME; }
        long ms() const { return static_cast<long>(ms_count(r) - m0); }
        long frames() const { return static_cast<long>(frame_count(r) - f0); }
        /// The frames within `frame_slack` of the time. The ms are printed as a record only: since OODOS 0.16 the
        /// counter is the bridge's, which counts the cycles whatever OODOS does, so a check of it can't fail (review
        /// r1's T8).
        void check(const char *what, double frame_slack) const
        {
            std::printf("  %s: %ld ms counted, %.1f in the cycles; %ld frames, %.1f\n", what, ms(), ms_want(), frames(),
                        frames_want());
            CHECK(std::abs(static_cast<double>(frames()) - frames_want()) <= frame_slack);
        }
    };

    /// Hex digits as a number, 0 if they aren't (a missing field fails its own check, not the suite).
    unsigned hx(const std::string &s)
    {
        if (s.empty() || s.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos)
            return 0;
        return static_cast<unsigned>(std::stoul(s, nullptr, 16));
    }
    std::vector<uint8_t> hexbytes(const std::string &s)
    {
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < s.size(); i += 2)
            out.push_back(static_cast<uint8_t>(hx(s.substr(i, 2))));
        return out;
    }
    uint32_t le32(const std::vector<uint8_t> &b, size_t at)
    {
        return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
    }
} // namespace

TEST(the_clocks_follow_emulated_time_at_the_prompt)
{
    // The CCP waits for a line in the BDOS, which runs with interrupts on: each tick and each vertical
    // blank counted (with the BDOS masked, as before L1, neither would move).
    CcpRig r;
    REQUIRE(r.ok);
    r.run(ms(100));
    Span s(r);
    r.run(ms(3000));
    s.check("3 s at the prompt", 1);
    // And through command lines: the CCP, the BDOS and the console in turn.
    Span t(r);
    for (int i = 0; i < 4; ++i)
        r.cmd("dir m:");
    t.check("four DIR M:s", 1);
}

TEST(function_80_says_oolib_s_version_and_72_keeps_its_10_bytes)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("l1test.com", " I");
    CHECK_EQ(field(o, "INFO"), std::string("12 2F"));        // interface 18 (82 renumbered), frame and timing, input, sound, DMA, serial
    CHECK_EQ(field(o, "INFOB"), std::string("122F0702"));    // both clocks and FC_T1 (a VIA), layouts to 2
    const std::string v = field(o, "VER");
    CHECK_EQ(v.size(), size_t{24});
    CHECK_EQ(v.substr(0, 4), std::string("0012"));            // OODOS 0.18 (question 15)
    CHECK_EQ(v.substr(20), std::string("EEEE"));              // 10 bytes, as on main: 11 and 12 untouched
}

TEST(ft_clock_reads_the_time_to_the_ms)
{
    // O8 (the random source's R6; nb3 N12): FT_CLOCK, with no block refused; with one, the DAT and the seconds as
    // T_GET gives them, and the ms into the second from the same latch: two calls with a 250 ms FT_DELAY between are
    // 250-300 ms apart by their own seconds and ms (the first's line is printed between). FK_SYNC set, FK_LOST
    // clear. Past 2155 refused, as T_GET. Must fail with MSSL read before SEC (the last latch's ms: the two calls'
    // ms the same).
    CcpRig r;
    REQUIRE(r.ok);
    // Past the clock chip's first edge since CcpRig set it: SYNC is set by an edge's read and lasts 1.1 s, and
    // CcpRig's set_time restarts the chip's second but not the bridge's SYNC, so before that edge SYNC is whatever an
    // edge before the set left (review r1's C2/T9: with the host's clock it hung on the host's milliseconds; the rig's
    // fixed time makes it the same each run, but the case doesn't lean on it).
    r.run(ms(1500));
    const std::string o = r.go("l1test.com", " C");
    CHECK_EQ(field(o, "CNB"), std::string("FF FF"));
    CHECK_EQ(field(o, "CLOCK"), std::string("00 00"));
    CHECK_EQ(field(o, "CLOCK2"), std::string("00 00"));
    const std::string a = field(o, "CLK1"), b = field(o, "CLK2");
    REQUIRE(a.size() == 16 && b.size() == 16);
    auto byte = [](const std::string &s, size_t i) { return static_cast<unsigned>(std::stoul(s.substr(2 * i, 2), nullptr, 16)); };
    auto when = [&](const std::string &s) {
        const unsigned days = byte(s, 0) | byte(s, 1) << 8, h = byte(s, 2), m = byte(s, 3), sec = byte(s, 4);
        const unsigned ms = byte(s, 5) | byte(s, 6) << 8;
        CHECK(ms < 1000);
        CHECK_EQ(byte(s, 7), 0x01u); // FK_SYNC, not FK_LOST
        auto bin = [](unsigned v) { return (v >> 4) * 10 + (v & 15); };
        return ((static_cast<uint64_t>(days) * 24 + bin(h)) * 60 + bin(m)) * 60'000 + bin(sec) * 1000 + ms;
    };
    const uint64_t t1 = when(a), t2 = when(b);
    std::printf("  CLK1 %s, CLK2 %s: %lld ms apart\n", a.c_str(), b.c_str(), static_cast<long long>(t2 - t1));
    CHECK(t2 >= t1 + 250 && t2 <= t1 + 300);
    // 2026-10-30 is day 17,835 from 1 January 1978 (day 1); CcpRig set 12:00:00 at its boot.
    CHECK_EQ(byte(a, 0) | byte(a, 1) << 8, 17'835u);
    CHECK_EQ(byte(a, 2), 0x12u);
    {
        CcpRig late;
        REQUIRE(late.ok);
        late.set_time(2156, 1, 1, 0, 0, 0);
        const std::string p = late.go("l1test.com", " C");
        CHECK_EQ(field(p, "CLOCK"), std::string("FF FF"));
    }
}

TEST(the_event_area_is_refused_where_it_cant_be_and_written_where_it_can)
{
    CcpRig r;
    REQUIRE(r.ok);
    const uint16_t area = l1("area");
    const size_t from = start(r, "l1test.com", " A");
    // While it's open (OPEN said, CLOSE not yet): the area's counters are OODOS's, to the tick.
    unsigned samples = 0, off = 0;
    for (int i = 0; i < 200 && r.out.find("CLOSE", from) == std::string::npos; ++i)
    {
        r.run(ms(10));
        // A program reads the area between instructions, never inside the interrupt handler (which runs with
        // interrupts off, the counters and the area brought level before it returns): so the sample is taken
        // at an instruction boundary with I clear.
        for (int n = 0; n < 100'000 && (r.cpu->regs().p & Cpu6502::FLAG_I); ++n)
            r.run(1);
        if (r.out.find("OPEN 00", from) == std::string::npos || r.out.find("DELAY ", from) != std::string::npos)
            continue;
        ++samples;
        const long d = static_cast<long>(ms_count(r)) - static_cast<long>(word32(r, area + EV_MS));
        const long f = static_cast<long>(frame_count(r)) - static_cast<long>(word32(r, area + EV_FRAMES));
        off += d < 0 || d > 1 || f != 0;
    }
    CHECK(r.until("\r\nA>", ms(3000), from));
    const std::string o = r.out.substr(from);
    std::printf("  %u samples while it was open, %u off\n", samples, off);
    CHECK(samples > 20);
    CHECK_EQ(off, 0u);
    for (const char *k : {"R1", "R2", "R3", "R4", "R5", "R6"})
        CHECK_EQ(field(o, k), std::string("FF FF"));
    CHECK_EQ(field(o, "OPEN"), std::string("00 00"));
    CHECK_EQ(field(o, "CLOSE"), std::string("00 00"));
    const std::vector<uint8_t> a = hexbytes(field(o, "AREA")), c = hexbytes(field(o, "COUNT")),
                               after = hexbytes(field(o, "AFTER"));
    REQUIRE(a.size() == 32 && c.size() == 8 && after.size() == 16);
    CHECK_EQ(a[EV_LAYOUT], 1);
    CHECK_EQ(a[EV_CLOCKS], 7); // FC_FRAME, FC_MS and FC_T1 (a VIA: interface 17)
    CHECK_EQ(a[EV_SIZE], 32);
    bool zeros = a[3] == 0;
    for (size_t i = 12; i < 32; ++i)
        zeros = zeros && a[i] == 0; // the $5A it was filled with gone: reserved, and past layout 1
    CHECK(zeros);
    const uint32_t ams = le32(a, EV_MS), cms = le32(c, 0);
    CHECK(ams <= cms && cms - ams <= 100); // the area as FT_COUNT says, its dump's printing apart (19,200 baud)
    // The frames between the two dumps follow the ms between them: a frame each 16.7 ms, and one more
    // for where the vertical blanks fall (the dump's printing takes 40 ms or so, which crosses 2 or 3).
    CHECK(le32(a, EV_FRAMES) <= le32(c, 4) && le32(c, 4) - le32(a, EV_FRAMES) <= (cms - ams) / 16 + 1);
    // Closed: 300 ms on, the area as it was at the close.
    const uint32_t closed = le32(after, EV_MS);
    CHECK(closed >= cms && closed - cms <= 100);
    CHECK(ms_count(r) - closed >= 300);
}

TEST(wait_for_frames_and_delay_by_milliseconds)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string w = r.go("l1test.com", " W");
    const std::vector<uint8_t> w0 = hexbytes(field(w, "W0")), w1 = hexbytes(field(w, "W1"));
    REQUIRE(w0.size() == 8 && w1.size() == 8);
    const long frames = static_cast<long>(le32(w1, 4) - le32(w0, 4)), msd = static_cast<long>(le32(w1, 0) - le32(w0, 0));
    std::printf("  FT_WAIT of 30 frames: %ld frames, %ld ms\n", frames, msd);
    CHECK(frames >= 30 && frames <= 31);
    // W0 and W1 are read about 18 ms of printing apart (W0's dump and WAIT's line at 19,200 baud), and the wait
    // ends at a vertical blank, so it may end a frame later than the frames alone say: the ms bound follows the
    // frames counted. At least 29 whole frames, 484 ms, whatever the phase: 480 holds.
    CHECK(msd >= 480 && msd <= static_cast<long>(static_cast<double>(frames) * 16.68 + 25));
    CHECK(field(w, "WAIT").size() == 5);
    const std::string d = r.go("l1test.com", " D");
    const long d0 = static_cast<long>(hx(field(d, "D0").substr(3, 2) + field(d, "D0").substr(0, 2)));
    const long d1 = static_cast<long>(hx(field(d, "D1").substr(3, 2) + field(d, "D1").substr(0, 2)));
    const long took = (d1 - d0 + 65536) % 65536;
    std::printf("  FT_DELAY of 1000 ms: %ld ms\n", took);
    CHECK(took >= 1000 && took <= 1040); // and D0's and DELAY's lines printed in between
    CHECK_EQ(field(d, "DELAY"), std::string("00 00"));
}

/// L1TEST H with `how`: the hooks' records cleared first (block 5 and block 1's $4000, which W1 has
/// while the program runs, sentinels), run to the prompt.
static std::string hooks(CcpRig &r, const std::string &how)
{
    r.poke(1 * BLOCK, std::vector<uint8_t>(32, 0x99));
    return r.go("l1test.com", " H" + how, ms(8000));
}

static void hooks_ran(CcpRig &r, const std::string &o, const char *what)
{
    CHECK_EQ(field(o, "FHOOK"), std::string("00 00"));
    CHECK_EQ(field(o, "THOOK"), std::string("00 00"));
    CHECK_EQ(field(o, "BAD"), std::string("00 00"));
    const std::string z = field(o, "FZ TZ");
    REQUIRE(z.size() == 5);
    const unsigned fz = hx(z.substr(0, 2)), tz = hx(z.substr(3, 2));
    std::printf("  %s: %u frame hooks, %u tick hooks (every 10 ms) in about 1 s\n", what, fz, tz);
    CHECK(fz >= 58 && fz <= 63);   // 59.94 a second
    CHECK(tz >= 99 && tz <= 103);  // 100
    // The records, in W1 as the hooks had it (block 5): as many, and a few more run before the warm boot.
    CHECK(word16(r, HOOKS) >= fz && word16(r, HOOKS) <= fz + 2);
    CHECK(word16(r, HOOKS + 0x10) >= tz && word16(r, HOOKS + 0x10) <= tz + 3);
    const std::vector<uint8_t> win = {0, 5, 2, 62}; // the windows the program had when it set them
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK_EQ(r.phys(HOOKS + 4 + i), win[i]);
        CHECK_EQ(r.phys(HOOKS + 0x14 + i), win[i]);
    }
    CHECK((r.phys(HOOKS + 8) & 0x04) == 0);    // the frame hook with interrupts on
    CHECK((r.phys(HOOKS + 0x18) & 0x04) != 0); // the tick hook with them off
    bool kept = true;
    for (uint32_t i = 0; i < 32; ++i)
        kept = kept && r.phys(1 * BLOCK + i) == 0x99; // block 1, the program's own W1: not the hooks'
    CHECK(kept);
}

/// After the warm boot: no hook runs, while the clocks go on.
static void hooks_gone(CcpRig &r)
{
    const unsigned f = word16(r, HOOKS), t = word16(r, HOOKS + 0x10);
    Span s(r);
    r.run(ms(500));
    CHECK_EQ(word16(r, HOOKS), f);
    CHECK_EQ(word16(r, HOOKS + 0x10), t);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("HK")), 0);      // the frame hook's record off
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("HK") + 16), 0); // the tick hook's
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("EVON")), 0);
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0x80); // none of the VIA's on (bit 7 reads 1): the tick is the bridge's (N9)
    CHECK_EQ(r.machine->bus().peek(VIDEO_IEN), 0x01);
    s.check("after the warm boot", 1);
}

TEST(hooks_run_with_their_windows_while_the_bdos_waits)
{
    // The program in FT_DELAY: each hook interrupts the BDOS, whose stack and the program's (the same
    // page index in two blocks) the hook's stack goes under. The program has 100 bytes on its stack at
    // the call, so its S is lower than the BDOS's (fresh at each call): a hook (16 bytes of its own)
    // stacking under the BDOS's S rather than the program's would overwrite them (BAD).
    CcpRig r;
    REQUIRE(r.ok);
    Span s(r);
    const std::string o = hooks(r, "B");
    hooks_ran(r, o, "in the BDOS");
    s.check("the hooks' run", 1);
    hooks_gone(r);
}

TEST(hooks_run_with_their_windows_while_the_program_runs)
{
    // The program spinning with no BDOS call: each hook interrupts it, and its registers, its W1 and a
    // value on its stack are as they were after each.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = hooks(r, "P");
    hooks_ran(r, o, "in the program");
    hooks_gone(r);
}

TEST(the_warm_boot_ends_the_hooks_by_jmp_wboot_by_ctrl_c_and_break)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::string o = hooks(r, "W"); // JMP WBOOT at once
        CHECK_EQ(field(o, "THOOK"), std::string("00 00"));
        hooks_gone(r);
    }
    {
        CcpRig r; // ^C at function 10's prompt
        REQUIRE(r.ok);
        r.poke(1 * BLOCK, std::vector<uint8_t>(32, 0x99));
        const size_t from = start(r, "l1test.com", " HC");
        CHECK(r.calls(CcpRig::C_READSTR_FN));
        r.run(ms(300));
        CHECK(word16(r, HOOKS) > 10); // running meanwhile
        r.type("\x03");
        CHECK(r.until("\r\nA>", ms(2000), from));
        hooks_gone(r);
    }
    {
        // Break (NMI) with the hooks running and the program spinning: the monitor, its interrupts
        // quiet (the ROM's QUIET: the VIA's off, the video card's off) and it answering; then OODOS
        // booted again, with its clocks and no hook.
        CcpRig r;
        REQUIRE(r.ok);
        start(r, "l1test.com", " HN");
        r.run(ms(500));
        CHECK(word16(r, HOOKS) > 20);
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(r.machine->bus().peek(VIA_IER), 0x80);
        CHECK_EQ(r.machine->bus().peek(VIDEO_IEN), 0x00);
        const unsigned f = word16(r, HOOKS);
        r.run(ms(300));
        CHECK_EQ(word16(r, HOOKS), f);
        const size_t m = r.out.size();
        r.type("C800.C802\r"); // the monitor still takes a command
        CHECK(r.until("C800:", ms(1000), m));
        REQUIRE(r.boot());
        hooks_gone(r);
    }
}

TEST(full_budget_hooks_lose_no_tick_and_no_frame)
{
    // An 8,000-cycle frame hook (more than a tick) and a 300-cycle tick hook every millisecond, 2 s: the
    // frame hook runs with interrupts on, so the ticks under it are taken; masked, about one frame in
    // six would lose one.
    CcpRig r;
    REQUIRE(r.ok);
    Span s(r);
    const std::string o = r.go("l1test.com", " B", ms(8000));
    s.check("2 s of full-budget hooks", 1);
    CHECK_EQ(field(o, "FHOOK"), std::string("00 00"));
    CHECK_EQ(field(o, "THOOK"), std::string("00 00"));
    CHECK_EQ(field(o, "COSTS"), std::string("00 00"));        // not counted in this build
    CHECK_EQ(field(o, "COST"), std::string("000000000000")); // and no frame hook still running at a frame
}

TEST(an_sd_read_loses_no_tick)
{
    // 36 KB from A: (an SD unit, by DMA) to M:, through PIP: the BDOS's and the BIOS's work with
    // interrupts on, every tick counted.
    CcpRig r;
    REQUIRE(r.ok);
    r.put_util("pip");
    r.run(ms(100));
    Span s(r);
    const std::string o = r.cmd("m:pip m:=a:pattern.bin");
    CHECK(o.find("Disk") == std::string::npos);
    s.check("PIP of 36 KB from A:", 1);
    CHECK(s.ms() > 200);
}

TEST(an_interrupt_oodos_doesnt_answer_is_turned_off)
{
    // A program turns on the video card's line interrupt and the VIA's timer 2 (against the rules):
    // the dispatcher turns each off and the machine goes on; the warm boot turns off what it turns on
    // again.
    CcpRig r;
    REQUIRE(r.ok);
    Span s(r);
    const std::string o = r.go("l1test.com", " E");
    CHECK_EQ(field(o, "ALIVE"), std::string("12 2F")); // FT_INFO answers: OOLIB 18, frame and timing, input, sound, DMA and serial
    CHECK_EQ(field(o, "VIEN VIER"), std::string("01 80")); // vertical blank alone; none of the VIA's (N9: the tick is the bridge's)
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0x80);
    s.check("through it", 1);
}

TEST(a_scrolling_screen_loses_no_tick)
{
    // Each line at the bottom scrolls the screen, a DMA copy of 4,640 bytes during which the CPU waits
    // (9,280 cycles, more than a tick): in one block it could cover two of timer 1's expiries and lose a
    // tick, about one scroll in three. DMAGO copies 2 KB a block, the tick taken between.
    CcpRig r;
    REQUIRE(r.ok);
    r.cmd("dir m:");
    Span s(r);
    const std::string o = r.go("l1test.com", " S");
    size_t lines = 0;
    for (size_t at = o.find("SCROLL\r\n"); at != std::string::npos; at = o.find("SCROLL\r\n", at + 1))
        ++lines;
    CHECK_EQ(lines, size_t{120});
    CHECK_EQ(r.row(29), std::string("A>")); // the screen scrolled: the prompt on its last row, the warm boot's blank line
    CHECK_EQ(r.row(28), std::string(""));
    CHECK_EQ(r.row(27), std::string("SCROLL"));
    s.check("120 lines scrolled", 1);
}

/// backplane.machine without the VIA (and the joysticks on it): the 6551 answers all of $C300-$C37F.
static std::string no_via_machine()
{
    std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
    for (const char *line : {"\nvia6522 ", "\njoyports "})
    {
        const size_t at = text.find(line);
        if (at == std::string::npos)
            return {};
        text.erase(at + 1, text.find('\n', at + 1) - at);
    }
    return text;
}

TEST(without_the_via_the_6551_s_interrupt_is_served_and_its_key_kept)
{
    // No VIA: ID 6's interrupt is the 6551's, its registers mirrored where the VIA's would be. (Since N9 the
    // millisecond clock is the bridge's, so it runs: FC_MS with the frame clock.) A program turns its receive interrupt on (OODOS's own since the serial receive
    // buffer, so on already) and reads a key: the dispatcher mustn't take the VIA's paths (they would write
    // the 6551's command and status at $C34E and $C34D, and read its data at $C344 as timer 1's), but take
    // the key into the serial line's ring for function 1, the receiver's interrupt left on.
    const std::string text = no_via_machine();
    REQUIRE(!text.empty());
    CcpRig r({}, text);
    REQUIRE(r.ok);
    REQUIRE(r.phys(SYS_BLOCK_PHYS + sym("CLOCKS")) == 0x03); // the frame clock, and the bridge's ms (N9)
    const size_t from = start(r, "l1test.com", " R");
    CHECK(r.calls(1)); // function 1, C_READ
    r.type("Q");
    CHECK(r.until("\r\nA>", ms(2000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "KEY"), std::string("51 00"));
    CHECK(!field(o, "CMD").empty() && (hx(field(o, "CMD").substr(0, 2)) & 0x02) == 0); // OODOS's: on
    CHECK(ms_count(r) > 0u); // the bridge's tick, no VIA needed
    const std::string d = r.cmd("dir m:"); // the serial line still both ways: echoed, answered, prompted
    CHECK(d.find("dir m:\r\n") == 0);
    CHECK(d.size() > 12 && d.substr(d.size() - 4) == "\r\nA>");
}

TEST(the_tick_is_the_bridge_s)
{
    // O1 (hw/design.md, "The north bridge, revision 3", N9). At the prompt the tick is north bridge revision 3's: TBCTL's MSIE
    // set; the VIA has no interrupt of OODOS's on (IER $80: none), ACR $00, and timer 1's latch is as the test
    // left it before the cold boot ($1234: OODOS loads it no more); FT_INFO's clocks have FC_MS, and the
    // millisecond counter follows emulated time. Must fail with CLKINIT still loading timer 1.
    before_boot() = [](Rig &b) {
        b.machine->bus().write(VIA_T1LL, 0x34);
        b.machine->bus().write(VIA_T1LH, 0x12);
    };
    CcpRig r;
    before_boot() = nullptr;
    REQUIRE(r.ok);
    r.run(ms(100));
    CHECK(r.bridge().tbctl() & BackplaneBridge::TB_MSIE);
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0x80);
    CHECK_EQ(r.machine->bus().peek(VIA_ACR), 0x00);
    CHECK_EQ(r.machine->bus().peek(VIA_T1LL) | r.machine->bus().peek(VIA_T1LH) << 8, 0x1234);
    CHECK(r.phys(SYS_BLOCK_PHYS + sym("CLOCKS")) & 0x02); // FC_MS
    Span s(r);
    r.run(ms(1000));
    s.check("the bridge's tick", 1);
}

TEST(timer_1_is_the_program_s)
{
    // O5. T1TEST runs the VIA's timer 1 itself, polled: 64 one-shots, every flag seen; then free-running on PB7
    // for 10 s, a flag each 65,536 cycles, every one seen (none cleared under it by OODOS), while OODOS's tick
    // loses none. The warm boot after puts ACR to $00 and leaves every VIA interrupt off. Must fail with the
    // dispatcher taking timer 1's flag as the tick (its enable on, VIASRV reading T1CL).
    CcpRig r;
    REQUIRE(r.ok);
    Span s(r);
    const std::string o = r.go("t1test.com", "", ms(12'000));
    CHECK_EQ(field(o, "ONE"), std::string("40"));
    const unsigned freed = hx(field(o, "FREE")), msd = hx(field(o, "MS"));
    const double want = msd * (78'750.0 / 11) / 65'536;
    std::printf("  free-running: %u flags in %u ms (%.1f by the cycles a ms)\n", freed, msd, want);
    CHECK(msd >= 10'000 && msd <= 10'010);
    CHECK(std::abs(static_cast<double>(freed) - want) <= 2);
    s.check("timer 1 the program's, 10 s", 1);
    CHECK_EQ(r.machine->bus().peek(VIA_ACR), 0x00);
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0x80);
}

TEST(the_tick_and_another_card_together)
{
    // Flag 5 (N9): the tick and the video card's vertical blank pending at once: L1TEST G holds interrupts off for
    // about 20 ms (SEI, a spin, CLI), so both are waiting at its CLI. The dispatcher's long way serves both: TICK
    // runs, the frame counts, and nothing is DEAFENed (the tick's IRQH bit 5 isn't taken for "any other card"), so
    // MSIE stays on. Must fail with bit 5 left unmasked in both the dispatcher's other-card test and IRQOTH's (it then
    // reaches R62_OTHER, which DEAFENs). Either mask alone, or the long way's JSR TICK alone, costs only a pass: the
    // other mask holds, and the tick is served on the next pass (review r1's T10).
    CcpRig r;
    REQUIRE(r.ok);
    const uint16_t tick = sym("TICK"), deafen = sym("DEAFEN"), gsei = l1("GSEI"), gcli = l1("GCLI");
    unsigned ticks = 0, deafs = 0;
    bool held = false, after = false;
    uint64_t until = 0;
    uint32_t f0 = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == gsei && !held)
        {
            held = true; // interrupts off from here: the frame count before the hold
            f0 = frame_count(r);
        }
        if (pc == gcli && held && !after)
        {
            after = true; // (the interrupts waiting at the CLI are served by now)
            until = r.cpu->cycles() + ms(2);
        }
        if (held && (!after || r.cpu->cycles() < until))
        {
            ticks += pc == tick;
            deafs += pc == deafen;
        }
    };
    start(r, "l1test.com", " G");
    for (int i = 0; i < 100 && !(after && r.cpu->cycles() >= until); ++i)
        r.run(ms(5));
    const uint32_t frames = frame_count(r) - f0;
    r.watch = nullptr;
    std::printf("  after 20 ms held: TICK %u, DEAFEN %u, frames +%u\n", ticks, deafs, frames);
    CHECK(after);
    CHECK(ticks >= 1);
    CHECK_EQ(deafs, 0u);
    CHECK(frames >= 1);
    CHECK(r.bridge().tbctl() & BackplaneBridge::TB_MSIE);
}

TEST(ft_count_is_never_torn)
{
    // O2 (N10). An event area open, so the tick's handler reads MS0 itself (a new latch); L1TEST F calls FT_COUNT in a
    // loop. At each call's SNAPIT the counter is put just before a carry into MS1 (set_ms), and the loop's own
    // period sweeps where in the copy the tick lands. Every result is whole: at least the count at the call and
    // at most the count after, never MS0 from one latch and MS1-MS3 from the handler's. Must fail with SNAPIT's
    // interrupts-off removed, or with MS1 read before MS0.
    CcpRig r;
    REQUIRE(r.ok);
    const uint16_t ftc = sym("FT_COUNTX"), snapit = sym("SNAPIT");
    const uint32_t snap = SYS_BLOCK_PHYS + sym("SNAP");
    bool counting = false, in = false;
    uint32_t lo = 0;
    unsigned calls = 0, torn = 0, landed = 0;
    r.bridge().set_ms(0x00FFFF00);
    r.watch = [&](uint16_t pc) {
        if (pc == ftc)
            counting = true;
        else if (pc == snapit && counting)
        {
            lo = r.bridge().ms() | 0xFF; // the next tick carries
            r.bridge().set_ms(lo);
            in = true;
        }
        else if (pc == ftc + 3 && in)
        {
            const uint32_t v = word32(r, snap), hi = r.bridge().ms();
            ++calls;
            landed += hi != lo;
            if ((v < lo || v > hi) && torn++ < 3)
                std::printf("  FT_COUNT read $%08X between $%08X and $%08X\n", v, lo, hi);
            in = counting = false;
        }
    };
    r.go("l1test.com", " F", ms(30'000));
    r.watch = nullptr;
    std::printf("  %u FT_COUNTs, %u with the tick inside SNAPIT's copy or around it\n", calls, landed);
    CHECK(calls >= 39'000);
    CHECK(landed >= 20);
    CHECK_EQ(torn, 0u);
}

TEST(the_area_s_counter_is_whole_after_ticks_run_together)
{
    // O3 (N10). An event area open (L1TEST GS); as GS turns interrupts off for 3 ms, from its second pass on, the
    // counter is put 2 ms before a carry into MS3, so ticks run together across the carry (OVR). The pass that
    // takes them, at the CLI, leaves the area's EV_MS whole, all four bytes, not the low byte alone. A pass whose
    // hold a vertical blank fell in is left out: the frame's pass writes the area whole itself (EVALL). Must fail
    // with OVR's test removed from TICK's area path.
    CcpRig r;
    REQUIRE(r.ok);
    const uint32_t area = l1("area");
    const uint16_t tick = sym("TICK"), gsei = l1("GSEI"), gcli = l1("GCLI");
    bool held = false, released = false;
    unsigned holds = 0, checked = 0, wrong = 0;
    uint32_t f0 = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == gsei && ++holds >= 2)
        {
            r.bridge().set_ms((r.bridge().ms() | 0x00FFFFFF) - 1);
            f0 = frame_count(r);
            held = true;
        }
        else if (pc == gcli && held)
            released = true; // the merged pass ran at the CLI, just before
        else if (pc == tick && released)
        {
            const uint32_t got = word32(r, area + EV_MS), want = r.bridge().ms() - 1;
            if (frame_count(r) == f0) // no frame in the hold
            {
                ++checked;
                if (got != want && wrong++ < 3)
                    std::printf("  the area's EV_MS $%08X, the bridge's $%08X before this tick\n", got, want);
            }
            held = released = false;
        }
    };
    start(r, "l1test.com", " GS");
    for (int i = 0; i < 100 && holds < 9; ++i)
        r.run(ms(5));
    r.watch = nullptr;
    std::printf("  %u holds across a carry with no frame in them, %u not whole after\n", checked, wrong);
    CHECK(checked >= 3);
    CHECK_EQ(wrong, 0u);
}

TEST(a_late_tick_is_neither_lost_nor_left_flagged)
{
    // Review r1's C1 (with T2). An event area open (L1TEST F). The tick that carries MS0 from $FF to $00 is served
    // late, and the next tick lands inside TICK's area path, between its read of TBCTL and its acknowledge
    // (bridge().inject_tick() there, as one lands: MS on one, MSF, and OVR since MSF is still set). The acknowledge
    // clears only what TICK saw, so OVR stays, and the next tick writes the area whole: at the tick after that,
    // EV_MS is the bridge's count, all four bytes, and OVR is clear again. Must fail with the acknowledge clearing
    // OVR whatever it saw (the area 256 ms behind), or with the OVR path's acknowledge leaving OVR set (every later
    // tick then takes the long way).
    CcpRig r;
    REQUIRE(r.ok);
    const uint32_t area = l1("area");
    const uint16_t tick = sym("TICK");
    uint16_t ack = 0; // the instruction after TICK's BIT NB_TBCTL ($2C $A8 $C6) in its area path
    for (uint16_t p = tick; p < tick + 32 && !ack; ++p)
        if (r.machine->bus().peek(p) == 0x2C && r.machine->bus().peek(static_cast<uint16_t>(p + 1)) == 0xA8 &&
            r.machine->bus().peek(static_cast<uint16_t>(p + 2)) == 0xC6)
            ack = static_cast<uint16_t>(p + 3);
    REQUIRE(ack != 0);
    int phase = 0;
    unsigned after = 0;
    uint32_t got = 0, want = 0;
    uint8_t tb = 0;
    r.watch = [&](uint16_t pc) {
        if (phase == 0 && pc == tick && r.phys(SYS_BLOCK_PHYS + sym("EVON")) != 0) // the area open
        {
            r.bridge().set_ms(r.bridge().ms() | 0xFF); // the next tick is the carry's
            phase = 1;
        }
        else if (phase == 1 && pc == tick && (r.bridge().ms() & 0xFF) == 0)
            phase = 2; // the carry's tick, in TICK
        else if (phase == 2 && pc == ack)
        {
            r.bridge().inject_tick(); // the next tick, inside the window
            phase = 3;
        }
        else if (phase == 3 && pc == tick && ++after == 2)
        {
            got = word32(r, area + EV_MS);
            want = r.bridge().ms() - 1; // what the last tick wrote
            tb = r.bridge().tbctl();
            phase = 4;
        }
    };
    start(r, "l1test.com", " F");
    r.run(ms(100));
    for (int i = 0; i < 100 && phase < 4; ++i)
        r.run(ms(5));
    r.watch = nullptr;
    REQUIRE(phase == 4);
    std::printf("  two ticks after: the area's EV_MS $%08X, the bridge's $%08X; TBCTL $%02X\n", got, want, tb);
    CHECK_EQ(got, want);
    CHECK_EQ(tb & 0x40, 0); // OVR clear: the ticks go back to the short way
}

TEST(a_hook_runs_with_the_win1h_its_program_had_and_the_bdos_gets_win1h_0_back)
{
    // O3 (B7, b7-plan.md §4): W1HTEST (oodos/sys/test/w1htest.s) sets a tick hook (every ms) and a frame hook with
    // WIN1H on $0E and W1 on block 1, so W1 shows the video card's slot 1 at $E04000, then puts WIN1H back to 0
    // and waits 200 ms in the BDOS (FT_DELAY). Each hook reads $4000 and WIN1H: slot 1's byte and $0E, the record's
    // (HK_W1H); and the BDOS's W1 is right after each (HKBACK's WIN1H 0), so the delay ends and $4000 is RAM's.
    CcpRig r;
    REQUIRE(r.ok);
    r.poke(0x4000, 0x5B);   // RAM block 1's
    r.poke(0xE04000, 0xA7); // slot 1's window's
    const std::string o = r.go("w1htest.com", " H", ms(8000));
    CHECK_EQ(field(o, "TB"), std::string("A7"));
    CHECK_EQ(field(o, "TH"), std::string("0E"));
    CHECK_EQ(field(o, "FB"), std::string("A7"));
    CHECK_EQ(field(o, "FH"), std::string("0E"));
    CHECK_EQ(field(o, "B1"), std::string("5B"));
    CHECK(hx(field(o, "TN")) >= 150); // about 200 ticks, and 12 frames
    CHECK(hx(field(o, "FN")) >= 8);
    CHECK_EQ(r.bridge().winh(1), 0);
}

int main() { return run_all(); }
