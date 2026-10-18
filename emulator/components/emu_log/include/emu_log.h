#pragma once

/**
 * @file emu_log.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief TAG-scoped, printf-style logging for the emulator.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Replaces easylogging++ (~7.7k vendored lines plus a config file) with the
 * shape of ESP-IDF's ESP_LOGx, so code here reads like pinled's:
 *
 * @code
 *   static const char *TAG = "loader";
 *   EMU_LOGE(TAG, "could not open %s", path);
 * @endcode
 *
 * The level is checked before any formatting, so a disabled message costs a
 * compare. EMU_LOGV is different: it is for per-instruction and per-access
 * tracing, and compiles to nothing unless EMU_TRACE is defined, because even
 * a compare on every bus cycle shows up in a 30M-instruction test run.
 *
 * Default: Warn and above to stderr. The terminal app turns logging off
 * unless given --log, since its stderr is the Apple-1 screen.
 */

#include <cstdio>

namespace ooe::emu::log
{
    enum class Level : int
    {
        None = 0,
        Error,
        Warn,
        Info,
        Debug,
        Verbose,
    };

    void set_level(Level level);
    Level level();

    /// Where messages go; nullptr discards. Not owned.
    void set_sink(std::FILE *sink);

    inline bool enabled(Level l)
    {
        return static_cast<int>(l) <= static_cast<int>(level());
    }

    /// Format and emit one line: "<L> (<tag>) <message>\n". Prefer the macros.
    void write(Level level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
} // namespace ooe::emu::log

#define EMU_LOG_AT(lvl, tag, ...)                                        \
    do {                                                                 \
        if (::ooe::emu::log::enabled(lvl))                               \
            ::ooe::emu::log::write(lvl, tag, __VA_ARGS__);               \
    } while (0)

#define EMU_LOGE(tag, ...) EMU_LOG_AT(::ooe::emu::log::Level::Error, tag, __VA_ARGS__)
#define EMU_LOGW(tag, ...) EMU_LOG_AT(::ooe::emu::log::Level::Warn, tag, __VA_ARGS__)
#define EMU_LOGI(tag, ...) EMU_LOG_AT(::ooe::emu::log::Level::Info, tag, __VA_ARGS__)
#define EMU_LOGD(tag, ...) EMU_LOG_AT(::ooe::emu::log::Level::Debug, tag, __VA_ARGS__)

#ifdef EMU_TRACE
#define EMU_LOGV(tag, ...) EMU_LOG_AT(::ooe::emu::log::Level::Verbose, tag, __VA_ARGS__)
#else
#define EMU_LOGV(tag, ...) ((void)0)
#endif
