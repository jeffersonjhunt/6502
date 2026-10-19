/**
 * @file test_pia.cpp
 * @brief The 6821 PIA's register rules.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Several of these were Apple-1 bugs before Phase 1, when the PIA was a few
 * special cases in a flat memory array: a CPU store setting key-ready (the
 * phantom key at reset), and the display only printing characters with bit 7
 * set (Integer BASIC's "EN ERR"). Here they are properties of the chip.
 */

#include "harness.h"

#include "pia6821.h"

#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t DATA_A = 0, CR_A = 1, DATA_B = 2, CR_B = 3;
} // namespace

TEST(data_register_is_ddr_until_cr_bit_2)
{
    Pia6821 p;
    p.write(DATA_B, 0x7F); // CRB = 0: this is DDRB
    CHECK_EQ(p.read(DATA_B), 0x7F);
    p.write(CR_B, Pia6821::CR_SELECT_OR);
    p.set_input_b(0x80);
    CHECK_EQ(p.read(DATA_B), 0x80); // ORB = 0 on outputs, the PB7 input high
}

TEST(port_read_mixes_outputs_and_inputs_by_ddr)
{
    Pia6821 p;
    p.write(DATA_A, 0xF0); // DDRA: high nibble out
    p.write(CR_A, Pia6821::CR_SELECT_OR);
    p.write(DATA_A, 0xAA);
    p.set_input_a(0x55);
    CHECK_EQ(p.read(DATA_A), 0xA5);
}

TEST(cpu_cannot_set_or_clear_the_flags)
{
    Pia6821 p;
    p.write(CR_A, 0xA7); // what Woz Monitor stores at reset
    CHECK_EQ(p.read(CR_A), 0x27);
    p.strobe_ca1();
    p.write(CR_A, 0x27);
    CHECK_EQ(p.read(CR_A) & Pia6821::CR_IRQ1, Pia6821::CR_IRQ1);
}

TEST(reading_data_clears_the_flags)
{
    Pia6821 p;
    p.write(CR_A, Pia6821::CR_SELECT_OR);
    p.set_input_a(0xC1);
    p.strobe_ca1();
    CHECK_EQ(p.read(DATA_A), 0xC1);
    CHECK_EQ(p.read(CR_A) & Pia6821::CR_IRQ1, 0x00);
}

TEST(reading_the_ddr_leaves_the_flags)
{
    Pia6821 p;
    p.strobe_ca1(); // CRA bit 2 is 0: offset 0 is DDRA
    p.read(DATA_A);
    CHECK_EQ(p.read(CR_A) & Pia6821::CR_IRQ1, Pia6821::CR_IRQ1);
}

TEST(write_handler_sees_every_store_whatever_bit_7)
{
    Pia6821 p;
    std::vector<uint8_t> seen;
    p.on_write_b([&](uint8_t v) { seen.push_back(v); });
    p.write(DATA_B, 0x7F);       // DDR write: not output
    p.write(CR_B, Pia6821::CR_SELECT_OR);
    p.write(DATA_B, 0x44);       // 'D', bit 7 clear
    p.write(DATA_B, 0xC5);       // 'E' with bit 7 set: PB7 is not an output
    REQUIRE(seen.size() == 2u);
    CHECK_EQ(seen[0], 0x44);
    CHECK_EQ(seen[1], 0x45);
}

TEST(irq_needs_flag_and_enable)
{
    Pia6821 p;
    p.strobe_ca1();
    CHECK(!p.irq());
    p.write(CR_A, Pia6821::CR_IRQ1_ENABLE);
    CHECK(p.irq());
    p.write(CR_A, Pia6821::CR_IRQ1_ENABLE | Pia6821::CR_SELECT_OR);
    p.read(DATA_A);
    CHECK(!p.irq());
}

TEST(registers_mirror_on_the_low_two_bits)
{
    Pia6821 p;
    p.write(CR_B + 4, 0x04);
    CHECK_EQ(p.read(CR_B), 0x04);
}

int main()
{
    return run_all();
}
