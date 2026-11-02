/**
 * @file test_sid_corpus.cpp
 * @brief Real `.sid` tunes, from the owner's fixtures, on the card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A sample of the `.sid` files under fixtures/SIDs, subfolders included
 * (gitignored: the tunes are their composers'; the owner's HVSC copy is
 * tens of thousands), is played: SID_COUNT of them (12; 0 for all), picked
 * by SID_SEED (printed, so a failure can be run again; random if unset).
 * A PSID with a play routine must be called first within two frames of
 * init's return for a VBI song (a CIA song's timer is the tune's to set, so
 * its first call only has to come); the test sees init return as the 6510
 * fetching the stub's instruction after its JSR (SidPlan::returned). Then
 * play must be called on time for two seconds from that call (VBI: 50 or
 * 60 a second, less one for the host's polling and at most two over; CIA:
 * at least once). Init's own length isn't the card's doing (some take over
 * a second, as on a C64), and the window starts at the first call, not at
 * the start, because the frames init spans with interrupts held off fold
 * into one. Its 6510 must not jam, and it must be heard within ten seconds (some open with seconds of silence): the card's audio,
 * in windows of 1,024 samples at 22,050 Hz, must swing by more than 0.01
 * RMS about its mean in five windows anywhere in the run, so a single click
 * (the volume turned up) isn't a sound but a voice gated apart from its
 * waveform is, and so are volume-register writes repeated (a 6581 plays
 * digis that way) and short staccato notes. RSIDs and
 * PSIDs that install their own interrupt are run too, to show they don't
 * jam; many need more of the VIC-II than the card has, so silence isn't a
 * failure for them. Nor for a PSID that turns the VIC-II's raster interrupt
 * on itself (a write of bit 0 to $D01A, seen on the 6510's bus) and is then
 * never played by the stub, as long as it takes no more than 8 interrupts a
 * frame (a storm, which the card's frame makes of a tune that took $FFFE and
 * can't acknowledge it, fails): it is judged as a play-0 tune, and counted;
 * one the stub does play is judged as any other. RSIDs that are BASIC
 * programs, which plan_sid refuses, are skipped and counted. The rule
 * itself is checked on synthetic tunes and on made-up results, fixtures or
 * not; without the fixtures only the sample is skipped. SID_DIR names
 * another folder of tunes instead of fixtures/SIDs.
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
    /// Where the tunes are: SID_DIR, else fixtures/SIDs.
    const std::string FOLDER = [] {
        const char *dir = std::getenv("SID_DIR");
        return dir && *dir ? std::string(dir) : std::string(EMU_FIXTURE_DIR) + "/SIDs";
    }();

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
        uint64_t from{0};   ///< the first frame, in clocks from the start (0: none); printed only
        int64_t late{-1};   ///< SID clocks from init's return to the first frame (0: it came first; -1: no frame)
        bool sounded{false};
        bool own_raster{false}; ///< it turned the VIC-II's raster interrupt on ($D01A bit 0) itself
        uint64_t irqs{0};       ///< interrupt entries: reads of the vector at $FFFE
        uint64_t clocks{0};     ///< how long it ran
    };

    /// Watches the 6510: a write of bit 0 to $D01A with I/O in, each IRQ
    /// vector pull, and init's return: the opcode fetch at the address in the
    /// stub that init's RTS lands on (SidPlan::returned).
    struct Watch : CycleObserver
    {
        const SoundCard &card;
        const uint16_t returned_to;
        bool raster{false};
        uint64_t irqs{0};
        bool returned{false};
        uint64_t returned_at{0}; ///< in SID clocks
        Watch(const SoundCard &c, uint16_t at) : card(c), returned_to(at) {}
        void on_cycle(const BusCycle &c) override
        {
            if (c.write && c.addr == 0xD01A && (c.data & 0x01) && card.io_visible())
                raster = true;
            if (!c.write && c.vector_pull && c.addr == 0xFFFE)
                ++irqs;
            if (c.sync && c.addr == returned_to && !returned)
                returned = true, returned_at = card.sid_clocks();
        }
    };

    /// Audio: windows of WINDOW samples at RATE; a tune has sounded once LOUD
    /// of them, anywhere in the run, swing by more than LEVEL RMS about their
    /// mean. One click (the volume turned up) makes one or two, so isn't a
    /// sound; volume writes repeated are, as on a 6581 (digis), and so are
    /// staccato notes, whose windows are loud a few at a time.
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
        Watch watch(card, plan->returned);
        card.set_cpu_observer(&watch);
        start_sid(card, *plan);
        bool first = false;
        std::vector<float> buf(2 * WINDOW);
        std::vector<double> window;
        unsigned loud = 0;
        uint64_t t = 0;
        for (; (t < r.from + on_time || (!r.sounded && t < most)) && !r.jammed; t += 256)
        {
            card.tick(256);
            if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
            {
                card.read(SoundCard::MBIN);
                if (!first)
                {
                    first = true, r.from = t;
                    const uint64_t now = card.sid_clocks();
                    r.late = watch.returned ? static_cast<int64_t>(now - watch.returned_at) : 0;
                }
                r.frames += t < r.from + on_time ? 1 : 0;
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
        r.own_raster = watch.raster;
        r.irqs = watch.irqs;
        r.clocks = t;
        return r;
    }

    /// Interrupts a frame a tune may take when judged only on running: a
    /// multi-speed player or a raster split takes a few (8 covers an 8x
    /// tune); a source no one acknowledges, such as the card's frame for a
    /// tune that has taken $FFFE, takes hundreds.
    constexpr uint64_t IRQS_A_FRAME = 8;

    /// Frames the card may take, from init's return, to a VBI song's first
    /// play call: the frame then pending, and play's own run. A CIA song is
    /// judged only on being called: its rate is the tune's timer and play
    /// together (one in the samples keeps the KERNAL's 60 Hz timer but runs
    /// a play of about 8.7 frames, so is called about every 9.2), and CIA 1's
    /// timer itself is checked in test_sound.
    constexpr uint64_t LATE_FRAMES = 2;

    /// The rule. A PSID with a play routine must be called first within
    /// LATE_FRAMES frames of init's return (a VBI song; a CIA song at all) (however long init itself runs, as
    /// on a C64), then on time for the window (a VBI song neither late nor
    /// more than twice over), and must sound; unless the stub never called
    /// it because it took the raster interrupt itself (like a play-0 tune,
    /// which needs more VIC-II than the card has) and its interrupts stayed
    /// in bounds. Every tune must start and not jam.
    bool passes(const SidFile &f, const Result &r)
    {
        if (!r.error.empty() || r.jammed)
            return false;
        const uint32_t frame = f.ntsc() ? SoundCard::FRAME_NTSC : SoundCard::FRAME_PAL;
        if (!f.play)
            return true;
        if (r.own_raster && r.frames == 0)
            return r.irqs <= IRQS_A_FRAME * (r.clocks / frame + 1);
        const bool cia = f.cia_timed(f.start_song);
        const unsigned rate = f.ntsc() ? 60 : 50;
        const unsigned want = cia ? 1 : 2 * rate - 1;
        const bool started = r.late >= 0 && (cia || static_cast<uint64_t>(r.late) <= LATE_FRAMES * frame);
        return r.frames >= want && (cia || r.frames <= 2 * rate + 2) && started && r.sounded;
    }

    /// A PSID: load and init $1000, play $1000 + `play_at`, PAL; VBI, or on the CIA.
    SidFile synthetic(std::vector<uint8_t> code, uint16_t play_at, bool cia = false)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 2, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10, b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = 1, b[0x11] = 1, b[0x15] = cia ? 1 : 0, b[0x77] = 0x14;
        b.insert(b.end(), code.begin(), code.end());
        std::string error;
        const auto f = parse_sid(b, error);
        CHECK(f.has_value());
        return f ? *f : SidFile{};
    }

    void run_rule(const char *what, const SidFile &f, bool want_pass)
    {
        const Result r = play(f, 2ull * SoundCard::PAL_HZ, 10ull * SoundCard::PAL_HZ);
        std::printf("  %s: %u frames from %.2f s (%.2f frames after init), %llu IRQs, %s, %s\n", what, r.frames,
                    static_cast<double>(r.from) / SoundCard::PAL_HZ, static_cast<double>(r.late) / SoundCard::FRAME_PAL,
                    static_cast<unsigned long long>(r.irqs),
                    r.sounded ? "sounded" : "silent", passes(f, r) ? "passes" : "fails");
        CHECK_EQ(passes(f, r), want_pass);
    }

    /// init's start: volume up, voice 1 a gated sawtooth (sustain 15).
    const std::vector<uint8_t> SAW = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4,
                                      0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, 0x21, 0x8D, 0x04, 0xD4};

    std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
} // namespace

TEST(the_rule_fails_a_silent_tune_and_passes_a_sounding_one)
{
    // init: volume up, RTS; play (at $1006): RTS. On time, never a voice:
    // one click, which must not count as a sound.
    run_rule("volume up, no voice", synthetic({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60, 0x60}, 6), false);
    run_rule("a gated sawtooth", synthetic(SAW + std::vector<uint8_t>{0x60, 0x60}, 21), true);
    // Gate and waveform never written together: init gates voice 1 with no
    // waveform (attack 0, sustain 15, release 15); from the third play call,
    // when the attack has long reached full, play writes a sawtooth without
    // the gate, so the voice sounds through its slow release.
    // play: INC $C000, LDA $C000, CMP #3, BCC out, LDA #$20, STA $D404, out: RTS.
    run_rule("gate and waveform written apart",
             synthetic({0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4, 0xA9, 0x00, 0x8D, 0x05, 0xD4, 0xA9, 0xFF,
                        0x8D, 0x06, 0xD4, 0xA9, 0x01, 0x8D, 0x04, 0xD4, 0x60, //
                        0xEE, 0x00, 0xC0, 0xAD, 0x00, 0xC0, 0xC9, 0x03, 0x90, 0x05, 0xA9, 0x20, 0x8D, 0x04, 0xD4, 0x60},
                       26),
             true);
    // init runs about 8 frames (LDX #123: DEY 256 times each) before it
    // returns: the window starts at the first call, so it is on time.
    run_rule("an init of 8 frames",
             synthetic(SAW + std::vector<uint8_t>{0xA2, 0x7B, 0xA0, 0x00, 0x88, 0xD0, 0xFD, 0xCA, 0xD0, 0xF8, 0x60, 0x60}, 31),
             true);
    // ...and so is one of 1.3 s (LDX #0 x 4), as such an init would be on a C64.
    run_rule("an init of 1.3 s",
             synthetic(SAW + std::vector<uint8_t>{0xA9, 0x04, 0x85, 0x40, 0xA2, 0x00, 0xA0, 0x00, 0x88, 0xD0, 0xFD, 0xCA,
                                                  0xD0, 0xF8, 0xC6, 0x40, 0xD0, 0xF2, 0x60, 0x60},
                       39),
             true);
    // A CIA song whose init sets the timer to $FFFF (3.3 frames): its first
    // call comes when its timer says, past the VBI limit. LDA #$FF STA $DC04 STA $DC05,
    // LDA #$11 STA $DC0E (loaded now, so the first period is the slow one).
    run_rule("a CIA song with a slow timer",
             synthetic(SAW + std::vector<uint8_t>{0xA9, 0xFF, 0x8D, 0x04, 0xDC, 0x8D, 0x05, 0xDC, 0xA9, 0x11, 0x8D, 0x0E, 0xDC,
                                                  0x60, 0x60},
                       34, true),
             true);
    // A card that never starts the frames after init: here init turns FRAMEEN off ($DE04 = 0).
    run_rule("no frame after init (FRAMEEN off)", synthetic(SAW + std::vector<uint8_t>{0xA9, 0x00, 0x8D, 0x04, 0xDE, 0x60, 0x60}, 26),
             false);
    // Twice a frame, as the host sees it: play also writes the mailbox and waits
    // 600 clocks for the host to take it. STA $DE00, LDX #120, DEX, BNE, RTS.
    run_rule("two sync points a frame",
             synthetic(SAW + std::vector<uint8_t>{0x60, 0x8D, 0x00, 0xDE, 0xA2, 0x78, 0xCA, 0xD0, 0xFD, 0x60}, 21), false);
    // A tune that takes the raster interrupt itself: init sets $D01A and its
    // own $FFFE (an RTI) and stays with I set, so the stub never plays and no
    // interrupt comes; it runs, so passes.
    run_rule("its own raster interrupt",
             synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0xA9, 0x12, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x4C,
                        0x0F, 0x10, 0x40, 0x60},
                       0x13),
             true);
    // A tune with its own $FFFE entry that passes on KERNAL-style: PHA TXA
    // PHA TYA PHA, JMP ($0314), the stub's handler. It plays once a frame.
    // init: the saw, $FFFE -> $1020, RTS. play ($101F): RTS.
    run_rule("chained $FFFE",
             synthetic(SAW + std::vector<uint8_t>{0xA9, 0x20, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x60, 0x60,
                                                  0x48, 0x8A, 0x48, 0x98, 0x48, 0x6C, 0x14, 0x03},
                       31),
             true);
    // A tune whose own $FFFE handler (an RTI) neither passes on to $0314 nor
    // clears $DE03: the pending frame storms it, a limit of the card.
    run_rule("its own raster interrupt, storming",
             synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0xA9, 0x13, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x58, 0x4C,
                        0x10, 0x10, 0x40, 0x60},
                       0x14),
             false);
    run_rule("its own raster interrupt, jamming",
             synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0xA9, 0x11, 0x8D, 0xFE, 0xFF, 0xA9, 0x10, 0x8D, 0xFF, 0xFF, 0x02,
                        0x40, 0x60},
                       0x12),
             false);
    run_rule("$D01A on, played, silent", synthetic({0xA9, 0x01, 0x8D, 0x1A, 0xD0, 0x60, 0x60}, 6), false);
}

TEST(the_rule_bounds_a_vbi_songs_first_call_after_init)
{
    // Results as play() would make them: on time, heard; only `late` differs.
    const SidFile f = synthetic({0x60, 0x60}, 1);
    Result r;
    r.frames = 101, r.sounded = true, r.irqs = 101, r.clocks = 2 * SoundCard::PAL_HZ;
    r.late = static_cast<int64_t>(1.9 * SoundCard::FRAME_PAL);
    CHECK(passes(f, r));
    r.late = 3 * SoundCard::FRAME_PAL;
    CHECK(!passes(f, r));
    r.late = -1; // never a call after init
    CHECK(!passes(f, r));
    r.late = 3 * SoundCard::FRAME_PAL; // a CIA song: its timer's doing
    SidFile cia = f;
    cia.speed = 1;
    CHECK(passes(cia, r));
}

TEST(a_sample_of_the_tunes_plays_or_at_least_runs)
{
    const std::vector<std::string> all = tunes();
    if (all.empty())
    {
        std::printf("  SKIP: no .sid files in %s\n", FOLDER.c_str());
        return;
    }
    unsigned played = 0, ran = 0, basic = 0, raster = 0;
    double latest_vbi = 0, latest_cia = 0; // frames from init's return to the first call
    std::string latest_vbi_name, latest_cia_name;
    for (const std::string &name : sample(all))
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
        if (f->rsid && (f->flags & SidFile::FLAG_BASIC)) // the card has no C64 BASIC
        {
            CHECK(!plan_sid(*f, 0, error)); // refused, so skipping it hides nothing
            ++basic;
            continue;
        }
        const uint32_t hz = f->ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ;
        const Result r = play(*f, 2ull * hz, 10ull * hz);
        const bool ok = passes(*f, r);
        if (!ok)
            std::printf("  %s: %s%s, %u frames from %.2f s (%.2f frames after init), %llu IRQs, %s\n", name.c_str(),
                        r.error.c_str(), r.jammed ? "jammed" : "", r.frames, static_cast<double>(r.from) / hz,
                        static_cast<double>(r.late) / (f->ntsc() ? SoundCard::FRAME_NTSC : SoundCard::FRAME_PAL),
                        static_cast<unsigned long long>(r.irqs), r.sounded ? "sounded" : "silent");
        CHECK(ok);
        if (f->play && r.late >= 0 && !(r.own_raster && r.frames == 0))
        {
            const double frames = static_cast<double>(r.late) / (f->ntsc() ? SoundCard::FRAME_NTSC : SoundCard::FRAME_PAL);
            double &most = f->cia_timed(f->start_song) ? latest_cia : latest_vbi;
            if (frames > most)
                most = frames, (f->cia_timed(f->start_song) ? latest_cia_name : latest_vbi_name) = name;
        }
        if (!f->play)
            ++ran;
        else if (r.own_raster && r.frames == 0)
            ++raster;
        else
            ++played;
    }
    std::printf("  %u tunes played, %u run on their own interrupt, %u PSIDs on their own raster interrupt, %u skipped as BASIC programs\n",
                played, ran, raster, basic);
    std::printf("  the first call after init's return: VBI at most %.2f frames (%s), CIA %.2f (%s)\n", latest_vbi,
                latest_vbi_name.c_str(), latest_cia, latest_cia_name.c_str());
}

int main() { return run_all(); }
