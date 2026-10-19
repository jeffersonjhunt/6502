/**
 * @file test_lcd.cpp
 * @brief The HD44780 through a 6522, and the PS/2 keyboard's bytes.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The LCD is driven exactly as a program would: stores to the VIA's port
 * registers, E pulsed by hand, the busy flag read back through the port
 * with DDRB switched to input. Both of Eater's wirings are here: 8 data
 * lines on port B with RS/RW/E on PA5-7, and 4 on PB0-3 with RS/RW/E on
 * PB4-6.
 */

#include "harness.h"

#include "hd44780.h"
#include "ps2_keyboard.h"
#include "via6522.h"

#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t ORB = 0, ORA = 1, DDRB = 2, DDRA = 3, PCR = 0xC, IER = 0xE;
    using P = Via6522::Port;

    /// Eater's 8-bit wiring (hello-world-final.s).
    struct Lcd8
    {
        static constexpr uint8_t E = 0x80, RW = 0x40, RS = 0x20;
        Lcd8() : lcd(via, {P::B, 0, 8}, {P::A, 5}, {P::A, 6}, {P::A, 7}, 16, 2)
        {
            via.write(DDRB, 0xFF);
            via.write(DDRA, 0xE0);
        }
        void send(uint8_t value, uint8_t rs)
        {
            via.write(ORB, value);
            via.write(ORA, rs);
            via.write(ORA, static_cast<uint8_t>(rs | E));
            via.write(ORA, rs);
        }
        uint8_t read(uint8_t rs)
        {
            via.write(DDRB, 0x00);
            via.write(ORA, static_cast<uint8_t>(rs | RW));
            via.write(ORA, static_cast<uint8_t>(rs | RW | E));
            const uint8_t v = via.read(ORB);
            via.write(ORA, static_cast<uint8_t>(rs | RW));
            via.write(DDRB, 0xFF);
            return v;
        }
        void text(const std::string &s)
        {
            for (const char c : s)
                send(static_cast<uint8_t>(c), RS);
        }
        Via6522 via;
        ViaLcd lcd;
    };

    /// Eater's 4-bit wiring (keyboard.s): D4-7 on PB0-3, RS PB4, RW PB5, E PB6.
    struct Lcd4
    {
        static constexpr uint8_t E = 0x40, RW = 0x20, RS = 0x10;
        Lcd4() : lcd(via, {P::B, 0, 4}, {P::B, 4}, {P::B, 5}, {P::B, 6}, 16, 2) { via.write(DDRB, 0xFF); }
        void nibble(uint8_t n, uint8_t rs)
        {
            via.write(ORB, static_cast<uint8_t>(n | rs));
            via.write(ORB, static_cast<uint8_t>(n | rs | E));
            via.write(ORB, static_cast<uint8_t>(n | rs));
        }
        void send(uint8_t v, uint8_t rs)
        {
            nibble(static_cast<uint8_t>(v >> 4), rs);
            nibble(static_cast<uint8_t>(v & 0x0F), rs);
        }
        uint8_t read(uint8_t rs)
        {
            via.write(DDRB, 0xF0);
            via.write(ORB, static_cast<uint8_t>(rs | RW));
            via.write(ORB, static_cast<uint8_t>(rs | RW | E));
            const uint8_t hi = via.read(ORB) & 0x0F;
            via.write(ORB, static_cast<uint8_t>(rs | RW));
            via.write(ORB, static_cast<uint8_t>(rs | RW | E));
            const uint8_t lo = via.read(ORB) & 0x0F;
            via.write(ORB, static_cast<uint8_t>(rs | RW));
            via.write(DDRB, 0xFF);
            return static_cast<uint8_t>(hi << 4 | lo);
        }
        Via6522 via;
        ViaLcd lcd;
    };
} // namespace

TEST(eight_bit_hello)
{
    Lcd8 l;
    CHECK(!l.lcd.lcd().used());
    l.send(0x38, 0); // 8-bit, 2 lines
    l.send(0x0E, 0); // display on
    l.send(0x06, 0); // increment
    l.send(0x01, 0); // clear
    l.text("Hello, world!");
    CHECK_EQ(l.lcd.lcd().line(0), std::string("Hello, world!   "));
    CHECK_EQ(l.lcd.lcd().line(1), std::string(16, ' '));
    CHECK_EQ(l.read(0), 13); // busy clear, AC = 13
    l.send(0x80 | 0x40, 0);  // second line
    l.text("2nd");
    CHECK_EQ(l.lcd.lcd().line(1), std::string("2nd             "));
    l.send(0x80 | 0x00, 0);
    CHECK_EQ(l.read(Lcd8::RS), 'H'); // data read, which advances AC
    CHECK_EQ(l.read(0), 1);
}

TEST(four_bit_after_a_one_nibble_function_set)
{
    Lcd4 l;
    l.nibble(0x2, 0); // still 8-bit: one E pulse is a whole "function set: 4-bit" (D3-D0 read 0)
    CHECK(l.lcd.lcd().four_bit());
    l.send(0x28, 0);  // now in nibbles: 4-bit, 2 lines
    l.send(0x0E, 0);
    l.send(0x06, 0);
    l.send(0x01, 0);
    for (const char c : std::string("4 bits"))
        l.send(static_cast<uint8_t>(c), Lcd4::RS);
    CHECK_EQ(l.lcd.lcd().line(0), std::string("4 bits          "));
    CHECK_EQ(l.read(0), 6); // busy clear in the high nibble, AC in both
    l.send(0xA8, 0);       // DDRAM 0x28: Eater's "enter" goes to position 40
    l.send('x', Lcd4::RS);
    CHECK_EQ(l.lcd.lcd().line(1), std::string("x               "));
}

TEST(ddram_wraps_between_lines_and_display_shifts)
{
    Lcd8 l;
    l.send(0x38, 0);
    l.send(0x0C, 0);
    l.send(0x01, 0);
    l.send(0x80 | 0x27, 0); // the last cell of line 1
    l.text("ab");           // b wraps to the start of line 2
    CHECK_EQ(l.lcd.lcd().line(1).substr(0, 1), std::string("b"));
    l.send(0x18, 0); // shift display left: the window moves right
    l.send(0x1C, 0); // and back
    l.send(0x1C, 0); // right once: column 0 now shows DDRAM 0x27
    CHECK_EQ(l.lcd.lcd().line(0).substr(0, 1), std::string("a"));
    l.send(0x08, 0); // display off
    CHECK_EQ(l.lcd.lcd().line(1), std::string(16, ' '));
}

TEST(ps2_bytes_one_at_a_time_on_ca1)
{
    Via6522 via;
    via.write(PCR, 0x01); // rising edge, as keyboard.s sets it
    via.write(IER, 0x82);
    Ps2Keyboard kb(via, {P::A, 0, 8});
    CHECK(kb.key('a'));
    CHECK(kb.key('A'));
    CHECK(!kb.key(0x01)); // no key for it
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 20 && via.irq(); ++i)
    {
        bytes.push_back(via.read(ORA)); // what the IRQ handler does
        kb.service();
    }
    CHECK(bytes == (std::vector<uint8_t>{0x1C, 0xF0, 0x1C, 0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12}));
    CHECK(kb.idle());
}

TEST(ps2_waits_for_the_byte_to_be_read)
{
    Via6522 via;
    Ps2Keyboard kb(via, {P::A, 0, 8});
    kb.key('b');
    kb.service();
    kb.service();
    CHECK_EQ(via.peek(ORA), 0x32); // still the first byte
    via.read(ORA);
    kb.service();
    CHECK_EQ(via.peek(ORA), 0xF0);
}

int main()
{
    return run_all();
}
