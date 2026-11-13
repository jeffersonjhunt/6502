/**
 * @file test_gwbasic_core.cpp
 * @brief GW-BASIC's core, phase 7a (hw/oodos.md, "The plan, phase 7"), run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree (EMU_GWBASIC_BUILD, or
 * GWBASIC_BUILD to try another build). OODOS.SYS boots as test_oodos_ccp boots it (oodos_rig.h's CcpRig), both
 * files go on M: behind OODOS's back, and M:GWBASIC is typed at the prompt.
 *
 * - The loader: the blocks loaded, common memory copied into them, the far call sites patched; the banner and
 *   Ok; SYSTEM back to the CCP.
 * - A far call's crossing (V8): FTSITE's FJSR of P1's FARTST, which does nothing, timed from the JSR to the
 *   instruction after it, less a JSR and its RTS: at most 340 cycles.
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

    /// The resident core's labels (ld65's -Ln).
    std::map<std::string, uint16_t> labels()
    {
        std::map<std::string, uint16_t> out;
        std::ifstream f(BUILD + "/gwbasic.lbl");
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
} // namespace

TEST(the_loader_loads_the_blocks_and_gw_basic_says_ok)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::string got = start(r);
    CHECK(got.find("GW-BASIC") != std::string::npos);
    CHECK(got.find("Ok\r\n") != std::string::npos);
    const std::string back = r.cmd("system");
    CHECK(back.find("A>") != std::string::npos);
}

TEST(a_far_call_crosses_in_at_most_340_cycles)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::map<std::string, uint16_t> lab = labels();
    REQUIRE(lab.count("FTSITE") == 1);
    const uint16_t site = lab.at("FTSITE"), after = static_cast<uint16_t>(site + 6);
    uint64_t from = 0;
    std::vector<uint64_t> took;
    r.watch = [&](uint16_t pc) {
        if (pc == site)
            from = r.cpu->cycles();
        else if (pc == after && from)
        {
            took.push_back(r.cpu->cycles() - from);
            from = 0;
        }
    };
    start(r);
    for (int i = 0; i < 4; ++i) // each empty line is READY again: another crossing
        r.to("", "Ok\r\n");
    r.watch = nullptr;
    REQUIRE(took.size() >= 5u);
    // The least of them: a crossing the 1 kHz tick (145 cycles) didn't land in.
    uint64_t least = took[0];
    for (const uint64_t t : took)
        least = t < least ? t : least;
    const uint64_t crossing = least - 12; // less a JSR (6) and its RTS (6): FARTST is an RTS
    std::printf("  a far call into P1 and back: %lu cycles over a JSR (the spike's FARCALL 281; OOUI's crossing 322)\n",
                static_cast<unsigned long>(crossing));
    CHECK(crossing <= 340u);
}

int main()
{
    return run_all();
}
