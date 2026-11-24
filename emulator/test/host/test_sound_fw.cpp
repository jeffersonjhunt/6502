/**
 * @file test_sound_fw.cpp
 * @brief The sound card's firmware, revision 3, alone (sound_fw.h; hw/future/sound-device.md and its S1 appendix).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The firmware on a fake port (no chips): the host's side is its FIFOs, as the front end's registers would give
 * them. Records are written byte by byte, the card's clock advanced, and the events read back and parsed.
 */

// The firmware depends on its port alone (the spec's Q2): its header, first, brings in no emulator chip.
#include "sound_fw.h"
#ifdef OOE_EMU_SID_H
#error "sound_fw.h brings in sid.h, the emulator's SID"
#endif

#include "harness.h"

#include "sid_plan.h"
#include "sound_flash.h"
#include "sound_card3.h"
#include "sound_fw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct FakePort : SoundFwPort
    {
        unsigned silenced{0};
        void silence() override { ++silenced; }
        void tune_start(const SidPlan &, bool, bool) override {}
        void tune_stop() override {}
        void tune_hold(bool) override {}
        bool tune_jammed(uint16_t &) override { return false; }
        uint64_t play_calls() override { return 0; }
        uint32_t play_cycles() override { return 0; }
        void sid_regs(unsigned, uint8_t out[25]) override
        {
            for (unsigned i = 0; i < 25; ++i)
                out[i] = 0;
        }
        void sid_write(unsigned, unsigned, uint8_t) override {}
        uint32_t adc_hz() override { return 0; }
        void adc_take(std::vector<float> &) override {}
        void doc_write(uint8_t, uint8_t) override {}
        uint8_t doc_read(uint8_t) override { return 0; }
        void doc_ram(uint32_t, const uint8_t *, size_t) override {}
        void doc_frame(int16_t out[2]) override { out[0] = out[1] = 0; }
    };

    struct Event
    {
        uint8_t kind{0};
        std::vector<uint8_t> body; ///< the bytes after n
        uint8_t tag() const { return body.at(0); }
        uint8_t status() const { return body.at(1); }
    };

    /// A firmware on a fake port, powered on and up.
    struct Card
    {
        FakePort port;
        SoundFw fw;
        explicit Card(unsigned units = 128) : fw(port, units)
        {
            fw.advance(SoundFw::STARTUP_MS * SoundFw::SAMPLES_PER_MS);
            CHECK(fw.ready());
        }
        void send(uint8_t cmd, uint8_t tag, std::vector<uint8_t> params = {})
        {
            send_raw(cmd, static_cast<uint8_t>(params.size()), tag, params);
        }
        void send_raw(uint8_t cmd, uint8_t n, uint8_t tag, const std::vector<uint8_t> &params)
        {
            CHECK(fw.cmd_write(cmd));
            CHECK(fw.cmd_write(n));
            CHECK(fw.cmd_write(tag));
            for (const uint8_t b : params)
                CHECK(fw.cmd_write(b));
        }
        void data(const std::vector<uint8_t> &bytes)
        {
            for (const uint8_t b : bytes)
                CHECK(fw.data_write(b));
        }
        /// Every event waiting, parsed.
        std::vector<Event> events()
        {
            std::vector<Event> out;
            while (fw.evt_len())
            {
                Event e;
                e.kind = fw.evt_read();
                const uint8_t n = fw.evt_read();
                for (unsigned i = 0; i < n; ++i)
                    e.body.push_back(fw.evt_read());
                out.push_back(e);
            }
            return out;
        }
    };

    std::vector<uint8_t> le24(uint32_t x) { return {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16)}; }
    uint32_t le32(const std::vector<uint8_t> &v, size_t at)
    {
        return static_cast<uint32_t>(v.at(at) | v.at(at + 1) << 8 | v.at(at + 2) << 16) | static_cast<uint32_t>(v.at(at + 3)) << 24;
    }
    /// SAMPLE_LOAD's parameters: slot, length, flags 0, rate 22,050, no loop.
    std::vector<uint8_t> sample_load(uint8_t slot, uint32_t len)
    {
        std::vector<uint8_t> p{slot};
        for (const uint8_t b : le24(len))
            p.push_back(b);
        p.insert(p.end(), {0x00, 0x22, 0x56, 0, 0, 0, 0, 0, 0});
        return p;
    }
    std::vector<uint8_t> noise(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> b(n);
        for (auto &x : b)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            x = static_cast<uint8_t>(seed >> 8);
        }
        return b;
    }
    /// The CRC-32 the test computes itself, from its own table.
    uint32_t crc32(const std::vector<uint8_t> &b)
    {
        static uint32_t table[256];
        static bool made = false;
        if (!made)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            made = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (const uint8_t x : b)
            c = table[(c ^ x) & 0xFF] ^ (c >> 8);
        return ~c;
    }
} // namespace

TEST(ready_comes_after_the_start_up_and_identify_says_what_the_card_is)
{
    FakePort port;
    SoundFw fw(port, 64);
    fw.advance(SoundFw::STARTUP_MS * SoundFw::SAMPLES_PER_MS - 1);
    CHECK(!fw.ready());
    fw.advance(1);
    CHECK(fw.ready());
    CHECK(fw.take_ready());
    CHECK(!fw.take_ready());
    Card c(64);
    c.send(SoundFw::IDENTIFY, 7);
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].kind, SoundFw::EV_REPLY);
    CHECK_EQ(ev[0].tag(), 7);
    CHECK_EQ(ev[0].status(), 0);
    REQUIRE(ev[0].body.size() == 2 + 32u);
    const std::vector<uint8_t> &r = ev[0].body;
    CHECK_EQ(r[2], 0xA3);
    CHECK_EQ(r[3], SoundFw::VERSION_MAJOR);
    CHECK_EQ(r[4], SoundFw::VERSION_MINOR);
    CHECK_EQ(r[7], 64);
    CHECK_EQ(le32(r, 14), 64u * 65536u);
    CHECK_EQ(le32(r, 10), 64u * 65536u); // all free
}

TEST(every_command_gets_one_reply_in_order)
{
    // A load, a CHECKSUM of what it loads (which waits for the transfer), a NOP and TIME behind it: the replies
    // come in the commands' order, the CHECKSUM's after the load's XFERDONE.
    Card c;
    const std::vector<uint8_t> bytes = noise(100, 1);
    c.send(SoundFw::SAMPLE_LOAD, 1, sample_load(5, 100));
    c.send(SoundFw::CHECKSUM, 2, {SoundFw::SAMPLE_LOAD, 5});
    c.send(SoundFw::NOP, 3);
    c.send(SoundFw::TIME, 4);
    c.fw.advance(5);
    auto ev = c.events();
    REQUIRE(ev.size() == 1); // the load's reply; the rest wait
    CHECK_EQ(ev[0].tag(), 1);
    CHECK_EQ(ev[0].status(), 0);
    CHECK(c.fw.xfer_open());
    CHECK_EQ(c.fw.xleft(), 100u);
    CHECK_EQ(c.fw.xtag(), 1);
    c.data(bytes);
    CHECK(!c.fw.xfer_open());
    CHECK(c.fw.take_xdone());
    CHECK(!c.fw.data_write(0)); // no transfer: dropped
    c.fw.advance(5);
    ev = c.events();
    REQUIRE(ev.size() == 4);
    CHECK_EQ(ev[0].kind, SoundFw::EV_XFERDONE);
    CHECK_EQ(ev[0].body[0], 1);                    // the load's tag
    CHECK_EQ(ev[0].body[5], SoundFw::SAMPLE_LOAD); // the command
    CHECK_EQ(ev[0].body[6], 0);                    // taken
    const uint8_t want[] = {2, 3, 4};
    for (unsigned i = 0; i < 3; ++i)
    {
        CHECK_EQ(ev[1 + i].kind, SoundFw::EV_REPLY);
        CHECK_EQ(ev[1 + i].tag(), want[i]);
        CHECK_EQ(ev[1 + i].status(), 0);
    }
    REQUIRE(ev[1].body.size() == 10u);
    CHECK_EQ(le32(ev[1].body, 2), crc32(bytes));
    CHECK_EQ(le32(ev[1].body, 6), 100u);
}

TEST(a_full_event_fifo_loses_events_never_replies)
{
    // Nobody reads EVT: each load's reply and its XFERDONE fill it. Once the FIFO can't take an XFERDONE it is
    // lost (counted), while every reply still comes, a command waiting in CMD until its reply has room. Then 20
    // more commands, and the host reads everything: one reply each, in order, and LOSTCLR's count right. (A real
    // card's event FIFO overflows so under a chatty stream.)
    Card c;
    std::vector<uint8_t> issued;
    unsigned completed = 0;
    for (uint8_t i = 0; i < 250; ++i)
    {
        c.send(SoundFw::SAMPLE_LOAD, i, sample_load(i, 16));
        issued.push_back(i);
        c.fw.advance(2);
        if (!c.fw.xfer_open())
            break; // its reply has no room: it waits
        c.data(noise(16, i));
        ++completed;
        c.fw.advance(2);
    }
    CHECK(c.fw.evlost());
    CHECK(completed > 100);
    for (uint8_t i = 0; i < 20; ++i)
    {
        c.send(SoundFw::NOP, static_cast<uint8_t>(250 + i % 5));
        issued.push_back(static_cast<uint8_t>(250 + i % 5));
    }
    std::vector<uint8_t> replies;
    unsigned done = 0;
    for (int round = 0; round < 50; ++round)
    {
        for (const Event &e : c.events())
            if (e.kind == SoundFw::EV_REPLY)
                replies.push_back(e.tag());
            else if (e.kind == SoundFw::EV_XFERDONE)
                ++done;
        if (c.fw.xfer_open()) // the waiting load opened its transfer now
        {
            c.data(noise(16, 99));
            ++completed;
        }
        c.fw.advance(2);
    }
    std::printf("  %u loads done, %u XFERDONEs read, %u lost; %zu replies of %zu commands\n", completed, done,
                c.fw.events_lost(), replies.size(), issued.size());
    CHECK(replies == issued);
    CHECK(c.fw.events_lost() > 0);
    CHECK_EQ(done + c.fw.events_lost(), completed);
    const uint32_t lost = c.fw.events_lost();
    c.send(SoundFw::LOSTCLR, 9);
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].body[2] | ev[0].body[3] << 8, static_cast<int>(lost));
    CHECK(!c.fw.evlost());
}

TEST(a_record_with_the_wrong_n_is_refused_at_the_door)
{
    Card c;
    c.send_raw(SoundFw::NOP, 1, 1, {0x55}); // NOP takes none
    CHECK(c.fw.err());
    CHECK_EQ(c.fw.error(), SoundFw::E_PARAM);
    c.send(SoundFw::TIME, 2); // the next record parses
    CHECK(!c.fw.err());
    c.send(0x1F, 3); // no such command
    CHECK_EQ(c.fw.error(), SoundFw::E_UNKNOWN);
    c.send(SoundFw::NOP, 4);
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 4);
    CHECK_EQ(ev[0].tag(), 1);
    CHECK_EQ(ev[0].status(), SoundFw::E_PARAM);
    CHECK_EQ(ev[1].tag(), 2);
    CHECK_EQ(ev[1].status(), 0);
    CHECK_EQ(ev[1].body.size(), 6u);
    CHECK_EQ(ev[2].tag(), 3);
    CHECK_EQ(ev[2].status(), SoundFw::E_UNKNOWN);
    CHECK_EQ(ev[3].tag(), 4);
    CHECK_EQ(ev[3].status(), 0);
    CHECK(!c.fw.err());
}

namespace
{
    /// Events (loads' XFERDONEs) into EVT, nobody reading, until a load's reply can't fit: the FIFO then within a
    /// few bytes of full. @return its free bytes
    size_t flood(Card &c, uint8_t first_slot = 0)
    {
        for (unsigned i = 0; i < 250; ++i)
        {
            const uint8_t slot = static_cast<uint8_t>(first_slot + i);
            c.send(SoundFw::SAMPLE_LOAD, 0xF0, sample_load(slot, 16));
            c.fw.advance(2);
            if (!c.fw.xfer_open())
                break; // its reply waits for room
            c.data(noise(16, slot));
            c.fw.advance(2);
        }
        return SoundFw::EVT_BYTES - c.fw.evt_len();
    }
    /// EVT read a byte at a time, the card advanced between, to empty: every event of it, parsed.
    std::vector<Event> trickle(Card &c, std::vector<uint8_t> bytes = {})
    {
        for (int guard = 0; guard < 100000 && (c.fw.evt_len() || bytes.size() < 2); ++guard)
        {
            if (c.fw.evt_len())
                bytes.push_back(c.fw.evt_read());
            c.fw.advance(1);
            if (!c.fw.evt_len() && guard > 20)
            {
                c.fw.advance(20);
                if (!c.fw.evt_len())
                    break;
            }
        }
        std::vector<Event> out;
        for (size_t i = 0; i + 1 < bytes.size();)
        {
            Event e;
            e.kind = bytes[i];
            const size_t n = bytes[i + 1];
            e.body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(i + 2),
                          bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), i + 2 + n)));
            out.push_back(e);
            i += 2 + n;
        }
        return out;
    }
} // namespace

TEST(a_reply_keeps_its_room_with_the_fifo_nearly_full)
{
    // Each command that answers with results, sent with EVT within a few bytes of full: its reply comes (never
    // dropped), and its size is the room the firmware keeps for it. And a command whose own events come before
    // its reply (PLAY stealing a voice: VOICE_STOLEN) still gets its reply: the event, not the reply, is lost.
    Card c;
    // What the commands name: a tune in slot 0, a sample in slot 200 (CHECKSUM), a SID effect 1.
    std::vector<uint8_t> tune(0x7C, 0);
    tune[0] = 'P', tune[1] = 'S', tune[2] = 'I', tune[3] = 'D', tune[5] = 2, tune[7] = 0x7C;
    tune[0x08] = 0x10, tune[0x0A] = 0x10, tune[0x0F] = 1, tune[0x11] = 1, tune[0x77] = 0x14;
    tune.push_back(0x60);
    tune.push_back(0x60);
    c.send(SoundFw::TUNE_LOAD, 1, {0, static_cast<uint8_t>(tune.size()), 0, 0});
    c.fw.advance(2);
    c.data(tune);
    c.send(SoundFw::SAMPLE_LOAD, 2, sample_load(200, 64));
    c.fw.advance(2);
    c.data(noise(64, 3));
    c.send(SoundFw::EFFECT_DEF, 3, {1, 0, 1, 0xFF, 0x10, 0x27, 0x21, 0x00, 0x1B, 0x00, 0, 8, 0x00, 0xF0, 0, 0, 0});
    c.fw.advance(4);
    for (const Event &e : c.events())
        if (e.kind == SoundFw::EV_XFERDONE)
            CHECK_EQ(e.body.at(6), 0); // the tune and the sample taken
    struct Ask
    {
        uint8_t cmd;
        std::vector<uint8_t> p;
        size_t size; ///< the reply's bytes, kind and n with them
    };
    const std::vector<Ask> asks = {
        {SoundFw::IDENTIFY, {}, 4 + 32},      {SoundFw::TIME, {}, 4 + 4},          {SoundFw::CHECKSUM, {SoundFw::SAMPLE_LOAD, 200}, 4 + 8},
        {SoundFw::XFERABORT, {}, 4 + 3},      {SoundFw::TUNE_STATUS, {}, 4 + 16}, // (the flood's last load opens a transfer first)  {SoundFw::TUNE_INFO, {0}, 4 + 104},
        {SoundFw::PLAY, {1, 255, 128, 0, 0, 0, 1}, 4 + 1}, {SoundFw::MUSIC_STATUS, {}, 4 + 24}, {SoundFw::STREAM_STATUS, {0}, 4 + 21},
        {SoundFw::VIS, {}, 4 + 98},           {SoundFw::SIDREGS, {}, 4 + 50},      {SoundFw::LEVELS, {}, 4 + 10},
        {SoundFw::SELFTEST, {0x0F}, 4 + 2},   {SoundFw::LOSTCLR, {}, 4 + 2},
    };
    for (const Ask &a : asks)
    {
        const size_t free = flood(c, 10);
        if (free >= a.size)
            std::printf("  command $%02X: %zu bytes free after the flood\n", a.cmd, free);
        CHECK(free < a.size); // too few for its reply: it must wait
        c.send(a.cmd, 0x77, a.p);
        bool seen = false;
        for (const Event &e : trickle(c))
            if (e.kind == SoundFw::EV_REPLY && e.body.at(0) == 0x77)
            {
                seen = true;
                CHECK_EQ(e.body.size() + 2, a.size);
                CHECK(e.body.size() + 2 <= SoundFw::reply_room_for(a.cmd));
            }
        if (!seen)
            std::printf("  command $%02X: no reply\n", a.cmd);
        CHECK(seen);
    }
    // PLAY that steals: six SID effects at priority 0, EVT nearly full, then one at priority 1.
    Card s;
    s.send(SoundFw::EFFECT_DEF, 3, {1, 0, 1, 0xFF, 0x10, 0x27, 0x21, 0x00, 0x1B, 0x00, 0, 8, 0x00, 0xF0, 0, 0, 0});
    for (int i = 0; i < 6; ++i)
        s.send(SoundFw::PLAY, 4, {1, 255, 128, 0, 0, 0, 1});
    s.fw.advance(4);
    s.events();
    flood(s, 10);
    // 12 bytes free, as the review found it: room for PLAY's reply (5), or for VOICE_STOLEN (9), not both.
    std::vector<uint8_t> read;
    while (SoundFw::EVT_BYTES - s.fw.evt_len() < 12)
        read.push_back(s.fw.evt_read());
    REQUIRE(SoundFw::EVT_BYTES - s.fw.evt_len() == 12);
    s.send(SoundFw::PLAY, 0x78, {1, 255, 128, 1, 0, 0, 1});
    unsigned reply = 0, stolen = 0;
    for (const Event &e : trickle(s, read))
    {
        reply += e.kind == SoundFw::EV_REPLY && e.body.at(0) == 0x78;
        stolen += e.kind == SoundFw::EV_VOICE_STOLEN;
    }
    std::printf("  PLAY's reply %u, VOICE_STOLEN %u (lost %u)\n", reply, stolen, s.fw.events_lost());
    CHECK_EQ(reply, 1u);
}

TEST(a_record_too_short_is_refused_at_the_door_too)
{
    Card c;
    c.send_raw(SoundFw::TUNE_PLAY, 8, 1, std::vector<uint8_t>(8, 0)); // TUNE_PLAY takes 9
    CHECK_EQ(c.fw.error(), SoundFw::E_PARAM);
    CHECK(c.fw.err());
    c.send_raw(SoundFw::SAMPLE_LOAD, 12, 2, std::vector<uint8_t>(12, 1)); // SAMPLE_LOAD takes 13
    CHECK_EQ(c.fw.error(), SoundFw::E_PARAM);
    c.send(SoundFw::TIME, 3); // the next parses
    CHECK(!c.fw.err());
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 3);
    CHECK_EQ(ev[0].status(), SoundFw::E_PARAM);
    CHECK_EQ(ev[1].status(), SoundFw::E_PARAM);
    CHECK_EQ(ev[2].status(), 0);
    CHECK(!c.fw.xfer_open()); // the short SAMPLE_LOAD opened nothing
}

TEST(events_masked_off_are_neither_sent_nor_lost)
{
    // EVENTS without group 1 (the transfers): a load's XFERDONE isn't sent, and isn't counted lost; the group on
    // again, the next load's comes. REPLY is always sent.
    Card c;
    c.send(SoundFw::EVENTS, 1, {static_cast<uint8_t>(0xFF & ~0x02)});
    c.send(SoundFw::SAMPLE_LOAD, 2, sample_load(0, 4));
    c.fw.advance(2);
    c.data(noise(4, 1));
    c.fw.advance(2);
    auto ev = c.events();
    REQUIRE(ev.size() == 2);
    CHECK_EQ(ev[0].kind, SoundFw::EV_REPLY);
    CHECK_EQ(ev[1].kind, SoundFw::EV_REPLY);
    CHECK_EQ(c.fw.events_lost(), 0u);
    CHECK(!c.fw.evlost());
    c.send(SoundFw::EVENTS, 3, {0xFF});
    c.send(SoundFw::SAMPLE_LOAD, 4, sample_load(1, 4));
    c.fw.advance(2);
    c.data(noise(4, 2));
    c.fw.advance(2);
    ev = c.events();
    REQUIRE(ev.size() == 3);
    CHECK_EQ(ev[2].kind, SoundFw::EV_XFERDONE);
}

TEST(xferabort_runs_at_once_behind_a_record_waiting_on_its_load)
{
    // TUNE_LOAD, its TUNE_PLAY sent at once (it waits for the load), then XFERABORT: the abort runs at once, ends
    // the transfer, and the play behind it is answered (no tune: E_SLOT).
    Card c;
    c.send(SoundFw::TUNE_LOAD, 1, {0, 200, 0, 0});
    c.send(SoundFw::TUNE_PLAY, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0});
    c.send(SoundFw::XFERABORT, 3);
    c.fw.advance(4);
    CHECK(!c.fw.xfer_open());
    const auto ev = c.events();
    REQUIRE(ev.size() == 3);
    CHECK_EQ(ev[0].tag(), 1);
    CHECK_EQ(ev[1].tag(), 3); // the abort, ahead of the play that waited
    CHECK_EQ(ev[1].status(), 0);
    CHECK_EQ(ev[2].tag(), 2);
    CHECK_EQ(ev[2].status(), SoundFw::E_SLOT);
}

namespace
{
    /// A PSID of `size` bytes (code of zeros after its RTS), loaded into tune slot `slot`. @return XFERDONE's status
    uint8_t load_tune(Card &c, uint8_t slot, size_t size)
    {
        std::vector<uint8_t> t(0x7C, 0);
        t[0] = 'P', t[1] = 'S', t[2] = 'I', t[3] = 'D', t[5] = 2, t[7] = 0x7C;
        t[0x08] = 0x10, t[0x0A] = 0x10, t[0x0F] = 1, t[0x11] = 1, t[0x77] = 0x14;
        t.resize(size, 0);
        t[0x7C] = 0x60;
        c.send(SoundFw::TUNE_LOAD, 0x31, {slot, static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size >> 16)});
        c.fw.advance(2);
        c.data(t);
        c.fw.advance(2);
        uint8_t status = 0xFF;
        for (const Event &e : c.events())
            if (e.kind == SoundFw::EV_XFERDONE)
                status = e.body.at(6);
        return status;
    }
    /// A sample of `len` into `slot`, whole. @return its reply's status (then XFERDONE's, if taken)
    uint8_t load_sample(Card &c, uint8_t slot, uint32_t len)
    {
        c.send(SoundFw::SAMPLE_LOAD, 0x32, sample_load(slot, len));
        c.fw.advance(2);
        uint8_t status = 0xFF;
        for (const Event &e : c.events())
            if (e.kind == SoundFw::EV_REPLY)
                status = e.status();
        if (status)
            return status;
        c.data(std::vector<uint8_t>(len, 0x55));
        c.fw.advance(2);
        c.events();
        return 0;
    }
    uint8_t ask_status(Card &c, uint8_t cmd, const std::vector<uint8_t> &p = {})
    {
        c.send(cmd, 0x33, p);
        c.fw.advance(3);
        for (const Event &e : c.events())
            if (e.kind == SoundFw::EV_REPLY && e.tag() == 0x33)
                return e.status();
        return 0xFF;
    }
} // namespace

TEST(a_refused_load_leaves_the_slot_as_it_was)
{
    // E_XFER: a tune loaded and playing, a sample's transfer open; TUNE_LOAD of the tune's slot is refused, and the
    // tune still plays, its header still there. SAMPLE_LOAD of a loaded sample's slot, refused the same, keeps it.
    Card c;
    CHECK_EQ(load_tune(c, 0, 200), 0);
    CHECK_EQ(load_sample(c, 5, 1000), 0);
    CHECK_EQ(ask_status(c, SoundFw::TUNE_PLAY, {0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
    const uint8_t playing = c.fw.tune_state();
    CHECK(playing != SoundFw::T_STOPPED);
    c.send(SoundFw::SAMPLE_LOAD, 0x34, sample_load(9, 100));
    c.fw.advance(2);
    c.events();
    REQUIRE(c.fw.xfer_open());
    CHECK_EQ(ask_status(c, SoundFw::TUNE_LOAD, {0, 200, 0, 0}), SoundFw::E_XFER);
    CHECK_EQ(c.fw.tune_state(), playing);
    CHECK_EQ(ask_status(c, SoundFw::TUNE_INFO, {0}), 0);
    CHECK_EQ(ask_status(c, SoundFw::SAMPLE_LOAD, sample_load(5, 500)), SoundFw::E_XFER);
    CHECK_EQ(ask_status(c, SoundFw::CHECKSUM, {SoundFw::SAMPLE_LOAD, 5}), 0);
    CHECK_EQ(ask_status(c, SoundFw::EFFECT_DEF, {7, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
    CHECK_EQ(ask_status(c, SoundFw::PLAY, {7, 255, 128, 0, 0, 0, 2}), 0); // the sample still plays
    // E_MEMORY: a 2 MB store, samples of 1 MB and 900 KB; slot 1 again at 1.2 MB doesn't fit even with its own run
    // free, so it is refused and keeps its sample; at 1,000 KB it fits in its own room and is taken.
    Card m(32);
    CHECK_EQ(load_sample(m, 0, 1u << 20), 0);
    CHECK_EQ(load_sample(m, 1, 900u << 10), 0);
    CHECK_EQ(load_sample(m, 1, 1200u << 10), SoundFw::E_MEMORY);
    CHECK_EQ(ask_status(m, SoundFw::CHECKSUM, {SoundFw::SAMPLE_LOAD, 1}), 0);
    CHECK_EQ(load_sample(m, 1, 1000u << 10), 0);
}

TEST(every_way_of_freeing_gives_the_store_back)
{
    // A 2 MB store filled, then freed by each path: the store's free bytes back, and a load of the room taken.
    const size_t all = 32u * 65536u;
    {
        Card c(32); // BANK_FREE 1, the samples; 0, the effects, leaves them; 2, both
        CHECK_EQ(load_sample(c, 0, static_cast<uint32_t>(all / 2)), 0);
        CHECK_EQ(load_sample(c, 1, static_cast<uint32_t>(all / 2)), 0);
        CHECK_EQ(c.fw.store_free(), 0u);
        CHECK_EQ(ask_status(c, SoundFw::BANK_FREE, {0}), 0);
        CHECK_EQ(c.fw.store_free(), 0u);
        CHECK_EQ(ask_status(c, SoundFw::BANK_FREE, {1}), 0);
        CHECK_EQ(c.fw.store_free(), all);
        CHECK_EQ(load_sample(c, 2, static_cast<uint32_t>(all)), 0);
        CHECK_EQ(ask_status(c, SoundFw::BANK_FREE, {2}), 0);
        CHECK_EQ(c.fw.store_free(), all);
    }
    {
        Card c(32); // TUNE_FREE
        CHECK_EQ(load_tune(c, 0, 30000), 0);
        CHECK_EQ(c.fw.store_free(), all - 30000);
        CHECK_EQ(ask_status(c, SoundFw::TUNE_FREE, {0}), 0);
        CHECK_EQ(c.fw.store_free(), all);
        CHECK_EQ(load_sample(c, 0, static_cast<uint32_t>(all)), 0);
    }
    {
        Card c(32); // FREEALL
        CHECK_EQ(load_tune(c, 1, 30000), 0);
        CHECK_EQ(load_sample(c, 3, 100000), 0);
        CHECK_EQ(ask_status(c, SoundFw::FREEALL), 0);
        CHECK_EQ(c.fw.store_free(), all);
    }
    {
        Card c(32); // a block freed when done (MUSIC_PLAY's bit 1): one 10 ms note on the tone generator
        std::vector<uint8_t> blk{1, 1, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 255, 128, 0x00, 69};
        for (const uint32_t x : {10000u, 5000u})
            for (int i = 0; i < 4; ++i)
                blk.push_back(static_cast<uint8_t>(x >> (8 * i)));
        c.send(SoundFw::MUSIC_LOAD, 0x35, {0, static_cast<uint8_t>(blk.size()), 0, 0});
        c.fw.advance(2);
        c.data(blk);
        c.fw.advance(2);
        CHECK_EQ(c.fw.store_free(), all - blk.size());
        CHECK_EQ(ask_status(c, SoundFw::MUSIC_PLAY, {0, 2}), 0);
        c.fw.advance(30 * SoundFw::SAMPLES_PER_MS);
        CHECK_EQ(c.fw.store_free(), all);
    }
    {
        Card c(32); // STREAM_CLOSE
        CHECK_EQ(ask_status(c, SoundFw::STREAM_OPEN, {0, 0, 0x80, 0xBB, 0, 0, 0, 0, 0, 0}), 0);
        CHECK(c.fw.store_free() < all);
        CHECK_EQ(ask_status(c, SoundFw::STREAM_CLOSE, {0}), 0);
        CHECK_EQ(c.fw.store_free(), all);
    }
    {
        Card c(32); // a full-size slot loaded again in place; a middle object's hole filled to the byte
        CHECK_EQ(load_sample(c, 0, static_cast<uint32_t>(all)), 0);
        CHECK_EQ(load_sample(c, 0, static_cast<uint32_t>(all)), 0);
        CHECK_EQ(ask_status(c, SoundFw::BANK_FREE, {1}), 0);
        CHECK_EQ(load_tune(c, 0, 30000), 0);
        CHECK_EQ(load_tune(c, 1, 30000), 0);
        CHECK_EQ(load_sample(c, 0, static_cast<uint32_t>(all - 60000)), 0);
        CHECK_EQ(c.fw.store_free(), 0u);
        CHECK_EQ(ask_status(c, SoundFw::TUNE_FREE, {0}), 0);
        CHECK_EQ(load_tune(c, 2, 30000), 0); // exactly the hole
        CHECK_EQ(c.fw.store_free(), 0u);
    }
}

TEST(the_store_refuses_what_doesn_t_fit_at_each_size)
{
    for (const unsigned units : {32u, 64u, 128u})
    {
        Card c(units);
        const uint32_t size = units * 65536u;
        CHECK_EQ(c.fw.memsize(), units);
        c.send(SoundFw::SAMPLE_LOAD, 1, sample_load(0, size + 1)); // a byte too many
        c.send(SoundFw::SAMPLE_LOAD, 2, sample_load(0, size));     // all of it
        c.send(SoundFw::XFERABORT, 3);
        c.send(SoundFw::SAMPLE_LOAD, 4, sample_load(1, size / 2)); // half, twice: then nothing more
        c.fw.advance(2);
        c.data(std::vector<uint8_t>(size / 2, 0x11));
        c.fw.advance(2);
        c.send(SoundFw::SAMPLE_LOAD, 5, sample_load(2, size / 2));
        c.fw.advance(2);
        c.data(std::vector<uint8_t>(size / 2, 0x22));
        c.fw.advance(2);
        c.send(SoundFw::SAMPLE_LOAD, 6, sample_load(3, 1));
        c.fw.advance(2);
        std::vector<uint8_t> status;
        for (const Event &e : c.events())
            if (e.kind == SoundFw::EV_REPLY)
                status.push_back(e.status());
        std::printf("  %u KB:", size / 1024);
        for (const uint8_t s : status)
            std::printf(" %u", s);
        std::printf("\n");
        CHECK(status == (std::vector<uint8_t>{SoundFw::E_MEMORY, 0, 0, 0, 0, SoundFw::E_MEMORY}));
        CHECK_EQ(c.fw.store_free(), 0u);
    }
}

TEST(checksum_is_crc_32_of_what_came)
{
    CHECK_EQ(Crc32::of(reinterpret_cast<const uint8_t *>("123456789"), 9), 0xCBF43926u); // the check value
    Card c;
    for (const size_t len : {1u, 255u, 4096u, 70000u})
    {
        const std::vector<uint8_t> bytes = noise(len, static_cast<uint32_t>(len));
        c.send(SoundFw::SAMPLE_LOAD, 1, sample_load(9, static_cast<uint32_t>(len)));
        c.fw.advance(2);
        c.data(bytes);
        c.send(SoundFw::CHECKSUM, 2, {SoundFw::SAMPLE_LOAD, 9});
        c.fw.advance(3);
        const auto ev = c.events();
        REQUIRE(ev.size() == 3);
        CHECK_EQ(ev[2].status(), 0);
        CHECK_EQ(le32(ev[2].body, 2), crc32(bytes));
        CHECK_EQ(le32(ev[2].body, 6), len);
    }
    c.send(SoundFw::CHECKSUM, 3, {SoundFw::SAMPLE_LOAD, 10}); // nothing there
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].status(), SoundFw::E_SLOT);
}

TEST(reset_empties_both_fifos_and_abandons_a_transfer_but_keeps_the_store)
{
    Card c;
    c.send(SoundFw::SAMPLE_LOAD, 1, sample_load(0, 10));
    c.fw.advance(2);
    c.data(noise(10, 3));
    c.fw.advance(2);
    c.send(SoundFw::SAMPLE_LOAD, 2, sample_load(1, 10));
    c.fw.advance(2);
    CHECK(c.fw.xfer_open());
    c.fw.cmd_write(SoundFw::NOP); // a half-written record
    const unsigned before = c.port.silenced;
    c.fw.reset();
    CHECK(c.port.silenced > before);
    CHECK(!c.fw.xfer_open());
    CHECK_EQ(c.fw.evt_len(), 0u);
    CHECK_EQ(c.fw.cmd_free(), SoundFw::CMD_BYTES);
    CHECK(c.fw.ready());
    c.send(SoundFw::CHECKSUM, 3, {SoundFw::SAMPLE_LOAD, 0}); // kept; the record parses afresh
    c.send(SoundFw::CHECKSUM, 4, {SoundFw::SAMPLE_LOAD, 1}); // abandoned
    c.fw.advance(2);
    const auto ev = c.events();
    REQUIRE(ev.size() == 2);
    CHECK_EQ(ev[0].status(), 0);
    CHECK_EQ(ev[1].status(), SoundFw::E_SLOT);
    c.fw.restart(); // FWRESTART: the store emptied, READY again later
    CHECK(!c.fw.ready());
    c.fw.advance(SoundFw::STARTUP_MS * SoundFw::SAMPLES_PER_MS);
    c.send(SoundFw::CHECKSUM, 5, {SoundFw::SAMPLE_LOAD, 0});
    c.fw.advance(2);
    const auto ev2 = c.events();
    REQUIRE(ev2.size() == 1);
    CHECK_EQ(ev2[0].status(), SoundFw::E_SLOT);
}

namespace
{
    // --- tunes, on the real front end ---------------------------------------------------------------------

    /// The card at PAL's SID clock as the host's (a host cycle a SID clock), powered on and up.
    struct Dev
    {
        SoundCard3 card;
        Dev()
        {
            card.set_cpu_hz(SoundCard::PAL_HZ);
            ms(SoundFw::STARTUP_MS + 1);
            card.write(SoundCard3::ISR, 0xFF);
        }
        void ms(unsigned n) { card.tick(static_cast<uint64_t>(n) * SoundCard::PAL_HZ / 1000); }
        void send(uint8_t cmd, uint8_t tag, const std::vector<uint8_t> &params = {})
        {
            card.write(SoundCard3::CMD, cmd);
            card.write(SoundCard3::CMD, static_cast<uint8_t>(params.size()));
            card.write(SoundCard3::CMD, tag);
            for (const uint8_t b : params)
                card.write(SoundCard3::CMD, b);
        }
        std::vector<Event> events()
        {
            std::vector<Event> out;
            while (card.peek(SoundCard3::EVTLEN))
            {
                Event e;
                e.kind = card.read(SoundCard3::EVT);
                const uint8_t n = card.read(SoundCard3::EVT);
                for (unsigned i = 0; i < n; ++i)
                    e.body.push_back(card.read(SoundCard3::EVT));
                out.push_back(e);
            }
            return out;
        }
        /// A tune into `slot`: TUNE_LOAD, its bytes by DATA, XFERDONE. @return XFERDONE's body
        std::vector<uint8_t> load(uint8_t slot, const std::vector<uint8_t> &file)
        {
            send(SoundFw::TUNE_LOAD, 0x40, {slot, static_cast<uint8_t>(file.size()), static_cast<uint8_t>(file.size() >> 8),
                                            static_cast<uint8_t>(file.size() >> 16)});
            ms(1);
            for (const uint8_t b : file)
                card.write(SoundCard3::DATA, b);
            ms(1);
            for (const Event &e : events())
                if (e.kind == SoundFw::EV_XFERDONE)
                    return e.body;
            return {};
        }
        /// TUNE_PLAY: song, flags, length and fade in ms. @return its reply's status
        uint8_t play(uint8_t slot, uint8_t song, uint8_t flags = 0, uint32_t length = 0, uint16_t fade = 0)
        {
            send(SoundFw::TUNE_PLAY, 0x41, {slot, song, flags, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
                                            static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 24),
                                            static_cast<uint8_t>(fade), static_cast<uint8_t>(fade >> 8)});
            ms(1);
            for (const Event &e : events())
                if (e.kind == SoundFw::EV_REPLY && e.tag() == 0x41)
                    return e.status();
            return 0xFF;
        }
        /// A command's reply. @return its body (tag, status, results)
        std::vector<uint8_t> ask(uint8_t cmd, const std::vector<uint8_t> &params = {})
        {
            send(cmd, 0x42, params);
            ms(1);
            for (const Event &e : events())
                if (e.kind == SoundFw::EV_REPLY && e.tag() == 0x42)
                    return e.body;
            return {};
        }
    };

    /// A PSID: load and init $1000, play $1000 + `play_at` (0: none), PAL, VBI; `songs`; a second SID at `sid2`.
    std::vector<uint8_t> psid(std::vector<uint8_t> code, uint16_t play_at, uint16_t songs = 1, uint16_t sid2 = 0,
                              uint8_t flags = 0x14)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 3, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10;
        if (play_at)
            b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = static_cast<uint8_t>(songs), b[0x11] = 1, b[0x77] = flags;
        const char name[] = "A TEST TUNE";
        for (size_t i = 0; i < sizeof name - 1; ++i)
            b[0x16 + i] = static_cast<uint8_t>(name[i]);
        if (sid2)
            b[0x7A] = static_cast<uint8_t>(sid2 >> 4);
        b.insert(b.end(), code.begin(), code.end());
        return b;
    }
    /// init's start: volume up, voice 1 a gated sawtooth (sustain 15).
    const std::vector<uint8_t> SAW = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4,
                                      0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, 0x21, 0x8D, 0x04, 0xD4};
    std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    std::vector<Event> of_kind(const std::vector<Event> &ev, uint8_t kind)
    {
        std::vector<Event> out;
        for (const Event &e : ev)
            if (e.kind == kind)
                out.push_back(e);
        return out;
    }
    uint32_t le16(const std::vector<uint8_t> &v, size_t at) { return static_cast<uint32_t>(v.at(at) | v.at(at + 1) << 8); }
} // namespace

TEST(a_tune_plays_its_song_and_its_play_calls_are_counted)
{
    // init: STA $C000 (the song, 0-based, in A), the saw, RTS; play ($1018): INC $C001, RTS.
    Dev d;
    const auto file = psid(std::vector<uint8_t>{0x8D, 0x00, 0xC0} + SAW + std::vector<uint8_t>{0x60, 0xEE, 0x01, 0xC0, 0x60}, 24, 3);
    const auto done = d.load(1, file);
    REQUIRE(done.size() == 7u + 7u);
    CHECK_EQ(done[5], SoundFw::TUNE_LOAD);
    CHECK_EQ(done[6], 0);
    CHECK_EQ(le16(done, 7), 3u);  // songs
    CHECK_EQ(le16(done, 9), 1u);  // the start song
    CHECK_EQ(done[11], 1);        // PAL
    CHECK_EQ(done[12], 1);        // one SID
    CHECK_EQ(done[13], 0);        // a PSID, VBI
    CHECK_EQ(d.play(1, 2), 0);
    d.ms(1000);
    const auto started = of_kind(d.events(), SoundFw::EV_TUNE_STARTED);
    REQUIRE(started.size() == 1);
    CHECK_EQ(started[0].body[0], 1);           // the slot
    CHECK_EQ(le16(started[0].body, 5), 2u);    // song 2
    CHECK_EQ(d.card.chips().ram()[0xC000], 1); // init had A = 1
    const auto s = d.ask(SoundFw::TUNE_STATUS);
    REQUIRE(s.size() == 2 + 16u);
    const uint32_t played = le32(s, 6), calls = le32(s, 10);
    std::printf("  state %u, slot %u, song %u, %u ms played, %u play calls, %u cycles in the last, %u ms since\n", s[2],
                s[3], le16(s, 4), played, calls, le16(s, 14), le16(s, 16));
    CHECK_EQ(s[2], SoundFw::T_PLAYING);
    CHECK_EQ(s[3], 1);
    CHECK_EQ(le16(s, 4), 2u);
    CHECK(played >= 995 && played <= 1005);
    CHECK(calls >= 49 && calls <= 51);
    CHECK_EQ(d.card.chips().ram()[0xC001], static_cast<uint8_t>(calls));
    CHECK(le16(s, 14) > 0 && le16(s, 14) < 200);
    const auto info = d.ask(SoundFw::TUNE_INFO, {1});
    REQUIRE(info.size() == 2 + 104u);
    CHECK_EQ(le16(info, 2), 3u);
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&info[10])), std::string("A TEST TUNE"));
    CHECK_EQ(d.ask(SoundFw::TUNE_STOP)[1], 0);
    CHECK_EQ(d.card.firmware().tune_state(), SoundFw::T_STOPPED);
}

TEST(pause_holds_the_6510_s_cycles_and_silences_its_chips)
{
    Dev d;
    d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21));
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(200);
    CHECK_EQ(d.ask(SoundFw::TUNE_PAUSE)[1], 0);
    const uint64_t c0 = d.card.chips().cpu().cycles();
    const uint32_t ms0 = le32(d.ask(SoundFw::TUNE_STATUS), 6);
    d.ms(300);
    CHECK_EQ(d.card.chips().cpu().cycles(), c0); // held where it is
    CHECK_EQ(d.card.chips().chip(0).regs()[0x18] & 0x0F, 0);
    const auto s = d.ask(SoundFw::TUNE_STATUS);
    CHECK_EQ(s[2], SoundFw::T_PAUSED);
    CHECK_EQ(le32(s, 6), ms0); // paused time out
    CHECK_EQ(d.ask(SoundFw::TUNE_PAUSE)[1], SoundFw::E_STATE);
    CHECK_EQ(d.ask(SoundFw::TUNE_RESUME)[1], 0);
    d.ms(100);
    CHECK(d.card.chips().cpu().cycles() > c0);
    CHECK_EQ(d.card.chips().chip(0).regs()[0x18] & 0x0F, 0x0F);
    const auto s2 = d.ask(SoundFw::TUNE_STATUS);
    CHECK_EQ(s2[2], SoundFw::T_PLAYING);
    CHECK(le32(s2, 6) > ms0 + 90);
    CHECK_EQ(d.ask(SoundFw::TUNE_RESUME)[1], SoundFw::E_STATE);
}

TEST(a_stalled_tune_says_so_after_2_s)
{
    // Never a play call (init turns the frames off: $DE04 = 0): stalled 10 s from the start.
    Dev d;
    d.load(0, psid(SAW + std::vector<uint8_t>{0xA9, 0x00, 0x8D, 0x04, 0xDE, 0x60, 0x60}, 26));
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(9990);
    CHECK(of_kind(d.events(), SoundFw::EV_TUNE_STALL).empty());
    d.ms(20);
    auto stall = of_kind(d.events(), SoundFw::EV_TUNE_STALL);
    REQUIRE(stall.size() == 1);
    CHECK_EQ(le32(stall[0].body, 5), 10000u);
    CHECK_EQ(d.card.firmware().tune_state(), SoundFw::T_STALLED);
    // 25 play calls, then play turns the frames off: stalled 2 s after the last call.
    // play: INC $C001, LDA $C001, CMP #25, BNE out, LDA #0, STA $DE04, out: RTS.
    Dev e;
    e.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0xEE, 0x01, 0xC0, 0xAD, 0x01, 0xC0, 0xC9, 0x19, 0xD0, 0x05, 0xA9, 0x00, 0x8D, 0x04, 0xDE, 0x60}, 21));
    CHECK_EQ(e.play(0, 0), 0);
    e.ms(600);
    CHECK_EQ(e.card.chips().ram()[0xC001], 0x19);
    const auto s = e.ask(SoundFw::TUNE_STATUS);
    const uint32_t last = e.card.firmware().ms() - le16(s, 16); // the card's ms at the last call
    e.ms(last + 1995 - e.card.firmware().ms());
    CHECK(of_kind(e.events(), SoundFw::EV_TUNE_STALL).empty());
    e.ms(10);
    stall = of_kind(e.events(), SoundFw::EV_TUNE_STALL);
    REQUIRE(stall.size() == 1);
    CHECK_EQ(le32(stall[0].body, 5), 2000u);
    const uint32_t at = le32(stall[0].body, 1); // its stamp
    std::printf("  the last call at card ms %u; the stall stamped at %u\n", last, at);
    CHECK(at - last >= 1999 && at - last <= 2001);
    CHECK_EQ(e.card.firmware().tune_state(), SoundFw::T_STALLED);
}

TEST(a_jam_stops_the_tune_with_e_jam)
{
    Dev d;
    d.load(0, psid(std::vector<uint8_t>{0xEA, 0x02}, 0x10)); // init: NOP, JAM
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(10);
    const auto jam = of_kind(d.events(), SoundFw::EV_TUNE_JAM);
    REQUIRE(jam.size() == 1);
    std::printf("  jammed at $%04X\n", le16(jam[0].body, 5));
    CHECK(le16(jam[0].body, 5) >= 0x1001 && le16(jam[0].body, 5) <= 0x1003);
    CHECK_EQ(d.ask(SoundFw::TUNE_STATUS)[2], SoundFw::T_JAMMED);
    CHECK_EQ(d.card.firmware().tune_chips(), 0);
}

TEST(the_length_and_fade_end_it_once)
{
    Dev d;
    d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21));
    CHECK_EQ(d.play(0, 0, 0, 500, 100), 0);
    d.ms(1500);
    const auto ended = of_kind(d.events(), SoundFw::EV_TUNE_ENDED);
    REQUIRE(ended.size() == 1);
    CHECK_EQ(le32(ended[0].body, 5), 500u);
    CHECK_EQ(d.ask(SoundFw::TUNE_STATUS)[2], SoundFw::T_ENDED);
    const uint64_t c0 = d.card.chips().cpu().cycles();
    d.ms(100);
    CHECK_EQ(d.card.chips().cpu().cycles(), c0); // stopped
    CHECK(of_kind(d.events(), SoundFw::EV_TUNE_ENDED).empty());
    CHECK_EQ(d.card.firmware().tune_chips(), 0);
}

TEST(a_tune_takes_whole_chips_and_gives_them_back)
{
    Dev d;
    d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21));
    d.load(1, psid(SAW + std::vector<uint8_t>{0xA9, 0x0F, 0x8D, 0x38, 0xD4, 0x60, 0x60}, 26, 1, 0xD420)); // and $D438 on B
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(50);
    CHECK_EQ(d.card.firmware().tune_chips(), 1);
    CHECK_EQ(d.card.chips().sid(0)[0x18], 0x0F);
    CHECK_EQ(d.card.chips().sid(1)[0x18], 0x00); // a one-SID tune on SID A alone: no BOTH
    const auto regs = d.ask(SoundFw::SIDREGS);
    REQUIRE(regs.size() == 2 + 50u);
    CHECK_EQ(regs[2 + 0x18], 0x0F);
    CHECK_EQ(regs[2 + 4], 0x21);
    CHECK_EQ(regs[2 + 25 + 0x18], 0x00);
    CHECK_EQ(d.play(1, 0), 0); // the next tune: the first gives its chips back
    d.ms(50);
    CHECK_EQ(d.card.firmware().tune_chips(), 3);
    CHECK_EQ(d.card.chips().sid(1)[0x18], 0x0F);
    CHECK_EQ(d.ask(SoundFw::TUNE_STOP)[1], 0);
    CHECK_EQ(d.card.firmware().tune_chips(), 0);
    CHECK_EQ(d.card.chips().sid(0)[0x18], 0x00);
    CHECK_EQ(d.card.chips().sid(1)[0x18], 0x00);
    // Not a .sid: E_DATA in XFERDONE, and the slot stays empty.
    const auto bad = d.load(2, std::vector<uint8_t>(0x80, 0x55));
    REQUIRE(bad.size() >= 7u);
    CHECK_EQ(bad[6], SoundFw::E_DATA);
    CHECK_EQ(d.play(2, 0), SoundFw::E_SLOT);
}

TEST(a_one_sid_tune_s_read_of_d423_gives_sid_a_s_d403)
{
    // init: $08 to $D403 (voice 1's pulse width, high: write-only, read back as last written), $D423 read into
    // $C000, RTS. A one-SID tune's $D420-$D43F is SID A's mirror for its reads (SID B, unwritten, would read $00);
    // a two-SID tune with its second at $D420 reads its own SID B there.
    const std::vector<uint8_t> code = {0xA9, 0x08, 0x8D, 0x03, 0xD4, 0xAD, 0x23, 0xD4, 0x8D, 0x00, 0xC0, 0x60, 0x60};
    Dev one;
    one.load(0, psid(code, 12));
    CHECK_EQ(one.play(0, 0), 0);
    one.ms(5);
    CHECK_EQ(one.card.chips().ram()[0xC000], 0x08);
    CHECK_EQ(one.card.chips().sid(1)[0x03], 0x00); // SID B untouched: the read came from A
    Dev two;
    two.load(0, psid(code, 12, 1, 0xD420));
    CHECK_EQ(two.play(0, 0), 0);
    two.ms(5);
    CHECK_EQ(two.card.chips().ram()[0xC000], 0x00); // its own SID B, never written
}

namespace
{
    // --- the digital path -----------------------------------------------------------------------------------

    /// The frequency of the left channel's rising zero crossings, each placed between its samples.
    double pitch(const std::vector<int16_t> &tap)
    {
        double first = -1, last = -1;
        unsigned n = 0;
        for (size_t i = 2; i + 1 < tap.size(); i += 2)
        {
            const int a = tap[i - 2], b = tap[i];
            if (a < 0 && b >= 0)
            {
                const double at = static_cast<double>(i / 2 - 1) + static_cast<double>(-a) / static_cast<double>(b - a);
                if (first < 0)
                    first = at;
                last = at;
                ++n;
            }
        }
        return n > 1 ? (n - 1) * static_cast<double>(SoundFw::RATE) / (last - first) : 0;
    }
    double rms(const std::vector<int16_t> &tap, unsigned ch)
    {
        double s = 0;
        for (size_t i = ch; i < tap.size(); i += 2)
            s += static_cast<double>(tap[i]) * tap[i];
        return std::sqrt(s / static_cast<double>(tap.size() / 2));
    }
    /// A one-SID tune holding voice 1 at SID frequency `f` ($D400-$D401) with `wave` (control's gate set).
    std::vector<uint8_t> tone_tune(uint16_t f, uint8_t wave)
    {
        return psid({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, static_cast<uint8_t>(f), 0x8D, 0x00, 0xD4, 0xA9,
                     static_cast<uint8_t>(f >> 8), 0x8D, 0x01, 0xD4, 0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, wave, 0x8D,
                     0x04, 0xD4, 0x60, 0x60},
                    26);
    }
} // namespace

TEST(the_beep_is_at_its_pitch_within_0_1_percent_over_a_tune_and_a_stream)
{
    // The beep at three pitches over a tune, the boot sound (the ES5503) and a stream playing; its pitch from
    // the beep's own tap, and the tune's SID writes the same as with no beep.
    for (const uint32_t hz : {440u, 1000u, 7040u})
    {
        Dev d, quiet;
        for (Dev *x : {&d, &quiet})
        {
            x->load(0, psid(SAW + std::vector<uint8_t>{0x60, 0xEE, 0x01, 0xD4, 0x60}, 21)); // play: INC $D401
            CHECK_EQ(x->play(0, 0), 0);
            CHECK_EQ(x->ask(SoundFw::BOOTSOUND)[1], 0);
            // A raw stream, 3 s of 22,050 Hz 8-bit mono, given whole.
            CHECK_EQ(x->ask(SoundFw::STREAM_OPEN, {0, 0, 0x22, 0x56, 0, 0, 0, 0, 0, 0})[1], 0);
            CHECK_EQ(x->ask(SoundFw::STREAM_DATA, {0, 0x66, 0x02, 0x01})[1], 0); // 66,150 bytes
            for (const uint8_t b : noise(66150, 9))
                x->card.write(SoundCard3::DATA, b);
            CHECK_EQ(x->ask(SoundFw::STREAM_END, {0})[1], 0);
        }
        d.card.set_tap(true, SoundFw::SRC_BEEP);
        CHECK_EQ(d.ask(SoundFw::BEEP, {static_cast<uint8_t>(hz), static_cast<uint8_t>(hz >> 8), 0xD0, 0x07, 0})[1], 0); // 2,000 ms
        quiet.ask(SoundFw::NOP);
        std::vector<uint8_t> regs_d, regs_q;
        for (int i = 0; i < 20; ++i)
        {
            d.ms(100);
            quiet.ms(100);
            for (unsigned r = 0; r < 25; ++r)
                regs_d.push_back(d.card.chips().sid(0)[r]), regs_q.push_back(quiet.card.chips().sid(0)[r]);
        }
        const double f = pitch(d.card.take_tap());
        std::printf("  %u Hz: %.3f Hz at the tap (%.4f%%)\n", hz, f, (f - hz) / hz * 100);
        CHECK(std::fabs(f - hz) / hz < 0.001);
        CHECK(regs_d == regs_q); // the tune's SIDs untouched by the beep
        CHECK_EQ(d.card.firmware().stream_state(0), SoundFw::S_PLAYING); // the stream still playing under it
        d.ms(20);
        CHECK_EQ(of_kind(d.events(), SoundFw::EV_BEEP_DONE).size(), 1u);
        CHECK(!d.card.firmware().beeping());
    }
    Dev d;
    CHECK_EQ(d.ask(SoundFw::BEEP, {19, 0, 100, 0, 0})[1], SoundFw::E_PARAM); // below 20 Hz
    CHECK_EQ(d.ask(SoundFw::BEEP, {0x21, 0x4E, 100, 0, 0})[1], SoundFw::E_PARAM); // 20,001
}

TEST(a_mono_tune_is_heard_in_both_ears)
{
    Dev d;
    d.load(0, tone_tune(0x1C00, 0x21)); // a sawtooth, about 420 Hz
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(100);
    d.card.set_tap(true);
    d.ms(500);
    const auto tap = d.card.take_tap();
    const double l = rms(tap, 0), r = rms(tap, 1);
    std::printf("  RMS left %.0f, right %.0f\n", l, r);
    CHECK(l > 500);
    CHECK_EQ(l, r);
}

TEST(the_eq_and_vis_page_cover_the_sids)
{
    // A SID's triangle at 3 kHz (frequency 51,090): the vis page's loudest band is 3 kHz's (bin 4 of 750 Hz,
    // byte 3), and the EQ's 3 kHz band cut by 12 dB lowers it there and at the tap.
    Dev d;
    d.load(0, tone_tune(51090, 0x11));
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(200);
    d.card.set_tap(true);
    d.ms(200);
    const double before = rms(d.card.take_tap(), 0);
    const auto vis = d.ask(SoundFw::VIS);
    REQUIRE(vis.size() == 2 + 98u);
    unsigned loudest = 0;
    for (unsigned b = 0; b < 32; ++b)
        if (vis[2 + b] > vis[2 + loudest])
            loudest = b;
    std::printf("  the loudest band %u (%u), the VU %u %u\n", loudest, vis[2 + loudest], vis[2 + 32], vis[2 + 33]);
    CHECK_EQ(loudest, 3u);
    CHECK(vis[2 + 32] > 0 && vis[2 + 33] > 0);
    const auto lv = d.ask(SoundFw::LEVELS);
    REQUIRE(lv.size() == 2 + 10u);
    CHECK(lv[2] > 0); // band 0 is bins 1-4
    std::vector<uint8_t> gains(11, 0);
    gains[6] = static_cast<uint8_t>(-24); // band 6: 3 kHz, -12 dB
    CHECK_EQ(d.ask(SoundFw::EQ_ALL, gains)[1], 0);
    CHECK_EQ(d.ask(SoundFw::EQ_ON, {1})[1], 0);
    d.ms(100);
    d.card.take_tap();
    d.ms(200);
    const double after = rms(d.card.take_tap(), 0);
    const auto vis2 = d.ask(SoundFw::VIS);
    std::printf("  RMS %.0f before the EQ, %.0f after; band 3 %u then %u\n", before, after, vis[2 + 3], vis2[2 + 3]);
    CHECK(after < before * 0.4);
    CHECK(vis2[2 + 3] < vis[2 + 3]);
    gains[6] = 25; // out of range: nothing set
    CHECK_EQ(d.ask(SoundFw::EQ_ALL, gains)[1], SoundFw::E_PARAM);
    CHECK_EQ(d.ask(SoundFw::VOLUME, {0, 255})[1], 0);
    d.ms(50);
    d.card.take_tap();
    d.ms(50);
    const auto tap = d.card.take_tap();
    CHECK_EQ(rms(tap, 0), 0.0);
    CHECK(rms(tap, 1) > 0);
}

namespace
{
    /// A tune, the boot sound and a beep, the output tapped for a second: with the host's audio rate `rate`
    /// (taken as it comes) and ticked `step` host cycles at a time.
    std::vector<int16_t> scene(uint32_t rate, uint64_t step)
    {
        Dev d;
        d.card.set_audio_rate(rate);
        d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0xEE, 0x01, 0xD4, 0x60}, 21));
        CHECK_EQ(d.play(0, 0), 0);
        d.send(SoundFw::BOOTSOUND, 1);
        d.send(SoundFw::BEEP, 2, {0xE8, 0x03, 0xF4, 0x01, 0});
        d.card.set_tap(true);
        std::vector<float> host(2 * 4096);
        for (uint64_t t = 0; t < SoundCard::PAL_HZ; t += step)
        {
            d.card.tick(step);
            while (d.card.take_audio(host.data(), 4096) > 0)
            {
            }
        }
        return d.card.take_tap();
    }
} // namespace

TEST(each_source_s_output_is_the_same_at_rate_0_and_48000)
{
    const auto a = scene(0, 1000), b = scene(48000, 997), c = scene(22050, 1);
    std::printf("  %zu, %zu and %zu frames\n", a.size() / 2, b.size() / 2, c.size() / 2);
    REQUIRE(!a.empty());
    const size_t n = std::min({a.size(), b.size(), c.size()});
    CHECK(n > 2 * 47000u);
    CHECK(std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), b.begin()));
    CHECK(std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), c.begin()));
    CHECK(rms(a, 0) > 100);
}

TEST(the_boot_sound_plays_from_the_card_s_flash)
{
    namespace fl = sound_flash;
    Dev d;
    d.card.set_tap(true, SoundFw::SRC_EFFECTS);
    CHECK_EQ(d.ask(SoundFw::BOOTSOUND)[1], 0);
    const Es5503 &doc = d.card.doc();
    CHECK(std::equal(fl::WAVE, fl::WAVE + fl::WAVE_SIZE, doc.ram().begin())); // the samples, from the card's flash
    CHECK_EQ(doc.enabled(), fl::OSCS);
    // Frame 0: bike 1 on oscillators 0-3, from the top, at its first pitch and volumes; the rest halted.
    for (unsigned o = 0; o < 4; ++o)
        CHECK_EQ(doc.peek(static_cast<uint8_t>(0xA0 + o)) & 1, 0);
    for (unsigned o = 4; o < fl::OSCS; ++o)
        CHECK_EQ(doc.peek(static_cast<uint8_t>(0xA0 + o)) & 1, 1);
    CHECK_EQ(doc.peek(0x00) | doc.peek(0x20) << 8, fl::pitch_lo[0] | fl::pitch_hi[0] << 8);
    CHECK_EQ(doc.peek(0x40), fl::vol_l[0] << 1);
    CHECK_EQ(doc.peek(0x41), fl::vol_r[0] << 1);
    CHECK_EQ(doc.peek(0xA1) & 0x10, 0x10); // the odd oscillator to the right
    // Frame 8 (8 frames of 4 ticks at 240 a second: 133 ms): bike 2 on 4-7.
    d.ms(140);
    for (unsigned o = 4; o < 8; ++o)
        CHECK_EQ(doc.peek(static_cast<uint8_t>(0xA0 + o)) & 1, 0);
    CHECK(d.card.firmware().boot_sound_playing());
    d.ms(5000); // 300 frames: 5 s
    CHECK(!d.card.firmware().boot_sound_playing());
    for (unsigned o = 0; o < fl::OSCS; ++o)
        CHECK_EQ(doc.peek(static_cast<uint8_t>(0xA0 + o)) & 1, 1);
    const auto tap = d.card.take_tap();
    std::printf("  %zu frames from the ES5503, RMS %.0f %.0f\n", tap.size() / 2, rms(tap, 0), rms(tap, 1));
    CHECK(rms(tap, 0) > 200 && rms(tap, 1) > 200);
}

TEST(a_one_sid_tune_leaves_sid_b_to_what_plays_there)
{
    // Music on SID B (a SID instrument: SID A is the tune's), a long note; then the one-SID tune started again,
    // paused, resumed and stopped: SID B's registers and its music voice are untouched each time.
    Dev d;
    d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21));
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(20);
    std::vector<uint8_t> blk{1, 1, 0, 0, 1, 0, 0, 0, 0, 0x20, 0x09, 0xA9, 0x00, 0x08, 0xFF, 128, 0x00, 60};
    for (const uint32_t x : {5'000'000u, 5'000'000u})
        for (int i = 0; i < 4; ++i)
            blk.push_back(static_cast<uint8_t>(x >> (8 * i)));
    CHECK_EQ(d.ask(SoundFw::MUSIC_LOAD, {0, static_cast<uint8_t>(blk.size()), 0, 0})[1], 0);
    for (const uint8_t b : blk)
        d.card.write(SoundCard3::DATA, b);
    d.ms(1);
    CHECK_EQ(d.ask(SoundFw::MUSIC_PLAY, {0, 0})[1], 0);
    d.ms(20);
    REQUIRE(d.card.firmware().voice_use(3) == SoundFw::V_MUSIC);
    const auto b = d.card.chips().sid(1);
    std::printf("  SID B: control $%02X, AD $%02X, SR $%02X, $18 $%02X\n", b[4], b[5], b[6], b[0x18]);
    CHECK_EQ(b[4] & 1, 1); // its note gated
    auto same = [&](const char *what) {
        const auto now = d.card.chips().sid(1);
        bool alike = true;
        for (unsigned r = 0; r < 25; ++r)
            alike = alike && now[r] == b[r];
        if (!alike)
            std::printf("  after %s: SID B's registers changed\n", what);
        CHECK(alike);
        CHECK(d.card.firmware().voice_use(3) == SoundFw::V_MUSIC);
    };
    CHECK_EQ(d.play(0, 0), 0); // the tune again
    d.ms(10);
    same("TUNE_PLAY");
    CHECK_EQ(d.ask(SoundFw::TUNE_PAUSE)[1], 0);
    d.ms(10);
    same("TUNE_PAUSE");
    CHECK_EQ(d.ask(SoundFw::TUNE_RESUME)[1], 0);
    d.ms(10);
    same("TUNE_RESUME");
    CHECK_EQ(d.ask(SoundFw::TUNE_STOP)[1], 0);
    d.ms(10);
    same("TUNE_STOP");
}

TEST(identify_says_the_sids_model_a_tune_set)
{
    Dev d;
    auto models = [&] {
        const auto r = d.ask(SoundFw::IDENTIFY);
        return std::make_pair(r.at(2 + 23), r.at(2 + 24));
    };
    CHECK(models() == std::make_pair(uint8_t{1}, uint8_t{1})); // the 6581, as the jumper is at first
    d.load(0, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21, 1, 0, 0x24)); // written for the 8580
    CHECK_EQ(d.play(0, 0), 0);
    d.ms(5);
    CHECK(models() == std::make_pair(uint8_t{2}, uint8_t{2}));
    d.load(1, psid(SAW + std::vector<uint8_t>{0x60, 0x60}, 21));
    CHECK_EQ(d.play(1, 0), 0);
    d.ms(5);
    CHECK(models() == std::make_pair(uint8_t{1}, uint8_t{1}));
}

int main() { return run_all(); }
