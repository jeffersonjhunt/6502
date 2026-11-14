#pragma once

/**
 * @file fault_devices.h
 * @brief Test-only faults for the diagnostics' suites (hw/diag.md, answer 8 and plan 2).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * FaultyMemory came from feat/diag-d2's test_oodos_diag.cpp (4fdc4f2), where DIAG.COM's tests used it; it
 * is shared here for the ROM's diagnostics (test_bios_diag). It adds an aliased address line to D2's stuck
 * bits, and one block answering as another (fault_redirect). Nothing here is in the emulator: a fault is mapped over the machine's own memory after it is built,
 * and the bus's later mappings win, for the CPU's windows and DMA alike.
 */

#include "decoder.h"
#include "machine.h"

#include <cstdint>

namespace ooe::test
{
    /// A bad cell or a bad address line, over a 16 KB block of physical memory: every access goes to the
    /// device that was there, but the byte at `bad` reads as (byte AND `and_mask`) OR `or_mask`, and an
    /// offset in the block with any of `alias_mask`'s bits set goes to the offset without them (an address
    /// line stuck at 0, so two cells answer as one).
    struct FaultyMemory : ooe::emu::Device
    {
        ooe::emu::Device *under{nullptr};
        uint32_t first{0}, under_first{0}, bad{0xFFFFFFFF};
        uint8_t and_mask{0xFF}, or_mask{0};
        uint32_t alias_mask{0};
        size_t size() const override { return 0x4000; }
        uint32_t at(uint32_t offset) const
        {
            return static_cast<uint32_t>((first + (offset & ~alias_mask) - under_first) % under->size());
        }
        uint8_t read(uint32_t offset) override { return fault(offset, under->read(at(offset))); }
        uint8_t peek(uint32_t offset) const override { return fault(offset, under->peek(at(offset))); }
        void write(uint32_t offset, uint8_t data) override { under->write(at(offset), data); }
        uint8_t fault(uint32_t offset, uint8_t v) const
        {
            return first + offset == bad ? static_cast<uint8_t>((v & and_mask) | or_mask) : v;
        }
    };

    /// `f` over the 16 KB block holding physical `block_addr`, on whatever is mapped there now. @return
    /// whether it could be mapped
    inline bool fault_over(ooe::emu::Machine &m, FaultyMemory &f, uint32_t block_addr)
    {
        using namespace ooe::emu;
        const uint32_t lo = block_addr & ~0x3FFFu, hi = lo + 0x3FFF;
        const auto &maps = m.bus().mappings();
        const Mapping *found = nullptr;
        for (auto it = maps.rbegin(); it != maps.rend() && !found; ++it)
            if (it->first <= bus_address(Space::Memory, lo) && it->last >= bus_address(Space::Memory, hi))
                found = &*it;
        if (!found)
            return false;
        f.under = found->device;
        f.under_first = offset_of(found->first);
        f.first = lo;
        Mapping n = *found;
        n.first = bus_address(Space::Memory, lo);
        n.last = bus_address(Space::Memory, hi);
        n.device = &f;
        return m.bus().map(n);
    }

    /// One block answering as another (a block address line stuck, or a decoder fault): `f` mapped over the
    /// 16 KB block at physical `from` forwards every access to the block at `to`, whatever device holds it.
    inline bool fault_redirect(ooe::emu::Machine &m, FaultyMemory &f, uint32_t from, uint32_t to)
    {
        using namespace ooe::emu;
        const uint32_t lo = to & ~0x3FFFu, hi = lo + 0x3FFF;
        const auto &maps = m.bus().mappings();
        const Mapping *found = nullptr;
        for (auto it = maps.rbegin(); it != maps.rend() && !found; ++it)
            if (it->first <= bus_address(Space::Memory, lo) && it->last >= bus_address(Space::Memory, hi))
                found = &*it;
        if (!found)
            return false;
        f.under = found->device;
        f.under_first = offset_of(found->first);
        f.first = lo; // at() is then `to`'s offset in its device
        Mapping n = *found;
        n.first = bus_address(Space::Memory, from & ~0x3FFFu);
        n.last = bus_address(Space::Memory, (from & ~0x3FFFu) + 0x3FFF);
        n.device = &f;
        return m.bus().map(n);
    }

    /// A stuck bit: the byte at physical `bad` reads (byte AND `and_mask`) OR `or_mask`.
    inline bool fault_at(ooe::emu::Machine &m, FaultyMemory &f, uint32_t bad, uint8_t and_mask, uint8_t or_mask)
    {
        f.bad = bad;
        f.and_mask = and_mask;
        f.or_mask = or_mask;
        return fault_over(m, f, bad);
    }
} // namespace ooe::test
