/**
 * @file memory_devices.cpp
 * @brief ROM image loading.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "memory_devices.h"

#include <fstream>
#include <iterator>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "rom";
    } // namespace

    bool Rom::load(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            EMU_LOGE(TAG, "could not open %s", path.c_str());
            return false;
        }
        const std::vector<char> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (image.size() > mem_.size())
        {
            EMU_LOGE(TAG, "%s: %zu bytes, but the ROM is %zu", path.c_str(), image.size(), mem_.size());
            return false;
        }
        for (size_t i = 0; i < image.size(); ++i)
            mem_[i] = static_cast<uint8_t>(image[i]);
        EMU_LOGI(TAG, "loaded %s (%zu bytes)", path.c_str(), image.size());
        return true;
    }
} // namespace ooe::emu
