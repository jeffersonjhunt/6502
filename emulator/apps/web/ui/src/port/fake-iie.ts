/**
 * A stand-in for the owner's IIe ROM, for tests (the real one is Apple's
 * and never in the repo): 16 KB as at $C000-$FFFF with a IIe's ID byte
 * at $FBB3, and a reset at $FA62 that counts resets in $0300, turns on
 * ALTCHARSET ($C00F), clears text page 1 to spaces, puts an "A" at $0400
 * (the top left), switches slot 6's drive motor on and off again (so it
 * runs on for its second) and waits.
 */
export function fakeIIeRom(): Uint8Array {
  const rom = new Uint8Array(0x4000);
  const at = (addr: number) => addr - 0xc000;
  rom[at(0xfbb3)] = 0x06;
  rom[at(0xfffc)] = 0x62;
  rom[at(0xfffd)] = 0xfa;
  // prettier-ignore
  rom.set([
    0xee, 0x00, 0x03, //       INC $0300
    0x8d, 0x0f, 0xc0, //       STA $C00F
    0xa9, 0xa0,       //       LDA #$A0 (a space)
    0xa2, 0x00,       //       LDX #0
    0x9d, 0x00, 0x04, // $FA6C STA $0400,X
    0x9d, 0x00, 0x05, //       STA $0500,X
    0x9d, 0x00, 0x06, //       STA $0600,X
    0x9d, 0x00, 0x07, //       STA $0700,X
    0xe8,             //       INX
    0xd0, 0xf1,       //       BNE $FA6C
    0xa9, 0xc1,       //       LDA #$C1 ("A")
    0x8d, 0x00, 0x04, //       STA $0400
    0xad, 0xe9, 0xc0, // $FA80 LDA $C0E9 (the Disk II's motor on...)
    0xad, 0xe8, 0xc0, //       LDA $C0E8 (...and off: it runs on)
    0x4c, 0x86, 0xfa, // $FA86 JMP $FA86
  ], at(0xfa62));
  return rom;
}
