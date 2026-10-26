; The BIOS's text screen on the video card (hw/design.md, "Video").
;
; VINIT sets up an 80x30 text layer and VPUTC writes a character to it, so
; everything the Woz Monitor prints (ECHO calls VPUTC) shows on the VGA
; screen as well as the serial line.
;
; Video memory, in the card's 1 KB units of banks 8-B ($80000 + unit x 1 KB):
;
;   unit 0   $80000  the palette: 16 colours, the CGA's
;   unit 1   $80400  the font, 4 KB, copied from flash block 30 (font/)
;   unit 8   $82000  the text: 80 x 30 cells of character + attribute
;
; The CPU reaches them through W2 ($8000-$BFFF) put on RAM block 32 for a
; moment and put back; DMA reaches them by physical address. Scrolling is
; one 4,640-byte memory-to-memory DMA copy (1.3 ms), waited for, so it is
; whole whatever BURST a program has set; clearing is a cell
; written by the CPU then copied onto the next by an overlapping DMA, which
; repeats it. The cursor is an underline whose attribute blinks (bit 7, with
; CONFIG's blink on), so the card blinks it and the BIOS does nothing.
;
; With no video card, or no DMA controller, VINIT leaves VOK clear and
; VPUTC does nothing. Break (NMI) puts W2 back and calls VSCREEN, so one
; during VPUTC, or a program that moved the screen, leaves the monitor on the
; text screen as VINIT made it. PUTAT keeps the cursor on the screen whatever
; a program has left in VCOL and VROW.

.setcpu "65C02"
.include "backplane.inc"
.export VINIT, VPUTC, VSCREEN, DEMO
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

VBLOCK  = 32                           ; RAM block 32 = $80000, through W2
VWIN    = $8000
PAL_UNIT  = 0
FONT_UNIT = 1
TEXT_UNIT = 8
TEXT_PHYS = $80000 + TEXT_UNIT * 1024

FONT_BLOCK = 30                        ; flash block 30, through W1
FONT_CPU   = $4000

.segment "VIDEO"

; Turn the screen on: font, palette, a clear screen, then the registers.
VINIT:
                STZ     VOK
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V1
                BNE     @done
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @done
                LDA     #DMA_ON
                STA     DMA_CONTROL

                LDA     NB_WIN1
                PHA
                LDA     NB_WIN2
                PHA
                LDA     #NB_ROM | NB_RO | FONT_BLOCK
                STA     NB_WIN1
                LDA     #VBLOCK
                STA     NB_WIN2

                STZ     VSRC           ; the font: 16 pages, flash to unit 1
                STZ     VPTR
                LDA     #>FONT_CPU
                STA     VSRC+1
                LDA     #>(VWIN + FONT_UNIT * 1024)
                STA     VPTR+1
                LDY     #$00
@font:          LDA     (VSRC),Y
                STA     (VPTR),Y
                INY
                BNE     @font
                INC     VPTR+1
                INC     VSRC+1
                LDA     VSRC+1
                CMP     #>(FONT_CPU + $1000)
                BNE     @font

                LDX     #31            ; the palette
@pal:           LDA     CGA,X
                STA     VWIN + PAL_UNIT * 1024,X
                DEX
                BPL     @pal

                LDA     #' '           ; the screen: one blank cell, repeated
                STA     VWIN + TEXT_UNIT * 1024
                LDA     #ATTR
                STA     VWIN + TEXT_UNIT * 1024 + 1
                LDX     #JOB_CLS
                JSR     DMAJOB

                PLA
                STA     NB_WIN2
                PLA
                STA     NB_WIN1

                STZ     VCOL
                STZ     VROW
                DEC     VOK            ; $FF: on
                BRA     VSCREEN
@done:          RTS

; The card's registers for the text screen, if it is on: layer 0 only,
; unscrolled, no interrupts. Its memory is left as it is.
VSCREEN:
                BIT     VOK
                BPL     @done
                STZ     VIDEO_IEN
                STZ     VIDEO_PALBASE
                STZ     VIDEO_SPRBASE
                STZ     VIDEO_BORDER
                LDA     #VIDEO_TEXT80B
                STA     VIDEO_L0_CONFIG
                LDA     #TEXT_UNIT
                STA     VIDEO_L0_MAP
                LDA     #FONT_UNIT
                STA     VIDEO_L0_TILE
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
; up one, other control characters do nothing. A, X and Y are kept.
VPUTC:
                BIT     VOK
                BPL     @off
                PHA
                PHX
                PHY
                LDX     NB_WIN2
                PHX
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
@scroll:        LDX     #JOB_SCROLL
                JSR     DMAJOB
                STZ     VCOL
                JSR     BLANK          ; the last row's first cell...
                LDX     #JOB_CLEARROW
                JMP     DMAJOB         ; ...repeated along it

; Run DMA job X (an offset into JOBS) on channel 3, a block transfer, and
; wait for it: with a BURST limit the CPU gets cycles back, but it is done
; on return. The master enable goes on, in case a program turned it off.
DMAJOB:
                LDY     #$00
@regs:          LDA     JOBS,X
                STA     DMA3_ADDR,Y    ; source: Y = 0-2
                LDA     JOBS+3,X
                STA     DMA3_DEST,Y    ; destination
                INX
                INY
                CPY     #$03
                BNE     @regs
                LDA     JOBS+3,X       ; count (X has moved on by 3)
                STA     DMA3_COUNT
                LDA     JOBS+4,X
                STA     DMA3_COUNT+1
                LDA     #DMA_M2M
                STA     DMA3_MODE
                LDA     DMA_CONTROL
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_GO
                STA     DMA3_CTRL
@wait:          LDA     DMA3_CTRL      ; ENABLE clears at terminal count
                LSR     A
                BCS     @wait
                RTS

.macro JOB src, dst, count
                .faraddr src
                .faraddr dst
                .word   count
.endmacro

JOBS:
JOB_CLS = * - JOBS                     ; the first cell onto all the others
                JOB     TEXT_PHYS, TEXT_PHYS + 2, COLS * ROWS * 2 - 2
JOB_SCROLL = * - JOBS                  ; rows 1-29 up to 0-28
                JOB     TEXT_PHYS + COLS * 2, TEXT_PHYS, COLS * (ROWS - 1) * 2
JOB_CLEARROW = * - JOBS                ; the last row's first cell along it
                JOB     TEXT_PHYS + COLS * (ROWS - 1) * 2, TEXT_PHYS + COLS * (ROWS - 1) * 2 + 2, COLS * 2 - 2

ROWLO:
.repeat ROWS, r
                .byte   <(VWIN + TEXT_UNIT * 1024 + r * COLS * 2)
.endrepeat
ROWHI:
.repeat ROWS, r
                .byte   >(VWIN + TEXT_UNIT * 1024 + r * COLS * 2)
.endrepeat

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
