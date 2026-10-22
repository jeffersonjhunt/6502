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
            return space_of(where) == Space::Io ? IO_HOLE + offset_of(where) : offset_of(where);
        }
    } // namespace

    NorthBridge Decoder::default_bridge_{};

    Decoder::Decoder(NorthBridge *bridge) : bridge_(bridge ? bridge : &default_bridge_) {}

    void Decoder::set_bridge(NorthBridge *bridge) { bridge_ = bridge ? bridge : &default_bridge_; }

    bool Decoder::map(const Mapping &m)
    {
        if (!m.device || m.first > m.last || m.last >= SPACE_COUNT * PHYSICAL_SPACE ||
            space_of(m.first) != space_of(m.last))
            return false;
        mappings_.push_back(m);
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

    const Mapping *Decoder::access(uint16_t addr, Route &r)
    {
        r = route(addr, vector_pull_);
        const Mapping *m = find(r.where);
        last_wait_ = 0;
        if (m)
            last_wait_ = m->wait + (m->module != NO_MODULE ? bridge_->wait_states(m->module) : 0);
        now_ += 1 + last_wait_; // the access completes on the last held cycle
        last_ = Access{lines(r.where), m ? m->module : NO_MODULE};
        return m;
    }

    uint8_t Decoder::read(uint16_t addr)
    {
        Route r;
        const Mapping *m = access(addr, r);
        if (!m)
        {
            EMU_LOGV(TAG, "%04X -> unmapped", addr);
            return UNMAPPED_READ;
        }
        catch_up(*m);
        const uint8_t v = m->device->read(r.where - m->first);
        EMU_LOGV(TAG, "%04X -> %02X", addr, v);
        return v;
    }

    void Decoder::write(uint16_t addr, uint8_t data)
    {
        Route r;
        const Mapping *m = access(addr, r);
        EMU_LOGV(TAG, "%04X <- %02X%s", addr, data, !m ? " (unmapped)" : r.writable ? "" : " (read-only)");
        if (!m || !r.writable)
            return;
        catch_up(*m);
        m->device->write(r.where - m->first, data);
    }

    void Decoder::poke(uint16_t addr, uint8_t data)
    {
        const Route r = route(addr);
        if (const Mapping *m = find(r.where))
            m->device->poke(r.where - m->first, data);
    }

    uint8_t Decoder::peek(uint16_t addr) const
    {
        const Route r = route(addr);
        const Mapping *m = find(r.where);
        return m ? m->device->peek(r.where - m->first) : UNMAPPED_READ;
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
