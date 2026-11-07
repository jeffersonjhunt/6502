/**
 * @file test_sound_doc.cpp
 * @brief The ES5503 on sound card revision 2 (hw/sound-rev2.md, step 7).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The host port (DOCCTL, DOCDATA, DOCADDR, DOCBANK: registers and wave RAM
 * whatever RUN is, no dummy read, auto-increment carrying into the bank and
 * wrapping at 128 KB) and DMA to wave RAM through DRQSEL 2; the 6510's
 * $DF00-$DFFF window while RUN is set, $FF as before while it isn't; both
 * interrupt routes; mutual exclusion with PCM ($82); /RESET against CTRL
 * RESET; a hand-assembled 6510 player looping a waveform; and the bus: each
 * frame the average of the chip's steps it spans or the last held, at
 * (byte - 128) / 128 x volume / 255 x 1/8, in integers, at 48 kHz, through
 * the EQ, the vis tap and volume, the same whatever the audio rate or the
 * ticks, to a golden hash WebAssembly's build must make too.
 */

#include "harness.h"

#include "dma_controller.h"
#include "es5503.h"
#include "machine.h"
#include "sound_card.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    using R = Es5503::Reg;
    constexpr uint64_t SECOND = SoundCard::NATIVE_HZ; ///< the host's cycles in a second (its clock unset)

    /// A DOC register through the port (DOCCTL's RAM bit cleared for it, then put back).
    void reg(SoundCard &c, uint8_t r, uint8_t v)
    {
        const uint8_t ctl = c.read(SoundCard::DOCCTL);
        c.write(SoundCard::DOCCTL, static_cast<uint8_t>(ctl & ~(SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC)));
        c.write(SoundCard::DOCADDR, r);
        c.write(SoundCard::DOCDATA, v);
        c.write(SoundCard::DOCCTL, ctl);
    }

    uint8_t reg(SoundCard &c, uint8_t r)
    {
        const uint8_t ctl = c.read(SoundCard::DOCCTL);
        c.write(SoundCard::DOCCTL, static_cast<uint8_t>(ctl & ~(SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC)));
        c.write(SoundCard::DOCADDR, r);
        const uint8_t v = c.read(SoundCard::DOCDATA);
        c.write(SoundCard::DOCCTL, ctl);
        return v;
    }

    /// `bytes` into wave RAM at `addr` (17 bits), by DOCDATA with auto-increment.
    void wave(SoundCard &c, uint32_t addr, const std::vector<uint8_t> &bytes)
    {
        const uint8_t ctl = c.read(SoundCard::DOCCTL);
        c.write(SoundCard::DOCCTL, static_cast<uint8_t>((ctl & SoundCard::DOCCTL_RUN) | SoundCard::DOCCTL_RAM |
                                                        SoundCard::DOCCTL_AUTOINC));
        c.write(SoundCard::DOCADDR, static_cast<uint8_t>(addr));
        c.write(SoundCard::DOCADDR + 1, static_cast<uint8_t>(addr >> 8));
        c.write(SoundCard::DOCBANK, static_cast<uint8_t>(addr >> 16));
        for (const uint8_t b : bytes)
            c.write(SoundCard::DOCDATA, b);
        c.write(SoundCard::DOCCTL, ctl);
    }

    /// Oscillator `n` through the port: frequency, volume, pointer, table, then control.
    void osc(SoundCard &c, unsigned n, uint16_t freq, uint8_t vol, uint8_t pointer, uint8_t table, uint8_t control)
    {
        reg(c, static_cast<uint8_t>(R::FREQ_LO + n), static_cast<uint8_t>(freq));
        reg(c, static_cast<uint8_t>(R::FREQ_HI + n), static_cast<uint8_t>(freq >> 8));
        reg(c, static_cast<uint8_t>(R::VOLUME + n), vol);
        reg(c, static_cast<uint8_t>(R::POINTER + n), pointer);
        reg(c, static_cast<uint8_t>(R::TABLE + n), table);
        reg(c, static_cast<uint8_t>(R::CONTROL + n), control);
    }

    uint8_t control(unsigned mode, unsigned channel = 0, bool ie = false, bool halt = false)
    {
        return static_cast<uint8_t>(channel << 4 | (ie ? Es5503::CONTROL_IE : 0) | mode << 1 | (halt ? 1 : 0));
    }

    void ticks(SoundCard &c, uint64_t cycles, uint64_t chunk = 7)
    {
        if (!chunk)
            return c.tick(cycles);
        for (; cycles >= chunk; cycles -= chunk)
            c.tick(chunk);
        c.tick(cycles);
    }

    /// `code` at `addr` in the card's RAM, the reset vector at it, the 6510 started.
    void start6510(SoundCard &c, uint16_t addr, std::initializer_list<uint8_t> code)
    {
        uint16_t a = addr;
        for (const uint8_t b : code)
            c.ram()[a++] = b;
        c.ram()[0xFFFC] = static_cast<uint8_t>(addr);
        c.ram()[0xFFFD] = static_cast<uint8_t>(addr >> 8);
        c.write(SoundCard::CTRL, static_cast<uint8_t>(SoundCard::CTRL_RUN | SoundCard::CTRL_RESET));
    }

    /// A square wave of `n` bytes, $FF then $01, in two halves.
    std::vector<uint8_t> square(size_t n)
    {
        std::vector<uint8_t> w(n);
        for (size_t i = 0; i < n; ++i)
            w[i] = i < n / 2 ? 0xFF : 0x01;
        return w;
    }

    /// One frame's 16 bits from the chip's stereo sum over `steps` steps.
    int16_t level(int64_t sum, int64_t steps)
    {
        const int64_t num = sum * 32'768, den = steps * 128 * 255 * 8;
        const int64_t q = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
        return static_cast<int16_t>(std::clamp<int64_t>(q, -32'768, 32'767));
    }

    uint64_t fnv(uint64_t h, uint8_t b) { return (h ^ b) * 0x100000001B3ull; }
} // namespace

TEST(the_port_reaches_registers_and_wave_ram_whatever_run_says)
{
    // DOCDATA: a register (DOCADDR's low byte) or, with DOCCTL bit 6, wave RAM at DOCBANK:DOCADDR. RUN clear and
    // set alike. No dummy read: the first read is the value.
    for (const bool run : {false, true})
    {
        SoundCard c;
        c.write(SoundCard::DOCCTL, run ? SoundCard::DOCCTL_RUN : 0);
        reg(c, R::VOLUME + 7, 0x5A);
        CHECK_EQ(reg(c, R::VOLUME + 7), 0x5A);
        CHECK_EQ(reg(c, R::ADC), 0x80);
        wave(c, 0x1ABCD, {0x11, 0x22});
        c.write(SoundCard::DOCCTL, static_cast<uint8_t>((run ? SoundCard::DOCCTL_RUN : 0) | SoundCard::DOCCTL_RAM));
        c.write(SoundCard::DOCADDR, 0xCD);
        c.write(SoundCard::DOCADDR + 1, 0xAB);
        c.write(SoundCard::DOCBANK, 1);
        CHECK_EQ(c.read(SoundCard::DOCDATA), 0x11); // at once, and DOCADDR stays (no auto-increment)
        CHECK_EQ(c.read(SoundCard::DOCDATA), 0x11);
        CHECK_EQ(c.read(SoundCard::DOCCTL), static_cast<uint8_t>((run ? 1 : 0) | SoundCard::DOCCTL_RAM));
    }
}

TEST(auto_increment_carries_into_the_bank_and_wraps_at_128_kb)
{
    SoundCard c;
    wave(c, 0x0FFFF, {0xA1, 0xA2});
    CHECK_EQ(c.read(SoundCard::DOCBANK), 1);
    CHECK_EQ(c.read(SoundCard::DOCADDR), 0x01);
    CHECK_EQ(c.read(SoundCard::DOCADDR + 1), 0x00);
    wave(c, 0x1FFFF, {0xB1, 0xB2});
    CHECK_EQ(c.read(SoundCard::DOCBANK), 0);
    CHECK_EQ(c.read(SoundCard::DOCADDR), 0x01);
    // As the chip sees them.
    reg(c, R::POINTER, 0xFF);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC);
    c.write(SoundCard::DOCADDR, 0xFF);
    c.write(SoundCard::DOCADDR + 1, 0xFF);
    c.write(SoundCard::DOCBANK, 0);
    const uint8_t a = c.read(SoundCard::DOCDATA), b = c.read(SoundCard::DOCDATA);
    std::printf("  $0FFFF $%02X, $10000 $%02X\n", a, b);
    CHECK_EQ(a, 0xA1);
    CHECK_EQ(b, 0xA2);
    c.write(SoundCard::DOCADDR, 0xFF);
    c.write(SoundCard::DOCADDR + 1, 0xFF);
    c.write(SoundCard::DOCBANK, 1);
    CHECK_EQ(c.read(SoundCard::DOCDATA), 0xB1);
    CHECK_EQ(c.read(SoundCard::DOCDATA), 0xB2); // $00000
}

TEST(demand_dma_through_drqsel_2_fills_wave_ram)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                            "dma 400-47F space=io name=dma irq\nsound 500-57F space=io name=sound irq drq=2\n",
                            "sound.machine", {}, error);
    REQUIRE(m != nullptr);
    SoundCard *sound = m->sound_card();
    Decoder &bus = m->bus();
    constexpr unsigned N = 3'000;
    for (unsigned i = 0; i < N; ++i)
        bus.write_physical(0x02000 + i, static_cast<uint8_t>(i * 13 + 1));
    bus.write(0xC500 + SoundCard::DRQSEL, SoundCard::DRQ_DOCDATA);
    bus.write(0xC500 + SoundCard::DOCCTL, SoundCard::DOCCTL_RAM | SoundCard::DOCCTL_AUTOINC);
    bus.write(0xC500 + SoundCard::DOCADDR, 0x00);
    bus.write(0xC500 + SoundCard::DOCADDR + 1, 0xF8); // $1F800: across the top, into $00000
    bus.write(0xC500 + SoundCard::DOCBANK, 1);
    bus.poke(0xC420, 0x00);
    bus.poke(0xC421, 0x20);
    bus.poke(0xC422, 0x00);
    bus.poke(0xC424, N & 0xFF);
    bus.poke(0xC425, N >> 8);
    bus.poke(0xC426, DmaController::DEMAND | DmaController::TO_CARD);
    bus.poke(0xC441, DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE);
    bus.poke(0xC427, DmaController::ENABLE);
    for (unsigned i = 0; i < 4 * N; ++i)
    {
        bus.read(0x0000);
        bus.irq();
    }
    bool same = true;
    for (unsigned i = 0; i < N; ++i)
        same = same && sound->doc().ram()[(0x1F800 + i) % Es5503::RAM_BYTES] == static_cast<uint8_t>(i * 13 + 1);
    std::printf("  %u bytes by DMA to $1F800 on: %s\n", N, same ? "all there" : "WRONG");
    CHECK(same);
    CHECK_EQ(sound->doc().ram()[(0x1F800 + N) % Es5503::RAM_BYTES], 0); // and no further
}

TEST(the_6510_sees_the_registers_at_df00_only_while_run_is_set)
{
    // A player's stores and loads at $DF00-$DFFF: with DOCCTL RUN, the chip's registers ($DF47 oscillator 7's
    // volume, $DFE2 the A/D's $80); with RUN clear, $FF and no write, as before the ES5503.
    for (const bool run : {true, false})
    {
        SoundCard c;
        if (run)
            c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
        reg(c, R::VOLUME + 7, 0x11);
        start6510(c, 0x1000,
                  {
                      0xA9, 0x77,       // LDA #$77
                      0x8D, 0x47, 0xDF, // STA $DF47
                      0xAD, 0x47, 0xDF, // LDA $DF47
                      0x85, 0x20,       // STA $20
                      0xAD, 0xE2, 0xDF, // LDA $DFE2
                      0x85, 0x21,       // STA $21
                      0xAD, 0x00, 0xDF, // LDA $DF00
                      0x85, 0x22,       // STA $22
                      0x4C, 0x14, 0x10, // JMP *
                  });
        ticks(c, SECOND / 1'000);
        std::printf("  RUN %s: $DF47 read $%02X, $DFE2 $%02X; volume 7 $%02X\n", run ? "set" : "clear", c.ram()[0x20],
                    c.ram()[0x21], reg(c, R::VOLUME + 7));
        CHECK_EQ(c.ram()[0x20], run ? 0x77 : 0xFF);
        CHECK_EQ(c.ram()[0x21], run ? 0x80 : 0xFF);
        CHECK_EQ(c.ram()[0x22], run ? 0x00 : 0xFF);
        CHECK_EQ(reg(c, R::VOLUME + 7), run ? 0x77 : 0x11);
        CHECK_EQ(c.ram()[0xDF47], 0x00); // I/O banked in: never the RAM under it
    }
}

TEST(the_host_route_interrupts_through_ien_bit_3)
{
    // Oscillator 0, one-shot with IE, ends its table: with IEN bit 3 (and DOCCTL bit 2 clear) the card
    // interrupts the host, STATUS bit 7; reading $E0 through DOCDATA takes it. Without IEN bit 3, or with the
    // interrupt routed to the 6510, the host isn't interrupted.
    for (const int way : {0, 1, 2})
    {
        SoundCard c;
        c.write(SoundCard::IEN, way == 1 ? 0 : SoundCard::IEN_DOC);
        c.write(SoundCard::DOCCTL,
                static_cast<uint8_t>(SoundCard::DOCCTL_RUN | (way == 2 ? SoundCard::DOCCTL_IRQ_CARD : 0)));
        wave(c, 0, std::vector<uint8_t>(256, 0x90));
        osc(c, 0, 0x2000, 100, 0, 0, control(Es5503::MODE_ONE_SHOT, 0, true));
        CHECK(!c.irq());
        ticks(c, SECOND / 100); // 256 x $200 / $2000 = 16 steps: well within
        const bool up = c.irq();
        std::printf("  %s: host IRQ %s, STATUS $%02X\n",
                    way == 0 ? "IEN bit 3" : way == 1 ? "IEN 0" : "routed to the 6510", up ? "up" : "down",
                    c.read(SoundCard::STATUS));
        CHECK_EQ(up, way == 0);
        CHECK_EQ(c.read(SoundCard::STATUS) & SoundCard::STATUS_IRQ, way == 0 ? SoundCard::STATUS_IRQ : 0);
        CHECK_EQ(reg(c, R::INTERRUPT), static_cast<uint8_t>(0x41)); // oscillator 0, taken
        CHECK(!c.irq());
    }
    // On the backplane: IRQH bit 2.
    std::string error;
    auto m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                            "sound 500-57F space=io name=sound irq\n",
                            "sound.machine", {}, error);
    REQUIRE(m != nullptr);
    SoundCard &c = *m->sound_card();
    Decoder &bus = m->bus();
    bus.write(0xC500 + SoundCard::IEN, SoundCard::IEN_DOC);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    wave(c, 0, std::vector<uint8_t>(256, 0x90));
    osc(c, 0, 0x2000, 100, 0, 0, control(Es5503::MODE_ONE_SHOT, 0, true));
    CHECK_EQ(bus.peek(0xC689), 0xFF);
    for (int i = 0; i < 20'000; ++i)
        bus.read(0x0000);
    CHECK(bus.irq());
    CHECK_EQ(bus.peek(0xC689), 0xFB); // IRQH: bit 2 low, module A
}

TEST(the_card_route_interrupts_the_6510_and_dfe0_takes_it)
{
    // DOCCTL bit 2: oscillator 0 free running with IE interrupts at each end of its table (255 steps), its
    // handler reading $DFE0 to take it and counting. The host is never interrupted.
    SoundCard c;
    c.write(SoundCard::IEN, SoundCard::IEN_DOC);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN | SoundCard::DOCCTL_IRQ_CARD);
    wave(c, 0, std::vector<uint8_t>(256, 0x90));
    osc(c, 0, 0x200, 100, 0, 0, control(Es5503::MODE_FREE, 0, true));
    c.ram()[0xFFFE] = 0x00; // IRQ: $1100
    c.ram()[0xFFFF] = 0x11;
    uint16_t a = 0x1100;
    for (const unsigned b : {0xADu, 0xE0u, 0xDFu, // LDA $DFE0
                             0x85u, 0x22u,        // STA $22
                             0xE6u, 0x20u,        // INC $20
                             0xD0u, 0x02u,        // BNE +2
                             0xE6u, 0x21u,        // INC $21
                             0x40u})              // RTI
        c.ram()[a++] = static_cast<uint8_t>(b);
    start6510(c, 0x1000, {0x58, 0x4C, 0x01, 0x10}); // CLI; JMP *
    bool host = false;
    for (int ms = 0; ms < 1'000; ++ms)
    {
        ticks(c, SECOND / 1'000);
        host = host || c.irq();
    }
    // One oscillator: 7,159,090 / 24 = 298,295 steps a second, an end every 255.
    const unsigned irqs = c.ram()[0x20] | static_cast<unsigned>(c.ram()[0x21]) << 8;
    const unsigned want = 298'295 / 255;
    std::printf("  1 s: %u interrupts taken (about %u), the last $DFE0 $%02X, host IRQ %s\n", irqs, want, c.ram()[0x22],
                host ? "seen" : "never");
    CHECK(irqs + 2 >= want && irqs <= want + 2);
    CHECK_EQ(c.ram()[0x22], 0x41);
    CHECK(!host);
}

TEST(pcm_and_the_es5503_exclude_each_other)
{
    // PCM running, DOCCTL RUN written: refused, not stored (DOCCTL reads RUN clear), STATE $82, PCM playing on
    // (POS moves), the chip still. The next write to PCMCTL or DOCCTL clears $82, setting it again only if that
    // write is refused too. Then the other way round. The SIDs play alongside either.
    SoundCard c;
    c.set_audio_rate(48'000);
    c.write(SoundCard::RATE, static_cast<uint8_t>(48'000));
    c.write(SoundCard::RATE + 1, static_cast<uint8_t>(48'000 >> 8));
    c.write(SoundCard::PCMFMT, SoundCard::PCMFMT_16BIT);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN);
    for (int i = 0; i < 40'000; ++i)
        c.write(SoundCard::PCMDATA, static_cast<uint8_t>(i & 1 ? 0x10 : 0));
    wave(c, 0, std::vector<uint8_t>(256, 0xC0));
    osc(c, 0, 0x200, 200, 0, 0, control(Es5503::MODE_FREE));
    ticks(c, SECOND / 100);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_PLAYING);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN | SoundCard::DOCCTL_AUTOINC);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH);
    CHECK_EQ(c.read(SoundCard::DOCCTL), SoundCard::DOCCTL_AUTOINC);
    const uint32_t acc = c.doc().accumulator(0);
    ticks(c, SECOND / 100);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH); // until a write
    CHECK_EQ(c.doc().accumulator(0), acc);                     // the chip still
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN); // refused again: $82 again
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH);
    c.write(SoundCard::DOCCTL, 0); // not refused: cleared
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_PLAYING);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN); // a PCMCTL write clears it too
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_PLAYING);
    // The other way round: PCM stopped, the chip running, PCMCTL RUN refused.
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_FLUSH);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    CHECK_EQ(c.read(SoundCard::DOCCTL), SoundCard::DOCCTL_RUN);
    c.write(SoundCard::PCMCTL, SoundCard::PCMCTL_RUN | SoundCard::PCMCTL_EOF);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH);
    CHECK_EQ(c.read(SoundCard::PCMCTL), SoundCard::PCMCTL_EOF);
    const uint32_t a2 = c.doc().accumulator(0);
    // A SID voice alongside.
    c.write(SoundCard::SID_A + 0x18, 0x0F);
    c.write(SoundCard::SID_A + 0x01, 0x20);
    c.write(SoundCard::SID_A + 0x06, 0xF0);
    c.write(SoundCard::SID_A + 0x04, 0x21);
    c.set_tap(true);
    ticks(c, SECOND / 10);
    CHECK(c.doc().accumulator(0) != a2); // the chip plays on
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_BOTH);
    c.write(SoundCard::PCMCTL, 0);
    CHECK_EQ(c.peek(SoundCard::STATE), SoundCard::STATE_IDLE);
    const std::vector<int16_t> tap = c.take_tap();
    float buf[2 * 8'192];
    double peak = 0;
    for (size_t n; (n = c.take_audio(buf, 8'192)) != 0;)
        for (size_t i = 0; i < 2 * n; ++i)
            peak = std::max(peak, static_cast<double>(std::fabs(buf[i])));
    std::printf("  the chip's frames %zu (want about 4,800), the mix's peak %.3f\n", tap.size() / 2, peak);
    CHECK(tap.size() / 2 >= 4'790 && tap.size() / 2 <= 4'810);
    CHECK(peak > 0.05);
}

TEST(reset_clears_the_es5503_and_ctrl_reset_leaves_it)
{
    SoundCard c;
    c.write(SoundCard::IEN, SoundCard::IEN_DOC);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN | SoundCard::DOCCTL_IRQ_CARD);
    wave(c, 0x100, std::vector<uint8_t>(256, 0x90));
    reg(c, R::ENABLE, 10);
    osc(c, 3, 0x200, 100, 1, 0, control(Es5503::MODE_FREE, 0, true));
    ticks(c, SECOND / 100);
    // CTRL RESET (the 6510's): the chip plays on, its registers kept.
    c.write(SoundCard::CTRL, SoundCard::CTRL_RESET);
    CHECK_EQ(c.read(SoundCard::DOCCTL), SoundCard::DOCCTL_RUN | SoundCard::DOCCTL_IRQ_CARD);
    CHECK_EQ(reg(c, R::ENABLE), 10);
    CHECK(!(reg(c, R::CONTROL + 3) & 1));
    // /RESET: DOCCTL 0, every oscillator halted, nothing pending, $E1 0; wave RAM kept.
    c.reset();
    CHECK_EQ(c.read(SoundCard::DOCCTL), 0);
    CHECK_EQ(reg(c, R::ENABLE), 0);
    CHECK(reg(c, R::CONTROL + 3) & 1);
    CHECK(!c.doc().irq());
    CHECK_EQ(reg(c, R::INTERRUPT), 0xFF);
    CHECK_EQ(c.doc().ram()[0x180], 0x90);
    CHECK_EQ(c.read(SoundCard::STATE), SoundCard::STATE_IDLE);
}

TEST(a_6510_player_loops_a_square_wave)
{
    // The host loads a 256-byte square wave into wave RAM and sets DOCCTL RUN; the card's 6510, through $DF00,
    // starts oscillator 0 on it, free running at one byte a step (frequency $200, resolution 0), volume 255, one
    // oscillator enabled: 298,295 steps a second, a loop every 255, about 1,170 Hz. On the bus at 48 kHz: frames
    // at +-(127 x 255 x 32,768 / 261,120) = +-4,064, two edges a loop.
    SoundCard c;
    c.set_tap(true);
    wave(c, 0, square(256));
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    start6510(c, 0x1000,
              {
                  0xA9, 0x00, 0x8D, 0xE1, 0xDF, // LDA #0  STA $DFE1   one oscillator
                  0xA9, 0x00, 0x8D, 0x00, 0xDF, // LDA #0  STA $DF00   frequency $0200
                  0xA9, 0x02, 0x8D, 0x20, 0xDF, // LDA #2  STA $DF20
                  0xA9, 0xFF, 0x8D, 0x40, 0xDF, // LDA #FF STA $DF40   volume
                  0xA9, 0x00, 0x8D, 0x80, 0xDF, // LDA #0  STA $DF80   pointer
                  0xA9, 0x00, 0x8D, 0xC0, 0xDF, // LDA #0  STA $DFC0   256 bytes, resolution 0
                  0xA9, 0x00, 0x8D, 0xA0, 0xDF, // LDA #0  STA $DFA0   free run, channel 0: go
                  0x4C, 0x23, 0x10,             // JMP *
              });
    ticks(c, SECOND);
    const std::vector<int16_t> tap = c.take_tap();
    int16_t hi = 0, lo = 0;
    unsigned edges = 0;
    for (size_t f = 1; f < tap.size() / 2; ++f)
    {
        hi = std::max(hi, tap[2 * f]);
        lo = std::min(lo, tap[2 * f]);
        edges += (tap[2 * f] > 0) != (tap[2 * f - 2] > 0);
        CHECK_EQ(tap[2 * f + 1], 0); // channel 0: the left
    }
    std::printf("  1 s: %zu frames, +%d / %d, %u edges (want about %u)\n", tap.size() / 2, hi, lo, edges,
                2 * 298'295 / 255);
    CHECK(tap.size() / 2 >= 47'990 && tap.size() / 2 <= 48'000);
    CHECK_EQ(hi, 4'064);
    CHECK_EQ(lo, -4'064);
    CHECK(edges + 4 >= 2 * 298'295 / 255 && edges <= 2 * 298'295 / 255 + 4);
}

TEST(each_frame_averages_its_steps_or_holds_the_last)
{
    // The bus frames against the chip alone stepped on the exact times: a step every 8 x (enabled + 2) of
    // 7,159,090 Hz from RUN, frame j ending at j / 48,000 s from RUN, a step at a frame's very end counted in it;
    // the frame the average of its steps' sums, or the last frame when none. With 1 oscillator enabled (6 or 7
    // steps a frame) and with 32 (0 or 1). Ramps on both channels, loud enough to clip a few frames with 32.
    for (const unsigned enabled : {1u, 32u})
    {
        SoundCard c;
        Es5503 ref;
        std::vector<uint8_t> ramp(256);
        for (size_t i = 0; i < 256; ++i)
            ramp[i] = static_cast<uint8_t>(1 + i * 254 / 255);
        wave(c, 0, ramp);
        std::copy(ramp.begin(), ramp.end(), ref.ram().begin());
        const auto both = [&](uint8_t r, uint8_t v) {
            reg(c, r, v);
            ref.write(r, v);
        };
        both(R::ENABLE, static_cast<uint8_t>((enabled - 1) << 1));
        for (unsigned n = 0; n < enabled; ++n)
        {
            both(static_cast<uint8_t>(R::FREQ_LO + n), static_cast<uint8_t>(0x37 * (n + 1)));
            both(static_cast<uint8_t>(R::FREQ_HI + n), static_cast<uint8_t>(1 + n % 3));
            both(static_cast<uint8_t>(R::VOLUME + n), 255);
            both(static_cast<uint8_t>(R::CONTROL + n), control(Es5503::MODE_FREE, n % 4));
        }
        c.set_tap(true);
        c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
        ticks(c, SECOND / 10);
        const std::vector<int16_t> tap = c.take_tap();
        // The reference: times in units of 1 / (7,159,090 x 48,000) s.
        const uint64_t step = uint64_t{8} * (enabled + 2) * 48'000, frame = Es5503::CLOCK_HZ;
        std::vector<int16_t> want;
        int64_t sum[2] = {0, 0}, steps = 0;
        int16_t last[2] = {0, 0};
        uint64_t next_step = step;
        for (uint64_t j = 1; want.size() < tap.size(); ++j)
        {
            while (next_step <= j * frame)
            {
                ref.step();
                const auto s = ref.stereo();
                sum[0] += s[0];
                sum[1] += s[1];
                ++steps;
                next_step += step;
            }
            if (steps)
                for (int ch = 0; ch < 2; ++ch)
                    last[ch] = level(sum[ch], steps);
            want.insert(want.end(), {last[0], last[1]});
            sum[0] = sum[1] = steps = 0;
        }
        std::printf("  %u enabled: %zu frames, %s\n", enabled, tap.size() / 2, tap == want ? "as the reference" : "DIFFERENT");
        CHECK(tap.size() / 2 >= 4'790);
        CHECK(tap == want);
    }
}

TEST(the_bus_runs_at_48_khz_through_the_eq_vis_and_volume)
{
    // While the chip plays the bus is 48 kHz: the 16 kHz band (below 0.45 of 48 kHz) runs, so the EQ changes the
    // frames; VOLL scales them; the vis page shows them. Stopping the chip (RUN cleared) freezes it: its
    // accumulator stays, the next snapshot is silence; RUN again plays on from there.
    SoundCard c;
    c.set_tap(true);
    wave(c, 0, square(256));
    osc(c, 0, 0x2000, 255, 0, 0, control(Es5503::MODE_FREE)); // 16 bytes a step: a fast square
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    ticks(c, SECOND / 10);
    const std::vector<int16_t> flat = c.take_tap();
    CHECK_EQ(c.read(SoundCard::RATE) | c.read(SoundCard::RATE + 1) << 8, 0); // PCM's RATE untouched (it reads RATE)
    c.write(SoundCard::EQIDX, 10);
    c.write(SoundCard::EQVAL, 24); // 16 kHz +12 dB
    c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
    c.write(SoundCard::EQVAL, 1);
    c.write(SoundCard::VOLL, 128);
    ticks(c, SECOND / 10);
    const std::vector<int16_t> shaped = c.take_tap();
    int16_t peak_flat = 0, peak_shaped = 0;
    for (size_t i = 0; i < flat.size(); i += 2)
        peak_flat = std::max<int16_t>(peak_flat, static_cast<int16_t>(std::abs(flat[i])));
    for (size_t i = 0; i < shaped.size(); i += 2)
        peak_shaped = std::max<int16_t>(peak_shaped, static_cast<int16_t>(std::abs(shaped[i])));
    std::printf("  0.1 s flat: %zu frames, peak %d; the 16 kHz band +12 dB and VOLL 128: peak %d\n", flat.size() / 2,
                peak_flat, peak_shaped);
    CHECK(flat.size() / 2 >= 4'790 && flat.size() / 2 <= 4'810);
    CHECK_EQ(peak_flat, 4'064);
    CHECK(peak_shaped != peak_flat && peak_shaped != (4'064 * 128 + 127) / 255);
    // The vis page has it; stopping freezes the chip and the next snapshot is silence.
    c.write(SoundCard::VISIDX, 0);
    c.write(SoundCard::VISIDX, 32);
    CHECK(c.read(SoundCard::VISDATA) > 150);
    c.write(SoundCard::DOCCTL, 0);
    const uint32_t acc = c.doc().accumulator(0);
    ticks(c, SECOND / 10);
    CHECK_EQ(c.doc().accumulator(0), acc);
    CHECK(c.take_tap().empty());
    c.write(SoundCard::VISIDX, 0);
    c.write(SoundCard::VISIDX, 34);
    CHECK_EQ(c.read(SoundCard::VISDATA), 0x80);
    c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
    ticks(c, SECOND / 100);
    CHECK(c.doc().accumulator(0) != acc); // on from where it was
    CHECK(!c.take_tap().empty());
}

TEST(the_bus_is_the_same_whatever_the_audio_rate_or_ticks_to_a_golden_hash)
{
    // Eight oscillators in every mode and both channels, the EQ on, VOLL 200, the 6510 not running: the tap and
    // the vis pages at 0.3 s and 2.8 s, at audio rates 0, 48 and 44.1 kHz, ticked in 7-cycle steps, by
    // milliseconds, or in one tick to each (over two seconds: the mix's bulk). Then the golden hash.
    std::vector<uint8_t> first;
    uint64_t golden = 0;
    bool same = true;
    for (const uint32_t audio : {0u, 48'000u, 44'100u})
        for (const uint64_t chunk : {uint64_t{7}, SECOND / 1'000, uint64_t{0}})
        {
            SoundCard c;
            c.set_audio_rate(audio);
            c.set_tap(true);
            std::vector<uint8_t> w(4'096);
            uint32_t seed = 99;
            for (uint8_t &b : w)
            {
                seed = seed * 1'103'515'245u + 12'345u;
                b = static_cast<uint8_t>(1 + (seed >> 16) % 255);
            }
            w[777] = 0;
            wave(c, 0x10000, w);
            reg(c, R::ENABLE, 14);
            for (unsigned n = 0; n < 8; ++n)
                osc(c, n, static_cast<uint16_t>(0x123 * (n + 1)), static_cast<uint8_t>(60 + 20 * n), 0,
                    static_cast<uint8_t>(Es5503::TABLE_BANK | (n % 5) << 3 | n % 4), control(n % 4, n, true));
            c.write(SoundCard::EQIDX, 0);
            for (int g : {-6, 10, -12, 8, 0, 0, 24, -24, 4, 4, 2})
                c.write(SoundCard::EQVAL, static_cast<uint8_t>(g));
            c.write(SoundCard::EQIDX, SoundCard::EQIDX_ENABLE);
            c.write(SoundCard::EQVAL, 1);
            c.write(SoundCard::VOLL, 200);
            c.write(SoundCard::DOCCTL, SoundCard::DOCCTL_RUN);
            std::vector<uint8_t> out;
            uint64_t t = 0;
            for (const uint64_t mark : {SECOND * 3 / 10, SECOND * 28 / 10})
            {
                ticks(c, mark - t, chunk);
                t = mark;
                for (const int16_t s : c.take_tap())
                    out.insert(out.end(), {static_cast<uint8_t>(s), static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8)});
                c.write(SoundCard::VISIDX, 0);
                for (unsigned i = 0; i < SoundCard::VIS_BYTES; ++i)
                    out.push_back(c.read(SoundCard::VISDATA));
                out.push_back(reg(c, R::INTERRUPT));
                float buf[2 * 4'096];
                while (c.take_audio(buf, 4'096))
                {
                }
            }
            if (first.empty())
            {
                first = out;
                uint64_t h = 0xCBF29CE484222325ull;
                for (const uint8_t b : out)
                    h = fnv(h, b);
                golden = h;
            }
            std::printf("  audio %5u Hz, %s: %s\n", audio,
                        chunk == 7 ? "7-cycle ticks" : chunk ? "1 ms ticks" : "one tick to each mark",
                        out == first ? "the same" : "DIFFERENT");
            same = same && out == first;
        }
    CHECK(same);
    std::printf("  golden: %016llx (%zu bytes)\n", static_cast<unsigned long long>(golden), first.size());
    CHECK_EQ(golden, 0x4154056ED2DCE0B9ull);
}

int main()
{
    return run_all();
}
