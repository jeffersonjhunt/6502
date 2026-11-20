; The graphics modes, in block P5 (hw/oodos.md, "The plan, phase 7": 7e1,
; U1; the owner's answers of 2026-11-19): the OEM's, the project's own. The
; 1983 source hands SCREEN's modes past 0 to the OEM's SCRSTT (P6's), and
; this is what SCRSTT, COLOR and CLS call for a graphics mode:
;
;   GMODE     A the mode (0, 1, 2), from P6's SCRSTT when it changes. 1 and
;             2: the mode's page of card memory (one GX_VALLOC at its first
;             use, kept until SYSTEM; SCREEN 1's tile map written then) and
;             GW-BASIC's palette (one GX_VALLOC of 4 pages at the first
;             graphics mode: the console's 512 bytes copied in, then its
;             groups: answer 10) allocated, the page cleared (DM_VFILL),
;             layer 0 the graphics (GX_LAYER, GX_SCROLL, its PALOFS the
;             mode's group), PALBASE GW-BASIC's palette, the background
;             black, the graphics cursor centred (GRPINI). 0: PALBASE the
;             console's again (P6's VSHOW then puts the text back on layer
;             0, layer 1 off). Carry set, nothing changed, if card memory
;             runs out
;   GCOLOR    COLOR in SCREEN 1 (the PC's COLOR [background][,palette]):
;             A the parameters (0-2; none changes nothing, the IBM manual's
;             p. 44), RDL the background (RDH 0: not given),
;             RBL the palette (RBH 0: not given). The background's CGA
;             colour (0-15, the low 4 bits) to palette entry 0, the backdrop
;             behind every transparent pixel; an even palette is group 1
;             (green, red, brown), an odd one group 2 (cyan, magenta, light
;             grey), on both layers, so the text's colour 3 follows it as a
;             PC's does. Carry set (Illegal function call) for a third
;             parameter (p. 43's form has two) and in SCREEN 2 (p. 43)
;   GCLS      CLS's graphics part, A CLS's n (0 if not given): the page
;             cleared and the cursor centred, but for CLS 2 (the text's);
;             carry set for CLS 1, the graphics alone (no text cleared)
;
; and inside P5: GRPSIZ, PIXSIZ, GETFBC and SETATR, the OEM's routines
; GENGRP.ASM calls. The palette's groups (PALOFS): 1 SCREEN 1's palette 0,
; entries 17-19 the console's 2, 4 and 6; 2 palette 1, 33-35 its 3, 5 and 7;
; 3 SCREEN 2's, 49 its 15 (white). A 2-bit or 1-bit pixel's colour is the
; group x 16 + the pixel, its 0 transparent; the text over it (layer 1, P5's
; TXMODE) is the mode's colour 3 or 1 in the same group, background 0.

.include "gwbasic.inc"
.include "p5/p5.inc"
; OOLIB's (oolib.inc, vmem.inc: not included here, since gwbasic.inc defines
; GXP, gxmem's pointer, which only p5/seed.s exports to it)
.import GX_VALLOC, GX_VADDR, GX_VFILL, GX_VCOPY, GX_VPUT, GX_LAYER, GX_SCROLL, GX_PALOFS
.import GX_PALBASE, GX_V, GX_R, GX_X, GX_Y, GX_A, GX_C

.export GMODE, GCOLOR, GCLS, GVIEW, GVWGET, VWRST, VWRSTW
.import BUF, GFLG

.code

; --- GMODE ------------------------------------------------------------------------------------

GMODE:          STA     GNEW
                CMP     #GM_TEXT
                BNE     GMGFX
                STZ     GMODEV         ; SCREEN 0: the console's palette back
                STZ     GMX
                STZ     GFLG           ; 7e4: no graphics, no window
                LDA     #0
                TAX
                JSR     GX_PALBASE
                CLC
GMFAIL:         RTS                    ; (and carry set: no card memory)
GMGFX:          LDA     GPAL           ; the palette, at the first graphics mode
                ORA     GPAL + 1
                BNE     @page
                LDA     #4
                STA     GX_V
                STZ     GX_V + 1
                LDA     #0
                JSR     GX_VALLOC
                BCS     GMFAIL
                STA     GPAL
                STX     GPAL + 1
                JSR     PALC           ; GX_C: the palette's card address
                STZ     GX_A           ; the console's 512 bytes
                STZ     GX_A + 1
                STZ     GX_A + 2
                STZ     GX_V
                LDA     #>512
                STA     GX_V + 1
                JSR     GX_VCOPY
                LDX     #0             ; its groups' entries, from its CGA entries
@grp:           PHX
                LDA     PALSRC,X
                JSR     PALA           ; GX_A: entry A of GW-BASIC's palette
                PLX
                PHX
                LDA     PALDST,X
                JSR     PALC1          ; GX_C: entry A
                JSR     PAL2
                PLX
                INX
                CPX     #PALDST - PALSRC
                BCC     @grp
@page:          LDA     GNEW           ; the mode's page, at its first use
                ASL     A
                TAX                    ; (GPAGES - 2),X: its card page
                LDA     GPAGES - 2,X
                ORA     GPAGES - 1,X
                BNE     @have
                PHX
                LDA     GSIZE - 2,X
                STA     GX_V
                STZ     GX_V + 1
                LDA     #GALIGN
                JSR     GX_VALLOC
                PLY
                BCS     GMFAIL
                STA     GPAGES - 2,Y
                TXA
                STA     GPAGES - 1,Y
                CPY     #2 * GM_TILE
                BNE     @have2
                LDA     GPAGES - 2,Y   ; SCREEN 1: its map, once
                JSR     TMAP
@have2:         TYA
                TAX
@have:          LDA     GPAGES - 2,X
                STA     GBASE
                LDA     GPAGES - 1,X
                STA     GBASE + 1
                LDA     GNEW
                STA     GMODEV
                ASL     A
                STA     GMX
                TAX
                LDA     GMAXT - 2,X    ; the PC's: 320 or 640 across
                STA     GMAXX
                LDA     GMAXT - 1,X
                STA     GMAXX + 1
                LDA     GGRPT - 2,X    ; SCREEN 1's palette 1, SCREEN 2's white
                STA     GGRP
                LDA     #1             ; 7e4: a graphics mode, no window (GFLG),
                STA     GFLG           ; the viewport the whole screen
                JSR     VWRST
                JSR     GPAGE          ; the page cleared, the cursor centred
                SEC                    ; 7e2: DRAW's colour the mode's foreground (the IBM
                JSR     GETFBC         ; manual's p. 74: C's default the mode's last)
                JSR     SETATR
                STA     ATRBYT
                LDX     GMX            ; layer 0: the frame or the mono rows
                LDA     GMAP - 2,X
                CLC
                ADC     GBASE
                STA     GX_V
                LDA     GBASE + 1
                ADC     #0
                STA     GX_V + 1
                LDA     GBASE
                STA     GX_R
                LDA     GBASE + 1
                STA     GX_R + 1
                LDA     GCONF - 2,X
                TAX
                LDA     #0
                JSR     GX_LAYER
                STZ     GX_X
                STZ     GX_X + 1
                LDX     GMX
                LDA     GVS - 2,X      ; 400 lines centred
                STA     GX_Y
                LDA     GVS - 1,X
                STA     GX_Y + 1
                LDA     #0
                JSR     GX_SCROLL
                LDX     GGRP
                LDA     #0
                JSR     GX_PALOFS
                LDA     #0             ; the background black (the console's entry 0)
                JSR     BACKG
                LDA     GPAL
                LDX     GPAL + 1
                JSR     GX_PALBASE
                CLC
                RTS

; GPAGE: the mode's page cleared (its tiles, or all its rows: rows 400-479
; of the mono layer, under the bands, too), and the graphics cursor centred.
GPAGE:          LDA     GBASE
                LDX     GBASE + 1
                JSR     GX_VADDR
                LDX     GMX
                LDA     GBYTES - 2,X
                STA     GX_V
                LDA     GBYTES - 1,X
                STA     GX_V + 1
                LDA     #0
                JSR     GX_VFILL
                JMP     GRPINI

; TMAP: SCREEN 1's map, its 32 rows of 64 entries, each written by one
; DM_VWRITE from BUF (the line buffer, free while a statement runs: GWSTS.ASM's
; SCREEN takes its parameters there): row r's columns 0-39 tile 40r + the
; column for r < 25, the rest the blank tile. A/X the page, Y kept.
TMAP:           PHY
                CLC                    ; GX_A: the map's card address
                ADC     #T1PAGES
                BCC     :+
                INX
:               JSR     GX_VADDR
                STZ     T0             ; the row
                STZ     T1             ; 40 x the row
                STZ     T2
@row:           LDA     T1
                STA     T3
                LDA     T2
                STA     T4
                LDX     #0
                LDY     #0
@col:           LDA     #<TILES
                STA     BUF,X
                LDA     #>TILES
                STA     BUF + 1,X
                CPY     #40
                BCS     @nx
                LDA     T0
                CMP     #25
                BCS     @nx
                LDA     T3
                STA     BUF,X
                LDA     T4
                STA     BUF + 1,X
                INC     T3
                BNE     @nx
                INC     T4
@nx:            INX
                INX
                INY
                CPY     #64
                BCC     @col
                LDA     #128
                STA     GX_V
                STZ     GX_V + 1
                LDA     #<BUF
                LDX     #>BUF
                JSR     GX_VPUT        ; (GX_A kept)
                CLC
                LDA     GX_A
                ADC     #128
                STA     GX_A
                BCC     :+
                INC     GX_A + 1
                BNE     :+
                INC     GX_A + 2
:               CLC
                LDA     T1
                ADC     #40
                STA     T1
                BCC     :+
                INC     T2
:               INC     T0
                LDA     T0
                CMP     #32
                BCC     @row
                PLY
                RTS

; --- GCOLOR, GCLS -----------------------------------------------------------------------------

GCOLOR:         LDX     GMODEV
                CPX     #GM_TILE
                BNE     @fc
                CMP     #3             ; a third parameter: the manual's form has two
                BCS     @fc
                TAX                    ; none: nothing changes (p. 44: any omitted)
                BEQ     @x
                LDA     RDH
                BEQ     @pal
                LDA     RDL
                AND     #$0F
                JSR     BACKG
@pal:           LDA     RBH
                BEQ     @x
                LDA     RBL            ; even: group 1; odd: 2
                AND     #1
                INC     A
                STA     GGRP
                TAX
                LDA     #0
                JSR     GX_PALOFS
                LDX     GGRP
                LDA     #1
                JSR     GX_PALOFS
@x:             CLC
                RTS
@fc:            SEC
                RTS

GCLS:           CMP     #2             ; CLS 2: the text alone
                BEQ     @t
                LDX     VWON           ; 7e4: a viewport
                BNE     @v
                PHA
                JSR     GPAGE
                PLA
                CMP     #1             ; C: CLS 1, the graphics alone
                RTS
@v:             LDA     #0             ; the viewport alone cleared, the cursor at its
                JSR     VFILL          ; centre, and the text left (CLS 1's path: the
                JSR     GRPINI         ; IBM manual's p. 36, no home; 7e's answer 5,
                SEC                    ; the overlay: the text isn't the graphics')
                RTS
@t:             CLC
                RTS

; BACKG: palette entry 0 the console's entry A (a CGA colour, 0-15).
BACKG:          ASL     A
                STA     GX_A
                STZ     GX_A + 1
                STZ     GX_A + 2
                LDA     #0
                JSR     PALC1
PAL2:           LDA     #2             ; one entry, two bytes
                STA     GX_V
                STZ     GX_V + 1
                JMP     GX_VCOPY

; PALA, PALC1: GX_A or GX_C entry A of GW-BASIC's palette (its card
; address + 2A); PALC: GX_C its first byte. PALE: A/X/Y that address, low
; byte first.
PALA:           JSR     PALE
                STA     GX_A
                STX     GX_A + 1
                STY     GX_A + 2
                RTS
PALC:           LDA     #0
PALC1:          JSR     PALE
                STA     GX_C
                STX     GX_C + 1
                STY     GX_C + 2
                RTS
PALE:           ASL     A
                PHA
                LDA     #0
                ROL     A
                CLC
                ADC     GPAL
                TAX
                LDA     GPAL + 1
                ADC     #0
                TAY
                PLA
                RTS

; --- The viewport (7e4): VWRST, GVIEW, GVWGET, VFILL ---------------------------------------

; The OO-1's (the IBM manual's pp. 357-360; the source's VIEW falls into
; ADVERR). Every coordinate this block keeps (the graphics cursor GRPACX and
; GRPACY, GXPOS and GYPOS, LINCLP's LX and LY, PAINT's PNX and PNY) is the
; viewport's, from its corner: 0 to VWMX across and 0 to VWMY down, the
; screen's GMAXX and 199 with no viewport. MAPXYC adds the corner (VWOX,
; VWOY) for the card; SCAND takes VIEW SCREEN's offset (VWSX, VWSY: the
; corner under VIEW SCREEN, else 0) off an absolute point. So every
; statement's clipping is the viewport's with no code of its own.
;
;   VWRST   the viewport the whole screen, and the line style solid (LSTYL:
;           GLINEP's, p5/gengrp.s); VWRSTW the same and the window ended
;           (GFLG's bits 7 and 6): GMODE and GRPRST (RUN, CLEAR, NEW)
;   GVIEW   VIEW, from the home block's stub (p8/far5.s), which has parsed
;           and checked it (the mode, the corners on the screen, sorted,
;           unequal): A bit 7 corners given, bit 6 SCREEN, bit 5 a fill,
;           bit 4 a border; RSI on the viewport (GVB: VWOX, VWOY, VWMX,
;           VWMY, the fill, the border), RCL/RCH, RDL/RDH the border box's
;           first corner (x1 - 1, y1 - 1), RBL/RBH, RAL/RAH its second. The
;           viewport the whole screen; the border box drawn there, each side
;           clipped to the screen as any line; the viewport set (VIEW SCREEN
;           its points absolute: VWSX, VWSY its corner), the fill over it;
;           no corners (VIEW, VIEW SCREEN alone) the whole screen. Then the
;           cursor at its centre (GRPINI), and with a WINDOW on P1's WINRSC
;           maps it onto the new viewport
;   GVWGET  RDL/RDH VWMX, RBL VWMY, RBH/RAL VWSX, RAH VWSY (for P1)
;   VFILL   A an attribute (SETATR's): the viewport filled with it
;           (DOBOXF), CPAT back to ATRBYT's
VWRSTW:         LDA     GFLG
                AND     #$3F
                STA     GFLG
VWRST:          LDX     #VWON - VWOX
@z:             STZ     VWOX,X
                DEX
                BPL     @z
                LDA     GMAXX
                STA     VWMX
                LDA     GMAXX + 1
                STA     VWMX + 1
                LDA     #199
                STA     VWMY
                LDA     #$FF
                STA     LSTYL
                STA     LSTYL + 1
                RTS

GVIEW:          STA     GVF
                LDX     #GVBN - 1      ; the viewport, the fill and the border kept
@k:             LDA     RSI,X
                STA     GVB,X
                DEX
                BPL     @k
                JSR     VWRST
                BIT     GVF
                BPL     @end           ; VIEW alone: the whole screen
                LDA     GVF
                AND     #$10
                BEQ     @set
                LDA     GVB + 7        ; the border, one pixel outside
                JSR     SETATR
                LDA     RBL
                STA     GXPOS
                LDA     RBH
                STA     GXPOS + 1
                LDA     RAL
                STA     GYPOS
                LDA     RAH
                STA     GYPOS + 1
                JSR     BOXLIN
@set:           LDX     #VWMY - VWOX   ; the viewport
@v:             LDA     GVB,X
                STA     VWOX,X
                DEX
                BPL     @v
                INC     VWON
                BIT     GVF            ; VIEW SCREEN: its points absolute
                BVC     @f
                LDX     #VWMX - VWOX - 1
@s:             LDA     VWOX,X         ; (VWSX, VWSY as VWOX, VWOY)
                STA     VWSX,X
                DEX
                BPL     @s
@f:             LDA     GVF
                AND     #$20
                BEQ     @end
                LDA     GVB + 6        ; the fill
                JSR     VFILL
@end:           JSR     GRPINI         ; the cursor at the centre
                LDA     ATRBYT         ; CPAT back (the border's)
                JSR     SETATR
                BIT     GFLG           ; a WINDOW: onto the new viewport
                BPL     @x
                JSR     GVWGET
                FJSR    WINRSC
@x:             RTS

GVWGET:         LDX     #VWSY - VWMX   ; (VWMX to VWSY)
@g:             LDA     VWMX,X
                STA     RDL,X
                DEX
                BPL     @g
                RTS

VFILL:          JSR     SETATR
                STZ     RCL
                STZ     RCH
                STZ     RDL
                STZ     RDH
                LDA     VWMX
                STA     GXPOS
                LDA     VWMX + 1
                STA     GXPOS + 1
                LDA     VWMY
                STA     GYPOS
                STZ     GYPOS + 1
                JSR     DOBOXF
                LDA     ATRBYT
                JMP     SETATR

; --- The OEM's: GRPSIZ, PIXSIZ, GETFBC, SETATR ----------------------------------------------

; GRPSIZ: CX the last x (319, 639), DX the last y (199); 7e4: the
; viewport's (VWMX, VWMY).
GRPSIZ:         LDA     VWMX
                STA     RCL
                LDA     VWMX + 1
                STA     RCH
                LDA     VWMY
                STA     RDL
                STZ     RDH
                RTS

; PIXSIZ: A the bits a pixel (2, 1; 0 in SCREEN 0: no graphics), Z by it.
PIXSIZ:         LDX     GMODEV
                LDA     PIXBIT,X
                RTS

; GETFBC (carry set: the graphics' colours): A the foreground (SCREEN 1's
; 3, SCREEN 2's 1), X the background (0).
GETFBC:         LDX     GMODEV
                LDA     PIXFG,X
                LDX     #0
                RTS

; SETATR: A an attribute (0-255) made the current one, the mode's last
; colour for any past it (PC-BASIC's: PSET of 7 or 255 in SCREEN 1 is 3, of
; 2 in SCREEN 2 is 1), and SETC's pattern of it. Carry clear: every
; attribute is legal.
SETATR:         LDX     GMODEV
                CMP     PIXMSK,X
                BCC     :+
                LDA     PIXMSK,X
:               PHA
                TAY
                LDA     PIXPAT,Y
                CPX     #GM_TILE
                BEQ     :+
                LDA     #0             ; mono: 0 or 1 in every bit
                CPY     #0
                BEQ     :+
                LDA     #$FF
:               STA     CPAT
                PLA
                CLC
                RTS

; GTASPC (7e2; the OEM's, OEM.H names it): BX the screen's aspect x 256 and
; DX its inverse x 256, for CIRCLE's default and DRAW's A1 and A3: the IBM
; manual's 5/6 in SCREEN 1 and 5/12 in SCREEN 2 (pp. 30, 77), each x 256 to
; the nearest (213, 107; 307, 614: the owner's G8). SCREEN 0's (no
; graphics: CIRCLE and DRAW are Illegal function call there) 1.
GTASPC:         LDX     GMX
                LDA     ASPBX,X
                STA     RBL
                LDA     ASPBX + 1,X
                STA     RBH
                LDA     ASPDX,X
                STA     RDL
                LDA     ASPDX + 1,X
                STA     RDH
                RTS


.segment "RODATA"
; GW-BASIC's palette's groups: the console's CGA entries (PALSRC) to theirs.
PALSRC:         .byte   2, 4, 6, 3, 5, 7, 15
PALDST:         .byte   17, 18, 19, 33, 34, 35, 49
; By mode, 1 and 2 (the tables from mode 1): page size, its cleared bytes,
; layer 0's CONFIG (tiles: 4 bits, 8 x 8, the map 64 wide; mono), its map
; past the page (SCREEN 1's after the tiles), VSCROLL (the tiles' 20 layer
; lines above the 200, the mono layer's 40 above the 400).
GSIZE:          .word   T1PAGES + MAPPAGES, M2PAGES
GBYTES:         .word   T1BYTES, M2BYTES
GCONF:          .word   $22, $05
GMAP:           .word   T1PAGES, 0
GVS:            .word   256 - 20, 480 - 40
GMAXT:          .word   319, 639
GGRPT:          .word   2, 3
; By mode, 0-2.
PIXBIT:         .byte   0, 2, 1
PIXFG:          .byte   0, 3, 1
PIXMSK:         .byte   0, 3, 1        ;   the last colour
PIXPAT:         .byte   $00, $11, $22, $33
ASPBX:          .word   256, 213, 107  ; GTASPC, by mode (GMX)
ASPDX:          .word   256, 307, 614

.segment "BRAM"
GMODEV:         .res    1              ; the mode: GM_TEXT, GM_TILE, GM_MONO
GMX:            .res    1              ;   x 2, the tables' index
GNEW:           .res    1              ; GMODE's
GGRP:           .res    1              ; the graphics' palette group (PALOFS)
GBASE:          .res    2              ; the mode's page (a card page, on a card bank)
GMAXX:          .res    2              ; its last x
GPAGES:         .res    4              ; SCREEN 1's and 2's pages (0: none yet)
GPAL:           .res    2              ; GW-BASIC's palette (0: none yet)
CPAT:           .res    1              ; SETATR's pattern: the attribute in every pixel of a byte
; 7e4: the viewport, in GVIEW's order (VWOX to VWMY), then VIEW SCREEN's
; (VWSX, VWSY as VWOX, VWOY) and P1's (GVWGET: VWMX to VWSY)
VWOX:           .res    2              ; its corner on the screen, for MAPXYC
VWOY:           .res    1
VWMX:           .res    2              ; its last x and y, from the corner (GMAXX and 199 without)
VWMY:           .res    1
VWSX:           .res    2              ; VIEW SCREEN's offset (its corner; else 0), for SCAND
VWSY:           .res    1
VWON:           .res    1              ; nonzero: a viewport set (GCLS)
LSTYL:          .res    2              ; LINE's style (p5/gengrp.s): $FFFF, solid, but inside GLINEP
GVF:            .res    1              ; GVIEW's flags
GVB:            .res    8              ;   the viewport (VWOX to VWMY), the fill, the border
GVBN            = * - GVB
.assert VWMY - VWOX = 5 && VWSX - VWOX = 6 && VWSY - VWSX = 2, error, "the viewport's RAM: GVIEW's and GVWGET's order"
