/**
 * @file test_oodos_dos33.cpp
 * @brief DOS33.COM and function 76 (phase 6d, hw/oodos.md "The DOS 3.3 exchange utility (6d)").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * DOS 3.3 images made by oodos/tools/dos33.py (ours), on an SD unit (C:, the card's /DISKS/DOS33.DSK
 * mounted as unit 3 and its sectors replaced) and in the Disk II's drive 1 (O:). DOS33's every form and
 * message, its files compared byte for byte with dos33.py's, and dos33.py's check of every image after.
 * Function 76 through MEMTEST's S, L and J. The owner's sequencer PROM from the fixtures, for O: (77
 * without it); nothing of it is printed.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "disk2_rig.h"

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
    const std::string DOS33PY = EMU_OODOS_SRC "/tools/dos33.py";
    std::string work()
    {
        static std::string w;
        if (w.empty())
        {
            w = std::string(EMU_DOS33_WORK) + "/" + std::to_string(getpid());
            std::system(("mkdir -p '" + w + "'").c_str());
        }
        return w;
    }
    void save(const std::string &path, const std::vector<uint8_t> &b)
    {
        std::ofstream o(path, std::ios::binary);
        o.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    /// dos33.py with `args` (paths in work()); its output in `out`. @return its exit status
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

    struct AFile
    {
        std::string name;
        char type;                 ///< T, B, A (raw), R
        std::vector<uint8_t> data; ///< T: CP/M text; B: the bytes; others: as stored
        unsigned address = 0x2000;
        bool locked = false;
    };
    /// A DOS 3.3 image by dos33.py with `files`.
    std::vector<uint8_t> image(const std::vector<AFile> &files)
    {
        const std::string img = work() + "/img.dsk", f = work() + "/f.bin";
        if (py("new '" + img + "'") != 0)
        {
            CHECK(false);
            return {};
        }
        for (const AFile &a : files)
        {
            save(f, a.data);
            std::string args = "put '" + img + "' '" + a.name + "' " + a.type + " '" + f + "'";
            if (a.type == 'B')
                args += " " + std::to_string(a.address);
            std::string o;
            const int st = py(args, &o);
            CHECK(st == 0);
            if (st)
                std::printf("  dos33.py put: %s\n", o.c_str());
            if (a.locked)
                CHECK(py("lock '" + img + "' '" + a.name + "'") == 0);
        }
        return read_bytes(img);
    }
    std::string ls(const std::vector<uint8_t> &d)
    {
        save(work() + "/ls.dsk", d);
        std::string o;
        py("ls '" + work() + "/ls.dsk'", &o);
        return o;
    }
    /// dos33.py's check of the image: "ok", or what's wrong.
    std::string check(const std::vector<uint8_t> &d)
    {
        save(work() + "/ck.dsk", d);
        std::string o;
        py("check '" + work() + "/ck.dsk'", &o);
        return o;
    }
    void checked(const std::vector<uint8_t> &d)
    {
        const std::string c = check(d);
        CHECK(c == "ok\n");
        if (c != "ok\n")
            std::printf("  dos33.py check: %s\n", c.c_str());
    }
    /// The file's sectors as stored (dos33.py get), empty if it isn't there.
    std::vector<uint8_t> stored(const std::vector<uint8_t> &d, const std::string &name)
    {
        save(work() + "/g.dsk", d);
        std::remove((work() + "/g.bin").c_str());
        if (py("get '" + work() + "/g.dsk' '" + name + "' '" + work() + "/g.bin'") != 0)
            return {};
        return read_bytes(work() + "/g.bin");
    }
    /// CP/M text as DOS keeps it, and back: dos33.py's rules, here again.
    std::vector<uint8_t> to_dos_text(const std::vector<uint8_t> &t)
    {
        std::vector<uint8_t> o;
        for (uint8_t b : t)
        {
            if (b == 0x1A)
                break;
            if (b != 0x0A)
                o.push_back(static_cast<uint8_t>(b | 0x80));
        }
        o.push_back(0);
        return o;
    }
    std::vector<uint8_t> from_dos_text(const std::vector<uint8_t> &s)
    {
        std::vector<uint8_t> o;
        for (uint8_t b : s)
        {
            b &= 0x7F;
            if (!b)
                break;
            o.push_back(b);
            if (b == 0x0D)
                o.push_back(0x0A);
        }
        return o;
    }
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
    std::vector<std::string> lines_of(const std::string &s)
    {
        std::vector<std::string> v;
        std::string l;
        for (const char c : s)
        {
            if (c == '\n' || c == '\r')
            {
                if (!l.empty())
                    v.push_back(l);
                l.clear();
            }
            else
                l += c;
        }
        if (!l.empty())
            v.push_back(l);
        return v;
    }

    /// The rig: DOS33.COM (and PIP) on M:, M: searched first; the card's DOS33.DSK as unit 3 (C:).
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
        void write_unit(uint32_t lba, const std::vector<uint8_t> &s)
        {
            sd().write(SdCard::UNIT, static_cast<uint8_t>(UNIT));
            for (unsigned i = 0; i < 4; ++i)
                sd().write(static_cast<uint8_t>(SdCard::LBA + i), static_cast<uint8_t>(lba >> (8 * i)));
            sd().write(SdCard::COUNT, 1);
            sd().write(SdCard::COUNT + 1, 0);
            sd().write(SdCard::CMD, SdCard::WRITE);
            for (size_t i = 0; i < SECTOR; ++i)
                sd().write(SdCard::DATA, s[i]);
            run(ms(2));
            sd().write(SdCard::UNIT, 0);
        }
        /// C:'s unit made `img` (before OODOS has read it).
        void put_disk(const std::vector<uint8_t> &img)
        {
            REQUIRE(img.size() == IMAGE);
            for (uint32_t s = 0; s < IMAGE / SECTOR; ++s)
                write_unit(s, std::vector<uint8_t>(img.begin() + s * SECTOR, img.begin() + (s + 1) * SECTOR));
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
        Disk2Card &card() { return *machine->disk2_card(); }
        void insert(unsigned drive, const std::vector<uint8_t> &image)
        {
            card().insert(drive, std::shared_ptr<DiskImage>(DiskImage::from_sectors(image, DiskImage::Format::Dos)));
        }
        std::vector<uint8_t> drive_image(unsigned drive)
        {
            std::vector<uint8_t> img;
            std::string error;
            if (!card().disk(drive) || !card().disk(drive)->to_sectors(DiskImage::Format::Dos, img, error))
                return {};
            return img;
        }
        std::string c(const std::string &line) { return to(line, "\r\nA>", ms(60000)); }
    };

    const std::vector<uint8_t> README_T = text("THIS IS A TEXT FILE.\nIT HAS TWO LINES.\n");
    std::vector<AFile> sample()
    {
        return {{"README", 'T', README_T},
                {"MY PROGRAM", 'B', gen(1000, 3), 0x6000},
                {"HELLO", 'A', gen(300, 5)},
                {"KEEP", 'B', gen(40, 9), 0x0800, true}};
    }
    /// No message of DOS33's in `o`: done quietly.
    void quiet(const std::string &o)
    {
        for (const char *m : {"Use:", "Not ", "Disk ", "File ", "Catalog", "Bad name", "No File", "CP/M"})
            hasnt(o, m);
    }
    std::string run_memtest(Dd &r, const std::string &tail, const std::string &prompt = "",
                            const std::function<void()> &then = {})
    {
        const size_t from = start(r, "memtest.com", tail);
        if (!prompt.empty())
        {
            if (!r.until(prompt, ms(30000), from))
                return r.out.substr(from);
            then();
            r.type(" ");
        }
        r.until("MEMTEST DONE", ms(60000), from);
        r.until("\r\nA>", ms(60000), from);
        return r.out.substr(from);
    }
} // namespace

TEST(the_card_s_dos33_dsk_is_ours_and_holds_no_dos)
{
    // /DISKS/DOS33.DSK, made by dos33.py on the card: a DOS 3.3 disk dos33.py checks clean, its README,
    // and nothing on tracks 0-2, where DOS 3.3's own disks keep DOS: so it holds no Apple bytes.
    Dd r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> d = r.unit_image();
    REQUIRE(d.size() == IMAGE);
    size_t nonzero = 0;
    for (size_t i = 0; i < 3 * 16 * 256; ++i)
        nonzero += d[i] != 0;
    CHECK(nonzero == 0u);
    checked(d);
    has(ls(d), " T 002 README");
}

TEST(the_catalog_as_dos33_py_lists_it)
{
    // DOS33 C:: the volume, then each file as dos33.py's ls has it (the lock, the type, the sectors, the
    // name), then the free sectors: the same lines, in the same order.
    Dd r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> img = image(sample());
    r.put_disk(img);
    const std::string o = r.c("dos33 c:");
    std::printf("  %s\n", vis(o).c_str());
    has(o, "C: DOS 3.3 disk, volume 254\r\n");
    const std::vector<std::string> want = lines_of(ls(img)), got = lines_of(o);
    size_t at = 0;
    for (const std::string &w : want)
    {
        while (at < got.size() && got[at] != w)
            ++at;
        CHECK(at < got.size());
        if (at == got.size())
            std::printf("  missing '%s'\n", w.c_str());
    }
    has(o, "*B 002 KEEP");
    has(o, " B 005 MY PROGRAM");
}

TEST(get_t_b_and_r_byte_for_byte)
{
    // GET in each mode, the CP/M file against dos33.py's sectors: T with bit 7 cleared, CR as CR LF,
    // to its 0, ^Z after; B the length's bytes, the address said; R every sector as stored; the default
    // by the type (A: R).
    Dd r;
    REQUIRE(r.ok);
    const std::vector<AFile> files = sample();
    const std::vector<uint8_t> img = image(files);
    r.put_disk(img);
    std::string o = r.c("dos33 get c:README m:readme.txt");
    quiet(o);
    std::vector<uint8_t> want = from_dos_text(stored(img, "README"));
    std::vector<uint8_t> got = m_data(r, 0, "README  TXT");
    CHECK(records(want, 0x1A) == got);
    CHECK(want == std::vector<uint8_t>(README_T.begin(), README_T.begin() + static_cast<long>(want.size())));

    o = r.c("dos33 get c:\"MY PROGRAM\" m:prog.bin");
    has(o, "Address $6000, 1000 bytes");
    CHECK(m_data(r, 0, "PROG    BIN") == records(gen(1000, 3)));

    o = r.c("dos33 get c:HELLO m:hello.raw");
    const std::vector<uint8_t> raw = stored(img, "HELLO");
    CHECK(raw.size() == 512u);
    CHECK(m_data(r, 0, "HELLO   RAW") == raw);

    o = r.c("dos33 get c:\"MY PROGRAM\" m:prog.r R");
    CHECK(m_data(r, 0, "PROG    R  ") == stored(img, "MY PROGRAM"));
    o = r.c("dos33 get c:README m:readme.r R");
    CHECK(m_data(r, 0, "README  R  ") == stored(img, "README"));
    checked(r.unit_image());
}

TEST(put_t_and_b_as_dos33_py_reads_them)
{
    // PUT a text file (T by its .TXT: bit 7 set, LF dropped, a 0 after) and a binary one (B=4000, the
    // address and length first), one under a name with blanks in quotes; dos33.py lists them, takes the
    // same sectors out, and checks the disk: every sector one owner's, the bitmap right.
    const std::vector<uint8_t> t = text("LINE ONE\nLINE TWO\nTHE END\n"), b = gen(700, 11);
    Dd r({{0, "NOTE    TXT", t}, {0, "CODE    BIN", b}});
    REQUIRE(r.ok);
    r.put_disk(image({}));
    std::string o = r.c("dos33 put m:note.txt c:NOTE");
    quiet(o);
    o = r.c("dos33 put m:code.bin c:\"MACHINE CODE\" B=4000");
    quiet(o);
    const std::vector<uint8_t> d = r.unit_image();
    const std::string l = ls(d);
    std::printf("  %s\n", vis(l).c_str());
    has(l, " T 002 NOTE");
    has(l, " B 005 MACHINE CODE");
    std::vector<uint8_t> want = to_dos_text(t);
    want.resize(256, 0);
    CHECK(stored(d, "NOTE") == want);
    std::vector<uint8_t> bw = {0x00, 0x40, 0x00, 0x03}; // $4000, 768: the records' 6 x 128
    std::vector<uint8_t> bb = records(b, 0x1A); // M:'s file, its last record ^Z-padded
    bw.insert(bw.end(), bb.begin(), bb.end());
    bw.resize(1024, 0);
    {
        const std::vector<uint8_t> got = stored(d, "MACHINE CODE");
        CHECK(got == bw);
        if (got != bw)
        {
            size_t k = 0;
            while (k < got.size() && k < bw.size() && got[k] == bw[k])
                ++k;
            std::printf("  MACHINE CODE: %zu bytes, %zu wanted; first difference at %zu\n", got.size(), bw.size(), k);
        }
    }
    checked(d);
    // and back by GET: the same bytes
    r.c("dos33 get c:\"MACHINE CODE\" m:back.bin");
    CHECK(m_data(r, 0, "BACK    BIN") == records(b, 0x1A));
}

TEST(files_of_more_than_one_track_sector_list)
{
    // A B file of 32,000 bytes (250 records: 126 sectors of data, 2 lists) PUT and GET back; an R file of
    // 300 sectors (3 lists) dos33.py made, GET as R; dos33.py reads the first and checks the disk.
    const std::vector<uint8_t> b = gen(32000, 6);
    Dd r({{0, "BIG     BIN", b}});
    REQUIRE(r.ok);
    std::vector<uint8_t> three;
    for (unsigned i = 0; i < 300 * 256; ++i)
        three.push_back(static_cast<uint8_t>(i * 13 + i / 256));
    r.put_disk(image({{"THREE", 'R', three}}));
    quiet(r.c("dos33 put m:big.bin c:BIG B=0800"));
    quiet(r.c("dos33 get c:BIG m:back.bin"));
    CHECK(m_data(r, 0, "BACK    BIN") == records(b, 0x1A));
    r.c("era m:back.bin");                // room on M: for 75 KB
    quiet(r.c("dos33 get c:THREE m:three.r"));
    CHECK(m_data(r, 0, "THREE   R  ") == three);
    const std::vector<uint8_t> d = r.unit_image();
    has(ls(d), " B 128 BIG");
    has(ls(d), " R 303 THREE");
    std::vector<uint8_t> bw = {0x00, 0x08, 0x00, 0x7D}; // $0800, 250 x 128 = 32,000
    const std::vector<uint8_t> bb = records(b, 0x1A);
    bw.insert(bw.end(), bb.begin(), bb.end());
    bw.resize(126 * 256, 0);
    CHECK(stored(d, "BIG") == bw);
    checked(d);
}

TEST(era_frees_the_file_s_sectors)
{
    Dd r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> img = image(sample());
    r.put_disk(img);
    std::string o = r.c("dos33 era c:\"MY PROGRAM\"");
    quiet(o);
    o = r.c("dos33 era c:KEEP");
    has(o, "File locked.");
    o = r.c("dos33 era c:GONE");
    has(o, "File not found.");
    const std::vector<uint8_t> d = r.unit_image();
    const std::string l = ls(d);
    hasnt(l, "MY PROGRAM");
    has(l, "KEEP");
    checked(d);
    const std::string before = ls(img);
    auto free_of = [](const std::string &s) { return std::stoi(s.substr(s.rfind('\n', s.size() - 2) + 1)); };
    CHECK(free_of(l) == free_of(before) + 5);
}

TEST(refusals_and_their_messages)
{
    Dd r({{0, "NOTE    TXT", text("X\n")}});
    REQUIRE(r.ok);
    r.put_disk(image(sample()));
    has(r.c("dos33"), "Use: DOS33 d:");
    has(r.c("dos33 c: x"), "Use: DOS33 d:");
    has(r.c("dos33 get c:README"), "Use: DOS33 d:");
    has(r.c("dos33 put m:note.txt c:README"), "File exists.");
    has(r.c("dos33 put m:none.txt c:NEWONE"), "No File");
    has(r.c("dos33 get c:NOSUCH m:x.txt"), "File not found.");
    has(r.c("dos33 get c:\"OPEN m:x.txt"), "Bad name.");
    has(r.c("dos33 era c:ABCDEFGHIJKLMNOPQRSTUVWXYZABCDE"), "Bad name.");
    has(r.c("dos33 put m:note.txt c:X Q"), "Use: DOS33 d:");
    has(r.c("dos33 m:"), "Not an Apple disk drive.");
    has(r.c("dos33 a:"), "Not an Apple disk drive.");
    checked(r.unit_image());
}

TEST(a_cp_m_disk_is_not_dos_3_3)
{
    Dd r;
    REQUIRE(r.ok);
    REQUIRE(r.mount(UNIT, "/DISKS/CPM.DSK") == 0);
    has(r.c("dos33 c:"), "Not a DOS 3.3 disk.");
}

TEST(a_full_disk_and_a_full_catalog)
{
    // A disk with 4 sectors free: a PUT of 6 (5 of data, a list) refused, Disk full, and nothing of it
    // in the catalog or the bitmap. The catalog: 7 files fill its first sector, the 8th goes into the next; with 105 (15
    // sectors' worth), Catalog full.
    Dd r({{0, "SIX     BIN", gen(1000, 1)}});
    REQUIRE(r.ok);
    std::vector<AFile> big = {{"FILLER", 'R', std::vector<uint8_t>(488 * 256, 0x55)}};
    const std::vector<uint8_t> img = image(big);
    REQUIRE(ls(img).find("\n4 sectors free") != std::string::npos);
    r.put_disk(img);
    has(r.c("dos33 put m:six.bin c:SIX"), "Disk full.");
    CHECK(ls(r.unit_image()) == ls(img));
    checked(r.unit_image());

    Dd s({{0, "EIGHT   BIN", gen(100, 2)}});
    REQUIRE(s.ok);
    std::vector<AFile> seven;
    for (int i = 0; i < 7; ++i)
        seven.push_back({"F" + std::to_string(i), 'B', gen(10, static_cast<unsigned>(i))});
    s.put_disk(image(seven));
    quiet(s.c("dos33 put m:eight.bin c:EIGHTH"));
    const std::vector<uint8_t> d = s.unit_image();
    has(ls(d), " B 002 EIGHTH");
    has(s.c("dos33 c:"), " B 002 EIGHTH");
    CHECK(d[(17 * 16 + 14) * 256 + 0x0B] != 0); // the second catalog sector's first entry
    checked(d);

    Dd f({{0, "MORE    BIN", gen(100, 2)}});
    REQUIRE(f.ok);
    std::vector<AFile> all;
    for (int i = 0; i < 105; ++i)
        all.push_back({"N" + std::to_string(i), 'R', gen(10, static_cast<unsigned>(i))});
    const std::vector<uint8_t> full = image(all);
    f.put_disk(full);
    has(f.c("dos33 put m:more.bin c:MORE"), "Catalog full.");
    CHECK(f.unit_image() == full);
}

TEST(o_in_the_drive_as_on_an_sd_unit)
{
    // The same image in the Disk II's drive 1: the catalog, a GET and a PUT, and the disk read back off
    // the drive checks clean, the PUT's file the same sectors as on C:.
    const std::vector<uint8_t> b = gen(1500, 4);
    Dd r({{0, "CODE    BIN", b}});
    REQUIRE(r.ok);
    const std::vector<uint8_t> img = image(sample());
    r.insert(0, img);
    has(r.c("dos33 o:"), " B 005 MY PROGRAM");
    has(r.c("dos33 get o:\"MY PROGRAM\" m:prog.bin"), "Address $6000, 1000 bytes");
    CHECK(m_data(r, 0, "PROG    BIN") == records(gen(1000, 3)));
    quiet(r.c("dos33 put m:code.bin o:CODE"));
    r.run(ms(1500));
    const std::vector<uint8_t> d = r.drive_image(0);
    REQUIRE(d.size() == IMAGE);
    checked(d);
    std::vector<uint8_t> bw = {0x00, 0x20, 0x00, 0x06};
    std::vector<uint8_t> bb = records(b, 0x1A); // M:'s file, its last record ^Z-padded
    bw.insert(bw.end(), bb.begin(), bb.end());
    bw.resize(7 * 256, 0);
    CHECK(stored(d, "CODE") == bw);
}

TEST(function_76_s_answers)
{
    // MEMTEST's S on C: (DOS33.DSK): refused for M:, A:, past P: ($FF); 1 for a track, sector or op
    // past what there is; the VTOC read, its volume changed and read back.
    Dd r;
    REQUIRE(r.ok);
    const std::string o = run_memtest(r, " S");
    std::printf("  %s\n", vis(o).c_str());
    has(o, "SM FF 00");
    has(o, "SA FF 00");
    has(o, "SQ FF 00");
    has(o, "ST 01 00");
    has(o, "SS 01 00");
    has(o, "SO 01 00");
    has(o, "SR 00 00");
    has(o, "SV 7A");
    has(o, "SW 00 00");
    has(o, "SR2 00 00");
    has(o, "SV2 01");                     // 254 ^ $FF
    CHECK(r.unit_image()[(17 * 16) * 256 + 6] == 0x01);
}

TEST(function_76_drops_a_cp_m_drive_s_buffers)
{
    // MEMTEST's L on C: (CPM.DSK): the drive logged in, its directory sector changed by 76; the next 17
    // reads the disk again, so finds the new name.
    Dd r;
    REQUIRE(r.ok);
    REQUIRE(r.mount(UNIT, "/DISKS/CPM.DSK") == 0);
    const std::string o = run_memtest(r, " L");
    std::printf("  %s\n", vis(o).c_str());
    has(o, "LF 0");
    has(o, "LR 00 00");
    has(o, "LW 00 00");
    hasnt(o, "LQ FF");
    has(o, "LQ 0");
}

TEST(function_76_on_a_write_protected_disk)
{
    Dd r;
    REQUIRE(r.ok);
    const std::vector<uint8_t> img = image({});
    r.insert(0, img);
    const std::string o = run_memtest(r, " J", "WP?", [&] { r.card().disk(0)->set_write_protected(true); });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "JR 00 00");
    has(o, "JW 02 00");
    CHECK(r.drive_image(0) == img);
}

int main()
{
    if (!have_prom())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
