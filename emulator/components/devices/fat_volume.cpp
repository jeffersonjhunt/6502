/**
 * @file fat_volume.cpp
 * @brief As much FAT as the SD card's firmware needs.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "fat_volume.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ooe::emu
{
    namespace
    {
        constexpr size_t SECTOR = SdMedia::SECTOR;
        constexpr size_t DIR_ENTRY = 32;
        constexpr size_t MAX_ENTRIES = 65536; ///< FAT's limit on a directory, so a looped chain stops
        constexpr uint8_t LFN = 0x0F;   ///< attributes of a long-name entry
        constexpr uint8_t LFN_LAST = 0x40;
        constexpr uint8_t DELETED = 0xE5;
        constexpr uint8_t LOWER_BASE = 0x08, LOWER_EXT = 0x10; ///< the case byte, +12, as Windows NT writes it

        uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
        uint32_t get32(const uint8_t *p) { return uint32_t{get16(p)} | uint32_t{get16(p + 2)} << 16; }

        bool same(const std::string &a, const std::string &b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
                   });
        }

        /// An 8.3 field without its padding, lowered if the case byte says so.
        std::string field(const uint8_t *p, size_t n, bool lower)
        {
            std::string s(reinterpret_cast<const char *>(p), n);
            s.erase(s.find_last_not_of(' ') + 1);
            if (lower)
                for (char &c : s)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        /// The checksum a long name keeps of its short one.
        uint8_t checksum(const uint8_t *short_name)
        {
            uint8_t sum = 0;
            for (size_t i = 0; i < 11; ++i)
                sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + short_name[i]);
            return sum;
        }

        /// Is this sector a FAT volume's boot sector (FatFs's test: a jump and "FAT" where a BPB keeps it)?
        bool boot_sector(const uint8_t *s)
        {
            if (s[510] != 0x55 || s[511] != 0xAA)
                return false;
            if (s[0] != 0xEB && s[0] != 0xE9 && s[0] != 0xE8)
                return false;
            return std::memcmp(s + 54, "FAT", 3) == 0 || std::memcmp(s + 82, "FAT", 3) == 0;
        }

        std::vector<std::string> split(const std::string &path)
        {
            std::vector<std::string> parts;
            size_t at = 0;
            while (at <= path.size())
            {
                const size_t slash = std::min(path.find('/', at), path.size());
                if (slash > at)
                    parts.push_back(path.substr(at, slash - at));
                at = slash + 1;
            }
            return parts;
        }
    } // namespace

    std::unique_ptr<FatVolume> FatVolume::open(SdMedia &media, std::string &error)
    {
        std::unique_ptr<FatVolume> v(new FatVolume(media));
        uint8_t s[SECTOR];
        if (!v->read_sector(0, s))
        {
            error = "the card can't be read";
            return nullptr;
        }
        if (boot_sector(s))
            return v->load(0, error) ? std::move(v) : nullptr; // a superfloppy
        if (s[510] != 0x55 || s[511] != 0xAA)
        {
            error = "no FAT volume or partition table";
            return nullptr;
        }
        for (unsigned i = 0; i < 4; ++i) // the first FAT partition
        {
            const uint8_t *p = s + 446 + 16 * i;
            switch (p[4])
            {
            case 0x01: // FAT12
            case 0x04: // FAT16 under 32 MB
            case 0x06: // FAT16
            case 0x0B: // FAT32 (CHS)
            case 0x0C: // FAT32 (LBA)
            case 0x0E: // FAT16 (LBA)
                return v->load(get32(p + 8), error) ? std::move(v) : nullptr;
            default:
                break;
            }
        }
        error = "no FAT partition";
        return nullptr;
    }

    bool FatVolume::load(uint64_t start, std::string &error)
    {
        uint8_t s[SECTOR];
        if (!read_sector(start, s) || !boot_sector(s))
        {
            error = "the partition holds no FAT volume";
            return false;
        }
        const unsigned bytes = get16(s + 11);
        const unsigned spc = s[13];
        const unsigned reserved = get16(s + 14);
        const unsigned fats = s[16];
        const unsigned root_entries = get16(s + 17);
        const uint32_t total = get16(s + 19) ? get16(s + 19) : get32(s + 32);
        const uint32_t fat_size = get16(s + 22) ? get16(s + 22) : get32(s + 36);
        if (bytes != SECTOR)
        {
            error = "sectors of " + std::to_string(bytes) + " bytes, not 512";
            return false;
        }
        if (!spc || (spc & (spc - 1)) || !reserved || !fats || !fat_size)
        {
            error = "a damaged boot sector";
            return false;
        }
        start_ = start;
        sectors_per_cluster_ = spc;
        fat_ = start + reserved;
        root_ = fat_ + uint64_t{fats} * fat_size;
        root_sectors_ = static_cast<uint32_t>((root_entries * DIR_ENTRY + SECTOR - 1) / SECTOR);
        data_ = root_ + root_sectors_;
        const uint64_t meta = reserved + uint64_t{fats} * fat_size + root_sectors_;
        if (total <= meta || start + total > media_.sectors())
        {
            error = "a volume bigger than the card";
            return false;
        }
        clusters_ = static_cast<uint32_t>((total - meta) / spc);
        type_ = clusters_ < 4085 ? 12 : clusters_ < 65525 ? 16 : 32;
        const uint8_t *label = s + (type_ == 32 ? 71 : 43);
        if (type_ == 32)
            root_cluster_ = get32(s + 44);
        label_ = field(label, 11, false);
        if (label_ == "NO NAME")
            label_.clear();
        return true;
    }

    bool FatVolume::read_sector(uint64_t lba, uint8_t *out) const
    {
        if (lba != cached_)
        {
            if (!media_.read(lba, cache_.data()))
                return false;
            cached_ = lba;
        }
        std::copy(cache_.begin(), cache_.end(), out);
        return true;
    }

    uint32_t FatVolume::next(uint32_t cluster) const
    {
        uint8_t s[SECTOR]{}; // zeroed: GCC's -O2 can't see read_sector fill it, and -Werror stops the release build
        if (type_ == 12)
        {
            const uint64_t at = cluster + cluster / 2; // 1.5 bytes an entry, which may span two sectors
            uint8_t lo = 0, hi = 0;
            if (!read_sector(fat_ + at / SECTOR, s))
                return 0;
            lo = s[at % SECTOR];
            if (!read_sector(fat_ + (at + 1) / SECTOR, s))
                return 0;
            hi = s[(at + 1) % SECTOR];
            const unsigned v = unsigned{lo} | unsigned{hi} << 8;
            const uint32_t entry = cluster & 1 ? v >> 4 : v & 0xFFF;
            return entry >= 0xFF7 ? 0 : entry;
        }
        const unsigned width = type_ == 16 ? 2 : 4;
        const uint64_t at = uint64_t{cluster} * width;
        if (!read_sector(fat_ + at / SECTOR, s))
            return 0;
        if (type_ == 16)
        {
            const uint32_t entry = get16(s + at % SECTOR);
            return entry >= 0xFFF7 ? 0 : entry;
        }
        const uint32_t entry = get32(s + at % SECTOR) & 0x0FFFFFFF;
        return entry >= 0x0FFFFFF7 ? 0 : entry;
    }

    FatVolume::Status FatVolume::read_directory(uint32_t cluster, std::vector<Entry> &out) const
    {
        out.clear();
        std::vector<uint64_t> sectors; // the directory's, in order
        if (cluster == 0 && type_ != 32)
        {
            for (uint32_t i = 0; i < root_sectors_; ++i)
                sectors.push_back(root_ + i);
        }
        else
        {
            uint32_t c = cluster ? cluster : root_cluster_;
            while (c)
            {
                if (!valid(c) || sectors.size() * (SECTOR / DIR_ENTRY) >= MAX_ENTRIES)
                    return Status::Damaged; // off the volume, or round in a loop
                for (uint32_t i = 0; i < sectors_per_cluster_; ++i)
                    sectors.push_back(cluster_sector(c) + i);
                c = next(c);
            }
        }

        std::string lfn;
        uint8_t lfn_sum = 0;
        unsigned lfn_parts = 0, lfn_next = 0; // parts in it, the next one expected (counting down)
        uint8_t s[SECTOR];
        for (uint64_t lba : sectors)
        {
            if (!read_sector(lba, s))
                return Status::Damaged;
            for (size_t at = 0; at < SECTOR; at += DIR_ENTRY)
            {
                const uint8_t *e = s + at;
                if (e[0] == 0x00)
                    return Status::Ok; // the end
                if (e[0] == DELETED)
                {
                    lfn_parts = lfn_next = 0;
                    continue;
                }
                if (e[11] == LFN)
                {
                    const unsigned seq = e[0] & 0x1F;
                    if (e[0] & LFN_LAST)
                    {
                        lfn_parts = seq;
                        lfn.assign(seq * 13, '\0');
                        lfn_sum = e[13];
                        lfn_next = seq;
                    }
                    if (!lfn_next || seq != lfn_next || e[13] != lfn_sum || !seq)
                    {
                        lfn_parts = lfn_next = 0; // out of order: forget it
                        continue;
                    }
                    static constexpr uint8_t CHARS[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                    for (unsigned k = 0; k < 13; ++k)
                    {
                        const uint16_t ch = get16(e + CHARS[k]);
                        lfn[(seq - 1) * 13 + k] = ch == 0 || ch == 0xFFFF ? '\0' : ch < 0x80 ? static_cast<char>(ch) : '?';
                    }
                    --lfn_next;
                    continue;
                }
                const bool have_lfn = lfn_parts && lfn_next == 0 && checksum(e) == lfn_sum;
                lfn_parts = lfn_next = 0;
                if (e[11] & VOLUME)
                    continue;
                Entry entry;
                uint8_t base[8];
                std::copy(e, e + 8, base);
                if (base[0] == 0x05)
                    base[0] = DELETED; // a name really starting with $E5
                entry.short_name = field(base, 8, false);
                const std::string ext = field(e + 8, 3, false);
                if (!ext.empty())
                    entry.short_name += "." + ext;
                if (entry.short_name == "." || entry.short_name == "..")
                    continue;
                if (have_lfn)
                    entry.name = lfn.substr(0, lfn.find('\0'));
                else
                {
                    entry.name = field(base, 8, e[12] & LOWER_BASE);
                    const std::string lower_ext = field(e + 8, 3, e[12] & LOWER_EXT);
                    if (!lower_ext.empty())
                        entry.name += "." + lower_ext;
                }
                entry.attributes = e[11];
                entry.size = get32(e + 28);
                entry.cluster = (type_ == 32 ? uint32_t{get16(e + 20)} << 16 : 0) | get16(e + 26);
                out.push_back(std::move(entry));
            }
        }
        return Status::Ok;
    }

    FatVolume::Status FatVolume::find(const std::string &path, Entry &out) const
    {
        Entry at;
        at.attributes = DIRECTORY; // the root
        std::vector<Entry> dir;
        for (const std::string &part : split(path))
        {
            if (!(at.attributes & DIRECTORY))
                return Status::NotFound; // a file where a directory should be
            if (const Status st = read_directory(at.cluster, dir); st != Status::Ok)
                return st;
            const auto it = std::find_if(dir.begin(), dir.end(), [&part](const Entry &e) {
                return same(e.name, part) || same(e.short_name, part);
            });
            if (it == dir.end())
                return Status::NotFound;
            at = *it;
        }
        out = at;
        return Status::Ok;
    }

    FatVolume::Status FatVolume::list(const std::string &path, std::vector<Entry> &out) const
    {
        Entry dir;
        if (const Status st = find(path, dir); st != Status::Ok)
            return st;
        if (!(dir.attributes & DIRECTORY))
            return Status::NotFound;
        return read_directory(dir.cluster, out);
    }

    FatVolume::Status FatVolume::extents(const Entry &file, std::vector<Extent> &out) const
    {
        // The chain must hold exactly the clusters the size needs and then end: one that
        // loops, stops early or runs on is damaged (fsck's rule), and a loop never ends.
        // A last part-sector counts whole: the rest of it is the cluster's slack.
        out.clear();
        const uint64_t sectors = (uint64_t{file.size} + SECTOR - 1) / SECTOR;
        const uint64_t cluster_bytes = uint64_t{sectors_per_cluster_} * SECTOR;
        const uint64_t chain = (file.size + cluster_bytes - 1) / cluster_bytes;
        uint64_t mapped = 0;
        uint32_t c = file.cluster;
        for (uint64_t n = 0; n < chain; ++n)
        {
            if (!valid(c))
                return Status::Damaged;
            const uint64_t count = std::min<uint64_t>(sectors_per_cluster_, sectors - mapped);
            const uint64_t card = cluster_sector(c);
            if (count && !out.empty() && out.back().card + out.back().count == card)
                out.back().count += count;
            else if (count)
                out.push_back({mapped, card, count});
            mapped += count;
            c = next(c);
        }
        return chain && c != 0 ? Status::Damaged : Status::Ok;
    }
} // namespace ooe::emu
