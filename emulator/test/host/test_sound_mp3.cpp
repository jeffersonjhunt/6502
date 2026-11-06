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

#include <cstdint>
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

TEST(a_tag_between_frames_is_resynced_past_as_minimp3_does)
{
    // A 1 KB tag half way: minimp3 on the same bytes loses its place there and finds the next frame, its
    // reservoir begun again; the card the same.
    const std::vector<uint8_t> mp3 = file("vbr0_stereo_44k.mp3");
    std::vector<uint8_t> bytes;
    const Decoded whole = reference(mp3);
    // The first frame boundary past the middle, from a decode that stops there.
    size_t cut = 0;
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
    bytes.insert(bytes.end(), mp3.begin(), mp3.begin() + static_cast<std::ptrdiff_t>(cut));
    const std::vector<uint8_t> t = tag(1'000, 3);
    bytes.insert(bytes.end(), t.begin(), t.end());
    bytes.insert(bytes.end(), mp3.begin() + static_cast<std::ptrdiff_t>(cut), mp3.end());
    const Decoded want = reference(bytes);
    SoundCard c;
    const std::vector<int16_t> got = play_all(c, bytes);
    std::printf("  %zu samples a side (the file alone %zu): %s\n", got.size() / 2, whole.frames.size() / 2,
                got == want.frames ? "as minimp3" : "different");
    CHECK(got == want.frames);
    CHECK(want.frames.size() + 3 * 2 * 1'152 >= whole.frames.size()); // at most a few frames lost to the tag
    // Fed in pieces, the tag met before EOF is set: the same.
    SoundCard d;
    const std::vector<int16_t> fed = play_fed(d, bytes, 300);
    std::printf("  fed in 300-byte pieces: %s\n", fed == want.frames ? "as minimp3" : "different");
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
    const std::vector<uint8_t> text = tag(80'000, 4);
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
        CHECK_EQ(static_cast<unsigned>(c.read(SoundCard::RATE) | c.read(SoundCard::RATE + 1) << 8), hz);
        CHECK_EQ(c.read(SoundCard::PCMFMT), fmt);
        CHECK_EQ(static_cast<unsigned>(c.read(SoundCard::KBPS) | c.read(SoundCard::KBPS + 1) << 8), kbps);
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
        const unsigned k = v.read(SoundCard::KBPS) | v.read(SoundCard::KBPS + 1) << 8;
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

int main()
{
    return run_all();
}
