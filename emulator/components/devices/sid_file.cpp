/**
 * @file sid_file.cpp
 * @brief C64 `.sid` files, and playing one on the sound card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sid_file.h"

#include "sound_card.h"

#include <cstdio>

namespace ooe::emu
{
    static_assert(SidPlan::CTRL_NTSC == SoundCard::CTRL_NTSC && SidPlan::CTRL_RUN == SoundCard::CTRL_RUN &&
                      SidPlan::CTRL_RESET == SoundCard::CTRL_RESET && SidPlan::CTRL_SID_B_D500 == SoundCard::CTRL_SID_B_D500 &&
                      SidPlan::CTRL_BOTH == SoundCard::CTRL_BOTH,
                  "the plan's CTRL is revision 2's");

    void stop_sid(SoundCard &card)
    {
        card.write(SoundCard::CTRL, 0); // stopped: the SIDs are the host's
        for (unsigned reg = 0; reg < 0x19; ++reg)
        {
            card.write(SoundCard::SID_A + reg, 0);
            card.write(SoundCard::SID_B + reg, 0);
        }
    }

    void start_sid(SoundCard &card, const SidPlan &plan)
    {
        stop_sid(card);
        card.set_model(plan.model);
        for (const auto &block : plan.blocks)
        {
            card.write(SoundCard::RAMADDR, static_cast<uint8_t>(block.addr));
            card.write(SoundCard::RAMADDR + 1, static_cast<uint8_t>(block.addr >> 8));
            for (const uint8_t byte : block.bytes)
                card.write(SoundCard::RAMDATA, byte);
        }
        card.write(SoundCard::CTRL, plan.ctrl);
    }

    const char *missing_io(uint16_t addr, bool write, bool raster_shown)
    {
        if ((addr == 0xD011 || addr == 0xD012) && !write && raster_shown)
            return nullptr;
        if (addr < 0xD400)
            return "VIC-II";
        if (addr >= 0xD800 && addr < 0xDC00)
            return "colour RAM";
        if (addr >= 0xDC00 && addr < 0xDD00)
        {
            const unsigned r = addr & 0x0F;
            if (r == SoundCard::CIA_TA_LO || r == SoundCard::CIA_TA_HI || r == SoundCard::CIA_ICR || r == SoundCard::CIA_CRA)
                return nullptr;
            return r == 0x06 || r == 0x07 || r == 0x0F ? "CIA 1 timer B" : "CIA 1 ports/TOD";
        }
        if (addr >= 0xDD00 && addr < 0xDE00)
            return "CIA 2";
        if (addr > 0xDE01 && addr != SoundCard::CARD_FRAME && addr != SoundCard::CARD_FRAME_EN)
            return "expansion I/O";
        return nullptr;
    }
} // namespace ooe::emu
