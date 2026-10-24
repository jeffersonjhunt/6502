/**
 * @file test_apple.cpp
 * @brief Apple II mode's north bridge (hw/design.md, "Apple II mode"): the IIe's map and soft switches.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A backplane with 1 MB of RAM and a USB card, put into Apple II mode by a
 * MODE write, as the BIOS's C806R does. Accesses are bus cycles (read() and
 * write()), so the switches see them as they would on the board; where each
 * lands is read back with peek_physical(). Bank E holds a pattern standing
 * in for the IIe ROM image: none of this needs Apple's.
 */

#include "harness.h"

#include "backplane_bridge.h"
#include "machine.h"
#include "usb_card.h"

#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint16_t MODE = 0xC685; // the north bridge's MODE, in native mode

    /// The stand-in ROM image's byte for CPU address `a` (bank E).
    uint8_t rom_byte(uint32_t a) { return static_cast<uint8_t>(a * 7 + 3); }

    struct Apple
    {
        Apple()
        {
            std::string error;
            machine = Machine::build("cpu w65c02s\nnorthbridge backplane\nram 00000-FFFFF module=0\n"
                                     "usb 580-5FF space=io name=usb irq\n",
                                     "apple.machine", {}, error);
            if (!machine)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            bridge = machine->find_as<BackplaneBridge>("northbridge");
            usb = machine->usb_card();
            for (uint32_t a = 0xC000; a < 0x10000; ++a)
                bus().write_physical(0xE0000 | a, rom_byte(a));
        }
        bool ok() const { return bridge != nullptr; }
        Decoder &bus() { return machine->bus(); }
        /// As C806R ends: MODE, then the /RESET pulse the run loop gives.
        void enter()
        {
            bus().write(MODE, BackplaneBridge::MODE_APPLE);
            if (bus().take_reset())
                bus().pulse_reset();
        }
        uint8_t at(uint32_t physical) { return bus().peek_physical(physical); }
        const AppleSwitches &s() const { return bridge->apple_switches(); }

        std::unique_ptr<Machine> machine;
        BackplaneBridge *bridge{nullptr};
        UsbCard *usb{nullptr};
    };
} // namespace

TEST(mode_enters_with_a_reset_pulse_and_only_a_hard_reset_leaves)
{
    Apple a;
    REQUIRE(a.ok());
    CHECK_EQ(a.bus().read(0xC6FF), BackplaneBridge::ID);    // native: the bridge's registers
    CHECK(!a.bus().take_reset());
    a.bus().write(MODE, BackplaneBridge::MODE_APPLE);
    CHECK(a.bridge->apple());
    CHECK(a.bus().take_reset());                            // the switch pulses /RESET, once
    CHECK(!a.bus().take_reset());
    a.bus().pulse_reset();
    CHECK(a.bridge->apple());                               // /RESET alone keeps it
    CHECK_EQ(a.bus().peek(0xC6FF), 0xFF);                  // slot 6's ROM space, empty: not the bridge's ID
    a.bus().write(0xC007, 0);                               // INTCXROM: $C6FF is the IIe ROM now
    CHECK_EQ(a.bus().peek(0xC6FF), rom_byte(0xC6FF));       // the native registers are gone
    a.bus().write(0xC685, 0);                               // ...so MODE can't be written to leave
    CHECK(a.bridge->apple());
    a.bus().reset();                                        // a hard reset
    CHECK(!a.bridge->apple());
    CHECK_EQ(a.bus().read(0xC6FF), BackplaneBridge::ID);
}

TEST(ramrd_and_ramwrt_pick_main_or_aux_for_reads_and_writes_apart)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.bus().write(0x2000, 0x11);
    CHECK_EQ(a.at(0x02000), 0x11);                          // main: bank 0
    a.bus().write(0xC005, 0);                               // RAMWRT on
    a.bus().write(0x2000, 0x22);
    CHECK_EQ(a.at(0x12000), 0x22);                          // aux: bank 1
    CHECK_EQ(a.bus().read(0x2000), 0x11);                   // reads still main
    a.bus().write(0xC003, 0);                               // RAMRD on
    CHECK_EQ(a.bus().read(0x2000), 0x22);
    CHECK_EQ(a.bus().read(0xC013) & 0x80, 0x80);            // status: RAMRD
    CHECK_EQ(a.bus().read(0xC014) & 0x80, 0x80);            // RAMWRT
    a.bus().write(0xC004, 0);                               // RAMWRT off
    a.bus().write(0x0300, 0x33);
    CHECK_EQ(a.at(0x00300), 0x33);
    a.bus().write(0x0080, 0x44);                            // zero page: ALTZP's, not RAMWRT's
    CHECK_EQ(a.at(0x00080), 0x44);
    a.bus().write(0xBFFF, 0x55);
    CHECK_EQ(a.at(0x0BFFF), 0x55);
}

TEST(altzp_moves_the_zero_page_the_stack_and_the_language_card)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.bus().read(0xC083);
    a.bus().read(0xC083);                                   // LC: read and write RAM, bank 2
    a.bus().write(0x0012, 0x01);
    a.bus().write(0x01FF, 0x02);
    a.bus().write(0xD000, 0x03);
    a.bus().write(0xC009, 0);                               // ALTZP on
    a.bus().write(0x0012, 0x11);
    a.bus().write(0x01FF, 0x12);
    a.bus().write(0xD000, 0x13);
    a.bus().write(0xFFFF, 0x14);
    CHECK_EQ(a.at(0x00012), 0x01);
    CHECK_EQ(a.at(0x10012), 0x11);
    CHECK_EQ(a.at(0x101FF), 0x12);
    CHECK_EQ(a.at(0x2D000), 0x03);                          // main language card: bank 2
    CHECK_EQ(a.at(0x3D000), 0x13);                          // aux: bank 3
    CHECK_EQ(a.at(0x3FFFF), 0x14);
    CHECK_EQ(a.bus().read(0xC016) & 0x80, 0x80);
    a.bus().write(0x0200, 0x15);                            // $0200 on: not ALTZP's
    CHECK_EQ(a.at(0x00200), 0x15);
}

TEST(80store_with_page2_and_hires_picks_the_display_pages)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.bus().write(0xC001, 0);                               // 80STORE on
    a.bus().read(0xC055);                                   // PAGE2 on: aux, for text page 1
    a.bus().write(0x0400, 0x01);
    a.bus().write(0x07FF, 0x02);
    a.bus().write(0x0800, 0x03);                            // past text page 1: RAMWRT's (main)
    a.bus().write(0x2000, 0x04);                            // hi-res page 1, HIRES off: main
    CHECK_EQ(a.at(0x10400), 0x01);
    CHECK_EQ(a.at(0x107FF), 0x02);
    CHECK_EQ(a.at(0x00800), 0x03);
    CHECK_EQ(a.at(0x02000), 0x04);
    a.bus().read(0xC057);                                   // HIRES on: hi-res page 1 follows PAGE2 too
    a.bus().write(0x2000, 0x05);
    a.bus().write(0x3FFF, 0x06);
    CHECK_EQ(a.at(0x12000), 0x05);
    CHECK_EQ(a.at(0x13FFF), 0x06);
    a.bus().write(0xC005, 0);                               // RAMWRT on, PAGE2 off: 80STORE still wins
    a.bus().read(0xC054);
    a.bus().write(0x0400, 0x07);
    CHECK_EQ(a.at(0x00400), 0x07);
    CHECK_EQ(a.bus().read(0xC018) & 0x80, 0x80);            // status: 80STORE
    a.bus().write(0xC000, 0);                               // 80STORE off: PAGE2 is display only
    a.bus().read(0xC055);
    a.bus().write(0x0400, 0x08);
    CHECK_EQ(a.at(0x10400), 0x08);                          // RAMWRT's aux now
}

TEST(the_language_card_two_reads_to_write_and_its_two_d000_banks)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    // /RESET: read ROM, write RAM bank 2
    CHECK_EQ(a.bus().read(0xD000), rom_byte(0xD000));
    a.bus().write(0xD000, 0x21);
    CHECK_EQ(a.at(0x2D000), 0x21);
    CHECK_EQ(a.at(0xED000), rom_byte(0xD000));              // the ROM image untouched
    CHECK_EQ(a.bus().read(0xC011) & 0x80, 0x80);            // BSRBANK2
    CHECK_EQ(a.bus().read(0xC012) & 0x80, 0x00);            // not reading RAM
    a.bus().read(0xC080);                                   // read RAM, no write, bank 2
    CHECK_EQ(a.bus().read(0xD000), 0x21);
    a.bus().write(0xD000, 0x99);                            // write-protected: dropped
    CHECK_EQ(a.at(0x2D000), 0x21);
    a.bus().read(0xC08B);                                   // bank 1, read RAM; one read: no write yet
    a.bus().write(0xD000, 0x31);
    CHECK_EQ(a.at(0x4D000), 0x00);
    a.bus().read(0xC08B);                                   // the second read: write too
    a.bus().write(0xD000, 0x31);
    CHECK_EQ(a.at(0x4D000), 0x31);                          // bank 1's $D000 is at bank 4
    a.bus().write(0xE000, 0x32);
    CHECK_EQ(a.at(0x2E000), 0x32);                          // $E000-$FFFF: bank 2 either way
    CHECK_EQ(a.bus().read(0xC011) & 0x80, 0x00);            // bank 1
    a.bus().read(0xC08A);                                   // even: write off, read ROM
    a.bus().write(0xD001, 0x33);
    CHECK_EQ(a.at(0x4D001), 0x00);
    CHECK_EQ(a.bus().read(0xD001), rom_byte(0xD001));
    a.bus().read(0xC089);                                   // read, a write in between, read: not two in a row
    a.bus().write(0xC089, 0);
    a.bus().read(0xC089);
    a.bus().write(0xD002, 0x34);
    CHECK_EQ(a.at(0x4D002), 0x00);
    a.bus().read(0xC089);                                   // now two: read ROM, write RAM bank 1
    a.bus().write(0xD002, 0x34);
    CHECK_EQ(a.at(0x4D002), 0x34);
    CHECK_EQ(a.bus().read(0xD002), rom_byte(0xD002));
}

TEST(rom_space_intcxrom_slotc3rom_and_intc8rom)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    CHECK_EQ(a.bus().read(0xC600), 0xFF);                   // slot 6: empty (no card yet)
    CHECK_EQ(a.bus().read(0xC800), 0xFF);                   // no expansion ROM selected
    CHECK_EQ(a.bus().read(0xC300), rom_byte(0xC300));       // SLOTC3ROM off: the internal $C3 ROM...
    CHECK(a.s().intc8rom);                                  // ...which turns on the internal $C8 ROM
    CHECK_EQ(a.bus().read(0xC800), rom_byte(0xC800));
    CHECK_EQ(a.bus().read(0xCFFE), rom_byte(0xCFFE));
    a.bus().read(0xCFFF);                                   // $CFFF lets it go
    CHECK(!a.s().intc8rom);
    CHECK_EQ(a.bus().read(0xC800), 0xFF);
    a.bus().write(0xC00B, 0);                               // SLOTC3ROM on: slot 3, empty
    CHECK_EQ(a.bus().read(0xC300), 0xFF);
    CHECK(!a.s().intc8rom);
    CHECK_EQ(a.bus().read(0xC017) & 0x80, 0x80);
    a.bus().write(0xC007, 0);                               // INTCXROM on: all of it internal
    CHECK_EQ(a.bus().read(0xC100), rom_byte(0xC100));
    CHECK_EQ(a.bus().read(0xC600), rom_byte(0xC600));
    CHECK_EQ(a.bus().read(0xC300), rom_byte(0xC300));
    CHECK_EQ(a.bus().read(0xCABC), rom_byte(0xCABC));
    CHECK_EQ(a.bus().read(0xC015) & 0x80, 0x80);
    a.bus().write(0xC006, 0);
    CHECK_EQ(a.bus().read(0xC100), 0xFF);
}

TEST(video_switches_on_any_access_and_their_status)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    CHECK(a.s().text);
    a.bus().read(0xC050);                                   // graphics
    a.bus().write(0xC053, 0);                               // MIXED on, by a write
    a.bus().read(0xC055);
    a.bus().read(0xC057);
    a.bus().write(0xC00D, 0);                               // 80COL on
    a.bus().write(0xC00F, 0);                               // ALTCHARSET on
    a.bus().read(0xC05E);                                   // AN3 off: DHIRES on
    CHECK(!a.s().text);
    CHECK(a.s().mixed && a.s().page2 && a.s().hires && a.s().col80 && a.s().altchar && a.s().dhires);
    CHECK_EQ(a.bus().read(0xC01A) & 0x80, 0x00);            // TEXT
    CHECK_EQ(a.bus().read(0xC01B) & 0x80, 0x80);            // MIXED
    CHECK_EQ(a.bus().read(0xC01C) & 0x80, 0x80);            // PAGE2
    CHECK_EQ(a.bus().read(0xC01D) & 0x80, 0x80);            // HIRES
    CHECK_EQ(a.bus().read(0xC01E) & 0x80, 0x80);            // ALTCHARSET
    CHECK_EQ(a.bus().read(0xC01F) & 0x80, 0x80);            // 80COL
    CHECK_EQ(a.bus().read(0xC07F) & 0x80, 0x80);            // RDDHIRES
    a.bus().read(0xC05F);                                   // AN3 on: DHIRES off
    CHECK(!a.s().dhires);
    CHECK_EQ(a.s().an, 0x08);
    a.bus().read(0xC059);                                   // AN0 on (odd: on)...
    a.bus().read(0xC05A);                                   // ...AN1 off...
    a.bus().read(0xC05B);                                   // ...and on
    CHECK_EQ(a.s().an, 0x0B);
    a.bus().read(0xC058);
    CHECK_EQ(a.s().an, 0x0A);
}

TEST(peek_changes_nothing)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.bus().peek(0xC055);
    a.bus().peek(0xC08B);
    a.bus().peek(0xC08B);
    a.bus().peek(0xC300);
    a.usb->key(0x04, true);
    a.bus().peek(0xC010);
    CHECK(!a.s().page2);
    CHECK(!a.s().lc_read);
    CHECK(!a.s().intc8rom);
    CHECK_EQ(a.usb->peek(UsbCard::KEY), 0x80 | 'A');        // the strobe still up
}

TEST(the_keyboard_speaker_and_game_port_are_the_usb_cards)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.usb->key(0x04, true);                                 // A
    CHECK_EQ(a.bus().read(0xC000), 0x80 | 'A');
    CHECK_EQ(a.bus().read(0xC00F), 0x80 | 'A');             // $C000-$C00F all read it
    CHECK_EQ(a.bus().read(0xC013), 'A');                    // bits 0-6 of the status reads: the latch
    CHECK_EQ(a.bus().read(0xC010), 0x80 | 'A');             // a key down; and the strobe cleared
    CHECK_EQ(a.bus().read(0xC000), 'A');
    a.usb->key(0x05, true);
    a.bus().write(0xC01A, 0);                               // a write to $C010-$C01F clears it too
    CHECK_EQ(a.bus().read(0xC000), 'B');
    const uint64_t clicks = a.usb->speaker_toggles();
    a.bus().read(0xC030);
    a.bus().write(0xC030, 0);
    CHECK_EQ(a.usb->speaker_toggles(), clicks + 2);
    a.usb->set_game_buttons(0x05);                          // buttons 0 and 2
    CHECK_EQ(a.bus().read(0xC061), 0x80);
    CHECK_EQ(a.bus().read(0xC062), 0x00);
    CHECK_EQ(a.bus().read(0xC063), 0x80);
    CHECK_EQ(a.bus().read(0xC069), 0x80);                   // mirrored every 8
    a.usb->set_paddle(1, 200);
    CHECK_EQ(a.bus().read(0xC065), 0x00);                   // not triggered
    a.bus().read(0xC070);
    CHECK_EQ(a.bus().read(0xC065), 0x80);                   // timing
    CHECK_EQ(a.bus().read(0xC064), 0x00);                   // paddle 0 at 0: done
}

TEST(vbl_follows_the_apples_frame_in_cpu_cycles)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    auto blank = [&] { return (a.bus().peek(0xC019) & 0x80) == 0; };
    while (a.bus().now() % AppleIo::FRAME_CYCLES != 0)
        a.bus().idle();                                     // to a frame's start
    CHECK(!blank());
    for (unsigned i = 0; i < 192 * 65 - 1; ++i)
        a.bus().idle();
    CHECK(!blank());                                        // the last cycle of line 191
    a.bus().idle();
    CHECK(blank());                                         // line 192: vertical blank
    for (unsigned i = 0; i < 70 * 65 - 1; ++i)
        a.bus().idle();
    CHECK(blank());
    a.bus().idle();
    CHECK(!blank());                                        // the next frame, 17,030 cycles on
}

TEST(a_reset_pulse_puts_the_iies_switches_back)
{
    Apple a;
    REQUIRE(a.ok());
    a.enter();
    a.bus().write(0xC005, 0);
    a.bus().write(0xC009, 0);
    a.bus().write(0xC001, 0);
    a.bus().read(0xC050);
    a.bus().read(0xC08B);
    a.bus().read(0xC08B);
    a.bus().pulse_reset();                                  // as Ctrl-Reset will
    CHECK(a.bridge->apple());
    CHECK(!a.s().ramwrt && !a.s().altzp && !a.s().store80);
    CHECK(a.s().text);
    CHECK(a.s().lc_bank2 && !a.s().lc_read && a.s().lc_write);
}

TEST(the_apple_clock_while_in_the_mode)
{
    Apple a;
    REQUIRE(a.ok());
    CHECK_EQ(a.bus().clock_hz(), 0u);                       // native: the caller's choice
    a.enter();
    CHECK_EQ(a.bus().clock_hz(), BackplaneBridge::APPLE_HZ);
    CHECK_EQ(BackplaneBridge::APPLE_HZ, 14'318'180u * 65 / 912); // 65 cycles in 912 master clocks
    a.bus().pulse_reset();
    CHECK_EQ(a.bus().clock_hz(), BackplaneBridge::APPLE_HZ); // Ctrl-Reset keeps it
    a.bus().reset();
    CHECK_EQ(a.bus().clock_hz(), 0u);
}

int main()
{
    return run_all();
}
