/**
 * @file test_dormann.cpp
 * @brief Klaus Dormann's 6502_functional_test against the core.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The test image fills all 64 KB, starts at $0400, and signals its result by
 * trapping: every check that fails branches to itself, and success is the
 * `jmp *` at $3469. So the runner steps until the PC stops moving and reports
 * where. A trap anywhere else is a failure, and the address is the lookup key:
 * find it in 6502_functional_test.lst at the pinned commit (see
 * tools/fetch_fixtures.py) to see which instruction and which check.
 *
 * The constants below are for that exact binary, which is why the fetcher
 * verifies its sha256: a rebuilt image moves the success address.
 */

#include "test_support.h"

#include <cstdio>
#include <fstream>
#include <vector>

using namespace ooe::test;

namespace
{
    constexpr uint16_t START = 0x0400;
    constexpr uint16_t SUCCESS = 0x3469;
    constexpr long MAX_STEPS = 200'000'000; // a pass is ~30M instructions
} // namespace

int main()
{
    const char *path = EMU_FIXTURE_DIR "/6502_functional_test.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::printf("SKIP: %s not found (run tools/fetch_fixtures.py)\n", path);
        return SKIP;
    }
    std::vector<char> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (image.size() != 65536)
    {
        std::printf("FAIL: %s is %zu bytes, expected 65536\n", path, image.size());
        return 1;
    }

    FlatBus bus;
    for (size_t i = 0; i < image.size(); ++i)
        bus.at(static_cast<uint16_t>(i)) = static_cast<uint8_t>(image[i]);

    cpu c(&bus);
    boot(c, bus, Regs{START, 0xFF, 0, 0, 0, flag::U | flag::I});

    long steps = 0;
    uint16_t pc = c.GetPC();
    for (; steps < MAX_STEPS; ++steps)
    {
        c.Step();
        const uint16_t now = c.GetPC();
        if (now == pc)
            break;
        pc = now;
    }

    std::printf("trapped at $%04X after %ld instructions (A=%02X X=%02X Y=%02X S=%02X P=%02X)\n",
                pc, steps, c.GetA(), c.GetX(), c.GetY(), c.GetS(), c.GetP());
    if (steps == MAX_STEPS)
    {
        std::printf("FAIL: no trap within %ld instructions\n", MAX_STEPS);
        return 1;
    }
    if (pc != SUCCESS)
    {
        std::printf("FAIL: expected the success trap at $%04X; look up $%04X in "
                    "6502_functional_test.lst\n", SUCCESS, pc);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
