#pragma once

#include <iostream>

#include <stdint.h>
#include <unistd.h>
#include <termios.h>

namespace ooe
{
    class Keyboard
    {
    public:
        Keyboard(bool blocking=false);
        ~Keyboard();

        uint8_t Pull();
    };
} // namespace ooe