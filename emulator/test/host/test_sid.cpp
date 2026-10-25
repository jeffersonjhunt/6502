/**
 * @file test_sid.cpp
 * @brief The SID's oscillators and envelopes (sid.h), and OSC3/ENV3 on the sound card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The chip alone, its waveforms and envelopes against the numbers the
 * datasheet and the chip's measured behaviour give: a sawtooth's slope, the
 * triangle's fold, the pulse width, noise from its 23-bit register, TEST,
 * sync and ring, attack in 255 x 9 clocks at A=0, decay's exponential
 * stretch, sustain, release, and the ADSR delay. Then on the card: the
 * chips keep the SID clock's time with the 6510 stopped, and the 6510 reads
 * OSC3 on the very cycle of its LDA.
 */

#include "harness.h"

#include "sid.h"
#include "sound_card.h"

#include <initializer_list>
#include <set>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    // Voice 3's registers, the ones OSC3 and ENV3 show.
    constexpr unsigned FREQ3 = 0x0E, PW3 = 0x10, CTRL3 = 0x12, AD3 = 0x13, SR3 = 0x14;

    void freq(Sid &s, unsigned base, unsigned f)
    {
        s.write(base, static_cast<uint8_t>(f));
        s.write(base + 1, static_cast<uint8_t>(f >> 8));
    }
    unsigned osc3(const Sid &s) { return s.read(Sid::OSC3); }
    unsigned env3(const Sid &s) { return s.read(Sid::ENV3); }
} // namespace

TEST(reads_pots_and_the_written_registers)
{
    Sid s;
    CHECK_EQ(s.read(Sid::POTX), 0xFF);
    CHECK_EQ(s.read(Sid::POTY), 0xFF);
    s.write(0x18, 0x1F);
    CHECK_EQ(s.read(0x18), 0x1F);
    s.write(Sid::OSC3, 0x55); // read-only
    CHECK_EQ(osc3(s), 0x00);
    CHECK_EQ(env3(s), 0x00);
}

TEST(sawtooth_is_the_accumulators_top_bits)
{
    Sid s;
    freq(s, FREQ3, 0x1000);
    s.write(CTRL3, Sid::SAW);
    s.clock(256); // 256 x $1000 = $100000
    CHECK_EQ(s.voice(2).accumulator, 0x100000u);
    CHECK_EQ(osc3(s), 0x10);
    s.clock(0xE0 * 16); // OSC3 is the top 8 bits: one more every 16 clocks
    CHECK_EQ(osc3(s), 0xF0);
    s.clock(256); // 4,096 clocks in all: round to 0
    CHECK_EQ(s.voice(2).accumulator, 0u);
}

TEST(triangle_folds_at_the_top_bit)
{
    Sid s;
    freq(s, FREQ3, 0x4000);
    s.write(CTRL3, Sid::TRIANGLE);
    s.clock(256); // $400000
    CHECK_EQ(s.waveform(2), 0x800u);
    CHECK_EQ(osc3(s), 0x80);
    s.clock(512); // $C00000: on the way down
    CHECK_EQ(s.waveform(2), 0x7FFu);
    CHECK_EQ(osc3(s), 0x7F);
}

TEST(pulse_is_high_from_the_width_up_and_test_forces_it)
{
    Sid s;
    freq(s, FREQ3, 0x1000);
    s.write(PW3, 0x00);
    s.write(PW3 + 1, 0x08); // width $800: a square wave
    s.write(CTRL3, Sid::PULSE);
    s.clock(2047); // $7FF000: just below the width
    CHECK_EQ(osc3(s), 0x00);
    s.clock(1);
    CHECK_EQ(osc3(s), 0xFF); // $800000 >> 12 = $800, at the width
    s.write(CTRL3, Sid::PULSE | Sid::TEST);
    CHECK_EQ(s.voice(2).accumulator, 0u);
    CHECK_EQ(osc3(s), 0xFF);
    s.write(PW3 + 1, 0x0F);
    s.write(CTRL3, Sid::PULSE); // test off: at 0, below any width
    CHECK_EQ(osc3(s), 0x00);
}

TEST(noise_comes_from_the_shift_register_clocked_by_bit_19)
{
    Sid s;
    s.write(CTRL3, Sid::NOISE);
    // The seed's bits 20, 18, 14, 11, 9, 5, 2, 0.
    CHECK_EQ(osc3(s), 0xFC);
    freq(s, FREQ3, 0x0100);
    s.clock(0x800); // accumulator $80000: bit 19 rises once
    CHECK_EQ(s.voice(2).noise, ((Sid::NOISE_SEED << 1) | 0u) & 0x7FFFFFu);
    std::set<unsigned> seen;
    freq(s, FREQ3, 0xFFFF);
    for (int i = 0; i < 20000; ++i)
    {
        s.clock(16); // one shift each
        seen.insert(osc3(s));
    }
    CHECK(seen.size() > 250); // random enough to hit nearly every value
    s.write(CTRL3, Sid::NOISE | Sid::TEST);
    CHECK_EQ(s.voice(2).noise, Sid::NOISE_SEED);
}

TEST(bulk_and_clock_by_clock_agree)
{
    // Sync on voice 1 forces the clock-by-clock path; with it off, bulk.
    Sid a, b;
    for (Sid *s : {&a, &b})
    {
        freq(*s, FREQ3, 0x3456);
        s->write(CTRL3, Sid::NOISE);
    }
    a.write(0x04, Sid::SYNC); // voice 1 synced by voice 3: its own freq is 0, so nothing changes
    freq(a, 0x00, 0);
    a.clock(100'000);
    b.clock(100'000);
    CHECK_EQ(a.voice(2).accumulator, b.voice(2).accumulator);
    CHECK_EQ(a.voice(2).noise, b.voice(2).noise);
}

TEST(sync_resets_a_voice_when_its_source_wraps)
{
    Sid s;
    freq(s, 0x00, 0x0100);  // voice 1: slow
    freq(s, FREQ3, 0x8000); // voice 3: top bit rises every 512 clocks
    s.write(0x04, Sid::SAW | Sid::SYNC);
    s.clock(255); // voice 3 at $7F8000
    CHECK_EQ(s.voice(0).accumulator, 255u * 0x100);
    s.clock(1); // $800000: voice 1 starts again
    CHECK_EQ(s.voice(0).accumulator, 0u);
    s.clock(10);
    CHECK_EQ(s.voice(0).accumulator, 10u * 0x100);
}

TEST(ring_flips_the_triangle_with_its_sources_top_bit)
{
    Sid s;
    freq(s, FREQ3, 0x1000);
    freq(s, 0x07, 0x8000); // voice 2, voice 3's source
    s.write(CTRL3, Sid::TRIANGLE | Sid::RING);
    s.clock(256); // voice 3 at $100000, voice 2 at $800000: top bit set
    CHECK_EQ(s.waveform(2), (~0x100000u >> 11) & 0xFFF);
    s.write(CTRL3, Sid::TRIANGLE);
    CHECK_EQ(s.waveform(2), (0x100000u >> 11) & 0xFFF);
}

TEST(attack_at_a0_is_255_steps_of_9_clocks)
{
    Sid s;
    s.write(AD3, 0x00);
    s.write(SR3, 0xF0); // sustain at the top
    s.write(CTRL3, Sid::GATE);
    s.clock(9);
    CHECK_EQ(env3(s), 1);
    s.clock(2295 - 9 - 1);
    CHECK_EQ(env3(s), 0xFE);
    s.clock(1);
    CHECK_EQ(env3(s), 0xFF);
    CHECK(s.voice(2).phase == Sid::Phase::DecaySustain);
    s.clock(100'000);
    CHECK_EQ(env3(s), 0xFF); // sustained
}

TEST(slow_attack_follows_the_rate_table)
{
    Sid s;
    s.write(AD3, 0x80); // A=8: 392 clocks a step, 100 ms at 1 MHz
    s.write(SR3, 0xF0);
    s.write(CTRL3, Sid::GATE);
    s.clock(392 * 128 - 1);
    CHECK_EQ(env3(s), 127); // a clock short of the 128th step
    s.clock(1);
    CHECK_EQ(env3(s), 128);
    s.clock(392 * 127);
    CHECK_EQ(env3(s), 0xFF);
}

TEST(decay_stretches_exponentially_to_the_sustain_level)
{
    Sid s;
    s.write(AD3, 0x00); // A=0, D=0
    s.write(SR3, 0x00); // sustain 0
    s.write(CTRL3, Sid::GATE);
    s.clock(2295); // the top
    // $FF to $00: 162 steps of 1 period, then 39 of 2, 28 of 4, 12 of 8,
    // 8 of 16 and 6 of 30: 756 periods of 9 clocks.
    s.clock(162 * 9);
    CHECK_EQ(env3(s), 0x5D);
    s.clock(39 * 2 * 9);
    CHECK_EQ(env3(s), 0x36);
    s.clock((28 * 4 + 12 * 8 + 8 * 16) * 9);
    CHECK_EQ(env3(s), 0x06);
    s.clock(6 * 30 * 9 - 1);
    CHECK_EQ(env3(s), 0x01);
    s.clock(1);
    CHECK_EQ(env3(s), 0x00);
    CHECK(s.voice(2).frozen);
    s.clock(1'000'000);
    CHECK_EQ(env3(s), 0x00); // held at 0, gate still on

    Sid t;
    t.write(AD3, 0x00);
    t.write(SR3, 0x80); // sustain $88
    t.write(CTRL3, Sid::GATE);
    t.clock(2295 + (0xFF - 0x88) * 9 + 100'000);
    CHECK_EQ(env3(t), 0x88);
}

TEST(release_from_sustain_and_attack_again_from_where_it_is)
{
    Sid s;
    s.write(AD3, 0x00);
    s.write(SR3, 0xA0); // sustain $AA, R=0
    s.write(CTRL3, Sid::GATE);
    s.clock(2295 + 0x55 * 9 + 10);
    CHECK_EQ(env3(s), 0xAA);
    s.write(CTRL3, 0); // gate off
    CHECK(s.voice(2).phase == Sid::Phase::Release);
    s.clock(0x10 * 9 + 9); // a step or so of slack for where the rate counter was
    const unsigned mid = env3(s);
    CHECK(mid < 0xAA && mid >= 0x98);
    s.write(CTRL3, Sid::GATE); // attack, not from 0
    s.clock(9 * 3 + 9);
    CHECK(env3(s) > mid);
}

TEST(a_lower_period_waits_for_the_rate_counter_to_wrap)
{
    // The ADSR delay: the rate counter only resets on a match, so after
    // counting past a short period it runs on to $7FFF and round.
    Sid s;
    s.write(SR3, 0x0F); // release R=F: 31,251 clocks a step
    s.clock(20'000);    // the counter at 20,000, frozen at 0
    CHECK_EQ(s.voice(2).rate_count, 20'000u);
    s.write(AD3, 0x00); // attack A=0: 9
    s.write(CTRL3, Sid::GATE);
    s.clock(0x8000 - 20'000 + 9 - 1);
    CHECK_EQ(env3(s), 0x00); // still waiting
    s.clock(1);
    CHECK_EQ(env3(s), 0x01);
    s.clock(9);
    CHECK_EQ(env3(s), 0x02); // and from then on, every 9
}

TEST(a_period_equal_to_the_count_waits_a_whole_wrap)
{
    Sid s;
    s.write(SR3, 0x0F);
    s.clock(9); // the counter at 9
    s.write(AD3, 0x00); // a period of 9: it has just gone past, not arrived
    s.write(CTRL3, Sid::GATE);
    s.clock(1);
    CHECK_EQ(env3(s), 0x00);
    s.clock(0x8000 - 1);
    CHECK_EQ(env3(s), 0x01);
}

TEST(noise_shifts_in_bit_22_xor_17_and_shows_bits_20_18_14_11_9_5_2_0)
{
    Sid s;
    s.write(CTRL3, Sid::NOISE);
    freq(s, FREQ3, 0x0100); // bit 19 rises every 4,096 clocks
    s.clock(0x800); // the first rise of bit 19, at $80000
    uint32_t prev = s.voice(2).noise;
    for (int i = 0; i < 200; ++i)
    {
        s.clock(4096);
        const uint32_t r = s.voice(2).noise;
        CHECK_EQ(r, ((prev << 1) | (((prev >> 22) ^ (prev >> 17)) & 1u)) & 0x7FFFFFu);
        unsigned bits = 0;
        for (const unsigned b : {20u, 18u, 14u, 11u, 9u, 5u, 2u, 0u})
            bits = bits << 1 | (r >> b & 1u);
        CHECK_EQ(osc3(s), bits);
        prev = r;
    }
}

TEST(reset_clears_registers_oscillators_and_envelopes)
{
    Sid s;
    freq(s, FREQ3, 0x1234);
    s.write(CTRL3, Sid::SAW | Sid::GATE);
    s.clock(10'000);
    s.reset();
    CHECK_EQ(s.regs()[CTRL3], 0);
    CHECK_EQ(s.voice(2).accumulator, 0u);
    CHECK_EQ(env3(s), 0);
    CHECK_EQ(s.voice(2).noise, Sid::NOISE_SEED);
}

TEST(on_the_card_the_sids_keep_time_with_the_6510_stopped)
{
    SoundCard c;
    c.write(SoundCard::SID_A + FREQ3, 0x00);
    c.write(SoundCard::SID_A + FREQ3 + 1, 0x01); // $0100
    c.write(SoundCard::SID_A + CTRL3, Sid::SAW);
    c.tick(SoundCard::NATIVE_HZ); // one host second: 985,248 SID clocks
    CHECK_EQ(c.sid_clocks(), SoundCard::PAL_HZ);
    CHECK_EQ(c.chip(0).voice(2).accumulator, (SoundCard::PAL_HZ * 0x100u) & 0xFFFFFFu);
    CHECK_EQ(c.read(SoundCard::SID_A + Sid::OSC3), ((SoundCard::PAL_HZ * 0x100u) & 0xFFFFFFu) >> 16);
    CHECK_EQ(c.peek(SoundCard::SID_A + Sid::OSC3), c.read(SoundCard::SID_A + Sid::OSC3)); // the debugger's view
    c.bus_reset();
    CHECK_EQ(c.chip(0).voice(2).accumulator, 0u);
}

TEST(the_6510_reads_osc3_on_the_cycle_of_its_lda)
{
    SoundCard c;
    const std::initializer_list<uint8_t> code = {
        0xA9, 0xFF,       // LDA #$FF
        0x8D, 0x0E, 0xD4, // STA $D40E
        0x8D, 0x0F, 0xD4, // STA $D40F     voice 3 at $FFFF: OSC3 up one a clock, near enough
        0xA9, 0x20,       // LDA #$20
        0x8D, 0x12, 0xD4, // STA $D412     sawtooth
        0xAD, 0x1B, 0xD4, // LDA $D41B
        0x85, 0x10,       // STA $10
        0xAD, 0x1B, 0xD4, // LDA $D41B     7 clocks after the first read
        0x85, 0x11,       // STA $11
        0xA0, 0x00,       // LDY #0
        0xB1, 0x22,       // LDA ($22),Y   reads on its 5th cycle, not its 4th
        0x85, 0x12,       // STA $12
        0x02,             // JAM
    };
    uint16_t a = 0x1000;
    for (const uint8_t b : code)
        c.ram()[a++] = b;
    c.ram()[0x22] = 0x1B; // ($22) = $D41B
    c.ram()[0x23] = 0xD4;
    c.ram()[0xFFFC] = 0x00;
    c.ram()[0xFFFD] = 0x10;
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN | SoundCard::CTRL_RESET);
    for (int i = 0; i < 1000; ++i)
        c.tick(7);
    // From the first instruction's start: the frequency's low byte lands on
    // clock 6 and its high byte on 10, and the LDAs read on clocks 20 and
    // 27 (their 4th). So OSC3 is 255 x 4 + 65,535 x 10 = 656,370 >> 16 = 10,
    // then 7 x 65,535 more: 17.
    CHECK_EQ(c.ram()[0x10], 10);
    CHECK_EQ(c.ram()[0x11], 17);
    // LDY (2 clocks) and LDA ($22),Y from clock 32, reading on 37: 27. Had
    // the access been taken at the instruction's start, every read would be
    // a little early, and this one more than the others: 26.
    CHECK_EQ(c.ram()[0x12], 27);
}

int main()
{
    return run_all();
}
