/**
 * @file terminal.cpp
 * @brief Terminal keyboard and display implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "terminal.h"

#include <cstdio>

#include <unistd.h>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "terminal";

        constexpr uint8_t BIT7 = 0x80;
    } // namespace

    TerminalKeyboard::TerminalKeyboard(Bus &bus, uint16_t ctrl_addr, uint16_t data_addr)
        : bus_(bus), ctrl_addr_(ctrl_addr), data_addr_(data_addr)
    {
        if (tcgetattr(STDIN_FILENO, &saved_) < 0)
        {
            EMU_LOGE(TAG, "tcgetattr() failed; keyboard left in line mode");
            return;
        }
        struct termios raw = saved_;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
        {
            EMU_LOGE(TAG, "tcsetattr() failed");
            return;
        }
        restore_ = true;
    }

    TerminalKeyboard::~TerminalKeyboard()
    {
        if (restore_ && tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_) < 0)
            EMU_LOGE(TAG, "tcsetattr() failed restoring the terminal");
    }

    uint8_t TerminalKeyboard::poll()
    {
        char key = 0;
        if (read(STDIN_FILENO, &key, 1) < 0)
            EMU_LOGE(TAG, "read() failed");
        if (key == 0)
            return 0;

        const uint8_t ctrl = bus_.read(ctrl_addr_);
        if (ctrl & BIT7)
        {
            EMU_LOGW(TAG, "key %02X dropped: previous key not yet read", static_cast<uint8_t>(key));
            return 0;
        }
        bus_.poke(data_addr_, encode(static_cast<uint8_t>(key)));
        bus_.poke(ctrl_addr_, static_cast<uint8_t>(ctrl | BIT7)); // key-ready: device side only
        return static_cast<uint8_t>(key);
    }

    uint8_t TerminalKeyboard::encode(uint8_t key)
    {
        if (key >= 'a' && key <= 'z')
            key = static_cast<uint8_t>(key - 0x20); // the Apple-1 is upper case only
        if (key == '\n')
            key = '\r';
        return static_cast<uint8_t>(key | BIT7);
    }

    void TerminalDisplay::update()
    {
        const uint8_t data = bus_.read(data_addr_);
        if (!(data & BIT7))
            return;
        const auto ch = static_cast<uint8_t>(data & ~BIT7);
        std::putchar(ch);
        if (ch == '\r')
            std::putchar('\n');
        std::fflush(stdout);
        bus_.poke(data_addr_, ch); // display no longer busy
    }
} // namespace ooe::emu
