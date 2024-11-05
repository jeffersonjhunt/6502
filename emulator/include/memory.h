#pragma once
#include <cstdint>
#include "bus.h"

#define MEM_SIZE 65536 // 64K

class Memory : public BusInterface
{
public:
    uint8_t Read(uint16_t addr) override;
    void Write(uint16_t addr, uint8_t data) override;

    void Reset();

    Memory();
    ~Memory();
    
private:
    uint8_t memory[MEM_SIZE];
};