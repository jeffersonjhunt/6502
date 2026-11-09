; OOLIB's graphics modules' own (lib/gx*.s): the video card's registers a
; program may write (Technical Reference chapter 6; backplane.inc's VIDEO_*
; names, which programs don't include), and what the modules share. The
; interface is include/oolib.inc's.

.ifndef GX_INC
GX_INC = 1

VID             = $C480                ; the video card
VID_CTRL        = VID + $00            ; bit 0 display, 1 layer 0, 2 layer 1, 3 sprites
VID_BORDER      = VID + $01
VID_PALBASE     = VID + $02            ; 1 KB units of video memory
VID_SPRBASE     = VID + $03
VID_ISR         = VID + $05            ; bit 2 a collision: $04 written clears it
VID_STATUS      = VID + $08            ; bits 3-0 the groups that collided
VID_L0          = VID + $10            ; a layer: +0 CONFIG, +1 MAPBASE, +2 TILEBASE, +3-4 HSCROLL,
VL_CONFIG       = 0                    ;   +5-6 VSCROLL, +7 PALOFS; layer 1 at $10 on
VL_MAP          = 1
VL_TILE         = 2
VL_HSCROLL      = 3
VL_VSCROLL      = 5
VL_PALOFS       = 7
VID_COLLIDE     = $04
WIN1            = $C681                ; the north bridge's W1 (the W1 contract)
VRAM            = $80000               ; video memory, banks 8-B: unit n at VRAM + n x 1,024
UNIT_FIRST      = 13                   ; DM_INFO's: units 0-12 are the console's

; gxmem.s's, which every module takes
.globalzp GXP, GXQ, GXT
.global GXDMA, GXB, GXPHYS, GXCPU, GXTSB, GXGO, GXTAKE, GX_AREA, GX_OK
GX_EV_VBGO      = 12                   ; oodos.inc's EV_VBGO

.endif
