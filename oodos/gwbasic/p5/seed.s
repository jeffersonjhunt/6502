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
;   DSRUN     BLOAD's and BSAVE's run checked whole, before anything moves:
;             its 20-bit start returned; a run on the screen page staged
;             in bank 8's RAM (7d3)
;   DSPUT     after BLOAD's BLDRUN (p8/far4.s): a run DSRUN staged for the
;             screen page, from bank 8's RAM to the card (7d3)
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
; DSRUN's run is linear, from segment x 16 + offset for its length (the
; 8086 wraps the offset inside the segment: a recorded deviation). Refused,
; Illegal function call: a run past $FFFFF (it would meet bank F, OODOS's);
; one that meets bank 8 but for a run on the screen page; one with any
; 16 KB block that function 83 won't copy from, found by a one-byte DM_COPY
; from each block's first byte (83 decides by the block, so one byte
; answers for all of it). A length of 0 is no run: nothing checked.
;
; 7d3, the screen page (the owner's answers 3-6 of 2026-11-18): a run inside
; bank 8, in screen mode, whose last byte is below the page's reach (DSRUN
; routes it by the 20-bit address, as DSBYTE does) is staged in bank 8's own
; RAM at its own address, $80000 + its offset (GW-BASIC's after GX_BANK8,
; B7's question 1): DSSRUN copies the page's bytes there by DM_VREAD, P4's
; BSAVEP saves them from there and P4's BLDRUN loads the file over them, and
; DSPUT then writes the run to the card by DM_VWRITE (so a file shorter than
; its header leaves the rest as it was). P4 is unchanged: its DM_COPY and
; DM_LOAD reach only GW-BASIC's RAM, and the card only function 83's port
; operations. Refused, Illegal function call: in console mode, where
; GX_BANK8 refused (bank 8 isn't RAM: a revision 1 bridge, a BIOS before
; 0.17; DSB8), a run from bank 7 into bank 8 or from it into bank 9, and one
; past the reach. The staging takes $80000 up to the reach ($812BF at most):
; nothing else of GW-BASIC's uses bank 8's RAM.
;
; The far call rule: every routine returns by RTS, or ends in FCERR with
; block 1 in W1.

.include "gwbasic.inc"
.include "oolib.inc"

;@entry GXSTRT
;@entry DSBYTE
;@entry DSRUN
;@entry TXMODE
;@entry DSPUT
.export GXSTRT, DSBYTE, DSRUN, TXMODE, DSPUT
.exportzp GXP, GXQ, GXT                ; gxmem's (lib/zpgxmem.s's), at gwbasic.inc's addresses
.import FCERR, SAVSEG, SCRMOD, EVAREA

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
; 5-2 the border, bit 0 40 columns. The layer alone shown.
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
                LDA     #0
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
:               LDA     #0
                JSR     GX_SCROLL
                LDA     DSVAL
                LSR     A
                LSR     A
                AND     #$0F
                JSR     GX_BORDER
                LDA     #$02           ; layer 0
                JMP     GX_SHOW

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

; DSPUT (7d3): after BLOAD's BLDRUN, a run DSRUN put on the screen page:
; its bytes, staged in bank 8's RAM, to the card (DSVC, DSPH and DSVL as
; DSSRUN left them: nothing between calls DSBYTE).
DSPUT:          LDA     DSSCF
                BEQ     DSRET
                STZ     DSSCF
                LDA     #DM_VWRITE
                BRA     DSVX

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

; --- DSRUN --------------------------------------------------------------------------------

; DSRUN: DX the segment, BX the offset, CX the length (0: none). The run
; checked whole (above): Illegal function call, nothing moved, if refused;
; else DX and BL its 20-bit start (DSA).
DSRUN:          STZ     DSSCF          ; 7d3: no run staged for DSPUT yet
                LDA     RDL
                STA     GXQ
                LDA     RDH
                STA     GXQ+1
                LDA     RBL
                STA     GXP
                LDA     RBH
                STA     GXP+1
                JSR     DSADR
                LDA     RCL            ; no bytes: no run
                ORA     RCH
                BNE     :+
                JMP     @ok
:               LDA     RCL            ; DSE: its last byte, DSA + CX - 1
                BNE     :+
                DEC     RCH
:               DEC     RCL
                CLC
                LDA     DSA
                ADC     RCL
                STA     DSE
                LDA     DSA+1
                ADC     RCH
                STA     DSE+1
                LDA     DSA+2
                ADC     #0
                STA     DSE+2
                INC     RCL            ; CX back
                BNE     :+
                INC     RCH
:               LDA     DSE+2
                CMP     #$10
                BCS     @fc            ; past $FFFFF: into bank F
                CMP     #8             ; meets bank 8: from it or below, to it or past
                BCC     @blocks
                BNE     @past
                LDA     DSA+2          ; to it: from it, the screen page (7d3); else from below
                CMP     #8
                BCC     @fc
                JSR     DSSRUN
                BRA     @ok
@past:          LDA     DSA+2
                CMP     #9
                BCC     @fc 
@blocks:        LDX     #DSE - DSA     ; the last block
                JSR     BLKOF
                STA     DSLAST
                LDX     #0             ; from the first
                JSR     BLKOF
@probe:         STA     DSBLK
                STZ     DSCB+1         ; its first byte: the block x $4000
                STZ     DSCB+2
                LSR     A
                ROR     DSCB+2
                LSR     A
                ROR     DSCB+2
                STA     DSCB+3
                LDX     #2
@s:             LDA     SCRP,X         ; to the scratch byte
                STA     DSCB+4,X
                DEX
                BPL     @s
                JSR     DSCALL
                CMP     #$FF
                BEQ     @fc 
                LDA     DSBLK
                CMP     DSLAST
                INC     A
                BCC     @probe         ; short of the last
@ok:            LDA     DSA            ; DX, BL: the start
                STA     RDL
                LDA     DSA+1
                STA     RDH
                LDA     DSA+2
                STA     RBL
                RTS
@fc:            JMP     FCERR

; DSSRUN (7d3): a run inside bank 8, in screen mode, with bank 8 the
; program's RAM (GX_BANK8): on the active page (P6's SCPAGP, the cache
; forgotten) within its reach, else Illegal function call. Staged in bank
; 8's RAM at its own address ($80000 + its offset): the page's bytes copied
; there by DM_VREAD, and DSSCF set for DSPUT.
DSSRUN:         BIT     SCRMOD
                BPL     @fc
                BIT     DSB8
                BMI     @fc
                SEC
                FJSR    SCPAGP
                JSR     DSPAGE
                BCS     @fc
                LDX     #2
@s:             LDA     DSA,X
                STA     DSPH,X
                DEX
                BPL     @s
                LDA     RCL
                STA     DSVL
                LDA     RCH
                STA     DSVL+1
                LDA     #DM_VREAD
                JSR     DSVX
                DEC     DSSCF
                RTS
@fc:            JMP     FCERR

.segment "BRAM"
.assert DSE = DSA + 3, error, "BLKOF takes DSE as DSA + 3"
DSA:            .res    3              ; the 20-bit address (DSADR)
DSE:            .res    3              ; DSRUN: the run's last byte (just past DSA: BLKOF)
DSBLK:          .res    1              ; a block (BLKOF; DSRUN's probe)
DSLAST:         .res    1              ; DSRUN: the run's last block
DSVAL:          .res    1              ; a POKE's byte
DSDIR:          .res    1              ; bit 7: a POKE
DSCB:           .res    9              ; function 83's DM_COPY block
DSPB:           .res    4              ; function 73's
SCRB:           .res    1              ; the scratch byte
DSVC:           .res    3              ; 7d3: the screen page's card address (DSPAGE)
DSPH:           .res    3              ;   the physical side (the scratch byte, or bank 8's RAM)
DSVL:           .res    2              ;   the bytes
DSB8:           .res    1              ; 7d3: bit 7, GX_BANK8 refused (bank 8 not RAM)
DSSCF:          .res    1              ; 7d3: DSRUN's run staged for the screen page, for DSPUT
.segment "RODATA"
TXPAGES         = 96                   ; card pages: 8 x 12, or 4 x 20
FTOPEN:         .byte   FT_OPEN, <EVAREA, >EVAREA, 0 ; function 80: the area, 256 bytes
SCBPB:          .byte   $24, 0, 0, 0   ; function 49: get SCB $24, CON:'s output vector
SCRP:           .res    3              ; and its physical address (GXSTRT)
