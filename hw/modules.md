# Modules for 6502 computer #

## Chain ##

Clock -> CPU -> North Bridge -> RAM -> ROM -> Bus Spy (ESP32) -> LSD (Level Shift Driver) -> I/O (Serial/Parallel)

## Notes: ##

  - Only 1 active clock per stack. Each clock module producing a clock should have a disable switch.
  - Each module should regulate 12v to the required voltage V~CC2~.
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
|  A10 | ?      | ?     ·|·TBD       ·············································|
|  A11 | ?      | ?     ·|·TBD       ·············································|
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
|  B9  |        |       ·|·        ········· ··············      ·················|
|  B10 | GND    |       ·|·Ground        ········· ·······························|
|  B11 |        |       ·|      ·        ········· ·······························|
|  B12 |        |       ·|·              ········· ·······························|
|  B13 |        |       ·|·        ···      ······ ·······························|
|  B14 |        |       ·|·        ·········       ·······························|
|  B15 |        |       ·|·        ········· ·····      ··························|
|  B16 |        |       ·|·        ········· ···········      ····················|
|  B17 |        |       ·|·        ········· ·················      ··············|
|  B18 |        |       ·|·        ········· ·······················      ········|
|  B19 |        |       ·|·        ········· ··························      ·····|
|  B20 | PHI2   | OUT   ·|·CPU Clock    (50% duty cycle) ·············        ····|
|  B21 |        |       ·|·              ········· ·······························|
|  B22 |        |       ·|·              ········· ·······························|
|  B23 |        |       ·|·        ····      ····· ·······························|
|  B24 |        |       ·|·        ········      · ·······························|
|  B25 |        |       ·|·        ········· ··      ·····························|
|  B26 |        |       ·|·        ········· ·····      ··························|
|  B27 |        |       ·|·        ········· ········      ·······················|
|  B28 |        |       ·|·        ········· ·········      ······················|
|  B29 | +24VDC |       ·|·        ········· ·················      ··············|
|  B30 | OSC    | OUT   ·|·System Clock (50% duty cycle)       ···················|
|  B31 | GND    |       ·|·Ground        ········· ·······························|


```
       => RWB (Read Write)
       => OE (Output Enabled)
       => BE (Bus Enable)
       => Vector Pull (VPB)
       => Sync
       => Ready (RDY)
```


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
| 0x8     |                            |
| 0x9     |                            |
| 0xA     |                            |
| 0xB     |                            |
| 0xC     |                            |
| 0xD     |                            |
| 0xE     |                            |
| 0xF     | Clock                      |



