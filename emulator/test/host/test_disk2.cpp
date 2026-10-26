/**
 * @file test_disk2.cpp
 * @brief The Disk II card: its switches, drives and stepper, and the sequencer's wiring.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card's real sequencer PROM is Apple's (test_iie boots disks on it,
 * with the owner's files). Here the tests make their own tables, laid out
 * as disk2_card.h says the PROM is wired, so the plumbing is checked
 * everywhere: what reaches the PROM's address lines, and what its commands
 * do.
 */

#include "harness.h"

#include "decoder.h"
#include "disk2_card.h"
#include "disk_image.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct In
    {
        uint8_t state; ///< 0-15
        bool q7, q6, qa, pulse;
    };
    struct Out
    {
        uint8_t next; ///< 0-15
        uint8_t command;
    };

    /// A sequencer PROM file from a rule, each address wired as the card reads it.
    std::string sequencer(const std::string &name, const std::function<Out(In)> &rule)
    {
        std::string rom(256, '\0');
        for (unsigned s = 0; s < 16; ++s)
            for (unsigned k = 0; k < 16; ++k)
            {
                const In in{static_cast<uint8_t>(s), (k & 8) != 0, (k & 4) != 0, (k & 2) != 0, (k & 1) != 0};
                // state D7 D6 on A7 A6, D5 on A0, D4 on A5; /pulse on A4; Q7 A3, Q6 A2, QA A1
                const unsigned a = (s & 8 ? 0x80 : 0) | (s & 4 ? 0x40 : 0) | (s & 2 ? 0x01 : 0) | (s & 1 ? 0x20 : 0) |
                                   (in.pulse ? 0 : 0x10) | (in.q7 ? 0x08 : 0) | (in.q6 ? 0x04 : 0) | (in.qa ? 0x02 : 0);
                const Out out = rule(in);
                rom[a] = static_cast<char>(out.next << 4 | out.command);
            }
        std::ofstream(name, std::ios::binary) << rom;
        return name;
    }

    constexpr uint8_t CLR = 0x0, NOP = 0x8, SL0 = 0x9, SR = 0xA, LD = 0xB, SL1 = 0xD;

    /// A disk whose track 0 reads `pattern` over and over (1 = a flux transition).
    std::shared_ptr<DiskImage> disk_of(const std::string &pattern)
    {
        auto d = std::shared_ptr<DiskImage>(
            DiskImage::from_sectors(std::vector<uint8_t>(DiskImage::SECTOR_IMAGE, 0), DiskImage::Format::Dos));
        DiskTrack &t = d->track_for_writing(0);
        for (uint32_t n = 0; n < t.bit_count; ++n)
            t.set_bit(n, pattern[n % pattern.size()] == '1');
        d->mark_saved();
        return d;
    }

    struct Rig
    {
        Decoder bus;
        Disk2Card card;
        explicit Rig(const std::string &seq = "")
        {
            card.attach(&bus);
            std::string error;
            if (!seq.empty() && !card.load_roms("", seq, error))
                std::printf("  %s\n", error.c_str());
        }
        void cycles(uint64_t n)
        {
            for (uint64_t i = 0; i < n; ++i)
                bus.idle();
        }
        /// $C0E0 + n, read, as a bus cycle does.
        uint8_t read(unsigned n)
        {
            bus.idle();
            return card.slot_io(n, false, 0, true);
        }
        void write(unsigned n, uint8_t v)
        {
            bus.idle();
            card.slot_io(n, true, v, true);
        }
    };

    constexpr unsigned MOTOR_ON = 0x9, MOTOR_OFF = 0x8, DRIVE1 = 0xA, DRIVE2 = 0xB, Q6L = 0xC, Q6H = 0xD, Q7L = 0xE,
                       Q7H = 0xF;
    unsigned phase(unsigned p, bool on) { return 2 * p + (on ? 1 : 0); }
} // namespace

TEST(native_mode_sees_only_the_id_byte)
{
    Disk2Card c;
    CHECK_EQ(c.peek(0x7F), 0xC1);
    CHECK_EQ(c.read(0x7F), 0xC1);
    CHECK_EQ(c.peek(0x00), 0xFF);
    CHECK_EQ(c.peek(0x10), 0xFF);
    CHECK_EQ(c.slot_rom(0x00), 0xFF); // no boot ROM: slot 6 is empty
}

TEST(the_proms_are_256_bytes_and_the_boot_rom_is_slot_6s)
{
    Disk2Card c;
    std::string boot(256, '\0');
    for (int i = 0; i < 256; ++i)
        boot[static_cast<size_t>(i)] = static_cast<char>(i ^ 0x5A);
    std::ofstream("test_disk2_boot.rom", std::ios::binary) << boot;
    std::ofstream("test_disk2_short.rom", std::ios::binary) << std::string(100, '\0');
    std::string error;
    CHECK(!c.load_roms("test_disk2_short.rom", "", error));
    CHECK(error == "test_disk2_short.rom: want a 256-byte Disk II PROM, got 100 bytes");
    CHECK(!c.has_boot());
    REQUIRE(c.load_roms("test_disk2_boot.rom", "", error));
    CHECK(c.has_boot());
    CHECK(!c.has_sequencer());
    CHECK_EQ(c.slot_rom(0x00), 0x5A);
    CHECK_EQ(c.slot_rom(0xFF), 0xA5);
    std::remove("test_disk2_boot.rom");
    std::remove("test_disk2_short.rom");
}

TEST(the_sequencer_loads_the_data_bus_and_even_addresses_read_the_register)
{
    Rig r(sequencer("test_disk2_ld.rom", [](In) { return Out{0, LD}; }));
    r.write(MOTOR_ON, 0);
    r.write(Q6H, 0x5A);         // the byte on the card's data bus
    r.cycles(2);
    CHECK_EQ(r.read(Q6L), 0x5A); // even: the data register
    CHECK_EQ(r.read(Q6H), 0xFF); // odd: nothing drives the bus
    std::remove("test_disk2_ld.rom");
}

TEST(shift_right_brings_in_write_protect)
{
    Rig r(sequencer("test_disk2_sr.rom", [](In) { return Out{0, SR}; }));
    r.write(MOTOR_ON, 0);
    r.cycles(8);
    CHECK_EQ(r.read(Q6L), 0xFF); // no disk: protected
    r.card.insert(0, disk_of("1"));
    r.cycles(8);
    CHECK_EQ(r.read(Q6L), 0x00);
    r.card.disk(0)->set_write_protected(true);
    r.cycles(8);
    CHECK_EQ(r.read(Q6L), 0xFF);
    std::remove("test_disk2_sr.rom");
}

TEST(a_read_pulse_for_each_1_bit_once_a_cell)
{
    // pulse: shift in a 1; otherwise nothing. Each 4 us cell (4 cycles) with a 1 adds one bit.
    Rig r(sequencer("test_disk2_rp.rom", [](In in) { return Out{0, in.pulse ? SL1 : NOP}; }));
    r.card.insert(0, disk_of("10"));
    r.write(MOTOR_ON, 0);
    r.cycles(4 * 8 - 2);         // eight cells (32 cycles) with the switch and the read: four of them 1s
    CHECK_EQ(r.read(Q6L), 0x0F);
    CHECK(!r.card.disk(0)->changed()); // reading writes nothing
    std::remove("test_disk2_rp.rom");
}

namespace
{
    /// Pulses read in `cells` cells: Q6 clears, else each pulse shifts in a 1, counted 8 cells at a time.
    unsigned pulses(Rig &r, unsigned cells)
    {
        unsigned n = 0;
        for (unsigned w = 0; w < cells / 8; ++w)
        {
            r.write(Q6H, 0);
            r.write(Q6L, 0);
            r.cycles(4 * 8 - 2);
            uint8_t v = r.read(Q6L);
            for (; v; v &= static_cast<uint8_t>(v - 1))
                ++n;
        }
        return n;
    }
    std::string counting() { return sequencer("test_disk2_count.rom", [](In in) { return Out{0, in.q6 ? CLR : in.pulse ? SL1 : NOP}; }); }
} // namespace

TEST(past_three_0_bits_the_amplifier_reads_noise)
{
    Rig r(counting());
    r.write(MOTOR_ON, 0);
    r.card.insert(0, disk_of("1000"));    // three 0s at most: never noise
    CHECK_EQ(pulses(r, 800), 200u);
    r.card.insert(0, disk_of("10000000")); // seven: the last four are noise, 3 in 10 of them 1s
    const unsigned n = pulses(r, 800);
    CHECK(n > 100 + 60 && n < 100 + 180);
    r.card.insert(0, nullptr);            // an empty drive: noise all round
    const unsigned e = pulses(r, 800);
    CHECK(e > 160 && e < 320);
    std::remove("test_disk2_count.rom");
}

TEST(a_woz_disks_bit_timing_sets_the_cell)
{
    // WOZ 2 INFO "optimal bit timing" 28: 3.5 us cells, 7 steps, so 8/7 the cells of 4 us in the same time.
    std::vector<uint8_t> woz = disk_of("1")->to_woz();
    woz[12 + 8 + 39] = 28;
    woz[8] = woz[9] = woz[10] = woz[11] = 0; // no CRC
    std::string error;
    auto d = std::shared_ptr<DiskImage>(DiskImage::load(woz, "fast.woz", error));
    REQUIRE(d != nullptr);
    CHECK_EQ(d->bit_timing(), 28);
    DiskTrack &t = d->track_for_writing(0);
    for (uint32_t n = 0; n < t.bit_count; ++n)
        t.set_bit(n, n % 4 == 0);
    Rig r(counting());
    r.write(MOTOR_ON, 0);
    r.card.insert(0, d);
    const unsigned n = pulses(r, 800); // 200 at 4 us
    CHECK(n >= 224 && n <= 234);
    std::remove("test_disk2_count.rom");
}

TEST(a_long_wait_in_read_mode_lands_the_head_where_it_would_be)
{
    Rig r(counting());
    auto d = disk_of("10");
    DiskTrack &t = d->track_for_writing(0);
    for (uint32_t n = 3000; n < 3040; ++n) // a run of 1s, 3,000 cells (12,000 cycles) round
        t.set_bit(n, true);
    r.card.insert(0, d);
    r.write(MOTOR_ON, 0);                 // cell 0 under the head
    r.cycles(4 * 3000 + 8);               // nobody looks: skipped, not stepped
    CHECK_EQ(pulses(r, 8), 8u);           // in the run: a 1 every cell (elsewhere, every other)
    std::remove("test_disk2_count.rom");
}

TEST(the_register_bit_7_and_q6_q7_reach_the_prom)
{
    // Shift in 1s while QA is clear; once it is set, clear in read mode, load in "sense", NOP else.
    Rig r(sequencer("test_disk2_qa.rom", [](In in) {
        if (in.q7)
            return Out{0, in.q6 ? LD : SL0};
        if (in.q6)
            return Out{0, SR};
        return Out{0, in.qa ? CLR : SL1};
    }));
    r.card.insert(0, disk_of("1"));
    r.write(MOTOR_ON, 0);
    r.cycles(3);
    bool full = false, part = false; // filling with 1s: 01, 03 ... 7F, FF, then cleared once bit 7 is up
    for (int i = 0; i < 20; ++i)
    {
        const uint8_t a = r.read(Q6L);
        full |= a == 0xFF;
        part |= a == 0x3F || a == 0x1F || a == 0x0F;
    }
    CHECK(full); // reaches $FF: QA is bit 7, not lower
    CHECK(part);
    r.write(Q7H, 0x81);         // write mode, Q6 low: shift left in 0s
    r.cycles(5);
    CHECK_EQ(r.read(Q6L), 0x00); // (an even address: the register)
    std::remove("test_disk2_qa.rom");
}

TEST(writing_the_head_follows_state_bit_7)
{
    // In write mode count the state round: D7 changes every 8 steps, once a cell, so every cell gets a 1.
    Rig r(sequencer("test_disk2_wr.rom", [](In in) { return Out{static_cast<uint8_t>(in.q7 ? (in.state + 1) & 15 : 0), NOP}; }));
    r.card.insert(0, disk_of("0"));
    r.write(MOTOR_ON, 0);
    r.write(Q7H, 0);
    r.cycles(4 * 100);           // 100 cells
    r.write(Q7L, 0);
    CHECK(r.card.disk(0)->changed());
    const DiskTrack &t = *r.card.disk(0)->track(0);
    unsigned ones = 0;
    for (uint32_t n = 0; n < 120; ++n)
        ones += t.bit(n);
    CHECK(ones >= 98 && ones <= 101); // the cells it wrote in, give or take the ones at either end
    CHECK(!t.bit(200));

    auto p = disk_of("0");
    p->set_write_protected(true);
    r.card.insert(0, p);
    r.write(Q7H, 0);
    r.cycles(4 * 100);
    CHECK(!p->changed());        // protected: the head never writes
    CHECK(!p->track(0)->bit(50));
    std::remove("test_disk2_wr.rom");
}

TEST(the_head_steps_towards_the_magnets)
{
    Rig r;
    r.card.insert(0, disk_of("1"));
    r.write(phase(1, true), 0);  // motor off: the stepper is not powered
    CHECK_EQ(r.card.status().quarter_track[0], 0);
    r.write(phase(1, false), 0);
    r.write(MOTOR_ON, 0);
    const auto at = [&] { return r.card.status().quarter_track[0]; };
    r.write(phase(1, true), 0);
    CHECK_EQ(at(), 2);           // half a track in
    r.write(phase(0, false), 0);
    r.write(phase(2, true), 0);
    r.write(phase(1, false), 0);
    CHECK_EQ(at(), 4);           // track 1
    r.write(phase(3, true), 0);  // two magnets: between them
    CHECK_EQ(at(), 5);
    r.write(phase(2, false), 0);
    CHECK_EQ(at(), 6);
    r.write(phase(1, true), 0);  // opposite magnets: no pull either way
    CHECK_EQ(at(), 6);
    r.write(phase(1, false), 0);
    r.write(phase(3, false), 0);
    for (unsigned p : {2u, 1u, 0u, 3u, 2u, 1u, 0u, 3u}) // back out, past track 0
    {
        r.write(phase(p, true), 0);
        r.write(phase((p + 1) % 4, false), 0);
    }
    CHECK_EQ(at(), 0);           // the stop at track 0
}

TEST(the_motor_runs_on_a_second_after_it_is_switched_off)
{
    Rig r;
    r.write(MOTOR_ON, 0);
    CHECK(r.card.status().motor);
    r.write(MOTOR_OFF, 0);
    CHECK(!r.card.status().motor);
    r.cycles(Disk2Card::MOTOR_DELAY - 10);
    CHECK(r.card.status().spinning);
    r.write(MOTOR_ON, 0);        // on again before it stopped: it never does
    r.cycles(2 * Disk2Card::MOTOR_DELAY);
    CHECK(r.card.status().spinning);
    r.write(MOTOR_OFF, 0);
    r.cycles(Disk2Card::MOTOR_DELAY + 1);
    CHECK(!r.card.status().spinning);
}

TEST(drive_2_has_its_own_disk_and_head)
{
    // Q6 on clears the register; then count pulses for eight cells.
    Rig r(sequencer("test_disk2_d2.rom", [](In in) { return Out{0, in.q6 ? CLR : in.pulse ? SL1 : NOP}; }));
    const auto eight_cells = [&] {
        r.write(Q6H, 0);
        r.write(Q6L, 0);
        r.cycles(4 * 8 - 2);
        return r.read(Q6L);
    };
    r.card.insert(0, disk_of("1"));
    r.card.insert(1, disk_of("1000"));
    r.write(MOTOR_ON, 0);
    r.write(DRIVE2, 0);
    r.write(phase(1, true), 0);  // drive 2's head moves; drive 1's stays
    CHECK_EQ(r.card.status().drive, 1);
    CHECK_EQ(r.card.status().quarter_track[1], 2);
    CHECK_EQ(r.card.status().quarter_track[0], 0);
    r.write(phase(1, false), 0);
    r.write(phase(0, true), 0);  // back to track 0, where drive 2's pattern is
    r.write(phase(0, false), 0);
    CHECK_EQ(eight_cells(), 0x03); // two pulses in eight cells
    r.write(DRIVE1, 0);
    CHECK_EQ(eight_cells(), 0xFF);
    std::remove("test_disk2_d2.rom");
}

TEST(reset_clears_the_latch_and_peeks_change_nothing)
{
    Rig r;
    r.write(MOTOR_ON, 0);
    r.write(DRIVE2, 0);
    r.write(Q6H, 0);
    r.write(Q7H, 0);
    r.write(phase(2, true), 0);
    r.card.slot_io(Q6L, false, 0, false); // a peek: Q6 stays on
    r.card.slot_io(MOTOR_OFF, false, 0, false);
    Disk2Card::Status s = r.card.status();
    CHECK(s.q6 && s.q7 && s.motor && s.drive == 1 && s.phases == 4);
    r.card.reset();
    s = r.card.status();
    CHECK(!s.q6 && !s.q7 && !s.motor && s.drive == 0 && s.phases == 0);
    CHECK(s.spinning);           // the motor runs on
}

int main() { return run_all(); }
