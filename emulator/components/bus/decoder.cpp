/**
 * @file decoder.cpp
 * @brief Address decoder implementation.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "decoder.h"

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

    uint8_t Decoder::read(uint16_t addr)
    {
        const Mapping *m = decode(addr);
        if (!m)
        {
            EMU_LOGV(TAG, "%04X -> unmapped", addr);
            return UNMAPPED_READ;
        }
        const uint8_t v = m->device->read(bridge_.physical(addr) - m->first);
        EMU_LOGV(TAG, "%04X -> %02X", addr, v);
        return v;
    }

    void Decoder::write(uint16_t addr, uint8_t data)
    {
        const Mapping *m = decode(addr);
        EMU_LOGV(TAG, "%04X <- %02X%s", addr, data, m ? "" : " (unmapped)");
        if (m)
            m->device->write(bridge_.physical(addr) - m->first, data);
    }

    void Decoder::poke(uint16_t addr, uint8_t data)
    {
        const Mapping *m = decode(addr);
        if (m)
            m->device->poke(bridge_.physical(addr) - m->first, data);
    }

    uint8_t Decoder::module_at(uint16_t addr) const
    {
        const Mapping *m = decode(addr);
        return m ? m->module : NO_MODULE;
    }

    bool Decoder::irq() const
    {
        for (const Mapping &m : mappings_)
        {
            if (m.irq_wired && m.device->irq())
                return true;
        }
        return false;
    }
} // namespace ooe::emu
