/**
 * @file usb_card.cpp
 * @brief The backplane's USB card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "usb_card.h"

#include <algorithm>
#include <cmath>

namespace ooe::emu
{
    namespace
    {
        constexpr uint8_t L_CTRL = 0x01, L_SHIFT = 0x02, R_CTRL = 0x10, R_SHIFT = 0x20; ///< MODS bits
        constexpr unsigned PADDLE_US = 11;                                                  ///< per count

        /// US layout for usages $1E-$38, unshifted then shifted.
        constexpr char ROW[] = "1234567890\r\x1b\b\t -=[]\\#;'`,./";
        constexpr char ROW_SHIFT[] = "!@#$%^&*()\r\x1b\b\t _+{}|~:\"~<>?";
    } // namespace

    void UsbCard::reset()
    {
        reset_base_ += now_; // the speaker's time runs on through resets
        now_ = 0;
        key_ = mods_ = leds_ = kbcfg_ = 0;
        repeat_ = 0x10;
        caps_ = true;
        nmi_ = reset_ = false;
        fifo_head_ = fifo_count_ = 0;
        held_ = 0;
        down_ = {};
        pending_.clear();
        next_report_ = 0;
        mctrl_ = mbtn_ = 0;
        sum_x_ = sum_y_ = sum_wheel_ = pos_x_ = pos_y_ = 0;
        max_x_ = 639;
        max_y_ = 479;
        latch_x_ = latch_y_ = 0;
        latch_wheel_ = 0;
        paddles_ = {};
        game_buttons_ = 0;
        triggered_ = false;
        ien_ = isr_ = 0;
        now_ = 0;
    }

    void UsbCard::bus_reset()
    {
        // What the CPU programs; the host side (keys held, Caps Lock, the queue, the mouse's
        // position, paddles and buttons) is the RP2350's and the keyboard's, and runs on.
        leds_ = kbcfg_ = 0;
        repeat_ = 0x10;
        fifo_head_ = fifo_count_ = 0;
        mctrl_ = 0;
        max_x_ = 639;
        max_y_ = 479;
        pos_x_ = clamp(pos_x_, max_x_);
        pos_y_ = clamp(pos_y_, max_y_);
        sum_x_ = sum_y_ = sum_wheel_ = 0;
        latch_x_ = latch_y_ = 0;
        latch_wheel_ = 0;
        triggered_ = false;
        ien_ = isr_ = 0;
    }

    int UsbCard::cooked(uint8_t usage, uint8_t mods, bool caps)
    {
        const bool shift = mods & (L_SHIFT | R_SHIFT), ctrl = mods & (L_CTRL | R_CTRL);
        uint8_t c = 0;
        if (usage >= 0x04 && usage <= 0x1D) // a-z
        {
            c = static_cast<uint8_t>('a' + usage - 0x04);
            if (shift != caps) // Caps Lock on letters only; Shift undoes it, as on a PC
                c = static_cast<uint8_t>(c - 0x20);
            if (ctrl)
                return static_cast<uint8_t>(c & 0x1F);
            return c;
        }
        if (usage >= 0x1E && usage <= 0x38)
            c = static_cast<uint8_t>((shift ? ROW_SHIFT : ROW)[usage - 0x1E]);
        else if (usage >= 0x59 && usage <= 0x62) // keypad 1-9, 0
            c = static_cast<uint8_t>(usage == 0x62 ? '0' : '1' + usage - 0x59);
        else
        {
            switch (usage)
            {
            case 0x4C: return 0x7F; // Delete
            case 0x4F: return 0x15; // right arrow, as the Apple's
            case 0x50: return 0x08; // left
            case 0x51: return 0x0A; // down
            case 0x52: return 0x0B; // up
            case 0x54: return '/';  // keypad
            case 0x55: return '*';
            case 0x56: return '-';
            case 0x57: return '+';
            case 0x58: return '\r';
            case 0x63: return '.';
            default: return NO_CHAR;
            }
        }
        if (ctrl && c >= 0x40 && c < 0x60)
            return static_cast<uint8_t>(c & 0x1F);
        if (ctrl && c >= 0x60 && c < 0x80)
            return static_cast<uint8_t>(c & 0x1F);
        return c;
    }

    void UsbCard::push_event(uint8_t usage, uint8_t flags)
    {
        isr_ |= IRQ_KEY; // an event, whether the FIFO has room for it or not
        if (fifo_count_ + 2 > FIFO_BYTES)
            return; // full: the event is dropped whole
        fifo_[(fifo_head_ + fifo_count_++) % FIFO_BYTES] = usage;
        fifo_[(fifo_head_ + fifo_count_++) % FIFO_BYTES] = flags;
    }

    uint8_t UsbCard::pop_event()
    {
        if (!fifo_count_)
            return 0;
        const uint8_t b = fifo_[fifo_head_];
        fifo_head_ = (fifo_head_ + 1) % FIFO_BYTES;
        --fifo_count_;
        return b;
    }

    void UsbCard::strobe(uint8_t ascii)
    {
        key_ = static_cast<uint8_t>(ascii | STROBE);
        strobed_at_ = now_;
        isr_ |= IRQ_KEY; // a repeat too: KEY has a new character
    }

    bool UsbCard::waiting_to_be_taken() const
    {
        const auto [usage, down] = pending_.front();
        if (!down || !(key_ & STROBE) || now_ >= strobed_at_ + ms(TAKE_MS))
            return false;
        if (usage == DELETE && (mods_ & (L_CTRL | R_CTRL)))
            return false; // Ctrl-Reset makes no character: never held back
        return cooked(usage, mods_, caps_) != NO_CHAR;
    }

    void UsbCard::key(uint8_t usage, bool down)
    {
        if (down == down_[usage])
            return; // USB reports state; the card sends changes
        down_[usage] = down;
        push_event(usage, down ? 0 : RELEASE);
        if (usage >= LEFT_CTRL && usage <= LEFT_CTRL + 7)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << (usage - LEFT_CTRL));
            mods_ = static_cast<uint8_t>(down ? mods_ | bit : mods_ & ~bit);
            return;
        }
        if (!down)
        {
            if (usage == held_)
                held_ = 0;
            return;
        }
        if (usage == CAPS_LOCK)
        {
            caps_ = !caps_;
            return;
        }
        if (usage == PAUSE && (kbcfg_ & NMI_ON_PAUSE))
            nmi_ = true;
        if (usage == DELETE && (mods_ & (L_CTRL | R_CTRL)))
        {
            reset_ = true; // Ctrl-Reset: no character
            return;
        }
        if (const int c = cooked(usage, mods_, caps_); c != NO_CHAR) // Ctrl-@ is NUL, a character
        {
            strobe(static_cast<uint8_t>(c));
            held_ = usage;
            next_repeat_ = now_ + ms(250 * (((repeat_ >> 4) & 3) + 1));
        }
    }

    void UsbCard::queue_key(uint8_t usage, bool down)
    {
        if (pending_.empty() && now_ >= next_report_)
        {
            key(usage, down);
            next_report_ = now_ + ms(POLL_MS);
            return;
        }
        pending_.emplace_back(usage, down);
    }

    void UsbCard::drop_queue()
    {
        pending_.clear();
        for (unsigned u = 0; u < down_.size(); ++u)
            if (down_[u])
                key(static_cast<uint8_t>(u), false); // a release queued behind the rest would be lost with it
    }

    bool UsbCard::take_reset()
    {
        const bool r = reset_;
        reset_ = false;
        return r;
    }

    uint8_t UsbCard::apple_keys() const
    {
        constexpr uint8_t L_ALT = 0x04, L_GUI = 0x08, R_ALT = 0x40, R_GUI = 0x80;
        return static_cast<uint8_t>((mods_ & (L_ALT | L_GUI) ? 1 : 0) | (mods_ & (R_ALT | R_GUI) ? 2 : 0));
    }

    bool UsbCard::take_nmi()
    {
        const bool n = nmi_;
        nmi_ = false;
        return n;
    }

    void UsbCard::mouse_move(int dx, int dy)
    {
        if (!dx && !dy)
            return;
        sum_x_ += dx;
        sum_y_ += dy;
        pos_x_ = clamp(pos_x_ + dx, max_x_);
        pos_y_ = clamp(pos_y_ + dy, max_y_);
        isr_ |= IRQ_MOUSE;
    }

    void UsbCard::mouse_to(int x, int y)
    {
        const int nx = clamp(x, max_x_), ny = clamp(y, max_y_);
        if (nx == pos_x_ && ny == pos_y_)
            return;
        sum_x_ += nx - pos_x_;
        sum_y_ += ny - pos_y_;
        pos_x_ = nx;
        pos_y_ = ny;
        isr_ |= IRQ_MOUSE;
    }

    void UsbCard::mouse_buttons(uint8_t buttons)
    {
        buttons &= 0x1F;
        if (buttons != mbtn_)
            isr_ |= IRQ_MOUSE;
        mbtn_ = buttons;
    }

    void UsbCard::mouse_wheel(int steps)
    {
        if (!steps)
            return;
        sum_wheel_ += steps;
        isr_ |= IRQ_MOUSE;
    }

    void UsbCard::set_paddle(unsigned n, uint8_t value)
    {
        if (n < paddles_.size())
            paddles_[n] = value;
    }

    void UsbCard::set_game_buttons(uint8_t buttons)
    {
        buttons &= 0x07;
        if (buttons != game_buttons_)
            isr_ |= IRQ_GAME;
        game_buttons_ = buttons;
    }

    void UsbCard::tick(uint64_t cycles)
    {
        now_ += cycles;
        if (!pending_.empty() && now_ >= next_report_ && !waiting_to_be_taken())
        {
            const auto [usage, down] = pending_.front();
            pending_.pop_front();
            key(usage, down);
            next_report_ = now_ + ms(POLL_MS);
        }
        if (held_ && down_[held_] && now_ >= next_repeat_)
        {
            if (const int c = cooked(held_, mods_, caps_); c != NO_CHAR)
                strobe(static_cast<uint8_t>(c));
            const uint64_t period = ms(33 * ((repeat_ & 0x0F) + 1));
            next_repeat_ += period;
            if (next_repeat_ <= now_) // a long tick: no burst of repeats, just the next one
                next_repeat_ = now_ + period;
        }
    }

    uint8_t UsbCard::dack_read(bool) { return pop_event(); }

    // --- the speaker -------------------------------------------------------------

    void UsbCard::toggle_speaker()
    {
        ++speaker_toggles_;
        if (audio_rate_)
        {
            toggles_.push_back(clock());
            if (toggles_.size() > 65'536) // nobody collecting: keep the backlog down
                render();
        }
        else
            level_ = !level_;
    }

    void UsbCard::set_audio_rate(uint32_t rate)
    {
        audio_rate_ = rate;
        toggles_.clear();
        audio_.clear();
        rendered_ = clock();
        sample_phase_ = 0;
        hp_in_ = SPEAKER_LEVEL * (level_ ? 1.0f : -1.0f); // at rest where it is: no click for turning it on
        hp_out_ = 0;
        hp_pole_ = rate ? std::exp(-2.0f * 3.14159265f * 20.0f / static_cast<float>(rate)) : 0.0f;
    }

    void UsbCard::render()
    {
        const uint64_t now = clock();
        for (;;)
        {
            // Each sample spans CPU Hz / rate cycles, the remainder carried (21 or 22 at 1.02 MHz and 48 kHz).
            const uint64_t owed = sample_phase_ + cpu_hz_;
            const uint64_t length = owed / audio_rate_;
            if (rendered_ + length > now)
                break;
            sample_phase_ = owed % audio_rate_;
            const uint64_t end = rendered_ + length;
            double high = 0; // cycles the flip-flop was high
            uint64_t t = rendered_;
            while (!toggles_.empty() && toggles_.front() < end)
            {
                const uint64_t at = std::max(toggles_.front(), t);
                if (level_)
                    high += static_cast<double>(at - t);
                t = at;
                level_ = !level_;
                toggles_.pop_front();
            }
            if (level_)
                high += static_cast<double>(end - t);
            rendered_ = end;
            if (!length)
                continue;
            const float in = SPEAKER_LEVEL * static_cast<float>(2 * high / static_cast<double>(length) - 1);
            hp_out_ = in - hp_in_ + hp_pole_ * hp_out_; // the coupling capacitor
            hp_in_ = in;
            audio_.push_back(hp_out_);
        }
        const size_t most = audio_rate_ / 2;
        if (audio_.size() > most)
            audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(audio_.size() - most));
    }

    size_t UsbCard::take_audio(float *out, size_t max)
    {
        if (!audio_rate_)
            return 0;
        render();
        const size_t n = std::min(max, audio_.size());
        std::copy(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(n), out);
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    namespace
    {
        uint8_t sat8(int v) { return static_cast<uint8_t>(static_cast<int8_t>(v < -128 ? -128 : v > 127 ? 127 : v)); }
        uint16_t sat16(int v)
        {
            return static_cast<uint16_t>(static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v));
        }
    } // namespace

    uint8_t UsbCard::read(uint32_t offset)
    {
        switch (offset)
        {
        case KEYCLR:
        {
            const uint8_t v = peek(offset);
            key_ &= static_cast<uint8_t>(~STROBE);
            return v;
        }
        case EVENT:
            return pop_event();
        case MX: // latch all three
            if (mctrl_ & ABSOLUTE)
            {
                latch_x_ = static_cast<uint16_t>(pos_x_);
                latch_y_ = static_cast<uint16_t>(pos_y_);
            }
            else
            {
                latch_x_ = sat16(sum_x_);
                latch_y_ = sat16(sum_y_);
            }
            latch_wheel_ = sat8(sum_wheel_);
            sum_x_ = sum_y_ = sum_wheel_ = 0;
            return static_cast<uint8_t>(latch_x_);
        case PTRIG:
            trigger_ = now_;
            triggered_ = true;
            return peek(offset);
        case SPKR:
            toggle_speaker();
            return peek(offset);
        default:
            return peek(offset);
        }
    }

    uint8_t UsbCard::peek(uint32_t offset) const
    {
        switch (offset)
        {
        case KEY:
            return key_;
        case KEYCLR:
        {
            bool any = false;
            for (unsigned u = 0x04; u < 0xE0; ++u)
                any = any || down_[u];
            return static_cast<uint8_t>((key_ & 0x7F) | (any ? 0x80 : 0));
        }
        case MODS:
            return mods_;
        case EVCOUNT:
            return static_cast<uint8_t>(fifo_count_ > 255 ? 255 : fifo_count_);
        case EVENT:
            return fifo_count_ ? fifo_[fifo_head_] : 0;
        case LEDS:
            return leds_;
        case REPEAT:
            return repeat_;
        case KBCFG:
            return kbcfg_;
        case MCTRL:
            return mctrl_;
        case MBTN:
            return mbtn_;
        case MX:
            return static_cast<uint8_t>(latch_x_);
        case MX + 1:
            return static_cast<uint8_t>(latch_x_ >> 8);
        case MY:
            return static_cast<uint8_t>(latch_y_);
        case MY + 1:
            return static_cast<uint8_t>(latch_y_ >> 8);
        case MWHEEL:
            return latch_wheel_;
        case MAXX:
            return static_cast<uint8_t>(max_x_);
        case MAXX + 1:
            return static_cast<uint8_t>(max_x_ >> 8);
        case MAXY:
            return static_cast<uint8_t>(max_y_);
        case MAXY + 1:
            return static_cast<uint8_t>(max_y_ >> 8);
        case GAME:
        {
            uint8_t g = static_cast<uint8_t>(game_buttons_ << 4);
            if (triggered_)
                for (unsigned i = 0; i < 4; ++i)
                    if (now_ - trigger_ < uint64_t{cpu_hz_} * PADDLE_US * paddles_[i] / 1'000'000)
                        g |= static_cast<uint8_t>(1u << i);
            return g;
        }
        case PADDLE0:
        case PADDLE0 + 1:
        case PADDLE0 + 2:
        case PADDLE0 + 3:
            return paddles_[offset - PADDLE0];
        case IEN:
            return ien_;
        case ISR:
            return isr_;
        case DEVICES:
            return 0x03; // keyboard and mouse
        case ID_REG:
            return ID;
        default:
            return 0xFF;
        }
    }

    void UsbCard::write(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
        case KEYCLR:
            key_ &= static_cast<uint8_t>(~STROBE);
            break;
        case LEDS:
            leds_ = data & 0x07;
            break;
        case REPEAT:
            repeat_ = data & 0x3F;
            break;
        case KBCFG:
            kbcfg_ = data & 0x8F;
            break;
        case MCTRL:
            mctrl_ = data & ABSOLUTE;
            break;
        case MAXX:
            max_x_ = static_cast<uint16_t>((max_x_ & 0xFF00) | data);
            pos_x_ = clamp(pos_x_, max_x_); // new limits hold the pointer at once
            break;
        case MAXX + 1:
            max_x_ = static_cast<uint16_t>((max_x_ & 0xFF) | data << 8);
            pos_x_ = clamp(pos_x_, max_x_);
            break;
        case MAXY:
            max_y_ = static_cast<uint16_t>((max_y_ & 0xFF00) | data);
            pos_y_ = clamp(pos_y_, max_y_);
            break;
        case MAXY + 1:
            max_y_ = static_cast<uint16_t>((max_y_ & 0xFF) | data << 8);
            pos_y_ = clamp(pos_y_, max_y_);
            break;
        case PTRIG:
            trigger_ = now_;
            triggered_ = true;
            break;
        case SPKR:
            toggle_speaker();
            break;
        case IEN:
            ien_ = data & 0x0F;
            break;
        case ISR:
            isr_ = static_cast<uint8_t>(isr_ & ~data);
            break;
        default:
            break;
        }
    }
} // namespace ooe::emu
