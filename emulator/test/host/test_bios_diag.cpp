/**
 * @file test_bios_diag.cpp
 * @brief The ROM's diagnostics (hw/diag.md, plan 2), D3a: the core in flash block 23 and BIOS 0.14's entries.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * backplane.machine with the BIOS as roms.cmake builds it, run as the emulator's own loop runs it (the
 * console card asked before each instruction, START a hard reset), and the panel's switches set before the
 * reset:
 *
 * - the entries: switch 19 up at reset and the prompt, answered by CONT, by Y on the serial line or the USB
 *   keyboard, by N, by nothing (the boot after 5-6 s, RAM untouched), by START (the prompt again); CONT
 *   pressed before the reset doesn't count; switch 19 down, no prompt; C812R with N and Y; $C815 with its
 *   two-byte token (A $A5, Y $5A), and without it (C815R with A $A5 still asks); a program's options from
 *   X, and the switches left up not changing them;
 * - a good quick run, golden on the serial line, the screen's form, the lights $FF, then a key boots and Esc
 *   is the monitor (with block 63 the BIOS again); its time in seconds at 7.16 MHz;
 * - the faults, each a stage's FAIL that names it: a stuck bit in block 0's first 512 bytes (W0 moves to block
 *   1), in block 41, in block 62 (OODOS's: plan 2 tests all of RAM); an address line aliased; every RAM
 *   block bad (the lights blink $82, the block, the bits); RAM B left out; a byte of flash block 30 changed;
 *   blocks 21, 22 and 23 blanked in a copy of bios.rom; no video card; no console card;
 * - LL1: a reset during P3 (the breakpoint on its address, $BD1A5) leaves it off, so the boot reaches A> and
 *   the core's own stages read that byte without halting; a user's breakpoint on another address the boot
 *   reads still halts the boot;
 * - the put-back: the VIA's ACR, IER and IFR 0 and timer 1 still, after a run.
 *
 * Each must-fail case was seen to fail on a build with its one fault put in (hw/diag.md, D3a as built).
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "console_card.h"
#include "fault_devices.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_images.h"
#include "usb_card.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint64_t HZ = 7'159'090;
    constexpr uint64_t ms(uint64_t n) { return HZ * n / 1000; }
    constexpr uint32_t BLOCK = 0x4000;
    constexpr uint32_t SCREEN = 0x82000; // the text layer's cells, card $002000, through aperture slot 0
    constexpr uint32_t P3ADDR = 0xBD1A5; // diag.inc's DG_P3ADDR
    constexpr uint16_t NB = 0xC680, VIA = 0xC340;
    constexpr uint32_t SW19 = 1u << 19, SW18 = 1u << 18, SW17 = 1u << 17, SW16 = 1u << 16;

    std::string read_text(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    std::vector<uint8_t> read_bytes(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// The label file's address for `name` (ld65 -Ln: "al 00C812 .DIAG").
    uint16_t label(const std::string &name)
    {
        static std::map<std::string, uint16_t> l;
        if (l.empty())
        {
            std::istringstream in(read_text(EMU_BIOS_ROM ".lbl"));
            std::string al, addr, sym;
            while (in >> al >> addr >> sym)
                l[sym.substr(1)] = static_cast<uint16_t>(std::stoul(addr, nullptr, 16));
        }
        const auto it = l.find(name);
        if (it == l.end())
            std::printf("  no label %s in bios.rom.lbl\n", name.c_str());
        return it == l.end() ? 0 : it->second;
    }

    /// A copy of bios.rom, changed by `edit`, in the build tree. @return its path
    std::string rom_copy(const std::string &name, const std::function<void(std::vector<uint8_t> &)> &edit)
    {
        std::vector<uint8_t> rom = read_bytes(EMU_BIOS_ROM);
        edit(rom);
        const std::string path = std::string(EMU_BIOS_ROM) + "." + name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(rom.data()),
                                                    static_cast<std::streamsize>(rom.size()));
        return path;
    }
    std::string rom_blanked(unsigned block)
    {
        return rom_copy("blank" + std::to_string(block), [block](std::vector<uint8_t> &r) {
            std::fill(r.begin() + block * BLOCK, r.begin() + (block + 1) * BLOCK, 0xFF);
        });
    }

    /// The machine file without these cards' lines.
    std::string machine_without(const std::vector<std::string> &cards)
    {
        std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
        for (const std::string &card : cards)
        {
            const size_t at = text.find("\n" + card + " ");
            if (at != std::string::npos)
                text.erase(at + 1, text.find('\n', at + 1) - at);
        }
        return text;
    }

    struct Rig
    {
        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
        std::shared_ptr<MemoryMedia> media;
        std::string out;
        std::function<void(uint16_t)> watch; ///< before each instruction
        bool halted{false};                 ///< the console card held the CPU (a breakpoint, say)

        /// `card`: an SD card image in the slot from power-on (AUTOBOOT boots it), or none.
        explicit Rig(uint32_t switches = 0, const std::string &rom = EMU_BIOS_ROM, const std::string &text = {},
                     std::vector<uint8_t> card = {})
        {
            MachineOptions opt;
            opt.rom_override = rom;
            opt.rtc_time = Ds3231::Time{2026, 11, 14, 12, 0, 0, 6};
            opt.video_revision = 2;
            std::string error;
            machine = Machine::build(text.empty() ? read_text(EMU_MACHINE_DIR "/backplane.machine") : text,
                                     "backplane.machine", opt, error);
            if (machine)
                console = make_console(*machine, error);
            if (!console)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            cpu = std::make_unique<Cpu6502>(machine->bus(), Cpu6502::Model::W65C02S);
            if (ConsoleCard *c = panel())
            {
                cpu->set_observer(c);
                c->set_switches(switches);
            }
            if (!card.empty())
            {
                media = std::make_shared<MemoryMedia>(std::move(card), "card");
                machine->sd_card()->insert(media, false);
            }
            machine->bus().reset();
            cpu->reset();
        }
        bool ok() const { return cpu != nullptr; }
        ConsoleCard *panel() const { return machine->console_card(); }

        /// Run `cycles` as the emulator's loop does: START a hard reset, the panel's halts kept. Stops early
        /// if the CPU is held (halted).
        void run(uint64_t cycles)
        {
            const uint64_t end = cpu->cycles() + cycles;
            ConsoleCard *card = panel();
            halted = false;
            while (cpu->cycles() < end)
            {
                if (card && card->take_start())
                {
                    machine->bus().reset();
                    cpu->reset();
                }
                if (card && !card->may_run(cpu->regs().pc))
                {
                    halted = true;
                    break;
                }
                if (watch)
                    watch(cpu->regs().pc);
                cpu->step();
                if (machine->bus().irq())
                    cpu->irq();
                if (UsbCard *usb = machine->usb_card(); usb && usb->take_nmi())
                    cpu->nmi();
                if (cpu->cycles() % 64 > 7)
                    continue;
                console->service();
                uint8_t ch = 0;
                while (console->output(ch))
                    out += static_cast<char>(ch);
            }
        }
        /// Run until the serial line has `text` after `from`. @return whether it came within `budget`
        bool until(const std::string &text, uint64_t budget, size_t from = 0)
        {
            const uint64_t end = cpu->cycles() + budget;
            while (out.find(text, from) == std::string::npos && cpu->cycles() < end && !halted)
                run(20'000);
            return out.find(text, from) != std::string::npos;
        }
        /// A key on the serial line, taken.
        void key(char k)
        {
            for (int i = 0; i < 2000 && !console->key(static_cast<uint8_t>(k)); ++i)
                run(1'000);
            run(30'000);
        }
        void type(const std::string &keys)
        {
            for (const char k : keys)
                key(k);
        }
        /// A letter on the USB keyboard: down, then up.
        void usb_letter(char ch)
        {
            UsbCard *usb = machine->usb_card();
            const uint8_t usage = static_cast<uint8_t>(0x04 + ch - 'A');
            usb->key(usage, true);
            run(30'000);
            usb->key(usage, false);
            run(30'000);
        }
        uint8_t io(uint16_t addr) const { return machine->bus().peek(addr); }
        uint8_t phys(uint32_t a) const { return machine->bus().peek_physical(a); }
        void poke(uint32_t a, uint8_t v) { machine->bus().write_physical(a, v); }
        /// The text screen's row, trailing blanks dropped.
        std::string row(unsigned r) const
        {
            std::string t;
            for (unsigned c = 0; c < 80; ++c)
                t += static_cast<char>(phys(SCREEN + r * 160 + c * 2));
            return t.substr(0, t.find_last_not_of(' ') + 1);
        }
        bool screen_has(const std::string &text) const
        {
            for (unsigned r = 0; r < 30; ++r)
                if (row(r).find(text) != std::string::npos)
                    return true;
            return false;
        }
        /// The data lamps as a byte, while CTRL's bit 3 shows DISPLAY on them (a lamp over half lit is 1).
        uint8_t lights()
        {
            const ConsoleCard::Lights l = panel()->lights();
            uint8_t v = 0;
            for (unsigned i = 0; i < 8; ++i)
                if (l.data[i] > 0.5f)
                    v = static_cast<uint8_t>(v | 1u << i);
            return v;
        }
    };

    const std::string PROMPT = "DIAGNOSTICS: PRESS CONT OR Y WITHIN 5 S";
    const std::string GREETING = "BACKPLANE BIOS 0.14\r";
    const std::string RESULT = "RESULT: ";

    /// Switch 19 up, the reset, the prompt, CONT: the core runs. @return whether the prompt came
    bool enter(Rig &r)
    {
        if (!r.until(PROMPT, ms(500)))
            return false;
        r.panel()->press(ConsoleCard::CONT);
        return r.until(" 1  FLASH", ms(500));
    }
    /// Run on to the result line and the keys' line after it. @return whether they came within `budget`
    bool to_result(Rig &r, uint64_t budget = ms(90'000))
    {
        return r.until("EXAM OR ESC: MONITOR\r", budget);
    }
    /// The serial line from the title on.
    std::string report(const Rig &r)
    {
        const size_t at = r.out.find("OO-1 ROM DIAGNOSTICS");
        return at == std::string::npos ? r.out : r.out.substr(at);
    }
    /// The stage line for stage n on the serial line, to its CR.
    std::string stage_line(const Rig &r, unsigned n)
    {
        const std::string head = (n < 10 ? " " : "") + std::to_string(n) + "  ";
        const std::string text = report(r);
        size_t at = text.find("\r" + head);
        if (at == std::string::npos)
            return "<no stage " + std::to_string(n) + ">";
        at += 1;
        return text.substr(at, text.find('\r', at) - at);
    }
    bool ends(const std::string &line, const std::string &word)
    {
        return line.size() >= word.size() && line.compare(line.size() - word.size(), word.size(), word) == 0;
    }
    std::string golden_path() { return std::string(EMU_GOLDEN_DIR) + "/romdiag.txt"; }
} // namespace

// --- The entries ----------------------------------------------------------------------------------------

TEST(switch_19_and_cont_enter_the_core_and_a_good_run_is_golden)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    REQUIRE(r.until(PROMPT, ms(500)));
    CHECK_EQ(r.lights(), 0xA5);
    CHECK(r.screen_has(PROMPT));
    r.panel()->press(ConsoleCard::CONT);
    const uint64_t from = r.cpu->cycles();
    REQUIRE(to_result(r));
    const double seconds = static_cast<double>(r.cpu->cycles() - from) / HZ;
    std::printf("  a quick run (stages 0-14, 7-13 SKIP in D3a): %.1f s at 7.16 MHz\n", seconds);
    CHECK(seconds > 10 && seconds < 40);
    const std::string got = report(r);
    const std::string want = read_text(golden_path());
    if (got != want)
    {
        std::ofstream(golden_path() + ".got", std::ios::binary) << got;
        std::printf("  the report differs from golden/romdiag.txt: see romdiag.txt.got\n");
    }
    CHECK(got == want);
    CHECK_EQ(r.lights(), 0xFF);
    CHECK(r.row(0).find("OO-1 ROM DIAGNOSTICS 0.1") == 0);
    CHECK(r.row(0).find("PASS 1") != std::string::npos);
    CHECK(r.row(3).find(" 1  FLASH BLOCKS 21-31") == 0 && ends(r.row(3), "PASS"));
    CHECK(ends(r.row(8), "PASS")); // stage 6
    CHECK(ends(r.row(9), "SKIP")); // stage 7, D3b's
    CHECK(r.row(20).find("RESULT: ALL PASSED") == 0);
    // a key: the boot, the greeting
    const size_t at = r.out.size();
    r.key('\r');
    CHECK(r.until(GREETING, ms(8000), at));
}

TEST(y_on_the_serial_line_or_the_usb_keyboard_enters_and_n_boots)
{
    {
        Rig r(SW19);
        REQUIRE(r.until(PROMPT, ms(500)));
        r.key('y');
        CHECK(r.until(" 1  FLASH", ms(500)));
    }
    {
        Rig r(SW19);
        REQUIRE(r.until(PROMPT, ms(500)));
        r.usb_letter('Y');
        CHECK(r.until(" 1  FLASH", ms(500)));
    }
    {
        Rig r(SW19);
        REQUIRE(r.until(PROMPT, ms(500)));
        const uint64_t asked = r.cpu->cycles();
        r.key('N');
        CHECK(r.until(GREETING, ms(1500)));
        CHECK(r.cpu->cycles() - asked < ms(1500));
        CHECK(r.out.find(" 1  FLASH") == std::string::npos);
        CHECK_EQ(r.panel()->peek(0x06) & 0x08, 0); // CTRL's bit 3 off: the lamps the bus's again
    }
}

TEST(no_answer_boots_within_6_s_with_ram_untouched)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    for (uint32_t a = 0; a < 0x100000; a += 0x1000) // a pattern in every 4 KB of RAM
        r.poke(a + 0x123, static_cast<uint8_t>(a >> 12));
    const uint16_t boot = label("BOOT");
    bool reached = false;
    bool kept = true;
    r.watch = [&](uint16_t pc) {
        if (reached || pc != boot)
            return;
        reached = true;
        for (uint32_t a = 0; a < 0x100000; a += 0x1000)
            if ((a >> 14) < 32 || (a >> 14) >= 36) // bank 8 is the aperture: card memory
                kept = kept && r.phys(a + 0x123) == static_cast<uint8_t>(a >> 12);
    };
    REQUIRE(r.until(PROMPT, ms(500)));
    const uint64_t asked = r.cpu->cycles();
    while (!reached && r.cpu->cycles() - asked < ms(8000))
        r.run(ms(10));
    CHECK(reached);
    const double s = static_cast<double>(r.cpu->cycles() - asked) / HZ;
    std::printf("  the prompt waited %.2f s\n", s);
    CHECK(s > 5.0 && s < 6.5);
    CHECK(kept);
    CHECK(r.until(GREETING, ms(8000)));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(cont_pressed_before_the_reset_doesnt_count)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    r.panel()->press(ConsoleCard::CONT);
    r.machine->bus().reset(); // the reset clears KEYS
    r.cpu->reset();
    REQUIRE(r.until(PROMPT, ms(500)));
    r.run(ms(1000));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(start_at_the_prompt_is_a_hard_reset_and_the_prompt_again)
{
    Rig r(SW19);
    REQUIRE(r.until(PROMPT, ms(500)));
    const size_t first = r.out.find(PROMPT);
    r.panel()->press_start();
    CHECK(r.until(PROMPT, ms(1000), first + PROMPT.size()));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(switch_19_down_no_prompt)
{
    Rig r(SW18 | SW17 | SW16);
    REQUIRE(r.until(GREETING, ms(8000)));
    CHECK(r.out.find(PROMPT) == std::string::npos);
}

TEST(c812r_asks_first)
{
    Rig r;
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500)); // past the splash's wait for a key (AUTOBOOT), at the monitor
    size_t at = r.out.size();
    r.type("C812R\r");
    REQUIRE(r.until("ALL OF MEMORY WILL BE OVERWRITTEN. Y TO GO ON", ms(500), at));
    r.key('N');
    CHECK(r.until("\\", ms(500), at));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
    at = r.out.size();
    r.type("C812R\r");
    REQUIRE(r.until("Y TO GO ON", ms(500), at));
    r.key('Y');
    CHECK(r.until(" 1  FLASH", ms(500), at));
}

/// At the monitor, `C815R`, with A, X and Y set so when the jump arrives at $C815.
void c815(Rig &r, uint8_t a, uint8_t x, uint8_t y)
{
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500));
    r.watch = [&r, a, x, y, done = false](uint16_t pc) mutable {
        if (pc != 0xC815 || done)
            return;
        done = true;
        Registers g = r.cpu->regs();
        g.a = a;
        g.x = x;
        g.y = y;
        r.cpu->set_regs(g);
    };
    r.type("C815R\r");
}

TEST(c815r_typed_with_a_a5_but_no_second_byte_still_asks)
{
    Rig r;
    REQUIRE(r.ok());
    const size_t at = r.out.size();
    c815(r, 0xA5, 0x00, 0x00); // Y is not $5A
    CHECK(r.until("Y TO GO ON", ms(500), at));
    r.run(ms(500));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(c815_with_the_token_runs_a_programs_options_whatever_the_switches)
{
    {
        Rig r;
        REQUIRE(r.ok());
        c815(r, 0xA5, 0x01, 0x5A); // full
        CHECK(r.until(" 2  ZERO", ms(2000)));
        CHECK(r.out.find("Y TO GO ON") == std::string::npos);
        CHECK(r.until("FULL", ms(500)));
    }
    {
        Rig r(SW18 | SW17 | SW16); // full, loop and interactive on the panel: a program's run doesn't take them
        REQUIRE(r.ok());
        c815(r, 0xA5, 0x00, 0x5A);
        REQUIRE(to_result(r));
        const std::string text = report(r);
        CHECK(text.find("PASS 1  QUICK\r") != std::string::npos);
        CHECK(text.find("PASS 2") == std::string::npos);
        CHECK(text.find(" LOOP") == std::string::npos);
        CHECK(text.find(" ASK") == std::string::npos);
    }
}

TEST(esc_at_the_end_is_the_monitor_with_the_bios_copied_again)
{
    Rig r;
    REQUIRE(r.ok());
    c815(r, 0xA5, 0x00, 0x5A);
    REQUIRE(to_result(r));
    const size_t at = r.out.size();
    r.key(0x1B);
    REQUIRE(r.until(GREETING, ms(2000), at));
    CHECK(r.until("\\", ms(500), at));
    CHECK(r.out.find("BOOTING THE SD CARD", at) == std::string::npos);
    const size_t m = r.out.size();
    r.type("C809R\r"); // the RAM copy answers: the monitor runs from block 63 again
    CHECK(r.until("\\", ms(3000), m + 6));
    CHECK_EQ(r.io(NB + 3), 0x40 | 63); // W3 on the copy, read-only
}

TEST(the_via_is_put_back)
{
    Rig r;
    REQUIRE(r.ok());
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500));
    // as OODOS leaves it: timer 1 free-running, its interrupt on
    r.machine->bus().write(VIA + 0x0B, 0x40);
    r.machine->bus().write(VIA + 0x04, 0x00);
    r.machine->bus().write(VIA + 0x05, 0x10);
    r.machine->bus().write(VIA + 0x0E, 0xC0);
    c815(r, 0xA5, 0x00, 0x5A);
    REQUIRE(to_result(r));
    const uint16_t boot = label("BOOT");
    bool reached = false;
    r.watch = [&](uint16_t pc) { reached = reached || pc == boot; };
    r.key('\r');
    for (int i = 0; i < 100 && !reached; ++i)
        r.run(1000);
    REQUIRE(reached);
    r.run(100'000);
    CHECK_EQ(r.io(VIA + 0x0B), 0x00); // ACR
    CHECK_EQ(r.io(VIA + 0x0E) & 0x7F, 0x00); // IER
    CHECK_EQ(r.io(VIA + 0x0D), 0x00); // IFR: timer 1 ran out once, before the flags were cleared
}

// --- Faults -----------------------------------------------------------------------------------------

/// A run through $C815 (quick) with `setup` done first. @return the rig, run to its result
void faulted_run(Rig &r)
{
    c815(r, 0xA5, 0x00, 0x5A);
    const bool ended = to_result(r);
    CHECK(ended);
    if (!ended)
        std::printf("  the serial line: %s\n", r.out.substr(r.out.size() > 1500 ? r.out.size() - 1500 : 0).c_str());
}

TEST(a_stuck_bit_in_block_0s_first_512_bytes_moves_w0_to_block_1)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_at(*r.machine, f, 0x00123, 0xFF, 0x08));
    faulted_run(r);
    CHECK(stage_line(r, 2).find("ZERO PAGE AND STACK: BLOCK 1 ") != std::string::npos);
    CHECK(ends(stage_line(r, 2), "FAIL"));
    CHECK(ends(stage_line(r, 5), "FAIL"));
    CHECK(report(r).find("BLOCK 0 +0123 (00123): READ ") != std::string::npos);
    CHECK_EQ(r.lights(), 0xC0 + 3); // stages 2, 5 and 6
}

TEST(a_stuck_bit_in_block_41_and_in_oodoss_block_62)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f, g;
    REQUIRE(fault_at(*r.machine, f, 41 * BLOCK + 0x0123, 0xFF, 0x08));
    REQUIRE(fault_at(*r.machine, g, 62 * BLOCK + 0x2345, 0xFE, 0x00));
    faulted_run(r);
    const std::string text = report(r);
    CHECK(ends(stage_line(r, 6), "FAIL")); // address in address happens to write those bits as the faults hold them
    CHECK(text.find("BLOCK 41 +0123 (A4123): READ ") != std::string::npos);
    CHECK(text.find("BLOCK 62 +2345 (FA345): READ ") != std::string::npos);
    CHECK(r.row(18).find("BLOCK 41 +0123 (A4123)") != std::string::npos);
    CHECK(r.row(20).find("RESULT: 1 FAILED") == 0);
}

TEST(an_aliased_address_line_is_named)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    f.alias_mask = 0x0020; // A5 stuck at 0 in block 40
    REQUIRE(fault_over(*r.machine, f, 40 * BLOCK));
    faulted_run(r);
    CHECK(ends(stage_line(r, 4), "FAIL"));
    CHECK(report(r).find("ADDRESS LINE A5") != std::string::npos);
}

TEST(ram_b_left_out_is_not_answering)
{
    Rig r(0, EMU_BIOS_ROM, machine_without({"ram   40000-7FFFF"}));
    REQUIRE(r.ok());
    faulted_run(r);
    CHECK(ends(stage_line(r, 3), "FAIL"));
    CHECK(report(r).find("RAM B (BLOCKS 16-31): NOT ANSWERING") != std::string::npos);
    CHECK(ends(stage_line(r, 4), "PASS")); // its blocks aren't tested again
    CHECK(ends(stage_line(r, 5), "PASS"));
}

TEST(every_ram_block_bad_blinks_82_the_block_and_the_bits)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    std::vector<std::unique_ptr<FaultyMemory>> faults;
    for (uint32_t b = 0; b < 64; ++b)
    {
        if (b >= 32 && b < 36)
            continue;
        auto f = std::make_unique<FaultyMemory>();
        f->or_mask = 0x10;
        f->bad = b * BLOCK; // the first byte of each: stage 2's march sees it
        REQUIRE(fault_over(*r.machine, *f, b * BLOCK));
        faults.push_back(std::move(f));
    }
    REQUIRE(enter(r));
    std::vector<uint8_t> seen;
    for (int i = 0; i < 40; ++i)
    {
        r.run(ms(100));
        if (seen.empty() || seen.back() != r.lights())
            seen.push_back(r.lights());
    }
    bool code = false, block = false, bits = false;
    for (const uint8_t v : seen)
        code = code || v == 0x82, block = block || v == 0x00, bits = bits || v == 0x10;
    CHECK(code && block && bits);
    CHECK(r.out.find(" 2  ZERO") == std::string::npos);
}

TEST(a_byte_of_flash_block_30_changed_fails_stage_1)
{
    const std::string rom = rom_copy("block30", [](std::vector<uint8_t> &b) { b[30 * BLOCK + 0x3000] ^= 0x01; });
    Rig r(0, rom);
    REQUIRE(r.ok());
    faulted_run(r);
    CHECK(ends(stage_line(r, 1), "FAIL"));
    CHECK(report(r).find("FLASH BLOCK $1E: ITS SUM IS WRONG") != std::string::npos);
    CHECK(ends(stage_line(r, 2), "PASS"));
}

TEST(block_22_blanked_its_stages_not_in_the_flash_and_the_run_ends)
{
    Rig r(0, rom_blanked(22));
    REQUIRE(r.ok());
    faulted_run(r);
    CHECK(ends(stage_line(r, 7), "SKIP"));
    CHECK(report(r).find("NOT IN THE FLASH") != std::string::npos);
    CHECK(report(r).find("NOT IN THIS BUILD YET") != std::string::npos); // block 21's, stages 11-13
    CHECK(report(r).find("FLASH BLOCK $16: ITS SUM IS WRONG") != std::string::npos);
    CHECK(r.out.find("RESULT: 1 FAILED") != std::string::npos);
}

TEST(block_21_blanked_block_22_ends_the_run)
{
    Rig r(0, rom_blanked(21));
    REQUIRE(r.ok());
    faulted_run(r);
    const std::string text = report(r);
    const size_t s11 = text.find("\r11  ");
    REQUIRE(s11 != std::string::npos);
    CHECK(text.find("NOT IN THE FLASH", s11) != std::string::npos);
    CHECK(ends(stage_line(r, 10), "SKIP"));
    CHECK(text.find("FLASH BLOCK $15: ITS SUM IS WRONG") != std::string::npos); // stage 1 sees the erased block too
    CHECK(text.find("RESULT: 1 FAILED") != std::string::npos);
}

TEST(block_23_blanked_no_diagnostics_then_the_boot)
{
    {
        Rig r(SW19, rom_blanked(23));
        REQUIRE(r.ok());
        CHECK(r.until(GREETING, ms(8000)));
        CHECK(r.until("NO DIAGNOSTICS IN THE FLASH", ms(500)));
        CHECK(r.out.find(PROMPT) == std::string::npos);
    }
    {
        Rig r(0, rom_blanked(23));
        REQUIRE(r.until(GREETING, ms(8000)));
        r.run(ms(2500));
        const size_t at = r.out.size();
        r.type("C812R\r");
        r.key('Y');
        CHECK(r.until("NO DIAGNOSTICS IN THE FLASH", ms(500), at));
    }
}

TEST(no_video_card_serial_only_and_no_console_card_boots_as_now)
{
    {
        Rig r(0, EMU_BIOS_ROM, machine_without({"video", "aperture"}));
        REQUIRE(r.ok());
        faulted_run(r);
        CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
        CHECK(report(r).find("ADDRESS IN ADDRESS") != std::string::npos);
    }
    {
        Rig r(SW19, EMU_BIOS_ROM, machine_without({"console"}));
        REQUIRE(r.ok());
        CHECK(r.until(GREETING, ms(8000)));
        CHECK(r.out.find(PROMPT) == std::string::npos);
    }
}

// --- LL1: P3's breakpoint, and only P3's, turned off at reset -------------------------------------------

/// Arm a read breakpoint on physical `addr` (BPADDR, then BPCTRL: enable, halt on a read).
void arm(Rig &r, uint32_t addr)
{
    ConsoleCard *c = r.panel();
    c->write(0x0B, 0x00);
    c->write(0x08, static_cast<uint8_t>(addr));
    c->write(0x09, static_cast<uint8_t>(addr >> 8));
    c->write(0x0A, static_cast<uint8_t>(addr >> 16));
    c->write(0x0B, ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ); // halt on a read of it
}

TEST(a_reset_during_p3_leaves_the_breakpoint_off_and_the_boot_reaches_a_prompt)
{
    Rig r(0, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(r.ok());
    arm(r, P3ADDR); // as P3 leaves it when a reset comes in the middle
    r.panel()->press_start();
    CHECK(r.until("A>", ms(20'000)));
    CHECK(!r.halted);
    CHECK_EQ(r.panel()->peek(0x0B), 0x00);
    // and the core, which reads $BD1A5 in stages 4-6, runs to its end
    Rig s(SW19);
    REQUIRE(s.ok());
    arm(s, P3ADDR);
    s.machine->bus().reset();
    s.cpu->reset();
    REQUIRE(enter(s));
    CHECK(to_result(s));
    CHECK(!s.halted);
}

TEST(a_users_breakpoint_on_another_address_the_boot_reads_still_halts_it)
{
    Rig r;
    REQUIRE(r.ok());
    const uint32_t greeting = 0xFC000 + (label("GREETING") - 0xC000); // its byte in the BIOS's RAM copy
    arm(r, greeting);
    r.panel()->press_start();
    r.run(ms(8000));
    CHECK(r.halted);
    CHECK((r.panel()->peek(0x0C) & ConsoleCard::HIT) != 0); // STATUS's HIT
    CHECK_EQ(r.panel()->peek(0x0B), ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ);
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
