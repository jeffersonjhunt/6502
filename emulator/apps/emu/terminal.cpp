/**
 * @file terminal.cpp
 * @brief Host terminal implementation.
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
    } // namespace

    Terminal::Terminal(apple1::Io &io) : io_(io)
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

    Terminal::~Terminal()
    {
        if (restore_ && tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_) < 0)
            EMU_LOGE(TAG, "tcsetattr() failed restoring the terminal");
    }

    uint8_t Terminal::poll_key()
    {
        char key = 0;
        if (read(STDIN_FILENO, &key, 1) < 0)
            EMU_LOGE(TAG, "read() failed");
        if (key == 0)
            return 0;
        if (key == DEBUG_KEY)
            return DEBUG_KEY;
        if (!io_.key(static_cast<uint8_t>(key)))
        {
            EMU_LOGW(TAG, "key %02X dropped: previous key not yet read", static_cast<uint8_t>(key));
            return 0;
        }
        return static_cast<uint8_t>(key);
    }

    void Terminal::update_screen()
    {
        uint8_t ch = 0;
        if (!io_.output(ch))
            return;
        std::putchar(ch);
        if (ch == '\r')
            std::putchar('\n');
        std::fflush(stdout);
    }

    bool Terminal::read_line(const char *prompt, std::string &line)
    {
        struct termios raw{};
        if (restore_)
        {
            tcgetattr(STDIN_FILENO, &raw);
            tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_);
        }
        std::fputs(prompt, stdout);
        std::fflush(stdout);
        line.clear();
        char buf[256];
        bool ok = false;
        while (std::fgets(buf, sizeof buf, stdin)) // a long line arrives in pieces
        {
            ok = true;
            line += buf;
            if (line.back() == '\n')
                break;
        }
        std::clearerr(stdin); // after EINTR, so the next read is not refused
        if (restore_)
            tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
        return ok;
    }

    void Terminal::write(const std::string &text)
    {
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
    }
} // namespace ooe::emu
