; L3TEST: OOLIB's L3 (hw/oolib.md, "The plan"): function 82, for
; test_oodos_sound.cpp. The command tail's first letter opens an area at
; AREA ($2000, in W0's block 0) or not:
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
;   S   ST aa xx and the status block's 12 bytes (SN_STATUS)
;   I   INFO aa xx and the info block's 3 bytes (SN_INFO)
;   H   HOOK aa xx: SN_DOCHOOK with DOCHOOK, its zero page byte HZP
;   R   RUN aa xx: 256 bytes, 0-255, written to wave RAM from $00000 through
;       DOCDATA (DOCCTL's RAM and AUTOINC), a byte at a time, slowly: an
;       ES5503 interrupt the test raises midway (at DOCRUN with X = 128)
;       must leave the run's bytes where they belong
;   W   the warm boot (JMP WBOOT)
;   D   spins on in a ^C-able function 10 (^C there ends it)
;
; DOCHOOK counts its calls at DCNT ($2180) and keeps the interrupt
; register OODOS gave it (HZP) at DLAST ($2181), and its S at DS ($2182);
; then writes the chip's register $40 (oscillator 0's volume) $77, as a
; hook reprogramming an oscillator would.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $2000
DOCCTL  = $C55E                         ; the sound card's ES5503 port (backplane.inc's SOUND_DOC*)
DOCDATA = $C55F
DOCADDR = $C560
DOCBANK = $C562
MBOX    = $2100
KBLK    = $2110
DCNT    = $2180
DLAST   = $2181
DS      = $2182

        .zeropage
t0:     .res    2
sayx:   .res    1
HZP:    .res    1

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
        stz     DCNT
        stz     DLAST
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

c_z:    lda     MBOX + 1
        ldx     #0
        jsr     f82
        SAY     "Z"
        rts

c_st:   lda     #<sblk
        ldx     #>sblk
        jsr     f82
        SAYDUMP "ST", sblk + 1, 12
        rts

c_info: lda     #<iblk
        ldx     #>iblk
        jsr     f82
        SAYDUMP "INFO", iblk + 1, 3
        rts

c_hook: lda     #<hblk
        ldx     #>hblk
        jsr     f82
        SAY     "HOOK"
        rts

c_wboot:
        jmp     WBOOT

c_run:  lda     #$60                    ; RAM, AUTOINC
        sta     DOCCTL
        stz     DOCADDR
        stz     DOCADDR + 1
        stz     DOCBANK
        ldx     #0
DOCRUN: txa
        sta     DOCDATA
        ldy     #40                     ; a run spread out, an interrupt may come in it
@d:     dey
        bne     @d
        inx
        bne     DOCRUN
        lda     #0
        ldx     #0
        SAY     "RUN"
        rts

c_line: lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        jsr     CRLF
        SAY     "LINEOK"
        rts

; The ES5503 hook: masked, its windows the program's; A, X and Y its own.
DOCHOOK:
        inc     DCNT
        lda     HZP
        sta     DLAST
        tsx
        stx     DS
        lda     #$40                    ; oscillator 0's volume
        sta     DOCADDR
        lda     #$77
        sta     DOCDATA
        rts

        .rodata
cmds:   CMD     'K', c_k
        CMD     'Z', c_z
        CMD     'S', c_st
        CMD     'I', c_info
        CMD     'H', c_hook
        CMD     'R', c_run
        CMD     'W', c_wboot
        CMD     'D', c_line
CMDS_LEN = * - cmds
hblk:   .byte   SN_DOCHOOK
        .word   DOCHOOK
        .byte   HZP

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0
sblk:   .byte   SN_STATUS
        .res    12, $EE
iblk:   .byte   SN_INFO
        .res    3, $EE
lbuf:   .byte   20, 0
        .res    20

        .include "print.inc"
