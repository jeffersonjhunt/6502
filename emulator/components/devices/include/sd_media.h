#pragma once

/**
 * @file sd_media.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief What is in the SD card slot: 512-byte sectors, in memory or in a host file.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The SD card (sd_card.h) reads and writes whole sectors through this, so
 * the same card runs on a memory image (the WebUI's, which hands the bytes
 * back to be saved) or on a file the terminal app writes through to. A
 * card's size is whole sectors: a file that is not is rounded down, and its
 * tail is left alone.
 */

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace ooe::emu
{
    class SdMedia
    {
    public:
        static constexpr size_t SECTOR = 512;

        virtual ~SdMedia() = default;
        virtual uint64_t sectors() const = 0;
        /// @return false if it can't (out of range, a host error)
        virtual bool read(uint64_t lba, uint8_t *out) = 0;
        virtual bool write(uint64_t lba, const uint8_t *in) = 0;
        /// Anything held back, out to the host. @return false on a host error
        virtual bool flush() { return true; }
        /// The write-protect switch on the card's side.
        bool write_protected() const { return write_protected_; }
        void set_write_protected(bool on) { write_protected_ = on; }
        /// What IDENTIFY calls it: the file's name, for an image.
        const std::string &name() const { return name_; }

    protected:
        explicit SdMedia(std::string name) : name_(std::move(name)) {}

    private:
        std::string name_;
        bool write_protected_{false};
    };

    /// A card held in memory. Writes change bytes() and set dirty().
    class MemoryMedia : public SdMedia
    {
    public:
        MemoryMedia(std::vector<uint8_t> bytes, std::string name);
        uint64_t sectors() const override { return bytes_.size() / SECTOR; }
        bool read(uint64_t lba, uint8_t *out) override;
        bool write(uint64_t lba, const uint8_t *in) override;
        const std::vector<uint8_t> &bytes() const { return bytes_; }
        /// Written to since it was made, or since clean().
        bool dirty() const { return dirty_; }
        void clean() { dirty_ = false; }

    private:
        std::vector<uint8_t> bytes_;
        bool dirty_{false};
    };

    /// A card that is a host file, written through: each sector goes to the file as it is
    /// written, flush() pushes it to the host. A file that can't be written is write-protected.
    class FileMedia : public SdMedia
    {
    public:
        /// @return nullptr, with `error`, if it can't be opened or holds less than a sector
        static std::unique_ptr<FileMedia> open(const std::string &path, std::string &error);
        uint64_t sectors() const override { return sectors_; }
        bool read(uint64_t lba, uint8_t *out) override;
        bool write(uint64_t lba, const uint8_t *in) override;
        bool flush() override;

    private:
        FileMedia(std::fstream file, uint64_t sectors, const std::string &path);
        std::fstream file_;
        uint64_t sectors_;
    };
} // namespace ooe::emu
