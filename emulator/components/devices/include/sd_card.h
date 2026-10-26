#pragma once

/**
 * @file sd_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's mass storage card (module ID 7): an SD card behind an RP2350.
 * @version 0.2.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Mass storage registers") is the spec. Registers at $C380:
 *
 *   $00 CTRL  $01 STATUS  $02 ERROR  $03 CMD  $04 UNIT  $06-$09 LBA  $0A-$0B COUNT
 *   $0C DATA  $0D-$0E BUFPTR  $10 IEN  $11 ISR  $7F ID
 *
 * An IDE disk: write UNIT, LBA and COUNT, then CMD, and the sectors stream
 * through DATA, a CPU loop or DMA channel 0 (CTRL bit 0) moving them. The
 * card holds two 512-byte buffers, so it fetches (or programs) one sector
 * while the bus empties (or fills) the other; DATA in STATUS is set while
 * the whole current sector is there to be read, or the whole buffer free to
 * be written, so a loop may wait for it once a sector and then move 512
 * bytes. With CTRL.DMA, DRQ follows it. LBA and COUNT advance as each
 * sector passes through DATA: during a transfer COUNT is the sectors left,
 * and after one LBA names the next sector.
 *
 * Commands:
 *
 * - READ $20, WRITE $30: COUNT sectors at LBA of UNIT, unit 0 being the
 *   whole card and 1-7 mounted image files, LBA 0 the file's first sector.
 * - FLUSH $E7: the media pushes out what it holds back (a host file to disk).
 * - IDENTIFY $EC: one sector out of DATA (below). LBA and COUNT stay.
 * - MOUNT $A0: one sector into DATA: +0 the unit (1-7), +1 flags (bit 0
 *   read-only), from +2 the file's path, NUL-terminated. The unit is the
 *   file's sectors, a last part-sector included (its cluster's slack after
 *   the end), read-only if the flag, the file's attribute or the
 *   card's switch says so; mounting a mounted unit replaces it.
 * - UNMOUNT $A1: UNIT (1-7) detached.
 * - LIST $A2: one sector into DATA, the directory's path from +0,
 *   NUL-terminated; then the entries out of DATA, COUNT set to the sectors
 *   they fill: 8 a sector of 64 bytes each, +0 the size (4 bytes), +4 the
 *   FAT attributes (bit 0 read-only, bit 4 a directory), +8 the name (the
 *   long one, 56 bytes, NUL padded; longer ones cut). An entry of zeros
 *   follows the last.
 *
 * MOUNT and LIST set COUNT to 1 for their sector, whatever was written to
 * it, and it counts down as it passes; it doesn't move LBA. Paths are '/'-separated from the card's root, matching long or
 * 8.3 names in any case (fat_volume.h). The firmware reads the FAT (whole
 * card, or the MBR's first FAT partition) at each MOUNT and LIST, so a card
 * changed through unit 0 is seen at the next one; a mounted unit's sectors
 * were found when it was mounted, and its writes go to the file's own
 * clusters, so the FAT never changes. Taking the card out unmounts all.
 *
 * BUSY is set from a command until it is done, when ISR bit 0 is set; a
 * failed command sets ERR and ERROR and stops at once. A command written
 * while BUSY is ignored; CTRL bit 1 (RESET) abandons it and starts the SD
 * card up again, which takes INIT_MS. So do power-up and putting a card in;
 * READY is set once it has.
 *
 * Timing, as an RP2350 reading SD in 4-bit mode would give it: a read's
 * first sector ACCESS_US after the command, each next one SECTOR_US after
 * that (into the spare buffer, so a DMA at a byte a cycle, 72 us a sector,
 * never waits after the first); a written sector programmed in SECTOR_US,
 * the command done once the last one is. Writes go to the media as each
 * sector is programmed. FLUSH, UNMOUNT, and MOUNT and LIST once their
 * sector is in, take ACCESS_US; a listing's sectors then follow, SECTOR_US
 * apart.
 *
 * IDENTIFY's sector: $000-$003 the card's sectors, $004 its type (0 none,
 * 1 SDSC up to 2 GB, 2 SDHC up to 32 GB, 3 SDXC), $005 flags (bit 0
 * write-protected, bit 1 a FAT volume found), $006-$007 the firmware's
 * version (major, minor), $010-$02F its name (ASCII, NUL padded: the
 * image's file name here), and from $040 a 32-byte entry for each of units
 * 1-7: sectors (4 bytes), flags (bit 0 mounted, bit 1 read-only), then from
 * +8 the file's name, 24 bytes. Everything else is 0.
 *
 * Interrupts: ISR bit 0 command done (with or without an error), bit 1 a
 * card put in or taken out, set whether IEN enables them or not; irq() is
 * ISR & IEN, which pulls bit 7 of IRQL. Write 1s to ISR to clear.
 *
 * /RESET abandons a command (sectors already programmed stay written) and
 * clears the registers; the card stays started up, and units stay mounted,
 * so a boot menu can mount an image and reset into it. DATA reads $FF, and
 * a write to it is dropped, when STATUS.DATA is clear. Reserved offsets read
 * $FF. In Apple II mode the card's block is hidden.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "device.h"
#include "fat_volume.h"
#include "sd_media.h"

namespace ooe::emu
{
    class SdCard : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0x7;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision
        static constexpr uint32_t NATIVE_HZ = 7'159'090;
        static constexpr size_t SECTOR = SdMedia::SECTOR;
        static constexpr uint8_t VERSION_MAJOR = 0, VERSION_MINOR = 2; ///< the firmware IDENTIFY reports
        static constexpr unsigned UNITS = 8;                          ///< 0, the card, and 1-7
        static constexpr size_t LIST_ENTRY = 64, LIST_NAME = 56;

        enum Reg : uint8_t
        {
            CTRL = 0x00,
            STATUS = 0x01,
            ERROR = 0x02,
            CMD = 0x03,
            UNIT = 0x04,
            LBA = 0x06,
            COUNT = 0x0A,
            DATA = 0x0C,
            BUFPTR = 0x0D,
            IEN = 0x10,
            ISR = 0x11,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t CTRL_DMA = 0x01, CTRL_RESET = 0x02;
        static constexpr uint8_t BUSY = 0x80, READY = 0x40, PRESENT = 0x20, WP = 0x10, DATA_READY = 0x08, ERR = 0x01;
        static constexpr uint8_t IRQ_DONE = 0x01, IRQ_CARD = 0x02;
        static constexpr uint8_t MOUNT_READ_ONLY = 0x01;
        static constexpr uint8_t UNIT_MOUNTED = 0x01, UNIT_READ_ONLY = 0x02; ///< IDENTIFY's unit flags
        static constexpr uint8_t ID_WP = 0x01, ID_FAT = 0x02;                ///< IDENTIFY's card flags
        enum Command : uint8_t
        {
            READ = 0x20,
            WRITE = 0x30,
            MOUNT = 0xA0,
            UNMOUNT = 0xA1,
            LIST = 0xA2,
            FLUSH = 0xE7,
            IDENTIFY = 0xEC,
        };
        enum Error : uint8_t
        {
            ERR_NONE = 0,
            ERR_NO_CARD = 1,   ///< none in the slot, or not started up yet
            ERR_RANGE = 2,     ///< past the card's (or unit's) last sector
            ERR_CRC = 3,       ///< the card's transfer failed its check
            ERR_TIMEOUT = 4,   ///< the card stopped answering (here: a host error)
            ERR_WP = 5,        ///< a write to a write-protected card or unit
            ERR_UNIT = 6,      ///< no image mounted as that unit, or not a unit 1-7
            ERR_COMMAND = 7,   ///< not a command the firmware knows
            ERR_NOT_FOUND = 8, ///< no such file (MOUNT) or directory (LIST)
            ERR_NO_FAT = 9,    ///< no FAT volume on the card, or a damaged one
        };
        static constexpr unsigned INIT_MS = 50, ACCESS_US = 100, SECTOR_US = 25;

        /// A mounted image file.
        struct Unit
        {
            bool mounted{false};
            bool read_only{false};
            uint64_t sectors{0};
            std::string name; ///< the file's, as the card has it
            std::vector<FatVolume::Extent> extents;
        };

        SdCard() { reset(); }
        /// Power-up: the registers cleared and the card (if any) starting up.
        void reset();
        /// The CPU clock tick()'s cycles run at (0: 7.16 MHz).
        void set_cpu_hz(uint32_t hz) { cpu_hz_ = hz ? hz : NATIVE_HZ; }

        // The slot
        /// Put a card in (replacing any): ISR bit 1, then it starts up. `changed` false: it
        /// was in the slot at power-up, which is no change.
        void insert(std::shared_ptr<SdMedia> media, bool changed = true);
        /// Take it out: a command in progress fails with ERR_NO_CARD.
        void eject();
        SdMedia *media() const { return media_.get(); }
        const Unit &unit(unsigned n) const { return units_[n % UNITS]; }
        /// Commands started since power-up, failed ones included: what a light shows.
        uint64_t commands() const { return commands_; }

        // Device: the registers at $C380
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        void bus_reset() override;
        bool irq() const override { return (isr_ & ien_ & (IRQ_DONE | IRQ_CARD)) != 0; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;
        bool drq() const override { return (ctrl_ & CTRL_DMA) && data_ready(); }
        uint8_t dack_read(bool tc) override;
        void dack_write(uint8_t data, bool tc) override;

    private:
        enum class Op : uint8_t
        {
            None,
            Read,   ///< sectors out of DATA: the card's, or the firmware's reply_
            Write,  ///< sectors into DATA: the card's, or MOUNT's or LIST's
            Work,   ///< the firmware busy for ACCESS_US: FLUSH, UNMOUNT, or MOUNT or LIST with their sector in
        };

        uint64_t us(uint64_t n) const { return uint64_t{cpu_hz_} * n / 1'000'000; }
        bool ready() const { return media_ && now_ >= ready_at_; }
        bool data_ready() const;
        void start(uint8_t command);
        void fail(Error error);
        void done();
        void abandon();
        void unmount_all();
        void sector_passed();
        void advance(); ///< the card's work, up to now_
        void work();    ///< the firmware's part, once ACCESS_US is up
        void mount(const uint8_t *param);
        void list(const uint8_t *param);
        /// Send reply_ out through DATA: its sectors, the first ready after `after` cycles.
        void reply(uint64_t after);
        bool card_sector(uint64_t lba, uint64_t &out) const; ///< op_unit_'s sector, on the card
        uint8_t take_byte();
        void put_byte(uint8_t data);
        void identify();
        uint32_t sectors_left() const { return count_ ? count_ : 0x10000; }

        uint32_t cpu_hz_{NATIVE_HZ};
        uint64_t now_{0};
        std::shared_ptr<SdMedia> media_;
        uint64_t ready_at_{0};            ///< cycle the card has started up
        std::array<Unit, UNITS> units_{}; ///< [0] unused: unit 0 is the card
        uint64_t commands_{0};

        // registers
        uint8_t ctrl_{0}, status_err_{0}, error_{0}, unit_{0}, ien_{0}, isr_{0};
        uint32_t lba_{0};
        uint16_t count_{0};

        // the command in progress
        Op op_{Op::None};
        uint8_t cmd_{0};
        unsigned op_unit_{0};   ///< the unit a READ or WRITE is on
        bool firmware_{false};  ///< the sectors are the firmware's (reply_ out, parameters in), not the card's
        std::vector<uint8_t> reply_;
        uint64_t total_{0};     ///< sectors in it
        uint64_t passed_{0};    ///< through DATA
        uint64_t worked_{0};    ///< fetched or programmed
        uint64_t media_lba_{0}; ///< the next sector the card fetches or programs, in op_unit_
        uint64_t due_{0};       ///< cycle the fetch, program or work under way finishes
        bool working_{false};   ///< one is under way
        std::array<std::array<uint8_t, SECTOR>, 2> buf_{};
        unsigned head_{0};      ///< the buffer DATA reads or (writing) the card programs next
        unsigned full_{0};      ///< buffers holding a sector: fetched, or written and not yet programmed
        uint16_t pos_{0};       ///< BUFPTR, in the sector at DATA
    };
} // namespace ooe::emu
