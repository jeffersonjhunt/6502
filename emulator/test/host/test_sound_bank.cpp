/**
 * @file test_sound_bank.cpp
 * @brief The sound card, revision 3: the sound bank (samples, SID and sample effects, the voice manager, wave RAM
 * as the store's cache; hw/future/sound-device.md, "The sound bank" and "How the firmware shares the chips").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "harness.h"

#include "sound_card3.h"

#include <algorithm>
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

    /// The card at PAL's SID clock as the host's, up.
    struct Dev
    {
        SoundCard3 card;
        std::vector<Event> seen;
        Dev()
        {
            card.set_cpu_hz(SoundCard::PAL_HZ);
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
        /// A command's reply: its body (tag, status, results).
        std::vector<uint8_t> ask(uint8_t cmd, const std::vector<uint8_t> &p = {})
        {
            const size_t from = seen.size();
            send(cmd, 0x42, p);
            card.tick(300);
            drain();
            for (size_t i = from; i < seen.size(); ++i)
                if (seen[i].kind == SoundFw::EV_REPLY && seen[i].body[0] == 0x42)
                    return seen[i].body;
            return {};
        }
        /// Bytes into an open transfer, then its check.
        void data(const std::vector<uint8_t> &b)
        {
            for (const uint8_t x : b)
                card.write(SoundCard3::DATA, x);
            card.tick(300);
            drain();
        }
        std::vector<Event> of(uint8_t kind) const
        {
            std::vector<Event> out;
            for (const Event &e : seen)
                if (e.kind == kind)
                    out.push_back(e);
            return out;
        }
        /// A sample into `slot`: unsigned, `rate`, no loop.
        void sample(uint8_t slot, const std::vector<uint8_t> &b, uint16_t rate)
        {
            const uint32_t n = static_cast<uint32_t>(b.size());
            CHECK_EQ(ask(SoundFw::SAMPLE_LOAD, {slot, static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n >> 16),
                                                0, static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), 0, 0, 0, 0, 0, 0})[1],
                     0);
            data(b);
        }
        /// PLAY: @return the reply's body
        std::vector<uint8_t> play(uint8_t effect, uint8_t prio = 1, uint8_t chip = 0, uint8_t vol = 255)
        {
            return ask(SoundFw::PLAY, {effect, vol, 128, prio, 0, 0, chip});
        }
    };

    /// A SID effect: one step of `ms`, a sawtooth at `hz16` (Hz x 16), sustain full.
    std::vector<uint8_t> sid_effect(uint8_t effect, uint16_t ms, uint32_t hz16)
    {
        return {effect, 0, 1, 0xFF, static_cast<uint8_t>(ms), static_cast<uint8_t>(ms >> 8), 0x21, static_cast<uint8_t>(hz16),
                static_cast<uint8_t>(hz16 >> 8), static_cast<uint8_t>(hz16 >> 16), 0, 8, 0x00, 0xF0, 0, 0, 0};
    }
    /// A sample effect on `slot`, at its own rate, one-shot, no envelope.
    std::vector<uint8_t> sample_effect(uint8_t effect, uint8_t slot)
    {
        return {effect, 1, slot, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    }
    /// A sine as unsigned bytes ($00 left out: the ES5503's stop), `n` of them, `per` a cycle.
    std::vector<uint8_t> sine(size_t n, double per)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(std::clamp(128.0 + 100.0 * std::sin(6.283185307 * static_cast<double>(i) / per), 1.0, 255.0));
        return b;
    }
    /// A PSID whose init holds voice 1 on a sawtooth; play ($1015) INC $D401 each frame.
    std::vector<uint8_t> tune()
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 2, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10, b[0x0C] = 0x10, b[0x0D] = 21, b[0x0F] = 1, b[0x11] = 1, b[0x77] = 0x14;
        const std::vector<uint8_t> code = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4, 0xA9, 0xF0, 0x8D, 0x06, 0xD4,
                                           0xA9, 0x21, 0x8D, 0x04, 0xD4, 0x60, 0xEE, 0x01, 0xD4, 0x60};
        b.insert(b.end(), code.begin(), code.end());
        return b;
    }
} // namespace

TEST(a_tune_s_writes_are_the_same_with_effects_on_sid_b_and_the_es5503)
{
    // A one-SID tune on SID A, alone and with SID effects (which can only go to SID B) and samples on the ES5503
    // playing beside it: SID A's registers and its own output (the ADC's samples) the same.
    std::vector<float> a_alone, a_shared;
    std::vector<uint8_t> regs_alone, regs_shared;
    for (const bool shared : {false, true})
    {
        Dev d;
        d.card.set_adc_copy(true);
        const auto file = tune();
        CHECK_EQ(d.ask(SoundFw::TUNE_LOAD, {0, static_cast<uint8_t>(file.size()), static_cast<uint8_t>(file.size() >> 8), 0})[1], 0);
        d.data(file);
        CHECK_EQ(d.ask(SoundFw::EFFECT_DEF, sid_effect(1, 400, 440 * 16))[1], 0); // defined both ways: only played shared
        d.sample(3, sine(8000, 40), 22050);
        CHECK_EQ(d.ask(SoundFw::EFFECT_DEF, sample_effect(2, 3))[1], 0);
        CHECK_EQ(d.ask(SoundFw::TUNE_PLAY, {0, 0, 0, 0, 0, 0, 0, 0, 0})[1], 0);
        d.card.take_adc_copy();
        std::vector<float> &a = shared ? a_shared : a_alone;
        std::vector<uint8_t> &regs = shared ? regs_shared : regs_alone;
        for (int i = 0; i < 10; ++i)
        {
            if (i % 3 == 0) // alone, NOPs in their place: the same card time each way
            {
                CHECK_EQ(shared ? d.play(1)[1] : d.ask(SoundFw::NOP)[1], 0); // a SID voice: SID B's
                CHECK_EQ(shared ? d.play(2)[1] : d.ask(SoundFw::NOP)[1], 0); // the ES5503
            }
            d.ms(100);
            const auto c = d.card.take_adc_copy();
            for (size_t k = 0; k < c.size(); k += 2)
                a.push_back(c[k]);
            for (unsigned r = 0; r < 25; ++r)
                regs.push_back(d.card.chips().sid(0)[r]);
        }
        if (shared)
        {
            for (unsigned v = 0; v < 3; ++v)
                CHECK(d.card.firmware().voice_use(v) == SoundFw::V_FREE); // none on the tune's chip
            unsigned b_used = 0;
            for (unsigned r = 0; r < 25; ++r)
                b_used += d.card.chips().sid(1)[r] != 0;
            CHECK(b_used > 0); // SID B had the effects
            CHECK(!d.of(SoundFw::EV_VOICE_DONE).empty());
        }
    }
    std::printf("  %zu samples of SID A each way\n", a_alone.size());
    CHECK(!a_alone.empty());
    CHECK(a_alone == a_shared);
    CHECK(regs_alone == regs_shared);
}

TEST(stealing_takes_the_lowest_priority_oldest_voice)
{
    // No tune: the six SID voices full, of priorities 1, 0, 2, 0, 1, 3 in that order; a seventh of priority 1
    // takes the oldest of the lowest (the second); one of priority 0 then takes the other 0. With every voice
    // above it, one of priority 0 finds none: E_BUSY.
    Dev d;
    CHECK_EQ(d.ask(SoundFw::EFFECT_DEF, sid_effect(1, 5000, 440 * 16))[1], 0);
    std::vector<uint8_t> h;
    for (const int prio : {1, 0, 2, 0, 1, 3})
    {
        const auto r = d.play(1, static_cast<uint8_t>(prio), 1);
        REQUIRE(r.size() == 3u && r[1] == 0);
        h.push_back(r[2]);
        d.ms(1);
    }
    const auto r7 = d.play(1, 1, 1);
    REQUIRE(r7.size() == 3u);
    auto stolen = d.of(SoundFw::EV_VOICE_STOLEN);
    REQUIRE(stolen.size() == 1);
    CHECK_EQ(stolen[0].body[0], h[1]);
    CHECK_EQ(stolen[0].body[6], 1); // by a priority 1
    d.play(1, 0, 1);
    stolen = d.of(SoundFw::EV_VOICE_STOLEN);
    REQUIRE(stolen.size() == 2);
    CHECK_EQ(stolen[1].body[0], h[3]);
    // VOICE_STOP of one handle frees its voice; of 0, every effect voice.
    CHECK_EQ(d.ask(SoundFw::VOICE_STOP, {h[0]})[1], 0);
    unsigned free = 0;
    for (unsigned v = 0; v < SoundFw::SID_VOICES; ++v)
        free += d.card.firmware().voice_use(v) == SoundFw::V_FREE;
    CHECK_EQ(free, 1u);
    CHECK_EQ(d.ask(SoundFw::VOICE_STOP, {0})[1], 0);
    for (unsigned v = 0; v < SoundFw::SID_VOICES; ++v)
        CHECK(d.card.firmware().voice_use(v) == SoundFw::V_FREE);
    for (int i = 0; i < 6; ++i)
        CHECK_EQ(d.play(1, 2, 1)[1], 0);
    CHECK_EQ(d.play(1, 1, 1)[1], SoundFw::E_BUSY);
}

TEST(a_bank_bigger_than_wave_ram_plays_each_sample)
{
    // 20 samples of 15 KB, 300 KB, into the store; wave RAM (128 KB) a cache of it: each played in turn plays to
    // its end (VOICE_DONE) and is heard.
    Dev d;
    for (uint8_t s = 0; s < 20; ++s)
    {
        d.sample(s, sine(15000, 20 + s), 26000);
        CHECK_EQ(d.ask(SoundFw::EFFECT_DEF, sample_effect(s, s))[1], 0);
    }
    std::printf("  store free %zu of %zu\n", d.card.firmware().store_free(), d.card.firmware().store_bytes());
    unsigned heard = 0, done = 0, most = 0;
    for (uint8_t s = 0; s < 20; ++s)
    {
        d.card.set_tap(true, SoundFw::SRC_EFFECTS);
        const size_t before = d.of(SoundFw::EV_VOICE_DONE).size();
        const auto r = d.play(s);
        REQUIRE(r.size() == 3u && r[1] == 0);
        d.ms(700); // 15,000 at 26 kHz: 0.58 s
        done += d.of(SoundFw::EV_VOICE_DONE).size() > before;
        const auto tap = d.card.take_tap();
        double sq = 0;
        for (size_t i = 0; i < tap.size(); i += 2)
            sq += static_cast<double>(tap[i]) * tap[i];
        heard += std::sqrt(sq / static_cast<double>(tap.size() / 2)) > 200;
        most = std::max(most, d.card.firmware().cached_samples());
    }
    std::printf("  %u heard, %u done; at most %u samples in wave RAM at once\n", heard, done, most);
    CHECK_EQ(heard, 20u);
    CHECK_EQ(done, 20u);
    CHECK(most <= 8u); // 16 KB tables: 8 fit
    // A bad definition is refused at EFFECT_DEF.
    CHECK_EQ(d.ask(SoundFw::EFFECT_DEF, {40, 0, 0, 0xFF})[1], SoundFw::E_PARAM);
}

int main() { return run_all(); }
