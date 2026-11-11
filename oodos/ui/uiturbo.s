; OOUI's Turbo Vision theme, UI_TURBO (hw/ooui.md, open question 3): blue
; windows on a grey desktop in the CGA's colours. Colour 0 is transparent
; on a layer, so black text on grey needs an opaque black: entry 13 is
; black here (the CGA's light magenta, which this look doesn't use).

        .setcpu "65C02"
        .include "ooui.inc"

.macro RGB c
        .byte   <(c), >(c)
.endmacro

BLACK0  = 0                            ; transparent
BLUE    = 1
GREEN   = 2
CYAN    = 3
RED     = 4
MAGENTA = 5
BROWN   = 6
GREY    = 7
DGREY   = 8
LBLUE   = 9
LGREEN  = 10
LCYAN   = 11
LRED    = 12
BLACK   = 13                           ; opaque
YELLOW  = 14
WHITE   = 15
.define AT(b, f) ((b) << 4 | (f))

        .rodata
UI_TURBO:
        .byte   1                       ; TH_VERSION
        .byte   TF_SHADOW               ; TH_FLAGS
        .word   PAL, 0                  ; TH_PALETTE, TH_OVERPAL
        .byte   0, 0, 0                 ; TH_GROUP, TH_OVERGROUP, TH_DIMGROUP
        .byte   $B1                     ; TH_DESKCHAR
        .byte   1, 0, 1, 1, 2, 1, 1, 0  ; TH_METRICS
        .byte   8, 16, 1, 2, 0, 0, 0, 0 ; TH_PIXELS
        .byte   30, 2, 24, 16           ; TH_TIMES
        .byte   TS_DOUBLE, TS_DOUBLE, TS_SINGLE, TS_SINGLE ; TH_FRAMES
        .byte   AT(GREY, BLUE)          ; TC_DESKTOP
        .byte   AT(BLUE, WHITE)         ; TC_FRAME
        .byte   AT(BLUE, WHITE)         ; TC_TITLE
        .byte   AT(BLUE, GREY)          ; TC_TITLEOFF
        .byte   AT(BLUE, YELLOW)        ; TC_CLIENT
        .byte   AT(BLUE, YELLOW)        ; TC_LABEL
        .byte   AT(GREEN, BLACK)        ; TC_BUTTON
        .byte   AT(GREEN, WHITE)        ; TC_BUTTONFOCUS
        .byte   AT(CYAN, WHITE)         ; TC_BUTTONDOWN
        .byte   AT(BLUE, DGREY)         ; TC_DISABLED
        .byte   AT(0, LRED)             ; TC_ACCEL
        .byte   AT(BLUE, YELLOW)        ; TC_CHECK
        .byte   AT(GREY, BLACK)         ; TC_MENUBAR
        .byte   AT(GREY, BLACK)         ; TC_MENU
        .byte   AT(GREEN, BLACK)        ; TC_MENUSEL
        .byte   AT(CYAN, BLACK)         ; TC_LIST
        .byte   AT(BLUE, WHITE)         ; TC_LISTSEL
        .byte   AT(GREEN, WHITE)        ; TC_LISTCUR
        .byte   AT(CYAN, DGREY)         ; TC_LISTDIM
        .byte   AT(CYAN, YELLOW)        ; TC_LISTMARK
        .byte   AT(BLUE, WHITE)         ; TC_FIELD
        .byte   AT(GREEN, WHITE)        ; TC_FIELDFOCUS
        .byte   AT(BLUE, CYAN)          ; TC_SCROLL
        .byte   AT(CYAN, BLUE)          ; TC_THUMB
        .byte   AT(BLUE, LGREEN)        ; TC_BAR
        .byte   AT(BLUE, DGREY)         ; TC_BAREMPTY
        .byte   AT(BLACK, DGREY)        ; TC_SHADOW
        .byte   AT(GREY, BLACK)         ; TC_DIALOG
        .byte   AT(GREY, WHITE)         ; TC_DIALOGFRAME
        .byte   AT(BLACK, WHITE)        ; TH_POINTER: outline, fill
.assert * - UI_TURBO = TH_LEN, error, "a theme is 64 bytes"

PAL:    RGB     $000
        RGB     $00A
        RGB     $0A0
        RGB     $0AA
        RGB     $A00
        RGB     $A0A
        RGB     $A50
        RGB     $AAA
        RGB     $555
        RGB     $55F
        RGB     $5F5
        RGB     $5FF
        RGB     $F55
        RGB     $000
        RGB     $FF5
        RGB     $FFF
