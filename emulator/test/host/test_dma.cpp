/**
 * @file test_dma.cpp
 * @brief The DMA controller (hw/design.md, "DMA registers") and cycle stealing.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The controller takes the bus before a CPU cycle, so these mostly watch
 * the decoder: each CPU access reports the cycles it was held for
 * (wait_states()), which is exactly the DMA's cycles: one to take the bus,
 * then one per fly-by byte or two per memory-to-memory byte. A test card
 * stands in for the cards that will request DMA (none is modelled yet).
 * The last cases run real 6502 code on the backplane machine.
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
        int want{-1}; ///< force DRQ: 1 on, 0 off, -1 = while the FIFO has data
    };

    /// 64 KB of RAM, the controller at $C400, and a card on DRQ0.
    struct Rig
    {
        Rig()
        {
            dma.attach(&bus);
            bus.set_master(&dma);
            bus.map({0x00000, 0x0FFFF, &ram});
            bus.map({DMA, DMA + 0x7F, &dma, DmaController::MODULE, true});
            bus.map({0x0D000, 0x0D003, &card, 0x7, false, 0, 0});
        }

        void channel(unsigned n, uint32_t addr, uint32_t count, uint8_t mode, uint32_t dest = 0)
        {
            const uint16_t base = static_cast<uint16_t>(DMA + n * 0x10);
            for (unsigned i = 0; i < 3; ++i)
            {
                bus.poke(static_cast<uint16_t>(base + i), static_cast<uint8_t>(addr >> (8 * i)));
                bus.poke(static_cast<uint16_t>(base + 8 + i), static_cast<uint8_t>(dest >> (8 * i)));
            }
            bus.poke(static_cast<uint16_t>(base + 4), static_cast<uint8_t>(count));
            bus.poke(static_cast<uint16_t>(base + 5), static_cast<uint8_t>(count >> 8));
            bus.poke(static_cast<uint16_t>(base + 6), mode);
        }
        void arm(unsigned n, uint8_t ctrl = DmaController::ENABLE)
        {
            bus.poke(static_cast<uint16_t>(DMA + n * 0x10 + 7), ctrl);
        }
        void on(uint8_t control = DmaController::CONTROL_RESET | DmaController::MASTER_ENABLE)
        {
            bus.poke(DMA + DmaController::CONTROL, control);
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
    CHECK_EQ(r.bus.peek(DMA + 0x10), 0xDE);
    CHECK_EQ(r.bus.peek(DMA + 0x12), 0x0A);   // 20 bits: A16-A19 only
    CHECK_EQ(r.bus.peek(DMA + 0x14), 0x34);
    CHECK_EQ(r.bus.peek(DMA + 0x15), 0x12);
    CHECK_EQ(r.bus.peek(DMA + 0x16), 0x5A);
    CHECK_EQ(r.bus.peek(DMA + 0x1A), 0x0F);
    CHECK_EQ(r.bus.peek(DMA + DmaController::CONTROL), 0x70);
    CHECK_EQ(r.bus.peek(DMA + DmaController::ID_REG), 0x81);
    CHECK_EQ(r.bus.peek(DMA + DmaController::ABORT), 0xFF);
    CHECK_EQ(r.bus.peek(DMA + 0x50), 0xFF);
    r.card.fifo = {1};
    CHECK_EQ(r.bus.peek(DMA + DmaController::STATUS), 0x10); // DRQ0 is up
    r.dma.reset();
    CHECK_EQ(r.bus.peek(DMA + 0x10), 0x00);
}

TEST(nothing_moves_until_the_master_enable)
{
    Rig r;
    r.channel(3, 0x1000, 4, DmaController::MEM_TO_MEM, 0x2000);
    r.arm(3, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 0u);               // CONTROL resets to $70: off
    r.on();
    CHECK_EQ(r.cpu_cycle(), 1u + 2 * 4);       // take the bus, then 2 cycles a byte
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
    CHECK_EQ(r.bus.peek(DMA + 0x37), 0x00);    // ENABLE and REQ clear at terminal count
    CHECK_EQ(r.bus.peek(DMA + DmaController::STATUS), 0x08);

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
    CHECK_EQ(r.bus.peek(DMA + 0x20), 0x0B);    // ADDR read back: the current address
}

TEST(single_mode_gives_the_cpu_a_cycle_after_each_byte)
{
    Rig r;
    r.on();
    r.card.fifo = {0x11, 0x22, 0x33};
    r.channel(0, 0x5000, 3, DmaController::SINGLE | DmaController::TO_MEMORY);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 2u);               // take the bus + one fly-by byte
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 2u);
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
    CHECK_EQ(r.cpu_cycle(), 1u + 5);           // until the card lets go of DRQ
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.bus.peek(DMA + 0x04), 95);      // COUNT left
    CHECK_EQ(r.bus.peek(DMA + 0x07), DmaController::ENABLE); // still armed

    r.on(0x10 | DmaController::MASTER_ENABLE); // BURST 1: two bytes, then a CPU cycle
    r.card.fifo = {6, 7, 8, 9, 10};
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 3u);
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.ram.read(0x5009), 10);
}

TEST(block_mode_finishes_the_count_once_started)
{
    Rig r;
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.card.want = 0;                           // the card never raises DRQ: REQ starts it
    r.channel(1, 0x6000, 3, DmaController::BLOCK | DmaController::TO_CARD);
    r.bus.map({0x0D010, 0x0D013, &r.card, 0x7, false, 0, 1}); // the card on DRQ1 too
    for (uint32_t i = 0; i < 3; ++i)
        r.ram.write(0x6000 + i, static_cast<uint8_t>(0xA0 + i));
    r.arm(1, DmaController::ENABLE | DmaController::REQ);
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 2u);               // carries on without DRQ or REQ
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK(r.card.sent == (std::vector<uint8_t>{0xA0, 0xA1, 0xA2}));
    CHECK(r.card.tcs.back());
}

TEST(block_mode_started_by_drq_carries_on_after_it_drops)
{
    Rig r;
    r.on(0x00 | DmaController::MASTER_ENABLE); // BURST 0: one byte per take
    r.card.fifo = {0x51};                      // DRQ for one byte only
    r.channel(0, 0x6100, 3, DmaController::BLOCK | DmaController::TO_MEMORY);
    r.arm(0);                                  // no REQ: the card's DRQ starts it
    CHECK_EQ(r.cpu_cycle(), 2u);
    CHECK(!r.card.drq());
    CHECK_EQ(r.cpu_cycle(), 2u);               // a block, once started, runs to the count
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
    CHECK_EQ(r.bus.peek(DMA + DmaController::STATUS) & 0x0F, 0x01);
    CHECK(!r.bus.irq());                       // no MODE.IRQ: status only
    r.bus.write(DMA + DmaController::STATUS, 0x01);
    CHECK_EQ(r.bus.peek(DMA + DmaController::STATUS) & 0x0F, 0x00);

    r.card.fifo = {1, 2, 3, 4, 5};
    r.channel(0, 0x5100, 2, DmaController::DEMAND | DmaController::TO_MEMORY | DmaController::AUTO |
                                DmaController::IRQ);
    r.arm(0);
    CHECK_EQ(r.cpu_cycle(), 3u);               // stops at terminal count, even with DRQ up
    CHECK(r.bus.irq());
    CHECK_EQ(r.bus.peek(DMA + 0x07), DmaController::ENABLE); // AUTO: still armed
    CHECK_EQ(r.bus.peek(DMA + 0x00), 0x00);    // and back at the start
    CHECK_EQ(r.bus.peek(DMA + 0x04), 2);       // with the count reloaded
    CHECK_EQ(r.cpu_cycle(), 3u);               // a ring buffer: the same 2 bytes again
    CHECK_EQ(r.ram.read(0x5100), 3);
    CHECK(r.card.drq());                       // the 5th byte waits: TC stopped it
    r.bus.write(DMA + DmaController::STATUS, 0x01);
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
    CHECK_EQ(r.cpu_cycle(), 2u);               // channel 0's fly-by byte first
    CHECK_EQ(r.ram.read(0x5000), 9);
    CHECK_EQ(r.cpu_cycle(), 3u);               // then channel 3, one byte a take
    r.bus.write(DMA + DmaController::ABORT, 0x08);
    CHECK_EQ(r.cpu_cycle(), 0u);
    CHECK_EQ(r.bus.peek(DMA + 0x34), 2);       // COUNT kept where it stopped
    CHECK_EQ(r.bus.peek(DMA + 0x30), 0x02);
    CHECK_EQ(r.bus.peek(DMA + 0x37), 0x00);
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
/// with an interrupt at the end, writes `control` to CONTROL, then waits in
/// WAI until the interrupt wakes it. `woke` = the cycle it woke on (0: never).
static void copy_64k_on_the_backplane(uint8_t control, uint64_t &woke)
{
    std::string error;
    auto m = Machine::build("cpu w65c02s\n"
                            "northbridge backplane bootsel\n"
                            "ram 00000-FFFFF module=0\n"
                            "dma 400-47F space=io name=dma irq\n",
                            "dma.machine", {}, error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    Device *ram = nullptr;
    for (const Mapping &map : bus.mappings())
        if (map.module == 0x0)
            ram = map.device;
    REQUIRE(ram != nullptr);
    for (uint32_t i = 0; i < 0x10000; ++i)
        ram->write(0x20000 + i, static_cast<uint8_t>(i * 7));
    const std::vector<uint8_t> prog = {
        0xA9, 0x00, 0x8D, 0x30, 0xC4, // LDA #$00; STA ADDR3 lo
        0x8D, 0x31, 0xC4,             //           STA ADDR3 mid
        0xA9, 0x02, 0x8D, 0x32, 0xC4, // LDA #$02; STA ADDR3 hi    source $20000
        0xA9, 0x00, 0x8D, 0x38, 0xC4, // LDA #$00; STA DEST3 lo
        0x8D, 0x39, 0xC4,             //           STA DEST3 mid
        0xA9, 0x03, 0x8D, 0x3A, 0xC4, // LDA #$03; STA DEST3 hi    dest $30000
        0xA9, 0x00, 0x8D, 0x34, 0xC4, // LDA #$00; STA COUNT3 lo
        0x8D, 0x35, 0xC4,             //           STA COUNT3 hi   0 = 65,536
        0xA9, 0x88, 0x8D, 0x36, 0xC4, // LDA #$88; STA MODE3       memory to memory, IRQ
        0xA9, control, 0x8D, 0x41, 0xC4, // LDA #control; STA CONTROL
        0xA9, 0x03, 0x8D, 0x37, 0xC4, // LDA #$03; STA CTRL3       ENABLE + REQ
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
