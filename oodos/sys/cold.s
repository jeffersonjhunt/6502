; OODOS's cold boot (hw/oodos.md, "The system, phase 2"), in block 62: OODOS.SYS's
; entry. OOLDR jumps here with W0 on block 0, W1-W3 and VEC on 60, 61, 62
; and 62, interrupts off and D clear.
;
; It takes the system block (31) as W0 and its stack, clears the system's
; zero page and data, sets the SCB, starts the BIOS (the devices, the drives
; from the card's units table), reads the clock into the SCB, makes M:,
; says who it is with the time and what it found (STATUS), starts the clocks
; (irq.s: timer 1's tick, the vertical blank), and goes to the CCP by the
; warm boot, which turns interrupts on.

.setcpu "65C02"
.include "sys.inc"
.import WARM, BINIT, MFORMAT, DAYS2DATE, UNKEEP, CLKINIT
.importzp TYEAR, TMONTH, TDATE
.export COLD
.exportzp ROMVER

VER_MAX         = 8                    ; the BIOS's version, at most

.zeropage
SP0:            .res    2
SP1:            .res    2
SP2:            .res    2
NUM:            .res    2
DRV:            .res    1
FIRST:          .res    1
FIRST2:         .res    1
SHIFT:          .res    1
CIDX:           .res    1              ; CARDS' place in CARDTAB
VER:            .res    VER_MAX + 1
ROMVER:         .res    2              ; the ROM BIOS's version, major and minor ($FF $FF: not found), for
                                       ; function 72's +10-11 (bdos.s, F_OODOS; interface 9)

.segment "RES"

COLD:           SEI
                CLD
                LDA     #SYS_BLOCK
                STA     NB_WIN0
                LDX     #$FF
                TXS
                LDA     #0             ; $0000-$0CFF; the stack is empty
                TAX
@clear:
.repeat >SYS_CLEAR_END, page
                STA     page * $100,X
.endrepeat
                INX
                BNE     @clear
                LDX     #0
@scb:           LDY     SCBINIT,X      ; the SCB's settings: offset, value
                LDA     SCBINIT + 1,X
                STA     SCB,Y
                INX
                INX
                CPX     #SCBINIT_LEN
                BNE     @scb
                LDA     #BIOS_BLOCK    ; bank 0's W2 until the jump table says otherwise
                STA     CALLW2
                JSR     BINIT
                LDA     #0
                JSR     JT(BIOS::TIME)
                JSR     MFORMAT
                LDX     #0
@name:          LDA     BANNER,X
                BEQ     @date
                JSR     PUT
                INX
                BRA     @name
@date:          JSR     DAYS2DATE
                BCS     @end           ; before 2000: no date to show
                LDA     #'2'           ; 20yy or 21yy
                JSR     PUT
                LDA     TYEAR
                LDX     #'0'
                CMP     #100
                BCC     @century
                SBC     #100
                LDX     #'1'
@century:       PHA
                TXA
                JSR     PUT
                PLA
                JSR     DEC2
                LDA     #'-'
                JSR     PUT
                LDA     TMONTH
                JSR     DEC2
                LDA     #'-'
                JSR     PUT
                LDA     TDATE
                JSR     DEC2
                LDA     #' '
                JSR     PUT
                LDA     SCB_HOUR
                JSR     HEX2
                LDA     #':'
                JSR     PUT
                LDA     SCB_MIN
                JSR     HEX2
                LDA     #':'
                JSR     PUT
                LDA     SCB_SEC
                JSR     HEX2
@end:           JSR     NEWLINE
                JSR     STATUS
                JSR     UNKEEP         ; the loader's table: no RSX kept (its marks
                JSR     CLKINIT        ; are BSS, which nothing has cleared)
                JMP     WARM

NEWLINE:        LDA     #$0D
                JSR     PUT
                LDA     #$0A
                JMP     PUT

; The boot status (2026-11-03, the owner's), three lines under the banner:
;
;   BIOS 0.13   47K TPA                  the ROM BIOS's version, the TPA
;   A: 8M   B: 8M   M: 128K              each drive and its size
;   Cards: video, sound, SD, USB keyboard, DMA, panel
;
; The version is what follows "BACKPLANE BIOS " in the ROM's own greeting,
; found in its copy in RAM block 63 through W1 ("?" if it isn't there, as
; when a test loads OODOS without the ROM's reset); so a ROM's own string
; says, and no BIOS needs to change. A drive's size is its DPB's, (DSM + 1)
; blocks of 128 << BSH bytes, in MB when it's whole MB. A card is there when
; its ID register reads its card's ID.
STATUS:         LDA     #<SBIOS
                LDX     #>SBIOS
                JSR     PUTS
                JSR     BIOSVER
                LDA     #<VER
                LDX     #>VER
                JSR     PUTS
                LDA     #<SGAP
                LDX     #>SGAP
                JSR     PUTS
                LDA     #<TPA_K
                LDX     #>TPA_K
                JSR     PUTDEC
                LDA     #<STPA
                LDX     #>STPA
                JSR     PUTS
                JSR     NEWLINE
                JSR     DRIVES
                JSR     NEWLINE
                JSR     CARDS
                JMP     NEWLINE

TPA_K           = (BDOS_ENTRY - $0300) / 1024
GREETKEY:       .byte   "BACKPLANE BIOS "
GREETKEY_LEN    = * - GREETKEY

; VER: the version from the ROM's greeting, or "?". W1 is the BDOS's again
; before anything is printed (the screen's output takes W1 too).
BIOSVER:        LDA     #63            ; RAM block 63: the ROM BIOS's copy
                STA     NB_WIN1
                LDA     #<($4000 + $0800) ; $C800-$FFFF of it
                STA     SP0
                LDA     #>($4000 + $0800)
                STA     SP0 + 1
@try:           LDY     #0
@cmp:           LDA     (SP0),Y
                CMP     GREETKEY,Y
                BNE     @next
                INY
                CPY     #GREETKEY_LEN
                BNE     @cmp
                LDX     #0             ; found: the word after it
@copy:          LDA     (SP0),Y
                CMP     #' ' + 1       ; to a blank or a control character
                BCC     @end
                STA     VER,X
                INY
                INX
                CPX     #VER_MAX
                BNE     @copy
@end:           STZ     VER,X
                TXA
                BNE     @back
                BRA     @none
@next:          INC     SP0
                BNE     @try
                INC     SP0 + 1
                LDA     SP0 + 1
                CMP     #$80           ; past $7FFF: not there
                BNE     @try
@none:          LDA     #'?'
                STA     VER
                STZ     VER + 1
@back:          LDA     #BDOS_BLOCK
                STA     NB_WIN1
                ; fall through: ROMVER from VER

; ROMVER: VER as "major.minor", each a decimal number to 255; $FF $FF if it isn't one.
ROMPARSE:       LDA     #$FF
                STA     ROMVER
                STA     ROMVER + 1
                LDX     #0
                JSR     @num
                BCS     @out
                PHA
                LDA     VER,X
                CMP     #'.'
                BNE     @drop
                INX
                JSR     @num
                BCS     @drop
                STA     ROMVER + 1
                PLA
                STA     ROMVER
                RTS
@drop:          PLA
@out:           RTS
; The decimal digits at VER + X: A their value, X past them; carry set if
; none, or past 255.
@num:           STZ     NUM
                STZ     NUM + 1        ; digits seen
@dig:           LDA     VER,X
                SEC
                SBC     #'0'
                CMP     #10
                BCS     @end
                PHA
                LDA     NUM            ; NUM x 10 + the digit
                ASL     A
                BCS     @big
                ASL     A
                BCS     @big
                ADC     NUM
                BCS     @big
                ASL     A
                BCS     @big
                STA     NUM
                PLA
                ADC     NUM
                BCS     @over
                STA     NUM
                INC     NUM + 1
                INX
                BRA     @dig
@big:           PLA
@over:          SEC
                RTS
@end:           LDA     NUM + 1
                BEQ     @none          ; no digit
                LDA     NUM
                CLC
                RTS
@none:          SEC
                RTS

; Each drive the BIOS has, A-P, and its size, three blanks between.
DRIVES:         JSR     JT(BIOS::DRVTBL)
                STA     SP1
                STX     SP1 + 1
                STZ     DRV
                STZ     FIRST
@drive:         LDA     DRV
                ASL     A
                TAY
                LDA     (SP1),Y        ; its DPH, or 0
                STA     SP0
                INY
                LDA     (SP1),Y
                STA     SP0 + 1
                ORA     SP0
                BEQ     @next
                LDY     #DPH_DPB       ; the DPB
                LDA     (SP0),Y
                TAX
                INY
                LDA     (SP0),Y
                STA     SP0 + 1
                STX     SP0
                LDY     #DPB_BSH
                LDA     (SP0),Y
                SEC
                SBC     #3             ; a block is 1 << (BSH - 3) KB
                STA     SHIFT
                LDY     #DPB_DSM       ; DSM + 1 blocks
                LDA     (SP0),Y
                CLC
                ADC     #1
                STA     NUM
                INY
                LDA     (SP0),Y
                ADC     #0
                STA     NUM + 1
                LDX     SHIFT
                BEQ     @kb
@shift:         ASL     NUM
                ROL     NUM + 1
                DEX
                BNE     @shift
@kb:            LDA     FIRST
                BEQ     @letter
                LDA     #<SGAP
                LDX     #>SGAP
                JSR     PUTS
@letter:        INC     FIRST
                LDA     DRV
                CLC
                ADC     #'A'
                JSR     PUT
                LDA     #':'
                JSR     PUT
                LDA     #' '
                JSR     PUT
                LDA     NUM            ; whole MB: in MB
                BNE     @k
                LDA     NUM + 1
                AND     #$03
                BNE     @k
                LDA     NUM + 1
                LSR     A
                LSR     A
                LDX     #0
                JSR     PUTDEC
                LDA     #'M'
                JSR     PUT
                BRA     @next
@k:             LDA     NUM
                LDX     NUM + 1
                JSR     PUTDEC
                LDA     #'K'
                JSR     PUT
@next:          INC     DRV
                LDA     DRV
                CMP     #16
                BEQ     @done
                JMP     @drive
@done:          RTS

; The cards whose ID registers read their IDs, in CARDTAB's order: the bits
; of the ID its mask keeps, so the sound card counts at any revision (its
; type, the high nibble) and the rest at exactly theirs.
CARDS:          LDA     #<SCARDS
                LDX     #>SCARDS
                JSR     PUTS
                STZ     FIRST
                LDX     #0
@card:          LDA     CARDTAB,X      ; the ID register
                STA     SP0
                LDA     CARDTAB + 1,X
                STA     SP0 + 1
                LDA     (SP0)
                AND     CARDTAB + 3,X  ; what of it says which card
                CMP     CARDTAB + 2,X
                BNE     @next
                STX     CIDX
                LDA     FIRST
                BEQ     @name
                LDA     #<SCOMMA
                LDX     #>SCOMMA
                JSR     PUTS
@name:          INC     FIRST
                LDX     CIDX
                LDA     CARDTAB + 4,X  ; its name
                STA     SP2
                LDA     CARDTAB + 5,X
                STA     SP2 + 1
                JSR     PUTSP
                LDX     CIDX
@next:          TXA
                CLC
                ADC     #CARD_LEN
                TAX
                CPX     #CARDTAB_LEN
                BNE     @card
                LDA     FIRST
                BNE     @done
                LDA     #<SNONE
                LDX     #>SNONE
                JSR     PUTS
@done:          RTS

.macro CARD id, want, mask, name
                .word   id
                .byte   want, mask     ; ID AND mask = want
                .word   name
.endmacro
CARD_LEN        = 6
CARDTAB:        CARD    VIDEO_ID, VIDEO_ID_V2, $FF, CVIDEO ; revision 2 only
                CARD    SOUND_ID, SOUND_TYPE, $F0, CSOUND
                CARD    SD_ID, SD_ID_V1, $FF, CSD
                CARD    DISK2_ID, DISK2_ID_V1, $FF, CDISK2
                CARD    USB_ID, USB_ID_V1, $FF, CUSB
                CARD    DMA_ID, DMA_ID_V1, $FF, CDMA
                CARD    CONSOLE_ID, CONSOLE_ID_V1, $FF, CPANEL
CARDTAB_LEN     = * - CARDTAB

; The 0-ended string at A/X; PUTSP, at SP2.
PUTS:           STA     SP2
                STX     SP2 + 1
PUTSP:          LDY     #0
@char:          LDA     (SP2),Y
                BEQ     @done
                PHY
                JSR     PUT
                PLY
                INY
                BNE     @char
@done:          RTS

; A/X (0-65,535) in decimal, without leading zeros.
PUTDEC:         STA     NUM
                STX     NUM + 1
                STZ     FIRST2
                LDX     #0             ; the power of ten's index
@power:         LDY     #'0'
@sub:           LDA     NUM
                SEC
                SBC     POW10,X
                PHA
                LDA     NUM + 1
                SBC     POW10 + 1,X
                BCC     @digit
                STA     NUM + 1
                PLA
                STA     NUM
                INY
                BRA     @sub
@digit:         PLA
                CPY     #'0'
                BNE     @print
                LDA     FIRST2
                BEQ     @skip
@print:         STY     FIRST2
                TYA
                PHX
                JSR     PUT
                PLX
@skip:          INX
                INX
                CPX     #8
                BNE     @power
                LDA     NUM            ; the units, always
                ORA     #'0'
                JMP     PUT
POW10:          .word   10000, 1000, 100, 10

SBIOS:          .byte   "BIOS ", 0
SGAP:           .byte   "   ", 0
STPA:           .byte   "K TPA", 0
SCARDS:         .byte   "Cards: ", 0
SCOMMA:         .byte   ", ", 0
SNONE:          .byte   "none", 0
CVIDEO:         .byte   "video", 0
CSOUND:         .byte   "sound", 0
CSD:            .byte   "SD", 0
CDISK2:         .byte   "Disk II", 0
CUSB:           .byte   "USB keyboard", 0
CDMA:           .byte   "DMA", 0
CPANEL:         .byte   "panel", 0


PUT:            PHX
                JSR     JT(BIOS::CONOUT)
                PLX
                RTS

; A (0-99) as two decimal digits.
DEC2:           LDX     #'0'
@tens:          CMP     #10
                BCC     @ones
                SBC     #10
                INX
                BRA     @tens
@ones:          PHA
                TXA
                JSR     PUT
                PLA
                ORA     #'0'
                JMP     PUT

; A (BCD) as two digits.
HEX2:           PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     #'0'
                JSR     PUT
                PLA
                AND     #$0F
                ORA     #'0'
                JMP     PUT

BANNER:         .byte   $0D, $0A, "OODOS ", '0' + OODOS_MAJOR, '.', '0' + OODOS_MINOR, "  ", 0

SCBINIT:        .byte   <(SCB_VERSION - SCB), $31
                .byte   <(SCB_WIDTH - SCB), 79
                .byte   <(SCB_PAGELEN - SCB), 24
                .byte   <(SCB_CIVEC + 1 - SCB), VEC_CRT | VEC_TTY ; CON: is CRT and TTY
                .byte   <(SCB_COVEC + 1 - SCB), VEC_CRT | VEC_TTY
                .byte   <(SCB_AIVEC + 1 - SCB), VEC_TTY           ; AUX: is TTY
                .byte   <(SCB_AOVEC + 1 - SCB), VEC_TTY
                .byte   <(SCB_LOVEC + 1 - SCB), VEC_LPT           ; LST: is LPT
                .byte   <(SCB_RUBOUT - SCB), $FF                  ; rub/del erases, as ^H
                .byte   <(SCB_DELIM - SCB), '$'
                .byte   <(SCB_MULTI - SCB), 1
                .byte   <(SCB_DRVCHAIN + 1 - SCB), $FF            ; the default drive alone
                .byte   <(SCB_FLAGS - SCB), $80                   ; the long error messages
                .byte   <(SCB_COMBASE + 1 - SCB), >COMMON
                .byte   <(SCB_DMA - SCB), <BP_DMA
                .byte   <(SCB_DMA + 1 - SCB), >BP_DMA
SCBINIT_LEN     = * - SCBINIT

; The base page, from oodos.inc
BP_DMA          = $0280
