; The screen statements' stubs, in the home block (the project's own; 7d1,
; hw/oodos.md, "The plan, phase 7"). Each far calls its routine in block P6
; (p6/gwsts.s) in screen mode, and is Advanced Feature in console mode, as it
; was (the owner's answer 7). FARCALL keeps A and the flags both ways.
;
;   WIDTH     GIO86.ASM's WIDTHS: WIDTH n[,m], the screen's (GWWID). OO-1:
;             WIDTH #f, WIDTH LPRINT and WIDTH "device" stay Advanced
;             Feature (their parts are P4's, which has no room for them yet)
;   COLOR CLS SCREEN LOCATE   GWSTS.ASM's

.include "gwbasic.inc"

.export WIDTH, COLOR, CLS, SCREEN, LOCATE
.import ADVERR, SCRMOD

.code

WIDTH:          BIT     SCRMOD
                BPL     ADV
                CMP     #'#'
                BEQ     ADV
                CMP     #LPRINT_TK
                BEQ     ADV
                CMP     #'"'
                BEQ     ADV
                FJSR    GWWIDP
                RTS
COLOR:          BIT     SCRMOD
                BPL     ADV
                FJSR    COLORP
                RTS
CLS:            BIT     SCRMOD
                BPL     ADV
                FJSR    CLSP
                RTS
SCREEN:         BIT     SCRMOD
                BPL     ADV
                FJSR    SCRNP
                RTS
LOCATE:         BIT     SCRMOD
                BPL     ADV
                FJSR    LOCATP
                RTS
ADV:            JMP     ADVERR
