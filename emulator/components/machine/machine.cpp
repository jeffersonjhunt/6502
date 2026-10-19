/**
 * @file machine.cpp
 * @brief Machine description parser and builder.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "machine.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "emu_log.h"
#include "memory_devices.h"
#include "pia6821.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "machine";

        /// Hex, optionally `$`-prefixed, at most 5 digits (A0-A19).
        bool parse_addr(const std::string &s, uint32_t &out)
        {
            const std::string digits = (!s.empty() && s[0] == '$') ? s.substr(1) : s;
            if (digits.empty() || digits.size() > 5)
                return false;
            char *end = nullptr;
            const unsigned long v = std::strtoul(digits.c_str(), &end, 16);
            if (*end != '\0')
                return false;
            out = static_cast<uint32_t>(v);
            return true;
        }

        bool parse_range(const std::string &s, uint32_t &first, uint32_t &last)
        {
            const size_t dash = s.find('-');
            return dash != std::string::npos && parse_addr(s.substr(0, dash), first) &&
                   parse_addr(s.substr(dash + 1), last) && first <= last;
        }

        std::string strip_comment(const std::string &line)
        {
            const size_t hash = line.find('#');
            return hash == std::string::npos ? line : line.substr(0, hash);
        }
    } // namespace

    std::unique_ptr<Machine> Machine::build(const std::string &text, const std::string &origin,
                                            const MachineOptions &options, std::string &error)
    {
        std::unique_ptr<Machine> m(new Machine(options.bridge));
        bool rom_overridden = false;
        int lineno = 0;
        std::istringstream lines(text);
        std::string line;

        auto fail = [&](const std::string &what) {
            error = origin + ":" + std::to_string(lineno) + ": " + what;
            return nullptr;
        };

        while (std::getline(lines, line))
        {
            ++lineno;
            std::istringstream words(strip_comment(line));
            std::vector<std::string> w{std::istream_iterator<std::string>(words), std::istream_iterator<std::string>()};
            if (w.empty())
                continue;

            const std::string &type = w[0];
            if (type == "cpu")
            {
                if (w.size() != 2 || w[1] != "nmos6502")
                    return fail("cpu: only 'nmos6502' is supported (the W65C02S is Phase 4)");
                m->cpu_ = w[1];
                continue;
            }
            if (type != "ram" && type != "rom" && type != "pia6821")
                return fail("unknown type '" + type + "' (want cpu, ram, rom or pia6821)");

            Mapping map;
            if (w.size() < 2 || !parse_range(w[1], map.first, map.last))
                return fail(type + ": want a range like 8000-FFFF");
            if (map.last >= PHYSICAL_SPACE)
                return fail(type + ": range is outside A0-A19");

            std::string name, image;
            for (size_t i = 2; i < w.size(); ++i)
            {
                const std::string &arg = w[i];
                const size_t eq = arg.find('=');
                if (arg == "irq")
                    map.irq_wired = true;
                else if (eq != std::string::npos && arg.compare(0, eq, "name") == 0)
                    name = arg.substr(eq + 1);
                else if (eq != std::string::npos && arg.compare(0, eq, "module") == 0)
                {
                    uint32_t id = 0;
                    if (!parse_addr(arg.substr(eq + 1), id) || id > 0xF)
                        return fail("module: want a hex ID 0-F");
                    map.module = static_cast<uint8_t>(id);
                }
                else if (eq != std::string::npos)
                    return fail("unknown option '" + arg.substr(0, eq) + "'");
                else if (image.empty())
                    image = arg;
                else
                    return fail("unexpected '" + arg + "'");
            }
            if (!name.empty() && m->named_.count(name))
                return fail("name '" + name + "' is already used");

            const size_t size = map.last - map.first + 1;
            std::unique_ptr<Device> dev;
            if (type == "ram")
            {
                if (!image.empty())
                    return fail("ram takes no image");
                dev = std::make_unique<Ram>(size);
            }
            else if (type == "rom")
            {
                if (!rom_overridden && !options.rom_override.empty())
                {
                    image = options.rom_override;
                    rom_overridden = true;
                }
                if (image.empty())
                    return fail("rom: needs an image file");
                auto rom = std::make_unique<Rom>(size);
                if (!rom->load(image))
                    return fail("rom: cannot load " + image);
                dev = std::move(rom);
            }
            else
            {
                if (!image.empty())
                    return fail("pia6821 takes no image");
                dev = std::make_unique<Pia6821>();
            }

            map.device = dev.get();
            m->bus_.map(map);
            if (!name.empty())
                m->named_[name] = dev.get();
            m->devices_.push_back(std::move(dev));
        }

        if (m->devices_.empty())
        {
            error = origin + ": describes no devices";
            return nullptr;
        }
        if (!options.rom_override.empty() && !rom_overridden)
        {
            error = origin + ": has no rom line for " + options.rom_override + " to replace";
            return nullptr;
        }
        EMU_LOGI(TAG, "%s: %zu devices", origin.c_str(), m->devices_.size());
        return m;
    }

    std::unique_ptr<Machine> Machine::load(const std::string &path, const MachineOptions &options,
                                           std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = path + ": cannot open";
            return nullptr;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return build(text, path, options, error);
    }

    Device *Machine::find(const std::string &name) const
    {
        const auto it = named_.find(name);
        return it == named_.end() ? nullptr : it->second;
    }
} // namespace ooe::emu
