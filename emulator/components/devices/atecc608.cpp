/**
 * @file atecc608.cpp
 * @brief The ATECC608B random number chip, as the north bridge sees it.
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 */

#include "atecc608.h"

#include <memory>

namespace ooe::emu
{
    Atecc608::Source Atecc608::splitmix(uint64_t seed)
    {
        struct State
        {
            uint64_t x;
            uint64_t word{0};
            unsigned left{0};
        };
        auto s = std::make_shared<State>(State{seed});
        return [s] {
            if (!s->left)
            {
                uint64_t z = (s->x += 0x9E3779B97F4A7C15ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                s->word = z ^ (z >> 31);
                s->left = 8;
            }
            --s->left;
            const uint8_t b = static_cast<uint8_t>(s->word);
            s->word >>= 8;
            return b;
        };
    }

    uint16_t Atecc608::crc(const uint8_t *data, size_t n)
    {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i)
            for (unsigned bit = 0; bit < 8; ++bit) // least significant bit first
            {
                const bool in = ((data[i] >> bit) & 1) != (c >> 15);
                c = static_cast<uint16_t>(c << 1);
                if (in)
                    c ^= 0x8005;
            }
        return c;
    }

    Atecc608::Atecc608(bool config_locked, bool data_locked, Source source)
        : config_locked_(config_locked), data_locked_(data_locked), source_(source ? std::move(source) : splitmix(1))
    {
    }

    void Atecc608::advance(uint64_t cycles)
    {
        now_ += cycles;
        if (awake_ && now_ >= watchdog_at_)
        {
            awake_ = false; // the watchdog: asleep, whatever it was doing
            pending_ = false;
            out_.clear();
            return;
        }
        if (pending_ && now_ >= busy_until_)
            finish();
    }

    void Atecc608::wake(uint64_t cycles)
    {
        if (awake_ || cycles < cycles_of(timing_.wake_low_us))
            return; // a wake while awake is ignored
        awake_ = true;
        ready_at_ = now_ + cycles_of(timing_.wake_delay_us);
        watchdog_at_ = now_ + cycles_of(timing_.watchdog_us);
        busy_until_ = 0;
        pending_ = false;
        status(ST_WOKEN);
    }

    bool Atecc608::address(bool read)
    {
        (void)read;
        if (pending_ && now_ >= busy_until_)
            finish();
        if (!awake_ || now_ < ready_at_ || busy())
            return false;
        if (nak_)
        {
            --nak_;
            return false;
        }
        return true;
    }

    void Atecc608::status(uint8_t code) { group({code}); }

    void Atecc608::group(const std::vector<uint8_t> &payload)
    {
        out_.assign(1, static_cast<uint8_t>(payload.size() + 3));
        out_.insert(out_.end(), payload.begin(), payload.end());
        const uint16_t c = crc(out_.data(), out_.size());
        out_.push_back(static_cast<uint8_t>(c));
        out_.push_back(static_cast<uint8_t>(c >> 8));
        at_ = 0;
    }

    void Atecc608::write(uint8_t word, const std::vector<uint8_t> &bytes)
    {
        switch (word)
        {
        case WA_RESET:
            at_ = 0;
            break;
        case WA_SLEEP:
        case WA_IDLE:
            awake_ = false;
            pending_ = false;
            out_.clear();
            break;
        case WA_COMMAND:
            execute(bytes);
            break;
        default:
            break;
        }
    }

    void Atecc608::execute(const std::vector<uint8_t> &g)
    {
        if (g.size() < 7 || g[0] != g.size() || crc(g.data(), g.size() - 2) != (g[g.size() - 2] | g[g.size() - 1] << 8))
        {
            status(ST_CRC);
            return;
        }
        const uint8_t op = g[1], mode = g[2];
        const uint16_t param2 = static_cast<uint16_t>(g[3] | g[4] << 8);
        uint32_t us = 0;
        result_.clear();
        if (op == OP_RANDOM && mode == 0)
        {
            ++randoms_;
            us = timing_.random_us;
            if (health_)
            {
                --health_;
                result_ = {ST_HEALTH};
            }
            else
                for (unsigned i = 0; i < 32; ++i)
                {
                    uint8_t b;
                    if (!config_locked_)
                        b = (i & 2) ? 0x00 : 0xFF; // the fixed pattern: $FF $FF $00 $00 ...
                    else if (constant_ >= 0)
                        b = static_cast<uint8_t>(constant_);
                    else
                        b = source_();
                    result_.push_back(b);
                }
        }
        else if (op == OP_READ && mode == 0 && param2 == LOCK_WORD)
        {
            us = timing_.read_us;
            result_ = {0x00, 0x00, static_cast<uint8_t>(data_locked_ ? 0x00 : UNLOCKED),
                       static_cast<uint8_t>(config_locked_ ? 0x00 : UNLOCKED)};
        }
        else
        {
            status(ST_EXEC);
            return;
        }
        busy_until_ = now_ + cycles_of(us);
        pending_ = true;
        out_.clear();
        at_ = 0;
    }

    void Atecc608::finish()
    {
        pending_ = false;
        group(result_);
        if (result_.size() == 32 && corrupt_)
        {
            --corrupt_;
            out_[5] ^= 0x40; // a data byte damaged on the way: the CRC no longer matches
        }
    }

    uint8_t Atecc608::read() { return at_ < out_.size() ? out_[at_++] : 0xFF; }
} // namespace ooe::emu
