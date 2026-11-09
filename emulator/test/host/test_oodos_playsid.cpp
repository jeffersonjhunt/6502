/**
 * @file test_oodos_playsid.cpp
 * @brief OOLIB's L3 (hw/oolib.md, "The plan"): PLAYSID.COM and the linked library's .sid (oodos/lib/sid.s),
 * on OODOS.SYS booted as test_oodos_sound boots it.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Every tune here is built by the test, as test_sid_file.cpp builds its own: a few bytes of code (init keeps
 * its A, play counts its calls) under headers of each kind the player treats differently. PLAYSID plays each
 * from M:, and what it put in the card's RAM, and CTRL, are checked against the emulator's own player's plan
 * (sid_file.h's plan_sid) for the same bytes: the library is a port of its rules. Never the owner's tunes.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sid_file.h"
#include "sound_card.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    struct Header
    {
        bool rsid{false};
        uint16_t version{2};
        uint16_t load{0x1000};
        uint16_t init{0x1000};
        uint16_t play{0x1004};
        uint16_t songs{3};
        uint16_t start{2};
        uint32_t speed{0};
        uint16_t flags{0x0014}; ///< PAL, 6581
        uint8_t start_page{0}, page_length{0};
        uint8_t sid2{0};
        bool load_in_data{false};
    };
    constexpr uint16_t SCRATCH = 0x0800; ///< init's and play's: outside every tune and stub page here
    /// init: STA $0800 (the song less 1), RTS; play: INC $0801, RTS: at the tune's load address and + 4.
    std::vector<uint8_t> code(uint16_t) { return {0x8D, 0x00, 0x08, 0x60, 0xEE, 0x01, 0x08, 0x60}; }
    std::vector<uint8_t> sid_bytes(const Header &h, const std::vector<uint8_t> &body)
    {
        std::vector<uint8_t> b(h.version == 1 ? 0x76 : 0x7C, 0);
        auto put16 = [&](size_t at, uint16_t v) {
            b[at] = static_cast<uint8_t>(v >> 8);
            b[at + 1] = static_cast<uint8_t>(v);
        };
        b[0] = h.rsid ? 'R' : 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D';
        put16(0x04, h.version);
        put16(0x06, static_cast<uint16_t>(b.size()));
        put16(0x08, h.load_in_data ? 0 : h.load);
        put16(0x0A, h.init);
        put16(0x0C, h.play);
        put16(0x0E, h.songs);
        put16(0x10, h.start);
        put16(0x12, static_cast<uint16_t>(h.speed >> 16));
        put16(0x14, static_cast<uint16_t>(h.speed));
        const std::string name = "Test Tune   ", author = "Nobody";
        std::copy(name.begin(), name.end(), b.begin() + 0x16);
        std::copy(author.begin(), author.end(), b.begin() + 0x36);
        if (h.version >= 2)
        {
            put16(0x76, h.flags);
            b[0x78] = h.start_page;
            b[0x79] = h.page_length;
            b[0x7A] = h.sid2;
        }
        if (h.load_in_data)
        {
            b.push_back(static_cast<uint8_t>(h.load));
            b.push_back(static_cast<uint8_t>(h.load >> 8));
        }
        b.insert(b.end(), body.begin(), body.end());
        return b;
    }

    SoundCard &card(Rig &r) { return *r.machine->sound_card(); }

    /// PLAYSID and T.SID (`file`) on M:, the last record's byte count in its last entry (S1), as cpmtools
    /// writes it (one entry for each 32 KB).
    void put(CcpRig &r, const std::vector<uint8_t> &file)
    {
        r.put_util("playsid");
        r.put_m({{0, "T       SID", file, false, false, true}});
    }

    /// `file` played by PLAYSID with `args`: what it said up to its key prompt; its blocks and CTRL checked
    /// against plan_sid's for `song` (0: the file's start song), init and play seen to have run; then a key,
    /// and the card stopped and released.
    /// `exact`: the card's RAM from the tune's end to the end of the file's last record, but for plan_sid's
    /// blocks, filled with $A5 first and checked still so after: the record's padding not loaded.
    void plays(const std::vector<uint8_t> &file, const std::string &args, unsigned song, unsigned want_song,
               bool exact = false)
    {
        std::string error;
        const std::optional<SidFile> f = parse_sid(file, error);
        REQUIRE(f);
        const std::optional<SidPlan> plan = plan_sid(*f, song, error);
        REQUIRE(plan);
        CcpRig r;
        REQUIRE(r.ok);
        put(r, file);
        std::vector<uint16_t> past; // the padding's reach, outside every block plan_sid loads
        if (exact)
        {
            const size_t pad = (RECORD - file.size() % RECORD) % RECORD;
            const uint16_t end = static_cast<uint16_t>(f->load + f->data.size());
            for (size_t i = 0; i < pad; ++i)
            {
                const uint16_t a = static_cast<uint16_t>(end + i);
                bool planned = false;
                for (const SidPlan::Block &b : plan->blocks)
                    planned = planned || (a >= b.addr && a < b.addr + b.bytes.size());
                if (!planned)
                    past.push_back(a);
            }
            REQUIRE(!past.empty());
            for (const uint16_t a : past)
                card(r).ram()[a] = 0xA5;
            std::printf("  %zu bytes, the tune $%04X-$%04X, %zu bytes of padding past it watched\n", file.size(),
                        f->load, end - 1, past.size());
        }
        const std::string o = r.to("M:PLAYSID M:T" + args, "Press a key to stop.", ms(4000));
        std::printf("  %s\n", vis(o).c_str());
        CHECK(o.find("Playing song " + std::to_string(want_song) + " of " + std::to_string(f->songs) +
                     ": Test Tune, Nobody") != std::string::npos);
        r.run(ms(100));
        const auto &ram = card(r).ram();
        for (const SidPlan::Block &b : plan->blocks)
        {
            bool same = true;
            for (size_t i = 0; i < b.bytes.size(); ++i)
                same = same && ram[static_cast<uint16_t>(b.addr + i)] == b.bytes[i];
            if (b.addr == plan->stub) // the stub's frame counter moves once play runs
            {
                same = true;
                for (size_t i = 0; i < b.bytes.size(); ++i)
                    if (static_cast<uint16_t>(b.addr + i) != plan->frame)
                        same = same && ram[static_cast<uint16_t>(b.addr + i)] == b.bytes[i];
            }
            if (!same)
                std::printf("  block at $%04X (%zu bytes) differs from plan_sid's\n", b.addr, b.bytes.size());
            CHECK(same);
        }
        unsigned overwritten = 0;
        for (const uint16_t a : past)
            overwritten += ram[a] != 0xA5;
        if (exact)
            std::printf("  %u of them overwritten\n", overwritten);
        CHECK_EQ(overwritten, 0u);
        std::printf("  stub at $%04X, CTRL $%02X (plan $%02X)\n", plan->stub, card(r).peek(SoundCard::CTRL), plan->ctrl);
        CHECK_EQ(card(r).peek(SoundCard::CTRL), plan->ctrl & ~SoundCard::CTRL_RESET); // RESET a strobe: reads 0
        const uint16_t page = SCRATCH;
        if (f->init == f->load)
            CHECK_EQ(ram[page], want_song - 1); // init ran with the song less 1
        if (f->play)
            CHECK(ram[page + 1] > 2); // play called, and the stub's mailbox frame count with it
        if (f->play)
            CHECK(ram[plan->frame] > 2);
        const size_t from = r.out.size();
        r.type("x");
        CHECK(r.until("\r\nA>", ms(2000), from));
        CHECK_EQ(card(r).peek(SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
        CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("SCLAIM")), 0);
    }

    /// PLAYSID refusing `file` with `args`: its message.
    std::string refuses(const std::vector<uint8_t> &file, const std::string &args = "")
    {
        CcpRig r;
        CHECK(r.ok);
        if (!r.ok)
            return {};
        put(r, file);
        const std::string o = r.cmd("M:PLAYSID M:T" + args);
        CHECK_EQ(card(r).peek(SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
        return o;
    }
} // namespace

TEST(a_frame_timed_song_plays_as_the_emulator_s_player_plans_it)
{
    Header h; // v2, PAL, three songs, start 2, play: the card's frame interrupt
    plays(sid_bytes(h, code(0x1000)), "", 0, 2);
    plays(sid_bytes(h, code(0x1000)), " 3", 3, 3);
}

TEST(a_cia_timed_ntsc_song_on_the_header_s_free_page)
{
    Header h;
    h.flags = 0x0018; // NTSC
    h.speed = 0x01;   // song 1 from CIA 1's timer
    h.start = 1;
    h.start_page = 0xC8, h.page_length = 2;
    plays(sid_bytes(h, code(0x1000)), "", 0, 1);
}

TEST(a_version_1_tune_with_its_load_address_in_its_data_and_no_play)
{
    Header h;
    h.version = 1;
    h.load = 0x2000, h.init = 0x2000, h.play = 0;
    h.load_in_data = true;
    h.songs = 1, h.start = 1;
    plays(sid_bytes(h, code(0x2000)), "", 0, 1);
}

TEST(a_tune_over_0314_and_the_top_pages_and_a_second_sid_at_d500)
{
    {
        Header h; // $0200-$04FF: $0314 is the tune's, so the stub leaves it; pages $02 and $04 taken
        h.load = 0x0200, h.init = 0x0200, h.play = 0x0204;
        h.version = 3, h.sid2 = 0x50;
        std::vector<uint8_t> body = code(0x0200);
        body.resize(0x300, 0xEA);
        plays(sid_bytes(h, body), "", 0, 2);
    }
    {
        Header h; // $F000-$FFEF: $FF and $FE taken, the stub at $04
        h.load = 0xF000, h.init = 0xF000, h.play = 0xF004;
        std::vector<uint8_t> body = code(0xF000);
        body.resize(0x0FF0, 0xEA);
        plays(sid_bytes(h, body), "", 0, 2);
    }
}

TEST(a_tune_of_two_entries_loads_to_its_last_byte_and_no_further)
{
    // L4a (hw/oolib.md): 34,956 bytes, two of M:'s entries, the last record 12 bytes; the count is in the
    // second entry only, as cpmtools keeps it. The tune $7000-$F80F: the record's 116 bytes of padding
    // would go on to $F883.
    Header h;
    h.load = 0x7000, h.init = 0x7000, h.play = 0x7004;
    std::vector<uint8_t> body = code(0x7000);
    body.resize(0x8810, 0xEA);
    const std::vector<uint8_t> file = sid_bytes(h, body);
    REQUIRE(file.size() > 32768 && file.size() % RECORD == 12);
    plays(file, "", 0, 2, true);
}

TEST(song_256_of_256)
{
    Header h; // the most songs a header may have; 256 its start song too, and CIA-timed (speed's bit 31)
    h.songs = 256, h.start = 256;
    h.speed = 0x80000000;
    plays(sid_bytes(h, code(0x1000)), "", 0, 256);
    plays(sid_bytes(h, code(0x1000)), " 256", 256, 256);
    CHECK(refuses(sid_bytes(h, code(0x1000)), " 257").find("Song 257 of 256?") != std::string::npos);
}

TEST(what_playsid_refuses)
{
    Header h;
    CHECK(refuses(sid_bytes(h, code(0x1000)), " 5").find("Song 5 of 3?") != std::string::npos);
    Header r = h;
    r.rsid = true;
    CHECK(refuses(sid_bytes(r, code(0x1000))).find("An RSID tune") != std::string::npos);
    Header m = h;
    m.flags = 0x0015; // MUS
    CHECK(refuses(sid_bytes(m, code(0x1000))).find("MUS") != std::string::npos);
    Header v = h;
    v.load = 0xFF00, v.init = 0xFF00, v.play = 0xFF04;
    std::vector<uint8_t> top = code(0xFF00);
    top.resize(0x100, 0xEA);
    CHECK(refuses(sid_bytes(v, top)).find("vectors") != std::string::npos);
    CHECK(refuses(std::vector<uint8_t>(200, 0x41)).find("Not a .sid file.") != std::string::npos);
    {
        CcpRig c;
        REQUIRE(c.ok);
        c.put_util("playsid");
        CHECK(c.cmd("M:PLAYSID M:NONE").find("No File") != std::string::npos);
        CHECK(c.cmd("M:PLAYSID").find("PLAYSID filespec {song}") != std::string::npos);
    }
}

int main() { return run_all(); }
