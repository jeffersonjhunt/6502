/**
 * @file test_sound.cpp
 * @brief The sound card (hw/design.md, "Sound"): the host's side of it.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card alone: the SIDs' registers as the host sees them, the card's RAM
 * through RAMADDR and RAMDATA, the mailbox from both sides, and /RESET. Then
 * its own 6510, with hand-assembled programs: its clock against the host's,
 * its map, and CIA 1's timer and interrupt. Then on a backplane machine: its
 * block at $C500, its interrupt in IRQH, a tune's bytes loaded by DMA, and
 * /RESET from the north bridge. test_sound_driver runs a real driver.
 * Then revision 2's registers (hw/sound-rev2.md, step 1): each one's reset
 * value, what it keeps and reads back, the latches and the moving indexes,
 * the DOC's port into its registers and wave RAM, DRQSEL's routes, and
 * /RESET against CTRL RESET.
 */

#include "harness.h"

#include "dma_controller.h"
#include "machine.h"
#include "sound_card.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint8_t VOICE1_FREQ_LO = 0x00, VOICE1_CTRL = 0x04, MODE_VOL = 0x18;
    constexpr uint64_t HOST_SECOND = SoundCard::NATIVE_HZ;

    /// `code` at `addr` in the card's RAM, the reset vector at it, and the
    /// 6510 started: CTRL = RUN | RESET, plus `ctrl`.
    void start(SoundCard &c, uint16_t addr, std::initializer_list<uint8_t> code, uint8_t ctrl = 0)
    {
        uint16_t a = addr;
        for (const uint8_t b : code)
            c.ram()[a++] = b;
        c.ram()[0xFFFC] = static_cast<uint8_t>(addr);
        c.ram()[0xFFFD] = static_cast<uint8_t>(addr >> 8);
        c.write(SoundCard::CTRL, static_cast<uint8_t>(SoundCard::CTRL_RUN | SoundCard::CTRL_RESET | ctrl));
    }

    /// `cycles` host cycles, in the small steps the decoder ticks a card in.
    void host(SoundCard &c, uint64_t cycles)
    {
        for (; cycles >= 7; cycles -= 7)
            c.tick(7);
        c.tick(cycles);
    }

    constexpr std::initializer_list<uint8_t> SPIN = {0xE6, 0x10, 0x4C, 0x00, 0x10}; // INC $10; JMP $1000
} // namespace

TEST(id_and_unused_offsets)
{
    SoundCard c;
    CHECK_EQ(c.peek(SoundCard::ID_REG), 0xA2);
    CHECK_EQ(c.read(0x1D), 0xFF); // past SID A's 29 registers
    CHECK_EQ(c.read(0x3F), 0xFF);
    CHECK_EQ(c.read(0x64), 0xFF); // past revision 2's (hw/sound-rev2.md), $48-$63
    CHECK_EQ(c.read(0x7E), 0xFF);
    CHECK_EQ(c.read(0xFF), 0xA2); // the block repeats every $80
}

TEST(sid_registers_read_back_as_written_and_the_chip_answers_its_four)
{
    SoundCard c;
    c.write(SoundCard::SID_A + VOICE1_FREQ_LO, 0x25);
    c.write(SoundCard::SID_A + MODE_VOL, 0x0F);
    c.write(SoundCard::SID_B + VOICE1_CTRL, 0x41);
    CHECK_EQ(c.read(SoundCard::SID_A + VOICE1_FREQ_LO), 0x25);
    CHECK_EQ(c.read(SoundCard::SID_A + MODE_VOL), 0x0F);
    CHECK_EQ(c.read(SoundCard::SID_B + VOICE1_CTRL), 0x41);
    CHECK_EQ(c.read(SoundCard::SID_A + VOICE1_CTRL), 0x00); // not SID B's
    CHECK_EQ(c.sid(1)[VOICE1_CTRL], 0x41);
    // POTX and POTY: no paddles. Writes to the read-only four go nowhere.
    c.write(SoundCard::SID_A + SoundCard::POTX, 0x12);
    CHECK_EQ(c.read(SoundCard::SID_A + SoundCard::POTX), 0xFF);
    CHECK_EQ(c.sid(0)[SoundCard::POTX], 0x00);
    CHECK_EQ(c.read(SoundCard::SID_B + SoundCard::POTY), 0xFF);
    CHECK_EQ(c.read(SoundCard::SID_A + SoundCard::OSC3), 0x00);
}

TEST(while_the_card_runs_its_6510_owns_the_sids)
{
    SoundCard c;
    c.write(SoundCard::SID_A + MODE_VOL, 0x0F);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN);
    c.write(SoundCard::SID_A + MODE_VOL, 0x03); // dropped
    c.write(SoundCard::SID_B + MODE_VOL, 0x03);
    CHECK_EQ(c.read(SoundCard::SID_A + MODE_VOL), 0x0F);
    CHECK_EQ(c.read(SoundCard::SID_B + MODE_VOL), 0x00);
    c.sid_write(0, MODE_VOL, 0x07); // the card's own write
    CHECK_EQ(c.read(SoundCard::SID_A + MODE_VOL), 0x07); // and reads still work
    c.write(SoundCard::CTRL, 0);
    c.write(SoundCard::SID_A + MODE_VOL, 0x03);
    CHECK_EQ(c.read(SoundCard::SID_A + MODE_VOL), 0x03);
}

TEST(both_sends_sid_a_writes_to_sid_b_too)
{
    SoundCard c;
    c.write(SoundCard::CTRL, SoundCard::CTRL_BOTH);
    c.write(SoundCard::SID_A + VOICE1_FREQ_LO, 0x99);
    CHECK_EQ(c.sid(1)[VOICE1_FREQ_LO], 0x99);
    c.write(SoundCard::SID_B + VOICE1_FREQ_LO, 0x11); // B alone stays B's
    CHECK_EQ(c.sid(0)[VOICE1_FREQ_LO], 0x99);
    CHECK_EQ(c.sid(1)[VOICE1_FREQ_LO], 0x11);
    c.write(SoundCard::CTRL, SoundCard::CTRL_BOTH | SoundCard::CTRL_RUN);
    c.sid_write(0, VOICE1_CTRL, 0x21); // the card's side too
    CHECK_EQ(c.sid(1)[VOICE1_CTRL], 0x21);
}

TEST(ctrl_keeps_its_bits_and_reset_is_a_strobe)
{
    SoundCard c;
    c.write(SoundCard::CTRL, 0x1F);
    CHECK_EQ(c.read(SoundCard::CTRL), 0x1B); // RESET reads 0
    CHECK_EQ(c.cpu_resets(), 1u);
    c.write(SoundCard::CTRL, SoundCard::CTRL_NTSC);
    CHECK_EQ(c.read(SoundCard::CTRL), SoundCard::CTRL_NTSC);
    CHECK_EQ(c.cpu_resets(), 1u);
}

TEST(ramdata_walks_the_cards_64k_from_ramaddr)
{
    SoundCard c;
    c.write(SoundCard::RAMADDR, 0xFE);
    c.write(SoundCard::RAMADDR + 1, 0x10);
    CHECK_EQ(c.read(SoundCard::RAMADDR), 0xFE);
    CHECK_EQ(c.read(SoundCard::RAMADDR + 1), 0x10);
    for (const unsigned b : {0xA9u, 0x00u, 0x60u})
        c.write(SoundCard::RAMDATA, static_cast<uint8_t>(b));
    CHECK_EQ(c.ram()[0x10FE], 0xA9);
    CHECK_EQ(c.ram()[0x10FF], 0x00);
    CHECK_EQ(c.ram()[0x1100], 0x60);
    CHECK_EQ(c.read(SoundCard::RAMADDR), 0x01);
    CHECK_EQ(c.read(SoundCard::RAMADDR + 1), 0x11);
    c.write(SoundCard::RAMADDR + 1, 0x10); // the low byte stays: $1001
    c.write(SoundCard::RAMADDR, 0xFF);
    CHECK_EQ(c.peek(SoundCard::RAMDATA), 0x00); // peek does not move on
    CHECK_EQ(c.read(SoundCard::RAMDATA), 0x00);
    CHECK_EQ(c.read(SoundCard::RAMDATA), 0x60);
    c.write(SoundCard::RAMADDR, 0xFF);
    c.write(SoundCard::RAMADDR + 1, 0xFF);
    c.write(SoundCard::RAMDATA, 0x42);
    CHECK_EQ(c.ram()[0xFFFF], 0x42);
    CHECK_EQ(c.read(SoundCard::RAMADDR + 1), 0x00); // wrapped
    CHECK(c.drq());                                 // always ready
}

TEST(mailbox_both_ways)
{
    SoundCard c;
    CHECK_EQ(c.read(SoundCard::STATUS), 0x00);
    CHECK_EQ(c.card_receive(), -1);
    c.write(SoundCard::MBOUT, 3); // "play song 3"
    CHECK_EQ(c.read(SoundCard::STATUS), SoundCard::STATUS_TO_CARD);
    CHECK_EQ(c.card_status(), SoundCard::CARD_FROM_HOST);
    c.write(SoundCard::MBOUT, 4); // a second byte replaces the first
    CHECK_EQ(c.card_receive(), 4);
    CHECK_EQ(c.card_receive(), -1);
    CHECK_EQ(c.read(SoundCard::STATUS), 0x00);

    c.card_send(0x80); // "tune finished"
    CHECK_EQ(c.read(SoundCard::STATUS), SoundCard::STATUS_TO_HOST);
    CHECK_EQ(c.card_status(), SoundCard::CARD_UNTAKEN);
    CHECK_EQ(c.peek(SoundCard::MBIN), 0x80);
    CHECK_EQ(c.read(SoundCard::STATUS), SoundCard::STATUS_TO_HOST); // a peek takes nothing
    CHECK_EQ(c.read(SoundCard::MBIN), 0x80);
    CHECK_EQ(c.read(SoundCard::STATUS), 0x00);
    CHECK_EQ(c.card_status(), 0x00);
    c.write(SoundCard::MBIN, 0x55); // MBIN is read-only, MBOUT write-only
    CHECK_EQ(c.read(SoundCard::STATUS), 0x00);
}

TEST(a_byte_for_the_host_interrupts_when_ien_says_so)
{
    SoundCard c;
    c.card_send(1);
    CHECK(!c.irq());
    c.write(SoundCard::IEN, 0xFF);
    CHECK_EQ(c.read(SoundCard::IEN), SoundCard::IEN_MAILBOX);
    CHECK(c.irq());
    CHECK_EQ(c.read(SoundCard::STATUS), SoundCard::STATUS_TO_HOST | SoundCard::STATUS_IRQ);
    c.read(SoundCard::MBIN);
    CHECK(!c.irq());
    c.write(SoundCard::MBOUT, 9); // the host's own byte never interrupts the host
    CHECK(!c.irq());
}

TEST(reset_stops_and_silences_and_keeps_the_ram)
{
    SoundCard c;
    c.write(SoundCard::SID_A + MODE_VOL, 0x0F);
    c.write(SoundCard::RAMADDR, 0x34);
    c.write(SoundCard::RAMDATA, 0xEA);
    c.write(SoundCard::IEN, SoundCard::IEN_MAILBOX);
    c.write(SoundCard::MBOUT, 1);
    c.card_send(2);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC);
    c.bus_reset();
    CHECK_EQ(c.read(SoundCard::CTRL), 0x00);
    CHECK_EQ(c.read(SoundCard::SID_A + MODE_VOL), 0x00);
    CHECK_EQ(c.read(SoundCard::STATUS), 0x00);
    CHECK_EQ(c.read(SoundCard::IEN), 0x00);
    CHECK_EQ(c.read(SoundCard::RAMADDR), 0x00);
    CHECK_EQ(c.ram()[0x34], 0xEA);
}

TEST(the_6510_runs_at_the_sid_clock_against_the_hosts)
{
    SoundCard pal, ntsc, slow;
    start(pal, 0x1000, SPIN);
    start(ntsc, 0x1000, SPIN, SoundCard::CTRL_NTSC);
    const uint64_t pal0 = pal.cpu().cycles(), ntsc0 = ntsc.cpu().cycles(); // the reset sequence
    host(pal, HOST_SECOND);
    host(ntsc, HOST_SECOND);
    // one host second is one SID second, to within the instruction it ends in
    CHECK(pal.cpu().cycles() - pal0 >= SoundCard::PAL_HZ && pal.cpu().cycles() - pal0 < SoundCard::PAL_HZ + 5);
    CHECK(ntsc.cpu().cycles() - ntsc0 >= SoundCard::NTSC_HZ && ntsc.cpu().cycles() - ntsc0 < SoundCard::NTSC_HZ + 5);
    CHECK(pal.ram()[0x10] != 0); // and it was the program running

    // An Apple-speed host at the NTSC clock: cycle for cycle.
    slow.set_cpu_hz(SoundCard::NTSC_HZ);
    start(slow, 0x1000, SPIN, SoundCard::CTRL_NTSC);
    const uint64_t slow0 = slow.cpu().cycles();
    host(slow, 100'000);
    CHECK(slow.cpu().cycles() - slow0 >= 100'000 && slow.cpu().cycles() - slow0 < 100'005);
}

TEST(run_off_holds_the_6510_where_it_is)
{
    SoundCard c;
    start(c, 0x1000, SPIN);
    host(c, 10'000);
    c.write(SoundCard::CTRL, 0);
    const uint64_t at = c.cpu().cycles();
    host(c, HOST_SECOND);
    CHECK_EQ(c.cpu().cycles(), at);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN); // on again, without a reset: it carries on
    host(c, 1000);
    CHECK(c.cpu().cycles() > at);
    CHECK(c.cpu().regs().pc >= 0x1000 && c.cpu().regs().pc < 0x1005);
    CHECK_EQ(c.cpu_resets(), 1u);
}

TEST(the_6510s_map_port_sids_mailbox_and_the_rest_of_io)
{
    SoundCard c;
    c.write(SoundCard::MBOUT, 0x42);
    start(c, 0x1000,
          {
              0xA9, 0x2F,       // LDA #$2F
              0x85, 0x00,       // STA $00       DDR
              0xA9, 0x34,       // LDA #$34
              0x85, 0x01,       // STA $01       I/O out
              0xA9, 0x5A,       // LDA #$5A
              0x8D, 0x00, 0xD4, // STA $D400     the RAM under SID A
              0xA9, 0x37,       // LDA #$37
              0x85, 0x01,       // STA $01       I/O in
              0xA9, 0x0F,       // LDA #$0F
              0x8D, 0x18, 0xD4, // STA $D418     SID A's volume
              0x8D, 0x38, 0xD4, // STA $D438     SID B's, at $D420
              0xA9, 0x07,       // LDA #$07
              0x8D, 0x58, 0xD7, // STA $D758     SID A again: it repeats to $D7FF
              0xAD, 0x01, 0xDE, // LDA $DE01
              0x85, 0x31,       // STA $31       the mailbox's status: a byte waiting
              0xAD, 0x00, 0xDE, // LDA $DE00     the host's byte
              0x8D, 0x00, 0xDE, // STA $DE00     back to the host
              0xAD, 0x00, 0xD0, // LDA $D000     no VIC-II
              0x85, 0x30,       // STA $30
              0xA5, 0x01,       // LDA $01
              0x85, 0x32,       // STA $32
              0x02,             // JAM
          });
    host(c, HOST_SECOND / 100);
    CHECK(c.cpu().state() == Cpu6502::State::Stopped);
    CHECK_EQ(c.ram()[0xD400], 0x5A);
    CHECK_EQ(c.sid(0)[0x00], 0x00); // not the SID
    CHECK_EQ(c.sid(0)[MODE_VOL], 0x07);
    CHECK_EQ(c.sid(1)[MODE_VOL], 0x0F);
    CHECK_EQ(c.ram()[0xD418], 0x00); // the I/O writes went to I/O only
    CHECK_EQ(c.ram()[0x31], SoundCard::CARD_FROM_HOST);
    CHECK_EQ(c.read(SoundCard::MBIN), 0x42);
    CHECK_EQ(c.ram()[0x30], 0xFF);
    CHECK_EQ(c.ram()[0x32], 0x37); // the port reads back
    CHECK_EQ(c.ram()[0x00], 0x2F); // and $00/$01 reach the RAM under them
    CHECK_EQ(c.ram()[0x01], 0x37);
    CHECK_EQ(c.port_ddr(), 0x2F);

    // CTRL bit 3: SID B at $D500, and $D420 is SID A's again.
    SoundCard d;
    start(d, 0x1000,
          {
              0xA9, 0x0C,       // LDA #$0C
              0x8D, 0x18, 0xD5, // STA $D518
              0xA9, 0x03,       // LDA #$03
              0x8D, 0x38, 0xD4, // STA $D438
              0x02,             // JAM
          },
          SoundCard::CTRL_SID_B_D500);
    host(d, HOST_SECOND / 100);
    CHECK_EQ(d.sid(1)[MODE_VOL], 0x0C);
    CHECK_EQ(d.sid(0)[MODE_VOL], 0x03);
}

TEST(cia_timer_a_interrupts_the_6510_every_latch_plus_one_clocks)
{
    SoundCard c;
    c.ram()[0xFFFE] = 0x00; // IRQ: $1100
    c.ram()[0xFFFF] = 0x11;
    uint16_t a = 0x1100;
    for (const unsigned b : {0xADu, 0x0Du, 0xDCu, // LDA $DC0D
                             0xE6u, 0x20u,        // INC $20
                             0xD0u, 0x02u,        // BNE +2
                             0xE6u, 0x21u,        // INC $21
                             0x40u})              // RTI
        c.ram()[a++] = static_cast<uint8_t>(b);
    start(c, 0x1000,
          {
              0xA9, 0x63,       // LDA #99
              0x8D, 0x04, 0xDC, // STA $DC04     latch 99: a period of 100
              0xA9, 0x00,       // LDA #0
              0x8D, 0x05, 0xDC, // STA $DC05     stopped, so it loads the counter too
              0xA9, 0x81,       // LDA #$81
              0x8D, 0x0D, 0xDC, // STA $DC0D     timer A's interrupt on
              0xA9, 0x01,       // LDA #$01
              0x8D, 0x0E, 0xDC, // STA $DC0E     start
              0x58,             // CLI
              0x4C, 0x15, 0x10, // JMP *
          });
    CHECK_EQ(c.timer_latch(), 0xFFFF);
    // 100,000 SID clocks: 1,000 periods, less the setup's 30 cycles
    host(c, 100'000ull * SoundCard::NATIVE_HZ / SoundCard::PAL_HZ);
    CHECK_EQ(c.timer_latch(), 99);
    CHECK(c.timer_running());
    const unsigned irqs = c.ram()[0x20] | static_cast<unsigned>(c.ram()[0x21]) << 8;
    CHECK(irqs >= 998 && irqs <= 1000); // a period of 99 would make it 1,010
}

TEST(the_6510s_interrupt_entry_takes_sid_clocks_too)
{
    // The same timer and handler: every cycle the 6510 spends, its 7-cycle
    // interrupt entries included, is a SID clock and a CIA clock.
    SoundCard c;
    c.ram()[0xFFFE] = 0x00;
    c.ram()[0xFFFF] = 0x11;
    uint16_t a = 0x1100;
    for (const unsigned b : {0xADu, 0x0Du, 0xDCu, 0xE6u, 0x20u, 0x40u}) // LDA $DC0D; INC $20; RTI
        c.ram()[a++] = static_cast<uint8_t>(b);
    start(c, 0x1000, {0xA9, 0x63, 0x8D, 0x04, 0xDC, 0xA9, 0x00, 0x8D, 0x05, 0xDC, 0xA9, 0x81, 0x8D, 0x0D, 0xDC, 0xA9,
                      0x01, 0x8D, 0x0E, 0xDC, 0x58, 0x4C, 0x15, 0x10});
    const uint64_t cpu0 = c.cpu().cycles(), sid0 = c.sid_clocks();
    host(c, 10'000ull * SoundCard::NATIVE_HZ / SoundCard::PAL_HZ);
    CHECK(c.ram()[0x20] >= 90); // about 100 interrupts
    CHECK_EQ(c.cpu().cycles() - cpu0, c.sid_clocks() - sid0);
}

TEST(cia_timer_a_reloads_from_the_latch_a_new_latch_waits_and_load_strobes)
{
    // Continuous, latch 99: five underflows; the latch to 199 while it runs
    // (taking effect at the next underflow); five more; then CRA = $11, LOAD
    // and start, which reloads at once. The host watches the counter jump up.
    SoundCard c;
    c.set_cpu_hz(SoundCard::PAL_HZ); // a tick is a SID clock
    start(c, 0x1000,
          {
              0xA9, 0x63, 0x8D, 0x04, 0xDC, // LDA #99 STA $DC04
              0xA9, 0x00, 0x8D, 0x05, 0xDC, // LDA #0 STA $DC05     stopped: loads the counter
              0xA9, 0x01, 0x8D, 0x0E, 0xDC, // LDA #1 STA $DC0E     start, continuous
              0xA2, 0x05,                   // LDX #5
              0xAD, 0x0D, 0xDC,             // LDA $DC0D           wait for an underflow
              0x29, 0x01,                   // AND #1
              0xF0, 0xF9,                   // BEQ the LDA
              0xCA, 0xD0, 0xF6,             // DEX BNE the LDA
              0xA9, 0xC7, 0x8D, 0x04, 0xDC, // LDA #199 STA $DC04  running: the latch only
              0xA2, 0x05,                   // LDX #5
              0xAD, 0x0D, 0xDC, 0x29, 0x01, 0xF0, 0xF9, 0xCA, 0xD0, 0xF6,
              0xA2, 0x20, 0xCA, 0xD0, 0xFD, // LDX #32 DEX BNE: into the period
              0xA9, 0x11, 0x8D, 0x0E, 0xDC, // LDA #$11 STA $DC0E  LOAD and start
              0x4C, 0x40, 0x10,             // JMP *
          },
          0);
    struct Jump
    {
        uint64_t at;
        unsigned from, to;
    };
    std::vector<Jump> jumps;
    unsigned last = c.timer();
    for (unsigned i = 0; i < 4000; ++i)
    {
        c.tick(1);
        if (c.timer() > last)
            jumps.push_back({c.sid_clocks(), last, c.timer()});
        last = c.timer();
    }
    // When each reload happened, to the clock (the counter has counted on
    // by however far the instruction ran past it), and the periods between:
    // -1 marks the strobe, which reloads mid-period.
    std::vector<long> seq;
    uint64_t prev = 0;
    for (size_t k = 0; k < jumps.size(); ++k)
    {
        const Jump &j = jumps[k];
        const unsigned latch = j.to <= 99 ? 99 : 199;
        const uint64_t when = j.at - (latch - j.to);
        if (j.from > 8)
            seq.push_back(-1);
        else if (k)
            seq.push_back(static_cast<long>(when - prev));
        prev = when;
    }
    // Five periods of 100 (the fifth runs out after the latch became 199:
    // a new latch waits for the underflow), four of 200, the strobe, 200s.
    CHECK(seq.size() >= 12);
    for (size_t k = 0; k < seq.size(); ++k)
        CHECK_EQ(seq[k], k < 5 ? 100L : k < 9 ? 200L : k == 9 ? -1L : 200L);
}

TEST(cia_one_shot_stops_and_reading_icr_acknowledges)
{
    SoundCard c;
    start(c, 0x1000,
          {
              0xA9, 0x63,       // LDA #99
              0x8D, 0x04, 0xDC, // STA $DC04
              0xA9, 0x00,       // LDA #0
              0x8D, 0x05, 0xDC, // STA $DC05
              0xA9, 0x09,       // LDA #$09
              0x8D, 0x0E, 0xDC, // STA $DC0E     start, one-shot
              0xA2, 0x40,       // LDX #$40      wait: 64 x 5 clocks
              0xCA,             // DEX
              0xD0, 0xFD,       // BNE *-1
              0xAD, 0x0D, 0xDC, // LDA $DC0D     the flag, no interrupt (the mask is clear)
              0x85, 0x40,       // STA $40
              0xAD, 0x0D, 0xDC, // LDA $DC0D     gone
              0x85, 0x41,       // STA $41
              0x02,             // JAM
          });
    host(c, HOST_SECOND / 100);
    CHECK(c.cpu().state() == Cpu6502::State::Stopped);
    CHECK_EQ(c.ram()[0x40], 0x01);
    CHECK_EQ(c.ram()[0x41], 0x00);
    CHECK(!c.timer_running());
    CHECK_EQ(c.timer(), 99); // reloaded, then stopped
    CHECK_EQ(c.timer_flags(), 0x00);
}

TEST(the_frame_interrupt_comes_once_a_frame_until_de03_is_cleared)
{
    // $1100: LDA #1 STA $DE03 (clear), INC $20, RTI. Main: $DE04 = 1, CLI, JMP *.
    for (const bool ntsc : {false, true})
    {
        SoundCard c;
        c.ram()[0xFFFE] = 0x00;
        c.ram()[0xFFFF] = 0x11;
        uint16_t a = 0x1100;
        for (const unsigned b : {0xA9u, 0x01u, 0x8Du, 0x03u, 0xDEu, 0xE6u, 0x20u, 0x40u})
            c.ram()[a++] = static_cast<uint8_t>(b);
        start(c, 0x1000, {0xA9, 0x01, 0x8D, 0x04, 0xDE, 0x58, 0x4C, 0x06, 0x10},
              ntsc ? SoundCard::CTRL_NTSC : 0);
        const uint32_t hz = ntsc ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ;
        const uint32_t frame = ntsc ? SoundCard::FRAME_NTSC : SoundCard::FRAME_PAL;
        host(c, (100ull * frame + frame / 2) * SoundCard::NATIVE_HZ / hz);
        CHECK_EQ(c.ram()[0x20], 100);
        CHECK(c.frame_enabled());
        CHECK_EQ(c.timer_flags(), 0x00); // CIA 1 not involved
    }
}

TEST(the_frame_latch_is_de03_bit_0_and_neither_cias_icr_nor_the_vic_reach_it)
{
    SoundCard c;
    c.ram()[0xFFFE] = 0x00; // an interrupt would land on the JAM at $1100
    c.ram()[0xFFFF] = 0x11;
    c.ram()[0x1100] = 0x02;
    start(c, 0x1000,
          {
              0xAD, 0x03, 0xDE, // LDA $DE03     no frame yet: $00
              0x85, 0x40,       // STA $40
              0xA9, 0xFF,       // LDA #$FF
              0x8D, 0x1A, 0xD0, // STA $D01A     the VIC-II's enable: nothing here
              0x58,             // CLI
              0xA2, 0x10,       // LDX #$10      wait over a frame: 16 x 256 x 5 clocks
              0xA0, 0x00,       // LDY #0
              0x88,             // DEY
              0xD0, 0xFD,       // BNE *-1
              0xCA,             // DEX
              0xD0, 0xF8,       // BNE *-6
              0x78,             // SEI
              0xAD, 0x0D, 0xDC, // LDA $DC0D     CIA 1's ICR: not the frame's
              0xAD, 0x19, 0xD0, // LDA $D019     the VIC-II's: $FF, as on main
              0x85, 0x44,       // STA $44
              0xAD, 0x03, 0xDE, // LDA $DE03     latched, its interrupt off: $01
              0x85, 0x41,       // STA $41
              0xA9, 0x01,       // LDA #1
              0x8D, 0x03, 0xDE, // STA $DE03     cleared
              0xAD, 0x03, 0xDE, // LDA $DE03
              0x85, 0x42,       // STA $42
              0xAD, 0x04, 0xDE, // LDA $DE04     off: $00
              0x85, 0x43,       // STA $43
              0x02,             // JAM
          });
    host(c, HOST_SECOND / 10);
    CHECK(c.cpu().state() == Cpu6502::State::Stopped);
    CHECK(c.cpu().regs().pc != 0x1101); // not by an interrupt
    CHECK_EQ(c.ram()[0x40], 0x00);
    CHECK_EQ(c.ram()[0x41], 0x01);
    CHECK_EQ(c.ram()[0x42], 0x00);
    CHECK_EQ(c.ram()[0x43], 0x00);
    CHECK_EQ(c.ram()[0x44], 0xFF);
}

TEST(d012_and_d011_bit_7_read_the_raster_line_with_frameen_bit_1)
{
    // FRAMEEN = 2; wait for line 256+ ($D011 bit 7), note $D012; wait for it to clear, note $D012 again.
    for (const bool ntsc : {false, true})
    {
        SoundCard c;
        start(c, 0x1000,
              {
                  0xA9, 0x02,       // LDA #2
                  0x8D, 0x04, 0xDE, // STA $DE04    the raster line shown, no interrupt
                  0x2C, 0x11, 0xD0, // BIT $D011
                  0x10, 0xFB,       // BPL *-3      to line 256
                  0xAD, 0x12, 0xD0, // LDA $D012
                  0x85, 0x40,       // STA $40
                  0x2C, 0x11, 0xD0, // BIT $D011
                  0x30, 0xFB,       // BMI *-3      to line 0
                  0xAD, 0x12, 0xD0, // LDA $D012
                  0x85, 0x41,       // STA $41
                  0xAD, 0x12, 0xD0, // LDA $D012    wait for line 100
                  0xC9, 0x64,       // CMP #100
                  0xD0, 0xF9,       // BNE *-5
                  0xAD, 0x11, 0xD0, // LDA $D011
                  0x85, 0x42,       // STA $42
                  0x02,             // JAM
              },
              ntsc ? SoundCard::CTRL_NTSC : 0);
        host(c, HOST_SECOND / 10);
        CHECK(c.cpu().state() == Cpu6502::State::Stopped);
        CHECK(c.ram()[0x40] <= 1);  // line 256 or 257, as the 4-cycle BIT lands
        CHECK(c.ram()[0x41] <= 1);  // line 0 or 1
        CHECK_EQ(c.ram()[0x42], 0x7F);
        CHECK(c.raster_shown());
        CHECK(!c.frame_enabled());
    }
}

TEST(without_frameen_bit_1_d011_and_d012_read_ff)
{
    SoundCard c;
    start(c, 0x1000,
          {
              0xA2, 0x00,       // LDX #0       over a frame: note $D012 and $D011 at 256 points
              0xAD, 0x12, 0xD0, // LDA $D012
              0x2D, 0x11, 0xD0, // AND $D011
              0x9D, 0x00, 0x20, // STA $2000,X
              0xA0, 0x10,       // LDY #16
              0x88,             // DEY
              0xD0, 0xFD,       // BNE *-1
              0xE8,             // INX
              0xD0, 0xEF,       // BNE to the LDA
              0x02,             // JAM
          });
    host(c, HOST_SECOND / 10);
    CHECK(c.cpu().state() == Cpu6502::State::Stopped);
    unsigned ff = 0;
    for (unsigned i = 0; i < 256; ++i)
        ff += c.ram()[0x2000 + i] == 0xFF ? 1 : 0;
    CHECK_EQ(ff, 256u);
    CHECK(!c.raster_shown());
}

TEST(the_raster_line_stays_in_the_frame_when_ctrl_changes_the_clock)
{
    // Early in a PAL frame (19,656 clocks), switch to NTSC (17,095) without RESET.
    SoundCard c;
    start(c, 0x1000, {0xA9, 0x02, 0x8D, 0x04, 0xDE, 0x4C, 0x05, 0x10}); // FRAMEEN = 2; JMP *
    host(c, 2'000ull * SoundCard::NATIVE_HZ / SoundCard::PAL_HZ);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC);
    for (unsigned i = 0; i < 400; ++i)
    {
        const unsigned line = c.raster_line();
        const unsigned seen = c.cpu_bus().peek(0xD012) | (c.cpu_bus().peek(0xD011) & 0x80u) << 1;
        CHECK(line < SoundCard::LINES_NTSC);
        CHECK_EQ(seen, line);
        if (line >= SoundCard::LINES_NTSC)
            break;
        host(c, 50 * 7);
    }
}

TEST(ctrl_reset_restarts_the_6510_and_the_cia)
{
    SoundCard c;
    start(c, 0x1000, {0xA9, 0x01, 0x8D, 0x0E, 0xDC, 0x4C, 0x05, 0x10}); // start timer A; JMP *
    host(c, 10'000);
    CHECK(c.timer_running());
    c.write(SoundCard::CTRL, SoundCard::CTRL_RESET); // stopped, and reset
    CHECK(!c.timer_running());
    CHECK_EQ(c.cpu().regs().pc, 0x1000);
    CHECK_EQ(c.cpu_resets(), 2u);
}

TEST(ctrl_reset_clears_frameen_and_the_frame_latch)
{
    SoundCard c;
    start(c, 0x1000, {0xA9, 0x03, 0x8D, 0x04, 0xDE, 0x4C, 0x05, 0x10}); // FRAMEEN = 3 (I set: no interrupt); JMP *
    host(c, 30'000ull * SoundCard::NATIVE_HZ / SoundCard::PAL_HZ);         // past a frame
    CHECK(c.frame_enabled());
    CHECK(c.raster_shown());
    CHECK_EQ(c.frame_flags(), 0x01);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RESET);
    CHECK(!c.frame_enabled());
    CHECK(!c.raster_shown());
    CHECK_EQ(c.frame_flags(), 0x00);
    CHECK_EQ(c.raster_line(), 0u); // the frame starts over
    c.write(SoundCard::CTRL, SoundCard::CTRL_RUN); // I/O in by the port's pull-ups
    CHECK_EQ(c.cpu_bus().peek(0xD012), 0xFF);
    CHECK_EQ(c.cpu_bus().peek(0xD011), 0xFF);
}

TEST(on_the_backplane_at_c500_with_its_interrupt_on_irqh_bit_2)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                            "sound 500-57F space=io name=sound irq\n",
                            "sound.machine", {}, error);
    REQUIRE(m != nullptr);
    SoundCard *sound = m->sound_card();
    REQUIRE(sound != nullptr);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC57F), 0xA2);
    bus.write(0xC518, 0x0F);
    CHECK_EQ(sound->sid(0)[MODE_VOL], 0x0F);
    CHECK_EQ(bus.peek(0xC689), 0xFF);
    bus.write(0xC542, SoundCard::IEN_MAILBOX);
    sound->card_send(7);
    CHECK(bus.irq());
    CHECK_EQ(bus.peek(0xC689), 0xFB); // IRQH: bit 2 low, module A
    CHECK_EQ(bus.read(0xC547), 7);
    CHECK(!bus.irq());
    bus.reset(); // the reset button, through the north bridge
    CHECK_EQ(sound->sid(0)[MODE_VOL], 0x00);
}

TEST(only_one_per_machine)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane\nram 00000-FFFFF module=0\n"
                            "sound 500-57F space=io\nsound 480-4FF space=io\n",
                            "two.machine", {}, error);
    CHECK(m == nullptr);
    CHECK(error.find("sound: only one per machine") != std::string::npos);
}

TEST(dma_loads_a_tune_into_the_cards_ram)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                            "dma 400-47F space=io name=dma irq\nsound 500-57F space=io name=sound irq drq=2\n",
                            "sound.machine", {}, error);
    REQUIRE(m != nullptr);
    SoundCard *sound = m->sound_card();
    Decoder &bus = m->bus();
    for (unsigned i = 0; i < 300; ++i)
        bus.write_physical(0x02000 + i, static_cast<uint8_t>(i * 7));
    bus.write(0xC543, 0x00); // RAMADDR $1000, a tune's load address
    bus.write(0xC544, 0x10);
    // channel 2: memory at $02000 to the card, 300 bytes, block mode
    bus.poke(0xC420, 0x00);
    bus.poke(0xC421, 0x20);
    bus.poke(0xC422, 0x00);
    bus.poke(0xC424, 300 & 0xFF);
    bus.poke(0xC425, 300 >> 8);
    bus.poke(0xC426, DmaController::BLOCK | DmaController::TO_CARD);
    bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
    bus.poke(0xC427, DmaController::ENABLE);
    for (int i = 0; i < 400; ++i)
        bus.read(0x0000);
    bool same = true;
    for (unsigned i = 0; i < 300; ++i)
        same = same && sound->ram()[0x1000 + i] == static_cast<uint8_t>(i * 7);
    CHECK(same);
    CHECK_EQ(sound->ram()[0x1000 + 300], 0x00); // and no further
    CHECK_EQ(bus.peek(0xC543), (0x1000 + 300) & 0xFF);
    CHECK_EQ(bus.peek(0xC544), (0x1000 + 300) >> 8);
}

// --- revision 2 (hw/sound-rev2.md): the registers ----------------------------

namespace
{
    /// Revision 2's registers, $48-$63, as the host reads them (peek: no latch, no index moved).
    std::vector<uint8_t> rev2(const SoundCard &c)
    {
        std::vector<uint8_t> out;
        for (unsigned o = SoundCard::PCMCTL; o <= SoundCard::DRQSEL; ++o)
            out.push_back(c.peek(o));
        return out;
    }

    /// Everything revision 2 adds, away from its reset value.
    void unsettle(SoundCard &c)
    {
        c.write(SoundCard::CODEC, 1);
        c.write(SoundCard::RATE, 0x44);
        c.write(SoundCard::RATE + 1, 0xAC);
        c.write(SoundCard::PCMFMT, 3);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE | SoundCard::PCMCTL_EOF);
        c.write(SoundCard::VOLL, 0x40);
        c.write(SoundCard::VOLR, 0x41);
        c.write(SoundCard::EQIDX, 3);
        c.write(SoundCard::EQVAL, 7);
        c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
        c.write(SoundCard::EQVAL, 1);
        c.write(SoundCard::VISIDX, 40);
        c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_AUTOINC);
        c.write(SoundCard::DOCADDR, Es5503::CONTROL + 5);
        c.write(SoundCard::DOCDATA, 0x06);          // oscillator 5 running: halt clear
        c.write(SoundCard::DOCADDR, Es5503::ENABLE);
        c.write(SoundCard::DOCDATA, 0x3E);          // all 32 enabled
        c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN | SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_IRQ_CARD);
        c.write(SoundCard::DOCBANK, 1);
        c.write(SoundCard::DOCADDR, 0x34);
        c.write(SoundCard::DOCADDR + 1, 0x12);
        c.write(SoundCard::DOCDATA, 0x99);          // wave RAM $11234
        c.write(SoundCard::DRQSEL, SoundCard::DRQ_DOCDATA);
    }
} // namespace

TEST(rev2_registers_reset_to_the_specs_values)
{
    SoundCard c;
    const std::vector<uint8_t> want = {
        0x00,             // $48 PCMCTL
        0x00,             // $49 CODEC: raw PCM
        0x00, 0x00,       // $4A-$4B RATE
        0x00,             // $4C PCMFMT
        0x00, 0x02,       // $4D-$4E FREE: 512 x 256 bytes, the FIFO empty
        0xFF,             // $4F PCMDATA: write only
        0x00,             // $50 STATE: idle
        0x00, 0x00, 0x00, 0x00, // $51-$54 POS
        0x00, 0x00,       // $55-$56 KBPS: no MP3
        0xFF, 0xFF,       // $57-$58 VOLL, VOLR: unity
        0x00,             // $59 EQIDX: the preamp
        0x00,             // $5A EQVAL: 0 dB
        0x00,             // $5B VISIDX
        0x00,             // $5C VISDATA: the spectrum's first band, silent
        0x83,             // $5D CODECS: raw PCM, MP3, an ES5503
        0x00,             // $5E DOCCTL
        0x00,             // $5F DOCDATA: the DOC's register 0, frequency low
        0x00, 0x00, 0x00, // $60-$62 DOCADDR, DOCBANK
        0x00,             // $63 DRQSEL: RAMDATA
    };
    CHECK(rev2(c) == want);
    CHECK(c.drq());
}

TEST(pcmctl_keeps_run_pause_and_eof_and_codec_holds_while_running)
{
    SoundCard c;
    c.write(SoundCard::PCMCTL, 0xFF);
    CHECK_EQ(c.read(SoundCard::PCMCTL), 0x0B); // FLUSH reads 0, bits 4-7 reserved
    c.write(SoundCard::CODEC, 2);              // ignored while RUN is set
    CHECK_EQ(c.read(SoundCard::CODEC), 0);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_FLUSH);
    CHECK_EQ(c.read(SoundCard::PCMCTL), 0);
    c.write(SoundCard::CODEC, 1);
    CHECK_EQ(c.read(SoundCard::CODEC), 1);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    c.write(SoundCard::CODEC, 0);
    CHECK_EQ(c.read(SoundCard::CODEC), 1);
}

TEST(rate_format_and_volumes_read_back)
{
    SoundCard c;
    c.write(SoundCard::RATE, 44100 & 0xFF);
    c.write(SoundCard::RATE + 1, 44100 >> 8);
    CHECK_EQ(c.read(SoundCard::RATE) | c.read(SoundCard::RATE + 1) << 8, 44100);
    c.write(SoundCard::PCMFMT, 0xFF);
    CHECK_EQ(c.read(SoundCard::PCMFMT), SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT);
    c.write(SoundCard::VOLL, 0);
    c.write(SoundCard::VOLR, 0x80);
    CHECK_EQ(c.read(SoundCard::VOLL), 0);
    CHECK_EQ(c.read(SoundCard::VOLR), 0x80);
    c.write(SoundCard::STATE, 3);       // read only
    c.write(SoundCard::KBPS, 0x80);
    c.write(SoundCard::CODECS, 0);
    c.write(SoundCard::FREE, 0);
    CHECK_EQ(c.read(SoundCard::STATE), 0);
    CHECK_EQ(c.read(SoundCard::KBPS), 0);
    CHECK_EQ(c.read(SoundCard::CODECS), 0x83);
    c.write(SoundCard::PCMDATA, 0x55);  // no FIFO yet: dropped, and FREE still the whole of it
    CHECK_EQ(c.read(SoundCard::FREE), 0x00);
    CHECK_EQ(c.read(SoundCard::FREE + 1), 0x02);
    CHECK_EQ(c.read(SoundCard::PCMDATA), 0xFF);
}

TEST(pos_reads_through_its_latch_and_a_write_to_51_zeroes_it)
{
    SoundCard c;
    for (unsigned o = SoundCard::POS; o < SoundCard::POS + 4; ++o)
        c.write(o, 0x5A); // $52-$54 ignore writes; $51 zeroes
    CHECK_EQ(c.read(SoundCard::POS), 0);
    CHECK_EQ(c.read(SoundCard::POS + 1), 0);
    CHECK_EQ(c.read(SoundCard::POS + 2), 0);
    CHECK_EQ(c.read(SoundCard::POS + 3), 0);
}

TEST(eqval_follows_eqidx_clamps_and_moves_it_on)
{
    SoundCard c;
    const int8_t gains[11] = {-30, 30, 24, -24, 1, -1, 0, 12, -12, 25, -128};
    for (const int8_t g : gains)
        c.write(SoundCard::EQVAL, static_cast<uint8_t>(g)); // the preamp, then bands 1-10
    CHECK_EQ(c.read(SoundCard::EQIDX), 11);
    c.write(SoundCard::EQVAL, 9);                           // index 11 is nothing
    CHECK_EQ(c.read(SoundCard::EQIDX), 12);
    c.write(SoundCard::EQIDX, 11);
    CHECK_EQ(c.read(SoundCard::EQVAL), 0);
    c.write(SoundCard::EQIDX, 0);
    const int8_t want[11] = {-24, 24, 24, -24, 1, -1, 0, 12, -12, 24, -24};
    bool same = true;
    for (const int8_t w : want)
        same = same && static_cast<int8_t>(c.read(SoundCard::EQVAL)) == w;
    CHECK(same);
    CHECK_EQ(c.read(SoundCard::EQIDX), 11);
    CHECK_EQ(c.peek(SoundCard::EQVAL), 0);  // the debugger's look moves nothing
    CHECK_EQ(c.read(SoundCard::EQIDX), 11);
    // Bit 7: the enable, bit 0 of EQVAL; the index moves on in bits 0-6.
    c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
    CHECK_EQ(c.read(SoundCard::EQVAL), 0);
    CHECK_EQ(c.read(SoundCard::EQIDX), 0x81);
    c.write(SoundCard::EQVAL, 0xFF);
    c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
    CHECK_EQ(c.read(SoundCard::EQVAL), 1);
    c.write(SoundCard::EQIDX, 0xFF);
    c.read(SoundCard::EQVAL);
    CHECK_EQ(c.read(SoundCard::EQIDX), 0x80); // wraps within bits 0-6
    c.write(SoundCard::EQIDX, 1);
    CHECK_EQ(static_cast<int8_t>(c.read(SoundCard::EQVAL)), 24); // the enable left the gains alone
}

TEST(the_vis_page_is_silence_and_visdata_moves_visidx_on)
{
    SoundCard c;
    c.write(SoundCard::VISIDX, 0); // a snapshot
    std::vector<uint8_t> page;
    for (unsigned i = 0; i < 100; ++i)
        page.push_back(c.read(SoundCard::VISDATA));
    std::vector<uint8_t> want(100, 0x00);       // spectrum, VU: -60 dBFS
    std::fill(want.begin() + 34, want.begin() + 98, uint8_t{0x80}); // the samples at zero; past 97, 0
    CHECK(page == want);
    CHECK_EQ(c.read(SoundCard::VISIDX), 100);
    c.write(SoundCard::VISIDX, 40);
    CHECK_EQ(c.peek(SoundCard::VISDATA), 0x80);
    CHECK_EQ(c.read(SoundCard::VISIDX), 40);
    c.write(SoundCard::VISIDX, 0xFF);
    c.read(SoundCard::VISDATA);
    CHECK_EQ(c.read(SoundCard::VISIDX), 0);
}

TEST(docdata_reaches_wave_ram_and_moves_on_across_the_bank_and_round)
{
    SoundCard c;
    c.write(SoundCard::DOCCTL, 0xFF);
    CHECK_EQ(c.read(SoundCard::DOCCTL), 0x65); // RUN, IRQ to the 6510, auto-increment, RAM
    c.write(SoundCard::DOCADDR, 0xFF);
    c.write(SoundCard::DOCADDR + 1, 0xFF);
    c.write(SoundCard::DOCBANK, 0xFE);         // bit 0 only
    CHECK_EQ(c.read(SoundCard::DOCBANK), 0);
    for (const uint8_t b : std::vector<uint8_t>{0x11, 0x22, 0x33})
        c.write(SoundCard::DOCDATA, b);
    CHECK_EQ(c.doc().ram()[0x0FFFF], 0x11);
    CHECK_EQ(c.doc().ram()[0x10000], 0x22);    // carried into the bank
    CHECK_EQ(c.doc().ram()[0x10001], 0x33);
    CHECK_EQ(c.read(SoundCard::DOCBANK), 1);
    CHECK_EQ(c.read(SoundCard::DOCADDR + 1), 0x00);
    CHECK_EQ(c.read(SoundCard::DOCADDR), 0x02);
    c.write(SoundCard::DOCADDR, 0xFF);
    c.write(SoundCard::DOCADDR + 1, 0xFF);
    c.write(SoundCard::DOCDATA, 0x44);         // $1FFFF, then round to 0
    CHECK_EQ(c.doc().ram()[0x1FFFF], 0x44);
    CHECK_EQ(c.doc_address(), 0u);
    c.write(SoundCard::DOCBANK, 1);
    c.write(SoundCard::DOCADDR, 0x00);
    c.write(SoundCard::DOCADDR + 1, 0x00);
    CHECK_EQ(c.read(SoundCard::DOCDATA), 0x22);
    CHECK_EQ(c.peek(SoundCard::DOCDATA), 0x33); // the next; peek moves nothing
    CHECK_EQ(c.doc_address(), 0x10001u);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RAM); // no auto-increment: DOCADDR stays
    c.write(SoundCard::DOCDATA, 0x55);
    c.write(SoundCard::DOCDATA, 0x66);
    CHECK_EQ(c.doc().ram()[0x10001], 0x66);
    CHECK_EQ(c.doc_address(), 0x10001u);
}

TEST(docdata_reaches_the_docs_registers)
{
    SoundCard c;
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_AUTOINC); // registers, auto-increment
    c.write(SoundCard::DOCADDR, Es5503::FREQ_LO + 31);
    c.write(SoundCard::DOCDATA, 0x34);                  // the last oscillator's, then the first's high
    c.write(SoundCard::DOCDATA, 0x12);
    c.write(SoundCard::DOCADDR, Es5503::FREQ_LO + 31);
    CHECK_EQ(c.read(SoundCard::DOCDATA), 0x34);
    CHECK_EQ(c.read(SoundCard::DOCDATA), 0x12);
    std::vector<uint8_t> regs;
    for (const uint8_t r : std::vector<uint8_t>{Es5503::VOLUME, Es5503::DATA, Es5503::POINTER, Es5503::CONTROL,
                                                Es5503::TABLE, Es5503::INTERRUPT, Es5503::ENABLE, Es5503::ADC, 0xE3,
                                                0xFF})
    {
        c.write(SoundCard::DOCADDR, r);
        regs.push_back(c.read(SoundCard::DOCDATA));
    }
    // Volume 0, data $80, pointer 0, halted, table 0; no interrupt; one oscillator; no A/D input; nothing.
    CHECK(regs == (std::vector<uint8_t>{0x00, 0x80, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x80, 0x00, 0x00}));
    c.write(SoundCard::DOCCTL, 0);
    // Oscillator 1's registers, then the chip's.
    const std::vector<uint8_t> osc1 = {Es5503::VOLUME + 1, Es5503::DATA + 1, Es5503::POINTER + 1, Es5503::CONTROL + 1,
                                       Es5503::TABLE + 1,  Es5503::INTERRUPT, Es5503::ENABLE, Es5503::ADC};
    for (const uint8_t r : osc1)
    {
        c.write(SoundCard::DOCADDR, r);
        c.write(SoundCard::DOCDATA, 0xFF);
    }
    regs.clear();
    for (const uint8_t r : osc1)
    {
        c.write(SoundCard::DOCADDR, r);
        regs.push_back(c.read(SoundCard::DOCDATA));
    }
    // The data register and $E0 and $E2 take no writes; the table drops bit 7; $E1 32 oscillators.
    CHECK(regs == (std::vector<uint8_t>{0xFF, 0x80, 0xFF, 0xFF, 0x7F, 0xFF, 0x3E, 0x80}));
    CHECK_EQ(c.doc().enabled(), 32u);
    CHECK_EQ(c.doc_address(), 0x1E2u); // no auto-increment: still $E2 (the $FF read above carried into $61)
}

TEST(drqsel_routes_the_cards_drq_and_its_fly_by_bytes)
{
    SoundCard c;
    c.write(SoundCard::RAMADDR, 0x00);
    c.write(SoundCard::RAMADDR + 1, 0x20);
    c.dack_write(0x5A, false);                    // DRQSEL 0: RAMDATA
    CHECK_EQ(c.ram()[0x2000], 0x5A);
    c.write(SoundCard::DRQSEL, 0xFE);             // bits 0-1: DOCDATA
    CHECK_EQ(c.read(SoundCard::DRQSEL), SoundCard::DRQ_DOCDATA);
    CHECK(c.drq());
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC);
    c.write(SoundCard::DOCBANK, 1);
    c.dack_write(0x21, false);
    c.dack_write(0x22, true);
    CHECK_EQ(c.doc().ram()[0x10000], 0x21);
    CHECK_EQ(c.doc().ram()[0x10001], 0x22);
    c.write(SoundCard::DOCADDR, 0);
    CHECK_EQ(c.dack_read(false), 0x21);
    CHECK_EQ(c.read(SoundCard::RAMADDR + 1), 0x20); // RAMADDR left alone
    CHECK_EQ(c.read(SoundCard::RAMADDR), 0x01);
    c.write(SoundCard::DRQSEL, SoundCard::DRQ_PCMDATA); // the FIFO always has room, for now
    CHECK(c.drq());
    CHECK_EQ(c.dack_read(false), 0xFF);
    c.dack_write(0x77, false);                    // dropped: no FIFO yet
    CHECK_EQ(c.doc_address(), 0x10001u);
    CHECK_EQ(c.read(SoundCard::RAMADDR), 0x01);
    c.write(SoundCard::DRQSEL, 3);                // no source: no DRQ
    CHECK(!c.drq());
    CHECK_EQ(c.dack_read(false), 0xFF);
}

TEST(rev2_reset_is_reset_and_ctrl_reset_leaves_it_alone)
{
    SoundCard c;
    const std::vector<uint8_t> fresh = rev2(c);
    unsettle(c);
    const std::vector<uint8_t> set = rev2(c);
    CHECK(set != fresh);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RESET); // the 6510's reset: none of this
    CHECK(rev2(c) == set);
    CHECK_EQ(c.doc().peek(Es5503::CONTROL + 5), 0x06);
    CHECK_EQ(c.doc().enabled(), 32u);
    c.write(SoundCard::EQIDX, 3);
    CHECK_EQ(c.read(SoundCard::EQVAL), 7);

    c.reset(); // /RESET
    CHECK(rev2(c) == fresh);
    c.write(SoundCard::EQIDX, 3);
    CHECK_EQ(c.read(SoundCard::EQVAL), 0);           // the gains 0
    c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
    CHECK_EQ(c.read(SoundCard::EQVAL), 0);           // and EQ off
    CHECK_EQ(c.doc().peek(Es5503::CONTROL + 5), 0x07); // halted, the rest of control kept
    CHECK_EQ(c.doc().peek(Es5503::ENABLE), 0x00);
    CHECK_EQ(c.doc().enabled(), 1u);
    CHECK_EQ(c.doc().ram()[0x11234], 0x99);          // wave RAM keeps its contents
    bool halted = true;
    for (unsigned i = 0; i < Es5503::OSCILLATORS; ++i)
        halted = halted && (c.doc().peek(static_cast<uint8_t>(Es5503::CONTROL + i)) & Es5503::CONTROL_HALT);
    CHECK(halted);
}

int main()
{
    return run_all();
}

TEST(a_write_to_de02_stops_the_6510_itself)
{
    // The boot splash's tune ends this way: the SIDs are the host's again, as after a reset.
    SoundCard c;
    start(c, 0x1000, {0xE6, 0x10, 0xA5, 0x10, 0xC9, 0x05, 0xD0, 0xF8, // INC $10; LDA $10; CMP #5; BNE
                      0x8D, 0x02, 0xDE,                                  // STA $DE02
                      0xE6, 0x11, 0x4C, 0x0B, 0x10});                    // INC $11; JMP (back to INC)
    host(c, HOST_SECOND / 100);
    CHECK_EQ(c.ctrl() & SoundCard::CTRL_RUN, 0);
    CHECK_EQ(c.ram()[0x10], 5);
    CHECK_EQ(c.ram()[0x11], 0); // nothing after the write ran
    c.write(VOICE1_FREQ_LO, 0x34); // and the host has the SIDs
    CHECK_EQ(c.sid(0)[VOICE1_FREQ_LO], 0x34);
}
