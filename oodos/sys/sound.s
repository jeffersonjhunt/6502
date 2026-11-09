; OODOS's sound (hw/oolib.md, "The plan", L3): the sound card's claims,
; its RAM and the ES5503's wave RAM loaded by DMA channel 2, the 6510
; started and stopped, the mailbox both ways, the PCM stream fed by
; channel 2 from two buffers, the ES5503 hook, the speaker's beep, the
; event area's sound part, and function 82.
;
; Where the code lives (the owner's decision, 2026-11-08, from L3's sizing):
;
;   block 61 (BIOS)   all of it. Function 82 itself (moved out of block 60
;                     in L4b, 2026-11-08, for L4a's and L4b's room, as
;                     function 83 lives): its entry (the BDOS's table's,
;                     reached with W2 on this block, as the stub leaves it)
;                     and every operation, whose parameters PGET and PPUT
;                     read in W1 and put W2 back on this block; none of it
;                     calls what moves W2 (the BDOS's positioning and
;                     buffers). The loads' engine (DMALOAD, synchronous, a
;                     piece at a time; RAMEND), the beep's division and its
;                     loop (BEEPQ, BEEPGO); and the handlers, reached from the dispatcher through a
;                     W2 borrow (CALL61: W2 on block 61, the routine, W2
;                     back as it was), as CALL62 borrows W3: channel 2's
;                     terminal count (the next piece, the next buffer), the
;                     mailbox into the event queue, the ES5503's
;                     acknowledge and its hook, the area's sound part (once
;                     a frame, and at each buffer's end), the warm boot's
;                     and cold boot's parts. The BDOS calls them directly,
;                     W2 being block 61 but inside PGET and PPUT. W1 is never
;                     code here: the handlers borrow it for the event area.
;   block 62 (RES)    only IRQOTH's branch (input.s), DEAFEN's care of what
;                     L3 owns, and the warm and cold boots' calls (irq.s)
;
; The parts (SC_*), each claimed outright (question 6) and released by
; function 82 or the warm boot: the 6510 and the SIDs, the stream, the
; ES5503. DMA channel 2 is OODOS's: a program reaches it only through the
; loads and the stream.
;
; The stream. OODOS keeps two buffers of the program's (BADR, BLEN; BBUSY
; says which are taken). The first given starts on channel 2 at once
; (demand mode, memory to the card, DRQSEL 1, MODE's interrupt on); at each
; terminal count the handler starts the next piece. A buffer goes in pieces
; of CHUNK bytes: with the FIFO's room the card holds DRQ, and the channel
; then holds the CPU a cycle a byte, so a piece is kept under a tick (7,159
; cycles), as DMAGO keeps its copies, and no tick is lost (the VIA has one
; flag). A buffer's last piece done, it is free (EV_SBUFS), and the other,
; if given, starts. With the FIFO full the card drops DRQ and the piece
; waits, going on as the card plays.
;
; The loads (SN_LOAD, SN_WAVE) run in the BDOS: channel 2 a piece at a
; time, each waited for, with interrupts on between pieces. RAMDATA and
; DOCDATA hold DRQ always, so a piece of CHUNK bytes holds the CPU 4,096
; cycles. A load while a stream buffer is moving is refused, SN_BUSY. During
; a wave load the ES5503's interrupt is held off at the card (IEN bit 3), so
; its handler never moves DOCADDR under the transfer.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.importzp PX, PP, T0, T1, T2, T3, T4, T5, SG ; the BDOS's (bdos.inc), for 82
.import RETFF, PGET, PPUT, SEGOK
.importzp EVP                          ; irq.s's
.import EVON, EVBLK, MS, CLOCKS, UCARD, HK, DHON, CALLHOOK, HOOKADDR, SETHOOK ; irq.s's
.import QEV, EVK, EVC, EVV             ; input.s's
.export F_SOUND, R61, SIENSET, SCLAIM, SIEN, C2ON
.import VBWORK, VBQUIET                ; video.s's

CHUNK           = 4096                 ; channel 2's piece: 4,096 cycles at most with the CPU held
SC_ALL          = SC_6510 | SC_STREAM | SC_DOC
BEEP_DIV        = 715909               ; 7,159,090 / 2 / 5: a half period in the delay loop's 5 cycles
BEEP_LOOP       = 12                   ; the beep loop's own cycles, in 5s, besides the delay (about 60)
SIDVOL          = $18                  ; a SID's $D418: bits 4-6 the filter's outputs, 7 voice 3 off, 0-3 the volume
.assert SC_6510 = 1 && SC_STREAM = 2 && SC_DOC = 4, error, "PQUIET and SN_INFO take the parts as bits 0-2"

.segment "SYSBSS"
SREV:           .res    1              ; the card's revision: 0 none, 1, 2 (cold boot's)
SCODECS:        .res    1              ; its CODECS (revision 2), else 0
SDMA:           .res    1              ; 1: the DMA controller is there
SPARTS:         .res    1              ; the parts this machine has (SC_*)
SCLAIM:         .res    1              ; the parts claimed
SIEN:           .res    1              ; SOUND_IEN as OODOS has it (DEAFEN writes it back)
C2ON:           .res    1              ; 1: a stream buffer is moving on channel 2 (its interrupt on)
BBUSY:          .res    1              ; the buffers given and not yet all sent: bit 0, bit 1
NEXTB:          .res    1              ; the buffer the next SN_BUFFER takes: 0, 1, 0, 1 in turn
BADR:           .res    6              ; each one's physical address, 3 bytes: buffer 0's, then 1's
BLEN:           .res    4              ; each one's length (0: 65,536)
CUR:            .res    1              ; the buffer moving (0, 1)
CADR:           .res    3              ; its next piece's address
CLEFT:          .res    3              ; its bytes not yet started (17 bits)
SUNDER:         .res    2              ; underruns seen (STATE going to 4), from the stream's start
SPREV:          .res    1              ; STATE as last seen
SANS:           .res    1              ; SNDIRQ's: the IRQH bits it answered
STMP:           .res    1
SDCTL:          .res    1              ; DOCIRQ's: DOCCTL and DOCADDR as it found them
SDAL:           .res    2
SDBANK:         .res    1
SST:            .res    12             ; SN_STATUS's: its block's +1-12, at one moment
HTSAVE:         .res    HK_LEN         ; DOCIRQ's: the tick hook's record, set aside
LDLEN:          .res    3              ; DMALOAD's bytes left (17 bits)
SHELD:          .res    1              ; 1: the 6510 held (SN_HOLD), SVOLA and SVOLB kept for SN_GO
SVOLA:          .res    1              ; SID A's $D418 and SID B's as the hold found them
SVOLB:          .res    1

; ---------------------------------------------------------------------------
; Block 61: the handlers, and what the BDOS and the boots share.

.segment "BIOS"

; irq.s's CALL61 (W2 on this block): routine X (sys.inc's R61_*).
R61:            JMP     (R61TAB,X)
R61TAB:         .word   SNDIRQ, SFIELDS, SNDQUIET, SNDINIT
                .word   VBWORK, VBQUIET ; video.s's (L4)
.assert R61_IRQ = 0 && R61_FRAME = 2 && R61_QUIET = 4 && R61_INIT = 6, error, "R61TAB is in sys.inc's R61_* order"
.assert R61_VB = 8 && R61_WARM = 10, error, "R61TAB's video entries are sys.inc's R61_VB, R61_WARM"

BUFBIT:         .byte   $01, $02

; Cold boot's: what there is. A sound card by its ID's high nibble, its
; revision the low; revision 2's codecs; the DMA controller by its ID. The
; parts: the 6510 on any revision; the stream on revision 2 with the DMA
; controller; the ES5503 on revision 2 if CODECS says so.
SNDINIT:        STZ     SREV
                STZ     SCODECS
                STZ     SDMA
                STZ     SPARTS
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @card
                INC     SDMA
@card:          LDA     SOUND_ID
                TAX
                AND     #$F0
                CMP     #SOUND_TYPE
                BNE     @done
                TXA
                AND     #$0F
                STA     SREV
                BEQ     @done
                LDA     #SC_6510
                STA     SPARTS
                LDA     SREV
                CMP     #2
                BCC     @done
                LDA     SOUND_CODECS
                STA     SCODECS
                BPL     @stream        ; bit 7: an ES5503
                LDA     #SC_DOC
                TSB     SPARTS
@stream:        LDA     SDMA
                BEQ     @done
                LDA     #SC_STREAM
                TSB     SPARTS
@done:          RTS
.assert SOUND_CODEC_DOC = $80, error, "SNDINIT tests CODECS's ES5503 bit by BPL"

; The warm boot's (input.s's WBQUIET, before DEAFEN): what QUIET does for
; the card (the 6510 stopped, the SIDs silenced, on revision 2 the stream
; flushed and stopped and the ES5503's RUN cleared), channel 2 aborted and
; its interrupt off, DRQSEL 0, the card's interrupts off, the ES5503 hook
; off, every claim released, a hold forgotten (SIDSTOP), and on revision
; 2 the master volumes back at 255, unity (SN_VOLUME's, L4b). Whether or
; not anything was claimed: a program that used the card behind OODOS's
; back leaves nothing on either.
SNDQUIET:       LDA     #SC_ALL
                JSR     PQUIET
                STZ     SCLAIM
                STZ     SIEN
                LDA     SREV
                BEQ     @done
                STZ     SOUND_IEN
                CMP     #2
                BCC     @done
                LDA     #$FF
                STA     SOUND_VOLL
                STA     SOUND_VOLR
@done:          RTS

; Parts A stopped and given back (their claims are the caller's to clear):
; the stream (channel 2 aborted, the FIFO flushed, RUN off, DRQSEL 0), the
; 6510 (stopped, the SIDs silenced), the ES5503 (its RUN cleared, the hook
; off). Masked by the caller.
PQUIET:         STA     STMP
                AND     #SC_STREAM
                BEQ     @cpu
                JSR     C2STOP
                LDA     SREV
                CMP     #2
                BCC     @cpu
                LDA     #SOUND_PCM_FLUSH
                STA     SOUND_PCMCTL   ; RUN, PAUSE and EOF off, the FIFO emptied
                STZ     SOUND_DRQSEL
@cpu:           LDA     STMP
                AND     #SC_6510
                BEQ     @doc
                JSR     SIDSTOP
@doc:           LDA     STMP
                AND     #SC_DOC
                BEQ     @done
                STZ     DHON ; the ES5503 hook off
                LDA     SREV
                CMP     #2
                BCC     @done
                LDA     SOUND_DOCCTL
                AND     #<~SOUND_DOC_RUN ; frozen where it is, its other bits kept (QUIET's)
                STA     SOUND_DOCCTL
@done:          RTS

; The 6510 stopped (CTRL's other bits kept) and every SID register 0:
; gates and volumes off; a hold forgotten. No card: nothing.
SIDSTOP:        STZ     SHELD
                LDA     SREV
                BEQ     @done
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN
                STA     SOUND_CTRL
                LDX     #SOUND_SIDS - 1
@sid:           STZ     SOUND,X
                DEX
                BPL     @sid
@done:          RTS

; Channel 2 stopped: aborted, its interrupt off, its terminal count
; cleared; no buffer moving and none taken. No DMA controller: the state
; alone.
C2STOP:         STZ     C2ON
                STZ     BBUSY
                STZ     NEXTB
                STZ     CLEFT
                STZ     CLEFT + 1
                STZ     CLEFT + 2
                LDA     SDMA
                BEQ     @done
                LDA     #DMA_CH2
                STA     DMA_ABORT
                STZ     DMA2_CTRL
                STZ     DMA2_MODE
                STA     DMA_STATUS
@done:          RTS

; SOUND_IEN as the claims and the area want it, into SIEN and the card: the
; mailbox's interrupt while the 6510 is claimed and a layout-2 area is open
; (its bytes go to the queue), the ES5503's while its hook is set. The BDOS's
; (claims, the hook, FT_OPEN and FT_CLOSE), with W2 on this block.
SIENSET:        PHP
                SEI
                LDA     SCLAIM
                AND     #SC_6510
                BEQ     @nobox
                BIT     EVON
                BPL     @nobox
                LDA     #SOUND_IEN_MBOX
                BRA     @doc
@nobox:         LDA     #0
@doc:           LDX     DHON
                BEQ     @set
                ORA     #SOUND_IEN_DOC
@set:           STA     SIEN
                LDX     SREV
                BEQ     @done
                STA     SOUND_IEN
@done:          PLP
                RTS
.assert EVON_L2 = $80, error, "SIENSET tests layout 2 by EVON's bit 7"

; The card's interrupt or channel 2's terminal count (input.s's IRQOTH,
; W3 on block 62, W2 here): channel 2's next piece; a byte from the 6510
; into the event queue; the ES5503's interrupt taken and its hook called. A
; = the IRQH bits it answered (NB_IRQ_DMA, NB_IRQ_SOUND), for IRQOTH's
; strays.
SNDIRQ:         STZ     SANS
                LDA     C2ON
                BEQ     @card
                LDA     NB_IRQH
                AND     #NB_IRQ_DMA
                BNE     @card
                LDA     DMA_STATUS
                AND     #DMA_CH2
                BEQ     @card
                STA     DMA_STATUS     ; its count cleared, then the next piece
                LDA     BBUSY
                PHA
                JSR     CNEXT
                PLA
                CMP     BBUSY
                BEQ     @moved         ; a buffer's end: the area's sound part afresh
                JSR     SFIELDS
@moved:         LDA     #NB_IRQ_DMA
                TSB     SANS
@card:          LDA     SIEN
                BEQ     @done
                LDA     NB_IRQH
                AND     #NB_IRQ_SOUND
                BNE     @done
                LDA     SIEN
                AND     #SOUND_IEN_MBOX
                BEQ     @doc
                LDA     SOUND_STATUS
                AND     #SOUND_ST_MBIN
                BEQ     @doc
                JSR     MBOXIN
                LDA     #NB_IRQ_SOUND
                TSB     SANS
@doc:           LDA     SIEN           ; the ES5503's, told by its own $E0 (STATUS's IRQ
                AND     #SOUND_IEN_DOC ; covers a mailbox byte too)
                BEQ     @done
                LDA     SOUND_DOCCTL   ; routed to the card's 6510: its to take, so $E0 isn't
                AND     #SOUND_DOC_IRQ_CARD ; read here (the read would take it)
                BNE     @done
                JSR     DOCIRQ
                BCS     @done          ; none pending
                LDA     #NB_IRQ_SOUND
                TSB     SANS
@done:          LDA     SANS
                RTS

; A byte from the 6510: MBIN read (the interrupt with it), and with a
; layout-2 area open queued as EK_SOUND, code 0. QEV is block 62's: W3 is
; on it, as IRQOTH runs there.
MBOXIN:         LDA     SOUND_MBIN
                BIT     EVON
                BPL     @done
                STA     EVV
                STZ     EVC
                LDA     #EK_SOUND
                STA     EVK
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                JSR     QEV
                PLA
                STA     NB_WIN1
@done:          RTS

; The ES5503's interrupt: its register $E0 read with the registers
; selected (DOCCTL's RAM and AUTOINC off, DOCADDR $E0), which takes the
; lowest oscillator pending: carry set, and nothing else done, if none was
; (bit 7 set). Then its hook, the register first written to the hook's zero
; page byte (HK_ZP, through W1 on the hook's W0); the hook finds the port
; as that read left it. DOCCTL, DOCADDR (both bytes) and DOCBANK are saved
; before the read and put back after the hook, so a register or wave RAM
; access the interrupt came in the middle of goes on as it was, whatever
; the hook wrote. Never during a wave load: SN_WAVE takes IEN bit 3 out of
; SIEN for it, so DOCADDR isn't moved under the transfer.
;
; The hook is called masked, as the tick hook is, and in the tick hook's
; record: masked hooks never nest, so HT's record is set aside, the ES5503
; hook's copied in, CALLHOOK called for HT (its return, THRET, is HT's),
; and HT's put back whole (in a debug build its count with it: FT_COSTS
; reports the frame and tick hooks only).
DOCIRQ:         LDA     SOUND_DOCCTL
                STA     SDCTL
                LDA     SOUND_DOCADDR
                STA     SDAL
                LDA     SOUND_DOCADDR + 1
                STA     SDAL + 1
                LDA     SOUND_DOCBANK
                STA     SDBANK
                LDA     SDCTL
                AND     #<~(SOUND_DOC_RAM | SOUND_DOC_AUTOINC)
                STA     SOUND_DOCCTL   ; the registers, one at a time
                LDA     #$E0
                STA     SOUND_DOCADDR
                LDA     SOUND_DOCDATA  ; the interrupt register: which oscillator, taken
                STA     STMP
                BMI     @none          ; bit 7: none was pending
                LDA     DHON
                BEQ     @back
                LDA     NB_WIN1
                PHA
                LDA     HK + HD + HK_WIN ; its W0: its zero page at $4000 in W1
                STA     NB_WIN1
                LDX     HK + HD + HK_ZP
                LDA     STMP
                STA     $4000,X
                PLA
                STA     NB_WIN1
                LDX     #HK_LEN - 1
@out:           LDA     HK + HT,X
                STA     HTSAVE,X
                LDA     HK + HD,X
                STA     HK + HT,X
                DEX
                BPL     @out
                LDY     #HT
                JSR     CALLHOOK
                LDX     #HK_LEN - 1
@ht:            LDA     HTSAVE,X
                STA     HK + HT,X
                DEX
                BPL     @ht
@back:          JSR     DOCBACK
                CLC
                RTS
@none:          JSR     DOCBACK
                SEC
                RTS

; The port as DOCIRQ found it: DOCADDR, DOCBANK, then DOCCTL.
DOCBACK:        LDA     SDAL
                STA     SOUND_DOCADDR
                LDA     SDAL + 1
                STA     SOUND_DOCADDR + 1
                LDA     SDBANK
                STA     SOUND_DOCBANK
                LDA     SDCTL
                STA     SOUND_DOCCTL
                RTS

; The beep's delay: X 5-cycle steps, then Y - 1 passes of 256. Aligned, so
; neither branch crosses a page (a crossing costs a cycle a step, a fifth of
; the pitch): the BIOS segment is page-aligned.
                .align  16
BEEPD:          DEX
                BNE     BEEPD
                DEY
                BNE     BEEPD
                RTS
.assert >BEEPD = >*, error, "BEEPD's branches stay in its page"

; Channel 2's next piece: of the buffer moving (CUR), or, when it has all
; gone, that buffer free and the other started if it was given; else the
; channel idle (C2ON 0). Masked.
CNEXT:          LDA     CLEFT
                ORA     CLEFT + 1
                ORA     CLEFT + 2
                BNE     PIECE
                LDX     CUR
                LDA     BUFBIT,X
                TRB     BBUSY          ; all sent: free
                TXA
                EOR     #1
                TAX
                LDA     BUFBIT,X
                AND     BBUSY
                BNE     BSTART
                STZ     C2ON
                RTS

; Buffer X started: its address and length to CADR and CLEFT, C2ON set,
; its first piece. Masked.
BSTART:         STX     CUR
                TXA
                ASL     A
                ADC     CUR            ; 3 x: BADR's (carry clear: X is 0 or 1)
                TAY
                LDA     BADR,Y
                STA     CADR
                LDA     BADR + 1,Y
                STA     CADR + 1
                LDA     BADR + 2,Y
                STA     CADR + 2
                TXA
                ASL     A
                TAY
                LDA     BLEN,Y
                STA     CLEFT
                LDA     BLEN + 1,Y
                STA     CLEFT + 1
                STZ     CLEFT + 2
                ORA     CLEFT
                BNE     @on
                INC     CLEFT + 2      ; 0: 65,536
@on:            LDA     #1
                STA     C2ON
                ; fall through

; The next piece of CADR's: CHUNK bytes, or what is left; CADR and CLEFT
; moved on before the channel is enabled, as it may run (and finish) at
; once.
PIECE:          LDA     #<CHUNK        ; STMP/Y the piece: CHUNK, or CLEFT if less
                STA     STMP
                LDY     #>CHUNK
                LDA     CLEFT + 2
                BNE     @set
                LDA     CLEFT + 1
                CMP     #>CHUNK
                BCS     @set
                LDA     CLEFT
                STA     STMP
                LDY     CLEFT + 1
@set:           STZ     DMA2_CTRL
                LDA     CADR
                STA     DMA2_ADDR
                LDA     CADR + 1
                STA     DMA2_ADDR + 1
                LDA     CADR + 2
                STA     DMA2_ADDR + 2
                LDA     STMP
                STA     DMA2_COUNT
                STY     DMA2_COUNT + 1
                LDA     #DMA_MODE_IRQ | DMA_DEMAND_OUT
                STA     DMA2_MODE
                LDA     #SOUND_DRQ_PCM
                STA     SOUND_DRQSEL   ; (a load in between may have moved it)
                CLC
                LDA     CADR
                ADC     STMP
                STA     CADR
                TYA
                ADC     CADR + 1
                STA     CADR + 1
                BCC     @left
                INC     CADR + 2
@left:          SEC
                LDA     CLEFT
                SBC     STMP
                STA     CLEFT
                TYA
                EOR     #$FF           ; CLEFT + 1 - Y, the borrow carried
                ADC     CLEFT + 1
                STA     CLEFT + 1
                BCS     @go
                DEC     CLEFT + 2
@go:            LDA     #DMA_ENABLE
                STA     DMA2_CTRL      ; last: it may run at once
                RTS
.assert <CHUNK = 0, error, "PIECE compares CLEFT's high byte with CHUNK's"

; The event area's sound part, with a layout-2 area open and the stream
; claimed: the state (an underrun counted), the buffers free, the FIFO's
; room, the frames played, the bitrate, the underruns. W1 borrowed for it.
; Once a frame (the video handler), at each terminal count, and from the
; BDOS (masked there).
SFIELDS:        BIT     EVON
                BPL     @no
                LDA     SCLAIM
                AND     #SC_STREAM
                BEQ     @no
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                JSR     SOBS
                LDY     #EV_SSTATE
                STA     (EVP),Y
                INY
                LDA     BBUSY
                EOR     #3
                STA     (EVP),Y
                INY
                LDA     SOUND_FREE     ; (the low byte latches the high)
                STA     (EVP),Y
                INY
                LDA     SOUND_FREE + 1
                STA     (EVP),Y
                LDX     #0
@pos:           INY                    ; POS, its low byte first (which latches the rest), KBPS
                LDA     SOUND_POS,X
                STA     (EVP),Y
                INX
                CPX     #6
                BNE     @pos
                INY
                LDA     SUNDER
                STA     (EVP),Y
                INY
                LDA     SUNDER + 1
                STA     (EVP),Y
                PLA
                STA     NB_WIN1
@no:            RTS
.assert EV_SBUFS = EV_SSTATE + 1 && EV_SFREE = EV_SSTATE + 2 && EV_SPOS = EV_SSTATE + 4 && EV_SKBPS = EV_SPOS + 4 && EV_SUNDER = EV_SKBPS + 2, error, "SFIELDS writes the sound part in EV_S* order"
.assert SOUND_KBPS = SOUND_POS + 4, error, "SFIELDS reads POS and KBPS as one run"

; STATE in A, an underrun counted when it is new (STATE 4 and not 4 when last
; seen). Keeps X and Y.
SOBS:           LDA     SOUND_STATE
                CMP     #SOUND_STATE_UNDERRUN
                BNE     @seen
                CMP     SPREV
                BEQ     @seen
                INC     SUNDER
                BNE     @seen
                INC     SUNDER + 1
@seen:          STA     SPREV
                RTS

; Carry set if the card's address T2/T3 (and T4's bit 0, wave RAM's A16) +
; LDLEN runs past A x $10000: the RAM's end (64 KB the card's, 128 KB wave
; RAM), where the address would wrap.
RAMEND:         STA     SST + 3
                CLC
                LDA     T2
                ADC     LDLEN
                STA     SST
                LDA     T3
                ADC     LDLEN + 1
                STA     SST + 1
                LDA     T4
                AND     #1
                ADC     LDLEN + 2
                CMP     SST + 3
                BCC     @ok
                BNE     @no
                LDA     SST
                ORA     SST + 1
                BNE     @no
@ok:            CLC
                RTS
@no:            SEC
                RTS

; Channel 2 from SG, LDLEN bytes, to the card (DRQSEL set by the caller:
; RAMDATA or DOCDATA, which hold DRQ always), a piece at a time, each
; waited for; interrupts on between. Carry set if a piece never ended (no
; DMA controller, or one that doesn't run): the channel aborted.
DMALOAD:        LDA     SDMA
                BEQ     @fail
                LDA     #DMA_MASTER
                TSB     DMA_CONTROL
@piece:         LDA     #<CHUNK        ; T0/T1 this piece
                STA     T0
                LDA     #>CHUNK
                STA     T1
                LDA     LDLEN + 2
                BNE     @set
                LDA     LDLEN + 1
                CMP     #>CHUNK
                BCS     @set
                LDA     LDLEN
                STA     T0
                LDA     LDLEN + 1
                STA     T1
@set:           STZ     DMA2_CTRL
                LDA     SG
                STA     DMA2_ADDR
                LDA     SG + 1
                STA     DMA2_ADDR + 1
                LDA     SG + 2
                STA     DMA2_ADDR + 2
                LDA     T0
                STA     DMA2_COUNT
                LDA     T1
                STA     DMA2_COUNT + 1
                LDA     #DMA_DEMAND_OUT ; no interrupt: waited for here
                STA     DMA2_MODE
                LDA     #DMA_ENABLE
                STA     DMA2_CTRL      ; it runs at once, the CPU held
                LDX     #0
                LDY     #0
@wait:          LDA     DMA2_CTRL
                LSR     A
                BCC     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH2
                STA     DMA_ABORT
@fail:          SEC
                RTS
@done:          LDA     #DMA_CH2
                STA     DMA_STATUS     ; its count cleared
                CLC
                LDA     SG
                ADC     T0
                STA     SG
                LDA     SG + 1
                ADC     T1
                STA     SG + 1
                BCC     @left
                INC     SG + 2
@left:          SEC
                LDA     LDLEN
                SBC     T0
                STA     LDLEN
                LDA     LDLEN + 1
                SBC     T1
                STA     LDLEN + 1
                BCS     @more
                DEC     LDLEN + 2
@more:          LDA     LDLEN
                ORA     LDLEN + 1
                ORA     LDLEN + 2
                BEQ     @end
                JMP     @piece
@end:           CLC
                RTS
.assert <CHUNK = 0, error, "DMALOAD compares the length's high byte with CHUNK's"

; T4/T5 = BEEP_DIV / T0/T1, less the loop's own BEEP_LOOP (1 at least): a
; 24-by-16 division by shifts.
BEEPQ:          LDA     #<BEEP_DIV     ; SG/SG+1/SG+2 the dividend, becoming the quotient
                STA     SG
                LDA     #>BEEP_DIV
                STA     SG + 1
                LDA     #^BEEP_DIV
                STA     SG + 2
                STZ     T4             ; T4/T5 the remainder
                STZ     T5
                LDX     #24
@bit:           ASL     SG
                ROL     SG + 1
                ROL     SG + 2
                ROL     T4
                ROL     T5
                LDA     T4
                SEC
                SBC     T0
                TAY
                LDA     T5
                SBC     T1
                BCC     @next
                STA     T5
                STY     T4
                INC     SG
@next:          DEX
                BNE     @bit
                SEC                    ; the quotient fits 16 bits: 20 Hz is 35,795
                LDA     SG
                SBC     #BEEP_LOOP
                STA     T4
                LDA     SG + 1
                SBC     #0
                STA     T5
                BCC     @one
                ORA     T4
                BNE     @done
@one:           LDA     #1
                STA     T4
                STZ     T5
@done:          RTS

; The beep's body (SN_BEEP, block 60's, which reads and checks the block):
; T0/T1 the pitch, T2/T3 the length in ms; the speaker toggled each half
; period until the millisecond counter has moved the length on. Here, not
; in block 60, as it reads no parameters (L4's question 4). Aligned, as
; BEEPD is, so the loop's branches never cross a page.
                .align  64
BEEPGO:         JSR     BEEPQ          ; T4/T5 the half period, in the delay loop's 5 cycles
                PHP
                SEI
                LDA     MS             ; SG/SG + 1 the start
                STA     SG
                LDA     MS + 1
                STA     SG + 1
                PLP
BEEPHALF:       LDA     USB_SPKR       ; (any access toggles it)
                LDX     T4             ; the delay: X, then 256 a pass, Y passes
                LDY     T5
                INY
                CPX     #0
                BNE     @d
                DEY
@d:             JSR     BEEPD
                PHP                    ; MS - start, against the length: MS read whole
                SEI
                SEC
                LDA     MS
                SBC     SG
                TAX
                LDA     MS + 1
                SBC     SG + 1
                PLP
                CMP     T3
                BCC     BEEPHALF
                BNE     @end
                CPX     T2
                BCC     BEEPHALF
@end:           RTS
.assert >BEEPHALF = >*, error, "the beep loop's branches stay in its page (a crossing changes the pitch)"

; ---------------------------------------------------------------------------
; Function 82, sound: block 61, as function 83 (video.s) is.

F_SOUND:        LDX     PX
                BEQ     @op            ; X = 0: A is the operation
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #SN_OPS
                BCS     SNFF
                ASL     A
                TAX
                JMP     (SNJUMPS,X)
SNFF:           JMP     RETFF
SNJUMPS:        .word   SN_INFOX, SN_CLAIMX, SN_RELEASEX, SN_LOADX, SN_STARTX, SN_STOPX, SN_SENDX
                .word   SN_STREAMX, SN_BUFFERX, SN_PAUSEX, SN_RESUMEX, SN_SEEKX, SN_ENDX, SN_STATUSX
                .word   SN_WAVEX, SN_DOCHOOKX, SN_BEEPX, SN_HOLDX, SN_GOX, SN_VOLUMEX
.assert (* - SNJUMPS) / 2 = SN_OPS, error, "SNJUMPS has an entry for each of oodos.inc's SN_*"

; The part A claimed, or the function ends: A = X = $FF.
.macro NEED part
                LDA     #part
                AND     SCLAIM
                BNE     :+
                JMP     RETFF
:
.endmacro

; A block, or the function ends.
.macro BLOCK
                LDX     PX
                BNE     :+
                JMP     RETFF
:
.endmacro

SNBUSY:         LDA     #SN_BUSY
                LDX     #$FF
                RTS
SN00:           LDA     #0
                TAX
                RTS

; The card's revision in A, its codecs in X; a block gets them at +1 and +2,
; and the parts no one has claimed at +3.
SN_INFOX:       LDX     PX
                BEQ     @ret
                LDA     SREV
                JSR     PPUT
                LDA     SCODECS
                JSR     PPUT
                LDA     SCLAIM
                EOR     #$FF
                AND     SPARTS
                JSR     PPUT
@ret:           LDA     SREV
                LDX     SCODECS
                RTS

; +1 the parts: refused if one isn't on this machine, or is held. The
; 6510's mailbox emptied first (a byte left from before isn't the program's);
; the stream's state afresh, and with a layout-2 area the area's sound part
; written at once (L4's review of L3: not left for the next frame).
SN_CLAIMX:      BLOCK
                JSR     PGET
                AND     #SC_ALL
                BEQ     @no
                STA     T0
                EOR     #$FF
                AND     SPARTS
                EOR     SPARTS         ; the parts asked for that are there
                CMP     T0
                BNE     @no            ; one isn't
                AND     SCLAIM
                BNE     @no            ; one is held
                PHP
                SEI
                LDA     T0
                AND     #SC_STREAM
                BEQ     @cpu
                JSR     C2STOP
                STZ     SUNDER
                STZ     SUNDER + 1
                STZ     SPREV
@cpu:           LDA     T0
                AND     #SC_6510
                BEQ     @set
                LDA     SOUND_MBIN     ; emptied
@set:           LDA     T0
                TSB     SCLAIM
                JSR     SFIELDS        ; the area's sound part at once, as the stream's start makes it (L4)
                PLP
                JSR     SIENSET
                JMP     SN00
@no:            JMP     RETFF

; +1 the parts: each of them held stopped and given back (PQUIET); the
; others asked for are no one's anyway.
SN_RELEASEX:    BLOCK
                JSR     PGET
                AND     SCLAIM
                STA     T0
                PHP
                SEI
                JSR     PQUIET
                LDA     T0
                TRB     SCLAIM
                PLP
                JSR     SIENSET
                JMP     SN00

; +1-3 the source, +4-5 the card's RAM address, +6-7 the length.
SN_LOADX:       BLOCK
                NEED    SC_6510
                JSR     SRCLEN         ; SG the source, T2/T3 the card's address, LDLEN the length
                BCS     SNFF2
                LDA     #1             ; to the card's RAM's end ($10000) at most
                JSR     RAMEND
                BCS     SNFF2
                LDA     C2ON
                BEQ     @go
                JMP     SNBUSY
@go:            LDA     T2
                STA     SOUND_RAMADDR
                LDA     T3
                STA     SOUND_RAMADDR + 1
                STZ     SOUND_DRQSEL   ; RAMDATA (revision 1 has none: there it is always)
                JSR     DMALOAD
                BCS     SNFF2
                JMP     SN00
SNFF2:          JMP     RETFF

; The block's +1-3 a physical address (SG), then two bytes (T2, T3), then a
; length (LDLEN, 0 as 65,536), checked as 51's segment is (SEGOK: memory a
; segment may reach). Carry set if refused. SN_WAVE's has a third byte
; (T4) before the length: WAVE3 set.
SRCLEN:         STZ     T4
                CLC
                BRA     SRC
SRCLEN3:        SEC
SRC:            PHP
                JSR     PGET
                STA     SG
                JSR     PGET
                STA     SG + 1
                JSR     PGET
                STA     SG + 2
                JSR     PGET
                STA     T2
                JSR     PGET
                STA     T3
                PLP
                BCC     @len
                JSR     PGET
                STA     T4
@len:           JSR     PGET
                STA     LDLEN
                JSR     PGET
                STA     LDLEN + 1
                STZ     LDLEN + 2
                ORA     LDLEN
                BNE     @check
                INC     LDLEN + 2      ; 0: 65,536
@check:         LDA     SG + 2
                CMP     #$10
                BCS     @no
                LDA     LDLEN          ; the last byte: SG + length - 1
                SEC
                SBC     #1
                PHA
                LDA     LDLEN + 1
                SBC     #0
                TAY
                PLA
                JMP     SEGOK          ; carry set: refused
@no:            SEC
                RTS

; +1-2 the entry, +3 CTRL's other bits: the 6510 stopped, its reset vector
; the entry, then reset and run. A hold before is forgotten, once the
; block has all been read (a refused block leaves the hold as it was).
SN_STARTX:      BLOCK
                NEED    SC_6510
                JSR     PGET
                STA     T0
                JSR     PGET
                STA     T1
                JSR     PGET
                AND     #SOUND_NTSC | $08 | $10 ; NTSC, SID B at $D500, BOTH
                STA     T2
                STZ     SHELD
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN
                STA     SOUND_CTRL
                LDA     #<SOUND_RAM_VECTOR
                STA     SOUND_RAMADDR
                LDA     #>SOUND_RAM_VECTOR
                STA     SOUND_RAMADDR + 1
                LDA     T0
                STA     SOUND_RAMDATA
                LDA     T1
                STA     SOUND_RAMDATA
                LDA     T2
                ORA     #SOUND_RUN | SOUND_RESET
                STA     SOUND_CTRL
                JMP     SN00

SN_STOPX:       NEED    SC_6510
                JSR     SIDSTOP
                JMP     SN00

; The 6510 held (L4b): CTRL's RUN off, RESET not pulsed, so its registers
; and RAM stay as they are; then each SID silenced through its own $D418,
; both read first (the card gives back what was last written, by the 6510
; or the 65C02), kept in SVOLA and SVOLB, and written back with the volume
; nibble 0 and the high nibble (the filter's outputs, voice 3 off) kept. Not by VOLL and VOLR: they scale the
; stream's DAC too, and are SN_VOLUME's. RUN off first, as the card drops
; the 65C02's SID writes while the 6510 runs; both read before either is
; written, as with CTRL's BOTH a write to A reaches B too. Refused if the
; 6510 isn't running: never started, stopped, or held already (the kept
; values then stay).
SN_HOLDX:       NEED    SC_6510
                LDA     SOUND_CTRL
                BIT     #SOUND_RUN
                BEQ     SNFF3
                AND     #<~SOUND_RUN
                STA     SOUND_CTRL
                LDA     SOUND_SIDA + SIDVOL
                STA     SVOLA
                LDA     SOUND_SIDB + SIDVOL
                STA     SVOLB
                LDA     SVOLA
                AND     #$F0
                STA     SOUND_SIDA + SIDVOL
                LDA     SVOLB
                AND     #$F0
                STA     SOUND_SIDB + SIDVOL
                LDA     #1
                STA     SHELD
                JMP     SN00
SNFF3:          JMP     RETFF

; A hold undone: RUN off (should the program have set it while held, the
; card would drop the SID writes), the kept $D418s written back (A, then
; B, as with BOTH a write to A reaches B), then RUN on with no RESET, so
; the 6510 carries on from where it was held. CTRL's other bits kept.
; Refused if not held.
SN_GOX:         NEED    SC_6510
                LDA     SHELD
                BEQ     SNFF3
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN
                STA     SOUND_CTRL
                LDA     SVOLA
                STA     SOUND_SIDA + SIDVOL
                LDA     SVOLB
                STA     SOUND_SIDB + SIDVOL
                STZ     SHELD
                LDA     SOUND_CTRL
                ORA     #SOUND_RUN
                STA     SOUND_CTRL
                JMP     SN00

; +1 VOLL, +2 VOLR (0-255, 255 unity): the card's master volumes, which
; scale the SIDs' volume nibbles and the stream's and the ES5503's output
; alike. No claim: the warm boot puts them back at 255. Refused on a card
; before revision 2, which has none.
SN_VOLUMEX:     BLOCK
                LDA     SREV
                CMP     #2
                BCC     SNFF3
                JSR     PGET           ; both read before either is written: a block
                STA     T0             ; refused at +2 changes nothing
                JSR     PGET
                STA     SOUND_VOLR
                LDA     T0
                STA     SOUND_VOLL
                JMP     SN00

; +1 the byte, to MBOUT; SN_BUSY while the 6510 hasn't taken the last.
SN_SENDX:       BLOCK
                NEED    SC_6510
                JSR     PGET
                TAX
                LDA     SOUND_STATUS   ; the last byte not taken yet: busy (a second would replace it)
                AND     #SOUND_ST_MBOUT
                BEQ     @send
                JMP     SNBUSY
@send:          STX     SOUND_MBOUT
                JMP     SN00

; +1 the codec, +2-3 the rate, +4 the format: channel 2 stopped, both
; buffers free, the FIFO flushed, POS 0, the underruns 0; then RUN.
SN_STREAMX:     BLOCK
                NEED    SC_STREAM
                JSR     PGET
                STA     T0
                JSR     PGET
                STA     T1
                JSR     PGET
                STA     T2
                JSR     PGET
                STA     T3
                PHP
                SEI
                JSR     C2STOP
                LDA     #SOUND_PCM_FLUSH
                STA     SOUND_PCMCTL   ; stopped and emptied
                LDA     T0
                STA     SOUND_CODEC
                LDA     T1
                STA     SOUND_RATE
                LDA     T2
                STA     SOUND_RATE + 1
                LDA     T3
                STA     SOUND_PCMFMT
                STZ     SOUND_POS      ; (any write zeroes it)
                STZ     SUNDER
                STZ     SUNDER + 1
                STZ     SPREV
                LDA     #SOUND_DRQ_PCM
                STA     SOUND_DRQSEL
                LDA     #DMA_MASTER
                TSB     DMA_CONTROL
                LDA     #SOUND_PCM_RUN
                STA     SOUND_PCMCTL   ; RUN takes the codec, the rate and the format
                JSR     SFIELDS
                PLP
                JMP     SN00

; +1-3 the buffer, +4-5 its length: into the next of the two in turn (0,
; 1, 0, 1 from the stream's start or a seek), refused, SN_BUSY, while that
; one is still taken; started at once if the channel is idle. A = the
; buffer taken. In turn, not the first free: a program alternating two
; buffers of its own then always has its first in 0 and its second in 1,
; even when one goes to the FIFO whole before the other is given.
SN_BUFFERX:     BLOCK
                NEED    SC_STREAM
                JSR     PGET
                STA     SG
                JSR     PGET
                STA     SG + 1
                JSR     PGET
                STA     SG + 2
                JSR     PGET
                STA     T0
                JSR     PGET
                STA     T1
                LDA     SG + 2
                CMP     #$10
                BCS     @no
                LDA     T0
                SEC
                SBC     #1
                PHA
                LDA     T1
                SBC     #0
                TAY
                PLA
                JSR     SEGOK
                BCS     @no
                PHP
                SEI
                LDX     NEXTB
                LDA     BUFBIT,X
                AND     BBUSY
                BEQ     @free
                PLP
                JMP     SNBUSY
@free:          STX     T2
                TXA
                EOR     #1
                STA     NEXTB
                TXA
                ASL     A
                ADC     T2             ; 3 x (carry clear)
                TAY
                LDA     SG
                STA     BADR,Y
                LDA     SG + 1
                STA     BADR + 1,Y
                LDA     SG + 2
                STA     BADR + 2,Y
                TXA
                ASL     A
                TAY
                LDA     T0
                STA     BLEN,Y
                LDA     T1
                STA     BLEN + 1,Y
                LDA     BUFBIT,X
                TSB     BBUSY
                LDA     C2ON
                BNE     @queued
                JSR     BSTART         ; (block 61's: W2 is on it)
@queued:        JSR     SFIELDS
                PLP
                LDA     T2
                LDX     #0
                RTS
@no:            JMP     RETFF

SN_PAUSEX:      NEED    SC_STREAM
                LDA     #SOUND_PCM_PAUSE
                BRA     PCMSET
SN_RESUMEX:     NEED    SC_STREAM
                LDA     SOUND_PCMCTL
                AND     #<~SOUND_PCM_PAUSE
                BRA     PCMPUT
SN_ENDX:        NEED    SC_STREAM
                LDA     #SOUND_PCM_EOF
PCMSET:         ORA     SOUND_PCMCTL
PCMPUT:         STA     SOUND_PCMCTL
                JMP     SN00

; The FIFO flushed (with RUN set, a seek: the card buffers again), EOF off,
; both buffers dropped and free: the program gives the new place's data.
SN_SEEKX:       NEED    SC_STREAM
                PHP
                SEI
                JSR     C2STOP
                LDA     SOUND_PCMCTL
                AND     #SOUND_PCM_RUN | SOUND_PCM_PAUSE
                ORA     #SOUND_PCM_FLUSH
                STA     SOUND_PCMCTL
                JSR     SFIELDS
                PLP
                JMP     SN00

; A = the state, X = the buffers free; a block gets +1 those, then the
; FIFO's room, the frames, the bitrate and the underruns, of one moment.
SN_STATUSX:     NEED    SC_STREAM
                PHP
                SEI
                JSR     SOBS
                STA     SST
                LDA     BBUSY
                EOR     #3
                STA     SST + 1
                LDA     SOUND_FREE     ; (the low byte latches the high)
                STA     SST + 2
                LDA     SOUND_FREE + 1
                STA     SST + 3
                LDX     #0             ; POS, its low byte first (which latches the rest), KBPS
@pos:           LDA     SOUND_POS,X
                STA     SST + 4,X
                INX
                CPX     #6
                BNE     @pos
                LDA     SUNDER
                STA     SST + 10
                LDA     SUNDER + 1
                STA     SST + 11
                JSR     SFIELDS
                PLP
                LDX     PX
                BEQ     @ret
                LDX     #0
@put:           LDA     SST,X
                JSR     PPUT
                INX
                CPX     #12
                BNE     @put
@ret:           LDA     SST
                LDX     SST + 1
                RTS
.assert SOUND_KBPS = SOUND_POS + 4, error, "SN_STATUS reads POS and KBPS as one run"

; +1-3 the source, +4-6 the wave RAM address (A16 in +6's bit 0), +7-8 the
; length: the ES5503's interrupt held off at the card meanwhile, DOCCTL's
; RAM and AUTOINC on (its RUN and route kept), DOCADDR and DOCBANK set,
; DRQSEL 2; DOCCTL put back after, and IEN.
SN_WAVEX:       BLOCK
                NEED    SC_DOC
                JSR     SRCLEN3        ; SG, T2/T3 the address, T4 its bank, LDLEN
                BCS     @no
                LDA     T4             ; 17 bits: +6 is 0 or 1 (2 up would alias into 0-1)
                CMP     #2
                BCS     @no
                LDA     #2             ; to wave RAM's end ($20000) at most
                JSR     RAMEND
                BCS     @no
                LDA     C2ON
                BEQ     @go
                JMP     SNBUSY
@go:            PHP
                SEI
                LDA     SIEN           ; the ES5503's interrupt out of SIEN for the load, so a
                AND     #<~SOUND_IEN_DOC ; stray's DEAFEN, which writes SIEN back, can't turn it on:
                STA     SIEN           ; its handler never moves DOCADDR under the transfer
                STA     SOUND_IEN
                LDA     SOUND_DOCCTL
                STA     T5
                AND     #SOUND_DOC_RUN | SOUND_DOC_IRQ_CARD
                ORA     #SOUND_DOC_RAM | SOUND_DOC_AUTOINC
                STA     SOUND_DOCCTL
                LDA     T2
                STA     SOUND_DOCADDR
                LDA     T3
                STA     SOUND_DOCADDR + 1
                LDA     T4
                AND     #1
                STA     SOUND_DOCBANK
                LDA     #SOUND_DRQ_DOC
                STA     SOUND_DRQSEL
                PLP
                JSR     DMALOAD
                PHP                    ; (the carry: DMALOAD's)
                SEI
                LDA     T5
                STA     SOUND_DOCCTL
                STZ     SOUND_DRQSEL
                JSR     SIENSET        ; SIEN and IEN as the hook wants them again
                PLP
                BCS     @no
                JMP     SN00
@no:            JMP     RETFF

; +1-2 the hook, 0 none (no block: none), +3 a byte of its zero page
; ($00-$EF) that gets the ES5503's interrupt register before each call:
; HOOKADDR's checks, irq.s's SETHOOK, and the ES5503's interrupt on at the
; card while it is set.
SN_DOCHOOKX:    NEED    SC_DOC
                PHP                    ; the old hook off, and the ES5503's bit out of SIEN and
                SEI                    ; IEN with it, so an interrupt meanwhile waits in the chip
                STZ     DHON           ; for the new hook (SIENSET below), not taken and lost
                LDA     SIEN
                AND     #<~SOUND_IEN_DOC
                STA     SIEN
                STA     SOUND_IEN
                PLP
                JSR     HOOKADDR
                BCS     @no
                BEQ     @set           ; none
                JSR     PGET
                CMP     #$F0
                BCS     @no            ; the RSXs' and the stub's
                STA     HK + HD + HK_ZP
                LDY     #HD
                JSR     SETHOOK
@set:           JSR     SIENSET
                JMP     SN00
@no:            JSR     SIENSET
                JMP     RETFF

; +1-2 the pitch in Hz (20-20,000), +3-4 the length in ms: the USB card's
; speaker toggled each half period, counted in cycles, interrupts left on
; (the tick's 145 cycles a millisecond lower the pitch about 2%); the
; length by the millisecond counter. Refused with no USB card or no VIA.
SN_BEEPX:       BLOCK
                JSR     PGET
                STA     T0
                JSR     PGET
                STA     T1
                JSR     PGET
                STA     T2
                JSR     PGET
                STA     T3
                LDA     UCARD
                BEQ     @no
                LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @no
                LDA     T1             ; 20 to 20,000
                CMP     #>20000
                BCC     @lo
                BNE     @no
                LDA     T0
                CMP     #<20000 + 1
                BCS     @no
@lo:            LDA     T1
                BNE     @ok
                LDA     T0
                CMP     #20
                BCC     @no
@ok:            JSR     BEEPGO         ; (block 61's, W2 on it)
@done:          JMP     SN00
@no:            JMP     RETFF

