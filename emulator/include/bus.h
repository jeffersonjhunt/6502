#pragma once

#include <cstdint>

class BusInterface
{
public:
    /** 
     * @brief Read a byte from the bus
     */
    virtual uint8_t Read(uint16_t) = 0;
    /** 
     * @brief Write a byte to the bus
     */
    virtual void Write(uint16_t, uint8_t) = 0;
};
