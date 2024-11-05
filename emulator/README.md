## MOS6502 Emulator in C++

This is my C++ implementation of the MOS Technology 6502 CPU. The code is written to be more readable than fast, however some minor tricks have been introduced to greatly reduce the overall execution time.

main features:

- 100% coverage of legal opcodes
- decimal mode implemented
- read/write bus callback
- jump table opcode selection


## Emulator features

Project is a simple jump-table based emulator: the actual value of the opcode (let's say 0x80) is used to address a function pointer table, each entry of such table is a C++ function which emulates the behavior of the corresponding real instruction.

All the 13 addressing modes are emulated:

```
// addressing modes
uint16_t Addr_ACC(); // ACCUMULATOR
uint16_t Addr_IMM(); // IMMEDIATE
uint16_t Addr_ABS(); // ABSOLUTE
uint16_t Addr_ZER(); // ZERO PAGE
uint16_t Addr_ZEX(); // INDEXED-X ZERO PAGE
uint16_t Addr_ZEY(); // INDEXED-Y ZERO PAGE
uint16_t Addr_ABX(); // INDEXED-X ABSOLUTE
uint16_t Addr_ABY(); // INDEXED-Y ABSOLUTE
uint16_t Addr_IMP(); // IMPLIED
uint16_t Addr_REL(); // RELATIVE
uint16_t Addr_INX(); // INDEXED-X INDIRECT
uint16_t Addr_INY(); // INDEXED-Y INDIRECT
uint16_t Addr_ABI(); // ABSOLUTE INDIRECT
```

All the 151 opcodes are emulated. Since the 6502 CPU uses 8 bit to encode the opcode value it also has a lot of "illegal opcodes" (i.e. opcode values other than the designed 151). Such opcodes perform weird operations, write multiple registers at the same time, sometimes are the combination of two or more "valid" opcodes. Such illegals were used to enforce software copy protection or to discover the exact CPU type.

The illegals are not supported yet, so instead a simple NOP is executed.

## Instruction Stepping

It's a fetch-decode-execute per step/tick, the "loop" is handled by the Emulator class.

```
	// fetch
	opcode = Read(pc++);

	// decode
	instr = InstrTable[opcode];

	// execute
	Exec(instr);
```

The next instruction (the opcode value) is retrieved from memory. Then it's decoded (i.e. the opcode is used to address the instruction table) and the resulting code block is executed.

## Public methods

The cpu emulator comes as a single C++ class with five public methods:

```
cpu(BusInterface *bus);
void NMI();
void IRQ();
void Reset();
void Step();
```

`cpu(BusInterface *bus);`

it's the class constructor. It requires you to pass an implementation of the BusInterface:

```
    /** 
     * @brief Read a byte from the bus
     */
    virtual uint8_t Read(uint16_t) = 0;
    /** 
     * @brief Write a byte to the bus
     */
    virtual void Write(uint16_t, uint8_t) = 0;
```

respectively to read/write from/to a memory location (16 bit address, 8 bit value). In such functions you can define your address decoding logic (if any) to address memory mapped I/O, external virtual devices and such.

```
void NMI();
```

triggers a Non-Mascherable Interrupt request, as done by the external pin of the real chip

```
void IRQ();
```

triggers an Interrupt ReQuest, as done by the external pin of the real chip

```
void Reset();
```

performs an hardware reset, as done by the external pin of the real chip

```
void Step();
```

It runs the CPU for the next machine instruction.

## Links

Some useful stuff I used...

https://github.com/gianlucag/mos6502

http://en.wikipedia.org/wiki/MOS_Technology_6502

http://www.6502.org/documents/datasheets/mos/

http://www.mdawson.net/vic20chrome/cpu/mos_6500_mpu_preliminary_may_1976.pdf

http://rubbermallet.org/fake6502.c
