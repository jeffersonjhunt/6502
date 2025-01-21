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

```
[1..16]  => [A0-A15]
[17..24] => [D0-D7]
25       => RWB (Read Write)
26       => OE (Output Enabled)
27       => IRQB (Interupt)
28       => NMIB (Non-Maskable Interupt)
29       => BE (Bus Enable)
30       => Vector Pull (VPB)
[31..34] => Peripheral Selector (0-15)
35       => Sync
36       => Ready (RDY)
37       => Reset (RESB) *See Notes
38       => Clock (PHI2)
39       => +12v (V~CC1~)
40       => GND
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



