; The OEM's pixel routines, in block P5 (hw/oodos.md, "The plan, phase 7":
; 7e1, U2): the project's own. GENGRP.ASM calls them and the release leaves
; them to the OEM (OEM.H names them), so they are the OO-1's, written for
; the two frames P5's GMODE sets up (p5/p5.inc), and drawn by the CPU
; through aperture slot 1 in W1 (OOLIB's gxmem, the W1 contract):
;
;   SCALXY    CX, DX (x, y, signed) made a point of the screen: each clipped
;             to 0 and the mode's last (7e4: the viewport's, VWMX and VWMY);
;             carry set if both were on it already
;             (PSET, POINT, and BF's corners; a line's ends are clipped by
;             p5/gengrp.s's LINCLP)
;   MAPXYC    C, the graphics cursor, made the pixel at CX, DX (on the
;             screen), and the aperture put in W1 on its card bank. 7e4: CX,
;             DX the viewport's, its corner (VWOX, VWOY) added for the card,
;             CX and DX kept; MAPABS the same of a point of the screen
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
.import GX_VMAP, GX_W1BACK, TXBUF

; 7e3: GET and PUT's row state, and DEF SEG's, in zero page: the floating
; point's scratch from DBUFF, which nothing uses while a statement draws or
; a PEEK runs (CIRCLE's too, p5/advgrp.s's CZ: never at once).
GPACT           = DBUFF                ; the action (PGINIT)
GOU             = DBUFF + 3            ;   a pixel's bits (3, 1)
GOUB            = DBUFF + 4            ;   a byte's complement ($33, $FF)
GPPN            = DBUFF + 5            ;   PIXSIZ
GPBYT           = DBUFF + 6            ;   the array's bytes a row
GPNB            = DBUFF + 7            ;   the byte path's CGA bytes a row
GPN0            = DBUFF + 8            ;   the pixels after them a row
GPN             = DBUFF + 10           ;   the pixels left
GPI             = DBUFF + 12           ;   the buffer's place
GPBB            = DBUFF + 13           ;   the byte being packed or unpacked
GPBN            = DBUFF + 14           ;   its bits so far, or left
GPV             = DBUFF + 15           ;   a pixel's bits
GOLD            = DBUFF + 16           ;   the pixel's old value (READC)
GOT             = DBUFF + 17           ;   TNIB's and GBRD's scratch
GOT2            = DBUFF + 18
GDN             = DBUFF + 19           ;   GBRD's and GBWR's bytes left
GTW             = DBUFF + 20           ;   GBWR: SCREEN 2's twin row's pointer
GTWD            = DBUFF + 22           ;   nonzero once GBWR has written the twin
.assert GTWD < FBPTR, error, "GET and PUT's zero page past the floating point's scratch"

.code

; --- SCALXY -----------------------------------------------------------------------------------

SCALXY:         LDY     #0             ; Y: 1 once a coordinate was clipped
                LDA     RCH
                BPL     :+
                STZ     RCL            ; x < 0
                STZ     RCH
                INY
                BRA     @y
:               LDA     VWMX           ; x > the last
                CMP     RCL
                LDA     VWMX + 1
                SBC     RCH
                BCS     @y
                LDA     VWMX
                STA     RCL
                LDA     VWMX + 1
                STA     RCH
                INY
@y:             LDA     RDH
                BPL     :+
                STZ     RDL
                STZ     RDH
                INY
                BRA     @f
:               LDA     VWMY           ; y > the last (7e5: a word)
                CMP     RDL
                LDA     VWMY + 1
                SBC     RDH
                BCS     @f
                LDA     VWMY
                STA     RDL
                LDA     VWMY + 1
                STA     RDH
                INY
@f:             CPY     #1             ; C: Y 0, on the screen
                ROL     A
                EOR     #1
                LSR     A
                RTS

; --- MAPXYC -----------------------------------------------------------------------------------

MAPXYC:         LDA     RCL            ; 7e4: the viewport's corner added, CX and DX kept
                PHA
                CLC
                ADC     VWOX
                STA     RCL
                LDA     RCH
                PHA
                ADC     VWOX + 1
                STA     RCH
                LDA     RDL
                PHA
                CLC
                ADC     VWOY
                STA     RDL
                LDA     RDH            ; (7e5: y a word)
                PHA
                ADC     VWOY + 1
                STA     RDH
                JSR     MAPABS
                PLA
                STA     RDH
                PLA
                STA     RDL
                PLA
                STA     RCH
                PLA
                STA     RCL
                RTS
MAPABS:         STZ     T3             ; 7e5: the offset's third byte (MAPC's)
                LDX     GMX
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
                JSR     MSHL
                LDX     #2             ; y x 128
                ; (on into MROW)
; MROW (7e5): T2:T1 + T2:T1 x 2^X (a row's offset, its two parts), then MXB.
MROW:           LDA     T1
                STA     T4
                LDA     T2
                STA     T5
                JSR     MSHL
                CLC
                LDA     T1
                ADC     T4
                STA     T1
                LDA     T2
                ADC     T5
                STA     T2
; MXB (7e5): + x / 8, CMASK x's bit (bit 7 the leftmost), on into MAPC.
MXB:            LDA     RCH            ; + x / 8
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
; MAPC: CMASK A; GXP the offset T2:T1's place in W1, its bank mapped. 7e5:
; the offset three bytes, T3 the third: its bank its bits 14 and up.
MAPC:           STA     CMASK
                LDA     T1
                STA     GXP
                LDA     T2
                AND     #$3F
                ORA     #>G_CPU
                STA     GXP + 1
                LDA     T2             ; its bank: T3 x 4 and T2's top 2 bits
                ASL     A
                ROL     T3
                ASL     A
                ROL     T3
                LDA     T3
                ; (on into NEWBNK)

; NEWBNK: the page's bank A (0-2; 7e5: to the mode's last, GNBNK's, 0-7) in
; slot 1 and W1 (GX_VMAP), CBANK A; GXP kept. A bank off the page (past the
; screen: no pixel is drawn there) is only noted.
NEWBNK:         STA     CBANK
                LDX     GMODEV
                CMP     GNBNK - 1,X
                BCS     @x
                STA     MAPBANK
                LSR     A              ; x 64 card pages: (A AND 3) x 64, C bit 2
                ROR     A
                ROR     A
                LDX     #0
                BCC     :+
                INX                    ;   (7e5: x 256 pages for banks 4-7)
:               CLC
                ADC     GBASE
                TAY
                TXA
                ADC     GBASE + 1
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

; MSHL (7e5): T2:T1 shifted left X times (X at least 1).
MSHL:           ASL     T1
                ROL     T2
                DEX
                BNE     MSHL
                RTS

; The OO-1's (7e5): SCREEN 10, a byte a pixel, 256 a row: the offset y x
; 256 + x; SCREEN 11, a nibble a pixel, the left high, 256 bytes a row: y x
; 256 + x / 2; SCREEN 12, a bit, 80 bytes a row: y x 80 + x / 8 (y x 16 +
; y x 64).
MAP10:          LDA     RCL
                STA     T1
                LDA     #$FF
MAPY:           LDX     RDL            ; T3:T2 y
                STX     T2
                LDX     RDH
                STX     T3
                BRA     MAPC
MAP11:          LDA     RCH
                LSR     A
                LDA     RCL
                ROR     A
                STA     T1
                LDA     #$F0           ; x even: the high nibble
                BCC     MAPY
                LDA     #$0F
                BRA     MAPY
MAP12:          LDA     RDL            ; y x 16
                STA     T1
                LDA     RDH
                STA     T2
                LDX     #4
                JSR     MSHL
                LDX     #2             ; + y x 64
                JMP     MROW

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

; READC: 7e5, by the mode's bits a pixel (PIXBIT): a nibble's (SCREEN 1's
; tiles, SCREEN 11), the left down 4 bits; a byte's (SCREEN 10) itself; a
; bit's 0 or 1.
READC:          LDA     (GXP)
                AND     CMASK
                LDX     GMODEV
                LDY     PIXBIT,X
                DEY
                BEQ     @m             ; a bit
                BIT     CMASK          ; the left pixel, or the byte: bit 7
                BPL     @x
                CPY     #7
                BEQ     @x             ; the byte
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
NSETCX:         LDX     GMODEV
                LDA     CMASK
                CMP     NSFST - 1,X
                BNE     @one
                LDA     RBH
                BNE     @byte
                LDA     RBL
                CMP     NSPPB - 1,X
                BCC     @one
@byte:          LDA     #$FF
                STA     CMASK
                JSR     SETC
                LDX     GMODEV
                LDA     NSLST - 1,X
                STA     CMASK
                JSR     RIGHTC
                LDX     GMODEV
                SEC
                LDA     RBL
                SBC     NSPPB - 1,X
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
; 7e5: SCREEN 12's rows, 80 bytes; SCREEN 11's nibbles (a byte for two);
; SCREEN 10's bytes; both 256 bytes a row, GXP's high byte.
DOWN12:         LDA     #80
                JMP     ADDP
UP12:           LDA     #80
                JMP     SUBP
RIGHT11:        LDA     CMASK
                EOR     #$FF
                STA     CMASK
                BPL     MV2            ; $0F: the same byte
RIGHT10:        INC     GXP
                BNE     MV2
DOWN256:        INC     GXP + 1
                JMP     BANKUP
LEFT11:         LDA     CMASK
                EOR     #$FF
                STA     CMASK
                BMI     MV2            ; $F0: the same byte
LEFT10:         LDA     #1
                JMP     SUBP
UP256:          DEC     GXP + 1
                JMP     BANKDN
MV2:            RTS

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
;           (7e4: the viewport's last, VWMY; 7e5: PNY a word)
;   PNTINI  A the border's attribute: past the mode's last colour the last
;           (as SETATR); carry clear (every attribute is legal)
;   PSTOR   C made (RCL/RCH, PNY), PNX that x (MAPXYC); DX kept
SCANR:          STZ     RCL
@skip:          LDA     VWMX           ; past the edge: none (7e4: the viewport's)
                CMP     PNX
                LDA     VWMX + 1
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
                LDX     TBGF           ; 7e4: the run all the background's, so far
                STX     TALLB
                STZ     RBL
                STZ     RBH
                BRA     @in
@r:             LDA     VWMX           ; the edge: the run's end
                CMP     PNX
                LDA     VWMX + 1
                SBC     PNX + 1
                BCC     @x
                JSR     READC
@in:            CMP     PBORD
                BEQ     @x
                JSR     PCHG
                INCW    RBL
                JSR     RIGHTC
                INCW    PNX
                BRA     @r
@x:             JMP     TEND

SCANL:          STZ     RBL
                STZ     RBH
                STZ     RCL
                LDX     TBGF           ; 7e4: (SCANR's)
                STX     TALLB
@l:             LDA     PNX             ; x 0: the edge
                ORA     PNX + 1
                BEQ     @x
                JSR     LEFTC
                DECW    PNX
                JSR     READC
                CMP     PBORD
                BEQ     @back
                JSR     PCHG
                INCW    RBL
                BRA     @l
@back:          JSR     RIGHTC         ; the border: back to the run's leftmost
                INCW    PNX
@x:             JMP     TEND

TUPC:           LDA     PNY
                ORA     PNY + 1
                BEQ     TEDGE
                DECW    PNY
                JSR     UPC
                JSR     TROWS          ; 7e4: the tile's row
                CLC
                RTS
TDOWNC:         LDA     PNY
                CMP     VWMY
                LDA     PNY + 1
                SBC     VWMY + 1
                BCS     TEDGE
                INCW    PNY
                JSR     DOWNC
                JSR     TROWS
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
                LDA     PNY + 1
                STA     RDH
                JSR     MAPXYC
                PLA
                STA     RDL
                PLA
                STA     RDH
                ; (on into TROWS: 7e4, the tile's row)

; --- PAINT's tiles (7e4) --------------------------------------------------------------------

; The OO-1's (the IBM manual's pp. 236-245; the source has no tiles). A
; string paint is a tile of 1-64 bytes, copied by PAINTP (p5/advgrp.s) to
; TXBUF before the aperture is mapped (TXBUF is below $4000: readable with
; the aperture in W1; the home block's PAINT stub emptied it, TXSYNC). Row y
; takes byte y MOD TLEN; each byte's pixels across from the high bits (2
; bits a pixel in SCREEN 1, 1 in SCREEN 2), the pixel at x MOD 4 or 8 of
; the viewport's own x (the owner's answer 7). The background (TBG, a
; string's first byte) lets the fill pass through rows already painted with
; it: a run that changed nothing still counts as painted when its old pixels
; are all the background's row pattern (TEND).
;
;   TROWS   PNY's row of the tile split into TCOL (a pixel's colour at each
;           place); nothing if no tile
;   TSET    the tile's set-up, after the parse: TBITS, TPN, TPM, the
;           background's row (TBGP)
;   PCHG    A a run's pixel's old value (not the border): set to the fill's
;           (ATRBYT's, or the tile's at PNX, CPAT its pattern), RCL 1 if it
;           changed
;   TEND    a run's end: with a background, RCL 1 if every pixel of the run
;           was the background's
TROWS:          BIT     TILEF
                BPL     TRX
                LDA     PNY            ; y MOD TLEN (7e5: y a word)
                LDX     PNY + 1
@m:             SEC
@n:             SBC     TLEN
                BCS     @n
                DEX
                BPL     @m
                ADC     TLEN           ; (C clear)
                TAY
                LDA     TXBUF,Y
                LDY     #0
                ; (on into TSPLIT)
; TSPLIT: the tile byte A's pixels' colours to TCOL + Y on.
TSPLIT:         STA     TROW
                TYA
                CLC
                ADC     TPN
                STA     TTOP
@p:             LDA     #0
                LDX     TBITS
@b:             ASL     TROW
                ROL     A
                DEX
                BNE     @b
                STA     TCOL,Y
                INY
                CPY     TTOP
                BNE     @p
TRX:            RTS

TSET:           JSR     PIXSIZ         ; 2, 1: a pixel's bits (7e5: 8, 4, 1 too)
                STA     TBITS
                LDA     TPNT - 1,X     ; the pixels a byte: 4, 8 (1, 2, 8); less one
                STA     TPN
                DEC     A
                STA     TPM
                LDA     TBG
                LDY     #TBGP - TCOL
                BRA     TSPLIT

PCHG:           BIT     TILEF
                BMI     @t
                CMP     ATRBYT
                BEQ     @x
@s:             LDA     #1             ; changed
                STA     RCL
                JMP     SETC
@t:             TAY                    ; the tile's pixel here
                LDA     PNX
                AND     TPM
                TAX
                TYA
                CMP     TBGP,X
                BEQ     :+
                STZ     TALLB          ; not the background's
:               CMP     TCOL,X
                BEQ     @x
                LDA     TCOL,X         ; its pattern, as SETATR's
                JSR     CPATOF
                STA     CPAT
                BRA     @s
@x:             RTS

TEND:           BIT     TILEF
                BPL     @x
                LDA     TALLB
                BEQ     @x
                LDA     RBL
                ORA     RBH
                BEQ     @x
                LDA     #1
                STA     RCL
@x:             RTS

; --- GET and PUT's rows, DEF SEG's bytes (7e3, U1): PGINIT, NREAD, NWRITE ---------------

; The OEM's routines ADVGRP.ASM's GET and PUT call and the release leaves to
; the OEM (OEM.H names PGINIT, NREAD and NWRITE), the OO-1's: their contract
; read from PGLOOP's use (a row from the cursor rightwards, the cursor left
; at the row's first pixel for PGLOOP's DOWNC), their format the IBM
; manual's (pp. 116-117): each pixel PIXSIZ bits, high bits first, the row
; left-justified on a byte, its last byte's spare bits 0, the bytes in
; screen order. While the aperture is in W1 the array may not be readable
; (any of it may lie in $4000-$7FFF), so each row passes through TXBUF
; (LOWRAM, below $4000, which the home block's stubs emptied by TXSYNC, and
; nothing in a GET or PUT prints): NREAD reads the row into TXBUF, puts block
; 1 back and copies it to the array; NWRITE copies it from the array with
; block 1 in W1, then draws it from TXBUF. Each maps the cursor's bank again
; after (NEWBNK). Nothing here can end in an error.
;
;   PGINIT  RBL/RBH the bits a row, RSI the data's address, A the action (0
;           OR, 1 AND, 2 PRESET, 3 PSET, 4 XOR: GFUNTB's order), the cursor
;           at the row's first pixel (MAPXYC): the bytes a row, the pixels
;           (the bits over PIXSIZ, the source's: a part pixel isn't drawn),
;           the byte path's bytes, CPAT kept (GPCPAT, for the caller to put
;           back)
;   NREAD   GET's row: the cursor's row into the array at RSI, RSI past it
;   NWRITE  PUT's: the array's row at RSI onto the screen by the action,
;           RSI past it
;   GROWR GROWW   the row to or from (PTR) alone (DEF SEG's pieces through
;           TXBUF, and PEEK's and POKE's byte, DSVAL)
;   GPSETN  GROWR and GROWW set for A whole CGA bytes by PSET (DEF SEG)
;
; A pixel: READC, the action with the array's bits (GOPP), SETC with the new
; value's pattern in CPAT, RIGHTC. The byte paths (the owner's answer 4),
; where the row starts on a CGA byte (SCREEN 2 x MOD 8 = 0, SCREEN 1 x MOD
; 4 = 0): a CGA byte at a time (GBRD, GBWR), SCREEN 2's the card byte itself,
; its twin row written after in a second pass from the results, SCREEN 1's
; two tile bytes of two nibbles each (T16, TNIB), the action on them bit by
; bit (GOPB); the rest of the row by the pixels.
PGINIT:         STA     GPACT
                ASL     A
                TAX
                LDA     GOPT,X
                STA     GOPV
                LDA     GOPT + 1,X
                STA     GOPV + 1
                LDA     CPAT
                STA     GPCPAT
                LDX     GMODEV
                LDA     PIXMSK,X       ; a pixel's bits: 3, 1
                STA     GOU
                LDA     GOUBT - 1,X    ; a byte's: $33 (two nibbles), $FF
                STA     GOUB
                JSR     PIXSIZ
                STA     GPPN
                LDA     RBL            ; 7e5: the row's bits, for its pieces (PGPCE)
                STA     GPRB
                LDA     RBH
                STA     GPRB + 1
                RTS

; PGPCE (7e5): the row's next piece, GPRL its bits left: at most 1,024 bits
; (TXBUF's 128 bytes; a row of SCREEN 10 or 11 is up to 2,048), GPRL less
; them. Its bytes, its whole CGA bytes if it starts on one and its pixels
; after (7e3's PGINIT's): every piece but the last is whole bytes, so the
; next starts on a byte of the array.
PGPCE:          LDA     GPRL + 1
                CMP     #>1024
                BCC     @all
                STZ     RBL
                LDA     #>1024
                STA     RBH
                BRA     @s
@all:           LDA     GPRL
                STA     RBL
                LDA     GPRL + 1
                STA     RBH
@s:             SEC
                LDA     GPRL
                SBC     RBL
                STA     GPRL
                LDA     GPRL + 1
                SBC     RBH
                STA     GPRL + 1
                LDA     RBL            ; T2 the bits past the whole bytes, T0 those
                AND     #7             ; (a piece's at most 1,024 bits)
                STA     T2
                LDA     RBH
                STA     T1
                LDA     RBL
                LDX     #3
@b:             LSR     T1
                ROR     A
                DEX
                BNE     @b
                STA     T0
                LDX     T2             ; the bytes a row: and one for a part
                CPX     #1
                ADC     #0
                STA     GPBYT
                LDA     RBL            ; the pixels, unaligned: all of them
                STA     GPN0
                LDA     RBH
                STA     GPN0 + 1
; GPNB: the whole CGA bytes, if the row starts on one (8 bits each, either
; mode); GPN0 the pixels after.
                STZ     GPNB
                LDX     GMODEV
                LDA     CMASK
                CMP     NSFST - 1,X
                BNE     @p
                CPX     #GM_TILE
                BNE     :+
                LDA     CXB            ; SCREEN 1: a tile byte pair's first
                LSR     A
                BCS     @p
:               LDA     T0
                STA     GPNB
                LDA     T2
                STA     GPN0
                STZ     GPN0 + 1
@p:             LDA     GPPN           ; bits over PIXSIZ: the pixels (a part pixel left;
@d:             LSR     A              ; 7e5: PIXSIZ 1, 2, 4 or 8)
                BEQ     :+
                LSR     GPN0 + 1
                ROR     GPN0
                BRA     @d
:               RTS

; GPSETN: A whole CGA bytes, by PSET (DEF SEG's page).
GPSETN:         STA     GPNB
                STZ     GPN0
                STZ     GPN0 + 1
                LDA     #3
                STA     GPACT
                LDA     #<GBPS
                STA     GOPV
                LDA     #>GBPS
                STA     GOPV + 1
                RTS

; NREAD, NWRITE: a row through TXBUF (above); then RSI past it (GADV) and
; the cursor's bank mapped again. 7e5: in pieces (PGPCE), the cursor
; carried on from one to the next and put back at the row's first pixel
; after the last (GBACK).
NREAD:          JSR     FETCHC
                JSR     GPROW
@p:             JSR     PGPCE
                JSR     GPTX
                JSR     GROWR1
                JSR     GX_W1BACK
                LDY     #0
@c:             LDA     TXBUF,Y
                STA     (RSI),Y
                INY
                CPY     GPBYT
                BNE     @c
                JSR     GADV
                JSR     GPMORE
                BNE     @p
                JMP     GBACK
NWRITE:         JSR     FETCHC
                JSR     GPROW
@p:             JSR     PGPCE
                JSR     GX_W1BACK
                LDY     #0
@c:             LDA     (RSI),Y
                STA     TXBUF,Y
                INY
                CPY     GPBYT
                BNE     @c
                JSR     GADV
                JSR     GPTX
                JSR     GROWW1
                JSR     GPMORE
                BNE     @p
                JMP     GBACK
; GPROW (7e5): GPRL the row's bits; GPMORE: Z if none are left.
GPROW:          LDA     GPRB
                STA     GPRL
                LDA     GPRB + 1
                STA     GPRL + 1
                RTS
GPMORE:         LDA     GPRL
                ORA     GPRL + 1
                RTS
GADV:           CLC
                LDA     RSI
                ADC     GPBYT
                STA     RSI
                BCC     :+
                INC     RSI + 1
:               LDA     CBANK
                JMP     NEWBNK
GPTX:           LDA     #<TXBUF
                STA     PTR
                LDA     #>TXBUF
                STA     PTR + 1
                RTS

; GROWR: the row into (PTR): GPNB CGA bytes, then GPN0 pixels packed. The
; cursor back at the row's first pixel (GBACK). 7e5: GROWR1 the same, the
; cursor left past the row (NREAD's pieces).
GROWR:          JSR     FETCHC
                JSR     GROWR1
GBACK:          JSR     STOREC
                JMP     REMAP
GROWR1:         LDY     #0
                LDX     GPNB
                BEQ     :+
                JSR     GBRD
:               JSR     GPPX
@px:            LDA     GPN
                ORA     GPN + 1
                BEQ     @end
                JSR     READC
                AND     GOU
                LDX     GPPN
@s:             ASL     GPBB
                DEX
                BNE     @s
                ORA     GPBB
                STA     GPBB
                CLC
                LDA     GPBN
                ADC     GPPN
                STA     GPBN
                CMP     #8
                BCC     :+
                JSR     GPSTB
:               JSR     RIGHTC
                DECW    GPN
                BRA     @px
@end:           LDA     GPBN           ; a part byte: left-justified, its spare bits 0
                BEQ     GPRTS
@al:            ASL     GPBB
                INC     GPBN
                LDA     GPBN
                CMP     #8
                BCC     @al
GPSTB:          LDA     GPBB
                LDY     GPI
                STA     (PTR),Y
                INC     GPI
                STZ     GPBN
GPRTS:          RTS
; GPPX: the pixel loop's start, Y the buffer's place.
GPPX:           STY     GPI
                STZ     GPBN
                LDA     GPN0
                STA     GPN
                LDA     GPN0 + 1
                STA     GPN + 1
                RTS

; GROWW: the row from (PTR), by the action: GPNB CGA bytes, then GPN0 pixels;
; SCREEN 2's byte path's twin row last. The cursor back (GBACK). 7e5:
; GROWW1 the same, the cursor left past the row but for SCREEN 2's twin.
GROWW:          JSR     FETCHC
                JSR     GROWW1
                JMP     GBACK
GROWW1:         STZ     GTWD
                LDY     #0
                LDX     GPNB
                BEQ     :+
                JSR     GBWR
:               JSR     GPPX
@px:            LDA     GPN
                ORA     GPN + 1
                BEQ     @end
                LDA     GPBN           ; the array's next bits
                BNE     :+
                LDY     GPI
                LDA     (PTR),Y
                STA     GPBB
                INC     GPI
                LDA     #8
                STA     GPBN
:               LDA     #0
                LDX     GPPN
@g:             ASL     GPBB
                ROL     A
                DEX
                BNE     @g
                STA     GPV
                SEC
                LDA     GPBN
                SBC     GPPN
                STA     GPBN
                JSR     READC
                STA     GOLD
                LDA     GPV
                JSR     GOPP
                AND     GOU
                JSR     CPATOF         ; its pattern (SETATR's)
                STA     CPAT
                JSR     SETC
                JSR     RIGHTC
                DECW    GPN
                BRA     @px
@end:           LDX     GMX            ; SCREEN 2's byte path: the twin row, 80 on,
                CPX     #2 * GM_MONO   ; unless GBWR wrote it
                BNE     @b
                LDA     GTWD
                BNE     @b
                LDA     GPNB
                BNE     :+
@b:             RTS
:
                JSR     STOREC
                JSR     REMAP
                LDA     #80
                JSR     ADDP
                LDX     GPNB
                LDY     #0
@w:             LDA     (PTR),Y
                STA     (GXP)
                INY
                INC     GXP
                BNE     :+
                JSR     GNXHI
:               DEX
                BNE     @w
                RTS

; GBRD: X CGA bytes from the cursor into (PTR),Y on; Y and the cursor past
; them.
GBRD:           STX     GDN
                LDX     GMX
                CPX     #2 * GM_TILE
                BEQ     @t
                CLC                    ; SCREEN 2: by Y at once (Y 0, the row's start) where
                LDA     GXP            ; they don't pass W1's end
                ADC     GDN
                LDA     GXP + 1
                ADC     #0
                BMI     @m
                LDX     GDN
@f:             LDA     (GXP),Y
                STA     (PTR),Y
                INY
                DEX
                BNE     @f
                BRA     GSKIP
@m:             LDA     (GXP)
                STA     (PTR),Y
                INY
                INC     GXP
                BNE     :+
                JSR     GNXHI
:               DEC     GDN
                BNE     @m
                RTS
@t:             LDA     (GXP)          ; SCREEN 1: two tile bytes, the left pixel high
                JSR     TNIB
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     GOT
                INC     GXP            ; (never wraps: the pair's first byte is 0 or 2 mod 4)
                LDA     (GXP)
                JSR     TNIB
                ORA     GOT
                STA     (PTR),Y
                INY
                JSR     TSTEP
                DEC     GDN
                BNE     @t
                RTS
; GSKIP: GXP past the Y bytes done by Y (inside W1, no bank crossed).
GSKIP:          TYA
                CLC
                ADC     GXP
                STA     GXP
                BCC     :+
                INC     GXP + 1
:               RTS
; TNIB: a tile byte's two pixels (nibbles, 0-3) as a CGA nibble, the left
; pixel in bits 3-2.
TNIB:           TAX
                AND     #$30
                LSR     A
                LSR     A
                STA     GOT2
                TXA
                AND     #$03
                ORA     GOT2
                RTS

; GBWR: X CGA bytes from (PTR),Y on to the cursor by the action, SCREEN 2's
; results back in (PTR) for the twin; Y and the cursor past them.
GBWR:           STX     GDN
                LDA     GPACT
                LDX     GMODEV
                CPX     #GM_TILE
                BEQ     @t
                CPX     #GM_MONO       ; 7e5: the twin's PSET at once SCREEN 2's alone; the
                BNE     @m             ; OO-1's modes have none, each action byte by byte
                CMP     #3
                BNE     @m
                CLC                    ; PSET: the row and its twin at once by Y (Y 0, the
                LDA     GXP            ; row's start) where they don't pass W1's end
                ADC     #80
                STA     GTW
                LDA     GXP + 1
                ADC     #0
                STA     GTW + 1
                CLC
                LDA     GTW
                ADC     GDN
                LDA     GTW + 1
                ADC     #0
                BMI     @m             ; (past it: as any action, GBPS's, the twin after)
                LDX     GDN
@f:             LDA     (PTR),Y
                STA     (GXP),Y
                STA     (GTW),Y
                INY
                DEX
                BNE     @f
                INC     GTWD           ; the twin written
                BRA     GSKIP
@m:             LDA     (PTR),Y
                JSR     GOPB
                STA     (GXP)
                STA     (PTR),Y
                INY
                INC     GXP
                BNE     :+
                JSR     GNXHI
:               DEC     GDN
                BNE     @m
                RTS
@t:             CMP     #3             ; SCREEN 1: the high nibble to the pair's first
                BNE     @to
@tp:            LDA     (PTR),Y        ; (PSET: no action)
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                TAX
                LDA     T16,X
                STA     (GXP)
                INC     GXP
                LDA     (PTR),Y
                AND     #$0F
                TAX
                LDA     T16,X
                STA     (GXP)
                INY
                LDA     CXB            ; (TSTEP, inline)
                EOR     #2
                STA     CXB
                BEQ     :+
                INC     GXP
                DEC     GDN
                BNE     @tp
                RTS
:               JSR     TSTEP2
                DEC     GDN
                BNE     @tp
                RTS
@to:            LDA     (PTR),Y
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                TAX
                LDA     T16,X
                JSR     GOPB
                STA     (GXP)
                INC     GXP
                LDA     (PTR),Y
                AND     #$0F
                TAX
                LDA     T16,X
                JSR     GOPB
                STA     (GXP)
                INY
                JSR     TSTEP
                DEC     GDN
                BNE     @to
                RTS

; TSTEP: SCREEN 1, from a pair's second tile byte to the next pair: the
; tile's other half (CXB 0 to 2), or the next tile's first (2 to 0, 29 on).
TSTEP:          LDA     CXB
                EOR     #2
                STA     CXB
                BEQ     TSTEP2
                INC     GXP            ; (1 mod 4: never wraps)
                RTS
TSTEP2:         LDA     #29
                CLC
                ADC     GXP
                STA     GXP
                BCC     @x
                PHY
                INC     GXP + 1
                JSR     BANKUP
                PLY
@x:             RTS
; GNXHI: GXP's high byte on, into the next bank past W1's end; X and Y kept.
GNXHI:          PHX
                PHY
                INC     GXP + 1
                JSR     BANKUP
                PLY
                PLX
                RTS

; GOPB: A the array's bits (a card byte, or a tile byte's two nibbles) by
; the action with (GXP)'s. GOPP: A the array's pixel by the action with
; GOLD's; the caller masks it (GOU).
GOPB:           JMP     (GOPV)
GBOR:           ORA     (GXP)
                RTS
GBAND:          AND     (GXP)
                RTS
GBPRE:          EOR     GOUB           ; the complement: 3 - v, 1 - v in each pixel
GBPS:           RTS
GBXOR:          EOR     (GXP)
                RTS
GOPP:           LDX     GPACT
                BEQ     @or
                DEX
                BEQ     @and
                DEX
                BEQ     @pre
                DEX
                BEQ     @x
                EOR     GOLD           ; XOR
                RTS
@or:            ORA     GOLD
                RTS
@and:           AND     GOLD
                RTS
@pre:           EOR     #$FF
@x:             RTS


.segment "RODATA"
; By mode, 1 and 2.
; 7e5: and 3-5, SCREEN 10, 11, 12 (one row a pixel: SETT, no twin).
MAPV:           .word   MAPT, MAPM, MAP10, MAP11, MAP12
SETV:           .word   SETT, SETM, SETT, SETT, SETT
RIGHTV:         .word   RIGHTT, RIGHTM, RIGHT10, RIGHT11, RIGHTM
LEFTV:          .word   LEFTT, LEFTM, LEFT10, LEFT11, LEFTM
DOWNV:          .word   DOWNT, DOWNM, DOWN256, DOWN256, DOWN12
UPV:            .word   UPT, UPM, UP256, UP256, UP12
BIT7:           .byte   $80, $40, $20, $10, $08, $04, $02, $01
NSFST:          .byte   $F0, $80, $FF, $F0, $80 ; NSETCX's: a byte's first pixel (7e5:
NSLST:          .byte   $0F, $01, $FF, $0F, $01 ;   bytes, by GMODEV from 1), its last,
NSPPB:          .byte   2, 8, 1, 2, 8  ;   its pixels
GNBNK:          .byte   3, 3, 4, 8, 3  ; 7e5: the page's banks (NEWBNK)
TPNT:           .byte   4, 8, 1, 2, 8  ; 7e5: a tile byte's pixels (TSET: 8 / the bits)
GOUBT:          .byte   $33, $FF, $FF, $FF, $FF ; 7e3: GBPRE's complement, by mode (1-5)
GOPT:           .word   GBOR, GBAND, GBPRE, GBPS, GBXOR ; 7e3: GOPB's, by action
; 7e3: T16, a CGA nibble (two pixels, the left in bits 3-2) as a tile byte
; (the left pixel's nibble high).
T16:            .byte   $00, $01, $02, $03, $10, $11, $12, $13
                .byte   $20, $21, $22, $23, $30, $31, $32, $33

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
PNY:            .res    2              ;   and y (7e5: a word)
PCSX:           .res    2              ;   SCANR's run's first pixel's x (the source's CSAVEA)
PBORD:          .res    1              ;   the border's attribute (PNTINI)
GPCPAT:         .res    1              ; 7e3: CPAT, kept for PGINIT's caller
GOPV:           .res    2              ;   GOPB's routine, by the action (not zero page: rule 2)
GPRB:           .res    2              ; 7e5: a row's bits (PGINIT)
GPRL:           .res    2              ;   and those left for its pieces (PGPCE)
TILEF:          .res    1              ; 7e4: bit 7, PAINT's paint a tile (TXBUF)
TLEN:           .res    1              ;   its bytes
TBG:            .res    1              ;   the background's byte
TBGF:           .res    1              ;   1: a background given
TALLB:          .res    1              ;   a run's pixels all the background's so far
TBITS:          .res    1              ;   a pixel's bits
TPN:            .res    1              ;   the pixels a byte
TPM:            .res    1              ;   less one
TROW:           .res    1              ;   TSPLIT's byte
TTOP:           .res    1              ;   and its end
TCOL:           .res    8              ;   the row's pixels' colours, by x MOD TPN
TBGP:           .res    8              ;   the background's
