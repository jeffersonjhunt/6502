; Backplane I/O addresses (hw/design.md). In native mode module n's registers
; are at $C000 + n * $80, in the I/O hole $C000-$C7FF.

; North bridge (module D): the four 16 KB windows, VEC and the rest.
NB              = $C680
NB_WIN0         = NB + $00             ; CPU $0000-$3FFF
NB_WIN1         = NB + $01             ; CPU $4000-$7FFF
NB_WIN2         = NB + $02             ; CPU $8000-$BFFF
NB_WIN3         = NB + $03             ; CPU $C000-$FFFF
NB_VEC          = NB + $04             ; $C000-$FFFF while VPB is low
NB_MODE         = NB + $05
NB_STATUS       = NB + $06
NB_IRQL         = NB + $08
NB_IRQH         = NB + $09
NB_ID           = NB + $7F

NB_ROM          = $80                  ; window bit: flash, not RAM
NB_RO           = $40                  ; window bit: read-only
NB_STATUS_BOOTSEL = $01                ; booted from RAM (the bus spy loaded it)

; STAND-IN terminal: a 6551 in the serial card's block (module 6) until the
; TL16C552A card exists.
ACIA            = $C300
ACIA_DATA       = ACIA + 0
ACIA_STATUS     = ACIA + 1
ACIA_CMD        = ACIA + 2
ACIA_CTRL       = ACIA + 3

; DMA controller (module 8): channel 3 does the BIOS's memory-to-memory copies.
DMA             = $C400
DMA3_ADDR       = DMA + $30            ; source, 20 bits
DMA3_COUNT      = DMA + $34
DMA3_MODE       = DMA + $36
DMA3_CTRL       = DMA + $37
DMA3_DEST       = DMA + $38            ; destination, 20 bits
DMA_CONTROL     = DMA + $41
DMA_ID          = DMA + $7F

DMA_M2M         = $08                  ; MODE: memory to memory
DMA_GO          = $03                  ; CTRL: ENABLE + REQ
DMA_ON          = $71                  ; CONTROL: master enable, no burst limit
DMA_ID_V1       = $81

; Video card (module 9): VGA 640x480 from banks 8-B.
VIDEO           = $C480
VIDEO_CTRL      = VIDEO + $00
VIDEO_BORDER    = VIDEO + $01
VIDEO_PALBASE   = VIDEO + $02
VIDEO_SPRBASE   = VIDEO + $03
VIDEO_STATUS    = VIDEO + $08          ; bit 7: in vertical blank
VIDEO_L0        = VIDEO + $10          ; layer 0
VIDEO_L0_CONFIG = VIDEO_L0 + 0
VIDEO_L0_MAP    = VIDEO_L0 + 1
VIDEO_L0_TILE   = VIDEO_L0 + 2
VIDEO_L0_HSCROLL = VIDEO_L0 + 3
VIDEO_L1        = VIDEO + $20          ; layer 1, the same registers
VIDEO_ID        = VIDEO + $7F

VIDEO_ON_L0     = $03                  ; CTRL: display on, layer 0 on
VIDEO_TEXT80B   = $80                  ; CONFIG: text 80x30, attribute bit 7 blinks
VIDEO_TEXT40    = $01                  ; CONFIG: text 40x30
VIDEO_TILE      = $02                  ; CONFIG: tiles, 8x8 at 4 bits...
VIDEO_MAP_W64   = $20                  ; ...from a map 64 wide (32 high)
VIDEO_ALL       = $0F                  ; CTRL: display, both layers, sprites
VIDEO_ID_V1     = $91

; Zero page the BIOS keeps for itself (the Woz Monitor has $24-$2B).
BIOS_ZP         = $E0                  ; $E0-$E7: the screen (video.s)
