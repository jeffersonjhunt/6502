/**
 * @file test_sd.cpp
 * @brief The mass storage card (hw/design.md, "Mass storage registers") and its media.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Most cases drive the card's registers directly, ticking its clock by hand,
 * so the timing can be checked to the cycle. The last run 6502 code on a
 * backplane machine: a CPU loop, and DMA channel 0 both ways.
 */

#include "harness.h"

#include "cpu6502.h"
#include "dma_controller.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_media.h"
#include "session.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t HZ = SdCard::NATIVE_HZ;
    constexpr uint64_t cycles_us(uint64_t n) { return HZ * n / 1'000'000; }
    constexpr uint64_t INIT = cycles_us(uint64_t{SdCard::INIT_MS} * 1000);
    constexpr uint64_t ACCESS = cycles_us(SdCard::ACCESS_US), SECTOR_TIME = cycles_us(SdCard::SECTOR_US);

    /// Sector n's byte i: different in every sector.
    uint8_t pattern(uint64_t n, size_t i) { return static_cast<uint8_t>(n * 31 + i * 7 + (i >> 8)); }

    std::vector<uint8_t> image(uint64_t sectors)
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(sectors) * SdCard::SECTOR);
        for (size_t n = 0; n < sectors; ++n)
            for (size_t i = 0; i < SdCard::SECTOR; ++i)
                bytes[n * SdCard::SECTOR + i] = pattern(n, i);
        return bytes;
    }

    /// A card with a 64-sector image in it, started up.
    struct Rig
    {
        explicit Rig(uint64_t sectors = 64, bool started = true)
            : media(std::make_shared<MemoryMedia>(image(sectors), "TEST.IMG"))
        {
            card.insert(media, false);
            if (started)
                card.tick(INIT);
        }
        uint8_t reg(uint8_t r) { return card.read(r); }
        void set(uint8_t r, uint8_t v) { card.write(r, v); }
        void command(uint8_t cmd, uint32_t lba, uint16_t count, uint8_t unit = 0)
        {
            for (unsigned i = 0; i < 4; ++i)
                set(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            set(SdCard::COUNT, static_cast<uint8_t>(count));
            set(SdCard::COUNT + 1, static_cast<uint8_t>(count >> 8));
            set(SdCard::UNIT, unit);
            set(SdCard::CMD, cmd);
        }
        bool has(uint8_t bits) { return (reg(SdCard::STATUS) & bits) == bits; }
        uint32_t lba()
        {
            uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i)
                v |= uint32_t{reg(static_cast<uint8_t>(SdCard::LBA + i))} << (8 * i);
            return v;
        }
        unsigned count() { return unsigned{reg(SdCard::COUNT)} | unsigned{reg(SdCard::COUNT + 1)} << 8; }
        /// Read a whole sector through DATA. @return bytes that differ from sector n
        size_t read_sector(uint64_t n)
        {
            size_t differ = 0;
            for (size_t i = 0; i < SdCard::SECTOR; ++i)
                differ += reg(SdCard::DATA) != pattern(n, i);
            return differ;
        }

        std::shared_ptr<MemoryMedia> media;
        SdCard card;
    };
} // namespace

TEST(registers_read_back_and_reserved_ones_read_ff)
{
    Rig r;
    CHECK_EQ(r.reg(SdCard::ID_REG), 0x71);
    r.command(0x00, 0x12345678, 0xABCD, 5); // not a command: fails, but the registers stay
    CHECK_EQ(r.lba(), 0x12345678u);
    CHECK_EQ(r.count(), 0xABCDu);
    CHECK_EQ(r.reg(SdCard::UNIT), 5);
    r.set(SdCard::UNIT, 0xFB);
    CHECK_EQ(r.reg(SdCard::UNIT), 3); // 0-7
    r.set(SdCard::IEN, 0xFF);
    CHECK_EQ(r.reg(SdCard::IEN), 0x03);
    r.set(SdCard::CTRL, 0xFF);
    CHECK_EQ(r.reg(SdCard::CTRL), 0x01); // RESET is a strobe
    CHECK_EQ(r.reg(SdCard::CMD), 0xFF);  // write-only
    CHECK_EQ(r.reg(0x05), 0xFF);
    CHECK_EQ(r.reg(0x0F), 0xFF);
    CHECK_EQ(r.reg(0x12), 0xFF);
    CHECK_EQ(r.reg(0x7E), 0xFF);
}

TEST(a_card_starts_up_in_init_ms_and_a_command_before_then_fails)
{
    Rig r(64, false);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT);
    r.card.tick(INIT - 1);
    CHECK(!r.has(SdCard::READY));
    r.command(SdCard::READ, 0, 1);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::ERR);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NO_CARD);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    r.card.tick(1);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::ERR); // ERR stays until the next command
    r.command(SdCard::READ, 0, 1);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::BUSY);
    CHECK_EQ(r.reg(SdCard::ERROR), 0);
}

TEST(an_empty_slot_fails_every_command_with_no_card)
{
    SdCard card;
    card.tick(INIT);
    CHECK_EQ(card.read(SdCard::STATUS), 0);
    card.write(SdCard::CMD, SdCard::IDENTIFY);
    CHECK_EQ(card.read(SdCard::STATUS), SdCard::ERR);
    CHECK_EQ(card.read(SdCard::ERROR), SdCard::ERR_NO_CARD);
}

TEST(ctrl_reset_abandons_the_command_and_starts_the_card_up_again)
{
    Rig r;
    r.command(SdCard::READ, 0, 4);
    r.card.tick(ACCESS);
    CHECK(r.has(SdCard::BUSY | SdCard::DATA_READY));
    r.set(SdCard::CTRL, SdCard::CTRL_RESET);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT);
    CHECK_EQ(r.reg(SdCard::ISR), 0); // abandoned, not done
    r.card.tick(INIT);
    CHECK(r.has(SdCard::READY));
}

TEST(a_read_comes_through_data_with_lba_and_count_advancing)
{
    Rig r;
    r.command(SdCard::READ, 10, 3);
    CHECK_EQ(r.count(), 3u);
    CHECK_EQ(r.reg(SdCard::DATA), 0xFF); // nothing yet
    r.card.tick(ACCESS);
    for (uint64_t n = 10; n < 13; ++n)
    {
        r.card.tick(SECTOR_TIME); // the next one into the spare buffer, the last into the freed one
        CHECK(r.has(SdCard::BUSY | SdCard::DATA_READY));
        CHECK_EQ(r.lba(), static_cast<uint32_t>(n));
        CHECK_EQ(r.count(), static_cast<unsigned>(13 - n));
        CHECK_EQ(r.read_sector(n), size_t{0});
    }
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
    CHECK_EQ(r.lba(), 13u); // the next sector
    CHECK_EQ(r.count(), 0u);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    CHECK_EQ(r.reg(SdCard::DATA), 0xFF); // done: DATA has nothing
}

TEST(the_first_sector_takes_access_us_and_the_spare_buffer_fills_sector_us_later)
{
    Rig r;
    r.command(SdCard::READ, 0, 4);
    r.card.tick(ACCESS - 1);
    CHECK(!r.has(SdCard::DATA_READY));
    r.card.tick(1);
    CHECK(r.has(SdCard::DATA_READY));
    CHECK_EQ(r.read_sector(0), size_t{0});
    CHECK(!r.has(SdCard::DATA_READY)); // read at once: sector 1 is still on its way
    r.card.tick(SECTOR_TIME - 1);
    CHECK(!r.has(SdCard::DATA_READY));
    r.card.tick(1);
    CHECK(r.has(SdCard::DATA_READY));
    // Both buffers full: the card waits for the bus, then fetches into the freed one.
    r.card.tick(10 * SECTOR_TIME);
    CHECK_EQ(r.read_sector(1), size_t{0});
    CHECK(r.has(SdCard::DATA_READY)); // sector 2, fetched while 1 waited
    CHECK_EQ(r.read_sector(2), size_t{0});
    CHECK(!r.has(SdCard::DATA_READY));
    r.card.tick(SECTOR_TIME - 1); // sector 3 started when sector 1 freed its buffer
    CHECK(!r.has(SdCard::DATA_READY));
    r.card.tick(1);
    CHECK_EQ(r.read_sector(3), size_t{0});
    CHECK(!r.has(SdCard::BUSY));
}

TEST(the_last_sector_reads_and_nothing_past_the_command_is_fetched)
{
    Rig r;
    r.command(SdCard::READ, 62, 2);
    r.card.tick(ACCESS + 10 * SECTOR_TIME);
    CHECK_EQ(r.read_sector(62), size_t{0});
    r.card.tick(10 * SECTOR_TIME); // with a buffer free: but the command has no more
    CHECK_EQ(r.read_sector(63), size_t{0});
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
    r.command(SdCard::READ, 63, 1);
    r.card.tick(ACCESS + 10 * SECTOR_TIME);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::BUSY | SdCard::DATA_READY);
    CHECK_EQ(r.read_sector(63), size_t{0});
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
}

TEST(power_up_starts_a_card_in_the_slot_up_again)
{
    Rig r;
    r.card.reset();
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT);
    r.card.tick(INIT);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
}

TEST(a_host_error_is_a_timeout)
{
    struct Failing : SdMedia
    {
        Failing() : SdMedia("BAD") {}
        uint64_t sectors() const override { return 8; }
        bool read(uint64_t, uint8_t *) override { return false; }
        bool write(uint64_t, const uint8_t *) override { return false; }
        bool flush() override { return false; }
    };
    for (uint8_t cmd : {SdCard::READ, SdCard::WRITE, SdCard::FLUSH})
    {
        SdCard card;
        card.insert(std::make_shared<Failing>(), false);
        card.tick(INIT);
        card.write(SdCard::COUNT, 1);
        card.write(SdCard::CMD, cmd);
        for (size_t i = 0; i < SdCard::SECTOR; ++i)
            card.write(SdCard::DATA, 0); // the write's sector (dropped otherwise)
        card.tick(ACCESS + SECTOR_TIME);
        CHECK_EQ(card.read(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::ERR);
        CHECK_EQ(card.read(SdCard::ERROR), SdCard::ERR_TIMEOUT);
        CHECK_EQ(card.read(SdCard::ISR), SdCard::IRQ_DONE);
    }
}

TEST(a_write_goes_to_the_media_as_each_sector_is_programmed)
{
    Rig r;
    r.command(SdCard::WRITE, 20, 2);
    CHECK(r.has(SdCard::BUSY | SdCard::DATA_READY)); // room at once: two empty buffers
    for (unsigned s = 0; s < 2; ++s)
        for (size_t i = 0; i < SdCard::SECTOR; ++i)
            r.set(SdCard::DATA, static_cast<uint8_t>(0xA0 + s + i));
    CHECK(!r.has(SdCard::DATA_READY)); // both sectors given
    CHECK_EQ(r.count(), 0u);
    CHECK_EQ(r.lba(), 22u);
    CHECK(r.has(SdCard::BUSY));
    CHECK(!r.media->dirty());
    r.card.tick(SECTOR_TIME);
    CHECK(r.media->dirty());
    CHECK_EQ(r.media->bytes()[20 * 512 + 5], 0xA5);
    CHECK_EQ(r.media->bytes()[21 * 512 + 5], pattern(21, 5)); // not yet
    CHECK(r.has(SdCard::BUSY));
    r.card.tick(SECTOR_TIME);
    CHECK_EQ(r.media->bytes()[21 * 512 + 5], 0xA6);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    CHECK_EQ(r.media->bytes()[22 * 512 + 5], pattern(22, 5));
}

TEST(a_write_waits_for_a_buffer_when_both_are_full)
{
    Rig r;
    r.command(SdCard::WRITE, 0, 3);
    for (size_t i = 0; i < 2 * SdCard::SECTOR; ++i)
        r.set(SdCard::DATA, 0x11);
    CHECK(!r.has(SdCard::DATA_READY));
    r.set(SdCard::DATA, 0x22); // dropped: no room
    CHECK_EQ(r.reg(SdCard::BUFPTR), 0);
    r.card.tick(SECTOR_TIME);
    CHECK(r.has(SdCard::DATA_READY));
    for (size_t i = 0; i < SdCard::SECTOR; ++i)
        r.set(SdCard::DATA, 0x33);
    r.card.tick(2 * SECTOR_TIME);
    CHECK(!r.has(SdCard::BUSY));
    CHECK_EQ(r.media->bytes()[0], 0x11);
    CHECK_EQ(r.media->bytes()[511], 0x11);
    CHECK_EQ(r.media->bytes()[512], 0x11);
    CHECK_EQ(r.media->bytes()[1024], 0x33);
}

TEST(errors_range_write_protect_unit_and_unknown_commands)
{
    struct Case
    {
        uint8_t cmd;
        uint32_t lba;
        uint16_t count;
        uint8_t unit;
        uint8_t error;
    };
    const Case cases[] = {
        {SdCard::READ, 63, 2, 0, SdCard::ERR_RANGE},
        {SdCard::READ, 64, 1, 0, SdCard::ERR_RANGE},
        {SdCard::READ, 0, 0, 0, SdCard::ERR_RANGE}, // 0 = 65,536
        {SdCard::WRITE, 63, 2, 0, SdCard::ERR_RANGE},
        {SdCard::READ, 0, 1, 1, SdCard::ERR_UNIT},
        {SdCard::WRITE, 0, 1, 7, SdCard::ERR_UNIT},
        {SdCard::MOUNT, 0, 1, 0, SdCard::ERR_COMMAND}, // not yet
        {SdCard::UNMOUNT, 0, 1, 1, SdCard::ERR_COMMAND},
        {SdCard::LIST, 0, 1, 0, SdCard::ERR_COMMAND},
        {0x00, 0, 1, 0, SdCard::ERR_COMMAND},
        {0xFF, 0, 1, 0, SdCard::ERR_COMMAND},
    };
    for (const Case &c : cases)
    {
        Rig r;
        r.command(c.cmd, c.lba, c.count, c.unit);
        CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::ERR);
        CHECK_EQ(r.reg(SdCard::ERROR), c.error);
        CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    }
    Rig r;
    r.media->set_write_protected(true);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::WP);
    r.command(SdCard::WRITE, 0, 1);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_WP);
    r.command(SdCard::READ, 63, 1); // the last sector is fine, and reading a protected card
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY | SdCard::WP | SdCard::BUSY);
}

TEST(count_zero_is_65536_sectors)
{
    Rig r(0x10000);
    r.command(SdCard::READ, 0, 0);
    CHECK(r.has(SdCard::BUSY));
    r.card.tick(ACCESS);
    CHECK_EQ(r.read_sector(0), size_t{0});
    CHECK_EQ(r.count(), 0xFFFFu);
}

TEST(bufptr_jumps_within_the_sector)
{
    Rig r;
    r.command(SdCard::READ, 3, 2);
    r.card.tick(ACCESS + SECTOR_TIME);
    r.set(SdCard::BUFPTR, 0x10);
    r.set(SdCard::BUFPTR + 1, 0x01);
    CHECK_EQ(r.reg(SdCard::BUFPTR), 0x10);
    CHECK_EQ(r.reg(SdCard::BUFPTR + 1), 0x01);
    CHECK_EQ(r.reg(SdCard::DATA), pattern(3, 0x110));
    r.set(SdCard::BUFPTR, 0xFF); // the last byte: reading it ends the sector
    CHECK_EQ(r.reg(SdCard::DATA), pattern(3, 0x1FF));
    CHECK_EQ(r.reg(SdCard::BUFPTR + 1), 0x00);
    CHECK_EQ(r.lba(), 4u);
    CHECK_EQ(r.read_sector(4), size_t{0});
    CHECK(!r.has(SdCard::BUSY));
}

TEST(identify_describes_the_card)
{
    struct Big : SdMedia // 2.5 GB, SDHC, without the bytes
    {
        Big() : SdMedia("TEST.IMG") {}
        uint64_t sectors() const override { return 0x500000; }
        bool read(uint64_t, uint8_t *) override { return false; }
        bool write(uint64_t, const uint8_t *) override { return false; }
    };
    Rig r;
    auto big = std::make_shared<Big>();
    big->set_write_protected(true);
    r.card.insert(big, false);
    r.card.tick(INIT);
    r.command(SdCard::IDENTIFY, 99, 7, 3); // LBA, COUNT and UNIT don't matter
    r.card.tick(ACCESS - 1);
    CHECK(!r.has(SdCard::DATA_READY));
    r.card.tick(1);
    std::vector<uint8_t> id(SdCard::SECTOR);
    for (uint8_t &b : id)
        b = r.reg(SdCard::DATA);
    CHECK(!r.has(SdCard::BUSY));
    CHECK_EQ(id[0] | id[1] << 8 | id[2] << 16 | id[3] << 24, 0x500000);
    CHECK_EQ(id[4], 2);
    CHECK_EQ(id[5], 0x01);
    CHECK_EQ(id[6], SdCard::VERSION_MAJOR);
    CHECK_EQ(id[7], SdCard::VERSION_MINOR);
    CHECK_EQ(std::string(reinterpret_cast<const char *>(&id[0x10])), std::string("TEST.IMG"));
    size_t nonzero = 0;
    for (size_t i = 0x20; i < SdCard::SECTOR; ++i)
        nonzero += id[i] != 0;
    CHECK_EQ(nonzero, size_t{0}); // no units mounted
    CHECK_EQ(r.lba(), 99u);     // IDENTIFY moves nothing
    Rig small(64);
    small.command(SdCard::IDENTIFY, 0, 1);
    small.card.tick(ACCESS);
    for (unsigned i = 0; i < 4; ++i)
        small.reg(SdCard::DATA);
    CHECK_EQ(small.reg(SdCard::DATA), 1); // SDSC
}

TEST(a_command_written_while_busy_is_ignored)
{
    Rig r;
    r.command(SdCard::READ, 0, 1);
    r.command(SdCard::WRITE, 5, 1);
    r.card.tick(ACCESS);
    CHECK_EQ(r.lba(), 5u); // the registers took the writes
    CHECK_EQ(r.read_sector(0), size_t{0}); // but the read went on
    CHECK(!r.has(SdCard::BUSY));
}

TEST(flush_takes_access_us)
{
    Rig r;
    r.command(SdCard::FLUSH, 0, 0);
    CHECK(r.has(SdCard::BUSY));
    r.card.tick(ACCESS - 1);
    CHECK(r.has(SdCard::BUSY));
    r.card.tick(1);
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
}

TEST(taking_the_card_out_fails_the_command_and_both_changes_interrupt)
{
    Rig r;
    r.set(SdCard::IEN, SdCard::IRQ_CARD);
    CHECK(!r.card.irq());
    r.command(SdCard::READ, 0, 2);
    r.card.tick(ACCESS);
    r.card.eject();
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::ERR);
    CHECK_EQ(r.reg(SdCard::ERROR), SdCard::ERR_NO_CARD);
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE | SdCard::IRQ_CARD);
    CHECK(r.card.irq());
    r.set(SdCard::ISR, SdCard::IRQ_CARD);
    CHECK(!r.card.irq()); // DONE is pending but not enabled
    CHECK_EQ(r.reg(SdCard::ISR), SdCard::IRQ_DONE);
    r.card.insert(r.media);
    CHECK(r.card.irq());
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::ERR);
    r.card.tick(INIT);
    CHECK(r.has(SdCard::READY));
}

TEST(bus_reset_abandons_the_command_and_keeps_the_card_started)
{
    Rig r;
    r.set(SdCard::CTRL, SdCard::CTRL_DMA);
    r.set(SdCard::IEN, 0x03);
    r.command(SdCard::WRITE, 0, 2);
    for (size_t i = 0; i < SdCard::SECTOR; ++i)
        r.set(SdCard::DATA, 0x5A);
    r.card.tick(SECTOR_TIME);
    r.card.bus_reset();
    CHECK_EQ(r.reg(SdCard::STATUS), SdCard::PRESENT | SdCard::READY);
    CHECK_EQ(r.reg(SdCard::CTRL), 0);
    CHECK_EQ(r.reg(SdCard::IEN), 0);
    CHECK_EQ(r.reg(SdCard::ISR), 0);
    CHECK_EQ(r.lba(), 0u);
    CHECK_EQ(r.media->bytes()[0], 0x5A); // the programmed sector stays written
}

TEST(drq_follows_data_only_with_ctrl_dma)
{
    Rig r;
    r.command(SdCard::READ, 0, 1);
    r.card.tick(ACCESS);
    CHECK(!r.card.drq());
    r.set(SdCard::CTRL, SdCard::CTRL_DMA);
    CHECK(r.card.drq());
    for (size_t i = 0; i + 1 < SdCard::SECTOR; ++i)
        CHECK_EQ(r.card.dack_read(false), pattern(0, i));
    CHECK(r.card.drq());
    CHECK_EQ(r.card.dack_read(true), pattern(0, 511));
    CHECK(!r.card.drq());
    r.command(SdCard::WRITE, 1, 1);
    CHECK(r.card.drq());
    for (size_t i = 0; i < SdCard::SECTOR; ++i)
        r.card.dack_write(0x77, i + 1 == SdCard::SECTOR);
    CHECK(!r.card.drq());
    r.card.tick(SECTOR_TIME);
    CHECK_EQ(r.media->bytes()[512], 0x77);
}

TEST(a_file_card_is_written_through_and_a_read_only_file_is_protected)
{
    const std::string path = "test_sd_card.img";
    {
        std::ofstream out(path, std::ios::binary);
        const std::vector<uint8_t> bytes = image(4);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.write("tail", 4); // not a whole sector: left alone
    }
    std::string error;
    std::shared_ptr<FileMedia> media = FileMedia::open(path, error);
    REQUIRE(media != nullptr);
    CHECK_EQ(media->sectors(), uint64_t{4});
    CHECK_EQ(media->name(), std::string("test_sd_card.img"));
    CHECK(!media->write_protected());
    uint8_t sector[512];
    CHECK(media->read(2, sector));
    CHECK_EQ(sector[9], pattern(2, 9));
    CHECK(!media->read(4, sector));
    std::fill(std::begin(sector), std::end(sector), uint8_t{0xC3});
    CHECK(media->write(1, sector));
    CHECK(!media->write(4, sector));
    CHECK(media->flush());
    media.reset();
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK_EQ(back.size(), size_t{4 * 512 + 4});
        CHECK_EQ(back[512 + 100], 0xC3);
        CHECK_EQ(back[1024], pattern(2, 0));
        CHECK_EQ(std::string(back.end() - 4, back.end()), std::string("tail"));
    }
    std::FILE *tiny = std::fopen("test_sd_tiny.img", "wb");
    std::fputs("short", tiny);
    std::fclose(tiny);
    CHECK(FileMedia::open("test_sd_tiny.img", error) == nullptr);
    CHECK(FileMedia::open("test_sd_missing.img", error) == nullptr);
    CHECK_EQ(error, std::string("test_sd_missing.img: cannot open it"));
    std::remove(path.c_str());
    std::remove("test_sd_tiny.img");
}

namespace
{
    /// A backplane with RAM, the DMA controller and the SD card, the card's image in its slot.
    struct Board
    {
        Board() : media(std::make_shared<MemoryMedia>(image(64), "BOARD.IMG"))
        {
            std::string error;
            m = Machine::build("cpu w65c02s\n"
                               "northbridge backplane bootsel\n"
                               "ram 00000-FFFFF module=0\n"
                               "dma 400-47F space=io name=dma irq\n"
                               "sd  380-3FF space=io name=sd irq drq=0\n",
                               "sd.machine", {}, error);
            if (!m)
                std::printf("  %s\n", error.c_str());
            REQUIRE(m != nullptr);
            REQUIRE(m->sd_card() != nullptr);
            m->sd_card()->insert(media, false);
            for (const Mapping &map : m->bus().mappings())
                if (map.module == 0x0)
                    ram = map.device;
        }
        /// Run `prog` at $0200 until STP. @return the cycle the card went BUSY, and the one the IRQ came
        void run(const std::vector<uint8_t> &prog, uint64_t &busy_at, uint64_t &irq_at)
        {
            Decoder &bus = m->bus();
            for (size_t i = 0; i < prog.size(); ++i)
                bus.poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
            bus.reset();
            Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
            Registers regs;
            regs.pc = 0x0200;
            regs.p = 0x24; // I set: the IRQ only ends the WAI
            cpu.set_regs(regs);
            busy_at = irq_at = 0;
            for (int i = 0; i < 2'000'000 && cpu.state() != Cpu6502::State::Stopped; ++i)
            {
                cpu.step();
                if (!busy_at && (bus.peek(0xC381) & SdCard::BUSY))
                    busy_at = cpu.cycles();
                if (bus.irq())
                {
                    if (!irq_at)
                        irq_at = cpu.cycles();
                    cpu.irq();
                }
            }
            REQUIRE(cpu.state() == Cpu6502::State::Stopped);
        }

        std::unique_ptr<Machine> m;
        std::shared_ptr<MemoryMedia> media;
        Device *ram{nullptr};
    };

    /// Wait for READY, then LBA 5 and COUNT 4 on the card.
    const std::vector<uint8_t> READY_LBA5_COUNT4 = {
        0xA9, 0x40,                   // LDA #READY
        0x2C, 0x81, 0xC3,             // BIT STATUS
        0xF0, 0xFB,                   // BEQ the BIT: still starting up
        0xA9, 0x05, 0x8D, 0x86, 0xC3, // LDA #5; STA LBA
        0xA9, 0x00, 0x8D, 0x87, 0xC3, // LDA #0; STA LBA+1
        0x8D, 0x88, 0xC3,             //         STA LBA+2
        0x8D, 0x89, 0xC3,             //         STA LBA+3
        0xA9, 0x04, 0x8D, 0x8A, 0xC3, // LDA #4; STA COUNT
        0xA9, 0x00, 0x8D, 0x8B, 0xC3, // LDA #0; STA COUNT+1
    };

    /// DMA channel 0 at $20000 for 2,048 bytes, demand mode, `mode`'s direction; the card's
    /// CTRL.DMA and IEN.DONE, then `cmd`; WAI for the interrupt, STP.
    std::vector<uint8_t> dma_program(uint8_t mode, uint8_t cmd)
    {
        std::vector<uint8_t> p = READY_LBA5_COUNT4;
        const std::vector<uint8_t> rest = {
            0xA9, 0x00, 0x8D, 0x00, 0xC4, // LDA #0; STA ADDR0
            0x8D, 0x01, 0xC4,             //         STA ADDR0+1
            0xA9, 0x02, 0x8D, 0x02, 0xC4, // LDA #2; STA ADDR0+2    $20000
            0xA9, 0x00, 0x8D, 0x04, 0xC4, // LDA #0; STA COUNT0
            0xA9, 0x08, 0x8D, 0x05, 0xC4, // LDA #8; STA COUNT0+1   2,048
            0xA9, mode, 0x8D, 0x06, 0xC4, // LDA #mode; STA MODE0
            0xA9, 0x71, 0x8D, 0x41, 0xC4, // LDA #$71; STA CONTROL  DMA on, no burst limit
            0xA9, 0x01, 0x8D, 0x07, 0xC4, // LDA #1; STA CTRL0      ENABLE
            0x8D, 0x80, 0xC3,             //         STA CTRL       the card's DMA
            0x8D, 0x90, 0xC3,             //         STA IEN        command done
            0xA9, cmd, 0x8D, 0x83, 0xC3,  // LDA #cmd; STA CMD
            0xCB,                         // WAI
            0xDB,                         // STP
        };
        p.insert(p.end(), rest.begin(), rest.end());
        return p;
    }
} // namespace

TEST(a_cpu_loop_reads_sectors_checking_data_once_a_sector)
{
    Board b;
    std::vector<uint8_t> p = READY_LBA5_COUNT4;
    const std::vector<uint8_t> rest = {
        0xA9, 0x20, 0x8D, 0x83, 0xC3, // LDA #READ; STA CMD
        0xA9, 0x00, 0x85, 0x10,       // LDA #0;   STA $10
        0xA9, 0x30, 0x85, 0x11,       // LDA #$30; STA $11     to $3000
        0xA2, 0x04,                   // LDX #4               sectors
        0xA9, 0x08,                   // sector: LDA #DATA
        0x2C, 0x81, 0xC3,             // BIT STATUS
        0xF0, 0xFB,                   // BEQ the BIT
        0xA0, 0x00,                   // LDY #0
        0xAD, 0x8C, 0xC3,             // first half: LDA DATA
        0x91, 0x10,                   // STA ($10),Y
        0xC8,                         // INY
        0xD0, 0xF8,                   // BNE first half
        0xE6, 0x11,                   // INC $11
        0xAD, 0x8C, 0xC3,             // second half: LDA DATA
        0x91, 0x10,                   // STA ($10),Y
        0xC8,                         // INY
        0xD0, 0xF8,                   // BNE second half
        0xE6, 0x11,                   // INC $11
        0xCA,                         // DEX
        0xD0, 0xE0,                   // BNE sector
        0xDB,                         // STP
    };
    p.insert(p.end(), rest.begin(), rest.end());
    uint64_t busy_at = 0, irq_at = 0;
    b.run(p, busy_at, irq_at);
    CHECK(busy_at >= INIT); // it waited for READY
    size_t differ = 0;
    for (uint64_t n = 0; n < 4; ++n)
        for (size_t i = 0; i < 512; ++i)
            differ += b.ram->peek(static_cast<uint32_t>(0x3000 + n * 512 + i)) != pattern(5 + n, i);
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(b.m->bus().peek(0xC381), SdCard::PRESENT | SdCard::READY); // done
}

TEST(dma_channel_0_reads_four_sectors_at_a_byte_a_cycle)
{
    Board b;
    uint64_t busy_at = 0, irq_at = 0;
    b.run(dma_program(DmaController::DEMAND | DmaController::TO_MEMORY, SdCard::READ), busy_at, irq_at);
    size_t differ = 0;
    for (uint64_t n = 0; n < 4; ++n)
        for (size_t i = 0; i < 512; ++i)
            differ += b.ram->peek(static_cast<uint32_t>(0x20000 + n * 512 + i)) != pattern(5 + n, i);
    CHECK_EQ(differ, size_t{0});
    // The first sector after ACCESS_US, then the bus never waits: the spare buffer
    // fills in 25 us while the DMA takes 72 us over a sector.
    const uint64_t took = irq_at - busy_at;
    CHECK(took >= ACCESS + 2048);
    CHECK(took < ACCESS + 2048 + 40);
    CHECK_EQ(b.m->bus().peek(0xC440) & 0x01, 0x01); // channel 0 reached terminal count
}

TEST(dma_channel_0_writes_four_sectors)
{
    Board b;
    for (uint32_t i = 0; i < 2048; ++i)
        b.ram->write(0x20000 + i, static_cast<uint8_t>(i * 13 + 1));
    uint64_t busy_at = 0, irq_at = 0;
    b.run(dma_program(DmaController::DEMAND | DmaController::TO_CARD, SdCard::WRITE), busy_at, irq_at);
    size_t differ = 0;
    for (uint32_t i = 0; i < 2048; ++i)
        differ += b.media->bytes()[5 * 512 + i] != static_cast<uint8_t>(i * 13 + 1);
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(b.media->bytes()[4 * 512 + 511], pattern(4, 511)); // either side untouched
    CHECK_EQ(b.media->bytes()[9 * 512], pattern(9, 0));
    // Two sectors go into the buffers at once; after that a sector waits for one to be
    // programmed (25 us, 179 cycles, against 512 to fill), so the bus never waits either.
    const uint64_t took = irq_at - busy_at;
    CHECK(took >= 2048 + SECTOR_TIME);
    CHECK(took < 2048 + SECTOR_TIME + 40);
}

TEST(the_session_keeps_the_card_in_its_slot_through_a_power_cycle)
{
    std::string error;
    auto session = Session::create({EMU_MACHINE_DIR "/backplane.machine", EMU_BIOS_ROM, ""}, error);
    if (!session)
        std::printf("  %s\n", error.c_str());
    REQUIRE(session != nullptr);
    Session &s = *session;
    REQUIRE(s.sd() != nullptr);
    CHECK(s.sd_media() == nullptr);
    CHECK(!s.insert_sd(std::vector<uint8_t>(100), "short.img", error));
    CHECK_EQ(error, std::string("short.img: not an SD card image (less than one 512-byte sector)"));
    REQUIRE(s.insert_sd(image(8), "card.img", error));
    REQUIRE(s.sd_image() != nullptr);
    CHECK_EQ(s.sd_image()->sectors(), uint64_t{8});
    CHECK_EQ(s.sd()->read(SdCard::ISR), SdCard::IRQ_CARD);
    REQUIRE(s.power_cycle(error));
    CHECK(s.sd()->media() == s.sd_media());
    CHECK_EQ(s.sd()->read(SdCard::STATUS), SdCard::PRESENT); // starting up again
    CHECK_EQ(s.sd()->read(SdCard::ISR), 0);                  // no change: it was in at power-up
    s.eject_sd();
    CHECK(s.sd_media() == nullptr);
    CHECK_EQ(s.sd()->read(SdCard::STATUS), 0);
}

int main()
{
    return run_all();
}
