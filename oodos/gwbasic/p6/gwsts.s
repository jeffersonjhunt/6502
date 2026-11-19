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
;   CLSP      CLS [n]: the active page cleared (CLRSCN), the cursor home;
;             in a graphics mode (7e1) the graphics' page too (P5's GCLS),
;             CLS 1 the graphics alone, CLS 2 the text alone
;   LOCATP    LOCATE [y][,[x][,[cursor][,[start][,stop]]]]
;   GWWIDP    WIDTH n[,m] for the screen: 40 or 80 columns, 25 or 30 rows
;             (30: decision 8's OO-1 extension); the pages cleared; in a
;             graphics mode (7e1) the mode: 40 SCREEN 1, 80 SCREEN 2
;   SWIDP     WIDTH "SCRN:",n (P4's SCNSWD, GIOSCN.ASM's): A the width,
;             40 or 80 with the rows kept, 255 (the source's infinite) no
;             change; Advanced Feature in console mode (7d2's answer 6)
;   COLORP    COLOR [fg][,[bg][,border]]: fg 0-31 (16 on: blinking), bg
;             0-15 (its low 3 bits shown), border 0-15; in a graphics mode
;             (7e1) P5's GCOLOR: COLOR [background][,palette]
;   SCRNP     SCREEN [mode][,[burst][,[apage][,vpage]]]: modes 0, 1 and 2
;             (7e1: 1 and 2 P5's GMODE, their text on layer 1 over the
;             graphics; the others Illegal function call), SCREEN 0's pages
;             0-3 at 80 columns, 0-7 at 40 (the owner's answer 6), a
;             graphics mode's 0 only; vpage apage's if not given
;   SCRNFP    SCREEN(y,x[,z]): the cell's character, or with z not 0 its
;             attribute
;   CSRLNP    CSRLIN: the line the next character goes on (GTLINE)
;   KEYSP     KEY ON, KEY OFF, KEY LIST, KEY n,"text" (7d2; KEY(n) is 7f's,
;             Advanced Feature until then: the owner's answer 5)
;   SKEYOP    SKEYON at start-up (P2's PRNTND, in screen mode with no
;             program named: the owner's answer 1)

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry CLSP
;@entry LOCATP
;@entry GWWIDP
;@entry SWIDP
;@entry COLORP
;@entry SCRNP
;@entry SCRNFP
;@entry CSRLNP
;@entry KEYSP
;@entry SKEYOP
.export CLSP, LOCATP, GWWIDP, SWIDP, COLORP, SCRNP, SCRNFP, CSRLNP, KEYSP, SKEYOP
.import CHRGTR, SYNCHR, GETBYT, FCERR, SNERR, MAKINT, LINLEN, ADVERR, SCRMOD
.import FRMEVL, FRESTR, STRTAB

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
; OO-1: every PFLG cleared first, so one past PCNT reads "not given" (the
; 8086's are on its stack, counted; SCRSTT reads the pages' flags whatever
; the count: a COLOR's third parameter was taken for SCREEN's apage).
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
@x:             RTS

; --- CLS (GWSTS.ASM 104) ------------------------------------------------------------------

CLSP:           JSR     P6IN
                JSR     SCNINT
                BCS     :+
                CMP     #3             ; CLS 0-2 (the OEM's: 0 all, 1 graphics, 2 text)
                BCC     :+
                JMP     FCERR
:               STA     LOCP
                JSR     EOSCHK
                LDA     GMOD           ; 7e1: a graphics mode's page, P5's GCLS
                BEQ     @t             ; (carry set: CLS 1, the graphics alone)
                LDA     LOCP
                FJSR    GCLS
                BCS     @x
@t:             JSR     CLRSCN
@x:             JMP     P6OUT

; --- LOCATE (GWSTS.ASM 120) ---------------------------------------------------------------

LOCATP:         JSR     P6IN
                JSR     SCNINT         ; the line
                BCC     YLCPRM
                JSR     GTLINE
YLCPRM:         CMP     #0
                BEQ     GOFCER
                STA     LOCY
                LDA     LINCNT         ; the last row: LINCNT, less 1 with the key
                CLC                    ; line (KEYSW $FF: 7d2), the line compared
                ADC     KEYSW          ; with it, so no row wraps (255 is past it)
                CMP     LOCY
                BCC     GOFCER
                JSR     SCNINT         ; the column
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

SWIDP:          BIT     SCRMOD
                BMI     :+
                JMP     ADVERR
:               CMP     #255
                BEQ     @x
                STA     LOCX
                JSR     P6IN
                LDA     LINCNT
                STA     LOCY
                JSR     SWIDTH
                BCS     GOFCER
                JMP     P6OUT
@x:             RTS

; SWIDTH (the OEM's): LOCX columns (40, 80), LOCY rows (25, 30): the
; driver set for them, every page cleared, page 0 active and shown, home.
; 7e1: in a graphics mode WIDTH chooses the mode (PC-BASIC's): 40 SCREEN 1,
; 80 SCREEN 2, 25 rows only; the same mode, nothing.
SWIDTH:         LDA     GMOD
                BEQ     @txt
                LDA     LOCY
                CMP     #25
                BNE     @bad
                LDX     #GMTILE
                LDA     LOCX
                CMP     #40
                BEQ     :+
                INX
                CMP     #80
                BNE     @bad
:               CPX     GMOD
                BEQ     @same
                STX     NMODE
                JMP     SMODE
@same:          CLC
                RTS
@txt:           LDA     LOCX
                CMP     #40
                BEQ     :+
                CMP     #80
                BNE     @bad
:               LDA     LOCY
                CMP     #25
                BEQ     SWSET
                CMP     #30
                BEQ     SWSET
@bad:           SEC
                RTS
; SWSET: SWIDTH's LOCX and LOCY, checked (SMODE's). The border black (the
; IBM manual's p. 367: a change of width clears the screen and sets it so;
; the foreground and background as they were).
SWSET:          STZ     BRDC
                LDA     LOCX
                LDX     LOCY
                JSR     SCNSWI
                STZ     APAGE
                STZ     VPAGE
                JSR     PGINIT
                JSR     SCNBRK
                JSR     CLRSCN         ; (SCNCLR's KEYDSP: the key line on the new last line)
                JSR     VSHOW
                CLC
                RTS

; --- COLOR (GWSTS.ASM 244) ----------------------------------------------------------------

COLORP:         JSR     P6IN
                JSR     GTPRMS
                LDA     GMOD           ; 7e1: a graphics mode's, P5's GCOLOR
                BNE     @g
                JSR     SETCLR
                JCS     GOFCER
                JMP     P6OUT
@g:             LDA     PVAL           ; the background, the palette
                STA     RDL
                LDA     PFLG
                STA     RDH
                LDA     PVAL + 1
                STA     RBL
                LDA     PFLG + 1
                STA     RBH
                LDA     PCNT
                FJSR    GCOLOR
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
                JSR     BANDS          ; the border changed: the bands, and
                JSR     VSHOW          ; the card's own
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

; SCRSTT (the OEM's): the mode (0, 1, 2; as it is if not given: 7e1; 3-9
; and the OO-1's own Illegal function call, 7e5's); the burst taken and
; passed over; the active page and the visual one, SCREEN 0's (a graphics
; mode has one, CGA's: 0 only). A change of mode (SMODE) sets the new one up
; (P5's GMODE: its page, its palette, layer 0) and the text for it: its
; colours the PC's (7, or the mode's 3 or 1 on 0, transparent over the
; graphics), 40 columns for SCREEN 1, 80 for SCREEN 2, SCREEN 0 the width
; the mode left (40 after SCREEN 1, 80 after SCREEN 2: PC-BASIC's), 25 rows, every page cleared, on layer 1 over the graphics (VSHOW).
; The same mode again changes nothing (PC-BASIC's: the picture stays).
SCRSTT:         LDA     PCNT
                CMP     #5
                BCS     @bad
                LDX     GMOD
                LDA     PFLG
                BEQ     :+
                LDX     PVAL
:               CPX     #GMMONO + 1
                BCS     @bad           ; SCREEN 3 ...: no such mode
                STX     NMODE
                TXA
                BEQ     @s0
                LDA     PFLG + 2       ; a graphics mode: page 0 only
                BEQ     :+
                LDA     PVAL + 2
                BNE     @bad
:               LDA     PFLG + 3
                BEQ     :+
                LDA     PVAL + 3
                BNE     @bad
:               CPX     GMOD
                BEQ     @ok
                JMP     SMODE
@s0:            LDX     APAGE
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
                LDA     GMOD           ; from a graphics mode: SCREEN 0 first
                BEQ     :+
                JSR     SMODE
                BCS     @bad
:               LDA     NAP
                CMP     APAGE
                BEQ     :+
                STA     APAGE
                JSR     APGSET         ; the cache written back and forgotten
                JSR     KEYDSP         ; the key line on the new page
:               LDA     NVP
                CMP     VPAGE
                BEQ     @ok
                STA     VPAGE
                JSR     VSHOW
@ok:            CLC
                RTS
@bad:           SEC
                RTS

; SMODE (7e1): the mode NMODE, from another (SCRSTT, SWIDTH); carry set,
; nothing changed, if P5 can't set it up (the card's memory taken).
SMODE:          LDA     NMODE
                FJSR    GMODE
                BCS     @x
                LDX     NMODE
                STX     GMOD
                LDA     MODEFG,X       ; the text's colours: COLOR 7,0 or the mode's
                STA     FGC
                STA     ATTR
                STZ     BGC
                STZ     BRDC
                LDA     MODEW,X        ; its width (SCREEN 0: the current, CRTWID)
                BNE     :+
                LDA     CRTWID
:               STA     LOCX
                LDA     #25
                STA     LOCY
                JMP     SWSET
@x:             RTS

; --- SCREEN(, CSRLIN (GWSTS.ASM 258) ------------------------------------------------------

FCERGO:         JMP     FCERR

; SCRNFP: TXTPTR at SCREEN's token in a formula: SCREEN(y,x[,z]), the FAC
; its value. In a graphics mode (the IBM manual, pp. 310-311): z Illegal
; function call, and a blank cell whose pixels aren't all 0 is 0 (the text
; is over the graphics here, so a character there is the character).
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
                LDA     GMOD           ; z in text mode only (the IBM manual's
                BNE     FCERGO         ; p. 310)
SCRCHR:         MOVB7   xDL, LOCY
                MOVB7   xDH, LOCX
                LDA     LOCP
                BEQ     :+
                JSR     SCRATR
                BRA     SCRENX
:               JSR     SCRINP
                LDX     GMOD           ; a graphics mode: 0 for a blank cell over
                BEQ     SCRENX         ; graphics (p. 311; P5's GCELL looks at the
                CMP     #' '           ; 8 x 8 pixels under it)
                BNE     SCRENX
                LDX     LOCY
                DEX
                STX     RDL
                LDX     LOCX
                DEX
                STX     RCL
                FJSR    GCELL
                LDA     #' '
                BCC     SCRENX
                LDA     #0
SCRENX:         STA     RBL
                STZ     RBH
                JSR     MAKINT
                DECW    TXTPTR
                JSR     CHRGTR
                PHP
                JSR     P6OUT
                PLP
                RTS

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


; --- KEY (GWSTS.ASM 1332) -----------------------------------------------------------------

NMKEYF          = 10                   ; GWSTS.ASM's: the soft keys

; KEYSP: TXTPTR at what follows KEY, A it. KEY n,"text": key n (1-10), its
; text's first 15 characters (a 0 after), the line drawn again if it is on.
KEYSP:          PHA
                JSR     P6IN
                PLA
                CMP     #'('
                BNE     :+
                JMP     ADVERR         ; KEYSTT: KEY(n), 7f's
:               CMP     #ON_TK
                BEQ     KEYON
                CMP     #OFF_TK
                BEQ     KEYOF
                CMP     #LIST_TK
                BEQ     KEYLST
                JSR     GETBYT         ; the key's number
                DEC     A
                CMP     #NMKEYF
                BCS     KEYFCE         ; 1 to 10, else Illegal function call
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
                BCC     KEY1
                LDA     #15            ; 0 to 15 characters
KEY1:           STA     KLEN
                LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDY     #0
@cp:            CPY     KLEN
                BEQ     @end
                LDA     (PTR),Y
                STA     STRTAB,X
                INX
                INY
                BRA     @cp
@end:           STZ     STRTAB,X       ; the 0 after it
                LDA     KEYSW
                BEQ     NODSPK         ; off: the last line left as it is
                JSR     KEYDSP
NODSPK:         JMP     P6OUT
KEYFCE:         JMP     FCERR

KEYON:          JSR     SKEYON
KEYOXX:         JSR     CHRGTR         ; past ON, OFF, LIST
                JMP     P6OUT
KEYOF:          LDA     #0
                JSR     KEYOX
                BRA     KEYOXX

; SKEYOP: SKEYON from P2's start-up.
SKEYOP:         JSR     P6IN
                JSR     SKEYON
                JMP     P6OUT
; SKEYON: the key line on; KEYOX: A the new KEYSW (0 or $FF): if that
; changes it, the line drawn or cleared (turned on: its first keys,
; FKYADV's). OO-1: the scroll window's foot doesn't move (KEYOX's AH): the
; last line is outside it whether the line is on or off (SCNSWI).
SKEYON:         LDA     #$FF
KEYOX:          CMP     KEYSW
                STA     KEYSW
                BEQ     @x
                CMP     #$FF
                BNE     @d
                JSR     FKYADV         ; (Z set: the first keys)
@d:             JMP     KEYDSP
@x:             RTS

; KEYLST: the ten keys, "F", the number, a blank, the text (KEYLSP: a CR as
; 27), a new line each. OO-1: 27 printed would be ESC (DOESC: written where
; the cursor is, which doesn't move, so the next character covers it); so a
; blank is printed, and 27 written by SCROUT over its cell (SCNOUT leaves DX
; at it), as the PC shows the CR (7d2's review round 1, F3).
KEYLST:         LDA     #1
                STA     KLEN           ; the key's number
                STZ     KOFS           ; its place in STRTAB
@k:             LDA     #'F'
                JSR     PUTC
                LDA     KLEN
                CMP     #10
                BCC     @one
                LDA     #'1'
                JSR     PUTC
                LDA     #0
@one:           ORA     #'0'
                JSR     PUTC
                LDA     #' '
                JSR     PUTC
                LDX     KOFS
@c:             LDA     STRTAB,X
                BEQ     @e
                PHX
                CMP     #13            ; KEYLSP
                BNE     @p
                LDA     #' '
                JSR     PUTC
                LDA     #27
                JSR     SCROUT
                BRA     @n
@p:             JSR     PUTC
@n:             PLX
                INX
                BRA     @c
@e:             LDA     #13            ; (PUTC: and an LF)
                JSR     PUTC
                LDA     KOFS
                CLC
                ADC     #16
                STA     KOFS
                INC     KLEN
                LDA     KLEN
                CMP     #NMKEYF + 1
                BCC     @k
                JMP     KEYOXX

.segment "RODATA"
GMTILE          = 1                    ; the modes (P5's GMODEV)
GMMONO          = 2
MODEFG:         .byte   7, 3, 1        ; by mode: the text's colour
MODEW:          .byte   0, 40, 80      ;   its width (0: the current one)

.segment "BRAM"
GMOD:           .res    1              ; 7e1: the mode, 0-2, as P5's GMODEV
NMODE:          .res    1              ;   SCRSTT's and SWIDTH's new one
KLEN:           .res    1              ; KEY's length; KEYLST's number
KOFS:           .res    1              ; KEYLST's place
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
