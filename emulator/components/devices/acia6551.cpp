/**
 * @file acia6551.cpp
 * @brief W65C51N ACIA implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "acia6551.h"

namespace ooe::emu
{
    uint8_t Acia6551::peek(uint32_t offset) const
    {
        switch (offset & 3)
        {
        case 0: return rx_;
        case 1: return static_cast<uint8_t>(status_ | STATUS_TDRE); // the W65C51N's stuck TDRE
        case 2: return command_;
        default: return control_;
        }
    }

    uint8_t Acia6551::read(uint32_t offset)
    {
        const uint8_t v = peek(offset);
        if ((offset & 3) == 0)
            status_ = static_cast<uint8_t>(status_ & ~STATUS_RDRF);
        else if ((offset & 3) == 1)
            status_ = static_cast<uint8_t>(status_ & ~STATUS_IRQ);
        return v;
    }

    void Acia6551::write(uint32_t offset, uint8_t data)
    {
        switch (offset & 3)
        {
        case 0:
            if ((command_ & CMD_DTR) && (command_ & CMD_TX_ON) && transmit_)
                transmit_(data);
            break;
        case 1: // programmed reset: command bits 0-4 cleared; control kept
            command_ = static_cast<uint8_t>(command_ & 0xE0);
            status_ = static_cast<uint8_t>(status_ & ~STATUS_IRQ);
            break;
        case 2:
            command_ = data;
            break;
        default:
            control_ = data;
            break;
        }
    }

    bool Acia6551::receive(uint8_t byte)
    {
        if (!(command_ & CMD_DTR) || (status_ & STATUS_RDRF))
            return false;
        rx_ = byte;
        status_ = static_cast<uint8_t>(status_ | STATUS_RDRF);
        if (!(command_ & CMD_RX_IRQ_OFF))
            status_ = static_cast<uint8_t>(status_ | STATUS_IRQ);
        return true;
    }
} // namespace ooe::emu
