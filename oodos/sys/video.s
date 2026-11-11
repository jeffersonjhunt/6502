; OODOS's DMA and loading (hw/oolib.md, "The plan", L4): function 83, the
; vertical-blank copies on DMA channel 1, the pointer sprite, and the warm
; boot's video part.
;
; Where the code lives:
;
;   block 61 (BIOS)   all of it but DM_LOAD's file work: function 83's entry
;                     (FJUMPS's, reached with W2 on this block, as the stub
;                     leaves it) and every operation's parameters, read by
;                     PGET and PPUT, which run in W1 and put W2 back on this
;                     block before they return; the copies and fills by the
;                     BIOS's DMAGO; VBWORK, from the frame handler through
;                     CALL61; VBQUIET, from the warm boot
;   block 60 (BDOS)   DM_LOAD's file work (bdosfile.s's LOADGO), which calls
;                     the BDOS's positioning and buffers: they move W2, so
;                     they can't be called from here
;
; Video card revision 2 (hw/video-rev2.md; hw/oolib.md, "Video card
; revision 2"; vmem.inc): the card draws from its own memory, card addresses
; of 24 bits. OODOS owns $000000-$03FFFF (the console's palette, font and
; text, its units 0-12, in bank 0, which aperture slot 0 always shows: block
; 32, $80000-$83FFF); the program owns $040000-$1BFFFF, through OOLIB; Apple
; II mode $1C0000-$1FFFFF. The warm boot (and so the cold boot, which ends in
; it) puts slot 1 on the program's first card bank (VM_PROG_BANK) and slots
; 2 and 3 on banks 2 and 3, OODOS's; DM_VCOPY borrows those two inside the
; call. No physical destination of function 83 may touch slot 0's window,
; nor slots 2 and 3's, $88000-$8FFFF (bdosmem.s's OSWIN), nor may a record
; read through a DMA segment (bdos.s's DOUT), and blocks 32, 34 and 35 aren't
; in DM_INFO's W1 map: only slot 1's window, block 33, is the program's. No
; card destination may leave the program's part (CDSTOK). So the warm boot
; only sets the card's registers (crt.s's CRTREGS) and the slots.
;
; The card's data port (PADDR, PDATA, PSTEP, PCTL) and DMA channel 1, on
; which the card asks, are OODOS's: the vertical-blank copies and the
; pointer from the frame handler, and function 83's DM_VWRITE, DM_VREAD and
; DM_VFILL in the BDOS's time, each piece masked (PORTGO), so the interrupt
; never meets the port half set. DM_VCOPY borrows aperture slots 2 and 3 for
; each piece, masked, and puts them back: card to card by channel 3.
;
; The vertical-blank copies (question 3): four slots, each a source in
; system memory, a destination in the program's card memory and a length,
; written through the port, 1,024 bytes in all: 1,024 cycles with the CPU
; held, since the port asks for every byte at once (PORTGO), inside the
; blank's 10,240 even when the interrupt is held off by a 4 KB DMA piece
; (test_oodos_video's latest end, all four slots with the pointer and a
; stream, is line 504 of 480-524 under a USB batch and 502 under 64 KB
; loads). A slot is 1,024 bytes at most alone and the four's sum too,
; checked so that no sum wraps. A slot is copied at the first vertical blank
; after the program sets its bit in the event area's EV_VBGO (with TSB, which
; an interrupt can't split), and the bit cleared then, so a program sets a
; bit once a frame at most and sees it clear when the copy is on the card.
; Channel 1, which the BDOS uses only masked, so the interrupt never meets a
; BDOS copy half-programmed on channel 1 or 3. Nothing is copied with no event area
; open (the frame handler calls VBWORK after EVALL), and only while VBON.
;
; The pointer (L2's deferral): a sprite whose X and Y (bytes 2-5 of its
; entry in the table at SPRBASE) are set at each vertical blank, after the
; slots, from the mouse's position as the layout-2 area has it (input.s's
; MXS, MYS) less the hotspot, modulo 1,024. Four bytes by channel 1 from a
; staging copy here (PSTAGE), through the port, so the interrupt needs no
; zero page of its own; the program draws the image and sets the entry's
; other bytes. Only while the sprite's entry, all four bytes, is in the
; program's part of card memory (the warm boot leaves SPRBASE 0, the
; console's palette).

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.include "vmem.inc"
.importzp PX, PP, T0, T1, T2, T3, T4, T5, SG ; the BDOS's (bdos.inc)
.import RETFF, PGET, PPUT, SEGOK, OSWIN, AMAP
.importzp SRCP, DSTP, DCOUNT           ; bios.s's
.import DMAGO
.importzp EVP                          ; irq.s's
.import EVON, EVBLK, CLOCKS
.import MXS, MYS                       ; input.s's
.import CRTREGS                        ; crt.s's
.import LOADGO                         ; bdosfile.s's: DM_LOAD's file work
.export F_DMAOP, VBWORK, VBQUIET, VBON
.export LFCB, LREC, LCNT, LDST, LDONE

SLOT_MAX        = DM_BUDGET            ; a slot's bytes at most, and all four's

.segment "SYSBSS"
VBON:           .res    1              ; not 0: a slot set or the pointer on (the frame handler's test)
SLSRC:          .res    3 * DM_SLOTS   ; each slot's source (physical), destination (card) and
SLDST:          .res    3 * DM_SLOTS   ;   length (2 bytes; 0: the slot free)
SLLEN:          .res    2 * DM_SLOTS
VGO:            .res    1              ; VBWORK's: the go bits left to do
PTRON:          .res    1              ; 1: the pointer on
PTRSPR:         .res    1              ; its sprite, 0-63
PHOTX:          .res    1              ; its hotspot
PHOTY:          .res    1
PSTAGE:         .res    4              ; X and Y as they go to the sprite's entry
FILLB:          .res    1              ; DM_FILL's byte, a DMA's source
VMAP:           .res    8              ; DM_INFO's W1 map, as it is made
VLEN:           .res    2              ; a destination's length less 1, for its checks
VSRC:           .res    3              ; DM_VBSET's source and destination
VDST:           .res    3
VLEFT:          .res    2              ; the port's and DM_VCOPY's bytes left (0: 65,536)
PMODE:          .res    1              ; PORTRUN's channel mode and the port's PCTL
PPCTL:          .res    1
LFCB:           .res    2              ; DM_LOAD's: the FCB, the next record, the records left,
LREC:           .res    3              ;   the next destination, and the records loaded
LCNT:           .res    2
LDST:           .res    3
LDONE:          .res    2

.segment "BIOS"

; Function 83: X = 0, A the operation; else A/X a block, +0 the operation.
F_DMAOP:        LDX     PX
                BEQ     @op
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #DM_OPS
                BCS     DMFF
                ASL     A
                TAX
                JMP     (DMJUMPS,X)
DMJUMPS:        .word   DM_INFOX, DM_LOADX, DM_COPYX, DM_FILLX, DM_VBSETX, DM_POINTERX, DM_CONSOLEX
                .word   DM_VWRITEX, DM_VREADX, DM_VFILLX, DM_VCOPYX
.assert (* - DMJUMPS) / 2 = DM_OPS, error, "DMJUMPS has an entry for each of oodos.inc's DM_*"

DMFF:           JMP     RETFF
DM00:           LDA     #0
                TAX
                RTS

; A block, or the function ends: A = X = $FF.
.macro BLOCK
                LDX     PX
                BNE     :+
                JMP     RETFF
:
.endmacro

; SG = the block's next 3 bytes, a physical address.
GETSG:          JSR     PGET
                STA     SG
                JSR     PGET
                STA     SG + 1
                JSR     PGET
                STA     SG + 2
                RTS

; Carry set if SG's VLEN + 1 bytes aren't all memory a segment may reach
; (SEGOK: not past $FFFFF, none of OODOS's own blocks) or, for a
; destination (DSTOK), if they touch an aperture window that shows OODOS's
; card memory (OSWIN: slots 0, 2 and 3's).
SRCOK:          LDA     VLEN
                LDY     VLEN + 1
                JMP     SEGOK
DSTOK:          JSR     SRCOK
                BCS     @no
                LDA     VLEN
                LDY     VLEN + 1
                JMP     OSWIN
@no:            RTS

; Carry set unless the card address SG to SG + VLEN is all in the fitted
; card memory (CSRCOK) and, for a destination (CDSTOK), in the program's
; part, $040000-$1BFFFF; with no revision 2 card (CLOCKS's frame bit, which
; irq.s sets only for one) or no DMA controller (every card operation's
; bytes go by DMA: review F4), never.
CSRCOK:         LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @no
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @no
                CLC                    ; the end's top byte, from SG + VLEN
                LDA     SG
                ADC     VLEN
                LDA     SG + 1
                ADC     VLEN + 1
                LDA     SG + 2
                ADC     #0
                BCS     @no            ; past $FFFFFF
                CMP     VIDEO_MEMSIZE  ; (carry set: past the fitted memory)
                RTS
@no:            SEC
                RTS
CDSTOK:         JSR     CSRCOK
                BCS     @no
                CMP     #^VM_PROG_END  ; (A: the end's top byte)
                BCS     @no
                LDA     SG + 2
                CMP     #^VM_PROG
                BCC     @below
                CLC
                RTS
@below:         SEC
@no:            RTS
.assert (VM_PROG & $FFFF) = 0 && (VM_PROG_END & $FFFF) = 0, error, "CDSTOK compares the top bytes alone"

; VLEN = the 16-bit length at A/X less 1 (0: 65,536, VLEN $FFFF).
LENSET:         SEC
                SBC     #1
                STA     VLEN
                TXA
                SBC     #0
                STA     VLEN + 1
                RTS

; A = the card's memory in 64 KB units (0 with no revision 2 card), X =
; the slots; a block gets +1 those, +3 the slots' budget in pages, +4 the
; pointer's sprite ($FF off), +5-12 the W1 map: block 1, the blocks the
; program has allocated (53-58's AMAP, blocks 4-23), aperture slot 1's
; window (33, the program's card bank OOLIB sets) and banks 9-B (36-47), but
; not slots 0, 2 and 3's (32, 34, 35: OODOS's card banks 0, 2 and 3);
; +13-16 the program's card pages, its first and the one past its last (the
; fitted memory's end if that is sooner; 0, 0 with no card).
DM_INFOX:       LDX     PX
                BNE     @block
                JMP     @ret
@block:         JSR     MEMSZ
                JSR     PPUT
                LDA     #DM_SLOTS
                JSR     PPUT
                LDA     #>DM_BUDGET
                JSR     PPUT
                LDA     #$FF
                LDX     PTRON
                BEQ     @ptr
                LDA     PTRSPR
@ptr:           JSR     PPUT
                LDX     #7             ; the map: the fixed blocks, then AMAP's
@fix:           LDA     W1FIX,X
                STA     VMAP,X
                DEX
                BPL     @fix
                LDY     #AMAP_LEN - 1
@amap:          LDA     AMAP,Y
                BEQ     @next
                TYA
                CLC
                ADC     #4             ; AMAP's 0 is block 4
                PHA
                AND     #7
                TAX
                LDA     BITS,X
                STA     T0
                PLA
                LSR     A
                LSR     A
                LSR     A
                TAX
                LDA     T0
                ORA     VMAP,X
                STA     VMAP,X
@next:          DEY
                BPL     @amap
                LDX     #0
@out:           LDA     VMAP,X
                JSR     PPUT
                INX
                CPX     #8
                BNE     @out
                JSR     MEMSZ          ; the program's pages
                BEQ     @none
                LDA     #<DM_PAGE0
                JSR     PPUT
                LDA     #>DM_PAGE0
                JSR     PPUT
                LDA     #0
                JSR     PPUT
                JSR     MEMSZ          ; its end: $1C00, or MEMSIZE x 256 if sooner
                CMP     #>DM_PAGEEND
                BCC     @end
                LDA     #>DM_PAGEEND
@end:           JSR     PPUT
                BRA     @ret
@none:          LDX     #4
@zero:          JSR     PPUT           ; (A = 0; PPUT keeps X)
                DEX
                BNE     @zero
@ret:           JSR     MEMSZ
                LDX     #DM_SLOTS
                RTS

; A = the card's memory in 64 KB units, 0 with no revision 2 card.
MEMSZ:          LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @no
                LDA     VIDEO_MEMSIZE
@no:            RTS
.assert <DM_PAGE0 = 0 && <DM_PAGEEND = 0, error, "DM_INFO writes the pages' low bytes as 0"
W1FIX:          .byte   $02, 0, 0, 0, $F2, $FF, 0, 0 ; block 1; blocks 33 and 36-47
BITS:           .byte   $01, $02, $04, $08, $10, $20, $40, $80
.assert AMAP_LEN = 20, error, "DM_INFO's map takes AMAP as blocks 4-23"

; +1-2 the FCB, +3-5 the first record, +6-7 the count (1-512), +8-10 the
; destination, checked here (the count, the destination's 128 x count
; bytes); the file's part is block 60's (LOADGO).
DM_LOADX:       BLOCK
                JSR     PGET
                STA     LFCB
                JSR     PGET
                STA     LFCB + 1
                LDX     #0
@rec:           JSR     PGET
                STA     LREC,X
                INX
                CPX     #3
                BNE     @rec
                JSR     PGET
                STA     LCNT
                JSR     PGET
                STA     LCNT + 1
                JSR     GETSG
                LDA     SG
                STA     LDST
                LDA     SG + 1
                STA     LDST + 1
                LDA     SG + 2
                STA     LDST + 2
                LDA     LCNT           ; 1-512
                ORA     LCNT + 1
                BEQ     @no
                LDA     LCNT
                SEC
                SBC     #1
                TAX                    ; X/A = count - 1, 0-511
                LDA     LCNT + 1
                SBC     #0
                CMP     #2
                BCS     @no
                LSR     A              ; 128 x (count - 1) + 127
                TXA
                ROR     A
                STA     VLEN + 1
                LDA     #0
                ROR     A
                ORA     #$7F
                STA     VLEN
                JSR     DSTOK
                BCS     @no
                JMP     LOADGO
@no:            JMP     RETFF

; +1-3 the source, +4-6 the destination, +7-8 the length (0: 65,536): by
; DMAGO, 2 KB a block, interrupts taken between.
DM_COPYX:       BLOCK
                JSR     GETSG
                LDX     #2
@src:           LDA     SG,X
                STA     SRCP,X
                DEX
                BPL     @src
                JSR     GETSG
                LDX     #2
@dst:           LDA     SG,X
                STA     DSTP,X
                DEX
                BPL     @dst
                JSR     PGET
                STA     DCOUNT
                JSR     PGET
                STA     DCOUNT + 1
                TAX
                LDA     DCOUNT
                JSR     LENSET
                JSR     DSTOK          ; SG: the destination
                BCS     @no
                LDX     #2
@sg:            LDA     SRCP,X
                STA     SG,X
                DEX
                BPL     @sg
                JSR     SRCOK
                BCS     @no
                JSR     DMAGO
                JMP     DM00
@no:            JMP     RETFF

; +1 the byte, +2-4 the destination, +5-6 the length (0: 65,536): the
; byte to the first by DMAGO from FILLB, then the rest by an overlapping
; copy forward of the first, which repeats it (the owner's choice, against
; the plan's HOLD: DMAGO stays as the BIOS has it; the same 2 cycles a
; byte).
FILLB_PHYS      = SYS_BLOCK * $4000 + FILLB
DM_FILLX:       BLOCK
                JSR     PGET
                STA     FILLB
                JSR     GETSG
                JSR     PGET
                STA     DCOUNT
                JSR     PGET
                STA     DCOUNT + 1
                TAX
                LDA     DCOUNT
                JSR     LENSET
                JSR     DSTOK
                BCS     @no
                LDX     #2
@dst:           LDA     SG,X
                STA     DSTP,X
                DEX
                BPL     @dst
                LDA     #<FILLB_PHYS
                STA     SRCP
                LDA     #>FILLB_PHYS
                STA     SRCP + 1
                LDA     #^FILLB_PHYS
                STA     SRCP + 2
                LDA     #1
                STA     DCOUNT
                STZ     DCOUNT + 1
                JSR     DMAGO          ; the first byte
                LDA     VLEN           ; the rest: length - 1 bytes, from the first to the second on
                ORA     VLEN + 1
                BEQ     @done
                LDA     VLEN
                STA     DCOUNT
                LDA     VLEN + 1
                STA     DCOUNT + 1
                LDX     #2
@src:           LDA     DSTP,X
                STA     SRCP,X
                DEX
                BPL     @src
                INC     DSTP
                BNE     @go
                INC     DSTP + 1
                BNE     @go
                INC     DSTP + 2
@go:            JSR     DMAGO
@done:          JMP     DM00
@no:            JMP     RETFF

; +1 the slot, +2-4 its source (physical), +5-7 its destination (card,
; the program's part), +8-9 its length (1-1,024, the four slots' 1,024 at
; most; 0 frees it). Refused with no revision 2 video card or no DMA
; controller.
DM_VBSETX:      BLOCK
                LDA     CLOCKS
                AND     #CLK_FRAME
                BNE     @vid
@no0:           JMP     RETFF
@vid:           LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @no0
                JSR     PGET
                CMP     #DM_SLOTS
                BCS     @no0
                STA     T0             ; the slot
                JSR     GETSG
                LDX     #2
@src:           LDA     SG,X
                STA     VSRC,X
                DEX
                BPL     @src
                JSR     GETSG
                LDX     #2
@dst:           LDA     SG,X
                STA     VDST,X
                DEX
                BPL     @dst
                JSR     PGET
                STA     T1             ; T1/T2 the length
                JSR     PGET
                STA     T2
                ORA     T1
                BEQ     @set           ; 0: the slot freed
                LDA     T1             ; this one 1,024 at most, alone (review F1: no sum wraps)
                CMP     #<(SLOT_MAX + 1)
                LDA     T2
                SBC     #>(SLOT_MAX + 1)
                BCS     @no0
                LDA     T0             ; the others' lengths and this one's, against the budget
                ASL     A
                STA     T3
                LDA     T1
                STA     T4
                LDA     T2
                STA     T5
                LDX     #2 * DM_SLOTS - 2
@sum:           CPX     T3
                BEQ     @skip
                CLC
                LDA     T4
                ADC     SLLEN,X
                STA     T4
                LDA     T5
                ADC     SLLEN + 1,X
                STA     T5
                BCS     @no0           ; (none can carry, each 1,024 at most: but never wrapped)
@skip:          DEX
                DEX
                BPL     @sum
                LDA     T4             ; T4/T5 <= 1,024
                CMP     #<(SLOT_MAX + 1)
                LDA     T5
                SBC     #>(SLOT_MAX + 1)
                BCS     @no
                LDA     T1
                LDX     T2
                JSR     LENSET
                JSR     CDSTOK         ; SG: the destination, in card memory
                BCS     @no
                LDX     #2
@sg:            LDA     VSRC,X
                STA     SG,X
                DEX
                BPL     @sg
                JSR     SRCOK
                BCS     @no
@set:           LDA     T0             ; X = 3 x the slot, Y = 2 x it
                ASL     A
                TAY
                ADC     T0             ; (carry clear: 2 x 3 at most)
                TAX
                PHP
                SEI
                LDA     T1
                STA     SLLEN,Y
                LDA     T2
                STA     SLLEN + 1,Y
                LDY     #3
@put:           LDA     VSRC
                STA     SLSRC,X
                LDA     VDST
                STA     SLDST,X
                INX
                LDA     VSRC + 1       ; (shifted along: the three bytes in turn)
                STA     VSRC
                LDA     VSRC + 2
                STA     VSRC + 1
                LDA     VDST + 1
                STA     VDST
                LDA     VDST + 2
                STA     VDST + 1
                DEY
                BNE     @put
                JSR     VBCALC
                PLP
                JMP     DM00
@no:            JMP     RETFF

; VBON afresh: a slot set, or the pointer on. Masked by the caller, or a
; single byte's store: the frame handler reads it whole.
VBCALC:         LDA     PTRON
                LDX     #2 * DM_SLOTS - 1
@len:           ORA     SLLEN,X
                DEX
                BPL     @len
                STA     VBON
                RTS

; +1 the sprite (0-63, $FF off), +2 and +3 its hotspot. On only with a
; layout-2 area open (MXS and MYS are kept only then) and a video card.
DM_POINTERX:    BLOCK
                JSR     PGET
                CMP     #$FF
                BEQ     @off
                CMP     #64
                BCS     @no
                STA     T0
                BIT     EVON
                BPL     @no
                LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @no
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @no
                JSR     PGET
                STA     T1
                JSR     PGET
                PHP
                SEI
                STA     PHOTY
                LDA     T1
                STA     PHOTX
                LDA     T0
                STA     PTRSPR
                LDA     #1
                STA     PTRON
                JSR     VBCALC
                PLP
                JMP     DM00
@off:           STZ     PTRON
                PHP
                SEI
                JSR     VBCALC
                PLP
                JMP     DM00
@no:            JMP     RETFF
.assert EVON_L2 = $80, error, "DM_POINTER tests layout 2 by EVON's bit 7"

DM_CONSOLEX:    JSR     CRTREGS
                JMP     DM00

; +1-3 the source (physical), +4-6 the destination (card), +7-8 the length.
DM_VWRITEX:     BLOCK
                JSR     GETSG          ; the source
                JSR     SG2SRC
                JSR     GETSG          ; the destination
                JSR     SG2DST
                JSR     GETLEN
                JSR     CDSTOK
                BCS     @no
                JSR     SRC2SG
                JSR     SRCOK
                BCS     @no
                LDA     #DMA_DEMAND_OUT
                LDX     #VIDEO_PCTL_DRQ
                JMP     PORTRUN
@no:            JMP     RETFF

; +1-3 the source (card), +4-6 the destination (physical), +7-8 the length.
DM_VREADX:      BLOCK
                JSR     GETSG
                JSR     SG2DST         ; (DSTP: the card's address, as PORTRUN takes it)
                JSR     GETSG
                JSR     SG2SRC         ; (SRCP: the physical one)
                JSR     GETLEN
                JSR     DSTOK          ; SG: the physical destination
                BCS     @no
                JSR     DST2SG
                JSR     CSRCOK
                BCS     @no
                LDA     #DMA_DEMAND_IN
                LDX     #VIDEO_PCTL_DRQ | VIDEO_PCTL_FROM
                JMP     PORTRUN
@no:            JMP     RETFF

; +1 the byte, +2-4 the destination (card), +5-6 the length: from FILLB,
; the channel's source held still.
DM_VFILLX:      BLOCK
                JSR     PGET
                STA     FILLB
                JSR     GETSG
                JSR     SG2DST
                JSR     GETLEN
                JSR     CDSTOK
                BCS     @no
                LDA     #<FILLB_PHYS
                STA     SRCP
                LDA     #>FILLB_PHYS
                STA     SRCP + 1
                LDA     #^FILLB_PHYS
                STA     SRCP + 2
                LDA     #DMA_DEMAND_OUT | DMA_HOLD
                LDX     #VIDEO_PCTL_DRQ
                JMP     PORTRUN
@no:            JMP     RETFF

; +1-3 the source (card, the fitted memory), +4-6 the destination (card,
; the program's part), +7-8 the length: a piece at a time, none crossing a
; bank of either, the source's bank in slot 2 and the destination's in slot
; 3, by DMAGO from one window to the other, masked, the slots put back
; after each.
DM_VCOPYX:      BLOCK
                JSR     GETSG
                JSR     SG2SRC         ; VSRC, VDST: the card addresses, as they go
                JSR     CSRC2V
                JSR     GETSG
                JSR     SG2DST
                JSR     GETLEN
                JSR     CDSTOK
                BCS     @no
                JSR     CDST2V
                LDX     #2
@sg:            LDA     VSRC,X
                STA     SG,X
                DEX
                BPL     @sg
                JSR     CSRCOK
                BCS     @no
@piece:         JSR     VPIECE         ; DCOUNT: this piece, SRCP and DSTP its windows' addresses
                PHP
                SEI
                LDX     #3
@save:          LDA     VIDEO_SLOT2,X
                PHA
                DEX
                BPL     @save
                JSR     VBANKS
                JSR     DMAGO
                LDX     #0
@back:          PLA
                STA     VIDEO_SLOT2,X
                INX
                CPX     #4
                BNE     @back
                PLP
                JSR     VNEXT          ; on by the piece: carry set when done
                BCC     @piece
                JMP     DM00
@no:            JMP     RETFF
.assert VIDEO_SLOT3 = VIDEO_SLOT2 + 2, error, "DM_VCOPY keeps slots 2 and 3 as four bytes"

; The pieces' helpers: SG to SRCP or DSTP, and back; the length to VLEN
; (less 1) and the bytes left (VLEFT, 0: 65,536).
SG2SRC:         LDX     #2
@c:             LDA     SG,X
                STA     SRCP,X
                DEX
                BPL     @c
                RTS
SG2DST:         LDX     #2
@c:             LDA     SG,X
                STA     DSTP,X
                DEX
                BPL     @c
                RTS
SRC2SG:         LDX     #2
@c:             LDA     SRCP,X
                STA     SG,X
                DEX
                BPL     @c
                RTS
DST2SG:         LDX     #2
@c:             LDA     DSTP,X
                STA     SG,X
                DEX
                BPL     @c
                RTS
CSRC2V:         LDX     #2
@c:             LDA     SRCP,X
                STA     VSRC,X
                DEX
                BPL     @c
                RTS
CDST2V:         LDX     #2
@c:             LDA     DSTP,X
                STA     VDST,X
                DEX
                BPL     @c
                RTS
GETLEN:         JSR     PGET
                STA     VLEFT
                JSR     PGET
                STA     VLEFT + 1
                TAX
                LDA     VLEFT
                JMP     LENSET

; SRCP (physical) and DSTP (card) on, VLEFT bytes (0: 65,536) through the
; port by channel 1, A its mode and X the port's PCTL: in pieces of
; PORT_PIECE at most, each masked, the port and the channel set for it, so
; that the frame handler's copies may come between. Ends the function: A =
; X = 0, or $FF if a piece timed out (PORTGO), the pieces after it not run.
PORT_PIECE      = 2048
PORTRUN:        STA     PMODE
                STX     PPCTL
@piece:         LDA     VLEFT + 1      ; this piece: PORT_PIECE, or what is left
                BEQ     @last          ; (0: 65,536 left, a whole piece)
                CMP     #>PORT_PIECE
                BCC     @last
                STZ     DCOUNT
                LDA     #>PORT_PIECE
                STA     DCOUNT + 1
                BRA     @go
@last:          LDA     VLEFT
                ORA     VLEFT + 1
                BEQ     @full
                LDA     VLEFT
                STA     DCOUNT
                LDA     VLEFT + 1
                STA     DCOUNT + 1
                BRA     @go
@full:          STZ     DCOUNT
                LDA     #>PORT_PIECE
                STA     DCOUNT + 1
@go:            PHP
                SEI
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDX     #2
@regs:          LDA     SRCP,X
                STA     DMA1_ADDR,X
                LDA     DSTP,X
                STA     VIDEO_PADDR,X
                DEX
                BPL     @regs
                LDA     DCOUNT
                STA     DMA1_COUNT
                LDA     DCOUNT + 1
                STA     DMA1_COUNT + 1
                LDA     #1
                STA     VIDEO_PSTEP
                STZ     VIDEO_PSTEP + 1
                LDA     PMODE
                LDX     PPCTL
                JSR     PORTGO
                PLA                    ; (PLA and STA keep PORTGO's carry)
                STA     DMA_CONTROL
                BCS     @late
                PLP
                CLC                    ; on by the piece: the card's address
                LDA     DSTP
                ADC     DCOUNT
                STA     DSTP
                LDA     DSTP + 1
                ADC     DCOUNT + 1
                STA     DSTP + 1
                BCC     @card
                INC     DSTP + 2
@card:          LDA     PMODE          ; and the physical one, unless held (a fill)
                AND     #DMA_HOLD
                BNE     @left
                CLC
                LDA     SRCP
                ADC     DCOUNT
                STA     SRCP
                LDA     SRCP + 1
                ADC     DCOUNT + 1
                STA     SRCP + 1
                BCC     @left
                INC     SRCP + 2
@left:          SEC
                LDA     VLEFT
                SBC     DCOUNT
                STA     VLEFT
                LDA     VLEFT + 1
                SBC     DCOUNT + 1
                STA     VLEFT + 1
                ORA     VLEFT
                BEQ     @end
                JMP     @piece
@end:           JMP     DM00
@late:          PLP                    ; a piece that didn't end: refused (review F4)
                JMP     RETFF
.assert <PORT_PIECE = 0, error, "PORTRUN's pieces are whole pages"

; Channel 1 to or from the card's port, masked by the caller, the master
; enable on, the channel's address and count and the port's PADDR and PSTEP
; set: A the channel's mode (DMA_DEMAND_OUT to the card, with DMA_HOLD a
; fill; DMA_DEMAND_IN from it), X the port's PCTL (DRQ, with FROM for
; DMA_DEMAND_IN). The card asks for every byte at once, so it runs with the
; CPU held, a cycle a byte; stopped if it hasn't ended after 65,536 polls,
; and carry set then (clear when it ended). The port's PCTL 0 after, the
; terminal count cleared.
PORTGO:         STA     DMA1_MODE
                STX     VIDEO_PCTL
                LDA     #DMA_ENABLE
                STA     DMA1_CTRL
                LDX     #0
                LDY     #0
@wait:          LDA     DMA1_CTRL
                LSR     A
                BCC     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH1
                STA     DMA_ABORT
                SEC                    ; timed out
@done:          STZ     VIDEO_PCTL     ; (carry kept from here: clear when it ended, by LSR)
                LDA     #DMA_CH1
                STA     DMA_STATUS
                RTS

; DM_VCOPY's next piece from VSRC and VDST, VLEFT bytes left (0: 65,536):
; DCOUNT the least of DMA_PIECE, VLEFT, and the bytes to the end of each's
; bank; SRCP slot 2's window's address for VSRC, DSTP slot 3's for VDST.
VC_PIECE        = 2048                 ; DMAGO's block, so DMAGO runs it as one
VPIECE:         LDA     VLEFT + 1
                BEQ     @tiny
                CMP     #>VC_PIECE
                BCC     @tiny
                STZ     DCOUNT
                LDA     #>VC_PIECE
                STA     DCOUNT + 1
                BRA     @banks
@tiny:          LDA     VLEFT
                ORA     VLEFT + 1
                BEQ     @full
                LDA     VLEFT
                STA     DCOUNT
                LDA     VLEFT + 1
                STA     DCOUNT + 1
                BRA     @banks
@full:          STZ     DCOUNT
                LDA     #>VC_PIECE
                STA     DCOUNT + 1
@banks:         LDX     #VSRC - VSRC   ; each side: at most $4000 less its offset in the bank
                JSR     @limit
                LDX     #VDST - VSRC
                JSR     @limit
                LDA     VSRC           ; the windows: $88000 and $8C000 + the offsets
                STA     SRCP
                LDA     VSRC + 1
                AND     #$3F
                ORA     #>(VM_APERTURE + 2 * VM_BANK)
                STA     SRCP + 1
                LDA     #^(VM_APERTURE + 2 * VM_BANK)
                STA     SRCP + 2
                LDA     VDST
                STA     DSTP
                LDA     VDST + 1
                AND     #$3F
                ORA     #>(VM_APERTURE + 3 * VM_BANK)
                STA     DSTP + 1
                LDA     #^(VM_APERTURE + 3 * VM_BANK)
                STA     DSTP + 2
                RTS
@limit:         SEC                    ; T0/T1 = $4000 - (the address AND $3FFF)
                LDA     #0
                SBC     VSRC,X
                STA     T0
                LDA     VSRC + 1,X
                AND     #$3F
                STA     T1
                LDA     #>VM_BANK
                SBC     T1
                STA     T1
                LDA     T0             ; DCOUNT the less of the two
                CMP     DCOUNT
                LDA     T1
                SBC     DCOUNT + 1
                BCS     @keep
                LDA     T0
                STA     DCOUNT
                LDA     T1
                STA     DCOUNT + 1
@keep:          RTS
.assert VDST = VSRC + 3, error, "VPIECE reaches VDST from VSRC"
.assert ((VM_APERTURE + 2 * VM_BANK) & $3FFF) = 0 && ((VM_APERTURE + 3 * VM_BANK) & $3FFF) = 0, error, "the windows on banks"

; Slots 2 and 3 on VSRC's and VDST's banks (the address / $4000: its top
; two bytes shifted), masked by the caller.
VBANKS:         LDX     #VSRC - VSRC
                LDY     #0
                JSR     @bank
                LDX     #VDST - VSRC
                LDY     #2
@bank:          LDA     VSRC + 1,X     ; the bank: (top byte x 4) + (middle byte / 64), 10 bits
                ASL     A
                LDA     VSRC + 2,X
                ROL     A
                STA     T0
                LDA     #0
                ROL     A
                STA     T1
                LDA     VSRC + 1,X
                ASL     A
                ASL     A
                LDA     T0
                ROL     A
                STA     VIDEO_SLOT2,Y
                LDA     T1
                ROL     A
                STA     VIDEO_SLOT2 + 1,Y
                RTS

; On by DCOUNT: VSRC, VDST and VLEFT; carry set when none is left.
VNEXT:          LDX     #VSRC - VSRC
                JSR     @add
                LDX     #VDST - VSRC
                JSR     @add
                SEC
                LDA     VLEFT
                SBC     DCOUNT
                STA     VLEFT
                LDA     VLEFT + 1
                SBC     DCOUNT + 1
                STA     VLEFT + 1
                ORA     VLEFT
                CLC
                BNE     @more
                SEC
@more:          RTS
@add:           CLC
                LDA     VSRC,X
                ADC     DCOUNT
                STA     VSRC,X
                LDA     VSRC + 1,X
                ADC     DCOUNT + 1
                STA     VSRC + 1,X
                BCC     @c
                INC     VSRC + 2,X
@c:             RTS

; The frame handler's (irq.s's VIDSRV, through CALL61, masked, after EVALL,
; while VBON): each slot whose EV_VBGO bit is set written through the
; card's port by channel 1 (PORTGO), its bit cleared; then the pointer's X
; and Y. W1 borrowed for the area, and DMA_CONTROL's master enable on for
; the copies and put back after, as whatever was interrupted had it. The
; port is OODOS's and the BDOS sets it only masked, so nothing is in the
; middle of it here.
VBWORK:         LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDY     #EV_VBGO
                LDA     (EVP),Y
                AND     #(1 << DM_SLOTS) - 1
                BEQ     @ptr
                STA     VGO
                LDA     #1             ; the port a byte at a time
                STA     VIDEO_PSTEP
                STZ     VIDEO_PSTEP + 1
                LDX     #0             ; X 3 x the slot, Y 2 x it
                LDY     #0
@slot:          LSR     VGO
                BCC     @next
                LDA     SLLEN,Y
                ORA     SLLEN + 1,Y
                BEQ     @next          ; a bit for a free slot: cleared, nothing copied
                LDA     SLLEN,Y
                STA     DMA1_COUNT
                LDA     SLLEN + 1,Y
                STA     DMA1_COUNT + 1
                LDA     SLSRC,X
                STA     DMA1_ADDR
                LDA     SLSRC + 1,X
                STA     DMA1_ADDR + 1
                LDA     SLSRC + 2,X
                STA     DMA1_ADDR + 2
                LDA     SLDST,X
                STA     VIDEO_PADDR
                LDA     SLDST + 1,X
                STA     VIDEO_PADDR + 1
                LDA     SLDST + 2,X
                STA     VIDEO_PADDR + 2
                PHX
                PHY
                JSR     PORTOUT
                PLY
                PLX
@next:          INX
                INX
                INX
                INY
                INY
                CPY     #2 * DM_SLOTS
                BNE     @slot
                LDY     #EV_VBGO       ; the bits done cleared (masked: the program set none since)
                LDA     (EVP),Y
                AND     #<~((1 << DM_SLOTS) - 1)
                STA     (EVP),Y
@ptr:           LDA     PTRON
                BEQ     @done
                BIT     EVON
                BPL     @done          ; MXS and MYS only with a layout-2 area
                LDA     PTRSPR         ; the entry's card address: SPRBASE x 256 + the sprite x 8 + 2,
                LSR     A              ;   its page SPRBASE + the sprite / 32 (its 4 bytes in that page)
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                CLC
                ADC     VIDEO_SPRBASE
                TAX                    ; X: the address's middle byte
                LDA     VIDEO_SPRBASE + 1
                ADC     #0             ; A: its top byte (review F3: the entry's, not SPRBASE's)
                BCS     @done          ; past page $FFFF
                CMP     #>DM_PAGE0
                BCC     @done          ; in OODOS's card memory: not written there
                CMP     #>DM_PAGEEND
                BCS     @done          ; nor in Apple II mode's, nor past it
                STA     VIDEO_PADDR + 2
                STX     VIDEO_PADDR + 1
                LDA     PTRSPR
                ASL     A
                ASL     A
                ASL     A
                ORA     #2
                STA     VIDEO_PADDR
                SEC                    ; X and Y less the hotspot, modulo 1,024
                LDA     MXS
                SBC     PHOTX
                STA     PSTAGE
                LDA     MXS + 1
                SBC     #0
                AND     #3
                STA     PSTAGE + 1
                SEC
                LDA     MYS
                SBC     PHOTY
                STA     PSTAGE + 2
                LDA     MYS + 1
                SBC     #0
                AND     #3
                STA     PSTAGE + 3
                LDA     #<PSTAGE_PHYS  ; from the staging copy
                STA     DMA1_ADDR
                LDA     #>PSTAGE_PHYS
                STA     DMA1_ADDR + 1
                LDA     #^PSTAGE_PHYS
                STA     DMA1_ADDR + 2
                LDA     #1
                STA     VIDEO_PSTEP
                STZ     VIDEO_PSTEP + 1
                LDA     #4
                STA     DMA1_COUNT
                STZ     DMA1_COUNT + 1
                JSR     PORTOUT
@done:          PLA
                STA     DMA_CONTROL
                PLA
                STA     NB_WIN1
                RTS
PSTAGE_PHYS     = SYS_BLOCK * $4000 + PSTAGE

; The frame handler's: channel 1 to the card's port, as programmed.
PORTOUT:        LDA     #DMA_DEMAND_OUT
                LDX     #VIDEO_PCTL_DRQ
                JMP     PORTGO

; The warm boot's (input.s's WBQUIET, after DEAFEN; the cold boot ends in
; the warm boot): the slots freed, the pointer off, channel 1 stopped, the
; port's DRQ off, the aperture's slots set (slot 0 on card bank 0, the
; console's; slot 1 on the program's first bank, VM_PROG_BANK, so that block
; 33 shows only the program's card memory; slots 2 and 3 on banks 2 and 3,
; OODOS's, whose windows no program reaches: review F2), and the console's
; screen shown again.
VBQUIET:        STZ     VBON
                STZ     PTRON
                LDX     #2 * DM_SLOTS - 1
@free:          STZ     SLLEN,X
                DEX
                BPL     @free
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @card
                LDA     #DMA_CH1
                STA     DMA_ABORT
@card:          LDA     CLOCKS         ; a revision 2 card: its port quiet, the slots set
                AND     #CLK_FRAME
                BEQ     @crt
                STZ     VIDEO_PCTL
                LDX     #7
@slot:          LDA     QSLOT,X
                STA     VIDEO_SLOT0,X
                DEX
                BPL     @slot
@crt:           JMP     CRTREGS
QSLOT:          .word   0, VM_PROG_BANK, 2, 3 ; SLOT0-SLOT3 as the warm boot leaves them
.assert VIDEO_SLOT3 = VIDEO_SLOT0 + 6, error, "VBQUIET sets the four slots as eight bytes"
