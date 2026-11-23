/**
 * @file test_oodos_random.cpp
 * @brief OODOS 0.16's function 78, the random source (hw/oodos.md, "The random source in OODOS"; its tests O1-O4 in hw/testing.md).
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 *
 * RNDTEST (oodos/sys/test/rndtest.s) calls function 78 on CcpRig, whose
 * north bridge's ATECC608B gives the fixed stream (Atecc608::splitmix(1)):
 * every byte a case sees is known. The bridge drew the stream's first 32 at
 * power-on; nothing else in OODOS reads the pool, so the bytes RN_FILL gives
 * are the stream's in order. (The spec's O5, a revision 2 bridge, can't run:
 * OODOS 0.16 refuses that bridge, test_oodos_sys's O7.)
 */

#include "harness.h"
#include "oodos_rig.h"

#include "atecc608.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    /// The fixed stream's bytes `skip` on, `n` of them, as RNDTEST prints them (hex, no spaces).
    std::string stream(size_t skip, size_t n)
    {
        Atecc608::Source s = Atecc608::splitmix(1);
        for (size_t i = 0; i < skip; ++i)
            s();
        std::string out;
        char b[3];
        for (size_t i = 0; i < n; ++i)
        {
            std::snprintf(b, sizeof b, "%02X", s());
            out += b;
        }
        return out;
    }

    /// backplane.machine with rng_chip=`chip` on its north bridge line.
    std::string with_chip(const std::string &chip)
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        const std::string line = "northbridge  backplane";
        const size_t at = text.find(line);
        if (at != std::string::npos)
            text.insert(at + line.size(), " rng_chip=" + chip);
        return text;
    }

    /// RNDTEST F's results, one a fill: A and X ("aa xx"), the bytes, and OVER.
    struct Fill
    {
        std::string ax, bytes, over;
    };
    std::vector<Fill> fills(const std::string &o)
    {
        std::vector<Fill> v;
        for (size_t at = o.find("FILL "); at != std::string::npos; at = o.find("FILL ", at + 1))
        {
            Fill f;
            const size_t e1 = o.find('\r', at);
            f.ax = o.substr(at + 5, e1 - at - 5);
            const size_t b0 = e1 + 2, e2 = o.find('\r', b0);
            f.bytes = o.substr(b0, e2 - b0);
            const size_t ov = o.find("OVER ", e2);
            f.over = ov == std::string::npos ? "" : o.substr(ov + 5, 2);
            v.push_back(f);
        }
        return v;
    }
} // namespace

TEST(rn_fill_gives_the_chip_s_bytes)
{
    // O1. RN_FILL of 1, 2, 32, 33 and 255 in turn: each A = X = 0, exactly the stream's next n bytes (across the
    // bridge's refills), and nothing past +1 + n written. Must fail with the count compare off by one, or a refill's
    // byte read twice.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("rndtest.com", " F01 02 20 21 FF", ms(10'000));
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 5);
    const size_t ns[] = {1, 2, 32, 33, 255};
    size_t at = 0;
    for (size_t i = 0; i < 5; ++i)
    {
        CHECK_EQ(f[i].ax, std::string("00 00"));
        CHECK_EQ(f[i].bytes, stream(at, ns[i]));
        CHECK_EQ(f[i].over, std::string("00"));
        at += ns[i];
    }
}

TEST(rn_fill_waits_for_ready)
{
    // O2. RN_FILL of 32 (the pool, emptied) and straight after it 64 (two refills waited for): the stream's bytes,
    // never the $00s an empty pool reads. Must fail with the wait for READY removed.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("rndtest.com", " F20 40", ms(10'000));
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 2);
    CHECK_EQ(f[0].bytes, stream(0, 32));
    CHECK_EQ(f[1].ax, std::string("00 00"));
    CHECK_EQ(f[1].bytes, stream(32, 64));
}

TEST(rn_fill_recovers_from_a_fault)
{
    // O3. A draw whose three tries all fail (three corrupted responses): the bridge sets FAULT, RN_FILL writes DRAW
    // once and the bytes come right (the stream past the failed tries' 96). Six failures, two draws: refused, $FF $FF,
    // and no hang. Must fail with DRAW not written, or the second failure's refusal removed. (RNGBYTE's 400 ms
    // give-up is a backstop for a bridge that reports neither READY nor FAULT, which the spec doesn't allow: no case
    // covers it.)
    {
        CcpRig r;
        REQUIRE(r.ok);
        r.bridge().rng_chip()->corrupt_next(3); // the refill after the pool's 32
        const std::string o = r.go("rndtest.com", " F20 20", ms(10'000));
        const std::vector<Fill> f = fills(o);
        REQUIRE(f.size() == 2);
        CHECK_EQ(f[1].ax, std::string("00 00"));
        CHECK_EQ(f[1].bytes, stream(32 + 96, 32));
    }
    CcpRig r;
    REQUIRE(r.ok);
    r.bridge().rng_chip()->corrupt_next(6);
    const std::string o = r.go("rndtest.com", " F20 20", ms(10'000));
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 2);
    CHECK_EQ(f[1].ax, std::string("FF FF"));
}

TEST(rn_fill_refuses_n_0_and_no_block)
{
    // Review r1's T3. RN_FILL with n 0: A = X = $FF and nothing written from +2 on (a count of 0 wrapped to 256
    // would write 256 bytes over what follows the block); RN_FILL with X = 0, no block: refused. Must fail with the
    // n 0 refusal removed, or the no-block refusal removed.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string o = r.go("rndtest.com", " F00 Z", ms(5000));
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 1);
    CHECK_EQ(f[0].ax, std::string("FF FF"));
    CHECK_EQ(f[0].over, std::string("00"));
    CHECK_EQ(field(o, "NOBLK"), std::string("FF FF"));
}

TEST(rn_info_says_the_last_draw_failed)
{
    // Review r1's T3. After a fill refused for two failed draws (six corrupted responses), RN_INFO says RS_CHIP and
    // RS_FAULT ($05): the chip is there, its last draw failed. Must fail with RNGOK never reporting RS_FAULT on a
    // locked chip.
    CcpRig r;
    REQUIRE(r.ok);
    r.bridge().rng_chip()->corrupt_next(6);
    const std::string o = r.go("rndtest.com", " F20 20 I", ms(10'000));
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 2);
    CHECK_EQ(f[1].ax, std::string("FF FF"));
    CHECK_EQ(field(o, "INFO"), std::string("05 00"));
}

TEST(each_call_may_retry_a_failed_draw_once)
{
    // Review r1's T4. A fault recovered in one call (three corrupted responses, then DRAW), then another in a later
    // call: F20 20 01 20, the last call's refill (its pool's 31 taken, then a draw) with three more corrupted
    // responses, put in as that call starts. It retries too, and gives the stream's bytes past its failed tries.
    // Must fail with RN_FILL's reset of its one DRAW (RNGNEW) removed: the flag carried over would refuse the last
    // call at its first fault.
    CcpRig r;
    REQUIRE(r.ok);
    r.bridge().rng_chip()->corrupt_next(3); // the refill after the pool's 32
    const uint16_t entry = sym("F_RANDOM");
    unsigned calls = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == entry && ++calls == 4)
            r.bridge().rng_chip()->corrupt_next(3); // the fourth call's refill
    };
    const std::string o = r.go("rndtest.com", " F20 20 01 20", ms(10'000));
    r.watch = nullptr;
    const std::vector<Fill> f = fills(o);
    REQUIRE(f.size() == 4);
    CHECK_EQ(f[1].ax, std::string("00 00"));
    CHECK_EQ(f[1].bytes, stream(32 + 96, 32)); // 128-159: the pool after the first fault
    CHECK_EQ(f[2].bytes, stream(160, 1));      // the next pool's first
    CHECK_EQ(f[3].ax, std::string("00 00"));
    CHECK_EQ(f[3].bytes, stream(161, 31) + stream(192 + 96, 1)); // its other 31, then past three failed tries
}

TEST(rn_info_says_what_there_is)
{
    // O4. RN_INFO: a locked chip RS_CHIP ($01); rng_chip=unlocked RS_UNLOCKED ($02), and RN_FILL refused; none 0,
    // refused. Must fail with RS_UNLOCKED's test inverted.
    {
        CcpRig r;
        REQUIRE(r.ok);
        CHECK_EQ(field(r.go("rndtest.com", " I"), "INFO"), std::string("01 00"));
    }
    for (const auto &[chip, info] : {std::pair<std::string, std::string>{"unlocked", "02 00"}, {"none", "00 00"}})
    {
        std::printf("  rng_chip=%s\n", chip.c_str());
        CcpRig r({}, with_chip(chip));
        REQUIRE(r.ok);
        CHECK_EQ(field(r.go("rndtest.com", " I"), "INFO"), info);
        const std::vector<Fill> f = fills(r.go("rndtest.com", " F04", ms(5000)));
        REQUIRE(f.size() == 1);
        CHECK_EQ(f[0].ax, std::string("FF FF"));
    }
}

int main() { return run_all(); }
