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
            status_ = static_cast<uint8_t>(status_ & ~(STATUS_RDRF | STATUS_OVRN)); // the errors clear with data's read
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

    uint32_t Acia6551::period() const
    {
        // CONTROL bits 3-0: 115,200 (1.8432 MHz / 16) for 0, then 50 ... 19,200, in hundredths of a baud.
        static constexpr uint32_t centibaud[16] = {11'520'000, 5'000, 7'500, 10'992, 13'458, 15'000, 30'000, 60'000,
                                                   120'000, 180'000, 240'000, 360'000, 480'000, 720'000, 960'000,
                                                   1'920'000};
        const unsigned data = 8 - ((control_ >> 5) & 3);
        const unsigned stop = (control_ & 0x80) ? 2 : 1; // (1.5 for 5 bits: counted as 2)
        const uint64_t bits = 1 + data + stop;
        const uint64_t b = centibaud[control_ & 0x0F];
        return static_cast<uint32_t>((uint64_t{CPU_HZ} * 100 * bits + b / 2) / b);
    }

    void Acia6551::send_paced(const std::string &bytes, uint32_t cycles_per_byte)
    {
        if (paced_.empty())
        {
            paced_period_ = cycles_per_byte;
            paced_due_ = cycles_per_byte ? cycles_per_byte : period();
        }
        for (const char c : bytes)
            paced_.push_back(static_cast<uint8_t>(c));
    }

    void Acia6551::tick(uint64_t cycles)
    {
        while (!paced_.empty())
        {
            if (cycles < paced_due_)
            {
                paced_due_ -= cycles;
                return;
            }
            cycles -= paced_due_;
            const uint8_t byte = paced_.front();
            paced_.pop_front();
            paced_due_ = paced_period_ ? paced_period_ : period();
            if (!(command_ & CMD_DTR))
                ++dropped_;
            else if (status_ & STATUS_RDRF)
            {
                status_ = static_cast<uint8_t>(status_ | STATUS_OVRN); // the held byte kept; no interrupt
                ++overruns_;
            }
            else
            {
                rx_ = byte;
                status_ = static_cast<uint8_t>(status_ | STATUS_RDRF);
                if (!(command_ & CMD_RX_IRQ_OFF))
                    status_ = static_cast<uint8_t>(status_ | STATUS_IRQ);
            }
        }
    }
} // namespace ooe::emu
