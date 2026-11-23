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
    const uint32_t m0 = ms_count(r) & 0xFFFF;
    const std::string o = r.go("l1test.com", " B", ms(8000));
    const uint32_t m1 = ms_count(r) & 0xFFFF;
    CHECK_EQ(field(o, "COSTS"), std::string("01 00")); // counted
    const std::string c = field(o, "COST");
    REQUIRE(c.size() == 12);
    const unsigned frame = hexval(c.substr(2, 2) + c.substr(0, 2)), tick = hexval(c.substr(6, 2) + c.substr(4, 2));
    std::printf("  the most a frame hook took: %u cycles; a tick hook: %u\n", frame, tick);
    // What's counted is from the dispatcher's call to the hook's return, so about 130 cycles of the
    // dispatcher's own besides the hook's, and for the frame hook the ticks taken under it (each tick
    // and its 300-cycle hook, one or two a frame hook).
    CHECK(frame >= 8000 && frame <= 10500); // 8,000 cycles of its own
    // 282 of its own. The cost counters sit behind CALL61 (in block 61, out of common memory, L3's
    // step (b), and out of block 62 since the serial receive buffer): the switch there and back adds about
    // 48 cycles to each count (431 measured with neither, 489 behind CALL62, 479 behind CALL61).
    CHECK(tick >= 380 && tick <= 520);
    const double want = static_cast<double>(r.cpu->cycles() - c0) / 7159.0;
    const double got = static_cast<double>((m1 - m0) & 0xFFFF);
    std::printf("  %.0f ms counted, %.1f in the cycles\n", got, want);
    CHECK(got >= want - 2 && got <= want + 2);
}

TEST(hook_costs_by_the_bridge)
{
    // O9 (N10). The debug build times its hooks by the bridge's MS0 and TBSUB from one latch, not by timer 1: L1TEST
    // B's tick hook (about 300 cycles) measures HOOK_TICK cycles, within 4, and no CPU cycle touches timer 1's
    // registers ($C344-$C347), which are the program's. Must fail with TBSUB read without MS0's latch.
    constexpr unsigned HOOK_TICK = 438; // measured (N10; 478 by timer 1 at N8, the plan's 479)
    struct T1Cycles : CycleObserver
    {
        unsigned n = 0;
        void on_cycle(const BusCycle &c) override
        {
            if (!c.dma && c.addr >= 0xC344 && c.addr <= 0xC347)
                ++n;
        }
    } t1;
    CcpRig r;
    REQUIRE(r.ok);
    r.cpu->set_observer(&t1);
    const std::string o = r.go("l1test.com", " B", ms(8000));
    r.cpu->set_observer(nullptr);
    const std::string c = field(o, "COST");
    REQUIRE(c.size() == 12);
    const unsigned tick = hexval(c.substr(6, 2) + c.substr(4, 2));
    std::printf("  the tick hook by the bridge: %u cycles (pinned %u); timer 1 touched %u times\n", tick, HOOK_TICK, t1.n);
    CHECK(tick + 4 >= HOOK_TICK && tick <= HOOK_TICK + 4);
    CHECK_EQ(t1.n, 0u);
}

int main() { return run_all(); }
