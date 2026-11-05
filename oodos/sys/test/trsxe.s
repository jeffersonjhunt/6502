; TRSXE: test_oodos_rsx's RSX E (trsx.inc): remove flag 0, non-banked 0, 16 bytes of zero page.

.define NAME "TRSXE"
LETTER  = 'E'
REMOVE  = 0
NONBANK = 0
ZPN     = 16
        .include "trsx.inc"
