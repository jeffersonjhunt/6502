/**
 * @file test_oodos_input.cpp
 * @brief OOLIB's L2 (hw/oolib.md, "The plan"): the event area's layout 2, the USB card's events, the
 * DE-9 sticks, the game port, the mouse and function 81, on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_clock boots it (oodos_rig.h), and L2TEST (oodos/sys/test/l2test.s)
 * run by GO: it opens its area at $2000 (block 0, W0's) and then takes commands, a letter the test
 * puts at $2100. The input comes from the machine's host side (the USB card's keys, mouse and game
 * port; the joystick ports on the VIA), and the area is read behind OODOS's back.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t AREA = 0x2000, MBOX = 0x2100; ///< l2test.s's, in block 0
    // oodos.inc's
    constexpr unsigned EV_LAYOUT = 0, EV_SIZE = 2, EV_MS = 4, EV_QHEAD = 16, EV_QTAIL = 17, EV_QSLOTS = 18,
                       EV_QLOST = 20, EV_MODS = 24, EV_DEVICES = 25, EV_MX = 32, EV_MY = 34, EV_MBUTTONS = 36,
                       EV_MWHEEL = 37, EV_CTL = 40, EV_PADDLES = 56, EV_KEYS = 64, EV_QUEUE = 128;
    constexpr unsigned EK_KEY = 1, EK_BUTTON = 2, EK_WHEEL = 3, EK_CTL = 4;
    constexpr uint8_t CB_UP = 1, CB_DOWN = 2, CB_LEFT = 4, CB_RIGHT = 8, CB_FIRE = 0x10;
    constexpr uint16_t USB = 0xC580, USB_LEDS = USB + 5, USB_REPEAT = USB + 6, USB_MCTRL = USB + 0x10,
                       USB_MAXX = USB + 0x17, USB_MAXY = USB + 0x19, USB_IEN = USB + 0x30;
    constexpr uint8_t A_KEY = 0x04, B_KEY = 0x05, C_KEY = 0x06, ENTER = 0x28, L_SHIFT = 0xE1;

    uint8_t area(Rig &r, unsigned off) { return r.phys(AREA + off); }
    unsigned area16(Rig &r, unsigned off) { return area(r, off) | area(r, off + 1) << 8; }
    uint32_t area32(Rig &r, unsigned off) { return area16(r, off) | static_cast<uint32_t>(area16(r, off + 2)) << 16; }
    uint32_t ms_count(Rig &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("MS");
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    uint8_t io(Rig &r, uint16_t a) { return r.machine->bus().peek(a); }
    unsigned io16(Rig &r, uint16_t a) { return io(r, a) | io(r, static_cast<uint16_t>(a + 1)) << 8; }

    struct Event
    {
        unsigned kind, code, value, mods;
        uint32_t ms;
    };
    /// The queue's event in slot n.
    Event slot(Rig &r, unsigned n)
    {
        const unsigned o = EV_QUEUE + n * 8;
        return {area(r, o), area(r, o + 1), area(r, o + 2), area(r, o + 3), area32(r, o + 4)};
    }
    /// The events waiting, from the program's tail to OODOS's head, oldest first.
    std::vector<Event> waiting(Rig &r)
    {
        std::vector<Event> out;
        const unsigned slots = area(r, EV_QSLOTS);
        for (unsigned t = area(r, EV_QTAIL); slots && t != area(r, EV_QHEAD); t = (t + 1) % slots)
            out.push_back(slot(r, t));
        return out;
    }
    void show(const std::vector<Event> &ev)
    {
        for (const Event &e : ev)
            std::printf("    kind %u code %02X value %02X mods %02X ms %u\n", e.kind, e.code, e.value, e.mods, e.ms);
    }
    bool held(Rig &r, uint8_t usage) { return (area(r, EV_KEYS + usage / 8) >> (usage % 8) & 1) != 0; }

    /// L2TEST with `tail`, run to its OPEN line (or, for X, a little).
    size_t open(CcpRig &r, const std::string &tail)
    {
        r.poke(MBOX, 0);
        const size_t from = start(r, "l2test.com", " " + tail);
        if (tail != "X" && tail != "C")
            CHECK(r.until("OPEN ", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("OPEN ", from)));
        else
            r.run(ms(200));
        return from;
    }
    /// L2TEST's command `c`: its line, `label` and what follows, to the line's end.
    std::string command(CcpRig &r, char c, const std::string &label)
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0; // the last line's \n on
        r.poke(MBOX, static_cast<uint8_t>(c));
        if (!r.until("\n" + label + " ", ms(2000), from))
        {
            std::printf("  no %s line after command %c\n", label.c_str(), c);
            r.tail();
            return {};
        }
        const size_t at = r.out.find("\n" + label + " ", from);
        r.until("\r", ms(500), at + 1);
        return field(r.out.substr(from), label);
    }
    /// L2TEST's warm boot (command W), to the CCP's prompt.
    void wboot(CcpRig &r)
    {
        const size_t from = r.out.size();
        r.poke(MBOX, 'W');
        CHECK(r.until("\r\nA>", ms(2000), from));
    }
    /// A key down and up through the USB card, `gap_ms` apart, then `after_ms` more.
    void tap(Rig &r, uint8_t usage, unsigned gap_ms = 20, unsigned after_ms = 20)
    {
        r.machine->usb_card()->key(usage, true);
        r.run(ms(gap_ms));
        r.machine->usb_card()->key(usage, false);
        r.run(ms(after_ms));
    }

    /// The dispatcher's cost for each interrupt over a stretch, from the interrupt's first cycle (7
    /// before IRQ) to its RTI's last (IRQEND's RTI, 6), by what interrupted: a tick alone (IRQL the
    /// VIA's bit alone, IRQH none, and no vertical blank counted meanwhile: one that came in the
    /// dispatcher's first cycles is answered in the same pass), the USB card (IRQH bit 3), or anything
    /// else.
    struct Costs
    {
        std::map<unsigned, unsigned> tick, usb, other;
        static void add(std::map<unsigned, unsigned> &m, unsigned c) { ++m[c]; }
        static unsigned mode(const std::map<unsigned, unsigned> &m)
        {
            unsigned best = 0, n = 0;
            for (const auto &[c, k] : m)
                if (k > n)
                    best = c, n = k;
            return best;
        }
        static void print(const char *what, const std::map<unsigned, unsigned> &m)
        {
            std::printf("  %s:", what);
            for (const auto &[c, k] : m)
                std::printf(" %u cycles x%u", c, k);
            std::printf("\n");
        }
    };
    Costs time_interrupts(Rig &r, uint64_t cycles, const std::function<void()> &during = {})
    {
        Costs out;
        const uint16_t irq = sym("IRQ"), end = sym("IRQEND");
        const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
        uint64_t c0 = 0;
        uint8_t l = 0, h = 0, f = 0;
        bool in = false;
        r.watch = [&](uint16_t pc) {
            if (pc == irq)
            {
                c0 = r.cpu->cycles() - 7;
                l = r.nb(8);
                h = r.nb(9);
                f = r.phys(frames);
                in = true;
            }
            else if (pc == end && in)
            {
                const unsigned c = static_cast<unsigned>(r.cpu->cycles() + 6 - c0);
                if (l == 0xBF && h == 0xFF && r.phys(frames) == f)
                    Costs::add(out.tick, c);
                else if (!(h & 0x08))
                    Costs::add(out.usb, c);
                else
                    Costs::add(out.other, c);
                in = false;
            }
        };
        if (during)
            during();
        r.run(cycles);
        r.watch = nullptr;
        return out;
    }
} // namespace

TEST(a_layout_2_area_takes_typed_keys_into_its_queue)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(field(r.out, "OPEN"), std::string("00 00"));
    CHECK_EQ(area(r, EV_LAYOUT), 2);
    CHECK_EQ(area(r, EV_SIZE), 0);    // 256
    CHECK_EQ(area(r, EV_QSLOTS), 16); // (256 - 128) / 8
    CHECK_EQ(area(r, EV_QHEAD), 0);
    CHECK_EQ(area(r, EV_QTAIL), 0);
    CHECK_EQ(area(r, EV_DEVICES), 0x03); // keyboard and mouse
    for (unsigned c = 0; c < 4; ++c)
        CHECK_EQ(area(r, EV_CTL + c * 4 + 3), c + 1); // controller n is hardware n
    CHECK_EQ(io(r, USB_IEN), 0x0F);
    // Two keys, then one held: each press and release queued, with the ms counter then; the map has the
    // held one only while it's held.
    const uint32_t t0 = ms_count(r);
    tap(r, A_KEY);
    tap(r, B_KEY);
    r.machine->usb_card()->key(C_KEY, true);
    r.run(ms(20));
    CHECK(held(r, C_KEY));
    CHECK(!held(r, A_KEY) && !held(r, B_KEY));
    r.machine->usb_card()->key(C_KEY, false);
    r.run(ms(20));
    CHECK(!held(r, C_KEY));
    std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 6);
    const unsigned codes[] = {A_KEY, A_KEY, B_KEY, B_KEY, C_KEY, C_KEY};
    for (unsigned i = 0; i < 6; ++i)
    {
        CHECK_EQ(ev[i].kind, EK_KEY);
        CHECK_EQ(ev[i].code, codes[i]);
        CHECK_EQ(ev[i].value, i % 2 ? 0u : 1u);
        CHECK_EQ(ev[i].mods, 0u);
        CHECK(ev[i].ms >= t0 && ev[i].ms <= ms_count(r));
        if (i)
            CHECK(ev[i].ms - ev[i - 1].ms >= 19 && ev[i].ms - ev[i - 1].ms <= 21); // 20 ms apart, to the tick
    }
    CHECK_EQ(area(r, EV_QHEAD), 6);
    // The program takes the first by function 81: it, and the tail moved on.
    CHECK_EQ(command(r, 'N', "NEXT").substr(0, 14), std::string("01 00 01040100")); // A pressed
    const std::string n = command(r, 'N', "NEXT");
    CHECK_EQ(n.substr(0, 14), std::string("01 00 01040000")); // A's release
    CHECK_EQ(area(r, EV_QTAIL), 2);
    // A press and release inside one frame (3 ms): both, 3 ms apart.
    tap(r, B_KEY, 3, 20);
    ev = waiting(r);
    REQUIRE(ev.size() == 6);
    CHECK_EQ(ev[4].code, B_KEY);
    CHECK_EQ(ev[4].value, 1u);
    CHECK_EQ(ev[5].value, 0u);
    CHECK(ev[5].ms - ev[4].ms >= 2 && ev[5].ms - ev[4].ms <= 4);
    // The modifiers: left Shift held, a key, Shift let go. The area's and each event's.
    r.machine->usb_card()->key(L_SHIFT, true);
    r.run(ms(20));
    CHECK_EQ(area(r, EV_MODS), 0x02);
    tap(r, A_KEY);
    r.machine->usb_card()->key(L_SHIFT, false);
    r.run(ms(20));
    CHECK_EQ(area(r, EV_MODS), 0x00);
    ev = waiting(r);
    REQUIRE(ev.size() == 10);
    CHECK(ev[6].code == L_SHIFT && ev[6].value == 1 && ev[6].mods == 0x02);
    CHECK(ev[7].code == A_KEY && ev[7].mods == 0x02);
    CHECK(ev[8].code == A_KEY && ev[8].mods == 0x02);
    CHECK(ev[9].code == L_SHIFT && ev[9].value == 0 && ev[9].mods == 0x00);
    // Flushed: none waits, and function 81 says so.
    CHECK_EQ(command(r, 'F', "FLUSH"), std::string("00 00"));
    CHECK(waiting(r).empty());
    CHECK_EQ(command(r, 'N', "NEXT"), std::string("00 00 EEEEEEEEEEEEEEEE"));
    wboot(r);
}

TEST(only_e0_to_e7_are_modifiers)
{
    // A usage past the modifiers ($E8-$FF) is a key like any other: queued, held, and not in MODS.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    r.machine->usb_card()->key(0xE8, true);
    r.run(ms(20));
    CHECK(held(r, 0xE8));
    CHECK_EQ(area(r, EV_MODS), 0);
    r.machine->usb_card()->key(0xE8, false);
    r.run(ms(20));
    tap(r, 0xE7); // right GUI, the last modifier: bit 7 while held
    const std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 4);
    CHECK(ev[0].code == 0xE8 && ev[0].value == 1 && ev[0].mods == 0);
    CHECK(ev[1].code == 0xE8 && ev[1].value == 0 && ev[1].mods == 0);
    CHECK(ev[2].code == 0xE7 && ev[2].mods == 0x80);
    CHECK(ev[3].code == 0xE7 && ev[3].mods == 0);
    wboot(r);
}

/// L2TEST's IN_NEXT with an interrupt made pending at AREAOUT's PLP, where QTAKE gives the mask back.
static std::string next_raced(CcpRig &r, int dx)
{
    const uint16_t from = sym("AREAOUT");
    bool fired = false;
    r.watch = [&](uint16_t pc) {
        if (!fired && pc >= from && pc < from + 12 && r.machine->bus().peek(pc) == 0x28) // PLP
        {
            r.machine->usb_card()->mouse_move(dx, 0); // its handler writes UTMP
            fired = true;
        }
    };
    const std::string n = command(r, 'N', "NEXT");
    r.watch = nullptr;
    CHECK(fired);
    return n;
}

TEST(an_interrupt_as_in_next_unmasks_changes_nothing_it_returns)
{
    // QTAKE's result is read before its PLP: an interrupt taken just after it (the mouse's, whose
    // handler shares QTAKE's scratch byte) neither makes an empty queue's "none" an event nor a
    // waiting event "none".
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(next_raced(r, 5), std::string("00 00 EEEEEEEEEEEEEEEE")); // empty: none
    tap(r, A_KEY);
    CHECK_EQ(next_raced(r, 256).substr(0, 14), std::string("01 00 01040100")); // X's low byte 0: UTMP 0
    CHECK_EQ(area(r, EV_QTAIL), 1);
    wboot(r);
}

TEST(the_paddles_are_read_in_a_frame_whose_ms_counter_jumps_past_their_time)
{
    // The ms counter can move on more than one at a tick (the Disk II's catch-up): a read due at an
    // exact count would be missed for the frame. Made here by moving MS on 10 just after a PTRIG.
    CcpRig r;
    REQUIRE(r.ok);
    UsbCard &usb = *r.machine->usb_card();
    usb.set_paddle(0, 100);
    open(r, "2");
    r.run(ms(40));
    const uint32_t padfrm = SYS_BLOCK_PHYS + sym("PADFRM"), msp = SYS_BLOCK_PHYS + sym("MS"),
                   frames = SYS_BLOCK_PHYS + sym("FRAMES");
    bool done = false;
    for (int tries = 0; tries < 20 && !done; ++tries)
    {
        const uint8_t f0 = r.phys(frames);
        for (int i = 0; i < 1000 && r.phys(frames) == f0; ++i)
            r.run(100); // a frame begins
        for (int i = 0; i < 100 && r.phys(padfrm) != r.phys(frames); ++i)
            r.run(100); // and its first tick sends PTRIG
        if (r.phys(padfrm) != r.phys(frames) || r.phys(msp) >= 240)
            continue; // (no carry to make)
        usb.set_paddle(0, static_cast<uint8_t>(110 + tries));
        r.poke(msp, static_cast<uint8_t>(r.phys(msp) + 10));
        const uint8_t f = r.phys(frames);
        for (int i = 0; i < 1000 && r.phys(frames) == f; ++i)
            r.run(100);
        CHECK_EQ(area(r, EV_PADDLES), 110 + tries); // read in that frame, not the next
        done = true;
    }
    CHECK(done);
    wboot(r);
}

TEST(a_full_queue_counts_what_it_loses_and_keeps_what_it_has_whole)
{
    // 4 slots: 3 events wait at most. Five keys tapped (10 events) with the program reading none.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "4");
    CHECK_EQ(area(r, EV_QSLOTS), 4);
    for (uint8_t k = A_KEY; k < A_KEY + 5; ++k)
        tap(r, k);
    const std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].code == A_KEY && ev[0].value == 1);
    CHECK(ev[1].code == A_KEY && ev[1].value == 0);
    CHECK(ev[2].code == B_KEY && ev[2].value == 1);
    for (const Event &e : ev)
        CHECK(e.kind == EK_KEY && e.ms != 0);
    CHECK_EQ(area16(r, EV_QLOST), 7u);
    // Taken one: room for one more.
    CHECK_EQ(command(r, 'N', "NEXT").substr(0, 14), std::string("01 00 01040100"));
    tap(r, A_KEY);
    CHECK_EQ(waiting(r).size(), size_t{3});
    CHECK_EQ(area16(r, EV_QLOST), 8u); // A's press went in, its release was lost
    CHECK_EQ(command(r, 'F', "FLUSH"), std::string("00 00"));
    CHECK_EQ(area16(r, EV_QLOST), 0u);
    CHECK(waiting(r).empty());
    wboot(r);
}

TEST(a_full_fifo_is_taken_a_batch_at_a_time_and_loses_no_tick)
{
    // The card's FIFO full at once, 64 events (32 keys pressed and let go): an interrupt takes 16 and
    // leaves the card's flag set, so the next takes the next 16, a tick due between them first. Taken
    // all at once (about 10,000 cycles masked) one tick in two would come behind another and be lost.
    // Each event is queued or counted lost (15 wait in a 256-byte area), none left in the FIFO.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    const uint64_t c0 = r.cpu->cycles();
    const uint32_t m0 = ms_count(r);
    const Costs passes = time_interrupts(r, ms(20), [&] {
        for (unsigned i = 0; i < 32; ++i)
        {
            r.machine->usb_card()->key(static_cast<uint8_t>(A_KEY + i), true);
            r.machine->usb_card()->key(static_cast<uint8_t>(A_KEY + i), false);
        }
    });
    Costs::print("the USB card's passes", passes.usb);
    unsigned n = 0;
    for (const auto &[c, k] : passes.usb)
        n += k;
    CHECK(n >= 4);
    // Each pass is masked from end to end, so a tick that expires as one starts is answered at its end (or
    // in it): if a pass took a whole tick period, the next expiry would come with the first still flagged,
    // and one would be lost. So every pass, a tick's work in it included, is under 7,159 cycles.
    for (const auto &[c, k] : passes.usb)
        CHECK(c < 7159);
    CHECK_EQ(io(r, USB + 3), 0); // EVCOUNT: the FIFO empty
    CHECK_EQ(waiting(r).size(), size_t{15});
    CHECK_EQ(area16(r, EV_QLOST), 49u);
    const std::vector<Event> ev = waiting(r);
    for (unsigned i = 0; i < ev.size(); ++i)
        CHECK(ev[i].code == A_KEY + i / 2 && ev[i].value == (i % 2 ? 0u : 1u));
    const double want = static_cast<double>(r.cpu->cycles() - c0) / 7159.0;
    std::printf("  through the burst: %u ms counted, %.1f in the cycles\n", ms_count(r) - m0, want);
    CHECK(std::abs(static_cast<double>(ms_count(r) - m0) - want) <= 1);
    wboot(r);
}

TEST(the_mouse_in_relative_and_absolute_mode_with_its_wheel_and_buttons)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    UsbCard &usb = *r.machine->usb_card();
    // Relative (cold boot's): movement summed into X and Y, none of it queued.
    usb.mouse_move(10, -5);
    r.run(ms(10));
    usb.mouse_move(3, 2);
    r.run(ms(10));
    CHECK_EQ(area16(r, EV_MX), 13u);
    CHECK_EQ(area16(r, EV_MY), 0xFFFDu); // -3
    CHECK(waiting(r).empty());
    // The wheel: summed in the area until the program clears it, and each change queued.
    usb.mouse_wheel(2);
    r.run(ms(10));
    usb.mouse_wheel(-1);
    r.run(ms(10));
    CHECK_EQ(area(r, EV_MWHEEL), 1);
    // A button pressed and let go.
    usb.mouse_buttons(0x02);
    r.run(ms(10));
    CHECK_EQ(area(r, EV_MBUTTONS), 0x02);
    usb.mouse_buttons(0x00);
    r.run(ms(10));
    std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 4);
    CHECK(ev[0].kind == EK_WHEEL && ev[0].value == 2);
    CHECK(ev[1].kind == EK_WHEEL && ev[1].value == 0xFF);
    CHECK(ev[2].kind == EK_BUTTON && ev[2].code == 1 && ev[2].value == 1);
    CHECK(ev[3].kind == EK_BUTTON && ev[3].code == 1 && ev[3].value == 0);
    // Absolute, limits 319 and 199: the position at once, clamped, and kept current.
    CHECK_EQ(command(r, 'A', "MOUSE"), std::string("00 00"));
    CHECK_EQ(io(r, USB_MCTRL), 1);
    CHECK_EQ(io16(r, USB_MAXX), 319u);
    CHECK_EQ(io16(r, USB_MAXY), 199u);
    usb.mouse_to(400, 50);
    r.run(ms(10));
    CHECK_EQ(area16(r, EV_MX), 319u);
    CHECK_EQ(area16(r, EV_MY), 50u);
    usb.mouse_move(-19, 7);
    r.run(ms(10));
    CHECK_EQ(area16(r, EV_MX), 300u);
    CHECK_EQ(area16(r, EV_MY), 57u);
    CHECK_EQ(waiting(r).size(), size_t{4}); // no moves queued
    // Back to relative: X and Y start again at 0.
    CHECK_EQ(command(r, 'R', "MOUSE"), std::string("00 00"));
    CHECK_EQ(area16(r, EV_MX), 0u);
    CHECK_EQ(area16(r, EV_MY), 0u);
    usb.mouse_move(5, 5);
    r.run(ms(10));
    CHECK_EQ(area16(r, EV_MX), 5u);
    wboot(r);
}

TEST(the_de9_sticks_are_sampled_each_tick_on_both_ports)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    JoystickPorts &joy = *r.machine->joysticks();
    // Nothing pressed: the controllers centred.
    for (unsigned c = 0; c < 2; ++c)
    {
        CHECK_EQ(area(r, EV_CTL + c * 4), 0);
        CHECK_EQ(area(r, EV_CTL + c * 4 + 1), 128);
        CHECK_EQ(area(r, EV_CTL + c * 4 + 2), 128);
    }
    // Stick 1 up and fire for 1.2 ms (a tick and a bit, far under a frame): seen, and let go.
    joy.set(0, JoystickPorts::UP | JoystickPorts::FIRE);
    r.run(ms(1) + ms(1) / 5);
    CHECK_EQ(area(r, EV_CTL), CB_UP | CB_FIRE);
    CHECK_EQ(area(r, EV_CTL + 2), 0); // Y up
    joy.set(0, 0);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL), 0);
    // Stick 2 down and right.
    joy.set(1, JoystickPorts::DOWN | JoystickPorts::RIGHT);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL + 4), CB_DOWN | CB_RIGHT);
    CHECK_EQ(area(r, EV_CTL + 5), 255);
    CHECK_EQ(area(r, EV_CTL + 6), 255);
    joy.set(1, JoystickPorts::LEFT);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL + 5), 0);
    CHECK_EQ(area(r, EV_CTL + 6), 128);
    const std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 4);
    CHECK(ev[0].kind == EK_CTL && ev[0].code == 1 && ev[0].value == (CB_UP | CB_FIRE));
    CHECK(ev[1].kind == EK_CTL && ev[1].code == 1 && ev[1].value == 0);
    CHECK(ev[1].ms - ev[0].ms >= 1 && ev[1].ms - ev[0].ms <= 2);
    CHECK(ev[2].kind == EK_CTL && ev[2].code == 2 && ev[2].value == (CB_DOWN | CB_RIGHT));
    CHECK(ev[3].kind == EK_CTL && ev[3].code == 2 && ev[3].value == CB_LEFT);
    wboot(r);
}

TEST(the_paddles_and_the_game_port_s_buttons)
{
    CcpRig r;
    REQUIRE(r.ok);
    UsbCard &usb = *r.machine->usb_card();
    usb.set_paddle(0, 128);
    usb.set_paddle(1, 128);
    open(r, "2");
    CHECK_EQ(area(r, EV_CTL + 8), 0); // controller 3, the game port: centred
    CHECK_EQ(area(r, EV_CTL + 9), 128);
    // Each paddle's value read at the frame after it changed (PTRIG at a frame's first tick, the read
    // 4 ms later), and controller 3's X, Y and directions from paddles 0 and 1 (64 and 192).
    const struct
    {
        uint8_t x, y, bits;
    } steps[] = {{10, 250, CB_LEFT | CB_DOWN}, {63, 191, CB_LEFT}, {64, 192, CB_DOWN}, {200, 0, CB_RIGHT | CB_UP},
                 {128, 128, 0}};
    for (const auto &s : steps)
    {
        usb.set_paddle(0, s.x);
        usb.set_paddle(1, s.y);
        usb.set_paddle(2, static_cast<uint8_t>(s.x ^ 0xFF));
        usb.set_paddle(3, 77);
        r.run(ms(40)); // two frames at most
        CHECK_EQ(area(r, EV_PADDLES), s.x);
        CHECK_EQ(area(r, EV_PADDLES + 1), s.y);
        CHECK_EQ(area(r, EV_PADDLES + 2), s.x ^ 0xFF);
        CHECK_EQ(area(r, EV_PADDLES + 3), 77);
        CHECK_EQ(area(r, EV_CTL + 9), s.x);
        CHECK_EQ(area(r, EV_CTL + 10), s.y);
        CHECK_EQ(area(r, EV_CTL + 8), s.bits);
    }
    // The buttons, by the card's interrupt: fire bits 4-6.
    usb.set_game_buttons(0x05);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL + 8), 0x50);
    usb.set_game_buttons(0x00);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL + 8), 0);
    const std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() == 7); // each change of bits once: 5 of the paddles' steps, 2 of the buttons
    for (const Event &e : ev)
        CHECK(e.kind == EK_CTL && e.code == 3);
    CHECK_EQ(ev[5].value, 0x50u);
    wboot(r);
}

TEST(controllers_are_swapped_by_function_81_and_put_back_by_the_warm_boot)
{
    CcpRig r;
    REQUIRE(r.ok);
    r.machine->usb_card()->set_paddle(0, 128); // the game port's stick centred
    r.machine->usb_card()->set_paddle(1, 128);
    open(r, "2");
    CHECK_EQ(command(r, 'G', "MAP"), std::string("00 00 01020304"));
    CHECK_EQ(command(r, 'S', "SWAP"), std::string("00 00")); // 1 and 3
    CHECK_EQ(command(r, 'G', "MAP"), std::string("00 00 03020104"));
    CHECK_EQ(area(r, EV_CTL + 3), 3); // controller 1 is the game port now
    CHECK_EQ(area(r, EV_CTL + 11), 1);
    // Stick 1 is controller 3.
    r.machine->joysticks()->set(0, JoystickPorts::FIRE);
    r.run(ms(5));
    CHECK_EQ(area(r, EV_CTL + 8), CB_FIRE);
    CHECK_EQ(area(r, EV_CTL), 0);
    const std::vector<Event> ev = waiting(r);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == EK_CTL && ev[0].code == 3 && ev[0].value == CB_FIRE);
    r.machine->joysticks()->set(0, 0);
    wboot(r);
    open(r, "X");
    CHECK_EQ(command(r, 'G', "MAP"), std::string("00 00 01020304"));
    wboot(r);
}

TEST(a_layout_1_program_is_as_l1_left_it)
{
    // A 16-byte area: layout 1; no USB interrupt on, nothing written past +16, nothing queued.
    CcpRig r;
    REQUIRE(r.ok);
    r.poke(AREA, std::vector<uint8_t>(256, 0x5A));
    open(r, "1");
    CHECK_EQ(area(r, EV_LAYOUT), 1);
    CHECK_EQ(area(r, EV_SIZE), 16);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("EVON")), 1);
    CHECK_EQ(io(r, USB_IEN), 0);
    tap(r, A_KEY);
    r.machine->usb_card()->mouse_move(5, 5);
    r.machine->joysticks()->set(0, JoystickPorts::UP);
    r.run(ms(50));
    r.machine->joysticks()->set(0, 0);
    bool kept = true;
    for (unsigned i = 16; i < 256; ++i)
        kept = kept && area(r, i) == 0x5A;
    CHECK(kept);
    CHECK_EQ(io(r, USB_IEN), 0);
    wboot(r);
}

TEST(the_tick_costs_l1_s_145_cycles_with_no_layout_2_area)
{
    // The dispatcher's cost for a tick alone: with no area exactly L1's (145 cycles, the interrupt's 7
    // and the RTI included); a layout-1 area's and a layout-2 area's, said; and the USB card's
    // interrupt for a key, said.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    const Costs none = time_interrupts(r, ms(600));
    Costs::print("no area, a tick", none.tick);
    REQUIRE(!none.tick.empty());
    CHECK_EQ(Costs::mode(none.tick), 145u);
    for (const auto &[c, n] : none.tick)
        CHECK(c == 145 || (c == 159 && n <= 3)); // and L1's 159 at MS's carry, once in 256 ms
    wboot(r);
    open(r, "1");
    const Costs one = time_interrupts(r, ms(600));
    Costs::print("layout 1, a tick", one.tick);
    CHECK_EQ(Costs::mode(one.tick), 145u + 39u);
    wboot(r);
    open(r, "2");
    const Costs two = time_interrupts(r, ms(600));
    Costs::print("layout 2, a tick", two.tick);
    Costs::print("layout 2, other interrupts", two.other);
    CHECK(Costs::mode(two.tick) > 145u + 39u && Costs::mode(two.tick) < 300u);
    const Costs key = time_interrupts(r, ms(60), [&] { r.machine->usb_card()->key(A_KEY, true); });
    Costs::print("layout 2, the USB card's interrupt for a key", key.usb);
    CHECK(!key.usb.empty());
    wboot(r);
}

TEST(ctrl_c_reaches_the_queue_with_function_109_s_mode_set)
{
    // Console mode $000A (no ^C ending the program): ^C in function 10 is a character, the program goes
    // on, and the queue has its keys, the Ctrl held with the C.
    CcpRig r;
    REQUIRE(r.ok);
    const size_t from = open(r, "C");
    CHECK(r.calls(CcpRig::C_READSTR_FN));
    r.type_usb("\x03\r");
    CHECK(r.until("\nLINE ", ms(2000), from));
    r.run(ms(50));
    CHECK_EQ(field(r.out.substr(from), "LINE"), std::string("010300"));
    const std::vector<Event> ev = waiting(r);
    show(ev);
    REQUIRE(ev.size() >= 6);
    CHECK(ev[0].code == 0xE0 && ev[0].value == 1 && ev[0].mods == 0x01);
    CHECK(ev[1].code == C_KEY && ev[1].value == 1 && ev[1].mods == 0x01);
    CHECK(ev[2].code == C_KEY && ev[2].value == 0);
    CHECK(ev[3].code == 0xE0 && ev[3].value == 0 && ev[3].mods == 0);
    CHECK(ev[4].code == ENTER && ev[4].value == 1);
    CHECK_EQ(command(r, 'F', "FLUSH"), std::string("00 00")); // still running
    wboot(r);
}

/// What the warm boot puts back: the USB card's interrupt off, the mouse, LEDs and repeat as cold boot
/// set them, the controllers 1-4, no area.
static void input_put_back(CcpRig &r)
{
    CHECK_EQ(io(r, USB_IEN), 0);
    CHECK_EQ(io(r, USB_MCTRL), 0);
    CHECK_EQ(io16(r, USB_MAXX), 639u);
    CHECK_EQ(io16(r, USB_MAXY), 479u);
    CHECK_EQ(io(r, USB_LEDS), 0);
    CHECK_EQ(io(r, USB_REPEAT), 0x10);
    const uint32_t cmap = SYS_BLOCK_PHYS + sym("CMAP");
    for (unsigned c = 0; c < 4; ++c)
        CHECK_EQ(r.phys(cmap + c), c + 1);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + sym("EVON")), 0);
}

/// L2TEST with an area, and everything a program may change changed.
static void change_it_all(CcpRig &r)
{
    open(r, "2");
    CHECK_EQ(command(r, 'A', "MOUSE"), std::string("00 00"));
    CHECK_EQ(command(r, 'L', "LEDS"), std::string("00 00"));
    CHECK_EQ(command(r, 'P', "REPEAT"), std::string("00 00"));
    CHECK_EQ(command(r, 'S', "SWAP"), std::string("00 00"));
    CHECK_EQ(io(r, USB_LEDS), 5);
    CHECK_EQ(io(r, USB_REPEAT), 0x25);
    CHECK_EQ(io(r, USB_IEN), 0x0F);
}

TEST(the_warm_boot_puts_input_back_by_jmp_wboot_by_ctrl_c_and_break)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        input_put_back(r); // cold boot's
        change_it_all(r);
        wboot(r);
        input_put_back(r);
        // And the queue gone with the area: a key now goes nowhere.
        const unsigned head = area(r, EV_QHEAD);
        tap(r, A_KEY);
        CHECK_EQ(area(r, EV_QHEAD), head);
    }
    {
        CcpRig r; // ^C at function 10's prompt, the CCP's console mode
        REQUIRE(r.ok);
        change_it_all(r);
        const size_t from = r.out.size();
        r.poke(MBOX, 'D');
        CHECK(r.calls(CcpRig::C_READSTR_FN));
        r.type("\x03");
        CHECK(r.until("\r\nA>", ms(2000), from));
        CHECK(r.out.find("LINEOK", from) == std::string::npos);
        input_put_back(r);
    }
    {
        // Break: the monitor, the ROM's QUIET turning the USB card's interrupt off; OODOS booted again,
        // as cold boot sets it.
        CcpRig r;
        REQUIRE(r.ok);
        change_it_all(r);
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(io(r, USB_IEN), 0);
        REQUIRE(r.boot());
        input_put_back(r);
    }
}

TEST(function_81_s_info_and_its_refusals)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    command(r, 'I', "FINFOB");
    CHECK_EQ(field(r.out, "INFO"), std::string("03 02")); // keyboard and mouse; layout 2
    CHECK_EQ(field(r.out, "INFOB"), std::string("0302"));
    CHECK_EQ(field(r.out, "FINFO"), std::string("02 03")); // OOLIB 2: frame and timing, input
    CHECK_EQ(field(r.out, "FINFOB"), std::string("02030302"));
    CHECK_EQ(command(r, 'N', "NEXT"), std::string("FF FF EEEEEEEEEEEEEEEE")); // no layout-2 area
    CHECK_EQ(command(r, 'F', "FLUSH"), std::string("00 00"));                 // nothing to flush
    command(r, 'B', "BAD5");
    for (const char *k : {"BAD1", "BAD2", "BAD3", "BAD4", "BAD5"})
        CHECK_EQ(field(r.out, k), std::string("FF FF"));
    wboot(r);
}

int main() { return run_all(); }
