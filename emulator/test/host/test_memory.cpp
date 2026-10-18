/**
 * @file test_memory.cpp
 * @brief Apple-1 memory map: ROM protection and the PIA's read-only flags.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Two bugs from EMULATOR_PLAN.md §1, both from a Memory that let the CPU write
 * anything anywhere:
 *
 * - ROM was writable. Applesoft sizes RAM by complementing a byte per page
 *   until one does not change; with no read-only memory it never found one
 *   and looped forever at $FCE8. That was "the Applesoft ROM does not work".
 * - Bit 7 of KBDCR (key ready) could be set by a CPU store. Woz Monitor
 *   writes $A7 there at reset, so every boot started with a phantom $00 key.
 *   On a 6821 bits 6-7 of each control register are status flags the CPU
 *   cannot write; only the device side (Poke) can.
 */

#include "harness.h"
#include "test_support.h"

#include "memory.h"

using namespace ooe::test;

TEST(ram_is_writable)
{
    Memory m;
    m.Write(0x1234, 0x5A);
    CHECK_EQ(m.Read(0x1234), 0x5A);
}

TEST(read_only_range_ignores_cpu_writes)
{
    Memory m;
    m.Poke(0xE000, 0x4C);
    m.SetReadOnly(0x8000, 0xFFFF);
    m.Write(0xE000, 0xB3);
    CHECK_EQ(m.Read(0xE000), 0x4C);
    m.Write(0x7FFF, 0x11); // the byte below the range is still RAM
    CHECK_EQ(m.Read(0x7FFF), 0x11);
}

TEST(poke_writes_through_read_only)
{
    // Loaders and devices are not the CPU: a ROM has to get its contents.
    Memory m;
    m.SetReadOnly(0x8000, 0xFFFF);
    m.Poke(0xFF00, 0xD8);
    CHECK_EQ(m.Read(0xFF00), 0xD8);
}

TEST(read_only_range_can_be_reopened)
{
    // The PIA sits inside the ROM image's address range and must stay live.
    Memory m;
    m.SetReadOnly(0x8000, 0xFFFF);
    m.SetReadOnly(KBD, DSPCR, false);
    m.Write(DSPCR, 0x27);
    CHECK_EQ(m.Read(DSPCR), 0x27);
}

TEST(cpu_cannot_set_key_ready)
{
    Memory m;
    m.Write(KBDCR, 0xA7); // what Woz Monitor stores at reset
    CHECK_EQ(m.Read(KBDCR) & 0x80, 0x00);
    CHECK_EQ(m.Read(KBDCR) & 0x3F, 0x27); // the writable bits do land
}

TEST(cpu_cannot_clear_key_ready)
{
    Memory m;
    m.Poke(KBDCR, 0x80);
    m.Write(KBDCR, 0x27);
    CHECK_EQ(m.Read(KBDCR) & 0x80, 0x80);
}

TEST(dsp_write_outputs_regardless_of_bit_7)
{
    // On the Apple-1, DSP bit 7 is the display's busy line, not part of the
    // character: every store outputs, and reads show busy until the display
    // takes it. Treating the stored bit 7 as "pending" dropped every
    // character sent without it: Integer BASIC's last letter of each error
    // message ("EN ERR"), and Applesoft's echo once its RDKEY masks the key.
    Memory m;
    m.Write(DSPCR, 0x04); // CRB bit 2: $D012 is the output register
    m.Write(DSP, 0x44);   // 'D', bit 7 clear
    CHECK_EQ(m.Read(DSP), 0xC4);
}

TEST(dsp_write_before_setup_goes_to_ddr)
{
    // At reset CRB bit 2 is 0 and $D012 is the data direction register. Woz
    // Monitor stores $7F there first; that must not reach the screen.
    Memory m;
    m.Write(DSP, 0x7F);
    CHECK_EQ(m.Read(DSP) & 0x80, 0x00);
}

TEST(reading_kbd_clears_key_ready)
{
    Memory m;
    m.Poke(KBD, 0xC1);
    m.Poke(KBDCR, 0x80);
    CHECK_EQ(m.Read(KBD), 0xC1);
    CHECK_EQ(m.Read(KBDCR) & 0x80, 0x00);
}

int main()
{
    return run_all();
}
