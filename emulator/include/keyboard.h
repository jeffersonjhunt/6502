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
    private:
        uint16_t ctrl_addr;
        uint16_t data_addr;
        BusInterface *bus;
        struct termios old_t;

        uint8_t DecodeKey(uint8_t key);
    };
} // namespace ooe