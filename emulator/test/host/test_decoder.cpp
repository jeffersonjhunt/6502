/**
 * @file test_decoder.cpp
 * @brief Address decoding: mappings, overlap, spaces, the north bridge, IRQ wiring, /WAIT, /DREQ.
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

TEST(a_mapping_larger_than_the_device_mirrors_it)
{
    Ram ram(0x100);
    Decoder bus;
    REQUIRE(bus.map({0x4000, 0x43FF, &ram}));      // 1 KB of decode for 256 bytes: four copies
    bus.write(0x4310, 0x5A);
    CHECK_EQ(ram.read(0x10), 0x5A);
    CHECK_EQ(bus.read(0x4010), 0x5A);
    CHECK_EQ(bus.peek(0x4210), 0x5A);
    bus.poke(0x41FF, 0x33);
    CHECK_EQ(ram.read(0xFF), 0x33);
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
    CHECK(!bus.map({0x0, 0x1000000, &ram}));      // past A23, into the I/O space
    CHECK(!bus.map({0x2000000, 0x200000F, &ram})); // past the I/O space, the last
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
    CHECK_EQ(bus.last_access().physical, 0xF00010u); // the flash at F00000
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

TEST(wait_stretches_a_dma_grant_and_a_fly_by_by_the_longer_side)
{
    // /WAIT (hw/design.md, "Wait states and the read budget"): the DMA controller obeys it in its grants. A memory
    // cycle lasts 1 + the memory's wait; a fly-by, which memory and the card answer in the one cycle on the one
    // open-drain line, 1 + the longer of the two, never the sum.
    struct Card : Ram
    {
        Card() : Ram(0x80) {}
        uint8_t dack_read(bool) override { return 0xC5; }
        void dack_write(uint8_t data, bool) override { got = data; }
        uint8_t got{0};
    };
    auto rig = [](uint8_t memory_wait, uint8_t card_wait, auto &&check) {
        Ram ram(0x10000);
        Card card;
        Decoder bus;
        bus.map({0x00000, 0x0FFFF, &ram, NO_MODULE, false, memory_wait});
        bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x3FF), &card, 0x7, false, card_wait});
        check(bus, ram, card);
    };
    rig(3, 0, [](Decoder &bus, Ram &ram, Card &card) {
        uint64_t t = bus.now();
        uint8_t v = 0;
        ram.write(0x1234, 0x42);
        CHECK_EQ(bus.dma_read(0x1234, v), 4u);  // memory to memory's read
        CHECK_EQ(v, 0x42);
        CHECK_EQ(bus.now() - t, 4u);
        t = bus.now();
        CHECK_EQ(bus.dma_write(0x1235, 0x43), 4u); // and its write
        CHECK_EQ(ram.peek(0x1235), 0x43);
        CHECK_EQ(bus.now() - t, 4u);
        CHECK_EQ(bus.dma_fly_by(0x1236, card, true, false), 4u); // the memory's side is the longer
        CHECK_EQ(ram.peek(0x1236), 0xC5);
    });
    rig(0, 2, [](Decoder &bus, Ram &ram, Card &card) {
        uint8_t v = 0;
        CHECK_EQ(bus.dma_read(0x1234, v), 1u);  // the card's /WAIT doesn't touch memory to memory
        const uint64_t t = bus.now();
        CHECK_EQ(bus.dma_fly_by(0x2000, card, true, false), 3u); // the card's side counts
        CHECK_EQ(bus.now() - t, 3u);
        ram.write(0x2001, 0x99);
        CHECK_EQ(bus.dma_fly_by(0x2001, card, false, false), 3u); // either way
        CHECK_EQ(card.got, 0x99);
    });
    rig(3, 2, [](Decoder &bus, Ram &, Card &card) {
        CHECK_EQ(bus.dma_fly_by(0x3000, card, true, false), 4u); // the longer, 3, not 3 + 2
    });
    rig(0, 0, [](Decoder &bus, Ram &, Card &card) {
        CHECK_EQ(bus.dma_fly_by(0x3000, card, true, false), 1u); // no /WAIT: one cycle a byte
    });
}

TEST(dreq_is_the_wired_cards_or_and_the_poll_finds_each_by_module)
{
    // DMA revision 2 (hw/design.md): a card wired to /DREQ (Mapping::drq) pulls the one shared line, and in the
    // poll its module's data line. Timed cards are caught up before the line is read (b2-plan.md [C15]); the
    // poll cycle is the controller's, unseen by the observer, with no /WAIT.
    struct Card : Ram
    {
        Card() : Ram(4) {}
        bool drq() const override { return want; }
        bool want{false};
    };
    struct Timed : Card
    {
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override
        {
            ticked += cycles;
            want = ticked >= 10; // ready after 10 cycles
        }
        uint64_t ticked{0};
    };
    struct Watch : CycleObserver
    {
        void on_cycle(const BusCycle &) override { ++seen; }
        unsigned seen{0};
    };
    Ram ram(0x100);
    Card a, unwired;
    Timed t;
    Watch watch;
    Decoder bus;
    bus.set_dma_observer(&watch);
    bus.map({0x0000, 0x00FF, &ram});
    bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x383), &a, 0x7, false, 2, Dreq::Wired});
    bus.map({bus_address(Space::Io, 0x500), bus_address(Space::Io, 0x503), &t, 0xA, false, 0, Dreq::Wired});
    bus.map({bus_address(Space::Io, 0x580), bus_address(Space::Io, 0x583), &unwired, 0xB});
    CHECK(!bus.dreq());
    unwired.want = true;
    CHECK(!bus.dreq());                                // not wired: never on /DREQ
    CHECK(bus.dreq_card(0xB) == nullptr);
    REQUIRE(bus.dreq_card(0x7) != nullptr);
    CHECK(bus.dreq_card(0x7)->device == &a);
    CHECK_EQ(bus.dreq_card(0x7)->wait, 2);             // its side of /WAIT in a grant
    CHECK(bus.dreq_card(0x8) == nullptr);              // the controller's own ID: none
    CHECK(bus.dreq_card(NO_MODULE) == nullptr);
    a.want = true;
    CHECK(bus.dreq());
    CHECK(bus.dreq_from(0x7));
    CHECK(!bus.dreq_from(0xA));
    CHECK(!bus.dreq_from(0xB));
    a.want = false;

    for (int i = 0; i < 12; ++i)
        bus.idle();                                    // time passes; nothing has ticked the timed card
    CHECK_EQ(t.ticked, uint64_t{0});
    CHECK(bus.dreq());                                 // caught up first: it is ready
    CHECK(t.ticked >= 10u);

    const uint64_t before = bus.now();
    CHECK_EQ(bus.dma_poll(), 1u);                      // one cycle, no /WAIT though module 7 holds 2
    CHECK_EQ(bus.now(), before + 1);
    CHECK(bus.dreq_from(0xA));
    CHECK(!bus.dreq_from(0x7));
    CHECK_EQ(watch.seen, 0u);                          // not reported, as the takeover isn't
}

// Word cycles (step B4, hw/design.md "16-bit transfers"): the DMA controller's WIDE moves a word on D0-D15 at an
// even address, in one bus cycle.

TEST(a_word_cycle_carries_the_even_byte_on_d0_d7_and_the_odd_on_d8_d15)
{
    // Must fail with the lanes swapped (a memory card with D0-D7 and D8-D15 crossed: every word load byte-swapped).
    struct Watch : CycleObserver
    {
        void on_cycle(const BusCycle &) override {}
    };
    Ram ram(0x10000);
    Watch watch;
    Decoder bus;
    bus.set_dma_observer(&watch); // last_access() describes a bus master's cycle while something watches
    bus.map({0x00000, 0x0FFFF, &ram});
    for (uint32_t i = 0; i < 0x100; ++i)
        ram.write(0x1200 + i, static_cast<uint8_t>(i * 3 + 1));
    uint16_t v = 0;
    uint64_t t = bus.now();
    CHECK_EQ(bus.dma_read_word(0x1234, v), 1u);
    CHECK_EQ(bus.now() - t, 1u);                                  // one cycle for both bytes
    CHECK_EQ(v, static_cast<uint16_t>(ram.peek(0x1235) << 8 | ram.peek(0x1234)));
    CHECK_EQ(v, 0xA09Du);                                         // $1235: $35 x 3 + 1 = $A0; $1234: $9D
    CHECK(bus.last_access().word);
    CHECK_EQ(bus.last_access().physical, 0x1234u);
    t = bus.now();
    CHECK_EQ(bus.dma_write_word(0x2000, 0xBEEF), 1u);
    CHECK_EQ(bus.now() - t, 1u);
    CHECK_EQ(ram.peek(0x2000), 0xEF);                             // the even byte, D0-D7
    CHECK_EQ(ram.peek(0x2001), 0xBE);                             // the odd, D8-D15
    CHECK(bus.last_access().word);
    uint8_t b = 0;
    bus.dma_read(0x2001, b);                                      // a byte cycle at an odd address: memory steers
    CHECK_EQ(b, 0xBE);
    CHECK(!bus.last_access().word);
}

TEST(a_word_cycle_takes_one_wait_stretch_the_longer_side)
{
    // /WAIT is one line and a word is one cycle: 1 + max(memory, card), once. Must fail if a word is done as two
    // byte cycles (a fly-by 8, a read 6): no speed gained, and a card's /WAIT counted twice.
    struct Card : Ram
    {
        Card() : Ram(0x80) {}
        uint16_t dack_read_word(bool) override { return 0x1234; }
        void dack_write_word(uint16_t data, bool) override { got = data; }
        uint16_t got{0};
    };
    Ram ram(0x10000);
    Card card;
    Decoder bus;
    bus.map({0x00000, 0x0FFFF, &ram, NO_MODULE, false, 2});
    bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x3FF), &card, 0x7, false, 3});
    uint64_t t = bus.now();
    CHECK_EQ(bus.dma_fly_by_word(0x4000, card, true, false), 4u); // the card's 3 is the longer
    CHECK_EQ(bus.now() - t, 4u);
    CHECK_EQ(ram.peek(0x4000), 0x34);
    CHECK_EQ(ram.peek(0x4001), 0x12);
    CHECK_EQ(bus.dma_fly_by_word(0x4000, card, false, true), 4u); // and from memory
    CHECK_EQ(card.got, 0x1234u);
    uint16_t v = 0;
    t = bus.now();
    CHECK_EQ(bus.dma_read_word(0x4000, v), 3u);                   // memory's 2 alone
    CHECK_EQ(bus.now() - t, 3u);
    CHECK_EQ(bus.dma_write_word(0x4002, v), 3u);
}

TEST(a_byte_only_card_in_a_word_grant_drives_d0_d7_only)
{
    // Device's default word hooks are a card with D0-D7 only, as the bus sees it: one register access a grant,
    // D8-D15 pulled up to memory, the odd byte from memory lost. Must fail if the default calls dack_read() twice
    // (two bytes taken from a register that moves once a grant).
    struct Card : Ram
    {
        Card() : Ram(0x80) {}
        uint8_t dack_read(bool tc) override
        {
            tcs.push_back(tc);
            return static_cast<uint8_t>(0xA0 + reads++);
        }
        void dack_write(uint8_t data, bool tc) override
        {
            tcs.push_back(tc);
            sent.push_back(data);
        }
        unsigned reads{0};
        std::vector<uint8_t> sent;
        std::vector<bool> tcs;
    };
    Ram ram(0x10000);
    Card card;
    Decoder bus;
    bus.map({0x00000, 0x0FFFF, &ram});
    bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x3FF), &card, 0x7});
    CHECK_EQ(bus.dma_fly_by_word(0x3000, card, true, false), 1u);
    CHECK_EQ(bus.dma_fly_by_word(0x3002, card, true, true), 1u);
    CHECK_EQ(card.reads, 2u);                                     // once a grant
    CHECK_EQ(ram.peek(0x3000), 0xA0);
    CHECK_EQ(ram.peek(0x3001), 0xFF);                             // D8-D15 float high
    CHECK_EQ(ram.peek(0x3002), 0xA1);
    CHECK_EQ(ram.peek(0x3003), 0xFF);
    ram.write(0x5000, 0x11);
    ram.write(0x5001, 0x22);
    CHECK_EQ(bus.dma_fly_by_word(0x5000, card, false, true), 1u);
    REQUIRE(card.sent.size() == 1u);                              // the even byte, once
    CHECK_EQ(card.sent[0], 0x11);
    CHECK(card.tcs == (std::vector<bool>{false, true, true}));    // TC passed through
}

TEST(an_unmapped_word_reads_ffff_and_the_observer_sees_one_cycle)
{
    // An emulator-model check: the observer (the Console card, a bus spy) sees a word cycle once, at the even
    // address, with both bytes. Must fail if it is told twice, or data_high is left 0. Unmapped: $FFFF, the
    // backplane's pull-ups.
    struct Watch : CycleObserver
    {
        void on_cycle(const BusCycle &c) override { seen.push_back(c); }
        std::vector<BusCycle> seen;
    };
    Ram ram(0x1000);
    Watch watch;
    Decoder bus;
    bus.set_dma_observer(&watch);
    bus.map({0x00000, 0x00FFF, &ram});
    uint16_t v = 0;
    CHECK_EQ(bus.dma_read_word(0x8000, v), 1u);
    CHECK_EQ(v, 0xFFFFu);
    REQUIRE(watch.seen.size() == 1u);
    CHECK(watch.seen[0].word);
    CHECK(watch.seen[0].dma);
    CHECK(!watch.seen[0].write);
    CHECK_EQ(watch.seen[0].addr, 0x8000u);
    CHECK_EQ(watch.seen[0].data, 0xFF);
    CHECK_EQ(watch.seen[0].data_high, 0xFF);
    CHECK_EQ(bus.dma_write_word(0x8002, 0x1234), 1u);             // dropped
    ram.write(0x0100, 0x5A);
    ram.write(0x0101, 0xC3);
    CHECK_EQ(bus.dma_read_word(0x0100, v), 1u);
    REQUIRE(watch.seen.size() == 3u);
    CHECK(watch.seen[1].write);
    CHECK_EQ(watch.seen[1].data, 0x34);
    CHECK_EQ(watch.seen[1].data_high, 0x12);
    CHECK_EQ(watch.seen[2].addr, 0x0100u);
    CHECK_EQ(watch.seen[2].data, 0x5A);
    CHECK_EQ(watch.seen[2].data_high, 0xC3);
    uint8_t b = 0;
    bus.dma_read(0x0101, b);                                      // a byte cycle: no word, no high byte
    REQUIRE(watch.seen.size() == 4u);
    CHECK(!watch.seen[3].word);
    CHECK_EQ(watch.seen[3].data_high, 0);
}

TEST(a_word_at_a_mapping_ending_even_floats_the_odd_byte)
{
    // The even address's mapping decodes the word cycle and answers both lanes (one chip select, as on the board),
    // so an odd byte past its end floats and its write is dropped, even where another mapping starts there. No
    // mapping in the repo ends even today. Must fail if the odd lane is looked up by its own address.
    Ram low(0x1001), high(0x1000);
    Decoder bus;
    bus.map({0x00000, 0x01000, &low});                            // ends at an even address
    bus.map({0x01001, 0x02000, &high});
    low.write(0x1000, 0x77);
    high.write(0x0000, 0x88);
    uint16_t v = 0;
    CHECK_EQ(bus.dma_read_word(0x1000, v), 1u);
    CHECK_EQ(v, 0xFF77u);                                         // D8-D15 float: the second Ram doesn't answer
    CHECK_EQ(bus.dma_write_word(0x1000, 0x2211), 1u);
    CHECK_EQ(low.peek(0x1000), 0x11);
    CHECK_EQ(high.peek(0x0000), 0x88);                            // dropped
    uint8_t b = 0;
    bus.dma_read(0x1001, b);                                      // a byte cycle there is the second Ram's
    CHECK_EQ(b, 0x88);
}

int main()
{
    return run_all();
}
