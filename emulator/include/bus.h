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

    /** 
     * @brief Set a bit in a byte
     */
    inline uint8_t bit_set(uint8_t number, uint8_t n) {
        return number | ((uint8_t)1 << n);
    };

    /** 
     * @brief Clear a bit in a byte
     */
    inline uint8_t bit_clear(uint8_t number, uint8_t n) {
        return number & ~((uint8_t)1 << n);
    };

    /** 
     * @brief Flip a bit in a byte
     */
    inline uint8_t bit_flip(uint8_t number, uint8_t n) {
        return number ^ ((uint8_t)1 << n);
    };

    /** 
     * @brief Test a bit in a byte
     */
    inline bool bit_test(uint8_t number, uint8_t n) {
        return (number >> n) & (uint8_t)1;
    };
};
