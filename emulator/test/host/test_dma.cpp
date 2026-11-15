/**
 * @file test_dma.cpp
 * @brief The DMA controller, revision 2 (hw/design.md, "The DMA controller, revision 2"), and cycle stealing.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The controller takes the bus before a CPU cycle, so these mostly watch
 * the decoder: each CPU access reports the cycles it was held for
 * (wait_states()), which is exactly the DMA's cycles: one to take the bus,
 * one to poll the cards when /DREQ is low and a channel serving a card is
 * armed, then one per fly-by byte or two per memory-to-memory byte. Test
 * cards, wired to /DREQ and answering by their module IDs, stand in for the
 * modelled ones. The last cases run real 6502 code on the backplane machine.
 */

#include "harness.h"

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
            const uint8_t v = fifo.empty() ? 0xEE : fifo.front();
            if (!fifo.empty())
                fifo.pop_front();
            return v;
        }
        void dack_write(uint8_t data, bool tc) override
        {
            tcs.push_back(tc);
            sent.push_back(data);
        }
        std::deque<uint8_t> fifo;
        std::vector<uint8_t> sent;
        std::vector<bool> tcs;
        int want{-1}; ///< force the request: 1 on, 0 off, -1 = while the FIFO has data
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
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
        r.channel(n, 0x10203 * (n + 1), 0x0102 * (n + 1), static_cast<uint8_t>(0x40 + n), 0x30405 * (n + 1));
    r.arm(2, DmaController::ENABLE);
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
        for (uint8_t reg = 0; reg < 0x10; ++reg)
            CHECK_EQ(r.dma.peek_channel(n, reg), r.at(n, reg));
    r.select(2);
    const uint64_t before = r.bus.now();
    CHECK_EQ(r.dma.peek_channel(1, DmaController::ADDR), 0x06);   // $20406's low byte
    CHECK_EQ(r.dma.peek_channel(3, DmaController::MODE), 0x43);
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CTRL), DmaController::ENABLE);
    CHECK_EQ(r.dma.peek_channel(0, DmaController::CTRL), 0x00);
    CHECK_EQ(r.dma.peek_channel(1, DmaController::CARD), 0x09);
    CHECK_EQ(r.dma.peek_channel(DmaController::LIVE, DmaController::CTRL), 0xFF);     // not fitted until B3
    CHECK_EQ(r.dma.peek_channel(DmaController::CHANNELS, DmaController::CTRL), 0xFF); // no such channel
    CHECK_EQ(r.dma.peek_channel(0, DmaController::ADDR + 3), 0xFF);                   // reserved
    CHECK_EQ(r.dma.peek_channel(0, 0x10), 0xFF);                                      // nor register
    CHECK_EQ(r.bus.now(), before);
    CHECK_EQ(r.bus.peek(CONTROL) & DmaController::SELECT_MASK, 2 << DmaController::SELECT_SHIFT); // SELECT kept
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CTRL), DmaController::ENABLE); // still armed
}

TEST(the_window_follows_select_and_select_4_to_7_read_ff)
{
    // Must fail if the window decodes channel 0 whatever SELECT says (every driver would program channel 0).
    Rig r;
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
        r.channel(n, 0x111111 * (n + 1), 0x100 + n, static_cast<uint8_t>(n), 0x0A0B0C + n);
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
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
    for (unsigned n = DmaController::LIVE; n < DmaController::CHANNELS; ++n)
    {
        r.select(n);
        for (uint8_t reg = 0; reg < 0x10; ++reg)
        {
            r.bus.write(static_cast<uint16_t>(DMA + reg), 0x00); // dropped
            CHECK_EQ(r.bus.peek(static_cast<uint16_t>(DMA + reg)), 0xFF);
        }
    }
    for (unsigned n = 0; n < DmaController::LIVE; ++n) // and the writes reached no live channel
    {
        CHECK_EQ(r.dma.peek_channel(n, DmaController::ADDR), static_cast<uint8_t>(0x11 * (n + 1)));
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
    }
    r.bus.write(CONTROL, 0xFE);               // bit 7 is free: it reads 0
    CHECK_EQ(r.bus.peek(CONTROL), 0x7E);
    for (uint16_t a = DMA + 0x13; a < CONTROL; ++a)
        CHECK_EQ(r.bus.peek(a), 0xFF);        // $13-$7D reserved
}

TEST(card_resets_to_todays_wiring)
{
    // Must fail if reset() leaves CARD alone: the SD boot would never be granted after a stray CARD.
    Rig r;
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
    {
        r.card_of(n, 0xFF);
        CHECK_EQ(r.at(n, DmaController::CARD), 0x1F); // MODULE and WIDE; bits 7-5 read 0
    }
    r.card_of(0, DmaController::WIDE | 0x7);
    CHECK_EQ(r.at(0, DmaController::CARD), 0x17);     // WIDE stored and read back (it does nothing until B4)
    r.dma.reset();
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
        CHECK_EQ(r.dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
    CHECK_EQ(r.dma.peek_channel(0, DmaController::CARD), 0x07); // mass storage
    CHECK_EQ(r.dma.peek_channel(1, DmaController::CARD), 0x09); // video
    CHECK_EQ(r.dma.peek_channel(2, DmaController::CARD), 0x0A); // sound
    CHECK_EQ(r.dma.peek_channel(3, DmaController::CARD), 0x08); // none

    for (unsigned n = 0; n < DmaController::LIVE; ++n)
        r.card_of(n, 0x05);
    r.on(0x01);                                       // master on, BURST 0, SELECT 3 kept
    r.bus.pulse_reset();                              // Apple II mode's /RESET
    for (unsigned n = 0; n < DmaController::LIVE; ++n)
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

/// 6502 code sets up channel 3 to copy 64 KB from physical $20000 to $30000
/// with an interrupt at the end: it writes `control` to CONTROL with SELECT 3
/// first, then the window, then waits in WAI until the interrupt wakes it.
/// `woke` = the cycle it woke on (0: never).
static void copy_64k_on_the_backplane(uint8_t control, uint64_t &woke)
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
    const std::vector<uint8_t> prog = {
        0xA9, sel3, 0x8D, 0x7E, 0xC4, // LDA #control|SELECT 3; STA CONTROL
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
    };
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

int main()
{
    return run_all();
}
