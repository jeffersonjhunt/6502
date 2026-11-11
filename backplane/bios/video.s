; The BIOS's text screen on the video card, revision 2 (hw/video-rev2.md,
; proposed; hw/design.md, "Video").
;
; VINIT sets up an 80x30 text layer and VPUTC writes a character to it, so
; everything the Woz Monitor prints (ECHO calls VPUTC) shows on the VGA
; screen as well as the serial line.
;
; The card draws from its own 2 MB, at these card addresses (OODOS's region,
; hw/video-rev2.md's layout: the old 1 KB unit u is card address u x 1 KB):
;
;   $000000  the palette: 16 colours, the CGA's
;   $000400  the font, 4 KB, copied from flash block 30 (font/)
;   $002000  the text: 80 x 30 cells of character + attribute
;
; VPUTC writes cells through aperture slot 0, which it points at card page 0
; for the moment and puts back: physical $80000 (RAM block 32's address, the
; aperture's bank) through W2, borrowed the same way. Everything else goes
; through the card's data port: VINIT writes the font and the palette into
; PDATA, and clearing and scrolling are port transfers by DMA on channel 1,
; the card's DRQ, each waited for (so whole whatever BURST a program has set:
; the master enable on for it and put back after; one that never ends is
; given up after about 0.1 s). A fill holds the DMA's source on one byte with
; PSTEP 2: the characters, then the attributes. Scrolling copies card to card
; in 2 KB pieces, out of the port into SCRBUF and back in 160 bytes lower:
; 4,640 bytes each way, about 1.3 ms. SCRBUF is the first 2 KB of RAM block
; 63, which the I/O hole hides from the CPU and the BIOS's copy of itself
; leaves unused. The port's PADDR, PSTEP and PCTL are put back after, so a
; program that prints keeps its own port settings, but a transfer of its
; own on channel 1 is stopped. The cursor is an underline whose attribute
; blinks (bit 7, with CONFIG's blink on), so the card blinks it and the BIOS
; does nothing.
;
; With no revision 2 video card, or no DMA controller, VINIT leaves VOK clear
; and VPUTC does nothing. Break (NMI) puts W2 back and calls VSCREEN, which
; sets VOK again from the ID bytes, so one during VPUTC, or a program that
; moved the screen or wrote VOK, leaves the monitor on the text screen as
; VINIT made it. PUTAT keeps the cursor on the screen whatever a program has
; left in VCOL and VROW.

.setcpu "65C02"
.include "backplane.inc"
.export VINIT, VPUTC, VSCREEN, DEMO, DMA3RUN
.import WOZMON, DEMO_MAIN

VPTR    = BIOS_ZP + 0                  ; cell pointer (CPU address)
VCOL    = BIOS_ZP + 2
VROW    = BIOS_ZP + 3
VOK     = BIOS_ZP + 4                  ; bit 7: the screen is on
VSRC    = BIOS_ZP + 5                  ; copy pointer while VINIT runs

COLS    = 80
ROWS    = 30
ATTR    = $07                          ; light grey on black
CURSOR  = '_'
CURATTR = $87                          ; light grey, blinking

VBLOCK  = 32                           ; physical $80000, the aperture: slot 0 through W2
VWIN    = $8000
PAL_CARD  = $000000                    ; card addresses
FONT_CARD = $000400
TEXT_CARD = $002000
ROWBYTES  = COLS * 2
SCRBUF    = $FC000                     ; RAM block 63's first 2 KB, under the I/O hole
SCRBUF_SIZE = $0800
BIOS_PHYS = $FC000 - $C000             ; a BIOS byte's physical address, in its copy, is its CPU address + this

FONT_BLOCK = 30                        ; flash block 30, through W1
FONT_CPU   = $4000

.segment "VIDEO"

; Turn the screen on: font, palette, a clear screen, then the registers.
VINIT:
                JSR     PROBE
                BPL     @done
                LDA     #DMA_ON
                STA     DMA_CONTROL

                LDA     NB_WIN1
                PHA
                LDA     #NB_ROM | NB_RO | FONT_BLOCK
                STA     NB_WIN1
                STZ     VIDEO_PCTL     ; the port: no DMA, stepping up by 1
                LDA     #1
                STA     VIDEO_PSTEP
                STZ     VIDEO_PSTEP + 1

                LDA     #<FONT_CARD    ; the font: 16 pages, flash into the port
                STA     VIDEO_PADDR
                LDA     #>FONT_CARD
                STA     VIDEO_PADDR + 1
                LDA     #^FONT_CARD
                STA     VIDEO_PADDR + 2
                STZ     VSRC
                LDA     #>FONT_CPU
                STA     VSRC+1
                LDY     #$00
@font:          LDA     (VSRC),Y
                STA     VIDEO_PDATA
                INY
                BNE     @font
                INC     VSRC+1
                LDA     VSRC+1
                CMP     #>(FONT_CPU + $1000)
                BNE     @font

                LDA     #<PAL_CARD     ; the palette
                STA     VIDEO_PADDR
                LDA     #>PAL_CARD
                STA     VIDEO_PADDR + 1
                LDA     #^PAL_CARD
                STA     VIDEO_PADDR + 2
                LDX     #0
@pal:           LDA     CGA,X
                STA     VIDEO_PDATA
                INX
                CPX     #32
                BNE     @pal

                PLA
                STA     NB_WIN1

                LDX     #JOB_CLS       ; the screen: blanks, then their attribute
                LDY     #JOBS_CLS
                JSR     PORTJOBS

                STZ     VCOL
                STZ     VROW
                BRA     VSCREEN
@done:          RTS

; VOK from the ID bytes: $FF (N set) with a revision 2 video card and the
; DMA controller, else 0.
PROBE:
                STZ     VOK
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V2
                BNE     @done
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @done
                DEC     VOK
@done:          BIT     VOK
                RTS

; The card's registers for the text screen, if there is one: layer 0 only,
; unscrolled, no interrupts. Its memory, the aperture's slots and the port
; are left as they are.
VSCREEN:
                JSR     PROBE
                BPL     @done
                STZ     VIDEO_IEN
                STZ     VIDEO_PALBASE
                STZ     VIDEO_PALBASE + 1
                STZ     VIDEO_SPRBASE
                STZ     VIDEO_SPRBASE + 1
                STZ     VIDEO_BORDER
                LDA     #VIDEO_TEXT80B
                STA     VIDEO_L0_CONFIG
                LDA     #<(TEXT_CARD >> 8)
                STA     VIDEO_L0_MAP
                LDA     #>(TEXT_CARD >> 8)
                STA     VIDEO_L0_MAP + 1
                LDA     #<(FONT_CARD >> 8)
                STA     VIDEO_L0_TILE
                LDA     #>(FONT_CARD >> 8)
                STA     VIDEO_L0_TILE + 1
                STZ     VIDEO_L0_HSCROLL
                STZ     VIDEO_L0_HSCROLL + 1
                STZ     VIDEO_L0_VSCROLL
                STZ     VIDEO_L0_VSCROLL + 1
                STZ     VIDEO_L0_PALOFS
                STZ     VIDEO_L1_CONFIG
                LDA     #VIDEO_ON_L0
                STA     VIDEO_CTRL
@done:          RTS

; The video demo (demo.s), in flash block 29: map it at $4000 through W1
; and run it. It needs the screen (the card and the DMA controller). It ends
; at BREAK (bios.s), which puts the windows and the screen back.
DEMO_BLOCK = 29
DEMO:
                BIT     VOK
                BPL     @none
                LDA     #NB_ROM | NB_RO | DEMO_BLOCK
                STA     NB_WIN1
                JMP     DEMO_MAIN
@none:          JMP     WOZMON

; Write A to the screen at the cursor: CR starts a new line, backspace backs
; up one, other control characters do nothing. A, X and Y are kept, and so
; are W2 and aperture slot 0, which it borrows.
VPUTC:
                BIT     VOK
                BPL     @off
                PHA
                PHX
                PHY
                LDX     NB_WIN2
                PHX
                LDX     VIDEO_SLOT0
                PHX
                LDX     VIDEO_SLOT0 + 1
                PHX
                STZ     VIDEO_SLOT0    ; slot 0 on card page 0, where the text is
                STZ     VIDEO_SLOT0 + 1
                LDX     #VBLOCK
                STX     NB_WIN2
                CMP     #$0D
                BEQ     @cr
                CMP     #$08
                BEQ     @bs
                CMP     #' '
                BCC     @cursor
                LDX     #ATTR
                JSR     PUTAT
                INC     VCOL
                LDA     VCOL
                CMP     #COLS
                BCC     @cursor
                STZ     VCOL
                JSR     NEWLINE
                BRA     @cursor
@cr:            JSR     BLANK          ; take the cursor off
                STZ     VCOL
                JSR     NEWLINE
                BRA     @cursor
@bs:            JSR     BLANK
                LDA     VCOL
                BEQ     @cursor
                DEC     VCOL
@cursor:        LDA     #CURSOR
                LDX     #CURATTR
                JSR     PUTAT
                PLX
                STX     VIDEO_SLOT0 + 1
                PLX
                STX     VIDEO_SLOT0
                PLX
                STX     NB_WIN2
                PLY
                PLX
                PLA
@off:           RTS

BLANK:          LDA     #' '
                LDX     #ATTR
                ; fall through

; Character A, attribute X, at VCOL, VROW, each first kept on the screen
; (a program may have used them). Uses A, X, Y.
PUTAT:
                PHA
                LDY     VROW
                CPY     #ROWS
                BCC     @row
                LDY     #ROWS - 1
                STY     VROW
@row:           LDA     VCOL
                CMP     #COLS
                BCC     @col
                LDA     #0
                STA     VCOL
@col:           ASL     A              ; 2 bytes a cell; at most 158, so no carry
                ADC     ROWLO,Y
                STA     VPTR
                LDA     ROWHI,Y
                ADC     #$00
                STA     VPTR+1
                PLA
                STA     (VPTR)
                TXA
                LDY     #$01
                STA     (VPTR),Y
                RTS

; Down a line; at the bottom, scroll up and clear the new last line.
NEWLINE:
                LDA     VROW           ; VROW is never past the bottom, even for a moment
                INC     A
                CMP     #ROWS
                BCS     @scroll
                STA     VROW
                RTS
@scroll:        LDX     #JOB_SCROLL    ; the scroll, then the last row cleared
                LDY     #JOBS_SCROLL + JOBS_CLEARROW
                ; fall through

; Run Y port jobs from job X (an offset into PJOBS), in turn, with the
; port's PADDR, PSTEP and PCTL put back after; after one that is given up,
; none of the rest. Uses A, X, Y.
PORTJOBS:
                LDA     VIDEO_PCTL
                PHA
                STZ     VIDEO_PCTL     ; no DRQ while a job is set up
                LDA     VIDEO_PSTEP
                PHA
                LDA     VIDEO_PSTEP + 1
                PHA
                LDA     VIDEO_PADDR
                PHA
                LDA     VIDEO_PADDR + 1
                PHA
                LDA     VIDEO_PADDR + 2
                PHA
@job:           PHY
                JSR     PORTJOB
                PLY
                BCS     @end           ; one given up: the rest would be too
                DEY
                BNE     @job
@end:
                PLA
                STA     VIDEO_PADDR + 2
                PLA
                STA     VIDEO_PADDR + 1
                PLA
                STA     VIDEO_PADDR
                PLA
                STA     VIDEO_PSTEP + 1
                PLA
                STA     VIDEO_PSTEP
                PLA
                STA     VIDEO_PCTL
                RTS

; Port job X: PADDR and PSTEP, then channel 1 programmed stopped (so a live
; channel never runs on a half-written address) and armed, a block, which
; the card's DRQ starts when PCTL is written; waited for as DMA3RUN waits,
; with CONTROL's master enable on and CONTROL put back after, and PCTL 0
; again. X moves on to the next job; carry set if it was given up. Uses A, Y.
PORTJOB:
                LDA     #DMA_CH1
                STA     DMA_ABORT
                LDY     #$00
@regs:          LDA     PJOBS,X
                STA     VIDEO_PADDR,Y  ; PADDR: Y = 0-2
                LDA     PJOBS+5,X
                STA     DMA1_ADDR,Y    ; the memory side
                INX
                INY
                CPY     #$03
                BNE     @regs
                LDA     PJOBS,X        ; PSTEP (X has moved on by 3)
                STA     VIDEO_PSTEP
                LDA     PJOBS+1,X
                STA     VIDEO_PSTEP + 1
                LDA     PJOBS+5,X      ; COUNT
                STA     DMA1_COUNT
                LDA     PJOBS+6,X
                STA     DMA1_COUNT + 1
                LDA     PJOBS+7,X
                STA     DMA1_MODE
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_ENABLE
                STA     DMA1_CTRL      ; armed: the card's DRQ starts it
                LDA     PJOBS+8,X
                STA     VIDEO_PCTL
                TXA
                CLC
                ADC     #PJOB_SIZE - 3
                PHA                    ; the next job
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
                SEC                    ; given up
                BRA     @out
@done:          CLC
@out:           STZ     VIDEO_PCTL
                PLX
                PLA
                STA     DMA_CONTROL
                RTS

; A port job: PADDR, PSTEP, then channel 1's ADDR, COUNT and MODE, and PCTL.
.macro PJOB paddr, pstep, addr, count, mode, pctl
                .faraddr paddr
                .word   pstep
                .faraddr addr
                .word   count
                .byte   mode
                .byte   pctl
.endmacro
PJOB_SIZE = 12

TO_CARD   = VIDEO_PCTL_DRQ
FROM_CARD = VIDEO_PCTL_DRQ | VIDEO_PCTL_FROM
FILL      = DMA_BLOCK_OUT | DMA_HOLD
BLANKS    = TEXT_CARD + ROWBYTES * (ROWS - 1) ; the last row
SCROLLED  = ROWBYTES * (ROWS - 1)      ; rows 1-29 up to 0-28: 4,640 bytes

PJOBS:
JOB_CLS = * - PJOBS                    ; every cell: ' ', then ATTR
                PJOB    TEXT_CARD, 2, BLANKCELL + BIOS_PHYS, COLS * ROWS, FILL, TO_CARD
                PJOB    TEXT_CARD + 1, 2, BLANKCELL + 1 + BIOS_PHYS, COLS * ROWS, FILL, TO_CARD
JOBS_CLS = 2
JOB_SCROLL = * - PJOBS                 ; out to SCRBUF and back in a row higher, 2 KB at a time
.repeat (SCROLLED + SCRBUF_SIZE - 1) / SCRBUF_SIZE, i
piece .set .min(SCROLLED - i * SCRBUF_SIZE, SCRBUF_SIZE)
                PJOB    TEXT_CARD + ROWBYTES + i * SCRBUF_SIZE, 1, SCRBUF, piece, DMA_BLOCK_IN, FROM_CARD
                PJOB    TEXT_CARD + i * SCRBUF_SIZE, 1, SCRBUF, piece, DMA_BLOCK_OUT, TO_CARD
.endrepeat
JOBS_SCROLL = (* - PJOBS - JOB_SCROLL) / PJOB_SIZE
                PJOB    BLANKS, 2, BLANKCELL + BIOS_PHYS, COLS, FILL, TO_CARD
                PJOB    BLANKS + 1, 2, BLANKCELL + 1 + BIOS_PHYS, COLS, FILL, TO_CARD
JOBS_CLEARROW = 2
.assert * - PJOBS < 256, error, "PJOBS: X indexes it"

BLANKCELL:      .byte   ' ', ATTR      ; the fills' sources, read by DMA from the BIOS's copy

ROWLO:
.repeat ROWS, r
                .byte   <(VWIN + TEXT_CARD + r * ROWBYTES)
.endrepeat
ROWHI:
.repeat ROWS, r
                .byte   >(VWIN + TEXT_CARD + r * ROWBYTES)
.endrepeat

; Start channel 3, programmed, and wait for ENABLE to clear at terminal
; count, with the master enable on (a program may have turned it off) and
; CONTROL put back after. The enable is the whole controller's, so a
; program's channels still armed run meanwhile: to keep one from running, a
; program aborts it, not just turns the master enable off. 65,536 polls, about 0.1 s, then it gives up and
; stops the channel: no controller there (ENABLE reads 1 off a floating
; bus), or one starved by a busier channel. For the demo too. Uses A, X, Y.
DMA3RUN:
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_GO
                STA     DMA3_CTRL
                LDX     #0
                LDY     #0
@wait:          LDA     DMA3_CTRL
                LSR     A
                BCC     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH3
                STA     DMA_ABORT
@done:          PLA
                STA     DMA_CONTROL
                RTS

; The CGA's 16 colours, 12-bit: GGGGBBBB, then 0000RRRR.
.macro RGB r, g, b
                .byte   (g << 4) | b, r
.endmacro
CGA:            RGB     $0, $0, $0     ; black
                RGB     $0, $0, $A     ; blue
                RGB     $0, $A, $0     ; green
                RGB     $0, $A, $A     ; cyan
                RGB     $A, $0, $0     ; red
                RGB     $A, $0, $A     ; magenta
                RGB     $A, $5, $0     ; brown
                RGB     $A, $A, $A     ; light grey
                RGB     $5, $5, $5     ; dark grey
                RGB     $5, $5, $F     ; light blue
                RGB     $5, $F, $5     ; light green
                RGB     $5, $F, $F     ; light cyan
                RGB     $F, $5, $5     ; light red
                RGB     $F, $5, $F     ; light magenta
                RGB     $F, $F, $5     ; yellow
                RGB     $F, $F, $F     ; white

.segment "FONT"
                .incbin "font/vga8x16.bin"
