; BIOSTEST: OODOS's BIOS through function 50 (hw/oodos.md, "BIOS"), a line
; a result, for test_oodos_sys.cpp to read. Its sector tests expect A: on a
; writable unit and B: on a read-only one.
;
;   SELDSK d hhhh            A-G and M: the DPH (0000: no drive)
;   SELDSK 16 hhhh           past P:
;   DPB d <17 bytes>         its DPB, copied out of the system bank by XMOVE
;   WRITE d rr / READ d rr   a sector out and back: the last of the drive
;   SAME d Y|N               and whether it came back as it went
;   BOUNCEW A rr             a WRITE of sector 14 from a buffer across $4000,
;                            W1 on block 4 for it, so the halves aren't one
;                            run: the pattern i ^ $A5
;   BOUNCE d rr Y|N          then a READ of sector 15 into that buffer
;   PAST A rr                READ A: past its last track
;   ERRORS M rr rr           READ M: past its last track, and a sector 16
;   LABEL M <16 bytes>       M: directory's first 16 bytes
;   TIME dddd hh mm ss       TIME into the SCB, then function 105
;   STATUS cs co ls as ao    CONST, CONOST, LISTST, AUXIST, AUXOST
;   DEVTBL <names>           the character devices' names
;   DRVTBL Y|N               its A: entry is SELDSK's
;   SECTRN hhhh MULTIO rr FLUSH rr
;   MOVE <text> hhhh         a MOVE in the program's bank, and A/X after
;   CONOUT/AUXOUT/LIST       characters straight to the devices
;   BADBIOS hhhh             function 50 of BIOS function 30
;   BIOSTEST DONE

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

BUF     = $2000                         ; a sector's buffer
CROSS   = $3F00                         ; one across W0 and W1, W1 moved to block 4

        .code
start:  ldx     #0                      ; A-G, then M
@drive: txa
        clc
        adc     #'A'
        phx
        jsr     drive
        plx
        inx
        cpx     #7
        bne     @drive
        lda     #'M'
        ldx     #12
        jsr     drive
        lda     #<sel16msg              ; past P:
        ldx     #>sel16msg
        jsr     PUTS
        lda     #BIOS::SELDSK
        sta     pb
        lda     #16
        sta     pb+1
        stz     pb+2
        jsr     bios
        jsr     HEX4
        jsr     CRLF

        ; A: its last sector out and back, then into a buffer across $4000
        lda     #0
        jsr     select
        lda     #<1023
        ldx     #>1023
        jsr     roundtrip
        lda     #'A'
        jsr     report
        lda     #4                      ; W1 on block 4: $3F00-$40FF isn't one run
        sta     NB_WIN1
        lda     #<CROSS
        ldx     #>CROSS
        jsr     setdma
        ldx     #0                      ; sector 14 from it: i ^ $A5
@fillx: txa
        eor     #$A5
        sta     CROSS,x
        sta     CROSS+$100,x
        inx
        bne     @fillx
        lda     #14
        jsr     setsec
        lda     #BIOS::WRITE
        jsr     bios0
        pha
        lda     #<bouncewmsg
        ldx     #>bouncewmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF
        lda     #15                     ; then sector 15 into it
        jsr     setsec
        lda     #BIOS::READ
        jsr     bios0
        pha
        lda     #<bouncemsg
        ldx     #>bouncemsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     #>CROSS
        jsr     compare
        jsr     CRLF
        lda     #1                      ; W1 back
        sta     NB_WIN1

        ; A: past its last track: the card refuses it
        lda     #<1024
        ldx     #>1024
        jsr     settrk
        lda     #0
        jsr     setsec
        lda     #BIOS::READ
        jsr     bios0
        pha
        lda     #<pastmsg
        ldx     #>pastmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF

        ; B: read-only
        lda     #1
        jsr     select
        lda     #<BUF
        ldx     #>BUF
        jsr     setdma
        lda     #BIOS::WRITE
        jsr     bios0
        pha
        lda     #<writebmsg
        ldx     #>writebmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF

        ; M: its last sector, then two that aren't there, then its label
        lda     #12
        jsr     select
        lda     #15
        ldx     #0
        jsr     roundtrip
        lda     #'M'
        jsr     report
        lda     #<errmsg
        ldx     #>errmsg
        jsr     PUTS
        lda     #16                     ; track 16: past the end
        ldx     #0
        jsr     settrk
        lda     #0
        jsr     setsec
        lda     #BIOS::READ
        jsr     bios0
        jsr     HEX2
        jsr     SPACE
        lda     #0
        ldx     #0
        jsr     settrk
        lda     #16                     ; sector 16 of 16
        jsr     setsec
        lda     #BIOS::READ
        jsr     bios0
        jsr     HEX2
        jsr     CRLF
        lda     #0
        jsr     setsec
        lda     #BIOS::READ
        jsr     bios0
        lda     #<labelmsg
        ldx     #>labelmsg
        jsr     PUTS
        lda     #<BUF
        sta     PTR
        lda     #>BUF
        sta     PTR+1
        ldx     #16
        jsr     HEXN
        jsr     CRLF

        ; TIME, then the date and time by function 105
        lda     #BIOS::TIME
        sta     pb
        stz     pb+1
        jsr     bios
        lda     #<dat
        ldx     #>dat
        ldy     #T_GET
        jsr     BDOS
        pha
        lda     #<timemsg
        ldx     #>timemsg
        jsr     PUTS
        lda     dat
        ldx     dat+1
        jsr     HEX4
        jsr     SPACE
        lda     dat+2
        jsr     HEX2
        jsr     SPACE
        lda     dat+3
        jsr     HEX2
        jsr     SPACE
        pla
        jsr     HEX2
        jsr     CRLF

        ; the character devices' statuses
        lda     #<statmsg
        ldx     #>statmsg
        jsr     PUTS
        ldx     #0
@stat:  lda     stats,x
        phx
        jsr     bios0
        jsr     HEX2
        jsr     SPACE
        plx
        inx
        cpx     #5
        bne     @stat
        jsr     CRLF

        ; DEVTBL and DRVTBL, read out of the system bank
        lda     #BIOS::DEVTBL
        jsr     bios0
        ldy     #24
        jsr     fetch
        lda     #<devmsg
        ldx     #>devmsg
        jsr     PUTS
        ldx     #0                      ; three names of 6, 8 bytes apart
@name:  lda     BUF,x
        phx
        jsr     PUTC
        plx
        inx
        txa
        and     #7
        cmp     #6
        bne     @name
        inx
        inx
        cpx     #24
        bne     @name
        jsr     CRLF
        lda     #BIOS::DRVTBL
        jsr     bios0
        ldy     #2
        jsr     fetch
        lda     #<drvmsg
        ldx     #>drvmsg
        jsr     PUTS
        lda     BUF
        cmp     dphs
        bne     @no
        lda     BUF+1
        cmp     dphs+1
        bne     @no
        lda     #'Y'
        bra     @yes
@no:    lda     #'N'
@yes:   jsr     PUTC
        jsr     CRLF

        ; SECTRN, MULTIO, FLUSH
        lda     #<secmsg
        ldx     #>secmsg
        jsr     PUTS
        lda     #BIOS::SECTRN
        sta     pb
        lda     #5
        sta     pb+1
        lda     #0
        sta     pb+2
        jsr     bios
        jsr     HEX4
        lda     #<multimsg
        ldx     #>multimsg
        jsr     PUTS
        lda     #BIOS::MULTIO
        sta     pb
        lda     #4
        sta     pb+1
        jsr     bios
        jsr     HEX2
        lda     #<flushmsg
        ldx     #>flushmsg
        jsr     PUTS
        lda     #BIOS::FLUSH
        jsr     bios0
        jsr     HEX2
        jsr     CRLF

        ; MOVE in the program's bank
        lda     #BIOS::SELMEM
        sta     pb
        lda     #1
        sta     pb+1
        jsr     bios
        lda     #BIOS::MOVE
        sta     pb
        lda     #movelen
        sta     pb+1
        stz     pb+2
        lda     #<movesrc
        sta     pb+4
        lda     #>movesrc
        sta     pb+5
        lda     #<movedst
        sta     pb+6
        lda     #>movedst
        sta     pb+7
        jsr     bios
        pha
        phx
        lda     #<movemsg
        ldx     #>movemsg
        jsr     PUTS
        lda     #<movedst
        ldx     #>movedst
        jsr     PUTS
        jsr     SPACE
        plx
        pla
        jsr     HEX4
        jsr     CRLF

        ; the devices themselves: CONOUT (CRT and TTY), AUXOUT (TTY), LIST (LPT: dropped)
        lda     #BIOS::CONOUT
        ldx     #'C'
        jsr     biosc
        lda     #BIOS::AUXOUT
        ldx     #'X'
        jsr     biosc
        lda     #BIOS::LIST
        ldx     #'L'
        jsr     biosc
        jsr     CRLF

        ; a BIOS function there isn't
        lda     #BIOS::COUNT
        jsr     bios0
        pha
        phx
        lda     #<badmsg
        ldx     #>badmsg
        jsr     PUTS
        plx
        pla
        jsr     HEX4
        jsr     CRLF

        lda     #<donemsg
        ldx     #>donemsg
        jsr     PUTS
        jmp     WBOOT

; SELDSK of drive X (letter A), the DPH printed, and its DPB if there is one.
drive:  pha
        phx
        lda     #<selmsg
        ldx     #>selmsg
        jsr     PUTS
        plx
        pla
        pha
        phx
        jsr     PUTC
        jsr     SPACE
        lda     #BIOS::SELDSK
        sta     pb
        plx
        stx     pb+1
        stz     pb+2                    ; a first select
        jsr     bios
        sta     dph
        stx     dph+1
        jsr     HEX4
        jsr     CRLF
        pla
        ldx     dph+1
        bne     @dpb
        ldx     dph
        bne     @dpb
        rts
@dpb:   pha
        cmp     #'A'
        bne     @keep
        lda     dph
        sta     dphs
        lda     dph+1
        sta     dphs+1
@keep:  lda     dph                     ; the DPH, then the DPB it points to
        ldx     dph+1
        ldy     #25
        jsr     fetch
        lda     BUF+12
        ldx     BUF+13
        ldy     #17
        jsr     fetch
        lda     #<dpbmsg
        ldx     #>dpbmsg
        jsr     PUTS
        pla
        jsr     PUTC
        jsr     SPACE
        lda     #<BUF
        sta     PTR
        lda     #>BUF
        sta     PTR+1
        ldx     #17
        jsr     HEXN
        jmp     CRLF

; Y bytes of the system bank at A/X to BUF: XMOVE, then MOVE.
fetch:  sta     pb+4
        stx     pb+5
        sty     fcount
        lda     #BIOS::XMOVE
        sta     pb
        lda     #0                      ; from the system's bank (CP/M's C)
        sta     pb+1
        lda     #1                      ; to the program's (B)
        sta     pb+2
        jsr     bios
        lda     #BIOS::MOVE
        sta     pb
        lda     fcount
        sta     pb+1
        stz     pb+2
        lda     #<BUF
        sta     pb+6
        lda     #>BUF
        sta     pb+7
        jmp     bios

; SELDSK A (drive number), already selected once.
select: sta     pb+1
        lda     #BIOS::SELDSK
        sta     pb
        lda     #1
        sta     pb+2
        jmp     bios

; Track A/X's sector 15: a pattern out, a blank in, then back.
roundtrip:
        jsr     settrk
        lda     #15
        jsr     setsec
        lda     #<BUF
        ldx     #>BUF
        jsr     setdma
        ldx     #0
@fill:  txa
        eor     #$5A
        sta     BUF,x
        sta     BUF+$100,x
        inx
        bne     @fill
        lda     #BIOS::WRITE
        jsr     bios0
        sta     wrote
        ldx     #0
@blank: stz     BUF,x
        stz     BUF+$100,x
        inx
        bne     @blank
        lda     #BIOS::READ
        jsr     bios0
        sta     readr
        rts

; WRITE A rr, READ A rr, SAME A Y|N, for drive letter A.
report: pha
        lda     #<writemsg
        ldx     #>writemsg
        jsr     PUTS
        pla
        pha
        jsr     PUTC
        jsr     SPACE
        lda     wrote
        jsr     HEX2
        jsr     CRLF
        lda     #<readmsg
        ldx     #>readmsg
        jsr     PUTS
        pla
        pha
        jsr     PUTC
        jsr     SPACE
        lda     readr
        jsr     HEX2
        jsr     CRLF
        lda     #<samemsg
        ldx     #>samemsg
        jsr     PUTS
        pla
        jsr     PUTC
        jsr     SPACE
        lda     #>BUF
        jsr     compare
        jmp     CRLF

; Y or N: the 512 bytes at page A are the pattern.
compare:
        sta     PTR+1
        stz     PTR
        ldy     #0
@cmp:   tya
        eor     #$5A
        cmp     (PTR),y
        bne     @no
        inc     PTR+1
        cmp     (PTR),y
        bne     @no
        dec     PTR+1
        iny
        bne     @cmp
        lda     #'Y'
        jmp     PUTC
@no:    lda     #'N'
        jmp     PUTC

settrk: sta     pb+1
        stx     pb+2
        lda     #BIOS::SETTRK
        sta     pb
        jmp     bios
setsec: sta     pb+1
        stz     pb+2
        lda     #BIOS::SETSEC
        sta     pb
        jmp     bios
setdma: sta     pb+1
        stx     pb+2
        lda     #BIOS::SETDMA
        sta     pb
        jsr     bios
        lda     #BIOS::SETBNK           ; the program's bank
        sta     pb
        lda     #1
        sta     pb+1
        jmp     bios

; BIOS function A with no parameters; X's character for one.
bios0:  sta     pb
        jmp     bios
biosc:  sta     pb
        stx     pb+1
        ; fall through

; Function 50 with pb.
bios:   lda     #<pb
        ldx     #>pb
        ldy     #S_BIOS
        jmp     BDOS

        .rodata
stats:  .byte   BIOS::CONST, BIOS::CONOST, BIOS::LISTST, BIOS::AUXIST, BIOS::AUXOST
selmsg: .byte   "SELDSK $"
sel16msg: .byte "SELDSK 16 $"
pastmsg: .byte  "PAST A $"
badmsg: .byte   "BADBIOS $"
dpbmsg: .byte   "DPB $"
writemsg: .byte "WRITE $"
readmsg: .byte  "READ $"
samemsg: .byte  "SAME $"
bouncemsg: .byte "BOUNCE A $"
bouncewmsg: .byte "BOUNCEW A $"
writebmsg: .byte "WRITE B $"
errmsg: .byte   "ERRORS M $"
labelmsg: .byte "LABEL M $"
timemsg: .byte  "TIME $"
statmsg: .byte  "STATUS $"
devmsg: .byte   "DEVTBL $"
drvmsg: .byte   "DRVTBL $"
secmsg: .byte   "SECTRN $"
multimsg: .byte " MULTIO $"
flushmsg: .byte " FLUSH $"
movemsg: .byte  "MOVE $"
movesrc: .byte  "MOVED$"
movelen = * - movesrc
donemsg: .byte  "BIOSTEST DONE", 13, 10, '$'

        .bss
pb:     .res    8                       ; function 50's parameter block
dph:    .res    2
dphs:   .res    2                       ; A:'s
dat:    .res    4
fcount: .res    1
wrote:  .res    1
readr:  .res    1
movedst: .res   8

        .include "print.inc"
