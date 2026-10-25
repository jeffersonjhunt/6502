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
 * A `.sid` file is a header and the tune's own 6502 code, which runs on the
 * sound card's 6510 (sound_card.h). The header (HVSC's SID_file_format.txt)
 * gives where the code loads, its init and play routines, how many songs,
 * and per song whether play is called at 50 Hz (the vertical blank, VBI) or
 * from CIA 1's timer. Versions 2-4 add the clock (PAL or NTSC), the SID
 * model, and where a second and third SID sit.
 *
 * Playing it follows hw/design.md (".sid playback"), as a host program would
 * through the card's registers: stop the card, load the code at its load
 * address through RAMDATA, write a stub, set the clock and start the 6510.
 * The stub stands in for what the C64's KERNAL leaves behind for a tune:
 *
 * - $01 banked for the routine's address ($37 below $A000, $36 below $D000,
 *   $34 inside $D000-$DFFF, $35 above), the stack at $FF, decimal clear;
 * - CIA 1's timer A running with its interrupt on: every 19,656 clocks (PAL)
 *   or 17,095 (NTSC), a C64's frame, for a VBI song; the KERNAL's own $4025
 *   or $4295 for a CIA song or an RSID (which expects the C64 as the KERNAL
 *   leaves it), which init may reprogram;
 * - the IRQ vector at $FFFE entering as the KERNAL does (A, X, Y pushed,
 *   then JMP ($0314)), and the KERNAL's exits at $EA31, $EA7E, $EA81 and
 *   $FEBC, for tunes that install their own interrupt at $0314 and leave
 *   through them;
 * - init called with the song (0-based) in A; then, if the tune has a play
 *   routine, the stub's handler calls it on each interrupt and writes the
 *   frame's number to the mailbox, a sync point for the host.
 *
 * A tune with play = 0 (RSID, or a PSID that installs its own interrupt)
 * runs from init on its own. The card has no VIC-II, CIA 2 or ROMs: a tune
 * that needs the raster interrupt, CIA 2's NMIs, BASIC or KERNAL calls
 * beyond those exits runs but stays silent or stops. A third SID is beyond
 * the card; a second one plays only at $D420 or $D500.
 */

#include "sid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ooe::emu
{
    class SoundCard;

    struct SidFile
    {
        enum class Clock : uint8_t
        {
            Unknown,
            Pal,
            Ntsc,
            Either,
        };

        bool rsid{false};
        uint16_t version{0};
        uint16_t load{0}; ///< from the header, or the data's first two bytes
        uint16_t init{0};
        uint16_t play{0}; ///< 0: the tune installs its own interrupt
        uint16_t songs{1};
        uint16_t start_song{1}; ///< 1-based
        uint32_t speed{0};      ///< bit n: song n + 1 is CIA-timed (bit 31 for songs past 32)
        uint16_t flags{0};
        uint8_t start_page{0}, page_length{0}; ///< v2+: free memory the tune leaves
        uint16_t sid2{0}, sid3{0};             ///< v3/v4: the extra SIDs' addresses, or 0
        std::string name, author, released;
        std::vector<uint8_t> data; ///< the code, without its load address

        static constexpr uint16_t FLAG_MUS = 0x01, FLAG_BASIC = 0x02; ///< bit 1: PSID-specific, or RSID's BASIC

        Clock clock() const { return static_cast<Clock>((flags >> 2) & 3); }
        /// The chip it was written for: 8580 when it says only that, else the 6581.
        Sid::Model model() const { return ((flags >> 4) & 3) == 2 ? Sid::Model::Mos8580 : Sid::Model::Mos6581; }
        bool ntsc() const { return clock() == Clock::Ntsc; }
        /// The last address the code fills.
        uint32_t end() const { return load + static_cast<uint32_t>(data.size()) - 1; }
        /// Song `song` (1-based) is called from CIA 1's timer, not at 50 Hz.
        bool cia_timed(unsigned song) const;
    };

    /// `bytes` as a `.sid` file; nullopt and `error` if it isn't one.
    std::optional<SidFile> parse_sid(const std::vector<uint8_t> &bytes, std::string &error);

    /// What playing a tune needs written into the card's RAM, and how to start it.
    struct SidPlan
    {
        struct Block
        {
            uint16_t addr;
            std::vector<uint8_t> bytes;
        };
        std::vector<Block> blocks; ///< the tune, then the stub, the KERNAL exits and vectors
        uint8_t ctrl{0};           ///< CTRL to start with (RUN and RESET included)
        Sid::Model model{Sid::Model::Mos6581}; ///< the chips' jumper, as the tune wants
        uint16_t stub{0};          ///< where the stub sits
        uint16_t frame{0};         ///< the stub's frame counter, written to the mailbox after each play
        std::vector<std::string> warnings; ///< what about the tune the card can't give it
    };

    /// The plan for song `song` (1-based; 0: the file's start song). Nullopt
    /// and `error` when there is nowhere free for the stub, or no such song.
    std::optional<SidPlan> plan_sid(const SidFile &file, unsigned song, std::string &error);

    /// Stop the card's 6510 and silence the SIDs, through its host registers.
    void stop_sid(SoundCard &card);

    /// Carry out `plan` through the card's host registers: stop, load, start;
    /// and set the chips' model, as the card's owner would set the jumper.
    void start_sid(SoundCard &card, const SidPlan &plan);
} // namespace ooe::emu
