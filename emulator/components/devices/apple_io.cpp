/**
 * @file apple_io.cpp
 * @brief Apple II mode's $C000-$CFFF.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "apple_io.h"

#include "decoder.h"
#include "disk2_card.h"
#include "usb_card.h"

namespace ooe::emu
{
    void AppleIo::reset()
    {
        s_ = AppleSwitches{};
        if (disk_)
            disk_->reset();
    }

    bool AppleIo::vbl() const
    {
        const uint64_t now = decoder_ ? decoder_->now() : 0;
        return now % FRAME_CYCLES >= uint64_t{SHOWN_LINES} * LINE_CYCLES;
    }

    uint8_t AppleIo::status(unsigned n) const
    {
        bool on = false;
        switch (n)
        {
        case 0x1: on = s_.lc_bank2; break;
        case 0x2: on = s_.lc_read; break;
        case 0x3: on = s_.ramrd; break;
        case 0x4: on = s_.ramwrt; break;
        case 0x5: on = s_.intcxrom; break;
        case 0x6: on = s_.altzp; break;
        case 0x7: on = s_.slotc3rom; break;
        case 0x8: on = s_.store80; break;
        case 0x9: on = !vbl(); break; // RDVBLBAR: 0 while blank
        case 0xA: on = s_.text; break;
        case 0xB: on = s_.mixed; break;
        case 0xC: on = s_.page2; break;
        case 0xD: on = s_.hires; break;
        case 0xE: on = s_.altchar; break;
        case 0xF: on = s_.col80; break;
        default: break;
        }
        const uint8_t latch = usb_ ? static_cast<uint8_t>(usb_->peek(UsbCard::KEY) & 0x7F) : 0;
        return static_cast<uint8_t>((on ? 0x80 : 0) | latch);
    }

    uint8_t AppleIo::rom(uint32_t offset) const
    {
        const uint32_t addr = 0xC000 + offset;
        bool internal = s_.intcxrom;
        if (!internal && addr >= 0xC300 && addr < 0xC400)
            internal = !s_.slotc3rom;
        else if (!internal && addr >= 0xC800)
            internal = s_.intc8rom;
        if (!internal)
            return disk_ && addr >= 0xC600 && addr < 0xC700 ? disk_->slot_rom(static_cast<uint8_t>(addr)) : FLOATING;
        return decoder_ ? decoder_->peek_physical(ROM_BASE | addr) : FLOATING;
    }

    void AppleIo::language_card(unsigned a, bool write)
    {
        s_.lc_bank2 = !(a & 0x8);
        s_.lc_read = (a & 3) == 0 || (a & 3) == 3;
        if (!(a & 1))
        {
            s_.lc_write = false;
            s_.lc_prewrite = false;
        }
        else if (write)
            s_.lc_prewrite = false; // only reads count towards the two
        else
        {
            if (s_.lc_prewrite)
                s_.lc_write = true;
            s_.lc_prewrite = true;
        }
    }

    uint8_t AppleIo::peek(uint32_t offset) const
    {
        return const_cast<AppleIo *>(this)->access(offset, false, 0, false);
    }

    uint8_t AppleIo::access(uint32_t offset, bool write, uint8_t data, bool effects)
    {
        if (offset >= 0x100) // $C100-$CFFF: ROM space
        {
            const uint32_t addr = 0xC000 + offset;
            const uint8_t v = write ? FLOATING : rom(offset);
            if (effects)
            {
                if (addr >= 0xC300 && addr < 0xC400 && !s_.slotc3rom)
                    s_.intc8rom = true;
                if (addr == 0xCFFF)
                    s_.intc8rom = false;
            }
            return v;
        }
        const unsigned lo = offset & 0x0F;
        switch (offset >> 4)
        {
        case 0x0: // keyboard; the MMU's switches on writes
            if (write)
            {
                if (effects)
                {
                    const bool on = lo & 1;
                    switch (lo >> 1)
                    {
                    case 0: s_.store80 = on; break;
                    case 1: s_.ramrd = on; break;
                    case 2: s_.ramwrt = on; break;
                    case 3: s_.intcxrom = on; break;
                    case 4: s_.altzp = on; break;
                    case 5: s_.slotc3rom = on; break;
                    case 6: s_.col80 = on; break;
                    case 7: s_.altchar = on; break;
                    default: break;
                    }
                }
                return FLOATING;
            }
            return usb_ ? usb_->peek(UsbCard::KEY) : 0;
        case 0x1:
            if (lo == 0 || write) // $C010, or any write here: clear the strobe
            {
                if (!usb_)
                    return 0;
                return effects ? usb_->read(UsbCard::KEYCLR) : usb_->peek(UsbCard::KEYCLR);
            }
            return status(lo);
        case 0x3: // the speaker
            if (usb_ && effects)
                usb_->read(UsbCard::SPKR);
            return FLOATING;
        case 0x5: // the IOU's video switches, on any access
            if (effects)
            {
                const bool on = lo & 1;
                switch (lo >> 1)
                {
                case 0: s_.text = on; break;
                case 1: s_.mixed = on; break;
                case 2: s_.page2 = on; break;
                case 3: s_.hires = on; break;
                default: // AN0-AN3; AN3 off is DHIRES on
                {
                    const unsigned n = (lo >> 1) - 4;
                    s_.an = static_cast<uint8_t>(on ? s_.an | 1u << n : s_.an & ~(1u << n));
                    if (n == 3)
                        s_.dhires = !on;
                    break;
                }
                }
            }
            return FLOATING;
        case 0x6: // game port, mirrored every 8
        {
            const unsigned n = lo & 7;
            if (n == 0 || !usb_)
                return 0; // cassette in
            // Buttons 0 and 1 are Open and Closed Apple too, as a IIe wires them
            const uint8_t game = static_cast<uint8_t>(usb_->peek(UsbCard::GAME) | usb_->apple_keys() << 4);
            const bool on = n <= 3 ? (game >> (4 + n - 1)) & 1 : (game >> (n - 4)) & 1;
            return on ? 0x80 : 0;
        }
        case 0x7:
            if (lo == 0xE && !write)
                return s_.ioudis ? 0x80 : 0;
            if (lo == 0xF && !write)
                return s_.dhires ? 0x80 : 0;
            if (write && (lo == 0xE || lo == 0xF))
            {
                if (effects)
                    s_.ioudis = lo == 0xE;
                return FLOATING;
            }
            if (usb_ && effects)
                usb_->read(UsbCard::PTRIG); // $C070-$C07D: the 558
            return FLOATING;
        case 0x8: // the language card, on any access
            if (effects)
                language_card(lo, write);
            return FLOATING;
        case 0xE: // slot 6
            return disk_ ? disk_->slot_io(lo, write, data, effects) : FLOATING;
        default: // cassette out, utility strobe, the other slots: nothing
            return FLOATING;
        }
    }
} // namespace ooe::emu
