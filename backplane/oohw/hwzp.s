; oohw's zero page (oohw.inc): one pointer, taken by any module that uses
; (zp),Y. In OODOS's build it is the program's own ZEROPAGE, $00-$EF.

        .setcpu "65C02"
        .include "oohw.inc"

        OOHW_ZP
HW_PTR: .res    2
