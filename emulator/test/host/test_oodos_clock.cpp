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
    constexpr double TICK = 7159.0;                          ///< cycles a millisecond: timer 1's period
    constexpr double FRAME = 7159090.0 * 800 * 525 / 25175000; ///< cycles a frame: 640x480 at 59.94 Hz
    constexpr uint32_t HOOKS = 5 * BLOCK;                    ///< L1TEST's hook records, block 5
    constexpr uint16_t VIA_IER = 0xC34E, VIDEO_IEN = 0xC484, VIA_T1LL = 0xC346, VIA_T1LH = 0xC347, VIA_ACR = 0xC34B;
    constexpr unsigned TICK_LATCH = 7157; ///< sys.inc's: timer 1's latch, free-running
    constexpr unsigned EV_LAYOUT = 0, EV_CLOCKS = 1, EV_SIZE = 2, EV_MS = 4, EV_FRAMES = 8; ///< oodos.inc's

    uint32_t word32(Rig &r, uint32_t a)
    {
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    uint32_t ms_count(Rig &r) { return word32(r, SYS_BLOCK_PHYS + sym("MS")); }
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
        /// Both within `ms_slack` ms and `frame_slack` frames of the time, said either way.
        void check(const char *what, double ms_slack, double frame_slack) const
        {
            std::printf("  %s: %ld ms counted, %.1f in the cycles; %ld frames, %.1f\n", what, ms(), ms_want(), frames(),
                        frames_want());
            CHECK(std::abs(static_cast<double>(ms()) - ms_want()) <= ms_slack);
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
    s.check("3 s at the prompt", 1, 1);
    // And through command lines: the CCP, the BDOS and the console in turn.
    Span t(r);
    for (int i = 0; i < 4; ++i)
        r.cmd("dir m:");
    t.check("four DIR M:s", 1, 1);
}

TEST(function_80_says_oolib_s_version_and_72_keeps_its_10_bytes)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("l1test.com", " I");
    CHECK_EQ(field(o, "INFO"), std::string("02 03"));        // interface 2 (L2), frame and timing, input
    CHECK_EQ(field(o, "INFOB"), std::string("02030302"));    // and both clocks, layouts to 2
    const std::string v = field(o, "VER");
    CHECK_EQ(v.size(), size_t{24});
    CHECK_EQ(v.substr(0, 4), std::string("0004"));            // OODOS 0.4 (question 15)
    CHECK_EQ(v.substr(20), std::string("EEEE"));              // 10 bytes, as on main: 11 and 12 untouched
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
    CHECK_EQ(a[EV_CLOCKS], 3);
    CHECK_EQ(a[EV_SIZE], 32);
    bool zeros = a[3] == 0;
    for (size_t i = 12; i < 32; ++i)
        zeros = zeros && a[i] == 0; // the $5A it was filled with gone: reserved, and past layout 1
    CHECK(zeros);
    const uint32_t ams = le32(a, EV_MS), cms = le32(c, 0);
    CHECK(ams <= cms && cms - ams <= 100); // the area as FT_COUNT says, its dump's printing apart (19,200 baud)
    CHECK(le32(a, EV_FRAMES) <= le32(c, 4) && le32(c, 4) - le32(a, EV_FRAMES) <= 2);
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
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0xC0); // timer 1 alone (bit 7 reads 1)
    CHECK_EQ(r.machine->bus().peek(VIDEO_IEN), 0x01);
    s.check("after the warm boot", 1, 1);
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
    s.check("the hooks' run", 2, 1);
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
    s.check("2 s of full-budget hooks", 2, 1);
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
    s.check("PIP of 36 KB from A:", 1, 1);
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
    CHECK_EQ(field(o, "ALIVE"), std::string("02 03")); // FT_INFO answers: OOLIB 2, frame and timing and input
    CHECK_EQ(field(o, "VIEN VIER"), std::string("01 C0")); // vertical blank and timer 1 alone
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0xC0);
    s.check("through it", 2, 1);
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
    s.check("120 lines scrolled", 1, 1);
}

TEST(a_stopped_timer_ends_a_delay_and_the_warm_boot_starts_it_again)
{
    // A program stops timer 1 (against the rules): its enable off, one-shot, another latch. FT_DELAY
    // gives up (A = X = $FF) when the ms counter has stood still for 4 frames, rather than waiting for
    // ever; the warm boot puts timer 1 back as cold boot had it, and the counter goes on from where it was.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("l1test.com", " T");
    CHECK_EQ(field(o, "DELAY"), std::string("FF FF"));
    const std::vector<uint8_t> t0 = hexbytes(field(o, "T0")), t1 = hexbytes(field(o, "T1"));
    REQUIRE(t0.size() == 8 && t1.size() == 8);
    const long frames = static_cast<long>(le32(t1, 4) - le32(t0, 4)), msd = static_cast<long>(le32(t1, 0) - le32(t0, 0));
    std::printf("  FT_DELAY with timer 1 stopped: %ld frames, %ld ms counted\n", frames, msd);
    CHECK(frames >= 4 && frames <= 8); // its 4 frames, and T0's printing
    CHECK(msd <= 20);                  // T0's line, at 19,200 baud, before the timer stopped
    CHECK_EQ(r.machine->bus().peek(VIA_ACR), 0x40);
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0xC0);
    CHECK_EQ(r.machine->bus().peek(VIA_T1LL) | r.machine->bus().peek(VIA_T1LH) << 8, static_cast<int>(TICK_LATCH));
    const uint32_t before = ms_count(r);
    r.run(ms(50)); // timer 1 reloads from the latch at its next expiry (up to 9 ms, from $FFFF)
    Span s(r);
    r.run(ms(1000));
    s.check("after the warm boot", 1, 1);
    CHECK(ms_count(r) >= le32(t1, 0) && ms_count(r) - before >= 1000); // from where it was: not cleared
    const std::string d = r.go("l1test.com", " D");
    CHECK_EQ(field(d, "DELAY"), std::string("00 00")); // and a delay waits again
}

TEST(a_changed_acr_is_put_back_by_the_warm_boot_losing_no_tick)
{
    // A program changes ACR (timer 1 free-running still, now on PB7 too) and writes timer 1's enable
    // again, the latch left alone: the warm boot puts ACR back, and with the latch as it was writes no
    // T1LH (which would clear a tick due then), so the counter keeps time across it to the tick.
    CcpRig r;
    REQUIRE(r.ok);
    r.run(ms(100));
    Span s(r);
    const std::string o = r.go("l1test.com", " U");
    CHECK(o.find("\r\nA>") != std::string::npos);
    CHECK_EQ(r.machine->bus().peek(VIA_ACR), 0x40);
    CHECK_EQ(r.machine->bus().peek(VIA_IER), 0xC0);
    CHECK_EQ(r.machine->bus().peek(VIA_T1LL) | r.machine->bus().peek(VIA_T1LH) << 8, static_cast<int>(TICK_LATCH));
    r.run(ms(500));
    s.check("across the warm boot", 1, 1);
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

TEST(without_the_via_the_6551_s_interrupt_is_turned_off_and_its_key_kept)
{
    // No VIA: no millisecond clock, and ID 6's interrupt is the 6551's, its registers mirrored where the
    // VIA's would be. A program turns its receive interrupt on (against the rules) and reads a key: the
    // dispatcher mustn't take the VIA's paths (they would write the 6551's command and status at $C34E
    // and $C34D, and read its data at $C344 as timer 1's), but turn the interrupt off and leave the key.
    const std::string text = no_via_machine();
    REQUIRE(!text.empty());
    CcpRig r({}, text);
    REQUIRE(r.ok);
    REQUIRE(r.phys(SYS_BLOCK_PHYS + sym("CLOCKS")) == 0x01); // the frame clock alone
    const size_t from = start(r, "l1test.com", " R");
    CHECK(r.calls(1)); // function 1, C_READ
    r.type("Q");
    CHECK(r.until("\r\nA>", ms(2000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "KEY"), std::string("51 00"));
    CHECK(!field(o, "CMD").empty() && (hx(field(o, "CMD").substr(0, 2)) & 0x02) != 0); // off again
    CHECK_EQ(ms_count(r), 0u);
    const std::string d = r.cmd("dir m:"); // the serial line still both ways: echoed, answered, prompted
    CHECK(d.find("dir m:\r\n") == 0);
    CHECK(d.size() > 12 && d.substr(d.size() - 4) == "\r\nA>");
}

int main() { return run_all(); }
