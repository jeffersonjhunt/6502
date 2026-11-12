; OOUI's shared variables in the program's memory (hw/ooui.md, "U3a: OOUI as
; a banked library"): UISTATE, UI_STATE_LEN bytes on a page, in a segment of
; its own that the program's link config puts in a memory area of its own
; outside $4000-$7FFF (test/ui/uiprog.cfg puts it at $8000). OOUI.BNK's blocks
; reach it at the address the program's link gave (ui_load's fixups), and
; ui_load zeroes it. What the program reads is at its start: UI_EV (+0),
; UI_RECT (+12) and UI_RAW (+16).

        .setcpu "65C02"
        .include "ooui.inc"
        .export UISTATE, UI_EV, UI_RECT, UI_RAW

        .segment "UISTATE"
UISTATE: .res   UI_STATE_LEN
UI_EV   = UISTATE
UI_RECT = UISTATE + 12
UI_RAW  = UISTATE + 16
.ifndef UI_NOALIGN                      ; (test/ui/uitmis.s only: ui_load's own refusal, UX_STATE)
.assert <UISTATE = 0, lderror, "UISTATE must start on a page (its segment's area: align it, hw/ooui.md, U3a, E2)"
.endif
