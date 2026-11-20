; The OEM's pixel routines, in block P5 (hw/oodos.md, "The plan, phase 7":
; 7e1, U2): the project's own. GENGRP.ASM calls them and the release leaves
; them to the OEM (OEM.H names them), so they are the OO-1's, written for
; the two frames P5's GMODE sets up (p5/p5.inc), and drawn by the CPU
; through aperture slot 1 in W1 (OOLIB's gxmem, the W1 contract):
;
;   SCALXY    CX, DX (x, y, signed) made a point of the screen: each clipped
;             to 0 and the mode's last; carry set if both were on it already
;             (PSET, POINT, and BF's corners; a line's ends are clipped by
;             p5/gengrp.s's LINCLP)
;   MAPXYC    C, the graphics cursor, made the pixel at CX, DX (on the
;             screen), and the aperture put in W1 on its card bank
;   SETC      the cursor's pixel set to the attribute's colour (SETATR's)
;   READC     A the cursor's pixel's attribute
;   RIGHTC LEFTC DOWNC UPC   the cursor moved by a pixel (off the screen
;             too: nothing is drawn there, and the bank isn't mapped)
;   NSETCX    BX pixels from the cursor rightwards set (BX at least 1), a
;             byte at a time where the run covers a whole one
;   FETCHC STOREC   the cursor kept (in CSAV) and put back; REMAP: the
;             cursor's bank put back in slot 1 after a STOREC
;
; The cursor C is GXP, the byte's address in W1 ($4000-$7FFF), CBANK its
; bank in the page (the page is on a bank, GALIGN: so a byte's bank is its
; offset's top 2 bits), CMASK the pixel's bits in it, and for the tiles CXB
; (the byte in the tile's row, 0-3) and CY7 (the row in the tile, 0-7).
; SCREEN 1: the byte of (x, y) is (y / 8) x 1,280 + (x / 8) x 32 + (y mod 8)
; x 4 + (x mod 8) / 2 from the page, the left pixel in its high 4 bits
; (CMASK $F0, else $0F). SCREEN 2: y x 160 + x / 8, CMASK bit 7 - x mod 8,
; and the row's twin 80 bytes on, both written (SETC). The slot is moved
; only when the cursor crosses a bank (NEWBNK: GX_VMAP), so a line or box
; maps it once and at each 16 KB. The caller puts block 1 back (GX_W1BACK)
; once it has drawn, before it returns and before anything else runs: W1
; holds the aperture only inside a statement's drawing, which calls nothing
; resident and no BDOS function but gxmem's GX_VMAP.

.include "gwbasic.inc"
.include "p5/p5.inc"
; OOLIB's (oolib.inc, vmem.inc: not included here, since gwbasic.inc defines
; GXP, gxmem's pointer, which only p5/seed.s exports to it)
.import GX_VMAP

.code

; --- SCALXY -----------------------------------------------------------------------------------

SCALXY:         LDY     #0             ; Y: 1 once a coordinate was clipped
                LDA     RCH
                BPL     :+
                STZ     RCL            ; x < 0
                STZ     RCH
                INY
                BRA     @y
:               LDA     GMAXX          ; x > the last
                CMP     RCL
                LDA     GMAXX + 1
                SBC     RCH
                BCS     @y
                LDA     GMAXX
                STA     RCL
                LDA     GMAXX + 1
                STA     RCH
                INY
@y:             LDA     RDH
                BPL     :+
                STZ     RDL
                STZ     RDH
                INY
                BRA     @f
:               BNE     @yh            ; y > 255
                LDA     RDL
                CMP     #200
                BCC     @f
@yh:            LDA     #199
                STA     RDL
                STZ     RDH
                INY
@f:             CPY     #1             ; C: Y 0, on the screen
                ROL     A
                EOR     #1
                LSR     A
                RTS

; --- MAPXYC -----------------------------------------------------------------------------------

MAPXYC:         LDX     GMX
                JMP     (MAPV - 2,X)

; The tiles: T2:T1 the byte's offset (above).
MAPT:           LDA     RDL            ; (y mod 8) x 4
                AND     #7
                STA     CY7
                ASL     A
                ASL     A
                STA     T0
                LDA     RCL            ; + (x mod 8) / 2
                LSR     A
                AND     #3
                STA     CXB
                ORA     T0
                STA     T0
                LDA     RCL            ; + (x / 8) x 32
                AND     #$F8
                STA     T1
                LDA     RCH
                STA     T2
                ASL     T1
                ROL     T2
                ASL     T1
                ROL     T2
                LDA     T1
                ORA     T0
                STA     T1
                LDA     RDL            ; + (y / 8) x 1,280: x 5 in the high byte
                LSR     A
                LSR     A
                LSR     A
                STA     T0
                ASL     A
                ASL     A
                ADC     T0
                ADC     T2
                STA     T2
                LDA     RCL            ; the left pixel: the high 4 bits
                LSR     A
                LDA     #$F0
                BCC     MAPC
                LDA     #$0F
                BRA     MAPC

; The mono rows: T2:T1 = y x 160 + x / 8.
MAPM:           LDA     RDL            ; y x 32
                STA     T1
                STZ     T2
                LDX     #5
@a:             ASL     T1
                ROL     T2
                DEX
                BNE     @a
                LDA     T1
                STA     T3
                LDA     T2
                STA     T4
                ASL     T1             ; y x 128
                ROL     T2
                ASL     T1
                ROL     T2
                CLC
                LDA     T1
                ADC     T3
                STA     T1
                LDA     T2
                ADC     T4
                STA     T2
                LDA     RCH            ; + x / 8
                STA     T4
                LDA     RCL
                LSR     T4
                ROR     A
                LSR     T4
                ROR     A
                LSR     A
                CLC
                ADC     T1
                STA     T1
                BCC     :+
                INC     T2
:               LDA     RCL
                AND     #7
                TAX
                LDA     BIT7,X
; MAPC: CMASK A; GXP the offset T2:T1's place in W1, its bank mapped.
MAPC:           STA     CMASK
                LDA     T1
                STA     GXP
                LDA     T2
                AND     #$3F
                ORA     #>G_CPU
                STA     GXP + 1
                LDA     T2             ; its bank: the offset's top 2 bits
                ASL     A
                ROL     A
                ROL     A
                AND     #3
                ; (on into NEWBNK)

; NEWBNK: the page's bank A (0-2) in slot 1 and W1 (GX_VMAP), CBANK A;
; GXP kept. A bank off the page (past the screen: no pixel is drawn there)
; is only noted.
NEWBNK:         STA     CBANK
                CMP     #3
                BCS     @x
                STA     MAPBANK
                LSR     A              ; x 64 card pages (A 0-2)
                ROR     A
                ROR     A
                CLC
                ADC     GBASE
                TAY
                LDA     GBASE + 1
                ADC     #0
                TAX
                LDA     GXP            ; (GX_VMAP's GXP is the bank's first byte)
                STA     T5
                LDA     GXP + 1
                STA     T6
                TYA
                JSR     GX_VMAP
                LDA     T5
                STA     GXP
                LDA     T6
                STA     GXP + 1
@x:             RTS

; REMAP: after STOREC, the cursor's bank mapped if a move left another.
REMAP:          LDA     CBANK
                CMP     MAPBANK
                BNE     NEWBNK
                RTS

; --- SETC, READC, NSETCX ----------------------------------------------------------------------

SETC:           LDX     GMX
                JMP     (SETV - 2,X)
SETT:           LDA     (GXP)          ; the old pixel's bits replaced by CPAT's
                EOR     CPAT
                AND     CMASK
                EOR     (GXP)
                STA     (GXP)
                RTS
SETM:           LDA     (GXP)
                EOR     CPAT
                AND     CMASK
                EOR     (GXP)
                STA     (GXP)
                LDA     GXP + 1        ; its twin 80 bytes on, in this bank?
                CMP     #>(G_CPU + G_BANK - 80)
                BCC     @same
                BNE     @next
                LDA     GXP
                CMP     #<(G_CPU + G_BANK - 80)
                BCS     @next
@same:          LDY     #80
                LDA     (GXP),Y
                EOR     CPAT
                AND     CMASK
                EOR     (GXP),Y
                STA     (GXP),Y
                RTS
@next:          LDA     GXP            ; in the next: there, then back
                PHA
                LDA     GXP + 1
                PHA
                CLC
                LDA     GXP
                ADC     #80
                STA     GXP
                LDA     GXP + 1
                ADC     #0
                SEC
                SBC     #>G_BANK
                STA     GXP + 1
                LDA     CBANK
                INC     A
                JSR     NEWBNK
                JSR     SETT
                LDA     CBANK
                DEC     A
                JSR     NEWBNK
                PLA
                STA     GXP + 1
                PLA
                STA     GXP
                RTS

READC:          LDA     (GXP)
                AND     CMASK
                LDX     GMX
                CPX     #2 * GM_TILE
                BNE     @m
                BIT     CMASK          ; the left pixel: down 4 bits
                BPL     @x
                LSR     A
                LSR     A
                LSR     A
                LSR     A
@x:             RTS
@m:             CMP     #1             ; mono: 0 or 1
                LDA     #0
                ROL     A
                RTS

; NSETCX: a whole byte at once where the run covers it (CMASK at its first
; pixel and a byte's pixels left): SETC with every bit, then RIGHTC from its
; last pixel to the next byte's first.
NSETCX:         LDX     GMX
                LDA     CMASK
                CMP     NSFST - 2,X
                BNE     @one
                LDA     RBH
                BNE     @byte
                LDA     RBL
                CMP     NSPPB - 2,X
                BCC     @one
@byte:          LDA     #$FF
                STA     CMASK
                JSR     SETC
                LDX     GMX
                LDA     NSLST - 2,X
                STA     CMASK
                JSR     RIGHTC
                LDX     GMX
                SEC
                LDA     RBL
                SBC     NSPPB - 2,X
                STA     RBL
                BCS     @n
                DEC     RBH
                BRA     @n
@one:           JSR     SETC
                JSR     RIGHTC
                LDA     RBL
                BNE     :+
                DEC     RBH
:               DEC     RBL
@n:             LDA     RBL
                ORA     RBH
                BNE     NSETCX
                RTS

; --- FETCHC, STOREC ---------------------------------------------------------------------------

FETCHC:         LDX     #CSIZ - 1
@c:             LDA     CBANK,X
                STA     CSAV,X
                DEX
                BPL     @c
                LDA     GXP
                STA     CSAVP
                LDA     GXP + 1
                STA     CSAVP + 1
                RTS
STOREC:         LDX     #CSIZ - 1
@c:             LDA     CSAV,X
                STA     CBANK,X
                DEX
                BPL     @c
                LDA     CSAVP
                STA     GXP
                LDA     CSAVP + 1
                STA     GXP + 1
                RTS

; --- The moves --------------------------------------------------------------------------------

RIGHTC:         LDX     GMX
                JMP     (RIGHTV - 2,X)
LEFTC:          LDX     GMX
                JMP     (LEFTV - 2,X)
DOWNC:          LDX     GMX
                JMP     (DOWNV - 2,X)
UPC:            LDX     GMX
                JMP     (UPV - 2,X)

; The tiles. Right: the right pixel of the byte, else the next byte, the
; next tile's from the row's last (32 - 3 on).
RIGHTT:         LDA     CMASK
                EOR     #$FF
                STA     CMASK
                BPL     MVX            ; $0F: the same byte
                LDA     CXB
                CMP     #3
                BEQ     @t
                INC     CXB
                INC     GXP
                BNE     MVX
                INC     GXP + 1
                BRA     BANKUP
@t:             STZ     CXB
                LDA     #29
ADDP:           CLC
                ADC     GXP
                STA     GXP
                BCC     MVX
                INC     GXP + 1
; BANKUP: past W1's $7FFF: on into the next bank.
BANKUP:         LDA     GXP + 1
                BPL     MVX
                SEC
                SBC     #>G_BANK
                STA     GXP + 1
                LDA     CBANK
                INC     A
                JMP     NEWBNK
MVX:            RTS
; The mono rows: a bit right or left (the next or last byte past its end),
; two rows (160 bytes) down or up.
RIGHTM:         LSR     CMASK
                BCC     MVX
                ROR     CMASK          ; $80
                INC     GXP
                BNE     MVX
                INC     GXP + 1
                BRA     BANKUP
LEFTM:          ASL     CMASK
                BCC     MVX
                ROL     CMASK          ; $01
                LDA     #1
                BRA     SUBP
DOWNM:          LDA     #160
                BRA     ADDP
UPM:            LDA     #160
                BRA     SUBP

LEFTT:          LDA     CMASK
                EOR     #$FF
                STA     CMASK
                BMI     MVX            ; $F0: the same byte
                LDA     CXB
                BEQ     @t
                DEC     CXB
                LDA     #1
                BRA     SUBP
@t:             LDA     #3
                STA     CXB
                LDA     #29
SUBP:           STA     T7
                SEC
                LDA     GXP
                SBC     T7
                STA     GXP
                BCS     BANKDN
                DEC     GXP + 1
; BANKDN: below W1's $4000: on into the bank before.
BANKDN:         LDA     GXP + 1
                CMP     #>G_CPU
                BCS     MVX
                ADC     #>G_BANK
                STA     GXP + 1
                LDA     CBANK
                DEC     A
                JMP     NEWBNK
; Down: 4 bytes on in the tile, or the next tile row's (1,280 - 28 on).
DOWNT:          LDA     CY7
                CMP     #7
                BEQ     @r
                INC     CY7
                LDA     #4
                JMP     ADDP
@r:             STZ     CY7
                CLC
                LDA     GXP
                ADC     #<(1280 - 28)
                STA     GXP
                LDA     GXP + 1
                ADC     #>(1280 - 28)
                STA     GXP + 1
                JMP     BANKUP
UPT:            LDA     CY7
                BEQ     @r
                DEC     CY7
                LDA     #4
                BRA     SUBP
@r:             LDA     #7
                STA     CY7
                SEC
                LDA     GXP
                SBC     #<(1280 - 28)
                STA     GXP
                LDA     GXP + 1
                SBC     #>(1280 - 28)
                STA     GXP + 1
                BRA     BANKDN

; --- PAINT's (7e2, U1): SCANR, SCANL, TUPC, TDOWNC, PNTINI, PSTOR -------------------------

; The OEM's routines ADVGRP.ASM's PAINT calls and the release leaves to the
; OEM (OEM.H names them), the OO-1's, their contracts read from how PAINT
; uses them. PAINT keeps the cursor's pixel (PNX, PNY) beside C, so a queue
; entry is (x, y), re-made by PSTOR (MAPXYC), and the edges are known.
; READC's attribute is compared with the border's (PBORD, PNTINI's) and the
; fill's (ATRBYT, SETATR's).
;
;   SCANR   DX the skip count. Over border pixels rightwards while DX is
;           above 0, DX less one each; none left, or the screen's edge: BX
;           0. Else PCSX the run's first pixel's x, and each pixel from it
;           that isn't the border set to the fill, to the border or the
;           edge: BX the count, CL 1 if any pixel changed (else 0), DX what
;           was left, the cursor one past the run (x the last + 1 at the
;           edge: nothing is drawn there)
;   SCANL   from the pixel left of the cursor leftwards, the same without
;           the skip: BX the count, CL the flag; the cursor left at the
;           run's leftmost pixel (the cursor's own if none)
;   TUPC    a row up, TDOWNC down: carry set, and no move, at row 0 or 199
;   PNTINI  A the border's attribute: past the mode's last colour the last
;           (as SETATR); carry clear (every attribute is legal)
;   PSTOR   C made (RCL/RCH, PNY), PNX that x (MAPXYC); DX kept
SCANR:          STZ     RCL
@skip:          LDA     GMAXX          ; past the edge: none
                CMP     PNX
                LDA     GMAXX + 1
                SBC     PNX + 1
                BCC     @none
                JSR     READC
                CMP     PBORD
                BNE     @run
                LDA     RDL            ; a border pixel skipped
                BNE     :+
                DEC     RDH
:               DEC     RDL
                JSR     RIGHTC
                INCW    PNX
                LDA     RDL
                ORA     RDH
                BNE     @skip
@none:          STZ     RBL
                STZ     RBH
                RTS
@run:           LDX     PNX            ; (A the pixel, kept)
                STX     PCSX
                LDX     PNX + 1
                STX     PCSX + 1
                STZ     RBL
                STZ     RBH
                BRA     @in
@r:             LDA     GMAXX          ; the edge: the run's end
                CMP     PNX
                LDA     GMAXX + 1
                SBC     PNX + 1
                BCC     @x
                JSR     READC
@in:            CMP     PBORD
                BEQ     @x
                CMP     ATRBYT
                BEQ     :+
                LDA     #1             ; changed
                STA     RCL
                JSR     SETC
:               INCW    RBL
                JSR     RIGHTC
                INCW    PNX
                BRA     @r
@x:             RTS

SCANL:          STZ     RBL
                STZ     RBH
                STZ     RCL
@l:             LDA     PNX             ; x 0: the edge
                ORA     PNX + 1
                BEQ     @x
                JSR     LEFTC
                DECW    PNX
                JSR     READC
                CMP     PBORD
                BEQ     @back
                CMP     ATRBYT
                BEQ     :+
                LDA     #1
                STA     RCL
                JSR     SETC
:               INCW    RBL
                BRA     @l
@back:          JSR     RIGHTC         ; the border: back to the run's leftmost
                INCW    PNX
@x:             RTS

TUPC:           LDA     PNY
                BEQ     TEDGE
                DEC     PNY
                JSR     UPC
                CLC
                RTS
TDOWNC:         LDA     PNY
                CMP     #199
                BCS     TEDGE
                INC     PNY
                JSR     DOWNC
                CLC
                RTS
TEDGE:          SEC
                RTS

PNTINI:         LDX     GMODEV
                CMP     PIXMSK,X
                BCC     :+
                LDA     PIXMSK,X
:               STA     PBORD
                CLC
                RTS

PSTOR:          LDA     RCL
                STA     PNX
                LDA     RCH
                STA     PNX + 1
                LDA     RDH            ; DX kept (an entry's count)
                PHA
                LDA     RDL
                PHA
                LDA     PNY
                STA     RDL
                STZ     RDH
                JSR     MAPXYC
                PLA
                STA     RDL
                PLA
                STA     RDH
                RTS


.segment "RODATA"
; By mode, 1 and 2.
MAPV:           .word   MAPT, MAPM
SETV:           .word   SETT, SETM
RIGHTV:         .word   RIGHTT, RIGHTM
LEFTV:          .word   LEFTT, LEFTM
DOWNV:          .word   DOWNT, DOWNM
UPV:            .word   UPT, UPM
BIT7:           .byte   $80, $40, $20, $10, $08, $04, $02, $01
NSFST:          .word   $F0, $80       ; NSETCX's: a byte's first pixel,
NSLST:          .word   $0F, $01       ;   its last,
NSPPB:          .word   2, 8           ;   its pixels

.segment "BRAM"
CBANK:          .res    1              ; C, the cursor: its bank in the page
CMASK:          .res    1              ;   its pixel's bits
CXB:            .res    1              ;   the tiles: the byte in the tile's row
CY7:            .res    1              ;   the row in the tile
CSIZ            = * - CBANK            ;   (and GXP, its byte)
CSAV:           .res    CSIZ           ; FETCHC's
CSAVP:          .res    2
.assert * - CSAV = CSIZE, error, "CSIZE (p5/p5.inc): CSAV and CSAVP"
MAPBANK:        .res    1              ; the bank NEWBNK put in slot 1
PNX:             .res    2              ; 7e2: PAINT's pixel, the cursor's x
PNY:             .res    1              ;   and y
PCSX:           .res    2              ;   SCANR's run's first pixel's x (the source's CSAVEA)
PBORD:          .res    1              ;   the border's attribute (PNTINI)
