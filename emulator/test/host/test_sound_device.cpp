/**
 * @file test_sound_device.cpp
 * @brief The sound card, revision 3, on the bus: its registers, FIFOs, DMA, interrupt and resets (sound_card3.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A backplane with the DMA controller and a revision 3 sound card in module A ($C500), as the machine file's
 * `sound ... rev=3` builds it. The host's side is the bus: records written to CMD, events read from EVT, data
 * by the CPU into DATA or by DMA channel 2 in demand mode.
 */

#include "harness.h"
#include "test_support.h"

#include "dma_controller.h"
#include "machine.h"
#include "sound_card3.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t CARD = 0xC500;

    struct Event
    {
        uint8_t kind{0};
        std::vector<uint8_t> body;
    };

    struct Board
    {
        std::unique_ptr<Machine> m;
        SoundCard3 *card{nullptr};

        explicit Board(const std::string &options = "")
        {
            std::string error;
            m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                               "dma 400-47F space=io name=dma irq\nsound 500-57F space=io name=sound irq drq rev=3" +
                                   options + "\n",
                               "sound.machine", {}, error);
            if (!m)
                std::printf("  %s\n", error.c_str());
            card = m ? m->sound_device() : nullptr;
            if (card)
                run_ms(SoundFw::STARTUP_MS + 1);
        }
        Decoder &bus() { return m->bus(); }
        uint8_t reg(uint8_t r) { return bus().read(static_cast<uint16_t>(CARD + r)); }
        void put(uint8_t r, uint8_t v) { bus().write(static_cast<uint16_t>(CARD + r), v); }
        void send(uint8_t cmd, uint8_t tag, const std::vector<uint8_t> &params = {})
        {
            put(SoundCard3::CMD, cmd);
            put(SoundCard3::CMD, static_cast<uint8_t>(params.size()));
            put(SoundCard3::CMD, tag);
            for (const uint8_t b : params)
                put(SoundCard3::CMD, b);
        }
        /// `cycles` of the host, the devices caught up after each as the run loop's irq() does.
        void run(uint64_t cycles)
        {
            for (uint64_t i = 0; i < cycles; ++i)
            {
                bus().read(0x0000);
                bus().irq();
            }
        }
        void run_ms(unsigned ms) { run(static_cast<uint64_t>(ms) * SoundCard3::NATIVE_HZ / 1000); }
        std::vector<Event> events()
        {
            std::vector<Event> out;
            while (reg(SoundCard3::EVTLEN))
            {
                Event e;
                e.kind = reg(SoundCard3::EVT);
                const uint8_t n = reg(SoundCard3::EVT);
                for (unsigned i = 0; i < n; ++i)
                    e.body.push_back(reg(SoundCard3::EVT));
                out.push_back(e);
            }
            return out;
        }
        /// Channel 2: `count` bytes (0: 65,536) from `from` to the card, demand mode.
        void channel(uint32_t from, uint32_t count)
        {
            bus().poke(0xC47E, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE | 2 << 1);
            bus().poke(0xC400, static_cast<uint8_t>(from));
            bus().poke(0xC401, static_cast<uint8_t>(from >> 8));
            bus().poke(0xC402, static_cast<uint8_t>(from >> 16));
            bus().poke(0xC404, static_cast<uint8_t>(count));
            bus().poke(0xC405, static_cast<uint8_t>(count >> 8));
            bus().poke(0xC406, DmaController::DEMAND | DmaController::TO_CARD);
            bus().poke(0xC407, DmaController::ENABLE);
        }
        bool armed() { return m->dma()->peek_channel(2, DmaController::CTRL) & DmaController::ENABLE; }
    };

    std::vector<uint8_t> sample_load(uint8_t slot, uint32_t len)
    {
        return {slot, static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len >> 16), 0, 0x22, 0x56,
                0, 0, 0, 0, 0, 0};
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
    uint32_t le32(const std::vector<uint8_t> &v, size_t at)
    {
        return static_cast<uint32_t>(v.at(at) | v.at(at + 1) << 8 | v.at(at + 2) << 16) | static_cast<uint32_t>(v.at(at + 3)) << 24;
    }
} // namespace

TEST(the_card_says_what_it_is_and_comes_up_ready)
{
    Board b;
    REQUIRE(b.card != nullptr);
    CHECK(b.m->sound_card() == nullptr); // revision 3 is the device, not revision 2's card
    CHECK_EQ(b.reg(SoundCard3::ID_REG), 0xA3);
    CHECK_EQ(b.reg(SoundCard3::MEMSIZE), 128);
    CHECK_EQ(b.reg(SoundCard3::FWVER), SoundFw::VERSION_MAJOR);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), SoundFw::VERSION_MINOR);
    CHECK_EQ(b.reg(SoundCard3::FEATURES), 0xFF);
    CHECK(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_READY);
    CHECK(b.reg(SoundCard3::ISR) & SoundCard3::INT_READY);
    CHECK_EQ(b.reg(0x20), 0); // an offset no register has
    Board small(" memsize=2");
    REQUIRE(small.card != nullptr);
    CHECK_EQ(small.reg(SoundCard3::MEMSIZE), 32);
}

TEST(a_load_s_bytes_go_in_by_channel_2_demand_dma_and_checksum)
{
    // 10,000 bytes into a sample slot by channel 2: DRQ only while the transfer is open, so the channel's
    // terminal count comes with the last byte and nothing asks between transfers.
    Board b;
    REQUIRE(b.card != nullptr);
    const std::vector<uint8_t> bytes = noise(10000, 5);
    for (size_t i = 0; i < bytes.size(); ++i)
        b.bus().write_physical(0x10000 + static_cast<uint32_t>(i), bytes[i]);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_DRQ);
    b.run(100);
    CHECK(!b.card->drq()); // DRQ on, but no transfer: nothing asks
    b.send(SoundFw::SAMPLE_LOAD, 1, sample_load(3, 10000));
    b.run(1000);
    CHECK(b.card->drq());
    b.channel(0x10000, 10000);
    b.run(12000);
    CHECK(!b.armed()); // all of it in: terminal count
    CHECK(!b.card->drq());
    CHECK(b.reg(SoundCard3::ISR) & SoundCard3::INT_XDONE);
    b.run(30000); // longer: still nothing asks
    CHECK(!b.card->drq());
    b.send(SoundFw::CHECKSUM, 2, {SoundFw::SAMPLE_LOAD, 3});
    b.run(1000);
    const auto ev = b.events();
    REQUIRE(ev.size() == 3);
    CHECK_EQ(ev[0].kind, SoundFw::EV_REPLY);
    CHECK_EQ(ev[1].kind, SoundFw::EV_XFERDONE);
    CHECK_EQ(ev[2].kind, SoundFw::EV_REPLY);
    CHECK_EQ(le32(ev[2].body, 2), Crc32::of(bytes.data(), bytes.size()));
    CHECK_EQ(b.card->data_dropped(), 0u);
}

TEST(evt_is_a_level_and_no_event_is_lost_between_the_drain_and_the_clear)
{
    Board b;
    REQUIRE(b.card != nullptr);
    b.put(SoundCard3::IEN, SoundCard3::INT_EVT);
    b.put(SoundCard3::ISR, 0xFF);
    CHECK(!b.card->irq());
    b.send(SoundFw::NOP, 1);
    b.run(1000);
    CHECK(b.card->irq());
    CHECK_EQ(b.events().size(), 1u); // the handler drains it...
    b.send(SoundFw::NOP, 2);         // ...an event comes before its write to ISR...
    b.run(1000);
    b.put(SoundCard3::ISR, SoundCard3::INT_EVT); // ...which clears nothing: the line stays
    CHECK(b.card->irq());
    CHECK(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_IRQ);
    CHECK_EQ(b.events().size(), 1u);
    CHECK(!b.card->irq());
}

TEST(xleft_latches_all_three_bytes_on_the_low_read)
{
    Board b;
    REQUIRE(b.card != nullptr);
    b.send(SoundFw::SAMPLE_LOAD, 1, sample_load(0, 0x10001));
    b.run(1000);
    CHECK_EQ(b.reg(SoundCard3::XTAG), 1);
    b.put(SoundCard3::DATA, 0x00); // left $010000
    CHECK_EQ(b.reg(SoundCard3::XLEFT), 0x00);
    b.put(SoundCard3::DATA, 0x00); // left $00FFFF, across the carry, between the reads
    CHECK_EQ(b.reg(SoundCard3::XLEFT + 1), 0x00);
    CHECK_EQ(b.reg(SoundCard3::XLEFT + 2), 0x01); // $010000 as latched, not a torn $01FF00 or $00FF..
    CHECK_EQ(b.reg(SoundCard3::XLEFT), 0xFF);
    CHECK_EQ(b.reg(SoundCard3::XLEFT + 1), 0xFF);
    CHECK_EQ(b.reg(SoundCard3::XLEFT + 2), 0x00);
}

TEST(reset_abandons_a_transfer_and_empties_both_fifos_but_keeps_the_store)
{
    for (const bool by_bus : {false, true})
    {
        Board b;
        REQUIRE(b.card != nullptr);
        b.send(SoundFw::SAMPLE_LOAD, 1, sample_load(0, 4));
        b.run(1000);
        for (int i = 0; i < 4; ++i)
            b.put(SoundCard3::DATA, static_cast<uint8_t>(i));
        b.send(SoundFw::SAMPLE_LOAD, 2, sample_load(1, 100));
        b.put(SoundCard3::IEN, SoundCard3::INT_EVT);
        b.put(SoundCard3::CTRL, SoundCard3::CTRL_DRQ);
        b.run(1000);
        CHECK(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_XFER);
        b.put(SoundCard3::CMD, SoundFw::NOP); // half a record
        if (by_bus)
            b.card->bus_reset();
        else
            b.put(SoundCard3::CTRL, SoundCard3::CTRL_RESET);
        CHECK_EQ(b.reg(SoundCard3::STATUS) & (SoundCard3::STATUS_XFER | SoundCard3::STATUS_EVT), 0);
        CHECK_EQ(b.reg(SoundCard3::EVTLEN), 0);
        CHECK_EQ(b.reg(SoundCard3::CMDFREE), 255);
        CHECK_EQ(b.reg(SoundCard3::CTRL), 0);
        CHECK_EQ(b.reg(SoundCard3::IEN), 0);
        CHECK_EQ(b.reg(SoundCard3::ISR), 0);
        CHECK(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_READY); // the firmware ran on
        b.send(SoundFw::CHECKSUM, 3, {SoundFw::SAMPLE_LOAD, 0});
        b.send(SoundFw::CHECKSUM, 4, {SoundFw::SAMPLE_LOAD, 1});
        b.run(1000);
        const auto ev = b.events();
        REQUIRE(ev.size() == 2);
        CHECK_EQ(ev[0].body[1], 0);              // kept
        CHECK_EQ(ev[1].body[1], SoundFw::E_SLOT); // abandoned
    }
}

TEST(fwrestart_empties_the_store)
{
    Board b;
    REQUIRE(b.card != nullptr);
    b.send(SoundFw::SAMPLE_LOAD, 1, sample_load(0, 2));
    b.run(1000);
    b.put(SoundCard3::DATA, 1);
    b.put(SoundCard3::DATA, 2);
    b.put(SoundCard3::ISR, 0xFF);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    CHECK(!(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_READY));
    b.run_ms(SoundFw::STARTUP_MS - 2);
    CHECK(!(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_READY));
    b.run_ms(3);
    CHECK(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_READY);
    CHECK(b.reg(SoundCard3::ISR) & SoundCard3::INT_READY);
    b.events();
    b.send(SoundFw::CHECKSUM, 2, {SoundFw::SAMPLE_LOAD, 0});
    b.run(1000);
    const auto ev = b.events();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].body[1], SoundFw::E_SLOT);
}

TEST(a_byte_to_data_with_no_transfer_is_dropped)
{
    Board b;
    REQUIRE(b.card != nullptr);
    b.put(SoundCard3::DATA, 0x42);
    CHECK_EQ(b.card->data_dropped(), 1u);
    CHECK(!(b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_DATA));
    b.send(SoundFw::NOP, 1);
    b.run(1000);
    const auto ev = b.events();
    REQUIRE(ev.size() == 1); // nothing else came of it
    CHECK_EQ(ev[0].body[0], 1);
}

TEST(cmdfree_and_cmdrdy_hold_a_whole_record)
{
    // The card held (not yet READY after a FWRESTART), so records wait: CMDRDY while 256 bytes fit, CMDFREE
    // the room (255 meaning 255 or more), and CMDROOM's edge once the full FIFO has room again.
    Board b;
    REQUIRE(b.card != nullptr);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.put(SoundCard3::ISR, 0xFF);
    unsigned records = 0;
    while (b.reg(SoundCard3::STATUS) & SoundCard3::STATUS_CMDRDY)
    {
        b.send(SoundFw::EQ_ALL, static_cast<uint8_t>(records), std::vector<uint8_t>(11, 0)); // 14 bytes
        ++records;
    }
    const unsigned used = records * 14;
    std::printf("  %u records (%u bytes) before CMDRDY fell; CMDFREE %u\n", records, used, b.reg(SoundCard3::CMDFREE));
    CHECK(SoundFw::CMD_BYTES - used < SoundFw::RECORD_MAX);
    CHECK(SoundFw::CMD_BYTES - (used - 14) >= SoundFw::RECORD_MAX);
    CHECK_EQ(b.reg(SoundCard3::CMDFREE), SoundFw::CMD_BYTES - used);
    while (b.reg(SoundCard3::CMDFREE) > 0)
        b.put(SoundCard3::CMD, SoundFw::NOP); // to the brim with half a record
    b.put(SoundCard3::CMD, SoundFw::NOP);      // dropped
    CHECK(!(b.reg(SoundCard3::ISR) & SoundCard3::INT_CMDROOM));
    b.run_ms(SoundFw::STARTUP_MS + 1); // up: the records run
    CHECK(b.reg(SoundCard3::ISR) & SoundCard3::INT_CMDROOM);
    CHECK_EQ(b.reg(SoundCard3::CMDFREE), 255);
}

int main() { return run_all(); }
