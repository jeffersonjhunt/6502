/**
 * @file test_sound_mp3.cpp
 * @brief Sound card revision 2's MP3 codec (hw/sound-rev2.md, step 4).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The files are ours: tools/make_mp3_fixtures.sh encodes a synthetic signal with LAME (test/host/mp3/). Each
 * plays through the card bit for bit as minimp3, run directly on the same file, decodes it: written at once by
 * the CPU, fed in small pieces while it plays, and by demand-mode DMA. Then junk and an ID3 tag before the first
 * frame, a tag between frames, CP/M's padding after the last (the last frame still played, by its own length),
 * $81 for 64 KB with no frame, RATE, PCMFMT and KBPS, and the same POS and STATE at audio rates 0 and 48,000.
 */

#include "harness.h"

#include "dma_controller.h"
#include "machine.h"
#include "minimp3.h"
#include "sound_card.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t SECOND = SoundCard::NATIVE_HZ;

    std::vector<uint8_t> file(const std::string &name)
    {
        std::ifstream in(std::string(EMU_MP3_DIR) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// minimp3 on its own, on the whole of `mp3`: its samples as the card's bus carries them (mono on both
    /// sides), and the rate and channels of the first frame.
    struct Decoded
    {
        std::vector<int16_t> frames;
        int hz{0}, channels{0}, kbps{0};
        size_t mp3_frames{0};
    };
    Decoded reference(const std::vector<uint8_t> &mp3)
    {
        Decoded out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        size_t pos = 0;
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos),
                                              pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            pos += static_cast<size_t>(info.frame_bytes);
            if (!info.hz)
                continue;
            if (!out.hz)
                out.hz = info.hz, out.channels = info.channels, out.kbps = info.bitrate_kbps;
            ++out.mp3_frames;
            for (int i = 0; i < n; ++i)
            {
                const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                out.frames.push_back(l);
                out.frames.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
            }
        }
        return out;
    }

    void run(SoundCard &c, uint64_t cycles)
    {
        for (; cycles >= 7; cycles -= 7)
            c.tick(7);
        if (cycles)
            c.tick(cycles);
    }

    void start_mp3(SoundCard &c, uint8_t ctl = SoundCard::PCMCTL_RUN)
    {
        c.write(SoundCard::CODEC, 1);
        c.write(SoundCard::PCMCTL, ctl);
    }

    uint8_t state(const SoundCard &c) { return c.peek(SoundCard::STATE); }

    uint32_t pos(SoundCard &c)
    {
        const uint32_t lo = c.read(SoundCard::POS);
        const uint32_t b1 = c.read(SoundCard::POS + 1), b2 = c.read(SoundCard::POS + 2), b3 = c.read(SoundCard::POS + 3);
        return lo | b1 << 8 | b2 << 16 | b3 << 24;
    }

    /// Plays to the end: every byte written at once, then EOF, then time until the stream ends (2 s at most).
    std::vector<int16_t> play_all(SoundCard &c, const std::vector<uint8_t> &bytes)
    {
        c.set_tap(true);
        start_mp3(c);
        for (const uint8_t b : bytes)
            c.write(SoundCard::PCMDATA, b);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        for (unsigned ms = 0; ms < 2'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
            run(c, SECOND / 1'000);
        return c.take_tap();
    }

    /// Plays to the end fed in `piece`-byte pieces a millisecond apart, EOF with the last: the FIFO never
    /// holds the whole file, so the decoder meets the end of the data mid-frame.
    std::vector<int16_t> play_fed(SoundCard &c, const std::vector<uint8_t> &bytes, size_t piece)
    {
        c.set_tap(true);
        start_mp3(c);
        size_t at = 0;
        for (unsigned ms = 0; ms < 3'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
        {
            for (size_t k = 0; k < piece && at < bytes.size(); ++k)
                c.write(SoundCard::PCMDATA, bytes[at++]);
            if (at == bytes.size())
                c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            run(c, SECOND / 1'000);
        }
        return c.take_tap();
    }

    const char *const FILES[] = {"cbr320_stereo_48k.mp3", "cbr128_joint_44k.mp3", "cbr32_mono_22k.mp3",
                                 "cbr8_mono_8k.mp3",      "vbr0_stereo_44k.mp3",  "vbr9_mono_22k.mp3"};

    /// ASCII junk: an ID3v2 tag (its header and text frames' worth of letters) and then loose text. No $FF, so
    /// no false sync in it.
    std::vector<uint8_t> tag(size_t payload, uint32_t seed)
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
} // namespace

TEST(each_file_plays_as_minimp3_decodes_it)
{
    for (const char *name : FILES)
    {
        const std::vector<uint8_t> bytes = file(name);
        REQUIRE(!bytes.empty());
        const Decoded want = reference(bytes);
        SoundCard c;
        const std::vector<int16_t> got = play_all(c, bytes);
        std::printf("  %s: %zu bytes, %zu MP3 frames, %zu samples a side at %d Hz, %d ch; the card %s\n", name,
                    bytes.size(), want.mp3_frames, want.frames.size() / 2, want.hz, want.channels,
                    got == want.frames ? "the same" : "different");
        CHECK(!want.frames.empty());
        CHECK(got == want.frames);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        CHECK_EQ(pos(c), want.frames.size() / 2);
        CHECK_EQ(c.underruns(), 0u);
        CHECK_EQ(c.fifo_bytes(), 0u);
    }
}

TEST(fed_in_pieces_while_it_plays_it_decodes_the_same)
{
    for (const char *name : FILES)
        for (const size_t piece : {size_t{97}, size_t{600}})
        {
            const std::vector<uint8_t> bytes = file(name);
            const Decoded want = reference(bytes);
            SoundCard c;
            const std::vector<int16_t> got = play_fed(c, bytes, piece); // faster than it plays: never dry
            std::printf("  %s in %zu-byte pieces: %s, %u underruns\n", name, piece,
                        got == want.frames ? "the same" : "different", c.underruns());
            CHECK(got == want.frames);
            CHECK_EQ(c.underruns(), 0u);
            CHECK_EQ(pos(c), want.frames.size() / 2);
            CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        }
}

TEST(fed_slower_than_it_plays_it_waits_for_each_frame_whole)
{
    // Fed at 60% of the stream's own rate, after the 4 KB it starts with: it runs dry again and again, the next frame only partly in the FIFO
    // when it falls due. The decoder waits for the frame's end (and the next header) each time rather than search
    // a partial one, so what plays, the silence left out, is still minimp3's decode of the file.
    for (const char *name : {"cbr128_joint_44k.mp3", "cbr320_stereo_48k.mp3"})
    {
        const std::vector<uint8_t> bytes = file(name);
        const Decoded want = reference(bytes);
        const double per_ms = 0.6 * static_cast<double>(bytes.size()) /
                              (1'000.0 * static_cast<double>(want.frames.size() / 2) / want.hz);
        SoundCard c;
        c.set_tap(true, false); // what played only
        start_mp3(c);
        double owed = 0;
        size_t at = 0;
        for (unsigned ms = 0; ms < 3'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
        {
            for (owed += per_ms; owed >= 1 && at < bytes.size(); owed -= 1)
                c.write(SoundCard::PCMDATA, bytes[at++]);
            if (at == bytes.size())
                c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            run(c, SECOND / 1'000);
        }
        const std::vector<int16_t> got = c.take_tap();
        std::printf("  %s at %.1f bytes a ms: %u underruns, %s\n", name, per_ms, c.underruns(),
                    got == want.frames ? "the same" : "different");
        CHECK(c.underruns() >= 3); // dry again and again
        CHECK(got == want.frames);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    }
}

TEST(junk_and_a_tag_before_the_first_frame_are_skipped)
{
    // An ID3v2 tag of 3 KB, then 2 KB of loose text, then the file: written at once and fed in pieces.
    const std::vector<uint8_t> mp3 = file("cbr128_joint_44k.mp3");
    std::vector<uint8_t> bytes = tag(3'000, 1);
    const std::vector<uint8_t> text = tag(2'000, 2);
    bytes.insert(bytes.end(), text.begin() + 10, text.end());
    bytes.insert(bytes.end(), mp3.begin(), mp3.end());
    const Decoded want = reference(mp3);
    for (const size_t piece : {size_t{0}, size_t{300}})
    {
        SoundCard c;
        const std::vector<int16_t> got = piece ? play_fed(c, bytes, piece) : play_all(c, bytes);
        std::printf("  %s: %zu samples a side, the file alone %zu\n", piece ? "fed" : "at once", got.size() / 2,
                    want.frames.size() / 2);
        CHECK(got == want.frames);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    }
}

TEST(a_tag_between_frames_is_skipped_by_its_size)
{
    // A 1 KB ID3v2 tag half way, between two frames: the frame before it plays by its own length, the tag is
    // skipped by the size it declares, and the decoder stays in step: the file's samples, every one, as if the tag
    // weren't there. Written at once, and fed in pieces.
    const std::vector<uint8_t> mp3 = file("vbr0_stereo_44k.mp3");
    const Decoded want = reference(mp3);
    size_t cut = 0; // the first frame boundary past the middle, from a decode that stops there
    {
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        while (cut < mp3.size() / 2)
        {
            mp3dec_frame_info_t info{};
            mp3dec_decode_frame(dec.get(), mp3.data() + cut, static_cast<int>(mp3.size() - cut), nullptr, &info);
            REQUIRE(info.frame_bytes > 0);
            cut += static_cast<size_t>(info.frame_bytes);
        }
    }
    std::vector<uint8_t> bytes(mp3.begin(), mp3.begin() + static_cast<std::ptrdiff_t>(cut));
    const std::vector<uint8_t> t = tag(1'000, 3);
    bytes.insert(bytes.end(), t.begin(), t.end());
    bytes.insert(bytes.end(), mp3.begin() + static_cast<std::ptrdiff_t>(cut), mp3.end());
    SoundCard c;
    const std::vector<int16_t> got = play_all(c, bytes);
    SoundCard d;
    const std::vector<int16_t> fed = play_fed(d, bytes, 300);
    std::printf("  at once %s, fed %s\n", got == want.frames ? "the file's" : "different",
                fed == want.frames ? "the file's" : "different");
    CHECK(got == want.frames);
    CHECK(fed == want.frames);
    CHECK_EQ(state(d), SoundCard::STATE_ENDED);
}

TEST(cp_m_padding_after_the_last_frame_leaves_it_played)
{
    // The file as a CP/M program would read it: $1A to the end of its last 128-byte record. The last frame plays,
    // by its own length; the padding is ignored. The same as minimp3 on the file without it.
    for (const char *name : FILES)
    {
        const std::vector<uint8_t> mp3 = file(name);
        std::vector<uint8_t> padded = mp3;
        do
            padded.push_back(0x1A);
        while (padded.size() % 128);
        const Decoded want = reference(mp3);
        SoundCard c;
        const std::vector<int16_t> got = play_all(c, padded);
        std::printf("  %s + %zu bytes of $1A: %zu samples a side, the file alone %zu\n", name,
                    padded.size() - mp3.size(), got.size() / 2, want.frames.size() / 2);
        CHECK(got == want.frames); // the last frame there
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        CHECK_EQ(c.fifo_bytes(), 0u);
    }
}

TEST(a_flush_starts_the_decoder_afresh)
{
    // Half of one file, then FLUSH, a seek, and another file of the same kind: what plays after the seek is that
    // file as minimp3 decodes it from its start, nothing of the first's reservoir or overlap carried in.
    const std::vector<uint8_t> first = file("vbr0_stereo_44k.mp3"), second = file("cbr128_joint_44k.mp3");
    SoundCard c;
    c.set_tap(true);
    start_mp3(c);
    for (const uint8_t b : first)
        c.write(SoundCard::PCMDATA, b);
    run(c, SECOND / 4);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_FLUSH);
    c.take_tap();
    for (const uint8_t b : second)
        c.write(SoundCard::PCMDATA, b);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    for (unsigned ms = 0; ms < 2'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
        run(c, SECOND / 1'000);
    CHECK(c.take_tap() == reference(second).frames);
}

TEST(sixty_four_kb_with_no_frame_is_error_81)
{
    // Text, no $FF in it: 80 KB, no EOF. Once 64 KB have been looked through with no frame, STATE is $81, and
    // stays so until RUN is cleared. Nothing plays.
    SoundCard c;
    start_mp3(c);
    const std::vector<uint8_t> tagged = tag(80'000, 4);
    const std::vector<uint8_t> text(tagged.begin() + 10, tagged.end()); // loose text: not a tag
    for (const uint8_t b : text)
        c.write(SoundCard::PCMDATA, b);
    run(c, SECOND / 10);
    CHECK_EQ(state(c), SoundCard::STATE_NO_SYNC);
    CHECK_EQ(pos(c), 0u);
    CHECK(c.fifo_bytes() < text.size() - 65'536 + 1);
    c.write(SoundCard::PCMDATA, 0x00);
    run(c, SECOND / 10);
    CHECK_EQ(state(c), SoundCard::STATE_NO_SYNC);
    c.write(SoundCard::PCMCTL, 0);
    CHECK_EQ(state(c), SoundCard::STATE_IDLE);
    // Under 64 KB of it with EOF is only the end, not the error.
    SoundCard d;
    start_mp3(d);
    for (size_t i = 0; i < 10'000; ++i)
        d.write(SoundCard::PCMDATA, text[i]);
    d.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    run(d, SECOND / 10);
    CHECK_EQ(state(d), SoundCard::STATE_ENDED);
}

TEST(rate_format_and_bitrate_read_back_the_streams)
{
    for (const auto &[name, hz, fmt, kbps] :
         {std::tuple<const char *, unsigned, uint8_t, unsigned>{"cbr32_mono_22k.mp3", 22'050, SoundCard::PCMFMT_16BIT, 32},
          {"cbr320_stereo_48k.mp3", 48'000, SoundCard::PCMFMT_16BIT | SoundCard::PCMFMT_STEREO, 320},
          {"cbr8_mono_8k.mp3", 8'000, SoundCard::PCMFMT_16BIT, 8}})
    {
        const std::vector<uint8_t> bytes = file(name);
        SoundCard c;
        c.write(SoundCard::RATE, 0x44); // RATE and PCMFMT as written don't matter to MP3
        c.write(SoundCard::RATE + 1, 0xAC);
        c.write(SoundCard::PCMFMT, 0);
        start_mp3(c);
        CHECK_EQ(c.read(SoundCard::RATE) | c.read(SoundCard::RATE + 1) << 8, 0); // no frame yet
        for (const uint8_t b : bytes)
            c.write(SoundCard::PCMDATA, b);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        run(c, SECOND / 20);
        CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
        const unsigned rlo = c.read(SoundCard::RATE), rhi = c.read(SoundCard::RATE + 1);
        CHECK_EQ(rlo | rhi << 8, hz);
        CHECK_EQ(c.read(SoundCard::PCMFMT), fmt);
        const unsigned klo = c.read(SoundCard::KBPS), khi = c.read(SoundCard::KBPS + 1);
        CHECK_EQ(klo | khi << 8, kbps);
        // And it plays at that rate: 50 ms is that many frames, give or take the first frame's wait.
        const uint32_t played = pos(c);
        CHECK(played + 2 >= hz / 20 && played <= hz / 20);
    }
    // VBR: the frame playing's bitrate, one of MPEG-1's.
    const std::vector<uint8_t> vbr = file("vbr0_stereo_44k.mp3");
    SoundCard v;
    start_mp3(v);
    for (const uint8_t b : vbr)
        v.write(SoundCard::PCMDATA, b);
    v.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    bool seen_two = false;
    unsigned first = 0;
    for (unsigned ms = 0; ms < 500; ++ms)
    {
        run(v, SECOND / 1'000);
        const unsigned lo = v.read(SoundCard::KBPS), hi = v.read(SoundCard::KBPS + 1);
        const unsigned k = lo | hi << 8;
        if (!first)
            first = k;
        seen_two = seen_two || (k && k != first);
        CHECK(k == 0 || (k >= 32 && k <= 320));
    }
    CHECK(seen_two);
}

TEST(pos_and_state_are_the_same_at_audio_rates_0_and_48000)
{
    const std::vector<uint8_t> bytes = file("vbr9_mono_22k.mp3");
    std::vector<uint32_t> seen[2];
    for (const uint32_t rate : {0u, 48'000u})
    {
        SoundCard c;
        c.set_audio_rate(rate);
        start_mp3(c);
        size_t at = 0;
        std::vector<float> buf(2 * 4'096);
        for (unsigned ms = 0; ms < 800; ++ms)
        {
            for (size_t k = 0; k < 80 && at < bytes.size(); ++k)
                c.write(SoundCard::PCMDATA, bytes[at++]);
            if (at == bytes.size())
                c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            run(c, SECOND / 1'000);
            c.take_audio(buf.data(), 4'096);
            seen[rate ? 1 : 0].insert(seen[rate ? 1 : 0].end(), {pos(c), state(c), static_cast<uint32_t>(c.fifo_bytes())});
        }
    }
    CHECK(seen[0] == seen[1]);
    CHECK_EQ(seen[0][seen[0].size() - 2], SoundCard::STATE_ENDED);
}

namespace
{
    struct Board
    {
        std::unique_ptr<Machine> m;
        SoundCard *sound{nullptr};

        Board()
        {
            std::string error;
            m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                               "dma 400-47F space=io name=dma irq\nsound 500-57F space=io name=sound irq drq=2\n",
                               "sound.machine", {}, error);
            if (m)
                sound = m->sound_card();
        }

        Decoder &bus() { return m->bus(); }

        void run_cycles(uint64_t cycles)
        {
            for (uint64_t i = 0; i < cycles; ++i)
            {
                bus().read(0x0000);
                bus().irq();
            }
        }
    };
} // namespace

TEST(by_demand_dma_each_file_plays_as_minimp3_decodes_it)
{
    for (const char *name : {"cbr320_stereo_48k.mp3", "cbr32_mono_22k.mp3", "vbr0_stereo_44k.mp3"})
    {
        const std::vector<uint8_t> bytes = file(name);
        const Decoded want = reference(bytes);
        Board b;
        REQUIRE(b.sound != nullptr);
        SoundCard &c = *b.sound;
        c.set_tap(true);
        for (size_t i = 0; i < bytes.size(); ++i)
            b.bus().write_physical(0x10000 + static_cast<uint32_t>(i), bytes[i]);
        b.bus().write(0xC563, SoundCard::DRQ_PCMDATA);
        b.bus().write(0xC549, 1); // CODEC: MP3
        b.bus().write(0xC548, SoundCard::PCMCTL_RUN);
        b.bus().poke(0xC420, 0x00);
        b.bus().poke(0xC421, 0x00);
        b.bus().poke(0xC422, 0x01);
        b.bus().poke(0xC424, static_cast<uint8_t>(bytes.size()));
        b.bus().poke(0xC425, static_cast<uint8_t>(bytes.size() >> 8));
        b.bus().poke(0xC426, DmaController::DEMAND | DmaController::TO_CARD);
        b.bus().poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
        b.bus().poke(0xC427, DmaController::ENABLE);
        b.run_cycles(100'000);
        REQUIRE(!(b.bus().peek(0xC427) & DmaController::ENABLE)); // all of it in
        b.bus().write(0xC548, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        for (unsigned i = 0; i < 100 && state(c) != SoundCard::STATE_ENDED; ++i)
            b.run_cycles(SECOND / 100);
        const std::vector<int16_t> got = c.take_tap();
        std::printf("  %s by DMA: %s\n", name, got == want.frames ? "the same" : "different");
        CHECK(got == want.frames);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    }
}

namespace
{
    std::vector<uint8_t> concat(std::initializer_list<const char *> names)
    {
        std::vector<uint8_t> out;
        for (const char *n : names)
        {
            const std::vector<uint8_t> f = file(n);
            out.insert(out.end(), f.begin(), f.end());
        }
        return out;
    }

    /// `mp3` with each frame's bitrate nibble set to 0: free format, the frames as long as before.
    std::vector<uint8_t> free_format(std::vector<uint8_t> mp3)
    {
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        size_t pos = 0;
        std::vector<size_t> heads;
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), nullptr, &info);
            if (!info.frame_bytes)
                break;
            if (info.hz)
                heads.push_back(pos + static_cast<size_t>(info.frame_offset));
            pos += static_cast<size_t>(info.frame_bytes);
        }
        for (const size_t h : heads)
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

    /// The card's tap and its last state for `bytes` played at once, then fed, each run.
    struct Both
    {
        std::vector<int16_t> once, fed;
    };
} // namespace

TEST(a_stuck_stream_is_not_searched_again_until_bytes_come)
{
    // No frame, and nothing new, no EOF: 4 KB of text; and a layer III header with 1,440 bytes of text after it,
    // which the decoder must search (the frame's next header isn't there) and keep (a frame could still be
    // coming). One emulated second in 4-cycle ticks, as instructions tick the card, must not search the same
    // bytes again each time: about 6 ms of host time for raw PCM's underrun; 1.5 to 2.1 s before the fix. Half
    // a second's bound: room for a sanitizer build or a slow host.
    const std::vector<uint8_t> tagged = tag(4'096, 5);
    std::vector<uint8_t> header = {0xFF, 0xFB, 0x90, 0xC0};
    header.insert(header.end(), tagged.begin() + 10, tagged.begin() + 10 + 1'440);
    for (const auto &bytes : {std::vector<uint8_t>(tagged.begin() + 10, tagged.end()), header})
    {
        SoundCard c;
        start_mp3(c);
        for (const uint8_t b : bytes)
            c.write(SoundCard::PCMDATA, b);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF); // under 4 KB: EOF starts it...
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);                         // ...and its end not yet
        const double secs = host_seconds([&] {
            for (uint64_t t = 0; t < SECOND; t += 4)
                c.tick(4);
        });
        std::printf("  %zu bytes, 1 s emulated in 4-cycle ticks: %.3f s of host time, STATE $%02X\n", bytes.size(),
                    secs, state(c));
        CHECK(secs < 0.5);
        CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
        CHECK_EQ(pos(c), 0u);
    }
}

TEST(hostile_frames_cost_little_and_end_in_81)
{
    // A FIFO full of 4-byte free-format "frames" (FF F2 00 00), and one of layer I frames: refused, they are junk,
    // passed over cheaply (248 ms of host time a millisecond, before), and 64 KB of them is $81.
    std::vector<uint8_t> fake;
    for (size_t i = 0; i < SoundCard::FIFO_BYTES / 4; ++i)
        fake.insert(fake.end(), {0xFF, 0xF2, 0x00, 0x00});
    for (const auto &bytes : {fake, layer_one(SoundCard::FIFO_BYTES / 136)})
    {
        SoundCard c;
        start_mp3(c);
        for (const uint8_t b : bytes)
            c.write(SoundCard::PCMDATA, b);
        const double secs = host_seconds([&] {
            for (uint64_t t = 0; t < SECOND / 100; t += 4)
                c.tick(4);
        });
        std::printf("  %s: 10 ms emulated, %.3f s of host time, STATE $%02X\n",
                    bytes[1] == 0xF2 ? "free-format 4-byte frames" : "layer I frames", secs, state(c));
        CHECK(secs < 1.0);
        CHECK_EQ(state(c), SoundCard::STATE_NO_SYNC);
        CHECK_EQ(pos(c), 0u);
    }
}

TEST(layer_iii_frames_that_make_nothing_end_in_81)
{
    // Proper layer III headers, MPEG-1 128 kbps at 44.1 kHz, 417 bytes apart, but each frame's main data begins
    // where no reservoir is (all ones): every frame decodes to nothing. They count as junk: $81, cheaply.
    std::vector<uint8_t> bytes;
    while (bytes.size() + 417 <= SoundCard::FIFO_BYTES)
    {
        bytes.insert(bytes.end(), {0xFF, 0xFB, 0x90, 0xC0});
        bytes.insert(bytes.end(), 417 - 4, uint8_t{0xFF});
    }
    SoundCard c;
    start_mp3(c);
    for (const uint8_t b : bytes)
        c.write(SoundCard::PCMDATA, b);
    const double secs = host_seconds([&] {
        for (uint64_t t = 0; t < SECOND / 10; t += 4)
            c.tick(4);
    });
    std::printf("  100 ms emulated: %.3f s of host time, STATE $%02X\n", secs, state(c));
    CHECK(secs < 1.0);
    CHECK_EQ(state(c), SoundCard::STATE_NO_SYNC);
    CHECK_EQ(pos(c), 0u);
}

TEST(free_format_is_refused)
{
    // cbr128 relabelled free format, the same frames otherwise: junk, all of it. At EOF it ends having played
    // nothing; seven times over (73 KB, no EOF), $81.
    const std::vector<uint8_t> once = free_format(file("cbr128_joint_44k.mp3"));
    SoundCard c;
    CHECK(play_all(c, once).empty());
    CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    SoundCard d;
    start_mp3(d);
    for (unsigned k = 0; k < 7; ++k)
        for (const uint8_t b : once)
            d.write(SoundCard::PCMDATA, b);
    run(d, SECOND / 10);
    CHECK_EQ(state(d), SoundCard::STATE_NO_SYNC);
    CHECK_EQ(pos(d), 0u);
}

TEST(a_tag_of_100_kb_is_skipped_and_doesnt_count_toward_81)
{
    // Cover art makes tags this big. Skipped by its declared size (with its footer, flagged), at once and fed in
    // pieces: then the file, every sample of it. A hostile size (over 8 MB) isn't taken for a tag: junk.
    const std::vector<uint8_t> mp3 = file("cbr128_joint_44k.mp3");
    std::vector<uint8_t> bytes = tag(100'000, 6);
    bytes[5] = 0x10;                                    // the footer flag
    const uint8_t footer[10] = {'3', 'D', 'I', 4, 0, 0x10, bytes[6], bytes[7], bytes[8], bytes[9]};
    bytes.insert(bytes.end(), footer, footer + 10);
    bytes.insert(bytes.end(), mp3.begin(), mp3.end());
    const Decoded want = reference(mp3);
    SoundCard c, d;
    CHECK(play_all(c, bytes) == want.frames);
    CHECK(play_fed(d, bytes, 2'000) == want.frames);
    CHECK_EQ(state(d), SoundCard::STATE_ENDED);
    // 9 MB declared, 70 KB of text actually there: no tag, junk, $81.
    std::vector<uint8_t> hostile = tag(70'000, 7);
    hostile[6] = 0x04, hostile[7] = 0x40, hostile[8] = 0x00, hostile[9] = 0x00; // 9 MB, syncsafe
    SoundCard e;
    start_mp3(e);
    for (const uint8_t b : hostile)
        e.write(SoundCard::PCMDATA, b);
    run(e, SECOND / 10);
    CHECK_EQ(state(e), SoundCard::STATE_NO_SYNC);
}

TEST(rate_and_channels_follow_each_frame_through_changes)
{
    // 44.1 kHz stereo, then 22.05 kHz mono, then 48 kHz stereo, end to end: as minimp3 decodes the whole of it,
    // written at once, fed (EOF written late), and fed with EOF written early. RATE, PCMFMT and KBPS follow.
    const std::vector<uint8_t> bytes = concat({"cbr128_joint_44k.mp3", "cbr32_mono_22k.mp3", "cbr320_stereo_48k.mp3"});
    const Decoded want = reference(bytes);
    SoundCard a, b;
    const std::vector<int16_t> once = play_all(a, bytes), fed = play_fed(b, bytes, 300);
    // Fed, EOF written before the last bytes: the same, whenever EOF is written.
    SoundCard e;
    e.set_tap(true);
    start_mp3(e);
    size_t at = 0;
    std::vector<std::tuple<unsigned, unsigned, unsigned>> seen;
    for (unsigned ms = 0; ms < 3'000 && state(e) != SoundCard::STATE_ENDED; ++ms)
    {
        for (size_t k = 0; k < 300 && at < bytes.size(); ++k)
            e.write(SoundCard::PCMDATA, bytes[at++]);
        if (bytes.size() - at < 20'000)
            e.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF); // early: more still to write
        run(e, SECOND / 1'000);
        const unsigned rlo = e.peek(SoundCard::RATE), rhi = e.peek(SoundCard::RATE + 1);
        const unsigned klo = e.peek(SoundCard::KBPS), khi = e.peek(SoundCard::KBPS + 1);
        const std::tuple<unsigned, unsigned, unsigned> now{rlo | rhi << 8, e.peek(SoundCard::PCMFMT), klo | khi << 8};
        if (state(e) == SoundCard::STATE_PLAYING && (seen.empty() || seen.back() != now))
            seen.push_back(now);
    }
    const std::vector<int16_t> early = e.take_tap();
    std::printf("  %zu samples a side: at once %s, fed %s, EOF early %s\n", want.frames.size() / 2,
                once == want.frames ? "as minimp3" : "different", fed == want.frames ? "as minimp3" : "different",
                early == want.frames ? "as minimp3" : "different");
    CHECK(once == want.frames);
    CHECK(fed == want.frames);
    CHECK(early == want.frames);
    const std::vector<std::tuple<unsigned, unsigned, unsigned>> changes = {
        {44'100, 3, 128}, {22'050, 2, 32}, {48'000, 3, 320}};
    CHECK(seen == changes);
}

TEST(pos_and_state_through_changes_are_the_same_at_audio_rates_0_and_48000)
{
    const std::vector<uint8_t> bytes = concat({"vbr0_stereo_44k.mp3", "vbr9_mono_22k.mp3", "cbr320_stereo_48k.mp3"});
    std::vector<uint32_t> seen[2];
    size_t least = SIZE_MAX;
    for (const uint32_t rate : {0u, 48'000u})
    {
        SoundCard c;
        c.set_audio_rate(rate);
        start_mp3(c);
        size_t at = 0;
        std::vector<float> buf(2 * 4'096);
        for (unsigned ms = 0; ms < 2'500; ++ms)
        {
            for (size_t k = 0; k < 120 && at < bytes.size(); ++k)
                c.write(SoundCard::PCMDATA, bytes[at++]);
            if (at == bytes.size())
                c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            // Collected at uneven moments within each millisecond, as a host's audio callback does.
            const uint64_t part = (ms * 7'919u) % (SECOND / 1'000);
            run(c, part);
            c.take_audio(buf.data(), 4'096);
            run(c, SECOND / 1'000 - part);
            c.take_audio(buf.data(), 4'096);
            if (rate && state(c) == SoundCard::STATE_PLAYING)
                least = std::min(least, c.mix_backlog()); // never dry while it plays
            seen[rate ? 1 : 0].insert(seen[rate ? 1 : 0].end(),
                                      {pos(c), state(c), static_cast<uint32_t>(c.fifo_bytes())});
        }
    }
    CHECK(seen[0] == seen[1]);
    CHECK_EQ(seen[0][seen[0].size() - 2], SoundCard::STATE_ENDED);
    std::printf("  the mix's queue, after each take while playing: at least %zu\n", least);
    CHECK(least >= 1);
}

TEST(at_a_rate_change_the_mix_lines_up_afresh_with_the_new_rates_lead)
{
    // 48 kHz (a lead of 2 samples into a 48 kHz mix) and then 8 kHz (a frame's 6 samples come in a burst: a lead
    // of 7 is wanted). Collected at random moments, as an audio callback is: the mix starts afresh at the change,
    // with the new rate's lead, so its queue never runs dry after a take, at either rate.
    const std::vector<uint8_t> bytes = concat({"cbr320_stereo_48k.mp3", "cbr8_mono_8k.mp3"});
    SoundCard c;
    c.set_audio_rate(48'000);
    start_mp3(c);
    for (const uint8_t b : bytes)
        c.write(SoundCard::PCMDATA, b);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    std::vector<float> buf(2 * 48'000);
    uint32_t seed = 9;
    size_t least[2] = {SIZE_MAX, SIZE_MAX};
    for (uint64_t t = 0; t < 2 * SECOND && state(c) != SoundCard::STATE_ENDED;)
    {
        seed = seed * 1'103'515'245u + 12'345u;
        const uint64_t n = 100 + (seed >> 8) % 29'901;
        run(c, n);
        t += n;
        c.take_audio(buf.data(), 48'000);
        if (state(c) == SoundCard::STATE_PLAYING)
        {
            const unsigned lo = c.peek(SoundCard::RATE), hi = c.peek(SoundCard::RATE + 1);
            size_t &l = least[(lo | hi << 8) == 8'000];
            l = std::min(l, c.mix_backlog());
        }
    }
    std::printf("  the queue after each take: at least %zu at 48 kHz, %zu at 8 kHz\n", least[0], least[1]);
    CHECK(least[0] >= 1 && least[0] != SIZE_MAX);
    CHECK(least[1] >= 1 && least[1] != SIZE_MAX);
}

TEST(a_stream_round_the_fifo_many_times_decodes_the_same)
{
    // 311 KB, twelve times cbr320, the FIFO kept full: the bytes wrap round the 128 KB ring again and again.
    std::vector<uint8_t> bytes;
    for (unsigned k = 0; k < 12; ++k)
    {
        const std::vector<uint8_t> f = file("cbr320_stereo_48k.mp3");
        bytes.insert(bytes.end(), f.begin(), f.end());
    }
    const Decoded want = reference(bytes);
    SoundCard c;
    c.set_tap(true);
    start_mp3(c);
    size_t at = 0;
    for (unsigned ms = 0; ms < 10'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
    {
        while (at < bytes.size() && c.fifo_bytes() < SoundCard::FIFO_BYTES)
            c.write(SoundCard::PCMDATA, bytes[at++]);
        if (at == bytes.size())
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        run(c, SECOND / 1'000);
    }
    const std::vector<int16_t> got = c.take_tap();
    std::printf("  %zu bytes, %zu samples a side: %s\n", bytes.size(), want.frames.size() / 2,
                got == want.frames ? "as minimp3" : "different");
    CHECK(got == want.frames);
    CHECK_EQ(c.underruns(), 0u);
}

namespace
{
    /// The stack below a call filled with a pattern.
    __attribute__((noinline)) void dirty(uint8_t v)
    {
        volatile uint8_t big[200'000];
        for (size_t i = 0; i < sizeof big; ++i)
            big[i] = static_cast<uint8_t>(v ^ (i * 37));
    }

    __attribute__((noinline)) std::vector<int16_t> through(const std::vector<uint8_t> &bytes)
    {
        SoundCard c;
        return play_all(c, bytes);
    }
} // namespace

TEST(bad_frames_decode_the_same_whatever_the_stack_held)
{
    // minimp3's Huffman decode reads past a bad frame's data into its scratch on the stack. Built with that
    // scratch zeroed, the same stream decodes the same whatever was on the stack before.
    const std::vector<uint8_t> base = file("cbr32_mono_22k.mp3");
    std::vector<size_t> heads;
    for (size_t i = 0; i + 1 < base.size(); ++i)
        if (base[i] == 0xFF && base[i + 1] == 0xF3)
            heads.push_back(i);
    unsigned differ = 0;
    for (uint32_t seed = 0; seed < 60; ++seed)
    {
        std::vector<uint8_t> s = base;
        uint32_t r = seed * 2'654'435'761u + 1;
        for (int k = 0; k < 6; ++k)
        {
            r = r * 1'103'515'245u + 12'345u;
            const size_t h = heads[(r >> 8) % heads.size()];
            for (size_t b = 5; b < 13 && h + b < s.size(); ++b)
            {
                r = r * 1'103'515'245u + 12'345u;
                if (r & 0x100)
                    s[h + b] = static_cast<uint8_t>(r >> 16); // the side info, broken
            }
        }
        dirty(0x00);
        const std::vector<int16_t> a = through(s);
        dirty(0xA5);
        const std::vector<int16_t> b = through(s);
        differ += a != b;
    }
    std::printf("  %u of 60 broken streams decode differently with the stack dirtied two ways\n", differ);
    CHECK_EQ(differ, 0u);
}

int main()
{
    return run_all();
}
