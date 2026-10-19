/**
 * @file symbols.cpp
 * @brief ld65 label file reader.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "symbols.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ooe::emu
{
    bool Symbols::load(const std::string &path, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = path + ": cannot open";
            return false;
        }
        std::string line;
        for (int n = 1; std::getline(in, line); ++n)
        {
            std::istringstream ls(line);
            std::string kind, hex, name, extra;
            if (!(ls >> kind))
                continue; // blank
            char *end = nullptr;
            const unsigned long addr = (ls >> hex) ? std::strtoul(hex.c_str(), &end, 16) : 0;
            if (kind != "al" || !end || *end != '\0' || addr > 0xFFFF || !(ls >> name) || name.size() < 2 ||
                name[0] != '.' || (ls >> extra))
            {
                error = path + ":" + std::to_string(n) + ": expected 'al <hex address> .<name>'";
                return false;
            }
            add(name.substr(1), static_cast<uint16_t>(addr));
        }
        return true;
    }

    void Symbols::add(const std::string &name, uint16_t addr)
    {
        if (!by_name_.emplace(name, addr).second)
            return;
        by_addr_.emplace(addr, name);
    }

    bool Symbols::find(const std::string &name, uint16_t &addr) const
    {
        const auto it = by_name_.find(name);
        if (it == by_name_.end())
            return false;
        addr = it->second;
        return true;
    }

    const std::string *Symbols::name_at(uint16_t addr) const
    {
        const auto it = by_addr_.find(addr);
        return it == by_addr_.end() ? nullptr : &it->second;
    }
} // namespace ooe::emu
