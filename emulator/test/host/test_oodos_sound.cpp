/**
 * @file test_oodos_sound.cpp
 * @brief OOLIB's L3 (hw/oolib.md, "The plan") on sound card revision 3, the card as a device
 * (hw/future/sound-device.md and its interface details; the build plan's S13-S16): function 82's clean table,
 * its claims, OODOS's record sender and reply wait, the handler's events, the loads by DMA channel 2 into DATA,
 * the stream fed when the card asks (NEED), the beep and the console's bell on the card, the event area's sound
 * part and the warm and cold boots; and the acceptance items: a file on an SD drive streamed through function 82.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_input boots it (oodos_rig.h), on a machine whose sound card is revision 3 (the
 * rig's sound_revision). L3TEST (oodos/sys/test/l3test.s), run by GO, opens its area at $2000 and takes commands,
 * a letter at $2100; function 82's blocks the test builds at $2110, and the data they name it puts in physical
 * memory itself. L3STRM (l3strm.s) streams a file from B: as a program would, refilling its two buffers with
 * ordinary BDOS reads. The MP3 is the repo's own (test/host/mp3, tools/make_mp3_fixtures.sh); the raw PCM is
 * made here.
 */

#include "dma_controller.h"
#include "harness.h"
#include "minimp3.h"
#include "oodos_rig.h"
#include "sound_card3.h"
#include "sound_fw.h"
#include "usb_card.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const bool REV3 = (sound_revision() = 3, true); ///< every rig here: revision 3

    constexpr uint32_t AREA = 0x2000, MBOX = 0x2100, KBLK = 0x2110; ///< l3test.s's
    constexpr uint32_t PARMS = 0x2100, FCB = 0x2120;                 ///< l3strm.s's
    // oodos.inc's
    constexpr unsigned EV_LAYOUT = 0, EV_QHEAD = 16, EV_QTAIL = 17, EV_QSLOTS = 18, EV_QUEUE = 128;
    constexpr unsigned EV_SSTATE = 96, EV_SBUFS = 97, EV_SFREE = 98, EV_SPOS = 100, EV_SKBPS = 104, EV_SUNDER = 106;
    constexpr unsigned EV_TSTATE = 116, EV_TMS = 117, EV_MSTATE = 120, EV_MWAIT = 122;
    constexpr unsigned EK_SOUND = 6;
    constexpr uint8_t SN_INFO = 0, SN_CLAIM = 1, SN_RELEASE = 2, SN_IDENTIFY = 3, SN_FIRMWARE = 4, SN_TUNE = 5,
                      SN_PLAY = 6, SN_STOP = 7, SN_TPAUSE = 8, SN_TRESUME = 9, SN_TSTATUS = 10, SN_TINFO = 11,
                      SN_STREAM = 12, SN_BUFFER = 13, SN_PAUSE = 14, SN_RESUME = 15, SN_SEEK = 16, SN_END = 17,
                      SN_NEXT = 19, SN_SAMPLE = 20, SN_EFFECTS = 21, SN_EFFECT = 22, SN_ESTOP = 23, SN_MUSIC = 24,
                      SN_MCTL = 25, SN_MSTATUS = 26, SN_BEEP = 27, SN_VOLUME = 28, SN_MIX = 29, SN_EQ = 30,
                      SN_VIS = 31, SN_SIDREGS = 32, SN_LEVELS = 33, SN_OPS = 34;
    constexpr uint8_t SC_TUNE = 1, SC_STREAM = 2, SC_MUSIC = 4;
    constexpr uint8_t ST_BUFFERING = 1, ST_PLAYING = 2, ST_PAUSED = 3, ST_UNDERRUN = 4, ST_ENDED = 5;
    constexpr uint8_t FMT_STEREO = 1, FMT_16BIT = 2;

    uint8_t area(Rig &r, unsigned off) { return r.phys(AREA + off); }
    unsigned area16(Rig &r, unsigned off) { return area(r, off) | area(r, off + 1) << 8; }
    uint32_t area32(Rig &r, unsigned off) { return area16(r, off) | static_cast<uint32_t>(area16(r, off + 2)) << 16; }
    SoundCard3 &dev(Rig &r) { return *r.machine->sound_device(); }
    SoundFw &fw(Rig &r) { return dev(r).firmware(); }
    uint8_t reg(Rig &r, uint8_t offset) { return dev(r).peek(offset); }
    uint8_t sys(Rig &r, const char *name) { return r.phys(SYS_BLOCK_PHYS + sym(name)); }
    /// sound.s's variable, in SYSBSS or in block 61 (its BIOS segment, RAM).
    uint8_t sv(Rig &r, const char *name)
    {
        const uint16_t a = sym(name);
        return a >= 0x8000 ? r.phys(BIOS_BLOCK_PHYS + a - 0x8000u) : sys(r, name);
    }

    /// Deterministic bytes.
    std::vector<uint8_t> noise(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> b(n);
        for (auto &x : b)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            x = static_cast<uint8_t>(seed >> 8);
        }
        return b;
    }
    std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    std::vector<uint8_t> le24(uint32_t v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16)}; }
    std::vector<uint8_t> u32(uint32_t v)
    {
        return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    }
    uint32_t crc32(const std::vector<uint8_t> &b) { return Crc32::of(b.data(), b.size()); }

    /// L3TEST with `tail`, run to its OPEN line (or, for X, a little).
    size_t open(CcpRig &r, const std::string &tail)
    {
        r.poke(MBOX, 0);
        const size_t from = start(r, "l3test.com", " " + tail);
        if (tail != "X")
            CHECK(r.until("OPEN ", ms(2000), from) && r.until("\r\n", ms(200), r.out.find("OPEN ", from)));
        else
            r.run(ms(200));
        return from;
    }
    /// L3TEST's command `c`: its line's `label` and what follows, to the line's end.
    std::string command(CcpRig &r, char c, const std::string &label, uint64_t budget = ms(2000))
    {
        const size_t from = r.out.size() ? r.out.size() - 1 : 0;
        r.poke(MBOX, static_cast<uint8_t>(c));
        if (!r.until("\n" + label + " ", budget, from))
        {
            std::printf("  no %s line after command %c\n", label.c_str(), c);
            r.tail();
            return {};
        }
        const size_t at = r.out.find("\n" + label + " ", from);
        r.until("\r", ms(500), at + 1);
        return field(r.out.substr(from), label);
    }
    void wboot(CcpRig &r)
    {
        const size_t from = r.out.size();
        r.poke(MBOX, 'W');
        CHECK(r.until("\r\nA>", ms(2000), from));
    }
    /// Function 82 with `block` at KBLK: "aa xx".
    std::string f82(CcpRig &r, const std::vector<uint8_t> &block, uint64_t budget = ms(2000))
    {
        r.poke(KBLK, block);
        return command(r, 'K', "K", budget);
    }
    /// Function 82 with X = 0, A = `op`.
    std::string f82op(CcpRig &r, uint8_t op)
    {
        r.poke(MBOX + 1, op);
        return command(r, 'Z', "Z");
    }
    /// Function 82 with `block` at `at`, just below $C000 (L3TEST's B, which keeps the BDOS's jump there).
    std::string f82at(CcpRig &r, uint16_t at, const std::vector<uint8_t> &block)
    {
        r.poke(KBLK, block);
        r.poke(MBOX + 1, std::vector<uint8_t>{static_cast<uint8_t>(at), static_cast<uint8_t>(at >> 8),
                                              static_cast<uint8_t>(block.size())});
        return command(r, 'B', "B");
    }
    /// The block at KBLK's bytes from +1 after a call: what it was given back.
    std::vector<uint8_t> got(Rig &r, unsigned n)
    {
        std::vector<uint8_t> out;
        for (unsigned i = 0; i < n; ++i)
            out.push_back(r.phys(KBLK + 1 + i));
        return out;
    }
    std::vector<uint8_t> buffer_block(uint32_t phys, unsigned len)
    {
        return {SN_BUFFER, static_cast<uint8_t>(phys), static_cast<uint8_t>(phys >> 8), static_cast<uint8_t>(phys >> 16),
                static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8)};
    }
    std::vector<uint8_t> stream_block(uint8_t codec, unsigned rate, uint8_t fmt, uint8_t op = SN_STREAM)
    {
        return {op, codec, static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), fmt};
    }
    /// A load's block: `op`, then `pre` (a slot ...), then the address and the 3-byte length, then `post`.
    std::vector<uint8_t> load_block(uint8_t op, const std::vector<uint8_t> &pre, uint32_t phys, uint32_t len,
                                    const std::vector<uint8_t> &post = {})
    {
        return std::vector<uint8_t>{op} + pre + le24(phys) + le24(len) + post;
    }
    /// The events waiting in the area's queue: kind, code, value.
    struct Ev
    {
        unsigned kind, code, value;
    };
    std::vector<Ev> waiting(Rig &r)
    {
        std::vector<Ev> out;
        const unsigned slots = area(r, EV_QSLOTS);
        for (unsigned t = area(r, EV_QTAIL); slots && t != area(r, EV_QHEAD); t = (t + 1) % slots)
            out.push_back({area(r, EV_QUEUE + t * 8), area(r, EV_QUEUE + t * 8 + 1), area(r, EV_QUEUE + t * 8 + 2)});
        return out;
    }
    bool queued(Rig &r, unsigned code, int value = -1)
    {
        for (const Ev &e : waiting(r))
            if (e.kind == EK_SOUND && e.code == code && (value < 0 || e.value == static_cast<unsigned>(value)))
                return true;
        return false;
    }
    /// A record straight to the card from the test, the CPU stopped (so OODOS reads nothing meanwhile): its REPLY's
    /// status and results (its tag $7E; any other record read meanwhile dropped).
    std::vector<uint8_t> card_cmd(Rig &r, const std::vector<uint8_t> &rec)
    {
        SoundCard3 &d = dev(r);
        for (const uint8_t b : rec)
            d.write(SoundCard3::CMD, b);
        std::vector<uint8_t> reply;
        for (int k = 0; k < 400 && reply.empty(); ++k)
        {
            d.tick(ms(1) / 10);
            while (d.peek(SoundCard3::EVTLEN))
            {
                const uint8_t kind = d.read(SoundCard3::EVT), n = d.read(SoundCard3::EVT);
                std::vector<uint8_t> body;
                for (unsigned i = 0; i < n; ++i)
                    body.push_back(d.read(SoundCard3::EVT));
                if (kind == SoundFw::EV_REPLY && !body.empty() && body[0] == rec[2])
                    reply.assign(body.begin() + 1, body.end());
            }
        }
        return reply;
    }
    /// CHECKSUM of what the card holds: kind (the loading command), slot. @return the CRC, or 0 with none
    uint32_t card_crc(Rig &r, uint8_t kind, uint8_t slot)
    {
        const std::vector<uint8_t> rp = card_cmd(r, {SoundFw::CHECKSUM, 2, 0x7E, kind, slot});
        return rp.size() >= 5 && rp[0] == 0 ? static_cast<uint32_t>(rp[1] | rp[2] << 8 | rp[3] << 16 | rp[4] << 24) : 0;
    }

    /// The dispatcher's cost for each interrupt over a stretch, from the interrupt's first cycle to its RTI's last
    /// (test_oodos_input's way), by what interrupted: channel 2's terminal count (IRQH bit 0), a vertical blank
    /// (the frame counter moved), a tick alone.
    struct Costs
    {
        std::map<unsigned, unsigned> dma, frame, tick;
        unsigned pieces = 0; ///< PIECE's entries: channel 2's pieces started, in an interrupt or a call
        static unsigned most(const std::map<unsigned, unsigned> &m) { return m.empty() ? 0 : m.rbegin()->first; }
        static unsigned mode(const std::map<unsigned, unsigned> &m)
        {
            unsigned best = 0, n = 0;
            for (const auto &[c, k] : m)
                if (k > n)
                    best = c, n = k;
            return best;
        }
        static void print(const char *what, const std::map<unsigned, unsigned> &m)
        {
            std::printf("  %s:", what);
            for (const auto &[c, k] : m)
                std::printf(" %u x%u", c, k);
            std::printf("\n");
        }
    };
    void time_interrupts(Rig &r, Costs &out, const std::function<bool()> &until, uint64_t budget)
    {
        const uint16_t irq = sym("IRQ"), end = sym("IRQEND"), piece = sym("PIECE");
        const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
        uint64_t c0 = 0;
        uint8_t l = 0, h = 0, f = 0;
        bool in = false;
        const std::function<void(uint16_t)> before = r.watch; // a watch already set goes on
        r.watch = [&](uint16_t pc) {
            if (before)
                before(pc);
            if (pc == piece && r.nb(2) == 61)
                ++out.pieces;
            if (pc == irq)
            {
                c0 = r.cpu->cycles() - 7;
                l = r.nb(8);
                h = r.nb(9);
                f = r.phys(frames);
                in = true;
            }
            else if (pc == end && in)
            {
                const unsigned c = static_cast<unsigned>(r.cpu->cycles() + 6 - c0);
                if (!(h & 0x01))
                    ++out.dma[c];
                else if (r.phys(frames) != f)
                    ++out.frame[c];
                else if (l == 0xFF && h == 0xDF) // the bridge's tick alone (N9)
                    ++out.tick[c];
                in = false;
            }
        };
        const uint64_t endc = r.cpu->cycles() + budget;
        while (!until() && r.cpu->cycles() < endc)
            r.run(ms(1));
        r.watch = before;
    }

    /// Every part released and quiet, as the warm boot leaves the card: nothing claimed or moving, the card's event
    /// interrupt on and DRQ set, nothing playing, the store empty.
    void quiet(CcpRig &r)
    {
        CHECK_EQ(sys(r, "SCLAIM"), 0);
        CHECK_EQ(sys(r, "C2ON"), 0);
        CHECK_EQ(reg(r, SoundCard3::IEN), SoundCard3::INT_EVT);
        CHECK_EQ(reg(r, SoundCard3::CTRL), SoundCard3::CTRL_DRQ);
        CHECK_EQ(r.machine->dma()->peek_channel(2, DmaController::MODE) & 0x80, 0); // channel 2's MODE: its interrupt
        CHECK_EQ(r.machine->dma()->peek_channel(2, DmaController::CTRL) & 0x01, 0); // its CTRL's ENABLE
        CHECK_EQ(fw(r).tune_state(), SoundFw::T_STOPPED);
        CHECK_EQ(fw(r).music_state(), SoundFw::M_STOPPED);
        CHECK_EQ(fw(r).stream_state(0), SoundFw::S_IDLE);
        CHECK_EQ(fw(r).stream_state(1), SoundFw::S_IDLE);
        CHECK(!fw(r).beeping());
        CHECK_EQ(fw(r).store_free(), fw(r).store_bytes());
    }

    /// A PSID: load and init $1000, play $1000 + `play_at` (0: none), PAL, VBI (test_sound_fw's).
    std::vector<uint8_t> psid(std::vector<uint8_t> code, uint16_t play_at, uint16_t songs = 1)
    {
        std::vector<uint8_t> b(0x7C, 0);
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D', b[5] = 3, b[7] = 0x7C;
        b[0x08] = 0x10, b[0x0A] = 0x10;
        if (play_at)
            b[0x0C] = static_cast<uint8_t>(0x10 + (play_at >> 8)), b[0x0D] = static_cast<uint8_t>(play_at);
        b[0x0F] = static_cast<uint8_t>(songs), b[0x11] = 1, b[0x77] = 0x14;
        const char name[] = "A TEST TUNE";
        for (size_t i = 0; i < sizeof name - 1; ++i)
            b[0x16 + i] = static_cast<uint8_t>(name[i]);
        return b + code;
    }
    /// init's start: volume up, voice 1 a gated sawtooth (sustain 15); then RTS, and play: INC $D401, RTS.
    const std::vector<uint8_t> SAW = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0xA9, 0x20, 0x8D, 0x01, 0xD4,
                                      0xA9, 0xF0, 0x8D, 0x06, 0xD4, 0xA9, 0x21, 0x8D, 0x04, 0xD4};
    std::vector<uint8_t> tune() { return psid(SAW + std::vector<uint8_t>{0x60, 0xEE, 0x01, 0xD4, 0x60}, 21); }

    /// A note block (the S1 appendix's layout): voice 0 on `source`'s instrument, the entries given.
    std::vector<uint8_t> note_block(const std::vector<std::vector<uint8_t>> &entries, uint8_t source = 2)
    {
        std::vector<uint8_t> b{1, 0x01, 0, 0};
        b = b + u32(static_cast<uint32_t>(entries.size()));
        b = b + std::vector<uint8_t>{source, 0x40, 0x00, 0xF0, 0x00, 0x08, 255, 128};
        for (const auto &e : entries)
            b = b + e;
        return b;
    }
    /// A frequency entry on voice 0: Hz, its length and gate in µs.
    std::vector<uint8_t> tone(unsigned hz, uint32_t us, uint32_t gate) { return std::vector<uint8_t>{0x10} + le24(hz * 16) + u32(us) + u32(gate); }

    std::vector<uint8_t> mp3_file(const std::string &name)
    {
        std::ifstream in(std::string(EMU_MP3_DIR) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    /// minimp3 on its own, the whole file (mono on both sides), and, when it carries an encoder's LAME tag, trimmed
    /// as the card's gapless rule does (the Info frame left out, the encoder's delay and the decoder's 529 off the
    /// front, the padding less 529 off the end: test_sound_stream3's reference).
    std::vector<int16_t> mp3_reference(const std::vector<uint8_t> &mp3)
    {
        const char tag[] = "LAME";
        const auto lame = std::search(mp3.begin(), mp3.end(), tag, tag + 4);
        std::vector<int16_t> out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        size_t pos = 0;
        bool first = lame != mp3.end();
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            pos += static_cast<size_t>(info.frame_bytes);
            if (first)
            {
                first = false;
                continue;
            }
            for (int i = 0; i < n; ++i)
            {
                const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                out.push_back(l);
                out.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
            }
        }
        if (lame == mp3.end())
            return out;
        const size_t t = static_cast<size_t>(lame - mp3.begin()) + 21;
        const uint32_t delay = static_cast<uint32_t>(mp3[t] << 4 | mp3[t + 1] >> 4);
        const uint32_t padding = static_cast<uint32_t>((mp3[t + 1] & 0x0F) << 8 | mp3[t + 2]);
        const size_t head = 2 * (delay + 529), tail = 2 * (padding - 529);
        return std::vector<int16_t>(out.begin() + static_cast<std::ptrdiff_t>(head), out.end() - static_cast<std::ptrdiff_t>(tail));
    }
    /// The stream's frames as the card played them (its stream tap), as bytes (16-bit, low byte first).
    std::vector<uint8_t> tap_bytes(Rig &r)
    {
        std::vector<uint8_t> out;
        for (const int16_t s : fw(r).take_stream_tap())
        {
            out.push_back(static_cast<uint8_t>(s & 0xFF));
            out.push_back(static_cast<uint8_t>((s >> 8) & 0xFF));
        }
        return out;
    }

    /// L3STRM: `data` as B:NAME83 streamed with codec, rate and format. @return its output
    std::string stream_file(CcpRig &r, const std::string &name83, const std::vector<uint8_t> &data, uint8_t codec,
                            unsigned rate, uint8_t fmt, uint64_t budget, Costs &costs)
    {
        put_b(r, name83, data);
        r.poke(PARMS, std::vector<uint8_t>{codec, static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), fmt});
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = 2; // B:
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        r.poke(FCB, fcb);
        const size_t from = start(r, "l3strm.com");
        time_interrupts(r, costs, [&] { return r.out.find("END ", from) != std::string::npos; }, budget);
        CHECK(r.until("END ", ms(10), from) && r.until("\r\nA>", ms(1000), r.out.find("END ", from)));
        return r.out.substr(from);
    }

    /// The stream claimed and opened (`fmt` at `rate`), with a layout-2 area.
    void streaming(CcpRig &r, unsigned rate = 48000, uint8_t fmt = FMT_16BIT | FMT_STEREO, const std::string &tail = "2")
    {
        open(r, tail);
        CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
        CHECK_EQ(f82(r, stream_block(0, rate, fmt)), std::string("00 00"));
    }
    /// The 32-byte IDENTIFY block.
    std::vector<uint8_t> identify()
    {
        std::vector<uint8_t> id(33, 0);
        id[0] = SN_IDENTIFY;
        return id;
    }
    /// The tap's left channel: the samples not 0.
    size_t sounding(const std::vector<int16_t> &tap)
    {
        size_t n = 0;
        for (size_t i = 0; i < tap.size(); i += 2)
            n += tap[i] != 0;
        return n;
    }
} // namespace

// --- S13: the driver's core ------------------------------------------------------------------------------------

TEST(info_and_the_claims)
{
    // SN_INFO: revision 3, FEATURES, the three parts free; claims all or none; a part's operations need its claim;
    // an operation past the table, or one that needs a block and has none, is refused; IDENTIFY's 32 bytes.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    char want[32];
    std::snprintf(want, sizeof want, "03 %02X 03%02X07", fw(r).features(), fw(r).features());
    CHECK_EQ(command(r, 'I', "INFO"), std::string(want));
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("FF FF"));             // held
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE | SC_STREAM}), std::string("FF FF")); // one of them held: neither
    CHECK_EQ(f82(r, {SN_CLAIM, 0}), std::string("FF FF"));                   // none asked for
    CHECK_EQ(f82(r, {SN_CLAIM, 0x08}), std::string("FF FF"));                // no such part
    std::snprintf(want, sizeof want, "03 %02X 03%02X06", fw(r).features(), fw(r).features());
    CHECK_EQ(command(r, 'I', "INFO"), std::string(want));
    CHECK_EQ(sys(r, "SCLAIM"), SC_TUNE);
    CHECK_EQ(f82(r, {SN_RELEASE, SC_TUNE | SC_MUSIC}), std::string("00 00")); // what isn't held: nothing
    CHECK_EQ(sys(r, "SCLAIM"), 0);
    // A part's operations need its claim; an operation that isn't one; a block needed and not given.
    CHECK_EQ(f82op(r, SN_STOP), std::string("FF FF"));
    CHECK_EQ(f82(r, stream_block(0, 8000, 0)), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_MCTL, 0}), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_OPS), std::string("FF FF"));
    CHECK_EQ(f82op(r, 0xFF), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_CLAIM), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_IDENTIFY), std::string("FF FF"));
    std::snprintf(want, sizeof want, "03 %02X", fw(r).features());
    CHECK_EQ(f82op(r, SN_INFO), std::string(want));
    // IDENTIFY: the card's 32 bytes in the block, and no further.
    std::vector<uint8_t> id(34, 0xEE);
    id[0] = SN_IDENTIFY;
    CHECK_EQ(f82(r, id), std::string("00 00"));
    const std::vector<uint8_t> b = got(r, 33);
    CHECK_EQ(b[0], 0xA3);
    CHECK_EQ(b[1], SoundFw::VERSION_MAJOR);
    CHECK_EQ(b[2], SoundFw::VERSION_MINOR);
    CHECK_EQ(b[5], fw(r).memsize());
    CHECK_EQ(b[6], fw(r).features());
    CHECK_EQ(b[16], 4); // tune slots
    CHECK_EQ(b[17], 8); // music slots
    CHECK_EQ(b[32], 0xEE);
    // A load from OODOS's own memory (banks 6-7: the system's) is refused, as 51's segment is.
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00 00"));
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x070000, 16, {0})), std::string("FF FF"));
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x05FFF0, 32, {0})), std::string("FF FF")); // runs into bank 6
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x010000, 0, {0})), std::string("FF FF"));  // nothing to load
}

TEST(the_card_s_refusal_is_sn_refused_with_its_error)
{
    // A = SN_REFUSED ($FD), X = the card's error, when the card refused the command: a tune slot past 3 (E_PARAM), a
    // tune not loaded (E_SLOT), a beep below 20 Hz or above 20,000 (E_PARAM); a resume of what isn't paused
    // (E_STATE).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE | SC_STREAM}), std::string("00 00"));
    r.poke(0x10000, tune());
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x10000, static_cast<uint32_t>(tune().size()), {4})), std::string("FD 02"));
    CHECK_EQ(f82(r, {SN_PLAY, 2, 0, 0, 0, 0, 0, 0, 0, 0}), std::string("FD 03"));
    CHECK_EQ(f82(r, {SN_BEEP, 19, 0, 10, 0}), std::string("FD 02"));
    CHECK_EQ(f82(r, {SN_BEEP, 0x21, 0x4E, 10, 0}), std::string("FD 02"));
    CHECK_EQ(f82(r, stream_block(0, 8000, 0)), std::string("00 00"));
    CHECK_EQ(f82op(r, SN_RESUME), std::string("FD 08"));
}

TEST(a_program_s_record_and_the_handler_s_never_mix)
{
    // OODOS writes every record to CMD whole with interrupts off. The frame's status requests (the handler's
    // records, sent from the vertical blank's interrupt) are timed here to fall inside a program's IDENTIFY: by the
    // frame's own period, the program's command started a step earlier each frame until a frame's interrupt comes
    // while the record is being written. Both get their replies: IDENTIFY's 32 bytes, the area's fields. Must fail
    // with the sender's SEI left out (the records interleave: the card refuses one, E_PARAM, or takes the wrong
    // bytes as a record).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE | SC_STREAM | SC_MUSIC}), std::string("00 00")); // three requests a frame
    CHECK_EQ(f82(r, stream_block(0, 48000, FMT_16BIT | FMT_STEREO)), std::string("00 00"));
    const uint16_t sframe = sym("SFRAME"), cmdb = sym("CMDB"), irq = sym("IRQ");
    std::vector<uint64_t> frames; // each frame's interrupt: its entry (the IRQ that went on to SFRAME)
    uint64_t last_irq = 0;
    auto watch_frames = [&] {
        r.watch = [&](uint16_t pc) {
            if (pc == irq)
                last_irq = r.cpu->cycles();
            if (pc == sframe && r.nb(2) == 61)
                frames.push_back(last_irq);
        };
    };
    watch_frames();
    r.run(ms(60));
    REQUIRE(frames.size() >= 3);
    const uint64_t period = frames[2] - frames[1];
    // From the poke of 'K' to the program's first CMD write.
    r.poke(KBLK, identify());
    uint64_t first = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == cmdb && r.nb(2) == 61 && !first)
            first = r.cpu->cycles();
    };
    const uint64_t poked = r.cpu->cycles();
    CHECK_EQ(command(r, 'K', "K"), std::string("00 00"));
    REQUIRE(first > poked);
    const uint64_t lead = first - poked;
    std::printf("  a frame every %llu cycles; the program's record %llu cycles after the poke\n",
                static_cast<unsigned long long>(period), static_cast<unsigned long long>(lead));
    unsigned bad = 0, near = 0, tries = 0;
    for (unsigned d = 0; d <= 90; d += 3, ++tries)
    {
        frames.clear();
        watch_frames();
        while (frames.empty())
            r.run(100);
        const uint64_t at = frames.back() + period - lead - d;
        r.watch = nullptr;
        while (r.cpu->cycles() + 2100 < at)
            r.run(2000);
        uint64_t rec = 0, frame = 0;
        r.watch = [&](uint16_t pc) {
            if (pc == cmdb && r.nb(2) == 61 && !rec)
                rec = r.cpu->cycles();
            if (pc == irq)
                last_irq = r.cpu->cycles();
            if (pc == sframe && r.nb(2) == 61 && !frame)
                frame = last_irq; // the frame's interrupt's entry
        };
        while (r.cpu->cycles() < at)
            r.run(1);
        r.poke(KBLK, identify());
        const std::string res = command(r, 'K', "K");
        r.watch = nullptr;
        const std::vector<uint8_t> b = got(r, 1);
        if ((res != "00 00" || b[0] != 0xA3) && ++bad <= 3)
            std::printf("  try %u: IDENTIFY gave %s, its first byte $%02X\n", d, res.c_str(), b[0]);
        if (rec && frame && frame >= rec && frame - rec < 60) // its interrupt came during the record (taken at its end)
            ++near;
    }
    r.run(ms(40));
    std::printf("  %u tries: %u with the frame's interrupt taken as the program's record ended, %u bad; ERROR $%02X\n",
                tries, near, bad, reg(r, SoundCard3::ERROR));
    CHECK(near > 0); // the premise: a frame's interrupt fell at the program's record
    CHECK_EQ(bad, 0u);
    CHECK_EQ(reg(r, SoundCard3::ERROR), 0);
    CHECK_EQ(reg(r, SoundCard3::STATUS) & SoundCard3::STATUS_ERR, 0);
    CHECK_EQ(area(r, EV_SSTATE), ST_BUFFERING); // and the frame's replies reached the area
    CHECK_EQ(sv(r, "FPEND"), 0);
}

TEST(a_card_that_never_answers_is_sn_busy_in_100_ms)
{
    // A card whose firmware has stopped answering (its command latency made a second): the call gives up after 100
    // ms, SN_BUSY, and OODOS goes on; the late reply is dropped. Must fail with the wait's time-out left out (the
    // call hangs until the reply, a second later).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    fw(r).set_latency_us(1'000'000);
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f82(r, identify(), ms(600)), std::string("FE FF"));
    const double took = static_cast<double>(r.cpu->cycles() - c0) / 7159.09;
    std::printf("  SN_BUSY after %.1f ms\n", took);
    CHECK(took >= 99 && took < 140);
    fw(r).set_latency_us(SoundFw::LATENCY_US);
    r.run(ms(1100)); // the late reply comes, and is dropped
    CHECK_EQ(f82(r, identify()), std::string("00 00"));
    CHECK_EQ(got(r, 1)[0], 0xA3);
}

TEST(the_cold_boot_clears_a_half_record_left_by_the_rom)
{
    // A record half written to CMD before OODOS boots (as a Break inside the ROM's own record could leave it): the
    // cold boot's CTRL RESET drops it, so OODOS's first record parses and the card answers each in turn. Must fail
    // with the strobe left out (OODOS's records complete the stray one; the replies don't match).
    before_boot() = [](Rig &r) {
        r.machine->bus().write(0xC503, 0x04); // EVENTS, n 1 (right for it): its tag and mask still to come
        r.machine->bus().write(0xC503, 0x01);
    };
    CcpRig r;
    before_boot() = nullptr;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, identify()), std::string("00 00"));
    CHECK_EQ(got(r, 1)[0], 0xA3);
    CHECK_EQ(reg(r, SoundCard3::ERROR), 0);
    CHECK_EQ(fw(r).store_free(), fw(r).store_bytes()); // FREEALL ran
}

TEST(sn_beep_lasts_its_length_at_its_pitch_on_the_card)
{
    // SN_BEEP: BEEP on the card's tone generator, the call back when it has ended (the owner's answer 6), its pitch
    // exact. Must fail with SN_BEEP returning at the REPLY (the call's time a millisecond).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    dev(r).set_tap(true, SoundFw::SRC_BEEP);
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(f82(r, {SN_BEEP, 0xE8, 0x03, 200, 0}), std::string("00 00")); // 1,000 Hz, 200 ms
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / 7159090.0;
    const std::vector<int16_t> tap = dev(r).take_tap();
    dev(r).set_tap(false);
    size_t on = 0, off = 0; // the first and last samples sounding
    for (size_t i = 0; i < tap.size(); i += 2)
        if (tap[i] != 0)
            off = i / 2 + 1, on = on ? on : i / 2 + 1;
    std::vector<size_t> rises; // the left channel's rising zero crossings
    for (size_t i = 2; i < tap.size(); i += 2)
        if (tap[i - 2] < 0 && tap[i] >= 0)
            rises.push_back(i / 2);
    REQUIRE(rises.size() > 10);
    const double hz = static_cast<double>(rises.size() - 1) * 48000.0 / static_cast<double>(rises.back() - rises.front());
    const size_t n = off - on + 1;
    std::printf("  1,000 Hz for 200 ms: %.2f Hz, %zu samples from the first sounding to the last (%.1f ms), the call %.1f ms\n", hz, n, static_cast<double>(n) / 48.0,
                secs * 1000);
    CHECK(std::fabs(hz - 1000) < 1.0);
    CHECK(static_cast<double>(n) / 48.0 > 195 && static_cast<double>(n) / 48.0 < 201);
    CHECK(secs >= 0.200 && secs <= 0.215); // the command's own line included
}

TEST(a_bel_rings_the_card_s_bell)
{
    // S16: the console's BEL sends BELL to the card (no wait): its tone sounds from the card's flash. Must fail with
    // BEL dropped (crt.s's test of $07 taken out).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    dev(r).set_tap(true, SoundFw::SRC_BEEP);
    const uint64_t c0 = r.cpu->cycles();
    CHECK_EQ(command(r, 'G', "G").substr(3), std::string("00"));
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / 7159090.0;
    r.run(ms(200));
    const std::vector<int16_t> tap = dev(r).take_tap();
    dev(r).set_tap(false);
    const size_t n = sounding(tap);
    std::printf("  the bell: %zu samples sounding (%.1f ms); the BEL's line %.1f ms after its command\n", n, static_cast<double>(n) / 48.0,
                secs * 1000);
    CHECK(static_cast<double>(n) / 48.0 > 100); // the bell's 120 ms
    CHECK(secs < 0.05);    // not waited for
}

namespace
{
    /// A tune playing, music playing, the stream claimed and a buffer sent, the volumes, MIX and the EQ moved:
    /// everything the warm boot must put back.
    void claim_it_all(CcpRig &r)
    {
        open(r, "2");
        CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE | SC_STREAM | SC_MUSIC}), std::string("00 00"));
        r.poke(0x10000, tune());
        CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x10000, static_cast<uint32_t>(tune().size()), {0})), std::string("00 00"));
        CHECK_EQ(f82(r, {SN_PLAY, 0, 0, 0, 0, 0, 0, 0, 0, 0}), std::string("00 00"));
        CHECK_EQ(f82(r, stream_block(0, 48000, FMT_16BIT | FMT_STEREO)), std::string("00 00"));
        r.poke(0x20000, noise(0x10000, 3));
        CHECK_EQ(f82(r, buffer_block(0x20000, 0)), std::string("00 00"));
        const std::vector<uint8_t> blk = note_block({tone(440, 2'000'000, 2'000'000)});
        r.poke(0x30000, blk);
        CHECK_EQ(f82(r, load_block(SN_MUSIC, {0}, 0x30000, static_cast<uint32_t>(blk.size()), {2})), std::string("00 00"));
        CHECK_EQ(f82(r, {SN_VOLUME, 0, 0}), std::string("00 00"));
        CHECK_EQ(f82(r, {SN_MIX, 4, 0}), std::string("00 00"));   // the beep's source muted
        CHECK_EQ(f82(r, {SN_EQ, 0xFE, 1}), std::string("00 00")); // the EQ on
        CHECK_EQ(f82(r, {SN_EQ, 3, 0xF0}), std::string("00 00")); // a band at -8 dB
        r.run(ms(50));
        CHECK_EQ(fw(r).tune_state(), SoundFw::T_PLAYING);
        CHECK_EQ(fw(r).music_state(), SoundFw::M_PLAYING);
        CHECK_EQ(sys(r, "SCLAIM"), 7);
    }
    /// After the warm boot: a beep through every default (the volumes 255, the beep's MIX 255) is heard at the
    /// tap's full level.
    void defaults_heard(CcpRig &r)
    {
        open(r, "X");
        dev(r).set_tap(true);
        CHECK_EQ(f82(r, {SN_BEEP, 0xE8, 0x03, 20, 0}), std::string("00 00"));
        const std::vector<int16_t> tap = dev(r).take_tap();
        dev(r).set_tap(false);
        int most = 0;
        for (const int16_t s : tap)
            most = std::max(most, std::abs(static_cast<int>(s)));
        std::printf("  a beep after the warm boot: its peak %d\n", most);
        CHECK(most > 4000);
    }
} // namespace

TEST(the_warm_boot_quiets_the_card_and_puts_back_its_defaults_by_wboot_ctrl_c_and_break)
{
    // The warm boot (WBOOT, ^C, Break and OODOS booted again): every claim released, nothing playing, the store
    // empty (FREEALL), and the volumes, every source's MIX and the EQ as the card starts (CTRL's RESET: the owner's
    // answer 4, MIX's levels too). Must fail if the warm boot leaves the card as the program left it.
    {
        CcpRig r;
        REQUIRE(r.ok);
        quiet(r); // cold boot's
        claim_it_all(r);
        wboot(r);
        quiet(r);
        defaults_heard(r);
    }
    {
        CcpRig r;
        REQUIRE(r.ok);
        claim_it_all(r);
        const size_t from = r.out.size();
        r.poke(MBOX, 'D');
        CHECK(r.calls(CcpRig::C_READSTR_FN));
        r.type("\x03");
        CHECK(r.until("\r\nA>", ms(2000), from));
        quiet(r);
        defaults_heard(r);
    }
    {
        // Break: the ROM's QUIET, then OODOS booted again (its cold boot's RESET and FREEALL).
        CcpRig r;
        REQUIRE(r.ok);
        claim_it_all(r);
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        REQUIRE(r.boot());
        quiet(r);
        defaults_heard(r);
    }
}

TEST(a_layout_1_or_2_program_that_claims_nothing_is_unaffected)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    Costs costs;
    const uint64_t until = r.cpu->cycles() + ms(300);
    time_interrupts(r, costs, [&] { return r.cpu->cycles() >= until; }, ms(400));
    Costs::print("a frame, layout 2, nothing claimed", costs.frame);
    Costs::print("a tick", costs.tick);
    CHECK_EQ(Costs::mode(costs.tick), 241u); // the bridge's tick, a layout-2 area (N16, code placement)
    for (unsigned i = 96; i < 128; ++i)
        CHECK_EQ(area(r, i), 0); // the sound part: 0, as L2 reserved it
    CHECK_EQ(reg(r, SoundCard3::IEN), SoundCard3::INT_EVT); // the card's events: on whenever OODOS drives it
    wboot(r);
    r.poke(AREA + 16, std::vector<uint8_t>(240, 0)); // what layout 2 left, cleared: layout 1 writes 16 bytes
    open(r, "1");
    r.run(ms(50));
    CHECK_EQ(area(r, EV_LAYOUT), 1);
    for (unsigned i = 16; i < 128; ++i)
        CHECK_EQ(area(r, i), 0);
}

TEST(the_card_s_events_reach_the_queue_as_ek_sound)
{
    // An event the card sends (a beep's end; music's end; a tune's end) reaches a layout-2 area's queue as EK_SOUND,
    // its code the event's kind, its value the object; the tune's and music's fields are written each frame while
    // their part is claimed.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_BEEP, 0xE8, 0x03, 5, 0}), std::string("00 00"));
    r.run(ms(10));
    CHECK(queued(r, SoundFw::EV_BEEP_DONE, 0));
    CHECK_EQ(f82(r, {SN_CLAIM, SC_MUSIC | SC_TUNE}), std::string("00 00"));
    const std::vector<uint8_t> blk = note_block({tone(440, 100'000, 100'000), tone(660, 100'000, 100'000)});
    r.poke(0x30000, blk);
    CHECK_EQ(f82(r, load_block(SN_MUSIC, {5}, 0x30000, static_cast<uint32_t>(blk.size()), {2})), std::string("00 00"));
    r.run(ms(50));
    CHECK_EQ(area(r, EV_MSTATE), SoundFw::M_PLAYING);
    CHECK_EQ(area16(r, EV_MWAIT), 1u); // the second note waits
    r.run(ms(250));
    CHECK_EQ(area(r, EV_MSTATE), SoundFw::M_STOPPED);
    CHECK(queued(r, SoundFw::EV_MUSIC_DONE, 5)); // the last block's slot
    r.poke(0x10000, tune());
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x10000, static_cast<uint32_t>(tune().size()), {1})), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_PLAY, 1, 0, 0, 0xC8, 0, 0, 0, 0, 0}), std::string("00 00")); // 200 ms
    r.run(ms(100));
    CHECK_EQ(area(r, EV_TSTATE), SoundFw::T_PLAYING);
    const unsigned played = area(r, EV_TMS) | area(r, EV_TMS + 1) << 8 | area(r, EV_TMS + 2) << 16;
    std::printf("  the tune at 100 ms: %u ms played\n", played);
    CHECK(played > 50 && played < 110);
    r.run(ms(200));
    CHECK(queued(r, SoundFw::EV_TUNE_ENDED, 1));
}

// --- S14: the stream, fed when the card asks --------------------------------------------------------------------

TEST(a_load_is_refused_busy_while_a_stream_buffer_moves)
{
    // While a stream's buffer moves on channel 2 (its transfer open on the card, one at a time), a load is refused
    // SN_BUSY; a seek drops the buffers and the load goes.
    CcpRig r;
    REQUIRE(r.ok);
    streaming(r);
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00 00"));
    r.poke(0x10000, noise(0x10000, 5));
    r.poke(0x30000, tune());
    const std::vector<uint8_t> load = load_block(SN_TUNE, {}, 0x30000, static_cast<uint32_t>(tune().size()), {0});
    // L3TEST's L: a 64 KB buffer given, the card asking (its NEED): its STREAM_DATA goes at once; the load straight
    // after it is refused while that is answered and the buffer moves (a piece holds the CPU, so a program sees
    // only the wait for the reply).
    r.run(ms(20));
    CHECK_EQ(sv(r, "NEEDP"), 1);
    r.poke(KBLK, buffer_block(0x10000, 0));
    r.poke(0x2160, load);
    fw(r).set_latency_us(5000); // a card slower to answer: STREAM_DATA's reply 5 ms on, the load meanwhile
    CHECK_EQ(command(r, 'L', "L2"), std::string("FE FF"));
    fw(r).set_latency_us(SoundFw::LATENCY_US);
    CHECK_EQ(field(r.out.substr(r.out.rfind("\nL1 ")), "L1"), std::string("00 00"));

    CHECK_EQ(f82op(r, SN_SEEK), std::string("00 00"));
    CHECK_EQ(sys(r, "C2ON"), 0);
    CHECK_EQ(sv(r, "BBUSY"), 0);
    CHECK_EQ(f82(r, load), std::string("00 00"));
    CHECK_EQ(card_crc(r, SoundFw::TUNE_LOAD, 0), crc32(tune()));
}

TEST(a_stream_s_buffers_chain_across_needs_with_no_gap)
{
    // Six buffers of 10,000 bytes (pieces of 4,096, 4,096 and 1,808), each given again as OODOS frees it, each sent
    // when the card asks (NEED): the card plays them as one stream, frame for frame, with no underrun.
    CcpRig r;
    REQUIRE(r.ok);
    streaming(r);
    fw(r).set_stream_tap(true);
    constexpr unsigned LEN = 10000, BUFS = 6;
    const uint32_t at[2] = {0x03000, 0x06000};
    std::vector<uint8_t> sent;
    unsigned given = 0;
    Costs costs;
    auto give = [&](unsigned k) {
        const std::vector<uint8_t> b = noise(LEN, 100 + given);
        r.poke(at[k], b);
        CHECK_EQ(f82(r, buffer_block(at[k], LEN)), std::string("0") + static_cast<char>('0' + k) + " 00");
        sent.insert(sent.end(), b.begin(), b.end());
        ++given;
    };
    r.run(ms(20));
    CHECK_EQ(area(r, EV_SBUFS), 3);
    give(0);
    give(1);
    while (given < BUFS)
    {
        bool freed = false;
        time_interrupts(r, costs, [&] { return (area(r, EV_SBUFS) & 3) != 0; }, ms(500));
        for (unsigned k = 0; k < 2 && given < BUFS; ++k)
            if (area(r, EV_SBUFS) >> k & 1)
                give(k), freed = true;
        REQUIRE(freed);
    }
    time_interrupts(r, costs, [&] { return area(r, EV_SBUFS) == 3; }, ms(500));
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    time_interrupts(r, costs, [&] { return area(r, EV_SSTATE) == ST_ENDED; }, ms(1000));
    CHECK_EQ(area(r, EV_SSTATE), ST_ENDED);
    const std::vector<uint8_t> played = tap_bytes(r);
    std::printf("  %zu bytes sent, %zu played\n", sent.size(), played.size());
    CHECK(played == sent);
    CHECK_EQ(area16(r, EV_SUNDER), 0u);
    CHECK_EQ(area32(r, EV_SPOS), LEN * BUFS / 4);
    CHECK(area16(r, EV_SFREE) > 256); // the ring's room, as the card's NEED said: more than 64 KB
    CHECK_EQ(area16(r, EV_SKBPS), 0u);
}

TEST(pause_resume_seek_end_and_status)
{
    CcpRig r;
    REQUIRE(r.ok);
    streaming(r, 22050, FMT_16BIT);
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), ST_BUFFERING);
    r.poke(0x10000, noise(0x10000, 9));
    CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
    r.run(ms(200));
    CHECK_EQ(area(r, EV_SSTATE), ST_PLAYING);
    CHECK_EQ(area(r, EV_SBUFS), 3);
    CHECK(area32(r, EV_SPOS) > 1000);
    CHECK_EQ(f82op(r, SN_PAUSE), std::string("00 00"));
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), ST_PAUSED);
    const uint32_t pos = area32(r, EV_SPOS);
    r.run(ms(100));
    CHECK_EQ(area32(r, EV_SPOS), pos);
    CHECK_EQ(f82op(r, SN_RESUME), std::string("00 00"));
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), ST_PLAYING);
    // A seek: buffering again.
    CHECK_EQ(f82op(r, SN_SEEK), std::string("00 00"));
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), ST_BUFFERING);
    // Run dry with no EOF: an underrun, counted once, in the area and in the status block.
    CHECK_EQ(f82(r, buffer_block(0x10000, 8192)), std::string("00 00"));
    r.run(ms(400));
    CHECK_EQ(area(r, EV_SSTATE), ST_UNDERRUN);
    CHECK_EQ(area16(r, EV_SUNDER), 1u);
    const std::string st = command(r, 'S', "ST");
    CHECK_EQ(st.substr(0, 6), std::string("04 03 ")); // underrun, both free
    CHECK_EQ(st.substr(6, 2), std::string("04"));
    CHECK_EQ(st.substr(26, 4), std::string("0100")); // the underruns: 1
    // EOF: what is in plays out, then ended.
    CHECK_EQ(f82(r, buffer_block(0x10000, 4096)), std::string("01 00")); // in turn: the seek's first was 0
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    r.run(ms(300));
    CHECK_EQ(area(r, EV_SSTATE), ST_ENDED);
    CHECK_EQ(area16(r, EV_SUNDER), 1u);
}

TEST(two_files_queued_by_sn_next_join_with_no_gap)
{
    // The first stream's data, SN_END, then SN_NEXT: the second opened and queued behind it, fed by SN_BUFFER; the
    // card's tap has the second's first frame straight after the first's last (TRACK says so), and the area follows
    // the stream now playing.
    CcpRig r;
    REQUIRE(r.ok);
    streaming(r, 44100, FMT_16BIT | FMT_STEREO);
    fw(r).set_stream_tap(true);
    const std::vector<uint8_t> a = noise(40000, 61), b = noise(30000, 62);
    r.poke(0x10000, a);
    r.poke(0x20000, b);
    CHECK_EQ(f82(r, buffer_block(0x10000, static_cast<unsigned>(a.size()))), std::string("00 00"));
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    CHECK_EQ(f82(r, stream_block(0, 44100, FMT_16BIT | FMT_STEREO, SN_NEXT)), std::string("00 00"));
    CHECK_EQ(f82(r, buffer_block(0x20000, static_cast<unsigned>(b.size()))), std::string("01 00"));
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    r.run(ms(700));
    const std::vector<uint8_t> played = tap_bytes(r);
    std::printf("  %zu + %zu bytes queued, %zu played; the area's state %u, the stream playing %u\n", a.size(), b.size(),
                played.size(), area(r, EV_SSTATE), sv(r, "SPLAY"));
    CHECK(played == a + b);
    CHECK_EQ(sv(r, "SPLAY"), 1);
    CHECK_EQ(area(r, EV_SSTATE), ST_ENDED);
    CHECK(queued(r, SoundFw::EV_TRACK, 1));
}

TEST(a_stray_terminal_count_on_channel_6_is_deafened)
{
    // B3: a channel 4-7's interrupt that no driver turned on (a program's, against the rules) reaches IRQOTH and
    // DEAFEN, which clears its MODE.IRQ and its terminal count through SELECT 6 and puts CONTROL back.
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(!r.cmd("dir").empty());
    DmaController &dma = *r.machine->dma();
    Decoder &bus = r.machine->bus();
    const uint8_t control = dma.peek(DmaController::CONTROL);
    REQUIRE(control & DmaController::MASTER_ENABLE);
    const uint16_t deafen = sym("DEAFEN");
    unsigned deaf = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == deafen && r.nb(3) == 62)
            ++deaf;
    };
    constexpr uint32_t BYTE = 0x07FFF0;
    bus.write(0xC47E, static_cast<uint8_t>((control & ~DmaController::SELECT_MASK) | 6 << DmaController::SELECT_SHIFT));
    for (unsigned i = 0; i < 3; ++i)
    {
        bus.write(static_cast<uint16_t>(0xC400 + DmaController::ADDR + i), static_cast<uint8_t>(BYTE >> (8 * i)));
        bus.write(static_cast<uint16_t>(0xC400 + DmaController::DEST + i), static_cast<uint8_t>(BYTE >> (8 * i)));
    }
    bus.write(0xC400 + DmaController::COUNT, 1);
    bus.write(0xC400 + DmaController::COUNT + 1, 0);
    bus.write(0xC400 + DmaController::MODE, DmaController::IRQ | DmaController::MEM_TO_MEM);
    bus.write(0xC400 + DmaController::CTRL, DmaController::ENABLE | DmaController::REQ);
    bus.write(0xC47E, control);
    r.run(ms(5));
    r.watch = nullptr;
    std::printf("  DEAFEN run %u times for channel 6's terminal count\n", deaf);
    CHECK(deaf >= 1);
    CHECK_EQ(dma.peek_channel(6, DmaController::MODE), DmaController::MEM_TO_MEM);
    CHECK_EQ(dma.peek(DmaController::STATUS) & 0x40, 0);
    CHECK(!dma.irq());
    CHECK_EQ(dma.peek(DmaController::CONTROL), control);
    CHECK(!r.cmd("dir").empty());
}

namespace
{
    /// A stream moving with no event area: 48 kHz 16-bit stereo, two 64 KB buffers given (their pieces chained by
    /// terminal counts as the card takes them).
    void stream_moving(CcpRig &r)
    {
        open(r, "X");
        CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
        CHECK_EQ(f82(r, stream_block(0, 48000, FMT_16BIT | FMT_STEREO)), std::string("00 00"));
        r.poke(0x10000, noise(0x10000, 5));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("01 00"));
    }
} // namespace

TEST(chained_terminal_counts_in_a_program_are_not_strays)
{
    // The handler says which interrupts it answered. A terminal count still pulling when it returns (the next piece
    // run at once: the card's ring has room, so each piece goes whole) must be left to the next interrupt, not taken
    // for a stray: if what it answered were lost, DEAFEN would run for each.
    CcpRig r;
    REQUIRE(r.ok);
    stream_moving(r);
    const uint16_t deafen = sym("DEAFEN"), cnext = sym("CNEXT");
    unsigned deaf = 0, chained = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == deafen && r.nb(3) == 62)
            ++deaf;
        if (pc == cnext && r.nb(2) == 61)
            ++chained;
    };
    for (int k = 0; k < 6; ++k) // 64 KB buffers given as each goes, the program spinning between
    {
        f82(r, buffer_block(0x10000, 0));
        r.run(ms(30));
    }
    r.watch = nullptr;
    std::printf("  200 ms: %u pieces chained, DEAFEN run %u times\n", chained, deaf);
    CHECK(chained >= 5);
    CHECK_EQ(deaf, 0u);
}

TEST(a_stray_in_a_bdos_call_is_turned_off_while_a_stream_moves)
{
    // The USB card's interrupt left on with no layout-2 area, while a stream moves and the program waits in function
    // 10: the stray must be turned off (DEAFEN), not taken as answered, and the call go on.
    CcpRig r;
    REQUIRE(r.ok);
    stream_moving(r);
    const size_t from = r.out.size();
    r.poke(MBOX, 'D');
    CHECK(r.calls(CcpRig::C_READSTR_FN));
    r.machine->bus().write(0xC5B0, 0x0F); // USB_IEN: all four, against the rules
    r.machine->usb_card()->key(0x04, true);
    r.run(ms(20));
    CHECK_EQ(r.machine->bus().peek(0xC5B0), 0);
    r.machine->usb_card()->key(0x04, false);
    r.type("x\r");
    CHECK(r.until("LINEOK", ms(2000), from));
}

TEST(acceptance_44_1_khz_16_bit_stereo_raw_pcm_from_b_with_no_underrun)
{
    // 1.3 s of 44.1 kHz 16-bit stereo, 1,792 records (229,376 bytes, whole records: a ^Z-padded last one would play
    // its padding), L3STRM's refills sent as the card asks.
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> pcm = noise(1792 * 128, 44);
    fw(r).set_stream_tap(true);
    const uint64_t c0 = r.cpu->cycles();
    Costs costs;
    const std::string o = stream_file(r, "TONE    PCM", pcm, 0, 44100, FMT_16BIT | FMT_STEREO, ms(4000), costs);
    std::printf("  %s", vis(o).c_str());
    std::printf("\n  %.2f s of machine time\n", static_cast<double>(r.cpu->cycles() - c0) / 7159090.0);
    CHECK_EQ(field(o, "CLAIM"), std::string("00 00"));
    CHECK_EQ(field(o, "FILE"), std::string("00 00"));
    CHECK_EQ(field(o, "STREAM"), std::string("00 00"));
    CHECK(o.find("SLOT") == std::string::npos && o.find("BUSY") == std::string::npos);
    CHECK_EQ(field(o, "SENT"), std::string("0013")); // 18 buffers of 96 records, then one of 64
    const std::string end = field(o, "END");
    CHECK_EQ(end.substr(0, 6), std::string("05 03 "));        // ended, both buffers free
    CHECK_EQ(end.substr(end.size() - 4), std::string("0000")); // no underrun
    const std::vector<uint8_t> played = tap_bytes(r);
    std::printf("  %zu bytes in the file, %zu at the card's stream tap\n", pcm.size(), played.size());
    CHECK(played == pcm);
    quiet(r); // L3STRM released it, and the warm boot after it
    Costs::print("channel 2's terminal count", costs.dma);
    Costs::print("a frame, the stream claimed", costs.frame);
    Costs::print("a tick", costs.tick);
    REQUIRE(!costs.dma.empty());
    CHECK(costs.dma.begin()->first < 1000);
    CHECK_EQ(Costs::mode(costs.tick), 241u); // a layout-2 area's tick: the stream costs it nothing
}

TEST(acceptance_an_mp3_from_b_plays_as_minimp3_decodes_it)
{
    // The card's decoding as minimp3's of the whole file, trimmed as the card's gapless rule does when the file
    // carries an encoder's LAME tag (the spec's Q11).
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> mp3 = mp3_file("cbr320_stereo_48k.mp3");
    REQUIRE(!mp3.empty());
    fw(r).set_stream_tap(true);
    Costs costs;
    const std::string o = stream_file(r, "SONG    MP3", mp3, 1, 0, 0, ms(5000), costs);
    std::printf("  %s\n", vis(o.substr(o.find("SENT"))).c_str());
    const std::string end = field(o, "END");
    CHECK_EQ(end.substr(0, 6), std::string("05 03 "));
    CHECK_EQ(end.substr(end.size() - 4), std::string("0000"));
    const std::vector<int16_t> want = mp3_reference(mp3);
    const std::vector<int16_t> got = fw(r).take_stream_tap();
    std::printf("  %zu bytes of MP3, %zu frames by minimp3, %zu at the card's stream tap\n", mp3.size(), want.size() / 2,
                got.size() / 2);
    CHECK(got == want);
}

TEST(a_file_bigger_than_the_store_streams_from_b_with_no_underrun)
{
    // The spec's acceptance, from an OODOS program: a file bigger than the card's store (a 2 MB card: 2.2 MB of 48
    // kHz 16-bit stereo, 11.7 s) streamed by L3STRM from B:, fed as the card asks: no underrun, every byte played.
    CcpRig r({}, machine_with_sound(3, 2));
    REQUIRE(r.ok);
    REQUIRE(fw(r).store_bytes() == 2u << 20);
    const std::vector<uint8_t> pcm = noise(17600 * 128, 45); // 2,252,800 bytes
    fw(r).set_stream_tap(true);
    Costs costs;
    const std::string o = stream_file(r, "BIG     PCM", pcm, 0, 48000, FMT_16BIT | FMT_STEREO, ms(16000), costs);
    const std::string end = field(o, "END");
    std::printf("  %zu bytes through a ring of %zu: END %s\n", pcm.size(), fw(r).stream_ring(0), end.c_str());
    CHECK_EQ(end.substr(0, 6), std::string("05 03 "));
    CHECK_EQ(end.substr(end.size() - 4), std::string("0000"));
    const std::vector<uint8_t> played = tap_bytes(r);
    CHECK_EQ(played.size(), pcm.size());
    CHECK(played == pcm);
}

TEST(the_stream_s_cost_to_the_host)
{
    // A record, not a gate (the build plan's section 8: S0's figures re-measured on the device): 44.1 kHz 16-bit
    // stereo raw PCM from memory, buffers of 16 KB given as OODOS frees them, each sent when the card asks. A piece is
    // 4 KB by channel 2 at a byte a cycle, its terminal count's handler starting the next; each frame with the stream
    // claimed asks for its status. The fewest cycles a terminal count's interrupt took is the handler.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    Costs idle;
    time_interrupts(r, idle, [] { return false; }, ms(300)); // nothing claimed: the frame's own cost
    CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
    CHECK_EQ(f82(r, stream_block(0, 44100, FMT_16BIT | FMT_STEREO)), std::string("00 00"));
    constexpr unsigned LEN = 16384, BUFS = 16;
    const uint32_t at[2] = {0x10000, 0x14000};
    unsigned given = 0;
    auto give = [&](unsigned k) {
        r.poke(at[k], noise(LEN, 200 + given));
        CHECK_EQ(f82(r, buffer_block(at[k], LEN)), std::string("0") + static_cast<char>('0' + k) + " 00");
        ++given;
    };
    Costs costs;
    const uint64_t c0 = r.cpu->cycles();
    time_interrupts(r, costs, [&] {
        for (unsigned k = 0; k < 2 && given < BUFS; ++k)
            if (given < 2 || area(r, EV_SBUFS) >> k & 1)
                give(given < 2 ? given : k);
        return given == BUFS && area(r, EV_SBUFS) == 3;
    }, ms(3000));
    CHECK_EQ(given, BUFS);
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / SoundCard3::NATIVE_HZ;
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    Costs::print("channel 2's terminal count", costs.dma);
    Costs::print("a frame, the stream claimed", costs.frame);
    Costs::print("a frame, nothing claimed", idle.frame);
    REQUIRE(!costs.dma.empty() && !costs.frame.empty() && !idle.frame.empty());
    const unsigned pieces = costs.pieces; // PIECE's entries (a terminal count may be served in the card's interrupt)
    const unsigned handler = costs.dma.begin()->first;
    const double frame_extra = static_cast<double>(Costs::mode(costs.frame)) - Costs::mode(idle.frame);
    const double per_piece = 4096.0 + handler;
    const double per_second = 176400.0 / 4096.0 * per_piece + 60.0 * frame_extra;
    std::printf("  %u pieces over %.2f s; a piece: 4,096 DMA cycles + a handler of %u = %.0f cycles; a frame, the "
                "stream claimed, %.0f more than a frame alone\n",
                pieces, secs, handler, per_piece, frame_extra);
    std::printf("  a second of 44.1 kHz stereo: about %.0f cycles of the host, %.1f%% of 7.16 MHz\n", per_second,
                per_second / SoundCard3::NATIVE_HZ * 100);
    CHECK(pieces >= 60); // premise: 256 KB went, in pieces of 4 KB
}

// --- S15: the new operations, a group each ---------------------------------------------------------------------

TEST(tunes_load_play_pause_status_and_info)
{
    // SN_TUNE (a .sid file by DMA, checked by the card), SN_PLAY, SN_TPAUSE/SN_TRESUME, SN_TSTATUS's 16 bytes,
    // SN_TINFO's 104 (the header's name), SN_STOP; the claim needed; the bytes as they came (CHECKSUM).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    const std::vector<uint8_t> t = tune();
    r.poke(0x10000, t);
    const std::vector<uint8_t> load = load_block(SN_TUNE, {}, 0x10000, static_cast<uint32_t>(t.size()), {2});
    CHECK_EQ(f82(r, load), std::string("FF FF")); // not claimed
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00 00"));
    CHECK_EQ(f82(r, load), std::string("00 00"));
    CHECK_EQ(card_crc(r, SoundFw::TUNE_LOAD, 2), crc32(t));
    r.poke(0x18000, std::vector<uint8_t>(200, 0xEA)); // not a .sid: the card's E_DATA at XFERDONE
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x18000, 200, {3})), std::string("FD 06"));
    CHECK_EQ(f82(r, {SN_PLAY, 2, 0, 0, 0, 0, 0, 0, 0, 0}), std::string("00 00"));
    r.run(ms(100));
    std::vector<uint8_t> st(17, 0);
    st[0] = SN_TSTATUS;
    CHECK_EQ(f82(r, st), std::string("00 00"));
    std::vector<uint8_t> s = got(r, 16);
    CHECK_EQ(s[0], SoundFw::T_PLAYING);
    CHECK_EQ(s[1], 2);
    CHECK((s[8] | s[9] << 8) > 3); // play calls
    CHECK_EQ(f82op(r, SN_TPAUSE), std::string("00 00"));
    CHECK_EQ(fw(r).tune_state(), SoundFw::T_PAUSED);
    CHECK_EQ(f82op(r, SN_TRESUME), std::string("00 00"));
    CHECK_EQ(fw(r).tune_state(), SoundFw::T_PLAYING);
    std::vector<uint8_t> in(106, 0);
    in[0] = SN_TINFO, in[1] = 2;
    CHECK_EQ(f82(r, in), std::string("00 00"));
    s = got(r, 105);
    CHECK_EQ(s[1] | s[2] << 8, 1); // one song
    CHECK_EQ(std::string(s.begin() + 9, s.begin() + 20), std::string("A TEST TUNE"));
    CHECK_EQ(f82op(r, SN_STOP), std::string("00 00"));
    CHECK_EQ(fw(r).tune_state(), SoundFw::T_STOPPED);
}

TEST(the_bank_sample_effects_play_and_stop)
{
    // SN_SAMPLE (a sample by DMA, its flags, rate and loop), SN_EFFECTS (a table of definitions), SN_EFFECT (A the
    // voice handle), SN_ESTOP; no claim (effects take free voices).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    std::vector<uint8_t> smp(4000);
    for (size_t i = 0; i < smp.size(); ++i)
        smp[i] = static_cast<uint8_t>(128 + 100 * std::sin(static_cast<double>(i) * 0.1));
    r.poke(0x10000, smp);
    CHECK_EQ(f82(r, load_block(SN_SAMPLE, {7}, 0x10000, static_cast<uint32_t>(smp.size()), {0, 0x40, 0x1F, 0, 0, 0, 0, 0, 0})),
             std::string("00 00")); // unsigned, 8,000 Hz, no loop
    CHECK_EQ(card_crc(r, SoundFw::SAMPLE_LOAD, 7), crc32(smp));
    // Two effects: a sample one (slot 7), and a SID one (a step of 50 ms, a gated sawtooth).
    const std::vector<uint8_t> table = std::vector<uint8_t>{12, 1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} +
                                       std::vector<uint8_t>{16, 0, 1, 0xFF, 50, 0, 0x21, 0x00, 0x1C, 0x00, 0x00, 0x08, 0x00, 0xF0, 0, 0, 0};
    r.poke(0x18000, table);
    CHECK_EQ(f82(r, load_block(SN_EFFECTS, {10}, 0x18000, static_cast<uint32_t>(table.size()))), std::string("00 00"));
    const std::string v1 = f82(r, {SN_EFFECT, 10, 255, 128, 1, 0, 0, 2}); // the sample, on the ES5503
    const std::string v2 = f82(r, {SN_EFFECT, 11, 255, 128, 1, 0, 0, 1}); // the SID one
    std::printf("  voice handles %s, %s\n", v1.c_str(), v2.c_str());
    CHECK(v1.size() == 5 && v1.substr(3) == "00" && v1.substr(0, 2) != "00" && v1.substr(0, 2) != "FD" && v1.substr(0, 2) != "FF");
    CHECK(v2.size() == 5 && v2.substr(3) == "00" && v2.substr(0, 2) != "00" && v2.substr(0, 2) != "FD" && v2 != v1);
    r.run(ms(10));
    unsigned busy = 0;
    for (unsigned v = 0; v < SoundFw::VOICES; ++v)
        busy += fw(r).voice_use(v) == SoundFw::V_EFFECT;
    CHECK_EQ(busy, 2u);
    CHECK_EQ(f82(r, {SN_ESTOP, 0}), std::string("00 00")); // every effect voice
    r.run(ms(10));
    busy = 0;
    for (unsigned v = 0; v < SoundFw::VOICES; ++v)
        busy += fw(r).voice_use(v) == SoundFw::V_EFFECT;
    CHECK_EQ(busy, 0u);
}

TEST(music_block_control_and_status)
{
    // SN_MUSIC (a note block by DMA, checked, then played or queued), SN_MCTL (pause, resume, the threshold, stop),
    // SN_MSTATUS's 24 bytes; the claim needed; a block that doesn't parse refused (E_DATA).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    const std::vector<uint8_t> blk = note_block({tone(440, 300'000, 300'000), tone(550, 300'000, 300'000),
                                                 tone(660, 300'000, 300'000)});
    r.poke(0x10000, blk);
    const std::vector<uint8_t> play = load_block(SN_MUSIC, {0}, 0x10000, static_cast<uint32_t>(blk.size()), {2});
    CHECK_EQ(f82(r, play), std::string("FF FF")); // not claimed
    CHECK_EQ(f82(r, {SN_CLAIM, SC_MUSIC}), std::string("00 00"));
    CHECK_EQ(f82(r, play), std::string("00 00"));
    CHECK_EQ(card_crc(r, SoundFw::MUSIC_LOAD, 0), crc32(blk));
    std::vector<uint8_t> msb(25, 0);
    msb[0] = SN_MSTATUS;
    CHECK_EQ(f82(r, msb), std::string("00 00"));
    const std::vector<uint8_t> s = got(r, 24);
    CHECK_EQ(s[0], SoundFw::M_PLAYING);
    CHECK_EQ(s[22] | s[23] << 8, 2); // two waiting (the one sounding isn't counted)
    CHECK_EQ(f82(r, {SN_MCTL, 1}), std::string("00 00"));
    CHECK_EQ(fw(r).music_state(), SoundFw::M_PAUSED);
    CHECK_EQ(f82(r, {SN_MCTL, 2}), std::string("00 00"));
    CHECK_EQ(fw(r).music_state(), SoundFw::M_PLAYING);
    CHECK_EQ(f82(r, {SN_MCTL, 3, 2}), std::string("00 00")); // a MUSIC_COUNT when the waiting fall from 2 to 1
    r.run(ms(400));
    CHECK(queued(r, SoundFw::EV_MUSIC_COUNT));
    CHECK_EQ(f82(r, {SN_MCTL, 0}), std::string("00 00"));
    CHECK_EQ(fw(r).music_state(), SoundFw::M_STOPPED);
    CHECK_EQ(f82(r, {SN_MCTL, 4}), std::string("FF FF"));
    std::vector<uint8_t> bad = blk;
    bad[1] = 0x02; // the voice mask says voice 1; the entries are voice 0's
    r.poke(0x10000, bad);
    CHECK_EQ(f82(r, play), std::string("FD 06"));
}

TEST(the_mix_eq_vis_sidregs_and_levels)
{
    // SN_VOLUME, SN_MIX, SN_EQ (a band, all eleven, on and off), SN_VIS's 98 bytes, SN_SIDREGS's 50 (a tune's SID
    // writes), SN_LEVELS's 10; no claim; a source or band out of range is the card's E_PARAM.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_VOLUME, 0x80, 0x40}), std::string("00 00"));
    CHECK_EQ(f82op(r, SN_VOLUME), std::string("FF FF"));                // a block needed
    CHECK_EQ(f82at(r, 0xBFFE, {SN_VOLUME, 0x40}), std::string("FF FF")); // +2 at $C000, the I/O hole: refused
    CHECK_EQ(f82(r, {SN_VOLUME, 0xFF, 0xFF}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_MIX, 1, 0x20}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_MIX, 5, 0x20}), std::string("FD 02"));
    CHECK_EQ(f82(r, {SN_EQ, 4, 6}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_EQ, 11, 6}), std::string("FD 02"));
    CHECK_EQ(f82(r, std::vector<uint8_t>{SN_EQ, 0xFF} + std::vector<uint8_t>(11, 2)), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_EQ, 0xFE, 1}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_EQ, 0xFE, 0}), std::string("00 00"));
    // A tune's SID writes in SIDREGS: its init's volume 15 and the sawtooth on voice 1.
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE}), std::string("00 00"));
    r.poke(0x10000, tune());
    CHECK_EQ(f82(r, load_block(SN_TUNE, {}, 0x10000, static_cast<uint32_t>(tune().size()), {0})), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_PLAY, 0, 0, 0, 0, 0, 0, 0, 0, 0}), std::string("00 00"));
    r.run(ms(100));
    std::vector<uint8_t> sr(51, 0);
    sr[0] = SN_SIDREGS;
    CHECK_EQ(f82(r, sr), std::string("00 00"));
    std::vector<uint8_t> s = got(r, 50);
    CHECK_EQ(s[0x18] & 0x0F, 0x0F);
    CHECK_EQ(s[0x04], 0x21);
    CHECK_EQ(s[0x06], 0xF0);
    std::vector<uint8_t> vis(99, 0);
    vis[0] = SN_VIS;
    CHECK_EQ(f82(r, vis), std::string("00 00"));
    s = got(r, 98);
    unsigned bands = 0;
    for (unsigned i = 0; i < 32; ++i)
        bands += s[i];
    CHECK(bands > 0); // the tune's sawtooth shows in the bands
    std::vector<uint8_t> lv(12, 0xEE);
    lv[0] = SN_LEVELS;
    CHECK_EQ(f82(r, lv), std::string("00 00"));
    s = got(r, 11);
    CHECK(s[8] > 0 || s[9] > 0); // the VU pair
    CHECK_EQ(s[10], 0xEE);       // and no further
}

TEST(sn_firmware_updates_the_card_s_image)
{
    // SN_FIRMWARE: begin with the image's length (its first piece with it), the next piece into DATA by channel 2,
    // commit: FWRESULT's status. A good image is taken (marked to boot), a bad one (its CRC) refused E_FIRMWARE; no
    // update while a part is claimed.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    auto image = [](uint8_t major, uint8_t minor, bool good) {
        std::vector<uint8_t> body = noise(3000, 77);
        std::vector<uint8_t> h(32, 0);
        h[0] = 'O', h[1] = 'O', h[2] = 'S', h[3] = 'D', h[4] = 0xA3, h[5] = 1, h[6] = major, h[7] = minor;
        const uint32_t len = static_cast<uint32_t>(body.size()), crc = Crc32::of(body.data(), body.size()) ^ (good ? 0u : 1u);
        for (unsigned i = 0; i < 4; ++i)
            h[12 + i] = static_cast<uint8_t>(len >> (8 * i)), h[16 + i] = static_cast<uint8_t>(crc >> (8 * i));
        const uint32_t hc = Crc32::of(h.data(), 28);
        for (unsigned i = 0; i < 4; ++i)
            h[28 + i] = static_cast<uint8_t>(hc >> (8 * i));
        return h + body;
    };
    auto update = [&](const std::vector<uint8_t> &img) {
        r.poke(0x10000, img);
        const uint32_t len = static_cast<uint32_t>(img.size()), half = len / 2;
        const std::string a = f82(r, std::vector<uint8_t>{SN_FIRMWARE} + le24(0x10000) + le24(half) + std::vector<uint8_t>{1} + u32(len));
        const std::string b = f82(r, std::vector<uint8_t>{SN_FIRMWARE} + le24(0x10000 + half) + le24(len - half) + std::vector<uint8_t>{0});
        const std::string c = f82(r, std::vector<uint8_t>{SN_FIRMWARE, 0, 0, 0, 0, 0, 0, 2});
        return a + " " + b + " " + c;
    };
    CHECK_EQ(update(image(1, 7, false)), std::string("00 00 00 00 FD 0B"));
    CHECK_EQ(fw(r).boot_slot(), fw(r).running_slot());
    CHECK_EQ(update(image(1, 7, true)), std::string("00 00 00 00 00 00"));
    CHECK_EQ(fw(r).boot_slot(), 1 - fw(r).running_slot());
    CHECK_EQ(f82(r, {SN_CLAIM, SC_MUSIC}), std::string("00 00"));
    CHECK_EQ(f82(r, std::vector<uint8_t>{SN_FIRMWARE, 0, 0, 0, 0, 0, 0, 2}), std::string("FF FF"));
}

TEST(each_load_s_address_is_checked_as_function_51_s_segment)
{
    // Every load's source (SN_TUNE, SN_SAMPLE, SN_EFFECTS, SN_MUSIC, SN_BUFFER, SN_FIRMWARE's pieces) is checked as
    // function 51's DMA segment: past $FFFFF, or into OODOS's own blocks (banks 6 and 7), refused, nothing sent to the
    // card. Must fail with a block past memory accepted (the 64 KB steps' check left out).
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_TUNE | SC_STREAM | SC_MUSIC}), std::string("00 00"));
    CHECK_EQ(f82(r, stream_block(0, 48000, FMT_16BIT)), std::string("00 00"));
    struct Load
    {
        uint8_t op;
        std::vector<uint8_t> pre, post;
    };
    const std::vector<Load> loads = {{SN_TUNE, {}, {0}},
                                     {SN_SAMPLE, {1}, {0, 0x40, 0x1F, 0, 0, 0, 0, 0, 0}},
                                     {SN_EFFECTS, {1}, {}},
                                     {SN_MUSIC, {1}, {2}}};
    for (const Load &l : loads)
    {
        CHECK_EQ(f82(r, load_block(l.op, l.pre, 0x0F8000, 0x10000, l.post)), std::string("FF FF")); // past $FFFFF
        CHECK_EQ(f82(r, load_block(l.op, l.pre, 0x040000, 0x30000, l.post)), std::string("FF FF")); // into bank 6
        CHECK_EQ(f82(r, load_block(l.op, l.pre, 0x0E0000, 0x20001, l.post)), std::string("FF FF")); // past, by whole 64 KBs
    }
    CHECK_EQ(f82(r, buffer_block(0x06FFF0, 32)), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_RELEASE, 7}), std::string("00 00"));
    CHECK_EQ(f82(r, std::vector<uint8_t>{SN_FIRMWARE} + le24(0x0FFFF0) + le24(0x20) + std::vector<uint8_t>{0}), std::string("FF FF"));
    CHECK_EQ(dev(r).data_dropped(), 0u);
    CHECK(!(reg(r, SoundCard3::STATUS) & SoundCard3::STATUS_XFER)); // and no transfer was opened
}

// --- the SELECT rule, the pin ----------------------------------------------------------------------------------

namespace
{
    /// The SELECT rule (hw/design.md, "The DMA controller, revision 2"): code that moves CONTROL's SELECT, in an
    /// interrupt too, puts CONTROL back as it found it. This watches the dispatcher: the DMA controller's CONTROL at
    /// each interrupt's entry (IRQ) and at its RTI (IRQEND), whether PIECE or DEAFEN ran in it, and with which SELECT
    /// it was entered.
    struct SelectRule
    {
        unsigned interrupts{0}, changed{0};
        unsigned piece_beside{0};  ///< interrupts that ran PIECE, entered with SELECT not 2 (SDIO's 0, say)
        unsigned deafen_beside{0}; ///< interrupts that ran DEAFEN, entered with SELECT not 0
        uint8_t entry{0};
        bool in{false}, piece{false}, deafen{false};

        static uint8_t control(Rig &r) { return r.machine->dma()->peek(DmaController::CONTROL); }
        static unsigned select(uint8_t c) { return (c & DmaController::SELECT_MASK) >> DmaController::SELECT_SHIFT; }

        void step(Rig &r, uint16_t pc)
        {
            static const uint16_t irq = sym("IRQ"), end = sym("IRQEND"), piece_at = sym("PIECE"),
                                  deafen_at = sym("DEAFEN");
            if (pc == irq)
            {
                entry = control(r);
                in = true;
                piece = deafen = false;
            }
            else if (in && pc == piece_at && r.nb(2) == 61)
                piece = true;
            else if (in && pc == deafen_at && r.nb(3) == 62)
                deafen = true;
            else if (in && pc == end)
            {
                ++interrupts;
                if (control(r) != entry && changed++ < 4)
                    std::printf("  an interrupt entered with CONTROL $%02X returned with $%02X%s%s\n", entry,
                                control(r), piece ? " (PIECE ran)" : "", deafen ? " (DEAFEN ran)" : "");
                piece_beside += piece && select(entry) != 2;
                deafen_beside += deafen && select(entry) != 0;
                in = false;
            }
        }
    };
} // namespace

TEST(the_select_rule_holds_over_a_stream_from_b_and_a_stray_in_a_load)
{
    // Must fail if PIECE (the stream's next piece, in the terminal count's interrupt) or DEAFEN (a stray's) leaves
    // SELECT moved: the window would be another channel's under SDIO or DMALOAD, and sectors or a load written
    // through the wrong channel.
    {
        CcpRig r;
        REQUIRE(r.ok);
        const std::string name83 = "SELECT  PCM";
        const std::vector<uint8_t> pcm = noise(1792 * 128, 46);
        fw(r).set_stream_tap(true);
        put_b(r, name83, pcm);
        r.poke(PARMS, std::vector<uint8_t>{0, 0x44, 0xAC, FMT_16BIT | FMT_STEREO}); // 44,100 Hz
        std::vector<uint8_t> fcb(36, 0);
        fcb[0] = 2; // B:
        for (unsigned i = 0; i < 11; ++i)
            fcb[1 + i] = static_cast<uint8_t>(name83[i]);
        r.poke(FCB, fcb);
        const uint8_t before = SelectRule::control(r);
        const size_t from = start(r, "l3strm.com");
        SelectRule rule;
        r.watch = [&](uint16_t pc) { rule.step(r, pc); };
        const uint64_t endc = r.cpu->cycles() + ms(4000);
        while (r.out.find("END ", from) == std::string::npos && r.cpu->cycles() < endc)
            r.run(ms(1));
        r.watch = nullptr;
        const uint8_t after = SelectRule::control(r);
        REQUIRE(r.until("END ", ms(10), from) && r.until("\r\nA>", ms(1000), r.out.find("END ", from)));
        std::printf("  the stream: %u interrupts, %u with PIECE run beside another SELECT, %u changed CONTROL\n",
                    rule.interrupts, rule.piece_beside, rule.changed);
        CHECK(rule.piece_beside > 0); // the premise: a piece started over SDIO's SELECT 0
        CHECK_EQ(rule.changed, 0u);
        CHECK_EQ(after, before);
        CHECK_EQ(field(r.out.substr(from), "END").substr(0, 6), std::string("05 03 "));
        CHECK(tap_bytes(r) == pcm); // every sector read through channel 0, every piece through 2
    }
    {
        // A 64 KB sample load (DMALOAD: each piece's SELECT 2 set and put back with interrupts off) with a stray (the
        // USB card's interrupt, no layout-2 area) in its middle: DEAFEN's SELECT loop runs inside it.
        CcpRig r;
        REQUIRE(r.ok);
        open(r, "X");
        const std::vector<uint8_t> w = noise(0x10000, 47);
        r.poke(0x10000, w);
        const uint16_t from = sym("DMALOAD"), to = sym("F_SOUND"), deafen = sym("DEAFEN");
        unsigned in = 0, deafened = 0;
        bool stray = false;
        SelectRule rule;
        r.watch = [&](uint16_t pc) {
            rule.step(r, pc);
            if (stray && pc == deafen && r.nb(3) == 62)
                ++deafened;
            if (r.nb(2) != 61 || pc < from || pc >= to)
                return;
            if (!stray && ++in > 200)
            {
                r.machine->bus().write(0xC5B0, 0x0F); // USB_IEN, against the rules
                r.machine->usb_card()->key(0x04, true);
                stray = true;
            }
        };
        CHECK_EQ(f82(r, load_block(SN_SAMPLE, {3}, 0x10000, 0x10000, {0, 0x40, 0x1F, 0, 0, 0, 0, 0, 0})), std::string("00 00"));
        r.watch = nullptr;
        r.machine->usb_card()->key(0x04, false);
        std::printf("  the load: %u interrupts, DEAFEN run %u times inside it, %u changed CONTROL\n", rule.interrupts,
                    deafened, rule.changed);
        CHECK(stray);
        CHECK(deafened > 0); // the premise: a stray's DEAFEN ran inside the load (between its pieces: each piece's
                             // own SELECT 2 is set and put back with interrupts off)
        CHECK_EQ(rule.changed, 0u);
        CHECK_EQ(card_crc(r, SoundFw::SAMPLE_LOAD, 3), crc32(w)); // every piece through channel 2
    }
}

int main() { return run_all(); }

TEST(the_interface_is_17)
{
    // OOLIB's interface pin: function 80's FT_INFO (L3TEST's F) answers interface 17 and its areas, $2F: frame and
    // timing, input, sound, DMA and the serial line.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(command(r, 'F', "F"), std::string("11 2F"));
}
