; The BIOS's CRT device (bios.s): the video card's 80x30 text screen, after
; the ROM BIOS's (backplane/bios/video.s; input.s for the keyboard, which is
; in bios.s), with a VT52 subset (hw/oodos.md, "BIOS"):
;
;   CR, LF, BS        column 0; down a line, scrolling at the bottom; back one
;   BEL               nothing (no bell yet)
;   ESC A, B, C, D    the cursor up, down, right, left, stopping at the edges
;   ESC H             home
;   ESC J, ESC K      erase to the end of the screen, of the line
;   ESC E             clear the screen and home
;   ESC Y row col     move to row - 32, col - 32 (past the edge: the last)
;
; Other control characters and DEL do nothing; anything else is shown, as it
; is: lowercase stays lowercase. A character in the last column wraps.
;
; Video memory, in the card's 1 KB units of banks 8-B: the palette at unit
; 0, the font at unit 1 (copied from flash block 30, as VINIT does), the text
; at unit 8 ($82000), a character and an attribute a cell. The BIOS reaches
; it through W1, put on block 32 for the call and back on the BDOS's (60)
; before it returns; scrolling and erasing are DMA copies, as video.s's.
; The cursor is the cell's attribute while it is there: black on light
; grey, blinking (the card blinks it), the character left as it is.
;
; With no video card or no DMA controller CRTINIT leaves CVOK clear and CRTOUT
; does nothing.

.setcpu "65C02"
.include "sys.inc"
.import DMAGO
.importzp SRCP, DSTP, DCOUNT
.export CRTINIT, CRTOUT

COLS            = 80
ROWS            = 30
ATTR            = $07                  ; light grey on black
CURATTR         = $F0                  ; black on light grey, blinking
VBLOCK          = 32                   ; $80000, through W1
VWIN            = $4000
PAL             = VWIN                 ; unit 0
FONT            = VWIN + $0400         ; unit 1
TEXT            = VWIN + $2000         ; unit 8
TEXT_PHYS       = $82000
FONT_BLOCK      = 30                   ; in the flash
FONT_UNIT       = 1
TEXT_UNIT       = 8
TEXT_END        = TEXT + COLS * ROWS * 2

.zeropage
CVOK:           .res    1              ; bit 7: the screen is there
CCOL:           .res    1
CROW:           .res    1
CESC:           .res    1              ; 0, 1 after ESC, 2 after ESC Y, 3 after its row
CUNDER:         .res    1              ; the attribute under the cursor
CP:             .res    2              ; a cell, in W1

.segment "BIOS"

; Write A. Uses A, X, Y.
CRTOUT:         BIT     CVOK
                BPL     @off
                PHA
                LDA     #VBLOCK
                STA     NB_WIN1
                JSR     CUROFF
                PLA
                JSR     CRTCH
                JSR     CURON
                LDA     #BDOS_BLOCK
                STA     NB_WIN1
@off:           RTS

CRTCH:          LDX     CESC
                BNE     ESCAPE
                CMP     #' '
                BCS     PRINT
                CMP     #$0D
                BEQ     CR
                CMP     #$0A
                BEQ     NEWLINE
                CMP     #$08
                BEQ     LEFT
                CMP     #$1B
                BNE     NONE
                LDA     #1
                STA     CESC
NONE:           RTS

CR:             STZ     CCOL
                RTS

PRINT:          CMP     #$7F
                BEQ     NONE
                PHA
                JSR     CELL
                PLA
                STA     (CP)
                LDY     #1
                LDA     #ATTR
                STA     (CP),Y
                INC     CCOL
                LDA     CCOL
                CMP     #COLS
                BCC     NONE
                STZ     CCOL
                ; fall through

; Down a line; at the bottom, rows 1-29 up a row and the last row blank.
NEWLINE:        LDA     CROW
                INC     A
                CMP     #ROWS
                BCS     @scroll
                STA     CROW
                RTS
@scroll:        LDA     #<(TEXT_PHYS + COLS * 2)
                STA     SRCP
                LDA     #>(TEXT_PHYS + COLS * 2)
                STA     SRCP + 1
                LDA     #<TEXT_PHYS
                STA     DSTP
                LDA     #>TEXT_PHYS
                STA     DSTP + 1
                LDA     #^TEXT_PHYS
                STA     SRCP + 2
                STA     DSTP + 2
                LDA     #<(COLS * (ROWS - 1) * 2)
                STA     DCOUNT
                LDA     #>(COLS * (ROWS - 1) * 2)
                STA     DCOUNT + 1
                JSR     DMAGO
                LDA     CCOL           ; the new last row, from its start
                PHA
                STZ     CCOL
                JSR     EOL
                PLA
                STA     CCOL
                RTS

LEFT:           LDA     CCOL
                BEQ     NONE
                DEC     CCOL
                RTS

ESCAPE:         CPX     #1
                BNE     @arg
                STZ     CESC
                CMP     #'Y'
                BNE     @letter
                LDA     #2
                STA     CESC
                RTS
@letter:        LDX     #ESCKEYS_LEN - 1
@find:          CMP     ESCKEYS,X
                BEQ     @go
                DEX
                BPL     @find
                RTS                    ; not one of ours
@go:            TXA
                ASL     A
                TAX
                JMP     (ESCJUMPS,X)
@arg:           SEC                    ; ESC Y's row, then its column
                SBC     #32
                CPX     #2
                BNE     @col
                CMP     #ROWS
                BCC     @row
                LDA     #ROWS - 1
@row:           STA     CROW
                LDA     #3
                STA     CESC
                RTS
@col:           CMP     #COLS
                BCC     @set
                LDA     #COLS - 1
@set:           STA     CCOL
                STZ     CESC
                RTS

ESCKEYS:        .byte   "ABCDHJKE"
ESCKEYS_LEN     = * - ESCKEYS
ESCJUMPS:       .word   UP, DOWN, RIGHT, LEFT, HOME, EOS, EOL, CLEAR

UP:             LDA     CROW
                BEQ     NONE2
                DEC     CROW
NONE2:          RTS
DOWN:           LDA     CROW
                CMP     #ROWS - 1
                BCS     NONE2
                INC     CROW
                RTS
RIGHT:          LDA     CCOL
                CMP     #COLS - 1
                BCS     NONE2
                INC     CCOL
                RTS
CLEAR:          JSR     HOME
                ; fall through
EOS:            JSR     CELL           ; (cells to the end - 1) x 2 = TEXT_END - 2 - CP
                SEC
                LDA     #<(TEXT_END - 2)
                SBC     CP
                STA     DCOUNT
                LDA     #>(TEXT_END - 2)
                SBC     CP + 1
                STA     DCOUNT + 1
                BRA     FILL
HOME:           STZ     CROW
                STZ     CCOL
                RTS
EOL:            JSR     CELL           ; (79 - column) x 2
                SEC
                LDA     #COLS - 1
                SBC     CCOL
                ASL     A
                STA     DCOUNT
                STZ     DCOUNT + 1
                ; fall through

; A blank at CP, then copied DCOUNT bytes on by an overlapping DMA copy,
; which repeats it.
FILL:           LDA     #' '
                STA     (CP)
                LDY     #1
                LDA     #ATTR
                STA     (CP),Y
                LDA     DCOUNT
                ORA     DCOUNT + 1
                BEQ     @done
                LDA     CP
                STA     SRCP
                CLC
                ADC     #2
                STA     DSTP
                LDA     CP + 1
                AND     #$3F
                STA     SRCP + 1
                ADC     #0
                STA     DSTP + 1
                LDA     #^TEXT_PHYS
                STA     SRCP + 2
                STA     DSTP + 2
                JMP     DMAGO
@done:          RTS

; CP = the cursor's cell, the cursor first kept on the screen.
CELL:           LDY     CROW
                CPY     #ROWS
                BCC     @row
                LDY     #ROWS - 1
                STY     CROW
@row:           LDA     CCOL
                CMP     #COLS
                BCC     @col
                LDA     #COLS - 1
                STA     CCOL
@col:           ASL     A              ; at most 158: no carry
                ADC     ROWLO,Y
                STA     CP
                LDA     ROWHI,Y
                ADC     #0
                STA     CP + 1
                RTS

CURON:          JSR     CELL
                LDY     #1
                LDA     (CP),Y
                STA     CUNDER
                LDA     #CURATTR
                STA     (CP),Y
                RTS
CUROFF:         JSR     CELL
                LDY     #1
                LDA     CUNDER
                STA     (CP),Y
                RTS

ROWLO:
.repeat ROWS, r
                .byte   <(TEXT + r * COLS * 2)
.endrepeat
ROWHI:
.repeat ROWS, r
                .byte   >(TEXT + r * COLS * 2)
.endrepeat

; The screen, from nothing: the font (flash block 30, a page at a time through
; SECBUF, since a window can't show both), the palette, a clear screen and the
; card's registers for it; and the USB keyboard's Pause/Break on /NMI.
CRTINIT:        STZ     CVOK
                STZ     CESC
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V1
                BNE     @nocard
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BEQ     @card
@nocard:        JMP     USBINIT
@card:          LDX     #0             ; the page
@font:          LDA     #NB_ROM | NB_RO | FONT_BLOCK
                STA     NB_WIN1
                STZ     CP
                TXA
                CLC
                ADC     #>VWIN
                STA     CP + 1
                LDY     #0
@in:            LDA     (CP),Y
                STA     SECBUF,Y
                INY
                BNE     @in
                LDA     #VBLOCK
                STA     NB_WIN1
                TXA
                CLC
                ADC     #>FONT
                STA     CP + 1
@out:           LDA     SECBUF,Y
                STA     (CP),Y
                INY
                BNE     @out
                INX
                CPX     #16
                BNE     @font
                LDX     #31            ; the palette
@pal:           LDA     CGA,X
                STA     PAL,X
                DEX
                BPL     @pal
                STZ     VIDEO_IEN      ; the card: layer 0, text, unscrolled
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
                JSR     CLEAR
                JSR     CURON
                LDA     #BDOS_BLOCK
                STA     NB_WIN1
                DEC     CVOK
USBINIT:        LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @done
                STZ     USB_IEN
                LDA     #USB_NMI_PAUSE ; Pause/Break: the monitor
                STA     USB_KBCFG
@done:          RTS

; The CGA's 16 colours, 12-bit: GGGGBBBB, then 0000RRRR (video.s's).
.macro RGB r, g, b
                .byte   (g << 4) | b, r
.endmacro
CGA:            RGB     $0, $0, $0
                RGB     $0, $0, $A
                RGB     $0, $A, $0
                RGB     $0, $A, $A
                RGB     $A, $0, $0
                RGB     $A, $0, $A
                RGB     $A, $5, $0
                RGB     $A, $A, $A
                RGB     $5, $5, $5
                RGB     $5, $5, $F
                RGB     $5, $F, $5
                RGB     $5, $F, $F
                RGB     $F, $5, $5
                RGB     $F, $5, $F
                RGB     $F, $F, $5
                RGB     $F, $F, $F
