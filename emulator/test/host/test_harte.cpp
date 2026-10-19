/**
 * @file test_harte.cpp
 * @brief Tom Harte's SingleStepTests against the core, bus cycle by bus cycle.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 *   test_harte 6502        NMOS 6502, the 151 documented opcodes
 *   test_harte wdc65c02    WDC 65C02 (the W65C02S), 254 opcodes
 *
 * Each case is one instruction from a random state: set registers and RAM,
 * step() once, then compare registers (all of P), RAM, and every bus access
 * the instruction made: address, data and direction, in order. Since every
 * 6502 cycle is one access, matching the list also matches the cycle count,
 * and an access to anything not in the list fails, as Harte requires.
 *
 * Not built on harness.h: the unit of report is an opcode, of which there
 * are up to 254 discovered from fixture files, and the useful output is a
 * table rather than one line per assertion.
 */

#include "test_support.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct Access
    {
        uint16_t addr;
        uint8_t data;
        bool write;
        bool operator==(const Access &o) const { return addr == o.addr && data == o.data && write == o.write; }
    };

    /// FlatBus that records every access the CPU makes.
    class RecordingBus : public FlatBus
    {
    public:
        uint8_t read(uint16_t addr) override
        {
            const uint8_t v = FlatBus::read(addr);
            log.push_back({addr, v, false});
            return v;
        }
        void write(uint16_t addr, uint8_t data) override
        {
            log.push_back({addr, data, true});
            FlatBus::write(addr, data);
        }
        std::vector<Access> log;
    };

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

    bool read_cycles(std::istringstream &in, std::vector<Access> &out)
    {
        unsigned n;
        if (!(in >> n))
            return false;
        out.clear();
        for (unsigned i = 0; i < n; ++i)
        {
            unsigned addr, val, w;
            if (!(in >> addr >> val >> w))
                return false;
            out.push_back({static_cast<uint16_t>(addr), static_cast<uint8_t>(val), w != 0});
        }
        return true;
    }

    std::string show(const std::vector<Access> &v, size_t k)
    {
        if (k >= v.size())
            return "nothing";
        char b[32];
        std::snprintf(b, sizeof b, "%c %04X=%02X", v[k].write ? 'W' : 'r', v[k].addr, v[k].data);
        return b;
    }

    /// Run one case; on mismatch, describe the first difference in `why`.
    bool run_case(Cpu6502::Model model, RecordingBus &bus, const State &init, const State &want,
                  const std::vector<Access> &cycles, std::string &why)
    {
        Cpu6502 c(bus, model);
        c.set_regs(init.regs);
        for (const auto &[addr, val] : init.ram)
            bus.at(addr) = val;
        bus.log.clear();

        const unsigned took = c.step();

        char buf[160];
        for (size_t i = 0; i < cycles.size() || i < bus.log.size(); ++i)
        {
            if (i < cycles.size() && i < bus.log.size() && bus.log[i] == cycles[i])
                continue;
            std::snprintf(buf, sizeof buf, "cycle %zu: got %s want %s (%zu cycles, want %zu)", i + 1,
                          show(bus.log, i).c_str(), show(cycles, i).c_str(), bus.log.size(), cycles.size());
            why = buf;
            return false;
        }
        if (took != cycles.size())
        {
            std::snprintf(buf, sizeof buf, "step() returned %u, made %zu accesses", took, cycles.size());
            why = buf;
            return false;
        }

        auto reg = [&](const char *name, unsigned got, unsigned exp) {
            if (got == exp)
                return true;
            std::snprintf(buf, sizeof buf, "%s got %02X want %02X", name, got, exp);
            why = buf;
            return false;
        };
        const Registers &r = c.regs();
        if (r.pc != want.regs.pc)
        {
            std::snprintf(buf, sizeof buf, "PC got %04X want %04X", r.pc, want.regs.pc);
            why = buf;
            return false;
        }
        if (!reg("A", r.a, want.regs.a) || !reg("X", r.x, want.regs.x) || !reg("Y", r.y, want.regs.y) ||
            !reg("S", r.s, want.regs.s) || !reg("P", r.p, want.regs.p))
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

int main(int argc, char **argv)
{
    if (argc != 2 || (std::strcmp(argv[1], "6502") != 0 && std::strcmp(argv[1], "wdc65c02") != 0))
    {
        std::printf("usage: test_harte 6502|wdc65c02\n");
        return 2;
    }
    const bool wdc = std::strcmp(argv[1], "wdc65c02") == 0;
    const Cpu6502::Model model = wdc ? Cpu6502::Model::W65C02S : Cpu6502::Model::Nmos6502;
    const int expected_opcodes = wdc ? 254 : 151;

    const std::string dir = std::string(EMU_FIXTURE_DIR "/harte/") + argv[1];
    std::ifstream source(dir + "/SOURCE");
    if (!source)
    {
        std::printf("SKIP: %s not found (run tools/fetch_fixtures.py)\n", dir.c_str());
        return SKIP;
    }
    std::string format;
    std::getline(source, format);
    if (format != "format 2")
    {
        std::printf("FAIL: %s is '%s', want 'format 2' (re-run tools/fetch_fixtures.py)\n", dir.c_str(),
                    format.c_str());
        return 1;
    }

    // HARTE_VERBOSE=1 lists every failing case, not just the first per opcode.
    const bool verbose = std::getenv("HARTE_VERBOSE") != nullptr;

    RecordingBus bus;
    int opcodes = 0, failed_opcodes = 0;
    long cases = 0, failed_cases = 0;

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
        std::vector<Access> cycles;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;
            std::istringstream ls(line);
            State init, want;
            if (!read_state(ls, init) || !read_state(ls, want) || !read_cycles(ls, cycles))
            {
                std::printf("FAIL: malformed line in %s%s\n", dir.c_str(), name);
                return 1;
            }
            ++n;
            std::string why;
            if (!run_case(model, bus, init, want, cycles, why))
            {
                if (verbose)
                    std::printf("  %02X case %ld (pc=%04X a=%02X p=%02X): %s\n", op, n, init.regs.pc,
                                init.regs.a, init.regs.p, why.c_str());
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
            std::printf("NOT OK %02X  %4ld/%ld failed  first: %s: %s\n", op, bad, n, first_case.c_str(),
                        first_why.c_str());
        }
    }

    // Zero examined is a failure, not a pass; so is a partial set.
    if (opcodes != expected_opcodes)
    {
        std::printf("FAIL: %s has %d opcode files, expected %d (re-run tools/fetch_fixtures.py)\n", dir.c_str(),
                    opcodes, expected_opcodes);
        return 1;
    }
    std::printf("\n%s: %d opcodes, %ld cases: %d opcode(s) / %ld case(s) failed\n", argv[1], opcodes, cases,
                failed_opcodes, failed_cases);
    return failed_opcodes == 0 ? 0 : 1;
}
