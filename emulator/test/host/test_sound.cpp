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
 */

#include "harness.h"

#include "dma_controller.h"
#include "machine.h"
#include "sound_card.h"

#include <initializer_list>
#include <string>

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
    CHECK_EQ(c.peek(SoundCard::ID_REG), 0xA1);
    CHECK_EQ(c.read(0x1D), 0xFF); // past SID A's 29 registers
    CHECK_EQ(c.read(0x3F), 0xFF);
    CHECK_EQ(c.read(0x48), 0xFF);
    CHECK_EQ(c.read(0xFF), 0xA1); // the block repeats every $80
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
    CHECK_EQ(bus.peek(0xC57F), 0xA1);
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

int main()
{
    return run_all();
}
