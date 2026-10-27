/**
 * @file test_oodos_sys.cpp
 * @brief OODOS.SYS, phase 2's system, on backplane.machine (hw/oodos.md, "The system, phase 2", "BIOS").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * oodos/sys's Makefile builds OODOS.SYS and the test programs into the
 * build tree (CMakeLists.txt runs it). Each case boots the ROM BIOS to its
 * monitor (so block 63 holds the monitor that Break and BRK go back to),
 * puts a card in and mounts its units as OOLDR would, loads OODOS.SYS's
 * segments itself (its header read here), sets the windows to OOLDR's entry
 * (W0 0, W1-W3 and VEC 60, 61, 62, 62, I set, D clear) and starts the cold
 * boot. Then it types at the CCP, over the serial line or the USB keyboard,
 * and runs programs with GO: the .COM's first record poked at $0280 and its
 * image at $0300, as a loader leaves them.
 *
 *   - the banner (the version and the clock's time) and A>; M:'s directory
 *     byte for byte against what mkfs.cpm -t wrote (sd/oodos-ram.sparse,
 *     oodos/sys/test/make_mdisk.sh);
 *   - HELLO, then the warm boot back to A>; the BDOS stub's cycles;
 *   - BIOSTEST: every BIOS entry through function 50, drives from the
 *     units table (8 MB, 32 MB, read-only, odd sizes), sectors on the card
 *     and in M:, TIME, the devices;
 *   - STUBTEST: all four windows (W3 a home block), S, and D clear kept
 *     across BDOS calls; FARTEST: FARCALL into blocks mapped by hand, nested,
 *     the spike's register checks, and its cycles;
 *   - TIME against the clock: a leap day, 2099 into 2100, setting it back
 *     from the SCB (the day of the week, the century);
 *   - the VT52 subset and lowercase on the video card's text memory;
 *   - keys from the USB keyboard and the serial line; the CCP's refusals;
 *   - Break and BRK into the ROM's monitor with W3 and VEC on block 63; an
 *     IRQ that returns.
 */

#include "harness.h"
#include "sd_images.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_media.h"
#include "usb_card.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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
    constexpr size_t SECTOR = 512, RECORD = 128;
    constexpr uint32_t BLOCK = 0x4000;
    constexpr uint32_t COMMON_PHYS = 62 * BLOCK + 0x3800; ///< $F800 in block 62
    constexpr uint32_t SCREEN = 0x82000;
    constexpr uint16_t NB = 0xC680;
    constexpr uint8_t ROM_BLOCK_RO = 0x40 | 63;
    const std::string SYS = EMU_OODOS_SYS_DIR;

    std::vector<uint8_t> read_bytes(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    std::string read_text(const std::string &path)
    {
        const std::vector<uint8_t> b = read_bytes(path);
        return std::string(b.begin(), b.end());
    }

    /// An ld65 -Ln file's labels.
    std::map<std::string, uint16_t> labels(const std::string &path)
    {
        std::map<std::string, uint16_t> out;
        std::istringstream in(read_text(path));
        std::string al, hex, name;
        while (in >> al >> hex >> name)
            if (al == "al" && name.size() > 1)
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(hex, nullptr, 16));
        return out;
    }
    const std::map<std::string, uint16_t> &sys_labels()
    {
        static const std::map<std::string, uint16_t> l = labels(SYS + "/oodos.lbl");
        return l;
    }
    uint16_t sym(const std::string &name)
    {
        const auto it = sys_labels().find(name);
        if (it == sys_labels().end())
        {
            std::printf("  no label %s in oodos.lbl\n", name.c_str());
            return 0;
        }
        return it->second;
    }

    /// OODOS.SYS's header, read as OOLDR reads it (hw/oodos.md).
    struct SysFile
    {
        uint8_t major{0}, minor{0};
        uint16_t entry{0};
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segments;
        std::string error;

        static SysFile parse(const std::vector<uint8_t> &f)
        {
            SysFile s;
            if (f.size() < RECORD || std::memcmp(f.data(), "OOSY", 4) != 0)
                return s.fail("no OOSY");
            s.major = f[4];
            s.minor = f[5];
            s.entry = static_cast<uint16_t>(f[6] | f[7] << 8);
            const unsigned count = f[8];
            if (count < 1 || count > 8)
                return s.fail("segments: " + std::to_string(count));
            for (unsigned i = 9; i < 16; ++i)
                if (f[i])
                    return s.fail("reserved bytes");
            for (unsigned i = 0; i < count; ++i)
            {
                const uint8_t *e = &f[16 + i * 8];
                const uint32_t phys = unsigned{e[0]} | unsigned{e[1]} << 8 | unsigned{e[2]} << 16;
                const size_t len = size_t{e[3]} | size_t{e[4]} << 8, rec = size_t{e[5]} | size_t{e[6]} << 8;
                if (len == 0 || len > 16 * 1024 || rec == 0 || rec * RECORD + len > f.size())
                    return s.fail("segment " + std::to_string(i + 1));
                const bool in31 = phys >= 31 * BLOCK && phys + len <= 32 * BLOCK;
                const bool in60 = phys >= 60 * BLOCK && phys + len <= 63 * BLOCK;
                if (!in31 && !in60)
                    return s.fail("segment " + std::to_string(i + 1) + " outside blocks 31 and 60-62");
                s.segments.emplace_back(phys, std::vector<uint8_t>(f.begin() + static_cast<long>(rec * RECORD),
                                                                   f.begin() + static_cast<long>(rec * RECORD + len)));
            }
            return s;
        }
        SysFile &fail(const std::string &why)
        {
            error = why;
            return *this;
        }
    };

    /// CP/M 3's days (1 January 1978 day 1), with the DS3231's leap years, every fourth, as OODOS's TIME.
    unsigned days(unsigned y, unsigned m, unsigned d)
    {
        static const unsigned DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        unsigned n = d;
        for (unsigned year = 1978; year < y; ++year)
            n += year % 4 == 0 ? 366 : 365;
        for (unsigned month = 1; month < m; ++month)
            n += DAYS[month - 1] + (month == 2 && y % 4 == 0);
        return n;
    }
    std::string hex(unsigned v, int digits)
    {
        char b[16];
        std::snprintf(b, sizeof b, "%0*X", digits, v);
        return b;
    }

    /// A diskdef's CP/M 3 DPB, worked out from oodos/diskdefs, as the 17 bytes the BIOS should have.
    std::string dpb_hex(const std::string &name)
    {
        std::istringstream in(read_text(EMU_OODOS_DISKDEFS));
        std::string line;
        bool on = false;
        std::map<std::string, unsigned> v;
        while (std::getline(in, line))
        {
            std::istringstream w(line);
            std::string key;
            unsigned value = 0;
            w >> key;
            if (key == "diskdef")
            {
                std::string n;
                w >> n;
                on = n == name;
            }
            else if (on && key == "end")
                break;
            else if (on && w >> value)
                v[key] = value;
        }
        const unsigned spt = v["sectrk"] * v["seclen"] / 128, bls = v["blocksize"];
        unsigned bsh = 0;
        while ((128u << bsh) < bls)
            ++bsh;
        const unsigned dsm = (v["tracks"] - v["boottrk"]) * v["sectrk"] * v["seclen"] / bls - 1;
        const unsigned exm = dsm < 256 ? bls / 1024 - 1 : bls / 2048 - 1;
        const unsigned drm = v["maxdir"] - 1, dirblocks = (v["maxdir"] * 32 + bls - 1) / bls;
        const unsigned al = (0xFFFFu << (16 - dirblocks)) & 0xFFFF;
        unsigned psh = 0;
        while ((128u << psh) < v["seclen"])
            ++psh;
        const unsigned bytes[] = {spt & 0xFF, spt >> 8, bsh, (1u << bsh) - 1, exm, dsm & 0xFF, dsm >> 8,
                                  drm & 0xFF, drm >> 8, al >> 8, al & 0xFF, 0x00, 0x80, v["boottrk"] & 0xFF,
                                  v["boottrk"] >> 8, psh, (1u << psh) - 1};
        std::string s;
        for (unsigned b : bytes)
            s += hex(b, 2);
        return s;
    }

    /// A FAT16 card with no partition table, its files contiguous: what the committed card
    /// lacks, a 32 MB image (and one of an odd size).
    std::vector<uint8_t> fat16_card(const std::vector<std::pair<std::string, uint32_t>> &files)
    {
        constexpr unsigned SPC = 8, ROOT = 32, SEC = 512, CLUSTER = SPC * SEC;
        unsigned clusters = 0;
        for (const auto &f : files)
            clusters += (f.second + CLUSTER - 1) / CLUSTER;
        const unsigned fat_sectors = ((clusters + 2) * 2 + SEC - 1) / SEC;
        const unsigned data = 1 + fat_sectors + ROOT;
        const unsigned total = data + clusters * SPC;
        std::vector<uint8_t> card(size_t{total} * SECTOR);
        uint8_t *b = card.data();
        auto put16 = [](uint8_t *p, unsigned v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); };
        auto put32 = [&](uint8_t *p, unsigned v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); };
        b[0] = 0xEB, b[1] = 0x3C, b[2] = 0x90;
        std::memcpy(b + 3, "OODOSTST", 8);
        put16(b + 11, SECTOR);
        b[13] = SPC;
        put16(b + 14, 1);                       // reserved
        b[16] = 1;                              // FATs
        put16(b + 17, ROOT * SECTOR / 32);      // root entries
        b[21] = 0xF8;
        put16(b + 22, fat_sectors);
        put32(b + 32, total);
        b[38] = 0x29;
        std::memcpy(b + 43, "OODOS TEST ", 11);
        std::memcpy(b + 54, "FAT16   ", 8);
        b[510] = 0x55, b[511] = 0xAA;
        uint8_t *fat = b + SECTOR, *root = b + (1 + fat_sectors) * SECTOR;
        put16(fat, 0xFFF8);
        put16(fat + 2, 0xFFFF);
        unsigned next = 2;
        for (size_t i = 0; i < files.size(); ++i)
        {
            const auto &[name, size] = files[i];
            const unsigned n = (size + CLUSTER - 1) / CLUSTER;
            uint8_t *e = root + i * 32;
            std::memset(e, ' ', 11);
            const size_t dot = name.find('.');
            std::memcpy(e, name.data(), dot);
            std::memcpy(e + 8, name.data() + dot + 1, name.size() - dot - 1);
            e[11] = 0x20;
            put16(e + 26, next);
            put32(e + 28, size);
            for (unsigned c = 0; c < n; ++c)
                put16(fat + 2 * (next + c), c + 1 == n ? 0xFFFF : next + c + 1);
            next += n;
        }
        return card;
    }

    /// backplane.machine with the ROM BIOS at its monitor, a card in the slot, and OODOS.SYS to boot.
    struct Rig
    {
        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
        std::shared_ptr<MemoryMedia> media;
        std::string out;                                           ///< the serial line, everything so far
        std::function<void(uint16_t pc)> watch;                    ///< before each instruction, if set
        bool ok{false};

        explicit Rig(std::vector<uint8_t> card = sd_image("oodos.sparse"))
        {
            MachineOptions opt;
            opt.rom_override = EMU_BIOS_ROM;
            std::string error;
            machine = Machine::build(read_text(EMU_MACHINE_DIR "/backplane.machine"), "backplane.machine", opt, error);
            if (machine)
                console = make_console(*machine, error);
            if (!console)
            {
                std::printf("  %s\n", error.c_str());
                return;
            }
            cpu = std::make_unique<Cpu6502>(machine->bus(), Cpu6502::Model::W65C02S);
            machine->bus().reset();
            cpu->reset();
            if (!until("BACKPLANE BIOS 0.9\r\\\r", ms(2000)))
            {
                std::printf("  the ROM BIOS didn't reach its monitor\n");
                return;
            }
            media = std::make_shared<MemoryMedia>(std::move(card), "card");
            machine->sd_card()->insert(media, false);
            run(ms(SdCard::INIT_MS + 10));
            ok = true;
        }

        BackplaneBridge &bridge() { return *machine->backplane_bridge(); }
        SdCard &sd() { return *machine->sd_card(); }
        uint8_t nb(unsigned reg) { return machine->bus().peek(static_cast<uint16_t>(NB + reg)); }
        uint8_t phys(uint32_t a) const { return machine->bus().peek_physical(a); }
        void poke(uint32_t a, uint8_t v) { machine->bus().write_physical(a, v); }
        void poke(uint32_t a, const std::vector<uint8_t> &bytes)
        {
            for (size_t i = 0; i < bytes.size(); ++i)
                poke(a + static_cast<uint32_t>(i), bytes[i]);
        }

        /// Run `cycles`: IRQ and Pause/Break's NMI as the run loop takes them, the console served.
        void run(uint64_t cycles)
        {
            const uint64_t end = cpu->cycles() + cycles;
            while (cpu->cycles() < end)
            {
                if (watch)
                    watch(cpu->regs().pc);
                cpu->step();
                if (machine->bus().irq())
                    cpu->irq();
                if (machine->usb_card()->take_nmi())
                    cpu->nmi();
                if (cpu->cycles() % 64 > 7)
                    continue;
                console->service();
                uint8_t ch = 0;
                while (console->output(ch))
                    out += static_cast<char>(ch);
            }
        }
        /// Run until the output since `from` holds `text`. @return whether it did
        bool until(const std::string &text, uint64_t budget = ms(3000), size_t from = 0)
        {
            const uint64_t end = cpu->cycles() + budget;
            while (out.find(text, from) == std::string::npos && cpu->cycles() < end)
                run(20'000);
            return out.find(text, from) != std::string::npos;
        }
        /// The serial line's last `n` characters, control characters shown, for a failure.
        void tail(size_t n = 300) const
        {
            std::string t;
            for (const char c : out.substr(out.size() > n ? out.size() - n : 0))
                t += c == '\r' ? std::string("\\r") : c == '\n' ? std::string("\\n\n       ")
                     : static_cast<unsigned char>(c) < 32 ? "^" + std::string(1, static_cast<char>(c + 64)) : std::string(1, c);
            std::printf("  the serial line's end:\n       %s\n", t.c_str());
        }
        /// Keys on the serial line, each offered until taken and echoed.
        void type(const std::string &keys)
        {
            for (const char k : keys)
            {
                for (int i = 0; i < 2000 && !console->key(static_cast<uint8_t>(k)); ++i)
                    run(1'000);
                run(30'000);
            }
        }
        /// Keys on the USB keyboard: down, a gap, up, a gap. Letters, digits, space and CR.
        void type_usb(const std::string &text)
        {
            UsbCard *usb = machine->usb_card();
            for (const char ch : text)
            {
                uint8_t usage = 0;
                if (ch >= 'A' && ch <= 'Z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'A');
                else if (ch >= 'a' && ch <= 'z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'a');
                else if (ch >= '1' && ch <= '9')
                    usage = static_cast<uint8_t>(0x1E + ch - '1');
                else if (ch == '0')
                    usage = 0x27;
                else if (ch == ' ')
                    usage = 0x2C;
                else if (ch == '\r')
                    usage = 0x28;
                usb->key(usage, true);
                run(ms(20));
                usb->key(usage, false);
                run(ms(20));
            }
        }

        /// MOUNT `path` as `unit` through the card's registers, as OOLDR will. @return ERROR, 0 mounted
        uint8_t mount(unsigned unit, const std::string &path, bool read_only = false)
        {
            sd().write(SdCard::UNIT, 0);
            sd().write(SdCard::CMD, SdCard::MOUNT);
            std::vector<uint8_t> p = {static_cast<uint8_t>(unit), static_cast<uint8_t>(read_only ? 1 : 0)};
            p.insert(p.end(), path.begin(), path.end());
            for (size_t i = 0; i < SECTOR; ++i)
                sd().write(SdCard::DATA, i < p.size() ? p[i] : 0);
            run(ms(2));
            return sd().read(SdCard::STATUS) & SdCard::ERR ? sd().read(SdCard::ERROR) : 0;
        }
        /// A unit's sector, read through the card's registers while OODOS waits at its prompt.
        std::vector<uint8_t> read_unit(unsigned unit, uint32_t lba)
        {
            sd().write(SdCard::UNIT, static_cast<uint8_t>(unit));
            for (unsigned i = 0; i < 4; ++i)
                sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            sd().write(SdCard::COUNT, 1);
            sd().write(SdCard::COUNT + 1, 0);
            sd().write(SdCard::CMD, SdCard::READ);
            run(ms(1));
            std::vector<uint8_t> s(SECTOR);
            for (auto &b : s)
                b = sd().read(SdCard::DATA);
            sd().write(SdCard::UNIT, 0);
            return s;
        }

        void set_time(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s)
        {
            Ds3231::Time t;
            t.year = y, t.month = mo, t.date = d, t.hour = h, t.minute = mi, t.second = s;
            t.day = (days(y, mo, d) - 1) % 7 + 1;
            bridge().set_rtc_time(t);
        }

        /// OODOS.SYS into memory and its cold boot started, as OOLDR leaves things. @return A> came
        bool boot()
        {
            const SysFile f = SysFile::parse(read_bytes(SYS + "/OODOS.SYS"));
            if (!f.error.empty())
            {
                std::printf("  OODOS.SYS: %s\n", f.error.c_str());
                return false;
            }
            for (const auto &[phys, bytes] : f.segments)
                poke(phys, bytes);
            const uint8_t windows[] = {0, 60, 61, 62, 62};
            for (unsigned i = 0; i < 5; ++i)
                machine->bus().write(static_cast<uint16_t>(NB + i), windows[i]);
            Registers r = cpu->regs();
            r.pc = f.entry;
            r.p = Cpu6502::FLAG_U | Cpu6502::FLAG_I;
            cpu->set_regs(r);
            return until("A>", ms(3000), out.size());
        }

        /// A .COM as a loader leaves it: its first record at $0280, its image at $0300.
        void poke_com(const std::vector<uint8_t> &com)
        {
            poke(0x0280, std::vector<uint8_t>(com.begin(), com.begin() + static_cast<long>(std::min(com.size(), RECORD))));
            if (com.size() > 16)
                poke(0x0300, std::vector<uint8_t>(com.begin() + 16, com.end()));
        }
        /// GO with this .COM and `tail`. @return what it printed, to the next prompt
        std::string go(const std::string &com, const std::string &tail = "", uint64_t budget = ms(4000))
        {
            poke_com(read_bytes(SYS + "/" + com));
            const size_t from = out.size();
            type("GO" + tail + "\r");
            if (!until("\r\nA>", budget, from + 2))
                std::printf("  %s: no prompt after it\n", com.c_str());
            return out.substr(from);
        }

        /// The text screen's row, trailing blanks dropped.
        std::string row(unsigned r) const
        {
            std::string t;
            for (unsigned c = 0; c < 80; ++c)
                t += static_cast<char>(phys(SCREEN + r * 160 + c * 2));
            return t.substr(0, t.find_last_not_of(' ') + 1);
        }
        uint8_t attr(unsigned r, unsigned c) const { return phys(SCREEN + r * 160 + c * 2 + 1); }
    };

    /// The value after `key ` on its line of `text`, to the line's end.
    std::string field(const std::string &text, const std::string &key)
    {
        const size_t at = text.find("\n" + key + " ");
        if (at == std::string::npos)
            return "<no " + key + ">";
        const size_t from = at + key.size() + 2;
        return text.substr(from, text.find('\r', from) - from);
    }

    /// Common memory's 2 KB copied into block b's top, as OODOS will do for a block allocated for code.
    void give_common(Rig &r, unsigned b)
    {
        for (uint32_t i = 0; i < 0x800; ++i)
            r.poke(b * BLOCK + 0x3800 + i, r.phys(COMMON_PHYS + i));
    }

    /// A booted rig with the committed card's A.IMG as A: and again, read-only, as B:.
    struct Booted : Rig
    {
        Booted()
        {
            if (!ok)
                return;
            ok = mount(1, "/OODOS/A.IMG") == 0 && mount(2, "/OODOS/A.IMG", true) == 0;
            set_time(2026, 10, 27, 12, 0, 0);
            ok = ok && boot();
        }
    };
} // namespace

TEST(oodos_sys_is_built_to_its_format)
{
    const std::vector<uint8_t> f = read_bytes(SYS + "/OODOS.SYS");
    REQUIRE(!f.empty());
    const SysFile s = SysFile::parse(f);
    CHECK_EQ(s.error, std::string());
    CHECK_EQ(s.major, 0);
    CHECK_EQ(s.minor, 2);
    CHECK_EQ(s.entry, sym("COLD"));
    CHECK(s.entry >= 0xC800);
    REQUIRE(s.segments.size() == 4u);
    CHECK_EQ(s.segments[0].first, 60u * BLOCK);  // the BDOS
    CHECK_EQ(s.segments[1].first, 61u * BLOCK);  // the BIOS
    CHECK_EQ(s.segments[2].first, 62u * BLOCK + 0x800); // block 62 from $C800
    CHECK_EQ(s.segments[3].first, COMMON_PHYS);  // common memory, to $FFFF
    CHECK_EQ(s.segments[3].second.size(), 0x800u);
    // The jump table: 30 JMPs, then the vectors: NMI, the warm boot, IRQ.
    const std::vector<uint8_t> &c = s.segments[3].second;
    for (unsigned i = 0; i < 30; ++i)
        CHECK_EQ(c[i * 3], 0x4C);
    CHECK_EQ(c[0x7FA] | c[0x7FB] << 8, sym("NMI"));
    CHECK_EQ(c[0x7FE] | c[0x7FF] << 8, sym("IRQ"));
    std::printf("  segments: BDOS %zu, BIOS %zu, block 62 %zu + common %zu bytes\n", s.segments[0].second.size(),
                s.segments[1].second.size(), s.segments[2].second.size(), s.segments[3].second.size());
}

TEST(cold_boot_says_who_it_is_with_the_time_and_prompts)
{
    Booted r;
    REQUIRE(r.ok);
    CHECK(r.out.find("\r\nOODOS 0.2  2026-10-27 12:00:00\r\n\r\nA>") != std::string::npos);
    // Waiting for a line, in the BDOS: the system's windows.
    CHECK_EQ(r.nb(0), 31);
    CHECK_EQ(r.nb(1), 60);
    CHECK_EQ(r.nb(2), 61);
    CHECK_EQ(r.nb(3), 62);
    CHECK_EQ(r.nb(4), 62);
    // The base page and the BDOS's entry at $BFFD, in the program's memory.
    CHECK_EQ(r.phys(0x0200), 0x4C);
    CHECK_EQ(r.phys(0x0201) | r.phys(0x0202) << 8, sym("WARM"));
    CHECK_EQ(r.phys(0x0203), 0x4C);
    CHECK_EQ(r.phys(0x0204) | r.phys(0x0205) << 8, 0xBFFD);
    CHECK_EQ(r.phys(0xBFFD), 0x4C);
    CHECK_EQ(r.phys(0xBFFE) | r.phys(0xBFFF) << 8, sym("STUB"));
    // The banner on the screen too.
    CHECK_EQ(r.row(1), std::string("OODOS 0.2  2026-10-27 12:00:00"));
    CHECK_EQ(r.row(3), std::string("A>"));
}

TEST(m_is_formatted_as_mkfs_cpm_t_with_mode_0x31)
{
    const std::vector<uint8_t> want = sd_image("oodos-ram.sparse");
    REQUIRE(want.size() == 16 * SECTOR);
    CHECK_EQ(want[12], 0x31);
    Booted r;
    REQUIRE(r.ok);
    size_t differ = 0, first = want.size();
    for (size_t i = 0; i < want.size(); ++i)
        if (r.phys(0xC0000 + static_cast<uint32_t>(i)) != want[i] && differ++ == 0)
            first = i;
    CHECK_EQ(differ, 0u);
    if (differ)
        std::printf("  first difference at %zu: %02X, mkfs.cpm %02X\n", first, r.phys(0xC0000 + static_cast<uint32_t>(first)), want[first]);
}

TEST(go_runs_hello_and_the_warm_boot_comes_back_to_the_prompt)
{
    Booted r;
    REQUIRE(r.ok);
    // The BDOS stub's cost, on HELLO's first call: $0203 to the BDOS, and its return to HELLO.
    const auto hello = labels(SYS + "/hello.lbl");
    const uint16_t back = static_cast<uint16_t>(hello.at("start") + 9); // after its JSR BDOS
    uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    bool started = false;
    r.watch = [&](uint16_t pc) {
        const uint64_t now = r.cpu->cycles();
        if (pc == 0x0300)
            started = true;
        else if (started && !t0 && pc == 0x0203)
            t0 = now;
        else if (t0 && !t1 && pc == sym("BDOSENT"))
            t1 = now;
        else if (t1 && !t2 && pc == sym("STUBRET"))
            t2 = now;
        else if (t2 && !t3 && pc == back)
            t3 = now;
    };
    const std::string o = r.go("hello.com", " world");
    CHECK(o.find("GO world\r\nHello from OODOS\r\nHello from OODOS\r\n world\r\n\r\nA>") != std::string::npos);
    REQUIRE(t3 != 0);
    std::printf("  the BDOS stub: %llu cycles in (JSR $0203 to the BDOS), %llu out (its RTS to the program's)\n",
                static_cast<unsigned long long>(t1 - t0), static_cast<unsigned long long>(t3 - t2));
    CHECK(t1 - t0 < 200 && t3 - t2 < 200);
    // Back at the prompt, the windows as the warm boot left them for the CCP (it waits in the BDOS).
    CHECK_EQ(r.nb(3), 62);
    // Again, so the warm boot leaves things as cold boot did.
    const std::string again = r.go("hello.com");
    CHECK(again.find("Hello from OODOS\r\nHello from OODOS\r\n\r\n\r\nA>") != std::string::npos);
}

TEST(biostest_reaches_every_bios_entry_through_function_50)
{
    Booted r;
    REQUIRE(r.ok);
    r.set_time(2026, 10, 27, 15, 42, 7);
    const std::string o = r.go("biostest.com", "", ms(8000));
    const auto bl = labels(SYS + "/biostest.lbl");
    const uint16_t dphs = sym("DPHS"), dph_len = 25;
    CHECK_EQ(field(o, "SELDSK A"), hex(dphs, 4));
    CHECK_EQ(field(o, "DPB A"), dpb_hex("oodos-8m"));
    CHECK_EQ(field(o, "SELDSK B"), hex(dphs + dph_len, 4));
    CHECK_EQ(field(o, "DPB B"), dpb_hex("oodos-8m"));
    for (const char *d : {"SELDSK C", "SELDSK D", "SELDSK E", "SELDSK F", "SELDSK G"})
        CHECK_EQ(field(o, d), std::string("0000"));
    CHECK_EQ(field(o, "SELDSK M"), hex(sym("DPHM"), 4));
    CHECK_EQ(field(o, "DPB M"), dpb_hex("oodos-ram"));
    CHECK_EQ(field(o, "SELDSK 16"), std::string("0000"));
    CHECK_EQ(field(o, "PAST A"), std::string("01"));      // the card: out of range
    CHECK_EQ(field(o, "BADBIOS"), std::string("FFFF"));
    CHECK_EQ(field(o, "WRITE A"), std::string("00"));
    CHECK_EQ(field(o, "READ A"), std::string("00"));
    CHECK_EQ(field(o, "SAME A"), std::string("Y"));
    CHECK_EQ(field(o, "BOUNCE A"), std::string("00 Y"));
    CHECK_EQ(field(o, "WRITE B"), std::string("02"));    // read-only: CP/M 3's write-protect
    CHECK_EQ(field(o, "WRITE M"), std::string("00"));
    CHECK_EQ(field(o, "READ M"), std::string("00"));
    CHECK_EQ(field(o, "SAME M"), std::string("Y"));
    CHECK_EQ(field(o, "ERRORS M"), std::string("01 01")); // track 16, sector 16
    CHECK_EQ(field(o, "LABEL M"), std::string("20554E4C4142454C4544202031000000"));
    CHECK_EQ(field(o, "TIME").substr(0, 10), hex(days(2026, 10, 27), 4) + " 15 42");
    CHECK_EQ(field(o, "STATUS"), std::string("00 FF FF 00 FF "));
    CHECK_EQ(field(o, "DEVTBL"), std::string("CRT   TTY   LPT   "));
    CHECK_EQ(field(o, "DRVTBL"), std::string("Y"));
    CHECK_EQ(field(o, "SECTRN"), std::string("0005 MULTIO 00 FLUSH 00"));
    CHECK_EQ(field(o, "MOVE"), "MOVED " + hex(bl.at("movedst") + 6u, 4));
    CHECK(o.find("\r\nCX\r\n") != std::string::npos);    // CONOUT to CRT and TTY, AUXOUT to TTY, LIST nowhere
    CHECK(o.find("BIOSTEST DONE") != std::string::npos);
    // What it wrote is on the card and in M:: the pattern, i ^ $5A.
    const std::vector<uint8_t> a = r.read_unit(1, 1023 * 16 + 15);
    bool card = true, mdisk = true;
    for (size_t i = 0; i < SECTOR; ++i)
    {
        card = card && a[i] == static_cast<uint8_t>((i & 0xFF) ^ 0x5A);
        mdisk = mdisk && r.phys(0xC0000 + 255 * SECTOR + static_cast<uint32_t>(i)) == static_cast<uint8_t>((i & 0xFF) ^ 0x5A);
    }
    CHECK(card);
    CHECK(mdisk);
    // BOUNCE's halves: $3F00-$3FFF in block 0, then $4000-$40FF in block 4 (W1 moved for it).
    bool halves = true;
    for (uint32_t i = 0; i < 256; ++i)
        halves = halves && r.phys(0x3F00 + i) == static_cast<uint8_t>(i ^ 0x5A) &&
                 r.phys(4 * BLOCK + i) == static_cast<uint8_t>(i ^ 0x5A);
    CHECK(halves);
    // The read-only unit's write never reached the card: B: is the same file.
    CHECK(r.read_unit(2, 0)[0] == 0x20);
}

TEST(drives_come_from_the_units_table_32_mb_and_odd_sizes)
{
    Rig r(fat16_card({{"BIG.IMG", 65536u * 512}, {"ODD.IMG", 1000u * 512}, {"EIGHT.IMG", 16384u * 512}}));
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/BIG.IMG"), 0);
    CHECK_EQ(r.mount(2, "/ODD.IMG"), 0);
    CHECK_EQ(r.mount(5, "/EIGHT.IMG", true), 0);
    REQUIRE(r.boot());
    const std::string o = r.go("biostest.com", "", ms(8000));
    const uint16_t dphs = sym("DPHS");
    CHECK_EQ(field(o, "SELDSK A"), hex(dphs, 4));
    CHECK_EQ(field(o, "DPB A"), dpb_hex("oodos-32m"));
    CHECK_EQ(field(o, "SELDSK B"), std::string("0000")); // mounted, but neither size
    CHECK_EQ(field(o, "SELDSK C"), std::string("0000"));
    CHECK_EQ(field(o, "SELDSK E"), hex(dphs + 4 * 25, 4));
    CHECK_EQ(field(o, "DPB E"), dpb_hex("oodos-8m"));
    CHECK_EQ(field(o, "SAME A"), std::string("Y"));       // a 32 MB drive's sector 16,383
}

TEST(the_bdos_stub_keeps_all_four_windows_s_and_returns_d_clear)
{
    Booted r;
    REQUIRE(r.ok);
    give_common(r, 20);                                   // the home block
    const std::string home = "HOME BLOCK\r\n$";
    r.poke(20 * BLOCK + 0x800, std::vector<uint8_t>(home.begin(), home.end()));
    r.set_time(2030, 6, 15, 9, 15, 30);
    const std::string o = r.go("stubtest.com");
    CHECK(o.find("\r\nW1 STRING\r\nHOME BLOCK\r\n") != std::string::npos);
    CHECK_EQ(field(o, "WINDOWS"), std::string("03 04 05 14 "));
    const std::string s = field(o, "S");
    CHECK_EQ(s.size(), 5u);
    CHECK_EQ(s.substr(0, 2), s.substr(3, 2));
    const unsigned p = static_cast<unsigned>(std::stoul(field(o, "P"), nullptr, 16));
    CHECK_EQ(p & Cpu6502::FLAG_D, 0u);
    CHECK_EQ(field(o, "DAT").substr(0, 10), hex(days(2030, 6, 15), 4) + " 09 15");
    CHECK_EQ(field(o, "NOFUNC"), std::string("FFFF"));
    CHECK(o.find("\r\nA>") != std::string::npos);         // the warm boot put the windows back
}

TEST(farcall_maps_blocks_by_hand_nests_and_keeps_the_registers)
{
    Booted r;
    REQUIRE(r.ok);
    const uint16_t far = sym("FARCALL");
    give_common(r, 20);
    give_common(r, 21);
    // Block 20 at $C800: record A X Y P WIN3 at $0290, far call block 21 with $44 $55 $66,
    // record what came back at $0295, return $77 $88 $99 with P $C3.
    const std::vector<uint8_t> t20 = {
        0x08, 0x8D, 0x90, 0x02, 0x8E, 0x91, 0x02, 0x8C, 0x92, 0x02, 0x68, 0x8D, 0x93, 0x02, 0xAD, 0x83, 0xC6,
        0x8D, 0x94, 0x02, 0xA9, 0x44, 0xA2, 0x55, 0xA0, 0x66, 0x20, static_cast<uint8_t>(far),
        static_cast<uint8_t>(far >> 8), 21, 0x00, 0xC8, 0x08, 0x8D, 0x95, 0x02, 0x8E, 0x96, 0x02, 0x8C, 0x97,
        0x02, 0x68, 0x8D, 0x98, 0x02, 0xAD, 0x83, 0xC6, 0x8D, 0x99, 0x02, 0xA9, 0xC3, 0x48, 0xA9, 0x77, 0xA2,
        0x88, 0xA0, 0x99, 0x28, 0x60};
    // Block 21 at $C800: record at $029A, return $AA $BB $CC with P $41.
    const std::vector<uint8_t> t21 = {0x08, 0x8D, 0x9A, 0x02, 0x8E, 0x9B, 0x02, 0x8C, 0x9C, 0x02, 0x68, 0x8D,
                                      0x9D, 0x02, 0xAD, 0x83, 0xC6, 0x8D, 0x9E, 0x02, 0xA9, 0x41, 0x48, 0xA9,
                                      0xAA, 0xA2, 0xBB, 0xA0, 0xCC, 0x28, 0x60};
    r.poke(20 * BLOCK + 0x800, t20);
    r.poke(21 * BLOCK + 0x800, t21);
    // The outer far call's cost: FARCALL to the target, and FCRET back to the caller.
    const uint16_t back = static_cast<uint16_t>(labels(SYS + "/fartest.lbl").at("outer") + 6);
    uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    r.watch = [&](uint16_t pc) {
        const uint64_t now = r.cpu->cycles();
        if (!t0 && pc == far && r.nb(3) == 62)
            t0 = now;
        else if (t0 && !t1 && pc == 0xC800 && r.nb(3) == 20)
            t1 = now;
        else if (t1 && !t3 && pc == sym("FCRET") && r.nb(3) == 20 && r.cpu->regs().a == 0x77)
            t2 = now;
        else if (t2 && !t3 && pc == back)
            t3 = now;
    };
    const std::string o = r.go("fartest.com");
    CHECK_EQ(field(o, "OODOS"), "000200020004" "4001" + hex(far & 0xFF, 2) + hex(far >> 8, 2));
    CHECK_EQ(field(o, "FARCALL"), hex(far, 4));
    // Block 20 got 11 22 33, P $CD (B and U pushed), in block 20; block 21's came back to it
    // with 20 in W3 again; 21 got 44 55 66 in block 21; the caller got 77 88 99, P $C3, in 62.
    CHECK_EQ(field(o, "FAR"), std::string("112233FD14" "AABBCC7114" "4455667D15" "778899F33E"));
    REQUIRE(t3 != 0);
    std::printf("  FARCALL: %llu cycles over a JSR/RTS (%llu in, %llu out)\n",
                static_cast<unsigned long long>((t1 - t0) + (t3 - t2)), static_cast<unsigned long long>(t1 - t0),
                static_cast<unsigned long long>(t3 - t2));
    CHECK_EQ((t1 - t0) + (t3 - t2), 281u); // the spike's figure: the same code
}

TEST(time_reads_and_sets_the_clock_through_the_scb)
{
    Booted r;
    REQUIRE(r.ok);
    // A leap day.
    r.set_time(2024, 2, 29, 13, 45, 30);
    CHECK_EQ(field(r.go("timetest.com"), "TIME").substr(0, 12), hex(days(2024, 2, 29), 4) + " 13 45 3");
    // The last second of 2099, into 2100 (the century bit).
    r.set_time(2099, 12, 31, 23, 59, 59);
    r.run(ms(1100));
    CHECK_EQ(field(r.go("timetest.com"), "TIME").substr(0, 12), hex(days(2100, 1, 1), 4) + " 00 00 0");
    // Set back from the SCB by function 104: 15 June 2030, 09:15, a Saturday (7).
    std::string o = r.go("timetest.com", " " + hex(days(2030, 6, 15), 4) + " 09 15");
    CHECK_EQ(field(o, "TIME").substr(0, 10), hex(days(2030, 6, 15), 4) + " 09 15");
    r.run(ms(5));
    auto t = r.bridge().rtc_time();                         // SEC-YEAR as the bridge last read them
    CHECK_EQ(t[1], 0x15);
    CHECK_EQ(t[2], 0x09);
    CHECK_EQ(t[3], 7);
    CHECK_EQ(t[4], 0x15);
    CHECK_EQ(t[5], 0x06);
    CHECK_EQ(t[6], 0x30);
    // 2101 (the DS3231's every-fourth leap years: 2100 has a 29 February, so OODOS's days do too).
    o = r.go("timetest.com", " " + hex(days(2101, 3, 1), 4) + " 23 01");
    CHECK_EQ(field(o, "TIME").substr(0, 10), hex(days(2101, 3, 1), 4) + " 23 01");
    r.run(ms(5));
    t = r.bridge().rtc_time();
    CHECK_EQ(t[4], 0x01);
    CHECK_EQ(t[5], 0x83);                                    // March, the century bit
    CHECK_EQ(t[6], 0x01);
    CHECK_EQ(t[3], (days(2101, 3, 1) - 1) % 7 + 1);
    // 31 December 1999 is before what the bridge holds: the clock is left as it was.
    o = r.go("timetest.com", " " + hex(days(1999, 12, 31), 4) + " 12 00");
    CHECK_EQ(field(o, "TIME").substr(0, 10), hex(days(2101, 3, 1), 4) + " 23 01");
    t = r.bridge().rtc_time();
    CHECK_EQ(t[6], 0x01);
    CHECK_EQ(t[5], 0x83);
}

TEST(the_crt_does_the_vt52_subset_and_lowercase)
{
    Booted r;
    REQUIRE(r.ok);
    r.go("vt52test.com");
    CHECK_EQ(r.row(0), std::string("Line one"));
    CHECK_EQ(r.row(1), std::string("lower case"));
    CHECK_EQ(r.row(2), std::string());
    CHECK_EQ(r.row(3), std::string("  X"));
    CHECK_EQ(r.row(5), std::string("          AT 5,10"));
    CHECK_EQ(r.row(10), std::string("ERASE"));
    CHECK_EQ(r.row(11), std::string("  U"));
    CHECK_EQ(r.row(12), std::string("aZc"));
    CHECK_EQ(r.row(14), std::string("AC"));
    CHECK_EQ(r.row(15), std::string("        T"));
    CHECK_EQ(r.row(16), std::string(78, ' ') + "WR");
    CHECK_EQ(r.row(17), std::string("AP"));
    CHECK_EQ(r.row(20), std::string("JJJ"));
    CHECK_EQ(r.row(21), std::string("K"));
    CHECK_EQ(r.row(22), std::string());
    CHECK_EQ(r.row(25), std::string("END"));
    CHECK_EQ(r.row(26), std::string("A>"));
    CHECK_EQ(r.attr(0, 0), 0x07);
    CHECK_EQ(r.attr(26, 2), 0xF0);                           // the cursor, after A>
    CHECK_EQ(r.attr(26, 3), 0x07);
    // Three empty lines at the prompt: six new lines from row 26, so three scrolls.
    r.type("\r\r\r");
    r.run(ms(50));
    CHECK_EQ(r.row(0), std::string("  X"));                  // up from row 3
    CHECK_EQ(r.row(9), std::string("aZc"));
    CHECK_EQ(r.row(22), std::string("END"));
    CHECK_EQ(r.row(23), std::string("A>"));
    CHECK_EQ(r.row(25), std::string("A>"));
    CHECK_EQ(r.row(27), std::string("A>"));
    CHECK_EQ(r.row(29), std::string("A>"));
    unsigned cursors = 0;
    for (unsigned row = 0; row < 30; ++row)
        for (unsigned col = 0; col < 80; ++col)
            cursors += r.attr(row, col) == 0xF0;
    CHECK_EQ(cursors, 1u);                                   // only at the prompt's end
    CHECK_EQ(r.attr(29, 2), 0xF0);
}

TEST(keys_come_from_the_usb_keyboard_and_the_serial_line)
{
    Booted r;
    REQUIRE(r.ok);
    r.poke_com(read_bytes(SYS + "/hello.com"));
    size_t from = r.out.size();
    r.type_usb("go\r");                                      // Caps Lock is on: GO
    CHECK(r.until("Hello from OODOS\r\n", ms(3000), from));
    CHECK(r.until("\r\nA>", ms(3000), from + 20));
    r.poke_com(read_bytes(SYS + "/hello.com"));             // the tail went over its header
    from = r.out.size();
    r.type("go\r");                                          // lowercase on the serial line
    CHECK(r.until("Hello from OODOS\r\n", ms(3000), from));
    CHECK(r.until("\r\nA>", ms(3000), from + 20));
    from = r.out.size();
    r.type("abc\r");                                         // echoed as typed; the CCP's capitals
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("abc\r\nABC?\r\nA>", from) != std::string::npos);
    bool echoed = false;
    for (unsigned row = 0; row < 30; ++row)
        echoed = echoed || r.row(row) == "A>abc";
    CHECK(echoed);
}

TEST(function_10_edits_the_line_as_the_ccp_reads_it)
{
    Booted r;
    REQUIRE(r.ok);
    size_t from = r.out.size();
    r.type("fx\x7Foo\r");                                    // DEL takes the x back
    CHECK(r.until("\nFOO?\r\nA>", ms(1000), from));
    CHECK(r.out.find("fx\b \boo\r\n", from) != std::string::npos);
    from = r.out.size();
    r.type("ba\x08r\x18qux\r");                              // BS, then ^X the whole line
    CHECK(r.until("\nQUX?\r\nA>", ms(1000), from));
    from = r.out.size();
    r.type("a\x01" "b\r");                                     // other control characters: nothing
    CHECK(r.until("\nAB?\r\nA>", ms(1000), from));
    from = r.out.size();
    r.type(std::string(130, 'z') + "\r");                   // 126 fit
    CHECK(r.until("\n" + std::string(126, 'Z') + "?\r\nA>", ms(3000), from));
    CHECK(r.out.find(std::string(127, 'Z'), from) == std::string::npos);
    // ^C at the start of a line: the warm boot.
    bool warm = false;
    r.watch = [&](uint16_t pc) { warm = warm || pc == sym("WARM"); };
    from = r.out.size();
    r.type("x\x03\x7F\x03");                               // not after a character: only at the start
    CHECK(r.until("\r\nA>", ms(1000), from));
    CHECK(warm);
    CHECK(r.out.find("x\b \b", from) != std::string::npos);
}

TEST(the_console_functions_of_the_stand_in_bdos)
{
    Booted r;
    REQUIRE(r.ok);
    r.poke_com(read_bytes(SYS + "/linetest.com"));
    const size_t from = r.out.size();
    r.type("GO\r");
    REQUIRE(r.until("KEY?", ms(2000), from));
    r.type("k");
    REQUIRE(r.until("RAW?", ms(2000), from));
    r.type("r");
    REQUIRE(r.until("WAIT ", ms(2000), from));
    r.run(ms(20));
    r.type("s");
    REQUIRE(r.until("LINE?", ms(2000), from));
    r.type("hello world\r");
    CHECK(r.until("\r\nA>", ms(2000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "VERSION"), std::string("0031"));
    CHECK_EQ(field(o, "STAT"), std::string("00 RAW 00"));
    CHECK(o.find("\nKEY?k GOT 6B\r\n") != std::string::npos);  // echoed
    CHECK(o.find("\nRAW? GOT 72\r\n") != std::string::npos);   // not
    CHECK_EQ(field(o, "WAIT"), std::string("01 GOT 73"));
    CHECK(o.find("\nQ\r\na       b\r\n") != std::string::npos);
    CHECK(o.find("LINE?hello wo\r COUNT 08 hello wo\r\n") != std::string::npos);
}

TEST(disk_errors_from_the_card_a_read_only_unit_and_no_card)
{
    Rig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/A.IMG"), 0);                // writable at boot
    REQUIRE(r.boot());
    CHECK_EQ(r.mount(2, "/OODOS/A.IMG", true), 0);           // then read-only, behind the BIOS's back
    const uint64_t commands = r.sd().commands();
    r.poke_com(read_bytes(SYS + "/wptest.com"));
    const size_t from = r.out.size();
    r.type("GO\r");
    REQUIRE(r.until("EJECT?", ms(2000), from));
    CHECK_EQ(field(r.out.substr(from), "WP"), std::string("02")); // the card's write-protect
    CHECK(r.sd().commands() > commands);                     // it got as far as the card
    r.sd().eject();
    r.type("e");
    CHECK(r.until("\r\nA>", ms(2000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "NOCARD"), std::string("01"));
    CHECK_EQ(field(o, "GONE"), std::string("0000"));
}

TEST(the_ccp_refuses_what_it_cant_run)
{
    Booted r;
    REQUIRE(r.ok);
    size_t from = r.out.size();
    r.type("   \r");                                         // nothing: the prompt again
    CHECK(r.until("\r\nA>", ms(1000), from + 2));
    from = r.out.size();
    r.type("FOO bar\r");
    CHECK(r.until("\nFOO?\r\nA>", ms(1000), from));
    r.type("TOOLONGNAME\r");
    CHECK(r.until("\nTOOLONGNAME?\r\nA>", ms(1000), from));
    std::vector<uint8_t> com = read_bytes(SYS + "/hello.com");
    REQUIRE(com.size() > 16);
    auto refused = [&](std::vector<uint8_t> c, const std::string &why) {
        r.poke_com(c);
        const size_t at = r.out.size();
        r.type("GO\r");
        CHECK(r.until(why + "\r\nA>", ms(1000), at));
        CHECK(r.out.find("Hello", at) == std::string::npos);
    };
    std::vector<uint8_t> c = com;
    c[3] = 0x1B;                                             // not the magic
    refused(c, "NO PROGRAM");
    c = com, c[4] = 0x01;                                    // a .PRL
    refused(c, "BAD PROGRAM HEADER");
    c = com, c[4] = 0x40;                                    // a flag nobody knows
    refused(c, "BAD PROGRAM HEADER");
    c = com, c[5] = 0x04;                                    // not linked at $0300
    refused(c, "BAD PROGRAM HEADER");
    c = com, c[6] = 0, c[7] = 0, c[8] = 0xFE, c[9] = 0xBC;  // image and BSS to $BFFE: a byte too many
    refused(c, "BAD PROGRAM HEADER");
    c = com, c[6] = 0xFF, c[7] = 0xFF, c[8] = 0x02, c[9] = 0;  // past 64 KB
    refused(c, "BAD PROGRAM HEADER");
    // To $BFFD exactly fits: HELLO with BSS to there runs, as the real one does.
    c = com, c[8] = static_cast<uint8_t>(0xBFFD - 0x0300 - (com[6] | com[7] << 8)),
    c[9] = static_cast<uint8_t>((0xBFFD - 0x0300 - (com[6] | com[7] << 8)) >> 8);
    r.poke_com(c);
    from = r.out.size();
    r.type("GO\r");
    CHECK(r.until("Hello from OODOS", ms(2000), from));
    CHECK(r.until("\r\nA>", ms(2000), from));
    r.poke_com(com);
    from = r.out.size();
    r.type("GO\r");
    CHECK(r.until("Hello from OODOS", ms(2000), from));
}

TEST(break_and_brk_reach_the_roms_monitor_with_w3_and_vec_on_block_63)
{
    {
        Booted r;
        REQUIRE(r.ok);
        UsbCard *usb = r.machine->usb_card();
        const size_t from = r.out.size();
        usb->key(UsbCard::PAUSE, true);                      // Break, at the prompt (in the BDOS)
        r.run(ms(20));
        usb->key(UsbCard::PAUSE, false);
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(r.nb(3), ROM_BLOCK_RO);
        CHECK_EQ(r.nb(4), ROM_BLOCK_RO);
        CHECK_EQ(r.nb(0), 0);
        CHECK_EQ(r.nb(1), 1);
        CHECK_EQ(r.nb(2), 2);
        const size_t at = r.out.size();
        r.type("C683\r");                                    // the monitor works: WIN3 as it reads it
        CHECK(r.until("C683: 7F", ms(1000), at));
    }
    {
        Booted r;
        REQUIRE(r.ok);
        // A program of one BRK.
        std::vector<uint8_t> brk = {0x4F, 0x4F, 0x44, 0x1A, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00};
        r.poke_com(brk);
        const size_t from = r.out.size();
        r.type("GO\r");
        CHECK(r.until("\\\r", ms(1000), from));
        CHECK_EQ(r.nb(3), ROM_BLOCK_RO);
        CHECK_EQ(r.nb(4), ROM_BLOCK_RO);
    }
}

TEST(an_irq_that_isnt_brk_returns)
{
    Booted r;
    REQUIRE(r.ok);
    const uint16_t irq = sym("IRQ");
    bool given = false, taken = false, returned = false;
    r.watch = [&](uint16_t pc) {
        if (!given && pc >= 0x0300 && pc < 0x0320 && !(r.cpu->regs().p & Cpu6502::FLAG_I) && r.nb(0) == 0)
        {
            r.cpu->irq();                                    // in IRQTEST's wait, interrupts on
            given = true;
            taken = r.cpu->regs().pc == irq;                 // through VEC, block 62's vector
        }
        if (taken && pc == irq + 12)                         // its RTI
            returned = true;
    };
    const std::string o = r.go("irqtest.com");
    CHECK_EQ(r.phys(COMMON_PHYS + irq + 12 - 0xF800), 0x40);
    CHECK(given);
    CHECK(taken);
    CHECK(returned);
    CHECK(o.find("IRQ RETURNED\r\n\r\nA>") != std::string::npos);
}

int main()
{
    if (read_bytes(SYS + "/OODOS.SYS").empty() || read_bytes(EMU_BIOS_ROM).empty())
    {
        std::printf("no OODOS.SYS or bios.rom (needs ca65, ld65, python3 and make): skipped\n");
        return SKIP;
    }
    return run_all();
}
