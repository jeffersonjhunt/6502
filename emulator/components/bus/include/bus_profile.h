#pragma once

/**
 * @file bus_profile.h
 * @brief Per-stage accounting of the bus cycles a run has spent, for a
 * performance baseline (docs/WASM_OPTIMIZATION_PLAN.md, Phase 0).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The core is bus-cycle accurate and the hot path is one chain per bus cycle
 * (Cpu6502 -> Decoder::read/write -> access -> route -> find -> device ->
 * catch_up -> wait). Optimising it needs to know where the time is, not a
 * guess. The Decoder, the machine it sits in, and the browser core all agree
 * on this little counter.
 *
 * It is a sink: a Decoder writes its own running total into a global pointer
 * (the machine holds its Decoder's, so a power cycle that makes a new machine
 * keeps pointing at the same counters). A run that is not profiled simply
 * never points one, and the Decoder's write is a single guarded store.
 */

#include <cstdint>

namespace ooe::emu
{
    /// Where the bus cycles a run has spent, by stage of one access.
    /// Every cycle is counted in exactly one stage; `total` is their sum.
    /// (A wait-state cycle is held by the device, so it is `device`, not
    /// `wait`: the cycle the decoder spends taking the access is the
    /// base one.)
    struct BusProfile
    {
        uint64_t route{0}; ///< NorthBridge::route(): CPU address -> bus address
        uint64_t find{0};  ///< the mapping lookup (decoder.cpp's find())
        uint64_t device{0}; ///< the access into the device (read/write/peek)
        uint64_t catch_up{0}; ///< the mapping's timed device ticked to this cycle
        uint64_t wait{0}; ///< cycles the access held the bus (RDY)
        uint64_t total{0}; ///< route + find + device + catch_up + wait
    };

    /// The Decoder's running total writes here. The machine owns it; nullptr
    /// (the default) means nothing is watching.
    extern BusProfile *g_bus_profile;
} // namespace ooe::emu
