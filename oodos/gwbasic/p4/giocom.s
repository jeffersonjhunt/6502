; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOCOM.ASM's communications device, translated, in block P4 (hw/oodos.md,
; "The plan, phase 7": 7c, the devices as files): a file OPENed on "COM1:".
;
;   COMDSP          its functions, by GIO86U's G_* (TBLDSP's table)
;   COMINI          its start (GIOINI's): no file open, no width
;   COMOPN PCOMOP   OPEN "COM1:[speed][,parity][,data][,stop][,RS][,CS[n]]
;   VALGET GETPRM   [,DS[n]][,CD[n]][,LF][,BIN|,ASC]": the options parsed
;   SYNPRM LPTRYS   into the DCB, each wrong one Bad file name, then INICOM's
;                   check; one file at a time (File already open)
;   COMEOF COMLOC   EOF, LOC (the bytes waiting), LOF (the room for more)
;   COMLOF GCOMSZ
;   COMCLS          CLOSE: a ^Z sent for an ASCII output file
;   COMSWD COMGPS   its width, set and read; its column
;   COMGWD
;   COMRND          GET and PUT: a record's bytes in and out
;   COMSIN          a byte in, waited for (POLKEY, so ^C breaks); ^Z is an
;                   ASCII file's end
;   COMSOT CMOUT1   a byte out: CRIFEL's CR LF at the width; LF eaten after
;   CMROUT          a CR, and sent after one with the LF option
;   CKCMER          an OEM routine's error made BASIC's
;
; OO-1 (hw/oodos.md, decision 12): COM1 is AUX:, **polled**, through the OEM's
; INICOM, SNDCOM, RECCOM, STACOM and TRMCOM (p4/oem.s): output by function
; 4, input by function 3 when function 7 says a byte waits. There is no
; receive buffer: a byte that comes while BASIC is busy elsewhere, and isn't
; read before the next, is lost (TTY holds one); so LOC is 0 or 1 (and the
; byte backed up) and LOF the room in that one place. The speed, parity,
; data and stop bits are parsed, checked and kept in the DCB as the source
; does, but not set: OODOS's DEVINI ignores the device table's speed and the
; line stays 19,200, 8-N-1 (the owner, 2026-11-14): a program asking for 300
; gets 19,200. The modem lines' options (RS, CS, DS, CD) are kept and have no
; effect: TTY has no modem lines to wait on. COM2 has no driver (DVPTR's 0:
; Device unavailable), so GCMDCB's DCB is always COM1's (CM1DCB). The BDOS's
; AUX: functions give no error, so CKCMER finds none. ON COM(n)'s trap from a
; byte waiting (POLCOM, CHKINT's) is 7f's.

.include "gwbasic.inc"

.export COMDSP, COMINI
.import DERBFM, DERIFN, DERFAO, DERBRN, INIFDB, SCDBIN, DEVBIN, DEVBOT, FILOPT
.import CRIFEL, UPDPOS, CRDL, CRDH, BXVEC, INCHSI, BCHRSI, GETDAT, FILMOD
.import MAKINT, POLKEY, ERROR
.import INICOM, RECCOM, SNDCOM, STACOM, TRMCOM

; The DCB's _CMFLG bits.
CMBIN           = $01                  ; binary (no ASC option)
CMRTS           = $02                  ; RS: Request To Send suppressed
CMCOD           = $10                  ; ASC or BIN given
CMCTS           = $20                  ; CS given
CMCLF           = $40                  ; LF: a line feed after a CR
CMCRF           = $80                  ; the last byte out a CR
ASCCTZ          = 26                   ; ^Z
ERRCBO          = 69                   ; GWDATA.ASM's Communication buffer overflow
ERRDTO          = 24                   ; Device Timeout
ERRDIO          = 57                   ; Device I/O Error (ERRDPE, a parity error)

.rodata

; COMDSP: the communications device's functions, by G_*.
COMDSP:         .word   COMEOF         ; G_EOF
                .word   COMLOC         ; G_LOC
                .word   COMLOF         ; G_LOF
                .word   COMCLS         ; G_CLS
                .word   COMSWD         ; G_SWD
                .word   COMRND         ; G_RND
                .word   COMOPN         ; G_OPN
                .word   COMSIN         ; G_SIN
                .word   COMSOT         ; G_SOT
                .word   COMGPS         ; G_GPS
                .word   COMGWD         ; G_GWD
                .word   COMSCW         ; G_SCW
                .word   COMGCW         ; G_GCW
                .word   DEVBIN         ; G_BIN
                .word   DEVBOT         ; G_BOT

; CMERRT (178): the OEM's error codes 1-5 as BASIC's.
CMERRT:         .byte   ERRCBO, ERRDIO, ERRDTO, ERRDTO, ERRDTO

.code

; COMINI (GIOCOM.ASM 108): at start-up: no width (255), no file open.
COMINI:         LDA     #255
                STA     CMWID
                STZ     CMFDB
                STZ     CMFDB+1
; COMTRM, COMSCW, COMGCW: nothing to do.
COMSCW:
COMGCW:
RET14:          RTS

; GCOMSZ (160): STACOM for the unit: DX the bytes waiting, CX the room.
GCOMSZ:         LDX     CMDEV
                JSR     STACOM
                CMP     #0
                BNE     CKCMER
                RTS

; CKCMER (172): A an OEM routine's error, 0 none: 1-5 by CMERRT, any other
; Device I/O Error.
CKCMER:         CMP     #0
                BEQ     RET14
                LDX     #ERRDIO
                CMP     #5 + 1
                BCS     ERROR1
                TAX
                LDA     CMERRT - 1,X
                TAX
ERROR1:         TXA
                JMP     ERROR

; --- COM OPEN (GIOCOM.ASM 270) ------------------------------------------------------------

; COMOPN: A the device id, OPNNUM the file, FILMOD its mode (any), CX a
; random file's length (0: 128, as the disk's), FILOPT the options. One file
; at a time on the device (File already open). The options into the DCB
; (PCOMOP); RS with no CS makes CTS's timeout 0; the stop bits not given are
; 1, or for 110 baud or less 1.5 with 5 data bits (or 4) and 2 with more.
; Then INICOM ($FF: Bad file name; else its error, CKCMER); the FDB (INIFDB:
; the DCB's width and column), FD_SIZ a random file's length, and the DCB's
; FDB. Binary unless ASC was given (SCDBIN).
COMOPN:         STA     CMAL           ; PUSH AX: the device id
                LDA     RCL            ; PUSH CX: the length
                STA     CMCX
                LDA     RCH
                STA     CMCX+1
                LDA     CMFDB          ; GCMDCB: COM1's (OO-1)
                ORA     CMFDB+1
                BEQ     STUNIT         ; not open to another file
                JMP     DERFAO
STUNIT:         STZ     CMDEV          ; _DEVID: the unit, 0
                JSR     PCOMOP
                LDA     CMFLG
                AND     #CMRTS
                BEQ     NORTSO         ; no RS
                LDA     CMFLG
                AND     #CMCTS
                BNE     NORTSO         ; CS given
                STZ     CMCTSTO        ; CTS's timeout 0
                STZ     CMCTSTO+1
NORTSO:         LDA     CMSTOPB
                CMP     #255
                BNE     STPNDF         ; given
                INC     CMSTOPB        ; 0: 1 stop bit
                LDA     CMBAUD+1       ; above 110 baud: 1
                BNE     STPNDF
                LDA     #110
                CMP     CMBAUD
                BCC     STPNDF
                INC     CMSTOPB        ; 1: 1.5
                LDA     CMBYTSZ
                CMP     #5 + 1
                BCC     STPNDF         ; 4 or 5 data bits
                INC     CMSTOPB        ; 2
STPNDF:         STZ     CMPOS          ; the column 0
                JSR     INICOM         ; A: $FF Bad file name, else an error
                CMP     #$FF
                BEQ     ERIFN1
                JSR     CKCMER
                LDA     FILMOD
                CMP     #MD_RND
                BNE     NOTRND
                LDA     CMCX           ; a length asked for?
                ORA     CMCX+1
                BNE     NOTRND
                LDA     #DATPSC        ; else 128, as the disk's
                STA     CMCX
NOTRND:         LDA     CMCX           ; CX: the FIELD buffer (OO-1: FALLOC's;
                STA     RCL            ; the 8086 adds the FDB's own part)
                LDA     CMCX+1
                STA     RCH
                LDA     CMWID          ; DL, DH: the DCB's width and column
                STA     RDL
                LDA     CMPOS
                STA     RDH
                LDX     #$FF           ; every mode
                LDA     CMAL
                JSR     INIFDB
                LDA     CMCX           ; a random file: its length (FALLOC's too)
                ORA     CMCX+1
                BEQ     NTRND
                LDY     #FD_SIZ
                LDA     CMCX
                STA     (FDBP),Y
                INY
                LDA     CMCX+1
                STA     (FDBP),Y
NTRND:          LDA     FDBP           ; the DCB's FDB
                STA     CMFDB
                LDA     FDBP+1
                STA     CMFDB+1
                LDA     CMFLG
                AND     #CMBIN
                BEQ     CMOPNX         ; ASCII
                JMP     SCDBIN
CMOPNX:         RTS
ERIFN1:         JMP     DERIFN

; PCOMOP (359): FILOPT's options into the DCB, X its place (the 8086's SI).
; The defaults: 300 baud, the stop bits not given (255), 7 data bits, even
; parity, binary, the timeouts CD 0, CTS and DSR 1,000 ms. Speed, parity
; (N, O, E, M or S), data bits (4-8) and stop bits (1, 1.5 or 2) by place,
; each may be left out with its comma; then RS, CS[n], DS[n], CD[n], LF, BIN
; and ASC in any order (not BIN and ASC both). Anything else is Bad file name.
PCOMOP:         LDA     #<300
                STA     CMBAUD
                LDA     #>300
                STA     CMBAUD+1
                LDA     #255
                STA     CMSTOPB
                LDA     #7
                STA     CMBYTSZ
                LDA     #2
                STA     CMPARIT
                LDA     #CMBIN
                STA     CMFLG
                STZ     CMRLSTO
                STZ     CMRLSTO+1
                LDA     #<1000
                STA     CMCTSTO
                STA     CMDSRTO
                LDA     #>1000
                STA     CMCTSTO+1
                STA     CMDSRTO+1
                LDX     #0             ; SI: FILOPT
                JSR     GETPR0         ; the first
                BEQ     PCOMX          ; none
                BCS     COMOP3         ; ",": the speed left out
                DEX                    ; for GETPRM
                JSR     VALGET         ; DX: the speed
                LDA     RDL
                STA     CMBAUD
                LDA     RDH
                STA     CMBAUD+1
                JSR     GETPRM
                BNE     COMOP3
PCOMX:          RTS
COMOP3:         JSR     SYNPRM         ; "," must follow
                LDY     #2             ; CH: even, the default
                BCS     DFTPTY         ; ",": left out
                LDY     #0
                CMP     #'N'
                BEQ     GOTPTY
                INY
                CMP     #'O'
                BEQ     GOTPTY
                INY
                CMP     #'E'
                BEQ     GOTPTY
                INY
                CMP     #'M'
                BEQ     GOTPTY
                INY
                CMP     #'S'
                BEQ     GOTPTY
ERIFN2:         JMP     DERIFN
DFTPTY:         DEX                    ; for GETPRM
GOTPTY:         STY     CMPARIT
                JSR     GETPRM         ; past the parity
                BEQ     COMOP5         ; the end
                JSR     SYNPRM         ; the data bits
                BCS     COMOP4         ; ",": left out
                SEC
                SBC     #'0'
                CMP     #4
                BCC     ERIFN2
                CMP     #9
                BCS     ERIFN2
                STA     CMBYTSZ
                JSR     GETPRM
                BEQ     COMOP5
COMOP4:         JSR     SYNPRM         ; the stop bits
                BCS     COMOP5         ; ",": left out
                SEC
                SBC     #'1'
                CMP     #2
                BCS     ERIFN2         ; "1" or "2"
                ASL     A              ; 0 or 2
                STA     CMSTOPB
                BNE     LPARM          ; "2"
                JSR     GETPRM
                BEQ     PCOMX2
                CMP     #'.'
                BNE     LPARM2         ; a comma, it had better be
                JSR     GETPRM
                CMP     #'5'
                BNE     ERIFN2         ; only 1.5
                INC     CMSTOPB        ; 1
                INX
COMOP5:         DEX                    ; for GETPRM
                BRA     LPARM
PCOMX2:         RTS
; LPARM (437): the options in any order.
LPARM:          JSR     GETPRM
                BEQ     RET24          ; no more
LPARM2:         JSR     SYNPRM         ; "," and the option
                CMP     #'R'
                BNE     LPARDS
                JSR     LPTRYS         ; RS
                BNE     PARIFN
                LDA     CMFLG
                ORA     #CMRTS
                STA     CMFLG
                BRA     LPARM
LPARDS:         CMP     #'D'
                BNE     LPARCD
                JSR     LPTRYS         ; DS
                BNE     PARIFN
                JSR     VALGET
                LDA     RDL
                STA     CMDSRTO
                LDA     RDH
                STA     CMDSRTO+1
                BRA     LPARM
LPARCD:         CMP     #'C'
                BNE     LPARLF
                JSR     LPTRYS
                BEQ     LPARCS         ; CS
                CMP     #'D'
                BNE     PARIFN
                JSR     VALGET         ; CD
                LDA     RDL
                STA     CMRLSTO
                LDA     RDH
                STA     CMRLSTO+1
                BRA     LPARM
LPARLF:         CMP     #'L'
                BNE     LPRBIN
                JSR     LPTRYS
                CMP     #'F'
                BNE     PARIFN
                LDA     CMFLG          ; LF
                ORA     #CMCLF
                STA     CMFLG
                BRA     LPARM
LPARCS:         JSR     VALGET
                LDA     RDL
                STA     CMCTSTO
                LDA     RDH
                STA     CMCTSTO+1
                LDA     CMFLG
                ORA     #CMCTS
                STA     CMFLG
                BRA     LPARM
PARIFN:         JMP     DERIFN

; LPTRYS (496): the next, and Z if it is "S".
LPTRYS:         JSR     GETPRM
                CMP     #'S'
RET21:
RET24:          RTS

LPRBIN:         CMP     #'B'
                BNE     LPRASC
                JSR     LPTRYS
                CMP     #'I'
                BNE     PARIFN
                JSR     LPTRYS
                CMP     #'N'
                BNE     PARIFN
                BRA     TSTCOD         ; binary, the default
LPRASC:         CMP     #'A'
                BNE     PARIFN
                JSR     LPTRYS
                CMP     #'S'
                BNE     PARIFN
                JSR     LPTRYS
                CMP     #'C'
                BNE     PARIFN
                LDA     CMFLG          ; ASCII
                AND     #$FF - CMBIN
                STA     CMFLG
TSTCOD:         LDA     CMFLG
                AND     #CMCOD
                BNE     PARIFN         ; both ASC and BIN
                LDA     CMFLG
                ORA     #CMCOD
                STA     CMFLG
                JMP     LPARM

; SYNPRM (527): a "," here, then the next (GETPRM); else Bad file name.
SYNPRM:         LDA     FILOPT,X
                CMP     #','
                BNE     PARIFN
; GETPRM (533): the next, unless at the end. GETPRI: the next; GETPR0: this
; one. Blanks skipped, upper case. A comma: C set, Z clear; the end: Z set,
; C clear (X at the 0); else the byte, Z and C clear.
GETPRM:         LDA     FILOPT,X
                BEQ     GETPRX         ; the end
GETPRI:         INX
GETPR0:         LDA     FILOPT,X
                CMP     #' '
                BEQ     GETPRM         ; a blank
                CMP     #','
                BNE     GETPR1
                ORA     #0             ; NZ
                SEC                    ; and C: a comma
                RTS
GETPR1:         CMP     #'a'
                BCC     GETPR2
                EOR     #$20           ; upper case
GETPR2:         ORA     #0             ; Z: the end
GETPRX:         CLC
                RTS

; VALGET (550): a decimal number in DX, of at most 5 digits (more: Bad file
; name), in 16 bits as the 8086's (no check past 65535); X back at its last
; digit, for GETPRM.
VALGET:         STZ     RDL
                STZ     RDH
                LDA     #6
                STA     VALAH          ; AH: the digits allowed, + 1
VALLOP:         JSR     GETPRI
                CMP     #'9' + 1
                BCS     VALXIT
                CMP     #'0'
                BCC     VALXIT
                PHA
                LDA     RDL            ; BX = DX * 10
                STA     RBL
                LDA     RDH
                STA     RBH
                ASL     RBL            ; * 2
                ROL     RBH
                ASL     RBL            ; * 4
                ROL     RBH
                CLC                    ; * 5
                LDA     RBL
                ADC     RDL
                STA     RBL
                LDA     RBH
                ADC     RDH
                STA     RBH
                ASL     RBL            ; * 10
                ROL     RBH
                PLA
                SEC                    ; + the digit
                SBC     #'0'
                CLC
                ADC     RBL
                STA     RDL
                LDA     RBH
                ADC     #0
                STA     RDH
                DEC     VALAH
                BNE     VALLOP
                JMP     DERIFN         ; too many digits
VALXIT:         DEX                    ; for GETPRM
                RTS

; --- COMEOF, COMLOC, COMLOF (GIOCOM.ASM 569) ----------------------------------------------

; COMEOF: BX -1 at the end, else 0. ASCII: the next byte read (waited for),
; ^Z the end, else backed up. Binary (IBM's): the end when no byte waits.
COMEOF:         LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                BEQ     BINEOF
                STZ     RBL            ; assume not
                STZ     RBH
                JSR     INCHSI
                BCS     YCMEOF         ; ^Z
                JSR     BCHRSI         ; back
CMEOFX:         RTS
BINEOF:         JSR     COMLOC         ; BX: the bytes waiting
                LDA     RBL
                ORA     RBH
                BEQ     YCMEOF
                LDA     #1             ; (0: not the end)
                STA     RBL
                STZ     RBH
YCMEOF:         LDA     RBL            ; BX - 1
                BNE     :+
                DEC     RBH
:               DEC     RBL
                RTS

; COMLOC (604): BX the bytes waiting (GCOMSZ), one more if one is backed up.
COMLOC:         JSR     GCOMSZ
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                LDY     #F_FLGS
                LDA     (FDBP),Y
                AND     #FL_BKC
                BEQ     CMLOCX
                INC     RBL
                BNE     CMLOCX
                INC     RBH
CMLOCX:         RTS

; COMLOF (618): the room for bytes (CX), an integer in the FAC.
COMLOF:         JSR     COMLOC
                LDA     RCL
                STA     RBL
                LDA     RCH
                STA     RBH
                JMP     MAKINT

; --- COMCLS, COMSWD, COMRND (GIOCOM.ASM 631) ----------------------------------------------

; COMCLS: the device free again; an ASCII file not for input gets a ^Z (its
; error not looked at); then TRMCOM and its error.
COMCLS:         STZ     CMFDB
                STZ     CMFDB+1
                LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                BEQ     COMCLX
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQI
                BEQ     COMCLX
                LDA     #ASCCTZ
                LDX     CMDEV
                JSR     SNDCOM
COMCLX:         LDX     CMDEV
                JSR     TRMCOM
                JMP     CKCMER

; COMSWD (657): the width, DL.
COMSWD:         LDA     RDL
                STA     CMWID
                RTS

; COMRND (680): A 0 GET, 1 PUT (the record's length, FD_SIZ, of bytes), 2
; GET, 3 PUT DX bytes; between the port and the FIELD buffer. OO-1: a count
; of 0 or past the record's length is Bad record number: the 8086's LOOP
; with CX 0 takes 65,536 bytes, and any count past FD_SIZ runs past the
; FIELD buffer, into the strings.
COMRND:         CMP     #2
                BCS     CMRND1         ; a count given
                PHA
                LDY     #FD_SIZ        ; the record's length
                LDA     (FDBP),Y
                STA     RDL
                INY
                LDA     (FDBP),Y
                STA     RDH
                PLA
CMRND1:         PHA
                LDA     RDL            ; OO-1: 1 to FD_SIZ
                ORA     RDH
                BEQ     ERBRN
                LDY     #FD_SIZ
                LDA     (FDBP),Y
                CMP     RDL
                INY
                LDA     (FDBP),Y
                SBC     RDH
                BCC     ERBRN
                LDA     RDL            ; CX: the count
                STA     RCL
                LDA     RDH
                STA     RCH
                JSR     GETDAT         ; BX: the FIELD buffer
                STA     RBL
                STX     RBH
                PLA
                LSR     A
                BCS     PUTLOP         ; PUT
GETLOP:         JSR     COMSIN         ; (an ASCII ^Z is a byte here)
                STA     (RBL)
                JSR     RNDNXT
                BNE     GETLOP
                RTS
PUTLOP:         LDA     (RBL)
                JSR     CMROUT
                JSR     RNDNXT
                BNE     PUTLOP
                RTS
ERBRN:          JMP     DERBRN
; (The project's own: BX on, CX less one, Z at 0: the 8086's INC BX, LOOP.)
RNDNXT:         INC     RBL
                BNE     :+
                INC     RBH
:               LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     RCL
                ORA     RCH
                RTS

; --- COMSIN, COMSOT (GIOCOM.ASM 718) ------------------------------------------------------

; COMSIN: the next byte in A, waited for: RECCOM, and while none waits the
; keys polled (POLKEY: ^C breaks). ^Z is an ASCII file's end: C set; else C
; clear. (RECCOM's error: CKCMER's, never on the OO-1.)
COMSIN:
CSWAIT:         LDX     CMDEV
                JSR     RECCOM         ; C: a byte in A
                BCS     CSGOT1
                JSR     POLKEY         ; let ^C in
                BRA     CSWAIT
CSGOT1:         CMP     #ASCCTZ
                BNE     CMNEOF
                LDY     #F_CODE
                LDA     (FDBP),Y
                EOR     #FC_BIN
                BEQ     CMNEOZ         ; binary: a ^Z is a byte
                LDA     #ASCCTZ
                SEC                    ; ASCII: the end
                RTS
CMNEOZ:         LDA     #ASCCTZ
CMNEOF:         CLC
                RTS

; COMSOT (772): A out. Binary, if BIN was given: as it is (CMROUT). Else
; CRIFEL, with the FDB's width (DL) and the DCB's column (DH), through
; CMOUT1, the column back in the DCB.
COMSOT:         PHA
                LDA     CMFLG
                AND     #CMCOD
                BEQ     NOTBIN         ; neither ASC nor BIN given
                LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                BNE     NOTBIN
                PLA
                JMP     CMROUT         ; BIN: raw
NOTBIN:         LDY     #F_WID
                LDA     (FDBP),Y
                STA     CRDL
                LDA     CMPOS
                STA     CRDH
                LDA     #<CMOUT1
                STA     BXVEC
                LDA     #>CMOUT1
                STA     BXVEC+1
                PLA
                JSR     CRIFEL
                LDA     CRDH
                STA     CMPOS
                RTS

; CMOUT1 (812): A out, the column (DH) moved first (UPDPOS): an LF straight
; after a CR eaten; after a CR an LF if the LF option was given. A kept.
CMOUT1:         JSR     UPDPOS
                PHA
                CMP     #10
                BNE     CMOUT2
                BIT     CMFLG          ; CMCRF: the last out a CR
                BMI     CMOUT3
CMOUT2:         JSR     CMROUT
CMOUT3:         LDA     CMFLG
                AND     #$FF - CMCRF
                STA     CMFLG
                PLA
                CMP     #13
                BNE     CMOUTX
                LDA     CMFLG
                ORA     #CMCRF
                STA     CMFLG
                AND     #CMCLF
                BEQ     CMOUTC         ; no LF after it
                LDA     #10
                JSR     CMROUT
CMOUTC:         LDA     #13
CMOUTX:         RTS

; CMROUT (838): A to the port (SNDCOM), its error to CKCMER. A kept.
CMROUT:         PHA
                LDX     CMDEV
                JSR     SNDCOM         ; A: its error
                JSR     CKCMER
                PLA
                RTS

; COMGPS (849): A the column. COMGWD (858): A the width.
COMGPS:         LDA     CMPOS
                RTS
COMGWD:         LDA     CMWID
                RTS

.segment "BRAM"
; CM1DCB (GWRAM.ASM): COM1's device control block, the source's fields in
; its order (OO-1: COM1's alone, 17 of CDCBSZ's 24 bytes).
CMDEV:          .res    1              ; _DEVID: the unit, 0
CMBAUD:         .res    2              ; _BAUDR: the speed asked for (OO-1: kept, not set)
CMBYTSZ:        .res    1              ; _BYTSZ: the data bits, 4-8
CMPARIT:        .res    1              ; _PARIT: 0-4, none, odd, even, mark, space
CMSTOPB:        .res    1              ; _STOPB: 0-2, 1, 1.5, 2 stop bits
CMRLSTO:        .res    2              ; _RLSTO: CD's timeout (ms)
CMCTSTO:        .res    2              ; _CTSTO: CTS's
CMDSRTO:        .res    2              ; _DSRTO: DSR's
CMFLG:          .res    1              ; _CMFLG: CMBIN to CMCRF
CMWID:          .res    1              ; _CMWID: the width
CMPOS:          .res    1              ; _CMPOS: the column
CMFDB:          .res    2              ; _CMFDB: the file's FDB, 0 none
; What the 8086 keeps on its stack and in AH.
CMAL:           .res    1              ; COMOPN's device id
CMCX:           .res    2              ; and length
VALAH:          .res    1              ; VALGET's digits allowed
.export CMBAUD, CMBYTSZ, CMPARIT, CMSTOPB
