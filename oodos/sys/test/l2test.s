; L2TEST: OOLIB's L2 (hw/oolib.md, "The plan"): the event area's layout 2
; and function 81, for test_oodos_input.cpp. The command tail's first
; letter opens an area at AREA ($2000, in W0's block 0) or not:
;
;   2   256 bytes (FT_OPEN's size 0): layout 2, 16 slots
;   4   160 bytes: layout 2, 4 slots (3 events wait at most)
;   1   16 bytes: layout 1
;   X   none
;   C   console mode $000A (no ^S, ^Q, ^P handling; no ^C ending the
;       program: function 109), then 256 bytes; then function 10 reads a
;       line: LINE and its count and characters; then the commands
;
; and says OPEN aa xx (FT_OPEN's A and X). Then it waits for a command,
; spinning with no BDOS call: the test puts a letter in MBOX ($2100) and
; the program clears it, does it, and prints a line, "name aa xx" being A
; and X as the BDOS returned them:
;
;   I   INFO aa xx (81's IN_INFO, no block); INFOB and its block's 2 bytes;
;       FINFO aa xx (80's FT_INFO); FINFOB and its 4
;   N   NEXT aa xx and the block's 8 bytes (IN_NEXT; $EE first)
;   F   FLUSH aa xx
;   G   MAP aa xx and the four controllers' hardware
;   S   SWAP aa xx: controllers 1 and 3 swapped
;   A   MOUSE aa xx: absolute, limits 319 and 199
;   R   MOUSE aa xx: relative, limits 639 and 479
;   L   LEDS aa xx: 5 (Num Lock and Scroll Lock)
;   P   REPEAT aa xx: $25
;   B   the refusals: BAD1 aa xx (operation IN_OPS), BAD2 (IN_SWAP 1 and
;       5), BAD3 (IN_NEXT with no block), BAD4 (IN_MAP with no block),
;       BAD5 (IN_MOUSE mode 2)
;   Q   CLOSE aa xx (FT_CLOSE), then waits on
;   W   the warm boot (JMP WBOOT), the area as it is
;   D   spins on in a ^C-able function 10 (^C there ends it: console mode
;       left as the CCP has it, so case C mustn't use it)

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $2000
MBOX    = $2100

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
; Print the label, then n bytes at addr.
.macro DUMP label, addr, n
        .local  s
        lda     #<s
        ldx     #>s
        jsr     PUTS
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
.macro CALL81 blk
        lda     #<(blk)
        ldx     #>(blk)
        jsr     f81
.endmacro

        .code
start:  stz     MBOX
        lda     BP_DMA + 1              ; the tail's first letter, after its space
        cmp     #' '
        bne     @l
        lda     BP_DMA + 2
@l:     cmp     #'X'
        beq     wait
        cmp     #'C'
        bne     @open
        lda     #$0A                    ; CM_NOSTOP | CM_NOCTLC
        ldx     #0
        ldy     #C_MODE
        jsr     BDOS
        lda     #'2'
@open:  ldx     #0                      ; 2: 256 bytes
        cmp     #'2'
        beq     @size
        ldx     #160
        cmp     #'4'
        beq     @size
        ldx     #16
@size:  stx     oblk + 3
        lda     BP_DMA + 1
        cmp     #' '
        bne     @l2
        lda     BP_DMA + 2
@l2:    pha
        lda     #<oblk
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "OPEN"
        pla
        cmp     #'C'
        bne     wait
        lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        jsr     CRLF
        DUMP    "LINE", lbuf + 1, 3
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

; ---- helpers ---------------------------------------------------------------

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

f81:    ldy     #O_INPUT
        jmp     BDOS
f80:    ldy     #O_FRAME
        jmp     BDOS

; ---- the commands ----------------------------------------------------------

c_info: lda     #IN_INFO
        ldx     #0
        jsr     f81
        SAY     "INFO"
        CALL81  iblk
        DUMP    "INFOB", iblk + 1, 2
        lda     #FT_INFO
        ldx     #0
        jsr     f80
        SAY     "FINFO"
        lda     #<fiblk
        ldx     #>fiblk
        jsr     f80
        DUMP    "FINFOB", fiblk + 1, 4
        rts

c_next: ldx     #7
        lda     #$EE
@fill:  sta     nblk + 1,x
        dex
        bpl     @fill
        CALL81  nblk
        SAYDUMP "NEXT", nblk + 1, 8
        rts

c_flush:
        lda     #IN_FLUSH
        ldx     #0
        jsr     f81
        SAY     "FLUSH"
        rts

c_map:  CALL81  mblk
        SAYDUMP "MAP", mblk + 1, 4
        rts

c_swap: CALL81  sblk
        SAY     "SWAP"
        rts

c_abs:  CALL81  ablk
        SAY     "MOUSE"
        rts

c_rel:  CALL81  rblk
        SAY     "MOUSE"
        rts

c_leds: CALL81  lblk
        SAY     "LEDS"
        rts

c_rep:  CALL81  pblk
        SAY     "REPEAT"
        rts

c_bad:  lda     #IN_OPS
        ldx     #0
        jsr     f81
        SAY     "BAD1"
        CALL81  bswap
        SAY     "BAD2"
        lda     #IN_NEXT
        ldx     #0
        jsr     f81
        SAY     "BAD3"
        lda     #IN_MAP
        ldx     #0
        jsr     f81
        SAY     "BAD4"
        CALL81  bmouse
        SAY     "BAD5"
        rts

c_close:
        lda     #FT_CLOSE
        ldx     #0
        jsr     f80
        SAY     "CLOSE"
        rts

c_wboot:
        jmp     WBOOT

c_line: lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        jsr     CRLF
        SAY     "LINEOK"
        rts

        .rodata
cmds:   CMD     'I', c_info
        CMD     'N', c_next
        CMD     'F', c_flush
        CMD     'G', c_map
        CMD     'S', c_swap
        CMD     'A', c_abs
        CMD     'R', c_rel
        CMD     'L', c_leds
        CMD     'P', c_rep
        CMD     'B', c_bad
        CMD     'Q', c_close
        CMD     'W', c_wboot
        CMD     'D', c_line
CMDS_LEN = * - cmds
ablk:   .byte   IN_MOUSE, 1
        .word   319, 199
rblk:   .byte   IN_MOUSE, 0
        .word   639, 479
lblk:   .byte   IN_LEDS, 5
pblk:   .byte   IN_REPEAT, $25
sblk:   .byte   IN_SWAP, 1, 3
bswap:  .byte   IN_SWAP, 1, 5
bmouse: .byte   IN_MOUSE, 2
        .word   100, 100

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0
iblk:   .byte   IN_INFO, $EE, $EE
fiblk:  .byte   FT_INFO, 0, 0, 0, 0
nblk:   .byte   IN_NEXT
        .res    8
mblk:   .byte   IN_MAP
        .res    4
lbuf:   .byte   20, 0
        .res    20

        .include "print.inc"
