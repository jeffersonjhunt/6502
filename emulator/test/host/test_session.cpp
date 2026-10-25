/**
 * @file test_session.cpp
 * @brief Session (session.h): the WebUI's core, driven as the browser will.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The backplane machine with the BIOS, run in budgets of one 60 Hz frame
 * at 7.16 MHz, talked to only through Session: serial in and out, the
 * panel's switches and keys, the card's SPI registers, the lamps and the
 * trace ring. Without cc65 the BIOS is not built and the suite skips.
 */

#include "harness.h"
#include "test_support.h"

#include "session.h"
#include "sound_card.h"
#include "usb_card.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t FRAME = 119'318; // 7.15909 MHz / 60
    const std::string GREETING = "\rBACKPLANE BIOS 0.6\r\\\r";
    const std::string MACHINE = EMU_MACHINE_DIR "/backplane.machine";

    std::unique_ptr<Session> open(const std::string &machine = MACHINE)
    {
        std::string error;
        auto s = Session::create({machine, EMU_BIOS_ROM, EMU_BIOS_ROM ".lbl"}, error);
        if (!s)
            std::printf("  %s\n", error.c_str());
        return s;
    }

    /// Frames until the output (accumulated in `out`) contains `want`.
    bool run_until(Session &s, std::string &out, const std::string &want, int frames = 60)
    {
        for (int i = 0; i < frames; ++i)
        {
            s.run(FRAME);
            out += s.take_output();
            if (out.find(want) != std::string::npos)
                return true;
        }
        std::printf("  never saw '%s' in '%s'\n", want.c_str(), out.c_str());
        return false;
    }

    /// Booted to the monitor's prompt, output drained.
    std::unique_ptr<Session> booted()
    {
        auto s = open();
        std::string out;
        if (s && !run_until(*s, out, GREETING))
            return nullptr;
        return s;
    }

    std::string write_machine(const std::string &name, const std::string &text)
    {
        std::ofstream(name) << text;
        return name;
    }
} // namespace

TEST(boots_the_bios_in_frame_budgets)
{
    auto s = open();
    REQUIRE(s != nullptr);
    CHECK(s->has_card());
    std::string out;
    int frames = 0;
    for (; frames < 30 && out.find(GREETING) == std::string::npos; ++frames)
    {
        const Session::RunResult r = s->run(FRAME);
        CHECK(r.why == Session::Stop::Budget);
        CHECK(r.cycles >= FRAME && r.cycles < FRAME + 16); // at most one instruction over
        out += s->take_output();
    }
    CHECK_EQ(out, GREETING);
    CHECK(frames < 15);                                     // ~1 M cycles: well under a quarter second
    CHECK_EQ(s->take_output(), std::string());              // taken means gone
}

TEST(serial_round_trip_through_the_monitor)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    s->send("C680.C684\r");
    std::string out;
    CHECK(run_until(*s, out, "C680: 00 01 02 7F 7F"));
}

TEST(halt_switch_cont_and_run_through_the_panel)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    s->set_panel(true, false);
    Session::RunResult r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK_EQ(r.cycles, 0u);
    CHECK(s->halted());
    CHECK_EQ(s->card_read(ConsoleCard::STATUS) & ConsoleCard::HALTED, ConsoleCard::HALTED);

    const uint64_t before = s->cycles();
    s->press(ConsoleCard::CONT);                            // S INST: one instruction
    r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK(r.cycles >= 2 && r.cycles <= 7);
    CHECK_EQ(s->cycles(), before + r.cycles);

    s->set_panel(false, false);                             // ENABLE, then CONT: runs on
    s->press(ConsoleCard::CONT);
    CHECK(s->run(FRAME).why == Session::Stop::Budget);
    CHECK(!s->halted());
}

TEST(a_breakpoint_on_a_label_through_the_spi_registers)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    uint16_t echo = 0;
    REQUIRE(s->resolve("ECHO", echo));
    uint16_t hex = 0;
    CHECK(s->resolve("$ECHO", hex) == false);               // `$` means hex, and ECHO is not
    CHECK(s->resolve("$C8AB", hex) && hex == 0xC8AB);
    CHECK(!s->resolve("NOWHERE", hex));

    const uint32_t phys = s->physical(echo);
    CHECK_EQ(phys >> 12, 0xFCu);                            // W3: RAM block 63
    for (uint8_t i = 0; i < 3; ++i)
        s->card_write(static_cast<uint8_t>(ConsoleCard::BPADDR + i), static_cast<uint8_t>(phys >> (8 * i)));
    s->card_write(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    CHECK(s->run(FRAME).why == Session::Stop::Budget);      // nothing typed: no echo

    s->send("A");
    Session::RunResult r;
    for (int i = 0; i < 10 && r.why != Session::Stop::Halted; ++i)
        r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Halted);
    CHECK_EQ(s->regs().pc, echo);
    CHECK_EQ(s->card_read(ConsoleCard::STATUS), ConsoleCard::HALTED | ConsoleCard::HIT);
    CHECK_EQ(s->regs().a, 'A');                             // the key it is about to echo
}

TEST(output_reaches_the_screen_while_halted)
{
    // Halt just after ECHO stores to the 6551: the character must still
    // arrive, though the CPU will not run again until CONT.
    auto s = booted();
    REQUIRE(s != nullptr);
    uint16_t txdelay = 0;
    REQUIRE(s->resolve("TXDELAY", txdelay));
    const uint32_t phys = s->physical(txdelay);
    for (uint8_t i = 0; i < 3; ++i)
        s->card_write(static_cast<uint8_t>(ConsoleCard::BPADDR + i), static_cast<uint8_t>(phys >> (8 * i)));
    s->card_write(ConsoleCard::BPCTRL, ConsoleCard::BP_ENABLE | ConsoleCard::BP_FETCH);
    s->send("Z");
    std::string out;
    for (int i = 0; i < 10 && !s->halted(); ++i)
    {
        s->run(FRAME);
        out += s->take_output();
    }
    REQUIRE(s->halted());
    s->run(FRAME);                                          // halted: no cycles, but the screen catches up
    out += s->take_output();
    CHECK_EQ(out, std::string("Z"));
}

TEST(the_card_interrupt_ends_a_wai)
{
    // The BIOS runs with I set, so an IRQ only ends WAI; then STP.
    auto s = booted();
    REQUIRE(s != nullptr);
    s->card_write(ConsoleCard::IEN, 0x01);
    s->send("300: CB DB\r300R\r");                          // WAI; STP
    for (int i = 0; i < 5; ++i)
        CHECK(s->run(FRAME).why == Session::Stop::Budget); // waiting
    CHECK(!s->irq());
    s->press(ConsoleCard::LOAD_ADRS);                       // a key press: ISR, IRQH bit 6
    CHECK(s->irq());                                        // the IRQ lamp
    CHECK(s->run(FRAME).why == Session::Stop::Stopped);
    CHECK(s->irq());                                        // nothing acknowledged it
}

TEST(lamps_glow_while_running_and_freeze_when_halted)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    CHECK_EQ(s->lights().addr[19], 0.0f);                   // off by default
    s->set_lights(true);
    s->run(FRAME);
    ConsoleCard::Lights l = s->lights();
    // The monitor polls the 6551 ($0C301: A16-A19 low) from code at $FC8xx (high).
    CHECK(l.addr[19] > 0.5f && l.addr[19] < 1.0f);
    CHECK_EQ(l.addr[16], l.addr[19]);
    CHECK(l.sync > 0.1f && l.sync < 0.5f);

    s->set_panel(true, false);
    s->run(FRAME);
    l = s->lights();
    for (float a : l.addr)
        CHECK(a == 0.0f || a == 1.0f);                      // one cycle, latched
}

TEST(trace_ring_keeps_the_newest_cycles)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::vector<Session::TraceRow> rows(100);
    CHECK_EQ(s->trace(rows.data(), rows.size()), 0u);       // off: nothing
    s->set_trace(true, 64);
    s->run(1000);
    CHECK_EQ(s->trace(rows.data(), rows.size()), 64u);
    CHECK(rows[63].cycle <= s->cycles());
    CHECK(rows[63].cycle > s->cycles() - 16);               // the newest is the last
    unsigned fetches = 0;
    for (size_t i = 0; i < 64; ++i)
    {
        if (i > 0)
            CHECK(rows[i].cycle > rows[i - 1].cycle);       // oldest first
        if (rows[i].flags & Session::TRACE_SYNC)
        {
            ++fetches;
            CHECK_EQ(rows[i].physical, 0xF0000u | rows[i].cpu); // W3 = block 63: $FC000 + (cpu - $C000)
            CHECK_EQ(rows[i].module, 3);                         // RAM D
        }
    }
    CHECK(fetches > 5);
    CHECK_EQ(s->trace(rows.data(), 3), 3u);                 // fewer than kept: the newest three
    CHECK(rows[2].cycle > s->cycles() - 16);
}

TEST(start_and_power_cycle)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::string out;
    s->send("300: 42\r");
    REQUIRE(run_until(*s, out, "00: "));                    // the monitor answers with the old byte
    s->run(FRAME);
    CHECK_EQ(s->peek(0x0300), 0x42);
    s->set_switches(0x12345);

    s->start();                                             // the hard reset: RAM stays
    out.clear();
    CHECK(run_until(*s, out, GREETING));
    CHECK_EQ(s->peek(0x0300), 0x42);

    std::string error;
    CHECK(s->power_cycle(error));                           // RAM forgets; switches do not
    CHECK_EQ(s->peek(0x0300), 0x00);
    CHECK_EQ(s->card_read(ConsoleCard::SWR + 2), 0x01);
    out.clear();
    CHECK(run_until(*s, out, GREETING));
}

TEST(the_video_card_keeps_the_callers_clock_across_a_power_cycle)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    REQUIRE(s->video() != nullptr);
    s->set_cpu_hz(1'020'484);              // the panel's slowest clock: a second is 1,020,484 cycles
    uint64_t before = s->video()->frames();
    s->run(1'020'484);
    const uint64_t slow = s->video()->frames() - before;
    CHECK(slow == 59 || slow == 60);       // 59.94 Hz
    std::string error;
    REQUIRE(s->power_cycle(error));
    before = s->video()->frames();         // a new card
    s->run(1'020'484);
    const uint64_t after = s->video()->frames() - before;
    CHECK(after == 59 || after == 60);     // at 7.16 MHz time it would be 8
    s->set_cpu_hz(0);
    before = s->video()->frames();
    s->run(1'020'484);
    const uint64_t native = s->video()->frames() - before;
    CHECK(native == 8 || native == 9);     // 0: the card's own 7.16 MHz, 8.54 frames
}

TEST(the_sound_cards_audio_keeps_real_time_and_outlasts_a_power_cycle)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    REQUIRE(s->sound() != nullptr);
    std::vector<float> out(2 * 48'000);
    CHECK_EQ(s->take_audio(out.data(), 48'000), 0u); // not asked for: none
    s->set_audio_rate(48'000);
    s->set_cpu_hz(1'020'484);
    s->run(1'020'484 / 4); // a quarter second at the slowest clock
    const size_t quarter = s->take_audio(out.data(), 48'000);
    CHECK(quarter >= 11'999 && quarter <= 12'000); // the card keeps the caller's time
    std::string error;
    REQUIRE(s->power_cycle(error));
    CHECK_EQ(s->audio_rate(), 48'000u);
    s->run(1'020'484 / 4);
    const size_t again = s->take_audio(out.data(), 48'000); // a new card, still making it
    CHECK(again >= 11'999 && again <= 12'000);
    s->set_audio_rate(0);
    s->run(1'020'484 / 4);
    CHECK_EQ(s->take_audio(out.data(), 48'000), 0u);
}

namespace
{
    /// A two-song PSID of our own: init stores the song at $C000 and sets volume 15; play INCs $D400.
    std::vector<uint8_t> two_song_sid()
    {
        std::vector<uint8_t> b(0x7C, 0);
        auto put16 = [&](size_t at, unsigned v) {
            b[at] = static_cast<uint8_t>(v >> 8);
            b[at + 1] = static_cast<uint8_t>(v);
        };
        b[0] = 'P', b[1] = 'S', b[2] = 'I', b[3] = 'D';
        put16(0x04, 2), put16(0x06, 0x7C), put16(0x08, 0x1000), put16(0x0A, 0x1000), put16(0x0C, 0x1009);
        put16(0x0E, 2), put16(0x10, 1), put16(0x76, 0x0014);
        const std::string name = "Two Songs";
        std::copy(name.begin(), name.end(), b.begin() + 0x16);
        for (const int x : {0x8D, 0x00, 0xC0, 0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60, 0xEE, 0x00, 0xD4, 0x60})
            b.push_back(static_cast<uint8_t>(x));
        return b;
    }
} // namespace

TEST(a_sid_tune_plays_on_the_card_changes_song_stops_and_goes_with_a_power_cycle)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    SoundCard *card = s->sound();
    REQUIRE(card != nullptr);
    std::string error;
    REQUIRE(s->play_sid(two_song_sid(), 0, error));
    REQUIRE(s->sid() != nullptr);
    CHECK_EQ(s->sid()->name, std::string("Two Songs"));
    CHECK_EQ(s->sid_song(), 1u);
    CHECK(card->ctrl() & SoundCard::CTRL_RUN);
    s->run(7'159'090 / 5); // a fifth of a second of the host: ten frames on the card
    CHECK_EQ(card->sid(0)[0x18], 0x0F);
    const uint8_t frames = card->sid(0)[0x00];
    CHECK(frames >= 9 && frames <= 10);
    CHECK_EQ(card->ram()[0xC000], 0);

    REQUIRE(s->play_song(2, error));
    CHECK_EQ(s->sid_song(), 2u);
    s->run(7'159'090 / 50);
    CHECK_EQ(card->ram()[0xC000], 1); // init again, with song 2 in A
    CHECK(card->sid(0)[0x00] <= 1);  // from silence again
    CHECK(!s->play_song(3, error));
    CHECK(!error.empty());

    CHECK(!s->play_sid({1, 2, 3}, 0, error)); // not a .sid: the tune playing stays
    CHECK_EQ(s->sid()->name, std::string("Two Songs"));
    CHECK(card->ctrl() & SoundCard::CTRL_RUN);

    s->stop_sid();
    CHECK(s->sid() == nullptr);
    CHECK(!(card->ctrl() & SoundCard::CTRL_RUN));
    CHECK_EQ(card->sid(0)[0x18], 0);

    REQUIRE(s->play_sid(two_song_sid(), 2, error));
    REQUIRE(s->power_cycle(error));
    CHECK(s->sid() == nullptr); // a new card, no tune on it
    CHECK(!(s->sound()->ctrl() & SoundCard::CTRL_RUN));
}

TEST(the_speaker_and_the_sids_mix_into_the_same_stream)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::string out;
    s->set_audio_rate(48'000);
    // A SID note on both sides, from the monitor...
    for (const char *line : {"C540: 10\r", "C500: 00 10 00 00 11 00 F0\r", "C518: 0F\r"})
    {
        s->send(line);
        CHECK(run_until(*s, out, std::string(line).substr(0, 5)));
    }
    // ...and the speaker ($C5A6) toggled every 3,866 cycles, 926 Hz: LDA $C5A6, then three rounds of
    // 256 DEXes (LDY #3, LDX #0, DEX, BNE, DEY, BNE), JMP $0300.
    s->send("300: AD A6 C5 A0 03 A2 00 CA D0 FD 88 D0 F8 4C 00 03\r");
    CHECK(run_until(*s, out, "0300:"));
    std::vector<float> block(2 * 24'000);
    s->take_audio(block.data(), 24'000); // what went before
    s->send("300R\r");
    for (int i = 0; i < 6; ++i) // a tenth of a second for it to start and settle
        s->run(7'159'090 / 60), s->take_audio(block.data(), 24'000);
    double sq = 0;
    size_t n = 0;
    for (int i = 0; i < 30; ++i)
    {
        s->run(7'159'090 / 60);
        const size_t k = s->take_audio(block.data(), 24'000);
        for (size_t f = 0; f < k; ++f, ++n)
        {
            CHECK_EQ(block[2 * f], block[2 * f + 1]); // a mono tune, and the speaker: both sides alike
            sq += static_cast<double>(block[2 * f]) * block[2 * f];
        }
    }
    CHECK(n > 23'000);
    const double rms = std::sqrt(sq / static_cast<double>(n));
    // The speaker's square, 0.2 RMS less the few % of its harmonics past 24 kHz, and the SID's
    // triangle at full envelope, 0.072: about 0.21 together (the speaker alone is 0.197).
    CHECK(rms > 0.203 && rms < 0.22);
    CHECK(s->usb()->speaker_toggles() > 400);
}

TEST(pause_break_on_the_usb_keyboard_breaks_into_the_monitor)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    UsbCard *usb = s->usb();
    REQUIRE(usb != nullptr);
    std::string out;
    CHECK_EQ(usb->peek(UsbCard::KBCFG), 0x80);   // the BIOS let Pause/Break break in
    usb->write(UsbCard::KBCFG, 0);
    usb->key(UsbCard::PAUSE, true);              // KBCFG bit 7 clear: nothing
    usb->key(UsbCard::PAUSE, false);
    s->run(FRAME * 4);
    CHECK_EQ(s->take_output(), std::string(""));
    usb->write(UsbCard::KBCFG, UsbCard::NMI_ON_PAUSE);
    usb->key(UsbCard::PAUSE, true);
    CHECK(run_until(*s, out, "\\\r"));          // /NMI: the vector is the Woz Monitor, a fresh prompt
}

TEST(apple_ii_mode_fixes_the_clock_and_the_cards_follow_it)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    CHECK_EQ(s->fixed_hz(), 0u);
    s->send("C685: 1\r");                     // MODE: Apple II mode, straight from the monitor
    for (int i = 0; i < 60 && !s->fixed_hz(); ++i)
        s->run(FRAME);
    CHECK_EQ(s->fixed_hz(), 1'020'484u);
    const uint64_t before = s->video()->frames();
    s->run(1'020'484);                         // a second at the Apple clock
    const uint64_t frames = s->video()->frames() - before;
    CHECK(frames == 59 || frames == 60);       // the video card keeps real time at it
    std::string error;
    REQUIRE(s->power_cycle(error));            // a hard reset: native again
    CHECK_EQ(s->fixed_hz(), 0u);
    const uint64_t native = s->video()->frames();
    s->run(1'020'484);                         // the caller's clock again (7.16 MHz): 8.5 frames,
    const uint64_t after = s->video()->frames() - native;
    CHECK(after == 8 || after == 9);           // not the Apple clock's 60
}

TEST(a_cartridge_plugged_in_power_cycles_and_c806_enters_apple_ii_mode)
{
    // A stand-in IIe ROM: the ID byte, and a reset that turns on graphics ($C050), stores $A5 at $0300 and stops.
    std::string image(0x4000, '\0');
    image[0xFBB3 - 0xC000] = 0x06;
    image[0xFFFC - 0xC000] = 0x62;
    image[0xFFFD - 0xC000] = static_cast<char>(0xFA);
    const uint8_t reset[] = {0xAD, 0x50, 0xC0, 0xA9, 0xA5, 0x8D, 0x00, 0x03, 0x4C, 0x6A, 0xFA};
    for (size_t i = 0; i < sizeof reset; ++i)
        image[0xFA62 - 0xC000 + i] = static_cast<char>(reset[i]);
    std::ofstream("test_session_iie.rom", std::ios::binary) << image;
    std::ofstream("test_session_short.rom", std::ios::binary) << std::string(100, '\0');

    auto s = booted();
    REQUIRE(s != nullptr);
    std::string error, out;
    REQUIRE(s->set_cartridge({"test_session_iie.rom"}, "", error));
    CHECK(run_until(*s, out, GREETING));         // plugged in with the power off: a fresh boot
    CHECK(!s->set_cartridge({"test_session_short.rom"}, "", error));
    CHECK(error.find("16 KB") != std::string::npos);
    CHECK_EQ(s->cartridge().size(), 1u);         // the old one stays in, and works:
    CHECK_EQ(s->cartridge()[0], std::string("test_session_iie.rom"));
    REQUIRE(s->apple_switches() != nullptr);
    CHECK(s->apple_switches()->text);
    CHECK(!s->apple_mode());
    CHECK_EQ(s->peek(0xC4FF), 0x91);             // the video card's ID, at $C480 + $7F
    s->send("C806R\r");
    for (int i = 0; i < 120 && s->peek_physical(0x0300) != 0xA5; ++i)
        s->run(FRAME);
    CHECK(s->apple_mode());
    CHECK_EQ(s->peek_physical(0x0300), 0xA5);
    CHECK(!s->apple_switches()->text);           // the ROM's $C050
    CHECK(s->peek(0xC4FF) != 0x91);              // $C4FF is the Apple's now...
    CHECK_EQ(s->peek_io(0x4FF), 0x91);           // ...and I/O space still reaches the card

    REQUIRE(s->set_cartridge({}, "", error));    // taken out
    CHECK(!s->apple_mode());
    out.clear();
    REQUIRE(run_until(*s, out, GREETING));
    s->send("C806R\r");
    CHECK(run_until(*s, out, "NO APPLE IIE ROM"));
    std::remove("test_session_iie.rom");
    std::remove("test_session_short.rom");
}

TEST(a_disk_stays_in_its_drive_through_power_cycles_and_cartridges)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    REQUIRE(s->disk2() != nullptr);
    std::string error;
    CHECK(!s->insert_disk(0, std::vector<uint8_t>(1000), "notes.txt", error));
    CHECK(error.find("notes.txt: 1000 bytes is no disk image") == 0);
    CHECK(s->disk(0) == nullptr);
    REQUIRE(s->insert_disk(0, std::vector<uint8_t>(143'360), "blank.dsk", error));
    DiskImage *d = s->disk(0);
    REQUIRE(d != nullptr);
    CHECK(s->disk2()->disk(0).get() == d);       // in the card's drive 1
    REQUIRE(s->power_cycle(error));
    CHECK(s->disk(0) == d);                       // the machine is new; the disk is the same one
    CHECK(s->disk2()->disk(0).get() == d);
    std::ofstream("test_session_p5.rom", std::ios::binary) << std::string(256, '\x60');
    std::ofstream("test_session_bad.rom", std::ios::binary) << std::string(255, '\0');
    CHECK(!s->set_cartridge({}, "", "test_session_bad.rom", "", error));
    CHECK(error.find("want a 256-byte Disk II PROM") != std::string::npos);
    CHECK(!s->disk2()->has_boot());               // refused: the old cartridge (none) stays
    REQUIRE(s->set_cartridge({}, "", "test_session_p5.rom", "", error));
    CHECK(s->disk2()->has_boot());
    CHECK_EQ(s->disk2()->slot_rom(0), 0x60);
    CHECK(s->disk2()->disk(0).get() == d);        // and through a cartridge change
    s->eject_disk(0);
    CHECK(s->disk(0) == nullptr);
    CHECK(s->disk2()->disk(0) == nullptr);
    CHECK_EQ(s->peek_io(0x67F), Disk2Card::ID);   // native mode: the card's ID at $C67F
    std::remove("test_session_p5.rom");
    std::remove("test_session_bad.rom");
}

TEST(stp_ends_the_run_and_start_gets_out)
{
    auto s = booted();
    REQUIRE(s != nullptr);
    std::string out;
    s->send("300: DB\r300R\r");                             // STP, and run it
    Session::RunResult r;
    for (int i = 0; i < 20 && r.why != Session::Stop::Stopped; ++i)
        r = s->run(FRAME);
    CHECK(r.why == Session::Stop::Stopped);
    CHECK(s->stopped());
    CHECK_EQ(s->run(FRAME).cycles, 0u);                     // and stays stopped
    s->start();
    out.clear();
    CHECK(run_until(*s, out, GREETING));
    CHECK(!s->stopped());
}

TEST(a_machine_without_a_panel)
{
    std::string text;
    {
        std::ifstream in(MACHINE);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const size_t at = text.find("\nconsole ");
    REQUIRE(at != std::string::npos);
    text.erase(at + 1, text.find('\n', at + 1) - at);       // drop the console line
    auto s = open(write_machine("no_panel.machine", text));
    std::remove("no_panel.machine");                        // read at create: leave nothing behind
    REQUIRE(s != nullptr);
    CHECK(!s->has_card());
    CHECK_EQ(s->card_read(ConsoleCard::ID_REG), 0xFF);
    s->set_panel(true, false);                              // nothing to halt
    CHECK(s->run(FRAME).why == Session::Stop::Budget);
    std::string out = s->take_output();
    CHECK(run_until(*s, out, GREETING));
    CHECK(s->lights().addr[19] == 0.0f);
}

TEST(errors_name_what_failed)
{
    std::string error;
    CHECK(Session::create({"no_such.machine", "", ""}, error) == nullptr);
    CHECK(error.find("no_such.machine") != std::string::npos);
    error.clear();
    CHECK(Session::create({MACHINE, EMU_BIOS_ROM, "no_such.lbl"}, error) == nullptr);
    CHECK(error.find("no_such.lbl") != std::string::npos);
    error.clear();
    CHECK(Session::create({MACHINE, "no_such.rom", ""}, error) == nullptr);
    CHECK(error.find("no_such.rom") != std::string::npos);
}

int main()
{
    if (!std::ifstream(EMU_BIOS_ROM))
    {
        std::printf("SKIP: %s not built (needs ca65/ld65)\n", EMU_BIOS_ROM);
        return SKIP;
    }
    return run_all();
}
