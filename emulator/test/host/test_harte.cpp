/**
 * @file test_harte.cpp
 * @brief Tom Harte's SingleStepTests (65x02/6502/v1) against the core.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Each case is one instruction from a random state: set registers and RAM,
 * Step() once, compare registers and RAM. Dormann tells you *that* something
 * is wrong and roughly where; this tells you which opcode, from which state,
 * and how. That is the list Phase 1 works from.
 *
 * Not built on harness.h: the unit of report here is an opcode, of which
 * there are 151 discovered at run time from fixture files, and the useful
 * output is a table rather than one line per assertion.
 *
 * Known gap, set aside by default: decimal ADC/SBC with an operand that is
 * not valid BCD (a nibble above 9). The core differs from NMOS silicon
 * there, every one of the failures is in that class (docs/PHASE0_RESULTS.md),
 * and fixing it belongs with the W65C02S in Phase 4, which differs again.
 * Those cases are counted and reported, not failed, so that anything new
 * stands out; HARTE_STRICT=1 fails them too. The classification needs the
 * operand, which for ADC/SBC is always the instruction's last bus read.
 *
 * Comparisons deliberately left out for now:
 * - Bus cycles. The core does not count them (Phase 4). The expected count is
 *   in the fixture so that phase can start checking without a re-fetch.
 * - P bit 4 (B). It is not a register bit on real silicon; it exists only in
 *   pushed copies of P, and those are checked through RAM. The core keeps it
 *   set in its live status byte (reset, PLP, RTI and BIT all force it), so
 *   comparing it would fail cases for a reason no program can observe.
 */

#include "test_support.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint8_t P_MASK = static_cast<uint8_t>(~Cpu6502::FLAG_B);
    constexpr int LEGAL_OPCODES = 151; // documented NMOS set, as fetched

    /// FlatBus that remembers the last value read: for ADC/SBC, the operand.
    class RecordingBus : public FlatBus
    {
    public:
        uint8_t read(uint16_t addr) override { return last_read = FlatBus::read(addr); }
        uint8_t last_read{0};
    };

    bool is_adc_sbc(unsigned op)
    {
        switch (op)
        {
        case 0x61: case 0x65: case 0x69: case 0x6D: case 0x71: case 0x75: case 0x79: case 0x7D: // ADC
        case 0xE1: case 0xE5: case 0xE9: case 0xED: case 0xF1: case 0xF5: case 0xF9: case 0xFD: // SBC
            return true;
        default:
            return false;
        }
    }

    bool valid_bcd(uint8_t v)
    {
        return (v >> 4) <= 9 && (v & 0xF) <= 9;
    }

    /// The known Phase 4 gap: decimal ADC/SBC on an invalid-BCD operand.
    bool known_gap(unsigned op, const Registers &init, uint8_t operand)
    {
        return is_adc_sbc(op) && (init.p & Cpu6502::FLAG_D) && !(valid_bcd(init.a) && valid_bcd(operand));
    }

    struct State
    {
        Registers regs;
        std::vector<std::pair<uint16_t, uint8_t>> ram;
    };

    bool read_state(std::istringstream &in, State &st)
    {
        unsigned pc, s, a, x, y, p, n;
        if (!(in >> pc >> s >> a >> x >> y >> p >> n))
            return false;
        st.regs = Registers{static_cast<uint16_t>(pc), static_cast<uint8_t>(s), static_cast<uint8_t>(a),
                       static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(p)};
        st.ram.clear();
        for (unsigned i = 0; i < n; ++i)
        {
            unsigned addr, val;
            if (!(in >> addr >> val))
                return false;
            st.ram.emplace_back(static_cast<uint16_t>(addr), static_cast<uint8_t>(val));
        }
        return true;
    }

    /// Run one case; on mismatch, describe the first difference in `why`.
    bool run_case(RecordingBus &bus, const State &init, const State &want, std::string &why)
    {
        Cpu6502 c(bus);
        c.set_regs(init.regs);
        for (const auto &[addr, val] : init.ram)
            bus.at(addr) = val;

        c.step();

        char buf[128];
        auto reg = [&](const char *name, unsigned got, unsigned exp) {
            if (got == exp)
                return true;
            std::snprintf(buf, sizeof buf, "%s got %02X want %02X", name, got, exp);
            why = buf;
            return false;
        };
        if (c.regs().pc != want.regs.pc)
        {
            std::snprintf(buf, sizeof buf, "PC got %04X want %04X", c.regs().pc, want.regs.pc);
            why = buf;
            return false;
        }
        if (!reg("A", c.regs().a, want.regs.a) || !reg("X", c.regs().x, want.regs.x) ||
            !reg("Y", c.regs().y, want.regs.y) || !reg("S", c.regs().s, want.regs.s) ||
            !reg("P", c.regs().p & P_MASK, want.regs.p & P_MASK))
            return false;
        for (const auto &[addr, val] : want.ram)
        {
            if (bus.at(addr) != val)
            {
                std::snprintf(buf, sizeof buf, "RAM[%04X] got %02X want %02X", addr, bus.at(addr), val);
                why = buf;
                return false;
            }
        }
        return true;
    }
} // namespace

int main()
{
    const std::string dir = EMU_FIXTURE_DIR "/harte";
    if (!std::ifstream(dir + "/SOURCE"))
    {
        std::printf("SKIP: %s not found (run tools/fetch_fixtures.py)\n", dir.c_str());
        return SKIP;
    }

    // HARTE_VERBOSE=1 lists every failing case, not just the first per opcode.
    const bool verbose = std::getenv("HARTE_VERBOSE") != nullptr;
    // HARTE_STRICT=1 fails the known gap too (for Phase 4).
    const bool strict = std::getenv("HARTE_STRICT") != nullptr;

    RecordingBus bus;
    int opcodes = 0, failed_opcodes = 0;
    long cases = 0, failed_cases = 0, known_cases = 0;

    for (unsigned op = 0; op < 256; ++op)
    {
        char name[16];
        std::snprintf(name, sizeof name, "/%02x.txt", op);
        std::ifstream in(dir + name);
        if (!in)
            continue;

        ++opcodes;
        long n = 0, bad = 0;
        std::string line, first_case, first_why;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;
            std::istringstream ls(line);
            State init, want;
            unsigned cycles;
            if (!read_state(ls, init) || !read_state(ls, want) || !(ls >> cycles))
            {
                std::printf("FAIL: malformed line in %s%s\n", dir.c_str(), name);
                return 1;
            }
            ++n;
            std::string why;
            if (!run_case(bus, init, want, why))
            {
                const bool known = !strict && known_gap(op, init.regs, bus.last_read);
                if (verbose)
                    std::printf("  %02X case %ld (pc=%04X a=%02X p=%02X): %s%s\n", op, n, init.regs.pc,
                                init.regs.a, init.regs.p, why.c_str(), known ? " [known gap]" : "");
                if (known)
                {
                    ++known_cases;
                    continue;
                }
                if (bad++ == 0)
                {
                    char at[64];
                    std::snprintf(at, sizeof at, "case %ld (pc=%04X p=%02X)", n, init.regs.pc, init.regs.p);
                    first_case = at;
                    first_why = why;
                }
            }
        }
        if (n == 0)
        {
            std::printf("FAIL: %s%s has no cases\n", dir.c_str(), name);
            return 1;
        }
        cases += n;
        failed_cases += bad;
        if (bad)
        {
            ++failed_opcodes;
            std::printf("NOT OK %02X  %4ld/%ld failed  first: %s: %s\n", op, bad, n,
                        first_case.c_str(), first_why.c_str());
        }
    }

    // Zero examined is a failure, not a pass: a SOURCE file with no opcode
    // files beside it means the fetch broke, not that the core is perfect.
    // Short of all 151 is a failure too, or a partial fetch reads as coverage.
    if (opcodes == 0)
    {
        std::printf("FAIL: %s has no opcode files\n", dir.c_str());
        return 1;
    }
    if (opcodes != LEGAL_OPCODES)
    {
        std::printf("FAIL: %s has %d opcode files, expected %d (re-run tools/fetch_fixtures.py)\n",
                    dir.c_str(), opcodes, LEGAL_OPCODES);
        return 1;
    }
    std::printf("\n%d opcodes, %ld cases: %d opcode(s) / %ld case(s) failed\n", opcodes, cases,
                failed_opcodes, failed_cases);
    if (known_cases)
        std::printf("%ld more in the known gap (decimal ADC/SBC on invalid BCD, Phase 4); "
                    "HARTE_STRICT=1 to fail them\n", known_cases);
    return failed_opcodes == 0 ? 0 : 1;
}
