/**
 * @file test_es5503.cpp
 * @brief The ES5503 (the DOC) alone (hw/sound-rev2.md, step 6).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The chip with no card: the output rate against the oscillators enabled;
 * the pitch from frequency, table size and resolution, and where each byte
 * comes from; the four modes (free run's loop with its phase kept, one-shot's
 * halt, swap's handover to its partner, sync's restart and AM's volume); a
 * $00 byte halting in every mode; the bank bit, kept over a later pointer
 * write; register reads; $E0's acknowledgement order and the interrupt line;
 * halts written from the CPU; /RESET; no triple mix of the top oscillator; and
 * a golden hash of a busy run, which WebAssembly's build must make too.
 */

#include "harness.h"

#include "es5503.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    using R = Es5503::Reg;

    /// Oscillator `n`: frequency, volume, pointer, table (bank, size, resolution), then control.
    void osc(Es5503 &d, unsigned n, uint16_t freq, uint8_t vol, uint8_t pointer, uint8_t table, uint8_t control)
    {
        d.write(static_cast<uint8_t>(R::FREQ_LO + n), static_cast<uint8_t>(freq));
        d.write(static_cast<uint8_t>(R::FREQ_HI + n), static_cast<uint8_t>(freq >> 8));
        d.write(static_cast<uint8_t>(R::VOLUME + n), vol);
        d.write(static_cast<uint8_t>(R::POINTER + n), pointer);
        d.write(static_cast<uint8_t>(R::TABLE + n), table);
        d.write(static_cast<uint8_t>(R::CONTROL + n), control);
    }

    void enable(Es5503 &d, unsigned n) { d.write(R::ENABLE, static_cast<uint8_t>((n - 1) << 1)); }

    uint8_t table(unsigned size, unsigned res, bool bank = false)
    {
        return static_cast<uint8_t>((bank ? Es5503::TABLE_BANK : 0) | size << 3 | res);
    }

    uint8_t control(unsigned mode, unsigned channel = 0, bool ie = false, bool halt = false)
    {
        return static_cast<uint8_t>(channel << 4 | (ie ? Es5503::CONTROL_IE : 0) | mode << 1 | (halt ? 1 : 0));
    }

    /// `n` bytes at `at`, none $00: a ramp from 1.
    void ramp(Es5503 &d, uint32_t at, uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i)
            d.ram()[at + i] = static_cast<uint8_t>(1 + i % 255);
    }

    bool halted(Es5503 &d, unsigned n) { return d.peek(static_cast<uint8_t>(R::CONTROL + n)) & 1; }
} // namespace

TEST(the_output_rate_follows_the_oscillators_enabled)
{
    // (7,159,090 / 8) / (enabled + 2): 8 x (enabled + 2) of the chip's clocks a sample. $E1 reads back.
    Es5503 d;
    for (unsigned n = 1; n <= 32; ++n)
    {
        enable(d, n);
        CHECK_EQ(d.enabled(), n);
        CHECK_EQ(d.peek(R::ENABLE), static_cast<uint8_t>((n - 1) << 1));
        CHECK_EQ(d.clocks_per_sample(), 8 * (n + 2));
        CHECK_EQ(d.output_hz(), Es5503::CLOCK_HZ / (8 * (n + 2)));
    }
    std::printf("  32 enabled: %u Hz; 1: %u Hz\n", d.output_hz(), (enable(d, 1), d.output_hz()));
    enable(d, 32);
    CHECK_EQ(d.output_hz(), 26'320u);
    enable(d, 1);
    CHECK_EQ(d.output_hz(), 298'295u);
    // Only the enabled ones step: oscillator 5 runs only once 6 are enabled.
    ramp(d, 0, 256);
    osc(d, 5, 0x100, 255, 0, table(0, 0), control(Es5503::MODE_FREE));
    enable(d, 5);
    d.step();
    CHECK_EQ(d.accumulator(5), 0u);
    enable(d, 6);
    d.step();
    CHECK_EQ(d.accumulator(5), 0x100u);
}

TEST(pitch_follows_frequency_size_and_resolution)
{
    // Each step's byte is the table's at (accumulator >> (9 + resolution - size)), its size masked, the table at
    // pointer x 256 with the size's low bits dropped; and the table loops every (size - 1) << shift of
    // accumulator: (size - 1) << shift / frequency steps a loop.
    bool bytes = true;
    unsigned cases = 0;
    for (unsigned size = 0; size < 8; ++size)
        for (unsigned res = 0; res < 8; ++res)
            for (const uint16_t freq : {uint16_t{0x0040}, uint16_t{0x0400}, uint16_t{0x2345}})
            {
                Es5503 d;
                const uint32_t bytes_n = 256u << size, shift = 9 + res - size;
                for (uint32_t i = 0; i < Es5503::RAM_BYTES; ++i)
                    d.ram()[i] = static_cast<uint8_t>(1 + (i * 7 + i / 251) % 255);
                const uint8_t pointer = 0xA7; // its low bits dropped for the bigger tables
                const uint32_t base = (uint32_t{pointer} << 8) & 0x1FF00u & ~(bytes_n - 1);
                osc(d, 0, freq, 100, pointer, table(size, res), control(Es5503::MODE_FREE));
                uint64_t loops = 0;
                const unsigned steps = 3'000;
                for (unsigned k = 0; k < steps; ++k)
                {
                    const uint32_t acc = d.accumulator(0);
                    d.step();
                    const uint32_t index = (acc >> shift) & (bytes_n - 1);
                    if (d.peek(static_cast<uint8_t>(R::DATA)) != d.ram()[base + index])
                        bytes = false;
                    if (d.accumulator(0) < acc)
                        ++loops;
                }
                const uint64_t span = uint64_t{bytes_n - 1} << shift;
                const uint64_t want = uint64_t{steps} * freq / span;
                CHECK(loops + 1 >= want && loops <= want + 1);
                if (size == 2 && res == 3 && freq == 0x2345)
                    std::printf("  size %u res %u freq $%04X: %llu loops in %u steps (want %llu)\n", size, res, freq,
                                static_cast<unsigned long long>(loops), steps, static_cast<unsigned long long>(want));
                ++cases;
            }
    std::printf("  %u size, resolution and frequency cases: each byte %s\n", cases, bytes ? "from its place" : "WRONG");
    CHECK(bytes);
}

TEST(free_run_loops_with_its_phase_kept_and_one_shot_halts)
{
    // A 256-byte table at resolution 0 (shift 9), frequency $180: the step whose index is 255 subtracts 255 << 9,
    // keeping the phase. One-shot halts there instead, and is silent after.
    for (const unsigned mode : {unsigned{Es5503::MODE_FREE}, unsigned{Es5503::MODE_ONE_SHOT}})
    {
        Es5503 d;
        ramp(d, 0, 256);
        osc(d, 0, 0x180, 255, 0, table(0, 0), control(mode));
        uint32_t acc = 0;
        unsigned ends = 0;
        for (unsigned k = 0; k < 2'000; ++k)
        {
            const bool was_halted = halted(d, 0);
            d.step();
            if (was_halted)
            {
                CHECK_EQ(d.out()[0], 0);
                continue;
            }
            const uint32_t index = acc >> 9;
            acc += 0x180;
            if (index >= 255)
            {
                ++ends;
                if (mode == Es5503::MODE_FREE)
                    acc -= 255u << 9;
            }
            if (mode == Es5503::MODE_FREE || !ends)
                CHECK_EQ(d.accumulator(0), acc & 0xFFFFFFu);
        }
        std::printf("  %s: %u ends of the table, %s\n", mode == Es5503::MODE_FREE ? "free run" : "one-shot", ends,
                    halted(d, 0) ? "halted" : "running");
        CHECK_EQ(halted(d, 0), mode == Es5503::MODE_ONE_SHOT);
        CHECK(mode == Es5503::MODE_FREE ? ends == 5 : ends == 1); // 2,000 x $180 / (255 << 9) = 5.9
    }
}

TEST(a_carry_past_the_top_still_ends_the_table)
{
    // A 32 KB table at resolution 7 (shift 9): its end is an index of 32,767, the accumulator's top 512. A frequency
    // of $FFFD steps over that: 256 x $FFFD is below it, 257 x $FFFD past 2^24. The 258th step's index is past
    // the table, which is its end: one-shot halts there, free run loops (its phase kept); neither wraps to the
    // table's start unnoticed.
    for (const unsigned mode : {unsigned{Es5503::MODE_ONE_SHOT}, unsigned{Es5503::MODE_FREE}})
    {
        Es5503 d;
        for (uint32_t i = 0; i < 0x8000; ++i)
            d.ram()[i] = static_cast<uint8_t>(1 + i % 255);
        osc(d, 0, 0xFFFD, 10, 0, table(7, 7), control(mode));
        unsigned k = 0, loops = 0;
        for (; k < 600; ++k)
        {
            const uint32_t acc = d.accumulator(0);
            d.step();
            if (halted(d, 0))
                break;
            loops += d.accumulator(0) < acc;
        }
        std::printf("  %s: %s after %u steps, %u loops\n", mode == Es5503::MODE_ONE_SHOT ? "one-shot" : "free run",
                    halted(d, 0) ? "halted" : "running", k + 1, loops);
        if (mode == Es5503::MODE_ONE_SHOT)
            CHECK(halted(d, 0) && k + 1 == 258);
        else
            CHECK(!halted(d, 0) && loops >= 2);
    }
}

TEST(a_loop_never_takes_the_accumulator_below_zero)
{
    // Oscillator 0 with IE, its partner 1 in swap mode and halted, so 0 starts again at each end. Free run: at its
    // table's end it loops twice (its own and the restart), which would take the accumulator below 0; it goes to
    // 0, one end a pass: one interrupt in a pass of 256 steps. One-shot with a $00 at index 40: halted there, it
    // starts again from 0, not from below it: an interrupt every 41 steps, 6 in the pass.
    for (const unsigned mode : {unsigned{Es5503::MODE_FREE}, unsigned{Es5503::MODE_ONE_SHOT}})
    {
        Es5503 d;
        enable(d, 2);
        ramp(d, 0, 256);
        if (mode == Es5503::MODE_ONE_SHOT)
            d.ram()[40] = 0;
        osc(d, 0, 0x200, 100, 0, table(0, 0), control(mode, 0, true)); // index k at step k
        osc(d, 1, 0x200, 100, 0, table(0, 0), control(Es5503::MODE_SWAP, 0, false, true));
        unsigned irqs = 0;
        for (unsigned k = 0; k < 256; ++k)
        {
            d.step();
            if (d.irq())
            {
                ++irqs;
                d.read(R::INTERRUPT);
            }
        }
        std::printf("  %s: %u interrupts in a pass of 256 steps\n", mode == Es5503::MODE_FREE ? "free run" : "one-shot",
                    irqs);
        CHECK_EQ(irqs, mode == Es5503::MODE_FREE ? 1u : 6u);
        CHECK(!halted(d, 0));
    }
}

TEST(swap_hands_over_to_its_partner_from_zero)
{
    // Oscillator 2 in swap mode reaches its table's end: it halts and 3 starts from 0, in the same sample (3 steps
    // after 2). With IE, 2 is pending. Then 3, also swap, hands back to 2.
    Es5503 d;
    enable(d, 4);
    ramp(d, 0, 512);
    osc(d, 2, 0x800, 200, 0, table(0, 0), control(Es5503::MODE_SWAP, 0, true));
    osc(d, 3, 0x300, 200, 1, table(0, 0), control(Es5503::MODE_SWAP, 1, false, true));
    unsigned k = 0;
    while (!halted(d, 2) && k < 1'000)
    {
        d.step();
        ++k;
    }
    std::printf("  2 ended after %u steps: 3 %s, its accumulator $%X; IRQ %s\n", k, halted(d, 3) ? "halted" : "running",
                d.accumulator(3), d.irq() ? "up" : "down");
    CHECK_EQ(k, 65u); // (255 << 9) / $800 = 63.75: the 65th step (its accumulator 64 x $800) reads index 256
    CHECK(!halted(d, 3));
    CHECK_EQ(d.accumulator(3), 0x300u);
    CHECK(d.irq());
    CHECK_EQ(d.read(R::INTERRUPT), static_cast<uint8_t>(2 << 1 | 0x41));
    unsigned j = 0;
    while (!halted(d, 3) && j < 2'000)
    {
        d.step();
        ++j;
    }
    // 3 ends: 2 starts from 0, but 2 steps before 3 in a sample, so it moves from the next.
    CHECK(halted(d, 3));
    CHECK(!halted(d, 2));
    CHECK_EQ(d.accumulator(2), 0u);
    CHECK(!d.irq()); // 3 has no IE
}

TEST(an_even_one_shot_with_a_swapping_partner_starts_again)
{
    // Oscillator 4 one-shot, its partner 5 in swap mode: at its table's end 4 doesn't halt but loops, its phase
    // kept, as MAME has it from the hardware.
    Es5503 d;
    enable(d, 6);
    ramp(d, 0, 256);
    osc(d, 4, 0x1000, 100, 0, table(0, 0), control(Es5503::MODE_ONE_SHOT));
    osc(d, 5, 0x100, 100, 0, table(0, 0), control(Es5503::MODE_SWAP, 0, false, true));
    unsigned loops = 0;
    for (unsigned k = 0; k < 500; ++k)
    {
        const uint32_t acc = d.accumulator(4);
        d.step();
        loops += d.accumulator(4) < acc;
    }
    std::printf("  4 after 500 steps: %s, %u loops\n", halted(d, 4) ? "halted" : "running", loops);
    CHECK(!halted(d, 4));
    CHECK(loops > 5);
    CHECK(halted(d, 5));
}

TEST(sync_restarts_the_odd_one_below)
{
    // Oscillator 1 free run, slow; 2 in sync mode, faster: each time 2's table ends, 1 starts again from 0 (1 steps
    // first in the sample, so its accumulator reads 0 after it). 2 loops. Oscillator 0 in sync mode, with nothing
    // below, just loops: 31, all 32 enabled and running slowly, is never restarted by it.
    Es5503 d;
    enable(d, 32);
    osc(d, 31, 0x10, 50, 0, table(0, 0), control(Es5503::MODE_FREE));
    ramp(d, 0, 256);
    osc(d, 0, 0x2000, 50, 0, table(0, 0), control(Es5503::MODE_SYNC_AM));
    osc(d, 1, 0x100, 50, 0, table(0, 0), control(Es5503::MODE_FREE));
    osc(d, 2, 0x900, 50, 0, table(0, 0), control(Es5503::MODE_SYNC_AM));
    unsigned restarts = 0, ends = 0, ends0 = 0;
    bool steady31 = true;
    for (unsigned k = 0; k < 2'000; ++k)
    {
        const uint32_t acc2 = d.accumulator(2), acc0 = d.accumulator(0);
        d.step();
        ends0 += d.accumulator(0) < acc0;
        steady31 = steady31 && d.accumulator(31) == (k + 1) * 0x10u; // 255 << 9 is 8,160 steps away
        if (d.accumulator(2) < acc2) // 2's table ended this step
        {
            ++ends;
            restarts += d.accumulator(1) == 0;
        }
    }
    std::printf("  2's table ended %u times; 1 restarted %u of them; 0 and 2 %s; 0 ended %u times, 31 %s\n", ends,
                restarts, !halted(d, 0) && !halted(d, 2) ? "running" : "halted", ends0,
                steady31 ? "never restarted" : "RESTARTED");
    CHECK(ends0 > 10);
    CHECK(steady31);
    CHECK(ends > 10);
    CHECK_EQ(restarts, ends);
    CHECK(!halted(d, 0));
    CHECK(!halted(d, 1));
    CHECK(!halted(d, 2));
    // 1 never reaches its own end: always restarted first.
    CHECK(d.accumulator(1) < (255u << 9));
}

TEST(am_takes_the_odd_ones_byte_as_the_volume_above)
{
    // Oscillator 3 in sync/AM mode plays nothing; each step its byte becomes 4's volume, so 4's output is its own
    // (byte - 128) x 3's byte; and the volume register reads it. Oscillator 31 in AM mode, with nothing above, is
    // silent and harmless.
    Es5503 d;
    enable(d, 32);
    for (uint32_t i = 0; i < 256; ++i)
    {
        d.ram()[i] = static_cast<uint8_t>(1 + i);              // 3's table at page 0
        d.ram()[0x100 + i] = static_cast<uint8_t>(0xFF - i / 2); // 4's at page 1
    }
    osc(d, 3, 0x200, 77, 0, table(0, 0), control(Es5503::MODE_SYNC_AM, 0));
    osc(d, 4, 0x200, 0, 1, table(0, 0), control(Es5503::MODE_FREE, 2));
    osc(d, 31, 0x200, 99, 0, table(0, 0), control(Es5503::MODE_SYNC_AM, 1));
    osc(d, 0, 0x100, 42, 0, table(0, 0), control(Es5503::MODE_FREE, 5)); // running: 31's AM must not reach it
    bool right = true;
    for (unsigned k = 0; k < 200; ++k)
    {
        const uint32_t index = d.accumulator(3) >> 9;
        d.step();
        const uint8_t b3 = d.ram()[index & 0xFF], b4 = d.ram()[0x100 + (index & 0xFF)];
        right = right && d.out()[0] == 0 && d.out()[1] == 0 && d.out()[2] == (int32_t{b4} - 128) * b3 &&
                d.peek(static_cast<uint8_t>(R::VOLUME + 4)) == b3;
    }
    std::printf("  4's level 200 steps by 3's bytes: %s; 0's volume %u\n", right ? "right" : "WRONG",
                d.peek(R::VOLUME));
    CHECK(right);
    CHECK_EQ(d.peek(R::VOLUME), 42);
}

TEST(a_zero_byte_halts_in_every_mode)
{
    // A $00 at index 40: whatever the mode, reading it halts the oscillator, plays nothing that step, and leaves
    // the data register $00; with IE, pending.
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        Es5503 d;
        enable(d, 2);
        ramp(d, 0, 256);
        d.ram()[40] = 0;
        osc(d, 0, 0x200, 255, 0, table(0, 0), control(mode, 0, true)); // index k at step k
        osc(d, 1, 0x200, 0, 0, table(0, 0), control(Es5503::MODE_FREE, 0, false, true)); // a partner, halted
        for (unsigned k = 0; k < 40; ++k)
            d.step();
        CHECK(!halted(d, 0));
        d.step(); // index 40
        std::printf("  mode %u: %s at the $00, output %d, data $%02X, IRQ %s\n", mode,
                    halted(d, 0) ? "halted" : "running", d.out()[0], d.peek(R::DATA), d.irq() ? "up" : "down");
        CHECK(halted(d, 0));
        CHECK_EQ(d.out()[0], 0);
        CHECK_EQ(d.peek(R::DATA), 0x00);
        CHECK(d.irq());
        // Swap hands over even so; the others leave the partner halted.
        CHECK_EQ(halted(d, 1), mode != Es5503::MODE_SWAP);
    }
}

TEST(the_bank_bit_reads_the_upper_64_kb_and_survives_a_pointer_write)
{
    // The same table offset in bank 0 and bank 1, different bytes: bit 6 of the table register picks. Written
    // before the pointer, it stays: the bank is its own latch, and $C0 reads back as written.
    Es5503 d;
    for (uint32_t i = 0; i < 256; ++i)
    {
        d.ram()[0x0500 + i] = 0x90;
        d.ram()[0x10500 + i] = 0xF0;
    }
    d.write(static_cast<uint8_t>(R::TABLE + 0), table(0, 0, true));
    d.write(static_cast<uint8_t>(R::POINTER + 0), 0x05);
    d.write(static_cast<uint8_t>(R::VOLUME + 0), 1);
    d.write(static_cast<uint8_t>(R::FREQ_HI + 0), 1);
    d.write(static_cast<uint8_t>(R::CONTROL + 0), control(Es5503::MODE_FREE));
    CHECK_EQ(d.peek(static_cast<uint8_t>(R::TABLE + 0)), table(0, 0, true));
    d.step();
    std::printf("  bank 1 after a pointer write: output %d (bank 1's $F0: 112; bank 0's $90: 16)\n", d.out()[0]);
    CHECK_EQ(d.out()[0], 0xF0 - 128);
    d.write(static_cast<uint8_t>(R::TABLE + 0), table(0, 0, false));
    d.step();
    CHECK_EQ(d.out()[0], 0x90 - 128);
}

TEST(registers_read_back)
{
    // Every oscillator register as written (the table's bit 7 0, data read only and $80 at first), $E1, $E2's $80,
    // $E3-$FF 0.
    Es5503 d;
    bool all = true;
    for (unsigned n = 0; n < 32; ++n)
    {
        const uint8_t v = static_cast<uint8_t>(n * 13 + 5);
        for (const uint8_t base : {uint8_t{R::FREQ_LO}, uint8_t{R::FREQ_HI}, uint8_t{R::VOLUME}, uint8_t{R::POINTER}})
        {
            d.write(static_cast<uint8_t>(base + n), v);
            all = all && d.read(static_cast<uint8_t>(base + n)) == v;
        }
        d.write(static_cast<uint8_t>(R::CONTROL + n), static_cast<uint8_t>(v | 1)); // halted: no sound
        all = all && d.read(static_cast<uint8_t>(R::CONTROL + n)) == (v | 1);
        d.write(static_cast<uint8_t>(R::TABLE + n), 0xFF);
        all = all && d.read(static_cast<uint8_t>(R::TABLE + n)) == 0x7F;
        d.write(static_cast<uint8_t>(R::DATA + n), 0x12);
        all = all && d.read(static_cast<uint8_t>(R::DATA + n)) == 0x80;
    }
    CHECK(all);
    d.write(R::ENABLE, 0xFF);
    CHECK_EQ(d.read(R::ENABLE), 62);
    CHECK_EQ(d.read(R::ADC), 0x80);
    for (unsigned r = 0xE3; r <= 0xFF; ++r)
        CHECK_EQ(d.read(static_cast<uint8_t>(r)), 0);
}

TEST(e0_takes_the_lowest_pending_first_and_the_line_follows)
{
    // Oscillators 12, 3 and 7 pending (CPU halts in one-shot mode with IE): $E0 reads 3, 7, 12 in that order, each
    // (n << 1) | $41, the line staying up until the last is taken; then the last one's again with bit 7 set.
    Es5503 d;
    enable(d, 32);
    for (const unsigned n : {12u, 3u, 7u})
    {
        osc(d, n, 0, 0, 0, table(0, 0), control(Es5503::MODE_ONE_SHOT, 0, true));
        d.write(static_cast<uint8_t>(R::CONTROL + n), control(Es5503::MODE_ONE_SHOT, 0, true, true));
    }
    CHECK(d.irq());
    CHECK_EQ(d.peek(R::INTERRUPT), static_cast<uint8_t>(3 << 1 | 0x41)); // a peek takes nothing
    for (const unsigned n : {3u, 7u, 12u})
    {
        CHECK_EQ(d.read(R::INTERRUPT), static_cast<uint8_t>(n << 1 | 0x41));
        CHECK_EQ(d.irq(), n != 12);
    }
    CHECK_EQ(d.read(R::INTERRUPT), static_cast<uint8_t>(12 << 1 | 0xC1));
    CHECK(!d.irq());
    // One beyond the enabled count is pending but neither reported nor holding the line after a read.
    d.write(static_cast<uint8_t>(R::CONTROL + 20), control(Es5503::MODE_ONE_SHOT, 0, true));
    d.write(static_cast<uint8_t>(R::CONTROL + 20), control(Es5503::MODE_ONE_SHOT, 0, true, true));
    CHECK(d.irq());
    enable(d, 10);
    CHECK_EQ(d.read(R::INTERRUPT), static_cast<uint8_t>(12 << 1 | 0xC1));
    CHECK(!d.irq());
}

TEST(halts_written_from_the_cpu)
{
    // One-shot or swap halted by a control write does what the table's end would, with an interrupt only if both
    // the old and the new value have IE; free run halted so does nothing more; clearing the halt bit starts from 0.
    struct Case
    {
        unsigned mode;
        bool old_ie, new_ie, pending, partner;
    };
    for (const Case c : {Case{Es5503::MODE_FREE, true, true, false, false},
                         Case{Es5503::MODE_ONE_SHOT, true, true, true, false},
                         Case{Es5503::MODE_ONE_SHOT, true, false, false, false},
                         Case{Es5503::MODE_ONE_SHOT, false, true, false, false},
                         Case{Es5503::MODE_SWAP, true, true, true, true}})
    {
        Es5503 d;
        enable(d, 4);
        ramp(d, 0, 256);
        osc(d, 2, 0x100, 10, 0, table(0, 0), control(c.mode, 0, c.old_ie));
        osc(d, 3, 0x100, 10, 0, table(0, 0), control(Es5503::MODE_FREE, 0, false, true));
        for (int k = 0; k < 10; ++k)
            d.step();
        d.write(static_cast<uint8_t>(R::CONTROL + 2), control(c.mode, 0, c.new_ie, true));
        CHECK(halted(d, 2));
        CHECK_EQ(d.irq(), c.pending);
        CHECK_EQ(!halted(d, 3), c.partner);
    }
    Es5503 d;
    ramp(d, 0, 256);
    osc(d, 0, 0x100, 10, 0, table(0, 0), control(Es5503::MODE_FREE));
    for (int k = 0; k < 10; ++k)
        d.step();
    d.write(static_cast<uint8_t>(R::CONTROL + 0), control(Es5503::MODE_FREE, 0, false, true));
    CHECK_EQ(d.accumulator(0), 0xA00u); // halted: kept
    d.write(static_cast<uint8_t>(R::CONTROL + 0), control(Es5503::MODE_FREE));
    CHECK_EQ(d.accumulator(0), 0u); // started: from 0
}

TEST(reset_halts_clears_and_keeps)
{
    Es5503 d;
    enable(d, 8);
    ramp(d, 0, 256);
    osc(d, 1, 0x1234, 0x56, 0x78, table(3, 2, true), control(Es5503::MODE_ONE_SHOT, 5, true));
    for (int k = 0; k < 10; ++k)
        d.step();
    d.write(static_cast<uint8_t>(R::CONTROL + 1), control(Es5503::MODE_ONE_SHOT, 5, true, true));
    CHECK(d.irq());
    d.reset();
    CHECK(!d.irq());
    CHECK(halted(d, 1));
    CHECK_EQ(d.accumulator(1), 0u);
    CHECK_EQ(d.peek(R::ENABLE), 0);
    CHECK_EQ(d.peek(R::INTERRUPT), 0xFF);
    CHECK_EQ(d.peek(static_cast<uint8_t>(R::FREQ_HI + 1)), 0x12);
    CHECK_EQ(d.peek(static_cast<uint8_t>(R::TABLE + 1)), table(3, 2, true));
    CHECK_EQ(d.peek(static_cast<uint8_t>(R::DATA + 1)), 0x80);
    CHECK_EQ(d.ram()[5], 6);
}

TEST(the_top_oscillator_is_mixed_once)
{
    // MAME mixes the highest enabled oscillator three times (a quirk of the real chip, it says); the spec leaves
    // that out. Two enabled, oscillator 1 a constant $C0 at volume 255: 64 x 255 on its channel, once.
    Es5503 d;
    enable(d, 2);
    for (uint32_t i = 0; i < 256; ++i)
        d.ram()[i] = 0xC0;
    osc(d, 1, 0x100, 255, 0, table(0, 0), control(Es5503::MODE_FREE, 3));
    d.step();
    CHECK_EQ(d.out()[3], 64 * 255);
    CHECK_EQ(d.stereo()[1], 64 * 255);
    CHECK_EQ(d.stereo()[0], 0);
}

TEST(a_busy_run_to_a_golden_hash)
{
    // All 32 enabled, every mode, sizes and resolutions mixed, wave RAM noise with $00s here and there, both banks,
    // control writes and $E0 reads along the way: the channels' sums, the data registers and the $E0 reads, hashed.
    // Integers only: this build's and WebAssembly's alike.
    Es5503 d;
    uint32_t seed = 12'345;
    const auto rnd = [&] {
        seed = seed * 1'103'515'245u + 12'345u;
        return seed >> 16;
    };
    for (uint32_t i = 0; i < Es5503::RAM_BYTES; ++i)
        d.ram()[i] = static_cast<uint8_t>(rnd() % 400 < 2 ? 0 : 1 + rnd() % 255);
    enable(d, 32);
    for (unsigned n = 0; n < 32; ++n)
        osc(d, n, static_cast<uint16_t>(rnd()), static_cast<uint8_t>(rnd()), static_cast<uint8_t>(rnd()),
            static_cast<uint8_t>(rnd() & 0x7F), static_cast<uint8_t>(rnd() & 0xFE));
    uint64_t h = 0xCBF29CE484222325ull;
    const auto mix = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b)
            h = (h ^ ((v >> (8 * b)) & 0xFF)) * 0x100000001B3ull;
    };
    for (unsigned k = 0; k < 200'000; ++k)
    {
        d.step();
        for (const int32_t s : d.out())
            mix(static_cast<uint32_t>(s));
        if (k % 997 == 0)
        {
            const unsigned n = rnd() % 32;
            d.write(static_cast<uint8_t>(R::CONTROL + n), static_cast<uint8_t>(rnd()));
            mix(d.read(R::INTERRUPT));
            mix(d.irq());
            for (unsigned m = 0; m < 32; ++m)
                mix(d.peek(static_cast<uint8_t>(R::DATA + m)));
        }
    }
    std::printf("  golden: %016llx\n", static_cast<unsigned long long>(h));
    CHECK_EQ(h, 0x94B024837C13A6FDull);
}

int main()
{
    return run_all();
}
