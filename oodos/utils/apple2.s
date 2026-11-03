; APPLE2.COM: restart the machine in Apple II mode (function 75, A = 1), the
; ROM BIOS's C806: the cartridge's IIe ROM copied into bank E and started, as
; a IIe starts at power-on; only a reset comes back to the OO-1. Without a IIe
; ROM in the cartridge, 75 returns $FF and this says so, "No IIe ROM in the
; cartridge (the emulator: Settings, ROMs)", the return code $FF00; OODOS goes
; on. The tail is ignored.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        lda     #1                      ; Apple II mode
        ldy     #O_ROM
        jsr     BDOS                    ; back only if there is no IIe ROM
        lda     #<snorom
        ldx     #>snorom
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
snorom: .byte   "No IIe ROM in the cartridge (the emulator: Settings, ROMs)$"
