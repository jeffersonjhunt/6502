/**
 * @file main.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Apple-1 emulator on the host terminal.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 *   emulator [--log FILE] [-v] [rom]     (rom defaults to ./ibasic.rom)
 *
 * Logging is off unless --log is given: stdout and stderr are the Apple-1's
 * screen, and a stray warning in the middle of BASIC output is worse than
 * none. --log writes Info and above; -v adds Debug (per-key).
 */

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple1.h"
#include "cpu6502.h"
#include "emu_log.h"
#include "emulator.h"
#include "terminal.h"

namespace
{
    const char *TAG = "main";

    // Raw mode keeps ISIG, so Ctrl-C arrives as SIGINT rather than as a
    // 0x03 byte. It ends run(), so the keyboard's destructor restores the
    // terminal on the way out.
    volatile std::sig_atomic_t quit_requested = 0;

    void request_quit(int)
    {
        quit_requested = 1;
    }

    int usage()
    {
        std::fprintf(stderr, "usage: emulator [--log FILE] [-v] [rom]\n");
        return 2;
    }
} // namespace

int main(int argc, char *argv[])
{
    using namespace ooe::emu;

    std::string rom = "ibasic.rom";
    const char *log_path = nullptr;
    bool verbose = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc)
            log_path = argv[++i];
        else if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (argv[i][0] == '-')
            return usage();
        else
            rom = argv[i];
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
        apple1::Memory memory;
        // Before the keyboard takes the terminal, so the error is readable
        // and there is nothing to restore.
        if (!apple1::load_rom(memory, rom))
        {
            std::fprintf(stderr, "emulator: cannot load ROM %s\n", rom.c_str());
            rc = 1;
        }
        else
        {
            Cpu6502 cpu(memory);
            TerminalKeyboard keyboard(memory, apple1::KBDCR, apple1::KBD);
            TerminalDisplay display(memory, apple1::DSP);
            Emulator emulator(cpu, keyboard, display);

            std::signal(SIGINT, request_quit);
            std::signal(SIGTERM, request_quit);

            cpu.reset();
            EMU_LOGI(TAG, "running %s from $%04X", rom.c_str(), cpu.regs().pc);
            emulator.run(quit_requested);
            EMU_LOGI(TAG, "exiting");
        }
    }

    if (log_file)
        std::fclose(log_file);
    return rc;
}
