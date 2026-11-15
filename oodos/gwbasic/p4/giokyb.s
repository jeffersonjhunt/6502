; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOKYB.ASM's KYBD: device, translated, in block P4 (hw/oodos.md, "The plan,
; phase 7": 7c, the devices as files): a file OPENed on "KYBD:" for input,
; read from the keys typed.
;
;   KYBDSP          its functions, by GIO86U's G_* (TBLDSP's table): its
;                   output, column and width are the screen's (p4/gioscn.s)
;   KYBEOF          EOF: the next key read (waited for); ^Z the end
;   KYBLOC KYBLOF   LOC: the keys waiting; LOF: the room in the queue
;   KYBOPN          OPEN: input only (a random OPEN made input)
;   KYBSIN          the next key; ^Z is the end
;
; OO-1: the keys are the OEM's queue (res/oem.s's KYBQ, CHSNS's: POLKEY
; fills it, ^C breaks); KYBSIN reads them by INCHRI (the OEM's: CHSNS until a
; key comes), where the 8086's KEYIN works the screen editor's cursor (7d).
; The two-byte keys are 7d's. KYBINI is the OEM's (res/oem.s).

.include "gwbasic.inc"

.export KYBDSP
.import DERBFM, INIFDB, DEVBIN, DEVBOT, INCHSI, BCHRSI, INCHRI, FILMOD, MAKINT
.import SCNSWD, SCNSOT, SCNGPS, SCNGWD, SCNSCW, SCNGCW, KYBQIN, KYBQOUT

ASCCTZ          = 26

.rodata

; KYBDSP: the keyboard's functions, by G_*.
KYBDSP:         .word   KYBEOF         ; G_EOF
                .word   KYBLOC         ; G_LOC
                .word   KYBLOF         ; G_LOF
                .word   KYBCLS         ; G_CLS
                .word   SCNSWD         ; G_SWD
                .word   DERBFM         ; G_RND
                .word   KYBOPN         ; G_OPN
                .word   KYBSIN         ; G_SIN
                .word   SCNSOT         ; G_SOT
                .word   SCNGPS         ; G_GPS
                .word   SCNGWD         ; G_GWD
                .word   SCNSCW         ; G_SCW
                .word   SCNGCW         ; G_GCW
                .word   DEVBIN         ; G_BIN
                .word   DEVBOT         ; G_BOT

.code

; KYBEOF (GIOKYB.ASM 138): BX -1 at the end, else 0: the next key read, ^Z
; the end, else backed up (SI is always a file's here).
KYBEOF:         STZ     RBL
                STZ     RBH
                JSR     INCHSI
                BCS     YKYEOF         ; ^Z
                JSR     BCHRSI
                RTS
YKYEOF:         DEC     RBL            ; -1
                DEC     RBH
KYBCLS:         RTS                    ; KYBCLS (179), KYBTRM

; KYBLOC (152): BX the keys waiting (NUMQ), one more if one is backed up.
KYBLOC:         JSR     NUMQ
                STA     RBL
                STZ     RBH
                LDY     #F_FLGS
                LDA     (FDBP),Y
                AND     #FL_BKC
                BEQ     KYLOCX
                INC     RBL
KYLOCX:         RTS

; KYBLOF (167): the room left in the queue (LFTQ: its size less one, less
; what waits), an integer in the FAC.
KYBLOF:         JSR     NUMQ
                EOR     #$FF           ; KYBQLN - 1 - NUMQ
                SEC
                ADC     #KYBQLN - 1
                STA     RBL
                STZ     RBH
                JMP     MAKINT

; NUMQ (GIO86.ASM 2036): A the keys in the queue.
NUMQ:           LDA     KYBQIN
                SEC
                SBC     KYBQOUT
                AND     #KYBQLN - 1
                RTS

; KYBOPN (203): A the device id: input only, and a random OPEN made input.
; OO-1: the width 255 and the column 0 (the 8086 passes on whatever DX the
; OPEN left; a keyboard file is never written).
KYBOPN:         LDX     FILMOD
                CPX     #MD_RND
                BNE     KYBOPX
                LDX     #MD_SQI
                STX     FILMOD
KYBOPX:         LDX     #255
                STX     RDL
                STZ     RDH
                LDX     #MD_SQI
                JMP     INIFDB

; KYBSIN (243): the next key in A (INCHRI: waited for); ^Z is the end, C set;
; else C clear.
KYBSIN:         JSR     INCHRI
                CMP     #ASCCTZ
                BNE     KB1X
                SEC
                RTS
KB1X:           CLC
                RTS
