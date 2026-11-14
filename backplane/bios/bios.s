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
;   4. starts the boot splash and its sound (splash.s), if there are such
;      cards; they play by themselves for 5 s while the boot goes on,
;   5. turns on the video card's text screen (video.s) and the USB keyboard
;      (input.s), if there are such cards,
;   6. greets the terminal, and
;   7. boots the SD card's /BOOT.BIN once the splash is over, if the card has
;      one (sd.s, AUTOBOOT: a key during the splash is the monitor instead),
;      or else enters the Woz Monitor.
;
; $C800 is reset, $C803 the video demo, $C806 Apple II mode, $C809 the SD
; card's directory and $C80C a boot from it: C803R and so on from the
; monitor. $C80F is ECHO, for programs: it uses $E0-$E7 (backplane.inc).
; $C812 is the ROM's diagnostics (diag23.s, hw/diag.md's plan 2), after a Y;
; it never returns (the diagnostics end in a boot, or the monitor). $C815 is
; the same for a program that has asked already: A = $A5 and Y = $5A, X the
; options (bit 0 full, bit 1 interactive); anything else is $C812. With no
; diagnostics in the flash (or another build's) $C815 returns, carry set, with
; A, X, Y and W2 as they were.
;
; Before the copy, with the front panel there (its ID read twice): a
; breakpoint left on P3's own address (DG_P3ADDR) is turned off, and only
; that one, so a reset during the diagnostics' P3 can't halt the next boot
; while any other breakpoint still catches the boot; and with switch 19 up,
; and the diagnostics in the flash, the diagnostics' prompt (CONT or Y
; within 5 s) comes first. No RAM is used until BOOT.
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
.include "diag.inc"
.import WOZMON, ECHO, VINIT, VSCREEN, DEMO, KINIT, APPLE, SDCAT, SDBOOT, SPLASH, AUTOBOOT, GETKEY
.export BREAK, BOOT, MONBOOT

SRC   = $00                            ; copy pointers (Woz Monitor uses $24-$2B)
DST   = $02

BLOCK63 = 63                           ; RAM block 63: physical $FC000-$FFFFF
BLOCK2  = 2
BOOTMODE = $04                         ; 0 BOOT, 1 MONBOOT: kept in block 0 while the boot runs
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
                JMP     DIAG           ; $C812: the ROM's diagnostics, after a Y (diag23.s)
                JMP     DIAGPGM        ; $C815: the same for a program: A = $A5, Y = $5A, X the options

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
                LDA     CONSOLE_ID     ; the front panel? Read twice (hw/diag.md, "Card discovery")
                CMP     #CONSOLE_ID_V1
                BNE     BOOT
                LDA     CONSOLE_ID - $FF,X
                CMP     #CONSOLE_ID_V1
                BNE     BOOT
                LDA     CONSOLE_BPADDR ; P3's breakpoint, and only P3's, off
                CMP     #<DG_P3ADDR
                BNE     @switch
                LDA     CONSOLE_BPADDR + 1
                CMP     #>DG_P3ADDR
                BNE     @switch
                LDA     CONSOLE_BPADDR + 2
                CMP     #^DG_P3ADDR
                BNE     @switch
                STZ     CONSOLE_BPCTRL
@switch:        LDA     CONSOLE_SWR + 2
                AND     #$08           ; switch 19
                BEQ     BOOT
                LDA     #DG_B23        ; the diagnostics' signature, through W2
                STA     NB_WIN2
                LDX     #7
@sig:           LDA     $8000,X
                CMP     DIAGSIG,X
                BNE     @none
                DEX
                BPL     @sig
                LDA     $8009          ; and this build's: the diagnostics' version, the BIOS they were linked with
                CMP     #DG_VERSION
                BNE     @none
                LDA     DG_LINKED
                CMP     #BIOS_VMAJ
                BNE     @none
                LDA     DG_LINKED + 1
                CMP     #BIOS_VMIN
                BNE     @none
                LDX     #DE_PROMPT
                JMP     (DG_ENTRIES,X) ; the prompt: back to BOOT unless CONT or Y
@none:          LDA     #BLOCK2        ; (said after the greeting: DIAGNOTE)
                STA     NB_WIN2

; The boot (BOOT), or the copy, the greeting and the monitor with no splash
; and no SD boot (MONBOOT): the diagnostics' two ways back.
BOOT:           LDA     #0
                BRA     BOOT2
MONBOOT:        LDA     #1
BOOT2:          LDX     #$FF
                TXS
                STZ     NB_WIN0        ; as at a hard reset, for C800R
                STA     BOOTMODE
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
                LDA     BOOTMODE
                BNE     MONB
                JSR     SPLASH         ; the logo and its sound, 5 s on their own
                JSR     VINIT          ; the screen, so ECHO writes there too
                JSR     KINIT          ; the USB keyboard, if there is one (input.s)
                JSR     GREET
MONITOR:        JMP     AUTOBOOT       ; the SD card's /BOOT.BIN after the splash, or the monitor (sd.s)
MONB:           JSR     VINIT
                JSR     KINIT
                JSR     GREET
                JMP     WOZMON

GREET:          LDX     #$00
BANNER:         LDA     GREETING,X
                BEQ     DIAGNOTE
                JSR     ECHO
                INX
                BNE     BANNER

; With switch 19 up and no diagnostics in the flash, or another build's, say
; which (RESET couldn't: it has no RAM for ECHO).
DIAGNOTE:       LDA     CONSOLE_ID
                CMP     #CONSOLE_ID_V1
                BNE     @done
                LDA     CONSOLE_SWR + 2
                AND     #$08
                BEQ     @done
                JSR     CHKSIG
                BCC     @done
                JMP     NODIAG
@done:          RTS

; What CHKSIG found, said: X 0 no diagnostics in the flash, else another
; build's, which the flash must be written again to fix (review NN4).
NODIAG:         TXA
                BNE     @other
                LDX     #MNODIAG - DMSGS
                JMP     DSAY
@other:         LDX     #MOTHER - DMSGS
                JMP     DSAY

; Carry clear if flash block 23 holds this build's diagnostics (the signature,
; DG_VERSION and the BIOS they were linked with), read through W2, which is
; put back as it was. Carry set: X 0 no signature, 1 another build's. A kept.
CHKSIG:         PHA
                LDA     NB_WIN2
                PHA
                LDA     #DG_B23
                STA     NB_WIN2
                LDX     #7
@sig:           LDA     $8000,X
                CMP     DIAGSIG,X
                BNE     @no
                DEX
                BPL     @sig
                LDX     #1             ; the signature's: from here, another build's
                LDA     $8009
                CMP     #DG_VERSION
                BNE     @other
                LDA     DG_LINKED
                CMP     #BIOS_VMAJ
                BNE     @other
                LDA     DG_LINKED + 1
                CMP     #BIOS_VMIN
                BNE     @other
                PLA
                STA     NB_WIN2
                PLA
                CLC
                RTS
@no:            LDX     #0
@other:         PLA
                STA     NB_WIN2
                PLA
                SEC
                RTS

; $C812: the diagnostics from the monitor, after a Y, since they overwrite
; all of memory.
DIAG:           STZ     NB_WAIT3       ; no program's options (WAIT3 is the diagnostics')
                LDX     #MALL - DMSGS
                JSR     DSAY
                JSR     GETKEY
                JSR     ECHO
                AND     #$DF           ; y or Y
                CMP     #'Y'
                BEQ     DIAGGO
                LDA     #$0D
                JSR     ECHO
                JMP     WOZMON
DIAGGO:         JSR     CHKSIG
                BCS     @none
                SEI
                LDA     #DG_B23
                STA     NB_WIN2
                LDX     #DE_GO
                JMP     (DG_ENTRIES,X)
@none:          STZ     NB_WAIT3       ; no options left behind
                JSR     NODIAG
                JMP     WOZMON

; $C815: a program's, which has asked already: A = $A5 and Y = $5A (two bytes,
; so the monitor's R can't match them by chance), X the options. Its options go
; to the core in WAIT3 (diag.inc): bit 0 a program's, bits 6 and 7 X's.
DIAGPGM:        CMP     #$A5
                BNE     DIAG
                CPY     #$5A
                BNE     DIAG
                PHX
                JSR     CHKSIG
                PLX
                BCC     @there
                RTS                    ; none: back to the program, carry set; A, X, Y and W2 as they were
@there:         TXA
                AND     #$03
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     #DW_PROGRAM
                STA     NB_WAIT3
                JMP     DIAGGO

; The message at DMSGS + X, ended by 0.
DSAY:           LDA     DMSGS,X
                BEQ     @done
                JSR     ECHO
                INX
                BNE     DSAY
@done:          RTS

DIAGSIG:        .byte   "OOHWDIAG"
DMSGS:
MALL:           .byte   $0D, "ALL OF MEMORY WILL BE OVERWRITTEN. Y TO GO ON ", 0
MNODIAG:        .byte   $0D, "NO DIAGNOSTICS IN THE FLASH", $0D, 0
MOTHER:         .byte   $0D, "THE DIAGNOSTICS IN THE FLASH ARE ANOTHER BUILD'S: WRITE THE FLASH AGAIN", $0D, 0

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
; command, the boot splash and the video port's DMA request, the sound card's 6510 and SIDs (and from revision
; 2, its PCM stream and ES5503); and turn off the interrupts that
; IRQ can't answer, so a program run with R can CLI: the clock's tick too
; (its time runs on), a front-panel key already pending, and the VIA's. What
; it doesn't know (a program's own state) is left alone; the serial card the
; monitor's entry (WOZMON) programs afresh. The serial card always has the VIA
; at $C340; only an emulator machine without its via6522 line has the stand-in
; 6551's mirror there, which takes VIA_IER's write until WOZMON undoes it.
QUIET:
                LDA     #NB_RTC_TICK   ; TICK cleared, TIE off; SET 0 sends nothing
                STA     NB_RTCCTL
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
                CMP     #VIDEO_ID_V2
                BNE     @sound
                STZ     VIDEO_IEN
                STZ     VIDEO_SPLASH   ; the boot splash, if it's still on
                STZ     VIDEO_PCTL     ; the port's DRQ off (channel 1 is stopped)
@sound:         LDA     SOUND_ID
                AND     #$F0           ; any revision: each has CTRL, IEN and the SIDs
                CMP     #SOUND_TYPE
                BNE     @panel
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN   ; the 6510 stopped, so the SIDs take writes
                STA     SOUND_CTRL
                STZ     SOUND_IEN
                LDX     #SOUND_SIDS - 1
@sid:           STZ     SOUND,X        ; every SID register: gates and volume off
                DEX
                BPL     @sid
                LDA     SOUND_ID
                CMP     #SOUND_ID_V2   ; revision 2 and on: the PCM stream and the ES5503 too
                BCC     @panel
                LDA     #SOUND_PCM_FLUSH
                STA     SOUND_PCMCTL   ; RUN, PAUSE, EOF off and the FIFO emptied: the stream idle
                LDA     SOUND_DOCCTL
                AND     #<~SOUND_DOC_RUN ; the ES5503 frozen where it was; its route, AUTOINC, RAM kept
                STA     SOUND_DOCCTL
@panel:         LDA     CONSOLE_ID
                CMP     #CONSOLE_ID_V1
                BNE     @via
                STZ     CONSOLE_IEN
                LDA     #1             ; a key already pending pulls /IRQ whatever IEN says
                STA     CONSOLE_ISR
@via:           LDA     #$7F           ; the VIA's interrupts off, all seven (a program's timer)
                STA     VIA_IER
                RTS

GREETING:       .byte   $0D, "BACKPLANE BIOS ", BIOS_VERSION, $0D, 0 ; (diag.inc's BIOS_VMAJ, BIOS_VMIN)

.segment "DTRAMP"                     ; the diagnostics' ways back: fixed, under the vectors
TBOOT:          JMP     BOOT
TMON:           JMP     MONBOOT
.assert TBOOT = DG_TBOOT, lderror, "the diagnostics' BOOT trampoline moved"
.assert TMON = DG_TMON, lderror, "the diagnostics' MONBOOT trampoline moved"

.segment "VECTORS"
                .word   BREAK          ; NMI
                .word   ENTRY          ; RESET, through $C800
                .word   IRQ            ; IRQ/BRK
