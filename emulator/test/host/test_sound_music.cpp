/**
 * @file test_sound_music.cpp
 * @brief The sound card, revision 3: music (note blocks in one transfer, the sequencer, notes waiting, the
 * threshold, markers and MUSIC_DONE; hw/design.md, "Music" and "The note block").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "sound_card3.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    struct Event
    {
        uint8_t kind{0};
        std::vector<uint8_t> body;
    };
    uint32_t le32(const std::vector<uint8_t> &v, size_t at)
    {
        return static_cast<uint32_t>(v.at(at) | v.at(at + 1) << 8 | v.at(at + 2) << 16) | static_cast<uint32_t>(v.at(at + 3)) << 24;
    }
    uint32_t le16(const std::vector<uint8_t> &v, size_t at) { return static_cast<uint32_t>(v.at(at) | v.at(at + 1) << 8); }
    void put32(std::vector<uint8_t> &v, uint32_t x)
    {
        for (int i = 0; i < 4; ++i)
            v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    }

    /// The card at PAL's SID clock as the host's, up.
    struct Dev
    {
        SoundCard3 card;
        std::vector<Event> seen;
        Dev()
        {
            card.set_cpu_hz(SoundCard::PAL_HZ);
            card.set_adc_rate(0);
            ms(SoundFw::STARTUP_MS + 1);
        }
        void ms(unsigned n)
        {
            for (unsigned i = 0; i < n; ++i)
            {
                card.tick(SoundCard::PAL_HZ / 1000);
                drain();
            }
        }
        void send(uint8_t cmd, uint8_t tag, const std::vector<uint8_t> &p = {})
        {
            card.write(SoundCard3::CMD, cmd);
            card.write(SoundCard3::CMD, static_cast<uint8_t>(p.size()));
            card.write(SoundCard3::CMD, tag);
            for (const uint8_t b : p)
                card.write(SoundCard3::CMD, b);
        }
        void drain()
        {
            while (card.peek(SoundCard3::EVTLEN))
            {
                Event e;
                e.kind = card.read(SoundCard3::EVT);
                const uint8_t n = card.read(SoundCard3::EVT);
                for (unsigned i = 0; i < n; ++i)
                    e.body.push_back(card.read(SoundCard3::EVT));
                seen.push_back(e);
            }
        }
        std::vector<uint8_t> ask(uint8_t cmd, const std::vector<uint8_t> &p = {})
        {
            const size_t from = seen.size();
            send(cmd, 0x42, p);
            card.tick(100);
            drain();
            for (size_t i = from; i < seen.size(); ++i)
                if (seen[i].kind == SoundFw::EV_REPLY && seen[i].body[0] == 0x42)
                    return seen[i].body;
            return {};
        }
        std::vector<Event> of(uint8_t kind) const
        {
            std::vector<Event> out;
            for (const Event &e : seen)
                if (e.kind == kind)
                    out.push_back(e);
            return out;
        }
        /// A block into `slot` and played with `flags`. @return MUSIC_PLAY's status (or XFERDONE's, if not 0)
        uint8_t music(uint8_t slot, const std::vector<uint8_t> &block, uint8_t flags = 0)
        {
            const uint32_t n = static_cast<uint32_t>(block.size());
            const auto r = ask(SoundFw::MUSIC_LOAD, {slot, static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n >> 16)});
            if (r.size() < 2 || r[1])
                return r.size() < 2 ? 0xFF : r[1];
            for (const uint8_t b : block)
                card.write(SoundCard3::DATA, b);
            card.tick(100);
            drain();
            const auto done = of(SoundFw::EV_XFERDONE);
            if (done.empty() || done.back().body[6])
                return done.empty() ? 0xFE : done.back().body[6];
            return ask(SoundFw::MUSIC_PLAY, {slot, flags})[1];
        }
        std::vector<uint8_t> status() { return ask(SoundFw::MUSIC_STATUS); }
    };

    /// A note block: voice 0 alone, `inst` its instrument, then `entries` (built below).
    std::vector<uint8_t> block(const std::vector<uint8_t> &inst, const std::vector<uint8_t> &entries, uint32_t count,
                               uint8_t mask = 1)
    {
        std::vector<uint8_t> b{1, mask, 0, 0};
        put32(b, count);
        b.insert(b.end(), inst.begin(), inst.end());
        b.insert(b.end(), entries.begin(), entries.end());
        return b;
    }
    const std::vector<uint8_t> TONE = {2, 0, 0, 0, 0, 0, 255, 128};                  // the tone generator
    const std::vector<uint8_t> SAW = {0, 0x20, 0x00, 0xF0, 0, 8, 255, 128};           // a SID sawtooth
    void note(std::vector<uint8_t> &e, uint8_t voice, uint8_t n, uint32_t len_us, uint32_t gate_us)
    {
        e.push_back(static_cast<uint8_t>(0x00 | voice));
        e.push_back(n);
        put32(e, len_us);
        put32(e, gate_us);
    }
    /// A tone entry: `freq16` the pitch in 1/16 Hz (24 bits).
    void tone(std::vector<uint8_t> &e, uint8_t voice, uint32_t freq16, uint32_t len_us, uint32_t gate_us)
    {
        e.push_back(static_cast<uint8_t>(0x10 | voice));
        for (int i = 0; i < 3; ++i)
            e.push_back(static_cast<uint8_t>(freq16 >> (8 * i)));
        put32(e, len_us);
        put32(e, gate_us);
    }
    void rest(std::vector<uint8_t> &e, uint8_t voice, uint32_t len_us)
    {
        e.push_back(static_cast<uint8_t>(0x20 | voice));
        put32(e, len_us);
    }
} // namespace

TEST(ten_thousand_notes_in_one_block_end_with_one_done)
{
    // 10,000 notes of 1 ms each (a gate of half), one block, one transfer: 10 s; one MUSIC_DONE at its end.
    Dev d;
    std::vector<uint8_t> e;
    for (int i = 0; i < 10000; ++i)
        note(e, 0, static_cast<uint8_t>(60 + i % 24), 1000, 500);
    const auto b = block(TONE, e, 10000);
    std::printf("  a block of %zu bytes\n", b.size());
    CHECK_EQ(d.music(0, b), 0);
    CHECK(d.card.firmware().music_tone_on());
    d.ms(9990);
    CHECK(d.of(SoundFw::EV_MUSIC_DONE).empty());
    CHECK_EQ(d.card.firmware().music_state(), SoundFw::M_PLAYING);
    d.ms(30);
    const auto done = d.of(SoundFw::EV_MUSIC_DONE);
    REQUIRE(done.size() == 1);
    std::printf("  MUSIC_DONE after %u ms played\n", le32(done[0].body, 5));
    CHECK(le32(done[0].body, 5) >= 9999 && le32(done[0].body, 5) <= 10001);
    CHECK_EQ(d.card.firmware().music_state(), SoundFw::M_STOPPED);
    CHECK(!d.card.firmware().music_tone_on());
}

TEST(notes_waiting_are_exact_and_the_threshold_fires_once_below_n)
{
    // 20 notes of 10 ms; the threshold 10. The first begins at once: 19 waiting; each 10 ms one fewer; MUSIC_COUNT
    // once, when the 11th begins (100 ms: 10 waiting become 9), and never again.
    Dev d;
    std::vector<uint8_t> e;
    for (int i = 0; i < 20; ++i)
        note(e, 0, 69, 10000, 8000);
    CHECK_EQ(d.ask(SoundFw::MUSIC_THRESH, {10})[1], 0);
    CHECK_EQ(d.music(0, block(TONE, e, 20)), 0);
    auto st = d.status();
    REQUIRE(st.size() == 2 + 24u);
    CHECK_EQ(le16(st, 4), 19u);  // voice 0
    CHECK_EQ(le16(st, 24), 19u); // all
    d.ms(55);
    st = d.status();
    std::printf("  at 55 ms: %u waiting, %u ms played\n", le16(st, 24), le32(st, 20));
    CHECK_EQ(le16(st, 24), 14u);
    CHECK(d.of(SoundFw::EV_MUSIC_COUNT).empty());
    d.ms(50);
    const auto count = d.of(SoundFw::EV_MUSIC_COUNT);
    REQUIRE(count.size() == 1);
    CHECK_EQ(le16(count[0].body, 5), 9u);
    CHECK_EQ(count[0].body[7], 10);
    d.ms(200);
    CHECK_EQ(d.of(SoundFw::EV_MUSIC_COUNT).size(), 1u);
    CHECK_EQ(le16(d.status(), 24), 0u);
    CHECK_EQ(d.of(SoundFw::EV_MUSIC_DONE).size(), 1u);
    // Queued blocks count too: two of 20 queued behind one playing.
    std::vector<uint8_t> e2;
    for (int i = 0; i < 20; ++i)
        note(e2, 0, 69, 10000, 8000);
    CHECK_EQ(d.music(1, block(TONE, e2, 20)), 0);
    CHECK_EQ(d.music(2, block(TONE, e2, 20), 1), 0);
    st = d.status();
    CHECK_EQ(st[3], 1); // a block queued
    CHECK_EQ(le16(st, 24), 39u);
}

TEST(a_3600_s_note_is_one_entry)
{
    // SOUND's longest: one entry of 3,600 s (3,600,000,000 µs, 32 bits); 200 s on, it is still sounding.
    Dev d;
    std::vector<uint8_t> e;
    note(e, 0, 69, 3'600'000'000u, 3'600'000'000u);
    CHECK_EQ(d.music(0, block(TONE, e, 1)), 0);
    for (int i = 0; i < 200; ++i)
        d.ms(1000);
    CHECK(d.card.firmware().music_tone_on());
    CHECK_EQ(d.card.firmware().music_state(), SoundFw::M_PLAYING);
    CHECK(d.of(SoundFw::EV_MUSIC_DONE).empty());
    const auto st = d.status();
    CHECK_EQ(le16(st, 24), 0u); // the one note sounding isn't waiting
    CHECK(le32(st, 20) >= 199000u); // (the test's ms are 985 cycles of 985,248 Hz: 199.95 s)
}

TEST(music_is_refused_e_busy_when_a_tune_holds_its_chips)
{
    // A two-SID tune holds both chips: a block on a SID voice is refused; on the tone generator it plays. With a
    // one-SID tune, SID B's voices are free for it. And a two-SID tune can't start while music has SID B.
    auto tune = [](bool two) {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 3, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10, b[0x0C] = 0x10, b[0x0D] = 1, b[0x0F] = 1, b[0x11] = 1, b[0x77] = 0x14;
        if (two)
            b[0x7A] = 0x42;
        b.push_back(0x60);
        b.push_back(0x60);
        return b;
    };
    std::vector<uint8_t> e;
    note(e, 0, 60, 500000, 400000);
    for (const bool two : {true, false})
    {
        Dev d;
        const auto f = tune(two);
        CHECK_EQ(d.ask(SoundFw::TUNE_LOAD, {0, static_cast<uint8_t>(f.size()), 0, 0})[1], 0);
        for (const uint8_t b : f)
            d.card.write(SoundCard3::DATA, b);
        d.ms(1);
        CHECK_EQ(d.ask(SoundFw::TUNE_PLAY, {0, 0, 0, 0, 0, 0, 0, 0, 0})[1], 0);
        const uint8_t sid = d.music(0, block(SAW, e, 1));
        std::printf("  a %s tune: music on a SID voice %u\n", two ? "two-SID" : "one-SID", sid);
        CHECK_EQ(sid, two ? SoundFw::E_BUSY : 0);
        if (!two)
        {
            CHECK_EQ(d.card.firmware().voice_use(3), SoundFw::V_MUSIC); // SID B's first
            const auto f2 = tune(true);
            CHECK_EQ(d.ask(SoundFw::TUNE_LOAD, {1, static_cast<uint8_t>(f2.size()), 0, 0})[1], 0);
            for (const uint8_t b : f2)
                d.card.write(SoundCard3::DATA, b);
            d.ms(1);
            CHECK_EQ(d.ask(SoundFw::TUNE_PLAY, {1, 0, 0, 0, 0, 0, 0, 0, 0})[1], SoundFw::E_BUSY);
        }
        CHECK_EQ(d.music(1, block(TONE, e, 1)), 0); // the tone generator is no chip's
    }
}

TEST(a_block_that_doesn_t_parse_is_refused_at_xferdone)
{
    Dev d;
    std::vector<uint8_t> e;
    note(e, 0, 60, 1000, 2000); // a gate past its length
    CHECK_EQ(d.music(0, block(TONE, e, 1)), SoundFw::E_DATA);
    std::vector<uint8_t> e2;
    note(e2, 1, 60, 1000, 500); // voice 1, not in the mask
    CHECK_EQ(d.music(0, block(TONE, e2, 1)), SoundFw::E_DATA);
    std::vector<uint8_t> e3;
    note(e3, 0, 60, 1000, 500);
    CHECK_EQ(d.music(0, block(TONE, e3, 2)), SoundFw::E_DATA); // a count past its entries
    // A marker comes back when its voice reaches it.
    std::vector<uint8_t> e4;
    note(e4, 0, 60, 20000, 10000);
    e4.push_back(0x50), e4.push_back(7);
    note(e4, 0, 62, 20000, 10000);
    CHECK_EQ(d.music(0, block(TONE, e4, 3)), 0);
    d.ms(10);
    CHECK(d.of(SoundFw::EV_MUSIC_MARK).empty());
    d.ms(15);
    const auto mark = d.of(SoundFw::EV_MUSIC_MARK);
    REQUIRE(mark.size() == 1);
    CHECK_EQ(mark[0].body[0], 0); // voice 0
    CHECK_EQ(mark[0].body[5], 7); // the marker
}

TEST(a_tone_entry_above_20_khz_is_a_rest_and_keeps_time)
{
    // The tone generator's top is BEEP's, 20,000 Hz: a tone entry above it is a rest for its length. Three entries
    // of 10 ms (480 frames at 48 kHz), each gated its whole length: 20,000 Hz sounds from the block's start;
    // 20,001 Hz is silent, exact zeros at music's tap (nothing else of music's sounds); 1 kHz begins 960 frames on.
    // The same block with a rest for the second entry is the reference, frame for frame; a block that begins with
    // the 1 kHz is the anchor for the block's first frame.
    constexpr uint32_t MS10 = 10000;
    auto run = [](const std::vector<uint8_t> &e, uint32_t count) {
        Dev d;
        d.card.set_tap(true, SoundFw::SRC_MUSIC);
        CHECK_EQ(d.music(0, block(TONE, e, count)), 0);
        d.ms(40);
        return d.card.take_tap();
    };
    auto first_sound = [](const std::vector<int16_t> &t, size_t from) {
        for (size_t f = from; 2 * f + 1 < t.size(); ++f)
            if (t[2 * f] || t[2 * f + 1])
                return f;
        return t.size() / 2;
    };
    std::vector<uint8_t> a, b, c;
    tone(a, 0, 20000 * 16, MS10, MS10);
    tone(a, 0, 20001 * 16, MS10, MS10);
    tone(a, 0, 1000 * 16, MS10, MS10);
    tone(b, 0, 20000 * 16, MS10, MS10);
    rest(b, 0, MS10);
    tone(b, 0, 1000 * 16, MS10, MS10);
    tone(c, 0, 1000 * 16, MS10, MS10);
    const auto ta = run(a, 3), tb = run(b, 3), tc = run(c, 1);
    const size_t start = first_sound(tc, 0); // the anchor: the block's first frame that sounds
    REQUIRE(start + 1500 < ta.size() / 2);
    const size_t fa = first_sound(ta, 0);
    double e1 = 0;
    for (size_t f = start; f < start + 480; ++f)
        e1 += static_cast<double>(ta[2 * f]) * ta[2 * f];
    size_t loud = 0; // frames of the 20,001 Hz entry that aren't exact zero
    for (size_t f = start - 1 + 480; f < start - 1 + 960; ++f)
        loud += ta[2 * f] || ta[2 * f + 1];
    const size_t next_a = first_sound(ta, start + 480), next_b = first_sound(tb, start + 480);
    std::printf("  block's first sounding frame %zu; 20,000 Hz from %zu, rms %.0f; 20,001 Hz: %zu frames not zero; "
                "1 kHz from %zu (the reference %zu, %zu after the start)\n",
                start, fa, std::sqrt(e1 / 480), loud, next_a, next_b, next_a - start);
    CHECK_EQ(fa, start);      // 20,000 Hz sounds, from the block's start
    CHECK(e1 / 480 > 1e6);    // well above silence (rms > 1,000 of 32,767)
    CHECK_EQ(loud, 0u);       // 20,001 Hz is silent for its length
    CHECK_EQ(next_a, next_b); // 1 kHz on time, to the frame
    CHECK(next_a >= start - 1 + 960 && next_a <= start + 960);
    CHECK(ta == tb);          // and the whole block is the rest's, frame for frame
}

int main() { return run_all(); }
