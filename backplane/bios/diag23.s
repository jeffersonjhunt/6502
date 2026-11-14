; The ROM's diagnostics, flash block 23: the core (hw/diag.md, plan 2).
;
; Seen at $8000-$BFFF through W2. Its entries (diag.inc's DE_*):
;
;   the prompt  from the BIOS's RESET, with switch 19 up: DIAGNOSTICS: PRESS
;               CONT OR Y WITHIN 5 S, $A5 on the lights. CONT or Y within 5 s
;               is stage 0; anything else, or nothing, the BIOS's BOOT.
;               Nothing is written to RAM before the answer: like stages 0-2
;               it is macros, with no JSR, no zero page and no stack
;   go          stage 0 on, from $C812 (after its own Y) and $C815 (a
;               program's, which has asked already)
;   loop        the loop's next pass, stage 3 on
;
; The stages here (diag.inc has the rest of the run's frame):
;
;   0  SEI; W3 and VEC on the flash (VEC on this block, for Break); every
;      card quiet, as the BIOS's QUIET does it; the lights, the 6551, the
;      screen (a revision 2 video card: the font, the palette, the text)
;   1  the flash's sums, blocks 21-31, against romsum.py's table at this
;      block's end. A wrong sum in block 23 itself is fatal ($81 blinking)
;   2  zero page and stack: the first RAM block whose first 512 bytes pass
;      a march (backgrounds $00 and $55) becomes W0. None: fatal ($82)
;   3  the CPU (XBA: a W65C02S, not a 65C816); the data lines at each RAM
;      module's first block, and which modules answer
;   4  the address lines and the blocks: every block tagged, then read back
;   5  address in address over every block: all written, then all checked
;   6  the march, background $00 (full: four), then W0's own first 512
;      bytes from another block that passed
;
; then blocks 22 and 21 (stages 7-14), each only if its signature says it is
; this build's. Stages 1 and 2 have no RAM: their state is in the registers
; (S the block, A and Y a sum) and in WAIT3's bit 1 (diag.inc).

.setcpu "65C02"
.include "backplane.inc"
.include "diag.inc"
.include "oohw.inc"
.importzp BIOS_VMAJ, BIOS_VMIN

FLASH30         = NB_ROM | NB_RO | 30  ; the font
DG_PROMPTWRAPS  = 38                   ; the prompt's CPU count: 38 x 256 polls of about 5,170 cycles, 7 s

.segment "D23"
                DG_HEADER 0, PROMPT, GO, LOOP
                jmp     BRKH           ; $8025: Break's, after the stub mapped this block
.assert * = $8028, error, "block 23's header"

; --- No RAM: the macros stages 0-2 and the prompt are made of ---------------

; The string at str, ended by 0, on the serial line, each character waited
; for as the BIOS's ECHO does. A, X, Y.
.macro SERS str
        .local loop, d1, d2, done
                ldx     #0
loop:           lda     str,x
                beq     done
                sta     ACIA_DATA
                ldy     #3
d1:             lda     #0
d2:             dec     a
                bne     d2
                dey
                bne     d1
                inx
                bne     loop
done:
.endmacro

; The string at str on the screen at row, col, if there is one. A, X.
.macro SCRS row, col, str
        .local loop, done
                lda     VIDEO_ID
                cmp     #VIDEO_ID_V2
                bne     done
                lda     #<(DG_TEXT + (row * DG_COLS + col) * 2)
                sta     VIDEO_PADDR
                lda     #>(DG_TEXT + (row * DG_COLS + col) * 2)
                sta     VIDEO_PADDR + 1
                lda     #^(DG_TEXT + (row * DG_COLS + col) * 2)
                sta     VIDEO_PADDR + 2
                ldx     #0
loop:           lda     str,x
                beq     done
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
                inx
                bne     loop
done:
.endmacro

; Both
.macro SAYS row, col, str
                SCRS    row, col, str
                SERS    str
.endmacro

; About 0.5 s at 7.16 MHz. A, X, Y.
.macro HALFSEC
        .local d0, d1, d2
                ldy     #11
d0:             ldx     #0
d1:             lda     #0
d2:             dec     a
                bne     d2
                dex
                bne     d1
                dey
                bne     d0
.endmacro

; The serial line: 8-N-1 at 19,200 baud, no interrupts, as the BIOS sets it.
.macro ACIAINIT
                lda     #$1F
                sta     ACIA_CTRL
                lda     #$0B
                sta     ACIA_CMD
.endmacro

; The text screen on a revision 2 video card, by the port alone (no DMA,
; no RAM): the font from flash block 30, the palette, a clear text area,
; and the registers as the BIOS's VSCREEN sets them.
.macro SCREENINIT
        .local done, pal, o, i
                lda     VIDEO_ID
                cmp     #VIDEO_ID_V2
                beq     :+
                jmp     done
:               stz     VIDEO_PCTL
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                lda     #FLASH30
                sta     NB_WIN1
                lda     #<DG_FONT
                sta     VIDEO_PADDR
                lda     #>DG_FONT
                sta     VIDEO_PADDR + 1
                lda     #^DG_FONT
                sta     VIDEO_PADDR + 2
        .repeat 16, p
        .scope
                ldx     #0
fl:             lda     $4000 + p * $100,x
                sta     VIDEO_PDATA
                inx
                bne     fl
        .endscope
        .endrepeat
                lda     #1
                sta     NB_WIN1
                stz     VIDEO_PADDR
                stz     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                ldx     #0
pal:            lda     CGA,x
                sta     VIDEO_PDATA
                inx
                cpx     #32
                bne     pal
                lda     #<DG_TEXT
                sta     VIDEO_PADDR
                lda     #>DG_TEXT
                sta     VIDEO_PADDR + 1
                lda     #^DG_TEXT
                sta     VIDEO_PADDR + 2
                ldy     #20
o:              ldx     #120
i:              lda     #' '
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
                dex
                bne     i
                dey
                bne     o
                stz     VIDEO_IEN
                stz     VIDEO_SPLASH
                stz     VIDEO_PALBASE
                stz     VIDEO_PALBASE + 1
                stz     VIDEO_SPRBASE
                stz     VIDEO_SPRBASE + 1
                stz     VIDEO_BORDER
                lda     #VIDEO_TEXT80B
                sta     VIDEO_L0_CONFIG
                lda     #<(DG_TEXT >> 8)
                sta     VIDEO_L0_MAP
                lda     #>(DG_TEXT >> 8)
                sta     VIDEO_L0_MAP + 1
                lda     #<(DG_FONT >> 8)
                sta     VIDEO_L0_TILE
                lda     #>(DG_FONT >> 8)
                sta     VIDEO_L0_TILE + 1
                stz     VIDEO_L0_HSCROLL
                stz     VIDEO_L0_HSCROLL + 1
                stz     VIDEO_L0_VSCROLL
                stz     VIDEO_L0_VSCROLL + 1
                stz     VIDEO_L0_PALOFS
                stz     VIDEO_L1_CONFIG
                lda     #VIDEO_ON_L0
                sta     VIDEO_CTRL
done:
.endmacro

; Every card quiet, as the BIOS's QUIET (bios.s) does it, with no JSR: DMA
; stopped, the SD card's command abandoned, every card's interrupt off, the
; 6510 stopped and the SIDs silent, the stream and the ES5503 stopped, the
; clock's TIE off, the VIA's interrupts off. The breakpoint is left alone.
.macro QUIETM
        .local sd, idle, usb, video, sound, sid, panel, via
                lda     #NB_RTC_TICK
                sta     NB_RTCCTL
                lda     DMA_ID
                cmp     #DMA_ID_V1
                bne     sd
                lda     #DMA_ALL
                sta     DMA_ABORT
                sta     DMA_STATUS
                stz     DMA0_MODE
                stz     DMA0_MODE + $10
                stz     DMA0_MODE + $20
                stz     DMA0_MODE + $30
sd:             lda     SD_ID
                cmp     #SD_ID_V1
                bne     usb
                stz     SD_IEN
                lda     SD_STATUS
                bpl     idle
                lda     #SD_CTRL_RESET
                sta     SD_CTRL
                bra     usb
idle:           stz     SD_CTRL
usb:            lda     USB_ID
                cmp     #USB_ID_V1
                bne     video
                stz     USB_IEN
video:          lda     VIDEO_ID
                cmp     #VIDEO_ID_V2
                bne     sound
                stz     VIDEO_IEN
                stz     VIDEO_SPLASH
                stz     VIDEO_PCTL
sound:          lda     SOUND_ID
                and     #$F0
                cmp     #SOUND_TYPE
                bne     panel
                lda     SOUND_CTRL
                and     #<~SOUND_RUN
                sta     SOUND_CTRL
                stz     SOUND_IEN
                ldx     #SOUND_SIDS - 1
sid:            stz     SOUND,x
                dex
                bpl     sid
                lda     SOUND_ID
                cmp     #SOUND_ID_V2
                bcc     panel
                lda     #SOUND_PCM_FLUSH
                sta     SOUND_PCMCTL
                lda     SOUND_DOCCTL
                and     #<~SOUND_DOC_RUN
                sta     SOUND_DOCCTL
panel:          lda     CONSOLE_ID
                cmp     #CONSOLE_ID_V1
                bne     via
                stz     CONSOLE_IEN
                lda     #1
                sta     CONSOLE_ISR
via:            lda     #$7F
                sta     VIA_IER
.endmacro


; A march over $4000-$41FF (W1's block's first 512 bytes) with background bg,
; by absolute indexed loads: up (w0); up (r0, w1); up (r1, w0); down (r0,
; w1); down (r1, w0); (r0). A wrong byte jumps to fail.
.macro M512UP page, rd, wr, fail
        .local l, ok
                ldx     #0
l:              lda     page,x
                cmp     #rd
                beq     ok
                jmp     fail
ok:             lda     #wr
                sta     page,x
                inx
                bne     l
.endmacro
.macro M512DN page, rd, wr, fail
        .local l, ok
                ldx     #$FF
l:              lda     page,x
                cmp     #rd
                beq     ok
                jmp     fail
ok:             lda     #wr
                sta     page,x
                dex
                cpx     #$FF
                bne     l
.endmacro
.macro MARCH512 bg, fail
        .local w0
                lda     #bg
                ldx     #0
w0:             sta     $4000,x
                sta     $4100,x
                inx
                bne     w0
                M512UP  $4000, bg, <~bg, fail
                M512UP  $4100, bg, <~bg, fail
                M512UP  $4000, <~bg, bg, fail
                M512UP  $4100, <~bg, bg, fail
                M512DN  $4100, bg, <~bg, fail
                M512DN  $4000, bg, <~bg, fail
                M512DN  $4100, <~bg, bg, fail
                M512DN  $4000, <~bg, bg, fail
                M512UP  $4000, bg, bg, fail
                M512UP  $4100, bg, bg, fail
.endmacro

; 512 bytes at base written through W1: each byte the low byte of its offset.
.macro PROBE512 base
        .local l
                ldx     #0
l:              txa
                sta     base,x
                sta     base + $100,x
                inx
                bne     l
.endmacro

; Block S, whose first 512 bytes passed the march, as W0 for a moment: are
; they its own? Canaries in them (offset XOR $A5, then $5A, through W0), then
; 512 bytes written where an address line stuck or two blocks answering as one
; would land on them: the block's own +$200, +$400 ... +$2000 (A9-A13), and
; the first 512 bytes of each block one block line (A14-A19) away, all through
; W1. A canary changed: fail. No RAM is trusted: S, A and X only.
.macro PROVEW0 fail
        .local can, chk, ok1, ok2
                tsx
                stx     NB_WIN0
                ldx     #0
can:            txa
                eor     #$A5
                sta     $00,x
                txa
                eor     #$5A
                sta     $0100,x
                inx
                bne     can
                tsx
                stx     NB_WIN1
        .repeat 5, n
                PROBE512 $4000 + ($200 << n)
        .endrepeat
        .repeat 6, k
        .scope
                tsx
                txa
                eor     #(1 << k)
                tax
                lda     VIDEO_ID       ; bank 8, with a revision 2 card, is its aperture: not RAM
                cmp     #VIDEO_ID_V2
                bne     go
                cpx     #32
                bcc     go
                cpx     #36
                bcc     skip
go:             stx     NB_WIN1
                PROBE512 $4000
skip:
        .endscope
        .endrepeat
                ldx     #0
chk:            txa
                eor     #$A5
                cmp     $00,x
                beq     ok1
                jmp     fail
ok1:            txa
                eor     #$5A
                cmp     $0100,x
                beq     ok2
                jmp     fail
ok2:            inx
                bne     chk
.endmacro

; The serial line gets X (a block number) in hex. A, Y.
.macro SERHEXX
        .local d1, d2, n1, d3, d4, n2
                txa
                lsr     a
                lsr     a
                lsr     a
                lsr     a
                cmp     #10
                bcc     n1
                adc     #'A' - '0' - 10 - 1
n1:             adc     #'0'
                sta     ACIA_DATA
                ldy     #3
d1:             lda     #0
d2:             dec     a
                bne     d2
                dey
                bne     d1
                txa
                and     #$0F
                cmp     #10
                bcc     n2
                adc     #'A' - '0' - 10 - 1
n2:             adc     #'0'
                sta     ACIA_DATA
                ldy     #3
d3:             lda     #0
d4:             dec     a
                bne     d4
                dey
                bne     d3
.endmacro

; --- The prompt ----------------------------------------------------------------

; From RESET, with W3 on the flash and W2 on this block: nothing in RAM is
; touched. Y is the last second seen, X the changes counted.
PROMPT:
                sei
                cld
                lda     #DG_B23        ; Break here: this block's stub, as no (BRKH), not the BIOS's
                sta     NB_VEC
                stz     NB_WAIT3       ; no options but the switches' (WAIT3 is the diagnostics')
                lda     #8             ; the lights on: CTRL's bit 3, DISPLAY
                sta     CONSOLE_CTRL
                lda     #$A5
                sta     CONSOLE_DISPLAY
                lda     CONSOLE_KEYS   ; a key pressed in the BIOS's first instructions doesn't count
                ACIAINIT
                SCREENINIT
                SCRS    0, 0, STITLE
                SCRS    2, 0, SPROMPT
                SERS    SPROMPTS
                ; 5 s: six of the clock chip's seconds (RTCCTL's TICK), or about 7 s
                ; counted by the CPU if they don't come. Y the seconds, X the polls
                ; (about 5,170 cycles each), S their wraps (1.32M cycles each).
                lda     #NB_RTC_TICK
                sta     NB_RTCCTL      ; a second already pending doesn't count
                ldx     #0
                txs
                ldy     #0
P_POLL:         bit     NB_RTCCTL
                bpl     P_KEYS         ; TICK is bit 7
                lda     #NB_RTC_TICK
                sta     NB_RTCCTL
                iny
                cpy     #6             ; six seconds: between 5 and 6 s
                beq     P_NO
P_KEYS:         lda     ACIA_STATUS
                and     #$08
                beq     P_USB
                lda     ACIA_DATA
                and     #$5F           ; a capital, bit 7 off
                cmp     #'Y'
                beq     P_YES
                bra     P_NO
P_USB:          lda     USB_ID
                cmp     #USB_ID_V1
                bne     P_PANEL
                lda     USB_KEY
                bpl     P_PANEL        ; no strobe
                bit     USB_KEYCLR
                and     #$5F
                cmp     #'Y'
                beq     P_YES
                bra     P_NO
P_PANEL:        lda     CONSOLE_ID
                cmp     #CONSOLE_ID_V1
                bne     P_WAIT
                lda     CONSOLE_KEYS
                and     #$08           ; CONT; the other keys do nothing here
                bne     P_YES
P_WAIT:         lda     #0
P_D1:           dec     a
                bne     P_D1
P_D2:           dec     a
                bne     P_D2
P_D3:           dec     a
                bne     P_D3
P_D4:           dec     a
                bne     P_D4
                inx
                bne     P_POLL
                tsx
                inx
                txs
                cpx     #DG_PROMPTWRAPS
                beq     P_CLOCK
                ldx     #0
                jmp     P_POLL
P_CLOCK:        cpy     #0
                bne     P_NO
                SERS    SNOTICK        ; the CPU's count ran out with no second from the clock chip
                bra     P_NO
P_YES:          jmp     GO
P_NO:           stz     CONSOLE_DISPLAY
                stz     CONSOLE_CTRL
                SERS    SBOOTING
                lda     #DG_FLASH31
                sta     NB_VEC
                jmp     DG_TBOOT

; --- Stage 0 ------------------------------------------------------------------

GO:
                sei
                cld
                ldx     #$FF
                txs
                lda     #DG_FLASH31
                sta     NB_WIN3        ; (this code is in W2)
                lda     #DG_B23
                sta     NB_VEC         ; Break: this block's vectors, the stub at $8020
                QUIETM
                lda     #8
                sta     CONSOLE_CTRL
                stz     CONSOLE_DISPLAY
                ACIAINIT
                SCREENINIT
                SCRS    0, 0, STITLE
                SERS    SCRLF
                SERS    STITLE
                SERS    SCRLF

; --- Stage 1: the flash's sums ------------------------------------------------

                lda     #1
                sta     CONSOLE_DISPLAY
                lda     NB_WAIT3       ; this stage's flag clear (a program's options kept)
                and     #<~DW_FLASHBAD
                sta     NB_WAIT3
                SAYS    3, 0, SL1
                ldx     #21
                txs                    ; S: the block (nothing is pushed until stage 3)
G_BLOCK:         tsx
                txa
                ora     #NB_ROM | NB_RO
                sta     NB_WIN1
                lda     #0
                tay
                tax
                clc
G_BYTE:
        .repeat 63, p
        .scope
                adc     $4000 + p * $100,x
                bcc     nc
                iny
                clc
nc:
        .endscope
        .endrepeat
                inx
                beq     G_LAST
                jmp     G_BYTE
G_LAST:          tsx
                cpx     #23            ; block 23's last page is the table: not summed
                beq     G_CMP
                clc                    ; (CPX set it)
                ldx     #0
G_TAIL:          adc     $7F00,x
                bcc     G_TNC
                iny
                clc
G_TNC:           inx
                bne     G_TAIL
G_CMP:           tsx
                cmp     SUMLO - 21,x
                bne     G_BAD
                tya
                cmp     SUMHI - 21,x
                bne     G_BAD
                jmp     G_NEXT
G_BAD:           cpx     #23
                bne     G_SAY
                jmp     FATAL1
G_SAY:           lda     NB_WAIT3
                and     #DW_FLASHBAD
                bne     G_DETAIL
                lda     NB_WAIT3
                ora     #DW_FLASHBAD
                sta     NB_WAIT3
                SCRS    3, DG_RESCOL, SFAIL
                SERS    SFAILCR
G_DETAIL:        SERS    SFLASHB
                tsx
                SERHEXX
                SERS    SSUMBAD
G_NEXT:          tsx
                inx
                txs
                cpx     #32
                beq     G_SUMS
                jmp     G_BLOCK
G_SUMS:          lda     #1
                sta     NB_WIN1
                lda     NB_WAIT3
                and     #DW_FLASHBAD
                bne     G_STAGE2
                SCRS    3, DG_RESCOL, SPASS
                SERS    SPASSCR

; --- Stage 2: zero page and stack ---------------------------------------------

G_STAGE2:        lda     #2
                sta     CONSOLE_DISPLAY
                ldx     #0
                txs
G_CAND:          tsx
                cpx     #32            ; bank 8 is the aperture on a revision 2 card: not RAM
                bcc     G_TRY
                cpx     #36
                bcs     G_TRY
                lda     VIDEO_ID
                cmp     #VIDEO_ID_V2
                bne     G_TRY
                jmp     G_NEXTC
G_TRY:           txa
                sta     NB_WIN1
                MARCH512 $00, G_NEXTC
                MARCH512 $55, G_NEXTC
                PROVEW0 G_NEXTC        ; and nothing else lands in them (review MM2)
                tsx                    ; this block: W0
                stx     NB_WIN0
                txa
                ldx     #$FF
                txs
                ldx     #$3F - $08
G_CLR:           stz     $08,x
                dex
                bpl     G_CLR
                sta     DG_W0
                sta     S2BLOCK        ; (BSS: the zero page block's, cleared by nothing)
                jmp     STAGE2
G_NEXTC:         tsx
                inx
                txs
                cpx     #64
                beq     G_NONE
                jmp     G_CAND
G_NONE:          jmp     FATAL2

; --- With RAM: stage 2's line, the options, then stages 3-6 ------------------

STAGE2:
                lda     #1
                sta     NB_WIN1
                lda     VIDEO_ID
                cmp     #VIDEO_ID_V2
                bne     @noscr
                lda     #$80
                sta     DG_SCR
                lda     #1
                sta     DG_APER
@noscr:         lda     #1
                sta     DG_PASS
                stz     DG_PASS + 1
                ; stage 1's result, from WAIT3
                ldx     #1
                lda     #DR_PASS
                sta     DG_RES + 1
                lda     NB_WAIT3
                and     #DW_FLASHBAD
                beq     @s2
                jsr     FAILED
@s2:            ldx     #NAME2END - NAME2 - 1 ; "ZERO PAGE AND STACK: BLOCK nn"
@copy:          lda     NAME2,x
                sta     NAMEBUF,x
                dex
                bpl     @copy
                lda     #<NAMEBUF
                sta     DG_T0
                lda     #>NAMEBUF
                sta     DG_T0 + 1
                lda     DG_W0          ; the block's number at the name's end
                ldy     #NAME2END - NAME2
                jsr     BUFDEC
                lda     #2
                jsr     STAGEBEG
                lda     DG_W0
                beq     @first
                lda     #DR_FAIL       ; a block before it failed
                jsr     STAGEEND
                bra     @opts
@first:         lda     #DR_PASS
                jsr     STAGEEND
@opts:          ; the options: a program's (WAIT3), or the panel's switches
                lda     NB_WAIT3
                bit     #DW_PROGRAM
                beq     @switches
                ldx     #DO_PROGRAM
                bit     #DW_FULL
                beq     @ask
                ldx     #DO_PROGRAM | DO_FULL
@ask:           bit     #DW_ASK
                beq     @setp
                txa
                ora     #DO_ASK
                tax
@setp:          stx     DG_OPT
                bra     @opted
@switches:      stz     DG_OPT
                lda     CONSOLE_ID
                cmp     #CONSOLE_ID_V1
                bne     @opted
                lda     CONSOLE_SWR + 2 ; switches 16-19: bits 0-3
                bit     #$04           ; 18: full
                beq     @s17
                ldx     #DO_FULL
                stx     DG_OPT
@s17:           bit     #$02           ; 17: loop
                beq     @s16
                pha
                lda     DG_OPT
                ora     #DO_LOOP
                sta     DG_OPT
                pla
@s16:           bit     #$01           ; 16: interactive
                beq     @opted
                lda     DG_OPT
                ora     #DO_ASK
                sta     DG_OPT
@opted:         stz     NB_WAIT3       ; read: the stage-1 flag and the program's options are done with
                jmp     PASSGO

; The loop's next pass, from the end of the last (ENDRUN, in whichever block):
; the pass's state cleared, stages 1 and 2's results kept.
LOOP:
                sei
                cld
                ldx     #$FF
                txs
                lda     #DG_FLASH31
                sta     NB_WIN3
                lda     #DG_B23
                sta     NB_VEC
                stz     DG_NFAIL
                lda     DG_RES + 1
                cmp     #DR_FAIL
                bne     @r2
                inc     DG_NFAIL
@r2:            lda     DG_RES + 2
                cmp     #DR_FAIL
                bne     PASSGO
                inc     DG_NFAIL
PASSGO:
                stz     DG_FSTAGE
                stz     DG_SHOWN
                ldx     #7
@bad:           stz     DG_BAD,x
                stz     SKIPS,x
                dex
                bpl     @bad
                lda     #0             ; the blocks stage 2 passed over: failed, each pass (MM7)
@rej:           cmp     S2BLOCK
                bcs     @hdr0
                ldx     DG_APER
                beq     @mark
                cmp     #32
                bcc     @mark
                cmp     #36
                bcc     @nomark
@mark:          jsr     MARKBAD
@nomark:        inc     a
                bra     @rej
@hdr0:
                ldx     #12
@res:           stz     DG_RES + 3,x
                dex
                bpl     @res
                ldx     #1             ; stages 1 and 2's failures, as the first
                lda     DG_RES + 1
                cmp     #DR_FAIL
                beq     @f
                inx
                lda     DG_RES + 2
                cmp     #DR_FAIL
                bne     @hdr
@f:             stx     DG_FSTAGE
@hdr:           jsr     CLEARLOW
                jsr     HEADER
                jsr     STAGE3
                jsr     STAGE4
                jsr     STAGE5
                jsr     STAGE6
                lda     #DG_B22        ; on to block 22, if it is this build's
                ldx     #DE_START
                jsr     HANDOFF
                lda     #10
                sta     DG_T0 + 2
                lda     #7
                jsr     NOTFLASH
                lda     #DG_B21
                ldx     #DE_START
                jsr     HANDOFF
                lda     #13
                sta     DG_T0 + 2
                lda     #11
                jsr     NOTFLASH
                jmp     ENDRUN

; The header: on the screen's first row, and the pass on the serial line.
HEADER:
                lda     #0
                jsr     ATROW
                jsr     SCROFF         ; the title is the screen's already: the pass after it
                lda     #<STITLE
                ldx     #>STITLE
                jsr     SCRSAY
                lda     #34
                sta     DG_COL
                jsr     SCRON
                lda     #<SBIOS
                ldx     #>SBIOS
                jsr     PUTS
                lda     #<SPASSH
                ldx     #>SPASSH
                jsr     PUTS
                lda     DG_PASS
                ldx     DG_PASS + 1
                jsr     PUTDEC16
                jsr     SPACE
                jsr     SPACE
                lda     DG_OPT
                and     #DO_FULL
                beq     @q
                lda     #<SFULL
                ldx     #>SFULL
                bra     @say
@q:             lda     #<SQUICK
                ldx     #>SQUICK
@say:           jsr     PUTS
                lda     DG_OPT
                and     #DO_LOOP
                beq     @ask
                lda     #<SLOOP
                ldx     #>SLOOP
                jsr     PUTS
@ask:           lda     DG_OPT
                and     #DO_ASK
                beq     @end
                lda     #<SASK
                ldx     #>SASK
                jsr     PUTS
@end:           jsr     CRLF
                jmp     SCROFF

; The screen's rows 18-21 blank (the last pass's result), the screen only.
CLEARLOW:
                bit     DG_SCR
                bpl     @done
                lda     #<(DG_TEXT + DG_ROWDET * DG_COLS * 2)
                sta     VIDEO_PADDR
                lda     #>(DG_TEXT + DG_ROWDET * DG_COLS * 2)
                sta     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                ldx     #4 * DG_COLS / 2
@cell:          lda     #' '
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
                lda     #' '
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
                dex
                bne     @cell
@done:          rts

; The string at A/X on the screen at the cursor, not the serial line.
SCRSAY:
                sta     DG_PTR
                stx     DG_PTR + 1
                bit     DG_SCR
                bpl     @done
                ldy     #0
@next:          lda     (DG_PTR),y
                beq     @done
                ldx     DG_ROW
                pha
                lda     DG_COL
                asl     a
                clc
                adc     ROWLO,x
                sta     VIDEO_PADDR
                lda     ROWHI,x
                adc     #0
                sta     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                pla
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
                inc     DG_COL
                iny
                bne     @next
@done:          rts

; A (0-99) in decimal into NAMEBUF at Y, then 0.
BUFDEC:
                ldx     #'0'
@tens:          cmp     #10
                bcc     @ones
                sbc     #10
                inx
                bra     @tens
@ones:          cpx     #'0'
                beq     @one
                pha
                txa
                sta     NAMEBUF,y
                iny
                pla
@one:           ora     #'0'
                sta     NAMEBUF,y
                lda     #0
                sta     NAMEBUF + 1,y
                rts

; --- Stage 3: the CPU and the data lines --------------------------------------

STAGE3:
                lda     #<SN3
                sta     DG_T0
                lda     #>SN3
                sta     DG_T0 + 1
                lda     #3
                jsr     STAGEBEG
                lda     #1             ; XBA ($EB) swaps A's bytes on a 65C816; on the W65C02S a NOP
                .byte   $EB
                lda     #2
                .byte   $EB
                cmp     #2
                beq     @data
                jsr     DETAIL
                lda     #<SCPU
                ldx     #>SCPU
                jsr     PUTS
                jsr     CRLF
@data:          stz     DG_MODS
                stz     DG_BLK         ; the module
@module:        ldx     DG_BLK
                lda     MODFIRST,x
                ldy     DG_APER
                beq     @blk
                lda     MODFIRSTA,x
@blk:           sta     NB_WIN1
                lda     #<$7FF0
                sta     HW_PTR
                lda     #>$7FF0
                sta     HW_PTR + 1
                jsr     HW_WALK
                bcc     @there
                lda     #0             ; a write of 0 that reads $FF, twice (CC5's two reads): nothing answers
                sta     $7FF0
                lda     $7FF0
                cmp     #$FF
                bne     @wrong
                ldx     #$FF
                lda     $7FF0 - $FF,x
                cmp     #$FF
                beq     @absent
@wrong:
                ldx     DG_BLK         ; there, with a data line wrong: its blocks are tested
                lda     MODBIT,x
                ora     DG_MODS
                sta     DG_MODS
                jsr     DETAIL
                jsr     MODNAME
                lda     #<SDATA
                ldx     #>SDATA
                jsr     PUTS
                lda     HW_GOT
                jsr     HEX2
                lda     #<SWROTE
                ldx     #>SWROTE
                jsr     PUTS
                lda     HW_WANT
                jsr     HEX2
                jsr     CRLF
                bra     @nextm
@absent:        jsr     DETAIL
                jsr     MODNAME
                lda     #<SBLOCKS
                ldx     #>SBLOCKS
                jsr     PUTS
                lda     DG_BLK
                asl     a
                asl     a
                asl     a
                asl     a
                jsr     PUTDEC
                lda     #'-'
                jsr     PUTC
                lda     DG_BLK
                asl     a
                asl     a
                asl     a
                asl     a
                ora     #$0F
                jsr     PUTDEC
                lda     #<SNOANS
                ldx     #>SNOANS
                jsr     PUTS
                jsr     CRLF
                bra     @nextm
@there:         ldx     DG_BLK
                lda     MODBIT,x
                ora     DG_MODS
                sta     DG_MODS
@nextm:         inc     DG_BLK
                lda     DG_BLK
                cmp     #4
                beq     @done
                jmp     @module
@done:          lda     #1
                sta     NB_WIN1
                lda     #DR_PASS
                jmp     STAGEEND

; "RAM x" for module DG_BLK.
MODNAME:
                lda     #<SRAM
                ldx     #>SRAM
                jsr     PUTS
                lda     DG_BLK
                clc
                adc     #'A'
                jmp     PUTC

; A the block before (or $FF to start): the next RAM block to test, in A,
; carry clear; carry set after the last. Bank 8 is passed over with the
; aperture, and a module that didn't answer.
NEXTBLK:
                inc     a
@chk:           cmp     #64
                bcs     @end
                ldx     DG_APER
                beq     @mod
                cmp     #32
                bcc     @mod
                cmp     #36
                bcs     @mod
                lda     #36
@mod:           pha
                lsr     a
                lsr     a
                lsr     a
                lsr     a
                tax
                lda     MODBIT,x
                and     DG_MODS
                bne     @ok
                pla                    ; the module's next: (module + 1) x 16
                inx
                txa
                asl     a
                asl     a
                asl     a
                asl     a
                bra     @chk
@ok:            pla
                jsr     ISSKIP         ; a block that answers as W0's isn't tested (MM2)
                bcc     @take
                inc     a
                bra     @chk
@take:          clc
@end:           rts

; Carry set if block A answers as W0's block (SKIPS). A kept.
ISSKIP:
                pha
                pha
                lsr     a
                lsr     a
                lsr     a
                tax
                pla
                and     #7
                tay
                lda     BIT8,y
                and     SKIPS,x
                cmp     #1
                pla
                rts

; Block A answers as W0's: kept out of stages 4-6 (SKIPS). A kept.
MARKSKIP:
                pha
                pha
                lsr     a
                lsr     a
                lsr     a
                tax
                pla
                and     #7
                tay
                lda     BIT8,y
                ora     SKIPS,x
                sta     SKIPS,x
                pla
                rts

; W1 on block A, and HW_PTR/HW_LEN its run: all of it, or past the first 512
; bytes if it is W0's. A kept; HW_KEY the block.
MAPRUN:
                sta     NB_WIN1
                sta     HW_KEY
                stz     HW_PTR
                ldx     #$40
                stz     HW_LEN
                ldy     #$40
                cmp     DG_W0
                bne     @set
                ldx     #$42
                ldy     #$3E
@set:           stx     HW_PTR + 1
                sty     HW_LEN + 1
                rts

; Block A failed this pass: DG_BAD's bit. A kept.
MARKBAD:
                pha
                pha
                lsr     a
                lsr     a
                lsr     a
                tax
                pla
                and     #7
                tay
                lda     BIT8,y
                ora     DG_BAD,x
                sta     DG_BAD,x
                pla
                rts

; Carry set if block A failed this pass. A kept.
ISBAD:
                pha
                pha
                lsr     a
                lsr     a
                lsr     a
                tax
                pla
                and     #7
                tay
                lda     BIT8,y
                and     DG_BAD,x
                cmp     #1             ; carry: not 0
                pla
                rts

; The library's failure (HW_BAD a CPU address in W1, HW_GOT, HW_WANT) in block
; DG_BLK as MEMFAULT's detail; only the stage's first eight are written.
LIBFAULT:
                lda     DG_BLK
                jsr     MARKBAD
                lda     DG_NDET
                cmp     #8
                bcs     @done
                lda     DG_BLK
                sta     DG_T0
                lda     HW_BAD
                sta     DG_T0 + 1
                lda     HW_BAD + 1
                sec
                sbc     #$40
                sta     DG_T0 + 2
                lda     HW_GOT
                sta     DG_MGOT
                lda     HW_WANT
                sta     DG_MWANT
                jsr     MEMFAULT
@done:          rts

; --- Stage 4: the address lines and the blocks --------------------------------

STAGE4:
                lda     #<SN4
                sta     DG_T0
                lda     #>SN4
                sta     DG_T0 + 1
                lda     #4
                jsr     STAGEBEG
                jsr     ALIASW0
                lda     #$FF
@w:             jsr     NEXTBLK
                bcs     @check
                sta     DG_BLK
                jsr     TAGW
                lda     DG_BLK
                bra     @w
@check:         lda     #$FF
@c:             jsr     NEXTBLK
                bcs     @done
                sta     DG_BLK
                jsr     TAGC
                lda     DG_BLK
                bra     @c
@done:          lda     #1
                sta     NB_WIN1
                lda     #DR_PASS
                jmp     STAGEEND

; Every block that answers as W0's (a byte written through W1 changes W0's
; own, at +$07, which nothing keeps) is said and kept out of stages 4-6:
; testing it would write over the run's zero page and stack (review MM2).
ALIASW0:
                lda     #$FF
@a:             jsr     NEXTBLK
                bcs     @done
                sta     DG_BLK
                cmp     DG_W0
                beq     @next
                sta     NB_WIN1
                lda     #$A5
                sta     $07
                lda     #$5A
                sta     $4007
                lda     $07
                cmp     #$A5
                beq     @next
                lda     DG_BLK
                jsr     MARKBAD
                jsr     MARKSKIP
                jsr     DETAIL
                lda     #<SBLOCK
                ldx     #>SBLOCK
                jsr     PUTS
                lda     DG_BLK
                jsr     PUTDEC
                lda     #<SASW0
                ldx     #>SASW0
                jsr     PUTS
                lda     DG_W0
                jsr     PUTDEC
                lda     #<SASW0B
                ldx     #>SASW0B
                jsr     PUTS
                jsr     CRLF
@next:          lda     DG_BLK
                bra     @a
@done:          lda     #1
                sta     NB_WIN1
                rts

; Block DG_BLK's tags through W1: its number at +0, and n XOR $FF XOR the
; number at +2^n, n 0-13; W0's block's first 512 bytes left alone.
TAGW:
                lda     DG_BLK
                sta     NB_WIN1
                cmp     DG_W0
                beq     @lines
                sta     $4000
@lines:         ldx     #13
@n:             lda     TAGHI,x
                ldy     DG_BLK
                cpy     DG_W0
                bne     @put
                cmp     #$42           ; below +$200 in W0's block: no
                bcc     @skip
@put:           sta     DG_PTR + 1
                lda     TAGLO,x
                sta     DG_PTR
                txa
                eor     #$FF
                eor     DG_BLK
                sta     (DG_PTR)
@skip:          dex
                bpl     @n
                rts

; The same read back; the first that's wrong in the block is the stage's
; detail, with what it says of a line.
TAGC:
                lda     DG_BLK
                sta     NB_WIN1
                cmp     DG_W0
                beq     @lines
                lda     $4000
                cmp     DG_BLK
                beq     @lines
                ldx     #$FF           ; +0
                stz     DG_T0 + 1
                stz     DG_T0 + 2
                sta     DG_MGOT
                lda     DG_BLK
                sta     DG_MWANT
                jmp     @wrong
@lines:         ldx     #13
@n:             lda     TAGHI,x
                ldy     DG_BLK
                cpy     DG_W0
                bne     @get
                cmp     #$42
                bcc     @skip
@get:           sta     DG_PTR + 1
                lda     TAGLO,x
                sta     DG_PTR
                txa
                eor     #$FF
                eor     DG_BLK
                sta     DG_MWANT
                lda     (DG_PTR)
                cmp     DG_MWANT
                bne     @bad
@skip:          dex
                bpl     @n
                rts
@bad:           sta     DG_MGOT
                lda     TAGLO,x
                sta     DG_T0 + 1
                lda     TAGHI,x
                sec
                sbc     #$40
                sta     DG_T0 + 2
@wrong:         stx     DG_T0 + 3      ; n, or $FF for +0
                lda     DG_BLK
                jsr     MARKBAD
                lda     DG_NDET
                cmp     #8
                bcs     @done
                lda     DG_BLK
                sta     DG_T0
                lda     DG_T0 + 3
                pha
                jsr     MEMFAULT
                pla
                tax
                jmp     WHATLINE
@done:          rts

; What a wrong tag says (X the line, $FF for +0; DG_MGOT read): a line, two
; lines, or two blocks as one.
WHATLINE:
                lda     DG_MGOT
                cpx     #$FF
                bne     @line
                cmp     #64            ; a block's number: that block answers here too
                bcs     @tagz
                pha
                lda     #<SAS1
                ldx     #>SAS1
                jsr     PUTS6
                lda     DG_BLK
                jsr     PUTDEC
                lda     #<SAND
                ldx     #>SAND
                jsr     PUTS
                pla
                jsr     PUTDEC
                lda     #<SAS2
                ldx     #>SAS2
                jsr     PUTS
                jmp     CRLF
@tagz:          eor     #$FF           ; line m's tag at +0: line m
                eor     DG_BLK
                cmp     #14
                bcs     @none
                pha
                lda     #<SLINE
                ldx     #>SLINE
                jsr     PUTS6
                pla
                jsr     PUTDEC
                jmp     CRLF
@line:          stx     DG_T0 + 3
                cmp     DG_BLK         ; +0's number at +2^n: line n
                bne     @two
                lda     #<SLINE
                ldx     #>SLINE
                jsr     PUTS6
                lda     DG_T0 + 3
                jsr     PUTDEC
                jmp     CRLF
@two:           eor     #$FF
                eor     DG_BLK
                cmp     #14
                bcs     @none
                pha
                lda     #<SLINES
                ldx     #>SLINES
                jsr     PUTS6
                lda     DG_T0 + 3
                jsr     PUTDEC
                lda     #<SANDA
                ldx     #>SANDA
                jsr     PUTS
                pla
                jsr     PUTDEC
                jmp     CRLF
@none:          rts

; Six blanks, then the string at A/X.
PUTS6:
                pha
                phx
                ldx     #6
@sp:            jsr     SPACE
                dex
                bne     @sp
                plx
                pla
                jmp     PUTS

; --- Stage 5: address in address ----------------------------------------------

STAGE5:
                lda     #<SN5
                sta     DG_T0
                lda     #>SN5
                sta     DG_T0 + 1
                lda     #5
                jsr     STAGEBEG
                lda     #$FF
@w:             jsr     NEXTBLK
                bcs     @check
                sta     DG_BLK
                jsr     MAPRUN
                jsr     HW_AIAW
                lda     DG_BLK
                bra     @w
@check:         lda     #$FF
@c:             jsr     NEXTBLK
                bcs     @done
                sta     DG_BLK
                jsr     MAPRUN
                jsr     HW_AIAC
                bcc     @ok
                jsr     LIBFAULT
@ok:            lda     DG_BLK
                bra     @c
@done:          lda     #1
                sta     NB_WIN1
                lda     #DR_PASS
                jmp     STAGEEND

; --- Stage 6: the march -------------------------------------------------------

STAGE6:
                lda     #<SN6
                sta     DG_T0
                lda     #>SN6
                sta     DG_T0 + 1
                lda     DG_OPT
                and     #DO_FULL
                beq     @named
                lda     #<SN6F
                sta     DG_T0
                lda     #>SN6F
                sta     DG_T0 + 1
@named:         lda     #6
                jsr     STAGEBEG
                ldx     #0             ; the background's index
@bg:            stx     DG_SPARE
                lda     #$FF
@b:             jsr     NEXTBLK
                bcs     @nextbg
                sta     DG_BLK
                jsr     MAPRUN
                ldx     DG_SPARE
                lda     BGS,x
                sta     HW_BG
                jsr     HW_MARCH
                bcc     @ok
                jsr     LIBFAULT
@ok:            lda     DG_BLK
                bra     @b
@nextbg:        ldx     DG_SPARE
                lda     DG_OPT
                and     #DO_FULL
                beq     @own
                inx
                cpx     #4
                bne     @bg
@own:           jsr     OWNW0
                lda     #1
                sta     NB_WIN1
                lda     #DR_PASS
                jmp     STAGEEND

; W0's block's first 512 bytes: the zero page and stack move to another block
; that passed (a copy, so the stack under this call is the same), then the old
; block's first 512 bytes are tested through W1 as the rest were.
OWNW0:
                lda     #$FF
@find:          jsr     NEXTBLK
                bcs     @nospare
                cmp     DG_W0
                beq     @find
                jsr     ISBAD
                bcs     @find
                sta     DG_SPARE
                sta     NB_WIN1
                ldx     #0
@copy:          lda     $0000,x
                sta     $4000,x
                lda     $0100,x
                sta     $4100,x
                inx
                bne     @copy
                ldx     DG_W0          ; (not on the stack: the copy is made)
                lda     DG_SPARE
                sta     NB_WIN0        ; the same bytes, so this call's stack is still here
                sta     DG_W0
                txa
                sta     DG_BLK
                sta     NB_WIN1
                sta     HW_KEY
                jsr     RUN512
                jsr     HW_AIAW
                jsr     RUN512
                jsr     HW_AIAC
                bcc     @march
                jmp     LIBFAULT
@march:         ldx     #0
@bg:            stx     DG_T0 + 3
                jsr     RUN512
                lda     BGS,x
                sta     HW_BG
                jsr     HW_MARCH
                bcc     @ok
                jmp     LIBFAULT
@ok:            ldx     DG_T0 + 3
                lda     DG_OPT
                and     #DO_FULL
                beq     @done
                inx
                cpx     #4
                bne     @bg
@done:          rts
@nospare:       jsr     DETAIL
                lda     #<SNOSPARE
                ldx     #>SNOSPARE
                jsr     PUTS
                jmp     CRLF

RUN512:         stz     HW_PTR
                lda     #$40
                sta     HW_PTR + 1
                stz     HW_LEN
                lda     #$02
                sta     HW_LEN + 1
                ldx     DG_T0 + 3
                rts

; --- Break, and the fatal ends ------------------------------------------------

; Break (NMI) or BRK while the core runs: VEC is on this block, and the stub at
; $8020 mapped it. No stack is trusted: BREAK said, what the run changed put
; back, and the boot, as a key at the end would.
BRKH:
                sei
                cld
                ldx     #$FF
                txs
                SERS    SBREAK
                PUTBACKM
                jmp     DG_TBOOT       ; (BOOT puts W0 back)

; Stage 1 found block 23, this code, wrong: $81, the block, round again.
FATAL1:
                lda     #$81
                sta     CONSOLE_DISPLAY
                HALFSEC
                lda     #23
                sta     CONSOLE_DISPLAY
                HALFSEC
                lda     #$00
                sta     CONSOLE_DISPLAY
                HALFSEC
                jmp     FATAL1

; Stage 2 found no block for a zero page and stack: $82, block 0, and the
; bits block 0's first byte gets wrong now (a 0 written, read back).
FATAL2:
                lda     #$82
                sta     CONSOLE_DISPLAY
                HALFSEC
                lda     #0
                sta     CONSOLE_DISPLAY
                HALFSEC
                stz     NB_WIN1
                stz     $4000
                lda     $4000
                sta     CONSOLE_DISPLAY
                HALFSEC
                jmp     FATAL2

        .include "diagcon.inc"

; --- Tables and text ----------------------------------------------------------

MODFIRST:       .byte   0, 16, 32, 48  ; each RAM module's first block
MODFIRSTA:      .byte   0, 16, 36, 48  ; with the aperture over 32-35
MODBIT:         .byte   $01, $02, $04, $08
BIT8:           .byte   $01, $02, $04, $08, $10, $20, $40, $80
BGS:            .byte   $00, $55, $33, $0F
TAGLO:
.repeat 14, n
                .byte   <($4000 + (1 << n))
.endrepeat
TAGHI:
.repeat 14, n
                .byte   >($4000 + (1 << n))
.endrepeat

; The CGA's 16 colours, as video.s has them (GGGGBBBB, then 0000RRRR)
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

; A stage's line as stages 1 and 2 write it with no RAM: " n  NAME", dots to
; the result's column
.macro STLINE text
                .byte   text, ' '
                .res    DG_RESCOL - 2 - .strlen(text), '.'
                .byte   ' ', 0
.endmacro

STITLE:         .byte   "OO-1 ROM DIAGNOSTICS 0.1", 0
SPROMPT:        .byte   "DIAGNOSTICS: PRESS CONT OR Y WITHIN 5 S", 0
SPROMPTS:       .byte   $0D, "DIAGNOSTICS: PRESS CONT OR Y WITHIN 5 S", $0D, 0
SBOOTING:       .byte   "BOOTING", $0D, 0
SNOTICK:        .byte   "THE CLOCK CHIP DIDN'T TICK: 7 S COUNTED BY THE CPU", $0D, 0
SCRLF:          .byte   $0D, 0
SL1:            STLINE  " 1  FLASH BLOCKS 21-31"
SPASSCR:        .byte   "PASS", $0D, 0
SFAILCR:        .byte   "FAIL", $0D, 0
SFLASHB:        .byte   "      FLASH BLOCK $", 0
SSUMBAD:        .byte   ": ITS SUM IS WRONG", $0D, 0
SBREAK:         .byte   $0D, "BREAK", $0D, 0
NAME2:          .byte   "ZERO PAGE AND STACK: BLOCK "
NAME2END:
SN3:            .byte   "CPU W65C02S; DATA LINES, RAM A B C D", 0
SN4:            .byte   "ADDRESS LINES AND BLOCKS", 0
SN5:            .byte   "ADDRESS IN ADDRESS, ALL RAM", 0
SN6:            .byte   "MARCH, ALL RAM, BACKGROUND 00", 0
SN6F:           .byte   "MARCH, ALL RAM, BACKGROUNDS 00 55 33 0F", 0
SCPU:           .byte   "THE CPU ANSWERS XBA: A 65C816, NOT A W65C02S", 0
SRAM:           .byte   "RAM ", 0
SDATA:          .byte   ": DATA LINES: READ ", 0
SBLOCKS:        .byte   " (BLOCKS ", 0
SNOANS:         .byte   "): NOT ANSWERING", 0
SLINE:          .byte   "ADDRESS LINE A", 0
SLINES:         .byte   "ADDRESS LINES A", 0
SANDA:          .byte   " AND A", 0
SAS1:           .byte   "BLOCKS ", 0
SAND:           .byte   " AND ", 0
SAS2:           .byte   " ANSWER AS ONE", 0
SASW0:          .byte   " ANSWERS AS BLOCK ", 0
SASW0B:         .byte   ", W0'S: NOT TESTED", 0
SNOSPARE:       .byte   "NO OTHER BLOCK PASSED: W0'S FIRST 512 BYTES NOT TESTED", 0
SBIOS:          .byte   "BIOS 0.14         ", 0
SPASSH:         .byte   "PASS ", 0
SQUICK:         .byte   "QUICK", 0
SFULL:          .byte   "FULL", 0
SLOOP:          .byte   " LOOP", 0
SASK:           .byte   " ASK", 0

.segment "D23BSS"
NAMEBUF:        .res    40
SKIPS:          .res    8              ; bit b%8 of byte b/8: block b answers as W0's
S2BLOCK:        .res    1              ; the block stage 2 chose first

; The flash's sums, blocks 21-31 (block 23's without this page), written by
; romsum.py after ld65: the low bytes, then the high.
.segment "D23SUMS"
SUMLO:          .res    16, 0
SUMHI:          .res    16, 0

.segment "D23VEC"
                .word   DG_BREAK       ; NMI
                .word   DG_BREAK       ; RESET (never: /RESET puts VEC back on block 31)
                .word   DG_BREAK       ; IRQ/BRK
