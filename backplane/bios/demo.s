; The video demo: tiles, sprites, scrolling, a palette fade and DMA, on the
; video card (hw/design.md, "Video"). From the Woz Monitor: C803R. Any key
; goes back to the monitor.
;
; It lives in flash block 29, which the BIOS's DEMO entry (video.s) maps at
; $4000 through W1, read-only, and jumps to. It runs from there: code and
; graphics (demo/, made by make_assets.py) are read through W1, and video
; memory is written through W2 put on the RAM block that holds it.
;
;   layer 0  tiles: a 64x32 map of 8x8 4-bit tiles, scrolled one pixel a frame
;   layer 1  text 40x30 in the BIOS's font: the caption, transparent elsewhere
;   sprites  12 balls, 16x16, between the layers; positions kept in RAM ($0400)
;            and copied to the card's table by DMA in each vertical blank
;   palette  its own, faded in from black over 16 frames
;
; The BIOS's text screen keeps its own part of video memory (units 0-12),
; so going back is a few register writes, with the screen as it was left:
; BREAK's (bios.s), the same as Pause/Break's.
;
; Video memory (1 KB units of banks 8-B, $80000 + unit x 1 KB):
;
;   unit 13  $83400  palette, 5 slices of 16: the scene, then 4 balls'
;   unit 16  $84000  caption text, 40 x 30 cells
;   unit 20  $85000  tiles
;   unit 24  $86000  map
;   unit 32  $88000  ball image (64-byte unit 512)
;   unit 36  $89000  sprite table

.setcpu "65C02"
.include "backplane.inc"
.import BREAK, KEYHIT
.export DEMO_MAIN

BALLS     = 12
SHADOW    = $0400                      ; sprite entries in RAM, physical $00400
PAL_UNIT  = 13
TEXT_UNIT = 16
TILE_UNIT = 20
MAP_UNIT  = 24
BALL_UNIT = 32
SPR_UNIT  = 36
BALL_IMAGE = BALL_UNIT * 1024 / 64

; Zero page $30-$7B (the rest is the BIOS's and the monitor's: backplane.inc)
SRC     = $30
DST     = $32
LEN     = $34
LEVEL   = $36                          ; fade level, 0-15
SCROLL  = $37                          ; layer 0 HSCROLL, 10 bits
XLO     = $40                          ; BALLS bytes each from here
XHI     = XLO + BALLS
DX      = XHI + BALLS                  ; +1 or -1 (low byte; high follows the sign)
YPOS    = DX + BALLS
DY      = YPOS + BALLS
.assert DY + BALLS <= $80, error, "demo zero page overflows"

VBLOCK0 = 32                           ; RAM blocks holding banks 8-B, 16 KB each
VWIN    = $8000                        ; W2


; LEN bytes from src to dst (COPY).
.macro COPYW src, dst, len
                LDA     #<(src)
                STA     SRC
                LDA     #>(src)
                STA     SRC+1
                LDA     #<(dst)
                STA     DST
                LDA     #>(dst)
                STA     DST+1
                LDA     #<(len)
                STA     LEN
                LDA     #>(len)
                STA     LEN+1
                JSR     COPY
.endmacro

.segment "DEMO"

DEMO_MAIN:
                LDA     #$00
                STA     VIDEO_CTRL     ; off while it is set up (the card keeps its timing)

                ; graphics from flash (W1) into video memory (W2)
                LDA     #VBLOCK0 + 1   ; $84000-$87FFF: text, tiles, map
                STA     NB_WIN2
                LDX     #0             ; the caption layer: all blank and transparent
                JSR     CLEARTEXT
                COPYW   TILES, VWIN + (TILE_UNIT - 16) * 1024, 512
                COPYW   MAP, VWIN + (MAP_UNIT - 16) * 1024, 4096
                LDA     #VBLOCK0 + 2   ; $88000-$8BFFF: the ball
                STA     NB_WIN2
                COPYW   BALL, VWIN + (BALL_UNIT - 32) * 1024, 128
                LDA     #VBLOCK0 + 1
                STA     NB_WIN2
                JSR     CAPTION

                STZ     LEVEL          ; black, then fade in
                JSR     FADE

                ; the balls: spread out, moving every which way
                LDX     #BALLS - 1
@init:          TXA
                ASL     A
                ASL     A
                ASL     A
                ASL     A              ; x 16
                ADC     #40            ; 40-216: on screen
                STA     XLO,X
                STZ     XHI,X
                TXA
                ASL     A
                ASL     A
                ASL     A
                ASL     A              ; x 16
                ADC     #24
                STA     YPOS,X
                TXA
                AND     #1
                BEQ     @right
                LDA     #$FF
                BRA     @dx
@right:         LDA     #$01
@dx:            STA     DX,X
                TXA
                AND     #2
                BEQ     @down
                LDA     #$FF
                BRA     @dy
@down:          LDA     #$01
@dy:            STA     DY,X
                DEX
                BPL     @init
                JSR     SPRITES        ; the shadow table, before the first copy

                ; the card: palette, layers, sprites
                LDA     #PAL_UNIT
                STA     VIDEO_PALBASE
                STZ     VIDEO_BORDER
                LDA     #VIDEO_TILE | VIDEO_MAP_W64
                STA     VIDEO_L0_CONFIG
                LDA     #MAP_UNIT
                STA     VIDEO_L0_MAP
                LDA     #TILE_UNIT
                STA     VIDEO_L0_TILE
                LDA     #VIDEO_TEXT40
                STA     VIDEO_L1 + 0
                LDA     #TEXT_UNIT
                STA     VIDEO_L1 + 1
                LDA     #1             ; the BIOS's font
                STA     VIDEO_L1 + 2
                LDA     #SPR_UNIT
                STA     VIDEO_SPRBASE
                STZ     SCROLL
                STZ     SCROLL+1
                LDA     #VIDEO_ALL
                STA     VIDEO_CTRL

FRAME:          JSR     WAITVB         ; the picture is not being drawn: change it now
                JSR     DMASPRITES
                LDA     SCROLL
                STA     VIDEO_L0_HSCROLL
                LDA     SCROLL+1
                STA     VIDEO_L0_HSCROLL + 1
                LDA     LEVEL
                CMP     #15
                BCS     @lit
                INC     LEVEL
                JSR     FADE
@lit:           INC     SCROLL         ; next frame's scroll: wraps at 1024 (the map is 512)
                BNE     @move
                INC     SCROLL+1
                LDA     SCROLL+1
                AND     #$03
                STA     SCROLL+1
@move:          JSR     MOVE
                JSR     SPRITES
                JSR     KEYHIT         ; a key, serial or USB? (taken: it only meant "stop")
                BCC     FRAME

                JMP     BREAK          ; the windows and the BIOS's screen back: this code is in W1

; Copy LEN bytes from (SRC) to (DST).
COPY:
                LDY     #0
@page:          LDA     LEN+1
                BEQ     @tail
@byte:          LDA     (SRC),Y
                STA     (DST),Y
                INY
                BNE     @byte
                INC     SRC+1
                INC     DST+1
                DEC     LEN+1
                BRA     @page
@tail:          LDX     LEN
                BEQ     @done
@last:          LDA     (SRC),Y
                STA     (DST),Y
                INY
                DEX
                BNE     @last
@done:          RTS


; The caption layer (W2 on block 33): blank cells, attribute 0: transparent.
CLEARTEXT:
                LDA     #<VWIN
                STA     DST
                LDA     #>VWIN
                STA     DST+1
                LDX     #10            ; 40 x 30 x 2 = 2,400 bytes: 10 pages is enough
                LDY     #0
@fill:          LDA     #' '
                STA     (DST),Y
                INY
                LDA     #0
                STA     (DST),Y
                INY
                BNE     @fill
                INC     DST+1
                DEX
                BNE     @fill
                RTS

; Write the caption lines: each is row, column, attribute, then text, 0.
CAPTION:
                LDX     #0
@line:          LDA     LINES,X
                BMI     @done          ; $FF ends the list
                ; DST = VWIN + (row x 40 + column) x 2: row x 8, kept, x 4 more, plus it
                STA     DST            ; row
                STZ     DST+1
                ASL     DST
                ROL     DST+1
                ASL     DST
                ROL     DST+1
                ASL     DST
                ROL     DST+1          ; x 8
                LDA     DST
                STA     LEN
                LDA     DST+1
                STA     LEN+1
                ASL     DST
                ROL     DST+1
                ASL     DST
                ROL     DST+1          ; x 32
                CLC
                LDA     DST
                ADC     LEN
                STA     DST
                LDA     DST+1
                ADC     LEN+1
                STA     DST+1          ; x 40
                LDA     LINES+1,X      ; + column
                CLC
                ADC     DST
                STA     DST
                BCC     @nc
                INC     DST+1
@nc:            ASL     DST            ; x 2: cells
                ROL     DST+1
                LDA     DST+1
                CLC
                ADC     #>VWIN
                STA     DST+1
                LDA     LINES+2,X
                STA     LEN            ; the attribute
                INX
                INX
                INX
                LDY     #0
@char:          LDA     LINES,X
                BEQ     @end
                STA     (DST),Y
                INY
                LDA     LEN
                STA     (DST),Y
                INY
                INX
                BRA     @char
@end:           INX
                BRA     @line
@done:          RTS

LINES:
                .byte   1, 2, $0F, "OO-1 VIDEO CARD: TILES, SPRITES, DMA", 0
                .byte   28, 6, $0E, "PRESS ANY KEY FOR THE MONITOR", 0
                .byte   $FF

; The palette at fade level LEVEL into unit 13 (W2 on block 32 for it).
FADE:
                LDA     #VBLOCK0
                STA     NB_WIN2
                ; SRC = PALETTES + LEVEL x 160
                LDA     LEVEL
                STA     SRC
                STZ     SRC+1
                LDY     #5             ; x 32, then x 5
@x32:           ASL     SRC
                ROL     SRC+1
                DEY
                BNE     @x32
                LDA     SRC
                STA     DST
                LDA     SRC+1
                STA     DST+1
                ASL     SRC
                ROL     SRC+1
                ASL     SRC
                ROL     SRC+1          ; x 128
                CLC
                LDA     SRC
                ADC     DST
                STA     SRC
                LDA     SRC+1
                ADC     DST+1
                STA     SRC+1          ; x 160
                CLC
                LDA     SRC
                ADC     #<PALETTES
                STA     SRC
                LDA     SRC+1
                ADC     #>PALETTES
                STA     SRC+1
                LDA     #<(VWIN + PAL_UNIT * 1024)
                STA     DST
                LDA     #>(VWIN + PAL_UNIT * 1024)
                STA     DST+1
                LDA     #160
                STA     LEN
                STZ     LEN+1
                JSR     COPY
                LDA     #VBLOCK0 + 1
                STA     NB_WIN2
                RTS

; Wait for the start of a vertical blank (STATUS bit 7 going on).
WAITVB:
@out:           BIT     VIDEO_STATUS
                BMI     @out
@in:            BIT     VIDEO_STATUS
                BPL     @in
                RTS

; Bounce the balls inside 320x240 (a ball is 16 square: x 0-304, y 0-224).
MOVE:
                LDX     #BALLS - 1
@ball:          LDA     DX,X           ; x += dx, 16 bits
                CLC
                ADC     XLO,X
                STA     XLO,X
                LDA     DX,X
                BMI     @neg
                LDA     #0
                BRA     @hi
@neg:           LDA     #$FF
@hi:            ADC     XHI,X
                STA     XHI,X
                ; off either side? (x < 0 shows as $FFFF; x > 304 as $0131+)
                BMI     @left
                CMP     #>304
                BCC     @xok
                LDA     XLO,X
                CMP     #<305
                BCC     @xok
                LDA     #$FF           ; past the right: turn left
                STA     DX,X
                BRA     @xok
@left:          STZ     XLO,X          ; past the left: back to 0, turn right
                STZ     XHI,X
                LDA     #$01
                STA     DX,X
@xok:           LDA     YPOS,X         ; y += dy, 0-224
                CLC
                ADC     DY,X
                STA     YPOS,X
                CMP     #225
                BCC     @yok
                LDA     DY,X           ; off the top ($FF) going up, or the bottom (225) going down
                BMI     @top
                LDA     #224
                BRA     @sety
@top:           LDA     #0
@sety:          STA     YPOS,X
                LDA     DY,X           ; turn round: $01 <-> $FF
                EOR     #$FE
                STA     DY,X
@yok:           DEX
                BPL     @ball
                RTS

; The shadow sprite table from the positions: image, X, Y, between the
; layers in collision group 1, 16x16 at 4 bits, a ball colour each.
SPRITES:
                LDX     #0             ; ball
                LDY     #0             ; entry byte
@entry:         LDA     #<BALL_IMAGE
                STA     SHADOW,Y
                LDA     #>BALL_IMAGE
                STA     SHADOW+1,Y
                LDA     XLO,X
                STA     SHADOW+2,Y
                LDA     XHI,X
                STA     SHADOW+3,Y
                LDA     YPOS,X
                STA     SHADOW+4,Y
                LDA     #0
                STA     SHADOW+5,Y
                LDA     #(2 << 2) | $10 ; depth 2 (between), group 1
                STA     SHADOW+6,Y
                TXA
                AND     #3
                CLC
                ADC     #$51           ; 16x16 ($50), palette slice 1-4
                STA     SHADOW+7,Y
                TYA
                CLC
                ADC     #8
                TAY
                INX
                CPX     #BALLS
                BNE     @entry
                RTS

; The shadow table into the card's (unit 36) by DMA: 96 bytes, about 27 us,
; waited for, since SPRITES writes the shadow next.
DMASPRITES:
                LDA     #<SHADOW
                STA     DMA3_ADDR
                LDA     #>SHADOW
                STA     DMA3_ADDR+1
                STZ     DMA3_ADDR+2    ; physical $00400: RAM block 0, as W0 shows it
                LDA     #<(SPR_UNIT * 1024)
                STA     DMA3_DEST
                LDA     #>(SPR_UNIT * 1024)
                STA     DMA3_DEST+1
                LDA     #$08           ; $80000 + unit x 1 KB
                STA     DMA3_DEST+2
                LDA     #BALLS * 8
                STA     DMA3_COUNT
                STZ     DMA3_COUNT+1
                LDA     #DMA_M2M
                STA     DMA3_MODE
                LDA     #DMA_GO
                STA     DMA3_CTRL
@wait:          LDA     DMA3_CTRL      ; ENABLE clears at terminal count
                LSR     A
                BCS     @wait
                RTS

TILES:          .incbin "demo/tiles.bin"
MAP:            .incbin "demo/map.bin"
BALL:           .incbin "demo/ball.bin"
PALETTES:       .incbin "demo/palettes.bin"
