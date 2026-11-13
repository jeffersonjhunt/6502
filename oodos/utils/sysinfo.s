; SYSINFO.COM (hw/diag.md, "SYSINFO", D1; the User's Guide's SYSINFO): what
; the machine has, from OODOS's functions and, where they don't say, the
; hardware's own ID and status registers, read as hw/diag.md's answer 1
; allows the system tools (AGENTS.md). It writes nothing but its own memory,
; the console, and the VIA's timer 2 (the program's), so it is always safe
; to run.
;
;   SYSINFO {section} {/ALL}
;
;   section     one of SYSTEM, CARDS, VIDEO, SOUND, STORAGE, INPUT, CLOCK,
;               PANEL; none is all of them
;   /ALL        no paging (for SUBMIT and the tests); else paged as the
;               SCB says (CM_PAGE)
;
; Where each part comes from:
;
;   SYSTEM   the CPU (the design's W65C02S: no opcode test, review CC1) and
;            its clock, measured by oohw's HW_CLOCK against the DS3231's
;            second and against 60 frames of the event area's counter
;            (function 80); the ROM BIOS's version (77, interface 9);
;            OODOS's and its BIOS's (72), OOLIB's interface
;            (80's FT_INFO); A:OOUI.BNK's header; the memory (72, 83's
;            DM_INFO, 53 and 57 for what is free, $0204 for the TPA); how
;            the machine booted (NB_STATUS)
;   CARDS    HW_PROBE: each ID byte, read twice
;   VIDEO    DM_INFO, and the card's MEMSIZE
;   SOUND    82's SN_INFO
;   STORAGE  the SD card's IDENTIFY (HW_SD_ID); drives A-N by 14, 31 and 46
;            in the error mode that returns errors (O: and P:, the Disk
;            II's, aren't selected, to leave its drives alone)
;   INPUT    the USB card's DEVICES; the event area's paddles and controllers
;   CLOCK    105; NB_RTCCTL's LOST; the temperature (HW_RTC)
;   PANEL    the console card's SWR and PANEL
;
; Errors: "SYSINFO {section} {/ALL}" for anything else in the tail, the
; return code $FF00.

        .setcpu "65C02"
        .macpack longbranch
        .include "backplane.inc"
        .include "oodos.inc"
        .include "oohw.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

NSECT   = 8                             ; the sections, 1-8 in sectab's order

        .zeropage
p:      .res    2                       ; the search's pointer

        .code
start:  jsr     UT_INIT
        stz     all
        stz     only
@arg:   jsr     UT_SKIP
        beq     @go
        jsr     word
        jsr     which
        bcs     usage
        bra     @arg
@go:    lda     all
        bne     @nopage
        jsr     CM_PAGE
@nopage:
        jsr     UT_MEM                  ; past the BSS: IDENTIFY's sector
        stz     areaok
        lda     #<opblk                 ; the event area: frames, paddles, controllers
        ldx     #>opblk
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #0
        bne     @noarea
        dec     areaok
        lda     area + EV_CLOCKS        ; the paddles are sampled at the ticks: let some come
        and     #FC_MS
        beq     @noarea
        lda     #<dlyblk
        ldx     #>dlyblk
        ldy     #O_FRAME
        jsr     BDOS
@noarea:
        jsr     HW_PROBE
        lda     #<stitle
        ldx     #>stitle
        jsr     CM_PUTS
        jsr     nl
        lda     #1
        sta     sect
@sect:  lda     only
        beq     @do
        cmp     sect
        bne     @next
@do:    jsr     blank
        lda     sect
        asl     a
        tax
        jsr     @call
@next:  inc     sect
        lda     sect
        cmp     #NSECT + 1
        bne     @sect
        lda     areaok
        beq     @end
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
@end:   jmp     UT_EXIT
@call:  jmp     (sectab - 2,x)

usage:  lda     #<susage
        ldx     #>susage
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; The tail's next word, to a blank or its end, into wbuf (0-ended; 15 at most).
word:   ldy     #0
@c:     jsr     UT_PEEK
        beq     @end
        cmp     #' '
        beq     @end
        cmp     #UTC_TAB
        beq     @end
        jsr     UT_GET
        cpy     #15
        bcs     @c                      ; too long: it can't match
        sta     wbuf,y
        iny
        bra     @c
@end:   lda     #0
        sta     wbuf,y
        rts

; wbuf: /ALL, or a section's name (only); carry set if neither, or a second section.
which:  lda     #<sall
        ldx     #>sall
        jsr     same
        bcc     @sect
        lda     #1
        sta     all
        clc
        rts
@sect:  lda     only
        bne     @no
        lda     #1
        sta     sect
@try:   lda     sect
        asl     a
        tax
        lda     namtab - 2,x
        pha
        lda     namtab - 1,x
        tax
        pla
        jsr     same
        bcs     @yes
        inc     sect
        lda     sect
        cmp     #NSECT + 1
        bne     @try
@no:    sec
        rts
@yes:   lda     sect
        sta     only
        clc
        rts

; Carry set if wbuf is the 0-ended string at A/X.
same:   sta     p
        stx     p + 1
        ldy     #0
@c:     lda     (p),y
        cmp     wbuf,y
        bne     @no
        cmp     #0
        beq     @yes
        iny
        bra     @c
@no:    clc
        rts
@yes:   sec
        rts

; ---------------------------------------------------------------------------
; Writing

HW_PUTC = CM_PUTC                       ; oohw's output: function 2 (keeps X and Y)

; A line ended (owed: the next output ends it, so the last is left for the prompt's).
nl:     jmp     CM_EOL
; The line ended, and an empty one after it.
blank:  jsr     CM_EOL
        jsr     CM_FLUSH
        jmp     CM_EOL
; The '$' string at A/X.
puts:   jmp     CM_PUTS
; The value column, on a new line.
cont:   jsr     nl
        lda     #<scont
        ldx     #>scont
        jmp     CM_PUTS
; A/X in decimal, with commas.
dec16:  sta     HW_NUM
        stx     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        sec
        jmp     HW_DEC
; A in decimal.
dec8:   ldx     #0
        bra     dec16
; ", " (A/X a string after it).
comma:  pha
        phx
        lda     #<scomma
        ldx     #>scomma
        jsr     CM_PUTS
        plx
        pla
        jmp     CM_PUTS
; The 4 bytes at A/X (low first) in decimal, with commas.
dec32:  sta     p
        stx     p + 1
        ldy     #3
@cp:    lda     (p),y
        sta     HW_NUM,y
        dey
        bpl     @cp
        sec
        jmp     HW_DEC
; Up to X characters at A/Y's address (A low, Y high), to a 0; blanks kept.
text:   sta     p
        sty     p + 1
        stx     tlen
        ldy     #0
@c:     cpy     tlen
        beq     @end
        lda     (p),y
        beq     @end
        cmp     #' '
        bcc     @dot
        cmp     #$7F
        bcc     @put
@dot:   lda     #'.'
@put:   jsr     CM_PUTC
        iny
        bra     @c
@end:   rts

; ---------------------------------------------------------------------------
; SYSTEM

system: lda     #<ssystem
        ldx     #>ssystem
        jsr     puts
        jsr     nl
        lda     #<scpu
        ldx     #>scpu
        jsr     puts
        lda     #<NB_SEC                ; the clock chip's second
        sta     HW_WATCH
        lda     #>NB_SEC
        sta     HW_WATCH + 1
        lda     #1
        sta     HW_EDGES
        jsr     HW_CLOCK
        lda     #0
        rol     a
        sta     rtcbad
        ldx     #3
@r:     lda     HW_CYC,x
        sta     hzrtc,x
        dex
        bpl     @r
        lda     #1                      ; and 60 video frames, if the area has them
        sta     frmbad
        lda     areaok
        beq     @nof
        lda     area + EV_CLOCKS
        and     #FC_FRAME
        beq     @nof
        lda     #<(area + EV_FRAMES)
        sta     HW_WATCH
        lda     #>(area + EV_FRAMES)
        sta     HW_WATCH + 1
        lda     #60
        sta     HW_EDGES
        jsr     HW_CLOCK
        bcs     @nof
        jsr     HW_FRAMEHZ
        stz     frmbad
        ldx     #3
@f:     lda     HW_NUM,x
        sta     hzfrm,x
        dex
        bpl     @f
@nof:   lda     rtcbad                  ; " at 7.159 MHz", by the clock chip, else the frame
        beq     @mhzr
        lda     frmbad
        bne     @hz
        lda     #<hzfrm
        ldx     #>hzfrm
        bra     @mhz
@mhzr:  lda     #<hzrtc
        ldx     #>hzrtc
@mhz:   jsr     mhz
@hz:    jsr     cont
        lda     rtcbad
        bne     @nortc
        lda     #<hzrtc
        ldx     #>hzrtc
        jsr     dec32
        lda     #<sbyrtc
        ldx     #>sbyrtc
        jsr     puts
        bra     @frm
@nortc: lda     #<snortc
        ldx     #>snortc
        jsr     puts
@frm:   lda     frmbad
        bne     @nofrm
        lda     #<scomma
        ldx     #>scomma
        jsr     puts
        lda     #<hzfrm
        ldx     #>hzfrm
        jsr     dec32
        lda     #<sbyfrm
        ldx     #>sbyfrm
        jsr     puts
        bra     @bios
@nofrm: lda     #<snofrm
        ldx     #>snofrm
        jsr     comma
@bios:  jsr     nl
        jsr     rombios
        jsr     nl
        jsr     oodos
        jsr     nl
        jsr     ooui
        jsr     nl
        jsr     memory
        jsr     nl
        lda     #<sbooted               ; how it booted
        ldx     #>sbooted
        jsr     puts
        lda     NB_STATUS
        sta     tmp
        and     #NB_STATUS_BOOTSEL
        beq     @flash
        lda     #<sfrom_ram
        ldx     #>sfrom_ram
        bra     @from
@flash: lda     #<sfrom_flash
        ldx     #>sfrom_flash
@from:  jsr     puts
        lda     tmp
        and     #$02                    ; STATUS bit 1: the last reset was the power-on
        beq     @reset
        lda     #<spoweron
        ldx     #>spoweron
        jmp     comma
@reset: lda     #<sareset
        ldx     #>sareset
        jmp     comma

; " at n.nnn MHz" from the Hz at A/X, to the nearest kHz.
mhz:    sta     p
        stx     p + 1
        clc
        ldy     #0
        lda     (p),y
        adc     #<500
        sta     HW_NUM
        iny
        lda     (p),y
        adc     #>500
        sta     HW_NUM + 1
        iny
        lda     (p),y
        adc     #0
        sta     HW_NUM + 2
        iny
        lda     (p),y
        adc     #0
        sta     HW_NUM + 3
        lda     #<1000                  ; kHz
        ldx     #>1000
        jsr     HW_DIV
        lda     #<1000                  ; MHz, the kHz left
        ldx     #>1000
        jsr     HW_DIV
        sta     frac
        stx     frac + 1
        lda     #<sat
        ldx     #>sat
        jsr     puts
        clc
        jsr     HW_DEC
        lda     #'.'
        jsr     CM_PUTC
        lda     frac + 1                ; three digits
        bne     @three
        lda     frac
        cmp     #100
        bcs     @three
        lda     #'0'
        jsr     CM_PUTC
        lda     frac
        cmp     #10
        bcs     @three
        lda     #'0'
        jsr     CM_PUTC
@three: lda     frac
        ldx     frac + 1
        jsr     dec16
        lda     #<smhz
        ldx     #>smhz
        jmp     puts

; "  ROM BIOS   0.13": function 77 (O_ROMVER, interface 9, hw/diag.md's D1-1). An OODOS before
; interface 9 hasn't it (and would answer it A = X = $FF anyway): FT_INFO says which.
rombios:
        lda     #<sbios
        ldx     #>sbios
        jsr     puts
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #9
        bcc     @old                    ; $FF (no function 80) is past 9: looked at next
        cmp     #$FF
        bne     @ask
@old:   lda     #<sbiosold
        ldx     #>sbiosold
        jmp     puts
@ask:   ldy     #O_ROMVER
        jsr     BDOS
        cmp     #$FF
        bne     @ver
        cpx     #$FF
        bne     @ver
        lda     #<sbiosnone
        ldx     #>sbiosnone
        jmp     puts
@ver:   jmp     version

; "  OODOS      0.9, its BIOS 0.9; OOLIB interface 8"
oodos:  lda     #<soodos
        ldx     #>soodos
        jsr     puts
        lda     #<ver
        ldx     #>ver
        ldy     #O_VERSION
        jsr     BDOS
        lda     ver
        ldx     ver + 1
        jsr     version
        lda     #<sitsbios
        ldx     #>sitsbios
        jsr     puts
        lda     ver + 2
        ldx     ver + 3
        jsr     version
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #$FF
        beq     @nolib
        pha
        lda     #<solib
        ldx     #>solib
        jsr     puts
        pla
        jmp     dec8
@nolib: rts

; A.X as "major.minor".
version:
        phx
        jsr     dec8
        lda     #'.'
        jsr     CM_PUTC
        pla
        jmp     dec8

; "  OOUI.BNK   A:, interface 1, 3 blocks", or "none on A:".
ooui:   lda     #<sooui
        ldx     #>sooui
        jsr     puts
        ldx     #35
@z:     stz     fcb,x
        dex
        bpl     @z
        lda     #1                      ; A:
        sta     fcb
        ldx     #10
@n:     lda     nooui,x
        sta     fcb + 1,x
        dex
        bpl     @n
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #4
        bcs     @none
        lda     #<rec
        ldx     #>rec
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_READ
        jsr     BDOS
        pha
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_CLOSE
        jsr     BDOS
        pla
        bne     @bad
        lda     rec
        cmp     #'O'
        bne     @bad
        lda     rec + 1
        cmp     #'U'
        bne     @bad
        lda     rec + 2
        cmp     #'B'
        bne     @bad
        lda     #<soouiat
        ldx     #>soouiat
        jsr     puts
        lda     rec + 4
        jsr     dec8
        lda     #<sblocks1
        ldx     #>sblocks1
        jsr     puts
        lda     rec + 5
        jsr     dec8
        lda     #<sblocks
        ldx     #>sblocks
        jmp     puts
@bad:   lda     #<sooubad
        ldx     #>sooubad
        jmp     puts
@none:  lda     #<snone_a
        ldx     #>snone_a
        jmp     puts

; MEMORY's lines.
memory: lda     #<smemory
        ldx     #>smemory
        jsr     puts
        lda     ver + 4
        ldx     ver + 5
        jsr     dec16
        lda     #<skbfit
        ldx     #>skbfit
        jsr     puts
        lda     #<diblk                 ; video card revision 2: the aperture
        ldx     #>diblk
        ldy     #O_DMA
        jsr     BDOS
        sta     vunits
        cmp     #0
        beq     @prog
        lda     #<sram960
        ldx     #>sram960
        jsr     puts
@prog:  jsr     cont
        lda     #<sforprog
        ldx     #>sforprog
        jsr     puts
        lda     BP_TOP + 1              ; the TPA: $0300 to $0204's word, in whole KB
        sec
        sbc     #>BP_TPA
        lsr     a
        lsr     a
        jsr     dec8
        lda     #<stpa
        ldx     #>stpa
        jsr     puts
        lda     ver + 6
        ldx     ver + 7
        jsr     dec16
        lda     #<sbanks15
        ldx     #>sbanks15
        jsr     puts
        stz     mcb                     ; what is free: the largest run, allocated and freed at once
        stz     mcb + 1
        lda     #$FF
        sta     mcb + 2
        sta     mcb + 3
        stz     mcb + 4
        lda     #<mcb
        ldx     #>mcb
        ldy     #MC_MAX
        jsr     BDOS
        cmp     #0
        beq     @got
        stz     mcb + 2
        stz     mcb + 3
        bra     @free
@got:   lda     #<mcb
        ldx     #>mcb
        ldy     #MC_FREE
        jsr     BDOS
@free:  lda     mcb + 2                 ; paragraphs / 64: KB
        sta     HW_NUM
        lda     mcb + 3
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lda     #64
        ldx     #0
        jsr     HW_DIV
        sec
        jsr     HW_DEC
        lda     #<skbfree
        ldx     #>skbfree
        jsr     puts
        lda     vunits
        beq     @end
        jsr     cont
        lda     #<sbanks9b
        ldx     #>sbanks9b
        jmp     puts
@end:   rts

; ---------------------------------------------------------------------------
; CARDS

cards:  lda     #<scards
        ldx     #>scards
        jsr     puts
        jsr     nl
        lda     #<scardhd
        ldx     #>scardhd
        jsr     puts
        lda     #HW_FIRST
        sta     mod
@mod:   jsr     nl
        lda     #<sindent
        ldx     #>sindent
        jsr     puts
        lda     #'$'                    ; its block's address
        jsr     CM_PUTC
        lda     mod
        lsr     a
        pha
        lda     #0
        ror     a
        tax                             ; the low byte: $00 or $80
        pla
        ora     #$C0
        pha
        txa
        plx
        jsr     HW_HEX4
        lda     #<stwo
        ldx     #>stwo
        jsr     puts
        ldx     mod
        lda     HW_REV,x
        cmp     #HW_NONE
        beq     @none
        lda     #'$'
        jsr     CM_PUTC
        ldx     mod
        lda     HW_IDB,x
        jsr     HW_HEX2
        lda     #<stwo
        ldx     #>stwo
        jsr     puts
        lda     mod
        jsr     HW_NAME
        jsr     HW_PUTS
        tya                             ; to the revision's column
        sec
        sbc     #NAMECOL
        eor     #$FF
        inc     a
        bmi     @one
        bne     @pad
@one:   lda     #1
@pad:   jsr     UT_BLANKS
        ldx     mod
        lda     HW_REV,x
        jsr     dec8
        ldx     mod
        lda     HW_REV,x
        tax
        lda     mod
        jsr     HW_KNOWN
        bcc     @next
        lda     #<sunknown
        ldx     #>sunknown
        jsr     puts
        bra     @next
@none:  lda     #<sdashes
        ldx     #>sdashes
        jsr     puts
        lda     mod
        cmp     #6
        bne     @nf
        lda     #<sstandin
        ldx     #>sstandin
        jsr     puts
        bra     @next
@nf:    lda     mod
        jsr     HW_NAME
        jsr     HW_PUTS
        lda     #<snotfound
        ldx     #>snotfound
        jsr     puts
@next:  inc     mod
        lda     mod
        cmp     #HW_LAST + 1
        jne     @mod
        rts

; ---------------------------------------------------------------------------
; VIDEO

video:  lda     #<svideo
        ldx     #>svideo
        jsr     puts
        lda     #<diblk
        ldx     #>diblk
        ldy     #O_DMA
        jsr     BDOS
        sta     vunits
        cmp     #0
        bne     @rev2
        lda     HW_REV + 9
        cmp     #1
        bne     @nocard
        lda     #<svrev1
        ldx     #>svrev1
        jmp     puts
@nocard:
        lda     #<snovideo
        ldx     #>snovideo
        jmp     puts
@rev2:  lda     vunits                  ; 64 KB units
        sta     HW_NUM
        stz     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        ldx     #6
@x64:   asl     HW_NUM
        rol     HW_NUM + 1
        dex
        bne     @x64
        sec
        jsr     HW_DEC
        lda     #<svmem
        ldx     #>svmem
        jsr     puts
        lda     VIDEO_MEMSIZE
        sta     tmp
        jsr     dec8
        lda     tmp
        cmp     vunits
        beq     @same
        lda     #<svdiff
        ldx     #>svdiff
        jsr     puts
@same:  lda     #<svprog
        ldx     #>svprog
        jsr     puts
        sec                             ; the program's pages, / 4: KB
        lda     dinfo + 15
        sbc     dinfo + 13
        sta     HW_NUM
        lda     dinfo + 16
        sbc     dinfo + 14
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lsr     HW_NUM + 1
        ror     HW_NUM
        lsr     HW_NUM + 1
        ror     HW_NUM
        sec
        jsr     HW_DEC
        lda     #<skb
        ldx     #>skb
        jmp     puts

; ---------------------------------------------------------------------------
; SOUND

sound:  lda     #<ssound
        ldx     #>ssound
        jsr     puts
        lda     #SN_INFO
        sta     sninfo
        lda     #<sninfo
        ldx     #>sninfo
        ldy     #O_SOUND
        jsr     BDOS
        lda     sninfo + 1
        bne     @card
        lda     #<snosound
        ldx     #>snosound
        jmp     puts
@card:  lda     #<srev
        ldx     #>srev
        jsr     puts
        lda     sninfo + 1
        jsr     dec8
        lda     #<ssids
        ldx     #>ssids
        jsr     puts
        lda     sninfo + 2
        bpl     @pcm
        jsr     cont
        lda     #<sdoc
        ldx     #>sdoc
        jsr     puts
@pcm:   lda     sninfo + 2
        and     #$03
        beq     @free
        jsr     cont
        lda     sninfo + 2
        lsr     a
        bcc     @mp3
        lda     #<spcm
        ldx     #>spcm
        jsr     puts
        lda     sninfo + 2
        and     #$02
        beq     @codecs
        lda     #<sand
        ldx     #>sand
        jsr     puts
@mp3:   lda     sninfo + 2
        and     #$02
        beq     @codecs
        lda     #<smp3
        ldx     #>smp3
        jsr     puts
@codecs:
        lda     #<scodecs
        ldx     #>scodecs
        jsr     puts
        lda     #'$'
        jsr     CM_PUTC
        lda     sninfo + 2
        jsr     HW_HEX2
        lda     #')'
        jsr     CM_PUTC
@free:  jsr     cont
        lda     #<sfree
        ldx     #>sfree
        jsr     puts
        lda     sninfo + 3
        and     #SC_6510 | SC_STREAM | SC_DOC
        bne     @parts
        lda     #<snone
        ldx     #>snone
        jmp     puts
@parts: stz     first
        lda     #SC_6510
        ldx     #<s6510
        ldy     #>s6510
        jsr     part
        lda     #SC_STREAM
        ldx     #<sstream
        ldy     #>sstream
        jsr     part
        lda     #SC_DOC
        ldx     #<sdocp
        ldy     #>sdocp
; A part (A, its bit) named X/Y, if it is free, after ", " if one came before.
part:   stx     name
        sty     name + 1
        and     sninfo + 3
        beq     @no
        jsr     sep
        lda     name
        ldx     name + 1
        jmp     puts
@no:    rts

; ", " unless it is the list's first.
sep:    lda     first
        inc     first
        cmp     #0
        beq     @no
        lda     #<scomma
        ldx     #>scomma
        jmp     puts
@no:    rts

; ---------------------------------------------------------------------------
; STORAGE

storage:
        lda     #<sstorage
        ldx     #>sstorage
        jsr     puts
        jsr     nl
        lda     #<ssdcard
        ldx     #>ssdcard
        jsr     puts
        lda     UT_MLO
        sta     HW_PTR
        lda     UT_MLO + 1
        sta     HW_PTR + 1
        jsr     HW_SD_ID
        bcc     @card
        cmp     #HE_ERROR
        bcs     @error
        asl     a
        tax
        lda     sderrs + 1,x
        pha
        lda     sderrs,x
        plx
        jsr     puts
        jmp     drives
@error: and     #$7F
        pha
        lda     #<ssderr
        ldx     #>ssderr
        jsr     puts
        lda     #'$'
        jsr     CM_PUTC
        pla
        jsr     HW_HEX2
        jmp     drives
@card:  lda     UT_MLO
        sta     sec0
        lda     UT_MLO + 1
        sta     sec0 + 1
        ldy     #SI_TYPE                ; its type
        lda     (HW_PTR),y
        cmp     #4
        bcc     :+
        lda     #0
:       asl     a
        tax
        lda     sdtypes + 1,x
        pha
        lda     sdtypes,x
        plx
        jsr     puts
        lda     #<scomma
        ldx     #>scomma
        jsr     puts
        lda     sec0                    ; its sectors
        ldx     sec0 + 1
        jsr     dec32
        lda     #<ssectors
        ldx     #>ssectors
        jsr     puts
        lda     #<sopen                 ; (n MB)
        ldx     #>sopen
        jsr     puts
        ldy     #3
        jsr     ldsec
        lda     #<2048
        ldx     #>2048
        jsr     HW_DIV
        sec
        jsr     HW_DEC
        lda     #<smbclose
        ldx     #>smbclose
        jsr     puts
        jsr     sdptr
        ldy     #SI_FLAGS
        lda     (HW_PTR),y
        sta     tmp
        and     #SIF_FAT
        beq     @wp
        lda     #<sfat
        ldx     #>sfat
        jsr     comma
@wp:    lda     tmp
        and     #SIF_WP
        beq     @fw
        lda     #<swp
        ldx     #>swp
        jsr     comma
@fw:    lda     #<sfirm
        ldx     #>sfirm
        jsr     comma
        jsr     sdptr
        ldy     #SI_FIRMWARE
        lda     (HW_PTR),y
        tax
        iny
        lda     (HW_PTR),y
        pha
        txa
        plx
        jsr     version
        jsr     cont                    ; its name
        lda     #<snamed
        ldx     #>snamed
        jsr     puts
        clc
        lda     sec0
        adc     #<SI_NAME
        tax
        lda     sec0 + 1
        adc     #>SI_NAME
        tay
        txa
        ldx     #32
        jsr     text
        lda     #'"'
        jsr     CM_PUTC
        lda     #1                      ; the units mounted
        sta     unit
@unit:  jsr     unitptr
        ldy     #SU_FLAGS
        lda     (HW_PTR),y
        sta     tmp
        and     #SUF_MOUNTED
        beq     @nxu
        jsr     cont
        lda     #<sunit
        ldx     #>sunit
        jsr     puts
        lda     unit
        jsr     dec8
        lda     #<stwo
        ldx     #>stwo
        jsr     puts
        clc
        lda     HW_PTR
        adc     #SU_NAME
        tax
        lda     HW_PTR + 1
        adc     #0
        tay
        txa
        ldx     #24
        jsr     text
        lda     #<scomma
        ldx     #>scomma
        jsr     puts
        lda     HW_PTR
        ldx     HW_PTR + 1
        jsr     dec32
        lda     #<ssectors
        ldx     #>ssectors
        jsr     puts
        lda     tmp
        and     #SUF_RO
        beq     @nxu
        lda     #<sro
        ldx     #>sro
        jsr     comma
@nxu:   inc     unit
        lda     unit
        cmp     #8
        bne     @unit
        ; fall through: the drives

; DRIVES: A-N, each that selects, its size and free space.
drives: jsr     nl
        lda     #<sdrives
        ldx     #>sdrives
        jsr     puts
        stz     first
        lda     #$FF                    ; errors returned, not said: a drive that isn't is passed over
        ldy     #F_ERRMODE
        jsr     BDOS
        stz     drv
@drv:   lda     drv
        ldy     #DRV_SET
        jsr     BDOS
        cmp     #0
        jne     @next
        lda     first
        beq     @one
        jsr     cont
@one:   inc     first
        lda     drv
        jsr     UT_PUTDRV
        lda     #' '
        jsr     CM_PUTC
        jsr     UT_DPB                  ; (DSM + 1) << (BSH - 3) KB
        clc
        lda     UT_DPBC + 5
        adc     #1
        sta     HW_NUM
        lda     UT_DPBC + 6
        adc     #0
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lda     UT_DPBC + 2
        sec
        sbc     #3
        tax
        beq     @size
@sh:    asl     HW_NUM
        rol     HW_NUM + 1
        rol     HW_NUM + 2
        dex
        bne     @sh
@size:  sec
        jsr     HW_DEC
        lda     #<sk
        ldx     #>sk
        jsr     puts
        lda     #<space
        ldx     #>space
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     drv
        ldy     #DRV_SPACE
        jsr     BDOS
        cmp     #0
        bne     @next
        lda     space                   ; records / 8: KB
        sta     HW_NUM
        lda     space + 1
        sta     HW_NUM + 1
        lda     space + 2
        sta     HW_NUM + 2
        stz     HW_NUM + 3
        lda     #8
        ldx     #0
        jsr     HW_DIV
        lda     #<scomma
        ldx     #>scomma
        jsr     puts
        sec
        jsr     HW_DEC
        lda     #<skfree
        ldx     #>skfree
        jsr     puts
@next:  inc     drv
        lda     drv
        cmp     #14                     ; A-N: O: and P: are the Disk II's
        jne     @drv
        lda     #$FE                    ; the error mode UT_INIT set
        ldy     #F_ERRMODE
        jsr     BDOS
        jsr     UT_BACK
        lda     HW_REV + $C
        cmp     #HW_NONE
        beq     @end
        jsr     cont
        lda     #<sdisk2
        ldx     #>sdisk2
        jmp     puts
@end:   rts

; The sector's address in HW_PTR again.
sdptr:  lda     sec0
        sta     HW_PTR
        lda     sec0 + 1
        sta     HW_PTR + 1
        rts
; HW_PTR at unit's entry: SI_UNITS + (unit - 1) x 32.
unitptr:
        lda     unit
        dec     a
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     #<SI_UNITS
        sta     name
        lda     #0
        adc     #>SI_UNITS
        sta     name + 1
        clc
        lda     name
        adc     sec0
        sta     HW_PTR
        lda     name + 1
        adc     sec0 + 1
        sta     HW_PTR + 1
        rts
; HW_NUM = the sector's 4 bytes at HW_PTR (Y 3: from its +3 down).
ldsec:  jsr     sdptr
@c:     lda     (HW_PTR),y
        sta     HW_NUM,y
        dey
        bpl     @c
        rts

; ---------------------------------------------------------------------------
; INPUT

input:  lda     #<sinput
        ldx     #>sinput
        jsr     puts
        lda     HW_REV + $B
        cmp     #HW_NONE
        bne     @usb
        lda     #<snousb
        ldx     #>snousb
        jsr     puts
        bra     @area
@usb:   lda     USB_DEVICES
        and     #$07
        sta     tmp
        bne     @devs
        lda     #<snodevs
        ldx     #>snodevs
        jsr     puts
        bra     @area
@devs:  stz     first
        lda     #$01
        ldx     #<skbd
        ldy     #>skbd
        jsr     dev
        lda     #$02
        ldx     #<smouse
        ldy     #>smouse
        jsr     dev
        lda     #$04
        ldx     #<spad
        ldy     #>spad
        jsr     dev
@area:  lda     areaok
        bne     @ctl
        jsr     cont
        lda     #<snoarea
        ldx     #>snoarea
        jmp     puts
@ctl:   lda     HW_REV + $B
        cmp     #HW_NONE
        beq     @sticks
        jsr     cont                    ; the game port: paddles and buttons
        lda     #<sgame
        ldx     #>sgame
        jsr     puts
        ldx     #0
@pad:   phx
        lda     #' '
        jsr     CM_PUTC
        plx
        phx
        lda     area + EV_PADDLES,x
        jsr     dec8
        plx
        inx
        cpx     #4
        bne     @pad
        lda     #CH_GAMEPORT
        jsr     ctl
        and     #$70
        bne     @btn
        lda     #<snobtn
        ldx     #>snobtn
        jsr     comma
        bra     @sticks
@btn:   lda     #<sbtn
        ldx     #>sbtn
        jsr     comma
@sticks:
        jsr     cont
        lda     #CH_STICK1
        jsr     ctl
        and     #$1F
        sta     tmp
        lda     #CH_STICK2
        jsr     ctl
        and     #$1F
        ora     tmp
        bne     @some
        lda     #<sidle
        ldx     #>sidle
        jmp     puts
@some:  lda     #<spressed
        ldx     #>spressed
        jmp     puts

; A device (A, its DEVICES bit) named X/Y, if tmp has it, after ", " if one came before.
dev:    stx     name
        sty     name + 1
        and     tmp
        beq     @no
        jsr     sep
        lda     name
        ldx     name + 1
        jmp     puts
@no:    rts

; The bits of the controller whose hardware is A (CH_*), 0 if none is.
ctl:    sta     tmp2
        ldx     #0
@c:     lda     area + EV_CTL + EC_HW,x
        cmp     tmp2
        beq     @got
        inx
        inx
        inx
        inx
        cpx     #4 * EC_LEN
        bne     @c
        lda     #0
        rts
@got:   lda     area + EV_CTL + EC_BITS,x
        rts

; ---------------------------------------------------------------------------
; CLOCK

clock:  lda     #<sclock
        ldx     #>sclock
        jsr     puts
        lda     #<dat
        ldx     #>dat
        ldy     #T_GET
        jsr     BDOS
        sta     secs
        lda     dat
        ldx     dat + 1
        jsr     UT_TODATE
        lda     UT_YEAR
        ldx     UT_YEAR + 1
        sta     HW_NUM
        stx     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        clc
        jsr     HW_DEC
        lda     #'-'
        jsr     CM_PUTC
        lda     UT_MONTH
        jsr     UT_PUT2
        lda     #'-'
        jsr     CM_PUTC
        lda     UT_DAY
        jsr     UT_PUT2
        lda     #' '
        jsr     CM_PUTC
        lda     dat + 2
        jsr     HW_HEX2
        lda     #':'
        jsr     CM_PUTC
        lda     dat + 3
        jsr     HW_HEX2
        lda     #':'
        jsr     CM_PUTC
        lda     secs
        jsr     HW_HEX2
        jsr     HW_RTC
        bcc     @st
        lda     #<srtcbusy
        ldx     #>srtcbusy
        jmp     comma
@st:    lda     HW_RTCST
        and     #NB_RTC_LOST
        beq     @kept
        lda     #<slost
        ldx     #>slost
        jsr     comma
        bra     @temp
@kept:  lda     #<skept
        ldx     #>skept
        jsr     comma
@temp:  lda     #<ssemi                 ; the temperature in quarters, signed: MSB x 4 + LSB's bits 7-6
        ldx     #>ssemi
        jsr     puts
        lda     HW_RTCST + 2
        rol     a
        rol     a
        rol     a
        and     #3
        sta     tmp
        lda     HW_RTCST + 1
        bpl     @pos
        eor     #$FF                    ; negative: -(MSB x 4 + q) is whole -MSB, less 1 and 4 - q if q
        inc     a
        sta     tmp2
        lda     #'-'
        jsr     CM_PUTC
        lda     tmp
        beq     @neg0
        dec     tmp2
        lda     #4
        sec
        sbc     tmp
        sta     tmp
@neg0:  lda     tmp2
@pos:   jsr     dec8
        lda     #'.'
        jsr     CM_PUTC
        ldx     tmp
        lda     quart,x
        jsr     CM_PUTC
        lda     #'5'
        cpx     #0
        bne     :+
        lda     #'0'
:       cpx     #2
        bne     :+
        lda     #'0'
:       jsr     CM_PUTC
        lda     #<sdegc
        ldx     #>sdegc
        jmp     puts

; ---------------------------------------------------------------------------
; PANEL

panel:  lda     #<spanel
        ldx     #>spanel
        jsr     puts
        lda     HW_REV + $E
        cmp     #HW_NONE
        bne     @card
        lda     #<snopanel
        ldx     #>snopanel
        jmp     puts
@card:  lda     #<sswitch
        ldx     #>sswitch
        jsr     puts
        lda     #'$'
        jsr     CM_PUTC
        lda     CONSOLE_SWR + 2         ; 20 bits: five digits
        and     #$0F
        ora     #'0'
        cmp     #'9' + 1
        bcc     :+
        adc     #'A' - '9' - 2          ; carry set
:       jsr     CM_PUTC
        lda     CONSOLE_SWR
        ldx     CONSOLE_SWR + 1
        jsr     HW_HEX4
        lda     CONSOLE_PANEL
        sta     tmp
        lsr     a
        bcc     @enable
        lda     #<shalt
        ldx     #>shalt
        bra     @h
@enable:
        lda     #<senable
        ldx     #>senable
@h:     jsr     comma
        lda     tmp
        and     #$02
        beq     @inst
        lda     #<sbuscyc
        ldx     #>sbuscyc
        jmp     comma
@inst:  lda     #<sinst
        ldx     #>sinst
        jmp     comma

        .rodata
sectab: .word   system, cards, video, sound, storage, input, clock, panel
namtab: .word   nsys, ncards, nvideo, nsound, nstorage, ninput, nclock, npanel
nsys:   .byte   "SYSTEM", 0
ncards: .byte   "CARDS", 0
nvideo: .byte   "VIDEO", 0
nsound: .byte   "SOUND", 0
nstorage: .byte "STORAGE", 0
ninput: .byte   "INPUT", 0
nclock: .byte   "CLOCK", 0
npanel: .byte   "PANEL", 0
sall:   .byte   "/ALL", 0
susage: .byte   "SYSINFO {section} {/ALL}$"
stitle: .byte   "OO-1 SYSTEM INFORMATION$"
scont:  .byte   "             $"
scomma: .byte   ", $"
sindent: .byte  "  $"
stwo:   .byte   "  $"
ssystem: .byte  "SYSTEM$"
scpu:   .byte   "  CPU        W65C02S$"
sat:    .byte   " at $"
smhz:   .byte   " MHz$"
sbyrtc: .byte   " Hz by the clock chip$"
sbyfrm: .byte   " Hz by the video frame$"
snortc: .byte   "the clock chip didn't tick$"
snofrm: .byte   "no video frame to measure by$"
sbios:  .byte   "  ROM BIOS   $"
sbiosold: .byte "not given by this OODOS (before interface 9)$"
sbiosnone: .byte "not found by OODOS at its boot$"
soodos: .byte   "  OODOS      $"
sitsbios: .byte ", its BIOS $"
solib:  .byte   "; OOLIB interface $"
sooui:  .byte   "  OOUI.BNK   $"
nooui:  .byte   "OOUI    BNK"
soouiat: .byte  "A:, interface $"
sblocks1: .byte ", $"
sblocks: .byte  " blocks$"
sooubad: .byte  "A:, not an OOUI.BNK$"
snone_a: .byte  "none on A:$"
smemory: .byte  "  MEMORY     $"
skbfit: .byte   " KB fitted$"
sram960: .byte  ": 960 KB of RAM and the video card's 64 KB aperture$"
sforprog: .byte "for programs: a $"
stpa:   .byte   "K TPA, $"
sbanks15: .byte " KB in banks 1-5 ($"
skbfree: .byte  " KB free)$"
sbanks9b: .byte "and banks 9-B, 192 KB$"
sbooted: .byte  "  BOOTED     $"
sfrom_ram: .byte "from RAM (/BOOTSEL)$"
sfrom_flash: .byte "from the flash$"
spoweron: .byte "at power-on$"
sareset: .byte  "at a reset$"
scards: .byte   "CARDS$"
scardhd: .byte  "  ADDR   ID   CARD                          REVISION$"
NAMECOL = 30                            ; the revision's column after the name's start
sdashes: .byte  "--   $"
sstandin: .byte "serial: the stand-in 6551 (no ID register)$"
snotfound: .byte ": not found$"
sunknown: .byte " (not known to SYSINFO)$"
svideo: .byte   "VIDEO      $"
svrev1: .byte   "revision 1, which OODOS no longer drives$"
snovideo: .byte "no video card$"
svmem:  .byte   " KB of card memory (MEMSIZE $"
svdiff: .byte   ", not DM_INFO's$"
svprog: .byte   "); the program's $"
skb:    .byte   " KB$"
ssound: .byte   "SOUND      $"
snosound: .byte "no sound card$"
srev:   .byte   "revision $"
ssids:  .byte   ": two SIDs, the 6510 and its 64 KB$"
sdoc:   .byte   "an ES5503 and 128 KB of wave RAM$"
spcm:   .byte   "raw PCM$"
sand:   .byte   " and $"
smp3:   .byte   "MP3$"
scodecs: .byte  " (CODECS $"
sfree:  .byte   "free: $"
snone:  .byte   "none$"
s6510:  .byte   "6510$"
sstream: .byte  "stream$"
sdocp:  .byte   "ES5503$"
sstorage: .byte "STORAGE$"
ssdcard: .byte  "  SD CARD    $"
sderrs: .word   snosd, sempty, sbusy
snosd:  .byte   "no mass storage card$"
sempty: .byte   "no card in the slot$"
sbusy:  .byte   "the card stayed busy$"
ssderr: .byte   "the card's error $"
sdtypes: .word  stnone, stsc, sthc, stxc
stnone: .byte   "no card$"
stsc:   .byte   "SDSC$"
sthc:   .byte   "SDHC$"
stxc:   .byte   "SDXC$"
ssectors: .byte " sectors$"
sopen:  .byte   " ($"
smbclose: .byte " MB)$"
sfat:   .byte   "FAT$"
swp:    .byte   "write-protected$"
sfirm:  .byte   "firmware $"
snamed: .byte   "named ", '"', "$"
sunit:  .byte   "unit $"
sro:    .byte   "read-only$"
sdrives: .byte  "  DRIVES     $"
sk:     .byte   "K$"
skfree: .byte   "K free$"
sdisk2: .byte   "O: and P:, the Disk II's (not looked at)$"
sinput: .byte   "INPUT      $"
snousb: .byte   "no USB card$"
snodevs: .byte  "nothing plugged into the USB card$"
skbd:   .byte   "USB keyboard$"
smouse: .byte   "USB mouse$"
spad:   .byte   "USB gamepad$"
snoarea: .byte  "no event area: the game port and joysticks not read$"
sgame:  .byte   "game port: paddles$"
snobtn: .byte   "no button down$"
sbtn:   .byte   "a button down$"
sidle:  .byte   "joysticks 1 and 2: nothing pressed (or nothing plugged in)$"
spressed: .byte "joysticks: a direction or fire pressed$"
sclock: .byte   "CLOCK      $"
srtcbusy: .byte "the clock chip stayed busy$"
slost:  .byte   "the oscillator stopped: set the time$"
skept:  .byte   "the oscillator kept time$"
ssemi:  .byte   "; $"
sdegc:  .byte   " C$"
quart:  .byte   "0257"
spanel: .byte   "PANEL      $"
snopanel: .byte "no console card$"
sswitch: .byte  "switches $"
shalt:  .byte   "HALT/ENABLE at HALT$"
senable: .byte  "HALT/ENABLE at ENABLE$"
sinst:  .byte   "S INST$"
sbuscyc: .byte  "S BUS CYCLE$"

; Blocks the functions take.
opblk:  .byte   FT_OPEN
        .word   area
        .byte   EV_LEN2
dlyblk: .byte   FT_DELAY
        .word   50                      ; ms: a few ticks' samples
diblk:  .byte   DM_INFO                 ; DM_INFO fills the 16 bytes after it
        .res    16, 0
dinfo   = diblk

        .bss
all:    .res    1
only:   .res    1
sect:   .res    1
areaok: .res    1
wbuf:   .res    16
tlen:   .res    1
rtcbad: .res    1
frmbad: .res    1
hzrtc:  .res    4
hzfrm:  .res    4
frac:   .res    2
tmp:    .res    1
tmp2:   .res    1
left:   .res    2
ver:    .res    10
fcb:    .res    36
rec:    .res    128
mcb:    .res    5
vunits: .res    1
mod:    .res    1
sninfo: .res    4
first:  .res    1
sec0:   .res    2
unit:   .res    1
drv:    .res    1
space:  .res    3
dat:    .res    4
secs:   .res    1
name:   .res    2
area:   .res    EV_LEN2

        .code
        .include "cmd.inc"
        .include "util.inc"
