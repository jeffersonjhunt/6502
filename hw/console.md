# Console Panel #

The consol panel provides the direct input/ouput panel

## Illuminations ##

- Power On
- Run
- Halt

- [D0..D15] Data Bits - bits currently on the data bus
- [A0..A19] Addr Bits - bits currently on the addr bus (Note: DOES NOT ALWAYS MATCH ADDR swiches!!!)


## Switches ##

- System Key: a three position switch
  - Run
  - Step/Halt
  - Off

- [A0.A19] Addr Switch Bits: 20 individual switches
- Store/Fetch - Center off momentary switch
  - Store: places the content of the addr switches into memory
  - Fetch: returns the content of the addr switches from memory


- Step/Next: Momentary switch

