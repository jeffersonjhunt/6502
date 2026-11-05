; TRSXB: test_oodos_rsx's RSX B (trsx.inc): remove flag 0, non-banked 0, 4 bytes of zero page.

.define NAME "TRSXB"
LETTER  = 'B'
REMOVE  = 0
NONBANK = 0
ZPN     = 4
        .include "trsx.inc"
