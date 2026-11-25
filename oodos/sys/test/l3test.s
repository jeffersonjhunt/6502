; L3TEST: OOLIB's L3 (hw/oolib.md, "The plan"): function 82, on sound card
; revision 3, for test_oodos_sound.cpp. The command tail's first letter
; opens an area at AREA ($2000, in W0's block 0) or not:
;
;   2   256 bytes (FT_OPEN's size 0): layout 2, 16 slots
;   1   16 bytes: layout 1
;   X   none
;
; and says OPEN aa xx. Then it waits for a command, spinning with no BDOS
; call: the test puts a letter in MBOX ($2100) and the program clears it,
; does it, and prints a line, "name aa xx" being A and X as the BDOS
; returned them. The test builds function 82's blocks itself, at KBLK
; ($2110), and puts any data the block names in the program's memory:
;
;   K   K aa xx: function 82 with the block at KBLK
;   Z   Z aa xx: function 82 with X = 0, A = MBOX + 1's byte
;   S   ST aa xx and the status block's 19 bytes (SN_STATUS, SN_STATLEN)
;   I   INFO aa xx and the info block's 3 bytes (SN_INFO)
;   W   the warm boot (JMP WBOOT)
;   D   spins on in a ^C-able function 10 (^C there ends it)
;   G   G aa xx: BEL written to the console (function 2)
;   L   L1 aa xx and L2 aa xx: function 82 with the block at KBLK, then at once
;       with the block at KBLK2 ($2160)
;   B   B aa xx: function 82 with a block just past the TPA, on the BDOS's jump at $BFFD, running
;       into the I/O hole at $C000: MBOX + 3's count of bytes from KBLK
;       copied to MBOX + 1-2's address. That is where the BDOS's jump
;       ($BFFD) lives, so its three bytes are kept, the stub called through
;       their address, and put back before anything else
;   F   F aa xx: function 80's FT_INFO, X = 0 (OOLIB's interface version)
;   R   R aa xx: a feeder as fast as a program can be (layout 2): MBOX + 1's
;       count of buffers given, the block at KBLK each time, each the moment
;       the area's EV_SBUFS shows the next one (0, 1 in turn, from MBOX +
;       2's) free; aa xx the last call's. No BDOS call while it waits

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $2000
MBOX    = $2100
KBLK    = $2110
KBLK2   = $2160

        .zeropage
t0:     .res    2
sayx:   .res    1

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
; "name aa xx " and n bytes at addr, one line.
.macro SAYDUMP label, addr, n
        .local  s
        pha
        lda     #<s
        sta     t0
        lda     #>s
        sta     t0 + 1
        pla
        jsr     saynl
        lda     #<(addr)
        sta     PTR
        lda     #>(addr)
        sta     PTR + 1
        ldx     #n
        jsr     HEXN
        jsr     CRLF
        .pushseg
        .rodata
s:      .byte   label, " $"
        .popseg
.endmacro

start:  stz     MBOX
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
saynl:  pha
        stx     sayx
        lda     t0
        ldx     t0 + 1
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sayx
        jsr     HEX2
        jmp     SPACE

f82:    ldy     #O_SOUND
        jmp     BDOS

c_k:    lda     #<KBLK
        ldx     #>KBLK
        jsr     f82
        SAY     "K"
        rts

c_l:    lda     #<KBLK
        ldx     #>KBLK
        jsr     f82
        sta     bres
        stx     bres + 1
        lda     #<KBLK2
        ldx     #>KBLK2
        jsr     f82
        pha
        phx
        lda     bres
        ldx     bres + 1
        SAY     "L1"
        plx
        pla
        SAY     "L2"
        rts

c_r:    lda     MBOX + 2
        sta     rnext
@one:   lda     MBOX + 1
        beq     @out
        ldx     rnext
@wait:  lda     AREA + EV_SBUFS
        and     rbit,x
        beq     @wait
        lda     #<KBLK
        ldx     #>KBLK
        jsr     f82
        sta     bres
        stx     bres + 1
        lda     rnext
        eor     #1
        sta     rnext
        dec     MBOX + 1
        bra     @one
@out:   lda     bres
        ldx     bres + 1
        SAY     "R"
        rts
rbit:   .byte   1, 2

c_f:    lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "F"
        rts

c_z:    lda     MBOX + 1
        ldx     #0
        jsr     f82
        SAY     "Z"
        rts

c_st:   lda     #<sblk
        ldx     #>sblk
        jsr     f82
        SAYDUMP "ST", sblk + 1, SN_STATLEN
        rts

c_info: lda     #<iblk
        ldx     #>iblk
        jsr     f82
        SAYDUMP "INFO", iblk + 1, 3
        rts

c_wboot:
        jmp     WBOOT

c_g:    lda     #7
        ldy     #C_WRITE
        jsr     BDOS
        pha
        phx
        jsr     CRLF
        plx
        pla
        SAY     "G"
        rts

c_b:    ldx     #2
@keep:  lda     $BFFD,x
        sta     bsave,x
        dex
        bpl     @keep
        lda     MBOX + 1
        sta     t0
        lda     MBOX + 2
        sta     t0 + 1
        ldy     MBOX + 3
@copy:  dey
        lda     KBLK,y
        sta     (t0),y
        tya
        bne     @copy
        lda     MBOX + 1
        ldx     MBOX + 2
        ldy     #O_SOUND
        jsr     bcall
        sta     bres
        stx     bres + 1
        ldx     #2
@back:  lda     bsave,x
        sta     $BFFD,x
        dex
        bpl     @back
        lda     bres
        ldx     bres + 1
        SAY     "B"
        rts
bcall:  jmp     (bsave + 1)             ; the stub, as $BFFD's jump goes

c_line: lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        jsr     CRLF
        SAY     "LINEOK"
        rts

        .rodata
cmds:   CMD     'K', c_k
        CMD     'Z', c_z
        CMD     'S', c_st
        CMD     'I', c_info
        CMD     'W', c_wboot
        CMD     'D', c_line
        CMD     'G', c_g
        CMD     'L', c_l
        CMD     'B', c_b
        CMD     'F', c_f
        CMD     'R', c_r
CMDS_LEN = * - cmds
        .data
bsave:  .res    3
bres:   .res    2
rnext:  .res    1
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0
sblk:   .byte   SN_STATUS
        .res    SN_STATLEN, $EE
iblk:   .byte   SN_INFO
        .res    3, $EE
lbuf:   .byte   20, 0
        .res    20

        .include "print.inc"
