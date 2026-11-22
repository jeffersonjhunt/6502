/**
 * @file gw_timing.h
 * @brief GW-BASIC's timing cases' shared method (T17's, fixed by 7f's answer 33): a span's cycles less the
 *        interrupt handlers inside it and, where a case asks, the BDOS calls inside it.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Every end is found by its return address as well as S. S alone isn't enough: during a BDOS call OODOS runs on
 * its own stack page (W0 its own, S set to $FF at the call), so S passes through the caller's values there, and an
 * interrupt inside the call can bring it to the value an end test waits for (7f's answer 33: a tick inside KEYEV's
 * drain counted some 200 cycles of the handler as KEYEV's, and ended the span inside OODOS).
 * - An interrupt: from the vector's first instruction, less the 7 cycles of the CPU's entry (2 reads, 3 pushes, the
 *   vector's 2 reads: cpu6502.cpp's irq(), taken between steps, so already in the count when the vector is
 *   watched), to its RTI's return: the PC pushed at the entry, with S three up.
 * - A BDOS call: from $0203 to the return address on the stack there (+1), with S two up.
 * - A call's span (CallSpan): from its entry to the return address on the stack there (+1), with S two up.
 * The includer includes harness.h and oodos_rig.h first.
 */

#pragma once

#include <cstdint>

namespace gw_timing
{
    /// The cycles left out of a span: the interrupt handlers inside it, and its BDOS calls if `bdos`.
    struct LeftOut
    {
        oodos_rig::ccp::CcpRig &r;
        bool bdos;
        uint16_t irq;
        uint64_t out = 0;  ///< the cycles left out since `out` was last set to 0
        unsigned irqs = 0; ///< the interrupts left out
        unsigned calls = 0; ///< the BDOS calls left out
        bool in_irq = false, in_bdos = false;
        uint16_t iret = 0, bret = 0;
        uint8_t isp = 0, bsp = 0;
        uint64_t ifrom = 0, bfrom = 0;

        LeftOut(oodos_rig::ccp::CcpRig &rig, bool leave_out_bdos)
            : r(rig), bdos(leave_out_bdos),
              irq(static_cast<uint16_t>(rig.machine->bus().peek(0xFFFE) | rig.machine->bus().peek(0xFFFF) << 8))
        {
        }
        uint16_t word(uint16_t a) const
        {
            return static_cast<uint16_t>(r.machine->bus().peek(a) |
                                         r.machine->bus().peek(static_cast<uint16_t>(a + 1)) << 8);
        }
        /// At each watch, first: true while `pc` is inside an interrupt handler or a BDOS call left out (the
        /// caller then tests nothing else). Only one that starts while `counting` is left out.
        bool inside(uint16_t pc, bool counting)
        {
            const uint8_t s = r.cpu->regs().s;
            const uint64_t c = r.cpu->cycles();
            if (in_irq)
            {
                if (pc != iret || s != static_cast<uint8_t>(isp + 3))
                    return true;
                out += c - ifrom;
                in_irq = false;
            }
            if (in_bdos)
            {
                if (pc != bret || s != static_cast<uint8_t>(bsp + 2))
                    return true;
                out += c - bfrom;
                in_bdos = false;
            }
            if (!counting)
                return false;
            if (pc == irq)
            {
                in_irq = true;
                ifrom = c - 7;
                isp = s;
                iret = word(static_cast<uint16_t>(0x102 + s)); // (P at S + 1, then the PC)
                ++irqs;
                return true;
            }
            if (bdos && pc == 0x0203)
            {
                in_bdos = true;
                bfrom = c;
                bsp = s;
                bret = static_cast<uint16_t>(word(static_cast<uint16_t>(0x101 + s)) + 1);
                ++calls;
                return true;
            }
            return false;
        }
    };

    /// A call's span: opened at its entry (the return address read off the stack), closed at that address with S
    /// two up.
    struct CallSpan
    {
        bool open = false;
        uint16_t ret = 0;
        uint8_t sp = 0;
        uint64_t from = 0;
        void enter(oodos_rig::ccp::CcpRig &r)
        {
            open = true;
            sp = r.cpu->regs().s;
            from = r.cpu->cycles();
            ret = static_cast<uint16_t>((r.machine->bus().peek(static_cast<uint16_t>(0x101 + sp)) |
                                         r.machine->bus().peek(static_cast<uint16_t>(0x102 + sp)) << 8) + 1);
        }
        /// True (and closed) at its return.
        bool returned(oodos_rig::ccp::CcpRig &r, uint16_t pc)
        {
            if (!open || pc != ret || r.cpu->regs().s != static_cast<uint8_t>(sp + 2))
                return false;
            open = false;
            return true;
        }
    };
} // namespace gw_timing
