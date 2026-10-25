/**
 * @file test_sid_file.cpp
 * @brief `.sid` files: the header, the plan, and tunes played on the card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The tunes here are a few bytes of hand-assembled 6502, built into PSID
 * files on the fly: each counts its play calls in a SID register (or RAM),
 * so the counts say how often the stub called it, with what in A, from
 * which interrupt path, and with what banked in.
 */

#include "harness.h"

#include "sid_file.h"
#include "sound_card.h"

#include <initializer_list>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct Header
    {
        bool rsid{false};
        uint16_t version{2};
        uint16_t load{0x1000}; ///< 0: put it in the data's first two bytes
        uint16_t init{0x1000};
        uint16_t play{0};
        uint16_t songs{1};
        uint16_t start{1};
        uint32_t speed{0};
        uint16_t flags{0x0014}; ///< PAL, 6581
        uint8_t start_page{0}, page_length{0};
        uint8_t sid2{0}, sid3{0};
        bool load_in_data{false};
    };

    std::vector<uint8_t> sid_bytes(const Header &h, const std::vector<uint8_t> &code)
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
        const std::string name = "Test Tune   ", author = "Nobody"; // trailing spaces: padding, as some files have
        std::copy(name.begin(), name.end(), b.begin() + 0x16);
        std::copy(author.begin(), author.end(), b.begin() + 0x36);
        if (h.version >= 2)
        {
            put16(0x76, h.flags);
            b[0x78] = h.start_page;
            b[0x79] = h.page_length;
            b[0x7A] = h.sid2;
            b[0x7B] = h.sid3;
        }
        if (h.load_in_data)
        {
            b.push_back(static_cast<uint8_t>(h.load));
            b.push_back(static_cast<uint8_t>(h.load >> 8));
        }
        b.insert(b.end(), code.begin(), code.end());
        return b;
    }

    SidFile parsed(const Header &h, const std::vector<uint8_t> &code)
    {
        std::string error;
        const auto f = parse_sid(sid_bytes(h, code), error);
        CHECK_EQ(error, std::string());
        return f ? *f : SidFile{};
    }

    /// init at $1000: song to $C000, volume up. play at $1009: INC $D400.
    const std::vector<uint8_t> COUNTER = {0x8D, 0x00, 0xC0, 0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60, // init
                                          0xEE, 0x00, 0xD4, 0x60};                               // play

    struct Played
    {
        SoundCard card;
        unsigned frames{0}; ///< mailbox sync points

        /// Song `song` of `f` for `clocks` SID clocks.
        Played(const SidFile &f, uint64_t clocks, unsigned song = 0)
        {
            std::string error;
            const auto plan = plan_sid(f, song, error);
            CHECK_EQ(error, std::string());
            if (!plan)
                return;
            card.set_cpu_hz(f.ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ);
            start_sid(card, *plan);
            for (uint64_t t = 0; t < clocks; t += 64)
            {
                card.tick(64);
                if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
                {
                    CHECK_EQ(card.read(SoundCard::MBIN), static_cast<uint8_t>(frames + 1));
                    ++frames;
                }
            }
            CHECK(card.cpu().state() == Cpu6502::State::Running);
        }
        uint8_t reg(unsigned r) const { return card.sid(0)[r]; }
    };

    constexpr uint64_t PAL_FRAME = 19656, NTSC_FRAME = 17095, SLACK = 2000; // reset, init: under a frame
} // namespace

// --- the header -------------------------------------------------------------

TEST(a_psid_header_reads_back)
{
    Header h;
    h.play = 0x1009, h.songs = 3, h.start = 2, h.speed = 0x2, h.flags = 0x0028, h.sid2 = 0x50;
    h.version = 3, h.start_page = 0x20, h.page_length = 0x10;
    const SidFile f = parsed(h, COUNTER);
    CHECK(!f.rsid);
    CHECK_EQ(f.version, 3);
    CHECK_EQ(f.load, 0x1000);
    CHECK_EQ(f.init, 0x1000);
    CHECK_EQ(f.play, 0x1009);
    CHECK_EQ(f.songs, 3);
    CHECK_EQ(f.start_song, 2);
    CHECK_EQ(f.name, std::string("Test Tune"));
    CHECK_EQ(f.author, std::string("Nobody"));
    CHECK(f.clock() == SidFile::Clock::Ntsc);
    CHECK_EQ(f.sid2, 0xD500);
    CHECK_EQ(f.sid3, 0);
    CHECK_EQ(f.start_page, 0x20);
    CHECK_EQ(f.data.size(), COUNTER.size());
    CHECK_EQ(f.end(), 0x1000u + COUNTER.size() - 1);
    CHECK(!f.cia_timed(1));
    CHECK(f.cia_timed(2));
    CHECK(!f.cia_timed(3));
}

TEST(the_load_address_can_lead_the_data_and_init_0_is_the_load_address)
{
    Header h;
    h.load = 0x2000, h.load_in_data = true, h.init = 0;
    const SidFile f = parsed(h, COUNTER);
    CHECK_EQ(f.load, 0x2000);
    CHECK_EQ(f.init, 0x2000);
    CHECK_EQ(f.data.size(), COUNTER.size()); // the address isn't code
    CHECK_EQ(f.data[0], COUNTER[0]);
}

TEST(songs_past_32_share_speed_bit_31)
{
    Header h;
    h.songs = 40, h.speed = 0x80000000u;
    const SidFile f = parsed(h, COUNTER);
    CHECK(!f.cia_timed(31));
    CHECK(f.cia_timed(32));
    CHECK(f.cia_timed(40));
}

TEST(what_isnt_a_sid_file_is_refused)
{
    std::string error;
    CHECK(!parse_sid(std::vector<uint8_t>(0x7C + 4, 0), error));
    CHECK(!error.empty());
    std::vector<uint8_t> b = sid_bytes(Header{}, COUNTER);
    b.resize(0x60);
    CHECK(!parse_sid(b, error)); // short
    Header past;
    past.load = 0xFFF8;
    CHECK(!parse_sid(sid_bytes(past, COUNTER), error)); // runs past $FFFF
    CHECK_EQ(error, std::string("code runs past $FFFF"));
    Header rsid1;
    rsid1.rsid = true, rsid1.version = 1;
    CHECK(!parse_sid(sid_bytes(rsid1, COUNTER), error));
    Header none;
    none.songs = 0;
    CHECK(!parse_sid(sid_bytes(none, COUNTER), error));
}

// --- the plan ---------------------------------------------------------------

TEST(the_stub_goes_on_a_free_page_with_the_vectors_at_fffa)
{
    std::string error;
    Header h;
    h.play = 0x1009;
    auto plan = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(plan);
    CHECK_EQ(plan->stub, 0xFF00);
    CHECK_EQ(plan->blocks.front().addr, 0x1000); // the tune first
    const auto &vectors = plan->blocks.back();
    CHECK_EQ(vectors.addr, 0xFFFA);
    CHECK_EQ(vectors.bytes[2] | vectors.bytes[3] << 8, 0xFF00); // reset: the stub

    // The header's free page, when it names one.
    h.start_page = 0x20, h.page_length = 1;
    plan = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(plan);
    CHECK_EQ(plan->stub, 0x2000);

    // A tune over $FE00-$FF7F: the next candidate, $0400.
    Header high;
    high.load = 0xFE00, high.init = 0xFE00;
    plan = plan_sid(parsed(high, std::vector<uint8_t>(0x180, 0x60)), 0, error);
    REQUIRE(plan);
    CHECK_EQ(plan->stub, 0x0400);

    // A tune over the vectors can't be started.
    Header vec;
    vec.load = 0xFF80, vec.init = 0xFF80;
    CHECK(!plan_sid(parsed(vec, std::vector<uint8_t>(0x7C, 0x60)), 0, error));
}

TEST(the_kernal_exits_stay_out_of_a_tune_over_them)
{
    std::string error;
    Header h;
    const auto plan = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(plan);
    auto has = [&](const SidPlan &p, uint16_t addr) {
        for (const auto &b : p.blocks)
            if (b.addr == addr)
                return true;
        return false;
    };
    CHECK(has(*plan, 0xEA31));
    CHECK(has(*plan, 0xEA7E));
    CHECK(has(*plan, 0xFEBC));
    Header over;
    over.load = 0xE000, over.init = 0xE000;
    const auto p2 = plan_sid(parsed(over, std::vector<uint8_t>(0xB00, 0x60)), 0, error);
    REQUIRE(p2);
    CHECK(!has(*p2, 0xEA31));
    CHECK(!has(*p2, 0xEA7E));
    CHECK(has(*p2, 0xFEBC));
}

TEST(ctrl_follows_the_clock_and_the_second_sid)
{
    std::string error;
    Header h;
    auto ctrl = [&](const Header &x) {
        const auto p = plan_sid(parsed(x, COUNTER), 0, error);
        return p ? p->ctrl : 0;
    };
    const uint8_t base = SoundCard::CTRL_RUN | SoundCard::CTRL_RESET;
    CHECK_EQ(ctrl(h), base | SoundCard::CTRL_BOTH); // mono: on both
    h.flags = 0x0008;
    CHECK_EQ(ctrl(h), base | SoundCard::CTRL_BOTH | SoundCard::CTRL_NTSC);
    h.flags = 0x000C; // either: PAL
    CHECK_EQ(ctrl(h), base | SoundCard::CTRL_BOTH);
    h.version = 3, h.flags = 0x0004, h.sid2 = 0x42;
    CHECK_EQ(ctrl(h), base);
    h.sid2 = 0x50;
    CHECK_EQ(ctrl(h), base | SoundCard::CTRL_SID_B_D500);

    h.sid2 = 0x60; // $D600: nowhere the card has one
    auto p = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(p);
    CHECK_EQ(p->warnings.size(), 1u);
    h.version = 4, h.sid2 = 0x42, h.sid3 = 0x44;
    p = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(p);
    CHECK_EQ(p->warnings.size(), 1u);
    CHECK(p->warnings[0].find("third") != std::string::npos);
}

TEST(the_model_follows_the_header)
{
    std::string error;
    auto model = [&](uint16_t flags) {
        Header h;
        h.flags = flags;
        return parsed(h, COUNTER).model();
    };
    CHECK(model(0x0014) == Sid::Model::Mos6581);
    CHECK(model(0x0024) == Sid::Model::Mos8580);
    CHECK(model(0x0034) == Sid::Model::Mos6581); // either: the 6581
    CHECK(model(0x0004) == Sid::Model::Mos6581); // unknown
    Header h;
    h.flags = 0x0024;
    const auto plan = plan_sid(parsed(h, COUNTER), 0, error);
    REQUIRE(plan);
    CHECK(plan->model == Sid::Model::Mos8580);
    SoundCard card;
    start_sid(card, *plan);
    CHECK(card.chip(0).model() == Sid::Model::Mos8580);
    CHECK(card.chip(1).model() == Sid::Model::Mos8580);
}

TEST(tunes_the_card_cant_start_are_refused)
{
    std::string error;
    Header mus;
    mus.flags = 0x0001;
    CHECK(!plan_sid(parsed(mus, COUNTER), 0, error));
    Header basic;
    basic.rsid = true, basic.flags = 0x0002;
    CHECK(!plan_sid(parsed(basic, COUNTER), 0, error));
    Header three;
    three.songs = 3;
    CHECK(!plan_sid(parsed(three, COUNTER), 4, error));
    CHECK(plan_sid(parsed(three, COUNTER), 3, error).has_value());
}

// --- played ---------------------------------------------------------------

TEST(a_vbi_tune_is_played_50_times_a_second)
{
    Header h;
    h.play = 0x1009;
    const SidFile f = parsed(h, COUNTER);
    Played p(f, 50 * PAL_FRAME + SLACK);
    CHECK_EQ(p.reg(0x00), 50);
    CHECK_EQ(p.frames, 50u);
    CHECK_EQ(p.reg(0x18), 0x0F);
    CHECK_EQ(p.card.ram()[0xC000], 0); // song 1: A = 0
    CHECK_EQ(p.card.sid(1)[0x00], 50); // mono: on SID B as well
    Played early(f, 50 * PAL_FRAME - SLACK);
    CHECK_EQ(early.frames, 49u);
}

TEST(init_gets_the_song_less_one)
{
    Header h;
    h.play = 0x1009, h.songs = 5, h.start = 4;
    const SidFile f = parsed(h, COUNTER);
    Played start(f, PAL_FRAME);
    CHECK_EQ(start.card.ram()[0xC000], 3);
    Played second(f, PAL_FRAME, 2);
    CHECK_EQ(second.card.ram()[0xC000], 1);
}

TEST(a_cia_song_plays_at_the_kernals_rate)
{
    Header h;
    h.play = 0x1009, h.songs = 2, h.speed = 0x2; // song 2 on the CIA
    const SidFile f = parsed(h, COUNTER);
    Played cia(f, 60 * (0x4025 + 1) + SLACK, 2);
    CHECK_EQ(cia.frames, 60u);
    Played vbi(f, 60 * (0x4025 + 1) + SLACK, 1);
    CHECK_EQ(vbi.frames, 50u);
}

/// Three play periods, from the 2nd sync point to the 5th, in SID clocks. The
/// interrupt waits for the stub's 3-cycle JMP to end, so one period can be a
/// clock or two out; three of them come back to the same phase.
uint64_t period(const SidFile &f, unsigned song = 0)
{
    std::string error;
    const auto plan = plan_sid(f, song, error);
    if (!plan)
        return 0;
    SoundCard card;
    card.set_cpu_hz(f.ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ);
    start_sid(card, *plan);
    std::vector<uint64_t> at;
    for (uint64_t t = 1; at.size() < 5 && t < 6 * PAL_FRAME; ++t)
    {
        card.tick(1);
        if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
        {
            card.read(SoundCard::MBIN);
            at.push_back(t);
        }
    }
    return at.size() == 5 ? at[4] - at[1] : 0;
}

TEST(the_play_period_is_a_frame_or_the_kernals_timer_to_the_clock)
{
    Header h;
    h.play = 0x1009, h.songs = 2, h.speed = 0x2;
    const SidFile pal = parsed(h, COUNTER);
    CHECK_EQ(period(pal, 1), 3 * PAL_FRAME);
    CHECK_EQ(period(pal, 2), 3 * (0x4025u + 1));
    h.flags = 0x0008;
    const SidFile ntsc = parsed(h, COUNTER);
    CHECK_EQ(period(ntsc, 1), 3 * NTSC_FRAME);
    CHECK_EQ(period(ntsc, 2), 3 * (0x4295u + 1));
}

TEST(io_comes_back_after_an_init_under_it)
{
    // $CFF0: the tune's interrupt, INC $D400, JMP $EA31. $D800: init, under
    // the I/O ($01 = $34 for it), points $0314 at $CFF0. The interrupt then
    // needs I/O in to reach the SID and acknowledge the CIA.
    std::vector<uint8_t> code = {0xEE, 0x00, 0xD4, 0x4C, 0x31, 0xEA};
    code.resize(0xD800 - 0xCFF0, 0xEA);
    for (const int b : {0xA9, 0xF0, 0x8D, 0x14, 0x03, 0xA9, 0xCF, 0x8D, 0x15, 0x03, 0x60})
        code.push_back(static_cast<uint8_t>(b));
    Header h;
    h.load = 0xCFF0, h.init = 0xD800;
    Played p(parsed(h, code), 50 * PAL_FRAME + SLACK);
    CHECK_EQ(p.reg(0x00), 50);
}

TEST(an_ntsc_tune_gets_ntsc_frames)
{
    Header h;
    h.play = 0x1009, h.flags = 0x0008;
    Played p(parsed(h, COUNTER), 60 * NTSC_FRAME + SLACK);
    CHECK_EQ(p.frames, 60u);
    CHECK(p.card.ctrl() & SoundCard::CTRL_NTSC);
}

TEST(a_tune_with_its_own_interrupt_at_0314_leaves_by_ea31)
{
    // init: point $0314 at $1010 and return. $1010: INC $D400, JMP $EA31.
    std::vector<uint8_t> code = {0xA9, 0x10, 0x8D, 0x14, 0x03, 0xA9, 0x10, 0x8D, 0x15, 0x03, 0x60};
    code.resize(0x10, 0xEA);
    for (const int b : {0xEE, 0x00, 0xD4, 0x4C, 0x31, 0xEA})
        code.push_back(static_cast<uint8_t>(b));
    Header h; // play 0
    Played p(parsed(h, code), 50 * PAL_FRAME + SLACK);
    CHECK_EQ(p.reg(0x00), 50); // acknowledged each time, so once a frame
    CHECK_EQ(p.frames, 0u);    // no play routine, no sync points

    // An RSID gets the CIA as the KERNAL leaves it: $4025, about 60 Hz.
    h.rsid = true;
    Played r(parsed(h, code), 60 * (0x4025 + 1) + SLACK);
    CHECK_EQ(r.reg(0x00), 60);
}

TEST(a_tune_banking_out_the_kernal_installs_its_own_fffe)
{
    // init: $01 = $35, $FFFE -> $1010. $1010: PHA, INC $D401, LDA $DC0D, PLA, RTI.
    std::vector<uint8_t> code = {0xA9, 0x35, 0x85, 0x01, 0xA9, 0x10, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x60};
    code.resize(0x10, 0xEA);
    for (const int b : {0x48, 0xEE, 0x01, 0xD4, 0xAD, 0x0D, 0xDC, 0x68, 0x40})
        code.push_back(static_cast<uint8_t>(b));
    Played p(parsed(Header{}, code), 50 * PAL_FRAME + SLACK);
    CHECK_EQ(p.reg(0x01), 50);
    CHECK_EQ(p.card.port_data(), 0x35);
}

TEST(play_under_the_io_runs_with_io_banked_out)
{
    // A tune at $D800: init RTS, play INC $D900 (RAM, with $01 = $34). The
    // stub banks I/O back in for its sync point.
    Header h;
    h.load = 0xD800, h.init = 0xD800, h.play = 0xD801;
    Played p(parsed(h, {0x60, 0xEE, 0x00, 0xD9, 0x60}), 50 * PAL_FRAME + SLACK);
    CHECK_EQ(p.card.ram()[0xD900], 50);
    CHECK_EQ(p.frames, 50u);
}

TEST(a_second_tune_starts_clean)
{
    Header h;
    h.play = 0x1009;
    const SidFile f = parsed(h, COUNTER);
    Played p(f, 10 * PAL_FRAME);
    std::string error;
    const auto plan = plan_sid(f, 0, error);
    REQUIRE(plan);
    start_sid(p.card, *plan); // over the running one
    CHECK_EQ(p.card.sid(0)[0x00], 0);
    CHECK_EQ(p.card.sid(0)[0x18], 0);
    p.card.tick(5 * PAL_FRAME + SLACK);
    CHECK_EQ(p.card.sid(0)[0x00], 5);
}

int main() { return run_all(); }
