#pragma once

/**
 * @file sd_images.h
 * @brief The SD card test images in sd/, from their sparse dumps.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * tools/make_sd_images.sh makes them with mkfs.fat, mtools and sfdisk and
 * keeps each as text: "oo1-sparse 1", "sectors N", then "LBA hex" for each
 * sector that isn't all zeros.
 */

#include <cstdint>
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
} // namespace ooe::test
