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
    } // namespace

    const NorthBridge Decoder::DEFAULT_BRIDGE{};

    Decoder::Decoder(const NorthBridge *bridge) : bridge_(bridge ? *bridge : DEFAULT_BRIDGE) {}

    bool Decoder::map(const Mapping &m)
    {
        if (!m.device || m.first > m.last || m.last >= PHYSICAL_SPACE)
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

    const Mapping *Decoder::decode(uint16_t addr) const
    {
        const uint32_t phys = bridge_.physical(addr);
        for (auto it = mappings_.rbegin(); it != mappings_.rend(); ++it)
        {
            if (phys >= it->first && phys <= it->last)
                return &*it;
        }
        return nullptr;
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

    uint8_t Decoder::read(uint16_t addr)
    {
        ++now_;
        const Mapping *m = decode(addr);
        if (!m)
        {
            EMU_LOGV(TAG, "%04X -> unmapped", addr);
            return UNMAPPED_READ;
        }
        catch_up(*m);
        const uint8_t v = m->device->read(bridge_.physical(addr) - m->first);
        EMU_LOGV(TAG, "%04X -> %02X", addr, v);
        return v;
    }

    void Decoder::write(uint16_t addr, uint8_t data)
    {
        ++now_;
        const Mapping *m = decode(addr);
        EMU_LOGV(TAG, "%04X <- %02X%s", addr, data, m ? "" : " (unmapped)");
        if (!m)
            return;
        catch_up(*m);
        m->device->write(bridge_.physical(addr) - m->first, data);
    }

    void Decoder::poke(uint16_t addr, uint8_t data)
    {
        const Mapping *m = decode(addr);
        if (m)
            m->device->poke(bridge_.physical(addr) - m->first, data);
    }

    uint8_t Decoder::peek(uint16_t addr) const
    {
        const Mapping *m = decode(addr);
        return m ? m->device->peek(bridge_.physical(addr) - m->first) : UNMAPPED_READ;
    }

    uint8_t Decoder::module_at(uint16_t addr) const
    {
        const Mapping *m = decode(addr);
        return m ? m->module : NO_MODULE;
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
