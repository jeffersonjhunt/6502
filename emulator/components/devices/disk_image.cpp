/**
 * @file disk_image.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A 5.25" Apple disk as tracks of bits: WOZ, and 140 KB sector images both ways.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "disk_image.h"

#include <algorithm>
#include <cstring>

namespace ooe::emu
{
    namespace
    {
        /// The file sector (of 16 in a track) that physical sector p holds, for each order.
        constexpr uint8_t DOS_ORDER[16] = {0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4, 0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF};
        constexpr uint8_t PRODOS_ORDER[16] = {0x0, 0x8, 0x1, 0x9, 0x2, 0xA, 0x3, 0xB, 0x4, 0xC, 0x5, 0xD, 0x6, 0xE, 0x7, 0xF};
        /// DOS 3.3's logical sector to physical.
        constexpr uint8_t DOS_PHYSICAL[16] = {0x0, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x1, 0xE, 0xC, 0xA, 0x8, 0x6, 0x4, 0x2, 0xF};

        const uint8_t *order_of(DiskImage::Format f) { return f == DiskImage::Format::Prodos ? PRODOS_ORDER : DOS_ORDER; }

        uint32_t le32(const uint8_t *p)
        {
            return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24;
        }
        uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
        void put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
        {
            for (int i = 0; i < 4; ++i)
                v[at + static_cast<size_t>(i)] = static_cast<uint8_t>(x >> (8 * i));
        }
        void put16(std::vector<uint8_t> &v, size_t at, uint32_t x)
        {
            v[at] = static_cast<uint8_t>(x);
            v[at + 1] = static_cast<uint8_t>(x >> 8);
        }

        uint32_t crc32(const uint8_t *p, size_t n)
        {
            uint32_t c = 0xFFFFFFFFu;
            for (size_t i = 0; i < n; ++i)
            {
                c ^= p[i];
                for (int k = 0; k < 8; ++k)
                    c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            }
            return ~c;
        }

        /// Bits onto a track as they go round.
        struct BitWriter
        {
            DiskTrack t;
            void bit(bool on)
            {
                if ((t.bit_count >> 3) >= t.bytes.size())
                    t.bytes.push_back(0);
                t.set_bit(t.bit_count++, on);
            }
            void byte(uint8_t b)
            {
                for (int i = 7; i >= 0; --i)
                    bit((b >> i) & 1);
            }
            void sync(unsigned n) // $FF and two zero bits: the reader falls into step on them
            {
                for (unsigned i = 0; i < n; ++i)
                {
                    byte(0xFF);
                    bit(false);
                    bit(false);
                }
            }
            void four_and_four(uint8_t v)
            {
                byte(static_cast<uint8_t>((v >> 1) | 0xAA));
                byte(static_cast<uint8_t>(v | 0xAA));
            }
        };

        uint8_t swap2(uint8_t b) { return static_cast<uint8_t>(((b & 1) << 1) | ((b >> 1) & 1)); }

        /// 256 bytes as 342 six-bit values (86 of the low two bits of each, then 256 of the high six), the
        /// order they are written in.
        std::array<uint8_t, 342> to_62(const uint8_t *data)
        {
            std::array<uint8_t, 342> v{};
            for (unsigned i = 0; i < 86; ++i)
            {
                uint8_t aux = swap2(data[i]);
                aux = static_cast<uint8_t>(aux | swap2(data[i + 86]) << 2);
                if (i + 172 < 256)
                    aux = static_cast<uint8_t>(aux | swap2(data[i + 172]) << 4);
                v[i] = aux;
            }
            for (unsigned i = 0; i < 256; ++i)
                v[86 + i] = static_cast<uint8_t>(data[i] >> 2);
            return v;
        }

        void from_62(const std::array<uint8_t, 342> &v, uint8_t *data)
        {
            for (unsigned i = 0; i < 256; ++i)
            {
                const uint8_t aux = v[i % 86];
                const uint8_t two = static_cast<uint8_t>((aux >> (2 * (i / 86))) & 3);
                data[i] = static_cast<uint8_t>(v[86 + i] << 2 | swap2(two));
            }
        }

        /// A track's disk bytes as an idealized reader takes them: bits in until bit 7 is set, twice round.
        std::vector<uint8_t> read_bytes(const DiskTrack &t)
        {
            std::vector<uint8_t> out;
            uint8_t reg = 0;
            for (uint32_t n = 0; n < 2 * t.bit_count; ++n)
            {
                reg = static_cast<uint8_t>(reg << 1 | t.bit(n % t.bit_count));
                if (reg & 0x80)
                {
                    out.push_back(reg);
                    reg = 0;
                }
            }
            return out;
        }

        bool looks_dos(const std::vector<uint8_t> &img, const uint8_t *order)
        {
            const auto sector = [&](unsigned track, unsigned logical) {
                return img.data() + (track * 16 + order[DOS_PHYSICAL[logical]]) * 256;
            };
            const uint8_t *vtoc = sector(17, 0);
            if (vtoc[3] != 3 || vtoc[0x27] != 122 || vtoc[0x34] != 35 || vtoc[0x35] != 16)
                return false;
            const unsigned ct = vtoc[1], cs = vtoc[2];
            if (ct == 0 || ct > 34 || cs > 15)
                return false;
            // The catalog's sectors link down its track. Sectors 0 and 15 sit alike in both orders (the VTOC
            // reads the same either way): it is the links further down that tell them apart, so three must
            // hold (DOS's INIT links all 15).
            for (unsigned s = cs, links = 0; links < 3; --s, ++links)
            {
                const uint8_t *cat = sector(ct, s);
                if (s == 0 || cat[1] != ct || cat[2] != s - 1)
                    return false;
            }
            return true;
        }

        bool looks_prodos(const std::vector<uint8_t> &img, const uint8_t *order)
        {
            const auto block = [&](unsigned b) { // the first half of a block: physical sector 2(b%8)*... per ProDOS
                static constexpr uint8_t FIRST[8] = {0, 4, 8, 12, 1, 5, 9, 13};
                return img.data() + ((b / 8) * 16 + order[FIRST[b % 8]]) * 256;
            };
            const uint8_t *dir = block(2);
            const uint8_t *next = block(3);
            return dir[0] == 0 && dir[1] == 0 && (dir[4] >> 4) == 0xF && (dir[4] & 0xF) != 0 && le16(next) == 2;
        }

        /// Each track's new index when only the ones `tmap` names, and that hold bits, are kept (NO_TRACK: dropped).
        /// In order, and never more than the quarter tracks.
        std::vector<uint8_t> kept_tracks(const std::array<uint8_t, DiskImage::QUARTER_TRACKS> &tmap,
                                         const std::vector<DiskTrack> &tracks)
        {
            std::vector<uint8_t> to(tracks.size(), DiskImage::NO_TRACK);
            for (const uint8_t m : tmap)
                if (m < tracks.size() && tracks[m].bit_count)
                    to[m] = 0;
            uint8_t n = 0;
            for (uint8_t &t : to)
                if (t != DiskImage::NO_TRACK)
                    t = n++;
            return to;
        }

        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            return s;
        }
    } // namespace

    const std::array<uint8_t, 64> &DiskImage::code62()
    {
        static const std::array<uint8_t, 64> table = [] {
            std::array<uint8_t, 64> t{};
            size_t n = 0;
            for (unsigned b = 0x80; b < 0x100 && n < t.size(); ++b)
            {
                unsigned zeros = 0, ones = 0;
                for (int i = 6; i >= 1; --i)
                {
                    const unsigned hi = (b >> i) & 1, lo = (b >> (i - 1)) & 1;
                    zeros += !hi && !lo;
                    ones += hi && lo;
                }
                zeros += !((b >> 7) & 1) && !((b >> 6) & 1);
                if (zeros <= 1 && ones >= 1 && b != 0xAA && b != 0xD5)
                    t[n++] = static_cast<uint8_t>(b);
            }
            return t;
        }();
        return table;
    }

    DiskTrack *DiskImage::track(unsigned q)
    {
        return q < QUARTER_TRACKS && tmap_[q] != NO_TRACK ? &tracks_[tmap_[q]] : nullptr;
    }

    const DiskTrack *DiskImage::track(unsigned q) const
    {
        return q < QUARTER_TRACKS && tmap_[q] != NO_TRACK ? &tracks_[tmap_[q]] : nullptr;
    }

    DiskTrack &DiskImage::track_for_writing(unsigned q)
    {
        q = std::min(q, QUARTER_TRACKS - 1);
        if (DiskTrack *t = track(q))
            return *t;
        // Every track is some quarter track's (load_woz keeps only those), and q had none: so this one is at
        // most the 160th, its index well below NO_TRACK.
        DiskTrack t;
        t.bit_count = BLANK_TRACK_BITS;
        t.bytes.assign((BLANK_TRACK_BITS + 7) / 8, 0);
        tracks_.push_back(std::move(t));
        tmap_[q] = static_cast<uint8_t>(tracks_.size() - 1);
        return tracks_.back();
    }

    std::unique_ptr<DiskImage> DiskImage::from_sectors(const std::vector<uint8_t> &image, Format order, uint8_t volume)
    {
        if (image.size() != SECTOR_IMAGE)
            return nullptr;
        std::unique_ptr<DiskImage> d(new DiskImage());
        d->format_ = order;
        d->boot_format_ = 1;
        d->tmap_.fill(NO_TRACK);
        const uint8_t *map = order_of(order);
        const auto &code = code62();
        for (unsigned t = 0; t < 35; ++t)
        {
            BitWriter w;
            w.sync(16);
            for (unsigned p = 0; p < 16; ++p)
            {
                w.byte(0xD5), w.byte(0xAA), w.byte(0x96);
                w.four_and_four(volume);
                w.four_and_four(static_cast<uint8_t>(t));
                w.four_and_four(static_cast<uint8_t>(p));
                w.four_and_four(static_cast<uint8_t>(volume ^ t ^ p));
                w.byte(0xDE), w.byte(0xAA), w.byte(0xEB);
                w.sync(7);
                w.byte(0xD5), w.byte(0xAA), w.byte(0xAD);
                const auto v = to_62(image.data() + (t * 16 + map[p]) * 256);
                uint8_t last = 0;
                for (const uint8_t x : v)
                {
                    w.byte(code[x ^ last]);
                    last = x;
                }
                w.byte(code[last]);
                w.byte(0xDE), w.byte(0xAA), w.byte(0xEB);
                w.sync(16);
            }
            d->tracks_.push_back(std::move(w.t));
            for (int q = static_cast<int>(4 * t) - 1; q <= static_cast<int>(4 * t) + 1; ++q) // WOZ's convention
                if (q >= 0)
                    d->tmap_[static_cast<size_t>(q)] = static_cast<uint8_t>(t);
        }
        return d;
    }

    std::unique_ptr<DiskImage> DiskImage::load(const std::vector<uint8_t> &bytes, const std::string &name,
                                               std::string &error)
    {
        if (bytes.size() >= 8 && (std::memcmp(bytes.data(), "WOZ1", 4) == 0 || std::memcmp(bytes.data(), "WOZ2", 4) == 0))
            return load_woz(bytes, error);
        if (bytes.size() != SECTOR_IMAGE)
        {
            error = name + ": " + std::to_string(bytes.size()) +
                    " bytes is no disk image (want a .woz, or a 143,360-byte .dsk, .do or .po)";
            return nullptr;
        }
        Format order;
        if (looks_dos(bytes, DOS_ORDER) || looks_prodos(bytes, DOS_ORDER))
            order = Format::Dos;
        else if (looks_dos(bytes, PRODOS_ORDER) || looks_prodos(bytes, PRODOS_ORDER))
            order = Format::Prodos;
        else
        {
            const std::string n = lower(name);
            order = n.size() > 3 && n.compare(n.size() - 3, 3, ".po") == 0 ? Format::Prodos : Format::Dos;
        }
        return from_sectors(bytes, order);
    }

    std::unique_ptr<DiskImage> DiskImage::load_woz(const std::vector<uint8_t> &b, std::string &error)
    {
        const bool v2 = b[3] == '2';
        if (b.size() < 12 || b[4] != 0xFF || b[5] != 0x0A || b[6] != 0x0D || b[7] != 0x0A)
        {
            error = "not a WOZ file: its header is damaged (sent as text?)";
            return nullptr;
        }
        if (const uint32_t crc = le32(&b[8]); crc && crc != crc32(b.data() + 12, b.size() - 12))
        {
            error = "WOZ file damaged: its CRC does not match";
            return nullptr;
        }
        std::unique_ptr<DiskImage> d(new DiskImage());
        d->tmap_.fill(NO_TRACK);
        bool info = false, tmap = false, trks = false;
        for (size_t at = 12; at + 8 <= b.size();)
        {
            const uint8_t *id = &b[at];
            const uint32_t size = le32(&b[at + 4]);
            const size_t data = at + 8;
            if (size > b.size() - data)
            {
                error = "WOZ file damaged: a chunk runs past its end";
                return nullptr;
            }
            if (std::memcmp(id, "INFO", 4) == 0 && size >= 37)
            {
                info = true;
                if (b[data + 1] != 1)
                {
                    error = "a 3.5\" WOZ disk: the Disk II takes 5.25\" disks";
                    return nullptr;
                }
                d->write_protected_ = b[data + 2] != 0;
                if (v2 && size >= 40)
                {
                    d->boot_format_ = b[data + 38];
                    if (b[data + 39])
                        d->bit_timing_ = b[data + 39];
                }
            }
            else if (std::memcmp(id, "TMAP", 4) == 0 && size >= QUARTER_TRACKS)
            {
                tmap = true;
                std::copy_n(&b[data], QUARTER_TRACKS, d->tmap_.begin());
            }
            else if (std::memcmp(id, "TRKS", 4) == 0)
            {
                trks = true;
                if (v2)
                {
                    for (unsigned i = 0; i < 160 && data + 8 * i + 8 <= data + size; ++i)
                    {
                        const uint8_t *e = &b[data + 8 * i];
                        const size_t start = size_t{le16(e)} * 512, blocks = le16(e + 2);
                        const uint32_t bits = le32(e + 4);
                        if (!blocks) // unused: kept in its place, as TMAP counts them
                        {
                            d->tracks_.emplace_back();
                            continue;
                        }
                        if (start + blocks * 512 > b.size() || bits > blocks * 512 * 8)
                        {
                            error = "WOZ file damaged: track " + std::to_string(i) + " is outside it";
                            return nullptr;
                        }
                        DiskTrack t;
                        t.bit_count = bits;
                        t.bytes.assign(b.begin() + static_cast<long>(start),
                                       b.begin() + static_cast<long>(start + (bits + 7) / 8));
                        d->tracks_.push_back(std::move(t));
                    }
                }
                else
                {
                    constexpr size_t ENTRY = 6656;
                    for (size_t i = 0; (i + 1) * ENTRY <= size && i < NO_TRACK; ++i) // TMAP names no more
                    {
                        const uint8_t *e = &b[data + i * ENTRY];
                        const uint32_t bits = le16(e + 6648);
                        DiskTrack t;
                        t.bit_count = std::min<uint32_t>(bits, 6646 * 8);
                        t.bytes.assign(e, e + (t.bit_count + 7) / 8);
                        d->tracks_.push_back(std::move(t));
                    }
                }
            }
            at = data + size;
        }
        if (!info || !tmap || !trks)
        {
            error = "WOZ file damaged: it lacks its INFO, TMAP or TRKS chunk";
            return nullptr;
        }
        // Only the tracks TMAP names: no more than its 160 quarter tracks, however many the file holds.
        const std::vector<uint8_t> to = kept_tracks(d->tmap_, d->tracks_);
        std::vector<DiskTrack> kept;
        for (size_t i = 0; i < to.size(); ++i)
            if (to[i] != NO_TRACK)
                kept.push_back(std::move(d->tracks_[i]));
        d->tracks_ = std::move(kept);
        for (uint8_t &m : d->tmap_)
            m = m < to.size() ? to[m] : NO_TRACK;
        return d;
    }

    std::vector<uint8_t> DiskImage::to_woz() const
    {
        std::vector<uint8_t> f(1536, 0);
        std::memcpy(f.data(), "WOZ2\xFF\x0A\x0D\x0A", 8);
        std::memcpy(&f[12], "INFO", 4);
        put32(f, 16, 60);
        f[20] = 2;                       // INFO version
        f[21] = 1;                       // 5.25"
        f[22] = write_protected_ ? 1 : 0;
        f[24] = format_ == Format::Woz ? 0 : 1; // cleaned: made from sectors
        const char creator[] = "OO-1 emulator";
        std::memset(&f[25], ' ', 32);
        std::memcpy(&f[25], creator, sizeof creator - 1);
        f[57] = 1;                       // one side
        f[58] = boot_format_;
        f[59] = bit_timing_;
        // The tracks TMAP names that hold bits, renumbered: at most 160, TRKS's entries, and none empty (a
        // reader stops at one).
        const std::vector<uint8_t> to = kept_tracks(tmap_, tracks_);
        std::memcpy(&f[80], "TMAP", 4);
        put32(f, 84, 160);
        for (size_t q = 0; q < QUARTER_TRACKS; ++q)
            f[88 + q] = tmap_[q] < to.size() ? to[tmap_[q]] : NO_TRACK;
        std::memcpy(&f[248], "TRKS", 4);
        uint32_t block = 3, largest = 0;
        for (size_t i = 0, n = 0; i < tracks_.size(); ++i)
        {
            if (to[i] == NO_TRACK)
                continue;
            const DiskTrack &t = tracks_[i];
            const size_t e = 256 + 8 * n++;
            const uint32_t blocks = ((t.bit_count + 7) / 8 + 511) / 512;
            put16(f, e, block);
            put16(f, e + 2, blocks);
            put32(f, e + 4, t.bit_count);
            std::vector<uint8_t> data(t.bytes.begin(), t.bytes.begin() + (t.bit_count + 7) / 8);
            data.resize(size_t{blocks} * 512, 0);
            f.insert(f.end(), data.begin(), data.end());
            block += blocks;
            largest = std::max(largest, blocks);
        }
        put16(f, 20 + 44, largest);
        put32(f, 252, static_cast<uint32_t>(f.size() - 256));
        put32(f, 8, crc32(f.data() + 12, f.size() - 12));
        return f;
    }

    bool DiskImage::to_sectors(Format order, std::vector<uint8_t> &out, std::string &error) const
    {
        out.assign(SECTOR_IMAGE, 0);
        const uint8_t *map = order_of(order);
        std::array<int, 256> decode{};
        decode.fill(-1);
        for (size_t i = 0; i < 64; ++i)
            decode[code62()[i]] = static_cast<int>(i);
        const auto odd_even = [](uint8_t a, uint8_t b) { return static_cast<uint8_t>(((a << 1) | 1) & b); };
        for (unsigned t = 0; t < 35; ++t)
        {
            const DiskTrack *trk = track(4 * t);
            if (!trk)
            {
                error = "track " + std::to_string(t) + " is empty: save it as .woz";
                return false;
            }
            const std::vector<uint8_t> n = read_bytes(*trk);
            unsigned found = 0;
            for (size_t i = 0; i + 14 < n.size() && found != 0xFFFF; ++i)
            {
                if (n[i] != 0xD5 || n[i + 1] != 0xAA || n[i + 2] != 0x96)
                    continue;
                const uint8_t vol = odd_even(n[i + 3], n[i + 4]), trk_no = odd_even(n[i + 5], n[i + 6]),
                              sec = odd_even(n[i + 7], n[i + 8]), sum = odd_even(n[i + 9], n[i + 10]);
                if ((vol ^ trk_no ^ sec) != sum || trk_no != t || sec > 15)
                    continue;
                size_t j = i + 11;
                const size_t limit = std::min(n.size(), j + 40);
                while (j + 2 < limit && !(n[j] == 0xD5 && n[j + 1] == 0xAA && n[j + 2] == 0xAD))
                    ++j;
                if (j + 3 + 343 > n.size() || j + 2 >= limit)
                    continue;
                std::array<uint8_t, 342> v{};
                uint8_t last = 0;
                bool ok = true;
                for (size_t k = 0; k < 343 && ok; ++k)
                {
                    const int x = decode[n[j + 3 + k]];
                    if (x < 0)
                        ok = false;
                    else if (k < 342)
                        last = v[k] = static_cast<uint8_t>(x ^ last);
                    else
                        ok = static_cast<uint8_t>(x) == last;
                }
                if (!ok)
                    continue;
                from_62(v, out.data() + (t * 16 + map[sec]) * 256);
                found |= 1u << sec;
            }
            if (found != 0xFFFF)
            {
                for (unsigned s = 0; s < 16; ++s)
                    if (!(found & (1u << s)))
                    {
                        error = "track " + std::to_string(t) + ", sector " + std::to_string(s) +
                                " does not read as a plain sector: save it as .woz";
                        return false;
                    }
            }
        }
        return true;
    }
} // namespace ooe::emu
