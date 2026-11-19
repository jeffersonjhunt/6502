; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWDATA.ASM's messages, in block P2: the error table ERRPRT walks (each
; message to its zero, in their numbers' order; a "?" is a number with no
; message: Unprintable error), and " in " and "Break". The disk errors, 50
; to 74, follow the others (1 to 30) directly. OO-1: the texts are the IBM
; BASIC Reference 3.0's, Appendix A's (the IBM spec's fix-up, the owner's
; G15: 13 of them in its sentence case, where the 1983 table has "Out of
; DATA", "Duplicate Definition", "FIELD overflow", "Disk write protected"
; ...); the numbers are the source's. 75 and 76, which the 1983 table ends
; before, are the manual's too, for the directory hook (p4/oem.s).

.include "gwbasic.inc"

.export ERRTAB, INTXT, BRKTXT

.rodata

ERRTAB:         .byte   0
                .byte   "NEXT without FOR", 0           ; 1
                .byte   "Syntax error", 0               ; 2
                .byte   "RETURN without GOSUB", 0       ; 3
                .byte   "Out of data", 0                ; 4
                .byte   "Illegal function call", 0      ; 5
                .byte   "Overflow", 0                   ; 6
                .byte   "Out of memory", 0              ; 7
                .byte   "Undefined line number", 0      ; 8
                .byte   "Subscript out of range", 0     ; 9
                .byte   "Duplicate definition", 0       ; 10
                .byte   "Division by zero", 0           ; 11
                .byte   "Illegal direct", 0             ; 12
                .byte   "Type mismatch", 0              ; 13
                .byte   "Out of string space", 0        ; 14
                .byte   "String too long", 0            ; 15
                .byte   "String formula too complex", 0 ; 16
                .byte   "Can't continue", 0             ; 17
                .byte   "Undefined user function", 0    ; 18
                .byte   "No RESUME", 0                  ; 19
                .byte   "RESUME without error", 0       ; 20
                .byte   "Unprintable error", 0          ; 21
                .byte   "Missing operand", 0            ; 22
                .byte   "Line buffer overflow", 0       ; 23
                .byte   "Device timeout", 0             ; 24
                .byte   "Device fault", 0               ; 25
                .byte   "FOR without NEXT", 0           ; 26
                .byte   "Out of paper", 0               ; 27
                .byte   "?", 0                          ; 28
                .byte   "WHILE without WEND", 0         ; 29
                .byte   "WEND without WHILE", 0         ; 30
                .byte   "Field overflow", 0             ; 50
                .byte   "Internal error", 0             ; 51
                .byte   "Bad file number", 0            ; 52
                .byte   "File not found", 0             ; 53
                .byte   "Bad file mode", 0              ; 54
                .byte   "File already open", 0          ; 55
                .byte   "?", 0                          ; 56
                .byte   "Device I/O error", 0           ; 57
                .byte   "File already exists", 0        ; 58
                .byte   "?", 0                          ; 59
                .byte   "?", 0                          ; 60
                .byte   "Disk full", 0                  ; 61
                .byte   "Input past end", 0             ; 62
                .byte   "Bad record number", 0          ; 63
                .byte   "Bad file name", 0              ; 64
                .byte   "?", 0                          ; 65
                .byte   "Direct statement in file", 0   ; 66
                .byte   "Too many files", 0             ; 67
                .byte   "Device unavailable", 0         ; 68
                .byte   "Communication buffer overflow", 0 ; 69
                .byte   "Disk write protect", 0         ; 70
                .byte   "Disk not ready", 0             ; 71
                .byte   "Disk media error", 0           ; 72
                .byte   "Advanced feature", 0           ; 73
                .byte   "Rename across disks", 0        ; 74
                .byte   "Path/file access error", 0     ; 75 OO-1: not in the 1983 table
                .byte   "Path not found", 0             ; 76 OO-1: not in the 1983 table

INTXT:          .byte   " in ", 0
BRKTXT:         .byte   "Break", 0
