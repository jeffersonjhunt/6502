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

        /// A0-A23 for a bus address: the offset, or in the I/O hole the CPU address.
        uint32_t lines(uint32_t where)
        {
            if (space_of(where) != Space::Io)
                return offset_of(where);
            const uint32_t off = offset_of(where);
            return IO_HOLE + (off >= APPLE_IO ? off - APPLE_IO : off);
        }
    } // namespace

    NorthBridge Decoder::default_bridge_{};

    Decoder::Decoder(NorthBridge *bridge) : bridge_(bridge ? bridge : &default_bridge_) { dreq_module_.fill(-1); }

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
        granting_ = 0;
        last_wait_ = master_ ? master_->take_bus(*this) : 0;
        last_stolen_ = last_wait_;
        last_granted_ = granting_;
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

    uint8_t Decoder::select(NorthBridge::Master master, const Mapping *m) const
    {
        const uint8_t ps = bridge_->memory_select(master);
        if (ps != NO_MODULE)
            return ps;
        return m ? m->module : NO_MODULE;
    }

    const Mapping *Decoder::io_mapping_of(const Device &card) const
    {
        for (const Mapping &m : mappings_)
        {
            if (m.device == &card && space_of(m.first) == Space::Io)
                return &m;
        }
        return nullptr;
    }

    const Mapping *Decoder::dma_cycle(uint32_t where, unsigned &cycles, unsigned card_wait)
    {
        const Mapping *m = find(where);
        // /WAIT is one open-drain line: the cycle lasts until the last of those holding it lets go.
        cycles = 1 + std::max(m ? unsigned{m->wait} : 0u, card_wait);
        now_ += cycles;
        if (m)
            catch_up(*m);
        return m;
    }

    void Decoder::dma_seen(uint32_t where, const Mapping *m, bool is_fly_by, uint8_t module, unsigned cycles,
                           uint8_t data, bool write)
    {
        if (!dma_observer_)
            return; // nothing watching: no need to work out PS0-PS3
        const uint8_t ps = select(NorthBridge::Master::Dma, m);
        // A fly-by's grant names the card, by the channel's MODULE; a bus with no /MEM shows the memory's
        // module, as before.
        if (!is_fly_by || bridge_->memory_select(NorthBridge::Master::Dma) == NO_MODULE)
            module = ps;
        last_ = Access{lines(where), module};
        last_wait_ = cycles - 1; // the CPU's access, after these, sets both again
        BusCycle c;
        c.cycle = now_;
        c.addr = static_cast<uint16_t>(where);
        c.data = data;
        c.write = write;
        c.dma = true;
        dma_observer_->on_cycle(c);
    }

    unsigned Decoder::dma_idle()
    {
        ++granting_;
        ++now_;
        return 1;
    }

    unsigned Decoder::dma_poll()
    {
        ++granting_; // like the takeover: no observer sees it, and the card lights it as the CPU's
        ++now_;
        catch_up_dreq();
        return 1;
    }

    void Decoder::catch_up_dreq()
    {
        for (const size_t i : dreq_wired_)
            catch_up(mappings_[i]);
    }

    bool Decoder::dreq()
    {
        for (const size_t i : dreq_wired_) // one open-drain line: the first card pulling it is enough
        {
            const Mapping &m = mappings_[i];
            catch_up(m);
            if (m.device->drq())
                return true;
        }
        return false;
    }

    bool Decoder::dreq_from(uint8_t module) const
    {
        const Mapping *m = dreq_card(module);
        return m && m->device->drq();
    }

    const Mapping *Decoder::dreq_card(uint8_t module) const
    {
        if (module >= MODULE_IDS || dreq_module_[module] < 0)
            return nullptr;
        return &mappings_[static_cast<size_t>(dreq_module_[module])];
    }

    unsigned Decoder::dma_read(uint32_t physical, uint8_t &data)
    {
        unsigned cycles = 0;
        const uint32_t where = memory(physical);
        const Mapping *m = dma_cycle(where, cycles);
        data = m ? m->device->read(device_offset(*m, where)) : UNMAPPED_READ;
        dma_seen(where, m, false, NO_MODULE, cycles, data, false);
        return cycles;
    }

    unsigned Decoder::dma_write(uint32_t physical, uint8_t data)
    {
        unsigned cycles = 0;
        const uint32_t where = memory(physical);
        const Mapping *m = dma_cycle(where, cycles);
        if (m)
            m->device->write(device_offset(*m, where), data);
        dma_seen(where, m, false, NO_MODULE, cycles, data, true);
        return cycles;
    }

    unsigned Decoder::dma_fly_by(uint32_t physical, Device &card, bool to_memory, bool tc)
    {
        const Mapping *cm = io_mapping_of(card); // the card's I/O block: its /WAIT and its module
        return fly_by(physical, card, cm ? cm->wait : 0u, cm ? cm->module : NO_MODULE, to_memory, tc);
    }

    unsigned Decoder::dma_fly_by(uint32_t physical, const Mapping &card, uint8_t module, bool to_memory, bool tc)
    {
        return fly_by(physical, *card.device, card.wait, module, to_memory, tc);
    }

    unsigned Decoder::fly_by(uint32_t physical, Device &card, unsigned card_wait, uint8_t module, bool to_memory,
                             bool tc)
    {
        unsigned cycles = 0;
        const uint32_t where = memory(physical);
        const Mapping *m = dma_cycle(where, cycles, card_wait);
        catch_up_device(card);
        const uint32_t off = m ? device_offset(*m, where) : 0;
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
        dma_seen(where, m, true, module, cycles, v, to_memory);
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
        if (m.drq == Dreq::Wired)
        {
            const size_t index = mappings_.size() - 1;
            bool seen = false; // a device mapped twice pulls the one line once
            for (const size_t i : dreq_wired_)
                seen = seen || mappings_[i].device == m.device;
            if (!seen)
                dreq_wired_.push_back(index);
            if (m.module < MODULE_IDS && dreq_module_[m.module] < 0)
                dreq_module_[m.module] = static_cast<int>(index); // the first card wired on a module answers it
        }
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
        const Route r = route(addr, vector_pull);
        const Mapping *m = find(r.where);
        if (space_of(r.where) == Space::Memory)
            return select(NorthBridge::Master::Cpu, m);
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
        granting_ = 0;
        const unsigned stolen = master_ ? master_->take_bus(*this) : 0; // RDY: the master goes first
        last_granted_ = granting_;
        r = route(addr, vector_pull_, write);
        const Mapping *m = find(r.where);
        const unsigned waits = m ? m->wait : 0u; // /WAIT, which the north bridge turns into RDY
        now_ += 1 + waits; // the access completes on the last held cycle
        last_wait_ = stolen + waits;
        last_stolen_ = stolen;
        last_ = Access{lines(r.where),
                       space_of(r.where) == Space::Memory ? select(NorthBridge::Master::Cpu, m)
                       : m                                ? m->module
                                                          : NO_MODULE};
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
        const uint32_t where = memory(physical);
        const Mapping *m = find(where);
        return m ? m->device->peek(device_offset(*m, where)) : UNMAPPED_READ;
    }

    void Decoder::write_physical(uint32_t physical, uint8_t data)
    {
        const uint32_t where = memory(physical);
        if (const Mapping *m = find(where))
            m->device->write(device_offset(*m, where), data);
    }

    uint8_t Decoder::peek_bus(uint32_t physical) const
    {
        const uint32_t where = bus_address(Space::Memory, physical);
        const Mapping *m = find(where);
        return m ? m->device->peek(device_offset(*m, where)) : UNMAPPED_READ;
    }

    void Decoder::write_bus(uint32_t physical, uint8_t data)
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
