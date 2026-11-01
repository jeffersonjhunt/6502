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
 * jam, and it must gate a voice with the volume up within ten seconds
 * (some open with seconds of silence). RSIDs and PSIDs that install their
 * own interrupt are run too, to show they don't jam; many need more of the
 * VIC-II than the card's frame interrupt, so silence isn't a failure for
 * them. RSIDs that are BASIC programs, which the card can't run, are
 * skipped and counted. Without the fixtures it skips.
 */

#include "harness.h"
#include "test_support.h"

#include "sid_file.h"
#include "sound_card.h"

#include <algorithm>
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
    };

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
        start_sid(card, *plan);
        uint64_t from = 0; // the first frame's
        bool first = false;
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
            for (unsigned chip = 0; chip < 2; ++chip) // some 2SID tunes start on B alone
            {
                const auto &regs = card.sid(chip);
                for (unsigned v = 0; v < 3; ++v)
                    if ((regs[v * 7 + 4] & Sid::GATE) && (regs[v * 7 + 4] & 0xF0) && (regs[0x18] & 0x0F))
                        r.sounded = true;
            }
        }
        return r;
    }
} // namespace

TEST(a_sample_of_the_tunes_plays_or_at_least_runs)
{
    unsigned played = 0, ran = 0, basic = 0;
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
        const bool cia = f->cia_timed(f->start_song);
        const unsigned want = cia ? 1 : 2 * (f->ntsc() ? 60 : 50) - 1;
        const bool ok = r.error.empty() && !r.jammed && (!f->play || (r.frames >= want && r.sounded));
        if (!ok)
            std::printf("  %s: %s%s, %u frames, %s\n", name.c_str(), r.error.c_str(), r.jammed ? "jammed" : "",
                        r.frames, r.sounded ? "sounded" : "silent");
        CHECK(ok);
        if (f->play)
            ++played;
        else
            ++ran;
    }
    std::printf("  %u tunes played, %u run on their own interrupt, %u skipped as BASIC programs\n", played, ran, basic);
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
