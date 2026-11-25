; OODOS's sound (hw/oolib.md, "The plan", L3), on sound card revision 3,
; the card as a device (hw/future/sound-device.md and its interface details):
; function 82, its claims, the card's command records and its events, the
; loads by DMA channel 2 into DATA, the stream's buffers sent when the card
; asks (NEED), the beep and the console's bell on the card, the event area's
; sound part, and the warm and cold boots' parts.
;
; Where the code lives (the owner's decision, 2026-11-08, from L3's sizing):
;
;   block 61 (BIOS)   all of it, and its own variables (block 61 is RAM, as
;                     disk2.s's buffers are): function 82 (the BDOS's table's
;                     entry, reached with W2 on this block, its parameters
;                     read and its results put by PGET and PPUT, a reply's
;                     by PPUTN in block 60, a run at a time, each of which
;                     puts W2 back on this block); the record sender, the reply wait
;                     and the event reader; and the handlers, reached from the
;                     dispatcher through a W2 borrow (CALL61): the card's
;                     events, channel 2's terminal count (the stream's next
;                     piece), the frame's status requests, the warm boot's
;                     and cold boot's parts. W1 is never code here: the
;                     handlers borrow it for the event area.
;   block 62 (RES)    only IRQOTH's branch (input.s), DEAFEN's care of what
;                     L3 owns, and the warm and cold boots' calls (irq.s)
;   SYSBSS            SCLAIM, SIEN and C2ON, which irq.s and input.s read
;
; The card is revision 3 (ID $A3), the card as a device; with no such card
; SN_INFO says 0 and every other operation is refused.
;
; The records (the build plan's flags 8 and 9). Every record goes to CMD
; whole with interrupts off (PHP/SEI ... PLP), so a program's record and
; the handler's never mix. A function 82 call waits for its own REPLY (its
; tag, 1-127) by reading EVT itself, with the card's interrupt off (IEN and
; SIEN 0, so a stray's DEAFEN can't turn it on) and RWAIT set (the handler
; leaves EVT alone); every other record it meets is handled as the handler
; would (EVONE). The handler's own records (STREAM_DATA on NEED, the
; frame's status requests, the bell) carry tags of $80 up, whose replies it
; takes wherever they are read. Each wait has a time-out (100 ms; a beep's
; its length more): A = SN_BUSY.
;
; The stream. OODOS keeps two buffers of the program's (BADR, BLEN; BBUSY
; says which are taken), sent in turn. When the card asks for data (NEED)
; and a buffer is waiting, STREAM_DATA opens a transfer of its length and,
; at its REPLY, channel 2 sends it into DATA in pieces of CHUNK bytes, one
; each terminal count (demand mode: the card asks only while it has room),
; each kept under a tick. A buffer sent, it is free (EV_SBUFS). SN_NEXT opens
; the other stream behind the one playing; each buffer goes to the stream it
; was given for, and SN_END's STREAM_END waits until that stream's buffers
; have all gone.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.importzp PX, PP, T0, T1, SG, TP
.import RETFF, PGET, PPUT, PMAP, SEGOK
.importzp EVP                          ; irq.s's
.import EVON, EVBLK                    ; irq.s's
.import QEV, EVK, EVC, EVV             ; input.s's
.export F_SOUND, R61, SIENSET, SCLAIM, SIEN, C2ON, SBELL
.import VBWORK, VBQUIET                ; video.s's
.ifdef OOLIB_DEBUG
.import HKON, HOOKCOST                 ; irq.s's (a debug build)
.endif

CHUNK           = 4096                 ; channel 2's piece: 4,096 cycles at most with the CPU held
SC_ALL          = SC_TUNE | SC_STREAM | SC_MUSIC
WAIT_MS         = 100                  ; a reply's time-out
READY_MS        = 250                  ; the cold boot's wait for READY
SCHUNK          = 0                    ; STREAM_OPEN's chunk (3 bytes, low byte 0): 65,536, OODOS's largest buffer
PREBUF          = 100                  ; and its prebuffer, ms: raw PCM's (rev 2 began at 4 KB), and
PREBUF_MP3      = 750                  ;   MP3's: the card's decoder takes a dozen frames to decide its sync
HTAG            = $80                  ; the handler's tags: $80 a record whose reply is dropped,
HT_DATA         = $81                  ;   STREAM_DATA,
HT_SST          = $82                  ;   the frame's STREAM_STATUS,
HT_TST          = $83                  ;   TUNE_STATUS,
HT_MST          = $84                  ;   MUSIC_STATUS
RRLEN           = 24                   ; RR: the results kept (the frame's and SN_STATUS's)
EBLEN           = 10                   ; EB: an event's object, stamp and payload's first five
.assert SC_TUNE = 1 && SC_STREAM = 2 && SC_MUSIC = 4, error, "the parts are bits 0-2 (FPEND's too)"

.segment "SYSBSS"
SCLAIM:         .res    1              ; the parts claimed
SIEN:           .res    1              ; SOUND_IEN as OODOS has it (DEAFEN writes it back): EVT, or 0
C2ON:           .res    1              ; 1: a stream buffer is moving on channel 2 (its interrupt on)
WT0:            .res    3              ; the wait's start (MS0-MS2)
WMS:            .res    3              ; and its time-out, ms
RB:             .res    16             ; the program's record: +0 the command, +1 n, +2 the tag, +3 on
HB:             .res    8              ; the handler's
EKIND:          .res    1              ; EVONE's: the record's kind,
ELEFT:          .res    1              ;   the bytes of it still to read,
EB:             .res    EBLEN          ;   an event's first bytes (+0 the object, +1-4 the stamp, +5 on)
BADR:           .res    6              ; each one's physical address, 3 bytes: buffer 0's, then 1's
BLEN:           .res    4              ; each one's length (0: 65,536)
BSTRM:          .res    2              ; each one's stream
CADR:           .res    3              ; its next piece's address
CLEFT:          .res    3              ; its bytes not yet started (17 bits)
PNLEFT:         .res    1              ; PPUTN's: the bytes still to put,
PNEVT:          .res    1              ;   and bit 7 from EVT (else RB)

; ---------------------------------------------------------------------------
; Block 61: the handlers, function 82, and what they share.

.segment "BIOS"

; irq.s's CALL61 (W2 on this block): routine X (sys.inc's R61_*).
R61:            JMP     (R61TAB,X)
R61TAB:         .word   SNDIRQ, SFRAME, SNDQUIET, SNDINIT
                .word   VBWORK, VBQUIET ; video.s's (L4)
.ifdef OOLIB_DEBUG
                .word   HKON, HOOKCOST ; irq.s's hook counters (a debug build)
.assert R61_HKON = 12 && R61_HKCOST = 14, error, "R61TAB's debug entries are sys.inc's R61_HKON, R61_HKCOST"
.endif
.assert R61_IRQ = 0 && R61_FRAME = 2 && R61_QUIET = 4 && R61_INIT = 6, error, "R61TAB is in sys.inc's R61_* order"
.assert R61_VB = 8 && R61_WARM = 10, error, "R61TAB's video entries are sys.inc's R61_VB, R61_WARM"

; The variables (block 61's RAM).
SREV:           .byte   0              ; the card's revision: 0 none, 3 (cold boot's)
SON:            .byte   0              ; 1: a revision 3 card, up: OODOS drives it
SFEAT:          .byte   0              ; its FEATURES (SN_INFO's X)
SPARTS:         .byte   0              ; the parts there are (SC_*: SC_ALL with the card up)
SDMA:           .byte   0              ; 1: the DMA controller is there
STAG:           .byte   0              ; the program's last tag (1-127)
WTAG:           .byte   0              ; the tag a call waits for (0: none)
RWAIT:          .byte   0              ; 1: a call reads EVT itself (the handler leaves it)
WFLAGS:         .byte   0              ; what the wait has seen: WF_*
RST:            .byte   0              ; the awaited REPLY's status
XST:            .byte   0              ; the awaited XFERDONE's status
RMODE:          .byte   0              ; the awaited REPLY's results: 0 dropped, 1 PPUTN (RCNT of them), 2 into RR
RCNT:           .byte   0
RR:             .res    RRLEN          ; a call's results kept
FR:             .res    RRLEN          ; the handler's (a frame's status)
WD:             .res    2              ; WOVER's
WMASK:          .byte   0              ; WAITF's
KEND:           .byte   0              ; KEEPY's
TONES:          .byte   0              ; BEEPs and BELLs sent and not yet ended (BEEP_DONE)
FPEND:          .byte   0              ; the frame's requests sent and not answered: SC_* bits
XBUSY:          .byte   0              ; 1: a program's transfer is open (the stream's must wait)
; The stream's
BBUSY:          .byte   0              ; the buffers given and not yet all sent: bit 0, bit 1
NEXTB:          .byte   0              ; the buffer the next SN_BUFFER takes: 0, 1, 0, 1 in turn
SENDB:          .byte   0              ; the buffer to send next
CUR:            .byte   0              ; the buffer moving (0, 1)
SDPEND:         .byte   0              ; 1: STREAM_DATA sent, its REPLY not yet come
NEEDP:          .byte   0              ; the streams that asked (NEED) and have had nothing: bit 0, bit 1
ENDP:           .byte   0              ; the streams whose SN_END waits for their buffers: bit 0, bit 1
SOPEN:          .byte   0              ; the streams open: bit 0, bit 1
FEED:           .byte   0              ; the stream SN_BUFFER feeds (the last opened)
SPLAY:          .byte   0              ; the stream playing, whose status the area has
NROOM:          .res    2              ; the ring's room, 256-byte units, as the last NEED said
SANS:           .byte   0              ; SNDIRQ's: the IRQH bits it answered
STMP:           .byte   0
LDLEN:          .res    3              ; a load's length
DMODE:          .byte   0              ; PIECE's channel MODE
STMP2:          .byte   0              ; SFRAME's
WF_REPLY        = $01                  ; WFLAGS: the REPLY came,
WF_XDONE        = $02                  ;   its XFERDONE,
WF_FW           = $04                  ;   FWRESULT

BUFBIT:         .byte   $01, $02

; --- the boots -------------------------------------------------------------

; Cold boot's: what there is. The sound card by its ID ($A3), the DMA
; controller by its ID. CTRL's RESET first (a record the ROM left half
; written is dropped: the build plan's flag 7), READY waited for (250 ms),
; DRQ on, the store emptied (FREEALL); then OODOS drives it, its event
; interrupt on.
SNDINIT:        STZ     SREV
                STZ     SON
                STZ     SFEAT
                STZ     SPARTS
                STZ     SDMA
                STZ     SIEN
                LDA     DMA_ID
                CMP     #DMA_ID_V2
                BNE     @card
                INC     SDMA
@card:          LDA     SOUND_ID
                CMP     #SOUND_ID_V3
                BEQ     @v3
                RTS
@v3:            LDA     #3
                STA     SREV
                LDA     #SOUND_CTRL_RESET
                STA     SOUND_CTRL
                LDA     #<READY_MS
                LDX     #>READY_MS
                JSR     WSTART
@ready:         LDA     SOUND_STATUS
                LSR     A              ; READY
                BCS     @up
                JSR     WOVER
                BCC     @ready
                RTS                    ; never up: no card for OODOS
@up:            JSR     WEND
                LDA     SOUND_FEATURES
                STA     SFEAT
                LDA     #SC_ALL
                STA     SPARTS
                INC     SON
                ; fall through: the rest as the warm boot's
.assert SOUND_ST_READY = $01, error, "SNDINIT tests READY by LSR"

; The warm boot's (input.s's WBQUIET, before DEAFEN), and the rest of cold
; boot's: CTRL's RESET (every sound stopped, an open transfer abandoned, both
; FIFOs emptied, the volumes 255, every source's MIX 255 and the EQ off),
; DRQ on, FREEALL (the store emptied); channel 2 aborted, every claim
; released, OODOS's state afresh, the card's event interrupt on. Whether or
; not anything was claimed.
SNDQUIET:       JSR     C2STOP
                STZ     SCLAIM
                STZ     SIEN
                STZ     RWAIT
                STZ     WTAG
                STZ     TONES
                STZ     FPEND
                STZ     XBUSY
                STZ     SOPEN
                STZ     NEEDP
                STZ     ENDP
                STZ     FEED
                STZ     SPLAY
                STZ     NROOM
                STZ     NROOM + 1
                LDA     SON
                BEQ     @done
                LDA     #SOUND_CTRL_RESET
                STA     SOUND_CTRL
                LDA     #SOUND_CTRL_DRQ
                STA     SOUND_CTRL
                LDA     #SOUND_FREEALL
                JSR     HREC0          ; (its reply dropped)
                LDA     #SOUND_INT_EVT
                STA     SIEN
                STA     SOUND_IEN
@done:          RTS

; Channel 2 stopped: aborted, its interrupt off, its terminal count
; cleared; no buffer moving and none taken. No DMA controller: the state
; alone. SELECT is shared: CONTROL saved, the window on channel 2 for its
; CTRL and MODE, CONTROL put back (ABORT and STATUS need no SELECT).
C2STOP:         STZ     C2ON
                STZ     BBUSY
                JSR     SBUFS
                STZ     NEXTB
                STZ     SENDB
                STZ     SDPEND
                STZ     CLEFT
                STZ     CLEFT + 1
                STZ     CLEFT + 2
                LDA     SDMA
                BEQ     @done
                LDA     #DMA_CH2
                STA     DMA_ABORT
                LDA     DMA_CONTROL
                PHA
                AND     #<~DMA_SEL
                ORA     #DMA_SEL2      ; the master as found
                STA     DMA_CONTROL
                STZ     DMA_CTRL
                STZ     DMA_MODE
                PLA
                STA     DMA_CONTROL
                LDA     #DMA_CH2
                STA     DMA_STATUS
@done:          RTS

; SOUND_IEN as OODOS wants it, into SIEN and the card: the card's events
; whenever OODOS drives it, but while a call reads them itself. The BDOS's
; (FT_OPEN and FT_CLOSE), with W2 on this block.
SIENSET:        PHP
                SEI
                LDA     RWAIT
                BNE     @done
                LDA     SON
                BEQ     @set
                LDA     #SOUND_INT_EVT
@set:           STA     SIEN
                LDX     SON
                BEQ     @done
                STA     SOUND_IEN
@done:          PLP
                RTS

; --- records ---------------------------------------------------------------

; The handler's record: A the command, n 0 (HREC0), or n X (HRECN, its
; parameters in HB + 3 on), tag HTAG + Y (HRECT) or HTAG. Carry set, and
; nothing sent, with no room for a whole record (it is tried again later
; by whoever needs it). Masked inside from before its first store to HB:
; SBELL calls it with interrupts on, and the handler's own records go
; through HB too, so building the record and sending it are one masked step
; (review C1). Keeps nothing.
HREC0:          LDX     #0
HRECN:          LDY     #0
HRECT:          PHP
                SEI
                STA     HB
                STX     HB + 1
                TYA
                ORA     #HTAG
                STA     HB + 2
                LDA     SOUND_STATUS
                AND     #SOUND_ST_CMDRDY
                BEQ     @full
                INX                    ; the record's bytes: 3 + n
                INX
                INX
                LDY     #0
@byte:          LDA     HB,Y
                STA     SOUND_CMD
                INY
                DEX
                BNE     @byte
                PLP
                CLC
                RTS
@full:          PLP
                SEC
                RTS

; The program's record in RB (+0 the command, +1 n, parameters from +3) sent
; with a new tag, and its REPLY awaited (RMODE says where its results go): A
; = its status, Z by it; carry set if the card took no record or gave no
; reply in 100 ms (SN_BUSY). RB + 2 the tag. In the BDOS.
CMDW:           LDA     #<WAIT_MS
                LDX     #>WAIT_MS
                JSR     WSTART
                LDA     STAG           ; the next tag, 1-127
                INC     A
                AND     #$7F
                BNE     @tag
                INC     A
@tag:           STA     STAG
                STA     RB + 2
                STA     WTAG
                LDA     #WF_REPLY
                TRB     WFLAGS
@room:          LDA     SOUND_STATUS   ; CMD's room for a whole record, waited for
                AND     #SOUND_ST_CMDRDY
                BNE     @send
                JSR     WOVER
                BCC     @room
                BRA     @fail
@send:          JSR     CMDB
                LDA     #WF_REPLY
                JSR     WAITF
                BCS     @fail
                JSR     WEND
                CLC
                LDA     RST
                RTS
@fail:          JSR     WEND
                SEC
                RTS

; RB's record (3 + RB + 1's n bytes) to CMD, whole, with interrupts off.
CMDB:           LDX     RB + 1
                INX
                INX
                INX
                LDY     #0
                PHP
                SEI
@byte:          LDA     RB,Y
                STA     SOUND_CMD
                INY
                DEX
                BNE     @byte
                PLP
                RTS

; The wait begun: A/X its time-out in ms; the card's interrupt off (SIEN
; too, for DEAFEN), RWAIT set, the start's MS0-2. WFLAGS is the caller's to
; clear before what it waits for is sent (the handler may see it first).
WSTART:         STA     WMS
                STX     WMS + 1
                STZ     WMS + 2
                PHP
                SEI
                LDA     #1
                STA     RWAIT
                STZ     SIEN
                LDA     SON
                BEQ     @t
                STZ     SOUND_IEN
@t:             LDA     NB_MS0         ; (MS0 first, latching MS1-MS3)
                STA     WT0
                LDA     NB_MS1
                STA     WT0 + 1
                LDA     NB_MS2
                STA     WT0 + 2
                PLP
                RTS

; The wait ended: no tag awaited, RWAIT clear, the card's interrupt back.
WEND:           STZ     WTAG
                STZ     RWAIT
                JMP     SIENSET

; Until WFLAGS has a bit of A, reading the card's records meanwhile (EVONE);
; carry set if the wait's time ran out first.
WAITF:          STA     WMASK
@loop:          JSR     WSTEP
                BCS     @x
                LDA     WFLAGS
                AND     WMASK
                BEQ     @loop
                CLC
@x:             RTS

; The wait's step: a record read if one waits (carry clear), else WOVER.
WSTEP:          LDA     SOUND_STATUS
                AND     #SOUND_ST_EVT
                BEQ     WOVER
                JSR     EVONE
                CLC
                RTS

; A whole wait of 100 ms for WFLAGS's A: WSTART, WAITF, WEND; carry by WAITF.
WAITX:          PHA
                LDA     #<WAIT_MS
                LDX     #>WAIT_MS
                JSR     WSTART
                PLA
                JSR     WAITF
                PHP
                JSR     WEND
                PLP
                RTS

; Carry set if the wait's time is over: MS - WT0 (24 bits) past WMS.
WOVER:          PHP
                SEI
                SEC
                LDA     NB_MS0
                SBC     WT0
                STA     WD
                LDA     NB_MS1
                SBC     WT0 + 1
                STA     WD + 1
                LDA     NB_MS2
                SBC     WT0 + 2
                PLP
                CMP     WMS + 2
                BNE     @x
                LDA     WD + 1
                CMP     WMS + 1
                BNE     @x
                LDA     WD
                CMP     WMS
@x:             BCC     @no            ; below: not yet (equal: carry set, over)
                RTS
@no:            CLC
                RTS

; --- the events ---------------------------------------------------------------

; One record from EVT (one waits: STATUS's EVT), whole, and what it means
; done: a REPLY the call waits for (its status, its results by RMODE), the
; handler's own; XFERDONE, FWRESULT; NEED; BEEP_DONE and the rest into the
; event queue as EK_SOUND (code the kind, value the object) with a layout-2
; area open. Masked, but while a program's results go to its block by PPUTN
; (only in a call's wait: the handler never waits). Keeps nothing.
EVONE:          PHP
                SEI
                LDA     SOUND_EVT
                STA     EKIND
                LDA     SOUND_EVT
                STA     ELEFT
                LDA     EKIND
                CMP     #SOUND_EV_REPLY
                BEQ     @reply
                JMP     EVENT
@reply:         JSR     EGET           ; the tag
                TAX
                JSR     EGET           ; the status
                CPX     WTAG
                BNE     @theirs
                LDY     WTAG
                BEQ     @theirs
                STA     RST
                LDA     #WF_REPLY
                TSB     WFLAGS
                STZ     WTAG           ; (a second with it, after a time-out, isn't this one)
                LDA     RMODE
                BEQ     ESKIP
                CMP     #1
                BNE     @keep
                PLP                    ; the program's block: interrupts as the call has them
                LDA     RCNT           ; RCNT of the results, or as many as the record has
                CMP     ELEFT
                BCC     @n
                LDA     ELEFT
@n:             TAX
                BEQ     @pskip
                EOR     #$FF           ; ELEFT less them
                SEC
                ADC     ELEFT
                STA     ELEFT
                SEC                    ; from EVT
                JSR     PPUTN
@pskip:         LDA     ELEFT          ; the rest dropped
                BEQ     @pdone
                JSR     EGET
                BRA     @pskip
@pdone:         RTS
@keep:          LDY     #0
                JSR     KEEPY
                BRA     ESKIP
@theirs:        STA     STMP           ; the handler's: its status
                CPX     #HT_DATA
                BEQ     @data
                CPX     #HT_SST
                BCC     ESKIP          ; HTAG's, or a call's that has gone
                CPX     #HT_MST + 1
                BCS     ESKIP
                LDY     #RRLEN         ; a frame's status: into FR, then the area
                JSR     KEEPY
                TXA
                JSR     FIELDS
                BRA     ESKIP
@data:          LDA     SDPEND         ; STREAM_DATA's: the channel on with the buffer, if taken
                BEQ     ESKIP          ; (dropped by a seek meanwhile)
                STZ     SDPEND
                LDX     SENDB
                LDA     STMP
                BEQ     @go
                CMP     #SOUND_E_STATE ; refused: the card asks once (its NEED stays
                BEQ     ESKIP          ;   outstanding), so but for a stream closed or
                LDY     BSTRM,X        ;   ended (E_STATE) its request is kept, for
                LDA     BUFBIT,Y       ;   the next SFEED to try again (review C4)
                TSB     NEEDP
                BRA     ESKIP
@go:            JSR     BSTART
                ; fall through
; The rest of the record dropped, then PLP.
ESKIP:          LDA     ELEFT
                BEQ     @done
                JSR     EGET
                BRA     ESKIP
@done:          PLP
                RTS

; The record's next byte (ELEFT counted down).
EGET:           DEC     ELEFT
                LDA     SOUND_EVT
                RTS

.segment "BDOS"

; X bytes (1-255) to the program's block at PP, PP moved on past them: from
; EVT (carry set; the caller has counted them off ELEFT) or from RB (carry
; clear). W2 is mapped on the program's block once (PMAP) and again where PP
; crosses a page, not once a byte as PPUT does, then put back on block 61;
; so this is in block 60. An address in the I/O hole ends the function, as
; PPUT's does. Keeps nothing.
PPUTN:          STX     PNLEFT
                LDA     #0
                ROR     A
                STA     PNEVT          ; bit 7: from EVT
                LDY     #0             ; RB's
@map:           JSR     PMAP           ; (TP's low byte is PP's)
@byte:          BIT     PNEVT
                BPL     @rb
                LDA     SOUND_EVT
                BRA     @put
@rb:            LDA     RB,Y
                INY
@put:           STA     (TP)
                INC     TP
                INC     PP
                BNE     @next
                INC     PP + 1
                LDX     #BIOS_BLOCK    ; a page crossed: block 61 back first (PMAP
                STX     NB_WIN2        ;   refuses with it in W2), then mapped again
                DEC     PNLEFT
                BNE     @map
                RTS
@next:          DEC     PNLEFT
                BNE     @byte
                LDX     #BIOS_BLOCK
                STX     NB_WIN2
                RTS

.segment "BIOS"

; The results into RR (Y 0) or FR (Y RRLEN, which follows it), RRLEN at
; most (the rest left for ESKIP). Keeps X.
KEEPY:          TYA
                CLC
                ADC     #RRLEN
                STA     KEND
@k:             LDA     ELEFT
                BEQ     @done
                CPY     KEND
                BCS     @done
                JSR     EGET
                STA     RR,Y
                INY
                BRA     @k
@done:          RTS
.assert FR = RR + RRLEN, error, "KEEPY: FR follows RR"

; An event: its object, stamp and payload's first bytes into EB.
EVENT:          LDY     #0
@k:             LDA     ELEFT
                BEQ     @have
                CPY     #EBLEN
                BCS     @have
                JSR     EGET
                STA     EB,Y
                INY
                BRA     @k
@have:          LDA     EKIND
                BEQ     @sk          ; (none: EVT read with nothing there)
                CMP     #SOUND_EV_XFERDONE
                BNE     @fw
                LDA     EB             ; the tag that opened it: a program's (a
                BMI     @sk          ;   stream's chunk is the handler's)
                LDA     EB + 6         ; its status
                STA     XST
                LDA     #WF_XDONE
                TSB     WFLAGS
                BRA     @sk
@fw:            CMP     #SOUND_EV_FWRESULT
                BNE     @need
                LDA     EB + 5
                STA     XST
                LDA     #WF_FW
                TSB     WFLAGS
@sk:            JMP     ESKIP
@need:          CMP     #SOUND_EV_NEED
                BNE     @track
                LDA     EB + 6         ; the room, bytes / 256 (held at 65,535)
                STA     NROOM
                LDA     EB + 7
                STA     NROOM + 1
                LDA     EB + 8
                BEQ     @ask
                LDA     #$FF
                STA     NROOM
                STA     NROOM + 1
@ask:           LDX     EB             ; the stream
                LDA     BUFBIT,X
                TSB     NEEDP
                JSR     SFEED
                BRA     @sk
@track:         CMP     #SOUND_EV_TRACK
                BNE     @beep
                LDX     EB + 5         ; the stream that ended: closed
                LDA     BUFBIT,X
                TRB     SOPEN
                TRB     NEEDP
                LDA     EB             ; the one now playing
                STA     SPLAY
                BRA     @queue
@beep:          CMP     #SOUND_EV_BEEP_DONE
                BNE     @queue
                LDA     TONES
                BEQ     @queue
                DEC     TONES
@queue:         BIT     EVON           ; a layout-2 area: EK_SOUND, code the kind, value the object
                BPL     @sk
                LDA     #EK_SOUND
                STA     EVK
                LDA     EKIND
                STA     EVC
                LDX     #0             ; the object; MUSIC_MARK's: the marker (+7)
                CMP     #SOUND_EV_MUSIC_MARK
                BNE     @obj
                LDX     #5
@obj:           LDA     EB,X
                STA     EVV
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                CALLRES QEV
                PLA
                STA     NB_WIN1
                JMP     ESKIP
.assert SOUND_EV_REPLY = 1, error, "EVONE: kind 1 is a REPLY"

; A frame's status reply (A its tag) from RR into the area's sound part, a
; layout-2 area open; its request answered (FPEND). W1 borrowed.
FIELDS:         TAX
                LDA     FPBIT - HT_SST,X
                TRB     FPEND
                BIT     EVON
                BPL     @done
                LDA     STMP
                BNE     @done          ; refused: nothing to write
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                CPX     #HT_TST
                BEQ     @tune
                BCS     @music
                JSR     SSFLDS
                BRA     @back
@tune:          LDY     #EV_TSTATE     ; TUNE_STATUS: +0 the state, +4-6 the ms played
                LDA     FR
                STA     (EVP),Y
                LDX     #4
@ms:            INY
                LDA     FR,X
                STA     (EVP),Y
                INX
                CPX     #7
                BNE     @ms
                BRA     @back
@music:         LDY     #EV_MSTATE     ; MUSIC_STATUS: +0 the state, +1 the blocks, +22-23 the notes waiting
                LDA     FR
                STA     (EVP),Y
                INY
                LDA     FR + 1
                STA     (EVP),Y
                INY
                LDA     FR + 22
                STA     (EVP),Y
                INY
                LDA     FR + 23
                STA     (EVP),Y
@back:          PLA
                STA     NB_WIN1
@done:          RTS
FPBIT:          .byte   SC_STREAM, SC_TUNE, SC_MUSIC
.assert HT_TST = HT_SST + 1 && HT_MST = HT_SST + 2, error, "FIELDS: the frame's tags in FPBIT's order"
.assert EV_TMS = EV_TSTATE + 1 && EV_MBLOCKS = EV_MSTATE + 1 && EV_MWAIT = EV_MSTATE + 2, error, "FIELDS writes them in order"

; The stream's fields from a STREAM_STATUS reply in FR (W1 on the area):
; the state, the buffers free, the room, the frames, the bitrate, the
; underruns, as SN_STATUS's block has them.
SSFLDS:         LDX     #RRLEN
                JSR     SSMAP
                LDY     #EV_SSTATE
                LDX     #0
@f:             LDA     SST,X
                STA     (EVP),Y
                INY
                INX
                CPX     #12
                BNE     @f
                RTS
.assert EV_SBUFS = EV_SSTATE + 1 && EV_SFREE = EV_SSTATE + 2 && EV_SPOS = EV_SSTATE + 4 && EV_SKBPS = EV_SPOS + 4 && EV_SUNDER = EV_SKBPS + 2, error, "SSFLDS writes the sound part in SN_STATUS's order"

; SN_STATUS's 14 bytes (SST) from a STREAM_STATUS reply in RR (X 0) or FR
; (X RRLEN): +0 the state, +1 the buffers free, +2-3 the room (NEED's),
; +4-7 the frames (+13 of the reply), +8-9 kbit/s (+17), +10-11 the
; underruns (+19), +12-13 the rate (+2).
SSMAP:          LDA     RR,X
                STA     SST
                LDA     RR + 2,X       ; +12-13 the rate (SN_STATUS's block's +13-14)
                STA     SST + 12
                LDA     RR + 3,X
                STA     SST + 13
                LDA     BBUSY
                EOR     #3
                STA     SST + 1
                LDA     NROOM
                STA     SST + 2
                LDA     NROOM + 1
                STA     SST + 3
                LDY     #0
@c:             LDA     RR + 13,X
                STA     SST + 4,Y
                INX
                INY
                CPY     #8
                BNE     @c
                RTS
SST:            .res    14

; --- the handler ---------------------------------------------------------------

; The card's interrupt or channel 2's terminal count (input.s's IRQOTH, W3
; on block 62, W2 here): the card's records, every one waiting (not while a
; call reads them itself), then the stream's next piece (a REPLY may have
; started a buffer, whose first piece's count is pending now). A = the IRQH
; bits it answered (NB_IRQ_DMA, NB_IRQ_SOUND), for IRQOTH's strays: a
; count still pulling after the piece it started is the next interrupt's.
SNDIRQ:         STZ     SANS
                JSR     SCARD
                LDA     C2ON
                BEQ     @done
                LDA     NB_IRQH
                AND     #NB_IRQ_DMA
                BNE     @done
                LDA     DMA_STATUS
                AND     #DMA_CH2
                BEQ     @done
                STA     DMA_STATUS     ; its count cleared, then the next piece
                JSR     CNEXT
                LDA     #NB_IRQ_DMA
                TSB     SANS
                JSR     SCARD          ; (an event that came while the piece held the CPU)
@done:          LDA     SANS
                RTS

; The card, OODOS's (SIEN on): every record waiting read if it interrupts,
; and the card answered (SANS) either way: an event that comes after the
; read is the next interrupt's, not a stray (the card's interrupt is EVT's
; level alone, so a pull left is always one OODOS drains).
SCARD:          LDA     SIEN
                BEQ     @done
                LDA     NB_IRQH
                AND     #NB_IRQ_SOUND
                BNE     @ans
@drain:         LDA     SOUND_STATUS
                AND     #SOUND_ST_EVT
                BEQ     @ans
                JSR     EVONE
                BRA     @drain
@ans:           LDA     #NB_IRQ_SOUND
                TSB     SANS
@done:          RTS

; The frame's (irq.s's, a layout-2 area open and a part claimed): each
; claimed part's status asked for, if the last frame's has been answered
; (FIELDS writes the area when it comes); a NEED not yet served tried again.
SFRAME:         LDA     SON
                BEQ     @done
                LDX     #2
@k:             LDA     FPBIT,X        ; music, the tune, the stream
                AND     SCLAIM
                BEQ     @next
                AND     FPEND
                BNE     @next
                STX     STMP2
                LDA     SPLAY          ; (the stream's parameter)
                STA     HB + 3
                TXA
                INC     A
                INC     A
                TAY                    ; its tag: HT_SST + X
                LDA     FNN,X
                PHA
                LDA     FCMD,X
                PLX
                JSR     HRECT
                LDX     STMP2
                BCS     @next          ; no room: the next frame's
                LDA     FPBIT,X
                TSB     FPEND
@next:          DEX
                BPL     @k
                JMP     SFEED
@done:          RTS
FCMD:           .byte   SOUND_STREAM_STATUS, SOUND_TUNE_STATUS, SOUND_MUSIC_STATUS
FNN:            .byte   1, 0, 0
.assert HT_SST - HTAG = 2, error, "SFRAME's tags: HT_SST + X"

; The console's bell (crt.s's BEL): BELL, not waited for. Keeps nothing.
SBELL:          LDA     SON
                BEQ     @done
                LDA     #SOUND_BELL
                JSR     HREC0
                BCS     @done
                INC     TONES
@done:          RTS

; --- the stream ------------------------------------------------------------------

; EV_SBUFS as BBUSY has it now (a buffer taken or freed), a layout-2 area
; open and the stream claimed: the program waits on it for a buffer to
; refill, so it can't wait for the frame's status. W1 borrowed. Masked.
SBUFS:          BIT     EVON
                BPL     @done
                LDA     SCLAIM
                AND     #SC_STREAM
                BEQ     @done
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDA     BBUSY
                EOR     #3
                LDY     #EV_SBUFS
                STA     (EVP),Y
                PLA
                STA     NB_WIN1
@done:          RTS

; Each stream whose SN_END waits (ENDP) and has no buffer taken (BBUSY's
; buffers, BSTRM theirs) gets its STREAM_END now, whichever buffer goes
; next: after SN_NEXT the program's buffers are the next stream's, so the
; first's end mustn't wait for a buffer of its own to be free (review C2).
; Masked by the caller.
SENDS:          LDA     ENDP
                BEQ     @x
                LDX     #1
@s:             LDA     BUFBIT,X
                AND     ENDP
                BEQ     @next
                LDY     #1
@b:             LDA     BUFBIT,Y
                AND     BBUSY
                BEQ     @nb
                TXA
                CMP     BSTRM,Y
                BEQ     @next          ; a buffer of its own still taken
@nb:            DEY
                BPL     @b
                STX     HB + 3
                PHX
                LDA     #SOUND_STREAM_END
                LDX     #1
                JSR     HRECN
                PLX
                BCS     @next          ; no room: the next frame's
                LDA     BUFBIT,X
                TRB     ENDP
@next:          DEX
                BPL     @s
@x:             RTS

; The next buffer to the card if it can go: one waiting (SENDB's, given), its
; stream asking (NEEDP), nothing moving or asked for (C2ON, SDPEND), no
; program's transfer open: STREAM_DATA of its length, the channel started at
; its REPLY (EVONE). First, each stream's SN_END that waits for nothing now
; (SENDS). Masked by the caller.
SFEED:          JSR     SENDS
                LDA     C2ON
                ORA     SDPEND
                ORA     XBUSY
                BNE     SFRTS
                LDX     SENDB
; CNEXT's entry: X = SENDB, and nothing moving, asked for or open (a
; buffer's last piece has just gone: C2ON was set, so SDPEND and XBUSY are
; clear).
SFEEDX:         LDA     BUFBIT,X
                AND     BBUSY
                BEQ     SFRTS          ; none given
                LDY     BSTRM,X
                LDA     BUFBIT,Y
                AND     NEEDP
                BEQ     SFRTS          ; its stream hasn't asked
                STY     HB + 3
                TXA
                ASL     A
                TAY
                LDA     BLEN,Y         ; the length, 3 bytes (0: 65,536)
                STA     HB + 4
                LDA     BLEN + 1,Y
                STA     HB + 5
                STZ     HB + 6
                ORA     HB + 4
                BNE     @send
                INC     HB + 6
@send:          LDA     #SOUND_STREAM_DATA
                LDX     #4
                LDY     #HT_DATA - HTAG
                JSR     HRECT
                BCS     SFRTS          ; no room: the next frame tries again
                INC     SDPEND
                LDX     HB + 3
                LDA     BUFBIT,X
                TRB     NEEDP          ; answered
SFRTS:          RTS

; Channel 2's next piece: of the buffer moving (CUR), or, when it has all
; gone, that buffer free, the channel idle (C2ON 0), a stream's waiting
; SN_END sent if that was its last buffer (SENDS), and the next buffer sent
; if the card has asked. Masked.
CNEXT:          LDA     CLEFT
                ORA     CLEFT + 1
                ORA     CLEFT + 2
                BNE     PIECE
                LDX     CUR
                LDA     BUFBIT,X
                TRB     BBUSY          ; all sent: free
                TXA
                EOR     #1
                STA     SENDB
                TAX                    ; (SBUFS keeps X)
                STZ     C2ON
                JSR     SBUFS
                LDA     ENDP
                BEQ     SFEEDX
                JSR     SENDS
                LDX     SENDB
                BRA     SFEEDX

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
                LDA     #DMA_MODE_IRQ | DMA_DEMAND_OUT
                STA     DMODE
                ; fall through

; The next piece of CADR's: CHUNK bytes, or what is left; CADR and CLEFT
; moved on before the channel is enabled, as it may run (and finish) at
; once. It runs in the interrupt (channel 2's terminal count) as well as in
; the BDOS, so it keeps the SELECT rule: CONTROL saved, the window on channel
; 2 (the master on), CONTROL put back before the RTS, so the code it
; interrupted finds the window where it left it.
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
@set:           LDA     DMA_CONTROL
                PHA
                AND     #<~DMA_SEL
                ORA     #DMA_MASTER | DMA_SEL2
                STA     DMA_CONTROL
                STZ     DMA_CTRL
                LDA     CADR
                STA     DMA_ADDR
                LDA     CADR + 1
                STA     DMA_ADDR + 1
                LDA     CADR + 2
                STA     DMA_ADDR + 2
                LDA     STMP
                STA     DMA_COUNT
                STY     DMA_COUNT + 1
                LDA     DMODE          ; the stream's: its interrupt on; a load's: off
                STA     DMA_MODE
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
                STA     DMA_CTRL       ; last: it may run at once
                PLA                    ; CONTROL as PIECE found it, SELECT with it
                STA     DMA_CONTROL
                RTS
.assert <CHUNK = 0, error, "PIECE compares CLEFT's high byte with CHUNK's"

; Channel 2 from SG, LDLEN bytes, into DATA (the card asks while its
; transfer is open and it has room), by PIECE a piece at a time (CADR and
; CLEFT borrowed: no buffer moves during a load), each waited for by its
; terminal count, interrupts on between. Carry set if a piece never ended
; (no DMA controller, or one that doesn't run): the channel aborted.
DMALOAD:        LDA     SDMA
                BEQ     @fail
                LDX     #2
@cp:            LDA     SG,X
                STA     CADR,X
                LDA     LDLEN,X
                STA     CLEFT,X
                DEX
                BPL     @cp
                LDA     #DMA_DEMAND_OUT ; no interrupt: waited for here
                STA     DMODE
@piece:         LDA     #DMA_CH2
                STA     DMA_STATUS     ; (no count left from before)
                PHP
                SEI
                JSR     PIECE
                PLP
                LDX     #0
                LDY     #0
@wait:          LDA     DMA_STATUS
                AND     #DMA_CH2
                BNE     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH2
                STA     DMA_ABORT
@fail:          SEC
                RTS
@done:          STA     DMA_STATUS     ; its count cleared
                LDA     CLEFT
                ORA     CLEFT + 1
                ORA     CLEFT + 2
                BNE     @piece
                CLC
                RTS

; ---------------------------------------------------------------------------
; Function 82, sound: block 61, as function 83 (video.s) is. Each operation
; has 4 bytes in SNTAB: the card's command, DF_* flags with the part in
; bits 0-2, the parameters read from the block, the results put after them;
; or DF_OWN and its routine's address.

F_SOUND:        LDX     PX
                BEQ     @op            ; X = 0: A is the operation
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #SN_OPS
                BCS     SNFF
                ASL     A
                ASL     A
                TAY
                LDA     SNTAB + 1,Y
                BMI     @own
                JMP     GENERIC
@own:
                LDA     SNTAB + 2,Y    ; DF_OWN: its routine, through RB's first two bytes
                STA     RB
                LDA     SNTAB + 3,Y
                STA     RB + 1
                JMP     (RB)
SNFF:           JMP     RETFF

DF_NOBLK        = $08                  ; X = 0 allowed (no parameters)
DF_STREAM       = $10                  ; the stream playing first among the parameters
DF_ARES         = $20                  ; A = the first result (none put)
DF_OWN          = $80                  ; its own routine
.macro OP cmd, flags, np, nr
                .byte   cmd, flags, np, nr
.endmacro
.macro OWN routine
                .byte   0, DF_OWN
                .word   routine
.endmacro
SNTAB:          OWN     SN_INFOX                                        ; 0
                OWN     SN_CLAIMX                                       ; 1
                OWN     SN_RELEASEX                                     ; 2
                OP      SOUND_IDENTIFY, 0, 0, 32                        ; 3 SN_IDENTIFY
                OWN     SN_FIRMX                                        ; 4
                OWN     SN_TUNEX                                        ; 5
                OP      SOUND_TUNE_PLAY, SC_TUNE, 9, 0                  ; 6 SN_PLAY
                OP      SOUND_TUNE_STOP, SC_TUNE | DF_NOBLK, 0, 0       ; 7 SN_STOP
                OP      SOUND_TUNE_PAUSE, SC_TUNE | DF_NOBLK, 0, 0      ; 8 SN_TPAUSE
                OP      SOUND_TUNE_RESUME, SC_TUNE | DF_NOBLK, 0, 0     ; 9 SN_TRESUME
                OP      SOUND_TUNE_STATUS, 0, 0, 16                     ; 10 SN_TSTATUS
                OP      SOUND_TUNE_INFO, 0, 1, 104                      ; 11 SN_TINFO
                OWN     SN_STREAMX                                      ; 12
                OWN     SN_BUFFERX                                      ; 13
                OP      SOUND_STREAM_PAUSE, SC_STREAM | DF_NOBLK | DF_STREAM, 0, 0 ; 14 SN_PAUSE
                OP      SOUND_STREAM_RESUME, SC_STREAM | DF_NOBLK | DF_STREAM, 0, 0 ; 15 SN_RESUME
                OWN     SN_SEEKX                                        ; 16
                OWN     SN_ENDX                                         ; 17
                OWN     SN_STATUSX                                      ; 18
                OWN     SN_NEXTX                                        ; 19
                OWN     SN_SAMPLEX                                      ; 20
                OWN     SN_EFFECTSX                                     ; 21
                OP      SOUND_PLAY, DF_ARES, 7, 1                       ; 22 SN_EFFECT
                OP      SOUND_VOICE_STOP, 0, 1, 0                       ; 23 SN_ESTOP
                OWN     SN_MUSICX                                       ; 24
                OWN     SN_MCTLX                                        ; 25
                OP      SOUND_MUSIC_STATUS, 0, 0, 24                    ; 26 SN_MSTATUS
                OWN     SN_BEEPX                                        ; 27
                OP      SOUND_VOLUME, 0, 2, 0                           ; 28 SN_VOLUME
                OP      SOUND_MIX, 0, 2, 0                              ; 29 SN_MIX
                OWN     SN_EQX                                          ; 30
                OP      SOUND_VIS, 0, 0, 98                             ; 31 SN_VIS
                OP      SOUND_SIDREGS, 0, 0, 50                         ; 32 SN_SIDREGS
                OP      SOUND_LEVELS, 0, 0, 10                          ; 33 SN_LEVELS
.assert (* - SNTAB) / 4 = SN_OPS, error, "SNTAB has an entry for each of oodos.inc's SN_*"

; An operation by its entry (Y its offset in SNTAB, A its flags): a block
; unless DF_NOBLK, the part claimed, a revision 3 card; the record from the
; command, the stream playing (DF_STREAM) and the block's parameters; its
; REPLY's results put in the block after them, or the first in A (DF_ARES).
GENERIC:        STA     T0             ; the flags
                BIT     #DF_NOBLK
                BNE     @blk
                LDX     PX
                BEQ     @ff
@blk:           AND     #SC_ALL
                BEQ     @card
                AND     SCLAIM
                BEQ     @ff
@card:          LDA     SON
                BEQ     @ff
                LDA     SNTAB,Y
                STA     RB
                LDA     SNTAB + 3,Y
                STA     RCNT
                LDA     SNTAB + 2,Y    ; the parameters
                STA     T1
                LDX     #3
                LDA     T0
                AND     #DF_STREAM
                BEQ     @p
                LDA     SPLAY
                STA     RB,X
                INX
@p:             LDA     T1
                BEQ     @rec
                JSR     PGET
                STA     RB,X
                INX
                DEC     T1
                BRA     @p
@rec:           LDA     T0             ; the results to the block (1), or kept (2: DF_ARES)
                AND     #DF_ARES
                BEQ     @put
                LDA     #1
@put:           INC     A
                STA     RMODE
                BRA     RUN            ; (X = 3 + n)
@ff:            JMP     RETFF

; RB's record (X = its length: 3 + n) sent and its REPLY awaited: A = X = 0;
; refused, SN_REFUSED and X its error; no answer, SN_BUSY. DF_ARES: A the
; first result.
RUN:            DEX
                DEX
                DEX
                STX     RB + 1
                JSR     CMDW
                BCS     SNBUSY
                BNE     SNREF
                LDA     RMODE
                CMP     #2
                BNE     SN00
                LDA     RR
                LDX     #0
                RTS
SNREF:          TAX
                LDA     #SN_REFUSED
                RTS
SNBUSY:         LDA     #SN_BUSY
                LDX     #$FF
                RTS
SN00:           LDA     #0
                TAX
                RTS

; A record of A (no parameters) sent, its REPLY awaited, its results
; dropped: as RUN.
RUN0:           STA     RB
                STZ     RMODE
                LDX     #3
                BRA     RUN

; The card driven by OODOS, or the function ends: A = X = $FF.
.macro CARD
                LDA     SON
                BNE     :+
                JMP     RETFF
:
.endmacro
; The part A claimed (and so a card), or the function ends.
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

; The card's revision in A (3, or 0: none), X its FEATURES; a
; block gets them at +1 and +2, and the parts no one has claimed at +3.
SN_INFOX:       LDX     PX
                BEQ     @ret
                LDA     SREV
                JSR     PPUT
                LDA     SFEAT
                JSR     PPUT
                LDA     SCLAIM
                EOR     #$FF
                AND     SPARTS
                JSR     PPUT
@ret:           LDA     SREV
                LDX     SFEAT
                RTS

; +1 the parts: refused if one isn't on this machine (or OODOS doesn't drive
; it), or is held. The stream's: its state afresh.
SN_CLAIMX:      BLOCK
                CARD
                JSR     PGET
                AND     #SC_ALL
                BEQ     @no
                STA     T0
                AND     SCLAIM
                BNE     @no            ; one is held
                PHP
                SEI
                LDA     T0
                AND     #SC_STREAM
                BEQ     @set
                JSR     C2STOP
                STZ     NEEDP
                STZ     ENDP
                STZ     NROOM
                STZ     NROOM + 1
@set:           LDA     T0
                TSB     SCLAIM
                PLP
                JMP     SN00
@no:            JMP     RETFF

; +1 the parts: each of them held stopped and given back: the tune
; (TUNE_STOP), the streams (channel 2 stopped, both closed), music
; (MUSIC_STOP); the others asked for are no one's anyway.
SN_RELEASEX:    BLOCK
                CARD
                JSR     PGET
                AND     SCLAIM
                STA     T0
                TRB     SCLAIM
                JSR     PQUIET
                JMP     SN00

; Parts T0 stopped (their claims the caller's): replies awaited, a
; refusal ignored.
PQUIET:         LDA     T0
                AND     #SC_TUNE
                BEQ     @stream
                LDA     #SOUND_TUNE_STOP
                JSR     RUN0
@stream:        LDA     T0
                AND     #SC_STREAM
                BEQ     @music
                PHP
                SEI
                JSR     C2STOP
                STZ     NEEDP
                STZ     ENDP
                PLP
                JSR     SCLOSE
@music:         LDA     T0
                AND     #SC_MUSIC
                BEQ     @done
                LDA     #SOUND_MUSIC_STOP
                JMP     RUN0
@done:          RTS

; Both streams closed (STREAM_CLOSE: a refusal, none open, ignored).
SCLOSE:         LDA     #1
@s:             STA     RB + 3
                PHA
                LDA     #SOUND_STREAM_CLOSE
                STA     RB
                STZ     RMODE
                LDX     #4
                JSR     RUN
                PLA
                DEC     A
                BPL     @s
                STZ     SOPEN
                STZ     FEED
                STZ     SPLAY
                RTS

; --- the loads ------------------------------------------------------------------

; A physical address and a 3-byte length from the block: SG, LDLEN; checked
; as 51's segment (SRCOK). Carry set: refused.
SRC:            JSR     SRC6
; SG's LDLEN bytes checked as 51's segment (SEGOK, 64 KB at a time; SG kept):
; carry set if refused, or LDLEN is 0.
SRCOK:          LDA     LDLEN
                ORA     LDLEN + 1
                ORA     LDLEN + 2
                BEQ     @no
                LDA     SG + 2
                PHA
                LDX     LDLEN + 2      ; whole 64 KBs first (SEGOK keeps X)
                BEQ     @tail
@whole:         LDA     #$FF
                TAY
                JSR     SEGOK
                BCS     @back
                INC     SG + 2
                DEX
                BNE     @whole
@tail:          LDA     LDLEN
                ORA     LDLEN + 1
                CLC
                BEQ     @back
                LDA     LDLEN          ; the rest: its last byte, SG + its length - 1
                SEC
                SBC     #1
                PHA
                LDA     LDLEN + 1
                SBC     #0
                TAY
                PLA
                JSR     SEGOK
@back:          PLA                    ; (the carry kept)
                STA     SG + 2
                RTS
@no:            SEC
                RTS

; SG and LDLEN from the block's next six bytes.
SRC6:           LDX     #0
@a:             JSR     PGET
                STA     SG,X
                INX
                CPX     #3
                BNE     @a
                LDX     #0
@l:             JSR     PGET
                STA     LDLEN,X
                INX
                CPX     #3
                BNE     @l
                RTS

; A load: RB's record (X its length) sent, then SG's LDLEN bytes into DATA,
; then its XFERDONE: as RUN, the transfer's status a refusal too (SN_REFUSED).
; SN_BUSY while the stream's buffer moves; the stream's next waits for it.
LOAD:           PHP
                SEI
                LDA     C2ON
                ORA     SDPEND
                BEQ     @go
                PLP
                JMP     SNBUSY
@go:            LDA     #1
                STA     XBUSY
                LDA     #WF_XDONE      ; (the transfer's end can't come before the REPLY)
                TRB     WFLAGS
                PLP
                STZ     RMODE
                JSR     RUN
                CMP     #0
                BNE     @x
                JSR     DMALOAD
                BCS     @busy
                LDA     #WF_XDONE
                JSR     WAITX
                BCS     @busy
                JSR     XDONE
                LDA     XST
                BEQ     @ok
                JMP     SNREF
@ok:            JMP     SN00
@busy:          JMP     XBUSYX
@x:             PHA
                PHX
                JSR     XDONE
                PLX
                PLA
                RTS

; A load that failed after the card took it (no DMA controller, a piece
; that never ended, no XFERDONE): the card's transfer closed (XFERABORT;
; its reply or a refusal, none open, either way), then XDONE: SN_BUSY. So
; the next load, and the stream's next STREAM_DATA, find no transfer open
; (review C4).
XBUSYX:         LDA     #SOUND_XFERABORT
                JSR     RUN0
                JSR     XDONE
                JMP     SNBUSY

; A program's transfer over: the stream's next, if it waited.
XDONE:          PHP
                SEI
                STZ     XBUSY
                JSR     SFEED
                PLP
                RTS

; +1-3 a .sid file's address, +4-6 its length, +7 a slot: TUNE_LOAD.
SN_TUNEX:       BLOCK
                NEED    SC_TUNE
                JSR     SRC
                BCS     @no
                JSR     PGET
                STA     RB + 3         ; the slot
                LDA     #SOUND_TUNE_LOAD
                LDX     #7
                BRA     LEN3
@no:            JMP     RETFF

; A load's record: A the command, RB + 4-6 LDLEN (the slot at RB + 3, and X
; the record's length, the caller's), then LOAD.
LEN3:           STA     RB
                LDA     LDLEN
                STA     RB + 4
                LDA     LDLEN + 1
                STA     RB + 5
                LDA     LDLEN + 2
                STA     RB + 6
                JMP     LOAD

; In block 60 (BDOS: W1 in every BDOS call, so SNTAB's jump reaches it as
; it reaches block 61's): rarely called operations, never the interrupt's or
; the stream's (the build plan's risk 1: block 61's room).
.segment "BDOS"

; +1 a slot, +2-4 the address, +5-7 the length, +8 the flags, +9-10 the
; rate, +11-13 the loop's start, +14-16 its end: SAMPLE_LOAD.
SN_SAMPLEX:     BLOCK
                CARD
                JSR     PGET
                STA     RB + 3
                JSR     SRC
                BCS     @no
                LDX     #7
@p:             JSR     PGET           ; +7-15: the flags, the rate, the loop
                STA     RB,X
                INX
                CPX     #16
                BNE     @p
                LDA     #SOUND_SAMPLE_LOAD
                JMP     LEN3
@no:            JMP     RETFF

; +1 the first effect, +2-4 the address, +5-7 the length: EFFECT_LOAD.
SN_EFFECTSX:    BLOCK
                CARD
                JSR     PGET
                STA     RB + 3
                JSR     SRC
                BCS     @no
                LDA     #SOUND_EFFECT_LOAD
                LDX     #7
                JMP     LEN3
@no:            JMP     RETFF

.segment "BIOS"

; +1 a slot, +2-4 a note block's address, +5-7 its length, +8 MUSIC_PLAY's
; flags: MUSIC_LOAD (the block checked: XFERDONE), then MUSIC_PLAY.
SN_MUSICX:      BLOCK
                NEED    SC_MUSIC
                JSR     PGET
                STA     RB + 3
                PHA
                JSR     SRC
                PLA
                BCS     @no
                STA     T0             ; the slot
                JSR     PGET
                STA     T1             ; the flags
                LDA     #SOUND_MUSIC_LOAD
                LDX     #7
                JSR     LEN3
                CMP     #0
                BNE     @ret           ; refused or busy
                LDA     #SOUND_MUSIC_PLAY
                STA     RB
                LDA     T0
                STA     RB + 3
                LDA     T1
                STA     RB + 4
                STZ     RMODE
                LDX     #5
                JMP     RUN
@ret:           RTS
@no:            JMP     RETFF

; In block 60 (BDOS: W1 in every BDOS call, so SNTAB's jump reaches it as
; it reaches block 61's): rarely called operations, never the interrupt's or
; the stream's (the build plan's risk 1: block 61's room).
.segment "BDOS"

; +1-3 the address, +4-6 the length, +7 0 a piece, 1 begin (+8-11 the
; image's length; the piece after it if its length isn't 0), 2 commit
; (FWRESULT awaited: its status a refusal too). No claim; refused while any
; part is claimed. A piece that ends the image: its XFERDONE awaited.
SN_FIRMX:       BLOCK
                CARD
                LDA     SCLAIM
                BNE     @no
                JSR     SRC6
                JSR     PGET
                CMP     #1
                BEQ     @begin
                BCS     @commit
@piece:         JSR     SRCOK
                BCS     @no
                LDA     #1
                STA     XBUSY
                JSR     DMALOAD
                BCS     @busy
                LDA     SOUND_STATUS   ; the image whole: its check
                AND     #SOUND_ST_XFER
                BNE     @ok            ; (more to come: XBUSY stays)
                LDA     #WF_XDONE
                JSR     WAITX
                BCS     @busy
                JSR     XDONE
                LDA     XST
                BNE     @ref
@ok:            JMP     SN00
@busy:          JMP     XBUSYX
@ref:           JMP     SNREF
@no:            JMP     RETFF
@begin:         LDX     #3
@ilen:          JSR     PGET           ; +8-11 the image's length
                STA     RB,X
                INX
                CPX     #7
                BNE     @ilen
                LDA     #SOUND_FW_BEGIN
                STA     RB
                STZ     RMODE
                LDA     #WF_XDONE
                TRB     WFLAGS
                JSR     RUN
                CMP     #0
                BNE     @ret
                LDA     LDLEN
                ORA     LDLEN + 1
                ORA     LDLEN + 2
                BEQ     @ret
                BRA     @piece
@commit:        LDA     #WF_FW
                TRB     WFLAGS
                LDA     #SOUND_FW_COMMIT
                JSR     RUN0
                CMP     #0
                BNE     @ret
                LDA     #WF_FW
                JSR     WAITX
                BCS     @busy
                LDA     XST
                BNE     @ref
                JMP     SN00
@ret:           RTS

.segment "BIOS"

; --- the stream ------------------------------------------------------------------

; +1 the codec, +2-3 the rate, +4 the format: channel 2 stopped, both
; buffers free, both streams closed, then stream 0 opened (STREAM_OPEN with
; OODOS's chunk and prebuffer).
SN_STREAMX:     BLOCK
                NEED    SC_STREAM
                JSR     SPARM
                PHP
                SEI
                JSR     C2STOP
                STZ     NEEDP
                STZ     ENDP
                STZ     NROOM
                STZ     NROOM + 1
                PLP
                JSR     SCLOSE         ; (RB's parameters first kept in SG and T0-T1)
                LDA     #0
                ; fall through: stream 0 opened

; Stream A opened with SPARM's parameters (SG, SG + 1-2 ... see SPARM).
SOPENA:         PHA
                STA     RB + 3
                LDA     SG
                STA     RB + 4
                LDA     SG + 1
                STA     RB + 5
                LDA     SG + 2
                STA     RB + 6
                LDA     T0
                STA     RB + 7
                LDA     #<SCHUNK       ; the chunk, 65,536
                STA     RB + 8
                LDA     #>SCHUNK
                STA     RB + 9
                LDA     #1
                STA     RB + 10
                LDX     #<PREBUF
                LDY     #>PREBUF
                LDA     SG             ; the codec
                BEQ     @pre
                LDX     #<PREBUF_MP3
                LDY     #>PREBUF_MP3
@pre:           STX     RB + 11
                STY     RB + 12
                LDA     #SOUND_STREAM_OPEN
                STA     RB
                STZ     RMODE
                LDX     #13
                JSR     RUN
                PLY                    ; the stream
                CMP     #0
                BNE     @ret
                PHP
                SEI
                LDA     BUFBIT,Y
                TSB     SOPEN
                STY     FEED
                PLP
                LDA     #0
@ret:           RTS
.assert <SCHUNK = 0 && >SCHUNK = 0, error, "SOPENA's chunk: 65,536 ($010000)"

; The block's +1-4 (the codec, the rate, the format) into SG, SG + 1-2, T0.
SPARM:          JSR     PGET
                STA     SG
                JSR     PGET
                STA     SG + 1
                JSR     PGET
                STA     SG + 2
                JSR     PGET
                STA     T0
                RTS

; +1-4 as SN_STREAM's: the other stream opened and queued behind the one
; fed (STREAM_NEXT); SN_BUFFER feeds it from now on.
SN_NEXTX:       BLOCK
                NEED    SC_STREAM
                JSR     SPARM
                LDA     FEED
                PHA
                EOR     #1
                JSR     SOPENA
                PLY                    ; the first
                CMP     #0
                BNE     @ret
                STY     RB + 3
                LDA     FEED
                STA     RB + 4
                LDA     #SOUND_STREAM_NEXT
                STA     RB
                STZ     RMODE
                LDX     #5
                JMP     RUN
@ret:           RTS

; +1-3 the buffer, +4-5 its length (0: 65,536): into the next of the two in
; turn (0, 1, 0, 1 from the stream's start or a seek), refused, SN_BUSY,
; while that one is still taken; sent at once if the card has asked. A = the
; buffer taken.
SN_BUFFERX:     BLOCK
                NEED    SC_STREAM
                JSR     PGET
                STA     SG
                JSR     PGET
                STA     SG + 1
                JSR     PGET
                STA     SG + 2
                JSR     PGET
                STA     LDLEN
                JSR     PGET
                STA     LDLEN + 1
                STZ     LDLEN + 2
                ORA     LDLEN
                BNE     @chk
                INC     LDLEN + 2
@chk:           JSR     SRCOK
                BCS     @no
                PHP
                SEI
                LDX     NEXTB
                LDA     BUFBIT,X
                AND     BBUSY
                BEQ     @free
                PLP
                JMP     SNBUSY
@free:          STX     T0
                TXA
                EOR     #1
                STA     NEXTB
                TXA
                ASL     A
                ADC     T0             ; 3 x (carry clear)
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
                LDA     LDLEN
                STA     BLEN,Y
                LDA     LDLEN + 1
                STA     BLEN + 1,Y
                LDA     FEED
                STA     BSTRM,X
                LDA     BUFBIT,X
                TSB     BBUSY
                JSR     SBUFS
                JSR     SFEED
                PLP
                LDA     T0
                LDX     #0
                RTS
@no:            JMP     RETFF

; A seek: channel 2 stopped, both buffers dropped and free, a NEED forgotten
; (the card asks again), a waiting SN_END dropped (the flush clears the
; card's end too); a next stream queued by SN_NEXT closed (STREAM_CLOSE, its
; transfer with it), so SN_BUFFER and SN_END feed the stream playing again
; (review C3); then STREAM_FLUSH of the stream playing (its transfer
; aborted with it).
SN_SEEKX:       NEED    SC_STREAM
                PHP
                SEI
                JSR     C2STOP
                STZ     NEEDP
                STZ     ENDP
                PLP
                LDA     FEED
                CMP     SPLAY
                BEQ     @flush
                STA     RB + 3         ; the queued one: closed (a refusal ignored)
                LDA     #SOUND_STREAM_CLOSE
                STA     RB
                STZ     RMODE
                LDX     #4
                JSR     RUN
                PHP
                SEI
                LDX     FEED
                LDA     BUFBIT,X
                TRB     SOPEN
                TRB     NEEDP          ; (an ask that came meanwhile: closed now)
                LDA     SPLAY
                STA     FEED
                PLP
@flush:         LDA     #SOUND_STREAM_FLUSH
                ; fall through
; A stream command of the stream playing, its reply awaited.
SCMD:           STA     RB
                LDA     SPLAY
                STA     RB + 3
                STZ     RMODE
                LDX     #4
                JMP     RUN

; No more data: STREAM_END of the stream fed, once its buffers have all
; gone (at once if they have).
SN_ENDX:        NEED    SC_STREAM
                PHP
                SEI
                LDX     FEED
                LDA     BUFBIT,X
                TSB     ENDP
                JSR     SFEED          ; (sends it now if nothing waits)
                PLP
                JMP     SN00

; A = the state, X = the buffers free; a block gets +1 those, then the
; ring's room, the frames, the bitrate and the underruns: STREAM_STATUS of
; the stream playing.
SN_STATUSX:     NEED    SC_STREAM
                LDA     #2
                STA     RMODE
                LDA     #SOUND_STREAM_STATUS
                STA     RB
                LDA     SPLAY
                STA     RB + 3
                LDA     #1
                STA     RB + 1
                JSR     CMDW
                BCS     @busy
                BNE     @ref
                PHP                    ; SST mapped and copied into RB with interrupts off:
                SEI                    ;   the frame's reply rewrites SST (review C5)
                LDX     #0
                JSR     SSMAP
                LDX     #13
@cp:            LDA     SST,X
                STA     RB,X
                DEX
                BPL     @cp
                PLP
                LDX     PX
                BEQ     @ret
                LDX     #14
                CLC                    ; from RB
                JSR     PPUTN
@ret:           LDA     RB
                LDX     RB + 1
                RTS
@busy:          JMP     SNBUSY
@ref:           JMP     SNREF

; --- music, the beep, the EQ --------------------------------------------------

; +1 0 stop, 1 pause, 2 resume, 3 the threshold (+2 n).
SN_MCTLX:       BLOCK
                NEED    SC_MUSIC
                JSR     PGET
                CMP     #4
                BCS     @no
                TAX
                LDA     MCTL,X
                STA     RB
                LDX     #3
                CMP     #SOUND_MUSIC_THRESH
                BNE     @run
                JSR     PGET
                STA     RB + 3
                LDX     #4
@run:           STZ     RMODE
                JMP     RUN
@no:            JMP     RETFF
MCTL:           .byte   SOUND_MUSIC_STOP, SOUND_MUSIC_PAUSE, SOUND_MUSIC_RESUME, SOUND_MUSIC_THRESH

; +1-2 the pitch in Hz, +3-4 the length in ms: BEEP on the card, and back
; when it has ended (every tone sent has, by BEEP_DONE), interrupts on; its
; length and 100 ms at most.
SN_BEEPX:       BLOCK
                CARD
                LDX     #3
@p:             JSR     PGET
                STA     RB,X
                INX
                CPX     #7
                BNE     @p
                STZ     RB + 7         ; the volume: the card's
                LDA     #SOUND_BEEP
                STA     RB
                STZ     RMODE
                PHP
                SEI
                INC     TONES          ; (counted before its BEEP_DONE can come)
                PLP
                LDX     #8
                JSR     RUN
                CMP     #0
                BNE     @x             ; refused or busy: no tone
                CLC
                LDA     RB + 5
                ADC     #<WAIT_MS
                PHA
                LDA     RB + 6
                ADC     #>WAIT_MS
                TAX
                PLA
                JSR     WSTART
                BCC     @w
                INC     WMS + 2
@w:             LDA     TONES
                BEQ     @end
                JSR     WSTEP
                BCC     @w
                STZ     TONES          ; one lost: none counted
@end:           JSR     WEND
                JMP     SN00
@x:             PHA
                PHP
                SEI
                LDA     TONES
                BEQ     @x2
                DEC     TONES
@x2:            PLP
                PLA
                RTS

; In block 60 (BDOS: W1 in every BDOS call, so SNTAB's jump reaches it as
; it reaches block 61's): rarely called operations, never the interrupt's or
; the stream's (the build plan's risk 1: block 61's room).
.segment "BDOS"

; +1 a band (0-10) and +2 its gain: EQ; +1 $FF and +2-12: EQ_ALL; +1 $FE
; and +2 off (0) or on: EQ_ON.
SN_EQX:         BLOCK
                CARD
                JSR     PGET
                STA     RB + 3
                CMP     #$FE
                BCC     @one
                BEQ     @on
                LDX     #3             ; EQ_ALL: +2-12
@all:           JSR     PGET
                STA     RB,X
                INX
                CPX     #14
                BNE     @all
                LDA     #SOUND_EQ_ALL
                BRA     @run
@on:            JSR     PGET
                CMP     #1             ; on: any but 0
                LDA     #0
                ROL     A              ; (carry: A was 1 or more)
                STA     RB + 3
                LDA     #SOUND_EQ_ON
                LDX     #4
                BRA     @run
@one:           JSR     PGET
                STA     RB + 4
                LDA     #SOUND_EQ
                LDX     #5
@run:           STA     RB
                STZ     RMODE
                JMP     RUN
