/**
 * @file test_oodos_serial.cpp
 * @brief OODOS's serial receive buffer (hw/oodos.md, "OODOS: a receive buffer for the serial line"): the ring
 * the dispatcher fills from the 6551 stand-in and the BIOS empties, its overflow rule, the warm boot's flush,
 * its costs.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * SERTEST (oodos/sys/test/sertest.s) is busy for a while with interrupts on and no BDOS call, so what comes on
 * the line meanwhile has only the ring to keep it, then reads AUX: by functions 7 and 3 and prints the count
 * and two Fletcher sums (the bytes' sum, and the sum of that after each: order counts). The line is either
 * paced (Acia6551::send_paced: one byte a character time whatever the chip holds, so a byte not taken in time
 * is lost to an overrun, as on a real line) or the waiting sender (receive(), retried until taken).
 *
 * A SERTEST unit of busy is 329,728 cycles (about 46 ms) and a little; a byte at 19,200 baud 3,729 cycles.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    constexpr uint32_t BYTE_CYCLES = 3729; ///< 19,200 baud, 8-N-1
    constexpr uint32_t AREA = 0x2000;      ///< SERTEST's event area, in block 0
    constexpr unsigned EV_VBGO = 12, EV_RXCNT = 13, EV_RXCNT2 = 14; ///< oodos.inc's

    /// n bytes of a pattern with every value in it.
    std::string pattern(size_t n, unsigned seed = 0)
    {
        std::string s;
        for (size_t i = 0; i < n; ++i)
            s += static_cast<char>((i * 7 + 3 + seed) & 0xFF);
        return s;
    }
    /// SERTEST's "N nnnn A aaaa B bbbb" for these bytes.
    std::string sums(const std::string &bytes, const std::string &suffix = "")
    {
        unsigned a = 0, b = 0;
        for (const char c : bytes)
        {
            a = (a + static_cast<uint8_t>(c)) & 0xFFFF;
            b = (b + a) & 0xFFFF;
        }
        char t[64];
        std::snprintf(t, sizeof t, "%04X A%s %04X B%s %04X", static_cast<unsigned>(bytes.size()), suffix.c_str(), a,
                      suffix.c_str(), b);
        return t;
    }
    std::string ran(CcpRig &r, size_t from)
    {
        if (!r.until("\r\nA>", ms(8000), from))
            r.tail();
        return r.out.substr(from);
    }
    uint8_t rxhead(Rig &r) { return r.phys(SYS_BLOCK_PHYS + sym("RXHEAD")); }

    /// Each dispatcher pass over a stretch, from the interrupt's first cycle (7 before IRQ) to its RTI's last
    /// (IRQEND's, 6): those that took a byte into the ring and nothing else (no tick, no frame), and the
    /// longest of all.
    struct Passes
    {
        std::map<unsigned, unsigned> bytes; ///< cycles, and how many passes took them
        unsigned longest{0};
        unsigned max_byte() const { return bytes.empty() ? 0 : bytes.rbegin()->first; }
        unsigned n() const
        {
            unsigned k = 0;
            for (const auto &[c, m] : bytes)
                k += m;
            return k;
        }
        void print(const char *what) const
        {
            std::printf("  %s: longest pass %u;", what, longest);
            for (const auto &[c, m] : bytes)
                std::printf(" %u x%u", c, m);
            std::printf("\n");
        }
    };
    Passes time_passes(Rig &r, const std::function<bool()> &done, uint64_t budget)
    {
        Passes out;
        const uint16_t irq = sym("IRQ"), end = sym("IRQEND"), tick = sym("TICK");
        bool ticked = false;
        const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
        uint64_t c0 = 0;
        uint8_t head = 0, f = 0;
        uint32_t m = 0;
        bool in = false;
        r.watch = [&](uint16_t pc) {
            if (pc == tick)
                ticked = true;
            if (pc == irq)
            {
                c0 = r.cpu->cycles() - 7;
                head = rxhead(r);
                f = r.phys(frames);
                m = ms_count(r);
                ticked = false;
                in = true;
            }
            else if (pc == end && in)
            {
                const unsigned c = static_cast<unsigned>(r.cpu->cycles() + 6 - c0);
                out.longest = std::max(out.longest, c);
                if (rxhead(r) == static_cast<uint8_t>(head + 1) && r.phys(frames) == f && ms_count(r) == m && !ticked)
                    ++out.bytes[c];
                in = false;
            }
        };
        const uint64_t stop = r.cpu->cycles() + budget;
        while (!done() && r.cpu->cycles() < stop)
            r.run(10'000);
        r.watch = nullptr;
        return out;
    }

    /// backplane.machine without the VIA (and the joysticks on it): the 6551 answers all of $C300-$C37F.
    std::string no_via_machine()
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        for (const char *line : {"\nvia6522 ", "\njoyports "})
        {
            const size_t at = text.find(line);
            if (at == std::string::npos)
                return {};
            text.erase(at + 1, text.find('\n', at + 1) - at);
        }
        return text;
    }

    /// A stand-in IIe ROM (16 KB): the IIe's ID byte and a loop at reset, in a file of the test's own, removed
    /// when it goes.
    struct IieRom
    {
        std::string path = (std::filesystem::temp_directory_path() /
                            ("test_oodos_serial_" + std::to_string(::getpid()) + "_iie.rom")).string();
        IieRom()
        {
            std::vector<char> rom(0x4000, static_cast<char>(0xEA));
            rom[0x3BB3] = 0x06; // $FBB3: a IIe
            rom[0x3A00] = static_cast<char>(0x4C); // $FA00: JMP $FA00
            rom[0x3A01] = 0x00;
            rom[0x3A02] = static_cast<char>(0xFA);
            rom[0x3FFC] = 0x00; // RESET
            rom[0x3FFD] = static_cast<char>(0xFA);
            std::ofstream(path, std::ios::binary).write(rom.data(), static_cast<std::streamsize>(rom.size()));
        }
        IieRom(const IieRom &) = delete;
        IieRom &operator=(const IieRom &) = delete;
        ~IieRom() { std::remove(path.c_str()); }
    };
} // namespace

TEST(a_paced_burst_while_a_program_is_busy_arrives_whole_and_in_order)
{
    // T1: 200 bytes at 19,200 (104 ms) while SERTEST is busy for 6 units (about 280 ms): every one in the
    // ring, in order, none overrun in the chip. Before the ring, the 6551's one byte was all there was.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string data = pattern(200);
    const size_t from = start(r, "sertest.com", " R06");
    r.serial_paced(data);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums(data));
    CHECK_EQ(field(o, "S7"), std::string("00"));
    CHECK_EQ(r.acia().overruns(), 0u);
}

TEST(a_paste_longer_than_the_ring_by_the_waiting_sender_arrives_whole)
{
    // T2: 600 bytes by the waiting sender (each offered until the chip takes it, as the console and the
    // WebUI's Terminal send) into a program busy for about 0.9 s: the ring fills, the last byte waits in the
    // chip with the receiver's interrupt off, the sender waits, and reading pulls the rest through: 600 in
    // order, none dropped at full.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string data = pattern(600, 1);
    const size_t from = start(r, "sertest.com", " R14");
    for (const char c : data)
        for (int i = 0; i < 100'000 && !r.acia().receive(static_cast<uint8_t>(c)); ++i)
            r.run(200);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums(data));
    CHECK_EQ(field(o, "S7"), std::string("00"));
}

TEST(a_paced_stream_past_a_full_ring_keeps_its_first_256_and_the_chip_overruns_the_rest)
{
    // T3: 400 bytes at 19,200 (208 ms) into a program busy for 10 units (about 460 ms): 255 in the ring, the
    // 256th left in the chip with the receiver's interrupt off, and the other 144 lost to the chip's overrun,
    // as on a real line with no flow control. Nothing in the ring is written over.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string data = pattern(400, 2);
    const size_t from = start(r, "sertest.com", " R0A");
    r.serial_paced(data);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums(data.substr(0, 256)));
    CHECK_EQ(r.acia().overruns(), 144u);
}

TEST(without_the_via_a_paced_burst_arrives_whole_and_the_frame_clock_runs)
{
    // T4: no VIA, so ID 6 is the 6551's alone and the dispatcher goes to it without VIASRV: T1's burst, whole.
    const std::string text = no_via_machine();
    REQUIRE(!text.empty());
    CcpRig r({}, text);
    REQUIRE(r.ok);
    REQUIRE(r.phys(SYS_BLOCK_PHYS + sym("CLOCKS")) == 0x03); // the frame clock, and the bridge's ms (N9)
    const uint32_t frames = SYS_BLOCK_PHYS + sym("FRAMES");
    const uint8_t f0 = r.phys(frames);
    const std::string data = pattern(200, 3);
    const size_t from = start(r, "sertest.com", " R06");
    r.serial_paced(data);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums(data));
    CHECK_EQ(r.acia().overruns(), 0u);
    CHECK(static_cast<uint8_t>(r.phys(frames) - f0) >= 10);
}

TEST(a_paced_stream_costs_the_tick_nothing_and_loses_no_millisecond)
{
    // T5: the tick's own pass is as it was (162 cycles with no area: L1's 145 and B7's 17), and through a
    // second of a stream (250 bytes, one each 28,636 cycles) the ms counter keeps the cycles' count.
    CcpRig r;
    REQUIRE(r.ok);
    const uint16_t irq = sym("IRQ"), end = sym("IRQEND");
    std::map<unsigned, unsigned> ticks;
    uint64_t c0 = 0;
    uint8_t head = 0, l = 0, h = 0;
    bool in = false;
    r.watch = [&](uint16_t pc) {
        if (pc == irq)
            c0 = r.cpu->cycles() - 7, l = r.nb(8), h = r.nb(9), head = rxhead(r), in = true;
        else if (pc == end && in)
        {
            if (l == 0xFF && h == 0xDF && rxhead(r) == head) // the bridge's tick alone (N9), no byte with it
                ++ticks[static_cast<unsigned>(r.cpu->cycles() + 6 - c0)];
            in = false;
        }
    };
    const size_t from = start(r, "sertest.com", " R16");
    const uint64_t cyc0 = r.cpu->cycles();
    const uint32_t ms0 = ms_count(r);
    r.serial_paced(pattern(250, 4), 28'636);
    r.run(ms(1000));
    const double want = static_cast<double>(r.cpu->cycles() - cyc0) / 7159.09;
    const uint32_t got = ms_count(r) - ms0;
    r.watch = nullptr;
    unsigned best = 0, n = 0;
    for (const auto &[c, k] : ticks)
        if (k > n)
            best = c, n = k;
    std::printf("  a tick: %u cycles (x%u); %u ms counted in %.1f\n", best, n, got, want);
    CHECK_EQ(best, 142u); // the bridge's tick (N10; 162 with timer 1's)
    CHECK(std::abs(static_cast<double>(got) - want) <= 1);
    ran(r, from);
    CHECK_EQ(r.acia().overruns(), 0u);
}

TEST(a_byte_costs_the_dispatcher_320_cycles_at_most_and_no_pass_reaches_a_character_time)
{
    // T6: the dispatcher's pass for a byte alone (no tick, no frame with it), the interrupt's 7 and the RTI
    // included, with no event area and with a layout 1 area (EV_RXCNT written through W1): at most 320
    // cycles, the plan's gate. And no pass of any kind through the stream is as long as a character time
    // (3,729 at 19,200), or the next byte would overrun the 6551.
    for (const char *tail : {" R06", " R061"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        const size_t from = start(r, "sertest.com", tail);
        r.serial_paced(pattern(200, 5));
        const Passes p = time_passes(r, [&] { return r.acia().paced_waiting() == 0; }, ms(400));
        p.print(tail);
        CHECK(p.n() >= 150);
        CHECK(p.max_byte() <= 320);
        CHECK(p.longest < BYTE_CYCLES);
        ran(r, from);
        CHECK_EQ(r.acia().overruns(), 0u);
    }
}

TEST(apple_ii_mode_leaves_the_6551_s_receiver_interrupt_off)
{
    // T7: the 6551 isn't reset by /RESET, so leaving OODOS for Apple II mode (APPLE2: function 75, A = 1)
    // turns its receiver's interrupt off, or a byte on the line would pull /IRQ in Apple II mode.
    const IieRom iie;
    CcpRig r({iie.path});
    REQUIRE(r.ok);
    CHECK_EQ(r.acia().command() & Acia6551::CMD_RX_IRQ_OFF, 0); // on under OODOS
    r.type("APPLE2\r");
    for (int i = 0; i < 40 && !r.bridge().apple(); ++i)
        r.run(ms(50));
    REQUIRE(r.bridge().apple());
    CHECK(r.acia().command() & Acia6551::CMD_RX_IRQ_OFF);
    CHECK(r.acia().receive('x'));
    CHECK(!r.acia().irq());
}

TEST(the_warm_boot_empties_the_ring)
{
    // T8: 50 bytes, a line with its CR, come while a program is busy and it ends without reading them: the
    // warm boot empties the ring, so the CCP doesn't take them as a command, and the next program finds
    // nothing waiting.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string line = "SERIALDATAABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789XYZ\r";
    REQUIRE(line.size() == 50);
    const size_t from = start(r, "sertest.com", " Z06");
    r.serial_paced(line);
    const std::string o = ran(r, from);
    r.run(ms(300)); // the CCP's prompt waiting: anything it took would be echoed by now
    const std::string after = r.out.substr(from);
    CHECK(after.find("SERIALDATA") == std::string::npos);
    if (after.find("SERIALDATA") != std::string::npos)
        r.tail();
    const size_t q = start(r, "sertest.com", " Q");
    CHECK_EQ(field(ran(r, q), "S7"), std::string("00"));
}

TEST(a_stopped_receiver_starts_again_when_the_program_reads)
{
    // T9: T3's overflow, the receiver stopped with a byte in the chip; the program reads (256 come, the chip's
    // through SRPULL) and is busy again: 100 more bytes paced then all come, the line taking bytes again.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string first = pattern(400, 6), second = pattern(100, 7);
    const size_t from = start(r, "sertest.com", " P0A");
    r.serial_paced(first);
    REQUIRE(r.until("\r\nN ", ms(4000), from));
    r.serial_paced(second);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums(first.substr(0, 256)));
    CHECK_EQ(field(o, "N2"), sums(second, "2"));
    CHECK_EQ(r.acia().overruns(), 144u);
}

TEST(the_warm_boot_quiets_a_program_s_6551_and_keeps_oodos_s_receiver_interrupt)
{
    // T10: a program leaves the 6551's command as it likes (here the receiver's interrupt off and the
    // transmitter's on, TIC 01, against the rules): the warm boot's DEAFEN puts OODOS's back, $09 (DTR, the
    // receiver's interrupt on, TIC 10: RTS low, the transmitter's interrupt off).
    CcpRig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.acia().command(), 0x09);
    r.machine->bus().write(0xC302, 0x07); // behind OODOS's back, as a program would: DTR, the receiver's interrupt off, TIC 01
    const size_t from = start(r, "sertest.com", " Q"); // a program's end: the warm boot
    ran(r, from);
    CHECK_EQ(r.acia().command(), 0x09);
}

// ---- S3: the event area's EV_RXCNT and function 85 ------------------------------------------------------

TEST(the_event_area_s_plus_13_counts_each_byte_from_0_at_the_open)
{
    // R1: with a layout 1 area and with a layout 2 one, +13 (EV_RXCNT) counts the bytes the ring took, modulo
    // 256; +14 (COM2's) and +15 stay 0, and +12 (EV_VBGO, whose bits start vertical-blank copies) stays 0. A
    // second program's area at the same place starts again from 0.
    for (const char *layout : {"1", "2"})
    {
        CcpRig r;
        REQUIRE(r.ok);
        // Each stream once the area is open (EVON): what the ring takes before that isn't counted.
        const uint32_t evon = SYS_BLOCK_PHYS + sym("EVON");
        REQUIRE(r.phys(evon) == 0);
        size_t from = start(r, "sertest.com", std::string(" R06") + layout);
        for (int i = 0; i < 100 && !r.phys(evon); ++i)
            r.run(1000);
        r.serial_paced(pattern(200, 8));
        CHECK_EQ(field(ran(r, from), "N").substr(0, 4), std::string("00C8"));
        CHECK_EQ(r.phys(AREA + EV_RXCNT), 200);
        CHECK_EQ(r.phys(AREA + EV_RXCNT2), 0);
        CHECK_EQ(r.phys(AREA + 15), 0);
        CHECK_EQ(r.phys(AREA + EV_VBGO), 0);
        from = start(r, "sertest.com", std::string(" R02") + layout);
        for (int i = 0; i < 100 && !r.phys(evon); ++i)
            r.run(1000);
        r.serial_paced(pattern(10, 9));
        CHECK_EQ(field(ran(r, from), "N").substr(0, 4), std::string("000A"));
        CHECK_EQ(r.phys(AREA + EV_RXCNT), 10);
    }
}

TEST(with_no_event_area_nothing_is_written_where_one_was)
{
    // R2: an area opened and closed, its 256 bytes then $5A: a stream after it writes none of them.
    CcpRig r;
    REQUIRE(r.ok);
    const size_t from = start(r, "sertest.com", " R06X");
    r.serial_paced(pattern(200, 10));
    CHECK_EQ(field(ran(r, from), "N").substr(0, 4), std::string("00C8"));
    bool kept = true;
    for (unsigned i = 0; i < 256; ++i)
        kept = kept && r.phys(AREA + i) == 0x5A;
    CHECK(kept);
}

TEST(function_85_says_the_ring_s_state_and_flushes_it)
{
    // R3-R5: T3's overflow (400 paced bytes into a full ring), then SERTEST's S: SR_INFO; SR_STATUS stopped,
    // 255 waiting, no room, nothing lost yet (the chip's OVRN is seen when its byte is taken); one byte read, so
    // SRPULL takes the chip's: 255 waiting again, not stopped, 1 overrun seen; port 1 and operation 3 refused;
    // SR_FLUSH: empty, the count 0; and the line takes bytes again.
    CcpRig r;
    REQUIRE(r.ok);
    const std::string data = pattern(400, 11), after = pattern(50, 12);
    const size_t from = start(r, "sertest.com", " S0A");
    r.serial_paced(data);
    REQUIRE(r.until("\r\nSTB3 ", ms(6000), from));
    REQUIRE(r.until("\r\n", ms(500), r.out.rfind("STB3 ")));
    r.serial_paced(after);
    const std::string o = ran(r, from);
    char got[8];
    std::snprintf(got, sizeof got, "%02X", static_cast<uint8_t>(data[0]));
    CHECK_EQ(field(o, "INFO"), std::string("01 FF"));
    CHECK_EQ(field(o, "INFOB"), std::string("01FF"));
    CHECK_EQ(field(o, "ST"), std::string("FF 01"));
    CHECK_EQ(field(o, "STB"), std::string("FF00000001"));
    CHECK_EQ(field(o, "GOT"), std::string(got));
    CHECK_EQ(field(o, "ST2"), std::string("FF 00"));
    CHECK_EQ(field(o, "STB2"), std::string("FF00010000"));
    CHECK_EQ(field(o, "BAD1"), std::string("FF FF"));
    CHECK_EQ(field(o, "BAD3"), std::string("FF FF"));
    CHECK_EQ(field(o, "FL"), std::string("00 00"));
    CHECK_EQ(field(o, "ST3"), std::string("00 00"));
    CHECK_EQ(field(o, "STB3"), std::string("00FF000000"));
    CHECK_EQ(field(o, "N"), sums(after));
    CHECK_EQ(r.acia().overruns(), 144u);
}

// ---- review round 1 ------------------------------------------------------------------------------------

TEST(a_byte_in_the_flush_s_window_leaves_no_interrupt_for_deafen)
{
    // Round 1's S1: a byte that comes inside SR_FLUSH's reads (here at its first read of DATA, put there by the
    // test, as a sender a cycle sooner or later would) is dropped with the rest, and the flush leaves the
    // 6551's IRQ flag clear. Were it left set with RDRF clear, SERID6 would take the interrupt for a stray and
    // DEAFEN would turn off the USB card's events for the program's open layout-2 area.
    CcpRig r;
    REQUIRE(r.ok);
    const uint16_t flush = sym("RXFLUSH");
    const uint32_t at = 0xF8000u + (flush - 0xC000u); // block 62's code, physical
    uint16_t data_read = 0;
    for (uint16_t i = 0; i < 64 && !data_read; ++i)
        if (r.phys(at + i) == 0xAD && r.phys(at + i + 1) == 0x00 && r.phys(at + i + 2) == 0xC3) // LDA ACIA_DATA
            data_read = static_cast<uint16_t>(flush + i);
    REQUIRE(data_read != 0);
    bool injected = false;
    r.watch = [&](uint16_t pc) {
        if (!injected && pc == data_read && r.nb(3) == 62 && r.acia().command() == 0x09)
            injected = r.acia().receive(0x55);
    };
    const size_t from = start(r, "sertest.com", " S012");
    r.serial_paced("AB"); // for its function 3 (GOT); the other is flushed
    REQUIRE(r.until("\r\nINFO ", ms(3000), from));
    CHECK_EQ(r.machine->bus().peek(0xC5B0), 0x0F); // USB_IEN: the layout-2 area's events on
    REQUIRE(r.until("\r\nFL ", ms(3000), from));
    r.run(ms(5));
    r.watch = nullptr;
    CHECK(injected);
    CHECK_EQ(r.machine->bus().peek(0xC5B0), 0x0F); // still: no DEAFEN for the flush's byte
    ran(r, from);
}

TEST(a_byte_held_with_the_receive_interrupt_off_is_found_by_function_7)
{
    // Round 1's S2: the ring empty, the receiver's interrupt turned off behind OODOS's back (as the monitor's
    // $0B or a program's write leaves it, not a full ring), and a byte held in the chip, which raises nothing:
    // function 7 (TTYIST) looks at the chip itself (TTYPULL, keyed on CMD bit 1), so it says $FF and 3 gets
    // the byte; the interrupt is on again, and the line takes a stream after it.
    CcpRig r;
    REQUIRE(r.ok);
    const size_t from = start(r, "sertest.com", " P04");
    r.run(ms(20)); // SERTEST busy, CONIN: on CRT alone: nothing reads TTY now
    r.machine->bus().write(0xC302, static_cast<uint8_t>(r.acia().command() | Acia6551::CMD_RX_IRQ_OFF));
    REQUIRE(r.acia().receive('Q'));
    CHECK(!r.acia().irq());
    REQUIRE(r.until("\r\nN ", ms(4000), from));
    CHECK_EQ(r.acia().command() & Acia6551::CMD_RX_IRQ_OFF, 0);
    const std::string after = pattern(20, 13);
    r.serial_paced(after);
    const std::string o = ran(r, from);
    CHECK_EQ(field(o, "N"), sums("Q"));
    CHECK_EQ(field(o, "N2"), sums(after, "2"));
    CHECK_EQ(r.acia().overruns(), 0u);
}

int main() { return run_all(); }
