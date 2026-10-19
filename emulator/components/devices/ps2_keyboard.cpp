/**
 * @file ps2_keyboard.cpp
 * @brief ASCII to PS/2 scan code set 2, presented on a 6522.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "ps2_keyboard.h"

namespace ooe::emu
{
    namespace
    {
        constexpr uint8_t LEFT_SHIFT = 0x12;
        constexpr uint8_t BREAK = 0xF0;

        // Unshifted and shifted characters sharing a key, with its set-2 code.
        struct Key
        {
            char plain;
            char shifted;
            uint8_t code;
        };
        constexpr Key KEYS[] = {
            {'a', 'A', 0x1C}, {'b', 'B', 0x32}, {'c', 'C', 0x21}, {'d', 'D', 0x23}, {'e', 'E', 0x24},
            {'f', 'F', 0x2B}, {'g', 'G', 0x34}, {'h', 'H', 0x33}, {'i', 'I', 0x43}, {'j', 'J', 0x3B},
            {'k', 'K', 0x42}, {'l', 'L', 0x4B}, {'m', 'M', 0x3A}, {'n', 'N', 0x31}, {'o', 'O', 0x44},
            {'p', 'P', 0x4D}, {'q', 'Q', 0x15}, {'r', 'R', 0x2D}, {'s', 'S', 0x1B}, {'t', 'T', 0x2C},
            {'u', 'U', 0x3C}, {'v', 'V', 0x2A}, {'w', 'W', 0x1D}, {'x', 'X', 0x22}, {'y', 'Y', 0x35},
            {'z', 'Z', 0x1A}, {'1', '!', 0x16}, {'2', '@', 0x1E}, {'3', '#', 0x26}, {'4', '$', 0x25},
            {'5', '%', 0x2E}, {'6', '^', 0x36}, {'7', '&', 0x3D}, {'8', '*', 0x3E}, {'9', '(', 0x46},
            {'0', ')', 0x45}, {'`', '~', 0x0E}, {'-', '_', 0x4E}, {'=', '+', 0x55}, {'[', '{', 0x54},
            {']', '}', 0x5B}, {'\\', '|', 0x5D}, {';', ':', 0x4C}, {'\'', '"', 0x52}, {',', '<', 0x41},
            {'.', '>', 0x49}, {'/', '?', 0x4A}, {' ', ' ', 0x29},
        };
    } // namespace

    uint8_t Ps2Keyboard::scan_code(uint8_t ascii, bool &shift)
    {
        shift = false;
        switch (ascii)
        {
        case '\r':
        case '\n': return 0x5A;
        case 0x08:
        case 0x7F: return 0x66;
        case 0x1B: return 0x76;
        case '\t': return 0x0D;
        default: break;
        }
        for (const Key &k : KEYS)
        {
            if (ascii == static_cast<uint8_t>(k.plain))
                return k.code;
            if (ascii == static_cast<uint8_t>(k.shifted))
            {
                shift = true;
                return k.code;
            }
        }
        return 0;
    }

    bool Ps2Keyboard::key(uint8_t ascii)
    {
        bool shift = false;
        const uint8_t code = scan_code(ascii, shift);
        if (code == 0 || queue_.size() + 8 > MAX_QUEUED)
            return false;
        if (shift)
            queue_.push_back(LEFT_SHIFT);
        queue_.insert(queue_.end(), {code, BREAK, code});
        if (shift)
            queue_.insert(queue_.end(), {BREAK, LEFT_SHIFT});
        service();
        return true;
    }

    void Ps2Keyboard::service()
    {
        if (presented_ && (via_.ifr() & Via6522::IRQ_CA1))
            return; // the last byte has not been read
        presented_ = false;
        if (queue_.empty())
            return;
        via_.drive(data_.port, data_.mask(), static_cast<uint8_t>(queue_.front() << data_.first));
        queue_.pop_front();
        via_.set_ca1(true);
        via_.set_ca1(false);
        presented_ = true;
    }
} // namespace ooe::emu
