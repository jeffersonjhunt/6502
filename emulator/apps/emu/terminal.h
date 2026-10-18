#pragma once

/**
 * @file terminal.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The host terminal as the Apple-1's keyboard and display.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Both sit on the device side of the PIA: they poke() its registers, which
 * the CPU's own stores cannot reach (apple1.h). Phase 3 moves the register
 * logic into a Pia6821 device and leaves only the terminal here.
 */

#include <cstdint>

#include <termios.h>

#include "bus.h"

namespace ooe::emu
{
    /**
     * @brief stdin in raw, non-blocking mode, feeding the PIA's keyboard port
     *
     * Restores the terminal in its destructor, which is why the app must
     * return from main rather than exit (Phase 1: Ctrl-C used to skip it).
     */
    class TerminalKeyboard
    {
    public:
        TerminalKeyboard(Bus &bus, uint16_t ctrl_addr, uint16_t data_addr);
        ~TerminalKeyboard();
        TerminalKeyboard(const TerminalKeyboard &) = delete;
        TerminalKeyboard &operator=(const TerminalKeyboard &) = delete;

        /**
         * @brief Pass one pending keystroke to the PIA, if it is free
         * @return the raw key, or 0 for none (or dropped: the last is unread)
         */
        uint8_t poll();

        /// Apple-1 keyboard encoding: upper case, LF as CR, bit 7 set.
        static uint8_t encode(uint8_t key);

    private:
        Bus &bus_;
        uint16_t ctrl_addr_;
        uint16_t data_addr_;
        struct termios saved_{};
        bool restore_{false};
    };

    /// stdout, fed from the PIA's display port.
    class TerminalDisplay
    {
    public:
        TerminalDisplay(Bus &bus, uint16_t data_addr) : bus_(bus), data_addr_(data_addr) {}

        /// Print the pending character, if any, and clear display-busy.
        void update();

    private:
        Bus &bus_;
        uint16_t data_addr_;
    };
} // namespace ooe::emu
