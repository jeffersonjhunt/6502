; SHOW.COM (hw/oodos.md, "The utilities, phase 4b"; the CP/M 3 User's
; Guide's SHOW): a drive's access mode and free space, its label, its users,
; its free directory entries and its characteristics.
;
;   SHOW {d:}{[SPACE|LABEL|USERS|DIR|DRIVE]}
;
;   SPACE (or none)  "A: RW, Space:     8,064K" (29, 46) for d:, or each
;                    logged-in drive (24), A to P
;   LABEL            the label: its name, passwords, stamps and its own
;                    stamps; "No directory label exists."
;   USERS            the current user, the users with files and their counts
;                    (a file is its extent-zero entry), the free entries
;   DIR              "A: Number of free directory entries: 24"
;   DRIVE            the DPB's numbers in the Guide's words
;
; One option, any unambiguous prefix. Errors: "Unrecognized drive." (a
; letter past P), "Unrecognized option.", "Unrecognized input." (anything
; else); a drive that isn't is the BDOS's error (mode $FE). The return code
; $FF00 after any.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

O_SPACE = 0
O_LABEL = 1
O_USERS = 2
O_DIR   = 3
O_DRIVE = 4

        .code
start:  jsr     UT_INIT
        jsr     CM_PAGE
        lda     #$FF                    ; no drive given
        sta     drv
        stz     opt                     ; SPACE
        jsr     UT_SKIP
        jeq     go
        cmp     #'['
        beq     opts
        sec                             ; a drive: a letter and ':'
        sbc     #'A'
        cmp     #26
        jcs     badin
        sta     cur
        ldx     UT_IDX
        lda     UT_TAIL + 1,x
        cmp     #':'
        jne     badin
        lda     cur
        cmp     #16
        jcs     baddrv
        sta     drv
        inx
        inx
        stx     UT_IDX
        jsr     UT_SKIP
        jeq     go
        cmp     #'['
        jne     badin
opts:   jsr     UT_OAT
        lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcc     @one
        cmp     #UT_OEND                ; [] : none, SPACE
        jne     badopt
        bra     @close
@one:   sta     opt
        lda     UT_HASV                 ; no option takes a value
        jne     badopt
        lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcc     badopt                  ; a second option
        cmp     #UT_OEND
        jne     badopt
@close: jsr     UT_OCLOSE
        jcs     badin
        jsr     UT_SKIP
        jne     badin
go:     lda     opt
        bne     @drive
        jmp     space
@drive: lda     drv                     ; the others: d: or the current drive
        bpl     @sel
        lda     UT_DRV0
        sta     drv
@sel:   jsr     UT_SELECT
        cmp     #0
        beq     @kind
        jmp     fail
@kind:  lda     opt
        cmp     #O_LABEL
        bne     :+
        jmp     label
:       cmp     #O_USERS
        bne     :+
        jmp     users
:       cmp     #O_DIR
        bne     :+
        jmp     dir
:       jmp     drive

baddrv: lda     #<sbaddrv
        ldx     #>sbaddrv
        bra     error
badopt: lda     #<sbadopt
        ldx     #>sbadopt
        bra     error
badin:  lda     #<sbadin
        ldx     #>sbadin
error:  jsr     UT_SAY
fail:   jsr     UT_FAIL
        jmp     UT_EXIT

; ---- helpers ---------------------------------------------------------------

; A blank line: the line owed ended, another owed.
blank:  jsr     CM_FLUSH
        jmp     CM_EOL

; UT_N right in A columns, with commas.
putn:   sec
        jmp     UT_NOUT

; The 16-bit A/X as UT_N.
setn:   sta     UT_N
        stx     UT_N + 1
        stz     UT_N + 2
        rts

; Bit cur of the 16-bit vector A/X: carry.
vbit:   sta     vec
        stx     vec + 1
        ldy     cur
        beq     @test
@shift: lsr     vec + 1
        ror     vec
        dey
        bne     @shift
@test:  lda     vec
        lsr     a
        rts

; ---- SPACE -----------------------------------------------------------------

space:  lda     drv
        bmi     @all
        jsr     UT_SELECT
        cmp     #0
        jne     fail
        lda     drv
        sta     cur
        jsr     space1
        jcs     fail
        jmp     UT_EXIT
@all:   ldy     #DRV_LOGINVEC
        jsr     BDOS
        sta     login
        stx     login + 1
        stz     cur
@drive: lda     login
        ldx     login + 1
        jsr     vbit
        bcc     @next
        jsr     space1
        jcs     fail
@next:  inc     cur
        lda     cur
        cmp     #16
        bne     @drive
        jmp     UT_EXIT

; Drive cur's line: carry set if 46 failed (the BDOS said why).
space1: lda     cur
        jsr     UT_PUTDRV
        lda     #<srw
        ldx     #>srw
        jsr     CM_PUTS
        ldy     #DRV_ROVEC
        jsr     BDOS
        jsr     vbit
        lda     #'W'
        bcc     :+
        lda     #'O'
:       jsr     CM_PUTC
        lda     #<sspace
        ldx     #>sspace
        jsr     CM_PUTS
        lda     #<UT_REC                ; 46's three bytes: the free records
        ldx     #>UT_REC
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     cur
        ldy     #DRV_SPACE
        jsr     BDOS
        cmp     #$FF
        beq     @fail
        lda     #<UT_REC
        ldx     #>UT_REC
        jsr     UT_LDN
        lda     #8                      ; kilobytes
        jsr     UT_DIVN
        lda     #9
        jsr     putn
        lda     #'K'
        jsr     CM_PUTC
        jsr     CM_EOL
        clc
        rts
@fail:  jsr     CM_EOL
        sec
        rts

; ---- LABEL -----------------------------------------------------------------

label:  jsr     UT_LABEL
        bcc     @found
        cpx     #0                      ; the scan failed: said by the BDOS
        beq     :+
        jmp     fail
:       lda     #<snolbl
        ldx     #>snolbl
        jmp     error
@found: lda     #<slblfor
        ldx     #>slblfor
        jsr     CM_PUTS
        lda     drv
        jsr     UT_PUTDRV
        jsr     CM_EOL
        jsr     blank
        lda     #<shead1
        ldx     #>shead1
        jsr     UT_SAY
        lda     #<shead2c
        ldx     #>shead2c
        bit     UT_LBL + 12             ; bit 6: access stamps
        bvc     :+
        lda     #<shead2a
        ldx     #>shead2a
:       jsr     UT_SAY
        lda     #<shead3
        ldx     #>shead3
        jsr     UT_SAY
        lda     #<(UT_LBL + 1)          ; the name, as NAME.TYP, in 12 and 2
        ldx     #>(UT_LBL + 1)
        jsr     CM_FNAME
        ldx     #0                      ; its length, as CM_FNAME writes it
@name:  lda     UT_LBL + 1,x
        and     #$7F
        cmp     #' '
        beq     @type
        inx
        cpx     #8
        bne     @name
@type:  stx     len
        lda     UT_LBL + 9
        and     #$7F
        cmp     #' '
        beq     @pad
        inc     len                     ; the '.'
        ldx     #8
@tchar: lda     UT_LBL + 1,x
        and     #$7F
        cmp     #' '
        beq     @pad
        inc     len
        inx
        cpx     #11
        bne     @tchar
@pad:   lda     #14
        sec
        sbc     len
        jsr     UT_BLANKS
        lda     UT_LBL + 12             ; passwords: bit 7
        and     #$80
        ldx     #9
        jsr     onoff
        lda     UT_LBL + 12             ; create or access: bits 4 and 6
        and     #$50
        ldx     #8
        jsr     onoff
        lda     UT_LBL + 12             ; update: bit 5
        and     #$20
        ldx     #8
        jsr     onoff
        lda     #<(UT_LBL + 24)
        ldx     #>(UT_LBL + 24)
        jsr     UT_PUTSTAMP
        lda     #2
        jsr     UT_BLANKS
        lda     #<(UT_LBL + 28)
        ldx     #>(UT_LBL + 28)
        jsr     UT_PUTSTAMP
        jmp     UT_EXIT

; "on" if A isn't 0, else "off", in a field of X.
onoff:  stx     len
        cmp     #0
        beq     @off
        lda     #<son
        ldx     #>son
        jsr     CM_PUTS
        lda     len
        sec
        sbc     #2
        jmp     UT_BLANKS
@off:   lda     #<soff
        ldx     #>soff
        jsr     CM_PUTS
        lda     len
        sec
        sbc     #3
        jmp     UT_BLANKS

; ---- USERS and DIR ---------------------------------------------------------

; The directory counted: each user's files (extent-zero entries) in counts,
; the free entries in free. Carry set if the scan failed.
count:  jsr     UT_DPB
        lda     UT_DPBC + 4             ; EXM: an entry's extents past 0 in it
        eor     #$1F
        sta     exmask
        ldx     #31
@zero:  stz     counts,x
        dex
        bpl     @zero
        stz     free
        stz     free + 1
        jsr     UT_SCAN
@entry: bcs     @end
        sta     @user + 1
        stx     @user + 2
        sta     @ex + 1
        stx     @ex + 2
        sta     @s2 + 1
        stx     @s2 + 2
@user:  lda     $FFFF
        cmp     #$E5
        bne     @used
        inc     free
        bne     @next
        inc     free + 1
        bra     @next
@used:  cmp     #16
        bcs     @next                   ; not a file's (an XFCB, the label, an SFCB)
        asl     a
        tax
        ldy     #14
@s2:    lda     $FFFF,y                 ; S2 0 and EX within EXM: extent zero
        bne     @next
        ldy     #12
@ex:    lda     $FFFF,y
        and     exmask
        bne     @next
        inc     counts,x
        bne     @next
        inc     counts + 1,x
@next:  jsr     UT_SNEXT
        bra     @entry
@end:   cpx     #0
        bne     @failed
        clc
        rts
@failed: sec
        rts

users:  jsr     count
        bcc     :+
        jmp     fail
:       lda     #<sactu
        ldx     #>sactu
        jsr     CM_PUTS
        lda     #$FF                    ; the current user
        ldy     #F_USERNUM
        jsr     BDOS
        ldx     #0
        jsr     setn
        lda     #3
        jsr     putn
        jsr     CM_EOL
        lda     #<sactf
        ldx     #>sactf
        jsr     CM_PUTS
        stz     cur
@user:  jsr     hasf
        beq     @unext
        lda     cur
        ldx     #0
        jsr     setn
        lda     #4
        jsr     putn
@unext: inc     cur
        lda     cur
        cmp     #16
        bne     @user
        jsr     CM_EOL
        lda     drv
        jsr     UT_PUTDRV
        lda     #<snfiles
        ldx     #>snfiles
        jsr     CM_PUTS
        stz     cur
@count: jsr     hasf
        beq     @cnext
        lda     counts,x
        pha
        lda     counts + 1,x
        tax
        pla
        jsr     setn
        lda     #4
        jsr     putn
@cnext: inc     cur
        lda     cur
        cmp     #16
        bne     @count
        jsr     CM_EOL
        jsr     freeln
        jmp     UT_EXIT

; User cur's count not 0: Z clear; X its index in counts.
hasf:   lda     cur
        asl     a
        tax
        lda     counts,x
        ora     counts + 1,x
        rts

dir:    jsr     count
        bcc     :+
        jmp     fail
:       jsr     freeln
        jmp     UT_EXIT

; "A: Number of free directory entries: n"
freeln: lda     drv
        jsr     UT_PUTDRV
        lda     #<sfree
        ldx     #>sfree
        jsr     CM_PUTS
        lda     free
        ldx     free + 1
        jsr     CM_DECOUT
        jmp     CM_EOL

; ---- DRIVE -----------------------------------------------------------------

drive:  jsr     UT_DPB
        lda     #4
        jsr     UT_BLANKS
        lda     drv
        jsr     UT_PUTDRV
        lda     #<schar
        ldx     #>schar
        jsr     UT_SAY
        jsr     blocks                  ; (DSM + 1) x BLS / 128: the records
        lda     #<srecs
        ldx     #>srecs
        jsr     line
        jsr     blocks                  ; / 8: the kilobytes
        lda     #8
        jsr     UT_DIVN
        lda     #<skb
        ldx     #>skb
        jsr     line
        lda     UT_DPBC + 7             ; DRM + 1
        ldx     UT_DPBC + 8
        jsr     setn
        jsr     inc24
        lda     #<sents
        ldx     #>sents
        jsr     line
        lda     UT_DPBC + 11            ; CKS's low 15 bits
        ldx     UT_DPBC + 12
        pha
        txa
        and     #$7F
        tax
        pla
        jsr     setn
        lda     #<schk
        ldx     #>schk
        jsr     line
        lda     UT_DPBC + 4             ; (EXM + 1) x 128
        ldx     #0
        jsr     setn
        jsr     inc24
        lda     #7
        jsr     UT_SHLN
        lda     #<srpe
        ldx     #>srpe
        jsr     line
        lda     UT_DPBC + 3             ; BLM + 1
        ldx     #0
        jsr     setn
        jsr     inc24
        lda     #<srpb
        ldx     #>srpb
        jsr     line
        lda     UT_DPBC                 ; SPT
        ldx     UT_DPBC + 1
        jsr     setn
        lda     #<sspt
        ldx     #>sspt
        jsr     line
        lda     #128                    ; 128 << PSH
        ldx     #0
        jsr     setn
        lda     UT_DPBC + 15
        jsr     UT_SHLN
        lda     #<sbpr
        ldx     #>sbpr
        jsr     line
        jmp     UT_EXIT

; UT_N right in 7 with commas, then the '$' string at A/X: a line.
line:   pha
        phx
        lda     #7
        jsr     putn
        plx
        pla
        jmp     UT_SAY

; UT_N + 1.
inc24:  inc     UT_N
        bne     @done
        inc     UT_N + 1
        bne     @done
        inc     UT_N + 2
@done:  rts

; UT_N = (DSM + 1) << BSH: the drive's records.
blocks: lda     UT_DPBC + 5
        ldx     UT_DPBC + 6
        jsr     setn
        jsr     inc24
        lda     UT_DPBC + 2
        jmp     UT_SHLN

        .include "cmd.inc"
        .include "util.inc"

        .rodata
OPTAB:  .byte   "SPACE", 0, "LABEL", 0, "USERS", 0, "DIR", 0, "DRIVE", 0, 0
sbaddrv: .byte  "Unrecognized drive.$"
sbadopt: .byte  "Unrecognized option.$"
sbadin: .byte   "Unrecognized input.$"
srw:    .byte   " R$"
sspace: .byte   ", Space: $"
snolbl: .byte   "No directory label exists.$"
slblfor: .byte  "Label for drive $"
shead1: .byte   "Directory     Passwds  Stamp   Stamp   Label Created   Label Updated$"
shead2c: .byte  "Label         Reqd     Create  Update$"
shead2a: .byte  "Label         Reqd     Access  Update$"
shead3: .byte   "------------  -------  ------  ------  --------------  --------------$"
son:    .byte   "on$"
soff:   .byte   "off$"
sactu:  .byte   "Active User :$"
sactf:  .byte   "Active Files: $"
snfiles: .byte  " # of files:$"
sfree:  .byte   " Number of free directory entries: $"
schar:  .byte   " Drive Characteristics$"
srecs:  .byte   ": 128 Byte Record Capacity$"
skb:    .byte   ": Kilobyte Drive Capacity$"
sents:  .byte   ": 32 Byte Directory Entries$"
schk:   .byte   ": Checked Directory Entries$"
srpe:   .byte   ": Records / Directory Entry$"
srpb:   .byte   ": Records / Block$"
sspt:   .byte   ": Sectors / Track$"
sbpr:   .byte   ": Bytes / Physical Record$"

        .bss
drv:    .res    1
opt:    .res    1
cur:    .res    1
len:    .res    1
vec:    .res    2
login:  .res    2
exmask: .res    1
free:   .res    2
counts: .res    32
