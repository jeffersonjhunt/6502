/**
 * @file test_sound_fw.cpp
 * @brief The sound card's firmware, revision 3, alone (sound_fw.h; hw/future/sound-device.md and its S1 appendix).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The firmware on a fake port (no chips): the host's side is its FIFOs, as the front end's registers would give
 * them. Records are written byte by byte, the card's clock advanced, and the events read back and parsed.
 */

#include "harness.h"

#include "sid_plan.h"
#include "sound_card3.h"
#include "sound_fw.h"

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
    std::vector<uint8_t> psid(std::vector<uint8_t> code, uint16_t play_at, uint16_t songs = 1, uint16_t sid2 = 0)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 3, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10;
        if (play_at)
            b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = static_cast<uint8_t>(songs), b[0x11] = 1, b[0x77] = 0x14;
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

int main() { return run_all(); }
