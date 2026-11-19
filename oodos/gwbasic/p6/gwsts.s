; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's screen statements, translated, in block P6 (hw/oodos.md,
; "The plan, phase 7": 7d1, U4), with the OEM's parts they call (CSRATR,
; SWIDTH, SETCLR, SCRSTT: the OO-1's, the project's own). Each is reached in
; screen mode only, from its home block stub (p8/far6.s) or the resident
; evaluator's (res/oem.s); in console mode they stay Advanced Feature (the
; owner's answer 7).
;
;   CLSP      CLS [n]: the active page cleared (CLRSCN), the cursor home
;   LOCATP    LOCATE [y][,[x][,[cursor][,[start][,stop]]]]
;   GWWIDP    WIDTH n[,m] for the screen: 40 or 80 columns, 25 or 30 rows
;             (30: decision 8's OO-1 extension); the pages cleared
;   COLORP    COLOR [fg][,[bg][,border]]: fg 0-31 (16 on: blinking), bg
;             0-15 (its low 3 bits shown), border 0-15
;   SCRNP     SCREEN [mode][,[burst][,[apage][,vpage]]]: mode 0 only (1, 2
;             and the others are 7e's: Illegal function call), the pages
;             0-3 at 80 columns, 0-7 at 40 (the owner's answer 6); vpage
;             apage's if not given
;   SCRNFP    SCREEN(y,x[,z]): the cell's character, or with z not 0 its
;             attribute
;   CSRLNP    CSRLIN: the line the next character goes on (GTLINE)

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry CLSP
;@entry LOCATP
;@entry GWWIDP
;@entry COLORP
;@entry SCRNP
;@entry SCRNFP
;@entry CSRLNP
.export CLSP, LOCATP, GWWIDP, COLORP, SCRNP, SCRNFP, CSRLNP
.import CHRGTR, SYNCHR, GETBYT, FCERR, SNERR, MAKINT, LINLEN

.macro  MOVB7   dst, src
                LDA     src
                STA     dst
.endmacro

.code

; --- SCNINT, EOSCHK (GWSTS.ASM 432) -------------------------------------------------------

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
GTPRMS:         STZ     PCNT
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
@x:             RTS

; --- CLS (GWSTS.ASM 104) ------------------------------------------------------------------

CLSP:           JSR     P6IN
                JSR     SCNINT
                BCS     :+
                CMP     #3             ; CLS 0-2 (the OEM's: 0 all, 1 graphics, 2 text)
                BCC     :+
                JMP     FCERR
:               JSR     EOSCHK
                JSR     CLRSCN
                JMP     P6OUT

; --- LOCATE (GWSTS.ASM 120) ---------------------------------------------------------------

LOCATP:         JSR     P6IN
                JSR     SCNINT         ; the line
                BCC     YLCPRM
                JSR     GTLINE
YLCPRM:         CMP     #0
                BEQ     GOFCER
                STA     LOCY
                SEC                    ; less KEYSW (0, or $FF with the key line: 7d2)
                SBC     KEYSW
                CMP     LINCNT
                BEQ     :+
                BCS     GOFCER
:               JSR     SCNINT         ; the column
                BCC     XLCPRM
                JSR     SCNPOS
                LDA     xDH
XLCPRM:         STA     LOCX
                DEC     A              ; not 0
                CMP     LINLEN
                BCS     GOFCER
                LDX     #0             ; the cursor, its start and stop: checked
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
                BCS     GOFCER
                MOVB7   CSRX, LOCX     ; SCNLOC
                MOVB7   CSRY, LOCY
                JMP     P6OUT          ; (CSRDSP's user cursor, 3: none shown, answer 9)
GOFCER:         JMP     FCERR

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

; --- WIDTH (GWSTS.ASM 190) ----------------------------------------------------------------

GWWIDP:         JSR     P6IN
                JSR     SCNINT
                BCC     :+
                LDA     LINLEN
:               STA     LOCX
                JSR     SCNINT
                BCC     :+
                LDA     LINCNT
:               STA     LOCY
                JSR     EOSCHK
                LDA     LOCX
                CMP     LINLEN
                BNE     :+
                LDA     LOCY
                CMP     LINCNT
                BEQ     @x             ; no change
:               JSR     SWIDTH
                BCS     GOFCER
@x:             JMP     P6OUT

; SWIDTH (the OEM's): LOCX columns (40, 80), LOCY rows (25, 30): the
; driver set for them, every page cleared, page 0 active and shown, home.
SWIDTH:         LDA     LOCX
                CMP     #40
                BEQ     :+
                CMP     #80
                BNE     @bad
:               LDA     LOCY
                CMP     #25
                BEQ     :+
                CMP     #30
                BNE     @bad
:               LDA     LOCX
                LDX     LOCY
                JSR     SCNSWI
                STZ     APAGE
                STZ     VPAGE
                JSR     PGINIT
                JSR     SCNBRK
                JSR     CLRSCN
                JSR     VSHOW
                CLC
                RTS
@bad:           SEC
                RTS

; --- COLOR (GWSTS.ASM 244) ----------------------------------------------------------------

COLORP:         JSR     P6IN
                JSR     GTPRMS
                JSR     SETCLR
                JCS     GOFCER
                JMP     P6OUT

; SETCLR (the OEM's): at most 3: the foreground 0-31, background 0-15,
; border 0-15; one not given kept. The attribute then the PC's: blink in
; bit 7 (16-31), the background's 3 bits in 6-4, the foreground in 3-0.
SETCLR:         LDA     PCNT
                CMP     #4
                BCS     @bad
                LDX     FGC
                LDA     PFLG
                BEQ     :+
                LDX     PVAL
                CPX     #32
                BCS     @bad
:               STX     NFG
                LDX     BGC
                LDA     PFLG + 1
                BEQ     :+
                LDX     PVAL + 1
                CPX     #16
                BCS     @bad
:               STX     NBG
                LDX     BRDC
                LDA     PFLG + 2
                BEQ     :+
                LDX     PVAL + 2
                CPX     #16
                BCS     @bad
:               CPX     BRDC
                STX     BRDC
                PHP
                MOVB7   FGC, NFG
                MOVB7   BGC, NBG
                AND     #$07
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     ATTR
                LDA     FGC
                AND     #$0F
                ORA     ATTR
                STA     ATTR
                LDA     FGC
                AND     #$10
                BEQ     :+
                LDA     ATTR
                ORA     #$80
                STA     ATTR
:               PLP
                BEQ     :+
                JSR     VSHOW          ; the border changed
:               CLC
                RTS
@bad:           SEC
                RTS

; --- SCREEN (GWSTS.ASM 350) ---------------------------------------------------------------

SCRNP:          JSR     P6IN
                JSR     GTPRMS
                JSR     SCRSTT
                BCS     GOFCER2
                JMP     P6OUT
GOFCER2:        JMP     FCERR

; SCRSTT (the OEM's): SCREEN 0 only (the other modes are 7e's); the burst
; taken and passed over; the active page and the visual one.
SCRSTT:         LDA     PCNT
                CMP     #5
                BCS     @bad
                LDA     PFLG
                BEQ     :+
                LDA     PVAL
                BNE     @bad           ; SCREEN 1, 2 ...: 7e's
:               LDX     APAGE
                LDA     PFLG + 2
                BEQ     :+
                LDX     PVAL + 2
                CPX     NPAGES
                BCS     @bad
:               STX     NAP
                LDA     PFLG + 3
                BEQ     :+
                LDX     PVAL + 3
                CPX     NPAGES
                BCS     @bad
:               STX     NVP            ; (not given: apage's)
                LDA     NAP
                CMP     APAGE
                BEQ     :+
                STA     APAGE
                JSR     APGSET         ; the cache written back and forgotten
:               LDA     NVP
                CMP     VPAGE
                BEQ     :+
                STA     VPAGE
                JSR     VSHOW
:               CLC
                RTS
@bad:           SEC
                RTS

; --- SCREEN(, CSRLIN (GWSTS.ASM 258) ------------------------------------------------------

; SCRNFP: TXTPTR at SCREEN's token in a formula: SCREEN(y,x[,z]), the FAC
; its value.
SCRNFP:         JSR     P6IN
                JSR     CHRGTR
                JSR     SYNCHR
                .byte   '('
                JSR     SCNINT         ; y, 1-LINCNT
                BCS     FCERGO
                STA     LOCY
                DEC     A
                CMP     LINCNT
                BCS     FCERGO
                JSR     SCNINT         ; x, 1-LINLEN
                BCS     FCERGO
                STA     LOCX
                DEC     A
                CMP     LINLEN
                BCS     FCERGO
                STZ     LOCP
                LDA     PTERM
                CMP     #')'
                BEQ     SCRCHR
                JSR     SCNINT         ; z
                BCS     FCERGO
                STA     LOCP
                LDA     PTERM
                CMP     #')'
                BNE     FCERGO
SCRCHR:         MOVB7   xDL, LOCY
                MOVB7   xDH, LOCX
                LDA     LOCP
                BEQ     :+
                JSR     SCRATR
                BRA     SCRENX
:               JSR     SCRINP
SCRENX:         STA     RBL
                STZ     RBH
                JSR     MAKINT
                DECW    TXTPTR
                JSR     CHRGTR
                PHP
                JSR     P6OUT
                PLP
                RTS
FCERGO:         JMP     FCERR

; CSRLNP: TXTPTR at CSRLIN's token: the FAC CSRLIN.
CSRLNP:         JSR     P6IN
                JSR     GTLINE
                STA     RBL
                STZ     RBH
                JSR     MAKINT
                JSR     CHRGTR
                PHP
                JSR     P6OUT
                PLP
                RTS

; GTLINE (GWSTS.ASM 273): A the line the next character goes on: CSRY, or
; the next if the cursor is past the line's end and that won't scroll.
GTLINE:         LDA     CSRY
                LDX     CSRX
                CPX     LINLEN
                BCC     @x
                BEQ     @x
                LDX     LINCNT
                DEX
                STX     LOCP
                CMP     LOCP
                BCS     @x
                INC     A
@x:             RTS


.segment "BRAM"
PMAX            = 8
PCNT:           .res    1              ; GTPRMS's list
PFLG:           .res    PMAX
PVAL:           .res    PMAX
PTERM:          .res    1              ; SCNINT's terminator
SNEND:          .res    1              ; and whether the statement ended
LOCX:           .res    1
LOCY:           .res    1
LOCP:           .res    3              ; LOCATE's cursor, start, stop (and SCREEN('s z)
LOCG:           .res    3              ;   each: 1 not given
NFG:            .res    1
NBG:            .res    1
NAP:            .res    1
NVP:            .res    1
