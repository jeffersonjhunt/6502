#pragma once

#include <iostream>
#include <cstdint>

#include <stdint.h>
#include <unistd.h>
#include <termios.h>

#include "bus.h"

namespace ooe
{
    class Keyboard
    {
    public:
        Keyboard(BusInterface *bus, uint16_t address, bool blocking=false);
        ~Keyboard();

        uint8_t Pull();
    private:
        uint16_t address;
        BusInterface *bus;
        struct termios old_t;
    };
} // namespace ooe