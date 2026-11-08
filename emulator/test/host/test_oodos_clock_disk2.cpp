/**
 * @file test_oodos_clock_disk2.cpp
 * @brief OOLIB's L1: the Disk II's fields with interrupts off, and the millisecond counter caught up.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The Disk II driver (oodos/sys/disk2.s) turns interrupts off for each field, about 12 ms a sector
 * (the CPU times the bits), and adds that time to the millisecond counter from the disk bytes it read
 * (hw/oolib.md, question 14). Here a PIP from O: and one onto it, against the emulated time: without
 * the catch-up the counter would miss most of the read's ticks. disk2_rig.h's rig, so it needs the
 * owner's sequencer PROM among the fixtures, and exits 77 without it.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "disk2_rig.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;
using namespace disk2_rig;

namespace
{
    constexpr double TICK = 7159.0;
    constexpr double FRAME = 7159090.0 * 800 * 525 / 25175000;

    uint32_t word32(Rig &r, uint32_t a)
    {
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }

    /// A command line, and the clocks over it against the cycles: the ms counter within `pct` percent,
    /// the frame counter within two frames (a field is shorter than a frame: none is lost).
    void timed(Dr &r, const std::string &line, double pct)
    {
        const uint64_t c0 = r.cpu->cycles();
        const uint32_t m0 = word32(r, SYS_BLOCK_PHYS + sym("MS")), f0 = word32(r, SYS_BLOCK_PHYS + sym("FRAMES"));
        const std::string o = r.c(line);
        CHECK(o.find("Error") == std::string::npos);
        const double want = static_cast<double>(r.cpu->cycles() - c0) / TICK;
        const double fwant = static_cast<double>(r.cpu->cycles() - c0) / FRAME;
        const double got = word32(r, SYS_BLOCK_PHYS + sym("MS")) - m0, fgot = word32(r, SYS_BLOCK_PHYS + sym("FRAMES")) - f0;
        std::printf("  %s: %.0f ms counted, %.1f in the cycles (%.2f%% off); %.0f frames, %.1f\n", line.c_str(), got,
                    want, 100 * (got - want) / want, fgot, fwant);
        CHECK(want > 1000);
        CHECK(std::abs(got - want) <= want * pct / 100);
        CHECK(std::abs(fgot - fwant) <= 2);
    }
} // namespace

TEST(a_disk_ii_read_is_caught_up)
{
    Dr r;
    REQUIRE(r.ok);
    timed(r, "pip m:=o:pattern.bin", 1.5);
    CHECK(m_data(r, 0, "PATTERN BIN") == records(gen(20000, 5))); // and read right
}

TEST(a_disk_ii_write_is_caught_up)
{
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}});
    REQUIRE(r.ok);
    timed(r, "pip o:=m:x.bin", 1.5);
}

TEST(a_disk_ii_write_keeps_up_under_a_full_budget_frame_hook)
{
    // A frame hook of 8,000 cycles, its budget, runs with interrupts on, so it can cover a sector's D5
    // as it goes by while the driver searches for it, and a turn is about 12 frames, so it may cover it
    // again a turn later; D2WSEC tries for about 6 turns. L1TEST K: 16 writes of 1 KB to O: with the
    // hook set, each after the drive has stopped (1.1 s) and 1 ms later than the last, so the hook
    // meets the track at 16 phases of a frame.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = r.go("l1test.com", " K", ms(120000));
    CHECK_EQ(field(o, "FHOOK"), std::string("00 00"));
    unsigned passes = 0;
    for (size_t at = o.find("\nK "); at != std::string::npos; at = o.find("\nK ", at + 1), ++passes)
        std::printf("  %s\n", o.substr(at + 1, o.find('\r', at) - at - 1).c_str());
    CHECK_EQ(passes, 16u);
    CHECK_EQ(field(o, "KERR"), std::string("00 00")); // every delete, make, write and close as it should be
    has(r.c("dir o:k.bin"), "K        BIN");
}

int main()
{
    if (!std::ifstream(P6).good())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
