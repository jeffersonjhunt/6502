/**
 * @file console.cpp
 * @brief Consoles for the PIA, ACIA, LCD and PS/2 keyboard.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "console.h"

#include <deque>

#include "acia6551.h"
#include "apple1.h"
#include "hd44780.h"
#include "pia6821.h"
#include "ps2_keyboard.h"

namespace ooe::emu
{
    namespace
    {
        /// A serial terminal on an ACIA: keys in as bytes, bytes out as text.
        class Serial
        {
        public:
            explicit Serial(Acia6551 &acia) : acia_(acia)
            {
                acia_.on_transmit([this](uint8_t b) { out_.push_back(b); });
            }
            bool listening() const { return acia_.command() & Acia6551::CMD_DTR; }
            bool key(uint8_t ascii) { return acia_.receive(ascii == '\n' ? '\r' : ascii); } // Enter is CR
            bool output(uint8_t &ch)
            {
                if (out_.empty())
                    return false;
                ch = out_.front();
                out_.pop_front();
                return true;
            }

        private:
            Acia6551 &acia_;
            std::deque<uint8_t> out_;
        };

        class MachineConsole : public Console
        {
        public:
            std::unique_ptr<apple1::Io> apple1;
            std::unique_ptr<Serial> serial;
            const ViaLcd *lcd{nullptr};
            Ps2Keyboard *keyboard{nullptr};

            bool key(uint8_t ascii) override
            {
                // Every listener must be able to take it, or none gets it yet.
                if (apple1)
                    return apple1->key(ascii);
                const bool to_serial = serial && serial->listening();
                if (to_serial && !serial->key(ascii))
                    return false;
                if (keyboard && keyboard->listening())
                    keyboard->key(ascii);
                return true;
            }

            bool output(uint8_t &ch) override
            {
                if (apple1)
                    return apple1->output(ch);
                return serial && serial->output(ch);
            }

            void service() override
            {
                if (keyboard)
                    keyboard->service();
            }

            bool display(std::vector<std::string> &rows) override
            {
                if (!lcd || !lcd->lcd().used() || lcd->lcd().version() == shown_)
                    return false;
                shown_ = lcd->lcd().version();
                rows.clear();
                for (unsigned r = 0; r < lcd->lcd().rows(); ++r)
                    rows.push_back(lcd->lcd().line(r));
                return true;
            }

        private:
            uint64_t shown_{0};
        };
    } // namespace

    std::unique_ptr<Console> make_console(Machine &machine, std::string &error)
    {
        auto c = std::make_unique<MachineConsole>();
        if (auto *pia = machine.find_as<Pia6821>(TERMINAL))
            c->apple1 = std::make_unique<apple1::Io>(*pia);
        else if (auto *acia = machine.find_as<Acia6551>(TERMINAL))
            c->serial = std::make_unique<Serial>(*acia);
        else if (machine.find(TERMINAL))
        {
            error = std::string("'") + TERMINAL + "' must be a pia6821 or an acia6551";
            return nullptr;
        }
        c->lcd = machine.find_peripheral<ViaLcd>(LCD);
        c->keyboard = machine.find_peripheral<Ps2Keyboard>(KEYBOARD);
        if (!c->apple1 && !c->serial && !c->lcd)
        {
            error = std::string("the machine has no '") + TERMINAL + "' (pia6821 or acia6551) and no '" + LCD +
                    "' (hd44780) to talk through";
            return nullptr;
        }
        return c;
    }
} // namespace ooe::emu
