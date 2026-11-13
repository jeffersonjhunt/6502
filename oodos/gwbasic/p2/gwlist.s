; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWLIST.ASM, in block P2 (hw/oodos.md, "The plan, phase 7"):
;
;   LISTP          LIST's lines: a range (SCNLIN), each line's number and
;                  its text as BUFLIN gives it, to the console or (LLIST,
;                  PTRFIL set) LPT1; ^C between lines (POLKEY). LIST's
;                  resident stub then goes to READY, as GW-BASIC's LIST does
;   BUFLIN BUFLN2  a line's tokens back to text in BUF: the reserved words
;                  (with the spaces GW-BASIC puts round them), embedded
;                  constants through P1's $FOUT ($FOUTO, $FOUTH for &O and
;                  &H, "!" or "#" where the type needs it), REM's and DATA's
;                  text and strings as they are, :REM' as '
;   LISPRT         BUF out
;   TSTANM         a letter or digit?
;   DELETP         DELETE's range: "Ok" first, then the lines out; FINI
;
; BUFLIN's text pointer is TXTPTR (the 8086's BX), as CHRGTR and CONFC1 want
; it; KDP is its deposit pointer (CX), KCNT the room left (DH), KCH the
; character in hand (DL). The reserved word table is walked with PTR and
; KRP, so nothing need be pushed round it as the 8086 does.

.include "gwbasic.inc"

;@entry LISTP
;@entry DELETP
.export LISTP, DELETP, BUFLIN, BUFLN2, LISPRT, TSTANM
.import CHRGTR, SYNCHR, POLKEY, OUTDO, CRDO, STROUT, ISLET2, CONFC1, SCNLIN
.import FNDLIN, DEPTR, DEL, CHEAD, FCERR, SNERR, FINI, ADVERR
.import BUF, DOT, RESLST, LISTX, REDDY

.code

; --- LIST (GWLIST.ASM 279) ---------------------------------------------------------------

; LISTP: A and the flags as CHRGTR left them after LIST. ", device" (the
; files) is 7c's.
LISTP:          JSR     SCNLIN         ; RCL: the first line; LSTMAX the last number
                LDA     TEMP           ; the text after the range
                STA     TXTPTR
                LDA     TEMP+1
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     LIST4
                JSR     SYNCHR
                .byte   ','
                JMP     ADVERR         ; NAMSCN, NULOPM: a device, 7c's
LIST4:          LDA     #$FF           ; CONT isn't changed by a ^C here
                STA     CURLIN
                STA     CURLIN+1
                LDA     RCL
                STA     LSTLIN
                LDA     RCH
                STA     LSTLIN+1
LSTLP:          LDA     LSTLIN
                STA     PTR
                LDA     LSTLIN+1
                STA     PTR+1
                LDA     (PTR)          ; the link: zero at the end
                LDY     #1
                ORA     (PTR),Y
                BNE     :+
                RTS
:               JSR     POLKEY         ; ^C?
                LDY     #2             ; its number, against the last
                LDA     LSTMAX
                CMP     (PTR),Y
                INY
                LDA     LSTMAX+1
                SBC     (PTR),Y
                BCS     :+
                RTS                    ; past the range
:               LDY     #2
                LDA     (PTR),Y
                STA     RBL
                STA     DOT
                INY
                LDA     (PTR),Y
                STA     RBH
                STA     DOT+1
                FJSR    LINPRT         ; with no space before it
                CLC                    ; the text
                LDA     LSTLIN
                ADC     #4
                STA     TXTPTR
                LDA     LSTLIN+1
                ADC     #0
                STA     TXTPTR+1
                LDA     (TXTPTR)
                CMP     #9             ; a space after the number, but before a tab
                BEQ     NOSPAL
                LDA     #' '
                JSR     OUTDO
NOSPAL:         JSR     BUFLIN
                JSR     LISPRT
                JSR     CRDO
                LDA     LSTLIN         ; the next line
                STA     PTR
                LDA     LSTLIN+1
                STA     PTR+1
                LDA     (PTR)
                TAX
                LDY     #1
                LDA     (PTR),Y
                STX     LSTLIN
                STA     LSTLIN+1
                BRA     LSTLP

; LISPRT: BUF out, to its zero. (LISEOL, the PC screen's erase to the end
; of the line after a line feed, has no counterpart on the console; OUTCH1's
; CR after a file's line feed is 7c's.)
LISPRT:         LDX     #0
@c:             LDA     BUF,X
                BNE     :+
                RTS
:               JSR     OUTDO
                INX
                BRA     @c

; --- BUFLIN (GWLIST.ASM 372) -------------------------------------------------------------

; The line's text at TXTPTR to BUF, at most BUFLEN characters (a longer
; line is cut, as GW-BASIC cuts it); BUFLN2: to KDP, KCNT at most. DORES:
; bit 0 in quotes, bit 1 in DATA, bit 2 in REM, where it is kept as it is.
; TEMPA: what went last: 0 a special character (no space before a word),
; 1 a name or number, $FF a reserved word (a space before a name or word).
BUFLIN:         LDA     #<BUF
                STA     KDP
                LDA     #>BUF
                STA     KDP+1
                LDA     #BUFLEN
                STA     KCNT
BUFLN2:         STZ     DORES
                STZ     TEMPA
                BRA     PLOOP2
PLOOP:          INCW    KDP
                INCW    TXTPTR
                DEC     KCNT
                BNE     PLOOP2
                RTS                    ; the buffer full
PLOOP2:         LDA     (TXTPTR)
                STA     (KDP)
                BNE     :+
                RTS                    ; the end of the line
:               CMP     #OCTCON
                BCC     NTEMBL
                CMP     #DBLCON+1
                STA     KCH
                BCC     PRTVAR         ; an embedded constant: a space before it?
                CMP     #'"'
                BNE     BFCHKC
                LDA     DORES          ; quotes in and out
                EOR     #1
                STA     DORES
                LDA     #'"'
BFCHKC:         CMP     #':'           ; a colon ends DATA, but in quotes
                BNE     NTEMBL
                LDA     DORES
                LSR     A
                BCS     QTCOLN
                LDA     DORES
                AND     #$FD
                STA     DORES
QTCOLN:         LDA     #':'
NTEMBL:         ORA     #0
                BPL     :+
                JMP     PLOOPR         ; a reserved word
:               STA     KCH
                CMP     #'.'           ; part of a name
                BEQ     PRTVAR
                JSR     TSTANM
                BCC     PRTVAR
                LDA     #0             ; special
                BRA     PLOOPH
PRTVAR:         LDA     TEMPA          ; after a reserved word: a space
                BEQ     PLOOPG
                INC     A
                BNE     PLOOPG
                LDA     #' '
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     PLOOPG
                RTS
PLOOPG:         LDA     #1
PLOOPH:         STA     TEMPA
                LDA     KCH
                CMP     #OCTCON
                BCC     PLOOPZ
                CMP     #DBLCON+1
                BCS     PLOOPZ
                JMP     NUMLIN
PLOOPZ:         STA     (KDP)          ; (again, after a space)
                JMP     PLOOP

; PLOOPR: a reserved word's byte. In quotes, DATA or REM it is a character
; (but REM's ' : the :REM before it taken back); else the word.
PLOOPR:         LDA     DORES
                LSR     A
                BCS     GPLOOP         ; in quotes
                LSR     A
                LSR     A
                BCC     CHKDRS
                LDA     (TXTPTR)       ; REM: its ' after ":REM"?
                CMP     #SNGQTK
                BNE     NOSNGQ
                SEC                    ; the four characters before it
                LDA     KDP
                SBC     #4
                STA     PTR
                LDA     KDP+1
                SBC     #0
                STA     PTR+1
                LDY     #3
@m:             LDA     (PTR),Y
                CMP     MREM,Y
                BNE     NOSNGQ
                DEY
                BPL     @m
                LDA     PTR            ; ":REM" taken back
                STA     KDP
                LDA     PTR+1
                STA     KDP+1
                CLC
                LDA     KCNT
                ADC     #4
                STA     KCNT
                BRA     RESEXP
NOSNGQ:         LDA     (TXTPTR)
GPLOOP:         JMP     PLOOP
CHKDRS:         LDA     DORES          ; in DATA
                AND     #2
                BNE     GPLOOP
                LDA     (TXTPTR)
                CMP     #DATA_TK
                BNE     :+
                LDA     DORES          ; DATSET
                ORA     #2
                STA     DORES
                BRA     RESEXP
:               CMP     #REM_TK
                BNE     RESEXP
                LDA     DORES          ; REMSET
                ORA     #4
                STA     DORES
RESEXP:         LDA     (TXTPTR)       ; $FF: a function, the next byte its token
                CMP     #$FF
                BNE     NTFNTK
                INCW    TXTPTR
                LDA     (TXTPTR)
                AND     #$7F
NTFNTK:         INCW    TXTPTR
NTQTTK:         CMP     #ELSE_TK       ; ELSE: CRUNCH's colon before it taken back
                BNE     NOTELS
                DECW    KDP            ; (DECW takes A)
                INC     KCNT
                LDA     #ELSE_TK
NOTELS:         CMP     #WHILE_TK      ; WHILE: CRUNCH's "+" after it skipped
                BNE     BFNTWH
                LDA     (TXTPTR)
                CMP     #PLUSTK
                BNE     @nplus
                INCW    TXTPTR
@nplus:         LDA     #WHILE_TK
BFNTWH:         STA     KTRM           ; the token sought
                JSR     LISTX          ; an extended word?
                BCS     BUFRES
                LDA     #<(RESLST-1)
                LDX     #>(RESLST-1)
                CLC

; BUFRES: the word with token KTRM found in the table at A/X+1 (C set: the
; extended words', for LISTX), then put in the buffer with its spaces.
BUFRES:         STA     PTR
                STX     PTR+1
                ROR     KCOMMN         ; bit 7: an extended word
                LDA     #'A'-1         ; KLEAD: the table's first letter
                STA     KLEAD
RESSR3:         INC     KLEAD
RESSR1:         INCW    PTR
RESSRC:         LDA     PTR            ; KRP: the word's start
                STA     KRP
                LDA     PTR+1
                STA     KRP+1
RESSR2:         LDA     (PTR)
                BEQ     RESSR3         ; the end of a letter's table
                TAX
                INCW    PTR
                TXA
                BPL     RESSR2         ; to the word's last character
                LDA     (PTR)          ; its token
                CMP     KTRM
                BNE     RESSR1
                BIT     KCOMMN
                BMI     NTSPCH         ; an extended word: no special cases
                CMP     #USR_TK        ; USR and FN: no space before what follows
                BEQ     :+
                CMP     #FN_TK
                BNE     NTFNEX
:               LDA     KLEAD
                STA     KCH
                LDA     TEMPA          ; a space before it as before a word
                STZ     TEMPA
                ORA     #0
                BRA     MORLNZ
NTFNEX:         LDA     KLEAD          ; a special character: no spaces round it
                CMP     #'Z'+1
                BNE     NTSPCH
                STZ     TEMPA
                BRA     MORPUR
NTSPCH:         LDA     KLEAD
                STA     KCH
                LDA     TEMPA          ; a space after a name, number or word
                LDX     #$FF
                STX     TEMPA
                ORA     #0
MORLNZ:         BEQ     MORLN0
                LDA     #' '
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     MORLN0
                RTS                    ; (PPSWRT: the buffer full)
MORLN0:         LDA     KCH            ; the first letter
                BRA     MORLN1
MORPUR:         LDA     (KRP)          ; the rest of the word
                INCW    KRP
                STA     KCH
MORLN1:         AND     #$7F
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     :+
                RTS                    ; (PPSWRT)
:               ORA     KCH
                BPL     MORPUR         ; to the character with bit 7
                CMP     #'(' | $80     ; SPC( and TAB(: a space may follow
                BNE     NTSPCT
                STZ     TEMPA
NTSPCT:         BIT     KCOMMN         ; an extended word: past its second byte
                BPL     @nx
                INCW    TXTPTR
@nx:            JMP     PLOOP2

; TSTANM (GWLIST.ASM 660): C clear if A is a letter or a digit.
TSTANM:         JSR     ISLET2
                BCC     @r
                CMP     #'0'
                BCC     @no
                CMP     #'9'+1         ; C clear for a digit
@r:             RTS
@no:            SEC
                RTS

; NUMLIN (GWLIST.ASM 666): an embedded constant, by CONFC1 and $FOUT (&O and
; &H in their bases), its leading space dropped; "!" after a single, "#"
; after a double, unless its text says the type (a single's ".", or E or D).
NUMLIN:         DECW    TXTPTR
                JSR     CHRGTR         ; the constant scanned: CONSAV, CONTYP, CONTXT
                PHA
                JSR     CONFC1
                PLA
                CMP     #OCTCON
                BNE     :+
                FJSR    FOUTO
                BRA     CONLIN
:               CMP     #HEXCON
                BNE     :+
                FJSR    FOUTH
                BRA     CONLIN
:               FJSR    FOUT
CONLIN:         LDA     CONSAV
                LDX     #'O'
                CMP     #OCTCON
                BEQ     SAVBAS
                LDX     #'H'
                CMP     #HEXCON
                BNE     NUMSLN
SAVBAS:         LDA     #'&'
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     :+
                RTS
:               TXA
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     NUMSLN
                RTS
NUMSLN:         LDX     #0             ; KCH: the type's character, or none
                LDA     CONTYP
                CMP     #VT_SNG
                BCC     TYPSET
                BNE     :+
                LDX     #'!'
                BRA     TYPSET
:               LDX     #'#'
TYPSET:         STX     KCH
                LDY     #0
                LDA     (FBPTR)        ; a leading space dropped
                CMP     #' '
                BNE     NUMSL2
                INY
NUMSL2:         LDA     (FBPTR),Y
                INY
                ORA     #0
                BEQ     NUMDN
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     :+
                RTS
:               LDX     CONTYP
                CPX     #VT_SNG
                BCC     NUMSL2         ; an integer: as it is
                BNE     DBLSCN         ; a double: "." doesn't say it
                CMP     #'.'
                BEQ     ZERE
DBLSCN:         CMP     #'D'
                BEQ     ZERE
                CMP     #'E'
                BNE     NUMSL2
ZERE:           STZ     KCH
                BRA     NUMSL2
NUMDN:          LDA     KCH
                BEQ     NOD
                STA     (KDP)
                INCW    KDP
                DEC     KCNT
                BNE     NOD
                RTS
NOD:            LDA     CONTXT         ; on past the constant
                STA     TXTPTR
                LDA     CONTXT+1
                STA     TXTPTR+1
                JMP     PLOOP2

; --- DELETE (GWLIST.ASM 771) -------------------------------------------------------------

; DELETE n, n-m, -m: the lines out (DEPTR first). The last must be a line
; of the program, and not before the first (else Illegal function call),
; and nothing may follow. "Ok" is printed first; then CHEAD from the first
; and FINI: DELETE doesn't come back.
DELETP:         JSR     SCNLIN         ; RCL: the first line
                LDA     RCH
                PHA
                LDA     RCL
                PHA
                JSR     DEPTR
                LDA     LSTMAX         ; the last must be there
                STA     RDL
                LDA     LSTMAX+1
                STA     RDH
                JSR     FNDLIN
                BCS     :+
                JMP     FCERR
:               PLA                    ; CX: the first; PTR2 the line after the last
                STA     RCL
                PLA
                STA     RCH
                LDA     RCL            ; the first before it
                CMP     PTR2
                LDA     RCH
                SBC     PTR2+1
                BCC     :+
                JMP     FCERR
:               LDA     TEMP           ; EOSCHK: nothing after the range
                STA     TXTPTR
                LDA     TEMP+1
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               LDA     #<REDDY
                STA     PTR
                LDA     #>REDDY
                STA     PTR+1
                JSR     STROUT
                LDA     RCH            ; FINI's place
                PHA
                LDA     RCL
                PHA
                LDA     PTR2
                STA     RBL
                LDA     PTR2+1
                STA     RBH
                JSR     DEL
                PLA
                STA     RDL
                PLA
                STA     RDH
                JSR     CHEAD
                JMP     FINI

.rodata
MREM:           .byte   ":REM"

.segment "BRAM"
KCOMMN:         .res    1              ; BUFRES: bit 7, an extended word
