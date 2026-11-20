; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; ADVGRP.ASM's CIRCLE, its parsing, translated, in block P3 (hw/oodos.md,
; "The plan, phase 7": 7e2, U2; the owner's answer 1 and option 1 of
; 2026-11-19: a statement's parsing in P3, its drawing in P5):
;
;   CIRCA     from P5's CIRCP (p5/advgrp.s), by one far call a CIRCLE,
;             TXTPTR past the centre (P5's SCAN1, the source's, has made it
;             the graphics cursor): the rest of CIRCLE [STEP](x,y),r[,[c][,
;             [start][,[end][,aspect]]]], the radius (GETIN2), the colour
;             (ATRSCN's parse; 7e4: under a WINDOW scaled, P1's WINRAD), the
;             start and end (CGTCNT, CMPONE) and the
;             aspect, with CNPNTS (the radius x SQR(2)/2), all the floating
;             point of the statement
;
; In: RAL the mode's foreground (the default colour), RAH PIXSIZ (0 in
; SCREEN 0: Illegal function call after the colour, as ATRSCN's ATRFI2),
; RBL/RBH and RSI GTASPC's pair (the aspect x 256 and its inverse). Out, for
; P5 (P3 can't reach P5's RAM): RDL/RDH CSTCNT, RBL/RBH CENCNT, RAL CLINEF,
; RAH CPLOTF, RCL/RCH CNPNTS, RSI ASPECT, RDI the radius, T1 CSCLXY.
; Every error is raised here, before P5 draws anything. The attribute is
; P5's: the colour goes to it by a far call to P5's CIRCC as soon as it is
; parsed (ATRSCN's place: review round 1, F1). So an error anywhere after
; the centre leaves the cursor at the centre (the owner's U2b ruling, and
; its revision after F1) and one in the angles or the aspect the attribute
; the colour, as the source's do.
;
; And GPUTA (7e3): from P5's GPUTG, by one far call, GET's and PUT's array
; (GTARRY), GET's size check and header, PUT's action word (GFUNTB), header
; and far corner (below).

.include "gwbasic.inc"

.export CIRCA, GPUTA
.import CHRGTR, CHRGT2, SYNCHR, GETIN2, GETBYT, FCERR, FRMEVL, FRCSNG, FRCINT, FMULS
.import PTRGET, SUBFLG, UMULT, TMERR, SNERR
.import FCOMP, FDIVS, MAKINT, PUSHF, POPR, GFLG

.macro  PSHW    zp
                LDA     zp + 1
                PHA
                LDA     zp
                PHA
.endmacro
.macro  PULW    zp
                PLA
                STA     zp
                PLA
                STA     zp + 1
.endmacro
.macro  MOVW    dst, src
                LDA     src
                STA     dst
                LDA     src + 1
                STA     dst + 1
.endmacro

.code

; --- CIRCLE (ADVGRP.ASM 460) ------------------------------------------------------------------

CIRCA:          LDA     RAL
                STA     CFG
                LDA     RAH
                STA     CPIX
                MOVW    CASP, RBL
                MOVW    CINV, RSI
                JSR     SYNCHR         ; (the centre P5's: its SCAN1)
                .byte   ','
                BIT     GFLG           ; 7e4: under a WINDOW the radius a single,
                BPL     @g             ; scaled by its x scale (P1's WINRAD: the
                JSR     FRMEVL         ; IBM manual's p. 373)
                JSR     FRCSNG
                FJSR    WINRAD
                BRA     @r
@g:             JSR     GETIN2         ; the radius
@r:             MOVW    CRAD, RDL
                MOVW    RBL, RDL       ; MKINTC
                JSR     MAKINT
                JSR     FRCSNG
                LDA     #$80           ; SQR(2)/2 (MOVRI 200O,65O,4O,363O)
                STA     RBH
                LDA     #$35
                STA     RBL
                LDA     #$04
                STA     RDH
                LDA     #$F3
                STA     RDL
                JSR     FMULS
                JSR     FRCINT
                MOVW    CNPNTS, RBL    ; the points an octant
                STZ     CLINEF         ; no lines to the centre
                STZ     CSCLXY         ; scaling y
                JSR     CATRS          ; the colour
                LDA     #1             ; CLINEF's bit 0: a line from the start
                STA     CGF
                STZ     RDL            ; the start's default: 0
                STZ     RDH
                JSR     CGTCNT
                PSHW    RDL
                LDA     #128           ; bit 7: a line from the end
                STA     CGF
                LDA     #$FF           ; the end's default: "infinity"
                STA     RDL
                STA     RDH
                JSR     CGTCNT
                MOVW    RBL, RDL       ; BX the end
                PULW    RDL            ; DX the start
                LDA     RBL            ; end >= start: plot between them
                CMP     RDL
                LDA     RBH
                SBC     RDH
                LDA     #0
                BCS     CSTPLT
                LDX     RBL            ; else above and below, start and
                LDY     RDL            ; end swapped (start < end)
                STX     RDL
                STY     RBL
                LDX     RBH
                LDY     RDH
                STX     RDH
                STY     RBH
                LDA     CLINEF         ; the centre lines' flags swapped
                ASL     A              ; (ROL AL twice, OR, ROR: 8-bit
                ADC     #0             ; rotates)
                ASL     A
                ADC     #0
                ORA     CLINEF
                LSR     A
                BCC     :+
                ORA     #$80
:               STA     CLINEF
                LDA     #$FF
CSTPLT:         STA     CPLOTF         ; the plot polarity
                MOVW    CSTCNT, RDL
                MOVW    CENCNT, RBL
                DECW    TXTPTR         ; an aspect?
                JSR     CHRGTR
                BNE     CIRC1
                MOVW    RBL, CASP      ; GTASPC's: past 1 (BH not 0) its inverse,
                LDA     CASP + 1       ; scaling x
                BEQ     CIRC2
                LDA     #1
                STA     CSCLXY
                MOVW    RBL, CINV
                BRA     CIRC2
CIRC1:          JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                JSR     FRCSNG
                JSR     CMPONE         ; over 1?
                BNE     CIRC11         ; no: scaling y
                INC     A
                STA     CSCLXY         ; scaling x, by 1/the aspect
                JSR     CONE
                JSR     FDIVS
CIRC11:         CLC                    ; x 256: 8 on the exponent
                LDA     FACEXP
                ADC     #8
                STA     FACEXP
                JSR     FRCINT
CIRC2:          ; (BX the aspect, a fraction of 256)
                MOVW    RSI, RBL       ; out, for P5
                MOVW    RDL, CSTCNT
                MOVW    RBL, CENCNT
                LDA     CLINEF
                STA     RAL
                LDA     CPLOTF
                STA     RAH
                MOVW    RCL, CNPNTS
                MOVW    RDI, CRAD
                LDA     CSCLXY
                STA     T1
                RTS

; CATRS: ATRSCN's parse (GENGRP.ASM 226), the colour to CATR: the
; foreground if the statement ends or it is null, else GETBYT's; then
; PIXSIZ's test (ATRFI2). P5's SETATR takes it after (every attribute is
; legal: SETATR is the OEM's, its carry always clear).
CATRS:          LDA     CFG
                STA     RDL
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     @fin
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @fin           ; another comma: null, the default
                JSR     GETBYT
@fin:           LDA     RDL
                STA     CATR
                FJSR    CIRCC          ; to P5's CIRCC: the colour the attribute
                                       ; (SETATR, ATRBYT) as soon as it is parsed,
                                       ; the source's ATRSCN (review round 1, F1)
                LDA     CPIX
                BNE     :+
                JMP     FCERR          ; no graphics (SCREEN 0)
:               JMP     CHRGT2

; CGTCNT (ADVGRP.ASM 782): an angle, DX its point count (the default DX if
; there is none): a negative one made positive with CGF's bit set in
; CLINEF (a line to the centre); over 2 x PI Illegal function call; the
; count angle / (2 x PI) x CNPNTS x 8, rounded (FRCINT).
CGTCNT:         DECW    TXTPTR
                JSR     CHRGTR
                BEQ     CGTRET             ; nothing: the default
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     CGTRET             ; no argument: the default
                LDA     CGF
                PHA
                JSR     FRMEVL
                JSR     FRCSNG
                PLA
                STA     CGF
                LDA     FACEXP         ; negative?
                BEQ     CGTC2
                LDA     FACM1
                BPL     CGTC2
                AND     #$7F           ; made positive
                STA     FACM1
                LDA     CLINEF
                ORA     CGF
                STA     CLINEF
CGTC2:          LDA     #$83           ; 1/(2 x PI) (MOVRI 176O,42O,371O,203O)
                STA     RDL
                LDA     #$F9
                STA     RDH
                LDA     #$22
                STA     RBL
                LDA     #$7E
                STA     RBH
                JSR     FMULS
                JSR     CMPONE         ; over one: Illegal function call
                BNE     :+
                JMP     FCERR
:               JSR     PUSHF
                MOVW    RBL, CNPNTS    ; x 8, the circumference
                LDX     #3
@x8:            ASL     RBL
                ROL     RBH
                DEX
                BNE     @x8
                JSR     MAKINT
                JSR     FRCSNG
                JSR     POPR
                JSR     FMULS
                JSR     FRCINT
                MOVW    RDL, RBL
CGTRET:             RTS

; CMPONE (ADVGRP.ASM 838): the FAC against 1: A FCOMP's less one (0, Z,
; when the FAC is over 1). CONE: BXDX 1.
CMPONE:         JSR     CONE
                JSR     FCOMP
                DEC     A
                RTS
CONE:           LDA     #$81
                STA     RBH
                STZ     RBL
                STZ     RDH
                STZ     RDL
                RTS

; --- GET and PUT's array (ADVGRP.ASM 845, 950, 1060; 7e3) -------------------------------

; GPUTA (7e3): from P5's GPUTG (p5/advgrp.s) by one far call (the owner's
; answer 1), TXTPTR past the corners (P5's SCAN1 and SCAND: the cursor is
; P5's). In: RAL 0 GET, 1 PUT; RAH PIXSIZ; RCL/RCH, RDL/RDH the corner; for
; GET RBL/RBH the pixels a row (MAXDEL) and RSI the rows (MINDEL). GTARRY's
; array (numeric: the owner's answer 6), then for GET its size (MULBLP: the
; bits, the pixels x PIXSIZ; their bytes, (bits + 7) / 8, x the rows by
; UMULT, + 4) against the array's, Illegal function call with nothing
; written, then the header (element 0 the bits, 1 the rows); for PUT (PUT1's
; part) the action word (GFUNTB; none XOR; any other returns with carry set,
; nothing drawn, for NEWSTT's Syntax error), the header read, and the far
; corner (x + the bits over PIXSIZ - 1, y + the rows - 1: a carry Illegal
; function call; P5's CHKRNG then checks it). Out, carry clear: RSI the
; data's address past the header, RBL/RBH the bits a row, RDI the rows, RAL
; the action (0-4, GFUNTB's order); PUT's far corner in RCL/RCH, RDL/RDH.
; Nothing here moves W1, so the array is read and written wherever it is.
GPUTA:          LDA     RAL
                STA     GAPUT
                LDA     RAH
                STA     GAPIX
                MOVW    GAX, RCL
                MOVW    GAY, RDL
                MOVW    GABX, RBL
                MOVW    GARW, RSI
                LDA     #3             ; (GET: no action)
                STA     GAACT
                JSR     GTARRY
                LDA     GAPUT
                BEQ     :+
                JMP     PUT1
:               MOVW    RBL, GABX      ; MULBLP: the bits, MAXDEL x PIXSIZ
                LDA     GAPIX
                CMP     #2
                BNE     :+
                ASL     RBL
                ROL     RBH
:               MOVW    GABT, RBL
                CLC                    ; their bytes, (bits + 7) / 8
                LDA     RBL
                ADC     #7
                STA     RDL
                LDA     RBH
                ADC     #0
                STA     RDH
                LDX     #3
@h:             LSR     RDH            ; HLFDE
                ROR     RDL
                DEX
                BNE     @h
                MOVW    RCL, GARW      ; x MINDEL (UMULT)
                JSR     UMULT
                CLC                    ; + 4, + the data's start: the end it needs
                LDA     RDL
                ADC     #4
                STA     RDL
                LDA     RDH
                ADC     #0
                STA     RDH
                CLC
                LDA     RDL
                ADC     GAD
                STA     RDL
                LDA     RDH
                ADC     GAD + 1
                STA     RDH
                BCS     GAFC           ; past 64 KB
                LDA     GAE            ; the array's end below it: too small
                CMP     RDL
                LDA     GAE + 1
                SBC     RDH
                BCC     GAFC
                MOVW    PTR, GAD       ; the header: the bits, the rows
                LDY     #3
@w:             LDA     GABT,Y         ; (GABT then GARW)
                STA     (PTR),Y
                DEY
                BPL     @w
GAOUT:          CLC                    ; out: past the header
                LDA     GAD
                ADC     #4
                STA     RSI
                LDA     GAD + 1
                ADC     #0
                STA     RSI + 1
                MOVW    RBL, GABT
                MOVW    RDI, GARW
                LDA     GAACT
                STA     RAL
                CLC
                RTS
GAFC:           JMP     FCERR

; PUT1 (ADVGRP.ASM 950): the action word, PFUNLP's search of GFUNTB.
PUT1:           DECW    TXTPTR
                JSR     CHRGTR
                BNE     :+
                LDX     #4             ; none: XOR
                BRA     PUT2
:               JSR     SYNCHR
                .byte   ','
                BNE     :+             ; OO-1: a comma and no word, Syntax error (the IBM
                JMP     SNERR          ; manual's form, p. 281; the source's NEWSTT finds the end)
:               LDX     #4
PFUNLP:         CMP     GFUNTB,X
                BEQ     PUT20
                DEX
                BPL     PFUNLP
                SEC                    ; none of them: for NEWSTT's Syntax error
                RTS
PUT20:          PHX
                JSR     CHRGTR         ; past the word
                PLX
PUT2:           STX     GAACT
                MOVW    PTR, GAD       ; the header
                LDY     #3
@r:             LDA     (PTR),Y
                STA     GABT,Y
                DEY
                BPL     @r
                MOVW    RCL, GABT      ; the pixels: the bits over PIXSIZ (DIVBLP)
                LDA     GAPIX
                CMP     #2
                BNE     :+
                LSR     RCH
                ROR     RCL
:               DECW    RCL            ; a count
                CLC                    ; + x: the far x
                LDA     RCL
                ADC     GAX
                STA     RCL
                LDA     RCH
                ADC     GAX + 1
                STA     RCH
                BCS     PFC            ; PRNGER
                MOVW    RDL, GARW      ; the far y: y + the rows - 1
                DECW    RDL
                CLC
                LDA     RDL
                ADC     GAY
                STA     RDL
                LDA     RDH
                ADC     GAY + 1
                STA     RDH
                BCS     PFC
                JMP     GAOUT
PFC:            JMP     FCERR

; GTARRY (ADVGRP.ASM 1060): the comma, then the array named (PTRGET with
; SUBFLG 1: arrays only, no subscripts, as ERASE's; not there, Illegal
; function call: no array is made). OO-1 (the owner's answer 6, the IBM
; manual's pp. 116, 281: the array must be numeric): a string array is Type
; mismatch, before anything is written. GAD its data's first byte (past the
; dimensions), GAE the byte past its end (the size before the dimensions'
; count, from it).
GTARRY:         JSR     SYNCHR
                .byte   ','
                LDA     #1
                STA     SUBFLG
                JSR     PTRGET
                BNE     PFC            ; not there
                STZ     SUBFLG
                LDA     VALTYP
                CMP     #VT_STR
                BNE     :+
                JMP     TMERR
:               SEC                    ; the size, the word before BX
                LDA     RBL
                SBC     #2
                STA     PTR
                LDA     RBH
                SBC     #0
                STA     PTR + 1
                CLC
                LDA     (PTR)
                ADC     RBL
                STA     GAE
                LDY     #1
                LDA     (PTR),Y
                ADC     RBH
                STA     GAE + 1
                LDA     (RBL)          ; the data: BX + 1 + 2 x the dimensions (the
                ASL     A              ; 8086's byte add)
                SEC
                ADC     RBL
                STA     GAD
                LDA     RBH
                ADC     #0
                STA     GAD + 1
                RTS

.segment "RODATA"
GFUNTB:         .byte   OR_TK, AND_TK, PRESET_TK, PSET_TK, XOR_TK ; PUT's actions, 0-4

.segment "BRAM"
CFG:            .res    1              ; the mode's foreground (in)
CPIX:           .res    1              ; PIXSIZ (in)
CASP:           .res    2              ; GTASPC's aspect (in)
CINV:           .res    2              ;   and its inverse
CRAD:           .res    2              ; the radius (the source's GXPOS)
CATR:           .res    1              ; the colour
CNPNTS:         .res    2              ; the points an octant
CSTCNT:         .res    2              ; the start's count
CENCNT:         .res    2              ; the end's
CLINEF:         .res    1              ; bit 0: a line from the start, bit 7 from the end
CPLOTF:         .res    1              ; 0: plot between start and end; $FF: outside
CSCLXY:         .res    1              ; 1: scaling x (an aspect over 1)
CGF:            .res    1              ; CGTCNT's flag for CLINEF
GAPUT:          .res    1              ; 7e3: GET 0, PUT 1 (in)
GAPIX:          .res    1              ;   PIXSIZ (in)
GAX:            .res    2              ;   the corner (in)
GAY:            .res    2
GABX:           .res    2              ;   GET's pixels a row (in)
.assert GARW = GABT + 2, error, "GPUTA's header: the bits, then the rows"
GABT:           .res    2              ;   the bits a row
GARW:           .res    2              ;   the rows
GAD:            .res    2              ;   the array's data
GAE:            .res    2              ;   the byte past its end
GAACT:          .res    1              ;   PUT's action
