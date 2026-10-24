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
 *            [--apple-rom FILE[,FILE]] [--apple-chars FILE] [rom]
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
 *   16 KB file, or the CD and EF halves), and --apple-chars its character
 *   ROM; C806R at the monitor enters Apple II mode with them.
 */

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    // terminal on the way out. Installed without SA_RESTART, so that it
    // also interrupts a read blocked at the debugger's prompt.
    volatile std::sig_atomic_t quit_requested = 0;

    void request_quit(int)
    {
        quit_requested = 1;
    }

    void on_signal(int sig)
    {
        struct sigaction sa{};
        sa.sa_handler = request_quit;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(sig, &sa, nullptr);
    }

    int usage()
    {
        std::fprintf(stderr, "usage: emulator [--machine FILE] [--clock HZ] [--log FILE] [-v]\n"
                             "                [--load FILE[@ADDR]]... [--symbols FILE]... [--trace FILE]\n"
                             "                [--break ADDR]... [--halt] [--screen FILE]\n"
                             "                [--apple-rom FILE[,FILE]] [--apple-chars FILE] [rom]\n");
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
        char *end = nullptr;
        const unsigned long addr =
            at == std::string::npos ? 0x10000 : std::strtoul(spec.c_str() + at + 1, &end, 16);
        if (at == std::string::npos || *end != '\0' || addr > 0xFFFF)
        {
            error = spec + ": a raw image needs a hex load address, FILE@ADDR";
            return false;
        }
        if (ooe::emu::load_image(bus, path, static_cast<uint16_t>(addr)) < 0)
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
            Terminal terminal(console);
            Emulator emulator(cpu, machine.bus(), terminal, debugger, settings.clock_hz, machine.console_card());
            emulator.on_clock_change([&machine](uint32_t hz) {
                if (VideoCard *video = machine.video_card())
                    video->set_cpu_hz(hz);
                if (UsbCard *usb = machine.usb_card())
                    usb->set_cpu_hz(hz);
            });

            on_signal(SIGINT);
            on_signal(SIGTERM);

            machine.bus().reset();
            cpu.reset();
            EMU_LOGI(TAG, "%s from $%04X at %u Hz", machine.cpu().c_str(), cpu.regs().pc, settings.clock_hz);
            if (settings.start_halted)
                terminal.write(debugger.halt("--halt"));
            emulator.run(quit_requested);
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
    const char *machine_path = nullptr;
    const char *log_path = nullptr;
    Settings settings;
    bool verbose = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc)
            log_path = argv[++i];
        else if (std::strcmp(argv[i], "--machine") == 0 && i + 1 < argc)
            machine_path = argv[++i];
        else if (std::strcmp(argv[i], "--clock") == 0 && i + 1 < argc)
        {
            char *end = nullptr;
            const unsigned long hz = std::strtoul(argv[++i], &end, 10);
            if (*end != '\0' || hz > 100'000'000ul)
                return usage();
            settings.clock_hz = static_cast<uint32_t>(hz);
        }
        else if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            settings.loads.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "--symbols") == 0 && i + 1 < argc)
            settings.symbol_files.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "--trace") == 0 && i + 1 < argc)
            settings.trace_path = argv[++i];
        else if (std::strcmp(argv[i], "--break") == 0 && i + 1 < argc)
            settings.breaks.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "--halt") == 0)
            settings.start_halted = true;
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc)
            settings.screen_path = argv[++i];
        else if (std::strcmp(argv[i], "--apple-rom") == 0 && i + 1 < argc)
        {
            std::string list = argv[++i];
            for (size_t at = 0; at <= list.size();)
            {
                const size_t comma = std::min(list.find(',', at), list.size());
                options.apple_rom.push_back(list.substr(at, comma - at));
                at = comma + 1;
            }
        }
        else if (std::strcmp(argv[i], "--apple-chars") == 0 && i + 1 < argc)
            options.apple_chars = argv[++i];
        else if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (argv[i][0] == '-')
            return usage();
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
        std::unique_ptr<Console> console = machine ? make_console(*machine, error) : nullptr;
        if (!console)
        {
            std::fprintf(stderr, "emulator: %s\n", error.c_str());
            rc = 1;
        }
        else
        {
            rc = run(*machine, *console, settings);
        }
    }

    if (log_file)
        std::fclose(log_file);
    return rc;
}
