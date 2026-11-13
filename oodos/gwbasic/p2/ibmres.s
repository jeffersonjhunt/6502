; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; IBMRES.ASM's reserved words, in block P2: the tables CRUNCH searches and
; LIST walks, and the code for the extended words ($FE statements, $FD
; functions), CRUNCX and LISTX. The tables are the project's own text, made
; with Q and QF below in IBMRES.ASM's order, which matters: the first word
; that matches is taken (DEFSTR before DEF, INPUT before INP).
;
; A word is in its first letter's table (ALPTAB), without that letter: its
; other characters, the last with bit 7 set, then its token; QF's is a
; function's, less 128 (CRUNCH puts $FF before it, and sets bit 7). Each
; table ends with a zero; SPCTAB, the special characters, follows the last
; (LIST's walk goes on into it, the letter after Z its "first letter").
; "GO TO" and "SPC(" and "TAB(" are words as they are.

.include "gwbasic.inc"

.export ALPTAB, RESLST, SPCTAB, LINRES, CRUNCX, LISTX, RESLSX
.import MAKUPL, TSTANM, NOTRFN, RENCRN

; Q "WORD", TOKEN: a statement's or an operator's.
.macro Q w, t
        .repeat .strlen(w) - 2, i
                .byte   .strat(w, i + 1)
        .endrepeat
                .byte   .strat(w, .strlen(w) - 1) | $80
                .byte   t
.endmacro
; QF "WORD", TOKEN: a function's.
.macro QF w, t
        .repeat .strlen(w) - 2, i
                .byte   .strat(w, i + 1)
        .endrepeat
                .byte   .strat(w, .strlen(w) - 1) | $80
                .byte   t - 128
.endmacro

.code

; CRUNX (IBMRES.ASM 741): CRUNCH's NOTRES, the word not in RESLST: tried in
; RESLSX, from its first letter (KSRC is past it). A match is $FE and the
; statement's token, or $FD and the function's, through RENCRN; none is a
; name (NOTRFN).
CRUNCX:         LDA     KSRC
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     MAKUPL
                SEC
                SBC     #'A'
                ASL     A
                TAX
                LDA     ALPTAX,X
                STA     KRP
                LDA     ALPTAX+1,X
                STA     KRP+1
                INCW    TXTPTR
TRYAGX:         LDA     TXTPTR
                STA     KSRC
                LDA     TXTPTR+1
                STA     KSRC+1
LOPPSX:         JSR     MAKUPL
                STA     KCH
                LDA     (KRP)
                AND     #$7F
                BNE     :+
                JMP     NOTRFN         ; the end of this letter's words
:               INCW    TXTPTR
                CMP     KCH
                BNE     LOPSKX
                LDA     (KRP)
                INCW    KRP
                ORA     #0
                BPL     LOPPSX         ; more of the word
                JSR     MAKUPL         ; a letter or digit after it: a name
                CMP     #'.'
                BNE     :+
                JMP     NOTRFN
:               JSR     TSTANM
                BCS     :+
                JMP     NOTRFN
:               LDA     (KRP)          ; the token
                BMI     @st
                ORA     #$80          ; a function: $FD
                PHA
                LDA     #$FD
                JMP     RENCRN
@st:            PHA                    ; a statement: $FE
                LDA     #$FE
                JMP     RENCRN
LOPSKX:         LDA     KSRC           ; the next word
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
@sk:            LDA     (KRP)
                INCW    KRP
                ORA     #0
                BPL     @sk
                INCW    KRP            ; past the token
                BRA     TRYAGX

; LISTX (IBMRES.ASM 805): BUFLIN's hook, A the token just passed and TXTPTR
; at the byte after it. $FD: a function, that byte less bit 7; $FE: a
; statement, that byte. C set for one of them, with the token to find in
; KTRM and RESLSX less one in A/X, for BUFLIN to walk (the 8086 walks it
; here, with its own copy of BUFLIN's code); otherwise C clear, A kept.
LISTX:          CMP     #$FD
                BEQ     NEWFUN
                CMP     #$FE
                BEQ     @st
                CLC
                RTS
@st:            LDA     (TXTPTR)
                BRA     BUFCON
NEWFUN:         LDA     (TXTPTR)
                AND     #$7F
BUFCON:         STA     KTRM
                LDA     #<(RESLSX-1)
                LDX     #>(RESLSX-1)
                SEC
                RTS

.rodata

; ALPTAB (IBMRES.ASM 298): each letter's table.
ALPTAB:         .word   ATAB, BTAB, CTAB, DTAB, ETAB, FTAB, GTAB, HTAB, ITAB
                .word   JTAB, KTAB, LTAB, MTAB, NTAB, OTAB, PTAB, QTAB, RTAB
                .word   STAB, TTAB, UTAB, VTAB, WTAB, XTAB, YTAB, ZTAB

; RESLST (IBMRES.ASM 327)
RESLST:
ATAB:
                Q       "AUTO", AUTO_TK
                Q       "AND", AND_TK
                QF      "ABS", ABS_FN
                QF      "ATN", ATN_FN
                QF      "ASC", ASC_FN
                .byte   0
BTAB:
                Q       "BSAVE", BSAVE_TK
                Q       "BLOAD", BLOAD_TK
                Q       "BEEP", BEEP_TK
                .byte   0
CTAB:
                Q       "COLOR", COLOR_TK
                Q       "CLOSE", CLOSE_TK
                Q       "CONT", CONT_TK
                Q       "CLEAR", CLEAR_TK
                Q       "CSRLIN", CSRLIN_TK
                QF      "CINT", CINT_FN
                QF      "CSNG", CSNG_FN
                QF      "CDBL", CDBL_FN
                QF      "COS", COS_FN
                QF      "CHR$", CHRS_FN
                Q       "CALL", CALL_TK
                Q       "CLS", CLS_TK
                .byte   0
DTAB:
                Q       "DELETE", DELETE_TK
                Q       "DATA", DATA_TK
                Q       "DIM", DIM_TK
                Q       "DEFSTR", DEFSTR_TK
                Q       "DEFINT", DEFINT_TK
                Q       "DEFSNG", DEFSNG_TK
                Q       "DEFDBL", DEFDBL_TK
                Q       "DEF", DEF_TK
                .byte   0
ETAB:
                Q       "ELSE", ELSE_TK
                Q       "END", END_TK
                Q       "ERASE", ERASE_TK
                Q       "EDIT", EDIT_TK
                Q       "ERROR", ERROR_TK
                Q       "ERL", ERL_TK
                Q       "ERR", ERR_TK
                QF      "EXP", EXP_FN
                QF      "EOF", EOF_FN
                Q       "EQV", EQV_TK
                .byte   0
FTAB:
                Q       "FOR", FOR_TK
                Q       "FN", FN_TK
                QF      "FRE", FRE_FN
                QF      "FIX", FIX_FN
                .byte   0
GTAB:
                Q       "GOTO", GOTO_TK
                Q       "GO TO", GOTO_TK
                Q       "GOSUB", GOSUB_TK
                .byte   0
HTAB:
                QF      "HEX$", HEXS_FN
                .byte   0
ITAB:
                Q       "INPUT", INPUT_TK
                Q       "IF", IF_TK
                Q       "INSTR", INSTR_TK
                QF      "INT", INT_FN
                QF      "INP", INP_FN
                Q       "IMP", IMP_TK
                Q       "INKEY$", INKEYS_TK
                .byte   0
JTAB:
                .byte   0
KTAB:
                Q       "KEY", KEY_TK
                .byte   0
LTAB:
                Q       "LOCATE", LOCATE_TK
                Q       "LPRINT", LPRINT_TK
                Q       "LLIST", LLIST_TK
                QF      "LPOS", LPOS_FN
                Q       "LET", LET_TK
                Q       "LINE", LINE_TK
                Q       "LOAD", LOAD_TK
                Q       "LIST", LIST_TK
                QF      "LOG", LOG_FN
                QF      "LOC", LOC_FN
                QF      "LEN", LEN_FN
                QF      "LEFT$", LEFTS_FN
                QF      "LOF", LOF_FN
                .byte   0
MTAB:
                Q       "MOTOR", MOTOR_TK
                Q       "MERGE", MERGE_TK
                Q       "MOD", MOD_TK
                QF      "MID$", MIDS_FN
                .byte   0
NTAB:
                Q       "NEXT", NEXT_TK
                Q       "NEW", NEW_TK
                Q       "NOT", NOT_TK
                .byte   0
OTAB:
                Q       "OPEN", OPEN_TK
                Q       "OUT", OUT_TK
                Q       "ON", ON_TK
                Q       "OR", OR_TK
                QF      "OCT$", OCTS_FN
                Q       "OPTION", OPTION_TK
                Q       "OFF", OFF_TK
                .byte   0
PTAB:
                Q       "PRINT", PRINT_TK
                Q       "POKE", POKE_TK
                QF      "POS", POS_FN
                QF      "PEEK", PEEK_FN
                Q       "PSET", PSET_TK
                Q       "PRESET", PRESET_TK
                Q       "POINT", POINT_TK
                QF      "PEN", PEN_FN
                .byte   0
QTAB:
                .byte   0
RTAB:
                Q       "RUN", RUN_TK
                Q       "RETURN", RETURN_TK
                Q       "READ", READ_TK
                Q       "RESTORE", RESTORE_TK
                Q       "REM", REM_TK
                Q       "RESUME", RESUME_TK
                QF      "RIGHT$", RIGHTS_FN
                QF      "RND", RND_FN
                Q       "RENUM", RENUM_TK
                Q       "RANDOMIZE", RANDOMIZE_TK
                .byte   0
STAB:
                Q       "SCREEN", SCREEN_TK
                Q       "STOP", STOP_TK
                Q       "SWAP", SWAP_TK
                Q       "SAVE", SAVE_TK
                Q       "SPC(", SPC_TK
                Q       "STEP", STEP_TK
                QF      "SGN", SGN_FN
                QF      "SQR", SQR_FN
                QF      "SIN", SIN_FN
                QF      "STR$", STRS_FN
                Q       "STRING$", STRINGS_TK
                QF      "SPACE$", SPACES_FN
                Q       "SOUND", SOUND_TK
                QF      "STICK", STICK_FN
                QF      "STRIG", STRIG_FN
                .byte   0
TTAB:
                Q       "THEN", THEN_TK
                Q       "TRON", TRON_TK
                Q       "TROFF", TROFF_TK
                Q       "TAB(", TAB_TK
                Q       "TO", TO_TK
                QF      "TAN", TAN_FN
                .byte   0
UTAB:
                Q       "USING", USING_TK
                Q       "USR", USR_TK
                .byte   0
VTAB:
                QF      "VAL", VAL_FN
                Q       "VARPTR", VARPTR_TK
                .byte   0
WTAB:
                Q       "WIDTH", WIDTH_TK
                Q       "WAIT", WAIT_TK
                Q       "WHILE", WHILE_TK
                Q       "WEND", WEND_TK
                Q       "WRITE", WRITE_TK
                .byte   0
XTAB:
                Q       "XOR", XOR_TK
                .byte   0
YTAB:
                .byte   0
ZTAB:
                .byte   0

; SPCTAB (IBMRES.ASM 520): each a character with bit 7 set, then its token.
SPCTAB:         .byte   '+' | $80, PLUSTK
                .byte   '-' | $80, MINUTK
                .byte   '*' | $80, MULTK
                .byte   '/' | $80, DIVTK
                .byte   '^' | $80, EXPTK
                .byte   $5C | $80, IDIVTK ; \
                .byte   $27 | $80, SNGQTK ; '
                .byte   '>' | $80, GREATK
                .byte   '=' | $80, EQULTK
                .byte   '<' | $80, LESSTK
                .byte   0

; LINRES (GWMAIN.ASM 1189): the words a line number may follow, so a
; number after them is crunched as one (LINCON), not as a constant.
LINRES:         .byte   RESTORE_TK, AUTO_TK, RENUM_TK, DELETE_TK, EDIT_TK
                .byte   RESUME_TK, ERL_TK, ELSE_TK, RUN_TK, LIST_TK, LLIST_TK
                .byte   GOTO_TK, RETURN_TK, THEN_TK, GOSUB_TK, 0

; ALPTAX (IBMRES.ASM 621) and RESLSX (651): the extended words.
ALPTAX:         .word   ATABX, BTABX, CTABX, DTABX, ETABX, FTABX, GTABX, HTABX
                .word   ITABX, JTABX, KTABX, LTABX, MTABX, NTABX, OTABX, PTABX
                .word   QTABX, RTABX, STABX, TTABX, UTABX, VTABX, WTABX, XTABX
                .word   YTABX, ZTABX
RESLSX:

ATABX:
                .byte   0
BTABX:
                .byte   0
CTABX:
                Q       "CHAIN", CHAIN_XS
                QF      "CVI", CVI_XF
                QF      "CVS", CVS_XF
                QF      "CVD", CVD_XF
                Q       "COMMON", COMMON_XS
                Q       "COM", COM_XS
                Q       "CIRCLE", CIRCLE_XS
                Q       "CALLS", CALLS_XS
                .byte   0
DTABX:
                Q       "DATE$", DATES_XS
                Q       "DRAW", DRAW_XS
                .byte   0
ETABX:
                .byte   0
FTABX:
                Q       "FIELD", FIELD_XS
                Q       "FILES", FILES_XS
                .byte   0
GTABX:
                Q       "GET", GET_XS
                .byte   0
HTABX:
                .byte   0
ITABX:
                Q       "IOCTL", IOCTL_XS
                .byte   0
JTABX:
                .byte   0
KTABX:
                Q       "KILL", KILL_XS
                .byte   0
LTABX:
                Q       "LSET", LSET_XS
                Q       "LCOPY", LCOPY_XS
                .byte   0
MTABX:
                QF      "MKI$", MKIS_XF
                QF      "MKS$", MKSS_XF
                QF      "MKD$", MKDS_XF
                .byte   0
NTABX:
                Q       "NAME", NAME_XS
                .byte   0
OTABX:
                .byte   0
PTABX:
                Q       "PUT", PUT_XS
                Q       "PAINT", PAINT_XS
                Q       "PLAY", PLAY_XS
                .byte   0
QTABX:
                .byte   0
RTABX:
                Q       "RSET", RSET_XS
                Q       "RESET", RESET_XS
                .byte   0
STABX:
                Q       "SYSTEM", SYSTEM_XS
                .byte   0
TTABX:
                Q       "TIME$", TIMES_XS
                .byte   0
UTABX:
                .byte   0
VTABX:
                .byte   0
WTABX:
                .byte   0
XTABX:
                .byte   0
YTABX:
                .byte   0
ZTABX:
                .byte   0
