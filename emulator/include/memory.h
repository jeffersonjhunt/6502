#pragma once
#include <cstdint>

#include "fmt/core.h"

#include "bus.h"
#include "logging.h"

#define MEM_SIZE 65536 // 64K

#define IN      0x0200          //  Input buffer to $027F
#define KBD     0xD010          //  PIA.A keyboard input
#define KBDCR   0xD011          //  PIA.A keyboard control register
#define DSP     0xD012          //  PIA.B display output register
#define DSPCR   0xD013          //  PIA.B display control register

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

    void ReadSideEffects(uint16_t addr);
    void WriteSideEffects(uint16_t addr, uint8_t data);

};