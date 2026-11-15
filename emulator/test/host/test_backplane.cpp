/**
 * @file test_backplane.cpp
 * @brief The backplane's north bridge, and machines/backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md is the spec: the windows and their ROM and RO bits, VEC on
 * vector pulls, the I/O hole ahead of the windows, the registers at $C680
 * with their reset values, and the /IACK interrupt poll. The last case
 * runs the boot sequence the design describes, as real 6502 code on the
 * shipped machine file: the BIOS starts in flash, copies itself into RAM
 * block 63 through W2, points W3 at the copy and carries on with no jump.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "cpu6502.h"
#include "decoder.h"
#include "machine.h"
#include "memory_devices.h"
#include "pia6821.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t NB = 0xC680; ///< the north bridge's registers

    /// The bridge on a decoder with 1 MB of RAM, 1 MB of flash (ROM A and B)
    /// and its own registers, as backplane.machine wires it.
    struct Rig
    {
        explicit Rig(bool bootsel = false) : bridge(bootsel), bus(&bridge)
        {
            bridge.attach(&bus);
            bus.map({0x00000, 0xFFFFF, &ram, 0x0});
            bus.map({bus_address(Space::Flash, 0x00000), bus_address(Space::Flash, 0x7FFFF), &rom_a, 0x4});
            bus.map({bus_address(Space::Flash, 0x80000), bus_address(Space::Flash, 0xFFFFF), &rom_b, 0x5});
            bus.map({bus_address(Space::Io, BackplaneBridge::REGISTERS),
                     bus_address(Space::Io, BackplaneBridge::REGISTERS + 0x7F), &bridge, BackplaneBridge::MODULE});
            bus.reset();
        }

        BackplaneBridge bridge;
        Decoder bus;
        Ram ram{0x100000};
        Rom rom_a{0x80000}, rom_b{0x80000};
    };

    /// Records the module ID and physical address of every opcode fetch.
    struct Fetches : CycleObserver
    {
        explicit Fetches(const Decoder &d) : bus(d) {}
        void on_cycle(const BusCycle &c) override
        {
            if (c.sync)
                seen.push_back({c.addr, bus.last_access()});
        }
        struct Fetch
        {
            uint16_t cpu;
            Access access;
        };
        const Decoder &bus;
        std::vector<Fetch> seen;
    };
} // namespace

TEST(reset_maps_ram_blocks_0_to_2_and_the_top_of_rom_a)
{
    Rig r;
    CHECK_EQ(r.bridge.window(0), 0x00);
    CHECK_EQ(r.bridge.window(1), 0x01);
    CHECK_EQ(r.bridge.window(2), 0x02);
    CHECK_EQ(r.bridge.window(3), 0xDF);
    CHECK_EQ(r.bridge.vec(), 0xDF);
    CHECK_EQ(r.bus.route(0x1234).where, 0x01234u);
    CHECK_EQ(r.bus.route(0x5678).where, 0x05678u);
    CHECK_EQ(r.bus.route(0x9ABC).where, 0x09ABCu);
    const Route top = r.bus.route(0xFFFC);
    CHECK_EQ(top.where, bus_address(Space::Flash, 0x7FFFC));
    CHECK(!top.writable);
    CHECK_EQ(r.bus.module_at(0xE000), BackplaneBridge::MODULE); // memory by address: PS0-PS3 carry the bridge's D
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::ID_REG), 0xD1);
}

TEST(bootsel_starts_with_w3_and_vec_on_ram_block_63)
{
    Rig r(true);
    CHECK_EQ(r.bridge.window(3), 0x3F);
    CHECK_EQ(r.bridge.vec(), 0x3F);
    CHECK_EQ(r.bus.route(0xFFFC).where, 0xFFFFCu);
    CHECK(r.bus.read(NB + BackplaneBridge::STATUS) & BackplaneBridge::STATUS_BOOTSEL);
}

TEST(status_reports_power_on_until_the_next_reset)
{
    Rig r;
    const uint8_t first = r.bus.read(NB + BackplaneBridge::STATUS);
    CHECK(first & BackplaneBridge::STATUS_POWER_ON);
    CHECK(!(first & BackplaneBridge::STATUS_BOOTSEL));
    CHECK_EQ(first >> 4, BackplaneBridge::REVISION);
    r.bus.reset();
    CHECK(!(r.bus.read(NB + BackplaneBridge::STATUS) & BackplaneBridge::STATUS_POWER_ON));
}

TEST(any_block_appears_in_any_window)
{
    Rig r;
    r.bus.write(NB + 1, 0x3F);         // W1 = RAM block 63
    r.bus.write(0x4010, 0x5A);
    CHECK_EQ(r.ram.read(0xFC010), 0x5A);
    r.bus.write(NB + 0, 0x05);         // W0 = block 5: even zero page moves
    r.bus.write(0x0002, 0xC3);
    CHECK_EQ(r.ram.read(0x14002), 0xC3);
    CHECK_EQ(r.bus.read(NB + 0), 0x05); // window registers read back
    r.bus.write(NB + 0, 0x00);
    CHECK_EQ(r.bus.read(0x4010), 0x5A); // W1 still on block 63
}

TEST(read_only_window_drops_writes)
{
    Rig r;
    r.bus.write(NB + 2, 0x40 | 0x07);  // W2 = RAM block 7, read-only
    r.ram.write(0x1C000, 0x11);
    r.bus.write(0x8000, 0x99);
    CHECK_EQ(r.ram.read(0x1C000), 0x11);
    CHECK_EQ(r.bus.read(0x8000), 0x11);
    r.bus.poke(0x8000, 0x22);          // the loader still reaches it
    CHECK_EQ(r.ram.read(0x1C000), 0x22);
    r.bus.write(NB + 2, 0x07);         // clear RO: software can, by design
    r.bus.write(0x8000, 0x33);
    CHECK_EQ(r.ram.read(0x1C000), 0x33);
}

TEST(rom_bit_maps_rom_a_and_the_cartridge)
{
    Rig r;
    r.rom_a.poke(0x00000, 0xA0);
    r.rom_b.poke(0x00000, 0xB0);
    r.bus.write(NB + 1, 0x80 | 0);     // W1 = ROM A block 0
    CHECK_EQ(r.bus.read(0x4000), 0xA0);
    CHECK_EQ(r.bus.last_access().module, BackplaneBridge::MODULE);
    r.bus.write(NB + 1, 0x80 | 32);    // W1 = block 32: ROM B, the cartridge
    CHECK_EQ(r.bus.read(0x4000), 0xB0);
    CHECK_EQ(r.bus.last_access().module, BackplaneBridge::MODULE);
    CHECK_EQ(r.bus.last_access().physical, 0xF80000u);
    r.bus.write(0x4000, 0x00);         // flash ignores a CPU store anyway
    CHECK_EQ(r.rom_b.read(0), 0xB0);
}

TEST(io_hole_is_decoded_ahead_of_the_windows)
{
    Rig r;
    Ram card(0x80);
    r.bus.map({bus_address(Space::Io, 0x300), bus_address(Space::Io, 0x37F), &card, 0x6});
    r.bus.write(NB + 3, 0x3F);         // W3 = RAM: the hole still wins
    r.bus.write(0xC305, 0x42);
    CHECK_EQ(card.read(5), 0x42);
    CHECK_EQ(r.ram.read(0xFC305), 0x00);
    CHECK_EQ(r.bus.last_access().physical, 0x0C305u);
    CHECK_EQ(r.bus.last_access().module, 0x6);
    r.bus.write(0xC800, 0x24);         // just past the hole: W3
    CHECK_EQ(r.ram.read(0xFC800), 0x24);
    CHECK_EQ(r.bus.read(0xC000), Decoder::UNMAPPED_READ); // module 0's block: no registers
}

TEST(vector_pulls_go_through_vec)
{
    Rig r;
    r.bus.write(NB + 3, 0x05);         // W3 = RAM block 5, holding one IRQ vector
    r.ram.write(0x17FFE, 0x00);
    r.ram.write(0x17FFF, 0x20);
    r.rom_a.poke(0x7FFFE, 0x00);       // VEC still on the flash: another
    r.rom_a.poke(0x7FFFF, 0x30);
    Cpu6502 cpu(r.bus, Cpu6502::Model::W65C02S);
    Registers regs;
    regs.pc = 0x0200;
    regs.p = 0x20;                     // I clear
    cpu.set_regs(regs);
    cpu.irq();
    CHECK_EQ(cpu.regs().pc, 0x3000);   // the vector came through VEC
    CHECK_EQ(r.bus.read(0xFFFE), 0x00);
    CHECK_EQ(r.bus.read(0xFFFF), 0x20); // an ordinary read sees W3
    r.bus.write(NB + 4, 0x05);         // VEC = W3's block: the OS's choice
    cpu.set_regs(regs);
    cpu.irq();
    CHECK_EQ(cpu.regs().pc, 0x2000);
}

TEST(irq_poll_reads_0_for_interrupting_modules)
{
    Rig r;
    Pia6821 serial, video, unwired;
    for (Pia6821 *p : {&serial, &video, &unwired})
        p->write(1, Pia6821::CR_IRQ1_ENABLE);
    r.bus.map({bus_address(Space::Io, 0x300), bus_address(Space::Io, 0x303), &serial, 0x6, true});
    r.bus.map({bus_address(Space::Io, 0x500), bus_address(Space::Io, 0x503), &video, 0xA, true});
    r.bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x383), &unwired, 0x7, false});
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::IRQL), 0xFF);
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::IRQH), 0xFF);
    serial.strobe_ca1();
    video.strobe_ca1();
    unwired.strobe_ca1();              // no IRQ wire: not on the poll either
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::IRQL), 0xBF);  // bit 6
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::IRQH), 0xFB);  // bit 2: ID A
}

TEST(wait_registers_slow_their_module_only)
{
    Rig r;
    Ram serial(0x80), storage(0x80);
    r.bus.map({bus_address(Space::Io, 0x300), bus_address(Space::Io, 0x37F), &serial, 0x6});
    r.bus.map({bus_address(Space::Io, 0x380), bus_address(Space::Io, 0x3FF), &storage, 0x7});
    r.bus.write(NB + BackplaneBridge::WAIT0 + 1, 0x20); // WAIT1: IDs 4-7; ID 6 is bits 5-4 = 2
    r.bus.read(0xC300);
    CHECK_EQ(r.bus.wait_states(), 2u);
    r.bus.read(0xC380);
    CHECK_EQ(r.bus.wait_states(), 0u);                 // ID 7 untouched
    r.bus.write(NB + BackplaneBridge::WAIT0, 0x03);     // WAIT0: ID 0 (RAM A) = 3
    const uint64_t before = r.bus.now();
    r.bus.read(0x1000);
    CHECK_EQ(r.bus.now() - before, 4u);
    r.bus.write(NB + BackplaneBridge::WAIT0 + 3, 0xC0); // WAIT3: ID F is bits 7-6
    CHECK_EQ(r.bridge.wait_states(0xF), 3u);
    CHECK_EQ(r.bridge.wait_states(0xC), 0u);
}

TEST(mode_wait_and_unused_registers)
{
    Rig r;
    r.bus.write(NB + BackplaneBridge::MODE, BackplaneBridge::MODE_TURBO);
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::MODE), BackplaneBridge::MODE_TURBO); // kept, not modelled
    r.bus.write(NB + BackplaneBridge::MODE, 0xFE);          // reserved bits: not stored
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::MODE), BackplaneBridge::MODE_TURBO);
    CHECK(!r.bridge.apple());                               // APPLE: test_apple
    r.bus.write(NB + BackplaneBridge::WAIT0 + 3, 0x5A);
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::WAIT0 + 3), 0x5A);
    r.bus.write(NB + BackplaneBridge::STATUS, 0x00);         // read-only
    CHECK(r.bus.read(NB + BackplaneBridge::STATUS) & BackplaneBridge::STATUS_POWER_ON);
    CHECK_EQ(r.bus.read(NB + 0x0E), 0xFF); // $07 is RTCCTL: test_rtc
    CHECK_EQ(r.bus.read(NB + 0x40), 0xFF);
    r.bus.reset();
    CHECK_EQ(r.bus.read(NB + BackplaneBridge::WAIT0 + 3), 0x00);
}

TEST(bios_shadows_itself_into_ram_and_keeps_running)
{
    // The BIOS boot of hw/design.md, at $C800 in the top block of ROM A.
    const std::vector<uint8_t> bios = {
        0xA9, 0x3F,       // C800 LDA #$3F
        0x8D, 0x82, 0xC6, // C802 STA WIN2     W2 = RAM block 63
        0x64, 0x00,       // C805 STZ $00
        0x64, 0x02,       // C807 STZ $02
        0xA9, 0xC8,       // C809 LDA #$C8
        0x85, 0x01,       // C80B STA $01      from $C800 (W3: flash)
        0xA9, 0x88,       // C80D LDA #$88
        0x85, 0x03,       // C80F STA $03      to $8800 (W2: RAM block 63)
        0xA0, 0x00,       // C811 LDY #0
        0xB1, 0x00,       // C813 LDA ($00),Y
        0x91, 0x02,       // C815 STA ($02),Y
        0xC8,             // C817 INY
        0xD0, 0xF9,       // C818 BNE $C813
        0xE6, 0x03,       // C81A INC $03
        0xE6, 0x01,       // C81C INC $01      $FF -> $00 after the last page
        0xD0, 0xF3,       // C81E BNE $C813
        0xA9, 0x7F,       // C820 LDA #$7F     RAM block 63, read-only
        0x8D, 0x83, 0xC6, // C822 STA WIN3     now running the copy
        0x8D, 0x84, 0xC6, // C825 STA VEC
        0xA9, 0x55,       // C828 LDA #$55
        0x8D, 0x00, 0xD0, // C82A STA $D000    read-only: dropped
        0xAD, 0x00, 0xD0, // C82D LDA $D000
        0x8D, 0x00, 0x02, // C830 STA $0200
        0xA9, 0xAA,       // C833 LDA #$AA
        0x8D, 0x01, 0x02, // C835 STA $0201    got here
        0xDB,             // C838 STP
    };
    std::vector<uint8_t> flash(0x80000, 0xFF);
    const size_t block31 = 0x7C000;
    for (size_t i = 0; i < bios.size(); ++i)
        flash[block31 + 0x0800 + i] = bios[i];
    flash[block31 + 0x1000] = 0x42;    // $D000 in the copy
    flash[block31 + 0x3FFC] = 0x00;    // reset vector: $C800
    flash[block31 + 0x3FFD] = 0xC8;
    const char *image = "backplane_bios.rom";
    std::FILE *f = std::fopen(image, "wb");
    REQUIRE(f != nullptr);
    std::fwrite(flash.data(), 1, flash.size(), f);
    std::fclose(f);

    MachineOptions opt;
    opt.rom_override = image;
    std::string error;
    auto m = Machine::load(EMU_MACHINE_DIR "/backplane.machine", opt, error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    REQUIRE(m->cpu() == "w65c02s");
    REQUIRE(m->find_as<BackplaneBridge>("northbridge") != nullptr);
    REQUIRE(m->sound_card() != nullptr);
    Decoder &bus = m->bus();
    CHECK_EQ(bus.peek(0xC57F), 0xA2); // the sound card, module A, revision 2

    Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
    Fetches fetches(bus);
    cpu.set_observer(&fetches);
    bus.reset();
    cpu.reset();
    CHECK_EQ(cpu.regs().pc, 0xC800);   // vector from the flash, through VEC
    for (int i = 0; i < 1'000'000 && cpu.state() != Cpu6502::State::Stopped; ++i)
        cpu.step();
    REQUIRE(cpu.state() == Cpu6502::State::Stopped);

    CHECK_EQ(bus.peek(0x0201), 0xAA);
    CHECK_EQ(bus.peek(0x0200), 0x42);  // the copy's byte, not the dropped $55
    const auto *bridge = m->find_as<BackplaneBridge>("northbridge");
    CHECK_EQ(bridge->window(3), 0x7F);
    CHECK_EQ(bridge->vec(), 0x7F);
    auto *ram_d = m->find("ram_d");    // C0000-FFFFF: block 63 is its top 16 KB
    REQUIRE(ram_d != nullptr);
    for (uint32_t a = 0x0800; a < 0x4000; ++a)
    {
        if (ram_d->peek(0x3C000 + a) != flash[block31 + a])
        {
            std::printf("  copy differs at $%04X\n", 0xC000 + a);
            CHECK(false);
            break;
        }
    }

    // Fetches before the switch came from the flash, after it from RAM D,
    // at the same CPU addresses with no jump.
    bool before = false, after = false;
    for (const auto &fe : fetches.seen)
    {
        if (fe.cpu == 0xC822)
            before = fe.access.module == BackplaneBridge::MODULE && fe.access.physical == 0xF7C822u;
        if (fe.cpu == 0xC825)
            after = fe.access.module == BackplaneBridge::MODULE && fe.access.physical == 0xFC825u;
    }
    CHECK(before);
    CHECK(after);
    std::remove(image);
}

// --- the new bus: memory by address (hw/design.md, "The new bus", step B1) ---

TEST(the_rom_bit_puts_out_f00000_plus_the_block)
{
    Rig r;
    for (const unsigned b : {0u, 5u, 31u, 32u, 47u, 63u})
    {
        Rom &chip = b < 32 ? r.rom_a : r.rom_b;
        chip.poke((b & 31) * 0x4000 + 0x123, static_cast<uint8_t>(0x40 + b));
        r.ram.write(b * 0x4000 + 0x123, 0x99); // RAM block b, at the same A0-A19
        r.bus.write(NB + 1, static_cast<uint8_t>(BackplaneBridge::ROM | BackplaneBridge::RO | b));
        const uint32_t want = 0xF00000 + b * 0x4000 + 0x123;
        CHECK_EQ(r.bus.read(0x4123), 0x40 + b);
        CHECK_EQ(r.bus.last_access().physical, want);
        CHECK_EQ(r.bus.last_access().module, BackplaneBridge::MODULE);
        CHECK_EQ(r.bus.physical(0x4123), want);
        CHECK_EQ(r.bus.peek_physical(want), 0x40 + b);
        CHECK_EQ(r.bus.peek_physical(want & 0xFFFFF), 0x99); // A20-A23 0: the RAM
    }
    CHECK_EQ(r.bus.route(0xFFFC).where, 0xF7FFFCu); // reset: W3 and VEC on block 31
}

TEST(an_io_cycle_never_reaches_memory_whatever_the_address_lines_hold)
{
    Rig r;
    // Bytes in RAM and in the flash wherever A0-A23 could point during a cycle to the hole.
    for (uint32_t a = 0; a < 0x800; ++a)
    {
        r.ram.write(0x0C000 + a, 0x11);       // bank 0's $C000: A0-A19 as the hole carries them
        r.ram.write(0xFC000 + a, 0x22);       // RAM block 63
        r.rom_a.poke(0x7C000 + a, 0x33);      // ROM A block 31
    }
    for (const uint8_t w3 : {uint8_t{0x03}, uint8_t{0x3F}, BackplaneBridge::RESET_TOP})
    {
        r.bus.write(NB + 3, w3);
        r.bus.write(NB + BackplaneBridge::VEC, w3);
        unsigned memory = 0;
        for (uint16_t a = 0xC000; a < 0xC680; ++a) // every card's block but the bridge's own
        {
            memory += r.bus.read(a) != 0xFF;
            r.bus.vector_pull(true);
            memory += r.bus.read(a) != 0xFF;
            r.bus.vector_pull(false);
            r.bus.write(a, 0x5A);
            memory += r.bus.last_access().module == BackplaneBridge::MODULE; // not a memory cycle
        }
        for (uint16_t a = 0xC700; a < 0xC800; ++a)
            memory += r.bus.read(a) != 0xFF;
        CHECK_EQ(memory, 0u);
        CHECK_EQ(r.bus.last_access().physical, 0x0C7FFu); // A0-A19 the CPU's, as the hole's
    }
    unsigned touched = 0;
    for (uint32_t a = 0; a < 0x800; ++a)
        touched += r.ram.read(0x0C000 + a) != 0x11 || r.ram.read(0xFC000 + a) != 0x22 ||
                   r.rom_a.read(0x7C000 + a) != 0x33;
    CHECK_EQ(touched, 0u);
}

TEST(ids_0_to_5_and_f_are_empty_slots_on_the_machine)
{
    MachineOptions opt;
    opt.rom_override = "-";
    std::string error;
    auto m = Machine::load(EMU_MACHINE_DIR "/backplane.machine", opt, error);
    if (!m)
        std::printf("  %s\n", error.c_str());
    REQUIRE(m != nullptr);
    Decoder &bus = m->bus();
    for (const unsigned id : {0x0u, 0x1u, 0x2u, 0x3u, 0x4u, 0x5u, 0xFu})
    {
        unsigned answered = 0;
        for (uint16_t a = 0; a < 0x80; ++a)
        {
            const uint16_t at = static_cast<uint16_t>(id * 0x80 + a);
            answered += bus.peek_io(at) != 0xFF;
            answered += bus.read(static_cast<uint16_t>(0xC000 + at)) != 0xFF; // the ID byte at +$7F too
        }
        if (answered)
            std::printf("  module %X answers\n", id);
        CHECK_EQ(answered, 0u);
    }
    // No line of the machine takes those IDs in the hole; the memory lines are memory only.
    for (const Mapping &map : bus.mappings())
    {
        if (space_of(map.first) == Space::Io && offset_of(map.first) < IO_SIZE)
            CHECK(map.module >= 0x6 && map.module <= 0xE);
    }
    CHECK_EQ(bus.peek(0xC6FF), BackplaneBridge::ID); // and the slots that are cards still answer
}

int main()
{
    return run_all();
}
