#pragma once

/**
 * @file via6522.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief W65C22 VIA: two ports with handshake lines, two timers.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Register select is offset & 15:
 *
 *   0 ORB/IRB   4 T1C-L   8 T2C-L   C PCR
 *   1 ORA/IRA   5 T1C-H   9 T2C-H   D IFR
 *   2 DDRB      6 T1L-L   A SR      E IER
 *   3 DDRA      7 T1L-H   B ACR     F ORA/IRA, no handshake
 *
 * Timers count CPU cycles, kept in lockstep by the decoder (device.h),
 * whose tick for a cycle arrives after that cycle's access. T1 is loaded
 * by writing T1C-H; the write's own cycle does not count, the counter
 * reaches 0 N cycles later and times out on the next, so the flag is set
 * N+1.5 cycles after the write as the datasheet says: a read N+2 cycles
 * after it is the first to see it. In free-running mode (ACR bit 6) it
 * reloads from the latch every N+2 cycles. With ACR bit 7, PB7 is T1's output: low from the load to the
 * timeout in one-shot mode, inverted at each timeout in free-running mode.
 * T2 is one-shot, or counts PB6 falling edges with ACR bit 5 and sets its
 * flag after N of them (datasheets word this differently; unchecked on
 * silicon).
 *
 * Interrupts: IFR bits are set by CA1/CA2/CB1/CB2 active edges (chosen in
 * the PCR) and timer timeouts, and cleared by the usual accesses (reading
 * or writing the port clears its C1/C2 flags, except C2 in "independent"
 * mode; reading T1C-L or writing T1C-H/T1L-H clears T1; T2C-L/T2C-H clear
 * T2; writing 1s to IFR clears them). IRQ is IFR & IER.
 *
 * Not modelled: the shift register (SR is a plain register and never
 * interrupts), input latching (ACR bits 0-1), and CA2/CB2 handshake and
 * pulse outputs (they stay high). CA2/CB2 as inputs and as manual outputs
 * (PCR 110/111) are modelled.
 *
 * The peripheral side mirrors Pia6821: listeners are told about the pins
 * the VIA drives whenever an output register, a DDR or PB7 changes, and
 * peripherals drive input pins by mask, so several can share a port (an
 * LCD on four bits of port B and its control lines on three more).
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "device.h"

namespace ooe::emu
{
    class Via6522 : public Device
    {
    public:
        enum class Port
        {
            A,
            B,
        };

        // IFR / IER bits.
        static constexpr uint8_t IRQ_CA2 = 0x01;
        static constexpr uint8_t IRQ_CA1 = 0x02;
        static constexpr uint8_t IRQ_SR = 0x04;
        static constexpr uint8_t IRQ_CB2 = 0x08;
        static constexpr uint8_t IRQ_CB1 = 0x10;
        static constexpr uint8_t IRQ_T2 = 0x20;
        static constexpr uint8_t IRQ_T1 = 0x40;
        static constexpr uint8_t IRQ_ANY = 0x80;

        // ACR bits.
        static constexpr uint8_t ACR_T2_COUNT_PB6 = 0x20;
        static constexpr uint8_t ACR_T1_FREE_RUN = 0x40;
        static constexpr uint8_t ACR_T1_PB7 = 0x80;

        /// `out` is the output register (PB7 replaced by T1's output when
        /// ACR bit 7 is set); `ddr` says which of those pins are driven.
        using Listener = std::function<void(Port port, uint8_t out, uint8_t ddr)>;

        size_t size() const override { return 16; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (ifr_ & ier_ & 0x7F) != 0; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        // --- the peripheral side -------------------------------------------
        void listen(Listener l) { listeners_.push_back(std::move(l)); }
        /// Drive the input pins in `mask` to `value`; other pins keep theirs.
        void drive(Port port, uint8_t mask, uint8_t value);
        void set_ca1(bool level);
        void set_ca2(bool level);
        void set_cb1(bool level);
        void set_cb2(bool level);

        uint8_t output(Port port) const;
        uint8_t ddr(Port port) const { return port == Port::A ? ddra_ : ddrb_; }
        uint8_t ifr() const { return peek(0xD); }
        uint16_t t1_counter() const { return t1_; }
        uint16_t t2_counter() const { return t2_; }
        /// CA2/CB2 in manual output mode (PCR 110 low, 111 high); else high.
        bool ca2_out() const { return (pcr_ & 0x0E) != 0x0C; }
        bool cb2_out() const { return (pcr_ & 0xE0) != 0xC0; }

    private:
        uint8_t port_value(Port port) const;
        void notify(Port port);
        void clear_port_flags(Port port);
        void t1_timeout();
        void advance_t1(uint64_t n);
        void advance_t2(uint64_t n);
        /// Set `flag` on the active edge of a control line.
        void edge(bool &line, bool level, bool rising_active, uint8_t flag);

        uint8_t ora_{0}, orb_{0}, ddra_{0}, ddrb_{0};
        uint8_t in_a_{0}, in_b_{0};
        uint16_t t1_{0xFFFF}, t1_latch_{0xFFFF}, t2_{0xFFFF};
        uint8_t t2_latch_lo_{0xFF};
        bool t1_reload_{false}; ///< free-run: reload from the latch on the next cycle
        bool t1_hold_{false};   ///< the next cycle is the load's own: no decrement
        bool t2_hold_{false};
        bool t1_armed_{false};  ///< one-shot: an interrupt is still due
        bool t2_armed_{false};
        bool pb7_{true};        ///< T1's output on PB7
        uint8_t sr_{0}, acr_{0}, pcr_{0}, ifr_{0}, ier_{0};
        bool ca1_{false}, ca2_{false}, cb1_{false}, cb2_{false};
        std::vector<Listener> listeners_;
    };
} // namespace ooe::emu
