; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's screen statements, translated, in block P6 (hw/oodos.md,
; "The plan, phase 7": 7d1, U4), with the OEM's parts they call (SWIDTH,
; SETCLR, SCRSTT: the OO-1's, the project's own; CSRATR the home block's
; since 7e5). Each is reached in
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
;
; 7e5 (U0, the space plan's M6): the statements' arguments are parsed by
; their home block stubs (p8/far6.s: SCNINT, GTPRMS and the parses, moved
; there), which far call these once, as before, with the values in the
; pseudo-registers; each entry here checks what needs P6's state (LINCNT,
; KEYSW, the mode) and acts. KEY n,"text" is written by its stub (STRTAB is
; resident) and the line drawn again by P6's KEYDP (p6/scpag.s). SCREEN('s
; parse is its own (SFINT: the owner's answer 7 to the 7e5 plan).

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
.import CHRGTR, SYNCHR, GETBYT, FCERR, MAKINT, LINLEN, ADVERR, SCRMOD
.import STRTAB

.macro  MOVB7   dst, src
                LDA     src
                STA     dst
.endmacro

.code

; --- PREGS (7e5: M6) ---------------------------------------------------------------------

; PREGS: the home block's GTPRMS (p8/far6.s), from the pseudo-registers:
; RDL the count, RDH a bit for each given (bit 0 the first), RBL on their
; values (0 not given); PCNT, PFLG,X ($80 given, else 0) and PVAL,X for the
; first PMAX, as GTPRMS left them.
PREGS:          LDA     RDL
                STA     PCNT
                LDX     #0
@u:             LDA     RBL,X
                STA     PVAL,X
                LDA     #0
                LSR     RDH
                ROR     A
                STA     PFLG,X
                INX
                CPX     #PMAX
                BCC     @u
                RTS

; --- CLS (GWSTS.ASM 104) ------------------------------------------------------------------

; CLSP: A n, 0-2 (the OEM's: 0 all, 1 graphics, 2 text; 0 not given),
; checked by the stub.
CLSP:           STA     LOCP
                JSR     P6IN
                LDA     GMOD           ; 7e1: a graphics mode's page, P5's GCLS
                BEQ     @t             ; (carry set: CLS 1, the graphics alone)
                LDA     LOCP
                FJSR    GCLS
                BCS     @x
@t:             JSR     CLRSCN
@x:             JMP     P6OUT

; --- LOCATE (GWSTS.ASM 120) ---------------------------------------------------------------

; LOCATP: from the stub, which has parsed and checked the rest (the column
; on the line, the cursor, its start and stop): RDL the line, RDH bit 7 not
; given; RBL the column, RBH bit 7 not given.
LOCATP:         LDA     RDL
                STA     LOCY
                LDA     RBL
                STA     LOCX
                LDA     RDH
                STA     LOCG
                LDA     RBH
                STA     LOCG + 1
                JSR     P6IN
                LDA     LOCY
                BIT     LOCG           ; the line not given: the cursor's
                BPL     YLCPRM
                JSR     GTLINE
                STA     LOCY
YLCPRM:         LDA     LINCNT         ; the last row: LINCNT, less 1 with the key
                CLC                    ; line (KEYSW $FF: 7d2), the line compared
                ADC     KEYSW          ; with it, so no row wraps (255 is past it)
                CMP     LOCY
                BCC     GOFCER
                BIT     LOCG + 1       ; the column not given: the cursor's
                BPL     :+
                JSR     SCNPOS
                LDA     xDH
                STA     LOCX
:               MOVB7   CSRX, LOCX     ; SCNLOC
                MOVB7   CSRY, LOCY
                JMP     P6OUT          ; (CSRDSP's user cursor, 3: none shown, answer 9)
GOFCER:         JMP     FCERR

; --- WIDTH (GWSTS.ASM 190) ----------------------------------------------------------------

; GWWIDP: from the stub: RDL the columns (LINLEN if not given), RBL the
; rows, RBH bit 7 not given (LINCNT then).
GWWIDP:         LDA     RDL
                STA     LOCX
                LDA     RBL
                BIT     RBH
                BPL     :+
                LDA     LINCNT
:               STA     LOCY
                JSR     P6IN
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

COLORP:         JSR     PREGS          ; 7e5: the stub's GTPRMS
                JSR     P6IN
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

SCRNP:          JSR     PREGS          ; 7e5: the stub's GTPRMS
                JSR     P6IN
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

; SFINT (7e5: the owner's answer 7): SCREEN('s byte parameter, as SCNINT
; (GWSTS.ASM's) takes one that must be there: none (the statement's end, or
; a comma) Illegal function call, else GETBYT's value in A, PTERM what ended
; it (a comma or ")" passed over; the statement's end not).
SFINT:          DECW    TXTPTR
                JSR     CHRGTR
                BEQ     FCERGO
                CMP     #','
                BEQ     FCERGO
                JSR     GETBYT
                PHA
                DECW    TXTPTR
                JSR     CHRGTR
                STA     PTERM
                BEQ     @x
                CMP     #','
                BEQ     @p
                CMP     #')'
                BNE     FCERGO
@p:             INCW    TXTPTR
@x:             PLA
                RTS

; SCRNFP: TXTPTR at SCREEN's token in a formula: SCREEN(y,x[,z]), the FAC
; its value. In a graphics mode (the IBM manual, pp. 310-311): z Illegal
; function call, and a blank cell whose pixels aren't all 0 is 0 (the text
; is over the graphics here, so a character there is the character).
SCRNFP:         JSR     P6IN
                JSR     CHRGTR
                JSR     SYNCHR
                .byte   '('
                JSR     SFINT          ; y, 1-LINCNT
                STA     LOCY
                DEC     A
                CMP     LINCNT
                BCS     FCERGO
                JSR     SFINT          ; x, 1-LINLEN
                STA     LOCX
                DEC     A
                CMP     LINLEN
                BCS     FCERGO
                STZ     LOCP
                LDA     PTERM
                CMP     #')'
                BEQ     SCRCHR
                JSR     SFINT          ; z
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

NMKEYF          = 10                   ; GWSTS.ASM's: the soft keys (p8/far6.s's too)

; KEYSP: TXTPTR at what follows KEY, A it: "(", ON, OFF or LIST (7e5: KEY
; n,"text" is the home block's, p8/far6.s, its line drawn by KEYDP).
KEYSP:          PHA
                JSR     P6IN
                PLA
                CMP     #'('
                BNE     :+
                JMP     ADVERR         ; KEYSTT: KEY(n), 7f's
:               CMP     #ON_TK
                BEQ     KEYON
                CMP     #OFF_TK
                BNE     KEYLST         ; (LIST)
                LDA     #0
                JSR     KEYOX
                BRA     KEYOXX

KEYON:          JSR     SKEYON
KEYOXX:         JSR     CHRGTR         ; past ON, OFF, LIST
                JMP     P6OUT

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
KLEN:           .res    1              ; KEYLST's number
KOFS:           .res    1              ; KEYLST's place
PMAX            = 4                    ; 7e5: the stub's GTPRMS's list (PREGS), the first 4
PCNT:           .res    1
PFLG:           .res    PMAX
PVAL:           .res    PMAX
PTERM:          .res    1              ; SFINT's terminator
LOCX:           .res    1
LOCY:           .res    1
LOCP:           .res    1              ; CLS's n, SCREEN('s z
LOCG:           .res    2              ; LOCATP's: bit 7, the line, the column not given
NFG:            .res    1
NBG:            .res    1
NAP:            .res    1
NVP:            .res    1
