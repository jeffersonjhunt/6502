#pragma once

/**
 * @file peripheral.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Something wired to a chip's pins rather than to the bus.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * An LCD on a 6522's ports, a PS/2 keyboard on its port A and CA1: they
 * have no address, so they are not Devices, but a machine description
 * still names and wires them (machine.h), and the machine owns them.
 */

#include <cstdint>

#include "via6522.h"

namespace ooe::emu
{
    class Peripheral
    {
    public:
        virtual ~Peripheral() = default;
    };

    /// One pin of a 6522 port.
    struct ViaPin
    {
        Via6522::Port port{Via6522::Port::A};
        uint8_t bit{0};
        uint8_t mask() const { return static_cast<uint8_t>(1u << bit); }
    };

    /// Consecutive pins of one 6522 port, lowest bit first.
    struct ViaPins
    {
        Via6522::Port port{Via6522::Port::A};
        uint8_t first{0};
        uint8_t count{8};
        uint8_t mask() const { return static_cast<uint8_t>(((1u << count) - 1) << first); }
    };
} // namespace ooe::emu
