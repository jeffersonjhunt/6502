/**
 * @file main.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Apple-1 emulator on the host terminal.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 *   emulator [--machine FILE] [--clock HZ] [--log FILE] [-v]
 *            [--load FILE[@ADDR]]... [--symbols FILE]... [--trace FILE]
 *            [--break ADDR]... [--halt] [--screen FILE]
 *            [--apple-rom FILE[,FILE]]... [--apple-chars FILE]
 *            [--disk2-rom BOOT,SEQUENCER] [--disk FILE] [--sd FILE] [rom]
 *
 * The machine defaults to the built-in Apple-1 (machines/apple1.machine);
 * `rom` replaces its first ROM image (default ./ibasic.rom). The terminal
 * talks to what the description names `terminal` (a 6821 keyboard and
 * display, or a 6551 serial line), `lcd` and `keyboard` (console.h). The
 * CPU model is
 * the description's `cpu` line. --clock sets the emulated clock (default
 * 1 MHz, real time for an Apple-1); --clock 0 runs as fast as the host can.
 *
 * Logging is off unless --log is given: stdout and stderr are the Apple-1's
 * screen, and a stray warning in the middle of BASIC output is worse than
 * none. --log writes Info and above; -v adds Debug (per-key).
 *
 * Bring-up (Phase 5):
 * - --load puts an image on the bus before reset, past ROM's write
 *   protection: Intel HEX if it ends in .hex or .ihx, else raw at @ADDR.
 * - --symbols reads an ld65 -Ln label file for the debugger.
 * - --trace writes every bus cycle in the bus spy's format
 *   (docs/BUS_TRACE.md), from the reset sequence on.
 * - --break sets a breakpoint (hex or label); --halt starts at the
 *   debugger's prompt, like the console's system key at Step/Halt.
 * - Ctrl-] stops a running machine into the debugger ('h' for commands).
 * - --screen writes the video card's last frame (if the machine has one)
 *   to FILE as a PPM on the way out.
 * - --apple-rom plugs the owner's Apple IIe ROM into the cartridge (one
 *   16 KB file, or the CD and EF halves, in one option or two), and
 *   --apple-chars its character ROM; C806R at the monitor enters Apple II mode with them.
 * - --disk2-rom gives the Disk II card the owner's two PROMs (the 16-sector
 *   boot ROM and the sequencer, 256 bytes each), and --disk puts a .woz,
 *   .dsk, .do or .po in drive 1; the IIe boots it. What is written to the
 *   disk stays in memory: the file is not changed.
 * - --sd puts a card image (whole 512-byte sectors) in the mass storage
 *   card's slot. Unlike a disk, it is written through: the machine's writes
 *   change the file, as they would a real card. A file that can't be
 *   written is a write-protected card.
 */

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <signal.h>

#include "apple1.h"
#include "bus_trace.h"
#include "console.h"
#include "cpu6502.h"
#include "debugger.h"
#include "emu_log.h"
#include "emulator.h"
#include "loader.h"
#include "machine.h"
#include "symbols.h"
#include "terminal.h"

namespace
{
    const char *TAG = "main";

    // Raw mode keeps ISIG, so Ctrl-C arrives as SIGINT rather than as a
    // 0x03 byte. It ends run(), so the keyboard's destructor restores the
    // terminal on the way out; so do SIGTERM, SIGHUP, SIGQUIT (Ctrl-\) and
    // SIGPIPE, whose default actions would leave it raw. Installed without
    // SA_RESTART, so that it also interrupts a read blocked at the
    // debugger's prompt. Ctrl-C during a debugger command (a long step)
    // only ends the command. Ctrl-Z is the Terminal's (terminal.h).
    ooe::emu::RunSignals run_signals;

    void request_quit(int sig)
    {
        if (sig == SIGINT && run_signals.in_command)
            run_signals.interrupt = 1;
        else
            run_signals.quit = 1;
    }

    void on_signal(int sig)
    {
        struct sigaction sa{};
        sa.sa_handler = request_quit;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(sig, &sa, nullptr);
    }

    /// An exception nobody catches, or one thrown where it can't be: no
    /// destructor runs, so put the terminal back here.
    [[noreturn]] void on_terminate()
    {
        ooe::emu::Terminal::restore();
        std::fputs("emulator: terminated\n", stderr);
        std::abort();
    }

    int usage(const std::string &why = "")
    {
        if (!why.empty())
            std::fprintf(stderr, "emulator: %s\n", why.c_str());
        std::fprintf(stderr, "usage: emulator [--machine FILE] [--clock HZ] [--log FILE] [-v]\n"
                             "                [--load FILE[@ADDR]]... [--symbols FILE]... [--trace FILE]\n"
                             "                [--break ADDR]... [--halt] [--screen FILE]\n"
                             "                [--apple-rom FILE[,FILE]]... [--apple-chars FILE]\n"
                             "                [--disk2-rom BOOT,SEQUENCER] [--disk FILE] [--sd FILE] [rom]\n");
        return 2;
    }

    bool ends_with(const std::string &s, const char *suffix)
    {
        const std::string x = suffix;
        return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
    }

    /// --load FILE[@ADDR]: Intel HEX by extension, else raw at ADDR.
    bool load(ooe::emu::Bus &bus, const std::string &spec, std::string &error)
    {
        const size_t at = spec.rfind('@');
        const std::string path = spec.substr(0, at);
        if (ends_with(path, ".hex") || ends_with(path, ".ihx"))
        {
            if (at != std::string::npos)
            {
                error = spec + ": a HEX file carries its own addresses; drop the @ADDR";
                return false;
            }
            return ooe::emu::load_hex(bus, path, error) >= 0;
        }
        const std::string hex = at == std::string::npos ? "" : spec.substr(at + 1);
        if (hex.empty() || hex.size() > 4 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        {
            error = spec + ": a raw image needs a hex load address, FILE@ADDR";
            return false;
        }
        const auto addr = static_cast<uint16_t>(std::strtoul(hex.c_str(), nullptr, 16));
        if (ooe::emu::load_image(bus, path, addr) < 0)
        {
            error = path + ": cannot read it, or it does not fit at $" + spec.substr(at + 1);
            return false;
        }
        return true;
    }

    struct Settings
    {
        const char *trace_path{nullptr};
        const char *screen_path{nullptr};
        std::vector<std::string> loads, symbol_files, breaks;
        uint32_t clock_hz{1'000'000};
        bool start_halted{false};
    };

    /// Everything after the machine is built. Errors are reported before
    /// the terminal goes raw, so they are readable and nothing needs restoring.
    int run(ooe::emu::Machine &machine, ooe::emu::Console &console, const Settings &settings)
    {
        using namespace ooe::emu;
        std::string error;
        for (const std::string &spec : settings.loads)
        {
            if (!load(machine.bus(), spec, error))
            {
                std::fprintf(stderr, "emulator: %s\n", error.c_str());
                return 1;
            }
        }
        Symbols symbols;
        for (const std::string &path : settings.symbol_files)
        {
            if (!symbols.load(path, error))
            {
                std::fprintf(stderr, "emulator: %s\n", error.c_str());
                return 1;
            }
        }

        const auto model = machine.cpu() == "w65c02s" ? Cpu6502::Model::W65C02S : Cpu6502::Model::Nmos6502;
        Cpu6502 cpu(machine.bus(), model);

        CycleTee observers; // the trace and the Console card both watch the bus
        std::FILE *trace_file = nullptr;
        std::unique_ptr<BusTrace> trace;
        if (settings.trace_path)
        {
            trace_file = std::fopen(settings.trace_path, "w");
            if (!trace_file)
            {
                std::fprintf(stderr, "emulator: cannot write trace %s\n", settings.trace_path);
                return 1;
            }
            std::setvbuf(trace_file, nullptr, _IOFBF, 1 << 20);
            trace = std::make_unique<BusTrace>(trace_file, machine.bus(), "cpu " + machine.cpu());
            observers.add(trace.get());
        }
        if (ConsoleCard *card = machine.console_card())
            observers.add(card);
        if (!observers.empty())
            cpu.set_observer(&observers);

        Debugger debugger(cpu, machine.bus(), symbols, trace.get());
        if (const BackplaneBridge *bridge = machine.backplane_bridge())
            debugger.add_view("rtc", "   rtc                 the real-time clock: time, RTCCTL, the DS3231's registers\n",
                              [bridge] { return bridge->describe_clock(); });
        int rc = 0;
        for (const std::string &b : settings.breaks)
        {
            uint16_t addr = 0;
            if (debugger.parse_address(b, addr))
                debugger.add_breakpoint(addr);
            else
            {
                std::fprintf(stderr, "emulator: --break %s: not a hex address or a known label\n", b.c_str());
                rc = 1;
            }
        }

        if (rc == 0)
        {
            if (VideoCard *video = machine.video_card())
                video->set_cpu_hz(settings.clock_hz); // 0, flat out: the card keeps 7.16 MHz time
            if (UsbCard *usb = machine.usb_card())
                usb->set_cpu_hz(settings.clock_hz); // key repeat and the paddles
            if (SoundCard *sound = machine.sound_card())
                sound->set_cpu_hz(settings.clock_hz); // its 6510 keeps the SID clock's time
            if (SdCard *sd = machine.sd_card())
                sd->set_cpu_hz(settings.clock_hz); // its start-up and sector times
            if (BackplaneBridge *bridge = machine.backplane_bridge())
                bridge->set_cpu_hz(settings.clock_hz); // the DS3231's second
            Terminal terminal(console);
            Emulator emulator(cpu, machine.bus(), terminal, debugger, settings.clock_hz, machine.console_card());
            emulator.on_clock_change([&machine](uint32_t hz) {
                if (VideoCard *video = machine.video_card())
                    video->set_cpu_hz(hz);
                if (UsbCard *usb = machine.usb_card())
                    usb->set_cpu_hz(hz);
                if (SoundCard *sound = machine.sound_card())
                    sound->set_cpu_hz(hz);
                if (SdCard *sd = machine.sd_card())
                    sd->set_cpu_hz(hz);
                if (BackplaneBridge *bridge = machine.backplane_bridge())
                    bridge->set_cpu_hz(hz);
            });

            for (const int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE})
                on_signal(sig);

            machine.bus().reset();
            cpu.reset();
            EMU_LOGI(TAG, "%s from $%04X at %u Hz", machine.cpu().c_str(), cpu.regs().pc, settings.clock_hz);
            if (settings.start_halted)
                terminal.write(debugger.halt("--halt"));
            emulator.run(run_signals);
            EMU_LOGI(TAG, "exiting");
            if (settings.screen_path)
            {
                const VideoCard *video = machine.video_card();
                if (!video || !write_ppm(*video, settings.screen_path))
                {
                    std::fprintf(stderr, "emulator: --screen %s: %s\n", settings.screen_path,
                                 video ? "cannot write it" : "the machine has no video card");
                    rc = 1;
                }
            }
        }

        cpu.set_observer(nullptr);
        if (trace_file)
            std::fclose(trace_file);
        return rc;
    }
} // namespace

int main(int argc, char *argv[])
{
    using namespace ooe::emu;

    MachineOptions options;
    const char *disk_path = nullptr;
    const char *sd_path = nullptr;
    const char *machine_path = nullptr;
    const char *log_path = nullptr;
    Settings settings;
    bool verbose = false;
    // Strict: an option's value must be there and not be the next option,
    // and one that takes a single value may not be given twice.
    std::vector<std::string> given;
    for (int i = 1; i < argc; ++i)
    {
        const std::string opt = argv[i];
        const bool repeatable = opt == "--load" || opt == "--symbols" || opt == "--break" || opt == "--apple-rom";
        static const char *const WITH_VALUE[] = {"--log",       "--machine",     "--clock",     "--load",
                                                 "--symbols",   "--trace",       "--break",     "--screen",
                                                 "--apple-rom", "--apple-chars", "--disk2-rom", "--disk",
                                                 "--sd"};
        const bool takes_value = std::any_of(std::begin(WITH_VALUE), std::end(WITH_VALUE),
                                             [&](const char *o) { return opt == o; });
        if (opt[0] != '-' || takes_value)
        {
            const std::string key = opt[0] == '-' ? opt : "rom";
            if (!repeatable && std::find(given.begin(), given.end(), key) != given.end())
                return usage(key + " given twice");
            given.push_back(key);
        }
        if (takes_value && (i + 1 >= argc || argv[i + 1][0] == '-' || argv[i + 1][0] == '\0'))
            return usage(opt + " needs a value");
        if (opt == "--log")
            log_path = argv[++i];
        else if (opt == "--machine")
            machine_path = argv[++i];
        else if (opt == "--clock")
        {
            char *end = nullptr;
            const unsigned long hz = std::strtoul(argv[++i], &end, 10);
            if (*end != '\0' || hz > 100'000'000ul)
                return usage();
            settings.clock_hz = static_cast<uint32_t>(hz);
        }
        else if (opt == "--load")
            settings.loads.emplace_back(argv[++i]);
        else if (opt == "--symbols")
            settings.symbol_files.emplace_back(argv[++i]);
        else if (opt == "--trace")
            settings.trace_path = argv[++i];
        else if (opt == "--break")
            settings.breaks.emplace_back(argv[++i]);
        else if (opt == "--halt")
            settings.start_halted = true;
        else if (opt == "--screen")
            settings.screen_path = argv[++i];
        else if (opt == "--apple-rom")
        {
            std::string list = argv[++i];
            for (size_t at = 0; at <= list.size();)
            {
                const size_t comma = std::min(list.find(',', at), list.size());
                options.apple_rom.push_back(list.substr(at, comma - at));
                at = comma + 1;
            }
        }
        else if (opt == "--apple-chars")
            options.apple_chars = argv[++i];
        else if (opt == "--disk2-rom")
        {
            const std::string pair = argv[++i];
            const size_t comma = pair.find(',');
            if (comma == std::string::npos)
                return usage();
            options.disk2_boot = pair.substr(0, comma);
            options.disk2_sequencer = pair.substr(comma + 1);
        }
        else if (opt == "--disk")
            disk_path = argv[++i];
        else if (opt == "--sd")
            sd_path = argv[++i];
        else if (opt == "-v")
            verbose = true;
        else if (opt[0] == '-')
            return usage(opt == "-" ? "" : "unknown option " + opt);
        else
            options.rom_override = argv[i];
    }

    std::FILE *log_file = nullptr;
    if (log_path)
    {
        log_file = std::fopen(log_path, "w");
        if (!log_file)
        {
            std::fprintf(stderr, "emulator: cannot open log %s\n", log_path);
            return 1;
        }
        log::set_sink(log_file);
        log::set_level(verbose ? log::Level::Debug : log::Level::Info);
    }
    else
    {
        log::set_level(log::Level::None);
    }

    int rc = 0;
    {
        // Before the terminal takes over stdin, so an error is readable and
        // there is nothing to restore.
        std::string error;
        std::unique_ptr<Machine> machine =
            machine_path ? Machine::load(machine_path, options, error)
                         : Machine::build(apple1::default_machine(), "apple1.machine (built in)", options, error);
        if (machine && disk_path)
        {
            std::ifstream in(disk_path, std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::unique_ptr<DiskImage> disk = in || in.eof() ? DiskImage::load(bytes, disk_path, error) : nullptr;
            if (!in && !in.eof())
                error = std::string(disk_path) + ": cannot read it";
            if (!machine->disk2_card() && disk)
                error = std::string(disk_path) + ": the machine has no Disk II card (a disk2 line)";
            if (disk && machine->disk2_card())
                machine->disk2_card()->insert(0, std::move(disk));
            else
                machine.reset();
        }
        if (machine && sd_path)
        {
            std::shared_ptr<SdMedia> card = FileMedia::open(sd_path, error);
            if (card && !machine->sd_card())
                error = std::string(sd_path) + ": the machine has no SD card slot (an sd line)";
            if (card && machine->sd_card())
                machine->sd_card()->insert(std::move(card), false);
            else
                machine.reset();
        }
        std::unique_ptr<Console> console = machine ? make_console(*machine, error) : nullptr;
        if (!console)
        {
            std::fprintf(stderr, "emulator: %s\n", error.c_str());
            rc = 1;
        }
        else
        {
            std::set_terminate(on_terminate);
            try
            {
                rc = run(*machine, *console, settings);
            }
            catch (const std::exception &e) // unwound: the terminal is already restored
            {
                std::fprintf(stderr, "emulator: %s\n", e.what());
                rc = 1;
            }
        }
    }

    if (log_file)
        std::fclose(log_file);
    return rc;
}
