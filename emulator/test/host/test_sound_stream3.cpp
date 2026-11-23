/**
 * @file test_sound_stream3.cpp
 * @brief The sound card, revision 3: streams (rings in the store, NEED, prebuffer, underruns, gapless joins and
 * the MP3 trim; hw/future/sound-device.md, "Gapless streaming").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card (sound_card3.h) on its own, ticked at the native clock; a feeder answers each NEED with a chunk
 * by STREAM_DATA and DATA, as a host would. The tap is the firmware's stream tap: each frame as it played, at
 * the stream's rate. The MP3s are the repo's own (test/host/mp3, made by LAME: their Info frame's LAME tag
 * carries the encoder's delay and padding).
 */

#include "harness.h"

#include "minimp3.h"
#include "sound_card3.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
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
                const size_t n = std::min<size_t>({data[s].size() - sent[s], 32 * 1024, room[s]});
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

int main() { return run_all(); }
