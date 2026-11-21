/**
 * @file test_acia.cpp
 * @brief The W65C51N ACIA's register rules.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "acia6551.h"

#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t DATA = 0, STATUS = 1, COMMAND = 2, CONTROL = 3;
    constexpr uint8_t EATER_COMMAND = 0x0B; // DTR, no rx IRQ, transmitter on (Eater's Woz Monitor)
} // namespace

TEST(tdre_is_always_set_the_w65c51n_bug)
{
    Acia6551 a;
    CHECK(a.read(STATUS) & Acia6551::STATUS_TDRE);
    a.write(COMMAND, EATER_COMMAND);
    a.write(DATA, 'A');
    CHECK(a.read(STATUS) & Acia6551::STATUS_TDRE); // even straight after a write
}

TEST(transmit_needs_dtr_and_the_transmitter_on)
{
    Acia6551 a;
    std::vector<uint8_t> sent;
    a.on_transmit([&](uint8_t b) { sent.push_back(b); });
    a.write(DATA, 'x'); // after reset: DTR off
    a.write(COMMAND, Acia6551::CMD_DTR | Acia6551::CMD_RX_IRQ_OFF); // DTR, but TIC = 00: transmitter off
    a.write(DATA, 'y');
    a.write(COMMAND, EATER_COMMAND);
    a.write(DATA, 'z');
    CHECK(sent == std::vector<uint8_t>{'z'});
}

TEST(receive_sets_rdrf_and_reading_data_clears_it)
{
    Acia6551 a;
    CHECK(!a.receive('q')); // receiver off until DTR
    a.write(COMMAND, EATER_COMMAND);
    CHECK(a.receive('q'));
    CHECK(a.read(STATUS) & Acia6551::STATUS_RDRF);
    CHECK(!a.receive('r')); // unread: the host waits
    CHECK_EQ(a.peek(DATA), 'q');
    CHECK(a.read(STATUS) & Acia6551::STATUS_RDRF); // peek did not take it
    CHECK_EQ(a.read(DATA), 'q');
    CHECK(!(a.read(STATUS) & Acia6551::STATUS_RDRF));
    CHECK(a.receive('r'));
    CHECK(!a.irq()); // command bit 1 set: no receive interrupt
}

TEST(receive_interrupt_and_status_read_clears_it)
{
    Acia6551 a;
    a.write(COMMAND, Acia6551::CMD_DTR | 0x08); // rx IRQ enabled
    CHECK(a.receive('k'));
    CHECK(a.irq());
    CHECK_EQ(a.peek(STATUS) & Acia6551::STATUS_IRQ, Acia6551::STATUS_IRQ);
    CHECK(a.read(STATUS) & Acia6551::STATUS_IRQ); // reads set...
    CHECK(!a.irq());                              // ...and that read cleared it
    CHECK(a.read(STATUS) & Acia6551::STATUS_RDRF); // the byte is still there
}

TEST(programmed_reset_clears_command_low_bits_keeps_control)
{
    Acia6551 a;
    a.write(CONTROL, 0x1F);
    a.write(COMMAND, 0xEB);
    a.write(STATUS, 0); // any value
    CHECK_EQ(a.read(COMMAND), 0xE0);
    CHECK_EQ(a.read(CONTROL), 0x1F);
    CHECK_EQ(a.read(4 + CONTROL), 0x1F); // mirrors on the low two bits
}

// The paced line (S1 of the serial receive buffer, hw/oodos.md): a sender that doesn't wait.

TEST(paced_bytes_into_a_reader_that_keeps_up_all_arrive_and_ovrn_stays_clear)
{
    Acia6551 a;
    a.write(CONTROL, 0x1F);
    a.write(COMMAND, EATER_COMMAND);
    a.send_paced("HELLO", 1000);
    std::string got;
    for (int i = 0; i < 60; ++i) // 100 cycles a step, the reader looking each step
    {
        a.tick(100);
        const uint8_t st = a.read(STATUS);
        CHECK(!(st & Acia6551::STATUS_OVRN));
        if (st & Acia6551::STATUS_RDRF)
            got += static_cast<char>(a.read(DATA));
    }
    CHECK_EQ(got, std::string("HELLO"));
    CHECK_EQ(a.overruns(), 0u);
    CHECK_EQ(a.paced_waiting(), 0u);
}

TEST(a_paced_byte_with_rdrf_set_sets_ovrn_keeps_the_held_byte_and_is_counted_lost)
{
    Acia6551 a;
    a.write(COMMAND, Acia6551::CMD_DTR | 0x08); // the receiver's interrupt on
    a.send_paced("AB", 500);
    a.tick(500);
    CHECK(a.irq());
    CHECK(a.read(STATUS) & Acia6551::STATUS_RDRF); // the interrupt's flag read away, data not
    a.tick(500);                                   // B, onto the unread A
    const uint8_t st = a.read(STATUS);
    CHECK(st & Acia6551::STATUS_OVRN);
    CHECK(!(st & Acia6551::STATUS_IRQ)); // no interrupt for an overrun
    CHECK_EQ(a.overruns(), 1u);
    CHECK_EQ(a.read(DATA), 'A'); // the held byte, not B
    CHECK(!(a.read(STATUS) & (Acia6551::STATUS_OVRN | Acia6551::STATUS_RDRF))); // data's read cleared both
}

TEST(the_waiting_receive_is_unchanged_it_refuses_and_never_sets_ovrn)
{
    Acia6551 a;
    a.write(COMMAND, EATER_COMMAND);
    CHECK(a.receive('x'));
    CHECK(!a.receive('y')); // refused, not lost
    CHECK(!(a.read(STATUS) & Acia6551::STATUS_OVRN));
    CHECK_EQ(a.overruns(), 0u);
    CHECK_EQ(a.paced_waiting(), 0u); // not queued on the paced line either
    CHECK_EQ(a.read(DATA), 'x');
    CHECK(a.receive('y'));
    CHECK_EQ(a.read(DATA), 'y');
}

TEST(the_paced_period_follows_control)
{
    Acia6551 a;
    a.write(CONTROL, 0x1F); // 19,200, 8-N-1: 10 bits of 7,159,090 / 19,200 cycles
    CHECK_EQ(a.period(), 3729u);
    a.write(CONTROL, 0x1E); // 9,600
    CHECK_EQ(a.period(), 7457u);
    a.write(CONTROL, 0x9F); // 19,200 with 2 stop bits: 11 bits
    CHECK_EQ(a.period(), 4102u);
    a.write(CONTROL, 0x3F); // 7 data bits: 9
    CHECK_EQ(a.period(), 3356u);
    // A paced byte with no period of its own comes after CONTROL's: 9,600's, not 19,200's.
    a.write(CONTROL, 0x1E);
    a.write(COMMAND, EATER_COMMAND);
    a.send_paced("ZY");
    for (const char want : {'Z', 'Y'}) // each a whole period after the last
    {
        a.tick(7456);
        CHECK(!(a.read(STATUS) & Acia6551::STATUS_RDRF));
        a.tick(1);
        CHECK(a.read(STATUS) & Acia6551::STATUS_RDRF);
        CHECK_EQ(a.read(DATA), want);
    }
    CHECK(a.rts());
    a.write(COMMAND, Acia6551::CMD_DTR | Acia6551::CMD_RX_IRQ_OFF); // TIC 00: RTS high
    CHECK(!a.rts());
}

int main()
{
    return run_all();
}
