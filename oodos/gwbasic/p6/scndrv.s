; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; SCNDRV.ASM, translated, in block P6 (hw/oodos.md, "The plan, phase 7":
; 7d1, the text screen): GW-BASIC's own screen driver and full-screen
; editor, on the OEM's text routines (p6/oem.s: SCROUT, SCRINP, CLREOL,
; SCROLL, CLRSCN, SETCSR), which keep a row of the card's text in P6's RAM.
;
;   SCNIPL8 SCNBRK  start-up's and a break's resets
;   SCNSWI          the screen's width and height (A, X)
;   SCNCLR          the terminators cleared, the cursor home, the key line
;   SCNPOS          DH, DL: the cursor (DH within the line)
;   SCNRDT SCNWTT   a line's terminator read (C EOL, Z LF), written
;   SCNOUT          a character out at the cursor (A, xAH its first byte):
;                   control characters and the editor's functions done
;   SCNMRK SCNRDL   the line's start marked for INLIN; the logical line read
;                   back from the cells into BUF (p6/scnedt.s)
;
; Each line has a terminator (TTCOL, TTTRM: the 8086's LINTTB, two arrays
; here, by line from 1): the column its data ends at, and how it ends: EOL
; (the logical line's end), WRP (it goes on to the next line), NWP (a null
; wrap), LF (a typed line feed); TRMNUL ($80) marks a line not to be taken
; as typed (the $FF a message leaves). The routines keep the source's order
; and labels, its registers in P6's bytes of their names (p6/p6.inc), its CF
; in C and its ZF in Z where a caller tests them.
;
; OO-1: the function-key line (KEYSW, LABELK's ^T, KEYDSP's line: 7d2) is
; the last line, LINCNT, always outside the scroll window; GRPINI (the graphics cursor) is
; P5's, far called in a graphics mode only (7e1); the bell (CBEEP's BEEP) is 7f's, so a ^G does nothing yet; the PC's
; two-byte (DBLCHR) characters aren't built (DBLCHR is 0 in the source
; too).

.include "gwbasic.inc"
.include "p6/p6.inc"

.import LINLEN, SEMFLG, BUF, STRTAB

; --- 8086-like moves ----------------------------------------------------------------------

.macro  MOVB    dst, src
                LDA     src
                STA     dst
.endmacro
.macro  PSH     r
                LDA     r
                PHA
.endmacro
.macro  PULB    r
                PLA
                STA     r
.endmacro
; 8086 conditions after LDA a / CMP b, for JA and JBE (C = a >= b on the 6502).
.macro  BRA_A   t                      ; a > b
        .local  @no
                BEQ     @no
                BCS     t
@no:
.endmacro
.macro  BRA_BE  t                      ; a <= b
                BCC     t
                BEQ     t
.endmacro
.macro  BRA_AL  t                      ; the same, far
        .local  @no
                BEQ     @no
                JCS     t
@no:
.endmacro
.macro  BRA_BEL t
                JCC     t
                JEQ     t
.endmacro

.code

; --- SCNIPL, SCNBRK, SCNSWI (SCNDRV.ASM 196) ----------------------------------------------

SCNIPL8:        STZ     ESCFLG
                RTS

SCNBRK:         STZ     FSTLIN
                LDA     #$FF
                STA     LSTLNE
                STA     F_CRET
                RTS

; SCNSWI: A the width, X the height.
SCNSWI:         STA     CRTWID
                STA     LINLEN
                STX     LINCNT
                LDY     #1
                STY     WDOTOP
                DEX                    ; the status line kept
                STX     WDOBOT
                STY     WDOLFT
                STA     WDORGT
                RTS

; --- SCNCLR, SCNPOS (SCNDRV.ASM 251) ------------------------------------------------------

SCNCLR:         JSR     TTBINI
                LDA     GMOD           ; GRPINI, P5's: a graphics mode's (7e1)
                BEQ     :+
                FJSR    GRPINI
:               JSR     WHOME
                MOVB    CSRY, xDL      ; SCNLOC
                MOVB    CSRX, xDH
                JMP     KEYDSP

; SCNPOS: DH the cursor's column (within the line: at most LINLEN), DL its
; line. The flags kept.
SCNPOS:         PHP
                LDA     CSRX
                CMP     LINLEN
                BRA_BE  :+
                LDA     LINLEN
:               STA     xDH
                MOVB    xDL, CSRY
                PLP
                RTS

; KEYDSP (GWSTS.ASM 1471): the key line, on the last line (LINCNT). KEYSW
; off: the line cleared. On: FKYFMT's keys from its first, each its number
; (key 10's 0) in the attribute, then its text's first 6 characters
; reversed (XFGBG; blanks past its end), then a blank (KEYDCH: a CR shown as
; 27, an LF as "<"). DX kept. OO-1: GETFMT's two-digit first key never
; arises (the first is F1 or F6), so a number is one digit and KEYADV's
; wrap (KADNRM) never comes; the cursor isn't turned off and on round it
; (it is shown only in KEYSIN's wait, which hides it before any caller).
KEYDSP:         PSH     xDH
                PSH     xDL
                LDA     #1
                STA     xDH
                MOVB    xDL, LINCNT
                LDA     KEYSW
                BNE     @on
                JSR     CLREOL
                BRA     @x
@on:            JSR     FKYFMT         ; GETFMT: X the keys, A the first
                STX     KYCNT
                STA     KYNUM
                MOVB    KYATR, ATTR
@key:           LDA     KYNUM          ; the number
                CMP     #10
                BCC     :+
                LDA     #0
:               ORA     #'0'
                JSR     KEYDCH
                LDA     KYATR          ; XFGBG: the foreground and background
                AND     #$07           ; swapped
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     ATTR
                LDA     KYATR
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                AND     #$07
                ORA     ATTR
                STA     ATTR
                LDA     KYNUM          ; STRTAB + 16 x (n - 1)
                DEC     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDY     #6             ; FKCNUM
@ch:            LDA     STRTAB,X
                BEQ     :+             ; past the text: blanks
                INX
:               PHX
                PHY
                JSR     KEYDCH
                PLY
                PLX
                DEY
                BNE     @ch
                MOVB    ATTR, KYATR    ; XFGBG back
                LDA     #0             ; KEYDB
                JSR     KEYDCH
                INC     KYNUM          ; KEYADV
                DEC     KYCNT
                BNE     @key
@x:             PULB    xDL
                PULB    xDH
                RTS
; KEYDCH: A at (DH, DL), DH moved on: a 0 as a blank, a CR as 27 (the IBM
; FKEYCR), an LF as "<".
KEYDCH:         CMP     #0
                BNE     :+
                LDA     #' '
:               CMP     #13
                BNE     :+
                LDA     #27
:               CMP     #10
                BNE     :+
                LDA     #'<'
:               JSR     SCROUT
                INC     xDH
                RTS

; LABELK (SCNDRV.ASM 816): ^T: the key line on (its first keys), on to the
; next (40 columns: F1-F5, then F6-F10), or off past the last (FKYADV);
; then drawn, or cleared. DX kept.
LABELK:         LDA     KEYSW          ; Z: off
                JSR     FKYADV
                STA     KEYSW
                JMP     KEYDSP

; --- SCNRDT, SCNWTT, TTBINI (SCNDRV.ASM 298) ----------------------------------------------

; SCNRDT: DL's terminator: AH its column, AL its type (TRMNUL off); TRMCUR
; DL. C set: EOL; else Z set: LF. A the type.
SCNRDT:         LDX     xDL
                STX     TRMCUR
                LDA     TTCOL,X
                STA     xAH
                LDA     TTTRM,X
                AND     #$7F
                STA     xAL
TFLG:           CMP     #TRMEOL        ; Z and C both set for EOL
                BEQ     @e
                CMP     #TRMLNF
                CLC
@e:             RTS

; SCNWTT: the line last read ends at AH, type AL (its TRMNUL kept).
SCNWTT:         LDX     TRMCUR
                LDA     xAH
                STA     TTCOL,X
                LDA     TTTRM,X
                AND     #TRMNUL
                ORA     xAL
                STA     TTTRM,X
                RTS

TTBINI:         LDX     #NMLINE + 1
@l:             STZ     TTCOL,X
                LDA     #TRMEOL
                STA     TTTRM,X
                DEX
                BNE     @l
                RTS

; XCHGAC: the 8086's XCHG AX,CX, the flags kept.
XCHGAC:         PHP
                LDX     xAH
                LDY     xCH
                STY     xAH
                STX     xCH
                LDX     xAL
                LDY     xCL
                STY     xAL
                STX     xCL
                PLP
                RTS

; --- SCNOUT (SCNDRV.ASM 344) --------------------------------------------------------------

; SCNOUT: A (the 8086's AL) and xAH out at the cursor. A control character,
; ESC, or an editor function is done; anything else goes where CHWRAP says.
SCNOUT:         STA     xAL
; OO-1: PRINT's common case done at once, as CTLDSP, CHWRAP and SCROUT do it
; (the 30,000-cycle gate for 80 characters, V6): a printable character, not
; in the editor, no escape, no cursor shown, the place on an EOL-terminated
; line. The terminator moved out to it, LSTPOS and FSTPOS as CHWRP9 moves
; them, the cursor one on, the cell written in the row cache.
                LDX     F_EDIT
                BNE     SCNSLO
                LDX     ESCFLG
                BNE     SCNSLO
                LDX     xAH
                BNE     SCNSLO
                LDX     F_INST
                BNE     SCNSLO
                LDX     CSRFLG
                BNE     SCNSLO
                CMP     #' '
                BCC     SCNSLO
                CMP     #$FF
                BEQ     SCNSLO
                LDA     LINLEN
                CMP     CSRX
                BCC     SCNSLO
                LDX     CSRY
                LDA     TTTRM,X
                AND     #$7F
                CMP     #TRMEOL
                BNE     SCNSLO
                LDY     CSRX
                STX     xDL
                STY     xDH
                TYA
                CMP     TTCOL,X
                BCC     :+
                STA     TTCOL,X
:               CPX     LSTLNE
                BNE     :+
                CPY     LSTCOL
                BCC     :+
                STY     LSTCOL
:               CPX     FSTLIN
                BNE     :+
                CPY     FSTCOL
                BCS     :+
                STY     FSTCOL
:               INY
                STY     CSRX
                STZ     CSRTYP
                LDA     #$FF
                STA     F_CRET
                LDA     xAL
                JMP     SCROUT
SCNSLO:         LDA     xAL
                JSR     CTLDSP
                BCS     SCNOTX
                JSR     CHWRAP
                STZ     CSRTYP         ; the cursor off
                JSR     SETCSR
                LDA     xAL
                JSR     SCROUT
                LDA     #$FF
                STA     F_CRET
SCNOTX:         RTS

; MKRMCI: in insert mode, room for the character (AL its length less 1)
; at DX, and CX the terminators again (SCNRDX); else the flags as given.
MKRMCI:         PHP
                LDA     F_INST
                BEQ     @x
                PLP
                LDA     xAL
                INC     A
                STA     xCH
                JSR     MKRMCH
                JMP     SCNRDX
@x:             PLP
                RTS

; SCNRDX: CX DL's terminators, AX kept, the flags SCNRDT's.
SCNRDX:         PSH     xAH
                PSH     xAL
                JSR     SCNRDT
                MOVB    xCH, xAH
                MOVB    xCL, xAL
                PULB    xAL
                PULB    xAH
                LDA     xCL
                JMP     TFLG

; --- CHWRAP (SCNDRV.ASM 386) --------------------------------------------------------------

; CHWRAP: AX a character, DX the cursor: DX where it goes, the cursor and
; the terminators moved on (C set: no room, DX an earlier place). AX kept.
CHWRAP:         PSH     xAL
                PSH     xAH
                STZ     xCH
                LDA     xAH
                BEQ     CHWRP0
                LDA     #1
                STA     xCH
CHWRP0:         MOVB    xCL, xCH
                CLC
                ADC     xDH
                STA     xCH            ; the column after it
                JSR     SCNRDT
                PHP
                JSR     XCHGAC         ; AH the place, AL its length, CX the terminators
                LDA     LINLEN
                CMP     xAH
                BCC     CHWRP1         ; past the line
                PLP
                JSR     MKRMCI
                BCS     CHWPXI         ; EOL: here
                BEQ     CHWPLF
                LDA     xCL            ; (TRMWRP)
                BEQ     CHWPXI
                LDA     xAH
                CMP     LINLEN
                BCC     CHWPXI
                STZ     xCL            ; a null wrap overwritten: a wrap
CHWPXI:         JMP     CHWRPX
CHWPLF:         LDA     xAH
                CMP     LINLEN
                BCS     CHWPL1
                INC     xAH            ; ends one past the place
                JSR     XCHGAC
                LDA     xAH
                CMP     xCH
                PHP
                MOVB    xAH, xCH
                PLP
                BRA_A   CHWPL0
                JSR     SCNWTT
CHWPL0:         JSR     XCHGAC
                CLC
                PHP
                DEC     xAH
                JMP     CHWRP9
CHWPL1:         JSR     WRAPLF         ; the line feed to the next line
                BCS     CHWRNI
                CLC
                PHP
                MOVB    xAH, xCH
                JMP     CHWRP9
CHWRNI:         JMP     CHWRNO

CHWRP1:         PLP                    ; no room on this line
                BCS     CHWREL
                BEQ     CHWRLF
CHWRWP:         STZ     xCL            ; WRAP, NULL_WRAP: a wrap at the end
                MOVB    xCH, LINLEN
                JSR     XCHGAC
                JSR     SCNWTT
                LDA     xDL
                CMP     LSTLNE
                BNE     CHWRN0
                INC     LSTLNE
                STZ     LSTCOL
CHWRN0:         INC     xDL
                JSR     SCNRDT
                LDA     WDOLFT
                STA     xDH
                CLC
                ADC     xCL
                STA     xCH
                JSR     XCHGAC
                JSR     MKRMCI
                LDA     xCL
                CMP     #TRMLNF
                BNE     CHWRPX
                JSR     XCHGAC
                INC     xCH
                LDA     xAH
                CMP     xCH
                PHP
                MOVB    xAH, xCH
                PLP
                BRA_A   CHWRN2
                JSR     SCNWTT
CHWRN2:         DEC     xCH
                JSR     XCHGAC
CHWRPX:         CLC
                BRA     CHWRPZ
CHWREL:         JSR     MKRMNL         ; EOL: the next line made empty
                BCS     CHWRP3
                JSR     SCNRDX
                BRA     CHWRWP
CHWRLF:         JSR     XCHGAC         ; LF: it goes to the next line
                JSR     WRAPLF
                LDA     #TRMNWP
                STA     xAL
                MOVB    xAH, LINLEN
                JSR     SCNWTT
                JSR     XCHGAC
                JMP     CHWRWP         ; (SCNWTT's OR clears CF: the 8086's JNB always goes)
CHWRP3:         MOVB    xAH, LINLEN
CHWRNO:         LDA     xAH
                SEC
                SBC     xAL
                STA     xDH
                SEC
CHWRPZ:         PHP
                JSR     XCHGAC         ; CH the place, AX the terminators
                LDA     xAH
                CMP     xCH
                PHP
                MOVB    xAH, xCH
                PLP
                BRA_A   CHWRP9
                PSH     xAH
                PSH     xAL
                JSR     SCNRDT
                PULB    xAL
                PULB    xAH
                JSR     SCNWTT
CHWRP9:         LDA     xDL
                STA     CSRY
                CMP     LSTLNE
                BNE     CHWNTL
                LDA     xAH
                CMP     LSTCOL
                BCC     CHWNTL
                STA     LSTCOL
CHWNTL:         LDA     xDL
                CMP     FSTLIN
                BNE     CHWNTF
                LDA     xDH
                CMP     FSTCOL
                BCS     CHWNTF
                STA     FSTCOL
CHWNTF:         LDA     xAH
                INC     A
                STA     xAH
                STA     CSRX
                LDA     LINLEN
                CMP     xAH
                BCS     CHWNTG
                JSR     SCNRDT
                BCS     CHWNTG
                INC     CSRY           ; the logical line goes on
                MOVB    CSRX, WDOLFT
CHWNTG:         PLP
                PULB    xAH
                PULB    xAL
                RTS

; WRAPLF: the line feed at the end of DL moved to the next line (a space
; before it). DH and CX kept, DL perhaps moved, TRMCUR kept; C MKRMCH's.
WRAPLF:         PSH     TRMCUR
                PSH     xCH
                PSH     xCL
                PSH     xDH
                PSH     xDL
                MOVB    xDH, LINLEN
                LDA     #1
                STA     xCH
                JSR     MKRMCH
                PHP
                PLA
                STA     WRPF
                PLA                    ; the old DL dropped: DL as MKRMCH left it
                PULB    xDH
                PULB    xCL
                PULB    xCH
                PULB    TRMCUR
                LDA     WRPF
                PHA
                PLP
                RTS

; --- MKRMNL (SCNDRV.ASM 545) --------------------------------------------------------------

; MKRMNL: the line after DL made a blank, empty line, scrolling if it must.
; C set: no room. DL may change; AX, BX, CX kept.
MKRMNL:         PSH     xAH
                PSH     xAL
                PSH     xBH
                PSH     xBL
                PSH     xCH
                PSH     xCL
                MOVB    MKDH, xDH
                MOVB    MKDL, xDL
MKRMN0:         JSR     SCNRDT
                BCS     MKRMN5
                LDA     xDL
                CMP     WDOBOT
                BEQ     MKRMN7
                BCC     MKRMN4
                MOVB    xDL, WDOBOT
                BRA     MKRMN0
MKRMN4:         INC     xDL
                BRA     MKRMN0
MKRMN5:         LDA     xDL
                CMP     WDOBOT
                PHP
                MOVB    xDL, MKDL      ; the place to scroll from
                MOVB    xDH, MKDH
                PLP
                BEQ     MKRMN7
                BCS     MKRMNS         ; past the window: CF set
                LDA     xDL
                CMP     WDOTOP
                BCC     MKRMNS
                LDA     F_EDIT
                BEQ     MKRMNC         ; not editing: no need to scroll
                INC     xDL
                JSR     SCRLD0         ; scroll down from the next line
MKRMNC:         CLC
                BRA     MKRMNZ
MKRMNS:         SEC
MKRMNZ:         PHP
                MOVB    xDL, MKDL
                MOVB    xDH, MKDH
                PLP
                BRA     MKRMNX
MKRMN7:         MOVB    xDL, MKDL
                LDA     F_EDIT
                BEQ     MKRMN9
MKRMN8:         JSR     CSRUP          ; editing: not if the line starts at the top
                BCS     MKRMNS
                PSH     xAH
                PSH     xAL
                JSR     SCNRDT
                PLA
                STA     xAL
                PLA
                STA     xAH            ; (the 8086's POP keeps SCNRDT's CF)
                LDX     TRMCUR
                LDA     TTTRM,X
                AND     #$7F
                CMP     #TRMEOL
                BNE     MKRMN8
MKRMN9:         MOVB    xDL, MKDL
                MOVB    xDH, MKDH
                LDA     xDL
                CMP     WDOBOT
                BCS     MKRMNF
                JSR     SCRLUD
                BRA     MKRMNU
MKRMNF:         JSR     SCRLUPN        ; (MKRMNX puts AX, BX, CX back; MKRMNU DX)
MKRMNU:         MOVB    xDL, MKDL
                MOVB    xDH, MKDH
                JSR     CSRUP
                CLC
MKRMNX:         PULB    xCL            ; (PLA keeps C)
                PULB    xCH
                PULB    xBL
                PULB    xBH
                PULB    xAL
                PULB    xAH
                RTS

; --- Scrolling (SCNDRV.ASM 623) -----------------------------------------------------------

; SCRLUD: up from DL to the window's top. SCRLUP: the whole window up.
; SCRLU0: up from DL to its bottom. DX, AX, BX, CX kept.
SCRLUD:         PSH     xCL
                MOVB    xCL, xDL
                MOVB    xDL, WDOTOP
                BRA     SCRLU1
; SCRLUPN (OO-1): SCRLUP for MKRMNL alone, which keeps its own registers:
; none kept here (the 13,944-cycle scroll's last trim, the owner's answer 4).
SCRLUPN:        MOVB    xDL, WDOTOP
                MOVB    xCL, WDOBOT
                LDA     #1
                STA     NOSAV
                JMP     SCRLU2
SCRLUP:         MOVB    xDL, WDOTOP
SCRLU0:         PSH     xCL
                MOVB    xCL, WDOBOT
SCRLU1:         PSH     xCH
                PSH     xAH
                PSH     xAL
                PSH     xBH
                PSH     xBL
                PSH     xDH
                PSH     xDL
SCRLU2:         LDA     xDL
                STA     xAL
                STA     xBL
                INC     xAL            ; from = to + 1
                LDA     WDOLFT
                STA     xBH
                STA     xAH
                SEC
                LDA     xCL
                SBC     xBL
                STA     xCL            ; the lines
                BEQ     @one
                BCC     SCRUL4         ; top > bottom (never)
                PSH     xAL
                MOVB    xCH, CRTWID
                JSR     SCROLL
                CLC
                LDA     xAL
                ADC     xCL
                SEC
                SBC     #1
                STA     xAL            ; the bottom line scrolled
                LDA     #$FF
                STA     xAH            ; their line numbers down one
                JSR     TSTSCR
                MOVB    xDL, xAL
                BRA     SCRUL3
@one:           PSH     xAL
                MOVB    xDL, xBL
SCRUL3:         MOVB    xDH, WDOLFT
                PULB    xAL
                LDA     WDOLFT         ; (the 8086's POP AX: AH from, AL from-line)
                STA     xAH
                LDA     xCL
                BEQ     SCRUL4
                JSR     SCRUTT
SCRUL4:         JSR     PLINIT
                LDA     NOSAV
                BEQ     :+
                STZ     NOSAV
                RTS
:               PULB    xDL
                PULB    xDH
                PULB    xBL
                PULB    xBH
                PULB    xAL
                PULB    xAH
                PULB    xCH
                PULB    xCL
                RTS

; TSTSCR: after a scroll of lines BL to AL, CSRY, FSTLIN and LSTLNE in it
; moved by AH (1 or -1), CSRY kept in the window.
TSTSCR:         LDX     #0             ; CSRY
                LDA     xAL            ; (TSTWDO inline)
                CMP     CSRY
                BCC     :+
                LDA     CSRY
                CMP     xBL
                BCC     :+
                CLC
                ADC     xAH
                STA     CSRY
:                LDA     CSRY
                CMP     WDOTOP
                BCC     @top
                LDA     WDOBOT
                CMP     CSRY
                BCS     @ok
                STA     CSRY
                BRA     @ok
@top:           MOVB    CSRY, WDOTOP
@ok:            LDX     #FSTLIN - CSRY
                JSR     TSTWDO
                LDX     #LSTLNE - CSRY
; TSTWDO: if BL <= CSRY+X <= AL, CSRY+X moved by AH.
TSTWDO:         LDA     xAL
                CMP     CSRY,X
                BCC     @x
                LDA     CSRY,X
                CMP     xBL
                BCC     @x
                CLC
                ADC     xAH
                STA     CSRY,X
@x:             RTS

; SCRLDN: the window down. SCRLD0: down from DL to its bottom. SCRDLN: DL
; down to the next line. AX, BX, CX kept.
SCRLDN:         MOVB    xDL, WDOTOP
SCRLD0:         PSH     xAH
                PSH     xAL
                PSH     xBH
                PSH     xBL
                PSH     xCH
                PSH     xCL
                LDA     xDL
                STA     xAL
                STA     xBL
                INC     xBL            ; to = from + 1
                LDA     WDOLFT
                STA     xBH
                STA     xAH
                SEC
                LDA     WDOBOT
                SBC     xAL
                STA     xCL
                PHP
                PSH     xAL
                PLA
                PLP
                PHA
                BEQ     SCRDL3
                BCC     SCRDL3         ; (JBE: a null scroll)
                MOVB    xCH, WDORGT
SCRLD1:         JSR     SCROLL
                CLC
                LDA     xAL
                ADC     xCL
                SEC
                SBC     #1
                STA     xAL
                LDA     #1
                STA     xAH
                JSR     TSTSCR
SCRDL3:         MOVB    xDH, WDOLFT
                PULB    xAL            ; the from-line
                LDA     WDOLFT
                STA     xAH
                LDA     xCL
                BMI     SCRDL4         ; out of the window
                BEQ     SCRDL4
                JSR     SCRDTT
SCRDL4:         JSR     PLINIT
                PULB    xCL
                PULB    xCH
                PULB    xBL
                PULB    xBH
                PULB    xAL
                PULB    xAH
                RTS
SCRDLN:         PSH     xAH
                PSH     xAL
                PSH     xBH
                PSH     xBL
                PSH     xCH
                PSH     xCL
                LDA     xDL
                STA     xAL
                STA     xBL
                INC     xBL
                LDA     WDOLFT
                STA     xAH
                STA     xBH
                MOVB    xCH, WDORGT
                LDA     #1
                STA     xCL
                PSH     xAL
                JMP     SCRLD1

; SCRUTT: the terminators of lines BL+1 .. BL+CL up one, the last EOL.
SCRUTT:         LDX     xBL
                STX     TRMCUR
                LDY     xCL
@l4:            CPY     #4             ; (four lines a turn, then one)
                BCC     @l
                LDA     TTCOL+1,X
                STA     TTCOL+0,X
                LDA     TTTRM+1,X
                STA     TTTRM+0,X
                LDA     TTCOL+2,X
                STA     TTCOL+1,X
                LDA     TTTRM+2,X
                STA     TTTRM+1,X
                LDA     TTCOL+3,X
                STA     TTCOL+2,X
                LDA     TTTRM+3,X
                STA     TTTRM+2,X
                LDA     TTCOL+4,X
                STA     TTCOL+3,X
                LDA     TTTRM+4,X
                STA     TTTRM+3,X
                TXA
                CLC
                ADC     #4
                TAX
                TYA
                SEC
                SBC     #4
                TAY
                BNE     @l4
                BEQ     @e
@l:             LDA     TTCOL+1,X
                STA     TTCOL,X
                LDA     TTTRM+1,X
                STA     TTTRM,X
                INX
                DEY
                BNE     @l
@e:
                STZ     TTCOL,X
                LDA     #TRMEOL
                STA     TTTRM,X
                RTS

; SCRDTT: the terminators of lines AL .. AL+CL-1 down one, AL's EOL, and
; the window's last line EOL (less its line feed). DX kept.
SCRDTT:         PSH     xDL
                CLC
                LDA     xAL
                ADC     xCL
                TAX
                STX     TRMCUR
                LDY     xCL
@l:             DEX
                LDA     TTCOL,X
                STA     TTCOL+1,X
                LDA     TTTRM,X
                STA     TTTRM+1,X
                DEY
                BNE     @l
                STZ     TTCOL,X
                LDA     #TRMEOL
                STA     TTTRM,X
                MOVB    xDL, WDOBOT
                JSR     SCNRTL
                BCS     @e
                BNE     @e
                DEC     xAH
@e:             LDA     #TRMEOL
                STA     xAL
                JSR     SCNWTT
                PULB    xDL
                RTS

; PLINIT: line DL ended just left of DH (EOL), then cleared from DH on.
; AX, CX kept.
PLINIT:         PSH     xAH
                PSH     xAL
                JSR     SCNRDT
                LDX     TRMCUR
                LDA     #TRMEOL
                STA     TTTRM,X
                LDA     xDH
                DEC     A
                STA     TTCOL,X
                PULB    xAL
                PULB    xAH
                JMP     CLREOL

; --- SCNMRK (SCNDRV.ASM 848) --------------------------------------------------------------

; SCNMRK: A the 8086's AL (1: EDIT's line, the cursor to its start), MKINP
; nonzero for an INPUT statement (the 8086's ZF). The line made a logical
; line's start, a "not typed" line ($FF in it) deleted, and FSTPOS, LSTPOS
; set: the cursor for INPUT, the whole logical line else.
SCNMRK:         TAX
                MOVB    xDL, CSRY
                MOVB    xDH, CSRX
                DEX
                BNE     SCNMK0
                JSR     LSTART
                BRA     SCNMK2
SCNMK0:         JSR     CSRUP
                BCS     SCNMK1
                JSR     SCNRTL         ; the line before ends here
                LDA     #TRMEOL
                STA     xAL
                JSR     SCNWTT
                INC     xDL
SCNMK1:         JSR     SCNRDT
                LDX     TRMCUR
                LDA     TTTRM,X
                AND     #TRMNUL
                BEQ     SCNMK2
                PSH     xDH
                PSH     xCL
                MOVB    xDH, WDOLFT
                MOVB    xCL, LINLEN
SCNMKL:         JSR     SCRINP
                CMP     #$FF
                BEQ     @f
                INC     xDH
                DEC     xCL
                BNE     SCNMKL
                STZ     MKFND          ; (the 8086's OR SP,SP: not on the line)
                BRA     SCNML3
@f:             LDA     #1
                STA     MKFND
SCNML3:         PULB    xCL
                PULB    xDH
                LDA     MKFND
                BEQ     SCNMK2
                JSR     LDELET
SCNMK2:         MOVB    CSRY, xDL
                MOVB    CSRX, xDH
                LDA     MKINP
                BNE     SCNMK3
                LDA     #$FF
                STA     LSTLNE
                STZ     FSTLIN
                RTS
SCNMK3:         LDA     xDL
                STA     LSTLNE
                STA     FSTLIN
                LDA     xDH
                STA     FSTCOL
                DEC     A
                STA     LSTCOL
                RTS

; --- SCNRDL (SCNDRV.ASM 911) --------------------------------------------------------------

; SCNRDL: the logical line at DX read from the cells into BUF from xBL (its
; index), at most xCL characters; MKINP the 8086's ZF (nonzero: INPUT, so
; FSTPOS and LSTPOS bound it; 0: a statement, the whole logical line).
; xBL returns past the last character.
SCNRDL:
SCRD00:         LDA     xDL
                CMP     FSTLIN
                BEQ     SCRD01
                JSR     CSRUP
                BCS     SCRD01
                JSR     SCNRDT
                BCC     SCRD00
                INC     xDL
SCRD01:         MOVB    xDH, WDOLFT
                LDA     MKINP
                BEQ     SCRD02
                LDA     xDL
                CMP     FSTLIN
                BNE     SCRD02
                MOVB    xDH, FSTCOL
SCRD02:         LDA     MKINP
                BEQ     SCRD03
                LDA     xDL
                CMP     LSTLNE
                BRA_AL  SCRDXZ
                BNE     SCRD03
                LDA     xDH
                CMP     LSTCOL
                BRA_AL  SCRDXZ
SCRD03:         JSR     SCNRDT
                PSH     xAH
                PSH     xAL
                LDA     xAL            ; SCNRDT's flags again (the 8086's PUSH keeps them)
                JSR     TFLG
                BCS     SCRD04
                BNE     SCRD04
                LDA     xAH
                CMP     xDH
                BNE     SCRD04
                LDA     #CHRLNF        ; at its line feed: a line feed
                BRA     SCRD08
SCRD04:         LDA     xAH
                CMP     xDH
                BRA_A   SCRD07
                BCC     SCRD06
                LDA     xAL
                CMP     #TRMNWP
                BNE     SCRD07
SCRD06:         PULB    xAL
                PULB    xAH
                LDA     xAL
                CMP     #TRMEOL
                BEQ     SCRDXZ
                INC     xDL            ; a wrap: on to the next line
                MOVB    xDH, WDOLFT
                BRA     SCRD02
SCRD07:         JSR     SCRINP
SCRD08:         LDX     xBL
                STA     BUF,X
                PULB    xAL
                PULB    xAH
                DEC     xCL
                BEQ     SCRDEX
                LDA     xAL
                CMP     #TRMEOL
                BNE     SCRD09
                LDA     xAH
                CMP     xDH
                BRA_BE  SCRDEX
SCRD09:         INC     xBL
                JSR     CSRADV
                BCS     SCRDXZ
                JMP     SCRD02
SCRDEX:         INC     xBL
SCRDXZ:         JMP     SCNBRK

; --- Control characters (SCNDRV.ASM 1020) -------------------------------------------------

; CTLDSP: the character in AX (xAH, xAL) at the cursor: C set, nothing to
; print (a control character or an editor function done, the cursor moved;
; ESC); C clear, AX the character to print. DX the cursor.
CTLDSP:         MOVB    xDL, CSRY
                STZ     CSRTYP         ; the cursor off
                JSR     SETCSR
                MOVB    xDH, CSRX      ; the true place, not SCNPOS's
                LDA     ESCFLG
                BNE     DOESC
                LDA     xAL
                CMP     #27
                BEQ     DOESC
                LDA     F_EDIT
                BEQ     CTLPRT
                JSR     EDTMAP         ; the editor's map
                BRA     CTLEDT
CTLPRT:         JSR     PRTMAP         ; PRINT's
CTLEDT:         BEQ     CTLDPY         ; nothing
                LDA     xAH
                CMP     #$FF
                BNE     CTLDSX         ; a character: printed
                STZ     xAH
                LDA     xAL
                CMP     #CHRDEL
                BNE     CTLNDL
                LDA     #' '           ; DEL: the table's last
                STA     xAL
CTLNDL:         CMP     #$FF
                BNE     CTLNMK
                JSR     SCNRDT         ; $FF: the line marked "not typed",
                LDX     TRMCUR         ; and the $FF printed (CF clear)
                LDA     TTTRM,X
                ORA     #TRMNUL
                STA     TTTRM,X
                LDA     #$FF
                STA     xAL
                STZ     xAH
                CLC
                RTS
CTLNMK:         CMP     #' ' + 1
                BCS     CTLDPY
                PHA
                JSR     CTLIRS
                ASL     A
                TAX
                JSR     CTLGO
CTLDPX:         MOVB    CSRY, xDL
                MOVB    CSRX, xDH
                PLA
                SEC
                SBC     #CHRRET
                STA     F_CRET         ; 0: the last was a CR
CTLDPY:         SEC
CTLIGN:         RTS
CTLDSX:         CLC
                RTS
CTLGO:          JMP     (FUNTAB,X)
; DOESC: the OEM's escape sequences; the OO-1 has none (SCROUT clears C),
; so ESC is SCROUT's at the cursor, which doesn't move.
DOESC:          LDA     xAL
                JSR     SCROUT
                STZ     ESCFLG
                SEC
                RTS

; CTLIRS: these functions end insert mode. A kept.
CTLIRS:         LDX     #CTLCNT - 1
@l:             CMP     CTLITB,X
                BEQ     @f
                DEX
                BPL     @l
                RTS
@f:             STZ     F_INST
                RTS

; --- The cursor's moves (SCNDRV.ASM 1180) -------------------------------------------------

WCSADV:         JSR     CSRADV
                BCS     WCSABT
; UPDPOS: FSTPOS and LSTPOS moved out to the cursor, while it is on their
; logical line. DX kept.
UPDPOS:         PSH     xDH
                PSH     xDL
                DEC     xDH            ; LSTCOL is left of the cursor
                LDA     xDL
                CMP     LSTLNE
                BCC     UPDPS3
                BEQ     UPDPS2
                PSH     xDH
                PSH     xDL
                JSR     LSTART
                MOVB    UPTMP, xDL
                PULB    xDL
                PULB    xDH
                LDA     UPTMP
                CMP     LSTLNE
                BRA_A   UPDPS3         ; LSTPOS isn't on this logical line
                MOVB    LSTLNE, xDL
                MOVB    LSTCOL, xDH
UPDPS2:         LDA     xDH
                CMP     LSTCOL
                BRA_BE  UPDPS3
                STA     LSTCOL
UPDPS3:         INC     xDH
                LDA     xDL
                CMP     FSTLIN
                BNE     UPDPSX
                LDA     xDH
                CMP     FSTCOL
                BCS     UPDPSX
                STA     FSTCOL
UPDPSX:         PULB    xDL
                PULB    xDH
WCSABT:         RTS

WCSREG:         JSR     CSRREG
                BCS     WCSABT
WCSRET:         JSR     SCNRDT
                LDA     xAH
                CMP     xDH
                BCS     UPDPOS
                RTS
WCSUP:          JSR     CSRUP
                BCC     WCSRET
                RTS
WCSDWN:         JSR     CSRDWN
                BCC     WCSRET
                RTS

; LTAB: to the next tab stop; in insert mode, blanks inserted to it.
LTAB:           LDA     F_INST
                BEQ     LTABL
LTABIL:         STZ     xAH
                LDA     #' '
                JSR     SCNOUT
                MOVB    xDL, CSRY
                MOVB    xDH, CSRX
                LDA     xDH
                CMP     LINLEN
                BRA_A   LTABZ
                DEC     xDH
                LDA     xDH
                AND     #7
                BNE     LTABIL
                INC     xDH
                BRA     LTABZ
LTABL:          LDA     xDH
                CMP     LINLEN
                BCS     LTABX0
                INC     xDH
                LDA     xDH
                AND     #7
                BNE     LTABL
                INC     xDH
                BRA     LTABX
LTABX0:         LDA     xDL
                CMP     WDOBOT
                BCS     LTABZ
                INC     xDL
                MOVB    xDH, WDOLFT
LTABX:          JSR     UPDPOS
LTABZ:          RTS

; --- Words (SCNDRV.ASM 1290) --------------------------------------------------------------

FWDWRD:         PSH     xDH
                PSH     xDL
                PSH     LSTCOL
                PSH     LSTLNE
                LDA     xDL
                CMP     LSTLNE
                BNE     FWDWR0
                LDA     xDH
                CMP     LSTCOL
                BCC     FWDWR0
                LDA     #$FF           ; LSTPOS left out of the scan
                STA     LSTLNE
FWDWR0:         CLC
FWDWR1:         JSR     FWDWDL
                BCS     FWDWR2
                PULB    LSTLNE
                PULB    LSTCOL
                JSR     UPDPOS
                PLA
                PLA
                RTS
FWDWR2:         JSR     CSRDWN         ; the next logical line
                BCS     FWDWRZ
                MOVB    xDH, WDOLFT
                SEC
                BRA     FWDWR1
FWDWRZ:         PULB    LSTLNE
                PULB    LSTCOL
                PULB    xDL
                PULB    xDH
                RTS

FWDWD0:         JSR     LCSADV
                BCS     FWDWD3
; FWDWDL: to the next word's start in the logical line (C set on entry: as
; if between words). C set: none.
FWDWDL:         BCS     FWDWD2
                JSR     ANCHK
                BCC     FWDWD0
FWDWD1:         JSR     LCSADV
                BCS     FWDWD3
FWDWD2:         JSR     ANCHK
                BCS     FWDWD1
FWDWDX:         RTS
FWDWD3:         BNE     FWDWDX         ; (C set)
                LDA     xAH
                CMP     xDH
                BRA_A   FWDWD4
                SEC
                RTS
FWDWD4:         LDA     #$FF
                STA     LSTLNE
                BRA     FWDWD1

BCKWRD:         PSH     FSTLIN
                PSH     FSTCOL
                PSH     xDH
                PSH     xDL
                LDA     xDH            ; (the source compares DH with FSTLIN)
                CMP     FSTLIN
                BNE     BCKWD2
                LDA     xDH
                CMP     FSTCOL
                BRA_A   BCKWD2
                STZ     FSTLIN
BCKWD2:         JSR     LCSREG
                BCS     BCKWD7
BCKWD4:         JSR     ANCHK
                BCS     BCKWD2
BCKWD5:         JSR     LCSREG
                BCS     BCKWD6
                JSR     ANCHK
                BCC     BCKWD5
                JSR     CSRADV
BCKWD6:         PLA
                PLA
                PSH     xDH
                PSH     xDL
BCKWDX:         PULB    xDL
                PULB    xDH
                PULB    FSTCOL
                PULB    FSTLIN
                JMP     UPDPOS
BCKWD7:         BNE     BCKWD8
                STZ     FSTLIN
                BRA     BCKWD2
BCKWD8:         JSR     CSRUP
                BCS     BCKWDX
                JSR     SCNRDT
                LDA     xAH
                BEQ     BCKWD8
                STA     xDH
                BRA     BCKWD4

; --- Home, clear, erase, truncate (SCNDRV.ASM 1404) ---------------------------------------

; CBEEP: the bell (BEEP is 7f's: OO-1, nothing yet), then SCNPOS.
CBEEP:          JMP     SCNPOS

CCLRSN:         CLC
                LDA     #2
                JSR     CLRSCN
WHOME:          MOVB    xDL, WDOTOP
                MOVB    xDH, WDOLFT
                RTS

WERASE:         JSR     PLINIT
                PSH     xDH
                PSH     xDL
                MOVB    xDH, WDOLFT
@l:             INC     xDL
                LDA     WDOBOT
                CMP     xDL
                BCC     @x
                JSR     PLINIT
                BRA     @l
@x:             PULB    xDL
                PULB    xDH
                RTS

LDELET:         JSR     LSTART
LTRUNC:         PSH     xDH
                PSH     xDL
                JSR     SCNRTL
                PHP
                SEC
                JSR     LCLEAR
                BCS     LTRUN9
                INC     xDL
LTRUN1:         PLP
                BCS     LTRUNX
                JSR     SCNRTL
                PHP
                MOVB    xDH, WDOLFT
                CLC
                JSR     LCLEAR
                BCS     LTRUN9
                BRA     LTRUN1
LTRUN9:         PLP
LTRUNX:         PULB    xDL
                PULB    xDH
                RTS

; LCLEAR: DL cleared from DH to its end or LSTPOS (C set: by PLINIT, else
; by scrolling the line away). C set: LSTPOS reached.
LCLEAR:         PHP
                LDA     xDL
                CMP     LSTLNE
                BEQ     LCLER2
                LDA     WDORGT
                CMP     xDH
                BCC     LCLER5
                PLP
                BCC     LCLER1
                JSR     PLINIT
                CLC
                RTS
LCLER1:         JSR     SCRLU0
                CLC
                RTS
LCLER2:         PLP
                JSR     PLINIT
                SEC
                RTS
LCLER5:         PLP
                CLC
                RTS

; --- Deleting (SCNDRV.ASM 1460) -----------------------------------------------------------

WDELET:         LDA     xDH
                CMP     LINLEN
                BRA_BE  WDELT0
                JSR     CSRADV
                JCS     WDELTZ
WDELT0:         LDA     xDL
                CMP     LSTLNE
                BNE     WDELT2
                LDA     xDH
                CMP     LSTCOL
                JCS     WDELTZ
WDELT2:         PSH     xDH
                PSH     xDL
                LDA     xDH
                CMP     LINLEN
                BRA_BE  WDELT4
                JSR     LCSADV
WDELT4:         PSH     xDH
                PSH     xDL
                PSH     LSTCOL
                PSH     LSTLNE
                CLC
                JSR     FWDWDL
                PULB    LSTLNE         ; (PLA keeps C)
                PULB    LSTCOL
                BCC     WDELT6
                INC     xDH            ; no word: to the line's end, its terminator too
WDELT6:         MOVB    xCL, xDL
                MOVB    xCH, xDH
                PULB    xDL
                PULB    xDH
                LDA     xCL
                CMP     LSTLNE
                BCC     WDELT8
                BNE     WDELT7
                LDA     xCH
                CMP     LSTCOL
                BCC     WDELT8
WDELT7:         MOVB    xCL, LSTLNE
                LDA     LSTCOL
                INC     A
                STA     xCH
WDELT8:         LDA     xDL
                CMP     xCL
                BNE     WDELTC
WDELT9:         SEC
                LDA     xCH
                SBC     xDH
                STA     xCH
                BCC     WDELTX
                BEQ     WDELTX
                JSR     CDELLG
WDELTX:         PULB    xDL
                PULB    xDH
WDELTZ:         RTS
WDELTC:         INC     xDL
WDELTD:         PSH     xDH
                PSH     xDL
                PSH     xCH
                PSH     xCL
                MOVB    xDH, WDOLFT
                LDA     xDL
                CMP     xCL
                BEQ     WDELTE
                JSR     SCNRTL
                MOVB    xCH, xAH
                JSR     CDELPH
                PULB    xCL
                PULB    xCH
                PULB    xDL
                PULB    xDH
                DEC     xCL
                BRA     WDELTD
WDELTE:         LDA     xCH
                CMP     WDOLFT
                BRA_BE  WDELTF
                DEC     xCH
                JSR     CDELLG
WDELTF:         PULB    xCL
                PULB    xCH
                PULB    xDL
                PULB    xDH
                DEC     xDL
                LDA     LINLEN
                INC     A
                STA     xCH
                JMP     WDELT9

BAKSPC:         JSR     LCSREG
                JSR     SCNRTL
                LDA     xDH
                CMP     xAH
                BRA_BE  CDELET
                LDA     xAH
                CMP     WDOLFT
                BCS     BAKSP1
                MOVB    xAH, WDOLFT
BAKSP1:         MOVB    xDH, xAH
CDELET:         LDA     #1
                STA     xCH
                LDA     xDH
                CMP     LINLEN
                BRA_BE  CDELLG
                RTS

; CDELLG: CH characters deleted at DX in the logical line, its later lines'
; characters pulled back (CDUNWP). DX kept.
CDELLG:         PSH     xDH
                PSH     xDL
                JSR     SCNRTL
                PHP
                JSR     CDELPH
                STZ     xCL            ; the lines gone on
                PLP
                BCS     @eol
                BNE     CDLGWP
                LDA     #TRMLNF
                STA     xAL
                LDA     xAH
                CMP     xDH
                BRA_AL  CDLGEX
                LDA     xAH
                JEQ     CDLGRT
                MOVB    xAH, xDH
                BRA     CDLWP1
@eol:           LDA     #TRMEOL
                STA     xAL
                JMP     CDLGEX
CDLGWP:         LDA     xAH
                JEQ     CDLGRT
                INC     xAH
CDLWP1:         LDA     xDL
                CMP     LSTLNE
                JEQ     CDLGRT
                PSH     LSTCOL
                PSH     LSTLNE
                JSR     CDUNWP
                JSR     SCNWTT
                LDA     xCH
                BNE     CDLWP2
                PLA
                PLA
                JMP     CDLGRT
CDLWP2:         MOVB    xDH, WDOLFT
                INC     xDL
                DEC     xCL
                JSR     SCNRTL
                PHP
                PLA
                STA     CDF1           ; SCNRTL's flags
                MOVB    CDLL, LSTLNE   ; LSTPOS as CDUNWP left it
                MOVB    CDLC, LSTCOL
                PULB    LSTLNE         ; and as it was, for CDELPH
                PULB    LSTCOL
                LDA     CDF1
                PHA
                PSH     CDLC
                PSH     CDLL
                LDA     CDLL
                CMP     LSTLNE
                PHP
                LDA     CDF1           ; SCNRTL's flags for CDELPH's caller's tests
                PHA
                PLP
                JSR     CDELPH
                PLA                    ; did CDUNWP move LSTPOS?
                STA     CDF2
                PULB    CDLL
                PULB    CDLC
                LDA     CDF2
                AND     #$02
                BNE     CDLGNC
                MOVB    LSTLNE, CDLL
                MOVB    LSTCOL, CDLC
CDLGNC:         LDA     xAH
                BNE     CDLGW1
                PLP
                BCC     CDLGRT
CDLGNL:         DEC     xDL
                INC     xCL
                JSR     SCNRTL
                BCS     CDLGN0
                BEQ     CDLGRT
                LDA     #TRMEOL
                STA     xAL
                JSR     SCNWTT
CDLGN0:         INC     xDL
                DEC     xCL
                JSR     SCRLU0         ; the null line scrolled away
                BRA     CDLGRT
CDLGW1:         PLP
                BCS     CDLGEX
                JNE     CDLGWP
CDLGEX:         JSR     SCNWTT
CDLGRT:         CLC
                LDA     xCL
                ADC     xDL
                STA     xCL
                PULB    xDL
                PULB    xDH
                MOVB    xDL, xCL
                RTS

; CDUNWP: characters from the line after DL pulled back to (AH, DL): AX
; the new terminator, CH their count. BX, DX, CL and TRMCUR kept.
CDUWXI:         STZ     xCH
                JMP     CDUWPX
CDUNWP:         PSH     xBH
                PSH     xBL
                PSH     xCH
                PSH     xCL
                PSH     xDH
                PSH     xDL
                PSH     TRMCUR
                LDA     #TRMWRP
                STA     xAL
                SEC
                LDA     LINLEN
                SBC     xAH
                STA     xCH
                BCC     CDUWXI         ; no room
                LDA     xDL
                CMP     LSTLNE
                BNE     CDUWP0
                LDA     LSTCOL
                BNE     CDUWP0
                MOVB    xAH, LINLEN
                BRA     CDUWXI
CDUWP0:         INC     xCH
                MOVB    xBH, xAH       ; the destination
                MOVB    xBL, xDL
                INC     xDL
                JSR     SCNRTL
                PHP
                PLA
                STA     CDUF
                PSH     xAH
                PSH     xAL
                LDA     CDUF
                PHA
                LDA     xAH
                CMP     xCH
                BCS     CDUWP1
                MOVB    xCH, xAH       ; all the next line has
CDUWP1:         LDA     #1
                STA     xCL
                MOVB    xDH, WDOLFT
                MOVB    xAH, xDH       ; the source
                MOVB    xAL, xDL
                LDA     xCH
                BNE     CDUWPN
                PLP
                PHP
                BEQ     CDUWP4         ; LF: the null line kept
                PSH     TRMCUR
                JSR     SCRLU0
                PULB    TRMCUR
                BRA     CDUWP4
CDUWPN:         JSR     SCROLL
                LDA     xDL
                CMP     LSTLNE
                BNE     CDUWP4
                CLC
                LDA     xAH
                ADC     xCH
                SEC
                SBC     LSTCOL
                BCC     CDUWP4
                BEQ     CDUWP4
                SEC                    ; LSTPOS pulled back with them
                LDA     LSTCOL
                SBC     xAH
                STA     LSTCOL
                BCS     CDUWPL
                STZ     LSTCOL
CDUWPL:         CLC
                LDA     LSTCOL
                ADC     xBH
                STA     LSTCOL
                DEC     LSTLNE
CDUWP4:         CLC
                LDA     xBH
                ADC     xCH
                SEC
                SBC     #1
                STA     xBH
                PLA
                STA     CDUF
                PULB    xAL
                PULB    xAH
                LDA     CDUF
                PHA
                PLP
                BCS     CDUWPE
                BEQ     CDUWP6L
CDUWP5:         LDA     #TRMWRP
                STA     xBL
                LDA     xBH
                CMP     LINLEN
                BEQ     CDUWP7
                LDA     #TRMNWP
                STA     xBL
                MOVB    xBH, LINLEN
                BRA     CDUWP7
CDUWPE:         LDA     #TRMEOL
                STA     xBL
                LDA     xAH
                BEQ     CDUWP7
                BRA     CDUWP6
CDUWP6L:        LDA     #TRMLNF
                STA     xBL
CDUWP6:         LDA     xAH
                CMP     xCH
                BNE     CDUWP5
CDUWP7:         MOVB    xAH, xBH
                MOVB    xAL, xBL
CDUWPX:         PULB    TRMCUR
                PULB    xDL
                PULB    xDH
                PULB    xCL
                PLA                    ; CH: the count
                PULB    xBL
                PULB    xBH
                RTS

; CDELPH: CH characters deleted at DX within the physical line, AX its
; terminator: AH the new terminator's column. BX, CX, DX kept.
CDELPH:         MOVB    CPAH, xAH
                MOVB    CPAL, xAL
                PSH     xBH
                PSH     xBL
                PSH     xCH
                PSH     xCL
                PSH     xDH
                PSH     xDL
                PSH     xAH
                PSH     xAL
                LDA     xDH
                CMP     xAH
                BRA_AL  CDEL9I         ; at or past the line's end
                MOVB    xBH, xDH       ; the destination
                MOVB    xBL, xDL
                MOVB    xAL, xDL       ; the source
                CLC
                LDA     xDH
                ADC     xCH
                STA     xAH
                MOVB    xCH, CPAH      ; CH the line's end
                MOVB    xCL, CPAL
                LDA     xAH
                CMP     xCH
                BRA_BE  CDELP0
                LDA     xCH
                INC     A
                STA     xAH
CDELP0:         LDA     xDL
                CMP     LSTLNE
                BNE     CDELP1
                LDA     xCH
                CMP     LSTCOL
                BRA_BE  CDELP1
                MOVB    xCH, LSTCOL
                LDA     xDH
                CMP     xCH
                BRA_A   CDEL9I
CDELP1:         MOVB    CPCH, xCH      ; the source's last column
                SEC
                LDA     xCH
                SBC     xAH
                STA     xCH
                PHP
                CLC
                LDA     xBH
                ADC     xCH
                STA     xDH
                PLP
                BCC     CDELP4         ; the source past the end
                LDA     #1
                STA     xCL
                INC     xCH
                JSR     SCROLL         ; the characters moved left
CDELP4:         MOVB    xCH, CPCH
                INC     xDH
                MOVB    CPDH, xDH
CDELP5:         LDA     xDH            ; blanks to the source's end
                CMP     xCH
                BRA_A   CDELP6
                LDA     #' '
                JSR     SCROUT
                INC     xDH
                BRA     CDELP5
CDEL9I:         JMP     CDELP9
CDELP6:         MOVB    xDH, CPDH
                MOVB    xAH, CPAH
                MOVB    xAL, CPAL
                PLA                    ; the terminator passed, off the stack
                PLA
                LDA     xDL
                CMP     LSTLNE
                BNE     CDELP7
                LDA     xCH
                CMP     LSTCOL
                BCC     CDELP7
                PULB    xDL            ; LSTPOS in it: LSTCOL less the count
                PULB    xDH
                PULB    xCL
                PULB    xCH
                SEC
                LDA     LSTCOL
                SBC     xCH
                STA     LSTCOL
                BCS     CDEL8A
                STZ     LSTCOL
                BRA     CDEL8A
CDELP7:         LDA     xDH
                DEC     A
                STA     xAH
CDELP8:         PULB    xDL
                PULB    xDH
                PULB    xCL
                PULB    xCH
CDEL8A:         LDA     xDH
                CMP     xAH
                BRA_BE  CDELPX
                LDA     xDH
                CMP     #1
                BNE     CDELPX
                LDA     xAL
                CMP     #TRMEOL
                BEQ     CDELPX
                PSH     TRMCUR         ; a null line: scrolled away
                LDA     xDL
                CMP     LSTLNE
                BNE     CDEL8B
                DEC     LSTLNE
                MOVB    LSTCOL, WDOLFT
CDEL8B:         JSR     SCRLU0
                PULB    TRMCUR
                BRA     CDELPX
CDELP9:         PULB    xAL
                PULB    xAH
                MOVB    xDH, xAH
                LDA     xAL
                CMP     #TRMLNF
                BEQ     CDELP7         ; the line feed deleted
                PULB    xDL
                PULB    xDH
                PULB    xCL
                PULB    xCH
CDELPX:         PULB    xBL
                PULB    xBH
                RTS

; --- Append, CR, LF (SCNDRV.ASM 1823) -----------------------------------------------------

LAPPND:
LAPPN0:         JSR     SCNRDT
                BCS     LAPPN1
                INC     xDL
                BRA     LAPPN0
LAPPN1:         MOVB    xDH, xAH
                LDA     xAH
                CMP     LINLEN
                BCS     LAPPN2
                JMP     WCSADV
LAPPN2:         INC     xDH
                RTS

LCARET:         JSR     SCNRDT
                BCS     LCRET1
                BEQ     LCRET0
                LDA     xAL
                CMP     #TRMNWP
                BNE     LCRET1
LCRET0:         DEC     xAH
LCRET1:         LDA     #TRMEOL
                STA     xAL
                JSR     SCNWTT
                MOVB    xDH, WDOLFT
                RTS

LFEED:          STZ     xSI
                LDA     F_CRET
                JEQ     LFED90         ; after a CR
                LDA     F_INST
                BEQ     LFED30
                JSR     SCNRTL
                LDA     xAH
                CMP     xDH
                BCC     LFED30
                LDA     xDL
                CMP     LSTLNE
                BEQ     CBEPI2
LFED10:         JSR     SCRINP         ; insert: the rest of the line to the next
                STA     xSI
                LDA     #' '
                JSR     SCROUT
                PSH     xDH
                PSH     xDL
LFED12:         LDA     #1
                STA     xCH
                JSR     MKRMCH
                INC     xDH
                LDA     LINLEN
                CMP     xDH
                BCS     LFED12
                MOVB    xAH, xDL
                PULB    xDL
                PULB    xDH
                MOVB    xDL, xAH
                JSR     PLINIT
                LDA     xDL
                CMP     WDOBOT
                BNE     LFED43
                JMP     LFEEDX
CBEPI2:         JMP     CBEEP
LFED30:         JSR     SCNRTL
                PHP
                BCS     LFED31
                BEQ     LFED32
LFED31:         INC     xAH
LFED32:         LDA     LINLEN
                CMP     xAH
                BCS     LFED40
                JSR     LFWRAP
                JCS     LFED69
                PLP
                BCC     LFED34
                JSR     SCNRDT
                MOVB    xAH, LINLEN
                LDA     #TRMWRP
                STA     xAL
                JSR     SCNWTT
LFED34:         MOVB    xDH, WDOLFT
                LDA     xDL
                CMP     LSTLNE
                BNE     LFED35
                INC     LSTLNE
                MOVB    LSTCOL, xDH
LFED35:         INC     xDL
                MOVB    xAH, xDH
                BRA     LFED43
LFED40:         PLP
                BCC     LFED42
LFED41:         JSR     MKRMNL
                BCS     LFEEDX
LFED42:         MOVB    xDH, xAH
                LDA     #' '
                JSR     SCROUT
LFED43:         JSR     SCNRDT
                MOVB    xAH, xDH
                LDA     #TRMLNF
                STA     xAL
                JSR     SCNWTT
                LDA     xDL
                CMP     LSTLNE
                BNE     LFED95
                STZ     LSTCOL
                INC     LSTLNE
                BRA     LFED95
LFED90:         PSH     xSI
                JSR     MKRMNL
                PULB    xSI
                BCS     LFEEDX
LFED95:         JSR     CSRDWN
                MOVB    xDH, WDOLFT
                LDA     xSI
                BEQ     LFEEDX
                JSR     SCROUT
LFEEDX:         LDA     WDOBOT
                CMP     xDL
                BCS     LFEEDZ
                STA     xDL
LFEEDZ:         RTS
LFED69:         PLP
                BRA     LFEEDX

LINSRT:         LDA     F_INST
                EOR     #$FF
                STA     F_INST
                RTS

; --- MKRMCH (SCNDRV.ASM 2001) -------------------------------------------------------------

; MKRMCH: CH spaces (1 or 2) opened at DX in the logical line, what runs
; off a line's end wrapped to the next (OVB, the 8086's two overflow
; buffers: SI and DI, 0 and 4). DH kept, DL the same line (it may move by a
; scroll); C the 8086's (its last ADD's carry).
MKRMCH:         PSH     xSI
                PSH     xAH
                PSH     xAL
                PSH     xCH
                PSH     xCL
                PSH     xDH
                PSH     xDL
                STZ     xSI
                LDA     #4
                STA     xDI
                JSR     SCNRDT
                PHP
                PSH     xAH
                PSH     xAL
                JSR     MVCHLN
                STZ     xCL
MKRMC0:         LDA     xCH
                BNE     MKRMC1
                PULB    xAL
                PULB    xAH
                PLP
MKRMCX:         CLC
                LDA     xCL
                ADC     xDL
                STA     xCL
                PHP
                PLA
                STA     MKF
                PULB    xDL
                PULB    xDH
                MOVB    xDL, xCL
                PULB    xCL
                PULB    xCH
                PULB    xAL
                PULB    xAH
                PULB    xSI
                LDA     MKF
                PHA
                PLP
                RTS
MKRMC1:         PULB    xAL
                PULB    xAH
                PLP
                BCS     MKRMC3
                BNE     MKRMC4
                PSH     xAH            ; LF: room for it on the next line
                PSH     xAL
                JSR     LFWRAP
                PULB    xAL
                PULB    xAH
                BCS     MKRMCQ
                DEC     xCH
                BEQ     MKRMCX
                BRA     MKRMC4
MKRMC3:         PSH     xAH            ; EOL: the next line opened
                PSH     xAL
                JSR     MKRMNL
                PULB    xAL
                PULB    xAH
                BCC     MKRMC4
MKRMCQ:         JSR     SCNRDT         ; no room: the overflow lost
                LDA     #TRMEOL
                STA     xAL
                JSR     SCNWTT
                JMP     MKRMCX
MKRMC4:         LDA     xDL
                CMP     LSTLNE
                BNE     MKRMC6
                SEC
                LDA     xAH
                SBC     xCH
                STA     xAH
                LDA     xAL
                CMP     #TRMNWP
                BNE     MKRMC5
                DEC     xAH
MKRMC5:         LDA     xAH
                CMP     LSTCOL
                BCS     MKRMC6
                STZ     LSTCOL         ; LSTPOS wrapped
                INC     LSTLNE
MKRMC6:         INC     xDL
                MOVB    xDH, WDOLFT
                DEC     xCL
                LDX     xSI
                LDY     xDI
                STY     xSI
                STX     xDI
                JSR     SCNRDT
                PHP
                PSH     xAH
                PSH     xAL
                PSH     xCH            ; CX kept round it, as the source's PUSH
                PSH     xCL            ; CX (MVCHLN's insert past the end sets CL)
                JSR     MVCHLN
                MOVB    xBH, xCH
                PULB    xCL
                PULB    xCH
                LDX     xSI
MKRMC7:         LDA     OVB,X          ; the last line's overflow in
                PHX
                JSR     SCROUT
                PLX
                INX
                INC     xDH
                DEC     xCH
                BNE     MKRMC7
                MOVB    xCH, xBH
                JMP     MKRMC0

; LFWRAP: DL's line feed wrapped to the next line. C set: no room.
LFWRAP:         INC     xDL
                JSR     MKRMNL
                BCS     LFWPEX
                PSH     xDH
                PSH     xDL
                JSR     SCRDLN
                PULB    xDL
                PULB    xDH
                JSR     LFTERM
                CLC
LFWPEX:         DEC     xDL
                RTS

; MVCHLN: CH spaces opened at DX in the physical line: CH the characters
; pushed off its end, at OVB from xDI. DX kept; BX used.
MVCHLN:         PSH     xDI
                PSH     xCH
                PSH     xCL
                LDA     xDL
                CMP     LSTLNE
                PHP
                MOVB    xCL, LINLEN
                PLP
                BNE     MVCH00
                MOVB    xCL, LSTCOL
MVCH00:         JSR     SCNRTL
                LDA     xAH
                CMP     xDH
                BCS     :+
                JMP     MVCH30         ; past the line's end
:               LDA     xDL
                CMP     LSTLNE
                BNE     MVCH03
                LDA     xCL
                CMP     xAH
                BCS     MVCH03
                MOVB    xAH, xCL       ; LSTPOS as the end
MVCH03:         PSH     xDH
                PSH     xDL
                PSH     xAH
                PSH     xAL
                CLC
                LDA     xCH
                ADC     xAH
                SEC
                SBC     LINLEN
                STA     xCH
                BEQ     @nov
                BCS     MVCH10
@nov:           STZ     xBH
                BRA     MVCH20
MVCH10:         SEC                    ; the overflow saved
                LDA     xAH
                SBC     xCH
                INC     A
                STA     xDH
                JSR     SCRINP
                MOVB    xBH, xCH
                MOVB    xBL, xCL
MVCH14:         LDX     xDI
                LDA     xAL
                STA     OVB,X
                INC     xDI
                DEC     xCH
                BEQ     MVCH20
                INC     xDH
                JSR     SCRINP
                BRA     MVCH14
MVCH20:         MOVB    xCH, xBH
                MOVB    xCL, xBL
                PULB    xAL
                PULB    xAH
                SEC
                LDA     xAH
                SBC     xCH
                STA     xAH
                PULB    xDL
                PULB    xDH
                PULB    xBL            ; the entry CX: BH the count, BL its CL
                PULB    xBH
                MOVB    xCL, xBL
                PSH     xCH
                PSH     xCL
                SEC
                LDA     xAH
                SBC     xDH
                CLC
                ADC     #1
                STA     xCH            ; from the place to the terminator
                PHP
                LDA     #1
                STA     xCL
                MOVB    xAH, xDH
                MOVB    xAL, xDL
                MOVB    xBL, xDL
                CLC
                LDA     xBH
                ADC     xDH
                STA     xBH
                PLP
                BNE     MVCH21
                MOVB    xBH, xDH       ; all of it overflowed
                BRA     MVCH22
MVCH21:         JSR     SCROLL
                CLC
                LDA     xBH
                ADC     xCH
                SEC
                SBC     #1
                STA     xBH
MVCH22:         PULB    xCL
                PULB    xCH
MVCH23:         JSR     SCNRDT
                BCS     MVCH25
                BEQ     MVCH25
MVCH24:         LDA     #TRMWRP
                STA     xAL
                LDA     xBH
                CMP     LINLEN
                BEQ     MVCH26
                LDA     xCH
                BEQ     MVCH26
                LDA     #TRMNWP
                STA     xAL
                MOVB    xBH, LINLEN
                PSH     xAH
                PSH     xAL
                PSH     xDH
                MOVB    xDH, xBH
                LDA     #' '
                JSR     SCROUT
                PULB    xDH
                PULB    xAL
                PULB    xAH
                BRA     MVCH26
MVCH25:         LDA     xCH
                BNE     MVCH24
MVCH26:         LDA     xAH
                CMP     xBH
                BRA_A   MVCH27
                MOVB    xAH, xBH
                JSR     SCNWTT
MVCH27:         LDA     xDL
                CMP     LSTLNE
                BNE     MVCHEX
                LDA     xBH
                CMP     LSTCOL
                BRA_BE  MVCHEX
                STA     LSTCOL
MVCHEX:         PULB    xDI
                RTS
MVCH30:         MOVB    xCL, xCH       ; inserting past the end
                CLC
                LDA     xCH
                ADC     xDH
                STA     xCH
                LDA     xAL
                CMP     #TRMLNF
                BEQ     MVCH31
                DEC     xCH
MVCH31:         LDA     LINLEN
                CMP     xCH
                BCS     MVCH33
                SEC                    ; blanks overflow
                LDA     xCH
                SBC     LINLEN
                STA     xCH
                TAY
@s:             LDX     xDI
                LDA     #' '
                STA     OVB,X
                INC     xDI
                DEY
                BNE     @s
                PULB    xBL
                PULB    xBH
                MOVB    xBH, xCH
                ; (the source pushes BX again here and never pops it, a slip on
                ; a path no insert reaches: kept out, so MVCHEX's POP is DI's)
                MOVB    xBH, LINLEN
                JMP     MVCH23
MVCH33:         PULB    xBL
                PULB    xBH
                MOVB    xBH, xCH
                STZ     xCH
                JMP     MVCH23

; SCNRTL: SCNRDT, and AH one less for a NULL_WRAP.
SCNRTL:         JSR     SCNRDT
                BCS     @x
                BEQ     @x
                LDA     xAL
                CMP     #TRMNWP
                BNE     @n
                DEC     xAH
@n:             LDA     xAL
                JMP     TFLG
@x:             RTS

; LFTERM: the line after a wrapped line feed: LF at the left margin.
LFTERM:         PSH     TRMCUR
                PSH     xAH
                PSH     xAL
                JSR     SCNRDT
                LDA     #TRMLNF
                STA     xAL
                MOVB    xAH, WDOLFT
                JSR     SCNWTT
                PULB    xAL
                PULB    xAH
                PULB    TRMCUR
                RTS

; --- The cursor within the window (SCNDRV.ASM 2316): C set, not moved ---------------------

CSRADV:         INC     xDH
                LDA     LINLEN
                CMP     xDH
                BCS     @ok
                DEC     xDH
                JSR     CSRDWN
                BCS     @r
                MOVB    xDH, WDOLFT
@ok:            CLC
@r:             RTS

CSRREG:         DEC     xDH
                LDA     xDH
                CMP     WDOLFT
                BCS     @ok
                INC     xDH
                JSR     CSRUP
                BCS     @r
                MOVB    xDH, LINLEN
@ok:            CLC
@r:             RTS

CSRUP:          LDA     WDOBOT
                CMP     xDL
                BCC     @no
                LDA     xDL
                CMP     WDOTOP
                BCC     @no
                BEQ     @no
                DEC     xDL
                CLC
                RTS
@no:            SEC
                RTS

CSRDWN:         LDA     xDL
                CMP     WDOTOP
                BCC     @no
                LDA     WDOBOT
                CMP     xDL
                BCC     @no
                BEQ     @no
                INC     xDL
                CLC
                RTS
@no:            SEC
                RTS

; --- Within the logical line (SCNDRV.ASM 2366) --------------------------------------------

; LCSADV: on one in the logical line. C set: at its end (Z set: stopped at
; LSTPOS); AH, AL the terminator.
LCSADV:         LDA     xDL
                CMP     LSTLNE
                BNE     LCSAD0
                LDA     xDH
                CMP     LSTCOL
                BCC     LCSAD0
                JSR     SCNRDT
                LDA     #0
                SEC
                RTS
LCSAD0:         JSR     SCNRDT
                BCS     LCSAD2
LCSAD1:         JSR     CSRADV
                BCS     LCSADA
                LDA     xAH
                CMP     xDH
                BCS     LCSADX
                INC     xDL
                MOVB    xDH, WDOLFT
LCSADX:         CLC
                RTS
LCSAD2:         LDA     xAH
                CMP     xDH
                BRA_A   LCSAD1
LCSADA:         LDA     #1
                SEC
                RTS

; LCSREG: back one in the logical line. C set: at its start (Z set: at
; FSTPOS).
LCSREG:         LDA     xDL
                CMP     FSTLIN
                BNE     LCSRG0
                LDA     xDH
                CMP     FSTCOL
                BRA_A   LCSRG0
                SEC                    ; (Z: DH is FSTCOL)
                RTS
LCSRG0:         MOVB    LRBL, xDL
                JSR     CSRREG
                BCS     LCSRGA
                LDA     LRBL
                CMP     xDL
                BEQ     LCSRGC
                JSR     SCNRDT
                BCC     LCSRGX
                MOVB    xDL, LRBL
                MOVB    xDH, WDOLFT
LCSRGA:         LDA     #1
                SEC
                RTS
LCSRGC:         CLC
LCSRGX:         RTS

; LSTART: DX the logical line's start (FSTPOS if on its line).
LSTART:
LSTRT0:         LDA     xDL
                CMP     FSTLIN
                BEQ     @f
                JSR     CSRUP
                BCS     @w
                JSR     SCNRDT
                BCC     LSTRT0
                INC     xDL
@w:             MOVB    xDH, WDOLFT
                RTS
@f:             MOVB    xDH, FSTCOL
                RTS

; ANCHK: C clear if the cell at DX is a letter or a digit.
ANCHK:          JSR     SCRINP
                CMP     #'0'
                BCC     @no
                CMP     #'9' + 1
                BCC     @yes
                CMP     #'A'
                BCC     @no
                CMP     #'Z' + 1
                BCC     @yes
                CMP     #'a'
                BCC     @no
                CMP     #'z' + 1
                BCC     @yes
@no:            SEC
                RTS
@yes:           CLC
                RTS

.rodata

FUNTAB:         .word   CTLIGN, CTLIGN, BCKWRD, CTLIGN ; ^@ ^A ^B ^C
                .word   CTLIGN, LTRUNC, FWDWRD, CBEEP  ; ^D ^E ^F ^G
                .word   BAKSPC, LTAB, LFEED, WHOME     ; ^H ^I ^J ^K
                .word   CCLRSN, LCARET, LAPPND, CTLIGN ; ^L ^M ^N ^O
                .word   CTLIGN, CTLIGN, LINSRT, CTLIGN ; ^P ^Q ^R ^S
                .word   LABELK, LDELET, CTLIGN, WDELET ; ^T ^U ^V ^W
                .word   CTLIGN, CTLIGN, WERASE, CTLIGN ; ^X ^Y ^Z ESC
                .word   WCSADV, WCSREG, WCSUP, WCSDWN  ; ^\ ^] ^^ ^_
                .word   CDELET                         ; DEL
CTLITB:         .byte   CHRLDL, CHRERA, CHRFDW, CHRBKW, CHRAPP, CHREDT, CHRCLE
                .byte   7, CHRHOM, CHRADV, CHRREG, CHRUP, CHRDWN
CTLCNT          = * - CTLITB

.segment "BRAM"
xAL:            .res    1
xAH:            .res    1
xBL:            .res    1
xBH:            .res    1
xCL:            .res    1
xCH:            .res    1
xDL:            .res    1
xDH:            .res    1
xSI:            .res    1
xDI:            .res    1
LINCNT:         .res    1              ; the screen's lines (25, 30)
CRTWID:         .res    1              ; its columns (40, 80)
WDOTOP:         .res    1              ; the window
WDOBOT:         .res    1
WDOLFT:         .res    1
WDORGT:         .res    1
TTCOL:          .res    NMLINE + 2     ; LINTTB: by line, from 1
TTTRM:          .res    NMLINE + 2
TRMCUR:         .res    1              ; the line SCNRDT read last
CSRY:           .res    1              ; the cursor's line: CSRY, FSTLIN, LSTLNE in
FSTLIN:         .res    1              ;   this order (TSTSCR's TSTWDO)
LSTLNE:         .res    1              ; LSTLIN (the name is LIST's here)
LSTCOL:         .res    1
FSTCOL:         .res    1
CSRX:           .res    1
F_CRET:         .res    1              ; 0: the last character was a CR
KEYSW:          .res    1              ; the key line: 0 off, $FF on
KYCNT:          .res    1              ; KEYDSP's keys left, the key, the attribute
KYNUM:          .res    1
KYATR:          .res    1
ESCFLG:         .res    1
F_EDIT:         .res    1              ; INLIN running
F_INST:         .res    1              ; insert mode
F_EDP6:         .res    1              ; SCNEDT.ASM's F_EDPG: a program line
MKINP:          .res    1              ; SCNMRK's, SCNRDL's: an INPUT statement's line
MKFND:          .res    1
NOSAV:          .res    1              ; SCRLUPN's: SCRLU1 keeps nothing
MKDL:           .res    1              ; MKRMNL's DX
MKDH:           .res    1
MKF:            .res    1              ; MKRMCH's carry
WRPF:           .res    1              ; WRAPLF's
UPTMP:          .res    1
LRBL:           .res    1              ; LCSREG's BL
CDF1:           .res    1              ; CDELLG's
CDF2:           .res    1
CDLL:           .res    1
CDLC:           .res    1
CDUF:           .res    1              ; CDUNWP's
CPAH:           .res    1              ; CDELPH's
CPAL:           .res    1
CPCH:           .res    1
CPDH:           .res    1
OVB:            .res    8              ; MKRMCH's overflow, two buffers of 4
