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
    constexpr uint32_t SYS_BLOCK_PHYS = 31 * BLOCK;      ///< bank 0's $0000
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

    // ---- phase 3a: the BDOS's files --------------------------------------------------------

    constexpr uint32_t MDISK = 0xC0000, MDISK_SIZE = 0x20000; ///< M:, banks C-D
    constexpr uint32_t SEC32 = 512, REC32 = 128;

    /// An OODOS drive read behind OODOS's back, as the test's oracle: oodos-8m on a card unit
    /// (read through the card's registers while OODOS waits), or M: in banks C-D. Not a file
    /// system: entries and records found by the DPB, as cpmtools would.
    struct CpmDisk
    {
        Rig &r;
        unsigned unit; ///< 1-7, 0 for M:
        unsigned bls() const { return unit ? 4096 : 2048; }
        unsigned entries() const { return unit ? 1024 : 256; }
        bool words() const { return unit != 0; } ///< 16-bit block pointers (DSM > 255)
        unsigned exm() const { return 1; }       ///< both formats' EXM

        std::vector<uint8_t> sector(uint32_t lba)
        {
            if (unit)
                return r.read_unit(unit, lba);
            std::vector<uint8_t> s(SECTOR);
            for (size_t i = 0; i < SECTOR; ++i)
                s[i] = r.phys(MDISK + lba * SEC32 + static_cast<uint32_t>(i));
            return s;
        }
        void put(uint32_t lba, const std::vector<uint8_t> &s)
        {
            REQUIRE(unit == 0); // M: only: the card's sectors are OODOS's
            r.poke(MDISK + lba * SEC32, s);
        }
        std::vector<uint8_t> directory()
        {
            std::vector<uint8_t> d;
            for (uint32_t n = 0; n * SEC32 < entries() * 32; ++n)
            {
                const std::vector<uint8_t> s = sector(n);
                d.insert(d.end(), s.begin(), s.end());
            }
            return d;
        }
        /// A file's entries (user `u`, an 8 + 3 name), in extent order.
        std::vector<unsigned> entries_of(const std::vector<uint8_t> &d, unsigned u, const std::string &name83)
        {
            std::vector<std::pair<unsigned, unsigned>> found;
            for (unsigned i = 0; i < entries(); ++i)
            {
                const uint8_t *e = &d[i * 32];
                bool same = e[0] == u;
                for (unsigned c = 0; c < 11 && same; ++c)
                    same = (e[1 + c] & 0x7F) == static_cast<uint8_t>(name83[c]);
                if (same)
                    found.push_back({unsigned{e[14]} * 32 + e[12], i});
            }
            std::sort(found.begin(), found.end());
            std::vector<unsigned> out;
            for (const auto &f : found)
                out.push_back(f.second);
            return out;
        }
        unsigned pointer(const uint8_t *e, unsigned k) const
        {
            return words() ? unsigned{e[16 + 2 * k]} | unsigned{e[17 + 2 * k]} << 8 : e[16 + k];
        }
        /// Record `n` of a file (its entries as entries_of gives them); empty if it has no block.
        std::vector<uint8_t> record(const std::vector<uint8_t> &d, const std::vector<unsigned> &es, unsigned n)
        {
            const unsigned group = n / 128 / (exm() + 1), per = bls() / REC32;
            for (unsigned i : es)
            {
                const uint8_t *e = &d[i * 32];
                if ((unsigned{e[14]} * 32 + e[12]) / (exm() + 1) != group)
                    continue;
                const unsigned in = n % (128 * (exm() + 1)), block = pointer(e, in / per);
                if (block == 0)
                    return {};
                const uint32_t at = block * bls() + (in % per) * REC32;
                const std::vector<uint8_t> s = sector(at / SEC32);
                return std::vector<uint8_t>(s.begin() + static_cast<long>(at % SEC32),
                                            s.begin() + static_cast<long>(at % SEC32 + REC32));
            }
            return {};
        }
    };

    /// FSTEST's record pattern (oodos/sys/test/fs.inc's FILL).
    std::vector<uint8_t> pattern_record(unsigned n)
    {
        std::vector<uint8_t> b(RECORD);
        for (unsigned j = 0; j < RECORD; ++j)
            b[j] = static_cast<uint8_t>(((n & 0xFF) + j) ^ (n >> 8) ^ 0x5A);
        return b;
    }

    /// Whether these bytes are anywhere in physical memory from `from`, `len` bytes.
    bool in_memory(Rig &r, uint32_t from, uint32_t len, const std::vector<uint8_t> &what)
    {
        std::vector<uint8_t> m(len);
        for (uint32_t i = 0; i < len; ++i)
            m[i] = r.phys(from + i);
        return std::search(m.begin(), m.end(), what.begin(), what.end()) != m.end();
    }
    /// Whether these bytes are anywhere on the card.
    bool on_card(Rig &r, const std::vector<uint8_t> &what)
    {
        const std::vector<uint8_t> &c = r.media->bytes();
        return std::search(c.begin(), c.end(), what.begin(), what.end()) != c.end();
    }

    /// Run a test program by GO with `tail`, without waiting for the prompt.
    size_t start(Rig &r, const std::string &com, const std::string &tail = "")
    {
        r.poke_com(read_bytes(SYS + "/" + com));
        const size_t from = r.out.size();
        r.type("GO" + tail + "\r");
        return from;
    }

    /// A 24-bit hex field as a number.
    unsigned hexval(const std::string &s) { return static_cast<unsigned>(std::stoul(s, nullptr, 16)); }
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
    // The jump table's W2 handling (common.s's BIOSW): cycles from the entry to the BIOS's
    // CONOUT, and from CONOUT's return to the caller, less the entry's JMP before.
    uint64_t j0 = 0, j1 = 0, j2 = 0, j3 = 0;
    const uint16_t entry = static_cast<uint16_t>(0xF800 + 3 * 4), after = static_cast<uint16_t>(sym("W_CONOUT") + 18);
    auto stub = r.watch;
    r.watch = [&](uint16_t pc) {
        stub(pc);
        if (j3)
            return;
        if (!j0 && pc == entry)
            j0 = r.cpu->cycles();
        else if (j0 && !j1 && pc == sym("CONOUT"))
            j1 = r.cpu->cycles();
        else if (j1 && !j2 && pc == after)
            j2 = r.cpu->cycles();
        else if (j2 && pc < 0xF800)
            j3 = r.cpu->cycles();
    };
    const std::string o = r.go("hello.com", " world");
    REQUIRE(j3 != 0);
    std::printf("  a BIOS call through the jump table: %llu cycles more than a JMP (%llu in, %llu out)\n",
                static_cast<unsigned long long>(j1 - j0 - 3 + j3 - j2), static_cast<unsigned long long>(j1 - j0 - 3),
                static_cast<unsigned long long>(j3 - j2));
    CHECK_EQ(j1 - j0 - 3 + j3 - j2, uint64_t{51});
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
    CHECK_EQ(field(o, "BOUNCEW A"), std::string("00"));  // a WRITE from across the windows
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
    // BOUNCEW's sector, from $3F00-$3FFF in block 0 and $4000-$40FF in block 4: i ^ $A5.
    const std::vector<uint8_t> w = r.read_unit(1, 1023 * 16 + 14);
    bool bounced = true;
    for (size_t i = 0; i < SECTOR; ++i)
        bounced = bounced && w[i] == static_cast<uint8_t>((i & 0xFF) ^ 0xA5);
    CHECK(bounced);
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

TEST(the_bdos_stub_writes_nothing_in_w3_so_it_may_be_read_only)
{
    // STUBTEST with a tail: the home block in W3 read-only, common memory in it.
    Booted r;
    REQUIRE(r.ok);
    give_common(r, 20);
    const std::string home = "HOME BLOCK\r\n$";
    r.poke(20 * BLOCK + 0x800, std::vector<uint8_t>(home.begin(), home.end()));
    std::vector<uint8_t> common(0x800);
    for (uint32_t i = 0; i < 0x800; ++i)
        common[i] = r.phys(20 * BLOCK + 0x3800 + i);
    const std::string o = r.go("stubtest.com", " RO");
    CHECK(o.find("\r\nW1 STRING\r\nHOME BLOCK\r\n") != std::string::npos);
    CHECK_EQ(field(o, "WINDOWS"), std::string("03 04 05 54 "));
    CHECK_EQ(field(o, "NOFUNC"), std::string("FFFF"));
    CHECK(o.find("\r\nA>") != std::string::npos);
    bool same = true;
    for (uint32_t i = 0; i < 0x800; ++i)
        same = same && r.phys(20 * BLOCK + 0x3800 + i) == common[i];
    CHECK(same);
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

TEST(a_clock_past_2155_is_no_time)
{
    // The SCB's days are said to reach 2155: a clock in 2156 is read as no time (BIOS TIME's
    // carry), so the banner has no date and function 105 returns $FF, the SCB left alone.
    Rig r;
    REQUIRE(r.ok);
    REQUIRE(r.mount(1, "/OODOS/A.IMG") == 0);
    r.set_time(2156, 3, 1, 10, 20, 30);
    REQUIRE(r.boot());
    CHECK(r.out.find("\r\nOODOS 0.2  \r\n\r\nA>") != std::string::npos);
    std::string o = r.go("timetest.com");
    const std::string t = field(o, "TIME");
    CHECK_EQ(t.substr(t.size() - 2), std::string("FF"));
    // Nor is a date past 2155 set: 1 January 2156 leaves the clock in March.
    o = r.go("timetest.com", " " + hex(days(2156, 1, 1), 4) + " 09 15");
    r.run(ms(5));
    const auto c = r.bridge().rtc_time();
    CHECK_EQ(c[5] & 0x1F, 0x03);
    CHECK_EQ(c[6], 0x56);
    // 2155 is still a time.
    r.set_time(2155, 12, 31, 23, 0, 0);
    o = r.go("timetest.com");
    CHECK_EQ(field(o, "TIME").substr(0, 10), hex(days(2155, 12, 31), 4) + " 23 00");
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

TEST(a_warm_boot_resets_the_banks_and_function_50_wont_boot)
{
    Booted r;
    REQUIRE(r.ok);
    // In the program's memory, where bank 1 would have put them: sentinels.
    r.poke(0x1000, std::vector<uint8_t>(SECTOR, 0xA5));
    r.poke(0x1800, std::vector<uint8_t>(16, 0xA5));
    CHECK(r.go("banktest.com", " SET").find("\r\nA>") != std::string::npos);
    const std::string o = r.go("banktest.com", " USE");
    CHECK_EQ(field(o, "READ"), std::string("00"));
    const std::vector<uint8_t> a0 = r.read_unit(1, 0);
    bool sector = true, moved = true, untouched = true;
    for (uint32_t i = 0; i < SECTOR; ++i)
    {
        sector = sector && r.phys(SYS_BLOCK_PHYS + 0x1000 + i) == a0[i];
        untouched = untouched && r.phys(0x1000 + i) == 0xA5;
    }
    for (uint32_t i = 0; i < 16; ++i)
    {
        moved = moved && r.phys(SYS_BLOCK_PHYS + 0x1800 + i) == r.phys(60 * BLOCK + i);
        untouched = untouched && r.phys(0x1800 + i) == 0xA5;
    }
    CHECK(sector);    // READ into bank 0: SETBNK's 1 didn't outlive its program
    CHECK(moved);     // MOVE in bank 0: nor SELMEM's 1, nor the XMOVE left pending
    CHECK(untouched);
    // Function 50 of BOOT is refused: cold boot would format M: again.
    r.poke(0xC0000 + 32, std::vector<uint8_t>{0x00, 'K', 'E', 'E', 'P'});
    const std::string b = r.go("banktest.com", " BOOT");
    CHECK_EQ(field(b, "BOOT"), std::string("FFFF"));
    CHECK_EQ(r.phys(0xC0000 + 33), 'K');
    size_t banners = 0;
    for (size_t at = r.out.find("OODOS 0.2"); at != std::string::npos; at = r.out.find("OODOS 0.2", at + 1))
        ++banners;
    CHECK_EQ(banners, size_t{1});
}

TEST(neither_the_bdos_nor_the_bios_takes_the_io_hole_or_flash_for_ram)
{
    Booted r;
    REQUIRE(r.ok);
    // Block 62's RAM under the hole, and block 4: as they were after.
    r.poke(62 * BLOCK + 0x100, std::vector<uint8_t>(SECTOR, 0x3C));
    r.poke(62 * BLOCK + 0x400, std::vector<uint8_t>(16, 0x3C));
    r.poke(4 * BLOCK, std::vector<uint8_t>(SECTOR, 0x4D));
    const std::string o = r.go("holetest.com");
    CHECK_EQ(field(o, "STRING"), std::string("FFFF"));
    CHECK_EQ(field(o, "HOLE"), std::string("01"));
    CHECK_EQ(field(o, "ROM"), std::string("01"));
    CHECK_EQ(field(o, "RO"), std::string("01"));
    CHECK_EQ(field(o, "ROW"), std::string("00"));
    CHECK_EQ(field(o, "MOVE"), std::string("FFFF"));
    CHECK_EQ(field(o, "MOVER"), std::string("FFFF"));
    CHECK_EQ(field(o, "OK"), std::string("00"));
    bool kept = true;
    for (uint32_t i = 0; i < SECTOR; ++i)
        kept = kept && r.phys(62 * BLOCK + 0x100 + i) == 0x3C && r.phys(4 * BLOCK + i) == 0x4D;
    for (uint32_t i = 0; i < 16; ++i)
        kept = kept && r.phys(62 * BLOCK + 0x400 + i) == 0x3C;
    CHECK(kept);
    // ROW wrote block 4's bytes, read-only to the program but read all the same.
    const std::vector<uint8_t> s = r.read_unit(1, 1023 * 16 + 13);
    CHECK(std::all_of(s.begin(), s.end(), [](uint8_t b) { return b == 0x4D; }));
}

/// BANKTEST USE's READ, with `hook` called once when the BIOS gets to `label`. @return READ's
/// result, and the cycles from `label` to SDRESET, if the BIOS gave up, in `waited`
std::string read_with(Rig &r, const std::string &label, const std::function<void()> &hook, uint64_t &waited)
{
    const uint16_t at = sym(label), end = sym("SDRESET");
    bool done = false;
    uint64_t t0 = 0;
    waited = 0;
    r.watch = [&](uint16_t pc) {
        if (!done && pc == at && r.nb(2) == 61)
        {
            done = true;
            t0 = r.cpu->cycles();
            hook();
        }
        else if (done && !waited && pc == end && r.nb(2) == 61)
            waited = r.cpu->cycles() - t0;
    };
    const std::string o = r.go("banktest.com", " USE", ms(6000));
    r.watch = nullptr;
    return field(o, "READ");
}

TEST(the_bios_waits_for_the_card_bounded_and_checks_the_dma_moved_it_all)
{
    Booted r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> a0 = r.read_unit(1, 0);
    auto read_ok = [&] {
        bool same = true;
        for (uint32_t i = 0; i < SECTOR; ++i)
            same = same && r.phys(SYS_BLOCK_PHYS + 0x1000 + i) == a0[i];
        r.poke(SYS_BLOCK_PHYS + 0x1000, std::vector<uint8_t>(SECTOR, 0));
        return same;
    };
    uint64_t waited = 0;
    // The card busy with a FLUSH as READ starts: READ waits for it, then reads.
    CHECK_EQ(read_with(r, "SDIO", [&] { r.sd().write(SdCard::CMD, 0xE7); }, waited), std::string("00"));
    CHECK(read_ok());
    // The card done but channel 0 short of its terminal count: an error, not 0.
    CHECK_EQ(read_with(r, "SDEND", [&] { r.machine->bus().write(0xC440, 0x01); }, waited), std::string("01"));
    // A command that never ends (channel 0 stopped under it): given about a second, then the
    // card reset, its units still mounted.
    CHECK_EQ(read_with(r, "SDWAIT", [&] { r.machine->bus().write(0xC442, 0x01); }, waited), std::string("01"));
    std::printf("  a READ that never ends: given up after %.2f s\n", static_cast<double>(waited) / HZ);
    CHECK(waited > ms(900) && waited < ms(1500));
    r.run(ms(SdCard::INIT_MS + 10));
    CHECK_EQ(read_with(r, "SDIO", [] {}, waited), std::string("00"));
    CHECK(read_ok());
    // The card still busy with something else as READ starts: given about a second too.
    auto stuck = [&] { // a READ through DATA that nobody takes: BUSY until reset
        r.sd().write(SdCard::CTRL, 0);
        r.sd().write(SdCard::UNIT, 0);
        r.sd().write(SdCard::COUNT, 1);
        r.sd().write(SdCard::CMD, SdCard::READ);
    };
    CHECK_EQ(read_with(r, "SDIO", stuck, waited), std::string("01"));
    std::printf("  a card busy from before: given up after %.2f s\n", static_cast<double>(waited) / HZ);
    CHECK(waited > ms(900) && waited < ms(1500));
    r.run(ms(SdCard::INIT_MS + 10));
    CHECK_EQ(read_with(r, "SDIO", [] {}, waited), std::string("00"));
    CHECK(read_ok());
}

TEST(the_ccps_load_gives_up_on_a_card_that_stays_busy)
{
    // The CCP opens HELLO.COM through the BDOS, which logs A: in first: the card busy as it
    // starts reading the directory, so the BIOS's READ gives up, the BDOS returns Disk I/O (the
    // CCP's error mode returns errors), and the CCP says READ ERROR.
    Booted r;
    REQUIRE(r.ok);
    bool stuck = false;
    uint64_t t0 = 0;
    r.watch = [&](uint16_t pc) {
        if (!stuck && pc == sym("LOGIN"))
        {
            stuck = true;
            t0 = r.cpu->cycles();
            r.sd().write(SdCard::CTRL, 0);
            r.sd().write(SdCard::UNIT, 0);
            r.sd().write(SdCard::COUNT, 1);
            r.sd().write(SdCard::CMD, SdCard::READ);
        }
    };
    const size_t from = r.out.size();
    r.type("HELLO\r");
    CHECK(r.until("READ ERROR\r\nA>", ms(4000), from));
    const uint64_t took = r.cpu->cycles() - t0;
    std::printf("  A:'s login on a busy card: READ ERROR after %.2f s\n", static_cast<double>(took) / HZ);
    CHECK(stuck && took > ms(900));
    r.watch = nullptr;
    r.run(ms(SdCard::INIT_MS + 10));
    const size_t again = r.out.size();
    r.type("HELLO\r");                                      // reset, so the next one reads
    CHECK(r.until("Hello from OODOS", ms(4000), again));
}

/// FSTEST on a drive: its results, the last sector held back until close (a write-back at close),
/// and the BIOS's READs while it reads the file back (deblocking: at most one a sector).
void fstest(Rig &r, char drive, unsigned free_records_per_block, bool big = false)
{
    REQUIRE(r.ok);
    CpmDisk disk{r, drive == 'M' ? 0u : 1u};
    const auto fl = labels(SYS + "/fstest.lbl");
    bool counting = false;
    unsigned reads = 0;
    // Each BDOS call's cycles, BDOSENT to STUBRET, and whether it read, wrote or zeroed a block.
    struct Call
    {
        uint8_t fn;
        uint64_t cycles;
        bool read, write, zero;
    };
    std::vector<Call> calls;
    Call now{};
    uint64_t t0 = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == fl.at("rdbegin"))
            counting = true;
        else if (pc == fl.at("rdend"))
            counting = false;
        else if (pc == sym("READ"))
        {
            reads += counting;
            now.read = true;
        }
        else if (pc == sym("WRITE"))
            now.write = true;
        else if (pc == sym("ZEROBLK"))
            now.zero = true;
        else if (pc == sym("BDOSENT"))
        {
            now = Call{r.cpu->regs().y, 0, false, false, false};
            t0 = r.cpu->cycles();
        }
        else if (pc == sym("STUBRET"))
        {
            now.cycles = r.cpu->cycles() - t0;
            calls.push_back(now);
        }
    };
    const size_t from = start(r, "fstest.com", std::string(" ") + drive);
    REQUIRE(r.until("WRITTEN?", ms(20000), from));
    // The last sector is still in its buffer: records 296-299 aren't on the disk yet.
    const std::vector<uint8_t> last = pattern_record(299);
    CHECK(!(drive == 'M' ? in_memory(r, MDISK, MDISK_SIZE, last) : on_card(r, last)));
    r.type(" ");
    REQUIRE(r.until("CLOSED?", ms(5000), from));
    CHECK(drive == 'M' ? in_memory(r, MDISK, MDISK_SIZE, last) : on_card(r, last));
    const std::vector<uint8_t> dir = disk.directory();
    const std::vector<unsigned> es = disk.entries_of(dir, 0, "SEQ     DAT");
    if (!big) // CpmDisk knows oodos-8m and M:
    {
        CHECK_EQ(es.size(), size_t{2}); // 256 records an entry
        bool all = es.size() == 2;
        for (unsigned n = 0; n < 300 && all; ++n)
            all = disk.record(dir, es, n) == pattern_record(n);
        CHECK(all);
    }
    r.type(" ");
    CHECK(r.until("FSTEST DONE", ms(20000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    r.watch = nullptr;
    const std::string o = r.out.substr(from);
    const unsigned free0 = hexval(field(o, "FREE0"));
    const unsigned blocks = (300 + free_records_per_block - 1) / free_records_per_block;
    CHECK_EQ(hexval(field(o, "FREE1")), free0 - blocks * free_records_per_block);
    CHECK_EQ(field(o, "FREE2"), field(o, "FREE0"));
    CHECK_EQ(field(o, "MAKE").substr(2), std::string(" 00"));
    CHECK_EQ(field(o, "WRITE"), std::string("012C 00"));
    CHECK_EQ(field(o, "CLOSE").substr(2), std::string(" 00"));
    CHECK_EQ(field(o, "OPEN"), field(o, "MAKE")); // the same entry: the first, extent 0's
    CHECK_EQ(field(o, "RC"), std::string("80"));
    CHECK_EQ(field(o, "READ"), std::string("012C 01"));
    CHECK_EQ(field(o, "RANDREC"), std::string("00012C"));
    CHECK_EQ(field(o, "SIZE"), std::string("00012C"));
    // Record 600's extent, 4: no entry has it, but on oodos-32m (EXM 3) 1000's has, without its block.
    for (const char *want : {"00C8 00 Y", "012B 00 Y", "012C 01 N", "1388 04 N", big ? "0258 01 N" : "0258 04 N",
                             "0384 01 N", "03E8 00 Y", "03E9 01 N", "03EA 00 Y"})
        if (o.find(std::string("\nRREAD ") + want + "\r") == std::string::npos)
        {
            std::printf("  no RREAD %s in:\n%s\n", want, o.c_str());
            CHECK(false);
        }
    CHECK_EQ(field(o, "RREAD 40000"), std::string("06"));
    CHECK_EQ(field(o, "RWRITE 1000"), std::string("00"));
    CHECK_EQ(field(o, "RWRITEZ 1003"), std::string("00"));
    CHECK_EQ(field(o, "CLOSE2").substr(2), std::string(" 00"));
    CHECK_EQ(field(o, "SIZE2"), std::string("0003EC"));
    CHECK_EQ(field(o, "DELETE"), std::string("00 00"));
    CHECK_EQ(field(o, "OPEN2"), std::string("FF 00"));
    std::printf("  %c: the BIOS's READs reading 300 records back: %u\n", drive, reads);
    // The BDOS's cycles (BDOSENT to its return), the least of each kind of call.
    auto least = [&](uint8_t fn, const std::function<bool(const Call &)> &which) {
        uint64_t best = 0;
        for (const Call &c : calls)
            if (c.fn == fn && which(c) && (best == 0 || c.cycles < best))
                best = c.cycles;
        return best;
    };
    const uint64_t open = least(15, [](const Call &) { return true; }),
                   cached = least(20, [](const Call &c) { return !c.read; }),
                   missed = least(20, [](const Call &c) { return c.read && !c.write; }),
                   wcached = least(21, [](const Call &c) { return !c.read && !c.write && !c.zero; }),
                   wnew = least(21, [](const Call &c) { return c.zero && !c.write; }),
                   wback = least(21, [](const Call &c) { return c.zero && c.write; });
    // A write's write-backs come with a new block: its sectors take buffers (zeroed), and those
    // the earlier blocks changed are written back as they're taken.
    std::printf("  %c: BDOS cycles: open %llu; read a record of a buffered sector %llu, of one read %llu; "
                "write to a buffered sector %llu, to a new block %llu, a new block's buffers written back first %llu\n",
                drive, static_cast<unsigned long long>(open), static_cast<unsigned long long>(cached),
                static_cast<unsigned long long>(missed), static_cast<unsigned long long>(wcached),
                static_cast<unsigned long long>(wnew), static_cast<unsigned long long>(wback));
    CHECK(open && cached && missed && wcached);
    if (drive == 'A')
        CHECK(wback != 0); // 32 buffers: writing 300 records needs some written back first
    else
        CHECK(wnew != 0);
    CHECK(reads > 0);   // buffers were reused: some of the file had to be read again
    CHECK(reads <= 75 + 2); // a sector at most once for its four records, and the directory's
                            // as each of the next two extents is opened
}

TEST(fstest_writes_reads_seeks_and_deletes_on_a)
{
    Booted r;
    fstest(r, 'A', 32);
}

TEST(fstest_on_an_oodos_32m_drive)
{
    // A 32 MB image as A:, its directory (8 blocks of 8 KB) $E5s: empty.
    std::vector<uint8_t> card = fat16_card({{"BIG.IMG", 65536u * 512}});
    auto word = [&](size_t at) { return size_t{card[at]} | size_t{card[at + 1]} << 8; };
    const size_t root = (word(14) + card[16] * word(22)) * SECTOR;
    const size_t data = root + word(17) * 32; // BIG.IMG is the first file
    std::fill(card.begin() + static_cast<long>(data), card.begin() + static_cast<long>(data + 2048 * 32), 0xE5);
    Rig r(card);
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/BIG.IMG"), 0);
    REQUIRE(r.boot());
    fstest(r, 'A', 64, true);
    CHECK_EQ(field(r.out, "FREE0"), std::string("03FE00")); // 4,088 blocks of 64 records
}

TEST(fstest_on_m_with_byte_pointers)
{
    Booted r;
    fstest(r, 'M', 16);
    const std::string o = r.out;
    CHECK_EQ(field(o, "FREE0"), std::string("0003C0")); // 60 blocks of 16 records: an empty M:
    CHECK_EQ(field(o, "MAKE"), std::string("01 00"));    // entry 1, after the label
}

TEST(dirtest_searches_users_attributes_names_and_full_drives_on_m)
{
    Booted r;
    REQUIRE(r.ok);
    const size_t from = start(r, "dirtest.com");
    CHECK(r.until("DIRTEST DONE", ms(60000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    const std::string o = r.out.substr(from);
    const std::pair<const char *, const char *> want[] = {
        {"SEARCH", "A1 A2"},           // A?.TXT, in the directory's order
        {"COUNT", "03"},               // A1, A2, B1 in user 0
        {"ALL", "0100 LABEL 01 SFCB 40 FREE 00BC"}, // '?' as the drive: all 256, free ones too
        {"USER3", "01"},               // user 3 sees only its own
        {"OPEN3", "FF 00"},            // A1 isn't SYS yet
        {"SYSOPEN", "01 00"},          // now it is: from user 0,
        {"F8", "80"},                  // f8' set
        {"SYSREAD", "00 00"},
        {"SYSWRITE", "FF 03"},         // File R/O
        {"SEARCH3", "FF 00"},          // search doesn't look in user 0
        {"DELRO", "FF 03"},
        {"RENRO", "FF 03"},
        {"WRITERO", "FF 03"},
        {"BYTECOUNT", "55"},
        {"DELX", "00 00"},             // found, but nothing to delete with f5'
        {"XKEPT", "01 00"},
        {"BITS", "C2 D4"},             // f1' and t3' on the disk
        {"OPENBITS", "C2 D4"},         // and in the FCB after Open
        {"REN", "00 00"},
        {"SEARCHB1", "FF 00"},
        {"SEARCHC1", "00 00"},
        {"RENEX", "FF 08"},            // File Exists
        {"RENWILD", "FF 09"},          // ? in Filename
        {"MAKEEX", "FF 08"},
        {"MAKEWILD", "FF 09"},
        {"OPENWILD", "FF 09"},
        {"HASHOPEN", "FF 00"},         // HASHBA.TXT's hash is HASHAG.TXT's: not taken for it
        {"HASHFIND", "FF 00"},
        {"HASHAG", "02 00"},
        {"BIGREN", "00 00"},
        {"BIGCOUNT", "02 OLDCOUNT 00"}, // both entries renamed
        {"BIGDEL", "00 00"},
        {"BIGGONE", "00 FREEBACK Y"},  // both deleted, every block back
        {"DIRFULL", "00BA FF"},        // 186 more: 191 entries less the five files
        {"WILDDEL", "00 00"},
        {"AFTERDEL", "00"},
        {"DISKFULL", "0370 02"},       // 55 free blocks of 16 records, then disk full
        {"RANDFULL", "02"},
        {"FREEFULL", "000000"},
        {"FREE98", "000340 000370"},   // 3 blocks held by a file not closed, freed by 98
        {"TRUNC", "00 00"},
        {"SIZE", "000064"},
        {"R99", "00Y R100 01"},
        {"TRUNCFREE", "Y"},            // 12 of its 19 blocks freed
        {"TRUNCBIG", "FF 00"},
        {"TRUNCWILD", "FF 09"},
        {"ROMAKE", "FF 02"},           // Read Only
        {"ROVEC", "00 10"},            // M:, bit 12
        {"ROWRITE", "FF 02"},
        {"ROVEC2", "00 00"},           // 37 made it writable
        {"MAKEAGAIN", "02 00"},        // entry 10, after TR.DAT and W.DAT
        {"LOGIN", "00 10"},            // M: only
        {"DRIVE", "0C 00"},
        {"ALLOC", "FF FF"},
        {"VERSION", "31 00"},
        {"LOCK", "00 00"},
        {"UNLOCK", "00 00"},
        {"SIZEGONE", "FF 00"},
        {"SNEXTEND", "FF 00"},
        {"FREEBAD", "FF 04"},          // Invalid Drive: F: isn't mounted
        {"SELBAD", "FF 04"},           // P:
        {"FLUSH", "00 00"},
        {"PURGE", "00 00"},
        {"RESET", "00 00"},
        {"RDRIVE", "00 00"},           // A: after 13
        {"RLOGIN", "00 00"},
        {"RROVEC", "00 00"},           // and nothing read-only
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    CHECK_EQ(field(o, "DPB"), dpb_hex("oodos-ram"));
    const std::string rdma = field(o, "RDMA"); // at $0280 after 13, as at BUF
    CHECK(rdma.size() == 13 && rdma.substr(0, 6) == rdma.substr(7));
    if (o.find("DIRTEST DONE") == std::string::npos)
        r.tail(2000);
}

/// ERRTEST's case `c` to the prompt, `between` called at its prompt `wait` if it has one.
std::string errtest(Rig &r, char c, const std::string &wait = "", const std::function<void()> &between = {})
{
    const size_t from = start(r, "errtest.com", std::string(" ") + c);
    if (!wait.empty())
    {
        CHECK(r.until(wait, ms(10000), from));
        between();
        r.type(" ");
    }
    CHECK(r.until("\r\nA>", ms(10000), from + 4));
    const std::string o = r.out.substr(from);
    CHECK(o.find("NOT REACHED") == std::string::npos); // the default mode ended the program
    return o;
}
size_t count_of(const std::string &text, const std::string &what)
{
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        ++n;
    return n;
}

TEST(errors_return_say_or_end_the_program_as_the_error_mode_says)
{
    {
        Booted r; // B: is a read-only unit
        REQUIRE(r.ok);
        const std::string o = errtest(r, 'B');
        CHECK_EQ(field(o, "RO"), std::string("FF 02")); // mode $FF: only returned
        const std::string msg = "\r\nOODOS Error On B: Read Only\r\nBDOS Function = 22 File = X       .TXT\r\n";
        CHECK_EQ(count_of(o, msg), size_t{2});         // mode $FE and the default say it
        CHECK(o.find(msg + "BACK FF 02\r\n") != std::string::npos); // $FE returned after
        CHECK(o.find("RO FF 02\r\nBACK") == std::string::npos);
    }
    {
        Booted r;
        REQUIRE(r.ok);
        const std::string o = errtest(r, 'M');
        CHECK_EQ(field(o, "EXISTS"), std::string("FF 08"));
        CHECK_EQ(field(o, "WILD"), std::string("FF 09"));
        CHECK(o.find("\r\nOODOS Error On M: File Exists\r\nBDOS Function = 22 File = ROF     .TXT\r\nBACK FF 08\r\n") !=
              std::string::npos);
        CHECK(o.find("\r\nOODOS Error On M: File R/O\r\nBDOS Function = 21 File = ROF     .TXT\r\n") != std::string::npos);
    }
    {
        Booted r;
        REQUIRE(r.ok);
        const std::string o = errtest(r, 'P');
        CHECK(o.find("\r\nOODOS Error On P: Invalid Drive\r\nBDOS Function = 14\r\n") != std::string::npos);
    }
    {
        Booted r;
        REQUIRE(r.ok);
        const std::string o = errtest(r, 'I', "EJECT?", [&] { r.sd().eject(); });
        CHECK_EQ(field(o, "IO"), std::string("FF 01"));
        CHECK(o.find("\r\nOODOS Error On A: Disk I/O\r\nBDOS Function = 15 File = README  .TXT\r\n") != std::string::npos);
    }
    {
        // B: writable when it is logged in, made read-only behind the BDOS's back: the close's
        // write-back gets the BIOS's 2, a Read Only error, and B: is read-only from then on.
        Rig r;
        REQUIRE(r.ok);
        CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
        CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
        REQUIRE(r.boot());
        const std::string o = errtest(r, 'W', "REMOUNT?", [&] { CHECK_EQ(r.mount(2, "/OODOS/B.IMG", true), 0); });
        CHECK_EQ(field(o, "WPCLOSE"), std::string("FF 02"));
        CHECK_EQ(field(o, "WPROVEC"), std::string("02 00"));
        CHECK(o.find("\r\nOODOS Error On B: Read Only\r\nBDOS Function = 22 File = WP2     .TXT\r\n") != std::string::npos);
    }
}

/// A CP/M 3 date stamp: days (1 January 1978 day 1, the DS3231's leap years), hour and minute in BCD.
std::vector<uint8_t> dat(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi)
{
    const unsigned n = days(y, mo, d);
    return {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(h / 10 << 4 | h % 10),
            static_cast<uint8_t>(mi / 10 << 4 | mi % 10)};
}

/// STAMPTEST on a drive: each SFCB byte for byte, at each step, against the clock the test set.
void stamptest(Booted &r, char drive)
{
    REQUIRE(r.ok);
    CpmDisk disk{r, drive == 'M' ? 0u : 1u};
    std::vector<uint8_t> before = disk.directory();
    // The SFCB of `name`'s entry, and what it should be: as before (with the slots checked so
    // far) but its slot, which is `first` (creation or access) and `update`, then the password
    // mode and a reserved byte, 0.
    auto check = [&](const std::string &name, const std::vector<uint8_t> &first, const std::vector<uint8_t> &update) {
        const std::vector<uint8_t> dir = disk.directory();
        const std::vector<unsigned> es = disk.entries_of(dir, 0, name);
        REQUIRE(es.size() == 1u);
        const unsigned i = es[0], at = (i | 3) * 32;
        std::vector<uint8_t> want(before.begin() + at, before.begin() + at + 32);
        CHECK_EQ(want[0], 0x21);
        std::vector<uint8_t> slot = first;
        slot.insert(slot.end(), update.begin(), update.end());
        slot.push_back(0);
        slot.push_back(0);
        std::copy(slot.begin(), slot.end(), want.begin() + 1 + (i & 3) * 10);
        std::copy(want.begin(), want.end(), before.begin() + at);
        const std::vector<uint8_t> got(dir.begin() + at, dir.begin() + at + 32);
        CHECK(got == want);
        if (got != want)
            for (unsigned k = 0; k < 32; ++k)
                std::printf("  %s SFCB byte %u: %02X, want %02X\n", name.c_str(), k, got[k], want[k]);
    };
    const std::vector<uint8_t> none = {0, 0, 0, 0};
    const auto t1 = dat(2026, 11, 3, 9, 15), t2 = dat(2026, 11, 4, 18, 30), t3 = dat(2026, 12, 31, 23, 59),
               t4 = dat(2027, 1, 1, 0, 1);
    r.set_time(2026, 11, 3, 9, 15, 0);
    const size_t from = start(r, "stamptest.com", std::string(" ") + drive);
    REQUIRE(r.until("MADE?", ms(5000), from));
    check("STAMP   DAT", t1, none); // made: its creation stamp, no update yet
    r.set_time(2026, 11, 4, 18, 30, 0);
    r.type(" ");
    REQUIRE(r.until("CLOSED?", ms(5000), from));
    check("STAMP   DAT", t1, t2); // written and closed: the update
    r.set_time(2026, 12, 31, 23, 59, 0);
    r.type(" ");
    REQUIRE(r.until("MODE?", ms(5000), from));
    check("EMPTY   DAT", t3, none); // closed unwritten: no update
    if (drive == 'M')
        r.poke(MDISK + 12, 0x61); // the label: access and update stamps
    r.set_time(2027, 1, 1, 0, 1, 0);
    r.type(" ");
    REQUIRE(r.until("OPENED?", ms(5000), from));
    check("STAMP   DAT", drive == 'M' ? t4 : t1, t2); // an access stamp only if the label asks
    r.type(" ");
    CHECK(r.until("STAMPTEST DONE", ms(5000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "WRITE"), std::string("00"));
    CHECK_EQ(field(o, "RESET"), std::string("00 00"));
    CHECK_EQ(field(o, "OPEN").substr(2), std::string(" 00"));
}

TEST(stamps_on_m_creation_update_and_access_byte_for_byte)
{
    Booted r;
    stamptest(r, 'M');
}

TEST(stamps_on_a_from_the_label_mode_0x31)
{
    Booted r;
    stamptest(r, 'A');
}

TEST(a_login_reads_the_directory_once_and_the_warm_boot_writes_back)
{
    Booted r;
    REQUIRE(r.ok);
    const auto lt = labels(SYS + "/logintest.lbl");
    int window = -1;
    unsigned reads[4] = {};
    r.watch = [&](uint16_t pc) {
        if (pc == lt.at("a1"))
            window = 0;
        else if (pc == lt.at("a2"))
            window = 1;
        else if (pc == lt.at("m1"))
            window = 2;
        else if (pc == lt.at("m3"))
            window = 3;
        else if (pc == lt.at("m2") || pc == lt.at("m4"))
            window = -1;
        else if (window >= 0 && pc == sym("READ"))
            ++reads[window];
    };
    const size_t from = start(r, "logintest.com");
    REQUIRE(r.until("ADDED?", ms(10000), from));
    // LATE.TXT in M:'s entry 1, behind the BDOS's back: user 0, no records, no blocks; and an
    // XFCB for it in entry 2 (user 16: a password's place, which 3b reads).
    CpmDisk m{r, 0};
    std::vector<uint8_t> s0 = m.sector(0);
    const std::string late = "LATE    TXT";
    std::fill(s0.begin() + 32, s0.begin() + 96, 0);
    std::copy(late.begin(), late.end(), s0.begin() + 33);
    std::copy(late.begin(), late.end(), s0.begin() + 65);
    s0[64] = 16;
    m.put(0, s0);
    r.type(" ");
    REQUIRE(r.until("RENAMED?", ms(5000), from));
    s0 = m.sector(0);
    CHECK_EQ(std::string(s0.begin() + 33, s0.begin() + 44), std::string("LATER   TXT"));
    CHECK_EQ(std::string(s0.begin() + 65, s0.begin() + 76), std::string("LATER   TXT")); // the XFCB too
    CHECK_EQ(s0[64], 16);
    r.type(" ");
    REQUIRE(r.until("XGONE?", ms(5000), from));
    s0 = m.sector(0);
    CHECK_EQ(s0[64], 0xE5); // f5': the XFCB only
    CHECK_EQ(s0[32], 0);
    r.type(" ");
    REQUIRE(r.until("UNCLOSED?", ms(5000), from));
    r.watch = nullptr;
    std::printf("  READs: A:'s login %u, again %u; M:'s %u, again after 37 %u\n", reads[0], reads[1], reads[2], reads[3]);
    CHECK_EQ(reads[0], 64u); // oodos-8m's directory: 64 sectors, once
    CHECK_EQ(reads[1], 0u);  // logged in: nothing
    CHECK_EQ(reads[2], 16u); // M:'s, 16
    CHECK_EQ(reads[3], 16u); // after 37, the first select reads it again
    std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "BEFORE"), std::string("FF 00")); // the directory as logged in
    CHECK_EQ(field(o, "RESET"), std::string("00 00"));
    CHECK_EQ(field(o, "AFTER"), std::string("01 00"));  // read again: there
    CHECK_EQ(field(o, "XREN"), std::string("01 00"));
    CHECK_EQ(field(o, "XDEL"), std::string("00 00"));
    CHECK_EQ(field(o, "DEL"), std::string("00 00"));
    CHECK(m.entries_of(m.directory(), 0, "LATER   TXT").empty());
    // W.DAT's 4 records are in a buffer, not on M:, until the warm boot writes them back.
    std::vector<uint8_t> four;
    for (unsigned n = 0; n < 4; ++n)
    {
        const std::vector<uint8_t> rec = pattern_record(n);
        four.insert(four.end(), rec.begin(), rec.end());
    }
    CHECK(!in_memory(r, MDISK, MDISK_SIZE, four));
    r.type(" ");
    CHECK(r.until("\r\nA>", ms(3000), from + o.size()));
    CHECK(in_memory(r, MDISK, MDISK_SIZE, four));
    // and frees its block, never closed (98): an FSTEST after sees all 60 free.
    const size_t mark = r.out.size();
    fstest(r, 'M', 16);
    CHECK_EQ(field(r.out.substr(mark), "FREE0"), std::string("0003C0"));
}

int main()
{
    // ctest stays hermetic with no skips (hw/oodos.md, "Testing"): CMake requires the tools
    // that build these, so their absence is a failure, not a skip.
    if (read_bytes(SYS + "/OODOS.SYS").empty() || read_bytes(EMU_BIOS_ROM).empty())
    {
        std::printf("no OODOS.SYS or bios.rom in the build tree (CMake builds them): failed\n");
        return 1;
    }
    return run_all();
}
