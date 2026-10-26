/**
 * @file sd_media.cpp
 * @brief What is in the SD card slot.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sd_media.h"

#include <algorithm>

namespace ooe::emu
{
    namespace
    {
        /// The last part of a path, for IDENTIFY.
        std::string base_name(const std::string &path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    } // namespace

    MemoryMedia::MemoryMedia(std::vector<uint8_t> bytes, std::string name)
        : SdMedia(std::move(name)), bytes_(std::move(bytes))
    {
    }

    bool MemoryMedia::read(uint64_t lba, uint8_t *out)
    {
        if (lba >= sectors())
            return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(lba * SECTOR), SECTOR, out);
        return true;
    }

    bool MemoryMedia::write(uint64_t lba, const uint8_t *in)
    {
        if (lba >= sectors())
            return false;
        std::copy_n(in, SECTOR, bytes_.begin() + static_cast<std::ptrdiff_t>(lba * SECTOR));
        dirty_ = true;
        return true;
    }

    std::unique_ptr<FileMedia> FileMedia::open(const std::string &path, std::string &error)
    {
        bool writable = true;
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file)
        {
            writable = false;
            file.open(path, std::ios::in | std::ios::binary);
        }
        if (!file)
        {
            error = path + ": cannot open it";
            return nullptr;
        }
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        const uint64_t sectors = end > 0 ? static_cast<uint64_t>(end) / SECTOR : 0;
        if (!sectors)
        {
            error = path + ": not an SD card image (less than one 512-byte sector)";
            return nullptr;
        }
        std::unique_ptr<FileMedia> media(new FileMedia(std::move(file), sectors, path));
        media->set_write_protected(!writable);
        return media;
    }

    FileMedia::FileMedia(std::fstream file, uint64_t sectors, const std::string &path)
        : SdMedia(base_name(path)), file_(std::move(file)), sectors_(sectors)
    {
    }

    bool FileMedia::read(uint64_t lba, uint8_t *out)
    {
        if (lba >= sectors_)
            return false;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(lba * SECTOR));
        file_.read(reinterpret_cast<char *>(out), SECTOR);
        return static_cast<bool>(file_);
    }

    bool FileMedia::write(uint64_t lba, const uint8_t *in)
    {
        if (lba >= sectors_)
            return false;
        file_.clear();
        file_.seekp(static_cast<std::streamoff>(lba * SECTOR));
        file_.write(reinterpret_cast<const char *>(in), SECTOR);
        return static_cast<bool>(file_);
    }

    bool FileMedia::flush()
    {
        file_.clear();
        file_.flush();
        return static_cast<bool>(file_);
    }
} // namespace ooe::emu
