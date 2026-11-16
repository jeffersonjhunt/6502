/**
 * @file test_bios_diag.cpp
 * @brief The ROM's diagnostics (hw/diag.md, plan 2): D3a's core in flash block 23 and BIOS 0.14's entries, D3b's card tests.
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
 * - the put-back: the VIA's ACR, IER and IFR 0 and timer 1 still, after a run;
 * - D3b, the card tests (stages 7-13, blocks 22 and 21): a good machine's, with their measurements in bounds
 *   and an SD card in the slot; a stuck bit in the sound card's RAM (SoundCard::set_ram_fault, EMU_FAULT_HOOKS)
 *   named by S2; the 6510's program not starting (S4 fails within 0.5 s, S5-S7 SKIP); an aperture fault (V6,
 *   not V4); a stuck bit under DMA named by D2 as stage 6 names it; no sound card, no DMA controller; the SD
 *   card's image unchanged by a full run (answer 5); CC5's floating bus in the Disk II's empty slot; a boot to
 *   A> never touching P3's $BD1A5; the interactive tests (switch 16) with every answer given, P3's halt and
 *   CONT; Break during P3 leaving the breakpoint off.
 *
 * Each must-fail case was seen to fail on a build with its one fault put in (hw/diag.md, D3a and D3b as built).
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "console_card.h"
#include "fault_devices.h"
#include "joystick_ports.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_images.h"
#include "sound_card.h"
#include "usb_card.h"

#include <cctype>
#include <cstdio>
#include <cstring>
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
                     std::vector<uint8_t> card = {}, unsigned video_revision = 2)
        {
            MachineOptions opt;
            opt.rom_override = rom;
            opt.rtc_time = Ds3231::Time{2026, 11, 14, 12, 0, 0, 6};
            opt.video_revision = video_revision;
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
    const std::string GREETING = "BACKPLANE BIOS 0.15\r";
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
    std::string masked(const std::string &text);
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
    std::printf("  a quick run (stages 0-14): %.1f s at 7.16 MHz\n", seconds);
    CHECK(seconds > 30 && seconds < 75); // hw/diag.md's table: about 40 s; D3b as built: 56.5 s (the serial line's pace)
    const std::string got = masked(report(r)); // the measurements' digits masked (D3b): their bounds are checked below
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
    CHECK(ends(r.row(9), "PASS")); // stage 7 (D3b)
    CHECK(ends(r.row(15), "SKIP")); // stage 13: not interactive
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
        CHECK(r.until(" 1  FLASH", ms(2000)));
        const uint64_t from = r.cpu->cycles();
        CHECK(r.out.find("Y TO GO ON") == std::string::npos);
        CHECK(r.until("FULL", ms(500)));
        REQUIRE(to_result(r, ms(300'000)));
        const double seconds = static_cast<double>(r.cpu->cycles() - from) / HZ;
        std::printf("  a full run (stages 0-14): %.1f s at 7.16 MHz\n", seconds);
        CHECK(report(r).find("BACKGROUNDS 00 55 33 0F") != std::string::npos);
        CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
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

TEST(esc_at_the_end_is_the_monitor_and_return_boots_the_card)
{
    // With OODOS's card in the slot, so the boot and the monitor can't be told apart by accident (review MM1):
    // Esc must reach the monitor, with the BIOS copied again, and never OODOS's A>; RETURN must reach A>.
    {
        Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
        REQUIRE(enter(r));
        REQUIRE(to_result(r));
        const size_t at = r.out.size();
        r.key(0x1B);
        REQUIRE(r.until(GREETING, ms(2000), at));
        CHECK(r.until("\\", ms(500), at));
        r.run(ms(8000));
        CHECK(r.out.find("A>", at) == std::string::npos);
        CHECK(r.out.find("BOOTING THE SD CARD", at) == std::string::npos);
        const size_t m = r.out.size();
        r.type("C809R\r"); // the RAM copy answers: the monitor runs from block 63 again
        CHECK(r.until("\\", ms(3000), m + 6));
        CHECK_EQ(r.io(NB + 3), 0x40 | 63); // W3 on the copy, read-only
    }
    {
        Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
        REQUIRE(enter(r));
        REQUIRE(to_result(r));
        const size_t at = r.out.size();
        r.key('\r');
        CHECK(r.until("A>", ms(20'000), at));
    }
    {
        Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
        REQUIRE(enter(r));
        REQUIRE(to_result(r));
        const size_t at = r.out.size();
        r.panel()->press(ConsoleCard::CONT);
        CHECK(r.until("A>", ms(20'000), at));
    }
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
    CHECK_EQ(r.lights(), 0xC0 + 4); // stages 2, 5 and 6, and 8's D2 (D3b: DMA's fill of block 0 too)
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
    CHECK(r.row(20).find("RESULT: 2 FAILED") == 0); // stage 6, and 8's D2 (D3b: block 62's bit under DMA too)
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

TEST(block_22_blanked_its_stages_not_in_the_flash_and_block_21_runs_on)
{
    Rig r(0, rom_blanked(22));
    REQUIRE(r.ok());
    faulted_run(r);
    CHECK(ends(stage_line(r, 7), "SKIP"));
    CHECK(report(r).find("NOT IN THE FLASH") != std::string::npos);
    CHECK(ends(stage_line(r, 11), "PASS")); // block 21's stages run still
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
    CHECK(ends(stage_line(r, 10), "PASS")); // block 22's last stage (D3b), before it ends the run itself
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

// --- Review MM2-MM11 --------------------------------------------------------------------------------

TEST(a9_stuck_on_ram_a_w0_goes_to_ram_b_and_the_run_ends)
{
    // A9 stuck at 0 in every RAM A block: +$200-$3FF answer as +$000-$1FF, W0's zero page and stack included,
    // which a 512-byte march alone can't see (review MM2).
    Rig r;
    REQUIRE(r.ok());
    std::vector<std::unique_ptr<FaultyMemory>> f;
    for (uint32_t b = 0; b < 16; ++b)
    {
        f.push_back(std::make_unique<FaultyMemory>());
        f.back()->alias_mask = 0x0200;
        REQUIRE(fault_over(*r.machine, *f.back(), b * BLOCK));
    }
    faulted_run(r);
    CHECK(stage_line(r, 2).find("ZERO PAGE AND STACK: BLOCK 16 ") != std::string::npos);
    CHECK(report(r).find("ADDRESS LINE A9") != std::string::npos);
    CHECK(report(r).find("RESULT: ") != std::string::npos);
}

TEST(a18_stuck_ram_b_answers_as_ram_a_and_w0_goes_to_ram_c)
{
    Rig r;
    REQUIRE(r.ok());
    std::vector<std::unique_ptr<FaultyMemory>> f;
    for (uint32_t b = 16; b < 32; ++b)
    {
        f.push_back(std::make_unique<FaultyMemory>());
        REQUIRE(fault_redirect(*r.machine, *f.back(), b * BLOCK, (b - 16) * BLOCK));
    }
    faulted_run(r);
    // (Review NN1: stage 2 no longer probes the block lines, so W0 is block 0, and stage 4 finds RAM B's
    // blocks answering as RAM A's, block 16 as W0's own.)
    CHECK(stage_line(r, 2).find("ZERO PAGE AND STACK: BLOCK 0 ") != std::string::npos);
    CHECK(report(r).find("BLOCK 16 ANSWERS AS BLOCK 0, W0'S: NOT TESTED") != std::string::npos);
    CHECK(report(r).find("BLOCKS 1 AND 17 ANSWER AS ONE") != std::string::npos);
    CHECK(report(r).find("RESULT: ") != std::string::npos);
}

TEST(a_block_answering_as_w0s_is_said_and_not_tested)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_redirect(*r.machine, f, 40 * BLOCK, 0)); // block 40 is block 0, W0's
    faulted_run(r);
    CHECK(report(r).find("BLOCK 40 ANSWERS AS BLOCK 0, W0'S: NOT TESTED") != std::string::npos);
    CHECK(ends(stage_line(r, 4), "FAIL"));
    CHECK(report(r).find("RESULT: ") != std::string::npos);
}

TEST(a_cross_block_alias_is_two_blocks_as_one)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_redirect(*r.machine, f, 20 * BLOCK, 10 * BLOCK));
    faulted_run(r);
    CHECK(report(r).find("BLOCKS 10 AND 20 ANSWER AS ONE") != std::string::npos);
    CHECK(ends(stage_line(r, 4), "FAIL"));
}

TEST(a_mismatched_version_in_block_23_is_no_diagnostics)
{
    for (const uint32_t at : {23 * BLOCK + 0x09u, 23 * BLOCK + 0x1Du}) // DG_VERSION; the BIOS it was linked with
    {
        const std::string rom = rom_copy("version" + std::to_string(at & 0xFF), [at](std::vector<uint8_t> &b) { ++b[at]; });
        Rig r(SW19, rom);
        REQUIRE(r.ok());
        CHECK(r.until(GREETING, ms(8000)));
        CHECK(r.until("THE DIAGNOSTICS IN THE FLASH ARE ANOTHER BUILD'S: WRITE THE FLASH AGAIN", ms(500)));
        CHECK(r.out.find("NO DIAGNOSTICS IN THE FLASH") == std::string::npos); // review NN4: its own message
        CHECK(r.out.find(PROMPT) == std::string::npos);
    }
}

TEST(the_prompt_times_out_by_the_cpus_count_when_the_clock_doesnt_tick)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    r.watch = [&r](uint16_t) { r.machine->bus().write(NB + 7, 0x80); }; // TICK cleared as soon as it's set
    REQUIRE(r.until(PROMPT, ms(500)));
    const uint64_t asked = r.cpu->cycles();
    REQUIRE(r.until("THE CLOCK CHIP DIDN'T TICK", ms(10'000)));
    const double s = static_cast<double>(r.cpu->cycles() - asked) / HZ;
    std::printf("  the prompt counted %.2f s by the CPU\n", s);
    CHECK(s > 6.5 && s < 7.5);
    r.watch = nullptr;
    CHECK(r.until(GREETING, ms(8000)));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(break_at_the_prompt_is_no)
{
    Rig r(SW19);
    REQUIRE(r.until(PROMPT, ms(500)));
    r.cpu->nmi();
    CHECK(r.until("BREAK", ms(500)));
    CHECK(r.until(GREETING, ms(8000)));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(break_in_stage_5_and_in_block_22_boots)
{
    {
        Rig r(SW19);
        REQUIRE(enter(r));
        REQUIRE(r.until(" 5  ADDRESS IN", ms(30'000)));
        r.cpu->nmi();
        const size_t at = r.out.size();
        CHECK(r.until("BREAK", ms(500), at));
        CHECK(r.until(GREETING, ms(8000), at));
        CHECK_EQ(r.io(NB + 4), 0x40 | 63); // VEC on the BIOS's copy again
    }
    {
        Rig r(SW19);
        REQUIRE(enter(r));
        REQUIRE(r.until(" 7  NORTH", ms(60'000)));
        CHECK_EQ(r.io(NB + 2), 0xC0 | 22); // W2 on block 22
        r.cpu->nmi();
        const size_t at = r.out.size();
        CHECK(r.until("BREAK", ms(500), at));
        CHECK(r.until(GREETING, ms(8000), at));
    }
}

TEST(c815_with_no_diagnostics_returns_carry_set_and_a_x_y_and_w2_kept)
{
    Rig r(0, rom_blanked(23));
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500));
    // $0300: W2 on block 5; LDA #$A5, LDY #$5A, LDX #$02, JSR $C815; then P, A, X, Y and W2 kept at
    // $0280-$0284, W2 back on block 2, and the monitor (review NN3)
    const uint16_t mon = label("WOZMON");
    const std::vector<uint8_t> prog = {
        0xA9, 0x05, 0x8D, 0x82, 0xC6,             // LDA #5, STA NB_WIN2
        0xA9, 0xA5, 0xA0, 0x5A, 0xA2, 0x02,       // LDA #$A5, LDY #$5A, LDX #$02
        0x20, 0x15, 0xC8,                         // JSR $C815
        0x08, 0x8D, 0x81, 0x02, 0x8E, 0x82, 0x02, // PHP, STA $0281, STX $0282
        0x8C, 0x83, 0x02, 0x68, 0x8D, 0x80, 0x02, // STY $0283, PLA, STA $0280
        0xAD, 0x82, 0xC6, 0x8D, 0x84, 0x02,       // LDA NB_WIN2, STA $0284
        0xA9, 0x02, 0x8D, 0x82, 0xC6,             // LDA #2, STA NB_WIN2
        0x4C, static_cast<uint8_t>(mon), static_cast<uint8_t>(mon >> 8)};
    for (size_t i = 0; i < prog.size(); ++i)
        r.poke(0x0300 + static_cast<uint32_t>(i), prog[i]);
    for (uint32_t a = 0x0280; a <= 0x0284; ++a)
        r.poke(a, 0xEE);
    const size_t at = r.out.size();
    r.type("300R\r");
    r.run(ms(500));
    CHECK((r.phys(0x0280) & 0x01) != 0); // carry
    CHECK_EQ(r.phys(0x0281), 0xA5);
    CHECK_EQ(r.phys(0x0282), 0x02);
    CHECK_EQ(r.phys(0x0283), 0x5A);
    CHECK_EQ(r.phys(0x0284), 0x05); // W2 as the program had it
    CHECK(r.out.find("Y TO GO ON", at) == std::string::npos);
}

TEST(a_loop_runs_two_passes_and_a_key_ends_it_after_the_second)
{
    Rig r(SW19 | SW17);
    REQUIRE(enter(r));
    REQUIRE(r.until("PASS 2  QUICK LOOP", ms(60'000)));
    r.key(' ');
    REQUIRE(to_result(r));
    const std::string text = report(r);
    CHECK(text.find("PASS 3") == std::string::npos);
    CHECK(r.row(0).find("PASS 2") != std::string::npos);
}

TEST(the_wait_states_and_the_vias_ier_are_put_back)
{
    Rig r;
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500));
    for (uint16_t w = 0; w < 4; ++w)
        r.machine->bus().write(static_cast<uint16_t>(NB + 0x0A + w), 0x55);
    bool armed = false;
    c815(r, 0xA5, 0x00, 0x5A);
    auto before = r.watch;
    r.watch = [&](uint16_t pc) {
        if (before)
            before(pc);
        if (!armed && r.out.find(" 6  MARCH") != std::string::npos)
        {
            armed = true; // an interrupt enabled in the middle of the run, after stage 0's quiet
            r.machine->bus().write(VIA + 0x0E, 0xA0);
        }
    };
    REQUIRE(to_result(r));
    CHECK(armed);
    const uint16_t boot = label("BOOT");
    bool reached = false;
    r.watch = [&](uint16_t pc) { reached = reached || pc == boot; };
    r.key('\r');
    for (int i = 0; i < 100 && !reached; ++i)
        r.run(1000);
    REQUIRE(reached);
    for (uint16_t w = 0; w < 4; ++w)
        CHECK_EQ(r.io(static_cast<uint16_t>(NB + 0x0A + w)), 0x00);
    CHECK_EQ(r.io(VIA + 0x0E) & 0x7F, 0x00);
}

TEST(cont_pressed_after_the_reset_before_the_prompt_reads_keys_doesnt_count)
{
    Rig r(SW19);
    REQUIRE(r.ok());
    const uint16_t reset = label("RESET");
    bool pressed = false;
    r.watch = [&](uint16_t pc) {
        if (!pressed && pc == reset)
        {
            pressed = true;
            r.panel()->press(ConsoleCard::CONT);
        }
    };
    r.panel()->press_start();
    REQUIRE(r.until(PROMPT, ms(500)));
    CHECK(pressed);
    r.run(ms(1000));
    CHECK(r.out.find(" 1  FLASH") == std::string::npos);
}

TEST(c812r_takes_no_options_left_in_wait3)
{
    // WAIT3 is the diagnostics' (review MM10): a program's bits left there (full, and "from a program") must not
    // change a run asked for at the monitor.
    Rig r;
    REQUIRE(r.until(GREETING, ms(8000)));
    r.run(ms(2500));
    r.machine->bus().write(NB + 0x0D, 0x41);
    const size_t at = r.out.size();
    r.type("C812R\r");
    REQUIRE(r.until("Y TO GO ON", ms(500), at));
    r.key('Y');
    CHECK(r.until("PASS 1  QUICK", ms(3000), at));
}

// --- Review round 2: NN1, NN2, NN5 ------------------------------------------------------------------

TEST(a18_stuck_backplane_wide_still_finds_a_zero_page_and_says_the_aliasing)
{
    // Half of RAM answering as the other half: A18 stuck at 0 on every module (video revision 1, so bank 8 is
    // RAM C's). Stage 2's block-line probes rejected every block here (FATAL2); stage 4 finds it (review NN1).
    Rig r(0, EMU_BIOS_ROM, {}, {}, 1);
    REQUIRE(r.ok());
    std::vector<std::unique_ptr<FaultyMemory>> f;
    for (uint32_t b = 0; b < 64; ++b)
        if (b & 16)
        {
            f.push_back(std::make_unique<FaultyMemory>());
            REQUIRE(fault_redirect(*r.machine, *f.back(), b * BLOCK, (b - 16) * BLOCK));
        }
    faulted_run(r);
    CHECK(stage_line(r, 2).find("ZERO PAGE AND STACK: BLOCK 0 ") != std::string::npos);
    CHECK(report(r).find("BLOCK 16 ANSWERS AS BLOCK 0, W0'S: NOT TESTED") != std::string::npos);
    CHECK(report(r).find("BLOCKS 1 AND 17 ANSWER AS ONE") != std::string::npos);
    CHECK(report(r).find("RESULT: ") != std::string::npos);
}

TEST(a_floating_bus_where_ram_b_is_left_out_is_not_answering)
{
    // No RAM B line, and an empty slot's floating bus in its place: its reads aren't $FF but differ between
    // CC5's two reads, so it is absent, its blocks aren't tested and no data line is blamed (review NN2).
    Rig r(0, EMU_BIOS_ROM, machine_without({"ram   40000-7FFFF"}));
    REQUIRE(r.ok());
    FloatingMemory fl;
    REQUIRE(float_over(*r.machine, *r.cpu, fl, 0x40000));
    faulted_run(r);
    CHECK(report(r).find("RAM B (BLOCKS 16-31): NOT ANSWERING") != std::string::npos);
    CHECK(report(r).find(": DATA LINES: READ") == std::string::npos);
    CHECK(ends(stage_line(r, 4), "PASS"));
    CHECK(ends(stage_line(r, 5), "PASS"));
    CHECK(ends(stage_line(r, 6), "PASS"));
}

TEST(a_block_stage_2_passed_over_is_never_w0s_spare)
{
    // Block 0's first 512 bytes bad while stage 2 looks, then good: stage 2 takes block 1, and block 0, though
    // it passes stages 4-6, must not be the block W0 moves to for its own test (review MM7, NN5).
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_at(*r.machine, f, 0x00123, 0xFF, 0x08));
    c815(r, 0xA5, 0x00, 0x5A);
    auto before = r.watch;
    r.watch = [&](uint16_t pc) {
        if (before)
            before(pc);
        if (f.or_mask && r.out.find(" 2  ZERO") != std::string::npos)
            f.or_mask = 0; // the fault gone once stage 2 is over
    };
    REQUIRE(to_result(r));
    CHECK(stage_line(r, 2).find("BLOCK 1 ") != std::string::npos);
    CHECK(ends(stage_line(r, 6), "PASS"));
    const uint8_t w0 = r.io(NB + 0);
    std::printf("  W0 moved to block %u\n", w0);
    CHECK(w0 != 0 && w0 != 1);
}

TEST(break_stops_the_sound_cards_6510)
{
    Rig r(SW19);
    REQUIRE(enter(r));
    REQUIRE(r.until(" 5  ADDRESS IN", ms(30'000)));
    r.machine->bus().write(0xC540, static_cast<uint8_t>(r.io(0xC540) | 0x02)); // the 6510 running, as a program left it
    const uint16_t boot = label("BOOT");
    bool reached = false;
    uint8_t ctrl = 0xFF;
    r.watch = [&](uint16_t pc) {
        if (!reached && pc == boot)
            reached = true, ctrl = r.io(0xC540); // as Break's put-back left it, before the boot's splash runs it again
    };
    r.cpu->nmi();
    for (int i = 0; i < 400 && !reached; ++i)
        r.run(1000);
    REQUIRE(reached);
    CHECK(r.out.find("BREAK") != std::string::npos);
    CHECK_EQ(ctrl & 0x02, 0); // SOUND_CTRL's RUN
}

TEST(a_pass_count_above_255_is_printed_whole)
{
    Rig r(SW19 | SW17);
    REQUIRE(enter(r));
    REQUIRE(r.until(" 3  CPU", ms(5000)));
    r.poke(0x00012, 0x2B); // DG_PASS = 299, in W0's block 0
    r.poke(0x00013, 0x01);
    REQUIRE(r.until("PASS 300  QUICK LOOP", ms(60'000)));
    r.key(' ');
    CHECK(to_result(r));
}

// --- D3b: the card tests (stages 7-13) -----------------------------------------------------------------

namespace
{
    /// Test `name`'s line on the serial line (its first), to its CR: "    NB5  CPU ... PASS".
    std::string test_line(const Rig &r, const std::string &name)
    {
        const std::string text = report(r);
        size_t at = text.find("\r    " + name + " ");
        if (at == std::string::npos)
            return "<no test " + name + ">";
        at += 1;
        return text.substr(at, text.find('\r', at) - at);
    }
    /// The serial line after test `name`'s line, to the next test's or stage's: its detail lines.
    std::string after_test(const Rig &r, const std::string &name)
    {
        const std::string text = report(r);
        size_t at = text.find("\r    " + name + " ");
        if (at == std::string::npos)
            return {};
        at = text.find('\r', at + 1);
        size_t end = at;
        while (end != std::string::npos && text.compare(end + 1, 9, "         ") == 0)
            end = text.find('\r', end + 1);
        return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }
    /// The number after `before` on test `name`'s line.
    uint64_t measured(const Rig &r, const std::string &name, const std::string &before)
    {
        const std::string line = test_line(r, name);
        const size_t at = line.find(before);
        return at == std::string::npos ? 0 : std::stoull(line.substr(at + before.size()));
    }
    /// The golden report with what the run measures masked: a run of four or more digits on the lines of
    /// NB5, NB6, V3, D5 and S9 becomes "#", so a change of the code's timing by a few cycles isn't a diff.
    std::string masked(const std::string &text)
    {
        std::string out;
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\r', at);
            end = end == std::string::npos ? text.size() : end + 1;
            std::string line = text.substr(at, end - at);
            for (const char *t : {"    NB5 ", "    NB6 ", "    V3 ", "    D5 ", "    S9 "})
                if (line.compare(0, std::strlen(t), t) == 0)
                {
                    std::string m;
                    for (size_t i = 0; i < line.size();)
                    {
                        size_t j = i;
                        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j])))
                            ++j;
                        if (j - i >= 4)
                            m += '#', i = j;
                        else if (j > i)
                            m += line.substr(i, j - i), i = j;
                        else
                            m += line[i++];
                    }
                    line = m;
                }
            out += line;
            at = end;
        }
        return out;
    }

    /// A device over a RAM block that counts the bus's accesses, the CPU's and DMA's, to one physical byte.
    struct Spy : FaultyMemory
    {
        uint32_t watched{0};
        unsigned reads{0}, writes{0};
        uint8_t read(uint32_t offset) override
        {
            reads += first + offset == watched;
            return FaultyMemory::read(offset);
        }
        void write(uint32_t offset, uint8_t data) override
        {
            writes += first + offset == watched;
            FaultyMemory::write(offset, data);
        }
    };

    /// Wait for `text` after `from` on the serial line; then `act`. @return where the text was found, or npos
    size_t on(Rig &r, const std::string &text, size_t from, const std::function<void()> &act, uint64_t budget)
    {
        if (!r.until(text, budget, from))
        {
            std::printf("  never came: %s\n", text.c_str());
            return std::string::npos;
        }
        const size_t at = r.out.find(text, from);
        act();
        return at + text.size();
    }
} // namespace

TEST(the_card_stages_pass_on_a_good_machine_with_their_measurements_in_bounds)
{
    // The golden run's measurements, read back: the clock within 0.002% by the DS3231 (the emulator's counts
    // cycles) and 0.01% by the frame, as SYSINFO's tests hold them; the frame, DMA's 2 cycles a byte, the
    // stream's 8,000 frames a second; and an SD card in the slot this time, so SD2-SD4 run.
    Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(enter(r));
    REQUIRE(to_result(r));
    for (unsigned n = 7; n <= 12; ++n)
        CHECK(ends(stage_line(r, n), "PASS"));
    CHECK(ends(stage_line(r, 13), "SKIP"));
    const uint64_t hz = measured(r, "NB5", "CPU "), fhz = measured(r, "NB6", "CPU ");
    std::printf("  NB5 %llu Hz, NB6 %llu Hz, V3 %llu cycles, S9 %llu frames, SD4 %llu KB/s\n",
                static_cast<unsigned long long>(hz), static_cast<unsigned long long>(fhz),
                static_cast<unsigned long long>(measured(r, "V3", "A FRAME ")),
                static_cast<unsigned long long>(measured(r, "S9", "8000 HZ: ")),
                static_cast<unsigned long long>(measured(r, "SD4", "SPEED: ")));
    CHECK(hz > HZ - HZ / 50000 && hz < HZ + HZ / 50000);
    CHECK(fhz > HZ - HZ / 10000 && fhz < HZ + HZ / 10000);
    CHECK(measured(r, "D4", "FILL: ") == 4096);
    const uint64_t frame = measured(r, "V3", "A FRAME "), frames = measured(r, "S9", "8000 HZ: "),
                   left = measured(r, "D5", "PART WAY: ");
    // V3's own frame, by HW_CLOCK's poll (about 80 cycles a pass): one change seen late at each end, so up to a
    // pass long; 119,460-119,475 in the runs seen (final check PP1)
    CHECK(frame >= 119437 && frame <= 119520);
    CHECK(frames >= 2051 - 2 && frames <= 2051 + 2);
    CHECK(left > 4096 - 16 && left < 4096);
    CHECK(ends(test_line(r, "SD2"), "PASS"));
    CHECK(test_line(r, "SD2").find("FAT") != std::string::npos);
    CHECK(ends(test_line(r, "SD3"), "PASS"));
    CHECK(ends(test_line(r, "SD4"), "INFO"));
    CHECK(measured(r, "SD4", "SPEED: ") > 1000 && measured(r, "SD4", "SPEED: ") < 6991); // DMA's 1 cycle a byte at most; 16/19 with bursts of 16 (the takeover, the poll, the CPU's cycle)
    CHECK(ends(test_line(r, "DK1"), "PASS"));
    CHECK(report(r).find("TESTS: ") != std::string::npos);
    CHECK(report(r).find(" 0 FAILED, ") != std::string::npos);
    CHECK(r.row(19).find("TESTS: ") == 0);
    CHECK(r.row(11).find(" 9  VIDEO CARD") == 0 && ends(r.row(11), "PASS")); // the screen drawn again after V4-V7
    CHECK(r.row(3).find(" 1  FLASH BLOCKS 21-31") == 0);
}

TEST(a_stuck_bit_in_the_sound_cards_ram_is_named_by_s2)
{
    Rig r;
    REQUIRE(r.ok());
    r.machine->sound_card()->set_ram_fault(0x5678, 0xFB, 0x00); // bit 2 stuck at 0
    faulted_run(r);
    CHECK(ends(test_line(r, "S2"), "FAIL"));
    CHECK(after_test(r, "S2").find("CARD RAM $5678: READ E9, WROTE ED, BITS 2") != std::string::npos);
    CHECK(ends(test_line(r, "S4"), "PASS"));
    CHECK(ends(stage_line(r, 10), "FAIL"));
}

TEST(the_6510_program_not_running_fails_s4_within_half_a_second_and_skips_s5_to_s7)
{
    // Its first opcode reads $02 (a jam on the NMOS 6510): the start byte never comes.
    Rig r;
    REQUIRE(r.ok());
    r.machine->sound_card()->set_ram_fault(0x0200, 0x00, 0x02);
    uint64_t asked = 0, failed = 0;
    c815(r, 0xA5, 0x00, 0x5A);
    auto before = r.watch;
    r.watch = [&](uint16_t pc) {
        if (before)
            before(pc);
        if (!asked && r.out.find("S4   THE 6510") != std::string::npos)
            asked = r.cpu->cycles();
        if (asked && !failed && r.out.find("NO START BYTE") != std::string::npos)
            failed = r.cpu->cycles();
    };
    REQUIRE(to_result(r));
    CHECK(ends(test_line(r, "S4"), "FAIL"));
    CHECK(after_test(r, "S4").find("NO START BYTE WITHIN 0.5 S") != std::string::npos);
    for (const char *t : {"S5", "S6", "S7"})
    {
        CHECK(ends(test_line(r, t), "SKIP"));
        CHECK(after_test(r, t).find("THE 6510 DIDN'T START (S4)") != std::string::npos);
    }
    const double s = static_cast<double>(failed - asked) / HZ;
    std::printf("  S4 gave up after %.2f s\n", s);
    CHECK(asked && failed && s > 0.45 && s < 0.65); // its wait and the line's own characters
}

TEST(an_aperture_fault_fails_v6_and_not_v4)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_at(*r.machine, f, 0x84123, 0xF7, 0x00)); // aperture slot 1 (block 33), bit 3 stuck at 0
    faulted_run(r);
    CHECK(ends(test_line(r, "V4"), "PASS")); // V4 goes through slot 0 alone
    CHECK(ends(test_line(r, "V6"), "FAIL"));
    CHECK(after_test(r, "V6").find("CARD $004123: READ 30, WROTE 38, BITS 3") != std::string::npos);
    CHECK(ends(stage_line(r, 9), "FAIL"));
}

TEST(a_stuck_bit_under_dma_is_named_by_d2_as_stage_6_names_it)
{
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_at(*r.machine, f, 41 * BLOCK + 0x0123, 0xF7, 0x00)); // bit 3 stuck at 0
    faulted_run(r);
    CHECK(ends(stage_line(r, 6), "FAIL"));
    CHECK(ends(test_line(r, "D2"), "FAIL"));
    CHECK(after_test(r, "D2").find("BLOCK 41 +0123 (A4123): READ 84, WROTE 8C, BITS 3") != std::string::npos);
    CHECK(ends(test_line(r, "D3"), "PASS"));
    CHECK(ends(stage_line(r, 8), "FAIL"));
}

TEST(no_sound_card_skips_stage_10_and_no_dma_skips_stage_8_v7_s3_and_sd3)
{
    {
        Rig r(0, EMU_BIOS_ROM, machine_without({"sound"}));
        REQUIRE(r.ok());
        faulted_run(r);
        CHECK(ends(stage_line(r, 10), "SKIP"));
        CHECK(report(r).find("10  SOUND CARD ....") != std::string::npos);
        CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
    }
    {
        Rig r(SW19, EMU_BIOS_ROM, machine_without({"dma"}), sd_image("oodos.sparse"));
        REQUIRE(enter(r));
        REQUIRE(to_result(r));
        CHECK(ends(stage_line(r, 8), "SKIP"));
        for (const char *t : {"V7", "S3", "SD3", "SD4"})
        {
            CHECK(ends(test_line(r, t), "SKIP"));
            CHECK(after_test(r, t).find("NO DMA CONTROLLER") != std::string::npos);
        }
        CHECK(test_line(r, "NB3").find("NO DMA CONTROLLER") != std::string::npos);
        CHECK(ends(test_line(r, "SD2"), "PASS"));
        CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
    }
}

TEST(a_full_run_never_writes_the_sd_card)
{
    // The owner's answer 5: the ROM never writes the card. Its image byte for byte before and after a full run.
    std::vector<uint8_t> card = sd_image("oodos.sparse");
    const std::vector<uint8_t> was = card;
    Rig r(SW19 | SW18, EMU_BIOS_ROM, {}, std::move(card));
    REQUIRE(enter(r));
    const uint64_t from = r.cpu->cycles();
    REQUIRE(to_result(r, ms(300'000)));
    std::printf("  a full run: %.1f s at 7.16 MHz\n", static_cast<double>(r.cpu->cycles() - from) / HZ);
    CHECK(report(r).find("QUICK") == std::string::npos);
    CHECK(ends(test_line(r, "SD3"), "PASS"));
    CHECK(!r.media->dirty());
    CHECK(r.media->bytes() == was);
    CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
}

TEST(a_floating_bus_in_the_disk_iis_empty_slot_is_no_card)
{
    // Review CC5, with card discovery: no Disk II line, and an empty slot's floating bus in its block, which
    // answers each read with the instruction's last byte ($C6 for LDA $C67F): HW_PROBE's second read differs.
    Rig r(0, EMU_BIOS_ROM, machine_without({"disk2"}));
    REQUIRE(r.ok());
    FloatingMemory f;
    f.cpu = r.cpu.get();
    f.bus = &r.machine->bus();
    f.bytes = 0x80;
    Mapping m;
    m.first = bus_address(Space::Io, 0x600);
    m.last = bus_address(Space::Io, 0x67F);
    m.device = &f;
    m.module = 0xC;
    REQUIRE(r.machine->bus().map(m));
    faulted_run(r);
    CHECK(ends(test_line(r, "DK1"), "SKIP"));
    CHECK(after_test(r, "DK1").find("NO CARD") != std::string::npos);
    CHECK(report(r).find("REVISION 6") == std::string::npos);
}

TEST(a_boot_to_a_prompt_never_touches_p3s_address)
{
    // hw/diag.md, LL1: P3's $BD1A5 was chosen as an address nothing reads in a boot to A>; the bus's every
    // access to it, the CPU's and DMA's, counted from the reset to OODOS's prompt.
    Rig r(0, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(r.ok());
    Spy spy;
    spy.watched = P3ADDR;
    REQUIRE(fault_over(*r.machine, spy, P3ADDR));
    REQUIRE(r.until("A>", ms(20'000)));
    std::printf("  $BD1A5: %u reads, %u writes in the boot\n", spy.reads, spy.writes);
    CHECK_EQ(spy.reads, 0u);
    CHECK_EQ(spy.writes, 0u);
}

TEST(the_interactive_tests_with_switch_16_every_answer_given)
{
    Rig r(SW19 | SW16);
    REQUIRE(enter(r));
    UsbCard *usb = r.machine->usb_card();
    JoystickPorts *joy = r.machine->joysticks();
    REQUIRE(usb && joy);
    size_t at = 0;
    const auto yes = [&r] { r.key('y'); };
    at = on(r, "DID ONE LIGHT WALK ACROSS? (Y OR N) ", at, yes, ms(90'000));
    REQUIRE(at != std::string::npos);
    bool card = false;
    at = on(r, "GRID? (Y OR N) ", at, [&] {
        card = r.io(0xC480) == 0x05 && r.phys(SCREEN + 0x1400) == '+'; // layer 1 alone; its map at card $3400
        yes();
    }, ms(5000));
    CHECK(card);
    at = on(r, "ES5503? (Y OR N) ", at, yes, ms(10'000));
    at = on(r, "(ESC ON THE TERMINAL: SKIP)", at, [&] { r.run(ms(5)); r.usb_letter('A'); }, ms(5000));
    const uint64_t toggles = usb->speaker_toggles(); // (U5 starts as soon as U4 has all it wants)
    at = on(r, "PADDLE BUTTONS 0 AND 1 (ESC: SKIP)", at, [&] {
        r.run(ms(5)); // past the line: U4 drops what the mouse had summed before it
        usb->mouse_move(5, 3);
        r.run(ms(20));
        for (const int b : {1, 2, 0})
            usb->mouse_buttons(static_cast<uint8_t>(b)), r.run(ms(20));
        for (const int b : {1, 2, 0})
            usb->set_game_buttons(static_cast<uint8_t>(b)), r.run(ms(20));
    }, ms(5000));
    at = on(r, "DID IT SOUND? (Y OR N) ", at, yes, ms(5000));
    std::printf("  U5 toggled the speaker %llu times\n", static_cast<unsigned long long>(usb->speaker_toggles() - toggles));
    CHECK(usb->speaker_toggles() - toggles >= 2000);
    at = on(r, "EACH WAY AND FIRE (ESC: SKIP)", at, [&] {
        r.run(ms(5));
        for (unsigned bit = 0; bit < 5; ++bit)
        {
            joy->set(0, static_cast<uint8_t>(1u << bit));
            joy->set(1, static_cast<uint8_t>(1u << bit));
            r.run(ms(20));
        }
        joy->set(0, 0);
        joy->set(1, 0);
    }, ms(5000));
    at = on(r, "RETURN, THEN CONT WHEN IT HALTS ", at, [&] { r.key('\r'); }, ms(5000));
    REQUIRE(at != std::string::npos);
    for (int i = 0; i < 200 && !r.halted; ++i)
        r.run(ms(10));
    CHECK(r.halted); // the read of $BD1A5 holds the CPU
    CHECK((r.panel()->peek(0x0C) & ConsoleCard::HIT) != 0);
    r.panel()->press(ConsoleCard::CONT);
    r.run(1000); // (run() clears `halted`, which until() stops on)
    CHECK(!r.halted);
    REQUIRE(to_result(r));
    for (const char *t : {"P2", "V8", "S10", "U3", "U4", "U5", "SR3", "P3"})
    {
        CHECK(ends(test_line(r, t), "PASS"));
        if (!ends(test_line(r, t), "PASS"))
            std::printf("  %s%s\n", test_line(r, t).c_str(), after_test(r, t).c_str());
    }
    CHECK(ends(stage_line(r, 13), "PASS"));
    CHECK_EQ(r.panel()->peek(0x0B), 0x00); // BPCTRL off again
    CHECK(report(r).find("RESULT: ALL PASSED") != std::string::npos);
}

TEST(break_during_p3_leaves_the_breakpoint_off)
{
    Rig r(SW19 | SW16);
    REQUIRE(enter(r));
    size_t at = 0;
    const auto yes = [&r] { r.key('y'); };
    const auto esc = [&r] { r.key(0x1B); };
    at = on(r, "DID ONE LIGHT WALK ACROSS? (Y OR N) ", at, yes, ms(90'000));
    at = on(r, "GRID? (Y OR N) ", at, yes, ms(5000));
    at = on(r, "ES5503? (Y OR N) ", at, yes, ms(10'000));
    at = on(r, "U3   A KEY", at, esc, ms(5000));
    at = on(r, "U4   MOVE THE MOUSE", at, esc, ms(5000));
    at = on(r, "DID IT SOUND? (Y OR N) ", at, yes, ms(5000));
    at = on(r, "SR3  BOTH JOYSTICKS", at, esc, ms(5000));
    REQUIRE(r.until("RETURN, THEN CONT WHEN IT HALTS ", ms(5000), at));
    CHECK_EQ(r.panel()->peek(0x0B), ConsoleCard::BP_ENABLE | ConsoleCard::BP_READ); // armed, waiting for RETURN
    const size_t brk = r.out.size();
    r.cpu->nmi();
    CHECK(r.until("BREAK", ms(500), brk));
    CHECK(r.until(GREETING, ms(8000), brk));
    CHECK_EQ(r.panel()->peek(0x0B), 0x00);
    CHECK(!r.halted);
}

// --- D3b review round 1: P1-P3 ---------------------------------------------------------------------------

namespace
{
    /// The SD card's registers, forwarded, with STATUS's BUSY held on while `stuck`; it notes a CMD written
    /// while stuck and CTRL's RESET (review P2).
    struct BusySd : Device
    {
        Device *under{nullptr};
        bool stuck{false}, reset{false};
        unsigned cmds{0};
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t o) override { return held(o, under->read(o)); }
        uint8_t peek(uint32_t o) const override { return held(o, under->peek(o)); }
        void write(uint32_t o, uint8_t d) override
        {
            cmds += stuck && o == 0x03;
            reset = reset || (o == 0x00 && (d & 0x02));
            under->write(o, d);
        }
        uint8_t held(uint32_t o, uint8_t v) const { return stuck && o == 0x01 ? static_cast<uint8_t>(v | 0x80) : v; }
    };
    bool busy_over(Rig &r, BusySd &b)
    {
        b.under = r.machine->sd_card();
        Mapping m;
        m.first = bus_address(Space::Io, 0x380);
        m.last = bus_address(Space::Io, 0x3FF);
        m.device = &b;
        m.module = 0x7;
        return b.under && r.machine->bus().map(m);
    }
    /// At the BIOS's BOOT after a Break: `check` called there, once. @return whether BOOT came
    bool at_boot(Rig &r, const std::function<void()> &check)
    {
        const uint16_t boot = label("BOOT");
        bool reached = false;
        r.watch = [&r, &reached, boot, check](uint16_t pc) {
            if (!reached && pc == boot)
                reached = true, check();
        };
        for (int i = 0; i < 4000 && !reached; ++i)
            r.run(1000);
        r.watch = nullptr;
        return reached;
    }
} // namespace

TEST(break_during_s10s_first_tone_leaves_the_sids_silent_at_the_boot)
{
    // Review P1: Break's way out quiets every card as stage 0 does, so a SID can't drone on into the boot.
    Rig r(SW19 | SW16);
    REQUIRE(enter(r));
    size_t at = 0;
    at = on(r, "DID ONE LIGHT WALK ACROSS? (Y OR N) ", at, [&r] { r.key('y'); }, ms(90'000));
    at = on(r, "GRID? (Y OR N) ", at, [&r] { r.key('y'); }, ms(5000));
    REQUIRE(r.until("S10  A TONE ON SID A, ON SID B", ms(5000), at));
    r.run(ms(150)); // in SID A's half second
    REQUIRE((r.io(0xC504) & 0x01) != 0); // its gate on: the tone sounding
    r.cpu->nmi();
    uint8_t vol = 0xFF, gate = 0xFF;
    REQUIRE(at_boot(r, [&] { vol = r.io(0xC518), gate = r.io(0xC504); }));
    CHECK_EQ(vol & 0x0F, 0);
    CHECK_EQ(gate & 0x01, 0);
    CHECK(r.until(GREETING, ms(8000)));
}

TEST(break_during_sd4s_read_leaves_no_channel_running_into_the_boot)
{
    // Review P1: Break while channel 0 takes SD4's 64 KB: the channel is aborted, so the boot's DMA_ON can't
    // resume it into memory, and the card's command abandoned, so the boot's own reaches A>.
    Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(enter(r));
    bool hit = false, reached = false;
    uint8_t ctrl = 0xFF, mode = 0xFF;
    const uint16_t boot = label("BOOT");
    const DmaController &dma = *r.machine->dma();
    r.watch = [&](uint16_t pc) {
        if (!hit && (dma.peek_channel(0, DmaController::CTRL) & 0x01) && (r.io(0xC381) & 0x80) &&
            r.out.find("SD4  READ SPEED") != std::string::npos)
            hit = true, r.cpu->nmi();
        else if (hit && !reached && pc == boot)
            reached = true, ctrl = dma.peek_channel(0, DmaController::CTRL),
            mode = dma.peek_channel(0, DmaController::MODE); // as Break's way out left them
    };
    for (int i = 0; i < 4000 && !reached; ++i)
        r.run(ms(25));
    r.watch = nullptr;
    REQUIRE(hit);
    REQUIRE(reached);
    CHECK_EQ(ctrl & 0x01, 0); // channel 0's ENABLE
    CHECK_EQ(mode, 0);
    CHECK(r.until("A>", ms(20'000)));
}

TEST(an_sd_card_that_stays_busy_is_reset_and_given_no_command)
{
    // Review P2: SD3 finding the card busy fails without writing CMD into it; SD4's long wait ends with the
    // command abandoned (CTRL's RESET) and channel 0 aborted. Neither writes the card.
    for (const char *from : {"SD3  LBA 0", "SD4  READ SPEED"})
    {
        Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
        REQUIRE(r.ok());
        BusySd b;
        REQUIRE(busy_over(r, b));
        REQUIRE(enter(r));
        REQUIRE(r.until(from, ms(90'000)));
        b.stuck = true;
        const std::string name = std::string(from).substr(0, 3);
        REQUIRE(r.until("\r    DK1", ms(30'000)));
        b.stuck = false;
        CHECK(ends(test_line(r, name), "FAIL"));
        CHECK(after_test(r, name).find("THE CARD STAYED BUSY") != std::string::npos);
        CHECK(b.reset);
        if (name == "SD3")
            CHECK_EQ(b.cmds, 0u);
        CHECK_EQ(r.machine->dma()->peek_channel(0, DmaController::CTRL) & 0x01, 0);
        CHECK(!r.media->dirty());
        REQUIRE(to_result(r));
    }
}

TEST(a_failed_block_answering_as_a_good_one_is_not_filled_over_it_by_d2)
{
    // Review P3: block 20 answers as block 10. Stage 4 says the two answer as one and fails block 10, the one it
    // read wrong (NB2's tag there is block 20's). D2 must not fill block 10 and then name it from block 20's fill,
    // which lands there: the failed block that answers as another is passed over, and its RAM, good, isn't named.
    Rig r;
    REQUIRE(r.ok());
    FaultyMemory f;
    REQUIRE(fault_redirect(*r.machine, f, 20 * BLOCK, 10 * BLOCK));
    faulted_run(r);
    CHECK(after_test(r, "D2").find("BLOCK 10 ") == std::string::npos);
    CHECK(ends(test_line(r, "D3"), "PASS"));
}

TEST(d2_marks_every_block_it_finds_bad_not_only_the_first_eight)
{
    // Review P3: ten blocks go bad after stage 6; D2 names the first eight but marks all ten failed, so D3's two
    // blocks are good ones (with the ninth and tenth unmarked, D3 takes blocks 9 and 10 and fails).
    Rig r;
    REQUIRE(r.ok());
    std::vector<std::unique_ptr<FaultyMemory>> f;
    std::vector<uint8_t> masks;
    for (const uint32_t b : {0u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u})
    {
        const uint8_t p = static_cast<uint8_t>(b ^ 0xA5);                     // D2's fill of block b
        uint8_t m = static_cast<uint8_t>(~(p & static_cast<uint8_t>(-p)));     // its lowest bit stuck at 0
        if (b == 9)
            m = 0xF7; // bit 3: set in D2's $AC and in D3's address in address at +$0123 ($6B) alike
        f.push_back(std::make_unique<FaultyMemory>());
        f.back()->bad = b * BLOCK + 0x0123;
        REQUIRE(fault_over(*r.machine, *f.back(), b * BLOCK));
        masks.push_back(m);
    }
    c815(r, 0xA5, 0x00, 0x5A);
    auto before = r.watch;
    bool on_now = false;
    r.watch = [&](uint16_t pc) {
        if (before)
            before(pc);
        if (!on_now && r.out.find(" 7  NORTH") != std::string::npos)
        {
            on_now = true; // the faults come once stage 6 is over
            for (size_t i = 0; i < f.size(); ++i)
                f[i]->and_mask = masks[i];
        }
    };
    REQUIRE(to_result(r));
    CHECK(on_now);
    CHECK(ends(test_line(r, "D2"), "FAIL"));
    CHECK(ends(test_line(r, "D3"), "PASS"));
}

// --- DMA revision 2 (b2-plan.md §4, items 12, 13 and 15) ------------------------------------------------

TEST(break_during_d1_puts_back_every_card_and_the_boot_reaches_a_prompt)
{
    // Item 12: D1 writes its patterns into each channel's CARD. A Break while channel 0's holds one: Break's
    // way out (PUTBACKM, QUIETM's SELECT loop) puts every CARD back to its reset module, so the boot's load of
    // OODOS (channel 0, the SD card's module) is granted. Must fail if PUTBACKM leaves CARD alone: LOADRUN's
    // wait has no timeout, so the boot hangs.
    Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(enter(r));
    const DmaController &dma = *r.machine->dma();
    bool hit = false;
    uint8_t card = 0;
    r.watch = [&](uint16_t) {
        if (!hit && r.out.find("D1   ID $82") != std::string::npos && dma.peek_channel(0, DmaController::CARD) != 0x07)
            hit = true, card = dma.peek_channel(0, DmaController::CARD), r.cpu->nmi();
    };
    REQUIRE(r.until("D2   DMA FILL", ms(90'000)) || hit);
    r.watch = nullptr;
    REQUIRE(hit);
    std::printf("  Break with channel 0's CARD at $%02X\n", card);
    const size_t at = r.out.find("D1   ID $82");
    CHECK(r.until("BREAK", ms(500), at));
    CHECK(r.until(GREETING, ms(8000), at));
    CHECK(r.until("A>", ms(20'000), at));
    for (unsigned n = 0; n < DmaController::CHANNELS; ++n)
        CHECK_EQ(dma.peek_channel(n, DmaController::CARD), DmaController::CARD_RESET[n]);
}

namespace
{
    /// A DMA controller's ID register answering revision 1's $81 over the revision 2 controller's.
    struct RevisionOneId : Device
    {
        size_t size() const override { return 1; }
        uint8_t read(uint32_t) override { return 0x81; }
        uint8_t peek(uint32_t) const override { return 0x81; }
        void write(uint32_t, uint8_t) override {}
    };
} // namespace

TEST(a_revision_1_dma_controller_is_listed_and_never_driven)
{
    // Item 13: revision 1's layout is another, so its ID ($81) reads as no controller this ROM drives. Stage 8
    // skips it, saying revision 1, and NB3, V7, S3, SD3 and SD4 skip their DMA parts as with none fitted.
    // Must fail if the check takes revision 1 (HAVEDMA's or a card test's `cmp #2` left at 1): the ROM would
    // program a revision 1 controller with revision 2's registers.
    Rig r(SW19, EMU_BIOS_ROM, {}, sd_image("oodos.sparse"));
    REQUIRE(r.ok());
    RevisionOneId id;
    Mapping m;
    m.first = bus_address(Space::Io, 0x47F);
    m.last = m.first;
    m.device = &id;
    m.module = DmaController::MODULE;
    REQUIRE(r.machine->bus().map(m));
    REQUIRE(enter(r));
    REQUIRE(to_result(r));
    CHECK(ends(stage_line(r, 8), "SKIP"));
    const std::string text = report(r);
    const size_t s8 = text.find("\r" + stage_line(r, 8) + "\r"); // the stage's line, then its reason's
    const size_t why = s8 == std::string::npos ? s8 : s8 + stage_line(r, 8).size() + 2;
    const std::string reason = why == std::string::npos ? "" : text.substr(why, text.find('\r', why) - why);
    std::printf("  stage 8: '%s' / '%s'\n", stage_line(r, 8).c_str(), reason.c_str());
    CHECK(reason.find("REVISION 1") != std::string::npos);
    for (const char *t : {"V7", "S3", "SD3", "SD4"})
    {
        CHECK(ends(test_line(r, t), "SKIP"));
        CHECK(after_test(r, t).find("NO DMA CONTROLLER") != std::string::npos);
    }
    CHECK(test_line(r, "NB3").find("NO DMA CONTROLLER") != std::string::npos);
    CHECK(text.find("D1   ID") == std::string::npos); // D1-D5 never run
    CHECK(text.find("RESULT: ALL PASSED") != std::string::npos);
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
