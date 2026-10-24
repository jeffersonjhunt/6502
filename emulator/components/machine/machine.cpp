/**
 * @file machine.cpp
 * @brief Machine description parser and builder.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "machine.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "acia6551.h"
#include "backplane_bridge.h"
#include "console_card.h"
#include "dma_controller.h"
#include "emu_log.h"
#include "hd44780.h"
#include "memory_devices.h"
#include "pia6821.h"
#include "ps2_keyboard.h"
#include "usb_card.h"
#include "via6522.h"
#include "video_card.h"

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

        /// "PB4" or "PA0-7": a port letter, then a bit or a range of bits.
        bool parse_pins(const std::string &s, ViaPins &out)
        {
            if (s.size() < 3 || s[0] != 'P' || (s[1] != 'A' && s[1] != 'B'))
                return false;
            out.port = s[1] == 'A' ? Via6522::Port::A : Via6522::Port::B;
            const std::string bits = s.substr(2);
            const size_t dash = bits.find('-');
            auto digit = [](const std::string &d, unsigned &v) {
                if (d.size() != 1 || d[0] < '0' || d[0] > '7')
                    return false;
                v = static_cast<unsigned>(d[0] - '0');
                return true;
            };
            unsigned first = 0, last = 0;
            if (!digit(bits.substr(0, dash), first))
                return false;
            last = first;
            if (dash != std::string::npos && (!digit(bits.substr(dash + 1), last) || last < first))
                return false;
            out.first = static_cast<uint8_t>(first);
            out.count = static_cast<uint8_t>(last - first + 1);
            return true;
        }

        bool parse_pin(const std::string &s, ViaPin &out)
        {
            ViaPins p;
            if (!parse_pins(s, p) || p.count != 1)
                return false;
            out = ViaPin{p.port, p.first};
            return true;
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
                if (w.size() != 2 || (w[1] != "nmos6502" && w[1] != "w65c02s"))
                    return fail("cpu: want nmos6502 or w65c02s");
                m->cpu_ = w[1];
                continue;
            }
            if (type == "northbridge")
            {
                if (w.size() < 2 || w[1] != "backplane" || w.size() > 3 || (w.size() == 3 && w[2] != "bootsel"))
                    return fail("northbridge: want 'northbridge backplane [bootsel]'");
                if (m->bridge_)
                    return fail("northbridge: only one per machine");
                if (options.bridge)
                    return fail("northbridge: the caller already supplied a north bridge");
                if (m->named_.count("northbridge"))
                    return fail("name 'northbridge' is already used");
                auto bridge = std::make_unique<BackplaneBridge>(w.size() == 3);
                bridge->attach(&m->bus_);
                m->bus_.set_bridge(bridge.get());
                m->bus_.map(Mapping{bus_address(Space::Io, BackplaneBridge::REGISTERS),
                                    bus_address(Space::Io, BackplaneBridge::REGISTERS + 0x7F), bridge.get(),
                                    BackplaneBridge::MODULE, false});
                m->bridge_ = bridge.get();
                m->named_["northbridge"] = bridge.get();
                m->devices_.push_back(std::move(bridge));
                continue;
            }
            if (type == "hd44780" || type == "ps2kbd")
            {
                std::unordered_map<std::string, std::string> opt;
                for (size_t i = 1; i < w.size(); ++i)
                {
                    const size_t eq = w[i].find('=');
                    if (eq == std::string::npos)
                        return fail(type + ": options are key=value, not '" + w[i] + "'");
                    opt[w[i].substr(0, eq)] = w[i].substr(eq + 1);
                }
                const std::string name = opt["name"];
                if (!name.empty() && m->named_peripherals_.count(name))
                    return fail("name '" + name + "' is already used");
                auto *via = dynamic_cast<Via6522 *>(m->find(opt["via"]));
                if (!via)
                    return fail(type + ": via=NAME must name a via6522 described above");
                ViaPins data;
                std::unique_ptr<Peripheral> p;
                if (type == "hd44780")
                {
                    ViaPin rs, rw, e;
                    if (!parse_pins(opt["data"], data) || (data.count != 4 && data.count != 8))
                        return fail("hd44780: data= wants 4 or 8 pins, like PB0-7 or PB0-3");
                    if (!parse_pin(opt["rs"], rs) || !parse_pin(opt["rw"], rw) || !parse_pin(opt["e"], e))
                        return fail("hd44780: rs=, rw= and e= each want one pin, like PA5");
                    unsigned cols = 16, rows = 2;
                    if (opt.count("size") &&
                        (std::sscanf(opt["size"].c_str(), "%ux%u", &cols, &rows) != 2 || cols == 0 || cols > 40 ||
                         (rows != 1 && rows != 2 && rows != 4)))
                        return fail("hd44780: size= wants COLSxROWS, like 16x2 or 20x4");
                    for (const char *k : {"name", "via", "data", "rs", "rw", "e", "size"})
                        opt.erase(k);
                    p = std::make_unique<ViaLcd>(*via, data, rs, rw, e, cols, rows);
                }
                else
                {
                    if (!parse_pins(opt["data"], data) || data.count != 8)
                        return fail("ps2kbd: data= wants 8 pins, like PA0-7");
                    for (const char *k : {"name", "via", "data"})
                        opt.erase(k);
                    p = std::make_unique<Ps2Keyboard>(*via, data);
                }
                if (!opt.empty())
                    return fail(type + ": unknown option '" + opt.begin()->first + "'");
                if (!name.empty())
                    m->named_peripherals_[name] = p.get();
                m->peripherals_.push_back(std::move(p));
                continue;
            }
            if (type != "ram" && type != "rom" && type != "pia6821" && type != "via6522" && type != "acia6551" &&
                type != "dma" && type != "console" && type != "video" && type != "usb")
                return fail("unknown type '" + type +
                            "' (want cpu, northbridge, ram, rom, pia6821, via6522, acia6551, dma, console, video, "
                            "usb, hd44780 or ps2kbd)");

            Mapping map;
            if (w.size() < 2 || !parse_range(w[1], map.first, map.last))
                return fail(type + ": want a range like 8000-FFFF");
            if (map.last >= PHYSICAL_SPACE)
                return fail(type + ": range is outside A0-A19");

            std::string name, image;
            Space space = Space::Memory;
            for (size_t i = 2; i < w.size(); ++i)
            {
                const std::string &arg = w[i];
                const size_t eq = arg.find('=');
                if (arg == "irq")
                    map.irq_wired = true;
                else if (eq != std::string::npos && arg.compare(0, eq, "drq") == 0)
                {
                    const std::string n = arg.substr(eq + 1);
                    if (n.size() != 1 || n[0] < '0' || n[0] > '3')
                        return fail("drq: want a DMA channel 0-3");
                    map.drq = static_cast<uint8_t>(n[0] - '0');
                }
                else if (eq != std::string::npos && arg.compare(0, eq, "wait") == 0)
                {
                    const std::string n = arg.substr(eq + 1);
                    if (n.empty() || n.size() > 2 || n.find_first_not_of("0123456789") != std::string::npos ||
                        std::stoul(n) > 15)
                        return fail("wait: want 0-15 wait states");
                    map.wait = static_cast<uint8_t>(std::stoul(n));
                }
                else if (eq != std::string::npos && arg.compare(0, eq, "space") == 0)
                {
                    const std::string s = arg.substr(eq + 1);
                    if (s == "memory")
                        space = Space::Memory;
                    else if (s == "flash")
                        space = Space::Flash;
                    else if (s == "io")
                        space = Space::Io;
                    else
                        return fail("space: want memory, flash or io");
                }
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
            if (space == Space::Io)
            {
                if (map.last >= IO_SIZE)
                    return fail(type + ": the I/O hole is 000-7FF");
                if (map.module == NO_MODULE)
                    map.module = static_cast<uint8_t>(map.first >> 7);
            }

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
                    return fail(type + " takes no image");
                if (type == "pia6821")
                    dev = std::make_unique<Pia6821>();
                else if (type == "via6522")
                    dev = std::make_unique<Via6522>();
                else if (type == "acia6551")
                    dev = std::make_unique<Acia6551>();
                else if (type == "console")
                {
                    if (m->console_card_)
                        return fail("console: only one per machine");
                    auto card = std::make_unique<ConsoleCard>();
                    card->attach(&m->bus_);
                    m->console_card_ = card.get();
                    dev = std::move(card);
                }
                else if (type == "usb")
                {
                    if (m->usb_card_)
                        return fail("usb: only one per machine");
                    auto card = std::make_unique<UsbCard>();
                    m->usb_card_ = card.get();
                    dev = std::move(card);
                }
                else if (type == "video")
                {
                    if (m->video_card_)
                        return fail("video: only one per machine");
                    auto card = std::make_unique<VideoCard>();
                    m->video_card_ = card.get();
                    dev = std::move(card);
                }
                else
                {
                    if (m->master_)
                        return fail("dma: only one per machine");
                    auto dma = std::make_unique<DmaController>();
                    dma->attach(&m->bus_);
                    m->bus_.set_master(dma.get());
                    m->master_ = dma.get();
                    dev = std::move(dma);
                }
            }

            map.device = dev.get();
            map.first = bus_address(space, map.first);
            map.last = bus_address(space, map.last);
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
        if (m->video_card_)
        {
            // The card draws from banks 8-B, which it snoops: give it the RAM there.
            const uint32_t first = bus_address(Space::Memory, VideoCard::VRAM_BASE);
            const uint32_t last = first + VideoCard::VRAM_SIZE - 1;
            const Ram *ram = nullptr;
            const auto &maps = m->bus_.mappings();
            for (auto it = maps.rbegin(); it != maps.rend(); ++it) // the later mapping wins
            {
                if (it->last < first || it->first > last)
                    continue;
                if (it->first <= first && it->last >= last)
                    ram = dynamic_cast<const Ram *>(it->device);
                if (ram)
                    m->video_card_->attach(ram->data() + (first - it->first));
                break;
            }
            if (!ram)
            {
                error = origin + ": video: banks 8-B (80000-BFFFF) must be one ram line";
                return nullptr;
            }
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
