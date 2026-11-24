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

TEST(a_record_runs_20_us_after_its_last_byte_wherever_in_a_sample_it_lands)
{
    // The last byte at each place in a sample (the bus ticked a cycle at a time): its reply comes no sooner
    // than LATENCY_US (20 µs, 143 cycles) after it, and no later than a sample past that; and with the test hook
    // at 100 µs, no sooner than 716 cycles.
    for (const uint32_t us : {20u, 100u})
    {
        unsigned soonest = 1'000'000, latest = 0;
        for (unsigned offset = 0; offset < 160; offset += 7)
        {
            SoundCard3 card; // alone, a tick a host cycle at 7.16 MHz
            card.firmware().set_latency_us(us);
            card.tick(SoundCard3::NATIVE_HZ / 1000 * (SoundFw::STARTUP_MS + 1) + offset);
            card.write(SoundCard3::CMD, SoundFw::NOP);
            card.write(SoundCard3::CMD, 0);
            card.write(SoundCard3::CMD, 1); // its last byte, now
            unsigned cycles = 0;
            while (!card.peek(SoundCard3::EVTLEN) && cycles < 5000)
                card.tick(1), ++cycles;
            soonest = std::min(soonest, cycles);
            latest = std::max(latest, cycles);
        }
        const unsigned want = us * 7'159'090u / 1'000'000u;
        std::printf("  %u µs: the reply %u to %u cycles after the last byte (at least %u)\n", us, soonest, latest, want);
        CHECK(soonest >= want);
        CHECK(latest <= want + 151); // and within a sample of it
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

namespace
{
    /// A firmware image as the S1 appendix lays it out (tools/mksndfw.py's), its body `body` bytes of a pattern.
    /// An image: `bad` 1 a wrong body CRC, 2 a wrong header CRC. `magic`, the header's version `hver` and the
    /// length field less the body's (`short_by`, the body CRC over what the field says) go in before the header's
    /// CRC, so an image wrong in one of them has every other check right.
    std::vector<uint8_t> fw_image(uint8_t major, uint8_t minor, uint8_t card = 0xA3, size_t body = 3000, int bad = 0,
                                  const char *magic = "OOSD", uint8_t hver = 1, size_t short_by = 0)
    {
        std::vector<uint8_t> b(body);
        for (size_t i = 0; i < body; ++i)
            b[i] = static_cast<uint8_t>(i * 7 + 3);
        const size_t said = body - short_by;
        const uint32_t bcrc = Crc32::of(b.data(), said) ^ (bad == 1 ? 1u : 0u);
        std::vector<uint8_t> h{static_cast<uint8_t>(magic[0]), static_cast<uint8_t>(magic[1]), static_cast<uint8_t>(magic[2]),
                               static_cast<uint8_t>(magic[3]), card, hver, major, minor, 5, 0, 0, 0};
        for (const uint32_t x : {static_cast<uint32_t>(said), bcrc})
            for (int i = 0; i < 4; ++i)
                h.push_back(static_cast<uint8_t>(x >> (8 * i)));
        h.resize(28, 0);
        const uint32_t hcrc = Crc32::of(h.data(), h.size()) ^ (bad == 2 ? 1u : 0u);
        for (int i = 0; i < 4; ++i)
            h.push_back(static_cast<uint8_t>(hcrc >> (8 * i)));
        h.insert(h.end(), b.begin(), b.end());
        return h;
    }
    /// FW_BEGIN, the image by DATA, FW_COMMIT, then FWRESULT. @return FWRESULT's status, or $FF for none
    uint8_t update(Board &b, const std::vector<uint8_t> &img)
    {
        const uint32_t n = static_cast<uint32_t>(img.size());
        b.send(SoundFw::FW_BEGIN, 1, {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n >> 16), 0});
        b.run(1000);
        for (const uint8_t x : img)
            b.put(SoundCard3::DATA, x);
        b.run(1000);
        b.send(SoundFw::FW_COMMIT, 2);
        b.run(1000);
        uint8_t commit = 0xFF;
        for (const Event &e : b.events())
            if (e.kind == SoundFw::EV_REPLY && e.body[0] == 2)
                commit = e.body[1];
        CHECK_EQ(commit, 0); // taken at once, checked after
        b.run_ms(SoundFw::FLASH_MS + 2);
        for (const Event &e : b.events())
            if (e.kind == SoundFw::EV_FWRESULT)
                return e.body[5];
        return 0xFF;
    }
} // namespace

TEST(a_good_image_boots_after_fwrestart_and_says_its_version)
{
    Board b;
    REQUIRE(b.card != nullptr);
    CHECK_EQ(update(b, fw_image(1, 1)), 0);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 0); // still the one running: 1.0
    b.card->bus_reset();                         // /RESET keeps the slots and runs on
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 0);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.run_ms(SoundFw::STARTUP_MS + 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER), 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 1); // 1.1 booted
    CHECK_EQ(b.card->firmware().running_slot(), 1);
    b.events();
    b.send(SoundFw::IDENTIFY, 3);
    b.run(1000);
    const auto ev = b.events();
    REQUIRE(ev.size() == 1 && ev[0].body.size() == 2 + 32u);
    CHECK_EQ(ev[0].body[3], 1);
    CHECK_EQ(ev[0].body[4], 1);
    CHECK_EQ(ev[0].body[2 + 25], 1); // image B
    // The next update goes into A, the slot not running.
    CHECK_EQ(update(b, fw_image(1, 2)), 0);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.run_ms(SoundFw::STARTUP_MS + 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 2);
    CHECK_EQ(b.card->firmware().running_slot(), 0);
}

TEST(a_restart_during_the_flash_write_keeps_the_old_image)
{
    // A good image (1.1) committed, FWRESTART 10 ms into the 50 ms write: the old image (1.0) boots, no FWRESULT
    // comes. Sent again and left to finish, it boots.
    Board b;
    REQUIRE(b.card != nullptr);
    const auto img = fw_image(1, 1);
    const uint32_t n = static_cast<uint32_t>(img.size());
    b.send(SoundFw::FW_BEGIN, 1, {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n >> 16), 0});
    b.run(1000);
    for (const uint8_t x : img)
        b.put(SoundCard3::DATA, x);
    b.run(1000);
    b.send(SoundFw::FW_COMMIT, 2);
    b.run_ms(10);
    b.events();
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.run_ms(SoundFw::STARTUP_MS + SoundFw::FLASH_MS + 10);
    bool result = false;
    for (const Event &e : b.events())
        result = result || e.kind == SoundFw::EV_FWRESULT;
    CHECK(!result);
    CHECK_EQ(b.reg(SoundCard3::FWVER), 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 0); // the old one
    CHECK_EQ(b.card->firmware().running_slot(), 0);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART); // and again: still the old one
    b.run_ms(SoundFw::STARTUP_MS + 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 0);
    b.events();
    CHECK_EQ(update(b, img), 0);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.run_ms(SoundFw::STARTUP_MS + 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 1);
    CHECK_EQ(b.card->firmware().running_slot(), 1);
}

TEST(a_bad_crc_or_another_card_s_image_leaves_the_old_one)
{
    Board b;
    REQUIRE(b.card != nullptr);
    // Each wrong in one way alone, its header's CRC right (T12): the body's CRC, the header's, another card's,
    // another magic, a header version 2, a length field one short of the body (its CRC over what it says).
    for (const auto &img : {fw_image(2, 0, 0xA3, 3000, 1), fw_image(2, 0, 0xA3, 3000, 2), fw_image(2, 0, 0xA2),
                            fw_image(2, 0, 0xA3, 3000, 0, "XOSD"), fw_image(2, 0, 0xA3, 3000, 0, "OOSD", 2),
                            fw_image(2, 0, 0xA3, 3000, 0, "OOSD", 1, 1)})
        CHECK_EQ(update(b, img), SoundFw::E_FIRMWARE);
    b.put(SoundCard3::CTRL, SoundCard3::CTRL_FWRESTART);
    b.run_ms(SoundFw::STARTUP_MS + 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER), 1);
    CHECK_EQ(b.reg(SoundCard3::FWVER + 1), 0); // the old one
    CHECK_EQ(b.card->firmware().running_slot(), 0);
    // A commit with no image: E_STATE.
    b.send(SoundFw::FW_COMMIT, 9);
    b.run(1000);
    const auto ev = b.events();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].body[1], SoundFw::E_STATE);
}

TEST(selftest_names_the_part_that_fails)
{
    Board b;
    REQUIRE(b.card != nullptr);
    for (unsigned bit = 0; bit <= 4; ++bit)
    {
        const uint8_t fault = static_cast<uint8_t>(bit < 4 ? 1u << bit : 0u);
        b.card->firmware().set_faults(fault);
        b.send(SoundFw::SELFTEST, 1, {0x0F});
        b.send(SoundFw::SELFTEST, 2, {0x05}); // the store and the SIDs alone
        b.run(1000);
        const auto ev = b.events();
        REQUIRE(ev.size() == 2);
        CHECK_EQ(ev[0].body[2], 0x0F);
        CHECK_EQ(ev[0].body[3], fault);
        CHECK_EQ(ev[1].body[2], 0x05);
        CHECK_EQ(ev[1].body[3], fault & 0x05);
    }
}

int main() { return run_all(); }
