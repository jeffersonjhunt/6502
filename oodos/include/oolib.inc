; OOLIB, the library linked into a program (hw/oolib.md, "Proposed: three
; layers", 3; question 9): the routines with nothing to share, which call
; OODOS's functions 80-82 for what is shared. Link a program with
; build/oolib.lib (oodos/Makefile's OOLIB_USERS) and include this.
;
; .sid tunes (lib/sid.s, L3): a C64 PSID file in the program's memory
; played on the sound card's 6510, as the emulator's own player plays one
; (sid_file.h): the header read, a page found for a player stub that stands
; in for what the C64's KERNAL leaves a tune, the stub built, and the tune,
; the stub, the KERNAL's interrupt exits and the vectors loaded into the
; card's RAM by function 82 (SN_LOAD), then the 6510 started (SN_START).
; PSID only: an RSID expects a whole C64 and is refused.
;
;   SID_PARSE   A/X the file's bytes, SID_LEN its length (set it first):
;               carry clear and the SID_* fields below; carry set and A an
;               error (SE_*)
;   SID_PLAN    A the song, 1-based (0: the file's start song): the stub
;               built for it, on a page the tune leaves free; carry set and
;               A an error. SID_SONG the song, SID_STUB the stub's page's
;               address, SID_CTRL CTRL's bits for SN_START
;   SID_PLAY    the 6510 claimed (function 82), stopped, loaded and
;               started: carry set and A an error (SE_CLAIM: the card held,
;               or none; SE_LOAD: a load refused)
;   SID_STOP    the 6510 stopped, the SIDs silenced, the claim released
;
; The tune's memory must be one run of physical memory (a .COM's $0000-
; $BFFF is: blocks 0-2). Every routine changes A, X and Y.

.ifndef OOLIB_INC
OOLIB_INC = 1

.global SID_PARSE, SID_PLAN, SID_PLAY, SID_STOP
.global SID_LEN, SID_VERSION, SID_LOAD, SID_INIT, SID_PLAYA, SID_SONGS, SID_START, SID_FLAGS
.global SID_SONG, SID_STUB, SID_STUBLEN, SID_CTRL, SID_NAME, SID_AUTHOR, SID_DATA, SID_DLEN

; The errors (A, carry set)
SE_NOTSID       = 1                    ; no PSID or RSID header
SE_RSID         = 2                    ; an RSID: PSID only
SE_VERSION      = 3                    ; a version other than 1-4
SE_OFFSET       = 4                    ; the data's offset isn't its version's
SE_NOCODE       = 5                    ; no code after the header
SE_PASTEND      = 6                    ; the code runs past $FFFF
SE_SONGS        = 7                    ; a song count of 0 or over 256
SE_SONG         = 8                    ; no such song
SE_MUS          = 9                    ; a Compute! MUS file: it needs a player of its own
SE_VECTORS      = 10                   ; the tune covers the 6510's vectors
SE_NOPAGE       = 11                   ; no page free for the stub
SE_CLAIM        = 12                   ; the 6510 claimed already, or no sound card
SE_LOAD         = 13                   ; a load into the card's RAM refused

.endif
