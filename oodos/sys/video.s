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
; The console's video memory, units 0-12 ($80000-$833FF: the palette, the
; font and the text screen), is OODOS's (L4's question 2): no destination of
; function 83 may touch it (bdosmem.s's UNITS0), nor a record read through
; a DMA segment (bdos.s's DOUT), and block 32 isn't in DM_INFO's W1 map. So
; the warm boot only sets the card's registers (crt.s's CRTREGS).
;
; The vertical-blank copies (question 3): four slots, each a source, a
; destination and a length, 1,024 bytes in all (2,048 cycles with the CPU
; held, inside the blank's 10,240 even when the interrupt is held off by a
; 4 KB DMA piece). A slot is copied at the first vertical blank after the
; program sets its bit in the event area's EV_VBGO (with TSB, which an
; interrupt can't split), and the bit cleared then, so a program sets a bit
; once a frame at most and sees it clear when the copy is on the card.
; Channel 1, which nothing else uses, so the interrupt never meets a BDOS
; copy half-programmed on channel 3. Nothing is copied with no event area
; open (the frame handler calls VBWORK after EVALL), and only while VBON.
;
; The pointer (L2's deferral): a sprite whose X and Y (bytes 2-5 of its
; entry in the table at SPRBASE) are set at each vertical blank, after the
; slots, from the mouse's position as the layout-2 area has it (input.s's
; MXS, MYS) less the hotspot, modulo 1,024. Four bytes by channel 1 from a
; staging copy here (PSTAGE), so the interrupt needs no zero page of its
; own; the program draws the image and sets the entry's other bytes. Not
; while SPRBASE points into the console's units 0-12 (the warm boot leaves
; it 0): the table there would be the console's palette.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.importzp PX, PP, T0, T1, T2, T3, T4, T5, SG ; the BDOS's (bdos.inc)
.import RETFF, PGET, PPUT, SEGOK, UNITS0, AMAP
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
SLSRC:          .res    3 * DM_SLOTS   ; each slot's source, destination (physical, 3 bytes) and
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
; destination (DSTOK), if they touch the console's units 0-12.
SRCOK:          LDA     VLEN
                LDY     VLEN + 1
                JMP     SEGOK
DSTOK:          JSR     SRCOK
                BCS     @no
                LDA     VLEN
                LDY     VLEN + 1
                JMP     UNITS0
@no:            RTS

; VLEN = the 16-bit length at A/X less 1 (0: 65,536, VLEN $FFFF).
LENSET:         SEC
                SBC     #1
                STA     VLEN
                TXA
                SBC     #0
                STA     VLEN + 1
                RTS

; A = the first unit a program may use, X = the slots; a block gets +1
; those, +3 the slots' budget in pages, +4 the pointer's sprite ($FF off),
; +5-12 the W1 map: block 1, the blocks the program has allocated (53-58's
; AMAP, blocks 4-23), and video memory but the console's block (33-47).
DM_INFOX:       LDX     PX
                BEQ     @ret
                LDA     #DM_UNIT0
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
@ret:           LDA     #DM_UNIT0
                LDX     #DM_SLOTS
                RTS
W1FIX:          .byte   $02, 0, 0, 0, $FE, $FF, 0, 0 ; block 1; blocks 33-47
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

; +1 the slot, +2-4 its source, +5-7 its destination, +8-9 its length
; (1-1,024, the four slots' 1,024 at most; 0 frees it). Refused with no
; video card or no DMA controller.
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
                JSR     DSTOK          ; SG: the destination
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

; The frame handler's (irq.s's VIDSRV, through CALL61, masked, after EVALL,
; while VBON): each slot whose EV_VBGO bit is set copied by channel 1, its
; bit cleared; then the pointer's X and Y. W1 borrowed for the area, and
; DMA_CONTROL's master enable on for the copies and put back after, as
; whatever was interrupted had it.
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
                STA     DMA1_DEST
                LDA     SLDST + 1,X
                STA     DMA1_DEST + 1
                LDA     SLDST + 2,X
                STA     DMA1_DEST + 2
                JSR     CH1GO
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
                LDA     VIDEO_SPRBASE
                CMP     #DM_UNIT0
                BCC     @done          ; a sprite table in the console's units: not written there
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
                LDA     PTRSPR         ; to $80000 + SPRBASE x 1,024 + the sprite x 8 + 2
                ASL     A
                ASL     A
                ASL     A
                ORA     #2
                STA     DMA1_DEST
                LDA     VIDEO_SPRBASE
                ASL     A
                ASL     A
                STA     T0WORK
                LDA     PTRSPR
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     T0WORK
                STA     DMA1_DEST + 1
                LDA     VIDEO_SPRBASE
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     #^VIDEO_RAM
                STA     DMA1_DEST + 2
                LDA     #4
                STA     DMA1_COUNT
                STZ     DMA1_COUNT + 1
                JSR     CH1GO
@done:          PLA
                STA     DMA_CONTROL
                PLA
                STA     NB_WIN1
                RTS
PSTAGE_PHYS     = SYS_BLOCK * $4000 + PSTAGE
VIDEO_RAM       = $80000
T0WORK          = VGO                  ; (its bits are all done by then)

; Channel 1, programmed, run memory to memory and waited for (the CPU is
; held while it runs: done at once with no burst limit), its terminal
; count cleared.
CH1GO:          LDA     #DMA_M2M
                STA     DMA1_MODE
                LDA     #DMA_GO
                STA     DMA1_CTRL
@wait:          LDA     DMA1_CTRL
                LSR     A
                BCS     @wait
                LDA     #DMA_CH1
                STA     DMA_STATUS
                RTS

; The warm boot's (input.s's WBQUIET, after DEAFEN): the slots freed, the
; pointer off, channel 1 stopped, and the console's screen shown again.
VBQUIET:        STZ     VBON
                STZ     PTRON
                LDX     #2 * DM_SLOTS - 1
@free:          STZ     SLLEN,X
                DEX
                BPL     @free
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @crt
                LDA     #DMA_CH1
                STA     DMA_ABORT
@crt:           JMP     CRTREGS
