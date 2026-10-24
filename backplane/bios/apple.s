; Apple II mode (hw/design.md, "Apple II Mode"), from the monitor: C806R.
;
; The owner's Apple IIe ROM is in the cartridge (ROM B, flash block 32 on):
; 16 KB as it sits at $C000-$FFFF. This checks it is a IIe's ($FBB3 = $06),
; copies it into bank E at $EC000 (RAM block 59, where Apple mode reads its
; ROM from), clears banks 0-3 (the IIe's main and auxiliary memory and the
; language card) and writes MODE: the north bridge switches to the Apple
; map and clock and pulses /RESET, and the CPU starts at the IIe ROM's
; reset vector, as a IIe does at power-on. Only a hard reset comes back.
;
; The clear wipes this code's stack and zero page (bank 0), so it is the last
; thing done, inline: no subroutine returns after it. It is four 64 KB DMA
; fills from a zero byte held as the source, about 75 ms.
;
; With no IIe ROM in the cartridge, or no DMA controller, it says so and
; goes back to the monitor.

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO
.export APPLE

CART      = NB_ROM | NB_RO | 32        ; the cartridge's first 16 KB, through W1
CART_CPU  = $4000
IIE_BLOCK = 59                         ; RAM block 59: $EC000-$EFFFF, bank E's $C000-$FFFF
ZERO_BLOCK = 58                        ; $E8000-$EBFFF: the fill's zero byte at its top
ZERO_PHYS = $EBFFF
SRC       = $00                        ; copy pointers (as the boot copy uses them)
DST       = $02

.segment "APPLE"

APPLE:
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BEQ     @dma
                JMP     NODMA_SAY
@dma:           LDA     #CART
                STA     NB_WIN1
                LDA     CART_CPU + $3BB3 ; $FBB3 in the image: $06 on a IIe
                CMP     #$06
                BEQ     @rom
                JMP     NOROM_SAY
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

                SEI
                LDA     #ZERO_BLOCK    ; the fill's source: one zero byte
                STA     NB_WIN2
                STZ     $BFFF
                LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2

                ; banks 0-3 by DMA, inline from here: the first fill takes the stack
                LDA     #DMA_ON
                STA     DMA_CONTROL
                LDA     #<ZERO_PHYS
                STA     DMA3_ADDR
                LDA     #>ZERO_PHYS
                STA     DMA3_ADDR+1
                LDA     #^ZERO_PHYS
                STA     DMA3_ADDR+2
.repeat 4, bank
                STZ     DMA3_DEST
                STZ     DMA3_DEST+1
                LDA     #bank
                STA     DMA3_DEST+2
                STZ     DMA3_COUNT     ; 0: 65,536 bytes
                STZ     DMA3_COUNT+1
                LDA     #DMA_M2M | DMA_HOLD
                STA     DMA3_MODE
                LDA     #DMA_GO
                STA     DMA3_CTRL      ; it runs before the next instruction
.endrepeat
                LDA     #NB_MODE_APPLE
                STA     NB_MODE        ; Apple II mode: the north bridge pulses /RESET
@reset:         BRA     @reset         ; ...which takes the CPU from here

NODMA_SAY:      LDX     #NODMA - MESSAGES
                BRA     SAY
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
NOROM:          .byte   $0D, "NO APPLE IIE ROM IN THE CARTRIDGE", 0
NODMA:          .byte   $0D, "APPLE II MODE NEEDS THE DMA CONTROLLER", 0
