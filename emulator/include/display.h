#pragma once

#include <cstdint>

#include <fmt/core.h>

#include "bus.h"
#include "logging.h"


namespace ooe
{

class Display
{
    public:
        Display( BusInterface *bus, uint16_t ctrl_addr, uint16_t data_addr );
        ~Display();

        void update();
    private:
        BusInterface *bus;
        uint16_t ctrl_addr;
        uint16_t data_addr;
        uint8_t last_data = 0x0;
};

} // namespace ooe