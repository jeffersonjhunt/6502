#pragma once

/**
 * @file terminal.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The host terminal as the Apple-1's keyboard and screen.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Only the host half lives here: raw stdin and stdout. What those mean to
 * the 6821 (key encoding, CA1 strobe, PB7 busy) is apple1::Io, shared with
 * the boot tests.
 */

#include <cstdint>

#include <termios.h>

#include "apple1.h"

namespace ooe::emu
{
    class Terminal
    {
    public:
        /// Puts stdin in raw, non-blocking mode; the destructor restores it,
        /// which is why the app returns from main rather than exiting.
        explicit Terminal(apple1::Io &io);
        ~Terminal();
        Terminal(const Terminal &) = delete;
        Terminal &operator=(const Terminal &) = delete;

        /**
         * @brief Pass one pending keystroke to the machine
         * @return the raw key, or 0 for none (or dropped: the last is unread)
         */
        uint8_t poll_key();

        /// Print the character the machine sent, if any.
        void update_screen();

    private:
        apple1::Io &io_;
        struct termios saved_{};
        bool restore_{false};
    };
} // namespace ooe::emu
