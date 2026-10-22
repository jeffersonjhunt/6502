; Minimal BIOS for the backplane computer (hw/design.md, "Boot").
;
; At reset the north bridge maps the top block of the flash (ROM A block 31)
; at $C000-$FFFF, read-only, through W3 and VEC; $C000-$C7FF of it is hidden
; by the I/O hole, so the BIOS lives at $C800-$FFFF. It:
;
;   1. copies itself into RAM block 63 (the top of bank F) through W2,
;   2. points W3 and VEC at the copy, read-only, and carries on in RAM with no
;      jump, because the copy sits at the same CPU addresses,
;   3. puts W2 back on RAM block 2, so the map is RAM blocks 0, 1, 2 and 63,
;   4. greets the terminal and enters the Woz Monitor.
;
; When the bus spy has loaded RAM and held /BOOTSEL, the reset map already
; has W3 on RAM block 63; the copy then copies the RAM onto itself, which is
; harmless, so one image serves both boots.
;
; NMI enters the monitor (the USB card's Pause/Break can pull /NMI); IRQ
; returns at once until there is something to interrupt.

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO

SRC   = $00                            ; copy pointers (Woz Monitor uses $24-$2B)
DST   = $02

BLOCK63 = 63                           ; RAM block 63: physical $FC000-$FFFFF
BLOCK2  = 2

.segment "CODE"

RESET:
                SEI
                CLD
                LDX     #$FF
                TXS

; Copy $C800-$FFFF (W3, the flash) to $8800-$BFFF (W2, RAM block 63).
                LDA     #BLOCK63
                STA     NB_WIN2
                STZ     SRC
                STZ     DST
                LDA     #$C8
                STA     SRC+1
                LDA     #$88
                STA     DST+1
                LDY     #$00
COPY:           LDA     (SRC),Y
                STA     (DST),Y
                INY
                BNE     COPY
                INC     DST+1
                INC     SRC+1          ; $FF -> $00 after the last page
                BNE     COPY

                LDA     #NB_RO | BLOCK63
                STA     NB_WIN3        ; from here on, running the RAM copy
                STA     NB_VEC
                LDA     #BLOCK2
                STA     NB_WIN2

                LDA     #$1F           ; 8-N-1, 19200 baud
                STA     ACIA_CTRL
                LDA     #$0B           ; no parity, no echo, no interrupts
                STA     ACIA_CMD
                LDX     #$00
BANNER:         LDA     GREETING,X
                BEQ     MONITOR
                JSR     ECHO
                INX
                BNE     BANNER
MONITOR:        JMP     WOZMON

IRQ:            RTI

GREETING:       .byte   $0D, "BACKPLANE BIOS 0.1", $0D, 0

.segment "VECTORS"
                .word   WOZMON         ; NMI
                .word   RESET          ; RESET
                .word   IRQ            ; IRQ/BRK
