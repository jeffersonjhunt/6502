/**
 * @file sd_card.cpp
 * @brief The backplane's mass storage card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sd_card.h"

#include <algorithm>

namespace ooe::emu
{
    namespace
    {
        constexpr uint64_t SDSC_MAX = 2ull << 30, SDHC_MAX = 32ull << 30; ///< bytes
        constexpr uint8_t CARD_SDSC = 1, CARD_SDHC = 2, CARD_SDXC = 3;
        constexpr unsigned NAME = 0x10, NAME_BYTES = 32, UNIT_TABLE = 0x40, UNIT_ENTRY = 32, UNIT_NAME = 24;

        void put32(uint8_t *at, uint64_t v)
        {
            for (unsigned i = 0; i < 4; ++i)
                at[i] = static_cast<uint8_t>(v >> (8 * i));
        }
    } // namespace

    void SdCard::reset()
    {
        now_ = 0;
        ready_at_ = us(uint64_t{INIT_MS} * 1000);
        abandon();
        unmount_all(); // the RP2350 starts afresh
        ctrl_ = status_err_ = error_ = unit_ = ien_ = isr_ = 0;
        lba_ = 0;
        count_ = 0;
    }

    void SdCard::bus_reset()
    {
        abandon();
        ctrl_ = status_err_ = error_ = unit_ = ien_ = isr_ = 0;
        lba_ = 0;
        count_ = 0;
    }

    void SdCard::unmount_all()
    {
        for (Unit &u : units_)
            u = Unit{};
    }

    void SdCard::insert(std::shared_ptr<SdMedia> media, bool changed)
    {
        if (op_ != Op::None)
            fail(ERR_NO_CARD); // the old card went with it
        unmount_all();
        media_ = std::move(media);
        ready_at_ = now_ + us(uint64_t{INIT_MS} * 1000);
        if (changed)
            isr_ = static_cast<uint8_t>(isr_ | IRQ_CARD);
    }

    void SdCard::eject()
    {
        if (!media_)
            return;
        if (op_ != Op::None)
            fail(ERR_NO_CARD);
        unmount_all();
        media_.reset();
        isr_ = static_cast<uint8_t>(isr_ | IRQ_CARD);
    }

    void SdCard::tick(uint64_t cycles)
    {
        now_ += cycles;
        advance();
    }

    bool SdCard::data_ready() const
    {
        switch (op_)
        {
        case Op::Read:
            return full_ > 0;
        case Op::Write:
            return passed_ < total_ && full_ < 2;
        default:
            return false;
        }
    }

    void SdCard::abandon()
    {
        op_ = Op::None;
        working_ = firmware_ = false;
        full_ = head_ = 0;
        pos_ = 0;
    }

    void SdCard::fail(Error error)
    {
        abandon();
        error_ = error;
        status_err_ = ERR;
        isr_ = static_cast<uint8_t>(isr_ | IRQ_DONE);
    }

    void SdCard::done()
    {
        abandon();
        isr_ = static_cast<uint8_t>(isr_ | IRQ_DONE);
    }

    void SdCard::start(uint8_t command)
    {
        if (op_ != Op::None)
            return; // BUSY: the firmware isn't listening
        status_err_ = error_ = 0;
        switch (command)
        {
        case READ:
        case WRITE:
        case FLUSH:
        case IDENTIFY:
        case MOUNT:
        case UNMOUNT:
        case LIST:
            break;
        default:
            fail(ERR_COMMAND);
            return;
        }
        if (!ready())
        {
            fail(ERR_NO_CARD);
            return;
        }
        abandon();
        cmd_ = command;
        op_unit_ = unit_;
        total_ = passed_ = worked_ = 0;
        media_lba_ = lba_;
        switch (command)
        {
        case FLUSH:
            op_ = Op::Work;
            working_ = true;
            due_ = now_ + us(ACCESS_US);
            return;
        case UNMOUNT:
            if (!units_[op_unit_].mounted)
            {
                fail(ERR_UNIT); // unit 0 never is
                return;
            }
            op_ = Op::Work;
            working_ = true;
            due_ = now_ + us(ACCESS_US);
            return;
        case IDENTIFY:
            identify();
            reply(us(ACCESS_US));
            return;
        case MOUNT:
        case LIST:
            op_ = Op::Write; // their sector, into DATA
            firmware_ = true;
            total_ = 1;
            count_ = 1;
            return;
        default:
            break;
        }
        const Unit &u = units_[op_unit_];
        if (op_unit_ != 0 && !u.mounted)
        {
            fail(ERR_UNIT);
            return;
        }
        total_ = sectors_left();
        if (media_lba_ + total_ > (op_unit_ ? u.sectors : media_->sectors()))
        {
            fail(ERR_RANGE);
            return;
        }
        if (command == WRITE)
        {
            if (media_->write_protected() || u.read_only)
            {
                fail(ERR_WP);
                return;
            }
            op_ = Op::Write; // nothing to program until a sector arrives
            return;
        }
        op_ = Op::Read;
        working_ = true;
        due_ = now_ + us(ACCESS_US);
    }

    void SdCard::reply(uint64_t after)
    {
        op_ = Op::Read;
        firmware_ = true;
        total_ = reply_.size() / SECTOR;
        passed_ = worked_ = 0;
        full_ = head_ = 0;
        pos_ = 0;
        working_ = true;
        due_ = now_ + after;
    }

    bool SdCard::card_sector(uint64_t lba, uint64_t &out) const
    {
        if (op_unit_ == 0)
        {
            out = lba;
            return true;
        }
        const std::vector<FatVolume::Extent> &ext = units_[op_unit_].extents;
        auto it = std::upper_bound(ext.begin(), ext.end(), lba,
                                   [](uint64_t v, const FatVolume::Extent &e) { return v < e.first; });
        if (it == ext.begin())
            return false;
        --it;
        if (lba >= it->first + it->count)
            return false;
        out = it->card + (lba - it->first);
        return true;
    }

    void SdCard::advance()
    {
        while (op_ != Op::None && working_ && now_ >= due_)
        {
            if (!media_)
            {
                fail(ERR_NO_CARD);
                return;
            }
            uint64_t lba = 0;
            switch (op_)
            {
            case Op::Read:
            {
                uint8_t *into = buf_[(head_ + full_) % 2].data();
                if (firmware_)
                    std::copy_n(reply_.begin() + static_cast<std::ptrdiff_t>(worked_ * SECTOR), SECTOR, into);
                else if (!card_sector(media_lba_, lba) || !media_->read(lba, into))
                {
                    fail(ERR_TIMEOUT);
                    return;
                }
                ++full_;
                ++worked_;
                ++media_lba_;
                working_ = worked_ < total_ && full_ < 2;
                due_ += us(SECTOR_US);
                break;
            }
            case Op::Write:
                if (!card_sector(media_lba_, lba) || !media_->write(lba, buf_[head_].data()))
                {
                    fail(ERR_TIMEOUT);
                    return;
                }
                head_ ^= 1;
                --full_;
                ++worked_;
                ++media_lba_;
                if (worked_ == total_)
                {
                    done();
                    return;
                }
                working_ = full_ > 0;
                due_ += us(SECTOR_US);
                break;
            case Op::Work:
                work(); // may go on to a reply, which this loop then fetches
                break;
            case Op::None:
                return;
            }
        }
    }

    void SdCard::work()
    {
        switch (cmd_)
        {
        case FLUSH:
            if (!media_->flush())
                fail(ERR_TIMEOUT);
            else
                done();
            return;
        case UNMOUNT:
            units_[op_unit_] = Unit{};
            done();
            return;
        case MOUNT:
            mount(buf_[head_].data());
            return;
        case LIST:
            list(buf_[head_].data());
            return;
        default:
            done();
            return;
        }
    }

    namespace
    {
        /// A NUL-terminated path in a parameter sector. @return false if it runs off the end
        bool path_at(const uint8_t *p, size_t room, std::string &out)
        {
            const uint8_t *end = std::find(p, p + room, uint8_t{0});
            if (end == p + room)
                return false;
            out.assign(reinterpret_cast<const char *>(p), static_cast<size_t>(end - p));
            return true;
        }
    } // namespace

    void SdCard::mount(const uint8_t *param)
    {
        const unsigned n = param[0];
        std::string path, error;
        if (n < 1 || n >= UNITS)
            return fail(ERR_UNIT);
        if (!path_at(param + 2, SECTOR - 2, path))
            return fail(ERR_NOT_FOUND);
        const std::unique_ptr<FatVolume> volume = FatVolume::open(*media_, error);
        if (!volume)
            return fail(ERR_NO_FAT);
        FatVolume::Entry file;
        Unit u;
        switch (volume->find(path, file))
        {
        case FatVolume::Status::Ok:
            break;
        case FatVolume::Status::NotFound:
            return fail(ERR_NOT_FOUND);
        case FatVolume::Status::Damaged:
            return fail(ERR_NO_FAT);
        }
        if (file.attributes & FatVolume::DIRECTORY)
            return fail(ERR_NOT_FOUND);
        if (volume->extents(file, u.extents) != FatVolume::Status::Ok)
            return fail(ERR_NO_FAT);
        u.mounted = true;
        u.read_only = (param[1] & MOUNT_READ_ONLY) || (file.attributes & FatVolume::READ_ONLY);
        u.sectors = file.size / SECTOR;
        u.name = file.name;
        units_[n] = std::move(u);
        done();
    }

    void SdCard::list(const uint8_t *param)
    {
        std::string path, error;
        if (!path_at(param, SECTOR, path))
            return fail(ERR_NOT_FOUND);
        const std::unique_ptr<FatVolume> volume = FatVolume::open(*media_, error);
        if (!volume)
            return fail(ERR_NO_FAT);
        std::vector<FatVolume::Entry> entries;
        switch (volume->list(path, entries))
        {
        case FatVolume::Status::Ok:
            break;
        case FatVolume::Status::NotFound:
            return fail(ERR_NOT_FOUND);
        case FatVolume::Status::Damaged:
            return fail(ERR_NO_FAT);
        }
        const size_t per = SECTOR / LIST_ENTRY;
        const size_t sectors = entries.size() / per + 1; // always room for the zero entry after the last
        reply_.assign(sectors * SECTOR, 0);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            uint8_t *e = reply_.data() + i * LIST_ENTRY;
            for (unsigned k = 0; k < 4; ++k)
                e[k] = static_cast<uint8_t>(entries[i].size >> (8 * k));
            e[4] = entries[i].attributes;
            const std::string &name = entries[i].name;
            std::copy_n(name.begin(), std::min(name.size(), LIST_NAME), e + 8);
        }
        count_ = static_cast<uint16_t>(sectors);
        reply(0);
    }

    void SdCard::sector_passed()
    {
        pos_ = 0;
        ++passed_;
        if (!firmware_)
            ++lba_; // the firmware's sectors aren't the card's
        if (cmd_ != IDENTIFY)
            --count_;
    }

    uint8_t SdCard::take_byte()
    {
        if (op_ != Op::Read || !full_)
            return 0xFF;
        const uint8_t v = buf_[head_][pos_];
        if (++pos_ < SECTOR)
            return v;
        sector_passed();
        head_ ^= 1;
        --full_;
        if (passed_ == total_)
            done();
        else if (!working_ && worked_ < total_)
        {
            working_ = true; // a buffer is free again: fetch into it
            due_ = now_ + us(SECTOR_US);
        }
        return v;
    }

    void SdCard::put_byte(uint8_t data)
    {
        if (op_ != Op::Write || !data_ready())
            return;
        buf_[(head_ + full_) % 2][pos_] = data;
        if (++pos_ < SECTOR)
            return;
        sector_passed();
        if (firmware_)
        {
            op_ = Op::Work; // MOUNT's or LIST's sector, in buf_[head_]
            working_ = true;
            due_ = now_ + us(ACCESS_US);
            return;
        }
        ++full_;
        if (!working_)
        {
            working_ = true;
            due_ = now_ + us(SECTOR_US);
        }
    }

    void SdCard::identify()
    {
        reply_.assign(SECTOR, 0);
        uint8_t *out = reply_.data();
        const uint64_t sectors = media_->sectors();
        put32(out, std::min<uint64_t>(sectors, 0xFFFFFFFF));
        const uint64_t bytes = sectors * SECTOR;
        out[4] = bytes <= SDSC_MAX ? CARD_SDSC : bytes <= SDHC_MAX ? CARD_SDHC : CARD_SDXC;
        std::string error;
        out[5] = static_cast<uint8_t>((media_->write_protected() ? ID_WP : 0) |
                                      (FatVolume::open(*media_, error) ? ID_FAT : 0));
        out[6] = VERSION_MAJOR;
        out[7] = VERSION_MINOR;
        const std::string &name = media_->name();
        std::copy_n(name.begin(), std::min<size_t>(name.size(), NAME_BYTES), out + NAME);
        for (unsigned n = 1; n < UNITS; ++n)
        {
            const Unit &u = units_[n];
            if (!u.mounted)
                continue;
            uint8_t *e = out + UNIT_TABLE + UNIT_ENTRY * (n - 1);
            put32(e, std::min<uint64_t>(u.sectors, 0xFFFFFFFF));
            e[4] = static_cast<uint8_t>(UNIT_MOUNTED | (u.read_only ? UNIT_READ_ONLY : 0));
            std::copy_n(u.name.begin(), std::min<size_t>(u.name.size(), UNIT_NAME), e + 8);
        }
    }

    uint8_t SdCard::read(uint32_t offset)
    {
        if (offset == DATA)
            return take_byte();
        return peek(offset);
    }

    uint8_t SdCard::peek(uint32_t offset) const
    {
        switch (offset)
        {
        case CTRL:
            return ctrl_;
        case STATUS:
            return static_cast<uint8_t>((op_ != Op::None ? BUSY : 0) | (ready() ? READY : 0) |
                                        (media_ ? PRESENT : 0) | (media_ && media_->write_protected() ? WP : 0) |
                                        (data_ready() ? DATA_READY : 0) | status_err_);
        case ERROR:
            return error_;
        case UNIT:
            return unit_;
        case LBA:
        case LBA + 1:
        case LBA + 2:
        case LBA + 3:
            return static_cast<uint8_t>(lba_ >> (8 * (offset - LBA)));
        case COUNT:
        case COUNT + 1:
            return static_cast<uint8_t>(count_ >> (8 * (offset - COUNT)));
        case DATA:
            return op_ == Op::Read && full_ ? buf_[head_][pos_] : 0xFF;
        case BUFPTR:
        case BUFPTR + 1:
            return static_cast<uint8_t>(pos_ >> (8 * (offset - BUFPTR)));
        case IEN:
            return ien_;
        case ISR:
            return isr_;
        case ID_REG:
            return ID;
        default:
            return 0xFF; // CMD is write-only; the rest reserved
        }
    }

    void SdCard::write(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
        case CTRL:
            ctrl_ = data & CTRL_DMA;
            if (data & CTRL_RESET)
            {
                abandon();
                ready_at_ = now_ + us(uint64_t{INIT_MS} * 1000);
            }
            break;
        case CMD:
            start(data);
            break;
        case UNIT:
            unit_ = data & (UNITS - 1);
            break;
        case LBA:
        case LBA + 1:
        case LBA + 2:
        case LBA + 3:
        {
            const unsigned shift = 8 * (offset - LBA);
            lba_ = (lba_ & ~(0xFFu << shift)) | uint32_t{data} << shift;
            break;
        }
        case COUNT:
        case COUNT + 1:
        {
            const unsigned shift = 8 * (offset - COUNT);
            count_ = static_cast<uint16_t>((count_ & ~(0xFFu << shift)) | unsigned{data} << shift);
            break;
        }
        case DATA:
            put_byte(data);
            break;
        case BUFPTR:
            pos_ = static_cast<uint16_t>((pos_ & 0x100) | data);
            break;
        case BUFPTR + 1:
            pos_ = static_cast<uint16_t>((pos_ & 0xFF) | (data & 1) << 8);
            break;
        case IEN:
            ien_ = data & (IRQ_DONE | IRQ_CARD);
            break;
        case ISR:
            isr_ = static_cast<uint8_t>(isr_ & ~data);
            break;
        default:
            break;
        }
    }

    uint8_t SdCard::dack_read(bool) { return take_byte(); }

    void SdCard::dack_write(uint8_t data, bool) { put_byte(data); }
} // namespace ooe::emu
