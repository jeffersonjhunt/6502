/**
 * @file test_dormann.cpp
 * @brief Klaus Dormann's functional tests against the core.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 *   test_dormann 6502_functional_test 6502          NMOS core
 *   test_dormann 6502_functional_test w65c02s       the 65C02 is a superset
 *   test_dormann 65C02_extended_opcodes_test w65c02s
 *
 * Each image fills all 64 KB, starts at $0400, and signals its result by
 * trapping: a check that fails branches to itself, and success is a
 * `jmp *` at a known address. So the runner steps until the PC stops moving
 * and reports where. A trap anywhere else is a failure, and its address is
 * the lookup key into the image's .lst at the pinned commit (see
 * tools/fetch_fixtures.py) for which instruction and which check.
 *
 * The success addresses are for those exact binaries, which is why the
 * fetcher verifies their sha256: a rebuilt image moves them.
 */

#include "test_support.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct Image
    {
        const char *name;
        uint16_t start;
        uint16_t success;
    };

    constexpr Image IMAGES[] = {
        {"6502_functional_test", 0x0400, 0x3469},
        {"65C02_extended_opcodes_test", 0x0400, 0x24F1}, // built with wdc_op = rkwl_wdc_op = 1
    };

    constexpr long MAX_STEPS = 200'000'000; // a pass is ~30M instructions
} // namespace

int main(int argc, char **argv)
{
    const Image *image = nullptr;
    for (const Image &i : IMAGES)
    {
        if (argc == 3 && std::strcmp(argv[1], i.name) == 0)
            image = &i;
    }
    const bool nmos = argc == 3 && std::strcmp(argv[2], "6502") == 0;
    const bool wdc = argc == 3 && std::strcmp(argv[2], "w65c02s") == 0;
    if (!image || (!nmos && !wdc))
    {
        std::printf("usage: test_dormann 6502_functional_test|65C02_extended_opcodes_test 6502|w65c02s\n");
        return 2;
    }

    const std::string path = std::string(EMU_FIXTURE_DIR "/") + image->name + ".bin";
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::printf("SKIP: %s not found (run tools/fetch_fixtures.py)\n", path.c_str());
        return SKIP;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() != 65536)
    {
        std::printf("FAIL: %s is %zu bytes, expected 65536\n", path.c_str(), bytes.size());
        return 1;
    }

    FlatBus bus;
    for (size_t i = 0; i < bytes.size(); ++i)
        bus.at(static_cast<uint16_t>(i)) = static_cast<uint8_t>(bytes[i]);

    Cpu6502 c(bus, wdc ? Cpu6502::Model::W65C02S : Cpu6502::Model::Nmos6502);
    c.set_regs(Registers{image->start, 0xFF, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});

    long steps = 0;
    uint16_t pc = c.regs().pc;
    for (; steps < MAX_STEPS; ++steps)
    {
        c.step();
        const uint16_t now = c.regs().pc;
        if (now == pc)
            break;
        pc = now;
    }

    const Registers &r = c.regs();
    std::printf("%s on %s: trapped at $%04X after %ld instructions, %llu cycles "
                "(A=%02X X=%02X Y=%02X S=%02X P=%02X)\n",
                image->name, argv[2], pc, steps, static_cast<unsigned long long>(c.cycles()), r.a, r.x, r.y, r.s,
                r.p);
    if (steps == MAX_STEPS)
    {
        std::printf("FAIL: no trap within %ld instructions\n", MAX_STEPS);
        return 1;
    }
    if (pc != image->success)
    {
        std::printf("FAIL: expected the success trap at $%04X; look up $%04X in %s.lst\n", image->success, pc,
                    image->name);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
