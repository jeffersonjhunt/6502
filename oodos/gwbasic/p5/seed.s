; P5's seed (hw/oodos.md, "The plan, phase 7": 7c's tail; V3, and B7's
; question 5): block P5, which 7d and 7e fill later. The project's own, the
; OEM's (GW-BASIC's PEKFLT and POKFLT were IBM's, and its 8086 needs no
; router: a segment is a segment). For now it holds OOLIB's gxmem.s, linked
; in (../lib/gxmem.s: GX_INIT, GX_BANK8, GX_BLOCK, GX_W1BACK), and:
;
;   GXSTRT    start-up, from P2's GWINIT, once: CON:'s output read (the
;             SCB's $24, function 49), GX_INIT with no event area (A/X 0),
;             and the screen chosen from the two (7d1, the owner's answer 2
;             of 2026-11-18): GW-BASIC's own text layer (SCRMOD bit 7) only
;             when GX_INIT took it (a revision 2 card, OODOS's interface 8 or
;             later, with DMA) and CON:'s output includes the CRT; else 7a's
;             console mode. Then GX_BANK8 whatever GX_INIT answered (with no
;             revision 2 card GX_BANK8 is a success that changes nothing,
;             bank 8 being RAM already: oolib.inc), then the scratch byte's
;             physical address (function 73)
;   DSBYTE    PEEK's and POKE's byte, for an address that isn't segment 0
;             below $C000 (the callers' own fast path): the 20-bit address
;             SAVSEG x 16 + DX routed as hw/oodos.md's DEF SEG section has
;             it (below)
;   DSINFO    7e2: for the home block's DSRUNH (p8/far4.s: BLOAD's and
;             BSAVE's run checks, 7c's tail and 7d3, the home block's since
;             7e2), A the graphics mode, X DSB8; DSPUT's entry a stub
;   DSGRUN    7e3: a BSAVE's or BLOAD's run on SCREEN 1 or 2's page, between
;             bank 8's RAM (the staging) and the card, from the home block's
;             DSSRUN and DSPUTH (below)
;   TXMODE    P6's text page shown: on layer 0, or (7e1) on layer 1 over
;             the graphics (below)
;
; 7e1: the graphics are p5/gfx.s (the modes), p5/gfxoem.s (the pixels) and
; p5/gengrp.s (the statements). 7e3: in SCREEN 1 and 2 the screen page is
; the PC's CGA layout (DSGPK, DSGRUN, below).
;
; The address is the segment x 16 + the offset, wrapping at 1 MB as an
; 8088's does. DSBYTE's routes:
;   1. bank 8, $80000-$8FFFF, by the 20-bit address, not the segment (B7's
;      question 2; 7d3's answer 1), so &H7FFF:&H100 is in it: in screen mode
;      the active page's card memory (7d3, question 10: DSSCR), its offset
;      the page's, up to the reach (25 rows the PC's page, 4,096 or 2,048
;      bytes; 30 rows the text, 4,800 or 2,400), by function 83's DM_VREAD
;      and DM_VWRITE; past the reach, in console mode, and the rest of bank
;      8: Illegal function call, PEEK included;
;   2. blocks 0-2 (the program's TPA, W0-W2): a load or store there;
;   3. any other block: GX_BLOCK, the load or store at $4000 + its place in
;      the block, GX_W1BACK (an allocated block, P1-P8's among them, or
;      banks 9-B);
;   4. if GX_BLOCK refuses (the block isn't in OOLIB's W1 map, or there is
;      no revision 2 card, B7's question 3): one byte by function 83's
;      DM_COPY through the scratch byte, so OODOS decides what may be
;      reached and nothing of its own list is copied here. A refused PEEK
;      is 0 (the scratch byte cleared first); a refused POKE is Illegal
;      function call (OODOS's banks 6, 7, C, D, F).
; While W1 holds another block (route 3) DSBYTE touches only zero page and
; this block's own RAM, and calls nothing resident, FCERR included: P5 calls
; resident code only with block 1 in W1 (the program's $4000-$7FFF).
;
; BLOAD's and BSAVE's runs (DSRUN, DSSRUN, DSPUT: 7c's tail and 7d3) are
; the home block's since 7e2 (p8/far4.s, the owner's option 3).
;
; The far call rule: every routine returns by RTS, or ends in FCERR with
; block 1 in W1.

.include "gwbasic.inc"
.include "oolib.inc"
.include "p5/p5.inc"

;@entry GXSTRT
;@entry DSBYTE
;@entry DSINFO
;@entry TXMODE
;@entry DSPUT
; 7e1's entries (p5/gfx.s, p5/gengrp.s) after the seed's, so these keep
; their places in the jump table and the blocks that call them are as they
; were (gwgen.py numbers the entries by file, then by line).
;@entry GMODE
;@entry GCOLOR
;@entry GCLS
;@entry GRPRST
;@entry GRPINI
;@entry PSETP
;@entry PRSETP
;@entry POINTP
;@entry GLINEP
;@entry GCELL
; 7e2's (p5/advgrp.s), after the rest.
;@entry CIRCP
;@entry CIRCC
;@entry PAINTP
;@entry DRWOP
; 7e3's (p5/advgrp.s, here), after 7e2's.
;@entry GPUTG
;@entry DSGRUN
; 7e4's (p5/gfx.s), after 7e3's.
;@entry GVIEW
;@entry GVWGET
.export GXSTRT, DSBYTE, DSINFO, TXMODE, DSPUT, DSGRUN
.exportzp GXP, GXQ, GXT                ; gxmem's (lib/zpgxmem.s's), at gwbasic.inc's addresses
.import FCERR, SAVSEG, SCRMOD, EVAREA, TXBUF

; 7e3: DSGRUN's and DSGXY's, in zero page: the floating point's scratch past
; GET and PUT's rows' (p5/gfxoem.s), which nothing uses while a statement
; draws or a PEEK runs (DSK and DSN live only inside DSGRUN, a statement's).
DSR             = DBUFF + 23           ; DSGXY: the byte in the row
DSK             = FBPTR                ; DSGRUN: the offset
DSN             = FBPTR + 2            ;   the bytes left
.assert DSN + 1 < SPN, error, "DSGRUN's zero page past FOUT's scratch"

CON_CRT         = $80                  ; an SCB vector's high byte: bit 15, the CRT (the Programmer's
                                       ;   Guide, chapter 4: bit 15 CRT, 14 TTY, 13 LPT)

.code

; --- GXSTRT -------------------------------------------------------------------------------

; GXSTRT: OOLIB started with no event area (7d opens one and calls GX_INIT
; and GX_BANK8 again, since GX_INIT goes back to the old mode), bank 8 made
; the program's RAM (B7's question 5), and the scratch byte's physical
; address, by function 73 while this block is in W3. Their answers aren't
; needed: GX_BLOCK refuses whatever GX_INIT didn't allow.
GXSTRT:         LDA     #<SCBPB        ; CON:'s output (SCB $24): X its high byte
                LDX     #>SCBPB
                LDY     #S_SCB
                JSR     BDOS
                TXA
                AND     #CON_CRT
                STA     DSVAL          ; (DSBYTE's, free until a PEEK)
                BEQ     @none          ; no CRT: no event area, as 7c's tail
                LDA     #<FTOPEN       ; 7d1: the event area (layout 2), the
                LDX     #>FTOPEN       ; keys' source on the screen (V2)
                LDY     #O_FRAME
                JSR     BDOS
                LDA     #<EVAREA
                LDX     #>EVAREA
                JSR     GX_INIT
                BCC     @scr
                LDA     #FT_CLOSE      ; refused: the area closed, the console
                LDX     #0
                LDY     #O_FRAME
                JSR     BDOS
                BRA     @con
@none:          LDA     #0
                TAX
                JSR     GX_INIT
                BRA     @con
@scr:           LDA     DSVAL
                STA     SCRMOD         ; bit 7: the CRT, so GW-BASIC's own screen
@con:           JSR     GX_BANK8
                ROR     DSB8           ; 7d3: bit 7, GX_BANK8 refused (bank 8 isn't RAM)
                LDA     #<SCRB
                STA     DSPB
                LDA     #>SCRB
                STA     DSPB+1
                LDA     #<DSPB
                LDX     #>DSPB
                LDY     #O_PHYSADDR
                JSR     BDOS
                LDA     DSPB+2         ; the paragraph x 16 + the offset (0-15)
                STA     SCRP
                LDA     DSPB+3
                STA     SCRP+1
                STZ     SCRP+2
                LDX     #4
@x16:           ASL     SCRP
                ROL     SCRP+1
                ROL     SCRP+2
                DEX
                BNE     @x16
                LDA     SCRP
                ORA     DSPB
                STA     SCRP
; 7d1: the text pages (SCREEN 0's: 4 of 5 KB at 80 columns, 8 of 3 KB at
; 40; the owner's answer 6), 24 KB of the program's card memory at once:
; A/X their first card page, for P6's SCNIPL. None to be had: the console.
                BIT     SCRMOD
                BPL     @x
                LDA     #<TXPAGES
                STA     GX_V
                LDA     #>TXPAGES
                STA     GX_V + 1
                LDA     #4             ; on a kilobyte (VM_UNIT)
                JSR     GX_VALLOC
                BCC     @x
                STZ     SCRMOD
@x:             RTS

; --- TXMODE (7d1) -------------------------------------------------------------------------

; TXMODE: P6's text page on layer 0: A/X its card page (MAPBASE), the font
; the console's at $000400, read only (the owner's answer 5); Y: bit 7
; BLINK, bit 6 25 rows centred (VSCROLL 440: the owner's answer 10), bits
; 5-2 the border, bit 0 40 columns. The layer alone shown. 7e1: Y's bit 1,
; the overlay (a graphics mode, the owner's answer 5 of 2026-11-19): the
; text on layer 1 instead, over the graphics on layer 0, in their palette
; group (PALOFS, GGRP), and both layers shown.
TXMODE:         STA     GX_V
                STX     GX_V + 1
                LDA     #<(VM_FONT / VM_PAGE)
                STA     GX_R
                LDA     #>(VM_FONT / VM_PAGE)
                STA     GX_R + 1
                STY     DSVAL
                TYA
                AND     #$81           ; CONFIG: BLINK, and type 0 (80) or 1 (40)
                TAX
                JSR     TXLAY          ; A: the layer
                JSR     GX_LAYER
                STZ     GX_X
                STZ     GX_X + 1
                STZ     GX_Y
                STZ     GX_Y + 1
                BIT     DSVAL
                BVC     :+
                LDA     #<440
                STA     GX_Y
                LDA     #>440
                STA     GX_Y + 1
:               JSR     TXLAY
                JSR     GX_SCROLL
                LDA     DSVAL
                LSR     A
                LSR     A
                AND     #$0F
                JSR     GX_BORDER
                JSR     TXLAY
                BEQ     @one
                LDX     GGRP           ; the overlay: layer 1 in the graphics'
                JSR     GX_PALOFS      ; group, both layers shown
                LDA     #$06
                BRA     :+
@one:           LDA     #$02           ; layer 0
:               JMP     GX_SHOW
; TXLAY: A the text's layer: 1 with the overlay (DSVAL's bit 1), else 0; Z
; by it.
TXLAY:          LDA     DSVAL
                AND     #$02
                LSR     A
                RTS

; --- DSBYTE -------------------------------------------------------------------------------

; DSBYTE: DX the offset, SAVSEG the segment; C clear PEEK, A the byte back;
; C set POKE A. X and Y aren't kept.
DSBYTE:         STA     DSVAL
                ROR     DSDIR          ; bit 7: a POKE
                LDA     SAVSEG
                STA     GXQ
                LDA     SAVSEG+1
                STA     GXQ+1
                LDA     RDL
                STA     GXP
                LDA     RDH
                STA     GXP+1
                JSR     DSADR          ; DSA: the 20-bit address
                LDA     DSA+2
                CMP     #8
                BEQ     DSSCR          ; 1. bank 8: the screen page (7d3)
                LDX     #0
                JSR     BLKOF          ; A: its block
                CMP     #3
                BCS     DSWIN
                LDA     DSA            ; 2. blocks 0-2: the CPU's own address
                STA     GXP
                LDA     DSA+1
                STA     GXP+1
                BRA     DSDO
DSWIN:          JSR     GX_BLOCK       ; 3. the block in W1
                BCS     DSCOPY
                LDA     DSA            ; $4000 + its place in the block
                STA     GXP
                LDA     DSA+1
                AND     #$3F
                ORA     #$40
                STA     GXP+1
                JSR     DSDO
                PHA
                JSR     GX_W1BACK      ; block 1 back, before anything else
                PLA
                RTS
; DSDO: the load or store at (GXP).
DSDO:           BIT     DSDIR
                BMI     @poke
                LDA     (GXP)
                RTS
@poke:          LDA     DSVAL
                STA     (GXP)
                RTS
; 4. One byte by DM_COPY through the scratch byte: a PEEK's from the
; address (0 if refused), a POKE's to it (refused: Illegal function call).
DSCOPY:         LDX     #2
@a:             LDA     DSA,X          ; the address, a PEEK's source
                STA     DSCB+1,X
                LDA     SCRP,X         ; the scratch byte, its destination
                STA     DSCB+4,X
                DEX
                BPL     @a
                BIT     DSDIR
                BMI     @poke
                STZ     SCRB
                JSR     DSCALL
                LDA     SCRB
                RTS
@poke:          JSR     DSSWAP         ; the other way round
                LDA     DSVAL
                STA     SCRB
                JSR     DSCALL
                CMP     #$FF
                BEQ     DSFC
                RTS
DSFC:           JMP     FCERR          ; (block 1 in W1: GX_BLOCK refused, or never asked)

; DSSCR (7d3): bank 8 in screen mode is the active page's card memory, its
; byte DSA's offset; else (console mode, or past the reach) Illegal function
; call. P6's SCPAGP first: TXBUF out, the cached row back, and for a POKE
; forgotten. One byte by DM_VREAD or DM_VWRITE through the scratch byte.
DSSCR:          BIT     SCRMOD
                BPL     DSFC
                LDA     GMODEV         ; 7e3: SCREEN 1 and 2's page, the PC's CGA layout
                BEQ     :+
                JMP     DSGPK
:
                LDA     DSA            ; the byte is the run's last (DSPAGE)
                STA     DSE
                LDA     DSA+1
                STA     DSE+1
                LDA     DSDIR          ; C: a POKE
                ASL     A
                FJSR    SCPAGP
                JSR     DSPAGE
                BCS     DSFC
                LDX     #2
@s:             LDA     SCRP,X
                STA     DSPH,X
                DEX
                BPL     @s
                LDA     #1
                STA     DSVL
                STZ     DSVL+1
                LDA     DSVAL
                STA     SCRB
                LDA     #DM_VWRITE
                BIT     DSDIR
                BMI     :+
                LDA     #DM_VREAD
:               JSR     DSVX
                LDA     SCRB
                RTS

; DSPAGE (7d3): DSVC the page's card address A/X/Y + DSA's offset; C set if
; DSE's offset (the run's last byte) isn't below the reach, GXT.
DSPAGE:         CLC
                ADC     DSA
                STA     DSVC
                TXA
                ADC     DSA+1
                STA     DSVC+1
                TYA
                ADC     #0
                STA     DSVC+2
                LDA     DSE
                CMP     GXT
                LDA     DSE+1
                SBC     GXT+1
                RTS

; DSVX (7d3): function 83's A (DM_VREAD, from the card's DSVC to the
; physical DSPH; DM_VWRITE, the other way), DSVL bytes.
DSVX:           STA     DSCB
                LDX     #2
@a:             LDA     DSVC,X
                STA     DSCB+1,X
                LDA     DSPH,X
                STA     DSCB+4,X
                DEX
                BPL     @a
                LDA     DSVL
                STA     DSCB+7
                LDA     DSVL+1
                STA     DSCB+8
                LDA     DSCB
                CMP     #DM_VREAD
                BEQ     DSGO
                JSR     DSSWAP
                BRA     DSGO

; DSSWAP: DSCB's source and destination swapped.
DSSWAP:         LDX     #2
@b:             LDA     DSCB+1,X
                PHA
                LDA     DSCB+4,X
                STA     DSCB+1,X
                PLA
                STA     DSCB+4,X
                DEX
                BPL     @b
                RTS

; DSCALL: function 83's DM_COPY of DSCB's one byte. A the result.
DSCALL:         LDA     #DM_COPY
                STA     DSCB
                LDA     #1
                STA     DSCB+7
                STZ     DSCB+8
DSGO:           LDA     #<DSCB
                LDX     #>DSCB
                LDY     #O_DMA
                JMP     BDOS

; DSADR: DSA = GXQ (a segment) x 16 + GXP (an offset), wrapping at 1 MB.
DSADR:          LDA     GXQ
                STA     DSA
                LDA     GXQ+1
                STA     DSA+1
                STZ     DSA+2
                LDX     #4
@x16:           ASL     DSA
                ROL     DSA+1
                ROL     DSA+2
                DEX
                BNE     @x16
                CLC
                LDA     DSA
                ADC     GXP
                STA     DSA
                LDA     DSA+1
                ADC     GXP+1
                STA     DSA+1
                LDA     DSA+2
                ADC     #0
                AND     #$0F
                STA     DSA+2
DSRET:          RTS

; BLKOF: A the 16 KB block (0-63) of DSA (X 0) or DSE (X 3), a 20-bit
; address: A16-A19 x 4 and A14-A15.
BLKOF:          LDA     DSA+1,X
                ASL     A
                LDA     DSA+2,X
                ROL     A
                STA     DSBLK
                LDA     DSA+1,X
                ASL     A
                ASL     A
                LDA     DSBLK
                ROL     A
                RTS

; --- DSINFO, DSPUT (7e2) ------------------------------------------------------------------

; 7e2 (the owner's option 3 of 2026-11-19): BLOAD's and BSAVE's run checks
; and copies (DSRUN, DSSRUN, DSPUT) are the home block's now (p8/far4.s's
; DSRUNH, DSPUTH), which calls them without a far call; their entries keep
; their places in the jump table. DSINFO, DSRUN's: what DSRUNH needs of this
; block, A the graphics mode (GMODEV) and X DSB8 (bit 7: GX_BANK8 refused);
; 7e5: and for VIEW's checks (p8/far5.s) RDI the mode's last x, RBP its last
; y. DSPUT's is a stub, unused.
DSINFO:         LDA     GMAXX
                STA     RDI
                LDA     GMAXX + 1
                STA     RDI + 1
                LDA     GMAXY
                STA     RBP
                LDA     GMAXY + 1
                STA     RBP + 1
                LDA     GMODEV
                LDX     DSB8
DSPUT:          RTS

; --- SCREEN 1 and 2's page (7e3) ----------------------------------------------------------

; 7e3 (7e's answer 9; the owner's answers 2 and 7 to the 7e3 plan): in a
; graphics mode bank 8's byte k (&H8000:k) is the PC's CGA byte k: the even
; rows from 0 and the odd from &H2000, 80 bytes a row, the leftmost pixel in
; the high bits; its row 2 x ((k AND &H1FFF) \ 80) + (k >= &H2000), its byte
; in the row (k AND &H1FFF) MOD 80, from x = that x 4 (SCREEN 1) or x 8
; (SCREEN 2). The reach is &H4000; past it Illegal function call (7d3's
; rule). The gaps, &H1F40-&H1FFF and &H3F40-&H3FFF, no row's, are bank 8's
; RAM at their own address (answer 7: real bytes, as a PC's CGA RAM is; a
; PEEK reads back a POKE, BSAVE and BLOAD carry them, a change of mode
; doesn't clear them: the OO-1's choice, the source and the manual silent).
; The page is the graphics alone: the text over them isn't in it (7e's
; answer 5), and no SCPAGP (the text page isn't reached).
;
;   DSGPK   PEEK's and POKE's byte (DSSCR's branch): one CGA byte by GROWR
;           or GROWW with PSET (p5/gfxoem.s), through DSVAL, not TXBUF (a PEEK
;           may come in the middle of a PRINT); a gap's by GX_BLOCK, or PEEK
;           0 and POKE nothing where bank 8 isn't GW-BASIC's RAM
;   DSGRUN  BSAVE's or BLOAD's run, from the home block (p8/far4.s): RBL/RBH
;           its first offset, RDL/RDH its bytes (0: none); C set BSAVE (the
;           card to the staging, bank 8's RAM at the run's own address,
;           $80000 + k, for P4's BSAVEP), clear BLOAD (the staging, where P4's
;           BLDRUN put the file, to the card: the bytes the file gave,
;           DSPUTH's count). In pieces along the rows, at most 80 bytes each,
;           through TXBUF: the staging's side with it in W1 (GX_BLOCK, no
;           BDOS call), the card's by GROWR or GROWW through the aperture;
;           block 1 back at the end. A gap is passed over: its bytes are the
;           staging's own.
DSGPK:          LDA     DSA+1
                CMP     #$40
                BCC     :+
                JMP     DSFC           ; past the reach
:               JSR     DSGXY
                BCS     @gap
                JSR     MAPABS         ; 7e4: the screen's, whatever the viewport
                LDA     #1
                JSR     GPSETN
                LDA     #<DSVAL
                STA     PTR
                LDA     #>DSVAL
                STA     PTR+1
                BIT     DSDIR
                BMI     @poke
                JSR     GROWR
                BRA     @x
@poke:          JSR     GROWW
@x:             JSR     GX_W1BACK
                LDA     DSVAL
                RTS
@gap:           LDA     #$80000 / $4000 ; bank 8's first block, in W1
                JSR     GX_BLOCK
                BCS     @none
                LDA     DSA
                STA     GXP
                LDA     DSA+1
                ORA     #$40
                STA     GXP+1
                JSR     DSDO
                PHA
                JSR     GX_W1BACK
                PLA
                RTS
@none:          LDA     #0
                RTS

; DSGXY: DSA's offset k (below &H4000) as the cursor's point: RCL/RCH x,
; RDL/RDH y, DSR the byte in the row; C set (nothing else) if it is a gap's.
DSGXY:          LDA     DSA
                STA     T0
                LDA     DSA+1
                AND     #$1F
                STA     T1
                LDA     T0             ; (k AND &H1FFF) >= &H1F40: a gap
                CMP     #<$1F40
                LDA     T1
                SBC     #>$1F40
                BCS     @x
                STZ     T2             ; T2 its row in the bank, T0 the byte: \ 80, MOD 80
                LDX     #0
@d:             SEC
                LDA     T0
                SBC     D80,X
                TAY
                LDA     T1
                SBC     D80+1,X
                BCC     :+
                STA     T1
                STY     T0
:               ROL     T2
                INX
                INX
                CPX     #D80N
                BNE     @d
                LDA     T0
                STA     DSR
                LDA     DSA+1          ; y: twice the row, and the odd bank's bit
                AND     #$20
                CMP     #$20
                LDA     T2
                ROL     A
                STA     RDL
                STZ     RDH
                STZ     RCH            ; x: the byte x 4, or x 8
                LDA     T0
                ASL     A
                ROL     RCH
                ASL     A
                ROL     RCH
                LDX     GMODEV
                CPX     #GM_MONO
                BNE     :+
                ASL     A
                ROL     RCH
:               STA     RCL
                CLC
@x:             RTS

DSGRUN:         ROR     DSDIR          ; bit 7: BSAVE
                LDA     RBL
                STA     DSK
                LDA     RBH
                STA     DSK+1
                LDA     RDL
                STA     DSN
                LDA     RDH
                STA     DSN+1
@p:             LDA     DSN
                ORA     DSN+1
                BEQ     @x
                LDA     DSK
                STA     DSA
                LDA     DSK+1
                STA     DSA+1
                JSR     DSGXY
                BCS     @gap
                SEC                    ; the piece: to the row's end, at most the rest
                LDA     #80
                SBC     DSR
                JSR     DSPMIN
                BIT     DSDIR
                BMI     @save
                JSR     DSSTG          ; BLOAD: the staging's bytes to TXBUF, then the card
@c:             LDA     (PTR),Y
                STA     TXBUF,Y
                INY
                CPY     DSVL
                BNE     @c
                JSR     DSGPC
                JSR     GROWW
                BRA     @n
@save:          JSR     DSGPC          ; BSAVE: the card's to TXBUF, then the staging
                JSR     GROWR
                JSR     DSSTG
@s:             LDA     TXBUF,Y
                STA     (PTR),Y
                INY
                CPY     DSVL
                BNE     @s
                BRA     @n
@gap:           SEC                    ; a gap, to its end: &H2000 - (k AND &H1FFF), 1-192
                LDA     #0
                SBC     DSA
                JSR     DSPMIN
@n:             CLC
                LDA     DSK
                ADC     DSVL
                STA     DSK
                BCC     :+
                INC     DSK+1
:               SEC
                LDA     DSN
                SBC     DSVL
                STA     DSN
                BCS     @p
                DEC     DSN+1
                BRA     @p
@x:             JMP     GX_W1BACK
; DSPMIN: DSVL the piece, A or the bytes left (DSN) if fewer.
DSPMIN:         STA     DSVL
                STZ     DSVL+1
                LDA     DSN+1
                BNE     :+
                LDA     DSN
                CMP     DSVL
                BCS     :+
                STA     DSVL
:               RTS
; DSGPC: the piece's cursor (MAPXYC), DSVL CGA bytes by PSET, through TXBUF.
DSGPC:          JSR     MAPABS         ; 7e4: the screen's, whatever the viewport
                LDA     DSVL
                JSR     GPSETN
                JMP     GPTX
; DSSTG: the staging, bank 8's RAM, in W1 (GX_BLOCK: GW-BASIC's after
; GX_BANK8, which DSSRUN has checked): PTR the piece's byte there ($4000 +
; k), Y 0.
DSSTG:          LDA     #$80000 / $4000
                JSR     GX_BLOCK
                LDA     DSA
                STA     PTR
                LDA     DSA+1
                ORA     #$40
                STA     PTR+1
                LDY     #0
                RTS

.segment "BRAM"
.assert DSE = DSA + 3, error, "BLKOF takes DSE as DSA + 3"
DSA:            .res    3              ; the 20-bit address (DSADR)
DSE:            .res    3              ; DSSCR: the byte (DSPAGE; just past DSA: BLKOF)
DSBLK:          .res    1              ; a block (BLKOF)
DSVAL:          .res    1              ; a POKE's byte
DSDIR:          .res    1              ; bit 7: a POKE
DSCB:           .res    9              ; function 83's DM_COPY block
DSPB:           .res    4              ; function 73's
SCRB:           .res    1              ; the scratch byte
DSVC:           .res    3              ; 7d3: the screen page's card address (DSPAGE)
DSPH:           .res    3              ;   the physical side (the scratch byte, or bank 8's RAM)
DSVL:           .res    2              ;   the bytes
DSB8:           .res    1              ; 7d3: bit 7, GX_BANK8 refused (bank 8 not RAM)

.segment "RODATA"
TXPAGES         = 96                   ; card pages: 8 x 12, or 4 x 20
FTOPEN:         .byte   FT_OPEN, <EVAREA, >EVAREA, 0 ; function 80: the area, 256 bytes
SCBPB:          .byte   $24, 0, 0, 0   ; function 49: get SCB $24, CON:'s output vector
SCRP:           .res    3              ; and its physical address (GXSTRT)
D80:            .word   80 * 64, 80 * 32, 80 * 16, 80 * 8, 80 * 4, 80 * 2, 80 ; 7e3: DSGXY's \ 80
D80N            = * - D80
