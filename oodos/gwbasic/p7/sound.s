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
.export SNDINI, SNDRST, SOUNDP, BEEPP, BELLP, DONOTE, MSNOW, MSN
.export BEATS, MSCALE, MMODE, NOTELN, NOTE1L, OCTAVE, NOTEHZ, NOTEU, DOSND, DRVON
.export PLYST, BLKNEW, FLUSH, GWREF, GWCNT, GWC
.import FCERR, SNERR, ADVERR, GETIN2, SYNCHR, FRMEVL, CHRGTR, FRCSNG, PUSHF, POPR, CONIA, FMULS, CHKINT
.import SCRMOD, EVAREA, TIMON, TIMOFP, PLYN
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
                STZ     GWC            ; (OO-1, sound card revision 3: the count, the block, the threshold)
                STZ     PLYST
                JSR     BLKNEW
                LDA     #$FF
                STA     THNOW
                LDA     TIMON          ; (7f: SYSTEM's too: ON TIMER's hook off)
                BEQ     :+
                JSR     TIMOFP
:               JMP     MU_QUIT        ; (MU_STOP's work with it; PE_STATE if not
                                       ;   started, nothing done)
; SNDRST: DONOTE's AL 255, the background music off and the queue emptied:
; MU_STOP of voice 0, if the driver was started (SOUND x,0 with nothing
; sounding starts nothing).
SNDRST:         STZ     GWC            ; (OO-1: nothing waits now, the block dropped)
                JSR     BLKNEW
                LDA     #0             ; (PE_STATE if not started: nothing done)
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
                STZ     PLYST          ; (OO-1: SOUND's notes go at once, not into PLAY's block)
                PLA
                JSR     GETIN2         ; DX: the frequency
                LDA     RDH            ; (review round 1, C5: negative, past the
                BMI     SNDFCE         ; manual's 32,767, Illegal function call too)
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

; NOTEHZ the frequency (0 a rest), NOTEU the length in 2.5 ms units (0: the
; driver's shortest, 1 ms, waited for as any: the plan, SOUND's .0015; review
; round 1, C7), A the gate's scale (1 legato, 2 staccato 3/4, 3 normal 7/8),
; MMODE MF (0) or MB (1). The driver started if it isn't (MU_INIT, PAL's
; clock: C set back if refused: no card, or the 6510 another's). A length
; past 65,535 ms (26,214 units) goes as more than one entry, the first
; waited for as MMODE says, the rest straight after it (OO-1: the driver's
; length is 16 bits of ms). SOUND's own units are held at 65,535 (y x 22
; clipped: about 2 min 44 s at most, a temporary OO-1 limit, review round 1,
; C9). C clear back.
DONOTE:         STA     NOTESC
                LDA     DRVON          ; started already this run
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
                LDA     MMODE          ; MF: nothing waiting first; MB: room (32 notes)
                BEQ     :+
                LDA     #MUQMAX - 1
:               STA     WLIM
                JSR     ROOM
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
                LDA     T0             ; (C7: 0 ms, SOUND's shortest: the driver's
                ORA     T1             ; shortest, 1 ms, gated the whole)
                BNE     :+
                LDA     #1
                STA     T0
                STA     T2
                STZ     T3
:               LDA     T0
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
                LDA     PLYST          ; PLAY's: into the statement's block (OO-1, revision 3)
                BEQ     @tone
                JSR     APPEND
                BRA     @end
@tone:          JSR     SENDT          ; SOUND's: at once (the owner's answer 9)
                BCS     @x             ; (refused: music stopped)
                INC     GWC
@end:
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

; ROOM (OO-1, sound card revision 3): room for one more note under BASIC's
; rule, WLIM waiting at most (MF's 0, MB's 31: 32 notes): the notes the card
; has (GWC) and those in the statement's block (BLKN). With none, the block
; sent (FLUSH) and WAITQ. Carry clear, or set (the card refused).
ROOM:           CLC
                LDA     GWC
                ADC     BLKN
                CMP     WLIM
                BCC     @ok
                BEQ     @ok
                LDA     BLKN
                BEQ     @w
                JSR     FLUSH
                BCS     @x
@w:             JSR     WAITQ
@ok:            CLC
@x:             RTS

; WAITQ: until the card's notes (GWC: its waiting, and the one sounding) are
; WLIM or fewer, the events taken meanwhile (CHKINT, screen mode's EVPOLL:
; SNDEV puts GWC right at each MUSIC_COUNT and MUSIC_DONE; MU_THRESH WLIM so
; a MUSIC_COUNT comes as the room does), or until the clock passes the
; queue's end by 100 ms, when MU_STOP sets the count to 0 (the owner's answer
; 6: an end lost to a full event queue can't hang the program).
WAITQ:          LDA     WLIM
                CMP     GWC
                BCS     @x
                JSR     SETTH          ; (MF's 0: none; its end is MUSIC_DONE)
                JSR     GWREF
@w:             LDA     WLIM
                CMP     GWC
                BCS     @done
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
                STZ     GWC            ; lost: the count put right
                LDA     #0
                JSR     MU_STOP
@done:          JMP     TH0
@x:             RTS

; GWREF: GWC from the card (GWCNT). A = GWC.
GWREF:          JSR     GWCNT
                STA     GWC
                RTS

; GWCNT: A = the card's notes: voice 0's waiting, and 1 more while music
; plays or is paused (the one sounding). PLAY(n)'s; GWC left (SNDEV's
; ON PLAY compares the count before an event with the count after).
GWCNT:          LDA     #0
                JSR     MU_COUNT
                CPX     #0
                BEQ     :+
                CMP     #$FF
                BEQ     :+
                INC     A
:               RTS

; TH0: the threshold as ON PLAY(n) wants it in MB (n - 1: a MUSIC_COUNT as the
; notes fall from n to n - 1), else none; SETTH: A's, if not set already.
TH0:            LDA     #0
                LDX     MMODE
                BEQ     SETTH
                LDX     PLYN
                CPX     #2
                BCC     SETTH
                DEX
                TXA
SETTH:          CMP     THNOW
                BEQ     @x
                STA     THNOW
                JMP     MU_THRESH
@x:             RTS

; BLKNEW: the statement's block empty.
BLKNEW:         STZ     BLKN
                LDA     #<(BLK + 16)
                STA     BLKP
                LDA     #>(BLK + 16)
                STA     BLKP+1
                RTS

; APPEND: TONEB's note (NOTEHZ, TONEB+3-6's length and gate in ms) as the
; block's next entry: a frequency (Hz x 16, the length and the gate in us),
; or a rest (its length).
APPEND:         LDA     BLKP
                STA     MUP
                LDA     BLKP+1
                STA     MUP+1
                LDY     #0
                LDA     NOTEHZ
                ORA     NOTEHZ+1
                BNE     @f
                LDA     #$20           ; a rest
                STA     (MUP),Y
                INY
                LDA     TONEB+3
                LDX     TONEB+4
                JSR     MSUS
                BRA     @n
@f:             LDA     #$10           ; a frequency
                STA     (MUP),Y
                INY
                LDA     NOTEHZ         ; x 16, 24 bits
                STA     U0
                LDA     NOTEHZ+1
                STA     U0+1
                STZ     U0+2
                LDX     #4
:               ASL     U0
                ROL     U0+1
                ROL     U0+2
                DEX
                BNE     :-
                LDX     #0
:               LDA     U0,X
                STA     (MUP),Y
                INY
                INX
                CPX     #3
                BNE     :-
                LDA     TONEB+3
                LDX     TONEB+4
                JSR     MSUS
                LDA     TONEB+5
                LDX     TONEB+6
                JSR     MSUS
@n:             TYA                    ; on past it
                CLC
                ADC     BLKP
                STA     BLKP
                BCC     :+
                INC     BLKP+1
:               INC     BLKN
                RTS

; MSUS: A/X ms x 1,000 (x 1,024 - x 16 - x 8) to (MUP),Y on, 4 bytes; Y on past them.
MSUS:           STA     U0
                STX     U0+1
                STZ     U0+2
                STZ     U0+3
                JSR     @sh
                JSR     @sh
                JSR     @sh
                LDX     #3
:               LDA     U0,X
                STA     U8,X
                DEX
                BPL     :-
                JSR     @sh
                LDX     #3
:               LDA     U0,X
                STA     U16,X
                DEX
                BPL     :-
                LDX     #6
:               JSR     @sh
                DEX
                BNE     :-
                SEC
:               LDA     U0,X
                SBC     U16,X
                STA     U0,X
                INX
                TXA
                EOR     #4
                BNE     :-
                LDX     #0
                SEC
:               LDA     U0,X
                SBC     U8,X
                STA     (MUP),Y
                INY
                INX
                TXA
                EOR     #4
                BNE     :-
                RTS
@sh:            ASL     U0
                ROL     U0+1
                ROL     U0+2
                ROL     U0+3
                RTS

; FLUSH: the statement's block, if it holds a note, to the card (MU_BLOCK,
; queued behind what plays; with the card's 8 blocks in use, again after
; CHKINT); GWC moved on, the block empty, the threshold ON PLAY's. Carry set
; if the card refused it.
FLUSH:          LDA     BLKN
                BNE     :+
                CLC
                RTS
:               LDX     #15            ; the header (one voice, BLKN entries) and voice 0's 50% pulse
:               LDA     BLKHDR,X
                STA     BLK,X
                DEX
                BPL     :-
                LDA     BLKN
                STA     BLK+4
                LDA     #<BLK
                STA     MUB
                LDA     #>BLK
                STA     MUB+1
                SEC
                LDA     BLKP
                SBC     #<BLK
                STA     MUB+2
                LDA     BLKP+1
                SBC     #>BLK
                STA     MUB+3
@try:           LDA     #<MUB
                LDX     #>MUB
                JSR     MU_BLOCK
                BCC     @sent
                CMP     #PE_FULL
                SEC
                BNE     @x
                JSR     CHKINT
                BRA     @try
@sent:          CLC
                LDA     GWC
                ADC     BLKN
                STA     GWC
                JSR     BLKNEW
                JSR     TH0
                CLC
@x:             RTS

; SENDT: MU_TONE of TONEB, again after CHKINT while the card's 8 blocks are in use.
SENDT:          LDA     #<TONEB
                LDX     #>TONEB
                JSR     MU_TONE
                BCC     @x
                CMP     #PE_FULL
                SEC
                BNE     @x
                JSR     CHKINT
                BRA     SENDT
@x:             RTS

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
BLKHDR:         .byte   1, $01, 0, 0, 0, 0, 0, 0 ; a note block: version 1, voice 0, the entries (+4)
                .byte   0, $40, $00, $F0, $00, $08, 255, 128 ; voice 0: a SID voice's 50% pulse (MU_INIT's)
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
WLIM:           .res    1              ; ROOM's and WAITQ's: the notes waiting at most
GWC:            .res    1              ; the card's notes, waiting and sounding, as last seen (OO-1)
THNOW:          .res    1              ; MU_THRESH as last set ($FF: not known)
PLYST:          .res    1              ; 1: in a PLAY statement (DONOTE's notes go into the block)
BLKN:           .res    1              ; the block's notes
BLKP:           .res    2              ; where its next entry goes
MUB:            .res    4              ; MU_BLOCK's block
U0:             .res    4              ; MSUS's
U8:             .res    4
U16:            .res    4
BLK:            .res    16 + MUQMAX * 12 ; PLAY's statement's block: header, instrument, 32 entries
DRVON:          .res    1              ; 1 once this run has started the driver (SNDINI clears it)
MSN:            .res    4              ; MSNOW's
