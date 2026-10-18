/**
 * @file emu_log.cpp
 * @brief TAG-scoped logging implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "emu_log.h"

#include <cstdarg>

namespace ooe::emu::log
{
    namespace
    {
        Level level_ = Level::Warn;
        std::FILE *sink_ = stderr;

        char letter(Level l)
        {
            switch (l)
            {
            case Level::Error:
                return 'E';
            case Level::Warn:
                return 'W';
            case Level::Info:
                return 'I';
            case Level::Debug:
                return 'D';
            case Level::Verbose:
                return 'V';
            case Level::None:
                break;
            }
            return '?';
        }
    } // namespace

    void set_level(Level level)
    {
        level_ = level;
    }

    Level level()
    {
        return level_;
    }

    void set_sink(std::FILE *sink)
    {
        sink_ = sink;
    }

    void write(Level level, const char *tag, const char *fmt, ...)
    {
        if (!sink_)
            return;
        std::fprintf(sink_, "%c (%s) ", letter(level), tag);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(sink_, fmt, ap);
        va_end(ap);
        std::fputc('\n', sink_);
        std::fflush(sink_); // a log is for after a crash too
    }
} // namespace ooe::emu::log
