; DATE.COM (hw/oodos.md, "The utilities, phase 4b"; the CP/M 3 User's
; Guide's DATE): the date and time shown and set.
;
;   DATE                    "Sat 10/31/26 12:00:00" (105)
;   DATE C                  (CONTINUOUS, or any prefix) again on the same
;                           line each second, to a key, which is taken
;   DATE MM/DD/YY HH:MM:SS  checked, "Press any key to set time", a key,
;                           the clock set: the SCB's date, hour, minute and
;                           second (49), then the BIOS's TIME (50), so the
;                           seconds are kept; then shown
;   DATE SET                "Enter today's date (MM/DD/YY): " and "Enter
;                           the time (HH:MM:SS): ", a line each, an empty
;                           one keeping that part (the clock's at the key)
;
; Each number is one or two digits; the year is 20YY (OODOS's clock holds
; 2000 on). Errors: "Illegal date/time specification." for anything else, a
; date or time out of range among them; "No clock" for a clock past 2155
; (105's $FF). The return code $FF00 after either.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"
        .include "system.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

SCB_DATE = $58                          ; the SCB's date, hour, minute, second
SCB_HOUR = $5A
SCB_MIN  = $5B
SCB_SEC  = $5C

        .code
start:  jsr     UT_INIT
        jsr     UT_SKIP
        bne     :+
        jmp     now
:       cmp     #'0'
        bcc     word
        cmp     #'9' + 1
        bcs     word
        jsr     pdate                   ; DATE MM/DD/YY HH:MM:SS
        jcs     illegal
        jsr     UT_PEEK                 ; a blank between
        cmp     #' '
        beq     :+
        cmp     #$09
        jne     illegal
:       jsr     UT_SKIP
        jsr     ptime
        jcs     illegal
        jsr     UT_SKIP
        jne     illegal
        stz     keepd
        stz     keept
        jmp     setit

; A word: SET, or CONTINUOUS or a prefix of it.
word:   ldy     #0
@char:  jsr     UT_PEEK
        beq     @end
        cmp     #' '
        beq     @end
        cmp     #$09
        beq     @end
        cpy     #10
        jcs     illegal
        sta     wbuf,y
        iny
        jsr     UT_GET
        bra     @char
@end:   sty     wlen
        jsr     UT_SKIP
        jne     illegal
        ldy     #0                      ; SET, in full
@set:   lda     wbuf,y
        cmp     sset,y
        bne     @cont
        iny
        cpy     #3
        bne     @set
        cpy     wlen
        bne     @cont
        jmp     dset
@cont:  ldy     #0                      ; CONTINUOUS's start
@c:     lda     wbuf,y
        cmp     scont,y
        jne     illegal
        iny
        cpy     wlen
        bne     @c
        jmp     cont

illegal: lda    #<sillegal
        ldx     #>sillegal
error:  jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT
noclock: lda    #<snoclock
        ldx     #>snoclock
        bra     error

; ---- showing ---------------------------------------------------------------

; The clock into dat and sec (105): carry set if it is past 2155.
clock:  lda     #<dat
        ldx     #>dat
        ldy     #T_GET
        jsr     BDOS
        cmp     #$FF
        bne     @ok
        cpx     #$FF
        beq     @none
@ok:    sta     secs
        clc
        rts
@none:  sec
        rts

; "Sat 10/31/26 12:00:00"
show:   lda     dat
        ldx     dat + 1
        jsr     UT_PUTWDAY
        lda     #' '
        jsr     CM_PUTC
        lda     dat
        ldx     dat + 1
        jsr     UT_PUTDATE
        lda     #' '
        jsr     CM_PUTC
        lda     dat + 2
        ldx     dat + 3
        ldy     secs
        jmp     UT_PUTTIMES

now:    jsr     clock
        bcs     noclock
        jsr     show
        jmp     UT_EXIT

; DATE C: the line again whenever the second changes, to a key (6 takes it).
cont:   jsr     clock
        bcs     noclock
        jsr     show
@wait:  lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        bne     @key
        lda     secs
        sta     shown
        jsr     clock
        bcs     @wait
        lda     secs
        cmp     shown
        beq     @wait
        lda     #$0D
        jsr     CM_PUTC
        jsr     show
        bra     @wait
@key:   jmp     UT_EXIT

; ---- reading ---------------------------------------------------------------

; A number of one or two digits at UT_IDX, past it: A; carry set if none.
num:    ldy     UT_IDX
        sty     t
        lda     #<UT_TAIL
        ldx     #>UT_TAIL
        jsr     CM_DECIN
        bcs     @bad
        sty     UT_IDX
        pha
        tya
        sec
        sbc     t
        cmp     #3
        pla
        rts                             ; carry set: three digits or more
@bad:   rts

; The character at UT_IDX is A: past it, carry clear; else carry set.
sep:    sta     t
        jsr     UT_PEEK
        cmp     t
        bne     @no
        jsr     UT_GET
        clc
        rts
@no:    sec
        rts

; MM/DD/YY at UT_IDX into month, day and UT_YEAR, checked: carry set if not.
pdate:  jsr     num
        bcs     @bad
        sta     month
        lda     #'/'
        jsr     sep
        bcs     @bad
        jsr     num
        bcs     @bad
        sta     day
        lda     #'/'
        jsr     sep
        bcs     @bad
        jsr     num
        bcs     @bad
        clc                             ; 20YY
        adc     #<2000
        sta     UT_YEAR
        lda     #>2000
        adc     #0
        sta     UT_YEAR + 1
        lda     UT_YEAR                 ; kept: UT_READ and the time leave it
        sta     year
        lda     UT_YEAR + 1
        sta     year + 1
        lda     month                   ; 1-12
        beq     @bad
        cmp     #13
        bcs     @bad
        lda     day                     ; 1 to the month's days
        beq     @bad
        lda     month
        jsr     UT_MLEN
        cmp     day
        bcc     @bad
        clc
        rts
@bad:   sec
        rts

; HH:MM:SS at UT_IDX into hour, minute and second, checked: carry set if not.
ptime:  jsr     num
        bcs     @bad
        cmp     #24
        bcs     @bad
        sta     hour
        lda     #':'
        jsr     sep
        bcs     @bad
        jsr     num
        bcs     @bad
        cmp     #60
        bcs     @bad
        sta     minute
        lda     #':'
        jsr     sep
        bcs     @bad
        jsr     num
        bcs     @bad
        cmp     #60
        bcs     @bad
        sta     second
        clc
        rts
@bad:   sec
        rts

; DATE SET: the date and the time asked for, an empty line (or one of only
; blanks and tabs) keeping each.
dset:   stz     keepd
        stz     keept
        lda     #<sasked
        ldx     #>sasked
        jsr     UT_READ
        jsr     UT_SKIP
        bne     @date
        inc     keepd
        bra     @time
@date:  jsr     pdate
        jcs     illegal
        jsr     UT_SKIP
        jne     illegal
@time:  lda     #<saskt
        ldx     #>saskt
        jsr     UT_READ
        jsr     UT_SKIP
        bne     @t
        inc     keept
        bra     setit
@t:     jsr     ptime
        jcs     illegal
        jsr     UT_SKIP
        jne     illegal
        bra     setit

; ---- setting ---------------------------------------------------------------

; A (0-99) in BCD.
bcd:    ldx     #0
@tens:  cmp     #10
        bcc     @done
        sbc     #10
        inx
        bra     @tens
@done:  sta     t
        txa
        asl     a
        asl     a
        asl     a
        asl     a
        ora     t
        rts

; At a key, the clock set from what was given and, for a part kept, the
; clock's own at that moment; then shown.
setit:  lda     #<spress
        ldx     #>spress
        jsr     UT_SAY
        lda     #$FD                    ; a key, waited for
        ldy     #C_RAWIO
        jsr     BDOS
        lda     keepd
        ora     keept
        beq     @given
        jsr     clock                   ; a part kept: the clock's
        jcs     noclock
@given: lda     keepd
        bne     @time
        lda     year
        sta     UT_YEAR
        lda     year + 1
        sta     UT_YEAR + 1
        lda     month
        sta     UT_MONTH
        lda     day
        sta     UT_DAY
        jsr     UT_TODAYS
        sta     dat
        stx     dat + 1
@time:  lda     keept
        bne     @set
        lda     hour
        jsr     bcd
        sta     dat + 2
        lda     minute
        jsr     bcd
        sta     dat + 3
        lda     second
        jsr     bcd
        sta     secs
@set:   lda     dat
        sta     UT_SCBW
        lda     dat + 1
        sta     UT_SCBW + 1
        lda     #SCB_DATE
        jsr     UT_SCBSETW
        lda     #SCB_HOUR
        ldx     dat + 2
        jsr     UT_SCBSET
        lda     #SCB_MIN
        ldx     dat + 3
        jsr     UT_SCBSET
        lda     #SCB_SEC
        ldx     secs
        jsr     UT_SCBSET
        lda     #BIOS::TIME             ; the SCB's into the clock
        ldx     #$FF
        jsr     UT_BIOS
        jsr     clock
        jcs     noclock
        jsr     show
        jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
sset:   .byte   "SET"
scont:  .byte   "CONTINUOUS"
sillegal: .byte "Illegal date/time specification.$"
snoclock: .byte "No clock$"
sasked: .byte   "Enter today's date (MM/DD/YY): $"
saskt:  .byte   "Enter the time (HH:MM:SS): $"
spress: .byte   "Press any key to set time$"

        .bss
dat:    .res    4                       ; days, hour, minute (BCD)
secs:   .res    1                       ; BCD
shown:  .res    1
month:  .res    1
day:    .res    1
year:   .res    2
hour:   .res    1
minute: .res    1
second: .res    1
keepd:  .res    1
keept:  .res    1
t:      .res    1
wbuf:   .res    10
wlen:   .res    1
