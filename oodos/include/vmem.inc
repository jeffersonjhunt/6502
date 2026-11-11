; Video card revision 2's memory, as OODOS and OOLIB share it out
; (hw/video-rev2.md, "Card memory layout (frozen)"; hw/oolib.md, "Video card
; revision 2"). Proposed. oolib.inc includes this; a program that links
; OOLIB's graphics modules (lib/gx*.s) gets it with oolib.inc.
;
; The card has memory of its own (2 MB, MEMSIZE x 64 KB), not system RAM.
; Addresses in it are card addresses, 24 bits. The base registers (PALBASE,
; SPRBASE, a layer's MAPBASE and TILEBASE) and a sprite's image address hold
; a card address in 256-byte units: a card page here. The aperture's slots
; show card memory in 16 KB pieces: a card bank here (SLOTn's value).
;
; Who owns what:
;
;   $000000-$03FFFF  OODOS's (the BIOS's before it): the palette, the font,
;                    the console's text, its video units 0-12 ($000000-
;                    $0033FF) and the rest kept for OODOS. A program reads
;                    it (function 83's card sources, the console's font for
;                    text on a bitmap) and never writes it.
;   $040000-$1BFFFF  the running program's, handed out by OOLIB (below), as
;                    its TPA is: whole while it runs, nothing kept after.
;   $1C0000-$1FFFFF  Apple II mode's: the banks 0-1 mirror, the IIe's
;                    character ROM at $1E0000. Never a program's.
;
; How a program reaches its part, all through OOLIB:
;
;   - The CPU, through aperture slot 1 in W1 (GX_VMAP), under the W1
;     contract of interface version 8: OOLIB sets SLOT1 to one of the
;     program's banks and puts block 33 (slot 1's window, physical $84000-
;     $87FFF) in W1. While it is there the program's own $4000-$7FFF is out
;     of reach, as before: its code, its event area, the FCBs and blocks it
;     passes and what it draws from stay in $0300-$3FFF or $8000-$BFFC.
;     The warm boot (and the cold boot, which ends in it) puts slot 1 on
;     the program's first bank (VM_PROG_BANK), so block 33 never shows
;     OODOS's part, even before GX_VMAP. Slot 0 is OODOS's: it shows bank
;     0, the console, always, and no program writes SLOT0. Slots 2 and 3
;     are OODOS's too, on banks 2 and 3 but inside function 83's DM_VCOPY,
;     which borrows them and puts them back; OOLIB doesn't use them, their
;     windows' blocks (34, 35) aren't in DM_INFO's W1 map, and no physical
;     destination of function 83 may touch them ($88000-$8FFFF).
;   - Bulk copies, fills and the vertical-blank copies through the card's
;     data port by DMA channel 1, OODOS's: function 83 (oodos.inc's DM_*).
;     A program never writes the port's registers itself.
;
; The routines (lib/gxmem.s; every one changes A, X and Y and may change
; W1; carry set and A an error, oolib.inc's GE_*, for a refusal):
;
;   GX_VALLOC   GX_V the size in card pages (1-6,144), A the alignment in
;               card pages (a power of two, 1-128; 0 is 1): A/X the first
;               page of a run of the program's card memory, aligned, and
;               GX_A its card address; GE_MEMORY if no run is free. Taken in
;               whole kilobytes (VM_UNIT), so a size or an alignment under 4
;               pages takes 4
;   GX_VFREE    A/X the first card page, GX_V the size in pages: that run
;               free again (pages outside the program's part are skipped)
;   GX_VADDR    A/X a card page: GX_A its card address (page x 256)
;   GX_VMAP     A/X a card page: its bank shown in slot 1 and block 33 put
;               in W1; A/X and GXP the page's CPU address ($4000-$7F00).
;               GE_MAP for a page outside the program's part
;   GX_W1BACK   block 1 back in W1 (slot 1 left as it is)
;   GX_VPUT     A/X a CPU address in the program's memory, GX_A a card
;               address, GX_V the length (1-65,535; 0 nothing): copied to
;               the card through the port (function 83's DM_VWRITE)
;   GX_VGET     the same the other way: GX_A's card bytes to A/X
;   GX_VCOPY    GX_A the source's card address (anywhere in the fitted
;               memory), GX_C the destination's (the program's part), GX_V
;               the length (0 nothing): card to card (function 83's
;               DM_VCOPY). Down over itself exact; up over itself, each byte
;               copied forward, as DM_COPY is
;   GX_VFILL    A the byte, GX_A a card address, GX_V the length (0
;               nothing): filled through the port (DM_VFILL)
;
; And for pointing the card at what was allocated (lib/gxmode.s, gxspr.s;
; oolib.inc says the rest): GX_LAYER takes the map's card page in GX_V and
; the tiles' in GX_R, GX_PALBASE and GX_SPRBASE a card page in A/X, GX_FLIP
; a card page in GX_V, and GX_SPRIMG an image address in card pages (bits
; 0-14) with bit 15 for 8 bits a pixel, which is the sprite table's own
; encoding.

.ifndef VMEM_INC
VMEM_INC = 1

VM_OODOS        = $000000              ; OODOS's part of card memory
VM_PALETTE      = $000000              ;   the console's palette (512 bytes)
VM_FONT         = $000400              ;   its font (4 KB, 256 characters of 16 bytes)
VM_TEXT         = $002000              ;   its 80 x 30 text, a character and an attribute a cell
VM_CONSOLE_END  = $003400              ;   past the console's units 0-12
VM_PROG         = $040000              ; the program's part, handed out by OOLIB
VM_PROG_END     = $1C0000
VM_APPLE        = $1C0000              ; Apple II mode's, never a program's
VM_APPLE_END    = $200000

VM_PAGE         = 256                  ; a card page: what the base registers count
VM_BANK         = $4000                ; a card bank: what an aperture slot shows
VM_UNIT         = 1024                 ; OOLIB's allocation grain: four pages
VM_PROG_PAGE    = VM_PROG / VM_PAGE    ; $0400: the program's first card page
VM_PROG_PAGES   = (VM_PROG_END - VM_PROG) / VM_PAGE ; 6,144
VM_PROG_BANK    = VM_PROG / VM_BANK    ; 16: the program's banks 16-111
VM_PROG_BANKS   = (VM_PROG_END - VM_PROG) / VM_BANK
VM_UNITS        = (VM_PROG_END - VM_PROG) / VM_UNIT ; 1,536: GX_VALLOC's map, a bit each

VM_SLOT         = 1                    ; the aperture slot OOLIB maps (SLOT1)
VM_SLOT_BLOCK   = 33                   ; its window's block: physical $84000-$87FFF
VM_SLOT_CPU     = $4000                ; where W1 shows it
VM_APERTURE     = $80000               ; the aperture: slot n's window at $80000 + n x $4000

.global GX_VALLOC, GX_VFREE, GX_VADDR, GX_VMAP, GX_W1BACK
.global GX_VPUT, GX_VGET, GX_VCOPY, GX_VFILL
.global GX_A, GX_C, GX_V
.globalzp GXP

.endif
