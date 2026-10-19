/**
 * @file test_decoder.cpp
 * @brief Address decoding: mappings, overlap, the north bridge, IRQ wiring.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "decoder.h"
#include "memory_devices.h"
#include "pia6821.h"

using namespace ooe::test;
using namespace ooe::emu;

TEST(device_sees_offset_not_system_address)
{
    Ram ram(0x100);
    Decoder bus;
    REQUIRE(bus.map({0x4000, 0x40FF, &ram}));
    bus.write(0x4010, 0x5A);
    CHECK_EQ(ram.read(0x10), 0x5A);
    CHECK_EQ(bus.read(0x4010), 0x5A);
}

TEST(unmapped_reads_ff_and_drops_writes)
{
    Ram ram(0x100);
    Decoder bus;
    bus.map({0x0000, 0x00FF, &ram});
    CHECK_EQ(bus.read(0x8000), Decoder::UNMAPPED_READ);
    bus.write(0x8000, 0x12); // nothing to crash into
    CHECK(bus.decode(0x8000) == nullptr);
}

TEST(later_mapping_wins_where_ranges_overlap)
{
    Rom rom(0x8000);
    Pia6821 pia;
    Decoder bus;
    bus.map({0x8000, 0xFFFF, &rom});
    bus.map({0xD010, 0xD013, &pia});
    bus.write(0xD011, 0x04);
    CHECK_EQ(pia.cra(), 0x04);
    CHECK(bus.decode(0xD00F)->device == &rom);
    CHECK(bus.decode(0xD014)->device == &rom);
}

TEST(rom_ignores_cpu_writes_but_takes_pokes)
{
    // Applesoft sizes RAM by finding a byte that will not change (Phase 1).
    Rom rom(0x8000);
    Decoder bus;
    bus.map({0x8000, 0xFFFF, &rom});
    bus.poke(0xE000, 0x4C);
    bus.write(0xE000, 0xB3);
    CHECK_EQ(bus.read(0xE000), 0x4C);
}

TEST(map_refuses_bad_ranges)
{
    Ram ram(0x10);
    Decoder bus;
    CHECK(!bus.map({0x10, 0x0F, &ram}));        // backwards
    CHECK(!bus.map({0x0, 0x100000, &ram}));     // past A19
    CHECK(!bus.map({0x0, 0xF, nullptr}));       // no device
    CHECK(bus.mappings().empty());
}

TEST(module_id_follows_the_mapping)
{
    Ram ram(0x8000);
    Rom rom(0x8000);
    Decoder bus;
    bus.map({0x0000, 0x7FFF, &ram, 0x0});
    bus.map({0x8000, 0xFFFF, &rom, 0x4});
    CHECK_EQ(bus.module_at(0x1234), 0x0);
    CHECK_EQ(bus.module_at(0x9000), 0x4);
}

TEST(north_bridge_places_cpu_addresses_in_physical_space)
{
    // A stand-in for the undecided A16-A19 rule: everything in bank 3.
    struct Bank3 : NorthBridge
    {
        uint32_t physical(uint16_t a) const override { return 0x30000u | a; }
    };
    const Bank3 bridge;
    Ram low(0x10000), high(0x10000);
    Decoder bus(&bridge);
    bus.map({0x00000, 0x0FFFF, &low});
    bus.map({0x30000, 0x3FFFF, &high});
    bus.write(0x0042, 0x99);
    CHECK_EQ(high.read(0x0042), 0x99);
    CHECK_EQ(low.read(0x0042), 0x00);
}

TEST(irq_reaches_the_cpu_only_when_wired)
{
    Pia6821 wired, loose;
    wired.write(1, Pia6821::CR_IRQ1_ENABLE);
    loose.write(1, Pia6821::CR_IRQ1_ENABLE);
    Decoder bus;
    bus.map({0xD000, 0xD003, &wired, NO_MODULE, true});
    bus.map({0xD010, 0xD013, &loose});
    loose.strobe_ca1();
    CHECK(!bus.irq());
    wired.strobe_ca1();
    CHECK(bus.irq());
}

TEST(peek_routes_like_read_without_side_effects)
{
    Ram ram(16);
    Pia6821 pia;
    Decoder bus;
    bus.map({0x0000, 0x000F, &ram});
    bus.map({0xD010, 0xD013, &pia});
    bus.write(0x0003, 0x42);
    bus.write(0xD011, Pia6821::CR_SELECT_OR);
    pia.strobe_ca1();
    CHECK_EQ(bus.peek(0x0003), 0x42);
    CHECK_EQ(bus.peek(0x8000), Decoder::UNMAPPED_READ);
    bus.peek(0xD010);
    CHECK(pia.cra() & Pia6821::CR_IRQ1); // a peek leaves key-ready alone
    bus.read(0xD010);
    CHECK(!(pia.cra() & Pia6821::CR_IRQ1));
}

TEST(tick_reaches_each_device_once)
{
    struct Counter : Ram
    {
        Counter() : Ram(16) {}
        void tick(unsigned cycles) override { total += cycles; }
        unsigned total{0};
    };
    Counter a, b;
    Decoder bus;
    bus.map({0x0000, 0x000F, &a});
    bus.map({0x0010, 0x001F, &a}); // a mirror: still one chip
    bus.map({0x0020, 0x002F, &b});
    bus.tick(5);
    CHECK_EQ(a.total, 5u);
    CHECK_EQ(b.total, 5u);
}

int main()
{
    return run_all();
}
