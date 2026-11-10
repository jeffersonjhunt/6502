; OOLIB's zero page for gxmem.s, in a module of its own (the Programmer's
; Guide, chapter 8, "Linking the library into an overlay"): a program links
; it with the code that names it; an overlay is given its caller's addresses
; instead (tools/libzp.py names), so it has no zero page of its own.

        .exportzp GXP, GXQ, GXT

        .zeropage
GXP:    .res    2                      ; a pointer into W1, or anywhere
GXQ:    .res    2                      ; a second
GXT:    .res    2                      ; scratch
