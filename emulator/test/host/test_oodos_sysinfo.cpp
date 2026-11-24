/**
 * @file test_oodos_sysinfo.cpp
 * @brief SYSINFO.COM and oohw.lib's OS build (hw/diag.md, D1), run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_ccp boots it (oodos_rig.h's CcpRig), the
 * build's SYSINFO.COM put on M: and run as `M:SYSINFO /ALL`. What it prints
 * is held to golden files in golden/ (sysinfo*.txt), after two checks that
 * take out what a measurement makes: the CPU's Hz by the clock chip, within
 * 0.002% of 7,159,090 (the DS3231 counts emulated cycles, so only the poll's
 * grain is left), and by the video frame, within 0.01%; each is written in
 * the golden file as "#,###,### Hz", and the time as "hh:mm:ss".
 *
 *   - the whole machine (sysinfo.txt), its SOUND section from the card's own IDENTIFY (function 82's
 *     SN_IDENTIFY: the firmware's version, build and image, the store, the chips, the features);
 *   - a card left out of the machine's text (the sound card, the USB card,
 *     the console card): its CARDS line says "not found" and its own
 *     section says there is none, and nothing else changes (each its
 *     golden file, and the lines that differ from sysinfo.txt counted);
 *   - a revision changed: the video card at revision 1;
 *   - the floating bus (review CC5): no Disk II card, and a device in its
 *     block that answers each read with the last byte the CPU fetched, as
 *     an empty slot might: HW_PROBE's two reads disagree, so no Disk II is
 *     found;
 *   - function 77 (interface 9): the ROM BIOS's version, $FF $FF with no greeting found, and SYSINFO on an
 *     OODOS that reports interface 8; function 72 as it was (VERTEST);
 *   - A:'s free space, exact, worked out from the card's directory (the golden files hold "#K free");
 *   - one section alone, and the usage line for a word that isn't one.
 *
 * SYSINFO_GOLDEN=update writes the golden files from what SYSINFO prints, which a person does on
 * purpose, after reading them. OODOS_UTILS_DIR names another build of the utilities, to see a case
 * fail on a fault put in on purpose.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    constexpr double NATIVE_HZ = 7'159'090.0;

    /// `line` at the CCP: what it wrote between the line's echo and the prompt, CR LF as \n.
    std::string run(CcpRig &r, const std::string &line)
    {
        const std::string o = r.to(line, "\r\nA>", ms(60000));
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

    /// The two Hz figures checked and taken out, and the time: what the golden file holds.
    /// `want_frame` false: the machine has no video frame to measure by. `rtc_hz` and `rtc_part`: the clock
    /// chip's figure and its bound (a part of `rtc_hz`), for a case whose chip is skewed.
    std::string normal(const std::string &got, bool want_frame = true, double rtc_hz = NATIVE_HZ,
                       double rtc_part = 0.00002)
    {
        std::string s = got;
        const std::regex rtc("([0-9],[0-9]{3},[0-9]{3}) Hz by the clock chip");
        const std::regex frm("([0-9],[0-9]{3},[0-9]{3}) Hz by the video frame");
        std::smatch m;
        auto hz = [](const std::string &t) {
            std::string d;
            for (char c : t)
                if (c != ',')
                    d += c;
            return std::stod(d);
        };
        if (std::regex_search(s, m, rtc))
        {
            const double v = hz(m[1]);
            const bool ok = std::abs(v - rtc_hz) <= rtc_hz * rtc_part;
            std::printf("  %.0f Hz by the clock chip (%+.0f from %.0f)\n", v, v - rtc_hz, rtc_hz);
            CHECK(ok);
            if (!ok)
                std::printf("  by the clock chip: %.0f Hz, not within %.4f%% of %.0f\n", v, rtc_part * 100, rtc_hz);
        }
        else
        {
            std::printf("  no Hz by the clock chip\n");
            CHECK(false);
        }
        if (want_frame)
        {
            if (std::regex_search(s, m, frm))
            {
                const double v = hz(m[1]);
                const bool ok = std::abs(v - NATIVE_HZ) <= NATIVE_HZ * 0.0001;
                std::printf("  %.0f Hz by the video frame\n", v);
                CHECK(ok);
                if (!ok)
                    std::printf("  by the video frame: %.0f Hz, not within 0.01%% of 7,159,090\n", v);
            }
            else
            {
                std::printf("  no Hz by the video frame\n");
                CHECK(false);
            }
        }
        s = std::regex_replace(s, std::regex("[0-9],[0-9]{3},[0-9]{3} Hz by"), "#,###,### Hz by");
        s = std::regex_replace(s, std::regex("[0-9,]+K free"), "#K free"); // what is on the card's drives (EE6)
        s = std::regex_replace(s, std::regex("(CLOCK      [0-9]{4}-[0-9]{2}-[0-9]{2} )[0-9]{2}:[0-9]{2}:[0-9]{2}"),
                               "$1hh:mm:ss");
        return s;
    }

    std::vector<std::string> split(const std::string &s)
    {
        std::vector<std::string> v;
        std::istringstream in(s);
        std::string l;
        while (std::getline(in, l))
            v.push_back(l);
        return v;
    }

    /// The output against golden/NAME; with SYSINFO_GOLDEN=update, stored as it.
    bool golden(const std::string &name, const std::string &got)
    {
        const std::string path = std::string(EMU_GOLDEN_DIR) + "/" + name;
        const char *u = std::getenv("SYSINFO_GOLDEN");
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

    /// How many lines are in one of a and b and not the other (as many times as they are).
    size_t differing(const std::string &a, const std::string &b)
    {
        std::vector<std::string> x = split(a), y = split(b);
        std::sort(x.begin(), x.end());
        std::sort(y.begin(), y.end());
        std::vector<std::string> d;
        std::set_symmetric_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(d));
        return d.size();
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

    /// backplane.machine with rng_chip=`chip` on its north bridge's line (N13: SYSINFO's RANDOM line).
    std::string with_rng(const std::string &chip)
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const std::string line = "northbridge  backplane";
        const size_t at = text.find(line);
        if (at != std::string::npos)
            text.insert(at + line.size(), " rng_chip=" + chip);
        return text;
    }

    /// A rig with the build's SYSINFO on M:.
    struct InfoRig : CcpRig
    {
        explicit InfoRig(const std::string &machine_text = {}, unsigned video_revision = 2)
            : CcpRig({}, machine_text, {}, video_revision)
        {
            if (ok)
                put_util("sysinfo", UTILS);
        }
    };

    /// An empty slot's floating bus, as far as a read goes: the last byte the CPU fetched, which is the
    /// instruction's last byte (an absolute load's high address byte; an indexed one's that crosses a
    /// page re-reads it on the W65C02S). Test-only: hw/diag.md's answer 8.
    struct FloatingBus : Device
    {
        Cpu6502 *cpu{nullptr};
        Decoder *bus{nullptr};
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t) override { return peek(0); }
        uint8_t peek(uint32_t) const override { return bus->peek(static_cast<uint16_t>(cpu->regs().pc - 1)); }
        void write(uint32_t, uint8_t) override {}
    };

    /// A:'s free space (the card's unit 1, oodos-8m), worked out from its directory behind OODOS's back, as
    /// test_oodos_utils_sys's SHOW cases do: (DSM + 1) blocks less the directory's (AL0, AL1) and every
    /// file's, of 4 KB each.
    unsigned a_free_kb(CcpRig &r)
    {
        CpmDisk disk{r, 1};
        const std::vector<uint8_t> d = disk.directory();
        const std::string h = dpb_hex("oodos-8m");
        std::vector<uint8_t> p;
        for (size_t i = 0; i + 1 < h.size(); i += 2)
            p.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
        const unsigned dsm = unsigned{p[5]} | unsigned{p[6]} << 8, al = unsigned{p[9]} << 8 | p[10];
        std::vector<bool> used(dsm + 1, false);
        for (unsigned b = 0; b < 16; ++b)
            if (al & (0x8000u >> b))
                used[b] = true;
        for (unsigned i = 0; i < disk.entries(); ++i)
            if (d[i * 32] < 16)
                for (unsigned k = 0; k < 8; ++k)
                {
                    const unsigned b = unsigned{d[i * 32 + 16 + 2 * k]} | unsigned{d[i * 32 + 17 + 2 * k]} << 8;
                    if (b && b <= dsm)
                        used[b] = true;
                }
        unsigned f = 0;
        for (const bool u : used)
            f += !u;
        return f * disk.bls() / 1024;
    }
    std::string commas(unsigned n)
    {
        const std::string d = std::to_string(n);
        std::string s;
        for (size_t i = 0; i < d.size(); ++i)
            s += (i && (d.size() - i) % 3 == 0 ? "," : "") + std::string(1, d[i]);
        return s;
    }

    /// FT_INFOX's LDA #17 in OODOS's block 60 made LDA #8: an OODOS that reports interface 8, as before function 77.
    void report_interface_8(CcpRig &r)
    {
        const uint32_t base = 60u * BLOCK - 0x4000u + sym("FT_INFOX");
        uint32_t p = base;
        while (p < base + 64 && !(r.phys(p) == 0xA9 && r.phys(p + 1) == 17 && r.phys(p + 2) == 0xA2 && r.phys(p + 3) == 0x2F))
            ++p;
        REQUIRE(p < base + 64);
        r.poke(p + 1, 8);
    }

    std::string full_golden() { return read_text(std::string(EMU_GOLDEN_DIR) + "/sysinfo.txt"); }
} // namespace

TEST(sysinfo_reports_the_whole_machine)
{
    InfoRig r;
    REQUIRE(r.ok);
    const std::string raw = run(r, "m:sysinfo /all");
    const std::string want_a = "DRIVES     A: 8,192K, " + commas(a_free_kb(r)) + "K free"; // the one exact free space
    CHECK(raw.find(want_a) != std::string::npos);
    if (raw.find(want_a) == std::string::npos)
        std::printf("  no '%s'\n", want_a.c_str());
    const std::string got = normal(raw);
    CHECK(golden("sysinfo.txt", got));
    CHECK(got.find("$C4FF") == std::string::npos); // the addresses are the blocks', not the ID bytes'
}

TEST(an_interrupt_at_a_windows_end_is_kept_out_of_the_clock)
{
    // HW_CLOCK's windows (hwclock.s): what makes a window's last change be stamped early, or the change itself
    // late, makes its count wrong, and the next one's wrong the other way. Here a burst of USB events (shift down
    // and up, which type nothing) comes just before the first window's end, for both measurements (the clock
    // chip's second and the 60 frames), so OODOS's USB handler is running when the change comes. One window alone
    // reads outside the tolerance (the clock chip's by about 2,000 cycles); HW_CLOCK keeps the span between the
    // two changes whose gaps say least, which leaves that change out and is within it.
    InfoRig r;
    REQUIRE(r.ok);
    const auto lbl = labels(UTILS + "/sysinfo.lbl");
    REQUIRE(lbl.count("HW_CLOCK") && lbl.count("HW_FRAMEHZ") && lbl.count("HW_WATCH") && lbl.count("HW_EDGES"));
    const uint16_t from = lbl.at("HW_CLOCK"), to = lbl.at("HW_FRAMEHZ");
    const uint16_t watch = lbl.at("HW_WATCH"), edges = lbl.at("HW_EDGES");
    auto peek16 = [&](uint16_t a) {
        return static_cast<uint16_t>(r.machine->bus().peek(a) | r.machine->bus().peek(static_cast<uint16_t>(a + 1)) << 8);
    };
    // The window's end is due a second, or 60 frames, after its start: the burst is put in 2,000 cycles before
    // it, so OODOS's USB handler (16 events a pass, 3,000-3,400 cycles, then again while events wait) is
    // running when the change comes, and the poll that sees it is late.
    unsigned changes = 0, bursts = 0;
    int last = -1;
    uint64_t due = 0;
    UsbCard *usb = r.machine->usb_card();
    REQUIRE(usb != nullptr);
    r.watch = [&](uint16_t pc) {
        if (pc == from)
            changes = 0, last = -1, due = 0; // a measurement starts
        if (due && r.cpu->cycles() >= due)
        {
            due = 0;
            for (int k = 0; k < 24; ++k)
            {
                usb->key(0xE1, true);
                usb->key(0xE1, false);
            }
            ++bursts;
        }
        if (pc < from || pc >= to)
            return;
        const int v = r.machine->bus().peek(peek16(watch));
        if (last >= 0 && v != last && ++changes == 1)
        {
            const bool frames = r.machine->bus().peek(edges) == 60;
            due = r.cpu->cycles() + (frames ? 7'166'199u : 7'159'090u) - 2'000u; // the first window's end, less a little
        }
        last = v;
    };
    const std::string o = run(r, "m:sysinfo system /all");
    r.watch = nullptr;
    CHECK_EQ(bursts, 2u);
    normal(o);
}

TEST(an_interrupt_after_the_last_poll_before_a_change_is_kept_out_of_the_clock)
{
    // HW_CLOCK's stamp (hwclock.s): a change is stamped by the last pass of the poll that saw the byte as it was,
    // so an interrupt taken after that pass's poll, and running when the change comes, doesn't move the stamp.
    // (The first form stamped a change by the timer read at the start of the pass that saw it: such an interrupt
    // put that read after the handler, and OODOS's millisecond handler, landing there or before the read by turns,
    // moved the clock chip's count by 250-500 cycles on BIOS 0.16's first build.) The machine is deterministic, so
    // SYSINFO runs on rigs booted alike: a run finds, for the clock chip's measurement, the second's changes (the
    // bridge's time) and the last poll before each; the next puts a burst of USB events (OODOS's handler,
    // 3,000-3,400 cycles a pass) just after that poll at the first window's end, and the last at the third's too,
    // so the handler runs over the change. With the first form two windows of three were out by the burst, the
    // middle one with them (7,170,533 Hz).
    // The clock chip's figure is held to the rate the skewed chip gives (NATIVE_HZ over 1 + SKEW_PPM ppm), within
    // 0.004% (N13, the owner's answer of 2026-11-22): this case guards the exclusion, not the precision, and its
    // readings scatter about 150 Hz from run to run against a skew of 20 ppm, so the precise cases' 0.002% would
    // fail it by where a stamp falls. A spoiled change kept is several times further out (the mutant: build-log).
    constexpr double SKEW_PPM = 20;
    const double skewed_hz = NATIVE_HZ / (1 + SKEW_PPM * 1e-6);
    constexpr double PART = 0.00004;
    struct Seen
    {
        std::vector<uint64_t> changes; ///< the bridge's seconds changing, from the measurement's start
        std::vector<uint64_t> polls;   ///< the cycle after each poll of the watched byte
        std::string out;
    };
    auto sysinfo = [&](const std::vector<uint64_t> &fire_at, unsigned &fired, Seen &s) {
        InfoRig r;
        REQUIRE(r.ok);
        const auto lbl = labels(UTILS + "/sysinfo.lbl");
        REQUIRE(lbl.count("HW_CLOCK") && lbl.count("HW_FRAMEHZ"));
        const uint16_t from = lbl.at("HW_CLOCK"), to = lbl.at("HW_FRAMEHZ");
        BackplaneBridge *nb = r.machine->backplane_bridge();
        UsbCard *usb = r.machine->usb_card();
        REQUIRE(nb != nullptr && usb != nullptr);
        // The DS3231 20 ppm fast, as a board's TCXO is against the bridge's crystal (N10, the owner's answer of
        // 2026-11-22): in the emulator the bridge's tick and the chip's second come from one clock, so they phase-lock
        // (the tick 0.9 cycles a second against the second, where the VIA's slid 90). Locked, the tick lands at the
        // same place before every SEC change, and its long pass that reads the paddles covered change 0 in every
        // run, a third spoiled change beside this case's two bursts.
        nb->rtc()->set_skew_ppm(SKEW_PPM);
        unsigned calls = 0;
        bool polled = false;
        int sec = -1;
        r.watch = [&](uint16_t pc) {
            if (pc == from)
                ++calls; // the clock chip's measurement is SYSINFO's first; the frames' its second
            if (calls != 1)
                return;
            const uint64_t now = r.cpu->cycles();
            const int v = nb->rtc_time()[0];
            if (sec >= 0 && v != sec)
                s.changes.push_back(now);
            sec = v;
            if (polled)
            {
                s.polls.push_back(now);
                if (std::find(fire_at.begin(), fire_at.end(), now) != fire_at.end())
                {
                    for (int k = 0; k < 24; ++k)
                    {
                        usb->key(0xE1, true);
                        usb->key(0xE1, false);
                    }
                    ++fired;
                }
            }
            polled = pc >= from && pc < to && r.machine->bus().peek(pc) == 0xB2; // LDA (HW_PTR)
        };
        s.out = run(r, "m:sysinfo system /all");
        r.watch = nullptr;
    };
    // A burst's place is found on a run with the bursts before it, since the machine runs on differently after one:
    // three runs, the last with both.
    std::vector<uint64_t> at;
    uint64_t start = 0;
    for (const size_t end : {size_t{1}, size_t{3}})
    {
        Seen s;
        unsigned fired = 0;
        sysinfo(at, fired, s);
        REQUIRE(s.changes.size() > end); // the first window's start, and its ends
        CHECK_EQ(fired, at.size());
        if (at.empty())
        {
            start = s.changes[0];
            normal(s.out, true, skewed_hz, PART); // undisturbed
        }
        else
            CHECK_EQ(s.changes[0], start); // booted alike: the same up to the first burst
        const uint64_t c = s.changes[end];
        const auto p = std::lower_bound(s.polls.begin(), s.polls.end(), c);
        REQUIRE(p != s.polls.begin());
        at.push_back(*(p - 1));
        std::printf("  change %zu at %llu, the last poll before it %llu cycles earlier\n", end,
                    static_cast<unsigned long long>(c), static_cast<unsigned long long>(c - *(p - 1)));
    }
    Seen s;
    unsigned fired = 0;
    sysinfo(at, fired, s);
    CHECK_EQ(fired, 2u);
    REQUIRE(!s.changes.empty());
    CHECK_EQ(s.changes[0], start);
    normal(s.out, true, skewed_hz, PART);
}

TEST(a_card_left_out_loses_its_line_and_its_section)
{
    struct Case
    {
        const char *type, *file, *card;
        size_t lines; ///< the lines that differ from sysinfo.txt
    };
    // Its CARDS line and its section's lines, out; the CARDS line and the section's one, in.
    for (const Case &c : {Case{"sound", "sysinfo_nosound.txt", "sound: not found", 10}, // its section seven lines
                          Case{"usb", "sysinfo_nousb.txt", "USB, game port and speaker: not found", 5},
                          Case{"console", "sysinfo_nopanel.txt", "console (front panel): not found", 4}})
    {
        std::printf("  without the %s card\n", c.type);
        InfoRig r(without(c.type));
        REQUIRE(r.ok);
        const std::string got = normal(run(r, "m:sysinfo /all"));
        CHECK(golden(c.file, got));
        CHECK(got.find(c.card) != std::string::npos);
        const size_t n = differing(got, full_golden());
        CHECK_EQ(n, c.lines);
        if (n != c.lines)
            std::printf("    %zu lines differ from sysinfo.txt, not %zu\n", n, c.lines);
    }
}

TEST(the_random_line_says_what_rn_info_says)
{
    // Random-source O7 (less its revision 2 golden: OODOS 0.16 refuses that bridge): the RANDOM line from function
    // 78's RN_INFO. The golden's ATECC608B on the north bridge (a locked chip, sysinfo.txt); rng_chip=unlocked "not
    // locked", none "none", each one line changed from sysinfo.txt (two in differing()'s count). Must fail with the line's RS bit tested wrongly.
    struct Case
    {
        const char *chip, *file, *line;
    };
    for (const Case &c : {Case{"unlocked", "sysinfo_rngunlocked.txt", "  RANDOM     not locked"},
                          Case{"none", "sysinfo_norng.txt", "  RANDOM     none"}})
    {
        std::printf("  rng_chip=%s\n", c.chip);
        InfoRig r(with_rng(c.chip));
        REQUIRE(r.ok);
        const std::string got = normal(run(r, "m:sysinfo /all"));
        CHECK(golden(c.file, got));
        CHECK(got.find(std::string(c.line) + "\n") != std::string::npos);
        CHECK_EQ(differing(got, full_golden()), size_t{2});
    }
    CHECK(full_golden().find("  RANDOM     ATECC608B on the north bridge\n") != std::string::npos);
}

TEST(a_revision_changed_changes_its_lines)
{
    {
        std::printf("  the video card at revision 1\n");
        InfoRig r({}, 1);
        REQUIRE(r.ok);
        const std::string got = normal(run(r, "m:sysinfo /all"), false);
        CHECK(golden("sysinfo_video1.txt", got));
        CHECK(got.find("  $C480  $91  video") != std::string::npos);
        CHECK(got.find("(not known to SYSINFO)") != std::string::npos);
        CHECK(got.find("VIDEO      revision 1, which OODOS no longer drives") != std::string::npos);
    }
}

TEST(a_floating_bus_in_an_empty_slot_is_no_card)
{
    InfoRig r(without("disk2"));
    REQUIRE(r.ok);
    FloatingBus f;
    f.cpu = r.cpu.get();
    f.bus = &r.machine->bus();
    Mapping m;
    m.first = bus_address(Space::Io, 0x600); // $C600-$C67F: the Disk II's block
    m.last = bus_address(Space::Io, 0x67F);
    m.device = &f;
    m.module = 0xC;
    REQUIRE(r.machine->bus().map(m));
    const std::string got = run(r, "m:sysinfo cards /all");
    CHECK(got.find("  $C600  --   Disk II controller: not found") != std::string::npos);
    CHECK(got.find("$C6  Disk II") == std::string::npos);
    if (got.find("not found") == std::string::npos)
        std::printf("  %s\n", vis(got).c_str());
}

TEST(function_77_gives_the_rom_bios_version_and_72_is_as_it_was)
{
    // VERTEST (oodos/sys/test/vertest.s): FT_INFO says interface 17, 77 the ROM BIOS 0.20 as bios.rom's greeting has
    // it, and 72's 10 bytes come as they always did, the two bytes after the block left alone.
    InfoRig r;
    REQUIRE(r.ok);
    r.put_util("vertest", EMU_OODOS_SYS_DIR);
    const std::string o = run(r, "m:vertest");
    CHECK(o.find("FINFO 11 2F\n") != std::string::npos);
    CHECK(o.find("ROMV 00 14\n") != std::string::npos);
    std::smatch m;
    CHECK(std::regex_search(o, m, std::regex("V72 0010000200044001[0-9A-F]{4}A55A")));
    if (o.find("ROMV 00 14") == std::string::npos || m.empty())
        std::printf("  %s\n", vis(o).c_str());
    CHECK(normal(run(r, "m:sysinfo system /all")).find("  ROM BIOS   0.20\n") != std::string::npos);
}

TEST(function_77_says_ff_ff_when_the_cold_boot_found_no_greeting)
{
    // The ROM's greeting in its RAM copy (block 63) taken away, and OODOS booted again: its cold boot finds none.
    InfoRig r;
    REQUIRE(r.ok);
    const std::string key = "BACKPLANE BIOS ";
    unsigned found = 0;
    for (uint32_t a = 0xFC800; a + key.size() <= 0x100000; ++a)
    {
        bool same = true;
        for (size_t i = 0; i < key.size() && same; ++i)
            same = r.phys(static_cast<uint32_t>(a + i)) == static_cast<uint8_t>(key[i]);
        if (same)
        {
            r.poke(a, 'X');
            ++found;
        }
    }
    REQUIRE(found >= 1);
    REQUIRE(r.boot());
    r.put_util("vertest", EMU_OODOS_SYS_DIR);
    r.put_util("sysinfo", UTILS);
    const std::string o = run(r, "m:vertest");
    CHECK(o.find("ROMV FF FF\n") != std::string::npos);
    if (o.find("ROMV FF FF") == std::string::npos)
        std::printf("  %s\n", vis(o).c_str());
    CHECK(run(r, "m:sysinfo system /all").find("  ROM BIOS   not found by OODOS at its boot\n") != std::string::npos);
}

TEST(sysinfo_on_an_oodos_of_interface_8_says_it_has_no_function_77)
{
    InfoRig r;
    REQUIRE(r.ok);
    report_interface_8(r);
    const std::string o = run(r, "m:sysinfo system /all");
    CHECK(o.find("  ROM BIOS   not given by this OODOS (before interface 9)\n") != std::string::npos);
    CHECK(o.find("OOLIB interface 8") != std::string::npos);
    if (o.find("before interface 9") == std::string::npos)
        std::printf("  %s\n", vis(o).c_str());
}

TEST(one_section_alone_and_a_wrong_word)
{
    InfoRig r;
    REQUIRE(r.ok);
    const std::string got = run(r, "m:sysinfo panel /all");
    CHECK_EQ(got, std::string("OO-1 SYSTEM INFORMATION\n\nPANEL      switches $00000, HALT/ENABLE at ENABLE, S INST"));
    CHECK_EQ(run(r, "m:sysinfo memory"), std::string("SYSINFO {section} {/ALL}"));
}

TEST(the_ram_line_takes_bank_8_from_maps_ap8_on_a_revision_2_north_bridge)
{
    // D6 (B7, b7-plan.md §4, as C18 left it): on a $D2 bridge SYSINFO reads MAP's AP8 to say what bank 8 is, with
    // no new text. With the revision 2 video card and AP8 set (the golden's), the 960 KB clause; with MAP forced
    // to 0 before it runs, bank 8 is RAM: the no-card text. With no revision 2 card and MAP forced to 1, the
    // no-card text too (DM_INFO says there is no card).
    const std::string fitted = "MEMORY     1,024 KB fitted";
    const auto clause = [&](const std::string &o) { // what follows "1,024 KB fitted" on its line
        const size_t at = o.find(fitted);
        if (at == std::string::npos)
            return std::string("<no MEMORY line>");
        const size_t from = at + fitted.size();
        return o.substr(from, o.find_first_of("\r\n", from) - from);
    };
    const std::string with = ": 960 KB of RAM and the video card's 64 KB aperture";
    {
        InfoRig r;
        REQUIRE(r.ok);
        REQUIRE(r.bridge().id() == 0xD3); // the default since N5: revision 2's MAP and more
        CHECK_EQ(clause(run(r, "m:sysinfo system")), with);
        r.machine->bus().write(0xC6A5, 0); // MAP: AP8 clear
        CHECK_EQ(clause(run(r, "m:sysinfo system")), std::string());
    }
    {
        InfoRig r({}, 1);
        REQUIRE(r.ok);
        r.machine->bus().write(0xC6A5, 1); // MAP: AP8 set, with no revision 2 card
        CHECK_EQ(clause(run(r, "m:sysinfo system")), std::string());
    }
}

int main()
{
    return run_all();
}
