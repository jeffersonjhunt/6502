/**
 * @file machine.cpp
 * @brief Machine description parser and builder.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "machine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#include "acia6551.h"
#include "backplane_bridge.h"
#include "console_card.h"
#include "dma_controller.h"
#include "emu_log.h"
#include "flash.h"
#include "hd44780.h"
#include "joystick_ports.h"
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
            if (digits.empty() || digits.size() > 6) // A0-A23
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

        /// The host's local time, for a new DS3231, and `ms` into its second: the clock is right when the
        /// emulator starts.
        Ds3231::Time host_time(unsigned &ms)
        {
            const auto clock = std::chrono::system_clock::now();
            const std::time_t now = std::chrono::system_clock::to_time_t(clock);
            ms = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           clock - std::chrono::system_clock::from_time_t(now))
                                           .count() %
                                       1000);
            std::tm local{};
            localtime_r(&now, &local);
            return Ds3231::Time{static_cast<unsigned>(local.tm_year + 1900), static_cast<unsigned>(local.tm_mon + 1),
                                static_cast<unsigned>(local.tm_mday),      static_cast<unsigned>(local.tm_hour),
                                static_cast<unsigned>(local.tm_min),       static_cast<unsigned>(local.tm_sec),
                                static_cast<unsigned>(local.tm_wday + 1)}; // 1 = Sunday
        }

        std::string strip_comment(const std::string &line)
        {
            const size_t hash = line.find('#');
            return hash == std::string::npos ? line : line.substr(0, hash);
        }

        /// The size of the file at `path`, or nullopt if it can't be opened.
        std::optional<size_t> file_size(const std::string &path)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in)
                return std::nullopt;
            return static_cast<size_t>(in.tellg());
        }

        /**
         * @brief The owner's Apple IIe ROMs into ROM A (hw/design.md, "ROM A's layout"): image n
         * (16 KB, or the CD and EF halves) at n x $4000, its character set n (4 or 8 KB) at
         * $10000 + n x $2000, each slot erased first. Every size is checked before anything is written.
         * @return empty, or why not (`origin`'s prefix added where the machine file is at fault)
         */
        std::string load_apple(const Machine &m, const MachineOptions &options, const std::string &origin)
        {
            constexpr size_t IMAGE = 0x4000, SETS = 0x10000, SET = 0x2000;
            bool any = !options.apple_chars.empty() || !options.apple_rom.empty();
            for (unsigned n = 0; n < 4; ++n)
                any = any || !options.apple_roms[n].empty() || !options.apple_charsets[n].empty();
            if (!any)
                return {};
            auto *flash = dynamic_cast<Rom *>(m.find("rom_a"));
            if (!flash || flash->size() < SETS + 4 * SET)
                return origin + ": an Apple IIe ROM needs ROM A, a 512 KB flash line named rom_a";
            if (!options.apple_chars.empty() && !options.apple_charsets[0].empty())
                return "Apple IIe character set 0: given twice";
            for (unsigned n = 0; n < 4; ++n)
            {
                std::vector<std::string> parts = n == 0 ? options.apple_rom : std::vector<std::string>{};
                parts.insert(parts.end(), options.apple_roms[n].begin(), options.apple_roms[n].end());
                const std::string &chars = n == 0 && !options.apple_chars.empty() ? options.apple_chars
                                                                                  : options.apple_charsets[n];
                const std::string which = std::to_string(n);
                size_t total = 0;
                for (const std::string &part : parts)
                {
                    const auto bytes = file_size(part);
                    if (!bytes)
                        return part + ": cannot open the Apple IIe ROM";
                    total += *bytes;
                }
                if (!parts.empty() && total != IMAGE)
                    return "Apple IIe ROM image " + which +
                           ": want 16 KB ($C000-$FFFF, or the CD and EF halves), got " + std::to_string(total) +
                           " bytes";
                if (!chars.empty())
                {
                    const auto bytes = file_size(chars);
                    if (bytes != size_t{0x1000} && bytes != size_t{0x2000})
                        return chars + ": want the 4 or 8 KB IIe character ROM (set " + which + ")";
                }
                if (!parts.empty())
                {
                    flash->erase(n * IMAGE, IMAGE);
                    size_t at = n * IMAGE;
                    for (const std::string &part : parts)
                    {
                        if (!flash->load(part, at))
                            return part + ": cannot load it into ROM A";
                        at += *file_size(part);
                    }
                }
                if (!chars.empty())
                {
                    flash->erase(SETS + n * SET, SET);
                    if (!flash->load(chars, SETS + n * SET))
                        return chars + ": cannot load it into ROM A";
                }
            }
            return {};
        }
    } // namespace

    std::unique_ptr<Machine> Machine::build(const std::string &text, const std::string &origin,
                                            const MachineOptions &options, std::string &error)
    {
        if (options.video_revision > 2)
        {
            error = origin + ": video_revision: want the video card's revision, 1 or 2 (0: the description's)";
            return nullptr;
        }
        std::unique_ptr<Machine> m(new Machine(options.bridge));
        bool rom_overridden = false;
        std::unordered_map<const Via6522 *, std::string> via_users; // the last peripheral type on each VIA
        unsigned video_rev = 1;                 // the video line's rev=
        std::vector<Mapping> apertures;         // mapped at the end, when the video card is revision 2
        std::vector<VideoAperture *> aperture_devices;
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
                if (w.size() < 2 || w[1] != "backplane")
                    return fail("northbridge: want 'northbridge backplane [bootsel] [revision=1|2|3] "
                                "[rng_chip=locked|unlocked|none]'");
                bool bootsel = false, revised = false;
                std::string rng_chip; // revision 3's random number chip: locked (the default), unlocked, none
                uint8_t revision = BackplaneBridge::REVISION; // the default (step B7)
                for (size_t i = 2; i < w.size(); ++i)
                {
                    if (w[i] == "bootsel" && !bootsel)
                        bootsel = true;
                    else if ((w[i] == "revision=1" || w[i] == "revision=2" || w[i] == "revision=3") && !revised)
                    {
                        revision = static_cast<uint8_t>(w[i].back() - '0');
                        revised = true;
                    }
                    else if ((w[i] == "rng_chip=locked" || w[i] == "rng_chip=unlocked" || w[i] == "rng_chip=none") &&
                             rng_chip.empty())
                        rng_chip = w[i].substr(9);
                    else
                        return fail("northbridge: want 'northbridge backplane [bootsel] [revision=1|2|3] "
                                    "[rng_chip=locked|unlocked|none]', not '" +
                                    w[i] + "'");
                }
                if (m->bridge_)
                    return fail("northbridge: only one per machine");
                if (options.bridge)
                    return fail("northbridge: the caller already supplied a north bridge");
                if (m->named_.count("northbridge"))
                    return fail("name 'northbridge' is already used");
                std::shared_ptr<Ds3231> rtc = options.rtc;
                if (!rtc && options.rtc_time)
                    rtc = std::make_shared<Ds3231>(*options.rtc_time);
                if (!rtc)
                {
                    unsigned ms = 0;
                    const Ds3231::Time now = host_time(ms);
                    rtc = std::make_shared<Ds3231>(now, false, ms);
                }
                if (!rng_chip.empty() && revision < BackplaneBridge::TIMEBASE)
                    return fail("northbridge: rng_chip= is revision 3's");
                auto bridge = std::make_unique<BackplaneBridge>(bootsel, std::move(rtc), revision);
                if (bridge->timebase())
                    bridge->set_rng_chip(rng_chip == "none" ? nullptr
                                                            : std::make_unique<Atecc608>(rng_chip != "unlocked", true,
                                                                                         options.rng_source));
                bridge->attach(&m->bus_);
                m->bus_.set_bridge(bridge.get());
                m->bus_.map(Mapping{bus_address(Space::Io, BackplaneBridge::REGISTERS),
                                    bus_address(Space::Io, BackplaneBridge::REGISTERS + 0x7F), bridge.get(),
                                    BackplaneBridge::MODULE, true}); // the clock's TICK interrupt
                // Apple II mode's $C000-$CFFF: the bridge sends it there only in that mode.
                m->bus_.map(Mapping{bus_address(Space::Io, APPLE_IO), bus_address(Space::Io, APPLE_IO + 0xFFF),
                                    &bridge->apple_io(), BackplaneBridge::MODULE, false});
                m->bridge_ = bridge.get();
                m->named_["northbridge"] = bridge.get();
                m->devices_.push_back(std::move(bridge));
                continue;
            }
            if (type == "hd44780" || type == "ps2kbd" || type == "joyports")
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
                if (type == "joyports" && m->joysticks_)
                    return fail("joyports: only one per machine");
                // The joysticks take both of a VIA's ports; nothing else can share it.
                const std::string &other = via_users[via];
                if (!other.empty() && (type == "joyports" || other == "joyports"))
                    return fail(type + ": via '" + opt["via"] + "' already has " + other +
                                " on it, and joyports needs a VIA of its own");
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
                else if (type == "joyports")
                {
                    for (const char *k : {"name", "via"})
                        opt.erase(k);
                    auto j = std::make_unique<JoystickPorts>(*via);
                    m->joysticks_ = j.get();
                    p = std::move(j);
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
                via_users[via] = type;
                m->peripherals_.push_back(std::move(p));
                continue;
            }
            if (type != "ram" && type != "rom" && type != "flash" && type != "pia6821" && type != "via6522" && type != "acia6551" &&
                type != "dma" && type != "console" && type != "video" && type != "usb" && type != "disk2" &&
                type != "sound" && type != "sd" && type != "aperture")
                return fail("unknown type '" + type +
                            "' (want cpu, northbridge, ram, rom, flash, pia6821, via6522, acia6551, dma, console, video, "
                            "aperture, usb, disk2, sound, sd, hd44780, ps2kbd or joyports)");

            Mapping map;
            if (w.size() < 2 || !parse_range(w[1], map.first, map.last))
                return fail(type + ": want a range like 8000-FFFF");
            if (map.last >= PHYSICAL_SPACE)
                return fail(type + ": range is outside A0-A23");

            std::string name, image;
            unsigned rev = 0;
            unsigned memsize = 0; // the sound card's, in MB (revision 3)
            Space space = Space::Memory;
            for (size_t i = 2; i < w.size(); ++i)
            {
                const std::string &arg = w[i];
                const size_t eq = arg.find('=');
                if (arg == "irq")
                    map.irq_wired = true;
                else if (arg == "drq")
                    map.drq = Dreq::Wired; // the card pulls /DREQ; its module, from its I/O block, names it
                else if (eq != std::string::npos && arg.compare(0, eq, "drq") == 0)
                    return fail("drq: a flag, with no channel: the card's request reaches /DREQ, and a DMA "
                                "channel serves it by the module its CARD register names (DMA revision 2)");
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
                else if (eq != std::string::npos && arg.compare(0, eq, "rev") == 0)
                {
                    const std::string n = arg.substr(eq + 1);
                    if (type != "video" && type != "sound")
                        return fail("rev: only the video and sound cards take a revision");
                    if (type == "video" && n != "1" && n != "2")
                        return fail("rev: want the video card's revision, 1 or 2");
                    if (type == "sound" && n != "1" && n != "2" && n != "3")
                        return fail("rev: want the sound card's revision, 1, 2 or 3");
                    rev = static_cast<unsigned>(n[0] - '0');
                }
                else if (eq != std::string::npos && arg.compare(0, eq, "memsize") == 0)
                {
                    const std::string n = arg.substr(eq + 1);
                    if (type != "sound")
                        return fail("memsize: only the sound card takes a memory size");
                    if (n != "2" && n != "4" && n != "8")
                        return fail("memsize: want the sound card's store in MB, 2, 4 or 8");
                    memsize = static_cast<unsigned>(n[0] - '0');
                }
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
            if (space == Space::Flash && map.last > ADDRESS_20)
                return fail(type + ": the flash is 00000-FFFFF (memory F00000-FFFFFF)");
            if (space == Space::Io)
            {
                if (map.last >= IO_SIZE)
                    return fail(type + ": the I/O hole is 000-7FF");
                if (map.first >> 7 != map.last >> 7) // a card matches one module ID and decodes A0-A6
                    return fail(type + ": " + w[1] + " crosses a module's 128-byte block");
                if (map.module == NO_MODULE)
                    map.module = static_cast<uint8_t>(map.first >> 7);
            }
            if (map.drq == Dreq::Wired && map.module == NO_MODULE) // wired, but no CARD could ever name it
                return fail("drq: the card needs a module ID (space=io, or module=) for a DMA channel's CARD "
                            "to name it");

            const size_t size = map.last - map.first + 1;
            std::unique_ptr<Device> dev;
            if (type == "ram")
            {
                if (!image.empty())
                    return fail("ram takes no image");
                dev = std::make_unique<Ram>(size);
            }
            else if (type == "rom" || type == "flash")
            {
                if (type == "flash" && size % 2)
                    return fail("flash: two chips, even and odd bytes: want an even size");
                if (!rom_overridden && !options.rom_override.empty())
                {
                    image = options.rom_override;
                    rom_overridden = true;
                }
                if (image.empty())
                    return fail(type + ": needs an image file, or - for erased flash");
                std::unique_ptr<Rom> rom;
                Flash *flash = nullptr;
                if (type == "flash")
                {
                    auto f = std::make_unique<Flash>(size);
                    f->attach(&m->bus_); // its busy times, in the bus's cycles
                    flash = f.get();
                    rom = std::move(f);
                }
                else
                    rom = std::make_unique<Rom>(size);
                if (image == "-")
                    rom->erase();
                else if (!rom->load(image))
                    return fail(type + ": cannot load " + image);
                if (flash)
                    m->flashes_.push_back(flash);
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
                    m->bus_.set_dma_observer(card.get()); // the CPLD sees DMA cycles too
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
                else if (type == "disk2")
                {
                    if (m->disk2_card_)
                        return fail("disk2: only one per machine");
                    auto card = std::make_unique<Disk2Card>();
                    card->attach(&m->bus_);
                    m->disk2_card_ = card.get();
                    dev = std::move(card);
                }
                else if (type == "sound")
                {
                    if (m->sound_card_ || m->sound_device_)
                        return fail("sound: only one per machine");
                    if (memsize && rev != 3)
                        return fail("sound: memsize= is revision 3's (rev=3)");
                    if (rev == 3)
                    {
                        auto card = std::make_unique<SoundCard3>((memsize ? memsize : 8) * 16);
                        m->sound_device_ = card.get();
                        dev = std::move(card);
                    }
                    else
                    {
                        auto card = std::make_unique<SoundCard>();
                        if (rev)
                            card->set_revision(rev);
                        m->sound_card_ = card.get();
                        dev = std::move(card);
                    }
                }
                else if (type == "sd")
                {
                    if (m->sd_card_)
                        return fail("sd: only one per machine");
                    auto card = std::make_unique<SdCard>();
                    m->sd_card_ = card.get();
                    dev = std::move(card);
                }
                else if (type == "video")
                {
                    if (m->video_card_)
                        return fail("video: only one per machine");
                    auto card = std::make_unique<VideoCard>();
                    video_rev = options.video_revision ? options.video_revision : rev ? rev : 1;
                    if (!card->set_revision(video_rev)) // a new card: never locked yet
                        return fail("video: the revision didn't take");
                    m->video_card_ = card.get();
                    dev = std::move(card);
                }
                else if (type == "aperture")
                {
                    // Its place on the new bus, or the old spelling of it, bank 8, which means the same (B1).
                    if (space != Space::Memory || size != VideoCard::APERTURE_SIZE ||
                        (map.first != BackplaneBridge::APERTURE && map.first != BackplaneBridge::BANK8))
                        return fail("aperture: want 64 KB at E00000-E0FFFF (or 80000-8FFFF, read as that)");
                    auto ap = std::make_unique<VideoAperture>();
                    aperture_devices.push_back(ap.get());
                    dev = std::move(ap);
                }
                else
                {
                    if (m->master_)
                        return fail("dma: only one per machine");
                    auto dma = std::make_unique<DmaController>();
                    dma->attach(&m->bus_);
                    m->bus_.set_master(dma.get());
                    m->master_ = dma.get();
                    m->dma_ = dma.get();
                    dev = std::move(dma);
                }
            }

            map.device = dev.get();
            map.first = bus_address(space, map.first);
            map.last = bus_address(space, map.last);
            if (type == "aperture")
                apertures.push_back(map);
            else
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
        if (auto *bp = dynamic_cast<BackplaneBridge *>(m->bridge_); bp && m->usb_card_)
            bp->apple_io().attach(m->usb_card_); // the IIe's keyboard, speaker and game port
        if (m->disk2_card_)
        {
            if (auto *bp = dynamic_cast<BackplaneBridge *>(m->bridge_))
            {
                bp->apple_io().attach(m->disk2_card_); // slot 6
                bp->apple_io().clock_changed(bp->apple()); // the CPU's clock, which counts its steps
            }
            if (!m->disk2_card_->load_roms(options.disk2_boot, options.disk2_sequencer, error))
                return nullptr;
        }
        else if (!options.disk2_boot.empty() || !options.disk2_sequencer.empty())
        {
            error = origin + ": Disk II PROMs, but no disk2 line for the card";
            return nullptr;
        }
        {
            // Revision 2: the card answers its aperture. A revision 2 north bridge sends bank 8 there
            // while MAP's AP8 is set (hw/design.md); a revision 1 bridge is told once, here, and only
            // with the card (B1). A revision 1 card has no aperture, nor a machine without the card:
            // the line is inert. A line at bank 8 itself, as machine files had it before the 24-bit
            // bus, is taken for the aperture's own place, E00000, on the backplane.
            auto *bp = dynamic_cast<BackplaneBridge *>(m->bridge_);
            if (m->video_card_ && video_rev == 2)
            {
                for (VideoAperture *ap : aperture_devices)
                    ap->attach(m->video_card_);
                for (Mapping map : apertures)
                {
                    if (bp && map.first == bus_address(Space::Memory, BackplaneBridge::BANK8))
                    {
                        map.first = bus_address(Space::Memory, BackplaneBridge::APERTURE);
                        map.last = map.first + VideoCard::APERTURE_SIZE - 1;
                    }
                    if (bp && !bp->wide() && bp->aperture() == BackplaneBridge::NO_APERTURE &&
                        map.first != bus_address(Space::Memory, BackplaneBridge::BANK8))
                        bp->set_aperture(offset_of(map.first));
                    m->bus_.map(map);
                }
            }
        }
        if (m->video_card_)
            m->video_card_->lock_revision(); // the bus is wired for it now: set_revision() can't change it
        if (m->video_card_ && video_rev == 1)
        {
            // Revision 1 draws from banks 8-B, which it snoops: give it the RAM there.
            // (Revision 2's card memory is its own; only Apple II mode reads system RAM.)
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
        if (m->video_card_)
        {
            const auto &maps = m->bus_.mappings();
            // Apple II mode: banks 0 and 1, and the bridge's switches
            if (auto *bp = dynamic_cast<BackplaneBridge *>(m->bridge_))
            {
                for (auto it = maps.rbegin(); it != maps.rend(); ++it)
                {
                    if (it->last < bus_address(Space::Memory, 0) || it->first > bus_address(Space::Memory, 0x1FFFF))
                        continue;
                    if (const auto *low = dynamic_cast<const Ram *>(it->device);
                        low && it->first <= bus_address(Space::Memory, 0) && it->last >= bus_address(Space::Memory, 0x1FFFF))
                        m->video_card_->attach_apple(low->data() + (bus_address(Space::Memory, 0) - it->first), bp);
                    break;
                }
            }
        }
        // What the flash kept through a power cycle, then the owner's Apple IIe ROMs over it.
        if (!options.flash_user.empty())
        {
            auto *rom_a = dynamic_cast<Rom *>(m->find("rom_a"));
            if (!rom_a || rom_a->size() < options.flash_user.size())
            {
                error = origin + ": the kept flash needs ROM A, a flash line named rom_a of at least " +
                        std::to_string(options.flash_user.size()) + " bytes";
                return nullptr;
            }
            for (size_t i = 0; i < options.flash_user.size(); ++i)
                rom_a->poke(static_cast<uint32_t>(i), options.flash_user[i]);
        }
        // The owner's Apple IIe ROMs, in ROM A's images and sets.
        if (std::string why = load_apple(*m, options, origin); !why.empty())
        {
            error = why;
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

    void Machine::set_cpu_hz(uint32_t hz)
    {
        if (video_card_)
            video_card_->set_cpu_hz(hz); // 0, flat out: the card keeps 7.16 MHz time
        if (usb_card_)
            usb_card_->set_cpu_hz(hz); // key repeat and the paddles
        if (sound_card_)
            sound_card_->set_cpu_hz(hz); // its 6510 keeps the SID clock's time
        if (sound_device_)
            sound_device_->set_cpu_hz(hz); // the firmware keeps the card's own time
        if (sd_card_)
            sd_card_->set_cpu_hz(hz); // its start-up and sector times
        if (BackplaneBridge *bridge = backplane_bridge())
            bridge->set_cpu_hz(hz); // the DS3231's second
        if (disk2_card_)
            disk2_card_->set_cpu_hz(hz); // native mode's sequencer steps
        for (Flash *flash : flashes_)
            flash->set_cpu_hz(hz); // its busy times are real time
    }

    Device *Machine::find(const std::string &name) const
    {
        const auto it = named_.find(name);
        return it == named_.end() ? nullptr : it->second;
    }
} // namespace ooe::emu
