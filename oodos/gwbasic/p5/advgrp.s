; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; ADVGRP.ASM's drawing, translated, in block P5 (hw/oodos.md, "The plan,
; phase 7": 7e2; the owner's answer 1 and option 1 of 2026-11-19: the
; parsing in P3, the drawing here, where the aperture is mapped):
;
;   CIRCP     CIRCLE: the centre by SCAN1 (the cursor at once, the source's),
;             the rest of its parse by one far call to P3's CIRCA (p3/advgrp.s),
;             then the circle's loop (CIRCLP) and its plotting (CPLOT8,
;             CPLOT4, CPLOT), with SCALE, GTABSC and CLINE2
;   GPUTG     7e3: GET and PUT of graphics, the corners here, the array and
;             the action by one far call to P3's GPUTA, the rows by the
;             OEM's PGINIT, NREAD and NWRITE (p5/gfxoem.s); CHKRNG
;
; The 8086's registers are the pseudo-registers, as in p5/gengrp.s: BX
; (HL in the source's comments) RBL/RBH, DX (DE) RDL/RDH, CX RCL/RCH. What
; the source keeps on the 8086's stack across CPLOT (the offsets, the loop
; count) and its data segment's CIRCLE words are zero page here: SI, DI, BP
; and PTR, which the drawing (DOGRPH, MAPXYC, SETC) doesn't use, and the
; floating point's scratch from DBUFF ($3A-$51), which nothing uses while a
; statement draws (OO-1: for room and speed). The drawing calls nothing resident: the aperture is in
; W1 from the first point, and block 1 is put back (GX_W1BACK) at the end.

.include "gwbasic.inc"
.include "p5/p5.inc"
.import GX_W1BACK

.export CIRCP, CIRCC, PAINTP, DRWOP, GPUTG
.import POLKEY, CHRGTR, SYNCHR, FRMEVL, CONINT, GETBYT, FCERR, OMERR, GARBA2, FREFAC, FRESTR, TXBUF

; Zero page: the loop's, and CIRCA's results from CZ, in the order
; CIRCA leaves them from DX on.
CRCX            = RSI                  ; the loop's X (twice the radius at first)
CRCY            = RDI                  ;   its Y
CRCSUM          = RBP                  ;   and SUM
CPC             = PTR                  ; CPLOT: this point's count
CZ              = DBUFF
CSTCNT          = CZ                   ; the start's point count
CENCNT          = CZ + 2               ; the end's
CLINEF          = CZ + 4               ; bit 0: a line from the start; bit 7 from the end
CPLOTF          = CZ + 5               ; 0: plot between start and end; $FF outside
CNPNTS          = CZ + 6               ; the points an octant
ASPECT          = CZ + 8               ; the aspect, a fraction of 256
CZOUT           = 10                   ;   (CIRCA's to here)
CSCLXY          = CZ + 10              ; 1: x scaled, not y
CPCNT           = CZ + 11              ; CPLOT8's point count
CPCNT8          = CZ + 13              ;   the octants' counts so far
CXOFF           = CZ + 15              ;   the offsets swapped between points
CYOFF           = CZ + 17
CHL             = CZ + 19              ; CPLOT: HL and DE (the 8086's stack)
CDE             = CZ + 21
CPLN            = CZ + 23              ;   the points left of four
.assert CPLN < FOEXP + 1, error, "CIRCLE's zero page past the floating point's scratch"

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
.macro  NEGW    zp                     ; NEGDE, NEGHL
                SEC
                LDA     #0
                SBC     zp
                STA     zp
                LDA     #0
                SBC     zp + 1
                STA     zp + 1
.endmacro

.code

; --- CIRCLE (ADVGRP.ASM 460) ------------------------------------------------------------------

CIRCP:          JSR     SCAN1          ; the centre: the cursor (the source's)
                JSR     GTASPC         ; for CIRCA: GTASPC's pair, the
                MOVW    RSI, RDL       ; foreground, PIXSIZ
                SEC
                JSR     GETFBC
                STA     RAL
                JSR     PIXSIZ
                STA     RAH
                FJSR    CIRCA          ; the statement parsed (P3)
                LDX     #CZOUT - 1     ; CSTCNT .. ASPECT, from DX .. SI
@o:             LDA     RDL,X
                STA     CZ,X
                DEX
                BPL     @o
                LDA     T1
                STA     CSCLXY
                MOVW    CRCX, RDI      ; X = the radius x 2
                ASL     CRCX
                ROL     CRCX + 1
                STZ     CRCY           ; Y = 0
                STZ     CRCY + 1
                STZ     CRCSUM         ; SUM = 0
                STZ     CRCSUM + 1
; CIRCLP: the half-unit loop: at each even Y the eight points of
; ((X + 1) / 2, (Y + 1) / 2), until Y reaches X; SUM = SUM + 2Y + 1, Y + 1,
; and while SUM isn't negative SUM = SUM - 2X + 1, X - 1.
CIRCLP:         LDA     CRCY
                LSR     A
                BCS     CRCLP2         ; Y odd: no points
                CLC                    ; HL = (X + 1) / 2
                LDA     CRCX
                ADC     #1
                STA     RBL
                LDA     CRCX + 1
                ADC     #0
                LSR     A
                STA     RBH
                ROR     RBL
                CLC                    ; DE = (Y + 1) / 2
                LDA     CRCY
                ADC     #1
                STA     RDL
                LDA     CRCY + 1
                ADC     #0
                LSR     A
                STA     RDH
                ROR     RDL
                JSR     CPLOT8
                LDA     CRCY           ; Y >= X: done
                CMP     CRCX
                LDA     CRCY + 1
                SBC     CRCX + 1
                BCC     CRCLP2
                JMP     GX_W1BACK      ; (OO-1)
CRCLP2:         SEC                    ; SUM + 2Y + 1
                LDA     CRCY
                ROL     A
                STA     T0
                LDA     CRCY + 1
                ROL     A
                STA     T1
                CLC
                LDA     CRCSUM
                ADC     T0
                STA     CRCSUM
                LDA     CRCSUM + 1
                ADC     T1
                STA     CRCSUM + 1
                BMI     CNODEX         ; negative: X stays
                LDA     CRCX           ; SUM - (2X - 1), X - 1
                ASL     A
                STA     T0
                LDA     CRCX + 1
                ROL     A
                STA     T1
                LDA     T0
                BNE     :+
                DEC     T1
:               DEC     T0
                SEC
                LDA     CRCSUM
                SBC     T0
                STA     CRCSUM
                LDA     CRCSUM + 1
                SBC     T1
                STA     CRCSUM + 1
                DECW    CRCX
CNODEX:         INC     CRCY           ; Y + 1
                BNE     :+
                INC     CRCY + 1       ; OO-1 (the owner's ruling of 2026-11-19):
                JSR     GX_W1BACK      ; every 256 steps Ctrl-C's poll, block 1
                JSR     POLKEY         ; back (the next point's MAPXYC maps the
:               JMP     CIRCLP         ; aperture again), for a radius's long loop

; CIRCC: from P3's CIRCA, the colour (A) parsed: (with pixels: ATRFI2
; refuses SCREEN 0 first) the attribute, SETATR's and ATRBYT, as ATRSCN
; makes it (review round 1, F1).
CIRCC:          LDX     GMODEV         ; SCREEN 0: none
                BEQ     @x
                JSR     SETATR
                STA     ATRBYT
@x:             RTS

; CPLSCX: DE scaled by the aspect (SCALEY) to DX, BX the unscaled; the two
; swapped when the aspect was over 1 (CSCLXY: x scaled, not y).
CPLSCX:         PSHW    RDL
                JSR     SCALEY
                PULW    RBL
                LDA     CSCLXY
                BEQ     XCHRT
; XCHBD: XCHG BX,DX.
XCHBD:          LDX     RBL
                LDY     RDL
                STX     RDL
                STY     RBL
                LDX     RBH
                LDY     RDH
                STX     RDH
                STY     RBH
XCHRT:          RTS

; CPLOT8: the eight points of HL (x) and DE (y), offsets from the centre:
; the even octants, then the odd ones backwards (the point count NP - DE).
CPLOT8:         MOVW    CPCNT, RDL     ; the point count: Y
                PSHW    RBL            ; X
                STZ     CPCNT8
                STZ     CPCNT8 + 1
                JSR     CPLSCX         ; Y scaled
                MOVW    CXOFF, RBL
                PULW    RBL
                JSR     XCHBD
                PSHW    RBL
                JSR     CPLSCX         ; X scaled
                MOVW    CYOFF, RDL
                PULW    RDL
                NEGW    RDL            ; DE = -SY, HL = X, CXOFF Y, CYOFF SX
                JSR     CPLOT4
                SEC                    ; the odd octants: CPCNT8 NP, CPCNT NP - CPCNT,
                LDA     CNPNTS         ; CXOFF and DE negated
                STA     CPCNT8
                SBC     CPCNT
                STA     CPCNT
                LDA     CNPNTS + 1
                STA     CPCNT8 + 1
                SBC     CPCNT + 1
                STA     CPCNT + 1
                NEGW    CXOFF
                NEGW    RDL
CPLOT4:         LDA     #4             ; four points
                STA     CPLN
CPLOT:          MOVW    CHL, RBL
                MOVW    CDE, RDL
                CLC                    ; this point's count: CPCNT + CPCNT8, and
                LDA     CPCNT8         ; CPCNT8 + 2 NP for the next
                ADC     CPCNT
                STA     CPC
                LDA     CPCNT8 + 1
                ADC     CPCNT + 1
                STA     CPC + 1
                LDA     CNPNTS
                ASL     A
                STA     T0
                LDA     CNPNTS + 1
                ROL     A
                STA     T1
                CLC
                LDA     CPCNT8
                ADC     T0
                STA     CPCNT8
                LDA     CPCNT8 + 1
                ADC     T1
                STA     CPCNT8 + 1
                LDA     CSTCNT         ; the start: the same, a line from it?
                CMP     CPC
                BNE     :+
                LDA     CSTCNT + 1
                CMP     CPC + 1
                BEQ     CLINSC
:               LDA     CPC            ; past this point: not between
                CMP     CSTCNT
                LDA     CPC + 1
                SBC     CSTCNT + 1
                BCC     CNBTWN
                LDA     CENCNT         ; the end
                CMP     CPC
                BNE     :+
                LDA     CENCNT + 1
                CMP     CPC + 1
                BEQ     CLINEC
:               LDA     CPC            ; the end past this point: between
                CMP     CENCNT
                LDA     CPC + 1
                SBC     CENCNT + 1
                BCC     CBTWEN
CNBTWN:         LDA     CPLOTF         ; not between: plotted if CPLOTF says
                BNE     CPLTIT
                BRA     CPLFIN
CLINEC:         BIT     CLINEF         ; bit 7: a line from the centre
                BPL     CPLTIT
                BRA     CLINE
CLINSC:         LDA     CLINEF         ; bit 0: a line to the centre
                LSR     A
                BCC     CPLTIT
CLINE:          JSR     GTABSC
                JSR     CLINE2
                BRA     CPLFIN
CBTWEN:         LDA     CPLOTF         ; between: plotted if CPLOTF is 0
                BNE     CPLFIN
CPLTIT:         JSR     GTABSC
                JSR     SCALXY         ; off the screen: no plot
                BCC     CPLFIN
                JSR     MAPXYC
                JSR     SETC
CPLFIN:         DEC     CPLN
                BNE     :+
                MOVW    RBL, CHL
                MOVW    RDL, CDE
                RTS
:               SEC                    ; HL = -CXOFF, CXOFF = HL; DE = -CYOFF,
                LDA     #0             ; CYOFF = DE
                SBC     CXOFF
                STA     RBL
                LDA     #0
                SBC     CXOFF + 1
                STA     RBH
                MOVW    CXOFF, CHL
                SEC
                LDA     #0
                SBC     CYOFF
                STA     RDL
                LDA     #0
                SBC     CYOFF + 1
                STA     RDH
                MOVW    CYOFF, CDE
                JMP     CPLOT

; --- PAINT (ADVGRP.ASM 121) -------------------------------------------------------------------

; PAINTP: PAINT [STEP](x,y)[,[paint][,[boundary][,background]]]. The queue
; first (INTQUE, free memory, a garbage collection under 1,000 bytes), then
; the start (SCAN1: the cursor moves there), the paint (ATRSCN's: the
; foreground if none) and the boundary (the paint's if none: PNTINI). The
; IBM manual's p. 236 (the owner's ruling under the IBM spec): a start off
; the screen paints nothing, no error, the cursor moved there as SCAN1 has
; it (the source's CHKRNG gives Illegal function call). 7e4 (pp. 236-245;
; the owner's answers 5 and 7): a string paint is a tile, 1-64 bytes (else
; Illegal function call), copied to TXBUF here, before the aperture is
; mapped (PTILE); the boundary's default then the mode's foreground; the
; background a string's first byte ("" none; a number Type mismatch; with a
; numeric paint taken and not used); with none given CHR$(0) (the owner's
; answer 13: a blank area isn't "already painted"); a tile with three bytes
; in a row equal to the background, or wholly it, Illegal function call
; (p. 243), the default's as any.
PAINTP:         LDA     FRETOP         ; the queue: free memory to FRETOP
                LDX     FRETOP + 1
                LDY     #0             ;   with a collection
                JSR     INTQUE
                JSR     SCAN1
                PSHW    RCL            ; the start
                PSHW    RDL
                STZ     TILEF
                STZ     TBGF
                SEC                    ; the paint: ATRSCN's, or a tile
                JSR     GETFBC
                STA     RDL
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     @pfin
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @pfin          ; null: the default
                JSR     FRMEVL
                LDA     VALTYP
                CMP     #VT_STR
                BNE     @num
                JSR     PTILE          ; a tile
                SEC                    ; the boundary's default: the foreground
                JSR     GETFBC
                STA     RDL
                BRA     @pix
@num:           JSR     CONINT
@pfin:          LDA     RDL
                JSR     SETATR
                STA     ATRBYT         ; the boundary's default: the paint
                STA     RDL
@pix:           JSR     PIXSIZ         ; (ATRFI2's)
                BNE     :+
                JMP     FCERR
:               DECW    TXTPTR
                JSR     CHRGTR
                BEQ     GOTBRD
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @bg            ; a background with no boundary
                JSR     GETBYT
                LDA     (TXTPTR)
                CMP     #','
                BNE     GOTBRD
@bg:            LDA     RDL            ; the background (the boundary kept)
                PHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                BIT     TILEF
                BPL     @bx            ; a numeric paint: not used
                JSR     PBACK
@bx:            PLA
                STA     RDL
GOTBRD:         LDA     RDL
                JSR     PNTINI
                BIT     TILEF          ; 7e4: the tile refused, or set up
                BPL     :+
                JSR     PREFU
                JSR     TSET
:
                PULW    RDL
                PULW    RCL
                JSR     SCALXY         ; off the screen: nothing (p. 236)
                BCC     PNTX
                ; (on into PNTCOR)

; PNTCOR: the fill from (CX, DX), on the screen, in ATRBYT to PBORD, with
; the queue set up (INTQUE). DRAW's P comes here too.
PNTCOR:         JSR     MAPXYC
                MOVW    PNX, RCL
                LDA     RDL
                STA     PNY
                JSR     TROWS          ; 7e4: the tile's row
                LDA     #1             ; the skip count: one (none of the border)
                STA     RDL
                STZ     RDH
                JSR     SCANR1         ; right from the start
                BEQ     PNTX           ; on the border: nothing
                PSHW    RBL            ; the count to the right
                JSR     SCANL1         ; and left
                PULW    RDL
                JSR     ADDBD          ; DX: the run's count
                LDA     #$40           ; an entry down
                JSR     ENTST1
                LDA     #$C0           ; and paint up now
                BRA     STPAIN
PNTX:           LDA     ATRBYT         ; 7e4: CPAT back (a tile's pixels set it)
                JSR     SETATR
                JMP     GX_W1BACK      ; (OO-1)

PNTLOP:         JSR     GX_W1BACK      ; CKCNTC, with block 1 in W1 (OO-1)
                JSR     POLKEY
PNTLP1:         JSR     GETQ           ; the next entry (A its direction, 0 none)
                BEQ     PNTX
                PHA
                JSR     PSTOR          ; STOREC
                PLA
STPAIN:         STA     PDIREC
                ASL     A              ; 0: done; C: up
                BEQ     PNTX
                PSHW    RDL
                BCC     PDOWN
                JSR     TUPC
                BRA     PDOWN2
PDOWN:          JSR     TDOWNC
PDOWN2:         PULW    RDL            ; (C kept)
                BCS     PNTLP1         ; past the screen: the next
PNTLP2:         JSR     SCANR1
                BEQ     PNTLP1         ; nothing painted: the next
                JSR     SCANL1
                MOVW    RDL, RBL       ; DX: the left count
                LDA     LFPROG         ; the line already painted: no overhang
                BEQ     PNTLP3
                LDA     RBH            ; the left count past 1: an overhang
                BNE     :+             ; entry the other way
                LDA     RBL
                CMP     #2
                BCC     PNTLP3
:               LDA     PDIREC
                EOR     #$FF
                JSR     ENTST1
PNTLP3:         MOVW    RBL, MOVCNT    ; the run's count: the right's and left's
                JSR     ADDBD
                JSR     ENTSLR
                MOVW    RCL, PCSX      ; back to the right scan's end
                JSR     PSTOR
PNTLP4:         SEC                    ; SKPCNT - MOVCNT: more border to skip?
                LDA     SKPCNT
                SBC     MOVCNT
                STA     RBL
                LDA     SKPCNT + 1
                SBC     MOVCNT + 1
                STA     RBH
                BMI     PNTLP6         ; a right overhang
                ORA     RBL
                BEQ     GOPLOP         ; none: the scan's end
                MOVW    RDL, RBL
                JSR     SCANR1         ; on right
                BEQ     GOPLOP
                LDA     RTPROG         ; already painted: no entry
                BEQ     PNTLP4
                MOVW    RDL, RBL       ; an entry at the run's start, this way
                MOVW    QX, PCSX
                LDA     PDIREC
                JSR     PUTQ
                BRA     PNTLP4
PNTLP6:         SEC                    ; -(SKPCNT - MOVCNT) - 2: an entry the
                LDA     #<-2           ; other way if it isn't negative
                SBC     RBL
                STA     RDL
                LDA     #>-2
                SBC     RBH
                STA     RDH
                BMI     GOPLOP
                INCW    RDL            ; from the overhang's start, -(SKPCNT -
                MOVW    RBL, RDL       ; MOVCNT) - 1 to the left
@l:             JSR     LEFTC
                DECW    PNX
                LDA     RBL
                BNE     :+
                DEC     RBH
:               DEC     RBL
                LDA     RBL
                ORA     RBH
                BNE     @l
                LDA     PDIREC
                EOR     #$FF
                JSR     ENTST1
GOPLOP:         JMP     PNTLOP

; ADDBD: DX = BX + DX.
ADDBD:          CLC
                LDA     RBL
                ADC     RDL
                STA     RDL
                LDA     RBH
                ADC     RDH
                STA     RDH
                RTS

; ENTSLR: an entry this way at the cursor, unless the line was painted
; already (neither scan changed a pixel). ENTST1: A's way at the cursor.
; DX the count. PUTQ: the entry (QX, PNY, A, DX).
ENTSLR:         LDA     LFPROG
                ORA     RTPROG
                BEQ     QRET
                LDA     PDIREC
ENTST1:         PHA
                MOVW    QX, PNX
                PLA
PUTQ:           STA     QDIR
                CLC                    ; the queue's length + 6
                LDA     PSNLEN
                ADC     #6
                STA     PSNLEN
                LDA     PSNLEN + 1
                ADC     #0
                STA     PSNLEN + 1
                LDA     PSNLEN         ; at its size or past: Out of memory
                CMP     QUELEN
                LDA     PSNLEN + 1
                SBC     QUELEN + 1
                BCC     :+
                JSR     GX_W1BACK      ; (OO-1)
                JMP     OMERR
:               LDX     #QUEINP - QPTRS
                JSR     QAT            ; PTR the entry's place, block 1 if needed
                LDY     #0
                LDA     QX
                STA     (PTR),Y
                INY
                LDA     QX + 1
                STA     (PTR),Y
                INY
                LDA     PNY
                STA     (PTR),Y
                INY
                LDA     QDIR
                STA     (PTR),Y
                INY
                LDA     RDL
                STA     (PTR),Y
                INY
                LDA     RDH
                STA     (PTR),Y
                BIT     QW1
                BPL     QRET
                LDA     CBANK          ; (OO-1) the aperture back
                JMP     NEWBNK
QRET:           RTS

; GETQ: an entry taken: A its direction (0: none left), CX its x, PNY its
; y, DX its count. Block 1 left in W1 if the entry was there.
GETQ:           LDA     PSNLEN
                ORA     PSNLEN + 1
                BEQ     QRET           ; (A 0)
                SEC
                LDA     PSNLEN
                SBC     #6
                STA     PSNLEN
                BCS     :+
                DEC     PSNLEN + 1
:               LDX     #QUEOUT - QPTRS
                JSR     QAT
                LDY     #5
                LDA     (PTR),Y
                STA     RDH
                DEY
                LDA     (PTR),Y
                STA     RDL
                DEY
                LDA     (PTR),Y
                PHA                    ; the direction
                DEY
                LDA     (PTR),Y
                STA     PNY
                DEY
                LDA     (PTR),Y
                STA     RCH
                LDA     (PTR)
                STA     RCL
                PLA
                RTS

; QAT: X QUEINP's or QUEOUT's offset in QPTRS: PTR its entry, wrapped (WRAP:
; to the queue's start when the entry wouldn't end before QTOP), the
; pointer moved past it; QW1 bit 7 and block 1 put back in W1 if the entry
; may lie in $4000-$7FFF (OO-1: the aperture is there while PAINT draws).
QAT:            CLC                    ; WRAP
                LDA     QPTRS,X
                ADC     #6
                TAY
                LDA     QPTRS + 1,X
                ADC     #0
                CPY     QTOP
                SBC     QTOP + 1
                BCC     :+
                LDA     STREND         ; past the end: the start
                STA     QPTRS,X
                LDA     STREND + 1
                STA     QPTRS + 1,X
:               LDA     QPTRS,X
                STA     PTR
                CLC
                ADC     #6
                STA     QPTRS,X
                LDA     QPTRS + 1,X
                STA     PTR + 1
                ADC     #0
                STA     QPTRS + 1,X
                STZ     QW1
                LDA     PTR + 1        ; $3F00-$7FFF: block 1 there
                CMP     #$3F
                BCC     @x
                CMP     #$80
                BCS     @x
                DEC     QW1
                JMP     GX_W1BACK
@x:             RTS

; INTQUE: the queue in free memory, from STREND to A/X (FRETOP, or DRAW's
; P's cap), Y 0 with a garbage collection if under 1,000 bytes are free
; (DRAW's P: none, Y not 0); under 12 bytes Out of memory.
INTQUE:         STA     QTOP
                STX     QTOP + 1
                TYA
                BNE     @len
                JSR     QFREE
                LDA     RBH
                CMP     #>1000
                BCC     :+
                BNE     @len
                LDA     RBL
                CMP     #<1000
                BCS     @len
:               JSR     GARBA2
                MOVW    QTOP, FRETOP
@len:           JSR     QFREE
                LDA     RBH
                BNE     :+
                LDA     RBL
                CMP     #12
                BCS     :+
                JMP     OMERR
:               MOVW    QUELEN, RBL
                STZ     PSNLEN
                STZ     PSNLEN + 1
                MOVW    QUEINP, STREND
                MOVW    QUEOUT, STREND
                RTS
; QFREE: BX = QTOP - STREND.
QFREE:          SEC
                LDA     QTOP
                SBC     STREND
                STA     RBL
                LDA     QTOP + 1
                SBC     STREND + 1
                STA     RBH
                RTS

; SCANR1: SCANR, SKPCNT and MOVCNT kept, RTPROG its flag; Z by BX.
SCANR1:         JSR     SCANR
                MOVW    SKPCNT, RDL
                MOVW    MOVCNT, RBL
                LDA     RCL
                STA     RTPROG
                LDA     RBL
                ORA     RBH
                RTS
; SCANL1: the cursor back at the run's start (PCSX), the right scan's end
; kept in PCSX; SCANL; LFPROG its flag (A).
SCANL1:         MOVW    RCL, PCSX
                MOVW    PCSX, PNX
                JSR     PSTOR
                JSR     SCANL
                LDA     RCL
                STA     LFPROG
                RTS

; --- DRAW's drawing (ADVGRP.ASM 1254) ---------------------------------------------------------

; DRWOP: from P3's DRAWP (p3/draw.s), Y the operation:
;   0  the start: PIXSIZ (SCREEN 0: Illegal function call, before the
;      string is read, the source's DRAW); out BX and DX GTASPC's pair, RAL
;      DRWSCL, RAH DRWANG, RCL TA's state (OO-1)
;   1  a relative move, BX and DX: GTABSC, then 2
;   2  DRWABS: CX, DX, A DRWFLG (bit 7 B: no line; bit 6 N: the cursor
;      stays); the line drawn as LINE's is (DOGRPH's clipping: the owner's
;      G11; DRWABS's SCALXY left out)
;   3  C: A the colour, SETATR's (ATRBYT)
;   4  P (OO-1): RCL the paint, RCH the border, BX the queue's end: the
;      paint becomes the drawing colour (ATRBYT), then PAINT's fill at the
;      cursor (none off the screen), the queue with no garbage collection
;   5  RAL DRWSCL, RAH DRWANG, RCL TA's state kept here (GRPRST clears them)
DRWOP:          CPY     #1
                BCC     DRW0
                BEQ     DRW1
                CPY     #3
                BCC     DRW2
                JEQ     DRW3
                CPY     #4
                JEQ     DRW4
                LDA     RAL            ; 5
                STA     DRWSCL
                LDA     RAH
                STA     DRWANG
                LDA     RCL
                STA     DRWTA
                RTS
DRW0:           JSR     PIXSIZ
                BNE     :+
                JMP     FCERR
:               LDA     ATRBYT         ; 7e4: CPAT ATRBYT's (a tiled PAINT broken off)
                JSR     SETATR
                JSR     GTASPC
                LDA     DRWSCL
                STA     RAL
                LDA     DRWANG
                STA     RAH
                LDA     DRWTA
                STA     RCL
                RTS
DRW1:           PHA
                JSR     GTABSC
                BRA     DRW2P
DRW2:           PHA                    ; 7e4: an absolute M, VIEW SCREEN's offset off
                JSR     VSOFF
DRW2P:          PLA
DRWABS:         STA     DRWFLG
                BIT     DRWFLG         ; B: just the position
                BMI     DSTPOS
                PSHW    RCL
                PSHW    RDL
                JSR     CLINE2
                JSR     GX_W1BACK      ; (OO-1)
                PULW    RDL
                PULW    RCL
DSTPOS:         BIT     DRWFLG         ; N: the cursor stays
                BVS     DNSTOR
                MOVW    GRPACX, RCL
                MOVW    GRPACY, RDL
DNSTOR:         RTS
DRW3:           JSR     SETATR
                STA     ATRBYT
                RTS
DRW4:           STZ     TILEF          ; 7e4: no tile
                LDA     RCL            ; the paint: the colour from now
                JSR     SETATR
                STA     ATRBYT
                LDA     RCH
                JSR     PNTINI
                LDA     RBL            ; the queue, no collection
                LDX     RBH
                LDY     #1
                JSR     INTQUE
                MOVW    RCL, GRPACX
                MOVW    RDL, GRPACY
                JSR     SCALXY         ; off the screen: nothing
                BCS     :+
                RTS
:               JMP     PNTCOR

; CLINE2: a line from (CX, DX) to the cursor (GRPACX, GRPACY).
CLINE2:         MOVW    GXPOS, GRPACX
                MOVW    GYPOS, GRPACY
                JMP     DOGRPH

; GTABSC: (CX, DX) = (GRPACX + HL, GRPACY + DE).
GTABSC:         CLC
                LDA     GRPACX
                ADC     RBL
                STA     RCL
                LDA     GRPACX + 1
                ADC     RBH
                STA     RCH
                CLC
                LDA     GRPACY
                ADC     RDL
                STA     RDL
                LDA     GRPACY + 1
                ADC     RDH
                STA     RDH
                RTS

; SCALEY: DE scaled by ASPECT; SCALE by BX: x BL / 256, rounded (SCAL2, the
; 8086's MUL as an 8 x 16 multiply here); BL 0 is x 1 if BH isn't 0, else x 0.
SCALEY:         MOVW    RBL, ASPECT
SCALE:          LDA     RBL
SCALE2:         BNE     SCAL2          ; A x DE (A not 0)
                ORA     RBH
                BEQ     :+
                RTS                    ; x 1
:               MOVW    RBL, RDL       ; x 0: XCHG BX,DX (BX 0)
                STZ     RDL
                STZ     RDH
                RTS
SCAL2:          STA     T0             ; T3:T2:T1 = A x DE
                STZ     T1
                STZ     T2
                STZ     T3
                LDX     #8
@m:             ASL     T1
                ROL     T2
                ROL     T3
                ASL     T0
                BCC     @n
                CLC
                LDA     T1
                ADC     RDL
                STA     T1
                LDA     T2
                ADC     RDH
                STA     T2
                BCC     @n
                INC     T3
@n:             DEX
                BNE     @m
                CLC                    ; + 128, the high 16 bits
                LDA     T1
                ADC     #128
                LDA     T2
                ADC     #0
                STA     RDL
                LDA     T3
                ADC     #0
                STA     RDH
                RTS


; --- GET and PUT (ADVGRP.ASM 845) ---------------------------------------------------------

; GPUTG (7e3): GET (A 0) or PUT (A 1) of graphics, from the home block's
; stubs (p8/far4.s: the character after the word "(" or "@", or GET's STEP,
; the owner's answer 5; screen mode; TXBUF emptied), TXTPTR at it.
;   GET [STEP](x1,y1)-[STEP](x2,y2),array
;   PUT (x,y),array[,OR|AND|PRESET|PSET|XOR]
; The mode's test (PIXSIZ), the first corner by SCAN1 (the cursor at once,
; as CIRCLE's centre), CHKRNG; for GET the "-", the second by SCAND, CHKRNG,
; and the deltas (MINDEL the rows, MAXDEL the pixels), the corner made the
; lesser of each. Then one far call to P3's GPUTA (p3/advgrp.s: the owner's
; answer 1) for the array (GTARRY, numeric: answer 6), GET's size check and
; header, PUT's action word, header and far corner; the corner and the deltas
; go in the pseudo-registers, the data's address, the bits a row, the rows
; and the action come back (and PUT's far corner, which CHKRNG checks here).
; An unknown action word comes back with carry set: nothing drawn, NEWSTT's
; Syntax error. Then MAPXYC, PGINIT and PGLOOP (the OEM's, p5/gfxoem.s), CPAT
; back, block 1 back. Every error comes before MAPXYC (7e1's rule).
GPUTG:          STA     GPPUT
                JSR     PIXSIZ
                BNE     :+
                JMP     FCERR          ; not a graphics mode
:               JSR     SCAN1          ; the first corner
                JSR     CHKRNG
                LDA     GPPUT
                BNE     PUT1
                JSR     SYNCHR
                .byte   MINUTK
                PSHW    RCL
                PSHW    RDL
                JSR     SCAND          ; GET's second
                JSR     CHKRNG
                PULW    RDL
                PULW    RCL
                JSR     YDELT
                BCC     :+
                JSR     XCHGY          ; DX the lesser
:               INCW    RBL            ; a count
                MOVW    RSI, RBL       ; the rows (MINDEL)
                JSR     XDELT
                BCC     :+
                JSR     XCHGX          ; CX the lesser
:               INCW    RBL            ; the pixels (MAXDEL), BX for GPUTA
PUT1:           MOVW    GPCX, RCL      ; the corner
                MOVW    GPCY, RDL
                LDA     GPPUT
                STA     RAL
                JSR     PIXSIZ
                STA     RAH
                FJSR    GPUTA          ; the array, the header, the action (P3)
                BCS     GPUTX          ; an unknown action: NEWSTT's Syntax error
                LDA     GPPUT
                BEQ     GOPGIN
                JSR     CHKRNG         ; PUT's far corner on the screen
GOPGIN:         MOVW    GPROWS, RDI
                MOVW    RCL, GPCX
                MOVW    RDL, GPCY
                JSR     MAPXYC
                LDA     RAL            ; the action; RBL/RBH the bits, RSI the data
                JSR     PGINIT
; PGLOOP: each row NREAD or NWRITE, then DOWNC.
PGLOOP:         LDA     GPPUT
                BNE     :+
                JSR     NREAD
                BRA     @d
:               JSR     NWRITE
@d:             JSR     DOWNC
                DECW    GPROWS
                LDA     GPROWS
                ORA     GPROWS + 1
                BNE     PGLOOP
                LDA     GPCPAT         ; (OO-1) the attribute's pattern back
                STA     CPAT
                JMP     GX_W1BACK
GPUTX:          RTS

; CHKRNG (ADVGRP.ASM 1348): CX, DX on the screen (SCALXY), else Illegal
; function call.
CHKRNG:         JSR     SCALXY
                BCS     :+
                JMP     FCERR
:               RTS


; PTILE (7e4): the FAC's string the tile: 1-64 bytes, else Illegal function
; call; copied to TXBUF (TLEN its bytes), TILEF set.
PTILE:          JSR     FREFAC         ; BX: the descriptor
                LDA     (RBL)
                BEQ     PTFC
                CMP     #65
                BCS     PTFC
                STA     TLEN
                JSR     PSTR
@c:             LDA     (PTR),Y
                STA     TXBUF,Y
                INY
                CPY     TLEN
                BNE     @c
                LDA     #$80
                STA     TILEF
                STZ     TBG            ; the background CHR$(0) unless one is given (the
                INC     TBGF           ; owner's answer 13)
                RTS
PTFC:           JMP     FCERR
; PSTR: PTR the string's text (BX its descriptor), Y 0.
PSTR:           LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR + 1
                LDY     #0
                RTS
; PBACK (7e4): the FAC the background, a string (else Type mismatch): its
; first byte TBG ("" none: the default's CHR$(0)).
PBACK:          JSR     FRESTR
                LDA     (RBL)
                BEQ     PRX
                JSR     PSTR
                LDA     (PTR)
                STA     TBG
PRX:            RTS
; PREFU (7e4): a tile refused (Illegal function call) if three of its bytes
; in a row, or all of them, are the background's (the IBM manual's p. 243;
; the default CHR$(0)'s too, the owner's answer 13).
PREFU:          LDY     #0
                LDX     #0             ; the run of tile bytes equal to it
@r:             LDA     TXBUF,Y
                CMP     TBG
                BEQ     :+
                LDX     #$FF
:               INX
                CPX     #3
                BCS     PTFC
                INY
                CPY     TLEN
                BNE     @r
                CPX     TLEN           ; a tile wholly the background
                BEQ     PTFC
                RTS


.segment "BRAM"
PDIREC:         .res    1              ; PAINT: the way, $40 down, $C0 up
SKPCNT:         .res    2              ;   SCANR's skip count left
MOVCNT:         .res    2              ;   its run's count
RTPROG:         .res    1              ;   the right scan changed a pixel
LFPROG:         .res    1              ;   the left scan
QX:             .res    2              ;   an entry's x
QDIR:           .res    1              ;   and its way
QW1:            .res    1              ;   bit 7: block 1 put in W1 for it
QTOP:           .res    2              ; the queue: its end (FRETOP, or a cap)
QUELEN:         .res    2              ;   its size
PSNLEN:         .res    2              ;   its length now
QPTRS:
QUEINP:         .res    2              ;   where an entry goes
QUEOUT:         .res    2              ;   where one is taken
DRWFLG:         .res    1              ; DRAW: DRWABS's B and N (DRWOP 2)
DRWSCL:         .res    1              ;   S (0 none: GRPRST's)
DRWANG:         .res    1              ;   A
DRWTA:          .res    1              ;   1: TA's turn (OO-1)
GPPUT:          .res    1              ; 7e3: GET 0, PUT 1 (the source's PUTFLG)
GPCX:           .res    2              ;   the corner, MAPXYC's
GPCY:           .res    2
GPROWS:         .res    2              ;   the rows left
