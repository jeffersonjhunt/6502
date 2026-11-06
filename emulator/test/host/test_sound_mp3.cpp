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
#include <cstdlib>
#include <random>
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
            // Faster than it plays: never dry once it has started. It may wait once at the start: the first
            // frame is taken only when minimp3's sync check on it has decided (ten frames in step), which can
            // be more than the 4 KB playing starts at.
            const std::vector<int16_t> got = play_fed(c, bytes, piece);
            std::printf("  %s in %zu-byte pieces: %s, %u underruns\n", name, piece,
                        got == want.frames ? "the same" : "different", c.underruns());
            CHECK(got == want.frames);
            CHECK(c.underruns() <= 1);
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
    // 8 kHz then 48 kHz, and 48 kHz then 8 kHz, mixed at 48 kHz and collected at random moments, as an audio
    // callback is. The mix starts afresh at each change with the new rate's lead, so no SID sample is made while
    // the stream plays with nothing queued for it (mix_dry()), in either direction.
    for (const auto &order : {std::vector<const char *>{"cbr8_mono_8k.mp3", "cbr320_stereo_48k.mp3"},
                              std::vector<const char *>{"cbr320_stereo_48k.mp3", "cbr8_mono_8k.mp3"}})
        for (uint32_t seed : {9u, 10u, 11u})
        {
            std::vector<uint8_t> bytes;
            for (const char *n : order)
            {
                const std::vector<uint8_t> f = file(n);
                bytes.insert(bytes.end(), f.begin(), f.end());
            }
            SoundCard c;
            c.set_audio_rate(48'000);
            start_mp3(c);
            for (const uint8_t b : bytes)
                c.write(SoundCard::PCMDATA, b);
            c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            std::vector<float> buf(2 * 48'000);
            size_t made = 0;
            for (uint64_t t = 0; t < 2 * SECOND && state(c) != SoundCard::STATE_ENDED;)
            {
                seed = seed * 1'103'515'245u + 12'345u;
                const uint64_t n = 100 + (seed >> 8) % 29'901;
                c.tick(n); // one tick, as a run loop that falls behind gives
                t += n;
                made += c.take_audio(buf.data(), 48'000);
            }
            std::printf("  %s then %s, seed %u: %llu of %zu samples dry\n", order[0], order[1], seed,
                        static_cast<unsigned long long>(c.mix_dry()), made);
            CHECK_EQ(c.mix_dry(), 0u);
            CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        }
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

namespace
{
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

    /// Plays to the end written as room allows, EOF either with the last byte (`late` false) or a second after.
    std::vector<int16_t> play_eof(const std::vector<uint8_t> &bytes, bool late, uint8_t &end)
    {
        SoundCard c;
        c.set_tap(true, false); // what played only
        start_mp3(c);
        size_t at = 0;
        for (unsigned ms = 0; ms < 20'000; ++ms)
        {
            while (at < bytes.size() && c.fifo_bytes() < SoundCard::FIFO_BYTES)
                c.write(SoundCard::PCMDATA, bytes[at++]);
            if (at == bytes.size() && (!late || ms % 1'000 == 999))
                c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
            run(c, SECOND / 1'000);
            if (state(c) == SoundCard::STATE_ENDED || state(c) == SoundCard::STATE_NO_SYNC)
                break;
        }
        end = state(c);
        return c.take_tap();
    }
} // namespace

TEST(damaged_frames_mid_stream_play_as_minimp3_with_eof_at_once_or_late)
{
    // Our cbr128 four times over, frames 40 on damaged (big_values past 288: the side info refused, the decoder
    // reset): minimp3 on the whole stream, frames after them and all, whether EOF comes with the last byte or later.
    const std::vector<uint8_t> one = file("cbr128_joint_44k.mp3");
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
        const Decoded want = reference(s);
        uint8_t ea = 0, eb = 0;
        const std::vector<int16_t> a = play_eof(s, false, ea), b = play_eof(s, true, eb);
        std::printf("  %zu damaged: minimp3 %zu samples a side; EOF at once %zu, late %zu\n", damaged,
                    want.frames.size() / 2, a.size() / 2, b.size() / 2);
        CHECK(a == want.frames);
        CHECK(b == want.frames);
        CHECK_EQ(ea, SoundCard::STATE_ENDED);
        CHECK_EQ(eb, SoundCard::STATE_ENDED);
    }
}

TEST(files_back_to_back_with_tags_between_play_whole)
{
    // File A, its ID3v1 record ("TAG" and 125 bytes), file B's 80 KB ID3v2 tag of binary cover art (FF FB 90 00 and
    // other frame-like bytes in it), then file B. A's last frame plays by its own length; the record is junk; the
    // tag is skipped by its size, whatever is in it; B plays from its first frame. Written at once, and fed in
    // 40-byte pieces.
    const std::vector<uint8_t> a = file("cbr128_joint_44k.mp3"), b = file("vbr0_stereo_44k.mp3");
    std::vector<uint8_t> bytes = a;
    bytes.insert(bytes.end(), {'T', 'A', 'G'});
    for (unsigned k = 0; k < 125; ++k)
        bytes.push_back(static_cast<uint8_t>(k < 60 ? 'a' + k % 26 : 0));
    const size_t size = 80'000;
    bytes.insert(bytes.end(), {'I', 'D', '3', 3, 0, 0, static_cast<uint8_t>(size >> 21 & 127),
                               static_cast<uint8_t>(size >> 14 & 127), static_cast<uint8_t>(size >> 7 & 127),
                               static_cast<uint8_t>(size & 127)});
    uint32_t r = 77;
    for (size_t k = 0; k < size; ++k)
    {
        r = r * 1'103'515'245u + 12'345u;
        bytes.push_back(k % 997 == 0 ? 0xFF : k % 997 == 1 ? 0xFB : k % 997 == 2 ? 0x90 : static_cast<uint8_t>(r >> 16));
    }
    bytes.insert(bytes.end(), b.begin(), b.end());
    std::vector<int16_t> want = reference(a).frames;
    const std::vector<int16_t> wb = reference(b).frames;
    want.insert(want.end(), wb.begin(), wb.end());
    SoundCard c, d;
    const std::vector<int16_t> once = play_all(c, bytes);
    d.set_tap(true, false); // fed slower than it plays, dry now and then: what played only
    start_mp3(d);
    size_t at = 0;
    for (unsigned ms = 0; ms < 20'000 && state(d) != SoundCard::STATE_ENDED; ++ms)
    {
        for (size_t k = 0; k < 40 && at < bytes.size(); ++k)
            d.write(SoundCard::PCMDATA, bytes[at++]);
        if (at == bytes.size())
            d.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        run(d, SECOND / 1'000);
    }
    const std::vector<int16_t> fed = d.take_tap();
    std::printf("  A then B, %zu samples a side wanted: at once %zu, fed %zu\n", want.size() / 2, once.size() / 2,
                fed.size() / 2);
    CHECK(once == want);
    CHECK(fed == want);
    CHECK_EQ(state(d), SoundCard::STATE_ENDED);
}

TEST(a_frame_whole_again_after_others_decode_doesnt_wait_for_a_write)
{
    // VBR. The card waits at some FIFO count for the last byte of a large frame k; the program then writes exactly
    // the bytes of frames k to k+j-1 still missing, which decode, and the count comes back to what it was, now
    // with a smaller frame k+j whole and its next header in. It must play without another write: bytes taken out
    // of the FIFO are news to the wait too. Two writes against the same bytes in one, no EOF either way.
    unsigned cases = 0, stalled = 0;
    for (const char *name : {"vbr0_stereo_44k.mp3", "vbr9_mono_22k.mp3"})
    {
        const std::vector<uint8_t> f = file(name);
        const std::vector<size_t> heads = frame_starts(f);
        for (size_t k = 10; k + 6 < heads.size() && heads[k] <= 40'000; ++k)
        {
            if (heads[k] < 4'096 + 2'000)
                continue;
            for (size_t j = 1; j <= 3; ++j)
            {
                const size_t fbk = heads[k + 1] - heads[k], fbn = heads[k + j + 1] - heads[k + j];
                if (fbn + 4 >= fbk)
                    continue;
                const size_t x = heads[k] + fbk - 1, y = heads[k + j] - heads[k];
                ++cases;
                const auto play = [&](std::initializer_list<std::pair<size_t, size_t>> writes) {
                    SoundCard c;
                    c.set_tap(true, false);
                    start_mp3(c);
                    for (const auto &[from, to] : writes)
                    {
                        for (size_t i = from; i < to; ++i)
                            c.write(SoundCard::PCMDATA, f[i]);
                        for (uint64_t t = 0; t < 2 * SECOND; t += 997)
                            c.tick(997);
                    }
                    return c.take_tap().size() / 2;
                };
                stalled += play({{0, x}, {x, x + y}}) != play({{0, x + y}, {0, 0}});
            }
        }
    }
    std::printf("  %u cases, %u stalled\n", cases, stalled);
    CHECK(cases >= 4);
    CHECK_EQ(stalled, 0u);
}

TEST(at_eof_a_frame_near_a_windows_end_still_plays)
{
    // A false header, then text, sized so the file's first frame starts just before the decoder's first 32 KB
    // window ends (its next header past it). At EOF the search mustn't drop the whole window: the frame and the
    // whole file play, with EOF at once or late, as minimp3 on the whole.
    const std::vector<uint8_t> f = file("cbr128_joint_44k.mp3");
    const std::vector<size_t> heads = frame_starts(f);
    const size_t fb = heads[1] - heads[0];
    for (const size_t back : {fb + 3, fb + 4, fb + 40})
    {
        std::vector<uint8_t> s = {0xFF, 0xFB, 0x90, 0x00};
        while (s.size() < 32'768 - back - heads[0])
            s.push_back(static_cast<uint8_t>('a' + s.size() % 26));
        s.insert(s.end(), f.begin(), f.end());
        const Decoded want = reference(s);
        uint8_t ea = 0, eb = 0;
        const std::vector<int16_t> a = play_eof(s, false, ea), b = play_eof(s, true, eb);
        std::printf("  the frame %zu bytes before the window's end: minimp3 %zu, EOF at once %zu, late %zu\n", back,
                    want.frames.size() / 2, a.size() / 2, b.size() / 2);
        CHECK(a == want.frames);
        CHECK(b == want.frames);
    }
}

TEST(a_tag_header_must_be_a_real_one_and_tags_alone_end_in_81)
{
    // "ID3" with version 5, or undefined flags, or a footer flag on 2.3, isn't a tag: its bytes are junk, and the
    // file after it still plays. And tags adding up to 8 MB with no frame are $81.
    const std::vector<uint8_t> mp3 = file("cbr32_mono_22k.mp3");
    const Decoded want = reference(mp3);
    for (const auto &bad : {std::vector<uint8_t>{'I', 'D', '3', 5, 0, 0, 0, 0, 0x10, 0},
                            std::vector<uint8_t>{'I', 'D', '3', 3, 0, 0x01, 0, 0, 0x10, 0},
                            std::vector<uint8_t>{'I', 'D', '3', 3, 0, 0x10, 0, 0, 0x10, 0}})
    {
        std::vector<uint8_t> s = bad;
        s.insert(s.end(), mp3.begin(), mp3.end()); // a "tag" declaring 2 KB: if taken, it would swallow frames
        SoundCard c;
        CHECK(play_all(c, s) == want.frames);
    }
    SoundCard c;
    start_mp3(c);
    size_t written = 0;
    const size_t body = 64'000;
    for (unsigned k = 0; k < 140 && state(c) != SoundCard::STATE_NO_SYNC; ++k) // 140 tags of 64 KB: past 8 MB
    {
        const uint8_t h[10] = {'I', 'D', '3', 4, 0, 0, static_cast<uint8_t>(body >> 21 & 127),
                               static_cast<uint8_t>(body >> 14 & 127), static_cast<uint8_t>(body >> 7 & 127),
                               static_cast<uint8_t>(body & 127)};
        for (const uint8_t b : h)
            c.write(SoundCard::PCMDATA, b);
        for (size_t i = 0; i < body; ++i)
            c.write(SoundCard::PCMDATA, static_cast<uint8_t>('a' + i % 26));
        written += 10 + body;
        run(c, SECOND / 50);
    }
    std::printf("  tags alone: STATE $%02X after %zu KB\n", state(c), written / 1'024);
    CHECK_EQ(state(c), SoundCard::STATE_NO_SYNC);
    CHECK(written >= (8u << 20));
}

TEST(a_short_run_of_frames_between_junk_is_judged_as_minimp3_judges_it)
{
    // Text, a run of frames cut from cbr320 (from 3 to 12 of them), text, then the whole of cbr128. minimp3 on the
    // whole takes a sync only after ten frames in step (or the data's end): a shorter run between junk isn't one.
    // Fed in pieces the card mustn't take it either, from the one match a short look gives: it waits until the
    // run decides. At once, fed in 300- and 40-byte pieces, EOF early or late: all as minimp3 on the whole.
    const std::vector<uint8_t> hi = file("cbr320_stereo_48k.mp3"), lo = file("cbr128_joint_44k.mp3");
    const std::vector<size_t> heads = frame_starts(hi);
    const std::vector<uint8_t> text = [] {
        std::vector<uint8_t> t;
        for (unsigned k = 0; k < 700; ++k)
            t.push_back(static_cast<uint8_t>('a' + k % 26));
        return t;
    }();
    unsigned bad = 0;
    for (const size_t run_of : {size_t{3}, size_t{9}, size_t{10}, size_t{12}})
    {
        std::vector<uint8_t> s = text;
        s.insert(s.end(), hi.begin() + static_cast<std::ptrdiff_t>(heads[2]),
                 hi.begin() + static_cast<std::ptrdiff_t>(heads[2 + run_of]));
        s.insert(s.end(), text.begin(), text.end());
        s.insert(s.end(), lo.begin(), lo.end());
        const Decoded want = reference(s);
        uint8_t ea = 0, eb = 0;
        const std::vector<int16_t> once = play_eof(s, false, ea), late = play_eof(s, true, eb);
        std::vector<std::vector<int16_t>> fed;
        for (const size_t piece : {size_t{300}, size_t{40}})
        {
            SoundCard c;
            c.set_tap(true, false);
            start_mp3(c);
            size_t at = 0;
            for (unsigned ms = 0; ms < 20'000 && state(c) != SoundCard::STATE_ENDED; ++ms)
            {
                for (size_t k = 0; k < piece && at < s.size(); ++k)
                    c.write(SoundCard::PCMDATA, s[at++]);
                if (at == s.size())
                    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
                run(c, SECOND / 1'000);
            }
            fed.push_back(c.take_tap());
        }
        const bool ok = once == want.frames && late == want.frames && fed[0] == want.frames && fed[1] == want.frames;
        std::printf("  a run of %zu frames: minimp3 %zu samples a side; at once %zu, late %zu, fed %zu and %zu\n",
                    run_of, want.frames.size() / 2, once.size() / 2, late.size() / 2, fed[0].size() / 2,
                    fed[1].size() / 2);
        bad += !ok;
    }
    CHECK_EQ(bad, 0u);
}

TEST(fed_at_once_and_late_eof_agree_on_cut_and_spliced_streams)
{
    // A fixed slice of the review's differential fuzzer: our files whole and cut, tags of our own (binary, some
    // with footers), text junk, $1A padding, ID3v1 and APE trailers, joined at random. Played written at once with
    // EOF, at once with EOF late, and fed in random pieces with random ticks and pauses: the same samples played,
    // and the same end. Whole files alone, end to end, are also minimp3 on the whole. (With junk or a cut after
    // the last frame they aren't, by design: the card plays that frame by its own length, which minimp3 drops.)
    const char *names[] = {"cbr320_stereo_48k.mp3", "cbr128_joint_44k.mp3", "cbr32_mono_22k.mp3",
                           "cbr8_mono_8k.mp3",      "vbr0_stereo_44k.mp3",  "vbr9_mono_22k.mp3"};
    std::vector<std::vector<uint8_t>> files;
    for (const char *n : names)
        files.push_back(file(n));
    unsigned bad = 0, badref = 0;
    const unsigned iterations = std::getenv("MP3_FUZZ") ? static_cast<unsigned>(std::atoi(std::getenv("MP3_FUZZ"))) : 40;
    for (unsigned it = 0; it < iterations; ++it)
    {
        std::mt19937 rng(7'919u + it);
        const auto R = [&](unsigned n) { return static_cast<unsigned>(rng() % n); };
        std::vector<uint8_t> s;
        bool clean = true;
        for (unsigned p = 0, parts = 1 + R(4); p < parts; ++p)
        {
            if (R(3) == 0)
            {
                const unsigned size = R(4) == 0 ? 70'000 + R(100'000) : R(3'000);
                const bool footer = R(4) == 0;
                s.insert(s.end(), {'I', 'D', '3', 4, 0, static_cast<uint8_t>(footer ? 0x10 : 0),
                                   static_cast<uint8_t>(size >> 21 & 127), static_cast<uint8_t>(size >> 14 & 127),
                                   static_cast<uint8_t>(size >> 7 & 127), static_cast<uint8_t>(size & 127)});
                for (unsigned k = 0; k < size; ++k)
                    s.push_back(R(6) == 0 ? 0xFF : R(6) == 0 ? static_cast<uint8_t>(0xE0 | R(32)) : static_cast<uint8_t>(R(256)));
                if (footer)
                    s.insert(s.end(), {'3', 'D', 'I', 4, 0, 0x10, static_cast<uint8_t>(size >> 21 & 127),
                                       static_cast<uint8_t>(size >> 14 & 127), static_cast<uint8_t>(size >> 7 & 127),
                                       static_cast<uint8_t>(size & 127)});
                clean = false;
            }
            const std::vector<uint8_t> &f = files[R(6)];
            if (R(3) == 0)
            {
                const size_t a = R(static_cast<unsigned>(f.size()) / 2), b = a + R(static_cast<unsigned>(f.size() - a));
                s.insert(s.end(), f.begin() + static_cast<std::ptrdiff_t>(a), f.begin() + static_cast<std::ptrdiff_t>(b));
                clean = false;
            }
            else
                s.insert(s.end(), f.begin(), f.end());
            if (R(5) == 0)
            {
                for (unsigned k = 0, n = R(2'000); k < n; ++k)
                    s.push_back(static_cast<uint8_t>('a' + R(26)));
                clean = false;
            }
        }
        const unsigned trail = R(4);
        if (trail == 1)
            s.insert(s.end(), 1 + R(127), uint8_t{0x1A});
        else if (trail == 2)
        {
            s.insert(s.end(), {'T', 'A', 'G'});
            for (unsigned k = 0; k < 125; ++k)
                s.push_back(static_cast<uint8_t>(k < 60 ? 'a' + R(26) : 0));
        }
        else if (trail == 3)
        {
            const char ape[] = "APETAGEX";
            s.insert(s.end(), ape, ape + 8);
            for (unsigned k = 0, n = 24 + R(400); k < n; ++k)
                s.push_back(static_cast<uint8_t>(R(4) ? 'a' + R(26) : R(256)));
        }
        clean = clean && !trail;
        uint8_t ea = 0, eb = 0;
        const std::vector<int16_t> a = play_eof(s, false, ea), b = play_eof(s, true, eb);
        // Fed: random pieces, random ticks, pauses now and then; EOF with the last byte.
        SoundCard c;
        c.set_tap(true, false);
        start_mp3(c);
        size_t at = 0;
        bool paused = false;
        for (unsigned step = 0; step < 400'000; ++step)
        {
            const unsigned piece = R(10) == 0 ? R(9'000) : R(300);
            for (unsigned k = 0; k < piece && at < s.size() && c.fifo_bytes() < SoundCard::FIFO_BYTES; ++k)
                c.write(SoundCard::PCMDATA, s[at++]);
            if (R(50) == 0)
                paused = !paused;
            if (at == s.size())
                paused = false;
            c.write(SoundCard::PCMCTL, static_cast<uint8_t>(SoundCard::PCMCTL_RUN |
                                                             (at == s.size() ? SoundCard::PCMCTL_EOF : 0) |
                                                             (paused ? SoundCard::PCMCTL_PAUSE : 0)));
            c.tick(R(20) == 0 ? R(static_cast<unsigned>(SECOND / 5)) : 1 + R(3'000));
            if (state(c) == SoundCard::STATE_ENDED || state(c) == SoundCard::STATE_NO_SYNC)
                break;
        }
        const std::vector<int16_t> f = c.take_tap();
        const bool same = a == b && a == f && ea == eb && ea == state(c);
        bad += !same;
        if (clean)
            badref += a != reference(s).frames;
        if (!same)
            std::printf("  iteration %u: at once %zu (%02X), late %zu (%02X), fed %zu (%02X)\n", it, a.size() / 2, ea,
                        b.size() / 2, eb, f.size() / 2, state(c));
    }
    std::printf("  %u streams: %u disagree, %u clean ones not minimp3's\n", iterations, bad, badref);
    CHECK_EQ(bad, 0u);
    CHECK_EQ(badref, 0u);
}

int main()
{
    return run_all();
}
