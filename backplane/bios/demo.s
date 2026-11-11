; The video demo: tiles, sprites, scrolling, a palette fade, a ripple and
; DMA, on the video card's revision 2 (hw/video-rev2.md, proposed). From the
; Woz Monitor: C803R. Any key goes back to the monitor.
;
; It lives in flash block 29, which the BIOS's DEMO entry (video.s) maps at
; $4000 through W1, read-only, and jumps to. It runs from there. Everything
; it shows is in the card's own memory, loaded through the card's data port
; (PADDR, PDATA): the CPU writes the graphics from flash (demo/, made by
; make_assets.py) and the font from flash block 30, which it maps through W2;
; the DMA controller's channel 1 streams from RAM into the port the sprite
; table, every frame, and the ripple's rows.
;
;   layer 0  tiles: a 64x32 map of 8x8 4-bit tiles, scrolled one pixel a frame;
;            or, every other 256 frames, the ripple (below)
;   layer 1  text 40x30 in its own copy of the BIOS's font: the caption,
;            transparent elsewhere. Two pages, one for each scene, flipped by
;            its MAPBASE
;   sprites  12 balls, 16x16, between the layers; positions kept in RAM ($0400)
;            and streamed to the card's table by port DMA in each vertical blank
;   palette  its own, faded in from black over 16 frames
;
; The ripple is the new effect, for what revision 2's memory allows: 16
; frames of a 256x240 8-bit bitmap, 60 KB each, 960 KB in all (revision 1's
; card had 256 KB for everything), built once at the start and then shown by
; flipping layer 0's MAPBASE every frame, so it costs the CPU nothing while
; it plays. Each row of a frame is a window on a stripe pattern in RAM,
; moved along by a sine of the row and the frame: 3,840 port DMA transfers
; of 256 bytes, about 0.3 s, while the screen is still off.
;
; The demo keeps out of the card's memory below $040000 (the BIOS's and
; OODOS's: the palette, the font, the text screen) and from $1C0000 (Apple
; II mode's), and never moves the aperture's slots, so the monitor's screen
; is still there when it ends: BREAK (bios.s) only puts the card's registers
; back (VSCREEN, video.s).
;
; Card memory (the programs' region, hw/video-rev2.md; a base register holds
; the address / 256):
;
;   $040000  palette, 6 slices of 16: the scene, 4 balls', the ripple's ramp
;   $040400  font, 4 KB, from flash block 30
;   $041400  caption, scene 1 (tiles): 40 x 30 cells
;   $041E00  caption, scene 2 (the ripple)
;   $043000  tiles
;   $044000  map
;   $045000  ball image (256-byte unit $0450)
;   $045100  sprite table
;   $050000  the ripple: 16 bitmaps, $F000 apart, to $13FFFF

.setcpu "65C02"
.include "backplane.inc"
.import BREAK, KEYHIT
.export DEMO_MAIN

BALLS     = 12
SHADOW    = $0400                      ; sprite entries in RAM, physical $00400
STRIPES   = $0480                      ; the ripple's pattern in RAM: 288 bytes
STRIPES_LEN = 256 + 32
.assert STRIPES >= SHADOW + BALLS * 8, error, "the stripes overlap the sprite table"
.assert STRIPES + STRIPES_LEN <= $0600, error, "the stripes leave the demo's RAM ($0400-$05FF)"

PAL_AT    = $040000
FONT_AT   = $040400
TEXT1_AT  = $041400
TEXT2_AT  = $041E00
TILE_AT   = $043000
MAP_AT    = $044000
BALL_AT   = $045000
SPR_AT    = $045100
FLIP_AT   = $050000
FLIP_SIZE = $F000                      ; 256 x 240, a byte a pixel
FRAMES    = 16
.assert FLIP_AT + FRAMES * FLIP_SIZE <= $1C0000, error, "the ripple runs into Apple II mode's memory"
BALL_IMAGE = BALL_AT / 256             ; the sprite table's unit
SLICES    = 6                          ; palette slices of 16 colours, 2 bytes each
RAMP      = 5 * 16                     ; the ripple's colours, 80-95
MEM_NEEDED = (FLIP_AT + FRAMES * FLIP_SIZE + $FFFF) / $10000 ; in MEMSIZE's 64 KB units

FONT_BLOCK = 30                        ; flash block 30, the BIOS's font, through W2
FONT_CPU   = $8000

L0_TILES  = VIDEO_TILE | VIDEO_MAP_W64
L0_RIPPLE = $03                        ; CONFIG: bitmap 256, 8 bits a pixel

; Zero page $30-$7B (the rest is the BIOS's and the monitor's: backplane.inc)
SRC     = $30
LEN     = $34
LEVEL   = $36                          ; fade level, 0-15
SCROLL  = $37                          ; layer 0 HSCROLL, 10 bits
FCOUNT  = $39                          ; frames, 0-255, in this scene
SCENE   = $3A                          ; 0 tiles, 1 the ripple
CONFIG  = $3B                          ; layer 0's CONFIG for the next frame
RIPPLE  = $3C                          ; bit 7: the card has the memory for it
FRM     = $3D                          ; while the ripple is built: its frame,
ROW     = $3E                          ; the row,
PHASE   = $3F                          ; and the frame x 16, where the sine starts
XLO     = $40                          ; BALLS bytes each from here
XHI     = XLO + BALLS
DX      = XHI + BALLS                  ; +1 or -1 (low byte; high follows the sign)
YPOS    = DX + BALLS
DY      = YPOS + BALLS
.assert DY + BALLS <= $7C, error, "demo zero page overflows"


; PADDR = card address `at`.
.macro PORTAT at
                LDA     #<(at)
                STA     VIDEO_PADDR
                LDA     #>(at)
                STA     VIDEO_PADDR+1
                LDA     #^(at)
                STA     VIDEO_PADDR+2
.endmacro

; `len` bytes from CPU address `src` into the port (TOPORT).
.macro PUTP src, len
                LDA     #<(src)
                STA     SRC
                LDA     #>(src)
                STA     SRC+1
                LDA     #<(len)
                STA     LEN
                LDA     #>(len)
                STA     LEN+1
                JSR     TOPORT
.endmacro

; A two-byte register (or zero page word) from a constant, low byte first.
.macro SET16 reg, value
                LDA     #<(value)
                STA     reg
                LDA     #>(value)
                STA     reg+1
.endmacro

.segment "DEMO"

DEMO_MAIN:
                LDA     VIDEO_ID       ; revision 2 only: on another card, straight back
                CMP     #VIDEO_ID_V2
                BEQ     @rev2
                JMP     BREAK
@rev2:          STZ     VIDEO_CTRL     ; off while it is set up (the card keeps its timing)
                STZ     VIDEO_SPLASH   ; and the boot splash, if it's still on: the screen is ours
                STZ     VIDEO_PCTL     ; the CPU drives the port: no DRQ, stepping up
                SET16   VIDEO_PSTEP, 1
                STZ     RIPPLE
                LDA     VIDEO_MEMSIZE
                CMP     #MEM_NEEDED
                BCC     @small         ; a card too small for the ripple: the tiles only
                DEC     RIPPLE

@small:         LDA     #NB_ROM | NB_RO | FONT_BLOCK
                STA     NB_WIN2        ; BREAK puts W2 back
                PORTAT  FONT_AT
                PUTP    FONT_CPU, 4096
                PORTAT  TILE_AT
                PUTP    TILES, 512
                PORTAT  MAP_AT
                PUTP    MAP, 4096
                PORTAT  BALL_AT
                PUTP    BALL, 128
                JSR     CAPTION
                BIT     RIPPLE
                BPL     @fade
                JSR     BUILD

@fade:          STZ     LEVEL          ; black, then fade in
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
                SET16   VIDEO_PALBASE, PAL_AT / 256
                STZ     VIDEO_BORDER
                LDA     #L0_TILES
                STA     VIDEO_L0_CONFIG
                STA     CONFIG
                SET16   VIDEO_L0_MAP, MAP_AT / 256
                SET16   VIDEO_L0_TILE, TILE_AT / 256
                STZ     VIDEO_L0_VSCROLL
                STZ     VIDEO_L0_VSCROLL+1
                STZ     VIDEO_L0_PALOFS
                LDA     #VIDEO_TEXT40
                STA     VIDEO_L1_CONFIG
                SET16   VIDEO_L1_MAP, TEXT1_AT / 256
                SET16   VIDEO_L1_TILE, FONT_AT / 256
                STZ     VIDEO_L1_HSCROLL
                STZ     VIDEO_L1_HSCROLL+1
                STZ     VIDEO_L1_VSCROLL
                STZ     VIDEO_L1_VSCROLL+1
                STZ     VIDEO_L1_PALOFS
                SET16   VIDEO_SPRBASE, SPR_AT / 256
                STZ     SCROLL
                STZ     SCROLL+1
                STZ     FCOUNT
                STZ     SCENE
                LDA     #VIDEO_ALL
                STA     VIDEO_CTRL

; Each frame, in the vertical blank. The base registers and the scroll take
; effect at the next line 480, a frame after they are written, but CONFIG at
; once: so CONFIG is written a frame after the bases it goes with.
FRAME:          JSR     WAITVB         ; the picture is not being drawn: change it now
                JSR     DMASPRITES
                LDA     CONFIG         ; what last frame's bases were written for
                STA     VIDEO_L0_CONFIG
                LDA     SCROLL
                STA     VIDEO_L0_HSCROLL
                LDA     SCROLL+1
                STA     VIDEO_L0_HSCROLL+1
                LDA     LEVEL
                CMP     #15
                BCS     @lit
                INC     LEVEL
                JSR     FADE
@lit:           INC     FCOUNT         ; the next frame: after 256 of them, the other scene
                BNE     @bases
                BIT     RIPPLE
                BPL     @bases
                LDA     SCENE
                EOR     #1
                STA     SCENE
@bases:         LDA     SCENE
                BNE     @ripple
                LDA     #L0_TILES
                STA     CONFIG
                SET16   VIDEO_L0_MAP, MAP_AT / 256
                SET16   VIDEO_L1_MAP, TEXT1_AT / 256
                BRA     @scroll
@ripple:        LDA     #L0_RIPPLE
                STA     CONFIG
                LDA     FCOUNT
                AND     #FRAMES - 1
                TAX
                LDA     FLIPLO,X
                STA     VIDEO_L0_MAP
                LDA     FLIPHI,X
                STA     VIDEO_L0_MAP+1
                SET16   VIDEO_L1_MAP, TEXT2_AT / 256
@scroll:        INC     SCROLL         ; next frame's scroll: wraps at 1024 (the map is 512)
                BNE     @move
                INC     SCROLL+1
                LDA     SCROLL+1
                AND     #$03
                STA     SCROLL+1
@move:          JSR     MOVE
                JSR     SPRITES
                JSR     KEYHIT         ; a key, serial or USB? (taken: it only meant "stop")
                BCS     @end
                JMP     FRAME
@end:           JMP     BREAK          ; the windows and the BIOS's screen back: this code is in W1

; Frame n of the ripple: its MAPBASE.
FLIPLO:
.repeat FRAMES, n
                .byte   <((FLIP_AT + n * FLIP_SIZE) / 256)
.endrepeat
FLIPHI:
.repeat FRAMES, n
                .byte   >((FLIP_AT + n * FLIP_SIZE) / 256)
.endrepeat

; LEN bytes from (SRC) into the port at PADDR, by the CPU. Uses A, X, Y.
TOPORT:
                LDY     #0
                LDX     LEN+1
                BEQ     @tail
@page:          LDA     (SRC),Y
                STA     VIDEO_PDATA
                INY
                BNE     @page
                INC     SRC+1
                DEX
                BNE     @page
@tail:          LDX     LEN
                BEQ     @done
@last:          LDA     (SRC),Y
                STA     VIDEO_PDATA
                INY
                DEX
                BNE     @last
@done:          RTS

; The two caption pages: blank cells, attribute 0 (transparent), then the
; lines. From TEXT1_AT, 2,560 cells: both pages, and the gap between them.
CAPTION:
                PORTAT  TEXT1_AT
                LDY     #10            ; 10 x 256 cells
                LDX     #0
                LDA     #' '
@blank:         STA     VIDEO_PDATA
                STZ     VIDEO_PDATA
                DEX
                BNE     @blank
                DEY
                BNE     @blank
                LDX     #0
@line:          LDA     LINES,X        ; attribute, card address (3 bytes), text, 0
                BEQ     @done          ; attribute 0 ends the list
                STA     SRC            ; the attribute
                LDA     LINES+1,X
                STA     VIDEO_PADDR
                LDA     LINES+2,X
                STA     VIDEO_PADDR+1
                LDA     LINES+3,X
                STA     VIDEO_PADDR+2
                INX
                INX
                INX
                INX
@char:          LDA     LINES,X
                BEQ     @end
                STA     VIDEO_PDATA
                LDA     SRC
                STA     VIDEO_PDATA
                INX
                BRA     @char
@end:           INX
                BRA     @line
@done:          RTS
.assert TEXT2_AT + 40 * 30 * 2 <= TEXT1_AT + 10 * 256 * 2, error, "the blank doesn't cover both captions"

.macro LINE page, row, col, attr, text
                .byte   attr
                .faraddr page + (row * 40 + col) * 2
                .byte   text, 0
.endmacro

LINES:
                LINE    TEXT1_AT, 1, 1, $0F, "OO-1 VIDEO CARD 2: TILES, SPRITES, DMA"
                LINE    TEXT1_AT, 28, 6, $0E, "PRESS ANY KEY FOR THE MONITOR"
                LINE    TEXT2_AT, 1, 2, $0F, "960 KB OF FRAMES, FLIPPED BY MAPBASE"
                LINE    TEXT2_AT, 28, 6, $0E, "PRESS ANY KEY FOR THE MONITOR"
                .byte   0
.assert * - LINES <= 256, error, "the caption lines outgrow X"

; The ripple's 16 frames, row after row, into card memory from FLIP_AT by
; port DMA. Row y of frame f is the 256 bytes of STRIPES from (y / 2 + 2f +
; SINE[4y + 16f]) mod 32: stripes 32 pixels apart, leaning, waved by the sine
; down the screen. Each frame moves the wave and the stripes on, so a frame
; 16 would be frame 0 again, and the flipping never shows a seam.
BUILD:
                LDX     #0             ; the pattern: a ramp up and down, colours 80-95
@stripe:        TXA
                AND     #31
                CMP     #16
                BCC     @up
                EOR     #31            ; 16-31 -> 15-0
@up:            CLC
                ADC     #RAMP
                STA     STRIPES,X
                CPX     #STRIPES_LEN - 256
                BCS     @next
                STA     STRIPES + 256,X ; and its first 32 again after it
@next:          INX
                BNE     @stripe
                PORTAT  FLIP_AT        ; the frames are back to back: PADDR just runs on
                STZ     FRM
@frame:         LDA     FRM
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     PHASE          ; f x 16
                STZ     ROW
@row:           LDA     ROW            ; the sine at 4y + 16f
                ASL     A
                ASL     A
                CLC
                ADC     PHASE
                TAX
                LDA     ROW
                LSR     A              ; + y / 2
                CLC
                ADC     SINE,X
                ADC     FRM            ; + 2f (no carry: the sum stays under 256)
                ADC     FRM
                AND     #31
                CLC
                ADC     #<STRIPES
                STA     SRC
                LDA     #>STRIPES
                ADC     #0
                STA     SRC+1
                STZ     LEN
                LDA     #1
                STA     LEN+1          ; 256 bytes
                JSR     PORTDMA
                INC     ROW
                LDA     ROW
                CMP     #240
                BNE     @row
                INC     FRM
                LDA     FRM
                CMP     #FRAMES
                BNE     @frame
                RTS

; The palette at fade level LEVEL into PAL_AT: SLICES x 32 bytes from
; PALETTES + LEVEL x 192, through the port.
.assert SLICES * 32 = 192, error, "FADE multiplies by 192"
FADE:
                LDA     LEVEL          ; x 64
                STA     SRC
                STZ     SRC+1
                LDY     #6
@x64:           ASL     SRC
                ROL     SRC+1
                DEY
                BNE     @x64
                LDA     SRC            ; and x 128
                ASL     A
                STA     LEN
                LDA     SRC+1
                ROL     A
                STA     LEN+1
                CLC
                LDA     SRC
                ADC     LEN
                STA     SRC
                LDA     SRC+1
                ADC     LEN+1
                STA     SRC+1          ; x 192
                CLC
                LDA     SRC
                ADC     #<PALETTES
                STA     SRC
                LDA     SRC+1
                ADC     #>PALETTES
                STA     SRC+1
                PORTAT  PAL_AT
                LDA     #SLICES * 32
                STA     LEN
                STZ     LEN+1
                JMP     TOPORT

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
.assert BALL_IMAGE < $8000, error, "the ball's unit needs more than 15 bits"
SPRITES:
                LDX     #0             ; ball
                LDY     #0             ; entry byte
@entry:         LDA     #<BALL_IMAGE   ; the image's unit: bits 7-0,
                STA     SHADOW,Y
                LDA     #>BALL_IMAGE   ; bits 14-8, and bit 7 0: 4 bits a pixel
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

; The shadow table into the card's (SPR_AT) through the port, by DMA: 96
; bytes, about 27 us, waited for, since SPRITES writes the shadow next.
DMASPRITES:
                PORTAT  SPR_AT
                SET16   SRC, SHADOW
                SET16   LEN, BALLS * 8
                ; fall through

; LEN bytes from (SRC), RAM block 0 as W0 shows it, into the port at PADDR
; by DMA channel 1 in demand mode: the card asks while PCTL's DRQ is on, and
; the controller moves the bytes with the CPU held. Waited for, with the
; master enable on (a program may have left it off) and CONTROL put back
; after, as the BIOS's DMA3RUN does: 65,536 polls, then it gives up and
; stops the channel. DRQ is off again on return, so the CPU has the port.
; Uses A, X, Y.
PORTDMA:
                LDA     #DMA_CH1       ; stopped while it is programmed
                STA     DMA_ABORT
                LDA     SRC
                STA     DMA1_ADDR
                LDA     SRC+1
                STA     DMA1_ADDR+1
                STZ     DMA1_ADDR+2    ; physical $00000-$0FFFF
                LDA     LEN
                STA     DMA1_COUNT
                LDA     LEN+1
                STA     DMA1_COUNT+1
                LDA     #DMA_DEMAND_OUT ; memory to the card, no interrupt
                STA     DMA1_MODE
                LDA     #VIDEO_PCTL_DRQ ; to the card, stepping up
                STA     VIDEO_PCTL
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
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
@done:          PLA
                STA     DMA_CONTROL
                STZ     VIDEO_PCTL
                LDA     #DMA_CH1
                STA     DMA_STATUS     ; its terminal count cleared
                RTS

TILES:          .incbin "demo/tiles.bin"
MAP:            .incbin "demo/map.bin"
BALL:           .incbin "demo/ball.bin"
PALETTES:       .incbin "demo/palettes.bin"
SINE:           .incbin "demo/sine.bin"
