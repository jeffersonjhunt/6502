#pragma once

/**
 * @file pia6821.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Motorola 6821 PIA: two 8-bit ports with handshake flags.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Register select is offset & 3:
 *
 *   0  CRA bit 2 ? port A data : DDRA      1  CRA
 *   2  CRB bit 2 ? port B data : DDRB      3  CRB
 *
 * Modelled: data direction (a port read is output-register bits where DDR
 * is 1 and input pins where it is 0); CR bits 6-7 as read-only interrupt
 * flags, set by a CA1/CB1 strobe and cleared by reading that port's data;
 * IRQA/IRQB from bit 7 with its enable (CR bit 0).
 *
 * Simplified: strobe_ca1/cb1() is "the active edge happened" (CR bit 1's
 * edge select is not checked), CA2/CB2 are not modelled, so bit 6 is never
 * set, and a CPU write to a data register calls the host back directly
 * instead of pulsing CB2. That is all the Apple-1 needs (Woz Monitor's
 * keyboard on A with CA1 strobe, display on B with PB7 as busy); the
 * 6522 and 6551 for the other machines are Phase 3b.
 */

#include <cstdint>
#include <functional>

#include "device.h"

namespace ooe::emu
{
    class Pia6821 : public Device
    {
    public:
        static constexpr uint8_t CR_IRQ1 = 0x80;   ///< bit 7: C1 strobe seen
        static constexpr uint8_t CR_IRQ2 = 0x40;   ///< bit 6: C2 (not modelled)
        static constexpr uint8_t CR_SELECT_OR = 0x04; ///< bit 2: data register, not DDR
        static constexpr uint8_t CR_IRQ1_ENABLE = 0x01;

        /// Called on every CPU write to a port's data register, with the
        /// port's output bits (output register & DDR).
        using WriteHandler = std::function<void(uint8_t)>;

        size_t size() const override { return 4; }
        uint8_t read(uint32_t offset) override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override;

        // --- the peripheral side -------------------------------------------
        void set_input_a(uint8_t pins) { in_a_ = pins; }
        void set_input_b(uint8_t pins) { in_b_ = pins; }
        void strobe_ca1() { cra_ = static_cast<uint8_t>(cra_ | CR_IRQ1); }
        void strobe_cb1() { crb_ = static_cast<uint8_t>(crb_ | CR_IRQ1); }
        void on_write_a(WriteHandler h) { write_a_ = std::move(h); }
        void on_write_b(WriteHandler h) { write_b_ = std::move(h); }
        uint8_t cra() const { return cra_; }
        uint8_t crb() const { return crb_; }

    private:
        struct Port
        {
            uint8_t &out;
            uint8_t &ddr;
            uint8_t &cr;
            uint8_t &in;
            WriteHandler &on_write;
        };
        Port port(bool b);

        uint8_t or_a_{0}, ddr_a_{0}, cra_{0}, in_a_{0};
        uint8_t or_b_{0}, ddr_b_{0}, crb_{0}, in_b_{0};
        WriteHandler write_a_, write_b_;
    };
} // namespace ooe::emu
