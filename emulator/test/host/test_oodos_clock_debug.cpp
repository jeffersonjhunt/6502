/**
 * @file test_oodos_clock_debug.cpp
 * @brief OOLIB's L1: a debug build of OODOS.SYS (`make DEBUG=1`) counts each hook's cycles.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The hooks' budgets (hw/oolib.md, question 3: 8,000 cycles a frame hook, 300 a tick hook) aren't
 * enforced; a debug build's dispatcher times each hook by timer 1 and function 80's FT_COSTS says the
 * most each took. L1TEST B's hooks are about 8,000 and 300 cycles; the clocks stay exact meanwhile.
 * EMU_OODOS_SYS_DIR is the debug build's (CMakeLists.txt's oodos_sys_debug).
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cstdio>
#include <string>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

TEST(a_debug_build_counts_the_hooks_cycles)
{
    CcpRig r;
    REQUIRE(r.ok);
    const uint64_t c0 = r.cpu->cycles();
    const uint32_t m0 = r.phys(SYS_BLOCK_PHYS + sym("MS")) | r.phys(SYS_BLOCK_PHYS + sym("MS") + 1) << 8;
    const std::string o = r.go("l1test.com", " B", ms(8000));
    const uint32_t m1 = r.phys(SYS_BLOCK_PHYS + sym("MS")) | r.phys(SYS_BLOCK_PHYS + sym("MS") + 1) << 8;
    CHECK_EQ(field(o, "COSTS"), std::string("01 00")); // counted
    const std::string c = field(o, "COST");
    REQUIRE(c.size() == 12);
    const unsigned frame = hexval(c.substr(2, 2) + c.substr(0, 2)), tick = hexval(c.substr(6, 2) + c.substr(4, 2));
    std::printf("  the most a frame hook took: %u cycles; a tick hook: %u\n", frame, tick);
    // What's counted is from the dispatcher's call to the hook's return, so about 130 cycles of the
    // dispatcher's own besides the hook's, and for the frame hook the ticks taken under it (each tick
    // and its 300-cycle hook, one or two a frame hook).
    CHECK(frame >= 8000 && frame <= 10500); // 8,000 cycles of its own
    CHECK(tick >= 380 && tick <= 460);      // 282 of its own
    const double want = static_cast<double>(r.cpu->cycles() - c0) / 7159.0;
    const double got = static_cast<double>((m1 - m0) & 0xFFFF);
    std::printf("  %.0f ms counted, %.1f in the cycles\n", got, want);
    CHECK(got >= want - 2 && got <= want + 2);
}

int main() { return run_all(); }
