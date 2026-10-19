/**
 * @file terminal.cpp
 * @brief Host terminal implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "terminal.h"

#include <cstdio>
#include <string>

#include <unistd.h>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "terminal";
    } // namespace

    Terminal::Terminal(Console &console) : console_(console)
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
        constexpr size_t MAX_QUEUED = 4096;
        uint8_t last = 0;
        bool debug = false;
        char buf[64];
        const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0)
            EMU_LOGE(TAG, "read() failed");
        for (ssize_t i = 0; i < n; ++i)
        {
            const auto key = static_cast<uint8_t>(buf[i]);
            if (key == DEBUG_KEY)
            {
                debug = true;
                continue;
            }
            last = key;
            if (keys_.size() < MAX_QUEUED)
                keys_.push_back(key);
            else
                EMU_LOGW(TAG, "key %02X dropped: %zu keys already waiting", key, keys_.size());
        }
        while (!keys_.empty() && console_.key(keys_.front()))
            keys_.pop_front();
        return debug ? DEBUG_KEY : last;
    }

    void Terminal::update_screen()
    {
        console_.service();
        uint8_t ch = 0;
        bool printed = false;
        while (console_.output(ch))
        {
            if (ch == '\n' && last_cr_)
            {
                last_cr_ = false; // the LF of a CR LF: the CR already gave one
                continue;
            }
            std::putchar(ch);
            if (ch == '\r')
                std::putchar('\n');
            last_cr_ = ch == '\r';
            printed = true;
        }
        if (printed)
            lcd_on_screen_ = false;

        std::vector<std::string> rows;
        if (console_.display(rows))
        {
            lcd_rows_ = rows;
            lcd_dirty_ = true;
        }
        if (lcd_dirty_ && std::chrono::steady_clock::now() - lcd_drawn_ > std::chrono::milliseconds(20))
            draw_lcd();
        if (printed || lcd_dirty_)
            std::fflush(stdout);
    }

    void Terminal::draw_lcd()
    {
        const std::string edge = "+" + std::string(lcd_rows_.empty() ? 0 : lcd_rows_[0].size(), '-') + "+\r\n";
        if (lcd_on_screen_)
            std::printf("\033[%zuA", lcd_rows_.size() + 2); // back up over the last box
        else if (last_cr_ == false && !lcd_rows_.empty())
            std::fputs("\r\n", stdout);
        std::fputs(edge.c_str(), stdout);
        for (const std::string &row : lcd_rows_)
            std::printf("|%s|\r\n", row.c_str());
        std::fputs(edge.c_str(), stdout);
        std::fflush(stdout);
        lcd_on_screen_ = true;
        lcd_dirty_ = false;
        lcd_drawn_ = std::chrono::steady_clock::now();
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
