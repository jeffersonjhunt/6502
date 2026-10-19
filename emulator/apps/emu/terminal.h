#pragma once

/**
 * @file terminal.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The host terminal as the machine's keyboard and screen.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Only the host half lives here: raw stdin and stdout. What those mean to
 * the machine (a 6821 keyboard, a serial line, a PS/2 keyboard and an LCD)
 * is the Console (console.h), shared with the tests.
 *
 * Keys are read as they arrive and queued, then offered to the console
 * until it takes each one, so typing ahead or pasting is not lost while
 * the program is busy. An LCD is drawn as a box under the text, redrawn
 * in place when it changes, at most every 20 ms. Text output turns a CR
 * into CR LF, but not a CR LF into two.
 *
 * Ctrl-] is the debugger's key (telnet's escape, and no use to an Apple-1):
 * poll_key() keeps it from the machine. The debugger's prompt reads whole
 * lines in the terminal's original mode, so editing and Ctrl-C work as
 * usual there.
 */

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <termios.h>

#include "console.h"

namespace ooe::emu
{
    class Terminal
    {
    public:
        static constexpr uint8_t DEBUG_KEY = 0x1D; ///< Ctrl-]

        /// Puts stdin in raw, non-blocking mode; the destructor restores it,
        /// which is why the app returns from main rather than exiting.
        explicit Terminal(Console &console);
        ~Terminal();
        Terminal(const Terminal &) = delete;
        Terminal &operator=(const Terminal &) = delete;

        /**
         * @brief Read pending keystrokes and offer queued ones to the machine
         * @return DEBUG_KEY if it was pressed (it is not queued), else the
         *         last key read, or 0 for none
         */
        uint8_t poll_key();

        /// Print what the machine sent, and the LCD if it changed.
        void update_screen();

        /// Print `prompt` and read one line in the original (cooked) mode.
        /// @return false at end of input or when a signal interrupts the read
        bool read_line(const char *prompt, std::string &line);
        /// Print debugger output.
        void write(const std::string &text);

    private:
        void draw_lcd();

        Console &console_;
        struct termios saved_{};
        bool restore_{false};
        std::deque<uint8_t> keys_;
        bool last_cr_{false};
        std::vector<std::string> lcd_rows_;
        bool lcd_dirty_{false};
        bool lcd_on_screen_{false}; ///< the box is the last thing printed
        std::chrono::steady_clock::time_point lcd_drawn_{};
    };
} // namespace ooe::emu
