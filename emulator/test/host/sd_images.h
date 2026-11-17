#pragma once

/**
 * @file sd_images.h
 * @brief The SD card test images in sd/, from their sparse dumps.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * tools/make_sd_images.sh makes them with mkfs.fat, mtools and sfdisk and
 * keeps each as text: "oo1-sparse 1", "sectors N", then "LBA hex" for each
 * sector that isn't all zeros.
 *
 * fat_card::make_card() makes a card in memory instead, FAT16 from a list
 * of files' bytes, for a test that needs files the committed images haven't
 * (test_oodos_boot's refusals, test_bios_sd's flash update).
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace ooe::test
{
    /// The image's bytes; empty if the dump is missing or malformed.
    inline std::vector<uint8_t> sd_image(const std::string &name)
    {
        constexpr size_t SECTOR = 512;
        std::ifstream in(std::string(EMU_SD_DIR) + "/" + name);
        std::string magic, word;
        uint64_t sectors = 0;
        std::getline(in, magic);
        in >> word >> sectors;
        if (magic != "oo1-sparse 1" || word != "sectors" || sectors == 0)
            return {};
        std::vector<uint8_t> bytes(static_cast<size_t>(sectors) * SECTOR);
        uint64_t lba = 0;
        std::string hex;
        while (in >> lba >> hex)
        {
            if (lba >= sectors || hex.size() != 2 * SECTOR)
                return {};
            for (size_t i = 0; i < SECTOR; ++i)
                bytes[static_cast<size_t>(lba) * SECTOR + i] =
                    static_cast<uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
        }
        return bytes;
    }

    /// A FAT16 card made from files' bytes, as fat_volume.h reads one.
    namespace fat_card
    {
        using Bytes = std::vector<uint8_t>;
        constexpr size_t SECTOR = 512, ENTRY = 32;
        constexpr uint8_t DIRECTORY = 0x10; ///< a directory entry's attribute

        struct File
        {
            std::string path; ///< "/NAME.EXT" or "/OODOS/NAME.EXT"
            Bytes bytes;
            std::string long_name{}; ///< if set, a long name (13 characters at most) for the 8.3 one
        };

        inline void put16(Bytes &b, size_t at, unsigned v)
        {
            b[at] = static_cast<uint8_t>(v);
            b[at + 1] = static_cast<uint8_t>(v >> 8);
        }
        inline void put32(Bytes &b, size_t at, uint32_t v)
        {
            put16(b, at, v & 0xFFFF);
            put16(b, at + 2, v >> 16);
        }

        /// An 8.3 directory entry's name, "NAME.EXT" to "NAME    EXT".
        inline std::string name83(const std::string &n)
        {
            const size_t dot = n.find('.');
            std::string base = n.substr(0, dot), ext = dot == std::string::npos ? "" : n.substr(dot + 1);
            base.resize(8, ' ');
            ext.resize(3, ' ');
            return base + ext;
        }

        /// One long-name entry before a short one: `name` (at most 13 characters) for `short11`.
        inline void long_entry(uint8_t *e, const std::string &name, const std::string &short11)
        {
            uint8_t sum = 0;
            for (char c : short11)
                sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + static_cast<uint8_t>(c));
            std::memset(e, 0, ENTRY);
            e[0] = 0x41; // the first part, and the last
            e[11] = 0x0F;
            e[13] = sum;
            static const unsigned at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (unsigned k = 0; k < 13; ++k)
            {
                const unsigned ch = k < name.size() ? static_cast<uint8_t>(name[k]) : k == name.size() ? 0 : 0xFFFF;
                e[at[k]] = static_cast<uint8_t>(ch);
                e[at[k] + 1] = static_cast<uint8_t>(ch >> 8);
            }
        }

        /// FAT16 on the whole card (a superfloppy), 4 KB clusters, each file in one run: what
        /// fat_volume.h reads, with /OODOS a directory. Short names, and a long one where a File has it.
        inline Bytes make_card(const std::vector<File> &files)
        {
            constexpr unsigned SPC = 8, CLUSTER = SPC * SECTOR, ROOT_ENTRIES = 512;
            auto clusters_of = [](size_t n) { return static_cast<uint32_t>((n + CLUSTER - 1) / CLUSTER); };
            uint32_t used = 1; // /OODOS
            for (const File &f : files)
                used += clusters_of(f.bytes.size());
            const uint32_t clusters = std::max<uint32_t>(used + 16, 4200); // FAT16: at least 4,085
            const uint32_t fat_sectors = static_cast<uint32_t>(((clusters + 2) * 2 + SECTOR - 1) / SECTOR);
            const uint32_t root = 1 + fat_sectors, data = root + ROOT_ENTRIES * ENTRY / SECTOR;
            const uint32_t total = data + clusters * SPC;
            Bytes card(size_t{total} * SECTOR);

            uint8_t *b = card.data();
            b[0] = 0xEB, b[1] = 0x3C, b[2] = 0x90;
            std::memcpy(b + 3, "OOTEST  ", 8);
            put16(card, 11, SECTOR);
            b[13] = SPC;
            put16(card, 14, 1);  // reserved
            b[16] = 1;           // one FAT
            put16(card, 17, ROOT_ENTRIES);
            put32(card, 32, total);
            b[21] = 0xF8;
            put16(card, 22, fat_sectors);
            b[38] = 0x29;
            std::memcpy(b + 43, "NO NAME    FAT16   ", 19);
            b[510] = 0x55, b[511] = 0xAA;

            auto fat = [&](uint32_t c, unsigned v) { put16(card, SECTOR + 2 * size_t{c}, v); };
            fat(0, 0xFFF8);
            fat(1, 0xFFFF);
            uint32_t next = 2;
            auto chain = [&](size_t bytes) -> uint32_t {
                const uint32_t n = clusters_of(bytes), first = n ? next : 0;
                for (uint32_t i = 0; i < n; ++i, ++next)
                    fat(next, i + 1 < n ? next + 1 : 0xFFFF);
                return first;
            };
            auto at = [&](uint32_t c) { return (size_t{data} + size_t{c - 2} * SPC) * SECTOR; };
            auto entry = [&](size_t where, const std::string &name, uint8_t attr, uint32_t cluster, size_t size) {
                std::memcpy(&card[where], name.data(), 11);
                card[where + 11] = attr;
                put16(card, where + 26, cluster);
                put32(card, where + 28, static_cast<uint32_t>(size));
            };
            const uint32_t dir = chain(CLUSTER);
            size_t root_at = size_t{root} * SECTOR, dir_at = at(dir);
            entry(root_at, "OODOS      ", DIRECTORY, dir, 0);
            root_at += ENTRY;
            entry(dir_at, ".          ", DIRECTORY, dir, 0);
            entry(dir_at + ENTRY, "..         ", DIRECTORY, 0, 0);
            dir_at += 2 * ENTRY;
            for (const File &f : files)
            {
                const bool in_dir = f.path.rfind("/OODOS/", 0) == 0;
                const uint32_t first = chain(f.bytes.size());
                if (first)
                    std::copy(f.bytes.begin(), f.bytes.end(), card.begin() + static_cast<std::ptrdiff_t>(at(first)));
                size_t &where = in_dir ? dir_at : root_at;
                const std::string short11 = name83(f.path.substr(in_dir ? 7 : 1));
                if (!f.long_name.empty())
                {
                    long_entry(&card[where], f.long_name, short11);
                    where += ENTRY;
                }
                entry(where, short11, 0, first, f.bytes.size());
                where += ENTRY;
            }
            return card;
        }

    } // namespace fat_card
} // namespace ooe::test
