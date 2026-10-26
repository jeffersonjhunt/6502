; The SD card (hw/design.md, "Mass storage registers"), from the monitor:
;
;   C809R  the card: its name and size, then its root directory, a line an
;          entry: the name, then "/" for a directory or its size in hex.
;   C80CR  boot: /BOOT.BIN mounted as unit 1, read to $1000 by DMA channel 0
;          (at most 88 sectors: $1000-$BFFF) and called there. A program
;          that returns comes back to the monitor.
;
; $C80F is ECHO, for a program loaded this way to print with: the character
; in A; A and X are kept.
;
; The card's sectors pass through a buffer at $0400-$05FF by CPU loop (the
; boot's by DMA). Each failure says what it was, then the monitor.

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO, PRBYTE
.export SDCAT, SDBOOT

BUF     = $0400
PTR     = SD_ZP                        ; $F0-$F1: into BUF, or a message
ENTRIES = SD_ZP + 2                    ; entries left in this sector
SECTORS = SD_ZP + 3                    ; BOOT.BIN's
LOAD    = $1000
MAX_SECTORS = 88                       ; $1000-$BFFF

.segment "SD"

QUIT:           JMP     WOZMON         ; within a branch of SDCAT's start

SDCAT:          JSR     START
                BCS     QUIT
                LDA     #SD_IDENTIFY
                STA     SD_CMD
                JSR     GETSECT
                BCS     QUIT
                LDX     #MCARD - MSGS  ; "CARD " and its name
                JSR     SAY
                LDX     #0
@name:          LDA     BUF + $10,X
                BEQ     @size
                JSR     ECHO
                INX
                CPX     #32
                BNE     @name
@size:          LDA     #' '
                JSR     ECHO
                LDX     #3             ; its sectors, in hex
@hex:           LDA     BUF,X
                JSR     PRBYTE
                DEX
                BPL     @hex
                LDX     #MSECTORS - MSGS
                JSR     SAY

                JSR     CLEARBUF       ; LIST "/"
                LDA     #'/'
                STA     BUF
                LDA     #SD_LIST
                STA     SD_CMD
                JSR     PUTBUF
@sector:        JSR     GETSECT
                BCS     @out
                STZ     PTR
                LDA     #>BUF
                STA     PTR+1
                LDA     #8
                STA     ENTRIES
@entry:         LDY     #8             ; the name
                LDA     (PTR),Y
                BEQ     @out           ; the zero entry: the end
@char:          LDA     (PTR),Y
                BEQ     @kind
                JSR     ECHO
                INY
                CPY     #64
                BNE     @char
@kind:          LDY     #4
                LDA     (PTR),Y
                AND     #$10           ; a directory
                BEQ     @file
                LDA     #'/'
                JSR     ECHO
                BRA     @next
@file:          LDA     #' '
                JSR     ECHO
                LDY     #3
@bytes:         LDA     (PTR),Y
                JSR     PRBYTE
                DEY
                BPL     @bytes
@next:          LDA     #$0D
                JSR     ECHO
                CLC
                LDA     PTR
                ADC     #64
                STA     PTR
                BCC     @more
                INC     PTR+1
@more:          DEC     ENTRIES
                BNE     @entry
                BRA     @sector
@out:           JMP     WOZMON

SDBOOT:         JSR     START
                BCS     @out
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BEQ     @dma
                LDX     #MNODMA - MSGS
                JSR     SAY
@out:           JMP     WOZMON
@dma:           JSR     CLEARBUF       ; MOUNT 1 "/BOOT.BIN"
                LDA     #1
                STA     BUF
                LDX     #0
@path:          LDA     BOOTBIN,X
                STA     BUF + 2,X
                BEQ     @mount
                INX
                BRA     @path
@mount:         LDA     #SD_MOUNT
                STA     SD_CMD
                JSR     PUTBUF
                JSR     WAITDONE
                BCS     @out
                LDA     #SD_IDENTIFY   ; its size, from the units table
                STA     SD_CMD
                JSR     GETSECT
                BCS     @out
                LDA     BUF + $41
                ORA     BUF + $42
                ORA     BUF + $43
                BNE     @big
                LDA     BUF + $40
                BEQ     @empty
                CMP     #MAX_SECTORS + 1
                BCC     @fits
@big:           LDX     #MBIG - MSGS
                BRA     @say
@empty:         LDX     #MEMPTY - MSGS
@say:           JSR     SAY
                BRA     @out
@fits:          STA     SECTORS

                STZ     DMA0_ADDR      ; channel 0: to $01000, a sector a 512
                LDA     #>LOAD
                STA     DMA0_ADDR + 1
                STZ     DMA0_ADDR + 2
                STZ     DMA0_COUNT
                LDA     SECTORS
                ASL
                STA     DMA0_COUNT + 1
                LDA     #DMA_DEMAND_IN
                STA     DMA0_MODE
                LDA     #DMA_ON
                STA     DMA_CONTROL
                LDA     #DMA_ENABLE
                STA     DMA0_CTRL

                LDA     #1             ; READ unit 1, from 0, SECTORS of it
                STA     SD_UNIT
                STZ     SD_LBA
                STZ     SD_LBA + 1
                STZ     SD_LBA + 2
                STZ     SD_LBA + 3
                LDA     SECTORS
                STA     SD_COUNT
                STZ     SD_COUNT + 1
                LDA     #SD_CTRL_DMA
                STA     SD_CTRL
                LDA     #SD_READ
                STA     SD_CMD
                JSR     WAITDONE
                STZ     SD_CTRL
                STZ     SD_UNIT
                BCS     @quit
                JSR     LOAD
@quit:          JMP     WOZMON

; Is there a card, started up? Carry set if not, having said so.
START:          LDA     SD_ID
                CMP     #SD_ID_V1
                BEQ     @wait
                LDX     #MNOCARD - MSGS
                BRA     @fail
@wait:          LDA     SD_STATUS
                BIT     #SD_PRESENT
                BEQ     @empty
                BIT     #SD_READY
                BEQ     @wait          ; starting up
                CLC
                RTS
@empty:         LDX     #MEMPTYSLOT - MSGS
@fail:          JSR     SAY
                SEC
                RTS

; Wait for a sector out of DATA, into BUF. Carry set if the command ended
; first: with an error, said.
GETSECT:        LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @read
                BIT     #SD_BUSY
                BNE     GETSECT
                BRA     FAILED
@read:          LDY     #0
@lo:            LDA     SD_DATA
                STA     BUF,Y
                INY
                BNE     @lo
@hi:            LDA     SD_DATA
                STA     BUF + $100,Y
                INY
                BNE     @hi
                CLC
                RTS

; Wait for the command to finish. Carry set if it failed, said.
WAITDONE:       LDA     SD_STATUS
                BIT     #SD_BUSY
                BNE     WAITDONE
                BIT     #SD_ERR
                BNE     FAILED
                CLC
                RTS

FAILED:         LDA     SD_ERROR
                CMP     #SD_NOT_FOUND
                BNE     @code
                LDX     #MNOTFOUND - MSGS
                BRA     @say
@code:          LDX     #MERROR - MSGS ; "SD ERROR nn"
                JSR     SAY
                LDA     SD_ERROR
                JSR     PRBYTE
                LDX     #MCR - MSGS
@say:           JSR     SAY
                SEC
                RTS

; BUF into DATA, a whole sector, once there is room.
PUTBUF:         LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BEQ     PUTBUF
                LDY     #0
@lo:            LDA     BUF,Y
                STA     SD_DATA
                INY
                BNE     @lo
@hi:            LDA     BUF + $100,Y
                STA     SD_DATA
                INY
                BNE     @hi
                RTS

CLEARBUF:       LDA     #0
                TAY
@clear:         STA     BUF,Y
                STA     BUF + $100,Y
                INY
                BNE     @clear
                RTS

; The message at MSGS + X, to its 0. Keeps nothing.
SAY:            LDA     MSGS,X
                BEQ     @done
                JSR     ECHO
                INX
                BRA     SAY
@done:          RTS

BOOTBIN:        .byte   "/BOOT.BIN", 0
MSGS:
MCARD:          .byte   $0D, "CARD ", 0
MSECTORS:       .byte   " SECTORS", $0D, 0
MNOCARD:        .byte   $0D, "NO SD CARD", $0D, 0
MEMPTYSLOT:     .byte   $0D, "NO CARD IN THE SLOT", $0D, 0
MNODMA:         .byte   $0D, "NO DMA", $0D, 0
MNOTFOUND:      .byte   $0D, "NOT ON THE CARD: /BOOT.BIN", $0D, 0
MERROR:         .byte   $0D, "SD ERROR ", 0
MBIG:           .byte   $0D, "BOOT.BIN IS OVER 44 KB", $0D, 0
MEMPTY:         .byte   $0D, "BOOT.BIN IS EMPTY", $0D, 0
MCR:            .byte   $0D, 0
