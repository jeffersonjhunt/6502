; OOLIB's zero page for sidnote.s, in a module of its own (the Programmer's
; Guide, chapter 8, "Linking the library into an overlay"): a program links
; it with the code that names it; an overlay is given its caller's addresses
; instead (tools/libzp.py names), so it has no zero page of its own.

        .exportzp SIDNOTE_ZP

        .zeropage
SIDNOTE_ZP: .res    2
