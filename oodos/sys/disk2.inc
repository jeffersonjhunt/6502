; The Disk II card's switches in native mode (hw/oodos.md, "The plan,
; phase 6"; hw/design.md, the Disk II controller clone): slot 6's sixteen,
; in the same order, at DISK2 + n (backplane.inc's DISK2, $C600). Offsets,
; not addresses, so backplane.inc stays as the Technical Reference has it;
; they move there with their entries once both are merged.
;
; A read of an even one gives the data register, of an odd one $FF; any
; access works the switch, a write puts its byte on the card's bus. Only
; plain absolute loads and stores may touch them, or an absolute,X load
; that stays inside the card's page (the W65C02S makes it once, no read
; before it): an indexed access across a page reads another address
; first, and a read-modify-write touches it twice.

D2_PH0OFF       = $00                  ; stepper phase 0 off, then on; 1-3 the same
D2_PH0ON        = $01
D2_PH1OFF       = $02
D2_PH1ON        = $03
D2_PH2OFF       = $04
D2_PH2ON        = $05
D2_PH3OFF       = $06
D2_PH3ON        = $07
D2_MOTOROFF     = $08                  ; off: the card keeps it turning a second more
D2_MOTORON      = $09
D2_DRIVE1       = $0A
D2_DRIVE2       = $0B
D2_Q6OFF        = $0C                  ; read: the data register; write mode: shift
D2_Q6ON         = $0D                  ; sense write-protect; write mode: load
D2_Q7OFF        = $0E                  ; read mode
D2_Q7ON         = $0F                  ; write mode
