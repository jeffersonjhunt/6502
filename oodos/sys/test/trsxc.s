; TRSXC: test_oodos_rsx's RSX C (trsx.inc): remove flag $FF, non-banked 0, 0 bytes of zero page.

.define NAME "TRSXC"
LETTER  = 'C'
REMOVE  = $FF
NONBANK = 0
ZPN     = 0
        .include "trsx.inc"
