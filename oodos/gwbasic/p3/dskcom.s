; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; DSKCOM.ASM's LSET and RSET, translated, in block P3 (hw/oodos.md, "The plan,
; phase 7": 7c; moved from P4, the file layer, by the IBM spec's fix-up, the
; owner's answer 2, for P4's room: they use nothing of P4's, only the
; resident core's string routines, so each is one far call from its home
; block stub, as before):
;
;   LSETP RSETP     LSET, RSET v$ = s$: s$ into v$'s room, left or right
;                   (BLKFIL, MAKDSC)

.include "gwbasic.inc"

;@entry LSETP
;@entry RSETP
.export LSETP, RSETP
.import PTRGET, CHKSTR, FRMEQL, FRESTR, GETSPA, STRINI, PUTNEW

.code

; --- LSET, RSET (DSKCOM.ASM 621) ----------------------------------------------------------

; LSETP, RSETP: LSET v$ = s$, RSET v$ = s$. s$'s bytes go into v$'s, as many
; as v$ has room for, the rest of it spaces: after them (LSET) or before
; (RSET). A null v$ is left as it is. v$'s bytes are changed where they
; are: a FIELD buffer (below TXTTAB in the 8080's map; here above MEMSIZ, as
; the 8086's) or the string space; but a v$ whose bytes are a literal in the
; program gets a copy of its length first (GETSPA), and if that could
; collect the strings, a temporary for s$ is made first (MAKDSC), if s$ was
; one: the flags FRESTR leaves (the 8086's PUSHF, "zero if was temp"; they
; are zero only for a null temporary at FRETOP, so MAKDSC almost never makes
; it). LRFLG bit 7 is the 8086's carry, LSET; LRVAR v$'s descriptor, LREXP
; s$'s, LRP FRESTR's flags; LRCL v$'s length (CL), LRCH s$'s (CH), then
; the bytes to copy and CL the spaces; DX (RDL) the place in v$.
RSETP:          CLC
                BRA     LSETA
LSETP:          SEC
LSETA:          ROR     LRFLG          ; PUSHF
                JSR     PTRGET
                JSR     CHKSTR         ; a string variable
                LDA     PTR            ; PUSH DX
                STA     LRVAR
                LDA     PTR+1
                STA     LRVAR+1
                JSR     FRMEQL         ; "=" and s$
                JSR     FRESTR         ; a string, its temporary freed: BX
                PHP
                PLA
                STA     LRP
                LDA     RBL
                STA     LREXP
                LDA     RBH
                STA     LREXP+1
                LDA     (RBL)          ; CH: s$'s length
                STA     LRCH
                LDA     LRVAR          ; CL: v$'s
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDA     (RBL)
                STA     LRCL
                BNE     :+
                RTS                    ; RETCUR: null, not changed
:               LDY     #1             ; DX: its bytes
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     TXTTAB         ; at or below TXTTAB: a buffer
                CMP     RDL
                LDA     TXTTAB+1
                SBC     RDH
                BCS     OLDSTR
                LDA     VARTAB         ; above VARTAB: the strings'
                CMP     RDL
                LDA     VARTAB+1
                SBC     RDH
                BCC     OLDSTR
                CLC                    ; a literal: a copy. Would GETSPA collect?
                LDA     STREND
                ADC     LRCL
                STA     RBL
                LDA     STREND+1
                ADC     #0
                STA     RBH
                LDA     FRETOP
                CMP     RBL
                LDA     FRETOP+1
                SBC     RBH
                JCC     MAKDSC
MADESC:         LDA     LRCL           ; room for v$'s length
                JSR     GETSPA
                LDA     RDL            ; PUSH DX round it
                PHA
                LDA     RDH
                PHA
                JSR     FRESTR         ; s$'s temporary freed, if any
                PLA
                STA     RDH
                PLA
                STA     RDL
                LDA     LRVAR          ; v$ at the copy
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDY     #1
                LDA     RDL
                STA     (RBL),Y
                INY
                LDA     RDH
                STA     (RBL),Y
OLDSTR:         LDA     LRVAR          ; DX: v$'s bytes
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDY     #1
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     LREXP          ; PTR2: s$'s
                STA     RBL
                LDA     LREXP+1
                STA     RBH
                LDY     #1
                LDA     (RBL),Y
                STA     PTR2
                INY
                LDA     (RBL),Y
                STA     PTR2+1
                LDA     LRCL           ; room for all of s$?
                CMP     LRCH
                BCS     FILDOK
                STA     LRCH           ; no: as many as there is
FILDOK:         SEC                    ; CL: the spaces
                SBC     LRCH
                STA     LRCL
                BIT     LRFLG
                BMI     :+
                JSR     BLKFIL         ; RSET: before
:               LDY     #0             ; COPLOP: CH bytes
@c:             CPY     LRCH
                BEQ     LRSTDN
                LDA     (PTR2),Y
                STA     (RDL),Y
                INY
                BRA     @c
LRSTDN:         TYA                    ; DX past them
                CLC
                ADC     RDL
                STA     RDL
                BCC     :+
                INC     RDH
:               BIT     LRFLG
                BPL     :+
                JSR     BLKFIL         ; LSET: after
:               RTS

; BLKFIL: CL spaces at DX, DX past them.
BLKFIL:         LDY     #0
@b:             CPY     LRCL
                BEQ     @x
                LDA     #' '
                STA     (RDL),Y
                INY
                BRA     @b
@x:             TYA
                CLC
                ADC     RDL
                STA     RDL
                BCC     :+
                INC     RDH
:               RTS

; MAKDSC: GETSPA may collect: if s$ was a temporary (LRP's Z), a temporary
; is made for it (STRINI of its length, PUTNEW) before MADESC goes on.
MAKDSC:         LDA     LRP
                AND     #$02           ; the Z flag
                BEQ     MAKDS1         ; not a temporary
                LDA     LRCH
                JSR     STRINI
                JSR     PUTNEW
MAKDS1:         JMP     MADESC

.segment "BRAM"
LRFLG:          .res    1              ; LSET (bit 7) or RSET
LRVAR:          .res    2              ; LSET's and RSET's v$ (its descriptor)
LREXP:          .res    2              ; and s$
LRP:            .res    1              ; and FRESTR's flags
LRCL:           .res    1              ; v$'s length (CL), then the spaces
LRCH:           .res    1              ; s$'s (CH), then the bytes to copy
