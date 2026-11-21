#pragma once

/**
 * @file acia6551.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief W65C51N ACIA: a serial port, as Eater's board and Woz Monitor use it.
 * @version 0.7.0
 * @date 2026-11-21
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
 * The paced line (send_paced()): a sender with no flow control, as a real
 * line is. Its bytes arrive one a period of CPU cycles, the period
 * CONTROL's rate and word (start bit, 5-8 data bits, 1 or 2 stop bits; the
 * W65C51N has no parity) at the backplane's 7,159,090 Hz, or a test's
 * own: 19,200 baud 8-N-1 is 3,729 cycles. A byte that arrives while RDRF
 * is set is lost: the held byte stays, OVRN (status bit 2) is set, and
 * overruns() counts it. No interrupt comes of it, and OVRN is cleared by
 * the next read of data, as WDC's datasheet has both. A byte that arrives
 * with DTR off is dropped (dropped()). rts(): RTS is low (asserted) while
 * command bits 3-2 aren't 00, for a sender that would honour it; the paced
 * sender doesn't.
 *
 * Not modelled: the waiting sender's character timing, parity (the part
 * has none) and framing errors, echo mode, DCD/DSR/CTS (all read as
 * asserted), and the transmit interrupt (which the W65C51N does not have
 * either).
 */

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "device.h"

namespace ooe::emu
{
    class Acia6551 : public Device
    {
    public:
        static constexpr uint8_t STATUS_OVRN = 0x04;
        static constexpr uint8_t STATUS_RDRF = 0x08;
        static constexpr uint8_t STATUS_TDRE = 0x10;
        static constexpr uint8_t STATUS_IRQ = 0x80;
        static constexpr uint8_t CMD_DTR = 0x01;
        static constexpr uint8_t CMD_RX_IRQ_OFF = 0x02;
        static constexpr uint8_t CMD_TX_ON = 0x0C;
        static constexpr uint32_t CPU_HZ = 7'159'090; ///< the backplane's clock, the paced line's periods'

        size_t size() const override { return 4; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return status_ & STATUS_IRQ; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        // --- the line side --------------------------------------------------
        /// A byte arriving from the host.
        /// @return false if the last one is unread or the receiver is off
        bool receive(uint8_t byte);
        /// Bytes on the paced line, after any still waiting there: one a period whatever RDRF says.
        /// `cycles_per_byte` 0: the period CONTROL's rate and word give at each byte (period()).
        void send_paced(const std::string &bytes, uint32_t cycles_per_byte = 0);
        /// The paced line's bytes not yet arrived.
        size_t paced_waiting() const { return paced_.size(); }
        /// Paced bytes lost to an overrun (RDRF still set when they came), and dropped with DTR off.
        uint64_t overruns() const { return overruns_; }
        uint64_t dropped() const { return dropped_; }
        /// A character's CPU cycles at CONTROL's rate and word, at CPU_HZ.
        uint32_t period() const;
        /// RTS low (asserted): command bits 3-2 not 00.
        bool rts() const { return command_ & CMD_TX_ON; }
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
        std::deque<uint8_t> paced_;
        uint32_t paced_period_{0}; ///< 0: period()
        uint64_t paced_due_{0};    ///< cycles to the next paced byte
        uint64_t overruns_{0};
        uint64_t dropped_{0};
    };
} // namespace ooe::emu
