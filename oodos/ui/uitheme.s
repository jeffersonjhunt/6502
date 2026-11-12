; OOUI's default theme, UI_HOUSE (hw/ooui.md, open question 3): the OO-1's
; house look, OO-Amp's 16 colours from its skin sheet (its tools/mkgfx.py)
; in their order, so a program that shares OO-Amp's palette shares OOUI's.

        .setcpu "65C02"
        .include "ooui.inc"

.macro RGB c
        .byte   <(c), >(c)
.endmacro

; The colours, by name (OO-Amp's).
BG      = 0                            ; $112 (transparent on a layer: entry 0 shows)
PANEL   = 1
MID     = 2
LITE    = 3
HI      = 4
LCD     = 5
GRN     = 6
DGRN    = 7
DIM     = 8
YEL     = 9
RED     = 10
OOE     = 11
AMB     = 12
SEL     = 13
WHT     = 14
LEDOFF  = 15
.define AT(b, f) ((b) << 4 | (f))

.ifdef UIBANK
        .segment "URODATA"              ; the bank's own copy: UICOM's, so every block reads it (U3a)
.else
        .rodata
.endif
UI_HOUSE:
        .byte   1                       ; TH_VERSION
        .byte   TF_SHADOW               ; TH_FLAGS
        .word   PAL, 0                  ; TH_PALETTE, TH_OVERPAL
        .byte   0, 0, 0                 ; TH_GROUP, TH_OVERGROUP, TH_DIMGROUP
        .byte   $B1                     ; TH_DESKCHAR: a medium shade
        .byte   1, 0, 1, 1, 2, 1, 1, 0  ; TH_METRICS
        .byte   8, 16, 1, 2, 0, 0, 0, 0 ; TH_PIXELS
        .byte   30, 2, 24, 16           ; TH_TIMES
        .byte   TS_SINGLE, TS_DOUBLE, TS_SINGLE, TS_SINGLE ; TH_FRAMES
        .byte   AT(BG, PANEL)           ; TC_DESKTOP
        .byte   AT(PANEL, LITE)         ; TC_FRAME
        .byte   AT(SEL, WHT)            ; TC_TITLE
        .byte   AT(PANEL, HI)           ; TC_TITLEOFF
        .byte   AT(PANEL, HI)           ; TC_CLIENT
        .byte   AT(PANEL, HI)           ; TC_LABEL
        .byte   AT(MID, WHT)            ; TC_BUTTON
        .byte   AT(LCD, GRN)            ; TC_BUTTONFOCUS
        .byte   AT(SEL, GRN)            ; TC_BUTTONDOWN
        .byte   AT(PANEL, MID)          ; TC_DISABLED
        .byte   AT(0, AMB)              ; TC_ACCEL
        .byte   AT(PANEL, HI)           ; TC_CHECK
        .byte   AT(MID, HI)             ; TC_MENUBAR
        .byte   AT(MID, WHT)            ; TC_MENU
        .byte   AT(SEL, WHT)            ; TC_MENUSEL
        .byte   AT(LCD, HI)             ; TC_LIST
        .byte   AT(SEL, WHT)            ; TC_LISTSEL
        .byte   AT(LCD, GRN)            ; TC_LISTCUR
        .byte   AT(LCD, MID)            ; TC_LISTDIM
        .byte   AT(LCD, AMB)            ; TC_LISTMARK
        .byte   AT(LCD, HI)             ; TC_FIELD
        .byte   AT(LCD, GRN)            ; TC_FIELDFOCUS
        .byte   AT(PANEL, MID)          ; TC_SCROLL
        .byte   AT(MID, LITE)           ; TC_THUMB
        .byte   AT(LCD, GRN)            ; TC_BAR
        .byte   AT(LCD, LEDOFF)         ; TC_BAREMPTY
        .byte   AT(LCD, MID)            ; TC_SHADOW
        .byte   AT(PANEL, HI)           ; TC_DIALOG
        .byte   AT(PANEL, LITE)         ; TC_DIALOGFRAME
        .byte   AT(LCD, WHT)            ; TH_POINTER: outline, fill
.assert * - UI_HOUSE = TH_LEN, error, "a theme is 64 bytes"

PAL:    RGB     $112
        RGB     $234
        RGB     $456
        RGB     $789
        RGB     $BCD
        RGB     $000
        RGB     $3F3
        RGB     $141
        RGB     $6A6
        RGB     $FE2
        RGB     $F32
        RGB     $D22
        RGB     $FB3
        RGB     $24A
        RGB     $FFF
        RGB     $030
