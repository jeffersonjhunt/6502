/**
 * @file dma_controller.cpp
 * @brief The backplane's DMA controller, revision 2.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "dma_controller.h"

#include <limits>

namespace ooe::emu
{
    namespace
    {
        constexpr uint32_t ADDR_MASK = PHYSICAL_SPACE - 1; // A0-A23
        constexpr uint8_t XFER = 0x03;
        constexpr uint8_t XFER_RESERVED = 0x03;
        constexpr uint8_t DIR = 0x0C;
        constexpr uint32_t FULL_COUNT = 0x10000; ///< COUNT = 0
        constexpr uint8_t WINDOW = 0x10;         ///< the window's size, $00-$0F

        constexpr uint8_t bit(unsigned n) { return static_cast<uint8_t>(1u << n); }
    } // namespace

    void DmaController::reset()
    {
        ch_ = {};
        for (unsigned n = 0; n < CHANNELS; ++n)
            ch_[n].card = CARD_RESET[n];
        tc_ = 0;
        req_ = 0;
        control_ = CONTROL_RESET;
        holdoff_ = false;
        holdoff_end_ = 0;
        dreq_went_high_ = false;
    }

    void DmaController::set_byte(uint32_t &v, unsigned i, uint8_t b)
    {
        v = (v & ~(0xFFu << (8 * i))) | static_cast<uint32_t>(b) << (8 * i);
    }

    uint32_t DmaController::step(uint32_t addr, uint8_t mode)
    {
        if (mode & HOLD)
            return addr;
        return (mode & DEC ? addr - 1 : addr + 1) & ADDR_MASK;
    }

    unsigned DmaController::take_bus(Decoder &bus)
    {
        if (!(control_ & MASTER_ENABLE))
            return 0; // nothing sampled, nothing taken: Apple II mode relies on it

        // Which armed channels software started, and which wait on a card's request.
        uint8_t started = 0, waiting = 0;
        for (unsigned n = 0; n < CHANNELS; ++n)
        {
            const Channel &c = ch_[n];
            if (!(c.ctrl & ENABLE))
                continue;
            const uint8_t dir = c.mode & DIR;
            if (dir == MEM_TO_MEM)
            {
                if (c.running || (c.ctrl & REQ))
                    started = static_cast<uint8_t>(started | bit(n));
                continue;
            }
            if (dir != TO_MEMORY && dir != TO_CARD)
                continue; // a reserved direction
            const uint8_t xfer = c.mode & XFER;
            if (xfer == XFER_RESERVED)
                continue;
            if (xfer == BLOCK && (c.running || (c.ctrl & REQ)))
                started = static_cast<uint8_t>(started | bit(n));
            else if ((c.card & CARD_MODULE) != CARD_NONE)
                waiting = static_cast<uint8_t>(waiting | bit(n)); // serves a card: only its request starts it
        }
        if (!started && !waiting)
            return 0;

        // /DREQ matters only to a channel serving a card; the hold-off, to the poll it holds off.
        bool poll = false;
        if (waiting)
        {
            const bool low = bus.dreq();
            if (holdoff_)
            {
                if (bus.now() >= holdoff_end_ || (low && dreq_went_high_))
                    holdoff_ = false; // its time is up, or /DREQ went high and low again: a new request
                else if (!low)
                    dreq_went_high_ = true;
            }
            poll = low && !holdoff_;
        }
        if (!poll && !started)
            return 0; // card channels only, and no poll to grant one: the bus isn't taken

        const uint64_t first = bus.now();
        unsigned cycles = bus.dma_idle(); // RDY, then BE: one cycle
        uint8_t grant = started;
        if (poll)
        {
            cycles += bus.dma_poll(); // each requesting card pulls its own data line
            req_ = 0;
            for (unsigned n = 0; n < CHANNELS; ++n)
            {
                const uint8_t module = ch_[n].card & CARD_MODULE;
                if (module != CARD_NONE && bus.dreq_from(module))
                    req_ = static_cast<uint8_t>(req_ | bit(n));
            }
            grant = static_cast<uint8_t>(grant | (req_ & waiting));
            if (!grant)
            {
                // A request no armed channel serves: give the bus back, and poll again only later.
                holdoff_ = true;
                holdoff_end_ = first + HOLD_OFF;
                dreq_went_high_ = false;
                return cycles;
            }
        }
        unsigned n = 0; // fixed priority: channel 0 first
        while (!(grant & bit(n)))
            ++n;
        return cycles + transfer(bus, n);
    }

    unsigned DmaController::transfer(Decoder &bus, unsigned n)
    {
        Channel &c = ch_[n];
        const uint8_t xfer = c.mode & XFER;
        const bool mem_to_mem = (c.mode & DIR) == MEM_TO_MEM;
        const unsigned burst = static_cast<unsigned>((control_ >> 4) & 7);
        const uint32_t limit = xfer == SINGLE && !mem_to_mem ? 1
                               : burst == 7                  ? std::numeric_limits<uint32_t>::max()
                                                             : 1u << burst;
        if (xfer == BLOCK || mem_to_mem)
            c.running = true;
        // The card the channel's MODULE names, on PS0-PS3 for the grant; none: a floating bus.
        const uint8_t module = c.card & CARD_MODULE;
        const Mapping *card = mem_to_mem || module == CARD_NONE ? nullptr : bus.dreq_card(module);

        unsigned cycles = 0;
        for (uint32_t moved = 0; moved < limit; ++moved)
        {
            if (!c.count) // never written since reset, or run out: COUNT 0 is 65,536
                c.count = FULL_COUNT;
            const bool tc = c.count == 1;
            if (mem_to_mem)
            {
                uint8_t v = 0;
                cycles += bus.dma_read(c.addr, v);
                cycles += bus.dma_write(c.dest, v);
                c.addr = step(c.addr, c.mode);
                c.dest = step(c.dest, static_cast<uint8_t>(c.mode & ~HOLD)); // HOLD is the source's
            }
            else
            {
                const bool to_memory = (c.mode & DIR) == TO_MEMORY;
                if (card)
                    cycles += bus.dma_fly_by(c.addr, *card, module, to_memory, tc);
                else if (to_memory)
                    cycles += bus.dma_write(c.addr, Decoder::UNMAPPED_READ); // no card: a floating bus
                else
                {
                    uint8_t ignored = 0;
                    cycles += bus.dma_read(c.addr, ignored);
                }
                c.addr = step(c.addr, c.mode);
            }
            --c.count;
            if (tc)
            {
                tc_ = static_cast<uint8_t>(tc_ | bit(n));
                c.ctrl = static_cast<uint8_t>(c.ctrl & ~REQ);
                c.running = false;
                if (c.mode & AUTO)
                {
                    c.addr = c.base_addr;
                    c.dest = c.base_dest;
                    c.count = c.base_count;
                }
                else
                    c.ctrl = static_cast<uint8_t>(c.ctrl & ~ENABLE);
                break;
            }
            // The release rule: in a grant only the card named on PS0-PS3 may hold /DREQ, so this is
            // its request alone, never the line's OR.
            if (xfer == DEMAND && !mem_to_mem && !(card && card->device->drq()))
                break; // the card let go
        }
        return cycles;
    }

    uint8_t DmaController::window(const Channel &c, unsigned reg)
    {
        switch (reg)
        {
        case ADDR:
        case ADDR + 1:
        case ADDR + 2:
            return byte_of(c.addr, reg - ADDR);
        case COUNT:
        case COUNT + 1:
            return byte_of(c.count & 0xFFFF, reg - COUNT);
        case MODE:
            return c.mode;
        case CTRL:
            return c.ctrl;
        case DEST:
        case DEST + 1:
        case DEST + 2:
            return byte_of(c.dest, reg - DEST);
        case CARD:
            return c.card;
        default:
            return 0xFF; // reserved: $03, $0B, $0D-$0F
        }
    }

    uint8_t DmaController::peek(uint32_t offset) const
    {
        if (offset < WINDOW)
        {
            return window(ch_[selected()], offset);
        }
        switch (offset)
        {
        case STATUS:
            return tc_;
        case REQ_REG:
            return req_;
        case CONTROL:
            return control_;
        case ID_REG:
            return ID;
        default:
            return 0xFF; // ABORT is write-only; the rest is reserved
        }
    }

    void DmaController::write(uint32_t offset, uint8_t data)
    {
        if (offset < WINDOW)
        {
            Channel &c = ch_[selected()];
            switch (offset)
            {
            case ADDR:
            case ADDR + 1:
            case ADDR + 2:
                set_byte(c.base_addr, offset - ADDR, data);
                c.base_addr &= ADDR_MASK;
                set_byte(c.addr, offset - ADDR, data);
                c.addr &= ADDR_MASK;
                break;
            case COUNT:
            case COUNT + 1:
            {
                uint32_t raw = c.base_count & 0xFFFF;
                set_byte(raw, offset - COUNT, data);
                c.base_count = raw ? raw : FULL_COUNT;
                c.count = c.base_count;
                break;
            }
            case MODE:
                c.mode = data;
                break;
            case CTRL:
                c.ctrl = static_cast<uint8_t>(data & (ENABLE | REQ));
                if (!(c.ctrl & ENABLE))
                    c.running = false;
                holdoff_ = false; // a channel newly armed (or changed) is polled for at once
                break;
            case DEST:
            case DEST + 1:
            case DEST + 2:
                set_byte(c.base_dest, offset - DEST, data);
                c.base_dest &= ADDR_MASK;
                set_byte(c.dest, offset - DEST, data);
                c.dest &= ADDR_MASK;
                break;
            case CARD:
                c.card = static_cast<uint8_t>(data & CARD_BITS);
                break;
            default:
                break; // reserved
            }
            return;
        }
        switch (offset)
        {
        case STATUS:
            tc_ = static_cast<uint8_t>(tc_ & ~data);
            break;
        case CONTROL:
            control_ = static_cast<uint8_t>(data & 0x7F); // bit 7 is free: reads 0
            break;
        case ABORT:
            for (unsigned n = 0; n < CHANNELS; ++n)
            {
                if (data & bit(n))
                {
                    ch_[n].ctrl = static_cast<uint8_t>(ch_[n].ctrl & ~(ENABLE | REQ));
                    ch_[n].running = false;
                }
            }
            break;
        default:
            break; // REQ and the ID are read-only; the rest is reserved
        }
    }

    uint8_t DmaController::peek_channel(unsigned n, uint8_t reg) const
    {
        if (n >= CHANNELS || reg >= WINDOW)
            return 0xFF;
        return window(ch_[n], reg); // whatever SELECT is, and with no effect
    }

    bool DmaController::irq() const
    {
        for (unsigned n = 0; n < CHANNELS; ++n)
        {
            if ((tc_ & bit(n)) && (ch_[n].mode & IRQ))
                return true;
        }
        return false;
    }
} // namespace ooe::emu
