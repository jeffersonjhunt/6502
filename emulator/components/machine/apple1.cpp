/**
 * @file apple1.cpp
 * @brief Apple-1 keyboard/display wiring.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "apple1.h"

namespace ooe::emu::apple1
{
    namespace
    {
#include "apple1_machine.inc" // const char APPLE1_MACHINE[]
    } // namespace

    const std::string &default_machine()
    {
        static const std::string text(APPLE1_MACHINE);
        return text;
    }

    Io::Io(Pia6821 &pia) : pia_(pia)
    {
        pia_.on_write_b([this](uint8_t out) {
            pending_ = static_cast<uint8_t>(out & 0x7F);
            have_pending_ = true;
            pia_.set_input_b(PB7_BUSY); // reads busy until output() takes it
        });
    }

    bool Io::key(uint8_t ascii)
    {
        if (pia_.cra() & Pia6821::CR_IRQ1)
            return false;
        pia_.set_input_a(encode(ascii));
        pia_.strobe_ca1();
        return true;
    }

    bool Io::output(uint8_t &ch)
    {
        if (!have_pending_)
            return false;
        ch = pending_;
        have_pending_ = false;
        pia_.set_input_b(0);
        return true;
    }

    uint8_t Io::encode(uint8_t ascii)
    {
        if (ascii >= 'a' && ascii <= 'z')
            ascii = static_cast<uint8_t>(ascii - 0x20); // the Apple-1 is upper case only
        if (ascii == '\n')
            ascii = '\r';
        return static_cast<uint8_t>(ascii | 0x80);    // PA7 is tied high
    }
} // namespace ooe::emu::apple1
