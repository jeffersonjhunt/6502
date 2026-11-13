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
 *   - the whole machine (sysinfo.txt);
 *   - a card left out of the machine's text (the sound card, the USB card,
 *     the console card): its CARDS line says "not found" and its own
 *     section says there is none, and nothing else changes (each its
 *     golden file, and the lines that differ from sysinfo.txt counted);
 *   - a revision changed: the sound card at revision 1 (OODOS booted again
 *     after, as test_oodos_sound does), the video card at revision 1;
 *   - the floating bus (review CC5): no Disk II card, and a device in its
 *     block that answers each read with the last byte the CPU fetched, as
 *     an empty slot might: HW_PROBE's two reads disagree, so no Disk II is
 *     found;
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
    /// `want_frame` false: the machine has no video frame to measure by.
    std::string normal(const std::string &got, bool want_frame = true)
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
            const bool ok = std::abs(v - NATIVE_HZ) <= NATIVE_HZ * 0.00002;
            std::printf("  %.0f Hz by the clock chip\n", v);
            CHECK(ok);
            if (!ok)
                std::printf("  by the clock chip: %.0f Hz, not within 0.002%% of 7,159,090\n", v);
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

    std::string full_golden() { return read_text(std::string(EMU_GOLDEN_DIR) + "/sysinfo.txt"); }
} // namespace

TEST(sysinfo_reports_the_whole_machine)
{
    InfoRig r;
    REQUIRE(r.ok);
    const std::string got = normal(run(r, "m:sysinfo /all"));
    CHECK(golden("sysinfo.txt", got));
    CHECK(got.find("$C4FF") == std::string::npos); // the addresses are the blocks', not the ID bytes'
}

TEST(a_card_left_out_loses_its_line_and_its_section)
{
    struct Case
    {
        const char *type, *file, *card;
        size_t lines; ///< the lines that differ from sysinfo.txt
    };
    // Its CARDS line and its section's lines, out; the CARDS line and the section's one, in.
    for (const Case &c : {Case{"sound", "sysinfo_nosound.txt", "sound: not found", 7},
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

TEST(a_revision_changed_changes_its_lines)
{
    {
        std::printf("  the sound card at revision 1\n");
        InfoRig r;
        REQUIRE(r.ok);
        r.machine->sound_card()->set_revision(1);
        REQUIRE(r.boot());
        r.put_util("sysinfo", UTILS);
        const std::string got = normal(run(r, "m:sysinfo /all"));
        CHECK(golden("sysinfo_sound1.txt", got));
        CHECK(got.find("  $C500  $A1  sound") != std::string::npos);
        CHECK(got.find("SOUND      revision 1: two SIDs") != std::string::npos);
    }
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

TEST(one_section_alone_and_a_wrong_word)
{
    InfoRig r;
    REQUIRE(r.ok);
    const std::string got = run(r, "m:sysinfo panel /all");
    CHECK_EQ(got, std::string("OO-1 SYSTEM INFORMATION\n\nPANEL      switches $00000, HALT/ENABLE at ENABLE, S INST"));
    CHECK_EQ(run(r, "m:sysinfo memory"), std::string("SYSINFO {section} {/ALL}"));
}

int main()
{
    return run_all();
}
