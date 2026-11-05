; SETDEF.COM (hw/oodos.md, "The utilities, phase 4b"; the CP/M 3 User's
; Guide's SETDEF): the CCP's drive search chain, the temporary drive, the
; filetype search order, the display mode and the page mode, in the SCB.
;
;   SETDEF {d:{,d:{,d:{,d:}}}} {[TEMPORARY=d: | ORDER=(typ{,typ})]}
;   SETDEF [DISPLAY | NO DISPLAY]
;   SETDEF [PAGE | NO PAGE]
;
; The options together in one list, any unambiguous prefix. The chain is
; the SCB's $4C-$4F (0 the default drive, *; 1-16 A-P; $FF the end), up to
; four drives; TEMPORARY $50 (0 the default, *: SUBMIT's temporary files go
; there); DISPLAY $18's bit 7, its other bits kept; ORDER $18's bits 0-1 (ours,
; 5c: 0 COM, 1 COM then SUB, 2 SUB then COM, 3 SUB), (COM), (SUB), (COM,SUB) or
; (SUB,COM); PAGE $2C (0 on, $FF off). Then, and alone, it shows them all.
;
; Errors: "Drive defined twice in search Path", "More than four drives
; specified.", "Invalid drive.", "Invalid option.", "Invalid type for ORDER
; option.", "Invalid delimiter."; nothing set after any, the return code
; $FF00.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

SCB_DISP  = $18
SCB_PMODE = $2C
SCB_CHAIN = $4C
SCB_TEMP  = $50

O_TEMP    = 0
O_ORDER   = 1
O_DISP    = 2
O_NODISP  = 3
O_PAGE    = 4
O_NOPAGE  = 5

        .code
start:  jsr     UT_INIT
        jsr     CM_PAGE
        stz     ndrv
        stz     settemp
        stz     setdisp
        stz     setord
        stz     setpage
        jsr     UT_SKIP
        jeq     apply
        cmp     #'['
        jeq     opts
; The chain: drives separated by commas.
@drive: jsr     UT_GET
        cmp     #'*'
        bne     @letter
        lda     #0
        bra     @got
@letter: sec
        sbc     #'A'
        cmp     #16
        jcs     baddrv
        sta     t                       ; (UT_PEEK and UT_GET change X)
        jsr     UT_PEEK
        cmp     #':'
        jne     baddrv
        jsr     UT_GET
        lda     t
        inc     a
@got:   ldx     ndrv                    ; defined already?
@twice: dex
        bmi     @new
        cmp     drives,x
        jeq     twice
        bra     @twice
@new:   ldx     ndrv
        cpx     #4
        jcs     four
        sta     drives,x
        inc     ndrv
        jsr     UT_SKIP
        jeq     apply
        cmp     #'['
        beq     opts
        cmp     #','
        jne     baddel
        jsr     UT_GET
        jsr     UT_SKIP
        jeq     baddrv                  ; a comma with no drive after it
        bra     @drive

; The option list, then nothing after it.
opts:   jsr     UT_OAT
@opt:   lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcc     @known
        cmp     #UT_OEND
        jne     badopt
        jsr     UT_OCLOSE
        jcs     badopt
        jsr     UT_SKIP
        jne     baddel
        jmp     apply
@known: sta     t
        cmp     #O_ORDER + 1            ; TEMPORARY and ORDER take a value
        bcc     @value
        lda     UT_HASV                 ; the others none
        jne     badopt
        lda     t
        cmp     #O_DISP
        bne     :+
        lda     #$80
        sta     setdisp                 ; $80 on
        bra     @opt
:       cmp     #O_NODISP
        bne     :+
        lda     #$01
        sta     setdisp                 ; $01 off
        bra     @opt
:       cmp     #O_PAGE
        bne     :+
        lda     #$01
        sta     setpage                 ; $01 on (0 in the SCB)
        bra     @opt
:       lda     #$FF
        sta     setpage                 ; $FF off
        bra     @opt
@value: lda     UT_HASV
        jeq     badopt
        lda     t
        bne     @order
        jsr     UT_VALUE                ; TEMPORARY=d:, one drive: a list is
        cmp     #1                      ; none
        jeq     baddrv
        jsr     UT_VITEM
        jcs     baddrv
        jsr     UT_WDRIVE
        jcs     baddrv
        sta     temp
        lda     #1
        sta     settemp
        jmp     @opt
@order: stz     nitem
        stz     types
        stz     types + 1
@item:  jsr     UT_VITEM                ; ORDER=(typ{,typ}), typ COM or SUB
        bcc     @typ
        cmp     #0
        jne     badtyp                  ; a list not closed, an item too long
        lda     nitem
        jeq     badtyp
        lda     types                   ; the two the same: refused
        cmp     types + 1
        jeq     badtyp
        ldx     #0                      ; (1,0) COM, (1,2) COM SUB, (2,1) SUB COM, (2,0) SUB
        lda     types
        cmp     #2
        bne     :+
        ldx     #2
:       lda     types + 1
        beq     :+
        cpx     #0
        bne     @val
        ldx     #1
        bra     @val
:       cpx     #2
        bne     @val
        ldx     #3
@val:   stx     order
        lda     #1
        sta     setord
        jmp     @opt
@typ:   ldy     nitem
        cpy     #2
        jcs     badtyp                  ; at most two
        inc     nitem
        ldx     #0
        lda     UT_WLEN
        cmp     #3
        jne     badtyp
@com:   lda     UT_WORD,x
        cmp     scom,x
        bne     @sub
        inx
        cpx     #3
        bne     @com
        lda     #1
        bra     @inext
@sub:   ldx     #0
@s:     lda     UT_WORD,x
        cmp     ssub,x
        jne     badtyp
        inx
        cpx     #3
        bne     @s
        lda     #2
@inext: sta     types,y
        jmp     @item                   ; (a word value is one item)

twice:  lda     #<stwice
        ldx     #>stwice
        bra     error
four:   lda     #<sfour
        ldx     #>sfour
        bra     error
baddrv: lda     #<sbaddrv
        ldx     #>sbaddrv
        bra     error
badopt: lda     #<sbadopt
        ldx     #>sbadopt
        bra     error
badtyp: lda     #<sbadtyp
        ldx     #>sbadtyp
        bra     error
baddel: lda     #<sbaddel
        ldx     #>sbaddel
error:  jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; ---- setting and showing ---------------------------------------------------

apply:  ldx     ndrv                    ; the chain, $FF after its last
        beq     @temp
        stx     t
        ldx     #0
@chain: lda     #$FF
        cpx     t
        bcs     :+
        lda     drives,x
:       phx
        tay
        txa
        clc
        adc     #SCB_CHAIN
        phy
        plx
        jsr     UT_SCBSET
        plx
        inx
        cpx     #4
        bne     @chain
@temp:  lda     settemp
        beq     @disp
        lda     #SCB_TEMP
        ldx     temp
        jsr     UT_SCBSET
@disp:  lda     setord                  ; the order: $18's bits 0-1, the rest kept
        beq     @dmode
        lda     #SCB_DISP
        jsr     UT_SCBGET
        and     #$FC
        ora     order
        tax
        lda     #SCB_DISP
        jsr     UT_SCBSET
@dmode: lda     setdisp
        beq     @page
        lda     #SCB_DISP               ; bit 7, the rest kept
        jsr     UT_SCBGET
        and     #$7F
        ldx     setdisp
        bpl     :+
        ora     #$80
:       tax
        lda     #SCB_DISP
        jsr     UT_SCBSET
@page:  lda     setpage
        beq     show
        ldx     #0
        cmp     #$FF
        bne     :+
        ldx     #$FF
:       lda     #SCB_PMODE
        jsr     UT_SCBSET
        jsr     CM_PAGE                 ; the page mode as it is now

show:   lda     #<spath
        ldx     #>spath
        jsr     UT_SAY
        stz     t
@drive: lda     t
        clc
        adc     #SCB_CHAIN
        jsr     UT_SCBGET
        cmp     #$FF
        beq     @rest
        pha
        lda     t                       ; "1st Drive            - "
        asl     a
        asl     a
        clc
        adc     #<sords
        ldx     #>sords
        bcc     :+
        inx
:       jsr     CM_PUTS
        lda     #<sdrvdash
        ldx     #>sdrvdash
        jsr     CM_PUTS
        pla
        jsr     putdrv
        jsr     CM_EOL
        inc     t
        lda     t
        cmp     #4
        bne     @drive
@rest:  jsr     CM_FLUSH                ; a blank line
        jsr     CM_EOL
        lda     #<sorder
        ldx     #>sorder
        jsr     CM_PUTS
        lda     #SCB_DISP
        jsr     UT_SCBGET
        and     #3
        asl     a
        tax
        lda     sordv,x
        pha
        lda     sordv + 1,x
        tax
        pla
        jsr     UT_SAY
        lda     #<stemp
        ldx     #>stemp
        jsr     CM_PUTS
        lda     #SCB_TEMP
        jsr     UT_SCBGET
        jsr     putdrv
        jsr     CM_EOL
        lda     #<spmode
        ldx     #>spmode
        jsr     CM_PUTS
        lda     #SCB_PMODE
        jsr     UT_SCBGET
        cmp     #0
        jsr     onoff0
        lda     #<sdmode
        ldx     #>sdmode
        jsr     CM_PUTS
        lda     #SCB_DISP
        jsr     UT_SCBGET
        and     #$80
        eor     #$80                    ; 0: on
        jsr     onoff0
        jmp     UT_EXIT

; A drive, 0 the default: "Default" or "M:".
putdrv: cmp     #0
        bne     :+
        lda     #<sdefault
        ldx     #>sdefault
        jmp     CM_PUTS
:       dec     a
        jmp     UT_PUTDRV

; "On" if A is 0, else "Off"; a line owed.
onoff0: cmp     #0
        bne     :+
        lda     #<son
        ldx     #>son
        jmp     UT_SAY
:       lda     #<soff
        ldx     #>soff
        jmp     UT_SAY

        .include "cmd.inc"
        .include "util.inc"

        .rodata
OPTAB:  .byte   "TEMPORARY", 0, "ORDER", 0, "DISPLAY", 0, "NODISPLAY", 0, "PAGE", 0, "NOPAGE", 0, 0
scom:   .byte   "COM"
ssub:   .byte   "SUB"
stwice: .byte   "Drive defined twice in search Path$"
sfour:  .byte   "More than four drives specified.$"
sbaddrv: .byte  "Invalid drive.$"
sbadopt: .byte  "Invalid option.$"
sbadtyp: .byte  "Invalid type for ORDER option.$"
sbaddel: .byte  "Invalid delimiter.$"
spath:  .byte   "Drive Search Path:$"
sords:  .byte   "1st$", "2nd$", "3rd$", "4th$"
sdrvdash: .byte " Drive            - $"
sorder: .byte   "Search Order         - $"
sordv:  .word   sord0, sord1, sord2, sord3
sord0:  .byte   "COM$"
sord1:  .byte   "COM, SUB$"
sord2:  .byte   "SUB, COM$"
sord3:  .byte   "SUB$"
stemp:  .byte   "Temporary Drive      - $"
spmode: .byte   "Console Page Mode    - $"
sdmode: .byte   "Program Name Display - $"
sdefault: .byte "Default$"
son:    .byte   "On$"
soff:   .byte   "Off$"

        .bss
drives: .res    4
ndrv:   .res    1
temp:   .res    1
settemp: .res   1
setdisp: .res   1
setpage: .res   1
types:  .res    2
order:  .res    1
setord: .res    1
nitem:  .res    1
t:      .res    1
