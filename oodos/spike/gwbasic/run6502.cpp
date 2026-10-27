/**
 * @file run6502.cpp
 * @brief Runs a spike image on the emulator's W65C02S core, as a `.COM` would run under OODOS.
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 *
 *   run6502 spike.bin [--load ADDR:FILE]... [--dump ADDR:LEN:FILE]... [--count ADDR]... [--max N]
 *
 * The image loads at $0300 and starts there with S = $FF, as OODOS will start
 * a `.COM`. The base page is OODOS's (hw/oodos.md, "What a program sees"):
 * $0200 is the warm boot and $0203 the BDOS entry, which this runner answers
 * itself instead of executing, so the spike calls the BDOS the way a real
 * program will (function in Y, byte parameter in A) and nothing else:
 *
 *   0   warm boot: stop
 *   2   console output: the character in A, to stdout
 *   9   print string: the string at A (low) and X (high), to its '$'
 *
 * A jump to $0200 also stops, and so does STP. Then each --dump writes LEN
 * bytes from ADDR to FILE, for the checker to read; --count reports how many
 * times the PC reached ADDR (a routine's entry), for the far-call estimates.
 * Numbers are hex with an optional $ or 0x, except --max, which is decimal.
 *
 * Everything outside the CPU is a flat 64 KB of RAM: the spike touches no
 * I/O.
 */

#include "cpu6502.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace ooe::emu;

namespace
{
    class FlatBus : public Bus
    {
    public:
        uint8_t read(uint16_t addr) override { return mem_[addr]; }
        void write(uint16_t addr, uint8_t data) override { mem_[addr] = data; }
        uint8_t peek(uint16_t addr) const override { return mem_[addr]; }

        uint8_t &at(uint16_t addr) { return mem_[addr]; }

    private:
        std::array<uint8_t, 65536> mem_{};
    };

    constexpr uint16_t LOAD = 0x0300;
    constexpr uint16_t WBOOT = 0x0200;
    constexpr uint16_t BDOS = 0x0203;
    constexpr uint16_t TPA_TOP = 0xC000;

    unsigned long parse_hex(const std::string &s)
    {
        std::string t = s;
        if (!t.empty() && t[0] == '$')
            t = t.substr(1);
        return std::strtoul(t.c_str(), nullptr, 16);
    }

    bool load(FlatBus &bus, uint16_t addr, const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            std::fprintf(stderr, "run6502: cannot read %s\n", path.c_str());
            return false;
        }
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (addr + bytes.size() > 0x10000)
        {
            std::fprintf(stderr, "run6502: %s does not fit at $%04X\n", path.c_str(), addr);
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i)
            bus.at(static_cast<uint16_t>(addr + i)) = static_cast<uint8_t>(bytes[i]);
        return true;
    }

    struct Dump
    {
        uint16_t addr;
        size_t len;
        std::string path;
    };

    struct Count
    {
        uint16_t addr;
        unsigned long hits;
    };
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: run6502 spike.bin [--load ADDR:FILE]... [--dump ADDR:LEN:FILE]... "
                             "[--count ADDR]... [--max N]\n");
        return 2;
    }

    FlatBus bus;
    std::vector<Dump> dumps;
    std::vector<Count> counts;
    unsigned long long max_steps = 2'000'000'000ULL;

    if (!load(bus, LOAD, argv[1]))
        return 2;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--load" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            const size_t colon = v.find(':');
            if (colon == std::string::npos || !load(bus, static_cast<uint16_t>(parse_hex(v.substr(0, colon))),
                                                    v.substr(colon + 1)))
                return 2;
        }
        else if (arg == "--dump" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            const size_t a = v.find(':');
            const size_t b = v.find(':', a + 1);
            if (a == std::string::npos || b == std::string::npos)
                return 2;
            dumps.push_back({static_cast<uint16_t>(parse_hex(v.substr(0, a))),
                             static_cast<size_t>(parse_hex(v.substr(a + 1, b - a - 1))), v.substr(b + 1)});
        }
        else if (arg == "--count" && i + 1 < argc)
            counts.push_back({static_cast<uint16_t>(parse_hex(argv[++i])), 0});
        else if (arg == "--max" && i + 1 < argc)
            max_steps = std::strtoull(argv[++i], nullptr, 10);
        else
        {
            std::fprintf(stderr, "run6502: unknown argument %s\n", arg.c_str());
            return 2;
        }
    }

    // The base page: JMP to itself at the warm boot, and the BDOS entry's
    // operand holds the top of the TPA + 1, as CP/M's address 6 does.
    bus.at(WBOOT) = 0x4C;
    bus.at(WBOOT + 1) = WBOOT & 0xFF;
    bus.at(WBOOT + 2) = WBOOT >> 8;
    bus.at(BDOS) = 0x4C;
    bus.at(BDOS + 1) = TPA_TOP & 0xFF;
    bus.at(BDOS + 2) = TPA_TOP >> 8;

    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    cpu.set_regs(Registers{LOAD, 0xFF, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});

    unsigned long long steps = 0;
    bool done = false;
    for (; steps < max_steps && !done; ++steps)
    {
        Registers r = cpu.regs();
        for (Count &c : counts)
            if (r.pc == c.addr)
                ++c.hits;
        if (r.pc == WBOOT)
            break;
        if (r.pc == BDOS)
        {
            switch (r.y)
            {
            case 0:
                done = true;
                break;
            case 2:
                std::fputc(r.a, stdout);
                break;
            case 9:
                for (unsigned n = 0; n < 0x10000 && bus.at(static_cast<uint16_t>((r.x << 8 | r.a) + n)) != '$'; ++n)
                    std::fputc(bus.at(static_cast<uint16_t>((r.x << 8 | r.a) + n)), stdout);
                break;
            default:
                std::fprintf(stderr, "run6502: BDOS function %u is not answered here\n", r.y);
                return 1;
            }
            // Return as the BDOS would: RTS.
            const uint16_t lo = bus.at(static_cast<uint16_t>(0x100 + static_cast<uint8_t>(r.s + 1)));
            const uint16_t hi = bus.at(static_cast<uint16_t>(0x100 + static_cast<uint8_t>(r.s + 2)));
            r.s = static_cast<uint8_t>(r.s + 2);
            r.pc = static_cast<uint16_t>(((hi << 8) | lo) + 1);
            cpu.set_regs(r);
            continue;
        }
        cpu.step();
        if (cpu.state() == Cpu6502::State::Stopped)
            break;
    }
    std::fflush(stdout);

    if (steps >= max_steps)
    {
        std::fprintf(stderr, "run6502: no stop within %llu instructions (PC $%04X)\n", max_steps, cpu.regs().pc);
        return 1;
    }
    for (const Dump &d : dumps)
    {
        std::ofstream out(d.path, std::ios::binary);
        for (size_t i = 0; i < d.len; ++i)
            out.put(static_cast<char>(bus.at(static_cast<uint16_t>(d.addr + i))));
    }
    std::fprintf(stderr, "run6502: %llu instructions, %llu cycles\n", steps,
                 static_cast<unsigned long long>(cpu.cycles()));
    for (const Count &c : counts)
        std::fprintf(stderr, "run6502: $%04X reached %lu times\n", c.addr, c.hits);
    return 0;
}
