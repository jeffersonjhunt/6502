/**
 * @file test_usb.cpp
 * @brief The USB card (hw/design.md, "USB card registers"): keyboard, mouse, game port, speaker.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card alone, its "CPU clock" at 1 MHz so a millisecond is 1,000 ticks,
 * then on a backplane machine: its interrupt in IRQH, Pause/Break's /NMI
 * through the Session, and its event FIFO drained by DMA.
 */

#include "harness.h"

#include "cpu6502.h"
#include "dma_controller.h"
#include "machine.h"
#include "usb_card.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t MS = 1000; // ticks a millisecond at 1 MHz

    // HID usages
    constexpr uint8_t A = 0x04, C = 0x06, Z = 0x1D, ONE = 0x1E, TWO = 0x1F, ENTER = 0x28, BACKSPACE = 0x2A,
                      SPACE = 0x2C, MINUS = 0x2D, LBRACKET = 0x2F, SLASH = 0x38, CAPS = 0x39, F1 = 0x3A,
                      PAUSE = 0x48, DEL = 0x4C, RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52, KP1 = 0x59,
                      KP0 = 0x62, LCTRL = 0xE0, LSHIFT = 0xE1, RSHIFT = 0xE5;

    struct Card : UsbCard
    {
        Card() { set_cpu_hz(1'000'000); }
        /// Press and release: the character it made, strobe and all.
        uint8_t type(uint8_t usage)
        {
            key(usage, true);
            key(usage, false);
            return peek(KEY);
        }
    };
} // namespace

TEST(registers_id_devices_and_reset)
{
    Card c;
    CHECK_EQ(c.peek(UsbCard::ID_REG), 0xB1);
    CHECK_EQ(c.peek(UsbCard::DEVICES), 0x03);    // keyboard and mouse
    CHECK_EQ(c.peek(UsbCard::REPEAT), 0x10);
    CHECK_EQ(c.peek(UsbCard::MAXX), 639 & 0xFF);
    CHECK_EQ(c.peek(UsbCard::MAXX + 1), 639 >> 8);
    c.write(UsbCard::REPEAT, 0xFF);
    CHECK_EQ(c.peek(UsbCard::REPEAT), 0x3F);
    c.write(UsbCard::LEDS, 0xFF);
    CHECK_EQ(c.leds(), 0x07);
    c.write(UsbCard::KBCFG, 0xFF);
    CHECK_EQ(c.peek(UsbCard::KBCFG), 0x8F);
    c.write(UsbCard::IEN, 0xFF);
    CHECK_EQ(c.peek(UsbCard::IEN), 0x0F);
    CHECK_EQ(c.peek(0x40), 0xFF);
    c.reset();
    CHECK_EQ(c.peek(UsbCard::KBCFG), 0x00);
    CHECK_EQ(c.peek(UsbCard::REPEAT), 0x10);
}

TEST(cooked_keys_in_the_us_layout_with_the_apples_conventions)
{
    Card c;
    CHECK_EQ(c.type(A), 0x80 | 'A');             // Caps Lock is on at power-up
    c.key(CAPS, true);
    c.key(CAPS, false);
    CHECK(!c.caps_lock());
    CHECK_EQ(c.type(A), 0x80 | 'a');
    CHECK_EQ(c.type(Z), 0x80 | 'z');
    c.key(LSHIFT, true);
    CHECK_EQ(c.type(A), 0x80 | 'A');
    CHECK_EQ(c.type(ONE), 0x80 | '!');
    CHECK_EQ(c.type(TWO), 0x80 | '@');
    CHECK_EQ(c.type(SLASH), 0x80 | '?');
    c.key(LSHIFT, false);
    CHECK_EQ(c.type(ONE), 0x80 | '1');            // Caps Lock leaves digits alone
    c.key(CAPS, true);
    c.key(CAPS, false);
    CHECK_EQ(c.type(ONE), 0x80 | '1');
    c.key(RSHIFT, true);
    CHECK_EQ(c.type(A), 0x80 | 'a');             // Shift undoes Caps Lock on letters
    c.key(RSHIFT, false);
    c.key(LCTRL, true);
    CHECK_EQ(c.type(C), 0x80 | 0x03);            // Ctrl-C
    CHECK_EQ(c.type(LBRACKET), 0x80 | 0x1B);     // Ctrl-[ is ESC
    c.key(LCTRL, false);
    CHECK_EQ(c.type(ENTER), 0x80 | 0x0D);
    CHECK_EQ(c.type(BACKSPACE), 0x80 | 0x08);
    CHECK_EQ(c.type(SPACE), 0x80 | ' ');
    CHECK_EQ(c.type(MINUS), 0x80 | '-');
    CHECK_EQ(c.type(DEL), 0x80 | 0x7F);
    CHECK_EQ(c.type(LEFT), 0x80 | 0x08);         // the Apple's arrows
    CHECK_EQ(c.type(RIGHT), 0x80 | 0x15);
    CHECK_EQ(c.type(DOWN), 0x80 | 0x0A);
    CHECK_EQ(c.type(UP), 0x80 | 0x0B);
    CHECK_EQ(c.type(KP1), 0x80 | '1');
    CHECK_EQ(c.type(KP0), 0x80 | '0');
    c.read(UsbCard::KEYCLR);
    CHECK_EQ(c.type(F1), '0');                   // no character: KEY keeps the last (keypad 0), strobe clear
}

TEST(the_strobe_one_character_and_keyclr)
{
    Card c;
    c.key(A, true);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');
    CHECK_EQ(c.peek(UsbCard::KEYCLR), 0x80 | 'A'); // bit 7 here: a key is held
    c.read(UsbCard::KEYCLR);                       // any access clears the strobe
    CHECK_EQ(c.peek(UsbCard::KEY), 'A');
    c.key(A, false);
    CHECK_EQ(c.peek(UsbCard::KEYCLR), 'A');        // none held
    c.type(Z);
    c.type(C);                                     // a second before the first was read: it replaces it
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'C');
    c.write(UsbCard::KEYCLR, 0);                   // a write clears it too
    CHECK_EQ(c.peek(UsbCard::KEY), 'C');
    c.key(LSHIFT, true);
    CHECK_EQ(c.peek(UsbCard::MODS), 0x02);         // HID bit order
    c.key(RSHIFT, true);
    CHECK_EQ(c.peek(UsbCard::MODS), 0x22);
    CHECK_EQ(c.peek(UsbCard::KEYCLR) & 0x80, 0);   // modifiers are not "a key held"
}

TEST(the_last_key_pressed_repeats_after_the_delay_at_the_rate)
{
    Card c;                                       // REPEAT $10: 500 ms, then every 33 ms
    c.key(A, true);
    c.read(UsbCard::KEYCLR);
    c.tick(500 * MS - 1);
    CHECK_EQ(c.peek(UsbCard::KEY) & 0x80, 0);
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');   // the first repeat
    c.read(UsbCard::KEYCLR);
    c.tick(33 * MS - 1);
    CHECK_EQ(c.peek(UsbCard::KEY) & 0x80, 0);
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');
    c.key(Z, true);                               // a second key takes over
    c.read(UsbCard::KEYCLR);
    c.tick(500 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'Z');
    c.key(Z, false);
    c.read(UsbCard::KEYCLR);
    c.tick(2000 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY) & 0x80, 0);     // released: no more, though A is still down
    c.key(A, false);
    c.write(UsbCard::REPEAT, 0x3F);               // the slowest: 1 s, then every 528 ms
    c.key(ONE, true);
    c.read(UsbCard::KEYCLR);
    c.tick(1000 * MS - 1);
    CHECK_EQ(c.peek(UsbCard::KEY) & 0x80, 0);
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | '1');
    c.read(UsbCard::KEYCLR);
    c.tick(528 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | '1');
}

TEST(host_keys_arrive_at_the_usb_polling_pace)
{
    Card c;
    c.queue_key(A, true);                         // the line is free: at once
    c.queue_key(A, false);
    c.queue_key(Z, true);                         // a burst, as a page can send in one frame
    c.queue_key(Z, false);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 2);
    c.tick(8 * MS - 1);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 2);        // the next report: 8 ms on
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 4);        // A up
    c.read(UsbCard::KEYCLR);
    c.tick(8 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'Z');   // Z, not lost behind A
    c.tick(8 * MS);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 8);
    c.tick(100 * MS);
    c.queue_key(C, true);                         // quiet a while: at once again
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'C');
}

TEST(a_queued_character_waits_for_the_last_to_be_taken)
{
    Card c;
    for (const uint8_t u : {A, Z, C})            // a paste: three characters at once
    {
        c.queue_key(u, true);
        c.queue_key(u, false);
    }
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');
    c.tick(8 * MS);                               // A up
    c.tick(8 * MS);
    c.tick(8 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');   // Z waits: nobody has taken A
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 4);        // A down and up; everything behind Z waits too
    c.read(UsbCard::KEYCLR);
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'Z');   // taken: the next at once
    c.tick(8 * MS);                               // Z up
    c.tick(250 * MS - 8 * MS - 2);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'Z');   // software that never takes it...
    c.tick(2);
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'C');   // ...gets the next after 250 ms anyway

    c.tick(8 * MS);                               // C up
    c.tick(100 * MS);
    c.queue_key(A, true);                         // a lone key, KEY still full: at once, as a person types
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');
    c.queue_key(A, false);
    c.queue_key(LCTRL, true);
    c.queue_key(DEL, true);                       // Ctrl-Reset in a burst: not held back
    c.tick(8 * MS);
    c.tick(8 * MS);
    c.tick(8 * MS);
    CHECK(c.take_reset());
}

TEST(a_paste_stopped_drops_what_is_queued_and_lets_go_of_the_keys)
{
    Card c;
    for (const uint8_t u : {A, Z, C})            // a paste: three characters at once
    {
        c.queue_key(u, true);
        c.queue_key(u, false);
    }
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'A');   // A down at once; A up and the rest queued
    CHECK_EQ(c.queued(), 5u);
    c.drop_queue();
    CHECK_EQ(c.queued(), 0u);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 4);        // A down, and up: nothing is left held
    c.read(UsbCard::KEYCLR);
    c.tick(1000 * MS);
    CHECK_EQ(c.peek(UsbCard::KEY), 'A');          // no Z, no C, no repeat
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 4);
    c.queue_key(C, true);                         // the keyboard as before
    CHECK_EQ(c.peek(UsbCard::KEY), 0x80 | 'C');
}

TEST(raw_events_every_press_and_release_in_a_fifo_with_drq)
{
    Card c;
    CHECK(!c.drq());
    c.key(LSHIFT, true);
    c.key(A, true);
    c.key(A, false);
    c.key(A, false);                              // no change: no event
    c.key(LSHIFT, false);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 8);
    CHECK(c.drq());
    const std::vector<uint8_t> want = {0xE1, 0x00, A, 0x00, A, 0x80, 0xE1, 0x80};
    std::vector<uint8_t> got;
    got.push_back(c.read(UsbCard::EVENT));
    got.push_back(c.read(UsbCard::EVENT));
    got.push_back(c.dack_read(false));            // DMA takes them the same way
    for (int i = 0; i < 5; ++i)
        got.push_back(c.read(UsbCard::EVENT));
    CHECK(got == want);
    CHECK(!c.drq());
    CHECK_EQ(c.read(UsbCard::EVENT), 0);
    for (unsigned u = 0x04; u < 0x04 + 70; ++u)   // 70 presses: 140 bytes into 128
        c.key(static_cast<uint8_t>(u), true);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 128);
    c.read(UsbCard::EVENT);                       // one byte taken: 127, room for half an event...
    c.key(0x50, true);
    CHECK_EQ(c.peek(UsbCard::EVCOUNT), 127);      // ...which is no room: dropped whole
    for (int i = 0; i < 125; ++i)
        c.read(UsbCard::EVENT);
    CHECK_EQ(c.read(UsbCard::EVENT), 0x04 + 63);  // the 64th, whole: the rest were dropped
    CHECK_EQ(c.read(UsbCard::EVENT), 0x00);
}

TEST(the_mouse_relative_sums_latched_together)
{
    Card c;
    c.mouse_move(5, -3);
    c.mouse_move(10, -1);
    c.mouse_wheel(-2);
    c.mouse_buttons(0x05);
    CHECK_EQ(c.peek(UsbCard::MBTN), 0x05);
    CHECK_EQ(c.read(UsbCard::MX), 15);            // reading MX latches all three
    c.mouse_move(100, 100);                       // after the latch: the next sums
    CHECK_EQ(c.peek(UsbCard::MX + 1), 0);
    CHECK_EQ(c.peek(UsbCard::MY), static_cast<uint8_t>(-4));
    CHECK_EQ(c.peek(UsbCard::MY + 1), 0xFF);
    CHECK_EQ(c.peek(UsbCard::MWHEEL), static_cast<uint8_t>(-2));
    CHECK_EQ(c.read(UsbCard::MX), 100);
    CHECK_EQ(c.peek(UsbCard::MWHEEL), 0);
    c.read(UsbCard::MX);
    CHECK_EQ(c.peek(UsbCard::MX), 0);             // nothing moved since
    c.mouse_wheel(500);
    c.read(UsbCard::MX);
    CHECK_EQ(c.peek(UsbCard::MWHEEL), 127);       // saturates
}

TEST(the_mouse_absolute_position_clamped_to_the_limits)
{
    Card c;
    c.write(UsbCard::MCTRL, UsbCard::ABSOLUTE);
    c.write(UsbCard::MAXX, 319 & 0xFF);
    c.write(UsbCard::MAXX + 1, 319 >> 8);
    c.write(UsbCard::MAXY, 239);
    c.write(UsbCard::MAXY + 1, 0);
    c.mouse_to(200, 100);
    c.read(UsbCard::MX);
    CHECK_EQ(c.peek(UsbCard::MX), 200);
    CHECK_EQ(c.peek(UsbCard::MY), 100);
    c.mouse_move(500, -500);                      // past the corner: held at it
    c.read(UsbCard::MX);
    CHECK_EQ(c.peek(UsbCard::MX) | c.peek(UsbCard::MX + 1) << 8, 319);
    CHECK_EQ(c.peek(UsbCard::MY), 0);
    c.mouse_to(-5, 1000);
    c.read(UsbCard::MX);
    CHECK_EQ(c.peek(UsbCard::MX), 0);
    CHECK_EQ(c.peek(UsbCard::MY), 239);
}

TEST(the_558_paddles_time_11_us_a_count_buttons_and_the_speaker)
{
    Card c;
    c.set_paddle(0, 100);                         // 1.1 ms
    c.set_paddle(1, 0);
    c.set_paddle(3, 255);                         // 2.805 ms
    CHECK_EQ(c.peek(UsbCard::PADDLE0), 100);
    CHECK_EQ(c.peek(UsbCard::PADDLE0 + 3), 255);
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0);    // not triggered
    c.write(UsbCard::PTRIG, 0);
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0x09); // 0 and 3 timing; 1 and 2 (at 0) already done
    c.tick(1100 - 1);
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0x09);
    c.tick(1);
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0x08);
    c.tick(2805 - 1100);
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0x00);
    c.read(UsbCard::PTRIG);                       // a read triggers too, like $C070
    CHECK_EQ(c.peek(UsbCard::GAME) & 0x0F, 0x09);
    c.set_game_buttons(0x05);
    CHECK_EQ(c.peek(UsbCard::GAME) >> 4, 0x05);
    const uint64_t s = c.speaker_toggles();
    c.read(UsbCard::SPKR);
    c.write(UsbCard::SPKR, 0);
    CHECK_EQ(c.speaker_toggles(), s + 2);
}

TEST(interrupts_pend_whatever_ien_says)
{
    Card c;
    c.type(A);
    CHECK_EQ(c.peek(UsbCard::ISR), UsbCard::IRQ_KEY);
    CHECK(!c.irq());
    c.write(UsbCard::IEN, UsbCard::IRQ_KEY | UsbCard::IRQ_MOUSE | UsbCard::IRQ_GAME);
    CHECK(c.irq());
    c.write(UsbCard::ISR, UsbCard::IRQ_KEY);
    CHECK(!c.irq());
    c.mouse_move(1, 0);
    CHECK_EQ(c.peek(UsbCard::ISR), UsbCard::IRQ_MOUSE);
    c.write(UsbCard::ISR, 0xFF);
    c.mouse_buttons(0);                           // no change: nothing
    c.mouse_move(0, 0);
    CHECK_EQ(c.peek(UsbCard::ISR), 0);
    c.set_game_buttons(1);
    CHECK_EQ(c.peek(UsbCard::ISR), UsbCard::IRQ_GAME);
}

TEST(ctrl_delete_is_ctrl_reset_and_gui_or_alt_are_the_apple_keys)
{
    Card c;
    CHECK_EQ(c.type(DEL), 0x80 | 0x7F);           // Delete alone: DEL
    CHECK(!c.take_reset());
    c.read(UsbCard::KEYCLR);
    c.key(LCTRL, true);
    c.type(DEL);
    CHECK(c.take_reset());                        // Ctrl-Delete: /RESET, once
    CHECK(!c.take_reset());
    CHECK_EQ(c.peek(UsbCard::KEY) & 0x80, 0);     // and no character
    c.key(LCTRL, false);
    CHECK_EQ(c.apple_keys(), 0);
    c.key(0xE2, true);                            // left Alt: Open Apple
    CHECK_EQ(c.apple_keys(), 1);
    c.key(0xE7, true);                            // right GUI: Closed Apple
    CHECK_EQ(c.apple_keys(), 3);
    c.key(0xE2, false);
    c.key(0xE3, true);                            // left GUI: Open Apple too
    CHECK_EQ(c.apple_keys(), 3);
    c.key(0xE7, false);
    c.key(0xE3, false);
    c.key(0xE6, true);                            // right Alt: Closed Apple
    CHECK_EQ(c.apple_keys(), 2);
}

TEST(pause_break_pulls_nmi_only_with_kbcfg_bit_7)
{
    Card c;
    c.type(PAUSE);
    CHECK(!c.take_nmi());
    c.write(UsbCard::KBCFG, UsbCard::NMI_ON_PAUSE);
    c.type(PAUSE);
    CHECK(c.take_nmi());
    CHECK(!c.take_nmi());                         // once
}

namespace
{
    constexpr uint32_t APPLE_HZ = 1'020'484, RATE = 48'000;

    /// A card with the speaker's sound on, at the Apple's clock.
    UsbCard speaker_card()
    {
        UsbCard u;
        u.set_cpu_hz(APPLE_HZ);
        u.set_audio_rate(RATE);
        return u;
    }

    /// Toggle the speaker every `half` cycles, `toggles` times; then collect everything.
    std::vector<float> square(UsbCard &u, unsigned half, unsigned toggles, uint64_t after = 0)
    {
        std::vector<float> out, block(RATE);
        auto collect = [&] {
            const size_t n = u.take_audio(block.data(), block.size());
            out.insert(out.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n));
        };
        for (unsigned i = 0; i < toggles; ++i)
        {
            u.tick(half);
            u.read(UsbCard::SPKR);
            if (i % 100 == 0) // often: half a second waits at most
                collect();
        }
        u.tick(after);
        collect();
        return out;
    }

    float rms(const std::vector<float> &v, size_t from, size_t to)
    {
        double sq = 0;
        for (size_t i = from; i < to; ++i)
            sq += static_cast<double>(v[i]) * v[i];
        return static_cast<float>(std::sqrt(sq / static_cast<double>(to - from)));
    }
} // namespace

TEST(the_speaker_a_toggle_every_510_cycles_is_1_khz_at_its_level)
{
    UsbCard u = speaker_card();
    const auto s = square(u, 510, 1000, APPLE_HZ / 4); // half a second, then a quarter of quiet
    const size_t tone = RATE / 2;
    const uint64_t want = (510'000 + APPLE_HZ / 4) * uint64_t{RATE} / APPLE_HZ; // whole samples of the cycles run
    CHECK(s.size() + 1 >= want && s.size() <= want);
    unsigned rises = 0;
    for (size_t i = RATE / 20; i < tone; ++i)
        rises += s[i - 1] < 0 && s[i] >= 0;
    const double hz = rises / (0.5 - 0.05);
    CHECK(hz > 995 && hz < 1006); // 1,020,484 / 1,020: 1,000.5 Hz
    float peak = 0;
    for (size_t i = RATE / 20; i < tone; ++i)
        peak = std::fmax(peak, std::fabs(s[i]));
    CHECK(peak > 0.19f && peak < 0.215f); // +-0.2, with the high-pass's droop
    CHECK(rms(s, s.size() - 1000, s.size()) < 0.002f); // left alone, it dies away
    CHECK_EQ(u.speaker_toggles(), 1000u);
}

TEST(the_speaker_one_toggle_is_one_click_and_two_on_a_cycle_are_none)
{
    UsbCard one = speaker_card();
    auto s = square(one, 2'000, 1, APPLE_HZ / 10);
    size_t at = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (std::fabs(s[i]) > std::fabs(s[at]))
            at = i;
    CHECK(std::fabs(s[at]) > 0.35f); // the step: 0.4, high-passed
    CHECK(std::fabs(s.back()) < 0.01f); // gone again in 100 ms (20 Hz)
    UsbCard two = speaker_card();
    two.tick(2'000);
    two.read(UsbCard::SPKR);
    two.write(UsbCard::SPKR, 0); // the same cycle: back where it was
    two.tick(APPLE_HZ / 10);
    std::vector<float> out(RATE);
    out.resize(two.take_audio(out.data(), out.size()));
    CHECK(!out.empty());
    CHECK_EQ(rms(out, 0, out.size()), 0.0f);
}

TEST(the_speaker_averages_each_sample_so_a_tone_past_hearing_is_quiet)
{
    // 46 kHz, toggled every 11 cycles: averaged over each 21-cycle sample it nearly cancels; sampled at a point it would alias loud.
    UsbCard u = speaker_card();
    const auto s = square(u, 11, 40'000);
    CHECK(rms(s, s.size() / 4, s.size()) < 0.06f);
}

TEST(the_speaker_makes_a_second_of_samples_a_second_at_any_clock_and_none_when_off)
{
    for (const uint32_t hz : {APPLE_HZ, UsbCard::NATIVE_HZ})
    {
        UsbCard u;
        u.set_cpu_hz(hz);
        u.set_audio_rate(RATE);
        std::vector<float> block(RATE);
        size_t n = 0;
        for (int i = 0; i < 4; ++i) // in quarters: half a second waits at most
        {
            u.tick(hz / 4);
            n += u.take_audio(block.data(), block.size());
        }
        CHECK(n >= RATE - 1 && n <= RATE);
    }
    UsbCard off;
    off.read(UsbCard::SPKR);
    off.tick(APPLE_HZ);
    std::vector<float> block(RATE);
    CHECK_EQ(off.take_audio(block.data(), block.size()), 0u);
    CHECK_EQ(off.speaker_toggles(), 1u);
}

TEST(the_speaker_keeps_half_a_second_and_its_place_while_nobody_listens)
{
    UsbCard u = speaker_card();
    u.tick(2 * APPLE_HZ); // nobody collecting: the oldest go
    std::vector<float> block(2 * RATE);
    CHECK_EQ(u.take_audio(block.data(), block.size()), size_t{RATE / 2});
    // Toggled with the sound off, the flip-flop still moves: high now, so the next toggle is a step down.
    UsbCard v;
    v.set_cpu_hz(APPLE_HZ);
    v.read(UsbCard::SPKR);
    v.set_audio_rate(RATE);
    v.tick(1'000);
    v.read(UsbCard::SPKR);
    v.tick(APPLE_HZ / 100);
    const size_t n = v.take_audio(block.data(), block.size());
    REQUIRE(n > 100);
    CHECK_EQ(block[0], 0.0f); // turned on at rest: no click
    CHECK(*std::min_element(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n)) < -0.35f); // a step of -0.4
    CHECK(*std::max_element(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n)) <= 0.0f);
}

TEST(the_speakers_time_runs_on_through_a_reset)
{
    UsbCard u = speaker_card();
    u.tick(APPLE_HZ / 10);
    u.reset(); // Ctrl-Reset: the card's own clock starts again, the speaker's doesn't
    u.tick(APPLE_HZ / 10);
    std::vector<float> block(RATE);
    const size_t n = u.take_audio(block.data(), block.size());
    CHECK(n >= RATE / 5 - 1 && n <= RATE / 5);
}

namespace
{
    std::unique_ptr<Machine> backplane(std::string &error, const std::string &more = "")
    {
        return Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                              "usb 580-5FF space=io name=usb irq" + more + "\n",
                              "usb.machine", {}, error);
    }
} // namespace

TEST(on_the_backplane_a_key_interrupt_is_irqh_bit_3)
{
    std::string error;
    auto m = backplane(error);
    REQUIRE(m != nullptr);
    UsbCard *usb = m->usb_card();
    REQUIRE(usb != nullptr);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC5FF), 0xB1);
    bus.write(0xC5B0, UsbCard::IRQ_KEY);          // IEN
    usb->key(0x04, true);
    CHECK(bus.irq());
    CHECK_EQ(bus.peek(0xC689), 0xF7);             // IRQH: bit 3 low
    CHECK_EQ(bus.read(0xC580), 0x80 | 'A');
}

TEST(dma_drains_the_event_fifo_on_its_drq)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                            "dma 400-47F space=io name=dma irq\nusb 580-5FF space=io name=usb irq drq=2\n",
                            "usb.machine", {}, error);
    REQUIRE(m != nullptr);
    UsbCard *usb = m->usb_card();
    Decoder &bus = m->bus();
    usb->key(0x04, true);
    usb->key(0x04, false);
    // channel 2: card to memory at $01000, 4 bytes, demand mode
    bus.poke(0xC420, 0x00);
    bus.poke(0xC421, 0x10);
    bus.poke(0xC422, 0x00);
    bus.poke(0xC424, 4);
    bus.poke(0xC425, 0);
    bus.poke(0xC426, DmaController::DEMAND);
    bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
    bus.poke(0xC427, DmaController::ENABLE);
    for (int i = 0; i < 8; ++i)
        bus.read(0x0000);                         // CPU cycles: the controller steals what it needs
    CHECK_EQ(bus.peek_physical(0x01000), 0x04);
    CHECK_EQ(bus.peek_physical(0x01001), 0x00);
    CHECK_EQ(bus.peek_physical(0x01002), 0x04);
    CHECK_EQ(bus.peek_physical(0x01003), 0x80);
    CHECK(!usb->drq());
}

int main()
{
    return run_all();
}
