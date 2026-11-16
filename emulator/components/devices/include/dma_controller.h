#pragma once

/**
 * @file dma_controller.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's DMA controller (module ID 8), revision 2.
 * @version 0.3.0
 * @date 2026-11-15
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("The DMA controller, revision 2") is the spec; revision 1's
 * layout is gone (step B2). Registers at $C400:
 *
 *   the window, on the channel CONTROL's SELECT names:
 *     $00-$02 ADDR  $04-$05 COUNT  $06 MODE  $07 CTRL  $08-$0A DEST  $0C CARD
 *     ($03, $0B, $0D-$0F read $FF)
 *   $10 STATUS  $11 ABORT  $12 REQ  $7E CONTROL  $7F ID ($82)
 *   (everything else reads $FF and ignores writes)
 *
 * MODE: bits 1-0 single/demand/block, bits 3-2 card->memory/memory->card/
 * memory->memory, bit 4 DEC, 5 HOLD, 6 AUTO, 7 IRQ. CTRL: bit 0 ENABLE,
 * bit 1 REQ. CARD: bits 3-0 MODULE, the module ID the channel serves ($8,
 * the controller's own, is none), bit 4 WIDE (stored and read back; 16-bit
 * transfers are step B4's, so it does nothing yet). CONTROL: bit 0 master
 * enable, bits 1-3 SELECT, bits 4-6 BURST (2^BURST bytes before the CPU
 * gets a cycle; 7 = no limit), bit 7 reads 0. It resets to $70: no limit,
 * DMA off, SELECT 0. STATUS bit n: channel n reached terminal count (write
 * 1 to clear). ABORT bit n stops channel n (it reads $FF). REQ bit n:
 * channel n's MODULE was requesting at the last poll (read only).
 *
 * All eight channels are live (since step B3; B2 had four), each behind
 * its SELECT, with its bit in STATUS, ABORT and REQ. Each channel's CARD
 * resets to today's wiring: $07 (mass storage), $09 (video), $0A (sound),
 * then $08 (none).
 *
 * It is a BusMaster: before each CPU cycle the decoder offers it the bus.
 * A take, when there is something to do:
 *
 * 1. With the master enable off, nothing (Apple II mode relies on it).
 * 2. /DREQ, the shared open-drain line, is the OR of the cards wired to it
 *    (Decoder::dreq()); it is sampled only while some armed channel serves
 *    a card.
 * 3. With /DREQ low, an armed channel serving a card and no hold-off
 *    running, the controller polls: the takeover cycle (RDY, then BE), then
 *    one poll cycle in which each requesting card pulls its own data line;
 *    REQ latches the result by channel.
 * 4. It grants the highest-priority channel among the card channels whose
 *    MODULE requested at the poll and the channels software started (REQ
 *    set, or a block or memory-to-memory transfer under way). Without a
 *    poll only the software-started ones are served, after the takeover.
 * 5. A poll that grants nothing gives the bus back after its 2 cycles, and
 *    no poll runs for HOLD_OFF cycles from the take's first, unless /DREQ is
 *    seen high and then low again, or a CTRL is written; software-started
 *    channels are served meanwhile, without a poll.
 *
 * Then, by mode:
 *
 * - single: one byte, then the CPU gets its cycle;
 * - demand: while the granted card holds its request, up to the BURST
 *   limit (the release rule: only the card named on PS0-PS3 counts in a
 *   grant, so a second card holding /DREQ doesn't keep it going);
 * - block: the whole count once a request or REQ starts it, a BURST at a
 *   time;
 * - memory to memory: a block started by REQ, two cycles a byte (a read
 *   into the latch, then the write).
 *
 * Card transfers are fly-by: one cycle a byte, the card named by the
 * channel's MODULE on PS0-PS3 answering. A card channel whose MODULE has no
 * card wired to /DREQ moves a floating bus ($FF) to memory, or reads memory
 * for nothing. Writing ADDR, COUNT or DEST sets both the start value (for
 * AUTO) and the current one; reads return the current one. At terminal
 * count the card sees TC with its last byte, STATUS records it, MODE.IRQ
 * raises the interrupt (IRQH bit 0), REQ clears, and ENABLE clears unless
 * AUTO reloads the start values. Addresses wrap within 24 bits, A0-A23.
 */

#include <array>
#include <cstdint>

#include "decoder.h"
#include "device.h"

namespace ooe::emu
{
    class DmaController : public Device, public BusMaster
    {
    public:
        static constexpr unsigned CHANNELS = 8; ///< the channels SELECT can name, all live (step B3)
        static constexpr uint8_t MODULE = 0x8;
        static constexpr uint8_t ID = MODULE << 4 | 2; ///< +$7F: type (module ID) and revision
        static constexpr unsigned HOLD_OFF = 64; ///< cycles with no poll after a poll that granted nothing

        enum Reg : uint8_t
        {
            // the window, on channel SELECT
            ADDR = 0x0,
            COUNT = 0x4,
            MODE = 0x6,
            CTRL = 0x7,
            DEST = 0x8,
            CARD = 0xC,
            // shared
            STATUS = 0x10,
            ABORT = 0x11,
            REQ_REG = 0x12,
            CONTROL = 0x7E,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t SINGLE = 0x0, DEMAND = 0x1, BLOCK = 0x2;
        static constexpr uint8_t TO_MEMORY = 0x0 << 2, TO_CARD = 0x1 << 2, MEM_TO_MEM = 0x2 << 2;
        static constexpr uint8_t DEC = 0x10, HOLD = 0x20, AUTO = 0x40, IRQ = 0x80;
        static constexpr uint8_t ENABLE = 0x01, REQ = 0x02;
        static constexpr uint8_t MASTER_ENABLE = 0x01;
        static constexpr uint8_t SELECT_SHIFT = 1, SELECT_MASK = 0x0E;
        static constexpr uint8_t CONTROL_RESET = 0x70;
        static constexpr uint8_t CARD_MODULE = 0x0F, WIDE = 0x10, CARD_BITS = 0x1F;
        static constexpr uint8_t CARD_NONE = MODULE; ///< CARD's MODULE for "serves no card"
        /// CARD after /RESET, by channel: mass storage, video, sound, then none.
        static constexpr std::array<uint8_t, CHANNELS> CARD_RESET{0x07, 0x09, 0x0A, 0x08, 0x08, 0x08, 0x08, 0x08};

        DmaController() { reset(); }

        /// Kept for the machine's wiring: the controller reaches the bus, and the cards on /DREQ
        /// (Decoder::dreq()), through the decoder take_bus() is given.
        void attach(Decoder *decoder) { (void)decoder; }

        // Device: the registers at $C400
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override;

        // BusMaster
        unsigned take_bus(Decoder &bus) override;
        /// /RESET (Decoder::reset() and pulse_reset(), Apple II mode's too): every channel, CARD at
        /// its reset value, STATUS, REQ, CONTROL $70 and the hold-off.
        void reset() override;

        /// Channel `n`'s register `reg` (the window's offsets: ADDR $0-$2, COUNT $4-$5, MODE $6,
        /// CTRL $7, DEST $8-$A, CARD $C), read with no effect on the controller, whatever SELECT
        /// is: for tests and tools, which must never reach a channel by changing what the running
        /// software has set up. $FF for `n` of CHANNELS or more (no such channel) or a reserved
        /// register, as the window reads.
        uint8_t peek_channel(unsigned n, uint8_t reg) const;

    private:
        struct Channel
        {
            uint32_t addr{0}, base_addr{0};
            uint32_t dest{0}, base_dest{0};
            uint32_t count{0}, base_count{0}; ///< bytes left; 0 in the register = 65,536
            uint8_t mode{0};
            uint8_t ctrl{0};
            uint8_t card{CARD_NONE};          ///< CARD: MODULE and WIDE
            bool running{false};              ///< a block or memory-to-memory transfer under way
        };

        unsigned selected() const { return static_cast<unsigned>((control_ & SELECT_MASK) >> SELECT_SHIFT); }
        /// Register `reg` ($0-$F) of channel `c`'s window.
        static uint8_t window(const Channel &c, unsigned reg);
        /// Move bytes on channel `n`; @return cycles used.
        unsigned transfer(Decoder &bus, unsigned n);
        static uint32_t step(uint32_t addr, uint8_t mode);
        static uint8_t byte_of(uint32_t v, unsigned i) { return static_cast<uint8_t>(v >> (8 * i)); }
        static void set_byte(uint32_t &v, unsigned i, uint8_t b);

        std::array<Channel, CHANNELS> ch_{};
        uint8_t tc_{0};      ///< STATUS: terminal count, by channel
        uint8_t req_{0};     ///< REQ: the last poll, by channel
        uint8_t control_{CONTROL_RESET};
        bool holdoff_{false};          ///< a poll granted nothing: no poll until it ends
        uint64_t holdoff_end_{0};      ///< the bus cycle it ends at
        bool dreq_went_high_{false};   ///< /DREQ seen high since it began
    };
} // namespace ooe::emu
