/**
 * @file test_sound_fw.cpp
 * @brief The sound card's firmware, revision 3, alone (sound_fw.h; hw/future/sound-device.md and its S1 appendix).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The firmware on a fake port (no chips): the host's side is its FIFOs, as the front end's registers would give
 * them. Records are written byte by byte, the card's clock advanced, and the events read back and parsed.
 */

#include "harness.h"

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

int main() { return run_all(); }
