; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7 (hw/oodos.md, "The plan, phase 7": 7f, sound, input and events;
; the space plan's answer 8: the eighth block, allocated at 7f). It links
; OOLIB's music (../lib/mu.s, mutone.s, physof.s: the Makefile's LIB_p7),
; its zero page word MUP gwbasic.inc's alias of GXQ, exported here. This
; file holds GWSTS.ASM's SOUND, BEEP and the sound reset, and the OEM's
; DONOTE on the music driver (voice 0, one voice as the PC's):
;
;   SNDINI    CLEARC's (P3's INITRP far calls it: RUN, NEW, CLEAR) and
;             SYSTEM's: PLAY's state to its defaults, then the driver quit
;             if it was started (the owner's answer 5: each program starts
;             with the 6510 free)
;   SNDRST    the break's (the resident ITSCTC) and SOUND x,0's: voice 0
;             stopped (MU_STOP), if the driver was started
;   SOUNDP    SOUND x,y (the home block's stub, screen mode: the owner's
;             answer 4), TXTPTR at x
;   BEEPP     BEEP, and ^G's bell (P6's CBEEP): function 82's SN_BEEP, 800
;             Hz for 250 ms, the USB card's speaker, no claim of the 6510, in
;             both modes (the owner's answer 3: the OEM's); BELLP the bell
;             alone, for P6's CBEEP (^G in screen mode), which prints
;   DONOTE    the OEM's (GWSTS.ASM's EXTRN): a note queued on voice 0, the
;             driver started first (MU_INIT, at a run's first note), MF's or
;             MB's wait before it (WAITQ), then MU_TONE, the length in the
;             source's 2.5 ms units turned to ms (x 5 / 2, rounded), the
;             gate time by PLAY's style
;   MSNOW     the millisecond counter, 32 bits: EVAREA's EV_MS read until two
;             reads agree (screen mode), else function 80's FT_COUNT
;
; And the resident core's and the home block's hand-offs (7f's U1):
;
;   EVP7      EVPOLL's: an event that isn't a key's, Y its slot's offset in
;             the queue (EVAREA + EV_QUEUE + Y): EK_SOUND's byte to MU_EVENT
;             (the voices' counts: PLAY(n)); EK_CTL STREV's (U6); the rest
;             dropped
;   TRPPOL    retired (an RTS, p7/event.s)
;   JOYFP     PEN(n), STICK(n), STRIG(n) from FUNDSP (the resident JOYF), X the
;             function as FINGO has it: Illegal function call for now
;   XFUNP     the home block's ERDEVP: PLAY(n) (p7/play.s PLAYN); DATE$,
;             TIME$ (Advanced Feature for now), TIMER (Syntax error for
;             now), A the token
;
; The far call rule: each returns by RTS, or ends in an error.

.include "gwbasic.inc"

;@entry SNDINI
;@entry SNDRST
;@entry EVP7
;@entry TRPPOL
;@entry JOYFP
;@entry XFUNP
;@entry SOUNDP
;@entry BEEPP
;@entry BELLP
;@entry PLAYP
;@entry ONSETP
;@entry DATESP
;@entry TIMESP
;@entry TIMONP
;@entry TIMOFP
;@entry SNDRND
.export SNDINI, SNDRST, SOUNDP, BEEPP, BELLP, DONOTE, MSNOW, MSN, SNDRND
.export BEATS, MSCALE, MMODE, NOTELN, NOTE1L, OCTAVE, NOTEHZ, NOTEU, DOSND, DRVON
.import FCERR, SNERR, ADVERR, GETIN2, SYNCHR, FRMEVL, CHRGTR, FRCSNG, PUSHF, POPR, CONIA, FMULS, CHKINT
.import SCRMOD, EVAREA, TIMON, TIMOFP, ERROR, RNDX, RNDMN2
.include "oolib.inc"

MUQMAX          = 32                   ; the driver's queue a voice (lib/mu.inc's MU_QLEN)

.exportzp MUP                          ; mu.s's and mutone.s's (lib/zpmu.s's), at gwbasic.inc's address

.code

; --- SNDINI, SNDRST (GWSTS.ASM 822, 840) -----------------------------------------------------

; SNDINI: BEATS 120, MSCALE 3 (MN), MMODE 0 (MF), NOTELN and NOTE1L 4,
; OCTAVE 4; then the sound off (SNDRST's) and, OO-1, the driver quit.
SNDINI:         LDA     #120
                STA     BEATS
                LDA     #3
                STA     MSCALE
                STZ     MMODE
                LDA     #4
                STA     NOTELN
                STA     NOTE1L
                STA     OCTAVE
                STZ     DRVON
                STZ     SNDUSE         ; (the owner's answer 20)
                LDA     TIMON          ; (7f: SYSTEM's too: ON TIMER's hook off)
                BEQ     :+
                JSR     TIMOFP
:               JMP     MU_QUIT        ; (MU_STOP's work with it; PE_STATE if not
                                       ;   started, nothing done)
; SNDRST: DONOTE's AL 255, the background music off and the queue emptied:
; MU_STOP of voice 0, if the driver was started (SOUND x,0 with nothing
; sounding starts nothing).
SNDRST:         LDA     #0             ; (PE_STATE if not started: nothing done)
                JMP     MU_STOP

; --- BEEP, SOUND (GWSTS.ASM 856, 877) --------------------------------------------------------

; BEEPS: 800 Hz for 1/4 s (OO-1: function 82's SN_BEEP, the OEM's, the
; owner's answer 3), then POLKEY's look at the keys (CHKINT).
BEEPP:          JSR     BELLP
                JMP     CHKINT
BELLP:          LDA     #1             ; sound used this run (answer 20)
                STA     SNDUSE
                LDA     #<BEEPB
                LDX     #>BEEPB
                LDY     #O_SOUND
                JMP     BDOS

; SOUNDS: SOUND x,y. x an integer, at least 37 (Illegal function call
; below), y the duration in clock ticks (18.2 a second), a single: 0 the
; sound stopped (SNDRST); 65,536 or more Illegal function call; OO-1 (the
; manual's p. 323: 0 to 65,535), negative Illegal function call too, where
; the source would queue its integer as a length. Then y x 22, the 2.5 ms
; units DONOTE takes, 65,535 at most (FRQINT: P1's), and DOSND.
SOUNDP:         PHA                    ; sound used this run (the owner's answer 20)
                LDA     #1
                STA     SNDUSE
                PLA
                JSR     GETIN2         ; DX: the frequency
                LDA     RDH
                BNE     :+
                LDA     RDL
                CMP     #37
                BCS     :+
SNDFCE:         JMP     FCERR          ; at least 37 Hz
:               LDA     RDH
                PHA
                LDA     RDL
                PHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL         ; the duration
                JSR     EOSP7          ; the statement's end
                JSR     FRCSNG
                LDA     FACEXP
                BNE     SOUNL4
                PLA                    ; 0: the sound off
                PLA
                JMP     SNDRST
SOUNL4:         CMP     #$91           ; 65,536 or more
                BCS     SNDFCE
                BIT     FACM1          ; OO-1: negative
                BMI     SNDFCE
                JSR     PUSHF          ; y
                LDA     #22            ; x 22
                JSR     CONIA
                JSR     FRCSNG
                JSR     POPR           ; BXDX: y
                JSR     FMULS
                LDA     FACEXP
                CMP     #$91           ; past 65,535: 65,535
                BCC     SOUNL5
                LDA     #$90
                STA     FACEXP
                LDA     #$7F
                STA     FACM1
                LDA     #$FF
                STA     FACLO+1
                STZ     FACLO
SOUNL5:         FJSR    FRQINT         ; BX: the units
                PLA                    ; CX: the frequency
                STA     NOTEHZ
                PLA
                STA     NOTEHZ+1
                LDA     RBL
                STA     NOTEU
                LDA     RBH
                STA     NOTEU+1
                LDA     #1             ; SOUND's note whole (legato)
; DOSND: the note with MMODE's wait (DONOTE), A the gate's scale (MSCALE's
; meaning: 1 all, 2 3/4, 3 7/8). DONOTE's error (no sound card) is Illegal
; function call, as the source's; then POLKEY's look at the keys.
DOSND:          JSR     DONOTE
                BCS     SNDFCE
                JMP     CHKINT

; EOSP7: the statement must end at TXTPTR, else Syntax error (GWSTS.ASM's
; EOSCHK, P3's: here too, not worth a far call).
EOSP7:          DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               RTS

; --- DONOTE: the OEM's note on the music driver --------------------------------------------

; NOTEHZ the frequency (0 a rest), NOTEU the length in 2.5 ms units (0
; nothing), A the gate's scale (1 legato, 2 staccato 3/4, 3 normal 7/8),
; MMODE MF (0) or MB (1). The driver started if it isn't (MU_INIT, PAL's
; clock: C set back if refused: no card, or the 6510 another's). A length
; past 65,535 ms (26,214 units) goes as more than one entry, the first
; waited for as MMODE says, the rest straight after it (OO-1: the driver's
; length is 16 bits of ms; SOUND's 65,535 ticks are 163,837). C clear back.
DONOTE:         STA     NOTESC
                LDA     NOTEU
                ORA     NOTEU+1
                BNE     :+
                CLC
                RTS
:               LDA     DRVON          ; started already this run
                BNE     @on
                LDA     #0             ; PAL
                JSR     MU_INIT
                BCC     @on
                CMP     #PE_STATE      ; started already
                BEQ     @on
                SEC
                RTS
@on:            LDA     #1             ; (PLAY(n)'s: played this run)
                STA     DRVON
                STA     SNDUSE
                LDA     MMODE          ; MF: voice 0 empty first; MB: room
                BEQ     :+
                LDA     #MUQMAX - 1
:               JSR     WAITQ
@chunk:         LDA     NOTEU+1        ; this entry: at most 26,214 units
                CMP     #>26214
                BCC     @all
                BNE     @big
                LDA     NOTEU
                CMP     #<26214 + 1
                BCC     @all
@big:           LDA     #<26214
                LDX     #>26214
                BRA     @n
@all:           LDA     NOTEU
                LDX     NOTEU+1
@n:             STA     T0             ; T0-T1: the entry's units
                STX     T1
                SEC                    ; what's left
                LDA     NOTEU
                SBC     T0
                STA     NOTEU
                LDA     NOTEU+1
                SBC     T1
                STA     NOTEU+1
                LDA     T0             ; the last entry's units and style again:
                CMP     LASTU          ; its ms as they were (TONEB's; the owner's
                BNE     @calc          ; gate, a note at most 8,000 cycles)
                LDA     T1
                CMP     LASTU+1
                BNE     @calc
                LDA     NOTESC
                CMP     LASTSC
                BNE     @calc
                LDA     TONEB+3
                STA     T0
                LDA     TONEB+4
                STA     T1
                JMP     @hz
@calc:          LDA     T0
                STA     LASTU
                LDA     T1
                STA     LASTU+1
                LDA     NOTESC
                STA     LASTSC
                LDX     #3             ; the gate: all, 3/4 (x 3, / 4), 7/8 (x 7, / 8)
                CPX     NOTESC
                BEQ     @mn
                LDA     NOTESC
                CMP     #2
                BEQ     @ms
                LDA     T0             ; legato: the whole note
                LDX     T1
                BRA     @g
@ms:            LDA     #3
                LDX     #2
                BRA     @mul
@mn:            LDA     #7
@mul:           STA     T6             ; T2-T4 = units x A, then >> X
                STX     T7
                STZ     T2
                STZ     T3
                STZ     T4
@m:             CLC
                LDA     T2
                ADC     T0
                STA     T2
                LDA     T3
                ADC     T1
                STA     T3
                BCC     :+
                INC     T4
:               DEC     T6
                BNE     @m
@sh:            LSR     T4
                ROR     T3
                ROR     T2
                DEC     T7
                BNE     @sh
                LDA     T2
                LDX     T3
                BNE     @g
                CMP     #0
                BNE     @g
                INC     A              ; at least 1
@g:             STA     T2             ; T2-T3: the gate's units
                STX     T3
                LDX     #0             ; the length and the gate to ms
                JSR     UNITMS
                LDX     #2
                JSR     UNITMS
                LDA     T0
                STA     TONEB+3
                LDA     T1
                STA     TONEB+4
                LDA     T2
                STA     TONEB+5
                LDA     T3
                STA     TONEB+6
@hz:            LDA     NOTEHZ
                STA     TONEB+1
                LDA     NOTEHZ+1
                STA     TONEB+2
                STZ     TONEB          ; voice 0
                LDA     #<TONEB
                LDX     #>TONEB
                JSR     MU_TONE
                BCS     @x             ; (refused: the driver stopped)
                JSR     MSNOW          ; the queue's end: from now if it had ended
                SEC
                LDA     QEND
                SBC     MSN
                LDA     QEND+1
                SBC     MSN+1
                LDA     QEND+2
                SBC     MSN+2
                LDA     QEND+3
                SBC     MSN+3
                BPL     :+             ; QEND still to come
                LDX     #3
@c:             LDA     MSN,X
                STA     QEND,X
                DEX
                BPL     @c
:               CLC                    ; QEND + the entry's ms
                LDA     QEND
                ADC     T0
                STA     QEND
                LDA     QEND+1
                ADC     T1
                STA     QEND+1
                BCC     :+
                INC     QEND+2
                BNE     :+
                INC     QEND+3
:               LDA     NOTEU          ; more of it: straight after
                ORA     NOTEU+1
                BEQ     @ok
                JMP     @chunk
@ok:            CLC
@x:             RTS

; UNITMS: T0+X (2 bytes, 2.5 ms units) to ms, x 5 / 2 rounded: 26,214 at
; most, so 65,535 at most.
UNITMS:         LDA     T0,X           ; x 4 + 1 (T4-T6), 17 bits
                STA     T4
                LDA     T1,X
                STA     T5
                STZ     T6
                ASL     T4
                ROL     T5
                ROL     T6
                ASL     T4
                ROL     T5
                ROL     T6
                CLC                    ; + 1: x 5
                LDA     T4
                ADC     T0,X
                STA     T4
                LDA     T5
                ADC     T1,X
                STA     T5
                BCC     :+
                INC     T6
:               INC     T4             ; + 1, / 2: rounded
                BNE     :+
                INC     T5
                BNE     :+
                INC     T6
:               LSR     T6
                ROR     T5
                ROR     T4
                LDA     T4
                STA     T0,X
                LDA     T5
                STA     T1,X
                RTS

; WAITQ: until voice 0 holds no more than A entries (MF's 0, MB's 31: the
; driver's 32), the events taken meanwhile (CHKINT, screen mode's EVPOLL:
; the note ends, the keys, ^C's break); or until the clock passes the
; queue's end by 100 ms, when MU_STOP sets the count to 0 (the owner's
; answer 6: an end lost to a full event queue can't hang the program).
WAITQ:          STA     WLIM
@w:             LDA     #0
                JSR     MU_COUNT
                CMP     WLIM
                BCC     @x
                BEQ     @x
                JSR     CHKINT
                JSR     MSNOW          ; now - (QEND + 100), 32 bits
                CLC
                LDA     QEND
                ADC     #100
                STA     T4
                LDA     QEND+1
                ADC     #0
                STA     T5
                LDA     QEND+2
                ADC     #0
                STA     T6
                LDA     QEND+3
                ADC     #0
                STA     T7
                SEC
                LDA     MSN
                SBC     T4
                LDA     MSN+1
                SBC     T5
                LDA     MSN+2
                SBC     T6
                LDA     MSN+3
                SBC     T7
                BMI     @w             ; not yet
                LDA     #0             ; lost: the count put right
                JMP     MU_STOP
@x:             RTS

; --- SNDRND (7f's U7) -----------------------------------------------------------------------

; SNDRND (OO-1, the owner's extension: the SID seed plan's answer 8):
; RANDOMIZE SOUND, TXTPTR at SOUND's token: the statement must end; GW-BASIC's
; own driver, started this run (DRVON), quit first if nothing is queued (the
; next SOUND or PLAY starts it again), else Device unavailable; then
; function 82's SN_NOISE for 3 bytes of SID A's voice 3 noise, RND's three
; seed bytes (RNDX: the plan's answer 11), and RND's step as RANDOMIZE n's
; (RNDMN2). Refused (another program's 6510, an OODOS before 0.14 or no
; card): Device unavailable (68).
ERRDNA          = 68
SNDRND:         JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               LDA     DRVON
                BEQ     @n
                LDA     #0             ; notes queued: the driver isn't idle
                JSR     MU_COUNT
                BNE     @dna
                JSR     MU_QUIT
                STZ     DRVON
@n:             LDA     #<NOISEB
                LDX     #>NOISEB
                LDY     #O_SOUND
                JSR     BDOS
                CMP     #0
                BNE     @dna
                LDX     #2
:               LDA     NOISEB + 2,X
                STA     RNDX,X
                DEX
                BPL     :-
                JMP     RNDMN2
@dna:           LDA     #ERRDNA
                JMP     ERROR

; --- MSNOW ---------------------------------------------------------------------------------

; MSNOW: MSN the millisecond counter (32 bits). Screen mode: EVAREA's
; EV_MS, read until two reads agree (the Programmer's Guide, chapter 8: the
; interrupt may write it between two bytes); console mode, which opens no
; event area: function 80's FT_COUNT, its block's +1-4.
MSNOW:          BIT     SCRMOD
                BPL     @con
@r:             LDA     EVAREA + EV_MS ; (each step changes the low byte, so a
                STA     MSN            ; low byte that reads the same after the
                LDA     EVAREA + EV_MS + 1 ; rest means none came between: the
                STA     MSN+1          ; two reads agree)
                LDA     EVAREA + EV_MS + 2
                STA     MSN+2
                LDA     EVAREA + EV_MS + 3
                STA     MSN+3
                LDA     EVAREA + EV_MS
                CMP     MSN
                BNE     @r
                RTS
@con:           LDA     #<FTCB
                LDX     #>FTCB
                LDY     #O_FRAME
                JSR     BDOS
                LDX     #3
:               LDA     FTCB+1,X
                STA     MSN,X
                DEX
                BPL     :-
                RTS

.rodata
BEEPB:          .byte   SN_BEEP, <800, >800, <250, >250 ; function 82: the bell

.data
; GWSTS.ASM's PLAY state (GWRAM.ASM's BEATS ... MMODE), SNDINI's defaults;
; OO-1: in DATA with them, as the overlay loads it, since CLEARC calls SNDINI
; only once a run has used sound (the owner's answer 20).
BEATS:          .byte   120            ; T: quarter notes a minute
MSCALE:         .byte   3              ; 1 ML, 2 MS, 3 MN
MMODE:          .byte   0              ; 0 MF, 1 MB
NOTELN:         .byte   4              ; L: the length
NOTE1L:         .byte   4              ; this note's
OCTAVE:         .byte   4              ; O

.data
NOISEB:         .byte   SN_NOISE, 3, 0, 0, 0 ; function 82's block: three noise bytes (SNDRND's)
FTCB:           .byte   FT_COUNT, 0, 0, 0, 0 ; function 80's FT_COUNT block (console mode's ms)

.segment "BRAM"
; DONOTE's
NOTEHZ:         .res    2              ; the frequency (CX)
NOTEU:          .res    2              ; the length, 2.5 ms units (DX)
NOTESC:         .res    1              ; the gate's scale
TONEB:          .res    MU_NLEN        ; MU_TONE's block
LASTU:          .res    2              ; its entry's units, and
LASTSC:         .res    1              ;   gate's scale (0: none yet)
QEND:           .res    4              ; when voice 0's queue ends, by the ms counter
WLIM:           .res    1              ; WAITQ's
DRVON:          .res    1              ; 1 once this run has started the driver (SNDINI clears it)
MSN:            .res    4              ; MSNOW's
