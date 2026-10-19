#pragma once

/**
 * @file terminal.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The host terminal as the Apple-1's keyboard and screen.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Only the host half lives here: raw stdin and stdout. What those mean to
 * the 6821 (key encoding, CA1 strobe, PB7 busy) is apple1::Io, shared with
 * the boot tests.
 *
 * Ctrl-] is the debugger's key (telnet's escape, and no use to an Apple-1):
 * poll_key() keeps it from the machine. The debugger's prompt reads whole
 * lines in the terminal's original mode, so editing and Ctrl-C work as
 * usual there.
 */

#include <cstdint>
#include <string>

#include <termios.h>

#include "apple1.h"

namespace ooe::emu
{
    class Terminal
    {
    public:
        static constexpr uint8_t DEBUG_KEY = 0x1D; ///< Ctrl-]

        /// Puts stdin in raw, non-blocking mode; the destructor restores it,
        /// which is why the app returns from main rather than exiting.
        explicit Terminal(apple1::Io &io);
        ~Terminal();
        Terminal(const Terminal &) = delete;
        Terminal &operator=(const Terminal &) = delete;

        /**
         * @brief Pass one pending keystroke to the machine
         * @return the raw key, or 0 for none (or dropped: the last is unread).
         *         DEBUG_KEY is returned but not passed on.
         */
        uint8_t poll_key();

        /// Print the character the machine sent, if any.
        void update_screen();

        /// Print `prompt` and read one line in the original (cooked) mode.
        /// @return false at end of input or when a signal interrupts the read
        bool read_line(const char *prompt, std::string &line);
        /// Print debugger output.
        void write(const std::string &text);

    private:
        apple1::Io &io_;
        struct termios saved_{};
        bool restore_{false};
    };
} // namespace ooe::emu
