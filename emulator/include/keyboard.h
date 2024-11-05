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
        Keyboard(BusInterface *bus, uint16_t address, uint8_t buffer_size=0x7F);
        ~Keyboard();

        uint8_t Pull();
    private:
        uint16_t buf_adr;
        uint8_t buf_size;
        uint16_t buf_ptr;
        BusInterface *bus;
        struct termios old_t;

        uint8_t DecodeKey(uint8_t key);
    };
} // namespace ooe