; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's PLAY (541-822), in block P7 (hw/oodos.md, "The plan, phase 7":
; 7f), on P7's own copy of MACLNG (p7/maclng.s) and the OEM's DONOTE
; (p7/sound.s):
;
;   PLAYP     PLAY string (the home block's stub, screen mode: the owner's
;             answer 4): MACLNG with PLYTAB
;   PLAYN     PLAY(n) (XFUNP's): the notes waiting in MB (MU_COUNT of voice
;             0); 0 in MF (the manual's p. 255) or with the driver not
;             started in this run; n parsed and not used; console mode
;             Advanced Feature (the owner's answer 4)
;
; Each command as the source's, its number in DX (1 if none) and C set if
; one was given (MACLNG's). What the OO-1 does differently:
;   - a note is one entry on the driver, its gate time MN's 7/8, MS's 3/4 or
;     the whole (ML), where the source sends the shortened note and then a
;     pause (PLYNO6-PLYNO9): so MB's 32 entries are the manual's 32 notes;
;   - the octave: the manual's (the owner's answer 2): O3 starts with middle
;     C, so NOTTAB's top octave is shifted right by 7 - O (the source's
;     6 - O puts each note an octave higher), rounded by the last bit out
;     (PLYNU4's ADC);
;   - `>` and `<` (the manual's pp. 250-251, the source has neither): the
;     octave one up or down, held at 6 and 0, then the note that follows is
;     played as any is;
;   - P0 Illegal function call (the manual: P is 1-64; the source takes it as
;     no pause); and each number checked whole against its range (the
;     manual's: N 0-84, O 0-6, L and P 1-64, T 32-255), where the source
;     tests its low byte alone.
;
; The far call rule: each returns by RTS, or ends in an error.

.include "gwbasic.inc"

.export PLAYP, PLAYN
.import MACLNG, MCLXEQ, FETCHR, FETCHZ, DECFET, VALSC2, MCLC2
.import FCERR, ADVERR, SCRMOD, CHRGTR, PARCHK, MAKINT, MU_COUNT ; (oolib.inc not included: its .globalzp would export gwbasic.inc's GXP again)
.import BEATS, MSCALE, MMODE, NOTELN, NOTE1L, OCTAVE, NOTEHZ, NOTEU, DOSND, DRVON

.code

; PLAYS (GWSTS.ASM 556): MACLNG with PLYTAB.
PLAYP:          LDA     #1             ; sound used this run (the owner's answer 20)
                STA     SNDUSE
                LDA     #<PLYTAB
                LDX     #>PLYTAB
                JMP     MACLNG

; PLAY(n) (OO-1): TXTPTR at PLAY's token; returns as EVAL, past ")".
PLAYN:          BIT     SCRMOD         ; console mode: Advanced Feature (answer 4)
                BMI     :+
                JMP     ADVERR
:               JSR     CHRGTR         ; to "("
                JSR     PARCHK         ; n (not used)
                LDX     #0
                LDA     MMODE          ; MF: 0
                AND     DRVON          ; nothing played this run: 0
                BEQ     :+
                LDA     #0
                JSR     MU_COUNT
                TAX
:               STX     RBL
                STZ     RBH
                JSR     MAKINT
                DECW    TXTPTR         ; CHRGTR's flags for what follows ")"
                JMP     CHRGTR

; --- The commands ---------------------------------------------------------------------------

; DLARG: DX (MACLNG's number) at most A - 1, else Illegal function call;
; DL in A, Z by it.
DLARG:          LDX     RDH
                BNE     PLGOFC
                CMP     RDL
                BEQ     PLGOFC
                BCC     PLGOFC
                LDA     RDL
                RTS

; PLYLEN (GWSTS.ASM 636): L n, 1-64.
PLYLEN:         BCC     PLGOFC         ; no number
                LDA     #65
                JSR     DLARG
                BEQ     PLGOFC         ; 0
                STA     NOTELN
                RTS

; PTEMPO (GWSTS.ASM 645): T n, 32-255.
PTEMPO:         LDA     RDH
                BNE     PLGOFC
                LDA     RDL
                CMP     #32
                BCC     PLGOFC
                STA     BEATS
                RTS

; PPAUSE (GWSTS.ASM 651): P n, 1-64 (OO-1: P0 too is Illegal function
; call, the manual's); a rest of that length, PPAUS2.
PPAUSE:         BCC     PLGOFC
                LDA     #65
                JSR     DLARG
                BEQ     PLGOFC
                STZ     NOTEHZ         ; CX 0: a rest
                STZ     NOTEHZ+1
                JMP     PPAUS2

; POCTAV (GWSTS.ASM 661): O n, 0-6.
POCTAV:         BCC     PLGOFC
                LDA     #7
                JSR     DLARG
                STA     OCTAVE
PLYRET:         RTS

; POCTUP, POCTDN (OO-1, the manual's p. 250): > and <: the octave one up or
; down, held at 6 and 0.
POCTUP:         LDA     OCTAVE
                CMP     #6
                BCS     PLYRET
                INC     OCTAVE
                RTS
POCTDN:         LDA     OCTAVE
                BEQ     PLYRET
                DEC     OCTAVE
                RTS

PLGOFC:         JMP     FCERR          ; GWSTS.ASM 685

; PLYNUM (GWSTS.ASM 667): N n, 0-84: 0 a rest; else n - 1 into its octave
; (/ 12) and its note (the remainder + 1, doubled).
PLYNUM:         BCC     PLGOFC
                LDA     #85
                JSR     DLARG
                BEQ     PLYNO3         ; 0: the rest (AL 0)
                DEC     A
                LDX     #0
:               CMP     #12            ; DH: the octave
                BCC     :+
                SBC     #12
                INX
                BRA     :-
:               INC     A              ; AL: (the note + 1) x 2
                ASL     A
                STX     RDH
                BRA     PLYNU3

; PLYNOT (GWSTS.ASM 687): A-G, then #, + (sharp) or - (flat), into NOTXLT's
; index of NOTTAB; 255 there (B#, C-, E#, F-) Illegal function call.
PLYNOT:         LDA     MCLC2          ; the note's letter x 2
                LSR     A
                SEC
                SBC     #'A' - 1       ; 1-7
                ASL     A              ; 2-14, as a sharp
                STA     PCL
                JSR     FETCHR
                BCC     PLYNO2         ; the string's end: no sharp nor flat
                CMP     #'#'
                BEQ     PLYSHP
                CMP     #'+'
                BEQ     PLYSHP
                CMP     #'-'
                BEQ     PLYFLT
                JSR     DECFET         ; not one: back
                BRA     PLYNO2
PLYFLT:         DEC     PCL            ; flat: twice down
PLYNO2:         DEC     PCL            ; natural: once
PLYSHP:         LDX     PCL
                LDA     NOTXLT,X
                BMI     PLGOFC         ; not a note
; PLYNO3: the note in A (0 a rest, 2-24 NOTTAB's index), the octave OCTAVE.
PLYNO3:         LDX     OCTAVE
                STX     RDH
; PLYNU3 (GWSTS.ASM 716): the note in A, its octave in DH; this note's length
; NOTELN, or a number after it (1-64; 0 or none: NOTELN's).
PLYNU3:         STA     PNOTE
                LDA     RDH
                STA     POCT
                LDA     NOTELN
                STA     NOTE1L
                JSR     FETCHR
                BCC     PLYNU4         ; the string's end
                JSR     VALSC2         ; a length?
                LDA     #65
                JSR     DLARG
                BEQ     PLYNU4         ; none
                STA     NOTE1L
; PLYNU4 (GWSTS.ASM 728): the frequency: NOTTAB's (the top octave's) shifted
; right by 7 - the octave (OO-1, the owner's answer 2: the manual's octave),
; rounded up by the last bit out; a rest 0.
PLYNU4:         LDX     PNOTE
                STZ     NOTEHZ
                STZ     NOTEHZ+1
                BEQ     PLYNO4         ; a rest
                LDA     NOTTAB - 2,X
                STA     NOTEHZ
                LDA     NOTTAB - 1,X
                STA     NOTEHZ+1
                SEC                    ; 7 - the octave
                LDA     #7
                SBC     POCT
                TAX
                CLC
:               LSR     NOTEHZ+1
                ROR     NOTEHZ
                DEX
                BNE     :-
                BCC     PLYNO4         ; ADC BX,0: the carry rounds up
                INC     NOTEHZ
                BNE     PLYNO4
                INC     NOTEHZ+1
PLYNO4:         LDA     NOTE1L         ; DL: this note's length
; PPAUS2 (GWSTS.ASM 742): the duration 96,000 / (BEATS x the length), the
; units DONOTE takes (2.5 ms: 400 a second); 0 nothing. OO-1: the last
; tempo and length's quotient kept (PDBEAT, PDLEN, PDU), so a run of notes
; of one length divides once (the owner's gate: a note at most 8,000 cycles).
PPAUS2:         CMP     PDLEN
                BNE     :+
                LDX     BEATS
                CPX     PDBEAT
                BNE     :+
                LDA     PDU
                STA     NOTEU
                LDA     PDU+1
                STA     NOTEU+1
                BRA     PDIV1
:               STA     PDLEN
                LDX     BEATS
                STX     PDBEAT
                STA     T2             ; T0-T1: BEATS x DL
                STZ     T0
                STZ     T1
                LDX     #8
@m:             ASL     T0
                ROL     T1
                ASL     T2
                BCC     :+
                CLC
                LDA     T0
                ADC     BEATS
                STA     T0
                BCC     :+
                INC     T1
:               DEX
                BNE     @m
                LDA     #<96000        ; T2-T4: 96,000, divided by T0-T1
                STA     T2
                LDA     #>96000
                STA     T3
                LDA     #^96000
                STA     T4
                STZ     T5             ; T5-T6: the remainder
                STZ     T6
                LDX     #24
@d:             ASL     T2
                ROL     T3
                ROL     T4
                ROL     T5
                ROL     T6
                SEC
                LDA     T5
                SBC     T0
                TAY
                LDA     T6
                SBC     T1
                BCC     :+
                STA     T6
                STY     T5
                INC     T2
:               DEX
                BNE     @d
                LDA     T2             ; AX: the quotient (below 65,536: BEATS x DL is 32 at least)
                STA     NOTEU
                STA     PDU
                LDA     T3
                STA     NOTEU+1
                STA     PDU+1
PDIV1:          LDA     NOTEU
                ORA     NOTEU+1
                BNE     PLYDOT
                RTS                    ; 0: nothing
; PLYDOT (GWSTS.ASM 754): each "." after it: the duration x 3 / 2, its
; product's high word (DX) halved nonzero Illegal function call; as the
; source, the low word (AX) halved alone.
PLYDOT:         JSR     FETCHR
                BCC     PLYDOX         ; the string's end
                CMP     #'.'
                BNE     PLYDO2
                LDA     NOTEU          ; T0-T2: x 3
                STA     T0
                LDA     NOTEU+1
                STA     T1
                STZ     T2
                ASL     T0
                ROL     T1
                ROL     T2
                CLC
                LDA     T0
                ADC     NOTEU
                STA     T0
                LDA     T1
                ADC     NOTEU+1
                STA     T1
                BCC     :+
                INC     T2
:               LDA     T2             ; DX / 2 nonzero: too big
                LSR     A
                BEQ     :+
                JMP     FCERR
:               LSR     T1             ; AX / 2
                ROR     T0
                LDA     T0
                STA     NOTEU
                LDA     T1
                STA     NOTEU+1
                BRA     PLYDOT
PLYDO2:         JSR     DECFET
; PLYDOX: a rest whole (legato); a note by MSCALE (DOSND's A: 1 legato, 2
; staccato, 3 normal).
PLYDOX:         LDA     NOTEHZ
                ORA     NOTEHZ+1
                BEQ     :+
                LDA     MSCALE
                JMP     DOSND
:               LDA     #1
                JMP     DOSND

; PLYMET (GWSTS.ASM 803): M and L, S, N (MSCALE 1, 2, 3), F or B (MMODE 0,
; 1); anything else, or the end, Illegal function call.
PLYMET:         JSR     FETCHZ
                LDX     #1
                CMP     #'L'
                BEQ     PLYDUR
                INX
                CMP     #'S'
                BEQ     PLYDUR
                INX
                CMP     #'N'
                BEQ     PLYDUR
                LDX     #0
                CMP     #'F'
                BEQ     PLYMOD
                INX
                CMP     #'B'
                BEQ     PLYMOD
                JMP     FCERR
PLYMOD:         STX     MMODE
                RTS
PLYDUR:         STX     MSCALE
                RTS

.rodata
; PLYTAB (GWSTS.ASM 559): each command's letter (+128: it takes a number)
; and its routine; 0 the end.
PLYTAB:         .byte   'A'
                .word   PLYNOT
                .byte   'B'
                .word   PLYNOT
                .byte   'C'
                .word   PLYNOT
                .byte   'D'
                .word   PLYNOT
                .byte   'E'
                .word   PLYNOT
                .byte   'F'
                .word   PLYNOT
                .byte   'G'
                .word   PLYNOT
                .byte   'M'
                .word   PLYMET
                .byte   'N' + 128
                .word   PLYNUM
                .byte   'O' + 128
                .word   POCTAV
                .byte   'P' + 128
                .word   PPAUSE
                .byte   'T' + 128
                .word   PTEMPO
                .byte   'L' + 128
                .word   PLYLEN
                .byte   'X'
                .word   MCLXEQ
                .byte   '>'            ; OO-1, the manual's
                .word   POCTUP
                .byte   '<'
                .word   POCTDN
                .byte   0
; NOTXLT (GWSTS.ASM 594): A- A A# B (B#) C C# D D# E (E#) F F# G G#, each
; NOTTAB's index (2 a note), 255 none.
NOTXLT:         .byte   9 * 2, 10 * 2, 11 * 2, 12 * 2, 255, 1 * 2, 2 * 2, 3 * 2
                .byte   4 * 2, 5 * 2, 255, 6 * 2, 7 * 2, 8 * 2, 9 * 2
; NOTTAB (GWSTS.ASM 613): the top octave's frequencies in Hz, C to B.
NOTTAB:         .word   4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902

.segment "BRAM"
PCL:            .res    1              ; PLYNOT's CL
PNOTE:          .res    1              ; PLYNU3's note (AL)
POCT:           .res    1              ; and octave (DH)
PDBEAT:         .res    1              ; PPAUS2's last tempo,
PDLEN:          .res    1              ;   length (0: none yet),
PDU:            .res    2              ;   and their quotient
