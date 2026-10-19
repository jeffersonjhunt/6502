/**
 * @file test_acia.cpp
 * @brief The W65C51N ACIA's register rules.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "acia6551.h"

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

int main()
{
    return run_all();
}
