/**
 * @file backplane_bridge.cpp
 * @brief The backplane's north bridge.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "backplane_bridge.h"

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "northbridge";

        constexpr uint32_t WINDOW_SHIFT = 14; ///< 16 KB windows and blocks
        constexpr uint32_t WINDOW_MASK = (1u << WINDOW_SHIFT) - 1;
    } // namespace

    BackplaneBridge::BackplaneBridge(bool bootsel) : bootsel_(bootsel) { apply_reset(); }

    void BackplaneBridge::apply_reset()
    {
        const uint8_t top = bootsel_ ? BOOTSEL_TOP : RESET_TOP;
        win_ = {0, 1, 2, top};
        vec_ = top;
        mode_ = 0;
        wait_ = {};
    }

    void BackplaneBridge::reset()
    {
        apply_reset();
        // The machine's first reset is the power-on one; STATUS says so until the next.
        power_on_ = !reset_seen_;
        reset_seen_ = true;
    }

    Route BackplaneBridge::route(uint16_t cpu_addr, bool vector_pull) const
    {
        if (cpu_addr >= IO_HOLE && cpu_addr < IO_HOLE + IO_SIZE)
            return Route{bus_address(Space::Io, cpu_addr - IO_HOLE), true};
        const unsigned n = cpu_addr >> WINDOW_SHIFT;
        const uint8_t reg = (n == 3 && vector_pull) ? vec_ : win_[n];
        const uint32_t offset = static_cast<uint32_t>(reg & BLOCK) << WINDOW_SHIFT | (cpu_addr & WINDOW_MASK);
        return Route{bus_address(reg & ROM ? Space::Flash : Space::Memory, offset), (reg & RO) == 0};
    }

    unsigned BackplaneBridge::wait_states(uint8_t module) const
    {
        return (wait_[(module >> 2) & 3] >> ((module & 3) * 2)) & 3u;
    }

    uint8_t BackplaneBridge::poll(bool high) const
    {
        uint8_t v = 0xFF;
        if (!decoder_)
            return v;
        for (const Mapping &m : decoder_->mappings())
        {
            if (m.irq_wired && m.module != NO_MODULE && (m.module >= 8) == high && m.device->irq())
                v = static_cast<uint8_t>(v & ~(1u << (m.module & 7)));
        }
        return v;
    }

    uint8_t BackplaneBridge::read(uint32_t offset)
    {
        if ((offset == IRQL || offset == IRQH) && decoder_)
            decoder_->sync(); // the poll sees every card as of this cycle
        return peek(offset);
    }

    uint8_t BackplaneBridge::peek(uint32_t offset) const
    {
        switch (offset)
        {
        case WIN0:
        case WIN0 + 1:
        case WIN0 + 2:
        case WIN0 + 3:
            return win_[offset - WIN0];
        case VEC:
            return vec_;
        case MODE:
            return mode_;
        case STATUS:
            return static_cast<uint8_t>(REVISION << 4 | (power_on_ ? STATUS_POWER_ON : 0) |
                                        (bootsel_ ? STATUS_BOOTSEL : 0));
        case IRQL:
            return poll(false);
        case IRQH:
            return poll(true);
        case WAIT0:
        case WAIT0 + 1:
        case WAIT0 + 2:
        case WAIT0 + 3:
            return wait_[offset - WAIT0];
        case ID_REG:
            return ID;
        default:
            return 0xFF;
        }
    }

    void BackplaneBridge::write(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
        case WIN0:
        case WIN0 + 1:
        case WIN0 + 2:
        case WIN0 + 3:
            win_[offset - WIN0] = data;
            break;
        case VEC:
            vec_ = data;
            break;
        case MODE:
            if (data & MODE_APPLE)
                EMU_LOGW(TAG, "MODE APPLE: Apple II mode is not modelled yet; ignored");
            mode_ = static_cast<uint8_t>(data & ~MODE_APPLE);
            break;
        case WAIT0:
        case WAIT0 + 1:
        case WAIT0 + 2:
        case WAIT0 + 3:
            wait_[offset - WAIT0] = data;
            break;
        default:
            break; // read-only or unused
        }
    }
} // namespace ooe::emu
