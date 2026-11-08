/**
 * @file test_oodos_sound.cpp
 * @brief OOLIB's L3 (hw/oolib.md, "The plan"): function 82, the sound card's claims, its RAM and wave RAM
 * loaded by DMA channel 2, the 6510 and the mailbox, the stream fed from two buffers, the ES5503 hook,
 * the beep, the event area's sound part and the warm boot; and sound revision 2's last acceptance item
 * (hw/sound-rev2.md, item 4), a file on an SD drive streamed through function 82.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_input boots it (oodos_rig.h). L3TEST (oodos/sys/test/l3test.s), run by
 * GO, opens its area at $2000 and takes commands, a letter at $2100; function 82's blocks the test builds
 * at $2110, and the data they name it puts in physical memory itself. L3STRM (l3strm.s) streams a file
 * from B: as a program would, refilling its two buffers with ordinary BDOS reads. The MP3 is the repo's
 * own (test/host/mp3, tools/make_mp3_fixtures.sh); the raw PCM is made here.
 */

#include "harness.h"
#include "minimp3.h"
#include "oodos_rig.h"
#include "sound_card.h"

#include <cstdio>
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
    constexpr uint32_t AREA = 0x2000, MBOX = 0x2100, KBLK = 0x2110, DCNT = 0x2180, DLAST = 0x2181; ///< l3test.s's
    constexpr uint32_t PARMS = 0x2100, FCB = 0x2120;                                                  ///< l3strm.s's
    // oodos.inc's
    constexpr unsigned EV_LAYOUT = 0, EV_QHEAD = 16, EV_QTAIL = 17, EV_QSLOTS = 18, EV_QUEUE = 128;
    constexpr unsigned EV_SSTATE = 96, EV_SBUFS = 97, EV_SFREE = 98, EV_SPOS = 100, EV_SKBPS = 104, EV_SUNDER = 106;
    constexpr unsigned EK_SOUND = 6;
    constexpr uint8_t SN_INFO = 0, SN_CLAIM = 1, SN_RELEASE = 2, SN_LOAD = 3, SN_START = 4, SN_STOP = 5, SN_SEND = 6,
                      SN_STREAM = 7, SN_BUFFER = 8, SN_PAUSE = 9, SN_RESUME = 10, SN_SEEK = 11, SN_END = 12,
                      SN_WAVE = 14, SN_BEEP = 16, SN_OPS = 17;
    constexpr uint8_t SC_6510 = 1, SC_STREAM = 2, SC_DOC = 4;

    uint8_t area(Rig &r, unsigned off) { return r.phys(AREA + off); }
    unsigned area16(Rig &r, unsigned off) { return area(r, off) | area(r, off + 1) << 8; }
    uint32_t area32(Rig &r, unsigned off) { return area16(r, off) | static_cast<uint32_t>(area16(r, off + 2)) << 16; }
    uint32_t ms_count(Rig &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("MS");
        return r.phys(a) | r.phys(a + 1) << 8 | r.phys(a + 2) << 16 | static_cast<uint32_t>(r.phys(a + 3)) << 24;
    }
    SoundCard &card(Rig &r) { return *r.machine->sound_card(); }
    uint8_t reg(Rig &r, uint8_t offset) { return card(r).peek(offset); }
    uint8_t sys(Rig &r, const char *name) { return r.phys(SYS_BLOCK_PHYS + sym(name)); }

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
    /// L3TEST's warm boot (command W), to the CCP's prompt.
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
    std::vector<uint8_t> buffer_block(uint32_t phys, unsigned len)
    {
        return {SN_BUFFER, static_cast<uint8_t>(phys), static_cast<uint8_t>(phys >> 8), static_cast<uint8_t>(phys >> 16),
                static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8)};
    }
    std::vector<uint8_t> stream_block(uint8_t codec, unsigned rate, uint8_t fmt)
    {
        return {SN_STREAM, codec, static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), fmt};
    }
    /// The events waiting in the area's queue: kind, value.
    std::vector<std::pair<unsigned, unsigned>> waiting(Rig &r)
    {
        std::vector<std::pair<unsigned, unsigned>> out;
        const unsigned slots = area(r, EV_QSLOTS);
        for (unsigned t = area(r, EV_QTAIL); slots && t != area(r, EV_QHEAD); t = (t + 1) % slots)
            out.push_back({area(r, EV_QUEUE + t * 8), area(r, EV_QUEUE + t * 8 + 2)});
        return out;
    }
    /// The frames the tap recorded, as bytes (16-bit stereo, low byte first).
    std::vector<uint8_t> tap_bytes(SoundCard &c)
    {
        std::vector<uint8_t> out;
        for (const int16_t s : c.take_tap())
        {
            out.push_back(static_cast<uint8_t>(s & 0xFF));
            out.push_back(static_cast<uint8_t>((s >> 8) & 0xFF));
        }
        return out;
    }

    /// The dispatcher's cost for each interrupt over a stretch, from the interrupt's first cycle to its
    /// RTI's last (test_oodos_input's way), by what interrupted: channel 2's terminal count (IRQH bit 0),
    /// a vertical blank (the frame counter moved), a tick alone.
    struct Costs
    {
        std::map<unsigned, unsigned> dma, frame, tick;
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
        const uint16_t irq = sym("IRQ"), end = sym("IRQEND");
        const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
        uint64_t c0 = 0;
        uint8_t l = 0, h = 0, f = 0;
        bool in = false;
        r.watch = [&](uint16_t pc) {
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
                else if (l == 0xBF && h == 0xFF)
                    ++out.tick[c];
                in = false;
            }
        };
        const uint64_t endc = r.cpu->cycles() + budget;
        while (!until() && r.cpu->cycles() < endc)
            r.run(ms(1));
        r.watch = nullptr;
    }

    /// Every part released and quiet, as the warm boot leaves them.
    void quiet(CcpRig &r)
    {
        CHECK_EQ(sys(r, "SCLAIM"), 0);
        CHECK_EQ(sys(r, "C2ON"), 0);
        CHECK_EQ(reg(r, SoundCard::IEN), 0);
        CHECK_EQ(reg(r, SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
        CHECK_EQ(reg(r, SoundCard::PCMCTL), 0);
        CHECK_EQ(reg(r, SoundCard::STATE), 0);
        CHECK_EQ(reg(r, SoundCard::DOCCTL) & 1, 0);
        CHECK_EQ(reg(r, SoundCard::DRQSEL), 0);
        CHECK_EQ(r.machine->bus().peek(0xC426) & 0x80, 0); // DMA2_MODE's interrupt
        CHECK_EQ(r.machine->bus().peek(0xC427) & 0x01, 0); // DMA2_CTRL's ENABLE
    }
    /// The 6510, the stream and the ES5503 claimed and running.
    void claim_it_all(CcpRig &r)
    {
        open(r, "2");
        CHECK_EQ(f82(r, {SN_CLAIM, SC_6510 | SC_STREAM | SC_DOC}), std::string("00 00"));
        r.poke(0x3000, std::vector<uint8_t>{0x4C, 0x00, 0x10}); // JMP $1000: the 6510 spins
        CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x10, 3, 0}), std::string("00 00"));
        CHECK_EQ(f82(r, {SN_START, 0x00, 0x10, 0}), std::string("00 00"));
        CHECK_EQ(f82(r, stream_block(0, 8000, 0)), std::string("00 00"));
        r.poke(0x10000, noise(0x10000, 3));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("01 00"));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00")); // the first two gone to the FIFO
        CHECK(reg(r, SoundCard::CTRL) & SoundCard::CTRL_RUN);
        CHECK(reg(r, SoundCard::PCMCTL) & SoundCard::PCMCTL_RUN);
        CHECK_EQ(sys(r, "SCLAIM"), 7);
        CHECK_EQ(sys(r, "C2ON"), 1); // the third waits for the FIFO's room
        CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_MAILBOX);
    }

    std::vector<uint8_t> mp3_file(const std::string &name)
    {
        std::ifstream in(std::string(EMU_MP3_DIR) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    /// minimp3 on its own, the whole file: its frames as the card's bus carries them (mono on both sides),
    /// as test_sound_mp3's reference.
    std::vector<int16_t> mp3_reference(const std::vector<uint8_t> &mp3)
    {
        std::vector<int16_t> out;
        auto dec = std::make_unique<mp3dec_t>();
        mp3dec_init(dec.get());
        std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        size_t pos = 0;
        while (pos < mp3.size())
        {
            mp3dec_frame_info_t info{};
            const int n = mp3dec_decode_frame(dec.get(), mp3.data() + pos, static_cast<int>(mp3.size() - pos), pcm.data(), &info);
            if (!info.frame_bytes)
                break;
            pos += static_cast<size_t>(info.frame_bytes);
            for (int i = 0; i < n; ++i)
            {
                const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                out.push_back(l);
                out.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
            }
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
} // namespace

TEST(info_and_the_claims)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(command(r, 'I', "INFO"), std::string("02 83 028307")); // revision 2, PCM, MP3, the ES5503; all free
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("FF FF"));             // held
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510 | SC_STREAM}), std::string("FF FF")); // one of them held: neither
    CHECK_EQ(f82(r, {SN_CLAIM, 0}), std::string("FF FF"));                   // none asked for
    CHECK_EQ(command(r, 'I', "INFO"), std::string("02 83 028306"));
    CHECK_EQ(sys(r, "SCLAIM"), SC_6510);
    CHECK_EQ(f82(r, {SN_RELEASE, SC_6510 | SC_DOC}), std::string("00 00")); // what isn't held: nothing
    CHECK_EQ(command(r, 'I', "INFO"), std::string("02 83 028307"));
    // A part's operations need its claim; an operation that isn't one; a block needed and not given.
    CHECK_EQ(f82op(r, SN_STOP), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_SEND, 1}), std::string("FF FF"));
    CHECK_EQ(f82(r, stream_block(0, 8000, 0)), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_WAVE, 0, 0x30, 0, 0, 0, 0, 1, 0}), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_OPS), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_CLAIM), std::string("FF FF"));
    CHECK_EQ(f82op(r, SN_INFO), std::string("02 83"));
    // A load from OODOS's own memory (banks 6-7: the system's) is refused, as 51's segment is.
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x00, 0x07, 0, 0, 16, 0}), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_LOAD, 0xF0, 0xFF, 0x05, 0, 0, 32, 0}), std::string("FF FF")); // runs into bank 6
    // A byte the 6510 (stopped) hasn't taken: a second is refused, busy, rather than replace it.
    CHECK_EQ(f82(r, {SN_SEND, 0x11}), std::string("00 00"));
    CHECK(reg(r, SoundCard::STATUS) & SoundCard::STATUS_TO_CARD);
    CHECK_EQ(f82(r, {SN_SEND, 0x22}), std::string("FE FF"));
    // The card's RAM's end: to $FFFF and no further (the address would wrap to 0).
    r.poke(0x3000, noise(32, 7));
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0xF0, 0xFF, 0x10, 0}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0xF0, 0xFF, 0x11, 0}), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x01, 0x00, 0, 0}), std::string("FF FF")); // 64 KB from 1
}

TEST(the_card_s_ram_is_loaded_by_dma_and_its_6510_talks_through_the_mailbox)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_MAILBOX); // the 6510 claimed, a layout-2 area open
    // All 64 KB, 16 pieces of 4,096 bytes with the CPU held for each: byte for byte, and no tick lost.
    const std::vector<uint8_t> all = noise(0x10000, 1);
    r.poke(0x10000, all);
    const uint64_t c0 = r.cpu->cycles();
    const uint32_t m0 = ms_count(r);
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x00, 0x01, 0x00, 0x00, 0, 0}), std::string("00 00"));
    const double want = static_cast<double>(r.cpu->cycles() - c0) / 7159.0;
    const double got = ms_count(r) - m0;
    std::printf("  64 KB loaded: %.0f ms counted, %.1f in the cycles\n", got, want);
    CHECK(got >= want - 1.5 && got <= want + 1.5);
    CHECK(std::equal(all.begin(), all.end(), card(r).ram().begin()));
    // A program at $1000, a piece and a bit (crossing pieces): it says $42, then echoes each byte inverted.
    std::vector<uint8_t> code = {0xA9, 0x42, 0x8D, 0x00, 0xDE, 0xAD, 0x01, 0xDE, 0x29, 0x01, 0xF0, 0xF9,
                                 0xAD, 0x00, 0xDE, 0x49, 0xFF, 0x8D, 0x00, 0xDE, 0x4C, 0x05, 0x10};
    std::vector<uint8_t> image = noise(5000, 2);
    std::copy(code.begin(), code.end(), image.begin());
    r.poke(0x3000, image);
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x10, 0x88, 0x13}), std::string("00 00")); // 5,000 bytes
    CHECK(std::equal(image.begin(), image.end(), card(r).ram().begin() + 0x1000));
    CHECK_EQ(reg(r, SoundCard::DRQSEL), 0);
    CHECK_EQ(f82(r, {SN_START, 0x00, 0x10, SoundCard::CTRL_NTSC}), std::string("00 00"));
    CHECK_EQ(card(r).ram()[0xFFFC], 0x00);
    CHECK_EQ(card(r).ram()[0xFFFD], 0x10);
    CHECK_EQ(reg(r, SoundCard::CTRL) & 0x07, SoundCard::CTRL_RUN | SoundCard::CTRL_NTSC);
    r.run(ms(5));
    CHECK_EQ(f82(r, {SN_SEND, 0x10}), std::string("00 00"));
    r.run(ms(5));
    std::vector<std::pair<unsigned, unsigned>> ev = waiting(r);
    REQUIRE(ev.size() == 2);
    CHECK_EQ(ev[0].first, EK_SOUND);
    CHECK_EQ(ev[0].second, 0x42u);
    CHECK_EQ(ev[1].first, EK_SOUND);
    CHECK_EQ(ev[1].second, 0xEFu);
    // Stopped: RUN off, the SIDs silenced.
    card(r).write(SoundCard::SID_A + 0x18, 0x0F); // dropped while the 6510 runs: so written after the stop
    CHECK_EQ(f82op(r, SN_STOP), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
    card(r).write(SoundCard::SID_A + 0x18, 0x0F);
    CHECK_EQ(f82op(r, SN_STOP), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::SID_A + 0x18), 0);
    // Without a layout-2 area the mailbox's interrupt is off: the program reads MBIN itself.
    wboot(r);
    open(r, "1");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510}), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), 0);
}

TEST(a_load_is_refused_busy_while_a_stream_buffer_moves)
{
    CcpRig r;
    REQUIRE(r.ok);
    claim_it_all(r); // a third 64 KB buffer waiting for the FIFO's room
    CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("01 00"));      // a fourth, queued
    CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("FE FF"));      // a fifth: both taken
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x20, 16, 0}), std::string("FE FF"));
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x30, 0x00, 0, 0, 0, 16, 0}), std::string("FE FF"));
    CHECK_EQ(area(r, EV_SBUFS), 0);
    // A seek drops both: the channel stopped, the buffers free, a load then goes.
    CHECK_EQ(f82op(r, SN_SEEK), std::string("00 00"));
    CHECK_EQ(sys(r, "C2ON"), 0);
    CHECK_EQ(area(r, EV_SBUFS), 3);
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x20, 16, 0}), std::string("00 00"));
}

TEST(a_stream_s_buffers_chain_across_terminal_counts_with_no_gap)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
    CHECK_EQ(area(r, EV_SBUFS), 3);
    CHECK_EQ(f82(r, stream_block(0, 48000, SoundCard::PCMFMT_16BIT | SoundCard::PCMFMT_STEREO)), std::string("00 00"));
    card(r).set_tap(true, false);
    // Six buffers of 10,000 bytes (pieces of 4,096, 4,096 and 1,808), each given again as OODOS frees it.
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
    time_interrupts(r, costs, [&] { return area(r, EV_SSTATE) == SoundCard::STATE_ENDED; }, ms(1000));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_ENDED);
    const std::vector<uint8_t> played = tap_bytes(card(r));
    std::printf("  %zu bytes sent, %zu played\n", sent.size(), played.size());
    CHECK(played == sent);
    CHECK_EQ(card(r).underruns(), 0u);
    CHECK_EQ(area16(r, EV_SUNDER), 0u);
    CHECK_EQ(area32(r, EV_SPOS), LEN * BUFS / 4);
    CHECK_EQ(area16(r, EV_SFREE), 512u);
    CHECK_EQ(area16(r, EV_SKBPS), 0u);
}

TEST(pause_resume_seek_end_and_status)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
    CHECK_EQ(f82(r, stream_block(0, 22050, SoundCard::PCMFMT_16BIT)), std::string("00 00"));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_BUFFERING);
    r.poke(0x10000, noise(0x10000, 9));
    CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
    r.run(ms(100));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_PLAYING);
    CHECK_EQ(area(r, EV_SBUFS), 3);
    CHECK(area32(r, EV_SPOS) > 1000);
    CHECK_EQ(f82op(r, SN_PAUSE), std::string("00 00"));
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_PAUSED);
    const uint32_t pos = area32(r, EV_SPOS);
    r.run(ms(100));
    CHECK_EQ(area32(r, EV_SPOS), pos);
    CHECK_EQ(f82op(r, SN_RESUME), std::string("00 00"));
    r.run(ms(40));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_PLAYING);
    // A seek: buffering again, the FIFO empty; POS runs on.
    CHECK_EQ(f82op(r, SN_SEEK), std::string("00 00"));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_BUFFERING);
    CHECK_EQ(area16(r, EV_SFREE), 512u);
    // Run dry with no EOF: an underrun, counted once, in the area and in the status block.
    CHECK_EQ(f82(r, buffer_block(0x10000, 8192)), std::string("00 00"));
    r.run(ms(400));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_UNDERRUN);
    CHECK_EQ(area16(r, EV_SUNDER), 1u);
    const std::string st = command(r, 'S', "ST");
    CHECK_EQ(st.substr(0, 6), std::string("04 03 ")); // underrun, both free
    CHECK_EQ(st.substr(6, 2), std::string("04"));
    CHECK_EQ(st.substr(26, 4), std::string("0100")); // the underruns: 1
    CHECK_EQ(card(r).underruns(), 1u);
    // EOF: what is in plays out, then ended.
    CHECK_EQ(f82(r, buffer_block(0x10000, 4096)), std::string("01 00")); // in turn: the seek's first was 0
    CHECK_EQ(f82op(r, SN_END), std::string("00 00"));
    r.run(ms(200));
    CHECK_EQ(area(r, EV_SSTATE), SoundCard::STATE_ENDED);
    CHECK_EQ(area16(r, EV_SUNDER), 1u);
}

TEST(wave_ram_is_loaded_by_dma_and_the_es5503_hook_is_called)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_DOC}), std::string("00 00"));
    // 6,000 bytes at $0F000: across the first 64 KB into the second (DOCBANK's carry), and two pieces.
    const std::vector<uint8_t> w = noise(6000, 4);
    r.poke(0x10000, w);
    card(r).write(SoundCard::DOCCTL, SoundCard::DOCCTL_AUTOINC); // the program's, kept
    card(r).write(SoundCard::DOCADDR, 0xA0);
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x00, 0x01, 0x00, 0xF0, 0x00, 0x70, 0x17}), std::string("00 00"));
    CHECK(std::equal(w.begin(), w.end(), card(r).doc().ram().begin() + 0xF000));
    CHECK_EQ(reg(r, SoundCard::DOCCTL), SoundCard::DOCCTL_AUTOINC);
    CHECK_EQ(reg(r, SoundCard::DRQSEL), 0);
    CHECK_EQ(reg(r, SoundCard::IEN), 0);
    // Wave RAM's end: to $1FFFF and no further.
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x00, 0x01, 0xF0, 0xFF, 0x01, 0x10, 0x00}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x00, 0x01, 0xF0, 0xFF, 0x01, 0x11, 0x00}), std::string("FF FF"));
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x10, 0x00}), std::string("FF FF")); // $20000: none
    CHECK(std::equal(w.begin(), w.begin() + 16, card(r).doc().ram().begin() + 0x1FFF0));
    // The hook: IEN bit 3 on. Oscillator 0 halted from a control write in one-shot with IE: its interrupt.
    CHECK_EQ(command(r, 'H', "HOOK"), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_DOC);
    card(r).write(SoundCard::DOCCTL, 0);
    card(r).write(SoundCard::DOCADDR, 0xA0);
    card(r).write(SoundCard::DOCDATA, 0x0A); // running, one-shot, IE
    card(r).write(SoundCard::DOCDATA, 0x0B); // halted: pending
    card(r).write(SoundCard::DOCADDR, 0x42); // where the program was: oscillator 2's volume
    card(r).write(SoundCard::DOCCTL, SoundCard::DOCCTL_AUTOINC);
    r.run(ms(5));
    CHECK_EQ(r.phys(DCNT), 1);
    CHECK_EQ(r.phys(DLAST), 0x41); // oscillator 0 (bits 1-5), bits 0 and 6 reading 1
    CHECK_EQ(reg(r, SoundCard::DOCCTL), SoundCard::DOCCTL_AUTOINC); // put back as the program had them
    CHECK_EQ(reg(r, SoundCard::DOCADDR), 0x42);
    CHECK_EQ(card(r).peek(SoundCard::STATUS) & SoundCard::STATUS_IRQ, 0);
    // The hook off: IEN bit 3 off.
    CHECK_EQ(f82op(r, 15), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), 0);
}

TEST(the_6510_s_es5503_interrupt_is_left_to_it_when_a_mailbox_byte_comes)
{
    // The hook set, but the ES5503's interrupt routed to the card's 6510 (DOCCTL's IRQ_CARD): an oscillator
    // pending there is the 6510's to take. A mailbox byte's interrupt mustn't read $E0 and take it instead.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_6510 | SC_DOC}), std::string("00 00"));
    CHECK_EQ(command(r, 'H', "HOOK"), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_MAILBOX | SoundCard::IEN_DOC);
    card(r).write(SoundCard::DOCCTL, SoundCard::DOCCTL_IRQ_CARD); // RUN clear: the 6510 isn't interrupted yet
    card(r).doc().write(0xA0, 0x0A);
    card(r).doc().write(0xA0, 0x0B); // oscillator 0 pending, on the 6510's route
    CHECK(card(r).doc().irq());
    r.poke(0x3000, std::vector<uint8_t>{0xA9, 0x42, 0x8D, 0x00, 0xDE, 0x4C, 0x05, 0x10}); // say $42, spin
    CHECK_EQ(f82(r, {SN_LOAD, 0x00, 0x30, 0x00, 0x00, 0x10, 8, 0}), std::string("00 00"));
    CHECK_EQ(f82(r, {SN_START, 0x00, 0x10, 0}), std::string("00 00"));
    r.run(ms(5));
    const std::vector<std::pair<unsigned, unsigned>> ev = waiting(r);
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].second, 0x42u); // the mailbox's interrupt came, and was answered
    CHECK(card(r).doc().irq());   // the oscillator still pending, for the 6510
    CHECK_EQ(r.phys(DCNT), 0);
}

TEST(a_stray_during_a_wave_load_leaves_the_es5503_s_interrupt_off)
{
    // The hook set (IEN bit 3), then a wave load of 64 KB (16 pieces) with a stray coming in its middle (the
    // USB card's interrupt, no layout-2 area): DEAFEN turns the stray off and writes the card's IEN back as
    // OODOS has it, which must not have bit 3 while the load runs.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_DOC}), std::string("00 00"));
    CHECK_EQ(command(r, 'H', "HOOK"), std::string("00 00"));
    CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_DOC);
    const uint16_t from = sym("DMALOAD"), to = sym("BEEPQ"), deafen = sym("DEAFEN");
    unsigned in = 0, on = 0, deaf = 0;
    bool stray = false;
    r.watch = [&](uint16_t pc) {
        if (r.nb(1) != 60)
            return;
        if (pc == deafen && r.nb(3) == 62)
            ++deaf;
        if (pc >= from && pc < to)
        {
            ++in;
            if (!stray && in > 200)
            {
                r.machine->bus().write(0xC5B0, 0x0F); // USB_IEN, against the rules
                r.machine->usb_card()->key(0x04, true);
                stray = true;
            }
            if (reg(r, SoundCard::IEN) & SoundCard::IEN_DOC)
                ++on;
        }
    };
    CHECK_EQ(f82(r, {SN_WAVE, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0}), std::string("00 00"));
    r.watch = nullptr;
    r.machine->usb_card()->key(0x04, false);
    std::printf("  %u steps in the load, the stray's DEAFEN %u, IEN bit 3 on in %u\n", in, deaf, on);
    CHECK(stray && deaf >= 1);
    CHECK_EQ(on, 0u);
    CHECK_EQ(reg(r, SoundCard::IEN), SoundCard::IEN_DOC); // the hook's, back after the load
}

TEST(the_beep_lasts_its_length_at_its_pitch)
{
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "X");
    UsbCard *usb = r.machine->usb_card();
    const uint64_t t0 = usb->speaker_toggles(), c0 = r.cpu->cycles();
    CHECK_EQ(f82(r, {SN_BEEP, 0xE8, 0x03, 100, 0}), std::string("00 00")); // 1,000 Hz, 100 ms
    const uint64_t toggles = usb->speaker_toggles() - t0;
    const double secs = static_cast<double>(r.cpu->cycles() - c0) / 7159090.0;
    std::printf("  1,000 Hz for 100 ms: %llu toggles, the call %.1f ms\n", static_cast<unsigned long long>(toggles), secs * 1000);
    CHECK(toggles >= 190 && toggles <= 202); // 200 at 1,000 Hz; the ticks lower it about 2%
    CHECK(secs >= 0.100 && secs <= 0.115); // the command's own line included
    CHECK_EQ(f82(r, {SN_BEEP, 19, 0, 10, 0}), std::string("FF FF"));          // below 20 Hz
    CHECK_EQ(f82(r, {SN_BEEP, 0x21, 0x4E, 10, 0}), std::string("FF FF"));     // above 20,000
}

TEST(the_warm_boot_quiets_the_card_and_releases_the_claims_by_wboot_ctrl_c_and_break)
{
    {
        CcpRig r;
        REQUIRE(r.ok);
        quiet(r); // cold boot's
        claim_it_all(r);
        const size_t from = r.out.size();
        r.poke(MBOX, 'W');
        CHECK(r.until("\r\nA>", ms(2000), from));
        quiet(r);
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
    }
    {
        // Break: the ROM's QUIET (every DMA channel aborted, the card quiet); OODOS booted again.
        CcpRig r;
        REQUIRE(r.ok);
        claim_it_all(r);
        const size_t from = r.out.size();
        r.cpu->nmi();
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(reg(r, SoundCard::IEN), 0);
        CHECK_EQ(reg(r, SoundCard::CTRL) & SoundCard::CTRL_RUN, 0);
        REQUIRE(r.boot());
        quiet(r);
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
    CHECK_EQ(Costs::mode(costs.tick), 234u);
    for (unsigned i = 96; i < 128; ++i)
        CHECK_EQ(area(r, i), 0); // the sound part: 0, as L2 reserved it
    CHECK_EQ(reg(r, SoundCard::IEN), 0);
    wboot(r);
    r.poke(AREA + 16, std::vector<uint8_t>(240, 0)); // what layout 2 left, cleared: layout 1 writes 16 bytes
    open(r, "1");
    r.run(ms(50));
    CHECK_EQ(area(r, EV_LAYOUT), 1);
    for (unsigned i = 16; i < 128; ++i)
        CHECK_EQ(area(r, i), 0);
}

namespace
{
    /// A stream moving with no event area: 48 kHz 16-bit stereo, two 64 KB buffers into the FIFO and a
    /// third given, waiting for its room (its pieces chained by terminal counts as the card plays).
    void stream_waiting(CcpRig &r)
    {
        open(r, "X");
        CHECK_EQ(f82(r, {SN_CLAIM, SC_STREAM}), std::string("00 00"));
        CHECK_EQ(f82(r, stream_block(0, 48000, SoundCard::PCMFMT_16BIT | SoundCard::PCMFMT_STEREO)), std::string("00 00"));
        r.poke(0x10000, noise(0x10000, 5));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("01 00"));
        CHECK_EQ(f82(r, buffer_block(0x10000, 0)), std::string("00 00"));
        CHECK_EQ(sys(r, "C2ON"), 1);
    }
} // namespace

TEST(chained_terminal_counts_in_a_program_are_not_strays)
{
    // The handler says which interrupts it answered. A terminal count still pulling when it returns (the
    // next piece run at once: the FIFO emptied under the program, so each piece goes whole) must be left to
    // the next interrupt, not taken for a stray: if what it answered were lost, DEAFEN would run for each.
    CcpRig r;
    REQUIRE(r.ok);
    stream_waiting(r);
    r.run(ms(30));
    card(r).write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_FLUSH); // the program spins in L3TEST
    const uint16_t deafen = sym("DEAFEN"), cnext = sym("CNEXT");
    unsigned deaf = 0, chained = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == deafen && r.nb(3) == 62)
            ++deaf;
        if (pc == cnext && r.nb(2) == 61)
            ++chained;
    };
    r.run(ms(200));
    r.watch = nullptr;
    std::printf("  200 ms: %u pieces chained, DEAFEN run %u times\n", chained, deaf);
    CHECK(chained >= 5);
    CHECK_EQ(deaf, 0u);
}

TEST(a_stray_in_a_bdos_call_is_turned_off_while_a_stream_moves)
{
    // The USB card's interrupt left on with no layout-2 area, while a stream moves and the program waits in
    // function 10: the stray must be turned off (DEAFEN), not taken as answered, and the call go on.
    CcpRig r;
    REQUIRE(r.ok);
    stream_waiting(r);
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

TEST(a_hook_s_register_write_leaves_the_interrupted_docdata_run_whole)
{
    // L3TEST writes 0-255 to wave RAM through DOCDATA; at its 128th byte an oscillator's interrupt comes, and
    // the hook writes the chip's register $40. The run's bytes must land in order, and the hook's in $40.
    CcpRig r;
    REQUIRE(r.ok);
    open(r, "2");
    CHECK_EQ(f82(r, {SN_CLAIM, SC_DOC}), std::string("00 00"));
    CHECK_EQ(command(r, 'H', "HOOK"), std::string("00 00"));
    card(r).write(SoundCard::DOCCTL, 0);
    card(r).doc().write(0xA0, 0x0A); // oscillator 0 running, one-shot, IE
    for (unsigned i = 0; i < 256; ++i)
        card(r).doc().ram()[i] = 0xEE;
    const uint16_t run = labels(SYS + "/l3test.lbl").at("DOCRUN");
    bool fired = false;
    r.watch = [&](uint16_t pc) {
        if (!fired && pc == run && r.cpu->regs().x == 128)
        {
            card(r).doc().write(0xA0, 0x0B); // halted: its interrupt, now
            fired = true;
        }
    };
    CHECK_EQ(command(r, 'R', "RUN"), std::string("00 00"));
    r.watch = nullptr;
    CHECK(fired);
    CHECK_EQ(r.phys(DCNT), 1);
    bool whole = true;
    for (unsigned i = 0; i < 256; ++i)
        whole = whole && card(r).doc().ram()[i] == i;
    CHECK(whole);
    if (!whole)
        for (unsigned i = 0; i < 256; ++i)
            if (card(r).doc().ram()[i] != i)
            {
                std::printf("  wave RAM $%02X: $%02X\n", i, card(r).doc().ram()[i]);
                break;
            }
    CHECK_EQ(card(r).doc().read(0x40), 0x77);
}

TEST(acceptance_44_1_khz_16_bit_stereo_raw_pcm_from_b_with_no_underrun)
{
    // 1.3 s of 44.1 kHz 16-bit stereo, 1,792 records (229,376 bytes, whole records: a ^Z-padded last one
    // would play its padding): past the FIFO's 128 KB, so the stream lives on the program's refills after
    // the first 0.74 s.
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> pcm = noise(1792 * 128, 44);
    card(r).set_tap(true, false);
    const uint64_t c0 = r.cpu->cycles();
    Costs costs;
    const std::string o = stream_file(r, "TONE    PCM", pcm, 0, 44100, SoundCard::PCMFMT_16BIT | SoundCard::PCMFMT_STEREO, ms(4000), costs);
    std::printf("  %s", vis(o).c_str());
    std::printf("\n  %.2f s of machine time\n", static_cast<double>(r.cpu->cycles() - c0) / 7159090.0);
    CHECK_EQ(field(o, "CLAIM"), std::string("00 00"));
    CHECK_EQ(field(o, "FILE"), std::string("00 00"));
    CHECK_EQ(field(o, "STREAM"), std::string("00 00"));
    CHECK(o.find("SLOT") == std::string::npos && o.find("BUSY") == std::string::npos);
    CHECK_EQ(field(o, "SENT"), std::string("0013")); // 18 buffers of 96 records, then one of 64
    const std::string end = field(o, "END");
    CHECK_EQ(end.substr(0, 6), std::string("05 03 "));    // ended, both buffers free
    CHECK_EQ(end.substr(end.size() - 4), std::string("0000")); // OODOS saw no underrun
    const std::vector<uint8_t> played = tap_bytes(card(r));
    std::printf("  %zu bytes in the file, %zu at the card's tap; the card's underruns: %u\n", pcm.size(), played.size(),
                card(r).underruns());
    CHECK_EQ(card(r).underruns(), 0u);
    CHECK(played == pcm);
    quiet(r); // L3STRM released it, and the warm boot after it
    Costs::print("channel 2's terminal count", costs.dma);
    Costs::print("a frame, the stream claimed", costs.frame);
    Costs::print("a tick", costs.tick);
    // A terminal count's handler, the next piece started (a piece that runs at once, the FIFO having room,
    // holds the CPU its 4,096 cycles inside it: those are counted too, and are the DMA's, not the handler's).
    REQUIRE(!costs.dma.empty());
    CHECK(costs.dma.begin()->first < 1000);
    CHECK_EQ(Costs::mode(costs.tick), 234u); // a layout-2 area's tick, L2's: the stream costs it nothing
}

TEST(acceptance_an_mp3_from_b_plays_as_minimp3_decodes_it)
{
    CcpRig r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> mp3 = mp3_file("cbr320_stereo_48k.mp3");
    REQUIRE(!mp3.empty());
    card(r).set_tap(true, false);
    Costs costs;
    const std::string o = stream_file(r, "SONG    MP3", mp3, 1, 0, 0, ms(4000), costs);
    std::printf("  %s\n", vis(o.substr(o.find("SENT"))).c_str());
    const std::string end = field(o, "END");
    CHECK_EQ(end.substr(0, 6), std::string("05 03 "));
    CHECK_EQ(end.substr(end.size() - 4), std::string("0000"));
    const std::vector<int16_t> want = mp3_reference(mp3);
    const std::vector<int16_t> got = card(r).take_tap();
    std::printf("  %zu bytes of MP3, %zu frames decoded by minimp3, %zu at the card's tap; underruns %u\n", mp3.size(),
                want.size() / 2, got.size() / 2, card(r).underruns());
    CHECK_EQ(card(r).underruns(), 0u);
    CHECK(got == want);
}

int main() { return run_all(); }
