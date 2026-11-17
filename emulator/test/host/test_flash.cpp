/**
 * @file test_flash.cpp
 * @brief The flash (flash.h): ROM A's two chips, their commands, busy times and status.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md, "The flash: ROM A and ROM B" (step B5), is the spec: two
 * chips, even and odd bytes, each with its own command addresses (flash
 * offsets 2c and 2c + 1 for chip address c), its own busy time and its own
 * status, polled at an address of its parity. The cases drive the chips as
 * the BIOS's flash update will, through the north bridge's W1 on a ROM
 * block with RO clear, so every byte is a CPU cycle on the decoder whose
 * cycle count times the chips.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "decoder.h"
#include "flash.h"
#include "memory_devices.h"

#include <cstdint>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t NB = 0xC680; ///< the north bridge's registers
    constexpr uint16_t W1 = 0x4000; ///< where W1 shows its block

    /// The bridge on a decoder with RAM and ROM A as a Flash, as backplane.machine wires them.
    struct Rig
    {
        Rig() : bus(&bridge)
        {
            bridge.attach(&bus);
            bus.map({0x00000, 0xFFFFF, &ram, NO_MODULE});
            bus.map({bus_address(Space::Flash, 0x00000), bus_address(Space::Flash, 0x7FFFF), &rom_a, NO_MODULE});
            bus.map({bus_address(Space::Io, BackplaneBridge::REGISTERS),
                     bus_address(Space::Io, BackplaneBridge::REGISTERS + 0x7F), &bridge, BackplaneBridge::MODULE});
            bus.reset();
            rom_a.attach(&bus);
        }

        /// W1 on the ROM A block holding flash offset `o`, RO as asked; the CPU address of `o` there.
        uint16_t at(uint32_t o, bool ro = false)
        {
            bus.write(NB + 1, static_cast<uint8_t>(BackplaneBridge::ROM | (ro ? BackplaneBridge::RO : 0) | (o >> 14)));
            return static_cast<uint16_t>(W1 + (o & 0x3FFF));
        }
        /// A CPU store at flash offset `o`.
        void store(uint32_t o, uint8_t data, bool ro = false) { bus.write(at(o, ro), data); }
        /// A CPU load at flash offset `o` (W1 with RO set, as the update's reads are).
        uint8_t load(uint32_t o) { return bus.read(at(o, true)); }
        /// A store to chip `chip` (0 even, 1 odd) at its chip address `a`: flash offset 2a + chip.
        void to_chip(unsigned chip, uint32_t a, uint8_t data, bool ro = false) { store(a << 1 | chip, data, ro); }
        void unlock(unsigned chip, bool ro = false)
        {
            to_chip(chip, 0x5555, 0xAA, ro);
            to_chip(chip, 0x2AAA, 0x55, ro);
        }
        /// The whole program sequence for flash offset `o`.
        void program(uint32_t o, uint8_t data, bool ro = false)
        {
            const unsigned chip = o & 1;
            unlock(chip, ro);
            to_chip(chip, 0x5555, 0xA0, ro);
            store(o, data, ro);
        }
        void sector_erase(unsigned chip, uint32_t chip_address)
        {
            unlock(chip);
            to_chip(chip, 0x5555, 0x80);
            unlock(chip);
            to_chip(chip, chip_address, 0x30);
        }
        void chip_erase(unsigned chip)
        {
            unlock(chip);
            to_chip(chip, 0x5555, 0x80);
            unlock(chip);
            to_chip(chip, 0x5555, 0x10);
        }
        /// Poll flash offset `o` until two reads agree (bit 6 stops toggling). @return the reads, or 0
        /// if it never settles within `limit`
        unsigned poll(uint32_t o, unsigned limit = 1'000'000)
        {
            const uint16_t a = at(o, true);
            uint8_t last = bus.read(a);
            for (unsigned n = 2; n <= limit; ++n)
            {
                const uint8_t v = bus.read(a);
                if (v == last)
                    return n;
                last = v;
            }
            return 0;
        }
        void wait(uint64_t cycles)
        {
            for (uint64_t i = 0; i < cycles; ++i)
                bus.idle();
        }
        /// The array's byte, past any status (Rom's view).
        uint8_t array(uint32_t o) const { return rom_a.Rom::peek(o); }

        BackplaneBridge bridge;
        Decoder bus;
        Ram ram{0x100000};
        Flash rom_a{0x80000};
    };
} // namespace

// §4 case 1 (hw/design.md: each chip's command addresses are flash offsets 2c and 2c + 1). Seen failing
// under a one-chip decode: the commands compared on the flash offset, as one SST39SF040 would be wired.
TEST(a_program_takes_the_full_sequence_at_the_chips_own_addresses)
{
    Rig r;
    r.rom_a.poke(0x0100, 0xF5);
    r.rom_a.poke(0x0101, 0xF5);
    // The even chip's commands at $AAAA and $5554: the new byte is the old AND the data.
    r.store(0xAAAA, 0xAA);
    r.store(0x5554, 0x55);
    r.store(0xAAAA, 0xA0);
    r.store(0x0100, 0x3C);
    CHECK(r.poll(0x0100) != 0);
    CHECK_EQ(r.load(0x0100), 0x34);
    CHECK_EQ(r.load(0x0101), 0xF5); // the odd chip took nothing
    // The odd chip's at $AAAB and $5555.
    r.store(0xAAAB, 0xAA);
    r.store(0x5555, 0x55);
    r.store(0xAAAB, 0xA0);
    r.store(0x0101, 0x0F);
    CHECK(r.poll(0x0101) != 0);
    CHECK_EQ(r.load(0x0101), 0x05);
    CHECK_EQ(r.load(0x0100), 0x34);
    // One part's sequence on the flash offsets ($5555, $2AAA) reaches neither chip as a command.
    r.store(0x5555, 0xAA);
    r.store(0x2AAA, 0x55);
    r.store(0x5555, 0xA0);
    r.store(0x0200, 0x00);
    r.store(0x0201, 0x00);
    CHECK_EQ(r.array(0x0200), 0xFF);
    CHECK_EQ(r.array(0x0201), 0xFF);
    CHECK(!r.rom_a.busy(0) && !r.rom_a.busy(1));
    // A chip compares only A0-A14: $D555 is $5555 to it.
    r.to_chip(0, 0xD555, 0xAA);
    r.to_chip(0, 0x2AAA, 0x55);
    r.to_chip(0, 0x5555, 0xA0);
    r.store(0x0300, 0x81);
    CHECK(r.poll(0x0300) != 0);
    CHECK_EQ(r.load(0x0300), 0x81);
}

// §4 case 1a (fold: critic 3). An emulator-model check: a chip keeps its place in a sequence through reads,
// the other chip's cycles and /RESET, so one left after $A0 programs the next write anywhere; a $FF write
// programs nothing and an $F0 leaves it reading. Seen failing with the pending step forgotten on a read.
TEST(a_chip_left_after_a0_programs_the_next_write_wherever_it_lands)
{
    Rig r;
    r.rom_a.poke(0x8000, 0x5A);   // image 2's first byte, say
    r.rom_a.poke(0x0000, 0xC3);
    r.unlock(0);
    r.to_chip(0, 0x5555, 0xA0);   // left here, as a Break mid-byte would
    for (int i = 0; i < 10; ++i)
        (void)r.load(0x0000);     // reads don't end it
    r.store(0x0001, 0x00);        // nor the odd chip's cycles
    r.bus.reset();                // nor /RESET: the part has no reset pin
    r.store(0x8000, 0x0F);        // the next even write is programmed, wherever it lands
    CHECK(r.poll(0x8000) != 0);
    CHECK_EQ(r.array(0x8000), 0x0A);
    CHECK_EQ(r.array(0x0001), 0xFF); // the odd chip wasn't in a sequence: nothing
    // The update's reset: a $FF write (programs nothing), then $F0. A later stray write does nothing.
    r.unlock(0);
    r.to_chip(0, 0x5555, 0xA0);
    r.store(0x0000, 0xFF);
    CHECK(r.poll(0x0000) != 0);
    r.store(0x0000, 0xF0);
    r.store(0x8000, 0x00);
    CHECK_EQ(r.array(0x0000), 0xC3);
    CHECK_EQ(r.array(0x8000), 0x0A);
    // A $FF write also ends a half-sent erase.
    r.unlock(0);
    r.to_chip(0, 0x5555, 0x80);
    r.unlock(0);
    r.store(0x0000, 0xFF);
    r.to_chip(0, 0x0000, 0x30);
    CHECK_EQ(r.array(0x0000), 0xC3);
    CHECK(!r.rom_a.busy(0));
}

// §4 case 2. A sector erase clears one chip's 4 KB sector, 8 KB of flash with both, and nothing beside it; a
// 16 KB block is four erases. Seen failing with the sector taken from the flash offset's bits (4 KB).
TEST(a_sector_erase_clears_8_kb_with_both_chips_and_nothing_beside_it)
{
    Rig r;
    for (uint32_t o = 0; o < 0x10000; ++o)
        r.rom_a.poke(o, 0x00);
    // Chip address $1234 is in sector 1, chip addresses $1000-$1FFF: flash offsets $2000-$3FFF.
    r.sector_erase(0, 0x1234);
    CHECK(r.rom_a.busy(0));
    CHECK(!r.rom_a.busy(1));
    r.sector_erase(1, 0x1FFF);
    CHECK(r.poll(0x2000) != 0);
    CHECK(r.poll(0x2001) != 0);
    unsigned wrong = 0;
    for (uint32_t o = 0; o < 0x10000; ++o)
        wrong += r.array(o) != ((o >= 0x2000 && o < 0x4000) ? 0xFF : 0x00);
    CHECK_EQ(wrong, 0u);
    // One chip erased alone: only its bytes.
    r.sector_erase(0, 0x2000);
    CHECK(r.poll(0x4000) != 0);
    CHECK_EQ(r.array(0x4000), 0xFF);
    CHECK_EQ(r.array(0x4001), 0x00);
    CHECK_EQ(r.array(0x5FFE), 0xFF);
    CHECK_EQ(r.array(0x5FFF), 0x00);
    // Block 1 ($4000-$7FFF) is four erases: two sectors in each chip.
    r.sector_erase(1, 0x2000);
    r.sector_erase(0, 0x3000);
    CHECK(r.poll(0x4001) != 0);
    CHECK(r.poll(0x6000) != 0);
    r.sector_erase(1, 0x3000);
    CHECK(r.poll(0x6001) != 0);
    wrong = 0;
    for (uint32_t o = 0x4000; o < 0x8000; ++o)
        wrong += r.array(o) != 0xFF;
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(r.array(0x8000), 0x00);
    CHECK_EQ(r.array(0x1FFF), 0x00);
    // A chip erase clears that chip alone.
    r.chip_erase(1);
    CHECK(r.poll(0x0001, 10'000'000) != 0);
    CHECK_EQ(r.array(0x7FFFF), 0xFF);
    CHECK_EQ(r.array(0x8001), 0xFF);
    CHECK_EQ(r.array(0x8000), 0x00);
}

// §4 case 3. Each chip's status: bit 7 the inverse of the data's (0 in an erase), bit 6 toggling on every
// read, bits 0-5 the array's; the other chip reads its array meanwhile; a word cycle toggles each chip once.
// Seen failing with one status shared by both chips.
TEST(each_chip_polls_on_its_own_and_the_other_reads_meanwhile)
{
    Rig r;
    r.rom_a.poke(0x0011, 0x6B);
    r.program(0x0010, 0x15);         // bit 7 of the data 0: status bit 7 is 1
    REQUIRE(r.rom_a.busy(0));
    const uint8_t s1 = r.load(0x0010), s2 = r.load(0x0010);
    CHECK_EQ(s1 & 0x80, 0x80);
    CHECK_EQ((s1 ^ s2) & 0x40, 0x40); // bit 6 toggles
    CHECK_EQ(s1 & 0x3F, 0x15);        // the rest the array's (old $FF AND $15)
    CHECK_EQ(r.load(0x0011), 0x6B);   // the odd chip isn't busy: its array
    CHECK_EQ(r.load(0x0011), 0x6B);
    // A word cycle (the DMA controller's, WIDE) reads each chip once: the even byte's bit 6 flips each time.
    uint16_t w1 = 0, w2 = 0;
    r.bus.dma_read_word(0xF00010, w1);
    r.bus.dma_read_word(0xF00010, w2);
    CHECK_EQ((w1 ^ w2) & 0x0040, 0x0040);
    CHECK_EQ(w1 >> 8, 0x6B);
    CHECK_EQ(w2 >> 8, 0x6B);
    // Both busy: one word read toggles each chip once.
    r.program(0x0011, 0x80);         // bit 7 1: status bit 7 is 0
    REQUIRE(r.rom_a.busy(1));
    r.bus.dma_read_word(0xF00010, w1);
    r.bus.dma_read_word(0xF00010, w2);
    CHECK_EQ((w1 ^ w2) & 0x4040, 0x4040);
    CHECK_EQ(w1 & 0x8000, 0x0000);
    CHECK(r.poll(0x0010) != 0);
    CHECK(r.poll(0x0011) != 0);
    CHECK_EQ(r.load(0x0010), 0x15);
    CHECK_EQ(r.load(0x0011), 0x00);  // $6B AND $80
    // An erase's status: bit 7 0.
    r.sector_erase(1, 0);
    CHECK_EQ(r.load(0x0001) & 0x80, 0x00);
    CHECK_EQ(r.load(0x0010), 0x15);  // the even chip reads on
}

// §4 case 4. A busy chip ignores writes, a broken sequence returns to reading, and peek() doesn't toggle;
// an erase takes its second unlock pair, and a chip erase its $10, only at the command addresses.
// Seen failing with a busy chip accepting the next program, with the second unlock pair taken at any
// address, and with $10 taken at any address.
TEST(a_busy_chip_ignores_writes_and_a_broken_sequence_reads_again)
{
    Rig r;
    r.sector_erase(0, 0x0000);       // busy for 18 ms
    REQUIRE(r.rom_a.busy(0));
    r.program(0x0020, 0x00);         // ignored: the even chip is busy
    r.program(0x0021, 0x00);         // the odd chip takes its own
    CHECK(r.poll(0x0021) != 0);
    CHECK_EQ(r.array(0x0021), 0x00);
    const uint8_t p1 = r.rom_a.peek(0x0020), p2 = r.rom_a.peek(0x0020);
    CHECK_EQ(p1, p2);                // peek: the status, not toggled
    CHECK_EQ(p1 & 0x80, 0x00);
    CHECK(r.poll(0x0020) != 0);
    CHECK_EQ(r.array(0x0020), 0xFF); // the program sent while busy never happened
    CHECK(!r.rom_a.busy(0));
    // A wrong write mid-sequence: back to reading, so the $A0 after it starts nothing.
    r.unlock(0);
    r.to_chip(0, 0x1234, 0x00);
    r.to_chip(0, 0x5555, 0xA0);
    r.store(0x0040, 0x00);
    CHECK_EQ(r.array(0x0040), 0xFF);
    CHECK(!r.rom_a.busy(0));
    r.to_chip(0, 0x5555, 0xAA);
    r.to_chip(0, 0x2AAB, 0x55);      // the wrong address
    r.to_chip(0, 0x5555, 0xA0);
    r.store(0x0040, 0x00);
    CHECK_EQ(r.array(0x0040), 0xFF);

    // An erase's second unlock pair at a wrong chip address, either half: the $30 erases nothing.
    r.program(0x0060, 0x00);         // in sector 0, to show an erase
    CHECK(r.poll(0x0060) != 0);
    r.program(0x10000, 0x00);        // and in another sector, to show a chip erase
    CHECK(r.poll(0x10000) != 0);
    REQUIRE(r.array(0x0060) == 0x00);
    REQUIRE(r.array(0x10000) == 0x00);
    for (const uint32_t aa : {0x5554u, 0x5555u})
    {
        r.unlock(0);
        r.to_chip(0, 0x5555, 0x80);
        r.to_chip(0, aa, 0xAA);
        r.to_chip(0, aa == 0x5555 ? 0x2AABu : 0x2AAAu, 0x55); // one of the two wrong
        r.to_chip(0, 0x0030, 0x30);
        CHECK_EQ(r.array(0x0060), 0x00);
        CHECK(!r.rom_a.busy(0));
    }
    // The whole erase sequence, but $10 at an address other than $5555: no chip erase.
    r.unlock(0);
    r.to_chip(0, 0x5555, 0x80);
    r.unlock(0);
    r.to_chip(0, 0x5554, 0x10);
    CHECK_EQ(r.array(0x0060), 0x00);
    CHECK_EQ(r.array(0x10000), 0x00);
    CHECK(!r.rom_a.busy(0));
}

// §4 case 5. An emulator-model check: the busy times are the part's typical ones, in bus cycles at the CPU
// clock, and the change is in the array from the start. Seen failing with the clock taken as 1 MHz.
TEST(busy_times_are_the_parts_at_the_cpu_clock_and_the_change_lands_at_once)
{
    Rig r;
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::Program), 101u);      // 14 us at 7.16 MHz, rounded up
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::SectorErase), 128'864u);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::ChipErase), 501'137u);
    const auto lasts = [&r](auto start, unsigned chip) {
        start();
        const uint64_t t0 = r.bus.now();
        while (r.rom_a.busy(chip))
            r.bus.idle();
        return r.bus.now() - t0;
    };
    const uint64_t p = lasts([&r] { r.program(0x0100, 0x00); }, 0);
    CHECK(p >= 95 && p <= 101);      // less the cycles of the store and the bridge write after it started
    r.rom_a.poke(0x0102, 0xFF);
    r.program(0x0102, 0x12);
    CHECK(r.rom_a.busy(0));
    CHECK_EQ(r.array(0x0102), 0x12); // in the array already
    r.wait(200);
    const uint64_t s = lasts([&r] { r.sector_erase(1, 0); }, 1);
    CHECK(s >= 128'860 && s <= 128'864);
    const uint64_t c = lasts([&r] { r.chip_erase(0); }, 0);
    CHECK(c >= 501'130 && c <= 501'137);
    r.rom_a.set_cpu_hz(1'000'000);   // a slower clock: fewer cycles, the same time
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::Program), 14u);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::SectorErase), 18'000u);
    r.rom_a.set_cpu_hz(0);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::Program), 101u);
}

// §4 case 6. Through the machine's windows: with RO set a full sequence reaches neither chip; with RO clear it
// does. Seen failing with the bridge's read-only route not dropping the write.
TEST(a_read_only_window_keeps_every_command_from_the_chips)
{
    Rig r;
    r.program(0x0400, 0x00, true);
    r.program(0x0401, 0x00, true);
    CHECK(!r.rom_a.busy(0) && !r.rom_a.busy(1));
    CHECK_EQ(r.array(0x0400), 0xFF);
    CHECK_EQ(r.array(0x0401), 0xFF);
    r.sector_erase(0, 0); // not read-only: to show the same path does reach it
    CHECK(r.rom_a.busy(0));
    CHECK(r.poll(0) != 0);
    r.program(0x0400, 0x00);
    CHECK(r.poll(0x0400) != 0);
    CHECK_EQ(r.array(0x0400), 0x00);
}

// §4 case 7. An emulator-model check: software ID at chip addresses 0 and 1 (flash offsets 0 and 2 in the
// even chip, 1 and 3 in the odd), left by $F0 alone or after the unlock pair. Seen failing with $F0 not
// leaving ID mode.
TEST(software_id_reads_the_codes_at_chip_addresses_0_and_1_until_f0)
{
    Rig r;
    r.rom_a.poke(0, 0x11);
    r.rom_a.poke(1, 0x22);
    r.rom_a.poke(2, 0x33);
    r.rom_a.poke(3, 0x44);
    r.unlock(0);
    r.to_chip(0, 0x5555, 0x90);
    CHECK(r.rom_a.id_mode(0));
    CHECK_EQ(r.load(0), Flash::MAKER_ID);
    CHECK_EQ(r.load(2), Flash::PART_ID);
    CHECK_EQ(r.load(4), Flash::MAKER_ID); // chip address bit 0 chooses
    CHECK_EQ(r.load(1), 0x22);            // the odd chip reads its array
    CHECK_EQ(r.load(3), 0x44);
    r.unlock(1);
    r.to_chip(1, 0x5555, 0x90);
    CHECK_EQ(r.load(1), Flash::MAKER_ID);
    CHECK_EQ(r.load(3), Flash::PART_ID);
    r.store(0, 0xF0);                     // $F0 alone, at any address
    CHECK(!r.rom_a.id_mode(0));
    CHECK_EQ(r.load(0), 0x11);
    CHECK_EQ(r.load(2), 0x33);
    CHECK_EQ(r.load(1), Flash::MAKER_ID); // the odd chip still in ID mode
    r.unlock(1);
    r.to_chip(1, 0x5555, 0xF0);           // or after the unlock pair
    CHECK(!r.rom_a.id_mode(1));
    CHECK_EQ(r.load(1), 0x22);
    CHECK_EQ(r.load(3), 0x44);
}

#ifdef EMU_FAULT_HOOKS
// The test-only hooks §4's update cases use (U5): a byte that won't program, and a slower part.
TEST(the_test_hooks_make_a_byte_that_wont_program_and_a_slower_part)
{
    Rig r;
    r.rom_a.set_program_fault(0x0101);
    r.program(0x0101, 0x00);
    CHECK(r.poll(0x0101) != 0);
    CHECK_EQ(r.array(0x0101), 0xFF);
    r.rom_a.clear_program_fault();
    r.program(0x0101, 0x00);
    CHECK(r.poll(0x0101) != 0);
    CHECK_EQ(r.array(0x0101), 0x00);
    r.rom_a.set_busy_cycles(5000, 0, 0);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::Program), 5000u);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::SectorErase), 128'864u);
    r.program(0x0103, 0x00);
    r.wait(1000);
    CHECK(r.rom_a.busy(1));
    r.rom_a.set_busy_cycles(0, 0, 0);
    CHECK_EQ(r.rom_a.busy_cycles(Flash::Op::Program), 101u);
}
#endif

int main()
{
    return run_all();
}
