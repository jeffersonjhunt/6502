; TRSXD: test_oodos_rsx's RSX D (trsx.inc): remove flag 0, non-banked $FF, 0 bytes of zero page.

.define NAME "TRSXD"
LETTER  = 'D'
REMOVE  = 0
NONBANK = $FF
ZPN     = 0
        .include "trsx.inc"
