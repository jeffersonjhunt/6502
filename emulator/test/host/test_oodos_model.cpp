/**
 * @file test_oodos_model.cpp
 * @brief The BDOS's randomized model test (hw/oodos.md, "The BDOS, phase 3"; oodos/test/modeltest.py).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * For each seed, oodos/test/modeltest.py makes a script of file operations
 * and an oodos-8m drive A: holding it and DRIVER.COM (oodos/test/driver/,
 * built by CMake through the rules gate); DRIVER.COM runs the script through
 * the BDOS and writes what each call returned to A:RESULT.BIN; modeltest.py
 * check runs the same script on the model and compares the results, then the
 * drive under test's directory and files, printing the seed, the operation
 * and the difference.
 *
 *   - DRIVER.COM is a .COM to OODOS's header and fits the TPA;
 *   - DRIVER.COM on the emulator's W65C02S with the model as its BDOS (the
 *     protocol in modeltest.py: JSR $0203 answered by `modeltest.py serve`),
 *     for seeds on all three formats: the comparison passes, so the driver,
 *     the script and result formats and the reference driver agree;
 *   - the same with the model made wrong on purpose (an update stamp, a
 *     record count, a directory code, a byte of data, an attribute on A:):
 *     the comparison fails, naming the seed;
 *   - DRIVER.COM under OODOS: the CCP runs it from A: and it reports;
 *   - with OODOS_MODEL_BDOS on (the CMake option, on by default; the
 *     environment variable OODOS_MODEL_BDOS=0 or 1 for one run): each seed
 *     run by DRIVER.COM under OODOS.SYS on backplane.machine, B: (oodos-8m,
 *     oodos-32m) on the card or M:, compared with the model. The default
 *     seeds (modeltest.py seeds: 1, 4, 6, 10, 14, 24, 41, 45, the three
 *     formats; each of the BDOS's bugs the comparison found fails one of
 *     them, and between them they fill a disk, a directory on M: and on B:
 *     and meet a read-only drive, which test_model.py checks) take about 6
 *     minutes under ASan; OODOS_MODEL_SEEDS (1,2,7-60...) chooses others,
 *     OODOS_MODEL_ONLY=1 runs the comparison alone (so seeds can run in
 *     parallel, a process each: oodos/test/model_seeds.sh), OODOS_MODEL_CUT=N
 *     a seed's first N operations. OODOS_MODEL_COMPARE (renumber, exact),
 *     OODOS_MODEL_ALLOC (lowest, nearest) and OODOS_MODEL_LOOSE_X=1 pass to
 *     modeltest.py check; any other value fails the run before it starts.
 *     With the comparison off, its case says it's skipped and the run exits
 *     77, which ctest counts as skipped; with OODOS_MODEL_ONLY=1 too, it fails.
 */

#include "harness.h"
#include "test_support.h"

#include "backplane_bridge.h"
#include "console.h"
#include "cpu6502.h"
#include "machine.h"
#include "sd_card.h"
#include "sd_media.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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
    constexpr uint16_t NB = 0xC680;
    const std::string OODOS = EMU_OODOS_SRC;
    const std::string MODELTEST = OODOS + "/test/modeltest.py";
    const std::string DRIVER = EMU_OODOS_DRIVER;
    const std::string WORK = EMU_OODOS_MODEL_DIR;

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
    void write_bytes(const std::string &path, const uint8_t *p, size_t n)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(p), static_cast<std::streamsize>(n));
    }

    std::string quote(const std::string &s)
    {
        std::string q = "'";
        for (const char c : s)
            q += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return q + "'";
    }

    /// Run modeltest.py with `args`. @return its exit status; its output in `out`
    int modeltest(const std::string &args, std::string &out)
    {
        const std::string cmd = quote(EMU_PYTHON) + " -I " + quote(MODELTEST) + " " + args + " 2>&1";
        FILE *p = popen(cmd.c_str(), "r");
        if (!p)
            return -1;
        out.clear();
        std::array<char, 4096> buf{};
        size_t n = 0;
        while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0)
            out.append(buf.data(), n);
        const int st = pclose(p);
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }

    void print_lines(const std::string &text)
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
            std::printf("  %s\n", line.c_str());
    }

    std::string seed_dir(const std::string &tag, unsigned seed) { return WORK + "/" + tag + "-" + std::to_string(seed); }

    /// The formats the seeds go round (modelscript.format_of's, model_cpmtools.sh's).
    const char *format_of(unsigned seed)
    {
        static const char *const F[] = {"oodos-ram", "oodos-8m", "oodos-32m"};
        return F[seed % 3];
    }

    bool prepare(unsigned seed, const std::string &dir)
    {
        std::string out, cut;
        if (const char *c = std::getenv("OODOS_MODEL_CUT"); c && *c)
            cut = std::string(" --cut ") + c;                  // the script's first N operations only (main checked it)
        const int rc = modeltest("prepare --seed " + std::to_string(seed) + " --format " + format_of(seed) +
                                     " --dir " + quote(dir) + " --driver " + quote(DRIVER) + cut,
                                 out);
        if (rc != 0)
            print_lines(out);
        return rc == 0;
    }

    /// modeltest.py check on dir. @return whether it passed; what it said in `out`
    bool check(const std::string &dir, std::string &out, const std::string &extra = "")
    {
        return modeltest("check --dir " + quote(dir) + extra, out) == 0 && out.find("PASS") != std::string::npos;
    }

    // ------------------------------------------------------------------ the model as the BDOS

    /// `modeltest.py serve`, a line at a time over two pipes.
    class Server
    {
    public:
        Server(const std::string &dir, const std::string &fault)
        {
            int in[2], out[2];
            if (pipe(in) != 0 || pipe(out) != 0)
                return;
            pid_ = fork();
            if (pid_ == 0)
            {
                dup2(in[0], 0);
                dup2(out[1], 1);
                close(in[1]);
                close(out[0]);
                std::vector<std::string> a = {EMU_PYTHON, "-I", MODELTEST, "serve", "--dir", dir};
                if (!fault.empty())
                    a.insert(a.end(), {"--fault", fault});
                std::vector<char *> argv;
                for (auto &s : a)
                    argv.push_back(s.data());
                argv.push_back(nullptr);
                execv(EMU_PYTHON, argv.data());
                _exit(127);
            }
            close(in[0]);
            close(out[1]);
            to_ = fdopen(in[1], "w");
            from_ = fdopen(out[0], "r");
        }
        ~Server()
        {
            if (to_)
                std::fclose(to_);
            if (from_)
                std::fclose(from_);
            if (pid_ > 0)
                waitpid(pid_, nullptr, 0);
        }
        bool ok() const { return to_ && from_; }
        void send(const std::string &line)
        {
            std::fputs((line + "\n").c_str(), to_);
            std::fflush(to_);
        }
        bool line(std::string &out)
        {
            char *buf = nullptr;
            size_t cap = 0;
            const ssize_t n = getline(&buf, &cap, from_);
            out = n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
            std::free(buf);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                out.pop_back();
            return n > 0;
        }

    private:
        pid_t pid_{-1};
        FILE *to_{nullptr};
        FILE *from_{nullptr};
    };

    std::string hex_of(const uint8_t *p, size_t n)
    {
        static const char D[] = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; ++i)
            s += D[p[i] >> 4], s += D[p[i] & 15];
        return s;
    }

    /// DRIVER.COM on the W65C02S, its BDOS calls answered by the model. @return whether it got to the
    /// warm boot; what it printed in `printed`
    bool run_on_the_model(const std::string &dir, const std::string &fault, std::string &printed)
    {
        const std::vector<uint8_t> com = read_bytes(DRIVER);
        if (com.size() < 16)
            return false;
        FlatBus bus;
        for (size_t i = 16; i < com.size(); ++i)
            bus.at(static_cast<uint16_t>(0x0300 + i - 16)) = com[i];
        const uint8_t base[] = {0x4C, 0x00, 0x02, 0x4C, 0xFD, 0xBF};   // JMP $0200, JMP $BFFD
        for (unsigned i = 0; i < sizeof base; ++i)
            bus.at(static_cast<uint16_t>(0x0200 + i)) = base[i];
        bus.at(0x01FE) = 0xFF;                                            // RTS goes to $0200
        bus.at(0x01FF) = 0x01;
        Cpu6502 cpu(bus, Cpu6502::Model::W65C02S);
        cpu.set_regs(Registers{0x0300, 0xFD, 0, 0, 0, Cpu6502::FLAG_U | Cpu6502::FLAG_I});
        Server srv(dir, fault);
        if (!srv.ok())
            return false;
        uint64_t calls = 0;
        for (uint64_t steps = 0; steps < 4'000'000'000ULL; ++steps)
        {
            Registers r = cpu.regs();
            if (r.pc == 0x0200)
            {
                srv.send("Q");
                std::string l;
                srv.line(l);
                std::printf("  %llu BDOS calls, %llu cycles\n", static_cast<unsigned long long>(calls),
                            static_cast<unsigned long long>(cpu.cycles()));
                return l == "Q";
            }
            if (r.pc == 0x0203)
            {
                ++calls;
                srv.send("C " + std::to_string(r.y) + " " + std::to_string(r.a) + " " + std::to_string(r.x));
                std::string l;
                for (;;)
                {
                    if (!srv.line(l))
                    {
                        std::printf("  the model stopped answering\n");
                        return false;
                    }
                    std::istringstream w(l);
                    std::string k;
                    w >> k;
                    if (k == "R")
                    {
                        unsigned addr = 0, n = 0;
                        w >> addr >> n;
                        std::vector<uint8_t> b(n);
                        for (unsigned i = 0; i < n; ++i)
                            b[i] = bus.at(static_cast<uint16_t>(addr + i));
                        srv.send(hex_of(b.data(), n));
                    }
                    else if (k == "W" || k == "O")
                    {
                        unsigned addr = 0;
                        if (k == "W")
                            w >> addr;
                        std::string h;
                        w >> h;
                        for (size_t i = 0; i + 1 < h.size(); i += 2)
                        {
                            const auto v = static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16));
                            if (k == "W")
                                bus.at(static_cast<uint16_t>(addr + i / 2)) = v;
                            else
                                printed += static_cast<char>(v);
                        }
                    }
                    else if (k == "E")
                    {
                        unsigned a = 0, x = 0;
                        w >> a >> x;
                        r.a = static_cast<uint8_t>(a);
                        r.x = static_cast<uint8_t>(x);
                        break;
                    }
                    else
                    {
                        std::printf("  the model: %s\n", l.c_str());
                        return false;
                    }
                }
                // Return as the BDOS does: RTS, D clear.
                const uint16_t lo = bus.at(static_cast<uint16_t>(0x100 + static_cast<uint8_t>(r.s + 1)));
                const uint16_t hi = bus.at(static_cast<uint16_t>(0x100 + static_cast<uint8_t>(r.s + 2)));
                r.s = static_cast<uint8_t>(r.s + 2);
                r.pc = static_cast<uint16_t>((hi << 8 | lo) + 1);
                r.p = static_cast<uint8_t>(r.p & ~Cpu6502::FLAG_D);
                cpu.set_regs(r);
                continue;
            }
            cpu.step();
            if (cpu.state() == Cpu6502::State::Stopped)
                break;
        }
        std::printf("  DRIVER.COM never got to the warm boot\n");
        return false;
    }

    // ------------------------------------------------------------------ OODOS on backplane.machine

    /// OODOS.SYS's segments from its header (hw/oodos.md), as OOLDR loads them.
    struct SysFile
    {
        uint16_t entry{0};
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segments;

        explicit SysFile(const std::vector<uint8_t> &f)
        {
            if (f.size() < RECORD || std::memcmp(f.data(), "OOSY", 4) != 0)
                return;
            entry = static_cast<uint16_t>(f[6] | f[7] << 8);
            for (unsigned i = 0; i < f[8] && i < 8; ++i)
            {
                const uint8_t *e = &f[16 + i * 8];
                const uint32_t phys = unsigned{e[0]} | unsigned{e[1]} << 8 | unsigned{e[2]} << 16;
                const size_t len = size_t{e[3]} | size_t{e[4]} << 8, rec = size_t{e[5]} | size_t{e[6]} << 8;
                if (rec * RECORD + len <= f.size())
                    segments.emplace_back(phys, std::vector<uint8_t>(f.begin() + static_cast<long>(rec * RECORD),
                                                                     f.begin() + static_cast<long>(rec * RECORD + len)));
            }
        }
    };

    /// A FAT16 card, no partition table, holding these files contiguously (test_oodos_sys's way, with
    /// clusters small enough that 8 MB is FAT16's 4,085 or more).
    /// @return the card; each file's byte offset in `at`
    std::vector<uint8_t> fat16_card(const std::vector<std::pair<std::string, const std::vector<uint8_t> *>> &files,
                                    std::vector<size_t> &at)
    {
        constexpr unsigned SPC = 4, ROOT = 32, SEC = 512, CLUSTER = SPC * SEC;  // 2 KB: FAT16 from 8 MB
        unsigned clusters = 0;
        for (const auto &f : files)
            clusters += static_cast<unsigned>((f.second->size() + CLUSTER - 1) / CLUSTER);
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
        put16(b + 14, 1);
        b[16] = 1;
        put16(b + 17, ROOT * SECTOR / 32);
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
        at.clear();
        for (size_t i = 0; i < files.size(); ++i)
        {
            const auto &[name, bytes] = files[i];
            const auto n = static_cast<unsigned>((bytes->size() + CLUSTER - 1) / CLUSTER);
            uint8_t *e = root + i * 32;
            std::memset(e, ' ', 11);
            const size_t dot = name.find('.');
            std::memcpy(e, name.data(), dot);
            std::memcpy(e + 8, name.data() + dot + 1, name.size() - dot - 1);
            e[11] = 0x20;
            put16(e + 26, next);
            put32(e + 28, static_cast<unsigned>(bytes->size()));
            for (unsigned c = 0; c < n; ++c)
                put16(fat + 2 * (next + c), c + 1 == n ? 0xFFFF : next + c + 1);
            const size_t off = (size_t{data} + size_t{next - 2} * SPC) * SECTOR;
            std::memcpy(b + off, bytes->data(), bytes->size());
            at.push_back(off);
            next += n;
        }
        return card;
    }

    /// backplane.machine at the ROM's monitor with a card in, then OODOS.SYS booted as OOLDR leaves it.
    struct Rig
    {
        std::unique_ptr<Machine> machine;
        std::unique_ptr<Console> console;
        std::unique_ptr<Cpu6502> cpu;
        std::shared_ptr<MemoryMedia> media;
        std::string out;
        bool ok{false};

        explicit Rig(std::vector<uint8_t> card)
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

        SdCard &sd() { return *machine->sd_card(); }
        uint8_t phys(uint32_t a) const { return machine->bus().peek_physical(a); }

        void run(uint64_t cycles)
        {
            const uint64_t end = cpu->cycles() + cycles;
            while (cpu->cycles() < end)
            {
                cpu->step();
                if (machine->bus().irq())
                    cpu->irq();
                if (cpu->cycles() % 64 > 7)
                    continue;
                console->service();
                uint8_t ch = 0;
                while (console->output(ch))
                    out += static_cast<char>(ch);
            }
        }
        bool until(const std::string &text, uint64_t budget, size_t from = 0)
        {
            const uint64_t end = cpu->cycles() + budget;
            while (out.find(text, from) == std::string::npos && cpu->cycles() < end)
                run(20'000);
            return out.find(text, from) != std::string::npos;
        }
        void type(const std::string &keys)
        {
            for (const char k : keys)
            {
                for (int i = 0; i < 2000 && !console->key(static_cast<uint8_t>(k)); ++i)
                    run(1'000);
                run(30'000);
            }
        }
        uint8_t mount(unsigned unit, const std::string &path)
        {
            sd().write(SdCard::UNIT, 0);
            sd().write(SdCard::CMD, SdCard::MOUNT);
            std::vector<uint8_t> p = {static_cast<uint8_t>(unit), 0};
            p.insert(p.end(), path.begin(), path.end());
            for (size_t i = 0; i < SECTOR; ++i)
                sd().write(SdCard::DATA, i < p.size() ? p[i] : 0);
            run(ms(2));
            return sd().read(SdCard::STATUS) & SdCard::ERR ? sd().read(SdCard::ERROR) : 0;
        }
        void set_time(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi)
        {
            Ds3231::Time t;
            t.year = y, t.month = mo, t.date = d, t.hour = h, t.minute = mi, t.second = 0;
            t.day = 1;
            machine->backplane_bridge()->set_rtc_time(t);
        }
        bool boot()
        {
            const SysFile f(read_bytes(std::string(EMU_OODOS_SYS_DIR) + "/OODOS.SYS"));
            if (f.segments.empty())
            {
                std::printf("  no OODOS.SYS\n");
                return false;
            }
            for (const auto &[phys, bytes] : f.segments)
                for (size_t i = 0; i < bytes.size(); ++i)
                    machine->bus().write_physical(phys + static_cast<uint32_t>(i), bytes[i]);
            const uint8_t windows[] = {0, 60, 61, 62, 62};
            for (unsigned i = 0; i < 5; ++i)
                machine->bus().write(static_cast<uint16_t>(NB + i), windows[i]);
            Registers r = cpu->regs();
            r.pc = f.entry;
            r.p = Cpu6502::FLAG_U | Cpu6502::FLAG_I;
            cpu->set_regs(r);
            return until("A>", ms(3000), out.size());
        }
        void tail(size_t n = 400) const
        {
            std::printf("  the serial line's end: %s\n", out.substr(out.size() > n ? out.size() - n : 0).c_str());
        }
    };

    constexpr uint32_t MDISK = 0xC0000, MDISK_SIZE = 0x20000;

    /// The seed's A: (and B:) on a card under OODOS, DRIVER typed at the CCP. Leaves DIR/A.out.IMG and
    /// DIR/T.out.IMG (and, for M:, DIR/T.IMG as cold boot made it). @return what DRIVER printed, or ""
    std::string run_on_oodos(const std::string &dir, const std::string &fmt, uint64_t budget)
    {
        const std::vector<uint8_t> a = read_bytes(dir + "/A.IMG"), t = read_bytes(dir + "/T.IMG");
        const bool sd_drive = fmt != "oodos-ram";
        std::vector<std::pair<std::string, const std::vector<uint8_t> *>> files = {{"A.IMG", &a}};
        if (sd_drive)
            files.emplace_back("B.IMG", &t);
        std::vector<size_t> at;
        Rig rig(fat16_card(files, at));
        if (!rig.ok)
            return "";
        rig.set_time(2026, 10, 27, 12, 0);
        if (rig.mount(1, "/A.IMG") != 0 || (sd_drive && rig.mount(2, "/B.IMG") != 0))
        {
            std::printf("  MOUNT refused the drives\n");
            return "";
        }
        if (!rig.boot())
        {
            std::printf("  OODOS didn't come up\n");
            rig.tail();
            return "";
        }
        if (!sd_drive)
        {
            std::vector<uint8_t> m(MDISK_SIZE);
            for (uint32_t i = 0; i < MDISK_SIZE; ++i)
                m[i] = rig.phys(MDISK + i);
            write_bytes(dir + "/T.IMG", m.data(), m.size());
        }
        const size_t from = rig.out.size();
        rig.type("DRIVER\r");
        if (!rig.until("\r\nA>", budget, from + 8))
        {
            std::printf("  DRIVER didn't come back to the prompt\n");
            rig.tail();
        }
        const std::vector<uint8_t> &card = rig.media->bytes();
        write_bytes(dir + "/A.out.IMG", card.data() + at[0], a.size());
        if (sd_drive)
            write_bytes(dir + "/T.out.IMG", card.data() + at[1], t.size());
        else
        {
            std::vector<uint8_t> m(MDISK_SIZE);
            for (uint32_t i = 0; i < MDISK_SIZE; ++i)
                m[i] = rig.phys(MDISK + i);
            write_bytes(dir + "/T.out.IMG", m.data(), m.size());
        }
        return rig.out.substr(from);
    }

    bool env_on(const char *name, bool fallback)
    {
        const char *v = std::getenv(name);
        return v && *v ? std::string(v) != "0" : fallback;
    }

    /// Seeds as OODOS_MODEL_SEEDS takes them: 7 or 7-60, comma-separated.
    /// @return whether `text` is that; the seeds in `seeds`
    bool parse_seeds(const std::string &text, std::vector<unsigned> &seeds)
    {
        seeds.clear();
        std::istringstream in(text);
        std::string v;
        while (std::getline(in, v, ','))
        {
            const size_t dash = v.find('-');
            const std::string lo = v.substr(0, dash), hi = dash == std::string::npos ? lo : v.substr(dash + 1);
            if (lo.empty() || hi.empty() || lo.size() > 6 || hi.size() > 6 ||
                lo.find_first_not_of("0123456789") != std::string::npos ||
                hi.find_first_not_of("0123456789") != std::string::npos || std::stoul(lo) > std::stoul(hi))
                return false;
            for (unsigned n = static_cast<unsigned>(std::stoul(lo)); n <= std::stoul(hi); ++n)
                seeds.push_back(n);
        }
        return !seeds.empty();
    }

    /// The environment's settings that reach modeltest.py's command line: only the values it takes, so
    /// nothing else gets into the shell's. @return "" or what's wrong
    std::string bad_settings()
    {
        auto value = [](const char *name) {
            const char *v = std::getenv(name);
            return std::string(v ? v : "");
        };
        const std::pair<const char *, std::vector<std::string>> choices[] = {
            {"OODOS_MODEL_COMPARE", {"renumber", "exact"}}, {"OODOS_MODEL_ALLOC", {"lowest", "nearest"}}};
        for (const auto &[name, allowed] : choices)
        {
            const std::string v = value(name);
            if (!v.empty() && std::find(allowed.begin(), allowed.end(), v) == allowed.end())
                return std::string(name) + "=" + v + ": not " + allowed[0] + " or " + allowed[1];
        }
        const std::string cut = value("OODOS_MODEL_CUT");
        if (!cut.empty() && (cut.size() > 5 || cut.find_first_not_of("0123456789") != std::string::npos))
            return "OODOS_MODEL_CUT=" + cut + ": not a number of operations";
        std::vector<unsigned> seeds;
        const std::string list = value("OODOS_MODEL_SEEDS");
        if (!list.empty() && !parse_seeds(list, seeds))
            return "OODOS_MODEL_SEEDS=" + list + ": not seeds (7 or 7-60, comma-separated)";
        return "";
    }

    /// OODOS's BDOS against the model, seed by seed: OODOS_MODEL_SEEDS, or modeltest.py's default seeds
    /// (between them every edge test_model.py checks: a full disk, a full directory on M: and on B:, a
    /// read-only drive's X 2).
    void compare_with_oodos()
    {
        std::vector<unsigned> seeds;
        std::string out;
        if (const char *s = std::getenv("OODOS_MODEL_SEEDS"); s && *s)
            parse_seeds(s, seeds);
        else
        {
            const bool listed = modeltest("seeds", out) == 0 && parse_seeds(out.substr(0, out.find('\n')), seeds);
            if (!listed)
                print_lines(out);
            REQUIRE(listed);
        }
        std::string extra;
        if (const char *c = std::getenv("OODOS_MODEL_COMPARE"); c && *c)
            extra += std::string(" --compare ") + c;
        if (const char *c = std::getenv("OODOS_MODEL_ALLOC"); c && *c)
            extra += std::string(" --alloc ") + c;
        if (env_on("OODOS_MODEL_LOOSE_X", false))
            extra += " --loose-x";
        for (const unsigned seed : seeds)
        {
            const std::string dir = seed_dir("oodos", seed);
            REQUIRE(prepare(seed, dir));
            const std::string said = run_on_oodos(dir, format_of(seed), ms(600'000));
            CHECK(said.find("DRIVER OK") != std::string::npos);
            const bool passed = check(dir, out, extra);
            CHECK(passed);
            print_lines(out);
        }
    }

    void comparison_skipped()
    {
        std::printf("  skipped: OODOS_MODEL_BDOS off (the CMake option, or the environment's =0): OODOS's BDOS "
                    "isn't compared with the model\n");
    }
} // namespace

TEST(driver_is_a_com_that_fits)
{
    const std::vector<uint8_t> com = read_bytes(DRIVER);
    REQUIRE(com.size() > 16);
    CHECK(std::memcmp(com.data(), "OOD\x1A", 4) == 0);
    CHECK_EQ(com[4], 0);                                     // no flags: a .COM
    CHECK_EQ(com[5], 3);                                     // linked at $0300
    const unsigned size = com[6] | com[7] << 8, bss = com[8] | com[9] << 8;
    CHECK_EQ(size_t{size}, com.size() - 16);
    CHECK(0x0300 + size + bss <= 0xBFFD);                    // the script and results buffers fit the TPA
}

TEST(the_model_as_the_bdos_agrees_with_itself)
{
    for (unsigned seed = 1; seed <= 9; ++seed)
    {
        const std::string dir = seed_dir("flat", seed);
        REQUIRE(prepare(seed, dir));
        std::string printed, out;
        CHECK(run_on_the_model(dir, "", printed));
        CHECK(printed.find("DRIVER OK") != std::string::npos);
        const bool passed = check(dir, out);
        CHECK(passed);
        if (!passed || seed == 1)
            print_lines(out);
        else
            std::printf("  %s", out.substr(out.rfind("seed")).c_str());
    }
}

TEST(a_wrong_bdos_is_caught)
{
    // Each fault on a seed whose script exercises it; check fails, and names the seed.
    const std::pair<const char *, unsigned> cases[] = {
        {"update", 1}, {"rc", 1}, {"code", 2}, {"data", 1}, {"a-attr", 1}};
    for (const auto &[fault, seed] : cases)
    {
        const std::string dir = seed_dir(std::string("fault-") + fault, seed);
        REQUIRE(prepare(seed, dir));
        std::string printed, out;
        CHECK(run_on_the_model(dir, fault, printed));
        CHECK(!check(dir, out));
        CHECK(out.find("seed " + std::to_string(seed) + ": op ") != std::string::npos ||
              out.find("seed " + std::to_string(seed) + ": M:") != std::string::npos ||
              out.find("seed " + std::to_string(seed) + ": B:") != std::string::npos ||
              out.find("seed " + std::to_string(seed) + ": A:") != std::string::npos);
        std::printf("  --fault %s: %s", fault, out.substr(0, out.find('\n') + 1).c_str());
    }
}

TEST(driver_runs_under_oodos)
{
    // DRIVER found on A:, run through OODOS's BDOS, its script read and run, and back at the prompt.
    const std::string dir = seed_dir("oodos-smoke", 3);
    REQUIRE(prepare(3, dir));
    const std::vector<uint8_t> a = read_bytes(dir + "/A.IMG"), t = read_bytes(dir + "/T.IMG");
    const std::string said = run_on_oodos(dir, format_of(3), ms(120'000));
    CHECK(said.find("DRIVER: the model test's script") != std::string::npos);
    CHECK(said.find("DRIVER OK") != std::string::npos);
    CHECK(said.find("\r\nA>") != std::string::npos);
    std::printf("  OODOS: %s\n", said.c_str());
    // What the harness takes off the card and out of banks C-D: A:'s SCRIPT.BIN and DRIVER.COM entries
    // (1, 2) and first block (8) as the model wrote them, and M:'s directory as cold boot made it, which
    // is the model's mkfs at the same minute.
    const std::vector<uint8_t> a_out = read_bytes(dir + "/A.out.IMG"), m = read_bytes(dir + "/T.IMG");
    REQUIRE(a_out.size() == a.size() && m.size() == MDISK_SIZE && t.size() == MDISK_SIZE);
    CHECK(std::equal(a.begin() + 32, a.begin() + 96, a_out.begin() + 32));
    CHECK(std::equal(a.begin() + 8 * 4096, a.begin() + 9 * 4096, a_out.begin() + 8 * 4096));
    CHECK(std::equal(t.begin(), t.begin() + 8192, m.begin()));
}

#if EMU_OODOS_MODEL_BDOS
constexpr bool MODEL_BDOS = true;
#else
constexpr bool MODEL_BDOS = false;
#endif

int main()
{
    if (const std::string bad = bad_settings(); !bad.empty())
    {
        std::printf("FAIL %s\n", bad.c_str());
        return 1;
    }
    const bool only = env_on("OODOS_MODEL_ONLY", false), compared = env_on("OODOS_MODEL_BDOS", MODEL_BDOS);
    if (only)
        ::ooe::test::registry().clear();                    // the comparison alone: seeds run in parallel by hand
    if (only && !compared)
    {
        std::printf("FAIL OODOS_MODEL_ONLY=1 runs the comparison alone, but OODOS_MODEL_BDOS is off: nothing to run\n");
        return 1;
    }
    static ::ooe::test::Registrar reg("oodos_matches_the_model", compared ? compare_with_oodos : comparison_skipped);
    if (::ooe::test::registry().empty())
    {
        std::printf("FAIL no cases to run\n");
        return 1;
    }
    const int rc = ::ooe::test::run_all();
    if (rc == 0 && !compared)
    {
        std::printf("skipped: OODOS_MODEL_BDOS off\n");
        return 77;                                           // SKIP_RETURN_CODE: ctest counts it skipped
    }
    return rc;
}
