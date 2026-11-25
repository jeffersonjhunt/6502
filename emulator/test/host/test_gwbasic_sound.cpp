/**
 * @file test_gwbasic_sound.cpp
 * @brief GW-BASIC's sound, input and events, phase 7f (hw/oodos.md, "The plan, phase 7": block P7): SOUND, BEEP,
 *        PLAY and its traps on sound card revision 3's music sequencer and tone generator, TIMER and ON TIMER, DATE$ and TIME$, STICK, STRIG and their
 *        traps, ON KEY(n) and KEY 15-20, PEN (ON COM(n)'s trap deferred: 7f's answer 24); run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * As test_gwbasic_screen does: oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL into the build tree
 * (EMU_GWBASIC_BUILD, or GWBASIC_BUILD to try another build), OODOS.SYS boots (oodos_rig.h's CcpRig), both files go
 * on M:, and GW-BASIC starts on its own screen (CON:'s output keeps the CRT), its keys pressed on the USB card.
 *
 * PLAY, SOUND, STICK and the traps print nothing PC-BASIC's oracle could compare, so every case reads the machine
 * (7f's plan, "New tests"): the note blocks P7 hands to function 82 (SN_MUSIC's, watched at the BDOS's entry and
 * decoded here: each block's instrument, and its entries as sent, the frequency in Hz and the length and the gate
 * in us), its stops (SN_MCTL), the bell's block (SN_BEEP), the card's firmware (its music state, its tone voice,
 * the music source's output), the RTC set by the rig, the joystick ports and the USB keyboard driven by it. Each expected figure is
 * worked out here from the manual (the IBM BASIC Reference 3.0, for meaning), the source (GWSTS.ASM's PLAY and
 * SOUND) and the owner's answers to 7f's plan; each case names the failure on real hardware it guards and the
 * broken build it was seen failing on.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card.h"
#include "sound_card3.h"
#include "sound_fw.h"
#include "gw_timing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const bool REV3 = (sound_revision() = 3, true); ///< every rig here: sound card revision 3, the card as a device

    const std::string BUILD = std::getenv("GWBASIC_BUILD") ? std::getenv("GWBASIC_BUILD") : EMU_GWBASIC_BUILD;

    std::vector<uint8_t> file(const std::string &name)
    {
        std::ifstream f(BUILD + "/" + name, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    /// A link's labels (ld65's -Ln): the resident core's (gwbasic.lbl) or a block's (p7.lbl).
    std::map<std::string, uint16_t> lbl_map(const std::string &lbl = "gwbasic.lbl")
    {
        std::map<std::string, uint16_t> out;
        std::ifstream f(BUILD + "/" + lbl);
        std::string al, addr, name;
        while (f >> al >> addr >> name)
            if (al == "al" && name.size() > 1 && name[0] == '.')
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(addr, nullptr, 16));
        return out;
    }

    constexpr uint16_t L0_CONFIG = 0xC490, L0_MAP = 0xC491;
    uint8_t reg(CcpRig &r, uint16_t a) { return r.machine->bus().peek(a); }
    uint32_t mapbase(CcpRig &r) { return (reg(r, L0_MAP) | reg(r, L0_MAP + 1) << 8) * 256u; }
    unsigned columns(CcpRig &r) { return (reg(r, L0_CONFIG) & 7) == 1 ? 40 : 80; }
    uint8_t vcard(CcpRig &r, uint32_t a) { return r.machine->video_card()->card_peek(a); }

    /// Row `row` of GW-BASIC's text, its trailing blanks dropped (a $FF as a blank).
    std::string line(CcpRig &r, unsigned row)
    {
        std::string s;
        const uint32_t base = mapbase(r);
        const unsigned cols = columns(r);
        for (unsigned c = 0; c < cols; ++c)
        {
            const char ch = static_cast<char>(vcard(r, base + row * cols * 2 + c * 2));
            s += static_cast<unsigned char>(ch) == 0xFF ? ' ' : ch;
        }
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    }
    std::string screen(CcpRig &r, unsigned rows = 25)
    {
        std::string s;
        for (unsigned i = 0; i < rows; ++i)
            s += line(r, i) + "|";
        return s;
    }
    /// Run until some row holds `text`. @return its row, or -1
    int until_row(CcpRig &r, const std::string &text, uint64_t budget = ms(4000))
    {
        const uint64_t end = r.cpu->cycles() + budget;
        while (r.cpu->cycles() < end)
        {
            for (unsigned i = 0; i < 30; ++i)
                if (line(r, i).find(text) != std::string::npos)
                    return static_cast<int>(i);
            r.run(ms(10));
        }
        std::printf("  never saw '%s' on the screen: %s\n", text.c_str(), screen(r).c_str());
        return -1;
    }

    /// A character's key on a US keyboard: its usage and whether it needs Shift. 0 none.
    std::pair<uint8_t, bool> usage(char ch)
    {
        static const std::string pairs = "1!2@3#4$5%6^7&8*9(0)\r\r\x1b\x1b\b\b\t\t  -_=+[{]}\\|~~;:'\"`~,<.>/?";
        if (ch >= 'a' && ch <= 'z')
            return {static_cast<uint8_t>(0x04 + ch - 'a'), false};
        if (ch >= 'A' && ch <= 'Z') // Caps Lock is on (GW-BASIC's own, at start-up): capitals unshifted
            return {static_cast<uint8_t>(0x04 + ch - 'A'), false};
        for (size_t i = 0; i < pairs.size(); ++i)
            if (pairs[i] == ch && !(i / 2 == 0x32 - 0x1E))
                return {static_cast<uint8_t>(0x1E + i / 2), (i & 1) != 0};
        return {0, false};
    }
    constexpr uint8_t SHIFT = 0xE1, CTRL = 0xE0;
    void press(CcpRig &r, uint8_t use, uint8_t mod = 0, unsigned hold_ms = 20)
    {
        UsbCard *usb = r.machine->usb_card();
        if (mod)
        {
            usb->key(mod, true);
            r.run(ms(10));
        }
        usb->key(use, true);
        r.run(ms(hold_ms));
        usb->key(use, false);
        r.run(ms(20));
        if (mod)
        {
            usb->key(mod, false);
            r.run(ms(10));
        }
    }
    void keys(CcpRig &r, const std::string &text)
    {
        for (const char ch : text)
        {
            const auto [u, shift] = usage(ch);
            REQUIRE(u != 0);
            press(r, u, shift ? SHIFT : 0);
        }
    }
    void enter(CcpRig &r, const std::string &text, uint64_t settle = ms(300))
    {
        keys(r, text + "\r");
        r.run(settle);
    }
    /// GW-BASIC's two files on M: and GW-BASIC started on its own screen, until its first Ok. @return its row
    int start(CcpRig &r)
    {
        const std::vector<uint8_t> com = file("GWBASIC.COM"), ovl = file("GWBASIC.OVL");
        CHECK(!com.empty() && !ovl.empty());
        if (com.empty() || ovl.empty())
            return -1;
        r.put_m({{0, "GWBASIC COM", com}, {0, "GWBASIC OVL", ovl}});
        const size_t from = r.out.size();
        r.type("m:gwbasic\r");
        CHECK(r.until("m:gwbasic\r\n", ms(2000), from));
        const int ok = until_row(r, "Ok");
        r.run(ms(100));
        return ok;
    }
    /// The screen cleared (so the next line's output is easy to find) and a direct line typed: the row after the
    /// line's echo, or "" if that is Ok (a statement that prints nothing).
    std::string said(CcpRig &r, const std::string &text, uint64_t settle = ms(600))
    {
        enter(r, "CLS");
        enter(r, text, settle);
        const unsigned cols = columns(r);
        for (unsigned i = 0; i + 1 < 25; ++i)
            if (text.rfind(line(r, i), 0) == 0 && !line(r, i).empty()) // its first row (a long line wraps)
            {
                const unsigned rows = static_cast<unsigned>((text.size() + cols - 1) / cols);
                const std::string next = line(r, i + std::max(1u, rows));
                return next == "Ok" ? std::string() : next;
            }
        std::printf("  '%s' not echoed: %s\n", text.c_str(), screen(r).c_str());
        return "?";
    }

    /// The sound card, revision 3, and music running: SC_MUSIC claimed or the firmware playing.
    SoundCard3 &dev(CcpRig &r) { return *r.machine->sound_device(); }
    bool tone_on(CcpRig &r) { return dev(r).firmware().music_tone_on(); }
    bool running(CcpRig &r)
    {
        return (r.phys(SYS_BLOCK_PHYS + sym("SCLAIM")) & 0x04) != 0 ||
               dev(r).firmware().music_state() != SoundFw::M_STOPPED;
    }

    constexpr uint8_t O_SOUND = 82, SN_CLAIM = 1, SN_RELEASE = 2, SN_MUSIC = 24, SN_MCTL = 25, SN_BEEP = 27, SC_MUSIC = 4;

    /// A voice's entry as P7 sent it to the card: an entry of an SN_MUSIC's note block (the S1 appendix's layout).
    struct Entry
    {
        uint64_t at;   ///< the cycle of the SN_MUSIC call that sent it
        unsigned voice;
        unsigned hz;   ///< the frequency entry's Hz x 16, / 16 (0: a rest)
        uint32_t len, gate; ///< us (a rest's gate its length)
    };
    /// Function 82 as P7 calls it, watched at the BDOS's entry ($0203, Y 82, A/X the block): SN_MUSIC's note blocks
    /// decoded into entries (frequency entries and rests) and each block's first instrument, stops (SN_MCTL's stop),
    /// beeps (SN_BEEP's blocks), the music's claim (`loads`: MU_INIT's start) and releases counted.
    struct Sound
    {
        std::vector<Entry> entries;
        std::vector<std::vector<uint8_t>> instruments; ///< each block's first voice's instrument, 8 bytes
        std::vector<std::pair<uint64_t, unsigned>> stops; ///< (cycle, voice)
        std::vector<std::vector<uint8_t>> beeps;          ///< SN_BEEP's blocks: +1-2 Hz, +3-4 ms
        std::vector<std::vector<uint8_t>> loads;          ///< SN_CLAIM's blocks of SC_MUSIC (MU_INIT's)
        unsigned releases = 0;
        uint64_t bdos_cycles = 0; ///< function 82's cycles, its calls (the interrupts inside included)
        unsigned bdos_calls = 0;
        // The call under way: its return address and S after it, its op and block, and a note block's bytes as
        // the call found them; taken only if it returns A 0.
        bool in = false;
        uint16_t ret = 0;
        uint8_t ret_s = 0;
        uint64_t call_at = 0;
        std::vector<uint8_t> blk, notes;
        static uint32_t le(const std::vector<uint8_t> &b, size_t at, unsigned n)
        {
            uint32_t v = 0;
            for (unsigned i = 0; i < n; ++i)
                v |= static_cast<uint32_t>(b.at(at + i)) << (8 * i);
            return v;
        }
        void decode(uint64_t at)
        {
            if (notes.size() < 8)
                return;
            unsigned voices = 0;
            for (unsigned v = 0; v < 8; ++v)
                voices += notes[1] >> v & 1;
            if (voices && notes.size() >= 16)
                instruments.emplace_back(notes.begin() + 8, notes.begin() + 16);
            const uint32_t count = le(notes, 4, 4);
            size_t p = 8 + 8 * voices;
            for (uint32_t k = 0; k < count && p < notes.size(); ++k)
            {
                const unsigned kind = notes[p] >> 4, voice = notes[p] & 15;
                if (kind == 1)
                    entries.push_back({at, voice, le(notes, p + 1, 3) / 16, le(notes, p + 4, 4), le(notes, p + 8, 4)});
                else if (kind == 2)
                    entries.push_back({at, voice, 0, le(notes, p + 1, 4), le(notes, p + 1, 4)});
                p += kind == 0 ? 10 : kind == 1 ? 12 : kind == 2 ? 5 : kind == 3 ? 9 : 2;
            }
        }
        void done(const std::vector<uint8_t> &b, uint64_t at)
        {
            if (b[0] == SN_MUSIC)
                decode(at);
            else if (b[0] == SN_MCTL && b[1] == 0)
                stops.push_back({at, 0u});
            else if (b[0] == SN_BEEP)
                beeps.push_back(b);
            else if (b[0] == SN_CLAIM && (b[1] & SC_MUSIC))
                loads.push_back(b);
            else if (b[0] == SN_RELEASE)
                ++releases;
        }
        void watch(CcpRig &r)
        {
            r.watch = [this, &r](uint16_t pc) {
                const auto &g = r.cpu->regs();
                Bus &bus = r.machine->bus();
                if (in && pc == ret && g.s == ret_s)
                {
                    in = false;
                    bdos_cycles += r.cpu->cycles() - call_at;
                    ++bdos_calls;
                    if (g.a == 0)
                        done(blk, call_at);
                    return;
                }
                if (pc != 0x0203 || g.y != O_SOUND || g.x == 0) // (X 0: an operation with no block)
                    return;
                const uint16_t at = static_cast<uint16_t>(g.a | g.x << 8);
                blk.clear();
                for (uint16_t i = 0; i < 9; ++i)
                    blk.push_back(bus.peek(static_cast<uint16_t>(at + i)));
                notes.clear();
                if (blk[0] == SN_MUSIC)
                {
                    const uint32_t phys = le(blk, 2, 3), len = le(blk, 5, 3);
                    for (uint32_t i = 0; i < len && i < 4096; ++i)
                        notes.push_back(r.phys(phys + i));
                }
                const uint16_t sp = static_cast<uint16_t>(0x100 + g.s);
                ret = static_cast<uint16_t>((bus.peek(static_cast<uint16_t>(sp + 1)) | bus.peek(static_cast<uint16_t>(sp + 2)) << 8) + 1);
                ret_s = static_cast<uint8_t>(g.s + 2);
                call_at = r.cpu->cycles();
                in = true;
            };
        }
    };
    /// GW-BASIC's 2.5 ms units (the source's) as us, exactly (branch C's answer 4).
    uint32_t units_us(unsigned u) { return u * 2500u; }
    /// The music source's pitch while it sounds: its output tapped for 200 ms (before the mix, its left channel),
    /// the rising zero crossings counted from the first to the last. @return Hz, or 0 if fewer than 3 crossings
    double music_hz(CcpRig &r)
    {
        dev(r).firmware().set_tap(true, SoundFw::SRC_MUSIC);
        r.run(ms(200));
        const std::vector<int16_t> t = dev(r).firmware().take_tap();
        dev(r).firmware().set_tap(false);
        size_t first = 0, last = 0, n = 0;
        for (size_t i = 2; i < t.size(); i += 2)
            if (t[i - 2] < 0 && t[i] >= 0)
            {
                if (n++ == 0)
                    first = i / 2;
                last = i / 2;
            }
        return n < 3 ? 0.0 : static_cast<double>(n - 1) * SoundFw::RATE / static_cast<double>(last - first);
    }
    double as_ms(uint64_t cycles) { return static_cast<double>(cycles) * 1000.0 / static_cast<double>(HZ); }

    /// A program typed after NEW and RUN, the sound watched; run for `budget` and then until Ok.
    void run_prog(CcpRig &r, const std::vector<std::string> &lines, uint64_t budget = ms(2000))
    {
        enter(r, "CLS");
        enter(r, "NEW");
        for (const std::string &l : lines)
            enter(r, l, ms(150));
        enter(r, "CLS");
        keys(r, "RUN\r");
        r.run(budget);
    }
    /// NOTTAB's top octave (GWSTS.ASM), C to B, and a note's Hz in octave o (the owner's answer 2: shifted right
    /// by 7 - o, rounded by the last bit out, as PLYNU4's ADC).
    const unsigned NOTTAB[12] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};
    unsigned note_hz(unsigned i, unsigned o)
    {
        const unsigned sh = 7 - o;
        return (NOTTAB[i] >> sh) + ((NOTTAB[i] >> (sh - 1)) & 1);
    }
    /// PLAY's units for tempo t and length l (96,000 / (t x l)), and the dots (x 3 / 2 each).
    unsigned play_units(unsigned t, unsigned l, unsigned dots = 0)
    {
        unsigned u = 96000 / (t * l);
        for (unsigned d = 0; d < dots; ++d)
            u = u * 3 / 2;
        return u;
    }

    /// RUN's cycles from the RUN statement to the end's ENDCON, less the interrupt handlers' inside it (T17's
    /// method, gw_timing.h's: the IRQ's entry to its RTI's return), the program `lines` typed after NEW.
    uint64_t run_cycles(CcpRig &r, const std::vector<std::string> &lines, uint64_t budget = ms(5000))
    {
        const std::map<std::string, uint16_t> lab = lbl_map();
        enter(r, "CLS");
        enter(r, "NEW");
        for (const std::string &l : lines)
            enter(r, l, ms(150));
        enter(r, "CLS");
        const uint16_t runc = lab.at("RUN"), endcon = lab.at("ENDCON");
        gw_timing::LeftOut left(r, false);
        uint64_t from = 0, cycles = 0;
        bool on = false;
        const auto before = r.watch;
        r.watch = [&](uint16_t pc) {
            if (before)
                before(pc);
            if (left.inside(pc, on))
                return;
            if (pc == runc && !on)
            {
                from = r.cpu->cycles();
                on = true;
                left.out = 0;
            }
            else if (on && pc == endcon)
            {
                cycles = r.cpu->cycles() - from - left.out;
                on = false;
            }
        };
        keys(r, "RUN\r");
        const uint64_t end = r.cpu->cycles() + budget;
        while (cycles == 0 && r.cpu->cycles() < end)
            r.run(ms(10));
        r.watch = before;
        r.run(ms(100));
        CHECK(cycles > 0);
        return cycles;
    }
} // namespace

// N9 (7f's plan; branch C, S26): SOUND 440,18.2 plays one entry on voice 0 of a block whose instrument is the card's
// tone generator (source 2) at the beep's volume, 128 (branch C's answer 3): 440 Hz, 18.2 ticks x 54,925 us (the
// PC's tick, the sound device's spec) = 999,635 us, the gate the whole note (the source's SOUND is legato). In MF
// (the default: the owner's answer 6, a queue one note deep) the statement after the first SOUND runs at once, and a
// second SOUND waits for the first to end. The music source sounds at the pitch asked, within 0.5%, at 440 Hz and
// at 15,000 Hz (past the SID's top, about 3,848 Hz). Guards SOUND silent, a wrong pitch, or a length in the wrong
// units. Seen failing with (a) the instrument's source left a SID voice's (0: the tap's pitch the SID's), (b) the
// tick taken as the source's 55,000 us, (c) MF's wait after the note, not before the next.
TEST(sound_plays_its_tone_on_the_tone_generator)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const std::map<std::string, uint16_t> lab = lbl_map();
    REQUIRE(lab.count("OUTDO") == 1);
    Sound s;
    std::vector<uint64_t> prints; // PRINT's "A" and "B" at OUTDO
    s.watch(r);
    const auto snd = r.watch;
    r.watch = [&](uint16_t pc) {
        snd(pc);
        if (pc == lab.at("OUTDO") && (r.cpu->regs().a == 'A' || r.cpu->regs().a == 'B'))
            prints.push_back(r.cpu->cycles());
    };
    run_prog(r, {"10 SOUND 440,18.2:PRINT \"A\":SOUND 880,18.2:PRINT \"B\""}, ms(2600));
    r.watch = nullptr;
    REQUIRE(s.entries.size() == 2u);
    REQUIRE(s.instruments.size() == 2u);
    CHECK_EQ(s.instruments[0][0], 2);   // the tone generator
    CHECK_EQ(s.instruments[0][6], 128); // the beep's volume
    CHECK_EQ(s.entries[0].voice, 0u);
    CHECK_EQ(s.entries[0].hz, 440u);
    CHECK_EQ(s.entries[0].len, 999635u);
    CHECK_EQ(s.entries[0].gate, 999635u);
    CHECK_EQ(s.entries[1].hz, 880u);
    REQUIRE(prints.size() == 2u);
    const double a = as_ms(prints[0] - s.entries[0].at), second = as_ms(s.entries[1].at - s.entries[0].at),
                 b = as_ms(prints[1] - s.entries[0].at);
    std::printf("  from the first entry: PRINT \"A\" %.1f ms, the second entry %.1f ms, PRINT \"B\" %.1f ms\n", a, second,
                b);
    CHECK(a < 50);
    CHECK(second > 950 && second < 1150);
    CHECK(b > second && b - second < 50);
    CHECK(running(r));
    r.run(ms(1200));
    for (const unsigned hz : {440u, 15000u})
    {
        keys(r, "SOUND " + std::to_string(hz) + ",18.2\r");
        r.run(ms(300));
        CHECK(tone_on(r));
        const double got = music_hz(r);
        std::printf("  SOUND %u: the music source at %.1f Hz\n", hz, got);
        CHECK(std::fabs(got - hz) < hz * 0.005);
        r.run(ms(800));
    }
}

// N10 (the manual's p. 323): SOUND 500,100 then SOUND 500,0 stops it at once (MU_STOP, the tone voice off); SOUND
// 500,0 with nothing sounding, in a run that hasn't played, starts nothing: music left unclaimed (no SN_CLAIM).
// Guards a stuck note; music claimed for nothing, refusing the next program's. Seen failing with SOUND x,0 queued as
// a short note.
TEST(sound_x_0_stops_and_does_nothing_when_silent)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    run_prog(r, {"10 SOUND 500,0"}, ms(500));
    CHECK(s.loads.empty());
    CHECK(s.entries.empty());
    CHECK(!running(r));
    run_prog(r, {"10 SOUND 500,100:SOUND 500,0"}, ms(500));
    r.watch = nullptr;
    CHECK_EQ(s.loads.size(), 1u);
    CHECK_EQ(s.entries.size(), 1u);
    REQUIRE(!s.stops.empty());
    CHECK_EQ(s.stops.back().second, 0u);
    CHECK(!s.entries.empty() && s.stops.back().first > s.entries[0].at);
    CHECK(!tone_on(r)); // the tone voice off
}

// N11 (the manual's p. 323: 37 to 32,767 Hz, .0015 to 65,535 ticks; the owner's answer 19: a negative duration
// Illegal function call; branch C's answers 2 and 5): 36 Illegal function call, 37 Hz exactly; 15,000 and 20,000 Hz
// tones, 20,001 and 32,767 (the manual's silence) rests; 65,535 ticks one entry of 65,535 x 54,925 = 3,599,509,875
// us (within a single's 256: 7f's 2 min 44 s cap, C9, gone); 65,536, 65,535.5, -1 and .001 Illegal function call; a
// negative frequency Illegal function call (C5); .0015 one entry of 82 us, waited for in MF (C7's 1 ms gone).
// Guards a frequency below the range sent, a long SOUND cut short, and silence played as a tone. Seen failing with
// (a) the 37 check one low, (b) the us cut to 16 bits, (c) 32,767 sent as a tone.
TEST(sound_s_ranges_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "SOUND 36,1"), std::string("Illegal function call"));
    CHECK(s.entries.empty());
    CHECK_EQ(said(r, "SOUND 37,1"), std::string());
    CHECK_EQ(said(r, "SOUND 15000,1"), std::string());
    CHECK_EQ(said(r, "SOUND 20000,1"), std::string());
    CHECK_EQ(said(r, "SOUND 20001,1"), std::string());
    CHECK_EQ(said(r, "SOUND 32767,1"), std::string());
    CHECK_EQ(said(r, "SOUND 440,65536"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "SOUND 440,65535.5"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "SOUND 440,-1"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "SOUND 440,.001"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "SOUND -1,1"), std::string("Illegal function call")); // (review round 1, C5)
    CHECK_EQ(said(r, "SOUND -32768,1"), std::string("Illegal function call"));
    REQUIRE(s.entries.size() == 5u);
    const unsigned want_hz[5] = {37, 15000, 20000, 0, 0}; // (past 20,000: a rest)
    for (unsigned i = 0; i < 5; ++i)
    {
        CHECK_EQ(s.entries[i].hz, want_hz[i]);
        CHECK_EQ(s.entries[i].len, 54925u); // one tick
    }
    r.run(ms(200));
    CHECK_EQ(said(r, "SOUND 440,65535"), std::string());
    r.watch = nullptr;
    REQUIRE(s.entries.size() == 6u);
    const int64_t hour = 65535LL * 54925;
    std::printf("  SOUND 440,65535: one entry of %u us (65,535 x 54,925 = %lld)\n", s.entries[5].len,
                static_cast<long long>(hour));
    CHECK(std::llabs(static_cast<int64_t>(s.entries[5].len) - hour) <= 256);
    CHECK_EQ(s.entries[5].gate, s.entries[5].len);
    // SOUND's shortest, .0015 (the manual's p. 323): .0015 x 54,925 = 82 us, gated the whole, and waited for as any
    // in MF: after an 18.2-tick note, about 1,000 ms later.
    said(r, "SOUND 440,0"); // (the long one stopped)
    s.watch(r);
    const size_t n = s.entries.size();
    CHECK_EQ(said(r, "SOUND 440,18.2:SOUND 880,.0015", ms(1500)), std::string());
    r.watch = nullptr;
    REQUIRE(s.entries.size() == n + 2);
    CHECK_EQ(s.entries[n + 1].hz, 880u);
    CHECK_EQ(s.entries[n + 1].len, 82u);
    CHECK_EQ(s.entries[n + 1].gate, 82u);
    const double gap = as_ms(s.entries[n + 1].at - s.entries[n].at);
    CHECK(gap > 950 && gap < 1100);
}

// N18 (the owner's answer 6; branch C's answer 7): PLAY's and SOUND's MF wait. SOUND 440,9.1 (500 ms) three times:
// each after the first is sent as the one before ends; ^C during the third: Ok (the break), MU_STOP of voice 0
// (SNDRST's) and the tone voice off; with every EK_SOUND event lost (each one's kind made 0 by the watch as it lands, so
// EVPOLL drops it) the next still goes as the one before ends, since the wait asks the card its count
// (SN_MSTATUS) and needs no event. Guards ^C leaving a tone on; a program hung for ever, or late, on a lost event.
// Seen failing with (a) SNDRST left out of the break, (b) the wait on the events alone (the poll left out: it
// hangs), (c) the wait's look at the keys left out (CHKINT: the break a note late; review round 1, T1).
TEST(play_mf_waits_and_ctrl_c_breaks_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    run_prog(r, {"10 SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1:SOUND 440,9.1"}, ms(1200));
    // (review round 1, T1) the break comes from inside the wait: within about 50 ms of the key, not a note later
    const uint16_t itsctc = lbl_map().at("ITSCTC");
    uint64_t brk = 0;
    const auto snd0 = r.watch;
    r.watch = [&](uint16_t pc) {
        snd0(pc);
        if (pc == itsctc && brk == 0)
            brk = r.cpu->cycles();
    };
    const uint64_t key = r.cpu->cycles() + ms(10); // press(): Ctrl down, 10 ms, then C
    press(r, 0x06, CTRL); // ^C, as the third plays (the fourth SOUND waiting for it)
    CHECK(until_row(r, "Break") >= 0);
    r.watch = snd0;
    REQUIRE(brk > key);
    std::printf("  ^C: the break %.1f ms after the key\n", as_ms(brk - key));
    CHECK(as_ms(brk - key) < 50);
    const size_t n = s.entries.size();
    r.run(ms(800));
    CHECK_EQ(s.entries.size(), n);
    CHECK(n >= 3);
    REQUIRE(!s.stops.empty());
    CHECK(s.stops.back().first > s.entries[n - 1].at);
    CHECK(!tone_on(r));
    for (size_t i = 1; i < n; ++i)
    {
        const double gap = as_ms(s.entries[i].at - s.entries[i - 1].at);
        CHECK(gap > 480 && gap < 560);
    }
    // The lost end: every EK_SOUND event taken off the queue as it comes, before GW-BASIC sees it.
    const std::map<std::string, uint16_t> lab = lbl_map();
    REQUIRE(lab.count("EVAREA") == 1);
    const uint16_t ev = lab.at("EVAREA");
    Sound t;
    t.watch(r);
    const auto snd = r.watch;
    bool lose = true;
    r.watch = [&](uint16_t pc) {
        snd(pc);
        Bus &bus = r.machine->bus();
        const unsigned slots = bus.peek(static_cast<uint16_t>(ev + 18));
        if (lose && slots)
            for (unsigned i = 0; i < slots; ++i) // each EK_SOUND event's kind made 0: dropped as an unknown one
                if (bus.peek(static_cast<uint16_t>(ev + 128 + i * 8)) == 6)
                    bus.poke(static_cast<uint16_t>(ev + 128 + i * 8), 0);
    };
    run_prog(r, {"10 SOUND 440,9.1:SOUND 440,9.1"}, ms(1500));
    r.watch = nullptr;
    REQUIRE(t.entries.size() == 2u);
    const double gap = as_ms(t.entries[1].at - t.entries[0].at);
    std::printf("  with the note's end lost, the next sent after %.1f ms (a note 500 ms)\n", gap);
    CHECK(gap > 480 && gap < 560);
}

// N19 (the owner's answer 5): after RUN, NEW, CLEAR and SYSTEM music is given back (SN_RELEASE of SC_MUSIC),
// and PLAY's state is the defaults again (SNDINI's; review round 1, T1): MB O6 T255 L64 MS set and played, then the
// next PLAY's C has O4's Hz, T120 L4's 500,000 us and MN's gate, and its D waits for it (MF). Guards music held
// after GW-BASIC (the next program's MU_INIT refused); a new run inheriting the last one's mode. Seen failing with (a) CLEARC's SNDINI not called
// (INITRP's far call left out), (b) SNDINI's stores dropped.
TEST(run_and_system_free_the_music)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    for (const char *what : {"RUN", "NEW", "CLEAR"})
    {
        // (review round 1, T1) PLAY's state set away from every default, and a note played with it
        CHECK_EQ(said(r, "PLAY \"MB O6 T255 L64 MS C\""), std::string());
        CHECK(running(r));
        const unsigned before = s.releases;
        if (std::string(what) == "RUN")
            run_prog(r, {"10 REM"}, ms(300));
        else
            enter(r, what);
        CHECK_EQ(s.releases, before + 1);
        CHECK(!running(r));
        // then the defaults (O4 L4 T120 MN MF): C's Hz, 500 ms with MN's gate, and D sent as C ends (MF)
        const size_t n = s.entries.size();
        CHECK_EQ(said(r, "PLAY \"CD\"", ms(1200)), std::string());
        REQUIRE(s.entries.size() == n + 2);
        const Entry &c = s.entries[n], &d = s.entries[n + 1];
        CHECK_EQ(c.hz, note_hz(0, 4));
        CHECK_EQ(c.len, units_us(play_units(120, 4)));
        CHECK_EQ(c.gate, units_us(play_units(120, 4) * 7 / 8));
        const double gap = as_ms(d.at - c.at);
        CHECK(gap > 480 && gap < 560);
        r.run(ms(600));
    }
    CHECK_EQ(said(r, "SOUND 440,2"), std::string());
    CHECK(running(r));
    const size_t from = r.out.size();
    enter(r, "SYSTEM");
    r.watch = nullptr;
    CHECK(r.until("A>", ms(3000), from));
    CHECK(!running(r));
}

// The owner's answer 20: CLEARC far calls P7's SNDINI only when the run used sound (SNDUSE); a CHAIN from a run that
// played still resets the sound: MB notes queued, then CHAIN to a saved program: music given back (SN_RELEASE) and
// the chained program's PLAY(0) 0; and a CHAIN from a run with no sound makes no SN_ call.
// Guards a CHAIN that leaves the last program's music playing, or music held. Seen failing with INITRP's test
// of SNDUSE inverted.
TEST(chain_after_sound_still_resets_the_sound)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "NEW");
    enter(r, "10 PRINT PLAY(0)");
    enter(r, "SAVE \"M:C2\",A", ms(1500));
    Sound s;
    s.watch(r);
    run_prog(r, {"10 PLAY \"MB L1 CDEF\":CHAIN \"M:C2\""}, ms(2500));
    if (s.entries.size() != 4u)
    {
        std::printf("  the screen: %s\n", screen(r).c_str());
        for (const Entry &e : s.entries)
            std::printf("  entry at %.1f: voice %u %u Hz len %u gate %u\n", as_ms(e.at), e.voice, e.hz, e.len, e.gate);
    }
    CHECK_EQ(s.entries.size(), 4u);
    CHECK_EQ(s.releases, 1u);
    CHECK(!running(r));
    CHECK_EQ(line(r, 2), std::string(" 0"));
    const unsigned rel = s.releases;
    run_prog(r, {"10 CHAIN \"M:C2\""}, ms(2500));
    r.watch = nullptr;
    CHECK_EQ(s.releases, rel);
    CHECK_EQ(line(r, 2), std::string(" 0"));
}

// N20 (the owner's answer 3): BEEP and PRINT CHR$(7) in screen mode, and BEEP in console mode (test_gwbasic_core's
// console_mode_keeps_its_guard, its SN_BEEP block watched: review round 1, T5), sound the bell by function 82's
// SN_BEEP, 800 Hz for 250 ms, and claim nothing (no SN_CLAIM). Guards
// BEEP silent. Seen failing with ^G's route left out of P6 (CBEEP's far call).
TEST(beep_and_ctrl_g_sound_the_bell)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "BEEP"), std::string());
    CHECK_EQ(said(r, "PRINT CHR$(7);"), std::string());
    r.watch = nullptr;
    REQUIRE(s.beeps.size() == 2u);
    for (const auto &b : s.beeps)
    {
        CHECK_EQ(b[1] | b[2] << 8, 800);
        CHECK_EQ(b[3] | b[4] << 8, 250);
    }
    CHECK(s.loads.empty());
    CHECK(!running(r));
}

// N12 (the manual's pp. 250-251; the owner's answer 2): each note's Hz from NOTTAB shifted right by 7 - the octave
// and rounded (O3's C 262 Hz, middle C; O4's A 880), as sent: C and B in every
// octave, A in O3 and O4, N1 (O0's C), N42, N84 (O6's B), N0 a rest (0); > and < one octave up or down, held at 6
// and 0, across PLAYs too (p. 250's PLAY ">A" again and again). All in MB at T255 L64 (12.5 ms notes), so nothing
// waits. Guards a tune an octave off or a note wrong on the card. Seen failing with (a) > not held at 6, (b)
// PLYNU4's rounding (the last bit out) left out.
TEST(play_s_notes_and_octaves_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    std::vector<unsigned> want;
    std::string tune = "MB T255 L64";
    for (unsigned o = 0; o <= 6; ++o)
    {
        tune += " O" + std::to_string(o) + " C B";
        want.push_back(note_hz(0, o));
        want.push_back(note_hz(11, o));
    }
    tune += " O3 A O4 A N1 N42 N84 N0";
    for (const unsigned hz : {note_hz(9, 3), note_hz(9, 4), note_hz(0, 0), note_hz(5, 3), note_hz(11, 6), 0u})
        want.push_back(hz);
    CHECK_EQ(said(r, "PLAY \"" + tune + "\""), std::string());
    r.run(ms(500));
    CHECK_EQ(said(r, "PLAY \"O5 >C >C >C O1 <C <C <C\""), std::string());
    for (const unsigned o : {6u, 6u, 6u, 0u, 0u, 0u})
        want.push_back(note_hz(0, o));
    CHECK_EQ(said(r, "PLAY \"O4\":PLAY \">A\":PLAY \">A\":PLAY \">A\""), std::string());
    for (const unsigned o : {5u, 6u, 6u})
        want.push_back(note_hz(9, o));
    r.watch = nullptr;
    CHECK_EQ(note_hz(0, 3), 262u); // middle C
    CHECK_EQ(note_hz(9, 4), 880u); // (the owner's answer 2)
    REQUIRE(s.entries.size() == want.size());
    unsigned bad = 0;
    for (size_t i = 0; i < want.size(); ++i)
        if (s.entries[i].hz != want[i] && ++bad <= 4)
            std::printf("  note %zu: %u Hz, want %u\n", i, s.entries[i].hz, want[i]);
    CHECK_EQ(bad, 0u);
}

// N13 (the manual's pp. 251-253, the source's PPAUS2 and PLYDOT): the lengths from 96,000 / (T x L) units of 2.5
// ms, as us exactly (units x 2,500: branch C's answer 4); L1, L4, L64; A16 the same as L16A; T32 and T255; one and
// two dots (x 3 / 2 each); the gates: MN 7/8, ML the whole, MS 3/4 (of the units, at least 1, x 2,500); a rest's
// gate the whole (P4). In MB. Guards rhythm wrong: notes run together or clipped, or run slow by a rounding. Seen
// failing with (a) MS's gate as 7/8, (b) the us rounded to whole ms.
TEST(play_s_lengths_tempo_dots_and_styles)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "PLAY \"MB L1 C L4 C L64 C A16 L16 A T32 L4 C T255 C T120 C. C.. ML C MS C P4\""), std::string());
    r.watch = nullptr;
    struct Want
    {
        unsigned units, gate_units;
    };
    auto mn = [](unsigned u) { return std::max(1u, u * 7 / 8); };
    auto msg = [](unsigned u) { return std::max(1u, u * 3 / 4); };
    const unsigned l1 = play_units(120, 1), l4 = play_units(120, 4), l64 = play_units(120, 64), l16 = play_units(120, 16);
    const unsigned t32 = play_units(32, 4), t255 = play_units(255, 4), d1 = play_units(120, 4, 1), d2 = play_units(120, 4, 2);
    const std::vector<Want> want = {{l1, mn(l1)},   {l4, mn(l4)},   {l64, mn(l64)}, {l16, mn(l16)}, {l16, mn(l16)},
                                    {t32, mn(t32)}, {t255, mn(t255)}, {d1, mn(d1)}, {d2, mn(d2)},  {l4, l4},
                                    {l4, msg(l4)},  {l4, l4}};
    CHECK_EQ(l4, 200u);
    CHECK_EQ(units_us(l4), 500000u);
    REQUIRE(s.entries.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i)
    {
        CHECK_EQ(s.entries[i].len, units_us(want[i].units));
        CHECK_EQ(s.entries[i].gate, units_us(want[i].gate_units));
    }
    CHECK_EQ(s.entries.back().hz, 0u); // P4: a rest
}

// N14 (the manual's pp. 249-253): B#, E#, C-, F- Illegal function call; O7, L0, L65, T31, N85, P0 (the manual's
// 1-64, where the source takes it as no pause), P65; a ";" after MF (p. 253); X with a numeric variable Type
// mismatch; and an error leaves the notes queued before it playing (no STOP). Guards a bad string half-played or
// crashing the parser. Seen failing with P0 taken (the source's).
TEST(play_s_errors_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    for (const char *k : {"B#", "E#", "C-", "F-", "O7", "L0", "L65", "T31", "N85", "P0", "P65", "MF;C", "MQ"})
        CHECK_EQ(said(r, std::string("PLAY \"") + k + "\""), std::string("Illegal function call"));
    CHECK_EQ(said(r, "A=5:PLAY \"XA;\""), std::string("Type mismatch"));
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "PLAY \"MB L1 C D N85\""), std::string("Illegal function call"));
    r.watch = nullptr;
    CHECK_EQ(s.entries.size(), 2u);
    CHECK(s.stops.empty());
    CHECK(running(r));
    CHECK_EQ(said(r, "PRINT PLAY(0)"), std::string(" 2"));
}

// N15 (the manual's p. 253): X runs a string, =name; a number, and VARPTR$ both: XA$; and "X"+VARPTR$(A$) play
// A$'s notes, O=I; and "O="+VARPTR$(I) set the octave from I. Guards substrings or variables not played. Seen
// failing with P7's MACLNG table without X (MCLXEQ).
TEST(play_x_and_variables_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "A$=\"CDE\":I=2:PLAY \"MB T255 L64 XA$; O=I; C\""), std::string());
    CHECK_EQ(said(r, "PLAY \"X\"+VARPTR$(A$)+\"O=\"+VARPTR$(I)+\"D\""), std::string());
    r.watch = nullptr;
    const std::vector<unsigned> want = {note_hz(0, 4), note_hz(2, 4), note_hz(4, 4), note_hz(0, 2),
                                        note_hz(0, 2), note_hz(2, 2), note_hz(4, 2), note_hz(2, 2)};
    REQUIRE(s.entries.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i)
        CHECK_EQ(s.entries[i].hz, want[i]);
}

// N16 (the manual's pp. 252-255): MB runs in the background: a program goes on at once, PLAY(0) counts the notes
// waiting (5, then each one fewer as one ends, to 0); in MF PLAY(0) is 0; the 33rd note of one PLAY waits for the
// first to end (32 a voice); RUN stops the music and PLAY(0) is 0 again. Guards a program blocked by MB, or
// PLAY(n) wrong in a game's music loop. Seen failing with PLAY(n) counting in MF.
TEST(play_mb_runs_in_the_background_and_play_n_counts)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    run_prog(r, {"10 PLAY \"MB T120 L8 CDEFG\":M=-1", "20 N=PLAY(0):IF N<>M THEN PRINT N;:M=N", "30 IF N>0 THEN 20",
                 "40 PLAY \"MF C\":PRINT PLAY(0)"},
             ms(2500));
    CHECK_EQ(line(r, 2), std::string(" 5  4  3  2  1  0  0")); // (row 1: RUN; line 40's 0 after the count's)
    Sound s;
    s.watch(r);
    CHECK_EQ(said(r, "PLAY \"MB T255 L8 " + std::string(33, 'C') + "\"", ms(1200)), std::string());
    REQUIRE(s.entries.size() == 33u);
    const double w31 = as_ms(s.entries[31].at - s.entries[0].at), w32 = as_ms(s.entries[32].at - s.entries[0].at);
    const double note = units_us(play_units(255, 8)) / 1000.0;
    std::printf("  from the first note: the 32nd sent after %.1f ms, the 33rd after %.1f ms (a note %.0f ms)\n", w31, w32,
                note);
    CHECK(w31 < note - 20);
    CHECK(w32 > note - 5 && w32 < note + 30);
    CHECK_EQ(said(r, "SOUND 37,0:PLAY \"MB L1 CDEF\":PRINT PLAY(0)"), std::string(" 4")); // (the 33 stopped first)
    run_prog(r, {"10 PRINT PLAY(0)"}, ms(500));
    r.watch = nullptr;
    CHECK_EQ(line(r, 2), std::string(" 0"));
    CHECK(!running(r));
}

// Review C6 (BASIC's 32-note rule across statements): 32 one-note PLAY "MB" statements all queue (the card's 32 music
// slots, a block a statement), PLAY(0) reaches 32, and ON PLAY(10) (n > 8) fires as the count falls past 10. Must fail
// with the card's 8 slots (the ninth statement waits: PLAY(0) never past 8, ON PLAY(10) never fires).
TEST(thirty_two_mb_statements_queue_and_on_play_past_8_fires)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    run_prog(r, {"10 ON PLAY(10) GOSUB 100:PLAY ON", "20 FOR I=1 TO 32:PLAY \"MB T120 L4 C\":NEXT", "30 M=PLAY(0)",
                 "40 IF PLAY(0)>0 THEN 40", "50 PRINT M;N:END", "100 N=N+1:RETURN"},
             ms(17500));
    if (line(r, 2) != " 32  1")
        std::printf("  %s\n", screen(r).c_str());
    CHECK_EQ(line(r, 2), std::string(" 32  1"));
}

// N33 (the owner's answer 11, its gate 9,000 by answer 24: a PLAY note in MB, the interrupts left out): PLAY "MB
// T255 L64" with 16 notes against one note, T17's method; the difference over 15. A statement's notes go to the card
// as one block (one SN_MUSIC, sound card revision 3), so function 82's share of a note is about none: what is left
// is the parse and the block's entry. A record and a bound (1,000-9,000), not a functional guard (the play_* cases
// are). Guards MB falling behind its own notes.
TEST(play_s_note_cost_is_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    const uint64_t one = run_cycles(r, {"10 PLAY \"MB T255 L64 C\""});
    const uint64_t b1 = s.bdos_cycles;
    const unsigned n1 = s.bdos_calls;
    const uint64_t many = run_cycles(r, {"10 PLAY \"MB T255 L64 CDEFGABCDEFGABCD\""});
    r.watch = nullptr;
    const double per = static_cast<double>(many - one) / 15;
    // (Both runs' calls are in the totals: the 16-note run's less the one-note run's, a note; signed, since the two
    // runs make about the same calls.)
    std::printf("  of which function 82: %.0f cycles a note, %.1f calls\n",
                (static_cast<double>(s.bdos_cycles) - 2.0 * static_cast<double>(b1)) / 15, (s.bdos_calls - 2.0 * n1) / 15);
    std::printf("  PLAY in MB: %.0f cycles a note, the interrupts left out (the gate 9,000); one note's PLAY %llu\n", per,
                static_cast<unsigned long long>(one));
    CHECK(per > 1000 && per <= 9000);
}

// N17 (the manual's pp. 211-212, the owner's answer 9's event): ON PLAY(2) GOSUB with PLAY ON, MB and 4 notes: one
// GOSUB, as the buffer falls from 2 to 1 (the 3rd note's end); PLAY STOP after PLAY ON remembers it (a trap never
// turned on remembers nothing, BIMISC.ASM's REQTRP), but a PLAY ON with the buffer already empty takes no trap (p.
// 212's note 2: branch C's answer 6), while one with a note still sounding takes the remembered trap then; PLAY OFF
// forgets it; ON PLAY(0) and ON PLAY(33) Illegal function call. Guards the background tune never refilled, refilled
// twice, or refilled after it ended; ON PLAY(1), the "tune ended" refill, in MB and not MF (review round 1, T3).
// Seen failing with (a) the request at n + 1 to n (the card's threshold n), (b) note 2's drop left out, (c)
// MUSIC_DONE never trapping, (d) the MB-only test left out.
TEST(on_play_fires_when_the_buffer_falls_to_n_minus_1)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    for (const char *how : {"PLAY ON", "PLAY ON:PLAY STOP", "PLAY ON:PLAY OFF"})
    {
        run_prog(r, {std::string("10 ON PLAY(2) GOSUB 100:") + how, "20 PLAY \"MB T255 L16 CDEF\"",
                     "30 IF PLAY(0)>0 THEN 30", "40 PLAY ON:PRINT \"N=\";N;P:END", "100 N=N+1:P=PLAY(0):RETURN"},
                 ms(1500));
        const std::string h(how);
        const std::string want = h.find("OFF") != std::string::npos    ? "N= 0  0"
                                 : h.find("STOP") != std::string::npos ? "N= 0  0" // (empty at PLAY ON: note 2)
                                                                       : "N= 1  1"; // (1 left: n - 1)
        if (line(r, 2) != want)
            std::printf("  %s: %s\n", how, screen(r).c_str());
        CHECK_EQ(line(r, 2), want);
    }
    // PLAY STOP, then PLAY ON while the last note (a half note, 470 ms) still sounds: the remembered trap taken then.
    run_prog(r, {"10 ON PLAY(2) GOSUB 100:PLAY ON:PLAY STOP", "20 PLAY \"MB T255 L16 CDE L2 F\"",
                 "30 IF PLAY(0)>1 THEN 30", "40 PLAY ON:PRINT \"N=\";N;P:END", "100 N=N+1:P=PLAY(0):RETURN"},
             ms(1500));
    if (line(r, 2) != "N= 1  1")
        std::printf("  PLAY STOP, PLAY ON with a note sounding: %s\n", screen(r).c_str());
    CHECK_EQ(line(r, 2), std::string("N= 1  1"));
    // (review round 1, T3) ON PLAY(1): MUSIC_DONE's trap, as the last note ends (PLAY(0) 0 in it), in MB only (the
    // manual's note 1): the same tune in MF traps nothing.
    for (const char *mode : {"MB", "MF"})
    {
        run_prog(r, {std::string("10 ON PLAY(1) GOSUB 100:PLAY ON:PLAY \"") + mode + " T255 L16 CDEF\"",
                     "20 IF PLAY(0)>0 THEN 20", "25 T!=TIMER", "26 IF TIMER-T!<.5 THEN 26", "30 PRINT N;Q:END",
                     "100 N=N+1:Q=PLAY(0):RETURN"},
                 ms(2500));
        const std::string want = std::string(mode) == "MB" ? " 1  0" : " 0  0";
        if (line(r, 2) != want)
            std::printf("  ON PLAY(1), %s: %s\n", mode, screen(r).c_str());
        CHECK_EQ(line(r, 2), want);
    }
    CHECK_EQ(said(r, "ON PLAY(0) GOSUB 10"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "ON PLAY(33) GOSUB 10"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "ON PLAY(32) GOSUB 10"), std::string());
}

// Review round 1, T2 (the manual's p. 212, note 2; BIMISC.ASM's trap counts): PLAY ON with a PLAY trap remembered
// under PLAY STOP and the buffer empty drops it, after SETEV1 has counted it in ONGSBF, so another trap's count is
// kept: ON KEY(1) armed, ON PLAY(2) under PLAY STOP while MB music plays out, ERROR 200, F1 pressed in the error
// handler (traps held: ONGSBF 1), then PLAY ON and RESUME: the key's GOSUB is taken (K 1), the PLAY trap not (N 0).
// Guards a key or timer trap lost to the drop. Seen failing with PLONP called before SETEV1 (K 0).
TEST(play_on_with_the_buffer_empty_keeps_another_trap_s_count)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    run_prog(r, {"10 ON KEY(1) GOSUB 200:KEY(1) ON:ON PLAY(2) GOSUB 300:PLAY ON:PLAY STOP:ON ERROR GOTO 500",
                 "20 PLAY \"MB T255 L16 CDEF\"", "30 IF PLAY(0)>0 THEN 30", "40 ERROR 200",
                 "50 PRINT \"K=\";K;N:END", "200 K=K+1:RETURN", "300 N=N+1:RETURN",
                 "500 T!=TIMER", "510 IF TIMER-T!<1.5 THEN 510", "520 PLAY ON:RESUME 50"},
             ms(700));
    press(r, 0x3A); // F1, inside the error handler
    r.run(ms(3500));
    if (line(r, 2) != "K= 1  0")
        std::printf("  %s\n", screen(r).c_str());
    CHECK_EQ(line(r, 2), std::string("K= 1  0"));
}

// Branch C (the manual's p. 211): ON PLAY(n) given after the notes were sent still traps as the buffer falls from n
// to n - 1: PLAY "MB" of 8 eighth notes, then ON PLAY(4): one GOSUB, PLAY(0) 3 in it. Guards a trap that comes only
// at the music's end (the card's threshold set only when a block is sent). Seen failing with ONSETP's threshold left
// out (" 0  0"; main's " 1  0").
TEST(on_play_given_after_the_notes_fires_as_the_count_falls_to_n_minus_1)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    run_prog(r, {"10 PLAY \"MB T120 L8 CDEFGABC\":ON PLAY(4) GOSUB 100:PLAY ON", "20 IF PLAY(0)>0 THEN 20",
                 "30 PRINT N;Q:END", "100 N=N+1:Q=PLAY(0):RETURN"},
             ms(3500));
    if (line(r, 2) != " 1  3")
        std::printf("  %s\n", screen(r).c_str());
    CHECK_EQ(line(r, 2), std::string(" 1  3"));
}

// Branch C (the manual's pp. 80, 324; the source: STOP and END don't call SNDRST, POLKEY's ^C does): MB music plays
// on after END (at Ok PLAY(0) counts down, no stop sent) and through STOP and CONT (its count fallen meanwhile: the
// note sounding counted, the owner's answer 1); ^C during MB music stops it
// (SNDRST: MU_STOP), PLAY(0) 0 after. Guards a program's end cutting its music, or a ^C leaving it playing. Seen
// failing with ITSCTC's SNDRST left out.
TEST(stop_end_and_break_with_background_music)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    Sound s;
    s.watch(r);
    run_prog(r, {"10 PLAY \"MB T120 L2 CDEF\":END"}, ms(300)); // 4 notes of 1 s
    CHECK(s.stops.empty());
    CHECK(dev(r).firmware().music_state() == SoundFw::M_PLAYING);
    const std::string p1 = said(r, "PRINT PLAY(0)");
    std::printf("  after END, at Ok: PLAY(0) '%s'\n", p1.c_str());
    CHECK(p1 == " 1" || p1 == " 2" || p1 == " 3");
    r.run(ms(4000));
    CHECK_EQ(said(r, "PRINT PLAY(0)"), std::string(" 0"));
    CHECK(s.stops.empty());
    CHECK_EQ(s.entries.size(), 4u);
    // STOP, CONT
    run_prog(r, {"10 PLAY \"MB T120 L2 CDEF\"", "20 STOP", "30 PRINT \"P\";PLAY(0)"}, ms(300));
    CHECK(until_row(r, "Break in 20") >= 0);
    CHECK(dev(r).firmware().music_state() == SoundFw::M_PLAYING);
    r.run(ms(1200)); // stopped past the first note's end: the count goes down meanwhile
    enter(r, "CONT");
    const int row = until_row(r, "P ");
    REQUIRE(row >= 0);
    const std::string p2 = line(r, static_cast<unsigned>(row));
    std::printf("  after STOP and CONT: '%s'\n", p2.c_str());
    CHECK(p2 == "P 2" || p2 == "P 3");
    CHECK(s.stops.empty());
    CHECK(dev(r).firmware().music_state() == SoundFw::M_PLAYING);
    r.run(ms(4000));
    // ^C during MB music
    run_prog(r, {"10 PLAY \"MB T120 L2 CDEF\"", "20 GOTO 20"}, ms(800));
    CHECK(dev(r).firmware().music_state() == SoundFw::M_PLAYING);
    press(r, 0x06, CTRL);
    CHECK(until_row(r, "Break") >= 0);
    r.watch = nullptr;
    CHECK(!s.stops.empty());
    CHECK(dev(r).firmware().music_state() == SoundFw::M_STOPPED);
    CHECK_EQ(said(r, "PRINT PLAY(0)"), std::string(" 0"));
}

// N27 (the manual's pp. 145-148, GWSTS.ASM's KEYTRP, GIOKYB.ASM's TRPCHK): ON KEY(1) with KEY(1) ON, F1 pressed: one
// GOSUB, and F1's soft key text not queued (p. 145: a trapped key doesn't reach the buffer); KEY(11), Cursor Up,
// likewise; an untrapped F9 still gives its text ("KEY "); KEY(1) STOP remembers a press (the GOSUB at KEY(1) ON),
// KEY(1) OFF forgets it (and F1 types its text). Guards a trapped key also typed into the program. Seen failing with
// the trapped key also queued.
TEST(on_key_traps_the_function_and_cursor_keys)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    run_prog(r, {"10 ON KEY(1) GOSUB 100:KEY(1) ON:ON KEY(11) GOSUB 200:KEY(11) ON",
                 "20 A$=INKEY$:IF A$<>\"\" THEN K$=K$+A$", "30 IF RIGHT$(K$,1)<>\"x\" THEN 20",
                 "40 PRINT N;M;K$:END", "100 N=N+1:RETURN", "200 M=M+1:RETURN"},
             ms(300));
    press(r, 0x3A); // F1
    press(r, 0x52); // Up
    press(r, 0x42); // F9: "KEY "
    keys(r, "x");
    r.run(ms(500));
    CHECK_EQ(line(r, 2), std::string(" 1  1 KEY X")); // (Caps Lock on: GW-BASIC's own at start-up)
    for (const char *how : {"STOP", "OFF"})
    {
        run_prog(r, {std::string("10 ON KEY(1) GOSUB 100:KEY(1) ON:KEY(1) ") + how, "20 FOR I=1 TO 1500:NEXT",
                     "30 KEY(1) ON:K$=\"\"", "40 A$=INKEY$:IF A$<>\"\" THEN K$=K$+A$:GOTO 40",
                     "50 PRINT N;K$:END", "100 N=N+1:RETURN"},
                 ms(200));
        press(r, 0x3A);
        r.run(ms(2500));
        CHECK_EQ(line(r, 2), std::string(how) == "STOP" ? std::string(" 1") : std::string(" 0 LIST"));
    }
}

// N28 (the manual's pp. 144-147): KEY 15,CHR$(&H40)+CHR$(25) traps P only with Caps Lock on (GW-BASIC's own, on at
// start-up), so with Caps Lock off P types; KEY 20,CHR$(&H04+&H03)+CHR$(30) traps Ctrl-Shift-A, and
// KEY 18,CHR$(&H04+&H01)+CHR$(31) Ctrl and the left Shift with S (&H01, &H02, &H03 each "a Shift"); the OO-1's
// Num Lock (the owner's answer 15): its &H20 ignored, so KEY 17,CHR$(&H60)+CHR$(49) traps N as KEY 15 traps P; a
// definition for Ctrl-C never traps (the break). Guards a program's hot key never firing, or firing on the unshifted
// key. Seen failing with Caps Lock's bit left out of the match.
TEST(key_15_to_20_trap_their_own_keys)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    UsbCard *usb = r.machine->usb_card();
    auto chord = [&](uint8_t m1, uint8_t m2, uint8_t key) {
        usb->key(m1, true);
        usb->key(m2, true);
        r.run(ms(10));
        press(r, key);
        usb->key(m2, false);
        usb->key(m1, false);
        r.run(ms(20));
    };
    run_prog(r, {"10 KEY 15,CHR$(&H40)+CHR$(25):ON KEY(15) GOSUB 100:KEY(15) ON",
                 "12 KEY 17,CHR$(&H60)+CHR$(49):ON KEY(17) GOSUB 400:KEY(17) ON",
                 "15 KEY 20,CHR$(&H04+&H03)+CHR$(30):ON KEY(20) GOSUB 200:KEY(20) ON",
                 "18 KEY 18,CHR$(&H04+&H01)+CHR$(31):ON KEY(18) GOSUB 300:KEY(18) ON",
                 "20 A$=INKEY$:IF A$<>\"\" THEN K$=K$+A$", "30 IF RIGHT$(K$,1)<>\"9\" THEN 20",
                 "40 PRINT N;M;O;Q;K$:END", "100 N=N+1:RETURN", "200 M=M+1:RETURN", "300 O=O+1:RETURN",
                 "400 Q=Q+1:RETURN"},
             ms(400));
    press(r, 0x13); // P, Caps Lock on: trapped
    press(r, 0x11); // N: trapped (Num Lock's bit ignored)
    press(r, 0x39); // Caps Lock off
    press(r, 0x13); // p: typed
    chord(CTRL, SHIFT, 0x04);     // Ctrl-Shift-A
    chord(CTRL, SHIFT, 0x16);     // Ctrl-(left) Shift-S
    keys(r, "9");
    r.run(ms(500));
    CHECK_EQ(line(r, 2), std::string(" 1  1  1  1 p9"));
    press(r, 0x39); // Caps Lock on again
    run_prog(r, {"10 KEY 16,CHR$(&H04)+CHR$(46):ON KEY(16) GOSUB 100:KEY(16) ON", "20 GOTO 20", "100 PRINT \"T\":RETURN"},
             ms(300));
    press(r, 0x06, CTRL); // ^C: the break, never the trap
    CHECK(until_row(r, "Break") >= 0);
}

// N29 (the manual's p. 147; G10's guard): KEY(n) is 1-20: KEY(21) ON and KEY(0) ON Illegal function call, and
// TRPTBL's bytes past the keys (COM(1)'s) unchanged by them; KEY 15,"x" (not 2 bytes) Illegal function call; KEY
// 11,"x" as the source (Illegal function call); KEY LIST still the ten soft keys. Guards a write past the keys
// (G10). Seen failing with KEYSTT's guard at 21 removed.
TEST(key_n_s_range_is_1_to_20)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const std::map<std::string, uint16_t> lab = lbl_map(), p3 = lbl_map("p3.lbl");
    REQUIRE(lab.count("PAGBLK") == 1 && p3.count("TRPTBL") == 1);
    const uint32_t com1 = static_cast<uint32_t>(r.phys(lab.at("PAGBLK") + 3u)) * 0x4000u + 0x800u +
                          (p3.at("TRPTBL") - 0xC800u) + 3u * 21u;
    for (const char *k : {"KEY(21) ON", "KEY(0) ON", "KEY 15,\"X\"", "KEY 11,\"X\"", "KEY(1) GO"})
        CHECK_EQ(said(r, k), std::string(k) == "KEY(1) GO" ? std::string("Syntax error") : std::string("Illegal function call"));
    CHECK_EQ(r.phys(com1), 0);
    CHECK_EQ(said(r, "KEY(20) ON"), std::string());
    CHECK_EQ(said(r, "KEY 15,CHR$(0)+CHR$(30)"), std::string());
    CHECK_EQ(said(r, "KEY LIST"), std::string("F1 LIST"));
    unsigned listed = 0;
    for (unsigned i = 0; i < 25; ++i)
        listed += line(r, i).rfind("F", 0) == 0 && line(r, i).size() > 2 && line(r, i)[2] == ' ' ? 1u : 0u;
    CHECK(listed >= 9u);
    CHECK(until_row(r, "F15", ms(10)) < 0);
}


// N31 (GWSTS.ASM's PENF and PENS; 7e4's answer 1: no light pen): PEN(0)-(9) 0, PEN(10) Illegal function call; PEN
// ON, OFF and STOP taken; ON PEN GOSUB never taken. Guards nothing on real hardware (no pen: the range only). Seen
// failing with the range 0-10.
TEST(pen_reports_no_pen)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    CHECK_EQ(said(r, "PRINT PEN(0);PEN(1);PEN(9)"), std::string(" 0  0  0"));
    CHECK_EQ(said(r, "PRINT PEN(10)"), std::string("Illegal function call"));
    for (const char *k : {"PEN ON", "PEN OFF", "PEN STOP"})
        CHECK_EQ(said(r, k), std::string());
    CHECK_EQ(said(r, "PEN GO"), std::string("Syntax error"));
    run_prog(r, {"10 ON PEN GOSUB 100:PEN ON:FOR I=1 TO 50:NEXT:PRINT N:END", "100 N=N+1:RETURN"}, ms(800));
    CHECK_EQ(line(r, 2), std::string(" 0"));
}

// N39 (7f's plan): with a key's trap on, every other key still types: KEY(1) ON (direct mode: nothing traps there),
// a line typed and run, and F9's text; and in a program with KEY(1) ON, Enter and letters through INKEY$. Guards
// typing broken by the match. Seen failing with KTRAP taking every key.
TEST(every_key_still_types_with_traps_armed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    CHECK_EQ(said(r, "KEY(1) ON:PRINT 3*4"), std::string(" 12"));
    run_prog(r, {"10 ON KEY(1) GOSUB 100:KEY(1) ON", "20 A$=INKEY$:IF A$<>\"\" THEN K$=K$+A$",
                 "30 IF RIGHT$(K$,1)<>\"x\" THEN 20", "40 PRINT K$:END", "100 RETURN"},
             ms(300));
    keys(r, "ab1");
    press(r, 0x42); // F9
    keys(r, "x");
    r.run(ms(500));
    CHECK_EQ(line(r, 2), std::string("AB1KEY X")); // (Caps Lock on)
}

// N21 (the manual's p. 344; the owner's answer 7): TIMER is the seconds since midnight, a single: at 23:59:58 its
// whole part 86,398 or 86,399; over half a second it never goes back, its fraction below 1 a second; across midnight
// it starts again from 0. Guards a clock read wrong by the minutes' factor, or TIMER going backwards. Seen failing
// with the minutes x 60 left out. (The fraction's cap at .999 isn't reached with the RTC running: holding the
// rig's clock at one second still let the next show, so no case forces it.)
TEST(timer_counts_the_seconds_since_midnight)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    r.set_time(2026, 11, 21, 23, 59, 58);
    const std::string t = said(r, "PRINT INT(TIMER)");
    CHECK(t == " 86398" || t == " 86399");
    r.set_time(2026, 11, 21, 10, 0, 0);
    run_prog(r, {"10 A=TIMER:B=A:N=0", "20 C=TIMER:IF C<B THEN N=N+1", "30 B=C:IF C<A+.5 AND C>=A THEN 20",
                 "40 PRINT N;C-A<.6:END"},
             ms(1500));
    CHECK_EQ(line(r, 2), std::string(" 0 -1"));
    r.set_time(2026, 11, 21, 23, 59, 59);
    r.run(ms(1500));
    const std::string m = said(r, "PRINT INT(TIMER)");
    CHECK(m == " 0" || m == " 1");
}

// N22 (the manual's p. 217): ON TIMER(1) GOSUB with TIMER ON: about one GOSUB a second (the 5th after 4-6 s), by
// the resident tick hook TIMHK (the owner's answer 23: OOLIB's, every 1,000 ms, counting n down and starting again);
// the hook set by TIMER ON and taken off by CLEARC (NEW); TIMER STOP after TIMER ON remembers (the
// GOSUB at the next TIMER ON), TIMER OFF forgets; TIMER ON 5 Syntax error with no hook set (review round 1, C4); ON
// TIMER(0) and (86401) Illegal function call, (86400) taken; ON TIMER(n) with TIMER on starts the count again (C2).
// Guards a timer trap firing every statement, or never. Seen failing with (a) TIMHK's count not started again after
// a request (one GOSUB), (b) TIMSET's restart left out (C2), (c) TIMERS' end checked after its far call (C4).
TEST(on_timer_fires_every_n_seconds)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    r.set_time(2026, 11, 21, 10, 0, 0);
    run_prog(r, {"10 ON TIMER(1) GOSUB 100:TIMER ON:T=TIMER", "20 IF N<5 THEN 20",
                 "30 E=TIMER-T:TIMER OFF:PRINT N;E>4;E<6:END", "100 N=N+1:RETURN"},
             ms(7000));
    CHECK_EQ(line(r, 2), std::string(" 5 -1 -1")); // (5 GOSUBs in about 5 s: TIMER's own fraction lags by < 1 s)
    for (const char *how : {"STOP", "OFF"})
    {
        run_prog(r, {std::string("10 ON TIMER(1) GOSUB 100:TIMER ON:TIMER ") + how + ":T=TIMER",
                     "20 IF TIMER<T+2.5 THEN 20", "30 TIMER ON:TIMER OFF:PRINT N:END", "100 N=N+1:RETURN"},
                 ms(4000));
        CHECK_EQ(line(r, 2), std::string(how) == "STOP" ? std::string(" 1") : std::string(" 0"));
    }
    // The hook (the owner's answer 23): TIMER ON sets it (TIMON), and CLEARC takes it off (NEW here), as do TIMER
    // OFF and SYSTEM.
    const uint16_t timon = lbl_map().at("TIMON");
    run_prog(r, {"10 ON TIMER(5) GOSUB 100:TIMER ON:END", "100 RETURN"}, ms(500));
    CHECK_EQ(r.phys(timon), 1); // (the TPA's own byte: the CPU's W0 may be OODOS's in a BDOS call)
    enter(r, "NEW");
    CHECK_EQ(r.phys(timon), 0);
    CHECK_EQ(said(r, "TIMER ON 5"), std::string("Syntax error")); // (review round 1, C4: the end checked first)
    CHECK_EQ(r.phys(timon), 0);
    CHECK_EQ(said(r, "ON TIMER(0) GOSUB 0"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "ON TIMER(86401) GOSUB 0"), std::string("Illegal function call"));
    CHECK_EQ(said(r, "ON TIMER(86400) GOSUB 0"), std::string());
    // ON TIMER(n) with TIMER on starts the count again from n (the manual's p. 217; review round 1, C2): TIMER ON
    // before any ON TIMER, and an ON TIMER(3600) changed to (1), each 2 GOSUBs in under 5 s.
    for (const char *arm : {"TIMER ON:ON TIMER(1) GOSUB 100", "ON TIMER(3600) GOSUB 100:TIMER ON:ON TIMER(1) GOSUB 100"})
    {
        run_prog(r, {std::string("10 ") + arm + ":T=TIMER", "20 IF N<2 AND TIMER<T+5 THEN 20",
                     "30 TIMER OFF:PRINT N:END", "100 N=N+1:RETURN"},
                 ms(6000));
        CHECK_EQ(line(r, 2), std::string(" 2"));
    }
}

// The tick hook asks once (review round 1, C1): ON TIMER(1) with TIMER ON, and four seconds' expiries in one
// statement (MF's second SOUND waiting for the first's 80 ticks); its GOSUB sets ON TIMER(1000). After it NEWSTT's
// trap test costs what it costs with ON TIMER(1000) armed and nothing asked (the hook's ONGSBF counted only as TIMREQ
// goes 0 to 1, so none is left over with no request behind it): NEWSTT to NEWST1, averaged over a loop's 2,400
// visits, the interrupt handlers' cycles left out (T17's method), within 30 cycles of each other. Guards every
// statement far calling P3's GOTRP after a long wait. Seen failing with TIMHK's ONGSBF counted at every expiry.
TEST(a_long_wait_leaves_no_timer_request_behind)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const std::map<std::string, uint16_t> lab = lbl_map();
    const uint16_t newstt = lab.at("NEWSTT"), newst1 = lab.at("NEWST1");
    auto per = [&](const std::string &arm) {
        gw_timing::LeftOut left(r, false);
        uint64_t from = 0, cycles = 0;
        unsigned visits = 0;
        bool in = false;
        r.watch = [&](uint16_t pc) {
            if (left.inside(pc, in))
                return;
            if (pc == newstt)
            {
                from = r.cpu->cycles();
                left.out = 0;
                in = true;
            }
            else if (pc == newst1 && in)
            {
                in = false;
                cycles += r.cpu->cycles() - from - left.out;
                ++visits;
            }
        };
        run_prog(r, {"10 " + arm, "20 I%=I%+1:IF I%<1200 THEN 20", "30 TIMER OFF:END",
                     "100 ON TIMER(1000) GOSUB 100:N=N+1:RETURN"},
                 ms(12000));
        r.watch = nullptr;
        CHECK(visits > 2400);
        return visits ? static_cast<double>(cycles) / visits : 0.0;
    };
    const double waited = per("ON TIMER(1) GOSUB 100:TIMER ON:SOUND 440,80:SOUND 440,1");
    CHECK_EQ(said(r, "PRINT N"), std::string(" 1")); // (the GOSUB taken once)
    const double armed = per("ON TIMER(1000) GOSUB 100:TIMER ON");
    std::printf("  NEWSTT after the long wait: %.1f cycles a statement, with ON TIMER armed only %.1f (within 30)\n",
                waited, armed);
    CHECK(waited - armed < 30 && armed - waited < 30);
}

// N23 (the manual's p. 286): RANDOMIZE TIMER seeds from the clock: runs at 10:00:00 and 10:00:05 give different
// RND sequences, and TIMER past 32,767 is no Overflow. Guards every run of a game the same; Overflow after 9 a.m.
// Seen failing with RANDOMIZE's seed through FRCINT.
TEST(randomize_timer_seeds_from_the_clock)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    std::vector<std::string> got;
    for (const unsigned sec : {0u, 5u})
    {
        r.set_time(2026, 11, 21, 10, 0, sec);
        run_prog(r, {"10 RANDOMIZE TIMER:PRINT RND;RND"}, ms(800));
        got.push_back(line(r, 2));
    }
    CHECK(got[0] != got[1]);
    for (const std::string &g : got)
        CHECK(g.find("Overflow") == std::string::npos && g.find('.') != std::string::npos);
}

// N24 (the manual's pp. 57-58 and TIME$; the owner's answer 8): DATE$ "mm-dd-yyyy" and TIME$ "hh:mm:ss" from the
// rig's clock, on 29 February 2028, 1 March 2028, 31 December 2099; DATE$= in mm-dd-yyyy and mm/dd/yyyy, one-digit
// month and day; a two-digit year (19yy: before 2000), 1999, 2100, 2-30, 13-1, an empty string Illegal function call;
// TIME$= "1:2" and "13:45:30" set the hour and the minute (the seconds 0: function 104's); 24:00 and 1:60 Illegal
// function call. Guards a wrong date on the files OODOS stamps; a leap day lost. Seen failing with the leap year
// test left out.
TEST(date_and_time_by_hand)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    r.set_time(2028, 2, 29, 10, 0, 0);
    CHECK_EQ(said(r, "PRINT DATE$;\" \";LEFT$(TIME$,5)"), std::string("02-29-2028 10:00"));
    r.set_time(2028, 3, 1, 23, 59, 30);
    CHECK_EQ(said(r, "PRINT DATE$;\" \";LEFT$(TIME$,5)"), std::string("03-01-2028 23:59"));
    r.set_time(2099, 12, 31, 7, 8, 9);
    CHECK_EQ(said(r, "PRINT DATE$"), std::string("12-31-2099"));
    for (const auto &[set, want] : std::vector<std::pair<std::string, std::string>>{
             {"03-15-2030", "03-15-2030"}, {"3/5/2031", "03-05-2031"}, {"2-29-2028", "02-29-2028"}, {"12/31/2099", "12-31-2099"}})
    {
        CHECK_EQ(said(r, "DATE$=\"" + set + "\""), std::string());
        CHECK_EQ(said(r, "PRINT DATE$"), want);
    }
    for (const char *bad : {"1-1-84", "1/1/26", "1-1-1999", "1-1-2100", "2-30-2030", "2-29-2030", "13-1-2030", "0-1-2030", "1-1", ""})
        CHECK_EQ(said(r, std::string("DATE$=\"") + bad + "\""), std::string("Illegal function call"));
    CHECK_EQ(said(r, "TIME$=\"1:2\":PRINT TIME$"), std::string("01:02:00"));
    CHECK_EQ(said(r, "TIME$=\"13:45:30\":PRINT LEFT$(TIME$,5)"), std::string("13:45"));
    for (const char *bad : {"24:00", "1:60", "1:2:60", ""})
        CHECK_EQ(said(r, std::string("TIME$=\"") + bad + "\""), std::string("Illegal function call"));
    CHECK_EQ(said(r, "PRINT DATE$"), std::string("12-31-2099"));
}

// N34 (the owner's answer 11: TIMER at most 10,000 cycles): T=TIMER in a loop against the loop with T=0, the
// interrupts left out (T17's method). Guards a TIMER too slow for a game's frame loop.
TEST(timer_is_timed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    const uint64_t none = run_cycles(r, {"10 FOR I=1 TO 50:T=0:NEXT"});
    const uint64_t with = run_cycles(r, {"10 FOR I=1 TO 50:T=TIMER:NEXT"});
    const double per = (static_cast<double>(with) - static_cast<double>(none)) / 50;
    std::printf("  T=TIMER: %.0f cycles, the interrupts left out (the gate 10,000)\n", per);
    CHECK(per > 500 && per <= 10000);
}


// N25 (the manual's p. 329, GWSTS.ASM's STICKF): the DE-9 sticks: STICK(0) latches both and gives 1's X; STICK(1)-
// (3) the latched 1's Y, 2's X and Y, unchanged when the sticks move until the next STICK(0); a digital stick reads
// 0, 128 or 255 (the OEM table's decision, against a PC's 1-200); STICK(4) Illegal function call. Guards a game
// reading a stale or the wrong axis. Seen failing with STICK(1) read afresh.
TEST(stick_reads_the_joysticks)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    JoystickPorts *j = r.machine->joysticks();
    REQUIRE(j != nullptr);
    j->set(0, JoystickPorts::LEFT | JoystickPorts::DOWN);
    j->set(1, JoystickPorts::RIGHT);
    r.run(ms(100));
    CHECK_EQ(said(r, "PRINT STICK(0);STICK(1);STICK(2);STICK(3)"), std::string(" 0  255  255  128"));
    j->set(0, JoystickPorts::UP);
    j->set(1, 0);
    r.run(ms(100));
    CHECK_EQ(said(r, "PRINT STICK(1);STICK(2);STICK(3)"), std::string(" 255  255  128")); // the latch
    CHECK_EQ(said(r, "PRINT STICK(0);STICK(1);STICK(2);STICK(3)"), std::string(" 128  0  128  128"));
    CHECK_EQ(said(r, "PRINT STICK(4)"), std::string("Illegal function call"));
}

// N26 (the manual's pp. 331-335, GWSTS.ASM's STRIGF and POLSTR): a press shorter than a statement is latched:
// STRIG(0) -1 once, then 0; STRIG(1) -1 while held, 0 after; joystick 2's STRIG(2), (3); STRIG(4)-(7) 0 (the DE-9
// sticks have one button); STRIG(8) Illegal function call; ON STRIG(0) GOSUB with STRIG(0) ON: one GOSUB a press,
// and the presses trapped don't set STRIG(0) (p. 333; the owner's answer 24 item 4).
// Guards a short press missed. Seen failing with the latch set from EV_CTL's bits now, not the events.
TEST(strig_latches_a_press_and_on_strig_traps_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    JoystickPorts *j = r.machine->joysticks();
    REQUIRE(j != nullptr);
    CHECK_EQ(said(r, "PRINT STRIG(0);STRIG(2)"), std::string(" 0  0"));
    j->set(0, JoystickPorts::FIRE);
    r.run(ms(60));
    j->set(0, 0);
    j->set(1, JoystickPorts::FIRE);
    r.run(ms(100));
    CHECK_EQ(said(r, "PRINT STRIG(0);STRIG(0);STRIG(1);STRIG(2);STRIG(3);STRIG(4);STRIG(5);STRIG(6);STRIG(7)"),
             std::string("-1  0  0 -1 -1  0  0  0  0"));
    j->set(1, 0);
    r.run(ms(100));
    CHECK_EQ(said(r, "PRINT STRIG(3);STRIG(2)"), std::string(" 0  0"));
    CHECK_EQ(said(r, "PRINT STRIG(8)"), std::string("Illegal function call"));
    run_prog(r, {"10 ON STRIG(0) GOSUB 100:STRIG(0) ON", "20 IF INKEY$<>\"9\" THEN 20", "30 PRINT N;STRIG(0):END",
                 "100 N=N+1:RETURN"},
             ms(300));
    for (int i = 0; i < 3; ++i)
    {
        j->set(0, JoystickPorts::FIRE);
        r.run(ms(60));
        j->set(0, 0);
        r.run(ms(150));
    }
    keys(r, "9");
    r.run(ms(500));
    CHECK_EQ(line(r, 2), std::string(" 3  0")); // (the trapped presses not latched: the manual's p. 333)
}


// The owner's answer 11's gate: a key with no trap armed costs within 30 cycles of main's. P6's KEYEV from its entry
// to its return (EVPOLL's far call to it), the interrupt handlers inside it left out (T17's method), the BDOS calls
// of its cooked-key drain too (how many there are depends on when the key's cooked twin comes: a race, not the
// code's), averaged over 8 presses each of A and of F1 (no text) while a program reads INKEY$, no trap on. The
// owner's answer 33: by gw_timing.h's method (each end by its return address as well as S: a tick inside a drain
// call once ended the call's leaving out, and the span, by S alone, counting some 200 cycles of the handler), re-
// anchored to main fa501cfb measured so on this rig on 2026-11-21: A 280, F1 340 (answer 29's 368 had a tick
// counted in it). Every press of a key costs the same; and the method itself: 24 presses of A at gaps 997 cycles
// apart put a tick inside a drain call (at least one does), and still cost the same. Seen failing with (a) KTRAP
// called with no trap on (KTON's test left out), (b) a BDOS call's end found by S alone (gw_timing.h's PC test
// dropped).
TEST(a_key_s_cost_with_no_trap_armed)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(start(r) > 0);
    enter(r, "KEY 1,\"\"");
    run_prog(r, {"10 A$=INKEY$:GOTO 10"}, ms(300));
    const uint16_t kev = lbl_map("p6.lbl").at("KEYEV");
    gw_timing::LeftOut left(r, true);
    gw_timing::CallSpan span;
    uint64_t total = 0, lo = ~uint64_t{0}, hi = 0;
    unsigned n = 0, ticked = 0; // (ticked: a tick inside one of the drain's BDOS calls)
    bool tick = false;
    r.watch = [&](uint16_t pc) {
        if (left.in_bdos && pc == left.irq)
            tick = true;
        if (left.inside(pc, span.open))
            return;
        if (!span.open && pc == kev)
        {
            span.enter(r);
            left.out = 0;
            tick = false;
        }
        else if (span.returned(r, pc))
        {
            const uint64_t c = r.cpu->cycles() - span.from - left.out;
            total += c;
            lo = std::min(lo, c);
            hi = std::max(hi, c);
            ticked += tick;
            ++n;
        }
    };
    // 8 presses 50 ms apart (or `sweep`: 24, each gap 997 cycles longer than the last, so the tick lands across the
    // drain's calls)
    auto avg = [&](uint8_t key, bool sweep = false) {
        n = ticked = 0;
        total = hi = 0;
        lo = ~uint64_t{0};
        for (unsigned i = 0; i < (sweep ? 24u : 8u); ++i)
        {
            press(r, key);
            r.run(ms(50) + (sweep ? i * 997u : 0u));
        }
        return n ? static_cast<double>(total) / n : 0.0;
    };
    const double a = avg(0x04);
    const uint64_t alo = lo, ahi = hi;
    const double f1 = avg(0x3A);
    const uint64_t flo = lo, fhi = hi;
    avg(0x04, true);
    r.watch = nullptr;
    std::printf("  KEYEV, no trap armed, less its interrupts and BDOS calls: A %.0f cycles (%llu-%llu), F1 %.0f "
                "(%llu-%llu) (main fa501cfb: A 280, F1 340; the gate + 30)\n", a, (unsigned long long)alo,
                (unsigned long long)ahi, f1, (unsigned long long)flo, (unsigned long long)fhi);
    std::printf("  A swept: %u presses, %u with a tick inside a drain call, %llu-%llu cycles\n", n, ticked,
                (unsigned long long)lo, (unsigned long long)hi);
    CHECK(a > 100 && a <= 280 + 30); // main fa501cfb on this rig (2026-11-21), gw_timing.h's method (answer 33)
    CHECK(f1 > 100 && f1 <= 340 + 30);
    CHECK(ticked > 0);
    CHECK(hi - lo <= 30);
}

int main()
{
    return run_all();
}
