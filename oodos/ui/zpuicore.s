; OOUI's zero page, in a module of its own (as OOLIB's are: the
; Programmer's Guide, chapter 8, "Linking the library into an overlay"): a
; program links it with uicore.s; an overlay is given its caller's
; addresses instead (tools/libzp.py names). Nothing is kept here between
; calls.

        .exportzp UIP, UIQ, UIR, UIT

        .zeropage
UIP:    .res    2                      ; a map's row (W1), or a string
UIQ:    .res    2                      ; a string, a second record, or the base map's row (a shadow)
UIR:    .res    2                      ; a record
UIT:    .res    2                      ; the theme
