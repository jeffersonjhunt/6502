/**
 * @file test_console_card.cpp
 * @brief The Console card (hw/design.md, "Front Panel", "Console registers").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card on a backplane machine with /BOOTSEL, so W3 is RAM block 63:
 * code at CPU $C800 is at physical $FC800, and a breakpoint has to compare
 * all 20 address lines to tell it from $0C800. A small run loop does what
 * the app's does: ask the card before each instruction, then IRQ, NMI and
 * START after it.
 */

#include "harness.h"

#include "console_card.h"
#include "cpu6502.h"
#include "machine.h"

#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t CARD = 0xC700;
    constexpr uint16_t IRQH = 0xC689; // the north bridge's interrupt sources, active low
    constexpr uint16_t CODE = 0xC800; // physical $FC800
    constexpr uint32_t CODE_PHYS = 0xFC800;

    struct Panel
    {
        Panel()
        {
            std::string error;
            machine = Machine::build("cpu w65c02s\n"
                                     "northbridge backplane bootsel\n"
                                     "ram 00000-FFFFF module=0\n"
                                     "console 700-77F space=io name=console irq\n",
                                     "panel.machine", {}, error);
            if (!machine)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            card = machine->console_card();
            cpu = std::make_unique<Cpu6502>(machine->bus(), Cpu6502::Model::W65C02S);
            cpu->set_observer(card);
        }

        bool ok() const { return card != nullptr; }
        Decoder &bus() { return machine->bus(); }
        uint8_t reg(uint8_t r) { return bus().peek(static_cast<uint16_t>(CARD + r)); }
        void set(uint8_t r, uint8_t v) { bus().poke(static_cast<uint16_t>(CARD + r), v); }
        uint32_t reg20(uint8_t r)
        {
            return reg(r) | static_cast<uint32_t>(reg(static_cast<uint8_t>(r + 1))) << 8 |
                   static_cast<uint32_t>(reg(static_cast<uint8_t>(r + 2))) << 16;
        }
        void set20(uint8_t r, uint32_t v)
        {
            for (uint8_t i = 0; i < 3; ++i)
                set(static_cast<uint8_t>(r + i), static_cast<uint8_t>(v >> (8 * i)));
        }

        /// Load `code` at CODE, the vectors at $FFFA-$FFFF, and reset.
        void boot(const std::vector<uint8_t> &code, uint16_t nmi = CODE, uint16_t irq = CODE)
        {
            for (size_t i = 0; i < code.size(); ++i)
                bus().poke(static_cast<uint16_t>(CODE + i), code[i]);
            const uint16_t vectors[] = {nmi, CODE, irq};
            for (unsigned i = 0; i < 3; ++i)
            {
                bus().poke(static_cast<uint16_t>(0xFFFA + 2 * i), static_cast<uint8_t>(vectors[i]));
                bus().poke(static_cast<uint16_t>(0xFFFB + 2 * i), static_cast<uint8_t>(vectors[i] >> 8));
            }
            bus().reset();
            cpu->reset();
        }

        /// Up to `max` instructions, as the app's run loop.
        /// @return instructions run before the card stopped it (or `max`)
        unsigned run(unsigned max)
        {
            for (unsigned i = 0; i < max; ++i)
            {
                if (!card->may_run(cpu->regs().pc))
                    return i;
                cpu->step();
                if (bus().irq())
                    cpu->irq();
                if (card->take_nmi())
                    cpu->nmi();
                if (card->take_start())
                {
                    bus().reset();
                    cpu->reset();
                }
            }
            return max;
        }
        uint16_t pc() const { return cpu->regs().pc; }

        std::unique_ptr<Machine> machine;
        ConsoleCard *card{nullptr};
        std::unique_ptr<Cpu6502> cpu;
    };

    // INX; INX; INX; JMP CODE: four instructions, 9 cycles a lap
    const std::vector<uint8_t> LOOP = {0xE8, 0xE8, 0xE8, 0x4C, 0x00, 0xC8};
} // namespace

TEST(registers_read_back_and_reset)
{
    Panel p;
    REQUIRE(p.ok());
    CHECK_EQ(p.reg(ConsoleCard::ID_REG), 0xE1);
    p.card->set_switches(0xABCDEF);                  // 24 bits in: only A0-A19 exist
    CHECK_EQ(p.reg20(ConsoleCard::SWR), 0xBCDEFu);
    CHECK_EQ(p.reg(ConsoleCard::PANEL), 0x00);
    p.card->set_halt_switch(true);
    p.card->set_bus_cycle_switch(true);
    CHECK_EQ(p.reg(ConsoleCard::PANEL), ConsoleCard::HALT_SWITCH | ConsoleCard::BUS_CYCLE);
    p.set(ConsoleCard::CTRL, 0xFF);
    CHECK_EQ(p.reg(ConsoleCard::CTRL), 0x0D);        // HALT, LOCK, DISPLAY select
    p.set20(ConsoleCard::BPADDR, 0xFFFFFF);
    CHECK_EQ(p.reg20(ConsoleCard::BPADDR), 0xFFFFFu);
    p.set(ConsoleCard::BPCTRL, 0xFF);
    CHECK_EQ(p.reg(ConsoleCard::BPCTRL), 0x1F);
    p.set20(ConsoleCard::PADDR, 0x12345);
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0x12345u);
    p.set(ConsoleCard::SWR, 0x00);                   // read-only
    CHECK_EQ(p.reg(ConsoleCard::SWR), 0xEF);
    CHECK_EQ(p.reg(ConsoleCard::DISPLAY), 0xFF);     // write-only
    CHECK_EQ(p.reg(0x40), 0xFF);                     // unused
}

TEST(keys_clear_on_read_interrupt_and_lock)
{
    Panel p;
    REQUIRE(p.ok());
    p.card->press(ConsoleCard::LOAD_ADRS);
    CHECK_EQ(p.reg(ConsoleCard::KEYS), ConsoleCard::LOAD_ADRS); // peek leaves it
    CHECK_EQ(p.reg(ConsoleCard::STATUS) & ConsoleCard::KEY_PRESSED, ConsoleCard::KEY_PRESSED);
    CHECK_EQ(p.bus().read(static_cast<uint16_t>(CARD + ConsoleCard::KEYS)), ConsoleCard::LOAD_ADRS);
    CHECK_EQ(p.reg(ConsoleCard::KEYS), 0x00);                   // a CPU read clears it
    CHECK(!p.bus().irq());                                       // IEN off: no interrupt

    p.set(ConsoleCard::IEN, 0x01);
    p.card->press(ConsoleCard::EXAM);
    CHECK(p.bus().irq());
    CHECK_EQ(p.reg(ConsoleCard::ISR), 0x01);
    CHECK_EQ(p.bus().peek(IRQH), 0xBF);                          // bit 6: module E
    p.set(ConsoleCard::ISR, 0x01);                               // write 1 to clear
    CHECK(!p.bus().irq());

    // LOCK: the panel's keys and HALT switch do nothing; the remote panel still works.
    p.card->set_switches(0x00042);
    p.set(ConsoleCard::CTRL, ConsoleCard::LOCK);
    p.card->press(ConsoleCard::LOAD_ADRS);
    p.card->set_halt_switch(true);
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0u);
    CHECK(!p.card->halted());
    p.set(ConsoleCard::KEYS, ConsoleCard::LOAD_ADRS);            // over SPI
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0x42u);
    p.set(ConsoleCard::CTRL, 0x00);
    p.card->set_lock_switch(true);                               // the keyswitch at LOCK
    CHECK(!p.card->halted());
    p.card->set_lock_switch(false);
    CHECK(p.card->halted());
}

TEST(load_adrs_exam_and_dep_reach_physical_memory)
{
    Panel p;
    REQUIRE(p.ok());
    Decoder &bus = p.bus();
    bus.write_physical(0x12345, 0x11);
    bus.write_physical(0x12346, 0x22);
    p.card->set_switches(0x12345);
    p.card->press(ConsoleCard::LOAD_ADRS);
    p.card->press(ConsoleCard::EXAM);                 // not halted: EXAM does nothing
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0x12345u);

    p.card->set_halt_switch(true);
    p.card->press(ConsoleCard::EXAM);
    ConsoleCard::Lights l = p.card->lights();
    CHECK_EQ(l.data[0], 1.0f);                        // $11 on the data lights
    CHECK_EQ(l.data[4], 1.0f);
    CHECK_EQ(l.data[1], 0.0f);
    CHECK_EQ(l.addr[16], 1.0f);                       // $12345 on the address lights
    CHECK_EQ(l.addr[17], 0.0f);
    p.card->press(ConsoleCard::EXAM);                 // a second EXAM steps on
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0x12346u);
    CHECK_EQ(p.card->lights().data[1], 1.0f);         // $22

    p.card->set_switches(0x000A5);
    p.card->press(ConsoleCard::DEP);                  // after EXAM: the same address
    CHECK_EQ(bus.peek_physical(0x12346), 0xA5);
    p.card->set_switches(0x0005A);
    p.card->press(ConsoleCard::DEP);                  // a second DEP steps on
    CHECK_EQ(bus.peek_physical(0x12347), 0x5A);
    CHECK_EQ(p.reg20(ConsoleCard::PADDR), 0x12347u);
    CHECK_EQ(bus.peek_physical(0x12345), 0x11);       // nothing else touched
}

TEST(halt_switch_and_cont_step_one_instruction)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot(LOOP);
    CHECK_EQ(p.run(8), 8u);
    p.card->set_halt_switch(true);
    CHECK_EQ(p.run(8), 0u);
    CHECK_EQ(p.reg(ConsoleCard::STATUS) & ConsoleCard::HALTED, ConsoleCard::HALTED);
    const uint16_t at = p.pc();
    const uint64_t cycles = p.cpu->cycles();
    CHECK_EQ(p.run(8), 0u);
    CHECK_EQ(p.cpu->cycles(), cycles);                // halted: nothing runs

    p.card->press(ConsoleCard::CONT);                 // S INST: exactly one
    CHECK_EQ(p.run(8), 1u);
    CHECK(p.pc() != at);
    CHECK(p.card->halted());
    p.card->press(ConsoleCard::CONT);
    CHECK_EQ(p.run(8), 1u);

    p.card->set_halt_switch(false);                   // ENABLE: CONT runs on
    CHECK_EQ(p.run(8), 8u);
    p.card->press(ConsoleCard::CONT);                 // CONT while running: nothing
    CHECK_EQ(p.run(8), 8u);
}

TEST(a_program_halts_itself_and_cont_resumes)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot({0xA9, 0x01, 0x8D, 0x06, 0xC7, // LDA #$01; STA CTRL: HALT
            0xE8, 0x4C, 0x05, 0xC8});     // INX; JMP *-1
    CHECK_EQ(p.run(10), 2u);                          // stops after the STA
    CHECK_EQ(p.pc(), CODE + 5);
    CHECK_EQ(p.reg20(ConsoleCard::LADDR), 0x0C706u);  // the STA's write: in the I/O hole, the CPU address
    CHECK_EQ(p.reg(ConsoleCard::LDATA), 0x01);
    CHECK_EQ(p.reg(ConsoleCard::LSTAT), 0x00);        // a write: RWB low, no SYNC
    p.card->press(ConsoleCard::CONT);
    CHECK_EQ(p.reg(ConsoleCard::CTRL), 0x00);
    CHECK_EQ(p.run(10), 10u);
}

TEST(a_fetch_breakpoint_halts_before_the_instruction)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot(LOOP);
    p.set20(ConsoleCard::BPADDR, 0x0C802);            // A16-A19 wrong: never matches
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    CHECK_EQ(p.run(20), 20u);

    p.set20(ConsoleCard::BPADDR, CODE_PHYS + 2);      // the third INX
    CHECK_EQ(p.run(20), 2u);                          // from CODE: two INX to get there
    CHECK_EQ(p.pc(), CODE + 2);
    CHECK_EQ(p.reg(ConsoleCard::STATUS), ConsoleCard::HALTED | ConsoleCard::HIT);
    CHECK_EQ(p.reg20(ConsoleCard::LADDR), CODE_PHYS + 2);
    CHECK_EQ(p.reg(ConsoleCard::LDATA), 0xE8);
    CHECK_EQ(p.reg(ConsoleCard::LSTAT), ConsoleCard::L_SYNC | ConsoleCard::L_RWB);

    p.card->press(ConsoleCard::CONT);                 // runs on, round the loop, and back
    CHECK_EQ(p.run(20), 4u);
    CHECK_EQ(p.pc(), CODE + 2);
    p.set(ConsoleCard::STATUS, ConsoleCard::HIT);     // hit stays until cleared
    CHECK_EQ(p.reg(ConsoleCard::STATUS), ConsoleCard::HALTED | ConsoleCard::KEY_PRESSED); // CONT is unread

    p.card->set_halt_switch(true);                    // CONT on the breakpoint with HALT down: one step
    p.card->press(ConsoleCard::CONT);
    CHECK_EQ(p.run(20), 1u);
    CHECK_EQ(p.pc(), CODE + 3);
}

TEST(read_and_write_breakpoints_latch_their_cycle)
{
    Panel p;
    REQUIRE(p.ok());
    p.bus().poke(0x1234, 0x77);
    p.boot({0xAD, 0x34, 0x12,       // LDA $1234
            0x8D, 0x45, 0x23,       // STA $2345
            0xE8,                   // INX
            0x4C, 0x00, 0xC8});     // JMP CODE

    // A read breakpoint on code is not a fetch breakpoint: fetches do not match it.
    p.set20(ConsoleCard::BPADDR, CODE_PHYS + 6);
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ);
    CHECK_EQ(p.run(12), 12u);

    p.boot({0xAD, 0x34, 0x12, 0x8D, 0x45, 0x23, 0xE8, 0x4C, 0x00, 0xC8});
    p.set20(ConsoleCard::BPADDR, 0x12345);            // A0-A15 match the STA's, A16-A19 do not
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_WRITE);
    CHECK_EQ(p.run(12), 12u);

    p.boot({0xAD, 0x34, 0x12, 0x8D, 0x45, 0x23, 0xE8, 0x4C, 0x00, 0xC8});
    p.set20(ConsoleCard::BPADDR, 0x02345);            // W0 is RAM block 0
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_WRITE);
    CHECK_EQ(p.run(12), 2u);                          // stops after the STA, not in it
    CHECK_EQ(p.pc(), CODE + 6);
    CHECK_EQ(p.reg20(ConsoleCard::LADDR), 0x02345u);
    CHECK_EQ(p.reg(ConsoleCard::LDATA), 0x77);
    CHECK_EQ(p.reg(ConsoleCard::LSTAT), 0x00);
    CHECK_EQ(p.card->lights().addr[13], 1.0f);        // the lights froze on that cycle
    p.card->press(ConsoleCard::CONT);

    p.set20(ConsoleCard::BPADDR, 0x01234);
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ);
    CHECK_EQ(p.run(12), 3u);                          // INX, JMP, then the LDA
    CHECK_EQ(p.pc(), CODE + 3);
    CHECK_EQ(p.reg20(ConsoleCard::LADDR), 0x01234u);
    CHECK_EQ(p.reg(ConsoleCard::LSTAT), ConsoleCard::L_RWB);
}

TEST(the_latch_keeps_the_matching_cycle_not_the_last)
{
    // INC $2345 reads $41, writes $41 back (the 65C02's dummy cycle is a
    // read), then writes $42: a read breakpoint keeps the read.
    Panel p;
    REQUIRE(p.ok());
    p.bus().poke(0x2345, 0x41);
    p.boot({0xEE, 0x45, 0x23,       // INC $2345
            0x4C, 0x03, 0xC8});     // JMP *
    p.set20(ConsoleCard::BPADDR, 0x02345);
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ);
    CHECK_EQ(p.run(4), 1u);
    CHECK_EQ(p.bus().peek(0x2345), 0x42);             // the instruction finished
    CHECK_EQ(p.reg(ConsoleCard::LDATA), 0x41);
    CHECK_EQ(p.reg(ConsoleCard::LSTAT), ConsoleCard::L_RWB);
    CHECK_EQ(p.card->lights().data[0], 1.0f);         // and the lights show $41, not $42
}

TEST(a_breakpoint_can_pull_nmi_instead)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot({0x8D, 0x45, 0x23,       // STA $2345
            0x4C, 0x00, 0xC8,       // JMP CODE
            0xE6, 0x10,             // NMI: INC $10
            0x40},                  //      RTI
           CODE + 6);
    p.set20(ConsoleCard::BPADDR, 0x02345);
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_WRITE | ConsoleCard::BP_NMI);
    CHECK_EQ(p.run(30), 30u);                         // never halts
    CHECK(p.bus().peek(0x0010) >= 5);                 // once a lap: 4 instructions a lap with the handler
    CHECK_EQ(p.reg(ConsoleCard::STATUS), ConsoleCard::HIT);
    CHECK_EQ(p.reg20(ConsoleCard::LADDR), 0x02345u);
}

TEST(start_resets_the_machine_and_keeps_the_breakpoint)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot(LOOP);
    p.set(ConsoleCard::IEN, 0x01);
    p.set(ConsoleCard::CTRL, ConsoleCard::CTRL_HALT);
    p.set20(ConsoleCard::BPADDR, CODE_PHYS + 1);
    p.set(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    CHECK_EQ(p.run(4), 0u);
    p.card->press_start();
    CHECK(!p.card->halted());
    CHECK_EQ(p.reg(ConsoleCard::CTRL), 0x00);
    CHECK_EQ(p.reg(ConsoleCard::IEN), 0x00);
    CHECK(p.card->take_start());
    CHECK(!p.card->take_start());                     // once per press
    p.card->press_start();
    CHECK_EQ(p.run(4), 2u);                           // an INX, the reset, an INX, the breakpoint
    CHECK_EQ(p.pc(), CODE + 1);

    p.set(ConsoleCard::CTRL, ConsoleCard::LOCK);      // locked: START does nothing
    p.card->press_start();
    CHECK(!p.card->take_start());
}

TEST(lights_glow_with_the_bus_and_freeze_when_halted)
{
    Panel p;
    REQUIRE(p.ok());
    p.boot({0x4C, 0x00, 0xC8});                       // JMP CODE: 3 cycles, all at $FC80x
    p.run(10);
    CHECK_EQ(p.card->lights().addr[19], 0.0f);        // off: nothing accumulated
    p.card->set_lights(true);
    p.run(300);
    ConsoleCard::Lights l = p.card->lights();
    for (unsigned i = 14; i < 20; ++i)
        CHECK_EQ(l.addr[i], 1.0f);                    // A14-A19: $FC800-$FC802
    CHECK_EQ(l.addr[11], 1.0f);
    CHECK_EQ(l.addr[12], 0.0f);
    CHECK(l.addr[0] > 0.3f && l.addr[0] < 0.36f);     // $FC801: one cycle in three
    CHECK(l.sync > 0.3f && l.sync < 0.36f);           // the opcode fetch
    CHECK_EQ(l.rwb, 1.0f);                            // nothing but reads
    CHECK_EQ(l.vpb, 0.0f);
    CHECK_EQ(l.rdy, 0.0f);

    p.set(ConsoleCard::DISPLAY, 0x81);
    p.set(ConsoleCard::CTRL, ConsoleCard::SHOW_DISPLAY);
    p.run(30);
    l = p.card->lights();
    CHECK_EQ(l.data[7], 1.0f);
    CHECK_EQ(l.data[0], 1.0f);
    CHECK_EQ(l.data[1], 0.0f);
    p.set(ConsoleCard::CTRL, 0x00);

    p.card->set_halt_switch(true);
    p.run(1);
    l = p.card->lights();
    CHECK_EQ(l.addr[0] + l.addr[1], 1.0f);            // one address, not a blur: $FC801 or $FC802
    CHECK_EQ(l.rdy, 1.0f);                            // held
}

int main()
{
    return run_all();
}
