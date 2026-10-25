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
    namespace
    {
        uint16_t be16(const std::vector<uint8_t> &b, size_t at) { return static_cast<uint16_t>(b[at] << 8 | b[at + 1]); }

        std::string text(const std::vector<uint8_t> &b, size_t at)
        {
            std::string s;
            for (size_t i = at; i < at + 32 && b[i]; ++i)
                s += static_cast<char>(b[i]);
            return s;
        }

        std::string hex4(uint32_t v)
        {
            char buf[8];
            std::snprintf(buf, sizeof buf, "$%04X", v & 0xFFFFu);
            return buf;
        }

        /// $01 for a routine at `addr`, as PSID players bank: ROMs out only where the tune is.
        uint8_t bank_for(uint16_t addr)
        {
            if (addr < 0xA000)
                return 0x37;
            if (addr < 0xD000)
                return 0x36;
            if (addr < 0xE000)
                return 0x34;
            return 0x35;
        }

        /// The stub, at `at`: see sid_file.h. Two passes, the first to find where its labels fall.
        struct Stub
        {
            uint16_t at, init, play, handler_vector;
            uint8_t song, init_bank, play_bank;
            uint16_t latch;
            bool set_0314;

            // labels, filled by the first pass
            uint16_t idle{0}, irq{0}, handler{0}, ack{0}, exit{0}, nmi{0}, frame{0};

            std::vector<uint8_t> emit()
            {
                std::vector<uint8_t> c;
                auto b = [&](std::initializer_list<int> bytes) {
                    for (const int x : bytes)
                        c.push_back(static_cast<uint8_t>(x));
                };
                auto w = [&](int op, uint16_t addr) { b({op, addr & 0xFF, addr >> 8}); };
                auto here = [&] { return static_cast<uint16_t>(at + c.size()); };

                b({0x78, 0xD8, 0xA2, 0xFF, 0x9A}); // SEI CLD LDX #$FF TXS
                b({0xA9, 0x2F, 0x85, 0x00});       // the port's DDR, as the KERNAL leaves it
                b({0xA9, 0x37, 0x85, 0x01});       // I/O in to set up the CIA
                b({0xA9, latch & 0xFF}), w(0x8D, 0xDC04);
                b({0xA9, latch >> 8}), w(0x8D, 0xDC05);
                b({0xA9, 0x81}), w(0x8D, 0xDC0D); // timer A's interrupt on
                b({0xA9, 0x11}), w(0x8D, 0xDC0E); // load and start, continuous
                if (set_0314)
                {
                    b({0xA9, handler_vector & 0xFF}), w(0x8D, 0x0314);
                    b({0xA9, handler_vector >> 8}), w(0x8D, 0x0315);
                }
                b({0xA9, init_bank, 0x85, 0x01});
                b({0xA9, song}), w(0x20, init);
                if (init_bank == 0x34) // I/O back for the interrupt; $35 and $36 differ from $37 only in ROMs
                    b({0xA9, 0x37, 0x85, 0x01});
                b({0x58}); // CLI
                idle = here();
                w(0x4C, idle);
                irq = here(); // the KERNAL's entry: PHA TXA PHA TYA PHA, JMP ($0314)
                b({0x48, 0x8A, 0x48, 0x98, 0x48}), w(0x6C, 0x0314);
                handler = here();
                if (play)
                {
                    b({0xA5, 0x01, 0x48});             // LDA $01 PHA
                    b({0xA9, play_bank, 0x85, 0x01}); // bank for play
                    w(0x20, play);
                    b({0xA9, 0x37, 0x85, 0x01});       // I/O in for the mailbox and the CIA
                    w(0xEE, frame), w(0xAD, frame), w(0x8D, 0xDE00);
                    w(0xAD, 0xDC0D);       // acknowledge
                    b({0x68, 0x85, 0x01}); // PLA STA $01
                    w(0x4C, static_cast<uint16_t>(here() + 6)); // past ack's 3 bytes to exit
                }
                ack = here();
                w(0xAD, 0xDC0D);
                exit = here();
                b({0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40}); // PLA TAY PLA TAX PLA RTI
                nmi = here();
                b({0x40});
                frame = here();
                b({0x00});
                return c;
            }
        };

        bool overlaps(const SidFile &f, uint32_t from, uint32_t to) { return from <= f.end() && to >= f.load; }
    } // namespace

    bool SidFile::cia_timed(unsigned song) const
    {
        const unsigned bit = song == 0 ? 0 : (song - 1 < 31 ? song - 1 : 31);
        return speed >> bit & 1;
    }

    std::optional<SidFile> parse_sid(const std::vector<uint8_t> &b, std::string &error)
    {
        if (b.size() < 0x76 || !(b[0] == 'P' || b[0] == 'R') || b[1] != 'S' || b[2] != 'I' || b[3] != 'D')
        {
            error = "not a .sid file (no PSID or RSID header)";
            return std::nullopt;
        }
        SidFile f;
        f.rsid = b[0] == 'R';
        f.version = be16(b, 0x04);
        const uint16_t offset = be16(b, 0x06);
        if (f.version < 1 || f.version > 4 || (f.rsid && f.version < 2))
        {
            error = "unknown .sid version " + std::to_string(f.version);
            return std::nullopt;
        }
        if (offset != (f.version == 1 ? 0x76 : 0x7C) || b.size() < offset + 2u + (be16(b, 0x08) ? 0 : 2))
        {
            error = "bad data offset";
            return std::nullopt;
        }
        f.load = be16(b, 0x08);
        f.init = be16(b, 0x0A);
        f.play = be16(b, 0x0C);
        f.songs = be16(b, 0x0E);
        f.start_song = be16(b, 0x10);
        f.speed = static_cast<uint32_t>(be16(b, 0x12)) << 16 | be16(b, 0x14);
        f.name = text(b, 0x16);
        f.author = text(b, 0x36);
        f.released = text(b, 0x56);
        if (f.version >= 2)
        {
            f.flags = be16(b, 0x76);
            f.start_page = b[0x78];
            f.page_length = b[0x79];
        }
        if (f.version >= 3 && b[0x7A])
            f.sid2 = static_cast<uint16_t>(0xD000 | b[0x7A] << 4);
        if (f.version >= 4 && b[0x7B])
            f.sid3 = static_cast<uint16_t>(0xD000 | b[0x7B] << 4);
        size_t at = offset;
        if (f.load == 0) // the C64's own: the load address leads the data
        {
            f.load = static_cast<uint16_t>(b[at] | b[at + 1] << 8);
            at += 2;
        }
        f.data.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.end());
        if (f.data.empty() || f.end() > 0xFFFF)
        {
            error = f.data.empty() ? "no code" : "code runs past $FFFF";
            return std::nullopt;
        }
        if (f.init == 0)
            f.init = f.load;
        if (f.songs == 0 || f.songs > 256)
        {
            error = "bad song count " + std::to_string(f.songs);
            return std::nullopt;
        }
        if (f.start_song == 0 || f.start_song > f.songs)
            f.start_song = 1;
        return f;
    }

    std::optional<SidPlan> plan_sid(const SidFile &f, unsigned song, std::string &error)
    {
        if (song == 0)
            song = f.start_song;
        if (song > f.songs)
        {
            error = "song " + std::to_string(song) + " of " + std::to_string(f.songs);
            return std::nullopt;
        }
        if (f.flags & SidFile::FLAG_MUS)
        {
            error = "a Compute! MUS file needs a player of its own";
            return std::nullopt;
        }
        if (f.rsid && (f.flags & SidFile::FLAG_BASIC))
        {
            error = "the tune is a BASIC program: the card has no C64 BASIC";
            return std::nullopt;
        }
        if (overlaps(f, 0xFFFA, 0xFFFF))
        {
            error = "the tune covers the 6510's vectors";
            return std::nullopt;
        }

        SidPlan plan;
        // The stub's page: the free one the header names, else the first of these the tune leaves alone.
        std::vector<unsigned> pages;
        if (f.version >= 2 && f.start_page != 0 && f.start_page != 0xFF && f.page_length)
            pages.push_back(f.start_page);
        for (const unsigned p : {0xFFu, 0xFEu, 0x04u, 0xC0u, 0x02u})
            pages.push_back(p);
        const bool ntsc = f.ntsc();
        Stub stub{0,
                  f.init,
                  f.play,
                  0,
                  static_cast<uint8_t>(song - 1),
                  bank_for(f.init),
                  bank_for(f.play),
                  static_cast<uint16_t>(f.rsid || f.cia_timed(song) ? (ntsc ? 0x4295 : 0x4025) : (ntsc ? 17095 - 1 : 19656 - 1)),
                  !overlaps(f, 0x0314, 0x0315)};
        std::vector<uint8_t> code;
        for (const unsigned p : pages)
        {
            const auto at = static_cast<uint16_t>(p << 8);
            stub.at = at;
            code = stub.emit(); // the labels
            stub.handler_vector = f.play ? stub.handler : stub.ack;
            code = stub.emit();
            const uint32_t last = at + static_cast<uint32_t>(code.size()) - 1;
            const bool clash = overlaps(f, at, last) || (at <= 0xFFF9 && last >= 0xFFFA) || (at <= 0xFEC1 && last >= 0xFEBC) ||
                               (at >= 0xD000 && at < 0xE000) || p == 0xEA;
            if (!clash)
                break;
            code.clear();
        }
        if (code.empty())
        {
            error = "no free page for the player's stub";
            return std::nullopt;
        }
        plan.stub = stub.at;
        plan.frame = stub.frame;
        plan.blocks.push_back({f.load, f.data});
        plan.blocks.push_back({stub.at, code});

        // The KERNAL's interrupt exits, where the tune leaves them free.
        const SidPlan::Block exits[] = {
            {0xEA31, {0x4C, 0x7E, 0xEA}},
            {0xEA7E, {0xAD, 0x0D, 0xDC, 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40}},
            {0xFEBC, {0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40}},
        };
        for (const auto &e : exits)
            if (!overlaps(f, e.addr, e.addr + static_cast<uint32_t>(e.bytes.size()) - 1u))
                plan.blocks.push_back(e);
        plan.blocks.push_back({0xFFFA,
                               {static_cast<uint8_t>(stub.nmi), static_cast<uint8_t>(stub.nmi >> 8),
                                static_cast<uint8_t>(stub.at), static_cast<uint8_t>(stub.at >> 8),
                                static_cast<uint8_t>(stub.irq), static_cast<uint8_t>(stub.irq >> 8)}});

        plan.model = f.model();
        plan.ctrl = SoundCard::CTRL_RUN | SoundCard::CTRL_RESET;
        if (ntsc)
            plan.ctrl |= SoundCard::CTRL_NTSC;
        if (f.sid2 == 0xD500)
            plan.ctrl |= SoundCard::CTRL_SID_B_D500;
        else if (f.sid2 == 0)
            plan.ctrl |= SoundCard::CTRL_BOTH; // a mono tune on both
        else if (f.sid2 != 0xD420)
            plan.warnings.push_back("its second SID is at " + hex4(f.sid2) + "; the card's is at $D420 or $D500");
        if (f.sid3)
            plan.warnings.push_back("it needs a third SID, at " + hex4(f.sid3));
        if (f.rsid)
            plan.warnings.push_back("an RSID tune expects a whole C64 (VIC-II, CIA 2, ROMs)");
        else if (f.flags & SidFile::FLAG_BASIC)
            plan.warnings.push_back("written for PlaySID's sample extensions");
        return plan;
    }

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
} // namespace ooe::emu
