#pragma once

#include <iostream>
#include <cstdint>

#include <unistd.h>
#include <termios.h>

#include <fmt/core.h>

#include "bus.h"
#include "logging.h"

namespace ooe
{
    class Keyboard
    {
    public:
        Keyboard(BusInterface *bus, uint16_t ctrl_addr, uint16_t data_addr);
        ~Keyboard();

        uint8_t Pull();
        uint8_t EncodeKey(uint8_t key);

    private:
        uint16_t ctrl_addr;
        uint16_t data_addr;
        BusInterface *bus;
        struct termios old_t;
    };
} // namespace ooe