#pragma once
#include <bitset>
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
    void Poke(uint16_t addr, uint8_t data) override;

    /**
     * @brief Mark [first, last] read-only to the CPU (or writable again)
     *
     * CPU writes into a read-only range are dropped, as on a ROM. Poke still
     * writes, so loaders can fill it.
     */
    void SetReadOnly(uint16_t first, uint16_t last, bool protect = true);

    void Reset();

    Memory();
    ~Memory();
    
private:
    uint8_t memory[MEM_SIZE];
    std::bitset<MEM_SIZE> read_only;

    void ReadSideEffects(uint16_t addr);
    uint8_t WriteSideEffects(uint16_t addr, uint8_t data);

};