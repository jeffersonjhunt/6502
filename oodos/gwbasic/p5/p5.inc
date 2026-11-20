; Block P5's shared names for the graphics (hw/oodos.md, "The plan, phase
; 7": 7e1): the project's own. p5/gfx.s sets the modes up (SCREEN 1 and 2,
; the palette, COLOR, CLS); p5/gfxoem.s has the OEM's pixel routines, which
; the 1983 source calls and leaves to the OEM (MAPXYC, SCALXY, SETC, READC,
; the moves); p5/gengrp.s is GENGRP.ASM's statements, translated. Their
; state is P5's own RAM (BRAM), and zero page only where the source's
; registers are (the pseudo-registers) and GXP, gxmem's pointer, which is
; also the graphics cursor's byte (C) while the aperture is in W1.
;
; The modes (GMODEV): 0 text (SCREEN 0); 1 SCREEN 1, 320 x 200 in 4
; colours, a frame of 8 x 8 4-bit tiles on layer 0 (tile n = column + 40 x
; row, 1,000 tiles and a blank one, 1,000, for the rest of the 64 x 32 map),
; each pixel shown 2 x 2; 2 SCREEN 2, 640 x 200 in 2, layer 0 mono 640 x
; 480, graphics row y on rows 2y and 2y + 1, each pixel shown 1 x 2. Both
; 400 lines, centred by VSCROLL; GW-BASIC's text over them on layer 1.

; p5/gfx.s
.global GMODEV, GMX, GGRP, GBASE, GMAXX, GMAXY, GRPSIZ, PIXSIZ, GETFBC, SETATR, CPAT, GTASPC, PIXMSK, CPATOF
.global VWOX, VWOY, VWSX, VWSY, VWON, VWMX, VWMY, LSTYL ; 7e4: the viewport, LINE's style
; p5/gfxoem.s
.global MAPXYC, MAPABS, SCALXY, SETC, READC, RIGHTC, LEFTC, DOWNC, UPC, NSETCX
.global FETCHC, STOREC, REMAP, RIGHTV, LEFTV, DOWNV, CSAV
.global CBANK, CMASK, CXB, CY7
.global NEWBNK
.global DRWSCL, DRWANG, DRWTA ; p5/advgrp.s (7e2)
.global SCANR, SCANL, TUPC, TDOWNC, PNTINI, PSTOR, PNX, PNY, PCSX, PBORD ; 7e2: PAINT's
.global TILEF, TLEN, TBG, TBGF, TROWS, TSET ; 7e4: PAINT's tiles
.global PGINIT, NREAD, NWRITE, GROWR, GROWW, GPSETN, GPTX, GPCPAT ; 7e3: GET and PUT's, DEF SEG's
; p5/gengrp.s
.global GRPACX, GRPACY, GXPOS, GYPOS, ATRBYT, GRPINI, DOGRPH, SCAN1
.global SCAND, XDELT, YDELT, XCHGX, XCHGY ; 7e3: GET and PUT's corners (p5/advgrp.s)
.global DOBOXF, BOXLIN, VSOFF ; 7e4: VIEW's fill and border (p5/gfx.s), DRAW's M (p5/advgrp.s)

GM_TEXT         = 0                    ; GMODEV: SCREEN 0
GM_TILE         = 1                    ;   SCREEN 1: the tile frame
GM_MONO         = 2                    ;   SCREEN 2: the doubled mono rows
TILES           = 1000                 ; SCREEN 1: the frame's tiles, then the blank one
T1PAGES         = 126                  ;   its tiles' card pages (1,001 x 32 bytes, 32,032)
T1BYTES         = (TILES + 1) * 32
MAPPAGES        = 16                   ;   the map's (64 x 32 entries of 2 bytes)
M2BYTES         = 80 * 480             ; SCREEN 2: the mono layer, 38,400 bytes
M2PAGES         = M2BYTES / 256        ;   150 card pages
CSIZE           = 6                    ; C kept by FETCHC: CSAV, its 4 bytes then GXP's 2
G_CPU           = $4000                ; W1, where slot 1 is (vmem.inc's VM_SLOT_CPU)
G_BANK          = $4000                ; a card bank (VM_BANK)
GALIGN          = 64                   ; a page on a card bank (16 KB): a byte's bank is its offset's top 2 bits
