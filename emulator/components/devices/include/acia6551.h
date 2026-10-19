#pragma once

/**
 * @file acia6551.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief W65C51N ACIA: a serial port, as Eater's board and Woz Monitor use it.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Register select is offset & 3:
 *
 *   0  data (read: received byte; write: transmit)
 *   1  status (read); write: programmed reset
 *   2  command      3  control
 *
 * It is the WDC part, bug included: TDRE (status bit 4) always reads 1,
 * so software cannot tell when the transmitter is free and has to wait out
 * a character time itself (Eater's Woz Monitor's TXDELAY). Here a byte goes
 * to the host the moment it is written, so no delay is ever too short,
 * which is kinder than the chip but never different for correct code.
 *
 * Receiving: a byte from the host sets RDRF and, if command bit 1 is 0,
 * the interrupt (status bit 7, cleared by reading status). Reading data
 * clears RDRF. The receiver and transmitter need DTR (command bit 0), and
 * the transmitter needs command bits 2-3 non-zero, as on the chip; bytes
 * written while it is off are dropped. receive() refuses a byte while the
 * last is unread, so the host waits instead of overrunning: a sender with
 * flow control, which is how a terminal paste should behave.
 *
 * Not modelled: baud rate and character timing, parity, framing and
 * overrun errors, echo mode, DCD/DSR/CTS (all read as asserted), and the
 * transmit interrupt (which the W65C51N does not have either).
 */

#include <cstdint>
#include <functional>

#include "device.h"

namespace ooe::emu
{
    class Acia6551 : public Device
    {
    public:
        static constexpr uint8_t STATUS_RDRF = 0x08;
        static constexpr uint8_t STATUS_TDRE = 0x10;
        static constexpr uint8_t STATUS_IRQ = 0x80;
        static constexpr uint8_t CMD_DTR = 0x01;
        static constexpr uint8_t CMD_RX_IRQ_OFF = 0x02;
        static constexpr uint8_t CMD_TX_ON = 0x0C;

        size_t size() const override { return 4; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return status_ & STATUS_IRQ; }

        // --- the line side --------------------------------------------------
        /// A byte arriving from the host.
        /// @return false if the last one is unread or the receiver is off
        bool receive(uint8_t byte);
        /// Called with each byte the CPU transmits.
        void on_transmit(std::function<void(uint8_t)> f) { transmit_ = std::move(f); }

        uint8_t command() const { return command_; }
        uint8_t control() const { return control_; }

    private:
        uint8_t rx_{0};
        uint8_t status_{STATUS_TDRE};
        uint8_t command_{CMD_RX_IRQ_OFF}; ///< after hardware reset
        uint8_t control_{0};
        std::function<void(uint8_t)> transmit_;
    };
} // namespace ooe::emu
