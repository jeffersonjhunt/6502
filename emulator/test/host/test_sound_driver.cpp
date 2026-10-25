/**
 * @file test_sound_driver.cpp
 * @brief A real driver on the sound card's 6510 (test/host/sound/driver.s).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The driver is assembled with ca65 at build time, loaded into the card
 * through RAMDATA as a host program would, and started with CTRL RUN |
 * RESET; then the host's W65C02S runs and the card keeps the SID clock's
 * time beside it. The driver counts frames on CIA 1's 50 Hz interrupt with
 * ISC, LAX, SAX and DCP, banks I/O out to reach the RAM under $D400, and
 * plays notes on voice 1 when the mailbox says, answering each command.
 * Without cc65 it skips.
 */

#include "harness.h"
#include "test_support.h"

#include "cpu6502.h"
#include "machine.h"
#include "sound_card.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    int skipped = 0;
    constexpr uint16_t SOUND = 0xC500;
    constexpr uint8_t FRAMES = 0xF0, COPY = 0xF1, COUNTDN = 0xF2;

    std::vector<uint8_t> driver()
    {
        std::ifstream in(EMU_SOUND_DRIVER, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    /// A backplane with the sound card, the driver loaded at $1000 and
    /// started, and a W65C02S spinning in RAM at $0200.
    struct Board
    {
        explicit Board(uint8_t ctrl = 0)
        {
            std::string error;
            machine = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                                     "sound 500-57F space=io name=sound irq\n",
                                     "sound.machine", {}, error);
            sound = machine->sound_card();
            Decoder &bus = machine->bus();
            put(0x1000);
            for (const uint8_t b : driver())
                bus.write(SOUND + SoundCard::RAMDATA, b);
            put(0xFFFC);
            bus.write(SOUND + SoundCard::RAMDATA, 0x00);
            bus.write(SOUND + SoundCard::RAMDATA, 0x10);
            bus.write(SOUND + SoundCard::IEN, SoundCard::IEN_MAILBOX);
            bus.poke(0x0200, 0x4C); // JMP $0200
            bus.poke(0x0201, 0x00);
            bus.poke(0x0202, 0x02);
            cpu = std::make_unique<Cpu6502>(bus, Cpu6502::Model::W65C02S);
            cpu->set_regs(Registers{0x0200, 0xFF, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});
            bus.write(SOUND + SoundCard::CTRL,
                      static_cast<uint8_t>(SoundCard::CTRL_RUN | SoundCard::CTRL_RESET | ctrl));
        }
        void put(uint16_t addr)
        {
            machine->bus().write(SOUND + SoundCard::RAMADDR, static_cast<uint8_t>(addr));
            machine->bus().write(SOUND + SoundCard::RAMADDR + 1, static_cast<uint8_t>(addr >> 8));
        }
        /// The host for `seconds` of its 7.16 MHz, sampling IRQ as the run loop does.
        void run(double seconds)
        {
            const uint64_t until = cpu->cycles() + static_cast<uint64_t>(seconds * SoundCard::NATIVE_HZ);
            while (cpu->cycles() < until)
            {
                cpu->step();
                machine->bus().irq(); // which also brings every timed card up to date
            }
        }
        /// A mailbox command, and the reply once the driver has sent it.
        int command(uint8_t c)
        {
            Decoder &bus = machine->bus();
            bus.write(SOUND + SoundCard::MBOUT, c);
            for (int i = 0; i < 100 && !bus.irq(); ++i)
                run(0.0001);
            if (!bus.irq())
                return -1;
            return bus.read(SOUND + SoundCard::MBIN);
        }
        std::unique_ptr<Machine> machine;
        SoundCard *sound{nullptr};
        std::unique_ptr<Cpu6502> cpu;
    };
} // namespace

TEST(the_driver_counts_50_hz_frames_with_undocumented_opcodes)
{
    if (driver().empty())
        return void(++skipped);
    Board b;
    b.run(1.0);
    const uint8_t frames = b.sound->ram()[FRAMES];
    CHECK(frames >= 49 && frames <= 50); // one second at 50 Hz, less the setup
    CHECK_EQ(b.sound->ram()[COPY], frames);
    CHECK_EQ(b.sound->ram()[COUNTDN], static_cast<uint8_t>(0xFF - frames));
    CHECK_EQ(b.sound->timer_latch(), 19704);
    // The host reads the count through RAMDATA while the card runs.
    b.put(FRAMES);
    CHECK_EQ(b.machine->bus().read(SOUND + SoundCard::RAMDATA), frames);
}

TEST(on_the_ntsc_clock_the_same_driver_runs_fast)
{
    if (driver().empty())
        return void(++skipped);
    Board b(SoundCard::CTRL_NTSC);
    b.run(1.0);
    const uint8_t frames = b.sound->ram()[FRAMES];
    CHECK(frames >= 51 && frames <= 52); // 1,022,727 / 19,705
}

TEST(the_driver_banks_io_out_and_sets_up_the_sid)
{
    if (driver().empty())
        return void(++skipped);
    Board b;
    b.run(0.01);
    CHECK_EQ(b.sound->ram()[0xD400], 0x5A); // under I/O
    CHECK_EQ(b.sound->sid(0)[0x00], 0x00);
    CHECK_EQ(b.sound->sid(0)[0x18], 0x0F);
    CHECK_EQ(b.sound->sid(0)[0x05], 0x09);
    CHECK_EQ(b.sound->sid(0)[0x06], 0xA0);
    CHECK_EQ(b.sound->port_ddr(), 0x2F);
    CHECK_EQ(b.sound->port_data(), 0x37);
}

TEST(mailbox_commands_play_and_release_notes)
{
    if (driver().empty())
        return void(++skipped);
    Board b;
    b.run(0.01);
    Decoder &bus = b.machine->bus();
    CHECK_EQ(b.command(2), 0x82);
    CHECK(!bus.irq()); // reading MBIN took it
    const auto &sid = b.sound->sid(0);
    CHECK_EQ(sid[0x00] | sid[0x01] << 8, 8911); // C5
    CHECK_EQ(sid[0x04], 0x21);                   // saw, gate
    CHECK_EQ(b.command(0), 0x80);
    CHECK_EQ(sid[0x04], 0x20); // released
    CHECK_EQ(b.command(9), 0x89); // not a note: answered, nothing played
    CHECK_EQ(sid[0x00] | sid[0x01] << 8, 8911);
    // The host's own SID writes are dropped while the card runs.
    bus.write(SOUND + 0x18, 0x00);
    CHECK_EQ(sid[0x18], 0x0F);
}

int main()
{
    const int rc = run_all();
    if (rc == 0 && skipped == static_cast<int>(ooe::test::registry().size()))
    {
        std::printf("SKIP: the driver was not built (needs ca65/ld65 on PATH at configure time)\n");
        return SKIP;
    }
    return rc;
}
