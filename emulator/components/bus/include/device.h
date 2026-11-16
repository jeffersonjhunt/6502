#pragma once

/**
 * @file device.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A chip on the bus: addressed by offset within its mapping.
 * @version 0.8.0
 * @date 2026-11-16
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A device sees what a chip sees: its own low address lines, not the
 * system address. The decoder (decoder.h) does the chip select and passes
 * `offset = physical address - mapping base`, so the same Pia6821 works at
 * $D010 on an Apple-1 and wherever the backplane's I/O module puts it.
 *
 * Time: a device that answers timed() gets tick() calls carrying CPU
 * cycles, in lockstep with the CPU. The decoder catches it up just before
 * each access to it, so a 6522 counter read mid-instruction is exact, and
 * before each IRQ check, so a timer interrupt is seen after the very
 * instruction it fired in. Untimed devices (RAM, ROM) are never ticked,
 * which keeps that free for them.
 *
 * DMA: a card whose mapping is wired (Mapping::drq) raises drq() on the
 * shared /DREQ, and the DMA controller polls the cards and grants the
 * channel whose CARD names the card's module, which it puts on PS0-PS3.
 * The grant cycles are fly-by: the card drives or takes the data while the
 * controller addresses memory, so dack_read() and dack_write() have no
 * offset. `tc` marks the transfer's last byte or word (the TC line).
 *
 * Word grants (step B4, CARD's WIDE): a grant may move a word on D0-D15,
 * the even byte on D0-D7. A card whose data register takes words says so
 * by overriding dack_read_word() and dack_write_word(); the defaults are a
 * card with D0-D7 only, as the bus sees it: its register moves once a
 * grant, D8-D15 float high to memory and the odd byte from memory is lost.
 * That is why WIDE must not be set on such a card's channel.
 */

#include <cstddef>
#include <cstdint>

namespace ooe::emu
{
    class Device
    {
    public:
        virtual ~Device() = default;

        /// Bytes of address space the device decodes (its mapping's natural size).
        virtual size_t size() const = 0;

        /// CPU read. May have side effects (a 6821 clears its flags).
        virtual uint8_t read(uint32_t offset) = 0;
        /// read() without the side effects, for the debugger.
        virtual uint8_t peek(uint32_t offset) const = 0;
        /// CPU write. A ROM ignores it.
        virtual void write(uint32_t offset, uint8_t data) = 0;
        /// Loader-side write, past any read-only rule.
        virtual void poke(uint32_t offset, uint8_t data) { write(offset, data); }

        /// The backplane's /RESET (power-up, START, a pulse). Most cards keep
        /// running through it, their own controllers unaffected; one that
        /// stops for it says so here.
        virtual void bus_reset() {}

        /// Interrupt output, active high. Only reaches the CPU if the mapping wires it.
        virtual bool irq() const { return false; }

        /// True if the device keeps time (tick() does something).
        virtual bool timed() const { return false; }
        /// `cycles` CPU cycles have passed since the last tick. Only called
        /// on timed devices.
        virtual void tick(uint64_t cycles) { (void)cycles; }

        /// The card wants a DMA transfer: with its mapping wired (Mapping::drq) it pulls the
        /// backplane's /DREQ, pulls its module's data line in the DMA controller's poll, and in a
        /// grant naming it holds /DREQ while it wants another transfer after this one.
        virtual bool drq() const { return false; }
        /// A grant naming the card on PS0-PS3 (/DMA low), card to memory: the byte the card puts on
        /// the bus.
        virtual uint8_t dack_read(bool tc)
        {
            (void)tc;
            return 0xFF;
        }
        /// A grant naming the card, memory to card: the byte on the bus.
        virtual void dack_write(uint8_t data, bool tc)
        {
            (void)data;
            (void)tc;
        }
        /// A word grant, card to memory: D0-D7 the even byte, D8-D15 the odd. Default: a card with
        /// D0-D7 only, its byte once and D8-D15 pulled up.
        virtual uint16_t dack_read_word(bool tc) { return static_cast<uint16_t>(0xFF00u | dack_read(tc)); }
        /// A word grant, memory to card. Default: a card with D0-D7 only takes the even byte, once.
        virtual void dack_write_word(uint16_t data, bool tc) { dack_write(static_cast<uint8_t>(data), tc); }
    };
} // namespace ooe::emu
