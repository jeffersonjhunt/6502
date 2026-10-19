#pragma once

/**
 * @file ps2_keyboard.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A PS/2 keyboard as Eater wires one: scan codes on a 6522 port.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Eater's keyboard interface shifts each PS/2 byte into a register on port
 * A and pulses CA1, and keyboard.s decodes scan code set 2 in its IRQ
 * handler. This turns host ASCII into those bytes: the key's make code,
 * then F0 and the code again for the release, wrapped in left-shift
 * make/break for shifted characters. CR and LF are Enter, 08/7F Backspace,
 * 1B Escape.
 *
 * One byte is presented at a time, and the next only once the CPU has
 * read the port (the VIA's CA1 flag is clear), so a fast typist or a paste
 * is paced by the program rather than lost. Real PS/2 pacing is ~1 ms a
 * byte; nothing here depends on it. A pulse on CA1 is both edges, so
 * either PCR edge setting sees it.
 */

#include <cstdint>
#include <deque>

#include "peripheral.h"
#include "via6522.h"

namespace ooe::emu
{
    class Ps2Keyboard : public Peripheral
    {
    public:
        static constexpr size_t MAX_QUEUED = 1024; ///< bytes; beyond that keys are dropped

        Ps2Keyboard(Via6522 &via, ViaPins data) : via_(via), data_(data) {}

        /// Queue a host key's scan codes. @return false if it has none, or the queue is full
        bool key(uint8_t ascii);
        /// Present the next byte if the last has been read. Call it often.
        void service();
        bool idle() const { return queue_.empty(); }
        /// Whether the program wants keys: it has enabled the CA1 interrupt.
        bool listening() const { return via_.peek(0xE) & Via6522::IRQ_CA1; }

        /// Set-2 make code for `ascii`, and whether it needs shift; 0 if none.
        static uint8_t scan_code(uint8_t ascii, bool &shift);

    private:
        Via6522 &via_;
        ViaPins data_;
        std::deque<uint8_t> queue_;
        bool presented_{false}; ///< a byte is on the port, waiting to be read
    };
} // namespace ooe::emu
