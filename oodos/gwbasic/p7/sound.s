; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7 (hw/oodos.md, "The plan, phase 7": 7f, sound, input and events;
; the space plan's answer 8: the eighth block, allocated at 7f). It links
; OOLIB's music (../lib/mu.s and physof.s: the Makefile's LIB_p7), its zero
; page word MUP gwbasic.inc's alias of GXQ, exported here. This file holds
; GWSTS.ASM's SOUND, BEEP and the sound reset, and the OEM's DONOTE on sound
; card revision 3's music sequencer: one voice, the card's tone generator,
; as the PC's one speaker (branch C of the sound device's build):
;
;   SNDINI    CLEARC's (P3's INITRP far calls it: RUN, NEW, CLEAR) and
;             SYSTEM's: PLAY's state to its defaults, then music quit (MU_QUIT,
;             released) if it was started (the owner's answer 5: each program
;             starts with the card's music free)
;   SNDRST    the break's (the resident ITSCTC) and SOUND x,0's: music stopped
;             and its queue emptied (MU_STOP), if it was started (SOUND x,0
;             with nothing sounding starts nothing)
;   SOUNDP    SOUND x,y (the home block's stub, screen mode: the owner's
;             answer 4), TXTPTR at x
;   BEEPP     BEEP, and ^G's bell (P6's CBEEP): function 82's SN_BEEP, 800
;             Hz for 250 ms on the card's tone generator, no claim, in both
;             modes (the owner's answer 3: the OEM's); BELLP the bell alone,
;             for P6's CBEEP (^G in screen mode), which prints
;   DONOTE    the OEM's (GWSTS.ASM's EXTRN): a note (a frequency, or a rest)
;             NOTEL us long, gated NOTEG us, into the statement's block: music
;             started first (MU_INIT, at a run's first note), MF's or MB's room
;             before it (ROOM, WAITQ); SOUND's block sent at once, PLAY's at
;             the statement's end or as BASIC's 32 notes need room (FLUSH)
;   MSNOW     the millisecond counter, 32 bits: EVAREA's EV_MS read until two
;             reads agree (screen mode), else function 80's FT_COUNT
;
; And the resident core's and the home block's hand-offs (7f's U1; their code
; is p7/event.s's):
;
;   EVP7      EVPOLL's: an event that isn't a key's, Y its slot's offset in
;             the queue (EVAREA + EV_QUEUE + Y): EK_SOUND SNDEV's (ON
;             PLAY(n)); EK_CTL STREV's (U6); the rest dropped
;   TRPPOL    retired (an RTS, p7/event.s)
;   JOYFP     PEN(n), STICK(n), STRIG(n) from FUNDSP (the resident JOYF)
;   XFUNP     the home block's ERDEVP: PLAY(n) (p7/play.s PLAYN); DATE$,
;             TIME$, TIMER (p7/time.s), A the token
;   PLONP     PLAY ON's (p7/event.s): no remembered trap with the buffer empty
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
;@entry PLONP
.export SNDINI, SNDRST, SOUNDP, BEEPP, BELLP, DONOTE, MSNOW, MSN
.export BEATS, MSCALE, MMODE, NOTELN, NOTE1L, OCTAVE, NOTEHZ, NOTEU, DOSND, DRVON
.export PLYST, BLKNEW, FLUSH, GWCNT, TH0
.import FCERR, SNERR, ADVERR, GETIN2, SYNCHR, FRMEVL, CHRGTR, FRCSNG, FMULS, CHKINT
.import SCRMOD, EVAREA, TIMON, TIMOFP, PLYN
.include "oolib.inc"

.exportzp MUP                          ; mu.s's (lib/zpmu.s's), at gwbasic.inc's address

.code

; --- SNDINI, SNDRST (GWSTS.ASM 822, 840) -----------------------------------------------------

; SNDINI: BEATS 120, MSCALE 3 (MN), MMODE 0 (MF), NOTELN and NOTE1L 4,
; OCTAVE 4; then the sound off (SNDRST's) and, OO-1, music quit.
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
; MU_STOP, if music was started (SOUND x,0 with nothing sounding starts
; nothing).
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
; sound stopped (SNDRST). OO-1 (the manual's p. 323: .0015 to 65,535; the
; owner's answer 19 and branch C's answer 5): below .0015, past 65,535 and
; negative are Illegal function call, where the source checks only 65,536
; and would queue a negative's integer. A frequency past 20,000 Hz (32,767,
; the manual's silence, among them) is a rest (branch C's answer 2: past the
; tone generator's range). Then y x 54,925 us (the PC's tick, the sound
; device's spec), rounded to the us, as one entry, legato: DONOTE.
SOUNDP:         PHA                    ; sound used this run (the owner's answer 20)
                LDA     #1
                STA     SNDUSE
                STZ     PLYST          ; (OO-1: SOUND's note a block of its own, sent at once)
                JSR     BLKNEW
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
SOUNL4:         BIT     FACM1          ; OO-1: negative
                BMI     SNDFCE
                LDX     #0             ; below .0015
                JSR     CMPF
                BCC     SNDFCE
                LDX     #4             ; past 65,535
                JSR     CMPF
                BEQ     :+
                BCS     SNDFCE
:               STZ     RDL            ; BXDX: 54,925, a single (0.D68D00 x 2^16)
                LDA     #$8D
                STA     RDH
                LDA     #$56
                STA     RBL
                LDA     #$90
                STA     RBH
                JSR     FMULS          ; y x 54,925: the us
                JSR     USINT          ; NOTEL
                LDX     #3             ; the gate the whole (the source's SOUND: legato)
:               LDA     NOTEL,X
                STA     NOTEG,X
                DEX
                BPL     :-
                PLA                    ; CX: the frequency
                STA     NOTEHZ
                PLA
                STA     NOTEHZ+1
                CMP     #>20000        ; past 20,000 Hz: a rest
                BCC     SNDGO
                BNE     :+
                LDA     NOTEHZ
                CMP     #<20000 + 1
                BCC     SNDGO
:               STZ     NOTEHZ
                STZ     NOTEHZ+1
; SNDGO: DONOTE; its error (no sound card) is Illegal function call, as the
; source's; then POLKEY's look at the keys. SOUND's and DOSND's.
SNDGO:          JSR     DONOTE
                BCS     SNDFCE
                JMP     CHKINT

; CMPF: the FAC (positive) against DLIM's single at X (its exponent, then its
; mantissa from the top: so for two positive singles, as their values): C
; set if the FAC is as great or greater, Z if the same.
CMPF:           LDA     FACEXP
                CMP     DLIM,X
                BNE     @x
                LDA     FACM1
                CMP     DLIM+1,X
                BNE     @x
                LDA     FACLO+1
                CMP     DLIM+2,X
                BNE     @x
                LDA     FACLO
                CMP     DLIM+3,X
@x:             RTS

; USINT: the FAC (positive, under 2^32) to NOTEL, a whole number, rounded:
; its 24-bit mantissa (the hidden 1 back) shifted by FACEXP - $98, the last
; bit shifted out rounding (exact to the us under 2^24 us, 16.7 s; within a
; single's 256 us at an hour).
USINT:          LDA     FACLO
                STA     NOTEL
                LDA     FACLO+1
                STA     NOTEL+1
                LDA     FACM1
                ORA     #$80
                STA     NOTEL+2
                STZ     NOTEL+3
                SEC
                LDA     FACEXP
                SBC     #$98
                BEQ     @x
                BCC     @r
                TAX                    ; up: x 2^(FACEXP - $98)
@l:             ASL     NOTEL
                ROL     NOTEL+1
                ROL     NOTEL+2
                ROL     NOTEL+3
                DEX
                BNE     @l
@x:             RTS
@r:             EOR     #$FF           ; down: / 2^($98 - FACEXP)
                INC     A
                TAX
@s:             LSR     NOTEL+2
                ROR     NOTEL+1
                ROR     NOTEL
                DEX
                BNE     @s
                BCC     @x             ; the last bit out rounds
                INC     NOTEL
                BNE     @x
                INC     NOTEL+1
                BNE     @x
                INC     NOTEL+2
                RTS

; DOSND: PLAY's note, NOTEU its length in the source's 2.5 ms units (PPAUS2's),
; A the gate's scale (MSCALE's meaning: 1 all, 2 3/4, 3 7/8): the gate's
; units by it (at least 1), then each x 2,500 us, exactly (branch C's answer
; 4: the source's timing, not rounded to the ms), and SNDGO.
DOSND:          STA     NOTESC
                LDA     NOTEU
                STA     T0
                LDA     NOTEU+1
                STA     T1
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
@g:             STA     T6             ; T6-T7: the gate's units
                STX     T7
                LDX     #0             ; NOTEL: the length's us
                JSR     U2500
                LDA     T6
                STA     T0
                LDA     T7
                STA     T1
                LDX     #4             ; NOTEG: the gate's
                JSR     U2500
                JMP     SNDGO

; U2500: T0-T1 (2.5 ms units) x 2,500 to NOTEL + X, 4 bytes (us); T0-T5 used.
U2500:          STZ     T2
                STZ     T3
                STZ     NOTEL,X
                STZ     NOTEL+1,X
                STZ     NOTEL+2,X
                STZ     NOTEL+3,X
                LDA     #<2500
                STA     T4
                LDA     #>2500
                STA     T5
@b:             LSR     T5             ; each bit of 2,500, from the bottom
                ROR     T4
                BCC     @n
                CLC
                LDA     NOTEL,X
                ADC     T0
                STA     NOTEL,X
                LDA     NOTEL+1,X
                ADC     T1
                STA     NOTEL+1,X
                LDA     NOTEL+2,X
                ADC     T2
                STA     NOTEL+2,X
                LDA     NOTEL+3,X
                ADC     T3
                STA     NOTEL+3,X
@n:             ASL     T0
                ROL     T1
                ROL     T2
                ROL     T3
                LDA     T4
                ORA     T5
                BNE     @b
                RTS

; EOSP7: the statement must end at TXTPTR, else Syntax error (GWSTS.ASM's
; EOSCHK, P3's: here too, not worth a far call).
EOSP7:          DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               RTS

; --- DONOTE: the OEM's note on the card's music sequencer ----------------------------------

; NOTEHZ the frequency (0 a rest), NOTEL its length and NOTEG its gate in us
; (32 bits: one entry, however long), MMODE MF (0) or MB (1). Music started
; if it isn't (MU_INIT: C set back if refused: no card, or music another
; program's); then room under BASIC's rule (ROOM: MF one note deep, the
; owner's answer 6; MB 32 notes), the entry into the statement's block
; (APPEND), and for SOUND (PLYST 0) the block sent at once (FLUSH). C clear
; back, or set (refused).
DONOTE:         LDA     DRVON          ; started already this run
                BNE     @on
                LDA     #0             ; (MU_INIT's clock: nothing on the card depends on it)
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
                JSR     APPEND
                LDA     PLYST          ; PLAY's: in the block until the statement's end
                BNE     @ok
                JMP     FLUSH          ; SOUND's: at once
@ok:            CLC
                RTS

; ROOM (OO-1, sound card revision 3): room for one more note under BASIC's
; rule, WLIM waiting at most (MF's 0, MB's 31: 32 notes): the notes the card
; has (GWC, an upper bound: it only falls on its own, so it is asked again
; here, GWCNT, when it says there is none) and those in the statement's block
; (BLKN). With none, the block sent (FLUSH) and WAITQ. Carry clear, or set
; (the card refused).
ROOM:           JSR     FITS
                BCS     @ok
                JSR     GWCNT
                STA     GWC
                JSR     FITS
                BCS     @ok
                LDA     BLKN
                BEQ     @w
                JSR     FLUSH
                BCS     @x
@w:             JSR     WAITQ
@ok:            CLC
@x:             RTS
; FITS: carry set if GWC + BLKN is WLIM or less.
FITS:           CLC
                LDA     GWC
                ADC     BLKN
                STA     T4
                LDA     WLIM
                CMP     T4
                RTS

; WAITQ (OO-1; the owner's answer 7 to branch C's plan): until the card's
; notes (GWCNT: its waiting, and the one sounding) are WLIM or fewer, the card
; asked again after each look at the keys and the events (CHKINT, screen
; mode's EVPOLL): its count is exact, so an event lost to a full queue can't
; hang the wait. GWC the count found.
WAITQ:          JSR     GWCNT
                STA     GWC
                LDA     WLIM
                CMP     GWC
                BCS     @x
                JSR     CHKINT
                BRA     WAITQ
@x:             RTS

; GWCNT: A = the card's notes: voice 0's waiting, and 1 more while music
; plays or is paused (the one sounding: the owner's answer 1 to branch C's
; plan). PLAY(n)'s, ROOM's and WAITQ's; GWC left.
GWCNT:          LDA     #0
                JSR     MU_COUNT
                CPX     #0
                BEQ     :+
                CMP     #$FF
                BEQ     :+
                INC     A
:               RTS

; TH0: the threshold as ON PLAY(n) wants it in MB (n - 1: a MUSIC_COUNT as the
; card's waiting falls from n - 1 to n - 2, PLAY(n)'s count from n to n - 1;
; n = 1 is MUSIC_DONE's), else none; SETTH: A's, if not set already. ON
; PLAY's alone (FLUSH's after a block and ONSETP's), so each MUSIC_COUNT is
; ON PLAY(n)'s fall (SNDEV). Music started (DRVON) first.
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

; APPEND: the note (NOTEHZ, NOTEL, NOTEG) as the block's next entry: a
; frequency (Hz x 16, the length and the gate in us), or a rest (its length).
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
                LDX     #0             ; its length
                JSR     PUT4
                BRA     @n
@f:             LDA     #$10           ; a frequency
                STA     (MUP),Y
                INY
                LDA     NOTEHZ         ; x 16, 24 bits
                STA     T0
                LDA     NOTEHZ+1
                STA     T1
                STZ     T2
                LDX     #4
:               ASL     T0
                ROL     T1
                ROL     T2
                DEX
                BNE     :-
:               LDA     T0,X
                STA     (MUP),Y
                INY
                INX
                CPX     #3
                BNE     :-
                LDX     #0             ; the length, then the gate
                JSR     PUT4
                JSR     PUT4
@n:             TYA                    ; on past it
                CLC
                ADC     BLKP
                STA     BLKP
                BCC     :+
                INC     BLKP+1
:               INC     BLKN
                RTS

; PUT4: NOTEL + X's 4 bytes to (MUP),Y on; X and Y on past them.
PUT4:           LDA     NOTEL,X
                STA     (MUP),Y
                INY
                INX
                TXA
                AND     #3
                BNE     PUT4
                RTS

; FLUSH: the statement's block, if it holds a note, to the card (MU_BLOCK,
; queued behind what plays; with the card's 32 blocks in use, again after
; CHKINT); GWC moved on, the block empty, the threshold ON PLAY's. Carry set
; if the card refused it.
FLUSH:          LDA     BLKN
                BNE     :+
                CLC
                RTS
:               LDX     #15            ; the header (one voice, BLKN entries) and voice 0's instrument
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
                .byte   2, 0, 0, 0, 0, 0, 128, 128 ; voice 0: the tone generator, at the beep's volume (branch C's answer 3)
; DLIM: SOUND's durations as singles (CMPF's order: the exponent, then the
; mantissa from its top): .0015 (one step below the single nearest it, $C49BA6,
; so .0015 as read is in range), and 65,535.
DLIM:           .byte   $77, $44, $9B, $A5
                .byte   $90, $7F, $FF, $00
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
NOTEU:          .res    2              ; PLAY's length, 2.5 ms units (DX)
NOTESC:         .res    1              ; the gate's scale
NOTEL:          .res    4              ; the entry's length, us
NOTEG:          .res    4              ; and its gate, us (NOTEL + 4: U2500's, PUT4's)
WLIM:           .res    1              ; ROOM's and WAITQ's: the notes waiting at most
GWC:            .res    1              ; the card's notes, waiting and sounding, as last seen (OO-1)
THNOW:          .res    1              ; MU_THRESH as last set ($FF: not known)
PLYST:          .res    1              ; 1: in a PLAY statement (DONOTE's notes go into the block)
BLKN:           .res    1              ; the block's notes
BLKP:           .res    2              ; where its next entry goes
MUB:            .res    4              ; MU_BLOCK's block
BLK:            .res    16 + MUQMAX * 12 ; PLAY's statement's block: header, instrument, 32 entries
DRVON:          .res    1              ; 1 once this run has started music (MU_INIT; SNDINI clears it)
MSN:            .res    4              ; MSNOW's
