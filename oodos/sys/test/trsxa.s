; TRSXA: test_oodos_rsx's RSX A (trsx.inc): remove flag 0, non-banked 0, 0 bytes of zero page.

.define NAME "TRSXA"
LETTER  = 'A'
REMOVE  = 0
NONBANK = 0
ZPN     = 0
        .include "trsx.inc"
