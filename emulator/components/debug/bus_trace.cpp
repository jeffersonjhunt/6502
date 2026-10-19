/**
 * @file bus_trace.cpp
 * @brief Bus trace writer.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "bus_trace.h"

#include <sstream>

namespace ooe::emu
{
    BusTrace::BusTrace(std::FILE *out, const Decoder &decoder, const std::string &about)
        : out_(out), decoder_(decoder)
    {
        std::fprintf(out_, "# ooe-bus-trace %d\n", VERSION);
        std::istringstream lines(about);
        std::string line;
        while (std::getline(lines, line))
            std::fprintf(out_, "# %s\n", line.c_str());
    }

    void BusTrace::on_cycle(const BusCycle &c)
    {
        if (!enabled_)
            return;
        const uint8_t module = decoder_.module_at(c.addr);
        char ps[2] = {'-', '\0'};
        if (module != NO_MODULE)
            ps[0] = "0123456789ABCDEF"[module & 0xF];
        std::fprintf(out_, "%llu %05X %02X %c %c%c %s\n", static_cast<unsigned long long>(c.cycle),
                     static_cast<unsigned>(decoder_.physical(c.addr)), c.data, c.write ? 'W' : 'R',
                     c.sync ? 'S' : '-', c.vector_pull ? 'V' : '-', ps);
        ++lines_;
    }
} // namespace ooe::emu
