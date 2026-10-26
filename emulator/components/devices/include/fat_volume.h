#pragma once

/**
 * @file fat_volume.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief As much FAT as the SD card's firmware needs: find files, list directories, map a file's sectors.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The mass storage card (sd_card.h) mounts image files from a card a PC
 * formatted, so this reads what a PC writes: FAT12, FAT16 and FAT32, on the
 * whole card (a "superfloppy") or in the first FAT partition of an MBR, with
 * long file names. It only reads the file system: a mounted image's sectors
 * are written in place, in the clusters the file already has, so nothing in
 * the FAT changes.
 *
 * Paths are '/'-separated from the root; each part matches a long name or
 * the 8.3 name, ignoring case. Long names are kept as ASCII, anything else
 * as '?'. The type is FAT's rule, from the count of clusters.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sd_media.h"

namespace ooe::emu
{
    class FatVolume
    {
    public:
        static constexpr uint8_t READ_ONLY = 0x01, HIDDEN = 0x02, SYSTEM = 0x04, VOLUME = 0x08, DIRECTORY = 0x10;

        enum class Status : uint8_t
        {
            Ok,
            NotFound,  ///< no such entry, or a file where a directory should be
            Damaged,   ///< a cluster chain that ends early, runs on, loops or leaves the volume
        };

        struct Entry
        {
            std::string name;     ///< the long name, or the 8.3 one as NAME.EXT
            std::string short_name; ///< the 8.3 one, NAME.EXT, as stored
            uint8_t attributes{0};
            uint32_t size{0};     ///< bytes
            uint32_t cluster{0};  ///< the first; 0 for an empty file, or the root
        };

        /// A run of a file's sectors on the card.
        struct Extent
        {
            uint64_t first;   ///< the file's sector it starts at
            uint64_t card;    ///< where that is on the card
            uint64_t count;
        };

        /// The FAT volume on `media`, or nullptr with `error`. Not owned; must outlive it.
        static std::unique_ptr<FatVolume> open(SdMedia &media, std::string &error);

        unsigned type() const { return type_; } ///< 12, 16 or 32
        uint64_t first_sector() const { return start_; } ///< the volume's, on the card
        const std::string &label() const { return label_; }

        /// The entry at `path` ("/" or "" is the root, a directory).
        Status find(const std::string &path, Entry &out) const;
        /// A directory's entries, in order, without ".", "..", volume labels or deleted ones.
        Status list(const std::string &path, std::vector<Entry> &out) const;
        /// Where a file's sectors are on the card (its size rounded up: a last part-sector
        /// runs on into its cluster's slack), in order.
        Status extents(const Entry &file, std::vector<Extent> &out) const;

    private:
        explicit FatVolume(SdMedia &media) : media_(media) {}
        bool read_sector(uint64_t lba, uint8_t *out) const;
        /// The next cluster in the chain, or 0 at its end (or a bad entry).
        uint32_t next(uint32_t cluster) const;
        bool valid(uint32_t cluster) const { return cluster >= 2 && cluster < clusters_ + 2; }
        uint64_t cluster_sector(uint32_t cluster) const
        {
            return data_ + uint64_t{cluster - 2} * sectors_per_cluster_;
        }
        /// The directory at `cluster` (0: the root) read out.
        Status read_directory(uint32_t cluster, std::vector<Entry> &out) const;
        bool load(uint64_t start, std::string &error);

        SdMedia &media_;
        unsigned type_{0};
        uint64_t start_{0};               ///< the volume's first sector on the card
        uint32_t sectors_per_cluster_{0};
        uint64_t fat_{0};                 ///< the first FAT's first sector
        uint64_t root_{0};                ///< FAT12/16: the root directory's region
        uint32_t root_sectors_{0};
        uint32_t root_cluster_{0};        ///< FAT32: the root directory's first cluster
        uint64_t data_{0};                ///< cluster 2's first sector
        uint32_t clusters_{0};
        std::string label_;
        mutable uint64_t cached_{UINT64_MAX}; ///< the sector in cache_
        mutable std::array<uint8_t, SdMedia::SECTOR> cache_{};
    };
} // namespace ooe::emu
