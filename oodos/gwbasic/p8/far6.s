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
;
; 7e5 (U0, the space plan's M6; the owner's answers to it of 2026-11-19 and
; to the 7e5 plan, 2026-11-20): each stub here parses its statement's
; arguments, GWSTS.ASM's SCNINT, EOSCHK and GTPRMS translated (moved from P6,
; their code unchanged), with the checks that need nothing of P6's (CLS's n,
; LOCATE's 0, its column against LINLEN, its cursor, start and stop: the
; OEM's CSRATR), then makes the one far call it made before, the values in
; the pseudo-registers, to the same entry of P6's, which checks what needs
; its state (LOCATE's line against LINCNT and the key line, WIDTH's rows,
; the mode's colours) and acts. KEY n,"text" is all here (STRTAB is
; resident), the line then drawn again by P6's KEYDP. In console mode every
; stub is Advanced Feature first, nothing parsed.

.include "gwbasic.inc"

.export WIDTH, COLOR, CLS, SCREEN, LOCATE, KEY
.import ADVERR, SCRMOD, CHRGTR, GETBYT, SYNCHR, FRMEVL, FREFAC, FCERR, LINLEN
.import SNERR, FRESTR, STRTAB

.code

WIDTH:          CMP     #'#'
                BNE     :+
                JMP     WIDFIL
:                CMP     #','
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
                BMI     :+
                JMP     ADVERR
:               JSR     SCNINT         ; 7e5: the columns (LINLEN if none)
                BCC     :+
                LDA     LINLEN
:               STA     LCX
                JSR     SCNINT         ;   the rows (P6's LINCNT if none)
                STA     LCY
                ROR     LCYN
                JSR     EOSCHK
                LDA     LCX
                STA     RDL
                LDA     LCY
                STA     RBL
                LDA     LCYN
                STA     RBH
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
                JSR     GTPRMS         ; 7e5: P6's SETCLR or P5's GCOLOR checks them
                FJSR    COLORP
                RTS
CLS:            BIT     SCRMOD
                BPL     ADV
                JSR     SCNINT         ; 7e5: n, 0-2 (0 if none)
                BCS     :+
                CMP     #3             ; CLS 0-2 (the OEM's: 0 all, 1 graphics, 2 text)
                BCS     FCER
:               PHA
                JSR     EOSCHK
                PLA
                FJSR    CLSP           ; (A n)
                RTS
SCREEN:         BIT     SCRMOD
                BPL     ADV
                JSR     GTPRMS         ; 7e5: P6's SCRSTT checks them
                FJSR    SCRNP
                RTS
KEY:            BIT     SCRMOD
                BPL     ADV
                CMP     #'('           ; 7f: KEY(n) ON, OFF, STOP
                BEQ     KEYSTT
                CMP     #ON_TK         ; ON, OFF, LIST: P6's
                BEQ     @p6
                CMP     #OFF_TK
                BEQ     @p6
                CMP     #LIST_TK
                BEQ     @p6
                JMP     KEYDEF
@p6:            FJSR    KEYSP
                RTS
ADV:            JMP     ADVERR

; KEYSTT (GWSTS.ASM 1310, 7f): KEY(n) ON, OFF or STOP, n 1-20 (the manual's
; p. 147; OO-1: KEY(0), which SEVSTT would take as every key, Illegal
; function call too, the manual's), parsed here (the space plan's answer 1)
; in SEVSTT's order: "(", n, ")", the token, the statement's end, then n's
; range; then P3's SETEV1 on event KEYOFF + n - 1.
KEYSTT:         JSR     SYNCHR
                .byte   '('
                JSR     GETBYT         ; n
                PHA
                JSR     SYNCHR
                .byte   ')'
                PHA                    ; ON, OFF or STOP
                JSR     CHRGTR         ; the statement must end
                BEQ     :+
                JMP     SNERR
:               PLA
                PLX
                DEX                    ; 0-19
                CPX     #NMKEYT
                BCS     LFC
                PHA
                TXA
                CLC
                ADC     #KEYOFF
                TAX
                PLA
                FJSR    SETEV1
                RTS

; --- LOCATE, KEY n,"text" (GWSTS.ASM 120, 1332; 7e5: M6) --------------------------------

; LOCATE [y][,[x][,[cursor][,[start][,stop]]]]: parsed here, the line not 0
; and the column on the line (LINLEN), the cursor, start and stop checked
; (CSRATR); then P6's LOCATP: RDL the line, RDH bit 7 not given (the
; cursor's then), RBL the column, RBH the same, P6 checking the line against
; its rows and the key line. The source's order kept (the owner's answer 13
; to the 7e5 build's report): a line given with more after it is checked
; against the last row as soon as it is parsed, by P6's LOCLIM (one more
; far call then), so LOCATE 99,"A" is Illegal function call, not Type
; mismatch; a line alone is checked by LOCATP, nothing after it to fail.
LOCATE:         BIT     SCRMOD
                BPL     ADV
                JSR     SCNINT         ; the line
                STA     LCY
                PHP                    ; (review round 1, F1: its own C, not ROR's)
                ROR     LCYN
                PLP
                BCS     :+
                TAX
                BEQ     LFC            ; 0
                LDA     SNEND          ; more after it: its row checked now
                BNE     :+
                FJSR    LOCLIM         ; A: the last row (LINCNT, less 1 with the key line)
                CMP     LCY
                BCC     LFC
:               JSR     SCNINT         ; the column
                STA     LCX
                PHP
                ROR     LCXN
                PLP
                BCS     :+
                DEC     A              ; not 0
                CMP     LINLEN
                BCS     LFC
:               LDX     #0             ; the cursor, its start and stop: checked
@c:             PHX
                JSR     SCNINT
                PLX
                STA     LOCP,X
                LDA     #0
                ROL     A              ; 1: not given
                STA     LOCG,X
                INX
                CPX     #3
                BCC     @c
                JSR     EOSCHK
                JSR     CSRATR
                BCS     LFC
                LDA     LCY
                STA     RDL
                LDA     LCYN
                STA     RDH
                LDA     LCX
                STA     RBL
                LDA     LCXN
                STA     RBH
                FJSR    LOCATP
                RTS

LFC:            JMP     FCERR

; CSRATR (the OEM's): LOCATE's cursor (0 or 1), start and stop (0-31),
; checked; not kept (the owner's answer 9). C set: out of range.
CSRATR:         LDA     LOCG
                BNE     :+
                LDA     LOCP
                CMP     #2
                BCS     @x
:               LDX     #1
@s:             LDA     LOCG,X
                BNE     :+
                LDA     LOCP,X
                CMP     #32
                BCS     @x
:               INX
                CPX     #3
                BCC     @s
                CLC
@x:             RTS

; (NMKEYF, the soft keys: gwbasic.inc's)

; KEYDEF: KEY n,"text": key n (1-10), its text's first 15 characters (a 0
; after) in STRTAB (resident); then P6's KEYDP draws the line again if it
; is on.
KEYDEF:         JSR     GETBYT         ; the key's number
                CMP     #NMKEYF + 5    ; 7f: KEY 15-20
                BCC     :+
                CMP     #NMKEYF + 5 + NMKEYD
                BCS     :+
                JMP     KEYD15
:               DEC     A
                CMP     #NMKEYF
                BCS     LFC            ; 1 to 10, else Illegal function call
                ASL     A              ; x 16: its place in STRTAB
                ASL     A
                ASL     A
                ASL     A
                PHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                JSR     FRESTR         ; BX: the descriptor
                PLX
                LDA     (RBL)
                CMP     #16
                BCC     :+
                LDA     #15            ; 0 to 15 characters
:               STA     LCX
                LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDY     #0
@cp:            CPY     LCX
                BEQ     @end
                LDA     (PTR),Y
                STA     STRTAB,X
                INX
                INY
                BRA     @cp
@end:           STZ     STRTAB,X       ; the 0 after it
                FJSR    KEYDP
                RTS

; KEYD15 (7f; the manual's pp. 144-147): KEY n,CHR$(KBflag)+CHR$(scan
; code), n 15-20, the string exactly 2 bytes (else Illegal function call).
; The scan code (the PC's, 1-83) to the USB key's HID usage (SCNHID); 0, the
; predefined keys' codes (59-68, 72, 75, 77, 80: "has no effect", p. 145)
; and past 83, never matched. KBflag kept as Caps Lock (&H40), Alt (&H08),
; Ctrl (&H04) and the Shifts as one (&H01, &H02 and &H03 each "a Shift"),
; in P6's order (KTSET's); OO-1 (the owner's answer 15): Num Lock's &H20 dropped (the OO-1's
; keypad is always digits: no Num Lock to latch); OO-1: Ctrl and C never
; matched (^C is the break, before any trap). Then P6's KTSET.
KEYD15:         SEC
                SBC     #NMKEYF + 5    ; 0-5
                PHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                JSR     FRESTR         ; BX: the descriptor
                LDA     (RBL)
                CMP     #2
                BNE     KD15FC
                LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDY     #1
                LDA     (PTR),Y        ; the scan code
                LDX     #0
                CMP     #SCNHN
                BCS     :+
                TAX
                LDA     SCNHID,X
                TAX
:               LDA     (PTR)          ; KBflag, in P6's order (EV_MODS's left
                STA     PTR            ; keys: bit 0 Ctrl, 1 Shift, 2 Alt), Caps
                AND     #$40           ; Lock bit 6
                LSR     PTR            ; &H01, &H02: a Shift
                BCC     :+
                ORA     #$02
:               LSR     PTR
                BCC     :+
                ORA     #$02
:               LSR     PTR            ; &H04: Ctrl
                BCC     :+
                ORA     #$01
:               LSR     PTR            ; &H08: Alt
                BCC     :+
                ORA     #$04
:               CPX     #$06           ; Ctrl-C: never
                BNE     :+
                BIT     #$01
                BEQ     :+
                LDX     #0
:               PHX                    ; Y: the usage, X: the state
                TAX
                PLY
                PLA                    ; A: n - 15
                SEC                    ; (C set: a definition)
                FJSR    KTSET
                RTS
KD15FC:         JMP     FCERR

; --- SCNINT, EOSCHK, GTPRMS (GWSTS.ASM 432, 359; 7e5: moved from P6) -------------------

; SCNINT: an optional byte parameter. C clear: A its value, PTERM what ended
; it (a comma or ")" passed over); C set: none (A 0). SNEND nonzero when the
; statement ended there (the 8086's ZF).
SCNINT:         DECW    TXTPTR
                JSR     CHRGTR
                BEQ     @nomore
                CMP     #','
                BEQ     @omit
                JSR     GETBYT
                PHA
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     @trmok
                CMP     #','
                BEQ     @trmcom
                CMP     #')'
                BEQ     @trmcom
                PLA
                JMP     FCERR
@trmcom:        STA     PTERM
                INCW    TXTPTR
                STZ     SNEND
                PLA
                CLC
                RTS
@trmok:         STA     PTERM
                LDA     #1
                STA     SNEND
                PLA
                CLC
                RTS
@omit:          INCW    TXTPTR
                STA     PTERM
                STZ     SNEND
                LDA     #0
                SEC
                RTS
@nomore:        STA     PTERM
                LDA     #1
                STA     SNEND
                LDA     #0
                SEC
                RTS

; EOSCHK: the statement must end here, else Syntax error.
EOSCHK:         DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               RTS

; GTPRMS (GWSTS.ASM 359): byte parameters to the statement's end, each one
; there or not: PCNT of them, PFLG,X nonzero for one given, PVAL,X its value.
; OO-1: every PFLG cleared first, so one past PCNT reads "not given" (the
; 8086's are on its stack, counted; SCRSTT reads the pages' flags whatever
; the count: a COLOR's third parameter was taken for SCREEN's apage). 7e5:
; then the list in the pseudo-registers for P6's PREGS: RDL PCNT, RDH a bit
; for each given (bit 0 the first), RBL on the values.
GTPRMS:         LDX     #PMAX - 1
:               STZ     PFLG,X
                DEX
                BPL     :-
                STZ     PCNT
@l:             JSR     SCNINT
                LDX     PCNT
                BCS     @null
                STA     PVAL,X
                LDA     #$FF
                STA     PFLG,X
                INC     PCNT
                LDA     SNEND
                BNE     @x
                LDA     PCNT
                CMP     #PMAX
                BCC     @l
                JMP     FCERR
@null:          LDA     SNEND          ; nothing at the end: not counted
                BNE     @x
                STZ     PFLG,X
                STZ     PVAL,X
                INC     PCNT
                LDA     PCNT
                CMP     #PMAX
                BCC     @l
                JMP     FCERR
@x:             LDX     #PMAX - 1      ; 7e5: the list to the pseudo-registers
@m:             LDA     PFLG,X
                CMP     #1
                ROL     RDH
                LDA     PVAL,X
                STA     RBL,X
                DEX
                BPL     @m
                LDA     PCNT
                STA     RDL
                RTS

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

.segment "BRAM"
PMAX            = 8
PCNT:           .res    1              ; GTPRMS's list
PFLG:           .res    PMAX
PVAL:           .res    PMAX
PTERM:          .res    1              ; SCNINT's terminator
SNEND:          .res    1              ; and whether the statement ended
LCY:            .res    1              ; LOCATE's line, WIDTH's rows
LCYN:           .res    1              ;   bit 7: not given
LCX:            .res    1              ; LOCATE's column, WIDTH's columns, KEY's length
LCXN:           .res    1              ;   bit 7: not given
LOCP:           .res    3              ; LOCATE's cursor, start, stop
LOCG:           .res    3              ;   each: 1 not given
.assert RBL + PMAX <= PTR, error, "GTPRMS's values in the pseudo-registers"

.rodata
; SCNHID (7f): the PC's scan codes 0-83 (the IBM manual's appendix E) as the
; USB keys' HID usages, 0 none or a predefined key's (KEY 15-20's, KEYD15).
SCNHID:
                .byte   $00, $29, $1E, $1F, $20, $21, $22, $23, $24, $25, $26, $27   ; 0-11
                .byte   $2D, $2E, $2A, $2B, $14, $1A, $08, $15, $17, $1C, $18, $0C   ; 12-23
                .byte   $12, $13, $2F, $30, $28, $E0, $04, $16, $07, $09, $0A, $0B   ; 24-35
                .byte   $0D, $0E, $0F, $33, $34, $35, $E1, $31, $1D, $1B, $06, $19   ; 36-47
                .byte   $05, $11, $10, $36, $37, $38, $E5, $55, $E2, $2C, $39, $00   ; 48-59
                .byte   $00, $00, $00, $00, $00, $00, $00, $00, $00, $53, $47, $4A   ; 60-71
                .byte   $00, $4B, $56, $00, $5D, $00, $57, $4D, $00, $4E, $49, $4C   ; 72-83
SCNHN           = * - SCNHID
