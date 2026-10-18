/**
 * @file loader.cpp
 * @brief Raw binary image loader.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "loader.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "loader";
    } // namespace

    long load_image(Bus &bus, const std::string &path, uint16_t address)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            EMU_LOGE(TAG, "could not open %s", path.c_str());
            return -1;
        }

        const std::vector<char> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (address + image.size() > 0x10000)
        {
            EMU_LOGE(TAG, "%s: %zu bytes do not fit at $%04X", path.c_str(), image.size(), address);
            return -1;
        }

        for (size_t i = 0; i < image.size(); ++i)
            bus.poke(static_cast<uint16_t>(address + i), static_cast<uint8_t>(image[i]));

        EMU_LOGI(TAG, "loaded %s (%zu bytes) at $%04X", path.c_str(), image.size(), address);
        return static_cast<long>(image.size());
    }
} // namespace ooe::emu
