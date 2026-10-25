/**
 * @file test_sound.cpp
 * @brief The sound card (hw/design.md, "Sound"): the host's side of it.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card alone: the SIDs' registers as the host sees them, the card's RAM
 * through RAMADDR and RAMDATA, the mailbox from both sides, and /RESET. Then
 * on a backplane machine: its block at $C500, its interrupt in IRQH, a tune's
 * bytes loaded by DMA, and /RESET from the north bridge.
 */

#include "harness.h"

#include "dma_controller.h"
#include "machine.h"
#include "sound_card.h"

#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint8_t VOICE1_FREQ_LO = 0x00, VOICE1_CTRL = 0x04, MODE_VOL = 0x18;
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
