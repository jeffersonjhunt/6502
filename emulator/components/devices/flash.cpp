/**
 * @file flash.cpp
 * @brief The backplane's flash: two chips, each with its own commands, busy time and status.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "flash.h"

namespace ooe::emu
{
    Flash::Flash(size_t size) : Rom(size) { erase(); }

    uint64_t Flash::busy_cycles(Op op) const
    {
        uint64_t ns = 0;
        size_t which = 0;
        switch (op)
        {
        case Op::Program:
            ns = PROGRAM_NS;
            which = 0;
            break;
        case Op::SectorErase:
            ns = SECTOR_NS;
            which = 1;
            break;
        case Op::ChipErase:
            ns = CHIP_NS;
            which = 2;
            break;
        case Op::None:
            return 0;
        }
#ifdef EMU_FAULT_HOOKS
        if (const uint64_t c = busy_override_[which])
            return c;
#else
        (void)which;
#endif
        return (ns * hz_ + 999'999'999u) / 1'000'000'000u; // rounded up: never shorter than the part
    }

    bool Flash::busy(unsigned chip) const { return busy_now(chips_[chip & 1]); }

    uint8_t Flash::status(const Chip &c, uint32_t offset) const
    {
        return static_cast<uint8_t>(c.poll7 | (c.toggle ? 0x40 : 0x00) | (Rom::peek(offset) & 0x3F));
    }

    uint8_t Flash::peek(uint32_t offset) const
    {
        if (offset >= size())
            return 0xFF;
        const Chip &c = chips_[offset & 1];
        if (busy_now(c))
            return status(c, offset);
        if (c.id)
            return (offset >> 1) & 1 ? PART_ID : MAKER_ID;
        return Rom::peek(offset);
    }

    uint8_t Flash::read(uint32_t offset)
    {
        if (offset >= size())
            return 0xFF;
        Chip &c = chips_[offset & 1];
        if (c.op != Op::None && !busy_now(c))
            c.op = Op::None;
        if (c.op == Op::None)
            return peek(offset);
        const uint8_t s = status(c, offset);
        c.toggle = !c.toggle;
        return s;
    }

    void Flash::write(uint32_t offset, uint8_t data)
    {
        if (offset >= size())
            return;
        const unsigned chip = offset & 1;
        Chip &c = chips_[chip];
        if (busy_now(c))
            return; // a busy chip ignores the bus
        c.op = Op::None;
        command(chip, offset >> 1, data);
    }

    void Flash::start(Chip &c, Op op, uint8_t data)
    {
        c.op = op;
        c.until = clock_ ? clock_->now() + busy_cycles(op) : 0;
        c.poll7 = op == Op::Program ? static_cast<uint8_t>(~data & 0x80) : 0x00;
        c.toggle = false;
    }

    void Flash::command(unsigned chip, uint32_t address, uint8_t data)
    {
        Chip &c = chips_[chip];
        const uint32_t cmd = address & 0x7FFF; // the chip compares A0-A14 alone
        const auto offset_of = [chip](uint32_t a) { return (a << 1) | chip; };
        const Step was = c.step;
        c.step = Step::Read;
        switch (was)
        {
        case Step::Read:
            if (cmd == COMMAND_1 && data == 0xAA)
                c.step = Step::Unlock1;
            break;
        case Step::Unlock1:
            if (cmd == COMMAND_2 && data == 0x55)
                c.step = Step::Unlock2;
            break;
        case Step::Unlock2:
            if (cmd != COMMAND_1)
                break;
            if (data == 0xA0)
                c.step = Step::Data;
            else if (data == 0x80)
                c.step = Step::Erase0;
            else if (data == 0x90)
                c.id = true;
            else if (data == 0xF0)
                c.id = false;
            return;
        case Step::Data:
        {
            const uint32_t o = offset_of(address);
#ifdef EMU_FAULT_HOOKS
            if (!(fault_on_ && o == fault_offset_))
#endif
                Rom::poke(o, static_cast<uint8_t>(Rom::peek(o) & data));
            start(c, Op::Program, data);
            return;
        }
        case Step::Erase0:
            if (cmd == COMMAND_1 && data == 0xAA)
                c.step = Step::Erase1;
            break;
        case Step::Erase1:
            if (cmd == COMMAND_2 && data == 0x55)
                c.step = Step::Erase2;
            break;
        case Step::Erase2:
            if (data == 0x30)
            {
                const uint32_t first = address & ~(CHIP_SECTOR - 1);
                for (uint32_t a = first; a < first + CHIP_SECTOR && a < size() / 2; ++a)
                    Rom::poke(offset_of(a), 0xFF);
                start(c, Op::SectorErase, data);
                return;
            }
            if (cmd == COMMAND_1 && data == 0x10)
            {
                for (uint32_t a = 0; a < size() / 2; ++a)
                    Rom::poke(offset_of(a), 0xFF);
                start(c, Op::ChipErase, data);
                return;
            }
            break;
        }
        if (c.step == Step::Read && data == 0xF0)
            c.id = false; // $F0 alone, anywhere, ends ID mode
    }
} // namespace ooe::emu
