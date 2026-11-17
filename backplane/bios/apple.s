; Apple II mode (hw/design.md, "Apple II Mode"), from the monitor: C806R,
; the first good image; or image A through the dispatching entry, $C818's
; function $A1 (bios.s), or its menu's 0-3 (APPLEN).
;
; The owner's Apple IIe ROMs are in ROM A (hw/design.md, "ROM A's layout"):
; image n in flash block n (0-3, $F00000 + n x $4000), 16 KB as it sits at
; $C000-$FFFF, and its character set n at $F10000 + n x $2000 (blocks 4-5).
; FINDIMG takes the first good image ($FBB3 = $06, a IIe's), copies it into
; bank E at $EC000 (RAM block 59, where Apple mode reads its ROM from),
; clears banks 0-3 (the IIe's main and auxiliary memory and the language
; card) and $D000-$DFFF of banks 4-5 (the language card's second $D000
; bank) and writes MODE: the north bridge switches to the Apple map and
; clock and pulses /RESET, and the CPU starts at the IIe ROM's reset vector,
; as a IIe does at power-on. Only a hard reset comes back.
;
; The clear wipes this code's stack and zero page (bank 0), so it is the last
; thing done, inline: no subroutine returns after it. It is four 64 KB DMA
; fills from a zero byte held as the source, and two of 4 KB, about 75 ms.
; Every channel is stopped first and CONTROL set to no burst limit, SELECT 3
; (Apple mode holds the controller idle, and /RESET puts CONTROL back, so a
; program's CONTROL is not kept); each fill is then a block transfer that is
; done before the next instruction, with no wait needed.
;
; The IIe's character ROM goes into the video card's memory too (CHARS),
; through its port, from the image's set n or, with that set erased, made
; from the VGA font.
;
; With no revision 2 video card (whose ID is $92: the BIOS no longer drives
; revision 1, and CHARS writes only revision 2's port), no DMA controller,
; or no IIe ROM in any of the four images (in image A, for APPLEN), it says
; so and goes back to the monitor, still in the native map, with nothing
; changed.

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO
.export APPLE, APPLEN

IMAGES    = NB_ROM | NB_RO             ; image n: ROM A block n, read-only, through W1
CART_CPU  = $4000                      ; where W1 shows it
IIE_BLOCK = 59                         ; RAM block 59: $EC000-$EFFFF, bank E's $C000-$FFFF
ZERO_BLOCK = 58                        ; $E8000-$EBFFF: the fill's zero byte at its top
ZERO_PHYS = $EBFFF
SRC       = $00                        ; copy pointers (as the boot copy uses them)
DST       = $02
CODE      = $04                        ; CHARS's
TMP       = $05
FPTR      = $06
IMAGE     = $08                        ; the image FINDIMG took, 0-3 (and its set's)
SETS      = NB_ROM | NB_RO | IIE_CHARS_BLOCK ; sets 0-1 in block 4, 2-3 in block 5, 8 KB each
CHARS_CARD = $1E0000                   ; where the card draws Apple text from (hw/video-rev2.md)
VGA_FONT  = NB_ROM | NB_RO | 30        ; flash block 30: the BIOS's VGA font, at $4000 through W1
VGA_CPU   = $4000

.segment "APPLE"

APPLE:          LDA     #$FF           ; no image named: the first good one
APPLEN:         LDX     NB_ID          ; a $D2 bridge: W0-W2 below 1 MB, whatever a program left
                CPX     #NB_ID_V2
                BNE     @low
                STZ     NB_WIN0H
                STZ     NB_WIN1H
                STZ     NB_WIN2H
@low:           STA     IMAGE          ; image A, 0-3 ($C818's $A1, which checked it)
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V2
                BEQ     @card
                JMP     NOCARD_SAY
@card:          LDA     DMA_ID
                CMP     #DMA_ID_V2
                BEQ     @dma
                JMP     NODMA_SAY
@dma:           LDA     IMAGE
                BPL     @named
                JSR     FINDIMG        ; the first good image, in W1
                BCC     @rom
                JMP     NOROM_SAY
@named:         ORA     #IMAGES        ; image A, in W1: a IIe's?
                STA     NB_WIN1
                LDA     CART_CPU + IIE_ID_AT
                CMP     #IIE_ID
                BEQ     @rom
                JMP     NOROMN_SAY
@rom:

                ; the ROM into bank E: $4000-$7FFF (W1) to $8000-$BFFF (W2)
                LDA     #IIE_BLOCK
                STA     NB_WIN2
                STZ     SRC
                STZ     DST
                LDA     #>CART_CPU
                STA     SRC+1
                LDA     #$80
                STA     DST+1
                LDY     #0
@copy:          LDA     (SRC),Y
                STA     (DST),Y
                INY
                BNE     @copy
                INC     DST+1
                INC     SRC+1
                LDA     SRC+1
                CMP     #>(CART_CPU + $4000)
                BNE     @copy

                JSR     CHARS          ; the character ROM into the card's memory

                SEI
                LDA     #ZERO_BLOCK    ; the fill's source: one zero byte
                STA     NB_WIN2
                STZ     $BFFF
                LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2

                ; banks 0-3 by DMA, inline from here: the first fill takes the stack
                LDA     #DMA_ALL       ; a program's channels stopped, so the fills
                STA     DMA_ABORT      ; have the bus to themselves
                STA     DMA_STATUS
                LDA     #DMA_ON | DMA_SEL3 ; no burst limit, whatever it set, and SELECT 3:
                STA     DMA_CONTROL    ;   the window is channel 3's (/RESET puts SELECT 0 back)
                LDA     #<ZERO_PHYS
                STA     DMA_ADDR
                LDA     #>ZERO_PHYS
                STA     DMA_ADDR+1
                LDA     #^ZERO_PHYS
                STA     DMA_ADDR+2
.repeat 4, bank
                STZ     DMA_DEST
                STZ     DMA_DEST+1
                LDA     #bank
                STA     DMA_DEST+2
                STZ     DMA_COUNT      ; 0: 65,536 bytes
                STZ     DMA_COUNT+1
                LDA     #DMA_M2M | DMA_HOLD
                STA     DMA_MODE
                LDA     #DMA_GO
                STA     DMA_CTRL       ; it runs before the next instruction
.endrepeat
.repeat 2, bank                        ; the second $D000 bank, main then auxiliary
                STZ     DMA_DEST
                LDA     #$D0
                STA     DMA_DEST+1
                LDA     #4 + bank
                STA     DMA_DEST+2
                STZ     DMA_COUNT      ; 4 KB
                LDA     #$10
                STA     DMA_COUNT+1
                LDA     #DMA_M2M | DMA_HOLD
                STA     DMA_MODE
                LDA     #DMA_GO
                STA     DMA_CTRL
.endrepeat
                LDA     #NB_MODE_APPLE
                STA     NB_MODE        ; Apple II mode: the north bridge pulses /RESET
@reset:         BRA     @reset         ; ...which takes the CPU from here

; The first good image of the four: carry clear, IMAGE its number and W1 on
; it (read-only); carry set if none is, W1 then on image 3. Good is a IIe's,
; $06 at its $FBB3. Erased flash reads $FF there.
FINDIMG:
                STZ     IMAGE
@look:          LDA     IMAGE
                ORA     #IMAGES        ; image n is ROM A block n
                STA     NB_WIN1
                LDA     CART_CPU + IIE_ID_AT ; $FBB3 in the image: a IIe's?
                CMP     #IIE_ID
                BEQ     @good
                INC     IMAGE
                LDA     IMAGE
                CMP     #IIE_IMAGES
                BCC     @look
                RTS                    ; carry set: none
@good:          CLC
                RTS

; The IIe's character ROM, 2 KB, into the video card's memory at $1E0000,
; where it draws Apple text from (hw/video-rev2.md's layout), through its
; data port: PADDR there, PSTEP 1, no DMA, and each byte written to PDATA in
; turn. Card memory outlives the /RESET that enters the mode. From set
; IMAGE (ROM A's $10000 + IMAGE x $2000) if it isn't erased; else made from
; the BIOS's VGA font (flash block 30), so Apple text still shows: each glyph's 16 rows folded
; to 8 (two ORed), its 7 left dots reversed into the IIe's order (bit 0
; leftmost), and normal codes ($80 up) stored inverted, lit where 0, as the
; real ROM does.
CHARS:
                STZ     VIDEO_PCTL
                LDA     #1
                STA     VIDEO_PSTEP
                STZ     VIDEO_PSTEP + 1
                LDA     #<CHARS_CARD
                STA     VIDEO_PADDR
                LDA     #>CHARS_CARD
                STA     VIDEO_PADDR + 1
                LDA     #^CHARS_CARD
                STA     VIDEO_PADDR + 2
                LDA     IMAGE          ; set n: block 4 + n / 2, its half n & 1
                LSR     A              ; (carry the half)
                ORA     #SETS
                STA     NB_WIN1
                STZ     SRC
                LDA     #>CART_CPU     ; $4000 for an even set,
                BCC     @half
                LDA     #>(CART_CPU + $2000) ; $6000 for an odd one
@half:          STA     SRC+1
                LDY     #7
@probe:         LDA     (SRC),Y        ; erased flash: all $FF
                CMP     #$FF
                BNE     @copy
                DEY
                BPL     @probe
                BRA     @make
@copy:          LDX     #8             ; 2 KB
                LDY     #0
@page:          LDA     (SRC),Y
                STA     VIDEO_PDATA
                INY
                BNE     @page
                INC     SRC+1
                DEX
                BNE     @page
                BRA     @done

@make:          LDA     #VGA_FONT      ; the VGA font through W1: $4000
                STA     NB_WIN1
                STZ     CODE
@code:          LDA     CODE           ; the ASCII it shows: $00-$1F and $80-$9F are capitals
                AND     #$7F
                CMP     #$20
                BCS     @ascii
                ORA     #$40
@ascii:         STA     FPTR           ; FPTR = $4000 + ASCII x 16
                STZ     FPTR+1
                LDX     #4
@x16:           ASL     FPTR
                ROL     FPTR+1
                DEX
                BNE     @x16
                LDA     FPTR+1
                CLC
                ADC     #>VGA_CPU
                STA     FPTR+1
                LDX     #0             ; the glyph's 8 rows
@row:           TXA
                ASL     A
                TAY
                LDA     (FPTR),Y       ; two VGA rows, ORed
                INY
                ORA     (FPTR),Y
                STA     TMP
                LDY     #7
@rev:           ASL     TMP            ; VGA bit 7 (leftmost) becomes bit 0
                ROR     A
                DEY
                BNE     @rev
                LSR     A
                BIT     CODE
                BPL     @store         ; inverse and flashing codes: lit where 1
                EOR     #$FF           ; normal: lit where 0
@store:         STA     VIDEO_PDATA    ; code x 8 + row: the rows in turn, the codes in turn
                INX
                CPX     #8
                BNE     @row
                INC     CODE
                BNE     @code
@done:          LDA     #1
                STA     NB_WIN1
                RTS

NOCARD_SAY:     LDX     #NOCARD - MESSAGES
                BRA     SAY
NODMA_SAY:      LDX     #NODMA - MESSAGES
                BRA     SAY
NOROMN_SAY:     LDA     #1             ; "...IMAGE n"
                STA     NB_WIN1
                LDX     #NOROMN - MESSAGES
@say:           LDA     MESSAGES,X
                BEQ     @n
                JSR     ECHO
                INX
                BNE     @say
@n:             LDA     IMAGE
                ORA     #'0'
                JSR     ECHO
                JMP     WOZMON
NOROM_SAY:      LDA     #1
                STA     NB_WIN1
                LDX     #NOROM - MESSAGES
SAY:            LDA     MESSAGES,X
                BEQ     @done
                JSR     ECHO
                INX
                BNE     SAY
@done:          JMP     WOZMON

MESSAGES:
NOROM:          .byte   $0D, "NO APPLE IIE ROM IN FLASH IMAGES 0-3", 0
NOROMN:         .byte   $0D, "NO APPLE IIE ROM IN FLASH IMAGE ", 0
NODMA:          .byte   $0D, "APPLE II MODE NEEDS THE DMA CONTROLLER", 0
NOCARD:         .byte   $0D, "APPLE II MODE NEEDS THE REVISION 2 VIDEO CARD", 0
