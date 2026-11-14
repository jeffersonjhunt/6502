; The ROM's diagnostics (hw/diag.md, plan 2): what blocks 21, 22 and 23 share.
;
; Each block is seen at $8000-$BFFF through W2, one at a time, and starts with
; the same header (DG_HEADER below):
;
;   $8000  "OOHWDIAG", the signature the BIOS and the hand-offs check
;   $8008  the block's place: 0 block 23, 1 block 22, 2 block 21
;   $8009  the build's version (DG_VERSION), the same in all three
;   $800A  the gate: STA NB_WIN2, then JMP ($8010,X). Its bytes are the same
;          in every block, so the fetch after the STA comes from the block
;          just mapped and takes that block's entry X: a hand-off needs no RAM
;   $8010  the block's entries, three words (X = 0, 2, 4)
;   $801C  the BIOS version this build was linked with: major, minor (bios.s's
;          BIOS_VMAJ, BIOS_VMIN, below). The BIOS checks the signature, DG_VERSION
;          and this before it enters block 23 (RESET, CHKSIG)
;   $8020  the Break stub: LDA #DG_B23, STA NB_WIN2; block 23 goes on at
;          $8025 to its handler. VEC is on block 23 while the core runs, and
;          its NMI and IRQ vectors are $8020, whichever block W2 holds
;
; What the run keeps across the hand-offs is here, at fixed addresses in the
; core's zero page: the first 512 bytes of the RAM block stage 2 chose for
; W0, tested before anything is kept there. $00-$07 are left alone; the
; library's own zero page and variables (DZP, DBSS in bios.cfg) are block
; 23's, from $40 and $0100; the stack is $0180-$01FF.

.ifndef DIAG_INC
DIAG_INC = 1

DG_VERSION      = 1                    ; ROM DIAGNOSTICS 0.1

; The BIOS's version, the one place it is set (review NN4): bios.s's greeting,
; the diagnostics' header line and the BIOS block 23 records (DG_LINKED) are all
; made from these
BIOS_VMAJ       = 0
BIOS_VMIN       = 14
.define BIOS_VERSION .sprintf("%d.%d", BIOS_VMAJ, BIOS_VMIN)
DG_B21          = NB_ROM | NB_RO | 21  ; the three blocks as W2 maps them
DG_B22          = NB_ROM | NB_RO | 22
DG_B23          = NB_ROM | NB_RO | 23
DG_FLASH31      = NB_ROM | NB_RO | 31  ; the BIOS, W3's and VEC's at reset
DG_GATE         = $800A
DG_ENTRIES      = $8010
DG_BREAK        = $8020
DG_LINKED       = $801C                ; the BIOS version it was linked with: major, minor

; The core's ways back into the BIOS: fixed trampolines at the top of block 31,
; just under the vectors, outside the jump table (bios.s asserts them), so the
; core never jumps to an address a later BIOS could move
DG_TBOOT        = $FFF4                ; JMP BOOT: the boot, as after a reset (past the switch check)
DG_TMON         = $FFF7                ; JMP MONBOOT: the BIOS copied, the greeting, the monitor

; Block 23's entries (X)
DE_PROMPT       = 0                    ; the reset's prompt (switch 19 up), no RAM: CONT or Y within 5 s
DE_GO           = 2                    ; stage 0 on, the caller has asked already ($C812, $C815)
DE_LOOP         = 4                    ; the loop's next pass: stage 3 on
; Block 22's and 21's
DE_START        = 0                    ; their first stage

; P3's breakpoint address (hw/diag.md, plan 2, LL1): the BIOS's RESET turns the
; breakpoint off when BPADDR holds it, and only then
DG_P3ADDR       = $BD1A5               ; RAM block 47, +$11A5

; WAIT3, the options' way through stages 0-2 (which keep no RAM): bit 0 set
; by $C815 (a program's entry: the switches don't count), bit 1 set by stage
; 1 when a flash block's sum is wrong, bits 6 and 7 a program's full and
; interactive. Wait states are reserved for turbo and do nothing at /2
; (hw/design.md); every /RESET clears WAIT3. Bits 0-1 are module C's (the
; Disk II, which the core never touches but for its ID), 6-7 module F's.
DW_PROGRAM      = $01
DW_FLASHBAD     = $02
DW_FULL         = $40
DW_ASK          = $80

; The run's state, in the core's zero page
DG_OPT          = $10                  ; DO_* below
DG_W0           = $11                  ; the block W0 holds
DG_PASS         = $12                  ; 2 bytes: the pass, from 1
DG_NFAIL        = $14                  ; stages failed this pass
DG_FSTAGE       = $15                  ; the first failure's stage (0: none yet)
DG_FBLK         = $16                  ; its block
DG_FOFS         = $17                  ; 2 bytes: its offset in the block
DG_FGOT         = $19                  ; what was read
DG_FWANT        = $1A                  ; what was written
DG_MODS         = $1B                  ; bit n: RAM module n answered (stage 3)
DG_STAGE        = $1C                  ; the stage running
DG_NDET         = $1D                  ; detail lines written this stage
DG_ROW          = $1E                  ; the screen's cursor
DG_COL          = $1F
DG_SCR          = $20                  ; bit 7: a revision 2 video card, so a screen; bit 6: write to it now
DG_APER         = $21                  ; 1: bank 8 is the video card's aperture (revision 2), so not RAM
DG_KEY          = $22                  ; the key that ended the run
DG_PTR          = $24                  ; 2 bytes: a string
DG_BLK          = $26                  ; the block a loop is on
DG_T0           = $27                  ; scratch, 4 bytes
DG_SPARE        = $2B                  ; the block W0 moves to for its own test
DG_MGOT         = $2C                  ; a memory fault's byte read (MEMFAULT)
DG_MWANT        = $2D                  ; and written
DG_SHOWN        = $23                  ; the run's first detail is on the screen
DG_N16          = $2E                  ; 2 bytes: PUTDEC16's number
DG_RES          = $30                  ; 16 bytes: stage n's result, DR_*
DG_BAD          = $08                  ; 8 bytes: bit b%8 of byte b/8, RAM block b failed this pass

DO_FULL         = $01
DO_LOOP         = $02
DO_ASK          = $04
DO_PROGRAM      = $80                  ; entered by $C815: the options are the program's

DR_NONE         = 0                    ; not run (yet)
DR_PASS         = 1
DR_FAIL         = 2
DR_SKIP         = 3

; The screen's form: stage n's line on row n + 2, the first failure on row
; 18, the result on row 20, the keys on row 21
DG_ROWDET       = 18
DG_ROWRES       = 20
DG_ROWKEYS      = 21

DG_TEXT         = $002000              ; the text layer's cells in card memory (video.s's TEXT_CARD)
DG_FONT         = $000400
DG_ATTR         = $07
DG_COLS         = 80
DG_ROWS         = 30
DG_RESCOL       = 60                   ; a stage line's result

; The header every block starts with: its signature, place and version, the
; gate, its entries (up to eight), and the Break stub.
.macro DG_HEADER place, e0, e1, e2
        .byte   "OOHWDIAG"
        .byte   place, DG_VERSION
        sta     NB_WIN2                ; $800A: the gate
        jmp     (DG_ENTRIES,x)
        .word   e0
.ifblank e1
        .word   0
.else
        .word   e1
.endif
.ifblank e2
        .word   0
.else
        .word   e2
.endif
        .res    6, $FF
        .byte   BIOS_VMAJ, BIOS_VMIN   ; $801C: the BIOS this was linked with
        .res    2, $FF
        lda     #DG_B23                ; $8020: the Break stub
        sta     NB_WIN2
.endmacro


; What a run changed, back as /RESET leaves it, with no stack (so Break's too,
; and the end's: diagcon.inc's PUTBACK is this and an RTS): the aperture's
; slots, the port, the DMA controller's CONTROL, the sound card's 6510
; stopped, the VIA (ACR 0, timer 1 a one-shot left to run out, its flags
; cleared and its interrupts off), WAIT0-3 (the diagnostics own WAIT3, and
; zero every wait state on the way out), the lights, VEC on the flash's top
; block and W1 on block 1. W0 and W2 are the BIOS's to put back: its BOOT
; and MONBOOT (DG_TBOOT, DG_TMON) start with W0.
.macro PUTBACKM
        .local slot, via
                ldx     #6
slot:           txa
                lsr     a
                sta     VIDEO_SLOT0,x
                stz     VIDEO_SLOT0 + 1,x
                dex
                dex
                bpl     slot
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                lda     #$70
                sta     DMA_CONTROL
                lda     SOUND_ID
                and     #$F0
                cmp     #SOUND_TYPE
                bne     via
                lda     SOUND_CTRL
                and     #<~SOUND_RUN
                sta     SOUND_CTRL
via:            stz     VIA_ACR        ; timer 1 one-shot; then started on a count it ends at once
                lda     #2
                sta     VIA_T1CL
                stz     VIA_T1CH
                nop
                nop
                nop
                nop
                lda     #$7F
                sta     VIA_IER
                sta     VIA_IFR
                stz     NB_WAIT0
                stz     NB_WAIT1
                stz     NB_WAIT2
                stz     NB_WAIT3
                stz     CONSOLE_DISPLAY
                stz     CONSOLE_CTRL
                lda     #DG_FLASH31
                sta     NB_VEC
                lda     #1
                sta     NB_WIN1
.endmacro

.endif
