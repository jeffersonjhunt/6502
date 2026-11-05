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
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t BLOCK62 = 62u * 0x4000u;

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
            files.push_back({0, "IOTEST  COM", built(SYS, "iotest.com")});
            put_m(files);
            put_util("get");
            put_util("put");
            put_util("gencom");
            chain({13, 0});
        }
    };
} // namespace

TEST(get_gives_the_next_programs_input_from_a_file_then_the_keyboard)
{
    Rx r({{0, "IN      TXT", bytes("ONE\r\nTWO\r\n")}});
    REQUIRE(r.ok);
    std::string o = r.cmd("get file m:in.txt");
    has(o, "Getting console input from file: IN.TXT");
    o = r.cmd("iotest L L");
    std::printf("[%s]\n", vis(o).c_str());
    {
        const auto l = labels(std::string(EMU_OODOS_UTILS_DIR) + "/getrsx.ra.lbl");
        const uint32_t base = r.phys(BLOCK62 + sym("TBASE") - 0xC000u) << 8;
        std::printf("  base %X mode %u flags %u mycount %u ccpst %02X\n", base, r.phys(base + l.at("mode") - 0x300),
                    r.phys(base + l.at("flags") - 0x300), r.phys(base + l.at("mycount") - 0x300), r.phys(SCB_PHYS + 0x19));
    }
    has(o, "ONE\rL[ONE] TWO\rL[TWO] ");
    hasnt(o, "KEY");
    // The file done: the keyboard again
    const size_t from = r.out.size();
    r.type("iotest L\r");
    r.run(ms(500));
    r.type("KEY\r");
    CHECK(r.until("L[KEY] ", ms(3000), from));
    r.run(ms(300));
    CHECK_EQ(nrsx(r), 0u);
}

int main()
{
    return run_all();
}
