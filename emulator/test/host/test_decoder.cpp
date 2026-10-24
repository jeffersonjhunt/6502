/**
 * @file test_decoder.cpp
 * @brief Address decoding: mappings, overlap, spaces, the north bridge, IRQ wiring, RDY wait states.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "cpu6502.h"
#include "decoder.h"
#include "memory_devices.h"
#include "pia6821.h"
#include "via6522.h"

#include <vector>

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
    CHECK(!bus.map({0x0, 0x100000, &ram}));     // past A19, into the next space
    CHECK(!bus.map({0x300000, 0x30000F, &ram})); // past the last space
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
    // A stand-in rule: everything in bank 3.
    struct Bank3 : NorthBridge
    {
        Route route(uint16_t a, bool, bool) const override { return Route{0x30000u | a, true}; }
    };
    Bank3 bridge;
    Ram low(0x10000), high(0x10000);
    Decoder bus(&bridge);
    bus.map({0x00000, 0x0FFFF, &low});
    bus.map({0x30000, 0x3FFFF, &high});
    bus.write(0x0042, 0x99);
    CHECK_EQ(high.read(0x0042), 0x99);
    CHECK_EQ(low.read(0x0042), 0x00);
}

TEST(routes_reach_other_spaces_and_honour_read_only)
{
    // $0xxx -> flash, read-only; $1xxx -> I/O; VPB sends $Fxxx to flash too.
    struct Split : NorthBridge
    {
        Route route(uint16_t a, bool vpb, bool) const override
        {
            if (a < 0x1000 || (vpb && a >= 0xF000))
                return Route{bus_address(Space::Flash, a), false};
            if (a < 0x2000)
                return Route{bus_address(Space::Io, a & 0x7FF), true};
            return Route{a, true};
        }
    };
    Split bridge;
    Ram mem(0x10000), io(0x800);
    Rom flash(0x10000);
    Decoder bus(&bridge);
    bus.map({0x00000, 0x0FFFF, &mem, 0x0});
    bus.map({bus_address(Space::Flash, 0), bus_address(Space::Flash, 0xFFFF), &flash, 0x4});
    bus.map({bus_address(Space::Io, 0), bus_address(Space::Io, 0x7FF), &io, 0x6});

    bus.poke(0x0010, 0xA5);                // the loader reaches flash
    CHECK_EQ(bus.read(0x0010), 0xA5);
    CHECK_EQ(bus.last_access().physical, 0x00010u);
    CHECK_EQ(bus.last_access().module, 0x4);
    flash.poke(0x0011, 0x00);
    bus.write(0x0011, 0xFF);               // read-only route: dropped
    CHECK_EQ(flash.read(0x0011), 0x00);

    bus.write(0x1803, 0x77);               // I/O: A0-A19 carry the CPU address
    CHECK_EQ(io.read(0x003), 0x77);
    CHECK_EQ(bus.last_access().physical, 0x0C003u);
    CHECK_EQ(bus.physical(0x1803), 0x0C003u);

    mem.write(0xFFFC, 0x11);
    flash.poke(0xFFFC, 0x22);
    CHECK_EQ(bus.read(0xFFFC), 0x11);      // an ordinary read: memory
    bus.vector_pull(true);
    CHECK_EQ(bus.read(0xFFFC), 0x22);      // VPB: the bridge's vector route
    bus.vector_pull(false);
    CHECK_EQ(bus.module_at(0xFFFC, true), 0x4);
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

TEST(timed_devices_keep_time_in_lockstep_with_bus_cycles)
{
    /// Reads back how many cycles it has been ticked through.
    struct Clock : Ram
    {
        Clock() : Ram(16) {}
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override { total += cycles; }
        uint8_t read(uint32_t) override { return static_cast<uint8_t>(total); }
        uint8_t peek(uint32_t) const override { return static_cast<uint8_t>(total); }
        uint64_t total{0};
    };
    struct Untimed : Ram
    {
        Untimed() : Ram(16) {}
        void tick(uint64_t cycles) override { total += cycles; }
        uint64_t total{0};
    };
    Clock clock;
    Untimed ram;
    Decoder bus;
    bus.map({0x0000, 0x000F, &ram});
    bus.map({0x0010, 0x001F, &clock});
    bus.map({0x0020, 0x002F, &clock}); // a mirror: still one chip, one clock

    for (int i = 0; i < 5; ++i)
        bus.read(0x0000);              // cycles 1-5
    CHECK_EQ(bus.read(0x0010), 5);     // cycle 6: caught up through cycle 5
    bus.write(0x0001, 0);              // 7
    bus.idle();                        // 8: WAI
    bus.idle();                        // 9
    CHECK_EQ(bus.read(0x0020), 9);     // 10, through the mirror
    CHECK_EQ(bus.now(), 10u);
    bus.irq();                         // brings every timed device to now()
    CHECK_EQ(clock.total, 10u);
    bus.poke(0x0000, 1);               // the loader and debugger take no cycles
    CHECK_EQ(bus.peek(0x0010), 10);
    CHECK_EQ(bus.now(), 10u);
    CHECK_EQ(ram.total, 0u);           // untimed: never ticked
}

TEST(wait_states_hold_one_access_for_more_cycles)
{
    /// Counts accesses, and reads back how far it has been ticked.
    struct Slow : Ram
    {
        Slow() : Ram(16) {}
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override { total += cycles; }
        uint8_t read(uint32_t) override
        {
            ++reads;
            return static_cast<uint8_t>(total);
        }
        uint64_t total{0};
        int reads{0};
    };
    Ram ram(0x100);
    Slow slow;
    Decoder bus;
    bus.map({0x0000, 0x00FF, &ram});
    bus.map({0x8000, 0x800F, &slow, NO_MODULE, false, 3});
    bus.read(0x0000);
    CHECK_EQ(bus.wait_states(), 0u);
    CHECK_EQ(bus.now(), 1u);
    CHECK_EQ(bus.read(0x8000), 4);     // taken at the end: ticked through cycles 1-4
    CHECK_EQ(bus.wait_states(), 3u);
    CHECK_EQ(bus.now(), 5u);           // 1 + (1 + 3)
    CHECK_EQ(slow.reads, 1);           // one access, however long
    bus.write(0x8001, 0);
    CHECK_EQ(bus.now(), 9u);
    bus.read(0x0001);
    CHECK_EQ(bus.wait_states(), 0u);   // per access, not sticky
}

TEST(cpu_counts_wait_states_and_observes_the_last_cycle)
{
    struct Seen : CycleObserver
    {
        void on_cycle(const BusCycle &c) override { cycles.push_back(c.cycle); }
        std::vector<uint64_t> cycles;
    };
    Ram ram(0x8000), slow(0x100);
    Decoder bus;
    bus.map({0x0000, 0x7FFF, &ram});
    bus.map({0x8000, 0x80FF, &slow, NO_MODULE, false, 3});
    const uint8_t prog[] = {
        0xAD, 0x00, 0x80, // LDA $8000: 4 accesses, the last (a read) held 3
        0x8D, 0x01, 0x80, // STA $8001: the same, the last a write
    };
    for (unsigned i = 0; i < sizeof prog; ++i)
        bus.poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    Seen seen;
    cpu.set_observer(&seen);
    Registers r;
    r.pc = 0x0200;
    cpu.set_regs(r);
    CHECK_EQ(cpu.step(), 7u);
    CHECK_EQ(cpu.step(), 7u);
    CHECK(seen.cycles == (std::vector<uint64_t>{1, 2, 3, 7, 8, 9, 10, 14}));
    CHECK_EQ(slow.read(1), 0x00);      // LDA got $00 from the slow RAM; stored once
    CHECK_EQ(bus.now(), cpu.cycles());   // the decoder's clock and the CPU's agree
}

TEST(a_timer_runs_through_wait_states)
{
    // T1 counts down while the CPU is held, so a program that reads it after
    // a waited access sees exactly the extra cycles gone.
    auto run = [](uint8_t wait) {
        Ram ram(0x6000), slow(0x100);
        Via6522 via;
        Decoder bus;
        bus.map({0x0000, 0x5FFF, &ram});
        bus.map({0x6000, 0x600F, &via});
        bus.map({0x8000, 0x80FF, &slow, NO_MODULE, false, wait});
        const uint8_t prog[] = {
            0xA9, 0xFF, 0x8D, 0x04, 0x60, // LDA #$FF; STA T1L-L
            0xA9, 0x00, 0x8D, 0x05, 0x60, // LDA #$00; STA T1C-H: load and start
            0xAD, 0x00, 0x80,             // LDA $8000 (waited)
            0xAD, 0x04, 0x60,             // LDA T1C-L
            0x85, 0x10,                   // STA $10
            0xDB,                         // STP
        };
        for (unsigned i = 0; i < sizeof prog; ++i)
            bus.poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
        Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
        Registers r;
        r.pc = 0x0200;
        cpu.set_regs(r);
        for (int i = 0; i < 20 && cpu.state() != Cpu6502::State::Stopped; ++i)
            cpu.step();
        return bus.peek(0x0010);
    };
    const uint8_t plain = run(0);
    CHECK_EQ(run(3), static_cast<uint8_t>(plain - 3));
    CHECK_EQ(run(15), static_cast<uint8_t>(plain - 15));
}

int main()
{
    return run_all();
}
