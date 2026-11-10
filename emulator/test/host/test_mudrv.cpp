/**
 * @file test_mudrv.cpp
 * @brief OOLIB's music driver on the sound card's 6510 (oodos/lib/mudrv/mudrv.s; hw/oolib.md, L6a).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The driver is assembled with ca65 at build time and loaded into the card
 * at $1000 through RAMDATA, as function 82's SN_LOAD puts it, its latch set
 * for the clock, and started with CTRL RUN | RESET. The host's W65C02S
 * spins in RAM; the test is the host's side of the mailbox: it sends each
 * byte once STATUS says the card took the last, and takes the card's
 * bytes. The oracle is the SIDs' registers as the 6510 wrote them, sampled
 * every 0.1 ms. Without cc65 it skips.
 */

#include "harness.h"
#include "test_support.h"

#include "cpu6502.h"
#include "machine.h"
#include "sound_card.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    int skipped = 0;
    constexpr uint16_t SOUND = 0xC500;
    constexpr uint16_t BASE = 0x1000, LATCH = 0x1003, VERSION = 0x1005, LOST = 0x1006, TICKS = 0x1007; // the image
    constexpr uint16_t PAL_LATCH = 984, NTSC_LATCH = 1022;
    constexpr double STEP = 0.0001; // the sampling: 0.1 ms
    constexpr double SLACK = 0.5;   // ms: a step's interrupt and the sampling

    std::vector<uint8_t> image()
    {
        std::ifstream in(EMU_MUDRV, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    struct Event
    {
        uint8_t byte;
        double ms;
    };

    /// A backplane with the sound card and the driver started, the host's side of the mailbox.
    struct Board
    {
        explicit Board(uint8_t ctrl = 0, uint16_t latch = 0)
        {
            std::string error;
            machine = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                                     "sound 500-57F space=io name=sound irq\n",
                                     "sound.machine", {}, error);
            sound = machine->sound_card();
            Decoder &bus = machine->bus();
            put(BASE);
            for (const uint8_t b : image())
                bus.write(SOUND + SoundCard::RAMDATA, b);
            if (!latch)
                latch = ctrl & SoundCard::CTRL_NTSC ? NTSC_LATCH : PAL_LATCH;
            put(LATCH);
            bus.write(SOUND + SoundCard::RAMDATA, static_cast<uint8_t>(latch));
            bus.write(SOUND + SoundCard::RAMDATA, static_cast<uint8_t>(latch >> 8));
            put(0xFFFC);
            bus.write(SOUND + SoundCard::RAMDATA, static_cast<uint8_t>(BASE));
            bus.write(SOUND + SoundCard::RAMDATA, static_cast<uint8_t>(BASE >> 8));
            bus.poke(0x0200, 0x4C); // JMP $0200
            bus.poke(0x0201, 0x00);
            bus.poke(0x0202, 0x02);
            cpu = std::make_unique<Cpu6502>(bus, Cpu6502::Model::W65C02S);
            cpu->set_regs(Registers{0x0200, 0xFF, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});
            bus.write(SOUND + SoundCard::CTRL,
                      static_cast<uint8_t>(SoundCard::CTRL_RUN | SoundCard::CTRL_RESET | ctrl));
            run(10); // its set-up: about 3 ms
        }
        void put(uint16_t addr)
        {
            machine->bus().write(SOUND + SoundCard::RAMADDR, static_cast<uint8_t>(addr));
            machine->bus().write(SOUND + SoundCard::RAMADDR + 1, static_cast<uint8_t>(addr >> 8));
        }
        double now() const { return static_cast<double>(cpu->cycles()) / SoundCard::NATIVE_HZ * 1000; }
        uint8_t status() { return machine->bus().read(SOUND + SoundCard::STATUS); }
        /// One sampling step of the host, the card's bytes taken (unless held) and `watch` called.
        void tick()
        {
            const uint64_t until = cpu->cycles() + static_cast<uint64_t>(STEP * SoundCard::NATIVE_HZ);
            while (cpu->cycles() < until)
            {
                cpu->step();
                machine->bus().irq(); // which also brings every timed card up to date
            }
            if (!hold && status() & SoundCard::STATUS_TO_HOST)
                events.push_back({machine->bus().read(SOUND + SoundCard::MBIN), now()});
            if (watch)
                watch(now());
        }
        /// `cycles` of the host, the card kept up and its bytes taken, the registers not sampled.
        void spin(uint64_t cycles)
        {
            const uint64_t until = cpu->cycles() + cycles;
            while (cpu->cycles() < until)
            {
                cpu->step();
                machine->bus().irq();
                if (!hold && status() & SoundCard::STATUS_TO_HOST)
                    events.push_back({machine->bus().read(SOUND + SoundCard::MBIN), now()});
            }
        }
        void run(double ms)
        {
            const double until = now() + ms;
            while (now() < until)
                tick();
        }
        /// Bytes to the card, each once it has taken the one before.
        bool send(std::initializer_list<int> bytes)
        {
            for (const int b : bytes)
            {
                int waited = 0;
                while (status() & SoundCard::STATUS_TO_CARD)
                {
                    tick();
                    if (++waited > 1000)
                        return false;
                }
                machine->bus().write(SOUND + SoundCard::MBOUT, static_cast<uint8_t>(b));
            }
            return true;
        }
        /// Bytes to the card, each the moment it has taken the one before (the host polling as fast as it runs).
        bool rush(std::initializer_list<int> bytes)
        {
            for (const int b : bytes)
            {
                uint64_t waited = 0;
                while (status() & SoundCard::STATUS_TO_CARD)
                {
                    spin(4);
                    if ((waited += 4) > 100'000)
                        return false;
                }
                machine->bus().write(SOUND + SoundCard::MBOUT, static_cast<uint8_t>(b));
            }
            return true;
        }
        bool note(int v, unsigned freq, unsigned len, unsigned gate)
        {
            return send({v, static_cast<int>(freq & 0xFF), static_cast<int>(freq >> 8), static_cast<int>(len & 0xFF),
                         static_cast<int>(len >> 8), static_cast<int>(gate & 0xFF), static_cast<int>(gate >> 8)});
        }
        const std::array<uint8_t, SoundCard::SID_REGS> &sid(int v) const { return sound->sid(v / 3); }
        static unsigned off(int v) { return 7u * static_cast<unsigned>(v % 3); }
        unsigned freq(int v) const { return sid(v)[off(v)] | sid(v)[off(v) + 1] << 8; }
        uint8_t ctrl(int v) const { return sid(v)[off(v) + 4]; }
        std::vector<uint8_t> bytes() const
        {
            std::vector<uint8_t> out;
            for (const Event &e : events)
                out.push_back(e.byte);
            return out;
        }
        /// The first event `b` from `from` on, or -1.
        double when(uint8_t b, size_t from = 0) const
        {
            for (size_t i = from; i < events.size(); ++i)
                if (events[i].byte == b)
                    return events[i].ms;
            return -1;
        }
        std::unique_ptr<Machine> machine;
        SoundCard *sound{nullptr};
        std::unique_ptr<Cpu6502> cpu;
        std::vector<Event> events;
        std::function<void(double)> watch;
        bool hold{false};
    };

    /// When voice v's gate goes on and off, and its frequency changes, as the samples see them.
    struct Edges
    {
        std::vector<double> on, off, freq;
        std::vector<unsigned> freqs;
    };
    void track(Board &b, int v, Edges &e)
    {
        uint8_t gate = b.ctrl(v) & 1;
        unsigned f = b.freq(v);
        b.watch = [&b, v, &e, gate, f](double ms) mutable {
            const uint8_t g = b.ctrl(v) & 1;
            if (g && !gate)
                e.on.push_back(ms);
            if (!g && gate)
                e.off.push_back(ms);
            gate = g;
            if (b.freq(v) != f)
            {
                f = b.freq(v);
                e.freq.push_back(ms);
                e.freqs.push_back(f);
            }
        };
    }
    bool near(double got, double want) { return std::fabs(got - want) <= SLACK; }
} // namespace

#define NEED_DRIVER()                                                                                                  \
    if (image().empty())                                                                                               \
    return void(++skipped)

TEST(the_image_and_hello)
{
    NEED_DRIVER();
    Board b;
    CHECK_EQ(b.sound->ram()[VERSION], 1);
    CHECK_EQ(b.sound->timer_latch(), PAL_LATCH);
    CHECK(b.send({0x70}));
    b.run(1);
    CHECK(b.bytes() == std::vector<uint8_t>{0x01});
    CHECK_EQ(b.sound->ram()[LOST], 0);
    // TICKS counts the steps: a millisecond each.
    const auto ticks = [&b] {
        return static_cast<unsigned>(b.sound->ram()[TICKS] | b.sound->ram()[TICKS + 1] << 8 |
                                     b.sound->ram()[TICKS + 2] << 16);
    };
    const unsigned t0 = ticks();
    b.run(300);
    CHECK(ticks() - t0 >= 299 && ticks() - t0 <= 301);
    // Silent at the start: both volumes 15, every voice a pulse with its gate off.
    CHECK_EQ(b.sound->sid(0)[0x18], 15);
    CHECK_EQ(b.sound->sid(1)[0x18], 15);
    for (int v = 0; v < 6; ++v)
        CHECK_EQ(b.ctrl(v), 0x00);
}

TEST(a_note_s_gate_and_length_by_the_millisecond)
{
    NEED_DRIVER();
    Board b;
    Edges e;
    track(b, 0, e);
    CHECK(b.note(0, 0x1CD6, 100, 87));
    CHECK(b.note(0, 0x2000, 50, 50));
    b.run(200);
    REQUIRE(e.on.size() == 2 && e.off.size() == 2);
    CHECK(near(e.off[0] - e.on[0], 87));
    CHECK(near(e.on[1] - e.on[0], 100));
    CHECK(near(e.off[1] - e.on[1], 50));
    REQUIRE(e.freqs.size() == 2);
    CHECK_EQ(e.freqs[0], 0x1CD6u);
    CHECK_EQ(e.freqs[1], 0x2000u);
    CHECK(near(e.freq[1] - e.freq[0], 100));
    // The default instrument: a 50% pulse, AD $00, SR $F0.
    CHECK_EQ(b.sid(0)[2] | b.sid(0)[3] << 8, 0x0800);
    CHECK_EQ(b.sid(0)[5], 0x00);
    CHECK_EQ(b.sid(0)[6], 0xF0);
    CHECK_EQ(b.ctrl(0), 0x40);
    CHECK(b.bytes() == (std::vector<uint8_t>{0x10, 0x10, 0x20}));
    const double said = b.when(0x10) - e.on[0]; // said at the note's end, within the mailbox's round
    CHECK(said >= 100 - SLACK && said < 100 + 1.5);
}

TEST(play_s_articulation_legato_and_staccato)
{
    NEED_DRIVER();
    Board b;
    Edges e;
    track(b, 1, e);
    CHECK(b.note(1, 0x1000, 80, 80)); // ML: the whole length
    CHECK(b.note(1, 0x1100, 80, 60)); // MS: 3/4
    CHECK(b.note(1, 0x1200, 80, 70)); // MN: 7/8
    b.run(260);
    REQUIRE(e.on.size() == 3 && e.off.size() == 3);
    CHECK(near(e.off[0] - e.on[0], 80));
    CHECK(near(e.off[1] - e.on[1], 60));
    CHECK(near(e.off[2] - e.on[2], 70));
    CHECK(near(e.on[2] - e.on[0], 160));
}

TEST(a_rest_is_silent_and_takes_its_time)
{
    NEED_DRIVER();
    Board b;
    Edges e;
    track(b, 2, e);
    CHECK(b.note(2, 0x1500, 40, 30));
    CHECK(b.note(2, 0, 50, 50)); // a rest
    CHECK(b.note(2, 0x1600, 40, 40));
    b.run(160);
    REQUIRE(e.on.size() == 2);
    CHECK(near(e.on[1] - e.on[0], 90));
    REQUIRE(e.freqs.size() == 2); // the rest wrote no frequency
    CHECK_EQ(e.freqs[1], 0x1600u);
    CHECK(b.bytes() == (std::vector<uint8_t>{0x12, 0x12, 0x12, 0x22}));
}

TEST(six_voices_each_on_its_own_registers)
{
    NEED_DRIVER();
    Board b;
    for (int v = 0; v < 6; ++v)
        CHECK(b.note(v, 0x1000 + 0x111 * static_cast<unsigned>(v), 30, 30));
    b.run(5);
    for (int v = 0; v < 6; ++v)
    {
        CHECK_EQ(b.freq(v), 0x1000u + 0x111u * static_cast<unsigned>(v));
        CHECK_EQ(b.ctrl(v), 0x41);
    }
    b.run(40);
    for (int v = 0; v < 6; ++v)
        CHECK_EQ(b.ctrl(v), 0x40);
    std::vector<uint8_t> got = b.bytes();
    CHECK_EQ(got.size(), 12u);
    for (int v = 0; v < 6; ++v)
    {
        CHECK(b.when(static_cast<uint8_t>(0x10 | v)) >= 0);
        CHECK(b.when(static_cast<uint8_t>(0x20 | v)) >= 0);
    }
}

TEST(the_queue_holds_32_the_33rd_is_refused_and_no_event_is_lost)
{
    NEED_DRIVER();
    Board b;
    CHECK(b.send({0x40})); // paused, so nothing is taken from the queue
    for (int i = 0; i < 33; ++i)
        CHECK(b.note(3, 0x1000 + static_cast<unsigned>(i), 3, 2));
    b.run(2);
    CHECK(b.bytes() == std::vector<uint8_t>{0x33});
    // The host takes nothing for a while: the 33 events wait on the card, in order.
    b.hold = true;
    CHECK(b.send({0x50}));
    b.run(150);
    CHECK_EQ(b.ctrl(3) & 1, 0);
    b.hold = false;
    b.run(20);
    std::vector<uint8_t> want{0x33};
    for (int i = 0; i < 32; ++i)
        want.push_back(0x13);
    want.push_back(0x23);
    CHECK(b.bytes() == want);
    CHECK_EQ(b.sound->ram()[LOST], 0); // none dropped
}

TEST(a_marker_comes_back_when_its_voice_reaches_it)
{
    NEED_DRIVER();
    Board b;
    CHECK(b.note(1, 0x1000, 30, 30));
    CHECK(b.send({0x31, 9}));
    CHECK(b.note(1, 0x1100, 30, 30));
    b.run(80);
    CHECK(b.bytes() == (std::vector<uint8_t>{0x11, 0x99, 0x11, 0x21}));
    CHECK(b.when(0x99) - b.when(0x11) < 1.5); // in the step the first note ended
}

TEST(stop_empties_the_queue_and_drops_the_gate)
{
    NEED_DRIVER();
    Board b;
    for (int i = 0; i < 3; ++i)
        CHECK(b.note(4, 0x1800, 200, 200));
    b.run(10);
    CHECK_EQ(b.ctrl(4), 0x41);
    CHECK(b.send({0x24}));
    b.run(2);
    CHECK_EQ(b.ctrl(4), 0x40);
    b.run(500);
    CHECK(b.bytes() == std::vector<uint8_t>{0x24}); // said, and nothing after
    // All six: each says it is empty.
    b.events.clear();
    CHECK(b.note(0, 0x1000, 100, 100));
    CHECK(b.send({0x27}));
    b.run(3);
    CHECK_EQ(b.ctrl(0), 0x40);
    CHECK(b.bytes() == (std::vector<uint8_t>{0x25, 0x24, 0x23, 0x22, 0x21, 0x20}));
}

TEST(pause_holds_the_time_and_silences_both_sids)
{
    NEED_DRIVER();
    Board b;
    Edges e;
    track(b, 5, e);
    CHECK(b.note(5, 0x2000, 100, 100));
    b.run(20);
    CHECK(b.send({0x40}));
    b.run(2);
    CHECK_EQ(b.sound->sid(0)[0x18], 0);
    CHECK_EQ(b.sound->sid(1)[0x18], 0);
    b.run(50);
    CHECK(e.off.empty());
    CHECK(b.send({0x50}));
    b.run(2);
    CHECK_EQ(b.sound->sid(0)[0x18], 15);
    CHECK_EQ(b.sound->sid(1)[0x18], 15);
    b.run(100);
    REQUIRE(e.on.size() == 1 && e.off.size() == 1);
    CHECK(std::fabs(e.off[0] - e.on[0] - 152) <= 2); // 100 ms played, 52 paused
}

TEST(an_instrument_from_the_voice_s_next_note)
{
    NEED_DRIVER();
    Board b;
    CHECK(b.send({0x10, 0x29, 0x12, 0x34, 0x23, 0x01})); // saw; the gate and TEST bits ignored
    CHECK(b.note(0, 0x1234, 20, 20));
    b.run(5);
    CHECK_EQ(b.ctrl(0), 0x21);
    CHECK_EQ(b.sid(0)[5], 0x12);
    CHECK_EQ(b.sid(0)[6], 0x34);
    CHECK_EQ(b.sid(0)[2] | b.sid(0)[3] << 8, 0x0123);
    b.run(20);
    CHECK_EQ(b.ctrl(0), 0x20);
    CHECK_EQ(b.ctrl(1), 0x00); // the others as they were
}

TEST(volume_by_sid_and_refusals)
{
    NEED_DRIVER();
    Board b;
    CHECK(b.send({0x60, 5}));
    b.run(1);
    CHECK_EQ(b.sound->sid(0)[0x18], 5);
    CHECK_EQ(b.sound->sid(1)[0x18], 15);
    CHECK(b.send({0x61, 7}));
    b.run(1);
    CHECK_EQ(b.sound->sid(1)[0x18], 7);
    CHECK(b.send({0x67, 9}));
    b.run(1);
    CHECK_EQ(b.sound->sid(0)[0x18], 9);
    CHECK_EQ(b.sound->sid(1)[0x18], 9);
    CHECK(b.send({0x62, 1}));             // no third SID
    CHECK(b.send({0x80}));                // no such command
    CHECK(b.note(6, 0x1000, 10, 10));     // no voice 6: its operands taken, then refused
    CHECK(b.send({0x26}));                // nor STOP of it
    CHECK(b.send({0x70}));                // and the next command still understood
    b.run(3);
    CHECK(b.bytes() == (std::vector<uint8_t>{0x37, 0x37, 0x37, 0x37, 0x01}));
    CHECK_EQ(b.sound->sid(0)[0x18], 9);
    CHECK_EQ(b.ctrl(0), 0x00);
}

TEST(on_the_ntsc_clock_its_latch_keeps_the_millisecond)
{
    NEED_DRIVER();
    Board b(SoundCard::CTRL_NTSC);
    CHECK_EQ(b.sound->timer_latch(), NTSC_LATCH);
    Edges e;
    track(b, 0, e);
    CHECK(b.note(0, 0x1000, 100, 87));
    b.run(110);
    REQUIRE(e.on.size() == 1 && e.off.size() == 1);
    CHECK(near(e.off[0] - e.on[0], 87));
    // PAL's latch on the NTSC clock runs fast: 87 steps in about 83.8 ms, which the check above refuses.
    Board w(SoundCard::CTRL_NTSC, PAL_LATCH);
    Edges f;
    track(w, 0, f);
    CHECK(w.note(0, 0x1000, 100, 87));
    w.run(110);
    REQUIRE(f.on.size() == 1 && f.off.size() == 1);
    CHECK(!near(f.off[0] - f.on[0], 87));
    CHECK(std::fabs(f.off[0] - f.on[0] - 87.0 * 985 / 1022.727) <= SLACK);
}

TEST(commands_taken_while_the_step_takes_entries_land_whole_and_in_place)
{
    // The main loop queues commands while the millisecond's interrupt takes entries off the same queues and starts
    // notes; neither may see the other's work half done. Five voices play 4 ms notes (a heavy load: PLAY's shortest
    // is about 15 ms) while markers stream onto voice 5 as fast as the card takes them, each taken off at the next
    // step, so that queue is emptied as it is filled; their arrival is jittered against the step so every phase of
    // it is met. One put a slot too far leaves the slot behind it unwritten, and the step plays what an older entry
    // left there: an old marker, which (numbered n mod 13) breaks the order. This load sends more events than the
    // mailbox carries, so some are dropped (LOST, as documented): the markers that come back must be the ones sent,
    // in order, with gaps but nothing else.
    NEED_DRIVER();
    Board b;
    std::vector<uint8_t> want;
    unsigned n = 0;
    for (int round = 0; round < 24; ++round)
    {
        CHECK(b.send({0x40}));
        for (int v = 0; v < 5; ++v)
            for (int i = 0; i < 32; ++i)
                CHECK(b.note(v, 0x1000 + static_cast<unsigned>(i), 4, 3));
        CHECK(b.send({0x50}));
        for (int m = 0; m < 150; ++m, ++n)
        {
            CHECK(b.rush({0x35, static_cast<int>(n % 13)}));
            want.push_back(static_cast<uint8_t>(0xD0 | (n % 13)));
            b.spin((n * 397) % 1433); // 0-0.2 ms of the host
        }
        b.run(140);
    }
    std::vector<uint8_t> got;
    for (const Event &e : b.events)
        if ((e.byte & 0xF0) == 0xD0)
            got.push_back(e.byte);
    size_t w = 0, matched = 0;
    for (const uint8_t g : got)
    {
        while (w < want.size() && want[w] != g)
            ++w;
        if (w == want.size())
            break;
        ++w;
        ++matched;
    }
    CHECK_EQ(matched, got.size());           // each one back is the next sent, gaps allowed
    CHECK(got.size() >= want.size() * 8 / 10); // and most came back
}

namespace
{
    struct Load
    {
        double share;   ///< of the 6510's time, in the step
        double longest; ///< one step, in the card's clocks
    };
    /// How much of the card's time the step takes over `ms`, the 6510's PC sampled at each host instruction: the
    /// interrupt's code is the image from its vector to the end.
    Load measure(Board &b, double ms)
    {
        const uint16_t irq = static_cast<uint16_t>(b.sound->ram()[0xFFFE] | b.sound->ram()[0xFFFF] << 8);
        const uint16_t end = static_cast<uint16_t>(BASE + image().size());
        uint64_t in = 0, total = 0, start = 0, longest = 0;
        bool inside = false;
        const uint64_t until = b.cpu->cycles() + static_cast<uint64_t>(ms / 1000 * SoundCard::NATIVE_HZ);
        while (b.cpu->cycles() < until)
        {
            b.spin(1);
            const uint16_t pc = b.sound->cpu().regs().pc;
            const bool now = pc >= irq && pc < end;
            ++total;
            in += now;
            if (now && !inside)
                start = b.cpu->cycles();
            if (!now && inside)
                longest = std::max<uint64_t>(longest, b.cpu->cycles() - start);
            inside = now;
        }
        return {static_cast<double>(in) / static_cast<double>(total),
                static_cast<double>(longest) * SoundCard::PAL_HZ / SoundCard::NATIVE_HZ};
    }
} // namespace

TEST(the_step_leaves_the_6510_time_for_the_mailbox)
{
    // A voice holding a note costs the step an INC and a branch, so six together leave most of each millisecond to
    // the main loop, which takes commands and sends events. Six starting in one step run past the millisecond, so
    // the next step runs late, but not by a whole one. (Measured 2026-11-09: 18% and 134 clocks; 1,119.)
    NEED_DRIVER();
    Board hold;
    for (int v = 0; v < 6; ++v)
        CHECK(hold.note(v, 0x1000, 1000, 1000));
    hold.run(3);
    const Load h = measure(hold, 50);
    std::printf("  six holding: %.1f%% of the 6510, a step at most %.0f clocks\n", h.share * 100, h.longest);
    CHECK(h.share < 0.22);
    CHECK(h.longest < 160);
    Board chord;
    CHECK(chord.send({0x40}));
    for (int v = 0; v < 6; ++v)
    {
        CHECK(chord.note(v, 0x1000, 10, 5));
        CHECK(chord.note(v, 0x1100, 10, 5));
    }
    CHECK(chord.send({0x50}));
    const Load c = measure(chord, 30);
    std::printf("  six starting together: a step at most %.0f clocks\n", c.longest);
    CHECK(c.longest < 1300);
    CHECK(c.longest < 2 * (PAL_LATCH + 1));
}

int main()
{
    const int rc = run_all();
    if (rc == 0 && skipped == static_cast<int>(ooe::test::registry().size()))
    {
        std::printf("SKIP: the driver was not built (needs ca65/ld65 on PATH at configure time)\n");
        return SKIP;
    }
    return rc;
}
