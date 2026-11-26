/**
 * @file test_sound_stream3.cpp
 * @brief The sound card, revision 3: streams (rings in the store, NEED, prebuffer, underruns, gapless joins and
 * the MP3 trim; hw/future/sound-device.md, "Gapless streaming").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card (sound_card3.h) on its own, ticked at the native clock; a feeder answers each NEED with a chunk
 * by STREAM_DATA and DATA, as a host would. The tap is the firmware's stream tap: each frame as it played, at
 * the stream's rate. The MP3s are the repo's own (test/host/mp3, made by LAME: their Info frame's LAME tag
 * carries the encoder's delay and padding). Then the decoder's robustness: 64 KB with no frame, hostile and
 * empty frames cheaply refused, free format, a 100 KB tag, rate and channels changing mid-stream, and damaged
 * frames, each as minimp3 decodes the whole data, the end given with the last byte or late.
 */

#include "harness.h"

#include "minimp3.h"
#include "sound_card3.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t HZ = SoundCard3::NATIVE_HZ;

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

    std::vector<uint8_t> noise(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> b(n);
        for (auto &x : b)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            x = static_cast<uint8_t>(seed >> 8);
        }
        return b;
    }

    /// The card, up; each stream's data fed on its NEEDs from `data` (a chunk at most each), `hold` saying when
    /// the feeder holds back.
    struct Card
    {
        SoundCard3 card{128};
        std::vector<Event> seen;         ///< every event, as read
        std::vector<uint8_t> statuses;   ///< every reply's status
        std::vector<uint8_t> data[2];
        size_t sent[2]{0, 0};
        bool fed_end[2]{false, false};
        bool auto_end{true};
        std::function<bool(unsigned)> hold = [](unsigned) { return false; };
        unsigned needs[2]{0, 0};
        uint32_t least_room[2]{0xFFFFFFFFu, 0xFFFFFFFFu}; ///< the least room a NEED said
        std::vector<std::pair<uint32_t, uint8_t>> urgency;  ///< each NEED's ms buffered and its urgent flag
        unsigned need_waiting[2]{0, 0}; ///< NEEDs not yet answered: a chunk each, as a host answers them
        uint32_t room[2]{0, 0};

        explicit Card(uint32_t audio_rate = 0)
        {
            card.set_cpu_hz(HZ);
            card.set_adc_rate(0); // no tune here: the SIDs needn't be made
            card.set_audio_rate(audio_rate);
            run_ms(SoundFw::STARTUP_MS + 1);
            card.firmware().set_stream_tap(true);
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
                if (e.kind == SoundFw::EV_REPLY)
                    statuses.push_back(e.body[1]);
                if (e.kind == SoundFw::EV_NEED)
                {
                    ++needs[e.body[0]];
                    ++need_waiting[e.body[0]];
                    room[e.body[0]] = le32(e.body, 5);
                    least_room[e.body[0]] = std::min(least_room[e.body[0]], room[e.body[0]]);
                    urgency.push_back({le32(e.body, 9), e.body[13]});
                }
                seen.push_back(e);
            }
        }
        /// The feeder: a chunk for each NEED (one at a time, as a host waits its transfer), then END.
        void feed()
        {
            for (unsigned s = 0; s < 2; ++s)
            {
                if (!need_waiting[s] || hold(s))
                    continue;
                --need_waiting[s];
                const size_t n = std::min<size_t>(data[s].size() - sent[s], 32 * 1024); // a whole chunk, as a host sends
                if (n)
                {
                    send(SoundFw::STREAM_DATA, 0x50, {static_cast<uint8_t>(s), static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8),
                                                      static_cast<uint8_t>(n >> 16)});
                    wait_xfer();
                    for (size_t i = 0; i < n; ++i)
                        card.write(SoundCard3::DATA, data[s][sent[s] + i]);
                    sent[s] += n;
                }
                if (sent[s] == data[s].size() && !fed_end[s] && auto_end)
                {
                    send(SoundFw::STREAM_END, 0x51, {static_cast<uint8_t>(s)});
                    fed_end[s] = true;
                }
            }
        }
        void run(uint64_t cycles) { card.tick(cycles); }
        /// Until the transfer a command opens is open (as a host waits for it before DATA), a millisecond at most.
        void wait_xfer()
        {
            for (int i = 0; i < 7200 && !(card.peek(SoundCard3::STATUS) & SoundCard3::STATUS_XFER); ++i)
                card.tick(1);
        }
        void run_ms(unsigned ms)
        {
            for (unsigned i = 0; i < ms; ++i)
            {
                card.tick(HZ / 1000);
                drain();
                feed();
            }
        }
        void open(uint8_t s, uint8_t codec, uint32_t rate, uint8_t fmt)
        {
            send(SoundFw::STREAM_OPEN, 0x40, {s, codec, static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), fmt, 0, 0, 0, 0, 0});
            run(200);
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
        std::vector<uint8_t> tap_bytes()
        {
            std::vector<uint8_t> out;
            for (const int16_t v : card.firmware().take_stream_tap())
                out.push_back(static_cast<uint8_t>(v)), out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
            return out;
        }
        bool all_ok() const
        {
            return std::all_of(statuses.begin(), statuses.end(), [](uint8_t s) { return s == 0; });
        }
    };

    std::vector<uint8_t> mp3_file(const std::string &name)
    {
        std::ifstream in(std::string(EMU_MP3_DIR) + "/" + name, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    /// minimp3 on its own, the whole file, then trimmed as the LAME tag says: the Info frame left out, the
    /// encoder's delay and the decoder's 529 off the front, the padding less 529 off the end.
    std::vector<int16_t> trimmed(const std::vector<uint8_t> &mp3)
    {
        const char tag[] = "LAME";
        const auto lame = std::search(mp3.begin(), mp3.end(), tag, tag + 4);
        CHECK(lame != mp3.end());
        if (lame == mp3.end())
            return {};
        const size_t t = static_cast<size_t>(lame - mp3.begin()) + 21;
        const uint32_t delay = static_cast<uint32_t>(mp3[t] << 4 | mp3[t + 1] >> 4);
        const uint32_t padding = static_cast<uint32_t>((mp3[t + 1] & 0x0F) << 8 | mp3[t + 2]);
        std::vector<int16_t> out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        size_t pos = 0;
        bool first = true;
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            pos += static_cast<size_t>(info.frame_bytes);
            if (first)
            {
                first = false; // the Info frame
                continue;
            }
            for (int i = 0; i < n; ++i)
            {
                const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                out.push_back(l);
                out.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
            }
        }
        const size_t head = 2 * (delay + 529), tail = 2 * (padding - 529);
        return std::vector<int16_t>(out.begin() + static_cast<std::ptrdiff_t>(head), out.end() - static_cast<std::ptrdiff_t>(tail));
    }
} // namespace

TEST(a_stream_bigger_than_the_store_plays_with_no_underrun)
{
    // 60 s of 44.1 kHz 16-bit stereo, 10.6 MB, on an 8 MB card, fed on NEED: no underrun, and the tap is the file.
    Card c;
    c.data[0] = noise(44100u * 4u * 60u, 7);
    c.open(0, 0, 44100, 3);
    std::printf("  a ring of %zu bytes\n", c.card.firmware().stream_ring(0));
    CHECK(c.card.firmware().stream_ring(0) >= (1u << 20));
    c.run_ms(61000);
    const std::vector<uint8_t> played = c.tap_bytes();
    std::printf("  %zu bytes sent, %zu played, %u NEEDs, %zu underruns\n", c.sent[0], played.size(), c.needs[0],
                c.of(SoundFw::EV_UNDERRUN).size());
    for (size_t i = 0; i < c.statuses.size(); ++i)
        if (c.statuses[i])
            std::printf("  reply %zu: status %u\n", i, c.statuses[i]);
    CHECK(c.all_ok());
    CHECK(c.of(SoundFw::EV_UNDERRUN).empty());
    CHECK(played == c.data[0]);
    CHECK_EQ(c.card.firmware().stream_state(0), SoundFw::S_ENDED);
    // Every NEED had room for a whole chunk, and said urgent exactly when under 1 s was buffered.
    std::printf("  the least room a NEED said: %u\n", c.least_room[0]);
    CHECK(c.least_room[0] >= 32 * 1024);
    unsigned urgent = 0, calm = 0;
    for (const auto &[ms, flag] : c.urgency)
    {
        CHECK_EQ(flag, ms < 1000 ? 1 : 0);
        (flag ? urgent : calm) += 1;
    }
    std::printf("  NEEDs: %u urgent, %u not\n", urgent, calm);
    CHECK(urgent > 0 && calm > 0);
    CHECK_EQ(c.of(SoundFw::EV_STREAM_ENDED).size(), 1u);
    // Every NEED was answered before the next came: never two outstanding.
    CHECK(c.needs[0] >= c.data[0].size() / (32 * 1024));
}

TEST(two_streams_queued_are_one_stream_at_the_tap)
{
    // Raw PCM, frame for frame: stream 0's data then stream 1's, with no gap, a TRACK when 1 starts.
    Card c;
    c.data[0] = noise(22050u * 4u, 11);
    c.data[1] = noise(22050u * 4u + 400u, 12);
    c.open(0, 0, 22050, 3);
    c.open(1, 0, 22050, 3);
    c.send(SoundFw::STREAM_NEXT, 0x41, {0, 1});
    c.run_ms(2600);
    std::vector<uint8_t> want = c.data[0];
    want.insert(want.end(), c.data[1].begin(), c.data[1].end());
    const std::vector<uint8_t> played = c.tap_bytes();
    std::printf("  %zu + %zu bytes, %zu played; TRACK %zu\n", c.data[0].size(), c.data[1].size(), played.size(),
                c.of(SoundFw::EV_TRACK).size());
    CHECK(c.all_ok());
    CHECK(played == want);
    const auto track = c.of(SoundFw::EV_TRACK);
    REQUIRE(track.size() == 1);
    CHECK_EQ(track[0].body[0], 1); // stream 1 playing now
    CHECK_EQ(track[0].body[5], 0); // stream 0 ended, closed
    CHECK_EQ(c.card.firmware().stream_state(0), SoundFw::S_IDLE);
    CHECK_EQ(c.card.firmware().stream_state(1), SoundFw::S_ENDED);
    CHECK(c.of(SoundFw::EV_UNDERRUN).empty());
}

TEST(an_mp3_pair_with_gapless_headers_is_trimmed)
{
    // Two of LAME's 44.1 kHz files (their Info frames' LAME tags: a delay of 576, padding 612), queued: the tap
    // has each one's frames trimmed of the delay and padding, the second's following the first's with no gap:
    // 0.6 s each, 26,460 frames.
    Card c;
    c.data[0] = mp3_file("cbr128_joint_44k.mp3");
    c.data[1] = mp3_file("vbr0_stereo_44k.mp3");
    REQUIRE(!c.data[0].empty() && !c.data[1].empty());
    c.open(0, 1, 0, 0);
    c.open(1, 1, 0, 0);
    c.send(SoundFw::STREAM_NEXT, 0x41, {0, 1});
    c.run_ms(2000);
    std::vector<int16_t> want = trimmed(c.data[0]);
    const std::vector<int16_t> second = trimmed(c.data[1]);
    CHECK_EQ(want.size(), 2 * 26460u);
    CHECK_EQ(second.size(), 2 * 26460u);
    want.insert(want.end(), second.begin(), second.end());
    const std::vector<int16_t> got = c.card.firmware().take_stream_tap();
    std::printf("  %zu frames played, %zu wanted\n", got.size() / 2, want.size() / 2);
    CHECK(c.all_ok());
    CHECK(got == want);
    CHECK_EQ(c.of(SoundFw::EV_TRACK).size(), 1u);
}

TEST(underruns_counted_as_status_says_and_sound_back_at_the_next_frame_due)
{
    // The feeder holds back after 4 chunks (128 KB: 0.74 s at 44.1 kHz stereo; the prebuffer is 0.5 s) for
    // 1.2 s, so the ring runs dry; then it gives one chunk (0.19 s, less than a prebuffer) and holds 150 ms
    // more, then feeds on: the sound is back with that one chunk.
    Card c;
    c.data[0] = noise(44100u * 4u * 2u, 21);
    unsigned chunks = 0;
    uint64_t held_until = 0, released = 0;
    c.hold = [&](unsigned) {
        const uint64_t now = c.card.firmware().ms();
        if (chunks == 4 && !held_until)
            held_until = now + 1200;
        if (held_until && now < held_until)
            return true;
        if (held_until && !released)
            released = now;
        if (chunks == 5 && now < released + 150)
            return true;
        ++chunks;
        return false;
    };
    c.open(0, 0, 44100, 3);
    c.run_ms(3500);
    const auto under = c.of(SoundFw::EV_UNDERRUN), back = c.of(SoundFw::EV_UNDERRUN_END);
    std::printf("  %zu underruns, %zu back\n", under.size(), back.size());
    REQUIRE(under.size() == 1 && back.size() == 1);
    // Back at the next frame due: no new prebuffer, so the sound returns within a millisecond of the data.
    const uint32_t gone_at = le32(under[0].body, 1), back_at = le32(back[0].body, 1);
    std::printf("  silent from card ms %u to %u; %u frames missed\n", gone_at, back_at, le32(back[0].body, 9));
    std::printf("  the feeder let go at card ms %llu\n", static_cast<unsigned long long>(released));
    CHECK(gone_at < released);
    CHECK(back_at >= released && back_at <= released + 3); // a prebuffer would make it 500 ms
    c.send(SoundFw::STREAM_STATUS, 0x52, {0});
    c.run(200);
    c.drain();
    const Event &st = c.seen.back();
    REQUIRE(st.kind == SoundFw::EV_REPLY && st.body.size() == 2 + 21u);
    CHECK_EQ(le16(st.body, 2 + 19), 1u); // the underruns STATUS counts
    CHECK_EQ(le32(back[0].body, 5), le32(under[0].body, 5)); // the frame position held through the silence
    const std::vector<uint8_t> played = c.tap_bytes();
    CHECK(played == c.data[0]); // every byte played once, none skipped
}

TEST(the_same_at_rate_0_and_48000)
{
    // A raw stream and an MP3 together: the card's output tap alike with the host's audio rate 0, 48,000 and
    // 22,050, and however it is ticked.
    auto scene = [](uint32_t rate, uint64_t step) {
        Card c(rate);
        c.data[0] = noise(22050u * 2u, 31);
        c.data[1] = mp3_file("cbr32_mono_22k.mp3");
        c.card.set_tap(true);
        c.open(0, 0, 22050, 1);
        c.open(1, 1, 0, 0);
        for (unsigned s = 0; s < 2; ++s) // all of it at once, at a card time every scene shares
        {
            const size_t n = c.data[s].size();
            c.send(SoundFw::STREAM_DATA, 0x50, {static_cast<uint8_t>(s), static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8),
                                                static_cast<uint8_t>(n >> 16)});
            c.run(200);
            for (const uint8_t b : c.data[s])
                c.card.write(SoundCard3::DATA, b);
            c.send(SoundFw::STREAM_END, 0x51, {static_cast<uint8_t>(s)});
        }
        std::vector<float> host(2 * 4096);
        for (uint64_t t = 0; t < HZ; t += step)
        {
            c.card.tick(step);
            while (c.card.take_audio(host.data(), 4096) > 0)
            {
            }
            c.drain();
        }
        return c.card.take_tap();
    };
    const auto a = scene(0, 7159), b = scene(48000, 7157), c = scene(22050, 1009);
    const size_t n = std::min({a.size(), b.size(), c.size()});
    std::printf("  %zu, %zu, %zu frames\n", a.size() / 2, b.size() / 2, c.size() / 2);
    CHECK(n > 2 * 47000u);
    CHECK(std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), b.begin()));
    CHECK(std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), c.begin()));
}

namespace
{
    /// STREAM_DATA of `len` for stream `s`, and `bytes` of it through DATA (the transfer then left open or done).
    void chunk(Card &c, uint8_t s, uint32_t len, const uint8_t *bytes, size_t n)
    {
        c.send(SoundFw::STREAM_DATA, 0x60, {s, static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len >> 16)});
        c.wait_xfer();
        for (size_t i = 0; i < n; ++i)
            c.card.write(SoundCard3::DATA, bytes[i]);
    }
    /// The reply with `tag`'s body (tag, status, results), waited for (a millisecond at most).
    std::vector<uint8_t> reply_for(Card &c, uint8_t tag)
    {
        for (int i = 0; i < 720; ++i)
        {
            c.drain();
            for (auto it = c.seen.rbegin(); it != c.seen.rend(); ++it)
                if (it->kind == SoundFw::EV_REPLY && it->body.at(0) == tag)
                    return it->body;
            c.run(10);
        }
        return {};
    }
    std::vector<int16_t> mp3_pcm(const std::vector<uint8_t> &mp3, size_t from)
    {
        std::vector<int16_t> out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        for (size_t pos = from; pos < mp3.size();)
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            pos += static_cast<size_t>(info.frame_bytes);
            for (int i = 0; i < n; ++i)
            {
                const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                out.push_back(l);
                out.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
            }
        }
        return out;
    }
} // namespace

TEST(an_aborted_chunk_drops_what_hasn_t_played_and_says_what_it_kept)
{
    // (a) Not yet playing (buffering): 1,001 bytes of a 32 KB chunk come, then XFERABORT: none kept (the count 0),
    // and the chunk sent again whole plays with no fragment of the first.
    {
        Card c;
    c.auto_end = false; // fed by hand
        c.auto_end = false; // fed by hand
        const std::vector<uint8_t> data = noise(32768, 41);
        c.open(0, 0, 44100, 3); // 16-bit stereo
        chunk(c, 0, 32768, data.data(), 1001);
        c.send(SoundFw::XFERABORT, 0x61);
        const auto r = reply_for(c, 0x61);
        REQUIRE(r.size() == 5u);
        CHECK_EQ(r[2] | r[3] << 8 | r[4] << 16, 0);
        chunk(c, 0, 32768, data.data(), data.size());
        c.send(SoundFw::STREAM_END, 0x62, {0});
        c.run_ms(400);
        CHECK(c.tap_bytes() == data);
    }
    // (b) Playing into the chunk (48 kHz 8-bit mono, run dry): 100 of 1,000 bytes come and play, then XFERABORT:
    // the count is what played and stays; the buffered figures sane; the rest resent from there plays on.
    {
        Card c;
    c.auto_end = false; // fed by hand
        c.auto_end = false; // fed by hand
        const std::vector<uint8_t> first = noise(4800, 42), second = noise(1000, 43);
        c.send(SoundFw::STREAM_OPEN, 0x40, {0, 0, 0x80, 0xBB, 0, 0, 0x10, 0, 10, 0}); // 4 KB chunks, a 10 ms prebuffer
        c.run(300);
        chunk(c, 0, 4800, first.data(), first.size());
        c.run_ms(150); // its 100 ms played: dry
        chunk(c, 0, 1000, second.data(), 100);
        c.run_ms(3);
        c.send(SoundFw::XFERABORT, 0x61);
        const auto r = reply_for(c, 0x61);
        REQUIRE(r.size() == 5u);
        const uint32_t kept = static_cast<uint32_t>(r[2] | r[3] << 8 | r[4] << 16);
        c.send(SoundFw::STREAM_STATUS, 0x63, {0});
        const auto st = reply_for(c, 0x63);
        REQUIRE(st.size() == 2 + 21u);
        std::printf("  kept %u of 100; buffered %u bytes, %u ms\n", kept, le32(st, 2 + 5), le32(st, 2 + 9));
        CHECK(kept > 0 && kept <= 100);
        CHECK(le32(st, 2 + 5) <= 100);
        CHECK(le32(st, 2 + 9) < 10);
        chunk(c, 0, 1000 - kept, second.data() + kept, 1000 - kept);
        c.send(SoundFw::STREAM_END, 0x62, {0});
        c.run_ms(100);
        std::vector<uint8_t> want = first;
        want.insert(want.end(), second.begin(), second.end());
        std::vector<uint8_t> got; // the tap's left channel, high bytes: the 8-bit samples back (unsigned)
        const auto tap = c.card.firmware().take_stream_tap();
        for (size_t i = 0; i < tap.size(); i += 2)
            got.push_back(static_cast<uint8_t>((tap[i] >> 8) + 128));
        CHECK(got == want);
        CHECK_EQ(c.card.firmware().stream_state(0), SoundFw::S_ENDED);
    }
}

TEST(stream_end_waits_for_the_chunk_coming)
{
    // STREAM_END right after a chunk's STREAM_DATA, 300 of its 32 KB in: the stream ends only once the chunk has
    // come and played; every byte of it plays.
    Card c;
    c.auto_end = false; // fed by hand
    const std::vector<uint8_t> data = noise(32768, 44);
    c.send(SoundFw::STREAM_OPEN, 0x40, {0, 0, 0x80, 0xBB, 0, 0, 0, 0, 0, 0}); // 48 kHz 8-bit mono
    c.run(300);
    chunk(c, 0, 32768, data.data(), 300);
    c.send(SoundFw::STREAM_END, 0x62, {0});
    c.run_ms(50);
    CHECK(c.of(SoundFw::EV_STREAM_ENDED).empty());
    for (size_t i = 300; i < data.size(); ++i)
        c.card.write(SoundCard3::DATA, data[i]);
    c.run_ms(800);
    CHECK_EQ(c.of(SoundFw::EV_STREAM_ENDED).size(), 1u);
    std::vector<uint8_t> got;
    const auto tap = c.card.firmware().take_stream_tap();
    for (size_t i = 0; i < tap.size(); i += 2)
        got.push_back(static_cast<uint8_t>((tap[i] >> 8) + 128));
    CHECK(got == data);
}

TEST(each_underrun_end_counts_its_own_frames)
{
    // 48 kHz 8-bit mono, 100 ms at a time: dry for 100 ms, then for 50 ms. Each UNDERRUN_END's frames missed are
    // its own underrun's: about 4,800, then about 2,400.
    Card c;
    c.auto_end = false; // fed by hand
    c.send(SoundFw::STREAM_OPEN, 0x40, {0, 0, 0x80, 0xBB, 0, 0, 0x10, 0, 10, 0});
    c.run(300);
    const std::vector<uint8_t> d = noise(4800, 45);
    for (const unsigned dry : {100u, 50u, 0u})
    {
        chunk(c, 0, 4800, d.data(), d.size());
        c.run_ms(100 + dry);
    }
    const auto back = c.of(SoundFw::EV_UNDERRUN_END);
    std::printf("  %zu underruns, %zu back, state %u, statuses:", c.of(SoundFw::EV_UNDERRUN).size(), back.size(),
                c.card.firmware().stream_state(0));
    for (const uint8_t s : c.statuses)
        std::printf(" %u", s);
    std::printf("\n");
    REQUIRE(back.size() == 2);
    const uint32_t one = le32(back[0].body, 9), two = le32(back[1].body, 9);
    std::printf("  missed %u, then %u\n", one, two);
    CHECK(one > 4300 && one < 5300);
    CHECK(two > 1900 && two < 2900);
}

TEST(a_seek_keeps_the_mp3_s_gapless_trim)
{
    // LAME's 44.1 kHz file: 12 KB of it played, then STREAM_FLUSH (a seek) and the file again from its second frame
    // (the first after the Info frame), queued before another: after the seek, its frames end trimmed of the
    // padding (as the header said at the start), and the next follows.
    Card c;
    c.auto_end = false; // fed by hand
    const std::vector<uint8_t> a = mp3_file("cbr128_joint_44k.mp3"), b = mp3_file("vbr0_stereo_44k.mp3");
    REQUIRE(!a.empty() && !b.empty());
    c.open(0, 1, 0, 0);
    c.open(1, 1, 0, 0);
    c.send(SoundFw::STREAM_NEXT, 0x41, {0, 1});
    chunk(c, 0, 12288, a.data(), 12288); // 768 ms: past the prebuffer, so it plays and its header is read
    c.run_ms(700);
    c.send(SoundFw::STREAM_FLUSH, 0x64, {0});
    c.run(300);
    c.card.firmware().take_stream_tap(); // before the seek
    // The second frame's place: the Info frame's length, as minimp3 finds it.
    size_t second = 0;
    {
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        mp3dec_frame_info_t info{};
        mp3dec_decode_frame(dec.get(), a.data(), static_cast<int>(a.size()), pcm.data(), &info);
        second = static_cast<size_t>(info.frame_bytes);
    }
    REQUIRE(second > 0);
    chunk(c, 0, static_cast<uint32_t>(a.size() - second), a.data() + second, a.size() - second);
    c.send(SoundFw::STREAM_END, 0x62, {0});
    chunk(c, 1, static_cast<uint32_t>(b.size()), b.data(), b.size());
    c.send(SoundFw::STREAM_END, 0x62, {1});
    c.run_ms(2000);
    std::vector<int16_t> want = mp3_pcm(a, second);
    want.resize(want.size() - 2 * (612 - 529)); // the padding less the decoder's 529 (the header's: 612)
    const std::vector<int16_t> rest = trimmed(b);
    want.insert(want.end(), rest.begin(), rest.end());
    const std::vector<int16_t> got = c.card.firmware().take_stream_tap();
    std::printf("  %zu frames after the seek, %zu wanted\n", got.size() / 2, want.size() / 2);
    CHECK(got == want);
}

TEST(a_join_s_output_is_one_stream_s)
{
    // Two queued 44.1 kHz streams against the same data as one: the card's output (resampled to 48 kHz) the same
    // sample for sample across the join.
    const std::vector<uint8_t> a = noise(44100u * 2u, 51), b = noise(44100u * 2u, 52); // 0.5 s each, 16-bit stereo
    std::vector<uint8_t> ab = a;
    ab.insert(ab.end(), b.begin(), b.end());
    auto output = [](const std::vector<std::vector<uint8_t>> &parts) {
        Card c;
        c.auto_end = false; // fed by hand
        c.card.set_tap(true);
        c.open(0, 0, 44100, 3);
        c.open(1, 0, 44100, 3); // both ways, so the card's time is the same (one stream: 1 opened, unused)
        if (parts.size() == 2)
            c.send(SoundFw::STREAM_NEXT, 0x41, {0, 1});
        for (unsigned s = 0; s < parts.size(); ++s)
        {
            chunk(c, static_cast<uint8_t>(s), static_cast<uint32_t>(parts[s].size()), parts[s].data(), parts[s].size());
            c.send(SoundFw::STREAM_END, 0x62, {static_cast<uint8_t>(s)});
        }
        c.run_ms(1300);
        return c.card.take_tap();
    };
    const auto one = output({ab}), two = output({a, b});
    // From each one's first sound (the feeding's waits differ by a sample or so), the same for as long as both run.
    auto first = [](const std::vector<int16_t> &v) {
        size_t i = 0;
        while (i < v.size() && v[i] == 0)
            ++i;
        return i & ~size_t{1};
    };
    const size_t f1 = first(one), f2 = first(two), n = std::min(one.size() - f1, two.size() - f2);
    std::printf("  %zu and %zu frames, sound from %zu and %zu, %zu compared\n", one.size() / 2, two.size() / 2, f1 / 2, f2 / 2, n / 2);
    CHECK(n > 2 * 47000u);
    CHECK(std::equal(one.begin() + static_cast<std::ptrdiff_t>(f1), one.begin() + static_cast<std::ptrdiff_t>(f1 + n),
                     two.begin() + static_cast<std::ptrdiff_t>(f2)));
}

namespace
{
    /// `n` layer III frames as a stream has them (MPEG-1, 128 kbit/s, 44.1 kHz: 417 bytes each), their audio silence.
    std::vector<uint8_t> mp3_frames(unsigned n)
    {
        std::vector<uint8_t> out;
        for (unsigned k = 0; k < n; ++k)
        {
            std::vector<uint8_t> f(417, 0);
            f[0] = 0xFF, f[1] = 0xFB, f[2] = 0x90, f[3] = 0x44;
            out.insert(out.end(), f.begin(), f.end());
        }
        return out;
    }
    /// The frames STREAM_STATUS says played.
    uint32_t frames_played(Card &c)
    {
        c.send(SoundFw::STREAM_STATUS, 0x65, {0});
        const auto r = reply_for(c, 0x65);
        return r.size() == 2 + 21u ? le32(r, 2 + 13) : 0;
    }
} // namespace

TEST(an_mp3_fed_as_it_plays_decodes_as_the_whole_data_would)
{
    // 120 frames (138,240 samples a channel): 8 KB at once, played out to an underrun, then the rest in 4 KB pieces
    // with the host late each time (an underrun between them); (a) each piece landing at once, (b) at DMA's pace,
    // about 149 bytes a 48 kHz sample. A frame not yet whole is waited for, never thrown away: every one plays.
    for (const bool paced : {false, true})
    {
        Card c;
        c.auto_end = false;
        const std::vector<uint8_t> data = mp3_frames(120);
        c.send(SoundFw::STREAM_OPEN, 0x40, {0, 1, 0, 0, 1, 0x00, 0x10, 0x00, 100, 0}); // MP3, 4 KB chunks, 100 ms
        c.run(300);
        size_t at = 0;
        auto piece = [&](size_t len) {
            c.send(SoundFw::STREAM_DATA, 0x60, {0, static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len >> 16)});
            c.wait_xfer();
            for (size_t i = 0; i < len; ++i)
            {
                c.card.write(SoundCard3::DATA, data[at++]);
                if (paced && i % 149 == 148)
                    c.run(SoundCard3::NATIVE_HZ / 48000);
            }
        };
        piece(8192);
        c.run_ms(1000);
        while (at < data.size())
        {
            piece(std::min<size_t>(4096, data.size() - at));
            c.run_ms(400);
        }
        c.send(SoundFw::STREAM_END, 0x62, {0});
        c.run_ms(3000);
        const uint32_t played = frames_played(c);
        std::printf("  %s: %u frames played of %u; %zu underruns\n", paced ? "at DMA's pace" : "pieces at once", played,
                    120u * 1152u, c.of(SoundFw::EV_UNDERRUN).size());
        CHECK_EQ(played, 120u * 1152u);
        CHECK_EQ(c.card.firmware().stream_state(0), SoundFw::S_ENDED);
    }
}

TEST(an_id3v2_tag_is_skipped_by_its_size)
{
    // An ID3v2.3 tag whose body holds twelve frame-like headers in step (as an embedded picture or clip might),
    // then 40 frames: the tag is passed over by its size, so exactly the 40 frames play.
    std::vector<uint8_t> body = mp3_frames(12);
    body.resize(body.size() + 100, 0);
    const size_t size = body.size();
    std::vector<uint8_t> data{'I', 'D', '3', 3, 0, 0, static_cast<uint8_t>(size >> 21 & 0x7F), static_cast<uint8_t>(size >> 14 & 0x7F),
                              static_cast<uint8_t>(size >> 7 & 0x7F), static_cast<uint8_t>(size & 0x7F)};
    data.insert(data.end(), body.begin(), body.end());
    const std::vector<uint8_t> frames = mp3_frames(40);
    data.insert(data.end(), frames.begin(), frames.end());
    Card c;
    c.auto_end = false;
    c.send(SoundFw::STREAM_OPEN, 0x40, {0, 1, 0, 0, 1, 0, 0, 0, 0, 0});
    c.run(300);
    c.send(SoundFw::STREAM_DATA, 0x60, {0, static_cast<uint8_t>(data.size()), static_cast<uint8_t>(data.size() >> 8), 0});
    c.wait_xfer();
    for (const uint8_t x : data)
        c.card.write(SoundCard3::DATA, x);
    c.send(SoundFw::STREAM_END, 0x62, {0});
    c.run_ms(2000);
    const uint32_t played = frames_played(c);
    std::printf("  %u frames played (40 x 1,152 = %u)\n", played, 40u * 1152u);
    CHECK_EQ(played, 40u * 1152u);
}

// --- The decoder's robustness: junk, tags, free format, rate changes, damaged frames ------------------------

namespace
{
    /// An ID3v2.4 tag of `payload` bytes of ASCII (its header, then letters): no $FF in it, so no false sync.
    std::vector<uint8_t> id3_tag(size_t payload, uint32_t seed)
    {
        std::vector<uint8_t> t = {'I', 'D', '3', 4, 0, 0};
        for (const unsigned shift : {21u, 14u, 7u, 0u}) // the size, syncsafe
            t.push_back(static_cast<uint8_t>((payload >> shift) & 0x7F));
        for (size_t i = 0; i < payload; ++i)
        {
            seed = seed * 1'103'515'245u + 12'345u;
            t.push_back(static_cast<uint8_t>(' ' + (seed >> 16) % 95));
        }
        return t;
    }

    std::vector<uint8_t> mp3_concat(std::initializer_list<const char *> names)
    {
        std::vector<uint8_t> out;
        for (const char *n : names)
        {
            const std::vector<uint8_t> f = mp3_file(n);
            out.insert(out.end(), f.begin(), f.end());
        }
        return out;
    }

    /// Where each frame minimp3 decodes in `mp3` starts.
    std::vector<size_t> frame_starts(const std::vector<uint8_t> &mp3)
    {
        std::vector<size_t> out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        size_t pos = 0;
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), nullptr, &info);
            if (!info.frame_bytes)
                break;
            if (info.hz)
                out.push_back(pos + static_cast<size_t>(info.frame_offset));
            pos += static_cast<size_t>(info.frame_bytes);
        }
        return out;
    }

    /// `mp3` with each frame's bitrate nibble set to 0: free format, the frames as long as before.
    std::vector<uint8_t> free_format(std::vector<uint8_t> mp3)
    {
        for (const size_t h : frame_starts(mp3))
            mp3[h + 2] &= 0x0F;
        return mp3;
    }

    /// `frames` MPEG-1 layer I frames, 128 kbps at 44.1 kHz: 136 bytes each, consistent, silent.
    std::vector<uint8_t> layer_one(size_t frames)
    {
        std::vector<uint8_t> out;
        for (size_t f = 0; f < frames; ++f)
        {
            const uint8_t h[4] = {0xFF, 0xFF, 0x40, 0xC0}; // MPEG-1, layer I, no CRC; 128 kbps, 44.1 kHz; mono
            out.insert(out.end(), h, h + 4);
            out.insert(out.end(), 136 - 4, uint8_t{0});
        }
        return out;
    }

    double host_seconds(const std::function<void()> &f)
    {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    /// STREAM_STATUS's results for stream 0 (state, codec, rate, ...), the reply waited for; empty if none came.
    std::vector<uint8_t> status0(Card &c)
    {
        c.drain();
        c.seen.clear(); // so the reply found is this one
        c.send(SoundFw::STREAM_STATUS, 0x66, {0});
        std::vector<uint8_t> r = reply_for(c, 0x66);
        return r.size() == 2 + 21u ? std::vector<uint8_t>(r.begin() + 2, r.end()) : std::vector<uint8_t>{};
    }

    /// A card with stream 0 open for an MP3 (the default chunk and prebuffer), fed by hand: no NEED answered.
    struct Mp3Card : Card
    {
        Mp3Card()
        {
            auto_end = false;
            open(0, 1, 0, 0);
        }
        /// `bytes` into the ring in one STREAM_DATA.
        void give(const std::vector<uint8_t> &bytes)
        {
            chunk(*this, 0, static_cast<uint32_t>(bytes.size()), bytes.data(), bytes.size());
            run(200);
        }
        void end() { send(SoundFw::STREAM_END, 0x62, {0}); }
        uint8_t state() const { return card.firmware().stream_state(0); }
    };

    struct Played
    {
        std::vector<int16_t> tap; ///< the stream tap: each frame as it played
        uint8_t state{0};
        std::vector<std::pair<unsigned, unsigned>> seen; ///< with `watch`: STREAM_STATUS's rate and kbps as each changed
        bool ok{false};                                  ///< every reply OK
    };

    /// `bytes` played on stream 0: given in STREAM_DATA pieces of `piece` bytes a millisecond apart (0: all in
    /// one), STREAM_END with the last or `late_ms` after it, then until the stream ends or stops (`most` ms at most).
    Played play_mp3(const std::vector<uint8_t> &bytes, size_t piece, unsigned late_ms = 0, bool watch = false)
    {
        Mp3Card c;
        Played out;
        size_t at = 0;
        bool ended = false;
        unsigned since = 0;
        for (unsigned ms = 0; ms < 20'000; ++ms)
        {
            if (at < bytes.size())
            {
                const size_t n = piece ? std::min(piece, bytes.size() - at) : bytes.size();
                chunk(c, 0, static_cast<uint32_t>(n), bytes.data() + at, n);
                at += n;
            }
            else if (!ended && since++ >= late_ms)
            {
                c.end();
                ended = true;
            }
            c.run_ms(1);
            if (watch && c.state() == SoundFw::S_PLAYING)
            {
                const std::vector<uint8_t> r = status0(c);
                if (r.size() == 21)
                {
                    const std::pair<unsigned, unsigned> now{le16(r, 2), le16(r, 17)};
                    if (r[0] == SoundFw::S_PLAYING && (out.seen.empty() || out.seen.back() != now))
                        out.seen.push_back(now);
                }
            }
            if (c.state() == SoundFw::S_ENDED || c.state() >= 0x80)
                break;
        }
        out.state = c.state();
        out.tap = c.card.firmware().take_stream_tap();
        out.ok = c.all_ok();
        return out;
    }
} // namespace

TEST(sixty_four_kb_with_no_frame_is_error_81)
{
    // Text, no $FF in it: 80 KB, no end. Once 64 KB have been looked through with no frame, the stream is $81, and
    // stays so (a byte more changes nothing) until it is closed. Nothing plays.
    const std::vector<uint8_t> tagged = id3_tag(80'000, 4);
    const std::vector<uint8_t> text(tagged.begin() + 10, tagged.end()); // loose text: not a tag
    Mp3Card c;
    c.give(text);
    c.run_ms(100);
    CHECK_EQ(c.state(), SoundFw::S_NO_SYNC);
    CHECK(frames_played(c) == 0u);
    CHECK(c.card.firmware().stream_buffered(0) < text.size() - 65'536 + 1);
    c.give({0x00});
    c.run_ms(100);
    CHECK_EQ(c.state(), SoundFw::S_NO_SYNC);
    c.send(SoundFw::STREAM_CLOSE, 0x67, {0});
    c.run_ms(1);
    CHECK_EQ(c.state(), SoundFw::S_IDLE);
    // Under 64 KB of it with the end is only the end, not the error.
    Mp3Card d;
    d.give(std::vector<uint8_t>(text.begin(), text.begin() + 10'000));
    d.end();
    d.run_ms(100);
    CHECK_EQ(d.state(), SoundFw::S_ENDED);
    CHECK(c.all_ok() && d.all_ok());
}

TEST(hostile_frames_cost_little_and_end_in_81)
{
    // 128 KB of 4-byte free-format "frames" (FF F2 00 00), and of layer I frames: refused, they are junk, passed
    // over cheaply (given, then 10 ms of the card in 4-cycle ticks: under a second of host time), and 64 KB of them is $81.
    constexpr size_t BYTES = 0x20000;
    std::vector<uint8_t> fake;
    for (size_t i = 0; i < BYTES / 4; ++i)
        fake.insert(fake.end(), {0xFF, 0xF2, 0x00, 0x00});
    for (const auto &bytes : {fake, layer_one(BYTES / 136)})
    {
        Mp3Card c;
        const double secs = host_seconds([&] { // the data's arrival too: the search starts as it comes
            c.give(bytes);
            for (uint64_t t = 0; t < HZ / 100; t += 4)
                c.card.tick(4);
        });
        std::printf("  %s: 10 ms emulated, %.3f s of host time, state $%02X\n",
                    bytes[1] == 0xF2 ? "free-format 4-byte frames" : "layer I frames", secs, c.state());
        CHECK(secs < 1.0);
        CHECK_EQ(c.state(), SoundFw::S_NO_SYNC);
        CHECK(frames_played(c) == 0u);
    }
}

TEST(layer_iii_frames_that_make_nothing_end_in_81)
{
    // Proper layer III headers, MPEG-1 128 kbps at 44.1 kHz, 417 bytes apart, but each frame's main data begins
    // where no reservoir is (all ones): every frame decodes to nothing. They count as junk: $81, cheaply.
    std::vector<uint8_t> bytes;
    while (bytes.size() + 417 <= 0x20000)
    {
        bytes.insert(bytes.end(), {0xFF, 0xFB, 0x90, 0xC0});
        bytes.insert(bytes.end(), 417 - 4, uint8_t{0xFF});
    }
    Mp3Card c;
    const double secs = host_seconds([&] {
        c.give(bytes);
        for (uint64_t t = 0; t < HZ / 10; t += 4)
            c.card.tick(4);
    });
    std::printf("  100 ms emulated: %.3f s of host time, state $%02X\n", secs, c.state());
    CHECK(secs < 1.0);
    CHECK_EQ(c.state(), SoundFw::S_NO_SYNC);
    CHECK(frames_played(c) == 0u);
}

TEST(free_format_is_refused)
{
    // cbr128 relabelled free format, the same frames otherwise: junk, all of it. With its end it ends having played
    // nothing; seven times over (73 KB, no end), $81.
    const std::vector<uint8_t> once = free_format(mp3_file("cbr128_joint_44k.mp3"));
    const Played p = play_mp3(once, 0);
    CHECK(p.tap.empty());
    CHECK_EQ(p.state, SoundFw::S_ENDED);
    Mp3Card d;
    std::vector<uint8_t> seven;
    for (unsigned k = 0; k < 7; ++k)
        seven.insert(seven.end(), once.begin(), once.end());
    d.give(seven);
    d.run_ms(100);
    CHECK_EQ(d.state(), SoundFw::S_NO_SYNC);
    CHECK(frames_played(d) == 0u);
}

TEST(a_tag_of_100_kb_is_skipped_and_doesnt_count_toward_81)
{
    // Cover art makes tags this big. Skipped by its declared size (with its footer, flagged), at once and fed in
    // pieces: then the file, every sample of it (trimmed by its LAME tag, as a file alone is). A hostile size (over
    // 8 MB) isn't taken for a tag: junk.
    const std::vector<uint8_t> mp3 = mp3_file("cbr128_joint_44k.mp3");
    std::vector<uint8_t> bytes = id3_tag(100'000, 6);
    bytes[5] = 0x10; // the footer flag
    const uint8_t footer[10] = {'3', 'D', 'I', 4, 0, 0x10, bytes[6], bytes[7], bytes[8], bytes[9]};
    bytes.insert(bytes.end(), footer, footer + 10);
    bytes.insert(bytes.end(), mp3.begin(), mp3.end());
    const std::vector<int16_t> want = trimmed(mp3);
    const Played once = play_mp3(bytes, 0), fed = play_mp3(bytes, 2'000);
    std::printf("  %zu frames wanted; at once %zu, fed %zu\n", want.size() / 2, once.tap.size() / 2, fed.tap.size() / 2);
    CHECK(once.tap == want);
    CHECK(fed.tap == want);
    CHECK_EQ(fed.state, SoundFw::S_ENDED);
    CHECK(once.ok && fed.ok);
    // 9 MB declared, 70 KB of text actually there: no tag, junk, $81.
    std::vector<uint8_t> hostile = id3_tag(70'000, 7);
    hostile[6] = 0x04, hostile[7] = 0x40, hostile[8] = 0x00, hostile[9] = 0x00; // 9 MB, syncsafe
    Mp3Card e;
    e.give(hostile);
    e.run_ms(100);
    CHECK_EQ(e.state(), SoundFw::S_NO_SYNC);
}

TEST(rate_and_channels_follow_each_frame_through_changes)
{
    // 44.1 kHz stereo, then 22.05 kHz mono, then 48 kHz stereo, end to end: as minimp3 decodes the whole of it
    // (trimmed by the first file's LAME tag), given at once, fed in pieces, and fed with the end a second late.
    // STREAM_STATUS's rate and bitrate follow.
    const std::vector<uint8_t> bytes = mp3_concat({"cbr128_joint_44k.mp3", "cbr32_mono_22k.mp3", "cbr320_stereo_48k.mp3"});
    const std::vector<int16_t> want = trimmed(bytes);
    const Played once = play_mp3(bytes, 0), fed = play_mp3(bytes, 300, 0, true), late = play_mp3(bytes, 300, 1'000);
    std::printf("  %zu frames: at once %s, fed %s, the end late %s\n", want.size() / 2,
                once.tap == want ? "as minimp3" : "different", fed.tap == want ? "as minimp3" : "different",
                late.tap == want ? "as minimp3" : "different");
    CHECK(once.tap == want);
    CHECK(fed.tap == want);
    CHECK(late.tap == want);
    CHECK(once.ok && fed.ok && late.ok);
    const std::vector<std::pair<unsigned, unsigned>> changes = {{44'100, 128}, {22'050, 32}, {48'000, 320}};
    for (const auto &[hz, kbps] : fed.seen)
        std::printf("  %u Hz, %u kbps\n", hz, kbps);
    CHECK(fed.seen == changes);
}

TEST(damaged_frames_mid_stream_play_as_minimp3_with_the_end_at_once_or_late)
{
    // Our cbr128 four times over, frames 40 on damaged (big_values past 288: the side info refused, the decoder
    // reset): minimp3 on the whole stream (trimmed by its LAME tag), frames after them and all, whether the end
    // comes with the last byte or a second later.
    const std::vector<uint8_t> one = mp3_file("cbr128_joint_44k.mp3");
    std::vector<uint8_t> four;
    for (int i = 0; i < 4; ++i)
        four.insert(four.end(), one.begin(), one.end());
    const std::vector<size_t> heads = frame_starts(four);
    for (const size_t damaged : {size_t{4}, size_t{12}, size_t{20}})
    {
        std::vector<uint8_t> s = four;
        for (size_t k = 40; k < 40 + damaged; ++k)
        {
            const size_t side = heads[k] + 4 + ((s[heads[k] + 1] & 1) ? 0 : 2); // past the CRC, if there is one
            s[side + 4] = 0xFF;
            s[side + 5] |= 0x80;
        }
        const std::vector<int16_t> want = trimmed(s);
        const Played a = play_mp3(s, 4'096), b = play_mp3(s, 4'096, 1'000);
        std::printf("  %zu damaged: minimp3 %zu frames; the end at once %zu, late %zu\n", damaged, want.size() / 2,
                    a.tap.size() / 2, b.tap.size() / 2);
        CHECK(a.tap == want);
        CHECK(b.tap == want);
        CHECK_EQ(a.state, SoundFw::S_ENDED);
        CHECK_EQ(b.state, SoundFw::S_ENDED);
    }
}

int main() { return run_all(); }
