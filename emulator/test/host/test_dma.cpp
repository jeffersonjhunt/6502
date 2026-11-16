/**
 * @file test_dma.cpp
 * @brief The DMA controller, revision 2 (hw/design.md, "The DMA controller, revision 2"), and cycle stealing.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The controller takes the bus before a CPU cycle, so these mostly watch
 * the decoder: each CPU access reports the cycles it was held for
 * (wait_states()), which is exactly the DMA's cycles: one to take the bus,
 * one to poll the cards when /DREQ is low and a channel serving a card is
 * armed, then one per fly-by transfer or two per memory-to-memory transfer,
 * a transfer being a byte, or a word with WIDE. Test
 * cards, wired to /DREQ and answering by their module IDs, stand in for the
 * modelled ones. The last cases run real 6502 code on the backplane machine.
 */

#include "harness.h"

#include "bus_cycle.h"
#include "cpu6502.h"
#include "decoder.h"
#include "dma_controller.h"
#include "machine.h"
#include "memory_devices.h"

#include <deque>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t DMA = 0xC400;
    constexpr uint16_t CONTROL = DMA + DmaController::CONTROL;
    constexpr uint16_t STATUS = DMA + DmaController::STATUS;
    constexpr uint16_t ABORT = DMA + DmaController::ABORT;
    constexpr uint16_t REQUESTS = DMA + DmaController::REQ_REG;

    /// A card with a receive FIFO (to memory) and a transmit log (from memory).
    struct Card : Device
    {
        size_t size() const override { return 4; }
        uint8_t read(uint32_t) override { return 0; }
        uint8_t peek(uint32_t) const override { return 0; }
        void write(uint32_t, uint8_t) override {}
        bool drq() const override { return want >= 0 ? want > 0 : !fifo.empty(); }
        uint8_t dack_read(bool tc) override
        {
            tcs.push_back(tc);
            widths.push_back(1);
            return next();
        }
        void dack_write(uint8_t data, bool tc) override
        {
            tcs.push_back(tc);
            widths.push_back(1);
            sent.push_back(data);
        }
        /// A word grant (WIDE): the FIFO's next two bytes, the even first, as a card whose register takes
        /// words; with `words` false, Device's defaults (a card with D0-D7 only).
        uint16_t dack_read_word(bool tc) override
        {
            if (!words)
                return Device::dack_read_word(tc);
            tcs.push_back(tc);
            widths.push_back(2);
            const uint8_t lo = next();
            return static_cast<uint16_t>(next() << 8 | lo);
        }
        void dack_write_word(uint16_t data, bool tc) override
        {
            if (!words)
                return Device::dack_write_word(data, tc);
            widths.push_back(2);
            tcs.push_back(tc);
            sent.push_back(static_cast<uint8_t>(data));
            sent.push_back(static_cast<uint8_t>(data >> 8));
        }
        std::deque<uint8_t> fifo;
        std::vector<uint8_t> sent;
        std::vector<bool> tcs;
        std::vector<int> widths; ///< per grant: 1 a byte, 2 a word
        uint8_t next()
        {
            const uint8_t v = fifo.empty() ? 0xEE : fifo.front();
            if (!fifo.empty())
                fifo.pop_front();
            return v;
        }
        int want{-1}; ///< force the request: 1 on, 0 off, -1 = while the FIFO has data
        bool words{true}; ///< the card's register takes words
    };

    /// 64 KB of RAM, the controller at $C400, and a card on module 7 (channel 0's CARD at reset), wired to
    /// /DREQ.
    struct Rig
    {
        Rig()
        {
            dma.attach(&bus);
            bus.set_master(&dma);
            bus.map({0x00000, 0x0FFFF, &ram});
            bus.map({DMA, DMA + 0x7F, &dma, DmaController::MODULE, true});
            bus.map({0x0D000, 0x0D003, &card, 0x7, false, 0, Dreq::Wired});
        }

        /// CONTROL's SELECT to channel `n`, the rest kept.
        void select(unsigned n)
        {
            const uint8_t c = bus.peek(CONTROL);
            bus.poke(CONTROL,
                     static_cast<uint8_t>((c & ~DmaController::SELECT_MASK) | n << DmaController::SELECT_SHIFT));
        }
        void channel(unsigned n, uint32_t addr, uint32_t count, uint8_t mode, uint32_t dest = 0)
        {
            select(n);
            for (unsigned i = 0; i < 3; ++i)
            {
                bus.poke(static_cast<uint16_t>(DMA + DmaController::ADDR + i), static_cast<uint8_t>(addr >> (8 * i)));
                bus.poke(static_cast<uint16_t>(DMA + DmaController::DEST + i), static_cast<uint8_t>(dest >> (8 * i)));
            }
            bus.poke(DMA + DmaController::COUNT, static_cast<uint8_t>(count));
            bus.poke(DMA + DmaController::COUNT + 1, static_cast<uint8_t>(count >> 8));
            bus.poke(DMA + DmaController::MODE, mode);
        }
        void card_of(unsigned n, uint8_t module)
        {
            select(n);
            bus.poke(DMA + DmaController::CARD, module);
        }
        void arm(unsigned n, uint8_t ctrl = DmaController::ENABLE)
        {
            select(n);
            bus.poke(DMA + DmaController::CTRL, ctrl);
        }
        /// CONTROL written whole but for SELECT.
        void on(uint8_t control = DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE)
        {
            const uint8_t sel = bus.peek(CONTROL) & DmaController::SELECT_MASK;
            bus.poke(CONTROL, static_cast<uint8_t>((control & ~DmaController::SELECT_MASK) | sel));
        }
        /// Channel `n`'s register `reg`, through the window.
        uint8_t at(unsigned n, uint8_t reg)
        {
            select(n);
            return bus.peek(static_cast<uint16_t>(DMA + reg));
        }
        /// One CPU read cycle; @return the cycles the DMA held it for.
        unsigned cpu_cycle()
        {
            bus.read(0x0000);
            return bus.wait_states();
        }

        DmaController dma;
        Decoder bus;
        Ram ram{0x10000};
        Card card;
    };
} // namespace

TEST(registers_read_back_and_reset)
{
    Rig r;
    r.channel(1, 0xABCDE, 0x1234, 0x5A, 0xF1234);
    CHECK_EQ(r.bus.peek(DMA + 0x00), 0xDE);   // the window shows channel 1: SELECT 1
    CHECK_EQ(r.bus.peek(DMA + 0x02), 0x0A);
    CHECK_EQ(r.bus.peek(DMA + 0x04), 0x34);
    CHECK_EQ(r.bus.peek(DMA + 0x05), 0x12);
    CHECK_EQ(r.bus.peek(DMA + 0x06), 0x5A);
    CHECK_EQ(r.bus.peek(DMA + 0x0A), 0x0F);
    CHECK_EQ(r.bus.peek(DMA + 0x0C), 0x09);   // CARD: the video card's module, channel 1's at reset
    r.channel(1, 0xABCDEF, 0x1234, 0x5A, 0xF12345);
    CHECK_EQ(r.bus.peek(DMA + 0x02), 0xAB);   // 24 bits, A0-A23
    CHECK_EQ(r.bus.peek(DMA + 0x0A), 0xF1);
    CHECK_EQ(r.bus.peek(CONTROL), 0x72);      // $70 with SELECT 1
    CHECK_EQ(r.bus.peek(DMA + DmaController::ID_REG), 0x82);
    CHECK_EQ(r.bus.peek(ABORT), 0xFF);
    CHECK_EQ(r.bus.peek(DMA + 0x50), 0xFF);   // reserved
    r.card.fifo = {1};
    CHECK_EQ(r.bus.peek(STATUS), 0x00);       // terminal counts only: no request bits
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);     // no poll yet
    r.channel(0, 0x5000, 1, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    r.on();
    CHECK_EQ(r.cpu_cycle(), 3u);              // the takeover, the poll, the byte
    CHECK_EQ(r.bus.peek(REQUESTS), 0x01);     // channel 0's module was requesting at the poll
    r.bus.poke(REQUESTS, 0x00);
    CHECK_EQ(r.bus.peek(REQUESTS), 0x01);     // read only
    r.dma.reset();
    CHECK_EQ(r.bus.peek(CONTROL), 0x70);
    CHECK_EQ(r.bus.peek(DMA + 0x0C), 0x07);   // the window shows channel 0 again, CARD at its reset value
    CHECK_EQ(r.bus.peek(DMA + 0x00), 0x00);
    CHECK_EQ(r.dma.peek_channel(1, DmaController::ADDR), 0x00);
    CHECK_EQ(r.bus.peek(STATUS), 0x00);
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);
}

TEST(peek_channel_reads_each_channel_with_no_effect)
{
    // The side door for tests and tools (b2-plan.md, U2): a channel's registers by channel number, whatever
    // SELECT is, with nothing changed and no cycle taken.
    Rig r;
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        r.channel(n, 0x10203 * (n + 1), 0x0102 * (n + 1), static_cast<uint8_t>(0x40 + n), 0x30405 * (n + 1));
    r.arm(2, DmaController::ENABLE);
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n) // channels 4-7 too: must fail with the guard at 4
        for (uint8_t reg = 0; reg < 0x10; ++reg)
            CHECK_EQ(r.dma.peek_channel(n, reg), r.at(n, reg));
    r.select(2);
    const uint64_t before = r.bus.now();
    CHECK_EQ(r.dma.peek_channel(1, DmaController::ADDR), 0x06);   // $20406's low byte
    CHECK_EQ(r.dma.peek_channel(3, DmaController::MODE), 0x43);
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CTRL), DmaController::ENABLE);
    CHECK_EQ(r.dma.peek_channel(0, DmaController::CTRL), 0x00);
    CHECK_EQ(r.dma.peek_channel(1, DmaController::CARD), 0x09);
    CHECK_EQ(r.dma.peek_channel(7, DmaController::MODE), 0x47);                       // channels 4-7 live (B3)
    CHECK_EQ(r.dma.peek_channel(5, DmaController::ADDR), 0x12);                       // $10203 x 6 = $060C12
    CHECK_EQ(r.dma.peek_channel(4, DmaController::CARD), 0x08);                       // none, at reset
    CHECK_EQ(r.dma.peek_channel(DmaController::CHANNELS, DmaController::CTRL), 0xFF); // no such channel
    CHECK_EQ(r.dma.peek_channel(0, DmaController::ADDR + 3), 0xFF);                   // reserved
    CHECK_EQ(r.dma.peek_channel(0, 0x10), 0xFF);                                      // nor register
    CHECK_EQ(r.bus.now(), before);
    CHECK_EQ(r.bus.peek(CONTROL) & DmaController::SELECT_MASK, 2 << DmaController::SELECT_SHIFT); // SELECT kept
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CTRL), DmaController::ENABLE); // still armed
}

TEST(the_window_follows_select_on_all_eight_channels)
{
    // Must fail if the window decodes channel 0 whatever SELECT says (every driver would program channel 0),
    // or decodes only two of SELECT's bits.
    Rig r;
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        r.channel(n, 0x111111 * (n + 1), 0x100 + n, static_cast<uint8_t>(n), 0x0A0B0C + n);
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
    {
        r.select(n);
        CHECK_EQ(r.bus.peek(DMA + 0x00), static_cast<uint8_t>(0x11 * (n + 1)));
        CHECK_EQ(r.bus.peek(DMA + 0x02), static_cast<uint8_t>(0x11 * (n + 1)));
        CHECK_EQ(r.bus.peek(DMA + 0x04), static_cast<uint8_t>(n));
        CHECK_EQ(r.bus.peek(DMA + 0x06), static_cast<uint8_t>(n));
        CHECK_EQ(r.bus.peek(DMA + 0x08), static_cast<uint8_t>(0x0C + n));
        CHECK_EQ(r.bus.peek(DMA + 0x0C), DmaController::CARD_RESET[n]);
        for (const int reserved : {0x03, 0x0B, 0x0D, 0x0E, 0x0F})
            CHECK_EQ(r.bus.peek(static_cast<uint16_t>(DMA + reserved)), 0xFF);
    }
    r.bus.write(CONTROL, 0xFE);               // bit 7 is free: it reads 0
    CHECK_EQ(r.bus.peek(CONTROL), 0x7E);
    for (uint16_t a = DMA + 0x13; a < CONTROL; ++a)
        CHECK_EQ(r.bus.peek(a), 0xFF);        // $13-$7D reserved
}

TEST(select_reaches_one_channel_of_eight)
{
    // Each channel written through its own SELECT holds its own values and no other's (B3). Must fail if the
    // window decodes two of SELECT's bits (4-7 aliasing 0-3: a driver on channel 4 reprogramming the SD card's
    // channel 0), or still drops SELECT 4-7's writes.
    Rig r;
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
    {
        r.select(n);
        for (unsigned i = 0; i < 3; ++i)
            r.bus.write(static_cast<uint16_t>(DMA + DmaController::ADDR + i), static_cast<uint8_t>(n << 4 | i));
        r.bus.write(DMA + DmaController::MODE, static_cast<uint8_t>(n));
    }
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
    {
        for (unsigned i = 0; i < 3; ++i)
            CHECK_EQ(r.dma.peek_channel(n, static_cast<uint8_t>(DmaController::ADDR + i)), n << 4 | i);
        CHECK_EQ(r.dma.peek_channel(n, DmaController::MODE), n);
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
    }
    r.select(5);
    r.bus.write(DMA + DmaController::ADDR, 0xA5);
    r.bus.write(DMA + DmaController::MODE, 0x5A);
    r.bus.write(DMA + DmaController::CARD, 0x03);
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
    {
        CHECK_EQ(r.dma.peek_channel(n, DmaController::ADDR), n == 5 ? 0xA5 : n << 4);
        CHECK_EQ(r.dma.peek_channel(n, DmaController::MODE), n == 5 ? 0x5A : n);
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), n == 5 ? 0x03 : DmaController::CARD_RESET[n]);
    }
}

TEST(card_resets_to_todays_wiring)
{
    // Must fail if reset() leaves CARD alone: the SD boot would never be granted after a stray CARD.
    Rig r;
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
    {
        r.card_of(n, 0xFF);
        CHECK_EQ(r.at(n, DmaController::CARD), 0x1F); // MODULE and WIDE; bits 7-5 read 0
    }
    r.card_of(0, DmaController::WIDE | 0x7);
    CHECK_EQ(r.at(0, DmaController::CARD), 0x17);     // WIDE stored and read back (what it does: the WIDE cases below)
    r.dma.reset();
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
    CHECK_EQ(r.dma.peek_channel(0, DmaController::CARD), 0x07); // mass storage
    CHECK_EQ(r.dma.peek_channel(1, DmaController::CARD), 0x09); // video
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CARD), 0x0A); // sound
    CHECK_EQ(r.dma.peek_channel(3, DmaController::CARD), 0x08); // none

    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        r.card_of(n, 0x05);
    r.on(0x01);                                       // master on, BURST 0, SELECT 7 kept
    r.bus.pulse_reset();                              // Apple II mode's /RESET
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
    CHECK_EQ(r.bus.peek(CONTROL), 0x70);
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);
}

TEST(addresses_are_24_bits)
{
    // Must fail with the mask left at 20 bits: the source's top nibble dropped (reading $0FFFFF) and the
    // destination's too (writing RAM at $000000 instead of $E00000).
    Rig r;
    Ram top{0x10000}, high{0x10000};
    r.bus.map({0xFF0000, 0xFFFFFF, &top});
    r.bus.map({0xE00000, 0xE0FFFF, &high});
    top.write(0xFFFF, 0x5A);
    r.ram.write(0x0000, 0xA5);
    r.ram.write(0x0001, 0x3C);
    r.on();
    r.channel(3, 0xFFFFFF, 3, DmaController::MEM_TO_MEM, 0xE00000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 3);
    CHECK_EQ(high.peek(0x0000), 0x5A);        // from $FFFFFF
    CHECK_EQ(high.peek(0x0001), 0xA5);        // then $000000: the address wrapped within 24 bits
    CHECK_EQ(high.peek(0x0002), 0x3C);
    CHECK_EQ(r.at(3, DmaController::ADDR + 2), 0x00);
    CHECK_EQ(r.at(3, DmaController::ADDR), 0x02);
    CHECK_EQ(r.at(3, DmaController::DEST + 2), 0xE0); // DEST reached $E00003
    CHECK_EQ(r.at(3, DmaController::DEST), 0x03);
}

TEST(nothing_moves_until_the_master_enable)
{
    Rig r;
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 0u);               // CONTROL resets to $70: off
    r.on();
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 4);       // take the bus, then 2 cycles a byte: no card channel, no poll
    CHECK_EQ(r.cpu_cycle(), 0u);
}

TEST(memory_to_memory_copy_fill_and_count_down)
{
    Rig r;
    for (uint32_t i = 0; i < 16; ++i)
        r.ram.write(0x1000 + i, static_cast<uint8_t>(i + 1));
    r.on();
    r.channel(3, 0x1000, 16, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 33u);
    for (uint32_t i = 0; i < 16; ++i)
        CHECK_EQ(r.ram.read(0x2000 + i), i + 1);
    CHECK_EQ(r.at(3, DmaController::CTRL), 0x00); // ENABLE and REQ clear at terminal count
    CHECK_EQ(r.bus.peek(STATUS), 0x08);

    r.ram.write(0x1100, 0x5A);                 // HOLD on the source: a fill
    r.channel(2, 0x1100, 8, DmaController::MEM_TO_MEM | DmaController::HOLD, 0x3000);
    r.arm(2, DmaController::ENABLE | DmaController::REQ);
    r.cpu_cycle();
    for (uint32_t i = 0; i < 8; ++i)
        CHECK_EQ(r.ram.read(0x3000 + i), 0x5A);
    CHECK_EQ(r.ram.read(0x3008), 0x00);

    r.channel(2, 0x100F, 4, DmaController::MEM_TO_MEM | DmaController::DEC, 0x400F); // both count down
    r.arm(2, DmaController::ENABLE | DmaController::REQ);
    r.cpu_cycle();
    CHECK_EQ(r.ram.read(0x400F), 16);
    CHECK_EQ(r.ram.read(0x400C), 13);
    CHECK_EQ(r.ram.read(0x400B), 0x00);
    CHECK_EQ(r.at(2, DmaController::ADDR), 0x0B); // ADDR read back: the current address
}

TEST(count_0_is_65536_however_the_channel_got_it)
{
    Rig r;
    r.on();
    // COUNT never written since reset: 0, which means 65,536 (not 4G, a hang that writes all of memory)
    r.select(3);
    for (unsigned i = 0; i < 3; ++i)
        r.bus.poke(static_cast<uint16_t>(DMA + DmaController::DEST + i), static_cast<uint8_t>(0x10000 >> (8 * i)));
    r.bus.poke(DMA + DmaController::MODE, DmaController::MEM_TO_MEM);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 0x10000);
    CHECK_EQ(r.bus.peek(STATUS) & 0x0F, 0x08);
    CHECK_EQ(r.at(3, DmaController::ADDR), 0x00); // ADDR moved on by exactly 65,536
    CHECK_EQ(r.at(3, DmaController::ADDR + 2), 0x01);
    // Re-armed after terminal count without a new COUNT: 0 again, so 65,536 again
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 0x10000);
    CHECK_EQ(r.at(3, DmaController::ADDR + 2), 0x02);
}

TEST(single_mode_gives_the_cpu_a_cycle_after_each_byte)
{
    Rig r;
    r.on();
    r.card.fifo = {0x11, 0x22, 0x33};
    r.channel(0, 0x5000, 3, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 3u);               // take the bus + the poll + one fly-by byte
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.ram.read(0x5000), 0x11);
    CHECK_EQ(r.ram.read(0x5002), 0x33);
    CHECK(r.card.tcs == (std::vector<bool>{false, false, true}));
}

TEST(demand_mode_runs_while_drq_is_held_up_to_the_burst)
{
    Rig r;
    r.on();
    r.card.fifo = {1, 2, 3, 4, 5};
    r.channel(0, 0x5000, 100, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 5);           // a poll each take, then until the card lets go
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.at(0, DmaController::COUNT), 95); // COUNT left
    CHECK_EQ(r.at(0, DmaController::CTRL), DmaController::ENABLE); // still armed

    r.on(0x10 | DmaController::MASTER_ENABLE); // BURST 1: two bytes, then a CPU cycle
    r.card.fifo = {6, 7, 8, 9, 10};
    CHECK_EQ(r.cpu_cycle(), 4u);
    CHECK_EQ(r.cpu_cycle(), 4u);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.ram.read(0x5009), 10);
}

TEST(block_mode_finishes_the_count_once_started)
{
    Rig r;
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.card.want = 0;                           // the card never requests: REQ starts it
    r.channel(1, 0x6000, 3, DmaController::BLOCK | DmaController::TO_CARD);
    r.card_of(1, 0x07);                        // channel 1 serves the card too, by its module
    for (uint32_t i = 0; i < 3; ++i)
        r.ram.write(0x6000 + i, static_cast<uint8_t>(0xA0 + i));
    r.arm(1, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 2u);               // carries on without a request or REQ
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK(r.card.sent == (std::vector<uint8_t>{0xA0, 0xA1, 0xA2}));
    REQUIRE(!r.card.tcs.empty()); // no byte reached the card: fail here, not on back() of nothing
    CHECK(r.card.tcs.back());
}

TEST(block_mode_started_by_drq_carries_on_after_it_drops)
{
    Rig r;
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.card.fifo = {0x51};                      // a request for one byte only
    r.channel(0, 0x6100, 3, DmaController::BLOCK | DmaController::TO_MEMORY);
    r.arm(0);                                  // no REQ: the card's request starts it
    CHECK_EQ(r.cpu_cycle(), 3u);               // the poll found it
    CHECK(!r.card.drq());
    CHECK_EQ(r.cpu_cycle(), 2u);               // a block, once started, runs to the count, /DREQ high
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.ram.read(0x6100), 0x51);
    CHECK_EQ(r.ram.read(0x6102), 0xEE);        // the card's empty FIFO
    REQUIRE(!r.card.tcs.empty()); // no byte reached the card: fail here, not on back() of nothing
    CHECK(r.card.tcs.back());
}

TEST(terminal_count_interrupt_and_auto_reload)
{
    Rig r;
    r.on();
    r.card.fifo = {1, 2};
    r.channel(0, 0x5000, 2, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    r.cpu_cycle();
    CHECK_EQ(r.bus.peek(STATUS) & 0x0F, 0x01);
    CHECK(!r.bus.irq());                       // no MODE.IRQ: status only
    r.bus.write(STATUS, 0x01);
    CHECK_EQ(r.bus.peek(STATUS) & 0x0F, 0x00);

    r.card.fifo = {1, 2, 3, 4, 5};
    r.channel(0, 0x5100, 2, DmaController::DEMAND | DmaController::TO_MEMORY | DmaController::AUTO |
                                DmaController::IRQ);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 4u);               // stops at terminal count, even with the request up
    CHECK(r.bus.irq());
    CHECK_EQ(r.at(0, DmaController::CTRL), DmaController::ENABLE); // AUTO: still armed
    CHECK_EQ(r.at(0, DmaController::ADDR), 0x00); // and back at the start
    CHECK_EQ(r.at(0, DmaController::COUNT), 2);   // with the count reloaded
    CHECK_EQ(r.cpu_cycle(), 4u);               // a ring buffer: the same 2 bytes again
    CHECK_EQ(r.ram.read(0x5100), 3);
    CHECK(r.card.drq());                       // the 5th byte waits: TC stopped it
    r.bus.write(STATUS, 0x01);
    CHECK(!r.bus.irq());
}

TEST(channel_0_goes_first_and_abort_stops_a_channel)
{
    Rig r;
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    r.card.fifo = {9};
    r.channel(0, 0x5000, 10, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 3u);               // channel 0's fly-by byte first, after the poll
    CHECK_EQ(r.ram.read(0x5000), 9);
    CHECK_EQ(r.cpu_cycle(), 3u);               // then channel 3, one byte a take, /DREQ high: no poll
    r.bus.write(ABORT, 0x08);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.at(3, DmaController::COUNT), 2); // COUNT kept where it stopped
    CHECK_EQ(r.at(3, DmaController::ADDR), 0x02);
    CHECK_EQ(r.at(3, DmaController::CTRL), 0x00);
}

TEST(a_card_is_granted_by_its_module)
{
    // Must fail if a card is found by its channel's index: the grant naming the wrong card on PS0-PS3.
    Rig r;
    Card other;
    r.bus.map({0x0D010, 0x0D013, &other, 0x5, false, 0, Dreq::Wired});
    r.card.want = 0;                           // module 7, channel 0's at reset: quiet
    other.fifo = {0x61, 0x62};
    r.on();
    r.channel(0, 0x5000, 10, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    r.channel(1, 0x5100, 10, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.card_of(1, 0x05);                        // channel 1 serves module 5
    r.arm(1);
    CHECK_EQ(r.cpu_cycle(), 2u + 2);           // the poll finds module 5: channel 1, two bytes
    CHECK_EQ(r.bus.peek(REQUESTS), 0x02);
    CHECK_EQ(r.ram.read(0x5100), 0x61);
    CHECK_EQ(r.ram.read(0x5101), 0x62);
    CHECK_EQ(r.ram.read(0x5000), 0x00);        // channel 0 moved nothing
    CHECK_EQ(r.at(0, DmaController::COUNT), 10);
    CHECK(r.card.tcs.empty());                 // and its card took no grant

    // MODULE none on a card direction: a floating bus, and no card named.
    r.ram.write(0x5200, 0x00);
    r.channel(2, 0x5200, 1, DmaController::BLOCK | DmaController::TO_MEMORY);
    r.card_of(2, DmaController::CARD_NONE);
    r.arm(2, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.ram.read(0x5200), 0xFF);
    CHECK(r.card.tcs.empty());
    CHECK_EQ(other.tcs.size(), size_t{2});
}

TEST(channels_4_to_7_copy_memory_to_memory)
{
    // B3: channels 4-7 are served like 0-3. Must fail with the take's armed-channel scan left at four channels
    // (a CPLD's request logic built for four: a driver on channel 5 waits for ever).
    Rig r;
    for (uint32_t i = 0; i < 4; ++i)
    {
        r.ram.write(0x1500 + i, static_cast<uint8_t>(0x50 + i));
        r.ram.write(0x1700 + i, static_cast<uint8_t>(0x70 + i));
    }
    r.on();
    r.channel(5, 0x1500, 4, DmaController::MEM_TO_MEM, 0x2500);
    r.arm(5, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 4);       // 1 + 2n: no card channel, no poll
    for (uint32_t i = 0; i < 4; ++i)
        CHECK_EQ(r.ram.read(0x2500 + i), 0x50 + i);
    CHECK_EQ(r.bus.peek(STATUS), 0x20);
    CHECK_EQ(r.at(5, DmaController::CTRL), 0x00); // no AUTO: ENABLE and REQ clear,
    CHECK_EQ(r.at(5, DmaController::COUNT), 0x00); // and COUNT run out
    r.channel(7, 0x1700, 4, DmaController::MEM_TO_MEM, 0x2700);
    r.arm(7, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 4);
    for (uint32_t i = 0; i < 4; ++i)
        CHECK_EQ(r.ram.read(0x2700 + i), 0x70 + i);
    CHECK_EQ(r.bus.peek(STATUS), 0xA0);
    CHECK_EQ(r.at(7, DmaController::CTRL), 0x00);
    CHECK_EQ(r.at(7, DmaController::COUNT), 0x00);
    CHECK_EQ(r.cpu_cycle(), 0u);
}

TEST(a_card_on_channel_5_is_granted_by_its_module)
{
    // B3: a card served by a channel 4-7. Must fail with the poll's REQ latch left at four channels: the card is
    // never granted (the bus goes back after the poll and the hold-off runs).
    Rig r;
    r.on();
    r.card_of(0, DmaController::CARD_NONE);   // channel 0 no longer serves module 7
    r.channel(0, 0x6600, 3, DmaController::DEMAND | DmaController::TO_MEMORY);   // programmed, never armed
    r.channel(5, 0x5500, 10, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.card_of(5, 0x07);                        // channel 5 does: the Rig's card
    r.arm(5);
    r.card.fifo = {0x91, 0x92};
    CHECK_EQ(r.cpu_cycle(), 2u + 2);           // the takeover, the poll, two fly-by bytes
    CHECK_EQ(r.bus.peek(REQUESTS), 0x20);      // channel 5's module, and channel 0's bit stays 0
    CHECK_EQ(r.ram.read(0x5500), 0x91);
    CHECK_EQ(r.ram.read(0x5501), 0x92);
    CHECK_EQ(r.card.tcs.size(), size_t{2});    // the grant named module 7
    CHECK_EQ(r.at(5, DmaController::COUNT), 8);
    CHECK_EQ(r.at(0, DmaController::COUNT), 3);   // channel 0 untouched by the grant
    CHECK_EQ(r.ram.read(0x6600), 0x00);
}

TEST(status_abort_and_the_interrupt_cover_channels_4_to_7)
{
    // B3: channel n's bit in STATUS, ABORT and the interrupt for 4-7. Must fail with irq()'s loop left at four
    // (an interrupt from channel 6 that never comes), ABORT's walk cut to four (Break unable to stop a channel
    // 4-7), or a bit aliased onto 0-3 (ABORT $10 stopping the SD card's channel 0).
    Rig r;
    r.ram.write(0x1600, 0x66);
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.channel(6, 0x1600, 1, DmaController::MEM_TO_MEM | DmaController::IRQ, 0x2600);
    r.arm(6, DmaController::ENABLE | DmaController::REQ);
    CHECK(!r.bus.irq());
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.ram.read(0x2600), 0x66);
    CHECK_EQ(r.bus.peek(STATUS), 0x40);        // channel 6's terminal count, and only it
    CHECK(r.bus.irq());                        // MODE.IRQ: it pulls the line
    r.bus.write(STATUS, 0x04);                 // bit 2 is not channel 6's
    CHECK(r.bus.irq());
    r.bus.write(STATUS, 0x40);
    CHECK_EQ(r.bus.peek(STATUS), 0x00);
    CHECK(!r.bus.irq());

    r.channel(7, 0x1000, 10, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(7, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 3u);
    r.bus.write(ABORT, 0x80);                  // stops channel 7 mid-block (the write's own cycle took a byte)
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.at(7, DmaController::COUNT), 7); // its count left part way
    CHECK_EQ(r.at(7, DmaController::CTRL), 0x00);
    CHECK_EQ(r.bus.peek(STATUS), 0x00);        // no terminal count

    r.channel(0, 0x1000, 4, DmaController::MEM_TO_MEM, 0x3000);
    r.arm(0, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 3u);
    r.bus.write(ABORT, 0x10);                  // channel 4, idle: channel 0 runs on
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.at(0, DmaController::CTRL), DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.at(0, DmaController::COUNT), 1); // a byte for each take, the ABORT write's included
}

TEST(priority_runs_from_channel_0_to_7)
{
    // B3: the fixed priority covers 4-7 in order. Channels 3, 4 and 7 started at once, memory to memory with
    // BURST 0, each run to its count in turn; then channel 4, started while 7 is part way, cuts in. Must fail
    // with the priority scan reversed or wrapped for 4-7 (a later driver starving the SD card or the screen).
    struct Writes : CycleObserver
    {
        void on_cycle(const BusCycle &c) override
        {
            if (c.dma && c.write)
                addrs.push_back(c.addr);
        }
        std::vector<uint16_t> addrs;
    } writes;
    Rig r;
    r.bus.set_dma_observer(&writes);
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.channel(7, 0x1000, 2, DmaController::MEM_TO_MEM, 0x2700);
    r.channel(4, 0x1000, 2, DmaController::MEM_TO_MEM, 0x2400);
    r.channel(3, 0x1000, 2, DmaController::MEM_TO_MEM, 0x2300);
    r.arm(7, DmaController::ENABLE | DmaController::REQ);
    r.arm(4, DmaController::ENABLE | DmaController::REQ);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    for (unsigned i = 0; i < 6; ++i)
        CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK(writes.addrs == (std::vector<uint16_t>{0x2300, 0x2301, 0x2400, 0x2401, 0x2700, 0x2701}));
    CHECK_EQ(r.bus.peek(STATUS), 0x98);

    writes.addrs.clear();
    r.channel(7, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2780);
    r.arm(7, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 3u);               // channel 7's first byte
    r.channel(4, 0x1000, 2, DmaController::MEM_TO_MEM, 0x2480);
    r.arm(4, DmaController::ENABLE | DmaController::REQ);
    for (unsigned i = 0; i < 5; ++i)
        CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK(writes.addrs == (std::vector<uint16_t>{0x2780, 0x2480, 0x2481, 0x2781, 0x2782, 0x2783}));
}

TEST(a_stray_request_costs_2_cycles_in_64)
{
    // A card requesting with no channel for it, beside an armed channel whose card is quiet. Must fail on a
    // poll every cycle (a stray card halving the CPU's speed).
    Rig r;
    Card stray;
    r.bus.map({0x0D010, 0x0D013, &stray, 0x5, false, 0, Dreq::Wired});
    stray.want = 1;
    r.card.want = 0;
    r.on();
    r.channel(0, 0x5000, 10, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u);               // the takeover and the poll, then the bus goes back
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);      // no channel serves module 5
    unsigned stolen = 0;
    for (unsigned i = 0; i < 61; ++i)          // the 64 cycles from the take's first, less its 3
        stolen += r.cpu_cycle();
    CHECK_EQ(stolen, 0u);
    CHECK_EQ(r.cpu_cycle(), 2u);               // 64 cycles on: another poll

    // The accepted cost (b2-plan.md, question 1): a card that becomes ready meanwhile waits for the hold-off.
    r.card.want = -1;
    r.card.fifo = {0x77};
    stolen = 0;
    for (unsigned i = 0; i < 61; ++i)
        stolen += r.cpu_cycle();
    CHECK_EQ(stolen, 0u);
    CHECK_EQ(r.ram.read(0x5000), 0x00);
    CHECK_EQ(r.cpu_cycle(), 3u);               // then served: the takeover, the poll, the byte
    CHECK_EQ(r.ram.read(0x5000), 0x77);
}

TEST(a_stray_request_with_no_card_channel_armed_costs_nothing)
{
    // The owner's answer 2: a take polls only with a card channel armed, so a stray request (the sound card's
    // standing one, say) costs memory to memory nothing. Must fail on a controller that polls every take with
    // /DREQ low: 2 + 2n, not 1 + 2n, with the sound card fitted.
    Rig r;
    Card stray;
    r.bus.map({0x0D010, 0x0D013, &stray, 0x5, false, 0, Dreq::Wired});
    stray.want = 1;                            // module 5: no channel's CARD names it
    r.card.want = 0;
    r.on();
    REQUIRE(r.bus.dreq());                     // /DREQ low throughout
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 4);       // the takeover and 2 cycles a byte: no poll
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);      // REQ: no poll was made
    CHECK_EQ(r.at(3, DmaController::CTRL), 0x00);
    CHECK_EQ(r.cpu_cycle(), 0u);

    // Nothing armed at all: no take, so no stray polls, not even 2 cycles in 64.
    unsigned stolen = 0;
    for (unsigned i = 0; i < 130; ++i)
        stolen += r.cpu_cycle();
    CHECK_EQ(stolen, 0u);
    CHECK_EQ(r.bus.peek(REQUESTS), 0x00);
}

TEST(the_hold_off_ends_on_a_ctrl_write_and_on_dreq_high_then_low)
{
    // Must fail if only the 64 cycles end it: a freshly armed channel starting late, a card made ready waiting.
    Rig r;
    Card stray;
    r.bus.map({0x0D010, 0x0D013, &stray, 0x5, false, 0, Dreq::Wired});
    stray.want = 1;
    r.card.want = 0;
    r.on();
    r.channel(0, 0x5000, 10, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u);               // a poll that grants nothing
    CHECK_EQ(r.cpu_cycle(), 0u);               // held off
    r.bus.write(DMA + DmaController::CTRL, DmaController::ENABLE); // (SELECT 0) a CTRL write ends it
    CHECK_EQ(r.cpu_cycle(), 2u);               // so the next take polls again

    CHECK_EQ(r.cpu_cycle(), 0u);               // held off again
    stray.want = 0;                            // /DREQ high...
    CHECK_EQ(r.cpu_cycle(), 0u);
    stray.want = 1;                            // ...and low again: a new request
    CHECK_EQ(r.cpu_cycle(), 2u);

    r.bus.write(DMA + DmaController::CTRL, DmaController::ENABLE); // and a card made ready is polled at once
    r.card.want = 1;
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.ram.read(0x5000), 0xEE);
}

TEST(a_demand_transfer_ends_when_the_granted_card_lets_go)
{
    // The release rule: in a grant only the card named on PS0-PS3 counts. Must fail if the grant samples the
    // OR of /DREQ: with a second card holding it, the transfer would run on past the first card's data.
    Rig r;
    Card other;
    r.bus.map({0x0D010, 0x0D013, &other, 0x5, false, 0, Dreq::Wired});
    other.want = 1;                            // holds /DREQ throughout, with no channel for it
    r.card.fifo = {1, 2, 3};
    r.on();
    r.channel(0, 0x5000, 100, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 3);           // the poll, then the card's three bytes
    CHECK_EQ(r.ram.read(0x5002), 3);
    CHECK_EQ(r.ram.read(0x5003), 0x00);        // and no more
    CHECK_EQ(r.at(0, DmaController::COUNT), 97);
    CHECK(other.tcs.empty());
}

TEST(the_poll_takes_no_wait_and_a_fly_by_takes_the_cards)
{
    // /WAIT (hw/design.md): the takeover and the poll are the controller's own cycles; a grant is stretched
    // by the longer of the memory's and the named card's /WAIT. Must fail if the card's side of a fly-by
    // isn't counted, or the poll takes /WAIT.
    Rig r;
    Card slow;
    r.bus.map({0x0D010, 0x0D013, &slow, 0x5, false, 2, Dreq::Wired});
    r.card.want = 0;
    slow.fifo = {0x42};
    r.channel(0, 0x5000, 1, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.card_of(0, 0x05);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 0u);               // the master off costs nothing, a card requesting or not
    r.on();
    CHECK_EQ(r.cpu_cycle(), 1u + 1 + (1 + 2)); // the takeover, the poll, a byte held 2 cycles by the card
    CHECK_EQ(r.ram.read(0x5000), 0x42);
}

TEST(the_take_order_serves_software_started_channels_beside_a_poll)
{
    // b2-plan.md [C4]: a software-started channel 3 and a stray request at once. Must fail if the hold-off
    // starts on any poll that found no card to grant, or if software-started channels are skipped while
    // /DREQ is low.
    Rig r;
    Card stray;
    r.bus.map({0x0D010, 0x0D013, &stray, 0x5, false, 0, Dreq::Wired});
    stray.want = 1;
    r.card.want = 0;
    for (uint32_t i = 0; i < 4; ++i)
        r.ram.write(0x1000 + i, static_cast<uint8_t>(0x30 + i));
    r.on();
    r.channel(0, 0x5000, 10, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);                                  // a card channel, armed, its card quiet
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 1 + 2 * 4);   // the take polls, and grants channel 3 in the same take
    CHECK_EQ(r.ram.read(0x2003), 0x33);
    CHECK_EQ(r.cpu_cycle(), 2u);               // no hold-off yet: this poll's set is empty, so now
    CHECK_EQ(r.cpu_cycle(), 0u);

    // A card channel of higher priority that requested at the poll goes before channel 3.
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x3000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ); // (a CTRL write: the hold-off ends)
    r.card.want = -1;
    r.card.fifo = {0x99};
    CHECK_EQ(r.cpu_cycle(), 1u + 1 + 1);       // channel 0's byte; the card lets go
    CHECK_EQ(r.ram.read(0x5000), 0x99);
    CHECK_EQ(r.ram.read(0x3000), 0x00);        // channel 3 not yet
    CHECK_EQ(r.cpu_cycle(), 1u + 1 + 2 * 4);   // then channel 3, beside the stray request's poll
    CHECK_EQ(r.ram.read(0x3003), 0x33);
}

TEST(timers_and_the_cpu_clock_run_through_dma_cycles)
{
    struct Clock : Ram
    {
        Clock() : Ram(16) {}
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override { total += cycles; }
        uint8_t read(uint32_t) override { return static_cast<uint8_t>(total); }
        uint64_t total{0};
    };
    Rig r;
    Clock clock;
    r.bus.map({0x0E000, 0x0E00F, &clock});
    r.on();
    r.channel(3, 0x1000, 10, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    const uint64_t before = r.bus.now();
    CHECK_EQ(r.bus.read(0xE000), static_cast<uint8_t>(before + 21)); // ticked through the DMA too
    CHECK_EQ(r.bus.now(), before + 22);
}

// 16-bit transfers (step B4, hw/design.md "16-bit transfers"): CARD's WIDE moves a word a transfer where the rules
// allow. COUNT stays in bytes; a word costs what a byte does.

namespace
{
    constexpr uint8_t WIDE_NONE = DmaController::WIDE | DmaController::CARD_NONE; ///< CARD $18
    constexpr uint8_t WIDE_SD = DmaController::WIDE | 0x7;                         ///< CARD $17: the test card

    void fill(Ram &ram, uint32_t at, uint32_t n, uint8_t seed)
    {
        for (uint32_t i = 0; i < n; ++i)
            ram.write(at + i, static_cast<uint8_t>(i * 13 + seed));
    }
    /// The bytes at `dest` differing from those at `src`, `n` of each.
    size_t differ(Ram &ram, uint32_t src, uint32_t dest, uint32_t n)
    {
        size_t d = 0;
        for (uint32_t i = 0; i < n; ++i)
            d += ram.peek(src + i) != ram.peek(dest + i);
        return d;
    }
} // namespace

TEST(wide_memory_to_memory_moves_a_word_in_two_cycles)
{
    // Must fail with WIDE ignored (1 + 4,096): the flip-flop not wired to the transfer logic.
    Rig r;
    fill(r.ram, 0x1000, 0x800, 5);
    r.on();
    r.card_of(3, WIDE_NONE);
    r.channel(3, 0x1000, 0x800, DmaController::MEM_TO_MEM, 0x4000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 2048);
    CHECK_EQ(differ(r.ram, 0x1000, 0x4000, 0x800), size_t{0});
    CHECK_EQ(r.bus.peek(STATUS), 0x08);
    CHECK_EQ(r.dma.peek_channel(3, DmaController::CTRL), 0x00); // done: ENABLE and REQ clear
}

TEST(wide_odd_head_and_last_bytes_move_as_bytes)
{
    // A word only at an even address with two bytes left: from an odd ADDR, count 6, a byte, two words, a byte;
    // TC on the last grant whatever its width. Must fail with a word taken at an odd address, or TC compared
    // with 1 (a byte-only TC comparator: a card on a WIDE channel never sees TC, so its job never ends).
    Rig r;
    r.on();
    r.card_of(0, WIDE_SD);
    r.card.fifo = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    r.channel(0, 0x5001, 6, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 4);                     // takeover, poll, four grants
    CHECK(r.card.widths == (std::vector<int>{1, 2, 2, 1}));
    CHECK(r.card.tcs == (std::vector<bool>{false, false, false, true}));
    for (uint32_t i = 0; i < 6; ++i)
        CHECK_EQ(r.ram.peek(0x5001 + i), static_cast<uint8_t>(0x11 * (i + 1)));
    CHECK_EQ(r.ram.peek(0x5000), 0x00);
    CHECK_EQ(r.ram.peek(0x5007), 0x00);
    CHECK_EQ(r.bus.peek(STATUS), 0x01);

    r.card.widths.clear();
    r.card.tcs.clear();
    r.card.fifo = {0xA1, 0xA2, 0xA3, 0xA4};
    r.channel(0, 0x6000, 4, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 2);
    CHECK(r.card.widths == (std::vector<int>{2, 2}));
    CHECK(r.card.tcs == (std::vector<bool>{false, true}));
    for (uint32_t i = 0; i < 4; ++i)
        CHECK_EQ(r.ram.peek(0x6000 + i), static_cast<uint8_t>(0xA1 + i));
}

TEST(a_wide_demand_transfer_ends_when_the_card_lets_go_after_a_word)
{
    // The release rule after a word grant: a card whose FIFO runs dry before TC lets go of /DREQ, and the burst
    // ends there, COUNT left and the channel still armed. Must fail if the release is checked only after a byte
    // grant: a WIDE channel would run on past the card's data and overrun it.
    Rig r;
    r.on();
    r.card_of(0, WIDE_SD);
    r.card.fifo = {0x11, 0x22, 0x33, 0x44};
    r.channel(0, 0x5000, 8, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 2);                     // takeover, poll, two words
    CHECK_EQ(r.cpu_cycle(), 0u);                         // the card has let go
    CHECK(r.card.widths == (std::vector<int>{2, 2}));
    CHECK(r.card.tcs == (std::vector<bool>{false, false}));
    CHECK_EQ(r.at(0, DmaController::COUNT), 4);
    CHECK_EQ(r.at(0, DmaController::CTRL), DmaController::ENABLE); // still armed
    CHECK_EQ(r.bus.peek(STATUS), 0x00);
    CHECK_EQ(r.ram.peek(0x5004), 0x00);                  // and no more

    r.card.fifo = {0x55, 0x66, 0x77, 0x88};              // the card asks again: the rest, to TC
    CHECK_EQ(r.cpu_cycle(), 2u + 2);
    CHECK(r.card.widths == (std::vector<int>{2, 2, 2, 2}));
    CHECK(r.card.tcs == (std::vector<bool>{false, false, false, true}));
    for (uint32_t i = 0; i < 8; ++i)
        CHECK_EQ(r.ram.peek(0x5000 + i), static_cast<uint8_t>(0x11 * (i + 1)));
    CHECK_EQ(r.bus.peek(STATUS), 0x01);
}

TEST(dec_and_hold_move_bytes_under_wide)
{
    // B0 review round 1's Q10: WIDE moves bytes with DEC or HOLD. Must fail if WIDE is honoured under DEC (the
    // pairs reversed) or HOLD (every other byte of a fill taken from the byte after the source).
    auto copy = [](uint8_t card, uint8_t mode, uint32_t src, uint32_t dest, uint32_t n, Ram &out) {
        Rig r;
        fill(r.ram, 0x1000, 0x40, 9);
        r.on();
        r.card_of(3, card);
        r.channel(3, src, n, static_cast<uint8_t>(DmaController::MEM_TO_MEM | mode), dest);
        r.arm(3, DmaController::ENABLE | DmaController::REQ);
        const unsigned cycles = r.cpu_cycle();
        for (uint32_t i = 0; i < 0x10000; ++i)
            out.write(i, r.ram.peek(i));
        return cycles;
    };
    Ram plain{0x10000}, wide{0x10000};
    CHECK_EQ(copy(DmaController::CARD_NONE, DmaController::DEC, 0x101F, 0x201F, 16, plain), 1u + 2 * 16);
    CHECK_EQ(copy(WIDE_NONE, DmaController::DEC, 0x101F, 0x201F, 16, wide), 1u + 2 * 16);
    size_t d = 0;
    for (uint32_t i = 0; i < 0x40; ++i)
        d += plain.peek(0x2000 + i) != wide.peek(0x2000 + i);
    CHECK_EQ(d, size_t{0});
    CHECK_EQ(wide.peek(0x2010), plain.peek(0x1010));     // the bytes where they belong
    CHECK_EQ(wide.peek(0x201F), plain.peek(0x101F));
    CHECK_EQ(wide.peek(0x200F), 0x00);                   // and none past the count

    CHECK_EQ(copy(WIDE_NONE, DmaController::HOLD, 0x1000, 0x3000, 16, wide), 1u + 2 * 16); // a fill
    for (uint32_t i = 0; i < 16; ++i)
        CHECK_EQ(wide.peek(0x3000 + i), wide.peek(0x1000));

    Rig r;                                               // a fly-by with DEC, and one with HOLD
    r.on();
    r.card_of(0, WIDE_SD);
    r.card.fifo = {1, 2, 3, 4};
    r.channel(0, 0x5008, 4, DmaController::DEMAND | DmaController::TO_MEMORY | DmaController::DEC);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 4);
    CHECK(r.card.widths == (std::vector<int>{1, 1, 1, 1}));
    CHECK_EQ(r.ram.peek(0x5008), 1);
    CHECK_EQ(r.ram.peek(0x5005), 4);
    r.card.widths.clear();
    r.card.fifo = {7, 8, 9, 10};
    r.channel(0, 0x5100, 4, DmaController::DEMAND | DmaController::TO_MEMORY | DmaController::HOLD);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 4);
    CHECK(r.card.widths == (std::vector<int>{1, 1, 1, 1}));
    CHECK_EQ(r.ram.peek(0x5100), 10);                    // the last byte held there
    CHECK_EQ(r.ram.peek(0x5101), 0x00);
}

TEST(memory_to_memory_parity_decides_the_width)
{
    // The latch keeps each byte on its lane, so a word moves only between addresses of one parity. Must fail with
    // the parity check dropped: a latch pairing the source's bytes on the wrong destination lanes, a shifted copy.
    auto copy = [](uint32_t src, uint32_t dest, unsigned want) {
        Rig r;
        fill(r.ram, 0x1000, 0x40, 3);
        r.on();
        r.card_of(3, WIDE_NONE);
        r.channel(3, src, 16, DmaController::MEM_TO_MEM, dest);
        r.arm(3, DmaController::ENABLE | DmaController::REQ);
        CHECK_EQ(r.cpu_cycle(), want);
        CHECK_EQ(differ(r.ram, src, dest, 16), size_t{0});
        CHECK_EQ(r.ram.peek(dest - 1), 0x00);
        CHECK_EQ(r.ram.peek(dest + 16), 0x00);
    };
    copy(0x1000, 0x2000, 1 + 2 * 8);         // even to even: 8 words
    copy(0x1001, 0x2001, 1 + 2 * (1 + 7 + 1)); // odd to odd: a byte, 7 words, a byte
    copy(0x1000, 0x2001, 1 + 2 * 16);        // even to odd: bytes
    copy(0x1001, 0x2000, 1 + 2 * 16);        // odd to even: bytes
}

TEST(wide_steps_count_by_2_and_addr_and_dest_by_2)
{
    // BURST 0, one transfer a take: COUNT down 2, ADDR and DEST up 2 a word; $FFFFFE + 2 wraps to 0; AUTO
    // reloads and MODE.IRQ pulls the line at the terminal count. Must fail with step() by 1 under WIDE (every
    // other byte written twice), or the wrap at $FFFFFF.
    Rig r;
    Ram top{0x10000};
    r.bus.map({0xFF0000, 0xFFFFFF, &top});
    fill(r.ram, 0x1000, 6, 1);
    r.on(0x01);                                          // master on, BURST 0
    r.card_of(3, WIDE_NONE);
    r.channel(3, 0x1000, 6, DmaController::MEM_TO_MEM | DmaController::AUTO | DmaController::IRQ, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    for (unsigned take = 1; take <= 3; ++take)
    {
        CHECK_EQ(r.cpu_cycle(), 3u);                     // takeover, a word's read and write
        if (take < 3)
        {
            CHECK_EQ(r.dma.peek_channel(3, DmaController::COUNT), 6 - 2 * take);
            CHECK_EQ(r.dma.peek_channel(3, DmaController::ADDR), 2 * take);         // $1000 + 2 x take, low byte
            CHECK_EQ(r.dma.peek_channel(3, DmaController::DEST), 2 * take);
            CHECK_EQ(r.dma.peek_channel(3, DmaController::ADDR + 1), 0x10);
            CHECK(!r.bus.irq());
        }
    }
    CHECK_EQ(differ(r.ram, 0x1000, 0x2000, 6), size_t{0});
    CHECK_EQ(r.bus.peek(STATUS), 0x08);
    CHECK(r.bus.irq());                                  // MODE.IRQ at the terminal count
    CHECK_EQ(r.dma.peek_channel(3, DmaController::COUNT), 6);                       // AUTO reloaded
    CHECK_EQ(r.dma.peek_channel(3, DmaController::ADDR), 0x00);
    CHECK_EQ(r.dma.peek_channel(3, DmaController::DEST), 0x00);
    CHECK_EQ(r.dma.peek_channel(3, DmaController::CTRL), DmaController::ENABLE);    // still armed

    top.write(0xFFFE, 0xC1);
    top.write(0xFFFF, 0xC2);
    r.ram.write(0x0000, 0xC3);
    r.ram.write(0x0001, 0xC4);
    r.channel(3, 0xFFFFFE, 4, DmaController::MEM_TO_MEM, 0x3000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.dma.peek_channel(3, DmaController::ADDR + 2), 0x00); // wrapped: $000000
    CHECK_EQ(r.dma.peek_channel(3, DmaController::ADDR), 0x00);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.ram.peek(0x3000), 0xC1);
    CHECK_EQ(r.ram.peek(0x3001), 0xC2);
    CHECK_EQ(r.ram.peek(0x3002), 0xC3);
    CHECK_EQ(r.ram.peek(0x3003), 0xC4);
}

TEST(a_byte_only_card_on_a_wide_channel_floats_the_odd_bytes)
{
    // The documented misuse (hw/design.md, "16-bit transfers"): WIDE on a channel whose card has D0-D7 only. Each
    // word grant is one access of its register; D8-D15 float to memory and the odd bytes from memory are lost.
    // Must fail if the default word hooks moved two bytes, hiding the misuse.
    Rig r;
    r.card.words = false;
    r.on();
    r.card_of(0, WIDE_SD);
    r.card.fifo = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    r.channel(0, 0x5000, 8, DmaController::DEMAND | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        CHECK_EQ(r.ram.peek(0x5000 + 2 * i), static_cast<uint8_t>(0x31 + i));
        CHECK_EQ(r.ram.peek(0x5001 + 2 * i), 0xFF);
    }
    CHECK_EQ(r.card.fifo.size(), size_t{4});             // half the card's bytes still there
    CHECK(r.card.tcs.back());

    fill(r.ram, 0x6000, 8, 0);
    r.card.want = 1;
    r.channel(0, 0x6000, 8, DmaController::DEMAND | DmaController::TO_CARD);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u + 4);
    REQUIRE(r.card.sent.size() == 4u);
    for (uint32_t i = 0; i < 4; ++i)
        CHECK_EQ(r.card.sent[i], r.ram.peek(0x6000 + 2 * i)); // the even bytes only
}

TEST(single_mode_under_wide_is_3_cycles_a_word)
{
    // The takeover, the poll, the word. Must fail if the word is split into two grants (5 or 6 cycles).
    Rig r;
    r.on();
    r.card_of(0, WIDE_SD);
    r.card.fifo = {0x41, 0x42, 0x43, 0x44};
    r.channel(0, 0x5000, 4, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.ram.peek(0x5000), 0x41);
    CHECK_EQ(r.ram.peek(0x5001), 0x42);
    CHECK_EQ(r.dma.peek_channel(0, DmaController::COUNT), 2);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK(r.card.widths == (std::vector<int>{2, 2}));
    CHECK_EQ(r.bus.peek(STATUS), 0x01);
}

TEST(burst_counts_transfers_under_wide)
{
    // The owner's Q1: BURST counts transfers, a byte or a word, so the CPU's longest wait is the same number of
    // cycles with or without WIDE. Must fail with the bytes counted (moved += width).
    for (const unsigned burst : {0u, 1u})
    {
        Rig r;
        fill(r.ram, 0x1000, 8, 2);
        r.on(static_cast<uint8_t>(burst << 4 | DmaController::MASTER_ENABLE));
        r.card_of(3, WIDE_NONE);
        r.channel(3, 0x1000, 8, DmaController::MEM_TO_MEM, 0x2000);
        r.arm(3, DmaController::ENABLE | DmaController::REQ);
        const unsigned words = 1u << burst;              // a take: 2^BURST words
        for (unsigned take = 0; take < 4 / words; ++take)
            CHECK_EQ(r.cpu_cycle(), 1u + 2 * words);     // the CPU gets its cycle between
        CHECK_EQ(r.cpu_cycle(), 0u);                     // done
        CHECK_EQ(differ(r.ram, 0x1000, 0x2000, 8), size_t{0});
    }
}

TEST(the_observer_sees_one_cycle_per_word)
{
    // An emulator-model check, as test_decoder's: the Console card on the board sees one bus cycle a word (the
    // owner's Q7), so the observer must too. Must fail if the decoder reports each byte (32).
    struct Watch : CycleObserver
    {
        void on_cycle(const BusCycle &c) override { seen.push_back(c); }
        std::vector<BusCycle> seen;
    };
    Rig r;
    Watch watch;
    r.bus.set_dma_observer(&watch);
    fill(r.ram, 0x1000, 16, 4);
    r.on();
    r.card_of(3, WIDE_NONE);
    r.channel(3, 0x1000, 16, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 16);
    REQUIRE(watch.seen.size() == 16u);
    unsigned reads = 0, writes = 0;
    for (size_t i = 0; i < watch.seen.size(); ++i)
    {
        const BusCycle &c = watch.seen[i];
        CHECK(c.word);
        CHECK_EQ(c.addr & 1, 0);
        CHECK_EQ(c.addr, (c.write ? 0x2000u : 0x1000u) + 2 * (i / 2));
        CHECK_EQ(c.data, r.ram.peek(c.addr));
        CHECK_EQ(c.data_high, r.ram.peek(c.addr + 1u));
        (c.write ? writes : reads) += 1;
    }
    CHECK_EQ(reads, 8u);
    CHECK_EQ(writes, 8u);
}

TEST(wide_with_no_card_floats_ffff_a_word)
{
    // A block started by REQ on a channel serving no card moves the floating bus: $FF on both lanes, a word a
    // cycle. Must fail with the float driven on D0-D7 only.
    Rig r;
    r.on();
    r.card_of(3, WIDE_NONE);
    r.channel(3, 0x6000, 8, DmaController::BLOCK | DmaController::TO_MEMORY);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 1u + 4);
    for (uint32_t i = 0; i < 8; ++i)
        CHECK_EQ(r.ram.peek(0x6000 + i), 0xFF);
    CHECK_EQ(r.ram.peek(0x6008), 0x00);
    CHECK_EQ(r.bus.peek(STATUS), 0x08);
}

/// 6502 code sets up channel 3 to copy 64 KB from physical $20000 to $30000
/// with an interrupt at the end: it writes `control` to CONTROL with SELECT 3
/// first, then `card` to CARD if it is given (the default writes nothing, so
/// the program is byte for byte what it was before B4), then the window, then
/// waits in WAI until the interrupt wakes it. `woke` = the cycle it woke on
/// (0: never).
static void copy_64k_on_the_backplane(uint8_t control, uint64_t &woke, int card = -1)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\n"
                            "northbridge backplane bootsel\n"
                            "ram 00000-FFFFF name=ram\n"
                            "dma 400-47F space=io name=dma irq\n",
                            "dma.machine", {}, error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    Device *ram = m->find("ram");
    REQUIRE(ram != nullptr);
    for (uint32_t i = 0; i < 0x10000; ++i)
        ram->write(0x20000 + i, static_cast<uint8_t>(i * 7));
    const uint8_t sel3 = static_cast<uint8_t>(control | 3 << DmaController::SELECT_SHIFT);
    std::vector<uint8_t> prog = {
        0xA9, sel3, 0x8D, 0x7E, 0xC4, // LDA #control|SELECT 3; STA CONTROL
    };
    if (card >= 0) // LDA #card; STA CARD
        prog.insert(prog.end(), {0xA9, static_cast<uint8_t>(card), 0x8D, 0x0C, 0xC4});
    prog.insert(prog.end(), {
        0xA9, 0x00, 0x8D, 0x00, 0xC4, // LDA #$00; STA ADDR lo
        0x8D, 0x01, 0xC4,             //           STA ADDR mid
        0xA9, 0x02, 0x8D, 0x02, 0xC4, // LDA #$02; STA ADDR hi     source $20000
        0xA9, 0x00, 0x8D, 0x08, 0xC4, // LDA #$00; STA DEST lo
        0x8D, 0x09, 0xC4,             //           STA DEST mid
        0xA9, 0x03, 0x8D, 0x0A, 0xC4, // LDA #$03; STA DEST hi     dest $30000
        0xA9, 0x00, 0x8D, 0x04, 0xC4, // LDA #$00; STA COUNT lo
        0x8D, 0x05, 0xC4,             //           STA COUNT hi    0 = 65,536
        0xA9, 0x88, 0x8D, 0x06, 0xC4, // LDA #$88; STA MODE        memory to memory, IRQ
        0xA9, 0x03, 0x8D, 0x07, 0xC4, // LDA #$03; STA CTRL        ENABLE + REQ
        0xCB,                         // WAI
        0xAD, 0x89, 0xC6,             // LDA IRQH: bit 0 low = the DMA (module 8)
        0x85, 0x10,                   // STA $10
        0xDB,                         // STP
    });
    for (size_t i = 0; i < prog.size(); ++i)
        bus.poke(static_cast<uint16_t>(0x0200 + i), prog[i]);
    bus.reset();
    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    Registers regs;
    regs.pc = 0x0200;
    regs.p = 0x24;                    // I set: the IRQ only ends the WAI
    cpu.set_regs(regs);
    woke = 0;
    for (int i = 0; i < 1'000'000 && cpu.state() != Cpu6502::State::Stopped; ++i)
    {
        cpu.step();
        if (bus.irq())
        {
            if (!woke)
                woke = cpu.cycles();
            cpu.irq();
        }
    }
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);
    size_t differ = 0;
    for (uint32_t i = 0; i < 0x10000; ++i)
        differ += ram->peek(0x30000 + i) != static_cast<uint8_t>(i * 7);
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(bus.peek(0x0010), 0xFE);  // IRQH: only bit 0 low
    CHECK_EQ(bus.now(), cpu.cycles()); // the CPU's clock counted every DMA cycle
}

TEST(a_64k_copy_on_the_backplane_takes_18_ms_and_interrupts)
{
    // No burst limit: it all goes on the WAI's opcode fetch. 1 cycle to take
    // the bus + 2 a byte = 131,073 cycles, 18.3 ms at 7.16 MHz.
    uint64_t woke = 0;
    copy_64k_on_the_backplane(0x71, woke);
    CHECK(woke > 131073 && woke < 131073 + 200);
}

TEST(a_one_byte_burst_copy_runs_while_the_cpu_waits)
{
    // BURST 0: a byte per take, so after the WAI the copy runs on the idle
    // cycles: 3 stolen (take + read + write) + the idle cycle = 4 a byte.
    uint64_t woke = 0;
    copy_64k_on_the_backplane(0x01, woke);
    CHECK(woke > 4 * 65536 - 200 && woke < 4 * 65536 + 200);
}

TEST(a_wide_64k_copy_on_the_backplane_takes_9_ms)
{
    // As the 18 ms copy, with CARD $18 (WIDE, no module) on channel 3: 1 cycle to take the bus + 2 a word =
    // 65,537 cycles, 9.15 ms at 7.16 MHz, the data identical and IRQH $FE (the helper checks both). Must fail with
    // WIDE ignored (131,073).
    uint64_t woke = 0;
    copy_64k_on_the_backplane(0x71, woke, DmaController::WIDE | DmaController::CARD_NONE);
    std::printf("  the WIDE 64 KB copy woke the CPU at cycle %llu\n", static_cast<unsigned long long>(woke));
    CHECK(woke > 65537 && woke < 65537 + 200);
}

int main()
{
    return run_all();
}
