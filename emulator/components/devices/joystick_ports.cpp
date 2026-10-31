/**
 * @file joystick_ports.cpp
 * @brief The serial card's joystick ports.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "joystick_ports.h"

namespace ooe::emu
{
    JoystickPorts::JoystickPorts(Via6522 &via) : via_(via)
    {
        via_.drive(Via6522::Port::A, 0xFF, 0xFF); // the pull-ups, and the user port's open lines
        via_.drive(Via6522::Port::B, 0xFF, 0xFF);
    }

    void JoystickPorts::set(unsigned n, uint8_t pressed)
    {
        n %= STICKS;
        pressed_[n] = static_cast<uint8_t>(pressed & ALL);
        via_.drive(n == 0 ? Via6522::Port::A : Via6522::Port::B, ALL, static_cast<uint8_t>(~pressed_[n]));
    }

    bool JoystickPorts::parse(const std::string &text, uint8_t &pressed)
    {
        pressed = 0;
        if (text == "-")
            return true;
        if (text.empty())
            return false;
        for (const char c : text)
        {
            switch (c | 0x20) // lower case
            {
            case 'u': pressed |= UP; break;
            case 'd': pressed |= DOWN; break;
            case 'l': pressed |= LEFT; break;
            case 'r': pressed |= RIGHT; break;
            case 'f': pressed |= FIRE; break;
            default: return false;
            }
        }
        return true;
    }

    std::string JoystickPorts::describe() const
    {
        static constexpr const char *NAMES[] = {"up", "down", "left", "right", "fire"};
        std::string out;
        for (unsigned n = 0; n < STICKS; ++n)
        {
            out += "joystick " + std::to_string(n + 1) + ":";
            if (!pressed_[n])
                out += " -";
            for (unsigned b = 0; b < 5; ++b)
                if (pressed_[n] & (1u << b))
                    out += std::string(" ") + NAMES[b];
            out += "\n";
        }
        return out;
    }

    std::string JoystickPorts::command(const std::vector<std::string> &args)
    {
        if (args.empty())
            return describe();
        uint8_t bits = 0;
        if (args.size() != 2 || (args[0] != "1" && args[0] != "2") || !parse(args[1], bits))
            return "joy [1|2 STATE]: STATE is u d l r f, together (uf), or - for none\n";
        set(args[0] == "1" ? 0 : 1, bits);
        return describe();
    }
} // namespace ooe::emu
