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
    constexpr uint32_t RES_PHYS = 62 * BLOCK;            ///< block 62's $C000
    constexpr uint8_t C_WRITE_FN = 2, C_STAT_FN = 11;    ///< BDOS functions, for timing
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
        /// Keys on the USB keyboard: down, a gap, up, a gap. Letters (Caps Lock is on: capitals),
        /// digits, space, CR, DEL, and the control characters $01-$1A as Ctrl and a letter.
        void type_usb(const std::string &text)
        {
            UsbCard *usb = machine->usb_card();
            for (const char ch : text)
            {
                uint8_t usage = 0;
                const bool ctrl = ch >= 0x01 && ch <= 0x1A && ch != '\r';
                if (ctrl)
                    usage = static_cast<uint8_t>(0x04 + ch - 1);
                else if (ch == 0x7F)
                    usage = UsbCard::DELETE;
                else if (ch >= 'A' && ch <= 'Z')
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
                if (ctrl)
                {
                    usb->key(UsbCard::LEFT_CTRL, true);
                    run(ms(20));
                }
                usb->key(usage, true);
                run(ms(20));
                usb->key(usage, false);
                run(ms(20));
                if (ctrl)
                {
                    usb->key(UsbCard::LEFT_CTRL, false);
                    run(ms(20));
                }
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

    /// How many times these bytes are in physical memory from `from`, `len` bytes.
    size_t count_in(const std::vector<uint8_t> &m, const std::vector<uint8_t> &what)
    {
        size_t n = 0;
        for (auto at = std::search(m.begin(), m.end(), what.begin(), what.end()); at != m.end();
             at = std::search(at + 1, m.end(), what.begin(), what.end()))
            ++n;
        return n;
    }
    size_t in_memory(Rig &r, uint32_t from, uint32_t len, const std::vector<uint8_t> &what)
    {
        std::vector<uint8_t> m(len);
        for (uint32_t i = 0; i < len; ++i)
            m[i] = r.phys(from + i);
        return count_in(m, what);
    }
    /// How many times these bytes are on the card.
    size_t on_card(Rig &r, const std::vector<uint8_t> &what) { return count_in(r.media->bytes(), what); }

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
    r.type("a\x01" "b\r");                                     // ^A: the cursor left (3b2, the Guide)
    CHECK(r.until("\nBA?\r\nA>", ms(1000), from));
    from = r.out.size();
    r.type("a\x19" "b\r");                                     // other control characters: in the line
    CHECK(r.until("a^Yb\r\nA\x19" "B?\r\nA>", ms(1000), from));
    from = r.out.size();
    r.type(std::string(130, 'z') + "\r");                   // 126 fit
    CHECK(r.until("\n" + std::string(126, 'Z') + "?\r\nA>", ms(3000), from));
    CHECK(r.out.find(std::string(127, 'Z'), from) == std::string::npos);
    // ^C at the start of a line: the warm boot.
    bool warm = false;
    r.watch = [&](uint16_t pc) { warm = warm || pc == sym("WARM"); };
    from = r.out.size();
    r.type("x\x03\x7F\x7F\x03");                           // after a character, a character: ^C
    CHECK(r.until("\r\nA>", ms(1000), from));
    CHECK(warm);
    CHECK(r.out.find("x^C\b\b  \b\b\b \b", from) != std::string::npos);
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
    CHECK(o.find("LINE?hello wo\a\a\a\r COUNT 08 hello wo\r\n") != std::string::npos); // a BEL each dropped
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
/// and the BIOS's READs while it reads the file back (deblocking: at most one a sector). `fresh`: no
/// FSTEST has run on the drive before, so record 299's pattern is nowhere on it until the close.
void fstest(Rig &r, char drive, unsigned free_records_per_block, bool big = false, bool fresh = true)
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
    // An entry written (DIRPUT) while a close or an extent change (CLOSEX) runs, and any changed
    // buffer of the drive's still not written back: the entry would be on the disk before the
    // data it points at.
    const uint16_t closex = sym("CLOSEX"), dirput = sym("DIRPUT"), bcb_drv = sym("BCB_DRV"),
                   bcb_dirty = sym("BCB_DIRTY"), cdrv = sym("CDRV");
    bool closing = false;
    unsigned entries = 0, early = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == closex)
            closing = true;
        else if (pc == dirput && closing)
        {
            ++entries;
            const uint8_t mine = static_cast<uint8_t>(r.machine->bus().peek(cdrv) + 1);
            for (uint16_t i = 0; i < 32; ++i)
                early += r.machine->bus().peek(static_cast<uint16_t>(bcb_dirty + i)) != 0 &&
                         r.machine->bus().peek(static_cast<uint16_t>(bcb_drv + i)) == mine;
        }
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
            closing = false;
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
    // The last sector is still in its buffer: record 299 isn't on the disk yet, until the close
    // writes it back (on a drive an earlier run left it on, deleted, there's nothing to see).
    const std::vector<uint8_t> last = pattern_record(299);
    const size_t before = drive == 'M' ? in_memory(r, MDISK, MDISK_SIZE, last) : on_card(r, last);
    r.type(" ");
    REQUIRE(r.until("CLOSED?", ms(5000), from));
    if (fresh)
    {
        CHECK_EQ(before, size_t{0});
        CHECK_EQ(drive == 'M' ? in_memory(r, MDISK, MDISK_SIZE, last) : on_card(r, last), size_t{1});
    }
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
    std::printf("  %c: entries written by a close or an extent change: %u, with the drive's data not yet written: %u\n",
                drive, entries, early);
    CHECK(entries > 0);
    CHECK_EQ(early, 0u); // the data first, then the entry that points at it
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

TEST(a_program_s_bios_calls_leave_the_bdos_its_drive)
{
    // BIOSTEST selects drives through function 50, M: last; the BDOS, which had A: selected in the
    // BIOS, must select it again before its next sector, or FSTEST's second run reads M:.
    Booted r;
    fstest(r, 'A', 32);
    r.set_time(2026, 10, 27, 15, 42, 7);
    CHECK(r.go("biostest.com", "", ms(8000)).find("BIOSTEST DONE") != std::string::npos);
    fstest(r, 'A', 32, false, false); // the first run's record 299 is on A:, deleted
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
    CHECK_EQ(field(o, "MAKE"), std::string("00 00"));    // entry 1, after the label: 0, the Guide's code
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
        {"SYSOPEN", "00 00"},          // now it is: from user 0,
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
        {"RENBITS", "C3 D4"},          // the new name, the old f1' and t3'
        {"RENEX", "FF 08"},            // File Exists
        {"RENWILD", "FF 09"},          // ? in Filename
        {"MAKEEX", "FF 08"},
        {"MAKEWILD", "FF 09"},
        {"OPENWILD", "FF 09"},
        {"HASHOPEN", "FF 00"},         // HASHBA.TXT's hash is HASHAG.TXT's: not taken for it
        {"HASHFIND", "FF 00"},
        {"HASHAG", "00 00"},
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
        {"ZEROFILL", "00 Y"},          // a block taken again is zeroed
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
        {"MAKEAGAIN", "00 00"},        // made again: 0, the Guide's code for a make
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
        CHECK_EQ(field(o, "SELB"), std::string("00 00"));
        CHECK_EQ(field(o, "ROVEC"), std::string("02 00")); // read-only from its login, before a write
        CHECK_EQ(field(o, "RO"), std::string("FF 02"));    // mode $FF: only returned
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

/// RULETEST's case `c` to its end, each prompt answered after its action ran.
std::string ruletest(Rig &r, char c, const std::vector<std::pair<std::string, std::function<void()>>> &prompts = {})
{
    const size_t from = start(r, "ruletest.com", std::string(" ") + c);
    for (const auto &[prompt, then] : prompts)
    {
        CHECK(r.until(prompt, ms(20000), from));
        then();
        r.type(" ");
    }
    CHECK(r.until("RULETEST DONE", ms(20000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    return r.out.substr(from);
}

TEST(the_bdos_follows_the_guide_where_the_review_found_it_did_not)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = ruletest(r, 'M');
    const std::pair<const char *, const char *> want[] = {
        {"MAKE", "00 00"},              // entry 1, but 0: the Guide's directory code (2.3)
        {"OPEN", "00 00"},
        {"CLOSE", "00 00"},
        {"SWRITE3", "01 00"},           // the extent can't be closed: two blocks for record 32
        {"SWCR", "80 EX 00"},           // and the FCB as it was, not at CR 0 of the same extent
        {"FPTR1", "00"},                // nothing merged into it from the entry
        {"SREAD3", "01 00"},
        {"SRCR", "80"},
        {"RWRITE3", "03 00"},
        {"RWCR", "2C"},                 // CR the record's (300 mod 128), as 33's
        {"XPTR0", "00"},                // nothing merged into the entry either, on the disk
        {"ATTR", "00 00"},
        {"BYTES", "55"},                // open with CR FF: the byte count (fn 15)
        {"F6CR", "03"},                 // f6' is 30's: open leaves CR
        {"OPENS2", "00 00 S2 80 RC 2D"}, // S2 1 taken as 0: extent 2, RC 45
        {"SRCHS2", "01 00"},            // entry 5
        {"MAKEATTR", "00 00"},
        {"FCBATTR", "41 54 80"},        // every attribute 0 (fn 22), S2 the BDOS's
        {"ATTRWRITE", "00 00"},         // t1' gone: not read-only
        {"ENTATTR", "41 54 58 00"},
        {"MAKEEXT", "FF 08"},           // the file is there, in another extent: File Exists
        {"THOLE", "FF 00"},             // R where the file has no data (fn 99)
        {"TNOBLK", "FF 00"},
        {"TPASTRC", "FF 00"},
        {"TNOEXT", "FF 00"},
        {"TDATA", "00 00"},
        {"TSIZE", "000029"},
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    if (o.find("RULETEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(delete_and_truncate_free_no_block_until_the_entry_is_written)
{
    // B: writable at its login, made read-only behind the BDOS's back: the directory's write
    // fails (Read Only), and the blocks stay in use, as the entry on the disk still has them.
    Rig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    REQUIRE(r.boot());
    const std::string o = ruletest(r, 'D', {{"REMOUNT?", [&] { CHECK_EQ(r.mount(2, "/OODOS/B.IMG", true), 0); }},
                                            {"WRITABLE?", [&] { CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0); }},
                                            {"REMOUNT2?", [&] { CHECK_EQ(r.mount(2, "/OODOS/B.IMG", true), 0); }}});
    CHECK_EQ(field(o, "DELRO"), std::string("FF 02"));
    CHECK_EQ(field(o, "DFREE1"), field(o, "DFREE0"));
    CHECK_EQ(field(o, "TRRO"), std::string("FF 02"));
    CHECK_EQ(field(o, "TFREE1"), field(o, "TFREE0"));
    CHECK(hexval(field(o, "DFREE0")) > 0);
}

TEST(a_rescan_of_the_units_logs_out_only_the_drives_that_changed)
{
    // A failed IDENTIFY (the card busy as A: logs in) leaves B:'s drive table, so its data is
    // written after; another image under B: logs B: out at the next rescan; a rescan with
    // nothing changed leaves B: logged in.
    Rig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    REQUIRE(r.boot());
    bool armed = false, stuck = false;
    r.watch = [&](uint16_t pc) {
        if (armed && !stuck && pc == sym("LOGIN"))
        {
            stuck = true;
            r.sd().write(SdCard::CTRL, 0);
            r.sd().write(SdCard::UNIT, 0);
            r.sd().write(SdCard::COUNT, 1);
            r.sd().write(SdCard::CMD, SdCard::READ);
        }
    };
    std::vector<uint8_t> swapped;
    const std::string o = ruletest(r, 'C', {{"BUSY?", [&] { armed = true; }},
                                            {"READY?", [&] { r.run(ms(SdCard::INIT_MS + 10)); }},
                                            {"SWAP?", [&] {
                                                 CHECK_EQ(r.mount(2, "/OODOS/A.IMG"), 0);
                                                 swapped = r.media->bytes();
                                             }}});
    r.watch = nullptr;
    CHECK(stuck);
    CHECK_EQ(field(o, "SELA"), std::string("FF 04")); // no units table: A: isn't a drive this time
    CHECK_EQ(field(o, "CLOSEB"), std::string("00 00")); // B: as it was: its data written
    CHECK_EQ(field(o, "SELA2"), std::string("00 00"));
    CHECK_EQ(field(o, "SELA3"), std::string("00 00"));
    CHECK_EQ(field(o, "LOGV"), std::string("01 00"));   // B: changed: logged out
    CHECK(field(o, "NEWB").substr(0, 2) != "FF");       // and read again: A.IMG's README.TXT
    CHECK_EQ(field(o, "SELA4"), std::string("00 00"));
    CHECK_EQ(field(o, "LOGV2"), std::string("03 00"));  // nothing changed: B: kept
    // CHG1.TXT's record is on B.IMG, written at its close.
    CpmDisk b{r, 2};
    CHECK_EQ(b.entries_of(b.directory(), 0, "CHG1    TXT").size(), size_t{0}); // unit 2 is A.IMG now
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    const std::vector<uint8_t> dir = b.directory();
    const std::vector<unsigned> es = b.entries_of(dir, 0, "CHG1    TXT");
    REQUIRE(es.size() == 1);
    CHECK_EQ(dir[es[0] * 32 + 15], 1); // RC 1
    // CHG2.TXT's record was in a buffer at the swap: dropped, not written to the new image, nor
    // anywhere else on the card; B.IMG has its entry (made before the swap) and no record.
    CHECK(r.media->bytes() == swapped);
    CHECK_EQ(on_card(r, pattern_record(0xA5C3)), size_t{0});
    const std::vector<unsigned> es2 = b.entries_of(dir, 0, "CHG2    TXT");
    REQUIRE(es2.size() == 1);
    CHECK_EQ(dir[es2[0] * 32 + 15], 0); // RC 0
}

/// A card whose writes fail while `fail` is set, as a card that has gone bad would; and, while
/// `bad` is set, each write of a sector that starts with those bytes (one bad sector's).
struct FaultyMedia : MemoryMedia
{
    using MemoryMedia::MemoryMedia;
    bool fail{false};
    std::vector<uint8_t> bad;
    bool write(uint64_t lba, const uint8_t *in) override
    {
        if (fail || (!bad.empty() && std::equal(bad.begin(), bad.end(), in)))
            return false;
        return MemoryMedia::write(lba, in);
    }
};

/// The rig's card made a FaultyMedia, units 1 and 2 A.IMG and B.IMG, OODOS booted.
std::shared_ptr<FaultyMedia> faulty(Rig &r)
{
    auto fm = std::make_shared<FaultyMedia>(r.media->bytes(), "card");
    r.media = fm;
    r.sd().insert(fm, false);
    r.run(ms(SdCard::INIT_MS + 10));
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    CHECK(r.boot());
    return fm;
}

/// A rig whose units 1 and 2 are A.IMG and B.IMG, B: (unit 2) swapped for A.IMG at RULETEST's
/// SWAP? (case `c`); the card's bytes then, and the output.
std::pair<std::string, std::vector<uint8_t>> swap_after_write(Rig &r, char c)
{
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    std::vector<uint8_t> swapped;
    if (!r.boot())
    {
        CHECK(false);
        return {};
    }
    const std::string o = ruletest(r, c, {{"SWAP?", [&] {
                                               CHECK_EQ(r.mount(2, "/OODOS/A.IMG"), 0);
                                               swapped = r.media->bytes();
                                           }}});
    return {o, swapped};
}

TEST(a_reset_after_a_swap_drops_the_old_images_buffers)
{
    // A record of B:'s in a buffer, unit 2's image swapped, then 37 of B:: the reset reads the
    // units first, so the buffer is dropped, not written into the new image (the Guide's
    // change-the-disk-then-reset). The card is as it was at the swap, after the warm boot too.
    Rig r;
    REQUIRE(r.ok);
    const auto [o, swapped] = swap_after_write(r, 'R');
    CHECK_EQ(field(o, "RESETB"), std::string("00 00"));
    CHECK(r.media->bytes() == swapped);
    CHECK_EQ(on_card(r, pattern_record(0xA5C3)), size_t{0});
}

TEST(the_warm_boots_flush_drops_a_swapped_drives_buffers)
{
    // The same, but a program's own SELDSK (a first select) reads the units, and the warm boot's
    // 48 comes next: it drops B:'s buffer (CHANGED) before it writes anything back.
    Rig r;
    REQUIRE(r.ok);
    const auto [o, swapped] = swap_after_write(r, 'W');
    CHECK(field(o, "SELDSK") != "00 00"); // B: is a drive still: A.IMG
    CHECK(r.media->bytes() == swapped);
    CHECK_EQ(on_card(r, pattern_record(0xA5C3)), size_t{0});
}

TEST(a_failed_write_back_keeps_the_buffer_so_the_close_fails_again)
{
    // FAULT.DAT's record 3 in a changed buffer, the card's writes failing: each close fails
    // (Disk I/O). The buffer is kept, so once the card writes again the close writes record 3
    // before the entry, and the records read back (a dropped buffer: the entry put in over a
    // sector never written, record 3 not there). Meanwhile BIG.DAT's 40 sectors still come and
    // go through the other 31 buffers. Then 37 with the card failing drops what it can't write,
    // says so, and logs B: out all the same: the next 37 has nothing to write.
    Rig r;
    REQUIRE(r.ok);
    auto fm = std::make_shared<FaultyMedia>(r.media->bytes(), "card");
    r.media = fm;
    r.sd().insert(fm, false);
    r.run(ms(SdCard::INIT_MS + 10));
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    REQUIRE(r.boot());
    const std::string o = ruletest(r, 'F', {{"FAULT?", [&] { fm->fail = true; }},
                                            {"FIXED?", [&] { fm->fail = false; }},
                                            {"FAULT2?", [&] { fm->fail = true; }},
                                            {"FIXED2?", [&] { fm->fail = false; }}});
    CHECK_EQ(field(o, "FWRITE"), std::string("00"));
    CHECK_EQ(field(o, "FCLOSE1"), std::string("FF 01"));
    CHECK_EQ(field(o, "FCLOSE2"), std::string("FF 01")); // the buffer kept: still not written
    CHECK_EQ(field(o, "FBIG"), std::string("00"));       // and the other 31 still in use
    CHECK_EQ(field(o, "FCLOSE3"), std::string("00 00"));
    CHECK_EQ(field(o, "FREAD"), std::string("YYYY"));
    CHECK_EQ(field(o, "FRESET"), std::string("FF 01"));
    CHECK_EQ(field(o, "FRESET2"), std::string("00 00"));
    CpmDisk b{r, 2};
    const std::vector<uint8_t> dir = b.directory();
    const std::vector<unsigned> es = b.entries_of(dir, 0, "FAULT   DAT");
    REQUIRE(es.size() == 1);
    CHECK_EQ(dir[es[0] * 32 + 15], 4); // RC 4
    for (unsigned n = 0; n < 4; ++n)
        CHECK(b.record(dir, es, n) == pattern_record(n));
    CHECK(b.record(dir, es, 4) == std::vector<uint8_t>(RECORD, 0)); // dropped by the 37
}

TEST(a_buffer_that_wont_write_fails_only_the_close_that_needs_it)
{
    // BAD.DAT's record won't write (one bad sector): its close fails, and again, kept marked. A
    // close that puts nothing of it in the directory doesn't fail on it: RD.DAT's, opened and
    // only read, and OK.DAT's, written and its data on the card. After the warm boot, 98 has
    // dropped the marked buffer (BAD.DAT wasn't closed, and its blocks are free): B:'s closes
    // work, and once the card writes, the next warm boot's 48 has nothing of BAD.DAT's to write.
    Rig r;
    REQUIRE(r.ok);
    auto fm = faulty(r);
    const std::vector<uint8_t> badrec = pattern_record(0xA5C3);
    const std::string o = ruletest(r, 'S', {{"FAULT?", [&] { fm->bad = badrec; }}});
    CHECK_EQ(field(o, "SFLUSH"), std::string("00 00"));
    CHECK_EQ(field(o, "SCLOSE1"), std::string("FF 01"));
    CHECK_EQ(field(o, "SRDCLOSE"), std::string("00 00"));
    CHECK_EQ(field(o, "SOKCLOSE"), std::string("00 00"));
    CHECK_EQ(field(o, "SCLOSE2"), std::string("FF 01")); // still not written: still fails
    CHECK(o.find("OODOS Error On B: Disk I/O\r\nBDOS Function = 48") != std::string::npos); // the warm boot's
    const std::string t = ruletest(r, 'T');
    CHECK_EQ(field(t, "TRDCLOSE"), std::string("00 00"));
    CHECK_EQ(field(t, "TWCLOSE"), std::string("00 00"));
    fm->bad.clear();
    r.go("hello.com"); // a program's end: the warm boot's 48 with the card writing
    CHECK_EQ(on_card(r, badrec), size_t{0});
    CpmDisk b{r, 2};
    const std::vector<uint8_t> dir = b.directory();
    const std::vector<unsigned> ok = b.entries_of(dir, 0, "OK      DAT");
    REQUIRE(ok.size() == 1);
    CHECK(b.record(dir, ok, 0) == pattern_record(0x1202));
    const std::vector<unsigned> rd = b.entries_of(dir, 0, "RD      DAT");
    REQUIRE(rd.size() == 1);
    CHECK_EQ(dir[rd[0] * 32 + 15], 2); // RC 2
    CHECK(b.record(dir, rd, 1) == pattern_record(0x1102));
    CHECK(b.entries_of(dir, 0, "BAD     DAT").size() == 1); // made, never closed: RC 0
}

TEST(the_warm_boot_keeps_a_closed_files_record_that_wont_write)
{
    // PERM.DAT closed, so its block is the directory's; its record written again in place and the
    // program ends without a close, the card failing that record: the warm boot's 48 says Disk
    // I/O on B:, and 98 keeps the buffer (the block isn't one it frees), so once the card writes
    // the next warm boot's 48 puts the record on the card, where PERM.DAT's entry points.
    Rig r;
    REQUIRE(r.ok);
    auto fm = faulty(r);
    const std::vector<uint8_t> badrec = pattern_record(0xA5C3);
    const std::string o = ruletest(r, 'P', {{"FAULT?", [&] { fm->bad = badrec; }}});
    const size_t done = o.find("RULETEST DONE");
    REQUIRE(done != std::string::npos);
    CHECK(o.find("OODOS Error On B: Disk I/O\r\nBDOS Function = 48", done) != std::string::npos);
    CHECK_EQ(on_card(r, badrec), size_t{0});
    fm->bad.clear();
    r.go("hello.com"); // a program's end: the warm boot's 48 with the card writing
    CHECK_EQ(on_card(r, badrec), size_t{1});
    CpmDisk b{r, 2};
    const std::vector<uint8_t> dir = b.directory();
    const std::vector<unsigned> es = b.entries_of(dir, 0, "PERM    DAT");
    REQUIRE(es.size() == 1);
    CHECK_EQ(dir[es[0] * 32 + 15], 1); // RC 1
    CHECK(b.record(dir, es, 0) == badrec);
}

TEST(a_ctrl_c_at_the_prompt_resets_the_disk_system)
{
    // Case P's kept buffer, the card still failing its record: ^C at A> is function 13 (the
    // Guide, 1.6.2), which drops the buffer and says so, then the prompt. The next warm boot's 48
    // has nothing to write, and once the card writes the record never reaches it.
    Rig r;
    REQUIRE(r.ok);
    auto fm = faulty(r);
    const std::vector<uint8_t> badrec = pattern_record(0xA5C3);
    ruletest(r, 'P', {{"FAULT?", [&] { fm->bad = badrec; }}});
    unsigned resets = 0;
    r.watch = [&](uint16_t pc) { resets += pc == sym("F_RESET"); };
    size_t from = r.out.size();
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK_EQ(resets, 1u);
    std::string o = r.out.substr(from);
    CHECK(o.find("OODOS Error On B: Disk I/O\r\nBDOS Function = 13\r\n") != std::string::npos);
    CHECK(o.find("Function = 48") == std::string::npos);
    o = r.go("hello.com"); // the card still failing: nothing to write
    CHECK(o.find("Disk I/O") == std::string::npos);
    fm->bad.clear();
    r.go("hello.com");
    CHECK_EQ(on_card(r, badrec), size_t{0});
    CHECK_EQ(resets, 1u); // a program's end is the warm boot, not 13
}

TEST(a_ctrl_c_at_the_prompt_with_nothing_wrong_gives_a_fresh_prompt)
{
    // ^C as the prompt's first key: 13 with nothing to say, the CCP's drive current again, a
    // fresh prompt. ^C after a character is a character of the line (3b2, the Guide), and ^C in
    // a program's own line input is the warm boot, as it was: no 13.
    Booted r;
    REQUIRE(r.ok);
    unsigned resets = 0, warms = 0;
    r.watch = [&](uint16_t pc) {
        resets += pc == sym("F_RESET");
        warms += pc == sym("WARM");
    };
    size_t from = r.out.size();
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK_EQ(resets, 1u);
    CHECK_EQ(r.out.substr(from), std::string("\r\nA>"));
    const uint32_t ccpdrv = RES_PHYS + sym("CCPDRV") - 0xC000;
    r.poke(ccpdrv, std::vector<uint8_t>{1}); // the CCP's drive B: (3b2: the CCP keeps its own)
    from = r.out.size();
    r.type("\x03");
    CHECK(r.until("\r\nB>", ms(3000), from)); // still B:, though 13 makes A: current
    CHECK_EQ(resets, 2u);
    r.poke(ccpdrv, std::vector<uint8_t>{0});
    r.poke(SYS_BLOCK_PHYS + 0x023E, std::vector<uint8_t>{0}); // and current now (SCB_DISK)
    from = r.out.size();
    r.type("x\x03\r"); // a ^C after a character is a character (3b2, the Guide)
    CHECK(r.until("\nX\x03?\r\nA>", ms(3000), from));
    CHECK_EQ(resets, 2u);
    r.poke_com(read_bytes(SYS + "/linetest.com"));
    from = r.out.size();
    r.type("GO\r");
    REQUIRE(r.until("KEY?", ms(2000), from));
    r.type("k");
    REQUIRE(r.until("RAW?", ms(2000), from));
    r.type("r");
    REQUIRE(r.until("WAIT ", ms(2000), from));
    r.run(ms(20));
    r.type("s");
    REQUIRE(r.until("LINE?", ms(2000), from));
    warms = 0;
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK_EQ(warms, 1u);
    CHECK_EQ(resets, 2u);
}

TEST(a_ctrl_c_at_the_prompt_makes_a_read_only_drive_writable)
{
    // A drive left read-only, as a Read Only error or 28 leaves it (its bit in ROVEC poked: an
    // error's from a unit made read-only doesn't last, as the warm boot's rescan finds the unit
    // changed and logs the drive out): the warm boot keeps it, and ^C at the prompt, 13, makes
    // every drive read-write again.
    Rig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/OODOS/A.IMG"), 0);
    CHECK_EQ(r.mount(2, "/OODOS/B.IMG"), 0);
    REQUIRE(r.boot());
    const uint32_t rovec = SYS_BLOCK_PHYS + sym("ROVEC");
    r.poke(rovec, std::vector<uint8_t>{0x03, 0x00}); // A: and B:
    r.go("hello.com");
    CHECK_EQ(r.phys(rovec), 0x03); // kept across the warm boot
    const size_t from = r.out.size();
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK_EQ(r.phys(rovec), 0x00);
    CHECK_EQ(r.phys(rovec + 1), 0x00);
}

TEST(a_flush_writes_every_buffer_it_can_then_says_what_failed)
{
    // Two changed buffers, G1.DAT's (first in 48's order) failing: 48 writes G2.DAT's all the
    // same and returns Disk I/O; 48 with $FF drops every buffer, the failed one too, so once the
    // card writes nothing of G1.DAT's reaches it.
    Rig r;
    REQUIRE(r.ok);
    auto fm = faulty(r);
    const std::vector<uint8_t> badrec = pattern_record(0xA5C3);
    size_t g2 = 0;
    const std::string o = ruletest(r, 'G', {{"FAULT?", [&] { fm->bad = badrec; }},
                                            {"FLUSHED?", [&] { g2 = on_card(r, pattern_record(0x1303)); }},
                                            {"GOOD?", [&] { fm->bad.clear(); }}});
    CHECK_EQ(field(o, "GFLUSH"), std::string("FF 01"));
    CHECK_EQ(g2, size_t{1});
    CHECK_EQ(field(o, "GPURGE"), std::string("FF 01"));
    CHECK_EQ(field(o, "GFLUSH2"), std::string("00 00"));
    CHECK_EQ(on_card(r, badrec), size_t{0});
}

TEST(the_warm_boot_reads_the_units_so_a_swap_drops_the_buffers)
{
    // A record of B:'s in a buffer, unit 2's image swapped, and the program ends with no rescan
    // of its own: the warm boot has the BIOS read the units before its 48, so the buffer is
    // dropped, not written into the new image. The card is as it was at the swap.
    Rig r;
    REQUIRE(r.ok);
    const auto [o, swapped] = swap_after_write(r, 'Z');
    CHECK(r.media->bytes() == swapped);
    CHECK_EQ(on_card(r, pattern_record(0xA5C3)), size_t{0});
}

TEST(the_bdos_bugs_the_model_comparison_found_each_a_directed_test)
{
    // Each of these the comparison with the model found (hw/oodos.md, "What the comparison
    // found"), on M:: 35 of no file zeroes R0-R2; stamps only in the extent-zero entry's slot
    // (ST.DAT's second entry, extent 2, has none, though M:'s label asks for creation and update
    // stamps); a write into an extent a read found no entry for makes the entry at once, so a
    // full directory is the write's own error (1 for 21, 5 for 34 and 40).
    Booted r;
    REQUIRE(r.ok);
    const std::string o = ruletest(r, 'X');
    CHECK_EQ(field(o, "SIZENONE"), std::string("FF 00"));
    CHECK_EQ(field(o, "SIZER"), std::string("000000"));
    CHECK_EQ(field(o, "DIRFULL"), std::string("FF"));
    CHECK_EQ(field(o, "SEQEND"), std::string("01"));
    CHECK_EQ(field(o, "SEQW"), std::string("01"));
    CHECK_EQ(field(o, "RREAD"), std::string("04 00"));
    CHECK_EQ(field(o, "RWRITE"), std::string("05 00"));
    CHECK_EQ(field(o, "RREADZ"), std::string("04 00"));
    CHECK_EQ(field(o, "RWRITEZ"), std::string("05 00"));
    CpmDisk m{r, 0};
    const std::vector<uint8_t> dir = m.directory();
    const std::vector<unsigned> es = m.entries_of(dir, 0, "ST      DAT");
    REQUIRE(es.size() == 2);
    auto slot = [&](unsigned i) {
        const unsigned at = (i | 3) * 32 + 1 + (i & 3) * 10;
        return std::vector<uint8_t>(dir.begin() + at, dir.begin() + at + 10);
    };
    const std::vector<uint8_t> first = slot(es[0]), second = slot(es[1]);
    CHECK_EQ(dir[es[1] * 32 + 12], 2);                                   // extent 2
    CHECK(std::vector<uint8_t>(first.begin(), first.begin() + 4) != std::vector<uint8_t>(4, 0)); // created
    CHECK(std::vector<uint8_t>(first.begin() + 4, first.begin() + 8) != std::vector<uint8_t>(4, 0)); // updated
    CHECK(second == std::vector<uint8_t>(10, 0));
}

TEST(exms_mask_is_each_drives_own)
{
    // A: oodos-32m (EXM 3: four extents an entry), logged in, then M: (EXM 1): 384 records on A:
    // are one entry. With M:'s mask a write into extent 2 found no entry and made a second.
    std::vector<uint8_t> card = fat16_card({{"BIG.IMG", 65536u * 512}});
    auto word = [&](size_t at) { return size_t{card[at]} | size_t{card[at + 1]} << 8; };
    const size_t root = (word(14) + card[16] * word(22)) * SECTOR;
    const size_t data = root + word(17) * 32; // BIG.IMG is the first file
    std::fill(card.begin() + static_cast<long>(data), card.begin() + static_cast<long>(data + 2048 * 32), 0xE5);
    Rig r(card);
    REQUIRE(r.ok);
    CHECK_EQ(r.mount(1, "/BIG.IMG"), 0);
    REQUIRE(r.boot());
    const std::string o = ruletest(r, 'E');
    CHECK_EQ(field(o, "EXMENTS"), std::string("01"));
}

TEST(the_ccp_keeps_its_own_user_number_and_puts_it_back_after_a_load)
{
    // 3b2: a program's 32 lasts only as long as it runs (the Guide, 2.3.5): the CCP's user, 0,
    // comes back at the warm boot. 47 with $FF makes the program's user the CCP's (systest's
    // chain test has more); the CCP still loads NAME.COM from user 0 and puts its user back.
    Booted r;
    REQUIRE(r.ok);
    CHECK(r.go("ruletest.com", " U").find("USER SET") != std::string::npos);
    CHECK(r.go("ruletest.com", " V").find("USER 00") != std::string::npos);
    size_t from = start(r, "systest.com", " U"); // user 5 and M:, then 47 of HELLO with $FF
    CHECK(r.until("\r\nM>HELLO\r\nHello from OODOS", ms(4000), from));
    CHECK(r.until("\r\nM>", ms(3000), r.out.find("Hello from OODOS", from)));
    from = start(r, "ruletest.com", " V");
    CHECK(r.until("USER 05\r\n\r\nM>", ms(4000), from));
    from = r.out.size();
    r.type("NOSUCH\r");
    CHECK(r.until("NOSUCH?\r\nM>", ms(4000), from));
    from = start(r, "ruletest.com", " V");
    CHECK(r.until("USER 05\r\n\r\nM>", ms(4000), from));
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
    check("STAMP   DAT", t1, t1); // made (extent 0): its creation and update stamps
    r.set_time(2026, 11, 4, 18, 30, 0);
    r.type(" ");
    REQUIRE(r.until("CLOSED?", ms(5000), from));
    check("STAMP   DAT", t1, t2); // written and closed: the update
    r.set_time(2026, 12, 31, 23, 59, 0);
    r.type(" ");
    REQUIRE(r.until("MODE?", ms(5000), from));
    check("EMPTY   DAT", t3, t3); // closed unwritten: make's stamps, no more
    if (drive == 'M')
        r.poke(MDISK + 12, 0x61); // the label: access and update stamps
    r.set_time(2027, 1, 1, 0, 1, 0);
    r.type(" ");
    REQUIRE(r.until("OPENED?", ms(5000), from));
    check("STAMP   DAT", drive == 'M' ? t4 : t1, t2); // an access stamp only if the label asks
    r.set_time(2027, 2, 2, 12, 0, 0);
    r.type(" ");
    REQUIRE(r.until("UNSTAMPED?", ms(5000), from));
    check("STAMP   DAT", drive == 'M' ? t4 : t1, t2); // opened at extent 1, then on a read-only drive: none
    r.type(" ");
    CHECK(r.until("STAMPTEST DONE", ms(5000), from));
    const std::string o = r.out.substr(from);
    CHECK_EQ(field(o, "WRITE"), std::string("00"));
    CHECK_EQ(field(o, "RESET"), std::string("00 00"));
    CHECK_EQ(field(o, "OPEN").substr(2), std::string(" 00"));
    CHECK_EQ(field(o, "OPEN1").substr(2), std::string(" 00"));   // found: extent 0's entry holds extent 1
    CHECK_EQ(field(o, "OPENRO").substr(2), std::string(" 00"));
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
    CHECK_EQ(field(o, "AFTER"), std::string("00 00"));  // read again: there
    CHECK_EQ(field(o, "XREN"), std::string("00 00"));
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
    CHECK_EQ(in_memory(r, MDISK, MDISK_SIZE, four), size_t{0});
    r.type(" ");
    CHECK(r.until("\r\nA>", ms(3000), from + o.size()));
    CHECK_EQ(in_memory(r, MDISK, MDISK_SIZE, four), size_t{1});
    // and frees its block, never closed (98): an FSTEST after sees all 60 free.
    const size_t mark = r.out.size();
    fstest(r, 'M', 16);
    CHECK_EQ(field(r.out.substr(mark), "FREE0"), std::string("0003C0"));
}

// ---- phase 3b1: the file extras ----------------------------------------------------------

/// XTRATEST's case `c` to its end, each prompt answered after its action ran.
std::string xtratest(Rig &r, char c, const std::vector<std::pair<std::string, std::function<void()>>> &prompts = {})
{
    const size_t from = start(r, "xtratest.com", std::string(" ") + c);
    for (const auto &[prompt, then] : prompts)
    {
        CHECK(r.until(prompt, ms(20000), from));
        then();
        r.type(" ");
    }
    CHECK(r.until("XTRATEST DONE", ms(60000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    return r.out.substr(from);
}

/// Bytes 13-23 of an entry holding `pw` as CP/M 3 keeps it (and cpmtools reads it): the key, the
/// 8 bytes' sum, at 13; 14 and 15 as `s2rc`; the bytes in reverse order, each XORed with the key.
std::vector<uint8_t> kept_pw(const std::string &pw, uint8_t s2 = 0, uint8_t rc = 0)
{
    uint8_t key = 0;
    for (char c : pw)
        key = static_cast<uint8_t>(key + static_cast<uint8_t>(c));
    std::vector<uint8_t> out = {key, s2, rc};
    for (unsigned j = 0; j < 8; ++j)
        out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(pw[7 - j]) ^ key));
    return out;
}

/// An XFCB (user u + 16) of a name in a directory: its entry's number, or -1.
int xfcb_of(const std::vector<uint8_t> &dir, unsigned u, const std::string &name83)
{
    for (unsigned i = 0; i * 32 < dir.size(); ++i)
        if (dir[i * 32] == 16 + u && std::equal(name83.begin(), name83.end(), dir.begin() + i * 32 + 1))
            return static_cast<int>(i);
    return -1;
}

/// The password mode byte of entry i's SFCB slot.
uint8_t slot_mode(const std::vector<uint8_t> &dir, unsigned i) { return dir[(i | 3) * 32 + 1 + (i & 3) * 10 + 8]; }

/// What an XFCB of u's `name83` should be: the mode, the password kept, the rest 0.
void check_xfcb(const std::vector<uint8_t> &dir, unsigned u, const std::string &name83, uint8_t mode, const std::string &pw)
{
    const int x = xfcb_of(dir, u, name83);
    REQUIRE(x >= 0);
    std::vector<uint8_t> want = {static_cast<uint8_t>(16 + u)};
    want.insert(want.end(), name83.begin(), name83.end());
    want.push_back(mode);
    const std::vector<uint8_t> k = kept_pw(pw);
    want.insert(want.end(), k.begin(), k.end());
    want.resize(32, 0);
    const std::vector<uint8_t> got(dir.begin() + x * 32, dir.begin() + x * 32 + 32);
    CHECK(got == want);
    if (got != want)
        for (unsigned b = 0; b < 32; ++b)
            std::printf("  XFCB byte %u: %02X, want %02X\n", b, got[b], want[b]);
}

TEST(labels_xfcbs_and_passwords_as_the_programmers_guide_has_them)
{
    // On M:: the label's passwords and stamps (100, 101); an XFCB in read mode, then write mode
    // (103), read back (102); open, write, delete, rename, 30 and truncate of a protected file
    // without its password, and with it from the DMA buffer or the default (106); make with f6';
    // delete with f5'; File R/O before Password Error; and the bytes on the disk, as cpmtools
    // reads them (the key, the password reversed and XORed).
    Booted r;
    REQUIRE(r.ok);
    CpmDisk m{r, 0};
    const std::vector<uint8_t> label0 = m.sector(0);
    r.set_time(2026, 11, 5, 10, 30, 0);
    const auto now = dat(2026, 11, 5, 10, 30);
    std::vector<uint8_t> dir;
    const std::string o = xtratest(r, 'L',
                                   {{"XFCB?", [&] { dir = m.directory(); }},
                                    {"XGONE?", [&] {
                                         const std::vector<uint8_t> d = m.directory();
                                         CHECK(xfcb_of(d, 0, "P       DAT") < 0);
                                         const std::vector<unsigned> es = m.entries_of(d, 0, "P       DAT");
                                         REQUIRE(!es.empty()); // f5': the file is still there
                                         CHECK_EQ(slot_mode(d, es[0]), 0);
                                     }},
                                    {"QXFCB?", [&] {
                                         const std::vector<uint8_t> d = m.directory();
                                         check_xfcb(d, 0, "Q       DAT", 0x20, "QPASS   ");
                                         const std::vector<unsigned> es = m.entries_of(d, 0, "Q       DAT");
                                         REQUIRE(!es.empty());
                                         CHECK_EQ(slot_mode(d, es[0]), 0x20);
                                     }},
                                    {"QGONE?", [&] {
                                         const std::vector<uint8_t> d = m.directory();
                                         CHECK(xfcb_of(d, 0, "Q       DAT") < 0);
                                         CHECK(m.entries_of(d, 0, "Q       DAT").empty());
                                     }}});
    const std::pair<const char *, const char *> want[] = {
        {"GETL", "31 00"},  {"XOFF", "FF 00"},   {"LSET", "00 00"},    {"GETL2", "B1 00"},   {"LNOPW", "FF 07"},
        {"LPW", "00 00"},   {"XSET", "00 00"},   {"STAMPS", "00 00"},  {"STMODE", "80"},     {"OPNOPW", "FF 07"},
        {"OPPW", "00 00"},  {"OPF7", "20"},      {"XWRITE", "00 00"},  {"OPW", "00 00"},     {"OPWF7", "A0"},
        {"WRF7", "FF 03"},  {"RDF7", "00 00"},   {"DELNOPW", "FF 07"}, {"RENNOPW", "FF 07"}, {"ATTNOPW", "FF 07"},
        {"TRNOPW", "FF 07"}, {"DEFPW", "00 00"}, {"XDEL5", "00 00"},   {"STAMPS2", "00 00"}, {"STMODE2", "00"},
        {"MAKEQ", "00 00"}, {"CLOSEQ", "00 00"}, {"OPQ", "00 00"},     {"OPQF7", "20"},      {"WRQ", "00 00"},
        {"ATTQ", "00 00"},  {"DELQRO", "FF 03"}, {"ATTQ2", "00 00"},   {"DELQNO", "FF 07"},  {"DELQ", "00 00"},
        {"LOFF", "00 00"},  {"GETL3", "31 00"},  {"XOFF2", "FF 00"},   {"LON", "00 00"},     {"MAKES", "00 00"},
        {"OPSDEF", "00 00"},
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    // In error mode $FE the error is said as CP/M 3 says it, then returned.
    CHECK(o.find("OPSNO \r\nOODOS Error On M: Password Error\r\nBDOS Function = 15 File = S       .DAT\r\nFF 07") !=
          std::string::npos);
    // At XFCB?: P.DAT's XFCB in write mode, SECRET kept from the read mode's 103; its slot's mode
    // byte the same; 102 gave its stamps (read when the mode was read's, 80).
    REQUIRE(!dir.empty());
    check_xfcb(dir, 0, "P       DAT", 0x40, "SECRET  ");
    REQUIRE(!m.entries_of(dir, 0, "P       DAT").empty());
    const unsigned p = m.entries_of(dir, 0, "P       DAT")[0];
    CHECK_EQ(slot_mode(dir, p), 0x40);
    char stamps[17];
    const uint8_t *sl = &dir[(p | 3) * 32 + 1 + (p & 3) * 10];
    std::snprintf(stamps, sizeof stamps, "%02X%02X%02X%02X%02X%02X%02X%02X", sl[0], sl[1], sl[2], sl[3], sl[4], sl[5],
                  sl[6], sl[7]);
    CHECK_EQ(field(o, "STAMPV"), std::string(stamps));
    // The label, last set by LON ($B1, its password LABELPW kept): the name, the byte, the key and
    // the password, its creation stamp the cold boot's, its update now.
    const std::vector<uint8_t> l = m.sector(0);
    std::vector<uint8_t> wantl = {0x20};
    const std::string name = "MYDISK  LBL";
    wantl.insert(wantl.end(), name.begin(), name.end());
    wantl.push_back(0xB1);
    const std::vector<uint8_t> k = kept_pw("LABELPW ", label0[14], label0[15]);
    wantl.insert(wantl.end(), k.begin(), k.end());
    wantl.insert(wantl.end(), label0.begin() + 24, label0.begin() + 28);
    wantl.insert(wantl.end(), now.begin(), now.end());
    CHECK(std::vector<uint8_t>(l.begin(), l.begin() + 32) == wantl);
    for (unsigned i = 0; i < 32; ++i)
        if (l[i] != wantl[i])
            std::printf("  label byte %u: %02X, want %02X\n", i, l[i], wantl[i]);
    if (o.find("XTRATEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(a_label_asking_for_stamps_needs_sfcbs)
{
    // M:'s SFCBs and label taken away behind the BDOS's back, M: reset: 101 is 0; 100 asking for
    // stamps is $FF, without them it makes a label in the first free entry (its password none, 8
    // blanks; its creation and update stamps now); 102 gives zeros for the stamps.
    Booted r;
    REQUIRE(r.ok);
    CpmDisk m{r, 0};
    r.set_time(2026, 11, 6, 8, 5, 0);
    const auto now = dat(2026, 11, 6, 8, 5);
    const std::string o = xtratest(r, 'N', {{"NOSFCB?", [&] {
                                                 for (uint32_t n = 0; n < 16; ++n)
                                                 {
                                                     std::vector<uint8_t> s = m.sector(n);
                                                     for (unsigned e = 0; e < 16; ++e)
                                                         if (s[e * 32] == 0x21 || s[e * 32] == 0x20)
                                                             std::fill(s.begin() + e * 32, s.begin() + e * 32 + 32, 0xE5);
                                                     m.put(n, s);
                                                 }
                                             }}});
    CHECK_EQ(field(o, "GETL0"), std::string("00 00"));
    CHECK_EQ(field(o, "NSTAMP"), std::string("FF 00"));
    CHECK_EQ(field(o, "NPLAIN"), std::string("00 00"));
    CHECK_EQ(field(o, "GETLN"), std::string("81 00"));
    CHECK_EQ(field(o, "NSTAMPS"), std::string("00 00"));
    CHECK_EQ(field(o, "NSTAMPV"), std::string("0000000000000000"));
    const std::vector<uint8_t> l = m.sector(0);
    CHECK_EQ(std::string(l.begin() + 1, l.begin() + 12), std::string("NOSTAMP LBL"));
    CHECK_EQ(l[12], 0x81);
    const std::vector<uint8_t> k = kept_pw("        ");
    CHECK(std::vector<uint8_t>(l.begin() + 13, l.begin() + 24) == k);
    CHECK(std::vector<uint8_t>(l.begin() + 24, l.begin() + 28) == now);
    CHECK(std::vector<uint8_t>(l.begin() + 28, l.begin() + 32) == now);
}

TEST(the_multi_sector_count_reads_and_writes_n_records_a_call)
{
    // On M:: 44's range; sequential and random reads and writes of N records, each at the DMA
    // address + 128 x k; the end of the file after some (A 1, X the records read), R0-R2 back to
    // the first record's, CR the last record tried's; a physical error's code in X, not a count;
    // and a disk filled 128 records a call, the last call's X what was left.
    Booted r;
    REQUIRE(r.ok);
    CpmDisk m{r, 0};
    // Each BDOS call's cycles, BDOSENT to STUBRET, by its function: the first 21s are MW8 and MW128,
    // the first 20s MR128 and MREND.
    std::map<uint8_t, std::vector<uint64_t>> took;
    uint8_t fn = 0;
    uint64_t t0 = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == sym("BDOSENT"))
        {
            fn = r.cpu->regs().y;
            t0 = r.cpu->cycles();
        }
        else if (pc == sym("STUBRET"))
            took[fn].push_back(r.cpu->cycles() - t0);
    };
    const std::string o = xtratest(r, 'S');
    r.watch = nullptr;
    if (took[21].size() >= 2 && took[20].size() >= 1)
        std::printf("  M: BDOS cycles: 21 of 8 records %llu, of 128 %llu (%llu a record); 20 of 128 %llu (%llu a record)\n",
                    static_cast<unsigned long long>(took[21][0]), static_cast<unsigned long long>(took[21][1]),
                    static_cast<unsigned long long>(took[21][1] / 128), static_cast<unsigned long long>(took[20][0]),
                    static_cast<unsigned long long>(took[20][0] / 128));
    const std::pair<const char *, const char *> want[] = {
        {"M0", "FF 00"},     {"M129", "FF 00"},   {"M128", "00 00"},   {"M1", "00 00"},     {"MW8", "00 00"},
        {"MW8CR", "08"},     {"MW128", "00 00"},  {"MW128CR", "08"},   {"MW128EX", "01"},   {"MR128", "00 00"},
        {"MRCHK", "Y"},      {"MREND", "01 08"},  {"MRECHK", "Y"},     {"MRECR", "08"},     {"MRR", "01 02"},
        {"MRRR", "000086"},  {"MRRCR", "08"},     {"MRRCHK", "Y"},     {"MRW", "00 00"},    {"MRWR", "0000C8"},
        {"MRWCR", "4A"},     {"MRZ", "00 00"},    {"MWRO", "FF 03"},
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    const std::vector<uint8_t> d = m.directory();
    const std::vector<unsigned> es = m.entries_of(d, 0, "MS      DAT");
    for (unsigned n : {0u, 7u, 8u, 127u, 128u, 135u, 200u, 201u, 202u, 300u, 301u})
        CHECK(m.record(d, es, n) == pattern_record(n));
    const unsigned free = hexval(field(o, "MFREE")), calls = hexval(field(o, "MCALLS"));
    const std::string full = field(o, "MFULL");
    CHECK_EQ(full.substr(0, 2), std::string("02"));
    std::printf("  M: %u records free, %u calls of 128, then %s\n", free, calls, full.c_str());
    CHECK_EQ(calls * 128 + hexval(full.substr(3, 2)), free);
    if (o.find("XTRATEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(a_physical_error_in_a_multi_sector_write_keeps_the_records_done_in_the_fcb)
{
    // On B:, the card's writes failing from FAULT?: a sequential write of 128 records after 64
    // fails (Disk I/O) moving to extent 1, after 64 more; the FCB is written back as they left it
    // (CR 128 of extent 0), not left as it was. The same by 34 from record 64: R0-R2 back to 64,
    // CR record 127's.
    for (const char c : {'P', 'Q'})
    {
        Rig r;
        REQUIRE(r.ok);
        auto fm = faulty(r);
        const std::string o = xtratest(r, c, {{"FAULT?", [&] { fm->fail = true; }}});
        fm->fail = false;
        if (c == 'P')
        {
            CHECK_EQ(field(o, "PWRITE"), std::string("FF 01"));
            CHECK_EQ(field(o, "PCR"), std::string("80"));
            CHECK_EQ(field(o, "PEX"), std::string("00"));
        }
        else
        {
            CHECK_EQ(field(o, "QWRITE"), std::string("FF 01"));
            CHECK_EQ(field(o, "QR"), std::string("000040"));
            CHECK_EQ(field(o, "QCR"), std::string("7F"));
        }
    }
}

TEST(a_multi_sector_call_whose_records_are_not_all_program_memory_does_nothing)
{
    // On M:: N records at the DMA address that reach the I/O hole, wrap past $FFFF or, for a
    // read, a read-only window are refused before the first (A = X = $FF), as one record there
    // is: no block taken, the FCB as it was, no byte of memory moved. 16 records to $BFFF are
    // all program memory.
    Booted r;
    REQUIRE(r.ok);
    const std::string o = xtratest(r, 'H');
    const std::pair<const char *, const char *> want[] = {
        {"HWR", "FF FF"}, {"HWRCR", "00"}, {"HW17", "FF FF"},  {"HWRAP", "FF FF"}, {"HW16", "00 00"},
        {"HW16CR", "10"}, {"HRD", "FF FF"}, {"HRDCHK", "Y"},    {"HRO", "FF FF"},   {"HROCHK", "Y"},
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    CHECK(field(o, "HFREE0").rfind("<no", 0) != 0);
    CHECK_EQ(field(o, "HFREE1"), field(o, "HFREE0"));
    if (o.find("XTRATEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(a_files_extent_zero_made_after_its_xfcb_keeps_the_xfcbs_mode_in_its_slot)
{
    // On M:, passwords on: E.DAT made at extent 16, then 103 (read mode); F.DAT made at extent 16
    // with f6' (write mode). Neither has an extent-zero entry for the XFCB's mode to go to, until a
    // 34 of record 0 makes one: its SFCB slot's mode byte is the XFCB's, not 0 (the Guide, 2.3.8).
    // G.DAT as E.DAT, but the label's passwords off before the 34: its slot takes the mode all the same.
    Booted r;
    REQUIRE(r.ok);
    CpmDisk m{r, 0};
    std::vector<uint8_t> d;
    const std::string o = xtratest(r, 'E', {{"ESLOT?", [&] { d = m.directory(); }}});
    for (const char *k : {"EXF", "EW0", "EMF", "FW0", "EGX", "EOFF", "GW0"})
        CHECK_EQ(field(o, k), std::string("00 00"));
    REQUIRE(!d.empty());
    for (const auto &[name, mode] :
         {std::pair<std::string, uint8_t>{"E       DAT", 0x80}, {"F       DAT", 0x40}, {"G       DAT", 0x80}})
    {
        const std::vector<unsigned> es = m.entries_of(d, 0, name);
        REQUIRE(es.size() == 2);
        CHECK_EQ(unsigned{d[es[0] * 32 + 12]}, 0u); // extent 0's entry first
        CHECK_EQ(unsigned{slot_mode(d, es[0])}, unsigned{mode});
        CHECK_EQ(xfcb_of(d, 0, name) >= 0, true);
    }
    if (o.find("XTRATEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(delete_with_f5_and_a_wildcard_and_a_sys_files_password_from_another_user)
{
    // On M:, passwords on: 19 with f5' of V?.DAT deletes V1's and V2's XFCBs (each slot's mode 0),
    // not W1's; user 0's protected SYS file opened from user 1 needs user 0's XFCB's password.
    Booted r;
    REQUIRE(r.ok);
    CpmDisk m{r, 0};
    const auto slot_of = [&](const std::vector<uint8_t> &d, const std::string &name) {
        const std::vector<unsigned> es = m.entries_of(d, 0, name);
        return es.empty() ? -1 : int{slot_mode(d, es[0])};
    };
    const std::string o = xtratest(r, 'V',
                                   {{"VSET?", [&] {
                                         const std::vector<uint8_t> d = m.directory();
                                         for (const char *n : {"V1      DAT", "V2      DAT", "W1      DAT"})
                                         {
                                             check_xfcb(d, 0, n, 0x80, "VPASS   ");
                                             CHECK_EQ(slot_of(d, n), 0x80);
                                         }
                                     }},
                                    {"VDEL?", [&] {
                                         const std::vector<uint8_t> d = m.directory();
                                         for (const char *n : {"V1      DAT", "V2      DAT"})
                                         {
                                             CHECK(xfcb_of(d, 0, n) < 0);
                                             CHECK_EQ(slot_of(d, n), 0); // the file kept, its mode 0
                                         }
                                         check_xfcb(d, 0, "W1      DAT", 0x80, "VPASS   ");
                                         CHECK_EQ(slot_of(d, "W1      DAT"), 0x80);
                                     }}});
    const std::pair<const char *, const char *> want[] = {
        {"VDEL", "00 00"}, {"VSYS", "00 00"}, {"VSYSNO", "FF 07"}, {"VSYSPW", "00 00"}, {"VSYSF8", "A0"},
    };
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    if (o.find("XTRATEST DONE") == std::string::npos)
        r.tail(2000);
}

TEST(write_protection_lasts_into_the_next_extent_and_a_new_slot_is_clear)
{
    // On M:: a write-protected file opened without its password stays read-only after a read moves
    // it on to extent 1 (f7' kept); with the label asking for no stamps, a new file's SFCB slot is
    // cleared all the same, so 102 reads zeros, not what was there.
    Booted r;
    REQUIRE(r.ok);
    std::string o = xtratest(r, 'W');
    CHECK_EQ(field(o, "WRD"), std::string("00 00"));
    CHECK_EQ(field(o, "WRX1"), std::string("FF 03"));
    Booted r2;
    REQUIRE(r2.ok);
    CpmDisk m2{r2, 0};
    o = xtratest(r2, 'Z', {{"DIRTY?", [&] {
                               std::vector<uint8_t> s = m2.sector(0);
                               CHECK_EQ(s[3 * 32], 0x21);
                               std::fill(s.begin() + 3 * 32 + 11, s.begin() + 3 * 32 + 21, 0xAA); // entry 1's slot
                               m2.put(0, s);
                           }}});
    CHECK_EQ(field(o, "ZLBL"), std::string("00 00"));
    CHECK_EQ(field(o, "ZSTAMPS"), std::string("00 00"));
    CHECK_EQ(field(o, "ZSTAMPV"), std::string("0000000000000000"));
    const std::vector<uint8_t> d = m2.directory();
    CHECK(m2.entries_of(d, 0, "Z       DAT") == std::vector<unsigned>{1u});
    CHECK(std::vector<uint8_t>(d.begin() + 3 * 32 + 11, d.begin() + 3 * 32 + 21) == std::vector<uint8_t>(10, 0));
}

TEST(the_ccp_starts_each_program_with_a_multi_sector_count_of_1)
{
    // XTRATEST C leaves 44 at 128 as it ends; the next program's one 21 writes one record.
    Booted r;
    REQUIRE(r.ok);
    const size_t from = start(r, "xtratest.com", " C");
    CHECK(r.until("COUNT SET", ms(5000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    const std::string o = xtratest(r, 'D');
    CHECK_EQ(field(o, "DCR"), std::string("01"));
}

TEST(functions_44_and_49_share_the_multi_sector_count)
{
    // One byte, the SCB's $4A: 44's count is what 49 reads, and 49's is what a 21 writes by.
    Booted r;
    REQUIRE(r.ok);
    const std::string o = xtratest(r, 'K');
    CHECK_EQ(field(o, "KGET"), std::string("05 FF")); // X the next byte: the error mode, $FF
    CHECK_EQ(field(o, "KSET"), std::string("00 00"));
    CHECK_EQ(field(o, "KW"), std::string("00 00"));
    CHECK_EQ(field(o, "KCR"), std::string("03")); // 3 records: 49's count, not 44's 5
    CHECK_EQ(field(o, "KGET7"), std::string("07 FF"));
}

// ---- phase 3b2: the system extras ------------------------------------------------------------

TEST(function_49_gets_and_sets_the_scb_at_cp_m_3s_offsets)
{
    Booted r;
    REQUIRE(r.ok);
    r.go("systest.com", " Z"); // what a program leaves the CCP puts back: '$', mode 0, count 1
    const std::string o = r.go("systest.com", " S");
    const std::pair<const char *, const char *> want[] = {
        {"CMODE0", "00 00"},   {"VER", "31 00"},      {"DMA", "80 02"},     {"WIDTH", "4F"},       {"PAGE", "18"},
        {"RUB", "00 FF"},      {"DELIM", "24 00"},   {"MULTI", "01 00"},    {"CHAIN0", "00 FF"},
        {"FLAGS", "80"},       {"COMBASE", "00 F8"}, {"PAST", "00 00"},     {"PASTFF", "00 00"},
        {"SETPAST", "00 00"},  {"LAST", "00 00"},    {"LASTGET", "34 00"},  {"LAST62", "00 34"},
        {"RESOP", "00 00"},    {"USERF0", "00 00"},  {"USERF", "EF BE"},    {"ERR", "FF 04"},
        {"ERRDRV", "0F"},      {"EMODE", "FE"},      {"DMA26", "34 12"},    {"DMA49", "N"},
        {"DISK", "0C 00"},     {"USER", "07 00"},     {"USER49", "05"},    {"CMODE", "00 03"},    {"D49", "AB"},
        {"AUX0", "1A 00"},     {"AUXL", "1A 00"},    {"DATE", "Y"},         {"LISTF", "00"},
        {"VERSION", "31 00"}};
    for (const auto &w : want)
        CHECK_EQ(field(o, w.first), std::string(w.second));
    CHECK(o.find("\nABC COLUMN 03\r\n") != std::string::npos);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + 0x0264), 0); // past the SCB: never written
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + 0x0263), 0);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + 0x0205), 0x31); // put back as it was
}

namespace
{
    /// SYSTEST's console case, its keys on the serial line or the USB keyboard (AUX: is the serial
    /// line, so AUX?'s is always there). @return what it printed
    std::string console_case(Rig &r, bool usb)
    {
        auto key = [&](const std::string &k) {
            if (usb)
                r.type_usb(k);
            else
                r.type(k);
        };
        const size_t from = start(r, "systest.com", " C");
        auto at = [&](const std::string &q) {
            const bool ok = r.until(q, ms(3000), from);
            if (!ok)
                r.tail();
            return ok;
        };
        if (!at("A7?"))
            return "";
        r.type_usb("j"); // CON:'s keyboard, not AUX:'s line
        if (!at("AUX?"))
            return "";
        // 4 and 5 (LST: put on the serial line) went to the serial line only, and printer echo too:
        // the screen has each once.
        bool lst = false, aux = false, echo = false;
        for (unsigned row = 0; row < 30; ++row)
            lst = lst || r.row(row) == "LST", aux = aux || r.row(row) == "AUX", echo = echo || r.row(row) == "ECHO";
        CHECK(lst);
        CHECK(aux);
        CHECK(echo);
        r.type("q");
        if (!at("KEEP?"))
            return "";
        key("k");
        if (!at("STOP?"))
            return "";
        key("\x13");
        r.run(ms(50));
        const size_t stop = r.out.find("STOP?", from) + 5;
        CHECK_EQ(r.out.substr(stop), std::string()); // stopped
        key("x");
        r.run(ms(20));
        CHECK_EQ(r.out.substr(stop), std::string("\a")); // a BEL for a key that isn't ^Q
        key("\x11");
        if (!at("PE?"))
            return "";
        key("\x13");
        key("\x10");
        key("\x11");
        if (!at("PPOO??"))
            return "";
        key("\x13");
        key("\x10");
        key("\x11");
        if (!at("DROP?"))
            return "";
        key("\x11");
        key("\x10");
        key("z");
        if (!at("RAW1?"))
            return "";
        key("\x13");
        if (!at("CC?"))
            return "";
        key("a");
        key("b");
        key("\x03");
        if (!at("S11?"))
            return "";
        key("\x11");
        key("w");
        if (!at("NC?"))
            return "";
        key("\x13");
        key("\x03");
        r.run(ms(20));
        CHECK(r.out.find("NC?\a", from) != std::string::npos); // ^C after ^S: a BEL, not the end
        key("\x11");
        if (!r.until("\r\nA>", ms(3000), from))
            r.tail();
        return r.out.substr(from);
    }

    void check_console(const std::string &o, bool usb)
    {
        const std::string k = usb ? "K" : "k", w = usb ? "W" : "w", z = usb ? "Z" : "z";
        CHECK_EQ(field(o, "MODE0"), std::string("00 00"));
        CHECK_EQ(field(o, "MODE1"), std::string("00 03"));
        CHECK_EQ(field(o, "DLM0"), std::string("24 00"));
        CHECK_EQ(field(o, "DLM1"), std::string("X25"));
        CHECK(o.find("\nPBLK\r\nP       Q\r\nP\tQ\t\r\nZB<>\r\n") != std::string::npos);
        CHECK(o.find("\nLST LLIST\tX\r\n") != std::string::npos); // 5 and 112 on LST: as they are
        CHECK(o.find("\nEECCHHOO\r\nRAW\r\n") != std::string::npos); // printer echo; none raw
        CHECK(o.find("\nAUX A\r\n") != std::string::npos);
        CHECK_EQ(field(o, "AUXIST"), std::string("00 00"));
        CHECK_EQ(field(o, "AUXOST"), std::string("FF 00"));
        CHECK(o.find("\nA7?00 J4A\r\n") != std::string::npos); // 7: the USB key isn't AUX:'s
        CHECK(o.find("\nAUX? GOT 71\r\n") != std::string::npos);
        CHECK(o.find("\nKEEP?z ST 01 FE FF K " + k + (usb ? "4B" : "6B") + "\r\n") != std::string::npos);
        CHECK(o.find("\nSTOP?\aAB\r\n") != std::string::npos);
        CHECK(o.find("\nPE?\aXXYYPPOO??XY\r\n") != std::string::npos);
        CHECK(o.find("\nDROP?" + z + (usb ? "5A" : "7A") + "\r\n") != std::string::npos);
        CHECK(o.find("\nRAW1?13\r\n") != std::string::npos);
        CHECK(o.find("\nCC?01 03\r\n") != std::string::npos); // 11 keeps the ^C for 6 to read
        CHECK(o.find("\nS11?01 " + w + (usb ? "57" : "77") + "\r\n") != std::string::npos);
        CHECK(o.find("\nNC?\aAB\r\n") != std::string::npos);
    }
} // namespace

TEST(the_console_functions_with_the_console_mode_and_printer_echo_from_the_serial_line)
{
    Booted r;
    REQUIRE(r.ok);
    // What the look before each character costs: function 2 of HELLO's characters, and 11 with
    // no key and the console mode 0 (S11?'s polling), which is that look.
    const uint16_t bdosent = sym("BDOSENT"), stubret = sym("STUBRET"), cout = sym("COUT"), looked = sym("LOOKED");
    uint64_t in = 0, total = 0, n = 0, stotal = 0, sn = 0, lin = 0, ltotal = 0, ln = 0;
    int fn = -1;
    bool plain = false;
    r.watch = [&](uint16_t pc) {
        if (pc == cout && fn == C_WRITE_FN && plain)
            lin = r.cpu->cycles();
        else if (pc == looked && lin)
            ltotal += r.cpu->cycles() - lin, ++ln, lin = 0;
        else if (pc == bdosent)
        {
            fn = r.cpu->regs().y, in = r.cpu->cycles();
            plain = r.phys(SYS_BLOCK_PHYS + 0x0233) == 0 && r.phys(SYS_BLOCK_PHYS + 0x0234) == 0;
        }
        else if (pc == stubret)
        {
            if (fn == C_WRITE_FN && plain && n < 1000)
                total += r.cpu->cycles() - in, ++n;
            else if (fn == C_STAT_FN && plain && r.cpu->regs().a == 0)
                stotal += r.cpu->cycles() - in, ++sn;
            fn = -1;
        }
    };
    const std::string o = console_case(r, false);
    total = n = ltotal = ln = 0; // function 2's from HELLO alone
    r.go("hello.com");
    r.watch = nullptr;
    check_console(o, false);
    if (n && ln)
        std::printf("  function 2, BDOSENT to its return: %llu cycles a character (%llu of them), the look at the console %llu of them\n",
                    static_cast<unsigned long long>(total / n), static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(ltotal / ln));
    if (sn)
        std::printf("  function 11 with no key, BDOSENT to its return: %llu cycles\n",
                    static_cast<unsigned long long>(stotal / sn));
    CHECK(n > 0);
    CHECK(ln > 0);
    CHECK(sn > 0);
}

TEST(the_console_functions_with_the_console_mode_and_printer_echo_from_the_usb_keyboard)
{
    Booted r;
    REQUIRE(r.ok);
    check_console(console_case(r, true), true);
}

TEST(function_1_echoes_only_graphic_characters)
{
    Booted r;
    REQUIRE(r.ok);
    const size_t from = start(r, "systest.com", " G");
    REQUIRE(r.until("G?", ms(3000), from));
    r.type("\x7F\xC1" "a"); // DEL and C1 returned, not echoed, no column
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("\nG?a 7FC161 COLUMN 03\r\n", from) != std::string::npos);
}

TEST(a_ctrl_s_ctrl_c_ends_the_program_and_function_10s_ctrl_c_too)
{
    Booted r;
    REQUIRE(r.ok);
    size_t from = start(r, "systest.com", " X");
    REQUIRE(r.until("XC?", ms(3000), from));
    r.type("\x13");
    r.type("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("AB", from) == std::string::npos);
    CHECK(r.out.find("NOT ENDED", from) == std::string::npos);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + 0x0210) | r.phys(SYS_BLOCK_PHYS + 0x0211) << 8, 0xFFFE);
    from = start(r, "systest.com", " W");
    REQUIRE(r.until("L?", ms(3000), from));
    r.type_usb("\x03");
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("NOT ENDED", from) == std::string::npos);
    CHECK_EQ(r.phys(SYS_BLOCK_PHYS + 0x0210) | r.phys(SYS_BLOCK_PHYS + 0x0211) << 8, 0xFFFE);
}

namespace
{
    /// Function 10's cases in SYSTEST's L, in order: the keys, the line it should read.
    struct LineCase
    {
        std::string keys, line;
    };
    const std::vector<LineCase> &line_cases()
    {
        static const std::vector<LineCase> c = {
            {"abc\x01\x01X\r", "aXbc"},           // ^A twice, then typing inserts
            {"abc\x02X\r", "Xabc"},               // ^B to the start
            {"abc\x02\x02Y\r", "abcY"},           // ^B there: to the end
            {"abcd\x01\x01\x07\r", "abd"},        // ^G the character at the cursor
            {"abcd\x01\x01\x0B\r", "ab"},         // ^K from it
            {"abcd\x01\x01\x18\r", "cd"},         // ^X before it
            {"abcd\x01\x01\x08\r", "acd"},        // BS in the middle
            {"ab\x01\x06\x06" "c\r", "abc"},      // ^F, and at the end nothing
            {"a\x19" "b\x1A\r", "a\x19" "b\x1A"}, // other control characters are the line's
            {"x\x03y\r", "x\x03y"},               // ^C after the first key
            {"\x17\r", "x\x03y"},                 // ^W: the previous line
            {"ab\x17" "c\r", "abc"},              // ^W in a line: to its end
            {"wxyz\x15\x17\r", "wxyz"},           // ^U: the previous line (a new length), then recalled
            {"abc\x01\x05\r", "abc"},             // ^E
            {"abc\x01\x12\r", "abc"},             // ^R
            {"ab\x10" "c\r", "abc"},              // ^P: printer echo on
            {"\x10\r", ""},                       // and off
            {"abc\x7F\r", "ab"},                  // DEL, as BS (SCB $2F $FF)
            {std::string(32, 'q') + "\r", std::string(30, 'q')}, // full: two BELs
            {"abcdef\x01\x01\x01\x01\x01\x01\x7F\r", "abcdef"}, // DEL at the start: nothing
            {"a\x01\x01\x08\x7F\x06\x06" "b\r", "ab"},
            {"abcd\r", "abc"},                    // a buffer of 3
            {"ab\r", "a"},                        // of 0, which is 1
            {"X\r", "INITX"},                     // the DMA buffer's string first
            {"", "AB"},                           // its CR: back at once
            {"\r", "ABC"},                        // its ^A
            {"abc\x7F\x7F\r", "a"},               // rub/del echoing (SCB $2F 0)
            {"ab\x08\r", "a"},                    // ^H as rub/del (SCB $2E $FF)
            {"abcdefghijkl\r", "abcdefghijkl"},   // a width of 9
            {"abcdefghij\x02X\r", "Xabcdefghij"}, // there, ^B back over a row's start
            {"abcdefg\x7F\r", "abcdef"},          // there, rub/del's echo on a new row
            {"\x03" "a\r", "\x03" "a"},           // console mode bit 3: ^C a character
        };
        return c;
    }
    std::string hexs(const std::string &b)
    {
        static const char *d = "0123456789ABCDEF";
        std::string h;
        for (const unsigned char c : b)
            h += d[c >> 4], h += d[c & 15];
        return h;
    }

    /// SYSTEST's L with each case's keys from the serial line or the USB keyboard (capitals:
    /// Caps Lock). @return what it printed
    std::string line_case(Rig &r, bool usb)
    {
        const size_t from = start(r, "systest.com", " L");
        size_t at = from;
        for (const LineCase &c : line_cases())
        {
            // The next question, L? or I?, after the last answer.
            size_t q = std::string::npos;
            const uint64_t end = r.cpu->cycles() + ms(3000);
            while (r.cpu->cycles() < end)
            {
                const size_t l = r.out.find("L?", at), i = r.out.find("I?", at);
                q = std::min(l, i);
                if (q != std::string::npos)
                    break;
                r.run(20'000);
            }
            if (q == std::string::npos)
            {
                r.tail();
                return "";
            }
            if (usb)
                r.type_usb(c.keys);
            else
                r.type(c.keys);
            if (!r.until("=", ms(3000), q))
            {
                r.tail();
                return "";
            }
            at = r.out.find("=", q);
        }
        if (!r.until("\r\nA>", ms(3000), at))
            r.tail();
        return r.out.substr(from);
    }

    void check_lines(const std::string &o, bool usb)
    {
        size_t at = 0;
        for (const LineCase &c : line_cases())
        {
            std::string line = c.line;
            if (usb && c.line != "INITX" && c.line != "AB" && c.line != "ABC")
                for (char &ch : line)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (usb && c.line == "INITX")
                line = "INITX";
            char n[3];
            std::snprintf(n, sizeof n, "%02X", static_cast<unsigned>(line.size()));
            const std::string want = "=" + std::string(n) + " " + hexs(line) + "\r\n";
            const size_t f = o.find("=", at);
            CHECK_EQ(o.substr(f, want.size()), want);
            at = f + 1;
        }
    }
} // namespace

TEST(function_10_edits_the_line_as_the_banked_cp_m_3s_from_the_serial_line)
{
    Booted r;
    REQUIRE(r.ok);
    const uint16_t insert = sym("INSERT"), loop = sym("LOOP");
    uint64_t in = 0, total = 0, n = 0;
    r.watch = [&](uint16_t pc) {
        if (pc == insert)
            in = r.cpu->cycles();
        else if (pc == loop && in)
            total += r.cpu->cycles() - in, ++n, in = 0;
    };
    const std::string o = line_case(r, false);
    r.watch = nullptr;
    check_lines(o, false);
    // What the screen got, for some.
    CHECK(o.find("L?abc\b\bXbc\b\b\bX\r") != std::string::npos);        // inserting
    CHECK(o.find("L?abcd\b\bd \b\b\r") != std::string::npos);            // ^G: d moved back, the end blanked
    CHECK(o.find("L?a^Yb^Z\r") != std::string::npos);                   // control characters as ^X
    CHECK(o.find("L?x^Cy\r") != std::string::npos);
    CHECK(o.find("L?wxyz\r\nwxyz\r") != std::string::npos);             // ^U: a new row; ^W the line
    CHECK(o.find("L?abc\b\r\nabc\b\r") != std::string::npos);           // ^R: the line again on a new row
    CHECK(o.find("L?abc\b\r\nc\b\r") != std::string::npos);             // ^E: the rest on a new row
    CHECK(o.find("L?ab\ac\r") != std::string::npos);                    // ^P: a BEL as echo goes on
    CHECK(o.find("L?" + std::string(30, 'q') + "\a\a\r") != std::string::npos); // full
    CHECK(o.find("L?abc\a\r") != std::string::npos);                  // a buffer of 3
    CHECK(o.find("L?abccb\r") != std::string::npos);                    // rub/del's echo
    CHECK(o.find("L?abb\r") != std::string::npos);
    CHECK(o.find("L?abcdefg\r\nhijkl\r") != std::string::npos);         // the width: a new row
    CHECK(o.find("L?abcdefg\r\ng\r") != std::string::npos);             // rub/del's echo: one too
    CHECK(o.find("\r\nabcdefghi\b\b\b\b\b\b\b\b\bXabcdefgh\b\b\b\b\b\b\b\b\bX\r") != std::string::npos);
    CHECK(o.find("I?INITX\r") != std::string::npos);
    CHECK(o.find("I?AB\r=") != std::string::npos);
    CHECK(o.find("L?^Ca\r") != std::string::npos);
    REQUIRE(n > 0);
    std::printf("  function 10, a key into the line (INSERT to the next key): %llu cycles on average (%llu keys)\n",
                static_cast<unsigned long long>(total / n), static_cast<unsigned long long>(n));
}

TEST(function_10_edits_the_line_as_the_banked_cp_m_3s_from_the_usb_keyboard)
{
    Booted r;
    REQUIRE(r.ok);
    check_lines(line_case(r, true), true);
}

TEST(function_152_parses_a_file_specification_as_the_guide_says)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = r.go("systest.com", " P");
    struct P
    {
        int ret; ///< -1 an error, 0 the end, else the offset + 1 of what it returns
        unsigned drive;
        std::string name, type, pw;
    };
    const std::vector<P> want = {
        {0, 2, "FOO", "TXT", "PW"},    {13, 0, "FOO", "BAR", ""}, {4, 0, "FOO", "", ""},
        {0, 0, "????????", "???", ""}, {0, 0, "X???????", "C??", ""}, {-1, 0, "", "", ""},
        {-1, 0, "", "", ""},           {-1, 0, "", "", ""},       {-1, 0, "", "", ""},
        {0, 16, "X", "", ""},          {-1, 0, "", "", ""},       {-1, 0, "", "", ""},
        {3, 0, "5A", "", ""},          {0, 2, "", "", ""},        {0, 0, "", "", ""},
        {0, 0, "", "", ""},            {-1, 0, "", "", ""},       {4, 0, "A", "B", ""},
        {2, 0, "A", "", ""},           {1, 0, "", "", ""},        {-1, 0, "", "", ""},
        {0, 0, "A", "", "P*W"},        {0, 0, "ABC", "", ""},     {2, 0, "A", "", ""},
        {-1, 0, "", "", ""},           {3, 0, "AB", "", ""},      {0, 0, "A", "", ""},
        {0, 0, "A", "TXT", "PASSWORD"}, {0, 13, "README", "", ""}, {2, 0, "A", "", ""},
        {-1, 0, "", "", ""},           {0, 0, "", "TYP", ""},     {4, 0, "A", "", "B"},
    };
    auto pad = [](std::string s, size_t n) { return s + std::string(n - s.size(), ' '); };
    for (size_t i = 0; i < want.size(); ++i)
    {
        char key[8];
        std::snprintf(key, sizeof key, "P%02X", static_cast<unsigned>(i));
        const std::string f = field(o, key);
        REQUIRE(f.size() == 4 + 1 + 4 + 1 + 52);
        const unsigned str = hexval(f.substr(0, 4)), ret = hexval(f.substr(5, 4));
        const P &w = want[i];
        std::string fcb;
        if (w.ret < 0)
            fcb = std::string(26, '\xAA');
        else
            fcb = std::string(1, static_cast<char>(w.drive)) + pad(w.name, 8) + pad(w.type, 3) +
                  std::string(4, '\0') + pad(w.pw, 8) + "\xAA\xAA";
        const unsigned want_ret = w.ret < 0 ? 0xFFFF : w.ret == 0 ? 0 : str + w.ret - 1;
        if (ret != want_ret || f.substr(10) != hexs(fcb))
            std::printf("  case %zu: %s\n", i, f.c_str());
        CHECK_EQ(ret, want_ret);
        CHECK_EQ(f.substr(10), hexs(fcb));
    }
}

TEST(function_107_returns_oodos_s_serial_number)
{
    Booted r;
    REQUIRE(r.ok);
    CHECK_EQ(field(r.go("systest.com", " N"), "SER"), std::string("4F4F44000001"));
}

TEST(function_47_chains_and_the_ccp_keeps_its_own_drive_and_user)
{
    Booted r;
    REQUIRE(r.ok);
    // A program's drive doesn't outlast it: the CCP's A: comes back.
    size_t from = start(r, "systest.com", " D");
    CHECK(r.until("ON M\r\n\r\nA>", ms(4000), from));
    CHECK(field(r.go("systest.com", " V"), "UD") == "00 00");
    // 47 with 0: HELLO runs, shown after the prompt, with the CCP's drive and user.
    from = start(r, "systest.com", " H");
    CHECK(r.until("\r\nA>HELLO\r\nHello from OODOS", ms(4000), from));
    CHECK(r.until("\r\nA>", ms(3000), r.out.find("Hello from OODOS", from)));
    CHECK(field(r.go("systest.com", " V"), "UD") == "00 00");
    // A command longer than the tail holds: 126 characters of it.
    from = start(r, "systest.com", " J");
    const std::string cmd = "HELLO " + std::string(116, 'y') + "zzzz";
    CHECK(r.until("\r\nA>" + cmd + "\r\nHello from OODOS", ms(4000), from));
    CHECK(r.out.find(cmd + "z", from) == std::string::npos);
    CHECK(r.until("\r\nA>", ms(3000), r.out.find("Hello from OODOS", from)));
    // 47 with $FF: M: and user 5 are the CCP's from then on.
    from = start(r, "systest.com", " U");
    const bool chained = r.until("\r\nM>HELLO\r\nHello from OODOS", ms(4000), from);
    if (!chained)
        r.tail();
    CHECK(chained);
    CHECK(r.until("\r\nM>", ms(3000), r.out.find("Hello from OODOS", from)));
    from = start(r, "systest.com", " V");
    CHECK(r.until("UD 05 0C\r\n\r\nM>", ms(4000), from));
}

TEST(function_108_s_return_code_is_kept_for_the_next_program)
{
    Booted r;
    REQUIRE(r.ok);
    auto code = [&] { return r.phys(SYS_BLOCK_PHYS + 0x0210) | r.phys(SYS_BLOCK_PHYS + 0x0211) << 8; };
    int at_start = -1; // the code as each program starts at $0300
    r.watch = [&](uint16_t pc) {
        if (pc == 0x0300 && r.nb(0) == 0)
            at_start = code();
    };
    CHECK_EQ(field(r.go("systest.com", " Q"), "CODE"), std::string("34 12"));
    CHECK_EQ(code(), 0x1234);                    // kept across the warm boot
    r.go("hello.com");
    CHECK_EQ(at_start, 0);                       // the CCP's 0 for a program it starts
    size_t from = start(r, "systest.com", " K"); // 4321, then 47 of HELLO
    CHECK(r.until("Hello from OODOS", ms(4000), from));
    CHECK_EQ(at_start, 0x4321);                  // a chained program gets it as it was
    CHECK(r.until("\r\nA>", ms(3000), r.out.find("Hello from OODOS", from)));
    from = start(r, "systest.com", " E");        // the default error mode: ended, $FFFD
    CHECK(r.until("OODOS Error On P: Invalid Drive\r\nBDOS Function = 15 File = X       .   \r\n", ms(3000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    CHECK(r.out.find("NOT ENDED", from) == std::string::npos);
    CHECK_EQ(code(), 0xFFFD);
    r.watch = nullptr;
}

// ---- phase 3c: the memory functions -----------------------------------------------------

/// MEMTEST's case `c` to its end, each prompt answered after its action ran.
std::string memtest(Rig &r, char c, const std::vector<std::pair<std::string, std::function<void()>>> &prompts = {})
{
    const size_t from = start(r, "memtest.com", std::string(" ") + c);
    for (const auto &[prompt, then] : prompts)
    {
        CHECK(r.until(prompt, ms(20000), from));
        then();
        r.type(" ");
    }
    CHECK(r.until("MEMTEST DONE", ms(60000), from));
    CHECK(r.until("\r\nA>", ms(3000), from));
    if (std::getenv("MEMTEST_SHOW"))
        std::printf("%s\n", r.out.substr(from).c_str());
    return r.out.substr(from);
}

/// Each key's value in `o`, against the table.
void check_fields(const std::string &o, const std::vector<std::pair<const char *, const char *>> &want)
{
    for (const auto &w : want)
    {
        const std::string got = field(o, w.first);
        CHECK_EQ(got, std::string(w.second));
        if (got != w.second)
            std::printf("  %s: %s, want %s\n", w.first, got.c_str(), w.second);
    }
}

TEST(function_51_s_dma_segment_moves_records_to_and_from_physical_memory)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = memtest(r, 'D');
    check_fields(o, {
        {"GET0", "8002FFFF"}, {"GET1", "00000010"}, {"DMK", "00 00"}, {"DW4", "00 00"}, {"DCL", "00 00"},
        {"DRD", "Y"}, {"DR2", "00 00"}, {"DR2CHK", "Y"}, {"DRX", "00 00"}, {"DRXCHK", "Y"}, {"D17", "01 00"},
        {"D17CHK", "Y"}, {"D18", "FF 00"}, {"DRST", "FF FF"}, {"DRSTCHK", "Y"}, {"DRB6", "FF FF"}, {"DRM", "FF FF"},
        {"DRBD", "FF FF"}, {"DRPAST", "FF FF"}, {"DRWRAP", "FF FF"}, {"DRM16", "01 04"}, {"DRM17", "FF FF"},
        {"DWMCR0", "04"}, {"DWM", "FF FF"}, {"DWMCR", "04"}, {"D17M", "FF FF"}, {"DFREE", "00 00"},
        {"DFREECPU", "Y"}, {"GET13", "80023412"},
    });
    // The records' bytes, against M:'s (the test's oracle, read behind OODOS's back): records 0-3
    // were written from $10000, 1-2 read to $14100, 3 to $1FFC0 across banks 1 and 2.
    CpmDisk m{r, 0};
    const std::vector<uint8_t> d = m.directory();
    const std::vector<unsigned> es = m.entries_of(d, 0, "D       DAT");
    REQUIRE(!es.empty());
    auto phys_at = [&](uint32_t a) {
        std::vector<uint8_t> b(REC32);
        for (uint32_t i = 0; i < REC32; ++i)
            b[i] = r.phys(a + i);
        return b;
    };
    for (unsigned k = 0; k < 4; ++k)
        CHECK(m.record(d, es, k) == phys_at(0x10000 + k * REC32));
    CHECK(m.record(d, es, 1) == phys_at(0x14100));
    CHECK(m.record(d, es, 2) == phys_at(0x14180));
    CHECK(m.record(d, es, 3) == phys_at(0x1FFC0));
    // The warm boot sets the segment back to none.
    const std::string w = memtest(r, 'W');
    check_fields(w, {{"WGET", "8002FFFF"}, {"WMAX", "00 00"}, {"WMAXM", "0010005000"}});
}

TEST(xmove_takes_a_physical_bank_through_function_50)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = memtest(r, 'B');
    check_fields(o, {{"BTO", "80 20"}, {"BCHK", "Y"}});
    CHECK(field(o, "BFROM") != "<no BFROM>");
    for (uint32_t i = 0; i < REC32; ++i)
        CHECK_EQ(r.phys(0x12000 + i), static_cast<uint8_t>(i));
}

TEST(cp_m_86_s_memory_functions_hand_out_banks_1_to_5_by_the_block)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = memtest(r, 'M');
    check_fields(o, {
        {"MAX", "00 00"}, {"MAXM", "0010005000"}, {"MAX1", "00 00"}, {"MAX1M", "0010000401"}, {"MAX0", "FF 00"},
        {"MAX0M", "3412000007"}, {"AL1", "00 00"}, {"AL1M", "0010000800"}, {"AL2", "00 00"}, {"AL2M", "0018000800"},
        {"AB1", "00 00"}, {"AB1M", "0020000400"}, {"AB2", "FF 00"}, {"AB2M", "001C000400"}, {"AB3", "FF 00"},
        {"AB3M", "0011000400"}, {"AB4", "FF 00"}, {"AB4M", "000C000400"}, {"AB5", "FF 00"}, {"AB5M", "0060000400"},
        {"AB6", "FF 00"}, {"AB6M", "001C000C00"}, {"AB7", "FF 00"}, {"AB7M", "0024000000"}, {"AM1", "FF 00"},
        {"AM1M", "0010000400"}, {"AM2", "00 00"}, {"AM2M", "0024003C00"}, {"AM3", "00 00"}, {"AM3M", "0024000C01"},
        {"AM4", "FF 00"}, {"AM4M", "001CFFFF00"}, {"MAX2", "00 00"}, {"MAX2M", "0024003C00"}, {"FR1", "FF 00"},
        {"FR1M", "001C000000"}, {"FR2", "00 00"}, {"FR2M", "0018000000"}, {"FR3", "FF 00"}, {"FR3M", "0018000000"},
        {"AL3", "FF 00"}, {"AL3M", "0000005000"}, {"MAX3", "00 00"}, {"MAX3M", "0024003C01"}, {"FRALL", "00 00"},
        {"FRALLM", "00000000FF"}, {"MAX4", "00 00"}, {"MAX4M", "0010005000"}, {"AL4", "00 00"},
        {"AL4M", "0010005000"}, {"AL5", "FF 00"}, {"AL5M", "0000010000"}, {"ALLFREE", "00 00"}, {"MAX5", "00 00"},
        {"MAX5M", "0010005000"}, {"MHOLE", "FF FF"}, {"AL6", "00 00"}, {"AL6M", "0010000400"},
    });
    // AL6's block, left allocated as the program ended, freed by the warm boot.
    const std::string w = memtest(r, 'W');
    check_fields(w, {{"WGET", "8002FFFF"}, {"WMAX", "00 00"}, {"WMAXM", "0010005000"}});
}

TEST(function_73_gives_a_cpu_address_s_paragraph_and_offset)
{
    Booted r;
    REQUIRE(r.ok);
    const std::string o = memtest(r, 'P');
    check_fields(o, {
        {"P0300", "00 00"}, {"P0300M", "00003000"}, {"P4123", "00 00"}, {"P4123M", "03001204"}, {"P8FFF", "00 00"},
        {"P8FFFM", "0F00FF08"}, {"PC900", "00 00"}, {"PC900M", "000090F8"}, {"PHOLE", "FF FF"},
        {"PHOLEM", "00C00000"}, {"PC7FF", "FF FF"}, {"PC7FFM", "FFC70000"}, {"P37", "00 00"}, {"P37M", "00000194"},
        {"PFL", "FF FF"}, {"PFLM", "00400000"}, {"PRO", "00 00"}, {"PROM", "00000004"}, {"PBLK", "FF FF"},
    });
}

/// A card unit's sector written through the card's registers while OODOS waits at a prompt.
void write_unit(Rig &r, unsigned unit, uint32_t lba, const std::vector<uint8_t> &s)
{
    r.sd().write(SdCard::UNIT, static_cast<uint8_t>(unit));
    for (unsigned i = 0; i < 4; ++i)
        r.sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
    r.sd().write(SdCard::COUNT, 1);
    r.sd().write(SdCard::COUNT + 1, 0);
    r.sd().write(SdCard::CMD, SdCard::WRITE);
    r.run(ms(1));
    for (const uint8_t b : s)
        r.sd().write(SdCard::DATA, b);
    r.run(ms(1));
    r.sd().write(SdCard::UNIT, 0);
}

TEST(function_74_mounts_and_unmounts_b_to_g_as_drives_txt_has_paths)
{
    Rig r;
    REQUIRE(r.ok);
    auto fm = std::make_shared<FaultyMedia>(r.media->bytes(), "card");
    r.media = fm;
    r.sd().insert(fm, false);
    r.run(ms(SdCard::INIT_MS + 10));
    REQUIRE(r.mount(1, "/OODOS/A.IMG") == 0); // A: only
    REQUIRE(r.boot());
    const std::string o = memtest(r, 'N',
                                  {{"SECT?", [&] {
                                        std::vector<uint8_t> s = r.read_unit(3, 0);
                                        std::vector<uint8_t> e(32, 0);
                                        const std::string y = "Y       DAT";
                                        std::copy(y.begin(), y.end(), e.begin() + 1);
                                        std::copy(e.begin(), e.end(), s.begin() + 64); // entry 2
                                        write_unit(r, 3, 0, s);
                                    }},
                                   {"FAULT?", [&] { fm->fail = true; }},
                                   {"MEND?", [&] { fm->fail = false; }}});
    check_fields(o, {
        {"NA", "FF 04"}, {"NM", "FF 04"}, {"NH", "FF 04"}, {"NP1", "FF 10"}, {"NP2", "FF 10"}, {"NP3", "FF 10"},
        {"NP4", "FF 10"}, {"NP5", "FF 10"}, {"NP6", "FF 10"}, {"NP7", "FF 10"}, {"NP8", "FF 10"}, {"NP9", "FF 10"},
        {"NLONG", "FF 10"}, {"NLEN", "FF 11"}, {"NNF", "FF 11"}, {"NDIR", "FF 11"}, {"NSZ", "FF 12"},
        {"NSZSEL", "FF 04"}, {"NSAME", "FF 13"}, {"NSAMESEL", "FF 04"}, {"NC", "00 00"}, {"NCSEL", "00 00"},
        {"NC17", "FF 00"}, {"NCW", "00 00"}, {"NC2", "00 00"}, {"NCY", "02 00"}, {"NUC", "00 00"},
        {"NUCSEL", "FF 04"}, {"NUC2", "00 00"}, {"NRO", "00 00"}, {"NROMK", "FF 02"}, {"NC3", "00 00"},
        {"NWW", "00 00"}, {"NWB", "FF 01"}, {"NWBSEL", "00 00"}, {"NWB2", "00 00"}, {"NC4", "00 00"},
        {"NWBCHK", "Y"},
    });
    // What C: wrote went to B.IMG (unit 3, mounted by 74), the record written again after the
    // card's writes came back among it, and Y.DAT as the test put it there.
    CpmDisk c{r, 3};
    const std::vector<uint8_t> d = c.directory();
    const std::vector<unsigned> xs = c.entries_of(d, 0, "X       DAT");
    REQUIRE(!xs.empty());
    CHECK(c.record(d, xs, 0) == std::vector<uint8_t>(REC32, 0xC3));
    CHECK(!c.entries_of(d, 0, "Y       DAT").empty());
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
