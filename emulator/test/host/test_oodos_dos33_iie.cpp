/**
 * @file test_oodos_dos33_iie.cpp
 * @brief DOS 3.3 itself as DOS33.COM's other oracle (phase 6d, hw/oodos.md, question 10).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS's DOS33 PUTs a text file, a binary one, one of two track/sector lists, six more (the catalog's
 * second sector), and one it then ERAs, on a dos33.py image; the emulated IIe then boots the owner's DOS
 * 3.3 disk from fixtures/disks in drive 1, with that image in drive 2: CATALOG,D2 lists them, VERIFY
 * reads them, BLOAD puts the binaries' bytes at their addresses, a program READs the text; DOS WRITEs a
 * text file, LOCKs one and BSAVEs one; then DOS33 sees the lock, GETs DOS's files back byte for byte. Everything is on copies in memory; only
 * what the test computed is printed. Without the owner's IIe ROMs, PROMs or DOS disk it skips (77).
 */

#include "harness.h"
#include "oodos_rig.h"
#include "disk2_rig.h"

#include "session.h"
#include "usb_card.h"

#include <array>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;
using namespace disk2_rig;

namespace
{
    const std::string ROMS = EMU_FIXTURE_DIR "/apple2e/";
    const std::string IIE = ROMS + "iie-enhanced-c000-ffff.bin";
    const std::string CHARS = ROMS + "iie-video-enhanced-342-0265-a.bin";
    const std::string P5 = ROMS + "disk2-16-p5-boot-341-0027-a.bin";
    const std::string DOS_DSK = EMU_FIXTURE_DIR "/disks/The Oregon Trail.dsk"; // the owner's DOS 3.3 disk
    const std::string DOS33PY = EMU_OODOS_SRC "/tools/dos33.py";
    constexpr uint64_t FRAME = 17'030;

    bool have(const std::string &path) { return std::ifstream(path).good(); }
    std::string work()
    {
        static std::string w;
        if (w.empty())
        {
            w = std::string(EMU_DOS33_WORK) + "/iie-" + std::to_string(getpid());
            std::system(("mkdir -p '" + w + "'").c_str());
        }
        return w;
    }
    void save(const std::string &path, const std::vector<uint8_t> &b)
    {
        std::ofstream o(path, std::ios::binary);
        o.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    int py(const std::string &args, std::string *out = nullptr)
    {
        const std::string cmd = std::string(EMU_PYTHON) + " -I '" + DOS33PY + "' " + args + " 2>&1";
        FILE *p = popen(cmd.c_str(), "r");
        if (!p)
            return -1;
        std::string o;
        std::array<char, 4096> buf{};
        size_t n = 0;
        while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0)
            o.append(buf.data(), n);
        const int st = pclose(p);
        if (out)
            *out = o;
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
    std::string check(const std::vector<uint8_t> &d)
    {
        save(work() + "/ck.dsk", d);
        std::string o;
        py("check '" + work() + "/ck.dsk'", &o);
        return o;
    }

    /// OODOS with DOS33 on M:, its files there too, and C: a dos33.py image.
    struct Dd : CcpRig
    {
        explicit Dd(std::vector<MFile> files = {}) : CcpRig({}, {}, P6)
        {
            if (!ok)
                return;
            put_m(files);
            put_util("dos33", UTILS);
            chain({13, 0});
            ok = mount(UNIT, "/DISKS/DOS33.DSK") == 0;
        }
        void put_disk(const std::vector<uint8_t> &img)
        {
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
            {
                sd().write(SdCard::UNIT, static_cast<uint8_t>(UNIT));
                for (unsigned i = 0; i < 4; ++i)
                    sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(s >> (8 * i)));
                sd().write(SdCard::COUNT, 1);
                sd().write(SdCard::COUNT + 1, 0);
                sd().write(SdCard::CMD, SdCard::WRITE);
                for (size_t i = 0; i < SECTOR; ++i)
                    sd().write(SdCard::DATA, img[s * SECTOR + i]);
                run(ms(2));
                sd().write(SdCard::UNIT, 0);
            }
        }
        std::vector<uint8_t> unit_image()
        {
            std::vector<uint8_t> d;
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
            {
                const std::vector<uint8_t> b = read_unit(UNIT, s);
                d.insert(d.end(), b.begin(), b.end());
            }
            return d;
        }
        std::string c(const std::string &line) { return to(line, "\r\nA>", ms(60000)); }
    };

    /// The IIe with the owner's DOS 3.3 disk in drive 1 and `d2` in drive 2, at DOS's prompt.
    struct IIe
    {
        explicit IIe(const std::vector<uint8_t> &d2)
        {
            std::string error;
            Session::Config c{EMU_MACHINE_DIR "/backplane.machine", EMU_BIOS_ROM, ""};
            c.apple_rom = {IIE};
            c.apple_chars = CHARS;
            c.disk2_boot = P5;
            c.disk2_sequencer = P6;
            s = Session::create(c, error);
            if (!s || !s->insert_disk(0, read_bytes(DOS_DSK), DOS_DSK, error) ||
                !s->insert_disk(1, d2, "d2.dsk", error))
            {
                std::printf("  %s\n", error.c_str());
                s.reset();
                return;
            }
            run(1'500'000);
            s->take_output();
            s->send("C806R\r");
            for (int i = 0; i < 400 && !s->fixed_hz(); ++i)
                s->run(FRAME);
            // DOS boots and its HELLO runs on: let it, then Ctrl-Reset, which DOS takes to its prompt
            for (int i = 0; i < 40 && (i < 8 || s->disk2()->status().spinning); ++i)
                run(60 * FRAME);
            chord(0xE0, 0x4C);
            run(60 * FRAME);
            type("HOME\r");
        }
        bool ok() const { return s && s->fixed_hz() != 0; }
        void run(uint64_t cycles) { s->run(cycles); }
        std::string row(unsigned r) const
        {
            std::string t;
            const uint32_t base = 0x0400 + (r & 7) * 0x80 + (r >> 3) * 0x28;
            for (unsigned c = 0; c < 40; ++c)
            {
                uint8_t b = s->peek_physical(base + c) & 0x7F;
                if (b < 0x20)
                    b = static_cast<uint8_t>(b | 0x40);
                t += static_cast<char>(b);
            }
            return t.substr(0, t.find_last_not_of(' ') + 1);
        }
        std::string screen() const
        {
            std::string t;
            for (unsigned r = 0; r < 24; ++r)
                t += row(r) + "\n";
            return t;
        }
        bool screen_has(const std::string &text) const { return screen().find(text) != std::string::npos; }
        void type(const std::string &text)
        {
            UsbCard *usb = s->usb();
            for (const char ch : text)
            {
                uint8_t usage = 0;
                bool shift = false;
                static const std::string digits = "1234567890", shifted = "!@#$%^&*()";
                if (ch >= 'A' && ch <= 'Z')
                    usage = static_cast<uint8_t>(0x04 + ch - 'A');
                else if (digits.find(ch) != std::string::npos)
                    usage = static_cast<uint8_t>(0x1E + digits.find(ch));
                else if (shifted.find(ch) != std::string::npos)
                    usage = static_cast<uint8_t>(0x1E + shifted.find(ch)), shift = true;
                else if (ch == ' ')
                    usage = 0x2C;
                else if (ch == '\r')
                    usage = 0x28;
                else if (ch == '+')
                    usage = 0x2E, shift = true;
                else if (ch == '=')
                    usage = 0x2E;
                else if (ch == ',')
                    usage = 0x36;
                else if (ch == '"')
                    usage = 0x34, shift = true;
                else if (ch == ':')
                    usage = 0x33, shift = true;
                else if (ch == ';')
                    usage = 0x33;
                if (shift)
                    usb->queue_key(0xE1, true);
                usb->queue_key(usage, true);
                run(3 * FRAME);
                usb->queue_key(usage, false);
                if (shift)
                    usb->queue_key(0xE1, false);
                run(3 * FRAME);
            }
            run(30 * FRAME);
        }
        /// A DOS command, then the drives left to stop.
        void dos(const std::string &line)
        {
            type(line + "\r");
            for (int i = 0; i < 30 && (i < 2 || s->disk2()->status().spinning); ++i)
                run(60 * FRAME);
        }
        void chord(uint8_t mod, uint8_t key)
        {
            UsbCard *usb = s->usb();
            usb->queue_key(mod, true);
            usb->queue_key(key, true);
            run(3 * FRAME);
            usb->queue_key(key, false);
            run(3 * FRAME);
            usb->queue_key(mod, false);
            run(3 * FRAME);
        }
        std::vector<uint8_t> d2()
        {
            std::vector<uint8_t> img;
            std::string error;
            if (!s->disk(1) || !s->disk(1)->to_sectors(DiskImage::Format::Dos, img, error))
                return {};
            return img;
        }
        std::unique_ptr<Session> s;
    };

    std::vector<uint8_t> text(const std::string &lines)
    {
        std::vector<uint8_t> t;
        for (const char c : lines)
        {
            if (c == '\n')
                t.push_back(0x0D);
            t.push_back(static_cast<uint8_t>(c));
        }
        return records(t, 0x1A);
    }
} // namespace

TEST(dos_3_3_reads_what_dos33_wrote_and_dos33_reads_what_dos_wrote)
{
    // OODOS: an empty dos33.py image on C:, DOS33 PUTs NOTE (T, three lines), CODE (B=4000, 700 bytes),
    // BIG (B=0800, 32,000 bytes: 126 sectors, 2 lists, the second starting at 122), F0-F5, and GONE,
    // which it then ERAs: 10 entries, so the catalog's second sector holds the last.
    const std::vector<uint8_t> note = text("LINE ONE\nLINE TWO\nTHE END\n"), code = gen(700, 11),
                               big = gen(32000, 12);
    std::vector<uint8_t> img;
    {
        Dd r({{0, "NOTE    TXT", note}, {0, "CODE    BIN", code}, {0, "BIG     BIN", big}, {0, "SMALL   BIN", gen(100, 3)}});
        REQUIRE(r.ok);
        REQUIRE(py("new '" + work() + "/e.dsk'") == 0);
        r.put_disk(read_bytes(work() + "/e.dsk"));
        has(r.c("dos33 put m:note.txt c:NOTE"), "A>");
        has(r.c("dos33 put m:code.bin c:CODE B=4000"), "A>");
        has(r.c("dos33 put m:big.bin c:BIG B=0800"), "A>");
        has(r.c("dos33 put m:small.bin c:GONE"), "A>");
        for (int i = 0; i < 6; ++i)
            has(r.c("dos33 put m:small.bin c:F" + std::to_string(i)), "A>");
        has(r.c("dos33 era c:GONE"), "A>");
        img = r.unit_image();
        REQUIRE(img.size() == IMAGE);
    }
    CHECK(check(img) == "ok\n");

    // DOS 3.3: the catalog, VERIFY, BLOAD at its address, a program that READs the text.
    IIe e(img);
    REQUIRE(e.ok());
    e.dos("CATALOG,D2");
    const std::string cat = e.screen();
    std::printf("  CATALOG,D2 shows NOTE: %d, CODE: %d\n", cat.find(" T 002 NOTE") != std::string::npos,
                cat.find(" B 005 CODE") != std::string::npos);
    CHECK(cat.find(" T 002 NOTE") != std::string::npos);
    CHECK(cat.find(" B 005 CODE") != std::string::npos);
    CHECK(cat.find(" B 128 BIG") != std::string::npos);
    CHECK(cat.find(" B 002 F5") != std::string::npos);   // the catalog's second sector
    CHECK(cat.find("GONE") == std::string::npos);
    const std::vector<std::string> names = {"NOTE", "CODE", "BIG", "F0", "F1", "F2", "F3", "F4", "F5"};
    auto verify = [&](const char *when) {
        e.type("HOME\r");
        for (const std::string &n : names)
            e.dos("VERIFY " + n + ",D2");
        const std::string ver = e.screen();
        const bool clean = ver.find("ERROR") == std::string::npos && ver.find("NOT FOUND") == std::string::npos;
        std::printf("  VERIFY of the nine %s: %s\n", when, clean ? "no error" : "an error");
        CHECK(clean);
    };
    verify("DOS33 put");
    e.dos("BLOAD CODE,D2");
    std::vector<uint8_t> loaded;
    for (uint32_t a = 0; a < 768; ++a)
        loaded.push_back(e.s->peek_physical(0x4000 + a));
    const std::vector<uint8_t> want = records(code, 0x1A);
    std::printf("  BLOAD CODE: %s\n", loaded == want ? "the bytes DOS33 put" : "other bytes");
    CHECK(loaded == want);

    e.type("HOME\r");
    e.type("NEW\r");
    e.type("10 D$=CHR$(4)\r");
    e.type("20 PRINT D$;\"OPEN NOTE,D2\"\r");
    e.type("30 PRINT D$;\"READ NOTE\"\r");
    e.type("40 INPUT A$\r");
    e.type("50 INPUT B$\r");
    e.type("60 PRINT D$;\"CLOSE\"\r");
    e.type("70 PRINT \"GOT \";A$;\",\";B$\r");
    e.type("HOME\r");
    e.dos("RUN");
    const bool read = e.screen_has("GOT LINE ONE,LINE TWO");
    std::printf("  READ NOTE: %s\n", read ? "its two lines" : "not its lines");
    CHECK(read);

    // DOS 3.3 writes: BSAVE 256 bytes 0-255 as OOB; CODE must be as it was (its sectors were taken in
    // the bitmap, so DOS didn't reuse them).
    e.type("HOME\r");
    e.type("FOR I=0 TO 255:POKE 24576+I,I:NEXT\r");
    e.run(120 * FRAME);                        // the loop's run, before the next line is typed
    e.dos("BSAVE OOB,A24576,L256,D2");
    e.dos("LOCK F0,D2");
    e.type("HOME\r");
    e.type("NEW\r");
    e.type("10 D$=CHR$(4)\r");
    e.type("20 PRINT D$;\"OPEN T2,D2\"\r");
    e.type("30 PRINT D$;\"WRITE T2\"\r");
    e.type("40 PRINT \"HELLO FROM DOS\"\r");
    e.type("50 PRINT \"SECOND LINE\"\r");
    e.type("60 PRINT D$;\"CLOSE\"\r");
    e.type("HOME\r");
    e.dos("RUN");
    verify("DOS's BSAVE and WRITE");
    e.dos("BLOAD CODE,A20480,D2");             // at $5000 this time
    loaded.clear();
    for (uint32_t a = 0; a < 768; ++a)
        loaded.push_back(e.s->peek_physical(0x5000 + a));
    std::printf("  BLOAD CODE after BSAVE: %s\n", loaded == want ? "the bytes DOS33 put" : "other bytes");
    CHECK(loaded == want);
    e.dos("BLOAD BIG,D2");                     // at $0800 (over BASIC's program, so last): its 32,000
    std::vector<uint8_t> bl;                   // bytes, through both lists
    for (uint32_t a = 0; a < 32000; ++a)
        bl.push_back(e.s->peek_physical(0x0800 + a));
    std::printf("  BLOAD BIG: %s\n", bl == big ? "the bytes DOS33 put" : "other bytes");
    CHECK(bl == big);
    const std::vector<uint8_t> after = e.d2();
    REQUIRE(after.size() == IMAGE);
    const std::string ck = check(after);
    std::printf("  dos33.py check after DOS's BSAVE: %s", ck.c_str());
    {
        save(work() + "/after.dsk", after);
        std::string l;
        py("ls '" + work() + "/after.dsk'", &l);
        std::printf("  its catalog by dos33.py:\n%s", l.c_str());
    }
    CHECK(ck == "ok\n");

    // OODOS again: DOS33 GET takes DOS's file back, its 256 bytes.
    Dd r;
    REQUIRE(r.ok);
    r.put_disk(after);
    const std::string o = r.c("dos33 get c:OOB m:oob.bin");
    has(o, "Address $6000, 256 bytes");
    std::vector<uint8_t> bytes(256);
    for (unsigned i = 0; i < 256; ++i)
        bytes[i] = static_cast<uint8_t>(i);
    CHECK(m_data(r, 0, "OOB     BIN") == bytes);
    // DOS's text file as T, and its lock as DOS33 sees it.
    has(r.c("dos33 get c:T2 m:t2.txt"), "A>");
    const std::vector<uint8_t> t2 = m_data(r, 0, "T2      TXT");
    const bool same = t2 == text("HELLO FROM DOS\nSECOND LINE\n");
    std::printf("  DOS's WRITE back by GET T: %s\n", same ? "its two lines" : "other bytes");
    CHECK(same);
    has(r.c("dos33 c:"), "*B 002 F0");
    has(r.c("dos33 era c:F0"), "File locked.");
}

TEST(a_random_access_file_dos_wrote_far_out)
{
    // DOS 3.3 OPENs RND with records of 250 and WRITEs record 700 (byte 175,000: the file's sector 683,
    // in its sixth list, the five before it empty), and RNDB with records of 100, record 100. DOS33 takes
    // RNDB's sectors (R: zeros, then NEAR at byte 10,000) and ERAs both (the review's R2F1: Bad file,
    // for want of more than five lists); then DOS itself saves and verifies a file on that disk.
    REQUIRE(py("new '" + work() + "/r.dsk'") == 0);
    std::vector<uint8_t> img;
    {
        IIe e(read_bytes(work() + "/r.dsk"));
        REQUIRE(e.ok());
        e.type("NEW\r");
        e.type("10 D$=CHR$(4)\r");
        e.type("20 PRINT D$;\"OPEN RND,L250,D2\"\r");
        e.type("30 PRINT D$;\"WRITE RND,R700\"\r");
        e.type("40 PRINT \"FAR\"\r");
        e.type("50 PRINT D$;\"CLOSE\"\r");
        e.type("60 PRINT D$;\"OPEN RNDB,L100,D2\"\r");
        e.type("70 PRINT D$;\"WRITE RNDB,R100\"\r");
        e.type("80 PRINT \"NEAR\"\r");
        e.type("90 PRINT D$;\"CLOSE\"\r");
        e.type("HOME\r");
        e.dos("RUN");
        e.run(600 * FRAME);
        img = e.d2();
        REQUIRE(img.size() == IMAGE);
    }
    CHECK(check(img) == "ok\n");
    Dd r;
    REQUIRE(r.ok);
    r.put_disk(img);
    const std::string cat = r.c("dos33 c:");
    has(cat, " T 007 RND");
    has(cat, " T 002 RNDB");
    has(r.c("dos33 get c:RNDB m:rndb.r R"), "A>");
    const std::vector<uint8_t> got = m_data(r, 0, "RNDB    R  ");
    const std::vector<uint8_t> near = {'N' | 0x80, 'E' | 0x80, 'A' | 0x80, 'R' | 0x80, 0x8D};
    const bool ok = got.size() == 40u * 256 && std::equal(near.begin(), near.end(), got.begin() + 10000);
    std::printf("  GET RNDB R: %zu bytes, NEAR at 10,000: %d\n", got.size(), ok);
    CHECK(ok);
    const std::string era = r.c("dos33 era c:RND");
    const std::string erb = r.c("dos33 era c:RNDB");
    CHECK(era.find("Bad") == std::string::npos && erb.find("Bad") == std::string::npos);
    const std::vector<uint8_t> after = r.unit_image();
    const std::string ck = check(after);
    std::printf("  dos33.py check after DOS33's ERAs: %s", ck.c_str());
    CHECK(ck == "ok\n");
    has(r.c("dos33 c:"), "496 sectors free");

    IIe e(after);
    REQUIRE(e.ok());
    e.dos("CATALOG,D2");
    CHECK(!e.screen_has("RND"));
    e.type("HOME\r");
    e.type("FOR I=0 TO 255:POKE 24576+I,I:NEXT\r");
    e.run(120 * FRAME);
    e.dos("BSAVE OOB,A24576,L256,D2");
    e.dos("VERIFY OOB,D2");
    const bool clean = !e.screen_has("ERROR") && !e.screen_has("NOT FOUND");
    std::printf("  DOS's BSAVE and VERIFY after DOS33's ERAs: %s\n", clean ? "no error" : "an error");
    CHECK(clean);
    CHECK(check(e.d2()) == "ok\n");
}

int main()
{
    for (const std::string &f : {IIE, CHARS, P5, P6, DOS_DSK})
        if (!have(f))
        {
            std::printf("  SKIP: the owner's IIe ROMs, Disk II PROMs and DOS 3.3 disk are needed (fixtures)\n");
            return 77;
        }
    return run_all();
}
