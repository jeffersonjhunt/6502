/**
 * @file test_sound_pcm.cpp
 * @brief Sound card revision 2's PCM stream (hw/sound-rev2.md, step 2).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card alone, its clock set so a 48 kHz frame is a whole number of host
 * cycles: 8- and 16-bit, mono and stereo streams bit for bit at the test tap
 * (the bus before any resampling); the state flow (buffering to 4 KB or EOF,
 * playing, paused, underrun and its recovery, ended, the bad-rate error);
 * FLUSH; a frame split across writes; the FIFO full, PCMDATA's dropped
 * write and DRQ; the POS and FREE latches while both move; /RESET against
 * CTRL RESET; and the same POS, STATE and FREE whatever the audio rate and
 * however the time is ticked. Then on a backplane machine: streams by
 * demand-mode DMA, one longer than the FIFO, which stops the channel by
 * itself and lets it go again as the card plays.
 */

#include "harness.h"

#include "dma_controller.h"
#include "machine.h"
#include "sound_card.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t RATE = 48'000;
    constexpr uint32_t HZ = RATE * 100; ///< the host's clock in these tests: a frame is 100 cycles
    constexpr uint64_t FRAME = HZ / RATE;
    constexpr uint8_t MONO8 = 0, STEREO8 = SoundCard::PCMFMT_STEREO, MONO16 = SoundCard::PCMFMT_16BIT,
                      STEREO16 = SoundCard::PCMFMT_STEREO | SoundCard::PCMFMT_16BIT;

    size_t frame_bytes(uint8_t fmt) { return size_t{fmt & 1 ? 2u : 1u} * (fmt & 2 ? 2u : 1u); }

    /// `n` bytes of a stream, the same for a seed every run.
    std::vector<uint8_t> stream(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> out(n);
        for (uint8_t &b : out)
        {
            seed = seed * 1'103'515'245u + 12'345u;
            b = static_cast<uint8_t>(seed >> 16);
        }
        return out;
    }

    /// What the bus carries for `bytes` in `fmt`: signed 16-bit, left then right; mono on both sides.
    std::vector<int16_t> expected(const std::vector<uint8_t> &bytes, uint8_t fmt)
    {
        std::vector<int16_t> out;
        const size_t fb = frame_bytes(fmt), step = fmt & 2 ? 2 : 1;
        for (size_t f = 0; f + fb <= bytes.size(); f += fb)
        {
            int16_t side[2];
            for (size_t c = 0; c < 2; ++c)
            {
                const size_t i = f + (fmt & 1 ? c * step : 0);
                side[c] = fmt & 2 ? static_cast<int16_t>(bytes[i] | bytes[i + 1] << 8)
                                  : static_cast<int16_t>((static_cast<int>(bytes[i]) - 128) * 256);
            }
            out.push_back(side[0]);
            out.push_back(side[1]);
        }
        return out;
    }

    /// `cycles` host cycles in steps of `chunk`, as a decoder ticks a card.
    void host(SoundCard &c, uint64_t cycles, uint64_t chunk = 7)
    {
        for (; cycles >= chunk; cycles -= chunk)
            c.tick(chunk);
        if (cycles)
            c.tick(cycles);
    }

    void start(SoundCard &c, uint8_t fmt, uint16_t rate = RATE)
    {
        c.write(SoundCard::RATE, static_cast<uint8_t>(rate));
        c.write(SoundCard::RATE + 1, static_cast<uint8_t>(rate >> 8));
        c.write(SoundCard::PCMFMT, fmt);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    }

    void put(SoundCard &c, const std::vector<uint8_t> &bytes, size_t from = 0, size_t to = SIZE_MAX)
    {
        for (size_t i = from; i < bytes.size() && i < to; ++i)
            c.write(SoundCard::PCMDATA, bytes[i]);
    }

    uint32_t pos(SoundCard &c)
    {
        const uint32_t lo = c.read(SoundCard::POS); // latches the rest
        const uint32_t b1 = c.read(SoundCard::POS + 1), b2 = c.read(SoundCard::POS + 2), b3 = c.read(SoundCard::POS + 3);
        return lo | b1 << 8 | b2 << 16 | b3 << 24;
    }

    unsigned free_units(SoundCard &c)
    {
        const unsigned lo = c.read(SoundCard::FREE); // first: it latches the high byte
        const unsigned hi = c.read(SoundCard::FREE + 1);
        return lo | hi << 8;
    }

    uint8_t state(const SoundCard &c) { return c.peek(SoundCard::STATE); }
} // namespace

TEST(each_format_by_cpu_writes_comes_out_bit_exact_at_the_tap)
{
    for (const uint8_t fmt : {MONO8, STEREO8, MONO16, STEREO16})
    {
        std::printf("  format %u\n", fmt);
        SoundCard c;
        c.set_cpu_hz(HZ);
        c.set_tap(true);
        const std::vector<uint8_t> bytes = stream(3'001 * frame_bytes(fmt), 7 + fmt);
        start(c, fmt);
        put(c, bytes);
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        host(c, 3'010 * FRAME);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        CHECK(c.take_tap() == expected(bytes, fmt));
        CHECK_EQ(pos(c), 3'001u);
        CHECK_EQ(c.underruns(), 0u);
        CHECK_EQ(free_units(c), 512u);
    }
}

TEST(buffering_lasts_to_4_kb_and_playing_keeps_the_rate)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    CHECK_EQ(state(c), SoundCard::STATE_IDLE);
    start(c, MONO8);
    CHECK_EQ(state(c), SoundCard::STATE_BUFFERING);
    const std::vector<uint8_t> bytes = stream(8'000, 1);
    put(c, bytes, 0, 4'095);
    host(c, 50 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_BUFFERING); // 4,095 bytes, and time passing, play nothing
    CHECK_EQ(pos(c), 0u);
    put(c, bytes, 4'095, 4'096);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    host(c, FRAME - 1);
    CHECK_EQ(pos(c), 0u);                           // the first frame a whole frame after the start
    host(c, 1);
    CHECK_EQ(pos(c), 1u);
    host(c, 999 * FRAME);
    CHECK_EQ(pos(c), 1'000u);
    CHECK_EQ(c.fifo_bytes(), 3'096u);
    CHECK_EQ(c.read(SoundCard::RATE) | c.read(SoundCard::RATE + 1) << 8, RATE); // the rate playing
}

TEST(eof_starts_a_short_stream_and_ends_it)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.set_tap(true);
    start(c, STEREO16);
    const std::vector<uint8_t> bytes = stream(40 * 4 + 3, 5); // and a partial frame, never played
    put(c, bytes);
    CHECK_EQ(state(c), SoundCard::STATE_BUFFERING);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    host(c, 40 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);   // the last frame played; none due yet
    host(c, FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    CHECK_EQ(pos(c), 40u);
    CHECK(c.take_tap() == expected(bytes, STEREO16));
    CHECK_EQ(c.underruns(), 0u);
    host(c, 100 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    c.write(SoundCard::PCMCTL, 0);
    CHECK_EQ(state(c), SoundCard::STATE_IDLE);
    CHECK_EQ(c.fifo_bytes(), 3u);                   // clearing RUN keeps the FIFO
}

TEST(underrun_is_silence_until_data_comes_and_counts_once)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.set_tap(true);
    start(c, MONO16);
    const std::vector<uint8_t> bytes = stream(2 * 2'100, 9);
    put(c, bytes, 0, 2 * 2'048); // 4 KB: playing
    host(c, 2'048 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);   // empty, but no frame due yet
    host(c, FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
    host(c, 9 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
    CHECK_EQ(c.underruns(), 1u);
    CHECK_EQ(pos(c), 2'048u);                       // silence isn't counted
    put(c, bytes, 2 * 2'048, 2 * 2'048 + 1);        // half a frame: still dry
    host(c, 3 * FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
    put(c, bytes, 2 * 2'048 + 1);                   // the rest: on again at once
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    host(c, 52 * FRAME);
    CHECK_EQ(pos(c), 2'100u);
    CHECK_EQ(c.underruns(), 1u);
    host(c, FRAME);
    CHECK_EQ(c.underruns(), 2u);                    // a second time dry, a second count
    // The tap: the stream, with silence where it ran dry, the split frame whole.
    std::vector<int16_t> want = expected(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 2 * 2'048), MONO16);
    want.insert(want.end(), 2 * 13, int16_t{0}); // 1 + 9 + 3 frames dry
    const std::vector<int16_t> rest = expected(std::vector<uint8_t>(bytes.begin() + 2 * 2'048, bytes.end()), MONO16);
    want.insert(want.end(), rest.begin(), rest.end());
    want.insert(want.end(), 2, int16_t{0});
    CHECK(c.take_tap() == want);
}

TEST(a_fifo_refilled_between_frames_never_underruns)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    start(c, STEREO16);
    const std::vector<uint8_t> bytes = stream(4 * 3'000, 3);
    put(c, bytes, 0, 4'096);
    host(c, 1'024 * FRAME); // played to empty; no frame due yet
    CHECK_EQ(c.fifo_bytes(), 0u);
    for (size_t f = 1'024; f < 3'000; ++f) // each frame put just before it falls due
    {
        put(c, bytes, 4 * f, 4 * f + 4);
        host(c, FRAME);
        if (c.fifo_bytes() != 0)
            break;
    }
    CHECK_EQ(pos(c), 3'000u);
    CHECK_EQ(c.underruns(), 0u);
}

TEST(pause_holds_the_place_and_flush_empties_the_fifo)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.set_tap(true);
    start(c, STEREO8);
    const std::vector<uint8_t> bytes = stream(2 * 3'000, 4);
    put(c, bytes);
    host(c, 100 * FRAME);
    CHECK_EQ(pos(c), 100u);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
    CHECK_EQ(state(c), SoundCard::STATE_PAUSED);
    host(c, 500 * FRAME);
    CHECK_EQ(pos(c), 100u);
    CHECK_EQ(c.fifo_bytes(), 2u * 2'900);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    host(c, 50 * FRAME);
    CHECK_EQ(pos(c), 150u);
    CHECK(c.take_tap() == expected(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 2 * 150), STEREO8));
    // FLUSH, RUN kept: the FIFO and the decoder empty, so the next frame due finds nothing.
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_FLUSH);
    CHECK_EQ(c.read(SoundCard::PCMCTL), SoundCard::PCMCTL_RUN);
    CHECK_EQ(c.fifo_bytes(), 0u);
    CHECK_EQ(free_units(c), 512u);
    host(c, FRAME);
    CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
    // Paused while buffering goes back to buffering.
    c.write(SoundCard::PCMCTL, 0);
    start(c, STEREO8);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
    CHECK_EQ(state(c), SoundCard::STATE_PAUSED);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    CHECK_EQ(state(c), SoundCard::STATE_BUFFERING);
}

TEST(a_rate_out_of_range_is_error_80_at_run)
{
    for (const auto &[rate, codec, want] :
         {std::tuple<uint16_t, uint8_t, uint8_t>{7'999, 0, SoundCard::STATE_BAD_FORMAT},
          {8'000, 0, SoundCard::STATE_BUFFERING},
          {48'000, 0, SoundCard::STATE_BUFFERING},
          {48'001, 0, SoundCard::STATE_BAD_FORMAT},
          {0, 0, SoundCard::STATE_BAD_FORMAT},
          {44'100, 1, SoundCard::STATE_BAD_FORMAT}}) // MP3: not yet (step 4)
    {
        SoundCard c;
        c.write(SoundCard::CODEC, codec);
        start(c, MONO8, rate);
        CHECK_EQ(state(c), want);
        put(c, stream(5'000, 1));
        host(c, 1'000'000);
        if (want == SoundCard::STATE_BAD_FORMAT)
        {
            CHECK_EQ(state(c), SoundCard::STATE_BAD_FORMAT); // nothing plays, and it stays
            CHECK_EQ(pos(c), 0u);
        }
        else
            CHECK(pos(c) > 0);
        c.write(SoundCard::PCMCTL, 0);
        CHECK_EQ(state(c), SoundCard::STATE_IDLE);
    }
}

TEST(the_fifo_full_drops_pcmdata_and_lets_go_of_drq)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    c.write(SoundCard::DRQSEL, SoundCard::DRQ_PCMDATA);
    CHECK(c.drq());
    for (size_t i = 0; i < SoundCard::FIFO_BYTES; ++i)
        c.dack_write(static_cast<uint8_t>(i), false);
    CHECK(!c.drq());
    CHECK_EQ(free_units(c), 0u);
    c.write(SoundCard::PCMDATA, 0xAA); // dropped
    c.dack_write(0xAA, false);
    CHECK_EQ(c.fifo_bytes(), SoundCard::FIFO_BYTES);
    start(c, MONO8);
    host(c, FRAME);
    CHECK(c.drq());                    // a frame played: a byte of room
    CHECK_EQ(c.fifo_bytes(), SoundCard::FIFO_BYTES - 1);
    host(c, 255 * FRAME);
    CHECK_EQ(free_units(c), 1u);
    // What was dropped never reaches the bus: played out, the stream ends on its own last byte.
    c.set_tap(true);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    host(c, SoundCard::FIFO_BYTES * FRAME, 1'000);
    CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    const std::vector<int16_t> tap = c.take_tap();
    REQUIRE(tap.size() == 2 * (SoundCard::FIFO_BYTES - 256));
    bool same = true;
    for (size_t f = 0; f < tap.size() / 2; ++f) // byte i was i & $FF, unsigned 8-bit
        same = same && tap[2 * f] == static_cast<int16_t>((static_cast<int>((f + 256) & 0xFF) - 128) * 256);
    CHECK(same);
}

TEST(pos_and_free_read_through_their_latches_while_both_move)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    start(c, MONO8);
    put(c, stream(SoundCard::FIFO_BYTES, 2));
    // FREE: $4E keeps what the last $4D read latched.
    CHECK_EQ(c.read(SoundCard::FREE), 0x00); // full: 0, and $4E latched at 0
    host(c, 300 * FRAME);                    // 300 bytes played: FREE 1 now
    CHECK_EQ(c.peek(SoundCard::FREE), 0x01);
    CHECK_EQ(c.read(SoundCard::FREE + 1), 0x00);
    host(c, 65'536 * FRAME);                 // past 256 units: high byte 1
    CHECK_EQ(c.read(SoundCard::FREE + 1), 0x00); // still the latch
    CHECK_EQ(c.read(SoundCard::FREE), 0x01);  // 65,836 bytes free: 257 units
    CHECK_EQ(c.read(SoundCard::FREE + 1), 0x01);
    // POS: $52-$54 keep what the last $51 read latched.
    const uint8_t lo = c.read(SoundCard::POS);
    const uint32_t at = 300 + 65'536;
    CHECK_EQ(lo, at & 0xFF);
    host(c, 20'000 * FRAME);                 // POS moves on past the next 256s
    CHECK_EQ(c.read(SoundCard::POS + 1), (at >> 8) & 0xFF);
    CHECK_EQ(c.read(SoundCard::POS + 2), (at >> 16) & 0xFF);
    CHECK_EQ(c.peek(SoundCard::POS), (at + 20'000) & 0xFF);
    CHECK_EQ(c.read(SoundCard::POS + 1), (at >> 8) & 0xFF); // a peek latches nothing
    CHECK_EQ(pos(c), at + 20'000);
    c.write(SoundCard::POS, 0);              // zeroes it, and it counts on from there
    host(c, 10 * FRAME);
    CHECK_EQ(pos(c), 10u);
}

TEST(reset_stops_and_empties_the_stream_and_ctrl_reset_leaves_it)
{
    SoundCard c;
    c.set_cpu_hz(HZ);
    start(c, MONO8);
    put(c, stream(6'000, 3));
    host(c, 10 * FRAME);
    c.write(SoundCard::CTRL, SoundCard::CTRL_RESET); // the 6510's reset
    CHECK_EQ(state(c), SoundCard::STATE_PLAYING);
    host(c, 10 * FRAME);
    CHECK_EQ(pos(c), 20u);
    c.reset(); // /RESET
    CHECK_EQ(state(c), SoundCard::STATE_IDLE);
    CHECK_EQ(c.read(SoundCard::PCMCTL), 0);
    CHECK_EQ(c.fifo_bytes(), 0u);
    CHECK_EQ(pos(c), 0u);
    CHECK_EQ(c.underruns(), 0u);
}

namespace
{
    /// A script of writes and time, and what POS, STATE and FREE read after each step.
    std::vector<uint32_t> scripted(uint32_t audio_rate, uint64_t chunk, uint32_t hz)
    {
        SoundCard c;
        c.set_cpu_hz(hz);
        c.set_audio_rate(audio_rate);
        std::vector<uint32_t> seen;
        const auto look = [&] {
            seen.push_back(pos(c));
            seen.push_back(state(c));
            seen.push_back(free_units(c));
        };
        const std::vector<uint8_t> bytes = stream(40'000, 11);
        start(c, STEREO16, 44'100);
        put(c, bytes, 0, 10'000);
        look();
        host(c, hz / 20, chunk); // 50 ms
        look();
        host(c, hz / 10, chunk); // dry by now
        look();
        put(c, bytes, 10'000, 30'000);
        host(c, hz / 50, chunk);
        look();
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_PAUSE);
        host(c, hz / 7, chunk);
        look();
        c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        host(c, hz / 3, chunk);
        look();
        seen.push_back(c.underruns());
        return seen;
    }
} // namespace

TEST(pos_state_and_free_are_the_same_at_audio_rates_0_and_48000_and_however_ticked)
{
    const std::vector<uint32_t> base = scripted(0, 7, SoundCard::NATIVE_HZ);
    CHECK(scripted(48'000, 7, SoundCard::NATIVE_HZ) == base);
    CHECK(scripted(0, 1, SoundCard::NATIVE_HZ) == base);
    CHECK(scripted(48'000, 1'000, SoundCard::NATIVE_HZ) == base);
    // And the time is the host's clock's: at 44.1 kHz, 50 ms (hz / 20 cycles, rounded down) is 2,204.99 frames.
    CHECK_EQ(base[3], SoundCard::NATIVE_HZ / 20 * 44'100ull / SoundCard::NATIVE_HZ);
    CHECK_EQ(base[3], 2'204u);
    CHECK_EQ(base[4], SoundCard::STATE_PLAYING);
    CHECK_EQ(base[7], SoundCard::STATE_UNDERRUN);
    CHECK_EQ(base[16], SoundCard::STATE_ENDED);
    CHECK_EQ(base[15], 30'000u / 4);         // every frame played at the end
    CHECK_EQ(base.back(), 1u);
    // At another host clock, 50 ms is exactly 2,205 frames.
    CHECK_EQ(scripted(0, 7, 1'000'000)[3], 2'205u);
}

// --- on the backplane: demand-mode DMA ---------------------------------------

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

        /// Channel 2: `count` bytes (0: 65,536) from `from` to the card, demand mode.
        void channel(uint32_t from, uint32_t count)
        {
            bus().poke(0xC420, static_cast<uint8_t>(from));
            bus().poke(0xC421, static_cast<uint8_t>(from >> 8));
            bus().poke(0xC422, static_cast<uint8_t>(from >> 16));
            bus().poke(0xC424, static_cast<uint8_t>(count));
            bus().poke(0xC425, static_cast<uint8_t>(count >> 8));
            bus().poke(0xC426, DmaController::DEMAND | DmaController::TO_CARD);
            bus().poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
            bus().poke(0xC427, DmaController::ENABLE);
        }

        bool armed() { return bus().peek(0xC427) & DmaController::ENABLE; }

        /// `cycles` CPU cycles, the devices caught up after each as the run loop's irq() does.
        void run(uint64_t cycles)
        {
            for (uint64_t i = 0; i < cycles; ++i)
            {
                bus().read(0x0000);
                bus().irq();
            }
        }
    };
} // namespace

TEST(each_format_by_demand_dma_comes_out_bit_exact_at_the_tap)
{
    for (const uint8_t fmt : {MONO8, STEREO8, MONO16, STEREO16})
    {
        std::printf("  format %u\n", fmt);
        Board b;
        REQUIRE(b.sound != nullptr);
        SoundCard &c = *b.sound;
        c.set_cpu_hz(HZ);
        c.set_tap(true);
        const std::vector<uint8_t> bytes = stream(1'200 * 4, 21 + fmt); // 4,800: past 4 KB, playing
        for (size_t i = 0; i < bytes.size(); ++i)
            b.bus().write_physical(0x10000 + static_cast<uint32_t>(i), bytes[i]);
        b.bus().write(0xC563, SoundCard::DRQ_PCMDATA);
        start(c, fmt);
        b.channel(0x10000, static_cast<uint32_t>(bytes.size()));
        b.run(1'000);
        CHECK(!b.armed());                     // all of it in: terminal count
        b.bus().write(0xC548, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        b.run((bytes.size() / frame_bytes(fmt) + 2) * FRAME);
        CHECK_EQ(state(c), SoundCard::STATE_ENDED);
        CHECK(c.take_tap() == expected(bytes, fmt));
        CHECK_EQ(c.underruns(), 0u);
    }
}

TEST(demand_dma_longer_than_the_fifo_stops_itself_and_goes_on_as_the_card_plays)
{
    Board b;
    REQUIRE(b.sound != nullptr);
    SoundCard &c = *b.sound;
    c.set_cpu_hz(HZ);
    c.set_tap(true);
    // 192 KB of 16-bit stereo, in three channel loads of 64 KB: 49,152 frames, 1.024 s at 48 kHz.
    const std::vector<uint8_t> bytes = stream(3 * 65'536, 31);
    for (size_t i = 0; i < bytes.size(); ++i)
        b.bus().write_physical(0x10000 + static_cast<uint32_t>(i), bytes[i]);
    b.bus().write(0xC563, SoundCard::DRQ_PCMDATA);
    start(c, STEREO16);
    unsigned loads = 0;
    bool stopped_full = false;
    uint64_t t = 0;
    const uint64_t total = (49'152 + 10) * FRAME;
    while (t < total)
    {
        if (!b.armed() && loads < 3)
            b.channel(0x10000 + 65'536 * loads++, 0); // COUNT 0: 65,536
        if (loads == 3 && !b.armed() && !(c.read(SoundCard::PCMCTL) & SoundCard::PCMCTL_EOF))
            b.bus().write(0xC548, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
        b.run(FRAME);
        t += FRAME;
        if (b.armed() && c.fifo_bytes() == SoundCard::FIFO_BYTES)
            stopped_full = true; // the channel armed, with bytes to go, and the card full: DRQ let go
    }
    CHECK(stopped_full);
    CHECK_EQ(loads, 3u);
    CHECK_EQ(state(c), SoundCard::STATE_ENDED);
    CHECK_EQ(c.underruns(), 0u);
    CHECK_EQ(pos(c), 49'152u);
    CHECK(c.take_tap() == expected(bytes, STEREO16));
}

TEST(an_hour_in_one_tick_is_caught_up_in_bulk)
{
    // A backgrounded tab hands the card a huge tick: the stream catches up at once, the frames it had played and
    // the rest dry, with nothing kept (the tap is off).
    SoundCard c;
    c.set_cpu_hz(SoundCard::NATIVE_HZ);
    start(c, STEREO16);
    put(c, stream(100'000, 6));
    c.tick(uint64_t{SoundCard::NATIVE_HZ} * 3'600);
    CHECK_EQ(pos(c), 25'000u);
    CHECK_EQ(state(c), SoundCard::STATE_UNDERRUN);
    CHECK_EQ(c.underruns(), 1u);
    CHECK(c.take_tap().empty());
    put(c, stream(4, 7));                              // and it plays on
    c.tick(uint64_t{SoundCard::NATIVE_HZ} * 3'600 * 24);
    CHECK_EQ(pos(c), 25'001u);
}

int main()
{
    return run_all();
}
