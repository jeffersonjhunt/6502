/**
 * @file terminal.cpp
 * @brief Host terminal implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "terminal.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <string>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "terminal";

        // For the signal handlers, which have no Terminal: both modes, and
        // which one is in force.
        struct termios g_saved{};
        struct termios g_raw{};
        volatile std::sig_atomic_t g_active = 0; ///< a Terminal changed the mode
        volatile std::sig_atomic_t g_raw_on = 0; ///< raw, not the debugger's prompt
        volatile std::sig_atomic_t g_resumed = 0;

        void handle(int sig, void (*handler)(int))
        {
            struct sigaction sa{};
            sa.sa_handler = handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART; // a stop is no reason to end the prompt's read
            sigaction(sig, &sa, nullptr);
        }

        void on_continue(int)
        {
            const int saved_errno = errno;
            if (g_active && g_raw_on)
                tcsetattr(STDIN_FILENO, TCSANOW, &g_raw); // the shell had it back meanwhile
            g_resumed = 1;
            errno = saved_errno;
        }

        // Ctrl-Z: leave the shell a usable terminal, then stop as the default
        // action would. Raised with it unblocked, so the stop happens inside
        // raise(), or not at all (an orphaned process group), and either way
        // raw mode is back on return.
        void on_stop(int sig)
        {
            const int saved_errno = errno;
            if (g_active)
                tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
            handle(sig, SIG_DFL);
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, sig);
            sigprocmask(SIG_UNBLOCK, &set, nullptr);
            raise(sig);
            handle(sig, on_stop);
            on_continue(SIGCONT);
            errno = saved_errno;
        }
    } // namespace

    bool Terminal::resumed()
    {
        const bool r = g_resumed;
        g_resumed = 0;
        return r;
    }

    void Terminal::restore()
    {
        if (g_active)
            tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    }

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
        g_saved = saved_;
        g_raw = raw;
        g_raw_on = 1;
        g_active = 1;
        handle(SIGTSTP, on_stop);
        handle(SIGCONT, on_continue);
    }

    Terminal::~Terminal()
    {
        if (!restore_)
            return;
        handle(SIGTSTP, SIG_DFL);
        handle(SIGCONT, SIG_DFL);
        g_active = 0;
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_) < 0)
            EMU_LOGE(TAG, "tcsetattr() failed restoring the terminal");
    }

    bool Terminal::fill(bool wait)
    {
        // Running, only read what is there. At the prompt a plain blocking
        // read(): a stop and continue restart it (SA_RESTART), where poll()
        // would fail with EINTR; Ctrl-C (no SA_RESTART) still ends it.
        struct pollfd in = {STDIN_FILENO, POLLIN, 0};
        while (!eof_)
        {
            if (!wait && poll(&in, 1, 0) <= 0)
                return true; // nothing yet
            char buf[256];
            const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n > 0)
            {
                input_.append(buf, static_cast<size_t>(n));
                return true;
            }
            if (n == 0)
                eof_ = true;
            else if (errno == EINTR)
                return !wait; // running, the loop sees the quit flag
            else if (errno == EAGAIN) // stdin came to us non-blocking
            {
                if (!wait)
                    return true;
                if (poll(&in, 1, -1) < 0)
                    return false;
            }
            else
            {
                EMU_LOGE(TAG, "reading stdin failed: errno %d", errno);
                eof_ = true;
            }
        }
        return false;
    }

    uint8_t Terminal::poll_key()
    {
        // Far more than a paste: past it a pipe waits for the machine to
        // catch up. Below it stdin is read however busy the machine is, so a
        // Ctrl-] behind a long backlog is still seen.
        constexpr size_t MAX_BACKLOG = 1 << 20;
        uint8_t last = 0;
        bool debug = false;
        if (input_.empty() && keys_.size() < MAX_BACKLOG)
            fill(false);
        // Keys go straight to keys_, so input_ only ever holds what follows
        // a Ctrl-]: a breakpoint's prompt never reads the machine's keys.
        size_t used = 0;
        while (used < input_.size() && !debug)
        {
            const auto key = static_cast<uint8_t>(input_[used++]);
            if (key == DEBUG_KEY)
            {
                debug = true; // what follows is the debugger's
                continue;
            }
            last = key;
            keys_.push_back(key);
        }
        input_.erase(0, used);
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
        if (restore_)
        {
            g_raw_on = 0;
            tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_);
        }
        std::fputs(prompt, stdout);
        std::fflush(stdout);
        line.clear();
        size_t end = input_.find('\n');
        bool ok = true;
        while (end == std::string::npos && (ok = fill(true))) // a long line arrives in pieces
            end = input_.find('\n');
        if (end != std::string::npos || (eof_ && !input_.empty())) // the last line may have no newline
        {
            ok = true;
            end = end == std::string::npos ? input_.size() : end + 1;
            line = input_.substr(0, end);
            input_.erase(0, end);
        }
        if (restore_)
        {
            tcsetattr(STDIN_FILENO, TCSADRAIN, &g_raw);
            g_raw_on = 1;
        }
        return ok;
    }

    void Terminal::write(const std::string &text)
    {
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
    }
} // namespace ooe::emu
