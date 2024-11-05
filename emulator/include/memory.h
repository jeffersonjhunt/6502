#pragma once
#include <cstdint>

class MemoryInterface
{
public:
    virtual uint8_t Read(uint16_t) = 0;
    virtual void Write(uint16_t, uint8_t) = 0;
};

class Memory : public MemoryInterface
{
public:
    uint8_t Read(uint16_t addr) override;
    void Write(uint16_t addr, uint8_t data) override;
private:
    uint8_t memory[1024*64];
};