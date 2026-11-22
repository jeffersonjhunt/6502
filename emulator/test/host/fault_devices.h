#pragma once

/**
 * @file fault_devices.h
 * @brief Test-only faults for the diagnostics' suites (hw/diag.md, answer 8 and plan 2).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * FaultyMemory came from feat/diag-d2's test_oodos_diag.cpp (4fdc4f2), where DIAG.COM's tests used it; it
 * is shared here for the ROM's diagnostics (test_bios_diag). It adds an aliased address line to D2's stuck
 * bits, and one block answering as another (fault_redirect). Nothing here is in the emulator: a fault is mapped over the machine's own memory after it is built,
 * and the bus's later mappings win, for the CPU's windows and DMA alike. Addresses are physical as the CPU card
 * reaches them (Decoder::redirect()) when the machine has a revision 2 video card: bank 8 is then the aperture at
 * $E00000 (AP8 set from reset, or a revision 1 bridge's fixed alias), so a fault there is the aperture's. With no
 * revision 2 card bank 8 is RAM (the BIOS clears AP8; step B7, proposed), and `ram` asks for the RAM card's bank 8
 * whatever is fitted: the diagnostics clear AP8 and test it (b7-plan.md, question 6).
 */

#include "backplane_bridge.h"
#include "decoder.h"
#include "cpu6502.h"
#include "machine.h"
#include "video_card.h"

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

    /// Where a fault at physical `a` goes: the CPU card's redirect (bank 8 to the aperture) where the machine has a
    /// revision 2 video card, unless `ram`; else `a` itself, the RAM card's.
    inline uint32_t fault_address(ooe::emu::Machine &m, uint32_t a, bool ram)
    {
        const ooe::emu::VideoCard *v = m.video_card();
        return ram || !v || v->revision() != 2 ? a : m.bus().redirect(a);
    }

    /// `f` over the 16 KB block holding physical `block_addr`, on whatever is mapped there now (`ram`: bank 8's
    /// RAM, never the aperture). @return whether it could be mapped
    inline bool fault_over(ooe::emu::Machine &m, FaultyMemory &f, uint32_t block_addr, bool ram = false)
    {
        using namespace ooe::emu;
        const uint32_t lo = fault_address(m, block_addr, ram) & ~0x3FFFu, hi = lo + 0x3FFF;
        f.bad = f.bad == 0xFFFFFFFF ? f.bad : fault_address(m, f.bad, ram);
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
    /// 16 KB block at physical `from` forwards every access to the block at `to`, whatever device holds it
    /// (`ram`: bank 8's RAM, never the aperture).
    inline bool fault_redirect(ooe::emu::Machine &m, FaultyMemory &f, uint32_t from, uint32_t to, bool ram = false)
    {
        using namespace ooe::emu;
        to = fault_address(m, to, ram);
        from = fault_address(m, from, ram);
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

    /// An empty slot's floating bus over memory, as far as a read goes: the last byte the CPU fetched, the
    /// instruction's last byte (an absolute load's high address byte; an indexed one that crosses a page
    /// re-reads it on the W65C02S), as test_oodos_sysinfo's FloatingBus answers for an I/O block (CC5). Writes
    /// go nowhere.
    struct FloatingMemory : ooe::emu::Device
    {
        ooe::emu::Cpu6502 *cpu{nullptr};
        ooe::emu::Decoder *bus{nullptr};
        size_t bytes{0x40000};
        size_t size() const override { return bytes; }
        uint8_t read(uint32_t) override { return peek(0); }
        uint8_t peek(uint32_t) const override { return bus->peek(static_cast<uint16_t>(cpu->regs().pc - 1)); }
        void write(uint32_t, uint8_t) override {}
    };
    /// `f` over physical `first` to `first` + f.bytes - 1, where no module answers (a RAM line left out).
    inline bool float_over(ooe::emu::Machine &m, ooe::emu::Cpu6502 &cpu, FloatingMemory &f, uint32_t first)
    {
        using namespace ooe::emu;
        f.cpu = &cpu;
        f.bus = &m.bus();
        Mapping n;
        n.first = bus_address(Space::Memory, first);
        n.last = bus_address(Space::Memory, first + static_cast<uint32_t>(f.bytes) - 1);
        n.device = &f;
        return m.bus().map(n);
    }

    /// A stuck bit: the byte at physical `bad` reads (byte AND `and_mask`) OR `or_mask` (`ram`: as fault_over()).
    inline bool fault_at(ooe::emu::Machine &m, FaultyMemory &f, uint32_t bad, uint8_t and_mask, uint8_t or_mask,
                         bool ram = false)
    {
        f.bad = bad;
        f.and_mask = and_mask;
        f.or_mask = or_mask;
        return fault_over(m, f, bad, ram);
    }

    /// A north bridge revision 3 whose timebase is wrong in one way, for NB10 (hw/future/north-bridge-rev3.md): mapped
    /// over the bridge's registers ($C680-$C6FF) after the machine is built, it passes every access through to the
    /// bridge (caught up to the cycle first: the bridge is timed and this isn't) but for its fault. Its IRQ is the
    /// bridge's own mapping's, which stays.
    /// - MssNotRestarted: MSS as the free-running millisecond count modulo 1,000, latched by SEC as the bridge's is,
    ///   never restarted by the DS3231's second. The chip's second starts at a phase of its own against the count
    ///   (437 ms here: the emulated chip, set at power-on, would otherwise start on a millisecond of the count's).
    /// - MsfNotClearedBy1: a 1 written to TBCTL's MSF leaves it set (OVR and MSIE as written).
    struct BridgeFault : ooe::emu::Device
    {
        enum class Kind
        {
            MssNotRestarted,
            MsfNotClearedBy1,
        };
        ooe::emu::BackplaneBridge *bridge{nullptr};
        ooe::emu::Decoder *bus{nullptr};
        Kind kind{Kind::MssNotRestarted};
        uint16_t mss{0};

        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override
        {
            bus->sync();
            const uint8_t v = bridge->read(offset);
            if (kind == Kind::MssNotRestarted)
            {
                if (offset == ooe::emu::BackplaneBridge::SEC)
                    mss = static_cast<uint16_t>((bridge->ms() + 437) % 1000);
                if (offset == ooe::emu::BackplaneBridge::MSSL)
                    return static_cast<uint8_t>(mss);
                if (offset == ooe::emu::BackplaneBridge::MSSH)
                    return static_cast<uint8_t>(mss >> 8);
            }
            return v;
        }
        uint8_t peek(uint32_t offset) const override { return bridge->peek(offset); }
        void write(uint32_t offset, uint8_t data) override
        {
            bus->sync();
            if (kind == Kind::MsfNotClearedBy1 && offset == ooe::emu::BackplaneBridge::TBCTL)
                data = static_cast<uint8_t>(data & ~ooe::emu::BackplaneBridge::TB_MSF);
            bridge->write(offset, data);
        }
    };

    /// `f` over machine `m`'s north bridge registers, as fault `kind`. @return whether it could be mapped
    inline bool bridge_fault(ooe::emu::Machine &m, BridgeFault &f, BridgeFault::Kind kind)
    {
        f.bridge = m.backplane_bridge();
        f.bus = &m.bus();
        f.kind = kind;
        if (!f.bridge)
            return false;
        m.bus().map(ooe::emu::Mapping{ooe::emu::bus_address(ooe::emu::Space::Io, ooe::emu::BackplaneBridge::REGISTERS),
                                      ooe::emu::bus_address(ooe::emu::Space::Io, ooe::emu::BackplaneBridge::REGISTERS + 0x7F),
                                      &f, ooe::emu::BackplaneBridge::MODULE});
        return true;
    }

} // namespace ooe::test
