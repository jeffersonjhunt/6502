#pragma once

/**
 * @file apple1.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The Apple-1's keyboard and display, as seen from the PIA's pins.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Woz Monitor drives a 6821: port A is the keyboard (PA7 tied high, a key
 * strobes CA1), port B is the display (PB0-6 the character, PB7 an input
 * that reads busy until the display has taken it). Io is that wiring, for
 * anything standing in for the keyboard and screen: the terminal app and
 * the boot tests share it, so they cannot drift apart.
 *
 * The default machine description (machines/apple1.machine) is compiled in,
 * so the app runs an Apple-1 with no --machine argument.
 */

#include <cstdint>
#include <string>

#include "pia6821.h"

namespace ooe::emu::apple1
{
    /// machines/apple1.machine, embedded at build time.
    const std::string &default_machine();

    /// Name the description gives the PIA the keyboard and display hang off.
    constexpr const char *TERMINAL_PIA = "terminal";

    class Io
    {
    public:
        static constexpr uint8_t PB7_BUSY = 0x80;

        /// Takes over `pia`'s port B write handler; `pia` must outlive this.
        explicit Io(Pia6821 &pia);
        Io(const Io &) = delete;
        Io &operator=(const Io &) = delete;

        /// Present a key (host ASCII) and strobe CA1.
        /// @return false if the previous key has not been read yet (the key is dropped)
        bool key(uint8_t ascii);

        /// Take the character the CPU last sent, if any, and drop PB7 busy.
        /// @return false if nothing is waiting
        bool output(uint8_t &ch);

        /// Apple-1 keyboard encoding: upper case, LF as CR, bit 7 set.
        static uint8_t encode(uint8_t ascii);

    private:
        Pia6821 &pia_;
        uint8_t pending_{0};
        bool have_pending_{false};
    };
} // namespace ooe::emu::apple1
