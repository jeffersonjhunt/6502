; L4TEST: OOLIB's L4 (hw/oolib.md, "The plan"): function 83 and the W1
; contract, for test_oodos_video.cpp and test_oodos_load.cpp. The command
; tail's first letter opens an area at AREA ($2000, in W0's block 0) or not:
;
;   2   256 bytes (FT_OPEN's size 0): layout 2
;   1   16 bytes: layout 1
;   X   none
;
; and says OPEN aa xx. Then it waits for a command, spinning with no BDOS
; call: the test puts a letter in MBOX ($2100), its arguments after it, and
; the program clears it, does it, and prints a line, "name aa xx" being A
; and X as the call returned them. The test builds the blocks at KBLK
; ($2110) itself. All of the program, its stack, its area and its blocks are
; below $4000, so W1 may hold any block (the W1 contract):
;
;   K   K aa xx: function 83 with the block at KBLK (kcall to kret: timed)
;   Z   Z aa xx: function 83 with X = 0, A = MBOX + 1's byte
;   N   N aa xx: function 81 with the block at KBLK
;   G   G aa xx: function MBOX + 1, A/X = MBOX + 2-3
;   T   T aa xx: function 80 with the block at KBLK
;   F   F aa xx: function 80's FT_INFO, X = 0
;   I   INFO aa xx and DM_INFO's 16 bytes
;   M   M ww bb: W1 := MBOX + 1's block, then a function 9 call from W0;
;       ww NB_WIN1 after it, bb the byte at $4000 through W1
;   P   P 00 00: MBOX + 2's byte to $4000-$40FF, through whatever W1 has
;   V   V ww 00: NB_WIN1 now
;   B   B 01 00: W1 back on block 1
;   H   H aa xx: the frame hook (HOOK) set, with the windows as they are
;   O   O aa xx: F_OPEN of the FCB at FCB ($2140)
;   R   R aa xx: the DMA address $4000, then F_READRAND of FCB
;   S   S aa xx: MBOX + 1's count (44), a segment MBOX + 2-3 (51), DMA
;       address 0, then MBOX + 4 random reads of FCB from its R0-R2 on,
;       each 128 records (the last's A); 51 and 44 put back (scall to sret)
;   W   the warm boot (JMP WBOOT)
;   D   spins on in a ^C-able function 10 (^C there ends it)
;
; HOOK counts its calls at HCNT ($2181) and keeps the W1 it ran with at HW1
; ($2180).

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $2000
MBOX    = $2100
KBLK    = $2110
FCB     = $2140
HW1     = $2180
HCNT    = $2181
WIN1    = $C681                         ; the north bridge's W1 (backplane.inc's NB_WIN1)

        .zeropage
t0:     .res    2
sayx:   .res    1
left:   .res    1

        .code
.macro SAY label
        .local  s
        pha
        lda     #<s
        sta     t0
        lda     #>s
        sta     t0 + 1
        pla
        jsr     say
        .pushseg
        .rodata
s:      .byte   label, " $"
        .popseg
.endmacro

start:  stz     MBOX
        stz     HCNT
        stz     HW1
        lda     BP_DMA + 1              ; the tail's first letter, after its space
        cmp     #' '
        bne     @l
        lda     BP_DMA + 2
@l:     cmp     #'X'
        beq     wait
        ldx     #0                      ; 2: 256 bytes
        cmp     #'2'
        beq     @size
        ldx     #16
@size:  stx     oblk + 3
        lda     #<oblk
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "OPEN"
        ; fall through

; The commands, MBOX's letter each.
wait:   lda     MBOX
        beq     wait
        stz     MBOX
        ldx     #0
@find:  cmp     cmds,x
        beq     @go
        inx
        inx
        inx
        cpx     #CMDS_LEN
        bne     @find
        bra     wait
@go:    lda     #>(done - 1)            ; the command returns to done
        pha
        lda     #<(done - 1)
        pha
        lda     cmds + 2,x
        pha
        lda     cmds + 1,x
        pha
        rts                             ; to the command
.macro CMD letter, routine
        .byte   letter
        .word   routine - 1
.endmacro
done:   jmp     wait

; Print the label at t0, then A and X.
say:    pha
        stx     sayx
        lda     t0
        ldx     t0 + 1
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sayx
        jsr     HEX2
        jmp     CRLF

c_k:    lda     #<KBLK
        ldx     #>KBLK
        ldy     #O_DMA
kcall:  jsr     BDOS
kret:   SAY     "K"
        rts

c_z:    lda     MBOX + 1
        ldx     #0
        ldy     #O_DMA
        jsr     BDOS
        SAY     "Z"
        rts

c_g:    lda     MBOX + 2
        ldx     MBOX + 3
        ldy     MBOX + 1
        jsr     BDOS
        SAY     "G"
        rts

c_n:    lda     #<KBLK
        ldx     #>KBLK
        ldy     #O_INPUT
        jsr     BDOS
        SAY     "N"
        rts

c_t:    lda     #<KBLK
        ldx     #>KBLK
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "T"
        rts

c_f:    lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "F"
        rts

c_info: lda     #<iblk
        ldx     #>iblk
        ldy     #O_DMA
        jsr     BDOS
        pha
        lda     #<sinfo
        sta     t0
        lda     #>sinfo
        sta     t0 + 1
        pla
        stx     sayx
        pha
        lda     t0
        ldx     t0 + 1
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sayx
        jsr     HEX2
        jsr     SPACE
        lda     #<(iblk + 1)
        sta     PTR
        lda     #>(iblk + 1)
        sta     PTR + 1
        ldx     #16
        jsr     HEXN
        jmp     CRLF

c_m:    lda     MBOX + 1
        sta     WIN1
        lda     #<dot
        ldx     #>dot
        ldy     #C_WRITESTR
        jsr     BDOS
        lda     WIN1
        ldx     $4000
        SAY     "M"
        rts

c_p:    lda     MBOX + 2
        ldx     #0
@w:     sta     $4000,x
        inx
        bne     @w
        lda     #0
        SAY     "P"
        rts

c_v:    lda     WIN1
        ldx     #0
        SAY     "V"
        rts

c_b:    lda     #1
        sta     WIN1
        ldx     #0
        SAY     "B"
        rts

c_h:    lda     #<hblk
        ldx     #>hblk
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "H"
        rts

c_o:    lda     #<FCB
        ldx     #>FCB
        ldy     #F_OPEN
        jsr     BDOS
        SAY     "O"
        rts

c_r:    lda     #<$4000
        ldx     #>$4000
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<FCB
        ldx     #>FCB
        ldy     #F_READRAND
        jsr     BDOS
        SAY     "R"
        rts

c_s:    lda     MBOX + 1
        ldy     #F_MULTISEC
        jsr     BDOS
        lda     MBOX + 2
        ldx     MBOX + 3
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #0
        tax
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     MBOX + 4
        sta     left
scall:
@read:  lda     #<FCB
        ldx     #>FCB
        ldy     #F_READRAND
        jsr     BDOS
        pha
        clc                             ; the next 128 records: R0-R2 and the segment on
        lda     FCB + 33
        adc     #128
        sta     FCB + 33
        bcc     @seg
        inc     FCB + 34
@seg:   clc                             ; 16 KB: 1,024 paragraphs
        lda     MBOX + 3
        adc     #4
        sta     MBOX + 3
        ldx     MBOX + 3
        lda     MBOX + 2
        ldy     #F_DMASEG
        jsr     BDOS
        pla
        dec     left
        bne     @read
sret:   pha
        lda     #1
        ldy     #F_MULTISEC
        jsr     BDOS
        lda     #$FF
        tax
        ldy     #F_DMASEG
        jsr     BDOS
        pla
        ldx     #0
        SAY     "S"
        rts

c_wboot:
        jmp     WBOOT

c_line: lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        jsr     CRLF
        lda     #0
        tax
        SAY     "LINEOK"
        rts

; The frame hook: its windows the ones the program had when it set it.
HOOK:   lda     WIN1
        sta     HW1
        inc     HCNT
        rts

        .rodata
cmds:   CMD     'K', c_k
        CMD     'Z', c_z
        CMD     'N', c_n
        CMD     'G', c_g
        CMD     'T', c_t
        CMD     'F', c_f
        CMD     'I', c_info
        CMD     'M', c_m
        CMD     'P', c_p
        CMD     'V', c_v
        CMD     'B', c_b
        CMD     'H', c_h
        CMD     'O', c_o
        CMD     'R', c_r
        CMD     'S', c_s
        CMD     'W', c_wboot
        CMD     'D', c_line
CMDS_LEN = * - cmds
sinfo:  .byte   "INFO $"
dot:    .byte   13, 10, "$"
hblk:   .byte   FT_FRAMEHOOK
        .word   HOOK

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0
iblk:   .byte   DM_INFO
        .res    16, $EE
lbuf:   .byte   20, 0
        .res    20

        .include "print.inc"
