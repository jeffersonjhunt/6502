#pragma once

/**
 * @file joystick_ports.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The serial card's two digital joystick ports, on its W65C22S's ports.
 * @version 0.1.0
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md, "The VIA and the joystick ports": two DE-9s in the Atari
 * and Commodore pinout. Joystick 1 is PA0-PA4, joystick 2 PB0-PB4: up,
 * down, left, right, fire, each switch closing to ground, so a line reads
 * 0 while pressed. The card's pull-ups hold the rest at 1, and the user
 * port's lines (PA5-PA7, PB5-PB7) read 1 too while nothing drives them,
 * so a game reads a stick with `LDA $C341`, `AND #$1F` and sees $1F with
 * nothing pressed or no stick plugged in.
 *
 * The host says what is pressed (set()), as bits that are 1 for pressed;
 * the lines are their inverse. A line the VIA drives as an output reads
 * as on the chip: port A reads the pin, so a pressed switch reads 0
 * whatever ORA holds; port B reads ORB (the switch shorts a driven-high
 * line either way: don't).
 *
 * The terminal app has no host joystick: its debugger's `joy 1 uf` holds
 * joystick 1 up with fire pressed, `joy 1 -` lets go, `joy` shows both.
 *
 * In a machine description, after the via6522 line it hangs off:
 *
 *   joyports  name=joysticks  via=via
 */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "peripheral.h"
#include "via6522.h"

namespace ooe::emu
{
    class JoystickPorts : public Peripheral
    {
    public:
        // set()'s bits, 1 = pressed; the port's lines are their inverse.
        static constexpr uint8_t UP = 0x01;
        static constexpr uint8_t DOWN = 0x02;
        static constexpr uint8_t LEFT = 0x04;
        static constexpr uint8_t RIGHT = 0x08;
        static constexpr uint8_t FIRE = 0x10;
        static constexpr uint8_t ALL = 0x1F;
        static constexpr unsigned STICKS = 2;

        /// Pulls both ports up: nothing pressed.
        explicit JoystickPorts(Via6522 &via);

        /// What stick `n` (0: joystick 1, on port A; 1: joystick 2, port B) has pressed.
        void set(unsigned n, uint8_t pressed);
        uint8_t pressed(unsigned n) const { return pressed_[n % STICKS]; }

        /// "udlrf" letters (any order, any case) to set()'s bits; "-" is none.
        /// @return false for anything else
        static bool parse(const std::string &text, uint8_t &pressed);
        /// "joystick 1: up fire" and "joystick 2: -", a line each.
        std::string describe() const;
        /// The terminal debugger's `joy`: no arguments shows them; `joy N STATE`
        /// (N 1 or 2, STATE for parse()) holds stick N so until the next.
        std::string command(const std::vector<std::string> &args);

    private:
        Via6522 &via_;
        std::array<uint8_t, STICKS> pressed_{};
    };
} // namespace ooe::emu
