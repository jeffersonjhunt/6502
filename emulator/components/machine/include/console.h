#pragma once

/**
 * @file console.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A machine's keyboard and screen, whatever they are wired to.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The host terminal (or a test) talks to a Console; make_console() builds
 * one from what the machine description names:
 *
 *   terminal  a pia6821 (the Apple-1's keyboard and display, apple1::Io)
 *             or an acia6551 (a serial terminal: bytes both ways)
 *   lcd       an hd44780, shown as its rows
 *   keyboard  a ps2kbd
 *
 * A key goes to every input that is listening: the PIA always, the ACIA
 * once DTR is on, the PS/2 keyboard once the program has enabled its CA1
 * interrupt. So one Eater machine runs Woz Monitor over serial or
 * keyboard.s on the LCD without being told which. If a listening input is
 * not ready (the last key unread), key() returns false and the host offers
 * it again later; with nothing listening the key is dropped.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "machine.h"

namespace ooe::emu
{
    class Console
    {
    public:
        virtual ~Console() = default;
        /// Offer a host key. @return false to offer it again later
        virtual bool key(uint8_t ascii) = 0;
        /// The next character for the host's screen, if any.
        virtual bool output(uint8_t &ch) = 0;
        /// Once per host poll: lets queued input move.
        virtual void service() {}
        /// The LCD's rows, if it has changed since the last call.
        virtual bool display(std::vector<std::string> &rows)
        {
            (void)rows;
            return false;
        }
    };

    /// @return nullptr, with `error`, if the machine has nothing to talk to
    std::unique_ptr<Console> make_console(Machine &machine, std::string &error);

    /// Name a description gives each part (see above).
    constexpr const char *TERMINAL = "terminal";
    constexpr const char *LCD = "lcd";
    constexpr const char *KEYBOARD = "keyboard";
} // namespace ooe::emu
