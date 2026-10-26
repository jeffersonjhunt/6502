/**
 * @file decoder.cpp
 * @brief Address decoder implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "decoder.h"

#include <algorithm>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        [[maybe_unused]] const char *TAG = "decoder";

        /// A0-A19 for a bus address: the offset, or in the I/O hole the CPU address.
        uint32_t lines(uint32_t where)
        {
            if (space_of(where) != Space::Io)
                return offset_of(where);
            const uint32_t off = offset_of(where);
            return IO_HOLE + (off >= APPLE_IO ? off - APPLE_IO : off);
        }
    } // namespace

    NorthBridge Decoder::default_bridge_{};

    Decoder::Decoder(NorthBridge *bridge) : bridge_(bridge ? bridge : &default_bridge_) {}

    void Decoder::set_bridge(NorthBridge *bridge) { bridge_ = bridge ? bridge : &default_bridge_; }

    void Decoder::reset()
    {
        bridge_->reset();
        if (master_)
            master_->reset();
        reset_cards();
    }

    void Decoder::pulse_reset()
    {
        bridge_->reset_pulse();
        if (master_)
            master_->reset();
        reset_cards();
    }

    void Decoder::reset_cards()
    {
        for (const Slot &s : slots_) // one per device, however many mappings it has
            s.device->bus_reset();
    }

    void Decoder::idle()
    {
        last_wait_ = master_ ? master_->take_bus(*this) : 0;
        last_stolen_ = last_wait_;
        ++now_;
    }

    void Decoder::catch_up_device(const Device &device)
    {
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            Slot &s = slots_[i];
            if (s.device != &device)
                continue;
            if (s.timed && s.synced + 1 < now_)
            {
                s.device->tick(now_ - 1 - s.synced);
                s.synced = now_ - 1;
            }
            return;
        }
    }

    const Mapping *Decoder::dma_cycle(uint32_t physical, unsigned &cycles)
    {
        const Mapping *m = find(bus_address(Space::Memory, physical));
        cycles = 1;
        if (m)
            cycles += m->wait + (m->module != NO_MODULE ? bridge_->wait_states(m->module) : 0);
        now_ += cycles;
        if (m)
            catch_up(*m);
        return m;
    }

    void Decoder::dma_seen(uint32_t physical, const Mapping *m, unsigned cycles, uint8_t data, bool write)
    {
        if (!dma_observer_)
            return;
        last_ = Access{physical & (PHYSICAL_SPACE - 1), m ? m->module : NO_MODULE};
        last_wait_ = cycles - 1; // the CPU's access, after these, sets both again
        BusCycle c;
        c.cycle = now_;
        c.addr = static_cast<uint16_t>(physical);
        c.data = data;
        c.write = write;
        c.dma = true;
        dma_observer_->on_cycle(c);
    }

    unsigned Decoder::dma_idle()
    {
        ++now_;
        return 1;
    }

    unsigned Decoder::dma_read(uint32_t physical, uint8_t &data)
    {
        unsigned cycles = 0;
        const Mapping *m = dma_cycle(physical, cycles);
        data = m ? m->device->read(device_offset(*m, bus_address(Space::Memory, physical))) : UNMAPPED_READ;
        dma_seen(physical, m, cycles, data, false);
        return cycles;
    }

    unsigned Decoder::dma_write(uint32_t physical, uint8_t data)
    {
        unsigned cycles = 0;
        const Mapping *m = dma_cycle(physical, cycles);
        if (m)
            m->device->write(device_offset(*m, bus_address(Space::Memory, physical)), data);
        dma_seen(physical, m, cycles, data, true);
        return cycles;
    }

    unsigned Decoder::dma_fly_by(uint32_t physical, Device &card, bool to_memory, bool tc)
    {
        unsigned cycles = 0;
        const Mapping *m = dma_cycle(physical, cycles);
        catch_up_device(card);
        const uint32_t off = m ? device_offset(*m, bus_address(Space::Memory, physical)) : 0;
        uint8_t v = UNMAPPED_READ;
        if (to_memory)
        {
            v = card.dack_read(tc);
            if (m)
                m->device->write(off, v);
        }
        else
        {
            if (m)
                v = m->device->read(off);
            card.dack_write(v, tc);
        }
        dma_seen(physical, m, cycles, v, to_memory);
        return cycles;
    }

    bool Decoder::map(const Mapping &m)
    {
        if (!m.device || m.first > m.last || m.last >= SPACE_COUNT * PHYSICAL_SPACE ||
            space_of(m.first) != space_of(m.last))
            return false;
        mappings_.push_back(m);
        const uint64_t span = uint64_t{m.last} - m.first + 1;
        mirror_.push_back(m.device->size() && span > m.device->size() ? static_cast<uint32_t>(m.device->size()) : 0);
        size_t slot = 0;
        while (slot < slots_.size() && slots_[slot].device != m.device)
            ++slot;
        if (slot == slots_.size())
        {
            slots_.push_back(Slot{m.device, now_, m.device->timed()});
            if (m.device->timed())
                timed_.push_back(slot);
        }
        slot_of_.push_back(slot);
        if (m.irq_wired && std::find(irq_wired_.begin(), irq_wired_.end(), m.device) == irq_wired_.end())
            irq_wired_.push_back(m.device);
        return true;
    }

    uint32_t Decoder::device_offset(const Mapping &m, uint32_t where) const
    {
        const uint32_t off = where - m.first;
        const uint32_t size = mirror_[static_cast<size_t>(&m - mappings_.data())];
        return size ? off % size : off; // a chip decodes its own low lines only
    }

    const Mapping *Decoder::find(uint32_t where) const
    {
        for (auto it = mappings_.rbegin(); it != mappings_.rend(); ++it)
        {
            if (where >= it->first && where <= it->last)
                return &*it;
        }
        return nullptr;
    }

    uint32_t Decoder::physical(uint16_t addr, bool vector_pull) const
    {
        return lines(route(addr, vector_pull).where);
    }

    const Mapping *Decoder::decode(uint16_t addr, bool vector_pull) const
    {
        return find(route(addr, vector_pull).where);
    }

    uint8_t Decoder::module_at(uint16_t addr, bool vector_pull) const
    {
        const Mapping *m = decode(addr, vector_pull);
        return m ? m->module : NO_MODULE;
    }

    void Decoder::catch_up(const Mapping &m)
    {
        Slot &s = slots_[slot_of_[static_cast<size_t>(&m - mappings_.data())]];
        if (s.timed && s.synced + 1 < now_)
        {
            s.device->tick(now_ - 1 - s.synced);
            s.synced = now_ - 1;
        }
    }

    const Mapping *Decoder::access(uint16_t addr, Route &r, bool write)
    {
        const unsigned stolen = master_ ? master_->take_bus(*this) : 0; // RDY: the master goes first
        r = route(addr, vector_pull_, write);
        const Mapping *m = find(r.where);
        unsigned waits = 0;
        if (m)
            waits = m->wait + (m->module != NO_MODULE ? bridge_->wait_states(m->module) : 0);
        now_ += 1 + waits; // the access completes on the last held cycle
        last_wait_ = stolen + waits;
        last_stolen_ = stolen;
        last_ = Access{lines(r.where), m ? m->module : NO_MODULE};
        return m;
    }

    uint8_t Decoder::read(uint16_t addr)
    {
        Route r;
        const Mapping *m = access(addr, r, false);
        if (!m)
        {
            EMU_LOGV(TAG, "%04X -> unmapped", addr);
            return UNMAPPED_READ;
        }
        catch_up(*m);
        const uint8_t v = m->device->read(device_offset(*m, r.where));
        EMU_LOGV(TAG, "%04X -> %02X", addr, v);
        return v;
    }

    void Decoder::write(uint16_t addr, uint8_t data)
    {
        Route r;
        const Mapping *m = access(addr, r, true);
        EMU_LOGV(TAG, "%04X <- %02X%s", addr, data, !m ? " (unmapped)" : r.writable ? "" : " (read-only)");
        if (!m || !r.writable)
            return;
        catch_up(*m);
        m->device->write(device_offset(*m, r.where), data);
    }

    void Decoder::poke(uint16_t addr, uint8_t data)
    {
        const Route r = route(addr, false, true);
        if (const Mapping *m = find(r.where))
            m->device->poke(device_offset(*m, r.where), data);
    }

    uint8_t Decoder::peek_physical(uint32_t physical) const
    {
        const uint32_t where = bus_address(Space::Memory, physical);
        const Mapping *m = find(where);
        return m ? m->device->peek(device_offset(*m, where)) : UNMAPPED_READ;
    }

    void Decoder::write_physical(uint32_t physical, uint8_t data)
    {
        const uint32_t where = bus_address(Space::Memory, physical);
        if (const Mapping *m = find(where))
            m->device->write(device_offset(*m, where), data);
    }

    uint8_t Decoder::peek(uint16_t addr) const
    {
        const Route r = route(addr);
        const Mapping *m = find(r.where);
        return m ? m->device->peek(device_offset(*m, r.where)) : UNMAPPED_READ;
    }

    uint8_t Decoder::peek_io(uint16_t offset) const
    {
        const uint32_t where = bus_address(Space::Io, offset);
        const Mapping *m = find(where);
        return m ? m->device->peek(device_offset(*m, where)) : UNMAPPED_READ;
    }

    void Decoder::sync()
    {
        for (const size_t i : timed_)
        {
            Slot &s = slots_[i];
            if (s.synced < now_)
            {
                s.device->tick(now_ - s.synced);
                s.synced = now_;
            }
        }
    }

    bool Decoder::irq()
    {
        sync();
        for (const Device *d : irq_wired_)
        {
            if (d->irq())
                return true;
        }
        return false;
    }
} // namespace ooe::emu
