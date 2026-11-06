/**
 * @file disk2_rig.h
 * @brief The Disk II suites' rig (phase 6c): OODOS.SYS booted with the owner's sequencer PROM, the
 * card's apple-do images in the drives, and the oracles the suites share.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * test_oodos_disk2 and test_oodos_disk2_media include it after oodos_rig.h. The PROM is loaded from
 * fixtures/apple2e at run time; each suite exits 77 without it. Nothing of it is printed.
 */

#pragma once

#include "bus_cycle.h"
#include "disk2_card.h"
#include "disk_image.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace disk2_rig
{
    using namespace ooe::test;
    using namespace ooe::emu;
    using namespace oodos_rig;
    using namespace oodos_rig::ccp;

    inline const std::string UTILS = std::getenv("OODOS_UTILS_DIR") ? std::getenv("OODOS_UTILS_DIR") : EMU_OODOS_UTILS_DIR;
    inline const std::string P6 = EMU_FIXTURE_DIR "/apple2e/disk2-16-p6-sequencer-341-0028-a.bin";
    constexpr unsigned UNIT = 3;
    constexpr size_t IMAGE = DiskImage::SECTOR_IMAGE;
    constexpr uint16_t DISK2 = 0xC600;

    inline std::vector<uint8_t> gen(size_t n, unsigned seed)
    {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i)
            b[i] = static_cast<uint8_t>(((i >> 7) * 31 + i * 7 + seed) & 0xFF);
        return b;
    }
    inline std::vector<uint8_t> records(std::vector<uint8_t> d, uint8_t pad = 0)
    {
        d.resize((d.size() + RECORD - 1) / RECORD * RECORD, pad);
        return d;
    }
    inline void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }
    inline void hasnt(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) == std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  don't want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }

    /// The rig: PIP on M:, the search chain M: first; the card's apple-do images read off it (by mounting
    /// them as unit 3 in turn), CPM.DSK left mounted there as C:, and `o` and `p` in drives 1 and 2.
    struct Dr : CcpRig
    {
        std::vector<uint8_t> cpm, blank; ///< the card's images, 140 KB each
        explicit Dr(bool with_cpm = true, bool with_blank = true, std::vector<MFile> files = {})
            : CcpRig({}, {}, P6)
        {
            if (!ok)
                return;
            put_m(files);
            put_util("pip", UTILS);
            chain({13, 0});
            ok = mount(UNIT, "/DISKS/BLANK.DSK") == 0;
            blank = unit_image();
            ok = ok && mount(UNIT, "/DISKS/CPM.DSK") == 0;
            cpm = unit_image();
            if (with_cpm)
                insert(0, cpm);
            if (with_blank)
                insert(1, blank);
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
        std::shared_ptr<DiskImage> insert(unsigned drive, const std::vector<uint8_t> &image)
        {
            auto d = std::shared_ptr<DiskImage>(DiskImage::from_sectors(image, DiskImage::Format::Dos));
            card().insert(drive, d);
            return d;
        }
        /// The disk in `drive` as 140 KB of the file's sectors, or empty if it doesn't read as one.
        std::vector<uint8_t> drive_image(unsigned drive)
        {
            std::vector<uint8_t> img;
            std::string error;
            if (!card().disk(drive) || !card().disk(drive)->to_sectors(DiskImage::Format::Dos, img, error))
                return {};
            return img;
        }
        /// A command line to its prompt, a minute of machine time at most (the drive is slow).
        std::string c(const std::string &line) { return to(line, "\r\nA>", ms(60000)); }
        /// Run on (the motor's second, say).
        void wait(uint64_t cycles) { run(cycles); }
    };

    /// The bit after each disk byte of a track, as a reader takes them (bits in until bit 7 is set).
    inline std::vector<std::pair<uint8_t, uint32_t>> track_bytes(const DiskTrack &t)
    {
        std::vector<std::pair<uint8_t, uint32_t>> out;
        uint8_t reg = 0;
        for (uint32_t n = 0; n < t.bit_count; ++n)
        {
            reg = static_cast<uint8_t>(reg << 1 | t.bit(n));
            if (reg & 0x80)
            {
                out.push_back({reg, n});
                reg = 0;
            }
        }
        return out;
    }
    inline uint8_t four4(uint8_t a, uint8_t b) { return static_cast<uint8_t>(((a << 1) | 1) & b); }
    /// Physical sector p's data field on track t spoiled: a bit of a byte in its middle cleared.
    inline bool spoil_data(DiskImage &d, unsigned t, unsigned p)
    {
        DiskTrack &tr = d.track_for_writing(4 * t);
        const auto b = track_bytes(tr);
        for (size_t i = 0; i + 20 < b.size(); ++i)
            if (b[i].first == 0xD5 && b[i + 1].first == 0xAA && b[i + 2].first == 0x96 &&
                four4(b[i + 7].first, b[i + 8].first) == p)
                for (size_t j = i + 13; j + 200 < b.size(); ++j)
                    if (b[j].first == 0xD5 && b[j + 1].first == 0xAA && b[j + 2].first == 0xAD)
                    {
                        const uint32_t bit = b[j + 150].second - 1; // a 1 bit: bit 7 of the byte is the last's... any bit set
                        for (uint32_t k = bit; k > b[j + 149].second; --k)
                            if (tr.bit(k))
                            {
                                tr.set_bit(k, false);
                                return true;
                            }
                        return false;
                    }
        return false;
    }
    /// The bus cycles of each load of the data bus while writing (a write to Q6 on or Q7 on).
    struct Loads : CycleObserver
    {
        std::vector<uint64_t> at;
        std::vector<bool> dma_off;
        Decoder *bus = nullptr;
        void on_cycle(const BusCycle &c) override
        {
            if (c.write && (c.addr == DISK2 + 0x0D || c.addr == DISK2 + 0x0F))
            {
                at.push_back(c.cycle);
                dma_off.push_back(bus && !(bus->peek(0xC441) & 1));
            }
        }
    };

    /// 343 disk bytes of a data field to its 256, or nothing: the test's own 6-and-2 (DiskImage's order).
    inline std::optional<std::vector<uint8_t>> decode62(const uint8_t *n)
    {
        std::array<int, 256> d{};
        d.fill(-1);
        for (unsigned i = 0; i < 64; ++i)
            d[DiskImage::code62()[i]] = static_cast<int>(i);
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
        auto sw = [](unsigned b) { return static_cast<uint8_t>(((b & 1) << 1) | ((b >> 1) & 1)); };
        std::vector<uint8_t> out(256);
        for (unsigned i = 0; i < 256; ++i)
            out[i] = static_cast<uint8_t>(v[86 + i] << 2 | sw((v[i % 86] >> (2 * (i / 86))) & 3));
        return out;
    }
    /// Track t's sectors that read whole (physical number: its 256 bytes), and how many address fields have
    /// an epilogue other than DE AA.
    struct TrackRead
    {
        std::vector<std::pair<unsigned, std::vector<uint8_t>>> good;
        unsigned bad_epilogues = 0;
    };
    inline TrackRead read_track(const DiskImage &d, unsigned t)
    {
        TrackRead out;
        const DiskTrack *tr = d.track(4 * t);
        if (!tr)
            return out;
        std::vector<uint8_t> n;
        for (const auto &b : track_bytes(*tr))
            n.push_back(b.first);
        const size_t once = n.size();
        n.insert(n.end(), n.begin(), n.end()); // round twice: a field across the index
        for (size_t i = 0; i < once; ++i)
        {
            if (n[i] != 0xD5 || n[i + 1] != 0xAA || n[i + 2] != 0x96)
                continue;
            if (n[i + 11] != 0xDE || n[i + 12] != 0xAA)
                ++out.bad_epilogues;
            const unsigned p = four4(n[i + 7], n[i + 8]);
            for (size_t j = i + 13; j < i + 60; ++j)
                if (n[j] == 0xD5 && n[j + 1] == 0xAA && n[j + 2] == 0xAD)
                {
                    if (auto v = decode62(&n[j + 3]))
                        out.good.push_back({p, *v});
                    break;
                }
        }
        return out;
    }
    inline bool have_prom() { return std::ifstream(P6).good(); }
} // namespace disk2_rig
