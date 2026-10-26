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
        constexpr unsigned NAME = 0x10, NAME_BYTES = 32;

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

    void SdCard::insert(std::shared_ptr<SdMedia> media, bool changed)
    {
        if (op_ != Op::None)
            fail(ERR_NO_CARD); // the old card went with it
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
            break;
        default:
            fail(ERR_COMMAND); // MOUNT, UNMOUNT and LIST included, for now
            return;
        }
        if (!ready())
        {
            fail(ERR_NO_CARD);
            return;
        }
        total_ = passed_ = worked_ = 0;
        media_lba_ = lba_;
        working_ = true;
        if (command == FLUSH)
        {
            op_ = Op::Flush;
            due_ = now_ + us(ACCESS_US);
            return;
        }
        if (command == IDENTIFY)
        {
            op_ = Op::Read;
            firmware_ = true;
            total_ = 1;
            due_ = now_ + us(ACCESS_US);
            return;
        }
        if (unit_ != 0)
        {
            fail(ERR_UNIT);
            return;
        }
        total_ = sectors_left();
        if (media_lba_ + total_ > media_->sectors())
        {
            fail(ERR_RANGE);
            return;
        }
        if (command == WRITE)
        {
            if (media_->write_protected())
            {
                fail(ERR_WP);
                return;
            }
            op_ = Op::Write;
            working_ = false; // nothing to program until a sector arrives
            return;
        }
        op_ = Op::Read;
        due_ = now_ + us(ACCESS_US);
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
            switch (op_)
            {
            case Op::Read:
            {
                uint8_t *into = buf_[(head_ + full_) % 2].data();
                if (firmware_)
                    identify(into);
                else if (!media_->read(media_lba_, into))
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
                if (!media_->write(media_lba_, buf_[head_].data()))
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
            case Op::Flush:
                if (!media_->flush())
                    fail(ERR_TIMEOUT);
                else
                    done();
                return;
            case Op::None:
                return;
            }
        }
    }

    void SdCard::sector_passed()
    {
        pos_ = 0;
        ++passed_;
        if (firmware_)
            return; // IDENTIFY's sector isn't the card's: LBA and COUNT stay
        ++lba_;
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
        ++full_;
        if (!working_)
        {
            working_ = true;
            due_ = now_ + us(SECTOR_US);
        }
    }

    void SdCard::identify(uint8_t *out) const
    {
        std::fill(out, out + SECTOR, uint8_t{0});
        if (!media_)
            return;
        const uint64_t sectors = media_->sectors();
        put32(out, std::min<uint64_t>(sectors, 0xFFFFFFFF));
        const uint64_t bytes = sectors * SECTOR;
        out[4] = bytes <= SDSC_MAX ? CARD_SDSC : bytes <= SDHC_MAX ? CARD_SDHC : CARD_SDXC;
        out[5] = media_->write_protected() ? 0x01 : 0x00;
        out[6] = VERSION_MAJOR;
        out[7] = VERSION_MINOR;
        const std::string &name = media_->name();
        std::copy_n(name.begin(), std::min<size_t>(name.size(), NAME_BYTES), out + NAME);
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
