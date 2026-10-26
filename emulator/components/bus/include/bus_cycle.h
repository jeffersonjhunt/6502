#pragma once

/**
 * @file bus_cycle.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief One bus cycle as the CPU's pins show it, and things that watch them.
 * @version 0.1.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The CPU reports every access to a CycleObserver (cpu6502.h). It lives
 * with the bus rather than the CPU because the watchers are bus things too:
 * the bus spy's trace, and the Console card, whose CPLD latches every cycle
 * for its lights and breakpoint.
 *
 * The CPU takes one observer; a CycleTee hands each cycle to several.
 */

#include <cstdint>
#include <vector>

namespace ooe::emu
{
    /// One bus cycle, as a logic analyser on the CPU's pins would see it.
    struct BusCycle
    {
        uint64_t cycle{0};        ///< Cpu6502::cycles() including this access
        uint16_t addr{0};
        uint8_t data{0};
        bool write{false};        ///< RWB low
        bool sync{false};         ///< SYNC high: an opcode fetch
        bool vector_pull{false};  ///< VPB low: a reset/interrupt vector read (a W65C02S pin; reported for NMOS too)
        bool dma{false};          ///< BE low: a bus master's cycle (Decoder::set_dma_observer()); addr is A0-A15
    };

    class CycleObserver
    {
    public:
        virtual ~CycleObserver() = default;
        virtual void on_cycle(const BusCycle &c) = 0;
    };

    /// Several observers on one CPU, called in the order added. Not owned.
    class CycleTee : public CycleObserver
    {
    public:
        void add(CycleObserver *observer)
        {
            if (observer)
                observers_.push_back(observer);
        }
        bool empty() const { return observers_.empty(); }
        void on_cycle(const BusCycle &c) override
        {
            for (CycleObserver *o : observers_)
                o->on_cycle(c);
        }

    private:
        std::vector<CycleObserver *> observers_;
    };
} // namespace ooe::emu
