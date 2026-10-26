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
;   4. turns on the video card's text screen (video.s) and the USB keyboard
;      (input.s), if there are such cards,
;   5. greets the terminal and enters the Woz Monitor.
;
; $C800 is reset, $C803 the video demo, $C806 Apple II mode, $C809 the SD
; card's directory and $C80C a boot from it: C803R and so on from the
; monitor. $C80F is ECHO, for programs: it uses $E0-$E7 (backplane.inc).
;
; C800R goes back to the flash first and starts from its reset vector, as a
; hard reset does, so it copies the flash again rather than the RAM copy it
; is running (unless the machine booted from RAM, whose image is the only one).
;
; When the bus spy has loaded RAM and held /BOOTSEL, the reset map already
; has W3 on RAM block 63; the copy then copies the RAM onto itself, which is
; harmless, so one image serves both boots.
;
; NMI (the USB card's Pause/Break can pull /NMI) and BRK enter the monitor
; with the windows and the text screen as the BIOS keeps them, the DMA
; stopped and the cards it knows quiet (QUIET), whatever was running; other
; IRQs return at once until there is something to interrupt.

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO, VINIT, VSCREEN, DEMO, KINIT, APPLE, SDCAT, SDBOOT
.export BREAK

SRC   = $00                            ; copy pointers (Woz Monitor uses $24-$2B)
DST   = $02

BLOCK63 = 63                           ; RAM block 63: physical $FC000-$FFFFF
BLOCK2  = 2
FLASH31 = NB_ROM | NB_RO | 31          ; the flash's top block, where reset maps W3

.segment "CODE"

; Entry points at fixed addresses, for the monitor (nnnnR) and programs:
; eight of them, so a new one does not move the code after them.
ENTRY:          JMP     START          ; $C800: reset, as the vector does
                JMP     DEMO           ; $C803: the video demo (video.s, demo.s)
                JMP     APPLE          ; $C806: Apple II mode (apple.s)
                JMP     SDCAT          ; $C809: the SD card and its root directory (sd.s)
                JMP     SDBOOT         ; $C80C: boot /BOOT.BIN from the SD card (sd.s)
                JMP     ECHO           ; $C80F: print A, for programs (keeps A and X)
.repeat 2
                JMP     WOZMON         ; $C812-$C815: spare
.endrepeat

; A hard reset arrives on the flash; C800R, on the RAM copy. Put W3 and VEC
; back on the flash from zero page, so the next fetch is the flash's own
; reset vector whatever BIOS the flash holds now.
START:
                SEI
                CLD
                BIT     NB_WIN3
                BMI     RESET          ; running the flash already
                LDA     NB_STATUS
                AND     #NB_STATUS_BOOTSEL
                BNE     RESET          ; booted from RAM: no flash to go back to
                STZ     NB_WIN0
                LDX     #STUB_END - STUB - 1
@stub:          LDA     STUB,X
                STA     $00,X
                DEX
                BPL     @stub
                LDA     #FLASH31
                JMP     $0000
STUB:           STA     NB_WIN3        ; an NMI between these two takes the RAM copy's
                STA     NB_VEC         ; vector into the flash's code: accepted, one instruction
                JMP     ($FFFC)
STUB_END:

RESET:
                LDX     #$FF
                TXS
                STZ     NB_WIN0        ; as at a hard reset, for C800R
                LDA     #1
                STA     NB_WIN1

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
                JSR     VINIT          ; the screen, so ECHO writes there too
                JSR     KINIT          ; the USB keyboard, if there is one (input.s)
                LDX     #$00
BANNER:         LDA     GREETING,X
                BEQ     MONITOR
                JSR     ECHO
                INX
                BNE     BANNER
MONITOR:        JMP     WOZMON

; Break (NMI), BRK and the demo's end: a fresh stack, windows 0-2 on RAM
; blocks 0-2 (W3 and VEC are the BIOS's), the cards quiet, the text screen
; (VSCREEN finds it from the ID bytes), then the monitor.
BREAK:
                CLD
                LDX     #$FF
                TXS
                STZ     NB_WIN0
                LDA     #1
                STA     NB_WIN1
                LDA     #BLOCK2
                STA     NB_WIN2
                JSR     QUIET
                JSR     VSCREEN
                JMP     WOZMON

; BRK is the monitor; a real IRQ just returns.
IRQ:            PHA
                PHX
                TSX
                LDA     $0103,X        ; the P it pushed
                AND     #$10           ; B: a BRK
                BNE     BREAK
                PLX
                PLA
                RTI

; Stop what would go on behind the monitor: every DMA channel, the SD card's
; command, the sound card's 6510 and SIDs; and turn off the interrupts that
; IRQ can't answer, so a program run with R can CLI. What it doesn't know
; (the RTC, a program's own state) is left alone; the serial card the
; monitor's entry (WOZMON) programs afresh.
QUIET:
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @sd
                LDA     #DMA_ALL
                STA     DMA_ABORT
                STA     DMA_STATUS     ; no terminal count left to interrupt
                STZ     DMA0_MODE      ; nor one to come: MODE's IRQ bit
                STZ     DMA0_MODE + $10
                STZ     DMA0_MODE + $20
                STZ     DMA0_MODE + $30
@sd:            LDA     SD_ID
                CMP     #SD_ID_V1
                BNE     @usb
                STZ     SD_IEN
                LDA     SD_STATUS
                BPL     @idle          ; BUSY clear
                LDA     #SD_CTRL_RESET ; abandon the command; units stay mounted
                STA     SD_CTRL
                BRA     @usb
@idle:          STZ     SD_CTRL        ; DRQ0 off
@usb:           LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @video
                STZ     USB_IEN
@video:         LDA     VIDEO_ID
                CMP     #VIDEO_ID_V1
                BNE     @sound
                STZ     VIDEO_IEN
@sound:         LDA     SOUND_ID
                CMP     #SOUND_ID_V1
                BNE     @panel
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN   ; the 6510 stopped, so the SIDs take writes
                STA     SOUND_CTRL
                STZ     SOUND_IEN
                LDX     #SOUND_SIDS - 1
@sid:           STZ     SOUND,X        ; every SID register: gates and volume off
                DEX
                BPL     @sid
@panel:         LDA     CONSOLE_ID
                CMP     #CONSOLE_ID_V1
                BNE     @done
                STZ     CONSOLE_IEN
@done:          RTS

GREETING:       .byte   $0D, "BACKPLANE BIOS 0.9", $0D, 0

.segment "VECTORS"
                .word   BREAK          ; NMI
                .word   ENTRY          ; RESET, through $C800
                .word   IRQ            ; IRQ/BRK
