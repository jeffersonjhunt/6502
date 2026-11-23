#pragma once

/**
 * @file sid_file.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief C64 `.sid` files (PSID and RSID), and playing one on the sound card.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Playing a tune on revision 2's card, through its host registers: the
 * header and the plan are sid_plan.h's.
 */

#include "sid_plan.h"

namespace ooe::emu
{
    class SoundCard;

    /// Stop the card's 6510 and silence the SIDs, through its host registers.
    void stop_sid(SoundCard &card);

    /// Carry out `plan` through the card's host registers: stop, load, start;
    /// and set the chips' model, as the card's owner would set the jumper.
    void start_sid(SoundCard &card, const SidPlan &plan);

    /// The C64 I/O the card lacks that an access to `addr` ($D000-$DFFF, I/O
    /// in) reaches: "VIC-II", "colour RAM", "CIA 1 timer B", "CIA 1
    /// ports/TOD", "CIA 2" or "expansion I/O"; nullptr where the card answers
    /// (the SIDs, CIA 1's timer A, the mailbox, $DE03/$DE04, and $D011/$D012
    /// reads while `raster_shown`). For sidcheck's report of what a tune missed.
    const char *missing_io(uint16_t addr, bool write, bool raster_shown);
} // namespace ooe::emu
