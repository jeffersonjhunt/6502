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
;             groups: answer 10; 7e5, its writes P3's VPAL, the space
;             plan's reserve (a)) allocated, the page cleared (DM_VFILL),
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
;             parameter (p. 43's form has two) and in SCREEN 2 (p. 43).
;             7e5: the checks and the background P3's VCOLOR
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
.import GX_VALLOC, GX_VADDR, GX_VFILL, GX_VPUT, GX_LAYER, GX_SCROLL, GX_PALOFS
.import GX_PALBASE, GX_V, GX_R, GX_X, GX_Y, GX_A

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
GMGFX:          LDX     GNEW           ; the mode's palette, at its first use: GW-BASIC's,
                LDY     GPLX - 1,X     ; or SCREEN 10's own (7e5)
                LDA     GPALS,Y
                ORA     GPALS + 1,Y
                BNE     @page
                PHY
                LDA     #4
                STA     GX_V
                STZ     GX_V + 1
                LDA     #0
                JSR     GX_VALLOC
                PLY
                BCS     GMFAIL
                STA     GPALS,Y
                TXA
                STA     GPALS + 1,Y
                TYA                    ; 7e5 (U1, the reserve's (a)): its entries set by
                LSR     A              ; P3's VPAL: 1 GW-BASIC's, 2 SCREEN 10's
                INC     A
                LDX     GNEW
                JSR     GVPAL
@page:          LDA     GNEW           ; the mode's page, at its first use
                ASL     A
                TAX                    ; (GPAGES - 2),X: its card page
                LDA     GPAGES - 2,X
                ORA     GPAGES - 1,X
                BNE     @have
                PHX
                LDA     GSIZE - 2,X
                STA     GX_V
                LDA     GSIZE - 1,X    ; (7e5: SCREEN 11's 480 pages)
                STA     GX_V + 1
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
                LDA     GMAXYT - 2,X   ; 7e5: and down, 200
                STA     GMAXY
                LDA     GMAXYT - 1,X
                STA     GMAXY + 1
                LDX     GMODEV
                LDA     GGRPT - 1,X    ; SCREEN 1's palette 1, SCREEN 2's white
                STA     GGRP
                LDA     #1             ; 7e4: a graphics mode, no window (GFLG),
                STA     GFLG           ; the viewport the whole screen
                JSR     VWRST
                JSR     GPAGE          ; the page cleared, the cursor centred
                SEC                    ; 7e2: DRAW's colour the mode's foreground (the IBM
                JSR     GETFBC         ; manual's p. 74: C's default the mode's last)
                JSR     SETATR
                STA     ATRBYT
                LDX     GMODEV         ; layer 0: the frame or the mono rows
                LDA     GMAP - 1,X
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
                LDA     GCONF - 1,X
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
                STZ     RDL            ; the background black (the console's entry 0:
                LDA     #0             ; P3's VPAL, 7e5)
                LDX     GMODEV
                JSR     GVPAL
                LDA     RCL            ; PALBASE the mode's palette (GVPAL's GVPG)
                LDX     RCH
                JSR     GX_PALBASE
                CLC
                RTS

; GPAGE: the mode's page cleared (its tiles, or all its rows: rows 400-479
; of the mono layer, under the bands, too), and the graphics cursor centred.
; 7e5: in pieces of at most 61,440 bytes (GX_VFILL's count is 16 bits:
; SCREEN 11's 122,880 two).
GPAGE:          LDA     GBASE
                LDX     GBASE + 1
                JSR     GX_VADDR
                LDX     GMODEV
                LDY     GPCS - 1,X
@p:             LDX     GMX
                LDA     GBYTES - 2,X
                STA     GX_V
                LDA     GBYTES - 1,X
                STA     GX_V + 1
                LDA     #0
                PHY
                JSR     GX_VFILL       ; (GX_A kept)
                PLY
                CLC                    ; the next piece, 61,440 on
                LDA     GX_A + 1
                ADC     #>61440
                STA     GX_A + 1
                BCC     :+
                INC     GX_A + 2
:               DEY
                BNE     @p
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

; 7e5 (U1): the parameters' checks and the background by P3's VCOLOR (the
; palette's write is P3's: the reserve's (a)), the group here.
GCOLOR:         PHA                    ; (the count, across GVPG: the owner's answer 12)
                LDX     GMODEV
                STX     RAL
                JSR     GVPG           ; (the mode's palette)
                PLA
                FJSR    VCOLOR
                BCS     @x
                TAX                    ; the palette's group, if given: both layers
                BEQ     @x
                STA     GGRP
                LDA     #0
                JSR     GX_PALOFS
                LDX     GGRP
                LDA     #1
                JSR     GX_PALOFS
                CLC
@x:             RTS

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

; GVPAL (7e5, U1): P3's VPAL, A the operation, on mode X's palette (GVPG:
; RCL/RCH its card page, GW-BASIC's, or SCREEN 10's own, U3).
GVPAL:          PHA
                JSR     GVPG
                PLA
                FJSR    VPAL
                RTS
GVPG:           LDY     GPLX - 1,X
                LDA     GPALS,Y
                STA     RCL
                LDA     GPALS + 1,Y
                STA     RCH
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
;           VWMY, words since 7e5, then the fill and the border: on into T0
;           and T1), RCL/RCH, RDL/RDH the border box's
;           first corner (x1 - 1, y1 - 1), RBL/RBH, RAL/RAH its second. The
;           viewport the whole screen; the border box drawn there, each side
;           clipped to the screen as any line; the viewport set (VIEW SCREEN
;           its points absolute: VWSX, VWSY its corner), the fill over it;
;           no corners (VIEW, VIEW SCREEN alone) the whole screen. Then the
;           cursor at its centre (GRPINI), and with a WINDOW on P1's WINRSC
;           maps it onto the new viewport
;   GVWGET  RDL/RDH VWMX, RBL/RBH VWMY, RAL/RAH VWSX, RCL/RCH VWSY (for
;           P1; 7e5: each a word)
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
                LDA     GMAXY          ; 7e5: the mode's last y, a word
                STA     VWMY
                LDA     GMAXY + 1
                STA     VWMY + 1
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
                LDA     GVB + 9        ; the border, one pixel outside
                JSR     SETATR
                LDX     #3             ; the second corner (and the cursor's,
@b:             LDA     RBL,X          ;   which DOLINE takes back for BOXLIN)
                STA     GXPOS,X
                STA     GRPACX,X
                DEX
                BPL     @b
                JSR     BOXLIN
@set:           LDX     #VWMY + 1 - VWOX ; the viewport
@v:             LDA     GVB,X
                STA     VWOX,X
                DEX
                BPL     @v
                INC     VWON
                BIT     GVF            ; VIEW SCREEN: its points absolute
                BVC     @f
                LDX     #VWMX - VWOX - 1
@s:             LDA     VWOX,X         ; (VWSX, VWSY as VWOX, VWOY, words)
                STA     VWSX,X
                DEX
                BPL     @s
@f:             LDA     GVF
                AND     #$20
                BEQ     @end
                LDA     GVB + 8        ; the fill
                JSR     VFILL
@end:           JSR     GRPINI         ; the cursor at the centre
                LDA     ATRBYT         ; CPAT back (the border's)
                JSR     SETATR
                BIT     GFLG           ; a WINDOW: onto the new viewport
                BPL     @x
                JSR     GVWGET
                FJSR    WINRSC
@x:             RTS

GVWGET:         LDX     #VWSY + 1 - VWMX ; (VWMX to VWSY, words)
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
                LDA     VWMY + 1
                STA     GYPOS + 1
                JSR     DOBOXF
                LDA     ATRBYT
                JMP     SETATR

; --- The OEM's: GRPSIZ, PIXSIZ, GETFBC, SETATR ----------------------------------------------

; GRPSIZ: CX the last x (319, 639), DX the last y (199); 7e4: the
; viewport's (VWMX, VWMY; 7e5: a word).
GRPSIZ:         LDA     VWMX
                STA     RCL
                LDA     VWMX + 1
                STA     RCH
                LDA     VWMY
                STA     RDL
                LDA     VWMY + 1
                STA     RDH
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
                JSR     CPATOF
                STA     CPAT
                PLA
                CLC
                RTS

; CPATOF (7e5): A an attribute (at most the mode's last) as a byte with it
; in every pixel: in each nibble in SCREEN 1 (a 4-bit tile's), in every bit
; in SCREEN 2. Y kept.
CPATOF:         LDX     GMODEV
                CPX     #GM_TILE
                BEQ     @n
                TAX                    ; a bit a pixel: 0 or every bit
                BEQ     @x
                LDA     #$FF
@x:             RTS
@n:             PHA                    ; a nibble a pixel
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     CPT
                PLA
                ORA     CPT
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
; By mode, 1 and 2 (the tables from mode 1): page size, its cleared bytes,
; layer 0's CONFIG (tiles: 4 bits, 8 x 8, the map 64 wide; mono), its map
; past the page (SCREEN 1's after the tiles), VSCROLL (the tiles' 20 layer
; lines above the 200, the mono layer's 40 above the 400).
; 7e5 (U3): and the OO-1's, SCREEN 10, 11 and 12, modes 3-5 (GM_M10 on: the
; OO-1's choices, the owner's answer 11 to the 7e5 plan): bitmap 256 (type
; 3, 240 pages: 61,440 bytes, a byte a pixel), bitmap 512 (type 4, 480
; pages, two pieces to clear, a nibble a pixel, the left high), mono (type
; 5, 150 pages, a row 80 bytes); none scrolled; SCREEN 10's colours its own
; palette's entries (PALOFS 15 for the text over it: entry 255), SCREEN
; 11's GW-BASIC's group 0 (the console's CGA colours), SCREEN 12's group 3.
GSIZE:          .word   T1PAGES + MAPPAGES, M2PAGES, 240, 480, M2PAGES
GBYTES:         .word   T1BYTES, M2BYTES, 61440, 61440, M2BYTES
GVS:            .word   256 - 20, 480 - 40, 0, 0, 0
GMAXT:          .word   319, 639, 255, 511, 639
GMAXYT:         .word   199, 199, 239, 479, 479 ; 7e5: the last y
GCONF:          .byte   $22, $05, $03, $04, $05 ; 7e5: bytes, by GMODEV from 1
GMAP:           .byte   T1PAGES, 0, 0, 0, 0
GGRPT:          .byte   2, 3, 15, 0, 3
GPCS:           .byte   1, 1, 1, 2, 1  ;   GPAGE's pieces
GPLX:           .byte   0, 0, 2, 0, 0  ;   the palette (GPALS's): GW-BASIC's, SCREEN 10's
; By mode, 0-5.
PIXBIT:         .byte   0, 2, 1, 8, 4, 1
PIXFG:          .byte   0, 3, 1, 255, 15, 1
PIXMSK:         .byte   0, 3, 1, 255, 15, 1 ; the last colour
ASPBX:          .word   256, 213, 107, 256, 256, 256 ; GTASPC, by mode (GMX); 7e5: the
ASPDX:          .word   256, 307, 614, 256, 256, 256 ;   OO-1's, 1 (7e's answer 4)

.segment "BRAM"
GMODEV:         .res    1              ; the mode: GM_TEXT, GM_TILE, GM_MONO
GMX:            .res    1              ;   x 2, the tables' index
GNEW:           .res    1              ; GMODE's
GGRP:           .res    1              ; the graphics' palette group (PALOFS)
GBASE:          .res    2              ; the mode's page (a card page, on a card bank)
GMAXX:          .res    2              ; its last x
GMAXY:          .res    2              ;   and y (7e5)
GPAGES:         .res    10             ; SCREEN 1's, 2's (7e5: 10's, 11's, 12's) pages (0: none yet)
GPALS:                                 ; the palettes (0: none yet):
GPAL:           .res    2              ;   GW-BASIC's
GPAL10:         .res    2              ;   SCREEN 10's own (7e5)
CPAT:           .res    1              ; SETATR's pattern: the attribute in every pixel of a byte
CPT:            .res    1              ; CPATOF's
; 7e4: the viewport, in GVIEW's order (VWOX to VWMY), then VIEW SCREEN's
; (VWSX, VWSY as VWOX, VWOY) and P1's (GVWGET: VWMX to VWSY)
VWOX:           .res    2              ; its corner on the screen, for MAPXYC
VWOY:           .res    2              ;   (7e5: y a word, as each below)
VWMX:           .res    2              ; its last x and y, from the corner (GMAXX and GMAXY without)
VWMY:           .res    2
VWSX:           .res    2              ; VIEW SCREEN's offset (its corner; else 0), for SCAND
VWSY:           .res    2
VWON:           .res    1              ; nonzero: a viewport set (GCLS)
LSTYL:          .res    2              ; LINE's style (p5/gengrp.s): $FFFF, solid, but inside GLINEP
GVF:            .res    1              ; GVIEW's flags
GVB:            .res    10             ;   the viewport (VWOX to VWMY), the fill, the border
GVBN            = * - GVB
.assert VWMY - VWOX = 6 && VWSX - VWOX = 8 && VWSY - VWSX = 2 && VWMY - VWMX = 2, error, "the viewport's RAM: GVIEW's, GVWGET's and XPAST's order"
