; CMDTEST: cmd.inc's console mode handling (hw/oodos.md, "The CCP, phase
; 4"), for test_oodos_ccp.cpp, which types what it waits for. It prints:
;
;   NOOP n              the console mode after a CM_UNPAGE with no CM_PAGE
;                       before it (10 set before it: 10 wanted)
;   PAGE n UNPAGE n     the console mode after CM_PAGE called twice (11
;                       wanted) and after one CM_UNPAGE (10 again: the
;                       second CM_PAGE kept the first's mode)
;   KEY?                then waits for a key, in console mode 0, and calls
;                       CM_BRK, which must leave it alone
;   KEPT n              the key 6 then reads (its code)
;   BRK?                then waits for a ^C, after CM_PAGE, and calls CM_BRK
;   BROKE MODE n        CMD_BREAK reached; the console mode after CM_UNPAGE
;                       (0, as before CM_PAGE)
;   NO BREAK            CM_BRK came back instead

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = broke

        OODOS_HEADER

        .code
start:  lda     #10                     ; a program's console mode, kept by
        ldx     #0                      ; CM_PAGE and put back by CM_UNPAGE
        ldy     #C_MODE
        jsr     BDOS
        jsr     CM_UNPAGE               ; not paged: nothing
        lda     #<snoop
        ldx     #>snoop
        jsr     CM_PUTS
        jsr     mode
        jsr     CM_PAGE
        jsr     CM_PAGE                 ; again: the first's mode kept
        lda     #<spage
        ldx     #>spage
        jsr     CM_PUTS
        jsr     mode
        jsr     CM_UNPAGE
        lda     #<sunpage
        ldx     #>sunpage
        jsr     CM_PUTS
        jsr     mode
        jsr     CM_CRLF
        lda     #0                      ; mode 0: CM_BRK leaves a key alone
        tax
        ldy     #C_MODE
        jsr     BDOS
        lda     #<skey
        ldx     #>skey
        jsr     CM_PUTS
        jsr     wait
        jsr     CM_BRK
        lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        pha
        lda     #<skept
        ldx     #>skept
        jsr     CM_PUTS
        pla
        ldx     #0
        jsr     CM_DECOUT
        jsr     CM_CRLF
        jsr     CM_PAGE                 ; paged: CM_BRK takes the ^C
        lda     #<sbrk
        ldx     #>sbrk
        jsr     CM_PUTS
        jsr     wait
        jsr     CM_BRK
        lda     #<snobrk
        ldx     #>snobrk
        jsr     CM_PUTS
        jmp     WBOOT

broke:  lda     #<sbroke
        ldx     #>sbroke
        jsr     CM_PUTS
        jsr     CM_UNPAGE
        jsr     mode
        jsr     CM_CRLF
        jmp     WBOOT

; Until 11 says a key waits.
wait:   ldy     #C_STAT
        jsr     BDOS
        cmp     #0
        beq     wait
        rts

; The console mode, in decimal.
mode:   lda     #$FF
        tax
        ldy     #C_MODE
        jsr     BDOS
        jmp     CM_DECOUT

        .include "cmd.inc"

        .rodata
snoop:  .byte   "NOOP $"
spage:  .byte   " PAGE $"
sunpage: .byte  " UNPAGE $"
skey:   .byte   "KEY?", 13, 10, '$'
skept:  .byte   "KEPT $"
sbrk:   .byte   "BRK?", 13, 10, '$'
sbroke: .byte   "BROKE MODE $"
snobrk: .byte   "NO BREAK", 13, 10, '$'
