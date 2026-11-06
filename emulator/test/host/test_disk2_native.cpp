/**
 * @file test_disk2_native.cpp
 * @brief The Disk II card in native mode, phase 6a (hw/oodos.md, "The plan, phase 6"): 6502 code on the
 * backplane driving its switches, and disks read and written at native mode's clock.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The card's sequencer is Apple's 341-0028, which the owner supplies: the cases that read or write a
 * disk load it from fixtures/apple2e at run time and say SKIP without it (nothing of it is in the repo,
 * and nothing of it is printed: only what the tests work out). The disks are ours, 140 KB of a pattern
 * written out by DiskImage::from_sectors. The test drives the switches itself, at chosen bus cycles, as
 * a native driver will: polling the data register every 7 cycles, and loading a byte to write every 224
 * (a sync byte's ten bits, 280). Its 6-and-2 and 4-and-4 code is its own, written from the format.
 */

#include "harness.h"

#include "cpu6502.h"
#include "decoder.h"
#include "disk2_card.h"
#include "disk_image.h"
#include "machine.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    const std::string P6 = EMU_FIXTURE_DIR "/apple2e/disk2-16-p6-sequencer-341-0028-a.bin";
    bool have(const std::string &path) { return std::ifstream(path).good(); }
    bool no_prom()
    {
        if (have(P6))
            return false;
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return true;
    }

    constexpr unsigned PH0 = 0x0, MOTOR_OFF = 0x8, MOTOR_ON = 0x9, Q6L = 0xC, Q6H = 0xD, Q7L = 0xE, Q7H = 0xF;
    constexpr uint8_t VOLUME = 254;

    /// Our disk: byte i of the file's sector s of track t.
    uint8_t pattern(unsigned t, unsigned s, unsigned i) { return static_cast<uint8_t>(t * 31 + s * 17 + i * 7 + (i >> 3)); }
    std::vector<uint8_t> image()
    {
        std::vector<uint8_t> b(DiskImage::SECTOR_IMAGE);
        for (unsigned t = 0; t < 35; ++t)
            for (unsigned s = 0; s < 16; ++s)
                for (unsigned i = 0; i < 256; ++i)
                    b[(t * 16 + s) * 256 + i] = pattern(t, s, i);
        return b;
    }

    // ---- the format, our own code: 6-and-2 as DiskImage writes it (the 86 values of each byte's low two
    // bits, swapped, then the 256 high sixes, each XORed with the one before, then the last as a checksum)

    std::array<int, 256> decode_table()
    {
        std::array<int, 256> d{};
        d.fill(-1);
        for (unsigned i = 0; i < 64; ++i)
            d[DiskImage::code62()[i]] = static_cast<int>(i);
        return d;
    }
    uint8_t swap2(unsigned v) { return static_cast<uint8_t>(((v & 1) << 1) | ((v >> 1) & 1)); }
    std::vector<uint8_t> encode62(const std::vector<uint8_t> &data)
    {
        std::array<uint8_t, 342> v{};
        for (unsigned i = 0; i < 86; ++i)
        {
            unsigned aux = swap2(data[i]) | swap2(data[i + 86]) << 2;
            if (i + 172 < 256)
                aux |= unsigned{swap2(data[i + 172])} << 4;
            v[i] = static_cast<uint8_t>(aux);
        }
        for (unsigned i = 0; i < 256; ++i)
            v[86 + i] = static_cast<uint8_t>(data[i] >> 2);
        std::vector<uint8_t> out;
        uint8_t last = 0;
        for (uint8_t x : v)
        {
            out.push_back(DiskImage::code62()[x ^ last]);
            last = x;
        }
        out.push_back(DiskImage::code62()[last]);
        return out;
    }
    /// 343 disk bytes to 256, or nothing (a byte not of the code, or the checksum wrong).
    std::optional<std::vector<uint8_t>> decode62(const uint8_t *n)
    {
        static const std::array<int, 256> d = decode_table();
        std::array<uint8_t, 342> v{};
        uint8_t x = 0;
        for (unsigned i = 0; i < 342; ++i)
        {
            if (d[n[i]] < 0)
                return std::nullopt;
            x = static_cast<uint8_t>(x ^ d[n[i]]);
            v[i] = x;
        }
        if (d[n[342]] < 0 || static_cast<uint8_t>(d[n[342]]) != x)
            return std::nullopt;
        std::vector<uint8_t> out(256);
        for (unsigned i = 0; i < 256; ++i)
            out[i] = static_cast<uint8_t>(v[86 + i] << 2 | swap2((v[i % 86] >> (2 * (i / 86))) & 3));
        return out;
    }
    uint8_t four4(uint8_t a, uint8_t b) { return static_cast<uint8_t>(((a << 1) | 1) & b); }

    struct Sector
    {
        uint8_t volume, track, sector;
        std::optional<std::vector<uint8_t>> data;
    };
    /// The address fields of a stream of disk bytes, each with the data field after it.
    std::vector<Sector> sectors(const std::vector<uint8_t> &n)
    {
        std::vector<Sector> out;
        for (size_t i = 0; i + 14 < n.size(); ++i)
        {
            if (n[i] != 0xD5 || n[i + 1] != 0xAA || n[i + 2] != 0x96)
                continue;
            const uint8_t *a = &n[i + 3];
            Sector s{four4(a[0], a[1]), four4(a[2], a[3]), four4(a[4], a[5]), std::nullopt};
            if (four4(a[6], a[7]) != (s.volume ^ s.track ^ s.sector) || a[8] != 0xDE || a[9] != 0xAA)
                continue;
            for (size_t j = i + 13; j + 3 + 343 + 2 < n.size() && j < i + 13 + 40; ++j)
                if (n[j] == 0xD5 && n[j + 1] == 0xAA && n[j + 2] == 0xAD)
                {
                    if (n[j + 3 + 343] == 0xDE && n[j + 3 + 344] == 0xAA)
                        s.data = decode62(&n[j + 3]);
                    break;
                }
            if (s.data)
                out.push_back(s);
        }
        return out;
    }

    /// A card on a bus of its own, its clock native mode's, the owner's sequencer in.
    struct Rig
    {
        Decoder bus;
        Disk2Card card;
        std::shared_ptr<DiskImage> disk;
        bool ok{false};
        Rig()
        {
            card.attach(&bus);
            card.set_clock(Disk2Card::Clock::Native);
            std::string error;
            ok = card.load_roms("", P6, error);
            if (!ok)
                std::printf("  %s\n", error.c_str());
            disk = std::shared_ptr<DiskImage>(DiskImage::from_sectors(image(), DiskImage::Format::Dos, VOLUME));
            card.insert(0, disk);
        }
        uint64_t now() const { return bus.now(); }
        /// Idle to the cycle before `at`, then the access in cycle `at`, as an instruction's last cycle.
        void until(uint64_t at)
        {
            while (bus.now() + 1 < at)
                bus.idle();
        }
        uint8_t get(unsigned n, uint64_t at = 0)
        {
            until(at);
            bus.idle();
            return card.read(n);
        }
        void put(unsigned n, uint8_t v, uint64_t at = 0)
        {
            until(at);
            bus.idle();
            card.write(n, v);
        }
        /// The data register every 7 cycles (LDA abs and BPL): the next byte with bit 7 set, then
        /// on past it, to its end (bit 7 clear again), so a byte is taken once however often it's polled.
        uint8_t next()
        {
            uint8_t v = 0;
            do
                v = get(Q6L, now() + 7);
            while (!(v & 0x80));
            for (uint8_t w = v; w & 0x80;)
                w = get(Q6L, now() + 7);
            return v;
        }
        std::vector<uint8_t> stream(size_t n)
        {
            std::vector<uint8_t> out;
            for (size_t i = 0; i < n; ++i)
                out.push_back(next());
            return out;
        }
        /// The file's sectors as DiskImage reads the disk back.
        std::optional<std::vector<uint8_t>> sectors_back() const
        {
            std::vector<uint8_t> out;
            std::string error;
            if (!disk->to_sectors(DiskImage::Format::Dos, out, error))
                return std::nullopt;
            return out;
        }
    };

    /// The file sectors of track t of image() that `s` (decoded) holds: as a set, each exactly once.
    bool every_sector_of_the_track(const std::vector<Sector> &s, unsigned t)
    {
        std::array<bool, 16> physical{}, file{};
        for (const Sector &x : s)
        {
            if (x.volume != VOLUME || x.track != t || x.sector > 15)
                return false;
            bool found = false;
            for (unsigned f = 0; f < 16 && !found; ++f)
            {
                bool same = true;
                for (unsigned i = 0; i < 256 && same; ++i)
                    same = (*x.data)[i] == pattern(t, f, i);
                if (same)
                    found = true, file[f] = true;
            }
            if (!found)
                return false;
            physical[x.sector] = true;
        }
        for (unsigned i = 0; i < 16; ++i)
            if (!physical[i] || !file[i])
                return false;
        return true;
    }

    /// Write the data field of physical sector `p` of the track under the head, a byte every `period`
    /// cycles (a sync byte every period x 10 / 8): found by its address field first, as a driver would.
    /// Q6 on (load) for `hold` cycles before Q6 off (shift): a whole loop of the sequencer's, 8 steps, as the
    /// Apple's STA then ORA (4 cycles at two steps a cycle) have it, is 28 native cycles. The table loads in
    /// one state of the eight, so a shorter hold can miss it.
    void write_sector(Rig &r, unsigned p, const std::vector<uint8_t> &data, uint64_t period, uint64_t hold = 28)
    {
        const uint64_t HOLD = hold;
        // read until the address field of p has gone by
        std::vector<uint8_t> n;
        for (int guard = 0; guard < 20000; ++guard)
        {
            n.push_back(r.next());
            const size_t k = n.size();
            if (k >= 13 && n[k - 13] == 0xD5 && n[k - 12] == 0xAA && n[k - 11] == 0x96 && n[k - 2] == 0xDE &&
                n[k - 1] == 0xAA && four4(n[k - 6], n[k - 5]) == p)
                break;
        }
        std::vector<uint8_t> field = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD5, 0xAA, 0xAD};
        const std::vector<uint8_t> body = encode62(data);
        field.insert(field.end(), body.begin(), body.end());
        field.insert(field.end(), {0xDE, 0xAA, 0xEB});
        r.get(Q6H, r.now() + 4);                 // Q6 on: sense
        const uint8_t wp = r.get(Q7L, r.now() + 4);
        CHECK(!(wp & 0x80));                      // not write-protected
        uint64_t at = r.now() + 8;
        for (size_t i = 0; i < field.size(); ++i)
        {
            r.put(i == 0 ? Q7H : Q6H, field[i], at); // Q7 on (the first), then Q6 on: load the byte
            r.get(Q6L, at + HOLD);                   // Q6 off: shift it out
            at += i < 5 ? period * 10 / 8 : period;
        }
        r.get(Q7L, at);                              // read mode again
        r.get(Q6L, at + 4);
    }
} // namespace

TEST(native_6502_code_drives_every_switch_and_the_bridge_sets_the_clock)
{
    // The backplane machine in native mode, the W65C02S running code at $0400 that touches each of the
    // sixteen switches at $C600-$C60F by LDA or STA, and reads the ID at $C67F. Then MODE's Apple II bit,
    // which the bridge passes on to the card's clock. No PROM needed.
    MachineOptions opt;
    opt.rom_override = EMU_BIOS_ROM;
    std::string error;
    std::ifstream f(EMU_MACHINE_DIR "/backplane.machine");
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto m = Machine::build(text, "backplane.machine", opt, error);
    REQUIRE(m != nullptr);
    Disk2Card *card = m->disk2_card();
    REQUIRE(card != nullptr);
    Cpu6502 cpu(m->bus(), Cpu6502::Model::W65C02S);
    m->bus().reset();
    cpu.reset();
    CHECK(card->clock() == Disk2Card::Clock::Native);
    auto run = [&](uint16_t pc, const std::vector<uint8_t> &code, unsigned instructions) {
        for (size_t i = 0; i < code.size(); ++i)
            m->bus().write_physical(static_cast<uint32_t>(pc + i), code[i]);
        Registers r = cpu.regs();
        r.pc = pc;
        r.p = Cpu6502::FLAG_I;
        cpu.set_regs(r);
        for (unsigned i = 0; i < instructions; ++i)
            cpu.step();
    };
    auto lda = [](uint8_t lo) { return std::vector<uint8_t>{0xAD, lo, 0xC6}; };
    auto sta = [](uint8_t lo) { return std::vector<uint8_t>{0x8D, lo, 0xC6}; };
    std::vector<uint8_t> on;
    for (const auto &i : {sta(0x09), lda(0x0B), lda(0x0D), sta(0x0F), lda(0x01), sta(0x03), lda(0x05), lda(0x07)})
        on.insert(on.end(), i.begin(), i.end());
    run(0x0400, on, 8);
    Disk2Card::Status s = card->status();
    CHECK(s.motor && s.drive == 1 && s.q6 && s.q7 && s.phases == 0x0F);
    std::vector<uint8_t> off;
    for (const auto &i : {lda(0x00), sta(0x02), lda(0x04), lda(0x06), lda(0x0E), sta(0x0C), lda(0x0A), lda(0x08), lda(0x7F)})
        off.insert(off.end(), i.begin(), i.end());
    off.insert(off.end(), {0x8D, 0x80, 0x04}); // STA $0480: the ID
    run(0x0500, off, 10);
    s = card->status();
    CHECK(!s.motor && s.spinning && s.drive == 0 && !s.q6 && !s.q7 && s.phases == 0);
    CHECK_EQ(m->bus().peek_physical(0x0480), 0xC1);
    // 7,000 cycles of native code are 2,000 steps
    const uint64_t s0 = card->status().steps;
    run(0x0600, {0x4C, 0x00, 0x06}, 2000);  // JMP *: 3 cycles each, 6,000; then 1,000 idle bus cycles
    for (int i = 0; i < 1000; ++i)
        m->bus().idle();
    CHECK_EQ(card->status().steps - s0, uint64_t{2000});
    run(0x0700, {0xA9, 0x01, 0x8D, 0x85, 0xC6}, 2); // LDA #1, STA $C685: MODE's Apple II bit
    CHECK(card->clock() == Disk2Card::Clock::Apple);
    m->bus().reset();                        // a hard reset: native mode again
    CHECK(card->clock() == Disk2Card::Clock::Native);
}

TEST(a_track_reads_at_native_speed_every_sector_once)
{
    if (no_prom())
        return;
    Rig r;
    REQUIRE(r.ok);
    r.get(MOTOR_ON);
    r.get(Q7L);
    r.get(Q6L);
    const std::vector<uint8_t> n = r.stream(14000); // two turns and more
    const std::vector<Sector> s = sectors(n);
    std::printf("  track 0: %zu sectors decoded from %zu disk bytes\n", s.size(), n.size());
    CHECK(s.size() >= 16);
    CHECK(every_sector_of_the_track(s, 0));
    // the head stepped out by the phases, a half track at a time, to track 1 (quarter track 4)
    r.get(PH0 + 3);
    r.get(PH0 + 5, r.now() + 20000);
    r.get(PH0 + 2, r.now() + 20000);
    r.get(PH0 + 4, r.now() + 20000);
    CHECK_EQ(r.card.status().quarter_track[0], 4);
    const std::vector<Sector> t1 = sectors(r.stream(14000));
    CHECK(every_sector_of_the_track(t1, 1));
    r.get(MOTOR_OFF);
}

namespace
{
    struct Written
    {
        bool good; ///< DiskImage reads the disk back as 16 sectors a track, only the one changed, to `data`
        bool read; ///< and the sequencer reads it back
    };
    Written write_and_read(uint64_t period, uint64_t hold = 28)
    {
        std::vector<uint8_t> data(256);
        for (unsigned i = 0; i < 256; ++i)
            data[i] = static_cast<uint8_t>(i * 13 + 5);
        Rig r;
        CHECK(r.ok);
        if (!r.ok)
            return {false, false};
        const std::vector<uint8_t> before = image();
        r.get(MOTOR_ON);
        r.get(Q7L);
        r.get(Q6L);
        write_sector(r, 5, data, period, hold);
        const auto back = r.sectors_back();
        unsigned changed = 0;
        bool is_data = false;
        if (back)
            for (unsigned s = 0; s < 35 * 16; ++s)
                if (!std::equal(before.begin() + s * 256, before.begin() + (s + 1) * 256, back->begin() + s * 256))
                {
                    ++changed;
                    is_data = std::equal(data.begin(), data.end(), back->begin() + s * 256);
                }
        r.get(Q6L);
        bool read = false;
        for (const Sector &x : sectors(r.stream(14000)))
            if (x.track == 0 && x.sector == 5 && *x.data == data)
                read = true;
        r.get(MOTOR_OFF);
        std::printf("  %llu cycles a byte, Q6 on %llu: %s, %u sectors changed; read back by the sequencer: %s\n",
                    static_cast<unsigned long long>(period), static_cast<unsigned long long>(hold),
                    back ? "16 sectors a track" : "not a readable disk", changed, read ? "yes" : "no");
        return {back && changed == 1 && is_data, read};
    }
} // namespace

TEST(a_sector_written_at_224_cycles_a_byte_reads_back)
{
    if (no_prom())
        return;
    const Written w = write_and_read(224);
    CHECK(w.good);
    CHECK(w.read);
}

TEST(a_byte_a_cycle_early_is_lost_and_a_load_too_short_writes_nothing)
{
    // 223 cycles a byte: each load comes before the last byte is out, a bit lost every 8 bytes or so, and
    // the field doesn't read back. Q6 on for 4 cycles, as the Apple's code has it at its own clock: in
    // native mode that is about one step of the eight, and the table's load is missed.
    if (no_prom())
        return;
    const Written early = write_and_read(223);
    CHECK(!early.good);
    CHECK(!early.read);
    const Written brief = write_and_read(224, 4);
    CHECK(!brief.good);
    CHECK(!brief.read);
}

TEST(a_byte_a_cycle_late_still_reads_back)
{
    // 225 cycles a byte: the table loads at the next loop of its eight steps, so now and then a byte has a
    // ninth bit, a 0, which a reader passes over as it does a sync byte's. Readable, as on a real Disk II;
    // hw/oodos.md's plan had this as a must-fail case, which it isn't.
    if (no_prom())
        return;
    const Written late = write_and_read(225);
    CHECK(late.good);
    CHECK(late.read);
}

int main() { return run_all(); }
