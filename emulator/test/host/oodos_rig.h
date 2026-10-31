/**
 * @file oodos_rig.h
 * @brief OODOS.SYS booted on backplane.machine for a test: the Rig and its helpers.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * test_oodos_sys.cpp's machinery, shared with test_oodos_ccp.cpp: the ROM BIOS
 * booted to its monitor, a card in the slot and its units mounted as OOLDR
 * would, OODOS.SYS's segments loaded from its header and its cold boot
 * started; then keys on the serial line or the USB keyboard, GO with a
 * .COM, the card's sectors, M:'s directory and physical memory read behind
 * OODOS's back. The includer includes harness.h first and has the
 * EMU_OODOS_* definitions CMakeLists.txt gives test_oodos_sys.
 */

#pragma once

#include "sd_images.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_media.h"
#include "usb_card.h"

#include <algorithm>
#include <cstdlib>
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

namespace oodos_rig
{
    using namespace ooe::test;
    using namespace ooe::emu;
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
    /// oodos/sys's build: the build tree's, or OODOS_SYS_DIR's (a build of a changed copy, to see a
    /// test fail on a fault put in on purpose).
    inline const std::string SYS = std::getenv("OODOS_SYS_DIR") ? std::getenv("OODOS_SYS_DIR") : EMU_OODOS_SYS_DIR;

    inline std::vector<uint8_t> read_bytes(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    inline std::string read_text(const std::string &path)
    {
        const std::vector<uint8_t> b = read_bytes(path);
        return std::string(b.begin(), b.end());
    }

    /// An ld65 -Ln file's labels.
    inline std::map<std::string, uint16_t> labels(const std::string &path)
    {
        std::map<std::string, uint16_t> out;
        std::istringstream in(read_text(path));
        std::string al, hex, name;
        while (in >> al >> hex >> name)
            if (al == "al" && name.size() > 1)
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(hex, nullptr, 16));
        return out;
    }
    inline const std::map<std::string, uint16_t> &sys_labels()
    {
        static const std::map<std::string, uint16_t> l = labels(SYS + "/oodos.lbl");
        return l;
    }
    inline uint16_t sym(const std::string &name)
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
    inline unsigned days(unsigned y, unsigned m, unsigned d)
    {
        static const unsigned DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        unsigned n = d;
        for (unsigned year = 1978; year < y; ++year)
            n += year % 4 == 0 ? 366 : 365;
        for (unsigned month = 1; month < m; ++month)
            n += DAYS[month - 1] + (month == 2 && y % 4 == 0);
        return n;
    }
    inline std::string hex(unsigned v, int digits)
    {
        char b[16];
        std::snprintf(b, sizeof b, "%0*X", digits, v);
        return b;
    }

    /// A diskdef's CP/M 3 DPB, worked out from oodos/diskdefs, as the 17 bytes the BIOS should have.
    inline std::string dpb_hex(const std::string &name)
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
    inline std::vector<uint8_t> fat16_card(const std::vector<std::pair<std::string, uint32_t>> &files)
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
    inline std::string field(const std::string &text, const std::string &key)
    {
        const size_t at = text.find("\n" + key + " ");
        if (at == std::string::npos)
            return "<no " + key + ">";
        const size_t from = at + key.size() + 2;
        return text.substr(from, text.find('\r', from) - from);
    }

    /// Common memory's 2 KB copied into block b's top, as OODOS will do for a block allocated for code.
    inline void give_common(Rig &r, unsigned b)
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
    inline std::vector<uint8_t> pattern_record(unsigned n)
    {
        std::vector<uint8_t> b(RECORD);
        for (unsigned j = 0; j < RECORD; ++j)
            b[j] = static_cast<uint8_t>(((n & 0xFF) + j) ^ (n >> 8) ^ 0x5A);
        return b;
    }

    /// How many times these bytes are in physical memory from `from`, `len` bytes.
    inline size_t count_in(const std::vector<uint8_t> &m, const std::vector<uint8_t> &what)
    {
        size_t n = 0;
        for (auto at = std::search(m.begin(), m.end(), what.begin(), what.end()); at != m.end();
             at = std::search(at + 1, m.end(), what.begin(), what.end()))
            ++n;
        return n;
    }
    inline size_t in_memory(Rig &r, uint32_t from, uint32_t len, const std::vector<uint8_t> &what)
    {
        std::vector<uint8_t> m(len);
        for (uint32_t i = 0; i < len; ++i)
            m[i] = r.phys(from + i);
        return count_in(m, what);
    }
    /// How many times these bytes are on the card.
    inline size_t on_card(Rig &r, const std::vector<uint8_t> &what) { return count_in(r.media->bytes(), what); }

    /// Run a test program by GO with `tail`, without waiting for the prompt.
    inline size_t start(Rig &r, const std::string &com, const std::string &tail = "")
    {
        r.poke_com(read_bytes(SYS + "/" + com));
        const size_t from = r.out.size();
        r.type("GO" + tail + "\r");
        return from;
    }

    /// A 24-bit hex field as a number.
    inline unsigned hexval(const std::string &s) { return static_cast<unsigned>(std::stoul(s, nullptr, 16)); }
} // namespace oodos_rig
