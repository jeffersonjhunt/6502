/**
 * @file dma_controller.cpp
 * @brief The backplane's DMA controller.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "dma_controller.h"

#include <limits>

namespace ooe::emu
{
    namespace
    {
        constexpr uint32_t ADDR_MASK = PHYSICAL_SPACE - 1;
        constexpr uint8_t XFER = 0x03;
        constexpr uint8_t DIR = 0x0C;
        constexpr uint32_t FULL_COUNT = 0x10000; ///< COUNT = 0
    } // namespace

    void DmaController::reset()
    {
        ch_ = {};
        tc_ = 0;
        control_ = CONTROL_RESET;
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

    Device *DmaController::card(unsigned n) const
    {
        if (!decoder_)
            return nullptr;
        for (const Mapping &m : decoder_->mappings())
        {
            if (m.drq == n)
                return m.device;
        }
        return nullptr;
    }

    bool DmaController::requesting(unsigned n) const
    {
        const Channel &c = ch_[n];
        if (!(c.ctrl & ENABLE))
            return false;
        if ((c.mode & DIR) == MEM_TO_MEM)
            return c.running || (c.ctrl & REQ);
        if ((c.mode & DIR) != TO_MEMORY && (c.mode & DIR) != TO_CARD)
            return false; // reserved direction
        const Device *d = card(n);
        const bool drq = d && d->drq();
        switch (c.mode & XFER)
        {
        case BLOCK:
            return c.running || (c.ctrl & REQ) || drq;
        case SINGLE:
        case DEMAND:
            return drq;
        default:
            return false; // reserved mode
        }
    }

    unsigned DmaController::take_bus(Decoder &bus)
    {
        if (!(control_ & MASTER_ENABLE))
            return 0;
        for (unsigned n = 0; n < CHANNELS; ++n) // fixed priority: channel 0 first
        {
            if (requesting(n))
                return bus.dma_idle() + transfer(bus, n); // RDY, then BE: one cycle
        }
        return 0;
    }

    unsigned DmaController::transfer(Decoder &bus, unsigned n)
    {
        Channel &c = ch_[n];
        const uint8_t xfer = c.mode & XFER;
        const bool mem_to_mem = (c.mode & DIR) == MEM_TO_MEM;
        const unsigned burst = (control_ >> 4) & 7;
        const uint32_t limit = xfer == SINGLE && !mem_to_mem ? 1
                               : burst == 7                  ? std::numeric_limits<uint32_t>::max()
                                                             : 1u << burst;
        if (xfer == BLOCK || mem_to_mem)
            c.running = true;
        Device *d = mem_to_mem ? nullptr : card(n);

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
                if (d)
                    cycles += bus.dma_fly_by(c.addr, *d, to_memory, tc);
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
                tc_ = static_cast<uint8_t>(tc_ | 1u << n);
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
            if (xfer == DEMAND && !mem_to_mem && !(d && d->drq()))
                break; // the card let go of DRQ
        }
        return cycles;
    }

    uint8_t DmaController::peek(uint32_t offset) const
    {
        if (offset < CHANNELS * 0x10)
        {
            const Channel &c = ch_[offset >> 4];
            const unsigned r = offset & 0xF;
            switch (r)
            {
            case ADDR:
            case ADDR + 1:
            case ADDR + 2:
                return byte_of(c.addr, r - ADDR);
            case COUNT:
            case COUNT + 1:
                return byte_of(c.count & 0xFFFF, r - COUNT);
            case MODE:
                return c.mode;
            case CTRL:
                return c.ctrl;
            case DEST:
            case DEST + 1:
            case DEST + 2:
                return byte_of(c.dest, r - DEST);
            default:
                return 0xFF;
            }
        }
        switch (offset)
        {
        case STATUS:
        {
            uint8_t drq = 0;
            for (unsigned n = 0; n < CHANNELS; ++n)
            {
                const Device *d = card(n);
                if (d && d->drq())
                    drq = static_cast<uint8_t>(drq | 1u << n);
            }
            return static_cast<uint8_t>(drq << 4 | tc_);
        }
        case CONTROL:
            return control_;
        case ID_REG:
            return ID;
        default:
            return 0xFF; // ABORT is write-only; the rest is unused
        }
    }

    void DmaController::write(uint32_t offset, uint8_t data)
    {
        if (offset < CHANNELS * 0x10)
        {
            Channel &c = ch_[offset >> 4];
            const unsigned r = offset & 0xF;
            switch (r)
            {
            case ADDR:
            case ADDR + 1:
            case ADDR + 2:
                set_byte(c.base_addr, r - ADDR, data);
                c.base_addr &= ADDR_MASK;
                set_byte(c.addr, r - ADDR, data);
                c.addr &= ADDR_MASK;
                break;
            case COUNT:
            case COUNT + 1:
            {
                uint32_t raw = c.base_count & 0xFFFF;
                set_byte(raw, r - COUNT, data);
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
                break;
            case DEST:
            case DEST + 1:
            case DEST + 2:
                set_byte(c.base_dest, r - DEST, data);
                c.base_dest &= ADDR_MASK;
                set_byte(c.dest, r - DEST, data);
                c.dest &= ADDR_MASK;
                break;
            default:
                break;
            }
            return;
        }
        switch (offset)
        {
        case STATUS:
            tc_ = static_cast<uint8_t>(tc_ & ~(data & 0x0F));
            break;
        case CONTROL:
            control_ = data;
            break;
        case ABORT:
            for (unsigned n = 0; n < CHANNELS; ++n)
            {
                if (data & (1u << n))
                {
                    ch_[n].ctrl = static_cast<uint8_t>(ch_[n].ctrl & ~(ENABLE | REQ));
                    ch_[n].running = false;
                }
            }
            break;
        default:
            break;
        }
    }

    bool DmaController::irq() const
    {
        for (unsigned n = 0; n < CHANNELS; ++n)
        {
            if ((tc_ >> n & 1) && (ch_[n].mode & IRQ))
                return true;
        }
        return false;
    }
} // namespace ooe::emu
