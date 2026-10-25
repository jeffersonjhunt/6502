/**
 * @file test_sid_corpus.cpp
 * @brief Real `.sid` tunes, from the owner's fixtures, on the card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Every file in fixtures/SIDs (gitignored: the tunes are their composers')
 * is played. A PSID with a play routine must be called on time in its
 * first two seconds (VBI: 50 or 60 a second, less one for init; CIA: at
 * least once), its 6510 must not jam, and it must gate a voice with the
 * volume up within ten (some open with seconds of silence). RSIDs and
 * PSIDs that install their own interrupt are run too, to show they don't
 * jam; many need the VIC-II's raster interrupt, which the card lacks, so
 * silence isn't a failure for them. Without the fixtures it skips.
 */

#include "harness.h"
#include "test_support.h"

#include "sid_file.h"
#include "sound_card.h"

#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    const std::string FOLDER = std::string(EMU_FIXTURE_DIR) + "/SIDs";

    std::vector<std::string> tunes()
    {
        std::vector<std::string> names;
        if (DIR *d = opendir(FOLDER.c_str()))
        {
            while (const dirent *e = readdir(d))
            {
                const std::string n = e->d_name;
                if (n.size() > 4 && (n.substr(n.size() - 4) == ".sid" || n.substr(n.size() - 4) == ".SID"))
                    names.push_back(n);
            }
            closedir(d);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    struct Result
    {
        std::string error;
        bool jammed{false};
        unsigned frames{0}; ///< in the first `on_time` clocks
        bool sounded{false};
    };

    /// At least `on_time` clocks, then on until it sounds, to `most`.
    Result play(const SidFile &f, uint64_t on_time, uint64_t most)
    {
        Result r;
        const auto plan = plan_sid(f, 0, r.error);
        if (!plan)
            return r;
        SoundCard card;
        card.set_cpu_hz(f.ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ);
        start_sid(card, *plan);
        for (uint64_t t = 0; (t < on_time || (!r.sounded && t < most)) && !r.jammed; t += 256)
        {
            card.tick(256);
            if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
            {
                card.read(SoundCard::MBIN);
                r.frames += t < on_time ? 1 : 0;
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

TEST(every_tune_plays_or_at_least_runs)
{
    unsigned played = 0, ran = 0;
    for (const std::string &name : tunes())
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
    std::printf("  %u tunes played, %u run on their own interrupt\n", played, ran);
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
