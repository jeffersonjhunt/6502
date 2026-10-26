/**
 * @file disk2_card.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The Disk II controller: the sequencer, the drives, slot 6's switches.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "disk2_card.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

#include "decoder.h"

namespace ooe::emu
{
    namespace
    {
        constexpr unsigned STEPS_PER_CYCLE = 2; ///< the sequencer's 2 MHz against the CPU's 1 MHz
        constexpr unsigned STEP = 4;            ///< 125 ns units (WOZ bit timing's) a step takes
        constexpr uint64_t SKIP_AFTER = 4096;   ///< steps: a longer gap in read mode skips ahead
        constexpr uint64_t SETTLE = 64;         ///< steps run at the end of a skip

        bool load256(const std::string &path, std::array<uint8_t, 256> &out, std::string &error)
        {
            std::ifstream in(path, std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!in.good() && !in.eof())
            {
                error = path + ": cannot read it";
                return false;
            }
            if (bytes.size() != out.size())
            {
                error = path + ": want a 256-byte Disk II PROM, got " + std::to_string(bytes.size()) + " bytes";
                return false;
            }
            std::copy(bytes.begin(), bytes.end(), reinterpret_cast<char *>(out.data()));
            return true;
        }
    } // namespace

    bool Disk2Card::load_roms(const std::string &boot, const std::string &sequencer, std::string &error)
    {
        if (!boot.empty() && !load256(boot, boot_, error))
            return false;
        if (!sequencer.empty() && !load256(sequencer, sequencer_, error))
            return false;
        has_boot_ = !boot.empty();
        has_sequencer_ = !sequencer.empty();
        return true;
    }

    void Disk2Card::insert(unsigned drive, std::shared_ptr<DiskImage> disk)
    {
        catch_up(now());
        Drive &d = drives_[drive % DRIVES];
        d.disk = std::move(disk);
        d.pos = 0;
    }

    uint64_t Disk2Card::now() const { return decoder_ ? decoder_->now() : synced_; }

    unsigned Disk2Card::cell_length(const Drive &d) const { return d.disk ? d.disk->bit_timing() : 32; }

    uint32_t Disk2Card::track_bits(const Drive &d) const
    {
        const DiskTrack *t = d.disk ? d.disk->track(d.quarter) : nullptr;
        return t ? t->bit_count : DiskImage::BLANK_TRACK_BITS;
    }

    void Disk2Card::catch_up(uint64_t cycle)
    {
        if (cycle <= synced_)
            return;
        if (spinning_ && !motor_ && cycle >= stops_at_)
        {
            if (stops_at_ > synced_)
                run((stops_at_ - synced_) * STEPS_PER_CYCLE);
            spinning_ = false; // run on, and stopped
        }
        else if (spinning_)
            run((cycle - synced_) * STEPS_PER_CYCLE);
        synced_ = cycle;
    }

    void Disk2Card::run(uint64_t steps)
    {
        if (!q7_ && steps > SKIP_AFTER)
        {
            // Reading with nobody looking: the disk turns on, and only the last few steps matter.
            Drive &d = drives_[drive_];
            const uint64_t skip = steps - SETTLE, cell = cell_length(d);
            const uint64_t cells = (cell_ + skip * STEP) / cell;
            cell_ = static_cast<unsigned>((cell_ + skip * STEP) % cell);
            d.pos = static_cast<uint32_t>((d.pos + cells) % track_bits(d));
            steps = SETTLE;
        }
        for (uint64_t i = 0; i < steps; ++i)
            step();
    }

    bool Disk2Card::noise()
    {
        noise_ ^= noise_ << 13; // xorshift32
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return noise_ % 10 < 3;
    }

    void Disk2Card::step()
    {
        Drive &d = drives_[drive_];
        const DiskTrack *t = d.disk ? d.disk->track(d.quarter) : nullptr;
        const unsigned cell = cell_length(d);
        bool pulse = false;
        if (cell_ < STEP && !q7_) // the cell's first step
        {
            bool bit = t && t->bit(d.pos % t->bit_count);
            if (bit)
                zeros_ = 0;
            else if (++zeros_ > 3)
                bit = noise(); // the amplifier, finding no flux, finds noise
            pulse = bit;
        }
        const bool wp = !d.disk || d.disk->write_protected();
        const uint8_t address = static_cast<uint8_t>((state_ & 0x80) | (state_ & 0x40) | (state_ & 0x20) >> 5 |
                                                     (state_ & 0x10) << 1 | (pulse ? 0 : 0x10) | (q7_ ? 0x08 : 0) |
                                                     (q6_ ? 0x04 : 0) | (data_ & 0x80) >> 6);
        const uint8_t op = has_sequencer_ ? sequencer_[address] : 0;
        switch (op & 0x0F)
        {
        case 0x8:
        case 0xC:
            break;
        case 0x9:
            data_ = static_cast<uint8_t>(data_ << 1);
            break;
        case 0xD:
            data_ = static_cast<uint8_t>(data_ << 1 | 1);
            break;
        case 0xA:
        case 0xE:
            data_ = static_cast<uint8_t>(data_ >> 1 | (wp ? 0x80 : 0));
            break;
        case 0xB:
        case 0xF:
            data_ = bus_;
            break;
        default:
            data_ = 0;
            break;
        }
        state_ = static_cast<uint8_t>(op & 0xF0);
        if (cell_ + STEP >= cell && q7_) // its last
        {
            const bool level = (state_ & 0x80) != 0;
            if (d.disk && !wp)
            {
                d.disk->track_for_writing(d.quarter).set_bit(d.pos % track_bits(d), level != level_);
                d.disk->mark_changed();
            }
            level_ = level;
        }
        if ((cell_ += STEP) >= cell)
        {
            cell_ = (cell_ - cell) % cell; // (a drive switched to a shorter cell: start the next)
            d.pos = (d.pos + 1) % track_bits(d);
        }
    }

    void Disk2Card::move_head()
    {
        Drive &d = drives_[drive_];
        const int at = static_cast<int>(d.quarter);
        int sum = 0, pulling = 0;
        for (int m = 0; m < 4; ++m)
        {
            if (!(phases_ & (1 << m)))
                continue;
            int dist = ((2 * m - at) % 8 + 8) % 8; // a magnet every half track, the four repeating
            if (dist >= 4)
                dist -= 8;
            if (dist >= -2 && dist <= 2)
            {
                sum += dist;
                ++pulling;
            }
        }
        if (!pulling)
            return;
        const int to = std::clamp(at + sum / pulling, 0, static_cast<int>(DiskImage::QUARTER_TRACKS) - 1);
        if (to == at)
            return;
        const uint32_t from_bits = track_bits(d);
        d.quarter = static_cast<unsigned>(to);
        d.pos = static_cast<uint32_t>(uint64_t{d.pos} * track_bits(d) / from_bits); // the same place round
    }

    uint8_t Disk2Card::slot_io(unsigned n, bool write, uint8_t data, bool effects)
    {
        n &= 0x0F;
        if (!effects)
            return n & 1 ? FLOATING : data_;
        const uint64_t cycle = now();
        catch_up(cycle ? cycle - 1 : 0); // up to this access
        if (write)
            bus_ = data;
        switch (n >> 1)
        {
        case 0:
        case 1:
        case 2:
        case 3:
        {
            const uint8_t bit = static_cast<uint8_t>(1u << (n >> 1));
            phases_ = static_cast<uint8_t>(n & 1 ? phases_ | bit : phases_ & ~bit);
            if (spinning_)
                move_head();
            break;
        }
        case 4:
            if (n & 1)
            {
                motor_ = spinning_ = true;
            }
            else if (motor_)
            {
                motor_ = false;
                stops_at_ = cycle + MOTOR_DELAY;
            }
            break;
        case 5:
            drive_ = n & 1;
            break;
        case 6:
            q6_ = n & 1;
            break;
        default:
            q7_ = n & 1;
            break;
        }
        catch_up(cycle); // this cycle, as switched
        return n & 1 ? FLOATING : data_;
    }

    void Disk2Card::reset()
    {
        catch_up(now());
        phases_ = 0;
        q6_ = q7_ = false;
        drive_ = 0;
        if (motor_)
        {
            motor_ = false;
            stops_at_ = now() + MOTOR_DELAY;
        }
    }

    Disk2Card::Status Disk2Card::status()
    {
        catch_up(now());
        Status s;
        s.motor = motor_;
        s.spinning = spinning_;
        s.drive = static_cast<uint8_t>(drive_);
        s.q6 = q6_;
        s.q7 = q7_;
        s.data = data_;
        s.phases = phases_;
        for (unsigned i = 0; i < DRIVES; ++i)
            s.quarter_track[i] = static_cast<uint8_t>(drives_[i].quarter);
        return s;
    }
} // namespace ooe::emu
