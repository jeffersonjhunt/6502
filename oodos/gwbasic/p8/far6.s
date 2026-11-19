; The screen statements' stubs, in the home block (the project's own; 7d1,
; hw/oodos.md, "The plan, phase 7"). Each far calls its routine in block P6
; (p6/gwsts.s) in screen mode, and is Advanced Feature in console mode, as it
; was (the owner's answer 7). FARCALL keeps A and the flags both ways.
;
;   WIDTH     GIO86.ASM's WIDTHS: WIDTH n[,m], the screen's (GWWID: screen
;             mode only); WIDTH #f,n a file's (FILWID); WIDTH LPRINT n
;             LPT1's; WIDTH "device",n the device's set-width function, by
;             P4's DEVSWD (SCRN: and KYBD: the screen's too, P6's SWIDP, so
;             screen mode only, and refused here in console mode before
;             DEVSWD, whose SCNSWD would set SCRN:'s width: SCNDEV, 7d2's
;             review round 1, F2; CONS: Bad file mode; a disk Bad file name;
;             P4's room: 7d2's answer 7). The file, LPRINT and printer
;             forms work in console mode too (7d2's answer 6). OO-1: the
;             device's name is evaluated twice, once here for its type and
;             once by P4's NAMSCN (a recorded deviation: only a user
;             function with side effects could tell)
;   COLOR CLS SCREEN LOCATE KEY   GWSTS.ASM's (KEY: 7d2)

.include "gwbasic.inc"

.export WIDTH, COLOR, CLS, SCREEN, LOCATE, KEY
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
                JSR     SCNDEV         ; Z: the screen's, in console mode
                PHP
                JSR     SYNCHR
                .byte   ','
                JSR     GETWDT
                PLP
                BNE     :+
                JMP     ADVERR         ; P4 not called: nothing changed
:               CLC
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
KEY:            BIT     SCRMOD
                BPL     ADV
                FJSR    KEYSP
                RTS
ADV:            JMP     ADVERR

; SCNDEV: Z set in console mode if the device NAMSCN has just taken is
; SCRN: or KYBD: (its string's descriptor still at RBL, FRESTR's; its text
; read by PTR2, which NAMSCN's caller doesn't use again). A name NAMSCN
; takes with its ":" fifth, none second, is a device's (a disk's has one
; letter before it; there are no devices of 2 or 3), and of the devices'
; (CONS, LPTn, COMn the others) only SCRN and KYBD end in N and D.
SCNDEV:         LDA     SCRMOD
                BMI     @no            ; screen mode: P6's SWIDP
                LDA     (RBL)
                CMP     #5
                BCC     @no
                LDY     #1
                LDA     (RBL),Y
                STA     PTR2
                INY
                LDA     (RBL),Y
                STA     PTR2+1
                LDY     #1
                LDA     (PTR2),Y
                CMP     #':'
                BEQ     @no            ; a disk's
                LDY     #4
                LDA     (PTR2),Y
                CMP     #':'
                BNE     @no
                DEY
                LDA     (PTR2),Y
                AND     #$DF           ; upper case
                CMP     #'N'
                BEQ     @x
                CMP     #'D'
@x:             RTS
@no:            LDA     #1             ; Z clear
                RTS
