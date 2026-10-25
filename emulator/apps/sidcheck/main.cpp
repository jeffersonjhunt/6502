/**
 * @file main.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Play `.sid` files on the emulated sound card, headless, and say how it went.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 *   sidcheck [--seconds N] [--song N] [--dump] FILE.sid...
 *
 * Each file is loaded onto a sound card as a host program would
 * (sid_file.h) and run for N seconds of the SID clock (default 10). There
 * is no sound yet; what it reports is what the tune did to the chips:
 *
 *   OK      the play routine ran every frame and the SIDs were played
 *   PARTIAL played, but it wants a SID the card doesn't have (a third, or
 *           a second away from $D420 and $D500), whose writes go elsewhere
 *   SILENT  the tune ran but never gated a voice with the volume up
 *   STALLED a tune with a play routine, but the frames stopped coming
 *   JAM     the 6510 hit a jam opcode (where, and the instruction)
 *   ERROR   not a .sid file, or one the card can't take at all
 *
 * with the frames played, the voices that sounded, and the I/O the tune
 * reached that the card doesn't have (the VIC-II, CIA 2, timer B), which is
 * usually why an RSID stays silent. --dump writes, after each frame, the
 * SIDs' 25 writable registers in hex, one line a frame, for comparing with
 * a reference player. The exit code is 1 if any file was not OK.
 */

#include "disasm.h"
#include "sid_file.h"
#include "sound_card.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace ooe::emu;

namespace
{
    /// The I/O a tune touches that the card answers with $FF or ignores.
    class Missing : public CycleObserver
    {
    public:
        explicit Missing(const SoundCard &card) : card_(card) {}
        void on_cycle(const BusCycle &c) override
        {
            if (c.addr < 0xD000 || c.addr > 0xDFFF || !card_.io_visible())
                return;
            const char *what = nullptr;
            if (c.addr < 0xD400)
                what = "VIC-II";
            else if (c.addr >= 0xD800 && c.addr < 0xDC00)
                what = "colour RAM";
            else if (c.addr >= 0xDC00 && c.addr < 0xDD00)
            {
                const unsigned r = c.addr & 0x0F;
                if (r != SoundCard::CIA_TA_LO && r != SoundCard::CIA_TA_HI && r != SoundCard::CIA_ICR &&
                    r != SoundCard::CIA_CRA)
                    what = r == 0x06 || r == 0x07 || r == 0x0F ? "CIA 1 timer B" : "CIA 1 ports/TOD";
            }
            else if (c.addr >= 0xDD00 && c.addr < 0xDE00)
                what = "CIA 2";
            else if (c.addr > 0xDE01)
                what = "expansion I/O";
            if (what)
                ++seen_[what];
        }
        std::string summary() const
        {
            std::string s;
            for (const auto &[what, n] : seen_)
                s += (s.empty() ? "" : ", ") + what + " x" + std::to_string(n);
            return s;
        }

    private:
        const SoundCard &card_;
        std::map<std::string, uint64_t> seen_;
    };

    std::string hex(unsigned v, int digits = 4)
    {
        char buf[8];
        std::snprintf(buf, sizeof buf, "$%0*X", digits, v);
        return buf;
    }

    struct Options
    {
        double seconds{10};
        unsigned song{0};
        bool dump{false};
    };

    /// Play one file; false unless it came out OK.
    bool check(const std::string &path, const Options &opt)
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        std::string error;
        const auto file = in ? parse_sid(bytes, error) : std::nullopt;
        if (!in)
            error = "can't read it";
        const auto plan = file ? plan_sid(*file, opt.song, error) : std::nullopt;
        const std::string name = path.substr(path.find_last_of('/') + 1);
        if (!plan)
        {
            std::printf("%-48s ERROR   %s\n", name.c_str(), error.c_str());
            return false;
        }

        SoundCard card;
        const uint32_t hz = file->ntsc() ? SoundCard::NTSC_HZ : SoundCard::PAL_HZ;
        card.set_cpu_hz(hz); // a host tick is one SID clock
        Missing missing(card);
        card.set_cpu_observer(&missing);
        start_sid(card, *plan);

        const auto total = static_cast<uint64_t>(opt.seconds * hz);
        const uint64_t frame_clocks = file->ntsc() ? 17095 : 19656;
        uint64_t frames = 0, last_frame_at = 0, gates = 0, changes = 0;
        unsigned voices = 0, loudest = 0;
        std::array<std::array<uint8_t, SoundCard::SID_REGS>, 2> before{};
        bool jam = false;
        for (uint64_t t = 0, next_look = frame_clocks; t < total; t += 16)
        {
            card.tick(16);
            if (card.peek(SoundCard::STATUS) & SoundCard::STATUS_TO_HOST)
            {
                card.read(SoundCard::MBIN);
                ++frames;
                last_frame_at = t;
                if (opt.dump)
                {
                    std::printf("%6llu ", static_cast<unsigned long long>(frames));
                    for (unsigned chip = 0; chip < (file->sid2 ? 2u : 1u); ++chip)
                        for (unsigned r = 0; r < 0x19; ++r)
                            std::printf("%02X", card.sid(chip)[r]);
                    std::printf("\n");
                }
            }
            if (card.cpu().state() == Cpu6502::State::Stopped)
            {
                jam = true;
                break;
            }
            if (t < next_look)
                continue;
            next_look += frame_clocks; // a look each frame's worth of clocks, frames or no
            for (unsigned chip = 0; chip < 2; ++chip)
            {
                const auto &now = card.sid(chip);
                if (now != before[chip])
                    ++changes;
                const unsigned volume = now[0x18] & 0x0F;
                loudest = volume > loudest ? volume : loudest;
                for (unsigned v = 0; v < 3; ++v)
                {
                    const uint8_t ctrl = now[v * 7 + 4];
                    const bool sounding = (ctrl & Sid::GATE) && (ctrl & 0xF0) && (now[v * 7] | now[v * 7 + 1]);
                    if ((ctrl & Sid::GATE) && !(before[chip][v * 7 + 4] & Sid::GATE))
                        ++gates;
                    if (sounding && volume)
                        voices |= 1u << (chip * 3 + v);
                }
                before[chip] = now;
            }
        }

        std::string verdict, detail;
        const uint64_t expected = static_cast<uint64_t>(opt.seconds * hz) / frame_clocks;
        if (jam)
        {
            const uint16_t pc = static_cast<uint16_t>(card.cpu().regs().pc - 1);
            verdict = "JAM";
            detail = "at " + hex(pc) + " (" + disassemble(Cpu6502::Model::Nmos6502, card.cpu_bus(), pc).text + ")";
        }
        else if (file->play && (frames == 0 || total - last_frame_at > hz))
            verdict = "STALLED";
        else if (!voices)
            verdict = "SILENT";
        else if (file->sid3 || (file->sid2 && file->sid2 != 0xD420 && file->sid2 != 0xD500))
            verdict = "PARTIAL";
        else
            verdict = "OK";
        if (file->play && !file->cia_timed(opt.song ? opt.song : file->start_song))
            detail += (detail.empty() ? "" : "; ") + std::string("frames ") + std::to_string(frames) + "/" +
                      std::to_string(expected);
        else if (file->play)
            detail += (detail.empty() ? "" : "; ") + std::string("CIA plays ") + std::to_string(frames);
        std::string sounded;
        for (unsigned v = 0; v < 6; ++v)
            if (voices >> v & 1)
                sounded += (sounded.empty() ? "" : ",") + std::string(v < 3 ? "A" : "B") + std::to_string(v % 3 + 1);
        detail += (detail.empty() ? "" : "; ") + std::string("voices ") + (sounded.empty() ? "-" : sounded) +
                  "; gates " + std::to_string(gates) + "; changed " + std::to_string(changes) + " frames; volume " +
                  std::to_string(loudest);
        if (const std::string m = missing.summary(); !m.empty())
            detail += "; touched " + m;
        if (verdict != "OK")
            detail += "; PC " + hex(card.cpu().regs().pc);
        for (const auto &w : plan->warnings)
            detail += "; " + w;
        std::printf("%-48s %-7s %s %s\n", name.c_str(), verdict.c_str(), file->rsid ? "RSID" : "PSID", detail.c_str());
        return verdict == "OK";
    }
} // namespace

int main(int argc, char **argv)
{
    Options opt;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc)
            opt.seconds = std::atof(argv[++i]);
        else if (a == "--song" && i + 1 < argc)
            opt.song = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (a == "--dump")
            opt.dump = true;
        else if (a.rfind("--", 0) == 0)
        {
            std::fprintf(stderr, "usage: sidcheck [--seconds N] [--song N] [--dump] FILE.sid...\n");
            return 2;
        }
        else
            files.push_back(a);
    }
    if (files.empty() || opt.seconds <= 0)
    {
        std::fprintf(stderr, "usage: sidcheck [--seconds N] [--song N] [--dump] FILE.sid...\n");
        return 2;
    }
    unsigned ok = 0;
    for (const auto &f : files)
        ok += check(f, opt) ? 1 : 0;
    if (files.size() > 1)
        std::printf("%u of %zu OK\n", ok, files.size());
    return ok == files.size() ? 0 : 1;
}
