; STUBTEST: the BDOS stub keeps a program's windows, all four, its S, and
; returns D clear (hw/oodos.md, "The system, phase 2"), for test_oodos_sys.cpp.
; It moves itself to block 3 (a copy of block 0, then W0 there), puts W1 and
; W2 on blocks 4 and 5 (with $BFFD's JMP in 5) and W3 on block 20, a home
; block the test has given common memory and a string at $C800. Then, with D
; set, function 9 of a string in W1 and of the one in W3, and 105 into W2:
;
;   W1 STRING / HOME BLOCK   function 9 through W1 and W3
;   WINDOWS 03 04 05 14      WIN0-WIN3 after them
;   S ss ss                  S before and after
;   P pp                     P after (D, bit 3, clear)
;   DAT dddd hh mm ss        what function 105 wrote at $8000, block 5
;   NOFUNC FFFF              function 13, which the stand-in hasn't

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

COPYBLK = 3
W1BLK   = 4
W2BLK   = 5
HOMEBLK = 20

        .zeropage
src:    .res    2
dst:    .res    2

        .code
start:  lda     #COPYBLK                ; block 0 to block 3, through W1
        sta     NB_WIN1
        stz     src
        stz     dst
        lda     #$00
        sta     src+1
        lda     #$40
        sta     dst+1
        ldy     #0
@copy:  lda     (src),y
        sta     (dst),y
        iny
        bne     @copy
        inc     dst+1
        inc     src+1
        lda     src+1
        cmp     #$40
        bne     @copy
        lda     #COPYBLK
        sta     NB_WIN0                 ; running on the copy now
        ldx     #2                      ; $BFFD's JMP, for block 5
@jmp:   lda     BDOS_ENTRY,x
        sta     jmpsave,x
        dex
        bpl     @jmp
        lda     #W1BLK
        sta     NB_WIN1
        lda     #W2BLK
        sta     NB_WIN2
        ldx     #2
@put:   lda     jmpsave,x
        sta     BDOS_ENTRY,x
        dex
        bpl     @put
        ldx     #w1len - 1              ; a string in W1
@str:   lda     w1str,x
        sta     $4000,x
        dex
        bpl     @str
        lda     #HOMEBLK
        sta     NB_WIN3

        tsx
        stx     sbefore
        sed
        lda     #<$4000
        ldx     #>$4000
        ldy     #C_WRITESTR
        jsr     BDOS
        php
        pla
        sta     pafter
        tsx
        stx     safter
        ldx     #3
@win:   lda     NB_WIN0,x
        sta     wafter,x
        dex
        bpl     @win
        lda     #<CODE_WINDOW           ; the home block's string
        ldx     #>CODE_WINDOW
        ldy     #C_WRITESTR
        jsr     BDOS
        lda     #<$8000
        ldx     #>$8000
        ldy     #T_GET
        jsr     BDOS
        sta     secs
        ldy     #13                     ; one the stand-in hasn't: A = X = $FF
        jsr     BDOS
        sta     nofunc
        stx     nofunc+1

        lda     #<winmsg
        ldx     #>winmsg
        jsr     PUTS
        ldx     #0
@show:  lda     wafter,x
        phx
        jsr     HEX2
        jsr     SPACE
        plx
        inx
        cpx     #4
        bne     @show
        jsr     CRLF
        lda     #<smsg
        ldx     #>smsg
        jsr     PUTS
        lda     sbefore
        jsr     HEX2
        jsr     SPACE
        lda     safter
        jsr     HEX2
        jsr     CRLF
        lda     #<pmsg
        ldx     #>pmsg
        jsr     PUTS
        lda     pafter
        jsr     HEX2
        jsr     CRLF
        lda     #<datmsg
        ldx     #>datmsg
        jsr     PUTS
        lda     $8000
        ldx     $8001
        jsr     HEX4
        jsr     SPACE
        lda     $8002
        jsr     HEX2
        jsr     SPACE
        lda     $8003
        jsr     HEX2
        jsr     SPACE
        lda     secs
        jsr     HEX2
        jsr     CRLF
        lda     #<nofmsg
        ldx     #>nofmsg
        jsr     PUTS
        lda     nofunc
        ldx     nofunc+1
        jsr     HEX4
        jsr     CRLF
        jmp     WBOOT                   ; which puts the windows back

        .rodata
w1str:  .byte   "W1 STRING", 13, 10, '$'
w1len   = * - w1str
winmsg: .byte   "WINDOWS $"
smsg:   .byte   "S $"
pmsg:   .byte   "P $"
datmsg: .byte   "DAT $"
nofmsg: .byte   "NOFUNC $"

        .bss
jmpsave: .res   3
sbefore: .res   1
safter: .res    1
pafter: .res    1
wafter: .res    4
secs:   .res    1
nofunc: .res    2

        .include "print.inc"
