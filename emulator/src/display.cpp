#include "display.h"


namespace ooe
{
    Display::Display( BusInterface *bus, uint16_t ctrl_addr, uint16_t data_addr )
    {
        this->bus = bus;
        this->ctrl_addr = ctrl_addr;
        this->data_addr = data_addr;
    }

    Display::~Display()
    {
    }

    void Display::update()
    {
        uint8_t data = bus->Read( this->data_addr );
        if( bus->bit_test(data, 7) ) // check for data available
        {            
            data = bus->bit_clear(data, 7); // adjust for apple 1 display encoding
   
            std::cout << (char)data;
            if(data == 0x0d) // CR -> LF
            {
                std::cout << std::endl;
            } else {
                std::cout << std::flush;
            }

           bus->Write(this->data_addr, data); // clear the data available flag
        }
    }
} // namespace ooe