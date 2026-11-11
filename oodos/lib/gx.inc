; OOLIB's graphics modules' own (lib/gx*.s): video card revision 2's
; registers a program may write (hw/video-rev2.md; backplane.inc's VIDEO_*
; names, which programs don't include), and what the modules share. The
; interface is include/oolib.inc's and vmem.inc's.

.ifndef GX_INC
GX_INC = 1

VID             = $C480                ; the video card, revision 2 (ID $92)
VID_CTRL        = VID + $00            ; bit 0 display, 1 layer 0, 2 layer 1, 3 sprites
VID_BORDER      = VID + $01
VID_ISR         = VID + $05            ; bit 2 a collision: $04 written clears it
VID_STATUS      = VID + $08            ; bits 3-0 the groups that collided
VID_PALBASE     = VID + $0A            ; 2 bytes: a card page (card address / 256)
VID_SPRBASE     = VID + $0C            ; the same
VID_L0          = VID + $10            ; a layer: +0 CONFIG, +1-2 MAPBASE, +3-4 TILEBASE, +5-6 HSCROLL,
VL_CONFIG       = 0                    ;   +7-8 VSCROLL, +9 PALOFS; layer 1 at $20 on
VL_MAP          = 1
VL_TILE         = 3
VL_HSCROLL      = 5
VL_VSCROLL      = 7
VL_PALOFS       = 9
VID_SLOT1       = VID + $32            ; aperture slot 1's bank, 2 bytes (the W1 contract: OOLIB's)
VID_COLLIDE     = $04
WIN1            = $C681                ; the north bridge's W1 (the W1 contract)
GX_IFACE        = 8                    ; the interface version the library needs (video card revision 2)

; gxmem.s's, which every module takes
.globalzp GXP, GXQ, GXT
.global GXDMA, GXB, GXPHYS, GXCPU, GXTSB, GXGO, GXTAKE, GX_AREA, GX_OK
GX_EV_VBGO      = 12                   ; oodos.inc's EV_VBGO

.endif
