; The screen statements' stubs, in the home block (the project's own; 7d1,
; hw/oodos.md, "The plan, phase 7"). Each far calls its routine in block P6
; (p6/gwsts.s) in screen mode, and is Advanced Feature in console mode, as it
; was (the owner's answer 7). FARCALL keeps A and the flags both ways.
;
;   WIDTH     GIO86.ASM's WIDTHS: WIDTH n[,m], the screen's (GWWID: screen
;             mode only); WIDTH #f,n a file's (FILWID); WIDTH LPRINT n
;             LPT1's; WIDTH "device",n the device's set-width function, by
;             P4's DEVSWD (SCRN: and KYBD: the screen's too, P6's SWIDP, so
;             screen mode only; CONS: Bad file mode; a disk Bad file name;
;             P4's room: 7d2's answer 7). The file,
;             LPRINT and printer forms work in console mode too (7d2's
;             answer 6). OO-1: the device's name is evaluated twice, once
;             here for its type and once by P4's NAMSCN (a recorded
;             deviation: only a user function with side effects could tell)
;   COLOR CLS SCREEN LOCATE   GWSTS.ASM's

.include "gwbasic.inc"

.export WIDTH, COLOR, CLS, SCREEN, LOCATE
.import ADVERR, SCRMOD, CHRGTR, GETBYT, SYNCHR, FRMEVL, FREFAC, FCERR, LINLEN

.code

WIDTH:          CMP     #'#'
                BEQ     WIDFIL
                CMP     #','
                BEQ     CRTWD1
                CMP     #LPRINT_TK
                BNE     NOTLPR
                JSR     CHRGTR         ; WIDTH LPRINT n: LPT1's (LPTSWD's
                JSR     GETWDT         ; store, its width resident)
                STA     LINLEN+1
                RTS
NOTLPR:         LDA     TXTPTR+1       ; a string or a number?
                PHA
                LDA     TXTPTR
                PHA
                JSR     FRMEVL
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                LDA     VALTYP
                CMP     #VT_STR
                BNE     CRTWD1         ; a number: the screen's
                JSR     FREFAC         ; its temporary freed
                FJSR    NAMSCN         ; FILDEV: the device (evaluated again)
                JSR     SYNCHR
                .byte   ','
                JSR     GETWDT
                CLC
                FJSR    DEVSWD         ; its G_SWD
                BCS     @x             ; not the screen's
                LDA     RDL
                FJSR    SWIDP          ; SCNSWD's SWIDTH
@x:             RTS
CRTWD1:         BIT     SCRMOD
                BPL     ADV
                FJSR    GWWIDP
                RTS
WIDFIL:         JSR     CHRGTR         ; WIDTH #f,n
                JSR     GETBYT
                PHA
                FJSR    GETPTR         ; open? (else Bad file number)
                JSR     SYNCHR
                .byte   ','
                JSR     GETWDT
                PLA
                FJSR    GETPTR         ; PTR: its FDB's F_MODE
                CLC
                LDA     PTR
                ADC     #F_WID - F_MODE
                STA     PTR
                BCC     :+
                INC     PTR+1
:               LDA     RDL
                SEC
                FJSR    DEVSWD         ; F_WID set
                RTS
; GETWDT (GIO86.ASM's): a width, 1-255, in A and RDL.
GETWDT:         JSR     GETBYT
                LDA     RDL
                BEQ     FCER
                RTS
FCER:           JMP     FCERR
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
