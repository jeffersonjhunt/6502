; Partly derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P6's OEM layer (hw/oodos.md, "The plan, phase 7": 7d1, the text
; screen; the owner's answer 3 of 2026-11-18): the text routines SCNDRV.ASM
; calls, which the release leaves to the OEM, written for the OO-1's video
; card, so the project's own; and the console's way to them (PUTC: GIOSCN.ASM's
; SCNSOT and SCNOL1, Microsoft's, translated).
;
;   SCNIPL    start-up, from P2's GWINIT in screen mode: A/X the text pages'
;             first card page (P5's GXSTRT allocated them); the cache's
;             physical address (function 73), 80 x 25, the pages cleared,
;             the layer shown (P5's TXMODE)
;   TXRUN     OUTDO's TXBUF (the console's bytes, LOWRAM) through PUTC
;   LCPYP     LCOPY (GWSTS.ASM's LCPY, the OEM's; 7d2, the owner's answer
;             9): the visual page's text to LST:
;
; GW-BASIC's text is layer 0, 80 or 40 cells a row, 30 rows (25 used, centred,
; or 30: WIDTH n,30), a character and its attribute a cell: foreground bits
; 3-0, background 6-4, blink 7 (CONFIG's BLINK), the PC's own attribute. The
; pages (SCREEN 0's: 4 at 80 columns, 8 at 40) are the program's card memory
; from PGBASE, PGSIZE card pages each. P6 never moves W1: it reaches the card
; only by function 83's port operations (DM_VREAD, DM_VWRITE, DM_VCOPY), and
; keeps one row of the active page in its RAM (CACHE): loaded on first touch,
; the span written back by one DM_VWRITE (FLUSH) before another row, a
; scroll, a clear, and every return from P6 (P6OUT), so nothing outside P6
; ever sees a stale cell; it is kept across returns (only P6 writes the
; text), and forgotten by a scroll, a clear and a change of page.
;
; The cursor (the owner's answer 9): the cell's attribute made inverse and
; blinking while GW-BASIC waits for a key in the editor (CSRTYP 1 or 2);
; LOCATE's user cursor (3) isn't shown.

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry SCNIPL
;@entry TXRUN
;@entry LCPYP
.export SCNIPL, TXRUN, LCPYP
.export APG, CROW, RWOLO, RWOHI         ; 7d3: p6/scpag.s's SCPAGP
.import TXBUF, TXLEN, LSTCHR, LINLEN, KEYINI

.code

; --- SCNIPL, TXRUN ------------------------------------------------------------------------

SCNIPL:         STA     PGBASE
                STX     PGBASE + 1
                LDA     #<CACHE        ; the cache's physical address
                STA     PB73
                LDA     #>CACHE
                STA     PB73 + 1
                LDA     #<PB73
                LDX     #>PB73
                LDY     #O_PHYSADDR
                JSR     BDOS
                LDA     PB73 + 2       ; the paragraph x 16 + the offset
                STA     CACHEP
                LDA     PB73 + 3
                STA     CACHEP + 1
                STZ     CACHEP + 2
                LDX     #4
@x16:           ASL     CACHEP
                ROL     CACHEP + 1
                ROL     CACHEP + 2
                DEX
                BNE     @x16
                LDA     CACHEP
                ORA     PB73
                STA     CACHEP
                LDA     #7             ; COLOR 7,0,0
                STA     FGC
                STA     ATTR
                STZ     BGC
                STZ     BRDC
                STZ     APAGE
                STZ     VPAGE
                STZ     CROW
                LDA     #$FF
                STA     CDLO
                STZ     CDHI
                STZ     CSRTYP
                STZ     CSRFLG
                STZ     CURON
                STZ     TWOBYT
                STZ     KEYSW
                STZ     F_EDIT
                STZ     F_INST
                STZ     F_EDP6
                JSR     SCNIPL8
                LDA     #80
                LDX     #25
                JSR     SCNSWI
                JSR     PGINIT
                JSR     SCNBRK
                JSR     CLRSCN
                JSR     VSHOW
                JSR     KEYINI
                JMP     P6OUT

TXRUN:          JSR     P6IN
                JMP     P6OUT

; LCPYP: the visual page's LINCNT rows, the key line's too, to LST: by
; function 5, as text: each row's trailing blanks left out, a character
; below 32 or $FF sent as a blank, a CR and an LF after each; LPT1's column
; 0 after. TXBUF's bytes and the cached row reach the card first; the rows
; are read into the cache (ROWIN on the visual page), which is then
; forgotten (APGSET).
LCPYP:          JSR     P6IN
                JSR     FLUSH
                LDA     VPAGE
                JSR     PGSET
                LDA     #1
                STA     LCR
@row:           LDA     LCR
                JSR     ROWIN
                LDA     CRTWID         ; the last cell that isn't a blank
                ASL     A
                TAY
@trim:          LDA     CACHE - 2,Y
                JSR     LCHAR
                BNE     @last
                DEY
                DEY
                BNE     @trim
@last:          STY     LCN
                LDY     #0
@out:           CPY     LCN
                BEQ     @eol
                INY
                INY
                PHY
                LDA     CACHE - 2,Y
                JSR     LCHAR
                JSR     LSTOUT
                PLY
                BRA     @out
@eol:           LDA     #13
                JSR     LSTOUT
                LDA     #10
                JSR     LSTOUT
                LDA     LCR
                INC     LCR
                CMP     LINCNT
                BCC     @row
                STZ     LPTPOS
                JSR     APGSET         ; the active page's again, the cache empty
                JMP     P6OUT
; LCHAR: A a cell's character as LCOPY sends it (a blank for one below 32
; or $FF); Z set for a blank.
LCHAR:          CMP     #' '
                BCC     @b
                CMP     #$FF
                BNE     @x
@b:             LDA     #' '
@x:             CMP     #' '
                RTS
LSTOUT:         LDY     #L_WRITE
                JMP     BDOS

; P6IN: OUTDO's bytes waiting in TXBUF, out first (every entry of P6's
; but TXRUN's own does this before anything else).
; OO-1: a run of PRINT's characters goes straight into the cached row
; (FASTRN), as PUTC and SCNOUT's own fast path would put them one by one,
; while nothing needs more: not in the editor, no escape, insert or cursor,
; the cursor's line EOL-terminated and the cursor on it (the 30,000-cycle
; gate, V6).
P6IN:           LDX     #0
@l:             CPX     TXLEN
                BCS     @d
                LDA     F_EDIT
                ORA     ESCFLG
                ORA     F_INST
                ORA     CSRFLG
                ORA     TWOBYT
                BNE     @one
                LDA     LINLEN
                CMP     CSRX
                BCC     @one
                LDY     CSRY
                LDA     TTTRM,Y
                AND     #$7F
                CMP     #TRMEOL
                BNE     @one
                LDA     TXBUF,X
                CMP     #' '
                BCC     @one
                CMP     #$FF
                BEQ     @one
                LDA     CSRY
                CMP     CROW
                BEQ     @c
                PHX
                JSR     ROWIN
                PLX
                BCS     @one
@c:             JSR     FASTRN
                BRA     @l
@one:           LDA     TXBUF,X
                PHX
                JSR     PUTC
                PLX
                INX
                BRA     @l
@d:             STZ     TXLEN
                RTS

; FASTRN: TXBUF's printable characters from X into the cached row, to its
; end at most: X past them. The terminator, LSTPOS and FSTPOS, LSTCHR,
; F_CRET and the cursor as SCNOUT leaves them after the last.
FASTRN:         LDA     CSRX
                STA     FRC0
                ASL     A
                TAY
                LDA     LINLEN
                ASL     A
                STA     FRLIM
@lp:            LDA     TXBUF,X
                CMP     #' '
                BCC     @e
                CMP     #$FF
                BEQ     @e
                STA     CACHE - 2,Y
                LDA     ATTR
                STA     CACHE - 1,Y
                INY
                INY
                INX
                CPX     TXLEN
                BCS     @e
                CPY     FRLIM
                BCC     @lp
                BEQ     @lp
@e:             TYA
                LSR     A
                STA     CSRX
                DEC     A
                STA     FRL            ; the last place written
                LDA     FRC0
                ASL     A
                CMP     CDLO
                BCS     :+
                STA     CDLO
:               LDA     FRL
                ASL     A
                CMP     CDHI
                BCC     :+
                STA     CDHI
:               LDY     CSRY
                LDA     FRL
                CMP     TTCOL,Y
                BCC     :+
                STA     TTCOL,Y
:               CPY     LSTLNE
                BNE     :+
                CMP     LSTCOL
                BCC     :+
                STA     LSTCOL
:               CPY     FSTLIN
                BNE     :+
                LDA     FRC0
                CMP     FSTCOL
                BCS     :+
                STA     FSTCOL
:               LDA     TXBUF - 1,X
                STA     LSTCHR
                LDA     #$FF
                STA     F_CRET
                STZ     CSRTYP
                RTS

; P6OUT: before every return from P6: the cached row written back (it is
; kept: only P6 writes the text pages, and a scroll, a clear or a page's
; change forgets it), and TTYPOS (OUTDO's column) the cursor's, CSRX less 1.
P6OUT:          JSR     FLUSH
                LDA     CSRX
                DEC     A
                STA     TTYPOS
                RTS

; --- PUTC (GIOSCN.ASM 191, SCNSOT and SCNOL1, the console's: no file) ----------------------

; PUTC: A, a byte to the screen. In the editor a $FF starts a two-byte
; function (TWOBYT); a CR is a CR and an LF, and an LF straight after a CR
; is dropped (LSTCHR, shared with SCRN:, p4/gioscn.s).
PUTC:           LDX     TWOBYT
                BEQ     @one
                STZ     TWOBYT
                STX     xAH
                BRA     SCNOL1
@one:           LDX     F_EDIT
                BEQ     @n
                CMP     #$FF
                BNE     @n
                STA     TWOBYT
                RTS
@n:             STZ     xAH
SCNOL1:         LDX     LSTCHR
                STA     LSTCHR
                CPX     #13
                BNE     @nt
                CMP     #10
                BEQ     @r
@nt:            PHA
                JSR     SCNOUT
                PLA
                CMP     #13
                BNE     @r
                STZ     xAH
                LDA     #10
                JMP     SCNOUT
@r:             RTS

; --- PRTMAP, EDTMAP (the OEM's maps) ------------------------------------------------------

; PRTMAP: PRINT's characters. Z clear always (nothing ignored); AH $FF for
; a function: the PC's BEL, TAB, LF, home, clear, CR and the four cursor
; moves (7, 9-13, 28-31), and $FF, the "not typed" mark; anything else its
; glyph.
PRTMAP:         LDA     xAH
                BNE     @pr
                LDA     xAL
                CMP     #$FF
                BEQ     @fn
                CMP     #7
                BEQ     @fn
                CMP     #9
                BCC     @pr
                CMP     #14
                BCC     @fn
                CMP     #28
                BCC     @pr
                CMP     #32
                BCS     @pr
@fn:            LDA     #$FF
                STA     xAH
@pr:            LDA     #1
                RTS

; EDTMAP: the editor's characters. A two-byte function ($FF, code) as it
; is; a control character or DEL typed is its function ($FF, it); anything
; else printed. Z clear always.
EDTMAP:         LDA     xAH
                BNE     @pr
                LDA     xAL
                CMP     #$7F
                BEQ     @fn
                CMP     #' '
                BCS     @pr
@fn:            LDA     #$FF
                STA     xAH
@pr:            LDA     #1
                RTS

; --- The row cache ------------------------------------------------------------------------

; ROWADR: A a row (1-LINCNT): CRADR its card address on the active page. C
; set: no such row.
ROWADR:         CMP     #1
                BCC     @bad
                CMP     LINCNT
                BEQ     :+
                BCS     @bad
:               TAX
                CLC
                LDA     APG
                ADC     RWOLO,X
                STA     CRADR
                LDA     APG + 1
                ADC     RWOHI,X
                STA     CRADR + 1
                LDA     APG + 2
                ADC     #0
                STA     CRADR + 2
                CLC
                RTS
@bad:           SEC
                RTS

; ROWIN: A a row: the cache on it, read from the card if it wasn't there.
; C set: no such row.
ROWIN:          CMP     CROW
                BEQ     @in
                PHA
                JSR     FLUSH
                PLA
                JSR     ROWADR
                BCS     @x
                STX     CROW
                LDA     #DM_VREAD
                STA     DMB
                LDX     #2
@a:             LDA     CRADR,X
                STA     DMB + 1,X
                LDA     CACHEP,X
                STA     DMB + 4,X
                DEX
                BPL     @a
                JSR     ROWLEN
                JSR     DMCALL
@in:            CLC
@x:             RTS

; ROWLEN: DMB's length a row's bytes, CRTWID x 2.
ROWLEN:         LDA     CRTWID
                ASL     A
                STA     DMB + 7
                STZ     DMB + 8
                RTS

; FLUSH: the cached row's written span back to the card.
FLUSH:          LDA     CDHI
                BEQ     @x
                LDA     #DM_VWRITE
                STA     DMB
                SEC
                LDA     CDLO
                SBC     #2
                STA     FLO
                CLC
                LDA     CACHEP
                ADC     FLO
                STA     DMB + 1
                LDA     CACHEP + 1
                ADC     #0
                STA     DMB + 2
                LDA     CACHEP + 2
                ADC     #0
                STA     DMB + 3
                CLC
                LDA     CRADR
                ADC     FLO
                STA     DMB + 4
                LDA     CRADR + 1
                ADC     #0
                STA     DMB + 5
                LDA     CRADR + 2
                ADC     #0
                STA     DMB + 6
                SEC
                LDA     CDHI
                SBC     CDLO
                CLC
                ADC     #2
                STA     DMB + 7
                STZ     DMB + 8
                JSR     DMCALL
                LDA     #$FF
                STA     CDLO
                STZ     CDHI
@x:             RTS

; DMCALL: function 83 on DMB.
DMCALL:         LDA     #<DMB
                LDX     #>DMB
                LDY     #O_DMA
                JMP     BDOS

; CELL: A a row, X a column: the cache on the row, Y the cell's place
; (CACHE-2,Y the character, CACHE-1,Y the attribute). C set: no such cell.
CELL:           PHX
                JSR     ROWIN
                PLX
                BCS     @x
                TXA
                BEQ     @bad
                CMP     CRTWID
                BEQ     :+
                BCS     @bad
:               ASL     A
                TAY
                CLC
@x:             RTS
@bad:           SEC
                RTS

; DIRTY: Y a cell just written: in the span to write back.
DIRTY:          CPY     CDLO
                BCS     :+
                STY     CDLO
:               CPY     CDHI
                BCC     :+
                STY     CDHI
:               RTS

; --- SCROUT, SCRINP, SCRATR, CLREOL -------------------------------------------------------

; SCROUT: A at (DH, DL) in the current attribute. xD* kept; C clear (no
; escape sequence).
SCROUT:         LDX     xDL            ; the cached row: no call
                CPX     CROW
                BNE     @far
                LDX     xDH
                BEQ     @x
                CPX     CRTWID
                BEQ     :+
                BCS     @x
:               PHA
                TXA
                ASL     A
                TAY
                PLA
                BRA     @st
@far:           PHA
                LDA     xDL
                LDX     xDH
                JSR     CELL
                PLA
                BCS     @x
@st:            STA     CACHE - 2,Y
                LDA     ATTR
                STA     CACHE - 1,Y
                CPY     CDLO           ; DIRTY's
                BCS     :+
                STY     CDLO
:               CPY     CDHI
                BCC     @x
                STY     CDHI
@x:             CLC
                RTS

; SCRINP: the character at (DH, DL): A and AL (AH 0); a blank off the
; screen. C clear.
SCRINP:         LDA     xDL
                LDX     xDH
                JSR     CELL
                LDA     #' '
                BCS     @x
                LDA     CACHE - 2,Y
@x:             STA     xAL
                STZ     xAH
                CLC
                RTS

; SCRATR: the attribute at (DH, DL): A; 0 off the screen.
SCRATR:         LDA     xDL
                LDX     xDH
                JSR     CELL
                LDA     #0
                BCS     @x
                LDA     CACHE - 1,Y
@x:             RTS

; CLREOL: DL from DH to its end blank, in the current attribute. A whole
; row needs no read: the cache is put on it as it is written. xD* kept.
CLREOL:         LDA     xDH
                CMP     #2
                BCC     :+
                JMP     @part
:
                LDA     xDL
                CMP     CROW
                BEQ     @fill
                JSR     FLUSH
                LDA     xDL
                JSR     ROWADR
                BCC     :+
                RTS
:               STX     CROW
@fill:          LDA     CRTWID         ; the scroll's blank row: 16 cells a turn
                ASL     A
                STA     CDHI
                TAY
                LDX     ATTR
@f:             CPY     #32            ; (40 columns: 2 turns, then 8 cells left)
                BCS     :+
                JMP     @f8
:               LDA     #' '
                STA     CACHE - 2,Y
                STA     CACHE - 4,Y
                STA     CACHE - 6,Y
                STA     CACHE - 8,Y
                STA     CACHE - 10,Y
                STA     CACHE - 12,Y
                STA     CACHE - 14,Y
                STA     CACHE - 16,Y
                STA     CACHE - 18,Y
                STA     CACHE - 20,Y
                STA     CACHE - 22,Y
                STA     CACHE - 24,Y
                STA     CACHE - 26,Y
                STA     CACHE - 28,Y
                STA     CACHE - 30,Y
                STA     CACHE - 32,Y
                TXA
                STA     CACHE - 1,Y
                STA     CACHE - 3,Y
                STA     CACHE - 5,Y
                STA     CACHE - 7,Y
                STA     CACHE - 9,Y
                STA     CACHE - 11,Y
                STA     CACHE - 13,Y
                STA     CACHE - 15,Y
                STA     CACHE - 17,Y
                STA     CACHE - 19,Y
                STA     CACHE - 21,Y
                STA     CACHE - 23,Y
                STA     CACHE - 25,Y
                STA     CACHE - 27,Y
                STA     CACHE - 29,Y
                STA     CACHE - 31,Y
                TYA
                SEC
                SBC     #32
                TAY
                BEQ     @fd
                JMP     @f
@f8:            LDA     #' '           ; (cells 1-8: bytes 0-15)
                STA     CACHE + 0
                STA     CACHE + 2
                STA     CACHE + 4
                STA     CACHE + 6
                STA     CACHE + 8
                STA     CACHE + 10
                STA     CACHE + 12
                STA     CACHE + 14
                TXA
                STA     CACHE + 1
                STA     CACHE + 3
                STA     CACHE + 5
                STA     CACHE + 7
                STA     CACHE + 9
                STA     CACHE + 11
                STA     CACHE + 13
                STA     CACHE + 15
@fd:            LDA     #2
                STA     CDLO
@r:             RTS
@part:          LDA     xDH
                PHA
@l:             LDA     xDH
                CMP     CRTWID
                BEQ     :+
                BCS     @d
:               LDA     #' '
                JSR     SCROUT
                INC     xDH
                BRA     @l
@d:             PLA
                STA     xDH
                RTS

; --- SCROLL -------------------------------------------------------------------------------

; SCROLL: CH columns of CL rows from (AH, AL) to (BH, BL). Rows (CL 2 or
; more: always whole ones) by DM_VCOPY on the card, the cache written back
; and forgotten first; up in one copy (forward over itself is exact), down
; a row at a time from the last. One row's part through the cache, by TMP.
; xA*, xB*, xC*, xD* kept.
SCROLL:         LDA     xCL
                CMP     #2
                BCS     @rows
                LDA     xCH
                BEQ     @x
                LDX     #0             ; the source's cells into TMP
                STZ     SCI
@rd:            CLC
                LDA     xAH
                ADC     SCI
                TAX
                LDA     xAL
                JSR     CELL
                LDX     SCI
                LDA     #' '
                STA     TMP,X
                LDA     ATTR
                STA     TMP + 80,X
                BCS     :+
                LDA     CACHE - 2,Y
                STA     TMP,X
                LDA     CACHE - 1,Y
                STA     TMP + 80,X
:               INC     SCI
                LDA     SCI
                CMP     xCH
                BCC     @rd
                STZ     SCI            ; then to the destination
@wr:            CLC
                LDA     xBH
                ADC     SCI
                TAX
                LDA     xBL
                JSR     CELL
                BCS     :+
                LDX     SCI
                LDA     TMP,X
                STA     CACHE - 2,Y
                LDA     TMP + 80,X
                STA     CACHE - 1,Y
                JSR     DIRTY
:               INC     SCI
                LDA     SCI
                CMP     xCH
                BCC     @wr
@x:             RTS
@rows:          JSR     FLUSH
                STZ     CROW
                LDA     #DM_VCOPY
                STA     DMB
                LDA     xBL
                CMP     xAL
                BCS     @down
                LDX     xAL            ; up: CL rows in one copy, from row AL
                CLC                    ;   to row BL of the active page
                LDA     APG
                ADC     RWOLO,X
                STA     DMB + 1
                LDA     APG + 1
                ADC     RWOHI,X
                STA     DMB + 2
                LDA     APG + 2
                ADC     #0
                STA     DMB + 3
                LDX     xBL
                CLC
                LDA     APG
                ADC     RWOLO,X
                STA     DMB + 4
                LDA     APG + 1
                ADC     RWOHI,X
                STA     DMB + 5
                LDA     APG + 2
                ADC     #0
                STA     DMB + 6
                LDX     xCL            ; CL x the stride: row CL+1's offset
                LDA     RWOLO + 1,X
                STA     DMB + 7
                LDA     RWOHI + 1,X
                STA     DMB + 8
                JMP     DMCALL
@down:          LDA     xCL            ; down: from the last row
                STA     SCI
@dl:            DEC     SCI
                CLC
                LDA     xAL
                ADC     SCI
                JSR     ROWADR
                LDX     #2
@s2:            LDA     CRADR,X
                STA     DMB + 1,X
                DEX
                BPL     @s2
                CLC
                LDA     xBL
                ADC     SCI
                JSR     ROWADR
                BCS     @dn
                LDX     #2
@d2:            LDA     CRADR,X
                STA     DMB + 4,X
                DEX
                BPL     @d2
                LDA     STRIDE
                STA     DMB + 7
                STZ     DMB + 8
                LDA     #DM_VCOPY
                STA     DMB
                JSR     DMCALL
@dn:            LDA     SCI
                BNE     @dl
                RTS

; --- CLRSCN, PGINIT -----------------------------------------------------------------------

; CLRSCN: the active page's rows 1-LINCNT blank in the current attribute
; (one cell written, then a forward DM_VCOPY over itself repeats it, as
; DM_FILL does), then SCNCLR: the terminators, the cursor home, the key line.
CLRSCN:         LDA     LINCNT
                STA     SCI
                LDA     APG
                LDX     APG + 1
                LDY     APG + 2
                JSR     FILL
                STZ     CURON
                JMP     SCNCLR

; FILL: SCI rows of STRIDE bytes from the card address A, X, Y blank.
FILL:           PHA                    ; the cached row written back first (FLUSH uses DMB)
                PHX
                PHY
                JSR     FLUSH
                STZ     CROW
                PLY
                PLX
                PLA
                STA     DMB + 4
                STX     DMB + 5
                STY     DMB + 6
                LDA     #' '
                STA     CACHE
                LDA     ATTR
                STA     CACHE + 1
                LDA     #DM_VWRITE
                STA     DMB
                LDX     #2
@s:             LDA     CACHEP,X
                STA     DMB + 1,X
                DEX
                BPL     @s
                LDA     #2
                STA     DMB + 7
                STZ     DMB + 8
                JSR     DMCALL
                LDA     #DM_VCOPY      ; the cell repeated over the rest
                STA     DMB
                LDX     #2
@c:             LDA     DMB + 4,X
                STA     DMB + 1,X
                DEX
                BPL     @c
                CLC
                LDA     DMB + 4
                ADC     #2
                STA     DMB + 4
                BCC     :+
                INC     DMB + 5
                BNE     :+
                INC     DMB + 6
:               LDX     SCI            ; SCI x STRIDE - 2: row SCI+1's offset less 2
                SEC
                LDA     RWOLO + 1,X
                SBC     #2
                STA     DMB + 7
                LDA     RWOHI + 1,X
                SBC     #0
                STA     DMB + 8
                JMP     DMCALL

; PGINIT: for the width (CRTWID): the pages' size (20 card pages at 80
; columns, 12 at 40) and count (4, 8), the stride, the rows' offsets, the
; active page's address; and every page blank (the owner's answer 6).
PGINIT:         LDX     #20
                LDY     #4
                LDA     CRTWID
                ASL     A
                STA     STRIDE
                CMP     #160
                BEQ     :+
                LDX     #12
                LDY     #8
:               STX     PGSIZE
                STY     NPAGES
                STZ     RWOLO + 1
                STZ     RWOHI + 1
                LDX     #1
@r:             CLC
                LDA     RWOLO,X
                ADC     STRIDE
                STA     RWOLO + 1,X
                LDA     RWOHI,X
                ADC     #0
                STA     RWOHI + 1,X
                INX
                CPX     #NMLINE + 1
                BCC     @r
                JSR     APGSET
                LDA     #0             ; every page: NPAGES x PGSIZE card pages
                LDX     NPAGES
@p:             CLC
                ADC     PGSIZE
                DEX
                BNE     @p
                STA     SCI            ; (at most 96: 24 KB)
                LDA     #0
                LDX     PGBASE
                LDY     PGBASE + 1
                JSR     FILLP
                ; (on into BANDS)

; BANDS: with 25 rows, the 30-row layer's rows 26-30 (the bands above and
; below the text, centred by VSCROLL 440) blank in the border's colour, as
; the background, on every page: the border shows as the PC's overscan
; (PGINIT, and COLOR's border). The text's rows, CLS and the scroll never
; touch them. The border's 3 bits, as the background's (bit 7 is BLINK).
BANDS:          SEC
                LDA     #NMLINE
                SBC     LINCNT
                BEQ     @x
                STA     SCI
                LDA     ATTR
                PHA
                LDA     BRDC
                AND     #$07
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     ATTR
                LDA     NPAGES
@p:             DEC     A
                PHA
                JSR     PGADR          ; A/X the page's first card page
                LDY     LINCNT         ; row LINCNT+1's offset added
                CLC
                ADC     RWOHI + 1,Y
                BCC     :+
                INX
:               PHX
                TAX
                LDA     RWOLO + 1,Y
                PLY
                JSR     FILL
                PLA
                BNE     @p
                PLA
                STA     ATTR
@x:             RTS

; FILLP: SCI card pages from the card address A, X, Y blank.
FILLP:          PHA                    ; the cached row written back first (FLUSH uses DMB)
                PHX
                PHY
                JSR     FLUSH
                STZ     CROW
                PLY
                PLX
                PLA
                STA     DMB + 4
                STX     DMB + 5
                STY     DMB + 6
                LDA     #' '
                STA     CACHE
                LDA     ATTR
                STA     CACHE + 1
                LDA     #DM_VWRITE
                STA     DMB
                LDX     #2
@s:             LDA     CACHEP,X
                STA     DMB + 1,X
                DEX
                BPL     @s
                LDA     #2
                STA     DMB + 7
                STZ     DMB + 8
                JSR     DMCALL
                LDA     #DM_VCOPY
                STA     DMB
                LDX     #2
@c:             LDA     DMB + 4,X
                STA     DMB + 1,X
                DEX
                BPL     @c
                LDA     #2
                STA     DMB + 4
                LDA     #$FE           ; SCI pages less 2 bytes
                STA     DMB + 7
                LDA     SCI
                DEC     A
                STA     DMB + 8
                JMP     DMCALL

; APGSET: APG the active page's card address, PGBASE + APAGE x PGSIZE pages;
; PGSET: page A's (LCPYP's visual page). The cache forgotten.
APGSET:         LDA     APAGE
PGSET:          JSR     PGADR
                STZ     APG
                STA     APG + 1
                STX     APG + 2
                STZ     CROW
                RTS
; PGADR: A a page: A/X its first card page.
PGADR:          TAY
                LDA     PGBASE
                LDX     PGBASE + 1
                CPY     #0
                BEQ     @x
@l:             CLC
                ADC     PGSIZE
                BCC     :+
                INX
:               DEY
                BNE     @l
@x:             RTS

; VSHOW: the visual page on layer 0, through P5's TXMODE (OOLIB's GX_LAYER,
; GX_SCROLL, GX_BORDER, GX_SHOW): A/X its card page, Y BLINK, 25 rows
; centred (the owner's answer 10: VSCROLL 440, 40 lines above and below),
; the border in bits 5-2, 40 columns in bit 0.
VSHOW:          LDA     BRDC
                ASL     A
                ASL     A
                ORA     #$80
                LDX     LINCNT
                CPX     #25
                BNE     :+
                ORA     #$40
:               LDX     CRTWID
                CPX     #40
                BNE     :+
                ORA     #$01
:               PHA
                LDA     VPAGE
                JSR     PGADR
                PLY
                FJSR    TXMODE
                RTS

; --- FKYFMT, FKYADV (the OEM's: the key line's format) ----------------------------------

; FKYFMT: X the keys the line shows, A the first: 10 from F1 at 80 columns,
; 5 from FKYST (F1 or F6) at 40; 6 characters each (KEYDSP's).
FKYFMT:         LDX     #10
                LDA     #1
                LDY     CRTWID
                CPY     #40
                BNE     @x
                LDX     #5
                LDA     FKYST
@x:             RTS

; FKYADV: Z set (the line off): its first keys, F1; A $FF (on). Z clear:
; the next keys, at 40 columns F6-F10 after F1-F5, A $FF; past the last
; (F6-F10, or any at 80 columns) A 0 (off).
FKYADV:         BNE     @adv
                LDA     #1
@set:           STA     FKYST
                LDA     #$FF
                RTS
@adv:           LDA     CRTWID
                CMP     #40
                BNE     @off
                LDA     FKYST
                CMP     #6
                BEQ     @off
                LDA     #6
                BRA     @set
@off:           LDA     #0
                RTS

; --- The cursor (GIOKYB.ASM's STCTYP and SETCSR; CSRDSP the OEM's) ------------------------

; STCTYP: C set: the user's cursor (3); else the editor's, insert (1) or
; overwrite (2). The flags and A kept.
STCTYP:         PHP
                PHA
                LDA     #3
                BCS     @s
                DEC     A
                LDX     F_INST
                BEQ     @s
                DEC     A
@s:             STA     CSRTYP
                PLA
                PLP
                RTS

; SETCSR: the cursor as CSRTYP says, if that changed.
SETCSR:         LDA     CSRTYP
                CMP     CSRFLG
                BEQ     @x
                STA     CSRFLG
                JMP     CSRDSP
@x:             RTS

; CSRDSP: A the cursor's type at DX: the old cell's attribute put back;
; then for 1 and 2 (the editor's) the cell inverse and blinking. 0 and 3
; show none (the owner's answer 9).
CSRDSP:         PHA
                LDA     CURON
                BEQ     @show
                STZ     CURON
                LDA     CURR
                LDX     CURC
                JSR     CELL
                BCS     @show
                LDA     CURA
                STA     CACHE - 1,Y
                JSR     DIRTY
@show:          PLA
                CMP     #1
                BEQ     @on
                CMP     #2
                BNE     @x
@on:            LDA     xDL
                LDX     xDH
                JSR     CELL
                BCS     @x
                LDA     xDL
                STA     CURR
                LDA     xDH
                STA     CURC
                LDA     CACHE - 1,Y
                STA     CURA
                AND     #$07           ; the foreground's low 3 bits the background
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     SCI
                LDA     CURA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                AND     #$07           ; the background the foreground
                ORA     SCI
                ORA     #$80           ; blinking
                STA     CACHE - 1,Y
                JSR     DIRTY
                LDA     #1
                STA     CURON
@x:             RTS

.segment "BRAM"
CSRTYP:         .res    1              ; the cursor wanted (GIOKYB.ASM's)
CSRFLG:         .res    1              ; and shown
TWOBYT:         .res    1              ; SCNSOT's two-byte function's first byte
ATTR:           .res    1              ; the attribute characters are written in
FGC:            .res    1              ; COLOR's: foreground (0-31), background, border
BGC:            .res    1
BRDC:           .res    1
APAGE:          .res    1              ; SCREEN's active page
VPAGE:          .res    1              ; and visual page
NPAGES:         .res    1
PGSIZE:         .res    1              ; a page's card pages
PGBASE:         .res    2              ; the first page's card page
STRIDE:         .res    1              ; a row's bytes
APG:            .res    3              ; the active page's card address
RWOLO:          .res    NMLINE + 2     ; a row's offset in a page, by row from 1 (row
RWOHI:          .res    NMLINE + 2     ;   n+1's is n rows' bytes, to 31)
CROW:           .res    1              ; the row in the cache, 0 none
CDLO:           .res    1              ; its span to write back: cells CDLO/2 .. CDHI/2
CDHI:           .res    1              ;   (0: none)
CRADR:          .res    3              ; its card address
FLO:            .res    1
FRC0:           .res    1              ; FASTRN's first place, last, and limit
FRL:            .res    1
FRLIM:          .res    1
SCI:            .res    1              ; a count
FKYST:          .res    1              ; the key line's first key at 40 columns: 1 or 6
LCR:            .res    1              ; LCPYP's row
LCN:            .res    1              ;   and its last cell x 2
CURON:          .res    1              ; the cursor shown: at CURR, CURC, over CURA
CURR:           .res    1
CURC:           .res    1
CURA:           .res    1
DMB:            .res    9              ; function 83's block
PB73:           .res    4              ; function 73's
CACHEP:         .res    3              ; the cache's physical address
CACHE:          .res    160            ; the row: a character and an attribute a cell
TMP:            .res    160            ; SCROLL's: 80 characters, then their attributes
