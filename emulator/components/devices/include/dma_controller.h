#pragma once

/**
 * @file dma_controller.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's DMA controller (module ID 8), in the style of the 8237.
 * @version 0.1.0
 * @date 2026-10-22
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("DMA", "DMA registers") is the spec. Registers at $C400:
 *
 *   channel n at n x $10:  $0-$2 ADDR  $4-$5 COUNT  $6 MODE  $7 CTRL  $8-$A DEST
 *   $40 STATUS  $41 CONTROL  $42 ABORT  $7F ID
 *
 * MODE: bits 1-0 single/demand/block, bits 3-2 card->memory/memory->card/
 * memory->memory, bit 4 DEC, 5 HOLD, 6 AUTO, 7 IRQ. CTRL: bit 0 ENABLE,
 * bit 1 REQ. CONTROL: bit 0 master enable, bits 4-6 BURST (2^BURST bytes
 * before the CPU gets a cycle; 7 = no limit). It resets to $70: no limit,
 * and DMA off until software enables it.
 *
 * It is a BusMaster: before each CPU cycle the decoder offers it the bus.
 * It takes it (one cycle, RDY then BE) when the highest-priority armed
 * channel has a request, moves bytes, and hands the bus back:
 *
 * - single: one byte, then the CPU gets its cycle;
 * - demand: while the card holds DRQ, up to the BURST limit;
 * - block: the whole count once DRQ or REQ starts it, a BURST at a time;
 * - memory to memory: a block started by REQ, two cycles a byte (a read
 *   into the latch, then the write).
 *
 * Card transfers are fly-by: one cycle a byte, the card answering /DACK.
 * Writing ADDR, COUNT or DEST sets both the start value (for AUTO) and the
 * current one; reads return the current one. At terminal count the card
 * sees TC with its last byte, STATUS records it, MODE.IRQ raises the
 * interrupt (IRQH bit 0), REQ clears, and ENABLE clears unless AUTO
 * reloads the start values. Counts and addresses wrap within 20 bits.
 */

#include <array>
#include <cstdint>
#include <vector>

#include "decoder.h"
#include "device.h"

namespace ooe::emu
{
    class DmaController : public Device, public BusMaster
    {
    public:
        static constexpr unsigned CHANNELS = 4;
        static constexpr uint8_t MODULE = 0x8;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision

        enum Reg : uint8_t
        {
            ADDR = 0x0,
            COUNT = 0x4,
            MODE = 0x6,
            CTRL = 0x7,
            DEST = 0x8,
            STATUS = 0x40,
            CONTROL = 0x41,
            ABORT = 0x42,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t SINGLE = 0x0, DEMAND = 0x1, BLOCK = 0x2;
        static constexpr uint8_t TO_MEMORY = 0x0 << 2, TO_CARD = 0x1 << 2, MEM_TO_MEM = 0x2 << 2;
        static constexpr uint8_t DEC = 0x10, HOLD = 0x20, AUTO = 0x40, IRQ = 0x80;
        static constexpr uint8_t ENABLE = 0x01, REQ = 0x02;
        static constexpr uint8_t MASTER_ENABLE = 0x01;
        static constexpr uint8_t CONTROL_RESET = 0x70;

        DmaController() { reset(); }

        /// The decoder whose cards it serves. Not owned.
        void attach(Decoder *decoder) { decoder_ = decoder; }

        // Device: the registers at $C400
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override;

        // BusMaster
        unsigned take_bus(Decoder &bus) override;
        void reset() override;

    private:
        struct Channel
        {
            uint32_t addr{0}, base_addr{0};
            uint32_t dest{0}, base_dest{0};
            uint32_t count{0}, base_count{0}; ///< bytes left; 0 in the register = 65,536
            uint8_t mode{0};
            uint8_t ctrl{0};
            bool running{false};              ///< a block or memory-to-memory transfer under way
        };

        /// The card on channel `n`'s DRQ line, or nullptr.
        Device *card(unsigned n) const;
        bool requesting(unsigned n) const;
        /// Move bytes on channel `n`; @return cycles used.
        unsigned transfer(Decoder &bus, unsigned n);
        static uint32_t step(uint32_t addr, uint8_t mode);
        static uint8_t byte_of(uint32_t v, unsigned i) { return static_cast<uint8_t>(v >> (8 * i)); }
        static void set_byte(uint32_t &v, unsigned i, uint8_t b);

        Decoder *decoder_{nullptr};
        std::array<Channel, CHANNELS> ch_{};
        uint8_t tc_{0};      ///< STATUS bits 0-3
        uint8_t control_{CONTROL_RESET};
    };
} // namespace ooe::emu
