#pragma once

/**
 * @file disk_image.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A 5.25" Apple disk as the Disk II sees it: each track's bits.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The Disk II card (disk2_card.h) reads and writes a stream of bits per
 * track, 4 us each, a 1 being a flux transition, so every image becomes
 * that when it is loaded:
 *
 * - .woz (WOZ 1 and 2, applesaucefdc.com/woz): the bits as captured, which
 *   is what copy-protected originals need. 160 quarter tracks map onto
 *   tracks (TMAP); an unmapped quarter track reads as an empty one.
 * - .dsk/.do and .po: 140 KB, 35 tracks of 16 sectors, written out the
 *   way DOS 3.3 formats a disk (volume 254): sync, then for each physical
 *   sector an address field and a 6-and-2 data field. Whether the file
 *   holds the sectors in DOS or ProDOS order is decided from its contents
 *   (a DOS catalog or a ProDOS volume directory where one order puts it),
 *   and only then from the name.
 *
 * Saving goes back the other way: to_woz() always works; to_sectors()
 * reads every track back as 16 sectors and refuses a disk where it cannot
 * (anything copy-protected, or written by a program with its own format).
 */

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ooe::emu
{
    /// One track's bits, the first in bit 7 of bytes[0].
    struct DiskTrack
    {
        std::vector<uint8_t> bytes;
        uint32_t bit_count{0};

        bool bit(uint32_t n) const { return (bytes[n >> 3] >> (7 - (n & 7))) & 1; }
        void set_bit(uint32_t n, bool on)
        {
            const uint8_t mask = static_cast<uint8_t>(0x80 >> (n & 7));
            bytes[n >> 3] = static_cast<uint8_t>(on ? bytes[n >> 3] | mask : bytes[n >> 3] & ~mask);
        }
    };

    class DiskImage
    {
    public:
        enum class Format : uint8_t
        {
            Woz,
            Dos,    ///< 140 KB, DOS 3.3 sector order (.dsk, .do)
            Prodos, ///< 140 KB, ProDOS order (.po)
        };
        static constexpr size_t SECTOR_IMAGE = 35 * 16 * 256; ///< 143,360 bytes
        static constexpr unsigned QUARTER_TRACKS = 160;
        static constexpr uint8_t NO_TRACK = 0xFF;
        static constexpr uint32_t BLANK_TRACK_BITS = 51'200; ///< a track written where there was none

        /// An image from a file's bytes; `name` only breaks a tie (.po). @return nullptr with `error`
        static std::unique_ptr<DiskImage> load(const std::vector<uint8_t> &bytes, const std::string &name,
                                               std::string &error);
        /// 35 tracks of 16 sectors, nibblized as DOS 3.3 formats them. @return nullptr if `image` is not SECTOR_IMAGE bytes
        static std::unique_ptr<DiskImage> from_sectors(const std::vector<uint8_t> &image, Format order,
                                                       uint8_t volume = 254);

        Format format() const { return format_; }
        /// The track under quarter track `q` (0-159), or nullptr: nothing there.
        DiskTrack *track(unsigned q);
        const DiskTrack *track(unsigned q) const;
        /// A track to write at quarter track `q`, made (blank) if there was none.
        DiskTrack &track_for_writing(unsigned q);

        bool write_protected() const { return write_protected_; }
        void set_write_protected(bool on) { write_protected_ = on; }
        /// Something was written since it was loaded (or since mark_saved()).
        bool changed() const { return changed_; }
        void mark_changed()
        {
            changed_ = true;
            ++writes_;
        }
        /// Counts writes, ever: whoever keeps a copy can tell it is behind.
        uint32_t writes() const { return writes_; }
        void mark_saved() { changed_ = false; }
        /// 125 ns units a bit takes (WOZ "optimal bit timing"): 32 = 4 us.
        uint8_t bit_timing() const { return bit_timing_; }

        /// The disk as a WOZ 2 file.
        std::vector<uint8_t> to_woz() const;
        /// The disk as 140 KB of sectors in `order`. @return false, with `error`, if a track is not 16 plain sectors
        bool to_sectors(Format order, std::vector<uint8_t> &out, std::string &error) const;

        /// The 64 disk bytes of the 6-and-2 code, in order (bit 7 set, no two zero bits together but one pair,
        /// two one bits together in bits 0-6, not $AA or $D5).
        static const std::array<uint8_t, 64> &code62();

    private:
        DiskImage() = default;
        static std::unique_ptr<DiskImage> load_woz(const std::vector<uint8_t> &bytes, std::string &error);

        Format format_{Format::Woz};
        std::array<uint8_t, QUARTER_TRACKS> tmap_{};
        std::vector<DiskTrack> tracks_;
        bool write_protected_{false};
        bool changed_{false};
        uint32_t writes_{0};
        uint8_t bit_timing_{32};
        uint8_t boot_format_{0}; ///< WOZ INFO: 0 unknown, 1 16-sector, 2 13-sector, 3 both
    };
} // namespace ooe::emu
