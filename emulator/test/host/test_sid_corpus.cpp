/**
 * @file test_sid_corpus.cpp
 * @brief Real `.sid` tunes, from the owner's fixtures, on the card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A sample of the `.sid` files under fixtures/SIDs, subfolders included
 * (gitignored: the tunes are their composers'; the owner's HVSC copy is
 * tens of thousands), is played: SID_COUNT of them (12; 0 for all), picked
 * by SID_SEED (printed, so a failure can be run again; random if unset).
 * A PSID with a play routine must be called on time for two seconds from
 * its first call (VBI: 50 or 60 a second, less one for the host's polling;
 * CIA: at least once). The window starts there, not at the start, because
 * init may run for frames (some take eight) with interrupts held off, and
 * those frames' interrupts fold into one, as on a C64. Its 6510 must not
 * jam, and it must be heard within ten seconds (some open with seconds of
 * silence): the card's audio, in windows of 1,024 samples at 22,050 Hz,
 * must swing by more than 0.01 RMS about its mean in five of them, so a
 * click (the volume turned up) isn't a sound but a voice gated apart from
 * its waveform is. RSIDs and PSIDs that install their own interrupt are run
 * too, to show they don't jam; many need more of the VIC-II than the card
 * has, so silence isn't a failure for them. Nor for a PSID that turns the
 * VIC-II's raster interrupt on itself (a write of bit 0 to $D01A, seen on
 * the 6510's bus) and is then never played by the stub: it is judged as a
 * play-0 tune, and counted; one the stub does play is judged as any other.
 * RSIDs that are BASIC programs, which the card can't run, are skipped and
 * counted. Without the fixtures it skips.
 */

#include "harness.h"
#include "test_support.h"

#include "bus_cycle.h"
#include "sid_file.h"
#include "sound_card.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    const std::string FOLDER = std::string(EMU_FIXTURE_DIR) + "/SIDs";

    /// Every tune under FOLDER, as paths relative to it, sorted.
    std::vector<std::string> tunes()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(FOLDER, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            const std::string n = it->path().filename().string();
            if (it->is_regular_file(ec) && n.size() > 4 &&
                (n.substr(n.size() - 4) == ".sid" || n.substr(n.size() - 4) == ".SID"))
                names.push_back(fs::relative(it->path(), FOLDER, ec).string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    unsigned long env_or(const char *name, unsigned long dflt)
    {
        const char *v = std::getenv(name);
        return v && *v ? std::strtoul(v, nullptr, 0) : dflt;
    }

    /// SID_COUNT of `all` (0: every one), chosen by SID_SEED, in path order.
    std::vector<std::string> sample(std::vector<std::string> all)
    {
        const unsigned long count = env_or("SID_COUNT", 12);
        const unsigned long seed = env_or("SID_SEED", std::random_device{}());
        std::printf("  %zu tunes; SID_SEED=%lu SID_COUNT=%lu\n", all.size(), seed, count);
        if (count == 0 || count >= all.size())
            return all;
        std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
        std::shuffle(all.begin(), all.end(), rng);
        all.resize(count);
        std::sort(all.begin(), all.end());
        return all;
    }

    struct Result
    {
        std::string error;
        bool jammed{false};
        unsigned frames{0}; ///< in the `on_time` clocks from the first
        bool sounded{false};
        bool own_raster{false}; ///< it turned the VIC-II's raster interrupt on ($D01A bit 0) itself
    };

    /// Watches the 6510 for a write of bit 0 to $D01A with I/O in.
    struct RasterWatch : CycleObserver
    {
        const SoundCard &card;
        bool seen{false};
        explicit RasterWatch(const SoundCard &c) : card(c) {}
        void on_cycle(const BusCycle &c) override
        {
            if (c.write && c.addr == 0xD01A && (c.data & 0x01) && card.io_visible())
                seen = true;
        }
    };

    /// Audio: windows of WINDOW samples at RATE; a tune has sounded once LOUD
    /// of them (about a quarter second in all) swing by more than LEVEL RMS
    /// about their mean, so turning the volume up, a click, isn't a sound.
    constexpr uint32_t RATE = 22050;
    constexpr size_t WINDOW = 1024;
    constexpr unsigned LOUD = 5;
    constexpr double LEVEL = 0.01;

    /// To `on_time` clocks past the first frame (or past the start, without
    /// one), then on until it sounds, to `most`.
    Result play(const SidFile &f, uint64_t on_time, uint64_t most)
    {
        Result r;
        const auto plan = plan_sid(f, 0, r.error);
        if (!plan)
            return r;
        SoundCard card;
        card.set_cpu_hz(f.ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ);
        card.set_audio_rate(RATE);
        RasterWatch watch(card);
        card.set_cpu_observer(&watch);
        start_sid(card, *plan);
        uint64_t from = 0; // the first frame's
        bool first = false;
        std::vector<float> buf(2 * WINDOW);
        std::vector<double> window;
        unsigned loud = 0;
        for (uint64_t t = 0; (t < from + on_time || (!r.sounded && t < most)) && !r.jammed; t += 256)
        {
            card.tick(256);
            if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
            {
                card.read(SoundCard::MBIN);
                if (!first)
                    first = true, from = t;
                r.frames += t < from + on_time ? 1 : 0;
            }
            r.jammed = card.cpu().state() == Cpu6502::State::Stopped;
            for (size_t n; !r.sounded && (n = card.take_audio(buf.data(), WINDOW)) > 0;)
                for (size_t i = 0; i < n; ++i)
                {
                    window.push_back(double{buf[2 * i]} + buf[2 * i + 1]); // both SIDs: some 2SID tunes start on B alone
                    if (window.size() < WINDOW)
                        continue;
                    double mean = 0, sq = 0;
                    for (const double v : window)
                        mean += v;
                    mean /= WINDOW;
                    for (const double v : window)
                        sq += (v - mean) * (v - mean);
                    loud += std::sqrt(sq / WINDOW) > LEVEL ? 1 : 0;
                    window.clear();
                }
            if (!r.sounded && loud >= LOUD)
            {
                r.sounded = true;
                card.set_audio_rate(0); // heard: the rest needs no sound, which costs
            }
        }
        card.set_cpu_observer(nullptr);
        r.own_raster = watch.seen;
        return r;
    }

    /// The rule. A PSID with a play routine must be called on time and sound,
    /// unless the stub never called it because the tune took the raster
    /// interrupt itself (like a play-0 tune, which needs more VIC-II than the
    /// card has); every tune must start and not jam.
    bool passes(const SidFile &f, const Result &r)
    {
        if (!r.error.empty() || r.jammed)
            return false;
        if (!f.play || (r.own_raster && r.frames == 0))
            return true;
        const unsigned want = f.cia_timed(f.start_song) ? 1 : 2 * (f.ntsc() ? 60 : 50) - 1;
        return r.frames >= want && r.sounded;
    }

    /// A PSID: load and init $1000, play $1000 + `play_at`, PAL.
    SidFile synthetic(std::vector<uint8_t> code, uint16_t play_at)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 2, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10, b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = 1, b[0x11] = 1, b[0x77] = 0x14;
        b.insert(b.end(), code.begin(), code.end());
        std::string error;
        const auto f = parse_sid(b, error);
        CHECK(f.has_value());
        return f ? *f : SidFile{};
    }

    void run_rule(const char *what, const SidFile &f, bool want_pass)
    {
        const Result r = play(f, 2ull * SoundCard::PAL_HZ, 10ull * SoundCard::PAL_HZ);
        std::printf("  %s: %u frames, %s, %s\n", what, r.frames, r.sounded ? "sounded" : "silent",
                    passes(f, r) ? "passes" : "fails");
        CHECK_EQ(passes(f, r), want_pass);
    }
} // namespace

TEST(the_rule_fails_a_silent_tune_and_passes_a_sounding_one)
{
    // init: volume up, RTS; play (at $1006): RTS. On time, never a voice.
    run_rule("volume up, no voice", synthetic({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60, 0x60}, 6), false);
    // init: volume up; voice 1 a sawtooth, gated (sustain 15), RTS; play: RTS.
    run_rule("a gated sawtooth",
             synthetic({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4, 0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, 0x21,
                        0x8D, 0x04, 0xD4, 0x60, 0x60},
                       21),
             true);
    // A voice that sounds with its gate written apart from its waveform: gate
    // alone in init, then sawtooth without gate on each play (attack, then release).
    run_rule("gate and waveform written apart",
             synthetic({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4, 0xA9, 0x0F, 0x8D, 0x06, 0xD4, 0xA9, 0x01,
                        0x8D, 0x04, 0xD4, 0x60, 0xA9, 0x20, 0x8D, 0x04, 0xD4, 0x60},
                       21),
             true);
    // A tune that takes the raster interrupt itself: init sets $D01A and its
    // own $FFFE (an RTI), so the stub never plays; it runs, so passes. The same
    // with a jam fails, and one that turns $D01A on but is played stays judged.
    run_rule("its own raster interrupt",
             synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0xA9, 0x13, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x58, 0x4C,
                        0x10, 0x10, 0x40, 0x60},
                       0x14),
             true);
    run_rule("its own raster interrupt, jamming",
             synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0xA9, 0x11, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x58, 0x02,
                        0x40, 0x60},
                       0x12),
             false);
    run_rule("$D01A on, played, silent", synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0x60, 0x60}, 6), false);
}

TEST(a_sample_of_the_tunes_plays_or_at_least_runs)
{
    unsigned played = 0, ran = 0, basic = 0, raster = 0;
    for (const std::string &name : sample(tunes()))
    {
        std::ifstream in(FOLDER + "/" + name, std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        std::string error;
        const auto f = parse_sid(bytes, error);
        if (!f)
        {
            std::printf("  %s: %s\n", name.c_str(), error.c_str());
            CHECK(f.has_value());
            continue;
        }
        if (f->rsid && (f->flags & SidFile::FLAG_BASIC)) // plan_sid refuses it: the card has no C64 BASIC
        {
            ++basic;
            continue;
        }
        const uint32_t hz = f->ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ;
        const Result r = play(*f, 2ull * hz, 10ull * hz);
        const bool ok = passes(*f, r);
        if (!ok)
            std::printf("  %s: %s%s, %u frames, %s\n", name.c_str(), r.error.c_str(), r.jammed ? "jammed" : "",
                        r.frames, r.sounded ? "sounded" : "silent");
        CHECK(ok);
        if (!f->play)
            ++ran;
        else if (r.own_raster && r.frames == 0)
            ++raster;
        else
            ++played;
    }
    std::printf("  %u tunes played, %u run on their own interrupt, %u PSIDs on their own raster interrupt, %u skipped as BASIC programs\n",
                played, ran, raster, basic);
}

int main()
{
    if (tunes().empty())
    {
        std::printf("SKIP: no .sid files in %s\n", FOLDER.c_str());
        return SKIP;
    }
    return run_all();
}
