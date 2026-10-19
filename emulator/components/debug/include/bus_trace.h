#pragma once

/**
 * @file bus_trace.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Writes every bus cycle in the bus spy's text format.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The format is docs/BUS_TRACE.md: one line per cycle with the 20-bit
 * address the north bridge makes, data, R/W, SYNC, VPB and the module ID,
 * so that a board capture and an emulator run can be diffed
 * (tools/trace_diff.py). Attach it with Cpu6502::set_observer().
 *
 * A line per cycle at 1 MHz is ~25 MB/s, so the trace can be paused and
 * resumed (the debugger's `trace` command) to capture only a window.
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include "cpu6502.h"
#include "decoder.h"

namespace ooe::emu
{
    class BusTrace : public CycleObserver
    {
    public:
        static constexpr int VERSION = 1;

        /// Writes the header to `out` (not owned; the caller closes it).
        /// `about` becomes free-form comment lines, e.g. "cpu w65c02s".
        BusTrace(std::FILE *out, const Decoder &decoder, const std::string &about);

        void on_cycle(const BusCycle &c) override;

        void set_enabled(bool on) { enabled_ = on; }
        bool enabled() const { return enabled_; }
        uint64_t lines() const { return lines_; }

    private:
        std::FILE *out_;
        const Decoder &decoder_;
        bool enabled_{true};
        uint64_t lines_{0};
    };
} // namespace ooe::emu
