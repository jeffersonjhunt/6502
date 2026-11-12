; OOUI's calls in the program (hw/ooui.md, "U3a: OOUI as a banked library"):
; a 7-byte stub each, in ui/uiimp.inc's order, which ui_load fills as JSR
; FARCALL, the block, the call's address in it, RTS. Until then each says
; UX_VERSION, carry set: a call before ui_load goes nowhere.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "ooui.inc"
        .include "../ui/uiimp.inc"
        .export UI_STUBS

.macro UIENT name
        .export name
name:   lda     #UX_VERSION
        sec
        rts
        .byte   0, 0, 0
.endmacro

        .data
UI_STUBS:
        UI_ENTRIES
.assert (* - UI_STUBS) .mod UI_STUB_LEN = 0, error, "a stub is 7 bytes"
