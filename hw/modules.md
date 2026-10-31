# Modules for 6502 computer #

## Chain ##

CPU + North Bridge + Clock (one card) -> RAM -> ROM -> Bus Spy (ESP32) -> LSD (Level Shift Driver) -> I/O (Serial/Parallel)

## Notes: ##

  - Only 1 active clock per stack. Each clock module producing a clock should have a disable switch.
  - Each module should regulate the bus's +24 V down to the voltage it needs (V~CC2~) at the point of use. This
    keeps the current on the bus low.
  - Reset should be pulled low by _any_ module should reset the system. Soft starts should go high after 100ms.
    - use a 100k resistor and 1uf cap to V~CC1~, clamp Reset high with a zener diode (3v3, 5v, etc...).
  - Each Module has an ID (2^4) set with lines 31 .. 34 by the North Bridge. More than one ID can be services by a
    pysical card. The ID in most cases sets Chip Enable (CE) or Chip Select (CS) of the target using a decoder.


## Bus Pin Out ##

ISA Card Slot with 62 Pins (e.g. EDC395062-ND) 20 Address Lines and 8 Data Lines


| PIN  |  Name  |  Dir   |  Description                                           |
| ---- | ------ | -----  | ------------------------------------------------------ |
|  A1  | GND    |        | Ground                                                 |
|  A2  | D7     | IN/OUT | Data Bit 7                                             |
|  A3  | D6     | IN/OUT·|·Data·Bit·6·············································|
|  A4  | D5     | IN/OUT·|·Data·Bit·5·············································|
|  A5  | D4     | IN/OUT·|·Data·Bit·4·············································|
|  A6  | D3     | IN/OUT·|·Data·Bit·3·············································|
|  A7  | D2     | IN/OUT·|·Data·Bit·2·············································|
|  A8  | D1     | IN/OUT·|·Data·Bit·1·············································|
|  A9  | D0     | IN/OUT·|·Data·Bit·0·············································|
|  A10 | RWB    | IN/OUT | Read/Write (CPU, or the DMA/bus master)                |
|  A11 | /OE    | OUT    | Read strobe from the north bridge                      |
|  A12 | A19    | OUT   ·|·Address Bit 19 ········································|
|  A13 | A18    | OUT   ·|·Address Bit 18 ········································|
|  A14 | A17    | OUT   ·|·Address Bit 17 ········································|
|  A15 | A16    | OUT   ·|·Address Bit 16 ········································|
|  A16 | A15    | OUT   ·|·Address Bit 15 ········································|
|  A17 | A14    | OUT   ·|·Address Bit 14 ········································|
|  A18 | A13    | OUT   ·|·Address Bit 13 ········································|
|  A19 | A12    | OUT   ·|·Address Bit 12 ········································|
|  A20 | A11    | OUT   ·|·Address Bit 11 ········································|
|  A21 | A10    | OUT   ·|·Address Bit 10 ········································|
|  A22 | A9     | OUT   ·|·Address Bit 9  ········································|
|  A23 | A8     | OUT   ·|·Address Bit 8 · ·······································|
|  A24 | A7     | OUT   ·|·Address Bit 7 ·· ······································|
|  A25 | A6     | OUT   ·|·Address Bit 6 ··· ·····································|
|  A26 | A5     | OUT   ·|·Address Bit 5 ···· ····································|
|  A27 | A4     | OUT   ·|·Address Bit 4 ····· ···································|
|  A28 | A3     | OUT   ·|·Address Bit 3 ······ ··································|
|  A29 | A2     | OUT   ·|·Address Bit 2 ······· ·································|
|  A30 | A1     | OUT   ·|·Address Bit 1 ········ ································|
|  A31 | A0     | OUT   ·|·Address Bit 0 ········· ·······························|
|  B1  | GND    |       ·|·Ground        ········· ·······························|
|  B2  | /RESET | OUT   ·|·Reset or initialize system logic                · ·····|
|  B3  | /NMI   | OUT   ·|·Non-maskable interupt · ·······························|
|  B4  | /IRQ   | IN    ·|·Interupt       ·· ········      ·······················|
|  B5  | PS3    | OUT   ·|·Peripheral·Selector Bit 3  · ··························|
|  B6  | PS2    | OUT   ·|·Peripheral·Selector Bit 2 ·····························|
|  B7  | PS1    | OUT   ·|·Peripheral·Selector Bit 1 ·····························|
|  B8  | PS0    | OUT   ·|·Peripheral·Selector Bit 0 ·····························|
|  B9  | SYNC   | OUT    | Opcode fetch (single-step, bus spy)                    |
|  B10 | GND    |       ·|·Ground        ········· ·······························|
|  B11 | VPB    | OUT    | Vector pull (north bridge vector rule)                 |
|  B12 | RDY    | IN     | Ready, open drain: wait states, DMA halt               |
|  B13 | BE     | IN     | Bus Enable, open drain: low floats the CPU             |
|  B14 | /IACK  | OUT    | Interrupt status poll                                  |
|  B15 | DRQ0   | DMA    | DMA request, channel 0 (storage)                       |
|  B16 | /DACK0 | DMA    | DMA acknowledge, channel 0                             |
|  B17 | DRQ1   | DMA    | DMA request, channel 1                                 |
|  B18 | /DACK1 | DMA    | DMA acknowledge, channel 1                             |
|  B19 | GND    |        | Ground (shields PHI2)                                  |
|  B20 | PHI2   | OUT   ·|·CPU Clock    (50% duty cycle) ·············        ····|
|  B21 | GND    |        | Ground (shields PHI2)                                  |
|  B22 | DRQ2   | DMA    | DMA request, channel 2                                 |
|  B23 | /DACK2 | DMA    | DMA acknowledge, channel 2                             |
|  B24 | DRQ3   | DMA    | DMA request, channel 3                                 |
|  B25 | /DACK3 | DMA    | DMA acknowledge, channel 3                             |
|  B26 | TC     | DMA    | DMA terminal count                                     |
|  B27 | AMODE  | OUT    | Apple II mode is active                                |
|  B28 | /BOOTSEL| IN     | Boot from RAM, not ROM (bus spy), open drain           |
|  B29 | +24VDC |       ·|·        ········· ·················      ··············|
|  B30 | OSC    | OUT   ·|·System Clock (50% duty cycle)       ···················|
|  B31 | GND    |       ·|·Ground        ········· ·······························|


Pull-ups (RDY, BE, /BOOTSEL, /RESET, /NMI, /IRQ, and weak ones on D0-D7 for /IACK) and pull-downs (DRQ0-DRQ3)
live on the backplane. Signal details are in [design.md](design.md#backplane-signals).


## Modules Id List ##

| Address | Module                     |
| ------- | -------------------------- |
| 0x0     | RAM A                      |
| 0x1     | RAM B                      |
| 0x2     | RAM C                      |
| 0x3     | RAM D                      |
| 0x4     | ROM A                      |
| 0x5     | ROM B (Game Cart)          |
| 0x6     | Serial A/B & Parallel      |
| 0x7     | Mass Storage               |
| 0x8     | DMA Controller             |
| 0x9     | Video                      |
| 0xA     | Sound (2x SID)             |
| 0xB     | USB, Game Port & Speaker   |
| 0xC     | Disk II Controller         |
| 0xD     | North Bridge (registers)   |
| 0xE     | Console                    |
| 0xF     | (free: the clock moved onto the CPU card, 2026-10-30) |



