; OOLIB, the library linked into a program (hw/oolib.md, "Proposed: three
; layers", 3; question 9): the routines with nothing to share, which call
; OODOS's functions 80-82 for what is shared. Link a program with
; build/oolib.lib (oodos/Makefile's OOLIB_USERS) and include this.
;
; Graphics (lib/gx*.s, L4): below, after the .sid tunes.
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
;   SID_PLAN    A/X the song, 1-based, up to 256 (0: the file's start
;               song): the stub built for it, on a page the tune leaves free;
;               carry set and A an error. SID_SONG the song (a word),
;               SID_STUB the stub's page's address, SID_CTRL CTRL's bits for
;               SN_START
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

; ---------------------------------------------------------------------------
; Graphics (lib/gx*.s, L4): the video card's layers, palette, sprites,
; tiles and bitmaps, video memory handed out, and asset files loaded. The
; card's memory is reached two ways: by function 83 (copies, fills, loads,
; the vertical-blank slots), and by the CPU through W1, which the library
; maps itself under the W1 contract (hw/oolib.md, L4; interface version 4):
; while it holds a video block the program's own $4000-$7FFF is out of
; reach, so the program's code, its event area, its FCBs and what it draws
; from stay in $0300-$3FFF or $8000-$BFFC. GX_W1BACK puts block 1 back.
;
; Coordinates and values the routines take are in the GX_* variables
; below (words, low byte first); a routine says which. Carry set and A an
; error (GE_*) for a refusal. Every routine changes A, X and Y, and W1.
;
;   GX_INIT     A/X the event area the program opened (0 none: no slots,
;               so no GX_PALSHOW or GX_SPRSHOW): OODOS's interface checked
;               (4 at least, with DMA), the W1 map read (function 83's
;               DM_INFO), video memory's units 13-255 all free
;   GX_REMAP    the W1 map read again: after the program allocates or frees
;               a block (functions 53-58)
;   GX_W1BACK   block 1 back in W1
;   GX_BLOCK    A a block: put in W1 if the W1 map has it (carry set if not)
;   GX_ALLOC    A units (1-243): A the first, aligned (to 8 for 8 or more,
;               else to the largest power of two at most A); carry set if
;               no run is free
;   GX_FREE     A the first unit, X the count
;   GX_UNIT     A a unit: GX_A its physical address
;   GX_LAYER    A the layer (0, 1), X its CONFIG, Y its map unit (MAPBASE),
;               GX_V's low byte its tile or font unit: scrolls and PALOFS 0
;   GX_SHOW     A CTRL's layer and sprite bits ($02 layer 0, $04 layer 1,
;               $08 sprites): the display on with them
;   GX_BORDER   A the border's palette entry
;   GX_PALBASE  A the palette's unit (PALBASE); GX_SPRBASE the sprite table's
;   GX_CONSOLE  the console's screen back (function 83's DM_CONSOLE), W1 on
;               block 1
;   GX_PALINIT  a unit for the palette, PALBASE on it, its 512-byte shadow
;               bound to vertical-blank slot 1
;   GX_PALSET   A an entry, X its GGGGBBBB byte, Y its 0000RRRR: in the shadow
;   GX_PALLOAD  A/X 512 bytes (256 entries) into the shadow
;   GX_PALSHOW  the shadow to the card at the next vertical blank (slot 1);
;               with no event area, refused (GE_AREA), as GX_FADE and
;               GX_SPRSHOW are
;   GX_FADE     A frames (1-255), GX_V the address of the 512-byte palette to
;               end at: the shadow faded to it a step a frame, shown each frame
;   GX_SPRINIT  a unit for the sprite table, SPRBASE on it, its 512-byte
;               shadow (all off) bound to slot 0
;   GX_SPRPOS   A a sprite (0-63): X and Y from GX_X, GX_Y (0-1023)
;   GX_SPRIMG   A a sprite: its image from GX_V (bits 0-11: the address /
;               64; bit 15: 8 bits a pixel)
;   GX_SPRATTR  A a sprite, X its flags byte (collision groups << 4, depth
;               << 2, V flip << 1, H flip), Y its size byte (height << 6,
;               width << 4, palette offset)
;   GX_SPRSHOW  the shadow to the card at the next vertical blank (slot 0)
;   GX_COLL     A the groups that collided in the last frame (STATUS);
;               carry set if the card flagged a collision (ISR bit 2,
;               cleared)
;   GX_TMAP     A a map's unit, X its width in entries (32, 64)
;   GX_TPUT     X the column, Y the row: GX_V's entry put in the map
;   GX_TGET     X the column, Y the row: A/X the entry
;   GX_TFILL    X the rows: every entry of them GX_V (by DMA); 0 rows, nothing
;   GX_SCROLL   A a layer: its scrolls GX_X and GX_Y (0-1023)
;   GX_BMP      A the bitmap (GB_*), X its first unit: what GX_PLOT and the
;               rest draw on
;   GX_PLOT     A a colour at (GX_X, GX_Y); off the bitmap, nothing
;   GX_POINT    A the colour at (GX_X, GX_Y); 0 off the bitmap
;   GX_LINE     A a colour from (GX_X, GX_Y) to (GX_X2, GX_Y2)
;   GX_RECT     A a colour: the rectangle's outline, corners (GX_X, GX_Y)
;               and (GX_X2, GX_Y2)
;   GX_FRECT    A a colour: the rectangle filled (whole rows by DM_FILL)
;   GX_CIRCLE   A a colour: the circle about (GX_X, GX_Y), radius GX_R
;   GX_FLOOD    A a colour: the region of (GX_X, GX_Y)'s colour around it,
;               4-connected, filled by spans; carry set if its stack ran out
;               (what it reached filled)
;   GX_BCOPY    the rectangle at (GX_X, GX_Y), GX_X2 wide and GX_Y2 high,
;               copied by DMA to (GX_DX, GX_DY): 8 bits a pixel, or 4 with
;               even x and widths; GE_BOUNDS otherwise (mono, an odd x or
;               width at 4 bits), and for a rectangle that leaves the
;               bitmap (a copy isn't clipped, as
;               a draw is, since clipping one side moves the other); a width
;               or height of 0, nothing. Rows overlapping downward are copied
;               from the last, but each row is one copy forward, as memcpy,
;               not memmove: on the same rows with GX_DX > GX_X the row
;               repeats its first bytes rather than moving
;   GX_FLIP     A a layer, X the unit it shows from the next frame (MAPBASE,
;               latched at the vertical blank), then that frame waited for
;   GX_FONT     A/X a 4 KB font (256 characters of 16 bytes) in the
;               program's memory, or 0: the console's, a character at a time by DMA
;   GX_TEXT     A/X a string ending in 0, drawn at (GX_X, GX_Y) in GX_FG,
;               on GX_BG if GX_OPAQUE isn't 0; GX_X moved on 8 a character
;   GX_ASSET    A/X an FCB (drive and name set): the asset file opened, its
;               header read into GX_HDR and each part loaded by function
;               83's DM_LOAD to the unit it names, or to units allocated
;               for it ($FF), which GX_HDR's part then says; a sprite
;               table's image numbers moved on by the sprite images' unit.
;               Each part is checked first (its type, its record on 1 KB,
;               its records its bytes', its units free and the program's):
;               GE_FORMAT or GE_MEMORY, and every unit the file took freed
;
; An asset file (format 3; question 8, oodos/tools/mkasset.py): record 0 a
; 16-byte header and up to 7 parts of 16 bytes; each part from a 1 KB
; boundary of the file, filling whole units.

.global GX_INIT, GX_REMAP, GX_W1BACK, GX_BLOCK, GX_ALLOC, GX_FREE, GX_UNIT
.global GX_LAYER, GX_SHOW, GX_BORDER, GX_CONSOLE, GX_PALBASE, GX_SPRBASE
.global GX_PALINIT, GX_PALSET, GX_PALLOAD, GX_PALSHOW, GX_FADE
.global GX_SPRINIT, GX_SPRPOS, GX_SPRIMG, GX_SPRATTR, GX_SPRSHOW, GX_COLL
.global GX_TMAP, GX_TPUT, GX_TGET, GX_TFILL, GX_SCROLL
.global GX_BMP, GX_PLOT, GX_POINT, GX_LINE, GX_RECT, GX_FRECT, GX_CIRCLE, GX_FLOOD, GX_BCOPY, GX_FLIP
.global GX_FONT, GX_TEXT, GX_ASSET
.global GX_X, GX_Y, GX_X2, GX_Y2, GX_DX, GX_DY, GX_R, GX_V, GX_A, GX_FG, GX_BG, GX_OPAQUE
.global GX_W1MAP, GX_UNITS, GX_HDR, GX_PALU, GX_SPRU, GX_PAL, GX_SPR

GB_256          = 0                    ; GX_BMP: bitmap 256 (256 x 240, 8 bits a pixel, 256 bytes a row)
GB_512          = 1                    ;   bitmap 512 (512 x 480, 4 bits, 256 bytes a row)
GB_MONO         = 2                    ;   mono (640 x 480, 1 bit, 80 bytes a row)

GE_VERSION      = 1                    ; OODOS's interface below 4, or no DMA area
GE_MAP          = 2                    ; a block the W1 map hasn't
GE_MEMORY       = 3                    ; no units free
GE_DMA          = 4                    ; function 83 refused
GE_FILE         = 5                    ; the asset file not found or not read
GE_FORMAT       = 6                    ; not an asset file of format 3, or a part the library can't take
GE_AREA         = 7                    ; no event area (GX_INIT's 0): no vertical-blank slot to show by
GE_BOUNDS       = 8                    ; GX_BCOPY: a rectangle that leaves the bitmap

; The asset file's header (GX_HDR, record 0) and its parts.
AH_MAGIC        = 0                    ; 'O', 'G', 'X'
AH_FORMAT       = 3                    ; 3
AH_PARTS        = 4                    ; 1-7
AH_LEN          = 16                   ; the header; the parts follow, AP_LEN each
AP_TYPE         = 0                    ; AT_*
AP_CONFIG       = 1                    ; the layer's CONFIG bits it was made for (a layer's part)
AP_UNIT         = 2                    ; its unit, $FF any (GX_ASSET writes the one it took)
AP_RECORD       = 4                    ; its first record in the file, 2 bytes (a multiple of 8)
AP_RECORDS      = 6                    ; its records, 2 bytes
AP_BYTES        = 8                    ; its length in bytes, 3 bytes (17 bits)
AP_W            = 11                   ; its width and height, as its type counts them (a map's
AP_H            = 12                   ;   entries, a bitmap's or sprite's pixels / 8); +13-15 0
AP_LEN          = 16
AP_MAX          = 7
AT_MAP          = 1                    ; a tile map (2-byte entries)
AT_TILES        = 2                    ; tile images
AT_PALETTE      = 3                    ; 256 12-bit entries, 512 bytes
AT_SPRITES      = 4                    ; sprite images
AT_SPRTABLE     = 5                    ; a sprite table, 64 entries of 8 bytes
AT_BITMAP       = 6                    ; a bitmap (its CONFIG says which)
AT_FONT         = 7                    ; 256 characters of 16 bytes
AT_RAW          = 8                    ; anything else
ASSET_FORMAT    = 3

.endif
